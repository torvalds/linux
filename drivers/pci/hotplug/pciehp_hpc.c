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

#include "../pci.h"
#include "pciehp.h"

static atomic_t pciehp_num_controllers = ATOMIC_INIT(0);

static inline int pciehp_readw(struct controller *ctrl, int reg, u16 *value)
{
	struct pci_dev *dev = ctrl->pci_dev;
	return pci_read_config_word(dev, ctrl->cap_base + reg, value);
}

static inline int pciehp_readl(struct controller *ctrl, int reg, u32 *value)
{
	struct pci_dev *dev = ctrl->pci_dev;
	return pci_read_config_dword(dev, ctrl->cap_base + reg, value);
}

static inline int pciehp_writew(struct controller *ctrl, int reg, u16 value)
{
	struct pci_dev *dev = ctrl->pci_dev;
	return pci_write_config_word(dev, ctrl->cap_base + reg, value);
}

static inline int pciehp_writel(struct controller *ctrl, int reg, u32 value)
{
	struct pci_dev *dev = ctrl->pci_dev;
	return pci_write_config_dword(dev, ctrl->cap_base + reg, value);
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

static int hpc_check_lnk_status(struct controller *ctrl)
{
	u16 lnk_status;
	int retval = 0;

        /*
         * Data Link Layer Link Active Reporting must be capable for
         * hot-plug capable downstream port. But old controller might
         * not implement it. In this case, we wait for 1000 ms.
         */
        if (ctrl->link_active_reporting){
                /* Wait for Data Link Layer Link Active bit to be set */
                pcie_wait_link_active(ctrl);
                /*
                 * We must wait for 100 ms after the Data Link Layer
                 * Link Active bit reads 1b before initiating a
                 * configuration access to the hot added device.
                 */
                msleep(100);
        } else
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

	return retval;
}

static int hpc_get_attention_status(struct slot *slot, u8 *status)
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

	ctrl_dbg(ctrl, "%s: SLOTCTRL %x, value read %x\n",
		 __func__, ctrl->cap_base + PCI_EXP_SLTCTL, slot_ctrl);

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

static int hpc_get_power_status(struct slot *slot, u8 *status)
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
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x value read %x\n",
		 __func__, ctrl->cap_base + PCI_EXP_SLTCTL, slot_ctrl);

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

static int hpc_get_latch_status(struct slot *slot, u8 *status)
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

static int hpc_get_adapter_status(struct slot *slot, u8 *status)
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

static int hpc_query_power_fault(struct slot *slot)
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

static int hpc_set_attention_status(struct slot *slot, u8 value)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_cmd;
	u16 cmd_mask;
	int rc;

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
			return -1;
	}
	rc = pcie_write_cmd(ctrl, slot_cmd, cmd_mask);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n",
		 __func__, ctrl->cap_base + PCI_EXP_SLTCTL, slot_cmd);

	return rc;
}

static void hpc_set_green_led_on(struct slot *slot)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_cmd;
	u16 cmd_mask;

	slot_cmd = 0x0100;
	cmd_mask = PCI_EXP_SLTCTL_PIC;
	pcie_write_cmd(ctrl, slot_cmd, cmd_mask);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n",
		 __func__, ctrl->cap_base + PCI_EXP_SLTCTL, slot_cmd);
}

static void hpc_set_green_led_off(struct slot *slot)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_cmd;
	u16 cmd_mask;

	slot_cmd = 0x0300;
	cmd_mask = PCI_EXP_SLTCTL_PIC;
	pcie_write_cmd(ctrl, slot_cmd, cmd_mask);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n",
		 __func__, ctrl->cap_base + PCI_EXP_SLTCTL, slot_cmd);
}

static void hpc_set_green_led_blink(struct slot *slot)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_cmd;
	u16 cmd_mask;

	slot_cmd = 0x0200;
	cmd_mask = PCI_EXP_SLTCTL_PIC;
	pcie_write_cmd(ctrl, slot_cmd, cmd_mask);
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n",
		 __func__, ctrl->cap_base + PCI_EXP_SLTCTL, slot_cmd);
}

static int hpc_power_on_slot(struct slot * slot)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_cmd;
	u16 cmd_mask;
	u16 slot_status;
	int retval = 0;

	ctrl_dbg(ctrl, "%s: slot->hp_slot %x\n", __func__, slot->hp_slot);

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

	slot_cmd = POWER_ON;
	cmd_mask = PCI_EXP_SLTCTL_PCC;
	if (!pciehp_poll_mode) {
		/* Enable power fault detection turned off at power off time */
		slot_cmd |= PCI_EXP_SLTCTL_PFDE;
		cmd_mask |= PCI_EXP_SLTCTL_PFDE;
	}

	retval = pcie_write_cmd(ctrl, slot_cmd, cmd_mask);
	if (retval) {
		ctrl_err(ctrl, "Write %x command failed!\n", slot_cmd);
		return retval;
	}
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n",
		 __func__, ctrl->cap_base + PCI_EXP_SLTCTL, slot_cmd);

	ctrl->power_fault_detected = 0;
	return retval;
}

static inline int pcie_mask_bad_dllp(struct controller *ctrl)
{
	struct pci_dev *dev = ctrl->pci_dev;
	int pos;
	u32 reg;

	pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR);
	if (!pos)
		return 0;
	pci_read_config_dword(dev, pos + PCI_ERR_COR_MASK, &reg);
	if (reg & PCI_ERR_COR_BAD_DLLP)
		return 0;
	reg |= PCI_ERR_COR_BAD_DLLP;
	pci_write_config_dword(dev, pos + PCI_ERR_COR_MASK, reg);
	return 1;
}

static inline void pcie_unmask_bad_dllp(struct controller *ctrl)
{
	struct pci_dev *dev = ctrl->pci_dev;
	u32 reg;
	int pos;

	pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR);
	if (!pos)
		return;
	pci_read_config_dword(dev, pos + PCI_ERR_COR_MASK, &reg);
	if (!(reg & PCI_ERR_COR_BAD_DLLP))
		return;
	reg &= ~PCI_ERR_COR_BAD_DLLP;
	pci_write_config_dword(dev, pos + PCI_ERR_COR_MASK, reg);
}

static int hpc_power_off_slot(struct slot * slot)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_cmd;
	u16 cmd_mask;
	int retval = 0;
	int changed;

	ctrl_dbg(ctrl, "%s: slot->hp_slot %x\n", __func__, slot->hp_slot);

	/*
	 * Set Bad DLLP Mask bit in Correctable Error Mask
	 * Register. This is the workaround against Bad DLLP error
	 * that sometimes happens during turning power off the slot
	 * which conforms to PCI Express 1.0a spec.
	 */
	changed = pcie_mask_bad_dllp(ctrl);

	slot_cmd = POWER_OFF;
	cmd_mask = PCI_EXP_SLTCTL_PCC;
	if (!pciehp_poll_mode) {
		/* Disable power fault detection */
		slot_cmd &= ~PCI_EXP_SLTCTL_PFDE;
		cmd_mask |= PCI_EXP_SLTCTL_PFDE;
	}

	retval = pcie_write_cmd(ctrl, slot_cmd, cmd_mask);
	if (retval) {
		ctrl_err(ctrl, "Write command failed!\n");
		retval = -1;
		goto out;
	}
	ctrl_dbg(ctrl, "%s: SLOTCTRL %x write cmd %x\n",
		 __func__, ctrl->cap_base + PCI_EXP_SLTCTL, slot_cmd);
 out:
	if (changed)
		pcie_unmask_bad_dllp(ctrl);

	return retval;
}

static irqreturn_t pcie_isr(int irq, void *dev_id)
{
	struct controller *ctrl = (struct controller *)dev_id;
	u16 detected, intr_loc;
	struct slot *p_slot;

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

	p_slot = pciehp_find_slot(ctrl, ctrl->slot_device_offset);

	/* Check MRL Sensor Changed */
	if (intr_loc & PCI_EXP_SLTSTA_MRLSC)
		pciehp_handle_switch_change(p_slot);

	/* Check Attention Button Pressed */
	if (intr_loc & PCI_EXP_SLTSTA_ABP)
		pciehp_handle_attention_button(p_slot);

	/* Check Presence Detect Changed */
	if (intr_loc & PCI_EXP_SLTSTA_PDC)
		pciehp_handle_presence_change(p_slot);

	/* Check Power Fault Detected */
	if ((intr_loc & PCI_EXP_SLTSTA_PFD) && !ctrl->power_fault_detected) {
		ctrl->power_fault_detected = 1;
		pciehp_handle_power_fault(p_slot);
	}
	return IRQ_HANDLED;
}

static int hpc_get_max_lnk_speed(struct slot *slot, enum pci_bus_speed *value)
{
	struct controller *ctrl = slot->ctrl;
	enum pcie_link_speed lnk_speed;
	u32	lnk_cap;
	int retval = 0;

	retval = pciehp_readl(ctrl, PCI_EXP_LNKCAP, &lnk_cap);
	if (retval) {
		ctrl_err(ctrl, "%s: Cannot read LNKCAP register\n", __func__);
		return retval;
	}

	switch (lnk_cap & 0x000F) {
	case 1:
		lnk_speed = PCIE_2PT5GB;
		break;
	default:
		lnk_speed = PCIE_LNK_SPEED_UNKNOWN;
		break;
	}

	*value = lnk_speed;
	ctrl_dbg(ctrl, "Max link speed = %d\n", lnk_speed);

	return retval;
}

static int hpc_get_max_lnk_width(struct slot *slot,
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

static int hpc_get_cur_lnk_speed(struct slot *slot, enum pci_bus_speed *value)
{
	struct controller *ctrl = slot->ctrl;
	enum pcie_link_speed lnk_speed = PCI_SPEED_UNKNOWN;
	int retval = 0;
	u16 lnk_status;

	retval = pciehp_readw(ctrl, PCI_EXP_LNKSTA, &lnk_status);
	if (retval) {
		ctrl_err(ctrl, "%s: Cannot read LNKSTATUS register\n",
			 __func__);
		return retval;
	}

	switch (lnk_status & PCI_EXP_LNKSTA_CLS) {
	case 1:
		lnk_speed = PCIE_2PT5GB;
		break;
	default:
		lnk_speed = PCIE_LNK_SPEED_UNKNOWN;
		break;
	}

	*value = lnk_speed;
	ctrl_dbg(ctrl, "Current link speed = %d\n", lnk_speed);

	return retval;
}

static int hpc_get_cur_lnk_width(struct slot *slot,
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

static void pcie_release_ctrl(struct controller *ctrl);
static struct hpc_ops pciehp_hpc_ops = {
	.power_on_slot			= hpc_power_on_slot,
	.power_off_slot			= hpc_power_off_slot,
	.set_attention_status		= hpc_set_attention_status,
	.get_power_status		= hpc_get_power_status,
	.get_attention_status		= hpc_get_attention_status,
	.get_latch_status		= hpc_get_latch_status,
	.get_adapter_status		= hpc_get_adapter_status,

	.get_max_bus_speed		= hpc_get_max_lnk_speed,
	.get_cur_bus_speed		= hpc_get_cur_lnk_speed,
	.get_max_lnk_width		= hpc_get_max_lnk_width,
	.get_cur_lnk_width		= hpc_get_cur_lnk_width,

	.query_power_fault		= hpc_query_power_fault,
	.green_led_on			= hpc_set_green_led_on,
	.green_led_off			= hpc_set_green_led_off,
	.green_led_blink		= hpc_set_green_led_blink,

	.release_ctlr			= pcie_release_ctrl,
	.check_lnk_status		= hpc_check_lnk_status,
};

int pcie_enable_notification(struct controller *ctrl)
{
	u16 cmd, mask;

	cmd = PCI_EXP_SLTCTL_PDCE;
	if (ATTN_BUTTN(ctrl))
		cmd |= PCI_EXP_SLTCTL_ABPE;
	if (POWER_CTRL(ctrl))
		cmd |= PCI_EXP_SLTCTL_PFDE;
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
		PCI_EXP_SLTCTL_HPIE | PCI_EXP_SLTCTL_CCIE);
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

	slot->hp_slot = 0;
	slot->ctrl = ctrl;
	slot->bus = ctrl->pci_dev->subordinate->number;
	slot->device = ctrl->slot_device_offset + slot->hp_slot;
	slot->hpc_ops = ctrl->hpc_ops;
	slot->number = ctrl->first_slot;
	mutex_init(&slot->lock);
	INIT_DELAYED_WORK(&slot->work, pciehp_queue_pushbutton_work);
	list_add(&slot->slot_list, &ctrl->slot_list);
	return 0;
}

static void pcie_cleanup_slot(struct controller *ctrl)
{
	struct slot *slot;
	slot = list_first_entry(&ctrl->slot_list, struct slot, slot_list);
	list_del(&slot->slot_list);
	cancel_delayed_work(&slot->work);
	flush_scheduled_work();
	flush_workqueue(pciehp_wq);
	kfree(slot);
}

static inline void dbg_ctrl(struct controller *ctrl)
{
	int i;
	u16 reg16;
	struct pci_dev *pdev = ctrl->pci_dev;

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
	ctrl_info(ctrl, "  PCIe Cap offset      : 0x%02x\n", ctrl->cap_base);
	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
		if (!pci_resource_len(pdev, i))
			continue;
		ctrl_info(ctrl, "  PCI resource [%d]     : 0x%llx@0x%llx\n",
			  i, (unsigned long long)pci_resource_len(pdev, i),
			  (unsigned long long)pci_resource_start(pdev, i));
	}
	ctrl_info(ctrl, "Slot Capabilities      : 0x%08x\n", ctrl->slot_cap);
	ctrl_info(ctrl, "  Physical Slot Number : %d\n", ctrl->first_slot);
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
	INIT_LIST_HEAD(&ctrl->slot_list);

	ctrl->pcie = dev;
	ctrl->pci_dev = pdev;
	ctrl->cap_base = pci_find_capability(pdev, PCI_CAP_ID_EXP);
	if (!ctrl->cap_base) {
		ctrl_err(ctrl, "Cannot find PCI Express capability\n");
		goto abort_ctrl;
	}
	if (pciehp_readl(ctrl, PCI_EXP_SLTCAP, &slot_cap)) {
		ctrl_err(ctrl, "Cannot read SLOTCAP register\n");
		goto abort_ctrl;
	}

	ctrl->slot_cap = slot_cap;
	ctrl->first_slot = slot_cap >> 19;
	ctrl->slot_device_offset = 0;
	ctrl->num_slots = 1;
	ctrl->hpc_ops = &pciehp_hpc_ops;
	mutex_init(&ctrl->crit_sect);
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

	/*
	 * If this is the first controller to be initialized,
	 * initialize the pciehp work queue
	 */
	if (atomic_add_return(1, &pciehp_num_controllers) == 1) {
		pciehp_wq = create_singlethread_workqueue("pciehpd");
		if (!pciehp_wq)
			goto abort_ctrl;
	}

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

void pcie_release_ctrl(struct controller *ctrl)
{
	pcie_shutdown_notification(ctrl);
	pcie_cleanup_slot(ctrl);
	/*
	 * If this is the last controller to be released, destroy the
	 * pciehp work queue
	 */
	if (atomic_dec_and_test(&pciehp_num_controllers))
		destroy_workqueue(pciehp_wq);
	kfree(ctrl);
}
