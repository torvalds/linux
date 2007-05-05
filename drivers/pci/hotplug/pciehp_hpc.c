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

static atomic_t pciehp_num_controllers = ATOMIC_INIT(0);

struct ctrl_reg {
	u8 cap_id;
	u8 nxt_ptr;
	u16 cap_reg;
	u32 dev_cap;
	u16 dev_ctrl;
	u16 dev_status;
	u32 lnk_cap;
	u16 lnk_ctrl;
	u16 lnk_status;
	u32 slot_cap;
	u16 slot_ctrl;
	u16 slot_status;
	u16 root_ctrl;
	u16 rsvp;
	u32 root_status;
} __attribute__ ((packed));

/* offsets to the controller registers based on the above structure layout */
enum ctrl_offsets {
	PCIECAPID	=	offsetof(struct ctrl_reg, cap_id),
	NXTCAPPTR	=	offsetof(struct ctrl_reg, nxt_ptr),
	CAPREG		=	offsetof(struct ctrl_reg, cap_reg),
	DEVCAP		=	offsetof(struct ctrl_reg, dev_cap),
	DEVCTRL		=	offsetof(struct ctrl_reg, dev_ctrl),
	DEVSTATUS	=	offsetof(struct ctrl_reg, dev_status),
	LNKCAP		=	offsetof(struct ctrl_reg, lnk_cap),
	LNKCTRL		=	offsetof(struct ctrl_reg, lnk_ctrl),
	LNKSTATUS	=	offsetof(struct ctrl_reg, lnk_status),
	SLOTCAP		=	offsetof(struct ctrl_reg, slot_cap),
	SLOTCTRL	=	offsetof(struct ctrl_reg, slot_ctrl),
	SLOTSTATUS	=	offsetof(struct ctrl_reg, slot_status),
	ROOTCTRL	=	offsetof(struct ctrl_reg, root_ctrl),
	ROOTSTATUS	=	offsetof(struct ctrl_reg, root_status),
};

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

/* Field definitions in PCI Express Capabilities Register */
#define CAP_VER			0x000F
#define DEV_PORT_TYPE		0x00F0
#define SLOT_IMPL		0x0100
#define MSG_NUM			0x3E00

/* Device or Port Type */
#define NAT_ENDPT		0x00
#define LEG_ENDPT		0x01
#define ROOT_PORT		0x04
#define UP_STREAM		0x05
#define	DN_STREAM		0x06
#define PCIE_PCI_BRDG		0x07
#define PCI_PCIE_BRDG		0x10

/* Field definitions in Device Capabilities Register */
#define DATTN_BUTTN_PRSN	0x1000
#define DATTN_LED_PRSN		0x2000
#define DPWR_LED_PRSN		0x4000

/* Field definitions in Link Capabilities Register */
#define MAX_LNK_SPEED		0x000F
#define MAX_LNK_WIDTH		0x03F0

/* Link Width Encoding */
#define LNK_X1		0x01
#define LNK_X2		0x02
#define LNK_X4		0x04	
#define LNK_X8		0x08
#define LNK_X12		0x0C
#define LNK_X16		0x10	
#define LNK_X32		0x20

/*Field definitions of Link Status Register */
#define LNK_SPEED	0x000F
#define NEG_LINK_WD	0x03F0
#define LNK_TRN_ERR	0x0400
#define	LNK_TRN		0x0800
#define SLOT_CLK_CONF	0x1000

/* Field definitions in Slot Capabilities Register */
#define ATTN_BUTTN_PRSN	0x00000001
#define	PWR_CTRL_PRSN	0x00000002
#define MRL_SENS_PRSN	0x00000004
#define ATTN_LED_PRSN	0x00000008
#define PWR_LED_PRSN	0x00000010
#define HP_SUPR_RM_SUP	0x00000020
#define HP_CAP		0x00000040
#define SLOT_PWR_VALUE	0x000003F8
#define SLOT_PWR_LIMIT	0x00000C00
#define PSN		0xFFF80000	/* PSN: Physical Slot Number */

/* Field definitions in Slot Control Register */
#define ATTN_BUTTN_ENABLE		0x0001
#define PWR_FAULT_DETECT_ENABLE		0x0002
#define MRL_DETECT_ENABLE		0x0004
#define PRSN_DETECT_ENABLE		0x0008
#define CMD_CMPL_INTR_ENABLE		0x0010
#define HP_INTR_ENABLE			0x0020
#define ATTN_LED_CTRL			0x00C0
#define PWR_LED_CTRL			0x0300
#define PWR_CTRL			0x0400
#define EMI_CTRL			0x0800

/* Attention indicator and Power indicator states */
#define LED_ON		0x01
#define LED_BLINK	0x10
#define LED_OFF		0x11

/* Power Control Command */
#define POWER_ON	0
#define POWER_OFF	0x0400

/* EMI Status defines */
#define EMI_DISENGAGED	0
#define EMI_ENGAGED	1

/* Field definitions in Slot Status Register */
#define ATTN_BUTTN_PRESSED	0x0001
#define PWR_FAULT_DETECTED	0x0002
#define MRL_SENS_CHANGED	0x0004
#define PRSN_DETECT_CHANGED	0x0008
#define CMD_COMPLETED		0x0010
#define MRL_STATE		0x0020
#define PRSN_STATE		0x0040
#define EMI_STATE		0x0080
#define EMI_STATUS_BIT		7

DEFINE_DBG_BUFFER		/* Debug string buffer for entire HPC defined here */

static irqreturn_t pcie_isr(int irq, void *dev_id);
static void start_int_poll_timer(struct controller *ctrl, int sec);

/* This is the interrupt polling timeout function. */
static void int_poll_timeout(unsigned long data)
{
	struct controller *ctrl = (struct controller *)data;

	DBG_ENTER_ROUTINE

	/* Poll for interrupt events.  regs == NULL => polling */
	pcie_isr(0, ctrl);

	init_timer(&ctrl->poll_timer);
	if (!pciehp_poll_time)
		pciehp_poll_time = 2; /* reset timer to poll in 2 secs if user doesn't specify at module installation*/

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

static inline int pcie_wait_cmd(struct controller *ctrl)
{
	int retval = 0;
	unsigned int msecs = pciehp_poll_mode ? 2500 : 1000;
	unsigned long timeout = msecs_to_jiffies(msecs);
	int rc;

	rc = wait_event_interruptible_timeout(ctrl->queue,
					      !ctrl->cmd_busy, timeout);
	if (!rc)
		dbg("Command not completed in 1000 msec\n");
	else if (rc < 0) {
		retval = -EINTR;
		info("Command was interrupted by a signal\n");
	}

	return retval;
}

static int pcie_write_cmd(struct slot *slot, u16 cmd)
{
	struct controller *ctrl = slot->ctrl;
	int retval = 0;
	u16 slot_status;

	DBG_ENTER_ROUTINE 

	mutex_lock(&ctrl->ctrl_lock);

	retval = pciehp_readw(ctrl, SLOTSTATUS, &slot_status);
	if (retval) {
		err("%s: Cannot read SLOTSTATUS register\n", __FUNCTION__);
		goto out;
	}

	if ((slot_status & CMD_COMPLETED) == CMD_COMPLETED ) { 
		/* After 1 sec and CMD_COMPLETED still not set, just
		   proceed forward to issue the next command according
		   to spec.  Just print out the error message */
		dbg("%s: CMD_COMPLETED not clear after 1 sec.\n",
		    __FUNCTION__);
	}

	ctrl->cmd_busy = 1;
	retval = pciehp_writew(ctrl, SLOTCTRL, (cmd | CMD_CMPL_INTR_ENABLE));
	if (retval) {
		err("%s: Cannot write to SLOTCTRL register\n", __FUNCTION__);
		goto out;
	}

	/*
	 * Wait for command completion.
	 */
	retval = pcie_wait_cmd(ctrl);
 out:
	mutex_unlock(&ctrl->ctrl_lock);
	DBG_LEAVE_ROUTINE 
	return retval;
}

static int hpc_check_lnk_status(struct controller *ctrl)
{
	u16 lnk_status;
	int retval = 0;

	DBG_ENTER_ROUTINE 

	retval = pciehp_readw(ctrl, LNKSTATUS, &lnk_status);
	if (retval) {
		err("%s: Cannot read LNKSTATUS register\n", __FUNCTION__);
		return retval;
	}

	dbg("%s: lnk_status = %x\n", __FUNCTION__, lnk_status);
	if ( (lnk_status & LNK_TRN) || (lnk_status & LNK_TRN_ERR) || 
		!(lnk_status & NEG_LINK_WD)) {
		err("%s : Link Training Error occurs \n", __FUNCTION__);
		retval = -1;
		return retval;
	}

	DBG_LEAVE_ROUTINE 
	return retval;
}


static int hpc_get_attention_status(struct slot *slot, u8 *status)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_ctrl;
	u8 atten_led_state;
	int retval = 0;
	
	DBG_ENTER_ROUTINE 

	retval = pciehp_readw(ctrl, SLOTCTRL, &slot_ctrl);
	if (retval) {
		err("%s: Cannot read SLOTCTRL register\n", __FUNCTION__);
		return retval;
	}

	dbg("%s: SLOTCTRL %x, value read %x\n",
	    __FUNCTION__, ctrl->cap_base + SLOTCTRL, slot_ctrl);

	atten_led_state = (slot_ctrl & ATTN_LED_CTRL) >> 6;

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

static int hpc_get_power_status(struct slot *slot, u8 *status)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_ctrl;
	u8 pwr_state;
	int	retval = 0;
	
	DBG_ENTER_ROUTINE 

	retval = pciehp_readw(ctrl, SLOTCTRL, &slot_ctrl);
	if (retval) {
		err("%s: Cannot read SLOTCTRL register\n", __FUNCTION__);
		return retval;
	}
	dbg("%s: SLOTCTRL %x value read %x\n",
	    __FUNCTION__, ctrl->cap_base + SLOTCTRL, slot_ctrl);

	pwr_state = (slot_ctrl & PWR_CTRL) >> 10;

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

	DBG_LEAVE_ROUTINE 
	return retval;
}


static int hpc_get_latch_status(struct slot *slot, u8 *status)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_status;
	int retval = 0;

	DBG_ENTER_ROUTINE 

	retval = pciehp_readw(ctrl, SLOTSTATUS, &slot_status);
	if (retval) {
		err("%s: Cannot read SLOTSTATUS register\n", __FUNCTION__);
		return retval;
	}

	*status = (((slot_status & MRL_STATE) >> 5) == 0) ? 0 : 1;  

	DBG_LEAVE_ROUTINE 
	return 0;
}

static int hpc_get_adapter_status(struct slot *slot, u8 *status)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_status;
	u8 card_state;
	int retval = 0;

	DBG_ENTER_ROUTINE 

	retval = pciehp_readw(ctrl, SLOTSTATUS, &slot_status);
	if (retval) {
		err("%s: Cannot read SLOTSTATUS register\n", __FUNCTION__);
		return retval;
	}
	card_state = (u8)((slot_status & PRSN_STATE) >> 6);
	*status = (card_state == 1) ? 1 : 0;

	DBG_LEAVE_ROUTINE 
	return 0;
}

static int hpc_query_power_fault(struct slot *slot)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_status;
	u8 pwr_fault;
	int retval = 0;

	DBG_ENTER_ROUTINE 

	retval = pciehp_readw(ctrl, SLOTSTATUS, &slot_status);
	if (retval) {
		err("%s: Cannot check for power fault\n", __FUNCTION__);
		return retval;
	}
	pwr_fault = (u8)((slot_status & PWR_FAULT_DETECTED) >> 1);
	
	DBG_LEAVE_ROUTINE
	return pwr_fault;
}

static int hpc_get_emi_status(struct slot *slot, u8 *status)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_status;
	int retval = 0;

	DBG_ENTER_ROUTINE

	retval = pciehp_readw(ctrl, SLOTSTATUS, &slot_status);
	if (retval) {
		err("%s : Cannot check EMI status\n", __FUNCTION__);
		return retval;
	}
	*status = (slot_status & EMI_STATE) >> EMI_STATUS_BIT;

	DBG_LEAVE_ROUTINE
	return retval;
}

static int hpc_toggle_emi(struct slot *slot)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_cmd = 0;
	u16 slot_ctrl;
	int rc = 0;

	DBG_ENTER_ROUTINE

	rc = pciehp_readw(ctrl, SLOTCTRL, &slot_ctrl);
	if (rc) {
		err("%s : hp_register_read_word SLOT_CTRL failed\n",
			__FUNCTION__);
		return rc;
	}

	slot_cmd = (slot_ctrl | EMI_CTRL);
	if (!pciehp_poll_mode)
		slot_cmd = slot_cmd | HP_INTR_ENABLE;

	pcie_write_cmd(slot, slot_cmd);
	slot->last_emi_toggle = get_seconds();
	DBG_LEAVE_ROUTINE
	return rc;
}

static int hpc_set_attention_status(struct slot *slot, u8 value)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_cmd = 0;
	u16 slot_ctrl;
	int rc = 0;

	DBG_ENTER_ROUTINE

	rc = pciehp_readw(ctrl, SLOTCTRL, &slot_ctrl);
	if (rc) {
		err("%s: Cannot read SLOTCTRL register\n", __FUNCTION__);
		return rc;
	}

	switch (value) {
		case 0 :	/* turn off */
			slot_cmd = (slot_ctrl & ~ATTN_LED_CTRL) | 0x00C0;
			break;
		case 1:		/* turn on */
			slot_cmd = (slot_ctrl & ~ATTN_LED_CTRL) | 0x0040;
			break;
		case 2:		/* turn blink */
			slot_cmd = (slot_ctrl & ~ATTN_LED_CTRL) | 0x0080;
			break;
		default:
			return -1;
	}
	if (!pciehp_poll_mode)
		slot_cmd = slot_cmd | HP_INTR_ENABLE; 

	pcie_write_cmd(slot, slot_cmd);
	dbg("%s: SLOTCTRL %x write cmd %x\n",
	    __FUNCTION__, ctrl->cap_base + SLOTCTRL, slot_cmd);
	
	DBG_LEAVE_ROUTINE
	return rc;
}


static void hpc_set_green_led_on(struct slot *slot)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_cmd;
	u16 slot_ctrl;
	int rc = 0;
       	
	DBG_ENTER_ROUTINE

	rc = pciehp_readw(ctrl, SLOTCTRL, &slot_ctrl);
	if (rc) {
		err("%s: Cannot read SLOTCTRL register\n", __FUNCTION__);
		return;
	}
	slot_cmd = (slot_ctrl & ~PWR_LED_CTRL) | 0x0100;
	if (!pciehp_poll_mode)
		slot_cmd = slot_cmd | HP_INTR_ENABLE; 

	pcie_write_cmd(slot, slot_cmd);

	dbg("%s: SLOTCTRL %x write cmd %x\n",
	    __FUNCTION__, ctrl->cap_base + SLOTCTRL, slot_cmd);
	DBG_LEAVE_ROUTINE
	return;
}

static void hpc_set_green_led_off(struct slot *slot)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_cmd;
	u16 slot_ctrl;
	int rc = 0;

	DBG_ENTER_ROUTINE

	rc = pciehp_readw(ctrl, SLOTCTRL, &slot_ctrl);
	if (rc) {
		err("%s: Cannot read SLOTCTRL register\n", __FUNCTION__);
		return;
	}

	slot_cmd = (slot_ctrl & ~PWR_LED_CTRL) | 0x0300;

	if (!pciehp_poll_mode)
		slot_cmd = slot_cmd | HP_INTR_ENABLE; 
	pcie_write_cmd(slot, slot_cmd);
	dbg("%s: SLOTCTRL %x write cmd %x\n",
	    __FUNCTION__, ctrl->cap_base + SLOTCTRL, slot_cmd);

	DBG_LEAVE_ROUTINE
	return;
}

static void hpc_set_green_led_blink(struct slot *slot)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_cmd;
	u16 slot_ctrl;
	int rc = 0; 
	
	DBG_ENTER_ROUTINE

	rc = pciehp_readw(ctrl, SLOTCTRL, &slot_ctrl);
	if (rc) {
		err("%s: Cannot read SLOTCTRL register\n", __FUNCTION__);
		return;
	}

	slot_cmd = (slot_ctrl & ~PWR_LED_CTRL) | 0x0200;

	if (!pciehp_poll_mode)
		slot_cmd = slot_cmd | HP_INTR_ENABLE; 
	pcie_write_cmd(slot, slot_cmd);

	dbg("%s: SLOTCTRL %x write cmd %x\n",
	    __FUNCTION__, ctrl->cap_base + SLOTCTRL, slot_cmd);
	DBG_LEAVE_ROUTINE
	return;
}

static void hpc_release_ctlr(struct controller *ctrl)
{
	DBG_ENTER_ROUTINE 

	if (pciehp_poll_mode)
		del_timer(&ctrl->poll_timer);
	else
		free_irq(ctrl->pci_dev->irq, ctrl);

	/*
	 * If this is the last controller to be released, destroy the
	 * pciehp work queue
	 */
	if (atomic_dec_and_test(&pciehp_num_controllers))
		destroy_workqueue(pciehp_wq);

	DBG_LEAVE_ROUTINE
}

static int hpc_power_on_slot(struct slot * slot)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_cmd;
	u16 slot_ctrl, slot_status;
	int retval = 0;

	DBG_ENTER_ROUTINE 

	dbg("%s: slot->hp_slot %x\n", __FUNCTION__, slot->hp_slot);

	/* Clear sticky power-fault bit from previous power failures */
	retval = pciehp_readw(ctrl, SLOTSTATUS, &slot_status);
	if (retval) {
		err("%s: Cannot read SLOTSTATUS register\n", __FUNCTION__);
		return retval;
	}
	slot_status &= PWR_FAULT_DETECTED;
	if (slot_status) {
		retval = pciehp_writew(ctrl, SLOTSTATUS, slot_status);
		if (retval) {
			err("%s: Cannot write to SLOTSTATUS register\n",
			    __FUNCTION__);
			return retval;
		}
	}

	retval = pciehp_readw(ctrl, SLOTCTRL, &slot_ctrl);
	if (retval) {
		err("%s: Cannot read SLOTCTRL register\n", __FUNCTION__);
		return retval;
	}

	slot_cmd = (slot_ctrl & ~PWR_CTRL) | POWER_ON;

	/* Enable detection that we turned off at slot power-off time */
	if (!pciehp_poll_mode)
		slot_cmd = slot_cmd |
		           PWR_FAULT_DETECT_ENABLE |
		           MRL_DETECT_ENABLE |
		           PRSN_DETECT_ENABLE |
		           HP_INTR_ENABLE;

	retval = pcie_write_cmd(slot, slot_cmd);

	if (retval) {
		err("%s: Write %x command failed!\n", __FUNCTION__, slot_cmd);
		return -1;
	}
	dbg("%s: SLOTCTRL %x write cmd %x\n",
	    __FUNCTION__, ctrl->cap_base + SLOTCTRL, slot_cmd);

	DBG_LEAVE_ROUTINE

	return retval;
}

static int hpc_power_off_slot(struct slot * slot)
{
	struct controller *ctrl = slot->ctrl;
	u16 slot_cmd;
	u16 slot_ctrl;
	int retval = 0;

	DBG_ENTER_ROUTINE 

	dbg("%s: slot->hp_slot %x\n", __FUNCTION__, slot->hp_slot);

	retval = pciehp_readw(ctrl, SLOTCTRL, &slot_ctrl);
	if (retval) {
		err("%s: Cannot read SLOTCTRL register\n", __FUNCTION__);
		return retval;
	}

	slot_cmd = (slot_ctrl & ~PWR_CTRL) | POWER_OFF;

	/*
	 * If we get MRL or presence detect interrupts now, the isr
	 * will notice the sticky power-fault bit too and issue power
	 * indicator change commands. This will lead to an endless loop
	 * of command completions, since the power-fault bit remains on
	 * till the slot is powered on again.
	 */
	if (!pciehp_poll_mode)
		slot_cmd = (slot_cmd &
		            ~PWR_FAULT_DETECT_ENABLE &
		            ~MRL_DETECT_ENABLE &
		            ~PRSN_DETECT_ENABLE) | HP_INTR_ENABLE;

	retval = pcie_write_cmd(slot, slot_cmd);

	if (retval) {
		err("%s: Write command failed!\n", __FUNCTION__);
		return -1;
	}
	dbg("%s: SLOTCTRL %x write cmd %x\n",
	    __FUNCTION__, ctrl->cap_base + SLOTCTRL, slot_cmd);

	DBG_LEAVE_ROUTINE

	return retval;
}

static irqreturn_t pcie_isr(int irq, void *dev_id)
{
	struct controller *ctrl = (struct controller *)dev_id;
	u16 slot_status, intr_detect, intr_loc;
	u16 temp_word;
	int hp_slot = 0;	/* only 1 slot per PCI Express port */
	int rc = 0;

	rc = pciehp_readw(ctrl, SLOTSTATUS, &slot_status);
	if (rc) {
		err("%s: Cannot read SLOTSTATUS register\n", __FUNCTION__);
		return IRQ_NONE;
	}

	intr_detect = ( ATTN_BUTTN_PRESSED | PWR_FAULT_DETECTED | MRL_SENS_CHANGED |
					PRSN_DETECT_CHANGED | CMD_COMPLETED );

	intr_loc = slot_status & intr_detect;

	/* Check to see if it was our interrupt */
	if ( !intr_loc )
		return IRQ_NONE;

	dbg("%s: intr_loc %x\n", __FUNCTION__, intr_loc);
	/* Mask Hot-plug Interrupt Enable */
	if (!pciehp_poll_mode) {
		rc = pciehp_readw(ctrl, SLOTCTRL, &temp_word);
		if (rc) {
			err("%s: Cannot read SLOT_CTRL register\n",
			    __FUNCTION__);
			return IRQ_NONE;
		}

		dbg("%s: pciehp_readw(SLOTCTRL) with value %x\n",
		    __FUNCTION__, temp_word);
		temp_word = (temp_word & ~HP_INTR_ENABLE & ~CMD_CMPL_INTR_ENABLE) | 0x00;
		rc = pciehp_writew(ctrl, SLOTCTRL, temp_word);
		if (rc) {
			err("%s: Cannot write to SLOTCTRL register\n",
			    __FUNCTION__);
			return IRQ_NONE;
		}

		rc = pciehp_readw(ctrl, SLOTSTATUS, &slot_status);
		if (rc) {
			err("%s: Cannot read SLOT_STATUS register\n",
			    __FUNCTION__);
			return IRQ_NONE;
		}
		dbg("%s: pciehp_readw(SLOTSTATUS) with value %x\n",
		    __FUNCTION__, slot_status);
		
		/* Clear command complete interrupt caused by this write */
		temp_word = 0x1f;
		rc = pciehp_writew(ctrl, SLOTSTATUS, temp_word);
		if (rc) {
			err("%s: Cannot write to SLOTSTATUS register\n",
			    __FUNCTION__);
			return IRQ_NONE;
		}
	}
	
	if (intr_loc & CMD_COMPLETED) {
		/* 
		 * Command Complete Interrupt Pending 
		 */
		ctrl->cmd_busy = 0;
		wake_up_interruptible(&ctrl->queue);
	}

	if (intr_loc & MRL_SENS_CHANGED)
		pciehp_handle_switch_change(hp_slot, ctrl);

	if (intr_loc & ATTN_BUTTN_PRESSED)
		pciehp_handle_attention_button(hp_slot, ctrl);

	if (intr_loc & PRSN_DETECT_CHANGED)
		pciehp_handle_presence_change(hp_slot, ctrl);

	if (intr_loc & PWR_FAULT_DETECTED)
		pciehp_handle_power_fault(hp_slot, ctrl);

	/* Clear all events after serving them */
	temp_word = 0x1F;
	rc = pciehp_writew(ctrl, SLOTSTATUS, temp_word);
	if (rc) {
		err("%s: Cannot write to SLOTSTATUS register\n", __FUNCTION__);
		return IRQ_NONE;
	}
	/* Unmask Hot-plug Interrupt Enable */
	if (!pciehp_poll_mode) {
		rc = pciehp_readw(ctrl, SLOTCTRL, &temp_word);
		if (rc) {
			err("%s: Cannot read SLOTCTRL register\n",
			    __FUNCTION__);
			return IRQ_NONE;
		}

		dbg("%s: Unmask Hot-plug Interrupt Enable\n", __FUNCTION__);
		temp_word = (temp_word & ~HP_INTR_ENABLE) | HP_INTR_ENABLE;

		rc = pciehp_writew(ctrl, SLOTCTRL, temp_word);
		if (rc) {
			err("%s: Cannot write to SLOTCTRL register\n",
			    __FUNCTION__);
			return IRQ_NONE;
		}

		rc = pciehp_readw(ctrl, SLOTSTATUS, &slot_status);
		if (rc) {
			err("%s: Cannot read SLOT_STATUS register\n",
			    __FUNCTION__);
			return IRQ_NONE;
		}
		
		/* Clear command complete interrupt caused by this write */
		temp_word = 0x1F;
		rc = pciehp_writew(ctrl, SLOTSTATUS, temp_word);
		if (rc) {
			err("%s: Cannot write to SLOTSTATUS failed\n",
			    __FUNCTION__);
			return IRQ_NONE;
		}
		dbg("%s: pciehp_writew(SLOTSTATUS) with value %x\n",
		    __FUNCTION__, temp_word);
	}
	
	return IRQ_HANDLED;
}

static int hpc_get_max_lnk_speed (struct slot *slot, enum pci_bus_speed *value)
{
	struct controller *ctrl = slot->ctrl;
	enum pcie_link_speed lnk_speed;
	u32	lnk_cap;
	int retval = 0;

	DBG_ENTER_ROUTINE 

	retval = pciehp_readl(ctrl, LNKCAP, &lnk_cap);
	if (retval) {
		err("%s: Cannot read LNKCAP register\n", __FUNCTION__);
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
	dbg("Max link speed = %d\n", lnk_speed);
	DBG_LEAVE_ROUTINE 
	return retval;
}

static int hpc_get_max_lnk_width (struct slot *slot, enum pcie_link_width *value)
{
	struct controller *ctrl = slot->ctrl;
	enum pcie_link_width lnk_wdth;
	u32	lnk_cap;
	int retval = 0;

	DBG_ENTER_ROUTINE 

	retval = pciehp_readl(ctrl, LNKCAP, &lnk_cap);
	if (retval) {
		err("%s: Cannot read LNKCAP register\n", __FUNCTION__);
		return retval;
	}

	switch ((lnk_cap & 0x03F0) >> 4){
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
	dbg("Max link width = %d\n", lnk_wdth);
	DBG_LEAVE_ROUTINE 
	return retval;
}

static int hpc_get_cur_lnk_speed (struct slot *slot, enum pci_bus_speed *value)
{
	struct controller *ctrl = slot->ctrl;
	enum pcie_link_speed lnk_speed = PCI_SPEED_UNKNOWN;
	int retval = 0;
	u16 lnk_status;

	DBG_ENTER_ROUTINE 

	retval = pciehp_readw(ctrl, LNKSTATUS, &lnk_status);
	if (retval) {
		err("%s: Cannot read LNKSTATUS register\n", __FUNCTION__);
		return retval;
	}

	switch (lnk_status & 0x0F) {
	case 1:
		lnk_speed = PCIE_2PT5GB;
		break;
	default:
		lnk_speed = PCIE_LNK_SPEED_UNKNOWN;
		break;
	}

	*value = lnk_speed;
	dbg("Current link speed = %d\n", lnk_speed);
	DBG_LEAVE_ROUTINE 
	return retval;
}

static int hpc_get_cur_lnk_width (struct slot *slot, enum pcie_link_width *value)
{
	struct controller *ctrl = slot->ctrl;
	enum pcie_link_width lnk_wdth = PCIE_LNK_WIDTH_UNKNOWN;
	int retval = 0;
	u16 lnk_status;

	DBG_ENTER_ROUTINE 

	retval = pciehp_readw(ctrl, LNKSTATUS, &lnk_status);
	if (retval) {
		err("%s: Cannot read LNKSTATUS register\n", __FUNCTION__);
		return retval;
	}
	
	switch ((lnk_status & 0x03F0) >> 4){
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
	dbg("Current link width = %d\n", lnk_wdth);
	DBG_LEAVE_ROUTINE 
	return retval;
}

static struct hpc_ops pciehp_hpc_ops = {
	.power_on_slot			= hpc_power_on_slot,
	.power_off_slot			= hpc_power_off_slot,
	.set_attention_status		= hpc_set_attention_status,
	.get_power_status		= hpc_get_power_status,
	.get_attention_status		= hpc_get_attention_status,
	.get_latch_status		= hpc_get_latch_status,
	.get_adapter_status		= hpc_get_adapter_status,
	.get_emi_status			= hpc_get_emi_status,
	.toggle_emi			= hpc_toggle_emi,

	.get_max_bus_speed		= hpc_get_max_lnk_speed,
	.get_cur_bus_speed		= hpc_get_cur_lnk_speed,
	.get_max_lnk_width		= hpc_get_max_lnk_width,
	.get_cur_lnk_width		= hpc_get_cur_lnk_width,
	
	.query_power_fault		= hpc_query_power_fault,
	.green_led_on			= hpc_set_green_led_on,
	.green_led_off			= hpc_set_green_led_off,
	.green_led_blink		= hpc_set_green_led_blink,
	
	.release_ctlr			= hpc_release_ctlr,
	.check_lnk_status		= hpc_check_lnk_status,
};

#ifdef CONFIG_ACPI
int pciehp_acpi_get_hp_hw_control_from_firmware(struct pci_dev *dev)
{
	acpi_status status;
	acpi_handle chandle, handle = DEVICE_ACPI_HANDLE(&(dev->dev));
	struct pci_dev *pdev = dev;
	struct pci_bus *parent;
	struct acpi_buffer string = { ACPI_ALLOCATE_BUFFER, NULL };

	/*
	 * Per PCI firmware specification, we should run the ACPI _OSC
	 * method to get control of hotplug hardware before using it.
	 * If an _OSC is missing, we look for an OSHP to do the same thing.
	 * To handle different BIOS behavior, we look for _OSC and OSHP
	 * within the scope of the hotplug controller and its parents, upto
	 * the host bridge under which this controller exists.
	 */
	while (!handle) {
		/*
		 * This hotplug controller was not listed in the ACPI name
		 * space at all. Try to get acpi handle of parent pci bus.
		 */
		if (!pdev || !pdev->bus->parent)
			break;
		parent = pdev->bus->parent;
		dbg("Could not find %s in acpi namespace, trying parent\n",
				pci_name(pdev));
		if (!parent->self)
			/* Parent must be a host bridge */
			handle = acpi_get_pci_rootbridge_handle(
					pci_domain_nr(parent),
					parent->number);
		else
			handle = DEVICE_ACPI_HANDLE(
					&(parent->self->dev));
		pdev = parent->self;
	}

	while (handle) {
		acpi_get_name(handle, ACPI_FULL_PATHNAME, &string);
		dbg("Trying to get hotplug control for %s \n",
			(char *)string.pointer);
		status = pci_osc_control_set(handle,
				OSC_PCI_EXPRESS_NATIVE_HP_CONTROL);
		if (status == AE_NOT_FOUND)
			status = acpi_run_oshp(handle);
		if (ACPI_SUCCESS(status)) {
			dbg("Gained control for hotplug HW for pci %s (%s)\n",
				pci_name(dev), (char *)string.pointer);
			kfree(string.pointer);
			return 0;
		}
		if (acpi_root_bridge(handle))
			break;
		chandle = handle;
		status = acpi_get_parent(chandle, &handle);
		if (ACPI_FAILURE(status))
			break;
	}

	err("Cannot get control of hotplug hardware for pci %s\n",
			pci_name(dev));

	kfree(string.pointer);
	return -1;
}
#endif



int pcie_init(struct controller * ctrl, struct pcie_device *dev)
{
	int rc;
	u16 temp_word;
	u16 cap_reg;
	u16 intr_enable = 0;
	u32 slot_cap;
	int cap_base;
	u16 slot_status, slot_ctrl;
	struct pci_dev *pdev;

	DBG_ENTER_ROUTINE
	
	pdev = dev->port;
	ctrl->pci_dev = pdev;	/* save pci_dev in context */

	dbg("%s: hotplug controller vendor id 0x%x device id 0x%x\n",
			__FUNCTION__, pdev->vendor, pdev->device);

	if ((cap_base = pci_find_capability(pdev, PCI_CAP_ID_EXP)) == 0) {
		dbg("%s: Can't find PCI_CAP_ID_EXP (0x10)\n", __FUNCTION__);
		goto abort_free_ctlr;
	}

	ctrl->cap_base = cap_base;

	dbg("%s: pcie_cap_base %x\n", __FUNCTION__, cap_base);

	rc = pciehp_readw(ctrl, CAPREG, &cap_reg);
	if (rc) {
		err("%s: Cannot read CAPREG register\n", __FUNCTION__);
		goto abort_free_ctlr;
	}
	dbg("%s: CAPREG offset %x cap_reg %x\n",
	    __FUNCTION__, ctrl->cap_base + CAPREG, cap_reg);

	if (((cap_reg & SLOT_IMPL) == 0) || (((cap_reg & DEV_PORT_TYPE) != 0x0040)
		&& ((cap_reg & DEV_PORT_TYPE) != 0x0060))) {
		dbg("%s : This is not a root port or the port is not connected to a slot\n", __FUNCTION__);
		goto abort_free_ctlr;
	}

	rc = pciehp_readl(ctrl, SLOTCAP, &slot_cap);
	if (rc) {
		err("%s: Cannot read SLOTCAP register\n", __FUNCTION__);
		goto abort_free_ctlr;
	}
	dbg("%s: SLOTCAP offset %x slot_cap %x\n",
	    __FUNCTION__, ctrl->cap_base + SLOTCAP, slot_cap);

	if (!(slot_cap & HP_CAP)) {
		dbg("%s : This slot is not hot-plug capable\n", __FUNCTION__);
		goto abort_free_ctlr;
	}
	/* For debugging purpose */
	rc = pciehp_readw(ctrl, SLOTSTATUS, &slot_status);
	if (rc) {
		err("%s: Cannot read SLOTSTATUS register\n", __FUNCTION__);
		goto abort_free_ctlr;
	}
	dbg("%s: SLOTSTATUS offset %x slot_status %x\n",
	    __FUNCTION__, ctrl->cap_base + SLOTSTATUS, slot_status);

	rc = pciehp_readw(ctrl, SLOTCTRL, &slot_ctrl);
	if (rc) {
		err("%s: Cannot read SLOTCTRL register\n", __FUNCTION__);
		goto abort_free_ctlr;
	}
	dbg("%s: SLOTCTRL offset %x slot_ctrl %x\n",
	    __FUNCTION__, ctrl->cap_base + SLOTCTRL, slot_ctrl);

	for ( rc = 0; rc < DEVICE_COUNT_RESOURCE; rc++)
		if (pci_resource_len(pdev, rc) > 0)
			dbg("pci resource[%d] start=0x%llx(len=0x%llx)\n", rc,
			    (unsigned long long)pci_resource_start(pdev, rc),
			    (unsigned long long)pci_resource_len(pdev, rc));

	info("HPC vendor_id %x device_id %x ss_vid %x ss_did %x\n", pdev->vendor, pdev->device, 
		pdev->subsystem_vendor, pdev->subsystem_device);

	mutex_init(&ctrl->crit_sect);
	mutex_init(&ctrl->ctrl_lock);

	/* setup wait queue */
	init_waitqueue_head(&ctrl->queue);

	/* return PCI Controller Info */
	ctrl->slot_device_offset = 0;
	ctrl->num_slots = 1;
	ctrl->first_slot = slot_cap >> 19;
	ctrl->ctrlcap = slot_cap & 0x0000007f;

	/* Mask Hot-plug Interrupt Enable */
	rc = pciehp_readw(ctrl, SLOTCTRL, &temp_word);
	if (rc) {
		err("%s: Cannot read SLOTCTRL register\n", __FUNCTION__);
		goto abort_free_ctlr;
	}

	dbg("%s: SLOTCTRL %x value read %x\n",
	    __FUNCTION__, ctrl->cap_base + SLOTCTRL, temp_word);
	temp_word = (temp_word & ~HP_INTR_ENABLE & ~CMD_CMPL_INTR_ENABLE) | 0x00;

	rc = pciehp_writew(ctrl, SLOTCTRL, temp_word);
	if (rc) {
		err("%s: Cannot write to SLOTCTRL register\n", __FUNCTION__);
		goto abort_free_ctlr;
	}

	rc = pciehp_readw(ctrl, SLOTSTATUS, &slot_status);
	if (rc) {
		err("%s: Cannot read SLOTSTATUS register\n", __FUNCTION__);
		goto abort_free_ctlr;
	}

	temp_word = 0x1F; /* Clear all events */
	rc = pciehp_writew(ctrl, SLOTSTATUS, temp_word);
	if (rc) {
		err("%s: Cannot write to SLOTSTATUS register\n", __FUNCTION__);
		goto abort_free_ctlr;
	}

	if (pciehp_poll_mode) {
		/* Install interrupt polling timer. Start with 10 sec delay */
		init_timer(&ctrl->poll_timer);
		start_int_poll_timer(ctrl, 10);
	} else {
		/* Installs the interrupt handler */
		rc = request_irq(ctrl->pci_dev->irq, pcie_isr, IRQF_SHARED,
				 MY_NAME, (void *)ctrl);
		dbg("%s: request_irq %d for hpc%d (returns %d)\n",
		    __FUNCTION__, ctrl->pci_dev->irq,
		    atomic_read(&pciehp_num_controllers), rc);
		if (rc) {
			err("Can't get irq %d for the hotplug controller\n",
			    ctrl->pci_dev->irq);
			goto abort_free_ctlr;
		}
	}
	dbg("pciehp ctrl b:d:f:irq=0x%x:%x:%x:%x\n", pdev->bus->number,
		PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn), dev->irq);

	/*
	 * If this is the first controller to be initialized,
	 * initialize the pciehp work queue
	 */
	if (atomic_add_return(1, &pciehp_num_controllers) == 1) {
		pciehp_wq = create_singlethread_workqueue("pciehpd");
		if (!pciehp_wq) {
			rc = -ENOMEM;
			goto abort_free_irq;
		}
	}

	rc = pciehp_readw(ctrl, SLOTCTRL, &temp_word);
	if (rc) {
		err("%s: Cannot read SLOTCTRL register\n", __FUNCTION__);
		goto abort_free_irq;
	}

	intr_enable = intr_enable | PRSN_DETECT_ENABLE;

	if (ATTN_BUTTN(slot_cap))
		intr_enable = intr_enable | ATTN_BUTTN_ENABLE;
	
	if (POWER_CTRL(slot_cap))
		intr_enable = intr_enable | PWR_FAULT_DETECT_ENABLE;
	
	if (MRL_SENS(slot_cap))
		intr_enable = intr_enable | MRL_DETECT_ENABLE;

	temp_word = (temp_word & ~intr_enable) | intr_enable; 

	if (pciehp_poll_mode) {
		temp_word = (temp_word & ~HP_INTR_ENABLE) | 0x0;
	} else {
		temp_word = (temp_word & ~HP_INTR_ENABLE) | HP_INTR_ENABLE;
	}

	/* Unmask Hot-plug Interrupt Enable for the interrupt notification mechanism case */
	rc = pciehp_writew(ctrl, SLOTCTRL, temp_word);
	if (rc) {
		err("%s: Cannot write to SLOTCTRL register\n", __FUNCTION__);
		goto abort_free_irq;
	}
	rc = pciehp_readw(ctrl, SLOTSTATUS, &slot_status);
	if (rc) {
		err("%s: Cannot read SLOTSTATUS register\n", __FUNCTION__);
		goto abort_disable_intr;
	}
	
	temp_word =  0x1F; /* Clear all events */
	rc = pciehp_writew(ctrl, SLOTSTATUS, temp_word);
	if (rc) {
		err("%s: Cannot write to SLOTSTATUS register\n", __FUNCTION__);
		goto abort_disable_intr;
	}
	
	if (pciehp_force) {
		dbg("Bypassing BIOS check for pciehp use on %s\n",
				pci_name(ctrl->pci_dev));
	} else {
		rc = pciehp_get_hp_hw_control_from_firmware(ctrl->pci_dev);
		if (rc)
			goto abort_disable_intr;
	}

	ctrl->hpc_ops = &pciehp_hpc_ops;

	DBG_LEAVE_ROUTINE
	return 0;

	/* We end up here for the many possible ways to fail this API.  */
abort_disable_intr:
	rc = pciehp_readw(ctrl, SLOTCTRL, &temp_word);
	if (!rc) {
		temp_word &= ~(intr_enable | HP_INTR_ENABLE);
		rc = pciehp_writew(ctrl, SLOTCTRL, temp_word);
	}
	if (rc)
		err("%s : disabling interrupts failed\n", __FUNCTION__);

abort_free_irq:
	if (pciehp_poll_mode)
		del_timer_sync(&ctrl->poll_timer);
	else
		free_irq(ctrl->pci_dev->irq, ctrl);

abort_free_ctlr:
	DBG_LEAVE_ROUTINE
	return -1;
}
