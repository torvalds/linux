// SPDX-License-Identifier: GPL-2.0+
/*
 * Standard PCI Hot Plug Driver
 *
 * Copyright (C) 1995,2001 Compaq Computer Corporation
 * Copyright (C) 2001 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001 IBM Corp.
 * Copyright (C) 2003-2004 Intel Corporation
 *
 * All rights reserved.
 *
 * Send feedback to <greg@kroah.com>,<kristen.c.accardi@intel.com>
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#include "shpchp.h"

/* Slot Available Register I field definition */
#define SLOT_33MHZ		0x0000001f
#define SLOT_66MHZ_PCIX		0x00001f00
#define SLOT_100MHZ_PCIX	0x001f0000
#define SLOT_133MHZ_PCIX	0x1f000000

/* Slot Available Register II field definition */
#define SLOT_66MHZ		0x0000001f
#define SLOT_66MHZ_PCIX_266	0x00000f00
#define SLOT_100MHZ_PCIX_266	0x0000f000
#define SLOT_133MHZ_PCIX_266	0x000f0000
#define SLOT_66MHZ_PCIX_533	0x00f00000
#define SLOT_100MHZ_PCIX_533	0x0f000000
#define SLOT_133MHZ_PCIX_533	0xf0000000

/* Slot Configuration */
#define SLOT_NUM		0x0000001F
#define	FIRST_DEV_NUM		0x00001F00
#define PSN			0x07FF0000
#define	UPDOWN			0x20000000
#define	MRLSENSOR		0x40000000
#define ATTN_BUTTON		0x80000000

/*
 * Interrupt Locator Register definitions
 */
#define CMD_INTR_PENDING	(1 << 0)
#define SLOT_INTR_PENDING(i)	(1 << (i + 1))

/*
 * Controller SERR-INT Register
 */
#define GLOBAL_INTR_MASK	(1 << 0)
#define GLOBAL_SERR_MASK	(1 << 1)
#define COMMAND_INTR_MASK	(1 << 2)
#define ARBITER_SERR_MASK	(1 << 3)
#define COMMAND_DETECTED	(1 << 16)
#define ARBITER_DETECTED	(1 << 17)
#define SERR_INTR_RSVDZ_MASK	0xfffc0000

/*
 * Logical Slot Register definitions
 */
#define SLOT_REG(i)		(SLOT1 + (4 * i))

#define SLOT_STATE_SHIFT	(0)
#define SLOT_STATE_MASK		(3 << 0)
#define SLOT_STATE_PWRONLY	(1)
#define SLOT_STATE_ENABLED	(2)
#define SLOT_STATE_DISABLED	(3)
#define PWR_LED_STATE_SHIFT	(2)
#define PWR_LED_STATE_MASK	(3 << 2)
#define ATN_LED_STATE_SHIFT	(4)
#define ATN_LED_STATE_MASK	(3 << 4)
#define ATN_LED_STATE_ON	(1)
#define ATN_LED_STATE_BLINK	(2)
#define ATN_LED_STATE_OFF	(3)
#define POWER_FAULT		(1 << 6)
#define ATN_BUTTON		(1 << 7)
#define MRL_SENSOR		(1 << 8)
#define MHZ66_CAP		(1 << 9)
#define PRSNT_SHIFT		(10)
#define PRSNT_MASK		(3 << 10)
#define PCIX_CAP_SHIFT		(12)
#define PCIX_CAP_MASK_PI1	(3 << 12)
#define PCIX_CAP_MASK_PI2	(7 << 12)
#define PRSNT_CHANGE_DETECTED	(1 << 16)
#define ISO_PFAULT_DETECTED	(1 << 17)
#define BUTTON_PRESS_DETECTED	(1 << 18)
#define MRL_CHANGE_DETECTED	(1 << 19)
#define CON_PFAULT_DETECTED	(1 << 20)
#define PRSNT_CHANGE_INTR_MASK	(1 << 24)
#define ISO_PFAULT_INTR_MASK	(1 << 25)
#define BUTTON_PRESS_INTR_MASK	(1 << 26)
#define MRL_CHANGE_INTR_MASK	(1 << 27)
#define CON_PFAULT_INTR_MASK	(1 << 28)
#define MRL_CHANGE_SERR_MASK	(1 << 29)
#define CON_PFAULT_SERR_MASK	(1 << 30)
#define SLOT_REG_RSVDZ_MASK	((1 << 15) | (7 << 21))

/*
 * SHPC Command Code definitions
 *
 *     Slot Operation				00h - 3Fh
 *     Set Bus Segment Speed/Mode A		40h - 47h
 *     Power-Only All Slots			48h
 *     Enable All Slots				49h
 *     Set Bus Segment Speed/Mode B (PI=2)	50h - 5Fh
 *     Reserved Command Codes			60h - BFh
 *     Vendor Specific Commands			C0h - FFh
 */
#define SET_SLOT_PWR		0x01	/* Slot Operation */
#define SET_SLOT_ENABLE		0x02
#define SET_SLOT_DISABLE	0x03
#define SET_PWR_ON		0x04
#define SET_PWR_BLINK		0x08
#define SET_PWR_OFF		0x0c
#define SET_ATTN_ON		0x10
#define SET_ATTN_BLINK		0x20
#define SET_ATTN_OFF		0x30
#define SETA_PCI_33MHZ		0x40	/* Set Bus Segment Speed/Mode A */
#define SETA_PCI_66MHZ		0x41
#define SETA_PCIX_66MHZ		0x42
#define SETA_PCIX_100MHZ	0x43
#define SETA_PCIX_133MHZ	0x44
#define SETA_RESERVED1		0x45
#define SETA_RESERVED2		0x46
#define SETA_RESERVED3		0x47
#define SET_PWR_ONLY_ALL	0x48	/* Power-Only All Slots */
#define SET_ENABLE_ALL		0x49	/* Enable All Slots */
#define	SETB_PCI_33MHZ		0x50	/* Set Bus Segment Speed/Mode B */
#define SETB_PCI_66MHZ		0x51
#define SETB_PCIX_66MHZ_PM	0x52
#define SETB_PCIX_100MHZ_PM	0x53
#define SETB_PCIX_133MHZ_PM	0x54
#define SETB_PCIX_66MHZ_EM	0x55
#define SETB_PCIX_100MHZ_EM	0x56
#define SETB_PCIX_133MHZ_EM	0x57
#define SETB_PCIX_66MHZ_266	0x58
#define SETB_PCIX_100MHZ_266	0x59
#define SETB_PCIX_133MHZ_266	0x5a
#define SETB_PCIX_66MHZ_533	0x5b
#define SETB_PCIX_100MHZ_533	0x5c
#define SETB_PCIX_133MHZ_533	0x5d
#define SETB_RESERVED1		0x5e
#define SETB_RESERVED2		0x5f

/*
 * SHPC controller command error code
 */
#define SWITCH_OPEN		0x1
#define INVALID_CMD		0x2
#define INVALID_SPEED_MODE	0x4

/*
 * For accessing SHPC Working Register Set via PCI Configuration Space
 */
#define DWORD_SELECT		0x2
#define DWORD_DATA		0x4

/* Field Offset in Logical Slot Register - byte boundary */
#define SLOT_EVENT_LATCH	0x2
#define SLOT_SERR_INT_MASK	0x3

static irqreturn_t shpc_isr(int irq, void *dev_id);
static void start_int_poll_timer(struct controller *ctrl, int sec);

static inline u8 shpc_readb(struct controller *ctrl, int reg)
{
	return readb(ctrl->creg + reg);
}

static inline u16 shpc_readw(struct controller *ctrl, int reg)
{
	return readw(ctrl->creg + reg);
}

static inline void shpc_writew(struct controller *ctrl, int reg, u16 val)
{
	writew(val, ctrl->creg + reg);
}

static inline u32 shpc_readl(struct controller *ctrl, int reg)
{
	return readl(ctrl->creg + reg);
}

static inline void shpc_writel(struct controller *ctrl, int reg, u32 val)
{
	writel(val, ctrl->creg + reg);
}

static inline int shpc_indirect_read(struct controller *ctrl, int index,
				     u32 *value)
{
	int rc;
	u32 cap_offset = ctrl->cap_offset;
	struct pci_dev *pdev = ctrl->pci_dev;

	rc = pci_write_config_byte(pdev, cap_offset + DWORD_SELECT, index);
	if (rc)
		return rc;
	return pci_read_config_dword(pdev, cap_offset + DWORD_DATA, value);
}

/*
 * This is the interrupt polling timeout function.
 */
static void int_poll_timeout(struct timer_list *t)
{
	struct controller *ctrl = from_timer(ctrl, t, poll_timer);

	/* Poll for interrupt events.  regs == NULL => polling */
	shpc_isr(0, ctrl);

	if (!shpchp_poll_time)
		shpchp_poll_time = 2; /* default polling interval is 2 sec */

	start_int_poll_timer(ctrl, shpchp_poll_time);
}

/*
 * This function starts the interrupt polling timer.
 */
static void start_int_poll_timer(struct controller *ctrl, int sec)
{
	/* Clamp to sane value */
	if ((sec <= 0) || (sec > 60))
		sec = 2;

	ctrl->poll_timer.expires = jiffies + sec * HZ;
	add_timer(&ctrl->poll_timer);
}

static inline int is_ctrl_busy(struct controller *ctrl)
{
	u16 cmd_status = shpc_readw(ctrl, CMD_STATUS);
	return cmd_status & 0x1;
}

/*
 * Returns 1 if SHPC finishes executing a command within 1 sec,
 * otherwise returns 0.
 */
static inline int shpc_poll_ctrl_busy(struct controller *ctrl)
{
	int i;

	if (!is_ctrl_busy(ctrl))
		return 1;

	/* Check every 0.1 sec for a total of 1 sec */
	for (i = 0; i < 10; i++) {
		msleep(100);
		if (!is_ctrl_busy(ctrl))
			return 1;
	}

	return 0;
}

static inline int shpc_wait_cmd(struct controller *ctrl)
{
	int retval = 0;
	unsigned long timeout = msecs_to_jiffies(1000);
	int rc;

	if (shpchp_poll_mode)
		rc = shpc_poll_ctrl_busy(ctrl);
	else
		rc = wait_event_interruptible_timeout(ctrl->queue,
						!is_ctrl_busy(ctrl), timeout);
	if (!rc && is_ctrl_busy(ctrl)) {
		retval = -EIO;
		ctrl_err(ctrl, "Command not completed in 1000 msec\n");
	} else if (rc < 0) {
		retval = -EINTR;
		ctrl_info(ctrl, "Command was interrupted by a signal\n");
	}

	return retval;
}

static int shpc_write_cmd(struct slot *slot, u8 t_slot, u8 cmd)
{
	struct controller *ctrl = slot->ctrl;
	u16 cmd_status;
	int retval = 0;
	u16 temp_word;

	mutex_lock(&slot->ctrl->cmd_lock);

	if (!shpc_poll_ctrl_busy(ctrl)) {
		/* After 1 sec and the controller is still busy */
		ctrl_err(ctrl, "Controller is still busy after 1 sec\n");
		retval = -EBUSY;
		goto out;
	}

	++t_slot;
	temp_word =  (t_slot << 8) | (cmd & 0xFF);
	ctrl_dbg(ctrl, "%s: t_slot %x cmd %x\n", __func__, t_slot, cmd);

	/* To make sure the Controller Busy bit is 0 before we send out the
	 * command.
	 */
	shpc_writew(ctrl, CMD, temp_word);

	/*
	 * Wait for command completion.
	 */
	retval = shpc_wait_cmd(slot->ctrl);
	if (retval)
		goto out;

	cmd_status = shpchp_check_cmd_status(slot->ctrl);
	if (cmd_status) {
		ctrl_err(ctrl, "Failed to issued command 0x%x (error code = %d)\n",
			 cmd, cmd_status);
		retval = -EIO;
	}
 out:
	mutex_unlock(&slot->ctrl->cmd_lock);
	return retval;
}

int shpchp_check_cmd_status(struct controller *ctrl)
{
	int retval = 0;
	u16 cmd_status = shpc_readw(ctrl, CMD_STATUS) & 0x000F;

	switch (cmd_status >> 1) {
	case 0:
		retval = 0;
		break;
	case 1:
		retval = SWITCH_OPEN;
		ctrl_err(ctrl, "Switch opened!\n");
		break;
	case 2:
		retval = INVALID_CMD;
		ctrl_err(ctrl, "Invalid HPC command!\n");
		break;
	case 4:
		retval = INVALID_SPEED_MODE;
		ctrl_err(ctrl, "Invalid bus speed/mode!\n");
		break;
	default:
		retval = cmd_status;
	}

	return retval;
}


int shpchp_get_attention_status(struct slot *slot, u8 *status)
{
	struct controller *ctrl = slot->ctrl;
	u32 slot_reg = shpc_readl(ctrl, SLOT_REG(slot->hp_slot));
	u8 state = (slot_reg & ATN_LED_STATE_MASK) >> ATN_LED_STATE_SHIFT;

	switch (state) {
	case ATN_LED_STATE_ON:
		*status = 1;	/* On */
		break;
	case ATN_LED_STATE_BLINK:
		*status = 2;	/* Blink */
		break;
	case ATN_LED_STATE_OFF:
		*status = 0;	/* Off */
		break;
	default:
		*status = 0xFF;	/* Reserved */
		break;
	}

	return 0;
}

int shpchp_get_power_status(struct slot *slot, u8 *status)
{
	struct controller *ctrl = slot->ctrl;
	u32 slot_reg = shpc_readl(ctrl, SLOT_REG(slot->hp_slot));
	u8 state = (slot_reg & SLOT_STATE_MASK) >> SLOT_STATE_SHIFT;

	switch (state) {
	case SLOT_STATE_PWRONLY:
		*status = 2;	/* Powered only */
		break;
	case SLOT_STATE_ENABLED:
		*status = 1;	/* Enabled */
		break;
	case SLOT_STATE_DISABLED:
		*status = 0;	/* Disabled */
		break;
	default:
		*status = 0xFF;	/* Reserved */
		break;
	}

	return 0;
}


int shpchp_get_latch_status(struct slot *slot, u8 *status)
{
	struct controller *ctrl = slot->ctrl;
	u32 slot_reg = shpc_readl(ctrl, SLOT_REG(slot->hp_slot));

	*status = !!(slot_reg & MRL_SENSOR);	/* 0 -> close; 1 -> open */

	return 0;
}

int shpchp_get_adapter_status(struct slot *slot, u8 *status)
{
	struct controller *ctrl = slot->ctrl;
	u32 slot_reg = shpc_readl(ctrl, SLOT_REG(slot->hp_slot));
	u8 state = (slot_reg & PRSNT_MASK) >> PRSNT_SHIFT;

	*status = (state != 0x3) ? 1 : 0;

	return 0;
}

int shpchp_get_prog_int(struct slot *slot, u8 *prog_int)
{
	struct controller *ctrl = slot->ctrl;

	*prog_int = shpc_readb(ctrl, PROG_INTERFACE);

	return 0;
}

int shpchp_get_adapter_speed(struct slot *slot, enum pci_bus_speed *value)
{
	int retval = 0;
	struct controller *ctrl = slot->ctrl;
	u32 slot_reg = shpc_readl(ctrl, SLOT_REG(slot->hp_slot));
	u8 m66_cap  = !!(slot_reg & MHZ66_CAP);
	u8 pi, pcix_cap;

	retval = shpchp_get_prog_int(slot, &pi);
	if (retval)
		return retval;

	switch (pi) {
	case 1:
		pcix_cap = (slot_reg & PCIX_CAP_MASK_PI1) >> PCIX_CAP_SHIFT;
		break;
	case 2:
		pcix_cap = (slot_reg & PCIX_CAP_MASK_PI2) >> PCIX_CAP_SHIFT;
		break;
	default:
		return -ENODEV;
	}

	ctrl_dbg(ctrl, "%s: slot_reg = %x, pcix_cap = %x, m66_cap = %x\n",
		 __func__, slot_reg, pcix_cap, m66_cap);

	switch (pcix_cap) {
	case 0x0:
		*value = m66_cap ? PCI_SPEED_66MHz : PCI_SPEED_33MHz;
		break;
	case 0x1:
		*value = PCI_SPEED_66MHz_PCIX;
		break;
	case 0x3:
		*value = PCI_SPEED_133MHz_PCIX;
		break;
	case 0x4:
		*value = PCI_SPEED_133MHz_PCIX_266;
		break;
	case 0x5:
		*value = PCI_SPEED_133MHz_PCIX_533;
		break;
	case 0x2:
	default:
		*value = PCI_SPEED_UNKNOWN;
		retval = -ENODEV;
		break;
	}

	ctrl_dbg(ctrl, "Adapter speed = %d\n", *value);
	return retval;
}

int shpchp_query_power_fault(struct slot *slot)
{
	struct controller *ctrl = slot->ctrl;
	u32 slot_reg = shpc_readl(ctrl, SLOT_REG(slot->hp_slot));

	/* Note: Logic 0 => fault */
	return !(slot_reg & POWER_FAULT);
}

int shpchp_set_attention_status(struct slot *slot, u8 value)
{
	u8 slot_cmd = 0;

	switch (value) {
		case 0:
			slot_cmd = SET_ATTN_OFF;	/* OFF */
			break;
		case 1:
			slot_cmd = SET_ATTN_ON;		/* ON */
			break;
		case 2:
			slot_cmd = SET_ATTN_BLINK;	/* BLINK */
			break;
		default:
			return -1;
	}

	return shpc_write_cmd(slot, slot->hp_slot, slot_cmd);
}


void shpchp_green_led_on(struct slot *slot)
{
	shpc_write_cmd(slot, slot->hp_slot, SET_PWR_ON);
}

void shpchp_green_led_off(struct slot *slot)
{
	shpc_write_cmd(slot, slot->hp_slot, SET_PWR_OFF);
}

void shpchp_green_led_blink(struct slot *slot)
{
	shpc_write_cmd(slot, slot->hp_slot, SET_PWR_BLINK);
}

void shpchp_release_ctlr(struct controller *ctrl)
{
	int i;
	u32 slot_reg, serr_int;

	/*
	 * Mask event interrupts and SERRs of all slots
	 */
	for (i = 0; i < ctrl->num_slots; i++) {
		slot_reg = shpc_readl(ctrl, SLOT_REG(i));
		slot_reg |= (PRSNT_CHANGE_INTR_MASK | ISO_PFAULT_INTR_MASK |
			     BUTTON_PRESS_INTR_MASK | MRL_CHANGE_INTR_MASK |
			     CON_PFAULT_INTR_MASK   | MRL_CHANGE_SERR_MASK |
			     CON_PFAULT_SERR_MASK);
		slot_reg &= ~SLOT_REG_RSVDZ_MASK;
		shpc_writel(ctrl, SLOT_REG(i), slot_reg);
	}

	cleanup_slots(ctrl);

	/*
	 * Mask SERR and System Interrupt generation
	 */
	serr_int = shpc_readl(ctrl, SERR_INTR_ENABLE);
	serr_int |= (GLOBAL_INTR_MASK  | GLOBAL_SERR_MASK |
		     COMMAND_INTR_MASK | ARBITER_SERR_MASK);
	serr_int &= ~SERR_INTR_RSVDZ_MASK;
	shpc_writel(ctrl, SERR_INTR_ENABLE, serr_int);

	if (shpchp_poll_mode)
		del_timer(&ctrl->poll_timer);
	else {
		free_irq(ctrl->pci_dev->irq, ctrl);
		pci_disable_msi(ctrl->pci_dev);
	}

	iounmap(ctrl->creg);
	release_mem_region(ctrl->mmio_base, ctrl->mmio_size);
}

int shpchp_power_on_slot(struct slot *slot)
{
	int retval;

	retval = shpc_write_cmd(slot, slot->hp_slot, SET_SLOT_PWR);
	if (retval)
		ctrl_err(slot->ctrl, "%s: Write command failed!\n", __func__);

	return retval;
}

int shpchp_slot_enable(struct slot *slot)
{
	int retval;

	/* Slot - Enable, Power Indicator - Blink, Attention Indicator - Off */
	retval = shpc_write_cmd(slot, slot->hp_slot,
			SET_SLOT_ENABLE | SET_PWR_BLINK | SET_ATTN_OFF);
	if (retval)
		ctrl_err(slot->ctrl, "%s: Write command failed!\n", __func__);

	return retval;
}

int shpchp_slot_disable(struct slot *slot)
{
	int retval;

	/* Slot - Disable, Power Indicator - Off, Attention Indicator - On */
	retval = shpc_write_cmd(slot, slot->hp_slot,
			SET_SLOT_DISABLE | SET_PWR_OFF | SET_ATTN_ON);
	if (retval)
		ctrl_err(slot->ctrl, "%s: Write command failed!\n", __func__);

	return retval;
}

static int shpc_get_cur_bus_speed(struct controller *ctrl)
{
	int retval = 0;
	struct pci_bus *bus = ctrl->pci_dev->subordinate;
	enum pci_bus_speed bus_speed = PCI_SPEED_UNKNOWN;
	u16 sec_bus_reg = shpc_readw(ctrl, SEC_BUS_CONFIG);
	u8 pi = shpc_readb(ctrl, PROG_INTERFACE);
	u8 speed_mode = (pi == 2) ? (sec_bus_reg & 0xF) : (sec_bus_reg & 0x7);

	if ((pi == 1) && (speed_mode > 4)) {
		retval = -ENODEV;
		goto out;
	}

	switch (speed_mode) {
	case 0x0:
		bus_speed = PCI_SPEED_33MHz;
		break;
	case 0x1:
		bus_speed = PCI_SPEED_66MHz;
		break;
	case 0x2:
		bus_speed = PCI_SPEED_66MHz_PCIX;
		break;
	case 0x3:
		bus_speed = PCI_SPEED_100MHz_PCIX;
		break;
	case 0x4:
		bus_speed = PCI_SPEED_133MHz_PCIX;
		break;
	case 0x5:
		bus_speed = PCI_SPEED_66MHz_PCIX_ECC;
		break;
	case 0x6:
		bus_speed = PCI_SPEED_100MHz_PCIX_ECC;
		break;
	case 0x7:
		bus_speed = PCI_SPEED_133MHz_PCIX_ECC;
		break;
	case 0x8:
		bus_speed = PCI_SPEED_66MHz_PCIX_266;
		break;
	case 0x9:
		bus_speed = PCI_SPEED_100MHz_PCIX_266;
		break;
	case 0xa:
		bus_speed = PCI_SPEED_133MHz_PCIX_266;
		break;
	case 0xb:
		bus_speed = PCI_SPEED_66MHz_PCIX_533;
		break;
	case 0xc:
		bus_speed = PCI_SPEED_100MHz_PCIX_533;
		break;
	case 0xd:
		bus_speed = PCI_SPEED_133MHz_PCIX_533;
		break;
	default:
		retval = -ENODEV;
		break;
	}

 out:
	bus->cur_bus_speed = bus_speed;
	dbg("Current bus speed = %d\n", bus_speed);
	return retval;
}


int shpchp_set_bus_speed_mode(struct slot *slot, enum pci_bus_speed value)
{
	int retval;
	struct controller *ctrl = slot->ctrl;
	u8 pi, cmd;

	pi = shpc_readb(ctrl, PROG_INTERFACE);
	if ((pi == 1) && (value > PCI_SPEED_133MHz_PCIX))
		return -EINVAL;

	switch (value) {
	case PCI_SPEED_33MHz:
		cmd = SETA_PCI_33MHZ;
		break;
	case PCI_SPEED_66MHz:
		cmd = SETA_PCI_66MHZ;
		break;
	case PCI_SPEED_66MHz_PCIX:
		cmd = SETA_PCIX_66MHZ;
		break;
	case PCI_SPEED_100MHz_PCIX:
		cmd = SETA_PCIX_100MHZ;
		break;
	case PCI_SPEED_133MHz_PCIX:
		cmd = SETA_PCIX_133MHZ;
		break;
	case PCI_SPEED_66MHz_PCIX_ECC:
		cmd = SETB_PCIX_66MHZ_EM;
		break;
	case PCI_SPEED_100MHz_PCIX_ECC:
		cmd = SETB_PCIX_100MHZ_EM;
		break;
	case PCI_SPEED_133MHz_PCIX_ECC:
		cmd = SETB_PCIX_133MHZ_EM;
		break;
	case PCI_SPEED_66MHz_PCIX_266:
		cmd = SETB_PCIX_66MHZ_266;
		break;
	case PCI_SPEED_100MHz_PCIX_266:
		cmd = SETB_PCIX_100MHZ_266;
		break;
	case PCI_SPEED_133MHz_PCIX_266:
		cmd = SETB_PCIX_133MHZ_266;
		break;
	case PCI_SPEED_66MHz_PCIX_533:
		cmd = SETB_PCIX_66MHZ_533;
		break;
	case PCI_SPEED_100MHz_PCIX_533:
		cmd = SETB_PCIX_100MHZ_533;
		break;
	case PCI_SPEED_133MHz_PCIX_533:
		cmd = SETB_PCIX_133MHZ_533;
		break;
	default:
		return -EINVAL;
	}

	retval = shpc_write_cmd(slot, 0, cmd);
	if (retval)
		ctrl_err(ctrl, "%s: Write command failed!\n", __func__);
	else
		shpc_get_cur_bus_speed(ctrl);

	return retval;
}

static irqreturn_t shpc_isr(int irq, void *dev_id)
{
	struct controller *ctrl = (struct controller *)dev_id;
	u32 serr_int, slot_reg, intr_loc, intr_loc2;
	int hp_slot;

	/* Check to see if it was our interrupt */
	intr_loc = shpc_readl(ctrl, INTR_LOC);
	if (!intr_loc)
		return IRQ_NONE;

	ctrl_dbg(ctrl, "%s: intr_loc = %x\n", __func__, intr_loc);

	if (!shpchp_poll_mode) {
		/*
		 * Mask Global Interrupt Mask - see implementation
		 * note on p. 139 of SHPC spec rev 1.0
		 */
		serr_int = shpc_readl(ctrl, SERR_INTR_ENABLE);
		serr_int |= GLOBAL_INTR_MASK;
		serr_int &= ~SERR_INTR_RSVDZ_MASK;
		shpc_writel(ctrl, SERR_INTR_ENABLE, serr_int);

		intr_loc2 = shpc_readl(ctrl, INTR_LOC);
		ctrl_dbg(ctrl, "%s: intr_loc2 = %x\n", __func__, intr_loc2);
	}

	if (intr_loc & CMD_INTR_PENDING) {
		/*
		 * Command Complete Interrupt Pending
		 * RO only - clear by writing 1 to the Command Completion
		 * Detect bit in Controller SERR-INT register
		 */
		serr_int = shpc_readl(ctrl, SERR_INTR_ENABLE);
		serr_int &= ~SERR_INTR_RSVDZ_MASK;
		shpc_writel(ctrl, SERR_INTR_ENABLE, serr_int);

		wake_up_interruptible(&ctrl->queue);
	}

	if (!(intr_loc & ~CMD_INTR_PENDING))
		goto out;

	for (hp_slot = 0; hp_slot < ctrl->num_slots; hp_slot++) {
		/* To find out which slot has interrupt pending */
		if (!(intr_loc & SLOT_INTR_PENDING(hp_slot)))
			continue;

		slot_reg = shpc_readl(ctrl, SLOT_REG(hp_slot));
		ctrl_dbg(ctrl, "Slot %x with intr, slot register = %x\n",
			 hp_slot, slot_reg);

		if (slot_reg & MRL_CHANGE_DETECTED)
			shpchp_handle_switch_change(hp_slot, ctrl);

		if (slot_reg & BUTTON_PRESS_DETECTED)
			shpchp_handle_attention_button(hp_slot, ctrl);

		if (slot_reg & PRSNT_CHANGE_DETECTED)
			shpchp_handle_presence_change(hp_slot, ctrl);

		if (slot_reg & (ISO_PFAULT_DETECTED | CON_PFAULT_DETECTED))
			shpchp_handle_power_fault(hp_slot, ctrl);

		/* Clear all slot events */
		slot_reg &= ~SLOT_REG_RSVDZ_MASK;
		shpc_writel(ctrl, SLOT_REG(hp_slot), slot_reg);
	}
 out:
	if (!shpchp_poll_mode) {
		/* Unmask Global Interrupt Mask */
		serr_int = shpc_readl(ctrl, SERR_INTR_ENABLE);
		serr_int &= ~(GLOBAL_INTR_MASK | SERR_INTR_RSVDZ_MASK);
		shpc_writel(ctrl, SERR_INTR_ENABLE, serr_int);
	}

	return IRQ_HANDLED;
}

static int shpc_get_max_bus_speed(struct controller *ctrl)
{
	int retval = 0;
	struct pci_bus *bus = ctrl->pci_dev->subordinate;
	enum pci_bus_speed bus_speed = PCI_SPEED_UNKNOWN;
	u8 pi = shpc_readb(ctrl, PROG_INTERFACE);
	u32 slot_avail1 = shpc_readl(ctrl, SLOT_AVAIL1);
	u32 slot_avail2 = shpc_readl(ctrl, SLOT_AVAIL2);

	if (pi == 2) {
		if (slot_avail2 & SLOT_133MHZ_PCIX_533)
			bus_speed = PCI_SPEED_133MHz_PCIX_533;
		else if (slot_avail2 & SLOT_100MHZ_PCIX_533)
			bus_speed = PCI_SPEED_100MHz_PCIX_533;
		else if (slot_avail2 & SLOT_66MHZ_PCIX_533)
			bus_speed = PCI_SPEED_66MHz_PCIX_533;
		else if (slot_avail2 & SLOT_133MHZ_PCIX_266)
			bus_speed = PCI_SPEED_133MHz_PCIX_266;
		else if (slot_avail2 & SLOT_100MHZ_PCIX_266)
			bus_speed = PCI_SPEED_100MHz_PCIX_266;
		else if (slot_avail2 & SLOT_66MHZ_PCIX_266)
			bus_speed = PCI_SPEED_66MHz_PCIX_266;
	}

	if (bus_speed == PCI_SPEED_UNKNOWN) {
		if (slot_avail1 & SLOT_133MHZ_PCIX)
			bus_speed = PCI_SPEED_133MHz_PCIX;
		else if (slot_avail1 & SLOT_100MHZ_PCIX)
			bus_speed = PCI_SPEED_100MHz_PCIX;
		else if (slot_avail1 & SLOT_66MHZ_PCIX)
			bus_speed = PCI_SPEED_66MHz_PCIX;
		else if (slot_avail2 & SLOT_66MHZ)
			bus_speed = PCI_SPEED_66MHz;
		else if (slot_avail1 & SLOT_33MHZ)
			bus_speed = PCI_SPEED_33MHz;
		else
			retval = -ENODEV;
	}

	bus->max_bus_speed = bus_speed;
	ctrl_dbg(ctrl, "Max bus speed = %d\n", bus_speed);

	return retval;
}

int shpc_init(struct controller *ctrl, struct pci_dev *pdev)
{
	int rc = -1, num_slots = 0;
	u8 hp_slot;
	u32 shpc_base_offset;
	u32 tempdword, slot_reg, slot_config;
	u8 i;

	ctrl->pci_dev = pdev;  /* pci_dev of the P2P bridge */
	ctrl_dbg(ctrl, "Hotplug Controller:\n");

	if (pdev->vendor == PCI_VENDOR_ID_AMD &&
	    pdev->device == PCI_DEVICE_ID_AMD_GOLAM_7450) {
		/* amd shpc driver doesn't use Base Offset; assume 0 */
		ctrl->mmio_base = pci_resource_start(pdev, 0);
		ctrl->mmio_size = pci_resource_len(pdev, 0);
	} else {
		ctrl->cap_offset = pci_find_capability(pdev, PCI_CAP_ID_SHPC);
		if (!ctrl->cap_offset) {
			ctrl_err(ctrl, "Cannot find PCI capability\n");
			goto abort;
		}
		ctrl_dbg(ctrl, " cap_offset = %x\n", ctrl->cap_offset);

		rc = shpc_indirect_read(ctrl, 0, &shpc_base_offset);
		if (rc) {
			ctrl_err(ctrl, "Cannot read base_offset\n");
			goto abort;
		}

		rc = shpc_indirect_read(ctrl, 3, &tempdword);
		if (rc) {
			ctrl_err(ctrl, "Cannot read slot config\n");
			goto abort;
		}
		num_slots = tempdword & SLOT_NUM;
		ctrl_dbg(ctrl, " num_slots (indirect) %x\n", num_slots);

		for (i = 0; i < 9 + num_slots; i++) {
			rc = shpc_indirect_read(ctrl, i, &tempdword);
			if (rc) {
				ctrl_err(ctrl, "Cannot read creg (index = %d)\n",
					 i);
				goto abort;
			}
			ctrl_dbg(ctrl, " offset %d: value %x\n", i, tempdword);
		}

		ctrl->mmio_base =
			pci_resource_start(pdev, 0) + shpc_base_offset;
		ctrl->mmio_size = 0x24 + 0x4 * num_slots;
	}

	ctrl_info(ctrl, "HPC vendor_id %x device_id %x ss_vid %x ss_did %x\n",
		  pdev->vendor, pdev->device, pdev->subsystem_vendor,
		  pdev->subsystem_device);

	rc = pci_enable_device(pdev);
	if (rc) {
		ctrl_err(ctrl, "pci_enable_device failed\n");
		goto abort;
	}

	if (!request_mem_region(ctrl->mmio_base, ctrl->mmio_size, MY_NAME)) {
		ctrl_err(ctrl, "Cannot reserve MMIO region\n");
		rc = -1;
		goto abort;
	}

	ctrl->creg = ioremap(ctrl->mmio_base, ctrl->mmio_size);
	if (!ctrl->creg) {
		ctrl_err(ctrl, "Cannot remap MMIO region %lx @ %lx\n",
			 ctrl->mmio_size, ctrl->mmio_base);
		release_mem_region(ctrl->mmio_base, ctrl->mmio_size);
		rc = -1;
		goto abort;
	}
	ctrl_dbg(ctrl, "ctrl->creg %p\n", ctrl->creg);

	mutex_init(&ctrl->crit_sect);
	mutex_init(&ctrl->cmd_lock);

	/* Setup wait queue */
	init_waitqueue_head(&ctrl->queue);

	/* Return PCI Controller Info */
	slot_config = shpc_readl(ctrl, SLOT_CONFIG);
	ctrl->slot_device_offset = (slot_config & FIRST_DEV_NUM) >> 8;
	ctrl->num_slots = slot_config & SLOT_NUM;
	ctrl->first_slot = (slot_config & PSN) >> 16;
	ctrl->slot_num_inc = ((slot_config & UPDOWN) >> 29) ? 1 : -1;

	/* Mask Global Interrupt Mask & Command Complete Interrupt Mask */
	tempdword = shpc_readl(ctrl, SERR_INTR_ENABLE);
	ctrl_dbg(ctrl, "SERR_INTR_ENABLE = %x\n", tempdword);
	tempdword |= (GLOBAL_INTR_MASK  | GLOBAL_SERR_MASK |
		      COMMAND_INTR_MASK | ARBITER_SERR_MASK);
	tempdword &= ~SERR_INTR_RSVDZ_MASK;
	shpc_writel(ctrl, SERR_INTR_ENABLE, tempdword);
	tempdword = shpc_readl(ctrl, SERR_INTR_ENABLE);
	ctrl_dbg(ctrl, "SERR_INTR_ENABLE = %x\n", tempdword);

	/* Mask the MRL sensor SERR Mask of individual slot in
	 * Slot SERR-INT Mask & clear all the existing event if any
	 */
	for (hp_slot = 0; hp_slot < ctrl->num_slots; hp_slot++) {
		slot_reg = shpc_readl(ctrl, SLOT_REG(hp_slot));
		ctrl_dbg(ctrl, "Default Logical Slot Register %d value %x\n",
			 hp_slot, slot_reg);
		slot_reg |= (PRSNT_CHANGE_INTR_MASK | ISO_PFAULT_INTR_MASK |
			     BUTTON_PRESS_INTR_MASK | MRL_CHANGE_INTR_MASK |
			     CON_PFAULT_INTR_MASK   | MRL_CHANGE_SERR_MASK |
			     CON_PFAULT_SERR_MASK);
		slot_reg &= ~SLOT_REG_RSVDZ_MASK;
		shpc_writel(ctrl, SLOT_REG(hp_slot), slot_reg);
	}

	if (shpchp_poll_mode) {
		/* Install interrupt polling timer. Start with 10 sec delay */
		timer_setup(&ctrl->poll_timer, int_poll_timeout, 0);
		start_int_poll_timer(ctrl, 10);
	} else {
		/* Installs the interrupt handler */
		rc = pci_enable_msi(pdev);
		if (rc) {
			ctrl_info(ctrl, "Can't get msi for the hotplug controller\n");
			ctrl_info(ctrl, "Use INTx for the hotplug controller\n");
		} else {
			pci_set_master(pdev);
		}

		rc = request_irq(ctrl->pci_dev->irq, shpc_isr, IRQF_SHARED,
				 MY_NAME, (void *)ctrl);
		ctrl_dbg(ctrl, "request_irq %d (returns %d)\n",
			 ctrl->pci_dev->irq, rc);
		if (rc) {
			ctrl_err(ctrl, "Can't get irq %d for the hotplug controller\n",
				 ctrl->pci_dev->irq);
			goto abort_iounmap;
		}
	}
	ctrl_dbg(ctrl, "HPC at %s irq=%x\n", pci_name(pdev), pdev->irq);

	shpc_get_max_bus_speed(ctrl);
	shpc_get_cur_bus_speed(ctrl);

	/*
	 * Unmask all event interrupts of all slots
	 */
	for (hp_slot = 0; hp_slot < ctrl->num_slots; hp_slot++) {
		slot_reg = shpc_readl(ctrl, SLOT_REG(hp_slot));
		ctrl_dbg(ctrl, "Default Logical Slot Register %d value %x\n",
			 hp_slot, slot_reg);
		slot_reg &= ~(PRSNT_CHANGE_INTR_MASK | ISO_PFAULT_INTR_MASK |
			      BUTTON_PRESS_INTR_MASK | MRL_CHANGE_INTR_MASK |
			      CON_PFAULT_INTR_MASK | SLOT_REG_RSVDZ_MASK);
		shpc_writel(ctrl, SLOT_REG(hp_slot), slot_reg);
	}
	if (!shpchp_poll_mode) {
		/* Unmask all general input interrupts and SERR */
		tempdword = shpc_readl(ctrl, SERR_INTR_ENABLE);
		tempdword &= ~(GLOBAL_INTR_MASK | COMMAND_INTR_MASK |
			       SERR_INTR_RSVDZ_MASK);
		shpc_writel(ctrl, SERR_INTR_ENABLE, tempdword);
		tempdword = shpc_readl(ctrl, SERR_INTR_ENABLE);
		ctrl_dbg(ctrl, "SERR_INTR_ENABLE = %x\n", tempdword);
	}

	return 0;

	/* We end up here for the many possible ways to fail this API.  */
abort_iounmap:
	iounmap(ctrl->creg);
abort:
	return rc;
}
