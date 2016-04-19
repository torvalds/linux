/*
 * PMC551 PCI Mezzanine Ram Device
 *
 * Author:
 *	Mark Ferrell <mferrell@mvista.com>
 *	Copyright 1999,2000 Nortel Networks
 *
 * License:
 *	As part of this driver was derived from the slram.c driver it
 *	falls under the same license, which is GNU General Public
 *	License v2
 *
 * Description:
 *	This driver is intended to support the PMC551 PCI Ram device
 *	from Ramix Inc.  The PMC551 is a PMC Mezzanine module for
 *	cPCI embedded systems.  The device contains a single SROM
 *	that initially programs the V370PDC chipset onboard the
 *	device, and various banks of DRAM/SDRAM onboard.  This driver
 *	implements this PCI Ram device as an MTD (Memory Technology
 *	Device) so that it can be used to hold a file system, or for
 *	added swap space in embedded systems.  Since the memory on
 *	this board isn't as fast as main memory we do not try to hook
 *	it into main memory as that would simply reduce performance
 *	on the system.  Using it as a block device allows us to use
 *	it as high speed swap or for a high speed disk device of some
 *	sort.  Which becomes very useful on diskless systems in the
 *	embedded market I might add.
 *
 * Notes:
 *	Due to what I assume is more buggy SROM, the 64M PMC551 I
 *	have available claims that all 4 of its DRAM banks have 64MiB
 *	of ram configured (making a grand total of 256MiB onboard).
 *	This is slightly annoying since the BAR0 size reflects the
 *	aperture size, not the dram size, and the V370PDC supplies no
 *	other method for memory size discovery.  This problem is
 *	mostly only relevant when compiled as a module, as the
 *	unloading of the module with an aperture size smaller than
 *	the ram will cause the driver to detect the onboard memory
 *	size to be equal to the aperture size when the module is
 *	reloaded.  Soooo, to help, the module supports an msize
 *	option to allow the specification of the onboard memory, and
 *	an asize option, to allow the specification of the aperture
 *	size.  The aperture must be equal to or less then the memory
 *	size, the driver will correct this if you screw it up.  This
 *	problem is not relevant for compiled in drivers as compiled
 *	in drivers only init once.
 *
 * Credits:
 *	Saeed Karamooz <saeed@ramix.com> of Ramix INC. for the
 *	initial example code of how to initialize this device and for
 *	help with questions I had concerning operation of the device.
 *
 *	Most of the MTD code for this driver was originally written
 *	for the slram.o module in the MTD drivers package which
 *	allows the mapping of system memory into an MTD device.
 *	Since the PMC551 memory module is accessed in the same
 *	fashion as system memory, the slram.c code became a very nice
 *	fit to the needs of this driver.  All we added was PCI
 *	detection/initialization to the driver and automatically figure
 *	out the size via the PCI detection.o, later changes by Corey
 *	Minyard set up the card to utilize a 1M sliding apature.
 *
 *	Corey Minyard <minyard@nortelnetworks.com>
 *	* Modified driver to utilize a sliding aperture instead of
 *	 mapping all memory into kernel space which turned out to
 *	 be very wasteful.
 *	* Located a bug in the SROM's initialization sequence that
 *	 made the memory unusable, added a fix to code to touch up
 *	 the DRAM some.
 *
 * Bugs/FIXMEs:
 *	* MUST fix the init function to not spin on a register
 *	waiting for it to set .. this does not safely handle busted
 *	devices that never reset the register correctly which will
 *	cause the system to hang w/ a reboot being the only chance at
 *	recover. [sort of fixed, could be better]
 *	* Add I2C handling of the SROM so we can read the SROM's information
 *	about the aperture size.  This should always accurately reflect the
 *	onboard memory size.
 *	* Comb the init routine.  It's still a bit cludgy on a few things.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <asm/io.h>
#include <linux/pci.h>
#include <linux/mtd/mtd.h>

#define PMC551_VERSION \
	"Ramix PMC551 PCI Mezzanine Ram Driver. (C) 1999,2000 Nortel Networks.\n"

#define PCI_VENDOR_ID_V3_SEMI 0x11b0
#define PCI_DEVICE_ID_V3_SEMI_V370PDC 0x0200

#define PMC551_PCI_MEM_MAP0 0x50
#define PMC551_PCI_MEM_MAP1 0x54
#define PMC551_PCI_MEM_MAP_MAP_ADDR_MASK 0x3ff00000
#define PMC551_PCI_MEM_MAP_APERTURE_MASK 0x000000f0
#define PMC551_PCI_MEM_MAP_REG_EN 0x00000002
#define PMC551_PCI_MEM_MAP_ENABLE 0x00000001

#define PMC551_SDRAM_MA  0x60
#define PMC551_SDRAM_CMD 0x62
#define PMC551_DRAM_CFG  0x64
#define PMC551_SYS_CTRL_REG 0x78

#define PMC551_DRAM_BLK0 0x68
#define PMC551_DRAM_BLK1 0x6c
#define PMC551_DRAM_BLK2 0x70
#define PMC551_DRAM_BLK3 0x74
#define PMC551_DRAM_BLK_GET_SIZE(x) (524288 << ((x >> 4) & 0x0f))
#define PMC551_DRAM_BLK_SET_COL_MUX(x, v) (((x) & ~0x00007000) | (((v) & 0x7) << 12))
#define PMC551_DRAM_BLK_SET_ROW_MUX(x, v) (((x) & ~0x00000f00) | (((v) & 0xf) << 8))

struct mypriv {
	struct pci_dev *dev;
	u_char *start;
	u32 base_map0;
	u32 curr_map0;
	u32 asize;
	struct mtd_info *nextpmc551;
};

static struct mtd_info *pmc551list;

static int pmc551_point(struct mtd_info *mtd, loff_t from, size_t len,
			size_t *retlen, void **virt, resource_size_t *phys);

static int pmc551_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct mypriv *priv = mtd->priv;
	u32 soff_hi, soff_lo;	/* start address offset hi/lo */
	u32 eoff_hi, eoff_lo;	/* end address offset hi/lo */
	unsigned long end;
	u_char *ptr;
	size_t retlen;

#ifdef CONFIG_MTD_PMC551_DEBUG
	printk(KERN_DEBUG "pmc551_erase(pos:%ld, len:%ld)\n", (long)instr->addr,
		(long)instr->len);
#endif

	end = instr->addr + instr->len - 1;
	eoff_hi = end & ~(priv->asize - 1);
	soff_hi = instr->addr & ~(priv->asize - 1);
	eoff_lo = end & (priv->asize - 1);
	soff_lo = instr->addr & (priv->asize - 1);

	pmc551_point(mtd, instr->addr, instr->len, &retlen,
		     (void **)&ptr, NULL);

	if (soff_hi == eoff_hi || mtd->size == priv->asize) {
		/* The whole thing fits within one access, so just one shot
		   will do it. */
		memset(ptr, 0xff, instr->len);
	} else {
		/* We have to do multiple writes to get all the data
		   written. */
		while (soff_hi != eoff_hi) {
#ifdef CONFIG_MTD_PMC551_DEBUG
			printk(KERN_DEBUG "pmc551_erase() soff_hi: %ld, "
				"eoff_hi: %ld\n", (long)soff_hi, (long)eoff_hi);
#endif
			memset(ptr, 0xff, priv->asize);
			if (soff_hi + priv->asize >= mtd->size) {
				goto out;
			}
			soff_hi += priv->asize;
			pmc551_point(mtd, (priv->base_map0 | soff_hi),
				     priv->asize, &retlen,
				     (void **)&ptr, NULL);
		}
		memset(ptr, 0xff, eoff_lo);
	}

      out:
	instr->state = MTD_ERASE_DONE;
#ifdef CONFIG_MTD_PMC551_DEBUG
	printk(KERN_DEBUG "pmc551_erase() done\n");
#endif

	mtd_erase_callback(instr);
	return 0;
}

static int pmc551_point(struct mtd_info *mtd, loff_t from, size_t len,
			size_t *retlen, void **virt, resource_size_t *phys)
{
	struct mypriv *priv = mtd->priv;
	u32 soff_hi;
	u32 soff_lo;

#ifdef CONFIG_MTD_PMC551_DEBUG
	printk(KERN_DEBUG "pmc551_point(%ld, %ld)\n", (long)from, (long)len);
#endif

	soff_hi = from & ~(priv->asize - 1);
	soff_lo = from & (priv->asize - 1);

	/* Cheap hack optimization */
	if (priv->curr_map0 != from) {
		pci_write_config_dword(priv->dev, PMC551_PCI_MEM_MAP0,
					(priv->base_map0 | soff_hi));
		priv->curr_map0 = soff_hi;
	}

	*virt = priv->start + soff_lo;
	*retlen = len;
	return 0;
}

static int pmc551_unpoint(struct mtd_info *mtd, loff_t from, size_t len)
{
#ifdef CONFIG_MTD_PMC551_DEBUG
	printk(KERN_DEBUG "pmc551_unpoint()\n");
#endif
	return 0;
}

static int pmc551_read(struct mtd_info *mtd, loff_t from, size_t len,
			size_t * retlen, u_char * buf)
{
	struct mypriv *priv = mtd->priv;
	u32 soff_hi, soff_lo;	/* start address offset hi/lo */
	u32 eoff_hi, eoff_lo;	/* end address offset hi/lo */
	unsigned long end;
	u_char *ptr;
	u_char *copyto = buf;

#ifdef CONFIG_MTD_PMC551_DEBUG
	printk(KERN_DEBUG "pmc551_read(pos:%ld, len:%ld) asize: %ld\n",
		(long)from, (long)len, (long)priv->asize);
#endif

	end = from + len - 1;
	soff_hi = from & ~(priv->asize - 1);
	eoff_hi = end & ~(priv->asize - 1);
	soff_lo = from & (priv->asize - 1);
	eoff_lo = end & (priv->asize - 1);

	pmc551_point(mtd, from, len, retlen, (void **)&ptr, NULL);

	if (soff_hi == eoff_hi) {
		/* The whole thing fits within one access, so just one shot
		   will do it. */
		memcpy(copyto, ptr, len);
		copyto += len;
	} else {
		/* We have to do multiple writes to get all the data
		   written. */
		while (soff_hi != eoff_hi) {
#ifdef CONFIG_MTD_PMC551_DEBUG
			printk(KERN_DEBUG "pmc551_read() soff_hi: %ld, "
				"eoff_hi: %ld\n", (long)soff_hi, (long)eoff_hi);
#endif
			memcpy(copyto, ptr, priv->asize);
			copyto += priv->asize;
			if (soff_hi + priv->asize >= mtd->size) {
				goto out;
			}
			soff_hi += priv->asize;
			pmc551_point(mtd, soff_hi, priv->asize, retlen,
				     (void **)&ptr, NULL);
		}
		memcpy(copyto, ptr, eoff_lo);
		copyto += eoff_lo;
	}

      out:
#ifdef CONFIG_MTD_PMC551_DEBUG
	printk(KERN_DEBUG "pmc551_read() done\n");
#endif
	*retlen = copyto - buf;
	return 0;
}

static int pmc551_write(struct mtd_info *mtd, loff_t to, size_t len,
			size_t * retlen, const u_char * buf)
{
	struct mypriv *priv = mtd->priv;
	u32 soff_hi, soff_lo;	/* start address offset hi/lo */
	u32 eoff_hi, eoff_lo;	/* end address offset hi/lo */
	unsigned long end;
	u_char *ptr;
	const u_char *copyfrom = buf;

#ifdef CONFIG_MTD_PMC551_DEBUG
	printk(KERN_DEBUG "pmc551_write(pos:%ld, len:%ld) asize:%ld\n",
		(long)to, (long)len, (long)priv->asize);
#endif

	end = to + len - 1;
	soff_hi = to & ~(priv->asize - 1);
	eoff_hi = end & ~(priv->asize - 1);
	soff_lo = to & (priv->asize - 1);
	eoff_lo = end & (priv->asize - 1);

	pmc551_point(mtd, to, len, retlen, (void **)&ptr, NULL);

	if (soff_hi == eoff_hi) {
		/* The whole thing fits within one access, so just one shot
		   will do it. */
		memcpy(ptr, copyfrom, len);
		copyfrom += len;
	} else {
		/* We have to do multiple writes to get all the data
		   written. */
		while (soff_hi != eoff_hi) {
#ifdef CONFIG_MTD_PMC551_DEBUG
			printk(KERN_DEBUG "pmc551_write() soff_hi: %ld, "
				"eoff_hi: %ld\n", (long)soff_hi, (long)eoff_hi);
#endif
			memcpy(ptr, copyfrom, priv->asize);
			copyfrom += priv->asize;
			if (soff_hi >= mtd->size) {
				goto out;
			}
			soff_hi += priv->asize;
			pmc551_point(mtd, soff_hi, priv->asize, retlen,
				     (void **)&ptr, NULL);
		}
		memcpy(ptr, copyfrom, eoff_lo);
		copyfrom += eoff_lo;
	}

      out:
#ifdef CONFIG_MTD_PMC551_DEBUG
	printk(KERN_DEBUG "pmc551_write() done\n");
#endif
	*retlen = copyfrom - buf;
	return 0;
}

/*
 * Fixup routines for the V370PDC
 * PCI device ID 0x020011b0
 *
 * This function basically kick starts the DRAM oboard the card and gets it
 * ready to be used.  Before this is done the device reads VERY erratic, so
 * much that it can crash the Linux 2.2.x series kernels when a user cat's
 * /proc/pci .. though that is mainly a kernel bug in handling the PCI DEVSEL
 * register.  FIXME: stop spinning on registers .. must implement a timeout
 * mechanism
 * returns the size of the memory region found.
 */
static int __init fixup_pmc551(struct pci_dev *dev)
{
#ifdef CONFIG_MTD_PMC551_BUGFIX
	u32 dram_data;
#endif
	u32 size, dcmd, cfg, dtmp;
	u16 cmd, tmp, i;
	u8 bcmd, counter;

	/* Sanity Check */
	if (!dev) {
		return -ENODEV;
	}

	/*
	 * Attempt to reset the card
	 * FIXME: Stop Spinning registers
	 */
	counter = 0;
	/* unlock registers */
	pci_write_config_byte(dev, PMC551_SYS_CTRL_REG, 0xA5);
	/* read in old data */
	pci_read_config_byte(dev, PMC551_SYS_CTRL_REG, &bcmd);
	/* bang the reset line up and down for a few */
	for (i = 0; i < 10; i++) {
		counter = 0;
		bcmd &= ~0x80;
		while (counter++ < 100) {
			pci_write_config_byte(dev, PMC551_SYS_CTRL_REG, bcmd);
		}
		counter = 0;
		bcmd |= 0x80;
		while (counter++ < 100) {
			pci_write_config_byte(dev, PMC551_SYS_CTRL_REG, bcmd);
		}
	}
	bcmd |= (0x40 | 0x20);
	pci_write_config_byte(dev, PMC551_SYS_CTRL_REG, bcmd);

	/*
	 * Take care and turn off the memory on the device while we
	 * tweak the configurations
	 */
	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	tmp = cmd & ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY);
	pci_write_config_word(dev, PCI_COMMAND, tmp);

	/*
	 * Disable existing aperture before probing memory size
	 */
	pci_read_config_dword(dev, PMC551_PCI_MEM_MAP0, &dcmd);
	dtmp = (dcmd | PMC551_PCI_MEM_MAP_ENABLE | PMC551_PCI_MEM_MAP_REG_EN);
	pci_write_config_dword(dev, PMC551_PCI_MEM_MAP0, dtmp);
	/*
	 * Grab old BAR0 config so that we can figure out memory size
	 * This is another bit of kludge going on.  The reason for the
	 * redundancy is I am hoping to retain the original configuration
	 * previously assigned to the card by the BIOS or some previous
	 * fixup routine in the kernel.  So we read the old config into cfg,
	 * then write all 1's to the memory space, read back the result into
	 * "size", and then write back all the old config.
	 */
	pci_read_config_dword(dev, PCI_BASE_ADDRESS_0, &cfg);
#ifndef CONFIG_MTD_PMC551_BUGFIX
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_0, ~0);
	pci_read_config_dword(dev, PCI_BASE_ADDRESS_0, &size);
	size = (size & PCI_BASE_ADDRESS_MEM_MASK);
	size &= ~(size - 1);
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_0, cfg);
#else
	/*
	 * Get the size of the memory by reading all the DRAM size values
	 * and adding them up.
	 *
	 * KLUDGE ALERT: the boards we are using have invalid column and
	 * row mux values.  We fix them here, but this will break other
	 * memory configurations.
	 */
	pci_read_config_dword(dev, PMC551_DRAM_BLK0, &dram_data);
	size = PMC551_DRAM_BLK_GET_SIZE(dram_data);
	dram_data = PMC551_DRAM_BLK_SET_COL_MUX(dram_data, 0x5);
	dram_data = PMC551_DRAM_BLK_SET_ROW_MUX(dram_data, 0x9);
	pci_write_config_dword(dev, PMC551_DRAM_BLK0, dram_data);

	pci_read_config_dword(dev, PMC551_DRAM_BLK1, &dram_data);
	size += PMC551_DRAM_BLK_GET_SIZE(dram_data);
	dram_data = PMC551_DRAM_BLK_SET_COL_MUX(dram_data, 0x5);
	dram_data = PMC551_DRAM_BLK_SET_ROW_MUX(dram_data, 0x9);
	pci_write_config_dword(dev, PMC551_DRAM_BLK1, dram_data);

	pci_read_config_dword(dev, PMC551_DRAM_BLK2, &dram_data);
	size += PMC551_DRAM_BLK_GET_SIZE(dram_data);
	dram_data = PMC551_DRAM_BLK_SET_COL_MUX(dram_data, 0x5);
	dram_data = PMC551_DRAM_BLK_SET_ROW_MUX(dram_data, 0x9);
	pci_write_config_dword(dev, PMC551_DRAM_BLK2, dram_data);

	pci_read_config_dword(dev, PMC551_DRAM_BLK3, &dram_data);
	size += PMC551_DRAM_BLK_GET_SIZE(dram_data);
	dram_data = PMC551_DRAM_BLK_SET_COL_MUX(dram_data, 0x5);
	dram_data = PMC551_DRAM_BLK_SET_ROW_MUX(dram_data, 0x9);
	pci_write_config_dword(dev, PMC551_DRAM_BLK3, dram_data);

	/*
	 * Oops .. something went wrong
	 */
	if ((size &= PCI_BASE_ADDRESS_MEM_MASK) == 0) {
		return -ENODEV;
	}
#endif				/* CONFIG_MTD_PMC551_BUGFIX */

	if ((cfg & PCI_BASE_ADDRESS_SPACE) != PCI_BASE_ADDRESS_SPACE_MEMORY) {
		return -ENODEV;
	}

	/*
	 * Precharge Dram
	 */
	pci_write_config_word(dev, PMC551_SDRAM_MA, 0x0400);
	pci_write_config_word(dev, PMC551_SDRAM_CMD, 0x00bf);

	/*
	 * Wait until command has gone through
	 * FIXME: register spinning issue
	 */
	do {
		pci_read_config_word(dev, PMC551_SDRAM_CMD, &cmd);
		if (counter++ > 100)
			break;
	} while ((PCI_COMMAND_IO) & cmd);

	/*
	 * Turn on auto refresh
	 * The loop is taken directly from Ramix's example code.  I assume that
	 * this must be held high for some duration of time, but I can find no
	 * documentation refrencing the reasons why.
	 */
	for (i = 1; i <= 8; i++) {
		pci_write_config_word(dev, PMC551_SDRAM_CMD, 0x0df);

		/*
		 * Make certain command has gone through
		 * FIXME: register spinning issue
		 */
		counter = 0;
		do {
			pci_read_config_word(dev, PMC551_SDRAM_CMD, &cmd);
			if (counter++ > 100)
				break;
		} while ((PCI_COMMAND_IO) & cmd);
	}

	pci_write_config_word(dev, PMC551_SDRAM_MA, 0x0020);
	pci_write_config_word(dev, PMC551_SDRAM_CMD, 0x0ff);

	/*
	 * Wait until command completes
	 * FIXME: register spinning issue
	 */
	counter = 0;
	do {
		pci_read_config_word(dev, PMC551_SDRAM_CMD, &cmd);
		if (counter++ > 100)
			break;
	} while ((PCI_COMMAND_IO) & cmd);

	pci_read_config_dword(dev, PMC551_DRAM_CFG, &dcmd);
	dcmd |= 0x02000000;
	pci_write_config_dword(dev, PMC551_DRAM_CFG, dcmd);

	/*
	 * Check to make certain fast back-to-back, if not
	 * then set it so
	 */
	pci_read_config_word(dev, PCI_STATUS, &cmd);
	if ((cmd & PCI_COMMAND_FAST_BACK) == 0) {
		cmd |= PCI_COMMAND_FAST_BACK;
		pci_write_config_word(dev, PCI_STATUS, cmd);
	}

	/*
	 * Check to make certain the DEVSEL is set correctly, this device
	 * has a tendency to assert DEVSEL and TRDY when a write is performed
	 * to the memory when memory is read-only
	 */
	if ((cmd & PCI_STATUS_DEVSEL_MASK) != 0x0) {
		cmd &= ~PCI_STATUS_DEVSEL_MASK;
		pci_write_config_word(dev, PCI_STATUS, cmd);
	}
	/*
	 * Set to be prefetchable and put everything back based on old cfg.
	 * it's possible that the reset of the V370PDC nuked the original
	 * setup
	 */
	/*
	   cfg |= PCI_BASE_ADDRESS_MEM_PREFETCH;
	   pci_write_config_dword( dev, PCI_BASE_ADDRESS_0, cfg );
	 */

	/*
	 * Turn PCI memory and I/O bus access back on
	 */
	pci_write_config_word(dev, PCI_COMMAND,
			      PCI_COMMAND_MEMORY | PCI_COMMAND_IO);
#ifdef CONFIG_MTD_PMC551_DEBUG
	/*
	 * Some screen fun
	 */
	printk(KERN_DEBUG "pmc551: %d%sB (0x%x) of %sprefetchable memory at "
		"0x%llx\n", (size < 1024) ? size : (size < 1048576) ?
		size >> 10 : size >> 20,
		(size < 1024) ? "" : (size < 1048576) ? "Ki" : "Mi", size,
		((dcmd & (0x1 << 3)) == 0) ? "non-" : "",
		(unsigned long long)pci_resource_start(dev, 0));

	/*
	 * Check to see the state of the memory
	 */
	pci_read_config_dword(dev, PMC551_DRAM_BLK0, &dcmd);
	printk(KERN_DEBUG "pmc551: DRAM_BLK0 Flags: %s,%s\n"
		"pmc551: DRAM_BLK0 Size: %d at %d\n"
		"pmc551: DRAM_BLK0 Row MUX: %d, Col MUX: %d\n",
		(((0x1 << 1) & dcmd) == 0) ? "RW" : "RO",
		(((0x1 << 0) & dcmd) == 0) ? "Off" : "On",
		PMC551_DRAM_BLK_GET_SIZE(dcmd),
		((dcmd >> 20) & 0x7FF), ((dcmd >> 13) & 0x7),
		((dcmd >> 9) & 0xF));

	pci_read_config_dword(dev, PMC551_DRAM_BLK1, &dcmd);
	printk(KERN_DEBUG "pmc551: DRAM_BLK1 Flags: %s,%s\n"
		"pmc551: DRAM_BLK1 Size: %d at %d\n"
		"pmc551: DRAM_BLK1 Row MUX: %d, Col MUX: %d\n",
		(((0x1 << 1) & dcmd) == 0) ? "RW" : "RO",
		(((0x1 << 0) & dcmd) == 0) ? "Off" : "On",
		PMC551_DRAM_BLK_GET_SIZE(dcmd),
		((dcmd >> 20) & 0x7FF), ((dcmd >> 13) & 0x7),
		((dcmd >> 9) & 0xF));

	pci_read_config_dword(dev, PMC551_DRAM_BLK2, &dcmd);
	printk(KERN_DEBUG "pmc551: DRAM_BLK2 Flags: %s,%s\n"
		"pmc551: DRAM_BLK2 Size: %d at %d\n"
		"pmc551: DRAM_BLK2 Row MUX: %d, Col MUX: %d\n",
		(((0x1 << 1) & dcmd) == 0) ? "RW" : "RO",
		(((0x1 << 0) & dcmd) == 0) ? "Off" : "On",
		PMC551_DRAM_BLK_GET_SIZE(dcmd),
		((dcmd >> 20) & 0x7FF), ((dcmd >> 13) & 0x7),
		((dcmd >> 9) & 0xF));

	pci_read_config_dword(dev, PMC551_DRAM_BLK3, &dcmd);
	printk(KERN_DEBUG "pmc551: DRAM_BLK3 Flags: %s,%s\n"
		"pmc551: DRAM_BLK3 Size: %d at %d\n"
		"pmc551: DRAM_BLK3 Row MUX: %d, Col MUX: %d\n",
		(((0x1 << 1) & dcmd) == 0) ? "RW" : "RO",
		(((0x1 << 0) & dcmd) == 0) ? "Off" : "On",
		PMC551_DRAM_BLK_GET_SIZE(dcmd),
		((dcmd >> 20) & 0x7FF), ((dcmd >> 13) & 0x7),
		((dcmd >> 9) & 0xF));

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	printk(KERN_DEBUG "pmc551: Memory Access %s\n",
		(((0x1 << 1) & cmd) == 0) ? "off" : "on");
	printk(KERN_DEBUG "pmc551: I/O Access %s\n",
		(((0x1 << 0) & cmd) == 0) ? "off" : "on");

	pci_read_config_word(dev, PCI_STATUS, &cmd);
	printk(KERN_DEBUG "pmc551: Devsel %s\n",
		((PCI_STATUS_DEVSEL_MASK & cmd) == 0x000) ? "Fast" :
		((PCI_STATUS_DEVSEL_MASK & cmd) == 0x200) ? "Medium" :
		((PCI_STATUS_DEVSEL_MASK & cmd) == 0x400) ? "Slow" : "Invalid");

	printk(KERN_DEBUG "pmc551: %sFast Back-to-Back\n",
		((PCI_COMMAND_FAST_BACK & cmd) == 0) ? "Not " : "");

	pci_read_config_byte(dev, PMC551_SYS_CTRL_REG, &bcmd);
	printk(KERN_DEBUG "pmc551: EEPROM is under %s control\n"
		"pmc551: System Control Register is %slocked to PCI access\n"
		"pmc551: System Control Register is %slocked to EEPROM access\n",
		(bcmd & 0x1) ? "software" : "hardware",
		(bcmd & 0x20) ? "" : "un", (bcmd & 0x40) ? "" : "un");
#endif
	return size;
}

/*
 * Kernel version specific module stuffages
 */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mark Ferrell <mferrell@mvista.com>");
MODULE_DESCRIPTION(PMC551_VERSION);

/*
 * Stuff these outside the ifdef so as to not bust compiled in driver support
 */
static int msize = 0;
static int asize = 0;

module_param(msize, int, 0);
MODULE_PARM_DESC(msize, "memory size in MiB [1 - 1024]");
module_param(asize, int, 0);
MODULE_PARM_DESC(asize, "aperture size, must be <= memsize [1-1024]");

/*
 * PMC551 Card Initialization
 */
static int __init init_pmc551(void)
{
	struct pci_dev *PCI_Device = NULL;
	struct mypriv *priv;
	int found = 0;
	struct mtd_info *mtd;
	int length = 0;

	if (msize) {
		msize = (1 << (ffs(msize) - 1)) << 20;
		if (msize > (1 << 30)) {
			printk(KERN_NOTICE "pmc551: Invalid memory size [%d]\n",
				msize);
			return -EINVAL;
		}
	}

	if (asize) {
		asize = (1 << (ffs(asize) - 1)) << 20;
		if (asize > (1 << 30)) {
			printk(KERN_NOTICE "pmc551: Invalid aperture size "
				"[%d]\n", asize);
			return -EINVAL;
		}
	}

	printk(KERN_INFO PMC551_VERSION);

	/*
	 * PCU-bus chipset probe.
	 */
	for (;;) {

		if ((PCI_Device = pci_get_device(PCI_VENDOR_ID_V3_SEMI,
						  PCI_DEVICE_ID_V3_SEMI_V370PDC,
						  PCI_Device)) == NULL) {
			break;
		}

		printk(KERN_NOTICE "pmc551: Found PCI V370PDC at 0x%llx\n",
			(unsigned long long)pci_resource_start(PCI_Device, 0));

		/*
		 * The PMC551 device acts VERY weird if you don't init it
		 * first.  i.e. it will not correctly report devsel.  If for
		 * some reason the sdram is in a wrote-protected state the
		 * device will DEVSEL when it is written to causing problems
		 * with the oldproc.c driver in
		 * some kernels (2.2.*)
		 */
		if ((length = fixup_pmc551(PCI_Device)) <= 0) {
			printk(KERN_NOTICE "pmc551: Cannot init SDRAM\n");
			break;
		}

		/*
		 * This is needed until the driver is capable of reading the
		 * onboard I2C SROM to discover the "real" memory size.
		 */
		if (msize) {
			length = msize;
			printk(KERN_NOTICE "pmc551: Using specified memory "
				"size 0x%x\n", length);
		} else {
			msize = length;
		}

		mtd = kzalloc(sizeof(struct mtd_info), GFP_KERNEL);
		if (!mtd)
			break;

		priv = kzalloc(sizeof(struct mypriv), GFP_KERNEL);
		if (!priv) {
			kfree(mtd);
			break;
		}
		mtd->priv = priv;
		priv->dev = PCI_Device;

		if (asize > length) {
			printk(KERN_NOTICE "pmc551: reducing aperture size to "
				"fit %dM\n", length >> 20);
			priv->asize = asize = length;
		} else if (asize == 0 || asize == length) {
			printk(KERN_NOTICE "pmc551: Using existing aperture "
				"size %dM\n", length >> 20);
			priv->asize = asize = length;
		} else {
			printk(KERN_NOTICE "pmc551: Using specified aperture "
				"size %dM\n", asize >> 20);
			priv->asize = asize;
		}
		priv->start = pci_iomap(PCI_Device, 0, priv->asize);

		if (!priv->start) {
			printk(KERN_NOTICE "pmc551: Unable to map IO space\n");
			kfree(mtd->priv);
			kfree(mtd);
			break;
		}
#ifdef CONFIG_MTD_PMC551_DEBUG
		printk(KERN_DEBUG "pmc551: setting aperture to %d\n",
			ffs(priv->asize >> 20) - 1);
#endif

		priv->base_map0 = (PMC551_PCI_MEM_MAP_REG_EN
				   | PMC551_PCI_MEM_MAP_ENABLE
				   | (ffs(priv->asize >> 20) - 1) << 4);
		priv->curr_map0 = priv->base_map0;
		pci_write_config_dword(priv->dev, PMC551_PCI_MEM_MAP0,
					priv->curr_map0);

#ifdef CONFIG_MTD_PMC551_DEBUG
		printk(KERN_DEBUG "pmc551: aperture set to %d\n",
			(priv->base_map0 & 0xF0) >> 4);
#endif

		mtd->size = msize;
		mtd->flags = MTD_CAP_RAM;
		mtd->_erase = pmc551_erase;
		mtd->_read = pmc551_read;
		mtd->_write = pmc551_write;
		mtd->_point = pmc551_point;
		mtd->_unpoint = pmc551_unpoint;
		mtd->type = MTD_RAM;
		mtd->name = "PMC551 RAM board";
		mtd->erasesize = 0x10000;
		mtd->writesize = 1;
		mtd->owner = THIS_MODULE;

		if (mtd_device_register(mtd, NULL, 0)) {
			printk(KERN_NOTICE "pmc551: Failed to register new device\n");
			pci_iounmap(PCI_Device, priv->start);
			kfree(mtd->priv);
			kfree(mtd);
			break;
		}

		/* Keep a reference as the mtd_device_register worked */
		pci_dev_get(PCI_Device);

		printk(KERN_NOTICE "Registered pmc551 memory device.\n");
		printk(KERN_NOTICE "Mapped %dMiB of memory from 0x%p to 0x%p\n",
			priv->asize >> 20,
			priv->start, priv->start + priv->asize);
		printk(KERN_NOTICE "Total memory is %d%sB\n",
			(length < 1024) ? length :
			(length < 1048576) ? length >> 10 : length >> 20,
			(length < 1024) ? "" : (length < 1048576) ? "Ki" : "Mi");
		priv->nextpmc551 = pmc551list;
		pmc551list = mtd;
		found++;
	}

	/* Exited early, reference left over */
	pci_dev_put(PCI_Device);

	if (!pmc551list) {
		printk(KERN_NOTICE "pmc551: not detected\n");
		return -ENODEV;
	} else {
		printk(KERN_NOTICE "pmc551: %d pmc551 devices loaded\n", found);
		return 0;
	}
}

/*
 * PMC551 Card Cleanup
 */
static void __exit cleanup_pmc551(void)
{
	int found = 0;
	struct mtd_info *mtd;
	struct mypriv *priv;

	while ((mtd = pmc551list)) {
		priv = mtd->priv;
		pmc551list = priv->nextpmc551;

		if (priv->start) {
			printk(KERN_DEBUG "pmc551: unmapping %dMiB starting at "
				"0x%p\n", priv->asize >> 20, priv->start);
			pci_iounmap(priv->dev, priv->start);
		}
		pci_dev_put(priv->dev);

		kfree(mtd->priv);
		mtd_device_unregister(mtd);
		kfree(mtd);
		found++;
	}

	printk(KERN_NOTICE "pmc551: %d pmc551 devices unloaded\n", found);
}

module_init(init_pmc551);
module_exit(cleanup_pmc551);
