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


/* Secondary Bus Configuration Register */
/* For PI = 1, Bits 0 to 2 have been encoded as follows to show current bus speed/mode */
#define PCI_33MHZ		0x0
#define PCI_66MHZ		0x1
#define PCIX_66MHZ		0x2
#define PCIX_100MHZ		0x3
#define PCIX_133MHZ		0x4

/* For PI = 2, Bits 0 to 3 have been encoded as follows to show current bus speed/mode */
#define PCI_33MHZ		0x0
#define PCI_66MHZ		0x1
#define PCIX_66MHZ		0x2
#define PCIX_100MHZ		0x3
#define PCIX_133MHZ		0x4
#define PCIX_66MHZ_ECC		0x5
#define PCIX_100MHZ_ECC		0x6
#define PCIX_133MHZ_ECC		0x7
#define PCIX_66MHZ_266		0x9
#define PCIX_100MHZ_266		0xa
#define PCIX_133MHZ_266		0xb
#define PCIX_66MHZ_533		0x11
#define PCIX_100MHZ_533		0x12
#define PCIX_133MHZ_533		0x13

/* Slot Configuration */
#define SLOT_NUM		0x0000001F
#define	FIRST_DEV_NUM		0x00001F00
#define PSN			0x07FF0000
#define	UPDOWN			0x20000000
#define	MRLSENSOR		0x40000000
#define ATTN_BUTTON		0x80000000

/* Slot Status Field Definitions */
/* Slot State */
#define PWR_ONLY		0x0001
#define ENABLED			0x0002
#define DISABLED		0x0003

/* Power Indicator State */
#define PWR_LED_ON		0x0004
#define PWR_LED_BLINK		0x0008
#define PWR_LED_OFF		0x000c

/* Attention Indicator State */
#define ATTEN_LED_ON		0x0010
#define	ATTEN_LED_BLINK		0x0020
#define ATTEN_LED_OFF		0x0030

/* Power Fault */
#define pwr_fault		0x0040

/* Attention Button */
#define ATTEN_BUTTON		0x0080

/* MRL Sensor */
#define MRL_SENSOR		0x0100

/* 66 MHz Capable */
#define IS_66MHZ_CAP		0x0200

/* PRSNT1#/PRSNT2# */
#define SLOT_EMP		0x0c00

/* PCI-X Capability */
#define NON_PCIX		0x0000
#define PCIX_66			0x1000
#define PCIX_133		0x3000
#define PCIX_266		0x4000  /* For PI = 2 only */
#define PCIX_533		0x5000	/* For PI = 2 only */

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

static int shpc_write_cmd(struct slot *slot, u8 t_slot, u8 cmd)
{
	struct php_ctlr_state_s *php_ctlr = slot->ctrl->hpc_ctlr_handle;
	u16 cmd_status;
	int retval = 0;
	u16 temp_word;
	int i;

	DBG_ENTER_ROUTINE 
	
	if (!php_ctlr) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return -1;
	}

	for (i = 0; i < 10; i++) {
		cmd_status = readw(php_ctlr->creg + CMD_STATUS);
		
		if (!(cmd_status & 0x1))
			break;
		/*  Check every 0.1 sec for a total of 1 sec*/
		msleep(100);
	}

	cmd_status = readw(php_ctlr->creg + CMD_STATUS);
	
	if (cmd_status & 0x1) { 
		/* After 1 sec and and the controller is still busy */
		err("%s : Controller is still busy after 1 sec.\n", __FUNCTION__);
		return -1;
	}

	++t_slot;
	temp_word =  (t_slot << 8) | (cmd & 0xFF);
	dbg("%s: t_slot %x cmd %x\n", __FUNCTION__, t_slot, cmd);
	
	/* To make sure the Controller Busy bit is 0 before we send out the
	 * command. 
	 */
	writew(temp_word, php_ctlr->creg + CMD);

	DBG_LEAVE_ROUTINE 
	return retval;
}

static int hpc_check_cmd_status(struct controller *ctrl)
{
	struct php_ctlr_state_s *php_ctlr = ctrl->hpc_ctlr_handle;
	u16 cmd_status;
	int retval = 0;

	DBG_ENTER_ROUTINE 
	
	if (!ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return -1;
	}

	cmd_status = readw(php_ctlr->creg + CMD_STATUS) & 0x000F;
	
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
	struct php_ctlr_state_s *php_ctlr = slot->ctrl->hpc_ctlr_handle;
	u32 slot_reg;
	u16 slot_status;
	u8 atten_led_state;
	
	DBG_ENTER_ROUTINE 

	if (!slot->ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return -1;
	}

	slot_reg = readl(php_ctlr->creg + SLOT1 + 4*(slot->hp_slot));
	slot_status = (u16) slot_reg;
	atten_led_state = (slot_status & 0x0030) >> 4;

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

	DBG_LEAVE_ROUTINE 
	return 0;
}

static int hpc_get_power_status(struct slot * slot, u8 *status)
{
	struct php_ctlr_state_s *php_ctlr = slot->ctrl->hpc_ctlr_handle;
	u32 slot_reg;
	u16 slot_status;
	u8 slot_state;
	int	retval = 0;
	
	DBG_ENTER_ROUTINE 

	if (!slot->ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return -1;
	}

	slot_reg = readl(php_ctlr->creg + SLOT1 + 4*(slot->hp_slot));
	slot_status = (u16) slot_reg;
	slot_state = (slot_status & 0x0003);

	switch (slot_state) {
	case 0:
		*status = 0xFF;
		break;
	case 1:
		*status = 2;	/* Powered only */
		break;
	case 2:
		*status = 1;	/* Enabled */
		break;
	case 3:
		*status = 0;	/* Disabled */
		break;
	default:
		*status = 0xFF;
		break;
	}

	DBG_LEAVE_ROUTINE 
	return retval;
}


static int hpc_get_latch_status(struct slot *slot, u8 *status)
{
	struct php_ctlr_state_s *php_ctlr = slot->ctrl->hpc_ctlr_handle;
	u32 slot_reg;
	u16 slot_status;

	DBG_ENTER_ROUTINE 

	if (!slot->ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return -1;
	}

	slot_reg = readl(php_ctlr->creg + SLOT1 + 4*(slot->hp_slot));
	slot_status = (u16)slot_reg;

	*status = ((slot_status & 0x0100) == 0) ? 0 : 1;   /* 0 -> close; 1 -> open */


	DBG_LEAVE_ROUTINE 
	return 0;
}

static int hpc_get_adapter_status(struct slot *slot, u8 *status)
{
	struct php_ctlr_state_s *php_ctlr = slot->ctrl->hpc_ctlr_handle;
	u32 slot_reg;
	u16 slot_status;
	u8 card_state;

	DBG_ENTER_ROUTINE 

	if (!slot->ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return -1;
	}

	slot_reg = readl(php_ctlr->creg + SLOT1 + 4*(slot->hp_slot));
	slot_status = (u16)slot_reg;
	card_state = (u8)((slot_status & 0x0C00) >> 10);
	*status = (card_state != 0x3) ? 1 : 0;

	DBG_LEAVE_ROUTINE 
	return 0;
}

static int hpc_get_prog_int(struct slot *slot, u8 *prog_int)
{
	struct php_ctlr_state_s *php_ctlr = slot->ctrl->hpc_ctlr_handle;

	DBG_ENTER_ROUTINE 
	
	if (!slot->ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return -1;
	}

	*prog_int = readb(php_ctlr->creg + PROG_INTERFACE);

	DBG_LEAVE_ROUTINE 
	return 0;
}

static int hpc_get_adapter_speed(struct slot *slot, enum pci_bus_speed *value)
{
	struct php_ctlr_state_s *php_ctlr = slot->ctrl->hpc_ctlr_handle;
	u32 slot_reg;
	u16 slot_status, sec_bus_status;
	u8 m66_cap, pcix_cap, pi;
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
	
	pi = readb(php_ctlr->creg + PROG_INTERFACE);
	slot_reg = readl(php_ctlr->creg + SLOT1 + 4*(slot->hp_slot));
	dbg("%s: pi = %d, slot_reg = %x\n", __FUNCTION__, pi, slot_reg);
	slot_status = (u16) slot_reg;
	dbg("%s: slot_status = %x\n", __FUNCTION__, slot_status);
	sec_bus_status = readw(php_ctlr->creg + SEC_BUS_CONFIG);

	pcix_cap = (u8) ((slot_status & 0x3000) >> 12);
	dbg("%s:  pcix_cap = %x\n", __FUNCTION__, pcix_cap);
	m66_cap = (u8) ((slot_status & 0x0200) >> 9);
	dbg("%s:  m66_cap = %x\n", __FUNCTION__, m66_cap);


	if (pi == 2) {
		switch (pcix_cap) {
		case 0:
			*value = m66_cap ? PCI_SPEED_66MHz : PCI_SPEED_33MHz;
			break;
		case 1:
			*value = PCI_SPEED_66MHz_PCIX;
			break;
		case 3:
			*value = PCI_SPEED_133MHz_PCIX;
			break;
		case 4:
			*value = PCI_SPEED_133MHz_PCIX_266;	
			break;
		case 5:
			*value = PCI_SPEED_133MHz_PCIX_533;	
			break;
		case 2:	/* Reserved */
		default:
			*value = PCI_SPEED_UNKNOWN;
			retval = -ENODEV;
			break;
		}
	} else {
		switch (pcix_cap) {
		case 0:
			*value = m66_cap ? PCI_SPEED_66MHz : PCI_SPEED_33MHz;
			break;
		case 1:
			*value = PCI_SPEED_66MHz_PCIX;
			break;
		case 3:
			*value = PCI_SPEED_133MHz_PCIX;	
			break;
		case 2:	/* Reserved */
		default:
			*value = PCI_SPEED_UNKNOWN;
			retval = -ENODEV;
			break;
		}
	}

	dbg("Adapter speed = %d\n", *value);
	
	DBG_LEAVE_ROUTINE 
	return retval;
}

static int hpc_get_mode1_ECC_cap(struct slot *slot, u8 *mode)
{
	struct php_ctlr_state_s *php_ctlr = slot->ctrl->hpc_ctlr_handle;
	u16 sec_bus_status;
	u8 pi;
	int retval = 0;

	DBG_ENTER_ROUTINE 

	if (!slot->ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return -1;
	}

	pi = readb(php_ctlr->creg + PROG_INTERFACE);
	sec_bus_status = readw(php_ctlr->creg + SEC_BUS_CONFIG);

	if (pi == 2) {
		*mode = (sec_bus_status & 0x0100) >> 7;
	} else {
		retval = -1;
	}

	dbg("Mode 1 ECC cap = %d\n", *mode);
	
	DBG_LEAVE_ROUTINE 
	return retval;
}

static int hpc_query_power_fault(struct slot * slot)
{
	struct php_ctlr_state_s *php_ctlr = slot->ctrl->hpc_ctlr_handle;
	u32 slot_reg;
	u16 slot_status;
	u8 pwr_fault_state, status;

	DBG_ENTER_ROUTINE 

	if (!slot->ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return -1;
	}

	slot_reg = readl(php_ctlr->creg + SLOT1 + 4*(slot->hp_slot));
	slot_status = (u16) slot_reg;
	pwr_fault_state = (slot_status & 0x0040) >> 7;
	status = (pwr_fault_state == 1) ? 0 : 1;

	DBG_LEAVE_ROUTINE
	/* Note: Logic 0 => fault */
	return status;
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
	struct php_ctlr_state_s *php_ctlr = ctrl->hpc_ctlr_handle;

	DBG_ENTER_ROUTINE 

	if (!ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return -1;
	}

	*first_device_num = php_ctlr->slot_device_offset;	/* Obtained in shpc_init() */
	*num_ctlr_slots = php_ctlr->num_slots;			/* Obtained in shpc_init() */

	*physical_slot_num = (readl(php_ctlr->creg + SLOT_CONFIG) & PSN) >> 16;
	dbg("%s: physical_slot_num = %x\n", __FUNCTION__, *physical_slot_num);
	*updown = ((readl(php_ctlr->creg + SLOT_CONFIG) & UPDOWN ) >> 29) ? 1 : -1;	

	DBG_LEAVE_ROUTINE 
	return 0;
}

static void hpc_release_ctlr(struct controller *ctrl)
{
	struct php_ctlr_state_s *php_ctlr = ctrl->hpc_ctlr_handle;
	struct php_ctlr_state_s *p, *p_prev;

	DBG_ENTER_ROUTINE 

	if (!ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return ;
	}

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
		release_mem_region(pci_resource_start(php_ctlr->pci_dev, 0), pci_resource_len(php_ctlr->pci_dev, 0));
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
	u8 slot_cmd;
	u8 pi;
	int retval = 0;
	struct php_ctlr_state_s *php_ctlr = slot->ctrl->hpc_ctlr_handle;

	DBG_ENTER_ROUTINE 
	
	if (!slot->ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return -1;
	}

	pi = readb(php_ctlr->creg + PROG_INTERFACE);
	
	if (pi == 1) {
		switch (value) {
		case 0:
			slot_cmd = SETA_PCI_33MHZ;
			break;
		case 1:
			slot_cmd = SETA_PCI_66MHZ;
			break;
		case 2:
			slot_cmd = SETA_PCIX_66MHZ;
			break;
		case 3:
			slot_cmd = SETA_PCIX_100MHZ;	
			break;
		case 4:
			slot_cmd = SETA_PCIX_133MHZ;	
			break;
		default:
			slot_cmd = PCI_SPEED_UNKNOWN;
			retval = -ENODEV;
			return retval;	
		}
	} else {
		switch (value) {
		case 0:
			slot_cmd = SETB_PCI_33MHZ;
			break;
		case 1:
			slot_cmd = SETB_PCI_66MHZ;
			break;
		case 2:
			slot_cmd = SETB_PCIX_66MHZ_PM;
			break;
		case 3:
			slot_cmd = SETB_PCIX_100MHZ_PM;	
			break;
		case 4:
			slot_cmd = SETB_PCIX_133MHZ_PM;	
			break;
		case 5:
			slot_cmd = SETB_PCIX_66MHZ_EM;	
			break;
		case 6:
			slot_cmd = SETB_PCIX_100MHZ_EM;	
			break;
		case 7:
			slot_cmd = SETB_PCIX_133MHZ_EM;	
			break;
		case 8:
			slot_cmd = SETB_PCIX_66MHZ_266;	
			break;
		case 0x9:
			slot_cmd = SETB_PCIX_100MHZ_266;	
			break;
		case 0xa:
			slot_cmd = SETB_PCIX_133MHZ_266;	
			break;
		case 0xb:
			slot_cmd = SETB_PCIX_66MHZ_533;	
			break;
		case 0xc:
			slot_cmd = SETB_PCIX_100MHZ_533;	
			break;
		case 0xd:
			slot_cmd = SETB_PCIX_133MHZ_533;	
			break;
		default:
			slot_cmd = PCI_SPEED_UNKNOWN;
			retval = -ENODEV;
			return retval;	
		}

	}
	retval = shpc_write_cmd(slot, 0, slot_cmd);
	if (retval) {
		err("%s: Write command failed!\n", __FUNCTION__);
		return -1;
	}

	DBG_LEAVE_ROUTINE
	return retval;
}

static irqreturn_t shpc_isr(int IRQ, void *dev_id, struct pt_regs *regs)
{
	struct controller *ctrl = NULL;
	struct php_ctlr_state_s *php_ctlr;
	u8 schedule_flag = 0;
	u8 temp_byte;
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
	intr_loc = readl(php_ctlr->creg + INTR_LOC);  

	if (!intr_loc)
		return IRQ_NONE;
	dbg("%s: intr_loc = %x\n",__FUNCTION__, intr_loc); 

	if(!shpchp_poll_mode) {
		/* Mask Global Interrupt Mask - see implementation note on p. 139 */
		/* of SHPC spec rev 1.0*/
		temp_dword = readl(php_ctlr->creg + SERR_INTR_ENABLE);
		temp_dword |= 0x00000001;
		writel(temp_dword, php_ctlr->creg + SERR_INTR_ENABLE);

		intr_loc2 = readl(php_ctlr->creg + INTR_LOC);  
		dbg("%s: intr_loc2 = %x\n",__FUNCTION__, intr_loc2); 
	}

	if (intr_loc & 0x0001) {
		/* 
		 * Command Complete Interrupt Pending 
		 * RO only - clear by writing 0 to the Command Completion
		 * Detect bit in Controller SERR-INT register
		 */
		temp_dword = readl(php_ctlr->creg + SERR_INTR_ENABLE);
		temp_dword &= 0xfffeffff;
		writel(temp_dword, php_ctlr->creg + SERR_INTR_ENABLE);
		wake_up_interruptible(&ctrl->queue);
	}

	if ((intr_loc = (intr_loc >> 1)) == 0) {
		/* Unmask Global Interrupt Mask */
		temp_dword = readl(php_ctlr->creg + SERR_INTR_ENABLE);
		temp_dword &= 0xfffffffe;
		writel(temp_dword, php_ctlr->creg + SERR_INTR_ENABLE);

		return IRQ_NONE;
	}

	for (hp_slot = 0; hp_slot < ctrl->num_slots; hp_slot++) { 
	/* To find out which slot has interrupt pending */
		if ((intr_loc >> hp_slot) & 0x01) {
			temp_dword = readl(php_ctlr->creg + SLOT1 + (4*hp_slot));
			dbg("%s: Slot %x with intr, slot register = %x\n",
				__FUNCTION__, hp_slot, temp_dword);
			temp_byte = (temp_dword >> 16) & 0xFF;
			if ((php_ctlr->switch_change_callback) && (temp_byte & 0x08))
				schedule_flag += php_ctlr->switch_change_callback(
					hp_slot, php_ctlr->callback_instance_id);
			if ((php_ctlr->attention_button_callback) && (temp_byte & 0x04))
				schedule_flag += php_ctlr->attention_button_callback(
					hp_slot, php_ctlr->callback_instance_id);
			if ((php_ctlr->presence_change_callback) && (temp_byte & 0x01))
				schedule_flag += php_ctlr->presence_change_callback(
					hp_slot , php_ctlr->callback_instance_id);
			if ((php_ctlr->power_fault_callback) && (temp_byte & 0x12))
				schedule_flag += php_ctlr->power_fault_callback(
					hp_slot, php_ctlr->callback_instance_id);
			
			/* Clear all slot events */
			temp_dword = 0xe01f3fff;
			writel(temp_dword, php_ctlr->creg + SLOT1 + (4*hp_slot));

			intr_loc2 = readl(php_ctlr->creg + INTR_LOC);  
			dbg("%s: intr_loc2 = %x\n",__FUNCTION__, intr_loc2); 
		}
	}
	if (!shpchp_poll_mode) {
		/* Unmask Global Interrupt Mask */
		temp_dword = readl(php_ctlr->creg + SERR_INTR_ENABLE);
		temp_dword &= 0xfffffffe;
		writel(temp_dword, php_ctlr->creg + SERR_INTR_ENABLE);
	}
	
	return IRQ_HANDLED;
}

static int hpc_get_max_bus_speed (struct slot *slot, enum pci_bus_speed *value)
{
	struct php_ctlr_state_s *php_ctlr = slot->ctrl->hpc_ctlr_handle;
	enum pci_bus_speed bus_speed = PCI_SPEED_UNKNOWN;
	int retval = 0;
	u8 pi;
	u32 slot_avail1, slot_avail2;
	int slot_num;

	DBG_ENTER_ROUTINE 

	if (!slot->ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return -1;
	}

	if (slot->hp_slot >= php_ctlr->num_slots) {
		err("%s: Invalid HPC slot number!\n", __FUNCTION__);
		return -1;
	}

	pi = readb(php_ctlr->creg + PROG_INTERFACE);
	slot_avail1 = readl(php_ctlr->creg + SLOT_AVAIL1);
	slot_avail2 = readl(php_ctlr->creg + SLOT_AVAIL2);

	if (pi == 2) {
		if ((slot_num = ((slot_avail2 & SLOT_133MHZ_PCIX_533) >> 27)  ) != 0 )
			bus_speed = PCIX_133MHZ_533;
		else if ((slot_num = ((slot_avail2 & SLOT_100MHZ_PCIX_533) >> 23)  ) != 0 )
			bus_speed = PCIX_100MHZ_533;
		else if ((slot_num = ((slot_avail2 & SLOT_66MHZ_PCIX_533) >> 19)  ) != 0 )
			bus_speed = PCIX_66MHZ_533;
		else if ((slot_num = ((slot_avail2 & SLOT_133MHZ_PCIX_266) >> 15)  ) != 0 )
			bus_speed = PCIX_133MHZ_266;
		else if ((slot_num = ((slot_avail2 & SLOT_100MHZ_PCIX_266) >> 11)  ) != 0 )
			bus_speed = PCIX_100MHZ_266;
		else if ((slot_num = ((slot_avail2 & SLOT_66MHZ_PCIX_266) >> 7)  ) != 0 )
			bus_speed = PCIX_66MHZ_266;
		else if ((slot_num = ((slot_avail1 & SLOT_133MHZ_PCIX) >> 23)  ) != 0 )
			bus_speed = PCIX_133MHZ;
		else if ((slot_num = ((slot_avail1 & SLOT_100MHZ_PCIX) >> 15)  ) != 0 )
			bus_speed = PCIX_100MHZ;
		else if ((slot_num = ((slot_avail1 & SLOT_66MHZ_PCIX) >> 7)  ) != 0 )
			bus_speed = PCIX_66MHZ;
		else if ((slot_num = (slot_avail2 & SLOT_66MHZ)) != 0 )
			bus_speed = PCI_66MHZ;
		else if ((slot_num = (slot_avail1 & SLOT_33MHZ)) != 0 )
			bus_speed = PCI_33MHZ;
		else bus_speed = PCI_SPEED_UNKNOWN;
	} else {
		if ((slot_num = ((slot_avail1 & SLOT_133MHZ_PCIX) >> 23)  ) != 0 )
			bus_speed = PCIX_133MHZ;
		else if ((slot_num = ((slot_avail1 & SLOT_100MHZ_PCIX) >> 15)  ) != 0 )
			bus_speed = PCIX_100MHZ;
		else if ((slot_num = ((slot_avail1 & SLOT_66MHZ_PCIX) >> 7)  ) != 0 )
			bus_speed = PCIX_66MHZ;
		else if ((slot_num = (slot_avail2 & SLOT_66MHZ)) != 0 )
			bus_speed = PCI_66MHZ;
		else if ((slot_num = (slot_avail1 & SLOT_33MHZ)) != 0 )
			bus_speed = PCI_33MHZ;
		else bus_speed = PCI_SPEED_UNKNOWN;
	}

	*value = bus_speed;
	dbg("Max bus speed = %d\n", bus_speed);
	DBG_LEAVE_ROUTINE 
	return retval;
}

static int hpc_get_cur_bus_speed (struct slot *slot, enum pci_bus_speed *value)
{
	struct php_ctlr_state_s *php_ctlr = slot->ctrl->hpc_ctlr_handle;
	enum pci_bus_speed bus_speed = PCI_SPEED_UNKNOWN;
	u16 sec_bus_status;
	int retval = 0;
	u8 pi;

	DBG_ENTER_ROUTINE 

	if (!slot->ctrl->hpc_ctlr_handle) {
		err("%s: Invalid HPC controller handle!\n", __FUNCTION__);
		return -1;
	}

	if (slot->hp_slot >= php_ctlr->num_slots) {
		err("%s: Invalid HPC slot number!\n", __FUNCTION__);
		return -1;
	}

	pi = readb(php_ctlr->creg + PROG_INTERFACE);
	sec_bus_status = readw(php_ctlr->creg + SEC_BUS_CONFIG);

	if (pi == 2) {
		switch (sec_bus_status & 0x000f) {
		case 0:
			bus_speed = PCI_SPEED_33MHz;
			break;
		case 1:
			bus_speed = PCI_SPEED_66MHz;
			break;
		case 2:
			bus_speed = PCI_SPEED_66MHz_PCIX;
			break;
		case 3:
			bus_speed = PCI_SPEED_100MHz_PCIX;	
			break;
		case 4:
			bus_speed = PCI_SPEED_133MHz_PCIX;	
			break;
		case 5:
			bus_speed = PCI_SPEED_66MHz_PCIX_ECC;
			break;
		case 6:
			bus_speed = PCI_SPEED_100MHz_PCIX_ECC;
			break;
		case 7:
			bus_speed = PCI_SPEED_133MHz_PCIX_ECC;	
			break;
		case 8:
			bus_speed = PCI_SPEED_66MHz_PCIX_266;	
			break;
		case 9:
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
		case 0xe:
		case 0xf:
		default:
			bus_speed = PCI_SPEED_UNKNOWN;
			break;
		}
	} else {
		/* In the case where pi is undefined, default it to 1 */ 
		switch (sec_bus_status & 0x0007) {
		case 0:
			bus_speed = PCI_SPEED_33MHz;
			break;
		case 1:
			bus_speed = PCI_SPEED_66MHz;
			break;
		case 2:
			bus_speed = PCI_SPEED_66MHz_PCIX;
			break;
		case 3:
			bus_speed = PCI_SPEED_100MHz_PCIX;	
			break;
		case 4:
			bus_speed = PCI_SPEED_133MHz_PCIX;	
			break;
		case 5:
			bus_speed = PCI_SPEED_UNKNOWN;		/*	Reserved */
			break;
		case 6:
			bus_speed = PCI_SPEED_UNKNOWN;		/*	Reserved */
			break;
		case 7:
			bus_speed = PCI_SPEED_UNKNOWN;		/*	Reserved */	
			break;
		default:
			bus_speed = PCI_SPEED_UNKNOWN;
			break;
		}
	}

	*value = bus_speed;
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
	.check_cmd_status		= hpc_check_cmd_status,
};

int shpc_init(struct controller * ctrl, struct pci_dev * pdev)
{
	struct php_ctlr_state_s *php_ctlr, *p;
	void *instance_id = ctrl;
	int rc;
	u8 hp_slot;
	static int first = 1;
	u32 shpc_cap_offset, shpc_base_offset;
	u32 tempdword, slot_reg;
	u8 i;

	DBG_ENTER_ROUTINE

	spin_lock_init(&list_lock);
	php_ctlr = (struct php_ctlr_state_s *) kmalloc(sizeof(struct php_ctlr_state_s), GFP_KERNEL);

	if (!php_ctlr) {	/* allocate controller state data */
		err("%s: HPC controller memory allocation error!\n", __FUNCTION__);
		goto abort;
	}

	memset(php_ctlr, 0, sizeof(struct php_ctlr_state_s));

	php_ctlr->pci_dev = pdev;	/* save pci_dev in context */

	if ((pdev->vendor == PCI_VENDOR_ID_AMD) || (pdev->device ==
				PCI_DEVICE_ID_AMD_GOLAM_7450)) {
		shpc_base_offset = 0;  /* amd shpc driver doesn't use this; assume 0 */
	} else {
		if ((shpc_cap_offset = pci_find_capability(pdev, PCI_CAP_ID_SHPC)) == 0) {
			err("%s : shpc_cap_offset == 0\n", __FUNCTION__);
			goto abort_free_ctlr;
		}
		dbg("%s: shpc_cap_offset = %x\n", __FUNCTION__, shpc_cap_offset);	
	
		rc = pci_write_config_byte(pdev, (u8)shpc_cap_offset + DWORD_SELECT , BASE_OFFSET);
		if (rc) {
			err("%s : pci_word_config_byte failed\n", __FUNCTION__);
			goto abort_free_ctlr;
		}
	
		rc = pci_read_config_dword(pdev, (u8)shpc_cap_offset + DWORD_DATA, &shpc_base_offset);
		if (rc) {
			err("%s : pci_read_config_dword failed\n", __FUNCTION__);
			goto abort_free_ctlr;
		}

		for (i = 0; i <= 14; i++) {
			rc = pci_write_config_byte(pdev, (u8)shpc_cap_offset +  DWORD_SELECT , i);
			if (rc) {
				err("%s : pci_word_config_byte failed\n", __FUNCTION__);
				goto abort_free_ctlr;
			}
	
			rc = pci_read_config_dword(pdev, (u8)shpc_cap_offset + DWORD_DATA, &tempdword);
			if (rc) {
				err("%s : pci_read_config_dword failed\n", __FUNCTION__);
				goto abort_free_ctlr;
			}
			dbg("%s: offset %d: value %x\n", __FUNCTION__,i,
					tempdword);
		}
	}

	if (first) {
		spin_lock_init(&hpc_event_lock);
		first = 0;
	}

	info("HPC vendor_id %x device_id %x ss_vid %x ss_did %x\n", pdev->vendor, pdev->device, pdev->subsystem_vendor, 
		pdev->subsystem_device);
	
	if (pci_enable_device(pdev))
		goto abort_free_ctlr;

	if (!request_mem_region(pci_resource_start(pdev, 0) + shpc_base_offset, pci_resource_len(pdev, 0), MY_NAME)) {
		err("%s: cannot reserve MMIO region\n", __FUNCTION__);
		goto abort_free_ctlr;
	}

	php_ctlr->creg = ioremap(pci_resource_start(pdev, 0) + shpc_base_offset, pci_resource_len(pdev, 0));
	if (!php_ctlr->creg) {
		err("%s: cannot remap MMIO region %lx @ %lx\n", __FUNCTION__, pci_resource_len(pdev, 0), 
			pci_resource_start(pdev, 0) + shpc_base_offset);
		release_mem_region(pci_resource_start(pdev, 0) + shpc_base_offset, pci_resource_len(pdev, 0));
		goto abort_free_ctlr;
	}
	dbg("%s: php_ctlr->creg %p\n", __FUNCTION__, php_ctlr->creg);

	init_MUTEX(&ctrl->crit_sect);
	/* Setup wait queue */
	init_waitqueue_head(&ctrl->queue);

	/* Find the IRQ */
	php_ctlr->irq = pdev->irq;
	php_ctlr->attention_button_callback = shpchp_handle_attention_button,
	php_ctlr->switch_change_callback = shpchp_handle_switch_change;
	php_ctlr->presence_change_callback = shpchp_handle_presence_change;
	php_ctlr->power_fault_callback = shpchp_handle_power_fault;
	php_ctlr->callback_instance_id = instance_id;

	/* Return PCI Controller Info */
	php_ctlr->slot_device_offset = (readl(php_ctlr->creg + SLOT_CONFIG) & FIRST_DEV_NUM ) >> 8;
	php_ctlr->num_slots = readl(php_ctlr->creg + SLOT_CONFIG) & SLOT_NUM;
	dbg("%s: slot_device_offset %x\n", __FUNCTION__, php_ctlr->slot_device_offset);
	dbg("%s: num_slots %x\n", __FUNCTION__, php_ctlr->num_slots);

	/* Mask Global Interrupt Mask & Command Complete Interrupt Mask */
	tempdword = readl(php_ctlr->creg + SERR_INTR_ENABLE);
	dbg("%s: SERR_INTR_ENABLE = %x\n", __FUNCTION__, tempdword);
	tempdword = 0x0003000f;   
	writel(tempdword, php_ctlr->creg + SERR_INTR_ENABLE);
	tempdword = readl(php_ctlr->creg + SERR_INTR_ENABLE);
	dbg("%s: SERR_INTR_ENABLE = %x\n", __FUNCTION__, tempdword);

	/* Mask the MRL sensor SERR Mask of individual slot in
	 * Slot SERR-INT Mask & clear all the existing event if any
	 */
	for (hp_slot = 0; hp_slot < php_ctlr->num_slots; hp_slot++) {
		slot_reg = readl(php_ctlr->creg + SLOT1 + 4*hp_slot );
		dbg("%s: Default Logical Slot Register %d value %x\n", __FUNCTION__,
			hp_slot, slot_reg);
		tempdword = 0xffff3fff;  
		writel(tempdword, php_ctlr->creg + SLOT1 + (4*hp_slot));
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
	ctrl->hpc_ctlr_handle = php_ctlr;
	ctrl->hpc_ops = &shpchp_hpc_ops;

	for (hp_slot = 0; hp_slot < php_ctlr->num_slots; hp_slot++) {
		slot_reg = readl(php_ctlr->creg + SLOT1 + 4*hp_slot );
		dbg("%s: Default Logical Slot Register %d value %x\n", __FUNCTION__,
			hp_slot, slot_reg);
		tempdword = 0xe01f3fff;  
		writel(tempdword, php_ctlr->creg + SLOT1 + (4*hp_slot));
	}
	if (!shpchp_poll_mode) {
		/* Unmask all general input interrupts and SERR */
		tempdword = readl(php_ctlr->creg + SERR_INTR_ENABLE);
		tempdword = 0x0000000a;
		writel(tempdword, php_ctlr->creg + SERR_INTR_ENABLE);
		tempdword = readl(php_ctlr->creg + SERR_INTR_ENABLE);
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
