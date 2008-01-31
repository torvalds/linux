/*
 * Copyright (C) 2005-2007, PA Semi, Inc
 *
 * Maintained by: Olof Johansson <olof@lixom.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#undef DEBUG

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <asm/iommu.h>
#include <asm/machdep.h>
#include <asm/abs_addr.h>
#include <asm/firmware.h>


#define IOBMAP_PAGE_SHIFT	12
#define IOBMAP_PAGE_SIZE	(1 << IOBMAP_PAGE_SHIFT)
#define IOBMAP_PAGE_MASK	(IOBMAP_PAGE_SIZE - 1)

#define IOB_BASE		0xe0000000
#define IOB_SIZE		0x3000
/* Configuration registers */
#define IOBCAP_REG		0x10
#define IOBCOM_REG		0x40
/* Enable IOB address translation */
#define IOBCOM_ATEN		0x00000100

/* Address decode configuration register */
#define IOB_AD_REG		0x53
/* IOBCOM_AD_REG fields */
#define IOB_AD_VGPRT		0x00000e00
#define IOB_AD_VGAEN		0x00000100
/* Direct mapping settings */
#define IOB_AD_MPSEL_MASK	0x00000030
#define IOB_AD_MPSEL_B38	0x00000000
#define IOB_AD_MPSEL_B40	0x00000010
#define IOB_AD_MPSEL_B42	0x00000020
/* Translation window size / enable */
#define IOB_AD_TRNG_MASK	0x00000003
#define IOB_AD_TRNG_256M	0x00000000
#define IOB_AD_TRNG_2G		0x00000001
#define IOB_AD_TRNG_128G	0x00000003

#define IOB_TABLEBASE_REG	0x55

/* Base of the 64 4-byte L1 registers */
#define IOB_XLT_L1_REGBASE	0xac0

/* Register to invalidate TLB entries */
#define IOB_AT_INVAL_TLB_REG	0xb40

/* The top two bits of the level 1 entry contains valid and type flags */
#define IOBMAP_L1E_V		0x40000000
#define IOBMAP_L1E_V_B		0x80000000

/* For big page entries, the bottom two bits contains flags */
#define IOBMAP_L1E_BIG_CACHED	0x00000002
#define IOBMAP_L1E_BIG_PRIORITY	0x00000001

/* For regular level 2 entries, top 2 bits contain valid and cache flags */
#define IOBMAP_L2E_V		0x80000000
#define IOBMAP_L2E_V_CACHED	0xc0000000

static u32 __iomem *iob;
static u32 iob_l1_emptyval;
static u32 iob_l2_emptyval;
static u32 *iob_l2_base;

static struct iommu_table iommu_table_iobmap;
static int iommu_table_iobmap_inited;

static void iobmap_build(struct iommu_table *tbl, long index,
			 long npages, unsigned long uaddr,
			 enum dma_data_direction direction)
{
	u32 *ip;
	u32 rpn;
	unsigned long bus_addr;

	pr_debug("iobmap: build at: %lx, %lx, addr: %lx\n", index, npages, uaddr);

	bus_addr = (tbl->it_offset + index) << IOBMAP_PAGE_SHIFT;

	ip = ((u32 *)tbl->it_base) + index;

	while (npages--) {
		rpn = virt_to_abs(uaddr) >> IOBMAP_PAGE_SHIFT;

		*(ip++) = IOBMAP_L2E_V | rpn;
		/* invalidate tlb, can be optimized more */
		out_le32(iob+IOB_AT_INVAL_TLB_REG, bus_addr >> 14);

		uaddr += IOBMAP_PAGE_SIZE;
		bus_addr += IOBMAP_PAGE_SIZE;
	}
}


static void iobmap_free(struct iommu_table *tbl, long index,
			long npages)
{
	u32 *ip;
	unsigned long bus_addr;

	pr_debug("iobmap: free at: %lx, %lx\n", index, npages);

	bus_addr = (tbl->it_offset + index) << IOBMAP_PAGE_SHIFT;

	ip = ((u32 *)tbl->it_base) + index;

	while (npages--) {
		*(ip++) = iob_l2_emptyval;
		/* invalidate tlb, can be optimized more */
		out_le32(iob+IOB_AT_INVAL_TLB_REG, bus_addr >> 14);
		bus_addr += IOBMAP_PAGE_SIZE;
	}
}


static void iommu_table_iobmap_setup(void)
{
	pr_debug(" -> %s\n", __func__);
	iommu_table_iobmap.it_busno = 0;
	iommu_table_iobmap.it_offset = 0;
	/* it_size is in number of entries */
	iommu_table_iobmap.it_size = 0x80000000 >> IOBMAP_PAGE_SHIFT;

	/* Initialize the common IOMMU code */
	iommu_table_iobmap.it_base = (unsigned long)iob_l2_base;
	iommu_table_iobmap.it_index = 0;
	/* XXXOJN tune this to avoid IOB cache invals.
	 * Should probably be 8 (64 bytes)
	 */
	iommu_table_iobmap.it_blocksize = 4;
	iommu_init_table(&iommu_table_iobmap, 0);
	pr_debug(" <- %s\n", __func__);
}



static void pci_dma_bus_setup_pasemi(struct pci_bus *bus)
{
	struct device_node *dn;

	pr_debug("pci_dma_bus_setup, bus %p, bus->self %p\n", bus, bus->self);

	if (!iommu_table_iobmap_inited) {
		iommu_table_iobmap_inited = 1;
		iommu_table_iobmap_setup();
	}

	dn = pci_bus_to_OF_node(bus);

	if (dn)
		PCI_DN(dn)->iommu_table = &iommu_table_iobmap;

}


static void pci_dma_dev_setup_pasemi(struct pci_dev *dev)
{
	pr_debug("pci_dma_dev_setup, dev %p (%s)\n", dev, pci_name(dev));

#if !defined(CONFIG_PPC_PASEMI_IOMMU_DMA_FORCE)
	/* For non-LPAR environment, don't translate anything for the DMA
	 * engine. The exception to this is if the user has enabled
	 * CONFIG_PPC_PASEMI_IOMMU_DMA_FORCE at build time.
	 */
	if (dev->vendor == 0x1959 && dev->device == 0xa007 &&
	    !firmware_has_feature(FW_FEATURE_LPAR)) {
		dev->dev.archdata.dma_ops = &dma_direct_ops;
		return;
	}
#endif

	dev->dev.archdata.dma_data = &iommu_table_iobmap;
}

static void pci_dma_bus_setup_null(struct pci_bus *b) { }
static void pci_dma_dev_setup_null(struct pci_dev *d) { }

int __init iob_init(struct device_node *dn)
{
	unsigned long tmp;
	u32 regword;
	int i;

	pr_debug(" -> %s\n", __func__);

	/* Allocate a spare page to map all invalid IOTLB pages. */
	tmp = lmb_alloc(IOBMAP_PAGE_SIZE, IOBMAP_PAGE_SIZE);
	if (!tmp)
		panic("IOBMAP: Cannot allocate spare page!");
	/* Empty l1 is marked invalid */
	iob_l1_emptyval = 0;
	/* Empty l2 is mapped to dummy page */
	iob_l2_emptyval = IOBMAP_L2E_V | (tmp >> IOBMAP_PAGE_SHIFT);

	iob = ioremap(IOB_BASE, IOB_SIZE);
	if (!iob)
		panic("IOBMAP: Cannot map registers!");

	/* setup direct mapping of the L1 entries */
	for (i = 0; i < 64; i++) {
		/* Each L1 covers 32MB, i.e. 8K entries = 32K of ram */
		regword = IOBMAP_L1E_V | (__pa(iob_l2_base + i*0x2000) >> 12);
		out_le32(iob+IOB_XLT_L1_REGBASE+i, regword);
	}

	/* set 2GB translation window, based at 0 */
	regword = in_le32(iob+IOB_AD_REG);
	regword &= ~IOB_AD_TRNG_MASK;
	regword |= IOB_AD_TRNG_2G;
	out_le32(iob+IOB_AD_REG, regword);

	/* Enable translation */
	regword = in_le32(iob+IOBCOM_REG);
	regword |= IOBCOM_ATEN;
	out_le32(iob+IOBCOM_REG, regword);

	pr_debug(" <- %s\n", __func__);

	return 0;
}


/* These are called very early. */
void __init iommu_init_early_pasemi(void)
{
	int iommu_off;

#ifndef CONFIG_PPC_PASEMI_IOMMU
	iommu_off = 1;
#else
	iommu_off = of_chosen &&
			of_get_property(of_chosen, "linux,iommu-off", NULL);
#endif
	if (iommu_off) {
		/* Direct I/O, IOMMU off */
		ppc_md.pci_dma_dev_setup = pci_dma_dev_setup_null;
		ppc_md.pci_dma_bus_setup = pci_dma_bus_setup_null;
		set_pci_dma_ops(&dma_direct_ops);

		return;
	}

	iob_init(NULL);

	ppc_md.pci_dma_dev_setup = pci_dma_dev_setup_pasemi;
	ppc_md.pci_dma_bus_setup = pci_dma_bus_setup_pasemi;
	ppc_md.tce_build = iobmap_build;
	ppc_md.tce_free  = iobmap_free;
	set_pci_dma_ops(&dma_iommu_ops);
}

void __init alloc_iobmap_l2(void)
{
#ifndef CONFIG_PPC_PASEMI_IOMMU
	return;
#endif
	/* For 2G space, 8x64 pages (2^21 bytes) is max total l2 size */
	iob_l2_base = (u32 *)abs_to_virt(lmb_alloc_base(1UL<<21, 1UL<<21, 0x80000000));

	printk(KERN_INFO "IOBMAP L2 allocated at: %p\n", iob_l2_base);
}
