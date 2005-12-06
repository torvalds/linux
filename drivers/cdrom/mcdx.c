/*
 * The Mitsumi CDROM interface
 * Copyright (C) 1995 1996 Heiko Schlittermann <heiko@lotte.sax.de>
 * VERSION: 2.14(hs)
 *
 * ... anyway, I'm back again, thanks to Marcin, he adopted
 * large portions of my code (at least the parts containing
 * my main thoughts ...)
 *
 ****************** H E L P *********************************
 * If you ever plan to update your CD ROM drive and perhaps
 * want to sell or simply give away your Mitsumi FX-001[DS]
 * -- Please --
 * mail me (heiko@lotte.sax.de).  When my last drive goes
 * ballistic no more driver support will be available from me!
 *************************************************************
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Thanks to
 *  The Linux Community at all and ...
 *  Martin Harriss (he wrote the first Mitsumi Driver)
 *  Eberhard Moenkeberg (he gave me much support and the initial kick)
 *  Bernd Huebner, Ruediger Helsch (Unifix-Software GmbH, they
 *      improved the original driver)
 *  Jon Tombs, Bjorn Ekwall (module support)
 *  Daniel v. Mosnenck (he sent me the Technical and Programming Reference)
 *  Gerd Knorr (he lent me his PhotoCD)
 *  Nils Faerber and Roger E. Wolff (extensively tested the LU portion)
 *  Andreas Kies (testing the mysterious hang-ups)
 *  Heiko Eissfeldt (VERIFY_READ/WRITE)
 *  Marcin Dalecki (improved performance, shortened code)
 *  ... somebody forgotten?
 *
 *  9 November 1999 -- Make kernel-parameter implementation work with 2.3.x 
 *	               Removed init_module & cleanup_module in favor of 
 *		       module_init & module_exit.
 *		       Torben Mathiasen <tmm@image.dk>
 */


#ifdef RCS
static const char *mcdx_c_version
    = "$Id: mcdx.c,v 1.21 1997/01/26 07:12:59 davem Exp $";
#endif

#include <linux/module.h>

#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/cdrom.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <asm/io.h>
#include <asm/current.h>
#include <asm/uaccess.h>

#include <linux/major.h>
#define MAJOR_NR MITSUMI_X_CDROM_MAJOR
#include <linux/blkdev.h>
#include <linux/devfs_fs_kernel.h>

#include "mcdx.h"

#ifndef HZ
#error HZ not defined
#endif

#define xwarn(fmt, args...) printk(KERN_WARNING MCDX " " fmt, ## args)

#if !MCDX_QUIET
#define xinfo(fmt, args...) printk(KERN_INFO MCDX " " fmt, ## args)
#else
#define xinfo(fmt, args...) { ; }
#endif

#if MCDX_DEBUG
#define xtrace(lvl, fmt, args...) \
		{ if (lvl > 0) \
			{ printk(KERN_DEBUG MCDX ":: " fmt, ## args); } }
#define xdebug(fmt, args...) printk(KERN_DEBUG MCDX ":: " fmt, ## args)
#else
#define xtrace(lvl, fmt, args...) { ; }
#define xdebug(fmt, args...) { ; }
#endif

/* CONSTANTS *******************************************************/

/* Following are the number of sectors we _request_ from the drive
   every time an access outside the already requested range is done.
   The _direct_ size is the number of sectors we're allowed to skip
   directly (performing a read instead of requesting the new sector
   needed */
static const int REQUEST_SIZE = 800;	/* should be less then 255 * 4 */
static const int DIRECT_SIZE = 400;	/* should be less then REQUEST_SIZE */

enum drivemodes { TOC, DATA, RAW, COOKED };
enum datamodes { MODE0, MODE1, MODE2 };
enum resetmodes { SOFT, HARD };

static const int SINGLE = 0x01;		/* single speed drive (FX001S, LU) */
static const int DOUBLE = 0x02;		/* double speed drive (FX001D, ..? */
static const int DOOR = 0x04;		/* door locking capability */
static const int MULTI = 0x08;		/* multi session capability */

static const unsigned char READ1X = 0xc0;
static const unsigned char READ2X = 0xc1;


/* DECLARATIONS ****************************************************/
struct s_subqcode {
	unsigned char control;
	unsigned char tno;
	unsigned char index;
	struct cdrom_msf0 tt;
	struct cdrom_msf0 dt;
};

struct s_diskinfo {
	unsigned int n_first;
	unsigned int n_last;
	struct cdrom_msf0 msf_leadout;
	struct cdrom_msf0 msf_first;
};

struct s_multi {
	unsigned char multi;
	struct cdrom_msf0 msf_last;
};

struct s_version {
	unsigned char code;
	unsigned char ver;
};

/* Per drive/controller stuff **************************************/

struct s_drive_stuff {
	/* waitqueues */
	wait_queue_head_t busyq;
	wait_queue_head_t lockq;
	wait_queue_head_t sleepq;

	/* flags */
	volatile int introk;	/* status of last irq operation */
	volatile int busy;	/* drive performs an operation */
	volatile int lock;	/* exclusive usage */

	/* cd infos */
	struct s_diskinfo di;
	struct s_multi multi;
	struct s_subqcode *toc;	/* first entry of the toc array */
	struct s_subqcode start;
	struct s_subqcode stop;
	int xa;			/* 1 if xa disk */
	int audio;		/* 1 if audio disk */
	int audiostatus;

	/* `buffer' control */
	volatile int valid;	/* pending, ..., values are valid */
	volatile int pending;	/* next sector to be read */
	volatile int low_border;	/* first sector not to be skipped direct */
	volatile int high_border;	/* first sector `out of area' */
#ifdef AK2
	volatile int int_err;
#endif				/* AK2 */

	/* adds and odds */
	unsigned wreg_data;	/* w data */
	unsigned wreg_reset;	/* w hardware reset */
	unsigned wreg_hcon;	/* w hardware conf */
	unsigned wreg_chn;	/* w channel */
	unsigned rreg_data;	/* r data */
	unsigned rreg_status;	/* r status */

	int irq;		/* irq used by this drive */
	int present;		/* drive present and its capabilities */
	unsigned char readcmd;	/* read cmd depends on single/double speed */
	unsigned char playcmd;	/* play should always be single speed */
	unsigned int xxx;	/* set if changed, reset while open */
	unsigned int yyy;	/* set if changed, reset by media_changed */
	int users;		/* keeps track of open/close */
	int lastsector;		/* last block accessible */
	int status;		/* last operation's error / status */
	int readerrs;		/* # of blocks read w/o error */
	struct cdrom_device_info info;
	struct gendisk *disk;
};


/* Prototypes ******************************************************/

/*	The following prototypes are already declared elsewhere.  They are
 	repeated here to show what's going on.  And to sense, if they're
	changed elsewhere. */

static int mcdx_init(void);

static int mcdx_block_open(struct inode *inode, struct file *file)
{
	struct s_drive_stuff *p = inode->i_bdev->bd_disk->private_data;
	return cdrom_open(&p->info, inode, file);
}

static int mcdx_block_release(struct inode *inode, struct file *file)
{
	struct s_drive_stuff *p = inode->i_bdev->bd_disk->private_data;
	return cdrom_release(&p->info, file);
}

static int mcdx_block_ioctl(struct inode *inode, struct file *file,
				unsigned cmd, unsigned long arg)
{
	struct s_drive_stuff *p = inode->i_bdev->bd_disk->private_data;
	return cdrom_ioctl(file, &p->info, inode, cmd, arg);
}

static int mcdx_block_media_changed(struct gendisk *disk)
{
	struct s_drive_stuff *p = disk->private_data;
	return cdrom_media_changed(&p->info);
}

static struct block_device_operations mcdx_bdops =
{
	.owner		= THIS_MODULE,
	.open		= mcdx_block_open,
	.release	= mcdx_block_release,
	.ioctl		= mcdx_block_ioctl,
	.media_changed	= mcdx_block_media_changed,
};


/*	Indirect exported functions. These functions are exported by their
	addresses, such as mcdx_open and mcdx_close in the
	structure mcdx_dops. */

/* exported by file_ops */
static int mcdx_open(struct cdrom_device_info *cdi, int purpose);
static void mcdx_close(struct cdrom_device_info *cdi);
static int mcdx_media_changed(struct cdrom_device_info *cdi, int disc_nr);
static int mcdx_tray_move(struct cdrom_device_info *cdi, int position);
static int mcdx_lockdoor(struct cdrom_device_info *cdi, int lock);
static int mcdx_audio_ioctl(struct cdrom_device_info *cdi,
			    unsigned int cmd, void *arg);

/* misc internal support functions */
static void log2msf(unsigned int, struct cdrom_msf0 *);
static unsigned int msf2log(const struct cdrom_msf0 *);
static unsigned int uint2bcd(unsigned int);
static unsigned int bcd2uint(unsigned char);
static unsigned port(int *);
static int irq(int *);
static void mcdx_delay(struct s_drive_stuff *, long jifs);
static int mcdx_transfer(struct s_drive_stuff *, char *buf, int sector,
			 int nr_sectors);
static int mcdx_xfer(struct s_drive_stuff *, char *buf, int sector,
		     int nr_sectors);

static int mcdx_config(struct s_drive_stuff *, int);
static int mcdx_requestversion(struct s_drive_stuff *, struct s_version *,
			       int);
static int mcdx_stop(struct s_drive_stuff *, int);
static int mcdx_hold(struct s_drive_stuff *, int);
static int mcdx_reset(struct s_drive_stuff *, enum resetmodes, int);
static int mcdx_setdrivemode(struct s_drive_stuff *, enum drivemodes, int);
static int mcdx_setdatamode(struct s_drive_stuff *, enum datamodes, int);
static int mcdx_requestsubqcode(struct s_drive_stuff *,
				struct s_subqcode *, int);
static int mcdx_requestmultidiskinfo(struct s_drive_stuff *,
				     struct s_multi *, int);
static int mcdx_requesttocdata(struct s_drive_stuff *, struct s_diskinfo *,
			       int);
static int mcdx_getstatus(struct s_drive_stuff *, int);
static int mcdx_getval(struct s_drive_stuff *, int to, int delay, char *);
static int mcdx_talk(struct s_drive_stuff *,
		     const unsigned char *cmd, size_t,
		     void *buffer, size_t size, unsigned int timeout, int);
static int mcdx_readtoc(struct s_drive_stuff *);
static int mcdx_playtrk(struct s_drive_stuff *, const struct cdrom_ti *);
static int mcdx_playmsf(struct s_drive_stuff *, const struct cdrom_msf *);
static int mcdx_setattentuator(struct s_drive_stuff *,
			       struct cdrom_volctrl *, int);

/* static variables ************************************************/

static int mcdx_drive_map[][2] = MCDX_DRIVEMAP;
static struct s_drive_stuff *mcdx_stuffp[MCDX_NDRIVES];
static DEFINE_SPINLOCK(mcdx_lock);
static struct request_queue *mcdx_queue;

/* You can only set the first two pairs, from old MODULE_PARM code.  */
static int mcdx_set(const char *val, struct kernel_param *kp)
{
	get_options((char *)val, 4, (int *)mcdx_drive_map);
	return 0;
}
module_param_call(mcdx, mcdx_set, NULL, NULL, 0);

static struct cdrom_device_ops mcdx_dops = {
	.open		= mcdx_open,
	.release	= mcdx_close,
	.media_changed	= mcdx_media_changed,
	.tray_move	= mcdx_tray_move,
	.lock_door	= mcdx_lockdoor,
	.audio_ioctl	= mcdx_audio_ioctl,
	.capability	= CDC_OPEN_TRAY | CDC_LOCK | CDC_MEDIA_CHANGED |
			  CDC_PLAY_AUDIO | CDC_DRIVE_STATUS,
};

/* KERNEL INTERFACE FUNCTIONS **************************************/


static int mcdx_audio_ioctl(struct cdrom_device_info *cdi,
			    unsigned int cmd, void *arg)
{
	struct s_drive_stuff *stuffp = cdi->handle;

	if (!stuffp->present)
		return -ENXIO;

	if (stuffp->xxx) {
		if (-1 == mcdx_requesttocdata(stuffp, &stuffp->di, 1)) {
			stuffp->lastsector = -1;
		} else {
			stuffp->lastsector = (CD_FRAMESIZE / 512)
			    * msf2log(&stuffp->di.msf_leadout) - 1;
		}

		if (stuffp->toc) {
			kfree(stuffp->toc);
			stuffp->toc = NULL;
			if (-1 == mcdx_readtoc(stuffp))
				return -1;
		}

		stuffp->xxx = 0;
	}

	switch (cmd) {
	case CDROMSTART:{
			xtrace(IOCTL, "ioctl() START\n");
			/* Spin up the drive.  Don't think we can do this.
			   * For now, ignore it.
			 */
			return 0;
		}

	case CDROMSTOP:{
			xtrace(IOCTL, "ioctl() STOP\n");
			stuffp->audiostatus = CDROM_AUDIO_INVALID;
			if (-1 == mcdx_stop(stuffp, 1))
				return -EIO;
			return 0;
		}

	case CDROMPLAYTRKIND:{
			struct cdrom_ti *ti = (struct cdrom_ti *) arg;

			xtrace(IOCTL, "ioctl() PLAYTRKIND\n");
			if ((ti->cdti_trk0 < stuffp->di.n_first)
			    || (ti->cdti_trk0 > stuffp->di.n_last)
			    || (ti->cdti_trk1 < stuffp->di.n_first))
				return -EINVAL;
			if (ti->cdti_trk1 > stuffp->di.n_last)
				ti->cdti_trk1 = stuffp->di.n_last;
			xtrace(PLAYTRK, "ioctl() track %d to %d\n",
			       ti->cdti_trk0, ti->cdti_trk1);
			return mcdx_playtrk(stuffp, ti);
		}

	case CDROMPLAYMSF:{
			struct cdrom_msf *msf = (struct cdrom_msf *) arg;

			xtrace(IOCTL, "ioctl() PLAYMSF\n");

			if ((stuffp->audiostatus == CDROM_AUDIO_PLAY)
			    && (-1 == mcdx_hold(stuffp, 1)))
				return -EIO;

			msf->cdmsf_min0 = uint2bcd(msf->cdmsf_min0);
			msf->cdmsf_sec0 = uint2bcd(msf->cdmsf_sec0);
			msf->cdmsf_frame0 = uint2bcd(msf->cdmsf_frame0);

			msf->cdmsf_min1 = uint2bcd(msf->cdmsf_min1);
			msf->cdmsf_sec1 = uint2bcd(msf->cdmsf_sec1);
			msf->cdmsf_frame1 = uint2bcd(msf->cdmsf_frame1);

			stuffp->stop.dt.minute = msf->cdmsf_min1;
			stuffp->stop.dt.second = msf->cdmsf_sec1;
			stuffp->stop.dt.frame = msf->cdmsf_frame1;

			return mcdx_playmsf(stuffp, msf);
		}

	case CDROMRESUME:{
			xtrace(IOCTL, "ioctl() RESUME\n");
			return mcdx_playtrk(stuffp, NULL);
		}

	case CDROMREADTOCENTRY:{
			struct cdrom_tocentry *entry =
			    (struct cdrom_tocentry *) arg;
			struct s_subqcode *tp = NULL;
			xtrace(IOCTL, "ioctl() READTOCENTRY\n");

			if (-1 == mcdx_readtoc(stuffp))
				return -1;
			if (entry->cdte_track == CDROM_LEADOUT)
				tp = &stuffp->toc[stuffp->di.n_last -
						  stuffp->di.n_first + 1];
			else if (entry->cdte_track > stuffp->di.n_last
				 || entry->cdte_track < stuffp->di.n_first)
				return -EINVAL;
			else
				tp = &stuffp->toc[entry->cdte_track -
						  stuffp->di.n_first];

			if (NULL == tp)
				return -EIO;
			entry->cdte_adr = tp->control;
			entry->cdte_ctrl = tp->control >> 4;
			/* Always return stuff in MSF, and let the Uniform cdrom driver
			   worry about what the user actually wants */
			entry->cdte_addr.msf.minute =
			    bcd2uint(tp->dt.minute);
			entry->cdte_addr.msf.second =
			    bcd2uint(tp->dt.second);
			entry->cdte_addr.msf.frame =
			    bcd2uint(tp->dt.frame);
			return 0;
		}

	case CDROMSUBCHNL:{
			struct cdrom_subchnl *sub =
			    (struct cdrom_subchnl *) arg;
			struct s_subqcode q;

			xtrace(IOCTL, "ioctl() SUBCHNL\n");

			if (-1 == mcdx_requestsubqcode(stuffp, &q, 2))
				return -EIO;

			xtrace(SUBCHNL, "audiostatus: %x\n",
			       stuffp->audiostatus);
			sub->cdsc_audiostatus = stuffp->audiostatus;
			sub->cdsc_adr = q.control;
			sub->cdsc_ctrl = q.control >> 4;
			sub->cdsc_trk = bcd2uint(q.tno);
			sub->cdsc_ind = bcd2uint(q.index);

			xtrace(SUBCHNL, "trk %d, ind %d\n",
			       sub->cdsc_trk, sub->cdsc_ind);
			/* Always return stuff in MSF, and let the Uniform cdrom driver
			   worry about what the user actually wants */
			sub->cdsc_absaddr.msf.minute =
			    bcd2uint(q.dt.minute);
			sub->cdsc_absaddr.msf.second =
			    bcd2uint(q.dt.second);
			sub->cdsc_absaddr.msf.frame = bcd2uint(q.dt.frame);
			sub->cdsc_reladdr.msf.minute =
			    bcd2uint(q.tt.minute);
			sub->cdsc_reladdr.msf.second =
			    bcd2uint(q.tt.second);
			sub->cdsc_reladdr.msf.frame = bcd2uint(q.tt.frame);
			xtrace(SUBCHNL,
			       "msf: abs %02d:%02d:%02d, rel %02d:%02d:%02d\n",
			       sub->cdsc_absaddr.msf.minute,
			       sub->cdsc_absaddr.msf.second,
			       sub->cdsc_absaddr.msf.frame,
			       sub->cdsc_reladdr.msf.minute,
			       sub->cdsc_reladdr.msf.second,
			       sub->cdsc_reladdr.msf.frame);

			return 0;
		}

	case CDROMREADTOCHDR:{
			struct cdrom_tochdr *toc =
			    (struct cdrom_tochdr *) arg;

			xtrace(IOCTL, "ioctl() READTOCHDR\n");
			toc->cdth_trk0 = stuffp->di.n_first;
			toc->cdth_trk1 = stuffp->di.n_last;
			xtrace(TOCHDR,
			       "ioctl() track0 = %d, track1 = %d\n",
			       stuffp->di.n_first, stuffp->di.n_last);
			return 0;
		}

	case CDROMPAUSE:{
			xtrace(IOCTL, "ioctl() PAUSE\n");
			if (stuffp->audiostatus != CDROM_AUDIO_PLAY)
				return -EINVAL;
			if (-1 == mcdx_stop(stuffp, 1))
				return -EIO;
			stuffp->audiostatus = CDROM_AUDIO_PAUSED;
			if (-1 ==
			    mcdx_requestsubqcode(stuffp, &stuffp->start,
						 1))
				return -EIO;
			return 0;
		}

	case CDROMMULTISESSION:{
			struct cdrom_multisession *ms =
			    (struct cdrom_multisession *) arg;
			xtrace(IOCTL, "ioctl() MULTISESSION\n");
			/* Always return stuff in LBA, and let the Uniform cdrom driver
			   worry about what the user actually wants */
			ms->addr.lba = msf2log(&stuffp->multi.msf_last);
			ms->xa_flag = !!stuffp->multi.multi;
			xtrace(MS,
			       "ioctl() (%d, 0x%08x [%02x:%02x.%02x])\n",
			       ms->xa_flag, ms->addr.lba,
			       stuffp->multi.msf_last.minute,
			       stuffp->multi.msf_last.second,
			       stuffp->multi.msf_last.frame);

			return 0;
		}

	case CDROMEJECT:{
			xtrace(IOCTL, "ioctl() EJECT\n");
			if (stuffp->users > 1)
				return -EBUSY;
			return (mcdx_tray_move(cdi, 1));
		}

	case CDROMCLOSETRAY:{
			xtrace(IOCTL, "ioctl() CDROMCLOSETRAY\n");
			return (mcdx_tray_move(cdi, 0));
		}

	case CDROMVOLCTRL:{
			struct cdrom_volctrl *volctrl =
			    (struct cdrom_volctrl *) arg;
			xtrace(IOCTL, "ioctl() VOLCTRL\n");

#if 0				/* not tested! */
			/* adjust for the weirdness of workman (md) */
			/* can't test it (hs) */
			volctrl.channel2 = volctrl.channel1;
			volctrl.channel1 = volctrl.channel3 = 0x00;
#endif
			return mcdx_setattentuator(stuffp, volctrl, 2);
		}

	default:
		return -EINVAL;
	}
}

static void do_mcdx_request(request_queue_t * q)
{
	struct s_drive_stuff *stuffp;
	struct request *req;

      again:

	req = elv_next_request(q);
	if (!req)
		return;

	stuffp = req->rq_disk->private_data;

	if (!stuffp->present) {
		xwarn("do_request(): bad device: %s\n",req->rq_disk->disk_name);
		xtrace(REQUEST, "end_request(0): bad device\n");
		end_request(req, 0);
		return;
	}

	if (stuffp->audio) {
		xwarn("do_request() attempt to read from audio cd\n");
		xtrace(REQUEST, "end_request(0): read from audio\n");
		end_request(req, 0);
		return;
	}

	xtrace(REQUEST, "do_request() (%lu + %lu)\n",
	       req->sector, req->nr_sectors);

	if (req->cmd != READ) {
		xwarn("do_request(): non-read command to cd!!\n");
		xtrace(REQUEST, "end_request(0): write\n");
		end_request(req, 0);
		return;
	}
	else {
		stuffp->status = 0;
		while (req->nr_sectors) {
			int i;

			i = mcdx_transfer(stuffp,
					  req->buffer,
					  req->sector,
					  req->nr_sectors);

			if (i == -1) {
				end_request(req, 0);
				goto again;
			}
			req->sector += i;
			req->nr_sectors -= i;
			req->buffer += (i * 512);
		}
		end_request(req, 1);
		goto again;

		xtrace(REQUEST, "end_request(1)\n");
		end_request(req, 1);
	}

	goto again;
}

static int mcdx_open(struct cdrom_device_info *cdi, int purpose)
{
	struct s_drive_stuff *stuffp;
	xtrace(OPENCLOSE, "open()\n");
	stuffp = cdi->handle;
	if (!stuffp->present)
		return -ENXIO;

	/* Make the modules looking used ... (thanx bjorn).
	 * But we shouldn't forget to decrement the module counter
	 * on error return */

	/* this is only done to test if the drive talks with us */
	if (-1 == mcdx_getstatus(stuffp, 1))
		return -EIO;

	if (stuffp->xxx) {

		xtrace(OPENCLOSE, "open() media changed\n");
		stuffp->audiostatus = CDROM_AUDIO_INVALID;
		stuffp->readcmd = 0;
		xtrace(OPENCLOSE, "open() Request multisession info\n");
		if (-1 ==
		    mcdx_requestmultidiskinfo(stuffp, &stuffp->multi, 6))
			xinfo("No multidiskinfo\n");
	} else {
		/* multisession ? */
		if (!stuffp->multi.multi)
			stuffp->multi.msf_last.second = 2;

		xtrace(OPENCLOSE, "open() MS: %d, last @ %02x:%02x.%02x\n",
		       stuffp->multi.multi,
		       stuffp->multi.msf_last.minute,
		       stuffp->multi.msf_last.second,
		       stuffp->multi.msf_last.frame);

		{;
		}		/* got multisession information */
		/* request the disks table of contents (aka diskinfo) */
		if (-1 == mcdx_requesttocdata(stuffp, &stuffp->di, 1)) {

			stuffp->lastsector = -1;

		} else {

			stuffp->lastsector = (CD_FRAMESIZE / 512)
			    * msf2log(&stuffp->di.msf_leadout) - 1;

			xtrace(OPENCLOSE,
			       "open() start %d (%02x:%02x.%02x) %d\n",
			       stuffp->di.n_first,
			       stuffp->di.msf_first.minute,
			       stuffp->di.msf_first.second,
			       stuffp->di.msf_first.frame,
			       msf2log(&stuffp->di.msf_first));
			xtrace(OPENCLOSE,
			       "open() last %d (%02x:%02x.%02x) %d\n",
			       stuffp->di.n_last,
			       stuffp->di.msf_leadout.minute,
			       stuffp->di.msf_leadout.second,
			       stuffp->di.msf_leadout.frame,
			       msf2log(&stuffp->di.msf_leadout));
		}

		if (stuffp->toc) {
			xtrace(MALLOC, "open() free old toc @ %p\n",
			       stuffp->toc);
			kfree(stuffp->toc);

			stuffp->toc = NULL;
		}

		xtrace(OPENCLOSE, "open() init irq generation\n");
		if (-1 == mcdx_config(stuffp, 1))
			return -EIO;
#ifdef FALLBACK
		/* Set the read speed */
		xwarn("AAA %x AAA\n", stuffp->readcmd);
		if (stuffp->readerrs)
			stuffp->readcmd = READ1X;
		else
			stuffp->readcmd =
			    stuffp->present | SINGLE ? READ1X : READ2X;
		xwarn("XXX %x XXX\n", stuffp->readcmd);
#else
		stuffp->readcmd =
		    stuffp->present | SINGLE ? READ1X : READ2X;
#endif

		/* try to get the first sector, iff any ... */
		if (stuffp->lastsector >= 0) {
			char buf[512];
			int ans;
			int tries;

			stuffp->xa = 0;
			stuffp->audio = 0;

			for (tries = 6; tries; tries--) {

				stuffp->introk = 1;

				xtrace(OPENCLOSE, "open() try as %s\n",
				       stuffp->xa ? "XA" : "normal");
				/* set data mode */
				if (-1 == (ans = mcdx_setdatamode(stuffp,
								  stuffp->
								  xa ?
								  MODE2 :
								  MODE1,
								  1))) {
					/* return -EIO; */
					stuffp->xa = 0;
					break;
				}

				if ((stuffp->audio = e_audio(ans)))
					break;

				while (0 ==
				       (ans =
					mcdx_transfer(stuffp, buf, 0, 1)));

				if (ans == 1)
					break;
				stuffp->xa = !stuffp->xa;
			}
		}
		/* xa disks will be read in raw mode, others not */
		if (-1 == mcdx_setdrivemode(stuffp,
					    stuffp->xa ? RAW : COOKED,
					    1))
			return -EIO;
		if (stuffp->audio) {
			xinfo("open() audio disk found\n");
		} else if (stuffp->lastsector >= 0) {
			xinfo("open() %s%s disk found\n",
			      stuffp->xa ? "XA / " : "",
			      stuffp->multi.
			      multi ? "Multi Session" : "Single Session");
		}
	}
	stuffp->xxx = 0;
	stuffp->users++;
	return 0;
}

static void mcdx_close(struct cdrom_device_info *cdi)
{
	struct s_drive_stuff *stuffp;

	xtrace(OPENCLOSE, "close()\n");

	stuffp = cdi->handle;

	--stuffp->users;
}

static int mcdx_media_changed(struct cdrom_device_info *cdi, int disc_nr)
/*	Return: 1 if media changed since last call to this function
			0 otherwise */
{
	struct s_drive_stuff *stuffp;

	xinfo("mcdx_media_changed called for device %s\n", cdi->name);

	stuffp = cdi->handle;
	mcdx_getstatus(stuffp, 1);

	if (stuffp->yyy == 0)
		return 0;

	stuffp->yyy = 0;
	return 1;
}

#ifndef MODULE
static int __init mcdx_setup(char *str)
{
	int pi[4];
	(void) get_options(str, ARRAY_SIZE(pi), pi);

	if (pi[0] > 0)
		mcdx_drive_map[0][0] = pi[1];
	if (pi[0] > 1)
		mcdx_drive_map[0][1] = pi[2];
	return 1;
}

__setup("mcdx=", mcdx_setup);

#endif

/* DIRTY PART ******************************************************/

static void mcdx_delay(struct s_drive_stuff *stuff, long jifs)
/* This routine is used for sleeping.
 * A jifs value <0 means NO sleeping,
 *              =0 means minimal sleeping (let the kernel
 *                 run for other processes)
 *              >0 means at least sleep for that amount.
 *	May be we could use a simple count loop w/ jumps to itself, but
 *	I wanna make this independent of cpu speed. [1 jiffy is 1/HZ] sec */
{
	if (jifs < 0)
		return;

	xtrace(SLEEP, "*** delay: sleepq\n");
	interruptible_sleep_on_timeout(&stuff->sleepq, jifs);
	xtrace(SLEEP, "delay awoken\n");
	if (signal_pending(current)) {
		xtrace(SLEEP, "got signal\n");
	}
}

static irqreturn_t mcdx_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct s_drive_stuff *stuffp = dev_id;
	unsigned char b;

	if (stuffp == NULL) {
		xwarn("mcdx: no device for intr %d\n", irq);
		return IRQ_NONE;
	}
#ifdef AK2
	if (!stuffp->busy && stuffp->pending)
		stuffp->int_err = 1;

#endif				/* AK2 */
	/* get the interrupt status */
	b = inb(stuffp->rreg_status);
	stuffp->introk = ~b & MCDX_RBIT_DTEN;

	/* NOTE: We only should get interrupts if the data we
	 * requested are ready to transfer.
	 * But the drive seems to generate ``asynchronous'' interrupts
	 * on several error conditions too.  (Despite the err int enable
	 * setting during initialisation) */

	/* if not ok, read the next byte as the drives status */
	if (!stuffp->introk) {
		xtrace(IRQ, "intr() irq %d hw status 0x%02x\n", irq, b);
		if (~b & MCDX_RBIT_STEN) {
			xinfo("intr() irq %d    status 0x%02x\n",
			      irq, inb(stuffp->rreg_data));
		} else {
			xinfo("intr() irq %d ambiguous hw status\n", irq);
		}
	} else {
		xtrace(IRQ, "irq() irq %d ok, status %02x\n", irq, b);
	}

	stuffp->busy = 0;
	wake_up_interruptible(&stuffp->busyq);
	return IRQ_HANDLED;
}


static int mcdx_talk(struct s_drive_stuff *stuffp,
	  const unsigned char *cmd, size_t cmdlen,
	  void *buffer, size_t size, unsigned int timeout, int tries)
/* Send a command to the drive, wait for the result.
 * returns -1 on timeout, drive status otherwise
 * If buffer is not zero, the result (length size) is stored there.
 * If buffer is zero the size should be the number of bytes to read
 * from the drive.  These bytes are discarded.
 */
{
	int st;
	char c;
	int discard;

	/* Somebody wants the data read? */
	if ((discard = (buffer == NULL)))
		buffer = &c;

	while (stuffp->lock) {
		xtrace(SLEEP, "*** talk: lockq\n");
		interruptible_sleep_on(&stuffp->lockq);
		xtrace(SLEEP, "talk: awoken\n");
	}

	stuffp->lock = 1;

	/* An operation other then reading data destroys the
	   * data already requested and remembered in stuffp->request, ... */
	stuffp->valid = 0;

#if MCDX_DEBUG & TALK
	{
		unsigned char i;
		xtrace(TALK,
		       "talk() %d / %d tries, res.size %d, command 0x%02x",
		       tries, timeout, size, (unsigned char) cmd[0]);
		for (i = 1; i < cmdlen; i++)
			xtrace(TALK, " 0x%02x", cmd[i]);
		xtrace(TALK, "\n");
	}
#endif

	/*  give up if all tries are done (bad) or if the status
	 *  st != -1 (good) */
	for (st = -1; st == -1 && tries; tries--) {

		char *bp = (char *) buffer;
		size_t sz = size;

		outsb(stuffp->wreg_data, cmd, cmdlen);
		xtrace(TALK, "talk() command sent\n");

		/* get the status byte */
		if (-1 == mcdx_getval(stuffp, timeout, 0, bp)) {
			xinfo("talk() %02x timed out (status), %d tr%s left\n",
			     cmd[0], tries - 1, tries == 2 ? "y" : "ies");
			continue;
		}
		st = *bp;
		sz--;
		if (!discard)
			bp++;

		xtrace(TALK, "talk() got status 0x%02x\n", st);

		/* command error? */
		if (e_cmderr(st)) {
			xwarn("command error cmd = %02x %s \n",
			      cmd[0], cmdlen > 1 ? "..." : "");
			st = -1;
			continue;
		}

		/* audio status? */
		if (stuffp->audiostatus == CDROM_AUDIO_INVALID)
			stuffp->audiostatus =
			    e_audiobusy(st) ? CDROM_AUDIO_PLAY :
			    CDROM_AUDIO_NO_STATUS;
		else if (stuffp->audiostatus == CDROM_AUDIO_PLAY
			 && e_audiobusy(st) == 0)
			stuffp->audiostatus = CDROM_AUDIO_COMPLETED;

		/* media change? */
		if (e_changed(st)) {
			xinfo("talk() media changed\n");
			stuffp->xxx = stuffp->yyy = 1;
		}

		/* now actually get the data */
		while (sz--) {
			if (-1 == mcdx_getval(stuffp, timeout, 0, bp)) {
				xinfo("talk() %02x timed out (data), %d tr%s left\n",
				     cmd[0], tries - 1,
				     tries == 2 ? "y" : "ies");
				st = -1;
				break;
			}
			if (!discard)
				bp++;
			xtrace(TALK, "talk() got 0x%02x\n", *(bp - 1));
		}
	}

#if !MCDX_QUIET
	if (!tries && st == -1)
		xinfo("talk() giving up\n");
#endif

	stuffp->lock = 0;
	wake_up_interruptible(&stuffp->lockq);

	xtrace(TALK, "talk() done with 0x%02x\n", st);
	return st;
}

/* MODULE STUFF ***********************************************************/

int __mcdx_init(void)
{
	int i;
	int drives = 0;

	mcdx_init();
	for (i = 0; i < MCDX_NDRIVES; i++) {
		if (mcdx_stuffp[i]) {
			xtrace(INIT, "init_module() drive %d stuff @ %p\n",
			       i, mcdx_stuffp[i]);
			drives++;
		}
	}

	if (!drives)
		return -EIO;

	return 0;
}

static void __exit mcdx_exit(void)
{
	int i;

	xinfo("cleanup_module called\n");

	for (i = 0; i < MCDX_NDRIVES; i++) {
		struct s_drive_stuff *stuffp = mcdx_stuffp[i];
		if (!stuffp)
			continue;
		del_gendisk(stuffp->disk);
		if (unregister_cdrom(&stuffp->info)) {
			printk(KERN_WARNING "Can't unregister cdrom mcdx\n");
			continue;
		}
		put_disk(stuffp->disk);
		release_region(stuffp->wreg_data, MCDX_IO_SIZE);
		free_irq(stuffp->irq, NULL);
		if (stuffp->toc) {
			xtrace(MALLOC, "cleanup_module() free toc @ %p\n",
			       stuffp->toc);
			kfree(stuffp->toc);
		}
		xtrace(MALLOC, "cleanup_module() free stuffp @ %p\n",
		       stuffp);
		mcdx_stuffp[i] = NULL;
		kfree(stuffp);
	}

	if (unregister_blkdev(MAJOR_NR, "mcdx") != 0) {
		xwarn("cleanup() unregister_blkdev() failed\n");
	}
	blk_cleanup_queue(mcdx_queue);
#if !MCDX_QUIET
	else
	xinfo("cleanup() succeeded\n");
#endif
}

#ifdef MODULE
module_init(__mcdx_init);
#endif
module_exit(mcdx_exit);


/* Support functions ************************************************/

static int __init mcdx_init_drive(int drive)
{
	struct s_version version;
	struct gendisk *disk;
	struct s_drive_stuff *stuffp;
	int size = sizeof(*stuffp);
	char msg[80];

	xtrace(INIT, "init() try drive %d\n", drive);

	xtrace(INIT, "kmalloc space for stuffpt's\n");
	xtrace(MALLOC, "init() malloc %d bytes\n", size);
	if (!(stuffp = kzalloc(size, GFP_KERNEL))) {
		xwarn("init() malloc failed\n");
		return 1;
	}

	disk = alloc_disk(1);
	if (!disk) {
		xwarn("init() malloc failed\n");
		kfree(stuffp);
		return 1;
	}

	xtrace(INIT, "init() got %d bytes for drive stuff @ %p\n",
	       sizeof(*stuffp), stuffp);

	/* set default values */
	stuffp->present = 0;	/* this should be 0 already */
	stuffp->toc = NULL;	/* this should be NULL already */

	/* setup our irq and i/o addresses */
	stuffp->irq = irq(mcdx_drive_map[drive]);
	stuffp->wreg_data = stuffp->rreg_data = port(mcdx_drive_map[drive]);
	stuffp->wreg_reset = stuffp->rreg_status = stuffp->wreg_data + 1;
	stuffp->wreg_hcon = stuffp->wreg_reset + 1;
	stuffp->wreg_chn = stuffp->wreg_hcon + 1;

	init_waitqueue_head(&stuffp->busyq);
	init_waitqueue_head(&stuffp->lockq);
	init_waitqueue_head(&stuffp->sleepq);

	/* check if i/o addresses are available */
	if (!request_region(stuffp->wreg_data, MCDX_IO_SIZE, "mcdx")) {
		xwarn("0x%03x,%d: Init failed. "
		      "I/O ports (0x%03x..0x%03x) already in use.\n",
		      stuffp->wreg_data, stuffp->irq,
		      stuffp->wreg_data,
		      stuffp->wreg_data + MCDX_IO_SIZE - 1);
		xtrace(MALLOC, "init() free stuffp @ %p\n", stuffp);
		kfree(stuffp);
		put_disk(disk);
		xtrace(INIT, "init() continue at next drive\n");
		return 0;	/* next drive */
	}

	xtrace(INIT, "init() i/o port is available at 0x%03x\n"
	       stuffp->wreg_data);
	xtrace(INIT, "init() hardware reset\n");
	mcdx_reset(stuffp, HARD, 1);

	xtrace(INIT, "init() get version\n");
	if (-1 == mcdx_requestversion(stuffp, &version, 4)) {
		/* failed, next drive */
		release_region(stuffp->wreg_data, MCDX_IO_SIZE);
		xwarn("%s=0x%03x,%d: Init failed. Can't get version.\n",
		      MCDX, stuffp->wreg_data, stuffp->irq);
		xtrace(MALLOC, "init() free stuffp @ %p\n", stuffp);
		kfree(stuffp);
		put_disk(disk);
		xtrace(INIT, "init() continue at next drive\n");
		return 0;
	}

	switch (version.code) {
	case 'D':
		stuffp->readcmd = READ2X;
		stuffp->present = DOUBLE | DOOR | MULTI;
		break;
	case 'F':
		stuffp->readcmd = READ1X;
		stuffp->present = SINGLE | DOOR | MULTI;
		break;
	case 'M':
		stuffp->readcmd = READ1X;
		stuffp->present = SINGLE;
		break;
	default:
		stuffp->present = 0;
		break;
	}

	stuffp->playcmd = READ1X;

	if (!stuffp->present) {
		release_region(stuffp->wreg_data, MCDX_IO_SIZE);
		xwarn("%s=0x%03x,%d: Init failed. No Mitsumi CD-ROM?.\n",
		      MCDX, stuffp->wreg_data, stuffp->irq);
		kfree(stuffp);
		put_disk(disk);
		return 0;	/* next drive */
	}

	xtrace(INIT, "init() register blkdev\n");
	if (register_blkdev(MAJOR_NR, "mcdx")) {
		release_region(stuffp->wreg_data, MCDX_IO_SIZE);
		kfree(stuffp);
		put_disk(disk);
		return 1;
	}

	mcdx_queue = blk_init_queue(do_mcdx_request, &mcdx_lock);
	if (!mcdx_queue) {
		unregister_blkdev(MAJOR_NR, "mcdx");
		release_region(stuffp->wreg_data, MCDX_IO_SIZE);
		kfree(stuffp);
		put_disk(disk);
		return 1;
	}

	xtrace(INIT, "init() subscribe irq and i/o\n");
	if (request_irq(stuffp->irq, mcdx_intr, SA_INTERRUPT, "mcdx", stuffp)) {
		release_region(stuffp->wreg_data, MCDX_IO_SIZE);
		xwarn("%s=0x%03x,%d: Init failed. Can't get irq (%d).\n",
		      MCDX, stuffp->wreg_data, stuffp->irq, stuffp->irq);
		stuffp->irq = 0;
		blk_cleanup_queue(mcdx_queue);
		kfree(stuffp);
		put_disk(disk);
		return 0;
	}

	xtrace(INIT, "init() get garbage\n");
	{
		int i;
		mcdx_delay(stuffp, HZ / 2);
		for (i = 100; i; i--)
			(void) inb(stuffp->rreg_status);
	}


#ifdef WE_KNOW_WHY
	/* irq 11 -> channel register */
	outb(0x50, stuffp->wreg_chn);
#endif

	xtrace(INIT, "init() set non dma but irq mode\n");
	mcdx_config(stuffp, 1);

	stuffp->info.ops = &mcdx_dops;
	stuffp->info.speed = 2;
	stuffp->info.capacity = 1;
	stuffp->info.handle = stuffp;
	sprintf(stuffp->info.name, "mcdx%d", drive);
	disk->major = MAJOR_NR;
	disk->first_minor = drive;
	strcpy(disk->disk_name, stuffp->info.name);
	disk->fops = &mcdx_bdops;
	disk->flags = GENHD_FL_CD;
	stuffp->disk = disk;

	sprintf(msg, " mcdx: Mitsumi CD-ROM installed at 0x%03x, irq %d."
		" (Firmware version %c %x)\n",
		stuffp->wreg_data, stuffp->irq, version.code, version.ver);
	mcdx_stuffp[drive] = stuffp;
	xtrace(INIT, "init() mcdx_stuffp[%d] = %p\n", drive, stuffp);
	if (register_cdrom(&stuffp->info) != 0) {
		printk("Cannot register Mitsumi CD-ROM!\n");
		free_irq(stuffp->irq, NULL);
		release_region(stuffp->wreg_data, MCDX_IO_SIZE);
		kfree(stuffp);
		put_disk(disk);
		if (unregister_blkdev(MAJOR_NR, "mcdx") != 0)
			xwarn("cleanup() unregister_blkdev() failed\n");
		blk_cleanup_queue(mcdx_queue);
		return 2;
	}
	disk->private_data = stuffp;
	disk->queue = mcdx_queue;
	add_disk(disk);
	printk(msg);
	return 0;
}

static int __init mcdx_init(void)
{
	int drive;
	xwarn("Version 2.14(hs) \n");

	xwarn("$Id: mcdx.c,v 1.21 1997/01/26 07:12:59 davem Exp $\n");

	/* zero the pointer array */
	for (drive = 0; drive < MCDX_NDRIVES; drive++)
		mcdx_stuffp[drive] = NULL;

	/* do the initialisation */
	for (drive = 0; drive < MCDX_NDRIVES; drive++) {
		switch (mcdx_init_drive(drive)) {
		case 2:
			return -EIO;
		case 1:
			break;
		}
	}
	return 0;
}

static int mcdx_transfer(struct s_drive_stuff *stuffp,
	      char *p, int sector, int nr_sectors)
/*	This seems to do the actually transfer.  But it does more.  It
	keeps track of errors occurred and will (if possible) fall back
	to single speed on error.
	Return:	-1 on timeout or other error
			else status byte (as in stuff->st) */
{
	int ans;

	ans = mcdx_xfer(stuffp, p, sector, nr_sectors);
	return ans;
#ifdef FALLBACK
	if (-1 == ans)
		stuffp->readerrs++;
	else
		return ans;

	if (stuffp->readerrs && stuffp->readcmd == READ1X) {
		xwarn("XXX Already reading 1x -- no chance\n");
		return -1;
	}

	xwarn("XXX Fallback to 1x\n");

	stuffp->readcmd = READ1X;
	return mcdx_transfer(stuffp, p, sector, nr_sectors);
#endif

}


static int mcdx_xfer(struct s_drive_stuff *stuffp,
		     char *p, int sector, int nr_sectors)
/*	This does actually the transfer from the drive.
	Return:	-1 on timeout or other error
			else status byte (as in stuff->st) */
{
	int border;
	int done = 0;
	long timeout;

	if (stuffp->audio) {
		xwarn("Attempt to read from audio CD.\n");
		return -1;
	}

	if (!stuffp->readcmd) {
		xinfo("Can't transfer from missing disk.\n");
		return -1;
	}

	while (stuffp->lock) {
		interruptible_sleep_on(&stuffp->lockq);
	}

	if (stuffp->valid && (sector >= stuffp->pending)
	    && (sector < stuffp->low_border)) {

		/* All (or at least a part of the sectors requested) seems
		   * to be already requested, so we don't need to bother the
		   * drive with new requests ...
		   * Wait for the drive become idle, but first
		   * check for possible occurred errors --- the drive
		   * seems to report them asynchronously */


		border = stuffp->high_border < (border =
						sector + nr_sectors)
		    ? stuffp->high_border : border;

		stuffp->lock = current->pid;

		do {

			while (stuffp->busy) {

				timeout =
				    interruptible_sleep_on_timeout
				    (&stuffp->busyq, 5 * HZ);

				if (!stuffp->introk) {
					xtrace(XFER,
					       "error via interrupt\n");
				} else if (!timeout) {
					xtrace(XFER, "timeout\n");
				} else if (signal_pending(current)) {
					xtrace(XFER, "signal\n");
				} else
					continue;

				stuffp->lock = 0;
				stuffp->busy = 0;
				stuffp->valid = 0;

				wake_up_interruptible(&stuffp->lockq);
				xtrace(XFER, "transfer() done (-1)\n");
				return -1;
			}

			/* check if we need to set the busy flag (as we
			 * expect an interrupt */
			stuffp->busy = (3 == (stuffp->pending & 3));

			/* Test if it's the first sector of a block,
			 * there we have to skip some bytes as we read raw data */
			if (stuffp->xa && (0 == (stuffp->pending & 3))) {
				const int HEAD =
				    CD_FRAMESIZE_RAW - CD_XA_TAIL -
				    CD_FRAMESIZE;
				insb(stuffp->rreg_data, p, HEAD);
			}

			/* now actually read the data */
			insb(stuffp->rreg_data, p, 512);

			/* test if it's the last sector of a block,
			 * if so, we have to handle XA special */
			if ((3 == (stuffp->pending & 3)) && stuffp->xa) {
				char dummy[CD_XA_TAIL];
				insb(stuffp->rreg_data, &dummy[0], CD_XA_TAIL);
			}

			if (stuffp->pending == sector) {
				p += 512;
				done++;
				sector++;
			}
		} while (++(stuffp->pending) < border);

		stuffp->lock = 0;
		wake_up_interruptible(&stuffp->lockq);

	} else {

		/* The requested sector(s) is/are out of the
		 * already requested range, so we have to bother the drive
		 * with a new request. */

		static unsigned char cmd[] = {
			0,
			0, 0, 0,
			0, 0, 0
		};

		cmd[0] = stuffp->readcmd;

		/* The numbers held in ->pending, ..., should be valid */
		stuffp->valid = 1;
		stuffp->pending = sector & ~3;

		/* do some sanity checks */
		if (stuffp->pending > stuffp->lastsector) {
			xwarn
			    ("transfer() sector %d from nirvana requested.\n",
			     stuffp->pending);
			stuffp->status = MCDX_ST_EOM;
			stuffp->valid = 0;
			xtrace(XFER, "transfer() done (-1)\n");
			return -1;
		}

		if ((stuffp->low_border = stuffp->pending + DIRECT_SIZE)
		    > stuffp->lastsector + 1) {
			xtrace(XFER, "cut low_border\n");
			stuffp->low_border = stuffp->lastsector + 1;
		}
		if ((stuffp->high_border = stuffp->pending + REQUEST_SIZE)
		    > stuffp->lastsector + 1) {
			xtrace(XFER, "cut high_border\n");
			stuffp->high_border = stuffp->lastsector + 1;
		}

		{		/* Convert the sector to be requested to MSF format */
			struct cdrom_msf0 pending;
			log2msf(stuffp->pending / 4, &pending);
			cmd[1] = pending.minute;
			cmd[2] = pending.second;
			cmd[3] = pending.frame;
		}

		cmd[6] =
		    (unsigned
		     char) ((stuffp->high_border - stuffp->pending) / 4);
		xtrace(XFER, "[%2d]\n", cmd[6]);

		stuffp->busy = 1;
		/* Now really issue the request command */
		outsb(stuffp->wreg_data, cmd, sizeof cmd);

	}
#ifdef AK2
	if (stuffp->int_err) {
		stuffp->valid = 0;
		stuffp->int_err = 0;
		return -1;
	}
#endif				/* AK2 */

	stuffp->low_border = (stuffp->low_border +=
			      done) <
	    stuffp->high_border ? stuffp->low_border : stuffp->high_border;

	return done;
}


/*	Access to elements of the mcdx_drive_map members */

static unsigned port(int *ip)
{
	return ip[0];
}
static int irq(int *ip)
{
	return ip[1];
}

/*	Misc number converters */

static unsigned int bcd2uint(unsigned char c)
{
	return (c >> 4) * 10 + (c & 0x0f);
}

static unsigned int uint2bcd(unsigned int ival)
{
	return ((ival / 10) << 4) | (ival % 10);
}

static void log2msf(unsigned int l, struct cdrom_msf0 *pmsf)
{
	l += CD_MSF_OFFSET;
	pmsf->minute = uint2bcd(l / 4500), l %= 4500;
	pmsf->second = uint2bcd(l / 75);
	pmsf->frame = uint2bcd(l % 75);
}

static unsigned int msf2log(const struct cdrom_msf0 *pmsf)
{
	return bcd2uint(pmsf->frame)
	    + bcd2uint(pmsf->second) * 75
	    + bcd2uint(pmsf->minute) * 4500 - CD_MSF_OFFSET;
}

int mcdx_readtoc(struct s_drive_stuff *stuffp)
/*  Read the toc entries from the CD,
 *  Return: -1 on failure, else 0 */
{

	if (stuffp->toc) {
		xtrace(READTOC, "ioctl() toc already read\n");
		return 0;
	}

	xtrace(READTOC, "ioctl() readtoc for %d tracks\n",
	       stuffp->di.n_last - stuffp->di.n_first + 1);

	if (-1 == mcdx_hold(stuffp, 1))
		return -1;

	xtrace(READTOC, "ioctl() tocmode\n");
	if (-1 == mcdx_setdrivemode(stuffp, TOC, 1))
		return -EIO;

	/* all seems to be ok so far ... malloc */
	{
		int size;
		size =
		    sizeof(struct s_subqcode) * (stuffp->di.n_last -
						 stuffp->di.n_first + 2);

		xtrace(MALLOC, "ioctl() malloc %d bytes\n", size);
		stuffp->toc = kmalloc(size, GFP_KERNEL);
		if (!stuffp->toc) {
			xwarn("Cannot malloc %d bytes for toc\n", size);
			mcdx_setdrivemode(stuffp, DATA, 1);
			return -EIO;
		}
	}

	/* now read actually the index */
	{
		int trk;
		int retries;

		for (trk = 0;
		     trk < (stuffp->di.n_last - stuffp->di.n_first + 1);
		     trk++)
			stuffp->toc[trk].index = 0;

		for (retries = 300; retries; retries--) {	/* why 300? */
			struct s_subqcode q;
			unsigned int idx;

			if (-1 == mcdx_requestsubqcode(stuffp, &q, 1)) {
				mcdx_setdrivemode(stuffp, DATA, 1);
				return -EIO;
			}

			idx = bcd2uint(q.index);

			if ((idx > 0)
			    && (idx <= stuffp->di.n_last)
			    && (q.tno == 0)
			    && (stuffp->toc[idx - stuffp->di.n_first].
				index == 0)) {
				stuffp->toc[idx - stuffp->di.n_first] = q;
				xtrace(READTOC,
				       "ioctl() toc idx %d (trk %d)\n",
				       idx, trk);
				trk--;
			}
			if (trk == 0)
				break;
		}
		memset(&stuffp->
		       toc[stuffp->di.n_last - stuffp->di.n_first + 1], 0,
		       sizeof(stuffp->toc[0]));
		stuffp->toc[stuffp->di.n_last - stuffp->di.n_first +
			    1].dt = stuffp->di.msf_leadout;
	}

	/* unset toc mode */
	xtrace(READTOC, "ioctl() undo toc mode\n");
	if (-1 == mcdx_setdrivemode(stuffp, DATA, 2))
		return -EIO;

#if MCDX_DEBUG && READTOC
	{
		int trk;
		for (trk = 0;
		     trk < (stuffp->di.n_last - stuffp->di.n_first + 2);
		     trk++)
			xtrace(READTOC, "ioctl() %d readtoc %02x %02x %02x"
			       "  %02x:%02x.%02x  %02x:%02x.%02x\n",
			       trk + stuffp->di.n_first,
			       stuffp->toc[trk].control,
			       stuffp->toc[trk].tno,
			       stuffp->toc[trk].index,
			       stuffp->toc[trk].tt.minute,
			       stuffp->toc[trk].tt.second,
			       stuffp->toc[trk].tt.frame,
			       stuffp->toc[trk].dt.minute,
			       stuffp->toc[trk].dt.second,
			       stuffp->toc[trk].dt.frame);
	}
#endif

	return 0;
}

static int
mcdx_playmsf(struct s_drive_stuff *stuffp, const struct cdrom_msf *msf)
{
	unsigned char cmd[7] = {
		0, 0, 0, 0, 0, 0, 0
	};

	if (!stuffp->readcmd) {
		xinfo("Can't play from missing disk.\n");
		return -1;
	}

	cmd[0] = stuffp->playcmd;

	cmd[1] = msf->cdmsf_min0;
	cmd[2] = msf->cdmsf_sec0;
	cmd[3] = msf->cdmsf_frame0;
	cmd[4] = msf->cdmsf_min1;
	cmd[5] = msf->cdmsf_sec1;
	cmd[6] = msf->cdmsf_frame1;

	xtrace(PLAYMSF, "ioctl(): play %x "
	       "%02x:%02x:%02x -- %02x:%02x:%02x\n",
	       cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6]);

	outsb(stuffp->wreg_data, cmd, sizeof cmd);

	if (-1 == mcdx_getval(stuffp, 3 * HZ, 0, NULL)) {
		xwarn("playmsf() timeout\n");
		return -1;
	}

	stuffp->audiostatus = CDROM_AUDIO_PLAY;
	return 0;
}

static int
mcdx_playtrk(struct s_drive_stuff *stuffp, const struct cdrom_ti *ti)
{
	struct s_subqcode *p;
	struct cdrom_msf msf;

	if (-1 == mcdx_readtoc(stuffp))
		return -1;

	if (ti)
		p = &stuffp->toc[ti->cdti_trk0 - stuffp->di.n_first];
	else
		p = &stuffp->start;

	msf.cdmsf_min0 = p->dt.minute;
	msf.cdmsf_sec0 = p->dt.second;
	msf.cdmsf_frame0 = p->dt.frame;

	if (ti) {
		p = &stuffp->toc[ti->cdti_trk1 - stuffp->di.n_first + 1];
		stuffp->stop = *p;
	} else
		p = &stuffp->stop;

	msf.cdmsf_min1 = p->dt.minute;
	msf.cdmsf_sec1 = p->dt.second;
	msf.cdmsf_frame1 = p->dt.frame;

	return mcdx_playmsf(stuffp, &msf);
}


/* Drive functions ************************************************/

static int mcdx_tray_move(struct cdrom_device_info *cdi, int position)
{
	struct s_drive_stuff *stuffp = cdi->handle;

	if (!stuffp->present)
		return -ENXIO;
	if (!(stuffp->present & DOOR))
		return -ENOSYS;

	if (position)		/* 1: eject */
		return mcdx_talk(stuffp, "\xf6", 1, NULL, 1, 5 * HZ, 3);
	else			/* 0: close */
		return mcdx_talk(stuffp, "\xf8", 1, NULL, 1, 5 * HZ, 3);
	return 1;
}

static int mcdx_stop(struct s_drive_stuff *stuffp, int tries)
{
	return mcdx_talk(stuffp, "\xf0", 1, NULL, 1, 2 * HZ, tries);
}

static int mcdx_hold(struct s_drive_stuff *stuffp, int tries)
{
	return mcdx_talk(stuffp, "\x70", 1, NULL, 1, 2 * HZ, tries);
}

static int mcdx_requestsubqcode(struct s_drive_stuff *stuffp,
		     struct s_subqcode *sub, int tries)
{
	char buf[11];
	int ans;

	if (-1 == (ans = mcdx_talk(stuffp, "\x20", 1, buf, sizeof(buf),
				   2 * HZ, tries)))
		return -1;
	sub->control = buf[1];
	sub->tno = buf[2];
	sub->index = buf[3];
	sub->tt.minute = buf[4];
	sub->tt.second = buf[5];
	sub->tt.frame = buf[6];
	sub->dt.minute = buf[8];
	sub->dt.second = buf[9];
	sub->dt.frame = buf[10];

	return ans;
}

static int mcdx_requestmultidiskinfo(struct s_drive_stuff *stuffp,
			  struct s_multi *multi, int tries)
{
	char buf[5];
	int ans;

	if (stuffp->present & MULTI) {
		ans =
		    mcdx_talk(stuffp, "\x11", 1, buf, sizeof(buf), 2 * HZ,
			      tries);
		multi->multi = buf[1];
		multi->msf_last.minute = buf[2];
		multi->msf_last.second = buf[3];
		multi->msf_last.frame = buf[4];
		return ans;
	} else {
		multi->multi = 0;
		return 0;
	}
}

static int mcdx_requesttocdata(struct s_drive_stuff *stuffp, struct s_diskinfo *info,
		    int tries)
{
	char buf[9];
	int ans;
	ans =
	    mcdx_talk(stuffp, "\x10", 1, buf, sizeof(buf), 2 * HZ, tries);
	if (ans == -1) {
		info->n_first = 0;
		info->n_last = 0;
	} else {
		info->n_first = bcd2uint(buf[1]);
		info->n_last = bcd2uint(buf[2]);
		info->msf_leadout.minute = buf[3];
		info->msf_leadout.second = buf[4];
		info->msf_leadout.frame = buf[5];
		info->msf_first.minute = buf[6];
		info->msf_first.second = buf[7];
		info->msf_first.frame = buf[8];
	}
	return ans;
}

static int mcdx_setdrivemode(struct s_drive_stuff *stuffp, enum drivemodes mode,
		  int tries)
{
	char cmd[2];
	int ans;

	xtrace(HW, "setdrivemode() %d\n", mode);

	if (-1 == (ans = mcdx_talk(stuffp, "\xc2", 1, cmd, sizeof(cmd), 5 * HZ, tries)))
		return -1;

	switch (mode) {
	case TOC:
		cmd[1] |= 0x04;
		break;
	case DATA:
		cmd[1] &= ~0x04;
		break;
	case RAW:
		cmd[1] |= 0x40;
		break;
	case COOKED:
		cmd[1] &= ~0x40;
		break;
	default:
		break;
	}
	cmd[0] = 0x50;
	return mcdx_talk(stuffp, cmd, 2, NULL, 1, 5 * HZ, tries);
}

static int mcdx_setdatamode(struct s_drive_stuff *stuffp, enum datamodes mode,
		 int tries)
{
	unsigned char cmd[2] = { 0xa0 };
	xtrace(HW, "setdatamode() %d\n", mode);
	switch (mode) {
	case MODE0:
		cmd[1] = 0x00;
		break;
	case MODE1:
		cmd[1] = 0x01;
		break;
	case MODE2:
		cmd[1] = 0x02;
		break;
	default:
		return -EINVAL;
	}
	return mcdx_talk(stuffp, cmd, 2, NULL, 1, 5 * HZ, tries);
}

static int mcdx_config(struct s_drive_stuff *stuffp, int tries)
{
	char cmd[4];

	xtrace(HW, "config()\n");

	cmd[0] = 0x90;

	cmd[1] = 0x10;		/* irq enable */
	cmd[2] = 0x05;		/* pre, err irq enable */

	if (-1 == mcdx_talk(stuffp, cmd, 3, NULL, 1, 1 * HZ, tries))
		return -1;

	cmd[1] = 0x02;		/* dma select */
	cmd[2] = 0x00;		/* no dma */

	return mcdx_talk(stuffp, cmd, 3, NULL, 1, 1 * HZ, tries);
}

static int mcdx_requestversion(struct s_drive_stuff *stuffp, struct s_version *ver,
		    int tries)
{
	char buf[3];
	int ans;

	if (-1 == (ans = mcdx_talk(stuffp, "\xdc",
				   1, buf, sizeof(buf), 2 * HZ, tries)))
		return ans;

	ver->code = buf[1];
	ver->ver = buf[2];

	return ans;
}

static int mcdx_reset(struct s_drive_stuff *stuffp, enum resetmodes mode, int tries)
{
	if (mode == HARD) {
		outb(0, stuffp->wreg_chn);	/* no dma, no irq -> hardware */
		outb(0, stuffp->wreg_reset);	/* hw reset */
		return 0;
	} else
		return mcdx_talk(stuffp, "\x60", 1, NULL, 1, 5 * HZ, tries);
}

static int mcdx_lockdoor(struct cdrom_device_info *cdi, int lock)
{
	struct s_drive_stuff *stuffp = cdi->handle;
	char cmd[2] = { 0xfe };

	if (!(stuffp->present & DOOR))
		return -ENOSYS;
	if (stuffp->present & DOOR) {
		cmd[1] = lock ? 0x01 : 0x00;
		return mcdx_talk(stuffp, cmd, sizeof(cmd), NULL, 1, 5 * HZ, 3);
	} else
		return 0;
}

static int mcdx_getstatus(struct s_drive_stuff *stuffp, int tries)
{
	return mcdx_talk(stuffp, "\x40", 1, NULL, 1, 5 * HZ, tries);
}

static int
mcdx_getval(struct s_drive_stuff *stuffp, int to, int delay, char *buf)
{
	unsigned long timeout = to + jiffies;
	char c;

	if (!buf)
		buf = &c;

	while (inb(stuffp->rreg_status) & MCDX_RBIT_STEN) {
		if (time_after(jiffies, timeout))
			return -1;
		mcdx_delay(stuffp, delay);
	}

	*buf = (unsigned char) inb(stuffp->rreg_data) & 0xff;

	return 0;
}

static int mcdx_setattentuator(struct s_drive_stuff *stuffp,
		    struct cdrom_volctrl *vol, int tries)
{
	char cmd[5];
	cmd[0] = 0xae;
	cmd[1] = vol->channel0;
	cmd[2] = 0;
	cmd[3] = vol->channel1;
	cmd[4] = 0;

	return mcdx_talk(stuffp, cmd, sizeof(cmd), NULL, 5, 200, tries);
}

MODULE_LICENSE("GPL");
MODULE_ALIAS_BLOCKDEV_MAJOR(MITSUMI_X_CDROM_MAJOR);
