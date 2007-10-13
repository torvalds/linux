/* 
        pd.c    (c) 1997-8  Grant R. Guenther <grant@torque.net>
                            Under the terms of the GNU General Public License.

        This is the high-level driver for parallel port IDE hard
        drives based on chips supported by the paride module.

	By default, the driver will autoprobe for a single parallel
	port IDE drive, but if their individual parameters are
        specified, the driver can handle up to 4 drives.

        The behaviour of the pd driver can be altered by setting
        some parameters from the insmod command line.  The following
        parameters are adjustable:
 
	    drive0  	These four arguments can be arrays of	    
	    drive1	1-8 integers as follows:
	    drive2
	    drive3	<prt>,<pro>,<uni>,<mod>,<geo>,<sby>,<dly>,<slv>

			Where,

		<prt>	is the base of the parallel port address for
			the corresponding drive.  (required)

		<pro>   is the protocol number for the adapter that
			supports this drive.  These numbers are
                        logged by 'paride' when the protocol modules
			are initialised.  (0 if not given)

		<uni>   for those adapters that support chained
			devices, this is the unit selector for the
		        chain of devices on the given port.  It should
			be zero for devices that don't support chaining.
			(0 if not given)

		<mod>   this can be -1 to choose the best mode, or one
		        of the mode numbers supported by the adapter.
			(-1 if not given)

		<geo>   this defaults to 0 to indicate that the driver
			should use the CHS geometry provided by the drive
			itself.  If set to 1, the driver will provide
			a logical geometry with 64 heads and 32 sectors
			per track, to be consistent with most SCSI
		        drivers.  (0 if not given)

		<sby>   set this to zero to disable the power saving
			standby mode, if needed.  (1 if not given)

		<dly>   some parallel ports require the driver to 
			go more slowly.  -1 sets a default value that
			should work with the chosen protocol.  Otherwise,
			set this to a small integer, the larger it is
			the slower the port i/o.  In some cases, setting
			this to zero will speed up the device. (default -1)

		<slv>   IDE disks can be jumpered to master or slave.
                        Set this to 0 to choose the master drive, 1 to
                        choose the slave, -1 (the default) to choose the
                        first drive found.
			

            major       You may use this parameter to overide the
                        default major number (45) that this driver
                        will use.  Be sure to change the device
                        name as well.

            name        This parameter is a character string that
                        contains the name the kernel will use for this
                        device (in /proc output, for instance).
			(default "pd")

	    cluster	The driver will attempt to aggregate requests
			for adjacent blocks into larger multi-block
			clusters.  The maximum cluster size (in 512
			byte sectors) is set with this parameter.
			(default 64)

	    verbose	This parameter controls the amount of logging
			that the driver will do.  Set it to 0 for 
			normal operation, 1 to see autoprobe progress
			messages, or 2 to see additional debugging
			output.  (default 0)

            nice        This parameter controls the driver's use of
                        idle CPU time, at the expense of some speed.

        If this driver is built into the kernel, you can use kernel
        the following command line parameters, with the same values
        as the corresponding module parameters listed above:

            pd.drive0
            pd.drive1
            pd.drive2
            pd.drive3
            pd.cluster
            pd.nice

        In addition, you can use the parameter pd.disable to disable
        the driver entirely.
 
*/

/* Changes:

	1.01	GRG 1997.01.24	Restored pd_reset()
				Added eject ioctl
	1.02    GRG 1998.05.06  SMP spinlock changes, 
				Added slave support
	1.03    GRG 1998.06.16  Eliminate an Ugh.
	1.04	GRG 1998.08.15  Extra debugging, use HZ in loop timing
	1.05    GRG 1998.09.24  Added jumbo support

*/

#define PD_VERSION      "1.05"
#define PD_MAJOR	45
#define PD_NAME		"pd"
#define PD_UNITS	4

/* Here are things one can override from the insmod command.
   Most are autoprobed by paride unless set here.  Verbose is off
   by default.

*/

static int verbose = 0;
static int major = PD_MAJOR;
static char *name = PD_NAME;
static int cluster = 64;
static int nice = 0;
static int disable = 0;

static int drive0[8] = { 0, 0, 0, -1, 0, 1, -1, -1 };
static int drive1[8] = { 0, 0, 0, -1, 0, 1, -1, -1 };
static int drive2[8] = { 0, 0, 0, -1, 0, 1, -1, -1 };
static int drive3[8] = { 0, 0, 0, -1, 0, 1, -1, -1 };

static int (*drives[4])[8] = {&drive0, &drive1, &drive2, &drive3};

enum {D_PRT, D_PRO, D_UNI, D_MOD, D_GEO, D_SBY, D_DLY, D_SLV};

/* end of parameters */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/cdrom.h>	/* for the eject ioctl */
#include <linux/blkdev.h>
#include <linux/blkpg.h>
#include <linux/kernel.h>
#include <asm/uaccess.h>
#include <linux/workqueue.h>

static DEFINE_SPINLOCK(pd_lock);

module_param(verbose, bool, 0);
module_param(major, int, 0);
module_param(name, charp, 0);
module_param(cluster, int, 0);
module_param(nice, int, 0);
module_param_array(drive0, int, NULL, 0);
module_param_array(drive1, int, NULL, 0);
module_param_array(drive2, int, NULL, 0);
module_param_array(drive3, int, NULL, 0);

#include "paride.h"

#define PD_BITS    4

/* numbers for "SCSI" geometry */

#define PD_LOG_HEADS    64
#define PD_LOG_SECTS    32

#define PD_ID_OFF       54
#define PD_ID_LEN       14

#define PD_MAX_RETRIES  5
#define PD_TMO          800	/* interrupt timeout in jiffies */
#define PD_SPIN_DEL     50	/* spin delay in micro-seconds  */

#define PD_SPIN         (1000000*PD_TMO)/(HZ*PD_SPIN_DEL)

#define STAT_ERR        0x00001
#define STAT_INDEX      0x00002
#define STAT_ECC        0x00004
#define STAT_DRQ        0x00008
#define STAT_SEEK       0x00010
#define STAT_WRERR      0x00020
#define STAT_READY      0x00040
#define STAT_BUSY       0x00080

#define ERR_AMNF        0x00100
#define ERR_TK0NF       0x00200
#define ERR_ABRT        0x00400
#define ERR_MCR         0x00800
#define ERR_IDNF        0x01000
#define ERR_MC          0x02000
#define ERR_UNC         0x04000
#define ERR_TMO         0x10000

#define IDE_READ        	0x20
#define IDE_WRITE       	0x30
#define IDE_READ_VRFY		0x40
#define IDE_INIT_DEV_PARMS	0x91
#define IDE_STANDBY     	0x96
#define IDE_ACKCHANGE   	0xdb
#define IDE_DOORLOCK    	0xde
#define IDE_DOORUNLOCK  	0xdf
#define IDE_IDENTIFY    	0xec
#define IDE_EJECT		0xed

#define PD_NAMELEN	8

struct pd_unit {
	struct pi_adapter pia;	/* interface to paride layer */
	struct pi_adapter *pi;
	int access;		/* count of active opens ... */
	int capacity;		/* Size of this volume in sectors */
	int heads;		/* physical geometry */
	int sectors;
	int cylinders;
	int can_lba;
	int drive;		/* master=0 slave=1 */
	int changed;		/* Have we seen a disk change ? */
	int removable;		/* removable media device  ?  */
	int standby;
	int alt_geom;
	char name[PD_NAMELEN];	/* pda, pdb, etc ... */
	struct gendisk *gd;
};

static struct pd_unit pd[PD_UNITS];

static char pd_scratch[512];	/* scratch block buffer */

static char *pd_errs[17] = { "ERR", "INDEX", "ECC", "DRQ", "SEEK", "WRERR",
	"READY", "BUSY", "AMNF", "TK0NF", "ABRT", "MCR",
	"IDNF", "MC", "UNC", "???", "TMO"
};

static inline int status_reg(struct pd_unit *disk)
{
	return pi_read_regr(disk->pi, 1, 6);
}

static inline int read_reg(struct pd_unit *disk, int reg)
{
	return pi_read_regr(disk->pi, 0, reg);
}

static inline void write_status(struct pd_unit *disk, int val)
{
	pi_write_regr(disk->pi, 1, 6, val);
}

static inline void write_reg(struct pd_unit *disk, int reg, int val)
{
	pi_write_regr(disk->pi, 0, reg, val);
}

static inline u8 DRIVE(struct pd_unit *disk)
{
	return 0xa0+0x10*disk->drive;
}

/*  ide command interface */

static void pd_print_error(struct pd_unit *disk, char *msg, int status)
{
	int i;

	printk("%s: %s: status = 0x%x =", disk->name, msg, status);
	for (i = 0; i < ARRAY_SIZE(pd_errs); i++)
		if (status & (1 << i))
			printk(" %s", pd_errs[i]);
	printk("\n");
}

static void pd_reset(struct pd_unit *disk)
{				/* called only for MASTER drive */
	write_status(disk, 4);
	udelay(50);
	write_status(disk, 0);
	udelay(250);
}

#define DBMSG(msg)	((verbose>1)?(msg):NULL)

static int pd_wait_for(struct pd_unit *disk, int w, char *msg)
{				/* polled wait */
	int k, r, e;

	k = 0;
	while (k < PD_SPIN) {
		r = status_reg(disk);
		k++;
		if (((r & w) == w) && !(r & STAT_BUSY))
			break;
		udelay(PD_SPIN_DEL);
	}
	e = (read_reg(disk, 1) << 8) + read_reg(disk, 7);
	if (k >= PD_SPIN)
		e |= ERR_TMO;
	if ((e & (STAT_ERR | ERR_TMO)) && (msg != NULL))
		pd_print_error(disk, msg, e);
	return e;
}

static void pd_send_command(struct pd_unit *disk, int n, int s, int h, int c0, int c1, int func)
{
	write_reg(disk, 6, DRIVE(disk) + h);
	write_reg(disk, 1, 0);		/* the IDE task file */
	write_reg(disk, 2, n);
	write_reg(disk, 3, s);
	write_reg(disk, 4, c0);
	write_reg(disk, 5, c1);
	write_reg(disk, 7, func);

	udelay(1);
}

static void pd_ide_command(struct pd_unit *disk, int func, int block, int count)
{
	int c1, c0, h, s;

	if (disk->can_lba) {
		s = block & 255;
		c0 = (block >>= 8) & 255;
		c1 = (block >>= 8) & 255;
		h = ((block >>= 8) & 15) + 0x40;
	} else {
		s = (block % disk->sectors) + 1;
		h = (block /= disk->sectors) % disk->heads;
		c0 = (block /= disk->heads) % 256;
		c1 = (block >>= 8);
	}
	pd_send_command(disk, count, s, h, c0, c1, func);
}

/* The i/o request engine */

enum action {Fail = 0, Ok = 1, Hold, Wait};

static struct request *pd_req;	/* current request */
static enum action (*phase)(void);

static void run_fsm(void);

static void ps_tq_int(struct work_struct *work);

static DECLARE_DELAYED_WORK(fsm_tq, ps_tq_int);

static void schedule_fsm(void)
{
	if (!nice)
		schedule_delayed_work(&fsm_tq, 0);
	else
		schedule_delayed_work(&fsm_tq, nice-1);
}

static void ps_tq_int(struct work_struct *work)
{
	run_fsm();
}

static enum action do_pd_io_start(void);
static enum action pd_special(void);
static enum action do_pd_read_start(void);
static enum action do_pd_write_start(void);
static enum action do_pd_read_drq(void);
static enum action do_pd_write_done(void);

static struct request_queue *pd_queue;
static int pd_claimed;

static struct pd_unit *pd_current; /* current request's drive */
static PIA *pi_current; /* current request's PIA */

static void run_fsm(void)
{
	while (1) {
		enum action res;
		unsigned long saved_flags;
		int stop = 0;

		if (!phase) {
			pd_current = pd_req->rq_disk->private_data;
			pi_current = pd_current->pi;
			phase = do_pd_io_start;
		}

		switch (pd_claimed) {
			case 0:
				pd_claimed = 1;
				if (!pi_schedule_claimed(pi_current, run_fsm))
					return;
			case 1:
				pd_claimed = 2;
				pi_current->proto->connect(pi_current);
		}

		switch(res = phase()) {
			case Ok: case Fail:
				pi_disconnect(pi_current);
				pd_claimed = 0;
				phase = NULL;
				spin_lock_irqsave(&pd_lock, saved_flags);
				end_request(pd_req, res);
				pd_req = elv_next_request(pd_queue);
				if (!pd_req)
					stop = 1;
				spin_unlock_irqrestore(&pd_lock, saved_flags);
				if (stop)
					return;
			case Hold:
				schedule_fsm();
				return;
			case Wait:
				pi_disconnect(pi_current);
				pd_claimed = 0;
		}
	}
}

static int pd_retries = 0;	/* i/o error retry count */
static int pd_block;		/* address of next requested block */
static int pd_count;		/* number of blocks still to do */
static int pd_run;		/* sectors in current cluster */
static int pd_cmd;		/* current command READ/WRITE */
static char *pd_buf;		/* buffer for request in progress */

static enum action do_pd_io_start(void)
{
	if (blk_special_request(pd_req)) {
		phase = pd_special;
		return pd_special();
	}

	pd_cmd = rq_data_dir(pd_req);
	if (pd_cmd == READ || pd_cmd == WRITE) {
		pd_block = pd_req->sector;
		pd_count = pd_req->current_nr_sectors;
		if (pd_block + pd_count > get_capacity(pd_req->rq_disk))
			return Fail;
		pd_run = pd_req->nr_sectors;
		pd_buf = pd_req->buffer;
		pd_retries = 0;
		if (pd_cmd == READ)
			return do_pd_read_start();
		else
			return do_pd_write_start();
	}
	return Fail;
}

static enum action pd_special(void)
{
	enum action (*func)(struct pd_unit *) = pd_req->special;
	return func(pd_current);
}

static int pd_next_buf(void)
{
	unsigned long saved_flags;

	pd_count--;
	pd_run--;
	pd_buf += 512;
	pd_block++;
	if (!pd_run)
		return 1;
	if (pd_count)
		return 0;
	spin_lock_irqsave(&pd_lock, saved_flags);
	end_request(pd_req, 1);
	pd_count = pd_req->current_nr_sectors;
	pd_buf = pd_req->buffer;
	spin_unlock_irqrestore(&pd_lock, saved_flags);
	return 0;
}

static unsigned long pd_timeout;

static enum action do_pd_read_start(void)
{
	if (pd_wait_for(pd_current, STAT_READY, "do_pd_read") & STAT_ERR) {
		if (pd_retries < PD_MAX_RETRIES) {
			pd_retries++;
			return Wait;
		}
		return Fail;
	}
	pd_ide_command(pd_current, IDE_READ, pd_block, pd_run);
	phase = do_pd_read_drq;
	pd_timeout = jiffies + PD_TMO;
	return Hold;
}

static enum action do_pd_write_start(void)
{
	if (pd_wait_for(pd_current, STAT_READY, "do_pd_write") & STAT_ERR) {
		if (pd_retries < PD_MAX_RETRIES) {
			pd_retries++;
			return Wait;
		}
		return Fail;
	}
	pd_ide_command(pd_current, IDE_WRITE, pd_block, pd_run);
	while (1) {
		if (pd_wait_for(pd_current, STAT_DRQ, "do_pd_write_drq") & STAT_ERR) {
			if (pd_retries < PD_MAX_RETRIES) {
				pd_retries++;
				return Wait;
			}
			return Fail;
		}
		pi_write_block(pd_current->pi, pd_buf, 512);
		if (pd_next_buf())
			break;
	}
	phase = do_pd_write_done;
	pd_timeout = jiffies + PD_TMO;
	return Hold;
}

static inline int pd_ready(void)
{
	return !(status_reg(pd_current) & STAT_BUSY);
}

static enum action do_pd_read_drq(void)
{
	if (!pd_ready() && !time_after_eq(jiffies, pd_timeout))
		return Hold;

	while (1) {
		if (pd_wait_for(pd_current, STAT_DRQ, "do_pd_read_drq") & STAT_ERR) {
			if (pd_retries < PD_MAX_RETRIES) {
				pd_retries++;
				phase = do_pd_read_start;
				return Wait;
			}
			return Fail;
		}
		pi_read_block(pd_current->pi, pd_buf, 512);
		if (pd_next_buf())
			break;
	}
	return Ok;
}

static enum action do_pd_write_done(void)
{
	if (!pd_ready() && !time_after_eq(jiffies, pd_timeout))
		return Hold;

	if (pd_wait_for(pd_current, STAT_READY, "do_pd_write_done") & STAT_ERR) {
		if (pd_retries < PD_MAX_RETRIES) {
			pd_retries++;
			phase = do_pd_write_start;
			return Wait;
		}
		return Fail;
	}
	return Ok;
}

/* special io requests */

/* According to the ATA standard, the default CHS geometry should be
   available following a reset.  Some Western Digital drives come up
   in a mode where only LBA addresses are accepted until the device
   parameters are initialised.
*/

static void pd_init_dev_parms(struct pd_unit *disk)
{
	pd_wait_for(disk, 0, DBMSG("before init_dev_parms"));
	pd_send_command(disk, disk->sectors, 0, disk->heads - 1, 0, 0,
			IDE_INIT_DEV_PARMS);
	udelay(300);
	pd_wait_for(disk, 0, "Initialise device parameters");
}

static enum action pd_door_lock(struct pd_unit *disk)
{
	if (!(pd_wait_for(disk, STAT_READY, "Lock") & STAT_ERR)) {
		pd_send_command(disk, 1, 0, 0, 0, 0, IDE_DOORLOCK);
		pd_wait_for(disk, STAT_READY, "Lock done");
	}
	return Ok;
}

static enum action pd_door_unlock(struct pd_unit *disk)
{
	if (!(pd_wait_for(disk, STAT_READY, "Lock") & STAT_ERR)) {
		pd_send_command(disk, 1, 0, 0, 0, 0, IDE_DOORUNLOCK);
		pd_wait_for(disk, STAT_READY, "Lock done");
	}
	return Ok;
}

static enum action pd_eject(struct pd_unit *disk)
{
	pd_wait_for(disk, 0, DBMSG("before unlock on eject"));
	pd_send_command(disk, 1, 0, 0, 0, 0, IDE_DOORUNLOCK);
	pd_wait_for(disk, 0, DBMSG("after unlock on eject"));
	pd_wait_for(disk, 0, DBMSG("before eject"));
	pd_send_command(disk, 0, 0, 0, 0, 0, IDE_EJECT);
	pd_wait_for(disk, 0, DBMSG("after eject"));
	return Ok;
}

static enum action pd_media_check(struct pd_unit *disk)
{
	int r = pd_wait_for(disk, STAT_READY, DBMSG("before media_check"));
	if (!(r & STAT_ERR)) {
		pd_send_command(disk, 1, 1, 0, 0, 0, IDE_READ_VRFY);
		r = pd_wait_for(disk, STAT_READY, DBMSG("RDY after READ_VRFY"));
	} else
		disk->changed = 1;	/* say changed if other error */
	if (r & ERR_MC) {
		disk->changed = 1;
		pd_send_command(disk, 1, 0, 0, 0, 0, IDE_ACKCHANGE);
		pd_wait_for(disk, STAT_READY, DBMSG("RDY after ACKCHANGE"));
		pd_send_command(disk, 1, 1, 0, 0, 0, IDE_READ_VRFY);
		r = pd_wait_for(disk, STAT_READY, DBMSG("RDY after VRFY"));
	}
	return Ok;
}

static void pd_standby_off(struct pd_unit *disk)
{
	pd_wait_for(disk, 0, DBMSG("before STANDBY"));
	pd_send_command(disk, 0, 0, 0, 0, 0, IDE_STANDBY);
	pd_wait_for(disk, 0, DBMSG("after STANDBY"));
}

static enum action pd_identify(struct pd_unit *disk)
{
	int j;
	char id[PD_ID_LEN + 1];

/* WARNING:  here there may be dragons.  reset() applies to both drives,
   but we call it only on probing the MASTER. This should allow most
   common configurations to work, but be warned that a reset can clear
   settings on the SLAVE drive.
*/

	if (disk->drive == 0)
		pd_reset(disk);

	write_reg(disk, 6, DRIVE(disk));
	pd_wait_for(disk, 0, DBMSG("before IDENT"));
	pd_send_command(disk, 1, 0, 0, 0, 0, IDE_IDENTIFY);

	if (pd_wait_for(disk, STAT_DRQ, DBMSG("IDENT DRQ")) & STAT_ERR)
		return Fail;
	pi_read_block(disk->pi, pd_scratch, 512);
	disk->can_lba = pd_scratch[99] & 2;
	disk->sectors = le16_to_cpu(*(__le16 *) (pd_scratch + 12));
	disk->heads = le16_to_cpu(*(__le16 *) (pd_scratch + 6));
	disk->cylinders = le16_to_cpu(*(__le16 *) (pd_scratch + 2));
	if (disk->can_lba)
		disk->capacity = le32_to_cpu(*(__le32 *) (pd_scratch + 120));
	else
		disk->capacity = disk->sectors * disk->heads * disk->cylinders;

	for (j = 0; j < PD_ID_LEN; j++)
		id[j ^ 1] = pd_scratch[j + PD_ID_OFF];
	j = PD_ID_LEN - 1;
	while ((j >= 0) && (id[j] <= 0x20))
		j--;
	j++;
	id[j] = 0;

	disk->removable = pd_scratch[0] & 0x80;

	printk("%s: %s, %s, %d blocks [%dM], (%d/%d/%d), %s media\n",
	       disk->name, id,
	       disk->drive ? "slave" : "master",
	       disk->capacity, disk->capacity / 2048,
	       disk->cylinders, disk->heads, disk->sectors,
	       disk->removable ? "removable" : "fixed");

	if (disk->capacity)
		pd_init_dev_parms(disk);
	if (!disk->standby)
		pd_standby_off(disk);

	return Ok;
}

/* end of io request engine */

static void do_pd_request(struct request_queue * q)
{
	if (pd_req)
		return;
	pd_req = elv_next_request(q);
	if (!pd_req)
		return;

	schedule_fsm();
}

static int pd_special_command(struct pd_unit *disk,
		      enum action (*func)(struct pd_unit *disk))
{
	DECLARE_COMPLETION_ONSTACK(wait);
	struct request rq;
	int err = 0;

	memset(&rq, 0, sizeof(rq));
	rq.errors = 0;
	rq.rq_disk = disk->gd;
	rq.ref_count = 1;
	rq.end_io_data = &wait;
	rq.end_io = blk_end_sync_rq;
	blk_insert_request(disk->gd->queue, &rq, 0, func);
	wait_for_completion(&wait);
	if (rq.errors)
		err = -EIO;
	blk_put_request(&rq);
	return err;
}

/* kernel glue structures */

static int pd_open(struct inode *inode, struct file *file)
{
	struct pd_unit *disk = inode->i_bdev->bd_disk->private_data;

	disk->access++;

	if (disk->removable) {
		pd_special_command(disk, pd_media_check);
		pd_special_command(disk, pd_door_lock);
	}
	return 0;
}

static int pd_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct pd_unit *disk = bdev->bd_disk->private_data;

	if (disk->alt_geom) {
		geo->heads = PD_LOG_HEADS;
		geo->sectors = PD_LOG_SECTS;
		geo->cylinders = disk->capacity / (geo->heads * geo->sectors);
	} else {
		geo->heads = disk->heads;
		geo->sectors = disk->sectors;
		geo->cylinders = disk->cylinders;
	}

	return 0;
}

static int pd_ioctl(struct inode *inode, struct file *file,
	 unsigned int cmd, unsigned long arg)
{
	struct pd_unit *disk = inode->i_bdev->bd_disk->private_data;

	switch (cmd) {
	case CDROMEJECT:
		if (disk->access == 1)
			pd_special_command(disk, pd_eject);
		return 0;
	default:
		return -EINVAL;
	}
}

static int pd_release(struct inode *inode, struct file *file)
{
	struct pd_unit *disk = inode->i_bdev->bd_disk->private_data;

	if (!--disk->access && disk->removable)
		pd_special_command(disk, pd_door_unlock);

	return 0;
}

static int pd_check_media(struct gendisk *p)
{
	struct pd_unit *disk = p->private_data;
	int r;
	if (!disk->removable)
		return 0;
	pd_special_command(disk, pd_media_check);
	r = disk->changed;
	disk->changed = 0;
	return r;
}

static int pd_revalidate(struct gendisk *p)
{
	struct pd_unit *disk = p->private_data;
	if (pd_special_command(disk, pd_identify) == 0)
		set_capacity(p, disk->capacity);
	else
		set_capacity(p, 0);
	return 0;
}

static struct block_device_operations pd_fops = {
	.owner		= THIS_MODULE,
	.open		= pd_open,
	.release	= pd_release,
	.ioctl		= pd_ioctl,
	.getgeo		= pd_getgeo,
	.media_changed	= pd_check_media,
	.revalidate_disk= pd_revalidate
};

/* probing */

static void pd_probe_drive(struct pd_unit *disk)
{
	struct gendisk *p = alloc_disk(1 << PD_BITS);
	if (!p)
		return;
	strcpy(p->disk_name, disk->name);
	p->fops = &pd_fops;
	p->major = major;
	p->first_minor = (disk - pd) << PD_BITS;
	disk->gd = p;
	p->private_data = disk;
	p->queue = pd_queue;

	if (disk->drive == -1) {
		for (disk->drive = 0; disk->drive <= 1; disk->drive++)
			if (pd_special_command(disk, pd_identify) == 0)
				return;
	} else if (pd_special_command(disk, pd_identify) == 0)
		return;
	disk->gd = NULL;
	put_disk(p);
}

static int pd_detect(void)
{
	int found = 0, unit, pd_drive_count = 0;
	struct pd_unit *disk;

	for (unit = 0; unit < PD_UNITS; unit++) {
		int *parm = *drives[unit];
		struct pd_unit *disk = pd + unit;
		disk->pi = &disk->pia;
		disk->access = 0;
		disk->changed = 1;
		disk->capacity = 0;
		disk->drive = parm[D_SLV];
		snprintf(disk->name, PD_NAMELEN, "%s%c", name, 'a'+unit);
		disk->alt_geom = parm[D_GEO];
		disk->standby = parm[D_SBY];
		if (parm[D_PRT])
			pd_drive_count++;
	}

	if (pd_drive_count == 0) { /* nothing spec'd - so autoprobe for 1 */
		disk = pd;
		if (pi_init(disk->pi, 1, -1, -1, -1, -1, -1, pd_scratch,
			    PI_PD, verbose, disk->name)) {
			pd_probe_drive(disk);
			if (!disk->gd)
				pi_release(disk->pi);
		}

	} else {
		for (unit = 0, disk = pd; unit < PD_UNITS; unit++, disk++) {
			int *parm = *drives[unit];
			if (!parm[D_PRT])
				continue;
			if (pi_init(disk->pi, 0, parm[D_PRT], parm[D_MOD],
				     parm[D_UNI], parm[D_PRO], parm[D_DLY],
				     pd_scratch, PI_PD, verbose, disk->name)) {
				pd_probe_drive(disk);
				if (!disk->gd)
					pi_release(disk->pi);
			}
		}
	}
	for (unit = 0, disk = pd; unit < PD_UNITS; unit++, disk++) {
		if (disk->gd) {
			set_capacity(disk->gd, disk->capacity);
			add_disk(disk->gd);
			found = 1;
		}
	}
	if (!found)
		printk("%s: no valid drive found\n", name);
	return found;
}

static int __init pd_init(void)
{
	if (disable)
		goto out1;

	pd_queue = blk_init_queue(do_pd_request, &pd_lock);
	if (!pd_queue)
		goto out1;

	blk_queue_max_sectors(pd_queue, cluster);

	if (register_blkdev(major, name))
		goto out2;

	printk("%s: %s version %s, major %d, cluster %d, nice %d\n",
	       name, name, PD_VERSION, major, cluster, nice);
	if (!pd_detect())
		goto out3;

	return 0;

out3:
	unregister_blkdev(major, name);
out2:
	blk_cleanup_queue(pd_queue);
out1:
	return -ENODEV;
}

static void __exit pd_exit(void)
{
	struct pd_unit *disk;
	int unit;
	unregister_blkdev(major, name);
	for (unit = 0, disk = pd; unit < PD_UNITS; unit++, disk++) {
		struct gendisk *p = disk->gd;
		if (p) {
			disk->gd = NULL;
			del_gendisk(p);
			put_disk(p);
			pi_release(disk->pi);
		}
	}
	blk_cleanup_queue(pd_queue);
}

MODULE_LICENSE("GPL");
module_init(pd_init)
module_exit(pd_exit)
