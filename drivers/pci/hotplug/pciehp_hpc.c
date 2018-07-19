// SPDX-License-Identifier: GPL-2.0+
/*
 * PCI Express PCI Hot Plug Driver
 *
 * Copyright (C) 1995,2001 Compaq Computer Corporation
 * Copyright (C) 2001 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001 IBM Corp.
 * Copyright (C) 2003-2004 Intel Corporation
 *
 * All rights reserved.
 *
 * Send feedback to <greg@kroah.com>,<kristen.c.accardi@intel.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/signal.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/slab.h>

#include "../pci.h"
#include "pciehp.h"

static inline struct pci_dev *ctrl_dev(struct controller *ctrl)
{
	return ctrl->pcie->port;
}

static irqreturn_t pciehp_isr(int irq, void *dev_id);
static irqreturn_t pciehp_ist(int irq, void *dev_id);
static int pciehp_poll(void *data);

static inline int pciehp_request_irq(struct controller *ctrl)
{
	int retval, irq = ctrl->pcie->irq;

	if (pciehp_poll_mode) {
		ctrl->poll_thread = kthread_run(&pciehp_poll, ctrl,
						"pciehp_poll-%s",
						slot_name(ctrl->slot));
		return PTR_ERR_OR_ZERO(ctrl->poll_thread);
	}

	/* Installs the interrupt handler */
	retval = request_threaded_irq(irq, pciehp_isr, pciehp_ist,
				      IRQF_SHARED, MY_NAME, ctrl);
	if (retval)
		ctrl_err(ctrl, "Cannot get irq %d for the hotplug controller\n",
			 irq);
	return retval;
}

static inline void pciehp_free_irq(struct controller *ctrl)
{
	if (pciehp_poll_mode)
		kthread_stop(ctrl->poll_thread);
	else
		free_irq(ctrl->pcie->irq, ctrl);
}

static int pcie_poll_cmd(struct controller *ctrl, int timeout)
{
	struct pci_dev *pdev = ctrl_dev(ctrl);
	u16 slot_status;

	while (true) {
		pcie_capability_read_word(pdev, PCI_EXP_SLTSTA, &slot_status);
		if (slot_status == (u16) ~0) {
			ctrl_info(ctrl, "%s: no response from device\n",
				  __func__);
			return 0;
		}

		if (slot_status & PCI_EXP_SLTSTA_CC) {
			pcie_capability_write_word(pdev, PCI_EXP_SLTSTA,
						   PCI_EXP_SLTSTA_CC);
			return 1;
		}
		if (timeout < 0)
			break;
		msleep(10);
		timeout -= 10;
	}
	return 0;	/* timeout */
}

static void pcie_wait_cmd(struct controller *ctrl)
{
	unsigned int msecs = pciehp_poll_mode ? 2500 : 1000;
	unsigned long duration = msecs_to_jiffies(msecs);
	unsigned long cmd_timeout = ctrl->cmd_started + duration;
	unsigned long now, timeout;
	int rc;

	/*
	 * If the controller does not generate notifications for command
	 * completions, we never need to wait between writes.
	 */
	if (NO_CMD_CMPL(ctrl))
		return;

	if (!ctrl->cmd_busy)
		return;

	/*
	 * Even if the command has already timed out, we want to call
	 * pcie_poll_cmd() so it can clear PCI_EXP_SLTSTA_CC.
	 */
	now = jiffies;
	if (time_before_eq(cmd_timeout, now))
		timeout = 1;
	else
		timeout = cmd_timeout - now;

	if (ctrl->slot_ctrl & PCI_EXP_SLTCTL_HPIE &&
	    ctrl->slot_ctrl & PCI_EXP_SLTCTL_CCIE)
		rc = wait_event_timeout(ctrl->queue, !ctrl->cmd_busy, timeout);
	else
		rc = pcie_poll_cmd(ctrl, jiffies_to_msecs(timeout));

	if (!rc)
		ctrl_info(ctrl, "Timeout on hotplug command %#06x (issued %u msec ago)\n",
			  ctrl->slot_ctrl,
			  jiffies_to_msecs(jiffies - ctrl->cmd_started));
}

#define CC_ERRATUM_MASK		(PCI_EXP_SLTCTL_PCC |	\
				 PCI_EXP_SLTCTL_PIC |	\
				 PCI_EXP_SLTCTL_AIC |	\
				 PCI_EXP_SLTCTL_EIC)

static void pcie_do_write_cmd(struct controller *ctrl, u16 cmd,
			      u16 mask, bool wait)
{
	struct pci_dev *pdev = ctrl_dev(ctrl);
	u16 slot_ctrl_orig, slot_ctrl;

	mutex_lock(&ctrl->ctrl_lock);

	/*
	 * Always wait for any previous command that might still be in progress
	 */
	pcie_wait_cmd(ctrl);

	pcie_capability_read_word(pdev, PCI_EXP_SLTCTL, &slot_ctrl);
	if (slot_ctrl == (u16) ~0) {
		ctrl_info(ctrl, "%s: no response from device\n", __func__);
		goto out;
	}

	slot_ctrl_orig = slot_ctrl;
	slot_ctrl &= ~mask;
	slot_ctrl |= (cmd & mask);
	ctrl->cmd_busy = 1;
	smp_mb();
	pcie_capability_write_word(pdev, PCI_EXP_SLTCTL, slot_ctrl);
	ctrl->cmd_started = jiffies;
	ctrl->slot_ctrl = slot_ctrl;

	/*
	 * Controllers with the Intel CF118 and similar errata advertise
	 * Command Completed support, but they only set Command Completed
	 * if we change the "Control" bits for power, power indicator,
	 * attention indicator, or interlock.  If we only change the
	 * "Enable" bits, they never set the Command Completed bit.
	 */
	if (pdev->broken_cmd_compl &&
	    (slot_ctrl_orig & CC_ERRATUM_MASK) == (slot_ctrl & CC_ERRATUM_MASK))
		ctrl->cmd_busy = 0;

	/*
	 * Optionally wait for the hardware to be ready for a new command,
	 * indicating completion of the above issued command.
	 */
	if (wait)
		pcie_wait_cmd(ctrl);

out:
	mutex_unlock(&ctrl->ctrl_lock);
}

/**
 * pcie_write_cmd - Issue controller command
 * @ctrl: controller to which the command is issued
 * @cmd:  command value written to slot control register
 * @mask: bitmask of slot control register to be modified
 */
static void pcie_write_cmd(struct controller *ctrl, u16 cmd, u16 mask)
{
	pcie_do_write_cmd(ctrl, cmd, mask, true);
}

/* Same as above without waiting for the hardware to latch */
static void pcie_write_cmd_nowait(struct controller *ctrl, u16 cmd, u16 mask)
{
	pcie_do_write_cmd(ctrl, cmd, mask, false);
}

bool pciehp_check_link_active(struct controller *ctrl)
{
	struct pci_dev *pdev = ctrl_dev(ctrl);
	u16 lnk_status;
	bool ret;

	pcie_capability_read_word(pdev, PCI_EXP_LNKSTA, &lnk_status);
	ret = !!(lnk_status & PCI_EXP_LNKSTA_DLLLA);

	if (ret)
		ctrl_dbg(ctrl, "%s: lnk_status = %x\n", __func__, lnk_status);

	return ret;
}

static void pcie_wait_link_active(struct controller *ctrl)
{
	struct pci_dev *pdev = ctrl_dev(ctrl);

	pcie_wait_for_link(pdev, true);
}

static bool pci_bus_check_dev(struct pci_bus *bus, int devfn)
{
	u32 l;
	int count = 0;
	int delay = 1000, step = 20;
	bool found = false;

	do {
		found = pci_bus_read_dev_vendor_id(bus, devfn, &l, 0);
		count++;

		if (found)
			break;

		msleep(step);
		delay -= step;
	} while (delay > 0);

	if (count > 1 && pciehp_debug)
		printk(KERN_DEBUG "pci %04x:%02x:%02x.%d id reading try %d times with interval %d ms to get %08x\n",
			pci_domain_nr(bus), bus->number, PCI_SLOT(devfn),
			PCI_FUNC(devfn), count, step, l);

	return found;
}

int pciehp_check_link_status(struct controller *ctrl)
{
	struct pci_dev *pdev = ctrl_dev(ctrl);
	bool found;
	u16 lnk_status;

	/*
	 * Data Link Layer Link Active Reporting must be capable for
	 * hot-plug capable downstream port. But old controller might
	 * not implement it. In this case, we wait for 1000 ms.
	*/
	if (ctrl->link_active_reporting)
		pcie_wait_link_active(ctrl);
	else
		msleep(1000);

	/* wait 100ms before read pci conf, and try in 1s */
	msleep(100);
	found = pci_bus_check_dev(ctrl->pcie->port->subordinate,
					PCI_DEVFN(0, 0));

	/* ignore link or presence changes up to this point */
	if (found)
		atomic_and(~(PCI_EXP_SLTSTA_DLLSC | PCI_EXP_SLTSTA_PDC),
			   &ctrl->pending_events);

	pcie_capability_read_word(pdev, PCI_EXP_LNKSTA, &lnk_status);
	ctrl_dbg(ctrl, "%s: lnk_status = %x\n", __func__, lnk_status);
	if ((lnk_status & PCI_EXP_LNKSTA_LT) ||
	    !(lnk_status & PCI_EXP_LNKSTA_NLW)) {
		ctrl_err(ctrl, "link training error: status %#06x\n",
			 lnk_status);
		return -1;
	}

	pcie_update_link_speed(ctrl->pcie->port->subordinate, lnk_status);

	if (!found)
		return -1;

	return 0;
}

static int __pciehp_link_set(struct controller *ctrl, bool enable)
{
	struct pci_dev *pdev = ctrl_dev(ctrl);
	u16 lnk_ctrl;

	pcie_capability_read_word(pdev, PCI_EXP_LNKCTL, &lnk_ctrl);

	if (enable)
		lnk_ctrl &= ~PCI_EXP_LNKCTL_LD;
	else
		lnk_ctrl |= PCI_EXP_LNKCTL_LD;

	pcie_capability_write_word(pdev, PCI_EXP_LNKCTL, lnk_ctrl);
	ctrl_dbg(ctrl, "%s: lnk_ctrl = %x\n", __func__, lnk_ctrl);
	return 0;
}

static int pciehp_link_enable(struct controller *ctrl)
{
	return __pciehp_link_set(ctrl, true);
}

int pciehp_get_raw_indicator_status(struct hotplug_slot *hotplug_slot,
				    u8 *status)
{
	struct slot *slot = hotplug_slot->private;
	struct pci_dev *pdev = ctrl_dev(slot->ctrl);
	u16 slot_ctrl;

	pcie_capability_read_word(pdev, PCI_EXP_SLTCTL, &slot_ctrl);
	*status = (slot_ctrl & (PCI_EXP_SLTCTL_AIC | PCI_EXP_SLTCTL_PIC)) >> 6;
	return 0;
}

void pciehp_get_attention_status(struct slot *slot, u8 *status)
{
	struct controller *ctrl = slot->ctrl;
	struct pci_dev *pdev = ctrl_dev(ctrl);
	u16 slot_ctrl;

	pcie_capability_read_word(pdev, PCI_EXP_SLTCTL, &slot_ctrl);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x, value read %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL, slot_ctrl);

	switch (slot_ctrl & PCI_EXP_SLTCTL_AIC) {
	case PCI_EXP_SLTCTL_ATTN_IND_ON:
		*status = 1;	/* On */
		break;
	case PCI_EXP_SLTCTL_ATTN_IND_BLINK:
		*status = 2;	/* Blink */
		break;
	case PCI_EXP_SLTCTL_ATTN_IND_OFF:
		*status = 0;	/* Off */
		break;
	default:
		*status = 0xFF;
		break;
	}
}

void pciehp_get_power_status(struct slot *slot, u8 *status)
{
	struct controller *ctrl = slot->ctrl;
	struct pci_dev *pdev = ctrl_dev(ctrl);
	u16 slot_ctrl;

	pcie_capability_read_word(pdev, PCI_EXP_SLTCTL, &slot_ctrl);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x value read %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL, slot_ctrl);

	switch (slot_ctrl & PCI_EXP_SLTCTL_PCC) {
	case PCI_EXP_SLTCTL_PWR_ON:
		*status = 1;	/* On */
		break;
	case PCI_EXP_SLTCTL_PWR_OFF:
		*status = 0;	/* Off */
		break;
	default:
		*status = 0xFF;
		break;
	}
}

void pciehp_get_latch_status(struct slot *slot, u8 *status)
{
	struct pci_dev *pdev = ctrl_dev(slot->ctrl);
	u16 slot_status;

	pcie_capability_read_word(pdev, PCI_EXP_SLTSTA, &slot_status);
	*status = !!(slot_status & PCI_EXP_SLTSTA_MRLSS);
}

void pciehp_get_adapter_status(struct slot *slot, u8 *status)
{
	struct pci_dev *pdev = ctrl_dev(slot->ctrl);
	u16 slot_status;

	pcie_capability_read_word(pdev, PCI_EXP_SLTSTA, &slot_status);
	*status = !!(slot_status & PCI_EXP_SLTSTA_PDS);
}

int pciehp_query_power_fault(struct slot *slot)
{
	struct pci_dev *pdev = ctrl_dev(slot->ctrl);
	u16 slot_status;

	pcie_capability_read_word(pdev, PCI_EXP_SLTSTA, &slot_status);
	return !!(slot_status & PCI_EXP_SLTSTA_PFD);
}

int pciehp_set_raw_indicator_status(struct hotplug_slot *hotplug_slot,
				    u8 status)
{
	struct slot *slot = hotplug_slot->private;
	struct controller *ctrl = slot->ctrl;

	pcie_write_cmd_nowait(ctrl, status << 6,
			      PCI_EXP_SLTCTL_AIC | PCI_EXP_SLTCTL_PIC);
	return 0;
}

void pciehp_set_attention_status(struct slot *slot, u8 value)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_cmd;

	if (!ATTN_LED(ctrl))
		return;

	switch (value) {
	case 0:		/* turn off */
		slot_cmd = PCI_EXP_SLTCTL_ATTN_IND_OFF;
		break;
	case 1:		/* turn on */
		slot_cmd = PCI_EXP_SLTCTL_ATTN_IND_ON;
		break;
	case 2:		/* turn blink */
		slot_cmd = PCI_EXP_SLTCTL_ATTN_IND_BLINK;
		break;
	default:
		return;
	}
	pcie_write_cmd_nowait(ctrl, slot_cmd, PCI_EXP_SLTCTL_AIC);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL, slot_cmd);
}

void pciehp_green_led_on(struct slot *slot)
{
	struct controller *ctrl = slot->ctrl;

	if (!PWR_LED(ctrl))
		return;

	pcie_write_cmd_nowait(ctrl, PCI_EXP_SLTCTL_PWR_IND_ON,
			      PCI_EXP_SLTCTL_PIC);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL,
		 PCI_EXP_SLTCTL_PWR_IND_ON);
}

void pciehp_green_led_off(struct slot *slot)
{
	struct controller *ctrl = slot->ctrl;

	if (!PWR_LED(ctrl))
		return;

	pcie_write_cmd_nowait(ctrl, PCI_EXP_SLTCTL_PWR_IND_OFF,
			      PCI_EXP_SLTCTL_PIC);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL,
		 PCI_EXP_SLTCTL_PWR_IND_OFF);
}

void pciehp_green_led_blink(struct slot *slot)
{
	struct controller *ctrl = slot->ctrl;

	if (!PWR_LED(ctrl))
		return;

	pcie_write_cmd_nowait(ctrl, PCI_EXP_SLTCTL_PWR_IND_BLINK,
			      PCI_EXP_SLTCTL_PIC);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL,
		 PCI_EXP_SLTCTL_PWR_IND_BLINK);
}

int pciehp_power_on_slot(struct slot *slot)
{
	struct controller *ctrl = slot->ctrl;
	struct pci_dev *pdev = ctrl_dev(ctrl);
	u16 slot_status;
	int retval;

	/* Clear sticky power-fault bit from previous power failures */
	pcie_capability_read_word(pdev, PCI_EXP_SLTSTA, &slot_status);
	if (slot_status & PCI_EXP_SLTSTA_PFD)
		pcie_capability_write_word(pdev, PCI_EXP_SLTSTA,
					   PCI_EXP_SLTSTA_PFD);
	ctrl->power_fault_detected = 0;

	pcie_write_cmd(ctrl, PCI_EXP_SLTCTL_PWR_ON, PCI_EXP_SLTCTL_PCC);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL,
		 PCI_EXP_SLTCTL_PWR_ON);

	retval = pciehp_link_enable(ctrl);
	if (retval)
		ctrl_err(ctrl, "%s: Can not enable the link!\n", __func__);

	return retval;
}

void pciehp_power_off_slot(struct slot *slot)
{
	struct controller *ctrl = slot->ctrl;

	pcie_write_cmd(ctrl, PCI_EXP_SLTCTL_PWR_OFF, PCI_EXP_SLTCTL_PCC);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL,
		 PCI_EXP_SLTCTL_PWR_OFF);
}

static irqreturn_t pciehp_isr(int irq, void *dev_id)
{
	struct controller *ctrl = (struct controller *)dev_id;
	struct pci_dev *pdev = ctrl_dev(ctrl);
	u16 status, events;

	/*
	 * Interrupts only occur in D3hot or shallower (PCIe r4.0, sec 6.7.3.4).
	 */
	if (pdev->current_state == PCI_D3cold)
		return IRQ_NONE;

	pcie_capability_read_word(pdev, PCI_EXP_SLTSTA, &status);
	if (status == (u16) ~0) {
		ctrl_info(ctrl, "%s: no response from device\n", __func__);
		return IRQ_NONE;
	}

	/*
	 * Slot Status contains plain status bits as well as event
	 * notification bits; right now we only want the event bits.
	 */
	events = status & (PCI_EXP_SLTSTA_ABP | PCI_EXP_SLTSTA_PFD |
			   PCI_EXP_SLTSTA_PDC | PCI_EXP_SLTSTA_CC |
			   PCI_EXP_SLTSTA_DLLSC);

	/*
	 * If we've already reported a power fault, don't report it again
	 * until we've done something to handle it.
	 */
	if (ctrl->power_fault_detected)
		events &= ~PCI_EXP_SLTSTA_PFD;

	if (!events)
		return IRQ_NONE;

	pcie_capability_write_word(pdev, PCI_EXP_SLTSTA, events);
	ctrl_dbg(ctrl, "pending interrupts %#06x from Slot Status\n", events);

	/*
	 * Command Completed notifications are not deferred to the
	 * IRQ thread because it may be waiting for their arrival.
	 */
	if (events & PCI_EXP_SLTSTA_CC) {
		ctrl->cmd_busy = 0;
		smp_mb();
		wake_up(&ctrl->queue);

		if (events == PCI_EXP_SLTSTA_CC)
			return IRQ_HANDLED;

		events &= ~PCI_EXP_SLTSTA_CC;
	}

	if (pdev->ignore_hotplug) {
		ctrl_dbg(ctrl, "ignoring hotplug event %#06x\n", events);
		return IRQ_HANDLED;
	}

	/* Save pending events for consumption by IRQ thread. */
	atomic_or(events, &ctrl->pending_events);
	return IRQ_WAKE_THREAD;
}

static irqreturn_t pciehp_ist(int irq, void *dev_id)
{
	struct controller *ctrl = (struct controller *)dev_id;
	struct slot *slot = ctrl->slot;
	u32 events;

	synchronize_hardirq(irq);
	events = atomic_xchg(&ctrl->pending_events, 0);
	if (!events)
		return IRQ_NONE;

	/* Check Attention Button Pressed */
	if (events & PCI_EXP_SLTSTA_ABP) {
		ctrl_info(ctrl, "Slot(%s): Attention button pressed\n",
			  slot_name(slot));
		pciehp_handle_button_press(slot);
	}

	/*
	 * Disable requests have higher priority than Presence Detect Changed
	 * or Data Link Layer State Changed events.
	 *
	 * Check Link Status Changed at higher precedence than Presence
	 * Detect Changed.  The PDS value may be set to "card present" from
	 * out-of-band detection, which may be in conflict with a Link Down.
	 */
	if (events & DISABLE_SLOT)
		pciehp_handle_disable_request(slot);
	else if (events & PCI_EXP_SLTSTA_DLLSC)
		pciehp_handle_link_change(slot);
	else if (events & PCI_EXP_SLTSTA_PDC)
		pciehp_handle_presence_change(slot);

	/* Check Power Fault Detected */
	if ((events & PCI_EXP_SLTSTA_PFD) && !ctrl->power_fault_detected) {
		ctrl->power_fault_detected = 1;
		ctrl_err(ctrl, "Slot(%s): Power fault\n", slot_name(slot));
		pciehp_set_attention_status(slot, 1);
		pciehp_green_led_off(slot);
	}

	wake_up(&ctrl->requester);
	return IRQ_HANDLED;
}

static int pciehp_poll(void *data)
{
	struct controller *ctrl = data;

	schedule_timeout_idle(10 * HZ); /* start with 10 sec delay */

	while (!kthread_should_stop()) {
		if (kthread_should_park())
			kthread_parkme();

		/* poll for interrupt events or user requests */
		while (pciehp_isr(IRQ_NOTCONNECTED, ctrl) == IRQ_WAKE_THREAD ||
		       atomic_read(&ctrl->pending_events))
			pciehp_ist(IRQ_NOTCONNECTED, ctrl);

		if (pciehp_poll_time <= 0 || pciehp_poll_time > 60)
			pciehp_poll_time = 2; /* clamp to sane value */

		schedule_timeout_idle(pciehp_poll_time * HZ);
	}

	return 0;
}

static void pcie_enable_notification(struct controller *ctrl)
{
	u16 cmd, mask;

	/*
	 * TBD: Power fault detected software notification support.
	 *
	 * Power fault detected software notification is not enabled
	 * now, because it caused power fault detected interrupt storm
	 * on some machines. On those machines, power fault detected
	 * bit in the slot status register was set again immediately
	 * when it is cleared in the interrupt service routine, and
	 * next power fault detected interrupt was notified again.
	 */

	/*
	 * Always enable link events: thus link-up and link-down shall
	 * always be treated as hotplug and unplug respectively. Enable
	 * presence detect only if Attention Button is not present.
	 */
	cmd = PCI_EXP_SLTCTL_DLLSCE;
	if (ATTN_BUTTN(ctrl))
		cmd |= PCI_EXP_SLTCTL_ABPE;
	else
		cmd |= PCI_EXP_SLTCTL_PDCE;
	if (!pciehp_poll_mode)
		cmd |= PCI_EXP_SLTCTL_HPIE | PCI_EXP_SLTCTL_CCIE;

	mask = (PCI_EXP_SLTCTL_PDCE | PCI_EXP_SLTCTL_ABPE |
		PCI_EXP_SLTCTL_PFDE |
		PCI_EXP_SLTCTL_HPIE | PCI_EXP_SLTCTL_CCIE |
		PCI_EXP_SLTCTL_DLLSCE);

	pcie_write_cmd_nowait(ctrl, cmd, mask);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL, cmd);
}

void pcie_reenable_notification(struct controller *ctrl)
{
	/*
	 * Clear both Presence and Data Link Layer Changed to make sure
	 * those events still fire after we have re-enabled them.
	 */
	pcie_capability_write_word(ctrl->pcie->port, PCI_EXP_SLTSTA,
				   PCI_EXP_SLTSTA_PDC | PCI_EXP_SLTSTA_DLLSC);
	pcie_enable_notification(ctrl);
}

static void pcie_disable_notification(struct controller *ctrl)
{
	u16 mask;

	mask = (PCI_EXP_SLTCTL_PDCE | PCI_EXP_SLTCTL_ABPE |
		PCI_EXP_SLTCTL_MRLSCE | PCI_EXP_SLTCTL_PFDE |
		PCI_EXP_SLTCTL_HPIE | PCI_EXP_SLTCTL_CCIE |
		PCI_EXP_SLTCTL_DLLSCE);
	pcie_write_cmd(ctrl, 0, mask);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL, 0);
}

/*
 * pciehp has a 1:1 bus:slot relationship so we ultimately want a secondary
 * bus reset of the bridge, but at the same time we want to ensure that it is
 * not seen as a hot-unplug, followed by the hot-plug of the device. Thus,
 * disable link state notification and presence detection change notification
 * momentarily, if we see that they could interfere. Also, clear any spurious
 * events after.
 */
int pciehp_reset_slot(struct slot *slot, int probe)
{
	struct controller *ctrl = slot->ctrl;
	struct pci_dev *pdev = ctrl_dev(ctrl);
	u16 stat_mask = 0, ctrl_mask = 0;

	if (probe)
		return 0;

	if (!ATTN_BUTTN(ctrl)) {
		ctrl_mask |= PCI_EXP_SLTCTL_PDCE;
		stat_mask |= PCI_EXP_SLTSTA_PDC;
	}
	ctrl_mask |= PCI_EXP_SLTCTL_DLLSCE;
	stat_mask |= PCI_EXP_SLTSTA_DLLSC;

	pcie_write_cmd(ctrl, 0, ctrl_mask);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL, 0);
	if (pciehp_poll_mode)
		kthread_park(ctrl->poll_thread);

	pci_reset_bridge_secondary_bus(ctrl->pcie->port);

	pcie_capability_write_word(pdev, PCI_EXP_SLTSTA, stat_mask);
	pcie_write_cmd_nowait(ctrl, ctrl_mask, ctrl_mask);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL, ctrl_mask);
	if (pciehp_poll_mode)
		kthread_unpark(ctrl->poll_thread);
	return 0;
}

int pcie_init_notification(struct controller *ctrl)
{
	if (pciehp_request_irq(ctrl))
		return -1;
	pcie_enable_notification(ctrl);
	ctrl->notification_enabled = 1;
	return 0;
}

void pcie_shutdown_notification(struct controller *ctrl)
{
	if (ctrl->notification_enabled) {
		pcie_disable_notification(ctrl);
		pciehp_free_irq(ctrl);
		ctrl->notification_enabled = 0;
	}
}

static int pcie_init_slot(struct controller *ctrl)
{
	struct pci_bus *subordinate = ctrl_dev(ctrl)->subordinate;
	struct slot *slot;

	slot = kzalloc(sizeof(*slot), GFP_KERNEL);
	if (!slot)
		return -ENOMEM;

	down_read(&pci_bus_sem);
	slot->state = list_empty(&subordinate->devices) ? OFF_STATE : ON_STATE;
	up_read(&pci_bus_sem);

	slot->ctrl = ctrl;
	mutex_init(&slot->lock);
	INIT_DELAYED_WORK(&slot->work, pciehp_queue_pushbutton_work);
	ctrl->slot = slot;
	return 0;
}

static void pcie_cleanup_slot(struct controller *ctrl)
{
	struct slot *slot = ctrl->slot;

	cancel_delayed_work_sync(&slot->work);
	kfree(slot);
}

static inline void dbg_ctrl(struct controller *ctrl)
{
	struct pci_dev *pdev = ctrl->pcie->port;
	u16 reg16;

	if (!pciehp_debug)
		return;

	ctrl_info(ctrl, "Slot Capabilities      : 0x%08x\n", ctrl->slot_cap);
	pcie_capability_read_word(pdev, PCI_EXP_SLTSTA, &reg16);
	ctrl_info(ctrl, "Slot Status            : 0x%04x\n", reg16);
	pcie_capability_read_word(pdev, PCI_EXP_SLTCTL, &reg16);
	ctrl_info(ctrl, "Slot Control           : 0x%04x\n", reg16);
}

#define FLAG(x, y)	(((x) & (y)) ? '+' : '-')

struct controller *pcie_init(struct pcie_device *dev)
{
	struct controller *ctrl;
	u32 slot_cap, link_cap;
	struct pci_dev *pdev = dev->port;

	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		goto abort;

	ctrl->pcie = dev;
	pcie_capability_read_dword(pdev, PCI_EXP_SLTCAP, &slot_cap);

	if (pdev->hotplug_user_indicators)
		slot_cap &= ~(PCI_EXP_SLTCAP_AIP | PCI_EXP_SLTCAP_PIP);

	/*
	 * We assume no Thunderbolt controllers support Command Complete events,
	 * but some controllers falsely claim they do.
	 */
	if (pdev->is_thunderbolt)
		slot_cap |= PCI_EXP_SLTCAP_NCCS;

	ctrl->slot_cap = slot_cap;
	mutex_init(&ctrl->ctrl_lock);
	init_waitqueue_head(&ctrl->requester);
	init_waitqueue_head(&ctrl->queue);
	dbg_ctrl(ctrl);

	/* Check if Data Link Layer Link Active Reporting is implemented */
	pcie_capability_read_dword(pdev, PCI_EXP_LNKCAP, &link_cap);
	if (link_cap & PCI_EXP_LNKCAP_DLLLARC)
		ctrl->link_active_reporting = 1;

	/*
	 * Clear all remaining event bits in Slot Status register except
	 * Presence Detect Changed. We want to make sure possible
	 * hotplug event is triggered when the interrupt is unmasked so
	 * that we don't lose that event.
	 */
	pcie_capability_write_word(pdev, PCI_EXP_SLTSTA,
		PCI_EXP_SLTSTA_ABP | PCI_EXP_SLTSTA_PFD |
		PCI_EXP_SLTSTA_MRLSC | PCI_EXP_SLTSTA_CC |
		PCI_EXP_SLTSTA_DLLSC);

	ctrl_info(ctrl, "Slot #%d AttnBtn%c PwrCtrl%c MRL%c AttnInd%c PwrInd%c HotPlug%c Surprise%c Interlock%c NoCompl%c LLActRep%c%s\n",
		(slot_cap & PCI_EXP_SLTCAP_PSN) >> 19,
		FLAG(slot_cap, PCI_EXP_SLTCAP_ABP),
		FLAG(slot_cap, PCI_EXP_SLTCAP_PCP),
		FLAG(slot_cap, PCI_EXP_SLTCAP_MRLSP),
		FLAG(slot_cap, PCI_EXP_SLTCAP_AIP),
		FLAG(slot_cap, PCI_EXP_SLTCAP_PIP),
		FLAG(slot_cap, PCI_EXP_SLTCAP_HPC),
		FLAG(slot_cap, PCI_EXP_SLTCAP_HPS),
		FLAG(slot_cap, PCI_EXP_SLTCAP_EIP),
		FLAG(slot_cap, PCI_EXP_SLTCAP_NCCS),
		FLAG(link_cap, PCI_EXP_LNKCAP_DLLLARC),
		pdev->broken_cmd_compl ? " (with Cmd Compl erratum)" : "");

	if (pcie_init_slot(ctrl))
		goto abort_ctrl;

	return ctrl;

abort_ctrl:
	kfree(ctrl);
abort:
	return NULL;
}

void pciehp_release_ctrl(struct controller *ctrl)
{
	pcie_cleanup_slot(ctrl);
	kfree(ctrl);
}

static void quirk_cmd_compl(struct pci_dev *pdev)
{
	u32 slot_cap;

	if (pci_is_pcie(pdev)) {
		pcie_capability_read_dword(pdev, PCI_EXP_SLTCAP, &slot_cap);
		if (slot_cap & PCI_EXP_SLTCAP_HPC &&
		    !(slot_cap & PCI_EXP_SLTCAP_NCCS))
			pdev->broken_cmd_compl = 1;
	}
}
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, PCI_ANY_ID,
			      PCI_CLASS_BRIDGE_PCI, 8, quirk_cmd_compl);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_QCOM, 0x0400,
			      PCI_CLASS_BRIDGE_PCI, 8, quirk_cmd_compl);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_QCOM, 0x0401,
			      PCI_CLASS_BRIDGE_PCI, 8, quirk_cmd_compl);
