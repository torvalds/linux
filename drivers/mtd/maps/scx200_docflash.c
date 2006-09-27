/* linux/drivers/mtd/maps/scx200_docflash.c

   Copyright (c) 2001,2002 Christer Weinigel <wingel@nano-system.com>

   $Id: scx200_docflash.c,v 1.12 2005/11/07 11:14:28 gleixner Exp $

   National Semiconductor SCx200 flash mapped with DOCCS
*/

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <linux/pci.h>
#include <linux/scx200.h>

#define NAME "scx200_docflash"

MODULE_AUTHOR("Christer Weinigel <wingel@hack.org>");
MODULE_DESCRIPTION("NatSemi SCx200 DOCCS Flash Driver");
MODULE_LICENSE("GPL");

static int probe = 0;		/* Don't autoprobe */
static unsigned size = 0x1000000; /* 16 MiB the whole ISA address space */
static unsigned width = 8;	/* Default to 8 bits wide */
static char *flashtype = "cfi_probe";

module_param(probe, int, 0);
MODULE_PARM_DESC(probe, "Probe for a BIOS mapping");
module_param(size, int, 0);
MODULE_PARM_DESC(size, "Size of the flash mapping");
module_param(width, int, 0);
MODULE_PARM_DESC(width, "Data width of the flash mapping (8/16)");
module_param(flashtype, charp, 0);
MODULE_PARM_DESC(flashtype, "Type of MTD probe to do");

static struct resource docmem = {
	.flags = IORESOURCE_MEM,
	.name  = "NatSemi SCx200 DOCCS Flash",
};

static struct mtd_info *mymtd;

#ifdef CONFIG_MTD_PARTITIONS
static struct mtd_partition partition_info[] = {
	{
		.name   = "DOCCS Boot kernel",
		.offset = 0,
		.size   = 0xc0000
	},
	{
		.name   = "DOCCS Low BIOS",
		.offset = 0xc0000,
		.size   = 0x40000
	},
	{
		.name   = "DOCCS File system",
		.offset = 0x100000,
		.size   = ~0	/* calculate from flash size */
	},
	{
		.name   = "DOCCS High BIOS",
		.offset = ~0, 	/* calculate from flash size */
		.size   = 0x80000
	},
};
#define NUM_PARTITIONS ARRAY_SIZE(partition_info)
#endif


static struct map_info scx200_docflash_map = {
	.name      = "NatSemi SCx200 DOCCS Flash",
};

static int __init init_scx200_docflash(void)
{
	unsigned u;
	unsigned base;
	unsigned ctrl;
	unsigned pmr;
	struct pci_dev *bridge;

	printk(KERN_DEBUG NAME ": NatSemi SCx200 DOCCS Flash Driver\n");

	if ((bridge = pci_get_device(PCI_VENDOR_ID_NS,
				      PCI_DEVICE_ID_NS_SCx200_BRIDGE,
				      NULL)) == NULL)
		return -ENODEV;

	/* check that we have found the configuration block */
	if (!scx200_cb_present()) {
		pci_dev_put(bridge);
		return -ENODEV;
	}

	if (probe) {
		/* Try to use the present flash mapping if any */
		pci_read_config_dword(bridge, SCx200_DOCCS_BASE, &base);
		pci_read_config_dword(bridge, SCx200_DOCCS_CTRL, &ctrl);
		pci_dev_put(bridge);

		pmr = inl(scx200_cb_base + SCx200_PMR);

		if (base == 0
		    || (ctrl & 0x07000000) != 0x07000000
		    || (ctrl & 0x0007ffff) == 0)
			return -ENODEV;

		size = ((ctrl&0x1fff)<<13) + (1<<13);

		for (u = size; u > 1; u >>= 1)
			;
		if (u != 1)
			return -ENODEV;

		if (pmr & (1<<6))
			width = 16;
		else
			width = 8;

		docmem.start = base;
		docmem.end = base + size;

		if (request_resource(&iomem_resource, &docmem)) {
			printk(KERN_ERR NAME ": unable to allocate memory for flash mapping\n");
			return -ENOMEM;
		}
	} else {
		pci_dev_put(bridge);
		for (u = size; u > 1; u >>= 1)
			;
		if (u != 1) {
			printk(KERN_ERR NAME ": invalid size for flash mapping\n");
			return -EINVAL;
		}

		if (width != 8 && width != 16) {
			printk(KERN_ERR NAME ": invalid bus width for flash mapping\n");
			return -EINVAL;
		}

		if (allocate_resource(&iomem_resource, &docmem,
				      size,
				      0xc0000000, 0xffffffff,
				      size, NULL, NULL)) {
			printk(KERN_ERR NAME ": unable to allocate memory for flash mapping\n");
			return -ENOMEM;
		}

		ctrl = 0x07000000 | ((size-1) >> 13);

		printk(KERN_INFO "DOCCS BASE=0x%08lx, CTRL=0x%08lx\n", (long)docmem.start, (long)ctrl);

		pci_write_config_dword(bridge, SCx200_DOCCS_BASE, docmem.start);
		pci_write_config_dword(bridge, SCx200_DOCCS_CTRL, ctrl);
		pmr = inl(scx200_cb_base + SCx200_PMR);

		if (width == 8) {
			pmr &= ~(1<<6);
		} else {
			pmr |= (1<<6);
		}
		outl(pmr, scx200_cb_base + SCx200_PMR);
	}

       	printk(KERN_INFO NAME ": DOCCS mapped at 0x%llx-0x%llx, width %d\n",
			(unsigned long long)docmem.start,
			(unsigned long long)docmem.end, width);

	scx200_docflash_map.size = size;
	if (width == 8)
		scx200_docflash_map.bankwidth = 1;
	else
		scx200_docflash_map.bankwidth = 2;

	simple_map_init(&scx200_docflash_map);

	scx200_docflash_map.phys = docmem.start;
	scx200_docflash_map.virt = ioremap(docmem.start, scx200_docflash_map.size);
	if (!scx200_docflash_map.virt) {
		printk(KERN_ERR NAME ": failed to ioremap the flash\n");
		release_resource(&docmem);
		return -EIO;
	}

	mymtd = do_map_probe(flashtype, &scx200_docflash_map);
	if (!mymtd) {
		printk(KERN_ERR NAME ": unable to detect flash\n");
		iounmap(scx200_docflash_map.virt);
		release_resource(&docmem);
		return -ENXIO;
	}

	if (size < mymtd->size)
		printk(KERN_WARNING NAME ": warning, flash mapping is smaller than flash size\n");

	mymtd->owner = THIS_MODULE;

#ifdef CONFIG_MTD_PARTITIONS
	partition_info[3].offset = mymtd->size-partition_info[3].size;
	partition_info[2].size = partition_info[3].offset-partition_info[2].offset;
	add_mtd_partitions(mymtd, partition_info, NUM_PARTITIONS);
#else
	add_mtd_device(mymtd);
#endif
	return 0;
}

static void __exit cleanup_scx200_docflash(void)
{
	if (mymtd) {
#ifdef CONFIG_MTD_PARTITIONS
		del_mtd_partitions(mymtd);
#else
		del_mtd_device(mymtd);
#endif
		map_destroy(mymtd);
	}
	if (scx200_docflash_map.virt) {
		iounmap(scx200_docflash_map.virt);
		release_resource(&docmem);
	}
}

module_init(init_scx200_docflash);
module_exit(cleanup_scx200_docflash);

/*
    Local variables:
        compile-command: "make -k -C ../../.. SUBDIRS=drivers/mtd/maps modules"
        c-basic-offset: 8
    End:
*/
