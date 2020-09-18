/******************************************************************
    Copyright (C) 2009-2015  Henrik Carlqvist

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
******************************************************************/
#ifdef __linux__
#include <linux/hdreg.h>
#include <linux/fd.h>
#endif
/* Ugly fix for compability with both older libc and newer kernels */
#ifdef __OpenBSD__
#include <sys/types.h>
#endif
#include <sys/mount.h>
#ifdef __linux__
#ifndef BLKGETSIZE
#include <linux/fs.h>
#endif
#endif
/* end of ugly fix */
#include <sys/ioctl.h>

#if defined(HAVE_SYS_DISK_H)
#include <sys/disk.h>
#endif

#include "br.h"
#include "fat12.h"
#include "fat16.h"
#include "fat32.h"
#include "ntfs.h"
#include "oem_id.h"
#include "nls.h"
#include "identify.h"

/* This one should be in stdio.h, however it seems to be missing. Declared here
   to avoid warnings... */
#ifndef fileno
int fileno( FILE *stream);
#endif


/* Returns TRUE if file is a device, otherwise FALSE */
static int is_disk_device(FILE *fp);

/* Returns TRUE if file is a floppy device, otherwise FALSE */
static int is_floppy(FILE *fp);

/* Returns TRUE if file is a partition device, otherwise FALSE */
static int is_partition(FILE *fp);

static int is_disk_device(FILE *fp)
{
   int iRes1;
   int iRes2;
   long lSectors;
   int iFd = fileno(fp);

#ifdef BLKGETSIZE
   struct hd_geometry sGeometry;
   iRes1 = ioctl(iFd, BLKGETSIZE, &lSectors);
#ifdef HDIO_REQ
   iRes2 = ioctl(iFd, HDIO_REQ, &sGeometry);
#else
   iRes2 = ioctl(iFd, HDIO_GETGEO, &sGeometry);
#endif
#endif

#ifdef DIOCGFWSECTORS
   uint start_sector = 0;
   iRes1 = ioctl(iFd, DIOCGFWSECTORS, &start_sector);
   iRes2 = 0;
   lSectors = 0;
#endif

   return ! (iRes1 && iRes2);
} /* is_device */

static int is_floppy(FILE *fp)
{
#ifdef FDGETPRM
   struct floppy_struct sFloppy;

   return ! ioctl(fileno(fp) ,FDGETPRM, &sFloppy);
#endif

#ifdef DIOCGFWHEADS
   int iFd = fileno(fp);
   unsigned heads;
   int iRes1 = ioctl(iFd, DIOCGFWHEADS, &heads);
   if (! iRes1 )
      return heads == 2;
   else
      return 0;
#endif
} /* is_floppy */

static int is_partition(FILE *fp)
{
   int iRes1;
   int iRes2;
   long lSectors;
   int iFd = fileno(fp);

#ifdef BLKGETSIZE
   struct hd_geometry sGeometry;
   iRes1 = ioctl(iFd, BLKGETSIZE, &lSectors);
#ifdef HDIO_REQ
   iRes2 = ioctl(iFd, HDIO_REQ, &sGeometry);
#else
   iRes2 = ioctl(iFd, HDIO_GETGEO, &sGeometry);
#endif

   return (! (iRes1 && iRes2)) && (sGeometry.start);
#endif

#ifdef DIOCGMEDIASIZE
   off_t bytes;
   unsigned int start_sector;
   iRes1 = ioctl(iFd, DIOCGMEDIASIZE, &bytes);
   iRes2 = ioctl(iFd, DIOCGFWSECTORS, &start_sector);
   lSectors = 0;

   return (! (iRes1 && iRes2));
#endif
} /* is_partition */

unsigned long partition_start_sector(FILE *fp)
{
   int iRes1;
   int iRes2;
   long lSectors;
   int iFd = fileno(fp);

#ifdef BLKGETSIZE
   struct hd_geometry sGeometry;
   iRes1 = ioctl(iFd, BLKGETSIZE, &lSectors);
#ifdef HDIO_REQ
   iRes2 = ioctl(iFd, HDIO_REQ, &sGeometry);
#else
   iRes2 = ioctl(iFd, HDIO_GETGEO, &sGeometry);
#endif
   if(! (iRes1 && iRes2) )
      return sGeometry.start;
   else
      return 0L;
#endif

#ifdef DIOCGFWSECTORS
   uint start_sector = 0;
   iRes1 = ioctl(iFd, DIOCGFWSECTORS, &start_sector);
   iRes2 = 0;
   lSectors = 0;
   
   if( ! iRes1 )
      return start_sector;
   else
      return 0L;
#endif
} /* partition_start_sector */

unsigned short partition_number_of_heads(FILE *fp)
{
   int iRes1;
   int iRes2;
   long lSectors;
   int iFd = fileno(fp);

#ifdef BLKGETSIZE
   struct hd_geometry sGeometry;
   iRes1 = ioctl(iFd, BLKGETSIZE, &lSectors);
#ifdef HDIO_REQ
   iRes2 = ioctl(iFd, HDIO_REQ, &sGeometry);
#else
   iRes2 = ioctl(iFd, HDIO_GETGEO, &sGeometry);
#endif
   if(! (iRes1 && iRes2) )
      return (unsigned short) sGeometry.heads;
   else
      return 0;
#endif

#ifdef DIOCGFWHEADS
   unsigned heads;
   iRes1 = ioctl(iFd, DIOCGFWHEADS, &heads);
   iRes2 = 0;
   lSectors = 0;

   if (! iRes1 )
      return (unsigned short) heads;
   else
      return 0;
#endif
} /* partition_number_of_heads */

int sanity_check(FILE *fp, const char *szPath, int iBr, int bPrintMessages)
{
   int bIsDiskDevice = is_disk_device(fp);
   int bIsFloppy = is_floppy(fp);
   int bIsPartition = is_partition(fp);
   switch(iBr)
   {
      case MBR_WIN7:
      case MBR_VISTA:
      case MBR_2000:
      case MBR_95B:
      case MBR_DOS:
      case MBR_SYSLINUX:
      case MBR_GPT_SYSLINUX:
      case MBR_RUFUS:
      case MBR_REACTOS:
      case MBR_KOLIBRIOS:
      case MBR_GRUB4DOS:
      case MBR_GRUB2:
      case MBR_ZERO:
      {
	 if( ! bIsDiskDevice )
	 {
	    if(bPrintMessages)
	    {
	       printf(_("%s does not seem to be a disk device,\n"), szPath);
	       printf(
		  _("use the switch -f to force writing of a master boot record\n"));
	    }
	    return 0;
	 }
	 if( bIsFloppy )
	 {
	    if(bPrintMessages)
	    {
	       printf(_("%s seems to be a floppy disk device,\n"), szPath);
	       printf(
		  _("use the switch -f to force writing of a master boot record\n"));
	    }
	    return 0;
	 }
	 if( bIsPartition )
	 {
	    if(bPrintMessages)
	    {
	       printf(_("%s seems to be a disk partition device,\n"), szPath);
	       printf(
		  _("use the switch -f to force writing of a master boot record\n"));
	    }
	    return 0;
	 }
      }
      break;
      case FAT12_BR:
      {
	 if( ! bIsFloppy )
	 {
	    if(bPrintMessages)
	    {
	       printf(_("%s does not seem to be a floppy disk device,\n"),
		      szPath);
	       printf(
		  _("use the switch -f to force writing of a FAT12 boot record\n"));
	    }
	    return 0;
	 }
	 if( ! is_fat_12_fs(fp))
	 {
	    if(bPrintMessages)
	    {
	       printf(_("%s does not seem to have a FAT12 file system,\n"),
		      szPath);
	       printf(
		  _("use the switch -f to force writing of a FAT12 boot record\n"));
	    }
	    return 0;
	 }
      }
      break;
      case FAT16_BR:
      case FAT16FD_BR:
      case FAT16ROS_BR:
      {
	 if( ! bIsPartition )
	 {
	    if(bPrintMessages)
	    {
	       printf(_("%s does not seem to be a disk partition device,\n"),
		      szPath);
	       printf(
		  _("use the switch -f to force writing of a FAT16 boot record\n"));
	    }
	    return 0;
	 }
	 if( ! is_fat_16_fs(fp))
	 {
	    if(bPrintMessages)
	    {
	       printf(_("%s does not seem to have a FAT16 file system,\n"),
		      szPath);
	       printf(
		  _("use the switch -f to force writing of a FAT16 boot record\n"));
	    }
	    return 0;
	 }
      }
      break;
      case FAT32_BR:
      case FAT32NT_BR:
      case FAT32PE_BR:
      case FAT32FD_BR:
      case FAT32ROS_BR:
      case FAT32KOS_BR:
      {
	 if( ! bIsPartition )
	 {
	    if(bPrintMessages)
	    {
	       printf(_("%s does not seem to be a disk partition device,\n"),
		      szPath);
	       printf(
		  _("use the switch -f to force writing of a FAT32 boot record\n"));
	    }
	    return 0;
	 }
	 if( ! is_fat_32_fs(fp))
	 {
	    if(bPrintMessages)
	    {
	       printf(_("%s does not seem to have a FAT32 file system,\n"),
		      szPath);
	       printf(
		  _("use the switch -f to force writing of a FAT32 boot record\n"));
	    }
	    return 0;
	 }
      }
      break;
      case NTFS_BR:
      {
	 if( ! bIsPartition )
	 {
	    if(bPrintMessages)
	    {
	       printf(_("%s does not seem to be a disk partition device,\n"),
		      szPath);
	       printf(
		  _("use the switch -f to force writing of a NTFS boot record\n"));
	    }
	    return 0;
	 }
	 if( ! is_ntfs_fs(fp))
	 {
	    if(bPrintMessages)
	    {
	       printf(_("%s does not seem to have a NTFS file system,\n"),
		      szPath);
	       printf(
		  _("use the switch -f to force writing of a NTFS boot record\n"));
	    }
	    return 0;
	 }
      }
      break;
      default:
      {
	 if(bPrintMessages)
	 {
	    printf(_("Whoops, internal error, unknown boot record\n"));
	 }
	 return 0;
      }
      break;
   }
   return 1;
} /* sanity_check */

void diagnose(FILE *fp, const char *szPath)
{
   char *pc;
   
   if(is_fat_12_fs(fp))
      printf(_("%s has a FAT12 file system.\n"), szPath);
   if(is_fat_16_fs(fp))
      printf(_("%s has a FAT16 file system.\n"), szPath);
   if(is_fat_32_fs(fp))
      printf(_("%s has a FAT32 file system.\n"), szPath);
   if(is_ntfs_fs(fp))
      printf(_("%s has a NTFS file system.\n"), szPath);
   if(is_br(fp))
      printf(_("%s has an x86 boot sector,\n"), szPath);
   else
   {
      printf(_("%s has no x86 boot sector\n"), szPath);
      return;
   }
   if(entire_ntfs_br_matches(fp))
   {
      printf(
	 _("it is exactly the kind of NTFS boot record this program\n"));
      printf(
	 _("would create with the switch -n on a NTFS partition.\n"));
   }
   else if(entire_fat_12_br_matches(fp))
   {
      printf(
	 _("it is exactly the kind of FAT12 boot record this program\n"));
      printf(
	 _("would create with the switch -1 on a floppy.\n"));
   }
   else if(is_fat_16_br(fp) || is_fat_32_br(fp))
   {
      if(entire_fat_16_br_matches(fp))
      {
	 printf(
	    _("it is exactly the kind of FAT16 DOS boot record this program\n"));
	 printf(
	    _("would create with the switch -6 on a FAT16 partition.\n"));
      }
      else if(entire_fat_16_fd_br_matches(fp))
      {
	 printf(
	  _("it is exactly the kind of FAT16 FreeDOS boot record this program\n"));
	 printf(
	    _("would create with the switch -5 on a FAT16 partition.\n"));
      }
      else if(entire_fat_16_ros_br_matches(fp))
      {
	 printf(
	  _("it is exactly the kind of FAT16 ReactOS boot record this program\n"));
	 printf(
	    _("would create with the switch -o on a FAT16 partition.\n"));
      }
      else if(entire_fat_32_br_matches(fp))
      {
	 printf(
	  _("it is exactly the kind of FAT32 DOS boot record this program\n"));
	 printf(
	    _("would create with the switch -3 on a FAT32 partition.\n"));
      }
      else if(entire_fat_32_nt_br_matches(fp))
      {
	 printf(
	   _("it is exactly the kind of FAT32 NT boot record this program\n"));
	 printf(
	    _("would create with the switch -2 on a FAT32 partition.\n"));
      }
      else if(entire_fat_32_pe_br_matches(fp))
      {
	 printf(
	   _("it is exactly the kind of FAT32 PE boot record this program\n"));
	 printf(
	    _("would create with the switch -e on a FAT32 partition.\n"));
      }
      else if(entire_fat_32_fd_br_matches(fp))
      {
	 printf(
	   _("it is exactly the kind of FAT32 FreeDOS boot record this program\n"));
	 printf(
	    _("would create with the switch -4 on a FAT32 partition.\n"));
      }
      else if(entire_fat_32_ros_br_matches(fp))
      {
	 printf(
	   _("it is exactly the kind of FAT32 ReactOS boot record this program\n"));
	 printf(
	    _("would create with the switch -c on a FAT32 partition.\n"));
      }
      else if(entire_fat_32_kos_br_matches(fp))
      {
	 printf(
	   _("it is exactly the kind of FAT32 KolibriOS boot record this program\n"));
	 printf(
	    _("would create with the switch -q on a FAT32 partition.\n"));
      }
      else
      {
	 printf(
	    _("it seems to be a FAT16 or FAT32 boot record, but it\n"));
	 printf(
	    _("differs from what this program would create with the\n"));
	 printf(_("switch -6, -2, -e or -3 on a FAT16 or FAT32 partition.\n"));
      }
   }
   else if(is_lilo_br(fp))
   {
      printf(_("it seems to be a LILO boot record, please use lilo to\n"));
      printf(_("create such boot records.\n"));
   }
   else if(is_dos_mbr(fp))
   {
      printf(
	 _("it is a Microsoft DOS/NT/95A master boot record, like the one this\n"));
      printf(
	 _("program creates with the switch -d on a hard disk device.\n"));
      printf(
	 _("It has windows disk signature 0x%08x.\n"),
	 read_windows_disk_signature(fp));
   }
   else if(is_dos_f2_mbr(fp))
   {
      printf(
	 _("it is a Microsoft DOS/NT/95A master boot record with the undocumented\n"));
      printf(
	 _("F2 instruction. You will get equal functionality with the MBR this\n"));
      printf(
	 _("program creates with the switch -d on a hard disk device.\n"));
      printf(
	 _("It has windows disk signature 0x%08x.\n"),
	 read_windows_disk_signature(fp));
   }
   else if(is_95b_mbr(fp))
   {
      printf(
	 _("it is a Microsoft 95B/98/98SE/ME master boot record, like the one this\n"));
      printf(
	 _("program creates with the switch -9 on a hard disk device.\n"));
      printf(
	 _("It has windows disk signature 0x%08x.\n"),
	 read_windows_disk_signature(fp));
   }
   else if(is_2000_mbr(fp))
   {
      printf(
	 _("it is a Microsoft 2000/XP/2003 master boot record, like the one this\n"));
      printf(
	 _("program creates with the switch -m on a hard disk device.\n"));
      printf(
	 _("It has windows disk signature 0x%08x.\n"),
	 read_windows_disk_signature(fp));
   }
   else if(is_vista_mbr(fp))
   {
      printf(
	 _("it is a Microsoft Vista master boot record, like the one this\n"));
      printf(
	 _("program creates with the switch -i on a hard disk device.\n"));
      printf(
	 _("It has windows disk signature 0x%08x.\n"),
	 read_windows_disk_signature(fp));
   }
   else if(is_win7_mbr(fp))
   {
      printf(
	 _("it is a Microsoft 7 master boot record, like the one this\n"));
      printf(
	 _("program creates with the switch -7 on a hard disk device.\n"));
      printf(
	 _("It has windows disk signature 0x%08x.\n"),
	 read_windows_disk_signature(fp));
   }
   else if(is_syslinux_mbr(fp))
   {
      printf(
	 _("it is a public domain syslinux master boot record, like the one this\n"));
      printf(
	 _("program creates with the switch -s on a hard disk device.\n"));
      printf(
	 _("It has windows disk signature 0x%08x.\n"),
	 read_windows_disk_signature(fp));
   }
   else if(is_syslinux_gpt_mbr(fp))
   {
      printf(
	 _("it is a GPL syslinux GPT master boot record, like the one this\n"));
      printf(
	 _("program creates with the switch -t on a hard disk device.\n"));
      printf(
	 _("It has windows disk signature 0x%08x.\n"),
	 read_windows_disk_signature(fp));
   }
   else if(is_rufus_mbr(fp))
   {
      printf(
	 _("it is a Rufus master boot record, like the one this\n"));
      printf(
	 _("program creates with the switch -r on a hard disk device.\n"));
      printf(
	 _("It has windows disk signature 0x%08x.\n"),
	 read_windows_disk_signature(fp));
   }
   else if(is_reactos_mbr(fp))
   {
      printf(
	 _("it is a ReactOS master boot record, like the one this\n"));
      printf(
	 _("program creates with the switch -a on a hard disk device.\n"));
      printf(
	 _("It has windows disk signature 0x%08x.\n"),
	 read_windows_disk_signature(fp));
   }
   else if(is_kolibrios_mbr(fp))
   {
      printf(
	 _("it is a KolibriOS master boot record, like the one this\n"));
      printf(
	 _("program creates with the switch -k on a hard disk device.\n"));
      printf(
	 _("It has windows disk signature 0x%08x.\n"),
	 read_windows_disk_signature(fp));
   }
   else if(is_grub4dos_mbr(fp))
   {
      printf(
	 _("it is a Grub4DOS master boot record, like the one this\n"));
      printf(
	 _("program creates with the switch -g on a hard disk device.\n"));
      printf(
	 _("It has windows disk signature 0x%08x.\n"),
	 read_windows_disk_signature(fp));
   }
   else if(is_grub2_mbr(fp))
   {
      printf(
	 _("it is a GRUB 2 master boot record, like the one this\n"));
      printf(
	 _("program creates with the switch -b on a hard disk device.\n"));
      printf(
	 _("It has windows disk signature 0x%08x.\n"),
	 read_windows_disk_signature(fp));
   }
   else if(is_zero_mbr(fp))
   {
      printf(
	 _("it is a zeroed non-bootable master boot record, like the one this\n"));
      printf(
	 _("program creates with the switch -z on a hard disk device.\n"));
   }
   else if(is_zero_mbr_not_including_disk_signature_or_copy_protect(fp))
   {
      printf(
	 _("it is a non-bootable master boot record, almost like the one this\n"));
      printf(
	 _("program creates with the switch -z on a hard disk device but\n"));
      printf(
	 _("with windows disk signature 0x%08x\n"),
	 read_windows_disk_signature(fp));
      printf(
	 _("which this program can write with switch -S and copy protect\n"));
      printf(
	 _("bytes 0x%04x (%s).\n"),
	 read_mbr_copy_protect_bytes(fp),
	 read_mbr_copy_protect_bytes_explained(fp));
   }
   else
      printf(_("it is an unknown boot record\n"));
   pc = read_oem_id(fp);
   if(pc)
      printf(_("The OEM ID is %s\n"), pc);
} /* diagnose */

int smart_select(FILE *fp)
{
   int i;

   for(i=AUTO_BR+1; i<NUMBER_OF_RECORD_TYPES; i++)
      if(sanity_check(fp, "", i, 0))
	 return i;
   return NO_WRITING;
} /* smart_select */
