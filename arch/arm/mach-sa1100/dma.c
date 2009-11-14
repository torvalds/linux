/*
 * arch/arm/mach-sa1100/dma.c
 *
 * Support functions for the SA11x0 internal DMA channels.
 *
 * Copyright (C) 2000, 2001 by Nicolas Pitre
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/errno.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <mach/hardware.h>
#include <mach/dma.h>


#undef DEBUG
#ifdef DEBUG
#define DPRINTK( s, arg... )  printk( "dma<%p>: " s, regs , ##arg )
#else
#define DPRINTK( x... )
#endif


typedef struct {
	const char *device_id;		/* device name */
	u_long device;			/* this channel device, 0  if unused*/
	dma_callback_t callback;	/* to call when DMA completes */
	void *data;			/* ... with private data ptr */
} sa1100_dma_t;

static sa1100_dma_t dma_chan[SA1100_DMA_CHANNELS];

static DEFINE_SPINLOCK(dma_list_lock);


static irqreturn_t dma_irq_handler(int irq, void *dev_id)
{
	dma_regs_t *dma_regs = dev_id;
	sa1100_dma_t *dma = dma_chan + (((u_int)dma_regs >> 5) & 7);
	int status = dma_regs->RdDCSR;

	if (status & (DCSR_ERROR)) {
		printk(KERN_CRIT "DMA on \"%s\" caused an error\n", dma->device_id);
		dma_regs->ClrDCSR = DCSR_ERROR;
	}

	dma_regs->ClrDCSR = status & (DCSR_DONEA | DCSR_DONEB);
	if (dma->callback) {
		if (status & DCSR_DONEA)
			dma->callback(dma->data);
		if (status & DCSR_DONEB)
			dma->callback(dma->data);
	}
	return IRQ_HANDLED;
}


/**
 *	sa1100_request_dma - allocate one of the SA11x0's DMA channels
 *	@device: The SA11x0 peripheral targeted by this request
 *	@device_id: An ascii name for the claiming device
 *	@callback: Function to be called when the DMA completes
 *	@data: A cookie passed back to the callback function
 *	@dma_regs: Pointer to the location of the allocated channel's identifier
 *
 * 	This function will search for a free DMA channel and returns the
 * 	address of the hardware registers for that channel as the channel
 * 	identifier. This identifier is written to the location pointed by
 * 	@dma_regs. The list of possible values for @device are listed into
 * 	arch/arm/mach-sa1100/include/mach/dma.h as a dma_device_t enum.
 *
 * 	Note that reading from a port and writing to the same port are
 * 	actually considered as two different streams requiring separate
 * 	DMA registrations.
 *
 * 	The @callback function is called from interrupt context when one
 * 	of the two possible DMA buffers in flight has terminated. That
 * 	function has to be small and efficient while posponing more complex
 * 	processing to a lower priority execution context.
 *
 * 	If no channels are available, or if the desired @device is already in
 * 	use by another DMA channel, then an error code is returned.  This
 * 	function must be called before any other DMA calls.
 **/

int sa1100_request_dma (dma_device_t device, const char *device_id,
			dma_callback_t callback, void *data,
			dma_regs_t **dma_regs)
{
	sa1100_dma_t *dma = NULL;
	dma_regs_t *regs;
	int i, err;

	*dma_regs = NULL;

	err = 0;
	spin_lock(&dma_list_lock);
	for (i = 0; i < SA1100_DMA_CHANNELS; i++) {
		if (dma_chan[i].device == device) {
			err = -EBUSY;
			break;
		} else if (!dma_chan[i].device && !dma) {
			dma = &dma_chan[i];
		}
	}
	if (!err) {
		if (dma)
			dma->device = device;
		else
			err = -ENOSR;
	}
	spin_unlock(&dma_list_lock);
	if (err)
		return err;

	i = dma - dma_chan;
	regs = (dma_regs_t *)&DDAR(i);
	err = request_irq(IRQ_DMA0 + i, dma_irq_handler, IRQF_DISABLED,
			  device_id, regs);
	if (err) {
		printk(KERN_ERR
		       "%s: unable to request IRQ %d for %s\n",
		       __func__, IRQ_DMA0 + i, device_id);
		dma->device = 0;
		return err;
	}

	*dma_regs = regs;
	dma->device_id = device_id;
	dma->callback = callback;
	dma->data = data;

	regs->ClrDCSR =
		(DCSR_DONEA | DCSR_DONEB | DCSR_STRTA | DCSR_STRTB |
		 DCSR_IE | DCSR_ERROR | DCSR_RUN);
	regs->DDAR = device;

	return 0;
}


/**
 * 	sa1100_free_dma - free a SA11x0 DMA channel
 * 	@regs: identifier for the channel to free
 *
 * 	This clears all activities on a given DMA channel and releases it
 * 	for future requests.  The @regs identifier is provided by a
 * 	successful call to sa1100_request_dma().
 **/

void sa1100_free_dma(dma_regs_t *regs)
{
	int i;

	for (i = 0; i < SA1100_DMA_CHANNELS; i++)
		if (regs == (dma_regs_t *)&DDAR(i))
			break;
	if (i >= SA1100_DMA_CHANNELS) {
		printk(KERN_ERR "%s: bad DMA identifier\n", __func__);
		return;
	}

	if (!dma_chan[i].device) {
		printk(KERN_ERR "%s: Trying to free free DMA\n", __func__);
		return;
	}

	regs->ClrDCSR =
		(DCSR_DONEA | DCSR_DONEB | DCSR_STRTA | DCSR_STRTB |
		 DCSR_IE | DCSR_ERROR | DCSR_RUN);
	free_irq(IRQ_DMA0 + i, regs);
	dma_chan[i].device = 0;
}


/**
 * 	sa1100_start_dma - submit a data buffer for DMA
 * 	@regs: identifier for the channel to use
 * 	@dma_ptr: buffer physical (or bus) start address
 * 	@size: buffer size
 *
 * 	This function hands the given data buffer to the hardware for DMA
 * 	access. If another buffer is already in flight then this buffer
 * 	will be queued so the DMA engine will switch to it automatically
 * 	when the previous one is done.  The DMA engine is actually toggling
 * 	between two buffers so at most 2 successful calls can be made before
 * 	one of them terminates and the callback function is called.
 *
 * 	The @regs identifier is provided by a successful call to
 * 	sa1100_request_dma().
 *
 * 	The @size must not be larger than %MAX_DMA_SIZE.  If a given buffer
 * 	is larger than that then it's the caller's responsibility to split
 * 	it into smaller chunks and submit them separately. If this is the
 * 	case then a @size of %CUT_DMA_SIZE is recommended to avoid ending
 * 	up with too small chunks. The callback function can be used to chain
 * 	submissions of buffer chunks.
 *
 * 	Error return values:
 * 	%-EOVERFLOW:	Given buffer size is too big.
 * 	%-EBUSY:	Both DMA buffers are already in use.
 * 	%-EAGAIN:	Both buffers were busy but one of them just completed
 * 			but the interrupt handler has to execute first.
 *
 * 	This function returs 0 on success.
 **/

int sa1100_start_dma(dma_regs_t *regs, dma_addr_t dma_ptr, u_int size)
{
	unsigned long flags;
	u_long status;
	int ret;

	if (dma_ptr & 3)
		printk(KERN_WARNING "DMA: unaligned start address (0x%08lx)\n",
		       (unsigned long)dma_ptr);

	if (size > MAX_DMA_SIZE)
		return -EOVERFLOW;

	local_irq_save(flags);
	status = regs->RdDCSR;

	/* If both DMA buffers are started, there's nothing else we can do. */
	if ((status & (DCSR_STRTA | DCSR_STRTB)) == (DCSR_STRTA | DCSR_STRTB)) {
		DPRINTK("start: st %#x busy\n", status);
		ret = -EBUSY;
		goto out;
	}

	if (((status & DCSR_BIU) && (status & DCSR_STRTB)) ||
	    (!(status & DCSR_BIU) && !(status & DCSR_STRTA))) {
		if (status & DCSR_DONEA) {
			/* give a chance for the interrupt to be processed */
			ret = -EAGAIN;
			goto out;
		}
		regs->DBSA = dma_ptr;
		regs->DBTA = size;
		regs->SetDCSR = DCSR_STRTA | DCSR_IE | DCSR_RUN;
		DPRINTK("start a=%#x s=%d on A\n", dma_ptr, size);
	} else {
		if (status & DCSR_DONEB) {
			/* give a chance for the interrupt to be processed */
			ret = -EAGAIN;
			goto out;
		}
		regs->DBSB = dma_ptr;
		regs->DBTB = size;
		regs->SetDCSR = DCSR_STRTB | DCSR_IE | DCSR_RUN;
		DPRINTK("start a=%#x s=%d on B\n", dma_ptr, size);
	}
	ret = 0;

out:
	local_irq_restore(flags);
	return ret;
}


/**
 * 	sa1100_get_dma_pos - return current DMA position
 * 	@regs: identifier for the channel to use
 *
 * 	This function returns the current physical (or bus) address for the
 * 	given DMA channel.  If the channel is running i.e. not in a stopped
 * 	state then the caller must disable interrupts prior calling this
 * 	function and process the returned value before re-enabling them to
 * 	prevent races with the completion interrupt handler and the callback
 * 	function. The validation of the returned value is the caller's
 * 	responsibility as well -- the hardware seems to return out of range
 * 	values when the DMA engine completes a buffer.
 *
 * 	The @regs identifier is provided by a successful call to
 * 	sa1100_request_dma().
 **/

dma_addr_t sa1100_get_dma_pos(dma_regs_t *regs)
{
	int status;

	/*
	 * We must determine whether buffer A or B is active.
	 * Two possibilities: either we are in the middle of
	 * a buffer, or the DMA controller just switched to the
	 * next toggle but the interrupt hasn't been serviced yet.
	 * The former case is straight forward.  In the later case,
	 * we'll do like if DMA is just at the end of the previous
	 * toggle since all registers haven't been reset yet.
	 * This goes around the edge case and since we're always
	 * a little behind anyways it shouldn't make a big difference.
	 * If DMA has been stopped prior calling this then the
	 * position is exact.
	 */
	status = regs->RdDCSR;
	if ((!(status & DCSR_BIU) &&  (status & DCSR_STRTA)) ||
	    ( (status & DCSR_BIU) && !(status & DCSR_STRTB)))
		return regs->DBSA;
	else
		return regs->DBSB;
}


/**
 * 	sa1100_reset_dma - reset a DMA channel
 * 	@regs: identifier for the channel to use
 *
 * 	This function resets and reconfigure the given DMA channel. This is
 * 	particularly useful after a sleep/wakeup event.
 *
 * 	The @regs identifier is provided by a successful call to
 * 	sa1100_request_dma().
 **/

void sa1100_reset_dma(dma_regs_t *regs)
{
	int i;

	for (i = 0; i < SA1100_DMA_CHANNELS; i++)
		if (regs == (dma_regs_t *)&DDAR(i))
			break;
	if (i >= SA1100_DMA_CHANNELS) {
		printk(KERN_ERR "%s: bad DMA identifier\n", __func__);
		return;
	}

	regs->ClrDCSR =
		(DCSR_DONEA | DCSR_DONEB | DCSR_STRTA | DCSR_STRTB |
		 DCSR_IE | DCSR_ERROR | DCSR_RUN);
	regs->DDAR = dma_chan[i].device;
}


EXPORT_SYMBOL(sa1100_request_dma);
EXPORT_SYMBOL(sa1100_free_dma);
EXPORT_SYMBOL(sa1100_start_dma);
EXPORT_SYMBOL(sa1100_get_dma_pos);
EXPORT_SYMBOL(sa1100_reset_dma);

