/*
 * RNG driver for Intel RNGs
 *
 * Copyright 2005 (c) MontaVista Software, Inc.
 *
 * with the majority of the code coming from:
 *
 * Hardware driver for the Intel/AMD/VIA Random Number Generators (RNG)
 * (c) Copyright 2003 Red Hat Inc <jgarzik@redhat.com>
 *
 * derived from
 *
 * Hardware driver for the AMD 768 Random Number Generator (RNG)
 * (c) Copyright 2001 Red Hat Inc <alan@redhat.com>
 *
 * derived from
 *
 * Hardware driver for Intel i810 Random Number Generator (RNG)
 * Copyright 2000,2001 Jeff Garzik <jgarzik@pobox.com>
 * Copyright 2000,2001 Philipp Rumpf <prumpf@mandrakesoft.com>
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/hw_random.h>
#include <asm/io.h>


#define PFX	KBUILD_MODNAME ": "

/*
 * RNG registers
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

/*
 * LPC bridge PCI config space registers
 */
#define FWH_DEC_EN1_REG_OLD			0xe3
#define FWH_DEC_EN1_REG_NEW			0xd9 /* high byte of 16-bit register */
#define FWH_F8_EN_MASK				0x80

#define BIOS_CNTL_REG_OLD			0x4e
#define BIOS_CNTL_REG_NEW			0xdc
#define BIOS_CNTL_WRITE_ENABLE_MASK		0x01
#define BIOS_CNTL_LOCK_ENABLE_MASK		0x02

/*
 * Magic address at which Intel Firmware Hubs get accessed
 */
#define INTEL_FWH_ADDR				0xffff0000
#define INTEL_FWH_ADDR_LEN			2

/*
 * Intel Firmware Hub command codes (write to any address inside the device)
 */
#define INTEL_FWH_RESET_CMD			0xff /* aka READ_ARRAY */
#define INTEL_FWH_READ_ID_CMD			0x90

/*
 * Intel Firmware Hub Read ID command result addresses
 */
#define INTEL_FWH_MANUFACTURER_CODE_ADDRESS	0x000000
#define INTEL_FWH_DEVICE_CODE_ADDRESS		0x000001

/*
 * Intel Firmware Hub Read ID command result values
 */
#define INTEL_FWH_MANUFACTURER_CODE		0x89
#define INTEL_FWH_DEVICE_CODE_8M		0xac
#define INTEL_FWH_DEVICE_CODE_4M		0xad

/*
 * Data for PCI driver interface
 *
 * This data only exists for exporting the supported
 * PCI ids via MODULE_DEVICE_TABLE.  We do not actually
 * register a pci_driver, because someone else might one day
 * want to register another driver on the same PCI id.
 */
static const struct pci_device_id pci_tbl[] = {
/* AA
	{ 0x8086, 0x2418, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, */
	{ 0x8086, 0x2410, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* AA */
/* AB
	{ 0x8086, 0x2428, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, */
	{ 0x8086, 0x2420, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* AB */
/* ??
	{ 0x8086, 0x2430, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, */
/* BAM, CAM, DBM, FBM, GxM
	{ 0x8086, 0x2448, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, */
	{ 0x8086, 0x244c, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* BAM */
	{ 0x8086, 0x248c, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* CAM */
	{ 0x8086, 0x24cc, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* DBM */
	{ 0x8086, 0x2641, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* FBM */
	{ 0x8086, 0x27b9, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* GxM */
	{ 0x8086, 0x27bd, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* GxM DH */
/* BA, CA, DB, Ex, 6300, Fx, 631x/632x, Gx
	{ 0x8086, 0x244e, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, */
	{ 0x8086, 0x2440, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* BA */
	{ 0x8086, 0x2480, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* CA */
	{ 0x8086, 0x24c0, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* DB */
	{ 0x8086, 0x24d0, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* Ex */
	{ 0x8086, 0x25a1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* 6300 */
	{ 0x8086, 0x2640, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* Fx */
	{ 0x8086, 0x2670, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* 631x/632x */
	{ 0x8086, 0x2671, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* 631x/632x */
	{ 0x8086, 0x2672, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* 631x/632x */
	{ 0x8086, 0x2673, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* 631x/632x */
	{ 0x8086, 0x2674, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* 631x/632x */
	{ 0x8086, 0x2675, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* 631x/632x */
	{ 0x8086, 0x2676, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* 631x/632x */
	{ 0x8086, 0x2677, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* 631x/632x */
	{ 0x8086, 0x2678, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* 631x/632x */
	{ 0x8086, 0x2679, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* 631x/632x */
	{ 0x8086, 0x267a, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* 631x/632x */
	{ 0x8086, 0x267b, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* 631x/632x */
	{ 0x8086, 0x267c, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* 631x/632x */
	{ 0x8086, 0x267d, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* 631x/632x */
	{ 0x8086, 0x267e, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* 631x/632x */
	{ 0x8086, 0x267f, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* 631x/632x */
	{ 0x8086, 0x27b8, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* Gx */
/* E
	{ 0x8086, 0x245e, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, */
	{ 0x8086, 0x2450, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, }, /* E  */
	{ 0, },	/* terminate list */
};
MODULE_DEVICE_TABLE(pci, pci_tbl);


static inline u8 hwstatus_get(void __iomem *mem)
{
	return readb(mem + INTEL_RNG_HW_STATUS);
}

static inline u8 hwstatus_set(void __iomem *mem,
			      u8 hw_status)
{
	writeb(hw_status, mem + INTEL_RNG_HW_STATUS);
	return hwstatus_get(mem);
}

static int intel_rng_data_present(struct hwrng *rng)
{
	void __iomem *mem = (void __iomem *)rng->priv;

	return !!(readb(mem + INTEL_RNG_STATUS) & INTEL_RNG_DATA_PRESENT);
}

static int intel_rng_data_read(struct hwrng *rng, u32 *data)
{
	void __iomem *mem = (void __iomem *)rng->priv;

	*data = readb(mem + INTEL_RNG_DATA);

	return 1;
}

static int intel_rng_init(struct hwrng *rng)
{
	void __iomem *mem = (void __iomem *)rng->priv;
	u8 hw_status;
	int err = -EIO;

	hw_status = hwstatus_get(mem);
	/* turn RNG h/w on, if it's off */
	if ((hw_status & INTEL_RNG_ENABLED) == 0)
		hw_status = hwstatus_set(mem, hw_status | INTEL_RNG_ENABLED);
	if ((hw_status & INTEL_RNG_ENABLED) == 0) {
		printk(KERN_ERR PFX "cannot enable RNG, aborting\n");
		goto out;
	}
	err = 0;
out:
	return err;
}

static void intel_rng_cleanup(struct hwrng *rng)
{
	void __iomem *mem = (void __iomem *)rng->priv;
	u8 hw_status;

	hw_status = hwstatus_get(mem);
	if (hw_status & INTEL_RNG_ENABLED)
		hwstatus_set(mem, hw_status & ~INTEL_RNG_ENABLED);
	else
		printk(KERN_WARNING PFX "unusual: RNG already disabled\n");
}


static struct hwrng intel_rng = {
	.name		= "intel",
	.init		= intel_rng_init,
	.cleanup	= intel_rng_cleanup,
	.data_present	= intel_rng_data_present,
	.data_read	= intel_rng_data_read,
};


#ifdef CONFIG_SMP
static char __initdata waitflag;

static void __init intel_init_wait(void *unused)
{
	while (waitflag)
		cpu_relax();
}
#endif

static int __init mod_init(void)
{
	int err = -ENODEV;
	unsigned i;
	struct pci_dev *dev = NULL;
	void __iomem *mem;
	unsigned long flags;
	u8 bios_cntl_off, fwh_dec_en1_off;
	u8 bios_cntl_val = 0xff, fwh_dec_en1_val = 0xff;
	u8 hw_status, mfc, dvc;

	for (i = 0; !dev && pci_tbl[i].vendor; ++i)
		dev = pci_get_device(pci_tbl[i].vendor, pci_tbl[i].device, NULL);

	if (!dev)
		goto out; /* Device not found. */

	/* Check for Intel 82802 */
	if (dev->device < 0x2640) {
		fwh_dec_en1_off = FWH_DEC_EN1_REG_OLD;
		bios_cntl_off = BIOS_CNTL_REG_OLD;
	} else {
		fwh_dec_en1_off = FWH_DEC_EN1_REG_NEW;
		bios_cntl_off = BIOS_CNTL_REG_NEW;
	}

	pci_read_config_byte(dev, fwh_dec_en1_off, &fwh_dec_en1_val);
	pci_read_config_byte(dev, bios_cntl_off, &bios_cntl_val);

	mem = ioremap_nocache(INTEL_FWH_ADDR, INTEL_FWH_ADDR_LEN);
	if (mem == NULL) {
		pci_dev_put(dev);
		err = -EBUSY;
		goto out;
	}

	/*
	 * Since the BIOS code/data is going to disappear from its normal
	 * location with the Read ID command, all activity on the system
	 * must be stopped until the state is back to normal.
	 */
#ifdef CONFIG_SMP
	set_mb(waitflag, 1);
	if (smp_call_function(intel_init_wait, NULL, 1, 0) != 0) {
		set_mb(waitflag, 0);
		pci_dev_put(dev);
		printk(KERN_ERR PFX "cannot run on all processors\n");
		err = -EAGAIN;
		goto err_unmap;
	}
#endif
	local_irq_save(flags);

	if (!(fwh_dec_en1_val & FWH_F8_EN_MASK))
		pci_write_config_byte(dev,
		                      fwh_dec_en1_off,
		                      fwh_dec_en1_val | FWH_F8_EN_MASK);
	if (!(bios_cntl_val &
	      (BIOS_CNTL_LOCK_ENABLE_MASK|BIOS_CNTL_WRITE_ENABLE_MASK)))
		pci_write_config_byte(dev,
		                      bios_cntl_off,
		                      bios_cntl_val | BIOS_CNTL_WRITE_ENABLE_MASK);

	writeb(INTEL_FWH_RESET_CMD, mem);
	writeb(INTEL_FWH_READ_ID_CMD, mem);
	mfc = readb(mem + INTEL_FWH_MANUFACTURER_CODE_ADDRESS);
	dvc = readb(mem + INTEL_FWH_DEVICE_CODE_ADDRESS);
	writeb(INTEL_FWH_RESET_CMD, mem);

	if (!(bios_cntl_val &
	      (BIOS_CNTL_LOCK_ENABLE_MASK|BIOS_CNTL_WRITE_ENABLE_MASK)))
		pci_write_config_byte(dev, bios_cntl_off, bios_cntl_val);
	if (!(fwh_dec_en1_val & FWH_F8_EN_MASK))
		pci_write_config_byte(dev, fwh_dec_en1_off, fwh_dec_en1_val);

	local_irq_restore(flags);
#ifdef CONFIG_SMP
	/* Tell other CPUs to resume. */
	set_mb(waitflag, 0);
#endif

	iounmap(mem);
	pci_dev_put(dev);

	if (mfc != INTEL_FWH_MANUFACTURER_CODE ||
	    (dvc != INTEL_FWH_DEVICE_CODE_8M &&
	     dvc != INTEL_FWH_DEVICE_CODE_4M)) {
		printk(KERN_ERR PFX "FWH not detected\n");
		err = -ENODEV;
		goto out;
	}

	err = -ENOMEM;
	mem = ioremap(INTEL_RNG_ADDR, INTEL_RNG_ADDR_LEN);
	if (!mem)
		goto out;
	intel_rng.priv = (unsigned long)mem;

	/* Check for Random Number Generator */
	err = -ENODEV;
	hw_status = hwstatus_get(mem);
	if ((hw_status & INTEL_RNG_PRESENT) == 0)
		goto err_unmap;

	printk(KERN_INFO "Intel 82802 RNG detected\n");
	err = hwrng_register(&intel_rng);
	if (err) {
		printk(KERN_ERR PFX "RNG registering failed (%d)\n",
		       err);
		goto err_unmap;
	}
out:
	return err;

err_unmap:
	iounmap(mem);
	goto out;
}

static void __exit mod_exit(void)
{
	void __iomem *mem = (void __iomem *)intel_rng.priv;

	hwrng_unregister(&intel_rng);
	iounmap(mem);
}

subsys_initcall(mod_init);
module_exit(mod_exit);

MODULE_DESCRIPTION("H/W RNG driver for Intel chipsets");
MODULE_LICENSE("GPL");
