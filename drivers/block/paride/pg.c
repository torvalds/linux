/* 
	pg.c    (c) 1998  Grant R. Guenther <grant@torque.net>
			  Under the terms of the GNU General Public License.

	The pg driver provides a simple character device interface for
	sending ATAPI commands to a device.  With the exception of the
	ATAPI reset operation, all operations are performed by a pair
	of read and write operations to the appropriate /dev/pgN device.
	A write operation delivers a command and any outbound data in
	a single buffer.  Normally, the write will succeed unless the
	device is offline or malfunctioning, or there is already another
	command pending.  If the write succeeds, it should be followed
	immediately by a read operation, to obtain any returned data and
	status information.  A read will fail if there is no operation
	in progress.

	As a special case, the device can be reset with a write operation,
	and in this case, no following read is expected, or permitted.

	There are no ioctl() operations.  Any single operation
	may transfer at most PG_MAX_DATA bytes.  Note that the driver must
	copy the data through an internal buffer.  In keeping with all
	current ATAPI devices, command packets are assumed to be exactly
	12 bytes in length.

	To permit future changes to this interface, the headers in the
	read and write buffers contain a single character "magic" flag.
	Currently this flag must be the character "P".

	By default, the driver will autoprobe for a single parallel
	port ATAPI device, but if their individual parameters are
	specified, the driver can handle up to 4 devices.

	To use this device, you must have the following device 
	special files defined:

		/dev/pg0 c 97 0
		/dev/pg1 c 97 1
		/dev/pg2 c 97 2
		/dev/pg3 c 97 3

	(You'll need to change the 97 to something else if you use
	the 'major' parameter to install the driver on a different
	major number.)

	The behaviour of the pg driver can be altered by setting
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

		<slv>   ATAPI devices can be jumpered to master or slave.
			Set this to 0 to choose the master drive, 1 to
			choose the slave, -1 (the default) to choose the
			first drive found.

		<dly>   some parallel ports require the driver to 
			go more slowly.  -1 sets a default value that
			should work with the chosen protocol.  Otherwise,
			set this to a small integer, the larger it is
			the slower the port i/o.  In some cases, setting
			this to zero will speed up the device. (default -1)

	    major	You may use this parameter to overide the
			default major number (97) that this driver
			will use.  Be sure to change the device
			name as well.

	    name	This parameter is a character string that
			contains the name the kernel will use for this
			device (in /proc output, for instance).
			(default "pg").

	    verbose     This parameter controls the amount of logging
			that is done by the driver.  Set it to 0 for 
			quiet operation, to 1 to enable progress
			messages while the driver probes for devices,
			or to 2 for full debug logging.  (default 0)

	If this driver is built into the kernel, you can use 
	the following command line parameters, with the same values
	as the corresponding module parameters listed above:

	    pg.drive0
	    pg.drive1
	    pg.drive2
	    pg.drive3

	In addition, you can use the parameter pg.disable to disable
	the driver entirely.

*/

/* Changes:

	1.01	GRG 1998.06.16	Bug fixes
	1.02    GRG 1998.09.24  Added jumbo support

*/

#define PG_VERSION      "1.02"
#define PG_MAJOR	97
#define PG_NAME		"pg"
#define PG_UNITS	4

#ifndef PI_PG
#define PI_PG	4
#endif

/* Here are things one can override from the insmod command.
   Most are autoprobed by paride unless set here.  Verbose is 0
   by default.

*/

static int verbose = 0;
static int major = PG_MAJOR;
static char *name = PG_NAME;
static int disable = 0;

static int drive0[6] = { 0, 0, 0, -1, -1, -1 };
static int drive1[6] = { 0, 0, 0, -1, -1, -1 };
static int drive2[6] = { 0, 0, 0, -1, -1, -1 };
static int drive3[6] = { 0, 0, 0, -1, -1, -1 };

static int (*drives[4])[6] = {&drive0, &drive1, &drive2, &drive3};
static int pg_drive_count;

enum {D_PRT, D_PRO, D_UNI, D_MOD, D_SLV, D_DLY};

/* end of parameters */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mtio.h>
#include <linux/pg.h>
#include <linux/device.h>
#include <linux/sched.h>	/* current, TASK_* */
#include <linux/jiffies.h>

#include <asm/uaccess.h>

module_param(verbose, bool, 0644);
module_param(major, int, 0);
module_param(name, charp, 0);
module_param_array(drive0, int, NULL, 0);
module_param_array(drive1, int, NULL, 0);
module_param_array(drive2, int, NULL, 0);
module_param_array(drive3, int, NULL, 0);

#include "paride.h"

#define PG_SPIN_DEL     50	/* spin delay in micro-seconds  */
#define PG_SPIN         200
#define PG_TMO		HZ
#define PG_RESET_TMO	10*HZ

#define STAT_ERR        0x01
#define STAT_INDEX      0x02
#define STAT_ECC        0x04
#define STAT_DRQ        0x08
#define STAT_SEEK       0x10
#define STAT_WRERR      0x20
#define STAT_READY      0x40
#define STAT_BUSY       0x80

#define ATAPI_IDENTIFY		0x12

static int pg_open(struct inode *inode, struct file *file);
static int pg_release(struct inode *inode, struct file *file);
static ssize_t pg_read(struct file *filp, char __user *buf,
		       size_t count, loff_t * ppos);
static ssize_t pg_write(struct file *filp, const char __user *buf,
			size_t count, loff_t * ppos);
static int pg_detect(void);

#define PG_NAMELEN      8

struct pg {
	struct pi_adapter pia;	/* interface to paride layer */
	struct pi_adapter *pi;
	int busy;		/* write done, read expected */
	int start;		/* jiffies at command start */
	int dlen;		/* transfer size requested */
	unsigned long timeout;	/* timeout requested */
	int status;		/* last sense key */
	int drive;		/* drive */
	unsigned long access;	/* count of active opens ... */
	int present;		/* device present ? */
	char *bufptr;
	char name[PG_NAMELEN];	/* pg0, pg1, ... */
};

static struct pg devices[PG_UNITS];

static int pg_identify(struct pg *dev, int log);

static char pg_scratch[512];	/* scratch block buffer */

static struct class *pg_class;

/* kernel glue structures */

static const struct file_operations pg_fops = {
	.owner = THIS_MODULE,
	.read = pg_read,
	.write = pg_write,
	.open = pg_open,
	.release = pg_release,
};

static void pg_init_units(void)
{
	int unit;

	pg_drive_count = 0;
	for (unit = 0; unit < PG_UNITS; unit++) {
		int *parm = *drives[unit];
		struct pg *dev = &devices[unit];
		dev->pi = &dev->pia;
		clear_bit(0, &dev->access);
		dev->busy = 0;
		dev->present = 0;
		dev->bufptr = NULL;
		dev->drive = parm[D_SLV];
		snprintf(dev->name, PG_NAMELEN, "%s%c", name, 'a'+unit);
		if (parm[D_PRT])
			pg_drive_count++;
	}
}

static inline int status_reg(struct pg *dev)
{
	return pi_read_regr(dev->pi, 1, 6);
}

static inline int read_reg(struct pg *dev, int reg)
{
	return pi_read_regr(dev->pi, 0, reg);
}

static inline void write_reg(struct pg *dev, int reg, int val)
{
	pi_write_regr(dev->pi, 0, reg, val);
}

static inline u8 DRIVE(struct pg *dev)
{
	return 0xa0+0x10*dev->drive;
}

static void pg_sleep(int cs)
{
	schedule_timeout_interruptible(cs);
}

static int pg_wait(struct pg *dev, int go, int stop, unsigned long tmo, char *msg)
{
	int j, r, e, s, p, to;

	dev->status = 0;

	j = 0;
	while ((((r = status_reg(dev)) & go) || (stop && (!(r & stop))))
	       && time_before(jiffies, tmo)) {
		if (j++ < PG_SPIN)
			udelay(PG_SPIN_DEL);
		else
			pg_sleep(1);
	}

	to = time_after_eq(jiffies, tmo);

	if ((r & (STAT_ERR & stop)) || to) {
		s = read_reg(dev, 7);
		e = read_reg(dev, 1);
		p = read_reg(dev, 2);
		if (verbose > 1)
			printk("%s: %s: stat=0x%x err=0x%x phase=%d%s\n",
			       dev->name, msg, s, e, p, to ? " timeout" : "");
		if (to)
			e |= 0x100;
		dev->status = (e >> 4) & 0xff;
		return -1;
	}
	return 0;
}

static int pg_command(struct pg *dev, char *cmd, int dlen, unsigned long tmo)
{
	int k;

	pi_connect(dev->pi);

	write_reg(dev, 6, DRIVE(dev));

	if (pg_wait(dev, STAT_BUSY | STAT_DRQ, 0, tmo, "before command"))
		goto fail;

	write_reg(dev, 4, dlen % 256);
	write_reg(dev, 5, dlen / 256);
	write_reg(dev, 7, 0xa0);	/* ATAPI packet command */

	if (pg_wait(dev, STAT_BUSY, STAT_DRQ, tmo, "command DRQ"))
		goto fail;

	if (read_reg(dev, 2) != 1) {
		printk("%s: command phase error\n", dev->name);
		goto fail;
	}

	pi_write_block(dev->pi, cmd, 12);

	if (verbose > 1) {
		printk("%s: Command sent, dlen=%d packet= ", dev->name, dlen);
		for (k = 0; k < 12; k++)
			printk("%02x ", cmd[k] & 0xff);
		printk("\n");
	}
	return 0;
fail:
	pi_disconnect(dev->pi);
	return -1;
}

static int pg_completion(struct pg *dev, char *buf, unsigned long tmo)
{
	int r, d, n, p;

	r = pg_wait(dev, STAT_BUSY, STAT_DRQ | STAT_READY | STAT_ERR,
		    tmo, "completion");

	dev->dlen = 0;

	while (read_reg(dev, 7) & STAT_DRQ) {
		d = (read_reg(dev, 4) + 256 * read_reg(dev, 5));
		n = ((d + 3) & 0xfffc);
		p = read_reg(dev, 2) & 3;
		if (p == 0)
			pi_write_block(dev->pi, buf, n);
		if (p == 2)
			pi_read_block(dev->pi, buf, n);
		if (verbose > 1)
			printk("%s: %s %d bytes\n", dev->name,
			       p ? "Read" : "Write", n);
		dev->dlen += (1 - p) * d;
		buf += d;
		r = pg_wait(dev, STAT_BUSY, STAT_DRQ | STAT_READY | STAT_ERR,
			    tmo, "completion");
	}

	pi_disconnect(dev->pi);

	return r;
}

static int pg_reset(struct pg *dev)
{
	int i, k, err;
	int expect[5] = { 1, 1, 1, 0x14, 0xeb };
	int got[5];

	pi_connect(dev->pi);
	write_reg(dev, 6, DRIVE(dev));
	write_reg(dev, 7, 8);

	pg_sleep(20 * HZ / 1000);

	k = 0;
	while ((k++ < PG_RESET_TMO) && (status_reg(dev) & STAT_BUSY))
		pg_sleep(1);

	for (i = 0; i < 5; i++)
		got[i] = read_reg(dev, i + 1);

	err = memcmp(expect, got, sizeof(got)) ? -1 : 0;

	if (verbose) {
		printk("%s: Reset (%d) signature = ", dev->name, k);
		for (i = 0; i < 5; i++)
			printk("%3x", got[i]);
		if (err)
			printk(" (incorrect)");
		printk("\n");
	}

	pi_disconnect(dev->pi);
	return err;
}

static void xs(char *buf, char *targ, int len)
{
	char l = '\0';
	int k;

	for (k = 0; k < len; k++) {
		char c = *buf++;
		if (c != ' ' || c != l)
			l = *targ++ = c;
	}
	if (l == ' ')
		targ--;
	*targ = '\0';
}

static int pg_identify(struct pg *dev, int log)
{
	int s;
	char *ms[2] = { "master", "slave" };
	char mf[10], id[18];
	char id_cmd[12] = { ATAPI_IDENTIFY, 0, 0, 0, 36, 0, 0, 0, 0, 0, 0, 0 };
	char buf[36];

	s = pg_command(dev, id_cmd, 36, jiffies + PG_TMO);
	if (s)
		return -1;
	s = pg_completion(dev, buf, jiffies + PG_TMO);
	if (s)
		return -1;

	if (log) {
		xs(buf + 8, mf, 8);
		xs(buf + 16, id, 16);
		printk("%s: %s %s, %s\n", dev->name, mf, id, ms[dev->drive]);
	}

	return 0;
}

/*
 * returns  0, with id set if drive is detected
 *	   -1, if drive detection failed
 */
static int pg_probe(struct pg *dev)
{
	if (dev->drive == -1) {
		for (dev->drive = 0; dev->drive <= 1; dev->drive++)
			if (!pg_reset(dev))
				return pg_identify(dev, 1);
	} else {
		if (!pg_reset(dev))
			return pg_identify(dev, 1);
	}
	return -1;
}

static int pg_detect(void)
{
	struct pg *dev = &devices[0];
	int k, unit;

	printk("%s: %s version %s, major %d\n", name, name, PG_VERSION, major);

	k = 0;
	if (pg_drive_count == 0) {
		if (pi_init(dev->pi, 1, -1, -1, -1, -1, -1, pg_scratch,
			    PI_PG, verbose, dev->name)) {
			if (!pg_probe(dev)) {
				dev->present = 1;
				k++;
			} else
				pi_release(dev->pi);
		}

	} else
		for (unit = 0; unit < PG_UNITS; unit++, dev++) {
			int *parm = *drives[unit];
			if (!parm[D_PRT])
				continue;
			if (pi_init(dev->pi, 0, parm[D_PRT], parm[D_MOD],
				    parm[D_UNI], parm[D_PRO], parm[D_DLY],
				    pg_scratch, PI_PG, verbose, dev->name)) {
				if (!pg_probe(dev)) {
					dev->present = 1;
					k++;
				} else
					pi_release(dev->pi);
			}
		}

	if (k)
		return 0;

	printk("%s: No ATAPI device detected\n", name);
	return -1;
}

static int pg_open(struct inode *inode, struct file *file)
{
	int unit = iminor(inode) & 0x7f;
	struct pg *dev = &devices[unit];

	if ((unit >= PG_UNITS) || (!dev->present))
		return -ENODEV;

	if (test_and_set_bit(0, &dev->access))
		return -EBUSY;

	if (dev->busy) {
		pg_reset(dev);
		dev->busy = 0;
	}

	pg_identify(dev, (verbose > 1));

	dev->bufptr = kmalloc(PG_MAX_DATA, GFP_KERNEL);
	if (dev->bufptr == NULL) {
		clear_bit(0, &dev->access);
		printk("%s: buffer allocation failed\n", dev->name);
		return -ENOMEM;
	}

	file->private_data = dev;

	return 0;
}

static int pg_release(struct inode *inode, struct file *file)
{
	struct pg *dev = file->private_data;

	kfree(dev->bufptr);
	dev->bufptr = NULL;
	clear_bit(0, &dev->access);

	return 0;
}

static ssize_t pg_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
	struct pg *dev = filp->private_data;
	struct pg_write_hdr hdr;
	int hs = sizeof (hdr);

	if (dev->busy)
		return -EBUSY;
	if (count < hs)
		return -EINVAL;

	if (copy_from_user(&hdr, buf, hs))
		return -EFAULT;

	if (hdr.magic != PG_MAGIC)
		return -EINVAL;
	if (hdr.dlen > PG_MAX_DATA)
		return -EINVAL;
	if ((count - hs) > PG_MAX_DATA)
		return -EINVAL;

	if (hdr.func == PG_RESET) {
		if (count != hs)
			return -EINVAL;
		if (pg_reset(dev))
			return -EIO;
		return count;
	}

	if (hdr.func != PG_COMMAND)
		return -EINVAL;

	dev->start = jiffies;
	dev->timeout = hdr.timeout * HZ + HZ / 2 + jiffies;

	if (pg_command(dev, hdr.packet, hdr.dlen, jiffies + PG_TMO)) {
		if (dev->status & 0x10)
			return -ETIME;
		return -EIO;
	}

	dev->busy = 1;

	if (copy_from_user(dev->bufptr, buf + hs, count - hs))
		return -EFAULT;
	return count;
}

static ssize_t pg_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	struct pg *dev = filp->private_data;
	struct pg_read_hdr hdr;
	int hs = sizeof (hdr);
	int copy;

	if (!dev->busy)
		return -EINVAL;
	if (count < hs)
		return -EINVAL;

	dev->busy = 0;

	if (pg_completion(dev, dev->bufptr, dev->timeout))
		if (dev->status & 0x10)
			return -ETIME;

	hdr.magic = PG_MAGIC;
	hdr.dlen = dev->dlen;
	copy = 0;

	if (hdr.dlen < 0) {
		hdr.dlen = -1 * hdr.dlen;
		copy = hdr.dlen;
		if (copy > (count - hs))
			copy = count - hs;
	}

	hdr.duration = (jiffies - dev->start + HZ / 2) / HZ;
	hdr.scsi = dev->status & 0x0f;

	if (copy_to_user(buf, &hdr, hs))
		return -EFAULT;
	if (copy > 0)
		if (copy_to_user(buf + hs, dev->bufptr, copy))
			return -EFAULT;
	return copy + hs;
}

static int __init pg_init(void)
{
	int unit;
	int err;

	if (disable){
		err = -EINVAL;
		goto out;
	}

	pg_init_units();

	if (pg_detect()) {
		err = -ENODEV;
		goto out;
	}

	err = register_chrdev(major, name, &pg_fops);
	if (err < 0) {
		printk("pg_init: unable to get major number %d\n", major);
		for (unit = 0; unit < PG_UNITS; unit++) {
			struct pg *dev = &devices[unit];
			if (dev->present)
				pi_release(dev->pi);
		}
		goto out;
	}
	major = err;	/* In case the user specified `major=0' (dynamic) */
	pg_class = class_create(THIS_MODULE, "pg");
	if (IS_ERR(pg_class)) {
		err = PTR_ERR(pg_class);
		goto out_chrdev;
	}
	for (unit = 0; unit < PG_UNITS; unit++) {
		struct pg *dev = &devices[unit];
		if (dev->present)
			class_device_create(pg_class, NULL, MKDEV(major, unit),
					NULL, "pg%u", unit);
	}
	err = 0;
	goto out;

out_chrdev:
	unregister_chrdev(major, "pg");
out:
	return err;
}

static void __exit pg_exit(void)
{
	int unit;

	for (unit = 0; unit < PG_UNITS; unit++) {
		struct pg *dev = &devices[unit];
		if (dev->present)
			class_device_destroy(pg_class, MKDEV(major, unit));
	}
	class_destroy(pg_class);
	unregister_chrdev(major, name);

	for (unit = 0; unit < PG_UNITS; unit++) {
		struct pg *dev = &devices[unit];
		if (dev->present)
			pi_release(dev->pi);
	}
}

MODULE_LICENSE("GPL");
module_init(pg_init)
module_exit(pg_exit)
