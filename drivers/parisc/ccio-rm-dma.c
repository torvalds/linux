/*
 * ccio-rm-dma.c:
 *	DMA management routines for first generation cache-coherent machines.
 *	"Real Mode" operation refers to U2/Uturn chip operation. The chip
 *      can perform coherency checks w/o using the I/O MMU. That's all we
 *      need until support for more than 4GB phys mem is needed.
 * 
 *	This is the trivial case - basically what x86 does.
 *
 *	Drawbacks of using Real Mode are:
 *	o outbound DMA is slower since one isn't using the prefetching
 *	  U2 can do for outbound DMA.
 *	o Ability to do scatter/gather in HW is also lost.
 *      o only known to work with PCX-W processor. (eg C360)
 *        (PCX-U/U+ are not coherent with U2 in real mode.)
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *
 * Original version/author:
 *      CVSROOT=:pserver:anonymous@198.186.203.37:/cvsroot/linux-parisc
 *      cvs -z3 co linux/arch/parisc/kernel/dma-rm.c
 *
 *	(C) Copyright 2000 Philipp Rumpf <prumpf@tux.org>
 *
 *
 * Adopted for The Puffin Group's parisc-linux port by Grant Grundler.
 *	(C) Copyright 2000 Grant Grundler <grundler@puffin.external.hp.com>
 *	
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/pci.h>

#include <asm/uaccess.h>

#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/page.h>

/* Only chose "ccio" since that's what HP-UX calls it....
** Make it easier for folks to migrate from one to the other :^)
*/
#define MODULE_NAME "ccio"

#define U2_IOA_RUNWAY 0x580
#define U2_BC_GSC     0x501
#define UTURN_IOA_RUNWAY 0x581
#define UTURN_BC_GSC     0x502

#define IS_U2(id) ( \
    (((id)->hw_type == HPHW_IOA) && ((id)->hversion == U2_IOA_RUNWAY)) || \
    (((id)->hw_type == HPHW_BCPORT) && ((id)->hversion == U2_BC_GSC))  \
)

#define IS_UTURN(id) ( \
    (((id)->hw_type == HPHW_IOA) && ((id)->hversion == UTURN_IOA_RUNWAY)) || \
    (((id)->hw_type == HPHW_BCPORT) && ((id)->hversion == UTURN_BC_GSC))  \
)

static int ccio_dma_supported( struct pci_dev *dev, u64 mask)
{
	if (dev == NULL) {
		printk(KERN_ERR MODULE_NAME ": EISA/ISA/et al not supported\n");
		BUG();
		return(0);
	}

	/* only support 32-bit devices (ie PCI/GSC) */
	return((int) (mask >= 0xffffffffUL));
}


static void *ccio_alloc_consistent(struct pci_dev *dev, size_t size,
				 dma_addr_t *handle)
{
	void *ret;
	
	ret = (void *)__get_free_pages(GFP_ATOMIC, get_order(size));

	if (ret != NULL) {
		memset(ret, 0, size);
		*handle = virt_to_phys(ret);
	}
	return ret;
}
	
static void ccio_free_consistent(struct pci_dev *dev, size_t size,
			       void *vaddr, dma_addr_t handle)
{
	free_pages((unsigned long)vaddr, get_order(size));
}

static dma_addr_t ccio_map_single(struct pci_dev *dev, void *ptr, size_t size,
			  int direction)
{
	return virt_to_phys(ptr);
}

static void ccio_unmap_single(struct pci_dev *dev, dma_addr_t dma_addr,
			    size_t size, int direction)
{
	/* Nothing to do */
}


static int ccio_map_sg(struct pci_dev *dev, struct scatterlist *sglist, int nents, int direction)
{
	int tmp = nents;

        /* KISS: map each buffer separately. */
	while (nents) {
		sg_dma_address(sglist) = ccio_map_single(dev, sglist->address, sglist->length, direction);
		sg_dma_len(sglist) = sglist->length;
		nents--;
		sglist++;
	}

	return tmp;
}


static void ccio_unmap_sg(struct pci_dev *dev, struct scatterlist *sglist, int nents, int direction)
{
#if 0
	while (nents) {
		ccio_unmap_single(dev, sg_dma_address(sglist), sg_dma_len(sglist), direction);
		nents--;
		sglist++;
	}
	return;
#else
	/* Do nothing (copied from current ccio_unmap_single()  :^) */
#endif
}


static struct pci_dma_ops ccio_ops = {
	ccio_dma_supported,
	ccio_alloc_consistent,
	ccio_free_consistent,
	ccio_map_single,
	ccio_unmap_single,
	ccio_map_sg,
	ccio_unmap_sg,
	NULL,                   /* dma_sync_single_for_cpu : NOP for U2 */
	NULL,                   /* dma_sync_single_for_device : NOP for U2 */
	NULL,                   /* dma_sync_sg_for_cpu     : ditto */
	NULL,                   /* dma_sync_sg_for_device     : ditto */
};


/*
** Determine if u2 should claim this chip (return 0) or not (return 1).
** If so, initialize the chip and tell other partners in crime they
** have work to do.
*/
static int
ccio_probe(struct parisc_device *dev)
{
	printk(KERN_INFO "%s found %s at 0x%lx\n", MODULE_NAME,
			dev->id.hversion == U2_BC_GSC ? "U2" : "UTurn",
			dev->hpa.start);

/*
** FIXME - should check U2 registers to verify it's really running
** in "Real Mode".
*/

#if 0
/* will need this for "Virtual Mode" operation */
	ccio_hw_init(ccio_dev);
	ccio_common_init(ccio_dev);
#endif
	hppa_dma_ops = &ccio_ops;
	return 0;
}

static struct parisc_device_id ccio_tbl[] = {
	{ HPHW_BCPORT, HVERSION_REV_ANY_ID, U2_BC_GSC, 0xc },
	{ HPHW_BCPORT, HVERSION_REV_ANY_ID, UTURN_BC_GSC, 0xc },
	{ 0, }
};

static struct parisc_driver ccio_driver = {
	.name =		"U2/Uturn",
	.id_table =	ccio_tbl,
	.probe =	ccio_probe,
};

void __init ccio_init(void)
{
	register_parisc_driver(&ccio_driver);
}
