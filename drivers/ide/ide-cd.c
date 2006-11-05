/*
 * linux/drivers/ide/ide-cd.c
 *
 * Copyright (C) 1994, 1995, 1996  scott snyder  <snyder@fnald0.fnal.gov>
 * Copyright (C) 1996-1998  Erik Andersen <andersee@debian.org>
 * Copyright (C) 1998-2000  Jens Axboe <axboe@suse.de>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * ATAPI CD-ROM driver.  To be used with ide.c.
 * See Documentation/cdrom/ide-cd for usage information.
 *
 * Suggestions are welcome. Patches that work are more welcome though. ;-)
 * For those wishing to work on this driver, please be sure you download
 * and comply with the latest Mt. Fuji (SFF8090 version 4) and ATAPI 
 * (SFF-8020i rev 2.6) standards. These documents can be obtained by 
 * anonymous ftp from:
 * ftp://fission.dt.wdc.com/pub/standards/SFF_atapi/spec/SFF8020-r2.6/PS/8020r26.ps
 * ftp://ftp.avc-pioneer.com/Mtfuji4/Spec/Fuji4r10.pdf
 *
 * Drives that deviate from these standards will be accommodated as much
 * as possible via compile time or command-line options.  Since I only have
 * a few drives, you generally need to send me patches...
 *
 * ----------------------------------
 * TO DO LIST:
 * -Make it so that Pioneer CD DR-A24X and friends don't get screwed up on
 *   boot
 *
 * ----------------------------------
 * 1.00  Oct 31, 1994 -- Initial version.
 * 1.01  Nov  2, 1994 -- Fixed problem with starting request in
 *                       cdrom_check_status.
 * 1.03  Nov 25, 1994 -- leaving unmask_intr[] as a user-setting (as for disks)
 * (from mlord)       -- minor changes to cdrom_setup()
 *                    -- renamed ide_dev_s to ide_drive_t, enable irq on command
 * 2.00  Nov 27, 1994 -- Generalize packet command interface;
 *                       add audio ioctls.
 * 2.01  Dec  3, 1994 -- Rework packet command interface to handle devices
 *                       which send an interrupt when ready for a command.
 * 2.02  Dec 11, 1994 -- Cache the TOC in the driver.
 *                       Don't use SCMD_PLAYAUDIO_TI; it's not included
 *                       in the current version of ATAPI.
 *                       Try to use LBA instead of track or MSF addressing
 *                       when possible.
 *                       Don't wait for READY_STAT.
 * 2.03  Jan 10, 1995 -- Rewrite block read routines to handle block sizes
 *                       other than 2k and to move multiple sectors in a
 *                       single transaction.
 * 2.04  Apr 21, 1995 -- Add work-around for Creative Labs CD220E drives.
 *                       Thanks to Nick Saw <cwsaw@pts7.pts.mot.com> for
 *                       help in figuring this out.  Ditto for Acer and
 *                       Aztech drives, which seem to have the same problem.
 * 2.04b May 30, 1995 -- Fix to match changes in ide.c version 3.16 -ml
 * 2.05  Jun  8, 1995 -- Don't attempt to retry after an illegal request
 *                        or data protect error.
 *                       Use HWIF and DEV_HWIF macros as in ide.c.
 *                       Always try to do a request_sense after
 *                        a failed command.
 *                       Include an option to give textual descriptions
 *                        of ATAPI errors.
 *                       Fix a bug in handling the sector cache which
 *                        showed up if the drive returned data in 512 byte
 *                        blocks (like Pioneer drives).  Thanks to
 *                        Richard Hirst <srh@gpt.co.uk> for diagnosing this.
 *                       Properly supply the page number field in the
 *                        MODE_SELECT command.
 *                       PLAYAUDIO12 is broken on the Aztech; work around it.
 * 2.05x Aug 11, 1995 -- lots of data structure renaming/restructuring in ide.c
 *                       (my apologies to Scott, but now ide-cd.c is independent)
 * 3.00  Aug 22, 1995 -- Implement CDROMMULTISESSION ioctl.
 *                       Implement CDROMREADAUDIO ioctl (UNTESTED).
 *                       Use input_ide_data() and output_ide_data().
 *                       Add door locking.
 *                       Fix usage count leak in cdrom_open, which happened
 *                        when a read-write mount was attempted.
 *                       Try to load the disk on open.
 *                       Implement CDROMEJECT_SW ioctl (off by default).
 *                       Read total cdrom capacity during open.
 *                       Rearrange logic in cdrom_decode_status.  Issue
 *                        request sense commands for failed packet commands
 *                        from here instead of from cdrom_queue_packet_command.
 *                        Fix a race condition in retrieving error information.
 *                       Suppress printing normal unit attention errors and
 *                        some drive not ready errors.
 *                       Implement CDROMVOLREAD ioctl.
 *                       Implement CDROMREADMODE1/2 ioctls.
 *                       Fix race condition in setting up interrupt handlers
 *                        when the `serialize' option is used.
 * 3.01  Sep  2, 1995 -- Fix ordering of reenabling interrupts in
 *                        cdrom_queue_request.
 *                       Another try at using ide_[input,output]_data.
 * 3.02  Sep 16, 1995 -- Stick total disk capacity in partition table as well.
 *                       Make VERBOSE_IDE_CD_ERRORS dump failed command again.
 *                       Dump out more information for ILLEGAL REQUEST errs.
 *                       Fix handling of errors occurring before the
 *                        packet command is transferred.
 *                       Fix transfers with odd bytelengths.
 * 3.03  Oct 27, 1995 -- Some Creative drives have an id of just `CD'.
 *                       `DCI-2S10' drives are broken too.
 * 3.04  Nov 20, 1995 -- So are Vertos drives.
 * 3.05  Dec  1, 1995 -- Changes to go with overhaul of ide.c and ide-tape.c
 * 3.06  Dec 16, 1995 -- Add support needed for partitions.
 *                       More workarounds for Vertos bugs (based on patches
 *                        from Holger Dietze <dietze@aix520.informatik.uni-leipzig.de>).
 *                       Try to eliminate byteorder assumptions.
 *                       Use atapi_cdrom_subchnl struct definition.
 *                       Add STANDARD_ATAPI compilation option.
 * 3.07  Jan 29, 1996 -- More twiddling for broken drives: Sony 55D,
 *                        Vertos 300.
 *                       Add NO_DOOR_LOCKING configuration option.
 *                       Handle drive_cmd requests w/NULL args (for hdparm -t).
 *                       Work around sporadic Sony55e audio play problem.
 * 3.07a Feb 11, 1996 -- check drive->id for NULL before dereferencing, to fix
 *                        problem with "hde=cdrom" with no drive present.  -ml
 * 3.08  Mar  6, 1996 -- More Vertos workarounds.
 * 3.09  Apr  5, 1996 -- Add CDROMCLOSETRAY ioctl.
 *                       Switch to using MSF addressing for audio commands.
 *                       Reformat to match kernel tabbing style.
 *                       Add CDROM_GET_UPC ioctl.
 * 3.10  Apr 10, 1996 -- Fix compilation error with STANDARD_ATAPI.
 * 3.11  Apr 29, 1996 -- Patch from Heiko Eissfeldt <heiko@colossus.escape.de>
 *                       to remove redundant verify_area calls.
 * 3.12  May  7, 1996 -- Rudimentary changer support.  Based on patches
 *                        from Gerhard Zuber <zuber@berlin.snafu.de>.
 *                       Let open succeed even if there's no loaded disc.
 * 3.13  May 19, 1996 -- Fixes for changer code.
 * 3.14  May 29, 1996 -- Add work-around for Vertos 600.
 *                        (From Hennus Bergman <hennus@sky.ow.nl>.)
 * 3.15  July 2, 1996 -- Added support for Sanyo 3 CD changers
 *                        from Ben Galliart <bgallia@luc.edu> with 
 *                        special help from Jeff Lightfoot 
 *                        <jeffml@pobox.com>
 * 3.15a July 9, 1996 -- Improved Sanyo 3 CD changer identification
 * 3.16  Jul 28, 1996 -- Fix from Gadi to reduce kernel stack usage for ioctl.
 * 3.17  Sep 17, 1996 -- Tweak audio reads for some drives.
 *                       Start changing CDROMLOADFROMSLOT to CDROM_SELECT_DISC.
 * 3.18  Oct 31, 1996 -- Added module and DMA support.
 *                       
 *                       
 * 4.00  Nov 5, 1996   -- New ide-cd maintainer,
 *                                 Erik B. Andersen <andersee@debian.org>
 *                     -- Newer Creative drives don't always set the error
 *                          register correctly.  Make sure we see media changes
 *                          regardless.
 *                     -- Integrate with generic cdrom driver.
 *                     -- CDROMGETSPINDOWN and CDROMSETSPINDOWN ioctls, based on
 *                          a patch from Ciro Cattuto <>.
 *                     -- Call set_device_ro.
 *                     -- Implement CDROMMECHANISMSTATUS and CDROMSLOTTABLE
 *                          ioctls, based on patch by Erik Andersen
 *                     -- Add some probes of drive capability during setup.
 *
 * 4.01  Nov 11, 1996  -- Split into ide-cd.c and ide-cd.h
 *                     -- Removed CDROMMECHANISMSTATUS and CDROMSLOTTABLE 
 *                          ioctls in favor of a generalized approach 
 *                          using the generic cdrom driver.
 *                     -- Fully integrated with the 2.1.X kernel.
 *                     -- Other stuff that I forgot (lots of changes)
 *
 * 4.02  Dec 01, 1996  -- Applied patch from Gadi Oxman <gadio@netvision.net.il>
 *                          to fix the drive door locking problems.
 *
 * 4.03  Dec 04, 1996  -- Added DSC overlap support.
 * 4.04  Dec 29, 1996  -- Added CDROMREADRAW ioclt based on patch 
 *                          by Ales Makarov (xmakarov@sun.felk.cvut.cz)
 *
 * 4.05  Nov 20, 1997  -- Modified to print more drive info on init
 *                        Minor other changes
 *                        Fix errors on CDROMSTOP (If you have a "Dolphin",
 *                          you must define IHAVEADOLPHIN)
 *                        Added identifier so new Sanyo CD-changer works
 *                        Better detection if door locking isn't supported
 *
 * 4.06  Dec 17, 1997  -- fixed endless "tray open" messages  -ml
 * 4.07  Dec 17, 1997  -- fallback to set pc->stat on "tray open"
 * 4.08  Dec 18, 1997  -- spew less noise when tray is empty
 *                     -- fix speed display for ACER 24X, 18X
 * 4.09  Jan 04, 1998  -- fix handling of the last block so we return
 *                         an end of file instead of an I/O error (Gadi)
 * 4.10  Jan 24, 1998  -- fixed a bug so now changers can change to a new
 *                         slot when there is no disc in the current slot.
 *                     -- Fixed a memory leak where info->changer_info was
 *                         malloc'ed but never free'd when closing the device.
 *                     -- Cleaned up the global namespace a bit by making more
 *                         functions static that should already have been.
 * 4.11  Mar 12, 1998  -- Added support for the CDROM_SELECT_SPEED ioctl
 *                         based on a patch for 2.0.33 by Jelle Foks 
 *                         <jelle@scintilla.utwente.nl>, a patch for 2.0.33
 *                         by Toni Giorgino <toni@pcape2.pi.infn.it>, the SCSI
 *                         version, and my own efforts.  -erik
 *                     -- Fixed a stupid bug which egcs was kind enough to
 *                         inform me of where "Illegal mode for this track"
 *                         was never returned due to a comparison on data
 *                         types of limited range.
 * 4.12  Mar 29, 1998  -- Fixed bug in CDROM_SELECT_SPEED so write speed is 
 *                         now set ionly for CD-R and CD-RW drives.  I had 
 *                         removed this support because it produced errors.
 *                         It produced errors _only_ for non-writers. duh.
 * 4.13  May 05, 1998  -- Suppress useless "in progress of becoming ready"
 *                         messages, since this is not an error.
 *                     -- Change error messages to be const
 *                     -- Remove a "\t" which looks ugly in the syslogs
 * 4.14  July 17, 1998 -- Change to pointing to .ps version of ATAPI spec
 *                         since the .pdf version doesn't seem to work...
 *                     -- Updated the TODO list to something more current.
 *
 * 4.15  Aug 25, 1998  -- Updated ide-cd.h to respect mechine endianess, 
 *                         patch thanks to "Eddie C. Dost" <ecd@skynet.be>
 *
 * 4.50  Oct 19, 1998  -- New maintainers!
 *                         Jens Axboe <axboe@image.dk>
 *                         Chris Zwilling <chris@cloudnet.com>
 *
 * 4.51  Dec 23, 1998  -- Jens Axboe <axboe@image.dk>
 *                      - ide_cdrom_reset enabled since the ide subsystem
 *                         handles resets fine now. <axboe@image.dk>
 *                      - Transfer size fix for Samsung CD-ROMs, thanks to
 *                        "Ville Hallik" <ville.hallik@mail.ee>.
 *                      - other minor stuff.
 *
 * 4.52  Jan 19, 1999  -- Jens Axboe <axboe@image.dk>
 *                      - Detect DVD-ROM/RAM drives
 *
 * 4.53  Feb 22, 1999   - Include other model Samsung and one Goldstar
 *                         drive in transfer size limit.
 *                      - Fix the I/O error when doing eject without a medium
 *                         loaded on some drives.
 *                      - CDROMREADMODE2 is now implemented through
 *                         CDROMREADRAW, since many drives don't support
 *                         MODE2 (even though ATAPI 2.6 says they must).
 *                      - Added ignore parameter to ide-cd (as a module), eg
 *                         	insmod ide-cd ignore='hda hdb'
 *                         Useful when using ide-cd in conjunction with
 *                         ide-scsi. TODO: non-modular way of doing the
 *                         same.
 *
 * 4.54  Aug 5, 1999	- Support for MMC2 class commands through the generic
 *			  packet interface to cdrom.c.
 *			- Unified audio ioctl support, most of it.
 *			- cleaned up various deprecated verify_area().
 *			- Added ide_cdrom_packet() as the interface for
 *			  the Uniform generic_packet().
 *			- bunch of other stuff, will fill in logs later.
 *			- report 1 slot for non-changers, like the other
 *			  cd-rom drivers. don't report select disc for
 *			  non-changers as well.
 *			- mask out audio playing, if the device can't do it.
 *
 * 4.55  Sep 1, 1999	- Eliminated the rest of the audio ioctls, except
 *			  for CDROMREADTOC[ENTRY|HEADER]. Some of the drivers
 *			  use this independently of the actual audio handling.
 *			  They will disappear later when I get the time to
 *			  do it cleanly.
 *			- Minimize the TOC reading - only do it when we
 *			  know a media change has occurred.
 *			- Moved all the CDROMREADx ioctls to the Uniform layer.
 *			- Heiko Eissfeldt <heiko@colossus.escape.de> supplied
 *			  some fixes for CDI.
 *			- CD-ROM leaving door locked fix from Andries
 *			  Brouwer <Andries.Brouwer@cwi.nl>
 *			- Erik Andersen <andersen@xmission.com> unified
 *			  commands across the various drivers and how
 *			  sense errors are handled.
 *
 * 4.56  Sep 12, 1999	- Removed changer support - it is now in the
 *			  Uniform layer.
 *			- Added partition based multisession handling.
 *			- Mode sense and mode select moved to the
 *			  Uniform layer.
 *			- Fixed a problem with WPI CDS-32X drive - it
 *			  failed the capabilities 
 *
 * 4.57  Apr 7, 2000	- Fixed sense reporting.
 *			- Fixed possible oops in ide_cdrom_get_last_session()
 *			- Fix locking mania and make ide_cdrom_reset relock
 *			- Stop spewing errors to log when magicdev polls with
 *			  TEST_UNIT_READY on some drives.
 *			- Various fixes from Tobias Ringstrom:
 *			  tray if it was locked prior to the reset.
 *			  - cdrom_read_capacity returns one frame too little.
 *			  - Fix real capacity reporting.
 *
 * 4.58  May 1, 2000	- Clean up ACER50 stuff.
 *			- Fix small problem with ide_cdrom_capacity
 *
 * 4.59  Aug 11, 2000	- Fix changer problem in cdrom_read_toc, we weren't
 *			  correctly sensing a disc change.
 *			- Rearranged some code
 *			- Use extended sense on drives that support it for
 *			  correctly reporting tray status -- from
 *			  Michael D Johnson <johnsom@orst.edu>
 * 4.60  Dec 17, 2003	- Add mt rainier support
 *			- Bump timeout for packet commands, matches sr
 *			- Odd stuff
 * 4.61  Jan 22, 2004	- support hardware sector sizes other than 2kB,
 *			  Pascal Schmidt <der.eremit@email.de>
 *
 *************************************************************************/
 
#define IDECD_VERSION "4.61"

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/cdrom.h>
#include <linux/ide.h>
#include <linux/completion.h>
#include <linux/mutex.h>

#include <scsi/scsi.h>	/* For SCSI -> ATAPI command conversion */

#include <asm/irq.h>
#include <asm/io.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>

#include "ide-cd.h"

static DEFINE_MUTEX(idecd_ref_mutex);

#define to_ide_cd(obj) container_of(obj, struct cdrom_info, kref) 

#define ide_cd_g(disk) \
	container_of((disk)->private_data, struct cdrom_info, driver)

static struct cdrom_info *ide_cd_get(struct gendisk *disk)
{
	struct cdrom_info *cd = NULL;

	mutex_lock(&idecd_ref_mutex);
	cd = ide_cd_g(disk);
	if (cd)
		kref_get(&cd->kref);
	mutex_unlock(&idecd_ref_mutex);
	return cd;
}

static void ide_cd_release(struct kref *);

static void ide_cd_put(struct cdrom_info *cd)
{
	mutex_lock(&idecd_ref_mutex);
	kref_put(&cd->kref, ide_cd_release);
	mutex_unlock(&idecd_ref_mutex);
}

/****************************************************************************
 * Generic packet command support and error handling routines.
 */

/* Mark that we've seen a media change, and invalidate our internal
   buffers. */
static void cdrom_saw_media_change (ide_drive_t *drive)
{
	struct cdrom_info *info = drive->driver_data;
	
	CDROM_STATE_FLAGS (drive)->media_changed = 1;
	CDROM_STATE_FLAGS (drive)->toc_valid = 0;
	info->nsectors_buffered = 0;
}

static int cdrom_log_sense(ide_drive_t *drive, struct request *rq,
			   struct request_sense *sense)
{
	int log = 0;

	if (!sense || !rq || (rq->cmd_flags & REQ_QUIET))
		return 0;

	switch (sense->sense_key) {
		case NO_SENSE: case RECOVERED_ERROR:
			break;
		case NOT_READY:
			/*
			 * don't care about tray state messages for
			 * e.g. capacity commands or in-progress or
			 * becoming ready
			 */
			if (sense->asc == 0x3a || sense->asc == 0x04)
				break;
			log = 1;
			break;
		case ILLEGAL_REQUEST:
			/*
			 * don't log START_STOP unit with LoEj set, since
			 * we cannot reliably check if drive can auto-close
			 */
			if (rq->cmd[0] == GPCMD_START_STOP_UNIT && sense->asc == 0x24)
				break;
			log = 1;
			break;
		case UNIT_ATTENTION:
			/*
			 * Make good and sure we've seen this potential media
			 * change. Some drives (i.e. Creative) fail to present
			 * the correct sense key in the error register.
			 */
			cdrom_saw_media_change(drive);
			break;
		default:
			log = 1;
			break;
	}
	return log;
}

static
void cdrom_analyze_sense_data(ide_drive_t *drive,
			      struct request *failed_command,
			      struct request_sense *sense)
{
	unsigned long sector;
	unsigned long bio_sectors;
	unsigned long valid;
	struct cdrom_info *info = drive->driver_data;

	if (!cdrom_log_sense(drive, failed_command, sense))
		return;

	/*
	 * If a read toc is executed for a CD-R or CD-RW medium where
	 * the first toc has not been recorded yet, it will fail with
	 * 05/24/00 (which is a confusing error)
	 */
	if (failed_command && failed_command->cmd[0] == GPCMD_READ_TOC_PMA_ATIP)
		if (sense->sense_key == 0x05 && sense->asc == 0x24)
			return;

 	if (sense->error_code == 0x70) {	/* Current Error */
 		switch(sense->sense_key) {
		case MEDIUM_ERROR:
		case VOLUME_OVERFLOW:
		case ILLEGAL_REQUEST:
			if (!sense->valid)
				break;
			if (failed_command == NULL ||
					!blk_fs_request(failed_command))
				break;
			sector = (sense->information[0] << 24) |
				 (sense->information[1] << 16) |
				 (sense->information[2] <<  8) |
				 (sense->information[3]);

			bio_sectors = bio_sectors(failed_command->bio);
			if (bio_sectors < 4)
				bio_sectors = 4;
			if (drive->queue->hardsect_size == 2048)
				sector <<= 2;	/* Device sector size is 2K */
			sector &= ~(bio_sectors -1);
			valid = (sector - failed_command->sector) << 9;

			if (valid < 0)
				valid = 0;
			if (sector < get_capacity(info->disk) &&
				drive->probed_capacity - sector < 4 * 75) {
				set_capacity(info->disk, sector);
			}
 		}
 	}
#if VERBOSE_IDE_CD_ERRORS
	{
		int i;
		const char *s = "bad sense key!";
		char buf[80];

		printk ("ATAPI device %s:\n", drive->name);
		if (sense->error_code==0x70)
			printk("  Error: ");
		else if (sense->error_code==0x71)
			printk("  Deferred Error: ");
		else if (sense->error_code == 0x7f)
			printk("  Vendor-specific Error: ");
		else
			printk("  Unknown Error Type: ");

		if (sense->sense_key < ARY_LEN(sense_key_texts))
			s = sense_key_texts[sense->sense_key];

		printk("%s -- (Sense key=0x%02x)\n", s, sense->sense_key);

		if (sense->asc == 0x40) {
			sprintf(buf, "Diagnostic failure on component 0x%02x",
				 sense->ascq);
			s = buf;
		} else {
			int lo = 0, mid, hi = ARY_LEN(sense_data_texts);
			unsigned long key = (sense->sense_key << 16);
			key |= (sense->asc << 8);
			if (!(sense->ascq >= 0x80 && sense->ascq <= 0xdd))
				key |= sense->ascq;
			s = NULL;

			while (hi > lo) {
				mid = (lo + hi) / 2;
				if (sense_data_texts[mid].asc_ascq == key ||
				    sense_data_texts[mid].asc_ascq == (0xff0000|key)) {
					s = sense_data_texts[mid].text;
					break;
				}
				else if (sense_data_texts[mid].asc_ascq > key)
					hi = mid;
				else
					lo = mid+1;
			}
		}

		if (s == NULL) {
			if (sense->asc > 0x80)
				s = "(vendor-specific error)";
			else
				s = "(reserved error code)";
		}

		printk(KERN_ERR "  %s -- (asc=0x%02x, ascq=0x%02x)\n",
			s, sense->asc, sense->ascq);

		if (failed_command != NULL) {

			int lo=0, mid, hi= ARY_LEN (packet_command_texts);
			s = NULL;

			while (hi > lo) {
				mid = (lo + hi) / 2;
				if (packet_command_texts[mid].packet_command ==
				    failed_command->cmd[0]) {
					s = packet_command_texts[mid].text;
					break;
				}
				if (packet_command_texts[mid].packet_command >
				    failed_command->cmd[0])
					hi = mid;
				else
					lo = mid+1;
			}

			printk (KERN_ERR "  The failed \"%s\" packet command was: \n  \"", s);
			for (i=0; i<sizeof (failed_command->cmd); i++)
				printk ("%02x ", failed_command->cmd[i]);
			printk ("\"\n");
		}

		/* The SKSV bit specifies validity of the sense_key_specific
		 * in the next two commands. It is bit 7 of the first byte.
		 * In the case of NOT_READY, if SKSV is set the drive can
		 * give us nice ETA readings.
		 */
		if (sense->sense_key == NOT_READY && (sense->sks[0] & 0x80)) {
			int progress = (sense->sks[1] << 8 | sense->sks[2]) * 100;
			printk(KERN_ERR "  Command is %02d%% complete\n", progress / 0xffff);

		}

		if (sense->sense_key == ILLEGAL_REQUEST &&
		    (sense->sks[0] & 0x80) != 0) {
			printk(KERN_ERR "  Error in %s byte %d",
				(sense->sks[0] & 0x40) != 0 ?
				"command packet" : "command data",
				(sense->sks[1] << 8) + sense->sks[2]);

			if ((sense->sks[0] & 0x40) != 0)
				printk (" bit %d", sense->sks[0] & 0x07);

			printk ("\n");
		}
	}

#else /* not VERBOSE_IDE_CD_ERRORS */

	/* Suppress printing unit attention and `in progress of becoming ready'
	   errors when we're not being verbose. */

	if (sense->sense_key == UNIT_ATTENTION ||
	    (sense->sense_key == NOT_READY && (sense->asc == 4 ||
						sense->asc == 0x3a)))
		return;

	printk(KERN_ERR "%s: error code: 0x%02x  sense_key: 0x%02x  asc: 0x%02x  ascq: 0x%02x\n",
		drive->name,
		sense->error_code, sense->sense_key,
		sense->asc, sense->ascq);
#endif /* not VERBOSE_IDE_CD_ERRORS */
}

/*
 * Initialize a ide-cd packet command request
 */
static void cdrom_prepare_request(ide_drive_t *drive, struct request *rq)
{
	struct cdrom_info *cd = drive->driver_data;

	ide_init_drive_cmd(rq);
	rq->cmd_type = REQ_TYPE_ATA_PC;
	rq->rq_disk = cd->disk;
}

static void cdrom_queue_request_sense(ide_drive_t *drive, void *sense,
				      struct request *failed_command)
{
	struct cdrom_info *info		= drive->driver_data;
	struct request *rq		= &info->request_sense_request;

	if (sense == NULL)
		sense = &info->sense_data;

	/* stuff the sense request in front of our current request */
	cdrom_prepare_request(drive, rq);

	rq->data = sense;
	rq->cmd[0] = GPCMD_REQUEST_SENSE;
	rq->cmd[4] = rq->data_len = 18;

	rq->cmd_type = REQ_TYPE_SENSE;

	/* NOTE! Save the failed command in "rq->buffer" */
	rq->buffer = (void *) failed_command;

	(void) ide_do_drive_cmd(drive, rq, ide_preempt);
}

static void cdrom_end_request (ide_drive_t *drive, int uptodate)
{
	struct request *rq = HWGROUP(drive)->rq;
	int nsectors = rq->hard_cur_sectors;

	if (blk_sense_request(rq) && uptodate) {
		/*
		 * For REQ_TYPE_SENSE, "rq->buffer" points to the original
		 * failed request
		 */
		struct request *failed = (struct request *) rq->buffer;
		struct cdrom_info *info = drive->driver_data;
		void *sense = &info->sense_data;
		unsigned long flags;

		if (failed) {
			if (failed->sense) {
				sense = failed->sense;
				failed->sense_len = rq->sense_len;
			}
			cdrom_analyze_sense_data(drive, failed, sense);
			/*
			 * now end failed request
			 */
			if (blk_fs_request(failed)) {
				if (ide_end_dequeued_request(drive, failed, 0,
						failed->hard_nr_sectors))
					BUG();
			} else {
				spin_lock_irqsave(&ide_lock, flags);
				end_that_request_chunk(failed, 0,
							failed->data_len);
				end_that_request_last(failed, 0);
				spin_unlock_irqrestore(&ide_lock, flags);
			}
		} else
			cdrom_analyze_sense_data(drive, NULL, sense);
	}

	if (!rq->current_nr_sectors && blk_fs_request(rq))
		uptodate = 1;
	/* make sure it's fully ended */
	if (blk_pc_request(rq))
		nsectors = (rq->data_len + 511) >> 9;
	if (!nsectors)
		nsectors = 1;

	ide_end_request(drive, uptodate, nsectors);
}

static void ide_dump_status_no_sense(ide_drive_t *drive, const char *msg, u8 stat)
{
	if (stat & 0x80)
		return;
	ide_dump_status(drive, msg, stat);
}

/* Returns 0 if the request should be continued.
   Returns 1 if the request was ended. */
static int cdrom_decode_status(ide_drive_t *drive, int good_stat, int *stat_ret)
{
	struct request *rq = HWGROUP(drive)->rq;
	int stat, err, sense_key;
	
	/* Check for errors. */
	stat = HWIF(drive)->INB(IDE_STATUS_REG);
	if (stat_ret)
		*stat_ret = stat;

	if (OK_STAT(stat, good_stat, BAD_R_STAT))
		return 0;

	/* Get the IDE error register. */
	err = HWIF(drive)->INB(IDE_ERROR_REG);
	sense_key = err >> 4;

	if (rq == NULL) {
		printk("%s: missing rq in cdrom_decode_status\n", drive->name);
		return 1;
	}

	if (blk_sense_request(rq)) {
		/* We got an error trying to get sense info
		   from the drive (probably while trying
		   to recover from a former error).  Just give up. */

		rq->cmd_flags |= REQ_FAILED;
		cdrom_end_request(drive, 0);
		ide_error(drive, "request sense failure", stat);
		return 1;

	} else if (blk_pc_request(rq) || rq->cmd_type == REQ_TYPE_ATA_PC) {
		/* All other functions, except for READ. */
		unsigned long flags;

		/*
		 * if we have an error, pass back CHECK_CONDITION as the
		 * scsi status byte
		 */
		if (!rq->errors)
			rq->errors = SAM_STAT_CHECK_CONDITION;

		/* Check for tray open. */
		if (sense_key == NOT_READY) {
			cdrom_saw_media_change (drive);
		} else if (sense_key == UNIT_ATTENTION) {
			/* Check for media change. */
			cdrom_saw_media_change (drive);
			/*printk("%s: media changed\n",drive->name);*/
			return 0;
		} else if (!(rq->cmd_flags & REQ_QUIET)) {
			/* Otherwise, print an error. */
			ide_dump_status(drive, "packet command error", stat);
		}
		
		rq->cmd_flags |= REQ_FAILED;

		/*
		 * instead of playing games with moving completions around,
		 * remove failed request completely and end it when the
		 * request sense has completed
		 */
		if (stat & ERR_STAT) {
			spin_lock_irqsave(&ide_lock, flags);
			blkdev_dequeue_request(rq);
			HWGROUP(drive)->rq = NULL;
			spin_unlock_irqrestore(&ide_lock, flags);

			cdrom_queue_request_sense(drive, rq->sense, rq);
		} else
			cdrom_end_request(drive, 0);

	} else if (blk_fs_request(rq)) {
		int do_end_request = 0;

		/* Handle errors from READ and WRITE requests. */

		if (blk_noretry_request(rq))
			do_end_request = 1;

		if (sense_key == NOT_READY) {
			/* Tray open. */
			if (rq_data_dir(rq) == READ) {
				cdrom_saw_media_change (drive);

				/* Fail the request. */
				printk ("%s: tray open\n", drive->name);
				do_end_request = 1;
			} else {
				struct cdrom_info *info = drive->driver_data;

				/* allow the drive 5 seconds to recover, some
				 * devices will return this error while flushing
				 * data from cache */
				if (!rq->errors)
					info->write_timeout = jiffies + ATAPI_WAIT_WRITE_BUSY;
				rq->errors = 1;
				if (time_after(jiffies, info->write_timeout))
					do_end_request = 1;
				else {
					unsigned long flags;

					/*
					 * take a breather relying on the
					 * unplug timer to kick us again
					 */
					spin_lock_irqsave(&ide_lock, flags);
					blk_plug_device(drive->queue);
					spin_unlock_irqrestore(&ide_lock,flags);
					return 1;
				}
			}
		} else if (sense_key == UNIT_ATTENTION) {
			/* Media change. */
			cdrom_saw_media_change (drive);

			/* Arrange to retry the request.
			   But be sure to give up if we've retried
			   too many times. */
			if (++rq->errors > ERROR_MAX)
				do_end_request = 1;
		} else if (sense_key == ILLEGAL_REQUEST ||
			   sense_key == DATA_PROTECT) {
			/* No point in retrying after an illegal
			   request or data protect error.*/
			ide_dump_status_no_sense (drive, "command error", stat);
			do_end_request = 1;
		} else if (sense_key == MEDIUM_ERROR) {
			/* No point in re-trying a zillion times on a bad 
			 * sector...  If we got here the error is not correctable */
			ide_dump_status_no_sense (drive, "media error (bad sector)", stat);
			do_end_request = 1;
		} else if (sense_key == BLANK_CHECK) {
			/* Disk appears blank ?? */
			ide_dump_status_no_sense (drive, "media error (blank)", stat);
			do_end_request = 1;
		} else if ((err & ~ABRT_ERR) != 0) {
			/* Go to the default handler
			   for other errors. */
			ide_error(drive, "cdrom_decode_status", stat);
			return 1;
		} else if ((++rq->errors > ERROR_MAX)) {
			/* We've racked up too many retries.  Abort. */
			do_end_request = 1;
		}

		/* End a request through request sense analysis when we have
		   sense data. We need this in order to perform end of media
		   processing */

		if (do_end_request) {
			if (stat & ERR_STAT) {
				unsigned long flags;
				spin_lock_irqsave(&ide_lock, flags);
				blkdev_dequeue_request(rq);
				HWGROUP(drive)->rq = NULL;
				spin_unlock_irqrestore(&ide_lock, flags);

				cdrom_queue_request_sense(drive, rq->sense, rq);
			} else
				cdrom_end_request(drive, 0);
		} else {
			/* If we got a CHECK_CONDITION status,
			   queue a request sense command. */
			if (stat & ERR_STAT)
				cdrom_queue_request_sense(drive, NULL, NULL);
		}
	} else {
		blk_dump_rq_flags(rq, "ide-cd: bad rq");
		cdrom_end_request(drive, 0);
	}

	/* Retry, or handle the next request. */
	return 1;
}

static int cdrom_timer_expiry(ide_drive_t *drive)
{
	struct request *rq = HWGROUP(drive)->rq;
	unsigned long wait = 0;

	/*
	 * Some commands are *slow* and normally take a long time to
	 * complete. Usually we can use the ATAPI "disconnect" to bypass
	 * this, but not all commands/drives support that. Let
	 * ide_timer_expiry keep polling us for these.
	 */
	switch (rq->cmd[0]) {
		case GPCMD_BLANK:
		case GPCMD_FORMAT_UNIT:
		case GPCMD_RESERVE_RZONE_TRACK:
		case GPCMD_CLOSE_TRACK:
		case GPCMD_FLUSH_CACHE:
			wait = ATAPI_WAIT_PC;
			break;
		default:
			if (!(rq->cmd_flags & REQ_QUIET))
				printk(KERN_INFO "ide-cd: cmd 0x%x timed out\n", rq->cmd[0]);
			wait = 0;
			break;
	}
	return wait;
}

/* Set up the device registers for transferring a packet command on DEV,
   expecting to later transfer XFERLEN bytes.  HANDLER is the routine
   which actually transfers the command to the drive.  If this is a
   drq_interrupt device, this routine will arrange for HANDLER to be
   called when the interrupt from the drive arrives.  Otherwise, HANDLER
   will be called immediately after the drive is prepared for the transfer. */

static ide_startstop_t cdrom_start_packet_command(ide_drive_t *drive,
						  int xferlen,
						  ide_handler_t *handler)
{
	ide_startstop_t startstop;
	struct cdrom_info *info = drive->driver_data;
	ide_hwif_t *hwif = drive->hwif;

	/* Wait for the controller to be idle. */
	if (ide_wait_stat(&startstop, drive, 0, BUSY_STAT, WAIT_READY))
		return startstop;

	if (info->dma)
		info->dma = !hwif->dma_setup(drive);

	/* Set up the controller registers. */
	/* FIXME: for Virtual DMA we must check harder */
	HWIF(drive)->OUTB(info->dma, IDE_FEATURE_REG);
	HWIF(drive)->OUTB(0, IDE_IREASON_REG);
	HWIF(drive)->OUTB(0, IDE_SECTOR_REG);

	HWIF(drive)->OUTB(xferlen & 0xff, IDE_BCOUNTL_REG);
	HWIF(drive)->OUTB(xferlen >> 8  , IDE_BCOUNTH_REG);
	if (IDE_CONTROL_REG)
		HWIF(drive)->OUTB(drive->ctl, IDE_CONTROL_REG);
 
	if (CDROM_CONFIG_FLAGS (drive)->drq_interrupt) {
		/* packet command */
		ide_execute_command(drive, WIN_PACKETCMD, handler, ATAPI_WAIT_PC, cdrom_timer_expiry);
		return ide_started;
	} else {
		unsigned long flags;

		/* packet command */
		spin_lock_irqsave(&ide_lock, flags);
		hwif->OUTBSYNC(drive, WIN_PACKETCMD, IDE_COMMAND_REG);
		ndelay(400);
		spin_unlock_irqrestore(&ide_lock, flags);

		return (*handler) (drive);
	}
}

/* Send a packet command to DRIVE described by CMD_BUF and CMD_LEN.
   The device registers must have already been prepared
   by cdrom_start_packet_command.
   HANDLER is the interrupt handler to call when the command completes
   or there's data ready. */
/*
 * changed 5 parameters to 3 for dvd-ram
 * struct packet_command *pc; now packet_command_t *pc;
 */
#define ATAPI_MIN_CDB_BYTES 12
static ide_startstop_t cdrom_transfer_packet_command (ide_drive_t *drive,
					  struct request *rq,
					  ide_handler_t *handler)
{
	ide_hwif_t *hwif = drive->hwif;
	int cmd_len;
	struct cdrom_info *info = drive->driver_data;
	ide_startstop_t startstop;

	if (CDROM_CONFIG_FLAGS(drive)->drq_interrupt) {
		/* Here we should have been called after receiving an interrupt
		   from the device.  DRQ should how be set. */

		/* Check for errors. */
		if (cdrom_decode_status(drive, DRQ_STAT, NULL))
			return ide_stopped;
	} else {
		/* Otherwise, we must wait for DRQ to get set. */
		if (ide_wait_stat(&startstop, drive, DRQ_STAT,
				BUSY_STAT, WAIT_READY))
			return startstop;
	}

	/* Arm the interrupt handler. */
	ide_set_handler(drive, handler, rq->timeout, cdrom_timer_expiry);

	/* ATAPI commands get padded out to 12 bytes minimum */
	cmd_len = COMMAND_SIZE(rq->cmd[0]);
	if (cmd_len < ATAPI_MIN_CDB_BYTES)
		cmd_len = ATAPI_MIN_CDB_BYTES;

	/* Send the command to the device. */
	HWIF(drive)->atapi_output_bytes(drive, rq->cmd, cmd_len);

	/* Start the DMA if need be */
	if (info->dma)
		hwif->dma_start(drive);

	return ide_started;
}

/****************************************************************************
 * Block read functions.
 */

/*
 * Buffer up to SECTORS_TO_TRANSFER sectors from the drive in our sector
 * buffer.  Once the first sector is added, any subsequent sectors are
 * assumed to be continuous (until the buffer is cleared).  For the first
 * sector added, SECTOR is its sector number.  (SECTOR is then ignored until
 * the buffer is cleared.)
 */
static void cdrom_buffer_sectors (ide_drive_t *drive, unsigned long sector,
                                  int sectors_to_transfer)
{
	struct cdrom_info *info = drive->driver_data;

	/* Number of sectors to read into the buffer. */
	int sectors_to_buffer = min_t(int, sectors_to_transfer,
				     (SECTOR_BUFFER_SIZE >> SECTOR_BITS) -
				       info->nsectors_buffered);

	char *dest;

	/* If we couldn't get a buffer, don't try to buffer anything... */
	if (info->buffer == NULL)
		sectors_to_buffer = 0;

	/* If this is the first sector in the buffer, remember its number. */
	if (info->nsectors_buffered == 0)
		info->sector_buffered = sector;

	/* Read the data into the buffer. */
	dest = info->buffer + info->nsectors_buffered * SECTOR_SIZE;
	while (sectors_to_buffer > 0) {
		HWIF(drive)->atapi_input_bytes(drive, dest, SECTOR_SIZE);
		--sectors_to_buffer;
		--sectors_to_transfer;
		++info->nsectors_buffered;
		dest += SECTOR_SIZE;
	}

	/* Throw away any remaining data. */
	while (sectors_to_transfer > 0) {
		static char dum[SECTOR_SIZE];
		HWIF(drive)->atapi_input_bytes(drive, dum, sizeof (dum));
		--sectors_to_transfer;
	}
}

/*
 * Check the contents of the interrupt reason register from the cdrom
 * and attempt to recover if there are problems.  Returns  0 if everything's
 * ok; nonzero if the request has been terminated.
 */
static
int cdrom_read_check_ireason (ide_drive_t *drive, int len, int ireason)
{
	if (ireason == 2)
		return 0;
	else if (ireason == 0) {
		/* Whoops... The drive is expecting to receive data from us! */
		printk(KERN_ERR "%s: read_intr: Drive wants to transfer data the "
						"wrong way!\n", drive->name);

		/* Throw some data at the drive so it doesn't hang
		   and quit this request. */
		while (len > 0) {
			int dum = 0;
			HWIF(drive)->atapi_output_bytes(drive, &dum, sizeof (dum));
			len -= sizeof (dum);
		}
	} else  if (ireason == 1) {
		/* Some drives (ASUS) seem to tell us that status
		 * info is available. just get it and ignore.
		 */
		(void) HWIF(drive)->INB(IDE_STATUS_REG);
		return 0;
	} else {
		/* Drive wants a command packet, or invalid ireason... */
		printk(KERN_ERR "%s: read_intr: bad interrupt reason %x\n", drive->name,
								ireason);
	}

	cdrom_end_request(drive, 0);
	return -1;
}

/*
 * Interrupt routine.  Called when a read request has completed.
 */
static ide_startstop_t cdrom_read_intr (ide_drive_t *drive)
{
	int stat;
	int ireason, len, sectors_to_transfer, nskip;
	struct cdrom_info *info = drive->driver_data;
	u8 lowcyl = 0, highcyl = 0;
	int dma = info->dma, dma_error = 0;

	struct request *rq = HWGROUP(drive)->rq;

	/*
	 * handle dma case
	 */
	if (dma) {
		info->dma = 0;
		if ((dma_error = HWIF(drive)->ide_dma_end(drive)))
			__ide_dma_off(drive);
	}

	if (cdrom_decode_status(drive, 0, &stat))
		return ide_stopped;

	if (dma) {
		if (!dma_error) {
			ide_end_request(drive, 1, rq->nr_sectors);
			return ide_stopped;
		} else
			return ide_error(drive, "dma error", stat);
	}

	/* Read the interrupt reason and the transfer length. */
	ireason = HWIF(drive)->INB(IDE_IREASON_REG) & 0x3;
	lowcyl  = HWIF(drive)->INB(IDE_BCOUNTL_REG);
	highcyl = HWIF(drive)->INB(IDE_BCOUNTH_REG);

	len = lowcyl + (256 * highcyl);

	/* If DRQ is clear, the command has completed. */
	if ((stat & DRQ_STAT) == 0) {
		/* If we're not done filling the current buffer, complain.
		   Otherwise, complete the command normally. */
		if (rq->current_nr_sectors > 0) {
			printk (KERN_ERR "%s: cdrom_read_intr: data underrun (%d blocks)\n",
				drive->name, rq->current_nr_sectors);
			rq->cmd_flags |= REQ_FAILED;
			cdrom_end_request(drive, 0);
		} else
			cdrom_end_request(drive, 1);
		return ide_stopped;
	}

	/* Check that the drive is expecting to do the same thing we are. */
	if (cdrom_read_check_ireason (drive, len, ireason))
		return ide_stopped;

	/* Assume that the drive will always provide data in multiples
	   of at least SECTOR_SIZE, as it gets hairy to keep track
	   of the transfers otherwise. */
	if ((len % SECTOR_SIZE) != 0) {
		printk (KERN_ERR "%s: cdrom_read_intr: Bad transfer size %d\n",
			drive->name, len);
		if (CDROM_CONFIG_FLAGS(drive)->limit_nframes)
			printk (KERN_ERR "  This drive is not supported by this version of the driver\n");
		else {
			printk (KERN_ERR "  Trying to limit transfer sizes\n");
			CDROM_CONFIG_FLAGS(drive)->limit_nframes = 1;
		}
		cdrom_end_request(drive, 0);
		return ide_stopped;
	}

	/* The number of sectors we need to read from the drive. */
	sectors_to_transfer = len / SECTOR_SIZE;

	/* First, figure out if we need to bit-bucket
	   any of the leading sectors. */
	nskip = min_t(int, rq->current_nr_sectors - bio_cur_sectors(rq->bio), sectors_to_transfer);

	while (nskip > 0) {
		/* We need to throw away a sector. */
		static char dum[SECTOR_SIZE];
		HWIF(drive)->atapi_input_bytes(drive, dum, sizeof (dum));

		--rq->current_nr_sectors;
		--nskip;
		--sectors_to_transfer;
	}

	/* Now loop while we still have data to read from the drive. */
	while (sectors_to_transfer > 0) {
		int this_transfer;

		/* If we've filled the present buffer but there's another
		   chained buffer after it, move on. */
		if (rq->current_nr_sectors == 0 && rq->nr_sectors)
			cdrom_end_request(drive, 1);

		/* If the buffers are full, cache the rest of the data in our
		   internal buffer. */
		if (rq->current_nr_sectors == 0) {
			cdrom_buffer_sectors(drive, rq->sector, sectors_to_transfer);
			sectors_to_transfer = 0;
		} else {
			/* Transfer data to the buffers.
			   Figure out how many sectors we can transfer
			   to the current buffer. */
			this_transfer = min_t(int, sectors_to_transfer,
					     rq->current_nr_sectors);

			/* Read this_transfer sectors
			   into the current buffer. */
			while (this_transfer > 0) {
				HWIF(drive)->atapi_input_bytes(drive, rq->buffer, SECTOR_SIZE);
				rq->buffer += SECTOR_SIZE;
				--rq->nr_sectors;
				--rq->current_nr_sectors;
				++rq->sector;
				--this_transfer;
				--sectors_to_transfer;
			}
		}
	}

	/* Done moving data!  Wait for another interrupt. */
	ide_set_handler(drive, &cdrom_read_intr, ATAPI_WAIT_PC, NULL);
	return ide_started;
}

/*
 * Try to satisfy some of the current read request from our cached data.
 * Returns nonzero if the request has been completed, zero otherwise.
 */
static int cdrom_read_from_buffer (ide_drive_t *drive)
{
	struct cdrom_info *info = drive->driver_data;
	struct request *rq = HWGROUP(drive)->rq;
	unsigned short sectors_per_frame;

	sectors_per_frame = queue_hardsect_size(drive->queue) >> SECTOR_BITS;

	/* Can't do anything if there's no buffer. */
	if (info->buffer == NULL) return 0;

	/* Loop while this request needs data and the next block is present
	   in our cache. */
	while (rq->nr_sectors > 0 &&
	       rq->sector >= info->sector_buffered &&
	       rq->sector < info->sector_buffered + info->nsectors_buffered) {
		if (rq->current_nr_sectors == 0)
			cdrom_end_request(drive, 1);

		memcpy (rq->buffer,
			info->buffer +
			(rq->sector - info->sector_buffered) * SECTOR_SIZE,
			SECTOR_SIZE);
		rq->buffer += SECTOR_SIZE;
		--rq->current_nr_sectors;
		--rq->nr_sectors;
		++rq->sector;
	}

	/* If we've satisfied the current request,
	   terminate it successfully. */
	if (rq->nr_sectors == 0) {
		cdrom_end_request(drive, 1);
		return -1;
	}

	/* Move on to the next buffer if needed. */
	if (rq->current_nr_sectors == 0)
		cdrom_end_request(drive, 1);

	/* If this condition does not hold, then the kluge i use to
	   represent the number of sectors to skip at the start of a transfer
	   will fail.  I think that this will never happen, but let's be
	   paranoid and check. */
	if (rq->current_nr_sectors < bio_cur_sectors(rq->bio) &&
	    (rq->sector & (sectors_per_frame - 1))) {
		printk(KERN_ERR "%s: cdrom_read_from_buffer: buffer botch (%ld)\n",
			drive->name, (long)rq->sector);
		cdrom_end_request(drive, 0);
		return -1;
	}

	return 0;
}

/*
 * Routine to send a read packet command to the drive.
 * This is usually called directly from cdrom_start_read.
 * However, for drq_interrupt devices, it is called from an interrupt
 * when the drive is ready to accept the command.
 */
static ide_startstop_t cdrom_start_read_continuation (ide_drive_t *drive)
{
	struct request *rq = HWGROUP(drive)->rq;
	unsigned short sectors_per_frame;
	int nskip;

	sectors_per_frame = queue_hardsect_size(drive->queue) >> SECTOR_BITS;

	/* If the requested sector doesn't start on a cdrom block boundary,
	   we must adjust the start of the transfer so that it does,
	   and remember to skip the first few sectors.
	   If the CURRENT_NR_SECTORS field is larger than the size
	   of the buffer, it will mean that we're to skip a number
	   of sectors equal to the amount by which CURRENT_NR_SECTORS
	   is larger than the buffer size. */
	nskip = rq->sector & (sectors_per_frame - 1);
	if (nskip > 0) {
		/* Sanity check... */
		if (rq->current_nr_sectors != bio_cur_sectors(rq->bio) &&
			(rq->sector & (sectors_per_frame - 1))) {
			printk(KERN_ERR "%s: cdrom_start_read_continuation: buffer botch (%u)\n",
				drive->name, rq->current_nr_sectors);
			cdrom_end_request(drive, 0);
			return ide_stopped;
		}
		rq->current_nr_sectors += nskip;
	}

	/* Set up the command */
	rq->timeout = ATAPI_WAIT_PC;

	/* Send the command to the drive and return. */
	return cdrom_transfer_packet_command(drive, rq, &cdrom_read_intr);
}


#define IDECD_SEEK_THRESHOLD	(1000)			/* 1000 blocks */
#define IDECD_SEEK_TIMER	(5 * WAIT_MIN_SLEEP)	/* 100 ms */
#define IDECD_SEEK_TIMEOUT	(2 * WAIT_CMD)		/* 20 sec */

static ide_startstop_t cdrom_seek_intr (ide_drive_t *drive)
{
	struct cdrom_info *info = drive->driver_data;
	int stat;
	static int retry = 10;

	if (cdrom_decode_status(drive, 0, &stat))
		return ide_stopped;
	CDROM_CONFIG_FLAGS(drive)->seeking = 1;

	if (retry && time_after(jiffies, info->start_seek + IDECD_SEEK_TIMER)) {
		if (--retry == 0) {
			/*
			 * this condition is far too common, to bother
			 * users about it
			 */
			/* printk("%s: disabled DSC seek overlap\n", drive->name);*/ 
			drive->dsc_overlap = 0;
		}
	}
	return ide_stopped;
}

static ide_startstop_t cdrom_start_seek_continuation (ide_drive_t *drive)
{
	struct request *rq = HWGROUP(drive)->rq;
	sector_t frame = rq->sector;

	sector_div(frame, queue_hardsect_size(drive->queue) >> SECTOR_BITS);

	memset(rq->cmd, 0, sizeof(rq->cmd));
	rq->cmd[0] = GPCMD_SEEK;
	put_unaligned(cpu_to_be32(frame), (unsigned int *) &rq->cmd[2]);

	rq->timeout = ATAPI_WAIT_PC;
	return cdrom_transfer_packet_command(drive, rq, &cdrom_seek_intr);
}

static ide_startstop_t cdrom_start_seek (ide_drive_t *drive, unsigned int block)
{
	struct cdrom_info *info = drive->driver_data;

	info->dma = 0;
	info->start_seek = jiffies;
	return cdrom_start_packet_command(drive, 0, cdrom_start_seek_continuation);
}

/* Fix up a possibly partially-processed request so that we can
   start it over entirely, or even put it back on the request queue. */
static void restore_request (struct request *rq)
{
	if (rq->buffer != bio_data(rq->bio)) {
		sector_t n = (rq->buffer - (char *) bio_data(rq->bio)) / SECTOR_SIZE;

		rq->buffer = bio_data(rq->bio);
		rq->nr_sectors += n;
		rq->sector -= n;
	}
	rq->hard_cur_sectors = rq->current_nr_sectors = bio_cur_sectors(rq->bio);
	rq->hard_nr_sectors = rq->nr_sectors;
	rq->hard_sector = rq->sector;
	rq->q->prep_rq_fn(rq->q, rq);
}

/*
 * Start a read request from the CD-ROM.
 */
static ide_startstop_t cdrom_start_read (ide_drive_t *drive, unsigned int block)
{
	struct cdrom_info *info = drive->driver_data;
	struct request *rq = HWGROUP(drive)->rq;
	unsigned short sectors_per_frame;

	sectors_per_frame = queue_hardsect_size(drive->queue) >> SECTOR_BITS;

	/* We may be retrying this request after an error.  Fix up
	   any weirdness which might be present in the request packet. */
	restore_request(rq);

	/* Satisfy whatever we can of this request from our cached sector. */
	if (cdrom_read_from_buffer(drive))
		return ide_stopped;

	/* Clear the local sector buffer. */
	info->nsectors_buffered = 0;

	/* use dma, if possible. */
	info->dma = drive->using_dma;
	if ((rq->sector & (sectors_per_frame - 1)) ||
	    (rq->nr_sectors & (sectors_per_frame - 1)))
		info->dma = 0;

	/* Start sending the read request to the drive. */
	return cdrom_start_packet_command(drive, 32768, cdrom_start_read_continuation);
}

/****************************************************************************
 * Execute all other packet commands.
 */

/* Interrupt routine for packet command completion. */
static ide_startstop_t cdrom_pc_intr (ide_drive_t *drive)
{
	int ireason, len, thislen;
	struct request *rq = HWGROUP(drive)->rq;
	u8 lowcyl = 0, highcyl = 0;
	int stat;

	/* Check for errors. */
	if (cdrom_decode_status(drive, 0, &stat))
		return ide_stopped;

	/* Read the interrupt reason and the transfer length. */
	ireason = HWIF(drive)->INB(IDE_IREASON_REG);
	lowcyl  = HWIF(drive)->INB(IDE_BCOUNTL_REG);
	highcyl = HWIF(drive)->INB(IDE_BCOUNTH_REG);

	len = lowcyl + (256 * highcyl);

	/* If DRQ is clear, the command has completed.
	   Complain if we still have data left to transfer. */
	if ((stat & DRQ_STAT) == 0) {
		/* Some of the trailing request sense fields are optional, and
		   some drives don't send them.  Sigh. */
		if (rq->cmd[0] == GPCMD_REQUEST_SENSE &&
		    rq->data_len > 0 &&
		    rq->data_len <= 5) {
			while (rq->data_len > 0) {
				*(unsigned char *)rq->data++ = 0;
				--rq->data_len;
			}
		}

		if (rq->data_len == 0)
			cdrom_end_request(drive, 1);
		else {
			/* Comment this out, because this always happens 
			   right after a reset occurs, and it is annoying to 
			   always print expected stuff.  */
			/*
			printk ("%s: cdrom_pc_intr: data underrun %d\n",
				drive->name, pc->buflen);
			*/
			rq->cmd_flags |= REQ_FAILED;
			cdrom_end_request(drive, 0);
		}
		return ide_stopped;
	}

	/* Figure out how much data to transfer. */
	thislen = rq->data_len;
	if (thislen > len) thislen = len;

	/* The drive wants to be written to. */
	if ((ireason & 3) == 0) {
		if (!rq->data) {
			blk_dump_rq_flags(rq, "cdrom_pc_intr, write");
			goto confused;
		}
		/* Transfer the data. */
		HWIF(drive)->atapi_output_bytes(drive, rq->data, thislen);

		/* If we haven't moved enough data to satisfy the drive,
		   add some padding. */
		while (len > thislen) {
			int dum = 0;
			HWIF(drive)->atapi_output_bytes(drive, &dum, sizeof(dum));
			len -= sizeof(dum);
		}

		/* Keep count of how much data we've moved. */
		rq->data += thislen;
		rq->data_len -= thislen;
	}

	/* Same drill for reading. */
	else if ((ireason & 3) == 2) {
		if (!rq->data) {
			blk_dump_rq_flags(rq, "cdrom_pc_intr, write");
			goto confused;
		}
		/* Transfer the data. */
		HWIF(drive)->atapi_input_bytes(drive, rq->data, thislen);

		/* If we haven't moved enough data to satisfy the drive,
		   add some padding. */
		while (len > thislen) {
			int dum = 0;
			HWIF(drive)->atapi_input_bytes(drive, &dum, sizeof(dum));
			len -= sizeof(dum);
		}

		/* Keep count of how much data we've moved. */
		rq->data += thislen;
		rq->data_len -= thislen;

		if (blk_sense_request(rq))
			rq->sense_len += thislen;
	} else {
confused:
		printk (KERN_ERR "%s: cdrom_pc_intr: The drive "
			"appears confused (ireason = 0x%02x). "
			"Trying to recover by ending request.\n",
			drive->name, ireason);
		rq->cmd_flags |= REQ_FAILED;
		cdrom_end_request(drive, 0);
		return ide_stopped;
	}

	/* Now we wait for another interrupt. */
	ide_set_handler(drive, &cdrom_pc_intr, ATAPI_WAIT_PC, cdrom_timer_expiry);
	return ide_started;
}

static ide_startstop_t cdrom_do_pc_continuation (ide_drive_t *drive)
{
	struct request *rq = HWGROUP(drive)->rq;

	if (!rq->timeout)
		rq->timeout = ATAPI_WAIT_PC;

	/* Send the command to the drive and return. */
	return cdrom_transfer_packet_command(drive, rq, &cdrom_pc_intr);
}


static ide_startstop_t cdrom_do_packet_command (ide_drive_t *drive)
{
	int len;
	struct request *rq = HWGROUP(drive)->rq;
	struct cdrom_info *info = drive->driver_data;

	info->dma = 0;
	rq->cmd_flags &= ~REQ_FAILED;
	len = rq->data_len;

	/* Start sending the command to the drive. */
	return cdrom_start_packet_command(drive, len, cdrom_do_pc_continuation);
}


static int cdrom_queue_packet_command(ide_drive_t *drive, struct request *rq)
{
	struct request_sense sense;
	int retries = 10;
	unsigned int flags = rq->cmd_flags;

	if (rq->sense == NULL)
		rq->sense = &sense;

	/* Start of retry loop. */
	do {
		int error;
		unsigned long time = jiffies;
		rq->cmd_flags = flags;

		error = ide_do_drive_cmd(drive, rq, ide_wait);
		time = jiffies - time;

		/* FIXME: we should probably abort/retry or something 
		 * in case of failure */
		if (rq->cmd_flags & REQ_FAILED) {
			/* The request failed.  Retry if it was due to a unit
			   attention status
			   (usually means media was changed). */
			struct request_sense *reqbuf = rq->sense;

			if (reqbuf->sense_key == UNIT_ATTENTION)
				cdrom_saw_media_change(drive);
			else if (reqbuf->sense_key == NOT_READY &&
				 reqbuf->asc == 4 && reqbuf->ascq != 4) {
				/* The drive is in the process of loading
				   a disk.  Retry, but wait a little to give
				   the drive time to complete the load. */
				ssleep(2);
			} else {
				/* Otherwise, don't retry. */
				retries = 0;
			}
			--retries;
		}

		/* End of retry loop. */
	} while ((rq->cmd_flags & REQ_FAILED) && retries >= 0);

	/* Return an error if the command failed. */
	return (rq->cmd_flags & REQ_FAILED) ? -EIO : 0;
}

/*
 * Write handling
 */
static int cdrom_write_check_ireason(ide_drive_t *drive, int len, int ireason)
{
	/* Two notes about IDE interrupt reason here - 0 means that
	 * the drive wants to receive data from us, 2 means that
	 * the drive is expecting to transfer data to us.
	 */
	if (ireason == 0)
		return 0;
	else if (ireason == 2) {
		/* Whoops... The drive wants to send data. */
		printk(KERN_ERR "%s: write_intr: wrong transfer direction!\n",
							drive->name);

		while (len > 0) {
			int dum = 0;
			HWIF(drive)->atapi_input_bytes(drive, &dum, sizeof(dum));
			len -= sizeof(dum);
		}
	} else {
		/* Drive wants a command packet, or invalid ireason... */
		printk(KERN_ERR "%s: write_intr: bad interrupt reason %x\n",
							drive->name, ireason);
	}

	cdrom_end_request(drive, 0);
	return 1;
}

static void post_transform_command(struct request *req)
{
	u8 *c = req->cmd;
	char *ibuf;

	if (!blk_pc_request(req))
		return;

	if (req->bio)
		ibuf = bio_data(req->bio);
	else
		ibuf = req->data;

	if (!ibuf)
		return;

	/*
	 * set ansi-revision and response data as atapi
	 */
	if (c[0] == GPCMD_INQUIRY) {
		ibuf[2] |= 2;
		ibuf[3] = (ibuf[3] & 0xf0) | 2;
	}
}

typedef void (xfer_func_t)(ide_drive_t *, void *, u32);

/*
 * best way to deal with dma that is not sector aligned right now... note
 * that in this path we are not using ->data or ->buffer at all. this irs
 * can replace cdrom_pc_intr, cdrom_read_intr, and cdrom_write_intr in the
 * future.
 */
static ide_startstop_t cdrom_newpc_intr(ide_drive_t *drive)
{
	struct cdrom_info *info = drive->driver_data;
	struct request *rq = HWGROUP(drive)->rq;
	int dma_error, dma, stat, ireason, len, thislen;
	u8 lowcyl, highcyl;
	xfer_func_t *xferfunc;
	unsigned long flags;

	/* Check for errors. */
	dma_error = 0;
	dma = info->dma;
	if (dma) {
		info->dma = 0;
		dma_error = HWIF(drive)->ide_dma_end(drive);
	}

	if (cdrom_decode_status(drive, 0, &stat))
		return ide_stopped;

	/*
	 * using dma, transfer is complete now
	 */
	if (dma) {
		if (dma_error) {
			printk(KERN_ERR "ide-cd: dma error\n");
			__ide_dma_off(drive);
			return ide_error(drive, "dma error", stat);
		}

		end_that_request_chunk(rq, 1, rq->data_len);
		rq->data_len = 0;
		goto end_request;
	}

	/*
	 * ok we fall to pio :/
	 */
	ireason = HWIF(drive)->INB(IDE_IREASON_REG) & 0x3;
	lowcyl  = HWIF(drive)->INB(IDE_BCOUNTL_REG);
	highcyl = HWIF(drive)->INB(IDE_BCOUNTH_REG);

	len = lowcyl + (256 * highcyl);
	thislen = rq->data_len;
	if (thislen > len)
		thislen = len;

	/*
	 * If DRQ is clear, the command has completed.
	 */
	if ((stat & DRQ_STAT) == 0)
		goto end_request;

	/*
	 * check which way to transfer data
	 */
	if (rq_data_dir(rq) == WRITE) {
		/*
		 * write to drive
		 */
		if (cdrom_write_check_ireason(drive, len, ireason))
			return ide_stopped;

		xferfunc = HWIF(drive)->atapi_output_bytes;
	} else  {
		/*
		 * read from drive
		 */
		if (cdrom_read_check_ireason(drive, len, ireason))
			return ide_stopped;

		xferfunc = HWIF(drive)->atapi_input_bytes;
	}

	/*
	 * transfer data
	 */
	while (thislen > 0) {
		int blen = blen = rq->data_len;
		char *ptr = rq->data;

		/*
		 * bio backed?
		 */
		if (rq->bio) {
			ptr = bio_data(rq->bio);
			blen = bio_iovec(rq->bio)->bv_len;
		}

		if (!ptr) {
			printk(KERN_ERR "%s: confused, missing data\n", drive->name);
			break;
		}

		if (blen > thislen)
			blen = thislen;

		xferfunc(drive, ptr, blen);

		thislen -= blen;
		len -= blen;
		rq->data_len -= blen;

		if (rq->bio)
			end_that_request_chunk(rq, 1, blen);
		else
			rq->data += blen;
	}

	/*
	 * pad, if necessary
	 */
	if (len > 0) {
		while (len > 0) {
			int pad = 0;

			xferfunc(drive, &pad, sizeof(pad));
			len -= sizeof(pad);
		}
	}

	BUG_ON(HWGROUP(drive)->handler != NULL);

	ide_set_handler(drive, cdrom_newpc_intr, rq->timeout, NULL);
	return ide_started;

end_request:
	if (!rq->data_len)
		post_transform_command(rq);

	spin_lock_irqsave(&ide_lock, flags);
	blkdev_dequeue_request(rq);
	end_that_request_last(rq, 1);
	HWGROUP(drive)->rq = NULL;
	spin_unlock_irqrestore(&ide_lock, flags);
	return ide_stopped;
}

static ide_startstop_t cdrom_write_intr(ide_drive_t *drive)
{
	int stat, ireason, len, sectors_to_transfer, uptodate;
	struct cdrom_info *info = drive->driver_data;
	int dma_error = 0, dma = info->dma;
	u8 lowcyl = 0, highcyl = 0;

	struct request *rq = HWGROUP(drive)->rq;

	/* Check for errors. */
	if (dma) {
		info->dma = 0;
		if ((dma_error = HWIF(drive)->ide_dma_end(drive))) {
			printk(KERN_ERR "ide-cd: write dma error\n");
			__ide_dma_off(drive);
		}
	}

	if (cdrom_decode_status(drive, 0, &stat))
		return ide_stopped;

	/*
	 * using dma, transfer is complete now
	 */
	if (dma) {
		if (dma_error)
			return ide_error(drive, "dma error", stat);

		ide_end_request(drive, 1, rq->nr_sectors);
		return ide_stopped;
	}

	/* Read the interrupt reason and the transfer length. */
	ireason = HWIF(drive)->INB(IDE_IREASON_REG);
	lowcyl  = HWIF(drive)->INB(IDE_BCOUNTL_REG);
	highcyl = HWIF(drive)->INB(IDE_BCOUNTH_REG);

	len = lowcyl + (256 * highcyl);

	/* If DRQ is clear, the command has completed. */
	if ((stat & DRQ_STAT) == 0) {
		/* If we're not done writing, complain.
		 * Otherwise, complete the command normally.
		 */
		uptodate = 1;
		if (rq->current_nr_sectors > 0) {
			printk(KERN_ERR "%s: write_intr: data underrun (%d blocks)\n",
			drive->name, rq->current_nr_sectors);
			uptodate = 0;
		}
		cdrom_end_request(drive, uptodate);
		return ide_stopped;
	}

	/* Check that the drive is expecting to do the same thing we are. */
	if (cdrom_write_check_ireason(drive, len, ireason))
		return ide_stopped;

	sectors_to_transfer = len / SECTOR_SIZE;

	/*
	 * now loop and write out the data
	 */
	while (sectors_to_transfer > 0) {
		int this_transfer;

		if (!rq->current_nr_sectors) {
			printk(KERN_ERR "ide-cd: write_intr: oops\n");
			break;
		}

		/*
		 * Figure out how many sectors we can transfer
		 */
		this_transfer = min_t(int, sectors_to_transfer, rq->current_nr_sectors);

		while (this_transfer > 0) {
			HWIF(drive)->atapi_output_bytes(drive, rq->buffer, SECTOR_SIZE);
			rq->buffer += SECTOR_SIZE;
			--rq->nr_sectors;
			--rq->current_nr_sectors;
			++rq->sector;
			--this_transfer;
			--sectors_to_transfer;
		}

		/*
		 * current buffer complete, move on
		 */
		if (rq->current_nr_sectors == 0 && rq->nr_sectors)
			cdrom_end_request(drive, 1);
	}

	/* re-arm handler */
	ide_set_handler(drive, &cdrom_write_intr, ATAPI_WAIT_PC, NULL);
	return ide_started;
}

static ide_startstop_t cdrom_start_write_cont(ide_drive_t *drive)
{
	struct request *rq = HWGROUP(drive)->rq;

#if 0	/* the immediate bit */
	rq->cmd[1] = 1 << 3;
#endif
	rq->timeout = ATAPI_WAIT_PC;

	return cdrom_transfer_packet_command(drive, rq, cdrom_write_intr);
}

static ide_startstop_t cdrom_start_write(ide_drive_t *drive, struct request *rq)
{
	struct cdrom_info *info = drive->driver_data;
	struct gendisk *g = info->disk;
	unsigned short sectors_per_frame = queue_hardsect_size(drive->queue) >> SECTOR_BITS;

	/*
	 * writes *must* be hardware frame aligned
	 */
	if ((rq->nr_sectors & (sectors_per_frame - 1)) ||
	    (rq->sector & (sectors_per_frame - 1))) {
		cdrom_end_request(drive, 0);
		return ide_stopped;
	}

	/*
	 * disk has become write protected
	 */
	if (g->policy) {
		cdrom_end_request(drive, 0);
		return ide_stopped;
	}

	info->nsectors_buffered = 0;

	/* use dma, if possible. we don't need to check more, since we
	 * know that the transfer is always (at least!) frame aligned */
	info->dma = drive->using_dma ? 1 : 0;

	info->devinfo.media_written = 1;

	/* Start sending the write request to the drive. */
	return cdrom_start_packet_command(drive, 32768, cdrom_start_write_cont);
}

static ide_startstop_t cdrom_do_newpc_cont(ide_drive_t *drive)
{
	struct request *rq = HWGROUP(drive)->rq;

	if (!rq->timeout)
		rq->timeout = ATAPI_WAIT_PC;

	return cdrom_transfer_packet_command(drive, rq, cdrom_newpc_intr);
}

static ide_startstop_t cdrom_do_block_pc(ide_drive_t *drive, struct request *rq)
{
	struct cdrom_info *info = drive->driver_data;

	rq->cmd_flags |= REQ_QUIET;

	info->dma = 0;

	/*
	 * sg request
	 */
	if (rq->bio) {
		int mask = drive->queue->dma_alignment;
		unsigned long addr = (unsigned long) page_address(bio_page(rq->bio));

		info->dma = drive->using_dma;

		/*
		 * check if dma is safe
		 *
		 * NOTE! The "len" and "addr" checks should possibly have
		 * separate masks.
		 */
		if ((rq->data_len & 15) || (addr & mask))
			info->dma = 0;
	}

	/* Start sending the command to the drive. */
	return cdrom_start_packet_command(drive, rq->data_len, cdrom_do_newpc_cont);
}

/****************************************************************************
 * cdrom driver request routine.
 */
static ide_startstop_t
ide_do_rw_cdrom (ide_drive_t *drive, struct request *rq, sector_t block)
{
	ide_startstop_t action;
	struct cdrom_info *info = drive->driver_data;

	if (blk_fs_request(rq)) {
		if (CDROM_CONFIG_FLAGS(drive)->seeking) {
			unsigned long elapsed = jiffies - info->start_seek;
			int stat = HWIF(drive)->INB(IDE_STATUS_REG);

			if ((stat & SEEK_STAT) != SEEK_STAT) {
				if (elapsed < IDECD_SEEK_TIMEOUT) {
					ide_stall_queue(drive, IDECD_SEEK_TIMER);
					return ide_stopped;
				}
				printk (KERN_ERR "%s: DSC timeout\n", drive->name);
			}
			CDROM_CONFIG_FLAGS(drive)->seeking = 0;
		}
		if ((rq_data_dir(rq) == READ) && IDE_LARGE_SEEK(info->last_block, block, IDECD_SEEK_THRESHOLD) && drive->dsc_overlap) {
			action = cdrom_start_seek(drive, block);
		} else {
			if (rq_data_dir(rq) == READ)
				action = cdrom_start_read(drive, block);
			else
				action = cdrom_start_write(drive, rq);
		}
		info->last_block = block;
		return action;
	} else if (rq->cmd_type == REQ_TYPE_SENSE ||
		   rq->cmd_type == REQ_TYPE_ATA_PC) {
		return cdrom_do_packet_command(drive);
	} else if (blk_pc_request(rq)) {
		return cdrom_do_block_pc(drive, rq);
	} else if (blk_special_request(rq)) {
		/*
		 * right now this can only be a reset...
		 */
		cdrom_end_request(drive, 1);
		return ide_stopped;
	}

	blk_dump_rq_flags(rq, "ide-cd bad flags");
	cdrom_end_request(drive, 0);
	return ide_stopped;
}



/****************************************************************************
 * Ioctl handling.
 *
 * Routines which queue packet commands take as a final argument a pointer
 * to a request_sense struct.  If execution of the command results
 * in an error with a CHECK CONDITION status, this structure will be filled
 * with the results of the subsequent request sense command.  The pointer
 * can also be NULL, in which case no sense information is returned.
 */

#if ! STANDARD_ATAPI
static inline
int bin2bcd (int x)
{
	return (x%10) | ((x/10) << 4);
}


static inline
int bcd2bin (int x)
{
	return (x >> 4) * 10 + (x & 0x0f);
}

static
void msf_from_bcd (struct atapi_msf *msf)
{
	msf->minute = bcd2bin (msf->minute);
	msf->second = bcd2bin (msf->second);
	msf->frame  = bcd2bin (msf->frame);
}

#endif /* not STANDARD_ATAPI */


static inline
void lba_to_msf (int lba, byte *m, byte *s, byte *f)
{
	lba += CD_MSF_OFFSET;
	lba &= 0xffffff;  /* negative lbas use only 24 bits */
	*m = lba / (CD_SECS * CD_FRAMES);
	lba %= (CD_SECS * CD_FRAMES);
	*s = lba / CD_FRAMES;
	*f = lba % CD_FRAMES;
}


static inline
int msf_to_lba (byte m, byte s, byte f)
{
	return (((m * CD_SECS) + s) * CD_FRAMES + f) - CD_MSF_OFFSET;
}

static int cdrom_check_status(ide_drive_t *drive, struct request_sense *sense)
{
	struct request req;
	struct cdrom_info *info = drive->driver_data;
	struct cdrom_device_info *cdi = &info->devinfo;

	cdrom_prepare_request(drive, &req);

	req.sense = sense;
	req.cmd[0] = GPCMD_TEST_UNIT_READY;
	req.cmd_flags |= REQ_QUIET;

#if ! STANDARD_ATAPI
        /* the Sanyo 3 CD changer uses byte 7 of TEST_UNIT_READY to 
           switch CDs instead of supporting the LOAD_UNLOAD opcode   */

	req.cmd[7] = cdi->sanyo_slot % 3;
#endif /* not STANDARD_ATAPI */

	return cdrom_queue_packet_command(drive, &req);
}


/* Lock the door if LOCKFLAG is nonzero; unlock it otherwise. */
static int
cdrom_lockdoor(ide_drive_t *drive, int lockflag, struct request_sense *sense)
{
	struct request_sense my_sense;
	struct request req;
	int stat;

	if (sense == NULL)
		sense = &my_sense;

	/* If the drive cannot lock the door, just pretend. */
	if (CDROM_CONFIG_FLAGS(drive)->no_doorlock) {
		stat = 0;
	} else {
		cdrom_prepare_request(drive, &req);
		req.sense = sense;
		req.cmd[0] = GPCMD_PREVENT_ALLOW_MEDIUM_REMOVAL;
		req.cmd[4] = lockflag ? 1 : 0;
		stat = cdrom_queue_packet_command(drive, &req);
	}

	/* If we got an illegal field error, the drive
	   probably cannot lock the door. */
	if (stat != 0 &&
	    sense->sense_key == ILLEGAL_REQUEST &&
	    (sense->asc == 0x24 || sense->asc == 0x20)) {
		printk (KERN_ERR "%s: door locking not supported\n",
			drive->name);
		CDROM_CONFIG_FLAGS(drive)->no_doorlock = 1;
		stat = 0;
	}
	
	/* no medium, that's alright. */
	if (stat != 0 && sense->sense_key == NOT_READY && sense->asc == 0x3a)
		stat = 0;

	if (stat == 0)
		CDROM_STATE_FLAGS(drive)->door_locked = lockflag;

	return stat;
}


/* Eject the disk if EJECTFLAG is 0.
   If EJECTFLAG is 1, try to reload the disk. */
static int cdrom_eject(ide_drive_t *drive, int ejectflag,
		       struct request_sense *sense)
{
	struct request req;
	char loej = 0x02;

	if (CDROM_CONFIG_FLAGS(drive)->no_eject && !ejectflag)
		return -EDRIVE_CANT_DO_THIS;
	
	/* reload fails on some drives, if the tray is locked */
	if (CDROM_STATE_FLAGS(drive)->door_locked && ejectflag)
		return 0;

	cdrom_prepare_request(drive, &req);

	/* only tell drive to close tray if open, if it can do that */
	if (ejectflag && !CDROM_CONFIG_FLAGS(drive)->close_tray)
		loej = 0;

	req.sense = sense;
	req.cmd[0] = GPCMD_START_STOP_UNIT;
	req.cmd[4] = loej | (ejectflag != 0);
	return cdrom_queue_packet_command(drive, &req);
}

static int cdrom_read_capacity(ide_drive_t *drive, unsigned long *capacity,
			       unsigned long *sectors_per_frame,
			       struct request_sense *sense)
{
	struct {
		__u32 lba;
		__u32 blocklen;
	} capbuf;

	int stat;
	struct request req;

	cdrom_prepare_request(drive, &req);

	req.sense = sense;
	req.cmd[0] = GPCMD_READ_CDVD_CAPACITY;
	req.data = (char *)&capbuf;
	req.data_len = sizeof(capbuf);
	req.cmd_flags |= REQ_QUIET;

	stat = cdrom_queue_packet_command(drive, &req);
	if (stat == 0) {
		*capacity = 1 + be32_to_cpu(capbuf.lba);
		*sectors_per_frame =
			be32_to_cpu(capbuf.blocklen) >> SECTOR_BITS;
	}

	return stat;
}

static int cdrom_read_tocentry(ide_drive_t *drive, int trackno, int msf_flag,
				int format, char *buf, int buflen,
				struct request_sense *sense)
{
	struct request req;

	cdrom_prepare_request(drive, &req);

	req.sense = sense;
	req.data =  buf;
	req.data_len = buflen;
	req.cmd_flags |= REQ_QUIET;
	req.cmd[0] = GPCMD_READ_TOC_PMA_ATIP;
	req.cmd[6] = trackno;
	req.cmd[7] = (buflen >> 8);
	req.cmd[8] = (buflen & 0xff);
	req.cmd[9] = (format << 6);

	if (msf_flag)
		req.cmd[1] = 2;

	return cdrom_queue_packet_command(drive, &req);
}


/* Try to read the entire TOC for the disk into our internal buffer. */
static int cdrom_read_toc(ide_drive_t *drive, struct request_sense *sense)
{
	int stat, ntracks, i;
	struct cdrom_info *info = drive->driver_data;
	struct cdrom_device_info *cdi = &info->devinfo;
	struct atapi_toc *toc = info->toc;
	struct {
		struct atapi_toc_header hdr;
		struct atapi_toc_entry  ent;
	} ms_tmp;
	long last_written;
	unsigned long sectors_per_frame = SECTORS_PER_FRAME;

	if (toc == NULL) {
		/* Try to allocate space. */
		toc = kmalloc(sizeof(struct atapi_toc), GFP_KERNEL);
		if (toc == NULL) {
			printk (KERN_ERR "%s: No cdrom TOC buffer!\n", drive->name);
			return -ENOMEM;
		}
		info->toc = toc;
	}

	/* Check to see if the existing data is still valid.
	   If it is, just return. */
	(void) cdrom_check_status(drive, sense);

	if (CDROM_STATE_FLAGS(drive)->toc_valid)
		return 0;

	/* Try to get the total cdrom capacity and sector size. */
	stat = cdrom_read_capacity(drive, &toc->capacity, &sectors_per_frame,
				   sense);
	if (stat)
		toc->capacity = 0x1fffff;

	set_capacity(info->disk, toc->capacity * sectors_per_frame);
	/* Save a private copy of te TOC capacity for error handling */
	drive->probed_capacity = toc->capacity * sectors_per_frame;

	blk_queue_hardsect_size(drive->queue,
				sectors_per_frame << SECTOR_BITS);

	/* First read just the header, so we know how long the TOC is. */
	stat = cdrom_read_tocentry(drive, 0, 1, 0, (char *) &toc->hdr,
				    sizeof(struct atapi_toc_header), sense);
	if (stat)
		return stat;

#if ! STANDARD_ATAPI
	if (CDROM_CONFIG_FLAGS(drive)->toctracks_as_bcd) {
		toc->hdr.first_track = bcd2bin(toc->hdr.first_track);
		toc->hdr.last_track  = bcd2bin(toc->hdr.last_track);
	}
#endif  /* not STANDARD_ATAPI */

	ntracks = toc->hdr.last_track - toc->hdr.first_track + 1;
	if (ntracks <= 0)
		return -EIO;
	if (ntracks > MAX_TRACKS)
		ntracks = MAX_TRACKS;

	/* Now read the whole schmeer. */
	stat = cdrom_read_tocentry(drive, toc->hdr.first_track, 1, 0,
				  (char *)&toc->hdr,
				   sizeof(struct atapi_toc_header) +
				   (ntracks + 1) *
				   sizeof(struct atapi_toc_entry), sense);

	if (stat && toc->hdr.first_track > 1) {
		/* Cds with CDI tracks only don't have any TOC entries,
		   despite of this the returned values are
		   first_track == last_track = number of CDI tracks + 1,
		   so that this case is indistinguishable from the same
		   layout plus an additional audio track.
		   If we get an error for the regular case, we assume
		   a CDI without additional audio tracks. In this case
		   the readable TOC is empty (CDI tracks are not included)
		   and only holds the Leadout entry. Heiko Eißfeldt */
		ntracks = 0;
		stat = cdrom_read_tocentry(drive, CDROM_LEADOUT, 1, 0,
					   (char *)&toc->hdr,
					   sizeof(struct atapi_toc_header) +
					   (ntracks + 1) *
					   sizeof(struct atapi_toc_entry),
					   sense);
		if (stat) {
			return stat;
		}
#if ! STANDARD_ATAPI
		if (CDROM_CONFIG_FLAGS(drive)->toctracks_as_bcd) {
			toc->hdr.first_track = bin2bcd(CDROM_LEADOUT);
			toc->hdr.last_track = bin2bcd(CDROM_LEADOUT);
		} else
#endif  /* not STANDARD_ATAPI */
		{
			toc->hdr.first_track = CDROM_LEADOUT;
			toc->hdr.last_track = CDROM_LEADOUT;
		}
	}

	if (stat)
		return stat;

	toc->hdr.toc_length = ntohs (toc->hdr.toc_length);

#if ! STANDARD_ATAPI
	if (CDROM_CONFIG_FLAGS(drive)->toctracks_as_bcd) {
		toc->hdr.first_track = bcd2bin(toc->hdr.first_track);
		toc->hdr.last_track  = bcd2bin(toc->hdr.last_track);
	}
#endif  /* not STANDARD_ATAPI */

	for (i=0; i<=ntracks; i++) {
#if ! STANDARD_ATAPI
		if (CDROM_CONFIG_FLAGS(drive)->tocaddr_as_bcd) {
			if (CDROM_CONFIG_FLAGS(drive)->toctracks_as_bcd)
				toc->ent[i].track = bcd2bin(toc->ent[i].track);
			msf_from_bcd(&toc->ent[i].addr.msf);
		}
#endif  /* not STANDARD_ATAPI */
		toc->ent[i].addr.lba = msf_to_lba (toc->ent[i].addr.msf.minute,
						   toc->ent[i].addr.msf.second,
						   toc->ent[i].addr.msf.frame);
	}

	/* Read the multisession information. */
	if (toc->hdr.first_track != CDROM_LEADOUT) {
		/* Read the multisession information. */
		stat = cdrom_read_tocentry(drive, 0, 0, 1, (char *)&ms_tmp,
					   sizeof(ms_tmp), sense);
		if (stat)
			return stat;

		toc->last_session_lba = be32_to_cpu(ms_tmp.ent.addr.lba);
	} else {
		ms_tmp.hdr.first_track = ms_tmp.hdr.last_track = CDROM_LEADOUT;
		toc->last_session_lba = msf_to_lba(0, 2, 0); /* 0m 2s 0f */
	}

#if ! STANDARD_ATAPI
	if (CDROM_CONFIG_FLAGS(drive)->tocaddr_as_bcd) {
		/* Re-read multisession information using MSF format */
		stat = cdrom_read_tocentry(drive, 0, 1, 1, (char *)&ms_tmp,
					   sizeof(ms_tmp), sense);
		if (stat)
			return stat;

		msf_from_bcd (&ms_tmp.ent.addr.msf);
		toc->last_session_lba = msf_to_lba(ms_tmp.ent.addr.msf.minute,
					  	   ms_tmp.ent.addr.msf.second,
						   ms_tmp.ent.addr.msf.frame);
	}
#endif  /* not STANDARD_ATAPI */

	toc->xa_flag = (ms_tmp.hdr.first_track != ms_tmp.hdr.last_track);

	/* Now try to get the total cdrom capacity. */
	stat = cdrom_get_last_written(cdi, &last_written);
	if (!stat && (last_written > toc->capacity)) {
		toc->capacity = last_written;
		set_capacity(info->disk, toc->capacity * sectors_per_frame);
		drive->probed_capacity = toc->capacity * sectors_per_frame;
	}

	/* Remember that we've read this stuff. */
	CDROM_STATE_FLAGS(drive)->toc_valid = 1;

	return 0;
}


static int cdrom_read_subchannel(ide_drive_t *drive, int format, char *buf,
				 int buflen, struct request_sense *sense)
{
	struct request req;

	cdrom_prepare_request(drive, &req);

	req.sense = sense;
	req.data = buf;
	req.data_len = buflen;
	req.cmd[0] = GPCMD_READ_SUBCHANNEL;
	req.cmd[1] = 2;     /* MSF addressing */
	req.cmd[2] = 0x40;  /* request subQ data */
	req.cmd[3] = format;
	req.cmd[7] = (buflen >> 8);
	req.cmd[8] = (buflen & 0xff);
	return cdrom_queue_packet_command(drive, &req);
}

/* ATAPI cdrom drives are free to select the speed you request or any slower
   rate :-( Requesting too fast a speed will _not_ produce an error. */
static int cdrom_select_speed(ide_drive_t *drive, int speed,
			      struct request_sense *sense)
{
	struct request req;
	cdrom_prepare_request(drive, &req);

	req.sense = sense;
	if (speed == 0)
		speed = 0xffff; /* set to max */
	else
		speed *= 177;   /* Nx to kbytes/s */

	req.cmd[0] = GPCMD_SET_SPEED;
	/* Read Drive speed in kbytes/second MSB */
	req.cmd[2] = (speed >> 8) & 0xff;	
	/* Read Drive speed in kbytes/second LSB */
	req.cmd[3] = speed & 0xff;
	if (CDROM_CONFIG_FLAGS(drive)->cd_r ||
	    CDROM_CONFIG_FLAGS(drive)->cd_rw ||
	    CDROM_CONFIG_FLAGS(drive)->dvd_r) {
		/* Write Drive speed in kbytes/second MSB */
		req.cmd[4] = (speed >> 8) & 0xff;
		/* Write Drive speed in kbytes/second LSB */
		req.cmd[5] = speed & 0xff;
       }

	return cdrom_queue_packet_command(drive, &req);
}

static int cdrom_play_audio(ide_drive_t *drive, int lba_start, int lba_end)
{
	struct request_sense sense;
	struct request req;

	cdrom_prepare_request(drive, &req);

	req.sense = &sense;
	req.cmd[0] = GPCMD_PLAY_AUDIO_MSF;
	lba_to_msf(lba_start, &req.cmd[3], &req.cmd[4], &req.cmd[5]);
	lba_to_msf(lba_end-1, &req.cmd[6], &req.cmd[7], &req.cmd[8]);

	return cdrom_queue_packet_command(drive, &req);
}

static int cdrom_get_toc_entry(ide_drive_t *drive, int track,
				struct atapi_toc_entry **ent)
{
	struct cdrom_info *info = drive->driver_data;
	struct atapi_toc *toc = info->toc;
	int ntracks;

	/*
	 * don't serve cached data, if the toc isn't valid
	 */
	if (!CDROM_STATE_FLAGS(drive)->toc_valid)
		return -EINVAL;

	/* Check validity of requested track number. */
	ntracks = toc->hdr.last_track - toc->hdr.first_track + 1;
	if (toc->hdr.first_track == CDROM_LEADOUT) ntracks = 0;
	if (track == CDROM_LEADOUT)
		*ent = &toc->ent[ntracks];
	else if (track < toc->hdr.first_track ||
		 track > toc->hdr.last_track)
		return -EINVAL;
	else
		*ent = &toc->ent[track - toc->hdr.first_track];

	return 0;
}

/* the generic packet interface to cdrom.c */
static int ide_cdrom_packet(struct cdrom_device_info *cdi,
			    struct packet_command *cgc)
{
	struct request req;
	ide_drive_t *drive = cdi->handle;

	if (cgc->timeout <= 0)
		cgc->timeout = ATAPI_WAIT_PC;

	/* here we queue the commands from the uniform CD-ROM
	   layer. the packet must be complete, as we do not
	   touch it at all. */
	cdrom_prepare_request(drive, &req);
	memcpy(req.cmd, cgc->cmd, CDROM_PACKET_SIZE);
	if (cgc->sense)
		memset(cgc->sense, 0, sizeof(struct request_sense));
	req.data = cgc->buffer;
	req.data_len = cgc->buflen;
	req.timeout = cgc->timeout;

	if (cgc->quiet)
		req.cmd_flags |= REQ_QUIET;

	req.sense = cgc->sense;
	cgc->stat = cdrom_queue_packet_command(drive, &req);
	if (!cgc->stat)
		cgc->buflen -= req.data_len;
	return cgc->stat;
}

static
int ide_cdrom_audio_ioctl (struct cdrom_device_info *cdi,
			   unsigned int cmd, void *arg)
			   
{
	ide_drive_t *drive = cdi->handle;
	struct cdrom_info *info = drive->driver_data;
	int stat;

	switch (cmd) {
	/*
	 * emulate PLAY_AUDIO_TI command with PLAY_AUDIO_10, since
	 * atapi doesn't support it
	 */
	case CDROMPLAYTRKIND: {
		unsigned long lba_start, lba_end;
		struct cdrom_ti *ti = arg;
		struct atapi_toc_entry *first_toc, *last_toc;

		stat = cdrom_get_toc_entry(drive, ti->cdti_trk0, &first_toc);
		if (stat)
			return stat;

		stat = cdrom_get_toc_entry(drive, ti->cdti_trk1, &last_toc);
		if (stat)
			return stat;

		if (ti->cdti_trk1 != CDROM_LEADOUT)
			++last_toc;
		lba_start = first_toc->addr.lba;
		lba_end   = last_toc->addr.lba;

		if (lba_end <= lba_start)
			return -EINVAL;

		return cdrom_play_audio(drive, lba_start, lba_end);
	}

	case CDROMREADTOCHDR: {
		struct cdrom_tochdr *tochdr = arg;
		struct atapi_toc *toc;

		/* Make sure our saved TOC is valid. */
		stat = cdrom_read_toc(drive, NULL);
		if (stat)
			return stat;

		toc = info->toc;
		tochdr->cdth_trk0 = toc->hdr.first_track;
		tochdr->cdth_trk1 = toc->hdr.last_track;

		return 0;
	}

	case CDROMREADTOCENTRY: {
		struct cdrom_tocentry *tocentry = arg;
		struct atapi_toc_entry *toce;

		stat = cdrom_get_toc_entry(drive, tocentry->cdte_track, &toce);
		if (stat)
			return stat;

		tocentry->cdte_ctrl = toce->control;
		tocentry->cdte_adr  = toce->adr;
		if (tocentry->cdte_format == CDROM_MSF) {
			lba_to_msf (toce->addr.lba,
				   &tocentry->cdte_addr.msf.minute,
				   &tocentry->cdte_addr.msf.second,
				   &tocentry->cdte_addr.msf.frame);
		} else
			tocentry->cdte_addr.lba = toce->addr.lba;

		return 0;
	}

	default:
		return -EINVAL;
	}
}

static
int ide_cdrom_reset (struct cdrom_device_info *cdi)
{
	ide_drive_t *drive = cdi->handle;
	struct request_sense sense;
	struct request req;
	int ret;

	cdrom_prepare_request(drive, &req);
	req.cmd_type = REQ_TYPE_SPECIAL;
	req.cmd_flags = REQ_QUIET;
	ret = ide_do_drive_cmd(drive, &req, ide_wait);

	/*
	 * A reset will unlock the door. If it was previously locked,
	 * lock it again.
	 */
	if (CDROM_STATE_FLAGS(drive)->door_locked)
		(void) cdrom_lockdoor(drive, 1, &sense);

	return ret;
}


static
int ide_cdrom_tray_move (struct cdrom_device_info *cdi, int position)
{
	ide_drive_t *drive = cdi->handle;
	struct request_sense sense;

	if (position) {
		int stat = cdrom_lockdoor(drive, 0, &sense);
		if (stat)
			return stat;
	}

	return cdrom_eject(drive, !position, &sense);
}

static
int ide_cdrom_lock_door (struct cdrom_device_info *cdi, int lock)
{
	ide_drive_t *drive = cdi->handle;
	return cdrom_lockdoor(drive, lock, NULL);
}

static
int ide_cdrom_get_capabilities(ide_drive_t *drive, struct atapi_capabilities_page *cap)
{
	struct cdrom_info *info = drive->driver_data;
	struct cdrom_device_info *cdi = &info->devinfo;
	struct packet_command cgc;
	int stat, attempts = 3, size = sizeof(*cap);

	/*
	 * ACER50 (and others?) require the full spec length mode sense
	 * page capabilities size, but older drives break.
	 */
	if (!(!strcmp(drive->id->model, "ATAPI CD ROM DRIVE 50X MAX") ||
	    !strcmp(drive->id->model, "WPI CDS-32X")))
		size -= sizeof(cap->pad);

	init_cdrom_command(&cgc, cap, size, CGC_DATA_UNKNOWN);
	do { /* we seem to get stat=0x01,err=0x00 the first time (??) */
		stat = cdrom_mode_sense(cdi, &cgc, GPMODE_CAPABILITIES_PAGE, 0);
		if (!stat)
			break;
	} while (--attempts);
	return stat;
}

static
void ide_cdrom_update_speed (ide_drive_t *drive, struct atapi_capabilities_page *cap)
{
	/* The ACER/AOpen 24X cdrom has the speed fields byte-swapped */
	if (!drive->id->model[0] &&
	    !strncmp(drive->id->fw_rev, "241N", 4)) {
		CDROM_STATE_FLAGS(drive)->current_speed  =
			(((unsigned int)cap->curspeed) + (176/2)) / 176;
		CDROM_CONFIG_FLAGS(drive)->max_speed =
			(((unsigned int)cap->maxspeed) + (176/2)) / 176;
	} else {
		CDROM_STATE_FLAGS(drive)->current_speed  =
			(ntohs(cap->curspeed) + (176/2)) / 176;
		CDROM_CONFIG_FLAGS(drive)->max_speed =
			(ntohs(cap->maxspeed) + (176/2)) / 176;
	}
}

static
int ide_cdrom_select_speed (struct cdrom_device_info *cdi, int speed)
{
	ide_drive_t *drive = cdi->handle;
	struct request_sense sense;
	struct atapi_capabilities_page cap;
	int stat;

	if ((stat = cdrom_select_speed(drive, speed, &sense)) < 0)
		return stat;

	if (!ide_cdrom_get_capabilities(drive, &cap)) {
		ide_cdrom_update_speed(drive, &cap);
		cdi->speed = CDROM_STATE_FLAGS(drive)->current_speed;
	}
        return 0;
}

/*
 * add logic to try GET_EVENT command first to check for media and tray
 * status. this should be supported by newer cd-r/w and all DVD etc
 * drives
 */
static
int ide_cdrom_drive_status (struct cdrom_device_info *cdi, int slot_nr)
{
	ide_drive_t *drive = cdi->handle;
	struct media_event_desc med;
	struct request_sense sense;
	int stat;

	if (slot_nr != CDSL_CURRENT)
		return -EINVAL;

	stat = cdrom_check_status(drive, &sense);
	if (!stat || sense.sense_key == UNIT_ATTENTION)
		return CDS_DISC_OK;

	if (!cdrom_get_media_event(cdi, &med)) {
		if (med.media_present)
			return CDS_DISC_OK;
		else if (med.door_open)
			return CDS_TRAY_OPEN;
		else
			return CDS_NO_DISC;
	}

	if (sense.sense_key == NOT_READY && sense.asc == 0x04 && sense.ascq == 0x04)
		return CDS_DISC_OK;

	/*
	 * If not using Mt Fuji extended media tray reports,
	 * just return TRAY_OPEN since ATAPI doesn't provide
	 * any other way to detect this...
	 */
	if (sense.sense_key == NOT_READY) {
		if (sense.asc == 0x3a && sense.ascq == 1)
			return CDS_NO_DISC;
		else
			return CDS_TRAY_OPEN;
	}
	return CDS_DRIVE_NOT_READY;
}

static
int ide_cdrom_get_last_session (struct cdrom_device_info *cdi,
				struct cdrom_multisession *ms_info)
{
	struct atapi_toc *toc;
	ide_drive_t *drive = cdi->handle;
	struct cdrom_info *info = drive->driver_data;
	struct request_sense sense;
	int ret;

	if (!CDROM_STATE_FLAGS(drive)->toc_valid || info->toc == NULL)
		if ((ret = cdrom_read_toc(drive, &sense)))
			return ret;

	toc = info->toc;
	ms_info->addr.lba = toc->last_session_lba;
	ms_info->xa_flag = toc->xa_flag;

	return 0;
}

static
int ide_cdrom_get_mcn (struct cdrom_device_info *cdi,
		       struct cdrom_mcn *mcn_info)
{
	int stat;
	char mcnbuf[24];
	ide_drive_t *drive = cdi->handle;

/* get MCN */
	if ((stat = cdrom_read_subchannel(drive, 2, mcnbuf, sizeof (mcnbuf), NULL)))
		return stat;

	memcpy (mcn_info->medium_catalog_number, mcnbuf+9,
		sizeof (mcn_info->medium_catalog_number)-1);
	mcn_info->medium_catalog_number[sizeof (mcn_info->medium_catalog_number)-1]
		= '\0';

	return 0;
}



/****************************************************************************
 * Other driver requests (open, close, check media change).
 */

static
int ide_cdrom_check_media_change_real (struct cdrom_device_info *cdi,
				       int slot_nr)
{
	ide_drive_t *drive = cdi->handle;
	int retval;
	
	if (slot_nr == CDSL_CURRENT) {
		(void) cdrom_check_status(drive, NULL);
		retval = CDROM_STATE_FLAGS(drive)->media_changed;
		CDROM_STATE_FLAGS(drive)->media_changed = 0;
		return retval;
	} else {
		return -EINVAL;
	}
}


static
int ide_cdrom_open_real (struct cdrom_device_info *cdi, int purpose)
{
	return 0;
}

/*
 * Close down the device.  Invalidate all cached blocks.
 */

static
void ide_cdrom_release_real (struct cdrom_device_info *cdi)
{
	ide_drive_t *drive = cdi->handle;

	if (!cdi->use_count)
		CDROM_STATE_FLAGS(drive)->toc_valid = 0;
}



/****************************************************************************
 * Device initialization.
 */
static struct cdrom_device_ops ide_cdrom_dops = {
	.open			= ide_cdrom_open_real,
	.release		= ide_cdrom_release_real,
	.drive_status		= ide_cdrom_drive_status,
	.media_changed		= ide_cdrom_check_media_change_real,
	.tray_move		= ide_cdrom_tray_move,
	.lock_door		= ide_cdrom_lock_door,
	.select_speed		= ide_cdrom_select_speed,
	.get_last_session	= ide_cdrom_get_last_session,
	.get_mcn		= ide_cdrom_get_mcn,
	.reset			= ide_cdrom_reset,
	.audio_ioctl		= ide_cdrom_audio_ioctl,
	.capability		= CDC_CLOSE_TRAY | CDC_OPEN_TRAY | CDC_LOCK |
				CDC_SELECT_SPEED | CDC_SELECT_DISC |
				CDC_MULTI_SESSION | CDC_MCN |
				CDC_MEDIA_CHANGED | CDC_PLAY_AUDIO | CDC_RESET |
				CDC_DRIVE_STATUS | CDC_CD_R |
				CDC_CD_RW | CDC_DVD | CDC_DVD_R| CDC_DVD_RAM |
				CDC_GENERIC_PACKET | CDC_MO_DRIVE | CDC_MRW |
				CDC_MRW_W | CDC_RAM,
	.generic_packet		= ide_cdrom_packet,
};

static int ide_cdrom_register (ide_drive_t *drive, int nslots)
{
	struct cdrom_info *info = drive->driver_data;
	struct cdrom_device_info *devinfo = &info->devinfo;

	devinfo->ops = &ide_cdrom_dops;
	devinfo->mask = 0;
	devinfo->speed = CDROM_STATE_FLAGS(drive)->current_speed;
	devinfo->capacity = nslots;
	devinfo->handle = drive;
	strcpy(devinfo->name, drive->name);
	
	/* set capability mask to match the probe. */
	if (!CDROM_CONFIG_FLAGS(drive)->cd_r)
		devinfo->mask |= CDC_CD_R;
	if (!CDROM_CONFIG_FLAGS(drive)->cd_rw)
		devinfo->mask |= CDC_CD_RW;
	if (!CDROM_CONFIG_FLAGS(drive)->dvd)
		devinfo->mask |= CDC_DVD;
	if (!CDROM_CONFIG_FLAGS(drive)->dvd_r)
		devinfo->mask |= CDC_DVD_R;
	if (!CDROM_CONFIG_FLAGS(drive)->dvd_ram)
		devinfo->mask |= CDC_DVD_RAM;
	if (!CDROM_CONFIG_FLAGS(drive)->is_changer)
		devinfo->mask |= CDC_SELECT_DISC;
	if (!CDROM_CONFIG_FLAGS(drive)->audio_play)
		devinfo->mask |= CDC_PLAY_AUDIO;
	if (!CDROM_CONFIG_FLAGS(drive)->close_tray)
		devinfo->mask |= CDC_CLOSE_TRAY;
	if (!CDROM_CONFIG_FLAGS(drive)->mo_drive)
		devinfo->mask |= CDC_MO_DRIVE;
	if (!CDROM_CONFIG_FLAGS(drive)->ram)
		devinfo->mask |= CDC_RAM;

	devinfo->disk = info->disk;
	return register_cdrom(devinfo);
}

static
int ide_cdrom_probe_capabilities (ide_drive_t *drive)
{
	struct cdrom_info *info = drive->driver_data;
	struct cdrom_device_info *cdi = &info->devinfo;
	struct atapi_capabilities_page cap;
	int nslots = 1;

	if (drive->media == ide_optical) {
		CDROM_CONFIG_FLAGS(drive)->mo_drive = 1;
		CDROM_CONFIG_FLAGS(drive)->ram = 1;
		printk(KERN_ERR "%s: ATAPI magneto-optical drive\n", drive->name);
		return nslots;
	}

	if (CDROM_CONFIG_FLAGS(drive)->nec260 ||
	    !strcmp(drive->id->model,"STINGRAY 8422 IDE 8X CD-ROM 7-27-95")) {
		CDROM_CONFIG_FLAGS(drive)->no_eject = 0;
		CDROM_CONFIG_FLAGS(drive)->audio_play = 1;
		return nslots;
	}

	/*
	 * we have to cheat a little here. the packet will eventually
	 * be queued with ide_cdrom_packet(), which extracts the
	 * drive from cdi->handle. Since this device hasn't been
	 * registered with the Uniform layer yet, it can't do this.
	 * Same goes for cdi->ops.
	 */
	cdi->handle = drive;
	cdi->ops = &ide_cdrom_dops;

	if (ide_cdrom_get_capabilities(drive, &cap))
		return 0;

	if (cap.lock == 0)
		CDROM_CONFIG_FLAGS(drive)->no_doorlock = 1;
	if (cap.eject)
		CDROM_CONFIG_FLAGS(drive)->no_eject = 0;
	if (cap.cd_r_write)
		CDROM_CONFIG_FLAGS(drive)->cd_r = 1;
	if (cap.cd_rw_write) {
		CDROM_CONFIG_FLAGS(drive)->cd_rw = 1;
		CDROM_CONFIG_FLAGS(drive)->ram = 1;
	}
	if (cap.test_write)
		CDROM_CONFIG_FLAGS(drive)->test_write = 1;
	if (cap.dvd_ram_read || cap.dvd_r_read || cap.dvd_rom)
		CDROM_CONFIG_FLAGS(drive)->dvd = 1;
	if (cap.dvd_ram_write) {
		CDROM_CONFIG_FLAGS(drive)->dvd_ram = 1;
		CDROM_CONFIG_FLAGS(drive)->ram = 1;
	}
	if (cap.dvd_r_write)
		CDROM_CONFIG_FLAGS(drive)->dvd_r = 1;
	if (cap.audio_play)
		CDROM_CONFIG_FLAGS(drive)->audio_play = 1;
	if (cap.mechtype == mechtype_caddy || cap.mechtype == mechtype_popup)
		CDROM_CONFIG_FLAGS(drive)->close_tray = 0;

	/* Some drives used by Apple don't advertise audio play
	 * but they do support reading TOC & audio datas
	 */
	if (strcmp(drive->id->model, "MATSHITADVD-ROM SR-8187") == 0 ||
	    strcmp(drive->id->model, "MATSHITADVD-ROM SR-8186") == 0 ||
	    strcmp(drive->id->model, "MATSHITADVD-ROM SR-8176") == 0 ||
	    strcmp(drive->id->model, "MATSHITADVD-ROM SR-8174") == 0)
		CDROM_CONFIG_FLAGS(drive)->audio_play = 1;

#if ! STANDARD_ATAPI
	if (cdi->sanyo_slot > 0) {
		CDROM_CONFIG_FLAGS(drive)->is_changer = 1;
		nslots = 3;
	}

	else
#endif /* not STANDARD_ATAPI */
	if (cap.mechtype == mechtype_individual_changer ||
	    cap.mechtype == mechtype_cartridge_changer) {
		if ((nslots = cdrom_number_of_slots(cdi)) > 1) {
			CDROM_CONFIG_FLAGS(drive)->is_changer = 1;
			CDROM_CONFIG_FLAGS(drive)->supp_disc_present = 1;
		}
	}

	ide_cdrom_update_speed(drive, &cap);
	/* don't print speed if the drive reported 0.
	 */
	printk(KERN_INFO "%s: ATAPI", drive->name);
	if (CDROM_CONFIG_FLAGS(drive)->max_speed)
		printk(" %dX", CDROM_CONFIG_FLAGS(drive)->max_speed);
	printk(" %s", CDROM_CONFIG_FLAGS(drive)->dvd ? "DVD-ROM" : "CD-ROM");

	if (CDROM_CONFIG_FLAGS(drive)->dvd_r|CDROM_CONFIG_FLAGS(drive)->dvd_ram)
        	printk(" DVD%s%s", 
        	(CDROM_CONFIG_FLAGS(drive)->dvd_r)? "-R" : "", 
        	(CDROM_CONFIG_FLAGS(drive)->dvd_ram)? "-RAM" : "");

        if (CDROM_CONFIG_FLAGS(drive)->cd_r|CDROM_CONFIG_FLAGS(drive)->cd_rw) 
        	printk(" CD%s%s", 
        	(CDROM_CONFIG_FLAGS(drive)->cd_r)? "-R" : "", 
        	(CDROM_CONFIG_FLAGS(drive)->cd_rw)? "/RW" : "");

        if (CDROM_CONFIG_FLAGS(drive)->is_changer) 
        	printk(" changer w/%d slots", nslots);
        else 	
        	printk(" drive");

	printk(", %dkB Cache", be16_to_cpu(cap.buffer_size));

	if (drive->using_dma)
		ide_dma_verbose(drive);

	printk("\n");

	return nslots;
}

static void ide_cdrom_add_settings(ide_drive_t *drive)
{
	ide_add_setting(drive,	"dsc_overlap",		SETTING_RW, -1, -1, TYPE_BYTE, 0, 1, 1,	1, &drive->dsc_overlap, NULL);
}

/*
 * standard prep_rq_fn that builds 10 byte cmds
 */
static int ide_cdrom_prep_fs(request_queue_t *q, struct request *rq)
{
	int hard_sect = queue_hardsect_size(q);
	long block = (long)rq->hard_sector / (hard_sect >> 9);
	unsigned long blocks = rq->hard_nr_sectors / (hard_sect >> 9);

	memset(rq->cmd, 0, sizeof(rq->cmd));

	if (rq_data_dir(rq) == READ)
		rq->cmd[0] = GPCMD_READ_10;
	else
		rq->cmd[0] = GPCMD_WRITE_10;

	/*
	 * fill in lba
	 */
	rq->cmd[2] = (block >> 24) & 0xff;
	rq->cmd[3] = (block >> 16) & 0xff;
	rq->cmd[4] = (block >>  8) & 0xff;
	rq->cmd[5] = block & 0xff;

	/*
	 * and transfer length
	 */
	rq->cmd[7] = (blocks >> 8) & 0xff;
	rq->cmd[8] = blocks & 0xff;
	rq->cmd_len = 10;
	return BLKPREP_OK;
}

/*
 * Most of the SCSI commands are supported directly by ATAPI devices.
 * This transform handles the few exceptions.
 */
static int ide_cdrom_prep_pc(struct request *rq)
{
	u8 *c = rq->cmd;

	/*
	 * Transform 6-byte read/write commands to the 10-byte version
	 */
	if (c[0] == READ_6 || c[0] == WRITE_6) {
		c[8] = c[4];
		c[5] = c[3];
		c[4] = c[2];
		c[3] = c[1] & 0x1f;
		c[2] = 0;
		c[1] &= 0xe0;
		c[0] += (READ_10 - READ_6);
		rq->cmd_len = 10;
		return BLKPREP_OK;
	}

	/*
	 * it's silly to pretend we understand 6-byte sense commands, just
	 * reject with ILLEGAL_REQUEST and the caller should take the
	 * appropriate action
	 */
	if (c[0] == MODE_SENSE || c[0] == MODE_SELECT) {
		rq->errors = ILLEGAL_REQUEST;
		return BLKPREP_KILL;
	}
	
	return BLKPREP_OK;
}

static int ide_cdrom_prep_fn(request_queue_t *q, struct request *rq)
{
	if (blk_fs_request(rq))
		return ide_cdrom_prep_fs(q, rq);
	else if (blk_pc_request(rq))
		return ide_cdrom_prep_pc(rq);

	return 0;
}

static
int ide_cdrom_setup (ide_drive_t *drive)
{
	struct cdrom_info *info = drive->driver_data;
	struct cdrom_device_info *cdi = &info->devinfo;
	int nslots;

	blk_queue_prep_rq(drive->queue, ide_cdrom_prep_fn);
	blk_queue_dma_alignment(drive->queue, 31);
	drive->queue->unplug_delay = (1 * HZ) / 1000;
	if (!drive->queue->unplug_delay)
		drive->queue->unplug_delay = 1;

	drive->special.all	= 0;

	CDROM_STATE_FLAGS(drive)->media_changed = 1;
	CDROM_STATE_FLAGS(drive)->toc_valid     = 0;
	CDROM_STATE_FLAGS(drive)->door_locked   = 0;

#if NO_DOOR_LOCKING
	CDROM_CONFIG_FLAGS(drive)->no_doorlock = 1;
#else
	CDROM_CONFIG_FLAGS(drive)->no_doorlock = 0;
#endif

	CDROM_CONFIG_FLAGS(drive)->drq_interrupt = ((drive->id->config & 0x0060) == 0x20);
	CDROM_CONFIG_FLAGS(drive)->is_changer = 0;
	CDROM_CONFIG_FLAGS(drive)->cd_r = 0;
	CDROM_CONFIG_FLAGS(drive)->cd_rw = 0;
	CDROM_CONFIG_FLAGS(drive)->test_write = 0;
	CDROM_CONFIG_FLAGS(drive)->dvd = 0;
	CDROM_CONFIG_FLAGS(drive)->dvd_r = 0;
	CDROM_CONFIG_FLAGS(drive)->dvd_ram = 0;
	CDROM_CONFIG_FLAGS(drive)->no_eject = 1;
	CDROM_CONFIG_FLAGS(drive)->supp_disc_present = 0;
	CDROM_CONFIG_FLAGS(drive)->audio_play = 0;
	CDROM_CONFIG_FLAGS(drive)->close_tray = 1;
	
	/* limit transfer size per interrupt. */
	CDROM_CONFIG_FLAGS(drive)->limit_nframes = 0;
	/* a testament to the nice quality of Samsung drives... */
	if (!strcmp(drive->id->model, "SAMSUNG CD-ROM SCR-2430"))
		CDROM_CONFIG_FLAGS(drive)->limit_nframes = 1;
	else if (!strcmp(drive->id->model, "SAMSUNG CD-ROM SCR-2432"))
		CDROM_CONFIG_FLAGS(drive)->limit_nframes = 1;
	/* the 3231 model does not support the SET_CD_SPEED command */
	else if (!strcmp(drive->id->model, "SAMSUNG CD-ROM SCR-3231"))
		cdi->mask |= CDC_SELECT_SPEED;

#if ! STANDARD_ATAPI
	/* by default Sanyo 3 CD changer support is turned off and
           ATAPI Rev 2.2+ standard support for CD changers is used */
	cdi->sanyo_slot = 0;

	CDROM_CONFIG_FLAGS(drive)->nec260 = 0;
	CDROM_CONFIG_FLAGS(drive)->toctracks_as_bcd = 0;
	CDROM_CONFIG_FLAGS(drive)->tocaddr_as_bcd = 0;
	CDROM_CONFIG_FLAGS(drive)->playmsf_as_bcd = 0;
	CDROM_CONFIG_FLAGS(drive)->subchan_as_bcd = 0;

	if (strcmp (drive->id->model, "V003S0DS") == 0 &&
	    drive->id->fw_rev[4] == '1' &&
	    drive->id->fw_rev[6] <= '2') {
		/* Vertos 300.
		   Some versions of this drive like to talk BCD. */
		CDROM_CONFIG_FLAGS(drive)->toctracks_as_bcd = 1;
		CDROM_CONFIG_FLAGS(drive)->tocaddr_as_bcd = 1;
		CDROM_CONFIG_FLAGS(drive)->playmsf_as_bcd = 1;
		CDROM_CONFIG_FLAGS(drive)->subchan_as_bcd = 1;
	}

	else if (strcmp (drive->id->model, "V006E0DS") == 0 &&
	    drive->id->fw_rev[4] == '1' &&
	    drive->id->fw_rev[6] <= '2') {
		/* Vertos 600 ESD. */
		CDROM_CONFIG_FLAGS(drive)->toctracks_as_bcd = 1;
	}
	else if (strcmp(drive->id->model, "NEC CD-ROM DRIVE:260") == 0 &&
		 strncmp(drive->id->fw_rev, "1.01", 4) == 0) { /* FIXME */
		/* Old NEC260 (not R).
		   This drive was released before the 1.2 version
		   of the spec. */
		CDROM_CONFIG_FLAGS(drive)->tocaddr_as_bcd = 1;
		CDROM_CONFIG_FLAGS(drive)->playmsf_as_bcd = 1;
		CDROM_CONFIG_FLAGS(drive)->subchan_as_bcd = 1;
		CDROM_CONFIG_FLAGS(drive)->nec260         = 1;
	}
	else if (strcmp(drive->id->model, "WEARNES CDD-120") == 0 &&
		 strncmp(drive->id->fw_rev, "A1.1", 4) == 0) { /* FIXME */
		/* Wearnes */
		CDROM_CONFIG_FLAGS(drive)->playmsf_as_bcd = 1;
		CDROM_CONFIG_FLAGS(drive)->subchan_as_bcd = 1;
	}
        /* Sanyo 3 CD changer uses a non-standard command
           for CD changing */
        else if ((strcmp(drive->id->model, "CD-ROM CDR-C3 G") == 0) ||
                 (strcmp(drive->id->model, "CD-ROM CDR-C3G") == 0) ||
                 (strcmp(drive->id->model, "CD-ROM CDR_C36") == 0)) {
                 /* uses CD in slot 0 when value is set to 3 */
                 cdi->sanyo_slot = 3;
        }
#endif /* not STANDARD_ATAPI */

	info->toc		= NULL;
	info->buffer		= NULL;
	info->sector_buffered	= 0;
	info->nsectors_buffered	= 0;
	info->changer_info      = NULL;
	info->last_block	= 0;
	info->start_seek	= 0;

	nslots = ide_cdrom_probe_capabilities (drive);

	/*
	 * set correct block size
	 */
	blk_queue_hardsect_size(drive->queue, CD_FRAMESIZE);

	if (drive->autotune == IDE_TUNE_DEFAULT ||
	    drive->autotune == IDE_TUNE_AUTO)
		drive->dsc_overlap = (drive->next != drive);
#if 0
	drive->dsc_overlap = (HWIF(drive)->no_dsc) ? 0 : 1;
	if (HWIF(drive)->no_dsc) {
		printk(KERN_INFO "ide-cd: %s: disabling DSC overlap\n",
			drive->name);
		drive->dsc_overlap = 0;
	}
#endif

	if (ide_cdrom_register(drive, nslots)) {
		printk (KERN_ERR "%s: ide_cdrom_setup failed to register device with the cdrom driver.\n", drive->name);
		info->devinfo.handle = NULL;
		return 1;
	}
	ide_cdrom_add_settings(drive);
	return 0;
}

#ifdef CONFIG_PROC_FS
static
sector_t ide_cdrom_capacity (ide_drive_t *drive)
{
	unsigned long capacity, sectors_per_frame;

	if (cdrom_read_capacity(drive, &capacity, &sectors_per_frame, NULL))
		return 0;

	return capacity * sectors_per_frame;
}
#endif

static void ide_cd_remove(ide_drive_t *drive)
{
	struct cdrom_info *info = drive->driver_data;

	ide_unregister_subdriver(drive, info->driver);

	del_gendisk(info->disk);

	ide_cd_put(info);
}

static void ide_cd_release(struct kref *kref)
{
	struct cdrom_info *info = to_ide_cd(kref);
	struct cdrom_device_info *devinfo = &info->devinfo;
	ide_drive_t *drive = info->drive;
	struct gendisk *g = info->disk;

	kfree(info->buffer);
	kfree(info->toc);
	kfree(info->changer_info);
	if (devinfo->handle == drive && unregister_cdrom(devinfo))
		printk(KERN_ERR "%s: %s failed to unregister device from the cdrom "
				"driver.\n", __FUNCTION__, drive->name);
	drive->dsc_overlap = 0;
	drive->driver_data = NULL;
	blk_queue_prep_rq(drive->queue, NULL);
	g->private_data = NULL;
	put_disk(g);
	kfree(info);
}

static int ide_cd_probe(ide_drive_t *);

#ifdef CONFIG_PROC_FS
static int proc_idecd_read_capacity
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t *drive = data;
	int len;

	len = sprintf(page,"%llu\n", (long long)ide_cdrom_capacity(drive));
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static ide_proc_entry_t idecd_proc[] = {
	{ "capacity", S_IFREG|S_IRUGO, proc_idecd_read_capacity, NULL },
	{ NULL, 0, NULL, NULL }
};
#else
# define idecd_proc	NULL
#endif

static ide_driver_t ide_cdrom_driver = {
	.gen_driver = {
		.owner		= THIS_MODULE,
		.name		= "ide-cdrom",
		.bus		= &ide_bus_type,
	},
	.probe			= ide_cd_probe,
	.remove			= ide_cd_remove,
	.version		= IDECD_VERSION,
	.media			= ide_cdrom,
	.supports_dsc_overlap	= 1,
	.do_request		= ide_do_rw_cdrom,
	.end_request		= ide_end_request,
	.error			= __ide_error,
	.abort			= __ide_abort,
	.proc			= idecd_proc,
};

static int idecd_open(struct inode * inode, struct file * file)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct cdrom_info *info;
	ide_drive_t *drive;
	int rc = -ENOMEM;

	if (!(info = ide_cd_get(disk)))
		return -ENXIO;

	drive = info->drive;

	drive->usage++;

	if (!info->buffer)
		info->buffer = kmalloc(SECTOR_BUFFER_SIZE,
					GFP_KERNEL|__GFP_REPEAT);
        if (!info->buffer || (rc = cdrom_open(&info->devinfo, inode, file)))
		drive->usage--;

	if (rc < 0)
		ide_cd_put(info);

	return rc;
}

static int idecd_release(struct inode * inode, struct file * file)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct cdrom_info *info = ide_cd_g(disk);
	ide_drive_t *drive = info->drive;

	cdrom_release (&info->devinfo, file);
	drive->usage--;

	ide_cd_put(info);

	return 0;
}

static int idecd_set_spindown(struct cdrom_device_info *cdi, unsigned long arg)
{
	struct packet_command cgc;
	char buffer[16];
	int stat;
	char spindown;

	if (copy_from_user(&spindown, (void __user *)arg, sizeof(char)))
		return -EFAULT;

	init_cdrom_command(&cgc, buffer, sizeof(buffer), CGC_DATA_UNKNOWN);

	stat = cdrom_mode_sense(cdi, &cgc, GPMODE_CDROM_PAGE, 0);
	if (stat)
		return stat;

	buffer[11] = (buffer[11] & 0xf0) | (spindown & 0x0f);
	return cdrom_mode_select(cdi, &cgc);
}

static int idecd_get_spindown(struct cdrom_device_info *cdi, unsigned long arg)
{
	struct packet_command cgc;
	char buffer[16];
	int stat;
 	char spindown;

	init_cdrom_command(&cgc, buffer, sizeof(buffer), CGC_DATA_UNKNOWN);

	stat = cdrom_mode_sense(cdi, &cgc, GPMODE_CDROM_PAGE, 0);
	if (stat)
		return stat;

	spindown = buffer[11] & 0x0f;
	if (copy_to_user((void __user *)arg, &spindown, sizeof (char)))
		return -EFAULT;
	return 0;
}

static int idecd_ioctl (struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct block_device *bdev = inode->i_bdev;
	struct cdrom_info *info = ide_cd_g(bdev->bd_disk);
	int err;

	switch (cmd) {
 	case CDROMSETSPINDOWN:
		return idecd_set_spindown(&info->devinfo, arg);
 	case CDROMGETSPINDOWN:
		return idecd_get_spindown(&info->devinfo, arg);
	default:
		break;
 	}

	err = generic_ide_ioctl(info->drive, file, bdev, cmd, arg);
	if (err == -EINVAL)
		err = cdrom_ioctl(file, &info->devinfo, inode, cmd, arg);

	return err;
}

static int idecd_media_changed(struct gendisk *disk)
{
	struct cdrom_info *info = ide_cd_g(disk);
	return cdrom_media_changed(&info->devinfo);
}

static int idecd_revalidate_disk(struct gendisk *disk)
{
	struct cdrom_info *info = ide_cd_g(disk);
	struct request_sense sense;
	cdrom_read_toc(info->drive, &sense);
	return  0;
}

static struct block_device_operations idecd_ops = {
	.owner		= THIS_MODULE,
	.open		= idecd_open,
	.release	= idecd_release,
	.ioctl		= idecd_ioctl,
	.media_changed	= idecd_media_changed,
	.revalidate_disk= idecd_revalidate_disk
};

/* options */
static char *ignore = NULL;

module_param(ignore, charp, 0400);
MODULE_DESCRIPTION("ATAPI CD-ROM Driver");

static int ide_cd_probe(ide_drive_t *drive)
{
	struct cdrom_info *info;
	struct gendisk *g;
	struct request_sense sense;

	if (!strstr("ide-cdrom", drive->driver_req))
		goto failed;
	if (!drive->present)
		goto failed;
	if (drive->media != ide_cdrom && drive->media != ide_optical)
		goto failed;
	/* skip drives that we were told to ignore */
	if (ignore != NULL) {
		if (strstr(ignore, drive->name)) {
			printk(KERN_INFO "ide-cd: ignoring drive %s\n", drive->name);
			goto failed;
		}
	}
	if (drive->scsi) {
		printk(KERN_INFO "ide-cd: passing drive %s to ide-scsi emulation.\n", drive->name);
		goto failed;
	}
	info = kzalloc(sizeof(struct cdrom_info), GFP_KERNEL);
	if (info == NULL) {
		printk(KERN_ERR "%s: Can't allocate a cdrom structure\n", drive->name);
		goto failed;
	}

	g = alloc_disk(1 << PARTN_BITS);
	if (!g)
		goto out_free_cd;

	ide_init_disk(g, drive);

	ide_register_subdriver(drive, &ide_cdrom_driver);

	kref_init(&info->kref);

	info->drive = drive;
	info->driver = &ide_cdrom_driver;
	info->disk = g;

	g->private_data = &info->driver;

	drive->driver_data = info;

	g->minors = 1;
	g->driverfs_dev = &drive->gendev;
	g->flags = GENHD_FL_CD | GENHD_FL_REMOVABLE;
	if (ide_cdrom_setup(drive)) {
		struct cdrom_device_info *devinfo = &info->devinfo;
		ide_unregister_subdriver(drive, &ide_cdrom_driver);
		kfree(info->buffer);
		kfree(info->toc);
		kfree(info->changer_info);
		if (devinfo->handle == drive && unregister_cdrom(devinfo))
			printk (KERN_ERR "%s: ide_cdrom_cleanup failed to unregister device from the cdrom driver.\n", drive->name);
		kfree(info);
		drive->driver_data = NULL;
		goto failed;
	}

	cdrom_read_toc(drive, &sense);
	g->fops = &idecd_ops;
	g->flags |= GENHD_FL_REMOVABLE;
	add_disk(g);
	return 0;

out_free_cd:
	kfree(info);
failed:
	return -ENODEV;
}

static void __exit ide_cdrom_exit(void)
{
	driver_unregister(&ide_cdrom_driver.gen_driver);
}

static int __init ide_cdrom_init(void)
{
	return driver_register(&ide_cdrom_driver.gen_driver);
}

MODULE_ALIAS("ide:*m-cdrom*");
module_init(ide_cdrom_init);
module_exit(ide_cdrom_exit);
MODULE_LICENSE("GPL");
