/* linux/drivers/cdrom/cdrom.c
   Copyright (c) 1996, 1997 David A. van Leeuwen.
   Copyright (c) 1997, 1998 Erik Andersen <andersee@debian.org>
   Copyright (c) 1998, 1999 Jens Axboe <axboe@image.dk>

   May be copied or modified under the terms of the GNU General Public
   License.  See linux/COPYING for more information.

   Uniform CD-ROM driver for Linux.
   See Documentation/cdrom/cdrom-standard.tex for usage information.

   The routines in the file provide a uniform interface between the
   software that uses CD-ROMs and the various low-level drivers that
   actually talk to the hardware. Suggestions are welcome.
   Patches that work are more welcome though.  ;-)

 To Do List:
 ----------------------------------

 -- Modify sysctl/proc interface. I plan on having one directory per
 drive, with entries for outputing general drive information, and sysctl
 based tunable parameters such as whether the tray should auto-close for
 that drive. Suggestions (or patches) for this welcome!


 Revision History
 ----------------------------------
 1.00  Date Unknown -- David van Leeuwen <david@tm.tno.nl>
 -- Initial version by David A. van Leeuwen. I don't have a detailed
  changelog for the 1.x series, David?

2.00  Dec  2, 1997 -- Erik Andersen <andersee@debian.org>
  -- New maintainer! As David A. van Leeuwen has been too busy to activly
  maintain and improve this driver, I am now carrying on the torch. If
  you have a problem with this driver, please feel free to contact me.

  -- Added (rudimentary) sysctl interface. I realize this is really weak
  right now, and is _very_ badly implemented. It will be improved...

  -- Modified CDROM_DISC_STATUS so that it is now incorporated into
  the Uniform CD-ROM driver via the cdrom_count_tracks function.
  The cdrom_count_tracks function helps resolve some of the false
  assumptions of the CDROM_DISC_STATUS ioctl, and is also used to check
  for the correct media type when mounting or playing audio from a CD.

  -- Remove the calls to verify_area and only use the copy_from_user and
  copy_to_user stuff, since these calls now provide their own memory
  checking with the 2.1.x kernels.

  -- Major update to return codes so that errors from low-level drivers
  are passed on through (thanks to Gerd Knorr for pointing out this
  problem).

  -- Made it so if a function isn't implemented in a low-level driver,
  ENOSYS is now returned instead of EINVAL.

  -- Simplified some complex logic so that the source code is easier to read.

  -- Other stuff I probably forgot to mention (lots of changes).

2.01 to 2.11 Dec 1997-Jan 1998
  -- TO-DO!  Write changelogs for 2.01 to 2.12.

2.12  Jan  24, 1998 -- Erik Andersen <andersee@debian.org>
  -- Fixed a bug in the IOCTL_IN and IOCTL_OUT macros.  It turns out that
  copy_*_user does not return EFAULT on error, but instead returns the number 
  of bytes not copied.  I was returning whatever non-zero stuff came back from 
  the copy_*_user functions directly, which would result in strange errors.

2.13  July 17, 1998 -- Erik Andersen <andersee@debian.org>
  -- Fixed a bug in CDROM_SELECT_SPEED where you couldn't lower the speed
  of the drive.  Thanks to Tobias Ringstr|m <tori@prosolvia.se> for pointing
  this out and providing a simple fix.
  -- Fixed the procfs-unload-module bug with the fill_inode procfs callback.
  thanks to Andrea Arcangeli
  -- Fixed it so that the /proc entry now also shows up when cdrom is
  compiled into the kernel.  Before it only worked when loaded as a module.

  2.14 August 17, 1998 -- Erik Andersen <andersee@debian.org>
  -- Fixed a bug in cdrom_media_changed and handling of reporting that
  the media had changed for devices that _don't_ implement media_changed.  
  Thanks to Grant R. Guenther <grant@torque.net> for spotting this bug.
  -- Made a few things more pedanticly correct.

2.50 Oct 19, 1998 - Jens Axboe <axboe@image.dk>
  -- New maintainers! Erik was too busy to continue the work on the driver,
  so now Chris Zwilling <chris@cloudnet.com> and Jens Axboe <axboe@image.dk>
  will do their best to follow in his footsteps
  
  2.51 Dec 20, 1998 - Jens Axboe <axboe@image.dk>
  -- Check if drive is capable of doing what we ask before blindly changing
  cdi->options in various ioctl.
  -- Added version to proc entry.
  
  2.52 Jan 16, 1999 - Jens Axboe <axboe@image.dk>
  -- Fixed an error in open_for_data where we would sometimes not return
  the correct error value. Thanks Huba Gaspar <huba@softcell.hu>.
  -- Fixed module usage count - usage was based on /proc/sys/dev
  instead of /proc/sys/dev/cdrom. This could lead to an oops when other
  modules had entries in dev. Feb 02 - real bug was in sysctl.c where
  dev would be removed even though it was used. cdrom.c just illuminated
  that bug.
  
  2.53 Feb 22, 1999 - Jens Axboe <axboe@image.dk>
  -- Fixup of several ioctl calls, in particular CDROM_SET_OPTIONS has
  been "rewritten" because capabilities and options aren't in sync. They
  should be...
  -- Added CDROM_LOCKDOOR ioctl. Locks the door and keeps it that way.
  -- Added CDROM_RESET ioctl.
  -- Added CDROM_DEBUG ioctl. Enable debug messages on-the-fly.
  -- Added CDROM_GET_CAPABILITY ioctl. This relieves userspace programs
  from parsing /proc/sys/dev/cdrom/info.
  
  2.54 Mar 15, 1999 - Jens Axboe <axboe@image.dk>
  -- Check capability mask from low level driver when counting tracks as
  per suggestion from Corey J. Scotts <cstotts@blue.weeg.uiowa.edu>.
  
  2.55 Apr 25, 1999 - Jens Axboe <axboe@image.dk>
  -- autoclose was mistakenly checked against CDC_OPEN_TRAY instead of
  CDC_CLOSE_TRAY.
  -- proc info didn't mask against capabilities mask.
  
  3.00 Aug 5, 1999 - Jens Axboe <axboe@image.dk>
  -- Unified audio ioctl handling across CD-ROM drivers. A lot of the
  code was duplicated before. Drives that support the generic packet
  interface are now being fed packets from here instead.
  -- First attempt at adding support for MMC2 commands - for DVD and
  CD-R(W) drives. Only the DVD parts are in now - the interface used is
  the same as for the audio ioctls.
  -- ioctl cleanups. if a drive couldn't play audio, it didn't get
  a change to perform device specific ioctls as well.
  -- Defined CDROM_CAN(CDC_XXX) for checking the capabilities.
  -- Put in sysctl files for autoclose, autoeject, check_media, debug,
  and lock.
  -- /proc/sys/dev/cdrom/info has been updated to also contain info about
  CD-Rx and DVD capabilities.
  -- Now default to checking media type.
  -- CDROM_SEND_PACKET ioctl added. The infrastructure was in place for
  doing this anyway, with the generic_packet addition.
  
  3.01 Aug 6, 1999 - Jens Axboe <axboe@image.dk>
  -- Fix up the sysctl handling so that the option flags get set
  correctly.
  -- Fix up ioctl handling so the device specific ones actually get
  called :).
  
  3.02 Aug 8, 1999 - Jens Axboe <axboe@image.dk>
  -- Fixed volume control on SCSI drives (or others with longer audio
  page).
  -- Fixed a couple of DVD minors. Thanks to Andrew T. Veliath
  <andrewtv@usa.net> for telling me and for having defined the various
  DVD structures and ioctls in the first place! He designed the original
  DVD patches for ide-cd and while I rearranged and unified them, the
  interface is still the same.
  
  3.03 Sep 1, 1999 - Jens Axboe <axboe@image.dk>
  -- Moved the rest of the audio ioctls from the CD-ROM drivers here. Only
  CDROMREADTOCENTRY and CDROMREADTOCHDR are left.
  -- Moved the CDROMREADxxx ioctls in here.
  -- Defined the cdrom_get_last_written and cdrom_get_next_block as ioctls
  and exported functions.
  -- Erik Andersen <andersen@xmission.com> modified all SCMD_ commands
  to now read GPCMD_ for the new generic packet interface. All low level
  drivers are updated as well.
  -- Various other cleanups.

  3.04 Sep 12, 1999 - Jens Axboe <axboe@image.dk>
  -- Fixed a couple of possible memory leaks (if an operation failed and
  we didn't free the buffer before returning the error).
  -- Integrated Uniform CD Changer handling from Richard Sharman
  <rsharman@pobox.com>.
  -- Defined CD_DVD and CD_CHANGER log levels.
  -- Fixed the CDROMREADxxx ioctls.
  -- CDROMPLAYTRKIND uses the GPCMD_PLAY_AUDIO_MSF command - too few
  drives supported it. We lose the index part, however.
  -- Small modifications to accommodate opens of /dev/hdc1, required
  for ide-cd to handle multisession discs.
  -- Export cdrom_mode_sense and cdrom_mode_select.
  -- init_cdrom_command() for setting up a cgc command.
  
  3.05 Oct 24, 1999 - Jens Axboe <axboe@image.dk>
  -- Changed the interface for CDROM_SEND_PACKET. Before it was virtually
  impossible to send the drive data in a sensible way.
  -- Lowered stack usage in mmc_ioctl(), dvd_read_disckey(), and
  dvd_read_manufact.
  -- Added setup of write mode for packet writing.
  -- Fixed CDDA ripping with cdda2wav - accept much larger requests of
  number of frames and split the reads in blocks of 8.

  3.06 Dec 13, 1999 - Jens Axboe <axboe@image.dk>
  -- Added support for changing the region of DVD drives.
  -- Added sense data to generic command.

  3.07 Feb 2, 2000 - Jens Axboe <axboe@suse.de>
  -- Do same "read header length" trick in cdrom_get_disc_info() as
  we do in cdrom_get_track_info() -- some drive don't obey specs and
  fail if they can't supply the full Mt Fuji size table.
  -- Deleted stuff related to setting up write modes. It has a different
  home now.
  -- Clear header length in mode_select unconditionally.
  -- Removed the register_disk() that was added, not needed here.

  3.08 May 1, 2000 - Jens Axboe <axboe@suse.de>
  -- Fix direction flag in setup_send_key and setup_report_key. This
  gave some SCSI adapters problems.
  -- Always return -EROFS for write opens
  -- Convert to module_init/module_exit style init and remove some
  of the #ifdef MODULE stuff
  -- Fix several dvd errors - DVD_LU_SEND_ASF should pass agid,
  DVD_HOST_SEND_RPC_STATE did not set buffer size in cdb, and
  dvd_do_auth passed uninitialized data to drive because init_cdrom_command
  did not clear a 0 sized buffer.
  
  3.09 May 12, 2000 - Jens Axboe <axboe@suse.de>
  -- Fix Video-CD on SCSI drives that don't support READ_CD command. In
  that case switch block size and issue plain READ_10 again, then switch
  back.

  3.10 Jun 10, 2000 - Jens Axboe <axboe@suse.de>
  -- Fix volume control on CD's - old SCSI-II drives now use their own
  code, as doing MODE6 stuff in here is really not my intention.
  -- Use READ_DISC_INFO for more reliable end-of-disc.

  3.11 Jun 12, 2000 - Jens Axboe <axboe@suse.de>
  -- Fix bug in getting rpc phase 2 region info.
  -- Reinstate "correct" CDROMPLAYTRKIND

   3.12 Oct 18, 2000 - Jens Axboe <axboe@suse.de>
  -- Use quiet bit on packet commands not known to work

   3.20 Dec 17, 2003 - Jens Axboe <axboe@suse.de>
  -- Various fixes and lots of cleanups not listed :-)
  -- Locking fixes
  -- Mt Rainier support
  -- DVD-RAM write open fixes

  Nov 5 2001, Aug 8 2002. Modified by Andy Polyakov
  <appro@fy.chalmers.se> to support MMC-3 compliant DVD+RW units.

  Modified by Nigel Kukard <nkukard@lbsd.net> - support DVD+RW
  2.4.x patch by Andy Polyakov <appro@fy.chalmers.se>

-------------------------------------------------------------------------*/

#define REVISION "Revision: 3.20"
#define VERSION "Id: cdrom.c 3.20 2003/12/17"

/* I use an error-log mask to give fine grain control over the type of
   messages dumped to the system logs.  The available masks include: */
#define CD_NOTHING      0x0
#define CD_WARNING	0x1
#define CD_REG_UNREG	0x2
#define CD_DO_IOCTL	0x4
#define CD_OPEN		0x8
#define CD_CLOSE	0x10
#define CD_COUNT_TRACKS 0x20
#define CD_CHANGER	0x40
#define CD_DVD		0x80

/* Define this to remove _all_ the debugging messages */
/* #define ERRLOGMASK CD_NOTHING */
#define ERRLOGMASK CD_WARNING
/* #define ERRLOGMASK (CD_WARNING|CD_OPEN|CD_COUNT_TRACKS|CD_CLOSE) */
/* #define ERRLOGMASK (CD_WARNING|CD_REG_UNREG|CD_DO_IOCTL|CD_OPEN|CD_CLOSE|CD_COUNT_TRACKS) */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/major.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h> 
#include <linux/cdrom.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/blkpg.h>
#include <linux/init.h>
#include <linux/fcntl.h>
#include <linux/blkdev.h>
#include <linux/times.h>

#include <asm/uaccess.h>

/* used to tell the module to turn on full debugging messages */
static int debug;
/* used to keep tray locked at all times */
static int keeplocked;
/* default compatibility mode */
static int autoclose=1;
static int autoeject;
static int lockdoor = 1;
/* will we ever get to use this... sigh. */
static int check_media_type;
/* automatically restart mrw format */
static int mrw_format_restart = 1;
module_param(debug, bool, 0);
module_param(autoclose, bool, 0);
module_param(autoeject, bool, 0);
module_param(lockdoor, bool, 0);
module_param(check_media_type, bool, 0);
module_param(mrw_format_restart, bool, 0);

static DEFINE_MUTEX(cdrom_mutex);

static const char *mrw_format_status[] = {
	"not mrw",
	"bgformat inactive",
	"bgformat active",
	"mrw complete",
};

static const char *mrw_address_space[] = { "DMA", "GAA" };

#if (ERRLOGMASK!=CD_NOTHING)
#define cdinfo(type, fmt, args...) \
        if ((ERRLOGMASK & type) || debug==1 ) \
            printk(KERN_INFO "cdrom: " fmt, ## args)
#else
#define cdinfo(type, fmt, args...) 
#endif

/* These are used to simplify getting data in from and back to user land */
#define IOCTL_IN(arg, type, in)					\
	if (copy_from_user(&(in), (type __user *) (arg), sizeof (in)))	\
		return -EFAULT;

#define IOCTL_OUT(arg, type, out) \
	if (copy_to_user((type __user *) (arg), &(out), sizeof (out)))	\
		return -EFAULT;

/* The (cdo->capability & ~cdi->mask & CDC_XXX) construct was used in
   a lot of places. This macro makes the code more clear. */
#define CDROM_CAN(type) (cdi->ops->capability & ~cdi->mask & (type))

/* used in the audio ioctls */
#define CHECKAUDIO if ((ret=check_for_audio_disc(cdi, cdo))) return ret

/*
 * Another popular OS uses 7 seconds as the hard timeout for default
 * commands, so it is a good choice for us as well.
 */
#define CDROM_DEF_TIMEOUT	(7 * HZ)

/* Not-exported routines. */
static int open_for_data(struct cdrom_device_info * cdi);
static int check_for_audio_disc(struct cdrom_device_info * cdi,
			 struct cdrom_device_ops * cdo);
static void sanitize_format(union cdrom_addr *addr, 
		u_char * curr, u_char requested);
static int mmc_ioctl(struct cdrom_device_info *cdi, unsigned int cmd,
		     unsigned long arg);

int cdrom_get_last_written(struct cdrom_device_info *, long *);
static int cdrom_get_next_writable(struct cdrom_device_info *, long *);
static void cdrom_count_tracks(struct cdrom_device_info *, tracktype*);

static int cdrom_mrw_exit(struct cdrom_device_info *cdi);

static int cdrom_get_disc_info(struct cdrom_device_info *cdi, disc_information *di);

static void cdrom_sysctl_register(void);

static LIST_HEAD(cdrom_list);

static int cdrom_dummy_generic_packet(struct cdrom_device_info *cdi,
				      struct packet_command *cgc)
{
	if (cgc->sense) {
		cgc->sense->sense_key = 0x05;
		cgc->sense->asc = 0x20;
		cgc->sense->ascq = 0x00;
	}

	cgc->stat = -EIO;
	return -EIO;
}

/* This macro makes sure we don't have to check on cdrom_device_ops
 * existence in the run-time routines below. Change_capability is a
 * hack to have the capability flags defined const, while we can still
 * change it here without gcc complaining at every line.
 */
#define ENSURE(call, bits) if (cdo->call == NULL) *change_capability &= ~(bits)

int register_cdrom(struct cdrom_device_info *cdi)
{
	static char banner_printed;
        struct cdrom_device_ops *cdo = cdi->ops;
        int *change_capability = (int *)&cdo->capability; /* hack */

	cdinfo(CD_OPEN, "entering register_cdrom\n"); 

	if (cdo->open == NULL || cdo->release == NULL)
		return -EINVAL;
	if (!banner_printed) {
		printk(KERN_INFO "Uniform CD-ROM driver " REVISION "\n");
		banner_printed = 1;
		cdrom_sysctl_register();
	}

	ENSURE(drive_status, CDC_DRIVE_STATUS );
	ENSURE(media_changed, CDC_MEDIA_CHANGED);
	ENSURE(tray_move, CDC_CLOSE_TRAY | CDC_OPEN_TRAY);
	ENSURE(lock_door, CDC_LOCK);
	ENSURE(select_speed, CDC_SELECT_SPEED);
	ENSURE(get_last_session, CDC_MULTI_SESSION);
	ENSURE(get_mcn, CDC_MCN);
	ENSURE(reset, CDC_RESET);
	ENSURE(generic_packet, CDC_GENERIC_PACKET);
	cdi->mc_flags = 0;
	cdo->n_minors = 0;
        cdi->options = CDO_USE_FFLAGS;
	
	if (autoclose==1 && CDROM_CAN(CDC_CLOSE_TRAY))
		cdi->options |= (int) CDO_AUTO_CLOSE;
	if (autoeject==1 && CDROM_CAN(CDC_OPEN_TRAY))
		cdi->options |= (int) CDO_AUTO_EJECT;
	if (lockdoor==1)
		cdi->options |= (int) CDO_LOCK;
	if (check_media_type==1)
		cdi->options |= (int) CDO_CHECK_TYPE;

	if (CDROM_CAN(CDC_MRW_W))
		cdi->exit = cdrom_mrw_exit;

	if (cdi->disk)
		cdi->cdda_method = CDDA_BPC_FULL;
	else
		cdi->cdda_method = CDDA_OLD;

	if (!cdo->generic_packet)
		cdo->generic_packet = cdrom_dummy_generic_packet;

	cdinfo(CD_REG_UNREG, "drive \"/dev/%s\" registered\n", cdi->name);
	mutex_lock(&cdrom_mutex);
	list_add(&cdi->list, &cdrom_list);
	mutex_unlock(&cdrom_mutex);
	return 0;
}
#undef ENSURE

void unregister_cdrom(struct cdrom_device_info *cdi)
{
	cdinfo(CD_OPEN, "entering unregister_cdrom\n"); 

	mutex_lock(&cdrom_mutex);
	list_del(&cdi->list);
	mutex_unlock(&cdrom_mutex);

	if (cdi->exit)
		cdi->exit(cdi);

	cdi->ops->n_minors--;
	cdinfo(CD_REG_UNREG, "drive \"/dev/%s\" unregistered\n", cdi->name);
}

int cdrom_get_media_event(struct cdrom_device_info *cdi,
			  struct media_event_desc *med)
{
	struct packet_command cgc;
	unsigned char buffer[8];
	struct event_header *eh = (struct event_header *) buffer;

	init_cdrom_command(&cgc, buffer, sizeof(buffer), CGC_DATA_READ);
	cgc.cmd[0] = GPCMD_GET_EVENT_STATUS_NOTIFICATION;
	cgc.cmd[1] = 1;		/* IMMED */
	cgc.cmd[4] = 1 << 4;	/* media event */
	cgc.cmd[8] = sizeof(buffer);
	cgc.quiet = 1;

	if (cdi->ops->generic_packet(cdi, &cgc))
		return 1;

	if (be16_to_cpu(eh->data_len) < sizeof(*med))
		return 1;

	if (eh->nea || eh->notification_class != 0x4)
		return 1;

	memcpy(med, &buffer[sizeof(*eh)], sizeof(*med));
	return 0;
}

/*
 * the first prototypes used 0x2c as the page code for the mrw mode page,
 * subsequently this was changed to 0x03. probe the one used by this drive
 */
static int cdrom_mrw_probe_pc(struct cdrom_device_info *cdi)
{
	struct packet_command cgc;
	char buffer[16];

	init_cdrom_command(&cgc, buffer, sizeof(buffer), CGC_DATA_READ);

	cgc.timeout = HZ;
	cgc.quiet = 1;

	if (!cdrom_mode_sense(cdi, &cgc, MRW_MODE_PC, 0)) {
		cdi->mrw_mode_page = MRW_MODE_PC;
		return 0;
	} else if (!cdrom_mode_sense(cdi, &cgc, MRW_MODE_PC_PRE1, 0)) {
		cdi->mrw_mode_page = MRW_MODE_PC_PRE1;
		return 0;
	}

	return 1;
}

static int cdrom_is_mrw(struct cdrom_device_info *cdi, int *write)
{
	struct packet_command cgc;
	struct mrw_feature_desc *mfd;
	unsigned char buffer[16];
	int ret;

	*write = 0;

	init_cdrom_command(&cgc, buffer, sizeof(buffer), CGC_DATA_READ);

	cgc.cmd[0] = GPCMD_GET_CONFIGURATION;
	cgc.cmd[3] = CDF_MRW;
	cgc.cmd[8] = sizeof(buffer);
	cgc.quiet = 1;

	if ((ret = cdi->ops->generic_packet(cdi, &cgc)))
		return ret;

	mfd = (struct mrw_feature_desc *)&buffer[sizeof(struct feature_header)];
	if (be16_to_cpu(mfd->feature_code) != CDF_MRW)
		return 1;
	*write = mfd->write;

	if ((ret = cdrom_mrw_probe_pc(cdi))) {
		*write = 0;
		return ret;
	}

	return 0;
}

static int cdrom_mrw_bgformat(struct cdrom_device_info *cdi, int cont)
{
	struct packet_command cgc;
	unsigned char buffer[12];
	int ret;

	printk(KERN_INFO "cdrom: %sstarting format\n", cont ? "Re" : "");

	/*
	 * FmtData bit set (bit 4), format type is 1
	 */
	init_cdrom_command(&cgc, buffer, sizeof(buffer), CGC_DATA_WRITE);
	cgc.cmd[0] = GPCMD_FORMAT_UNIT;
	cgc.cmd[1] = (1 << 4) | 1;

	cgc.timeout = 5 * 60 * HZ;

	/*
	 * 4 byte format list header, 8 byte format list descriptor
	 */
	buffer[1] = 1 << 1;
	buffer[3] = 8;

	/*
	 * nr_blocks field
	 */
	buffer[4] = 0xff;
	buffer[5] = 0xff;
	buffer[6] = 0xff;
	buffer[7] = 0xff;

	buffer[8] = 0x24 << 2;
	buffer[11] = cont;

	ret = cdi->ops->generic_packet(cdi, &cgc);
	if (ret)
		printk(KERN_INFO "cdrom: bgformat failed\n");

	return ret;
}

static int cdrom_mrw_bgformat_susp(struct cdrom_device_info *cdi, int immed)
{
	struct packet_command cgc;

	init_cdrom_command(&cgc, NULL, 0, CGC_DATA_NONE);
	cgc.cmd[0] = GPCMD_CLOSE_TRACK;

	/*
	 * Session = 1, Track = 0
	 */
	cgc.cmd[1] = !!immed;
	cgc.cmd[2] = 1 << 1;

	cgc.timeout = 5 * 60 * HZ;

	return cdi->ops->generic_packet(cdi, &cgc);
}

static int cdrom_flush_cache(struct cdrom_device_info *cdi)
{
	struct packet_command cgc;

	init_cdrom_command(&cgc, NULL, 0, CGC_DATA_NONE);
	cgc.cmd[0] = GPCMD_FLUSH_CACHE;

	cgc.timeout = 5 * 60 * HZ;

	return cdi->ops->generic_packet(cdi, &cgc);
}

static int cdrom_mrw_exit(struct cdrom_device_info *cdi)
{
	disc_information di;
	int ret;

	ret = cdrom_get_disc_info(cdi, &di);
	if (ret < 0 || ret < (int)offsetof(typeof(di),disc_type))
		return 1;

	ret = 0;
	if (di.mrw_status == CDM_MRW_BGFORMAT_ACTIVE) {
		printk(KERN_INFO "cdrom: issuing MRW back ground "
				"format suspend\n");
		ret = cdrom_mrw_bgformat_susp(cdi, 0);
	}

	if (!ret && cdi->media_written)
		ret = cdrom_flush_cache(cdi);

	return ret;
}

static int cdrom_mrw_set_lba_space(struct cdrom_device_info *cdi, int space)
{
	struct packet_command cgc;
	struct mode_page_header *mph;
	char buffer[16];
	int ret, offset, size;

	init_cdrom_command(&cgc, buffer, sizeof(buffer), CGC_DATA_READ);

	cgc.buffer = buffer;
	cgc.buflen = sizeof(buffer);

	if ((ret = cdrom_mode_sense(cdi, &cgc, cdi->mrw_mode_page, 0)))
		return ret;

	mph = (struct mode_page_header *) buffer;
	offset = be16_to_cpu(mph->desc_length);
	size = be16_to_cpu(mph->mode_data_length) + 2;

	buffer[offset + 3] = space;
	cgc.buflen = size;

	if ((ret = cdrom_mode_select(cdi, &cgc)))
		return ret;

	printk(KERN_INFO "cdrom: %s: mrw address space %s selected\n", cdi->name, mrw_address_space[space]);
	return 0;
}

static int cdrom_get_random_writable(struct cdrom_device_info *cdi,
			      struct rwrt_feature_desc *rfd)
{
	struct packet_command cgc;
	char buffer[24];
	int ret;

	init_cdrom_command(&cgc, buffer, sizeof(buffer), CGC_DATA_READ);

	cgc.cmd[0] = GPCMD_GET_CONFIGURATION;	/* often 0x46 */
	cgc.cmd[3] = CDF_RWRT;			/* often 0x0020 */
	cgc.cmd[8] = sizeof(buffer);		/* often 0x18 */
	cgc.quiet = 1;

	if ((ret = cdi->ops->generic_packet(cdi, &cgc)))
		return ret;

	memcpy(rfd, &buffer[sizeof(struct feature_header)], sizeof (*rfd));
	return 0;
}

static int cdrom_has_defect_mgt(struct cdrom_device_info *cdi)
{
	struct packet_command cgc;
	char buffer[16];
	__be16 *feature_code;
	int ret;

	init_cdrom_command(&cgc, buffer, sizeof(buffer), CGC_DATA_READ);

	cgc.cmd[0] = GPCMD_GET_CONFIGURATION;
	cgc.cmd[3] = CDF_HWDM;
	cgc.cmd[8] = sizeof(buffer);
	cgc.quiet = 1;

	if ((ret = cdi->ops->generic_packet(cdi, &cgc)))
		return ret;

	feature_code = (__be16 *) &buffer[sizeof(struct feature_header)];
	if (be16_to_cpu(*feature_code) == CDF_HWDM)
		return 0;

	return 1;
}


static int cdrom_is_random_writable(struct cdrom_device_info *cdi, int *write)
{
	struct rwrt_feature_desc rfd;
	int ret;

	*write = 0;

	if ((ret = cdrom_get_random_writable(cdi, &rfd)))
		return ret;

	if (CDF_RWRT == be16_to_cpu(rfd.feature_code))
		*write = 1;

	return 0;
}

static int cdrom_media_erasable(struct cdrom_device_info *cdi)
{
	disc_information di;
	int ret;

	ret = cdrom_get_disc_info(cdi, &di);
	if (ret < 0 || ret < offsetof(typeof(di), n_first_track))
		return -1;

	return di.erasable;
}

/*
 * FIXME: check RO bit
 */
static int cdrom_dvdram_open_write(struct cdrom_device_info *cdi)
{
	int ret = cdrom_media_erasable(cdi);

	/*
	 * allow writable open if media info read worked and media is
	 * erasable, _or_ if it fails since not all drives support it
	 */
	if (!ret)
		return 1;

	return 0;
}

static int cdrom_mrw_open_write(struct cdrom_device_info *cdi)
{
	disc_information di;
	int ret;

	/*
	 * always reset to DMA lba space on open
	 */
	if (cdrom_mrw_set_lba_space(cdi, MRW_LBA_DMA)) {
		printk(KERN_ERR "cdrom: failed setting lba address space\n");
		return 1;
	}

	ret = cdrom_get_disc_info(cdi, &di);
	if (ret < 0 || ret < offsetof(typeof(di),disc_type))
		return 1;

	if (!di.erasable)
		return 1;

	/*
	 * mrw_status
	 * 0	-	not MRW formatted
	 * 1	-	MRW bgformat started, but not running or complete
	 * 2	-	MRW bgformat in progress
	 * 3	-	MRW formatting complete
	 */
	ret = 0;
	printk(KERN_INFO "cdrom open: mrw_status '%s'\n",
			mrw_format_status[di.mrw_status]);
	if (!di.mrw_status)
		ret = 1;
	else if (di.mrw_status == CDM_MRW_BGFORMAT_INACTIVE &&
			mrw_format_restart)
		ret = cdrom_mrw_bgformat(cdi, 1);

	return ret;
}

static int mo_open_write(struct cdrom_device_info *cdi)
{
	struct packet_command cgc;
	char buffer[255];
	int ret;

	init_cdrom_command(&cgc, &buffer, 4, CGC_DATA_READ);
	cgc.quiet = 1;

	/*
	 * obtain write protect information as per
	 * drivers/scsi/sd.c:sd_read_write_protect_flag
	 */

	ret = cdrom_mode_sense(cdi, &cgc, GPMODE_ALL_PAGES, 0);
	if (ret)
		ret = cdrom_mode_sense(cdi, &cgc, GPMODE_VENDOR_PAGE, 0);
	if (ret) {
		cgc.buflen = 255;
		ret = cdrom_mode_sense(cdi, &cgc, GPMODE_ALL_PAGES, 0);
	}

	/* drive gave us no info, let the user go ahead */
	if (ret)
		return 0;

	return buffer[3] & 0x80;
}

static int cdrom_ram_open_write(struct cdrom_device_info *cdi)
{
	struct rwrt_feature_desc rfd;
	int ret;

	if ((ret = cdrom_has_defect_mgt(cdi)))
		return ret;

	if ((ret = cdrom_get_random_writable(cdi, &rfd)))
		return ret;
	else if (CDF_RWRT == be16_to_cpu(rfd.feature_code))
		ret = !rfd.curr;

	cdinfo(CD_OPEN, "can open for random write\n");
	return ret;
}

static void cdrom_mmc3_profile(struct cdrom_device_info *cdi)
{
	struct packet_command cgc;
	char buffer[32];
	int ret, mmc3_profile;

	init_cdrom_command(&cgc, buffer, sizeof(buffer), CGC_DATA_READ);

	cgc.cmd[0] = GPCMD_GET_CONFIGURATION;
	cgc.cmd[1] = 0;
	cgc.cmd[2] = cgc.cmd[3] = 0;		/* Starting Feature Number */
	cgc.cmd[8] = sizeof(buffer);		/* Allocation Length */
	cgc.quiet = 1;

	if ((ret = cdi->ops->generic_packet(cdi, &cgc)))
		mmc3_profile = 0xffff;
	else
		mmc3_profile = (buffer[6] << 8) | buffer[7];

	cdi->mmc3_profile = mmc3_profile;
}

static int cdrom_is_dvd_rw(struct cdrom_device_info *cdi)
{
	switch (cdi->mmc3_profile) {
	case 0x12:	/* DVD-RAM	*/
	case 0x1A:	/* DVD+RW	*/
		return 0;
	default:
		return 1;
	}
}

/*
 * returns 0 for ok to open write, non-0 to disallow
 */
static int cdrom_open_write(struct cdrom_device_info *cdi)
{
	int mrw, mrw_write, ram_write;
	int ret = 1;

	mrw = 0;
	if (!cdrom_is_mrw(cdi, &mrw_write))
		mrw = 1;

	if (CDROM_CAN(CDC_MO_DRIVE))
		ram_write = 1;
	else
		(void) cdrom_is_random_writable(cdi, &ram_write);
	
	if (mrw)
		cdi->mask &= ~CDC_MRW;
	else
		cdi->mask |= CDC_MRW;

	if (mrw_write)
		cdi->mask &= ~CDC_MRW_W;
	else
		cdi->mask |= CDC_MRW_W;

	if (ram_write)
		cdi->mask &= ~CDC_RAM;
	else
		cdi->mask |= CDC_RAM;

	if (CDROM_CAN(CDC_MRW_W))
		ret = cdrom_mrw_open_write(cdi);
	else if (CDROM_CAN(CDC_DVD_RAM))
		ret = cdrom_dvdram_open_write(cdi);
 	else if (CDROM_CAN(CDC_RAM) &&
 		 !CDROM_CAN(CDC_CD_R|CDC_CD_RW|CDC_DVD|CDC_DVD_R|CDC_MRW|CDC_MO_DRIVE))
 		ret = cdrom_ram_open_write(cdi);
	else if (CDROM_CAN(CDC_MO_DRIVE))
		ret = mo_open_write(cdi);
	else if (!cdrom_is_dvd_rw(cdi))
		ret = 0;

	return ret;
}

static void cdrom_dvd_rw_close_write(struct cdrom_device_info *cdi)
{
	struct packet_command cgc;

	if (cdi->mmc3_profile != 0x1a) {
		cdinfo(CD_CLOSE, "%s: No DVD+RW\n", cdi->name);
		return;
	}

	if (!cdi->media_written) {
		cdinfo(CD_CLOSE, "%s: DVD+RW media clean\n", cdi->name);
		return;
	}

	printk(KERN_INFO "cdrom: %s: dirty DVD+RW media, \"finalizing\"\n",
	       cdi->name);

	init_cdrom_command(&cgc, NULL, 0, CGC_DATA_NONE);
	cgc.cmd[0] = GPCMD_FLUSH_CACHE;
	cgc.timeout = 30*HZ;
	cdi->ops->generic_packet(cdi, &cgc);

	init_cdrom_command(&cgc, NULL, 0, CGC_DATA_NONE);
	cgc.cmd[0] = GPCMD_CLOSE_TRACK;
	cgc.timeout = 3000*HZ;
	cgc.quiet = 1;
	cdi->ops->generic_packet(cdi, &cgc);

	init_cdrom_command(&cgc, NULL, 0, CGC_DATA_NONE);
	cgc.cmd[0] = GPCMD_CLOSE_TRACK;
	cgc.cmd[2] = 2;	 /* Close session */
	cgc.quiet = 1;
	cgc.timeout = 3000*HZ;
	cdi->ops->generic_packet(cdi, &cgc);

	cdi->media_written = 0;
}

static int cdrom_close_write(struct cdrom_device_info *cdi)
{
#if 0
	return cdrom_flush_cache(cdi);
#else
	return 0;
#endif
}

/* We use the open-option O_NONBLOCK to indicate that the
 * purpose of opening is only for subsequent ioctl() calls; no device
 * integrity checks are performed.
 *
 * We hope that all cd-player programs will adopt this convention. It
 * is in their own interest: device control becomes a lot easier
 * this way.
 */
int cdrom_open(struct cdrom_device_info *cdi, struct inode *ip, struct file *fp)
{
	int ret;

	cdinfo(CD_OPEN, "entering cdrom_open\n"); 

	/* if this was a O_NONBLOCK open and we should honor the flags,
	 * do a quick open without drive/disc integrity checks. */
	cdi->use_count++;
	if ((fp->f_flags & O_NONBLOCK) && (cdi->options & CDO_USE_FFLAGS)) {
		ret = cdi->ops->open(cdi, 1);
	} else {
		ret = open_for_data(cdi);
		if (ret)
			goto err;
		cdrom_mmc3_profile(cdi);
		if (fp->f_mode & FMODE_WRITE) {
			ret = -EROFS;
			if (cdrom_open_write(cdi))
				goto err_release;
			if (!CDROM_CAN(CDC_RAM))
				goto err_release;
			ret = 0;
			cdi->media_written = 0;
		}
	}

	if (ret)
		goto err;

	cdinfo(CD_OPEN, "Use count for \"/dev/%s\" now %d\n",
			cdi->name, cdi->use_count);
	/* Do this on open.  Don't wait for mount, because they might
	    not be mounting, but opening with O_NONBLOCK */
	check_disk_change(ip->i_bdev);
	return 0;
err_release:
	if (CDROM_CAN(CDC_LOCK) && cdi->options & CDO_LOCK) {
		cdi->ops->lock_door(cdi, 0);
		cdinfo(CD_OPEN, "door unlocked.\n");
	}
	cdi->ops->release(cdi);
err:
	cdi->use_count--;
	return ret;
}

static
int open_for_data(struct cdrom_device_info * cdi)
{
	int ret;
	struct cdrom_device_ops *cdo = cdi->ops;
	tracktype tracks;
	cdinfo(CD_OPEN, "entering open_for_data\n");
	/* Check if the driver can report drive status.  If it can, we
	   can do clever things.  If it can't, well, we at least tried! */
	if (cdo->drive_status != NULL) {
		ret = cdo->drive_status(cdi, CDSL_CURRENT);
		cdinfo(CD_OPEN, "drive_status=%d\n", ret); 
		if (ret == CDS_TRAY_OPEN) {
			cdinfo(CD_OPEN, "the tray is open...\n"); 
			/* can/may i close it? */
			if (CDROM_CAN(CDC_CLOSE_TRAY) &&
			    cdi->options & CDO_AUTO_CLOSE) {
				cdinfo(CD_OPEN, "trying to close the tray.\n"); 
				ret=cdo->tray_move(cdi,0);
				if (ret) {
					cdinfo(CD_OPEN, "bummer. tried to close the tray but failed.\n"); 
					/* Ignore the error from the low
					level driver.  We don't care why it
					couldn't close the tray.  We only care 
					that there is no disc in the drive, 
					since that is the _REAL_ problem here.*/
					ret=-ENOMEDIUM;
					goto clean_up_and_return;
				}
			} else {
				cdinfo(CD_OPEN, "bummer. this drive can't close the tray.\n"); 
				ret=-ENOMEDIUM;
				goto clean_up_and_return;
			}
			/* Ok, the door should be closed now.. Check again */
			ret = cdo->drive_status(cdi, CDSL_CURRENT);
			if ((ret == CDS_NO_DISC) || (ret==CDS_TRAY_OPEN)) {
				cdinfo(CD_OPEN, "bummer. the tray is still not closed.\n"); 
				cdinfo(CD_OPEN, "tray might not contain a medium.\n");
				ret=-ENOMEDIUM;
				goto clean_up_and_return;
			}
			cdinfo(CD_OPEN, "the tray is now closed.\n"); 
		}
		/* the door should be closed now, check for the disc */
		ret = cdo->drive_status(cdi, CDSL_CURRENT);
		if (ret!=CDS_DISC_OK) {
			ret = -ENOMEDIUM;
			goto clean_up_and_return;
		}
	}
	cdrom_count_tracks(cdi, &tracks);
	if (tracks.error == CDS_NO_DISC) {
		cdinfo(CD_OPEN, "bummer. no disc.\n");
		ret=-ENOMEDIUM;
		goto clean_up_and_return;
	}
	/* CD-Players which don't use O_NONBLOCK, workman
	 * for example, need bit CDO_CHECK_TYPE cleared! */
	if (tracks.data==0) {
		if (cdi->options & CDO_CHECK_TYPE) {
		    /* give people a warning shot, now that CDO_CHECK_TYPE
		       is the default case! */
		    cdinfo(CD_OPEN, "bummer. wrong media type.\n"); 
		    cdinfo(CD_WARNING, "pid %d must open device O_NONBLOCK!\n",
					(unsigned int)task_pid_nr(current));
		    ret=-EMEDIUMTYPE;
		    goto clean_up_and_return;
		}
		else {
		    cdinfo(CD_OPEN, "wrong media type, but CDO_CHECK_TYPE not set.\n");
		}
	}

	cdinfo(CD_OPEN, "all seems well, opening the device.\n"); 

	/* all seems well, we can open the device */
	ret = cdo->open(cdi, 0); /* open for data */
	cdinfo(CD_OPEN, "opening the device gave me %d.\n", ret); 
	/* After all this careful checking, we shouldn't have problems
	   opening the device, but we don't want the device locked if 
	   this somehow fails... */
	if (ret) {
		cdinfo(CD_OPEN, "open device failed.\n"); 
		goto clean_up_and_return;
	}
	if (CDROM_CAN(CDC_LOCK) && (cdi->options & CDO_LOCK)) {
			cdo->lock_door(cdi, 1);
			cdinfo(CD_OPEN, "door locked.\n");
	}
	cdinfo(CD_OPEN, "device opened successfully.\n"); 
	return ret;

	/* Something failed.  Try to unlock the drive, because some drivers
	(notably ide-cd) lock the drive after every command.  This produced
	a nasty bug where after mount failed, the drive would remain locked!  
	This ensures that the drive gets unlocked after a mount fails.  This 
	is a goto to avoid bloating the driver with redundant code. */ 
clean_up_and_return:
	cdinfo(CD_OPEN, "open failed.\n"); 
	if (CDROM_CAN(CDC_LOCK) && cdi->options & CDO_LOCK) {
			cdo->lock_door(cdi, 0);
			cdinfo(CD_OPEN, "door unlocked.\n");
	}
	return ret;
}

/* This code is similar to that in open_for_data. The routine is called
   whenever an audio play operation is requested.
*/
static int check_for_audio_disc(struct cdrom_device_info * cdi,
				struct cdrom_device_ops * cdo)
{
        int ret;
	tracktype tracks;
	cdinfo(CD_OPEN, "entering check_for_audio_disc\n");
	if (!(cdi->options & CDO_CHECK_TYPE))
		return 0;
	if (cdo->drive_status != NULL) {
		ret = cdo->drive_status(cdi, CDSL_CURRENT);
		cdinfo(CD_OPEN, "drive_status=%d\n", ret); 
		if (ret == CDS_TRAY_OPEN) {
			cdinfo(CD_OPEN, "the tray is open...\n"); 
			/* can/may i close it? */
			if (CDROM_CAN(CDC_CLOSE_TRAY) &&
			    cdi->options & CDO_AUTO_CLOSE) {
				cdinfo(CD_OPEN, "trying to close the tray.\n"); 
				ret=cdo->tray_move(cdi,0);
				if (ret) {
					cdinfo(CD_OPEN, "bummer. tried to close tray but failed.\n"); 
					/* Ignore the error from the low
					level driver.  We don't care why it
					couldn't close the tray.  We only care 
					that there is no disc in the drive, 
					since that is the _REAL_ problem here.*/
					return -ENOMEDIUM;
				}
			} else {
				cdinfo(CD_OPEN, "bummer. this driver can't close the tray.\n"); 
				return -ENOMEDIUM;
			}
			/* Ok, the door should be closed now.. Check again */
			ret = cdo->drive_status(cdi, CDSL_CURRENT);
			if ((ret == CDS_NO_DISC) || (ret==CDS_TRAY_OPEN)) {
				cdinfo(CD_OPEN, "bummer. the tray is still not closed.\n"); 
				return -ENOMEDIUM;
			}	
			if (ret!=CDS_DISC_OK) {
				cdinfo(CD_OPEN, "bummer. disc isn't ready.\n"); 
				return -EIO;
			}	
			cdinfo(CD_OPEN, "the tray is now closed.\n"); 
		}	
	}
	cdrom_count_tracks(cdi, &tracks);
	if (tracks.error) 
		return(tracks.error);

	if (tracks.audio==0)
		return -EMEDIUMTYPE;

	return 0;
}

int cdrom_release(struct cdrom_device_info *cdi, struct file *fp)
{
	struct cdrom_device_ops *cdo = cdi->ops;
	int opened_for_data;

	cdinfo(CD_CLOSE, "entering cdrom_release\n");

	if (cdi->use_count > 0)
		cdi->use_count--;

	if (cdi->use_count == 0) {
		cdinfo(CD_CLOSE, "Use count for \"/dev/%s\" now zero\n", cdi->name);
		cdrom_dvd_rw_close_write(cdi);

		if ((cdo->capability & CDC_LOCK) && !keeplocked) {
			cdinfo(CD_CLOSE, "Unlocking door!\n");
			cdo->lock_door(cdi, 0);
		}
	}

	opened_for_data = !(cdi->options & CDO_USE_FFLAGS) ||
		!(fp && fp->f_flags & O_NONBLOCK);

	/*
	 * flush cache on last write release
	 */
	if (CDROM_CAN(CDC_RAM) && !cdi->use_count && cdi->for_data)
		cdrom_close_write(cdi);

	cdo->release(cdi);
	if (cdi->use_count == 0) {      /* last process that closes dev*/
		if (opened_for_data &&
		    cdi->options & CDO_AUTO_EJECT && CDROM_CAN(CDC_OPEN_TRAY))
			cdo->tray_move(cdi, 1);
	}
	return 0;
}

static int cdrom_read_mech_status(struct cdrom_device_info *cdi, 
				  struct cdrom_changer_info *buf)
{
	struct packet_command cgc;
	struct cdrom_device_ops *cdo = cdi->ops;
	int length;

	/*
	 * Sanyo changer isn't spec compliant (doesn't use regular change
	 * LOAD_UNLOAD command, and it doesn't implement the mech status
	 * command below
	 */
	if (cdi->sanyo_slot) {
		buf->hdr.nslots = 3;
		buf->hdr.curslot = cdi->sanyo_slot == 3 ? 0 : cdi->sanyo_slot;
		for (length = 0; length < 3; length++) {
			buf->slots[length].disc_present = 1;
			buf->slots[length].change = 0;
		}
		return 0;
	}

	length = sizeof(struct cdrom_mechstat_header) +
		 cdi->capacity * sizeof(struct cdrom_slot);

	init_cdrom_command(&cgc, buf, length, CGC_DATA_READ);
	cgc.cmd[0] = GPCMD_MECHANISM_STATUS;
	cgc.cmd[8] = (length >> 8) & 0xff;
	cgc.cmd[9] = length & 0xff;
	return cdo->generic_packet(cdi, &cgc);
}

static int cdrom_slot_status(struct cdrom_device_info *cdi, int slot)
{
	struct cdrom_changer_info *info;
	int ret;

	cdinfo(CD_CHANGER, "entering cdrom_slot_status()\n"); 
	if (cdi->sanyo_slot)
		return CDS_NO_INFO;
	
	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	if ((ret = cdrom_read_mech_status(cdi, info)))
		goto out_free;

	if (info->slots[slot].disc_present)
		ret = CDS_DISC_OK;
	else
		ret = CDS_NO_DISC;

out_free:
	kfree(info);
	return ret;
}

/* Return the number of slots for an ATAPI/SCSI cdrom, 
 * return 1 if not a changer. 
 */
int cdrom_number_of_slots(struct cdrom_device_info *cdi) 
{
	int status;
	int nslots = 1;
	struct cdrom_changer_info *info;

	cdinfo(CD_CHANGER, "entering cdrom_number_of_slots()\n"); 
	/* cdrom_read_mech_status requires a valid value for capacity: */
	cdi->capacity = 0; 

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	if ((status = cdrom_read_mech_status(cdi, info)) == 0)
		nslots = info->hdr.nslots;

	kfree(info);
	return nslots;
}


/* If SLOT < 0, unload the current slot.  Otherwise, try to load SLOT. */
static int cdrom_load_unload(struct cdrom_device_info *cdi, int slot) 
{
	struct packet_command cgc;

	cdinfo(CD_CHANGER, "entering cdrom_load_unload()\n"); 
	if (cdi->sanyo_slot && slot < 0)
		return 0;

	init_cdrom_command(&cgc, NULL, 0, CGC_DATA_NONE);
	cgc.cmd[0] = GPCMD_LOAD_UNLOAD;
	cgc.cmd[4] = 2 + (slot >= 0);
	cgc.cmd[8] = slot;
	cgc.timeout = 60 * HZ;

	/* The Sanyo 3 CD changer uses byte 7 of the 
	GPCMD_TEST_UNIT_READY to command to switch CDs instead of
	using the GPCMD_LOAD_UNLOAD opcode. */
	if (cdi->sanyo_slot && -1 < slot) {
		cgc.cmd[0] = GPCMD_TEST_UNIT_READY;
		cgc.cmd[7] = slot;
		cgc.cmd[4] = cgc.cmd[8] = 0;
		cdi->sanyo_slot = slot ? slot : 3;
	}

	return cdi->ops->generic_packet(cdi, &cgc);
}

static int cdrom_select_disc(struct cdrom_device_info *cdi, int slot)
{
	struct cdrom_changer_info *info;
	int curslot;
	int ret;

	cdinfo(CD_CHANGER, "entering cdrom_select_disc()\n"); 
	if (!CDROM_CAN(CDC_SELECT_DISC))
		return -EDRIVE_CANT_DO_THIS;

	(void) cdi->ops->media_changed(cdi, slot);

	if (slot == CDSL_NONE) {
		/* set media changed bits, on both queues */
		cdi->mc_flags = 0x3;
		return cdrom_load_unload(cdi, -1);
	}

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	if ((ret = cdrom_read_mech_status(cdi, info))) {
		kfree(info);
		return ret;
	}

	curslot = info->hdr.curslot;
	kfree(info);

	if (cdi->use_count > 1 || keeplocked) {
		if (slot == CDSL_CURRENT) {
	    		return curslot;
		} else {
			return -EBUSY;
		}
	}

	/* Specifying CDSL_CURRENT will attempt to load the currnet slot,
	which is useful if it had been previously unloaded.
	Whether it can or not, it returns the current slot. 
	Similarly,  if slot happens to be the current one, we still
	try and load it. */
	if (slot == CDSL_CURRENT)
		slot = curslot;

	/* set media changed bits on both queues */
	cdi->mc_flags = 0x3;
	if ((ret = cdrom_load_unload(cdi, slot)))
		return ret;

	return slot;
}

/* We want to make media_changed accessible to the user through an
 * ioctl. The main problem now is that we must double-buffer the
 * low-level implementation, to assure that the VFS and the user both
 * see a medium change once.
 */

static
int media_changed(struct cdrom_device_info *cdi, int queue)
{
	unsigned int mask = (1 << (queue & 1));
	int ret = !!(cdi->mc_flags & mask);

	if (!CDROM_CAN(CDC_MEDIA_CHANGED))
	    return ret;
	/* changed since last call? */
	if (cdi->ops->media_changed(cdi, CDSL_CURRENT)) {
		cdi->mc_flags = 0x3;    /* set bit on both queues */
		ret |= 1;
		cdi->media_written = 0;
	}
	cdi->mc_flags &= ~mask;         /* clear bit */
	return ret;
}

int cdrom_media_changed(struct cdrom_device_info *cdi)
{
	/* This talks to the VFS, which doesn't like errors - just 1 or 0.  
	 * Returning "0" is always safe (media hasn't been changed). Do that 
	 * if the low-level cdrom driver dosn't support media changed. */ 
	if (cdi == NULL || cdi->ops->media_changed == NULL)
		return 0;
	if (!CDROM_CAN(CDC_MEDIA_CHANGED))
		return 0;
	return media_changed(cdi, 0);
}

/* badly broken, I know. Is due for a fixup anytime. */
static void cdrom_count_tracks(struct cdrom_device_info *cdi, tracktype* tracks)
{
	struct cdrom_tochdr header;
	struct cdrom_tocentry entry;
	int ret, i;
	tracks->data=0;
	tracks->audio=0;
	tracks->cdi=0;
	tracks->xa=0;
	tracks->error=0;
	cdinfo(CD_COUNT_TRACKS, "entering cdrom_count_tracks\n"); 
	/* Grab the TOC header so we can see how many tracks there are */
	if ((ret = cdi->ops->audio_ioctl(cdi, CDROMREADTOCHDR, &header))) {
		if (ret == -ENOMEDIUM)
			tracks->error = CDS_NO_DISC;
		else
			tracks->error = CDS_NO_INFO;
		return;
	}	
	/* check what type of tracks are on this disc */
	entry.cdte_format = CDROM_MSF;
	for (i = header.cdth_trk0; i <= header.cdth_trk1; i++) {
		entry.cdte_track  = i;
		if (cdi->ops->audio_ioctl(cdi, CDROMREADTOCENTRY, &entry)) {
			tracks->error=CDS_NO_INFO;
			return;
		}	
		if (entry.cdte_ctrl & CDROM_DATA_TRACK) {
		    if (entry.cdte_format == 0x10)
			tracks->cdi++;
		    else if (entry.cdte_format == 0x20) 
			tracks->xa++;
		    else
			tracks->data++;
		} else
		    tracks->audio++;
		cdinfo(CD_COUNT_TRACKS, "track %d: format=%d, ctrl=%d\n",
		       i, entry.cdte_format, entry.cdte_ctrl);
	}	
	cdinfo(CD_COUNT_TRACKS, "disc has %d tracks: %d=audio %d=data %d=Cd-I %d=XA\n", 
		header.cdth_trk1, tracks->audio, tracks->data, 
		tracks->cdi, tracks->xa);
}	

/* Requests to the low-level drivers will /always/ be done in the
   following format convention:

   CDROM_LBA: all data-related requests.
   CDROM_MSF: all audio-related requests.

   However, a low-level implementation is allowed to refuse this
   request, and return information in its own favorite format.

   It doesn't make sense /at all/ to ask for a play_audio in LBA
   format, or ask for multi-session info in MSF format. However, for
   backward compatibility these format requests will be satisfied, but
   the requests to the low-level drivers will be sanitized in the more
   meaningful format indicated above.
 */

static
void sanitize_format(union cdrom_addr *addr,
		     u_char * curr, u_char requested)
{
	if (*curr == requested)
		return;                 /* nothing to be done! */
	if (requested == CDROM_LBA) {
		addr->lba = (int) addr->msf.frame +
			75 * (addr->msf.second - 2 + 60 * addr->msf.minute);
	} else {                        /* CDROM_MSF */
		int lba = addr->lba;
		addr->msf.frame = lba % 75;
		lba /= 75;
		lba += 2;
		addr->msf.second = lba % 60;
		addr->msf.minute = lba / 60;
	}
	*curr = requested;
}

void init_cdrom_command(struct packet_command *cgc, void *buf, int len,
			int type)
{
	memset(cgc, 0, sizeof(struct packet_command));
	if (buf)
		memset(buf, 0, len);
	cgc->buffer = (char *) buf;
	cgc->buflen = len;
	cgc->data_direction = type;
	cgc->timeout = CDROM_DEF_TIMEOUT;
}

/* DVD handling */

#define copy_key(dest,src)	memcpy((dest), (src), sizeof(dvd_key))
#define copy_chal(dest,src)	memcpy((dest), (src), sizeof(dvd_challenge))

static void setup_report_key(struct packet_command *cgc, unsigned agid, unsigned type)
{
	cgc->cmd[0] = GPCMD_REPORT_KEY;
	cgc->cmd[10] = type | (agid << 6);
	switch (type) {
		case 0: case 8: case 5: {
			cgc->buflen = 8;
			break;
		}
		case 1: {
			cgc->buflen = 16;
			break;
		}
		case 2: case 4: {
			cgc->buflen = 12;
			break;
		}
	}
	cgc->cmd[9] = cgc->buflen;
	cgc->data_direction = CGC_DATA_READ;
}

static void setup_send_key(struct packet_command *cgc, unsigned agid, unsigned type)
{
	cgc->cmd[0] = GPCMD_SEND_KEY;
	cgc->cmd[10] = type | (agid << 6);
	switch (type) {
		case 1: {
			cgc->buflen = 16;
			break;
		}
		case 3: {
			cgc->buflen = 12;
			break;
		}
		case 6: {
			cgc->buflen = 8;
			break;
		}
	}
	cgc->cmd[9] = cgc->buflen;
	cgc->data_direction = CGC_DATA_WRITE;
}

static int dvd_do_auth(struct cdrom_device_info *cdi, dvd_authinfo *ai)
{
	int ret;
	u_char buf[20];
	struct packet_command cgc;
	struct cdrom_device_ops *cdo = cdi->ops;
	rpc_state_t rpc_state;

	memset(buf, 0, sizeof(buf));
	init_cdrom_command(&cgc, buf, 0, CGC_DATA_READ);

	switch (ai->type) {
	/* LU data send */
	case DVD_LU_SEND_AGID:
		cdinfo(CD_DVD, "entering DVD_LU_SEND_AGID\n"); 
		cgc.quiet = 1;
		setup_report_key(&cgc, ai->lsa.agid, 0);

		if ((ret = cdo->generic_packet(cdi, &cgc)))
			return ret;

		ai->lsa.agid = buf[7] >> 6;
		/* Returning data, let host change state */
		break;

	case DVD_LU_SEND_KEY1:
		cdinfo(CD_DVD, "entering DVD_LU_SEND_KEY1\n"); 
		setup_report_key(&cgc, ai->lsk.agid, 2);

		if ((ret = cdo->generic_packet(cdi, &cgc)))
			return ret;

		copy_key(ai->lsk.key, &buf[4]);
		/* Returning data, let host change state */
		break;

	case DVD_LU_SEND_CHALLENGE:
		cdinfo(CD_DVD, "entering DVD_LU_SEND_CHALLENGE\n"); 
		setup_report_key(&cgc, ai->lsc.agid, 1);

		if ((ret = cdo->generic_packet(cdi, &cgc)))
			return ret;

		copy_chal(ai->lsc.chal, &buf[4]);
		/* Returning data, let host change state */
		break;

	/* Post-auth key */
	case DVD_LU_SEND_TITLE_KEY:
		cdinfo(CD_DVD, "entering DVD_LU_SEND_TITLE_KEY\n"); 
		cgc.quiet = 1;
		setup_report_key(&cgc, ai->lstk.agid, 4);
		cgc.cmd[5] = ai->lstk.lba;
		cgc.cmd[4] = ai->lstk.lba >> 8;
		cgc.cmd[3] = ai->lstk.lba >> 16;
		cgc.cmd[2] = ai->lstk.lba >> 24;

		if ((ret = cdo->generic_packet(cdi, &cgc)))
			return ret;

		ai->lstk.cpm = (buf[4] >> 7) & 1;
		ai->lstk.cp_sec = (buf[4] >> 6) & 1;
		ai->lstk.cgms = (buf[4] >> 4) & 3;
		copy_key(ai->lstk.title_key, &buf[5]);
		/* Returning data, let host change state */
		break;

	case DVD_LU_SEND_ASF:
		cdinfo(CD_DVD, "entering DVD_LU_SEND_ASF\n"); 
		setup_report_key(&cgc, ai->lsasf.agid, 5);
		
		if ((ret = cdo->generic_packet(cdi, &cgc)))
			return ret;

		ai->lsasf.asf = buf[7] & 1;
		break;

	/* LU data receive (LU changes state) */
	case DVD_HOST_SEND_CHALLENGE:
		cdinfo(CD_DVD, "entering DVD_HOST_SEND_CHALLENGE\n"); 
		setup_send_key(&cgc, ai->hsc.agid, 1);
		buf[1] = 0xe;
		copy_chal(&buf[4], ai->hsc.chal);

		if ((ret = cdo->generic_packet(cdi, &cgc)))
			return ret;

		ai->type = DVD_LU_SEND_KEY1;
		break;

	case DVD_HOST_SEND_KEY2:
		cdinfo(CD_DVD, "entering DVD_HOST_SEND_KEY2\n"); 
		setup_send_key(&cgc, ai->hsk.agid, 3);
		buf[1] = 0xa;
		copy_key(&buf[4], ai->hsk.key);

		if ((ret = cdo->generic_packet(cdi, &cgc))) {
			ai->type = DVD_AUTH_FAILURE;
			return ret;
		}
		ai->type = DVD_AUTH_ESTABLISHED;
		break;

	/* Misc */
	case DVD_INVALIDATE_AGID:
		cgc.quiet = 1;
		cdinfo(CD_DVD, "entering DVD_INVALIDATE_AGID\n"); 
		setup_report_key(&cgc, ai->lsa.agid, 0x3f);
		if ((ret = cdo->generic_packet(cdi, &cgc)))
			return ret;
		break;

	/* Get region settings */
	case DVD_LU_SEND_RPC_STATE:
		cdinfo(CD_DVD, "entering DVD_LU_SEND_RPC_STATE\n");
		setup_report_key(&cgc, 0, 8);
		memset(&rpc_state, 0, sizeof(rpc_state_t));
		cgc.buffer = (char *) &rpc_state;

		if ((ret = cdo->generic_packet(cdi, &cgc)))
			return ret;

		ai->lrpcs.type = rpc_state.type_code;
		ai->lrpcs.vra = rpc_state.vra;
		ai->lrpcs.ucca = rpc_state.ucca;
		ai->lrpcs.region_mask = rpc_state.region_mask;
		ai->lrpcs.rpc_scheme = rpc_state.rpc_scheme;
		break;

	/* Set region settings */
	case DVD_HOST_SEND_RPC_STATE:
		cdinfo(CD_DVD, "entering DVD_HOST_SEND_RPC_STATE\n");
		setup_send_key(&cgc, 0, 6);
		buf[1] = 6;
		buf[4] = ai->hrpcs.pdrc;

		if ((ret = cdo->generic_packet(cdi, &cgc)))
			return ret;
		break;

	default:
		cdinfo(CD_WARNING, "Invalid DVD key ioctl (%d)\n", ai->type);
		return -ENOTTY;
	}

	return 0;
}

static int dvd_read_physical(struct cdrom_device_info *cdi, dvd_struct *s)
{
	unsigned char buf[21], *base;
	struct dvd_layer *layer;
	struct packet_command cgc;
	struct cdrom_device_ops *cdo = cdi->ops;
	int ret, layer_num = s->physical.layer_num;

	if (layer_num >= DVD_LAYERS)
		return -EINVAL;

	init_cdrom_command(&cgc, buf, sizeof(buf), CGC_DATA_READ);
	cgc.cmd[0] = GPCMD_READ_DVD_STRUCTURE;
	cgc.cmd[6] = layer_num;
	cgc.cmd[7] = s->type;
	cgc.cmd[9] = cgc.buflen & 0xff;

	/*
	 * refrain from reporting errors on non-existing layers (mainly)
	 */
	cgc.quiet = 1;

	if ((ret = cdo->generic_packet(cdi, &cgc)))
		return ret;

	base = &buf[4];
	layer = &s->physical.layer[layer_num];

	/*
	 * place the data... really ugly, but at least we won't have to
	 * worry about endianess in userspace.
	 */
	memset(layer, 0, sizeof(*layer));
	layer->book_version = base[0] & 0xf;
	layer->book_type = base[0] >> 4;
	layer->min_rate = base[1] & 0xf;
	layer->disc_size = base[1] >> 4;
	layer->layer_type = base[2] & 0xf;
	layer->track_path = (base[2] >> 4) & 1;
	layer->nlayers = (base[2] >> 5) & 3;
	layer->track_density = base[3] & 0xf;
	layer->linear_density = base[3] >> 4;
	layer->start_sector = base[5] << 16 | base[6] << 8 | base[7];
	layer->end_sector = base[9] << 16 | base[10] << 8 | base[11];
	layer->end_sector_l0 = base[13] << 16 | base[14] << 8 | base[15];
	layer->bca = base[16] >> 7;

	return 0;
}

static int dvd_read_copyright(struct cdrom_device_info *cdi, dvd_struct *s)
{
	int ret;
	u_char buf[8];
	struct packet_command cgc;
	struct cdrom_device_ops *cdo = cdi->ops;

	init_cdrom_command(&cgc, buf, sizeof(buf), CGC_DATA_READ);
	cgc.cmd[0] = GPCMD_READ_DVD_STRUCTURE;
	cgc.cmd[6] = s->copyright.layer_num;
	cgc.cmd[7] = s->type;
	cgc.cmd[8] = cgc.buflen >> 8;
	cgc.cmd[9] = cgc.buflen & 0xff;

	if ((ret = cdo->generic_packet(cdi, &cgc)))
		return ret;

	s->copyright.cpst = buf[4];
	s->copyright.rmi = buf[5];

	return 0;
}

static int dvd_read_disckey(struct cdrom_device_info *cdi, dvd_struct *s)
{
	int ret, size;
	u_char *buf;
	struct packet_command cgc;
	struct cdrom_device_ops *cdo = cdi->ops;

	size = sizeof(s->disckey.value) + 4;

	if ((buf = kmalloc(size, GFP_KERNEL)) == NULL)
		return -ENOMEM;

	init_cdrom_command(&cgc, buf, size, CGC_DATA_READ);
	cgc.cmd[0] = GPCMD_READ_DVD_STRUCTURE;
	cgc.cmd[7] = s->type;
	cgc.cmd[8] = size >> 8;
	cgc.cmd[9] = size & 0xff;
	cgc.cmd[10] = s->disckey.agid << 6;

	if (!(ret = cdo->generic_packet(cdi, &cgc)))
		memcpy(s->disckey.value, &buf[4], sizeof(s->disckey.value));

	kfree(buf);
	return ret;
}

static int dvd_read_bca(struct cdrom_device_info *cdi, dvd_struct *s)
{
	int ret;
	u_char buf[4 + 188];
	struct packet_command cgc;
	struct cdrom_device_ops *cdo = cdi->ops;

	init_cdrom_command(&cgc, buf, sizeof(buf), CGC_DATA_READ);
	cgc.cmd[0] = GPCMD_READ_DVD_STRUCTURE;
	cgc.cmd[7] = s->type;
	cgc.cmd[9] = cgc.buflen & 0xff;

	if ((ret = cdo->generic_packet(cdi, &cgc)))
		return ret;

	s->bca.len = buf[0] << 8 | buf[1];
	if (s->bca.len < 12 || s->bca.len > 188) {
		cdinfo(CD_WARNING, "Received invalid BCA length (%d)\n", s->bca.len);
		return -EIO;
	}
	memcpy(s->bca.value, &buf[4], s->bca.len);

	return 0;
}

static int dvd_read_manufact(struct cdrom_device_info *cdi, dvd_struct *s)
{
	int ret = 0, size;
	u_char *buf;
	struct packet_command cgc;
	struct cdrom_device_ops *cdo = cdi->ops;

	size = sizeof(s->manufact.value) + 4;

	if ((buf = kmalloc(size, GFP_KERNEL)) == NULL)
		return -ENOMEM;

	init_cdrom_command(&cgc, buf, size, CGC_DATA_READ);
	cgc.cmd[0] = GPCMD_READ_DVD_STRUCTURE;
	cgc.cmd[7] = s->type;
	cgc.cmd[8] = size >> 8;
	cgc.cmd[9] = size & 0xff;

	if ((ret = cdo->generic_packet(cdi, &cgc))) {
		kfree(buf);
		return ret;
	}

	s->manufact.len = buf[0] << 8 | buf[1];
	if (s->manufact.len < 0 || s->manufact.len > 2048) {
		cdinfo(CD_WARNING, "Received invalid manufacture info length"
				   " (%d)\n", s->manufact.len);
		ret = -EIO;
	} else {
		memcpy(s->manufact.value, &buf[4], s->manufact.len);
	}

	kfree(buf);
	return ret;
}

static int dvd_read_struct(struct cdrom_device_info *cdi, dvd_struct *s)
{
	switch (s->type) {
	case DVD_STRUCT_PHYSICAL:
		return dvd_read_physical(cdi, s);

	case DVD_STRUCT_COPYRIGHT:
		return dvd_read_copyright(cdi, s);

	case DVD_STRUCT_DISCKEY:
		return dvd_read_disckey(cdi, s);

	case DVD_STRUCT_BCA:
		return dvd_read_bca(cdi, s);

	case DVD_STRUCT_MANUFACT:
		return dvd_read_manufact(cdi, s);
		
	default:
		cdinfo(CD_WARNING, ": Invalid DVD structure read requested (%d)\n",
					s->type);
		return -EINVAL;
	}
}

int cdrom_mode_sense(struct cdrom_device_info *cdi,
		     struct packet_command *cgc,
		     int page_code, int page_control)
{
	struct cdrom_device_ops *cdo = cdi->ops;

	memset(cgc->cmd, 0, sizeof(cgc->cmd));

	cgc->cmd[0] = GPCMD_MODE_SENSE_10;
	cgc->cmd[2] = page_code | (page_control << 6);
	cgc->cmd[7] = cgc->buflen >> 8;
	cgc->cmd[8] = cgc->buflen & 0xff;
	cgc->data_direction = CGC_DATA_READ;
	return cdo->generic_packet(cdi, cgc);
}

int cdrom_mode_select(struct cdrom_device_info *cdi,
		      struct packet_command *cgc)
{
	struct cdrom_device_ops *cdo = cdi->ops;

	memset(cgc->cmd, 0, sizeof(cgc->cmd));
	memset(cgc->buffer, 0, 2);
	cgc->cmd[0] = GPCMD_MODE_SELECT_10;
	cgc->cmd[1] = 0x10;		/* PF */
	cgc->cmd[7] = cgc->buflen >> 8;
	cgc->cmd[8] = cgc->buflen & 0xff;
	cgc->data_direction = CGC_DATA_WRITE;
	return cdo->generic_packet(cdi, cgc);
}

static int cdrom_read_subchannel(struct cdrom_device_info *cdi,
				 struct cdrom_subchnl *subchnl, int mcn)
{
	struct cdrom_device_ops *cdo = cdi->ops;
	struct packet_command cgc;
	char buffer[32];
	int ret;

	init_cdrom_command(&cgc, buffer, 16, CGC_DATA_READ);
	cgc.cmd[0] = GPCMD_READ_SUBCHANNEL;
	cgc.cmd[1] = 2;     /* MSF addressing */
	cgc.cmd[2] = 0x40;  /* request subQ data */
	cgc.cmd[3] = mcn ? 2 : 1;
	cgc.cmd[8] = 16;

	if ((ret = cdo->generic_packet(cdi, &cgc)))
		return ret;

	subchnl->cdsc_audiostatus = cgc.buffer[1];
	subchnl->cdsc_format = CDROM_MSF;
	subchnl->cdsc_ctrl = cgc.buffer[5] & 0xf;
	subchnl->cdsc_trk = cgc.buffer[6];
	subchnl->cdsc_ind = cgc.buffer[7];

	subchnl->cdsc_reladdr.msf.minute = cgc.buffer[13];
	subchnl->cdsc_reladdr.msf.second = cgc.buffer[14];
	subchnl->cdsc_reladdr.msf.frame = cgc.buffer[15];
	subchnl->cdsc_absaddr.msf.minute = cgc.buffer[9];
	subchnl->cdsc_absaddr.msf.second = cgc.buffer[10];
	subchnl->cdsc_absaddr.msf.frame = cgc.buffer[11];

	return 0;
}

/*
 * Specific READ_10 interface
 */
static int cdrom_read_cd(struct cdrom_device_info *cdi,
			 struct packet_command *cgc, int lba,
			 int blocksize, int nblocks)
{
	struct cdrom_device_ops *cdo = cdi->ops;

	memset(&cgc->cmd, 0, sizeof(cgc->cmd));
	cgc->cmd[0] = GPCMD_READ_10;
	cgc->cmd[2] = (lba >> 24) & 0xff;
	cgc->cmd[3] = (lba >> 16) & 0xff;
	cgc->cmd[4] = (lba >>  8) & 0xff;
	cgc->cmd[5] = lba & 0xff;
	cgc->cmd[6] = (nblocks >> 16) & 0xff;
	cgc->cmd[7] = (nblocks >>  8) & 0xff;
	cgc->cmd[8] = nblocks & 0xff;
	cgc->buflen = blocksize * nblocks;
	return cdo->generic_packet(cdi, cgc);
}

/* very generic interface for reading the various types of blocks */
static int cdrom_read_block(struct cdrom_device_info *cdi,
			    struct packet_command *cgc,
			    int lba, int nblocks, int format, int blksize)
{
	struct cdrom_device_ops *cdo = cdi->ops;

	memset(&cgc->cmd, 0, sizeof(cgc->cmd));
	cgc->cmd[0] = GPCMD_READ_CD;
	/* expected sector size - cdda,mode1,etc. */
	cgc->cmd[1] = format << 2;
	/* starting address */
	cgc->cmd[2] = (lba >> 24) & 0xff;
	cgc->cmd[3] = (lba >> 16) & 0xff;
	cgc->cmd[4] = (lba >>  8) & 0xff;
	cgc->cmd[5] = lba & 0xff;
	/* number of blocks */
	cgc->cmd[6] = (nblocks >> 16) & 0xff;
	cgc->cmd[7] = (nblocks >>  8) & 0xff;
	cgc->cmd[8] = nblocks & 0xff;
	cgc->buflen = blksize * nblocks;
	
	/* set the header info returned */
	switch (blksize) {
	case CD_FRAMESIZE_RAW0	: cgc->cmd[9] = 0x58; break;
	case CD_FRAMESIZE_RAW1	: cgc->cmd[9] = 0x78; break;
	case CD_FRAMESIZE_RAW	: cgc->cmd[9] = 0xf8; break;
	default			: cgc->cmd[9] = 0x10;
	}
	
	return cdo->generic_packet(cdi, cgc);
}

static int cdrom_read_cdda_old(struct cdrom_device_info *cdi, __u8 __user *ubuf,
			       int lba, int nframes)
{
	struct packet_command cgc;
	int ret = 0;
	int nr;

	cdi->last_sense = 0;

	memset(&cgc, 0, sizeof(cgc));

	/*
	 * start with will ra.nframes size, back down if alloc fails
	 */
	nr = nframes;
	do {
		cgc.buffer = kmalloc(CD_FRAMESIZE_RAW * nr, GFP_KERNEL);
		if (cgc.buffer)
			break;

		nr >>= 1;
	} while (nr);

	if (!nr)
		return -ENOMEM;

	if (!access_ok(VERIFY_WRITE, ubuf, nframes * CD_FRAMESIZE_RAW)) {
		ret = -EFAULT;
		goto out;
	}

	cgc.data_direction = CGC_DATA_READ;
	while (nframes > 0) {
		if (nr > nframes)
			nr = nframes;

		ret = cdrom_read_block(cdi, &cgc, lba, nr, 1, CD_FRAMESIZE_RAW);
		if (ret)
			break;
		if (__copy_to_user(ubuf, cgc.buffer, CD_FRAMESIZE_RAW * nr)) {
			ret = -EFAULT;
			break;
		}
		ubuf += CD_FRAMESIZE_RAW * nr;
		nframes -= nr;
		lba += nr;
	}
out:
	kfree(cgc.buffer);
	return ret;
}

static int cdrom_read_cdda_bpc(struct cdrom_device_info *cdi, __u8 __user *ubuf,
			       int lba, int nframes)
{
	struct request_queue *q = cdi->disk->queue;
	struct request *rq;
	struct bio *bio;
	unsigned int len;
	int nr, ret = 0;

	if (!q)
		return -ENXIO;

	rq = blk_get_request(q, READ, GFP_KERNEL);
	if (!rq)
		return -ENOMEM;

	cdi->last_sense = 0;

	while (nframes) {
		nr = nframes;
		if (cdi->cdda_method == CDDA_BPC_SINGLE)
			nr = 1;
		if (nr * CD_FRAMESIZE_RAW > (q->max_sectors << 9))
			nr = (q->max_sectors << 9) / CD_FRAMESIZE_RAW;

		len = nr * CD_FRAMESIZE_RAW;

		ret = blk_rq_map_user(q, rq, ubuf, len, GFP_KERNEL);
		if (ret)
			break;

		rq->cmd[0] = GPCMD_READ_CD;
		rq->cmd[1] = 1 << 2;
		rq->cmd[2] = (lba >> 24) & 0xff;
		rq->cmd[3] = (lba >> 16) & 0xff;
		rq->cmd[4] = (lba >>  8) & 0xff;
		rq->cmd[5] = lba & 0xff;
		rq->cmd[6] = (nr >> 16) & 0xff;
		rq->cmd[7] = (nr >>  8) & 0xff;
		rq->cmd[8] = nr & 0xff;
		rq->cmd[9] = 0xf8;

		rq->cmd_len = 12;
		rq->cmd_type = REQ_TYPE_BLOCK_PC;
		rq->timeout = 60 * HZ;
		bio = rq->bio;

		if (blk_execute_rq(q, cdi->disk, rq, 0)) {
			struct request_sense *s = rq->sense;
			ret = -EIO;
			cdi->last_sense = s->sense_key;
		}

		if (blk_rq_unmap_user(bio))
			ret = -EFAULT;

		if (ret)
			break;

		nframes -= nr;
		lba += nr;
		ubuf += len;
	}

	blk_put_request(rq);
	return ret;
}

static int cdrom_read_cdda(struct cdrom_device_info *cdi, __u8 __user *ubuf,
			   int lba, int nframes)
{
	int ret;

	if (cdi->cdda_method == CDDA_OLD)
		return cdrom_read_cdda_old(cdi, ubuf, lba, nframes);

retry:
	/*
	 * for anything else than success and io error, we need to retry
	 */
	ret = cdrom_read_cdda_bpc(cdi, ubuf, lba, nframes);
	if (!ret || ret != -EIO)
		return ret;

	/*
	 * I've seen drives get sense 4/8/3 udma crc errors on multi
	 * frame dma, so drop to single frame dma if we need to
	 */
	if (cdi->cdda_method == CDDA_BPC_FULL && nframes > 1) {
		printk("cdrom: dropping to single frame dma\n");
		cdi->cdda_method = CDDA_BPC_SINGLE;
		goto retry;
	}

	/*
	 * so we have an io error of some sort with multi frame dma. if the
	 * condition wasn't a hardware error
	 * problems, not for any error
	 */
	if (cdi->last_sense != 0x04 && cdi->last_sense != 0x0b)
		return ret;

	printk("cdrom: dropping to old style cdda (sense=%x)\n", cdi->last_sense);
	cdi->cdda_method = CDDA_OLD;
	return cdrom_read_cdda_old(cdi, ubuf, lba, nframes);	
}

static int cdrom_ioctl_multisession(struct cdrom_device_info *cdi,
		void __user *argp)
{
	struct cdrom_multisession ms_info;
	u8 requested_format;
	int ret;

	cdinfo(CD_DO_IOCTL, "entering CDROMMULTISESSION\n");

	if (!(cdi->ops->capability & CDC_MULTI_SESSION))
		return -ENOSYS;

	if (copy_from_user(&ms_info, argp, sizeof(ms_info)))
		return -EFAULT;

	requested_format = ms_info.addr_format;
	if (requested_format != CDROM_MSF && requested_format != CDROM_LBA)
		return -EINVAL;
	ms_info.addr_format = CDROM_LBA;

	ret = cdi->ops->get_last_session(cdi, &ms_info);
	if (ret)
		return ret;

	sanitize_format(&ms_info.addr, &ms_info.addr_format, requested_format);

	if (copy_to_user(argp, &ms_info, sizeof(ms_info)))
		return -EFAULT;

	cdinfo(CD_DO_IOCTL, "CDROMMULTISESSION successful\n");
	return 0;
}

static int cdrom_ioctl_eject(struct cdrom_device_info *cdi)
{
	cdinfo(CD_DO_IOCTL, "entering CDROMEJECT\n");

	if (!CDROM_CAN(CDC_OPEN_TRAY))
		return -ENOSYS;
	if (cdi->use_count != 1 || keeplocked)
		return -EBUSY;
	if (CDROM_CAN(CDC_LOCK)) {
		int ret = cdi->ops->lock_door(cdi, 0);
		if (ret)
			return ret;
	}

	return cdi->ops->tray_move(cdi, 1);
}

static int cdrom_ioctl_closetray(struct cdrom_device_info *cdi)
{
	cdinfo(CD_DO_IOCTL, "entering CDROMCLOSETRAY\n");

	if (!CDROM_CAN(CDC_CLOSE_TRAY))
		return -ENOSYS;
	return cdi->ops->tray_move(cdi, 0);
}

static int cdrom_ioctl_eject_sw(struct cdrom_device_info *cdi,
		unsigned long arg)
{
	cdinfo(CD_DO_IOCTL, "entering CDROMEJECT_SW\n");

	if (!CDROM_CAN(CDC_OPEN_TRAY))
		return -ENOSYS;
	if (keeplocked)
		return -EBUSY;

	cdi->options &= ~(CDO_AUTO_CLOSE | CDO_AUTO_EJECT);
	if (arg)
		cdi->options |= CDO_AUTO_CLOSE | CDO_AUTO_EJECT;
	return 0;
}

static int cdrom_ioctl_media_changed(struct cdrom_device_info *cdi,
		unsigned long arg)
{
	struct cdrom_changer_info *info;
	int ret;

	cdinfo(CD_DO_IOCTL, "entering CDROM_MEDIA_CHANGED\n");

	if (!CDROM_CAN(CDC_MEDIA_CHANGED))
		return -ENOSYS;

	/* cannot select disc or select current disc */
	if (!CDROM_CAN(CDC_SELECT_DISC) || arg == CDSL_CURRENT)
		return media_changed(cdi, 1);

	if ((unsigned int)arg >= cdi->capacity)
		return -EINVAL;

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	ret = cdrom_read_mech_status(cdi, info);
	if (!ret)
		ret = info->slots[arg].change;
	kfree(info);
	return ret;
}

static int cdrom_ioctl_set_options(struct cdrom_device_info *cdi,
		unsigned long arg)
{
	cdinfo(CD_DO_IOCTL, "entering CDROM_SET_OPTIONS\n");

	/*
	 * Options need to be in sync with capability.
	 * Too late for that, so we have to check each one separately.
	 */
	switch (arg) {
	case CDO_USE_FFLAGS:
	case CDO_CHECK_TYPE:
		break;
	case CDO_LOCK:
		if (!CDROM_CAN(CDC_LOCK))
			return -ENOSYS;
		break;
	case 0:
		return cdi->options;
	/* default is basically CDO_[AUTO_CLOSE|AUTO_EJECT] */
	default:
		if (!CDROM_CAN(arg))
			return -ENOSYS;
	}
	cdi->options |= (int) arg;
	return cdi->options;
}

static int cdrom_ioctl_clear_options(struct cdrom_device_info *cdi,
		unsigned long arg)
{
	cdinfo(CD_DO_IOCTL, "entering CDROM_CLEAR_OPTIONS\n");

	cdi->options &= ~(int) arg;
	return cdi->options;
}

static int cdrom_ioctl_select_speed(struct cdrom_device_info *cdi,
		unsigned long arg)
{
	cdinfo(CD_DO_IOCTL, "entering CDROM_SELECT_SPEED\n");

	if (!CDROM_CAN(CDC_SELECT_SPEED))
		return -ENOSYS;
	return cdi->ops->select_speed(cdi, arg);
}

static int cdrom_ioctl_select_disc(struct cdrom_device_info *cdi,
		unsigned long arg)
{
	cdinfo(CD_DO_IOCTL, "entering CDROM_SELECT_DISC\n");

	if (!CDROM_CAN(CDC_SELECT_DISC))
		return -ENOSYS;

	if (arg != CDSL_CURRENT && arg != CDSL_NONE) {
		if ((int)arg >= cdi->capacity)
			return -EINVAL;
	}

	/*
	 * ->select_disc is a hook to allow a driver-specific way of
	 * seleting disc.  However, since there is no equivalent hook for
	 * cdrom_slot_status this may not actually be useful...
	 */
	if (cdi->ops->select_disc)
		return cdi->ops->select_disc(cdi, arg);

	cdinfo(CD_CHANGER, "Using generic cdrom_select_disc()\n");
	return cdrom_select_disc(cdi, arg);
}

static int cdrom_ioctl_reset(struct cdrom_device_info *cdi,
		struct block_device *bdev)
{
	cdinfo(CD_DO_IOCTL, "entering CDROM_RESET\n");

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	if (!CDROM_CAN(CDC_RESET))
		return -ENOSYS;
	invalidate_bdev(bdev);
	return cdi->ops->reset(cdi);
}

static int cdrom_ioctl_lock_door(struct cdrom_device_info *cdi,
		unsigned long arg)
{
	cdinfo(CD_DO_IOCTL, "%socking door.\n", arg ? "L" : "Unl");

	if (!CDROM_CAN(CDC_LOCK))
		return -EDRIVE_CANT_DO_THIS;

	keeplocked = arg ? 1 : 0;

	/*
	 * Don't unlock the door on multiple opens by default, but allow
	 * root to do so.
	 */
	if (cdi->use_count != 1 && !arg && !capable(CAP_SYS_ADMIN))
		return -EBUSY;
	return cdi->ops->lock_door(cdi, arg);
}

static int cdrom_ioctl_debug(struct cdrom_device_info *cdi,
		unsigned long arg)
{
	cdinfo(CD_DO_IOCTL, "%sabling debug.\n", arg ? "En" : "Dis");

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	debug = arg ? 1 : 0;
	return debug;
}

static int cdrom_ioctl_get_capability(struct cdrom_device_info *cdi)
{
	cdinfo(CD_DO_IOCTL, "entering CDROM_GET_CAPABILITY\n");
	return (cdi->ops->capability & ~cdi->mask);
}

/*
 * The following function is implemented, although very few audio
 * discs give Universal Product Code information, which should just be
 * the Medium Catalog Number on the box.  Note, that the way the code
 * is written on the CD is /not/ uniform across all discs!
 */
static int cdrom_ioctl_get_mcn(struct cdrom_device_info *cdi,
		void __user *argp)
{
	struct cdrom_mcn mcn;
	int ret;

	cdinfo(CD_DO_IOCTL, "entering CDROM_GET_MCN\n");

	if (!(cdi->ops->capability & CDC_MCN))
		return -ENOSYS;
	ret = cdi->ops->get_mcn(cdi, &mcn);
	if (ret)
		return ret;

	if (copy_to_user(argp, &mcn, sizeof(mcn)))
		return -EFAULT;
	cdinfo(CD_DO_IOCTL, "CDROM_GET_MCN successful\n");
	return 0;
}

static int cdrom_ioctl_drive_status(struct cdrom_device_info *cdi,
		unsigned long arg)
{
	cdinfo(CD_DO_IOCTL, "entering CDROM_DRIVE_STATUS\n");

	if (!(cdi->ops->capability & CDC_DRIVE_STATUS))
		return -ENOSYS;
	if (!CDROM_CAN(CDC_SELECT_DISC) ||
	    (arg == CDSL_CURRENT || arg == CDSL_NONE))
		return cdi->ops->drive_status(cdi, CDSL_CURRENT);
	if (((int)arg >= cdi->capacity))
		return -EINVAL;
	return cdrom_slot_status(cdi, arg);
}

/*
 * Ok, this is where problems start.  The current interface for the
 * CDROM_DISC_STATUS ioctl is flawed.  It makes the false assumption that
 * CDs are all CDS_DATA_1 or all CDS_AUDIO, etc.  Unfortunatly, while this
 * is often the case, it is also very common for CDs to have some tracks
 * with data, and some tracks with audio.  Just because I feel like it,
 * I declare the following to be the best way to cope.  If the CD has ANY
 * data tracks on it, it will be returned as a data CD.  If it has any XA
 * tracks, I will return it as that.  Now I could simplify this interface
 * by combining these  returns with the above, but this more clearly
 * demonstrates the problem with the current interface.  Too bad this
 * wasn't designed to use bitmasks...         -Erik
 *
 * Well, now we have the option CDS_MIXED: a mixed-type CD.
 * User level programmers might feel the ioctl is not very useful.
 *					---david
 */
static int cdrom_ioctl_disc_status(struct cdrom_device_info *cdi)
{
	tracktype tracks;

	cdinfo(CD_DO_IOCTL, "entering CDROM_DISC_STATUS\n");

	cdrom_count_tracks(cdi, &tracks);
	if (tracks.error)
		return tracks.error;

	/* Policy mode on */
	if (tracks.audio > 0) {
		if (!tracks.data && !tracks.cdi && !tracks.xa)
			return CDS_AUDIO;
		else
			return CDS_MIXED;
	}

	if (tracks.cdi > 0)
		return CDS_XA_2_2;
	if (tracks.xa > 0)
		return CDS_XA_2_1;
	if (tracks.data > 0)
		return CDS_DATA_1;
	/* Policy mode off */

	cdinfo(CD_WARNING,"This disc doesn't have any tracks I recognize!\n");
	return CDS_NO_INFO;
}

static int cdrom_ioctl_changer_nslots(struct cdrom_device_info *cdi)
{
	cdinfo(CD_DO_IOCTL, "entering CDROM_CHANGER_NSLOTS\n");
	return cdi->capacity;
}

static int cdrom_ioctl_get_subchnl(struct cdrom_device_info *cdi,
		void __user *argp)
{
	struct cdrom_subchnl q;
	u8 requested, back;
	int ret;

	/* cdinfo(CD_DO_IOCTL,"entering CDROMSUBCHNL\n");*/

	if (copy_from_user(&q, argp, sizeof(q)))
		return -EFAULT;

	requested = q.cdsc_format;
	if (requested != CDROM_MSF && requested != CDROM_LBA)
		return -EINVAL;
	q.cdsc_format = CDROM_MSF;

	ret = cdi->ops->audio_ioctl(cdi, CDROMSUBCHNL, &q);
	if (ret)
		return ret;

	back = q.cdsc_format; /* local copy */
	sanitize_format(&q.cdsc_absaddr, &back, requested);
	sanitize_format(&q.cdsc_reladdr, &q.cdsc_format, requested);

	if (copy_to_user(argp, &q, sizeof(q)))
		return -EFAULT;
	/* cdinfo(CD_DO_IOCTL, "CDROMSUBCHNL successful\n"); */
	return 0;
}

static int cdrom_ioctl_read_tochdr(struct cdrom_device_info *cdi,
		void __user *argp)
{
	struct cdrom_tochdr header;
	int ret;

	/* cdinfo(CD_DO_IOCTL, "entering CDROMREADTOCHDR\n"); */

	if (copy_from_user(&header, argp, sizeof(header)))
		return -EFAULT;

	ret = cdi->ops->audio_ioctl(cdi, CDROMREADTOCHDR, &header);
	if (ret)
		return ret;

	if (copy_to_user(argp, &header, sizeof(header)))
		return -EFAULT;
	/* cdinfo(CD_DO_IOCTL, "CDROMREADTOCHDR successful\n"); */
	return 0;
}

static int cdrom_ioctl_read_tocentry(struct cdrom_device_info *cdi,
		void __user *argp)
{
	struct cdrom_tocentry entry;
	u8 requested_format;
	int ret;

	/* cdinfo(CD_DO_IOCTL, "entering CDROMREADTOCENTRY\n"); */

	if (copy_from_user(&entry, argp, sizeof(entry)))
		return -EFAULT;

	requested_format = entry.cdte_format;
	if (requested_format != CDROM_MSF && requested_format != CDROM_LBA)
		return -EINVAL;
	/* make interface to low-level uniform */
	entry.cdte_format = CDROM_MSF;
	ret = cdi->ops->audio_ioctl(cdi, CDROMREADTOCENTRY, &entry);
	if (ret)
		return ret;
	sanitize_format(&entry.cdte_addr, &entry.cdte_format, requested_format);

	if (copy_to_user(argp, &entry, sizeof(entry)))
		return -EFAULT;
	/* cdinfo(CD_DO_IOCTL, "CDROMREADTOCENTRY successful\n"); */
	return 0;
}

static int cdrom_ioctl_play_msf(struct cdrom_device_info *cdi,
		void __user *argp)
{
	struct cdrom_msf msf;

	cdinfo(CD_DO_IOCTL, "entering CDROMPLAYMSF\n");

	if (!CDROM_CAN(CDC_PLAY_AUDIO))
		return -ENOSYS;
	if (copy_from_user(&msf, argp, sizeof(msf)))
		return -EFAULT;
	return cdi->ops->audio_ioctl(cdi, CDROMPLAYMSF, &msf);
}

static int cdrom_ioctl_play_trkind(struct cdrom_device_info *cdi,
		void __user *argp)
{
	struct cdrom_ti ti;
	int ret;

	cdinfo(CD_DO_IOCTL, "entering CDROMPLAYTRKIND\n");

	if (!CDROM_CAN(CDC_PLAY_AUDIO))
		return -ENOSYS;
	if (copy_from_user(&ti, argp, sizeof(ti)))
		return -EFAULT;

	ret = check_for_audio_disc(cdi, cdi->ops);
	if (ret)
		return ret;
	return cdi->ops->audio_ioctl(cdi, CDROMPLAYTRKIND, &ti);
}
static int cdrom_ioctl_volctrl(struct cdrom_device_info *cdi,
		void __user *argp)
{
	struct cdrom_volctrl volume;

	cdinfo(CD_DO_IOCTL, "entering CDROMVOLCTRL\n");

	if (!CDROM_CAN(CDC_PLAY_AUDIO))
		return -ENOSYS;
	if (copy_from_user(&volume, argp, sizeof(volume)))
		return -EFAULT;
	return cdi->ops->audio_ioctl(cdi, CDROMVOLCTRL, &volume);
}

static int cdrom_ioctl_volread(struct cdrom_device_info *cdi,
		void __user *argp)
{
	struct cdrom_volctrl volume;
	int ret;

	cdinfo(CD_DO_IOCTL, "entering CDROMVOLREAD\n");

	if (!CDROM_CAN(CDC_PLAY_AUDIO))
		return -ENOSYS;

	ret = cdi->ops->audio_ioctl(cdi, CDROMVOLREAD, &volume);
	if (ret)
		return ret;

	if (copy_to_user(argp, &volume, sizeof(volume)))
		return -EFAULT;
	return 0;
}

static int cdrom_ioctl_audioctl(struct cdrom_device_info *cdi,
		unsigned int cmd)
{
	int ret;

	cdinfo(CD_DO_IOCTL, "doing audio ioctl (start/stop/pause/resume)\n");

	if (!CDROM_CAN(CDC_PLAY_AUDIO))
		return -ENOSYS;
	ret = check_for_audio_disc(cdi, cdi->ops);
	if (ret)
		return ret;
	return cdi->ops->audio_ioctl(cdi, cmd, NULL);
}

/*
 * Just about every imaginable ioctl is supported in the Uniform layer
 * these days.
 * ATAPI / SCSI specific code now mainly resides in mmc_ioctl().
 */
int cdrom_ioctl(struct file * file, struct cdrom_device_info *cdi,
		struct inode *ip, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int ret;
	struct gendisk *disk = ip->i_bdev->bd_disk;

	/*
	 * Try the generic SCSI command ioctl's first.
	 */
	ret = scsi_cmd_ioctl(file, disk->queue, disk, cmd, argp);
	if (ret != -ENOTTY)
		return ret;

	switch (cmd) {
	case CDROMMULTISESSION:
		return cdrom_ioctl_multisession(cdi, argp);
	case CDROMEJECT:
		return cdrom_ioctl_eject(cdi);
	case CDROMCLOSETRAY:
		return cdrom_ioctl_closetray(cdi);
	case CDROMEJECT_SW:
		return cdrom_ioctl_eject_sw(cdi, arg);
	case CDROM_MEDIA_CHANGED:
		return cdrom_ioctl_media_changed(cdi, arg);
	case CDROM_SET_OPTIONS:
		return cdrom_ioctl_set_options(cdi, arg);
	case CDROM_CLEAR_OPTIONS:
		return cdrom_ioctl_clear_options(cdi, arg);
	case CDROM_SELECT_SPEED:
		return cdrom_ioctl_select_speed(cdi, arg);
	case CDROM_SELECT_DISC:
		return cdrom_ioctl_select_disc(cdi, arg);
	case CDROMRESET:
		return cdrom_ioctl_reset(cdi, ip->i_bdev);
	case CDROM_LOCKDOOR:
		return cdrom_ioctl_lock_door(cdi, arg);
	case CDROM_DEBUG:
		return cdrom_ioctl_debug(cdi, arg);
	case CDROM_GET_CAPABILITY:
		return cdrom_ioctl_get_capability(cdi);
	case CDROM_GET_MCN:
		return cdrom_ioctl_get_mcn(cdi, argp);
	case CDROM_DRIVE_STATUS:
		return cdrom_ioctl_drive_status(cdi, arg);
	case CDROM_DISC_STATUS:
		return cdrom_ioctl_disc_status(cdi);
	case CDROM_CHANGER_NSLOTS:
		return cdrom_ioctl_changer_nslots(cdi);
	}

	/*
	 * Use the ioctls that are implemented through the generic_packet()
	 * interface. this may look at bit funny, but if -ENOTTY is
	 * returned that particular ioctl is not implemented and we
	 * let it go through the device specific ones.
	 */
	if (CDROM_CAN(CDC_GENERIC_PACKET)) {
		ret = mmc_ioctl(cdi, cmd, arg);
		if (ret != -ENOTTY)
			return ret;
	}

	/*
	 * Note: most of the cdinfo() calls are commented out here,
	 * because they fill up the sys log when CD players poll
	 * the drive.
	 */
	switch (cmd) {
	case CDROMSUBCHNL:
		return cdrom_ioctl_get_subchnl(cdi, argp);
	case CDROMREADTOCHDR:
		return cdrom_ioctl_read_tochdr(cdi, argp);
	case CDROMREADTOCENTRY:
		return cdrom_ioctl_read_tocentry(cdi, argp);
	case CDROMPLAYMSF:
		return cdrom_ioctl_play_msf(cdi, argp);
	case CDROMPLAYTRKIND:
		return cdrom_ioctl_play_trkind(cdi, argp);
	case CDROMVOLCTRL:
		return cdrom_ioctl_volctrl(cdi, argp);
	case CDROMVOLREAD:
		return cdrom_ioctl_volread(cdi, argp);
	case CDROMSTART:
	case CDROMSTOP:
	case CDROMPAUSE:
	case CDROMRESUME:
		return cdrom_ioctl_audioctl(cdi, cmd);
	}

	return -ENOSYS;
}

/*
 * Required when we need to use READ_10 to issue other than 2048 block
 * reads
 */
static int cdrom_switch_blocksize(struct cdrom_device_info *cdi, int size)
{
	struct cdrom_device_ops *cdo = cdi->ops;
	struct packet_command cgc;
	struct modesel_head mh;

	memset(&mh, 0, sizeof(mh));
	mh.block_desc_length = 0x08;
	mh.block_length_med = (size >> 8) & 0xff;
	mh.block_length_lo = size & 0xff;

	memset(&cgc, 0, sizeof(cgc));
	cgc.cmd[0] = 0x15;
	cgc.cmd[1] = 1 << 4;
	cgc.cmd[4] = 12;
	cgc.buflen = sizeof(mh);
	cgc.buffer = (char *) &mh;
	cgc.data_direction = CGC_DATA_WRITE;
	mh.block_desc_length = 0x08;
	mh.block_length_med = (size >> 8) & 0xff;
	mh.block_length_lo = size & 0xff;

	return cdo->generic_packet(cdi, &cgc);
}

static int mmc_ioctl(struct cdrom_device_info *cdi, unsigned int cmd,
		     unsigned long arg)
{		
	struct cdrom_device_ops *cdo = cdi->ops;
	struct packet_command cgc;
	struct request_sense sense;
	unsigned char buffer[32];
	int ret = 0;

	memset(&cgc, 0, sizeof(cgc));

	/* build a unified command and queue it through
	   cdo->generic_packet() */
	switch (cmd) {
	case CDROMREADRAW:
	case CDROMREADMODE1:
	case CDROMREADMODE2: {
		struct cdrom_msf msf;
		int blocksize = 0, format = 0, lba;
		
		switch (cmd) {
		case CDROMREADRAW:
			blocksize = CD_FRAMESIZE_RAW;
			break;
		case CDROMREADMODE1:
			blocksize = CD_FRAMESIZE;
			format = 2;
			break;
		case CDROMREADMODE2:
			blocksize = CD_FRAMESIZE_RAW0;
			break;
		}
		IOCTL_IN(arg, struct cdrom_msf, msf);
		lba = msf_to_lba(msf.cdmsf_min0,msf.cdmsf_sec0,msf.cdmsf_frame0);
		/* FIXME: we need upper bound checking, too!! */
		if (lba < 0)
			return -EINVAL;
		cgc.buffer = kmalloc(blocksize, GFP_KERNEL);
		if (cgc.buffer == NULL)
			return -ENOMEM;
		memset(&sense, 0, sizeof(sense));
		cgc.sense = &sense;
		cgc.data_direction = CGC_DATA_READ;
		ret = cdrom_read_block(cdi, &cgc, lba, 1, format, blocksize);
		if (ret && sense.sense_key==0x05 && sense.asc==0x20 && sense.ascq==0x00) {
			/*
			 * SCSI-II devices are not required to support
			 * READ_CD, so let's try switching block size
			 */
			/* FIXME: switch back again... */
			if ((ret = cdrom_switch_blocksize(cdi, blocksize))) {
				kfree(cgc.buffer);
				return ret;
			}
			cgc.sense = NULL;
			ret = cdrom_read_cd(cdi, &cgc, lba, blocksize, 1);
			ret |= cdrom_switch_blocksize(cdi, blocksize);
		}
		if (!ret && copy_to_user((char __user *)arg, cgc.buffer, blocksize))
			ret = -EFAULT;
		kfree(cgc.buffer);
		return ret;
		}
	case CDROMREADAUDIO: {
		struct cdrom_read_audio ra;
		int lba;

		IOCTL_IN(arg, struct cdrom_read_audio, ra);

		if (ra.addr_format == CDROM_MSF)
			lba = msf_to_lba(ra.addr.msf.minute,
					 ra.addr.msf.second,
					 ra.addr.msf.frame);
		else if (ra.addr_format == CDROM_LBA)
			lba = ra.addr.lba;
		else
			return -EINVAL;

		/* FIXME: we need upper bound checking, too!! */
		if (lba < 0 || ra.nframes <= 0 || ra.nframes > CD_FRAMES)
			return -EINVAL;

		return cdrom_read_cdda(cdi, ra.buf, lba, ra.nframes);
		}
	case CDROMSUBCHNL: {
		struct cdrom_subchnl q;
		u_char requested, back;
		IOCTL_IN(arg, struct cdrom_subchnl, q);
		requested = q.cdsc_format;
		if (!((requested == CDROM_MSF) ||
		      (requested == CDROM_LBA)))
			return -EINVAL;
		q.cdsc_format = CDROM_MSF;
		if ((ret = cdrom_read_subchannel(cdi, &q, 0)))
			return ret;
		back = q.cdsc_format; /* local copy */
		sanitize_format(&q.cdsc_absaddr, &back, requested);
		sanitize_format(&q.cdsc_reladdr, &q.cdsc_format, requested);
		IOCTL_OUT(arg, struct cdrom_subchnl, q);
		/* cdinfo(CD_DO_IOCTL, "CDROMSUBCHNL successful\n"); */ 
		return 0;
		}
	case CDROMPLAYMSF: {
		struct cdrom_msf msf;
		cdinfo(CD_DO_IOCTL, "entering CDROMPLAYMSF\n");
		IOCTL_IN(arg, struct cdrom_msf, msf);
		cgc.cmd[0] = GPCMD_PLAY_AUDIO_MSF;
		cgc.cmd[3] = msf.cdmsf_min0;
		cgc.cmd[4] = msf.cdmsf_sec0;
		cgc.cmd[5] = msf.cdmsf_frame0;
		cgc.cmd[6] = msf.cdmsf_min1;
		cgc.cmd[7] = msf.cdmsf_sec1;
		cgc.cmd[8] = msf.cdmsf_frame1;
		cgc.data_direction = CGC_DATA_NONE;
		return cdo->generic_packet(cdi, &cgc);
		}
	case CDROMPLAYBLK: {
		struct cdrom_blk blk;
		cdinfo(CD_DO_IOCTL, "entering CDROMPLAYBLK\n");
		IOCTL_IN(arg, struct cdrom_blk, blk);
		cgc.cmd[0] = GPCMD_PLAY_AUDIO_10;
		cgc.cmd[2] = (blk.from >> 24) & 0xff;
		cgc.cmd[3] = (blk.from >> 16) & 0xff;
		cgc.cmd[4] = (blk.from >>  8) & 0xff;
		cgc.cmd[5] = blk.from & 0xff;
		cgc.cmd[7] = (blk.len >> 8) & 0xff;
		cgc.cmd[8] = blk.len & 0xff;
		cgc.data_direction = CGC_DATA_NONE;
		return cdo->generic_packet(cdi, &cgc);
		}
	case CDROMVOLCTRL:
	case CDROMVOLREAD: {
		struct cdrom_volctrl volctrl;
		char mask[sizeof(buffer)];
		unsigned short offset;

		cdinfo(CD_DO_IOCTL, "entering CDROMVOLUME\n");

		IOCTL_IN(arg, struct cdrom_volctrl, volctrl);

		cgc.buffer = buffer;
		cgc.buflen = 24;
		if ((ret = cdrom_mode_sense(cdi, &cgc, GPMODE_AUDIO_CTL_PAGE, 0)))
		    return ret;
		
		/* originally the code depended on buffer[1] to determine
		   how much data is available for transfer. buffer[1] is
		   unfortunately ambigious and the only reliable way seem
		   to be to simply skip over the block descriptor... */
		offset = 8 + be16_to_cpu(*(__be16 *)(buffer+6));

		if (offset + 16 > sizeof(buffer))
			return -E2BIG;

		if (offset + 16 > cgc.buflen) {
			cgc.buflen = offset+16;
			ret = cdrom_mode_sense(cdi, &cgc,
						GPMODE_AUDIO_CTL_PAGE, 0);
			if (ret)
				return ret;
		}

		/* sanity check */
		if ((buffer[offset] & 0x3f) != GPMODE_AUDIO_CTL_PAGE ||
				buffer[offset+1] < 14)
			return -EINVAL;

		/* now we have the current volume settings. if it was only
		   a CDROMVOLREAD, return these values */
		if (cmd == CDROMVOLREAD) {
			volctrl.channel0 = buffer[offset+9];
			volctrl.channel1 = buffer[offset+11];
			volctrl.channel2 = buffer[offset+13];
			volctrl.channel3 = buffer[offset+15];
			IOCTL_OUT(arg, struct cdrom_volctrl, volctrl);
			return 0;
		}
		
		/* get the volume mask */
		cgc.buffer = mask;
		if ((ret = cdrom_mode_sense(cdi, &cgc, 
				GPMODE_AUDIO_CTL_PAGE, 1)))
			return ret;

		buffer[offset+9] = volctrl.channel0 & mask[offset+9];
		buffer[offset+11] = volctrl.channel1 & mask[offset+11];
		buffer[offset+13] = volctrl.channel2 & mask[offset+13];
		buffer[offset+15] = volctrl.channel3 & mask[offset+15];

		/* set volume */
		cgc.buffer = buffer + offset - 8;
		memset(cgc.buffer, 0, 8);
		return cdrom_mode_select(cdi, &cgc);
		}

	case CDROMSTART:
	case CDROMSTOP: {
		cdinfo(CD_DO_IOCTL, "entering CDROMSTART/CDROMSTOP\n"); 
		cgc.cmd[0] = GPCMD_START_STOP_UNIT;
		cgc.cmd[1] = 1;
		cgc.cmd[4] = (cmd == CDROMSTART) ? 1 : 0;
		cgc.data_direction = CGC_DATA_NONE;
		return cdo->generic_packet(cdi, &cgc);
		}

	case CDROMPAUSE:
	case CDROMRESUME: {
		cdinfo(CD_DO_IOCTL, "entering CDROMPAUSE/CDROMRESUME\n"); 
		cgc.cmd[0] = GPCMD_PAUSE_RESUME;
		cgc.cmd[8] = (cmd == CDROMRESUME) ? 1 : 0;
		cgc.data_direction = CGC_DATA_NONE;
		return cdo->generic_packet(cdi, &cgc);
		}

	case DVD_READ_STRUCT: {
		dvd_struct *s;
		int size = sizeof(dvd_struct);
		if (!CDROM_CAN(CDC_DVD))
			return -ENOSYS;
		if ((s = kmalloc(size, GFP_KERNEL)) == NULL)
			return -ENOMEM;
		cdinfo(CD_DO_IOCTL, "entering DVD_READ_STRUCT\n"); 
		if (copy_from_user(s, (dvd_struct __user *)arg, size)) {
			kfree(s);
			return -EFAULT;
		}
		if ((ret = dvd_read_struct(cdi, s))) {
			kfree(s);
			return ret;
		}
		if (copy_to_user((dvd_struct __user *)arg, s, size))
			ret = -EFAULT;
		kfree(s);
		return ret;
		}

	case DVD_AUTH: {
		dvd_authinfo ai;
		if (!CDROM_CAN(CDC_DVD))
			return -ENOSYS;
		cdinfo(CD_DO_IOCTL, "entering DVD_AUTH\n"); 
		IOCTL_IN(arg, dvd_authinfo, ai);
		if ((ret = dvd_do_auth (cdi, &ai)))
			return ret;
		IOCTL_OUT(arg, dvd_authinfo, ai);
		return 0;
		}

	case CDROM_NEXT_WRITABLE: {
		long next = 0;
		cdinfo(CD_DO_IOCTL, "entering CDROM_NEXT_WRITABLE\n"); 
		if ((ret = cdrom_get_next_writable(cdi, &next)))
			return ret;
		IOCTL_OUT(arg, long, next);
		return 0;
		}
	case CDROM_LAST_WRITTEN: {
		long last = 0;
		cdinfo(CD_DO_IOCTL, "entering CDROM_LAST_WRITTEN\n"); 
		if ((ret = cdrom_get_last_written(cdi, &last)))
			return ret;
		IOCTL_OUT(arg, long, last);
		return 0;
		}
	} /* switch */

	return -ENOTTY;
}

static int cdrom_get_track_info(struct cdrom_device_info *cdi, __u16 track, __u8 type,
			 track_information *ti)
{
	struct cdrom_device_ops *cdo = cdi->ops;
	struct packet_command cgc;
	int ret, buflen;

	init_cdrom_command(&cgc, ti, 8, CGC_DATA_READ);
	cgc.cmd[0] = GPCMD_READ_TRACK_RZONE_INFO;
	cgc.cmd[1] = type & 3;
	cgc.cmd[4] = (track & 0xff00) >> 8;
	cgc.cmd[5] = track & 0xff;
	cgc.cmd[8] = 8;
	cgc.quiet = 1;

	if ((ret = cdo->generic_packet(cdi, &cgc)))
		return ret;
	
	buflen = be16_to_cpu(ti->track_information_length) +
		     sizeof(ti->track_information_length);

	if (buflen > sizeof(track_information))
		buflen = sizeof(track_information);

	cgc.cmd[8] = cgc.buflen = buflen;
	if ((ret = cdo->generic_packet(cdi, &cgc)))
		return ret;

	/* return actual fill size */
	return buflen;
}

/* requires CD R/RW */
static int cdrom_get_disc_info(struct cdrom_device_info *cdi, disc_information *di)
{
	struct cdrom_device_ops *cdo = cdi->ops;
	struct packet_command cgc;
	int ret, buflen;

	/* set up command and get the disc info */
	init_cdrom_command(&cgc, di, sizeof(*di), CGC_DATA_READ);
	cgc.cmd[0] = GPCMD_READ_DISC_INFO;
	cgc.cmd[8] = cgc.buflen = 2;
	cgc.quiet = 1;

	if ((ret = cdo->generic_packet(cdi, &cgc)))
		return ret;

	/* not all drives have the same disc_info length, so requeue
	 * packet with the length the drive tells us it can supply
	 */
	buflen = be16_to_cpu(di->disc_information_length) +
		     sizeof(di->disc_information_length);

	if (buflen > sizeof(disc_information))
		buflen = sizeof(disc_information);

	cgc.cmd[8] = cgc.buflen = buflen;
	if ((ret = cdo->generic_packet(cdi, &cgc)))
		return ret;

	/* return actual fill size */
	return buflen;
}

/* return the last written block on the CD-R media. this is for the udf
   file system. */
int cdrom_get_last_written(struct cdrom_device_info *cdi, long *last_written)
{
	struct cdrom_tocentry toc;
	disc_information di;
	track_information ti;
	__u32 last_track;
	int ret = -1, ti_size;

	if (!CDROM_CAN(CDC_GENERIC_PACKET))
		goto use_toc;

	ret = cdrom_get_disc_info(cdi, &di);
	if (ret < (int)(offsetof(typeof(di), last_track_lsb)
			+ sizeof(di.last_track_lsb)))
		goto use_toc;

	/* if unit didn't return msb, it's zeroed by cdrom_get_disc_info */
	last_track = (di.last_track_msb << 8) | di.last_track_lsb;
	ti_size = cdrom_get_track_info(cdi, last_track, 1, &ti);
	if (ti_size < (int)offsetof(typeof(ti), track_start))
		goto use_toc;

	/* if this track is blank, try the previous. */
	if (ti.blank) {
		if (last_track==1)
			goto use_toc;
		last_track--;
		ti_size = cdrom_get_track_info(cdi, last_track, 1, &ti);
	}

	if (ti_size < (int)(offsetof(typeof(ti), track_size)
				+ sizeof(ti.track_size)))
		goto use_toc;

	/* if last recorded field is valid, return it. */
	if (ti.lra_v && ti_size >= (int)(offsetof(typeof(ti), last_rec_address)
				+ sizeof(ti.last_rec_address))) {
		*last_written = be32_to_cpu(ti.last_rec_address);
	} else {
		/* make it up instead */
		*last_written = be32_to_cpu(ti.track_start) +
				be32_to_cpu(ti.track_size);
		if (ti.free_blocks)
			*last_written -= (be32_to_cpu(ti.free_blocks) + 7);
	}
	return 0;

	/* this is where we end up if the drive either can't do a
	   GPCMD_READ_DISC_INFO or GPCMD_READ_TRACK_RZONE_INFO or if
	   it doesn't give enough information or fails. then we return
	   the toc contents. */
use_toc:
	toc.cdte_format = CDROM_MSF;
	toc.cdte_track = CDROM_LEADOUT;
	if ((ret = cdi->ops->audio_ioctl(cdi, CDROMREADTOCENTRY, &toc)))
		return ret;
	sanitize_format(&toc.cdte_addr, &toc.cdte_format, CDROM_LBA);
	*last_written = toc.cdte_addr.lba;
	return 0;
}

/* return the next writable block. also for udf file system. */
static int cdrom_get_next_writable(struct cdrom_device_info *cdi, long *next_writable)
{
	disc_information di;
	track_information ti;
	__u16 last_track;
	int ret, ti_size;

	if (!CDROM_CAN(CDC_GENERIC_PACKET))
		goto use_last_written;

	ret = cdrom_get_disc_info(cdi, &di);
	if (ret < 0 || ret < offsetof(typeof(di), last_track_lsb)
				+ sizeof(di.last_track_lsb))
		goto use_last_written;

	/* if unit didn't return msb, it's zeroed by cdrom_get_disc_info */
	last_track = (di.last_track_msb << 8) | di.last_track_lsb;
	ti_size = cdrom_get_track_info(cdi, last_track, 1, &ti);
	if (ti_size < 0 || ti_size < offsetof(typeof(ti), track_start))
		goto use_last_written;

        /* if this track is blank, try the previous. */
	if (ti.blank) {
		if (last_track == 1)
			goto use_last_written;
		last_track--;
		ti_size = cdrom_get_track_info(cdi, last_track, 1, &ti);
		if (ti_size < 0)
			goto use_last_written;
	}

	/* if next recordable address field is valid, use it. */
	if (ti.nwa_v && ti_size >= offsetof(typeof(ti), next_writable)
				+ sizeof(ti.next_writable)) {
		*next_writable = be32_to_cpu(ti.next_writable);
		return 0;
	}

use_last_written:
	if ((ret = cdrom_get_last_written(cdi, next_writable))) {
		*next_writable = 0;
		return ret;
	} else {
		*next_writable += 7;
		return 0;
	}
}

EXPORT_SYMBOL(cdrom_get_last_written);
EXPORT_SYMBOL(register_cdrom);
EXPORT_SYMBOL(unregister_cdrom);
EXPORT_SYMBOL(cdrom_open);
EXPORT_SYMBOL(cdrom_release);
EXPORT_SYMBOL(cdrom_ioctl);
EXPORT_SYMBOL(cdrom_media_changed);
EXPORT_SYMBOL(cdrom_number_of_slots);
EXPORT_SYMBOL(cdrom_mode_select);
EXPORT_SYMBOL(cdrom_mode_sense);
EXPORT_SYMBOL(init_cdrom_command);
EXPORT_SYMBOL(cdrom_get_media_event);

#ifdef CONFIG_SYSCTL

#define CDROM_STR_SIZE 1000

static struct cdrom_sysctl_settings {
	char	info[CDROM_STR_SIZE];	/* general info */
	int	autoclose;		/* close tray upon mount, etc */
	int	autoeject;		/* eject on umount */
	int	debug;			/* turn on debugging messages */
	int	lock;			/* lock the door on device open */
	int	check;			/* check media type */
} cdrom_sysctl_settings;

enum cdrom_print_option {
	CTL_NAME,
	CTL_SPEED,
	CTL_SLOTS,
	CTL_CAPABILITY
};

static int cdrom_print_info(const char *header, int val, char *info,
				int *pos, enum cdrom_print_option option)
{
	const int max_size = sizeof(cdrom_sysctl_settings.info);
	struct cdrom_device_info *cdi;
	int ret;

	ret = scnprintf(info + *pos, max_size - *pos, header);
	if (!ret)
		return 1;

	*pos += ret;

	list_for_each_entry(cdi, &cdrom_list, list) {
		switch (option) {
		case CTL_NAME:
			ret = scnprintf(info + *pos, max_size - *pos,
					"\t%s", cdi->name);
			break;
		case CTL_SPEED:
			ret = scnprintf(info + *pos, max_size - *pos,
					"\t%d", cdi->speed);
			break;
		case CTL_SLOTS:
			ret = scnprintf(info + *pos, max_size - *pos,
					"\t%d", cdi->capacity);
			break;
		case CTL_CAPABILITY:
			ret = scnprintf(info + *pos, max_size - *pos,
					"\t%d", CDROM_CAN(val) != 0);
			break;
		default:
			printk(KERN_INFO "cdrom: invalid option%d\n", option);
			return 1;
		}
		if (!ret)
			return 1;
		*pos += ret;
	}

	return 0;
}

static int cdrom_sysctl_info(ctl_table *ctl, int write, struct file * filp,
                           void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int pos;
	char *info = cdrom_sysctl_settings.info;
	const int max_size = sizeof(cdrom_sysctl_settings.info);
	
	if (!*lenp || (*ppos && !write)) {
		*lenp = 0;
		return 0;
	}

	mutex_lock(&cdrom_mutex);

	pos = sprintf(info, "CD-ROM information, " VERSION "\n");
	
	if (cdrom_print_info("\ndrive name:\t", 0, info, &pos, CTL_NAME))
		goto done;
	if (cdrom_print_info("\ndrive speed:\t", 0, info, &pos, CTL_SPEED))
		goto done;
	if (cdrom_print_info("\ndrive # of slots:", 0, info, &pos, CTL_SLOTS))
		goto done;
	if (cdrom_print_info("\nCan close tray:\t",
				CDC_CLOSE_TRAY, info, &pos, CTL_CAPABILITY))
		goto done;
	if (cdrom_print_info("\nCan open tray:\t",
				CDC_OPEN_TRAY, info, &pos, CTL_CAPABILITY))
		goto done;
	if (cdrom_print_info("\nCan lock tray:\t",
				CDC_LOCK, info, &pos, CTL_CAPABILITY))
		goto done;
	if (cdrom_print_info("\nCan change speed:",
				CDC_SELECT_SPEED, info, &pos, CTL_CAPABILITY))
		goto done;
	if (cdrom_print_info("\nCan select disk:",
				CDC_SELECT_DISC, info, &pos, CTL_CAPABILITY))
		goto done;
	if (cdrom_print_info("\nCan read multisession:",
				CDC_MULTI_SESSION, info, &pos, CTL_CAPABILITY))
		goto done;
	if (cdrom_print_info("\nCan read MCN:\t",
				CDC_MCN, info, &pos, CTL_CAPABILITY))
		goto done;
	if (cdrom_print_info("\nReports media changed:",
				CDC_MEDIA_CHANGED, info, &pos, CTL_CAPABILITY))
		goto done;
	if (cdrom_print_info("\nCan play audio:\t",
				CDC_PLAY_AUDIO, info, &pos, CTL_CAPABILITY))
		goto done;
	if (cdrom_print_info("\nCan write CD-R:\t",
				CDC_CD_R, info, &pos, CTL_CAPABILITY))
		goto done;
	if (cdrom_print_info("\nCan write CD-RW:",
				CDC_CD_RW, info, &pos, CTL_CAPABILITY))
		goto done;
	if (cdrom_print_info("\nCan read DVD:\t",
				CDC_DVD, info, &pos, CTL_CAPABILITY))
		goto done;
	if (cdrom_print_info("\nCan write DVD-R:",
				CDC_DVD_R, info, &pos, CTL_CAPABILITY))
		goto done;
	if (cdrom_print_info("\nCan write DVD-RAM:",
				CDC_DVD_RAM, info, &pos, CTL_CAPABILITY))
		goto done;
	if (cdrom_print_info("\nCan read MRW:\t",
				CDC_MRW, info, &pos, CTL_CAPABILITY))
		goto done;
	if (cdrom_print_info("\nCan write MRW:\t",
				CDC_MRW_W, info, &pos, CTL_CAPABILITY))
		goto done;
	if (cdrom_print_info("\nCan write RAM:\t",
				CDC_RAM, info, &pos, CTL_CAPABILITY))
		goto done;
	if (!scnprintf(info + pos, max_size - pos, "\n\n"))
		goto done;
doit:
	mutex_unlock(&cdrom_mutex);
	return proc_dostring(ctl, write, filp, buffer, lenp, ppos);
done:
	printk(KERN_INFO "cdrom: info buffer too small\n");
	goto doit;
}

/* Unfortunately, per device settings are not implemented through
   procfs/sysctl yet. When they are, this will naturally disappear. For now
   just update all drives. Later this will become the template on which
   new registered drives will be based. */
static void cdrom_update_settings(void)
{
	struct cdrom_device_info *cdi;

	mutex_lock(&cdrom_mutex);
	list_for_each_entry(cdi, &cdrom_list, list) {
		if (autoclose && CDROM_CAN(CDC_CLOSE_TRAY))
			cdi->options |= CDO_AUTO_CLOSE;
		else if (!autoclose)
			cdi->options &= ~CDO_AUTO_CLOSE;
		if (autoeject && CDROM_CAN(CDC_OPEN_TRAY))
			cdi->options |= CDO_AUTO_EJECT;
		else if (!autoeject)
			cdi->options &= ~CDO_AUTO_EJECT;
		if (lockdoor && CDROM_CAN(CDC_LOCK))
			cdi->options |= CDO_LOCK;
		else if (!lockdoor)
			cdi->options &= ~CDO_LOCK;
		if (check_media_type)
			cdi->options |= CDO_CHECK_TYPE;
		else
			cdi->options &= ~CDO_CHECK_TYPE;
	}
	mutex_unlock(&cdrom_mutex);
}

static int cdrom_sysctl_handler(ctl_table *ctl, int write, struct file * filp,
				void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;
	
	ret = proc_dointvec(ctl, write, filp, buffer, lenp, ppos);

	if (write) {
	
		/* we only care for 1 or 0. */
		autoclose        = !!cdrom_sysctl_settings.autoclose;
		autoeject        = !!cdrom_sysctl_settings.autoeject;
		debug	         = !!cdrom_sysctl_settings.debug;
		lockdoor         = !!cdrom_sysctl_settings.lock;
		check_media_type = !!cdrom_sysctl_settings.check;

		/* update the option flags according to the changes. we
		   don't have per device options through sysctl yet,
		   but we will have and then this will disappear. */
		cdrom_update_settings();
	}

        return ret;
}

/* Place files in /proc/sys/dev/cdrom */
static ctl_table cdrom_table[] = {
	{
		.procname	= "info",
		.data		= &cdrom_sysctl_settings.info, 
		.maxlen		= CDROM_STR_SIZE,
		.mode		= 0444,
		.proc_handler	= &cdrom_sysctl_info,
	},
	{
		.procname	= "autoclose",
		.data		= &cdrom_sysctl_settings.autoclose,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &cdrom_sysctl_handler,
	},
	{
		.procname	= "autoeject",
		.data		= &cdrom_sysctl_settings.autoeject,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &cdrom_sysctl_handler,
	},
	{
		.procname	= "debug",
		.data		= &cdrom_sysctl_settings.debug,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &cdrom_sysctl_handler,
	},
	{
		.procname	= "lock",
		.data		= &cdrom_sysctl_settings.lock,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &cdrom_sysctl_handler,
	},
	{
		.procname	= "check_media",
		.data		= &cdrom_sysctl_settings.check,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &cdrom_sysctl_handler
	},
	{ .ctl_name = 0 }
};

static ctl_table cdrom_cdrom_table[] = {
	{
		.ctl_name	= DEV_CDROM,
		.procname	= "cdrom",
		.maxlen		= 0,
		.mode		= 0555,
		.child		= cdrom_table,
	},
	{ .ctl_name = 0 }
};

/* Make sure that /proc/sys/dev is there */
static ctl_table cdrom_root_table[] = {
	{
		.ctl_name	= CTL_DEV,
		.procname	= "dev",
		.maxlen		= 0,
		.mode		= 0555,
		.child		= cdrom_cdrom_table,
	},
	{ .ctl_name = 0 }
};
static struct ctl_table_header *cdrom_sysctl_header;

static void cdrom_sysctl_register(void)
{
	static int initialized;

	if (initialized == 1)
		return;

	cdrom_sysctl_header = register_sysctl_table(cdrom_root_table);

	/* set the defaults */
	cdrom_sysctl_settings.autoclose = autoclose;
	cdrom_sysctl_settings.autoeject = autoeject;
	cdrom_sysctl_settings.debug = debug;
	cdrom_sysctl_settings.lock = lockdoor;
	cdrom_sysctl_settings.check = check_media_type;

	initialized = 1;
}

static void cdrom_sysctl_unregister(void)
{
	if (cdrom_sysctl_header)
		unregister_sysctl_table(cdrom_sysctl_header);
}

#else /* CONFIG_SYSCTL */

static void cdrom_sysctl_register(void)
{
}

static void cdrom_sysctl_unregister(void)
{
}

#endif /* CONFIG_SYSCTL */

static int __init cdrom_init(void)
{
	cdrom_sysctl_register();

	return 0;
}

static void __exit cdrom_exit(void)
{
	printk(KERN_INFO "Uniform CD-ROM driver unloaded\n");
	cdrom_sysctl_unregister();
}

module_init(cdrom_init);
module_exit(cdrom_exit);
MODULE_LICENSE("GPL");
