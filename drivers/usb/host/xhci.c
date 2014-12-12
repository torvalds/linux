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

#include <linux/pci.h>
#include <linux/irq.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/dmi.h>
#include <linux/dma-mapping.h>

#include "xhci.h"
#include "xhci-trace.h"

#define DRIVER_AUTHOR "Sarah Sharp"
#define DRIVER_DESC "'eXtensible' Host Controller (xHC) Driver"

#define	PORT_WAKE_BITS	(PORT_WKOC_E | PORT_WKDISC_E | PORT_WKCONN_E)

/* Some 0.95 hardware can't handle the chain bit on a Link TRB being cleared */
static int link_quirk;
module_param(link_quirk, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(link_quirk, "Don't clear the chain bit on a link TRB");

static unsigned int quirks;
module_param(quirks, uint, S_IRUGO);
MODULE_PARM_DESC(quirks, "Bit flags for quirks to be enabled as default");

/* TODO: copied from ehci-hcd.c - can this be refactored? */
/*
 * xhci_handshake - spin reading hc until handshake completes or fails
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
int xhci_handshake(struct xhci_hcd *xhci, void __iomem *ptr,
		      u32 mask, u32 done, int usec)
{
	u32	result;

	do {
		result = readl(ptr);
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
 * Disable interrupts and begin the xHCI halting process.
 */
void xhci_quiesce(struct xhci_hcd *xhci)
{
	u32 halted;
	u32 cmd;
	u32 mask;

	mask = ~(XHCI_IRQS);
	halted = readl(&xhci->op_regs->status) & STS_HALT;
	if (!halted)
		mask &= ~CMD_RUN;

	cmd = readl(&xhci->op_regs->command);
	cmd &= mask;
	writel(cmd, &xhci->op_regs->command);
}

/*
 * Force HC into halt state.
 *
 * Disable any IRQs and clear the run/stop bit.
 * HC will complete any current and actively pipelined transactions, and
 * should halt within 16 ms of the run/stop bit being cleared.
 * Read HC Halted bit in the status register to see when the HC is finished.
 */
int xhci_halt(struct xhci_hcd *xhci)
{
	int ret;
	xhci_dbg_trace(xhci, trace_xhci_dbg_init, "// Halt the HC");
	xhci_quiesce(xhci);

	ret = xhci_handshake(xhci, &xhci->op_regs->status,
			STS_HALT, STS_HALT, XHCI_MAX_HALT_USEC);
	if (!ret) {
		xhci->xhc_state |= XHCI_STATE_HALTED;
		xhci->cmd_ring_state = CMD_RING_STATE_STOPPED;
	} else
		xhci_warn(xhci, "Host not halted after %u microseconds.\n",
				XHCI_MAX_HALT_USEC);
	return ret;
}

/*
 * Set the run bit and wait for the host to be running.
 */
static int xhci_start(struct xhci_hcd *xhci)
{
	u32 temp;
	int ret;

	temp = readl(&xhci->op_regs->command);
	temp |= (CMD_RUN);
	xhci_dbg_trace(xhci, trace_xhci_dbg_init, "// Turn on HC, cmd = 0x%x.",
			temp);
	writel(temp, &xhci->op_regs->command);

	/*
	 * Wait for the HCHalted Status bit to be 0 to indicate the host is
	 * running.
	 */
	ret = xhci_handshake(xhci, &xhci->op_regs->status,
			STS_HALT, 0, XHCI_MAX_HALT_USEC);
	if (ret == -ETIMEDOUT)
		xhci_err(xhci, "Host took too long to start, "
				"waited %u microseconds.\n",
				XHCI_MAX_HALT_USEC);
	if (!ret)
		xhci->xhc_state &= ~XHCI_STATE_HALTED;
	return ret;
}

/*
 * Reset a halted HC.
 *
 * This resets pipelines, timers, counters, state machines, etc.
 * Transactions will be terminated immediately, and operational registers
 * will be set to their defaults.
 */
int xhci_reset(struct xhci_hcd *xhci)
{
	u32 command;
	u32 state;
	int ret, i;

	state = readl(&xhci->op_regs->status);
	if ((state & STS_HALT) == 0) {
		xhci_warn(xhci, "Host controller not halted, aborting reset.\n");
		return 0;
	}

	xhci_dbg_trace(xhci, trace_xhci_dbg_init, "// Reset the HC");
	command = readl(&xhci->op_regs->command);
	command |= CMD_RESET;
	writel(command, &xhci->op_regs->command);

	ret = xhci_handshake(xhci, &xhci->op_regs->command,
			CMD_RESET, 0, 10 * 1000 * 1000);
	if (ret)
		return ret;

	xhci_dbg_trace(xhci, trace_xhci_dbg_init,
			 "Wait for controller to be ready for doorbell rings");
	/*
	 * xHCI cannot write to any doorbells or operational registers other
	 * than status until the "Controller Not Ready" flag is cleared.
	 */
	ret = xhci_handshake(xhci, &xhci->op_regs->status,
			STS_CNR, 0, 10 * 1000 * 1000);

	for (i = 0; i < 2; ++i) {
		xhci->bus_state[i].port_c_suspend = 0;
		xhci->bus_state[i].suspended_ports = 0;
		xhci->bus_state[i].resuming_ports = 0;
	}

	return ret;
}

#ifdef CONFIG_PCI
static int xhci_free_msi(struct xhci_hcd *xhci)
{
	int i;

	if (!xhci->msix_entries)
		return -EINVAL;

	for (i = 0; i < xhci->msix_count; i++)
		if (xhci->msix_entries[i].vector)
			free_irq(xhci->msix_entries[i].vector,
					xhci_to_hcd(xhci));
	return 0;
}

/*
 * Set up MSI
 */
static int xhci_setup_msi(struct xhci_hcd *xhci)
{
	int ret;
	struct pci_dev  *pdev = to_pci_dev(xhci_to_hcd(xhci)->self.controller);

	ret = pci_enable_msi(pdev);
	if (ret) {
		xhci_dbg_trace(xhci, trace_xhci_dbg_init,
				"failed to allocate MSI entry");
		return ret;
	}

	ret = request_irq(pdev->irq, xhci_msi_irq,
				0, "xhci_hcd", xhci_to_hcd(xhci));
	if (ret) {
		xhci_dbg_trace(xhci, trace_xhci_dbg_init,
				"disable MSI interrupt");
		pci_disable_msi(pdev);
	}

	return ret;
}

/*
 * Free IRQs
 * free all IRQs request
 */
static void xhci_free_irq(struct xhci_hcd *xhci)
{
	struct pci_dev *pdev = to_pci_dev(xhci_to_hcd(xhci)->self.controller);
	int ret;

	/* return if using legacy interrupt */
	if (xhci_to_hcd(xhci)->irq > 0)
		return;

	ret = xhci_free_msi(xhci);
	if (!ret)
		return;
	if (pdev->irq > 0)
		free_irq(pdev->irq, xhci_to_hcd(xhci));

	return;
}

/*
 * Set up MSI-X
 */
static int xhci_setup_msix(struct xhci_hcd *xhci)
{
	int i, ret = 0;
	struct usb_hcd *hcd = xhci_to_hcd(xhci);
	struct pci_dev *pdev = to_pci_dev(hcd->self.controller);

	/*
	 * calculate number of msi-x vectors supported.
	 * - HCS_MAX_INTRS: the max number of interrupts the host can handle,
	 *   with max number of interrupters based on the xhci HCSPARAMS1.
	 * - num_online_cpus: maximum msi-x vectors per CPUs core.
	 *   Add additional 1 vector to ensure always available interrupt.
	 */
	xhci->msix_count = min(num_online_cpus() + 1,
				HCS_MAX_INTRS(xhci->hcs_params1));

	xhci->msix_entries =
		kmalloc((sizeof(struct msix_entry))*xhci->msix_count,
				GFP_KERNEL);
	if (!xhci->msix_entries) {
		xhci_err(xhci, "Failed to allocate MSI-X entries\n");
		return -ENOMEM;
	}

	for (i = 0; i < xhci->msix_count; i++) {
		xhci->msix_entries[i].entry = i;
		xhci->msix_entries[i].vector = 0;
	}

	ret = pci_enable_msix_exact(pdev, xhci->msix_entries, xhci->msix_count);
	if (ret) {
		xhci_dbg_trace(xhci, trace_xhci_dbg_init,
				"Failed to enable MSI-X");
		goto free_entries;
	}

	for (i = 0; i < xhci->msix_count; i++) {
		ret = request_irq(xhci->msix_entries[i].vector,
				xhci_msi_irq,
				0, "xhci_hcd", xhci_to_hcd(xhci));
		if (ret)
			goto disable_msix;
	}

	hcd->msix_enabled = 1;
	return ret;

disable_msix:
	xhci_dbg_trace(xhci, trace_xhci_dbg_init, "disable MSI-X interrupt");
	xhci_free_irq(xhci);
	pci_disable_msix(pdev);
free_entries:
	kfree(xhci->msix_entries);
	xhci->msix_entries = NULL;
	return ret;
}

/* Free any IRQs and disable MSI-X */
static void xhci_cleanup_msix(struct xhci_hcd *xhci)
{
	struct usb_hcd *hcd = xhci_to_hcd(xhci);
	struct pci_dev *pdev = to_pci_dev(hcd->self.controller);

	if (xhci->quirks & XHCI_PLAT)
		return;

	xhci_free_irq(xhci);

	if (xhci->msix_entries) {
		pci_disable_msix(pdev);
		kfree(xhci->msix_entries);
		xhci->msix_entries = NULL;
	} else {
		pci_disable_msi(pdev);
	}

	hcd->msix_enabled = 0;
	return;
}

static void __maybe_unused xhci_msix_sync_irqs(struct xhci_hcd *xhci)
{
	int i;

	if (xhci->msix_entries) {
		for (i = 0; i < xhci->msix_count; i++)
			synchronize_irq(xhci->msix_entries[i].vector);
	}
}

static int xhci_try_enable_msi(struct usb_hcd *hcd)
{
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	struct pci_dev  *pdev;
	int ret;

	/* The xhci platform device has set up IRQs through usb_add_hcd. */
	if (xhci->quirks & XHCI_PLAT)
		return 0;

	pdev = to_pci_dev(xhci_to_hcd(xhci)->self.controller);
	/*
	 * Some Fresco Logic host controllers advertise MSI, but fail to
	 * generate interrupts.  Don't even try to enable MSI.
	 */
	if (xhci->quirks & XHCI_BROKEN_MSI)
		goto legacy_irq;

	/* unregister the legacy interrupt */
	if (hcd->irq)
		free_irq(hcd->irq, hcd);
	hcd->irq = 0;

	ret = xhci_setup_msix(xhci);
	if (ret)
		/* fall back to msi*/
		ret = xhci_setup_msi(xhci);

	if (!ret)
		/* hcd->irq is 0, we have MSI */
		return 0;

	if (!pdev->irq) {
		xhci_err(xhci, "No msi-x/msi found and no IRQ in BIOS\n");
		return -EINVAL;
	}

 legacy_irq:
	if (!strlen(hcd->irq_descr))
		snprintf(hcd->irq_descr, sizeof(hcd->irq_descr), "%s:usb%d",
			 hcd->driver->description, hcd->self.busnum);

	/* fall back to legacy interrupt*/
	ret = request_irq(pdev->irq, &usb_hcd_irq, IRQF_SHARED,
			hcd->irq_descr, hcd);
	if (ret) {
		xhci_err(xhci, "request interrupt %d failed\n",
				pdev->irq);
		return ret;
	}
	hcd->irq = pdev->irq;
	return 0;
}

#else

static inline int xhci_try_enable_msi(struct usb_hcd *hcd)
{
	return 0;
}

static inline void xhci_cleanup_msix(struct xhci_hcd *xhci)
{
}

static inline void xhci_msix_sync_irqs(struct xhci_hcd *xhci)
{
}

#endif

static void compliance_mode_recovery(unsigned long arg)
{
	struct xhci_hcd *xhci;
	struct usb_hcd *hcd;
	u32 temp;
	int i;

	xhci = (struct xhci_hcd *)arg;

	for (i = 0; i < xhci->num_usb3_ports; i++) {
		temp = readl(xhci->usb3_ports[i]);
		if ((temp & PORT_PLS_MASK) == USB_SS_PORT_LS_COMP_MOD) {
			/*
			 * Compliance Mode Detected. Letting USB Core
			 * handle the Warm Reset
			 */
			xhci_dbg_trace(xhci, trace_xhci_dbg_quirks,
					"Compliance mode detected->port %d",
					i + 1);
			xhci_dbg_trace(xhci, trace_xhci_dbg_quirks,
					"Attempting compliance mode recovery");
			hcd = xhci->shared_hcd;

			if (hcd->state == HC_STATE_SUSPENDED)
				usb_hcd_resume_root_hub(hcd);

			usb_hcd_poll_rh_status(hcd);
		}
	}

	if (xhci->port_status_u0 != ((1 << xhci->num_usb3_ports)-1))
		mod_timer(&xhci->comp_mode_recovery_timer,
			jiffies + msecs_to_jiffies(COMP_MODE_RCVRY_MSECS));
}

/*
 * Quirk to work around issue generated by the SN65LVPE502CP USB3.0 re-driver
 * that causes ports behind that hardware to enter compliance mode sometimes.
 * The quirk creates a timer that polls every 2 seconds the link state of
 * each host controller's port and recovers it by issuing a Warm reset
 * if Compliance mode is detected, otherwise the port will become "dead" (no
 * device connections or disconnections will be detected anymore). Becasue no
 * status event is generated when entering compliance mode (per xhci spec),
 * this quirk is needed on systems that have the failing hardware installed.
 */
static void compliance_mode_recovery_timer_init(struct xhci_hcd *xhci)
{
	xhci->port_status_u0 = 0;
	init_timer(&xhci->comp_mode_recovery_timer);

	xhci->comp_mode_recovery_timer.data = (unsigned long) xhci;
	xhci->comp_mode_recovery_timer.function = compliance_mode_recovery;
	xhci->comp_mode_recovery_timer.expires = jiffies +
			msecs_to_jiffies(COMP_MODE_RCVRY_MSECS);

	set_timer_slack(&xhci->comp_mode_recovery_timer,
			msecs_to_jiffies(COMP_MODE_RCVRY_MSECS));
	add_timer(&xhci->comp_mode_recovery_timer);
	xhci_dbg_trace(xhci, trace_xhci_dbg_quirks,
			"Compliance mode recovery timer initialized");
}

/*
 * This function identifies the systems that have installed the SN65LVPE502CP
 * USB3.0 re-driver and that need the Compliance Mode Quirk.
 * Systems:
 * Vendor: Hewlett-Packard -> System Models: Z420, Z620 and Z820
 */
static bool xhci_compliance_mode_recovery_timer_quirk_check(void)
{
	const char *dmi_product_name, *dmi_sys_vendor;

	dmi_product_name = dmi_get_system_info(DMI_PRODUCT_NAME);
	dmi_sys_vendor = dmi_get_system_info(DMI_SYS_VENDOR);
	if (!dmi_product_name || !dmi_sys_vendor)
		return false;

	if (!(strstr(dmi_sys_vendor, "Hewlett-Packard")))
		return false;

	if (strstr(dmi_product_name, "Z420") ||
			strstr(dmi_product_name, "Z620") ||
			strstr(dmi_product_name, "Z820") ||
			strstr(dmi_product_name, "Z1 Workstation"))
		return true;

	return false;
}

static int xhci_all_ports_seen_u0(struct xhci_hcd *xhci)
{
	return (xhci->port_status_u0 == ((1 << xhci->num_usb3_ports)-1));
}


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

	xhci_dbg_trace(xhci, trace_xhci_dbg_init, "xhci_init");
	spin_lock_init(&xhci->lock);
	if (xhci->hci_version == 0x95 && link_quirk) {
		xhci_dbg_trace(xhci, trace_xhci_dbg_quirks,
				"QUIRK: Not clearing Link TRB chain bits.");
		xhci->quirks |= XHCI_LINK_TRB_QUIRK;
	} else {
		xhci_dbg_trace(xhci, trace_xhci_dbg_init,
				"xHCI doesn't need link TRB QUIRK");
	}
	retval = xhci_mem_init(xhci, GFP_KERNEL);
	xhci_dbg_trace(xhci, trace_xhci_dbg_init, "Finished xhci_init");

	/* Initializing Compliance Mode Recovery Data If Needed */
	if (xhci_compliance_mode_recovery_timer_quirk_check()) {
		xhci->quirks |= XHCI_COMP_MODE_QUIRK;
		compliance_mode_recovery_timer_init(xhci);
	}

	return retval;
}

/*-------------------------------------------------------------------------*/


static int xhci_run_finished(struct xhci_hcd *xhci)
{
	if (xhci_start(xhci)) {
		xhci_halt(xhci);
		return -ENODEV;
	}
	xhci->shared_hcd->state = HC_STATE_RUNNING;
	xhci->cmd_ring_state = CMD_RING_STATE_RUNNING;

	if (xhci->quirks & XHCI_NEC_HOST)
		xhci_ring_cmd_db(xhci);

	xhci_dbg_trace(xhci, trace_xhci_dbg_init,
			"Finished xhci_run for USB3 roothub");
	return 0;
}

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
	int ret;
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);

	/* Start the xHCI host controller running only after the USB 2.0 roothub
	 * is setup.
	 */

	hcd->uses_new_polling = 1;
	if (!usb_hcd_is_primary_hcd(hcd))
		return xhci_run_finished(xhci);

	xhci_dbg_trace(xhci, trace_xhci_dbg_init, "xhci_run");

	ret = xhci_try_enable_msi(hcd);
	if (ret)
		return ret;

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
	xhci_dbg_trace(xhci, trace_xhci_dbg_init,
			"ERST deq = 64'h%0lx", (long unsigned int) temp_64);

	xhci_dbg_trace(xhci, trace_xhci_dbg_init,
			"// Set the interrupt modulation register");
	temp = readl(&xhci->ir_set->irq_control);
	temp &= ~ER_IRQ_INTERVAL_MASK;
	temp |= (u32) 160;
	writel(temp, &xhci->ir_set->irq_control);

	/* Set the HCD state before we enable the irqs */
	temp = readl(&xhci->op_regs->command);
	temp |= (CMD_EIE);
	xhci_dbg_trace(xhci, trace_xhci_dbg_init,
			"// Enable interrupts, cmd = 0x%x.", temp);
	writel(temp, &xhci->op_regs->command);

	temp = readl(&xhci->ir_set->irq_pending);
	xhci_dbg_trace(xhci, trace_xhci_dbg_init,
			"// Enabling event ring interrupter %p by writing 0x%x to irq_pending",
			xhci->ir_set, (unsigned int) ER_IRQ_ENABLE(temp));
	writel(ER_IRQ_ENABLE(temp), &xhci->ir_set->irq_pending);
	xhci_print_ir_set(xhci, 0);

	if (xhci->quirks & XHCI_NEC_HOST) {
		struct xhci_command *command;
		command = xhci_alloc_command(xhci, false, false, GFP_KERNEL);
		if (!command)
			return -ENOMEM;
		xhci_queue_vendor_command(xhci, command, 0, 0, 0,
				TRB_TYPE(TRB_NEC_GET_FW));
	}
	xhci_dbg_trace(xhci, trace_xhci_dbg_init,
			"Finished xhci_run for USB2 roothub");
	return 0;
}
EXPORT_SYMBOL_GPL(xhci_run);

static void xhci_only_stop_hcd(struct usb_hcd *hcd)
{
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);

	spin_lock_irq(&xhci->lock);
	xhci_halt(xhci);

	/* The shared_hcd is going to be deallocated shortly (the USB core only
	 * calls this function when allocation fails in usb_add_hcd(), or
	 * usb_remove_hcd() is called).  So we need to unset xHCI's pointer.
	 */
	xhci->shared_hcd = NULL;
	spin_unlock_irq(&xhci->lock);
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

	if (!usb_hcd_is_primary_hcd(hcd)) {
		xhci_only_stop_hcd(xhci->shared_hcd);
		return;
	}

	spin_lock_irq(&xhci->lock);
	/* Make sure the xHC is halted for a USB3 roothub
	 * (xhci_stop() could be called as part of failed init).
	 */
	xhci_halt(xhci);
	xhci_reset(xhci);
	spin_unlock_irq(&xhci->lock);

	xhci_cleanup_msix(xhci);

	/* Deleting Compliance Mode Recovery Timer */
	if ((xhci->quirks & XHCI_COMP_MODE_QUIRK) &&
			(!(xhci_all_ports_seen_u0(xhci)))) {
		del_timer_sync(&xhci->comp_mode_recovery_timer);
		xhci_dbg_trace(xhci, trace_xhci_dbg_quirks,
				"%s: compliance mode recovery timer deleted",
				__func__);
	}

	if (xhci->quirks & XHCI_AMD_PLL_FIX)
		usb_amd_dev_put();

	xhci_dbg_trace(xhci, trace_xhci_dbg_init,
			"// Disabling event ring interrupts");
	temp = readl(&xhci->op_regs->status);
	writel(temp & ~STS_EINT, &xhci->op_regs->status);
	temp = readl(&xhci->ir_set->irq_pending);
	writel(ER_IRQ_DISABLE(temp), &xhci->ir_set->irq_pending);
	xhci_print_ir_set(xhci, 0);

	xhci_dbg_trace(xhci, trace_xhci_dbg_init, "cleaning up memory");
	xhci_mem_cleanup(xhci);
	xhci_dbg_trace(xhci, trace_xhci_dbg_init,
			"xhci_stop completed - status = %x",
			readl(&xhci->op_regs->status));
}

/*
 * Shutdown HC (not bus-specific)
 *
 * This is called when the machine is rebooting or halting.  We assume that the
 * machine will be powered off, and the HC's internal state will be reset.
 * Don't bother to free memory.
 *
 * This will only ever be called with the main usb_hcd (the USB3 roothub).
 */
void xhci_shutdown(struct usb_hcd *hcd)
{
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);

	if (xhci->quirks & XHCI_SPURIOUS_REBOOT)
		usb_disable_xhci_ports(to_pci_dev(hcd->self.controller));

	spin_lock_irq(&xhci->lock);
	xhci_halt(xhci);
	/* Workaround for spurious wakeups at shutdown with HSW */
	if (xhci->quirks & XHCI_SPURIOUS_WAKEUP)
		xhci_reset(xhci);
	spin_unlock_irq(&xhci->lock);

	xhci_cleanup_msix(xhci);

	xhci_dbg_trace(xhci, trace_xhci_dbg_init,
			"xhci_shutdown completed - status = %x",
			readl(&xhci->op_regs->status));

	/* Yet another workaround for spurious wakeups at shutdown with HSW */
	if (xhci->quirks & XHCI_SPURIOUS_WAKEUP)
		pci_set_power_state(to_pci_dev(hcd->self.controller), PCI_D3hot);
}

#ifdef CONFIG_PM
static void xhci_save_registers(struct xhci_hcd *xhci)
{
	xhci->s3.command = readl(&xhci->op_regs->command);
	xhci->s3.dev_nt = readl(&xhci->op_regs->dev_notification);
	xhci->s3.dcbaa_ptr = xhci_read_64(xhci, &xhci->op_regs->dcbaa_ptr);
	xhci->s3.config_reg = readl(&xhci->op_regs->config_reg);
	xhci->s3.erst_size = readl(&xhci->ir_set->erst_size);
	xhci->s3.erst_base = xhci_read_64(xhci, &xhci->ir_set->erst_base);
	xhci->s3.erst_dequeue = xhci_read_64(xhci, &xhci->ir_set->erst_dequeue);
	xhci->s3.irq_pending = readl(&xhci->ir_set->irq_pending);
	xhci->s3.irq_control = readl(&xhci->ir_set->irq_control);
}

static void xhci_restore_registers(struct xhci_hcd *xhci)
{
	writel(xhci->s3.command, &xhci->op_regs->command);
	writel(xhci->s3.dev_nt, &xhci->op_regs->dev_notification);
	xhci_write_64(xhci, xhci->s3.dcbaa_ptr, &xhci->op_regs->dcbaa_ptr);
	writel(xhci->s3.config_reg, &xhci->op_regs->config_reg);
	writel(xhci->s3.erst_size, &xhci->ir_set->erst_size);
	xhci_write_64(xhci, xhci->s3.erst_base, &xhci->ir_set->erst_base);
	xhci_write_64(xhci, xhci->s3.erst_dequeue, &xhci->ir_set->erst_dequeue);
	writel(xhci->s3.irq_pending, &xhci->ir_set->irq_pending);
	writel(xhci->s3.irq_control, &xhci->ir_set->irq_control);
}

static void xhci_set_cmd_ring_deq(struct xhci_hcd *xhci)
{
	u64	val_64;

	/* step 2: initialize command ring buffer */
	val_64 = xhci_read_64(xhci, &xhci->op_regs->cmd_ring);
	val_64 = (val_64 & (u64) CMD_RING_RSVD_BITS) |
		(xhci_trb_virt_to_dma(xhci->cmd_ring->deq_seg,
				      xhci->cmd_ring->dequeue) &
		 (u64) ~CMD_RING_RSVD_BITS) |
		xhci->cmd_ring->cycle_state;
	xhci_dbg_trace(xhci, trace_xhci_dbg_init,
			"// Setting command ring address to 0x%llx",
			(long unsigned long) val_64);
	xhci_write_64(xhci, val_64, &xhci->op_regs->cmd_ring);
}

/*
 * The whole command ring must be cleared to zero when we suspend the host.
 *
 * The host doesn't save the command ring pointer in the suspend well, so we
 * need to re-program it on resume.  Unfortunately, the pointer must be 64-byte
 * aligned, because of the reserved bits in the command ring dequeue pointer
 * register.  Therefore, we can't just set the dequeue pointer back in the
 * middle of the ring (TRBs are 16-byte aligned).
 */
static void xhci_clear_command_ring(struct xhci_hcd *xhci)
{
	struct xhci_ring *ring;
	struct xhci_segment *seg;

	ring = xhci->cmd_ring;
	seg = ring->deq_seg;
	do {
		memset(seg->trbs, 0,
			sizeof(union xhci_trb) * (TRBS_PER_SEGMENT - 1));
		seg->trbs[TRBS_PER_SEGMENT - 1].link.control &=
			cpu_to_le32(~TRB_CYCLE);
		seg = seg->next;
	} while (seg != ring->deq_seg);

	/* Reset the software enqueue and dequeue pointers */
	ring->deq_seg = ring->first_seg;
	ring->dequeue = ring->first_seg->trbs;
	ring->enq_seg = ring->deq_seg;
	ring->enqueue = ring->dequeue;

	ring->num_trbs_free = ring->num_segs * (TRBS_PER_SEGMENT - 1) - 1;
	/*
	 * Ring is now zeroed, so the HW should look for change of ownership
	 * when the cycle bit is set to 1.
	 */
	ring->cycle_state = 1;

	/*
	 * Reset the hardware dequeue pointer.
	 * Yes, this will need to be re-written after resume, but we're paranoid
	 * and want to make sure the hardware doesn't access bogus memory
	 * because, say, the BIOS or an SMI started the host without changing
	 * the command ring pointers.
	 */
	xhci_set_cmd_ring_deq(xhci);
}

static void xhci_disable_port_wake_on_bits(struct xhci_hcd *xhci)
{
	int port_index;
	__le32 __iomem **port_array;
	unsigned long flags;
	u32 t1, t2;

	spin_lock_irqsave(&xhci->lock, flags);

	/* disble usb3 ports Wake bits*/
	port_index = xhci->num_usb3_ports;
	port_array = xhci->usb3_ports;
	while (port_index--) {
		t1 = readl(port_array[port_index]);
		t1 = xhci_port_state_to_neutral(t1);
		t2 = t1 & ~PORT_WAKE_BITS;
		if (t1 != t2)
			writel(t2, port_array[port_index]);
	}

	/* disble usb2 ports Wake bits*/
	port_index = xhci->num_usb2_ports;
	port_array = xhci->usb2_ports;
	while (port_index--) {
		t1 = readl(port_array[port_index]);
		t1 = xhci_port_state_to_neutral(t1);
		t2 = t1 & ~PORT_WAKE_BITS;
		if (t1 != t2)
			writel(t2, port_array[port_index]);
	}

	spin_unlock_irqrestore(&xhci->lock, flags);
}

/*
 * Stop HC (not bus-specific)
 *
 * This is called when the machine transition into S3/S4 mode.
 *
 */
int xhci_suspend(struct xhci_hcd *xhci, bool do_wakeup)
{
	int			rc = 0;
	unsigned int		delay = XHCI_MAX_HALT_USEC;
	struct usb_hcd		*hcd = xhci_to_hcd(xhci);
	u32			command;

	if (hcd->state != HC_STATE_SUSPENDED ||
			xhci->shared_hcd->state != HC_STATE_SUSPENDED)
		return -EINVAL;

	/* Clear root port wake on bits if wakeup not allowed. */
	if (!do_wakeup)
		xhci_disable_port_wake_on_bits(xhci);

	/* Don't poll the roothubs on bus suspend. */
	xhci_dbg(xhci, "%s: stopping port polling.\n", __func__);
	clear_bit(HCD_FLAG_POLL_RH, &hcd->flags);
	del_timer_sync(&hcd->rh_timer);
	clear_bit(HCD_FLAG_POLL_RH, &xhci->shared_hcd->flags);
	del_timer_sync(&xhci->shared_hcd->rh_timer);

	spin_lock_irq(&xhci->lock);
	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &xhci->shared_hcd->flags);
	/* step 1: stop endpoint */
	/* skipped assuming that port suspend has done */

	/* step 2: clear Run/Stop bit */
	command = readl(&xhci->op_regs->command);
	command &= ~CMD_RUN;
	writel(command, &xhci->op_regs->command);

	/* Some chips from Fresco Logic need an extraordinary delay */
	delay *= (xhci->quirks & XHCI_SLOW_SUSPEND) ? 10 : 1;

	if (xhci_handshake(xhci, &xhci->op_regs->status,
		      STS_HALT, STS_HALT, delay)) {
		xhci_warn(xhci, "WARN: xHC CMD_RUN timeout\n");
		spin_unlock_irq(&xhci->lock);
		return -ETIMEDOUT;
	}
	xhci_clear_command_ring(xhci);

	/* step 3: save registers */
	xhci_save_registers(xhci);

	/* step 4: set CSS flag */
	command = readl(&xhci->op_regs->command);
	command |= CMD_CSS;
	writel(command, &xhci->op_regs->command);
	if (xhci_handshake(xhci, &xhci->op_regs->status,
				STS_SAVE, 0, 10 * 1000)) {
		xhci_warn(xhci, "WARN: xHC save state timeout\n");
		spin_unlock_irq(&xhci->lock);
		return -ETIMEDOUT;
	}
	spin_unlock_irq(&xhci->lock);

	/*
	 * Deleting Compliance Mode Recovery Timer because the xHCI Host
	 * is about to be suspended.
	 */
	if ((xhci->quirks & XHCI_COMP_MODE_QUIRK) &&
			(!(xhci_all_ports_seen_u0(xhci)))) {
		del_timer_sync(&xhci->comp_mode_recovery_timer);
		xhci_dbg_trace(xhci, trace_xhci_dbg_quirks,
				"%s: compliance mode recovery timer deleted",
				__func__);
	}

	/* step 5: remove core well power */
	/* synchronize irq when using MSI-X */
	xhci_msix_sync_irqs(xhci);

	return rc;
}
EXPORT_SYMBOL_GPL(xhci_suspend);

/*
 * start xHC (not bus-specific)
 *
 * This is called when the machine transition from S3/S4 mode.
 *
 */
int xhci_resume(struct xhci_hcd *xhci, bool hibernated)
{
	u32			command, temp = 0, status;
	struct usb_hcd		*hcd = xhci_to_hcd(xhci);
	struct usb_hcd		*secondary_hcd;
	int			retval = 0;
	bool			comp_timer_running = false;

	/* Wait a bit if either of the roothubs need to settle from the
	 * transition into bus suspend.
	 */
	if (time_before(jiffies, xhci->bus_state[0].next_statechange) ||
			time_before(jiffies,
				xhci->bus_state[1].next_statechange))
		msleep(100);

	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	set_bit(HCD_FLAG_HW_ACCESSIBLE, &xhci->shared_hcd->flags);

	spin_lock_irq(&xhci->lock);
	if (xhci->quirks & XHCI_RESET_ON_RESUME)
		hibernated = true;

	if (!hibernated) {
		/* step 1: restore register */
		xhci_restore_registers(xhci);
		/* step 2: initialize command ring buffer */
		xhci_set_cmd_ring_deq(xhci);
		/* step 3: restore state and start state*/
		/* step 3: set CRS flag */
		command = readl(&xhci->op_regs->command);
		command |= CMD_CRS;
		writel(command, &xhci->op_regs->command);
		if (xhci_handshake(xhci, &xhci->op_regs->status,
			      STS_RESTORE, 0, 10 * 1000)) {
			xhci_warn(xhci, "WARN: xHC restore state timeout\n");
			spin_unlock_irq(&xhci->lock);
			return -ETIMEDOUT;
		}
		temp = readl(&xhci->op_regs->status);
	}

	/* If restore operation fails, re-initialize the HC during resume */
	if ((temp & STS_SRE) || hibernated) {

		if ((xhci->quirks & XHCI_COMP_MODE_QUIRK) &&
				!(xhci_all_ports_seen_u0(xhci))) {
			del_timer_sync(&xhci->comp_mode_recovery_timer);
			xhci_dbg_trace(xhci, trace_xhci_dbg_quirks,
				"Compliance Mode Recovery Timer deleted!");
		}

		/* Let the USB core know _both_ roothubs lost power. */
		usb_root_hub_lost_power(xhci->main_hcd->self.root_hub);
		usb_root_hub_lost_power(xhci->shared_hcd->self.root_hub);

		xhci_dbg(xhci, "Stop HCD\n");
		xhci_halt(xhci);
		xhci_reset(xhci);
		spin_unlock_irq(&xhci->lock);
		xhci_cleanup_msix(xhci);

		xhci_dbg(xhci, "// Disabling event ring interrupts\n");
		temp = readl(&xhci->op_regs->status);
		writel(temp & ~STS_EINT, &xhci->op_regs->status);
		temp = readl(&xhci->ir_set->irq_pending);
		writel(ER_IRQ_DISABLE(temp), &xhci->ir_set->irq_pending);
		xhci_print_ir_set(xhci, 0);

		xhci_dbg(xhci, "cleaning up memory\n");
		xhci_mem_cleanup(xhci);
		xhci_dbg(xhci, "xhci_stop completed - status = %x\n",
			    readl(&xhci->op_regs->status));

		/* USB core calls the PCI reinit and start functions twice:
		 * first with the primary HCD, and then with the secondary HCD.
		 * If we don't do the same, the host will never be started.
		 */
		if (!usb_hcd_is_primary_hcd(hcd))
			secondary_hcd = hcd;
		else
			secondary_hcd = xhci->shared_hcd;

		xhci_dbg(xhci, "Initialize the xhci_hcd\n");
		retval = xhci_init(hcd->primary_hcd);
		if (retval)
			return retval;
		comp_timer_running = true;

		xhci_dbg(xhci, "Start the primary HCD\n");
		retval = xhci_run(hcd->primary_hcd);
		if (!retval) {
			xhci_dbg(xhci, "Start the secondary HCD\n");
			retval = xhci_run(secondary_hcd);
		}
		hcd->state = HC_STATE_SUSPENDED;
		xhci->shared_hcd->state = HC_STATE_SUSPENDED;
		goto done;
	}

	/* step 4: set Run/Stop bit */
	command = readl(&xhci->op_regs->command);
	command |= CMD_RUN;
	writel(command, &xhci->op_regs->command);
	xhci_handshake(xhci, &xhci->op_regs->status, STS_HALT,
		  0, 250 * 1000);

	/* step 5: walk topology and initialize portsc,
	 * portpmsc and portli
	 */
	/* this is done in bus_resume */

	/* step 6: restart each of the previously
	 * Running endpoints by ringing their doorbells
	 */

	spin_unlock_irq(&xhci->lock);

 done:
	if (retval == 0) {
		/* Resume root hubs only when have pending events. */
		status = readl(&xhci->op_regs->status);
		if (status & STS_EINT) {
			usb_hcd_resume_root_hub(hcd);
			usb_hcd_resume_root_hub(xhci->shared_hcd);
		}
	}

	/*
	 * If system is subject to the Quirk, Compliance Mode Timer needs to
	 * be re-initialized Always after a system resume. Ports are subject
	 * to suffer the Compliance Mode issue again. It doesn't matter if
	 * ports have entered previously to U0 before system's suspension.
	 */
	if ((xhci->quirks & XHCI_COMP_MODE_QUIRK) && !comp_timer_running)
		compliance_mode_recovery_timer_init(xhci);

	/* Re-enable port polling. */
	xhci_dbg(xhci, "%s: starting port polling.\n", __func__);
	set_bit(HCD_FLAG_POLL_RH, &hcd->flags);
	usb_hcd_poll_rh_status(hcd);
	set_bit(HCD_FLAG_POLL_RH, &xhci->shared_hcd->flags);
	usb_hcd_poll_rh_status(xhci->shared_hcd);

	return retval;
}
EXPORT_SYMBOL_GPL(xhci_resume);
#endif	/* CONFIG_PM */

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

/* The reverse operation to xhci_get_endpoint_index. Calculate the USB endpoint
 * address from the XHCI endpoint index.
 */
unsigned int xhci_get_endpoint_address(unsigned int ep_index)
{
	unsigned int number = DIV_ROUND_UP(ep_index, 2);
	unsigned int direction = ep_index % 2 ? USB_DIR_OUT : USB_DIR_IN;
	return direction | number;
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
static int xhci_check_args(struct usb_hcd *hcd, struct usb_device *udev,
		struct usb_host_endpoint *ep, int check_ep, bool check_virt_dev,
		const char *func) {
	struct xhci_hcd	*xhci;
	struct xhci_virt_device	*virt_dev;

	if (!hcd || (check_ep && !ep) || !udev) {
		pr_debug("xHCI %s called with invalid args\n", func);
		return -EINVAL;
	}
	if (!udev->parent) {
		pr_debug("xHCI %s called for root hub\n", func);
		return 0;
	}

	xhci = hcd_to_xhci(hcd);
	if (check_virt_dev) {
		if (!udev->slot_id || !xhci->devs[udev->slot_id]) {
			xhci_dbg(xhci, "xHCI %s called with unaddressed device\n",
					func);
			return -EINVAL;
		}

		virt_dev = xhci->devs[udev->slot_id];
		if (virt_dev->udev != udev) {
			xhci_dbg(xhci, "xHCI %s called with udev and "
					  "virt_dev does not match\n", func);
			return -EINVAL;
		}
	}

	if (xhci->xhc_state & XHCI_STATE_HALTED)
		return -ENODEV;

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
	struct xhci_container_ctx *out_ctx;
	struct xhci_input_control_ctx *ctrl_ctx;
	struct xhci_ep_ctx *ep_ctx;
	struct xhci_command *command;
	int max_packet_size;
	int hw_max_packet_size;
	int ret = 0;

	out_ctx = xhci->devs[slot_id]->out_ctx;
	ep_ctx = xhci_get_ep_ctx(xhci, out_ctx, ep_index);
	hw_max_packet_size = MAX_PACKET_DECODED(le32_to_cpu(ep_ctx->ep_info2));
	max_packet_size = usb_endpoint_maxp(&urb->dev->ep0.desc);
	if (hw_max_packet_size != max_packet_size) {
		xhci_dbg_trace(xhci,  trace_xhci_dbg_context_change,
				"Max Packet Size for ep 0 changed.");
		xhci_dbg_trace(xhci,  trace_xhci_dbg_context_change,
				"Max packet size in usb_device = %d",
				max_packet_size);
		xhci_dbg_trace(xhci,  trace_xhci_dbg_context_change,
				"Max packet size in xHCI HW = %d",
				hw_max_packet_size);
		xhci_dbg_trace(xhci,  trace_xhci_dbg_context_change,
				"Issuing evaluate context command.");

		/* Set up the input context flags for the command */
		/* FIXME: This won't work if a non-default control endpoint
		 * changes max packet sizes.
		 */

		command = xhci_alloc_command(xhci, false, true, GFP_KERNEL);
		if (!command)
			return -ENOMEM;

		command->in_ctx = xhci->devs[slot_id]->in_ctx;
		ctrl_ctx = xhci_get_input_control_ctx(xhci, command->in_ctx);
		if (!ctrl_ctx) {
			xhci_warn(xhci, "%s: Could not get input context, bad type.\n",
					__func__);
			ret = -ENOMEM;
			goto command_cleanup;
		}
		/* Set up the modified control endpoint 0 */
		xhci_endpoint_copy(xhci, xhci->devs[slot_id]->in_ctx,
				xhci->devs[slot_id]->out_ctx, ep_index);

		ep_ctx = xhci_get_ep_ctx(xhci, command->in_ctx, ep_index);
		ep_ctx->ep_info2 &= cpu_to_le32(~MAX_PACKET_MASK);
		ep_ctx->ep_info2 |= cpu_to_le32(MAX_PACKET(max_packet_size));

		ctrl_ctx->add_flags = cpu_to_le32(EP0_FLAG);
		ctrl_ctx->drop_flags = 0;

		xhci_dbg(xhci, "Slot %d input context\n", slot_id);
		xhci_dbg_ctx(xhci, command->in_ctx, ep_index);
		xhci_dbg(xhci, "Slot %d output context\n", slot_id);
		xhci_dbg_ctx(xhci, out_ctx, ep_index);

		ret = xhci_configure_endpoint(xhci, urb->dev, command,
				true, false);

		/* Clean up the input context for later use by bandwidth
		 * functions.
		 */
		ctrl_ctx->add_flags = cpu_to_le32(SLOT_FLAG);
command_cleanup:
		kfree(command->completion);
		kfree(command);
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
	struct xhci_td *buffer;
	unsigned long flags;
	int ret = 0;
	unsigned int slot_id, ep_index;
	struct urb_priv	*urb_priv;
	int size, i;

	if (!urb || xhci_check_args(hcd, urb->dev, urb->ep,
					true, true, __func__) <= 0)
		return -EINVAL;

	slot_id = urb->dev->slot_id;
	ep_index = xhci_get_endpoint_index(&urb->ep->desc);

	if (!HCD_HW_ACCESSIBLE(hcd)) {
		if (!in_interrupt())
			xhci_dbg(xhci, "urb submitted during PCI suspend\n");
		ret = -ESHUTDOWN;
		goto exit;
	}

	if (usb_endpoint_xfer_isoc(&urb->ep->desc))
		size = urb->number_of_packets;
	else
		size = 1;

	urb_priv = kzalloc(sizeof(struct urb_priv) +
				  size * sizeof(struct xhci_td *), mem_flags);
	if (!urb_priv)
		return -ENOMEM;

	buffer = kzalloc(size * sizeof(struct xhci_td), mem_flags);
	if (!buffer) {
		kfree(urb_priv);
		return -ENOMEM;
	}

	for (i = 0; i < size; i++) {
		urb_priv->td[i] = buffer;
		buffer++;
	}

	urb_priv->length = size;
	urb_priv->td_cnt = 0;
	urb->hcpriv = urb_priv;

	if (usb_endpoint_xfer_control(&urb->ep->desc)) {
		/* Check to see if the max packet size for the default control
		 * endpoint changed during FS device enumeration
		 */
		if (urb->dev->speed == USB_SPEED_FULL) {
			ret = xhci_check_maxpacket(xhci, slot_id,
					ep_index, urb);
			if (ret < 0) {
				xhci_urb_free_priv(xhci, urb_priv);
				urb->hcpriv = NULL;
				return ret;
			}
		}

		/* We have a spinlock and interrupts disabled, so we must pass
		 * atomic context to this function, which may allocate memory.
		 */
		spin_lock_irqsave(&xhci->lock, flags);
		if (xhci->xhc_state & XHCI_STATE_DYING)
			goto dying;
		ret = xhci_queue_ctrl_tx(xhci, GFP_ATOMIC, urb,
				slot_id, ep_index);
		if (ret)
			goto free_priv;
		spin_unlock_irqrestore(&xhci->lock, flags);
	} else if (usb_endpoint_xfer_bulk(&urb->ep->desc)) {
		spin_lock_irqsave(&xhci->lock, flags);
		if (xhci->xhc_state & XHCI_STATE_DYING)
			goto dying;
		if (xhci->devs[slot_id]->eps[ep_index].ep_state &
				EP_GETTING_STREAMS) {
			xhci_warn(xhci, "WARN: Can't enqueue URB while bulk ep "
					"is transitioning to using streams.\n");
			ret = -EINVAL;
		} else if (xhci->devs[slot_id]->eps[ep_index].ep_state &
				EP_GETTING_NO_STREAMS) {
			xhci_warn(xhci, "WARN: Can't enqueue URB while bulk ep "
					"is transitioning to "
					"not having streams.\n");
			ret = -EINVAL;
		} else {
			ret = xhci_queue_bulk_tx(xhci, GFP_ATOMIC, urb,
					slot_id, ep_index);
		}
		if (ret)
			goto free_priv;
		spin_unlock_irqrestore(&xhci->lock, flags);
	} else if (usb_endpoint_xfer_int(&urb->ep->desc)) {
		spin_lock_irqsave(&xhci->lock, flags);
		if (xhci->xhc_state & XHCI_STATE_DYING)
			goto dying;
		ret = xhci_queue_intr_tx(xhci, GFP_ATOMIC, urb,
				slot_id, ep_index);
		if (ret)
			goto free_priv;
		spin_unlock_irqrestore(&xhci->lock, flags);
	} else {
		spin_lock_irqsave(&xhci->lock, flags);
		if (xhci->xhc_state & XHCI_STATE_DYING)
			goto dying;
		ret = xhci_queue_isoc_tx_prepare(xhci, GFP_ATOMIC, urb,
				slot_id, ep_index);
		if (ret)
			goto free_priv;
		spin_unlock_irqrestore(&xhci->lock, flags);
	}
exit:
	return ret;
dying:
	xhci_dbg(xhci, "Ep 0x%x: URB %p submitted for "
			"non-responsive xHCI host.\n",
			urb->ep->desc.bEndpointAddress, urb);
	ret = -ESHUTDOWN;
free_priv:
	xhci_urb_free_priv(xhci, urb_priv);
	urb->hcpriv = NULL;
	spin_unlock_irqrestore(&xhci->lock, flags);
	return ret;
}

/* Get the right ring for the given URB.
 * If the endpoint supports streams, boundary check the URB's stream ID.
 * If the endpoint doesn't support streams, return the singular endpoint ring.
 */
static struct xhci_ring *xhci_urb_to_transfer_ring(struct xhci_hcd *xhci,
		struct urb *urb)
{
	unsigned int slot_id;
	unsigned int ep_index;
	unsigned int stream_id;
	struct xhci_virt_ep *ep;

	slot_id = urb->dev->slot_id;
	ep_index = xhci_get_endpoint_index(&urb->ep->desc);
	stream_id = urb->stream_id;
	ep = &xhci->devs[slot_id]->eps[ep_index];
	/* Common case: no streams */
	if (!(ep->ep_state & EP_HAS_STREAMS))
		return ep->ring;

	if (stream_id == 0) {
		xhci_warn(xhci,
				"WARN: Slot ID %u, ep index %u has streams, "
				"but URB has no stream ID.\n",
				slot_id, ep_index);
		return NULL;
	}

	if (stream_id < ep->stream_info->num_streams)
		return ep->stream_info->stream_rings[stream_id];

	xhci_warn(xhci,
			"WARN: Slot ID %u, ep index %u has "
			"stream IDs 1 to %u allocated, "
			"but stream ID %u is requested.\n",
			slot_id, ep_index,
			ep->stream_info->num_streams - 1,
			stream_id);
	return NULL;
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
	int ret, i;
	u32 temp;
	struct xhci_hcd *xhci;
	struct urb_priv	*urb_priv;
	struct xhci_td *td;
	unsigned int ep_index;
	struct xhci_ring *ep_ring;
	struct xhci_virt_ep *ep;
	struct xhci_command *command;

	xhci = hcd_to_xhci(hcd);
	spin_lock_irqsave(&xhci->lock, flags);
	/* Make sure the URB hasn't completed or been unlinked already */
	ret = usb_hcd_check_unlink_urb(hcd, urb, status);
	if (ret || !urb->hcpriv)
		goto done;
	temp = readl(&xhci->op_regs->status);
	if (temp == 0xffffffff || (xhci->xhc_state & XHCI_STATE_HALTED)) {
		xhci_dbg_trace(xhci, trace_xhci_dbg_cancel_urb,
				"HW died, freeing TD.");
		urb_priv = urb->hcpriv;
		for (i = urb_priv->td_cnt; i < urb_priv->length; i++) {
			td = urb_priv->td[i];
			if (!list_empty(&td->td_list))
				list_del_init(&td->td_list);
			if (!list_empty(&td->cancelled_td_list))
				list_del_init(&td->cancelled_td_list);
		}

		usb_hcd_unlink_urb_from_ep(hcd, urb);
		spin_unlock_irqrestore(&xhci->lock, flags);
		usb_hcd_giveback_urb(hcd, urb, -ESHUTDOWN);
		xhci_urb_free_priv(xhci, urb_priv);
		return ret;
	}
	if ((xhci->xhc_state & XHCI_STATE_DYING) ||
			(xhci->xhc_state & XHCI_STATE_HALTED)) {
		xhci_dbg_trace(xhci, trace_xhci_dbg_cancel_urb,
				"Ep 0x%x: URB %p to be canceled on "
				"non-responsive xHCI host.",
				urb->ep->desc.bEndpointAddress, urb);
		/* Let the stop endpoint command watchdog timer (which set this
		 * state) finish cleaning up the endpoint TD lists.  We must
		 * have caught it in the middle of dropping a lock and giving
		 * back an URB.
		 */
		goto done;
	}

	ep_index = xhci_get_endpoint_index(&urb->ep->desc);
	ep = &xhci->devs[urb->dev->slot_id]->eps[ep_index];
	ep_ring = xhci_urb_to_transfer_ring(xhci, urb);
	if (!ep_ring) {
		ret = -EINVAL;
		goto done;
	}

	urb_priv = urb->hcpriv;
	i = urb_priv->td_cnt;
	if (i < urb_priv->length)
		xhci_dbg_trace(xhci, trace_xhci_dbg_cancel_urb,
				"Cancel URB %p, dev %s, ep 0x%x, "
				"starting at offset 0x%llx",
				urb, urb->dev->devpath,
				urb->ep->desc.bEndpointAddress,
				(unsigned long long) xhci_trb_virt_to_dma(
					urb_priv->td[i]->start_seg,
					urb_priv->td[i]->first_trb));

	for (; i < urb_priv->length; i++) {
		td = urb_priv->td[i];
		list_add_tail(&td->cancelled_td_list, &ep->cancelled_td_list);
	}

	/* Queue a stop endpoint command, but only if this is
	 * the first cancellation to be handled.
	 */
	if (!(ep->ep_state & EP_HALT_PENDING)) {
		command = xhci_alloc_command(xhci, false, false, GFP_ATOMIC);
		if (!command) {
			ret = -ENOMEM;
			goto done;
		}
		ep->ep_state |= EP_HALT_PENDING;
		ep->stop_cmds_pending++;
		ep->stop_cmd_timer.expires = jiffies +
			XHCI_STOP_EP_CMD_TIMEOUT * HZ;
		add_timer(&ep->stop_cmd_timer);
		xhci_queue_stop_endpoint(xhci, command, urb->dev->slot_id,
					 ep_index, 0);
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
	unsigned int ep_index;
	struct xhci_ep_ctx *ep_ctx;
	u32 drop_flag;
	u32 new_add_flags, new_drop_flags;
	int ret;

	ret = xhci_check_args(hcd, udev, ep, 1, true, __func__);
	if (ret <= 0)
		return ret;
	xhci = hcd_to_xhci(hcd);
	if (xhci->xhc_state & XHCI_STATE_DYING)
		return -ENODEV;

	xhci_dbg(xhci, "%s called for udev %p\n", __func__, udev);
	drop_flag = xhci_get_endpoint_flag(&ep->desc);
	if (drop_flag == SLOT_FLAG || drop_flag == EP0_FLAG) {
		xhci_dbg(xhci, "xHCI %s - can't drop slot or ep 0 %#x\n",
				__func__, drop_flag);
		return 0;
	}

	in_ctx = xhci->devs[udev->slot_id]->in_ctx;
	out_ctx = xhci->devs[udev->slot_id]->out_ctx;
	ctrl_ctx = xhci_get_input_control_ctx(xhci, in_ctx);
	if (!ctrl_ctx) {
		xhci_warn(xhci, "%s: Could not get input context, bad type.\n",
				__func__);
		return 0;
	}

	ep_index = xhci_get_endpoint_index(&ep->desc);
	ep_ctx = xhci_get_ep_ctx(xhci, out_ctx, ep_index);
	/* If the HC already knows the endpoint is disabled,
	 * or the HCD has noted it is disabled, ignore this request
	 */
	if (((ep_ctx->ep_info & cpu_to_le32(EP_STATE_MASK)) ==
	     cpu_to_le32(EP_STATE_DISABLED)) ||
	    le32_to_cpu(ctrl_ctx->drop_flags) &
	    xhci_get_endpoint_flag(&ep->desc)) {
		xhci_warn(xhci, "xHCI %s called with disabled ep %p\n",
				__func__, ep);
		return 0;
	}

	ctrl_ctx->drop_flags |= cpu_to_le32(drop_flag);
	new_drop_flags = le32_to_cpu(ctrl_ctx->drop_flags);

	ctrl_ctx->add_flags &= cpu_to_le32(~drop_flag);
	new_add_flags = le32_to_cpu(ctrl_ctx->add_flags);

	xhci_endpoint_zero(xhci, xhci->devs[udev->slot_id], ep);

	xhci_dbg(xhci, "drop ep 0x%x, slot id %d, new drop flags = %#x, new add flags = %#x\n",
			(unsigned int) ep->desc.bEndpointAddress,
			udev->slot_id,
			(unsigned int) new_drop_flags,
			(unsigned int) new_add_flags);
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
	struct xhci_input_control_ctx *ctrl_ctx;
	u32 added_ctxs;
	u32 new_add_flags, new_drop_flags;
	struct xhci_virt_device *virt_dev;
	int ret = 0;

	ret = xhci_check_args(hcd, udev, ep, 1, true, __func__);
	if (ret <= 0) {
		/* So we won't queue a reset ep command for a root hub */
		ep->hcpriv = NULL;
		return ret;
	}
	xhci = hcd_to_xhci(hcd);
	if (xhci->xhc_state & XHCI_STATE_DYING)
		return -ENODEV;

	added_ctxs = xhci_get_endpoint_flag(&ep->desc);
	if (added_ctxs == SLOT_FLAG || added_ctxs == EP0_FLAG) {
		/* FIXME when we have to issue an evaluate endpoint command to
		 * deal with ep0 max packet size changing once we get the
		 * descriptors
		 */
		xhci_dbg(xhci, "xHCI %s - can't add slot or ep 0 %#x\n",
				__func__, added_ctxs);
		return 0;
	}

	virt_dev = xhci->devs[udev->slot_id];
	in_ctx = virt_dev->in_ctx;
	out_ctx = virt_dev->out_ctx;
	ctrl_ctx = xhci_get_input_control_ctx(xhci, in_ctx);
	if (!ctrl_ctx) {
		xhci_warn(xhci, "%s: Could not get input context, bad type.\n",
				__func__);
		return 0;
	}

	ep_index = xhci_get_endpoint_index(&ep->desc);
	/* If this endpoint is already in use, and the upper layers are trying
	 * to add it again without dropping it, reject the addition.
	 */
	if (virt_dev->eps[ep_index].ring &&
			!(le32_to_cpu(ctrl_ctx->drop_flags) &
				xhci_get_endpoint_flag(&ep->desc))) {
		xhci_warn(xhci, "Trying to add endpoint 0x%x "
				"without dropping it.\n",
				(unsigned int) ep->desc.bEndpointAddress);
		return -EINVAL;
	}

	/* If the HCD has already noted the endpoint is enabled,
	 * ignore this request.
	 */
	if (le32_to_cpu(ctrl_ctx->add_flags) &
	    xhci_get_endpoint_flag(&ep->desc)) {
		xhci_warn(xhci, "xHCI %s called with enabled ep %p\n",
				__func__, ep);
		return 0;
	}

	/*
	 * Configuration and alternate setting changes must be done in
	 * process context, not interrupt context (or so documenation
	 * for usb_set_interface() and usb_set_configuration() claim).
	 */
	if (xhci_endpoint_init(xhci, virt_dev, udev, ep, GFP_NOIO) < 0) {
		dev_dbg(&udev->dev, "%s - could not initialize ep %#x\n",
				__func__, ep->desc.bEndpointAddress);
		return -ENOMEM;
	}

	ctrl_ctx->add_flags |= cpu_to_le32(added_ctxs);
	new_add_flags = le32_to_cpu(ctrl_ctx->add_flags);

	/* If xhci_endpoint_disable() was called for this endpoint, but the
	 * xHC hasn't been notified yet through the check_bandwidth() call,
	 * this re-adds a new state for the endpoint from the new endpoint
	 * descriptors.  We must drop and re-add this endpoint, so we leave the
	 * drop flags alone.
	 */
	new_drop_flags = le32_to_cpu(ctrl_ctx->drop_flags);

	/* Store the usb_device pointer for later use */
	ep->hcpriv = udev;

	xhci_dbg(xhci, "add ep 0x%x, slot id %d, new drop flags = %#x, new add flags = %#x\n",
			(unsigned int) ep->desc.bEndpointAddress,
			udev->slot_id,
			(unsigned int) new_drop_flags,
			(unsigned int) new_add_flags);
	return 0;
}

static void xhci_zero_in_ctx(struct xhci_hcd *xhci, struct xhci_virt_device *virt_dev)
{
	struct xhci_input_control_ctx *ctrl_ctx;
	struct xhci_ep_ctx *ep_ctx;
	struct xhci_slot_ctx *slot_ctx;
	int i;

	ctrl_ctx = xhci_get_input_control_ctx(xhci, virt_dev->in_ctx);
	if (!ctrl_ctx) {
		xhci_warn(xhci, "%s: Could not get input context, bad type.\n",
				__func__);
		return;
	}

	/* When a device's add flag and drop flag are zero, any subsequent
	 * configure endpoint command will leave that endpoint's state
	 * untouched.  Make sure we don't leave any old state in the input
	 * endpoint contexts.
	 */
	ctrl_ctx->drop_flags = 0;
	ctrl_ctx->add_flags = 0;
	slot_ctx = xhci_get_slot_ctx(xhci, virt_dev->in_ctx);
	slot_ctx->dev_info &= cpu_to_le32(~LAST_CTX_MASK);
	/* Endpoint 0 is always valid */
	slot_ctx->dev_info |= cpu_to_le32(LAST_CTX(1));
	for (i = 1; i < 31; ++i) {
		ep_ctx = xhci_get_ep_ctx(xhci, virt_dev->in_ctx, i);
		ep_ctx->ep_info = 0;
		ep_ctx->ep_info2 = 0;
		ep_ctx->deq = 0;
		ep_ctx->tx_info = 0;
	}
}

static int xhci_configure_endpoint_result(struct xhci_hcd *xhci,
		struct usb_device *udev, u32 *cmd_status)
{
	int ret;

	switch (*cmd_status) {
	case COMP_CMD_ABORT:
	case COMP_CMD_STOP:
		xhci_warn(xhci, "Timeout while waiting for configure endpoint command\n");
		ret = -ETIME;
		break;
	case COMP_ENOMEM:
		dev_warn(&udev->dev,
			 "Not enough host controller resources for new device state.\n");
		ret = -ENOMEM;
		/* FIXME: can we allocate more resources for the HC? */
		break;
	case COMP_BW_ERR:
	case COMP_2ND_BW_ERR:
		dev_warn(&udev->dev,
			 "Not enough bandwidth for new device state.\n");
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
	case COMP_DEV_ERR:
		dev_warn(&udev->dev,
			 "ERROR: Incompatible device for endpoint configure command.\n");
		ret = -ENODEV;
		break;
	case COMP_SUCCESS:
		xhci_dbg_trace(xhci, trace_xhci_dbg_context_change,
				"Successful Endpoint Configure command");
		ret = 0;
		break;
	default:
		xhci_err(xhci, "ERROR: unexpected command completion code 0x%x.\n",
				*cmd_status);
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int xhci_evaluate_context_result(struct xhci_hcd *xhci,
		struct usb_device *udev, u32 *cmd_status)
{
	int ret;
	struct xhci_virt_device *virt_dev = xhci->devs[udev->slot_id];

	switch (*cmd_status) {
	case COMP_CMD_ABORT:
	case COMP_CMD_STOP:
		xhci_warn(xhci, "Timeout while waiting for evaluate context command\n");
		ret = -ETIME;
		break;
	case COMP_EINVAL:
		dev_warn(&udev->dev,
			 "WARN: xHCI driver setup invalid evaluate context command.\n");
		ret = -EINVAL;
		break;
	case COMP_EBADSLT:
		dev_warn(&udev->dev,
			"WARN: slot not enabled for evaluate context command.\n");
		ret = -EINVAL;
		break;
	case COMP_CTX_STATE:
		dev_warn(&udev->dev,
			"WARN: invalid context state for evaluate context command.\n");
		xhci_dbg_ctx(xhci, virt_dev->out_ctx, 1);
		ret = -EINVAL;
		break;
	case COMP_DEV_ERR:
		dev_warn(&udev->dev,
			"ERROR: Incompatible device for evaluate context command.\n");
		ret = -ENODEV;
		break;
	case COMP_MEL_ERR:
		/* Max Exit Latency too large error */
		dev_warn(&udev->dev, "WARN: Max Exit Latency too large\n");
		ret = -EINVAL;
		break;
	case COMP_SUCCESS:
		xhci_dbg_trace(xhci, trace_xhci_dbg_context_change,
				"Successful evaluate context command");
		ret = 0;
		break;
	default:
		xhci_err(xhci, "ERROR: unexpected command completion code 0x%x.\n",
			*cmd_status);
		ret = -EINVAL;
		break;
	}
	return ret;
}

static u32 xhci_count_num_new_endpoints(struct xhci_hcd *xhci,
		struct xhci_input_control_ctx *ctrl_ctx)
{
	u32 valid_add_flags;
	u32 valid_drop_flags;

	/* Ignore the slot flag (bit 0), and the default control endpoint flag
	 * (bit 1).  The default control endpoint is added during the Address
	 * Device command and is never removed until the slot is disabled.
	 */
	valid_add_flags = le32_to_cpu(ctrl_ctx->add_flags) >> 2;
	valid_drop_flags = le32_to_cpu(ctrl_ctx->drop_flags) >> 2;

	/* Use hweight32 to count the number of ones in the add flags, or
	 * number of endpoints added.  Don't count endpoints that are changed
	 * (both added and dropped).
	 */
	return hweight32(valid_add_flags) -
		hweight32(valid_add_flags & valid_drop_flags);
}

static unsigned int xhci_count_num_dropped_endpoints(struct xhci_hcd *xhci,
		struct xhci_input_control_ctx *ctrl_ctx)
{
	u32 valid_add_flags;
	u32 valid_drop_flags;

	valid_add_flags = le32_to_cpu(ctrl_ctx->add_flags) >> 2;
	valid_drop_flags = le32_to_cpu(ctrl_ctx->drop_flags) >> 2;

	return hweight32(valid_drop_flags) -
		hweight32(valid_add_flags & valid_drop_flags);
}

/*
 * We need to reserve the new number of endpoints before the configure endpoint
 * command completes.  We can't subtract the dropped endpoints from the number
 * of active endpoints until the command completes because we can oversubscribe
 * the host in this case:
 *
 *  - the first configure endpoint command drops more endpoints than it adds
 *  - a second configure endpoint command that adds more endpoints is queued
 *  - the first configure endpoint command fails, so the config is unchanged
 *  - the second command may succeed, even though there isn't enough resources
 *
 * Must be called with xhci->lock held.
 */
static int xhci_reserve_host_resources(struct xhci_hcd *xhci,
		struct xhci_input_control_ctx *ctrl_ctx)
{
	u32 added_eps;

	added_eps = xhci_count_num_new_endpoints(xhci, ctrl_ctx);
	if (xhci->num_active_eps + added_eps > xhci->limit_active_eps) {
		xhci_dbg_trace(xhci, trace_xhci_dbg_quirks,
				"Not enough ep ctxs: "
				"%u active, need to add %u, limit is %u.",
				xhci->num_active_eps, added_eps,
				xhci->limit_active_eps);
		return -ENOMEM;
	}
	xhci->num_active_eps += added_eps;
	xhci_dbg_trace(xhci, trace_xhci_dbg_quirks,
			"Adding %u ep ctxs, %u now active.", added_eps,
			xhci->num_active_eps);
	return 0;
}

/*
 * The configure endpoint was failed by the xHC for some other reason, so we
 * need to revert the resources that failed configuration would have used.
 *
 * Must be called with xhci->lock held.
 */
static void xhci_free_host_resources(struct xhci_hcd *xhci,
		struct xhci_input_control_ctx *ctrl_ctx)
{
	u32 num_failed_eps;

	num_failed_eps = xhci_count_num_new_endpoints(xhci, ctrl_ctx);
	xhci->num_active_eps -= num_failed_eps;
	xhci_dbg_trace(xhci, trace_xhci_dbg_quirks,
			"Removing %u failed ep ctxs, %u now active.",
			num_failed_eps,
			xhci->num_active_eps);
}

/*
 * Now that the command has completed, clean up the active endpoint count by
 * subtracting out the endpoints that were dropped (but not changed).
 *
 * Must be called with xhci->lock held.
 */
static void xhci_finish_resource_reservation(struct xhci_hcd *xhci,
		struct xhci_input_control_ctx *ctrl_ctx)
{
	u32 num_dropped_eps;

	num_dropped_eps = xhci_count_num_dropped_endpoints(xhci, ctrl_ctx);
	xhci->num_active_eps -= num_dropped_eps;
	if (num_dropped_eps)
		xhci_dbg_trace(xhci, trace_xhci_dbg_quirks,
				"Removing %u dropped ep ctxs, %u now active.",
				num_dropped_eps,
				xhci->num_active_eps);
}

static unsigned int xhci_get_block_size(struct usb_device *udev)
{
	switch (udev->speed) {
	case USB_SPEED_LOW:
	case USB_SPEED_FULL:
		return FS_BLOCK;
	case USB_SPEED_HIGH:
		return HS_BLOCK;
	case USB_SPEED_SUPER:
		return SS_BLOCK;
	case USB_SPEED_UNKNOWN:
	case USB_SPEED_WIRELESS:
	default:
		/* Should never happen */
		return 1;
	}
}

static unsigned int
xhci_get_largest_overhead(struct xhci_interval_bw *interval_bw)
{
	if (interval_bw->overhead[LS_OVERHEAD_TYPE])
		return LS_OVERHEAD;
	if (interval_bw->overhead[FS_OVERHEAD_TYPE])
		return FS_OVERHEAD;
	return HS_OVERHEAD;
}

/* If we are changing a LS/FS device under a HS hub,
 * make sure (if we are activating a new TT) that the HS bus has enough
 * bandwidth for this new TT.
 */
static int xhci_check_tt_bw_table(struct xhci_hcd *xhci,
		struct xhci_virt_device *virt_dev,
		int old_active_eps)
{
	struct xhci_interval_bw_table *bw_table;
	struct xhci_tt_bw_info *tt_info;

	/* Find the bandwidth table for the root port this TT is attached to. */
	bw_table = &xhci->rh_bw[virt_dev->real_port - 1].bw_table;
	tt_info = virt_dev->tt_info;
	/* If this TT already had active endpoints, the bandwidth for this TT
	 * has already been added.  Removing all periodic endpoints (and thus
	 * making the TT enactive) will only decrease the bandwidth used.
	 */
	if (old_active_eps)
		return 0;
	if (old_active_eps == 0 && tt_info->active_eps != 0) {
		if (bw_table->bw_used + TT_HS_OVERHEAD > HS_BW_LIMIT)
			return -ENOMEM;
		return 0;
	}
	/* Not sure why we would have no new active endpoints...
	 *
	 * Maybe because of an Evaluate Context change for a hub update or a
	 * control endpoint 0 max packet size change?
	 * FIXME: skip the bandwidth calculation in that case.
	 */
	return 0;
}

static int xhci_check_ss_bw(struct xhci_hcd *xhci,
		struct xhci_virt_device *virt_dev)
{
	unsigned int bw_reserved;

	bw_reserved = DIV_ROUND_UP(SS_BW_RESERVED*SS_BW_LIMIT_IN, 100);
	if (virt_dev->bw_table->ss_bw_in > (SS_BW_LIMIT_IN - bw_reserved))
		return -ENOMEM;

	bw_reserved = DIV_ROUND_UP(SS_BW_RESERVED*SS_BW_LIMIT_OUT, 100);
	if (virt_dev->bw_table->ss_bw_out > (SS_BW_LIMIT_OUT - bw_reserved))
		return -ENOMEM;

	return 0;
}

/*
 * This algorithm is a very conservative estimate of the worst-case scheduling
 * scenario for any one interval.  The hardware dynamically schedules the
 * packets, so we can't tell which microframe could be the limiting factor in
 * the bandwidth scheduling.  This only takes into account periodic endpoints.
 *
 * Obviously, we can't solve an NP complete problem to find the minimum worst
 * case scenario.  Instead, we come up with an estimate that is no less than
 * the worst case bandwidth used for any one microframe, but may be an
 * over-estimate.
 *
 * We walk the requirements for each endpoint by interval, starting with the
 * smallest interval, and place packets in the schedule where there is only one
 * possible way to schedule packets for that interval.  In order to simplify
 * this algorithm, we record the largest max packet size for each interval, and
 * assume all packets will be that size.
 *
 * For interval 0, we obviously must schedule all packets for each interval.
 * The bandwidth for interval 0 is just the amount of data to be transmitted
 * (the sum of all max ESIT payload sizes, plus any overhead per packet times
 * the number of packets).
 *
 * For interval 1, we have two possible microframes to schedule those packets
 * in.  For this algorithm, if we can schedule the same number of packets for
 * each possible scheduling opportunity (each microframe), we will do so.  The
 * remaining number of packets will be saved to be transmitted in the gaps in
 * the next interval's scheduling sequence.
 *
 * As we move those remaining packets to be scheduled with interval 2 packets,
 * we have to double the number of remaining packets to transmit.  This is
 * because the intervals are actually powers of 2, and we would be transmitting
 * the previous interval's packets twice in this interval.  We also have to be
 * sure that when we look at the largest max packet size for this interval, we
 * also look at the largest max packet size for the remaining packets and take
 * the greater of the two.
 *
 * The algorithm continues to evenly distribute packets in each scheduling
 * opportunity, and push the remaining packets out, until we get to the last
 * interval.  Then those packets and their associated overhead are just added
 * to the bandwidth used.
 */
static int xhci_check_bw_table(struct xhci_hcd *xhci,
		struct xhci_virt_device *virt_dev,
		int old_active_eps)
{
	unsigned int bw_reserved;
	unsigned int max_bandwidth;
	unsigned int bw_used;
	unsigned int block_size;
	struct xhci_interval_bw_table *bw_table;
	unsigned int packet_size = 0;
	unsigned int overhead = 0;
	unsigned int packets_transmitted = 0;
	unsigned int packets_remaining = 0;
	unsigned int i;

	if (virt_dev->udev->speed == USB_SPEED_SUPER)
		return xhci_check_ss_bw(xhci, virt_dev);

	if (virt_dev->udev->speed == USB_SPEED_HIGH) {
		max_bandwidth = HS_BW_LIMIT;
		/* Convert percent of bus BW reserved to blocks reserved */
		bw_reserved = DIV_ROUND_UP(HS_BW_RESERVED * max_bandwidth, 100);
	} else {
		max_bandwidth = FS_BW_LIMIT;
		bw_reserved = DIV_ROUND_UP(FS_BW_RESERVED * max_bandwidth, 100);
	}

	bw_table = virt_dev->bw_table;
	/* We need to translate the max packet size and max ESIT payloads into
	 * the units the hardware uses.
	 */
	block_size = xhci_get_block_size(virt_dev->udev);

	/* If we are manipulating a LS/FS device under a HS hub, double check
	 * that the HS bus has enough bandwidth if we are activing a new TT.
	 */
	if (virt_dev->tt_info) {
		xhci_dbg_trace(xhci, trace_xhci_dbg_quirks,
				"Recalculating BW for rootport %u",
				virt_dev->real_port);
		if (xhci_check_tt_bw_table(xhci, virt_dev, old_active_eps)) {
			xhci_warn(xhci, "Not enough bandwidth on HS bus for "
					"newly activated TT.\n");
			return -ENOMEM;
		}
		xhci_dbg_trace(xhci, trace_xhci_dbg_quirks,
				"Recalculating BW for TT slot %u port %u",
				virt_dev->tt_info->slot_id,
				virt_dev->tt_info->ttport);
	} else {
		xhci_dbg_trace(xhci, trace_xhci_dbg_quirks,
				"Recalculating BW for rootport %u",
				virt_dev->real_port);
	}

	/* Add in how much bandwidth will be used for interval zero, or the
	 * rounded max ESIT payload + number of packets * largest overhead.
	 */
	bw_used = DIV_ROUND_UP(bw_table->interval0_esit_payload, block_size) +
		bw_table->interval_bw[0].num_packets *
		xhci_get_largest_overhead(&bw_table->interval_bw[0]);

	for (i = 1; i < XHCI_MAX_INTERVAL; i++) {
		unsigned int bw_added;
		unsigned int largest_mps;
		unsigned int interval_overhead;

		/*
		 * How many packets could we transmit in this interval?
		 * If packets didn't fit in the previous interval, we will need
		 * to transmit that many packets twice within this interval.
		 */
		packets_remaining = 2 * packets_remaining +
			bw_table->interval_bw[i].num_packets;

		/* Find the largest max packet size of this or the previous
		 * interval.
		 */
		if (list_empty(&bw_table->interval_bw[i].endpoints))
			largest_mps = 0;
		else {
			struct xhci_virt_ep *virt_ep;
			struct list_head *ep_entry;

			ep_entry = bw_table->interval_bw[i].endpoints.next;
			virt_ep = list_entry(ep_entry,
					struct xhci_virt_ep, bw_endpoint_list);
			/* Convert to blocks, rounding up */
			largest_mps = DIV_ROUND_UP(
					virt_ep->bw_info.max_packet_size,
					block_size);
		}
		if (largest_mps > packet_size)
			packet_size = largest_mps;

		/* Use the larger overhead of this or the previous interval. */
		interval_overhead = xhci_get_largest_overhead(
				&bw_table->interval_bw[i]);
		if (interval_overhead > overhead)
			overhead = interval_overhead;

		/* How many packets can we evenly distribute across
		 * (1 << (i + 1)) possible scheduling opportunities?
		 */
		packets_transmitted = packets_remaining >> (i + 1);

		/* Add in the bandwidth used for those scheduled packets */
		bw_added = packets_transmitted * (overhead + packet_size);

		/* How many packets do we have remaining to transmit? */
		packets_remaining = packets_remaining % (1 << (i + 1));

		/* What largest max packet size should those packets have? */
		/* If we've transmitted all packets, don't carry over the
		 * largest packet size.
		 */
		if (packets_remaining == 0) {
			packet_size = 0;
			overhead = 0;
		} else if (packets_transmitted > 0) {
			/* Otherwise if we do have remaining packets, and we've
			 * scheduled some packets in this interval, take the
			 * largest max packet size from endpoints with this
			 * interval.
			 */
			packet_size = largest_mps;
			overhead = interval_overhead;
		}
		/* Otherwise carry over packet_size and overhead from the last
		 * time we had a remainder.
		 */
		bw_used += bw_added;
		if (bw_used > max_bandwidth) {
			xhci_warn(xhci, "Not enough bandwidth. "
					"Proposed: %u, Max: %u\n",
				bw_used, max_bandwidth);
			return -ENOMEM;
		}
	}
	/*
	 * Ok, we know we have some packets left over after even-handedly
	 * scheduling interval 15.  We don't know which microframes they will
	 * fit into, so we over-schedule and say they will be scheduled every
	 * microframe.
	 */
	if (packets_remaining > 0)
		bw_used += overhead + packet_size;

	if (!virt_dev->tt_info && virt_dev->udev->speed == USB_SPEED_HIGH) {
		unsigned int port_index = virt_dev->real_port - 1;

		/* OK, we're manipulating a HS device attached to a
		 * root port bandwidth domain.  Include the number of active TTs
		 * in the bandwidth used.
		 */
		bw_used += TT_HS_OVERHEAD *
			xhci->rh_bw[port_index].num_active_tts;
	}

	xhci_dbg_trace(xhci, trace_xhci_dbg_quirks,
		"Final bandwidth: %u, Limit: %u, Reserved: %u, "
		"Available: %u " "percent",
		bw_used, max_bandwidth, bw_reserved,
		(max_bandwidth - bw_used - bw_reserved) * 100 /
		max_bandwidth);

	bw_used += bw_reserved;
	if (bw_used > max_bandwidth) {
		xhci_warn(xhci, "Not enough bandwidth. Proposed: %u, Max: %u\n",
				bw_used, max_bandwidth);
		return -ENOMEM;
	}

	bw_table->bw_used = bw_used;
	return 0;
}

static bool xhci_is_async_ep(unsigned int ep_type)
{
	return (ep_type != ISOC_OUT_EP && ep_type != INT_OUT_EP &&
					ep_type != ISOC_IN_EP &&
					ep_type != INT_IN_EP);
}

static bool xhci_is_sync_in_ep(unsigned int ep_type)
{
	return (ep_type == ISOC_IN_EP || ep_type == INT_IN_EP);
}

static unsigned int xhci_get_ss_bw_consumed(struct xhci_bw_info *ep_bw)
{
	unsigned int mps = DIV_ROUND_UP(ep_bw->max_packet_size, SS_BLOCK);

	if (ep_bw->ep_interval == 0)
		return SS_OVERHEAD_BURST +
			(ep_bw->mult * ep_bw->num_packets *
					(SS_OVERHEAD + mps));
	return DIV_ROUND_UP(ep_bw->mult * ep_bw->num_packets *
				(SS_OVERHEAD + mps + SS_OVERHEAD_BURST),
				1 << ep_bw->ep_interval);

}

void xhci_drop_ep_from_interval_table(struct xhci_hcd *xhci,
		struct xhci_bw_info *ep_bw,
		struct xhci_interval_bw_table *bw_table,
		struct usb_device *udev,
		struct xhci_virt_ep *virt_ep,
		struct xhci_tt_bw_info *tt_info)
{
	struct xhci_interval_bw	*interval_bw;
	int normalized_interval;

	if (xhci_is_async_ep(ep_bw->type))
		return;

	if (udev->speed == USB_SPEED_SUPER) {
		if (xhci_is_sync_in_ep(ep_bw->type))
			xhci->devs[udev->slot_id]->bw_table->ss_bw_in -=
				xhci_get_ss_bw_consumed(ep_bw);
		else
			xhci->devs[udev->slot_id]->bw_table->ss_bw_out -=
				xhci_get_ss_bw_consumed(ep_bw);
		return;
	}

	/* SuperSpeed endpoints never get added to intervals in the table, so
	 * this check is only valid for HS/FS/LS devices.
	 */
	if (list_empty(&virt_ep->bw_endpoint_list))
		return;
	/* For LS/FS devices, we need to translate the interval expressed in
	 * microframes to frames.
	 */
	if (udev->speed == USB_SPEED_HIGH)
		normalized_interval = ep_bw->ep_interval;
	else
		normalized_interval = ep_bw->ep_interval - 3;

	if (normalized_interval == 0)
		bw_table->interval0_esit_payload -= ep_bw->max_esit_payload;
	interval_bw = &bw_table->interval_bw[normalized_interval];
	interval_bw->num_packets -= ep_bw->num_packets;
	switch (udev->speed) {
	case USB_SPEED_LOW:
		interval_bw->overhead[LS_OVERHEAD_TYPE] -= 1;
		break;
	case USB_SPEED_FULL:
		interval_bw->overhead[FS_OVERHEAD_TYPE] -= 1;
		break;
	case USB_SPEED_HIGH:
		interval_bw->overhead[HS_OVERHEAD_TYPE] -= 1;
		break;
	case USB_SPEED_SUPER:
	case USB_SPEED_UNKNOWN:
	case USB_SPEED_WIRELESS:
		/* Should never happen because only LS/FS/HS endpoints will get
		 * added to the endpoint list.
		 */
		return;
	}
	if (tt_info)
		tt_info->active_eps -= 1;
	list_del_init(&virt_ep->bw_endpoint_list);
}

static void xhci_add_ep_to_interval_table(struct xhci_hcd *xhci,
		struct xhci_bw_info *ep_bw,
		struct xhci_interval_bw_table *bw_table,
		struct usb_device *udev,
		struct xhci_virt_ep *virt_ep,
		struct xhci_tt_bw_info *tt_info)
{
	struct xhci_interval_bw	*interval_bw;
	struct xhci_virt_ep *smaller_ep;
	int normalized_interval;

	if (xhci_is_async_ep(ep_bw->type))
		return;

	if (udev->speed == USB_SPEED_SUPER) {
		if (xhci_is_sync_in_ep(ep_bw->type))
			xhci->devs[udev->slot_id]->bw_table->ss_bw_in +=
				xhci_get_ss_bw_consumed(ep_bw);
		else
			xhci->devs[udev->slot_id]->bw_table->ss_bw_out +=
				xhci_get_ss_bw_consumed(ep_bw);
		return;
	}

	/* For LS/FS devices, we need to translate the interval expressed in
	 * microframes to frames.
	 */
	if (udev->speed == USB_SPEED_HIGH)
		normalized_interval = ep_bw->ep_interval;
	else
		normalized_interval = ep_bw->ep_interval - 3;

	if (normalized_interval == 0)
		bw_table->interval0_esit_payload += ep_bw->max_esit_payload;
	interval_bw = &bw_table->interval_bw[normalized_interval];
	interval_bw->num_packets += ep_bw->num_packets;
	switch (udev->speed) {
	case USB_SPEED_LOW:
		interval_bw->overhead[LS_OVERHEAD_TYPE] += 1;
		break;
	case USB_SPEED_FULL:
		interval_bw->overhead[FS_OVERHEAD_TYPE] += 1;
		break;
	case USB_SPEED_HIGH:
		interval_bw->overhead[HS_OVERHEAD_TYPE] += 1;
		break;
	case USB_SPEED_SUPER:
	case USB_SPEED_UNKNOWN:
	case USB_SPEED_WIRELESS:
		/* Should never happen because only LS/FS/HS endpoints will get
		 * added to the endpoint list.
		 */
		return;
	}

	if (tt_info)
		tt_info->active_eps += 1;
	/* Insert the endpoint into the list, largest max packet size first. */
	list_for_each_entry(smaller_ep, &interval_bw->endpoints,
			bw_endpoint_list) {
		if (ep_bw->max_packet_size >=
				smaller_ep->bw_info.max_packet_size) {
			/* Add the new ep before the smaller endpoint */
			list_add_tail(&virt_ep->bw_endpoint_list,
					&smaller_ep->bw_endpoint_list);
			return;
		}
	}
	/* Add the new endpoint at the end of the list. */
	list_add_tail(&virt_ep->bw_endpoint_list,
			&interval_bw->endpoints);
}

void xhci_update_tt_active_eps(struct xhci_hcd *xhci,
		struct xhci_virt_device *virt_dev,
		int old_active_eps)
{
	struct xhci_root_port_bw_info *rh_bw_info;
	if (!virt_dev->tt_info)
		return;

	rh_bw_info = &xhci->rh_bw[virt_dev->real_port - 1];
	if (old_active_eps == 0 &&
				virt_dev->tt_info->active_eps != 0) {
		rh_bw_info->num_active_tts += 1;
		rh_bw_info->bw_table.bw_used += TT_HS_OVERHEAD;
	} else if (old_active_eps != 0 &&
				virt_dev->tt_info->active_eps == 0) {
		rh_bw_info->num_active_tts -= 1;
		rh_bw_info->bw_table.bw_used -= TT_HS_OVERHEAD;
	}
}

static int xhci_reserve_bandwidth(struct xhci_hcd *xhci,
		struct xhci_virt_device *virt_dev,
		struct xhci_container_ctx *in_ctx)
{
	struct xhci_bw_info ep_bw_info[31];
	int i;
	struct xhci_input_control_ctx *ctrl_ctx;
	int old_active_eps = 0;

	if (virt_dev->tt_info)
		old_active_eps = virt_dev->tt_info->active_eps;

	ctrl_ctx = xhci_get_input_control_ctx(xhci, in_ctx);
	if (!ctrl_ctx) {
		xhci_warn(xhci, "%s: Could not get input context, bad type.\n",
				__func__);
		return -ENOMEM;
	}

	for (i = 0; i < 31; i++) {
		if (!EP_IS_ADDED(ctrl_ctx, i) && !EP_IS_DROPPED(ctrl_ctx, i))
			continue;

		/* Make a copy of the BW info in case we need to revert this */
		memcpy(&ep_bw_info[i], &virt_dev->eps[i].bw_info,
				sizeof(ep_bw_info[i]));
		/* Drop the endpoint from the interval table if the endpoint is
		 * being dropped or changed.
		 */
		if (EP_IS_DROPPED(ctrl_ctx, i))
			xhci_drop_ep_from_interval_table(xhci,
					&virt_dev->eps[i].bw_info,
					virt_dev->bw_table,
					virt_dev->udev,
					&virt_dev->eps[i],
					virt_dev->tt_info);
	}
	/* Overwrite the information stored in the endpoints' bw_info */
	xhci_update_bw_info(xhci, virt_dev->in_ctx, ctrl_ctx, virt_dev);
	for (i = 0; i < 31; i++) {
		/* Add any changed or added endpoints to the interval table */
		if (EP_IS_ADDED(ctrl_ctx, i))
			xhci_add_ep_to_interval_table(xhci,
					&virt_dev->eps[i].bw_info,
					virt_dev->bw_table,
					virt_dev->udev,
					&virt_dev->eps[i],
					virt_dev->tt_info);
	}

	if (!xhci_check_bw_table(xhci, virt_dev, old_active_eps)) {
		/* Ok, this fits in the bandwidth we have.
		 * Update the number of active TTs.
		 */
		xhci_update_tt_active_eps(xhci, virt_dev, old_active_eps);
		return 0;
	}

	/* We don't have enough bandwidth for this, revert the stored info. */
	for (i = 0; i < 31; i++) {
		if (!EP_IS_ADDED(ctrl_ctx, i) && !EP_IS_DROPPED(ctrl_ctx, i))
			continue;

		/* Drop the new copies of any added or changed endpoints from
		 * the interval table.
		 */
		if (EP_IS_ADDED(ctrl_ctx, i)) {
			xhci_drop_ep_from_interval_table(xhci,
					&virt_dev->eps[i].bw_info,
					virt_dev->bw_table,
					virt_dev->udev,
					&virt_dev->eps[i],
					virt_dev->tt_info);
		}
		/* Revert the endpoint back to its old information */
		memcpy(&virt_dev->eps[i].bw_info, &ep_bw_info[i],
				sizeof(ep_bw_info[i]));
		/* Add any changed or dropped endpoints back into the table */
		if (EP_IS_DROPPED(ctrl_ctx, i))
			xhci_add_ep_to_interval_table(xhci,
					&virt_dev->eps[i].bw_info,
					virt_dev->bw_table,
					virt_dev->udev,
					&virt_dev->eps[i],
					virt_dev->tt_info);
	}
	return -ENOMEM;
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
	unsigned long flags;
	struct xhci_input_control_ctx *ctrl_ctx;
	struct xhci_virt_device *virt_dev;

	if (!command)
		return -EINVAL;

	spin_lock_irqsave(&xhci->lock, flags);
	virt_dev = xhci->devs[udev->slot_id];

	ctrl_ctx = xhci_get_input_control_ctx(xhci, command->in_ctx);
	if (!ctrl_ctx) {
		spin_unlock_irqrestore(&xhci->lock, flags);
		xhci_warn(xhci, "%s: Could not get input context, bad type.\n",
				__func__);
		return -ENOMEM;
	}

	if ((xhci->quirks & XHCI_EP_LIMIT_QUIRK) &&
			xhci_reserve_host_resources(xhci, ctrl_ctx)) {
		spin_unlock_irqrestore(&xhci->lock, flags);
		xhci_warn(xhci, "Not enough host resources, "
				"active endpoint contexts = %u\n",
				xhci->num_active_eps);
		return -ENOMEM;
	}
	if ((xhci->quirks & XHCI_SW_BW_CHECKING) &&
	    xhci_reserve_bandwidth(xhci, virt_dev, command->in_ctx)) {
		if ((xhci->quirks & XHCI_EP_LIMIT_QUIRK))
			xhci_free_host_resources(xhci, ctrl_ctx);
		spin_unlock_irqrestore(&xhci->lock, flags);
		xhci_warn(xhci, "Not enough bandwidth\n");
		return -ENOMEM;
	}

	if (!ctx_change)
		ret = xhci_queue_configure_endpoint(xhci, command,
				command->in_ctx->dma,
				udev->slot_id, must_succeed);
	else
		ret = xhci_queue_evaluate_context(xhci, command,
				command->in_ctx->dma,
				udev->slot_id, must_succeed);
	if (ret < 0) {
		if ((xhci->quirks & XHCI_EP_LIMIT_QUIRK))
			xhci_free_host_resources(xhci, ctrl_ctx);
		spin_unlock_irqrestore(&xhci->lock, flags);
		xhci_dbg_trace(xhci,  trace_xhci_dbg_context_change,
				"FIXME allocate a new ring segment");
		return -ENOMEM;
	}
	xhci_ring_cmd_db(xhci);
	spin_unlock_irqrestore(&xhci->lock, flags);

	/* Wait for the configure endpoint command to complete */
	wait_for_completion(command->completion);

	if (!ctx_change)
		ret = xhci_configure_endpoint_result(xhci, udev,
						     &command->status);
	else
		ret = xhci_evaluate_context_result(xhci, udev,
						   &command->status);

	if ((xhci->quirks & XHCI_EP_LIMIT_QUIRK)) {
		spin_lock_irqsave(&xhci->lock, flags);
		/* If the command failed, remove the reserved resources.
		 * Otherwise, clean up the estimate to include dropped eps.
		 */
		if (ret)
			xhci_free_host_resources(xhci, ctrl_ctx);
		else
			xhci_finish_resource_reservation(xhci, ctrl_ctx);
		spin_unlock_irqrestore(&xhci->lock, flags);
	}
	return ret;
}

static void xhci_check_bw_drop_ep_streams(struct xhci_hcd *xhci,
	struct xhci_virt_device *vdev, int i)
{
	struct xhci_virt_ep *ep = &vdev->eps[i];

	if (ep->ep_state & EP_HAS_STREAMS) {
		xhci_warn(xhci, "WARN: endpoint 0x%02x has streams on set_interface, freeing streams.\n",
				xhci_get_endpoint_address(i));
		xhci_free_stream_info(xhci, ep->stream_info);
		ep->stream_info = NULL;
		ep->ep_state &= ~EP_HAS_STREAMS;
	}
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
	struct xhci_command *command;

	ret = xhci_check_args(hcd, udev, NULL, 0, true, __func__);
	if (ret <= 0)
		return ret;
	xhci = hcd_to_xhci(hcd);
	if (xhci->xhc_state & XHCI_STATE_DYING)
		return -ENODEV;

	xhci_dbg(xhci, "%s called for udev %p\n", __func__, udev);
	virt_dev = xhci->devs[udev->slot_id];

	command = xhci_alloc_command(xhci, false, true, GFP_KERNEL);
	if (!command)
		return -ENOMEM;

	command->in_ctx = virt_dev->in_ctx;

	/* See section 4.6.6 - A0 = 1; A1 = D0 = D1 = 0 */
	ctrl_ctx = xhci_get_input_control_ctx(xhci, command->in_ctx);
	if (!ctrl_ctx) {
		xhci_warn(xhci, "%s: Could not get input context, bad type.\n",
				__func__);
		ret = -ENOMEM;
		goto command_cleanup;
	}
	ctrl_ctx->add_flags |= cpu_to_le32(SLOT_FLAG);
	ctrl_ctx->add_flags &= cpu_to_le32(~EP0_FLAG);
	ctrl_ctx->drop_flags &= cpu_to_le32(~(SLOT_FLAG | EP0_FLAG));

	/* Don't issue the command if there's no endpoints to update. */
	if (ctrl_ctx->add_flags == cpu_to_le32(SLOT_FLAG) &&
	    ctrl_ctx->drop_flags == 0) {
		ret = 0;
		goto command_cleanup;
	}
	/* Fix up Context Entries field. Minimum value is EP0 == BIT(1). */
	slot_ctx = xhci_get_slot_ctx(xhci, virt_dev->in_ctx);
	for (i = 31; i >= 1; i--) {
		__le32 le32 = cpu_to_le32(BIT(i));

		if ((virt_dev->eps[i-1].ring && !(ctrl_ctx->drop_flags & le32))
		    || (ctrl_ctx->add_flags & le32) || i == 1) {
			slot_ctx->dev_info &= cpu_to_le32(~LAST_CTX_MASK);
			slot_ctx->dev_info |= cpu_to_le32(LAST_CTX(i));
			break;
		}
	}
	xhci_dbg(xhci, "New Input Control Context:\n");
	xhci_dbg_ctx(xhci, virt_dev->in_ctx,
		     LAST_CTX_TO_EP_NUM(le32_to_cpu(slot_ctx->dev_info)));

	ret = xhci_configure_endpoint(xhci, udev, command,
			false, false);
	if (ret)
		/* Callee should call reset_bandwidth() */
		goto command_cleanup;

	xhci_dbg(xhci, "Output context after successful config ep cmd:\n");
	xhci_dbg_ctx(xhci, virt_dev->out_ctx,
		     LAST_CTX_TO_EP_NUM(le32_to_cpu(slot_ctx->dev_info)));

	/* Free any rings that were dropped, but not changed. */
	for (i = 1; i < 31; ++i) {
		if ((le32_to_cpu(ctrl_ctx->drop_flags) & (1 << (i + 1))) &&
		    !(le32_to_cpu(ctrl_ctx->add_flags) & (1 << (i + 1)))) {
			xhci_free_or_cache_endpoint_ring(xhci, virt_dev, i);
			xhci_check_bw_drop_ep_streams(xhci, virt_dev, i);
		}
	}
	xhci_zero_in_ctx(xhci, virt_dev);
	/*
	 * Install any rings for completely new endpoints or changed endpoints,
	 * and free or cache any old rings from changed endpoints.
	 */
	for (i = 1; i < 31; ++i) {
		if (!virt_dev->eps[i].new_ring)
			continue;
		/* Only cache or free the old ring if it exists.
		 * It may not if this is the first add of an endpoint.
		 */
		if (virt_dev->eps[i].ring) {
			xhci_free_or_cache_endpoint_ring(xhci, virt_dev, i);
		}
		xhci_check_bw_drop_ep_streams(xhci, virt_dev, i);
		virt_dev->eps[i].ring = virt_dev->eps[i].new_ring;
		virt_dev->eps[i].new_ring = NULL;
	}
command_cleanup:
	kfree(command->completion);
	kfree(command);

	return ret;
}

void xhci_reset_bandwidth(struct usb_hcd *hcd, struct usb_device *udev)
{
	struct xhci_hcd *xhci;
	struct xhci_virt_device	*virt_dev;
	int i, ret;

	ret = xhci_check_args(hcd, udev, NULL, 0, true, __func__);
	if (ret <= 0)
		return;
	xhci = hcd_to_xhci(hcd);

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
		struct xhci_input_control_ctx *ctrl_ctx,
		u32 add_flags, u32 drop_flags)
{
	ctrl_ctx->add_flags = cpu_to_le32(add_flags);
	ctrl_ctx->drop_flags = cpu_to_le32(drop_flags);
	xhci_slot_copy(xhci, in_ctx, out_ctx);
	ctrl_ctx->add_flags |= cpu_to_le32(SLOT_FLAG);

	xhci_dbg(xhci, "Input Context:\n");
	xhci_dbg_ctx(xhci, in_ctx, xhci_last_valid_endpoint(add_flags));
}

static void xhci_setup_input_ctx_for_quirk(struct xhci_hcd *xhci,
		unsigned int slot_id, unsigned int ep_index,
		struct xhci_dequeue_state *deq_state)
{
	struct xhci_input_control_ctx *ctrl_ctx;
	struct xhci_container_ctx *in_ctx;
	struct xhci_ep_ctx *ep_ctx;
	u32 added_ctxs;
	dma_addr_t addr;

	in_ctx = xhci->devs[slot_id]->in_ctx;
	ctrl_ctx = xhci_get_input_control_ctx(xhci, in_ctx);
	if (!ctrl_ctx) {
		xhci_warn(xhci, "%s: Could not get input context, bad type.\n",
				__func__);
		return;
	}

	xhci_endpoint_copy(xhci, xhci->devs[slot_id]->in_ctx,
			xhci->devs[slot_id]->out_ctx, ep_index);
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
	ep_ctx->deq = cpu_to_le64(addr | deq_state->new_cycle_state);

	added_ctxs = xhci_get_endpoint_flag_from_index(ep_index);
	xhci_setup_input_ctx_for_config_ep(xhci, xhci->devs[slot_id]->in_ctx,
			xhci->devs[slot_id]->out_ctx, ctrl_ctx,
			added_ctxs, added_ctxs);
}

void xhci_cleanup_stalled_ring(struct xhci_hcd *xhci,
		struct usb_device *udev, unsigned int ep_index)
{
	struct xhci_dequeue_state deq_state;
	struct xhci_virt_ep *ep;

	xhci_dbg_trace(xhci, trace_xhci_dbg_reset_ep,
			"Cleaning up stalled endpoint ring");
	ep = &xhci->devs[udev->slot_id]->eps[ep_index];
	/* We need to move the HW's dequeue pointer past this TD,
	 * or it will attempt to resend it on the next doorbell ring.
	 */
	xhci_find_new_dequeue_state(xhci, udev->slot_id,
			ep_index, ep->stopped_stream, ep->stopped_td,
			&deq_state);

	if (!deq_state.new_deq_ptr || !deq_state.new_deq_seg)
		return;

	/* HW with the reset endpoint quirk will use the saved dequeue state to
	 * issue a configure endpoint command later.
	 */
	if (!(xhci->quirks & XHCI_RESET_EP_QUIRK)) {
		xhci_dbg_trace(xhci, trace_xhci_dbg_reset_ep,
				"Queueing new dequeue state");
		xhci_queue_new_dequeue_state(xhci, udev->slot_id,
				ep_index, ep->stopped_stream, &deq_state);
	} else {
		/* Better hope no one uses the input context between now and the
		 * reset endpoint completion!
		 * XXX: No idea how this hardware will react when stream rings
		 * are enabled.
		 */
		xhci_dbg_trace(xhci, trace_xhci_dbg_quirks,
				"Setting up input context for "
				"configure endpoint command");
		xhci_setup_input_ctx_for_quirk(xhci, udev->slot_id,
				ep_index, &deq_state);
	}
}

/* Called when clearing halted device. The core should have sent the control
 * message to clear the device halt condition. The host side of the halt should
 * already be cleared with a reset endpoint command issued when the STALL tx
 * event was received.
 *
 * Context: in_interrupt
 */

void xhci_endpoint_reset(struct usb_hcd *hcd,
		struct usb_host_endpoint *ep)
{
	struct xhci_hcd *xhci;

	xhci = hcd_to_xhci(hcd);

	/*
	 * We might need to implement the config ep cmd in xhci 4.8.1 note:
	 * The Reset Endpoint Command may only be issued to endpoints in the
	 * Halted state. If software wishes reset the Data Toggle or Sequence
	 * Number of an endpoint that isn't in the Halted state, then software
	 * may issue a Configure Endpoint Command with the Drop and Add bits set
	 * for the target endpoint. that is in the Stopped state.
	 */

	/* For now just print debug to follow the situation */
	xhci_dbg(xhci, "Endpoint 0x%x ep reset callback called\n",
		 ep->desc.bEndpointAddress);
}

static int xhci_check_streams_endpoint(struct xhci_hcd *xhci,
		struct usb_device *udev, struct usb_host_endpoint *ep,
		unsigned int slot_id)
{
	int ret;
	unsigned int ep_index;
	unsigned int ep_state;

	if (!ep)
		return -EINVAL;
	ret = xhci_check_args(xhci_to_hcd(xhci), udev, ep, 1, true, __func__);
	if (ret <= 0)
		return -EINVAL;
	if (usb_ss_max_streams(&ep->ss_ep_comp) == 0) {
		xhci_warn(xhci, "WARN: SuperSpeed Endpoint Companion"
				" descriptor for ep 0x%x does not support streams\n",
				ep->desc.bEndpointAddress);
		return -EINVAL;
	}

	ep_index = xhci_get_endpoint_index(&ep->desc);
	ep_state = xhci->devs[slot_id]->eps[ep_index].ep_state;
	if (ep_state & EP_HAS_STREAMS ||
			ep_state & EP_GETTING_STREAMS) {
		xhci_warn(xhci, "WARN: SuperSpeed bulk endpoint 0x%x "
				"already has streams set up.\n",
				ep->desc.bEndpointAddress);
		xhci_warn(xhci, "Send email to xHCI maintainer and ask for "
				"dynamic stream context array reallocation.\n");
		return -EINVAL;
	}
	if (!list_empty(&xhci->devs[slot_id]->eps[ep_index].ring->td_list)) {
		xhci_warn(xhci, "Cannot setup streams for SuperSpeed bulk "
				"endpoint 0x%x; URBs are pending.\n",
				ep->desc.bEndpointAddress);
		return -EINVAL;
	}
	return 0;
}

static void xhci_calculate_streams_entries(struct xhci_hcd *xhci,
		unsigned int *num_streams, unsigned int *num_stream_ctxs)
{
	unsigned int max_streams;

	/* The stream context array size must be a power of two */
	*num_stream_ctxs = roundup_pow_of_two(*num_streams);
	/*
	 * Find out how many primary stream array entries the host controller
	 * supports.  Later we may use secondary stream arrays (similar to 2nd
	 * level page entries), but that's an optional feature for xHCI host
	 * controllers. xHCs must support at least 4 stream IDs.
	 */
	max_streams = HCC_MAX_PSA(xhci->hcc_params);
	if (*num_stream_ctxs > max_streams) {
		xhci_dbg(xhci, "xHCI HW only supports %u stream ctx entries.\n",
				max_streams);
		*num_stream_ctxs = max_streams;
		*num_streams = max_streams;
	}
}

/* Returns an error code if one of the endpoint already has streams.
 * This does not change any data structures, it only checks and gathers
 * information.
 */
static int xhci_calculate_streams_and_bitmask(struct xhci_hcd *xhci,
		struct usb_device *udev,
		struct usb_host_endpoint **eps, unsigned int num_eps,
		unsigned int *num_streams, u32 *changed_ep_bitmask)
{
	unsigned int max_streams;
	unsigned int endpoint_flag;
	int i;
	int ret;

	for (i = 0; i < num_eps; i++) {
		ret = xhci_check_streams_endpoint(xhci, udev,
				eps[i], udev->slot_id);
		if (ret < 0)
			return ret;

		max_streams = usb_ss_max_streams(&eps[i]->ss_ep_comp);
		if (max_streams < (*num_streams - 1)) {
			xhci_dbg(xhci, "Ep 0x%x only supports %u stream IDs.\n",
					eps[i]->desc.bEndpointAddress,
					max_streams);
			*num_streams = max_streams+1;
		}

		endpoint_flag = xhci_get_endpoint_flag(&eps[i]->desc);
		if (*changed_ep_bitmask & endpoint_flag)
			return -EINVAL;
		*changed_ep_bitmask |= endpoint_flag;
	}
	return 0;
}

static u32 xhci_calculate_no_streams_bitmask(struct xhci_hcd *xhci,
		struct usb_device *udev,
		struct usb_host_endpoint **eps, unsigned int num_eps)
{
	u32 changed_ep_bitmask = 0;
	unsigned int slot_id;
	unsigned int ep_index;
	unsigned int ep_state;
	int i;

	slot_id = udev->slot_id;
	if (!xhci->devs[slot_id])
		return 0;

	for (i = 0; i < num_eps; i++) {
		ep_index = xhci_get_endpoint_index(&eps[i]->desc);
		ep_state = xhci->devs[slot_id]->eps[ep_index].ep_state;
		/* Are streams already being freed for the endpoint? */
		if (ep_state & EP_GETTING_NO_STREAMS) {
			xhci_warn(xhci, "WARN Can't disable streams for "
					"endpoint 0x%x, "
					"streams are being disabled already\n",
					eps[i]->desc.bEndpointAddress);
			return 0;
		}
		/* Are there actually any streams to free? */
		if (!(ep_state & EP_HAS_STREAMS) &&
				!(ep_state & EP_GETTING_STREAMS)) {
			xhci_warn(xhci, "WARN Can't disable streams for "
					"endpoint 0x%x, "
					"streams are already disabled!\n",
					eps[i]->desc.bEndpointAddress);
			xhci_warn(xhci, "WARN xhci_free_streams() called "
					"with non-streams endpoint\n");
			return 0;
		}
		changed_ep_bitmask |= xhci_get_endpoint_flag(&eps[i]->desc);
	}
	return changed_ep_bitmask;
}

/*
 * The USB device drivers use this function (though the HCD interface in USB
 * core) to prepare a set of bulk endpoints to use streams.  Streams are used to
 * coordinate mass storage command queueing across multiple endpoints (basically
 * a stream ID == a task ID).
 *
 * Setting up streams involves allocating the same size stream context array
 * for each endpoint and issuing a configure endpoint command for all endpoints.
 *
 * Don't allow the call to succeed if one endpoint only supports one stream
 * (which means it doesn't support streams at all).
 *
 * Drivers may get less stream IDs than they asked for, if the host controller
 * hardware or endpoints claim they can't support the number of requested
 * stream IDs.
 */
int xhci_alloc_streams(struct usb_hcd *hcd, struct usb_device *udev,
		struct usb_host_endpoint **eps, unsigned int num_eps,
		unsigned int num_streams, gfp_t mem_flags)
{
	int i, ret;
	struct xhci_hcd *xhci;
	struct xhci_virt_device *vdev;
	struct xhci_command *config_cmd;
	struct xhci_input_control_ctx *ctrl_ctx;
	unsigned int ep_index;
	unsigned int num_stream_ctxs;
	unsigned long flags;
	u32 changed_ep_bitmask = 0;

	if (!eps)
		return -EINVAL;

	/* Add one to the number of streams requested to account for
	 * stream 0 that is reserved for xHCI usage.
	 */
	num_streams += 1;
	xhci = hcd_to_xhci(hcd);
	xhci_dbg(xhci, "Driver wants %u stream IDs (including stream 0).\n",
			num_streams);

	/* MaxPSASize value 0 (2 streams) means streams are not supported */
	if ((xhci->quirks & XHCI_BROKEN_STREAMS) ||
			HCC_MAX_PSA(xhci->hcc_params) < 4) {
		xhci_dbg(xhci, "xHCI controller does not support streams.\n");
		return -ENOSYS;
	}

	config_cmd = xhci_alloc_command(xhci, true, true, mem_flags);
	if (!config_cmd) {
		xhci_dbg(xhci, "Could not allocate xHCI command structure.\n");
		return -ENOMEM;
	}
	ctrl_ctx = xhci_get_input_control_ctx(xhci, config_cmd->in_ctx);
	if (!ctrl_ctx) {
		xhci_warn(xhci, "%s: Could not get input context, bad type.\n",
				__func__);
		xhci_free_command(xhci, config_cmd);
		return -ENOMEM;
	}

	/* Check to make sure all endpoints are not already configured for
	 * streams.  While we're at it, find the maximum number of streams that
	 * all the endpoints will support and check for duplicate endpoints.
	 */
	spin_lock_irqsave(&xhci->lock, flags);
	ret = xhci_calculate_streams_and_bitmask(xhci, udev, eps,
			num_eps, &num_streams, &changed_ep_bitmask);
	if (ret < 0) {
		xhci_free_command(xhci, config_cmd);
		spin_unlock_irqrestore(&xhci->lock, flags);
		return ret;
	}
	if (num_streams <= 1) {
		xhci_warn(xhci, "WARN: endpoints can't handle "
				"more than one stream.\n");
		xhci_free_command(xhci, config_cmd);
		spin_unlock_irqrestore(&xhci->lock, flags);
		return -EINVAL;
	}
	vdev = xhci->devs[udev->slot_id];
	/* Mark each endpoint as being in transition, so
	 * xhci_urb_enqueue() will reject all URBs.
	 */
	for (i = 0; i < num_eps; i++) {
		ep_index = xhci_get_endpoint_index(&eps[i]->desc);
		vdev->eps[ep_index].ep_state |= EP_GETTING_STREAMS;
	}
	spin_unlock_irqrestore(&xhci->lock, flags);

	/* Setup internal data structures and allocate HW data structures for
	 * streams (but don't install the HW structures in the input context
	 * until we're sure all memory allocation succeeded).
	 */
	xhci_calculate_streams_entries(xhci, &num_streams, &num_stream_ctxs);
	xhci_dbg(xhci, "Need %u stream ctx entries for %u stream IDs.\n",
			num_stream_ctxs, num_streams);

	for (i = 0; i < num_eps; i++) {
		ep_index = xhci_get_endpoint_index(&eps[i]->desc);
		vdev->eps[ep_index].stream_info = xhci_alloc_stream_info(xhci,
				num_stream_ctxs,
				num_streams, mem_flags);
		if (!vdev->eps[ep_index].stream_info)
			goto cleanup;
		/* Set maxPstreams in endpoint context and update deq ptr to
		 * point to stream context array. FIXME
		 */
	}

	/* Set up the input context for a configure endpoint command. */
	for (i = 0; i < num_eps; i++) {
		struct xhci_ep_ctx *ep_ctx;

		ep_index = xhci_get_endpoint_index(&eps[i]->desc);
		ep_ctx = xhci_get_ep_ctx(xhci, config_cmd->in_ctx, ep_index);

		xhci_endpoint_copy(xhci, config_cmd->in_ctx,
				vdev->out_ctx, ep_index);
		xhci_setup_streams_ep_input_ctx(xhci, ep_ctx,
				vdev->eps[ep_index].stream_info);
	}
	/* Tell the HW to drop its old copy of the endpoint context info
	 * and add the updated copy from the input context.
	 */
	xhci_setup_input_ctx_for_config_ep(xhci, config_cmd->in_ctx,
			vdev->out_ctx, ctrl_ctx,
			changed_ep_bitmask, changed_ep_bitmask);

	/* Issue and wait for the configure endpoint command */
	ret = xhci_configure_endpoint(xhci, udev, config_cmd,
			false, false);

	/* xHC rejected the configure endpoint command for some reason, so we
	 * leave the old ring intact and free our internal streams data
	 * structure.
	 */
	if (ret < 0)
		goto cleanup;

	spin_lock_irqsave(&xhci->lock, flags);
	for (i = 0; i < num_eps; i++) {
		ep_index = xhci_get_endpoint_index(&eps[i]->desc);
		vdev->eps[ep_index].ep_state &= ~EP_GETTING_STREAMS;
		xhci_dbg(xhci, "Slot %u ep ctx %u now has streams.\n",
			 udev->slot_id, ep_index);
		vdev->eps[ep_index].ep_state |= EP_HAS_STREAMS;
	}
	xhci_free_command(xhci, config_cmd);
	spin_unlock_irqrestore(&xhci->lock, flags);

	/* Subtract 1 for stream 0, which drivers can't use */
	return num_streams - 1;

cleanup:
	/* If it didn't work, free the streams! */
	for (i = 0; i < num_eps; i++) {
		ep_index = xhci_get_endpoint_index(&eps[i]->desc);
		xhci_free_stream_info(xhci, vdev->eps[ep_index].stream_info);
		vdev->eps[ep_index].stream_info = NULL;
		/* FIXME Unset maxPstreams in endpoint context and
		 * update deq ptr to point to normal string ring.
		 */
		vdev->eps[ep_index].ep_state &= ~EP_GETTING_STREAMS;
		vdev->eps[ep_index].ep_state &= ~EP_HAS_STREAMS;
		xhci_endpoint_zero(xhci, vdev, eps[i]);
	}
	xhci_free_command(xhci, config_cmd);
	return -ENOMEM;
}

/* Transition the endpoint from using streams to being a "normal" endpoint
 * without streams.
 *
 * Modify the endpoint context state, submit a configure endpoint command,
 * and free all endpoint rings for streams if that completes successfully.
 */
int xhci_free_streams(struct usb_hcd *hcd, struct usb_device *udev,
		struct usb_host_endpoint **eps, unsigned int num_eps,
		gfp_t mem_flags)
{
	int i, ret;
	struct xhci_hcd *xhci;
	struct xhci_virt_device *vdev;
	struct xhci_command *command;
	struct xhci_input_control_ctx *ctrl_ctx;
	unsigned int ep_index;
	unsigned long flags;
	u32 changed_ep_bitmask;

	xhci = hcd_to_xhci(hcd);
	vdev = xhci->devs[udev->slot_id];

	/* Set up a configure endpoint command to remove the streams rings */
	spin_lock_irqsave(&xhci->lock, flags);
	changed_ep_bitmask = xhci_calculate_no_streams_bitmask(xhci,
			udev, eps, num_eps);
	if (changed_ep_bitmask == 0) {
		spin_unlock_irqrestore(&xhci->lock, flags);
		return -EINVAL;
	}

	/* Use the xhci_command structure from the first endpoint.  We may have
	 * allocated too many, but the driver may call xhci_free_streams() for
	 * each endpoint it grouped into one call to xhci_alloc_streams().
	 */
	ep_index = xhci_get_endpoint_index(&eps[0]->desc);
	command = vdev->eps[ep_index].stream_info->free_streams_command;
	ctrl_ctx = xhci_get_input_control_ctx(xhci, command->in_ctx);
	if (!ctrl_ctx) {
		spin_unlock_irqrestore(&xhci->lock, flags);
		xhci_warn(xhci, "%s: Could not get input context, bad type.\n",
				__func__);
		return -EINVAL;
	}

	for (i = 0; i < num_eps; i++) {
		struct xhci_ep_ctx *ep_ctx;

		ep_index = xhci_get_endpoint_index(&eps[i]->desc);
		ep_ctx = xhci_get_ep_ctx(xhci, command->in_ctx, ep_index);
		xhci->devs[udev->slot_id]->eps[ep_index].ep_state |=
			EP_GETTING_NO_STREAMS;

		xhci_endpoint_copy(xhci, command->in_ctx,
				vdev->out_ctx, ep_index);
		xhci_setup_no_streams_ep_input_ctx(xhci, ep_ctx,
				&vdev->eps[ep_index]);
	}
	xhci_setup_input_ctx_for_config_ep(xhci, command->in_ctx,
			vdev->out_ctx, ctrl_ctx,
			changed_ep_bitmask, changed_ep_bitmask);
	spin_unlock_irqrestore(&xhci->lock, flags);

	/* Issue and wait for the configure endpoint command,
	 * which must succeed.
	 */
	ret = xhci_configure_endpoint(xhci, udev, command,
			false, true);

	/* xHC rejected the configure endpoint command for some reason, so we
	 * leave the streams rings intact.
	 */
	if (ret < 0)
		return ret;

	spin_lock_irqsave(&xhci->lock, flags);
	for (i = 0; i < num_eps; i++) {
		ep_index = xhci_get_endpoint_index(&eps[i]->desc);
		xhci_free_stream_info(xhci, vdev->eps[ep_index].stream_info);
		vdev->eps[ep_index].stream_info = NULL;
		/* FIXME Unset maxPstreams in endpoint context and
		 * update deq ptr to point to normal string ring.
		 */
		vdev->eps[ep_index].ep_state &= ~EP_GETTING_NO_STREAMS;
		vdev->eps[ep_index].ep_state &= ~EP_HAS_STREAMS;
	}
	spin_unlock_irqrestore(&xhci->lock, flags);

	return 0;
}

/*
 * Deletes endpoint resources for endpoints that were active before a Reset
 * Device command, or a Disable Slot command.  The Reset Device command leaves
 * the control endpoint intact, whereas the Disable Slot command deletes it.
 *
 * Must be called with xhci->lock held.
 */
void xhci_free_device_endpoint_resources(struct xhci_hcd *xhci,
	struct xhci_virt_device *virt_dev, bool drop_control_ep)
{
	int i;
	unsigned int num_dropped_eps = 0;
	unsigned int drop_flags = 0;

	for (i = (drop_control_ep ? 0 : 1); i < 31; i++) {
		if (virt_dev->eps[i].ring) {
			drop_flags |= 1 << i;
			num_dropped_eps++;
		}
	}
	xhci->num_active_eps -= num_dropped_eps;
	if (num_dropped_eps)
		xhci_dbg_trace(xhci, trace_xhci_dbg_quirks,
				"Dropped %u ep ctxs, flags = 0x%x, "
				"%u now active.",
				num_dropped_eps, drop_flags,
				xhci->num_active_eps);
}

/*
 * This submits a Reset Device Command, which will set the device state to 0,
 * set the device address to 0, and disable all the endpoints except the default
 * control endpoint.  The USB core should come back and call
 * xhci_address_device(), and then re-set up the configuration.  If this is
 * called because of a usb_reset_and_verify_device(), then the old alternate
 * settings will be re-installed through the normal bandwidth allocation
 * functions.
 *
 * Wait for the Reset Device command to finish.  Remove all structures
 * associated with the endpoints that were disabled.  Clear the input device
 * structure?  Cache the rings?  Reset the control endpoint 0 max packet size?
 *
 * If the virt_dev to be reset does not exist or does not match the udev,
 * it means the device is lost, possibly due to the xHC restore error and
 * re-initialization during S3/S4. In this case, call xhci_alloc_dev() to
 * re-allocate the device.
 */
int xhci_discover_or_reset_device(struct usb_hcd *hcd, struct usb_device *udev)
{
	int ret, i;
	unsigned long flags;
	struct xhci_hcd *xhci;
	unsigned int slot_id;
	struct xhci_virt_device *virt_dev;
	struct xhci_command *reset_device_cmd;
	int last_freed_endpoint;
	struct xhci_slot_ctx *slot_ctx;
	int old_active_eps = 0;

	ret = xhci_check_args(hcd, udev, NULL, 0, false, __func__);
	if (ret <= 0)
		return ret;
	xhci = hcd_to_xhci(hcd);
	slot_id = udev->slot_id;
	virt_dev = xhci->devs[slot_id];
	if (!virt_dev) {
		xhci_dbg(xhci, "The device to be reset with slot ID %u does "
				"not exist. Re-allocate the device\n", slot_id);
		ret = xhci_alloc_dev(hcd, udev);
		if (ret == 1)
			return 0;
		else
			return -EINVAL;
	}

	if (virt_dev->udev != udev) {
		/* If the virt_dev and the udev does not match, this virt_dev
		 * may belong to another udev.
		 * Re-allocate the device.
		 */
		xhci_dbg(xhci, "The device to be reset with slot ID %u does "
				"not match the udev. Re-allocate the device\n",
				slot_id);
		ret = xhci_alloc_dev(hcd, udev);
		if (ret == 1)
			return 0;
		else
			return -EINVAL;
	}

	/* If device is not setup, there is no point in resetting it */
	slot_ctx = xhci_get_slot_ctx(xhci, virt_dev->out_ctx);
	if (GET_SLOT_STATE(le32_to_cpu(slot_ctx->dev_state)) ==
						SLOT_STATE_DISABLED)
		return 0;

	xhci_dbg(xhci, "Resetting device with slot ID %u\n", slot_id);
	/* Allocate the command structure that holds the struct completion.
	 * Assume we're in process context, since the normal device reset
	 * process has to wait for the device anyway.  Storage devices are
	 * reset as part of error handling, so use GFP_NOIO instead of
	 * GFP_KERNEL.
	 */
	reset_device_cmd = xhci_alloc_command(xhci, false, true, GFP_NOIO);
	if (!reset_device_cmd) {
		xhci_dbg(xhci, "Couldn't allocate command structure.\n");
		return -ENOMEM;
	}

	/* Attempt to submit the Reset Device command to the command ring */
	spin_lock_irqsave(&xhci->lock, flags);

	ret = xhci_queue_reset_device(xhci, reset_device_cmd, slot_id);
	if (ret) {
		xhci_dbg(xhci, "FIXME: allocate a command ring segment\n");
		spin_unlock_irqrestore(&xhci->lock, flags);
		goto command_cleanup;
	}
	xhci_ring_cmd_db(xhci);
	spin_unlock_irqrestore(&xhci->lock, flags);

	/* Wait for the Reset Device command to finish */
	wait_for_completion(reset_device_cmd->completion);

	/* The Reset Device command can't fail, according to the 0.95/0.96 spec,
	 * unless we tried to reset a slot ID that wasn't enabled,
	 * or the device wasn't in the addressed or configured state.
	 */
	ret = reset_device_cmd->status;
	switch (ret) {
	case COMP_CMD_ABORT:
	case COMP_CMD_STOP:
		xhci_warn(xhci, "Timeout waiting for reset device command\n");
		ret = -ETIME;
		goto command_cleanup;
	case COMP_EBADSLT: /* 0.95 completion code for bad slot ID */
	case COMP_CTX_STATE: /* 0.96 completion code for same thing */
		xhci_dbg(xhci, "Can't reset device (slot ID %u) in %s state\n",
				slot_id,
				xhci_get_slot_state(xhci, virt_dev->out_ctx));
		xhci_dbg(xhci, "Not freeing device rings.\n");
		/* Don't treat this as an error.  May change my mind later. */
		ret = 0;
		goto command_cleanup;
	case COMP_SUCCESS:
		xhci_dbg(xhci, "Successful reset device command.\n");
		break;
	default:
		if (xhci_is_vendor_info_code(xhci, ret))
			break;
		xhci_warn(xhci, "Unknown completion code %u for "
				"reset device command.\n", ret);
		ret = -EINVAL;
		goto command_cleanup;
	}

	/* Free up host controller endpoint resources */
	if ((xhci->quirks & XHCI_EP_LIMIT_QUIRK)) {
		spin_lock_irqsave(&xhci->lock, flags);
		/* Don't delete the default control endpoint resources */
		xhci_free_device_endpoint_resources(xhci, virt_dev, false);
		spin_unlock_irqrestore(&xhci->lock, flags);
	}

	/* Everything but endpoint 0 is disabled, so free or cache the rings. */
	last_freed_endpoint = 1;
	for (i = 1; i < 31; ++i) {
		struct xhci_virt_ep *ep = &virt_dev->eps[i];

		if (ep->ep_state & EP_HAS_STREAMS) {
			xhci_warn(xhci, "WARN: endpoint 0x%02x has streams on device reset, freeing streams.\n",
					xhci_get_endpoint_address(i));
			xhci_free_stream_info(xhci, ep->stream_info);
			ep->stream_info = NULL;
			ep->ep_state &= ~EP_HAS_STREAMS;
		}

		if (ep->ring) {
			xhci_free_or_cache_endpoint_ring(xhci, virt_dev, i);
			last_freed_endpoint = i;
		}
		if (!list_empty(&virt_dev->eps[i].bw_endpoint_list))
			xhci_drop_ep_from_interval_table(xhci,
					&virt_dev->eps[i].bw_info,
					virt_dev->bw_table,
					udev,
					&virt_dev->eps[i],
					virt_dev->tt_info);
		xhci_clear_endpoint_bw_info(&virt_dev->eps[i].bw_info);
	}
	/* If necessary, update the number of active TTs on this root port */
	xhci_update_tt_active_eps(xhci, virt_dev, old_active_eps);

	xhci_dbg(xhci, "Output context after successful reset device cmd:\n");
	xhci_dbg_ctx(xhci, virt_dev->out_ctx, last_freed_endpoint);
	ret = 0;

command_cleanup:
	xhci_free_command(xhci, reset_device_cmd);
	return ret;
}

/*
 * At this point, the struct usb_device is about to go away, the device has
 * disconnected, and all traffic has been stopped and the endpoints have been
 * disabled.  Free any HC data structures associated with that device.
 */
void xhci_free_dev(struct usb_hcd *hcd, struct usb_device *udev)
{
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	struct xhci_virt_device *virt_dev;
	unsigned long flags;
	u32 state;
	int i, ret;
	struct xhci_command *command;

	command = xhci_alloc_command(xhci, false, false, GFP_KERNEL);
	if (!command)
		return;

#ifndef CONFIG_USB_DEFAULT_PERSIST
	/*
	 * We called pm_runtime_get_noresume when the device was attached.
	 * Decrement the counter here to allow controller to runtime suspend
	 * if no devices remain.
	 */
	if (xhci->quirks & XHCI_RESET_ON_RESUME)
		pm_runtime_put_noidle(hcd->self.controller);
#endif

	ret = xhci_check_args(hcd, udev, NULL, 0, true, __func__);
	/* If the host is halted due to driver unload, we still need to free the
	 * device.
	 */
	if (ret <= 0 && ret != -ENODEV) {
		kfree(command);
		return;
	}

	virt_dev = xhci->devs[udev->slot_id];

	/* Stop any wayward timer functions (which may grab the lock) */
	for (i = 0; i < 31; ++i) {
		virt_dev->eps[i].ep_state &= ~EP_HALT_PENDING;
		del_timer_sync(&virt_dev->eps[i].stop_cmd_timer);
	}

	spin_lock_irqsave(&xhci->lock, flags);
	/* Don't disable the slot if the host controller is dead. */
	state = readl(&xhci->op_regs->status);
	if (state == 0xffffffff || (xhci->xhc_state & XHCI_STATE_DYING) ||
			(xhci->xhc_state & XHCI_STATE_HALTED)) {
		xhci_free_virt_device(xhci, udev->slot_id);
		spin_unlock_irqrestore(&xhci->lock, flags);
		kfree(command);
		return;
	}

	if (xhci_queue_slot_control(xhci, command, TRB_DISABLE_SLOT,
				    udev->slot_id)) {
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
 * Checks if we have enough host controller resources for the default control
 * endpoint.
 *
 * Must be called with xhci->lock held.
 */
static int xhci_reserve_host_control_ep_resources(struct xhci_hcd *xhci)
{
	if (xhci->num_active_eps + 1 > xhci->limit_active_eps) {
		xhci_dbg_trace(xhci, trace_xhci_dbg_quirks,
				"Not enough ep ctxs: "
				"%u active, need to add 1, limit is %u.",
				xhci->num_active_eps, xhci->limit_active_eps);
		return -ENOMEM;
	}
	xhci->num_active_eps += 1;
	xhci_dbg_trace(xhci, trace_xhci_dbg_quirks,
			"Adding 1 ep ctx, %u now active.",
			xhci->num_active_eps);
	return 0;
}


/*
 * Returns 0 if the xHC ran out of device slots, the Enable Slot command
 * timed out, or allocating memory failed.  Returns 1 on success.
 */
int xhci_alloc_dev(struct usb_hcd *hcd, struct usb_device *udev)
{
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	unsigned long flags;
	int ret;
	struct xhci_command *command;

	command = xhci_alloc_command(xhci, false, false, GFP_KERNEL);
	if (!command)
		return 0;

	spin_lock_irqsave(&xhci->lock, flags);
	command->completion = &xhci->addr_dev;
	ret = xhci_queue_slot_control(xhci, command, TRB_ENABLE_SLOT, 0);
	if (ret) {
		spin_unlock_irqrestore(&xhci->lock, flags);
		xhci_dbg(xhci, "FIXME: allocate a command ring segment\n");
		kfree(command);
		return 0;
	}
	xhci_ring_cmd_db(xhci);
	spin_unlock_irqrestore(&xhci->lock, flags);

	wait_for_completion(command->completion);

	if (!xhci->slot_id || command->status != COMP_SUCCESS) {
		xhci_err(xhci, "Error while assigning device slot ID\n");
		xhci_err(xhci, "Max number of devices this xHCI host supports is %u.\n",
				HCS_MAX_SLOTS(
					readl(&xhci->cap_regs->hcs_params1)));
		kfree(command);
		return 0;
	}

	if ((xhci->quirks & XHCI_EP_LIMIT_QUIRK)) {
		spin_lock_irqsave(&xhci->lock, flags);
		ret = xhci_reserve_host_control_ep_resources(xhci);
		if (ret) {
			spin_unlock_irqrestore(&xhci->lock, flags);
			xhci_warn(xhci, "Not enough host resources, "
					"active endpoint contexts = %u\n",
					xhci->num_active_eps);
			goto disable_slot;
		}
		spin_unlock_irqrestore(&xhci->lock, flags);
	}
	/* Use GFP_NOIO, since this function can be called from
	 * xhci_discover_or_reset_device(), which may be called as part of
	 * mass storage driver error handling.
	 */
	if (!xhci_alloc_virt_device(xhci, xhci->slot_id, udev, GFP_NOIO)) {
		xhci_warn(xhci, "Could not allocate xHCI USB device data structures\n");
		goto disable_slot;
	}
	udev->slot_id = xhci->slot_id;

#ifndef CONFIG_USB_DEFAULT_PERSIST
	/*
	 * If resetting upon resume, we can't put the controller into runtime
	 * suspend if there is a device attached.
	 */
	if (xhci->quirks & XHCI_RESET_ON_RESUME)
		pm_runtime_get_noresume(hcd->self.controller);
#endif


	kfree(command);
	/* Is this a LS or FS device under a HS hub? */
	/* Hub or peripherial? */
	return 1;

disable_slot:
	/* Disable slot, if we can do it without mem alloc */
	spin_lock_irqsave(&xhci->lock, flags);
	command->completion = NULL;
	command->status = 0;
	if (!xhci_queue_slot_control(xhci, command, TRB_DISABLE_SLOT,
				     udev->slot_id))
		xhci_ring_cmd_db(xhci);
	spin_unlock_irqrestore(&xhci->lock, flags);
	return 0;
}

/*
 * Issue an Address Device command and optionally send a corresponding
 * SetAddress request to the device.
 * We should be protected by the usb_address0_mutex in hub_wq's hub_port_init,
 * so we should only issue and wait on one address command at the same time.
 */
static int xhci_setup_device(struct usb_hcd *hcd, struct usb_device *udev,
			     enum xhci_setup_dev setup)
{
	const char *act = setup == SETUP_CONTEXT_ONLY ? "context" : "address";
	unsigned long flags;
	struct xhci_virt_device *virt_dev;
	int ret = 0;
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	struct xhci_slot_ctx *slot_ctx;
	struct xhci_input_control_ctx *ctrl_ctx;
	u64 temp_64;
	struct xhci_command *command;

	if (!udev->slot_id) {
		xhci_dbg_trace(xhci, trace_xhci_dbg_address,
				"Bad Slot ID %d", udev->slot_id);
		return -EINVAL;
	}

	virt_dev = xhci->devs[udev->slot_id];

	if (WARN_ON(!virt_dev)) {
		/*
		 * In plug/unplug torture test with an NEC controller,
		 * a zero-dereference was observed once due to virt_dev = 0.
		 * Print useful debug rather than crash if it is observed again!
		 */
		xhci_warn(xhci, "Virt dev invalid for slot_id 0x%x!\n",
			udev->slot_id);
		return -EINVAL;
	}

	command = xhci_alloc_command(xhci, false, false, GFP_KERNEL);
	if (!command)
		return -ENOMEM;

	command->in_ctx = virt_dev->in_ctx;
	command->completion = &xhci->addr_dev;

	slot_ctx = xhci_get_slot_ctx(xhci, virt_dev->in_ctx);
	ctrl_ctx = xhci_get_input_control_ctx(xhci, virt_dev->in_ctx);
	if (!ctrl_ctx) {
		xhci_warn(xhci, "%s: Could not get input context, bad type.\n",
				__func__);
		kfree(command);
		return -EINVAL;
	}
	/*
	 * If this is the first Set Address since device plug-in or
	 * virt_device realloaction after a resume with an xHCI power loss,
	 * then set up the slot context.
	 */
	if (!slot_ctx->dev_info)
		xhci_setup_addressable_virt_dev(xhci, udev);
	/* Otherwise, update the control endpoint ring enqueue pointer. */
	else
		xhci_copy_ep0_dequeue_into_input_ctx(xhci, udev);
	ctrl_ctx->add_flags = cpu_to_le32(SLOT_FLAG | EP0_FLAG);
	ctrl_ctx->drop_flags = 0;

	xhci_dbg(xhci, "Slot ID %d Input Context:\n", udev->slot_id);
	xhci_dbg_ctx(xhci, virt_dev->in_ctx, 2);
	trace_xhci_address_ctx(xhci, virt_dev->in_ctx,
				le32_to_cpu(slot_ctx->dev_info) >> 27);

	spin_lock_irqsave(&xhci->lock, flags);
	ret = xhci_queue_address_device(xhci, command, virt_dev->in_ctx->dma,
					udev->slot_id, setup);
	if (ret) {
		spin_unlock_irqrestore(&xhci->lock, flags);
		xhci_dbg_trace(xhci, trace_xhci_dbg_address,
				"FIXME: allocate a command ring segment");
		kfree(command);
		return ret;
	}
	xhci_ring_cmd_db(xhci);
	spin_unlock_irqrestore(&xhci->lock, flags);

	/* ctrl tx can take up to 5 sec; XXX: need more time for xHC? */
	wait_for_completion(command->completion);

	/* FIXME: From section 4.3.4: "Software shall be responsible for timing
	 * the SetAddress() "recovery interval" required by USB and aborting the
	 * command on a timeout.
	 */
	switch (command->status) {
	case COMP_CMD_ABORT:
	case COMP_CMD_STOP:
		xhci_warn(xhci, "Timeout while waiting for setup device command\n");
		ret = -ETIME;
		break;
	case COMP_CTX_STATE:
	case COMP_EBADSLT:
		xhci_err(xhci, "Setup ERROR: setup %s command for slot %d.\n",
			 act, udev->slot_id);
		ret = -EINVAL;
		break;
	case COMP_TX_ERR:
		dev_warn(&udev->dev, "Device not responding to setup %s.\n", act);
		ret = -EPROTO;
		break;
	case COMP_DEV_ERR:
		dev_warn(&udev->dev,
			 "ERROR: Incompatible device for setup %s command\n", act);
		ret = -ENODEV;
		break;
	case COMP_SUCCESS:
		xhci_dbg_trace(xhci, trace_xhci_dbg_address,
			       "Successful setup %s command", act);
		break;
	default:
		xhci_err(xhci,
			 "ERROR: unexpected setup %s command completion code 0x%x.\n",
			 act, command->status);
		xhci_dbg(xhci, "Slot ID %d Output Context:\n", udev->slot_id);
		xhci_dbg_ctx(xhci, virt_dev->out_ctx, 2);
		trace_xhci_address_ctx(xhci, virt_dev->out_ctx, 1);
		ret = -EINVAL;
		break;
	}
	if (ret) {
		kfree(command);
		return ret;
	}
	temp_64 = xhci_read_64(xhci, &xhci->op_regs->dcbaa_ptr);
	xhci_dbg_trace(xhci, trace_xhci_dbg_address,
			"Op regs DCBAA ptr = %#016llx", temp_64);
	xhci_dbg_trace(xhci, trace_xhci_dbg_address,
		"Slot ID %d dcbaa entry @%p = %#016llx",
		udev->slot_id,
		&xhci->dcbaa->dev_context_ptrs[udev->slot_id],
		(unsigned long long)
		le64_to_cpu(xhci->dcbaa->dev_context_ptrs[udev->slot_id]));
	xhci_dbg_trace(xhci, trace_xhci_dbg_address,
			"Output Context DMA address = %#08llx",
			(unsigned long long)virt_dev->out_ctx->dma);
	xhci_dbg(xhci, "Slot ID %d Input Context:\n", udev->slot_id);
	xhci_dbg_ctx(xhci, virt_dev->in_ctx, 2);
	trace_xhci_address_ctx(xhci, virt_dev->in_ctx,
				le32_to_cpu(slot_ctx->dev_info) >> 27);
	xhci_dbg(xhci, "Slot ID %d Output Context:\n", udev->slot_id);
	xhci_dbg_ctx(xhci, virt_dev->out_ctx, 2);
	/*
	 * USB core uses address 1 for the roothubs, so we add one to the
	 * address given back to us by the HC.
	 */
	slot_ctx = xhci_get_slot_ctx(xhci, virt_dev->out_ctx);
	trace_xhci_address_ctx(xhci, virt_dev->out_ctx,
				le32_to_cpu(slot_ctx->dev_info) >> 27);
	/* Zero the input context control for later use */
	ctrl_ctx->add_flags = 0;
	ctrl_ctx->drop_flags = 0;

	xhci_dbg_trace(xhci, trace_xhci_dbg_address,
		       "Internal device address = %d",
		       le32_to_cpu(slot_ctx->dev_state) & DEV_ADDR_MASK);
	kfree(command);
	return 0;
}

int xhci_address_device(struct usb_hcd *hcd, struct usb_device *udev)
{
	return xhci_setup_device(hcd, udev, SETUP_CONTEXT_ADDRESS);
}

int xhci_enable_device(struct usb_hcd *hcd, struct usb_device *udev)
{
	return xhci_setup_device(hcd, udev, SETUP_CONTEXT_ONLY);
}

/*
 * Transfer the port index into real index in the HW port status
 * registers. Caculate offset between the port's PORTSC register
 * and port status base. Divide the number of per port register
 * to get the real index. The raw port number bases 1.
 */
int xhci_find_raw_port_number(struct usb_hcd *hcd, int port1)
{
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	__le32 __iomem *base_addr = &xhci->op_regs->port_status_base;
	__le32 __iomem *addr;
	int raw_port;

	if (hcd->speed != HCD_USB3)
		addr = xhci->usb2_ports[port1 - 1];
	else
		addr = xhci->usb3_ports[port1 - 1];

	raw_port = (addr - base_addr)/NUM_PORT_REGS + 1;
	return raw_port;
}

/*
 * Issue an Evaluate Context command to change the Maximum Exit Latency in the
 * slot context.  If that succeeds, store the new MEL in the xhci_virt_device.
 */
static int __maybe_unused xhci_change_max_exit_latency(struct xhci_hcd *xhci,
			struct usb_device *udev, u16 max_exit_latency)
{
	struct xhci_virt_device *virt_dev;
	struct xhci_command *command;
	struct xhci_input_control_ctx *ctrl_ctx;
	struct xhci_slot_ctx *slot_ctx;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&xhci->lock, flags);

	virt_dev = xhci->devs[udev->slot_id];

	/*
	 * virt_dev might not exists yet if xHC resumed from hibernate (S4) and
	 * xHC was re-initialized. Exit latency will be set later after
	 * hub_port_finish_reset() is done and xhci->devs[] are re-allocated
	 */

	if (!virt_dev || max_exit_latency == virt_dev->current_mel) {
		spin_unlock_irqrestore(&xhci->lock, flags);
		return 0;
	}

	/* Attempt to issue an Evaluate Context command to change the MEL. */
	command = xhci->lpm_command;
	ctrl_ctx = xhci_get_input_control_ctx(xhci, command->in_ctx);
	if (!ctrl_ctx) {
		spin_unlock_irqrestore(&xhci->lock, flags);
		xhci_warn(xhci, "%s: Could not get input context, bad type.\n",
				__func__);
		return -ENOMEM;
	}

	xhci_slot_copy(xhci, command->in_ctx, virt_dev->out_ctx);
	spin_unlock_irqrestore(&xhci->lock, flags);

	ctrl_ctx->add_flags |= cpu_to_le32(SLOT_FLAG);
	slot_ctx = xhci_get_slot_ctx(xhci, command->in_ctx);
	slot_ctx->dev_info2 &= cpu_to_le32(~((u32) MAX_EXIT));
	slot_ctx->dev_info2 |= cpu_to_le32(max_exit_latency);

	xhci_dbg_trace(xhci, trace_xhci_dbg_context_change,
			"Set up evaluate context for LPM MEL change.");
	xhci_dbg(xhci, "Slot %u Input Context:\n", udev->slot_id);
	xhci_dbg_ctx(xhci, command->in_ctx, 0);

	/* Issue and wait for the evaluate context command. */
	ret = xhci_configure_endpoint(xhci, udev, command,
			true, true);
	xhci_dbg(xhci, "Slot %u Output Context:\n", udev->slot_id);
	xhci_dbg_ctx(xhci, virt_dev->out_ctx, 0);

	if (!ret) {
		spin_lock_irqsave(&xhci->lock, flags);
		virt_dev->current_mel = max_exit_latency;
		spin_unlock_irqrestore(&xhci->lock, flags);
	}
	return ret;
}

#ifdef CONFIG_PM_RUNTIME

/* BESL to HIRD Encoding array for USB2 LPM */
static int xhci_besl_encoding[16] = {125, 150, 200, 300, 400, 500, 1000, 2000,
	3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000};

/* Calculate HIRD/BESL for USB2 PORTPMSC*/
static int xhci_calculate_hird_besl(struct xhci_hcd *xhci,
					struct usb_device *udev)
{
	int u2del, besl, besl_host;
	int besl_device = 0;
	u32 field;

	u2del = HCS_U2_LATENCY(xhci->hcs_params3);
	field = le32_to_cpu(udev->bos->ext_cap->bmAttributes);

	if (field & USB_BESL_SUPPORT) {
		for (besl_host = 0; besl_host < 16; besl_host++) {
			if (xhci_besl_encoding[besl_host] >= u2del)
				break;
		}
		/* Use baseline BESL value as default */
		if (field & USB_BESL_BASELINE_VALID)
			besl_device = USB_GET_BESL_BASELINE(field);
		else if (field & USB_BESL_DEEP_VALID)
			besl_device = USB_GET_BESL_DEEP(field);
	} else {
		if (u2del <= 50)
			besl_host = 0;
		else
			besl_host = (u2del - 51) / 75 + 1;
	}

	besl = besl_host + besl_device;
	if (besl > 15)
		besl = 15;

	return besl;
}

/* Calculate BESLD, L1 timeout and HIRDM for USB2 PORTHLPMC */
static int xhci_calculate_usb2_hw_lpm_params(struct usb_device *udev)
{
	u32 field;
	int l1;
	int besld = 0;
	int hirdm = 0;

	field = le32_to_cpu(udev->bos->ext_cap->bmAttributes);

	/* xHCI l1 is set in steps of 256us, xHCI 1.0 section 5.4.11.2 */
	l1 = udev->l1_params.timeout / 256;

	/* device has preferred BESLD */
	if (field & USB_BESL_DEEP_VALID) {
		besld = USB_GET_BESL_DEEP(field);
		hirdm = 1;
	}

	return PORT_BESLD(besld) | PORT_L1_TIMEOUT(l1) | PORT_HIRDM(hirdm);
}

int xhci_set_usb2_hardware_lpm(struct usb_hcd *hcd,
			struct usb_device *udev, int enable)
{
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);
	__le32 __iomem	**port_array;
	__le32 __iomem	*pm_addr, *hlpm_addr;
	u32		pm_val, hlpm_val, field;
	unsigned int	port_num;
	unsigned long	flags;
	int		hird, exit_latency;
	int		ret;

	if (hcd->speed == HCD_USB3 || !xhci->hw_lpm_support ||
			!udev->lpm_capable)
		return -EPERM;

	if (!udev->parent || udev->parent->parent ||
			udev->descriptor.bDeviceClass == USB_CLASS_HUB)
		return -EPERM;

	if (udev->usb2_hw_lpm_capable != 1)
		return -EPERM;

	spin_lock_irqsave(&xhci->lock, flags);

	port_array = xhci->usb2_ports;
	port_num = udev->portnum - 1;
	pm_addr = port_array[port_num] + PORTPMSC;
	pm_val = readl(pm_addr);
	hlpm_addr = port_array[port_num] + PORTHLPMC;
	field = le32_to_cpu(udev->bos->ext_cap->bmAttributes);

	xhci_dbg(xhci, "%s port %d USB2 hardware LPM\n",
			enable ? "enable" : "disable", port_num + 1);

	if (enable) {
		/* Host supports BESL timeout instead of HIRD */
		if (udev->usb2_hw_lpm_besl_capable) {
			/* if device doesn't have a preferred BESL value use a
			 * default one which works with mixed HIRD and BESL
			 * systems. See XHCI_DEFAULT_BESL definition in xhci.h
			 */
			if ((field & USB_BESL_SUPPORT) &&
			    (field & USB_BESL_BASELINE_VALID))
				hird = USB_GET_BESL_BASELINE(field);
			else
				hird = udev->l1_params.besl;

			exit_latency = xhci_besl_encoding[hird];
			spin_unlock_irqrestore(&xhci->lock, flags);

			/* USB 3.0 code dedicate one xhci->lpm_command->in_ctx
			 * input context for link powermanagement evaluate
			 * context commands. It is protected by hcd->bandwidth
			 * mutex and is shared by all devices. We need to set
			 * the max ext latency in USB 2 BESL LPM as well, so
			 * use the same mutex and xhci_change_max_exit_latency()
			 */
			mutex_lock(hcd->bandwidth_mutex);
			ret = xhci_change_max_exit_latency(xhci, udev,
							   exit_latency);
			mutex_unlock(hcd->bandwidth_mutex);

			if (ret < 0)
				return ret;
			spin_lock_irqsave(&xhci->lock, flags);

			hlpm_val = xhci_calculate_usb2_hw_lpm_params(udev);
			writel(hlpm_val, hlpm_addr);
			/* flush write */
			readl(hlpm_addr);
		} else {
			hird = xhci_calculate_hird_besl(xhci, udev);
		}

		pm_val &= ~PORT_HIRD_MASK;
		pm_val |= PORT_HIRD(hird) | PORT_RWE | PORT_L1DS(udev->slot_id);
		writel(pm_val, pm_addr);
		pm_val = readl(pm_addr);
		pm_val |= PORT_HLE;
		writel(pm_val, pm_addr);
		/* flush write */
		readl(pm_addr);
	} else {
		pm_val &= ~(PORT_HLE | PORT_RWE | PORT_HIRD_MASK | PORT_L1DS_MASK);
		writel(pm_val, pm_addr);
		/* flush write */
		readl(pm_addr);
		if (udev->usb2_hw_lpm_besl_capable) {
			spin_unlock_irqrestore(&xhci->lock, flags);
			mutex_lock(hcd->bandwidth_mutex);
			xhci_change_max_exit_latency(xhci, udev, 0);
			mutex_unlock(hcd->bandwidth_mutex);
			return 0;
		}
	}

	spin_unlock_irqrestore(&xhci->lock, flags);
	return 0;
}

/* check if a usb2 port supports a given extened capability protocol
 * only USB2 ports extended protocol capability values are cached.
 * Return 1 if capability is supported
 */
static int xhci_check_usb2_port_capability(struct xhci_hcd *xhci, int port,
					   unsigned capability)
{
	u32 port_offset, port_count;
	int i;

	for (i = 0; i < xhci->num_ext_caps; i++) {
		if (xhci->ext_caps[i] & capability) {
			/* port offsets starts at 1 */
			port_offset = XHCI_EXT_PORT_OFF(xhci->ext_caps[i]) - 1;
			port_count = XHCI_EXT_PORT_COUNT(xhci->ext_caps[i]);
			if (port >= port_offset &&
			    port < port_offset + port_count)
				return 1;
		}
	}
	return 0;
}

int xhci_update_device(struct usb_hcd *hcd, struct usb_device *udev)
{
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);
	int		portnum = udev->portnum - 1;

	if (hcd->speed == HCD_USB3 || !xhci->sw_lpm_support ||
			!udev->lpm_capable)
		return 0;

	/* we only support lpm for non-hub device connected to root hub yet */
	if (!udev->parent || udev->parent->parent ||
			udev->descriptor.bDeviceClass == USB_CLASS_HUB)
		return 0;

	if (xhci->hw_lpm_support == 1 &&
			xhci_check_usb2_port_capability(
				xhci, portnum, XHCI_HLC)) {
		udev->usb2_hw_lpm_capable = 1;
		udev->l1_params.timeout = XHCI_L1_TIMEOUT;
		udev->l1_params.besl = XHCI_DEFAULT_BESL;
		if (xhci_check_usb2_port_capability(xhci, portnum,
					XHCI_BLC))
			udev->usb2_hw_lpm_besl_capable = 1;
	}

	return 0;
}

#else

int xhci_set_usb2_hardware_lpm(struct usb_hcd *hcd,
				struct usb_device *udev, int enable)
{
	return 0;
}

int xhci_update_device(struct usb_hcd *hcd, struct usb_device *udev)
{
	return 0;
}

#endif /* CONFIG_PM_RUNTIME */

/*---------------------- USB 3.0 Link PM functions ------------------------*/

#ifdef CONFIG_PM
/* Service interval in nanoseconds = 2^(bInterval - 1) * 125us * 1000ns / 1us */
static unsigned long long xhci_service_interval_to_ns(
		struct usb_endpoint_descriptor *desc)
{
	return (1ULL << (desc->bInterval - 1)) * 125 * 1000;
}

static u16 xhci_get_timeout_no_hub_lpm(struct usb_device *udev,
		enum usb3_link_state state)
{
	unsigned long long sel;
	unsigned long long pel;
	unsigned int max_sel_pel;
	char *state_name;

	switch (state) {
	case USB3_LPM_U1:
		/* Convert SEL and PEL stored in nanoseconds to microseconds */
		sel = DIV_ROUND_UP(udev->u1_params.sel, 1000);
		pel = DIV_ROUND_UP(udev->u1_params.pel, 1000);
		max_sel_pel = USB3_LPM_MAX_U1_SEL_PEL;
		state_name = "U1";
		break;
	case USB3_LPM_U2:
		sel = DIV_ROUND_UP(udev->u2_params.sel, 1000);
		pel = DIV_ROUND_UP(udev->u2_params.pel, 1000);
		max_sel_pel = USB3_LPM_MAX_U2_SEL_PEL;
		state_name = "U2";
		break;
	default:
		dev_warn(&udev->dev, "%s: Can't get timeout for non-U1 or U2 state.\n",
				__func__);
		return USB3_LPM_DISABLED;
	}

	if (sel <= max_sel_pel && pel <= max_sel_pel)
		return USB3_LPM_DEVICE_INITIATED;

	if (sel > max_sel_pel)
		dev_dbg(&udev->dev, "Device-initiated %s disabled "
				"due to long SEL %llu ms\n",
				state_name, sel);
	else
		dev_dbg(&udev->dev, "Device-initiated %s disabled "
				"due to long PEL %llu ms\n",
				state_name, pel);
	return USB3_LPM_DISABLED;
}

/* The U1 timeout should be the maximum of the following values:
 *  - For control endpoints, U1 system exit latency (SEL) * 3
 *  - For bulk endpoints, U1 SEL * 5
 *  - For interrupt endpoints:
 *    - Notification EPs, U1 SEL * 3
 *    - Periodic EPs, max(105% of bInterval, U1 SEL * 2)
 *  - For isochronous endpoints, max(105% of bInterval, U1 SEL * 2)
 */
static unsigned long long xhci_calculate_intel_u1_timeout(
		struct usb_device *udev,
		struct usb_endpoint_descriptor *desc)
{
	unsigned long long timeout_ns;
	int ep_type;
	int intr_type;

	ep_type = usb_endpoint_type(desc);
	switch (ep_type) {
	case USB_ENDPOINT_XFER_CONTROL:
		timeout_ns = udev->u1_params.sel * 3;
		break;
	case USB_ENDPOINT_XFER_BULK:
		timeout_ns = udev->u1_params.sel * 5;
		break;
	case USB_ENDPOINT_XFER_INT:
		intr_type = usb_endpoint_interrupt_type(desc);
		if (intr_type == USB_ENDPOINT_INTR_NOTIFICATION) {
			timeout_ns = udev->u1_params.sel * 3;
			break;
		}
		/* Otherwise the calculation is the same as isoc eps */
	case USB_ENDPOINT_XFER_ISOC:
		timeout_ns = xhci_service_interval_to_ns(desc);
		timeout_ns = DIV_ROUND_UP_ULL(timeout_ns * 105, 100);
		if (timeout_ns < udev->u1_params.sel * 2)
			timeout_ns = udev->u1_params.sel * 2;
		break;
	default:
		return 0;
	}

	return timeout_ns;
}

/* Returns the hub-encoded U1 timeout value. */
static u16 xhci_calculate_u1_timeout(struct xhci_hcd *xhci,
		struct usb_device *udev,
		struct usb_endpoint_descriptor *desc)
{
	unsigned long long timeout_ns;

	if (xhci->quirks & XHCI_INTEL_HOST)
		timeout_ns = xhci_calculate_intel_u1_timeout(udev, desc);
	else
		timeout_ns = udev->u1_params.sel;

	/* The U1 timeout is encoded in 1us intervals.
	 * Don't return a timeout of zero, because that's USB3_LPM_DISABLED.
	 */
	if (timeout_ns == USB3_LPM_DISABLED)
		timeout_ns = 1;
	else
		timeout_ns = DIV_ROUND_UP_ULL(timeout_ns, 1000);

	/* If the necessary timeout value is bigger than what we can set in the
	 * USB 3.0 hub, we have to disable hub-initiated U1.
	 */
	if (timeout_ns <= USB3_LPM_U1_MAX_TIMEOUT)
		return timeout_ns;
	dev_dbg(&udev->dev, "Hub-initiated U1 disabled "
			"due to long timeout %llu ms\n", timeout_ns);
	return xhci_get_timeout_no_hub_lpm(udev, USB3_LPM_U1);
}

/* The U2 timeout should be the maximum of:
 *  - 10 ms (to avoid the bandwidth impact on the scheduler)
 *  - largest bInterval of any active periodic endpoint (to avoid going
 *    into lower power link states between intervals).
 *  - the U2 Exit Latency of the device
 */
static unsigned long long xhci_calculate_intel_u2_timeout(
		struct usb_device *udev,
		struct usb_endpoint_descriptor *desc)
{
	unsigned long long timeout_ns;
	unsigned long long u2_del_ns;

	timeout_ns = 10 * 1000 * 1000;

	if ((usb_endpoint_xfer_int(desc) || usb_endpoint_xfer_isoc(desc)) &&
			(xhci_service_interval_to_ns(desc) > timeout_ns))
		timeout_ns = xhci_service_interval_to_ns(desc);

	u2_del_ns = le16_to_cpu(udev->bos->ss_cap->bU2DevExitLat) * 1000ULL;
	if (u2_del_ns > timeout_ns)
		timeout_ns = u2_del_ns;

	return timeout_ns;
}

/* Returns the hub-encoded U2 timeout value. */
static u16 xhci_calculate_u2_timeout(struct xhci_hcd *xhci,
		struct usb_device *udev,
		struct usb_endpoint_descriptor *desc)
{
	unsigned long long timeout_ns;

	if (xhci->quirks & XHCI_INTEL_HOST)
		timeout_ns = xhci_calculate_intel_u2_timeout(udev, desc);
	else
		timeout_ns = udev->u2_params.sel;

	/* The U2 timeout is encoded in 256us intervals */
	timeout_ns = DIV_ROUND_UP_ULL(timeout_ns, 256 * 1000);
	/* If the necessary timeout value is bigger than what we can set in the
	 * USB 3.0 hub, we have to disable hub-initiated U2.
	 */
	if (timeout_ns <= USB3_LPM_U2_MAX_TIMEOUT)
		return timeout_ns;
	dev_dbg(&udev->dev, "Hub-initiated U2 disabled "
			"due to long timeout %llu ms\n", timeout_ns);
	return xhci_get_timeout_no_hub_lpm(udev, USB3_LPM_U2);
}

static u16 xhci_call_host_update_timeout_for_endpoint(struct xhci_hcd *xhci,
		struct usb_device *udev,
		struct usb_endpoint_descriptor *desc,
		enum usb3_link_state state,
		u16 *timeout)
{
	if (state == USB3_LPM_U1)
		return xhci_calculate_u1_timeout(xhci, udev, desc);
	else if (state == USB3_LPM_U2)
		return xhci_calculate_u2_timeout(xhci, udev, desc);

	return USB3_LPM_DISABLED;
}

static int xhci_update_timeout_for_endpoint(struct xhci_hcd *xhci,
		struct usb_device *udev,
		struct usb_endpoint_descriptor *desc,
		enum usb3_link_state state,
		u16 *timeout)
{
	u16 alt_timeout;

	alt_timeout = xhci_call_host_update_timeout_for_endpoint(xhci, udev,
		desc, state, timeout);

	/* If we found we can't enable hub-initiated LPM, or
	 * the U1 or U2 exit latency was too high to allow
	 * device-initiated LPM as well, just stop searching.
	 */
	if (alt_timeout == USB3_LPM_DISABLED ||
			alt_timeout == USB3_LPM_DEVICE_INITIATED) {
		*timeout = alt_timeout;
		return -E2BIG;
	}
	if (alt_timeout > *timeout)
		*timeout = alt_timeout;
	return 0;
}

static int xhci_update_timeout_for_interface(struct xhci_hcd *xhci,
		struct usb_device *udev,
		struct usb_host_interface *alt,
		enum usb3_link_state state,
		u16 *timeout)
{
	int j;

	for (j = 0; j < alt->desc.bNumEndpoints; j++) {
		if (xhci_update_timeout_for_endpoint(xhci, udev,
					&alt->endpoint[j].desc, state, timeout))
			return -E2BIG;
		continue;
	}
	return 0;
}

static int xhci_check_intel_tier_policy(struct usb_device *udev,
		enum usb3_link_state state)
{
	struct usb_device *parent;
	unsigned int num_hubs;

	if (state == USB3_LPM_U2)
		return 0;

	/* Don't enable U1 if the device is on a 2nd tier hub or lower. */
	for (parent = udev->parent, num_hubs = 0; parent->parent;
			parent = parent->parent)
		num_hubs++;

	if (num_hubs < 2)
		return 0;

	dev_dbg(&udev->dev, "Disabling U1 link state for device"
			" below second-tier hub.\n");
	dev_dbg(&udev->dev, "Plug device into first-tier hub "
			"to decrease power consumption.\n");
	return -E2BIG;
}

static int xhci_check_tier_policy(struct xhci_hcd *xhci,
		struct usb_device *udev,
		enum usb3_link_state state)
{
	if (xhci->quirks & XHCI_INTEL_HOST)
		return xhci_check_intel_tier_policy(udev, state);
	else
		return 0;
}

/* Returns the U1 or U2 timeout that should be enabled.
 * If the tier check or timeout setting functions return with a non-zero exit
 * code, that means the timeout value has been finalized and we shouldn't look
 * at any more endpoints.
 */
static u16 xhci_calculate_lpm_timeout(struct usb_hcd *hcd,
			struct usb_device *udev, enum usb3_link_state state)
{
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	struct usb_host_config *config;
	char *state_name;
	int i;
	u16 timeout = USB3_LPM_DISABLED;

	if (state == USB3_LPM_U1)
		state_name = "U1";
	else if (state == USB3_LPM_U2)
		state_name = "U2";
	else {
		dev_warn(&udev->dev, "Can't enable unknown link state %i\n",
				state);
		return timeout;
	}

	if (xhci_check_tier_policy(xhci, udev, state) < 0)
		return timeout;

	/* Gather some information about the currently installed configuration
	 * and alternate interface settings.
	 */
	if (xhci_update_timeout_for_endpoint(xhci, udev, &udev->ep0.desc,
			state, &timeout))
		return timeout;

	config = udev->actconfig;
	if (!config)
		return timeout;

	for (i = 0; i < config->desc.bNumInterfaces; i++) {
		struct usb_driver *driver;
		struct usb_interface *intf = config->interface[i];

		if (!intf)
			continue;

		/* Check if any currently bound drivers want hub-initiated LPM
		 * disabled.
		 */
		if (intf->dev.driver) {
			driver = to_usb_driver(intf->dev.driver);
			if (driver && driver->disable_hub_initiated_lpm) {
				dev_dbg(&udev->dev, "Hub-initiated %s disabled "
						"at request of driver %s\n",
						state_name, driver->name);
				return xhci_get_timeout_no_hub_lpm(udev, state);
			}
		}

		/* Not sure how this could happen... */
		if (!intf->cur_altsetting)
			continue;

		if (xhci_update_timeout_for_interface(xhci, udev,
					intf->cur_altsetting,
					state, &timeout))
			return timeout;
	}
	return timeout;
}

static int calculate_max_exit_latency(struct usb_device *udev,
		enum usb3_link_state state_changed,
		u16 hub_encoded_timeout)
{
	unsigned long long u1_mel_us = 0;
	unsigned long long u2_mel_us = 0;
	unsigned long long mel_us = 0;
	bool disabling_u1;
	bool disabling_u2;
	bool enabling_u1;
	bool enabling_u2;

	disabling_u1 = (state_changed == USB3_LPM_U1 &&
			hub_encoded_timeout == USB3_LPM_DISABLED);
	disabling_u2 = (state_changed == USB3_LPM_U2 &&
			hub_encoded_timeout == USB3_LPM_DISABLED);

	enabling_u1 = (state_changed == USB3_LPM_U1 &&
			hub_encoded_timeout != USB3_LPM_DISABLED);
	enabling_u2 = (state_changed == USB3_LPM_U2 &&
			hub_encoded_timeout != USB3_LPM_DISABLED);

	/* If U1 was already enabled and we're not disabling it,
	 * or we're going to enable U1, account for the U1 max exit latency.
	 */
	if ((udev->u1_params.timeout != USB3_LPM_DISABLED && !disabling_u1) ||
			enabling_u1)
		u1_mel_us = DIV_ROUND_UP(udev->u1_params.mel, 1000);
	if ((udev->u2_params.timeout != USB3_LPM_DISABLED && !disabling_u2) ||
			enabling_u2)
		u2_mel_us = DIV_ROUND_UP(udev->u2_params.mel, 1000);

	if (u1_mel_us > u2_mel_us)
		mel_us = u1_mel_us;
	else
		mel_us = u2_mel_us;
	/* xHCI host controller max exit latency field is only 16 bits wide. */
	if (mel_us > MAX_EXIT) {
		dev_warn(&udev->dev, "Link PM max exit latency of %lluus "
				"is too big.\n", mel_us);
		return -E2BIG;
	}
	return mel_us;
}

/* Returns the USB3 hub-encoded value for the U1/U2 timeout. */
int xhci_enable_usb3_lpm_timeout(struct usb_hcd *hcd,
			struct usb_device *udev, enum usb3_link_state state)
{
	struct xhci_hcd	*xhci;
	u16 hub_encoded_timeout;
	int mel;
	int ret;

	xhci = hcd_to_xhci(hcd);
	/* The LPM timeout values are pretty host-controller specific, so don't
	 * enable hub-initiated timeouts unless the vendor has provided
	 * information about their timeout algorithm.
	 */
	if (!xhci || !(xhci->quirks & XHCI_LPM_SUPPORT) ||
			!xhci->devs[udev->slot_id])
		return USB3_LPM_DISABLED;

	hub_encoded_timeout = xhci_calculate_lpm_timeout(hcd, udev, state);
	mel = calculate_max_exit_latency(udev, state, hub_encoded_timeout);
	if (mel < 0) {
		/* Max Exit Latency is too big, disable LPM. */
		hub_encoded_timeout = USB3_LPM_DISABLED;
		mel = 0;
	}

	ret = xhci_change_max_exit_latency(xhci, udev, mel);
	if (ret)
		return ret;
	return hub_encoded_timeout;
}

int xhci_disable_usb3_lpm_timeout(struct usb_hcd *hcd,
			struct usb_device *udev, enum usb3_link_state state)
{
	struct xhci_hcd	*xhci;
	u16 mel;
	int ret;

	xhci = hcd_to_xhci(hcd);
	if (!xhci || !(xhci->quirks & XHCI_LPM_SUPPORT) ||
			!xhci->devs[udev->slot_id])
		return 0;

	mel = calculate_max_exit_latency(udev, state, USB3_LPM_DISABLED);
	ret = xhci_change_max_exit_latency(xhci, udev, mel);
	if (ret)
		return ret;
	return 0;
}
#else /* CONFIG_PM */

int xhci_enable_usb3_lpm_timeout(struct usb_hcd *hcd,
			struct usb_device *udev, enum usb3_link_state state)
{
	return USB3_LPM_DISABLED;
}

int xhci_disable_usb3_lpm_timeout(struct usb_hcd *hcd,
			struct usb_device *udev, enum usb3_link_state state)
{
	return 0;
}
#endif	/* CONFIG_PM */

/*-------------------------------------------------------------------------*/

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
	config_cmd = xhci_alloc_command(xhci, true, true, mem_flags);
	if (!config_cmd) {
		xhci_dbg(xhci, "Could not allocate xHCI command structure.\n");
		return -ENOMEM;
	}
	ctrl_ctx = xhci_get_input_control_ctx(xhci, config_cmd->in_ctx);
	if (!ctrl_ctx) {
		xhci_warn(xhci, "%s: Could not get input context, bad type.\n",
				__func__);
		xhci_free_command(xhci, config_cmd);
		return -ENOMEM;
	}

	spin_lock_irqsave(&xhci->lock, flags);
	if (hdev->speed == USB_SPEED_HIGH &&
			xhci_alloc_tt_info(xhci, vdev, hdev, tt, GFP_ATOMIC)) {
		xhci_dbg(xhci, "Could not allocate xHCI TT structure.\n");
		xhci_free_command(xhci, config_cmd);
		spin_unlock_irqrestore(&xhci->lock, flags);
		return -ENOMEM;
	}

	xhci_slot_copy(xhci, config_cmd->in_ctx, vdev->out_ctx);
	ctrl_ctx->add_flags |= cpu_to_le32(SLOT_FLAG);
	slot_ctx = xhci_get_slot_ctx(xhci, config_cmd->in_ctx);
	slot_ctx->dev_info |= cpu_to_le32(DEV_HUB);
	if (tt->multi)
		slot_ctx->dev_info |= cpu_to_le32(DEV_MTT);
	if (xhci->hci_version > 0x95) {
		xhci_dbg(xhci, "xHCI version %x needs hub "
				"TT think time and number of ports\n",
				(unsigned int) xhci->hci_version);
		slot_ctx->dev_info2 |= cpu_to_le32(XHCI_MAX_PORTS(hdev->maxchild));
		/* Set TT think time - convert from ns to FS bit times.
		 * 0 = 8 FS bit times, 1 = 16 FS bit times,
		 * 2 = 24 FS bit times, 3 = 32 FS bit times.
		 *
		 * xHCI 1.0: this field shall be 0 if the device is not a
		 * High-spped hub.
		 */
		think_time = tt->think_time;
		if (think_time != 0)
			think_time = (think_time / 666) - 1;
		if (xhci->hci_version < 0x100 || hdev->speed == USB_SPEED_HIGH)
			slot_ctx->tt_info |=
				cpu_to_le32(TT_THINK_TIME(think_time));
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
	return readl(&xhci->run_regs->microframe_index) >> 3;
}

int xhci_gen_setup(struct usb_hcd *hcd, xhci_get_quirks_t get_quirks)
{
	struct xhci_hcd		*xhci;
	struct device		*dev = hcd->self.controller;
	int			retval;

	/* Accept arbitrarily long scatter-gather lists */
	hcd->self.sg_tablesize = ~0;

	/* support to build packet from discontinuous buffers */
	hcd->self.no_sg_constraint = 1;

	/* XHCI controllers don't stop the ep queue on short packets :| */
	hcd->self.no_stop_on_short = 1;

	if (usb_hcd_is_primary_hcd(hcd)) {
		xhci = kzalloc(sizeof(struct xhci_hcd), GFP_KERNEL);
		if (!xhci)
			return -ENOMEM;
		*((struct xhci_hcd **) hcd->hcd_priv) = xhci;
		xhci->main_hcd = hcd;
		/* Mark the first roothub as being USB 2.0.
		 * The xHCI driver will register the USB 3.0 roothub.
		 */
		hcd->speed = HCD_USB2;
		hcd->self.root_hub->speed = USB_SPEED_HIGH;
		/*
		 * USB 2.0 roothub under xHCI has an integrated TT,
		 * (rate matching hub) as opposed to having an OHCI/UHCI
		 * companion controller.
		 */
		hcd->has_tt = 1;
	} else {
		/* xHCI private pointer was set in xhci_pci_probe for the second
		 * registered roothub.
		 */
		return 0;
	}

	xhci->cap_regs = hcd->regs;
	xhci->op_regs = hcd->regs +
		HC_LENGTH(readl(&xhci->cap_regs->hc_capbase));
	xhci->run_regs = hcd->regs +
		(readl(&xhci->cap_regs->run_regs_off) & RTSOFF_MASK);
	/* Cache read-only capability registers */
	xhci->hcs_params1 = readl(&xhci->cap_regs->hcs_params1);
	xhci->hcs_params2 = readl(&xhci->cap_regs->hcs_params2);
	xhci->hcs_params3 = readl(&xhci->cap_regs->hcs_params3);
	xhci->hcc_params = readl(&xhci->cap_regs->hc_capbase);
	xhci->hci_version = HC_VERSION(xhci->hcc_params);
	xhci->hcc_params = readl(&xhci->cap_regs->hcc_params);
	xhci_print_registers(xhci);

	xhci->quirks = quirks;

	get_quirks(dev, xhci);

	/* In xhci controllers which follow xhci 1.0 spec gives a spurious
	 * success event after a short transfer. This quirk will ignore such
	 * spurious event.
	 */
	if (xhci->hci_version > 0x96)
		xhci->quirks |= XHCI_SPURIOUS_SUCCESS;

	/* Make sure the HC is halted. */
	retval = xhci_halt(xhci);
	if (retval)
		goto error;

	xhci_dbg(xhci, "Resetting HCD\n");
	/* Reset the internal HC memory state and registers. */
	retval = xhci_reset(xhci);
	if (retval)
		goto error;
	xhci_dbg(xhci, "Reset complete\n");

	/* Set dma_mask and coherent_dma_mask to 64-bits,
	 * if xHC supports 64-bit addressing */
	if (HCC_64BIT_ADDR(xhci->hcc_params) &&
			!dma_set_mask(dev, DMA_BIT_MASK(64))) {
		xhci_dbg(xhci, "Enabling 64-bit DMA addresses.\n");
		dma_set_coherent_mask(dev, DMA_BIT_MASK(64));
	}

	xhci_dbg(xhci, "Calling HCD init\n");
	/* Initialize HCD and host controller data structures. */
	retval = xhci_init(hcd);
	if (retval)
		goto error;
	xhci_dbg(xhci, "Called HCD init\n");
	return 0;
error:
	kfree(xhci);
	return retval;
}
EXPORT_SYMBOL_GPL(xhci_gen_setup);

static const struct hc_driver xhci_hc_driver = {
	.description =		"xhci-hcd",
	.product_desc =		"xHCI Host Controller",
	.hcd_priv_size =	sizeof(struct xhci_hcd *),

	/*
	 * generic hardware linkage
	 */
	.irq =			xhci_irq,
	.flags =		HCD_MEMORY | HCD_USB3 | HCD_SHARED,

	/*
	 * basic lifecycle operations
	 */
	.reset =		NULL, /* set in xhci_init_driver() */
	.start =		xhci_run,
	.stop =			xhci_stop,
	.shutdown =		xhci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue =		xhci_urb_enqueue,
	.urb_dequeue =		xhci_urb_dequeue,
	.alloc_dev =		xhci_alloc_dev,
	.free_dev =		xhci_free_dev,
	.alloc_streams =	xhci_alloc_streams,
	.free_streams =		xhci_free_streams,
	.add_endpoint =		xhci_add_endpoint,
	.drop_endpoint =	xhci_drop_endpoint,
	.endpoint_reset =	xhci_endpoint_reset,
	.check_bandwidth =	xhci_check_bandwidth,
	.reset_bandwidth =	xhci_reset_bandwidth,
	.address_device =	xhci_address_device,
	.enable_device =	xhci_enable_device,
	.update_hub_device =	xhci_update_hub_device,
	.reset_device =		xhci_discover_or_reset_device,

	/*
	 * scheduling support
	 */
	.get_frame_number =	xhci_get_frame,

	/*
	 * root hub support
	 */
	.hub_control =		xhci_hub_control,
	.hub_status_data =	xhci_hub_status_data,
	.bus_suspend =		xhci_bus_suspend,
	.bus_resume =		xhci_bus_resume,

	/*
	 * call back when device connected and addressed
	 */
	.update_device =        xhci_update_device,
	.set_usb2_hw_lpm =	xhci_set_usb2_hardware_lpm,
	.enable_usb3_lpm_timeout =	xhci_enable_usb3_lpm_timeout,
	.disable_usb3_lpm_timeout =	xhci_disable_usb3_lpm_timeout,
	.find_raw_port_number =	xhci_find_raw_port_number,
};

void xhci_init_driver(struct hc_driver *drv, int (*setup_fn)(struct usb_hcd *))
{
	BUG_ON(!setup_fn);
	*drv = xhci_hc_driver;
	drv->reset = setup_fn;
}
EXPORT_SYMBOL_GPL(xhci_init_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_LICENSE("GPL");

static int __init xhci_hcd_init(void)
{
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
	return 0;
}
module_init(xhci_hcd_init);
