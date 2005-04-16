/*
 	Hardware driver for the Intel/AMD/VIA Random Number Generators (RNG)
	(c) Copyright 2003 Red Hat Inc <jgarzik@redhat.com>
 
 	derived from
 
        Hardware driver for the AMD 768 Random Number Generator (RNG)
        (c) Copyright 2001 Red Hat Inc <alan@redhat.com>

 	derived from
 
	Hardware driver for Intel i810 Random Number Generator (RNG)
	Copyright 2000,2001 Jeff Garzik <jgarzik@pobox.com>
	Copyright 2000,2001 Philipp Rumpf <prumpf@mandrakesoft.com>

	Please read Documentation/hw_random.txt for details on use.

	----------------------------------------------------------
	This software may be used and distributed according to the terms
        of the GNU General Public License, incorporated herein by reference.

 */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/random.h>
#include <linux/miscdevice.h>
#include <linux/smp_lock.h>
#include <linux/mm.h>
#include <linux/delay.h>

#ifdef __i386__
#include <asm/msr.h>
#include <asm/cpufeature.h>
#endif

#include <asm/io.h>
#include <asm/uaccess.h>


/*
 * core module and version information
 */
#define RNG_VERSION "1.0.0"
#define RNG_MODULE_NAME "hw_random"
#define RNG_DRIVER_NAME   RNG_MODULE_NAME " hardware driver " RNG_VERSION
#define PFX RNG_MODULE_NAME ": "


/*
 * debugging macros
 */

/* pr_debug() collapses to a no-op if DEBUG is not defined */
#define DPRINTK(fmt, args...) pr_debug(PFX "%s: " fmt, __FUNCTION__ , ## args)


#undef RNG_NDEBUG        /* define to enable lightweight runtime checks */
#ifdef RNG_NDEBUG
#define assert(expr)							\
		if(!(expr)) {						\
		printk(KERN_DEBUG PFX "Assertion failed! %s,%s,%s,"	\
		"line=%d\n", #expr, __FILE__, __FUNCTION__, __LINE__);	\
		}
#else
#define assert(expr)
#endif

#define RNG_MISCDEV_MINOR		183 /* official */

static int rng_dev_open (struct inode *inode, struct file *filp);
static ssize_t rng_dev_read (struct file *filp, char __user *buf, size_t size,
				loff_t * offp);

static int __init intel_init (struct pci_dev *dev);
static void intel_cleanup(void);
static unsigned int intel_data_present (void);
static u32 intel_data_read (void);

static int __init amd_init (struct pci_dev *dev);
static void amd_cleanup(void);
static unsigned int amd_data_present (void);
static u32 amd_data_read (void);

#ifdef __i386__
static int __init via_init(struct pci_dev *dev);
static void via_cleanup(void);
static unsigned int via_data_present (void);
static u32 via_data_read (void);
#endif

struct rng_operations {
	int (*init) (struct pci_dev *dev);
	void (*cleanup) (void);
	unsigned int (*data_present) (void);
	u32 (*data_read) (void);
	unsigned int n_bytes; /* number of bytes per ->data_read */
};
static struct rng_operations *rng_ops;

static struct file_operations rng_chrdev_ops = {
	.owner		= THIS_MODULE,
	.open		= rng_dev_open,
	.read		= rng_dev_read,
};


static struct miscdevice rng_miscdev = {
	RNG_MISCDEV_MINOR,
	RNG_MODULE_NAME,
	&rng_chrdev_ops,
};

enum {
	rng_hw_none,
	rng_hw_intel,
	rng_hw_amd,
	rng_hw_via,
};

static struct rng_operations rng_vendor_ops[] = {
	/* rng_hw_none */
	{ },

	/* rng_hw_intel */
	{ intel_init, intel_cleanup, intel_data_present,
	  intel_data_read, 1 },

	/* rng_hw_amd */
	{ amd_init, amd_cleanup, amd_data_present, amd_data_read, 4 },

#ifdef __i386__
	/* rng_hw_via */
	{ via_init, via_cleanup, via_data_present, via_data_read, 1 },
#endif
};

/*
 * Data for PCI driver interface
 *
 * This data only exists for exporting the supported
 * PCI ids via MODULE_DEVICE_TABLE.  We do not actually
 * register a pci_driver, because someone else might one day
 * want to register another driver on the same PCI id.
 */
static struct pci_device_id rng_pci_tbl[] = {
	{ 0x1022, 0x7443, PCI_ANY_ID, PCI_ANY_ID, 0, 0, rng_hw_amd },
	{ 0x1022, 0x746b, PCI_ANY_ID, PCI_ANY_ID, 0, 0, rng_hw_amd },

	{ 0x8086, 0x2418, PCI_ANY_ID, PCI_ANY_ID, 0, 0, rng_hw_intel },
	{ 0x8086, 0x2428, PCI_ANY_ID, PCI_ANY_ID, 0, 0, rng_hw_intel },
	{ 0x8086, 0x2448, PCI_ANY_ID, PCI_ANY_ID, 0, 0, rng_hw_intel },
	{ 0x8086, 0x244e, PCI_ANY_ID, PCI_ANY_ID, 0, 0, rng_hw_intel },
	{ 0x8086, 0x245e, PCI_ANY_ID, PCI_ANY_ID, 0, 0, rng_hw_intel },

	{ 0, },	/* terminate list */
};
MODULE_DEVICE_TABLE (pci, rng_pci_tbl);


/***********************************************************************
 *
 * Intel RNG operations
 *
 */

/*
 * RNG registers (offsets from rng_mem)
 */
#define INTEL_RNG_HW_STATUS			0
#define         INTEL_RNG_PRESENT		0x40
#define         INTEL_RNG_ENABLED		0x01
#define INTEL_RNG_STATUS			1
#define         INTEL_RNG_DATA_PRESENT		0x01
#define INTEL_RNG_DATA				2

/*
 * Magic address at which Intel PCI bridges locate the RNG
 */
#define INTEL_RNG_ADDR				0xFFBC015F
#define INTEL_RNG_ADDR_LEN			3

/* token to our ioremap'd RNG register area */
static void __iomem *rng_mem;

static inline u8 intel_hwstatus (void)
{
	assert (rng_mem != NULL);
	return readb (rng_mem + INTEL_RNG_HW_STATUS);
}

static inline u8 intel_hwstatus_set (u8 hw_status)
{
	assert (rng_mem != NULL);
	writeb (hw_status, rng_mem + INTEL_RNG_HW_STATUS);
	return intel_hwstatus ();
}

static unsigned int intel_data_present(void)
{
	assert (rng_mem != NULL);

	return (readb (rng_mem + INTEL_RNG_STATUS) & INTEL_RNG_DATA_PRESENT) ?
		1 : 0;
}

static u32 intel_data_read(void)
{
	assert (rng_mem != NULL);

	return readb (rng_mem + INTEL_RNG_DATA);
}

static int __init intel_init (struct pci_dev *dev)
{
	int rc;
	u8 hw_status;

	DPRINTK ("ENTER\n");

	rng_mem = ioremap (INTEL_RNG_ADDR, INTEL_RNG_ADDR_LEN);
	if (rng_mem == NULL) {
		printk (KERN_ERR PFX "cannot ioremap RNG Memory\n");
		rc = -EBUSY;
		goto err_out;
	}

	/* Check for Intel 82802 */
	hw_status = intel_hwstatus ();
	if ((hw_status & INTEL_RNG_PRESENT) == 0) {
		printk (KERN_ERR PFX "RNG not detected\n");
		rc = -ENODEV;
		goto err_out_free_map;
	}

	/* turn RNG h/w on, if it's off */
	if ((hw_status & INTEL_RNG_ENABLED) == 0)
		hw_status = intel_hwstatus_set (hw_status | INTEL_RNG_ENABLED);
	if ((hw_status & INTEL_RNG_ENABLED) == 0) {
		printk (KERN_ERR PFX "cannot enable RNG, aborting\n");
		rc = -EIO;
		goto err_out_free_map;
	}

	DPRINTK ("EXIT, returning 0\n");
	return 0;

err_out_free_map:
	iounmap (rng_mem);
	rng_mem = NULL;
err_out:
	DPRINTK ("EXIT, returning %d\n", rc);
	return rc;
}

static void intel_cleanup(void)
{
	u8 hw_status;

	hw_status = intel_hwstatus ();
	if (hw_status & INTEL_RNG_ENABLED)
		intel_hwstatus_set (hw_status & ~INTEL_RNG_ENABLED);
	else
		printk(KERN_WARNING PFX "unusual: RNG already disabled\n");
	iounmap(rng_mem);
	rng_mem = NULL;
}

/***********************************************************************
 *
 * AMD RNG operations
 *
 */

static u32 pmbase;			/* PMxx I/O base */
static struct pci_dev *amd_dev;

static unsigned int amd_data_present (void)
{
      	return inl(pmbase + 0xF4) & 1;
}


static u32 amd_data_read (void)
{
	return inl(pmbase + 0xF0);
}

static int __init amd_init (struct pci_dev *dev)
{
	int rc;
	u8 rnen;

	DPRINTK ("ENTER\n");

	pci_read_config_dword(dev, 0x58, &pmbase);

	pmbase &= 0x0000FF00;

	if (pmbase == 0)
	{
		printk (KERN_ERR PFX "power management base not set\n");
		rc = -EIO;
		goto err_out;
	}

	pci_read_config_byte(dev, 0x40, &rnen);
	rnen |= (1 << 7);	/* RNG on */
	pci_write_config_byte(dev, 0x40, rnen);

	pci_read_config_byte(dev, 0x41, &rnen);
	rnen |= (1 << 7);	/* PMIO enable */
	pci_write_config_byte(dev, 0x41, rnen);

	pr_info( PFX "AMD768 system management I/O registers at 0x%X.\n",
			pmbase);

	amd_dev = dev;

	DPRINTK ("EXIT, returning 0\n");
	return 0;

err_out:
	DPRINTK ("EXIT, returning %d\n", rc);
	return rc;
}

static void amd_cleanup(void)
{
	u8 rnen;

	pci_read_config_byte(amd_dev, 0x40, &rnen);
	rnen &= ~(1 << 7);	/* RNG off */
	pci_write_config_byte(amd_dev, 0x40, rnen);

	/* FIXME: twiddle pmio, also? */
}

#ifdef __i386__
/***********************************************************************
 *
 * VIA RNG operations
 *
 */

enum {
	VIA_STRFILT_CNT_SHIFT	= 16,
	VIA_STRFILT_FAIL	= (1 << 15),
	VIA_STRFILT_ENABLE	= (1 << 14),
	VIA_RAWBITS_ENABLE	= (1 << 13),
	VIA_RNG_ENABLE		= (1 << 6),
	VIA_XSTORE_CNT_MASK	= 0x0F,

	VIA_RNG_CHUNK_8		= 0x00,	/* 64 rand bits, 64 stored bits */
	VIA_RNG_CHUNK_4		= 0x01,	/* 32 rand bits, 32 stored bits */
	VIA_RNG_CHUNK_4_MASK	= 0xFFFFFFFF,
	VIA_RNG_CHUNK_2		= 0x02,	/* 16 rand bits, 32 stored bits */
	VIA_RNG_CHUNK_2_MASK	= 0xFFFF,
	VIA_RNG_CHUNK_1		= 0x03,	/* 8 rand bits, 32 stored bits */
	VIA_RNG_CHUNK_1_MASK	= 0xFF,
};

static u32 via_rng_datum;

/*
 * Investigate using the 'rep' prefix to obtain 32 bits of random data
 * in one insn.  The upside is potentially better performance.  The
 * downside is that the instruction becomes no longer atomic.  Due to
 * this, just like familiar issues with /dev/random itself, the worst
 * case of a 'rep xstore' could potentially pause a cpu for an
 * unreasonably long time.  In practice, this condition would likely
 * only occur when the hardware is failing.  (or so we hope :))
 *
 * Another possible performance boost may come from simply buffering
 * until we have 4 bytes, thus returning a u32 at a time,
 * instead of the current u8-at-a-time.
 */

static inline u32 xstore(u32 *addr, u32 edx_in)
{
	u32 eax_out;

	asm(".byte 0x0F,0xA7,0xC0 /* xstore %%edi (addr=%0) */"
		:"=m"(*addr), "=a"(eax_out)
		:"D"(addr), "d"(edx_in));

	return eax_out;
}

static unsigned int via_data_present(void)
{
	u32 bytes_out;

	/* We choose the recommended 1-byte-per-instruction RNG rate,
	 * for greater randomness at the expense of speed.  Larger
	 * values 2, 4, or 8 bytes-per-instruction yield greater
	 * speed at lesser randomness.
	 *
	 * If you change this to another VIA_CHUNK_n, you must also
	 * change the ->n_bytes values in rng_vendor_ops[] tables.
	 * VIA_CHUNK_8 requires further code changes.
	 *
	 * A copy of MSR_VIA_RNG is placed in eax_out when xstore
	 * completes.
	 */
	via_rng_datum = 0; /* paranoia, not really necessary */
	bytes_out = xstore(&via_rng_datum, VIA_RNG_CHUNK_1) & VIA_XSTORE_CNT_MASK;
	if (bytes_out == 0)
		return 0;

	return 1;
}

static u32 via_data_read(void)
{
	return via_rng_datum;
}

static int __init via_init(struct pci_dev *dev)
{
	u32 lo, hi, old_lo;

	/* Control the RNG via MSR.  Tread lightly and pay very close
	 * close attention to values written, as the reserved fields
	 * are documented to be "undefined and unpredictable"; but it
	 * does not say to write them as zero, so I make a guess that
	 * we restore the values we find in the register.
	 */
	rdmsr(MSR_VIA_RNG, lo, hi);

	old_lo = lo;
	lo &= ~(0x7f << VIA_STRFILT_CNT_SHIFT);
	lo &= ~VIA_XSTORE_CNT_MASK;
	lo &= ~(VIA_STRFILT_ENABLE | VIA_STRFILT_FAIL | VIA_RAWBITS_ENABLE);
	lo |= VIA_RNG_ENABLE;

	if (lo != old_lo)
		wrmsr(MSR_VIA_RNG, lo, hi);

	/* perhaps-unnecessary sanity check; remove after testing if
	   unneeded */
	rdmsr(MSR_VIA_RNG, lo, hi);
	if ((lo & VIA_RNG_ENABLE) == 0) {
		printk(KERN_ERR PFX "cannot enable VIA C3 RNG, aborting\n");
		return -ENODEV;
	}

	return 0;
}

static void via_cleanup(void)
{
	/* do nothing */
}
#endif


/***********************************************************************
 *
 * /dev/hwrandom character device handling (major 10, minor 183)
 *
 */

static int rng_dev_open (struct inode *inode, struct file *filp)
{
	/* enforce read-only access to this chrdev */
	if ((filp->f_mode & FMODE_READ) == 0)
		return -EINVAL;
	if (filp->f_mode & FMODE_WRITE)
		return -EINVAL;

	return 0;
}


static ssize_t rng_dev_read (struct file *filp, char __user *buf, size_t size,
				loff_t * offp)
{
	static DEFINE_SPINLOCK(rng_lock);
	unsigned int have_data;
	u32 data = 0;
	ssize_t ret = 0;

	while (size) {
		spin_lock(&rng_lock);

		have_data = 0;
		if (rng_ops->data_present()) {
			data = rng_ops->data_read();
			have_data = rng_ops->n_bytes;
		}

		spin_unlock (&rng_lock);

		while (have_data && size) {
			if (put_user((u8)data, buf++)) {
				ret = ret ? : -EFAULT;
				break;
			}
			size--;
			ret++;
			have_data--;
			data>>=8;
		}

		if (filp->f_flags & O_NONBLOCK)
			return ret ? : -EAGAIN;

		if(need_resched())
		{
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(1);
		}
		else
			udelay(200);	/* FIXME: We could poll for 250uS ?? */

		if (signal_pending (current))
			return ret ? : -ERESTARTSYS;
	}
	return ret;
}



/*
 * rng_init_one - look for and attempt to init a single RNG
 */
static int __init rng_init_one (struct pci_dev *dev)
{
	int rc;

	DPRINTK ("ENTER\n");

	assert(rng_ops != NULL);

	rc = rng_ops->init(dev);
	if (rc)
		goto err_out;

	rc = misc_register (&rng_miscdev);
	if (rc) {
		printk (KERN_ERR PFX "misc device register failed\n");
		goto err_out_cleanup_hw;
	}

	DPRINTK ("EXIT, returning 0\n");
	return 0;

err_out_cleanup_hw:
	rng_ops->cleanup();
err_out:
	DPRINTK ("EXIT, returning %d\n", rc);
	return rc;
}



MODULE_AUTHOR("The Linux Kernel team");
MODULE_DESCRIPTION("H/W Random Number Generator (RNG) driver");
MODULE_LICENSE("GPL");


/*
 * rng_init - initialize RNG module
 */
static int __init rng_init (void)
{
	int rc;
	struct pci_dev *pdev = NULL;
	const struct pci_device_id *ent;

	DPRINTK ("ENTER\n");

	/* Probe for Intel, AMD RNGs */
	for_each_pci_dev(pdev) {
		ent = pci_match_device (rng_pci_tbl, pdev);
		if (ent) {
			rng_ops = &rng_vendor_ops[ent->driver_data];
			goto match;
		}
	}

#ifdef __i386__
	/* Probe for VIA RNG */
	if (cpu_has_xstore) {
		rng_ops = &rng_vendor_ops[rng_hw_via];
		pdev = NULL;
		goto match;
	}
#endif

	DPRINTK ("EXIT, returning -ENODEV\n");
	return -ENODEV;

match:
	rc = rng_init_one (pdev);
	if (rc)
		return rc;

	pr_info( RNG_DRIVER_NAME " loaded\n");

	DPRINTK ("EXIT, returning 0\n");
	return 0;
}


/*
 * rng_init - shutdown RNG module
 */
static void __exit rng_cleanup (void)
{
	DPRINTK ("ENTER\n");

	misc_deregister (&rng_miscdev);

	if (rng_ops->cleanup)
		rng_ops->cleanup();

	DPRINTK ("EXIT\n");
}


module_init (rng_init);
module_exit (rng_cleanup);
