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
#include <linux/pci.h>
#include <linux/interrupt.h>

#include "shpchp.h"

#ifdef DEBUG
#define DBG_K_TRACE_ENTRY      ((unsigned int)0x00000001)	/* On function entry */
#define DBG_K_TRACE_EXIT       ((unsigned int)0x00000002)	/* On function exit */
#define DBG_K_INFO             ((unsigned int)0x00000004)	/* Info messages */
#define DBG_K_ERROR            ((unsigned int)0x00000008)	/* Error messages */
#define DBG_K_TRACE            (DBG_K_TRACE_ENTRY|DBG_K_TRACE_EXIT)
#define DBG_K_STANDARD         (DBG_K_INFO|DBG_K_ERROR|DBG_K_TRACE)
/* Redefine this flagword to set debug level */
#define DEBUG_LEVEL            DBG_K_STANDARD

#define DEFINE_DBG_BUFFER     char __dbg_str_buf[256];

#define DBG_PRINT( dbg_flags, args... )              \
	do {                                             \
	  if ( DEBUG_LEVEL & ( dbg_flags ) )             \
	  {                                              \
	    int len;                                     \
	    len = sprintf( __dbg_str_buf, "%s:%d: %s: ", \
		  __FILE__, __LINE__, __FUNCTION__ );    \
	    sprintf( __dbg_str_buf + len, args );        \
	    printk( KERN_NOTICE "%s\n", __dbg_str_buf ); \
	  }                                              \
	} while (0)

#define DBG_ENTER_ROUTINE	DBG_PRINT (DBG_K_TRACE_ENTRY, "%s", "[Entry]");
#define DBG_LEAVE_ROUTINE	DBG_PRINT (DBG_K_TRACE_EXIT, "%s", "[Exit]");
#else
#define DEFINE_DBG_BUFFER
#define DBG_ENTER_ROUTINE
#define DBG_LEAVE_ROUTINE
#endif				/* DEBUG */

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
#define SLOT_REG_RSVDZ_MASK	(1 << 15) | (7 << 21)

/* SHPC 'write' operations/commands */

/* Slot operation - 0x00h to 0x3Fh */

#define NO_CHANGE		0x00

/* Slot state - Bits 0 & 1 of controller command register */
#define SET_SLOT_PWR		0x01	
#define SET_SLOT_ENABLE		0x02	
#define SET_SLOT_DISABLE	0x03	

/* Power indicator state - Bits 2 & 3 of controller command register*/
#define SET_PWR_ON		0x04	
#define SET_PWR_BLINK		0x08	
#define SET_PWR_OFF		0x0C	

/* Attention indicator state - Bits 4 & 5 of controller command register*/
#define SET_ATTN_ON		0x010	
#define SET_ATTN_BLINK		0x020
#define SET_ATTN_OFF		0x030	

/* Set bus speed/mode A - 0x40h to 0x47h */
#define SETA_PCI_33MHZ		0x40
#define SETA_PCI_66MHZ		0x41
#define SETA_PCIX_66MHZ		0x42
#define SETA_PCIX_100MHZ	0x43
#define SETA_PCIX_133MHZ	0x44
#define RESERV_1		0x45
#define RESERV_2		0x46
#define RESERV_3		0x47

/* Set bus speed/mode B - 0x50h to 0x5fh */
#define	SETB_PCI_33MHZ		0x50
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


/* Power-on all slots - 0x48h */
#define SET_PWR_ON_ALL		0x48

/* Enable all slots	- 0x49h */
#define SET_ENABLE_ALL		0x49

/*  SHPC controller command error code */
#define SWITCH_OPEN		0x1
#define INVALID_CMD		0x2
#define INVALID_SPEED_MODE	0x4

/* For accessing SHPC Working Register Set */
#define DWORD_SELECT		0x2
#define DWORD_DATA		0x4
#define BASE_OFFSET		0x0

/* Field Offset in Logical Slot Register - byte boundary */
#define SLOT_EVENT_LATCH	0x2
#define SLOT_SERR_INT_MASK	0x3

static spinlock_t hpc_event_lock;

DEFINE_DBG_BUFFER		/* Debug string buffer for entire HPC defined here */
static struct php_ctlr_state_s *php_ctlr_list_head;	/* HPC state linked list */
static int ctlr_seq_num = 0;	/* Controller sequenc # */
static spinlock_t list_lock;

static irqreturn_t shpc_isr(int IRQ, void *dev_id, struct pt_regs *regs);

static void start_int_poll_timer(struct php_ctlr_state_s *php_ctlr, int seconds);
static int hpc_check_cmd_status(struct controller *ctrl);

static inline u8 shpc_readb(struct controller *ctrl, int reg)
{
	return readb(ctrl->hpc_ctlr_handle->creg + reg);
}

static inline void shpc_writeb(struct controller *ctrl, int reg, u8 val)
{
	writeb(val, ctrl->hpc_ctlr_handle->creg + reg);
}

static inline u16 shpc_readw(struct controller *ctrl, int reg)
{
	return readw(ctrl->hpc_ctlr_handle->creg + reg);
}

static inline void shpc_writew(struct controller *ctrl, int reg, u16 val)
{
	writew(val, ctrl->hpc_ctlr_handle->creg + reg);
}

static inline u32 shpc_readl(struct controller *ctrl, int reg)
{
	return readl(ctrl->hpc_ctlr_handle->creg + reg);
}

static inline void shpc_writel(struct controller *ctrl, int reg, u32 val)
{
	writel(val, ctrl->hpc_ctlr_handle->creg + reg);
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

/* This is the interrupt polling timeout function. */
static void int_poll_timeout(unsigned long lphp_ctlr)
{
    struct php_ctlr_state_s *php_ctlr = (struct php_ctlr_state_s *)lphp_ctlr;

    DBG_ENTER_ROUTINE

    if ( !php_ctlr ) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return;
    }

    /* Poll for interrupt events.  regs == NULL => polling */
    shpc_isr( 0, (void *)php_ctlr, NULL );

    init_timer(&php_ctlr->int_poll_timer);
	if (!shpchp_poll_time)
		shpchp_poll_time = 2; /* reset timer to poll in 2 secs if user doesn't specify at module installation*/

    start_int_poll_timer(php_ctlr, shpchp_poll_time);  
	
	return;
}

/* This function starts the interrupt polling timer. */
static void start_int_poll_timer(struct php_ctlr_state_s *php_ctlr, int seconds)
{
    if (!php_ctlr) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return;
	}

    if ( ( seconds <= 0 ) || ( seconds > 60 ) )
        seconds = 2;            /* Clamp to sane value */

    php_ctlr->int_poll_timer.function = &int_poll_timeout;
    php_ctlr->int_poll_timer.data = (unsigned long)php_ctlr;    /* Instance data */
    php_ctlr->int_poll_timer.expires = jiffies + seconds * HZ;
    add_timer(&php_ctlr->int_poll_timer);

	return;
}

static inline int shpc_wait_cmd(struct controller *ctrl)
{
	int retval = 0;
	unsigned int timeout_msec = shpchp_poll_mode ? 2000 : 1000;
	unsigned long timeout = msecs_to_jiffies(timeout_msec);
	int rc = wait_event_interruptible_timeout(ctrl->queue,
						  !ctrl->cmd_busy, timeout);
	if (!rc) {
		retval = -EIO;
		err("Command not completed in %d msec\n", timeout_msec);
	} else if (rc < 0) {
		retval = -EINTR;
		info("Command was interrupted by a signal\n");
	}
	ctrl->cmd_busy = 0;

	return retval;
}

static int shpc_write_cmd(struct slot *slot, u8 t_slot, u8 cmd)
{
	struct php_ctlr_state_s *php_ctlr = slot->ctrl->hpc_ctlr_handle;
	struct controller *ctrl = slot->ctrl;
	u16 cmd_status;
	int retval = 0;
	u16 temp_word;
	int i;

	DBG_ENTER_ROUTINE 

	mutex_lock(&slot->ctrl->cmd_lock);

	if (!php_ctlr) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		retval = -EINVAL;
		goto out;
	}

	for (i = 0; i < 10; i++) {
		cmd_status = shpc_readw(ctrl, CMD_STATUS);
		
		if (!(cmd_status & 0x1))
			break;
		/*  Check every 0.1 sec for a total of 1 sec*/
		msleep(100);
	}

	cmd_status = shpc_readw(ctrl, CMD_STATUS);
	
	if (cmd_status & 0x1) { 
		/* After 1 sec and and the controller is still busy */
		err("%s : Controller is still busy after 1 sec.\n", __FUNCTION__);
		retval = -EBUSY;
		goto out;
	}

	++t_slot;
	temp_word =  (t_slot << 8) | (cmd & 0xFF);
	dbg("%s: t_slot %x cmd %x\n", __FUNCTION__, t_slot, cmd);
	
	/* To make sure the Controller Busy bit is 0 before we send out the
	 * command. 
	 */
	slot->ctrl->cmd_busy = 1;
	shpc_writew(ctrl, CMD, temp_word);

	/*
	 * Wait for command completion.
	 */
	retval = shpc_wait_cmd(slot->ctrl);
	if (retval)
		goto out;

	cmd_status = hpc_check_cmd_status(slot->ctrl);
	if (cmd_status) {
		err("%s: Failed to issued command 0x%x (error code = %d)\n",
		    __FUNCTION__, cmd, cmd_status);
		retval = -EIO;
	}
 out:
	mutex_unlock(&slot->ctrl->cmd_lock);

	DBG_LEAVE_ROUTINE 
	return retval;
}

static int hpc_check_cmd_status(struct controller *ctrl)
{
	u16 cmd_status;
	int retval = 0;

	DBG_ENTER_ROUTINE 
	
	if (!ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return -1;
	}

	cmd_status = shpc_readw(ctrl, CMD_STATUS) & 0x000F;
	
	switch (cmd_status >> 1) {
	case 0:
		retval = 0;
		break;
	case 1:
		retval = SWITCH_OPEN;
		err("%s: Switch opened!\n", __FUNCTION__);
		break;
	case 2:
		retval = INVALID_CMD;
		err("%s: Invalid HPC command!\n", __FUNCTION__);
		break;
	case 4:
		retval = INVALID_SPEED_MODE;
		err("%s: Invalid bus speed/mode!\n", __FUNCTION__);
		break;
	default:
		retval = cmd_status;
	}

	DBG_LEAVE_ROUTINE 
	return retval;
}


static int hpc_get_attention_status(struct slot *slot, u8 *status)
{
	struct controller *ctrl = slot->ctrl;
	u32 slot_reg;
	u8 state;
	
	DBG_ENTER_ROUTINE 

	if (!slot->ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return -1;
	}

	slot_reg = shpc_readl(ctrl, SLOT_REG(slot->hp_slot));
	state = (slot_reg & ATN_LED_STATE_MASK) >> ATN_LED_STATE_SHIFT;

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

	DBG_LEAVE_ROUTINE 
	return 0;
}

static int hpc_get_power_status(struct slot * slot, u8 *status)
{
	struct controller *ctrl = slot->ctrl;
	u32 slot_reg;
	u8 state;
	
	DBG_ENTER_ROUTINE 

	if (!slot->ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return -1;
	}

	slot_reg = shpc_readl(ctrl, SLOT_REG(slot->hp_slot));
	state = (slot_reg & SLOT_STATE_MASK) >> SLOT_STATE_SHIFT;

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

	DBG_LEAVE_ROUTINE 
	return 0;
}


static int hpc_get_latch_status(struct slot *slot, u8 *status)
{
	struct controller *ctrl = slot->ctrl;
	u32 slot_reg;

	DBG_ENTER_ROUTINE 

	if (!slot->ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return -1;
	}

	slot_reg = shpc_readl(ctrl, SLOT_REG(slot->hp_slot));
	*status = !!(slot_reg & MRL_SENSOR);	/* 0 -> close; 1 -> open */

	DBG_LEAVE_ROUTINE 
	return 0;
}

static int hpc_get_adapter_status(struct slot *slot, u8 *status)
{
	struct controller *ctrl = slot->ctrl;
	u32 slot_reg;
	u8 state;

	DBG_ENTER_ROUTINE 

	if (!slot->ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return -1;
	}

	slot_reg = shpc_readl(ctrl, SLOT_REG(slot->hp_slot));
	state = (slot_reg & PRSNT_MASK) >> PRSNT_SHIFT;
	*status = (state != 0x3) ? 1 : 0;

	DBG_LEAVE_ROUTINE 
	return 0;
}

static int hpc_get_prog_int(struct slot *slot, u8 *prog_int)
{
	struct controller *ctrl = slot->ctrl;

	DBG_ENTER_ROUTINE 
	
	if (!slot->ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return -1;
	}

	*prog_int = shpc_readb(ctrl, PROG_INTERFACE);

	DBG_LEAVE_ROUTINE 
	return 0;
}

static int hpc_get_adapter_speed(struct slot *slot, enum pci_bus_speed *value)
{
	int retval = 0;
	struct controller *ctrl = slot->ctrl;
	u32 slot_reg = shpc_readl(ctrl, SLOT_REG(slot->hp_slot));
	u8 pcix_cap = (slot_reg & PCIX_CAP_MASK_PI2) >> PCIX_CAP_SHIFT;
	u8 m66_cap  = !!(slot_reg & MHZ66_CAP);

	DBG_ENTER_ROUTINE 

	dbg("%s: slot_reg = %x, pcix_cap = %x, m66_cap = %x\n",
	    __FUNCTION__, slot_reg, pcix_cap, m66_cap);

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

	dbg("Adapter speed = %d\n", *value);
	DBG_LEAVE_ROUTINE 
	return retval;
}

static int hpc_get_mode1_ECC_cap(struct slot *slot, u8 *mode)
{
	struct controller *ctrl = slot->ctrl;
	u16 sec_bus_status;
	u8 pi;
	int retval = 0;

	DBG_ENTER_ROUTINE 

	if (!slot->ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return -1;
	}

	pi = shpc_readb(ctrl, PROG_INTERFACE);
	sec_bus_status = shpc_readw(ctrl, SEC_BUS_CONFIG);

	if (pi == 2) {
		*mode = (sec_bus_status & 0x0100) >> 8;
	} else {
		retval = -1;
	}

	dbg("Mode 1 ECC cap = %d\n", *mode);
	
	DBG_LEAVE_ROUTINE 
	return retval;
}

static int hpc_query_power_fault(struct slot * slot)
{
	struct controller *ctrl = slot->ctrl;
	u32 slot_reg;

	DBG_ENTER_ROUTINE 

	if (!slot->ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return -1;
	}

	slot_reg = shpc_readl(ctrl, SLOT_REG(slot->hp_slot));

	DBG_LEAVE_ROUTINE
	/* Note: Logic 0 => fault */
	return !(slot_reg & POWER_FAULT);
}

static int hpc_set_attention_status(struct slot *slot, u8 value)
{
	struct php_ctlr_state_s *php_ctlr = slot->ctrl->hpc_ctlr_handle;
	u8 slot_cmd = 0;
	int rc = 0;

	if (!slot->ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return -1;
	}

	if (slot->hp_slot >= php_ctlr->num_slots) {
		err("%s: Invalid HPC slot number!\n", __FUNCTION__);
		return -1;
	}

	switch (value) {
		case 0 :	
			slot_cmd = 0x30;	/* OFF */
			break;
		case 1:
			slot_cmd = 0x10;	/* ON */
			break;
		case 2:
			slot_cmd = 0x20;	/* BLINK */
			break;
		default:
			return -1;
	}

	shpc_write_cmd(slot, slot->hp_slot, slot_cmd);
	
	return rc;
}


static void hpc_set_green_led_on(struct slot *slot)
{
	struct php_ctlr_state_s *php_ctlr = slot->ctrl->hpc_ctlr_handle;
	u8 slot_cmd;

	if (!slot->ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return ;
	}

	if (slot->hp_slot >= php_ctlr->num_slots) {
		err("%s: Invalid HPC slot number!\n", __FUNCTION__);
		return ;
	}

	slot_cmd = 0x04;

	shpc_write_cmd(slot, slot->hp_slot, slot_cmd);

	return;
}

static void hpc_set_green_led_off(struct slot *slot)
{
	struct php_ctlr_state_s *php_ctlr = slot->ctrl->hpc_ctlr_handle;
	u8 slot_cmd;

	if (!slot->ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return ;
	}

	if (slot->hp_slot >= php_ctlr->num_slots) {
		err("%s: Invalid HPC slot number!\n", __FUNCTION__);
		return ;
	}

	slot_cmd = 0x0C;

	shpc_write_cmd(slot, slot->hp_slot, slot_cmd);

	return;
}

static void hpc_set_green_led_blink(struct slot *slot)
{
	struct php_ctlr_state_s *php_ctlr = slot->ctrl->hpc_ctlr_handle;
	u8 slot_cmd;

	if (!slot->ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return ;
	}

	if (slot->hp_slot >= php_ctlr->num_slots) {
		err("%s: Invalid HPC slot number!\n", __FUNCTION__);
		return ;
	}

	slot_cmd = 0x08;

	shpc_write_cmd(slot, slot->hp_slot, slot_cmd);

	return;
}

int shpc_get_ctlr_slot_config(struct controller *ctrl,
	int *num_ctlr_slots,	/* number of slots in this HPC			*/
	int *first_device_num,	/* PCI dev num of the first slot in this SHPC	*/
	int *physical_slot_num,	/* phy slot num of the first slot in this SHPC	*/
	int *updown,		/* physical_slot_num increament: 1 or -1	*/
	int *flags)
{
	u32 slot_config;

	DBG_ENTER_ROUTINE 

	if (!ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return -1;
	}

	slot_config = shpc_readl(ctrl, SLOT_CONFIG);
	*first_device_num = (slot_config & FIRST_DEV_NUM) >> 8;
	*num_ctlr_slots = slot_config & SLOT_NUM;
	*physical_slot_num = (slot_config & PSN) >> 16;
	*updown = ((slot_config & UPDOWN) >> 29) ? 1 : -1;

	dbg("%s: physical_slot_num = %x\n", __FUNCTION__, *physical_slot_num);

	DBG_LEAVE_ROUTINE 
	return 0;
}

static void hpc_release_ctlr(struct controller *ctrl)
{
	struct php_ctlr_state_s *php_ctlr = ctrl->hpc_ctlr_handle;
	struct php_ctlr_state_s *p, *p_prev;
	int i;

	DBG_ENTER_ROUTINE 

	if (!ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return ;
	}

	/*
	 * Mask all slot event interrupts
	 */
	for (i = 0; i < ctrl->num_slots; i++)
		shpc_writel(ctrl, SLOT_REG(i), 0xffff3fff);

	cleanup_slots(ctrl);

	if (shpchp_poll_mode) {
	    del_timer(&php_ctlr->int_poll_timer);
	} else {	
		if (php_ctlr->irq) {
			free_irq(php_ctlr->irq, ctrl);
			php_ctlr->irq = 0;
			pci_disable_msi(php_ctlr->pci_dev);
		}
	}

	if (php_ctlr->pci_dev) {
		iounmap(php_ctlr->creg);
		release_mem_region(ctrl->mmio_base, ctrl->mmio_size);
		php_ctlr->pci_dev = NULL;
	}

	spin_lock(&list_lock);
	p = php_ctlr_list_head;
	p_prev = NULL;
	while (p) {
		if (p == php_ctlr) {
			if (p_prev)
				p_prev->pnext = p->pnext;
			else
				php_ctlr_list_head = p->pnext;
			break;
		} else {
			p_prev = p;
			p = p->pnext;
		}
	}
	spin_unlock(&list_lock);

	kfree(php_ctlr);

DBG_LEAVE_ROUTINE
			  
}

static int hpc_power_on_slot(struct slot * slot)
{
	struct php_ctlr_state_s *php_ctlr = slot->ctrl->hpc_ctlr_handle;
	u8 slot_cmd;
	int retval = 0;

	DBG_ENTER_ROUTINE 

	if (!slot->ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return -1;
	}

	if (slot->hp_slot >= php_ctlr->num_slots) {
		err("%s: Invalid HPC slot number!\n", __FUNCTION__);
		return -1;
	}
	slot_cmd = 0x01;

	retval = shpc_write_cmd(slot, slot->hp_slot, slot_cmd);

	if (retval) {
		err("%s: Write command failed!\n", __FUNCTION__);
		return -1;
	}

	DBG_LEAVE_ROUTINE

	return retval;
}

static int hpc_slot_enable(struct slot * slot)
{
	struct php_ctlr_state_s *php_ctlr = slot->ctrl->hpc_ctlr_handle;
	u8 slot_cmd;
	int retval = 0;

	DBG_ENTER_ROUTINE 

	if (!slot->ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return -1;
	}

	if (slot->hp_slot >= php_ctlr->num_slots) {
		err("%s: Invalid HPC slot number!\n", __FUNCTION__);
		return -1;
	}
	/* 3A => Slot - Enable, Power Indicator - Blink, Attention Indicator - Off */
	slot_cmd = 0x3A;  

	retval = shpc_write_cmd(slot, slot->hp_slot, slot_cmd);

	if (retval) {
		err("%s: Write command failed!\n", __FUNCTION__);
		return -1;
	}

	DBG_LEAVE_ROUTINE
	return retval;
}

static int hpc_slot_disable(struct slot * slot)
{
	struct php_ctlr_state_s *php_ctlr = slot->ctrl->hpc_ctlr_handle;
	u8 slot_cmd;
	int retval = 0;

	DBG_ENTER_ROUTINE 

	if (!slot->ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return -1;
	}

	if (slot->hp_slot >= php_ctlr->num_slots) {
		err("%s: Invalid HPC slot number!\n", __FUNCTION__);
		return -1;
	}

	/* 1F => Slot - Disable, Power Indicator - Off, Attention Indicator - On */
	slot_cmd = 0x1F;

	retval = shpc_write_cmd(slot, slot->hp_slot, slot_cmd);

	if (retval) {
		err("%s: Write command failed!\n", __FUNCTION__);
		return -1;
	}

	DBG_LEAVE_ROUTINE
	return retval;
}

static int hpc_set_bus_speed_mode(struct slot * slot, enum pci_bus_speed value)
{
	int retval;
	struct controller *ctrl = slot->ctrl;
	u8 pi, cmd;

	DBG_ENTER_ROUTINE 

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
		err("%s: Write command failed!\n", __FUNCTION__);

	DBG_LEAVE_ROUTINE
	return retval;
}

static irqreturn_t shpc_isr(int IRQ, void *dev_id, struct pt_regs *regs)
{
	struct controller *ctrl = NULL;
	struct php_ctlr_state_s *php_ctlr;
	u8 schedule_flag = 0;
	u32 temp_dword, intr_loc, intr_loc2;
	int hp_slot;

	if (!dev_id)
		return IRQ_NONE;

	if (!shpchp_poll_mode) { 
		ctrl = (struct controller *)dev_id;
		php_ctlr = ctrl->hpc_ctlr_handle;
	} else { 
		php_ctlr = (struct php_ctlr_state_s *) dev_id;
		ctrl = (struct controller *)php_ctlr->callback_instance_id;
	}

	if (!ctrl)
		return IRQ_NONE;
	
	if (!php_ctlr || !php_ctlr->creg)
		return IRQ_NONE;

	/* Check to see if it was our interrupt */
	intr_loc = shpc_readl(ctrl, INTR_LOC);

	if (!intr_loc)
		return IRQ_NONE;
	dbg("%s: intr_loc = %x\n",__FUNCTION__, intr_loc); 

	if(!shpchp_poll_mode) {
		/* Mask Global Interrupt Mask - see implementation note on p. 139 */
		/* of SHPC spec rev 1.0*/
		temp_dword = shpc_readl(ctrl, SERR_INTR_ENABLE);
		temp_dword |= 0x00000001;
		shpc_writel(ctrl, SERR_INTR_ENABLE, temp_dword);

		intr_loc2 = shpc_readl(ctrl, INTR_LOC);
		dbg("%s: intr_loc2 = %x\n",__FUNCTION__, intr_loc2); 
	}

	if (intr_loc & 0x0001) {
		/* 
		 * Command Complete Interrupt Pending 
		 * RO only - clear by writing 1 to the Command Completion
		 * Detect bit in Controller SERR-INT register
		 */
		temp_dword = shpc_readl(ctrl, SERR_INTR_ENABLE);
		temp_dword &= 0xfffdffff;
		shpc_writel(ctrl, SERR_INTR_ENABLE, temp_dword);
		ctrl->cmd_busy = 0;
		wake_up_interruptible(&ctrl->queue);
	}

	if ((intr_loc = (intr_loc >> 1)) == 0)
		goto out;

	for (hp_slot = 0; hp_slot < ctrl->num_slots; hp_slot++) { 
	/* To find out which slot has interrupt pending */
		if ((intr_loc >> hp_slot) & 0x01) {
			temp_dword = shpc_readl(ctrl, SLOT_REG(hp_slot));
			dbg("%s: Slot %x with intr, slot register = %x\n",
				__FUNCTION__, hp_slot, temp_dword);
			if ((php_ctlr->switch_change_callback) &&
			    (temp_dword & MRL_CHANGE_DETECTED))
				schedule_flag += php_ctlr->switch_change_callback(
					hp_slot, php_ctlr->callback_instance_id);
			if ((php_ctlr->attention_button_callback) &&
			    (temp_dword & BUTTON_PRESS_DETECTED))
				schedule_flag += php_ctlr->attention_button_callback(
					hp_slot, php_ctlr->callback_instance_id);
			if ((php_ctlr->presence_change_callback) &&
			    (temp_dword & PRSNT_CHANGE_DETECTED))
				schedule_flag += php_ctlr->presence_change_callback(
					hp_slot , php_ctlr->callback_instance_id);
			if ((php_ctlr->power_fault_callback) &&
			    (temp_dword & (ISO_PFAULT_DETECTED | CON_PFAULT_DETECTED)))
				schedule_flag += php_ctlr->power_fault_callback(
					hp_slot, php_ctlr->callback_instance_id);
			
			/* Clear all slot events */
			temp_dword = 0xe01f3fff;
			shpc_writel(ctrl, SLOT_REG(hp_slot), temp_dword);

			intr_loc2 = shpc_readl(ctrl, INTR_LOC);
			dbg("%s: intr_loc2 = %x\n",__FUNCTION__, intr_loc2); 
		}
	}
 out:
	if (!shpchp_poll_mode) {
		/* Unmask Global Interrupt Mask */
		temp_dword = shpc_readl(ctrl, SERR_INTR_ENABLE);
		temp_dword &= 0xfffffffe;
		shpc_writel(ctrl, SERR_INTR_ENABLE, temp_dword);
	}
	
	return IRQ_HANDLED;
}

static int hpc_get_max_bus_speed (struct slot *slot, enum pci_bus_speed *value)
{
	int retval = 0;
	struct controller *ctrl = slot->ctrl;
	enum pci_bus_speed bus_speed = PCI_SPEED_UNKNOWN;
	u8 pi = shpc_readb(ctrl, PROG_INTERFACE);
	u32 slot_avail1 = shpc_readl(ctrl, SLOT_AVAIL1);
	u32 slot_avail2 = shpc_readl(ctrl, SLOT_AVAIL2);

	DBG_ENTER_ROUTINE 

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

	*value = bus_speed;
	dbg("Max bus speed = %d\n", bus_speed);
	DBG_LEAVE_ROUTINE 
	return retval;
}

static int hpc_get_cur_bus_speed (struct slot *slot, enum pci_bus_speed *value)
{
	int retval = 0;
	struct controller *ctrl = slot->ctrl;
	enum pci_bus_speed bus_speed = PCI_SPEED_UNKNOWN;
	u16 sec_bus_reg = shpc_readw(ctrl, SEC_BUS_CONFIG);
	u8 pi = shpc_readb(ctrl, PROG_INTERFACE);
	u8 speed_mode = (pi == 2) ? (sec_bus_reg & 0xF) : (sec_bus_reg & 0x7);

	DBG_ENTER_ROUTINE 

	if ((pi == 1) && (speed_mode > 4)) {
		*value = PCI_SPEED_UNKNOWN;
		return -ENODEV;
	}

	switch (speed_mode) {
	case 0x0:
		*value = PCI_SPEED_33MHz;
		break;
	case 0x1:
		*value = PCI_SPEED_66MHz;
		break;
	case 0x2:
		*value = PCI_SPEED_66MHz_PCIX;
		break;
	case 0x3:
		*value = PCI_SPEED_100MHz_PCIX;
		break;
	case 0x4:
		*value = PCI_SPEED_133MHz_PCIX;
		break;
	case 0x5:
		*value = PCI_SPEED_66MHz_PCIX_ECC;
		break;
	case 0x6:
		*value = PCI_SPEED_100MHz_PCIX_ECC;
		break;
	case 0x7:
		*value = PCI_SPEED_133MHz_PCIX_ECC;
		break;
	case 0x8:
		*value = PCI_SPEED_66MHz_PCIX_266;
		break;
	case 0x9:
		*value = PCI_SPEED_100MHz_PCIX_266;
		break;
	case 0xa:
		*value = PCI_SPEED_133MHz_PCIX_266;
		break;
	case 0xb:
		*value = PCI_SPEED_66MHz_PCIX_533;
		break;
	case 0xc:
		*value = PCI_SPEED_100MHz_PCIX_533;
		break;
	case 0xd:
		*value = PCI_SPEED_133MHz_PCIX_533;
		break;
	default:
		*value = PCI_SPEED_UNKNOWN;
		retval = -ENODEV;
		break;
	}

	dbg("Current bus speed = %d\n", bus_speed);
	DBG_LEAVE_ROUTINE 
	return retval;
}

static struct hpc_ops shpchp_hpc_ops = {
	.power_on_slot			= hpc_power_on_slot,
	.slot_enable			= hpc_slot_enable,
	.slot_disable			= hpc_slot_disable,
	.set_bus_speed_mode		= hpc_set_bus_speed_mode,	  
	.set_attention_status	= hpc_set_attention_status,
	.get_power_status		= hpc_get_power_status,
	.get_attention_status	= hpc_get_attention_status,
	.get_latch_status		= hpc_get_latch_status,
	.get_adapter_status		= hpc_get_adapter_status,

	.get_max_bus_speed		= hpc_get_max_bus_speed,
	.get_cur_bus_speed		= hpc_get_cur_bus_speed,
	.get_adapter_speed		= hpc_get_adapter_speed,
	.get_mode1_ECC_cap		= hpc_get_mode1_ECC_cap,
	.get_prog_int			= hpc_get_prog_int,

	.query_power_fault		= hpc_query_power_fault,
	.green_led_on			= hpc_set_green_led_on,
	.green_led_off			= hpc_set_green_led_off,
	.green_led_blink		= hpc_set_green_led_blink,
	
	.release_ctlr			= hpc_release_ctlr,
};

int shpc_init(struct controller * ctrl, struct pci_dev * pdev)
{
	struct php_ctlr_state_s *php_ctlr, *p;
	void *instance_id = ctrl;
	int rc, num_slots = 0;
	u8 hp_slot;
	static int first = 1;
	u32 shpc_base_offset;
	u32 tempdword, slot_reg, slot_config;
	u8 i;

	DBG_ENTER_ROUTINE

	ctrl->pci_dev = pdev;  /* pci_dev of the P2P bridge */

	spin_lock_init(&list_lock);
	php_ctlr = kzalloc(sizeof(*php_ctlr), GFP_KERNEL);

	if (!php_ctlr) {	/* allocate controller state data */
		err("%s: HPC controller memory allocation error!\n", __FUNCTION__);
		goto abort;
	}

	php_ctlr->pci_dev = pdev;	/* save pci_dev in context */

	if ((pdev->vendor == PCI_VENDOR_ID_AMD) || (pdev->device ==
				PCI_DEVICE_ID_AMD_GOLAM_7450)) {
		/* amd shpc driver doesn't use Base Offset; assume 0 */
		ctrl->mmio_base = pci_resource_start(pdev, 0);
		ctrl->mmio_size = pci_resource_len(pdev, 0);
	} else {
		ctrl->cap_offset = pci_find_capability(pdev, PCI_CAP_ID_SHPC);
		if (!ctrl->cap_offset) {
			err("%s : cap_offset == 0\n", __FUNCTION__);
			goto abort_free_ctlr;
		}
		dbg("%s: cap_offset = %x\n", __FUNCTION__, ctrl->cap_offset);

		rc = shpc_indirect_read(ctrl, 0, &shpc_base_offset);
		if (rc) {
			err("%s: cannot read base_offset\n", __FUNCTION__);
			goto abort_free_ctlr;
		}

		rc = shpc_indirect_read(ctrl, 3, &tempdword);
		if (rc) {
			err("%s: cannot read slot config\n", __FUNCTION__);
			goto abort_free_ctlr;
		}
		num_slots = tempdword & SLOT_NUM;
		dbg("%s: num_slots (indirect) %x\n", __FUNCTION__, num_slots);

		for (i = 0; i < 9 + num_slots; i++) {
			rc = shpc_indirect_read(ctrl, i, &tempdword);
			if (rc) {
				err("%s: cannot read creg (index = %d)\n",
				    __FUNCTION__, i);
				goto abort_free_ctlr;
			}
			dbg("%s: offset %d: value %x\n", __FUNCTION__,i,
					tempdword);
		}

		ctrl->mmio_base =
			pci_resource_start(pdev, 0) + shpc_base_offset;
		ctrl->mmio_size = 0x24 + 0x4 * num_slots;
	}

	if (first) {
		spin_lock_init(&hpc_event_lock);
		first = 0;
	}

	info("HPC vendor_id %x device_id %x ss_vid %x ss_did %x\n", pdev->vendor, pdev->device, pdev->subsystem_vendor, 
		pdev->subsystem_device);
	
	if (pci_enable_device(pdev))
		goto abort_free_ctlr;

	if (!request_mem_region(ctrl->mmio_base, ctrl->mmio_size, MY_NAME)) {
		err("%s: cannot reserve MMIO region\n", __FUNCTION__);
		goto abort_free_ctlr;
	}

	php_ctlr->creg = ioremap(ctrl->mmio_base, ctrl->mmio_size);
	if (!php_ctlr->creg) {
		err("%s: cannot remap MMIO region %lx @ %lx\n", __FUNCTION__,
		    ctrl->mmio_size, ctrl->mmio_base);
		release_mem_region(ctrl->mmio_base, ctrl->mmio_size);
		goto abort_free_ctlr;
	}
	dbg("%s: php_ctlr->creg %p\n", __FUNCTION__, php_ctlr->creg);

	mutex_init(&ctrl->crit_sect);
	mutex_init(&ctrl->cmd_lock);

	/* Setup wait queue */
	init_waitqueue_head(&ctrl->queue);

	/* Find the IRQ */
	php_ctlr->irq = pdev->irq;
	php_ctlr->attention_button_callback = shpchp_handle_attention_button,
	php_ctlr->switch_change_callback = shpchp_handle_switch_change;
	php_ctlr->presence_change_callback = shpchp_handle_presence_change;
	php_ctlr->power_fault_callback = shpchp_handle_power_fault;
	php_ctlr->callback_instance_id = instance_id;

	ctrl->hpc_ctlr_handle = php_ctlr;
	ctrl->hpc_ops = &shpchp_hpc_ops;

	/* Return PCI Controller Info */
	slot_config = shpc_readl(ctrl, SLOT_CONFIG);
	php_ctlr->slot_device_offset = (slot_config & FIRST_DEV_NUM) >> 8;
	php_ctlr->num_slots = slot_config & SLOT_NUM;
	dbg("%s: slot_device_offset %x\n", __FUNCTION__, php_ctlr->slot_device_offset);
	dbg("%s: num_slots %x\n", __FUNCTION__, php_ctlr->num_slots);

	/* Mask Global Interrupt Mask & Command Complete Interrupt Mask */
	tempdword = shpc_readl(ctrl, SERR_INTR_ENABLE);
	dbg("%s: SERR_INTR_ENABLE = %x\n", __FUNCTION__, tempdword);
	tempdword = 0x0003000f;   
	shpc_writel(ctrl, SERR_INTR_ENABLE, tempdword);
	tempdword = shpc_readl(ctrl, SERR_INTR_ENABLE);
	dbg("%s: SERR_INTR_ENABLE = %x\n", __FUNCTION__, tempdword);

	/* Mask the MRL sensor SERR Mask of individual slot in
	 * Slot SERR-INT Mask & clear all the existing event if any
	 */
	for (hp_slot = 0; hp_slot < php_ctlr->num_slots; hp_slot++) {
		slot_reg = shpc_readl(ctrl, SLOT_REG(hp_slot));
		dbg("%s: Default Logical Slot Register %d value %x\n", __FUNCTION__,
			hp_slot, slot_reg);
		tempdword = 0xffff3fff;  
		shpc_writel(ctrl, SLOT_REG(hp_slot), tempdword);
	}
	
	if (shpchp_poll_mode)  {/* Install interrupt polling code */
		/* Install and start the interrupt polling timer */
		init_timer(&php_ctlr->int_poll_timer);
		start_int_poll_timer( php_ctlr, 10 );   /* start with 10 second delay */
	} else {
		/* Installs the interrupt handler */
		rc = pci_enable_msi(pdev);
		if (rc) {
			info("Can't get msi for the hotplug controller\n");
			info("Use INTx for the hotplug controller\n");
		} else
			php_ctlr->irq = pdev->irq;
		
		rc = request_irq(php_ctlr->irq, shpc_isr, SA_SHIRQ, MY_NAME, (void *) ctrl);
		dbg("%s: request_irq %d for hpc%d (returns %d)\n", __FUNCTION__, php_ctlr->irq, ctlr_seq_num, rc);
		if (rc) {
			err("Can't get irq %d for the hotplug controller\n", php_ctlr->irq);
			goto abort_free_ctlr;
		}
	}
	dbg("%s: HPC at b:d:f:irq=0x%x:%x:%x:%x\n", __FUNCTION__,
			pdev->bus->number, PCI_SLOT(pdev->devfn),
			PCI_FUNC(pdev->devfn), pdev->irq);
	get_hp_hw_control_from_firmware(pdev);

	/*  Add this HPC instance into the HPC list */
	spin_lock(&list_lock);
	if (php_ctlr_list_head == 0) {
		php_ctlr_list_head = php_ctlr;
		p = php_ctlr_list_head;
		p->pnext = NULL;
	} else {
		p = php_ctlr_list_head;

		while (p->pnext)
			p = p->pnext;

		p->pnext = php_ctlr;
	}
	spin_unlock(&list_lock);

	ctlr_seq_num++;

	for (hp_slot = 0; hp_slot < php_ctlr->num_slots; hp_slot++) {
		slot_reg = shpc_readl(ctrl, SLOT_REG(hp_slot));
		dbg("%s: Default Logical Slot Register %d value %x\n", __FUNCTION__,
			hp_slot, slot_reg);
		tempdword = 0xe01f3fff;  
		shpc_writel(ctrl, SLOT_REG(hp_slot), tempdword);
	}
	if (!shpchp_poll_mode) {
		/* Unmask all general input interrupts and SERR */
		tempdword = shpc_readl(ctrl, SERR_INTR_ENABLE);
		tempdword = 0x0000000a;
		shpc_writel(ctrl, SERR_INTR_ENABLE, tempdword);
		tempdword = shpc_readl(ctrl, SERR_INTR_ENABLE);
		dbg("%s: SERR_INTR_ENABLE = %x\n", __FUNCTION__, tempdword);
	}

	DBG_LEAVE_ROUTINE
	return 0;

	/* We end up here for the many possible ways to fail this API.  */
abort_free_ctlr:
	kfree(php_ctlr);
abort:
	DBG_LEAVE_ROUTINE
	return -1;
}
