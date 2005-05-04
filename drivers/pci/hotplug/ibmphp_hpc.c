/*
 * IBM Hot Plug Controller Driver
 *
 * Written By: Jyoti Shah, IBM Corporation
 *
 * Copyright (C) 2001-2003 IBM Corp.
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
 * Send feedback to <gregkh@us.ibm.com>
 *                  <jshah@us.ibm.com>
 *
 */

#include <linux/wait.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include "ibmphp.h"

static int to_debug = FALSE;
#define debug_polling(fmt, arg...)	do { if (to_debug) debug (fmt, arg); } while (0)

//----------------------------------------------------------------------------
// timeout values
//----------------------------------------------------------------------------
#define CMD_COMPLETE_TOUT_SEC	60	// give HPC 60 sec to finish cmd
#define HPC_CTLR_WORKING_TOUT	60	// give HPC 60 sec to finish cmd
#define HPC_GETACCESS_TIMEOUT	60	// seconds
#define POLL_INTERVAL_SEC	2	// poll HPC every 2 seconds
#define POLL_LATCH_CNT		5	// poll latch 5 times, then poll slots

//----------------------------------------------------------------------------
// Winnipeg Architected Register Offsets
//----------------------------------------------------------------------------
#define WPG_I2CMBUFL_OFFSET	0x08	// I2C Message Buffer Low
#define WPG_I2CMOSUP_OFFSET	0x10	// I2C Master Operation Setup Reg
#define WPG_I2CMCNTL_OFFSET	0x20	// I2C Master Control Register
#define WPG_I2CPARM_OFFSET	0x40	// I2C Parameter Register
#define WPG_I2CSTAT_OFFSET	0x70	// I2C Status Register

//----------------------------------------------------------------------------
// Winnipeg Store Type commands (Add this commands to the register offset)
//----------------------------------------------------------------------------
#define WPG_I2C_AND		0x1000	// I2C AND operation
#define WPG_I2C_OR		0x2000	// I2C OR operation

//----------------------------------------------------------------------------
// Command set for I2C Master Operation Setup Register
//----------------------------------------------------------------------------
#define WPG_READATADDR_MASK	0x00010000	// read,bytes,I2C shifted,index
#define WPG_WRITEATADDR_MASK	0x40010000	// write,bytes,I2C shifted,index
#define WPG_READDIRECT_MASK	0x10010000
#define WPG_WRITEDIRECT_MASK	0x60010000


//----------------------------------------------------------------------------
// bit masks for I2C Master Control Register
//----------------------------------------------------------------------------
#define WPG_I2CMCNTL_STARTOP_MASK	0x00000002	// Start the Operation

//----------------------------------------------------------------------------
//
//----------------------------------------------------------------------------
#define WPG_I2C_IOREMAP_SIZE	0x2044	// size of linear address interval

//----------------------------------------------------------------------------
// command index
//----------------------------------------------------------------------------
#define WPG_1ST_SLOT_INDEX	0x01	// index - 1st slot for ctlr
#define WPG_CTLR_INDEX		0x0F	// index - ctlr
#define WPG_1ST_EXTSLOT_INDEX	0x10	// index - 1st ext slot for ctlr
#define WPG_1ST_BUS_INDEX	0x1F	// index - 1st bus for ctlr

//----------------------------------------------------------------------------
// macro utilities
//----------------------------------------------------------------------------
// if bits 20,22,25,26,27,29,30 are OFF return TRUE
#define HPC_I2CSTATUS_CHECK(s)	((u8)((s & 0x00000A76) ? FALSE : TRUE))

//----------------------------------------------------------------------------
// global variables
//----------------------------------------------------------------------------
static int ibmphp_shutdown;
static int tid_poll;
static struct semaphore sem_hpcaccess;	// lock access to HPC
static struct semaphore semOperations;	// lock all operations and
					// access to data structures
static struct semaphore sem_exit;	// make sure polling thread goes away
//----------------------------------------------------------------------------
// local function prototypes
//----------------------------------------------------------------------------
static u8 i2c_ctrl_read (struct controller *, void __iomem *, u8);
static u8 i2c_ctrl_write (struct controller *, void __iomem *, u8, u8);
static u8 hpc_writecmdtoindex (u8, u8);
static u8 hpc_readcmdtoindex (u8, u8);
static void get_hpc_access (void);
static void free_hpc_access (void);
static void poll_hpc (void);
static int process_changeinstatus (struct slot *, struct slot *);
static int process_changeinlatch (u8, u8, struct controller *);
static int hpc_poll_thread (void *);
static int hpc_wait_ctlr_notworking (int, struct controller *, void __iomem *, u8 *);
//----------------------------------------------------------------------------


/*----------------------------------------------------------------------
* Name:    ibmphp_hpc_initvars
*
* Action:  initialize semaphores and variables
*---------------------------------------------------------------------*/
void __init ibmphp_hpc_initvars (void)
{
	debug ("%s - Entry\n", __FUNCTION__);

	init_MUTEX (&sem_hpcaccess);
	init_MUTEX (&semOperations);
	init_MUTEX_LOCKED (&sem_exit);
	to_debug = FALSE;
	ibmphp_shutdown = FALSE;
	tid_poll = 0;

	debug ("%s - Exit\n", __FUNCTION__);
}

/*----------------------------------------------------------------------
* Name:    i2c_ctrl_read
*
* Action:  read from HPC over I2C
*
*---------------------------------------------------------------------*/
static u8 i2c_ctrl_read (struct controller *ctlr_ptr, void __iomem *WPGBbar, u8 index)
{
	u8 status;
	int i;
	void __iomem *wpg_addr;	// base addr + offset
	unsigned long wpg_data;	// data to/from WPG LOHI format
	unsigned long ultemp;
	unsigned long data;	// actual data HILO format

	debug_polling ("%s - Entry WPGBbar[%p] index[%x] \n", __FUNCTION__, WPGBbar, index);

	//--------------------------------------------------------------------
	// READ - step 1
	// read at address, byte length, I2C address (shifted), index
	// or read direct, byte length, index
	if (ctlr_ptr->ctlr_type == 0x02) {
		data = WPG_READATADDR_MASK;
		// fill in I2C address
		ultemp = (unsigned long)ctlr_ptr->u.wpeg_ctlr.i2c_addr;
		ultemp = ultemp >> 1;
		data |= (ultemp << 8);

		// fill in index
		data |= (unsigned long)index;
	} else if (ctlr_ptr->ctlr_type == 0x04) {
		data = WPG_READDIRECT_MASK;

		// fill in index
		ultemp = (unsigned long)index;
		ultemp = ultemp << 8;
		data |= ultemp;
	} else {
		err ("this controller type is not supported \n");
		return HPC_ERROR;
	}

	wpg_data = swab32 (data);	// swap data before writing
	wpg_addr = WPGBbar + WPG_I2CMOSUP_OFFSET;
	writel (wpg_data, wpg_addr);

	//--------------------------------------------------------------------
	// READ - step 2 : clear the message buffer
	data = 0x00000000;
	wpg_data = swab32 (data);
	wpg_addr = WPGBbar + WPG_I2CMBUFL_OFFSET;
	writel (wpg_data, wpg_addr);

	//--------------------------------------------------------------------
	// READ - step 3 : issue start operation, I2C master control bit 30:ON
	//                 2020 : [20] OR operation at [20] offset 0x20
	data = WPG_I2CMCNTL_STARTOP_MASK;
	wpg_data = swab32 (data);
	wpg_addr = WPGBbar + WPG_I2CMCNTL_OFFSET + WPG_I2C_OR;
	writel (wpg_data, wpg_addr);

	//--------------------------------------------------------------------
	// READ - step 4 : wait until start operation bit clears
	i = CMD_COMPLETE_TOUT_SEC;
	while (i) {
		msleep(10);
		wpg_addr = WPGBbar + WPG_I2CMCNTL_OFFSET;
		wpg_data = readl (wpg_addr);
		data = swab32 (wpg_data);
		if (!(data & WPG_I2CMCNTL_STARTOP_MASK))
			break;
		i--;
	}
	if (i == 0) {
		debug ("%s - Error : WPG timeout\n", __FUNCTION__);
		return HPC_ERROR;
	}
	//--------------------------------------------------------------------
	// READ - step 5 : read I2C status register
	i = CMD_COMPLETE_TOUT_SEC;
	while (i) {
		msleep(10);
		wpg_addr = WPGBbar + WPG_I2CSTAT_OFFSET;
		wpg_data = readl (wpg_addr);
		data = swab32 (wpg_data);
		if (HPC_I2CSTATUS_CHECK (data))
			break;
		i--;
	}
	if (i == 0) {
		debug ("ctrl_read - Exit Error:I2C timeout\n");
		return HPC_ERROR;
	}

	//--------------------------------------------------------------------
	// READ - step 6 : get DATA
	wpg_addr = WPGBbar + WPG_I2CMBUFL_OFFSET;
	wpg_data = readl (wpg_addr);
	data = swab32 (wpg_data);

	status = (u8) data;

	debug_polling ("%s - Exit index[%x] status[%x]\n", __FUNCTION__, index, status);

	return (status);
}

/*----------------------------------------------------------------------
* Name:    i2c_ctrl_write
*
* Action:  write to HPC over I2C
*
* Return   0 or error codes
*---------------------------------------------------------------------*/
static u8 i2c_ctrl_write (struct controller *ctlr_ptr, void __iomem *WPGBbar, u8 index, u8 cmd)
{
	u8 rc;
	void __iomem *wpg_addr;	// base addr + offset
	unsigned long wpg_data;	// data to/from WPG LOHI format 
	unsigned long ultemp;
	unsigned long data;	// actual data HILO format
	int i;

	debug_polling ("%s - Entry WPGBbar[%p] index[%x] cmd[%x]\n", __FUNCTION__, WPGBbar, index, cmd);

	rc = 0;
	//--------------------------------------------------------------------
	// WRITE - step 1
	// write at address, byte length, I2C address (shifted), index
	// or write direct, byte length, index
	data = 0x00000000;

	if (ctlr_ptr->ctlr_type == 0x02) {
		data = WPG_WRITEATADDR_MASK;
		// fill in I2C address
		ultemp = (unsigned long)ctlr_ptr->u.wpeg_ctlr.i2c_addr;
		ultemp = ultemp >> 1;
		data |= (ultemp << 8);

		// fill in index
		data |= (unsigned long)index;
	} else if (ctlr_ptr->ctlr_type == 0x04) {
		data = WPG_WRITEDIRECT_MASK;

		// fill in index
		ultemp = (unsigned long)index;
		ultemp = ultemp << 8;
		data |= ultemp;
	} else {
		err ("this controller type is not supported \n");
		return HPC_ERROR;
	}

	wpg_data = swab32 (data);	// swap data before writing
	wpg_addr = WPGBbar + WPG_I2CMOSUP_OFFSET;
	writel (wpg_data, wpg_addr);

	//--------------------------------------------------------------------
	// WRITE - step 2 : clear the message buffer
	data = 0x00000000 | (unsigned long)cmd;
	wpg_data = swab32 (data);
	wpg_addr = WPGBbar + WPG_I2CMBUFL_OFFSET;
	writel (wpg_data, wpg_addr);

	//--------------------------------------------------------------------
	// WRITE - step 3 : issue start operation,I2C master control bit 30:ON
	//                 2020 : [20] OR operation at [20] offset 0x20
	data = WPG_I2CMCNTL_STARTOP_MASK;
	wpg_data = swab32 (data);
	wpg_addr = WPGBbar + WPG_I2CMCNTL_OFFSET + WPG_I2C_OR;
	writel (wpg_data, wpg_addr);

	//--------------------------------------------------------------------
	// WRITE - step 4 : wait until start operation bit clears
	i = CMD_COMPLETE_TOUT_SEC;
	while (i) {
		msleep(10);
		wpg_addr = WPGBbar + WPG_I2CMCNTL_OFFSET;
		wpg_data = readl (wpg_addr);
		data = swab32 (wpg_data);
		if (!(data & WPG_I2CMCNTL_STARTOP_MASK))
			break;
		i--;
	}
	if (i == 0) {
		debug ("%s - Exit Error:WPG timeout\n", __FUNCTION__);
		rc = HPC_ERROR;
	}

	//--------------------------------------------------------------------
	// WRITE - step 5 : read I2C status register
	i = CMD_COMPLETE_TOUT_SEC;
	while (i) {
		msleep(10);
		wpg_addr = WPGBbar + WPG_I2CSTAT_OFFSET;
		wpg_data = readl (wpg_addr);
		data = swab32 (wpg_data);
		if (HPC_I2CSTATUS_CHECK (data))
			break;
		i--;
	}
	if (i == 0) {
		debug ("ctrl_read - Error : I2C timeout\n");
		rc = HPC_ERROR;
	}

	debug_polling ("%s Exit rc[%x]\n", __FUNCTION__, rc);
	return (rc);
}

//------------------------------------------------------------
//  Read from ISA type HPC 
//------------------------------------------------------------
static u8 isa_ctrl_read (struct controller *ctlr_ptr, u8 offset)
{
	u16 start_address;
	u16 end_address;
	u8 data;

	start_address = ctlr_ptr->u.isa_ctlr.io_start;
	end_address = ctlr_ptr->u.isa_ctlr.io_end;
	data = inb (start_address + offset);
	return data;
}

//--------------------------------------------------------------
// Write to ISA type HPC
//--------------------------------------------------------------
static void isa_ctrl_write (struct controller *ctlr_ptr, u8 offset, u8 data)
{
	u16 start_address;
	u16 port_address;
	
	start_address = ctlr_ptr->u.isa_ctlr.io_start;
	port_address = start_address + (u16) offset;
	outb (data, port_address);
}

static u8 pci_ctrl_read (struct controller *ctrl, u8 offset)
{
	u8 data = 0x00;
	debug ("inside pci_ctrl_read\n");
	if (ctrl->ctrl_dev)
		pci_read_config_byte (ctrl->ctrl_dev, HPC_PCI_OFFSET + offset, &data);
	return data;
}

static u8 pci_ctrl_write (struct controller *ctrl, u8 offset, u8 data)
{
	u8 rc = -ENODEV;
	debug ("inside pci_ctrl_write\n");
	if (ctrl->ctrl_dev) {
		pci_write_config_byte (ctrl->ctrl_dev, HPC_PCI_OFFSET + offset, data);
		rc = 0;
	}
	return rc;
}

static u8 ctrl_read (struct controller *ctlr, void __iomem *base, u8 offset)
{
	u8 rc;
	switch (ctlr->ctlr_type) {
	case 0:
		rc = isa_ctrl_read (ctlr, offset);
		break;
	case 1:
		rc = pci_ctrl_read (ctlr, offset);
		break;
	case 2:
	case 4:
		rc = i2c_ctrl_read (ctlr, base, offset);
		break;
	default:
		return -ENODEV;
	}
	return rc;
}

static u8 ctrl_write (struct controller *ctlr, void __iomem *base, u8 offset, u8 data)
{
	u8 rc = 0;
	switch (ctlr->ctlr_type) {
	case 0:
		isa_ctrl_write(ctlr, offset, data);
		break;
	case 1:
		rc = pci_ctrl_write (ctlr, offset, data);
		break;
	case 2:
	case 4:
		rc = i2c_ctrl_write(ctlr, base, offset, data);
		break;
	default:
		return -ENODEV;
	}
	return rc;
}
/*----------------------------------------------------------------------
* Name:    hpc_writecmdtoindex()
*
* Action:  convert a write command to proper index within a controller
*
* Return   index, HPC_ERROR
*---------------------------------------------------------------------*/
static u8 hpc_writecmdtoindex (u8 cmd, u8 index)
{
	u8 rc;

	switch (cmd) {
	case HPC_CTLR_ENABLEIRQ:	// 0x00.N.15
	case HPC_CTLR_CLEARIRQ:	// 0x06.N.15
	case HPC_CTLR_RESET:	// 0x07.N.15
	case HPC_CTLR_IRQSTEER:	// 0x08.N.15
	case HPC_CTLR_DISABLEIRQ:	// 0x01.N.15
	case HPC_ALLSLOT_ON:	// 0x11.N.15
	case HPC_ALLSLOT_OFF:	// 0x12.N.15
		rc = 0x0F;
		break;

	case HPC_SLOT_OFF:	// 0x02.Y.0-14
	case HPC_SLOT_ON:	// 0x03.Y.0-14
	case HPC_SLOT_ATTNOFF:	// 0x04.N.0-14
	case HPC_SLOT_ATTNON:	// 0x05.N.0-14
	case HPC_SLOT_BLINKLED:	// 0x13.N.0-14
		rc = index;
		break;

	case HPC_BUS_33CONVMODE:
	case HPC_BUS_66CONVMODE:
	case HPC_BUS_66PCIXMODE:
	case HPC_BUS_100PCIXMODE:
	case HPC_BUS_133PCIXMODE:
		rc = index + WPG_1ST_BUS_INDEX - 1;
		break;

	default:
		err ("hpc_writecmdtoindex - Error invalid cmd[%x]\n", cmd);
		rc = HPC_ERROR;
	}

	return rc;
}

/*----------------------------------------------------------------------
* Name:    hpc_readcmdtoindex()
*
* Action:  convert a read command to proper index within a controller
*
* Return   index, HPC_ERROR
*---------------------------------------------------------------------*/
static u8 hpc_readcmdtoindex (u8 cmd, u8 index)
{
	u8 rc;

	switch (cmd) {
	case READ_CTLRSTATUS:
		rc = 0x0F;
		break;
	case READ_SLOTSTATUS:
	case READ_ALLSTAT:
		rc = index;
		break;
	case READ_EXTSLOTSTATUS:
		rc = index + WPG_1ST_EXTSLOT_INDEX;
		break;
	case READ_BUSSTATUS:
		rc = index + WPG_1ST_BUS_INDEX - 1;
		break;
	case READ_SLOTLATCHLOWREG:
		rc = 0x28;
		break;
	case READ_REVLEVEL:
		rc = 0x25;
		break;
	case READ_HPCOPTIONS:
		rc = 0x27;
		break;
	default:
		rc = HPC_ERROR;
	}
	return rc;
}

/*----------------------------------------------------------------------
* Name:    HPCreadslot()
*
* Action:  issue a READ command to HPC
*
* Input:   pslot   - can not be NULL for READ_ALLSTAT
*          pstatus - can be NULL for READ_ALLSTAT
*
* Return   0 or error codes
*---------------------------------------------------------------------*/
int ibmphp_hpc_readslot (struct slot * pslot, u8 cmd, u8 * pstatus)
{
	void __iomem *wpg_bbar = NULL;
	struct controller *ctlr_ptr;
	struct list_head *pslotlist;
	u8 index, status;
	int rc = 0;
	int busindex;

	debug_polling ("%s - Entry pslot[%p] cmd[%x] pstatus[%p]\n", __FUNCTION__, pslot, cmd, pstatus);

	if ((pslot == NULL)
	    || ((pstatus == NULL) && (cmd != READ_ALLSTAT) && (cmd != READ_BUSSTATUS))) {
		rc = -EINVAL;
		err ("%s - Error invalid pointer, rc[%d]\n", __FUNCTION__, rc);
		return rc;
	}

	if (cmd == READ_BUSSTATUS) {
		busindex = ibmphp_get_bus_index (pslot->bus);
		if (busindex < 0) {
			rc = -EINVAL;
			err ("%s - Exit Error:invalid bus, rc[%d]\n", __FUNCTION__, rc);
			return rc;
		} else
			index = (u8) busindex;
	} else
		index = pslot->ctlr_index;

	index = hpc_readcmdtoindex (cmd, index);

	if (index == HPC_ERROR) {
		rc = -EINVAL;
		err ("%s - Exit Error:invalid index, rc[%d]\n", __FUNCTION__, rc);
		return rc;
	}

	ctlr_ptr = pslot->ctrl;

	get_hpc_access ();

	//--------------------------------------------------------------------
	// map physical address to logical address
	//--------------------------------------------------------------------
	if ((ctlr_ptr->ctlr_type == 2) || (ctlr_ptr->ctlr_type == 4))
		wpg_bbar = ioremap (ctlr_ptr->u.wpeg_ctlr.wpegbbar, WPG_I2C_IOREMAP_SIZE);

	//--------------------------------------------------------------------
	// check controller status before reading
	//--------------------------------------------------------------------
	rc = hpc_wait_ctlr_notworking (HPC_CTLR_WORKING_TOUT, ctlr_ptr, wpg_bbar, &status);
	if (!rc) {
		switch (cmd) {
		case READ_ALLSTAT:
			// update the slot structure
			pslot->ctrl->status = status;
			pslot->status = ctrl_read (ctlr_ptr, wpg_bbar, index);
			rc = hpc_wait_ctlr_notworking (HPC_CTLR_WORKING_TOUT, ctlr_ptr, wpg_bbar,
						       &status);
			if (!rc)
				pslot->ext_status = ctrl_read (ctlr_ptr, wpg_bbar, index + WPG_1ST_EXTSLOT_INDEX);

			break;

		case READ_SLOTSTATUS:
			// DO NOT update the slot structure
			*pstatus = ctrl_read (ctlr_ptr, wpg_bbar, index);
			break;

		case READ_EXTSLOTSTATUS:
			// DO NOT update the slot structure
			*pstatus = ctrl_read (ctlr_ptr, wpg_bbar, index);
			break;

		case READ_CTLRSTATUS:
			// DO NOT update the slot structure
			*pstatus = status;
			break;

		case READ_BUSSTATUS:
			pslot->busstatus = ctrl_read (ctlr_ptr, wpg_bbar, index);
			break;
		case READ_REVLEVEL:
			*pstatus = ctrl_read (ctlr_ptr, wpg_bbar, index);
			break;
		case READ_HPCOPTIONS:
			*pstatus = ctrl_read (ctlr_ptr, wpg_bbar, index);
			break;
		case READ_SLOTLATCHLOWREG:
			// DO NOT update the slot structure
			*pstatus = ctrl_read (ctlr_ptr, wpg_bbar, index);
			break;

			// Not used
		case READ_ALLSLOT:
			list_for_each (pslotlist, &ibmphp_slot_head) {
				pslot = list_entry (pslotlist, struct slot, ibm_slot_list);
				index = pslot->ctlr_index;
				rc = hpc_wait_ctlr_notworking (HPC_CTLR_WORKING_TOUT, ctlr_ptr,
								wpg_bbar, &status);
				if (!rc) {
					pslot->status = ctrl_read (ctlr_ptr, wpg_bbar, index);
					rc = hpc_wait_ctlr_notworking (HPC_CTLR_WORKING_TOUT,
									ctlr_ptr, wpg_bbar, &status);
					if (!rc)
						pslot->ext_status =
						    ctrl_read (ctlr_ptr, wpg_bbar,
								index + WPG_1ST_EXTSLOT_INDEX);
				} else {
					err ("%s - Error ctrl_read failed\n", __FUNCTION__);
					rc = -EINVAL;
					break;
				}
			}
			break;
		default:
			rc = -EINVAL;
			break;
		}
	}
	//--------------------------------------------------------------------
	// cleanup
	//--------------------------------------------------------------------
	
	// remove physical to logical address mapping
	if ((ctlr_ptr->ctlr_type == 2) || (ctlr_ptr->ctlr_type == 4))
		iounmap (wpg_bbar);
	
	free_hpc_access ();

	debug_polling ("%s - Exit rc[%d]\n", __FUNCTION__, rc);
	return rc;
}

/*----------------------------------------------------------------------
* Name:    ibmphp_hpc_writeslot()
*
* Action: issue a WRITE command to HPC
*---------------------------------------------------------------------*/
int ibmphp_hpc_writeslot (struct slot * pslot, u8 cmd)
{
	void __iomem *wpg_bbar = NULL;
	struct controller *ctlr_ptr;
	u8 index, status;
	int busindex;
	u8 done;
	int rc = 0;
	int timeout;

	debug_polling ("%s - Entry pslot[%p] cmd[%x]\n", __FUNCTION__, pslot, cmd);
	if (pslot == NULL) {
		rc = -EINVAL;
		err ("%s - Error Exit rc[%d]\n", __FUNCTION__, rc);
		return rc;
	}

	if ((cmd == HPC_BUS_33CONVMODE) || (cmd == HPC_BUS_66CONVMODE) ||
		(cmd == HPC_BUS_66PCIXMODE) || (cmd == HPC_BUS_100PCIXMODE) ||
		(cmd == HPC_BUS_133PCIXMODE)) {
		busindex = ibmphp_get_bus_index (pslot->bus);
		if (busindex < 0) {
			rc = -EINVAL;
			err ("%s - Exit Error:invalid bus, rc[%d]\n", __FUNCTION__, rc);
			return rc;
		} else
			index = (u8) busindex;
	} else
		index = pslot->ctlr_index;

	index = hpc_writecmdtoindex (cmd, index);

	if (index == HPC_ERROR) {
		rc = -EINVAL;
		err ("%s - Error Exit rc[%d]\n", __FUNCTION__, rc);
		return rc;
	}

	ctlr_ptr = pslot->ctrl;

	get_hpc_access ();

	//--------------------------------------------------------------------
	// map physical address to logical address
	//--------------------------------------------------------------------
	if ((ctlr_ptr->ctlr_type == 2) || (ctlr_ptr->ctlr_type == 4)) {
		wpg_bbar = ioremap (ctlr_ptr->u.wpeg_ctlr.wpegbbar, WPG_I2C_IOREMAP_SIZE);

		debug ("%s - ctlr id[%x] physical[%lx] logical[%lx] i2c[%x]\n", __FUNCTION__,
		ctlr_ptr->ctlr_id, (ulong) (ctlr_ptr->u.wpeg_ctlr.wpegbbar), (ulong) wpg_bbar,
		ctlr_ptr->u.wpeg_ctlr.i2c_addr);
	}
	//--------------------------------------------------------------------
	// check controller status before writing
	//--------------------------------------------------------------------
	rc = hpc_wait_ctlr_notworking (HPC_CTLR_WORKING_TOUT, ctlr_ptr, wpg_bbar, &status);
	if (!rc) {

		ctrl_write (ctlr_ptr, wpg_bbar, index, cmd);

		//--------------------------------------------------------------------
		// check controller is still not working on the command
		//--------------------------------------------------------------------
		timeout = CMD_COMPLETE_TOUT_SEC;
		done = FALSE;
		while (!done) {
			rc = hpc_wait_ctlr_notworking (HPC_CTLR_WORKING_TOUT, ctlr_ptr, wpg_bbar,
							&status);
			if (!rc) {
				if (NEEDTOCHECK_CMDSTATUS (cmd)) {
					if (CTLR_FINISHED (status) == HPC_CTLR_FINISHED_YES)
						done = TRUE;
				} else
					done = TRUE;
			}
			if (!done) {
				msleep(1000);
				if (timeout < 1) {
					done = TRUE;
					err ("%s - Error command complete timeout\n", __FUNCTION__);
					rc = -EFAULT;
				} else
					timeout--;
			}
		}
		ctlr_ptr->status = status;
	}
	// cleanup

	// remove physical to logical address mapping
	if ((ctlr_ptr->ctlr_type == 2) || (ctlr_ptr->ctlr_type == 4))
		iounmap (wpg_bbar);
	free_hpc_access ();

	debug_polling ("%s - Exit rc[%d]\n", __FUNCTION__, rc);
	return rc;
}

/*----------------------------------------------------------------------
* Name:    get_hpc_access()
*
* Action: make sure only one process can access HPC at one time
*---------------------------------------------------------------------*/
static void get_hpc_access (void)
{
	down (&sem_hpcaccess);
}

/*----------------------------------------------------------------------
* Name:    free_hpc_access()
*---------------------------------------------------------------------*/
void free_hpc_access (void)
{
	up (&sem_hpcaccess);
}

/*----------------------------------------------------------------------
* Name:    ibmphp_lock_operations()
*
* Action: make sure only one process can change the data structure
*---------------------------------------------------------------------*/
void ibmphp_lock_operations (void)
{
	down (&semOperations);
	to_debug = TRUE;
}

/*----------------------------------------------------------------------
* Name:    ibmphp_unlock_operations()
*---------------------------------------------------------------------*/
void ibmphp_unlock_operations (void)
{
	debug ("%s - Entry\n", __FUNCTION__);
	up (&semOperations);
	to_debug = FALSE;
	debug ("%s - Exit\n", __FUNCTION__);
}

/*----------------------------------------------------------------------
* Name:    poll_hpc()
*---------------------------------------------------------------------*/
#define POLL_LATCH_REGISTER	0
#define POLL_SLOTS		1
#define POLL_SLEEP		2
static void poll_hpc (void)
{
	struct slot myslot;
	struct slot *pslot = NULL;
	struct list_head *pslotlist;
	int rc;
	int poll_state = POLL_LATCH_REGISTER;
	u8 oldlatchlow = 0x00;
	u8 curlatchlow = 0x00;
	int poll_count = 0;
	u8 ctrl_count = 0x00;

	debug ("%s - Entry\n", __FUNCTION__);

	while (!ibmphp_shutdown) {
		if (ibmphp_shutdown) 
			break;
		
		/* try to get the lock to do some kind of hardware access */
		down (&semOperations);

		switch (poll_state) {
		case POLL_LATCH_REGISTER: 
			oldlatchlow = curlatchlow;
			ctrl_count = 0x00;
			list_for_each (pslotlist, &ibmphp_slot_head) {
				if (ctrl_count >= ibmphp_get_total_controllers())
					break;
				pslot = list_entry (pslotlist, struct slot, ibm_slot_list);
				if (pslot->ctrl->ctlr_relative_id == ctrl_count) {
					ctrl_count++;
					if (READ_SLOT_LATCH (pslot->ctrl)) {
						rc = ibmphp_hpc_readslot (pslot,
									  READ_SLOTLATCHLOWREG,
									  &curlatchlow);
						if (oldlatchlow != curlatchlow)
							process_changeinlatch (oldlatchlow,
									       curlatchlow,
									       pslot->ctrl);
					}
				}
			}
			++poll_count;
			poll_state = POLL_SLEEP;
			break;
		case POLL_SLOTS:
			list_for_each (pslotlist, &ibmphp_slot_head) {
				pslot = list_entry (pslotlist, struct slot, ibm_slot_list);
				// make a copy of the old status
				memcpy ((void *) &myslot, (void *) pslot,
					sizeof (struct slot));
				rc = ibmphp_hpc_readslot (pslot, READ_ALLSTAT, NULL);
				if ((myslot.status != pslot->status)
				    || (myslot.ext_status != pslot->ext_status))
					process_changeinstatus (pslot, &myslot);
			}
			ctrl_count = 0x00;
			list_for_each (pslotlist, &ibmphp_slot_head) {
				if (ctrl_count >= ibmphp_get_total_controllers())
					break;
				pslot = list_entry (pslotlist, struct slot, ibm_slot_list);
				if (pslot->ctrl->ctlr_relative_id == ctrl_count) {
					ctrl_count++;
					if (READ_SLOT_LATCH (pslot->ctrl))
						rc = ibmphp_hpc_readslot (pslot,
									  READ_SLOTLATCHLOWREG,
									  &curlatchlow);
				}
			}
			++poll_count;
			poll_state = POLL_SLEEP;
			break;
		case POLL_SLEEP:
			/* don't sleep with a lock on the hardware */
			up (&semOperations);
			msleep(POLL_INTERVAL_SEC * 1000);

			if (ibmphp_shutdown) 
				break;
			
			down (&semOperations);
			
			if (poll_count >= POLL_LATCH_CNT) {
				poll_count = 0;
				poll_state = POLL_SLOTS;
			} else
				poll_state = POLL_LATCH_REGISTER;
			break;
		}	
		/* give up the hardware semaphore */
		up (&semOperations);
		/* sleep for a short time just for good measure */
		msleep(100);
	}
	up (&sem_exit);
	debug ("%s - Exit\n", __FUNCTION__);
}


/*----------------------------------------------------------------------
* Name:    process_changeinstatus
*
* Action:  compare old and new slot status, process the change in status
*
* Input:   pointer to slot struct, old slot struct
*
* Return   0 or error codes
* Value:
*
* Side
* Effects: None.
*
* Notes:
*---------------------------------------------------------------------*/
static int process_changeinstatus (struct slot *pslot, struct slot *poldslot)
{
	u8 status;
	int rc = 0;
	u8 disable = FALSE;
	u8 update = FALSE;

	debug ("process_changeinstatus - Entry pslot[%p], poldslot[%p]\n", pslot, poldslot);

	// bit 0 - HPC_SLOT_POWER
	if ((pslot->status & 0x01) != (poldslot->status & 0x01))
		update = TRUE;

	// bit 1 - HPC_SLOT_CONNECT
	// ignore

	// bit 2 - HPC_SLOT_ATTN
	if ((pslot->status & 0x04) != (poldslot->status & 0x04))
		update = TRUE;

	// bit 3 - HPC_SLOT_PRSNT2
	// bit 4 - HPC_SLOT_PRSNT1
	if (((pslot->status & 0x08) != (poldslot->status & 0x08))
		|| ((pslot->status & 0x10) != (poldslot->status & 0x10)))
		update = TRUE;

	// bit 5 - HPC_SLOT_PWRGD
	if ((pslot->status & 0x20) != (poldslot->status & 0x20))
		// OFF -> ON: ignore, ON -> OFF: disable slot
		if ((poldslot->status & 0x20) && (SLOT_CONNECT (poldslot->status) == HPC_SLOT_CONNECTED) && (SLOT_PRESENT (poldslot->status))) 
			disable = TRUE;

	// bit 6 - HPC_SLOT_BUS_SPEED
	// ignore

	// bit 7 - HPC_SLOT_LATCH
	if ((pslot->status & 0x80) != (poldslot->status & 0x80)) {
		update = TRUE;
		// OPEN -> CLOSE
		if (pslot->status & 0x80) {
			if (SLOT_PWRGD (pslot->status)) {
				// power goes on and off after closing latch
				// check again to make sure power is still ON
				msleep(1000);
				rc = ibmphp_hpc_readslot (pslot, READ_SLOTSTATUS, &status);
				if (SLOT_PWRGD (status))
					update = TRUE;
				else	// overwrite power in pslot to OFF
					pslot->status &= ~HPC_SLOT_POWER;
			}
		}
		// CLOSE -> OPEN 
		else if ((SLOT_PWRGD (poldslot->status) == HPC_SLOT_PWRGD_GOOD)
			&& (SLOT_CONNECT (poldslot->status) == HPC_SLOT_CONNECTED) && (SLOT_PRESENT (poldslot->status))) {
			disable = TRUE;
		}
		// else - ignore
	}
	// bit 4 - HPC_SLOT_BLINK_ATTN
	if ((pslot->ext_status & 0x08) != (poldslot->ext_status & 0x08))
		update = TRUE;

	if (disable) {
		debug ("process_changeinstatus - disable slot\n");
		pslot->flag = FALSE;
		rc = ibmphp_do_disable_slot (pslot);
	}

	if (update || disable) {
		ibmphp_update_slot_info (pslot);
	}

	debug ("%s - Exit rc[%d] disable[%x] update[%x]\n", __FUNCTION__, rc, disable, update);

	return rc;
}

/*----------------------------------------------------------------------
* Name:    process_changeinlatch
*
* Action:  compare old and new latch reg status, process the change
*
* Input:   old and current latch register status
*
* Return   0 or error codes
* Value:
*---------------------------------------------------------------------*/
static int process_changeinlatch (u8 old, u8 new, struct controller *ctrl)
{
	struct slot myslot, *pslot;
	u8 i;
	u8 mask;
	int rc = 0;

	debug ("%s - Entry old[%x], new[%x]\n", __FUNCTION__, old, new);
	// bit 0 reserved, 0 is LSB, check bit 1-6 for 6 slots

	for (i = ctrl->starting_slot_num; i <= ctrl->ending_slot_num; i++) {
		mask = 0x01 << i;
		if ((mask & old) != (mask & new)) {
			pslot = ibmphp_get_slot_from_physical_num (i);
			if (pslot) {
				memcpy ((void *) &myslot, (void *) pslot, sizeof (struct slot));
				rc = ibmphp_hpc_readslot (pslot, READ_ALLSTAT, NULL);
				debug ("%s - call process_changeinstatus for slot[%d]\n", __FUNCTION__, i);
				process_changeinstatus (pslot, &myslot);
			} else {
				rc = -EINVAL;
				err ("%s - Error bad pointer for slot[%d]\n", __FUNCTION__, i);
			}
		}
	}
	debug ("%s - Exit rc[%d]\n", __FUNCTION__, rc);
	return rc;
}

/*----------------------------------------------------------------------
* Name:    hpc_poll_thread
*
* Action:  polling
*
* Return   0
* Value:
*---------------------------------------------------------------------*/
static int hpc_poll_thread (void *data)
{
	debug ("%s - Entry\n", __FUNCTION__);

	daemonize("hpc_poll");
	allow_signal(SIGKILL);

	poll_hpc ();

	tid_poll = 0;
	debug ("%s - Exit\n", __FUNCTION__);
	return 0;
}


/*----------------------------------------------------------------------
* Name:    ibmphp_hpc_start_poll_thread
*
* Action:  start polling thread
*---------------------------------------------------------------------*/
int __init ibmphp_hpc_start_poll_thread (void)
{
	int rc = 0;

	debug ("%s - Entry\n", __FUNCTION__);

	tid_poll = kernel_thread (hpc_poll_thread, NULL, 0);
	if (tid_poll < 0) {
		err ("%s - Error, thread not started\n", __FUNCTION__);
		rc = -1;
	}

	debug ("%s - Exit tid_poll[%d] rc[%d]\n", __FUNCTION__, tid_poll, rc);
	return rc;
}

/*----------------------------------------------------------------------
* Name:    ibmphp_hpc_stop_poll_thread
*
* Action:  stop polling thread and cleanup
*---------------------------------------------------------------------*/
void __exit ibmphp_hpc_stop_poll_thread (void)
{
	debug ("%s - Entry\n", __FUNCTION__);

	ibmphp_shutdown = TRUE;
	debug ("before locking operations \n");
	ibmphp_lock_operations ();
	debug ("after locking operations \n");
	
	// wait for poll thread to exit
	debug ("before sem_exit down \n");
	down (&sem_exit);
	debug ("after sem_exit down \n");

	// cleanup
	debug ("before free_hpc_access \n");
	free_hpc_access ();
	debug ("after free_hpc_access \n");
	ibmphp_unlock_operations ();
	debug ("after unlock operations \n");
	up (&sem_exit);
	debug ("after sem exit up\n");

	debug ("%s - Exit\n", __FUNCTION__);
}

/*----------------------------------------------------------------------
* Name:    hpc_wait_ctlr_notworking
*
* Action:  wait until the controller is in a not working state
*
* Return   0, HPC_ERROR
* Value:
*---------------------------------------------------------------------*/
static int hpc_wait_ctlr_notworking (int timeout, struct controller *ctlr_ptr, void __iomem *wpg_bbar,
				    u8 * pstatus)
{
	int rc = 0;
	u8 done = FALSE;

	debug_polling ("hpc_wait_ctlr_notworking - Entry timeout[%d]\n", timeout);

	while (!done) {
		*pstatus = ctrl_read (ctlr_ptr, wpg_bbar, WPG_CTLR_INDEX);
		if (*pstatus == HPC_ERROR) {
			rc = HPC_ERROR;
			done = TRUE;
		}
		if (CTLR_WORKING (*pstatus) == HPC_CTLR_WORKING_NO)
			done = TRUE;
		if (!done) {
			msleep(1000);
			if (timeout < 1) {
				done = TRUE;
				err ("HPCreadslot - Error ctlr timeout\n");
				rc = HPC_ERROR;
			} else
				timeout--;
		}
	}
	debug_polling ("hpc_wait_ctlr_notworking - Exit rc[%x] status[%x]\n", rc, *pstatus);
	return rc;
}
