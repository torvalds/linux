/*
 * IBM PPC4xx DMA engine scatter/gather library
 *
 * Copyright 2002-2003 MontaVista Software Inc.
 *
 * Cleaned up and converted to new DCR access
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * Original code by Armin Kuster <akuster@mvista.com>
 * and Pete Popov <ppopov@mvista.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/dma-mapping.h>
#include <asm/ppc4xx_dma.h>

void
ppc4xx_set_sg_addr(int dmanr, phys_addr_t sg_addr)
{
	if (dmanr >= MAX_PPC4xx_DMA_CHANNELS) {
		printk("ppc4xx_set_sg_addr: bad channel: %d\n", dmanr);
		return;
	}

#ifdef PPC4xx_DMA_64BIT
	mtdcr(DCRN_ASGH0 + (dmanr * 0x8), (u32)(sg_addr >> 32));
#endif
	mtdcr(DCRN_ASG0 + (dmanr * 0x8), (u32)sg_addr);
}

/*
 *   Add a new sgl descriptor to the end of a scatter/gather list
 *   which was created by alloc_dma_handle().
 *
 *   For a memory to memory transfer, both dma addresses must be
 *   valid. For a peripheral to memory transfer, one of the addresses
 *   must be set to NULL, depending on the direction of the transfer:
 *   memory to peripheral: set dst_addr to NULL,
 *   peripheral to memory: set src_addr to NULL.
 */
int
ppc4xx_add_dma_sgl(sgl_handle_t handle, phys_addr_t src_addr, phys_addr_t dst_addr,
		   unsigned int count)
{
	sgl_list_info_t *psgl = (sgl_list_info_t *) handle;
	ppc_dma_ch_t *p_dma_ch;

	if (!handle) {
		printk("ppc4xx_add_dma_sgl: null handle\n");
		return DMA_STATUS_BAD_HANDLE;
	}

	if (psgl->dmanr >= MAX_PPC4xx_DMA_CHANNELS) {
		printk("ppc4xx_add_dma_sgl: bad channel: %d\n", psgl->dmanr);
		return DMA_STATUS_BAD_CHANNEL;
	}

	p_dma_ch = &dma_channels[psgl->dmanr];

#ifdef DEBUG_4xxDMA
	{
		int error = 0;
		unsigned int aligned =
		    (unsigned) src_addr | (unsigned) dst_addr | count;
		switch (p_dma_ch->pwidth) {
		case PW_8:
			break;
		case PW_16:
			if (aligned & 0x1)
				error = 1;
			break;
		case PW_32:
			if (aligned & 0x3)
				error = 1;
			break;
		case PW_64:
			if (aligned & 0x7)
				error = 1;
			break;
		default:
			printk("ppc4xx_add_dma_sgl: invalid bus width: 0x%x\n",
			       p_dma_ch->pwidth);
			return DMA_STATUS_GENERAL_ERROR;
		}
		if (error)
			printk
			    ("Alignment warning: ppc4xx_add_dma_sgl src 0x%x dst 0x%x count 0x%x bus width var %d\n",
			     src_addr, dst_addr, count, p_dma_ch->pwidth);

	}
#endif

	if ((unsigned) (psgl->ptail + 1) >= ((unsigned) psgl + SGL_LIST_SIZE)) {
		printk("sgl handle out of memory \n");
		return DMA_STATUS_OUT_OF_MEMORY;
	}

	if (!psgl->ptail) {
		psgl->phead = (ppc_sgl_t *)
		    ((unsigned) psgl + sizeof (sgl_list_info_t));
		psgl->phead_dma = psgl->dma_addr + sizeof(sgl_list_info_t);
		psgl->ptail = psgl->phead;
		psgl->ptail_dma = psgl->phead_dma;
	} else {
		if(p_dma_ch->int_on_final_sg) {
			/* mask out all dma interrupts, except error, on tail
			before adding new tail. */
			psgl->ptail->control_count &=
				~(SG_TCI_ENABLE | SG_ETI_ENABLE);
		}
		psgl->ptail->next = psgl->ptail_dma + sizeof(ppc_sgl_t);
		psgl->ptail++;
		psgl->ptail_dma += sizeof(ppc_sgl_t);
	}

	psgl->ptail->control = psgl->control;
	psgl->ptail->src_addr = src_addr;
	psgl->ptail->dst_addr = dst_addr;
	psgl->ptail->control_count = (count >> p_dma_ch->shift) |
	    psgl->sgl_control;
	psgl->ptail->next = (uint32_t) NULL;

	return DMA_STATUS_GOOD;
}

/*
 * Enable (start) the DMA described by the sgl handle.
 */
void
ppc4xx_enable_dma_sgl(sgl_handle_t handle)
{
	sgl_list_info_t *psgl = (sgl_list_info_t *) handle;
	ppc_dma_ch_t *p_dma_ch;
	uint32_t sg_command;

	if (!handle) {
		printk("ppc4xx_enable_dma_sgl: null handle\n");
		return;
	} else if (psgl->dmanr > (MAX_PPC4xx_DMA_CHANNELS - 1)) {
		printk("ppc4xx_enable_dma_sgl: bad channel in handle %d\n",
		       psgl->dmanr);
		return;
	} else if (!psgl->phead) {
		printk("ppc4xx_enable_dma_sgl: sg list empty\n");
		return;
	}

	p_dma_ch = &dma_channels[psgl->dmanr];
	psgl->ptail->control_count &= ~SG_LINK;	/* make this the last dscrptr */
	sg_command = mfdcr(DCRN_ASGC);

	ppc4xx_set_sg_addr(psgl->dmanr, psgl->phead_dma);

	sg_command |= SSG_ENABLE(psgl->dmanr);

	mtdcr(DCRN_ASGC, sg_command);	/* start transfer */
}

/*
 * Halt an active scatter/gather DMA operation.
 */
void
ppc4xx_disable_dma_sgl(sgl_handle_t handle)
{
	sgl_list_info_t *psgl = (sgl_list_info_t *) handle;
	uint32_t sg_command;

	if (!handle) {
		printk("ppc4xx_enable_dma_sgl: null handle\n");
		return;
	} else if (psgl->dmanr > (MAX_PPC4xx_DMA_CHANNELS - 1)) {
		printk("ppc4xx_enable_dma_sgl: bad channel in handle %d\n",
		       psgl->dmanr);
		return;
	}

	sg_command = mfdcr(DCRN_ASGC);
	sg_command &= ~SSG_ENABLE(psgl->dmanr);
	mtdcr(DCRN_ASGC, sg_command);	/* stop transfer */
}

/*
 *  Returns number of bytes left to be transferred from the entire sgl list.
 *  *src_addr and *dst_addr get set to the source/destination address of
 *  the sgl descriptor where the DMA stopped.
 *
 *  An sgl transfer must NOT be active when this function is called.
 */
int
ppc4xx_get_dma_sgl_residue(sgl_handle_t handle, phys_addr_t * src_addr,
			   phys_addr_t * dst_addr)
{
	sgl_list_info_t *psgl = (sgl_list_info_t *) handle;
	ppc_dma_ch_t *p_dma_ch;
	ppc_sgl_t *pnext, *sgl_addr;
	uint32_t count_left;

	if (!handle) {
		printk("ppc4xx_get_dma_sgl_residue: null handle\n");
		return DMA_STATUS_BAD_HANDLE;
	} else if (psgl->dmanr > (MAX_PPC4xx_DMA_CHANNELS - 1)) {
		printk("ppc4xx_get_dma_sgl_residue: bad channel in handle %d\n",
		       psgl->dmanr);
		return DMA_STATUS_BAD_CHANNEL;
	}

	sgl_addr = (ppc_sgl_t *) __va(mfdcr(DCRN_ASG0 + (psgl->dmanr * 0x8)));
	count_left = mfdcr(DCRN_DMACT0 + (psgl->dmanr * 0x8)) & SG_COUNT_MASK;

	if (!sgl_addr) {
		printk("ppc4xx_get_dma_sgl_residue: sgl addr register is null\n");
		goto error;
	}

	pnext = psgl->phead;
	while (pnext &&
	       ((unsigned) pnext < ((unsigned) psgl + SGL_LIST_SIZE) &&
		(pnext != sgl_addr))
	    ) {
		pnext++;
	}

	if (pnext == sgl_addr) {	/* found the sgl descriptor */

		*src_addr = pnext->src_addr;
		*dst_addr = pnext->dst_addr;

		/*
		 * Now search the remaining descriptors and add their count.
		 * We already have the remaining count from this descriptor in
		 * count_left.
		 */
		pnext++;

		while ((pnext != psgl->ptail) &&
		       ((unsigned) pnext < ((unsigned) psgl + SGL_LIST_SIZE))
		    ) {
			count_left += pnext->control_count & SG_COUNT_MASK;
		}

		if (pnext != psgl->ptail) {	/* should never happen */
			printk
			    ("ppc4xx_get_dma_sgl_residue error (1) psgl->ptail 0x%x handle 0x%x\n",
			     (unsigned int) psgl->ptail, (unsigned int) handle);
			goto error;
		}

		/* success */
		p_dma_ch = &dma_channels[psgl->dmanr];
		return (count_left << p_dma_ch->shift);	/* count in bytes */

	} else {
		/* this shouldn't happen */
		printk
		    ("get_dma_sgl_residue, unable to match current address 0x%x, handle 0x%x\n",
		     (unsigned int) sgl_addr, (unsigned int) handle);

	}

      error:
	*src_addr = (phys_addr_t) NULL;
	*dst_addr = (phys_addr_t) NULL;
	return 0;
}

/*
 * Returns the address(es) of the buffer(s) contained in the head element of
 * the scatter/gather list.  The element is removed from the scatter/gather
 * list and the next element becomes the head.
 *
 * This function should only be called when the DMA is not active.
 */
int
ppc4xx_delete_dma_sgl_element(sgl_handle_t handle, phys_addr_t * src_dma_addr,
			      phys_addr_t * dst_dma_addr)
{
	sgl_list_info_t *psgl = (sgl_list_info_t *) handle;

	if (!handle) {
		printk("ppc4xx_delete_sgl_element: null handle\n");
		return DMA_STATUS_BAD_HANDLE;
	} else if (psgl->dmanr > (MAX_PPC4xx_DMA_CHANNELS - 1)) {
		printk("ppc4xx_delete_sgl_element: bad channel in handle %d\n",
		       psgl->dmanr);
		return DMA_STATUS_BAD_CHANNEL;
	}

	if (!psgl->phead) {
		printk("ppc4xx_delete_sgl_element: sgl list empty\n");
		*src_dma_addr = (phys_addr_t) NULL;
		*dst_dma_addr = (phys_addr_t) NULL;
		return DMA_STATUS_SGL_LIST_EMPTY;
	}

	*src_dma_addr = (phys_addr_t) psgl->phead->src_addr;
	*dst_dma_addr = (phys_addr_t) psgl->phead->dst_addr;

	if (psgl->phead == psgl->ptail) {
		/* last descriptor on the list */
		psgl->phead = NULL;
		psgl->ptail = NULL;
	} else {
		psgl->phead++;
		psgl->phead_dma += sizeof(ppc_sgl_t);
	}

	return DMA_STATUS_GOOD;
}


/*
 *   Create a scatter/gather list handle.  This is simply a structure which
 *   describes a scatter/gather list.
 *
 *   A handle is returned in "handle" which the driver should save in order to
 *   be able to access this list later.  A chunk of memory will be allocated
 *   to be used by the API for internal management purposes, including managing
 *   the sg list and allocating memory for the sgl descriptors.  One page should
 *   be more than enough for that purpose.  Perhaps it's a bit wasteful to use
 *   a whole page for a single sg list, but most likely there will be only one
 *   sg list per channel.
 *
 *   Interrupt notes:
 *   Each sgl descriptor has a copy of the DMA control word which the DMA engine
 *   loads in the control register.  The control word has a "global" interrupt
 *   enable bit for that channel. Interrupts are further qualified by a few bits
 *   in the sgl descriptor count register.  In order to setup an sgl, we have to
 *   know ahead of time whether or not interrupts will be enabled at the completion
 *   of the transfers.  Thus, enable_dma_interrupt()/disable_dma_interrupt() MUST
 *   be called before calling alloc_dma_handle().  If the interrupt mode will never
 *   change after powerup, then enable_dma_interrupt()/disable_dma_interrupt()
 *   do not have to be called -- interrupts will be enabled or disabled based
 *   on how the channel was configured after powerup by the hw_init_dma_channel()
 *   function.  Each sgl descriptor will be setup to interrupt if an error occurs;
 *   however, only the last descriptor will be setup to interrupt. Thus, an
 *   interrupt will occur (if interrupts are enabled) only after the complete
 *   sgl transfer is done.
 */
int
ppc4xx_alloc_dma_handle(sgl_handle_t * phandle, unsigned int mode, unsigned int dmanr)
{
	sgl_list_info_t *psgl=NULL;
	dma_addr_t dma_addr;
	ppc_dma_ch_t *p_dma_ch = &dma_channels[dmanr];
	uint32_t sg_command;
	uint32_t ctc_settings;
	void *ret;

	if (dmanr >= MAX_PPC4xx_DMA_CHANNELS) {
		printk("ppc4xx_alloc_dma_handle: invalid channel 0x%x\n", dmanr);
		return DMA_STATUS_BAD_CHANNEL;
	}

	if (!phandle) {
		printk("ppc4xx_alloc_dma_handle: null handle pointer\n");
		return DMA_STATUS_NULL_POINTER;
	}

	/* Get a page of memory, which is zeroed out by consistent_alloc() */
	ret = dma_alloc_coherent(NULL, DMA_PPC4xx_SIZE, &dma_addr, GFP_KERNEL);
	if (ret != NULL) {
		memset(ret, 0, DMA_PPC4xx_SIZE);
		psgl = (sgl_list_info_t *) ret;
	}

	if (psgl == NULL) {
		*phandle = (sgl_handle_t) NULL;
		return DMA_STATUS_OUT_OF_MEMORY;
	}

	psgl->dma_addr = dma_addr;
	psgl->dmanr = dmanr;

	/*
	 * Modify and save the control word. These words will be
	 * written to each sgl descriptor.  The DMA engine then
	 * loads this control word into the control register
	 * every time it reads a new descriptor.
	 */
	psgl->control = p_dma_ch->control;
	/* Clear all mode bits */
	psgl->control &= ~(DMA_TM_MASK | DMA_TD);
	/* Save control word and mode */
	psgl->control |= (mode | DMA_CE_ENABLE);

	/* In MM mode, we must set ETD/TCE */
	if (mode == DMA_MODE_MM)
		psgl->control |= DMA_ETD_OUTPUT | DMA_TCE_ENABLE;

	if (p_dma_ch->int_enable) {
		/* Enable channel interrupt */
		psgl->control |= DMA_CIE_ENABLE;
	} else {
		psgl->control &= ~DMA_CIE_ENABLE;
	}

	sg_command = mfdcr(DCRN_ASGC);
	sg_command |= SSG_MASK_ENABLE(dmanr);

	/* Enable SGL control access */
	mtdcr(DCRN_ASGC, sg_command);
	psgl->sgl_control = SG_ERI_ENABLE | SG_LINK;

	/* keep control count register settings */
	ctc_settings = mfdcr(DCRN_DMACT0 + (dmanr * 0x8))
		& (DMA_CTC_BSIZ_MSK | DMA_CTC_BTEN); /*burst mode settings*/
	psgl->sgl_control |= ctc_settings;

	if (p_dma_ch->int_enable) {
		if (p_dma_ch->tce_enable)
			psgl->sgl_control |= SG_TCI_ENABLE;
		else
			psgl->sgl_control |= SG_ETI_ENABLE;
	}

	*phandle = (sgl_handle_t) psgl;
	return DMA_STATUS_GOOD;
}

/*
 * Destroy a scatter/gather list handle that was created by alloc_dma_handle().
 * The list must be empty (contain no elements).
 */
void
ppc4xx_free_dma_handle(sgl_handle_t handle)
{
	sgl_list_info_t *psgl = (sgl_list_info_t *) handle;

	if (!handle) {
		printk("ppc4xx_free_dma_handle: got NULL\n");
		return;
	} else if (psgl->phead) {
		printk("ppc4xx_free_dma_handle: list not empty\n");
		return;
	} else if (!psgl->dma_addr) {	/* should never happen */
		printk("ppc4xx_free_dma_handle: no dma address\n");
		return;
	}

	dma_free_coherent(NULL, DMA_PPC4xx_SIZE, (void *) psgl, 0);
}

EXPORT_SYMBOL(ppc4xx_alloc_dma_handle);
EXPORT_SYMBOL(ppc4xx_free_dma_handle);
EXPORT_SYMBOL(ppc4xx_add_dma_sgl);
EXPORT_SYMBOL(ppc4xx_delete_dma_sgl_element);
EXPORT_SYMBOL(ppc4xx_enable_dma_sgl);
EXPORT_SYMBOL(ppc4xx_disable_dma_sgl);
EXPORT_SYMBOL(ppc4xx_get_dma_sgl_residue);
