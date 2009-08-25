/*
**       COWLOOP block device driver (2.6 kernel compliant)
** =======================================================================
** Read-write loop-driver with copy-on-write functionality.
**
** Synopsis:
**
**     modprobe cowloop [maxcows=..] [rdofile=..... cowfile=.... [option=r]]
**
** Definition of number of configured cowdevices:
**   maxcows=	number of configured cowdevices (default: 16)
** (do not confuse this with MAXCOWS: absolute maximum as compiled)
**
** One pair of filenames can be supplied during insmod/modprobe to open
** the first cowdevice:
**   rdofile=	read-only file (or filesystem)
**   cowfile=	storage-space for modified blocks of read-only file(system)
**   option=r	repair cowfile automatically if it appears to be dirty
**
** Other cowdevices can be activated via the command "cowdev"
** whenever the cowloop-driver is loaded.
**
** The read-only file may be of type 'regular' or 'block-device'.
**
** The cowfile must be of type 'regular'.
** If an existing regular file is used as cowfile, its contents will be
** used again for the current read-only file. When the cowfile has not been
** closed properly during a previous session (i.e. rmmod cowloop), the
** cowloop-driver refuses to open it unless the parameter "option=r" is
** specified.
**
** Layout of cowfile:
**
** 	+-----------------------------+
**	|       cow head block        |   MAPUNIT bytes
**	|-----------------------------|
**	|                             |   MAPUNIT bytes
**	|---                       ---|
**	|                             |   MAPUNIT bytes
**	|---                       ---|
**	|      used-block bitmap      |   MAPUNIT bytes
**	|-----------------------------|
**	|  gap to align start-offset  |
**	|        to 4K multiple       |
**	|-----------------------------|  <---- start-offset cow blocks
**	|                             |
**      |    written cow blocks       |   MAPUNIT bytes
**      |          .....              |
**
** 	cowhead block:
**   	  - contains general info about the rdofile which is related
** 	    to this cowfile
**
** 	used-block bitmap:
** 	  - contains one bit per block with a size of MAPUNIT bytes
** 	  - bit-value '1' = block has been written on cow
** 	              '0' = block unused on cow
** 	  - total bitmap rounded to multiples of MAPUNIT
**
** ============================================================================
** Author:             Gerlof Langeveld - AT Computing (March 2003)
** Current maintainer: Hendrik-Jan Thomassen - AT Computing (Summer 2006)
** Email:              hjt@ATComputing.nl
** ----------------------------------------------------------------------------
** Copyright (C) 2003-2009 AT Consultancy
**
** This program is free software; you can redistribute it and/or modify it
** under the terms of the GNU General Public License as published by the
** Free Software Foundation; either version 2, or (at your option) any
** later version.
**
** This program is distributed in the hope that it will be useful, but
** WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
** See the GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
** ----------------------------------------------------------------------------
**
** Major modifications:
**
** 	200405	Ported to kernel-version 2.6		Hendrik-Jan Thomassen
**	200405	Added cowhead to cowfile to garantee
**		consistency with read-only file		Gerlof Langeveld
**	200405	Postponed flushing of bitmaps to improve
**		performance. 				Gerlof Langeveld
**	200405	Inline recovery for dirty cowfiles.	Gerlof Langeveld
**	200502	Redesign to support more cowdevices.	Gerlof Langeveld
**	200502	Support devices/file > 2 Gbytes.	Gerlof Langeveld
**	200507	Check for free space to expand cowfile.	Gerlof Langeveld
**	200902  Upgrade for kernel 2.6.28               Hendrik-Jan Thomassen
**
** Inspired by
**    loop.c  by Theodore Ts'o and
**    cloop.c by Paul `Rusty' Russell & Klaus Knopper.
**
** Design-considerations:
**
**   For the first experiments with the cowloop-driver, the request-queue
**   made use of the do_generic_file_read() which worked fine except
**   in combination with the cloop-driver; that combination
**   resulted in a non-interruptible hangup of the system during
**   heavy load. Other experiments using the `make_request' interface also
**   resulted in unpredictable system hangups (with proper use of spinlocks).
**
**   To overcome these problems, the cowloop-driver starts a kernel-thread
**   for every active cowdevice.
**   All read- and write-request on the read-only file and copy-on-write file
**   are handled in the context of that thread.
**   A scheme has been designed to wakeup the kernel-thread as
**   soon as I/O-requests are available in the request-queue; this thread
**   handles the requests one-by-one by calling the proper read- or
**   write-function related to the open read-only file or copy-on-write file.
**   When all pending requests have been handled, the kernel-thread goes
**   back to sleep-state.
**   This approach requires some additional context-switches; however the
**   performance loss during heavy I/O is less than 3%.
**
** -------------------------------------------------------------------------*/
/* The following is the cowloop package version number. It must be
   identical to the content of the include-file "version.h" that is
   used in all supporting utilities:                                  */
char revision[] = "$Revision: 3.1 $"; /* cowlo_init_module() has
			     assumptions about this string's format   */

/* Note that the following numbers are *not* the cowloop package version
   numbers, but separate revision history numbers to track the
   modifications of this particular source file:                      */
/* $Log: cowloop.c,v $
**
** Revision 1.30  2009/02/08 hjt
** Integrated earlier fixes
** Upgraded to kernel 2.6.28 (thanks Jerome Poulin)
**
** Revision 1.29  2006/12/03 22:12:00  hjt
** changed 'cowdevlock' from spinlock to semaphore, to avoid
** "scheduling while atomic". Contributed by Juergen Christ.
** Added version.h again
**
** Revision 1.28  2006/08/16 16:00:00  hjt
** malloc each individual cowloopdevice struct separately
**
** Revision 1.27  2006/03/14 14:57:03  root
** Removed include version.h
**
** Revision 1.26  2005/08/08 11:22:48  root
** Implement possibility to close a cow file or reopen a cowfile read-only.
**
** Revision 1.25  2005/08/03 14:00:39  root
** Added modinfo info to driver.
**
** Revision 1.24  2005/07/21 06:14:53  root
** Cosmetic changes source code.
**
** Revision 1.23  2005/07/20 13:07:32  root
** Supply ioctl to write watchdog program to react on lack of cowfile space.
**
** Revision 1.22  2005/07/20 07:53:34  root
** Regular verification of free space in filesystem holding the cowfile
** (give warnings whenever space is almost exhausted).
** Terminology change: checksum renamed to fingerprint.
**
** Revision 1.21  2005/07/19 09:21:52  root
** Removing maximum limit of 16 Gb per cowdevice.
**
** Revision 1.20  2005/07/19 07:50:33  root
** Minor bugfixes and cosmetic changes.
**
** Revision 1.19  2005/06/10 12:29:55  root
** Removed lock/unlock operation from cowlo_open().
**
** Revision 1.18  2005/05/09 12:56:26  root
** Allow a cowdevice to be open more than once
** (needed for support of ReiserFS and XFS).
**
** Revision 1.17  2005/03/17 14:36:16  root
** Fixed some license issues.
**
** Revision 1.16  2005/03/07 14:42:05  root
** Only allow one parallel open per cowdevice.
**
** Revision 1.15  2005/02/18 11:52:04  gerlof
** Redesign to support more than one cowdevice > 2 Gb space.
**
** Revision 1.14  2004/08/17 14:19:16  gerlof
** Modified output of /proc/cowloop.
**
** Revision 1.13  2004/08/16 07:21:10  gerlof
** Separate statistical counter for read on rdofile and cowfile.
**
** Revision 1.12  2004/08/11 06:52:11  gerlof
** Modified messages.
**
** Revision 1.11  2004/08/11 06:44:11  gerlof
** Modified log messages.
**
** Revision 1.10  2004/08/10 12:27:27  gerlof
** Cosmetic changes.
**
** Revision 1.9  2004/08/09 11:43:37  gerlof
** Removed double definition of major number (COWMAJOR).
**
** Revision 1.8  2004/08/09 08:03:39  gerlof
** Cleanup of messages.
**
** Revision 1.7  2004/05/27 06:37:33  gerlof
** Modified /proc message.
**
** Revision 1.6  2004/05/26 21:23:28  gerlof
** Modified /proc output.
**
** Revision 1.5  2004/05/26 13:23:34  gerlof
** Support cowsync to force flushing the bitmaps and cowhead.
**
** Revision 1.4  2004/05/26 11:11:10  gerlof
** Updated the comment to the actual situation.
**
** Revision 1.3  2004/05/26 10:50:00  gerlof
** Implemented recovery-option.
**
** Revision 1.2  2004/05/25 15:14:41  gerlof
** Modified bitmap flushing strategy.
**
*/

#define COWMAJOR	241

// #define COWDEBUG

#ifdef 	COWDEBUG
#define DEBUGP		printk
#define DCOW		KERN_ALERT
#else
#define DEBUGP(format, x...)
#endif

#include <linux/types.h>
#include <linux/autoconf.h>
#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif
#include <linux/module.h>
#include <linux/version.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27))
#include <linux/semaphore.h>
#else
#include <asm/semaphore.h>
#endif
#include <asm/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/hdreg.h>
#include <linux/genhd.h>
#include <linux/statfs.h>

#include "cowloop.h"

MODULE_LICENSE("GPL");
/* MODULE_AUTHOR("Gerlof Langeveld <gerlof@ATComputing.nl>");     obsolete address */
MODULE_AUTHOR("Hendrik-Jan Thomassen <hjt@ATComputing.nl>"); /* current maintainer */
MODULE_DESCRIPTION("Copy-on-write loop driver");
MODULE_PARM_DESC(maxcows, " Number of configured cowdevices (default 16)");
MODULE_PARM_DESC(rdofile, " Read-only file for /dev/cow/0");
MODULE_PARM_DESC(cowfile, " Cowfile for /dev/cow/0");
MODULE_PARM_DESC(option, "  Repair cowfile if inconsistent: option=r");

#define DEVICE_NAME	"cow"

#define	DFLCOWS		16		/* default cowloop devices	*/

static int maxcows = DFLCOWS;
module_param(maxcows, int, 0);
static char *rdofile = "";
module_param(rdofile, charp, 0);
static char *cowfile = "";
module_param(cowfile, charp, 0);
static char *option = "";
module_param(option, charp, 0);

/*
** per cowdevice several bitmap chunks are allowed of MAPCHUNKSZ each
**
** each bitmap chunk can describe MAPCHUNKSZ * 8 * MAPUNIT bytes of data
** suppose:
**	MAPCHUNKSZ 4096 and MAPUNIT 1024 --> 4096 * 8 * 1024 = 32 Mb per chunk
*/
#define	MAPCHUNKSZ	4096	/* #bytes per bitmap chunk  (do not change)  */

#define SPCMINBLK	100	/* space threshold to give warning messages  */
#define SPCDFLINTVL	16	/* once every SPCDFLINTVL writes to cowfile, */
				/* available space in filesystem is checked  */

#define	CALCMAP(x)	((x)/(MAPCHUNKSZ*8))
#define	CALCBYTE(x)	(((x)%(MAPCHUNKSZ*8))>>3)
#define	CALCBIT(x)	((x)&7)

#define ALLCOW		1
#define ALLRDO		2
#define MIXEDUP		3

static char	allzeroes[MAPUNIT];

/*
** administration per cowdevice (pair of cowfile/rdofile)
*/

/* bit-values for state */
#define	COWDEVOPEN	0x01	/* cowdevice opened                          */
#define	COWRWCOWOPEN	0x02	/* cowfile opened read-write                 */
#define	COWRDCOWOPEN	0x04	/* cowfile opened read-only                  */
#define	COWWATCHDOG	0x08 	/* ioctl for watchdog cowfile space active   */

#define	COWCOWOPEN	(COWRWCOWOPEN|COWRDCOWOPEN)

struct cowloop_device
{
	/*
	** current status
	*/
	int		state;			/* bit-values (see above)    */
	int		opencnt;		/* # opens for cowdevice     */

        /*
	** open file pointers
	*/
        struct file  	*rdofp,   *cowfp;	/* open file pointers        */
	char		*rdoname, *cowname;	/* file names                */

	/*
	** request queue administration
	*/
	struct request_queue	*rqueue;
	spinlock_t		rqlock;
	struct gendisk		*gd;

	/*
	** administration about read-only file
	*/
	unsigned int	     numblocks;	/* # blocks input file in MAPUNIT    */
	unsigned int	     blocksz;   /* minimum unit to access this dev   */
	unsigned long	     fingerprint; /* fingerprint of current rdofile  */
	struct block_device  *belowdev;	/* block device below us             */
	struct gendisk       *belowgd;  /* gendisk for blk dev below us      */
	struct request_queue *belowq;	/* req. queue of blk dev below us    */

	/*
	** bitmap administration to register which blocks are modified
	*/
	long int	mapsize;	/* total size of bitmap (bytes)      */
	long int	mapremain;	/* remaining bytes in last bitmap    */
	int		mapcount;       /* number of bitmaps in use          */
	char 		**mapcache;	/* area with pointers to bitmaps     */

	char		*iobuf;		/* databuffer of MAPUNIT bytes       */
	struct cowhead	*cowhead;	/* buffer containing cowhead         */

	/*
	** administration for interface with the kernel-thread
	*/
	int		pid;		/* pid==0: no thread available       */
	struct request	*req;		/* request to be handled now         */
	wait_queue_head_t waitq;	/* wait-Q: thread waits for work     */
	char		closedown;	/* boolean: thread exit required     */
	char		qfilled;	/* boolean: I/O request pending      */
	char		iobusy;		/* boolean: req under treatment      */

	/*
	** administration to keep track of free space in cowfile filesystem
	*/
	unsigned long	blksize;	/* block size of fs (bytes)          */
	unsigned long	blktotal;	/* recent total space in fs (blocks) */
	unsigned long	blkavail;	/* recent free  space in fs (blocks) */

	wait_queue_head_t watchq;	/* wait-Q: watcher awaits threshold  */
	unsigned long	watchthresh;	/* threshold of watcher (blocks)     */

	/*
	** statistical counters
	*/
	unsigned long	rdoreads;	/* number of  read-actions rdo       */
	unsigned long	cowreads;	/* number of  read-actions cow       */
	unsigned long	cowwrites;	/* number of write-actions           */
	unsigned long	nrcowblocks;	/* number of blocks in use on cow    */
};

static struct cowloop_device	**cowdevall;	/* ptr to ptrs to all cowdevices */
static struct semaphore 	cowdevlock;	/* generic lock for cowdevs      */

static struct gendisk		*cowctlgd;	/* gendisk control channel       */
static spinlock_t		cowctlrqlock;   /* for req.q. of ctrl. channel   */

/*
** private directory /proc/cow
*/
struct proc_dir_entry	*cowlo_procdir;

/*
** function prototypes
*/
static long int cowlo_do_request (struct request *req);
static void	cowlo_sync       (void);
static int	cowlo_checkio    (struct cowloop_device *,         int, loff_t);
static int	cowlo_readmix    (struct cowloop_device *, void *, int, loff_t);
static int	cowlo_writemix   (struct cowloop_device *, void *, int, loff_t);
static long int cowlo_readrdo    (struct cowloop_device *, void *, int, loff_t);
static long int cowlo_readcow    (struct cowloop_device *, void *, int, loff_t);
static long int cowlo_readcowraw (struct cowloop_device *, void *, int, loff_t);
static long int cowlo_writecow   (struct cowloop_device *, void *, int, loff_t);
static long int cowlo_writecowraw(struct cowloop_device *, void *, int, loff_t);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28))
static int      cowlo_ioctl      (struct block_device *, fmode_t,
           			 		unsigned int, unsigned long);
#else
static int      cowlo_ioctl      (struct inode *, struct file *,
           			 		unsigned int, unsigned long);
#endif

static int	cowlo_makepair    (struct cowpair __user *);
static int	cowlo_removepair  (unsigned long  __user *);
static int	cowlo_watch       (struct cowpair __user *);
static int	cowlo_cowctl      (unsigned long  __user *, int);
static int	cowlo_openpair    (char *, char *, int, int);
static int 	cowlo_closepair   (struct cowloop_device *);
static int	cowlo_openrdo     (struct cowloop_device *, char *);
static int	cowlo_opencow     (struct cowloop_device *, char *, int);
static void	cowlo_undo_openrdo(struct cowloop_device *);
static void	cowlo_undo_opencow(struct cowloop_device *);

/*****************************************************************************/
/* System call handling                                                      */
/*****************************************************************************/

/*
** handle system call open()/mount()
**
** returns:
** 	0   - okay
**    < 0   - error value
*/
static int
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28))
cowlo_open(struct block_device *bdev, fmode_t mode)
#else
cowlo_open(struct inode *inode, struct file *file)
#endif
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28))
	struct inode *inode = bdev->bd_inode;
#endif
	if (!inode)
		return -EINVAL;

	if (imajor(inode) != COWMAJOR) {
		printk(KERN_WARNING
		       "cowloop - unexpected major %d\n", imajor(inode));
		return -ENODEV;
	}

	switch (iminor(inode)) {
	   case COWCTL:
		DEBUGP(DCOW"cowloop - open %d control\n", COWCTL);
		break;

	   default:
		DEBUGP(DCOW"cowloop - open minor %d\n", iminor(inode));

		if ( iminor(inode) >= maxcows )
			return -ENODEV;

		if ( !((cowdevall[iminor(inode)])->state & COWDEVOPEN) )
			return -ENODEV;

		(cowdevall[iminor(inode)])->opencnt++;
	}

	return 0;
}

/*
** handle system call close()/umount()
**
** returns:
** 	0   - okay
*/
static int
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28))
cowlo_release(struct gendisk *gd, fmode_t mode)
#else
cowlo_release(struct inode *inode, struct file *file)
#endif
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28))
	struct block_device *bdev;
	struct inode *inode;

	bdev = bdget_disk(gd, 0);
	inode = bdev->bd_inode;
#endif
	if (!inode)
		return 0;

	DEBUGP(DCOW"cowloop - release (close) minor %d\n", iminor(inode));

	if ( iminor(inode) != COWCTL)
		(cowdevall[iminor(inode)])->opencnt--;

	return 0;
}

/*
** handle system call ioctl()
**
** returns:
** 	0   - okay
**    < 0   - error value
*/
static int
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28))
cowlo_ioctl(struct block_device *bdev, fmode_t mode,
            unsigned int cmd, unsigned long arg)
#else
cowlo_ioctl(struct inode *inode, struct file *filp,
            unsigned int cmd, unsigned long arg)
#endif
{
	struct hd_geometry	geo;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28))
	struct inode *inode = bdev->bd_inode;
#endif

	DEBUGP(DCOW "cowloop - ioctl cmd %x\n", cmd);

	switch ( iminor(inode) ) {

	   /*
	   ** allowed via control device only
	   */
	   case COWCTL:
		switch (cmd) {
		   /*
		   ** write all bitmap chunks and cowheaders to cowfiles
		   */
		   case COWSYNC:
			down(&cowdevlock);
			cowlo_sync();
			up(&cowdevlock);
			return 0;

		   /*
		   ** open a new cowdevice (pair of rdofile/cowfile)
		   */
		   case COWMKPAIR:
			return cowlo_makepair((void __user *)arg);

		   /*
		   ** close a cowdevice (pair of rdofile/cowfile)
		   */
		   case COWRMPAIR:
			return cowlo_removepair((void __user *)arg);

		   /*
		   ** watch free space of filesystem containing cowfile
		   */
		   case COWWATCH:
			return cowlo_watch((void __user *)arg);

		   /*
		   ** close cowfile for active device
		   */
		   case COWCLOSE:
			return cowlo_cowctl((void __user *)arg, COWCLOSE);

		   /*
		   ** reopen cowfile read-only for active device
		   */
		   case COWRDOPEN:
			return cowlo_cowctl((void __user *)arg, COWRDOPEN);

		   default:
			return -EINVAL;
		} /* end of switch on command */

	   /*
	   ** allowed for any other cowdevice
	   */
	   default:
		switch (cmd) {
		   /*
		   ** HDIO_GETGEO must be supported for fdisk, etc
		   */
		   case HDIO_GETGEO:
			geo.cylinders = 0;
			geo.heads     = 0;
			geo.sectors   = 0;

			if (copy_to_user((void __user *)arg, &geo, sizeof geo))
				return -EFAULT;
			return 0;

		   default:
			return -EINVAL;
		} /* end of switch on ioctl-cmd code parameter */
	} /* end of switch on minor number */
}

static struct block_device_operations cowlo_fops =
{
	.owner	 =     THIS_MODULE,
        .open    =     cowlo_open,	/* called upon open  */
        .release =     cowlo_release,	/* called upon close */
        .ioctl   =     cowlo_ioctl,     /* called upon ioctl */
};

/*
** handle ioctl-command COWMKPAIR:
**	open a new cowdevice (pair of rdofile/cowfile) on-the-fly
**
** returns:
** 	0   - okay
**    < 0   - error value
*/
static int
cowlo_makepair(struct cowpair __user *arg)
{
	int		i, rv=0;
	struct cowpair	cowpair;
	unsigned char	*cowpath;
	unsigned char	*rdopath;

	/*
	** retrieve info about pathnames
	*/
	if ( copy_from_user(&cowpair, arg, sizeof cowpair) )
		return -EFAULT;

	if ( (MAJOR(cowpair.device) != COWMAJOR) && (cowpair.device != ANYDEV) )
		return -EINVAL;

	if ( (MINOR(cowpair.device) >= maxcows)  && (cowpair.device != ANYDEV) )
		return -EINVAL;

	/*
	** retrieve pathname strings
	*/
	if ( (cowpair.cowflen > PATH_MAX) || (cowpair.rdoflen > PATH_MAX) )
		return -ENAMETOOLONG;

	if ( !(cowpath = kmalloc(cowpair.cowflen+1, GFP_KERNEL)) )
		return -ENOMEM;

	if ( copy_from_user(cowpath, (void __user *)cowpair.cowfile,
	                                            cowpair.cowflen) ) {
		kfree(cowpath);
		return -EFAULT;
	}
	*(cowpath+cowpair.cowflen) = 0;

	if ( !(rdopath = kmalloc(cowpair.rdoflen+1, GFP_KERNEL)) ) {
		kfree(cowpath);
		return -ENOMEM;
	}

	if ( copy_from_user(rdopath, (void __user *)cowpair.rdofile,
	                                            cowpair.rdoflen) ) {
		kfree(rdopath);
		kfree(cowpath);
		return -EFAULT;
	}
	*(rdopath+cowpair.rdoflen) = 0;

	/*
	** open new cowdevice
	*/
	if ( cowpair.device == ANYDEV) {
		/*
		** search first unused minor
		*/
		for (i=0, rv=-EBUSY; i < maxcows; i++) {
			if ( !((cowdevall[i])->state & COWDEVOPEN) ) {
				rv = cowlo_openpair(rdopath, cowpath, 0, i);
				break;
			}
		}

		if (rv) { 		/* open failed? */
			kfree(rdopath);
			kfree(cowpath);
			return rv;
		}

		/*
		** return newly allocated cowdevice to user space
		*/
		cowpair.device = MKDEV(COWMAJOR, i);

		if ( copy_to_user(arg, &cowpair, sizeof cowpair)) {
			kfree(rdopath);
			kfree(cowpath);
			return -EFAULT;
		}
	} else { 		/* specific minor requested */
		if ( (rv = cowlo_openpair(rdopath, cowpath, 0,
						MINOR(cowpair.device)))) {
			kfree(rdopath);
			kfree(cowpath);
			return rv;
		}
	}

	return 0;
}

/*
** handle ioctl-command COWRMPAIR:
** 	deactivate an existing cowdevice (pair of rdofile/cowfile) on-the-fly
**
** returns:
** 	0   - okay
**    < 0   - error value
*/
static int
cowlo_removepair(unsigned long __user *arg)
{
	unsigned long		cowdevice;
	struct cowloop_device	*cowdev;

	/*
	** retrieve info about device to be removed
	*/
	if ( copy_from_user(&cowdevice, arg, sizeof cowdevice))
		return -EFAULT;

	/*
	** verify major-minor number
	*/
	if ( MAJOR(cowdevice) != COWMAJOR)
		return -EINVAL;

	if ( MINOR(cowdevice) >= maxcows)
		return -EINVAL;

	cowdev = cowdevall[MINOR(cowdevice)];

	if ( !(cowdev->state & COWDEVOPEN) )
		return -ENODEV;

	/*
	** synchronize bitmaps and close cowdevice
	*/
	if (cowdev->state & COWRWCOWOPEN) {
		down(&cowdevlock);
		cowlo_sync();
		up(&cowdevlock);
	}

	return cowlo_closepair(cowdev);
}

/*
** handle ioctl-command COWWATCH:
**	watch the free space of the filesystem containing a cowfile
**      of an open cowdevice
**
** returns:
** 	0   - okay
**    < 0   - error value
*/
static int
cowlo_watch(struct cowpair __user *arg)
{
	struct cowloop_device	*cowdev;
	struct cowwatch		cowwatch;

	/*
	** retrieve structure holding info
	*/
	if ( copy_from_user(&cowwatch, arg, sizeof cowwatch))
		return -EFAULT;

	/*
	** verify if cowdevice exists and is currently open
	*/
	if ( MINOR(cowwatch.device) >= maxcows)
		return -EINVAL;

	cowdev = cowdevall[MINOR(cowwatch.device)];

	if ( !(cowdev->state & COWDEVOPEN) )
		return -ENODEV;

	/*
	** if the WATCHWAIT-option is set, wait until the indicated
	** threshold is reached (only one waiter allowed)
	*/
	if (cowwatch.flags & WATCHWAIT) {
		/*
		** check if already another waiter active
		** for this cowdevice
		*/
		if (cowdev->state & COWWATCHDOG)
			return -EAGAIN;

		cowdev->state |= COWWATCHDOG;

		cowdev->watchthresh = (unsigned long long)
		                      cowwatch.threshold /
				      (cowdev->blksize / 1024);

		if (wait_event_interruptible(cowdev->watchq,
		                    cowdev->watchthresh >= cowdev->blkavail)) {
			cowdev->state &= ~COWWATCHDOG;
			return EINTR;
		}

		cowdev->state &= ~COWWATCHDOG;
	}

	cowwatch.totalkb = (unsigned long long)cowdev->blktotal *
	                                       cowdev->blksize / 1024;
	cowwatch.availkb = (unsigned long long)cowdev->blkavail *
	                                       cowdev->blksize / 1024;

	if ( copy_to_user(arg, &cowwatch, sizeof cowwatch))
		return -EFAULT;

	return 0;
}

/*
** handle ioctl-commands COWCLOSE and COWRDOPEN:
**	COWCLOSE  - close the cowfile while the cowdevice remains open;
**                  this allows an unmount of the filesystem on which
**                  the cowfile resides
**	COWRDOPEN - close the cowfile and reopen it for read-only;
**                  this allows a remount read-ony of the filesystem
**                  on which the cowfile resides
**
** returns:
** 	0   - okay
**    < 0   - error value
*/
static int
cowlo_cowctl(unsigned long __user *arg, int cmd)
{
	struct cowloop_device	*cowdev;
	unsigned long		cowdevice;

	/*
	** retrieve info about device to be removed
	*/
	if ( copy_from_user(&cowdevice, arg, sizeof cowdevice))
		return -EFAULT;

	/*
	** verify major-minor number
	*/
	if ( MAJOR(cowdevice) != COWMAJOR)
		return -EINVAL;

	if ( MINOR(cowdevice) >= maxcows)
		return -EINVAL;

	cowdev = cowdevall[MINOR(cowdevice)];

	if ( !(cowdev->state & COWDEVOPEN) )
		return -ENODEV;

	/*
	** synchronize bitmaps and close cowfile
	*/
	if (cowdev->state & COWRWCOWOPEN) {
		down(&cowdevlock);
		cowlo_sync();
		up(&cowdevlock);
	}

	/*
	** handle specific ioctl-command
	*/
	switch (cmd) {
	   case COWRDOPEN:
		/*
		** if the cowfile is still opened read-write
		*/
		if (cowdev->state & COWRWCOWOPEN) {
			/*
			** close the cowfile
			*/
  			if (cowdev->cowfp)
		  		filp_close(cowdev->cowfp, 0);

			cowdev->state &= ~COWRWCOWOPEN;

			/*
			** open again for read-only
			*/
			cowdev->cowfp = filp_open(cowdev->cowname,
		                          O_RDONLY|O_LARGEFILE, 0600);

			if ( (cowdev->cowfp == NULL) || IS_ERR(cowdev->cowfp) ) {
				printk(KERN_ERR
				     "cowloop - failed to reopen cowfile %s\n",
				     cowdev->cowname);
				return -EINVAL;
			}

			/*
			** mark cowfile open for read-only
			*/
			cowdev->state |= COWRDCOWOPEN;
		} else {
			return -EINVAL;
		}
		break;

	   case COWCLOSE:
		/*
		** if the cowfile is still open
		*/
		if (cowdev->state & COWCOWOPEN) {
			/*
			** close the cowfile
			*/
  			if (cowdev->cowfp)
		  		filp_close(cowdev->cowfp, 0);

			cowdev->state &= ~COWCOWOPEN;
		}
	}

	return 0;
}


/*****************************************************************************/
/* Handling of I/O-requests for a cowdevice                                  */
/*****************************************************************************/

/*
** function to be called by core-kernel to handle the I/O-requests
** in the queue
*/
static void
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25))
cowlo_request(struct request_queue *q)
#else
cowlo_request(request_queue_t *q)
#endif
{
	struct request		*req;
	struct cowloop_device	*cowdev;

	DEBUGP(DCOW "cowloop - request function called....\n");

	while((req = blk_peek_request(q)) != NULL) {
		DEBUGP(DCOW "cowloop - got next request\n");

		if (! blk_fs_request(req)) {
               		 /* this is not a normal file system request */
                	__blk_end_request_cur(req, -EIO);
                	continue;
        	}
		cowdev = req->rq_disk->private_data;

		if (cowdev->iobusy)
			return;
		else
			cowdev->iobusy = 1;

		/*
		** when no kernel-thread is available, the request will
		** produce an I/O-error
		*/
		if (!cowdev->pid) {
			printk(KERN_ERR"cowloop - no thread available\n");
			__blk_end_request_cur(req, -EIO);	/* request failed */
			cowdev->iobusy	= 0;
			continue;
		}

		/*
		** handle I/O-request in the context of the kernel-thread
		*/
		cowdev->req 	= req;
		cowdev->qfilled	= 1;

		wake_up_interruptible_sync(&cowdev->waitq);

		/*
		** get out of this function now while the I/O-request is
		** under treatment of the kernel-thread; this function
		** will be called again after the current I/O-request has
		** been finished by the thread
		*/
		return;
	}
}

/*
** daemon-process (kernel-thread) executes this function
*/
static int
cowlo_daemon(struct cowloop_device *cowdev)
{
	int	rv;
	int     minor;
	char	myname[16];

	for (minor = 0; minor < maxcows; minor++) {
		if (cowdev == cowdevall[minor]) break;
	}
	sprintf(myname, "cowloopd%d", minor);

        daemonize(myname);

	while (!cowdev->closedown) {
		/*
		** sleep while waiting for an I/O request;
		** note that no non-interruptible wait has been used
		** because the non-interruptible version of
		** a *synchronous* wake_up does not exist (any more)
		*/
		if (wait_event_interruptible(cowdev->waitq, cowdev->qfilled)){
			flush_signals(current); /* ignore signal-based wakeup */
			continue;
		}

		if (cowdev->closedown)		/* module will be unloaded ? */{
			cowdev->pid = 0;
			return 0;
		}

		/*
		** woken up by the I/O-request handler:	treat requested I/O
		*/
		cowdev->qfilled = 0;

		rv = cowlo_do_request(cowdev->req);

		/*
		** reacquire the queue-spinlock for manipulating
		** the request-queue and dequeue the request
		*/
		spin_lock_irq(&cowdev->rqlock);

		__blk_end_request_cur(cowdev->req, rv);
		cowdev->iobusy = 0;

		/*
		** initiate the next request from the queue
		*/
		cowlo_request(cowdev->rqueue);

		spin_unlock_irq(&cowdev->rqlock);
	}
	return 0;
}

/*
** function to be called in the context of the kernel thread
** to handle the queued I/O-requests
**
** returns:
** 	0   - fail
**      1   - success
*/
static long int
cowlo_do_request(struct request *req)
{
	unsigned long		len;
	long int		rv;
	loff_t 			offset;
	struct cowloop_device	*cowdev = req->rq_disk->private_data;

	/*
	** calculate some variables which are needed later on
	*/
	len     =          blk_rq_cur_sectors(req) << 9;
	offset  = (loff_t) blk_rq_pos(req)         << 9;

	DEBUGP(DCOW"cowloop - req cmd=%d offset=%lld len=%lu addr=%p\n",
				*(req->cmd), offset, len, req->buffer);

	/*
	** handle READ- or WRITE-request
	*/
	switch (rq_data_dir(req)) {
	   /**********************************************************/
	   case READ:
		switch ( cowlo_checkio(cowdev, len, offset) ) {
		   case ALLCOW:
			rv = cowlo_readcow(cowdev, req->buffer, len, offset);
			break;

		   case ALLRDO:
			rv = cowlo_readrdo(cowdev, req->buffer, len, offset);
			break;

	   	   case MIXEDUP:
			rv = cowlo_readmix(cowdev, req->buffer, len, offset);
			break;

		   default:
			rv = 0;	/* never happens */
		}
		break;

	   /**********************************************************/
	   case WRITE:
		switch ( cowlo_checkio(cowdev, len, offset) ) {
		   case ALLCOW:
			/*
			** straight-forward write will do...
			*/
			DEBUGP(DCOW"cowloop - write straight ");

			rv = cowlo_writecow(cowdev, req->buffer, len, offset);
			break;	/* from switch */

		   case ALLRDO:
			if ( (len & MUMASK) == 0) {
				DEBUGP(DCOW"cowloop - write straight ");

				rv = cowlo_writecow(cowdev, req->buffer,
								len, offset);
				break;
			}

	   	   case MIXEDUP:
			rv = cowlo_writemix(cowdev, req->buffer, len, offset);
			break;

		   default:
			rv = 0;	/* never happens */
		}
		break;

	   default:
		printk(KERN_ERR
		       "cowloop - unrecognized command %d\n", *(req->cmd));
		rv = 0;
	}

	return (rv <= 0 ? 0 : 1);
}

/*
** check for a given I/O-request if all underlying blocks
** (with size MAPUNIT) are either in the read-only file or in
** the cowfile (or a combination of the two)
**
** returns:
** 	ALLRDO  - all underlying blocks in rdofile
**      ALLCOW  - all underlying blocks in cowfile
**      MIXEDUP - underlying blocks partly in rdofile and partly in cowfile
*/
static int
cowlo_checkio(struct cowloop_device *cowdev, int len, loff_t offset)
{
	unsigned long	mapnum, bytenum, bitnum, blocknr, partlen;
	long int	totcnt, cowcnt;
	char		*mc;

	/*
	** notice that the requested block might cross
	** a blocksize boundary while one of the concerned
	** blocks resides in the read-only file and another
	** one in the copy-on-write file; in that case the
        ** request will be broken up into pieces
	*/
	if ( (len <= MAPUNIT) &&
	     (MAPUNIT - (offset & MUMASK) <= len) ) {
		/*
		** easy situation:
		** requested data-block entirely fits within
		** the mapunit used for the bitmap
		** check if that block is located in rdofile or
		** cowfile
		*/
		blocknr = offset >> MUSHIFT;

		mapnum  = CALCMAP (blocknr);
		bytenum = CALCBYTE(blocknr);
		bitnum  = CALCBIT (blocknr);

		if (*(*(cowdev->mapcache+mapnum)+bytenum)&(1<<bitnum))
			return ALLCOW;
		else
			return ALLRDO;
	}

	/*
	** less easy situation:
	** the requested data-block does not fit within the mapunit
	** used for the bitmap
	** check if *all* underlying blocks involved reside on the rdofile
       	** or the cowfile (so still no breakup required)
	*/
	for (cowcnt=totcnt=0; len > 0; len-=partlen, offset+=partlen, totcnt++){
		/*
		** calculate blocknr of involved block
		*/
		blocknr = offset >> MUSHIFT;

		/*
		** calculate partial length for this transfer
		*/
		partlen = MAPUNIT - (offset & MUMASK);
		if (partlen > len)
			partlen = len;

		/*
		** is this block located in the cowfile
		*/
		mapnum  = CALCMAP (blocknr);
		bytenum = CALCBYTE(blocknr);
		bitnum  = CALCBIT (blocknr);

		mc	= *(cowdev->mapcache+mapnum);

		if (*(mc+bytenum)&(1<<bitnum))
			cowcnt++;;

		DEBUGP(DCOW
		       "cowloop - check %lu - map %lu, byte %lu, bit %lu, "
		       "cowcnt %ld, totcnt %ld %02x %p\n",
			blocknr, mapnum, bytenum, bitnum, cowcnt, totcnt,
			*(mc+bytenum), mc);
	}

	if (cowcnt == 0)	/* all involved blocks on rdofile? */
		return ALLRDO;

	if (cowcnt == totcnt)	/* all involved blocks on cowfile? */
		return ALLCOW;

	/*
	** situation somewhat more complicated:
	** involved underlying blocks spread over both files
	*/
	return MIXEDUP;
}

/*
** read requested chunk partly from rdofile and partly from cowfile
**
** returns:
** 	0   - fail
**      1   - success
*/
static int
cowlo_readmix(struct cowloop_device *cowdev, void *buf, int len, loff_t offset)
{
	unsigned long	mapnum, bytenum, bitnum, blocknr, partlen;
	long int	rv;
	char		*mc;

	/*
	** complicated approach: breakup required of read-request
	*/
	for (rv=1; len > 0; len-=partlen, buf+=partlen, offset+=partlen) {
		/*
		** calculate blocknr of entire block
		*/
		blocknr = offset >> MUSHIFT;

		/*
		** calculate partial length for this transfer
		*/
		partlen = MAPUNIT - (offset & MUMASK);
		if (partlen > len)
			partlen = len;

		/*
		** is this block located in the cowfile
		*/
		mapnum  = CALCMAP (blocknr);
		bytenum = CALCBYTE(blocknr);
		bitnum  = CALCBIT (blocknr);
		mc	= *(cowdev->mapcache+mapnum);

		if (*(mc+bytenum)&(1<<bitnum)) {
			/*
			** read (partial) block from cowfile
			*/
			DEBUGP(DCOW"cowloop - split read "
				"cow partlen=%ld off=%lld\n", partlen, offset);

			if (cowlo_readcow(cowdev, buf, partlen, offset) <= 0)
				rv = 0;
		} else {
			/*
			** read (partial) block from rdofile
			*/
			DEBUGP(DCOW"cowloop - split read "
				"rdo partlen=%ld off=%lld\n", partlen, offset);

			if (cowlo_readrdo(cowdev, buf, partlen, offset) <= 0)
				rv = 0;
		}
	}

	return rv;
}

/*
** chunk to be written to the cowfile needs pieces to be
** read from the rdofile
**
** returns:
** 	0   - fail
**      1   - success
*/
static int
cowlo_writemix(struct cowloop_device *cowdev, void *buf, int len, loff_t offset)
{
	unsigned long	mapnum, bytenum, bitnum, blocknr, partlen;
	long int	rv;
	char		*mc;

	/*
	** somewhat more complicated stuff is required:
	** if the request is larger than one underlying
	** block or is spread over two underlying blocks,
	** split the request into pieces; if a block does not
	** start at a block boundary, take care that
	** surrounding data is read first (if needed),
	** fit the new data in and write it as a full block
	*/
	for (rv=1; len > 0; len-=partlen, buf+=partlen, offset+=partlen) {
		/*
		** calculate partial length for this transfer
		*/
		partlen = MAPUNIT - (offset & MUMASK);
		if (partlen > len)
			partlen = len;

		/*
		** calculate blocknr of entire block
		*/
		blocknr = offset >> MUSHIFT;

		/*
		** has this block been written before?
		*/
		mapnum  = CALCMAP (blocknr);
		bytenum = CALCBYTE(blocknr);
		bitnum  = CALCBIT (blocknr);
		mc	= *(cowdev->mapcache+mapnum);

		if (*(mc+bytenum)&(1<<bitnum)) {
			/*
			** block has been written before;
			** write transparantly to cowfile
			*/
			DEBUGP(DCOW
			       "cowloop - splitwr transp\n");

			if (cowlo_writecow(cowdev, buf, partlen, offset) <= 0)
				rv = 0;
		} else {
			/*
			** block has never been written before,
			** so read entire block from
			** read-only file first, unless
			** a full block is requested to
			** be written
			*/
			if (partlen < MAPUNIT) {
				if (cowlo_readrdo(cowdev, cowdev->iobuf,
				      MAPUNIT, (loff_t)blocknr << MUSHIFT) <= 0)
					rv = 0;
			}

			/*
			** transfer modified part into
			** the block just read
			*/
			memcpy(cowdev->iobuf + (offset & MUMASK), buf, partlen);

			/*
			** write entire block to cowfile
			*/
			DEBUGP(DCOW"cowloop - split "
				"partlen=%ld off=%lld\n",
				partlen, (loff_t)blocknr << MUSHIFT);

			if (cowlo_writecow(cowdev, cowdev->iobuf, MAPUNIT,
					     (loff_t)blocknr << MUSHIFT) <= 0)
				rv = 0;
		}
	}

	return rv;
}

/*****************************************************************************/
/* I/O-support for read-only file and copy-on-write file                     */
/*****************************************************************************/

/*
** read data from the read-only file
**
** return-value: similar to user-mode read
*/
static long int
cowlo_readrdo(struct cowloop_device *cowdev, void *buf, int len, loff_t offset)
{
	long int	rv;
	mm_segment_t	old_fs;
	loff_t		saveoffset = offset;

	DEBUGP(DCOW"cowloop - readrdo called\n");

        old_fs = get_fs();
	set_fs( get_ds() );
	rv = cowdev->rdofp->f_op->read(cowdev->rdofp, buf, len, &offset);
        set_fs(old_fs);

	if (rv < len) {
		printk(KERN_WARNING "cowloop - read-failure %ld on rdofile"
		                    "- offset=%lld len=%d\n",
					rv, saveoffset, len);
	}

	cowdev->rdoreads++;
	return rv;
}

/*
** read cowfile from a modified offset, i.e. skipping the bitmap and cowhead
**
** return-value: similar to user-mode read
*/
static long int
cowlo_readcow(struct cowloop_device *cowdev, void *buf, int len, loff_t offset)
{
	DEBUGP(DCOW"cowloop - readcow called\n");

	offset += cowdev->cowhead->doffset;

	return cowlo_readcowraw(cowdev, buf, len, offset);
}

/*
** read cowfile from an absolute offset
**
** return-value: similar to user-mode read
*/
static long int
cowlo_readcowraw(struct cowloop_device *cowdev,
					void *buf, int len, loff_t offset)
{
	long int	rv;
	mm_segment_t	old_fs;
	loff_t		saveoffset = offset;

	DEBUGP(DCOW"cowloop - readcowraw called\n");

	/*
	** be sure that cowfile is opened for read-write
	*/
	if ( !(cowdev->state & COWCOWOPEN) ) {
		 printk(KERN_WARNING
		        "cowloop - read request from cowfile refused\n");

		return -EBADF;
	}

	/*
	** issue low level read
	*/
        old_fs = get_fs();
	set_fs( get_ds() );
	rv = cowdev->cowfp->f_op->read(cowdev->cowfp, buf, len, &offset);
        set_fs(old_fs);

	if (rv < len) {
		printk(KERN_WARNING
		       "cowloop - read-failure %ld on cowfile"
		       "- offset=%lld len=%d\n", rv, saveoffset, len);
	}

	cowdev->cowreads++;
	return rv;
}

/*
** write cowfile from a modified offset, i.e. skipping the bitmap and cowhead
**
** if a block is written for the first time while its contents consists
** of binary zeroes only, the concerning bitmap is flushed to the cowfile
**
** return-value: similar to user-mode write
*/
static long int
cowlo_writecow(struct cowloop_device *cowdev, void *buf, int len, loff_t offset)
{
	long int	rv;
	unsigned long	mapnum=0, mapbyte=0, mapbit=0, cowblock=0, partlen;
	char		*tmpptr,  *mapptr = NULL;
	loff_t		tmpoffset, mapoffset = 0;

	DEBUGP(DCOW"cowloop - writecow called\n");

	/*
	** be sure that cowfile is opened for read-write
	*/
	if ( !(cowdev->state & COWRWCOWOPEN) ) {
		 printk(KERN_WARNING
		        "cowloop - Write request to cowfile refused\n");

		return -EBADF;
	}

	/*
	** write the entire block to the cowfile
	*/
	tmpoffset = offset + cowdev->cowhead->doffset;

	rv = cowlo_writecowraw(cowdev, buf, len, tmpoffset);

	/*
	** verify if enough space available on filesystem holding
	** the cowfile
	**   - when the last write failed (might be caused by lack of space)
	**   - when a watcher is active (to react adequatly)
	**   - when the previous check indicated fs was almost full
	**   - with regular intervals
	*/
	if ( (rv <= 0)				       ||
	     (cowdev->state        & COWWATCHDOG)      ||
	     (cowdev->blkavail / 2 < SPCDFLINTVL)      ||
	     (cowdev->cowwrites    % SPCDFLINTVL == 0) ) {
		struct kstatfs		ks;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18))
		if (vfs_statfs(cowdev->cowfp->f_dentry, &ks)==0){
#else
		if (vfs_statfs(cowdev->cowfp->f_dentry->d_inode->i_sb, &ks)==0){
#endif
			if (ks.f_bavail <= SPCMINBLK) {
				switch (ks.f_bavail) {
				   case 0:
				   case 1:
				   case 2:
				   case 3:
					printk(KERN_ALERT
					       "cowloop - "
					       "ALERT: cowfile full!\n");
					break;

				   default:
					printk(KERN_WARNING
					       "cowloop - cowfile almost "
					       "full (only %llu Kb free)\n",
						(unsigned long long)
                                                ks.f_bsize * ks.f_bavail /1024);
				}
			}

			cowdev->blktotal = ks.f_blocks;
			cowdev->blkavail = ks.f_bavail;

			/*
			** wakeup watcher if threshold has been reached
			*/
			if ( (cowdev->state & COWWATCHDOG) &&
			    (cowdev->watchthresh >= cowdev->blkavail) ) {
				wake_up_interruptible(&cowdev->watchq);
			}
		}
	}

	if (rv <= 0)
		return rv;

	DEBUGP(DCOW"cowloop - block written\n");

	/*
	** check if block(s) is/are written to the cowfile
	** for the first time; if so, adapt the bitmap
	*/
	for (; len > 0; len-=partlen, offset+=partlen, buf+=partlen) {
		/*
		** calculate partial length for this transfer
		*/
		partlen = MAPUNIT - (offset & MUMASK);
		if (partlen > len)
			partlen = len;

		/*
		** calculate bitnr of written chunk of cowblock
		*/
		cowblock = offset >> MUSHIFT;

		mapnum   = CALCMAP (cowblock);
		mapbyte  = CALCBYTE(cowblock);
		mapbit   = CALCBIT (cowblock);

		if (*(*(cowdev->mapcache+mapnum)+mapbyte) & (1<<mapbit))
			continue;	/* already written before */

	       	/*
		** if the block is written for the first time,
		** the corresponding bit should be set in the bitmap
		*/
		*(*(cowdev->mapcache+mapnum)+mapbyte) |= (1<<mapbit);

		cowdev->nrcowblocks++;

		DEBUGP(DCOW"cowloop - bitupdate blk=%ld map=%ld "
		        "byte=%ld bit=%ld\n",
			cowblock, mapnum, mapbyte, mapbit);

		/*
		** check if the cowhead in the cowfile is currently
		** marked clean; if so, mark it dirty and flush it
		*/
		if ( !(cowdev->cowhead->flags &= COWDIRTY)) {
			cowdev->cowhead->flags	|= COWDIRTY;

			cowlo_writecowraw(cowdev, cowdev->cowhead,
							MAPUNIT, (loff_t)0);
		}

		/*
		** if the written datablock contained binary zeroes,
		** the bitmap block should be marked to be flushed to disk
		** (blocks containing all zeroes cannot be recovered by
		** the cowrepair-program later on if cowloop is not properly
		** removed via rmmod)
		*/
		if ( memcmp(buf, allzeroes, partlen) ) /* not all zeroes? */
			continue;                      /* no flush needed */

		/*
		** calculate positions of bitmap block to be flushed
		** - pointer of bitmap block in memory
		** - offset  of bitmap block in cowfile
		*/
		tmpptr    = *(cowdev->mapcache+mapnum) + (mapbyte & (~MUMASK));
		tmpoffset = (loff_t) MAPUNIT + mapnum * MAPCHUNKSZ +
		                                       (mapbyte & (~MUMASK));

		/*
		** flush a bitmap block at the moment that all bits have
		** been set in that block, i.e. at the moment that we
		** switch to another bitmap block
		*/
		if ( (mapoffset != 0) && (mapoffset != tmpoffset) ) {
			if (cowlo_writecowraw(cowdev, mapptr, MAPUNIT,
							mapoffset) < 0) {
				printk(KERN_WARNING
				       "cowloop - write-failure on bitmap - "
				       "blk=%ld map=%ld byte=%ld bit=%ld\n",
				  	cowblock, mapnum, mapbyte, mapbit);
			}

			DEBUGP(DCOW"cowloop - bitmap blk written %lld\n",
								mapoffset);
		}

		/*
		** remember offset in cowfile and offset in memory
		** for bitmap to be flushed; flushing will be done
		** as soon as all updates in this bitmap block have
		** been done
		*/
		mapoffset = tmpoffset;
		mapptr    = tmpptr;
	}

	/*
	** any new block written containing binary zeroes?
	*/
	if (mapoffset) {
		if (cowlo_writecowraw(cowdev, mapptr, MAPUNIT, mapoffset) < 0) {
			printk(KERN_WARNING
			       "cowloop - write-failure on bitmap - "
			       "blk=%ld map=%ld byte=%ld bit=%ld\n",
			       cowblock, mapnum, mapbyte, mapbit);
		}

		DEBUGP(DCOW"cowloop - bitmap block written %lld\n", mapoffset);
	}

	return rv;
}

/*
** write cowfile from an absolute offset
**
** return-value: similar to user-mode write
*/
static long int
cowlo_writecowraw(struct cowloop_device *cowdev,
					void *buf, int len, loff_t offset)
{
	long int	rv;
	mm_segment_t	old_fs;
	loff_t		saveoffset = offset;

	DEBUGP(DCOW"cowloop - writecowraw called\n");

	/*
	** be sure that cowfile is opened for read-write
	*/
	if ( !(cowdev->state & COWRWCOWOPEN) ) {
		 printk(KERN_WARNING
		        "cowloop - write request to cowfile refused\n");

		return -EBADF;
	}

	/*
	** issue low level write
	*/
        old_fs = get_fs();
	set_fs( get_ds() );
	rv = cowdev->cowfp->f_op->write(cowdev->cowfp, buf, len, &offset);
        set_fs(old_fs);

	if (rv < len) {
		printk(KERN_WARNING
		       "cowloop - write-failure %ld on cowfile"
		       "- offset=%lld len=%d\n", rv, saveoffset, len);
	}

	cowdev->cowwrites++;
	return rv;
}


/*
** readproc-function: called when the corresponding /proc-file is read
*/
static int
cowlo_readproc(char *buf, char **start, off_t pos, int cnt, int *eof, void *p)
{
	struct cowloop_device *cowdev = p;

	revision[sizeof revision - 3] = '\0';

	return sprintf(buf,
		"   cowloop version: %9s\n\n"
		"      device state: %s%s%s%s\n"
		"   number of opens: %9d\n"
		"     pid of thread: %9d\n\n"
		"    read-only file: %9s\n"
		"          rdoreads: %9lu\n\n"
		"copy-on-write file: %9s\n"
		"     state cowfile: %9s\n"
		"     bitmap-blocks: %9lu (of %d bytes)\n"
		"  cowblocks in use: %9lu (of %d bytes)\n"
		"          cowreads: %9lu\n"
		"         cowwrites: %9lu\n",
			&revision[11],

			cowdev->state & COWDEVOPEN   ? "devopen "   : "",
			cowdev->state & COWRWCOWOPEN ? "cowopenrw " : "",
			cowdev->state & COWRDCOWOPEN ? "cowopenro " : "",
			cowdev->state & COWWATCHDOG  ? "watchdog "  : "",

			cowdev->opencnt,
			cowdev->pid,
			cowdev->rdoname,
			cowdev->rdoreads,
			cowdev->cowname,
			cowdev->cowhead->flags & COWDIRTY ? "dirty":"clean",
			cowdev->mapsize >> MUSHIFT, MAPUNIT,
			cowdev->nrcowblocks, MAPUNIT,
			cowdev->cowreads,
			cowdev->cowwrites);
}

/*****************************************************************************/
/* Setup and destroy cowdevices                                              */
/*****************************************************************************/

/*
** open and prepare a cowdevice (rdofile and cowfile) and allocate bitmaps
**
** returns:
** 	0   - okay
**    < 0   - error value
*/
static int
cowlo_openpair(char *rdof, char *cowf, int autorecover, int minor)
{
	long int		rv;
	struct cowloop_device	*cowdev = cowdevall[minor];
	struct kstatfs		ks;

	down(&cowdevlock);

	/*
	** requested device exists?
	*/
	if (minor >= maxcows) {
		up(&cowdevlock);
		return -ENODEV;
	}

	/*
	** requested device already assigned to cowdevice?
	*/
	if (cowdev->state & COWDEVOPEN) {
		up(&cowdevlock);
		return -EBUSY;
	}

	/*
	** initialize administration
	*/
	memset(cowdev, 0, sizeof *cowdev);

	spin_lock_init     (&cowdev->rqlock);
	init_waitqueue_head(&cowdev->waitq);
	init_waitqueue_head(&cowdev->watchq);

	/*
	** open the read-only file
	*/
	DEBUGP(DCOW"cowloop - call openrdo....\n");

	if ( (rv = cowlo_openrdo(cowdev, rdof)) ) {
		cowlo_undo_openrdo(cowdev);
		up(&cowdevlock);
		return rv;
	}

	/*
	** open the cowfile
	*/
	DEBUGP(DCOW"cowloop - call opencow....\n");

	if ( (rv = cowlo_opencow(cowdev, cowf, autorecover)) ) {
		cowlo_undo_openrdo(cowdev);
		cowlo_undo_opencow(cowdev);
		up(&cowdevlock);
		return rv;
	}

	/*
	** administer total and available size of filesystem holding cowfile
	*/
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18))
		if (vfs_statfs(cowdev->cowfp->f_dentry, &ks)==0){
#else
		if (vfs_statfs(cowdev->cowfp->f_dentry->d_inode->i_sb, &ks)==0){
#endif
		cowdev->blksize  = ks.f_bsize;
		cowdev->blktotal = ks.f_blocks;
		cowdev->blkavail = ks.f_bavail;
	} else {
		cowdev->blksize  = 1024;	/* avoid division by zero */
	}

	/*
	** flush the (recovered) bitmaps and cowhead to the cowfile
	*/
	DEBUGP(DCOW"cowloop - call cowsync....\n");

	cowlo_sync();

	/*
	** allocate gendisk for the cow device
	*/
	DEBUGP(DCOW"cowloop - alloc disk....\n");

	if ((cowdev->gd = alloc_disk(1)) == NULL) {
		printk(KERN_WARNING
		       "cowloop - unable to alloc_disk for cowloop\n");

		cowlo_undo_openrdo(cowdev);
		cowlo_undo_opencow(cowdev);
		up(&cowdevlock);
		return -ENOMEM;
	}

	cowdev->gd->major        = COWMAJOR;
	cowdev->gd->first_minor  = minor;
	cowdev->gd->minors       = 1;
	cowdev->gd->fops         = &cowlo_fops;
	cowdev->gd->private_data = cowdev;
	sprintf(cowdev->gd->disk_name, "%s%d", DEVICE_NAME, minor);

	/* in .5 Kb units */
	set_capacity(cowdev->gd, (cowdev->numblocks*(MAPUNIT/512)));

	DEBUGP(DCOW"cowloop - init request queue....\n");

	if ((cowdev->rqueue = blk_init_queue(cowlo_request, &cowdev->rqlock))
								== NULL) {
		printk(KERN_WARNING
		       "cowloop - unable to get request queue for cowloop\n");

		del_gendisk(cowdev->gd);
		cowlo_undo_openrdo(cowdev);
		cowlo_undo_opencow(cowdev);
		up(&cowdevlock);
		return -EINVAL;
	}

	blk_queue_logical_block_size(cowdev->rqueue, cowdev->blocksz);
	cowdev->gd->queue = cowdev->rqueue;

	/*
	** start kernel thread to handle requests
	*/
	DEBUGP(DCOW"cowloop - kickoff daemon....\n");

	cowdev->pid = kernel_thread((int (*)(void *))cowlo_daemon, cowdev, 0);

	/*
	** create a file below directory /proc/cow for this new cowdevice
	*/
	if (cowlo_procdir) {
		char 	tmpname[64];

		sprintf(tmpname, "%d", minor);

		create_proc_read_entry(tmpname, 0 , cowlo_procdir,
						cowlo_readproc, cowdev);
	}

	cowdev->state	|= COWDEVOPEN;

	cowdev->rdoname = rdof;
	cowdev->cowname = cowf;

	/*
	** enable the new disk; this triggers the first request!
	*/
	DEBUGP(DCOW"cowloop - call add_disk....\n");

	add_disk(cowdev->gd);

	up(&cowdevlock);
	return 0;
}

/*
** close a cowdevice (pair of rdofile/cowfile) and release memory
**
** returns:
** 	0   - okay
**    < 0   - error value
*/
static int
cowlo_closepair(struct cowloop_device *cowdev)
{
	int minor;

	down(&cowdevlock);

	/*
	** if cowdevice is not activated at all, refuse
	*/
	if ( !(cowdev->state & COWDEVOPEN) ) {
		up(&cowdevlock);
		return -ENODEV;
	}

	/*
	** if this cowdevice is still open, refuse
	*/
	if (cowdev->opencnt > 0) {
		up(&cowdevlock);
		return -EBUSY;
	}

	up(&cowdevlock);

	/*
	** wakeup watcher (if any)
	*/
	if (cowdev->state & COWWATCHDOG) {
		cowdev->watchthresh = cowdev->blkavail;
		wake_up_interruptible(&cowdev->watchq);
	}

	/*
	** wakeup kernel-thread to be able to exit
	** and wait until it has exited
	*/
	cowdev->closedown = 1;
	cowdev->qfilled   = 1;
	wake_up_interruptible(&cowdev->waitq);

       	while (cowdev->pid)
               	schedule();

	del_gendisk(cowdev->gd);  /* revert the alloc_disk() */
	put_disk(cowdev->gd);     /* revert the add_disk()   */

	if (cowlo_procdir) {
		char 	tmpname[64];

		for (minor = 0; minor < maxcows; minor++) {
			if (cowdev == cowdevall[minor]) break;
		}
		sprintf(tmpname, "%d", minor);

		remove_proc_entry(tmpname, cowlo_procdir);
	}

	blk_cleanup_queue(cowdev->rqueue);

	/*
	** release memory for filenames if these names have
	** been allocated dynamically
	*/
	if ( (cowdev->cowname) && (cowdev->cowname != cowfile))
		kfree(cowdev->cowname);

	if ( (cowdev->rdoname) && (cowdev->rdoname != rdofile))
		kfree(cowdev->rdoname);

	cowlo_undo_openrdo(cowdev);
	cowlo_undo_opencow(cowdev);

	cowdev->state &= ~COWDEVOPEN;

	return 0;
}

/*
** open the read-only file
**
** returns:
** 	0   - okay
**    < 0   - error value
*/
static int
cowlo_openrdo(struct cowloop_device *cowdev, char *rdof)
{
	struct file	*f;
	struct inode	*inode;
	long int	i, nrval;

	DEBUGP(DCOW"cowloop - openrdo called\n");

	/*
	** open the read-only file
	*/
        if(*rdof == '\0') {
     	     	printk(KERN_ERR
		       "cowloop - specify name for read-only file\n\n");
         	return -EINVAL;
        }

	f = filp_open(rdof, O_RDONLY|O_LARGEFILE, 0);

	if ( (f == NULL) || IS_ERR(f) ) {
		printk(KERN_ERR
		       "cowloop - open of rdofile %s failed\n", rdof);
		return -EINVAL;
	}

	cowdev->rdofp = f;

	inode = f->f_dentry->d_inode;

	if ( !S_ISREG(inode->i_mode) && !S_ISBLK(inode->i_mode) ) {
		printk(KERN_ERR
		       "cowloop - %s not regular file or blockdev\n", rdof);
		return -EINVAL;
	}

	DEBUGP(DCOW"cowloop - determine size rdo....\n");

	/*
	** determine block-size and total size of read-only file
	*/
	if (S_ISREG(inode->i_mode)) {
		/*
		** read-only file is a regular file
		*/
		cowdev->blocksz   = 512;	/* other value fails */
		cowdev->numblocks = inode->i_size >> MUSHIFT;

		if (inode->i_size & MUMASK) {
			printk(KERN_WARNING
			       "cowloop - rdofile %s truncated to multiple "
			       "of %d bytes\n", rdof, MAPUNIT);
		}

		DEBUGP(DCOW"cowloop - RO=regular: numblocks=%d, blocksz=%d\n",
			cowdev->numblocks, cowdev->blocksz);
	} else {
		/*
		** read-only file is a block device
		*/
		cowdev->belowdev  = inode->i_bdev;
		cowdev->belowgd   = cowdev->belowdev->bd_disk; /* gendisk */

		if (cowdev->belowdev->bd_part) {
			cowdev->numblocks = cowdev->belowdev->bd_part->nr_sects
								/ (MAPUNIT/512);
		}

		if (cowdev->belowgd) {
			cowdev->belowq = cowdev->belowgd->queue;

			if (cowdev->numblocks == 0) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27))
				cowdev->numblocks = get_capacity(cowdev->belowgd)
                         					/ (MAPUNIT/512);
#else
				cowdev->numblocks = cowdev->belowgd->capacity
                         					/ (MAPUNIT/512);
#endif
			}
		}


		if (cowdev->belowq)
			cowdev->blocksz = queue_logical_block_size(cowdev->belowq);

		if (cowdev->blocksz == 0)
			cowdev->blocksz = BLOCK_SIZE; /* default 2^10 */

		DEBUGP(DCOW"cowloop - numblocks=%d, "
		           "blocksz=%d, belowgd=%p, belowq=%p\n",
		  	cowdev->numblocks, cowdev->blocksz,
			cowdev->belowgd, cowdev->belowq);

		DEBUGP(DCOW"cowloop - belowdev.bd_block_size=%d\n",
		  	cowdev->belowdev->bd_block_size);
	}

	if (cowdev->numblocks == 0) {
		printk(KERN_ERR "cowloop - %s has no contents\n", rdof);
		return -EINVAL;
	}

	/*
	** reserve space in memory as generic I/O buffer
	*/
	cowdev->iobuf  = kmalloc(MAPUNIT, GFP_KERNEL);

	if (!cowdev->iobuf) {
		printk(KERN_ERR
		       "cowloop - cannot get space for buffer %d\n", MAPUNIT);
		return -ENOMEM;
	}

	DEBUGP(DCOW"cowloop - determine fingerprint rdo....\n");

	/*
	** determine fingerprint for read-only file
	** 	calculate fingerprint from first four datablocks
	**	which do not contain binary zeroes
	*/
	for (i=0, cowdev->fingerprint=0, nrval=0;
			(nrval < 4)&&(i < cowdev->numblocks); i++) {
		int 		j;
		unsigned char	cs;

		/*
		** read next block
		*/
		if (cowlo_readrdo(cowdev, cowdev->iobuf, MAPUNIT,
						(loff_t)i << MUSHIFT) < 1)
			break;

		/*
		** calculate fingerprint by adding all byte-values
		*/
		for (j=0, cs=0; j < MAPUNIT; j++)
			cs += *(cowdev->iobuf+j);

		if (cs == 0)	/* block probably contained zeroes */
			continue;

		/*
		** shift byte-value to proper place in final fingerprint
		*/
		cowdev->fingerprint |= cs << (nrval*8);
		nrval++;
	}

	return 0;
}

/*
** undo memory allocs and file opens issued so far
** related to the read-only file
*/
static void
cowlo_undo_openrdo(struct cowloop_device *cowdev)
{
	if(cowdev->iobuf);
		kfree(cowdev->iobuf);

	if (cowdev->rdofp)
  		filp_close(cowdev->rdofp, 0);
}

/*
** open the cowfile
**
** returns:
** 	0   - okay
**    < 0   - error value
*/
static int
cowlo_opencow(struct cowloop_device *cowdev, char *cowf, int autorecover)
{
	long int		i, rv;
	int			minor;
	unsigned long		nb;
	struct file		*f;
	struct inode		*inode;
	loff_t			offset;
	struct cowloop_device	*cowtmp;

	DEBUGP(DCOW"cowloop - opencow called\n");

	/*
	** open copy-on-write file (read-write)
	*/
        if (cowf[0] == '\0') {
          	printk(KERN_ERR
                 "cowloop - specify name of copy-on-write file\n\n");
         	return -EINVAL;
        }

	f = filp_open(cowf, O_RDWR|O_LARGEFILE, 0600);

	if ( (f == NULL) || IS_ERR(f) ) {
		/*
		** non-existing cowfile: try to create
		*/
		f = filp_open(cowf, O_RDWR|O_CREAT|O_LARGEFILE, 0600);

		if ( (f == NULL) || IS_ERR(f) ) {
			printk(KERN_ERR
		       	  "cowloop - failed to open file %s for read-write\n\n",
		       						cowf);
			return -EINVAL;
	 	}
	}

	cowdev->cowfp = f;

	inode = f->f_dentry->d_inode;

	if (!S_ISREG(inode->i_mode)) {
		printk(KERN_ERR "cowloop - %s is not regular file\n", cowf);
		return -EINVAL;
	}

	/*
	** check if this cowfile is already in use for another cowdevice
	*/
	for (minor = 0; minor < maxcows; minor++) {

		cowtmp = cowdevall[minor];

		if ( !(cowtmp->state & COWDEVOPEN) )
			continue;

		if (cowtmp == cowdev)
			continue;

		if (cowtmp->cowfp->f_dentry->d_inode == f->f_dentry->d_inode) {
			printk(KERN_ERR
			       "cowloop - %s: already in use as cow\n", cowf);
			return -EBUSY;
		}
	}

	/*
	** mark cowfile open for read-write
	*/
	cowdev->state |= COWRWCOWOPEN;

	/*
	** calculate size (in bytes) for total bitmap in cowfile;
	** when the size of the cowhead block is added, the start-offset
	** for the modified data blocks can be found
	*/
	nb = cowdev->numblocks;

	if (nb%8)		/* transform #bits to #bytes */
		nb+=8;  	/* rounded if necessary      */
	nb /= 8;

	if (nb & MUMASK)	/* round up #bytes to MAPUNIT chunks */
		cowdev->mapsize = ( (nb>>MUSHIFT) +1) << MUSHIFT;
	else
		cowdev->mapsize = nb;

	/*
	** reserve space in memory for the cowhead
	*/
	cowdev->cowhead = kmalloc(MAPUNIT, GFP_KERNEL);

	if (!cowdev->cowhead) {
		printk(KERN_ERR "cowloop - cannot get space for cowhead %d\n",
								     MAPUNIT);
		return -ENOMEM;
	}

	memset(cowdev->cowhead, 0, MAPUNIT);

	DEBUGP(DCOW"cowloop - prepare cowhead....\n");

	/*
	** check if the cowfile exists or should be created
	*/
	if (inode->i_size != 0) {
		/*
		** existing cowfile: read the cow head
		*/
		if (inode->i_size < MAPUNIT) {
			printk(KERN_ERR
			       "cowloop - existing cowfile %s too small\n",
				cowf);
			return -EINVAL;
		}

		cowlo_readcowraw(cowdev, cowdev->cowhead, MAPUNIT, (loff_t) 0);

		/*
		** verify if the existing file is really a cowfile
		*/
		if (cowdev->cowhead->magic != COWMAGIC) {
			printk(KERN_ERR
			       "cowloop - cowfile %s has incorrect format\n",
				cowf);
			return -EINVAL;
		}

		/*
		** verify the cowhead version of the cowfile
		*/
		if (cowdev->cowhead->version > COWVERSION) {
			printk(KERN_ERR
			       "cowloop - cowfile %s newer than this driver\n",
				cowf);
			return -EINVAL;
		}

		/*
		** make sure that this is not a packed cowfile
		*/
		if (cowdev->cowhead->flags & COWPACKED) {
			printk(KERN_ERR
			    "cowloop - packed cowfile %s not accepted\n", cowf);
			return -EINVAL;
		}

		/*
		** verify if the cowfile has been properly closed
		*/
		if (cowdev->cowhead->flags & COWDIRTY) {
			/*
			** cowfile was not properly closed;
			** check if automatic recovery is required
			** (actual recovery will be done later on)
			*/
			if (!autorecover) {
				printk(KERN_ERR
				       "cowloop - cowfile %s is dirty "
				       "(not properly closed by rmmod?)\n",
					cowf);
				printk(KERN_ERR
				       "cowloop - run cowrepair or specify "
				       "'option=r' to recover\n");
				return -EINVAL;
			}
		}

		/*
		** verify if the cowfile is really related to this rdofile
		*/
		if (cowdev->cowhead->rdoblocks != cowdev->numblocks) {
			printk(KERN_ERR
		       	       "cowloop - cowfile %s (size %lld) not related "
		       	       "to rdofile (size %lld)\n",
				cowf,
				(long long)cowdev->cowhead->rdoblocks <<MUSHIFT,
				(long long)cowdev->numblocks <<MUSHIFT);
			return -EINVAL;
		}

		if (cowdev->cowhead->rdofingerprint != cowdev->fingerprint) {
			printk(KERN_ERR
		       	     "cowloop - cowfile %s not related to rdofile "
			     " (fingerprint err - rdofile modified?)\n", cowf);
			return -EINVAL;
		}
	} else {
		/*
		** new cowfile: determine the minimal size (cowhead+bitmap)
		*/
		offset = (loff_t) MAPUNIT + cowdev->mapsize - 1;

		if ( cowlo_writecowraw(cowdev, "", 1, offset) < 1) {
			printk(KERN_ERR
			       "cowloop - cannot set cowfile to size %lld\n",
				offset+1);
			return -EINVAL;
		}

		/*
		** prepare new cowhead
		*/
		cowdev->cowhead->magic		= COWMAGIC;
		cowdev->cowhead->version	= COWVERSION;
		cowdev->cowhead->mapunit	= MAPUNIT;
		cowdev->cowhead->mapsize	= cowdev->mapsize;
		cowdev->cowhead->rdoblocks	= cowdev->numblocks;
		cowdev->cowhead->rdofingerprint	= cowdev->fingerprint;
		cowdev->cowhead->cowused	= 0;

		/*
		** calculate start offset of data in cowfile,
		** rounded up to multiple of 4K to avoid
		** unnecessary disk-usage for written datablocks in
		** the sparsed cowfile on e.g. 4K filesystems
		*/
		cowdev->cowhead->doffset =
			((MAPUNIT+cowdev->mapsize+4095)>>12)<<12;
	}

	cowdev->cowhead->flags	= 0;

	DEBUGP(DCOW"cowloop - reserve space bitmap....\n");

	/*
	** reserve space in memory for the entire bitmap and
	** fill it with the bitmap-data from disk; the entire
	** bitmap is allocated in several chunks because kmalloc
	** has restrictions regarding the allowed size per kmalloc
	*/
	cowdev->mapcount = (cowdev->mapsize+MAPCHUNKSZ-1)/MAPCHUNKSZ;

	/*
	** the size of every bitmap chunk will be MAPCHUNKSZ bytes, except for
	** the last bitmap chunk: calculate remaining size for this chunk
	*/
	if (cowdev->mapsize % MAPCHUNKSZ == 0)
		cowdev->mapremain = MAPCHUNKSZ;
	else
		cowdev->mapremain = cowdev->mapsize % MAPCHUNKSZ;

	/*
	** allocate space to store all pointers for the bitmap-chunks
	** (initialize area with zeroes to allow proper undo)
	*/
	cowdev->mapcache = kmalloc(cowdev->mapcount * sizeof(char *),
								GFP_KERNEL);
	if (!cowdev->mapcache) {
		printk(KERN_ERR
		       "cowloop - can not allocate space for bitmap ptrs\n");
		return -ENOMEM;
	}

	memset(cowdev->mapcache, 0, cowdev->mapcount * sizeof(char *));

	/*
	** allocate space to store the bitmap-chunks themselves
	*/
	for (i=0; i < cowdev->mapcount; i++) {
		if (i < (cowdev->mapcount-1))
			*(cowdev->mapcache+i) = kmalloc(MAPCHUNKSZ, GFP_KERNEL);
		else
			*(cowdev->mapcache+i) = kmalloc(cowdev->mapremain,
						                  GFP_KERNEL);

		if (*(cowdev->mapcache+i) == NULL) {
			printk(KERN_ERR "cowloop - no space for bitmapchunk %ld"
					" totmapsz=%ld, mapcnt=%d mapunit=%d\n",
					i, cowdev->mapsize, cowdev->mapcount,
					MAPUNIT);
			return -ENOMEM;
		}
	}

	DEBUGP(DCOW"cowloop - read bitmap from cow....\n");

	/*
	** read the entire bitmap from the cowfile into the in-memory cache;
	** count the number of blocks that are in use already
	** (statistical purposes)
	*/
	for (i=0, offset=MAPUNIT; i < cowdev->mapcount;
					i++, offset+=MAPCHUNKSZ) {
		unsigned long	numbytes;

		if (i < (cowdev->mapcount-1))
			/*
			** full bitmap chunk
			*/
			numbytes = MAPCHUNKSZ;
		else
			/*
			** last bitmap chunk: might be partly filled
			*/
			numbytes = cowdev->mapremain;

		cowlo_readcowraw(cowdev, *(cowdev->mapcache+i),
							numbytes, offset);
	}

	/*
	** if the cowfile was dirty and automatic recovery is required,
	** reconstruct a proper bitmap in memory now
	*/
	if (cowdev->cowhead->flags & COWDIRTY) {
		unsigned long long	blocknum;
		char			databuf[MAPUNIT];
		unsigned long		mapnum, mapbyte, mapbit;

		printk(KERN_NOTICE "cowloop - recover dirty cowfile %s....\n",
							cowf);

		/*
		** read all data blocks
		*/
		for (blocknum=0, rv=1, offset=0;
			cowlo_readcow(cowdev, databuf, MAPUNIT, offset) > 0;
			blocknum++, offset += MAPUNIT) {

			/*
			** if this datablock contains real data (not binary
			** zeroes), set the corresponding bit in the bitmap
			*/
			if ( memcmp(databuf, allzeroes, MAPUNIT) == 0)
				continue;

			mapnum  = CALCMAP (blocknum);
			mapbyte = CALCBYTE(blocknum);
			mapbit  = CALCBIT (blocknum);

			*(*(cowdev->mapcache+mapnum)+mapbyte) |= (1<<mapbit);
		}

		printk(KERN_NOTICE "cowloop - cowfile recovery completed\n");
	}

	/*
	** count all bits set in the bitmaps for statistical purposes
	*/
	for (i=0, cowdev->nrcowblocks = 0; i < cowdev->mapcount; i++) {
		long	numbytes;
		char	*p;

		if (i < (cowdev->mapcount-1))
			numbytes = MAPCHUNKSZ;
		else
			numbytes = cowdev->mapremain;

		p = *(cowdev->mapcache+i);

		for (numbytes--; numbytes >= 0; numbytes--, p++) {
			/*
			** for only eight checks the following construction
			** is faster than a loop-construction
			*/
			if ((*p) & 0x01)	cowdev->nrcowblocks++;
			if ((*p) & 0x02)	cowdev->nrcowblocks++;
			if ((*p) & 0x04)	cowdev->nrcowblocks++;
			if ((*p) & 0x08)	cowdev->nrcowblocks++;
			if ((*p) & 0x10)	cowdev->nrcowblocks++;
			if ((*p) & 0x20)	cowdev->nrcowblocks++;
			if ((*p) & 0x40)	cowdev->nrcowblocks++;
			if ((*p) & 0x80)	cowdev->nrcowblocks++;
		}
	}

	/*
	** consistency-check for number of bits set in bitmap
	*/
	if ( !(cowdev->cowhead->flags & COWDIRTY) &&
	    (cowdev->cowhead->cowused != cowdev->nrcowblocks) ) {
		printk(KERN_ERR "cowloop - inconsistent cowfile admi\n");
		return -EINVAL;
	}

	return 0;
}

/*
** undo memory allocs and file opens issued so far
** related to the cowfile
*/
static void
cowlo_undo_opencow(struct cowloop_device *cowdev)
{
	int	i;

	if (cowdev->mapcache) {
		for (i=0; i < cowdev->mapcount; i++) {
			if (*(cowdev->mapcache+i) != NULL)
				kfree( *(cowdev->mapcache+i) );
		}

		kfree(cowdev->mapcache);
	}

	if (cowdev->cowhead)
		kfree(cowdev->cowhead);

	if ( (cowdev->state & COWCOWOPEN) && (cowdev->cowfp) )
  		filp_close(cowdev->cowfp, 0);

	/*
	** mark cowfile closed
	*/
	cowdev->state &= ~COWCOWOPEN;
}

/*
** flush the entire bitmap and the cowhead (clean) to the cowfile
**
** must be called with the cowdevices-lock set
*/
static void
cowlo_sync(void)
{
	int			i, minor;
	loff_t			offset;
	struct cowloop_device	*cowdev;

	for (minor=0; minor < maxcows;  minor++) {
		cowdev = cowdevall[minor];
		if ( ! (cowdev->state & COWRWCOWOPEN) )
			continue;

		for (i=0, offset=MAPUNIT; i < cowdev->mapcount;
					i++, offset += MAPCHUNKSZ) {
			unsigned long	numbytes;

			if (i < (cowdev->mapcount-1))
				/*
				** full bitmap chunk
				*/
				numbytes = MAPCHUNKSZ;
			else
				/*
				** last bitmap chunk: might be partly filled
				*/
				numbytes = cowdev->mapremain;

			DEBUGP(DCOW
			       "cowloop - flushing bitmap %2d (%3ld Kb)\n",
							i, numbytes/1024);

			if (cowlo_writecowraw(cowdev, *(cowdev->mapcache+i),
						numbytes, offset) < numbytes) {
				break;
			}
		}

		/*
		** flush clean up-to-date cowhead to cowfile
		*/
		cowdev->cowhead->cowused	 = cowdev->nrcowblocks;
		cowdev->cowhead->flags		&= ~COWDIRTY;

		DEBUGP(DCOW "cowloop - flushing cowhead (%3d Kb)\n",
							MAPUNIT/1024);

		cowlo_writecowraw(cowdev, cowdev->cowhead, MAPUNIT, (loff_t) 0);
	}
}

/*****************************************************************************/
/* Module loading/unloading                                                  */
/*****************************************************************************/

/*
** called during insmod/modprobe
*/
static int __init
cowlo_init_module(void)
{
	int	rv;
	int	minor, uptocows;

        revision[sizeof revision - 3] = '\0';

        printk(KERN_NOTICE "cowloop - (C) 2009 ATComputing.nl - version: %s\n", &revision[11]);
        printk(KERN_NOTICE "cowloop - info: www.ATComputing.nl/cowloop\n");

	memset(allzeroes, 0, MAPUNIT);

	/*
	** Setup administration for all possible cowdevices.
        ** Note that their minor numbers go from 0 to MAXCOWS-1 inclusive
        ** and minor == MAXCOWS-1 is reserved for the control device.
	*/
	if ((maxcows < 1) || (maxcows > MAXCOWS)) {
		printk(KERN_WARNING
		       "cowloop - maxcows exceeds maximum of %d\n", MAXCOWS);

                maxcows = DFLCOWS;
        }

	/* allocate room for a table with a pointer to each cowloop_device: */
        if ( (cowdevall = kmalloc(maxcows * sizeof(struct cowloop_device *),
							GFP_KERNEL)) == NULL) {
		printk(KERN_WARNING
		        "cowloop - can not alloc table for %d devs\n", maxcows);
		uptocows = 0;
		rv = -ENOMEM;
		goto error_out;
	}
	memset(cowdevall, 0, maxcows * sizeof(struct cowloop_device *));
	/* then hook an actual cowloop_device struct to each pointer: */
	for (minor=0; minor < maxcows; minor++) {
		if ((cowdevall[minor] = kmalloc(sizeof(struct cowloop_device),
						GFP_KERNEL)) == NULL) {
			printk(KERN_WARNING
		           "cowloop - can not alloc admin-struct for dev no %d\n", minor);

			uptocows = minor; /* this is how far we got.... */
			rv = -ENOMEM;
			goto error_out;
		}
        	memset(cowdevall[minor], 0, sizeof(struct cowloop_device));
	}
	uptocows = maxcows; /* we got all devices */

	sema_init(&cowdevlock, 1);

	/*
	** register cowloop module
	*/
	if ( register_blkdev(COWMAJOR, DEVICE_NAME) < 0) {
		printk(KERN_WARNING
		    "cowloop - unable to get major %d for cowloop\n", COWMAJOR);
		rv = -EIO;
		goto error_out;
	}

	/*
	** create a directory below /proc to allocate a file
	** for each cowdevice that is allocated later on
	*/
	cowlo_procdir = proc_mkdir("cow", NULL);

	/*
	** check if a cowdevice has to be opened during insmod/modprobe;
	** two parameters should be specified then: rdofile= and cowfile=
	*/
	if( (rdofile[0] != '\0') && (cowfile[0] != '\0') ) {
		char	*po = option;
		int	wantrecover = 0;

		/*
		** check if automatic recovery is wanted
		*/
		while (*po) {
			if (*po == 'r') {
				wantrecover = 1;
				break;
                        }
			po++;
		}

		/*
		** open new cowdevice with minor number 0
		*/
		if ( (rv = cowlo_openpair(rdofile, cowfile, wantrecover, 0))) {
			remove_proc_entry("cow", NULL);
			unregister_blkdev(COWMAJOR, DEVICE_NAME);
			goto error_out;
		}
        } else {
		/*
		** check if only one parameter has been specified
		*/
		if( (rdofile[0] != '\0') || (cowfile[0] != '\0') ) {
			printk(KERN_ERR
			       "cowloop - only one filename specified\n");
			remove_proc_entry("cow", NULL);
			unregister_blkdev(COWMAJOR, DEVICE_NAME);
			rv = -EINVAL;
			goto error_out;
		}
	}

	/*
	** allocate fake disk as control channel to handle the requests
	** to activate and deactivate cowdevices dynamically
	*/
	if (!(cowctlgd = alloc_disk(1))) {
		printk(KERN_WARNING
		       "cowloop - unable to alloc_disk for cowctl\n");

		remove_proc_entry("cow", NULL);
		(void) cowlo_closepair(cowdevall[0]);
		unregister_blkdev(COWMAJOR, DEVICE_NAME);
		rv = -ENOMEM;
		goto error_out;
	}

	spin_lock_init(&cowctlrqlock);
	cowctlgd->major        = COWMAJOR;
	cowctlgd->first_minor  = COWCTL;
	cowctlgd->minors       = 1;
	cowctlgd->fops         = &cowlo_fops;
	cowctlgd->private_data = NULL;
	/* the device has capacity 0, so there will be no q-requests */
	cowctlgd->queue = blk_init_queue(NULL, &cowctlrqlock);
	sprintf(cowctlgd->disk_name, "cowctl");
	set_capacity(cowctlgd, 0);

	add_disk(cowctlgd);

        printk(KERN_NOTICE "cowloop - number of configured cowdevices: %d\n",
								maxcows);
	if (rdofile[0] != '\0') {
	    printk(KERN_NOTICE "cowloop - initialized on rdofile=%s\n",
								rdofile);
	} else {
	    printk(KERN_NOTICE "cowloop - initialized without rdofile yet\n");
	}
	return 0;

error_out:
	for (minor=0; minor < uptocows ; minor++) {
		kfree(cowdevall[minor]);
	}
	kfree(cowdevall);
	return rv;
}

/*
** called during rmmod
*/
static void __exit
cowlo_cleanup_module(void)
{
	int	minor;

	/*
	** flush bitmaps and cowheads to the cowfiles
	*/
	down(&cowdevlock);
	cowlo_sync();
	up(&cowdevlock);

	/*
	** close all cowdevices
	*/
	for (minor=0; minor < maxcows;  minor++)
		(void) cowlo_closepair(cowdevall[minor]);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23))
	unregister_blkdev(COWMAJOR, DEVICE_NAME);
#else
	if (unregister_blkdev(COWMAJOR, DEVICE_NAME) != 0)
		printk(KERN_WARNING "cowloop - cannot unregister blkdev\n");
#endif

	/*
	** get rid of /proc/cow and unregister the driver
	*/
	remove_proc_entry("cow", NULL);

	for (minor = 0; minor < maxcows; minor++) {
		kfree(cowdevall[minor]);
	}
	kfree(cowdevall);

	del_gendisk(cowctlgd);  /* revert the alloc_disk() */
	put_disk   (cowctlgd);  /* revert the add_disk()   */
	blk_cleanup_queue(cowctlgd->queue); /* cleanup the empty queue */

	printk(KERN_NOTICE "cowloop - unloaded\n");
}

module_init(cowlo_init_module);
module_exit(cowlo_cleanup_module);
