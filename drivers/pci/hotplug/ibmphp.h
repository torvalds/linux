#ifndef __IBMPHP_H
#define __IBMPHP_H

/*
 * IBM Hot Plug Controller Driver
 *
 * Written By: Jyoti Shah, Tong Yu, Irene Zubarev, IBM Corporation
 *
 * Copyright (C) 2001 Greg Kroah-Hartman (greg@kroah.com)
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
 *
 */

#include <linux/pci_hotplug.h>

extern int ibmphp_debug;

#if !defined(MODULE)
	#define MY_NAME "ibmphpd"
#else
	#define MY_NAME THIS_MODULE->name
#endif
#define debug(fmt, arg...) do { if (ibmphp_debug == 1) printk(KERN_DEBUG "%s: " fmt, MY_NAME, ## arg); } while (0)
#define debug_pci(fmt, arg...) do { if (ibmphp_debug) printk(KERN_DEBUG "%s: " fmt, MY_NAME, ## arg); } while (0)
#define err(format, arg...) printk(KERN_ERR "%s: " format, MY_NAME, ## arg)
#define info(format, arg...) printk(KERN_INFO "%s: " format, MY_NAME, ## arg)
#define warn(format, arg...) printk(KERN_WARNING "%s: " format, MY_NAME, ## arg)


/* EBDA stuff */

/***********************************************************
* SLOT CAPABILITY                                          *
***********************************************************/

#define EBDA_SLOT_133_MAX		0x20
#define EBDA_SLOT_100_MAX		0x10
#define EBDA_SLOT_66_MAX		0x02
#define EBDA_SLOT_PCIX_CAP		0x08


/************************************************************
*  RESOURCE TYPE                                             *
************************************************************/

#define EBDA_RSRC_TYPE_MASK		0x03
#define EBDA_IO_RSRC_TYPE		0x00
#define EBDA_MEM_RSRC_TYPE		0x01
#define EBDA_PFM_RSRC_TYPE		0x03
#define EBDA_RES_RSRC_TYPE		0x02


/*************************************************************
*  IO RESTRICTION TYPE                                       *
*************************************************************/

#define EBDA_IO_RESTRI_MASK		0x0c
#define EBDA_NO_RESTRI			0x00
#define EBDA_AVO_VGA_ADDR		0x04
#define EBDA_AVO_VGA_ADDR_AND_ALIA	0x08
#define EBDA_AVO_ISA_ADDR		0x0c


/**************************************************************
*  DEVICE TYPE DEF                                            *
**************************************************************/

#define EBDA_DEV_TYPE_MASK		0x10
#define EBDA_PCI_DEV			0x10
#define EBDA_NON_PCI_DEV		0x00


/***************************************************************
*  PRIMARY DEF DEFINITION                                      *
***************************************************************/

#define EBDA_PRI_DEF_MASK		0x20
#define EBDA_PRI_PCI_BUS_INFO		0x20
#define EBDA_NORM_DEV_RSRC_INFO		0x00


//--------------------------------------------------------------
// RIO TABLE DATA STRUCTURE
//--------------------------------------------------------------

struct rio_table_hdr {
	u8 ver_num;
	u8 scal_count;
	u8 riodev_count;
	u16 offset;
};

//-------------------------------------------------------------
// SCALABILITY DETAIL
//-------------------------------------------------------------

struct scal_detail {
	u8 node_id;
	u32 cbar;
	u8 port0_node_connect;
	u8 port0_port_connect;
	u8 port1_node_connect;
	u8 port1_port_connect;
	u8 port2_node_connect;
	u8 port2_port_connect;
	u8 chassis_num;
//	struct list_head scal_detail_list;
};

//--------------------------------------------------------------
// RIO DETAIL
//--------------------------------------------------------------

struct rio_detail {
	u8 rio_node_id;
	u32 bbar;
	u8 rio_type;
	u8 owner_id;
	u8 port0_node_connect;
	u8 port0_port_connect;
	u8 port1_node_connect;
	u8 port1_port_connect;
	u8 first_slot_num;
	u8 status;
	u8 wpindex;
	u8 chassis_num;
	struct list_head rio_detail_list;
};

struct opt_rio {
	u8 rio_type;
	u8 chassis_num;
	u8 first_slot_num;
	u8 middle_num;
	struct list_head opt_rio_list;
};

struct opt_rio_lo {
	u8 rio_type;
	u8 chassis_num;
	u8 first_slot_num;
	u8 middle_num;
	u8 pack_count;
	struct list_head opt_rio_lo_list;
};

/****************************************************************
*  HPC DESCRIPTOR NODE                                          *
****************************************************************/

struct ebda_hpc_list {
	u8 format;
	u16 num_ctlrs;
	short phys_addr;
//      struct list_head ebda_hpc_list;
};
/*****************************************************************
*   IN HPC DATA STRUCTURE, THE ASSOCIATED SLOT AND BUS           *
*   STRUCTURE                                                    *
*****************************************************************/

struct ebda_hpc_slot {
	u8 slot_num;
	u32 slot_bus_num;
	u8 ctl_index;
	u8 slot_cap;
};

struct ebda_hpc_bus {
	u32 bus_num;
	u8 slots_at_33_conv;
	u8 slots_at_66_conv;
	u8 slots_at_66_pcix;
	u8 slots_at_100_pcix;
	u8 slots_at_133_pcix;
};


/********************************************************************
*   THREE TYPE OF HOT PLUG CONTROLLER                                *
********************************************************************/

struct isa_ctlr_access {
	u16 io_start;
	u16 io_end;
};

struct pci_ctlr_access {
	u8 bus;
	u8 dev_fun;
};

struct wpeg_i2c_ctlr_access {
	ulong wpegbbar;
	u8 i2c_addr;
};

#define HPC_DEVICE_ID		0x0246
#define HPC_SUBSYSTEM_ID	0x0247
#define HPC_PCI_OFFSET		0x40
/*************************************************************************
*   RSTC DESCRIPTOR NODE                                                 *
*************************************************************************/

struct ebda_rsrc_list {
	u8 format;
	u16 num_entries;
	u16 phys_addr;
	struct ebda_rsrc_list *next;
};


/***************************************************************************
*   PCI RSRC NODE                                                          *
***************************************************************************/

struct ebda_pci_rsrc {
	u8 rsrc_type;
	u8 bus_num;
	u8 dev_fun;
	u32 start_addr;
	u32 end_addr;
	u8 marked;	/* for NVRAM */
	struct list_head ebda_pci_rsrc_list;
};


/***********************************************************
* BUS_INFO DATE STRUCTURE                                  *
***********************************************************/

struct bus_info {
	u8 slot_min;
	u8 slot_max;
	u8 slot_count;
	u8 busno;
	u8 controller_id;
	u8 current_speed;
	u8 current_bus_mode;
	u8 index;
	u8 slots_at_33_conv;
	u8 slots_at_66_conv;
	u8 slots_at_66_pcix;
	u8 slots_at_100_pcix;
	u8 slots_at_133_pcix;
	struct list_head bus_info_list;
};


/***********************************************************
* GLOBAL VARIABLES                                         *
***********************************************************/
extern struct list_head ibmphp_ebda_pci_rsrc_head;
extern struct list_head ibmphp_slot_head;
/***********************************************************
* FUNCTION PROTOTYPES                                      *
***********************************************************/

void ibmphp_free_ebda_hpc_queue(void);
int ibmphp_access_ebda(void);
struct slot *ibmphp_get_slot_from_physical_num(u8);
int ibmphp_get_total_hp_slots(void);
void ibmphp_free_ibm_slot(struct slot *);
void ibmphp_free_bus_info_queue(void);
void ibmphp_free_ebda_pci_rsrc_queue(void);
struct bus_info *ibmphp_find_same_bus_num(u32);
int ibmphp_get_bus_index(u8);
u16 ibmphp_get_total_controllers(void);
int ibmphp_register_pci(void);

/* passed parameters */
#define MEM		0
#define IO		1
#define PFMEM		2

/* bit masks */
#define RESTYPE		0x03
#define IOMASK		0x00	/* will need to take its complement */
#define MMASK		0x01
#define PFMASK		0x03
#define PCIDEVMASK	0x10	/* we should always have PCI devices */
#define PRIMARYBUSMASK	0x20

/* pci specific defines */
#define PCI_VENDOR_ID_NOTVALID		0xFFFF
#define PCI_HEADER_TYPE_MULTIDEVICE	0x80
#define PCI_HEADER_TYPE_MULTIBRIDGE	0x81

#define LATENCY		0x64
#define CACHE		64
#define DEVICEENABLE	0x015F		/* CPQ has 0x0157 */

#define IOBRIDGE	0x1000		/* 4k */
#define MEMBRIDGE	0x100000	/* 1M */

/* irqs */
#define SCSI_IRQ	0x09
#define LAN_IRQ		0x0A
#define OTHER_IRQ	0x0B

/* Data Structures */

/* type is of the form x x xx xx
 *                     | |  |  |_ 00 - I/O, 01 - Memory, 11 - PFMemory
 *                     | |  - 00 - No Restrictions, 01 - Avoid VGA, 10 - Avoid
 *                     | |    VGA and their aliases, 11 - Avoid ISA
 *                     | - 1 - PCI device, 0 - non pci device
 *                     - 1 - Primary PCI Bus Information (0 if Normal device)
 * the IO restrictions [2:3] are only for primary buses
 */


/* we need this struct because there could be several resource blocks
 * allocated per primary bus in the EBDA
 */
struct range_node {
	int rangeno;
	u32 start;
	u32 end;
	struct range_node *next;
};

struct bus_node {
	u8 busno;
	int noIORanges;
	struct range_node *rangeIO;
	int noMemRanges;
	struct range_node *rangeMem;
	int noPFMemRanges;
	struct range_node *rangePFMem;
	int needIOUpdate;
	int needMemUpdate;
	int needPFMemUpdate;
	struct resource_node *firstIO;	/* first IO resource on the Bus */
	struct resource_node *firstMem;	/* first memory resource on the Bus */
	struct resource_node *firstPFMem;	/* first prefetchable memory resource on the Bus */
	struct resource_node *firstPFMemFromMem;	/* when run out of pfmem available, taking from Mem */
	struct list_head bus_list;
};

struct resource_node {
	int rangeno;
	u8 busno;
	u8 devfunc;
	u32 start;
	u32 end;
	u32 len;
	int type;		/* MEM, IO, PFMEM */
	u8 fromMem;		/* this is to indicate that the range is from
				 * from the Memory bucket rather than from PFMem */
	struct resource_node *next;
	struct resource_node *nextRange;	/* for the other mem range on bus */
};

struct res_needed {
	u32 mem;
	u32 pfmem;
	u32 io;
	u8 not_correct;		/* needed for return */
	int devices[32];	/* for device numbers behind this bridge */
};

/* functions */

int ibmphp_rsrc_init(void);
int ibmphp_add_resource(struct resource_node *);
int ibmphp_remove_resource(struct resource_node *);
int ibmphp_find_resource(struct bus_node *, u32, struct resource_node **, int);
int ibmphp_check_resource(struct resource_node *, u8);
int ibmphp_remove_bus(struct bus_node *, u8);
void ibmphp_free_resources(void);
int ibmphp_add_pfmem_from_mem(struct resource_node *);
struct bus_node *ibmphp_find_res_bus(u8);
void ibmphp_print_test(void);	/* for debugging purposes */

void ibmphp_hpc_initvars(void);
int ibmphp_hpc_readslot(struct slot *, u8, u8 *);
int ibmphp_hpc_writeslot(struct slot *, u8);
void ibmphp_lock_operations(void);
void ibmphp_unlock_operations(void);
int ibmphp_hpc_start_poll_thread(void);
void ibmphp_hpc_stop_poll_thread(void);

//----------------------------------------------------------------------------


//----------------------------------------------------------------------------
// HPC return codes
//----------------------------------------------------------------------------
#define HPC_ERROR			0xFF

//-----------------------------------------------------------------------------
// BUS INFO
//-----------------------------------------------------------------------------
#define BUS_SPEED			0x30
#define BUS_MODE			0x40
#define BUS_MODE_PCIX			0x01
#define BUS_MODE_PCI			0x00
#define BUS_SPEED_2			0x20
#define BUS_SPEED_1			0x10
#define BUS_SPEED_33			0x00
#define BUS_SPEED_66			0x01
#define BUS_SPEED_100			0x02
#define BUS_SPEED_133			0x03
#define BUS_SPEED_66PCIX		0x04
#define BUS_SPEED_66UNKNOWN		0x05
#define BUS_STATUS_AVAILABLE		0x01
#define BUS_CONTROL_AVAILABLE		0x02
#define SLOT_LATCH_REGS_SUPPORTED	0x10

#define PRGM_MODEL_REV_LEVEL		0xF0
#define MAX_ADAPTER_NONE		0x09

//----------------------------------------------------------------------------
// HPC 'write' operations/commands
//----------------------------------------------------------------------------
//	Command			Code	State	Write to reg
//					Machine	at index
//-------------------------	----	-------	------------
#define HPC_CTLR_ENABLEIRQ	0x00	// N	15
#define HPC_CTLR_DISABLEIRQ	0x01	// N	15
#define HPC_SLOT_OFF		0x02	// Y	0-14
#define HPC_SLOT_ON		0x03	// Y	0-14
#define HPC_SLOT_ATTNOFF	0x04	// N	0-14
#define HPC_SLOT_ATTNON		0x05	// N	0-14
#define HPC_CTLR_CLEARIRQ	0x06	// N	15
#define HPC_CTLR_RESET		0x07	// Y	15
#define HPC_CTLR_IRQSTEER	0x08	// N	15
#define HPC_BUS_33CONVMODE	0x09	// Y	31-34
#define HPC_BUS_66CONVMODE	0x0A	// Y	31-34
#define HPC_BUS_66PCIXMODE	0x0B	// Y	31-34
#define HPC_BUS_100PCIXMODE	0x0C	// Y	31-34
#define HPC_BUS_133PCIXMODE	0x0D	// Y	31-34
#define HPC_ALLSLOT_OFF		0x11	// Y	15
#define HPC_ALLSLOT_ON		0x12	// Y	15
#define HPC_SLOT_BLINKLED	0x13	// N	0-14

//----------------------------------------------------------------------------
// read commands
//----------------------------------------------------------------------------
#define READ_SLOTSTATUS		0x01
#define READ_EXTSLOTSTATUS	0x02
#define READ_BUSSTATUS		0x03
#define READ_CTLRSTATUS		0x04
#define READ_ALLSTAT		0x05
#define READ_ALLSLOT		0x06
#define READ_SLOTLATCHLOWREG	0x07
#define READ_REVLEVEL		0x08
#define READ_HPCOPTIONS		0x09
//----------------------------------------------------------------------------
// slot status
//----------------------------------------------------------------------------
#define HPC_SLOT_POWER		0x01
#define HPC_SLOT_CONNECT	0x02
#define HPC_SLOT_ATTN		0x04
#define HPC_SLOT_PRSNT2		0x08
#define HPC_SLOT_PRSNT1		0x10
#define HPC_SLOT_PWRGD		0x20
#define HPC_SLOT_BUS_SPEED	0x40
#define HPC_SLOT_LATCH		0x80

//----------------------------------------------------------------------------
// HPC_SLOT_POWER status return codes
//----------------------------------------------------------------------------
#define HPC_SLOT_POWER_OFF	0x00
#define HPC_SLOT_POWER_ON	0x01

//----------------------------------------------------------------------------
// HPC_SLOT_CONNECT status return codes
//----------------------------------------------------------------------------
#define HPC_SLOT_CONNECTED	0x00
#define HPC_SLOT_DISCONNECTED	0x01

//----------------------------------------------------------------------------
// HPC_SLOT_ATTN status return codes
//----------------------------------------------------------------------------
#define HPC_SLOT_ATTN_OFF	0x00
#define HPC_SLOT_ATTN_ON	0x01
#define HPC_SLOT_ATTN_BLINK	0x02

//----------------------------------------------------------------------------
// HPC_SLOT_PRSNT status return codes
//----------------------------------------------------------------------------
#define HPC_SLOT_EMPTY		0x00
#define HPC_SLOT_PRSNT_7	0x01
#define HPC_SLOT_PRSNT_15	0x02
#define HPC_SLOT_PRSNT_25	0x03

//----------------------------------------------------------------------------
// HPC_SLOT_PWRGD status return codes
//----------------------------------------------------------------------------
#define HPC_SLOT_PWRGD_FAULT_NONE	0x00
#define HPC_SLOT_PWRGD_GOOD		0x01

//----------------------------------------------------------------------------
// HPC_SLOT_BUS_SPEED status return codes
//----------------------------------------------------------------------------
#define HPC_SLOT_BUS_SPEED_OK	0x00
#define HPC_SLOT_BUS_SPEED_MISM	0x01

//----------------------------------------------------------------------------
// HPC_SLOT_LATCH status return codes
//----------------------------------------------------------------------------
#define HPC_SLOT_LATCH_OPEN	0x01	// NOTE : in PCI spec bit off = open
#define HPC_SLOT_LATCH_CLOSED	0x00	// NOTE : in PCI spec bit on  = closed


//----------------------------------------------------------------------------
// extended slot status
//----------------------------------------------------------------------------
#define HPC_SLOT_PCIX		0x01
#define HPC_SLOT_SPEED1		0x02
#define HPC_SLOT_SPEED2		0x04
#define HPC_SLOT_BLINK_ATTN	0x08
#define HPC_SLOT_RSRVD1		0x10
#define HPC_SLOT_RSRVD2		0x20
#define HPC_SLOT_BUS_MODE	0x40
#define HPC_SLOT_RSRVD3		0x80

//----------------------------------------------------------------------------
// HPC_XSLOT_PCIX_CAP status return codes
//----------------------------------------------------------------------------
#define HPC_SLOT_PCIX_NO	0x00
#define HPC_SLOT_PCIX_YES	0x01

//----------------------------------------------------------------------------
// HPC_XSLOT_SPEED status return codes
//----------------------------------------------------------------------------
#define HPC_SLOT_SPEED_33	0x00
#define HPC_SLOT_SPEED_66	0x01
#define HPC_SLOT_SPEED_133	0x02

//----------------------------------------------------------------------------
// HPC_XSLOT_ATTN_BLINK status return codes
//----------------------------------------------------------------------------
#define HPC_SLOT_ATTN_BLINK_OFF	0x00
#define HPC_SLOT_ATTN_BLINK_ON	0x01

//----------------------------------------------------------------------------
// HPC_XSLOT_BUS_MODE status return codes
//----------------------------------------------------------------------------
#define HPC_SLOT_BUS_MODE_OK	0x00
#define HPC_SLOT_BUS_MODE_MISM	0x01

//----------------------------------------------------------------------------
// Controller status
//----------------------------------------------------------------------------
#define HPC_CTLR_WORKING	0x01
#define HPC_CTLR_FINISHED	0x02
#define HPC_CTLR_RESULT0	0x04
#define HPC_CTLR_RESULT1	0x08
#define HPC_CTLR_RESULE2	0x10
#define HPC_CTLR_RESULT3	0x20
#define HPC_CTLR_IRQ_ROUTG	0x40
#define HPC_CTLR_IRQ_PENDG	0x80

//----------------------------------------------------------------------------
// HPC_CTLR_WORKING status return codes
//----------------------------------------------------------------------------
#define HPC_CTLR_WORKING_NO	0x00
#define HPC_CTLR_WORKING_YES	0x01

//----------------------------------------------------------------------------
// HPC_CTLR_FINISHED status return codes
//----------------------------------------------------------------------------
#define HPC_CTLR_FINISHED_NO	0x00
#define HPC_CTLR_FINISHED_YES	0x01

//----------------------------------------------------------------------------
// HPC_CTLR_RESULT status return codes
//----------------------------------------------------------------------------
#define HPC_CTLR_RESULT_SUCCESS	0x00
#define HPC_CTLR_RESULT_FAILED	0x01
#define HPC_CTLR_RESULT_RSVD	0x02
#define HPC_CTLR_RESULT_NORESP	0x03


//----------------------------------------------------------------------------
// macro for slot info
//----------------------------------------------------------------------------
#define SLOT_POWER(s)	((u8) ((s & HPC_SLOT_POWER) \
	? HPC_SLOT_POWER_ON : HPC_SLOT_POWER_OFF))

#define SLOT_CONNECT(s)	((u8) ((s & HPC_SLOT_CONNECT) \
	? HPC_SLOT_DISCONNECTED : HPC_SLOT_CONNECTED))

#define SLOT_ATTN(s, es)	((u8) ((es & HPC_SLOT_BLINK_ATTN) \
	? HPC_SLOT_ATTN_BLINK \
	: ((s & HPC_SLOT_ATTN) ? HPC_SLOT_ATTN_ON : HPC_SLOT_ATTN_OFF)))

#define SLOT_PRESENT(s)	((u8) ((s & HPC_SLOT_PRSNT1) \
	? ((s & HPC_SLOT_PRSNT2) ? HPC_SLOT_EMPTY : HPC_SLOT_PRSNT_15) \
	: ((s & HPC_SLOT_PRSNT2) ? HPC_SLOT_PRSNT_25 : HPC_SLOT_PRSNT_7)))

#define SLOT_PWRGD(s)	((u8) ((s & HPC_SLOT_PWRGD) \
	? HPC_SLOT_PWRGD_GOOD : HPC_SLOT_PWRGD_FAULT_NONE))

#define SLOT_BUS_SPEED(s)	((u8) ((s & HPC_SLOT_BUS_SPEED) \
	? HPC_SLOT_BUS_SPEED_MISM : HPC_SLOT_BUS_SPEED_OK))

#define SLOT_LATCH(s)	((u8) ((s & HPC_SLOT_LATCH) \
	? HPC_SLOT_LATCH_CLOSED : HPC_SLOT_LATCH_OPEN))

#define SLOT_PCIX(es)	((u8) ((es & HPC_SLOT_PCIX) \
	? HPC_SLOT_PCIX_YES : HPC_SLOT_PCIX_NO))

#define SLOT_SPEED(es)	((u8) ((es & HPC_SLOT_SPEED2) \
	? ((es & HPC_SLOT_SPEED1) ? HPC_SLOT_SPEED_133   \
				: HPC_SLOT_SPEED_66)   \
	: HPC_SLOT_SPEED_33))

#define SLOT_BUS_MODE(es)	((u8) ((es & HPC_SLOT_BUS_MODE) \
	? HPC_SLOT_BUS_MODE_MISM : HPC_SLOT_BUS_MODE_OK))

//--------------------------------------------------------------------------
// macro for bus info
//---------------------------------------------------------------------------
#define CURRENT_BUS_SPEED(s)	((u8) (s & BUS_SPEED_2) \
	? ((s & BUS_SPEED_1) ? BUS_SPEED_133 : BUS_SPEED_100) \
	: ((s & BUS_SPEED_1) ? BUS_SPEED_66 : BUS_SPEED_33))

#define CURRENT_BUS_MODE(s)	((u8) (s & BUS_MODE) ? BUS_MODE_PCIX : BUS_MODE_PCI)

#define READ_BUS_STATUS(s)	((u8) (s->options & BUS_STATUS_AVAILABLE))

#define READ_BUS_MODE(s)	((s->revision & PRGM_MODEL_REV_LEVEL) >= 0x20)

#define SET_BUS_STATUS(s)	((u8) (s->options & BUS_CONTROL_AVAILABLE))

#define READ_SLOT_LATCH(s)	((u8) (s->options & SLOT_LATCH_REGS_SUPPORTED))

//----------------------------------------------------------------------------
// macro for controller info
//----------------------------------------------------------------------------
#define CTLR_WORKING(c) ((u8) ((c & HPC_CTLR_WORKING) \
	? HPC_CTLR_WORKING_YES : HPC_CTLR_WORKING_NO))
#define CTLR_FINISHED(c) ((u8) ((c & HPC_CTLR_FINISHED) \
	? HPC_CTLR_FINISHED_YES : HPC_CTLR_FINISHED_NO))
#define CTLR_RESULT(c) ((u8) ((c & HPC_CTLR_RESULT1)  \
	? ((c & HPC_CTLR_RESULT0) ? HPC_CTLR_RESULT_NORESP \
				: HPC_CTLR_RESULT_RSVD)  \
	: ((c & HPC_CTLR_RESULT0) ? HPC_CTLR_RESULT_FAILED \
				: HPC_CTLR_RESULT_SUCCESS)))

// command that affect the state machine of HPC
#define NEEDTOCHECK_CMDSTATUS(c) ((c == HPC_SLOT_OFF)        || \
				  (c == HPC_SLOT_ON)         || \
				  (c == HPC_CTLR_RESET)      || \
				  (c == HPC_BUS_33CONVMODE)  || \
				  (c == HPC_BUS_66CONVMODE)  || \
				  (c == HPC_BUS_66PCIXMODE)  || \
				  (c == HPC_BUS_100PCIXMODE) || \
				  (c == HPC_BUS_133PCIXMODE) || \
				  (c == HPC_ALLSLOT_OFF)     || \
				  (c == HPC_ALLSLOT_ON))


/* Core part of the driver */

#define ENABLE		1
#define DISABLE		0

#define CARD_INFO	0x07
#define PCIX133		0x07
#define PCIX66		0x05
#define PCI66		0x04

extern struct pci_bus *ibmphp_pci_bus;

/* Variables */

struct pci_func {
	struct pci_dev *dev;	/* from the OS */
	u8 busno;
	u8 device;
	u8 function;
	struct resource_node *io[6];
	struct resource_node *mem[6];
	struct resource_node *pfmem[6];
	struct pci_func *next;
	int devices[32];	/* for bridge config */
	u8 irq[4];		/* for interrupt config */
	u8 bus;			/* flag for unconfiguring, to say if PPB */
};

struct slot {
	u8 bus;
	u8 device;
	u8 number;
	u8 real_physical_slot_num;
	u32 capabilities;
	u8 supported_speed;
	u8 supported_bus_mode;
	u8 flag;		/* this is for disable slot and polling */
	u8 ctlr_index;
	struct hotplug_slot *hotplug_slot;
	struct controller *ctrl;
	struct pci_func *func;
	u8 irq[4];
	int bit_mode;		/* 0 = 32, 1 = 64 */
	struct bus_info *bus_on;
	struct list_head ibm_slot_list;
	u8 status;
	u8 ext_status;
	u8 busstatus;
};

struct controller {
	struct ebda_hpc_slot *slots;
	struct ebda_hpc_bus *buses;
	struct pci_dev *ctrl_dev; /* in case where controller is PCI */
	u8 starting_slot_num;	/* starting and ending slot #'s this ctrl controls*/
	u8 ending_slot_num;
	u8 revision;
	u8 options;		/* which options HPC supports */
	u8 status;
	u8 ctlr_id;
	u8 slot_count;
	u8 bus_count;
	u8 ctlr_relative_id;
	u32 irq;
	union {
		struct isa_ctlr_access isa_ctlr;
		struct pci_ctlr_access pci_ctlr;
		struct wpeg_i2c_ctlr_access wpeg_ctlr;
	} u;
	u8 ctlr_type;
	struct list_head ebda_hpc_list;
};

/* Functions */

int ibmphp_init_devno(struct slot **);	/* This function is called from EBDA, so we need it not be static */
int ibmphp_do_disable_slot(struct slot *slot_cur);
int ibmphp_update_slot_info(struct slot *);	/* This function is called from HPC, so we need it to not be be static */
int ibmphp_configure_card(struct pci_func *, u8);
int ibmphp_unconfigure_card(struct slot **, int);
extern struct hotplug_slot_ops ibmphp_hotplug_slot_ops;

#endif				//__IBMPHP_H

