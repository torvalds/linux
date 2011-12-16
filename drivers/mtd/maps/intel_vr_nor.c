/*
 * drivers/mtd/maps/intel_vr_nor.c
 *
 * An MTD map driver for a NOR flash bank on the Expansion Bus of the Intel
 * Vermilion Range chipset.
 *
 * The Vermilion Range Expansion Bus supports four chip selects, each of which
 * has 64MiB of address space.  The 2nd BAR of the Expansion Bus PCI Device
 * is a 256MiB memory region containing the address spaces for all four of the
 * chip selects, with start addresses hardcoded on 64MiB boundaries.
 *
 * This map driver only supports NOR flash on chip select 0.  The buswidth
 * (either 8 bits or 16 bits) is determined by reading the Expansion Bus Timing
 * and Control Register for Chip Select 0 (EXP_TIMING_CS0).  This driver does
 * not modify the value in the EXP_TIMING_CS0 register except to enable writing
 * and disable boot acceleration.  The timing parameters in the register are
 * assumed to have been properly initialized by the BIOS.  The reset default
 * timing parameters are maximally conservative (slow), so access to the flash
 * will be slower than it should be if the BIOS has not initialized the timing
 * parameters.
 *
 * Author: Andy Lowe <alowe@mvista.com>
 *
 * 2006 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/cfi.h>
#include <linux/mtd/flashchip.h>

#define DRV_NAME "vr_nor"

struct vr_nor_mtd {
	void __iomem *csr_base;
	struct map_info map;
	struct mtd_info *info;
	struct pci_dev *dev;
};

/* Expansion Bus Configuration and Status Registers are in BAR 0 */
#define EXP_CSR_MBAR 0
/* Expansion Bus Memory Window is BAR 1 */
#define EXP_WIN_MBAR 1
/* Maximum address space for Chip Select 0 is 64MiB */
#define CS0_SIZE 0x04000000
/* Chip Select 0 is at offset 0 in the Memory Window */
#define CS0_START 0x0
/* Chip Select 0 Timing Register is at offset 0 in CSR */
#define EXP_TIMING_CS0 0x00
#define TIMING_CS_EN		(1 << 31)	/* Chip Select Enable */
#define TIMING_BOOT_ACCEL_DIS	(1 <<  8)	/* Boot Acceleration Disable */
#define TIMING_WR_EN		(1 <<  1)	/* Write Enable */
#define TIMING_BYTE_EN		(1 <<  0)	/* 8-bit vs 16-bit bus */
#define TIMING_MASK		0x3FFF0000

static void __devexit vr_nor_destroy_partitions(struct vr_nor_mtd *p)
{
	mtd_device_unregister(p->info);
}

static int __devinit vr_nor_init_partitions(struct vr_nor_mtd *p)
{
	/* register the flash bank */
	/* partition the flash bank */
	return mtd_device_parse_register(p->info, NULL, 0, NULL, 0);
}

static void __devexit vr_nor_destroy_mtd_setup(struct vr_nor_mtd *p)
{
	map_destroy(p->info);
}

static int __devinit vr_nor_mtd_setup(struct vr_nor_mtd *p)
{
	static const char *probe_types[] =
	    { "cfi_probe", "jedec_probe", NULL };
	const char **type;

	for (type = probe_types; !p->info && *type; type++)
		p->info = do_map_probe(*type, &p->map);
	if (!p->info)
		return -ENODEV;

	p->info->owner = THIS_MODULE;

	return 0;
}

static void __devexit vr_nor_destroy_maps(struct vr_nor_mtd *p)
{
	unsigned int exp_timing_cs0;

	/* write-protect the flash bank */
	exp_timing_cs0 = readl(p->csr_base + EXP_TIMING_CS0);
	exp_timing_cs0 &= ~TIMING_WR_EN;
	writel(exp_timing_cs0, p->csr_base + EXP_TIMING_CS0);

	/* unmap the flash window */
	iounmap(p->map.virt);

	/* unmap the csr window */
	iounmap(p->csr_base);
}

/*
 * Initialize the map_info structure and map the flash.
 * Returns 0 on success, nonzero otherwise.
 */
static int __devinit vr_nor_init_maps(struct vr_nor_mtd *p)
{
	unsigned long csr_phys, csr_len;
	unsigned long win_phys, win_len;
	unsigned int exp_timing_cs0;
	int err;

	csr_phys = pci_resource_start(p->dev, EXP_CSR_MBAR);
	csr_len = pci_resource_len(p->dev, EXP_CSR_MBAR);
	win_phys = pci_resource_start(p->dev, EXP_WIN_MBAR);
	win_len = pci_resource_len(p->dev, EXP_WIN_MBAR);

	if (!csr_phys || !csr_len || !win_phys || !win_len)
		return -ENODEV;

	if (win_len < (CS0_START + CS0_SIZE))
		return -ENXIO;

	p->csr_base = ioremap_nocache(csr_phys, csr_len);
	if (!p->csr_base)
		return -ENOMEM;

	exp_timing_cs0 = readl(p->csr_base + EXP_TIMING_CS0);
	if (!(exp_timing_cs0 & TIMING_CS_EN)) {
		dev_warn(&p->dev->dev, "Expansion Bus Chip Select 0 "
		       "is disabled.\n");
		err = -ENODEV;
		goto release;
	}
	if ((exp_timing_cs0 & TIMING_MASK) == TIMING_MASK) {
		dev_warn(&p->dev->dev, "Expansion Bus Chip Select 0 "
		       "is configured for maximally slow access times.\n");
	}
	p->map.name = DRV_NAME;
	p->map.bankwidth = (exp_timing_cs0 & TIMING_BYTE_EN) ? 1 : 2;
	p->map.phys = win_phys + CS0_START;
	p->map.size = CS0_SIZE;
	p->map.virt = ioremap_nocache(p->map.phys, p->map.size);
	if (!p->map.virt) {
		err = -ENOMEM;
		goto release;
	}
	simple_map_init(&p->map);

	/* Enable writes to flash bank */
	exp_timing_cs0 |= TIMING_BOOT_ACCEL_DIS | TIMING_WR_EN;
	writel(exp_timing_cs0, p->csr_base + EXP_TIMING_CS0);

	return 0;

      release:
	iounmap(p->csr_base);
	return err;
}

static struct pci_device_id vr_nor_pci_ids[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x500D)},
	{0,}
};

static void __devexit vr_nor_pci_remove(struct pci_dev *dev)
{
	struct vr_nor_mtd *p = pci_get_drvdata(dev);

	pci_set_drvdata(dev, NULL);
	vr_nor_destroy_partitions(p);
	vr_nor_destroy_mtd_setup(p);
	vr_nor_destroy_maps(p);
	kfree(p);
	pci_release_regions(dev);
	pci_disable_device(dev);
}

static int __devinit
vr_nor_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct vr_nor_mtd *p = NULL;
	unsigned int exp_timing_cs0;
	int err;

	err = pci_enable_device(dev);
	if (err)
		goto out;

	err = pci_request_regions(dev, DRV_NAME);
	if (err)
		goto disable_dev;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	err = -ENOMEM;
	if (!p)
		goto release;

	p->dev = dev;

	err = vr_nor_init_maps(p);
	if (err)
		goto release;

	err = vr_nor_mtd_setup(p);
	if (err)
		goto destroy_maps;

	err = vr_nor_init_partitions(p);
	if (err)
		goto destroy_mtd_setup;

	pci_set_drvdata(dev, p);

	return 0;

      destroy_mtd_setup:
	map_destroy(p->info);

      destroy_maps:
	/* write-protect the flash bank */
	exp_timing_cs0 = readl(p->csr_base + EXP_TIMING_CS0);
	exp_timing_cs0 &= ~TIMING_WR_EN;
	writel(exp_timing_cs0, p->csr_base + EXP_TIMING_CS0);

	/* unmap the flash window */
	iounmap(p->map.virt);

	/* unmap the csr window */
	iounmap(p->csr_base);

      release:
	kfree(p);
	pci_release_regions(dev);

      disable_dev:
	pci_disable_device(dev);

      out:
	return err;
}

static struct pci_driver vr_nor_pci_driver = {
	.name = DRV_NAME,
	.probe = vr_nor_pci_probe,
	.remove = __devexit_p(vr_nor_pci_remove),
	.id_table = vr_nor_pci_ids,
};

static int __init vr_nor_mtd_init(void)
{
	return pci_register_driver(&vr_nor_pci_driver);
}

static void __exit vr_nor_mtd_exit(void)
{
	pci_unregister_driver(&vr_nor_pci_driver);
}

module_init(vr_nor_mtd_init);
module_exit(vr_nor_mtd_exit);

MODULE_AUTHOR("Andy Lowe");
MODULE_DESCRIPTION("MTD map driver for NOR flash on Intel Vermilion Range");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, vr_nor_pci_ids);
