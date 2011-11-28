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
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <greg@kroah.com>,<kristen.c.accardi@intel.com>
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/signal.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/slab.h>

#include "../pci.h"
#include "pciehp.h"

static inline int pciehp_readw(struct controller *ctrl, int reg, u16 *value)
{
	struct pci_dev *dev = ctrl->pcie->port;
	return pci_read_config_word(dev, pci_pcie_cap(dev) + reg, value);
}

static inline int pciehp_readl(struct controller *ctrl, int reg, u32 *value)
{
	struct pci_dev *dev = ctrl->pcie->port;
	return pci_read_config_dword(dev, pci_pcie_cap(dev) + reg, value);
}

static inline int pciehp_writew(struct controller *ctrl, int reg, u16 value)
{
	struct pci_dev *dev = ctrl->pcie->port;
	return pci_write_config_word(dev, pci_pcie_cap(dev) + reg, value);
}

static inline int pciehp_writel(struct controller *ctrl, int reg, u32 value)
{
	struct pci_dev *dev = ctrl->pcie->port;
	return pci_write_config_dword(dev, pci_pcie_cap(dev) + reg, value);
}

/* Power Control Command */
#define POWER_ON	0
#define POWER_OFF	PCI_EXP_SLTCTL_PCC

static irqreturn_t pcie_isr(int irq, void *dev_id);
static void start_int_poll_timer(struct controller *ctrl, int sec);

/* This is the interrupt polling timeout function. */
static void int_poll_timeout(unsigned long data)
{
	struct controller *ctrl = (struct controller *)data;

	/* Poll for interrupt events.  regs == NULL => polling */
	pcie_isr(0, ctrl);

	init_timer(&ctrl->poll_timer);
	if (!pciehp_poll_time)
		pciehp_poll_time = 2; /* default polling interval is 2 sec */

	start_int_poll_timer(ctrl, pciehp_poll_time);
}

/* This function starts the interrupt polling timer. */
static void start_int_poll_timer(struct controller *ctrl, int sec)
{
	/* Clamp to sane value */
	if ((sec <= 0) || (sec > 60))
        	sec = 2;

	ctrl->poll_timer.function = &int_poll_timeout;
	ctrl->poll_timer.data = (unsigned long)ctrl;
	ctrl->poll_timer.expires = jiffies + sec * HZ;
	add_timer(&ctrl->poll_timer);
}

static inline int pciehp_request_irq(struct controller *ctrl)
{
	int retval, irq = ctrl->pcie->irq;

	/* Install interrupt polling timer. Start with 10 sec delay */
	if (pciehp_poll_mode) {
		init_timer(&ctrl->poll_timer);
		start_int_poll_timer(ctrl, 10);
		return 0;
	}

	/* Installs the interrupt handler */
	retval = request_irq(irq, pcie_isr, IRQF_SHARED, MY_NAME, ctrl);
	if (retval)
		ctrl_err(ctrl, "Cannot get irq %d for the hotplug controller\n",
			 irq);
	return retval;
}

static inline void pciehp_free_irq(struct controller *ctrl)
{
	if (pciehp_poll_mode)
		del_timer_sync(&ctrl->poll_timer);
	else
		free_irq(ctrl->pcie->irq, ctrl);
}

static int pcie_poll_cmd(struct controller *ctrl)
{
	u16 slot_status;
	int err, timeout = 1000;

	err = pciehp_readw(ctrl, PCI_EXP_SLTSTA, &slot_status);
	if (!err && (slot_status & PCI_EXP_SLTSTA_CC)) {
		pciehp_writew(ctrl, PCI_EXP_SLTSTA, PCI_EXP_SLTSTA_CC);
		return 1;
	}
	while (timeout > 0) {
		msleep(10);
		timeout -= 10;
		err = pciehp_readw(ctrl, PCI_EXP_SLTSTA, &slot_status);
		if (!err && (slot_status & PCI_EXP_SLTSTA_CC)) {
			pciehp_writew(ctrl, PCI_EXP_SLTSTA, PCI_EXP_SLTSTA_CC);
			return 1;
		}
	}
	return 0;	/* timeout */
}

static void pcie_wait_cmd(struct controller *ctrl, int poll)
{
	unsigned int msecs = pciehp_poll_mode ? 2500 : 1000;
	unsigned long timeout = msecs_to_jiffies(msecs);
	int rc;

	if (poll)
		rc = pcie_poll_cmd(ctrl);
	else
		rc = wait_event_timeout(ctrl->queue, !ctrl->cmd_busy, timeout);
	if (!rc)
		ctrl_dbg(ctrl, "Command not completed in 1000 msec\n");
}

/**
 * pcie_write_cmd - Issue controller command
 * @ctrl: controller to which the command is issued
 * @cmd:  command value written to slot control register
 * @mask: bitmask of slot control register to be modified
 */
static int pcie_write_cmd(struct controller *ctrl, u16 cmd, u16 mask)
{
	int retval = 0;
	u16 slot_status;
	u16 slot_ctrl;

	mutex_lock(&ctrl->ctrl_lock);

	retval = pciehp_readw(ctrl, PCI_EXP_SLTSTA, &slot_status);
	if (retval) {
		ctrl_err(ctrl, "%s: Cannot read SLOTSTATUS register\n",
			 __func__);
		goto out;
	}

	if (slot_status & PCI_EXP_SLTSTA_CC) {
		if (!ctrl->no_cmd_complete) {
			/*
			 * After 1 sec and CMD_COMPLETED still not set, just
			 * proceed forward to issue the next command according
			 * to spec. Just print out the error message.
			 */
			ctrl_dbg(ctrl, "CMD_COMPLETED not clear after 1 sec\n");
		} else if (!NO_CMD_CMPL(ctrl)) {
			/*
			 * This controller semms to notify of command completed
			 * event even though it supports none of power
			 * controller, attention led, power led and EMI.
			 */
			ctrl_dbg(ctrl, "Unexpected CMD_COMPLETED. Need to "
				 "wait for command completed event.\n");
			ctrl->no_cmd_complete = 0;
		} else {
			ctrl_dbg(ctrl, "Unexpected CMD_COMPLETED. Maybe "
				 "the controller is broken.\n");
		}
	}

	retval = pciehp_readw(ctrl, PCI_EXP_SLTCTL, &slot_ctrl);
	if (retval) {
		ctrl_err(ctrl, "%s: Cannot read SLOTCTRL register\n", __func__);
		goto out;
	}

	slot_ctrl &= ~mask;
	slot_ctrl |= (cmd & mask);
	ctrl->cmd_busy = 1;
	smp_mb();
	retval = pciehp_writew(ctrl, PCI_EXP_SLTCTL, slot_ctrl);
	if (retval)
		ctrl_err(ctrl, "Cannot write to SLOTCTRL register\n");

	/*
	 * Wait for command completion.
	 */
	if (!retval && !ctrl->no_cmd_complete) {
		int poll = 0;
		/*
		 * if hotplug interrupt is not enabled or command
		 * completed interrupt is not enabled, we need to poll
		 * command completed event.
		 */
		if (!(slot_ctrl & PCI_EXP_SLTCTL_HPIE) ||
		    !(slot_ctrl & PCI_EXP_SLTCTL_CCIE))
			poll = 1;
                pcie_wait_cmd(ctrl, poll);
	}
 out:
	mutex_unlock(&ctrl->ctrl_lock);
	return retval;
}

static inline int check_link_active(struct controller *ctrl)
{
	u16 link_status;

	if (pciehp_readw(ctrl, PCI_EXP_LNKSTA, &link_status))
		return 0;
	return !!(link_status & PCI_EXP_LNKSTA_DLLLA);
}

static void pcie_wait_link_active(struct controller *ctrl)
{
	int timeout = 1000;

	if (check_link_active(ctrl))
		return;
	while (timeout > 0) {
		msleep(10);
		timeout -= 10;
		if (check_link_active(ctrl))
			return;
	}
	ctrl_dbg(ctrl, "Data Link Layer Link Active not set in 1000 msec\n");
}

int pciehp_check_link_status(struct controller *ctrl)
{
	u16 lnk_status;
	int retval = 0;

        /*
         * Data Link Layer Link Active Reporting must be capable for
         * hot-plug capable downstream port. But old controller might
         * not implement it. In this case, we wait for 1000 ms.
         */
        if (ctrl->link_active_reporting)
                pcie_wait_link_active(ctrl);
        else
                msleep(1000);

	/*
	 * Need to wait for 1000 ms after Data Link Layer Link Active
	 * (DLLLA) bit reads 1b before sending configuration request.
	 * We need it before checking Link Training (LT) bit becuase
	 * LT is still set even after DLLLA bit is set on some platform.
	 */
	msleep(1000);

	retval = pciehp_readw(ctrl, PCI_EXP_LNKSTA, &lnk_status);
	if (retval) {
		ctrl_err(ctrl, "Cannot read LNKSTATUS register\n");
		return retval;
	}

	ctrl_dbg(ctrl, "%s: lnk_status = %x\n", __func__, lnk_status);
	if ((lnk_status & PCI_EXP_LNKSTA_LT) ||
	    !(lnk_status & PCI_EXP_LNKSTA_NLW)) {
		ctrl_err(ctrl, "Link Training Error occurs \n");
		retval = -1;
		return retval;
	}

	/*
	 * If the port supports Link speeds greater than 5.0 GT/s, we
	 * must wait for 100 ms after Link training completes before
	 * sending configuration request.
	 */
	if (ctrl->pcie->port->subordinate->max_bus_speed > PCIE_SPEED_5_0GT)
		msleep(100);

	pcie_update_link_speed(ctrl->pcie->port->subordinate, lnk_status);

	return retval;
}

int pciehp_get_attention_status(struct slot *slot, u8 *status)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_ctrl;
	u8 atten_led_state;
	int retval = 0;

	retval = pciehp_readw(ctrl, PCI_EXP_SLTCTL, &slot_ctrl);
	if (retval) {
		ctrl_err(ctrl, "%s: Cannot read SLOTCTRL register\n", __func__);
		return retval;
	}

	ctrl_dbg(ctrl, "%s: SLOTCTRL %x, value read %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL, slot_ctrl);

	atten_led_state = (slot_ctrl & PCI_EXP_SLTCTL_AIC) >> 6;

	switch (atten_led_state) {
	case 0:
		*status = 0xFF;	/* Reserved */
		break;
	case 1:
		*status = 1;	/* On */
		break;
	case 2:
		*status = 2;	/* Blink */
		break;
	case 3:
		*status = 0;	/* Off */
		break;
	default:
		*status = 0xFF;
		break;
	}

	return 0;
}

int pciehp_get_power_status(struct slot *slot, u8 *status)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_ctrl;
	u8 pwr_state;
	int	retval = 0;

	retval = pciehp_readw(ctrl, PCI_EXP_SLTCTL, &slot_ctrl);
	if (retval) {
		ctrl_err(ctrl, "%s: Cannot read SLOTCTRL register\n", __func__);
		return retval;
	}
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x value read %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL, slot_ctrl);

	pwr_state = (slot_ctrl & PCI_EXP_SLTCTL_PCC) >> 10;

	switch (pwr_state) {
	case 0:
		*status = 1;
		break;
	case 1:
		*status = 0;
		break;
	default:
		*status = 0xFF;
		break;
	}

	return retval;
}

int pciehp_get_latch_status(struct slot *slot, u8 *status)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_status;
	int retval;

	retval = pciehp_readw(ctrl, PCI_EXP_SLTSTA, &slot_status);
	if (retval) {
		ctrl_err(ctrl, "%s: Cannot read SLOTSTATUS register\n",
			 __func__);
		return retval;
	}
	*status = !!(slot_status & PCI_EXP_SLTSTA_MRLSS);
	return 0;
}

int pciehp_get_adapter_status(struct slot *slot, u8 *status)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_status;
	int retval;

	retval = pciehp_readw(ctrl, PCI_EXP_SLTSTA, &slot_status);
	if (retval) {
		ctrl_err(ctrl, "%s: Cannot read SLOTSTATUS register\n",
			 __func__);
		return retval;
	}
	*status = !!(slot_status & PCI_EXP_SLTSTA_PDS);
	return 0;
}

int pciehp_query_power_fault(struct slot *slot)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_status;
	int retval;

	retval = pciehp_readw(ctrl, PCI_EXP_SLTSTA, &slot_status);
	if (retval) {
		ctrl_err(ctrl, "Cannot check for power fault\n");
		return retval;
	}
	return !!(slot_status & PCI_EXP_SLTSTA_PFD);
}

int pciehp_set_attention_status(struct slot *slot, u8 value)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_cmd;
	u16 cmd_mask;

	cmd_mask = PCI_EXP_SLTCTL_AIC;
	switch (value) {
	case 0 :	/* turn off */
		slot_cmd = 0x00C0;
		break;
	case 1:		/* turn on */
		slot_cmd = 0x0040;
		break;
	case 2:		/* turn blink */
		slot_cmd = 0x0080;
		break;
	default:
		return -EINVAL;
	}
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL, slot_cmd);
	return pcie_write_cmd(ctrl, slot_cmd, cmd_mask);
}

void pciehp_green_led_on(struct slot *slot)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_cmd;
	u16 cmd_mask;

	slot_cmd = 0x0100;
	cmd_mask = PCI_EXP_SLTCTL_PIC;
	pcie_write_cmd(ctrl, slot_cmd, cmd_mask);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL, slot_cmd);
}

void pciehp_green_led_off(struct slot *slot)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_cmd;
	u16 cmd_mask;

	slot_cmd = 0x0300;
	cmd_mask = PCI_EXP_SLTCTL_PIC;
	pcie_write_cmd(ctrl, slot_cmd, cmd_mask);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL, slot_cmd);
}

void pciehp_green_led_blink(struct slot *slot)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_cmd;
	u16 cmd_mask;

	slot_cmd = 0x0200;
	cmd_mask = PCI_EXP_SLTCTL_PIC;
	pcie_write_cmd(ctrl, slot_cmd, cmd_mask);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL, slot_cmd);
}

int pciehp_power_on_slot(struct slot * slot)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_cmd;
	u16 cmd_mask;
	u16 slot_status;
	int retval = 0;

	/* Clear sticky power-fault bit from previous power failures */
	retval = pciehp_readw(ctrl, PCI_EXP_SLTSTA, &slot_status);
	if (retval) {
		ctrl_err(ctrl, "%s: Cannot read SLOTSTATUS register\n",
			 __func__);
		return retval;
	}
	slot_status &= PCI_EXP_SLTSTA_PFD;
	if (slot_status) {
		retval = pciehp_writew(ctrl, PCI_EXP_SLTSTA, slot_status);
		if (retval) {
			ctrl_err(ctrl,
				 "%s: Cannot write to SLOTSTATUS register\n",
				 __func__);
			return retval;
		}
	}
	ctrl->power_fault_detected = 0;

	slot_cmd = POWER_ON;
	cmd_mask = PCI_EXP_SLTCTL_PCC;
	retval = pcie_write_cmd(ctrl, slot_cmd, cmd_mask);
	if (retval) {
		ctrl_err(ctrl, "Write %x command failed!\n", slot_cmd);
		return retval;
	}
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL, slot_cmd);

	return retval;
}

int pciehp_power_off_slot(struct slot * slot)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_cmd;
	u16 cmd_mask;
	int retval;

	slot_cmd = POWER_OFF;
	cmd_mask = PCI_EXP_SLTCTL_PCC;
	retval = pcie_write_cmd(ctrl, slot_cmd, cmd_mask);
	if (retval) {
		ctrl_err(ctrl, "Write command failed!\n");
		return retval;
	}
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n", __func__,
		 pci_pcie_cap(ctrl->pcie->port) + PCI_EXP_SLTCTL, slot_cmd);
	return 0;
}

static irqreturn_t pcie_isr(int irq, void *dev_id)
{
	struct controller *ctrl = (struct controller *)dev_id;
	struct slot *slot = ctrl->slot;
	u16 detected, intr_loc;

	/*
	 * In order to guarantee that all interrupt events are
	 * serviced, we need to re-inspect Slot Status register after
	 * clearing what is presumed to be the last pending interrupt.
	 */
	intr_loc = 0;
	do {
		if (pciehp_readw(ctrl, PCI_EXP_SLTSTA, &detected)) {
			ctrl_err(ctrl, "%s: Cannot read SLOTSTATUS\n",
				 __func__);
			return IRQ_NONE;
		}

		detected &= (PCI_EXP_SLTSTA_ABP | PCI_EXP_SLTSTA_PFD |
			     PCI_EXP_SLTSTA_MRLSC | PCI_EXP_SLTSTA_PDC |
			     PCI_EXP_SLTSTA_CC);
		detected &= ~intr_loc;
		intr_loc |= detected;
		if (!intr_loc)
			return IRQ_NONE;
		if (detected && pciehp_writew(ctrl, PCI_EXP_SLTSTA, intr_loc)) {
			ctrl_err(ctrl, "%s: Cannot write to SLOTSTATUS\n",
				 __func__);
			return IRQ_NONE;
		}
	} while (detected);

	ctrl_dbg(ctrl, "%s: intr_loc %x\n", __func__, intr_loc);

	/* Check Command Complete Interrupt Pending */
	if (intr_loc & PCI_EXP_SLTSTA_CC) {
		ctrl->cmd_busy = 0;
		smp_mb();
		wake_up(&ctrl->queue);
	}

	if (!(intr_loc & ~PCI_EXP_SLTSTA_CC))
		return IRQ_HANDLED;

	/* Check MRL Sensor Changed */
	if (intr_loc & PCI_EXP_SLTSTA_MRLSC)
		pciehp_handle_switch_change(slot);

	/* Check Attention Button Pressed */
	if (intr_loc & PCI_EXP_SLTSTA_ABP)
		pciehp_handle_attention_button(slot);

	/* Check Presence Detect Changed */
	if (intr_loc & PCI_EXP_SLTSTA_PDC)
		pciehp_handle_presence_change(slot);

	/* Check Power Fault Detected */
	if ((intr_loc & PCI_EXP_SLTSTA_PFD) && !ctrl->power_fault_detected) {
		ctrl->power_fault_detected = 1;
		pciehp_handle_power_fault(slot);
	}
	return IRQ_HANDLED;
}

int pciehp_get_max_lnk_width(struct slot *slot,
				 enum pcie_link_width *value)
{
	struct controller *ctrl = slot->ctrl;
	enum pcie_link_width lnk_wdth;
	u32	lnk_cap;
	int retval = 0;

	retval = pciehp_readl(ctrl, PCI_EXP_LNKCAP, &lnk_cap);
	if (retval) {
		ctrl_err(ctrl, "%s: Cannot read LNKCAP register\n", __func__);
		return retval;
	}

	switch ((lnk_cap & PCI_EXP_LNKSTA_NLW) >> 4){
	case 0:
		lnk_wdth = PCIE_LNK_WIDTH_RESRV;
		break;
	case 1:
		lnk_wdth = PCIE_LNK_X1;
		break;
	case 2:
		lnk_wdth = PCIE_LNK_X2;
		break;
	case 4:
		lnk_wdth = PCIE_LNK_X4;
		break;
	case 8:
		lnk_wdth = PCIE_LNK_X8;
		break;
	case 12:
		lnk_wdth = PCIE_LNK_X12;
		break;
	case 16:
		lnk_wdth = PCIE_LNK_X16;
		break;
	case 32:
		lnk_wdth = PCIE_LNK_X32;
		break;
	default:
		lnk_wdth = PCIE_LNK_WIDTH_UNKNOWN;
		break;
	}

	*value = lnk_wdth;
	ctrl_dbg(ctrl, "Max link width = %d\n", lnk_wdth);

	return retval;
}

int pciehp_get_cur_lnk_width(struct slot *slot,
				 enum pcie_link_width *value)
{
	struct controller *ctrl = slot->ctrl;
	enum pcie_link_width lnk_wdth = PCIE_LNK_WIDTH_UNKNOWN;
	int retval = 0;
	u16 lnk_status;

	retval = pciehp_readw(ctrl, PCI_EXP_LNKSTA, &lnk_status);
	if (retval) {
		ctrl_err(ctrl, "%s: Cannot read LNKSTATUS register\n",
			 __func__);
		return retval;
	}

	switch ((lnk_status & PCI_EXP_LNKSTA_NLW) >> 4){
	case 0:
		lnk_wdth = PCIE_LNK_WIDTH_RESRV;
		break;
	case 1:
		lnk_wdth = PCIE_LNK_X1;
		break;
	case 2:
		lnk_wdth = PCIE_LNK_X2;
		break;
	case 4:
		lnk_wdth = PCIE_LNK_X4;
		break;
	case 8:
		lnk_wdth = PCIE_LNK_X8;
		break;
	case 12:
		lnk_wdth = PCIE_LNK_X12;
		break;
	case 16:
		lnk_wdth = PCIE_LNK_X16;
		break;
	case 32:
		lnk_wdth = PCIE_LNK_X32;
		break;
	default:
		lnk_wdth = PCIE_LNK_WIDTH_UNKNOWN;
		break;
	}

	*value = lnk_wdth;
	ctrl_dbg(ctrl, "Current link width = %d\n", lnk_wdth);

	return retval;
}

int pcie_enable_notification(struct controller *ctrl)
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
	cmd = PCI_EXP_SLTCTL_PDCE;
	if (ATTN_BUTTN(ctrl))
		cmd |= PCI_EXP_SLTCTL_ABPE;
	if (MRL_SENS(ctrl))
		cmd |= PCI_EXP_SLTCTL_MRLSCE;
	if (!pciehp_poll_mode)
		cmd |= PCI_EXP_SLTCTL_HPIE | PCI_EXP_SLTCTL_CCIE;

	mask = (PCI_EXP_SLTCTL_PDCE | PCI_EXP_SLTCTL_ABPE |
		PCI_EXP_SLTCTL_MRLSCE | PCI_EXP_SLTCTL_PFDE |
		PCI_EXP_SLTCTL_HPIE | PCI_EXP_SLTCTL_CCIE);

	if (pcie_write_cmd(ctrl, cmd, mask)) {
		ctrl_err(ctrl, "Cannot enable software notification\n");
		return -1;
	}
	return 0;
}

static void pcie_disable_notification(struct controller *ctrl)
{
	u16 mask;
	mask = (PCI_EXP_SLTCTL_PDCE | PCI_EXP_SLTCTL_ABPE |
		PCI_EXP_SLTCTL_MRLSCE | PCI_EXP_SLTCTL_PFDE |
		PCI_EXP_SLTCTL_HPIE | PCI_EXP_SLTCTL_CCIE |
		PCI_EXP_SLTCTL_DLLSCE);
	if (pcie_write_cmd(ctrl, 0, mask))
		ctrl_warn(ctrl, "Cannot disable software notification\n");
}

int pcie_init_notification(struct controller *ctrl)
{
	if (pciehp_request_irq(ctrl))
		return -1;
	if (pcie_enable_notification(ctrl)) {
		pciehp_free_irq(ctrl);
		return -1;
	}
	ctrl->notification_enabled = 1;
	return 0;
}

static void pcie_shutdown_notification(struct controller *ctrl)
{
	if (ctrl->notification_enabled) {
		pcie_disable_notification(ctrl);
		pciehp_free_irq(ctrl);
		ctrl->notification_enabled = 0;
	}
}

static int pcie_init_slot(struct controller *ctrl)
{
	struct slot *slot;

	slot = kzalloc(sizeof(*slot), GFP_KERNEL);
	if (!slot)
		return -ENOMEM;

	slot->ctrl = ctrl;
	mutex_init(&slot->lock);
	INIT_DELAYED_WORK(&slot->work, pciehp_queue_pushbutton_work);
	ctrl->slot = slot;
	return 0;
}

static void pcie_cleanup_slot(struct controller *ctrl)
{
	struct slot *slot = ctrl->slot;
	cancel_delayed_work(&slot->work);
	flush_workqueue(pciehp_wq);
	flush_workqueue(pciehp_ordered_wq);
	kfree(slot);
}

static inline void dbg_ctrl(struct controller *ctrl)
{
	int i;
	u16 reg16;
	struct pci_dev *pdev = ctrl->pcie->port;

	if (!pciehp_debug)
		return;

	ctrl_info(ctrl, "Hotplug Controller:\n");
	ctrl_info(ctrl, "  Seg/Bus/Dev/Func/IRQ : %s IRQ %d\n",
		  pci_name(pdev), pdev->irq);
	ctrl_info(ctrl, "  Vendor ID            : 0x%04x\n", pdev->vendor);
	ctrl_info(ctrl, "  Device ID            : 0x%04x\n", pdev->device);
	ctrl_info(ctrl, "  Subsystem ID         : 0x%04x\n",
		  pdev->subsystem_device);
	ctrl_info(ctrl, "  Subsystem Vendor ID  : 0x%04x\n",
		  pdev->subsystem_vendor);
	ctrl_info(ctrl, "  PCIe Cap offset      : 0x%02x\n",
		  pci_pcie_cap(pdev));
	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
		if (!pci_resource_len(pdev, i))
			continue;
		ctrl_info(ctrl, "  PCI resource [%d]     : %pR\n",
			  i, &pdev->resource[i]);
	}
	ctrl_info(ctrl, "Slot Capabilities      : 0x%08x\n", ctrl->slot_cap);
	ctrl_info(ctrl, "  Physical Slot Number : %d\n", PSN(ctrl));
	ctrl_info(ctrl, "  Attention Button     : %3s\n",
		  ATTN_BUTTN(ctrl) ? "yes" : "no");
	ctrl_info(ctrl, "  Power Controller     : %3s\n",
		  POWER_CTRL(ctrl) ? "yes" : "no");
	ctrl_info(ctrl, "  MRL Sensor           : %3s\n",
		  MRL_SENS(ctrl)   ? "yes" : "no");
	ctrl_info(ctrl, "  Attention Indicator  : %3s\n",
		  ATTN_LED(ctrl)   ? "yes" : "no");
	ctrl_info(ctrl, "  Power Indicator      : %3s\n",
		  PWR_LED(ctrl)    ? "yes" : "no");
	ctrl_info(ctrl, "  Hot-Plug Surprise    : %3s\n",
		  HP_SUPR_RM(ctrl) ? "yes" : "no");
	ctrl_info(ctrl, "  EMI Present          : %3s\n",
		  EMI(ctrl)        ? "yes" : "no");
	ctrl_info(ctrl, "  Command Completed    : %3s\n",
		  NO_CMD_CMPL(ctrl) ? "no" : "yes");
	pciehp_readw(ctrl, PCI_EXP_SLTSTA, &reg16);
	ctrl_info(ctrl, "Slot Status            : 0x%04x\n", reg16);
	pciehp_readw(ctrl, PCI_EXP_SLTCTL, &reg16);
	ctrl_info(ctrl, "Slot Control           : 0x%04x\n", reg16);
}

struct controller *pcie_init(struct pcie_device *dev)
{
	struct controller *ctrl;
	u32 slot_cap, link_cap;
	struct pci_dev *pdev = dev->port;

	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl) {
		dev_err(&dev->device, "%s: Out of memory\n", __func__);
		goto abort;
	}
	ctrl->pcie = dev;
	if (!pci_pcie_cap(pdev)) {
		ctrl_err(ctrl, "Cannot find PCI Express capability\n");
		goto abort_ctrl;
	}
	if (pciehp_readl(ctrl, PCI_EXP_SLTCAP, &slot_cap)) {
		ctrl_err(ctrl, "Cannot read SLOTCAP register\n");
		goto abort_ctrl;
	}

	ctrl->slot_cap = slot_cap;
	mutex_init(&ctrl->ctrl_lock);
	init_waitqueue_head(&ctrl->queue);
	dbg_ctrl(ctrl);
	/*
	 * Controller doesn't notify of command completion if the "No
	 * Command Completed Support" bit is set in Slot Capability
	 * register or the controller supports none of power
	 * controller, attention led, power led and EMI.
	 */
	if (NO_CMD_CMPL(ctrl) ||
	    !(POWER_CTRL(ctrl) | ATTN_LED(ctrl) | PWR_LED(ctrl) | EMI(ctrl)))
	    ctrl->no_cmd_complete = 1;

        /* Check if Data Link Layer Link Active Reporting is implemented */
        if (pciehp_readl(ctrl, PCI_EXP_LNKCAP, &link_cap)) {
                ctrl_err(ctrl, "%s: Cannot read LNKCAP register\n", __func__);
                goto abort_ctrl;
        }
        if (link_cap & PCI_EXP_LNKCAP_DLLLARC) {
                ctrl_dbg(ctrl, "Link Active Reporting supported\n");
                ctrl->link_active_reporting = 1;
        }

	/* Clear all remaining event bits in Slot Status register */
	if (pciehp_writew(ctrl, PCI_EXP_SLTSTA, 0x1f))
		goto abort_ctrl;

	/* Disable sotfware notification */
	pcie_disable_notification(ctrl);

	ctrl_info(ctrl, "HPC vendor_id %x device_id %x ss_vid %x ss_did %x\n",
		  pdev->vendor, pdev->device, pdev->subsystem_vendor,
		  pdev->subsystem_device);

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
	pcie_shutdown_notification(ctrl);
	pcie_cleanup_slot(ctrl);
	kfree(ctrl);
}
