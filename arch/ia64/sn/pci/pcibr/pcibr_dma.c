/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001-2004 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/pci.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/geo.h>
#include "xtalk/xwidgetdev.h"
#include "xtalk/hubdev.h"
#include "pci/pcibus_provider_defs.h"
#include "pci/pcidev.h"
#include "pci/tiocp.h"
#include "pci/pic.h"
#include "pci/pcibr_provider.h"
#include "pci/tiocp.h"
#include "tio.h"
#include <asm/sn/addrs.h>

extern int sn_ioif_inited;

/* =====================================================================
 *    DMA MANAGEMENT
 *
 *      The Bridge ASIC provides three methods of doing DMA: via a "direct map"
 *      register available in 32-bit PCI space (which selects a contiguous 2G
 *	address space on some other widget), via "direct" addressing via 64-bit
 *      PCI space (all destination information comes from the PCI address,
 *      including transfer attributes), and via a "mapped" region that allows 
 *      a bunch of different small mappings to be established with the PMU.
 *
 *      For efficiency, we most prefer to use the 32bit direct mapping facility,
 *      since it requires no resource allocations. The advantage of using the
 *      PMU over the 64-bit direct is that single-cycle PCI addressing can be
 *      used; the advantage of using 64-bit direct over PMU addressing is that
 *      we do not have to allocate entries in the PMU.
 */

static uint64_t
pcibr_dmamap_ate32(struct pcidev_info *info,
		   uint64_t paddr, size_t req_size, uint64_t flags)
{

	struct pcidev_info *pcidev_info = info->pdi_host_pcidev_info;
	struct pcibus_info *pcibus_info = (struct pcibus_info *)pcidev_info->
	    pdi_pcibus_info;
	uint8_t internal_device = (PCI_SLOT(pcidev_info->pdi_host_pcidev_info->
					    pdi_linux_pcidev->devfn)) - 1;
	int ate_count;
	int ate_index;
	uint64_t ate_flags = flags | PCI32_ATE_V;
	uint64_t ate;
	uint64_t pci_addr;
	uint64_t xio_addr;
	uint64_t offset;

	/* PIC in PCI-X mode does not supports 32bit PageMap mode */
	if (IS_PIC_SOFT(pcibus_info) && IS_PCIX(pcibus_info)) {
		return 0;
	}

	/* Calculate the number of ATEs needed. */
	if (!(MINIMAL_ATE_FLAG(paddr, req_size))) {
		ate_count = IOPG((IOPGSIZE - 1)	/* worst case start offset */
				 +req_size	/* max mapping bytes */
				 - 1) + 1;	/* round UP */
	} else {		/* assume requested target is page aligned */
		ate_count = IOPG(req_size	/* max mapping bytes */
				 - 1) + 1;	/* round UP */
	}

	/* Get the number of ATEs required. */
	ate_index = pcibr_ate_alloc(pcibus_info, ate_count);
	if (ate_index < 0)
		return 0;

	/* In PCI-X mode, Prefetch not supported */
	if (IS_PCIX(pcibus_info))
		ate_flags &= ~(PCI32_ATE_PREF);

	xio_addr =
	    IS_PIC_SOFT(pcibus_info) ? PHYS_TO_DMA(paddr) :
	    PHYS_TO_TIODMA(paddr);
	offset = IOPGOFF(xio_addr);
	ate = ate_flags | (xio_addr - offset);

	/* If PIC, put the targetid in the ATE */
	if (IS_PIC_SOFT(pcibus_info)) {
		ate |= (pcibus_info->pbi_hub_xid << PIC_ATE_TARGETID_SHFT);
	}
	ate_write(pcibus_info, ate_index, ate_count, ate);

	/*
	 * Set up the DMA mapped Address.
	 */
	pci_addr = PCI32_MAPPED_BASE + offset + IOPGSIZE * ate_index;

	/*
	 * If swap was set in device in pcibr_endian_set()
	 * we need to turn swapping on.
	 */
	if (pcibus_info->pbi_devreg[internal_device] & PCIBR_DEV_SWAP_DIR)
		ATE_SWAP_ON(pci_addr);

	return pci_addr;
}

static uint64_t
pcibr_dmatrans_direct64(struct pcidev_info * info, uint64_t paddr,
			uint64_t dma_attributes)
{
	struct pcibus_info *pcibus_info = (struct pcibus_info *)
	    ((info->pdi_host_pcidev_info)->pdi_pcibus_info);
	uint64_t pci_addr;

	/* Translate to Crosstalk View of Physical Address */
	pci_addr = (IS_PIC_SOFT(pcibus_info) ? PHYS_TO_DMA(paddr) :
		    PHYS_TO_TIODMA(paddr)) | dma_attributes;

	/* Handle Bus mode */
	if (IS_PCIX(pcibus_info))
		pci_addr &= ~PCI64_ATTR_PREF;

	/* Handle Bridge Chipset differences */
	if (IS_PIC_SOFT(pcibus_info)) {
		pci_addr |=
		    ((uint64_t) pcibus_info->
		     pbi_hub_xid << PIC_PCI64_ATTR_TARG_SHFT);
	} else
		pci_addr |= TIOCP_PCI64_CMDTYPE_MEM;

	/* If PCI mode, func zero uses VCHAN0, every other func uses VCHAN1 */
	if (!IS_PCIX(pcibus_info) && PCI_FUNC(info->pdi_linux_pcidev->devfn))
		pci_addr |= PCI64_ATTR_VIRTUAL;

	return pci_addr;

}

static uint64_t
pcibr_dmatrans_direct32(struct pcidev_info * info,
			uint64_t paddr, size_t req_size, uint64_t flags)
{

	struct pcidev_info *pcidev_info = info->pdi_host_pcidev_info;
	struct pcibus_info *pcibus_info = (struct pcibus_info *)pcidev_info->
	    pdi_pcibus_info;
	uint64_t xio_addr;

	uint64_t xio_base;
	uint64_t offset;
	uint64_t endoff;

	if (IS_PCIX(pcibus_info)) {
		return 0;
	}

	xio_addr = IS_PIC_SOFT(pcibus_info) ? PHYS_TO_DMA(paddr) :
	    PHYS_TO_TIODMA(paddr);

	xio_base = pcibus_info->pbi_dir_xbase;
	offset = xio_addr - xio_base;
	endoff = req_size + offset;
	if ((req_size > (1ULL << 31)) ||	/* Too Big */
	    (xio_addr < xio_base) ||	/* Out of range for mappings */
	    (endoff > (1ULL << 31))) {	/* Too Big */
		return 0;
	}

	return PCI32_DIRECT_BASE | offset;

}

/*
 * Wrapper routine for free'ing DMA maps
 * DMA mappings for Direct 64 and 32 do not have any DMA maps.
 */
void
pcibr_dma_unmap(struct pcidev_info *pcidev_info, dma_addr_t dma_handle,
		int direction)
{
	struct pcibus_info *pcibus_info = (struct pcibus_info *)pcidev_info->
	    pdi_pcibus_info;

	if (IS_PCI32_MAPPED(dma_handle)) {
		int ate_index;

		ate_index =
		    IOPG((ATE_SWAP_OFF(dma_handle) - PCI32_MAPPED_BASE));
		pcibr_ate_free(pcibus_info, ate_index);
	}
}

/*
 * On SN systems there is a race condition between a PIO read response and 
 * DMA's.  In rare cases, the read response may beat the DMA, causing the
 * driver to think that data in memory is complete and meaningful.  This code
 * eliminates that race.  This routine is called by the PIO read routines
 * after doing the read.  For PIC this routine then forces a fake interrupt
 * on another line, which is logically associated with the slot that the PIO
 * is addressed to.  It then spins while watching the memory location that
 * the interrupt is targetted to.  When the interrupt response arrives, we 
 * are sure that the DMA has landed in memory and it is safe for the driver
 * to proceed.	For TIOCP use the Device(x) Write Request Buffer Flush 
 * Bridge register since it ensures the data has entered the coherence domain,
 * unlike the PIC Device(x) Write Request Buffer Flush register.
 */

void sn_dma_flush(uint64_t addr)
{
	nasid_t nasid;
	int is_tio;
	int wid_num;
	int i, j;
	int bwin;
	uint64_t flags;
	struct hubdev_info *hubinfo;
	volatile struct sn_flush_device_list *p;
	struct sn_flush_nasid_entry *flush_nasid_list;

	if (!sn_ioif_inited)
		return;

	nasid = NASID_GET(addr);
	if (-1 == nasid_to_cnodeid(nasid))
		return;

	hubinfo = (NODEPDA(nasid_to_cnodeid(nasid)))->pdinfo;

	if (!hubinfo) {
		BUG();
	}
	is_tio = (nasid & 1);
	if (is_tio) {
		wid_num = TIO_SWIN_WIDGETNUM(addr);
		bwin = TIO_BWIN_WINDOWNUM(addr);
	} else {
		wid_num = SWIN_WIDGETNUM(addr);
		bwin = BWIN_WINDOWNUM(addr);
	}

	flush_nasid_list = &hubinfo->hdi_flush_nasid_list;
	if (flush_nasid_list->widget_p == NULL)
		return;
	if (bwin > 0) {
		uint64_t itte = flush_nasid_list->iio_itte[bwin];

		if (is_tio) {
			wid_num = (itte >> TIO_ITTE_WIDGET_SHIFT) &
			    TIO_ITTE_WIDGET_MASK;
		} else {
			wid_num = (itte >> IIO_ITTE_WIDGET_SHIFT) &
			    IIO_ITTE_WIDGET_MASK;
		}
	}
	if (flush_nasid_list->widget_p == NULL)
		return;
	if (flush_nasid_list->widget_p[wid_num] == NULL)
		return;
	p = &flush_nasid_list->widget_p[wid_num][0];

	/* find a matching BAR */
	for (i = 0; i < DEV_PER_WIDGET; i++) {
		for (j = 0; j < PCI_ROM_RESOURCE; j++) {
			if (p->sfdl_bar_list[j].start == 0)
				break;
			if (addr >= p->sfdl_bar_list[j].start
			    && addr <= p->sfdl_bar_list[j].end)
				break;
		}
		if (j < PCI_ROM_RESOURCE && p->sfdl_bar_list[j].start != 0)
			break;
		p++;
	}

	/* if no matching BAR, return without doing anything. */
	if (i == DEV_PER_WIDGET)
		return;

	/*
	 * For TIOCP use the Device(x) Write Request Buffer Flush Bridge
	 * register since it ensures the data has entered the coherence
	 * domain, unlike PIC
	 */
	if (is_tio) {
		uint32_t tio_id = REMOTE_HUB_L(nasid, TIO_NODE_ID);
		uint32_t revnum = XWIDGET_PART_REV_NUM(tio_id);

		/* TIOCP BRINGUP WAR (PV907516): Don't write buffer flush reg */
		if ((1 << XWIDGET_PART_REV_NUM_REV(revnum)) & PV907516) {
			return;
		} else {
			pcireg_wrb_flush_get(p->sfdl_pcibus_info,
					     (p->sfdl_slot - 1));
		}
	} else {
		spin_lock_irqsave(&((struct sn_flush_device_list *)p)->
				  sfdl_flush_lock, flags);

		p->sfdl_flush_value = 0;

		/* force an interrupt. */
		*(volatile uint32_t *)(p->sfdl_force_int_addr) = 1;

		/* wait for the interrupt to come back. */
		while (*(p->sfdl_flush_addr) != 0x10f) ;

		/* okay, everything is synched up. */
		spin_unlock_irqrestore((spinlock_t *)&p->sfdl_flush_lock, flags);
	}
	return;
}

/*
 * Wrapper DMA interface.  Called from pci_dma.c routines.
 */

uint64_t
pcibr_dma_map(struct pcidev_info * pcidev_info, unsigned long phys_addr,
	      size_t size, unsigned int flags)
{
	dma_addr_t dma_handle;
	struct pci_dev *pcidev = pcidev_info->pdi_linux_pcidev;

	if (flags & SN_PCIDMA_CONSISTENT) {
		/* sn_pci_alloc_consistent interfaces */
		if (pcidev->dev.coherent_dma_mask == ~0UL) {
			dma_handle =
			    pcibr_dmatrans_direct64(pcidev_info, phys_addr,
						    PCI64_ATTR_BAR);
		} else {
			dma_handle =
			    (dma_addr_t) pcibr_dmamap_ate32(pcidev_info,
							    phys_addr, size,
							    PCI32_ATE_BAR);
		}
	} else {
		/* map_sg/map_single interfaces */

		/* SN cannot support DMA addresses smaller than 32 bits. */
		if (pcidev->dma_mask < 0x7fffffff) {
			return 0;
		}

		if (pcidev->dma_mask == ~0UL) {
			/*
			 * Handle the most common case: 64 bit cards.  This
			 * call should always succeed.
			 */

			dma_handle =
			    pcibr_dmatrans_direct64(pcidev_info, phys_addr,
						    PCI64_ATTR_PREF);
		} else {
			/* Handle 32-63 bit cards via direct mapping */
			dma_handle =
			    pcibr_dmatrans_direct32(pcidev_info, phys_addr,
						    size, 0);
			if (!dma_handle) {
				/*
				 * It is a 32 bit card and we cannot do direct mapping,
				 * so we use an ATE.
				 */

				dma_handle =
				    pcibr_dmamap_ate32(pcidev_info, phys_addr,
						       size, PCI32_ATE_PREF);
			}
		}
	}

	return dma_handle;
}

EXPORT_SYMBOL(sn_dma_flush);
