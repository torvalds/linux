/* 
	pcd.c	(c) 1997-8  Grant R. Guenther <grant@torque.net>
		            Under the terms of the GNU General Public License.

	This is a high-level driver for parallel port ATAPI CD-ROM
        drives based on chips supported by the paride module.

        By default, the driver will autoprobe for a single parallel
        port ATAPI CD-ROM drive, but if their individual parameters are
        specified, the driver can handle up to 4 drives.

        The behaviour of the pcd driver can be altered by setting
        some parameters from the insmod command line.  The following
        parameters are adjustable:

            drive0      These four arguments can be arrays of       
            drive1      1-6 integers as follows:
            drive2
            drive3      <prt>,<pro>,<uni>,<mod>,<slv>,<dly>

                        Where,

                <prt>   is the base of the parallel port address for
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

		<slv>   ATAPI CD-ROMs can be jumpered to master or slave.
			Set this to 0 to choose the master drive, 1 to
                        choose the slave, -1 (the default) to choose the
			first drive found.

                <dly>   some parallel ports require the driver to 
                        go more slowly.  -1 sets a default value that
                        should work with the chosen protocol.  Otherwise,
                        set this to a small integer, the larger it is
                        the slower the port i/o.  In some cases, setting
                        this to zero will speed up the device. (default -1)
                        
            major       You may use this parameter to override the
                        default major number (46) that this driver
                        will use.  Be sure to change the device
                        name as well.

            name        This parameter is a character string that
                        contains the name the kernel will use for this
                        device (in /proc output, for instance).
                        (default "pcd")

            verbose     This parameter controls the amount of logging
                        that the driver will do.  Set it to 0 for
                        normal operation, 1 to see autoprobe progress
                        messages, or 2 to see additional debugging
                        output.  (default 0)
  
            nice        This parameter controls the driver's use of
                        idle CPU time, at the expense of some speed.
 
	If this driver is built into the kernel, you can use the
        following kernel command line parameters, with the same values
        as the corresponding module parameters listed above:

	    pcd.drive0
	    pcd.drive1
	    pcd.drive2
	    pcd.drive3
	    pcd.nice

        In addition, you can use the parameter pcd.disable to disable
        the driver entirely.

*/

/* Changes:

	1.01	GRG 1998.01.24	Added test unit ready support
	1.02    GRG 1998.05.06  Changes to pcd_completion, ready_wait,
				and loosen interpretation of ATAPI
			        standard for clearing error status.
				Use spinlocks. Eliminate sti().
	1.03    GRG 1998.06.16  Eliminated an Ugh
	1.04	GRG 1998.08.15  Added extra debugging, improvements to
				pcd_completion, use HZ in loop timing
	1.05	GRG 1998.08.16	Conformed to "Uniform CD-ROM" standard
	1.06    GRG 1998.08.19  Added audio ioctl support
	1.07    GRG 1998.09.24  Increased reset timeout, added jumbo support

*/

#define	PCD_VERSION	"1.07"
#define PCD_MAJOR	46
#define PCD_NAME	"pcd"
#define PCD_UNITS	4

/* Here are things one can override from the insmod command.
   Most are autoprobed by paride unless set here.  Verbose is off
   by default.

*/

static int verbose = 0;
static int major = PCD_MAJOR;
static char *name = PCD_NAME;
static int nice = 0;
static int disable = 0;

static int drive0[6] = { 0, 0, 0, -1, -1, -1 };
static int drive1[6] = { 0, 0, 0, -1, -1, -1 };
static int drive2[6] = { 0, 0, 0, -1, -1, -1 };
static int drive3[6] = { 0, 0, 0, -1, -1, -1 };

static int (*drives[4])[6] = {&drive0, &drive1, &drive2, &drive3};
static int pcd_drive_count;

enum {D_PRT, D_PRO, D_UNI, D_MOD, D_SLV, D_DLY};

/* end of parameters */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/cdrom.h>
#include <linux/spinlock.h>
#include <linux/blk-mq.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

static DEFINE_MUTEX(pcd_mutex);
static DEFINE_SPINLOCK(pcd_lock);

module_param(verbose, int, 0644);
module_param(major, int, 0);
module_param(name, charp, 0);
module_param(nice, int, 0);
module_param_array(drive0, int, NULL, 0);
module_param_array(drive1, int, NULL, 0);
module_param_array(drive2, int, NULL, 0);
module_param_array(drive3, int, NULL, 0);

#include "paride.h"
#include "pseudo.h"

#define PCD_RETRIES	     5
#define PCD_TMO		   800	/* timeout in jiffies */
#define PCD_DELAY           50	/* spin delay in uS */
#define PCD_READY_TMO	    20	/* in seconds */
#define PCD_RESET_TMO	   100	/* in tenths of a second */

#define PCD_SPIN	(1000000*PCD_TMO)/(HZ*PCD_DELAY)

#define IDE_ERR		0x01
#define IDE_DRQ         0x08
#define IDE_READY       0x40
#define IDE_BUSY        0x80

static int pcd_open(struct cdrom_device_info *cdi, int purpose);
static void pcd_release(struct cdrom_device_info *cdi);
static int pcd_drive_status(struct cdrom_device_info *cdi, int slot_nr);
static unsigned int pcd_check_events(struct cdrom_device_info *cdi,
				     unsigned int clearing, int slot_nr);
static int pcd_tray_move(struct cdrom_device_info *cdi, int position);
static int pcd_lock_door(struct cdrom_device_info *cdi, int lock);
static int pcd_drive_reset(struct cdrom_device_info *cdi);
static int pcd_get_mcn(struct cdrom_device_info *cdi, struct cdrom_mcn *mcn);
static int pcd_audio_ioctl(struct cdrom_device_info *cdi,
			   unsigned int cmd, void *arg);
static int pcd_packet(struct cdrom_device_info *cdi,
		      struct packet_command *cgc);

static void do_pcd_read_drq(void);
static blk_status_t pcd_queue_rq(struct blk_mq_hw_ctx *hctx,
				 const struct blk_mq_queue_data *bd);
static void do_pcd_read(void);

struct pcd_unit {
	struct pi_adapter pia;	/* interface to paride layer */
	struct pi_adapter *pi;
	int drive;		/* master/slave */
	int last_sense;		/* result of last request sense */
	int changed;		/* media change seen */
	int present;		/* does this unit exist ? */
	char *name;		/* pcd0, pcd1, etc */
	struct cdrom_device_info info;	/* uniform cdrom interface */
	struct gendisk *disk;
	struct blk_mq_tag_set tag_set;
	struct list_head rq_list;
};

static struct pcd_unit pcd[PCD_UNITS];

static char pcd_scratch[64];
static char pcd_buffer[2048];	/* raw block buffer */
static int pcd_bufblk = -1;	/* block in buffer, in CD units,
				   -1 for nothing there. See also
				   pd_unit.
				 */

/* the variables below are used mainly in the I/O request engine, which
   processes only one request at a time.
*/

static struct pcd_unit *pcd_current; /* current request's drive */
static struct request *pcd_req;
static int pcd_retries;		/* retries on current request */
static int pcd_busy;		/* request being processed ? */
static int pcd_sector;		/* address of next requested sector */
static int pcd_count;		/* number of blocks still to do */
static char *pcd_buf;		/* buffer for request in progress */
static void *par_drv;		/* reference of parport driver */

/* kernel glue structures */

static int pcd_block_open(struct block_device *bdev, fmode_t mode)
{
	struct pcd_unit *cd = bdev->bd_disk->private_data;
	int ret;

	bdev_check_media_change(bdev);

	mutex_lock(&pcd_mutex);
	ret = cdrom_open(&cd->info, bdev, mode);
	mutex_unlock(&pcd_mutex);

	return ret;
}

static void pcd_block_release(struct gendisk *disk, fmode_t mode)
{
	struct pcd_unit *cd = disk->private_data;
	mutex_lock(&pcd_mutex);
	cdrom_release(&cd->info, mode);
	mutex_unlock(&pcd_mutex);
}

static int pcd_block_ioctl(struct block_device *bdev, fmode_t mode,
				unsigned cmd, unsigned long arg)
{
	struct pcd_unit *cd = bdev->bd_disk->private_data;
	int ret;

	mutex_lock(&pcd_mutex);
	ret = cdrom_ioctl(&cd->info, bdev, mode, cmd, arg);
	mutex_unlock(&pcd_mutex);

	return ret;
}

static unsigned int pcd_block_check_events(struct gendisk *disk,
					   unsigned int clearing)
{
	struct pcd_unit *cd = disk->private_data;
	return cdrom_check_events(&cd->info, clearing);
}

static const struct block_device_operations pcd_bdops = {
	.owner		= THIS_MODULE,
	.open		= pcd_block_open,
	.release	= pcd_block_release,
	.ioctl		= pcd_block_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= blkdev_compat_ptr_ioctl,
#endif
	.check_events	= pcd_block_check_events,
};

static const struct cdrom_device_ops pcd_dops = {
	.open		= pcd_open,
	.release	= pcd_release,
	.drive_status	= pcd_drive_status,
	.check_events	= pcd_check_events,
	.tray_move	= pcd_tray_move,
	.lock_door	= pcd_lock_door,
	.get_mcn	= pcd_get_mcn,
	.reset		= pcd_drive_reset,
	.audio_ioctl	= pcd_audio_ioctl,
	.generic_packet	= pcd_packet,
	.capability	= CDC_CLOSE_TRAY | CDC_OPEN_TRAY | CDC_LOCK |
			  CDC_MCN | CDC_MEDIA_CHANGED | CDC_RESET |
			  CDC_PLAY_AUDIO | CDC_GENERIC_PACKET | CDC_CD_R |
			  CDC_CD_RW,
};

static const struct blk_mq_ops pcd_mq_ops = {
	.queue_rq	= pcd_queue_rq,
};

static int pcd_open(struct cdrom_device_info *cdi, int purpose)
{
	struct pcd_unit *cd = cdi->handle;
	if (!cd->present)
		return -ENODEV;
	return 0;
}

static void pcd_release(struct cdrom_device_info *cdi)
{
}

static inline int status_reg(struct pcd_unit *cd)
{
	return pi_read_regr(cd->pi, 1, 6);
}

static inline int read_reg(struct pcd_unit *cd, int reg)
{
	return pi_read_regr(cd->pi, 0, reg);
}

static inline void write_reg(struct pcd_unit *cd, int reg, int val)
{
	pi_write_regr(cd->pi, 0, reg, val);
}

static int pcd_wait(struct pcd_unit *cd, int go, int stop, char *fun, char *msg)
{
	int j, r, e, s, p;

	j = 0;
	while ((((r = status_reg(cd)) & go) || (stop && (!(r & stop))))
	       && (j++ < PCD_SPIN))
		udelay(PCD_DELAY);

	if ((r & (IDE_ERR & stop)) || (j > PCD_SPIN)) {
		s = read_reg(cd, 7);
		e = read_reg(cd, 1);
		p = read_reg(cd, 2);
		if (j > PCD_SPIN)
			e |= 0x100;
		if (fun)
			printk("%s: %s %s: alt=0x%x stat=0x%x err=0x%x"
			       " loop=%d phase=%d\n",
			       cd->name, fun, msg, r, s, e, j, p);
		return (s << 8) + r;
	}
	return 0;
}

static int pcd_command(struct pcd_unit *cd, char *cmd, int dlen, char *fun)
{
	pi_connect(cd->pi);

	write_reg(cd, 6, 0xa0 + 0x10 * cd->drive);

	if (pcd_wait(cd, IDE_BUSY | IDE_DRQ, 0, fun, "before command")) {
		pi_disconnect(cd->pi);
		return -1;
	}

	write_reg(cd, 4, dlen % 256);
	write_reg(cd, 5, dlen / 256);
	write_reg(cd, 7, 0xa0);	/* ATAPI packet command */

	if (pcd_wait(cd, IDE_BUSY, IDE_DRQ, fun, "command DRQ")) {
		pi_disconnect(cd->pi);
		return -1;
	}

	if (read_reg(cd, 2) != 1) {
		printk("%s: %s: command phase error\n", cd->name, fun);
		pi_disconnect(cd->pi);
		return -1;
	}

	pi_write_block(cd->pi, cmd, 12);

	return 0;
}

static int pcd_completion(struct pcd_unit *cd, char *buf, char *fun)
{
	int r, d, p, n, k, j;

	r = -1;
	k = 0;
	j = 0;

	if (!pcd_wait(cd, IDE_BUSY, IDE_DRQ | IDE_READY | IDE_ERR,
		      fun, "completion")) {
		r = 0;
		while (read_reg(cd, 7) & IDE_DRQ) {
			d = read_reg(cd, 4) + 256 * read_reg(cd, 5);
			n = (d + 3) & 0xfffc;
			p = read_reg(cd, 2) & 3;

			if ((p == 2) && (n > 0) && (j == 0)) {
				pi_read_block(cd->pi, buf, n);
				if (verbose > 1)
					printk("%s: %s: Read %d bytes\n",
					       cd->name, fun, n);
				r = 0;
				j++;
			} else {
				if (verbose > 1)
					printk
					    ("%s: %s: Unexpected phase %d, d=%d, k=%d\n",
					     cd->name, fun, p, d, k);
				if (verbose < 2)
					printk_once(
					    "%s: WARNING: ATAPI phase errors\n",
					    cd->name);
				mdelay(1);
			}
			if (k++ > PCD_TMO) {
				printk("%s: Stuck DRQ\n", cd->name);
				break;
			}
			if (pcd_wait
			    (cd, IDE_BUSY, IDE_DRQ | IDE_READY | IDE_ERR, fun,
			     "completion")) {
				r = -1;
				break;
			}
		}
	}

	pi_disconnect(cd->pi);

	return r;
}

static void pcd_req_sense(struct pcd_unit *cd, char *fun)
{
	char rs_cmd[12] = { 0x03, 0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0 };
	char buf[16];
	int r, c;

	r = pcd_command(cd, rs_cmd, 16, "Request sense");
	mdelay(1);
	if (!r)
		pcd_completion(cd, buf, "Request sense");

	cd->last_sense = -1;
	c = 2;
	if (!r) {
		if (fun)
			printk("%s: %s: Sense key: %x, ASC: %x, ASQ: %x\n",
			       cd->name, fun, buf[2] & 0xf, buf[12], buf[13]);
		c = buf[2] & 0xf;
		cd->last_sense =
		    c | ((buf[12] & 0xff) << 8) | ((buf[13] & 0xff) << 16);
	}
	if ((c == 2) || (c == 6))
		cd->changed = 1;
}

static int pcd_atapi(struct pcd_unit *cd, char *cmd, int dlen, char *buf, char *fun)
{
	int r;

	r = pcd_command(cd, cmd, dlen, fun);
	mdelay(1);
	if (!r)
		r = pcd_completion(cd, buf, fun);
	if (r)
		pcd_req_sense(cd, fun);

	return r;
}

static int pcd_packet(struct cdrom_device_info *cdi, struct packet_command *cgc)
{
	return pcd_atapi(cdi->handle, cgc->cmd, cgc->buflen, cgc->buffer,
			 "generic packet");
}

#define DBMSG(msg)	((verbose>1)?(msg):NULL)

static unsigned int pcd_check_events(struct cdrom_device_info *cdi,
				     unsigned int clearing, int slot_nr)
{
	struct pcd_unit *cd = cdi->handle;
	int res = cd->changed;
	if (res)
		cd->changed = 0;
	return res ? DISK_EVENT_MEDIA_CHANGE : 0;
}

static int pcd_lock_door(struct cdrom_device_info *cdi, int lock)
{
	char un_cmd[12] = { 0x1e, 0, 0, 0, lock, 0, 0, 0, 0, 0, 0, 0 };

	return pcd_atapi(cdi->handle, un_cmd, 0, pcd_scratch,
			 lock ? "lock door" : "unlock door");
}

static int pcd_tray_move(struct cdrom_device_info *cdi, int position)
{
	char ej_cmd[12] = { 0x1b, 0, 0, 0, 3 - position, 0, 0, 0, 0, 0, 0, 0 };

	return pcd_atapi(cdi->handle, ej_cmd, 0, pcd_scratch,
			 position ? "eject" : "close tray");
}

static void pcd_sleep(int cs)
{
	schedule_timeout_interruptible(cs);
}

static int pcd_reset(struct pcd_unit *cd)
{
	int i, k, flg;
	int expect[5] = { 1, 1, 1, 0x14, 0xeb };

	pi_connect(cd->pi);
	write_reg(cd, 6, 0xa0 + 0x10 * cd->drive);
	write_reg(cd, 7, 8);

	pcd_sleep(20 * HZ / 1000);	/* delay a bit */

	k = 0;
	while ((k++ < PCD_RESET_TMO) && (status_reg(cd) & IDE_BUSY))
		pcd_sleep(HZ / 10);

	flg = 1;
	for (i = 0; i < 5; i++)
		flg &= (read_reg(cd, i + 1) == expect[i]);

	if (verbose) {
		printk("%s: Reset (%d) signature = ", cd->name, k);
		for (i = 0; i < 5; i++)
			printk("%3x", read_reg(cd, i + 1));
		if (!flg)
			printk(" (incorrect)");
		printk("\n");
	}

	pi_disconnect(cd->pi);
	return flg - 1;
}

static int pcd_drive_reset(struct cdrom_device_info *cdi)
{
	return pcd_reset(cdi->handle);
}

static int pcd_ready_wait(struct pcd_unit *cd, int tmo)
{
	char tr_cmd[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	int k, p;

	k = 0;
	while (k < tmo) {
		cd->last_sense = 0;
		pcd_atapi(cd, tr_cmd, 0, NULL, DBMSG("test unit ready"));
		p = cd->last_sense;
		if (!p)
			return 0;
		if (!(((p & 0xffff) == 0x0402) || ((p & 0xff) == 6)))
			return p;
		k++;
		pcd_sleep(HZ);
	}
	return 0x000020;	/* timeout */
}

static int pcd_drive_status(struct cdrom_device_info *cdi, int slot_nr)
{
	char rc_cmd[12] = { 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	struct pcd_unit *cd = cdi->handle;

	if (pcd_ready_wait(cd, PCD_READY_TMO))
		return CDS_DRIVE_NOT_READY;
	if (pcd_atapi(cd, rc_cmd, 8, pcd_scratch, DBMSG("check media")))
		return CDS_NO_DISC;
	return CDS_DISC_OK;
}

static int pcd_identify(struct pcd_unit *cd)
{
	char id_cmd[12] = { 0x12, 0, 0, 0, 36, 0, 0, 0, 0, 0, 0, 0 };
	char id[18];
	int k, s;

	pcd_bufblk = -1;

	s = pcd_atapi(cd, id_cmd, 36, pcd_buffer, "identify");

	if (s)
		return -1;
	if ((pcd_buffer[0] & 0x1f) != 5) {
		if (verbose)
			printk("%s: %s is not a CD-ROM\n",
			       cd->name, cd->drive ? "Slave" : "Master");
		return -1;
	}
	memcpy(id, pcd_buffer + 16, 16);
	id[16] = 0;
	k = 16;
	while ((k >= 0) && (id[k] <= 0x20)) {
		id[k] = 0;
		k--;
	}

	printk("%s: %s: %s\n", cd->name, cd->drive ? "Slave" : "Master", id);

	return 0;
}

/*
 * returns 0, with id set if drive is detected, otherwise an error code.
 */
static int pcd_probe(struct pcd_unit *cd, int ms)
{
	if (ms == -1) {
		for (cd->drive = 0; cd->drive <= 1; cd->drive++)
			if (!pcd_reset(cd) && !pcd_identify(cd))
				return 0;
	} else {
		cd->drive = ms;
		if (!pcd_reset(cd) && !pcd_identify(cd))
			return 0;
	}
	return -ENODEV;
}

static int pcd_probe_capabilities(struct pcd_unit *cd)
{
	char cmd[12] = { 0x5a, 1 << 3, 0x2a, 0, 0, 0, 0, 18, 0, 0, 0, 0 };
	char buffer[32];
	int ret;

	ret = pcd_atapi(cd, cmd, 18, buffer, "mode sense capabilities");
	if (ret)
		return ret;

	/* we should now have the cap page */
	if ((buffer[11] & 1) == 0)
		cd->info.mask |= CDC_CD_R;
	if ((buffer[11] & 2) == 0)
		cd->info.mask |= CDC_CD_RW;
	if ((buffer[12] & 1) == 0)
		cd->info.mask |= CDC_PLAY_AUDIO;
	if ((buffer[14] & 1) == 0)
		cd->info.mask |= CDC_LOCK;
	if ((buffer[14] & 8) == 0)
		cd->info.mask |= CDC_OPEN_TRAY;
	if ((buffer[14] >> 6) == 0)
		cd->info.mask |= CDC_CLOSE_TRAY;

	return 0;
}

/* I/O request processing */
static int pcd_queue;

static int set_next_request(void)
{
	struct pcd_unit *cd;
	int old_pos = pcd_queue;

	do {
		cd = &pcd[pcd_queue];
		if (++pcd_queue == PCD_UNITS)
			pcd_queue = 0;
		if (cd->present && !list_empty(&cd->rq_list)) {
			pcd_req = list_first_entry(&cd->rq_list, struct request,
							queuelist);
			list_del_init(&pcd_req->queuelist);
			blk_mq_start_request(pcd_req);
			break;
		}
	} while (pcd_queue != old_pos);

	return pcd_req != NULL;
}

static void pcd_request(void)
{
	struct pcd_unit *cd;

	if (pcd_busy)
		return;

	if (!pcd_req && !set_next_request())
		return;

	cd = pcd_req->rq_disk->private_data;
	if (cd != pcd_current)
		pcd_bufblk = -1;
	pcd_current = cd;
	pcd_sector = blk_rq_pos(pcd_req);
	pcd_count = blk_rq_cur_sectors(pcd_req);
	pcd_buf = bio_data(pcd_req->bio);
	pcd_busy = 1;
	ps_set_intr(do_pcd_read, NULL, 0, nice);
}

static blk_status_t pcd_queue_rq(struct blk_mq_hw_ctx *hctx,
				 const struct blk_mq_queue_data *bd)
{
	struct pcd_unit *cd = hctx->queue->queuedata;

	if (rq_data_dir(bd->rq) != READ) {
		blk_mq_start_request(bd->rq);
		return BLK_STS_IOERR;
	}

	spin_lock_irq(&pcd_lock);
	list_add_tail(&bd->rq->queuelist, &cd->rq_list);
	pcd_request();
	spin_unlock_irq(&pcd_lock);

	return BLK_STS_OK;
}

static inline void next_request(blk_status_t err)
{
	unsigned long saved_flags;

	spin_lock_irqsave(&pcd_lock, saved_flags);
	if (!blk_update_request(pcd_req, err, blk_rq_cur_bytes(pcd_req))) {
		__blk_mq_end_request(pcd_req, err);
		pcd_req = NULL;
	}
	pcd_busy = 0;
	pcd_request();
	spin_unlock_irqrestore(&pcd_lock, saved_flags);
}

static int pcd_ready(void)
{
	return (((status_reg(pcd_current) & (IDE_BUSY | IDE_DRQ)) == IDE_DRQ));
}

static void pcd_transfer(void)
{

	while (pcd_count && (pcd_sector / 4 == pcd_bufblk)) {
		int o = (pcd_sector % 4) * 512;
		memcpy(pcd_buf, pcd_buffer + o, 512);
		pcd_count--;
		pcd_buf += 512;
		pcd_sector++;
	}
}

static void pcd_start(void)
{
	int b, i;
	char rd_cmd[12] = { 0xa8, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0 };

	pcd_bufblk = pcd_sector / 4;
	b = pcd_bufblk;
	for (i = 0; i < 4; i++) {
		rd_cmd[5 - i] = b & 0xff;
		b = b >> 8;
	}

	if (pcd_command(pcd_current, rd_cmd, 2048, "read block")) {
		pcd_bufblk = -1;
		next_request(BLK_STS_IOERR);
		return;
	}

	mdelay(1);

	ps_set_intr(do_pcd_read_drq, pcd_ready, PCD_TMO, nice);
}

static void do_pcd_read(void)
{
	pcd_busy = 1;
	pcd_retries = 0;
	pcd_transfer();
	if (!pcd_count) {
		next_request(0);
		return;
	}

	pi_do_claimed(pcd_current->pi, pcd_start);
}

static void do_pcd_read_drq(void)
{
	unsigned long saved_flags;

	if (pcd_completion(pcd_current, pcd_buffer, "read block")) {
		if (pcd_retries < PCD_RETRIES) {
			mdelay(1);
			pcd_retries++;
			pi_do_claimed(pcd_current->pi, pcd_start);
			return;
		}
		pcd_bufblk = -1;
		next_request(BLK_STS_IOERR);
		return;
	}

	do_pcd_read();
	spin_lock_irqsave(&pcd_lock, saved_flags);
	pcd_request();
	spin_unlock_irqrestore(&pcd_lock, saved_flags);
}

/* the audio_ioctl stuff is adapted from sr_ioctl.c */

static int pcd_audio_ioctl(struct cdrom_device_info *cdi, unsigned int cmd, void *arg)
{
	struct pcd_unit *cd = cdi->handle;

	switch (cmd) {

	case CDROMREADTOCHDR:

		{
			char cmd[12] =
			    { GPCMD_READ_TOC_PMA_ATIP, 0, 0, 0, 0, 0, 0, 0, 12,
			 0, 0, 0 };
			struct cdrom_tochdr *tochdr =
			    (struct cdrom_tochdr *) arg;
			char buffer[32];
			int r;

			r = pcd_atapi(cd, cmd, 12, buffer, "read toc header");

			tochdr->cdth_trk0 = buffer[2];
			tochdr->cdth_trk1 = buffer[3];

			return r ? -EIO : 0;
		}

	case CDROMREADTOCENTRY:

		{
			char cmd[12] =
			    { GPCMD_READ_TOC_PMA_ATIP, 0, 0, 0, 0, 0, 0, 0, 12,
			 0, 0, 0 };

			struct cdrom_tocentry *tocentry =
			    (struct cdrom_tocentry *) arg;
			unsigned char buffer[32];
			int r;

			cmd[1] =
			    (tocentry->cdte_format == CDROM_MSF ? 0x02 : 0);
			cmd[6] = tocentry->cdte_track;

			r = pcd_atapi(cd, cmd, 12, buffer, "read toc entry");

			tocentry->cdte_ctrl = buffer[5] & 0xf;
			tocentry->cdte_adr = buffer[5] >> 4;
			tocentry->cdte_datamode =
			    (tocentry->cdte_ctrl & 0x04) ? 1 : 0;
			if (tocentry->cdte_format == CDROM_MSF) {
				tocentry->cdte_addr.msf.minute = buffer[9];
				tocentry->cdte_addr.msf.second = buffer[10];
				tocentry->cdte_addr.msf.frame = buffer[11];
			} else
				tocentry->cdte_addr.lba =
				    (((((buffer[8] << 8) + buffer[9]) << 8)
				      + buffer[10]) << 8) + buffer[11];

			return r ? -EIO : 0;
		}

	default:

		return -ENOSYS;
	}
}

static int pcd_get_mcn(struct cdrom_device_info *cdi, struct cdrom_mcn *mcn)
{
	char cmd[12] =
	    { GPCMD_READ_SUBCHANNEL, 0, 0x40, 2, 0, 0, 0, 0, 24, 0, 0, 0 };
	char buffer[32];

	if (pcd_atapi(cdi->handle, cmd, 24, buffer, "get mcn"))
		return -EIO;

	memcpy(mcn->medium_catalog_number, buffer + 9, 13);
	mcn->medium_catalog_number[13] = 0;

	return 0;
}

static int pcd_init_unit(struct pcd_unit *cd, bool autoprobe, int port,
		int mode, int unit, int protocol, int delay, int ms)
{
	struct gendisk *disk;
	int ret;

	ret = blk_mq_alloc_sq_tag_set(&cd->tag_set, &pcd_mq_ops, 1,
				      BLK_MQ_F_SHOULD_MERGE);
	if (ret)
		return ret;

	disk = blk_mq_alloc_disk(&cd->tag_set, cd);
	if (IS_ERR(disk)) {
		ret = PTR_ERR(disk);
		goto out_free_tag_set;
	}

	INIT_LIST_HEAD(&cd->rq_list);
	blk_queue_bounce_limit(disk->queue, BLK_BOUNCE_HIGH);
	cd->disk = disk;
	cd->pi = &cd->pia;
	cd->present = 0;
	cd->last_sense = 0;
	cd->changed = 1;
	cd->drive = (*drives[cd - pcd])[D_SLV];

	cd->name = &cd->info.name[0];
	snprintf(cd->name, sizeof(cd->info.name), "%s%d", name, unit);
	cd->info.ops = &pcd_dops;
	cd->info.handle = cd;
	cd->info.speed = 0;
	cd->info.capacity = 1;
	cd->info.mask = 0;
	disk->major = major;
	disk->first_minor = unit;
	disk->minors = 1;
	strcpy(disk->disk_name, cd->name);	/* umm... */
	disk->fops = &pcd_bdops;
	disk->flags = GENHD_FL_BLOCK_EVENTS_ON_EXCL_WRITE;
	disk->events = DISK_EVENT_MEDIA_CHANGE;

	if (!pi_init(cd->pi, autoprobe, port, mode, unit, protocol, delay,
			pcd_buffer, PI_PCD, verbose, cd->name)) {
		ret = -ENODEV;
		goto out_free_disk;
	}
	ret = pcd_probe(cd, ms);
	if (ret)
		goto out_pi_release;

	cd->present = 1;
	pcd_probe_capabilities(cd);
	ret = register_cdrom(cd->disk, &cd->info);
	if (ret)
		goto out_pi_release;
	ret = add_disk(cd->disk);
	if (ret)
		goto out_unreg_cdrom;
	return 0;

out_unreg_cdrom:
	unregister_cdrom(&cd->info);
out_pi_release:
	pi_release(cd->pi);
out_free_disk:
	blk_cleanup_disk(cd->disk);
out_free_tag_set:
	blk_mq_free_tag_set(&cd->tag_set);
	return ret;
}

static int __init pcd_init(void)
{
	int found = 0, unit;

	if (disable)
		return -EINVAL;

	if (register_blkdev(major, name))
		return -EBUSY;

	pr_info("%s: %s version %s, major %d, nice %d\n",
		name, name, PCD_VERSION, major, nice);

	par_drv = pi_register_driver(name);
	if (!par_drv) {
		pr_err("failed to register %s driver\n", name);
		goto out_unregister_blkdev;
	}

	for (unit = 0; unit < PCD_UNITS; unit++) {
		if ((*drives[unit])[D_PRT])
			pcd_drive_count++;
	}

	if (pcd_drive_count == 0) { /* nothing spec'd - so autoprobe for 1 */
		if (!pcd_init_unit(pcd, 1, -1, -1, -1, -1, -1, -1))
			found++;
	} else {
		for (unit = 0; unit < PCD_UNITS; unit++) {
			struct pcd_unit *cd = &pcd[unit];
			int *conf = *drives[unit];

			if (!conf[D_PRT])
				continue;
			if (!pcd_init_unit(cd, 0, conf[D_PRT], conf[D_MOD],
					conf[D_UNI], conf[D_PRO], conf[D_DLY],
					conf[D_SLV]))
				found++;
		}
	}

	if (!found) {
		pr_info("%s: No CD-ROM drive found\n", name);
		goto out_unregister_pi_driver;
	}

	return 0;

out_unregister_pi_driver:
	pi_unregister_driver(par_drv);
out_unregister_blkdev:
	unregister_blkdev(major, name);
	return -ENODEV;
}

static void __exit pcd_exit(void)
{
	struct pcd_unit *cd;
	int unit;

	for (unit = 0, cd = pcd; unit < PCD_UNITS; unit++, cd++) {
		if (!cd->present)
			continue;

		unregister_cdrom(&cd->info);
		del_gendisk(cd->disk);
		pi_release(cd->pi);
		blk_cleanup_disk(cd->disk);

		blk_mq_free_tag_set(&cd->tag_set);
	}
	pi_unregister_driver(par_drv);
	unregister_blkdev(major, name);
}

MODULE_LICENSE("GPL");
module_init(pcd_init)
module_exit(pcd_exit)
