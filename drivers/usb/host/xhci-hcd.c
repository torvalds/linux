/*
 * xHCI host controller driver
 *
 * Copyright (C) 2008 Intel Corp.
 *
 * Author: Sarah Sharp
 * Some code borrowed from the Linux EHCI driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/irq.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include "xhci.h"

#define DRIVER_AUTHOR "Sarah Sharp"
#define DRIVER_DESC "'eXtensible' Host Controller (xHC) Driver"

/* Some 0.95 hardware can't handle the chain bit on a Link TRB being cleared */
static int link_quirk;
module_param(link_quirk, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(link_quirk, "Don't clear the chain bit on a link TRB");

/* TODO: copied from ehci-hcd.c - can this be refactored? */
/*
 * handshake - spin reading hc until handshake completes or fails
 * @ptr: address of hc register to be read
 * @mask: bits to look at in result of read
 * @done: value of those bits when handshake succeeds
 * @usec: timeout in microseconds
 *
 * Returns negative errno, or zero on success
 *
 * Success happens when the "mask" bits have the specified value (hardware
 * handshake done).  There are two failure modes:  "usec" have passed (major
 * hardware flakeout), or the register reads as all-ones (hardware removed).
 */
static int handshake(struct xhci_hcd *xhci, void __iomem *ptr,
		      u32 mask, u32 done, int usec)
{
	u32	result;

	do {
		result = xhci_readl(xhci, ptr);
		if (result == ~(u32)0)		/* card removed */
			return -ENODEV;
		result &= mask;
		if (result == done)
			return 0;
		udelay(1);
		usec--;
	} while (usec > 0);
	return -ETIMEDOUT;
}

/*
 * Force HC into halt state.
 *
 * Disable any IRQs and clear the run/stop bit.
 * HC will complete any current and actively pipelined transactions, and
 * should halt within 16 microframes of the run/stop bit being cleared.
 * Read HC Halted bit in the status register to see when the HC is finished.
 * XXX: shouldn't we set HC_STATE_HALT here somewhere?
 */
int xhci_halt(struct xhci_hcd *xhci)
{
	u32 halted;
	u32 cmd;
	u32 mask;

	xhci_dbg(xhci, "// Halt the HC\n");
	/* Disable all interrupts from the host controller */
	mask = ~(XHCI_IRQS);
	halted = xhci_readl(xhci, &xhci->op_regs->status) & STS_HALT;
	if (!halted)
		mask &= ~CMD_RUN;

	cmd = xhci_readl(xhci, &xhci->op_regs->command);
	cmd &= mask;
	xhci_writel(xhci, cmd, &xhci->op_regs->command);

	return handshake(xhci, &xhci->op_regs->status,
			STS_HALT, STS_HALT, XHCI_MAX_HALT_USEC);
}

/*
 * Reset a halted HC, and set the internal HC state to HC_STATE_HALT.
 *
 * This resets pipelines, timers, counters, state machines, etc.
 * Transactions will be terminated immediately, and operational registers
 * will be set to their defaults.
 */
int xhci_reset(struct xhci_hcd *xhci)
{
	u32 command;
	u32 state;

	state = xhci_readl(xhci, &xhci->op_regs->status);
	if ((state & STS_HALT) == 0) {
		xhci_warn(xhci, "Host controller not halted, aborting reset.\n");
		return 0;
	}

	xhci_dbg(xhci, "// Reset the HC\n");
	command = xhci_readl(xhci, &xhci->op_regs->command);
	command |= CMD_RESET;
	xhci_writel(xhci, command, &xhci->op_regs->command);
	/* XXX: Why does EHCI set this here?  Shouldn't other code do this? */
	xhci_to_hcd(xhci)->state = HC_STATE_HALT;

	return handshake(xhci, &xhci->op_regs->command, CMD_RESET, 0, 250 * 1000);
}

/*
 * Stop the HC from processing the endpoint queues.
 */
static void xhci_quiesce(struct xhci_hcd *xhci)
{
	/*
	 * Queues are per endpoint, so we need to disable an endpoint or slot.
	 *
	 * To disable a slot, we need to insert a disable slot command on the
	 * command ring and ring the doorbell.  This will also free any internal
	 * resources associated with the slot (which might not be what we want).
	 *
	 * A Release Endpoint command sounds better - doesn't free internal HC
	 * memory, but removes the endpoints from the schedule and releases the
	 * bandwidth, disables the doorbells, and clears the endpoint enable
	 * flag.  Usually used prior to a set interface command.
	 *
	 * TODO: Implement after command ring code is done.
	 */
	BUG_ON(!HC_IS_RUNNING(xhci_to_hcd(xhci)->state));
	xhci_dbg(xhci, "Finished quiescing -- code not written yet\n");
}

#if 0
/* Set up MSI-X table for entry 0 (may claim other entries later) */
static int xhci_setup_msix(struct xhci_hcd *xhci)
{
	int ret;
	struct pci_dev *pdev = to_pci_dev(xhci_to_hcd(xhci)->self.controller);

	xhci->msix_count = 0;
	/* XXX: did I do this right?  ixgbe does kcalloc for more than one */
	xhci->msix_entries = kmalloc(sizeof(struct msix_entry), GFP_KERNEL);
	if (!xhci->msix_entries) {
		xhci_err(xhci, "Failed to allocate MSI-X entries\n");
		return -ENOMEM;
	}
	xhci->msix_entries[0].entry = 0;

	ret = pci_enable_msix(pdev, xhci->msix_entries, xhci->msix_count);
	if (ret) {
		xhci_err(xhci, "Failed to enable MSI-X\n");
		goto free_entries;
	}

	/*
	 * Pass the xhci pointer value as the request_irq "cookie".
	 * If more irqs are added, this will need to be unique for each one.
	 */
	ret = request_irq(xhci->msix_entries[0].vector, &xhci_irq, 0,
			"xHCI", xhci_to_hcd(xhci));
	if (ret) {
		xhci_err(xhci, "Failed to allocate MSI-X interrupt\n");
		goto disable_msix;
	}
	xhci_dbg(xhci, "Finished setting up MSI-X\n");
	return 0;

disable_msix:
	pci_disable_msix(pdev);
free_entries:
	kfree(xhci->msix_entries);
	xhci->msix_entries = NULL;
	return ret;
}

/* XXX: code duplication; can xhci_setup_msix call this? */
/* Free any IRQs and disable MSI-X */
static void xhci_cleanup_msix(struct xhci_hcd *xhci)
{
	struct pci_dev *pdev = to_pci_dev(xhci_to_hcd(xhci)->self.controller);
	if (!xhci->msix_entries)
		return;

	free_irq(xhci->msix_entries[0].vector, xhci);
	pci_disable_msix(pdev);
	kfree(xhci->msix_entries);
	xhci->msix_entries = NULL;
	xhci_dbg(xhci, "Finished cleaning up MSI-X\n");
}
#endif

/*
 * Initialize memory for HCD and xHC (one-time init).
 *
 * Program the PAGESIZE register, initialize the device context array, create
 * device contexts (?), set up a command ring segment (or two?), create event
 * ring (one for now).
 */
int xhci_init(struct usb_hcd *hcd)
{
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	int retval = 0;

	xhci_dbg(xhci, "xhci_init\n");
	spin_lock_init(&xhci->lock);
	if (link_quirk) {
		xhci_dbg(xhci, "QUIRK: Not clearing Link TRB chain bits.\n");
		xhci->quirks |= XHCI_LINK_TRB_QUIRK;
	} else {
		xhci_dbg(xhci, "xHCI doesn't need link TRB QUIRK\n");
	}
	retval = xhci_mem_init(xhci, GFP_KERNEL);
	xhci_dbg(xhci, "Finished xhci_init\n");

	return retval;
}

/*
 * Called in interrupt context when there might be work
 * queued on the event ring
 *
 * xhci->lock must be held by caller.
 */
static void xhci_work(struct xhci_hcd *xhci)
{
	u32 temp;
	u64 temp_64;

	/*
	 * Clear the op reg interrupt status first,
	 * so we can receive interrupts from other MSI-X interrupters.
	 * Write 1 to clear the interrupt status.
	 */
	temp = xhci_readl(xhci, &xhci->op_regs->status);
	temp |= STS_EINT;
	xhci_writel(xhci, temp, &xhci->op_regs->status);
	/* FIXME when MSI-X is supported and there are multiple vectors */
	/* Clear the MSI-X event interrupt status */

	/* Acknowledge the interrupt */
	temp = xhci_readl(xhci, &xhci->ir_set->irq_pending);
	temp |= 0x3;
	xhci_writel(xhci, temp, &xhci->ir_set->irq_pending);
	/* Flush posted writes */
	xhci_readl(xhci, &xhci->ir_set->irq_pending);

	/* FIXME this should be a delayed service routine that clears the EHB */
	xhci_handle_event(xhci);

	/* Clear the event handler busy flag (RW1C); the event ring should be empty. */
	temp_64 = xhci_read_64(xhci, &xhci->ir_set->erst_dequeue);
	xhci_write_64(xhci, temp_64 | ERST_EHB, &xhci->ir_set->erst_dequeue);
	/* Flush posted writes -- FIXME is this necessary? */
	xhci_readl(xhci, &xhci->ir_set->irq_pending);
}

/*-------------------------------------------------------------------------*/

/*
 * xHCI spec says we can get an interrupt, and if the HC has an error condition,
 * we might get bad data out of the event ring.  Section 4.10.2.7 has a list of
 * indicators of an event TRB error, but we check the status *first* to be safe.
 */
irqreturn_t xhci_irq(struct usb_hcd *hcd)
{
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	u32 temp, temp2;
	union xhci_trb *trb;

	spin_lock(&xhci->lock);
	trb = xhci->event_ring->dequeue;
	/* Check if the xHC generated the interrupt, or the irq is shared */
	temp = xhci_readl(xhci, &xhci->op_regs->status);
	temp2 = xhci_readl(xhci, &xhci->ir_set->irq_pending);
	if (temp == 0xffffffff && temp2 == 0xffffffff)
		goto hw_died;

	if (!(temp & STS_EINT) && !ER_IRQ_PENDING(temp2)) {
		spin_unlock(&xhci->lock);
		return IRQ_NONE;
	}
	xhci_dbg(xhci, "op reg status = %08x\n", temp);
	xhci_dbg(xhci, "ir set irq_pending = %08x\n", temp2);
	xhci_dbg(xhci, "Event ring dequeue ptr:\n");
	xhci_dbg(xhci, "@%llx %08x %08x %08x %08x\n",
			(unsigned long long)xhci_trb_virt_to_dma(xhci->event_ring->deq_seg, trb),
			lower_32_bits(trb->link.segment_ptr),
			upper_32_bits(trb->link.segment_ptr),
			(unsigned int) trb->link.intr_target,
			(unsigned int) trb->link.control);

	if (temp & STS_FATAL) {
		xhci_warn(xhci, "WARNING: Host System Error\n");
		xhci_halt(xhci);
hw_died:
		xhci_to_hcd(xhci)->state = HC_STATE_HALT;
		spin_unlock(&xhci->lock);
		return -ESHUTDOWN;
	}

	xhci_work(xhci);
	spin_unlock(&xhci->lock);

	return IRQ_HANDLED;
}

#ifdef CONFIG_USB_XHCI_HCD_DEBUGGING
void xhci_event_ring_work(unsigned long arg)
{
	unsigned long flags;
	int temp;
	u64 temp_64;
	struct xhci_hcd *xhci = (struct xhci_hcd *) arg;
	int i, j;

	xhci_dbg(xhci, "Poll event ring: %lu\n", jiffies);

	spin_lock_irqsave(&xhci->lock, flags);
	temp = xhci_readl(xhci, &xhci->op_regs->status);
	xhci_dbg(xhci, "op reg status = 0x%x\n", temp);
	if (temp == 0xffffffff) {
		xhci_dbg(xhci, "HW died, polling stopped.\n");
		spin_unlock_irqrestore(&xhci->lock, flags);
		return;
	}

	temp = xhci_readl(xhci, &xhci->ir_set->irq_pending);
	xhci_dbg(xhci, "ir_set 0 pending = 0x%x\n", temp);
	xhci_dbg(xhci, "No-op commands handled = %d\n", xhci->noops_handled);
	xhci_dbg(xhci, "HC error bitmask = 0x%x\n", xhci->error_bitmask);
	xhci->error_bitmask = 0;
	xhci_dbg(xhci, "Event ring:\n");
	xhci_debug_segment(xhci, xhci->event_ring->deq_seg);
	xhci_dbg_ring_ptrs(xhci, xhci->event_ring);
	temp_64 = xhci_read_64(xhci, &xhci->ir_set->erst_dequeue);
	temp_64 &= ~ERST_PTR_MASK;
	xhci_dbg(xhci, "ERST deq = 64'h%0lx\n", (long unsigned int) temp_64);
	xhci_dbg(xhci, "Command ring:\n");
	xhci_debug_segment(xhci, xhci->cmd_ring->deq_seg);
	xhci_dbg_ring_ptrs(xhci, xhci->cmd_ring);
	xhci_dbg_cmd_ptrs(xhci);
	for (i = 0; i < MAX_HC_SLOTS; ++i) {
		if (!xhci->devs[i])
			continue;
		for (j = 0; j < 31; ++j) {
			struct xhci_ring *ring = xhci->devs[i]->eps[j].ring;
			if (!ring)
				continue;
			xhci_dbg(xhci, "Dev %d endpoint ring %d:\n", i, j);
			xhci_debug_segment(xhci, ring->deq_seg);
		}
	}

	if (xhci->noops_submitted != NUM_TEST_NOOPS)
		if (xhci_setup_one_noop(xhci))
			xhci_ring_cmd_db(xhci);
	spin_unlock_irqrestore(&xhci->lock, flags);

	if (!xhci->zombie)
		mod_timer(&xhci->event_ring_timer, jiffies + POLL_TIMEOUT * HZ);
	else
		xhci_dbg(xhci, "Quit polling the event ring.\n");
}
#endif

/*
 * Start the HC after it was halted.
 *
 * This function is called by the USB core when the HC driver is added.
 * Its opposite is xhci_stop().
 *
 * xhci_init() must be called once before this function can be called.
 * Reset the HC, enable device slot contexts, program DCBAAP, and
 * set command ring pointer and event ring pointer.
 *
 * Setup MSI-X vectors and enable interrupts.
 */
int xhci_run(struct usb_hcd *hcd)
{
	u32 temp;
	u64 temp_64;
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	void (*doorbell)(struct xhci_hcd *) = NULL;

	hcd->uses_new_polling = 1;
	hcd->poll_rh = 0;

	xhci_dbg(xhci, "xhci_run\n");
#if 0	/* FIXME: MSI not setup yet */
	/* Do this at the very last minute */
	ret = xhci_setup_msix(xhci);
	if (!ret)
		return ret;

	return -ENOSYS;
#endif
#ifdef CONFIG_USB_XHCI_HCD_DEBUGGING
	init_timer(&xhci->event_ring_timer);
	xhci->event_ring_timer.data = (unsigned long) xhci;
	xhci->event_ring_timer.function = xhci_event_ring_work;
	/* Poll the event ring */
	xhci->event_ring_timer.expires = jiffies + POLL_TIMEOUT * HZ;
	xhci->zombie = 0;
	xhci_dbg(xhci, "Setting event ring polling timer\n");
	add_timer(&xhci->event_ring_timer);
#endif

	xhci_dbg(xhci, "Command ring memory map follows:\n");
	xhci_debug_ring(xhci, xhci->cmd_ring);
	xhci_dbg_ring_ptrs(xhci, xhci->cmd_ring);
	xhci_dbg_cmd_ptrs(xhci);

	xhci_dbg(xhci, "ERST memory map follows:\n");
	xhci_dbg_erst(xhci, &xhci->erst);
	xhci_dbg(xhci, "Event ring:\n");
	xhci_debug_ring(xhci, xhci->event_ring);
	xhci_dbg_ring_ptrs(xhci, xhci->event_ring);
	temp_64 = xhci_read_64(xhci, &xhci->ir_set->erst_dequeue);
	temp_64 &= ~ERST_PTR_MASK;
	xhci_dbg(xhci, "ERST deq = 64'h%0lx\n", (long unsigned int) temp_64);

	xhci_dbg(xhci, "// Set the interrupt modulation register\n");
	temp = xhci_readl(xhci, &xhci->ir_set->irq_control);
	temp &= ~ER_IRQ_INTERVAL_MASK;
	temp |= (u32) 160;
	xhci_writel(xhci, temp, &xhci->ir_set->irq_control);

	/* Set the HCD state before we enable the irqs */
	hcd->state = HC_STATE_RUNNING;
	temp = xhci_readl(xhci, &xhci->op_regs->command);
	temp |= (CMD_EIE);
	xhci_dbg(xhci, "// Enable interrupts, cmd = 0x%x.\n",
			temp);
	xhci_writel(xhci, temp, &xhci->op_regs->command);

	temp = xhci_readl(xhci, &xhci->ir_set->irq_pending);
	xhci_dbg(xhci, "// Enabling event ring interrupter %p by writing 0x%x to irq_pending\n",
			xhci->ir_set, (unsigned int) ER_IRQ_ENABLE(temp));
	xhci_writel(xhci, ER_IRQ_ENABLE(temp),
			&xhci->ir_set->irq_pending);
	xhci_print_ir_set(xhci, xhci->ir_set, 0);

	if (NUM_TEST_NOOPS > 0)
		doorbell = xhci_setup_one_noop(xhci);

	temp = xhci_readl(xhci, &xhci->op_regs->command);
	temp |= (CMD_RUN);
	xhci_dbg(xhci, "// Turn on HC, cmd = 0x%x.\n",
			temp);
	xhci_writel(xhci, temp, &xhci->op_regs->command);
	/* Flush PCI posted writes */
	temp = xhci_readl(xhci, &xhci->op_regs->command);
	xhci_dbg(xhci, "// @%p = 0x%x\n", &xhci->op_regs->command, temp);
	if (doorbell)
		(*doorbell)(xhci);

	xhci_dbg(xhci, "Finished xhci_run\n");
	return 0;
}

/*
 * Stop xHCI driver.
 *
 * This function is called by the USB core when the HC driver is removed.
 * Its opposite is xhci_run().
 *
 * Disable device contexts, disable IRQs, and quiesce the HC.
 * Reset the HC, finish any completed transactions, and cleanup memory.
 */
void xhci_stop(struct usb_hcd *hcd)
{
	u32 temp;
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);

	spin_lock_irq(&xhci->lock);
	if (HC_IS_RUNNING(hcd->state))
		xhci_quiesce(xhci);
	xhci_halt(xhci);
	xhci_reset(xhci);
	spin_unlock_irq(&xhci->lock);

#if 0	/* No MSI yet */
	xhci_cleanup_msix(xhci);
#endif
#ifdef CONFIG_USB_XHCI_HCD_DEBUGGING
	/* Tell the event ring poll function not to reschedule */
	xhci->zombie = 1;
	del_timer_sync(&xhci->event_ring_timer);
#endif

	xhci_dbg(xhci, "// Disabling event ring interrupts\n");
	temp = xhci_readl(xhci, &xhci->op_regs->status);
	xhci_writel(xhci, temp & ~STS_EINT, &xhci->op_regs->status);
	temp = xhci_readl(xhci, &xhci->ir_set->irq_pending);
	xhci_writel(xhci, ER_IRQ_DISABLE(temp),
			&xhci->ir_set->irq_pending);
	xhci_print_ir_set(xhci, xhci->ir_set, 0);

	xhci_dbg(xhci, "cleaning up memory\n");
	xhci_mem_cleanup(xhci);
	xhci_dbg(xhci, "xhci_stop completed - status = %x\n",
		    xhci_readl(xhci, &xhci->op_regs->status));
}

/*
 * Shutdown HC (not bus-specific)
 *
 * This is called when the machine is rebooting or halting.  We assume that the
 * machine will be powered off, and the HC's internal state will be reset.
 * Don't bother to free memory.
 */
void xhci_shutdown(struct usb_hcd *hcd)
{
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);

	spin_lock_irq(&xhci->lock);
	xhci_halt(xhci);
	spin_unlock_irq(&xhci->lock);

#if 0
	xhci_cleanup_msix(xhci);
#endif

	xhci_dbg(xhci, "xhci_shutdown completed - status = %x\n",
		    xhci_readl(xhci, &xhci->op_regs->status));
}

/*-------------------------------------------------------------------------*/

/**
 * xhci_get_endpoint_index - Used for passing endpoint bitmasks between the core and
 * HCDs.  Find the index for an endpoint given its descriptor.  Use the return
 * value to right shift 1 for the bitmask.
 *
 * Index  = (epnum * 2) + direction - 1,
 * where direction = 0 for OUT, 1 for IN.
 * For control endpoints, the IN index is used (OUT index is unused), so
 * index = (epnum * 2) + direction - 1 = (epnum * 2) + 1 - 1 = (epnum * 2)
 */
unsigned int xhci_get_endpoint_index(struct usb_endpoint_descriptor *desc)
{
	unsigned int index;
	if (usb_endpoint_xfer_control(desc))
		index = (unsigned int) (usb_endpoint_num(desc)*2);
	else
		index = (unsigned int) (usb_endpoint_num(desc)*2) +
			(usb_endpoint_dir_in(desc) ? 1 : 0) - 1;
	return index;
}

/* Find the flag for this endpoint (for use in the control context).  Use the
 * endpoint index to create a bitmask.  The slot context is bit 0, endpoint 0 is
 * bit 1, etc.
 */
unsigned int xhci_get_endpoint_flag(struct usb_endpoint_descriptor *desc)
{
	return 1 << (xhci_get_endpoint_index(desc) + 1);
}

/* Find the flag for this endpoint (for use in the control context).  Use the
 * endpoint index to create a bitmask.  The slot context is bit 0, endpoint 0 is
 * bit 1, etc.
 */
unsigned int xhci_get_endpoint_flag_from_index(unsigned int ep_index)
{
	return 1 << (ep_index + 1);
}

/* Compute the last valid endpoint context index.  Basically, this is the
 * endpoint index plus one.  For slot contexts with more than valid endpoint,
 * we find the most significant bit set in the added contexts flags.
 * e.g. ep 1 IN (with epnum 0x81) => added_ctxs = 0b1000
 * fls(0b1000) = 4, but the endpoint context index is 3, so subtract one.
 */
unsigned int xhci_last_valid_endpoint(u32 added_ctxs)
{
	return fls(added_ctxs) - 1;
}

/* Returns 1 if the arguments are OK;
 * returns 0 this is a root hub; returns -EINVAL for NULL pointers.
 */
int xhci_check_args(struct usb_hcd *hcd, struct usb_device *udev,
		struct usb_host_endpoint *ep, int check_ep, const char *func) {
	if (!hcd || (check_ep && !ep) || !udev) {
		printk(KERN_DEBUG "xHCI %s called with invalid args\n",
				func);
		return -EINVAL;
	}
	if (!udev->parent) {
		printk(KERN_DEBUG "xHCI %s called for root hub\n",
				func);
		return 0;
	}
	if (!udev->slot_id) {
		printk(KERN_DEBUG "xHCI %s called with unaddressed device\n",
				func);
		return -EINVAL;
	}
	return 1;
}

static int xhci_configure_endpoint(struct xhci_hcd *xhci,
		struct usb_device *udev, struct xhci_command *command,
		bool ctx_change, bool must_succeed);

/*
 * Full speed devices may have a max packet size greater than 8 bytes, but the
 * USB core doesn't know that until it reads the first 8 bytes of the
 * descriptor.  If the usb_device's max packet size changes after that point,
 * we need to issue an evaluate context command and wait on it.
 */
static int xhci_check_maxpacket(struct xhci_hcd *xhci, unsigned int slot_id,
		unsigned int ep_index, struct urb *urb)
{
	struct xhci_container_ctx *in_ctx;
	struct xhci_container_ctx *out_ctx;
	struct xhci_input_control_ctx *ctrl_ctx;
	struct xhci_ep_ctx *ep_ctx;
	int max_packet_size;
	int hw_max_packet_size;
	int ret = 0;

	out_ctx = xhci->devs[slot_id]->out_ctx;
	ep_ctx = xhci_get_ep_ctx(xhci, out_ctx, ep_index);
	hw_max_packet_size = MAX_PACKET_DECODED(ep_ctx->ep_info2);
	max_packet_size = urb->dev->ep0.desc.wMaxPacketSize;
	if (hw_max_packet_size != max_packet_size) {
		xhci_dbg(xhci, "Max Packet Size for ep 0 changed.\n");
		xhci_dbg(xhci, "Max packet size in usb_device = %d\n",
				max_packet_size);
		xhci_dbg(xhci, "Max packet size in xHCI HW = %d\n",
				hw_max_packet_size);
		xhci_dbg(xhci, "Issuing evaluate context command.\n");

		/* Set up the modified control endpoint 0 */
		xhci_endpoint_copy(xhci, xhci->devs[slot_id]->in_ctx,
				xhci->devs[slot_id]->out_ctx, ep_index);
		in_ctx = xhci->devs[slot_id]->in_ctx;
		ep_ctx = xhci_get_ep_ctx(xhci, in_ctx, ep_index);
		ep_ctx->ep_info2 &= ~MAX_PACKET_MASK;
		ep_ctx->ep_info2 |= MAX_PACKET(max_packet_size);

		/* Set up the input context flags for the command */
		/* FIXME: This won't work if a non-default control endpoint
		 * changes max packet sizes.
		 */
		ctrl_ctx = xhci_get_input_control_ctx(xhci, in_ctx);
		ctrl_ctx->add_flags = EP0_FLAG;
		ctrl_ctx->drop_flags = 0;

		xhci_dbg(xhci, "Slot %d input context\n", slot_id);
		xhci_dbg_ctx(xhci, in_ctx, ep_index);
		xhci_dbg(xhci, "Slot %d output context\n", slot_id);
		xhci_dbg_ctx(xhci, out_ctx, ep_index);

		ret = xhci_configure_endpoint(xhci, urb->dev, NULL,
				true, false);

		/* Clean up the input context for later use by bandwidth
		 * functions.
		 */
		ctrl_ctx->add_flags = SLOT_FLAG;
	}
	return ret;
}

/*
 * non-error returns are a promise to giveback() the urb later
 * we drop ownership so next owner (or urb unlink) can get it
 */
int xhci_urb_enqueue(struct usb_hcd *hcd, struct urb *urb, gfp_t mem_flags)
{
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	unsigned long flags;
	int ret = 0;
	unsigned int slot_id, ep_index;


	if (!urb || xhci_check_args(hcd, urb->dev, urb->ep, true, __func__) <= 0)
		return -EINVAL;

	slot_id = urb->dev->slot_id;
	ep_index = xhci_get_endpoint_index(&urb->ep->desc);

	if (!xhci->devs || !xhci->devs[slot_id]) {
		if (!in_interrupt())
			dev_warn(&urb->dev->dev, "WARN: urb submitted for dev with no Slot ID\n");
		ret = -EINVAL;
		goto exit;
	}
	if (!test_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags)) {
		if (!in_interrupt())
			xhci_dbg(xhci, "urb submitted during PCI suspend\n");
		ret = -ESHUTDOWN;
		goto exit;
	}
	if (usb_endpoint_xfer_control(&urb->ep->desc)) {
		/* Check to see if the max packet size for the default control
		 * endpoint changed during FS device enumeration
		 */
		if (urb->dev->speed == USB_SPEED_FULL) {
			ret = xhci_check_maxpacket(xhci, slot_id,
					ep_index, urb);
			if (ret < 0)
				return ret;
		}

		/* We have a spinlock and interrupts disabled, so we must pass
		 * atomic context to this function, which may allocate memory.
		 */
		spin_lock_irqsave(&xhci->lock, flags);
		ret = xhci_queue_ctrl_tx(xhci, GFP_ATOMIC, urb,
				slot_id, ep_index);
		spin_unlock_irqrestore(&xhci->lock, flags);
	} else if (usb_endpoint_xfer_bulk(&urb->ep->desc)) {
		spin_lock_irqsave(&xhci->lock, flags);
		ret = xhci_queue_bulk_tx(xhci, GFP_ATOMIC, urb,
				slot_id, ep_index);
		spin_unlock_irqrestore(&xhci->lock, flags);
	} else if (usb_endpoint_xfer_int(&urb->ep->desc)) {
		spin_lock_irqsave(&xhci->lock, flags);
		ret = xhci_queue_intr_tx(xhci, GFP_ATOMIC, urb,
				slot_id, ep_index);
		spin_unlock_irqrestore(&xhci->lock, flags);
	} else {
		ret = -EINVAL;
	}
exit:
	return ret;
}

/*
 * Remove the URB's TD from the endpoint ring.  This may cause the HC to stop
 * USB transfers, potentially stopping in the middle of a TRB buffer.  The HC
 * should pick up where it left off in the TD, unless a Set Transfer Ring
 * Dequeue Pointer is issued.
 *
 * The TRBs that make up the buffers for the canceled URB will be "removed" from
 * the ring.  Since the ring is a contiguous structure, they can't be physically
 * removed.  Instead, there are two options:
 *
 *  1) If the HC is in the middle of processing the URB to be canceled, we
 *     simply move the ring's dequeue pointer past those TRBs using the Set
 *     Transfer Ring Dequeue Pointer command.  This will be the common case,
 *     when drivers timeout on the last submitted URB and attempt to cancel.
 *
 *  2) If the HC is in the middle of a different TD, we turn the TRBs into a
 *     series of 1-TRB transfer no-op TDs.  (No-ops shouldn't be chained.)  The
 *     HC will need to invalidate the any TRBs it has cached after the stop
 *     endpoint command, as noted in the xHCI 0.95 errata.
 *
 *  3) The TD may have completed by the time the Stop Endpoint Command
 *     completes, so software needs to handle that case too.
 *
 * This function should protect against the TD enqueueing code ringing the
 * doorbell while this code is waiting for a Stop Endpoint command to complete.
 * It also needs to account for multiple cancellations on happening at the same
 * time for the same endpoint.
 *
 * Note that this function can be called in any context, or so says
 * usb_hcd_unlink_urb()
 */
int xhci_urb_dequeue(struct usb_hcd *hcd, struct urb *urb, int status)
{
	unsigned long flags;
	int ret;
	u32 temp;
	struct xhci_hcd *xhci;
	struct xhci_td *td;
	unsigned int ep_index;
	struct xhci_ring *ep_ring;
	struct xhci_virt_ep *ep;

	xhci = hcd_to_xhci(hcd);
	spin_lock_irqsave(&xhci->lock, flags);
	/* Make sure the URB hasn't completed or been unlinked already */
	ret = usb_hcd_check_unlink_urb(hcd, urb, status);
	if (ret || !urb->hcpriv)
		goto done;
	temp = xhci_readl(xhci, &xhci->op_regs->status);
	if (temp == 0xffffffff) {
		xhci_dbg(xhci, "HW died, freeing TD.\n");
		td = (struct xhci_td *) urb->hcpriv;

		usb_hcd_unlink_urb_from_ep(hcd, urb);
		spin_unlock_irqrestore(&xhci->lock, flags);
		usb_hcd_giveback_urb(xhci_to_hcd(xhci), urb, -ESHUTDOWN);
		kfree(td);
		return ret;
	}

	xhci_dbg(xhci, "Cancel URB %p\n", urb);
	xhci_dbg(xhci, "Event ring:\n");
	xhci_debug_ring(xhci, xhci->event_ring);
	ep_index = xhci_get_endpoint_index(&urb->ep->desc);
	ep = &xhci->devs[urb->dev->slot_id]->eps[ep_index];
	ep_ring = ep->ring;
	xhci_dbg(xhci, "Endpoint ring:\n");
	xhci_debug_ring(xhci, ep_ring);
	td = (struct xhci_td *) urb->hcpriv;

	ep->cancels_pending++;
	list_add_tail(&td->cancelled_td_list, &ep->cancelled_td_list);
	/* Queue a stop endpoint command, but only if this is
	 * the first cancellation to be handled.
	 */
	if (ep->cancels_pending == 1) {
		xhci_queue_stop_endpoint(xhci, urb->dev->slot_id, ep_index);
		xhci_ring_cmd_db(xhci);
	}
done:
	spin_unlock_irqrestore(&xhci->lock, flags);
	return ret;
}

/* Drop an endpoint from a new bandwidth configuration for this device.
 * Only one call to this function is allowed per endpoint before
 * check_bandwidth() or reset_bandwidth() must be called.
 * A call to xhci_drop_endpoint() followed by a call to xhci_add_endpoint() will
 * add the endpoint to the schedule with possibly new parameters denoted by a
 * different endpoint descriptor in usb_host_endpoint.
 * A call to xhci_add_endpoint() followed by a call to xhci_drop_endpoint() is
 * not allowed.
 *
 * The USB core will not allow URBs to be queued to an endpoint that is being
 * disabled, so there's no need for mutual exclusion to protect
 * the xhci->devs[slot_id] structure.
 */
int xhci_drop_endpoint(struct usb_hcd *hcd, struct usb_device *udev,
		struct usb_host_endpoint *ep)
{
	struct xhci_hcd *xhci;
	struct xhci_container_ctx *in_ctx, *out_ctx;
	struct xhci_input_control_ctx *ctrl_ctx;
	struct xhci_slot_ctx *slot_ctx;
	unsigned int last_ctx;
	unsigned int ep_index;
	struct xhci_ep_ctx *ep_ctx;
	u32 drop_flag;
	u32 new_add_flags, new_drop_flags, new_slot_info;
	int ret;

	ret = xhci_check_args(hcd, udev, ep, 1, __func__);
	if (ret <= 0)
		return ret;
	xhci = hcd_to_xhci(hcd);
	xhci_dbg(xhci, "%s called for udev %p\n", __func__, udev);

	drop_flag = xhci_get_endpoint_flag(&ep->desc);
	if (drop_flag == SLOT_FLAG || drop_flag == EP0_FLAG) {
		xhci_dbg(xhci, "xHCI %s - can't drop slot or ep 0 %#x\n",
				__func__, drop_flag);
		return 0;
	}

	if (!xhci->devs || !xhci->devs[udev->slot_id]) {
		xhci_warn(xhci, "xHCI %s called with unaddressed device\n",
				__func__);
		return -EINVAL;
	}

	in_ctx = xhci->devs[udev->slot_id]->in_ctx;
	out_ctx = xhci->devs[udev->slot_id]->out_ctx;
	ctrl_ctx = xhci_get_input_control_ctx(xhci, in_ctx);
	ep_index = xhci_get_endpoint_index(&ep->desc);
	ep_ctx = xhci_get_ep_ctx(xhci, out_ctx, ep_index);
	/* If the HC already knows the endpoint is disabled,
	 * or the HCD has noted it is disabled, ignore this request
	 */
	if ((ep_ctx->ep_info & EP_STATE_MASK) == EP_STATE_DISABLED ||
			ctrl_ctx->drop_flags & xhci_get_endpoint_flag(&ep->desc)) {
		xhci_warn(xhci, "xHCI %s called with disabled ep %p\n",
				__func__, ep);
		return 0;
	}

	ctrl_ctx->drop_flags |= drop_flag;
	new_drop_flags = ctrl_ctx->drop_flags;

	ctrl_ctx->add_flags &= ~drop_flag;
	new_add_flags = ctrl_ctx->add_flags;

	last_ctx = xhci_last_valid_endpoint(ctrl_ctx->add_flags);
	slot_ctx = xhci_get_slot_ctx(xhci, in_ctx);
	/* Update the last valid endpoint context, if we deleted the last one */
	if ((slot_ctx->dev_info & LAST_CTX_MASK) > LAST_CTX(last_ctx)) {
		slot_ctx->dev_info &= ~LAST_CTX_MASK;
		slot_ctx->dev_info |= LAST_CTX(last_ctx);
	}
	new_slot_info = slot_ctx->dev_info;

	xhci_endpoint_zero(xhci, xhci->devs[udev->slot_id], ep);

	xhci_dbg(xhci, "drop ep 0x%x, slot id %d, new drop flags = %#x, new add flags = %#x, new slot info = %#x\n",
			(unsigned int) ep->desc.bEndpointAddress,
			udev->slot_id,
			(unsigned int) new_drop_flags,
			(unsigned int) new_add_flags,
			(unsigned int) new_slot_info);
	return 0;
}

/* Add an endpoint to a new possible bandwidth configuration for this device.
 * Only one call to this function is allowed per endpoint before
 * check_bandwidth() or reset_bandwidth() must be called.
 * A call to xhci_drop_endpoint() followed by a call to xhci_add_endpoint() will
 * add the endpoint to the schedule with possibly new parameters denoted by a
 * different endpoint descriptor in usb_host_endpoint.
 * A call to xhci_add_endpoint() followed by a call to xhci_drop_endpoint() is
 * not allowed.
 *
 * The USB core will not allow URBs to be queued to an endpoint until the
 * configuration or alt setting is installed in the device, so there's no need
 * for mutual exclusion to protect the xhci->devs[slot_id] structure.
 */
int xhci_add_endpoint(struct usb_hcd *hcd, struct usb_device *udev,
		struct usb_host_endpoint *ep)
{
	struct xhci_hcd *xhci;
	struct xhci_container_ctx *in_ctx, *out_ctx;
	unsigned int ep_index;
	struct xhci_ep_ctx *ep_ctx;
	struct xhci_slot_ctx *slot_ctx;
	struct xhci_input_control_ctx *ctrl_ctx;
	u32 added_ctxs;
	unsigned int last_ctx;
	u32 new_add_flags, new_drop_flags, new_slot_info;
	int ret = 0;

	ret = xhci_check_args(hcd, udev, ep, 1, __func__);
	if (ret <= 0) {
		/* So we won't queue a reset ep command for a root hub */
		ep->hcpriv = NULL;
		return ret;
	}
	xhci = hcd_to_xhci(hcd);

	added_ctxs = xhci_get_endpoint_flag(&ep->desc);
	last_ctx = xhci_last_valid_endpoint(added_ctxs);
	if (added_ctxs == SLOT_FLAG || added_ctxs == EP0_FLAG) {
		/* FIXME when we have to issue an evaluate endpoint command to
		 * deal with ep0 max packet size changing once we get the
		 * descriptors
		 */
		xhci_dbg(xhci, "xHCI %s - can't add slot or ep 0 %#x\n",
				__func__, added_ctxs);
		return 0;
	}

	if (!xhci->devs || !xhci->devs[udev->slot_id]) {
		xhci_warn(xhci, "xHCI %s called with unaddressed device\n",
				__func__);
		return -EINVAL;
	}

	in_ctx = xhci->devs[udev->slot_id]->in_ctx;
	out_ctx = xhci->devs[udev->slot_id]->out_ctx;
	ctrl_ctx = xhci_get_input_control_ctx(xhci, in_ctx);
	ep_index = xhci_get_endpoint_index(&ep->desc);
	ep_ctx = xhci_get_ep_ctx(xhci, out_ctx, ep_index);
	/* If the HCD has already noted the endpoint is enabled,
	 * ignore this request.
	 */
	if (ctrl_ctx->add_flags & xhci_get_endpoint_flag(&ep->desc)) {
		xhci_warn(xhci, "xHCI %s called with enabled ep %p\n",
				__func__, ep);
		return 0;
	}

	/*
	 * Configuration and alternate setting changes must be done in
	 * process context, not interrupt context (or so documenation
	 * for usb_set_interface() and usb_set_configuration() claim).
	 */
	if (xhci_endpoint_init(xhci, xhci->devs[udev->slot_id],
				udev, ep, GFP_KERNEL) < 0) {
		dev_dbg(&udev->dev, "%s - could not initialize ep %#x\n",
				__func__, ep->desc.bEndpointAddress);
		return -ENOMEM;
	}

	ctrl_ctx->add_flags |= added_ctxs;
	new_add_flags = ctrl_ctx->add_flags;

	/* If xhci_endpoint_disable() was called for this endpoint, but the
	 * xHC hasn't been notified yet through the check_bandwidth() call,
	 * this re-adds a new state for the endpoint from the new endpoint
	 * descriptors.  We must drop and re-add this endpoint, so we leave the
	 * drop flags alone.
	 */
	new_drop_flags = ctrl_ctx->drop_flags;

	slot_ctx = xhci_get_slot_ctx(xhci, in_ctx);
	/* Update the last valid endpoint context, if we just added one past */
	if ((slot_ctx->dev_info & LAST_CTX_MASK) < LAST_CTX(last_ctx)) {
		slot_ctx->dev_info &= ~LAST_CTX_MASK;
		slot_ctx->dev_info |= LAST_CTX(last_ctx);
	}
	new_slot_info = slot_ctx->dev_info;

	/* Store the usb_device pointer for later use */
	ep->hcpriv = udev;

	xhci_dbg(xhci, "add ep 0x%x, slot id %d, new drop flags = %#x, new add flags = %#x, new slot info = %#x\n",
			(unsigned int) ep->desc.bEndpointAddress,
			udev->slot_id,
			(unsigned int) new_drop_flags,
			(unsigned int) new_add_flags,
			(unsigned int) new_slot_info);
	return 0;
}

static void xhci_zero_in_ctx(struct xhci_hcd *xhci, struct xhci_virt_device *virt_dev)
{
	struct xhci_input_control_ctx *ctrl_ctx;
	struct xhci_ep_ctx *ep_ctx;
	struct xhci_slot_ctx *slot_ctx;
	int i;

	/* When a device's add flag and drop flag are zero, any subsequent
	 * configure endpoint command will leave that endpoint's state
	 * untouched.  Make sure we don't leave any old state in the input
	 * endpoint contexts.
	 */
	ctrl_ctx = xhci_get_input_control_ctx(xhci, virt_dev->in_ctx);
	ctrl_ctx->drop_flags = 0;
	ctrl_ctx->add_flags = 0;
	slot_ctx = xhci_get_slot_ctx(xhci, virt_dev->in_ctx);
	slot_ctx->dev_info &= ~LAST_CTX_MASK;
	/* Endpoint 0 is always valid */
	slot_ctx->dev_info |= LAST_CTX(1);
	for (i = 1; i < 31; ++i) {
		ep_ctx = xhci_get_ep_ctx(xhci, virt_dev->in_ctx, i);
		ep_ctx->ep_info = 0;
		ep_ctx->ep_info2 = 0;
		ep_ctx->deq = 0;
		ep_ctx->tx_info = 0;
	}
}

static int xhci_configure_endpoint_result(struct xhci_hcd *xhci,
		struct usb_device *udev, int *cmd_status)
{
	int ret;

	switch (*cmd_status) {
	case COMP_ENOMEM:
		dev_warn(&udev->dev, "Not enough host controller resources "
				"for new device state.\n");
		ret = -ENOMEM;
		/* FIXME: can we allocate more resources for the HC? */
		break;
	case COMP_BW_ERR:
		dev_warn(&udev->dev, "Not enough bandwidth "
				"for new device state.\n");
		ret = -ENOSPC;
		/* FIXME: can we go back to the old state? */
		break;
	case COMP_TRB_ERR:
		/* the HCD set up something wrong */
		dev_warn(&udev->dev, "ERROR: Endpoint drop flag = 0, "
				"add flag = 1, "
				"and endpoint is not disabled.\n");
		ret = -EINVAL;
		break;
	case COMP_SUCCESS:
		dev_dbg(&udev->dev, "Successful Endpoint Configure command\n");
		ret = 0;
		break;
	default:
		xhci_err(xhci, "ERROR: unexpected command completion "
				"code 0x%x.\n", *cmd_status);
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int xhci_evaluate_context_result(struct xhci_hcd *xhci,
		struct usb_device *udev, int *cmd_status)
{
	int ret;
	struct xhci_virt_device *virt_dev = xhci->devs[udev->slot_id];

	switch (*cmd_status) {
	case COMP_EINVAL:
		dev_warn(&udev->dev, "WARN: xHCI driver setup invalid evaluate "
				"context command.\n");
		ret = -EINVAL;
		break;
	case COMP_EBADSLT:
		dev_warn(&udev->dev, "WARN: slot not enabled for"
				"evaluate context command.\n");
	case COMP_CTX_STATE:
		dev_warn(&udev->dev, "WARN: invalid context state for "
				"evaluate context command.\n");
		xhci_dbg_ctx(xhci, virt_dev->out_ctx, 1);
		ret = -EINVAL;
		break;
	case COMP_SUCCESS:
		dev_dbg(&udev->dev, "Successful evaluate context command\n");
		ret = 0;
		break;
	default:
		xhci_err(xhci, "ERROR: unexpected command completion "
				"code 0x%x.\n", *cmd_status);
		ret = -EINVAL;
		break;
	}
	return ret;
}

/* Issue a configure endpoint command or evaluate context command
 * and wait for it to finish.
 */
static int xhci_configure_endpoint(struct xhci_hcd *xhci,
		struct usb_device *udev,
		struct xhci_command *command,
		bool ctx_change, bool must_succeed)
{
	int ret;
	int timeleft;
	unsigned long flags;
	struct xhci_container_ctx *in_ctx;
	struct completion *cmd_completion;
	int *cmd_status;
	struct xhci_virt_device *virt_dev;

	spin_lock_irqsave(&xhci->lock, flags);
	virt_dev = xhci->devs[udev->slot_id];
	if (command) {
		in_ctx = command->in_ctx;
		cmd_completion = command->completion;
		cmd_status = &command->status;
		command->command_trb = xhci->cmd_ring->enqueue;
		list_add_tail(&command->cmd_list, &virt_dev->cmd_list);
	} else {
		in_ctx = virt_dev->in_ctx;
		cmd_completion = &virt_dev->cmd_completion;
		cmd_status = &virt_dev->cmd_status;
	}

	if (!ctx_change)
		ret = xhci_queue_configure_endpoint(xhci, in_ctx->dma,
				udev->slot_id, must_succeed);
	else
		ret = xhci_queue_evaluate_context(xhci, in_ctx->dma,
				udev->slot_id);
	if (ret < 0) {
		spin_unlock_irqrestore(&xhci->lock, flags);
		xhci_dbg(xhci, "FIXME allocate a new ring segment\n");
		return -ENOMEM;
	}
	xhci_ring_cmd_db(xhci);
	spin_unlock_irqrestore(&xhci->lock, flags);

	/* Wait for the configure endpoint command to complete */
	timeleft = wait_for_completion_interruptible_timeout(
			cmd_completion,
			USB_CTRL_SET_TIMEOUT);
	if (timeleft <= 0) {
		xhci_warn(xhci, "%s while waiting for %s command\n",
				timeleft == 0 ? "Timeout" : "Signal",
				ctx_change == 0 ?
					"configure endpoint" :
					"evaluate context");
		/* FIXME cancel the configure endpoint command */
		return -ETIME;
	}

	if (!ctx_change)
		return xhci_configure_endpoint_result(xhci, udev, cmd_status);
	return xhci_evaluate_context_result(xhci, udev, cmd_status);
}

/* Called after one or more calls to xhci_add_endpoint() or
 * xhci_drop_endpoint().  If this call fails, the USB core is expected
 * to call xhci_reset_bandwidth().
 *
 * Since we are in the middle of changing either configuration or
 * installing a new alt setting, the USB core won't allow URBs to be
 * enqueued for any endpoint on the old config or interface.  Nothing
 * else should be touching the xhci->devs[slot_id] structure, so we
 * don't need to take the xhci->lock for manipulating that.
 */
int xhci_check_bandwidth(struct usb_hcd *hcd, struct usb_device *udev)
{
	int i;
	int ret = 0;
	struct xhci_hcd *xhci;
	struct xhci_virt_device	*virt_dev;
	struct xhci_input_control_ctx *ctrl_ctx;
	struct xhci_slot_ctx *slot_ctx;

	ret = xhci_check_args(hcd, udev, NULL, 0, __func__);
	if (ret <= 0)
		return ret;
	xhci = hcd_to_xhci(hcd);

	if (!udev->slot_id || !xhci->devs || !xhci->devs[udev->slot_id]) {
		xhci_warn(xhci, "xHCI %s called with unaddressed device\n",
				__func__);
		return -EINVAL;
	}
	xhci_dbg(xhci, "%s called for udev %p\n", __func__, udev);
	virt_dev = xhci->devs[udev->slot_id];

	/* See section 4.6.6 - A0 = 1; A1 = D0 = D1 = 0 */
	ctrl_ctx = xhci_get_input_control_ctx(xhci, virt_dev->in_ctx);
	ctrl_ctx->add_flags |= SLOT_FLAG;
	ctrl_ctx->add_flags &= ~EP0_FLAG;
	ctrl_ctx->drop_flags &= ~SLOT_FLAG;
	ctrl_ctx->drop_flags &= ~EP0_FLAG;
	xhci_dbg(xhci, "New Input Control Context:\n");
	slot_ctx = xhci_get_slot_ctx(xhci, virt_dev->in_ctx);
	xhci_dbg_ctx(xhci, virt_dev->in_ctx,
			LAST_CTX_TO_EP_NUM(slot_ctx->dev_info));

	ret = xhci_configure_endpoint(xhci, udev, NULL,
			false, false);
	if (ret) {
		/* Callee should call reset_bandwidth() */
		return ret;
	}

	xhci_dbg(xhci, "Output context after successful config ep cmd:\n");
	xhci_dbg_ctx(xhci, virt_dev->out_ctx,
			LAST_CTX_TO_EP_NUM(slot_ctx->dev_info));

	xhci_zero_in_ctx(xhci, virt_dev);
	/* Free any old rings */
	for (i = 1; i < 31; ++i) {
		if (virt_dev->eps[i].new_ring) {
			xhci_ring_free(xhci, virt_dev->eps[i].ring);
			virt_dev->eps[i].ring = virt_dev->eps[i].new_ring;
			virt_dev->eps[i].new_ring = NULL;
		}
	}

	return ret;
}

void xhci_reset_bandwidth(struct usb_hcd *hcd, struct usb_device *udev)
{
	struct xhci_hcd *xhci;
	struct xhci_virt_device	*virt_dev;
	int i, ret;

	ret = xhci_check_args(hcd, udev, NULL, 0, __func__);
	if (ret <= 0)
		return;
	xhci = hcd_to_xhci(hcd);

	if (!xhci->devs || !xhci->devs[udev->slot_id]) {
		xhci_warn(xhci, "xHCI %s called with unaddressed device\n",
				__func__);
		return;
	}
	xhci_dbg(xhci, "%s called for udev %p\n", __func__, udev);
	virt_dev = xhci->devs[udev->slot_id];
	/* Free any rings allocated for added endpoints */
	for (i = 0; i < 31; ++i) {
		if (virt_dev->eps[i].new_ring) {
			xhci_ring_free(xhci, virt_dev->eps[i].new_ring);
			virt_dev->eps[i].new_ring = NULL;
		}
	}
	xhci_zero_in_ctx(xhci, virt_dev);
}

static void xhci_setup_input_ctx_for_config_ep(struct xhci_hcd *xhci,
		struct xhci_container_ctx *in_ctx,
		struct xhci_container_ctx *out_ctx,
		u32 add_flags, u32 drop_flags)
{
	struct xhci_input_control_ctx *ctrl_ctx;
	ctrl_ctx = xhci_get_input_control_ctx(xhci, in_ctx);
	ctrl_ctx->add_flags = add_flags;
	ctrl_ctx->drop_flags = drop_flags;
	xhci_slot_copy(xhci, in_ctx, out_ctx);
	ctrl_ctx->add_flags |= SLOT_FLAG;

	xhci_dbg(xhci, "Input Context:\n");
	xhci_dbg_ctx(xhci, in_ctx, xhci_last_valid_endpoint(add_flags));
}

void xhci_setup_input_ctx_for_quirk(struct xhci_hcd *xhci,
		unsigned int slot_id, unsigned int ep_index,
		struct xhci_dequeue_state *deq_state)
{
	struct xhci_container_ctx *in_ctx;
	struct xhci_ep_ctx *ep_ctx;
	u32 added_ctxs;
	dma_addr_t addr;

	xhci_endpoint_copy(xhci, xhci->devs[slot_id]->in_ctx,
			xhci->devs[slot_id]->out_ctx, ep_index);
	in_ctx = xhci->devs[slot_id]->in_ctx;
	ep_ctx = xhci_get_ep_ctx(xhci, in_ctx, ep_index);
	addr = xhci_trb_virt_to_dma(deq_state->new_deq_seg,
			deq_state->new_deq_ptr);
	if (addr == 0) {
		xhci_warn(xhci, "WARN Cannot submit config ep after "
				"reset ep command\n");
		xhci_warn(xhci, "WARN deq seg = %p, deq ptr = %p\n",
				deq_state->new_deq_seg,
				deq_state->new_deq_ptr);
		return;
	}
	ep_ctx->deq = addr | deq_state->new_cycle_state;

	added_ctxs = xhci_get_endpoint_flag_from_index(ep_index);
	xhci_setup_input_ctx_for_config_ep(xhci, xhci->devs[slot_id]->in_ctx,
			xhci->devs[slot_id]->out_ctx, added_ctxs, added_ctxs);
}

void xhci_cleanup_stalled_ring(struct xhci_hcd *xhci,
		struct usb_device *udev, unsigned int ep_index)
{
	struct xhci_dequeue_state deq_state;
	struct xhci_virt_ep *ep;

	xhci_dbg(xhci, "Cleaning up stalled endpoint ring\n");
	ep = &xhci->devs[udev->slot_id]->eps[ep_index];
	/* We need to move the HW's dequeue pointer past this TD,
	 * or it will attempt to resend it on the next doorbell ring.
	 */
	xhci_find_new_dequeue_state(xhci, udev->slot_id,
			ep_index, ep->stopped_td,
			&deq_state);

	/* HW with the reset endpoint quirk will use the saved dequeue state to
	 * issue a configure endpoint command later.
	 */
	if (!(xhci->quirks & XHCI_RESET_EP_QUIRK)) {
		xhci_dbg(xhci, "Queueing new dequeue state\n");
		xhci_queue_new_dequeue_state(xhci, udev->slot_id,
				ep_index, &deq_state);
	} else {
		/* Better hope no one uses the input context between now and the
		 * reset endpoint completion!
		 */
		xhci_dbg(xhci, "Setting up input context for "
				"configure endpoint command\n");
		xhci_setup_input_ctx_for_quirk(xhci, udev->slot_id,
				ep_index, &deq_state);
	}
}

/* Deal with stalled endpoints.  The core should have sent the control message
 * to clear the halt condition.  However, we need to make the xHCI hardware
 * reset its sequence number, since a device will expect a sequence number of
 * zero after the halt condition is cleared.
 * Context: in_interrupt
 */
void xhci_endpoint_reset(struct usb_hcd *hcd,
		struct usb_host_endpoint *ep)
{
	struct xhci_hcd *xhci;
	struct usb_device *udev;
	unsigned int ep_index;
	unsigned long flags;
	int ret;
	struct xhci_virt_ep *virt_ep;

	xhci = hcd_to_xhci(hcd);
	udev = (struct usb_device *) ep->hcpriv;
	/* Called with a root hub endpoint (or an endpoint that wasn't added
	 * with xhci_add_endpoint()
	 */
	if (!ep->hcpriv)
		return;
	ep_index = xhci_get_endpoint_index(&ep->desc);
	virt_ep = &xhci->devs[udev->slot_id]->eps[ep_index];
	if (!virt_ep->stopped_td) {
		xhci_dbg(xhci, "Endpoint 0x%x not halted, refusing to reset.\n",
				ep->desc.bEndpointAddress);
		return;
	}
	if (usb_endpoint_xfer_control(&ep->desc)) {
		xhci_dbg(xhci, "Control endpoint stall already handled.\n");
		return;
	}

	xhci_dbg(xhci, "Queueing reset endpoint command\n");
	spin_lock_irqsave(&xhci->lock, flags);
	ret = xhci_queue_reset_ep(xhci, udev->slot_id, ep_index);
	/*
	 * Can't change the ring dequeue pointer until it's transitioned to the
	 * stopped state, which is only upon a successful reset endpoint
	 * command.  Better hope that last command worked!
	 */
	if (!ret) {
		xhci_cleanup_stalled_ring(xhci, udev, ep_index);
		kfree(virt_ep->stopped_td);
		xhci_ring_cmd_db(xhci);
	}
	spin_unlock_irqrestore(&xhci->lock, flags);

	if (ret)
		xhci_warn(xhci, "FIXME allocate a new ring segment\n");
}

/*
 * At this point, the struct usb_device is about to go away, the device has
 * disconnected, and all traffic has been stopped and the endpoints have been
 * disabled.  Free any HC data structures associated with that device.
 */
void xhci_free_dev(struct usb_hcd *hcd, struct usb_device *udev)
{
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	unsigned long flags;
	u32 state;

	if (udev->slot_id == 0)
		return;

	spin_lock_irqsave(&xhci->lock, flags);
	/* Don't disable the slot if the host controller is dead. */
	state = xhci_readl(xhci, &xhci->op_regs->status);
	if (state == 0xffffffff) {
		xhci_free_virt_device(xhci, udev->slot_id);
		spin_unlock_irqrestore(&xhci->lock, flags);
		return;
	}

	if (xhci_queue_slot_control(xhci, TRB_DISABLE_SLOT, udev->slot_id)) {
		spin_unlock_irqrestore(&xhci->lock, flags);
		xhci_dbg(xhci, "FIXME: allocate a command ring segment\n");
		return;
	}
	xhci_ring_cmd_db(xhci);
	spin_unlock_irqrestore(&xhci->lock, flags);
	/*
	 * Event command completion handler will free any data structures
	 * associated with the slot.  XXX Can free sleep?
	 */
}

/*
 * Returns 0 if the xHC ran out of device slots, the Enable Slot command
 * timed out, or allocating memory failed.  Returns 1 on success.
 */
int xhci_alloc_dev(struct usb_hcd *hcd, struct usb_device *udev)
{
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	unsigned long flags;
	int timeleft;
	int ret;

	spin_lock_irqsave(&xhci->lock, flags);
	ret = xhci_queue_slot_control(xhci, TRB_ENABLE_SLOT, 0);
	if (ret) {
		spin_unlock_irqrestore(&xhci->lock, flags);
		xhci_dbg(xhci, "FIXME: allocate a command ring segment\n");
		return 0;
	}
	xhci_ring_cmd_db(xhci);
	spin_unlock_irqrestore(&xhci->lock, flags);

	/* XXX: how much time for xHC slot assignment? */
	timeleft = wait_for_completion_interruptible_timeout(&xhci->addr_dev,
			USB_CTRL_SET_TIMEOUT);
	if (timeleft <= 0) {
		xhci_warn(xhci, "%s while waiting for a slot\n",
				timeleft == 0 ? "Timeout" : "Signal");
		/* FIXME cancel the enable slot request */
		return 0;
	}

	if (!xhci->slot_id) {
		xhci_err(xhci, "Error while assigning device slot ID\n");
		return 0;
	}
	/* xhci_alloc_virt_device() does not touch rings; no need to lock */
	if (!xhci_alloc_virt_device(xhci, xhci->slot_id, udev, GFP_KERNEL)) {
		/* Disable slot, if we can do it without mem alloc */
		xhci_warn(xhci, "Could not allocate xHCI USB device data structures\n");
		spin_lock_irqsave(&xhci->lock, flags);
		if (!xhci_queue_slot_control(xhci, TRB_DISABLE_SLOT, udev->slot_id))
			xhci_ring_cmd_db(xhci);
		spin_unlock_irqrestore(&xhci->lock, flags);
		return 0;
	}
	udev->slot_id = xhci->slot_id;
	/* Is this a LS or FS device under a HS hub? */
	/* Hub or peripherial? */
	return 1;
}

/*
 * Issue an Address Device command (which will issue a SetAddress request to
 * the device).
 * We should be protected by the usb_address0_mutex in khubd's hub_port_init, so
 * we should only issue and wait on one address command at the same time.
 *
 * We add one to the device address issued by the hardware because the USB core
 * uses address 1 for the root hubs (even though they're not really devices).
 */
int xhci_address_device(struct usb_hcd *hcd, struct usb_device *udev)
{
	unsigned long flags;
	int timeleft;
	struct xhci_virt_device *virt_dev;
	int ret = 0;
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	struct xhci_slot_ctx *slot_ctx;
	struct xhci_input_control_ctx *ctrl_ctx;
	u64 temp_64;

	if (!udev->slot_id) {
		xhci_dbg(xhci, "Bad Slot ID %d\n", udev->slot_id);
		return -EINVAL;
	}

	virt_dev = xhci->devs[udev->slot_id];

	/* If this is a Set Address to an unconfigured device, setup ep 0 */
	if (!udev->config)
		xhci_setup_addressable_virt_dev(xhci, udev);
	/* Otherwise, assume the core has the device configured how it wants */
	xhci_dbg(xhci, "Slot ID %d Input Context:\n", udev->slot_id);
	xhci_dbg_ctx(xhci, virt_dev->in_ctx, 2);

	spin_lock_irqsave(&xhci->lock, flags);
	ret = xhci_queue_address_device(xhci, virt_dev->in_ctx->dma,
					udev->slot_id);
	if (ret) {
		spin_unlock_irqrestore(&xhci->lock, flags);
		xhci_dbg(xhci, "FIXME: allocate a command ring segment\n");
		return ret;
	}
	xhci_ring_cmd_db(xhci);
	spin_unlock_irqrestore(&xhci->lock, flags);

	/* ctrl tx can take up to 5 sec; XXX: need more time for xHC? */
	timeleft = wait_for_completion_interruptible_timeout(&xhci->addr_dev,
			USB_CTRL_SET_TIMEOUT);
	/* FIXME: From section 4.3.4: "Software shall be responsible for timing
	 * the SetAddress() "recovery interval" required by USB and aborting the
	 * command on a timeout.
	 */
	if (timeleft <= 0) {
		xhci_warn(xhci, "%s while waiting for a slot\n",
				timeleft == 0 ? "Timeout" : "Signal");
		/* FIXME cancel the address device command */
		return -ETIME;
	}

	switch (virt_dev->cmd_status) {
	case COMP_CTX_STATE:
	case COMP_EBADSLT:
		xhci_err(xhci, "Setup ERROR: address device command for slot %d.\n",
				udev->slot_id);
		ret = -EINVAL;
		break;
	case COMP_TX_ERR:
		dev_warn(&udev->dev, "Device not responding to set address.\n");
		ret = -EPROTO;
		break;
	case COMP_SUCCESS:
		xhci_dbg(xhci, "Successful Address Device command\n");
		break;
	default:
		xhci_err(xhci, "ERROR: unexpected command completion "
				"code 0x%x.\n", virt_dev->cmd_status);
		xhci_dbg(xhci, "Slot ID %d Output Context:\n", udev->slot_id);
		xhci_dbg_ctx(xhci, virt_dev->out_ctx, 2);
		ret = -EINVAL;
		break;
	}
	if (ret) {
		return ret;
	}
	temp_64 = xhci_read_64(xhci, &xhci->op_regs->dcbaa_ptr);
	xhci_dbg(xhci, "Op regs DCBAA ptr = %#016llx\n", temp_64);
	xhci_dbg(xhci, "Slot ID %d dcbaa entry @%p = %#016llx\n",
			udev->slot_id,
			&xhci->dcbaa->dev_context_ptrs[udev->slot_id],
			(unsigned long long)
				xhci->dcbaa->dev_context_ptrs[udev->slot_id]);
	xhci_dbg(xhci, "Output Context DMA address = %#08llx\n",
			(unsigned long long)virt_dev->out_ctx->dma);
	xhci_dbg(xhci, "Slot ID %d Input Context:\n", udev->slot_id);
	xhci_dbg_ctx(xhci, virt_dev->in_ctx, 2);
	xhci_dbg(xhci, "Slot ID %d Output Context:\n", udev->slot_id);
	xhci_dbg_ctx(xhci, virt_dev->out_ctx, 2);
	/*
	 * USB core uses address 1 for the roothubs, so we add one to the
	 * address given back to us by the HC.
	 */
	slot_ctx = xhci_get_slot_ctx(xhci, virt_dev->out_ctx);
	udev->devnum = (slot_ctx->dev_state & DEV_ADDR_MASK) + 1;
	/* Zero the input context control for later use */
	ctrl_ctx = xhci_get_input_control_ctx(xhci, virt_dev->in_ctx);
	ctrl_ctx->add_flags = 0;
	ctrl_ctx->drop_flags = 0;

	xhci_dbg(xhci, "Device address = %d\n", udev->devnum);
	/* XXX Meh, not sure if anyone else but choose_address uses this. */
	set_bit(udev->devnum, udev->bus->devmap.devicemap);

	return 0;
}

/* Once a hub descriptor is fetched for a device, we need to update the xHC's
 * internal data structures for the device.
 */
int xhci_update_hub_device(struct usb_hcd *hcd, struct usb_device *hdev,
			struct usb_tt *tt, gfp_t mem_flags)
{
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	struct xhci_virt_device *vdev;
	struct xhci_command *config_cmd;
	struct xhci_input_control_ctx *ctrl_ctx;
	struct xhci_slot_ctx *slot_ctx;
	unsigned long flags;
	unsigned think_time;
	int ret;

	/* Ignore root hubs */
	if (!hdev->parent)
		return 0;

	vdev = xhci->devs[hdev->slot_id];
	if (!vdev) {
		xhci_warn(xhci, "Cannot update hub desc for unknown device.\n");
		return -EINVAL;
	}
	config_cmd = xhci_alloc_command(xhci, true, mem_flags);
	if (!config_cmd) {
		xhci_dbg(xhci, "Could not allocate xHCI command structure.\n");
		return -ENOMEM;
	}

	spin_lock_irqsave(&xhci->lock, flags);
	xhci_slot_copy(xhci, config_cmd->in_ctx, vdev->out_ctx);
	ctrl_ctx = xhci_get_input_control_ctx(xhci, config_cmd->in_ctx);
	ctrl_ctx->add_flags |= SLOT_FLAG;
	slot_ctx = xhci_get_slot_ctx(xhci, config_cmd->in_ctx);
	slot_ctx->dev_info |= DEV_HUB;
	if (tt->multi)
		slot_ctx->dev_info |= DEV_MTT;
	if (xhci->hci_version > 0x95) {
		xhci_dbg(xhci, "xHCI version %x needs hub "
				"TT think time and number of ports\n",
				(unsigned int) xhci->hci_version);
		slot_ctx->dev_info2 |= XHCI_MAX_PORTS(hdev->maxchild);
		/* Set TT think time - convert from ns to FS bit times.
		 * 0 = 8 FS bit times, 1 = 16 FS bit times,
		 * 2 = 24 FS bit times, 3 = 32 FS bit times.
		 */
		think_time = tt->think_time;
		if (think_time != 0)
			think_time = (think_time / 666) - 1;
		slot_ctx->tt_info |= TT_THINK_TIME(think_time);
	} else {
		xhci_dbg(xhci, "xHCI version %x doesn't need hub "
				"TT think time or number of ports\n",
				(unsigned int) xhci->hci_version);
	}
	slot_ctx->dev_state = 0;
	spin_unlock_irqrestore(&xhci->lock, flags);

	xhci_dbg(xhci, "Set up %s for hub device.\n",
			(xhci->hci_version > 0x95) ?
			"configure endpoint" : "evaluate context");
	xhci_dbg(xhci, "Slot %u Input Context:\n", hdev->slot_id);
	xhci_dbg_ctx(xhci, config_cmd->in_ctx, 0);

	/* Issue and wait for the configure endpoint or
	 * evaluate context command.
	 */
	if (xhci->hci_version > 0x95)
		ret = xhci_configure_endpoint(xhci, hdev, config_cmd,
				false, false);
	else
		ret = xhci_configure_endpoint(xhci, hdev, config_cmd,
				true, false);

	xhci_dbg(xhci, "Slot %u Output Context:\n", hdev->slot_id);
	xhci_dbg_ctx(xhci, vdev->out_ctx, 0);

	xhci_free_command(xhci, config_cmd);
	return ret;
}

int xhci_get_frame(struct usb_hcd *hcd)
{
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	/* EHCI mods by the periodic size.  Why? */
	return xhci_readl(xhci, &xhci->run_regs->microframe_index) >> 3;
}

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_LICENSE("GPL");

static int __init xhci_hcd_init(void)
{
#ifdef CONFIG_PCI
	int retval = 0;

	retval = xhci_register_pci();

	if (retval < 0) {
		printk(KERN_DEBUG "Problem registering PCI driver.");
		return retval;
	}
#endif
	/*
	 * Check the compiler generated sizes of structures that must be laid
	 * out in specific ways for hardware access.
	 */
	BUILD_BUG_ON(sizeof(struct xhci_doorbell_array) != 256*32/8);
	BUILD_BUG_ON(sizeof(struct xhci_slot_ctx) != 8*32/8);
	BUILD_BUG_ON(sizeof(struct xhci_ep_ctx) != 8*32/8);
	/* xhci_device_control has eight fields, and also
	 * embeds one xhci_slot_ctx and 31 xhci_ep_ctx
	 */
	BUILD_BUG_ON(sizeof(struct xhci_stream_ctx) != 4*32/8);
	BUILD_BUG_ON(sizeof(union xhci_trb) != 4*32/8);
	BUILD_BUG_ON(sizeof(struct xhci_erst_entry) != 4*32/8);
	BUILD_BUG_ON(sizeof(struct xhci_cap_regs) != 7*32/8);
	BUILD_BUG_ON(sizeof(struct xhci_intr_reg) != 8*32/8);
	/* xhci_run_regs has eight fields and embeds 128 xhci_intr_regs */
	BUILD_BUG_ON(sizeof(struct xhci_run_regs) != (8+8*128)*32/8);
	BUILD_BUG_ON(sizeof(struct xhci_doorbell_array) != 256*32/8);
	return 0;
}
module_init(xhci_hcd_init);

static void __exit xhci_hcd_cleanup(void)
{
#ifdef CONFIG_PCI
	xhci_unregister_pci();
#endif
}
module_exit(xhci_hcd_cleanup);
