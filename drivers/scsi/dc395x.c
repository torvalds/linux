/*
 * dc395x.c
 *
 * Device Driver for Tekram DC395(U/UW/F), DC315(U)
 * PCI SCSI Bus Master Host Adapter
 * (SCSI chip set used Tekram ASIC TRM-S1040)
 *
 * Authors:
 *  C.L. Huang <ching@tekram.com.tw>
 *  Erich Chen <erich@tekram.com.tw>
 *  (C) Copyright 1995-1999 Tekram Technology Co., Ltd.
 *
 *  Kurt Garloff <garloff@suse.de>
 *  (C) 1999-2000 Kurt Garloff
 *
 *  Oliver Neukum <oliver@neukum.name>
 *  Ali Akcaagac <aliakc@web.de>
 *  Jamie Lenehan <lenehan@twibble.org>
 *  (C) 2003
 *
 * License: GNU GPL
 *
 *************************************************************************
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ************************************************************************
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/ctype.h>
#include <linux/blkdev.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <asm/io.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport_spi.h>

#include "dc395x.h"

#define DC395X_NAME	"dc395x"
#define DC395X_BANNER	"Tekram DC395(U/UW/F), DC315(U) - ASIC TRM-S1040"
#define DC395X_VERSION	"v2.05, 2004/03/08"

/*---------------------------------------------------------------------------
                                  Features
 ---------------------------------------------------------------------------*/
/*
 * Set to disable parts of the driver
 */
/*#define DC395x_NO_DISCONNECT*/
/*#define DC395x_NO_TAGQ*/
/*#define DC395x_NO_SYNC*/
/*#define DC395x_NO_WIDE*/

/*---------------------------------------------------------------------------
                                  Debugging
 ---------------------------------------------------------------------------*/
/*
 * Types of debugging that can be enabled and disabled
 */
#define DBG_KG		0x0001
#define DBG_0		0x0002
#define DBG_1		0x0004
#define DBG_SG		0x0020
#define DBG_FIFO	0x0040
#define DBG_PIO		0x0080


/*
 * Set set of things to output debugging for.
 * Undefine to remove all debugging
 */
/*#define DEBUG_MASK (DBG_0|DBG_1|DBG_SG|DBG_FIFO|DBG_PIO)*/
/*#define  DEBUG_MASK	DBG_0*/


/*
 * Output a kernel mesage at the specified level and append the
 * driver name and a ": " to the start of the message
 */
#define dprintkl(level, format, arg...)  \
    printk(level DC395X_NAME ": " format , ## arg)


#ifdef DEBUG_MASK
/*
 * print a debug message - this is formated with KERN_DEBUG, then the
 * driver name followed by a ": " and then the message is output. 
 * This also checks that the specified debug level is enabled before
 * outputing the message
 */
#define dprintkdbg(type, format, arg...) \
	do { \
		if ((type) & (DEBUG_MASK)) \
			dprintkl(KERN_DEBUG , format , ## arg); \
	} while (0)

/*
 * Check if the specified type of debugging is enabled
 */
#define debug_enabled(type)	((DEBUG_MASK) & (type))

#else
/*
 * No debugging. Do nothing
 */
#define dprintkdbg(type, format, arg...) \
	do {} while (0)
#define debug_enabled(type)	(0)

#endif


#ifndef PCI_VENDOR_ID_TEKRAM
#define PCI_VENDOR_ID_TEKRAM                    0x1DE1	/* Vendor ID    */
#endif
#ifndef PCI_DEVICE_ID_TEKRAM_TRMS1040
#define PCI_DEVICE_ID_TEKRAM_TRMS1040           0x0391	/* Device ID    */
#endif


#define DC395x_LOCK_IO(dev,flags)		spin_lock_irqsave(((struct Scsi_Host *)dev)->host_lock, flags)
#define DC395x_UNLOCK_IO(dev,flags)		spin_unlock_irqrestore(((struct Scsi_Host *)dev)->host_lock, flags)

#define DC395x_read8(acb,address)		(u8)(inb(acb->io_port_base + (address)))
#define DC395x_read16(acb,address)		(u16)(inw(acb->io_port_base + (address)))
#define DC395x_read32(acb,address)		(u32)(inl(acb->io_port_base + (address)))
#define DC395x_write8(acb,address,value)	outb((value), acb->io_port_base + (address))
#define DC395x_write16(acb,address,value)	outw((value), acb->io_port_base + (address))
#define DC395x_write32(acb,address,value)	outl((value), acb->io_port_base + (address))

#define TAG_NONE 255

/*
 * srb->segement_x is the hw sg list. It is always allocated as a
 * DC395x_MAX_SG_LISTENTRY entries in a linear block which does not
 * cross a page boundy.
 */
#define SEGMENTX_LEN	(sizeof(struct SGentry)*DC395x_MAX_SG_LISTENTRY)


struct SGentry {
	u32 address;		/* bus! address */
	u32 length;
};

/* The SEEPROM structure for TRM_S1040 */
struct NVRamTarget {
	u8 cfg0;		/* Target configuration byte 0  */
	u8 period;		/* Target period                */
	u8 cfg2;		/* Target configuration byte 2  */
	u8 cfg3;		/* Target configuration byte 3  */
};

struct NvRamType {
	u8 sub_vendor_id[2];	/* 0,1  Sub Vendor ID   */
	u8 sub_sys_id[2];	/* 2,3  Sub System ID   */
	u8 sub_class;		/* 4    Sub Class       */
	u8 vendor_id[2];	/* 5,6  Vendor ID       */
	u8 device_id[2];	/* 7,8  Device ID       */
	u8 reserved;		/* 9    Reserved        */
	struct NVRamTarget target[DC395x_MAX_SCSI_ID];
						/** 10,11,12,13
						 ** 14,15,16,17
						 ** ....
						 ** ....
						 ** 70,71,72,73
						 */
	u8 scsi_id;		/* 74 Host Adapter SCSI ID      */
	u8 channel_cfg;		/* 75 Channel configuration     */
	u8 delay_time;		/* 76 Power on delay time       */
	u8 max_tag;		/* 77 Maximum tags              */
	u8 reserved0;		/* 78  */
	u8 boot_target;		/* 79  */
	u8 boot_lun;		/* 80  */
	u8 reserved1;		/* 81  */
	u16 reserved2[22];	/* 82,..125 */
	u16 cksum;		/* 126,127 */
};

struct ScsiReqBlk {
	struct list_head list;		/* next/prev ptrs for srb lists */
	struct DeviceCtlBlk *dcb;
	struct scsi_cmnd *cmd;

	struct SGentry *segment_x;	/* Linear array of hw sg entries (up to 64 entries) */
	dma_addr_t sg_bus_addr;	        /* Bus address of sg list (ie, of segment_x) */

	u8 sg_count;			/* No of HW sg entries for this request */
	u8 sg_index;			/* Index of HW sg entry for this request */
	size_t total_xfer_length;	/* Total number of bytes remaining to be transferred */
	size_t request_length;		/* Total number of bytes in this request */
	/*
	 * The sense buffer handling function, request_sense, uses
	 * the first hw sg entry (segment_x[0]) and the transfer
	 * length (total_xfer_length). While doing this it stores the
	 * original values into the last sg hw list
	 * (srb->segment_x[DC395x_MAX_SG_LISTENTRY - 1] and the
	 * total_xfer_length in xferred. These values are restored in
	 * pci_unmap_srb_sense. This is the only place xferred is used.
	 */
	size_t xferred;		        /* Saved copy of total_xfer_length */

	u16 state;

	u8 msgin_buf[6];
	u8 msgout_buf[6];

	u8 adapter_status;
	u8 target_status;
	u8 msg_count;
	u8 end_message;

	u8 tag_number;
	u8 status;
	u8 retry_count;
	u8 flag;

	u8 scsi_phase;
};

struct DeviceCtlBlk {
	struct list_head list;		/* next/prev ptrs for the dcb list */
	struct AdapterCtlBlk *acb;
	struct list_head srb_going_list;	/* head of going srb list */
	struct list_head srb_waiting_list;	/* head of waiting srb list */

	struct ScsiReqBlk *active_srb;
	u32 tag_mask;

	u16 max_command;

	u8 target_id;		/* SCSI Target ID  (SCSI Only) */
	u8 target_lun;		/* SCSI Log.  Unit (SCSI Only) */
	u8 identify_msg;
	u8 dev_mode;

	u8 inquiry7;		/* To store Inquiry flags */
	u8 sync_mode;		/* 0:async mode */
	u8 min_nego_period;	/* for nego. */
	u8 sync_period;		/* for reg.  */

	u8 sync_offset;		/* for reg. and nego.(low nibble) */
	u8 flag;
	u8 dev_type;
	u8 init_tcq_flag;
};

struct AdapterCtlBlk {
	struct Scsi_Host *scsi_host;

	unsigned long io_port_base;
	unsigned long io_port_len;

	struct list_head dcb_list;		/* head of going dcb list */
	struct DeviceCtlBlk *dcb_run_robin;
	struct DeviceCtlBlk *active_dcb;

	struct list_head srb_free_list;		/* head of free srb list */
	struct ScsiReqBlk *tmp_srb;
	struct timer_list waiting_timer;
	struct timer_list selto_timer;

	unsigned long last_reset;

	u16 srb_count;

	u8 sel_timeout;

	unsigned int irq_level;
	u8 tag_max_num;
	u8 acb_flag;
	u8 gmode2;

	u8 config;
	u8 lun_chk;
	u8 scan_devices;
	u8 hostid_bit;

	u8 dcb_map[DC395x_MAX_SCSI_ID];
	struct DeviceCtlBlk *children[DC395x_MAX_SCSI_ID][32];

	struct pci_dev *dev;

	u8 msg_len;

	struct ScsiReqBlk srb_array[DC395x_MAX_SRB_CNT];
	struct ScsiReqBlk srb;

	struct NvRamType eeprom;	/* eeprom settings for this adapter */
};


/*---------------------------------------------------------------------------
                            Forward declarations
 ---------------------------------------------------------------------------*/
static void data_out_phase0(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		u16 *pscsi_status);
static void data_in_phase0(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		u16 *pscsi_status);
static void command_phase0(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		u16 *pscsi_status);
static void status_phase0(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		u16 *pscsi_status);
static void msgout_phase0(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		u16 *pscsi_status);
static void msgin_phase0(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		u16 *pscsi_status);
static void data_out_phase1(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		u16 *pscsi_status);
static void data_in_phase1(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		u16 *pscsi_status);
static void command_phase1(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		u16 *pscsi_status);
static void status_phase1(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		u16 *pscsi_status);
static void msgout_phase1(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		u16 *pscsi_status);
static void msgin_phase1(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		u16 *pscsi_status);
static void nop0(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		u16 *pscsi_status);
static void nop1(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb, 
		u16 *pscsi_status);
static void set_basic_config(struct AdapterCtlBlk *acb);
static void cleanup_after_transfer(struct AdapterCtlBlk *acb,
		struct ScsiReqBlk *srb);
static void reset_scsi_bus(struct AdapterCtlBlk *acb);
static void data_io_transfer(struct AdapterCtlBlk *acb,
		struct ScsiReqBlk *srb, u16 io_dir);
static void disconnect(struct AdapterCtlBlk *acb);
static void reselect(struct AdapterCtlBlk *acb);
static u8 start_scsi(struct AdapterCtlBlk *acb, struct DeviceCtlBlk *dcb,
		struct ScsiReqBlk *srb);
static inline void enable_msgout_abort(struct AdapterCtlBlk *acb,
		struct ScsiReqBlk *srb);
static void build_srb(struct scsi_cmnd *cmd, struct DeviceCtlBlk *dcb,
		struct ScsiReqBlk *srb);
static void doing_srb_done(struct AdapterCtlBlk *acb, u8 did_code,
		struct scsi_cmnd *cmd, u8 force);
static void scsi_reset_detect(struct AdapterCtlBlk *acb);
static void pci_unmap_srb(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb);
static void pci_unmap_srb_sense(struct AdapterCtlBlk *acb,
		struct ScsiReqBlk *srb);
static void srb_done(struct AdapterCtlBlk *acb, struct DeviceCtlBlk *dcb,
		struct ScsiReqBlk *srb);
static void request_sense(struct AdapterCtlBlk *acb, struct DeviceCtlBlk *dcb,
		struct ScsiReqBlk *srb);
static void set_xfer_rate(struct AdapterCtlBlk *acb,
		struct DeviceCtlBlk *dcb);
static void waiting_timeout(struct timer_list *t);


/*---------------------------------------------------------------------------
                                 Static Data
 ---------------------------------------------------------------------------*/
static u16 current_sync_offset = 0;

static void *dc395x_scsi_phase0[] = {
	data_out_phase0,/* phase:0 */
	data_in_phase0,	/* phase:1 */
	command_phase0,	/* phase:2 */
	status_phase0,	/* phase:3 */
	nop0,		/* phase:4 PH_BUS_FREE .. initial phase */
	nop0,		/* phase:5 PH_BUS_FREE .. initial phase */
	msgout_phase0,	/* phase:6 */
	msgin_phase0,	/* phase:7 */
};

static void *dc395x_scsi_phase1[] = {
	data_out_phase1,/* phase:0 */
	data_in_phase1,	/* phase:1 */
	command_phase1,	/* phase:2 */
	status_phase1,	/* phase:3 */
	nop1,		/* phase:4 PH_BUS_FREE .. initial phase */
	nop1,		/* phase:5 PH_BUS_FREE .. initial phase */
	msgout_phase1,	/* phase:6 */
	msgin_phase1,	/* phase:7 */
};

/*
 *Fast20:	000	 50ns, 20.0 MHz
 *		001	 75ns, 13.3 MHz
 *		010	100ns, 10.0 MHz
 *		011	125ns,  8.0 MHz
 *		100	150ns,  6.6 MHz
 *		101	175ns,  5.7 MHz
 *		110	200ns,  5.0 MHz
 *		111	250ns,  4.0 MHz
 *
 *Fast40(LVDS):	000	 25ns, 40.0 MHz
 *		001	 50ns, 20.0 MHz
 *		010	 75ns, 13.3 MHz
 *		011	100ns, 10.0 MHz
 *		100	125ns,  8.0 MHz
 *		101	150ns,  6.6 MHz
 *		110	175ns,  5.7 MHz
 *		111	200ns,  5.0 MHz
 */
/*static u8	clock_period[] = {12,19,25,31,37,44,50,62};*/

/* real period:48ns,76ns,100ns,124ns,148ns,176ns,200ns,248ns */
static u8 clock_period[] = { 12, 18, 25, 31, 37, 43, 50, 62 };
static u16 clock_speed[] = { 200, 133, 100, 80, 67, 58, 50, 40 };


/*---------------------------------------------------------------------------
                                Configuration
  ---------------------------------------------------------------------------*/
/*
 * Module/boot parameters currently effect *all* instances of the
 * card in the system.
 */

/*
 * Command line parameters are stored in a structure below.
 * These are the index's into the structure for the various
 * command line options.
 */
#define CFG_ADAPTER_ID		0
#define CFG_MAX_SPEED		1
#define CFG_DEV_MODE		2
#define CFG_ADAPTER_MODE	3
#define CFG_TAGS		4
#define CFG_RESET_DELAY		5

#define CFG_NUM			6	/* number of configuration items */


/*
 * Value used to indicate that a command line override
 * hasn't been used to modify the value.
 */
#define CFG_PARAM_UNSET -1


/*
 * Hold command line parameters.
 */
struct ParameterData {
	int value;		/* value of this setting */
	int min;		/* minimum value */
	int max;		/* maximum value */
	int def;		/* default value */
	int safe;		/* safe value */
};
static struct ParameterData cfg_data[] = {
	{ /* adapter id */
		CFG_PARAM_UNSET,
		0,
		15,
		7,
		7
	},
	{ /* max speed */
		CFG_PARAM_UNSET,
		  0,
		  7,
		  1,	/* 13.3Mhz */
		  4,	/*  6.7Hmz */
	},
	{ /* dev mode */
		CFG_PARAM_UNSET,
		0,
		0x3f,
		NTC_DO_PARITY_CHK | NTC_DO_DISCONNECT | NTC_DO_SYNC_NEGO |
			NTC_DO_WIDE_NEGO | NTC_DO_TAG_QUEUEING |
			NTC_DO_SEND_START,
		NTC_DO_PARITY_CHK | NTC_DO_SEND_START
	},
	{ /* adapter mode */
		CFG_PARAM_UNSET,
		0,
		0x2f,
		NAC_SCANLUN |
		NAC_GT2DRIVES | NAC_GREATER_1G | NAC_POWERON_SCSI_RESET
			/*| NAC_ACTIVE_NEG*/,
		NAC_GT2DRIVES | NAC_GREATER_1G | NAC_POWERON_SCSI_RESET | 0x08
	},
	{ /* tags */
		CFG_PARAM_UNSET,
		0,
		5,
		3,	/* 16 tags (??) */
		2,
	},
	{ /* reset delay */
		CFG_PARAM_UNSET,
		0,
		180,
		1,	/* 1 second */
		10,	/* 10 seconds */
	}
};


/*
 * Safe settings. If set to zero the BIOS/default values with
 * command line overrides will be used. If set to 1 then safe and
 * slow settings will be used.
 */
static bool use_safe_settings = 0;
module_param_named(safe, use_safe_settings, bool, 0);
MODULE_PARM_DESC(safe, "Use safe and slow settings only. Default: false");


module_param_named(adapter_id, cfg_data[CFG_ADAPTER_ID].value, int, 0);
MODULE_PARM_DESC(adapter_id, "Adapter SCSI ID. Default 7 (0-15)");

module_param_named(max_speed, cfg_data[CFG_MAX_SPEED].value, int, 0);
MODULE_PARM_DESC(max_speed, "Maximum bus speed. Default 1 (0-7) Speeds: 0=20, 1=13.3, 2=10, 3=8, 4=6.7, 5=5.8, 6=5, 7=4 Mhz");

module_param_named(dev_mode, cfg_data[CFG_DEV_MODE].value, int, 0);
MODULE_PARM_DESC(dev_mode, "Device mode.");

module_param_named(adapter_mode, cfg_data[CFG_ADAPTER_MODE].value, int, 0);
MODULE_PARM_DESC(adapter_mode, "Adapter mode.");

module_param_named(tags, cfg_data[CFG_TAGS].value, int, 0);
MODULE_PARM_DESC(tags, "Number of tags (1<<x). Default 3 (0-5)");

module_param_named(reset_delay, cfg_data[CFG_RESET_DELAY].value, int, 0);
MODULE_PARM_DESC(reset_delay, "Reset delay in seconds. Default 1 (0-180)");


/**
 * set_safe_settings - if the use_safe_settings option is set then
 * set all values to the safe and slow values.
 **/
static void set_safe_settings(void)
{
	if (use_safe_settings)
	{
		int i;

		dprintkl(KERN_INFO, "Using safe settings.\n");
		for (i = 0; i < CFG_NUM; i++)
		{
			cfg_data[i].value = cfg_data[i].safe;
		}
	}
}


/**
 * fix_settings - reset any boot parameters which are out of range
 * back to the default values.
 **/
static void fix_settings(void)
{
	int i;

	dprintkdbg(DBG_1,
		"setup: AdapterId=%08x MaxSpeed=%08x DevMode=%08x "
		"AdapterMode=%08x Tags=%08x ResetDelay=%08x\n",
		cfg_data[CFG_ADAPTER_ID].value,
		cfg_data[CFG_MAX_SPEED].value,
		cfg_data[CFG_DEV_MODE].value,
		cfg_data[CFG_ADAPTER_MODE].value,
		cfg_data[CFG_TAGS].value,
		cfg_data[CFG_RESET_DELAY].value);
	for (i = 0; i < CFG_NUM; i++)
	{
		if (cfg_data[i].value < cfg_data[i].min
		    || cfg_data[i].value > cfg_data[i].max)
			cfg_data[i].value = cfg_data[i].def;
	}
}



/*
 * Mapping from the eeprom delay index value (index into this array)
 * to the number of actual seconds that the delay should be for.
 */
static char eeprom_index_to_delay_map[] =
	{ 1, 3, 5, 10, 16, 30, 60, 120 };


/**
 * eeprom_index_to_delay - Take the eeprom delay setting and convert it
 * into a number of seconds.
 *
 * @eeprom: The eeprom structure in which we find the delay index to map.
 **/
static void eeprom_index_to_delay(struct NvRamType *eeprom)
{
	eeprom->delay_time = eeprom_index_to_delay_map[eeprom->delay_time];
}


/**
 * delay_to_eeprom_index - Take a delay in seconds and return the
 * closest eeprom index which will delay for at least that amount of
 * seconds.
 *
 * @delay: The delay, in seconds, to find the eeprom index for.
 **/
static int delay_to_eeprom_index(int delay)
{
	u8 idx = 0;
	while (idx < 7 && eeprom_index_to_delay_map[idx] < delay)
		idx++;
	return idx;
}


/**
 * eeprom_override - Override the eeprom settings, in the provided
 * eeprom structure, with values that have been set on the command
 * line.
 *
 * @eeprom: The eeprom data to override with command line options.
 **/
static void eeprom_override(struct NvRamType *eeprom)
{
	u8 id;

	/* Adapter Settings */
	if (cfg_data[CFG_ADAPTER_ID].value != CFG_PARAM_UNSET)
		eeprom->scsi_id = (u8)cfg_data[CFG_ADAPTER_ID].value;

	if (cfg_data[CFG_ADAPTER_MODE].value != CFG_PARAM_UNSET)
		eeprom->channel_cfg = (u8)cfg_data[CFG_ADAPTER_MODE].value;

	if (cfg_data[CFG_RESET_DELAY].value != CFG_PARAM_UNSET)
		eeprom->delay_time = delay_to_eeprom_index(
					cfg_data[CFG_RESET_DELAY].value);

	if (cfg_data[CFG_TAGS].value != CFG_PARAM_UNSET)
		eeprom->max_tag = (u8)cfg_data[CFG_TAGS].value;

	/* Device Settings */
	for (id = 0; id < DC395x_MAX_SCSI_ID; id++) {
		if (cfg_data[CFG_DEV_MODE].value != CFG_PARAM_UNSET)
			eeprom->target[id].cfg0 =
				(u8)cfg_data[CFG_DEV_MODE].value;

		if (cfg_data[CFG_MAX_SPEED].value != CFG_PARAM_UNSET)
			eeprom->target[id].period =
				(u8)cfg_data[CFG_MAX_SPEED].value;

	}
}


/*---------------------------------------------------------------------------
 ---------------------------------------------------------------------------*/

static unsigned int list_size(struct list_head *head)
{
	unsigned int count = 0;
	struct list_head *pos;
	list_for_each(pos, head)
		count++;
	return count;
}


static struct DeviceCtlBlk *dcb_get_next(struct list_head *head,
		struct DeviceCtlBlk *pos)
{
	int use_next = 0;
	struct DeviceCtlBlk* next = NULL;
	struct DeviceCtlBlk* i;

	if (list_empty(head))
		return NULL;

	/* find supplied dcb and then select the next one */
	list_for_each_entry(i, head, list)
		if (use_next) {
			next = i;
			break;
		} else if (i == pos) {
			use_next = 1;
		}
	/* if no next one take the head one (ie, wraparound) */
	if (!next)
        	list_for_each_entry(i, head, list) {
        		next = i;
        		break;
        	}

	return next;
}


static void free_tag(struct DeviceCtlBlk *dcb, struct ScsiReqBlk *srb)
{
	if (srb->tag_number < 255) {
		dcb->tag_mask &= ~(1 << srb->tag_number);	/* free tag mask */
		srb->tag_number = 255;
	}
}


/* Find cmd in SRB list */
static inline struct ScsiReqBlk *find_cmd(struct scsi_cmnd *cmd,
		struct list_head *head)
{
	struct ScsiReqBlk *i;
	list_for_each_entry(i, head, list)
		if (i->cmd == cmd)
			return i;
	return NULL;
}

/* Sets the timer to wake us up */
static void waiting_set_timer(struct AdapterCtlBlk *acb, unsigned long to)
{
	if (timer_pending(&acb->waiting_timer))
		return;
	if (time_before(jiffies + to, acb->last_reset - HZ / 2))
		acb->waiting_timer.expires =
		    acb->last_reset - HZ / 2 + 1;
	else
		acb->waiting_timer.expires = jiffies + to + 1;
	add_timer(&acb->waiting_timer);
}


/* Send the next command from the waiting list to the bus */
static void waiting_process_next(struct AdapterCtlBlk *acb)
{
	struct DeviceCtlBlk *start = NULL;
	struct DeviceCtlBlk *pos;
	struct DeviceCtlBlk *dcb;
	struct ScsiReqBlk *srb;
	struct list_head *dcb_list_head = &acb->dcb_list;

	if (acb->active_dcb
	    || (acb->acb_flag & (RESET_DETECT + RESET_DONE + RESET_DEV)))
		return;

	if (timer_pending(&acb->waiting_timer))
		del_timer(&acb->waiting_timer);

	if (list_empty(dcb_list_head))
		return;

	/*
	 * Find the starting dcb. Need to find it again in the list
	 * since the list may have changed since we set the ptr to it
	 */
	list_for_each_entry(dcb, dcb_list_head, list)
		if (dcb == acb->dcb_run_robin) {
			start = dcb;
			break;
		}
	if (!start) {
		/* This can happen! */
		start = list_entry(dcb_list_head->next, typeof(*start), list);
		acb->dcb_run_robin = start;
	}


	/*
	 * Loop over the dcb, but we start somewhere (potentially) in
	 * the middle of the loop so we need to manully do this.
	 */
	pos = start;
	do {
		struct list_head *waiting_list_head = &pos->srb_waiting_list;

		/* Make sure, the next another device gets scheduled ... */
		acb->dcb_run_robin = dcb_get_next(dcb_list_head,
						  acb->dcb_run_robin);

		if (list_empty(waiting_list_head) ||
		    pos->max_command <= list_size(&pos->srb_going_list)) {
			/* move to next dcb */
			pos = dcb_get_next(dcb_list_head, pos);
		} else {
			srb = list_entry(waiting_list_head->next,
					 struct ScsiReqBlk, list);

			/* Try to send to the bus */
			if (!start_scsi(acb, pos, srb))
				list_move(&srb->list, &pos->srb_going_list);
			else
				waiting_set_timer(acb, HZ/50);
			break;
		}
	} while (pos != start);
}


/* Wake up waiting queue */
static void waiting_timeout(struct timer_list *t)
{
	unsigned long flags;
	struct AdapterCtlBlk *acb = from_timer(acb, t, waiting_timer);
	dprintkdbg(DBG_1,
		"waiting_timeout: Queue woken up by timer. acb=%p\n", acb);
	DC395x_LOCK_IO(acb->scsi_host, flags);
	waiting_process_next(acb);
	DC395x_UNLOCK_IO(acb->scsi_host, flags);
}


/* Get the DCB for a given ID/LUN combination */
static struct DeviceCtlBlk *find_dcb(struct AdapterCtlBlk *acb, u8 id, u8 lun)
{
	return acb->children[id][lun];
}


/* Send SCSI Request Block (srb) to adapter (acb) */
static void send_srb(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb)
{
	struct DeviceCtlBlk *dcb = srb->dcb;

	if (dcb->max_command <= list_size(&dcb->srb_going_list) ||
	    acb->active_dcb ||
	    (acb->acb_flag & (RESET_DETECT + RESET_DONE + RESET_DEV))) {
		list_add_tail(&srb->list, &dcb->srb_waiting_list);
		waiting_process_next(acb);
		return;
	}

	if (!start_scsi(acb, dcb, srb)) {
		list_add_tail(&srb->list, &dcb->srb_going_list);
	} else {
		list_add(&srb->list, &dcb->srb_waiting_list);
		waiting_set_timer(acb, HZ / 50);
	}
}

/* Prepare SRB for being sent to Device DCB w/ command *cmd */
static void build_srb(struct scsi_cmnd *cmd, struct DeviceCtlBlk *dcb,
		struct ScsiReqBlk *srb)
{
	int nseg;
	enum dma_data_direction dir = cmd->sc_data_direction;
	dprintkdbg(DBG_0, "build_srb: (0x%p) <%02i-%i>\n",
		cmd, dcb->target_id, dcb->target_lun);

	srb->dcb = dcb;
	srb->cmd = cmd;
	srb->sg_count = 0;
	srb->total_xfer_length = 0;
	srb->sg_bus_addr = 0;
	srb->sg_index = 0;
	srb->adapter_status = 0;
	srb->target_status = 0;
	srb->msg_count = 0;
	srb->status = 0;
	srb->flag = 0;
	srb->state = 0;
	srb->retry_count = 0;
	srb->tag_number = TAG_NONE;
	srb->scsi_phase = PH_BUS_FREE;	/* initial phase */
	srb->end_message = 0;

	nseg = scsi_dma_map(cmd);
	BUG_ON(nseg < 0);

	if (dir == DMA_NONE || !nseg) {
		dprintkdbg(DBG_0,
			"build_srb: [0] len=%d buf=%p use_sg=%d !MAP=%08x\n",
			   cmd->bufflen, scsi_sglist(cmd), scsi_sg_count(cmd),
			   srb->segment_x[0].address);
	} else {
		int i;
		u32 reqlen = scsi_bufflen(cmd);
		struct scatterlist *sg;
		struct SGentry *sgp = srb->segment_x;

		srb->sg_count = nseg;

		dprintkdbg(DBG_0,
			   "build_srb: [n] len=%d buf=%p use_sg=%d segs=%d\n",
			   reqlen, scsi_sglist(cmd), scsi_sg_count(cmd),
			   srb->sg_count);

		scsi_for_each_sg(cmd, sg, srb->sg_count, i) {
			u32 busaddr = (u32)sg_dma_address(sg);
			u32 seglen = (u32)sg->length;
			sgp[i].address = busaddr;
			sgp[i].length = seglen;
			srb->total_xfer_length += seglen;
		}
		sgp += srb->sg_count - 1;

		/*
		 * adjust last page if too big as it is allocated
		 * on even page boundaries
		 */
		if (srb->total_xfer_length > reqlen) {
			sgp->length -= (srb->total_xfer_length - reqlen);
			srb->total_xfer_length = reqlen;
		}

		/* Fixup for WIDE padding - make sure length is even */
		if (dcb->sync_period & WIDE_SYNC &&
		    srb->total_xfer_length % 2) {
			srb->total_xfer_length++;
			sgp->length++;
		}

		srb->sg_bus_addr = dma_map_single(&dcb->acb->dev->dev,
				srb->segment_x, SEGMENTX_LEN, DMA_TO_DEVICE);

		dprintkdbg(DBG_SG, "build_srb: [n] map sg %p->%08x(%05x)\n",
			srb->segment_x, srb->sg_bus_addr, SEGMENTX_LEN);
	}

	srb->request_length = srb->total_xfer_length;
}


/**
 * dc395x_queue_command_lck - queue scsi command passed from the mid
 * layer, invoke 'done' on completion
 *
 * @cmd: pointer to scsi command object
 *
 * Returns 1 if the adapter (host) is busy, else returns 0. One
 * reason for an adapter to be busy is that the number
 * of outstanding queued commands is already equal to
 * struct Scsi_Host::can_queue .
 *
 * Required: if struct Scsi_Host::can_queue is ever non-zero
 *           then this function is required.
 *
 * Locks: struct Scsi_Host::host_lock held on entry (with "irqsave")
 *        and is expected to be held on return.
 *
 */
static int dc395x_queue_command_lck(struct scsi_cmnd *cmd)
{
	void (*done)(struct scsi_cmnd *) = scsi_done;
	struct DeviceCtlBlk *dcb;
	struct ScsiReqBlk *srb;
	struct AdapterCtlBlk *acb =
	    (struct AdapterCtlBlk *)cmd->device->host->hostdata;
	dprintkdbg(DBG_0, "queue_command: (0x%p) <%02i-%i> cmnd=0x%02x\n",
		cmd, cmd->device->id, (u8)cmd->device->lun, cmd->cmnd[0]);

	/* Assume BAD_TARGET; will be cleared later */
	set_host_byte(cmd, DID_BAD_TARGET);

	/* ignore invalid targets */
	if (cmd->device->id >= acb->scsi_host->max_id ||
	    cmd->device->lun >= acb->scsi_host->max_lun ||
	    cmd->device->lun >31) {
		goto complete;
	}

	/* does the specified lun on the specified device exist */
	if (!(acb->dcb_map[cmd->device->id] & (1 << cmd->device->lun))) {
		dprintkl(KERN_INFO, "queue_command: Ignore target <%02i-%i>\n",
			cmd->device->id, (u8)cmd->device->lun);
		goto complete;
	}

	/* do we have a DCB for the device */
	dcb = find_dcb(acb, cmd->device->id, cmd->device->lun);
	if (!dcb) {
		/* should never happen */
		dprintkl(KERN_ERR, "queue_command: No such device <%02i-%i>",
			cmd->device->id, (u8)cmd->device->lun);
		goto complete;
	}

	set_host_byte(cmd, DID_OK);
	set_status_byte(cmd, SAM_STAT_GOOD);

	srb = list_first_entry_or_null(&acb->srb_free_list,
			struct ScsiReqBlk, list);
	if (!srb) {
		/*
		 * Return 1 since we are unable to queue this command at this
		 * point in time.
		 */
		dprintkdbg(DBG_0, "queue_command: No free srb's\n");
		return 1;
	}
	list_del(&srb->list);

	build_srb(cmd, dcb, srb);

	if (!list_empty(&dcb->srb_waiting_list)) {
		/* append to waiting queue */
		list_add_tail(&srb->list, &dcb->srb_waiting_list);
		waiting_process_next(acb);
	} else {
		/* process immediately */
		send_srb(acb, srb);
	}
	dprintkdbg(DBG_1, "queue_command: (0x%p) done\n", cmd);
	return 0;

complete:
	/*
	 * Complete the command immediatey, and then return 0 to
	 * indicate that we have handled the command. This is usually
	 * done when the commad is for things like non existent
	 * devices.
	 */
	done(cmd);
	return 0;
}

static DEF_SCSI_QCMD(dc395x_queue_command)

static void dump_register_info(struct AdapterCtlBlk *acb,
		struct DeviceCtlBlk *dcb, struct ScsiReqBlk *srb)
{
	u16 pstat;
	struct pci_dev *dev = acb->dev;
	pci_read_config_word(dev, PCI_STATUS, &pstat);
	if (!dcb)
		dcb = acb->active_dcb;
	if (!srb && dcb)
		srb = dcb->active_srb;
	if (srb) {
		if (!srb->cmd)
			dprintkl(KERN_INFO, "dump: srb=%p cmd=%p OOOPS!\n",
				srb, srb->cmd);
		else
			dprintkl(KERN_INFO, "dump: srb=%p cmd=%p "
				 "cmnd=0x%02x <%02i-%i>\n",
				srb, srb->cmd,
				srb->cmd->cmnd[0], srb->cmd->device->id,
				(u8)srb->cmd->device->lun);
		printk("  sglist=%p cnt=%i idx=%i len=%zu\n",
		       srb->segment_x, srb->sg_count, srb->sg_index,
		       srb->total_xfer_length);
		printk("  state=0x%04x status=0x%02x phase=0x%02x (%sconn.)\n",
		       srb->state, srb->status, srb->scsi_phase,
		       (acb->active_dcb) ? "" : "not");
	}
	dprintkl(KERN_INFO, "dump: SCSI{status=0x%04x fifocnt=0x%02x "
		"signals=0x%02x irqstat=0x%02x sync=0x%02x target=0x%02x "
		"rselid=0x%02x ctr=0x%08x irqen=0x%02x config=0x%04x "
		"config2=0x%02x cmd=0x%02x selto=0x%02x}\n",
		DC395x_read16(acb, TRM_S1040_SCSI_STATUS),
		DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT),
		DC395x_read8(acb, TRM_S1040_SCSI_SIGNAL),
		DC395x_read8(acb, TRM_S1040_SCSI_INTSTATUS),
		DC395x_read8(acb, TRM_S1040_SCSI_SYNC),
		DC395x_read8(acb, TRM_S1040_SCSI_TARGETID),
		DC395x_read8(acb, TRM_S1040_SCSI_IDMSG),
		DC395x_read32(acb, TRM_S1040_SCSI_COUNTER),
		DC395x_read8(acb, TRM_S1040_SCSI_INTEN),
		DC395x_read16(acb, TRM_S1040_SCSI_CONFIG0),
		DC395x_read8(acb, TRM_S1040_SCSI_CONFIG2),
		DC395x_read8(acb, TRM_S1040_SCSI_COMMAND),
		DC395x_read8(acb, TRM_S1040_SCSI_TIMEOUT));
	dprintkl(KERN_INFO, "dump: DMA{cmd=0x%04x fifocnt=0x%02x fstat=0x%02x "
		"irqstat=0x%02x irqen=0x%02x cfg=0x%04x tctr=0x%08x "
		"ctctr=0x%08x addr=0x%08x:0x%08x}\n",
		DC395x_read16(acb, TRM_S1040_DMA_COMMAND),
		DC395x_read8(acb, TRM_S1040_DMA_FIFOCNT),
		DC395x_read8(acb, TRM_S1040_DMA_FIFOSTAT),
		DC395x_read8(acb, TRM_S1040_DMA_STATUS),
		DC395x_read8(acb, TRM_S1040_DMA_INTEN),
		DC395x_read16(acb, TRM_S1040_DMA_CONFIG),
		DC395x_read32(acb, TRM_S1040_DMA_XCNT),
		DC395x_read32(acb, TRM_S1040_DMA_CXCNT),
		DC395x_read32(acb, TRM_S1040_DMA_XHIGHADDR),
		DC395x_read32(acb, TRM_S1040_DMA_XLOWADDR));
	dprintkl(KERN_INFO, "dump: gen{gctrl=0x%02x gstat=0x%02x gtmr=0x%02x} "
		"pci{status=0x%04x}\n",
		DC395x_read8(acb, TRM_S1040_GEN_CONTROL),
		DC395x_read8(acb, TRM_S1040_GEN_STATUS),
		DC395x_read8(acb, TRM_S1040_GEN_TIMER),
		pstat);
}


static inline void clear_fifo(struct AdapterCtlBlk *acb, char *txt)
{
#if debug_enabled(DBG_FIFO)
	u8 lines = DC395x_read8(acb, TRM_S1040_SCSI_SIGNAL);
	u8 fifocnt = DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT);
	if (!(fifocnt & 0x40))
		dprintkdbg(DBG_FIFO,
			"clear_fifo: (%i bytes) on phase %02x in %s\n",
			fifocnt & 0x3f, lines, txt);
#endif
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_CLRFIFO);
}


static void reset_dev_param(struct AdapterCtlBlk *acb)
{
	struct DeviceCtlBlk *dcb;
	struct NvRamType *eeprom = &acb->eeprom;
	dprintkdbg(DBG_0, "reset_dev_param: acb=%p\n", acb);

	list_for_each_entry(dcb, &acb->dcb_list, list) {
		u8 period_index;

		dcb->sync_mode &= ~(SYNC_NEGO_DONE + WIDE_NEGO_DONE);
		dcb->sync_period = 0;
		dcb->sync_offset = 0;

		dcb->dev_mode = eeprom->target[dcb->target_id].cfg0;
		period_index = eeprom->target[dcb->target_id].period & 0x07;
		dcb->min_nego_period = clock_period[period_index];
		if (!(dcb->dev_mode & NTC_DO_WIDE_NEGO)
		    || !(acb->config & HCC_WIDE_CARD))
			dcb->sync_mode &= ~WIDE_NEGO_ENABLE;
	}
}


/*
 * perform a hard reset on the SCSI bus
 * @cmd - some command for this host (for fetching hooks)
 * Returns: SUCCESS (0x2002) on success, else FAILED (0x2003).
 */
static int __dc395x_eh_bus_reset(struct scsi_cmnd *cmd)
{
	struct AdapterCtlBlk *acb =
		(struct AdapterCtlBlk *)cmd->device->host->hostdata;
	dprintkl(KERN_INFO,
		"eh_bus_reset: (0%p) target=<%02i-%i> cmd=%p\n",
		cmd, cmd->device->id, (u8)cmd->device->lun, cmd);

	if (timer_pending(&acb->waiting_timer))
		del_timer(&acb->waiting_timer);

	/*
	 * disable interrupt    
	 */
	DC395x_write8(acb, TRM_S1040_DMA_INTEN, 0x00);
	DC395x_write8(acb, TRM_S1040_SCSI_INTEN, 0x00);
	DC395x_write8(acb, TRM_S1040_SCSI_CONTROL, DO_RSTMODULE);
	DC395x_write8(acb, TRM_S1040_DMA_CONTROL, DMARESETMODULE);

	reset_scsi_bus(acb);
	udelay(500);

	/* We may be in serious trouble. Wait some seconds */
	acb->last_reset =
	    jiffies + 3 * HZ / 2 +
	    HZ * acb->eeprom.delay_time;

	/*
	 * re-enable interrupt      
	 */
	/* Clear SCSI FIFO          */
	DC395x_write8(acb, TRM_S1040_DMA_CONTROL, CLRXFIFO);
	clear_fifo(acb, "eh_bus_reset");
	/* Delete pending IRQ */
	DC395x_read8(acb, TRM_S1040_SCSI_INTSTATUS);
	set_basic_config(acb);

	reset_dev_param(acb);
	doing_srb_done(acb, DID_RESET, cmd, 0);
	acb->active_dcb = NULL;
	acb->acb_flag = 0;	/* RESET_DETECT, RESET_DONE ,RESET_DEV */
	waiting_process_next(acb);

	return SUCCESS;
}

static int dc395x_eh_bus_reset(struct scsi_cmnd *cmd)
{
	int rc;

	spin_lock_irq(cmd->device->host->host_lock);
	rc = __dc395x_eh_bus_reset(cmd);
	spin_unlock_irq(cmd->device->host->host_lock);

	return rc;
}

/*
 * abort an errant SCSI command
 * @cmd - command to be aborted
 * Returns: SUCCESS (0x2002) on success, else FAILED (0x2003).
 */
static int dc395x_eh_abort(struct scsi_cmnd *cmd)
{
	/*
	 * Look into our command queues: If it has not been sent already,
	 * we remove it and return success. Otherwise fail.
	 */
	struct AdapterCtlBlk *acb =
	    (struct AdapterCtlBlk *)cmd->device->host->hostdata;
	struct DeviceCtlBlk *dcb;
	struct ScsiReqBlk *srb;
	dprintkl(KERN_INFO, "eh_abort: (0x%p) target=<%02i-%i> cmd=%p\n",
		cmd, cmd->device->id, (u8)cmd->device->lun, cmd);

	dcb = find_dcb(acb, cmd->device->id, cmd->device->lun);
	if (!dcb) {
		dprintkl(KERN_DEBUG, "eh_abort: No such device\n");
		return FAILED;
	}

	srb = find_cmd(cmd, &dcb->srb_waiting_list);
	if (srb) {
		list_del(&srb->list);
		pci_unmap_srb_sense(acb, srb);
		pci_unmap_srb(acb, srb);
		free_tag(dcb, srb);
		list_add_tail(&srb->list, &acb->srb_free_list);
		dprintkl(KERN_DEBUG, "eh_abort: Command was waiting\n");
		set_host_byte(cmd, DID_ABORT);
		return SUCCESS;
	}
	srb = find_cmd(cmd, &dcb->srb_going_list);
	if (srb) {
		dprintkl(KERN_DEBUG, "eh_abort: Command in progress\n");
		/* XXX: Should abort the command here */
	} else {
		dprintkl(KERN_DEBUG, "eh_abort: Command not found\n");
	}
	return FAILED;
}


/* SDTR */
static void build_sdtr(struct AdapterCtlBlk *acb, struct DeviceCtlBlk *dcb,
		struct ScsiReqBlk *srb)
{
	u8 *ptr = srb->msgout_buf + srb->msg_count;
	if (srb->msg_count > 1) {
		dprintkl(KERN_INFO,
			"build_sdtr: msgout_buf BUSY (%i: %02x %02x)\n",
			srb->msg_count, srb->msgout_buf[0],
			srb->msgout_buf[1]);
		return;
	}
	if (!(dcb->dev_mode & NTC_DO_SYNC_NEGO)) {
		dcb->sync_offset = 0;
		dcb->min_nego_period = 200 >> 2;
	} else if (dcb->sync_offset == 0)
		dcb->sync_offset = SYNC_NEGO_OFFSET;

	srb->msg_count += spi_populate_sync_msg(ptr, dcb->min_nego_period,
						dcb->sync_offset);
	srb->state |= SRB_DO_SYNC_NEGO;
}


/* WDTR */
static void build_wdtr(struct AdapterCtlBlk *acb, struct DeviceCtlBlk *dcb,
		struct ScsiReqBlk *srb)
{
	u8 wide = ((dcb->dev_mode & NTC_DO_WIDE_NEGO) &
		   (acb->config & HCC_WIDE_CARD)) ? 1 : 0;
	u8 *ptr = srb->msgout_buf + srb->msg_count;
	if (srb->msg_count > 1) {
		dprintkl(KERN_INFO,
			"build_wdtr: msgout_buf BUSY (%i: %02x %02x)\n",
			srb->msg_count, srb->msgout_buf[0],
			srb->msgout_buf[1]);
		return;
	}
	srb->msg_count += spi_populate_width_msg(ptr, wide);
	srb->state |= SRB_DO_WIDE_NEGO;
}


#if 0
/* Timer to work around chip flaw: When selecting and the bus is 
 * busy, we sometimes miss a Selection timeout IRQ */
void selection_timeout_missed(unsigned long ptr);
/* Sets the timer to wake us up */
static void selto_timer(struct AdapterCtlBlk *acb)
{
	if (timer_pending(&acb->selto_timer))
		return;
	acb->selto_timer.function = selection_timeout_missed;
	acb->selto_timer.data = (unsigned long) acb;
	if (time_before
	    (jiffies + HZ, acb->last_reset + HZ / 2))
		acb->selto_timer.expires =
		    acb->last_reset + HZ / 2 + 1;
	else
		acb->selto_timer.expires = jiffies + HZ + 1;
	add_timer(&acb->selto_timer);
}


void selection_timeout_missed(unsigned long ptr)
{
	unsigned long flags;
	struct AdapterCtlBlk *acb = (struct AdapterCtlBlk *)ptr;
	struct ScsiReqBlk *srb;
	dprintkl(KERN_DEBUG, "Chip forgot to produce SelTO IRQ!\n");
	if (!acb->active_dcb || !acb->active_dcb->active_srb) {
		dprintkl(KERN_DEBUG, "... but no cmd pending? Oops!\n");
		return;
	}
	DC395x_LOCK_IO(acb->scsi_host, flags);
	srb = acb->active_dcb->active_srb;
	disconnect(acb);
	DC395x_UNLOCK_IO(acb->scsi_host, flags);
}
#endif


static u8 start_scsi(struct AdapterCtlBlk* acb, struct DeviceCtlBlk* dcb,
		struct ScsiReqBlk* srb)
{
	u16 __maybe_unused s_stat2, return_code;
	u8 s_stat, scsicommand, i, identify_message;
	u8 *ptr;
	dprintkdbg(DBG_0, "start_scsi: (0x%p) <%02i-%i> srb=%p\n",
		dcb->target_id, dcb->target_lun, srb);

	srb->tag_number = TAG_NONE;	/* acb->tag_max_num: had error read in eeprom */

	s_stat = DC395x_read8(acb, TRM_S1040_SCSI_SIGNAL);
	s_stat2 = 0;
	s_stat2 = DC395x_read16(acb, TRM_S1040_SCSI_STATUS);
#if 1
	if (s_stat & 0x20 /* s_stat2 & 0x02000 */ ) {
		dprintkdbg(DBG_KG, "start_scsi: (0x%p) BUSY %02x %04x\n",
			s_stat, s_stat2);
		/*
		 * Try anyway?
		 *
		 * We could, BUT: Sometimes the TRM_S1040 misses to produce a Selection
		 * Timeout, a Disconnect or a Reselection IRQ, so we would be screwed!
		 * (This is likely to be a bug in the hardware. Obviously, most people
		 *  only have one initiator per SCSI bus.)
		 * Instead let this fail and have the timer make sure the command is 
		 * tried again after a short time
		 */
		/*selto_timer (acb); */
		return 1;
	}
#endif
	if (acb->active_dcb) {
		dprintkl(KERN_DEBUG, "start_scsi: (0x%p) Attempt to start a"
			"command while another command (0x%p) is active.",
			srb->cmd,
			acb->active_dcb->active_srb ?
			    acb->active_dcb->active_srb->cmd : 0);
		return 1;
	}
	if (DC395x_read16(acb, TRM_S1040_SCSI_STATUS) & SCSIINTERRUPT) {
		dprintkdbg(DBG_KG, "start_scsi: (0x%p) Failed (busy)\n", srb->cmd);
		return 1;
	}
	/* Allow starting of SCSI commands half a second before we allow the mid-level
	 * to queue them again after a reset */
	if (time_before(jiffies, acb->last_reset - HZ / 2)) {
		dprintkdbg(DBG_KG, "start_scsi: Refuse cmds (reset wait)\n");
		return 1;
	}

	/* Flush FIFO */
	clear_fifo(acb, "start_scsi");
	DC395x_write8(acb, TRM_S1040_SCSI_HOSTID, acb->scsi_host->this_id);
	DC395x_write8(acb, TRM_S1040_SCSI_TARGETID, dcb->target_id);
	DC395x_write8(acb, TRM_S1040_SCSI_SYNC, dcb->sync_period);
	DC395x_write8(acb, TRM_S1040_SCSI_OFFSET, dcb->sync_offset);
	srb->scsi_phase = PH_BUS_FREE;	/* initial phase */

	identify_message = dcb->identify_msg;
	/*DC395x_TRM_write8(TRM_S1040_SCSI_IDMSG, identify_message); */
	/* Don't allow disconnection for AUTO_REQSENSE: Cont.All.Cond.! */
	if (srb->flag & AUTO_REQSENSE)
		identify_message &= 0xBF;

	if (((srb->cmd->cmnd[0] == INQUIRY)
	     || (srb->cmd->cmnd[0] == REQUEST_SENSE)
	     || (srb->flag & AUTO_REQSENSE))
	    && (((dcb->sync_mode & WIDE_NEGO_ENABLE)
		 && !(dcb->sync_mode & WIDE_NEGO_DONE))
		|| ((dcb->sync_mode & SYNC_NEGO_ENABLE)
		    && !(dcb->sync_mode & SYNC_NEGO_DONE)))
	    && (dcb->target_lun == 0)) {
		srb->msgout_buf[0] = identify_message;
		srb->msg_count = 1;
		scsicommand = SCMD_SEL_ATNSTOP;
		srb->state = SRB_MSGOUT;
#ifndef SYNC_FIRST
		if (dcb->sync_mode & WIDE_NEGO_ENABLE
		    && dcb->inquiry7 & SCSI_INQ_WBUS16) {
			build_wdtr(acb, dcb, srb);
			goto no_cmd;
		}
#endif
		if (dcb->sync_mode & SYNC_NEGO_ENABLE
		    && dcb->inquiry7 & SCSI_INQ_SYNC) {
			build_sdtr(acb, dcb, srb);
			goto no_cmd;
		}
		if (dcb->sync_mode & WIDE_NEGO_ENABLE
		    && dcb->inquiry7 & SCSI_INQ_WBUS16) {
			build_wdtr(acb, dcb, srb);
			goto no_cmd;
		}
		srb->msg_count = 0;
	}
	/* Send identify message */
	DC395x_write8(acb, TRM_S1040_SCSI_FIFO, identify_message);

	scsicommand = SCMD_SEL_ATN;
	srb->state = SRB_START_;
#ifndef DC395x_NO_TAGQ
	if ((dcb->sync_mode & EN_TAG_QUEUEING)
	    && (identify_message & 0xC0)) {
		/* Send Tag message */
		u32 tag_mask = 1;
		u8 tag_number = 0;
		while (tag_mask & dcb->tag_mask
		       && tag_number < dcb->max_command) {
			tag_mask = tag_mask << 1;
			tag_number++;
		}
		if (tag_number >= dcb->max_command) {
			dprintkl(KERN_WARNING, "start_scsi: (0x%p) "
				"Out of tags target=<%02i-%i>)\n",
				srb->cmd, srb->cmd->device->id,
				(u8)srb->cmd->device->lun);
			srb->state = SRB_READY;
			DC395x_write16(acb, TRM_S1040_SCSI_CONTROL,
				       DO_HWRESELECT);
			return 1;
		}
		/* Send Tag id */
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, SIMPLE_QUEUE_TAG);
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, tag_number);
		dcb->tag_mask |= tag_mask;
		srb->tag_number = tag_number;
		scsicommand = SCMD_SEL_ATN3;
		srb->state = SRB_START_;
	}
#endif
/*polling:*/
	/* Send CDB ..command block ......... */
	dprintkdbg(DBG_KG, "start_scsi: (0x%p) <%02i-%i> cmnd=0x%02x tag=%i\n",
		srb->cmd, srb->cmd->device->id, (u8)srb->cmd->device->lun,
		srb->cmd->cmnd[0], srb->tag_number);
	if (srb->flag & AUTO_REQSENSE) {
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, REQUEST_SENSE);
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, (dcb->target_lun << 5));
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, 0);
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, 0);
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, SCSI_SENSE_BUFFERSIZE);
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, 0);
	} else {
		ptr = (u8 *)srb->cmd->cmnd;
		for (i = 0; i < srb->cmd->cmd_len; i++)
			DC395x_write8(acb, TRM_S1040_SCSI_FIFO, *ptr++);
	}
      no_cmd:
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL,
		       DO_HWRESELECT | DO_DATALATCH);
	if (DC395x_read16(acb, TRM_S1040_SCSI_STATUS) & SCSIINTERRUPT) {
		/* 
		 * If start_scsi return 1:
		 * we caught an interrupt (must be reset or reselection ... )
		 * : Let's process it first!
		 */
		dprintkdbg(DBG_0, "start_scsi: (0x%p) <%02i-%i> Failed - busy\n",
			srb->cmd, dcb->target_id, dcb->target_lun);
		srb->state = SRB_READY;
		free_tag(dcb, srb);
		srb->msg_count = 0;
		return_code = 1;
		/* This IRQ should NOT get lost, as we did not acknowledge it */
	} else {
		/* 
		 * If start_scsi returns 0:
		 * we know that the SCSI processor is free
		 */
		srb->scsi_phase = PH_BUS_FREE;	/* initial phase */
		dcb->active_srb = srb;
		acb->active_dcb = dcb;
		return_code = 0;
		/* it's important for atn stop */
		DC395x_write16(acb, TRM_S1040_SCSI_CONTROL,
			       DO_DATALATCH | DO_HWRESELECT);
		/* SCSI command */
		DC395x_write8(acb, TRM_S1040_SCSI_COMMAND, scsicommand);
	}
	return return_code;
}


#define DC395x_ENABLE_MSGOUT \
 DC395x_write16 (acb, TRM_S1040_SCSI_CONTROL, DO_SETATN); \
 srb->state |= SRB_MSGOUT


/* abort command */
static inline void enable_msgout_abort(struct AdapterCtlBlk *acb,
		struct ScsiReqBlk *srb)
{
	srb->msgout_buf[0] = ABORT;
	srb->msg_count = 1;
	DC395x_ENABLE_MSGOUT;
	srb->state &= ~SRB_MSGIN;
	srb->state |= SRB_MSGOUT;
}


/**
 * dc395x_handle_interrupt - Handle an interrupt that has been confirmed to
 *                           have been triggered for this card.
 *
 * @acb:	 a pointer to the adpter control block
 * @scsi_status: the status return when we checked the card
 **/
static void dc395x_handle_interrupt(struct AdapterCtlBlk *acb,
		u16 scsi_status)
{
	struct DeviceCtlBlk *dcb;
	struct ScsiReqBlk *srb;
	u16 phase;
	u8 scsi_intstatus;
	unsigned long flags;
	void (*dc395x_statev)(struct AdapterCtlBlk *, struct ScsiReqBlk *, 
			      u16 *);

	DC395x_LOCK_IO(acb->scsi_host, flags);

	/* This acknowledges the IRQ */
	scsi_intstatus = DC395x_read8(acb, TRM_S1040_SCSI_INTSTATUS);
	if ((scsi_status & 0x2007) == 0x2002)
		dprintkl(KERN_DEBUG,
			"COP after COP completed? %04x\n", scsi_status);
	if (debug_enabled(DBG_KG)) {
		if (scsi_intstatus & INT_SELTIMEOUT)
			dprintkdbg(DBG_KG, "handle_interrupt: Selection timeout\n");
	}
	/*dprintkl(KERN_DEBUG, "handle_interrupt: intstatus = 0x%02x ", scsi_intstatus); */

	if (timer_pending(&acb->selto_timer))
		del_timer(&acb->selto_timer);

	if (scsi_intstatus & (INT_SELTIMEOUT | INT_DISCONNECT)) {
		disconnect(acb);	/* bus free interrupt  */
		goto out_unlock;
	}
	if (scsi_intstatus & INT_RESELECTED) {
		reselect(acb);
		goto out_unlock;
	}
	if (scsi_intstatus & INT_SELECT) {
		dprintkl(KERN_INFO, "Host does not support target mode!\n");
		goto out_unlock;
	}
	if (scsi_intstatus & INT_SCSIRESET) {
		scsi_reset_detect(acb);
		goto out_unlock;
	}
	if (scsi_intstatus & (INT_BUSSERVICE | INT_CMDDONE)) {
		dcb = acb->active_dcb;
		if (!dcb) {
			dprintkl(KERN_DEBUG,
				"Oops: BusService (%04x %02x) w/o ActiveDCB!\n",
				scsi_status, scsi_intstatus);
			goto out_unlock;
		}
		srb = dcb->active_srb;
		if (dcb->flag & ABORT_DEV_) {
			dprintkdbg(DBG_0, "MsgOut Abort Device.....\n");
			enable_msgout_abort(acb, srb);
		}

		/* software sequential machine */
		phase = (u16)srb->scsi_phase;

		/* 
		 * 62037 or 62137
		 * call  dc395x_scsi_phase0[]... "phase entry"
		 * handle every phase before start transfer
		 */
		/* data_out_phase0,	phase:0 */
		/* data_in_phase0,	phase:1 */
		/* command_phase0,	phase:2 */
		/* status_phase0,	phase:3 */
		/* nop0,		phase:4 PH_BUS_FREE .. initial phase */
		/* nop0,		phase:5 PH_BUS_FREE .. initial phase */
		/* msgout_phase0,	phase:6 */
		/* msgin_phase0,	phase:7 */
		dc395x_statev = dc395x_scsi_phase0[phase];
		dc395x_statev(acb, srb, &scsi_status);

		/* 
		 * if there were any exception occurred scsi_status
		 * will be modify to bus free phase new scsi_status
		 * transfer out from ... previous dc395x_statev
		 */
		srb->scsi_phase = scsi_status & PHASEMASK;
		phase = (u16)scsi_status & PHASEMASK;

		/* 
		 * call  dc395x_scsi_phase1[]... "phase entry" handle
		 * every phase to do transfer
		 */
		/* data_out_phase1,	phase:0 */
		/* data_in_phase1,	phase:1 */
		/* command_phase1,	phase:2 */
		/* status_phase1,	phase:3 */
		/* nop1,		phase:4 PH_BUS_FREE .. initial phase */
		/* nop1,		phase:5 PH_BUS_FREE .. initial phase */
		/* msgout_phase1,	phase:6 */
		/* msgin_phase1,	phase:7 */
		dc395x_statev = dc395x_scsi_phase1[phase];
		dc395x_statev(acb, srb, &scsi_status);
	}
      out_unlock:
	DC395x_UNLOCK_IO(acb->scsi_host, flags);
}


static irqreturn_t dc395x_interrupt(int irq, void *dev_id)
{
	struct AdapterCtlBlk *acb = dev_id;
	u16 scsi_status;
	u8 dma_status;
	irqreturn_t handled = IRQ_NONE;

	/*
	 * Check for pending interrupt
	 */
	scsi_status = DC395x_read16(acb, TRM_S1040_SCSI_STATUS);
	dma_status = DC395x_read8(acb, TRM_S1040_DMA_STATUS);
	if (scsi_status & SCSIINTERRUPT) {
		/* interrupt pending - let's process it! */
		dc395x_handle_interrupt(acb, scsi_status);
		handled = IRQ_HANDLED;
	}
	else if (dma_status & 0x20) {
		/* Error from the DMA engine */
		dprintkl(KERN_INFO, "Interrupt from DMA engine: 0x%02x!\n", dma_status);
#if 0
		dprintkl(KERN_INFO, "This means DMA error! Try to handle ...\n");
		if (acb->active_dcb) {
			acb->active_dcb-> flag |= ABORT_DEV_;
			if (acb->active_dcb->active_srb)
				enable_msgout_abort(acb, acb->active_dcb->active_srb);
		}
		DC395x_write8(acb, TRM_S1040_DMA_CONTROL, ABORTXFER | CLRXFIFO);
#else
		dprintkl(KERN_INFO, "Ignoring DMA error (probably a bad thing) ...\n");
		acb = NULL;
#endif
		handled = IRQ_HANDLED;
	}

	return handled;
}


static void msgout_phase0(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		u16 *pscsi_status)
{
	dprintkdbg(DBG_0, "msgout_phase0: (0x%p)\n", srb->cmd);
	if (srb->state & (SRB_UNEXPECT_RESEL + SRB_ABORT_SENT))
		*pscsi_status = PH_BUS_FREE;	/*.. initial phase */

	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
	srb->state &= ~SRB_MSGOUT;
}


static void msgout_phase1(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		u16 *pscsi_status)
{
	u16 i;
	u8 *ptr;
	dprintkdbg(DBG_0, "msgout_phase1: (0x%p)\n", srb->cmd);

	clear_fifo(acb, "msgout_phase1");
	if (!(srb->state & SRB_MSGOUT)) {
		srb->state |= SRB_MSGOUT;
		dprintkl(KERN_DEBUG,
			"msgout_phase1: (0x%p) Phase unexpected\n",
			srb->cmd);	/* So what ? */
	}
	if (!srb->msg_count) {
		dprintkdbg(DBG_0, "msgout_phase1: (0x%p) NOP msg\n",
			srb->cmd);
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, NOP);
		DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);
		/* it's important for atn stop */
		DC395x_write8(acb, TRM_S1040_SCSI_COMMAND, SCMD_FIFO_OUT);
		return;
	}
	ptr = (u8 *)srb->msgout_buf;
	for (i = 0; i < srb->msg_count; i++)
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, *ptr++);
	srb->msg_count = 0;
	if (srb->msgout_buf[0] == ABORT_TASK_SET)
		srb->state = SRB_ABORT_SENT;

	DC395x_write8(acb, TRM_S1040_SCSI_COMMAND, SCMD_FIFO_OUT);
}


static void command_phase0(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		u16 *pscsi_status)
{
	dprintkdbg(DBG_0, "command_phase0: (0x%p)\n", srb->cmd);
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);
}


static void command_phase1(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		u16 *pscsi_status)
{
	struct DeviceCtlBlk *dcb;
	u8 *ptr;
	u16 i;
	dprintkdbg(DBG_0, "command_phase1: (0x%p)\n", srb->cmd);

	clear_fifo(acb, "command_phase1");
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_CLRATN);
	if (!(srb->flag & AUTO_REQSENSE)) {
		ptr = (u8 *)srb->cmd->cmnd;
		for (i = 0; i < srb->cmd->cmd_len; i++) {
			DC395x_write8(acb, TRM_S1040_SCSI_FIFO, *ptr);
			ptr++;
		}
	} else {
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, REQUEST_SENSE);
		dcb = acb->active_dcb;
		/* target id */
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, (dcb->target_lun << 5));
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, 0);
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, 0);
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, SCSI_SENSE_BUFFERSIZE);
		DC395x_write8(acb, TRM_S1040_SCSI_FIFO, 0);
	}
	srb->state |= SRB_COMMAND;
	/* it's important for atn stop */
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);
	/* SCSI command */
	DC395x_write8(acb, TRM_S1040_SCSI_COMMAND, SCMD_FIFO_OUT);
}


/*
 * Verify that the remaining space in the hw sg lists is the same as
 * the count of remaining bytes in srb->total_xfer_length
 */
static void sg_verify_length(struct ScsiReqBlk *srb)
{
	if (debug_enabled(DBG_SG)) {
		unsigned len = 0;
		unsigned idx = srb->sg_index;
		struct SGentry *psge = srb->segment_x + idx;
		for (; idx < srb->sg_count; psge++, idx++)
			len += psge->length;
		if (len != srb->total_xfer_length)
			dprintkdbg(DBG_SG,
			       "Inconsistent SRB S/G lengths (Tot=%i, Count=%i) !!\n",
			       srb->total_xfer_length, len);
	}			       
}


/*
 * Compute the next Scatter Gather list index and adjust its length
 * and address if necessary
 */
static void sg_update_list(struct ScsiReqBlk *srb, u32 left)
{
	u8 idx;
	u32 xferred = srb->total_xfer_length - left; /* bytes transferred */
	struct SGentry *psge = srb->segment_x + srb->sg_index;

	dprintkdbg(DBG_0,
		"sg_update_list: Transferred %i of %i bytes, %i remain\n",
		xferred, srb->total_xfer_length, left);
	if (xferred == 0) {
		/* nothing to update since we did not transfer any data */
		return;
	}

	sg_verify_length(srb);
	srb->total_xfer_length = left;	/* update remaining count */
	for (idx = srb->sg_index; idx < srb->sg_count; idx++) {
		if (xferred >= psge->length) {
			/* Complete SG entries done */
			xferred -= psge->length;
		} else {
			/* Partial SG entry done */
			dma_sync_single_for_cpu(&srb->dcb->acb->dev->dev,
					srb->sg_bus_addr, SEGMENTX_LEN,
					DMA_TO_DEVICE);
			psge->length -= xferred;
			psge->address += xferred;
			srb->sg_index = idx;
			dma_sync_single_for_device(&srb->dcb->acb->dev->dev,
					srb->sg_bus_addr, SEGMENTX_LEN,
					DMA_TO_DEVICE);
			break;
		}
		psge++;
	}
	sg_verify_length(srb);
}


/*
 * We have transferred a single byte (PIO mode?) and need to update
 * the count of bytes remaining (total_xfer_length) and update the sg
 * entry to either point to next byte in the current sg entry, or of
 * already at the end to point to the start of the next sg entry
 */
static void sg_subtract_one(struct ScsiReqBlk *srb)
{
	sg_update_list(srb, srb->total_xfer_length - 1);
}


/* 
 * cleanup_after_transfer
 * 
 * Makes sure, DMA and SCSI engine are empty, after the transfer has finished
 * KG: Currently called from  StatusPhase1 ()
 * Should probably also be called from other places
 * Best might be to call it in DataXXPhase0, if new phase will differ 
 */
static void cleanup_after_transfer(struct AdapterCtlBlk *acb,
		struct ScsiReqBlk *srb)
{
	/*DC395x_write8 (TRM_S1040_DMA_STATUS, FORCEDMACOMP); */
	if (DC395x_read16(acb, TRM_S1040_DMA_COMMAND) & 0x0001) {	/* read */
		if (!(DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT) & 0x40))
			clear_fifo(acb, "cleanup/in");
		if (!(DC395x_read8(acb, TRM_S1040_DMA_FIFOSTAT) & 0x80))
			DC395x_write8(acb, TRM_S1040_DMA_CONTROL, CLRXFIFO);
	} else {		/* write */
		if (!(DC395x_read8(acb, TRM_S1040_DMA_FIFOSTAT) & 0x80))
			DC395x_write8(acb, TRM_S1040_DMA_CONTROL, CLRXFIFO);
		if (!(DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT) & 0x40))
			clear_fifo(acb, "cleanup/out");
	}
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);
}


/*
 * Those no of bytes will be transferred w/ PIO through the SCSI FIFO
 * Seems to be needed for unknown reasons; could be a hardware bug :-(
 */
#define DC395x_LASTPIO 4


static void data_out_phase0(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		u16 *pscsi_status)
{
	struct DeviceCtlBlk *dcb = srb->dcb;
	u16 scsi_status = *pscsi_status;
	u32 d_left_counter = 0;
	dprintkdbg(DBG_0, "data_out_phase0: (0x%p) <%02i-%i>\n",
		srb->cmd, srb->cmd->device->id, (u8)srb->cmd->device->lun);

	/*
	 * KG: We need to drain the buffers before we draw any conclusions!
	 * This means telling the DMA to push the rest into SCSI, telling
	 * SCSI to push the rest to the bus.
	 * However, the device might have been the one to stop us (phase
	 * change), and the data in transit just needs to be accounted so
	 * it can be retransmitted.)
	 */
	/* 
	 * KG: Stop DMA engine pushing more data into the SCSI FIFO
	 * If we need more data, the DMA SG list will be freshly set up, anyway
	 */
	dprintkdbg(DBG_PIO, "data_out_phase0: "
		"DMA{fifocnt=0x%02x fifostat=0x%02x} "
		"SCSI{fifocnt=0x%02x cnt=0x%06x status=0x%04x} total=0x%06x\n",
		DC395x_read8(acb, TRM_S1040_DMA_FIFOCNT),
		DC395x_read8(acb, TRM_S1040_DMA_FIFOSTAT),
		DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT),
		DC395x_read32(acb, TRM_S1040_SCSI_COUNTER), scsi_status,
		srb->total_xfer_length);
	DC395x_write8(acb, TRM_S1040_DMA_CONTROL, STOPDMAXFER | CLRXFIFO);

	if (!(srb->state & SRB_XFERPAD)) {
		if (scsi_status & PARITYERROR)
			srb->status |= PARITY_ERROR;

		/*
		 * KG: Right, we can't just rely on the SCSI_COUNTER, because this
		 * is the no of bytes it got from the DMA engine not the no it 
		 * transferred successfully to the device. (And the difference could
		 * be as much as the FIFO size, I guess ...)
		 */
		if (!(scsi_status & SCSIXFERDONE)) {
			/*
			 * when data transfer from DMA FIFO to SCSI FIFO
			 * if there was some data left in SCSI FIFO
			 */
			d_left_counter =
			    (u32)(DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT) &
				  0x1F);
			if (dcb->sync_period & WIDE_SYNC)
				d_left_counter <<= 1;

			dprintkdbg(DBG_KG, "data_out_phase0: FIFO contains %i %s\n"
				"SCSI{fifocnt=0x%02x cnt=0x%08x} "
				"DMA{fifocnt=0x%04x cnt=0x%02x ctr=0x%08x}\n",
				DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT),
				(dcb->sync_period & WIDE_SYNC) ? "words" : "bytes",
				DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT),
				DC395x_read32(acb, TRM_S1040_SCSI_COUNTER),
				DC395x_read8(acb, TRM_S1040_DMA_FIFOCNT),
				DC395x_read8(acb, TRM_S1040_DMA_FIFOSTAT),
				DC395x_read32(acb, TRM_S1040_DMA_CXCNT));
		}
		/*
		 * calculate all the residue data that not yet tranfered
		 * SCSI transfer counter + left in SCSI FIFO data
		 *
		 * .....TRM_S1040_SCSI_COUNTER (24bits)
		 * The counter always decrement by one for every SCSI byte transfer.
		 * .....TRM_S1040_SCSI_FIFOCNT ( 5bits)
		 * The counter is SCSI FIFO offset counter (in units of bytes or! words)
		 */
		if (srb->total_xfer_length > DC395x_LASTPIO)
			d_left_counter +=
			    DC395x_read32(acb, TRM_S1040_SCSI_COUNTER);

		/* Is this a good idea? */
		/*clear_fifo(acb, "DOP1"); */
		/* KG: What is this supposed to be useful for? WIDE padding stuff? */
		if (d_left_counter == 1 && dcb->sync_period & WIDE_SYNC
		    && scsi_bufflen(srb->cmd) % 2) {
			d_left_counter = 0;
			dprintkl(KERN_INFO,
				"data_out_phase0: Discard 1 byte (0x%02x)\n",
				scsi_status);
		}
		/*
		 * KG: Oops again. Same thinko as above: The SCSI might have been
		 * faster than the DMA engine, so that it ran out of data.
		 * In that case, we have to do just nothing! 
		 * But: Why the interrupt: No phase change. No XFERCNT_2_ZERO. Or?
		 */
		/*
		 * KG: This is nonsense: We have been WRITING data to the bus
		 * If the SCSI engine has no bytes left, how should the DMA engine?
		 */
		if (d_left_counter == 0) {
			srb->total_xfer_length = 0;
		} else {
			/*
			 * if transfer not yet complete
			 * there were some data residue in SCSI FIFO or
			 * SCSI transfer counter not empty
			 */
			long oldxferred =
			    srb->total_xfer_length - d_left_counter;
			const int diff =
			    (dcb->sync_period & WIDE_SYNC) ? 2 : 1;
			sg_update_list(srb, d_left_counter);
			/* KG: Most ugly hack! Apparently, this works around a chip bug */
			if ((srb->segment_x[srb->sg_index].length ==
			     diff && scsi_sg_count(srb->cmd))
			    || ((oldxferred & ~PAGE_MASK) ==
				(PAGE_SIZE - diff))
			    ) {
				dprintkl(KERN_INFO, "data_out_phase0: "
					"Work around chip bug (%i)?\n", diff);
				d_left_counter =
				    srb->total_xfer_length - diff;
				sg_update_list(srb, d_left_counter);
				/*srb->total_xfer_length -= diff; */
				/*srb->virt_addr += diff; */
				/*if (srb->cmd->use_sg) */
				/*      srb->sg_index++; */
			}
		}
	}
	if ((*pscsi_status & PHASEMASK) != PH_DATA_OUT) {
		cleanup_after_transfer(acb, srb);
	}
}


static void data_out_phase1(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		u16 *pscsi_status)
{
	dprintkdbg(DBG_0, "data_out_phase1: (0x%p) <%02i-%i>\n",
		srb->cmd, srb->cmd->device->id, (u8)srb->cmd->device->lun);
	clear_fifo(acb, "data_out_phase1");
	/* do prepare before transfer when data out phase */
	data_io_transfer(acb, srb, XFERDATAOUT);
}

static void data_in_phase0(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		u16 *pscsi_status)
{
	u16 scsi_status = *pscsi_status;

	dprintkdbg(DBG_0, "data_in_phase0: (0x%p) <%02i-%i>\n",
		srb->cmd, srb->cmd->device->id, (u8)srb->cmd->device->lun);

	/*
	 * KG: DataIn is much more tricky than DataOut. When the device is finished
	 * and switches to another phase, the SCSI engine should be finished too.
	 * But: There might still be bytes left in its FIFO to be fetched by the DMA
	 * engine and transferred to memory.
	 * We should wait for the FIFOs to be emptied by that (is there any way to 
	 * enforce this?) and then stop the DMA engine, because it might think, that
	 * there are more bytes to follow. Yes, the device might disconnect prior to
	 * having all bytes transferred! 
	 * Also we should make sure that all data from the DMA engine buffer's really
	 * made its way to the system memory! Some documentation on this would not
	 * seem to be a bad idea, actually.
	 */
	if (!(srb->state & SRB_XFERPAD)) {
		u32 d_left_counter;
		unsigned int sc, fc;

		if (scsi_status & PARITYERROR) {
			dprintkl(KERN_INFO, "data_in_phase0: (0x%p) "
				"Parity Error\n", srb->cmd);
			srb->status |= PARITY_ERROR;
		}
		/*
		 * KG: We should wait for the DMA FIFO to be empty ...
		 * but: it would be better to wait first for the SCSI FIFO and then the
		 * the DMA FIFO to become empty? How do we know, that the device not already
		 * sent data to the FIFO in a MsgIn phase, eg.?
		 */
		if (!(DC395x_read8(acb, TRM_S1040_DMA_FIFOSTAT) & 0x80)) {
#if 0
			int ctr = 6000000;
			dprintkl(KERN_DEBUG,
				"DIP0: Wait for DMA FIFO to flush ...\n");
			/*DC395x_write8  (TRM_S1040_DMA_CONTROL, STOPDMAXFER); */
			/*DC395x_write32 (TRM_S1040_SCSI_COUNTER, 7); */
			/*DC395x_write8  (TRM_S1040_SCSI_COMMAND, SCMD_DMA_IN); */
			while (!
			       (DC395x_read16(acb, TRM_S1040_DMA_FIFOSTAT) &
				0x80) && --ctr);
			if (ctr < 6000000 - 1)
				dprintkl(KERN_DEBUG
				       "DIP0: Had to wait for DMA ...\n");
			if (!ctr)
				dprintkl(KERN_ERR,
				       "Deadlock in DIP0 waiting for DMA FIFO empty!!\n");
			/*DC395x_write32 (TRM_S1040_SCSI_COUNTER, 0); */
#endif
			dprintkdbg(DBG_KG, "data_in_phase0: "
				"DMA{fifocnt=0x%02x fifostat=0x%02x}\n",
				DC395x_read8(acb, TRM_S1040_DMA_FIFOCNT),
				DC395x_read8(acb, TRM_S1040_DMA_FIFOSTAT));
		}
		/* Now: Check remainig data: The SCSI counters should tell us ... */
		sc = DC395x_read32(acb, TRM_S1040_SCSI_COUNTER);
		fc = DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT);
		d_left_counter = sc + ((fc & 0x1f)
		       << ((srb->dcb->sync_period & WIDE_SYNC) ? 1 :
			   0));
		dprintkdbg(DBG_KG, "data_in_phase0: "
			"SCSI{fifocnt=0x%02x%s ctr=0x%08x} "
			"DMA{fifocnt=0x%02x fifostat=0x%02x ctr=0x%08x} "
			"Remain{totxfer=%i scsi_fifo+ctr=%i}\n",
			fc,
			(srb->dcb->sync_period & WIDE_SYNC) ? "words" : "bytes",
			sc,
			fc,
			DC395x_read8(acb, TRM_S1040_DMA_FIFOSTAT),
			DC395x_read32(acb, TRM_S1040_DMA_CXCNT),
			srb->total_xfer_length, d_left_counter);
#if DC395x_LASTPIO
		/* KG: Less than or equal to 4 bytes can not be transferred via DMA, it seems. */
		if (d_left_counter
		    && srb->total_xfer_length <= DC395x_LASTPIO) {
			size_t left_io = srb->total_xfer_length;

			/*u32 addr = (srb->segment_x[srb->sg_index].address); */
			/*sg_update_list (srb, d_left_counter); */
			dprintkdbg(DBG_PIO, "data_in_phase0: PIO (%i %s) "
				   "for remaining %i bytes:",
				fc & 0x1f,
				(srb->dcb->sync_period & WIDE_SYNC) ?
				    "words" : "bytes",
				srb->total_xfer_length);
			if (srb->dcb->sync_period & WIDE_SYNC)
				DC395x_write8(acb, TRM_S1040_SCSI_CONFIG2,
					      CFG2_WIDEFIFO);
			while (left_io) {
				unsigned char *virt, *base = NULL;
				unsigned long flags = 0;
				size_t len = left_io;
				size_t offset = srb->request_length - left_io;

				local_irq_save(flags);
				/* Assumption: it's inside one page as it's at most 4 bytes and
				   I just assume it's on a 4-byte boundary */
				base = scsi_kmap_atomic_sg(scsi_sglist(srb->cmd),
							   srb->sg_count, &offset, &len);
				virt = base + offset;

				left_io -= len;

				while (len) {
					u8 byte;
					byte = DC395x_read8(acb, TRM_S1040_SCSI_FIFO);
					*virt++ = byte;

					if (debug_enabled(DBG_PIO))
						printk(" %02x", byte);

					d_left_counter--;
					sg_subtract_one(srb);

					len--;

					fc = DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT);

					if (fc == 0x40) {
						left_io = 0;
						break;
					}
				}

				WARN_ON((fc != 0x40) == !d_left_counter);

				if (fc == 0x40 && (srb->dcb->sync_period & WIDE_SYNC)) {
					/* Read the last byte ... */
					if (srb->total_xfer_length > 0) {
						u8 byte = DC395x_read8(acb, TRM_S1040_SCSI_FIFO);

						*virt++ = byte;
						srb->total_xfer_length--;
						if (debug_enabled(DBG_PIO))
							printk(" %02x", byte);
					}

					DC395x_write8(acb, TRM_S1040_SCSI_CONFIG2, 0);
				}

				scsi_kunmap_atomic_sg(base);
				local_irq_restore(flags);
			}
			/*printk(" %08x", *(u32*)(bus_to_virt (addr))); */
			/*srb->total_xfer_length = 0; */
			if (debug_enabled(DBG_PIO))
				printk("\n");
		}
#endif				/* DC395x_LASTPIO */

#if 0
		/*
		 * KG: This was in DATAOUT. Does it also belong here?
		 * Nobody seems to know what counter and fifo_cnt count exactly ...
		 */
		if (!(scsi_status & SCSIXFERDONE)) {
			/*
			 * when data transfer from DMA FIFO to SCSI FIFO
			 * if there was some data left in SCSI FIFO
			 */
			d_left_counter =
			    (u32)(DC395x_read8(acb, TRM_S1040_SCSI_FIFOCNT) &
				  0x1F);
			if (srb->dcb->sync_period & WIDE_SYNC)
				d_left_counter <<= 1;
			/*
			 * if WIDE scsi SCSI FIFOCNT unit is word !!!
			 * so need to *= 2
			 * KG: Seems to be correct ...
			 */
		}
#endif
		/* KG: This should not be needed any more! */
		if (d_left_counter == 0
		    || (scsi_status & SCSIXFERCNT_2_ZERO)) {
#if 0
			int ctr = 6000000;
			u8 TempDMAstatus;
			do {
				TempDMAstatus =
				    DC395x_read8(acb, TRM_S1040_DMA_STATUS);
			} while (!(TempDMAstatus & DMAXFERCOMP) && --ctr);
			if (!ctr)
				dprintkl(KERN_ERR,
				       "Deadlock in DataInPhase0 waiting for DMA!!\n");
			srb->total_xfer_length = 0;
#endif
			srb->total_xfer_length = d_left_counter;
		} else {	/* phase changed */
			/*
			 * parsing the case:
			 * when a transfer not yet complete 
			 * but be disconnected by target
			 * if transfer not yet complete
			 * there were some data residue in SCSI FIFO or
			 * SCSI transfer counter not empty
			 */
			sg_update_list(srb, d_left_counter);
		}
	}
	/* KG: The target may decide to disconnect: Empty FIFO before! */
	if ((*pscsi_status & PHASEMASK) != PH_DATA_IN) {
		cleanup_after_transfer(acb, srb);
	}
}


static void data_in_phase1(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		u16 *pscsi_status)
{
	dprintkdbg(DBG_0, "data_in_phase1: (0x%p) <%02i-%i>\n",
		srb->cmd, srb->cmd->device->id, (u8)srb->cmd->device->lun);
	data_io_transfer(acb, srb, XFERDATAIN);
}


static void data_io_transfer(struct AdapterCtlBlk *acb, 
		struct ScsiReqBlk *srb, u16 io_dir)
{
	struct DeviceCtlBlk *dcb = srb->dcb;
	u8 bval;
	dprintkdbg(DBG_0,
		"data_io_transfer: (0x%p) <%02i-%i> %c len=%i, sg=(%i/%i)\n",
		srb->cmd, srb->cmd->device->id, (u8)srb->cmd->device->lun,
		((io_dir & DMACMD_DIR) ? 'r' : 'w'),
		srb->total_xfer_length, srb->sg_index, srb->sg_count);
	if (srb == acb->tmp_srb)
		dprintkl(KERN_ERR, "data_io_transfer: Using tmp_srb!\n");
	if (srb->sg_index >= srb->sg_count) {
		/* can't happen? out of bounds error */
		return;
	}

	if (srb->total_xfer_length > DC395x_LASTPIO) {
		u8 dma_status = DC395x_read8(acb, TRM_S1040_DMA_STATUS);
		/*
		 * KG: What should we do: Use SCSI Cmd 0x90/0x92?
		 * Maybe, even ABORTXFER would be appropriate
		 */
		if (dma_status & XFERPENDING) {
			dprintkl(KERN_DEBUG, "data_io_transfer: Xfer pending! "
				"Expect trouble!\n");
			dump_register_info(acb, dcb, srb);
			DC395x_write8(acb, TRM_S1040_DMA_CONTROL, CLRXFIFO);
		}
		/* clear_fifo(acb, "IO"); */
		/* 
		 * load what physical address of Scatter/Gather list table
		 * want to be transfer
		 */
		srb->state |= SRB_DATA_XFER;
		DC395x_write32(acb, TRM_S1040_DMA_XHIGHADDR, 0);
		if (scsi_sg_count(srb->cmd)) {	/* with S/G */
			io_dir |= DMACMD_SG;
			DC395x_write32(acb, TRM_S1040_DMA_XLOWADDR,
				       srb->sg_bus_addr +
				       sizeof(struct SGentry) *
				       srb->sg_index);
			/* load how many bytes in the sg list table */
			DC395x_write32(acb, TRM_S1040_DMA_XCNT,
				       ((u32)(srb->sg_count -
					      srb->sg_index) << 3));
		} else {	/* without S/G */
			io_dir &= ~DMACMD_SG;
			DC395x_write32(acb, TRM_S1040_DMA_XLOWADDR,
				       srb->segment_x[0].address);
			DC395x_write32(acb, TRM_S1040_DMA_XCNT,
				       srb->segment_x[0].length);
		}
		/* load total transfer length (24bits) max value 16Mbyte */
		DC395x_write32(acb, TRM_S1040_SCSI_COUNTER,
			       srb->total_xfer_length);
		DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
		if (io_dir & DMACMD_DIR) {	/* read */
			DC395x_write8(acb, TRM_S1040_SCSI_COMMAND,
				      SCMD_DMA_IN);
			DC395x_write16(acb, TRM_S1040_DMA_COMMAND, io_dir);
		} else {
			DC395x_write16(acb, TRM_S1040_DMA_COMMAND, io_dir);
			DC395x_write8(acb, TRM_S1040_SCSI_COMMAND,
				      SCMD_DMA_OUT);
		}

	}
#if DC395x_LASTPIO
	else if (srb->total_xfer_length > 0) {	/* The last four bytes: Do PIO */
		/* 
		 * load what physical address of Scatter/Gather list table
		 * want to be transfer
		 */
		srb->state |= SRB_DATA_XFER;
		/* load total transfer length (24bits) max value 16Mbyte */
		DC395x_write32(acb, TRM_S1040_SCSI_COUNTER,
			       srb->total_xfer_length);
		DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
		if (io_dir & DMACMD_DIR) {	/* read */
			DC395x_write8(acb, TRM_S1040_SCSI_COMMAND,
				      SCMD_FIFO_IN);
		} else {	/* write */
			int ln = srb->total_xfer_length;
			size_t left_io = srb->total_xfer_length;

			if (srb->dcb->sync_period & WIDE_SYNC)
				DC395x_write8(acb, TRM_S1040_SCSI_CONFIG2,
				     CFG2_WIDEFIFO);

			while (left_io) {
				unsigned char *virt, *base = NULL;
				unsigned long flags = 0;
				size_t len = left_io;
				size_t offset = srb->request_length - left_io;

				local_irq_save(flags);
				/* Again, max 4 bytes */
				base = scsi_kmap_atomic_sg(scsi_sglist(srb->cmd),
							   srb->sg_count, &offset, &len);
				virt = base + offset;

				left_io -= len;

				while (len--) {
					if (debug_enabled(DBG_PIO))
						printk(" %02x", *virt);

					DC395x_write8(acb, TRM_S1040_SCSI_FIFO, *virt++);

					sg_subtract_one(srb);
				}

				scsi_kunmap_atomic_sg(base);
				local_irq_restore(flags);
			}
			if (srb->dcb->sync_period & WIDE_SYNC) {
				if (ln % 2) {
					DC395x_write8(acb, TRM_S1040_SCSI_FIFO, 0);
					if (debug_enabled(DBG_PIO))
						printk(" |00");
				}
				DC395x_write8(acb, TRM_S1040_SCSI_CONFIG2, 0);
			}
			/*DC395x_write32(acb, TRM_S1040_SCSI_COUNTER, ln); */
			if (debug_enabled(DBG_PIO))
				printk("\n");
			DC395x_write8(acb, TRM_S1040_SCSI_COMMAND,
					  SCMD_FIFO_OUT);
		}
	}
#endif				/* DC395x_LASTPIO */
	else {		/* xfer pad */
		if (srb->sg_count) {
			srb->adapter_status = H_OVER_UNDER_RUN;
			srb->status |= OVER_RUN;
		}
		/*
		 * KG: despite the fact that we are using 16 bits I/O ops
		 * the SCSI FIFO is only 8 bits according to the docs
		 * (we can set bit 1 in 0x8f to serialize FIFO access ...)
		 */
		if (dcb->sync_period & WIDE_SYNC) {
			DC395x_write32(acb, TRM_S1040_SCSI_COUNTER, 2);
			DC395x_write8(acb, TRM_S1040_SCSI_CONFIG2,
				      CFG2_WIDEFIFO);
			if (io_dir & DMACMD_DIR) {
				DC395x_read8(acb, TRM_S1040_SCSI_FIFO);
				DC395x_read8(acb, TRM_S1040_SCSI_FIFO);
			} else {
				/* Danger, Robinson: If you find KGs
				 * scattered over the wide disk, the driver
				 * or chip is to blame :-( */
				DC395x_write8(acb, TRM_S1040_SCSI_FIFO, 'K');
				DC395x_write8(acb, TRM_S1040_SCSI_FIFO, 'G');
			}
			DC395x_write8(acb, TRM_S1040_SCSI_CONFIG2, 0);
		} else {
			DC395x_write32(acb, TRM_S1040_SCSI_COUNTER, 1);
			/* Danger, Robinson: If you find a collection of Ks on your disk
			 * something broke :-( */
			if (io_dir & DMACMD_DIR)
				DC395x_read8(acb, TRM_S1040_SCSI_FIFO);
			else
				DC395x_write8(acb, TRM_S1040_SCSI_FIFO, 'K');
		}
		srb->state |= SRB_XFERPAD;
		DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
		/* SCSI command */
		bval = (io_dir & DMACMD_DIR) ? SCMD_FIFO_IN : SCMD_FIFO_OUT;
		DC395x_write8(acb, TRM_S1040_SCSI_COMMAND, bval);
	}
}


static void status_phase0(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		u16 *pscsi_status)
{
	dprintkdbg(DBG_0, "status_phase0: (0x%p) <%02i-%i>\n",
		srb->cmd, srb->cmd->device->id, (u8)srb->cmd->device->lun);
	srb->target_status = DC395x_read8(acb, TRM_S1040_SCSI_FIFO);
	srb->end_message = DC395x_read8(acb, TRM_S1040_SCSI_FIFO);	/* get message */
	srb->state = SRB_COMPLETED;
	*pscsi_status = PH_BUS_FREE;	/*.. initial phase */
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
	DC395x_write8(acb, TRM_S1040_SCSI_COMMAND, SCMD_MSGACCEPT);
}


static void status_phase1(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		u16 *pscsi_status)
{
	dprintkdbg(DBG_0, "status_phase1: (0x%p) <%02i-%i>\n",
		srb->cmd, srb->cmd->device->id, (u8)srb->cmd->device->lun);
	srb->state = SRB_STATUS;
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
	DC395x_write8(acb, TRM_S1040_SCSI_COMMAND, SCMD_COMP);
}


/* Check if the message is complete */
static inline u8 msgin_completed(u8 * msgbuf, u32 len)
{
	if (*msgbuf == EXTENDED_MESSAGE) {
		if (len < 2)
			return 0;
		if (len < msgbuf[1] + 2)
			return 0;
	} else if (*msgbuf >= 0x20 && *msgbuf <= 0x2f)	/* two byte messages */
		if (len < 2)
			return 0;
	return 1;
}

/* reject_msg */
static inline void msgin_reject(struct AdapterCtlBlk *acb,
		struct ScsiReqBlk *srb)
{
	srb->msgout_buf[0] = MESSAGE_REJECT;
	srb->msg_count = 1;
	DC395x_ENABLE_MSGOUT;
	srb->state &= ~SRB_MSGIN;
	srb->state |= SRB_MSGOUT;
	dprintkl(KERN_INFO, "msgin_reject: 0x%02x <%02i-%i>\n",
		srb->msgin_buf[0],
		srb->dcb->target_id, srb->dcb->target_lun);
}


static struct ScsiReqBlk *msgin_qtag(struct AdapterCtlBlk *acb,
		struct DeviceCtlBlk *dcb, u8 tag)
{
	struct ScsiReqBlk *srb = NULL;
	struct ScsiReqBlk *i;
	dprintkdbg(DBG_0, "msgin_qtag: (0x%p) tag=%i srb=%p\n",
		   srb->cmd, tag, srb);

	if (!(dcb->tag_mask & (1 << tag)))
		dprintkl(KERN_DEBUG,
			"msgin_qtag: tag_mask=0x%08x does not reserve tag %i!\n",
			dcb->tag_mask, tag);

	if (list_empty(&dcb->srb_going_list))
		goto mingx0;
	list_for_each_entry(i, &dcb->srb_going_list, list) {
		if (i->tag_number == tag) {
			srb = i;
			break;
		}
	}
	if (!srb)
		goto mingx0;

	dprintkdbg(DBG_0, "msgin_qtag: (0x%p) <%02i-%i>\n",
		srb->cmd, srb->dcb->target_id, srb->dcb->target_lun);
	if (dcb->flag & ABORT_DEV_) {
		/*srb->state = SRB_ABORT_SENT; */
		enable_msgout_abort(acb, srb);
	}

	if (!(srb->state & SRB_DISCONNECT))
		goto mingx0;

	memcpy(srb->msgin_buf, dcb->active_srb->msgin_buf, acb->msg_len);
	srb->state |= dcb->active_srb->state;
	srb->state |= SRB_DATA_XFER;
	dcb->active_srb = srb;
	/* How can we make the DORS happy? */
	return srb;

      mingx0:
	srb = acb->tmp_srb;
	srb->state = SRB_UNEXPECT_RESEL;
	dcb->active_srb = srb;
	srb->msgout_buf[0] = ABORT_TASK;
	srb->msg_count = 1;
	DC395x_ENABLE_MSGOUT;
	dprintkl(KERN_DEBUG, "msgin_qtag: Unknown tag %i - abort\n", tag);
	return srb;
}


static inline void reprogram_regs(struct AdapterCtlBlk *acb,
		struct DeviceCtlBlk *dcb)
{
	DC395x_write8(acb, TRM_S1040_SCSI_TARGETID, dcb->target_id);
	DC395x_write8(acb, TRM_S1040_SCSI_SYNC, dcb->sync_period);
	DC395x_write8(acb, TRM_S1040_SCSI_OFFSET, dcb->sync_offset);
	set_xfer_rate(acb, dcb);
}


/* set async transfer mode */
static void msgin_set_async(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb)
{
	struct DeviceCtlBlk *dcb = srb->dcb;
	dprintkl(KERN_DEBUG, "msgin_set_async: No sync transfers <%02i-%i>\n",
		dcb->target_id, dcb->target_lun);

	dcb->sync_mode &= ~(SYNC_NEGO_ENABLE);
	dcb->sync_mode |= SYNC_NEGO_DONE;
	/*dcb->sync_period &= 0; */
	dcb->sync_offset = 0;
	dcb->min_nego_period = 200 >> 2;	/* 200ns <=> 5 MHz */
	srb->state &= ~SRB_DO_SYNC_NEGO;
	reprogram_regs(acb, dcb);
	if ((dcb->sync_mode & WIDE_NEGO_ENABLE)
	    && !(dcb->sync_mode & WIDE_NEGO_DONE)) {
		build_wdtr(acb, dcb, srb);
		DC395x_ENABLE_MSGOUT;
		dprintkdbg(DBG_0, "msgin_set_async(rej): Try WDTR anyway\n");
	}
}


/* set sync transfer mode */
static void msgin_set_sync(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb)
{
	struct DeviceCtlBlk *dcb = srb->dcb;
	u8 bval;
	int fact;
	dprintkdbg(DBG_1, "msgin_set_sync: <%02i> Sync: %ins "
		"(%02i.%01i MHz) Offset %i\n",
		dcb->target_id, srb->msgin_buf[3] << 2,
		(250 / srb->msgin_buf[3]),
		((250 % srb->msgin_buf[3]) * 10) / srb->msgin_buf[3],
		srb->msgin_buf[4]);

	if (srb->msgin_buf[4] > 15)
		srb->msgin_buf[4] = 15;
	if (!(dcb->dev_mode & NTC_DO_SYNC_NEGO))
		dcb->sync_offset = 0;
	else if (dcb->sync_offset == 0)
		dcb->sync_offset = srb->msgin_buf[4];
	if (srb->msgin_buf[4] > dcb->sync_offset)
		srb->msgin_buf[4] = dcb->sync_offset;
	else
		dcb->sync_offset = srb->msgin_buf[4];
	bval = 0;
	while (bval < 7 && (srb->msgin_buf[3] > clock_period[bval]
			    || dcb->min_nego_period >
			    clock_period[bval]))
		bval++;
	if (srb->msgin_buf[3] < clock_period[bval])
		dprintkl(KERN_INFO,
			"msgin_set_sync: Increase sync nego period to %ins\n",
			clock_period[bval] << 2);
	srb->msgin_buf[3] = clock_period[bval];
	dcb->sync_period &= 0xf0;
	dcb->sync_period |= ALT_SYNC | bval;
	dcb->min_nego_period = srb->msgin_buf[3];

	if (dcb->sync_period & WIDE_SYNC)
		fact = 500;
	else
		fact = 250;

	dprintkl(KERN_INFO,
		"Target %02i: %s Sync: %ins Offset %i (%02i.%01i MB/s)\n",
		dcb->target_id, (fact == 500) ? "Wide16" : "",
		dcb->min_nego_period << 2, dcb->sync_offset,
		(fact / dcb->min_nego_period),
		((fact % dcb->min_nego_period) * 10 +
		dcb->min_nego_period / 2) / dcb->min_nego_period);

	if (!(srb->state & SRB_DO_SYNC_NEGO)) {
		/* Reply with corrected SDTR Message */
		dprintkl(KERN_DEBUG, "msgin_set_sync: answer w/%ins %i\n",
			srb->msgin_buf[3] << 2, srb->msgin_buf[4]);

		memcpy(srb->msgout_buf, srb->msgin_buf, 5);
		srb->msg_count = 5;
		DC395x_ENABLE_MSGOUT;
		dcb->sync_mode |= SYNC_NEGO_DONE;
	} else {
		if ((dcb->sync_mode & WIDE_NEGO_ENABLE)
		    && !(dcb->sync_mode & WIDE_NEGO_DONE)) {
			build_wdtr(acb, dcb, srb);
			DC395x_ENABLE_MSGOUT;
			dprintkdbg(DBG_0, "msgin_set_sync: Also try WDTR\n");
		}
	}
	srb->state &= ~SRB_DO_SYNC_NEGO;
	dcb->sync_mode |= SYNC_NEGO_DONE | SYNC_NEGO_ENABLE;

	reprogram_regs(acb, dcb);
}


static inline void msgin_set_nowide(struct AdapterCtlBlk *acb,
		struct ScsiReqBlk *srb)
{
	struct DeviceCtlBlk *dcb = srb->dcb;
	dprintkdbg(DBG_1, "msgin_set_nowide: <%02i>\n", dcb->target_id);

	dcb->sync_period &= ~WIDE_SYNC;
	dcb->sync_mode &= ~(WIDE_NEGO_ENABLE);
	dcb->sync_mode |= WIDE_NEGO_DONE;
	srb->state &= ~SRB_DO_WIDE_NEGO;
	reprogram_regs(acb, dcb);
	if ((dcb->sync_mode & SYNC_NEGO_ENABLE)
	    && !(dcb->sync_mode & SYNC_NEGO_DONE)) {
		build_sdtr(acb, dcb, srb);
		DC395x_ENABLE_MSGOUT;
		dprintkdbg(DBG_0, "msgin_set_nowide: Rejected. Try SDTR anyway\n");
	}
}

static void msgin_set_wide(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb)
{
	struct DeviceCtlBlk *dcb = srb->dcb;
	u8 wide = (dcb->dev_mode & NTC_DO_WIDE_NEGO
		   && acb->config & HCC_WIDE_CARD) ? 1 : 0;
	dprintkdbg(DBG_1, "msgin_set_wide: <%02i>\n", dcb->target_id);

	if (srb->msgin_buf[3] > wide)
		srb->msgin_buf[3] = wide;
	/* Completed */
	if (!(srb->state & SRB_DO_WIDE_NEGO)) {
		dprintkl(KERN_DEBUG,
			"msgin_set_wide: Wide nego initiated <%02i>\n",
			dcb->target_id);
		memcpy(srb->msgout_buf, srb->msgin_buf, 4);
		srb->msg_count = 4;
		srb->state |= SRB_DO_WIDE_NEGO;
		DC395x_ENABLE_MSGOUT;
	}

	dcb->sync_mode |= (WIDE_NEGO_ENABLE | WIDE_NEGO_DONE);
	if (srb->msgin_buf[3] > 0)
		dcb->sync_period |= WIDE_SYNC;
	else
		dcb->sync_period &= ~WIDE_SYNC;
	srb->state &= ~SRB_DO_WIDE_NEGO;
	/*dcb->sync_mode &= ~(WIDE_NEGO_ENABLE+WIDE_NEGO_DONE); */
	dprintkdbg(DBG_1,
		"msgin_set_wide: Wide (%i bit) negotiated <%02i>\n",
		(8 << srb->msgin_buf[3]), dcb->target_id);
	reprogram_regs(acb, dcb);
	if ((dcb->sync_mode & SYNC_NEGO_ENABLE)
	    && !(dcb->sync_mode & SYNC_NEGO_DONE)) {
		build_sdtr(acb, dcb, srb);
		DC395x_ENABLE_MSGOUT;
		dprintkdbg(DBG_0, "msgin_set_wide: Also try SDTR.\n");
	}
}


/*
 * extended message codes:
 *
 *	code	description
 *
 *	02h	Reserved
 *	00h	MODIFY DATA  POINTER
 *	01h	SYNCHRONOUS DATA TRANSFER REQUEST
 *	03h	WIDE DATA TRANSFER REQUEST
 *   04h - 7Fh	Reserved
 *   80h - FFh	Vendor specific
 */
static void msgin_phase0(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		u16 *pscsi_status)
{
	struct DeviceCtlBlk *dcb = acb->active_dcb;
	dprintkdbg(DBG_0, "msgin_phase0: (0x%p)\n", srb->cmd);

	srb->msgin_buf[acb->msg_len++] = DC395x_read8(acb, TRM_S1040_SCSI_FIFO);
	if (msgin_completed(srb->msgin_buf, acb->msg_len)) {
		/* Now eval the msg */
		switch (srb->msgin_buf[0]) {
		case DISCONNECT:
			srb->state = SRB_DISCONNECT;
			break;

		case SIMPLE_QUEUE_TAG:
		case HEAD_OF_QUEUE_TAG:
		case ORDERED_QUEUE_TAG:
			srb =
			    msgin_qtag(acb, dcb,
					      srb->msgin_buf[1]);
			break;

		case MESSAGE_REJECT:
			DC395x_write16(acb, TRM_S1040_SCSI_CONTROL,
				       DO_CLRATN | DO_DATALATCH);
			/* A sync nego message was rejected ! */
			if (srb->state & SRB_DO_SYNC_NEGO) {
				msgin_set_async(acb, srb);
				break;
			}
			/* A wide nego message was rejected ! */
			if (srb->state & SRB_DO_WIDE_NEGO) {
				msgin_set_nowide(acb, srb);
				break;
			}
			enable_msgout_abort(acb, srb);
			/*srb->state |= SRB_ABORT_SENT */
			break;

		case EXTENDED_MESSAGE:
			/* SDTR */
			if (srb->msgin_buf[1] == 3
			    && srb->msgin_buf[2] == EXTENDED_SDTR) {
				msgin_set_sync(acb, srb);
				break;
			}
			/* WDTR */
			if (srb->msgin_buf[1] == 2
			    && srb->msgin_buf[2] == EXTENDED_WDTR
			    && srb->msgin_buf[3] <= 2) { /* sanity check ... */
				msgin_set_wide(acb, srb);
				break;
			}
			msgin_reject(acb, srb);
			break;

		case IGNORE_WIDE_RESIDUE:
			/* Discard  wide residual */
			dprintkdbg(DBG_0, "msgin_phase0: Ignore Wide Residual!\n");
			break;

		case COMMAND_COMPLETE:
			/* nothing has to be done */
			break;

		case SAVE_POINTERS:
			/*
			 * SAVE POINTER may be ignored as we have the struct
			 * ScsiReqBlk* associated with the scsi command.
			 */
			dprintkdbg(DBG_0, "msgin_phase0: (0x%p) "
				"SAVE POINTER rem=%i Ignore\n",
				srb->cmd, srb->total_xfer_length);
			break;

		case RESTORE_POINTERS:
			dprintkdbg(DBG_0, "msgin_phase0: RESTORE POINTER. Ignore\n");
			break;

		case ABORT:
			dprintkdbg(DBG_0, "msgin_phase0: (0x%p) "
				"<%02i-%i> ABORT msg\n",
				srb->cmd, dcb->target_id,
				dcb->target_lun);
			dcb->flag |= ABORT_DEV_;
			enable_msgout_abort(acb, srb);
			break;

		default:
			/* reject unknown messages */
			if (srb->msgin_buf[0] & IDENTIFY_BASE) {
				dprintkdbg(DBG_0, "msgin_phase0: Identify msg\n");
				srb->msg_count = 1;
				srb->msgout_buf[0] = dcb->identify_msg;
				DC395x_ENABLE_MSGOUT;
				srb->state |= SRB_MSGOUT;
				/*break; */
			}
			msgin_reject(acb, srb);
		}

		/* Clear counter and MsgIn state */
		srb->state &= ~SRB_MSGIN;
		acb->msg_len = 0;
	}
	*pscsi_status = PH_BUS_FREE;
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important ... you know! */
	DC395x_write8(acb, TRM_S1040_SCSI_COMMAND, SCMD_MSGACCEPT);
}


static void msgin_phase1(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		u16 *pscsi_status)
{
	dprintkdbg(DBG_0, "msgin_phase1: (0x%p)\n", srb->cmd);
	clear_fifo(acb, "msgin_phase1");
	DC395x_write32(acb, TRM_S1040_SCSI_COUNTER, 1);
	if (!(srb->state & SRB_MSGIN)) {
		srb->state &= ~SRB_DISCONNECT;
		srb->state |= SRB_MSGIN;
	}
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
	/* SCSI command */
	DC395x_write8(acb, TRM_S1040_SCSI_COMMAND, SCMD_FIFO_IN);
}


static void nop0(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		u16 *pscsi_status)
{
}


static void nop1(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb,
		u16 *pscsi_status)
{
}


static void set_xfer_rate(struct AdapterCtlBlk *acb, struct DeviceCtlBlk *dcb)
{
	struct DeviceCtlBlk *i;

	/* set all lun device's  period, offset */
	if (dcb->identify_msg & 0x07)
		return;

	if (acb->scan_devices) {
		current_sync_offset = dcb->sync_offset;
		return;
	}

	list_for_each_entry(i, &acb->dcb_list, list)
		if (i->target_id == dcb->target_id) {
			i->sync_period = dcb->sync_period;
			i->sync_offset = dcb->sync_offset;
			i->sync_mode = dcb->sync_mode;
			i->min_nego_period = dcb->min_nego_period;
		}
}


static void disconnect(struct AdapterCtlBlk *acb)
{
	struct DeviceCtlBlk *dcb = acb->active_dcb;
	struct ScsiReqBlk *srb;

	if (!dcb) {
		dprintkl(KERN_ERR, "disconnect: No such device\n");
		udelay(500);
		/* Suspend queue for a while */
		acb->last_reset =
		    jiffies + HZ / 2 +
		    HZ * acb->eeprom.delay_time;
		clear_fifo(acb, "disconnectEx");
		DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_HWRESELECT);
		return;
	}
	srb = dcb->active_srb;
	acb->active_dcb = NULL;
	dprintkdbg(DBG_0, "disconnect: (0x%p)\n", srb->cmd);

	srb->scsi_phase = PH_BUS_FREE;	/* initial phase */
	clear_fifo(acb, "disconnect");
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_HWRESELECT);
	if (srb->state & SRB_UNEXPECT_RESEL) {
		dprintkl(KERN_ERR,
			"disconnect: Unexpected reselection <%02i-%i>\n",
			dcb->target_id, dcb->target_lun);
		srb->state = 0;
		waiting_process_next(acb);
	} else if (srb->state & SRB_ABORT_SENT) {
		dcb->flag &= ~ABORT_DEV_;
		acb->last_reset = jiffies + HZ / 2 + 1;
		dprintkl(KERN_ERR, "disconnect: SRB_ABORT_SENT\n");
		doing_srb_done(acb, DID_ABORT, srb->cmd, 1);
		waiting_process_next(acb);
	} else {
		if ((srb->state & (SRB_START_ + SRB_MSGOUT))
		    || !(srb->
			 state & (SRB_DISCONNECT | SRB_COMPLETED))) {
			/*
			 * Selection time out 
			 * SRB_START_ || SRB_MSGOUT || (!SRB_DISCONNECT && !SRB_COMPLETED)
			 */
			/* Unexp. Disc / Sel Timeout */
			if (srb->state != SRB_START_
			    && srb->state != SRB_MSGOUT) {
				srb->state = SRB_READY;
				dprintkl(KERN_DEBUG,
					"disconnect: (0x%p) Unexpected\n",
					srb->cmd);
				srb->target_status = SCSI_STAT_SEL_TIMEOUT;
				goto disc1;
			} else {
				/* Normal selection timeout */
				dprintkdbg(DBG_KG, "disconnect: (0x%p) "
					"<%02i-%i> SelTO\n", srb->cmd,
					dcb->target_id, dcb->target_lun);
				if (srb->retry_count++ > DC395x_MAX_RETRIES
				    || acb->scan_devices) {
					srb->target_status =
					    SCSI_STAT_SEL_TIMEOUT;
					goto disc1;
				}
				free_tag(dcb, srb);
				list_move(&srb->list, &dcb->srb_waiting_list);
				dprintkdbg(DBG_KG,
					"disconnect: (0x%p) Retry\n",
					srb->cmd);
				waiting_set_timer(acb, HZ / 20);
			}
		} else if (srb->state & SRB_DISCONNECT) {
			u8 bval = DC395x_read8(acb, TRM_S1040_SCSI_SIGNAL);
			/*
			 * SRB_DISCONNECT (This is what we expect!)
			 */
			if (bval & 0x40) {
				dprintkdbg(DBG_0, "disconnect: SCSI bus stat "
					" 0x%02x: ACK set! Other controllers?\n",
					bval);
				/* It could come from another initiator, therefore don't do much ! */
			} else
				waiting_process_next(acb);
		} else if (srb->state & SRB_COMPLETED) {
		      disc1:
			/*
			 ** SRB_COMPLETED
			 */
			free_tag(dcb, srb);
			dcb->active_srb = NULL;
			srb->state = SRB_FREE;
			srb_done(acb, dcb, srb);
		}
	}
}


static void reselect(struct AdapterCtlBlk *acb)
{
	struct DeviceCtlBlk *dcb = acb->active_dcb;
	struct ScsiReqBlk *srb = NULL;
	u16 rsel_tar_lun_id;
	u8 id, lun;
	dprintkdbg(DBG_0, "reselect: acb=%p\n", acb);

	clear_fifo(acb, "reselect");
	/*DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_HWRESELECT | DO_DATALATCH); */
	/* Read Reselected Target ID and LUN */
	rsel_tar_lun_id = DC395x_read16(acb, TRM_S1040_SCSI_TARGETID);
	if (dcb) {		/* Arbitration lost but Reselection win */
		srb = dcb->active_srb;
		if (!srb) {
			dprintkl(KERN_DEBUG, "reselect: Arb lost Resel won, "
				"but active_srb == NULL\n");
			DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
			return;
		}
		/* Why the if ? */
		if (!acb->scan_devices) {
			dprintkdbg(DBG_KG, "reselect: (0x%p) <%02i-%i> "
				"Arb lost but Resel win rsel=%i stat=0x%04x\n",
				srb->cmd, dcb->target_id,
				dcb->target_lun, rsel_tar_lun_id,
				DC395x_read16(acb, TRM_S1040_SCSI_STATUS));
			/*srb->state |= SRB_DISCONNECT; */

			srb->state = SRB_READY;
			free_tag(dcb, srb);
			list_move(&srb->list, &dcb->srb_waiting_list);
			waiting_set_timer(acb, HZ / 20);

			/* return; */
		}
	}
	/* Read Reselected Target Id and LUN */
	if (!(rsel_tar_lun_id & (IDENTIFY_BASE << 8)))
		dprintkl(KERN_DEBUG, "reselect: Expects identify msg. "
			"Got %i!\n", rsel_tar_lun_id);
	id = rsel_tar_lun_id & 0xff;
	lun = (rsel_tar_lun_id >> 8) & 7;
	dcb = find_dcb(acb, id, lun);
	if (!dcb) {
		dprintkl(KERN_ERR, "reselect: From non existent device "
			"<%02i-%i>\n", id, lun);
		DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
		return;
	}
	acb->active_dcb = dcb;

	if (!(dcb->dev_mode & NTC_DO_DISCONNECT))
		dprintkl(KERN_DEBUG, "reselect: in spite of forbidden "
			"disconnection? <%02i-%i>\n",
			dcb->target_id, dcb->target_lun);

	if (dcb->sync_mode & EN_TAG_QUEUEING) {
		srb = acb->tmp_srb;
		dcb->active_srb = srb;
	} else {
		/* There can be only one! */
		srb = dcb->active_srb;
		if (!srb || !(srb->state & SRB_DISCONNECT)) {
			/*
			 * abort command
			 */
			dprintkl(KERN_DEBUG,
				"reselect: w/o disconnected cmds <%02i-%i>\n",
				dcb->target_id, dcb->target_lun);
			srb = acb->tmp_srb;
			srb->state = SRB_UNEXPECT_RESEL;
			dcb->active_srb = srb;
			enable_msgout_abort(acb, srb);
		} else {
			if (dcb->flag & ABORT_DEV_) {
				/*srb->state = SRB_ABORT_SENT; */
				enable_msgout_abort(acb, srb);
			} else
				srb->state = SRB_DATA_XFER;

		}
	}
	srb->scsi_phase = PH_BUS_FREE;	/* initial phase */

	/* Program HA ID, target ID, period and offset */
	dprintkdbg(DBG_0, "reselect: select <%i>\n", dcb->target_id);
	DC395x_write8(acb, TRM_S1040_SCSI_HOSTID, acb->scsi_host->this_id);	/* host   ID */
	DC395x_write8(acb, TRM_S1040_SCSI_TARGETID, dcb->target_id);		/* target ID */
	DC395x_write8(acb, TRM_S1040_SCSI_OFFSET, dcb->sync_offset);		/* offset    */
	DC395x_write8(acb, TRM_S1040_SCSI_SYNC, dcb->sync_period);		/* sync period, wide */
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_DATALATCH);		/* it's important for atn stop */
	/* SCSI command */
	DC395x_write8(acb, TRM_S1040_SCSI_COMMAND, SCMD_MSGACCEPT);
}


static inline u8 tagq_blacklist(char *name)
{
#ifndef DC395x_NO_TAGQ
#if 0
	u8 i;
	for (i = 0; i < BADDEVCNT; i++)
		if (memcmp(name, DC395x_baddevname1[i], 28) == 0)
			return 1;
#endif
	return 0;
#else
	return 1;
#endif
}


static void disc_tagq_set(struct DeviceCtlBlk *dcb, struct ScsiInqData *ptr)
{
	/* Check for SCSI format (ANSI and Response data format) */
	if ((ptr->Vers & 0x07) >= 2 || (ptr->RDF & 0x0F) == 2) {
		if ((ptr->Flags & SCSI_INQ_CMDQUEUE)
		    && (dcb->dev_mode & NTC_DO_TAG_QUEUEING) &&
		    /*(dcb->dev_mode & NTC_DO_DISCONNECT) */
		    /* ((dcb->dev_type == TYPE_DISK) 
		       || (dcb->dev_type == TYPE_MOD)) && */
		    !tagq_blacklist(((char *)ptr) + 8)) {
			if (dcb->max_command == 1)
				dcb->max_command =
				    dcb->acb->tag_max_num;
			dcb->sync_mode |= EN_TAG_QUEUEING;
			/*dcb->tag_mask = 0; */
		} else
			dcb->max_command = 1;
	}
}


static void add_dev(struct AdapterCtlBlk *acb, struct DeviceCtlBlk *dcb,
		struct ScsiInqData *ptr)
{
	u8 bval1 = ptr->DevType & SCSI_DEVTYPE;
	dcb->dev_type = bval1;
	/* if (bval1 == TYPE_DISK || bval1 == TYPE_MOD) */
	disc_tagq_set(dcb, ptr);
}


/* unmap mapped pci regions from SRB */
static void pci_unmap_srb(struct AdapterCtlBlk *acb, struct ScsiReqBlk *srb)
{
	struct scsi_cmnd *cmd = srb->cmd;
	enum dma_data_direction dir = cmd->sc_data_direction;

	if (scsi_sg_count(cmd) && dir != DMA_NONE) {
		/* unmap DC395x SG list */
		dprintkdbg(DBG_SG, "pci_unmap_srb: list=%08x(%05x)\n",
			srb->sg_bus_addr, SEGMENTX_LEN);
		dma_unmap_single(&acb->dev->dev, srb->sg_bus_addr, SEGMENTX_LEN,
				DMA_TO_DEVICE);
		dprintkdbg(DBG_SG, "pci_unmap_srb: segs=%i buffer=%p\n",
			   scsi_sg_count(cmd), scsi_bufflen(cmd));
		/* unmap the sg segments */
		scsi_dma_unmap(cmd);
	}
}


/* unmap mapped pci sense buffer from SRB */
static void pci_unmap_srb_sense(struct AdapterCtlBlk *acb,
		struct ScsiReqBlk *srb)
{
	if (!(srb->flag & AUTO_REQSENSE))
		return;
	/* Unmap sense buffer */
	dprintkdbg(DBG_SG, "pci_unmap_srb_sense: buffer=%08x\n",
	       srb->segment_x[0].address);
	dma_unmap_single(&acb->dev->dev, srb->segment_x[0].address,
			 srb->segment_x[0].length, DMA_FROM_DEVICE);
	/* Restore SG stuff */
	srb->total_xfer_length = srb->xferred;
	srb->segment_x[0].address =
	    srb->segment_x[DC395x_MAX_SG_LISTENTRY - 1].address;
	srb->segment_x[0].length =
	    srb->segment_x[DC395x_MAX_SG_LISTENTRY - 1].length;
}


/*
 * Complete execution of a SCSI command
 * Signal completion to the generic SCSI driver  
 */
static void srb_done(struct AdapterCtlBlk *acb, struct DeviceCtlBlk *dcb,
		struct ScsiReqBlk *srb)
{
	u8 tempcnt, status;
	struct scsi_cmnd *cmd = srb->cmd;
	enum dma_data_direction dir = cmd->sc_data_direction;
	int ckc_only = 1;

	dprintkdbg(DBG_1, "srb_done: (0x%p) <%02i-%i>\n", srb->cmd,
		srb->cmd->device->id, (u8)srb->cmd->device->lun);
	dprintkdbg(DBG_SG, "srb_done: srb=%p sg=%i(%i/%i) buf=%p\n",
		   srb, scsi_sg_count(cmd), srb->sg_index, srb->sg_count,
		   scsi_sgtalbe(cmd));
	status = srb->target_status;
	set_host_byte(cmd, DID_OK);
	set_status_byte(cmd, SAM_STAT_GOOD);
	if (srb->flag & AUTO_REQSENSE) {
		dprintkdbg(DBG_0, "srb_done: AUTO_REQSENSE1\n");
		pci_unmap_srb_sense(acb, srb);
		/*
		 ** target status..........................
		 */
		srb->flag &= ~AUTO_REQSENSE;
		srb->adapter_status = 0;
		srb->target_status = SAM_STAT_CHECK_CONDITION;
		if (debug_enabled(DBG_1)) {
			switch (cmd->sense_buffer[2] & 0x0f) {
			case NOT_READY:
				dprintkl(KERN_DEBUG,
				     "ReqSense: NOT_READY cmnd=0x%02x <%02i-%i> stat=%i scan=%i ",
				     cmd->cmnd[0], dcb->target_id,
				     dcb->target_lun, status, acb->scan_devices);
				break;
			case UNIT_ATTENTION:
				dprintkl(KERN_DEBUG,
				     "ReqSense: UNIT_ATTENTION cmnd=0x%02x <%02i-%i> stat=%i scan=%i ",
				     cmd->cmnd[0], dcb->target_id,
				     dcb->target_lun, status, acb->scan_devices);
				break;
			case ILLEGAL_REQUEST:
				dprintkl(KERN_DEBUG,
				     "ReqSense: ILLEGAL_REQUEST cmnd=0x%02x <%02i-%i> stat=%i scan=%i ",
				     cmd->cmnd[0], dcb->target_id,
				     dcb->target_lun, status, acb->scan_devices);
				break;
			case MEDIUM_ERROR:
				dprintkl(KERN_DEBUG,
				     "ReqSense: MEDIUM_ERROR cmnd=0x%02x <%02i-%i> stat=%i scan=%i ",
				     cmd->cmnd[0], dcb->target_id,
				     dcb->target_lun, status, acb->scan_devices);
				break;
			case HARDWARE_ERROR:
				dprintkl(KERN_DEBUG,
				     "ReqSense: HARDWARE_ERROR cmnd=0x%02x <%02i-%i> stat=%i scan=%i ",
				     cmd->cmnd[0], dcb->target_id,
				     dcb->target_lun, status, acb->scan_devices);
				break;
			}
			if (cmd->sense_buffer[7] >= 6)
				printk("sense=0x%02x ASC=0x%02x ASCQ=0x%02x "
					"(0x%08x 0x%08x)\n",
					cmd->sense_buffer[2], cmd->sense_buffer[12],
					cmd->sense_buffer[13],
					*((unsigned int *)(cmd->sense_buffer + 3)),
					*((unsigned int *)(cmd->sense_buffer + 8)));
			else
				printk("sense=0x%02x No ASC/ASCQ (0x%08x)\n",
					cmd->sense_buffer[2],
					*((unsigned int *)(cmd->sense_buffer + 3)));
		}

		if (status == SAM_STAT_CHECK_CONDITION) {
			set_host_byte(cmd, DID_BAD_TARGET);
			goto ckc_e;
		}
		dprintkdbg(DBG_0, "srb_done: AUTO_REQSENSE2\n");

		set_status_byte(cmd, SAM_STAT_CHECK_CONDITION);

		goto ckc_e;
	}

/*************************************************************/
	if (status) {
		/*
		 * target status..........................
		 */
		if (status == SAM_STAT_CHECK_CONDITION) {
			request_sense(acb, dcb, srb);
			return;
		} else if (status == SAM_STAT_TASK_SET_FULL) {
			tempcnt = (u8)list_size(&dcb->srb_going_list);
			dprintkl(KERN_INFO, "QUEUE_FULL for dev <%02i-%i> with %i cmnds\n",
			     dcb->target_id, dcb->target_lun, tempcnt);
			if (tempcnt > 1)
				tempcnt--;
			dcb->max_command = tempcnt;
			free_tag(dcb, srb);
			list_move(&srb->list, &dcb->srb_waiting_list);
			waiting_set_timer(acb, HZ / 20);
			srb->adapter_status = 0;
			srb->target_status = 0;
			return;
		} else if (status == SCSI_STAT_SEL_TIMEOUT) {
			srb->adapter_status = H_SEL_TIMEOUT;
			srb->target_status = 0;
			set_host_byte(cmd, DID_NO_CONNECT);
		} else {
			srb->adapter_status = 0;
			set_host_byte(cmd, DID_ERROR);
			set_status_byte(cmd, status);
		}
	} else {
		/*
		 ** process initiator status..........................
		 */
		status = srb->adapter_status;
		if (status & H_OVER_UNDER_RUN) {
			srb->target_status = 0;
			scsi_msg_to_host_byte(cmd, srb->end_message);
		} else if (srb->status & PARITY_ERROR) {
			set_host_byte(cmd, DID_PARITY);
		} else {	/* No error */

			srb->adapter_status = 0;
			srb->target_status = 0;
		}
	}

	ckc_only = 0;
/* Check Error Conditions */
      ckc_e:

	pci_unmap_srb(acb, srb);

	if (cmd->cmnd[0] == INQUIRY) {
		unsigned char *base = NULL;
		struct ScsiInqData *ptr;
		unsigned long flags = 0;
		struct scatterlist* sg = scsi_sglist(cmd);
		size_t offset = 0, len = sizeof(struct ScsiInqData);

		local_irq_save(flags);
		base = scsi_kmap_atomic_sg(sg, scsi_sg_count(cmd), &offset, &len);
		ptr = (struct ScsiInqData *)(base + offset);

		if (!ckc_only && get_host_byte(cmd) == DID_OK
		    && cmd->cmnd[2] == 0 && scsi_bufflen(cmd) >= 8
		    && dir != DMA_NONE && ptr && (ptr->Vers & 0x07) >= 2)
			dcb->inquiry7 = ptr->Flags;

	/*if( srb->cmd->cmnd[0] == INQUIRY && */
	/*  (host_byte(cmd->result) == DID_OK || status_byte(cmd->result) & CHECK_CONDITION) ) */
		if ((get_host_byte(cmd) == DID_OK) ||
		    (get_status_byte(cmd) == SAM_STAT_CHECK_CONDITION)) {
			if (!dcb->init_tcq_flag) {
				add_dev(acb, dcb, ptr);
				dcb->init_tcq_flag = 1;
			}
		}

		scsi_kunmap_atomic_sg(base);
		local_irq_restore(flags);
	}

	/* Here is the info for Doug Gilbert's sg3 ... */
	scsi_set_resid(cmd, srb->total_xfer_length);
	if (debug_enabled(DBG_KG)) {
		if (srb->total_xfer_length)
			dprintkdbg(DBG_KG, "srb_done: (0x%p) <%02i-%i> "
				"cmnd=0x%02x Missed %i bytes\n",
				cmd, cmd->device->id, (u8)cmd->device->lun,
				cmd->cmnd[0], srb->total_xfer_length);
	}

	if (srb != acb->tmp_srb) {
		/* Add to free list */
		dprintkdbg(DBG_0, "srb_done: (0x%p) done result=0x%08x\n",
			   cmd, cmd->result);
		list_move_tail(&srb->list, &acb->srb_free_list);
	} else {
		dprintkl(KERN_ERR, "srb_done: ERROR! Completed cmd with tmp_srb\n");
	}

	scsi_done(cmd);
	waiting_process_next(acb);
}


/* abort all cmds in our queues */
static void doing_srb_done(struct AdapterCtlBlk *acb, u8 did_flag,
		struct scsi_cmnd *cmd, u8 force)
{
	struct DeviceCtlBlk *dcb;
	dprintkl(KERN_INFO, "doing_srb_done: pids ");

	list_for_each_entry(dcb, &acb->dcb_list, list) {
		struct ScsiReqBlk *srb;
		struct ScsiReqBlk *tmp;
		struct scsi_cmnd *p;

		list_for_each_entry_safe(srb, tmp, &dcb->srb_going_list, list) {
			p = srb->cmd;
			printk("G:%p(%02i-%i) ", p,
			       p->device->id, (u8)p->device->lun);
			list_del(&srb->list);
			free_tag(dcb, srb);
			list_add_tail(&srb->list, &acb->srb_free_list);
			set_host_byte(p, did_flag);
			set_status_byte(p, SAM_STAT_GOOD);
			pci_unmap_srb_sense(acb, srb);
			pci_unmap_srb(acb, srb);
			if (force) {
				/* For new EH, we normally don't need to give commands back,
				 * as they all complete or all time out */
				scsi_done(p);
			}
		}
		if (!list_empty(&dcb->srb_going_list))
			dprintkl(KERN_DEBUG, 
			       "How could the ML send cmnds to the Going queue? <%02i-%i>\n",
			       dcb->target_id, dcb->target_lun);
		if (dcb->tag_mask)
			dprintkl(KERN_DEBUG,
			       "tag_mask for <%02i-%i> should be empty, is %08x!\n",
			       dcb->target_id, dcb->target_lun,
			       dcb->tag_mask);

		/* Waiting queue */
		list_for_each_entry_safe(srb, tmp, &dcb->srb_waiting_list, list) {
			p = srb->cmd;

			printk("W:%p<%02i-%i>", p, p->device->id,
			       (u8)p->device->lun);
			list_move_tail(&srb->list, &acb->srb_free_list);
			set_host_byte(p, did_flag);
			set_status_byte(p, SAM_STAT_GOOD);
			pci_unmap_srb_sense(acb, srb);
			pci_unmap_srb(acb, srb);
			if (force) {
				/* For new EH, we normally don't need to give commands back,
				 * as they all complete or all time out */
				scsi_done(cmd);
			}
		}
		if (!list_empty(&dcb->srb_waiting_list))
			dprintkl(KERN_DEBUG, "ML queued %i cmnds again to <%02i-%i>\n",
			     list_size(&dcb->srb_waiting_list), dcb->target_id,
			     dcb->target_lun);
		dcb->flag &= ~ABORT_DEV_;
	}
	printk("\n");
}


static void reset_scsi_bus(struct AdapterCtlBlk *acb)
{
	dprintkdbg(DBG_0, "reset_scsi_bus: acb=%p\n", acb);
	acb->acb_flag |= RESET_DEV;	/* RESET_DETECT, RESET_DONE, RESET_DEV */
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_RSTSCSI);

	while (!(DC395x_read8(acb, TRM_S1040_SCSI_INTSTATUS) & INT_SCSIRESET))
		/* nothing */;
}


static void set_basic_config(struct AdapterCtlBlk *acb)
{
	u8 bval;
	u16 wval;
	DC395x_write8(acb, TRM_S1040_SCSI_TIMEOUT, acb->sel_timeout);
	if (acb->config & HCC_PARITY)
		bval = PHASELATCH | INITIATOR | BLOCKRST | PARITYCHECK;
	else
		bval = PHASELATCH | INITIATOR | BLOCKRST;

	DC395x_write8(acb, TRM_S1040_SCSI_CONFIG0, bval);

	/* program configuration 1: Act_Neg (+ Act_Neg_Enh? + Fast_Filter? + DataDis?) */
	DC395x_write8(acb, TRM_S1040_SCSI_CONFIG1, 0x03);	/* was 0x13: default */
	/* program Host ID                  */
	DC395x_write8(acb, TRM_S1040_SCSI_HOSTID, acb->scsi_host->this_id);
	/* set ansynchronous transfer       */
	DC395x_write8(acb, TRM_S1040_SCSI_OFFSET, 0x00);
	/* Turn LED control off */
	wval = DC395x_read16(acb, TRM_S1040_GEN_CONTROL) & 0x7F;
	DC395x_write16(acb, TRM_S1040_GEN_CONTROL, wval);
	/* DMA config          */
	wval = DC395x_read16(acb, TRM_S1040_DMA_CONFIG) & ~DMA_FIFO_CTRL;
	wval |=
	    DMA_FIFO_HALF_HALF | DMA_ENHANCE /*| DMA_MEM_MULTI_READ */ ;
	DC395x_write16(acb, TRM_S1040_DMA_CONFIG, wval);
	/* Clear pending interrupt status */
	DC395x_read8(acb, TRM_S1040_SCSI_INTSTATUS);
	/* Enable SCSI interrupt    */
	DC395x_write8(acb, TRM_S1040_SCSI_INTEN, 0x7F);
	DC395x_write8(acb, TRM_S1040_DMA_INTEN, EN_SCSIINTR | EN_DMAXFERERROR
		      /*| EN_DMAXFERABORT | EN_DMAXFERCOMP | EN_FORCEDMACOMP */
		      );
}


static void scsi_reset_detect(struct AdapterCtlBlk *acb)
{
	dprintkl(KERN_INFO, "scsi_reset_detect: acb=%p\n", acb);
	/* delay half a second */
	if (timer_pending(&acb->waiting_timer))
		del_timer(&acb->waiting_timer);

	DC395x_write8(acb, TRM_S1040_SCSI_CONTROL, DO_RSTMODULE);
	DC395x_write8(acb, TRM_S1040_DMA_CONTROL, DMARESETMODULE);
	/*DC395x_write8(acb, TRM_S1040_DMA_CONTROL,STOPDMAXFER); */
	udelay(500);
	/* Maybe we locked up the bus? Then lets wait even longer ... */
	acb->last_reset =
	    jiffies + 5 * HZ / 2 +
	    HZ * acb->eeprom.delay_time;

	clear_fifo(acb, "scsi_reset_detect");
	set_basic_config(acb);
	/*1.25 */
	/*DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_HWRESELECT); */

	if (acb->acb_flag & RESET_DEV) {	/* RESET_DETECT, RESET_DONE, RESET_DEV */
		acb->acb_flag |= RESET_DONE;
	} else {
		acb->acb_flag |= RESET_DETECT;
		reset_dev_param(acb);
		doing_srb_done(acb, DID_RESET, NULL, 1);
		/*DC395x_RecoverSRB( acb ); */
		acb->active_dcb = NULL;
		acb->acb_flag = 0;
		waiting_process_next(acb);
	}
}


static void request_sense(struct AdapterCtlBlk *acb, struct DeviceCtlBlk *dcb,
		struct ScsiReqBlk *srb)
{
	struct scsi_cmnd *cmd = srb->cmd;
	dprintkdbg(DBG_1, "request_sense: (0x%p) <%02i-%i>\n",
		cmd, cmd->device->id, (u8)cmd->device->lun);

	srb->flag |= AUTO_REQSENSE;
	srb->adapter_status = 0;
	srb->target_status = 0;

	/* KG: Can this prevent crap sense data ? */
	memset(cmd->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);

	/* Save some data */
	srb->segment_x[DC395x_MAX_SG_LISTENTRY - 1].address =
	    srb->segment_x[0].address;
	srb->segment_x[DC395x_MAX_SG_LISTENTRY - 1].length =
	    srb->segment_x[0].length;
	srb->xferred = srb->total_xfer_length;
	/* srb->segment_x : a one entry of S/G list table */
	srb->total_xfer_length = SCSI_SENSE_BUFFERSIZE;
	srb->segment_x[0].length = SCSI_SENSE_BUFFERSIZE;
	/* Map sense buffer */
	srb->segment_x[0].address = dma_map_single(&acb->dev->dev,
			cmd->sense_buffer, SCSI_SENSE_BUFFERSIZE,
			DMA_FROM_DEVICE);
	dprintkdbg(DBG_SG, "request_sense: map buffer %p->%08x(%05x)\n",
	       cmd->sense_buffer, srb->segment_x[0].address,
	       SCSI_SENSE_BUFFERSIZE);
	srb->sg_count = 1;
	srb->sg_index = 0;

	if (start_scsi(acb, dcb, srb)) {	/* Should only happen, if sb. else grabs the bus */
		dprintkl(KERN_DEBUG,
			"request_sense: (0x%p) failed <%02i-%i>\n",
			srb->cmd, dcb->target_id, dcb->target_lun);
		list_move(&srb->list, &dcb->srb_waiting_list);
		waiting_set_timer(acb, HZ / 100);
	}
}


/**
 * device_alloc - Allocate a new device instance. This create the
 * devices instance and sets up all the data items. The adapter
 * instance is required to obtain confiuration information for this
 * device. This does *not* add this device to the adapters device
 * list.
 *
 * @acb: The adapter to obtain configuration information from.
 * @target: The target for the new device.
 * @lun: The lun for the new device.
 *
 * Return the new device if successful or NULL on failure.
 **/
static struct DeviceCtlBlk *device_alloc(struct AdapterCtlBlk *acb,
		u8 target, u8 lun)
{
	struct NvRamType *eeprom = &acb->eeprom;
	u8 period_index = eeprom->target[target].period & 0x07;
	struct DeviceCtlBlk *dcb;

	dcb = kmalloc(sizeof(struct DeviceCtlBlk), GFP_ATOMIC);
	dprintkdbg(DBG_0, "device_alloc: <%02i-%i>\n", target, lun);
	if (!dcb)
		return NULL;
	dcb->acb = NULL;
	INIT_LIST_HEAD(&dcb->srb_going_list);
	INIT_LIST_HEAD(&dcb->srb_waiting_list);
	dcb->active_srb = NULL;
	dcb->tag_mask = 0;
	dcb->max_command = 1;
	dcb->target_id = target;
	dcb->target_lun = lun;
	dcb->dev_mode = eeprom->target[target].cfg0;
#ifndef DC395x_NO_DISCONNECT
	dcb->identify_msg =
	    IDENTIFY(dcb->dev_mode & NTC_DO_DISCONNECT, lun);
#else
	dcb->identify_msg = IDENTIFY(0, lun);
#endif
	dcb->inquiry7 = 0;
	dcb->sync_mode = 0;
	dcb->min_nego_period = clock_period[period_index];
	dcb->sync_period = 0;
	dcb->sync_offset = 0;
	dcb->flag = 0;

#ifndef DC395x_NO_WIDE
	if ((dcb->dev_mode & NTC_DO_WIDE_NEGO)
	    && (acb->config & HCC_WIDE_CARD))
		dcb->sync_mode |= WIDE_NEGO_ENABLE;
#endif
#ifndef DC395x_NO_SYNC
	if (dcb->dev_mode & NTC_DO_SYNC_NEGO)
		if (!(lun) || current_sync_offset)
			dcb->sync_mode |= SYNC_NEGO_ENABLE;
#endif
	if (dcb->target_lun != 0) {
		/* Copy settings */
		struct DeviceCtlBlk *p = NULL, *iter;

		list_for_each_entry(iter, &acb->dcb_list, list)
			if (iter->target_id == dcb->target_id) {
				p = iter;
				break;
			}

		if (!p) {
			kfree(dcb);
			return NULL;
		}

		dprintkdbg(DBG_1, 
		       "device_alloc: <%02i-%i> copy from <%02i-%i>\n",
		       dcb->target_id, dcb->target_lun,
		       p->target_id, p->target_lun);
		dcb->sync_mode = p->sync_mode;
		dcb->sync_period = p->sync_period;
		dcb->min_nego_period = p->min_nego_period;
		dcb->sync_offset = p->sync_offset;
		dcb->inquiry7 = p->inquiry7;
	}
	return dcb;
}


/**
 * adapter_add_device - Adds the device instance to the adaptor instance.
 *
 * @acb: The adapter device to be updated
 * @dcb: A newly created and initialised device instance to add.
 **/
static void adapter_add_device(struct AdapterCtlBlk *acb,
		struct DeviceCtlBlk *dcb)
{
	/* backpointer to adapter */
	dcb->acb = acb;
	
	/* set run_robin to this device if it is currently empty */
	if (list_empty(&acb->dcb_list))
		acb->dcb_run_robin = dcb;

	/* add device to list */
	list_add_tail(&dcb->list, &acb->dcb_list);

	/* update device maps */
	acb->dcb_map[dcb->target_id] |= (1 << dcb->target_lun);
	acb->children[dcb->target_id][dcb->target_lun] = dcb;
}


/**
 * adapter_remove_device - Removes the device instance from the adaptor
 * instance. The device instance is not check in any way or freed by this. 
 * The caller is expected to take care of that. This will simply remove the
 * device from the adapters data strcutures.
 *
 * @acb: The adapter device to be updated
 * @dcb: A device that has previously been added to the adapter.
 **/
static void adapter_remove_device(struct AdapterCtlBlk *acb,
		struct DeviceCtlBlk *dcb)
{
	struct DeviceCtlBlk *i;
	struct DeviceCtlBlk *tmp;
	dprintkdbg(DBG_0, "adapter_remove_device: <%02i-%i>\n",
		dcb->target_id, dcb->target_lun);

	/* fix up any pointers to this device that we have in the adapter */
	if (acb->active_dcb == dcb)
		acb->active_dcb = NULL;
	if (acb->dcb_run_robin == dcb)
		acb->dcb_run_robin = dcb_get_next(&acb->dcb_list, dcb);

	/* unlink from list */
	list_for_each_entry_safe(i, tmp, &acb->dcb_list, list)
		if (dcb == i) {
			list_del(&i->list);
			break;
		}

	/* clear map and children */	
	acb->dcb_map[dcb->target_id] &= ~(1 << dcb->target_lun);
	acb->children[dcb->target_id][dcb->target_lun] = NULL;
	dcb->acb = NULL;
}


/**
 * adapter_remove_and_free_device - Removes a single device from the adapter
 * and then frees the device information.
 *
 * @acb: The adapter device to be updated
 * @dcb: A device that has previously been added to the adapter.
 */
static void adapter_remove_and_free_device(struct AdapterCtlBlk *acb,
		struct DeviceCtlBlk *dcb)
{
	if (list_size(&dcb->srb_going_list) > 1) {
		dprintkdbg(DBG_1, "adapter_remove_and_free_device: <%02i-%i> "
		           "Won't remove because of %i active requests.\n",
			   dcb->target_id, dcb->target_lun,
			   list_size(&dcb->srb_going_list));
		return;
	}
	adapter_remove_device(acb, dcb);
	kfree(dcb);
}


/**
 * adapter_remove_and_free_all_devices - Removes and frees all of the
 * devices associated with the specified adapter.
 *
 * @acb: The adapter from which all devices should be removed.
 **/
static void adapter_remove_and_free_all_devices(struct AdapterCtlBlk* acb)
{
	struct DeviceCtlBlk *dcb;
	struct DeviceCtlBlk *tmp;
	dprintkdbg(DBG_1, "adapter_remove_and_free_all_devices: num=%i\n",
		   list_size(&acb->dcb_list));

	list_for_each_entry_safe(dcb, tmp, &acb->dcb_list, list)
		adapter_remove_and_free_device(acb, dcb);
}


/**
 * dc395x_slave_alloc - Called by the scsi mid layer to tell us about a new
 * scsi device that we need to deal with. We allocate a new device and then
 * insert that device into the adapters device list.
 *
 * @scsi_device: The new scsi device that we need to handle.
 **/
static int dc395x_slave_alloc(struct scsi_device *scsi_device)
{
	struct AdapterCtlBlk *acb = (struct AdapterCtlBlk *)scsi_device->host->hostdata;
	struct DeviceCtlBlk *dcb;

	dcb = device_alloc(acb, scsi_device->id, scsi_device->lun);
	if (!dcb)
		return -ENOMEM;
	adapter_add_device(acb, dcb);

	return 0;
}


/**
 * dc395x_slave_destroy - Called by the scsi mid layer to tell us about a
 * device that is going away.
 *
 * @scsi_device: The new scsi device that we need to handle.
 **/
static void dc395x_slave_destroy(struct scsi_device *scsi_device)
{
	struct AdapterCtlBlk *acb = (struct AdapterCtlBlk *)scsi_device->host->hostdata;
	struct DeviceCtlBlk *dcb = find_dcb(acb, scsi_device->id, scsi_device->lun);
	if (dcb)
		adapter_remove_and_free_device(acb, dcb);
}




/**
 * trms1040_wait_30us: wait for 30 us
 *
 * Waits for 30us (using the chip by the looks of it..)
 *
 * @io_port: base I/O address
 **/
static void trms1040_wait_30us(unsigned long io_port)
{
	/* ScsiPortStallExecution(30); wait 30 us */
	outb(5, io_port + TRM_S1040_GEN_TIMER);
	while (!(inb(io_port + TRM_S1040_GEN_STATUS) & GTIMEOUT))
		/* nothing */ ;
}


/**
 * trms1040_write_cmd - write the secified command and address to
 * chip
 *
 * @io_port:	base I/O address
 * @cmd:	SB + op code (command) to send
 * @addr:	address to send
 **/
static void trms1040_write_cmd(unsigned long io_port, u8 cmd, u8 addr)
{
	int i;
	u8 send_data;

	/* program SB + OP code */
	for (i = 0; i < 3; i++, cmd <<= 1) {
		send_data = NVR_SELECT;
		if (cmd & 0x04)	/* Start from bit 2 */
			send_data |= NVR_BITOUT;

		outb(send_data, io_port + TRM_S1040_GEN_NVRAM);
		trms1040_wait_30us(io_port);
		outb((send_data | NVR_CLOCK),
		     io_port + TRM_S1040_GEN_NVRAM);
		trms1040_wait_30us(io_port);
	}

	/* send address */
	for (i = 0; i < 7; i++, addr <<= 1) {
		send_data = NVR_SELECT;
		if (addr & 0x40)	/* Start from bit 6 */
			send_data |= NVR_BITOUT;

		outb(send_data, io_port + TRM_S1040_GEN_NVRAM);
		trms1040_wait_30us(io_port);
		outb((send_data | NVR_CLOCK),
		     io_port + TRM_S1040_GEN_NVRAM);
		trms1040_wait_30us(io_port);
	}
	outb(NVR_SELECT, io_port + TRM_S1040_GEN_NVRAM);
	trms1040_wait_30us(io_port);
}


/**
 * trms1040_set_data - store a single byte in the eeprom
 *
 * Called from write all to write a single byte into the SSEEPROM
 * Which is done one bit at a time.
 *
 * @io_port:	base I/O address
 * @addr:	offset into EEPROM
 * @byte:	bytes to write
 **/
static void trms1040_set_data(unsigned long io_port, u8 addr, u8 byte)
{
	int i;
	u8 send_data;

	/* Send write command & address */
	trms1040_write_cmd(io_port, 0x05, addr);

	/* Write data */
	for (i = 0; i < 8; i++, byte <<= 1) {
		send_data = NVR_SELECT;
		if (byte & 0x80)	/* Start from bit 7 */
			send_data |= NVR_BITOUT;

		outb(send_data, io_port + TRM_S1040_GEN_NVRAM);
		trms1040_wait_30us(io_port);
		outb((send_data | NVR_CLOCK), io_port + TRM_S1040_GEN_NVRAM);
		trms1040_wait_30us(io_port);
	}
	outb(NVR_SELECT, io_port + TRM_S1040_GEN_NVRAM);
	trms1040_wait_30us(io_port);

	/* Disable chip select */
	outb(0, io_port + TRM_S1040_GEN_NVRAM);
	trms1040_wait_30us(io_port);

	outb(NVR_SELECT, io_port + TRM_S1040_GEN_NVRAM);
	trms1040_wait_30us(io_port);

	/* Wait for write ready */
	while (1) {
		outb((NVR_SELECT | NVR_CLOCK), io_port + TRM_S1040_GEN_NVRAM);
		trms1040_wait_30us(io_port);

		outb(NVR_SELECT, io_port + TRM_S1040_GEN_NVRAM);
		trms1040_wait_30us(io_port);

		if (inb(io_port + TRM_S1040_GEN_NVRAM) & NVR_BITIN)
			break;
	}

	/*  Disable chip select */
	outb(0, io_port + TRM_S1040_GEN_NVRAM);
}


/**
 * trms1040_write_all - write 128 bytes to the eeprom
 *
 * Write the supplied 128 bytes to the chips SEEPROM
 *
 * @eeprom:	the data to write
 * @io_port:	the base io port
 **/
static void trms1040_write_all(struct NvRamType *eeprom, unsigned long io_port)
{
	u8 *b_eeprom = (u8 *)eeprom;
	u8 addr;

	/* Enable SEEPROM */
	outb((inb(io_port + TRM_S1040_GEN_CONTROL) | EN_EEPROM),
	     io_port + TRM_S1040_GEN_CONTROL);

	/* write enable */
	trms1040_write_cmd(io_port, 0x04, 0xFF);
	outb(0, io_port + TRM_S1040_GEN_NVRAM);
	trms1040_wait_30us(io_port);

	/* write */
	for (addr = 0; addr < 128; addr++, b_eeprom++)
		trms1040_set_data(io_port, addr, *b_eeprom);

	/* write disable */
	trms1040_write_cmd(io_port, 0x04, 0x00);
	outb(0, io_port + TRM_S1040_GEN_NVRAM);
	trms1040_wait_30us(io_port);

	/* Disable SEEPROM */
	outb((inb(io_port + TRM_S1040_GEN_CONTROL) & ~EN_EEPROM),
	     io_port + TRM_S1040_GEN_CONTROL);
}


/**
 * trms1040_get_data - get a single byte from the eeprom
 *
 * Called from read all to read a single byte into the SSEEPROM
 * Which is done one bit at a time.
 *
 * @io_port:	base I/O address
 * @addr:	offset into SEEPROM
 *
 * Returns the byte read.
 **/
static u8 trms1040_get_data(unsigned long io_port, u8 addr)
{
	int i;
	u8 read_byte;
	u8 result = 0;

	/* Send read command & address */
	trms1040_write_cmd(io_port, 0x06, addr);

	/* read data */
	for (i = 0; i < 8; i++) {
		outb((NVR_SELECT | NVR_CLOCK), io_port + TRM_S1040_GEN_NVRAM);
		trms1040_wait_30us(io_port);
		outb(NVR_SELECT, io_port + TRM_S1040_GEN_NVRAM);

		/* Get data bit while falling edge */
		read_byte = inb(io_port + TRM_S1040_GEN_NVRAM);
		result <<= 1;
		if (read_byte & NVR_BITIN)
			result |= 1;

		trms1040_wait_30us(io_port);
	}

	/* Disable chip select */
	outb(0, io_port + TRM_S1040_GEN_NVRAM);
	return result;
}


/**
 * trms1040_read_all - read all bytes from the eeprom
 *
 * Read the 128 bytes from the SEEPROM.
 *
 * @eeprom:	where to store the data
 * @io_port:	the base io port
 **/
static void trms1040_read_all(struct NvRamType *eeprom, unsigned long io_port)
{
	u8 *b_eeprom = (u8 *)eeprom;
	u8 addr;

	/* Enable SEEPROM */
	outb((inb(io_port + TRM_S1040_GEN_CONTROL) | EN_EEPROM),
	     io_port + TRM_S1040_GEN_CONTROL);

	/* read details */
	for (addr = 0; addr < 128; addr++, b_eeprom++)
		*b_eeprom = trms1040_get_data(io_port, addr);

	/* Disable SEEPROM */
	outb((inb(io_port + TRM_S1040_GEN_CONTROL) & ~EN_EEPROM),
	     io_port + TRM_S1040_GEN_CONTROL);
}



/**
 * check_eeprom - get and check contents of the eeprom
 *
 * Read seeprom 128 bytes into the memory provider in eeprom.
 * Checks the checksum and if it's not correct it uses a set of default
 * values.
 *
 * @eeprom:	caller allocated strcuture to read the eeprom data into
 * @io_port:	io port to read from
 **/
static void check_eeprom(struct NvRamType *eeprom, unsigned long io_port)
{
	u16 *w_eeprom = (u16 *)eeprom;
	u16 w_addr;
	u16 cksum;
	u32 d_addr;
	u32 *d_eeprom;

	trms1040_read_all(eeprom, io_port);	/* read eeprom */

	cksum = 0;
	for (w_addr = 0, w_eeprom = (u16 *)eeprom; w_addr < 64;
	     w_addr++, w_eeprom++)
		cksum += *w_eeprom;
	if (cksum != 0x1234) {
		/*
		 * Checksum is wrong.
		 * Load a set of defaults into the eeprom buffer
		 */
		dprintkl(KERN_WARNING,
			"EEProm checksum error: using default values and options.\n");
		eeprom->sub_vendor_id[0] = (u8)PCI_VENDOR_ID_TEKRAM;
		eeprom->sub_vendor_id[1] = (u8)(PCI_VENDOR_ID_TEKRAM >> 8);
		eeprom->sub_sys_id[0] = (u8)PCI_DEVICE_ID_TEKRAM_TRMS1040;
		eeprom->sub_sys_id[1] =
		    (u8)(PCI_DEVICE_ID_TEKRAM_TRMS1040 >> 8);
		eeprom->sub_class = 0x00;
		eeprom->vendor_id[0] = (u8)PCI_VENDOR_ID_TEKRAM;
		eeprom->vendor_id[1] = (u8)(PCI_VENDOR_ID_TEKRAM >> 8);
		eeprom->device_id[0] = (u8)PCI_DEVICE_ID_TEKRAM_TRMS1040;
		eeprom->device_id[1] =
		    (u8)(PCI_DEVICE_ID_TEKRAM_TRMS1040 >> 8);
		eeprom->reserved = 0x00;

		for (d_addr = 0, d_eeprom = (u32 *)eeprom->target;
		     d_addr < 16; d_addr++, d_eeprom++)
			*d_eeprom = 0x00000077;	/* cfg3,cfg2,period,cfg0 */

		*d_eeprom++ = 0x04000F07;	/* max_tag,delay_time,channel_cfg,scsi_id */
		*d_eeprom++ = 0x00000015;	/* reserved1,boot_lun,boot_target,reserved0 */
		for (d_addr = 0; d_addr < 12; d_addr++, d_eeprom++)
			*d_eeprom = 0x00;

		/* Now load defaults (maybe set by boot/module params) */
		set_safe_settings();
		fix_settings();
		eeprom_override(eeprom);

		eeprom->cksum = 0x00;
		for (w_addr = 0, cksum = 0, w_eeprom = (u16 *)eeprom;
		     w_addr < 63; w_addr++, w_eeprom++)
			cksum += *w_eeprom;

		*w_eeprom = 0x1234 - cksum;
		trms1040_write_all(eeprom, io_port);
		eeprom->delay_time = cfg_data[CFG_RESET_DELAY].value;
	} else {
		set_safe_settings();
		eeprom_index_to_delay(eeprom);
		eeprom_override(eeprom);
	}
}


/**
 * print_eeprom_settings - output the eeprom settings
 * to the kernel log so people can see what they were.
 *
 * @eeprom: The eeprom data strucutre to show details for.
 **/
static void print_eeprom_settings(struct NvRamType *eeprom)
{
	dprintkl(KERN_INFO, "Used settings: AdapterID=%02i, Speed=%i(%02i.%01iMHz), dev_mode=0x%02x\n",
		eeprom->scsi_id,
		eeprom->target[0].period,
		clock_speed[eeprom->target[0].period] / 10,
		clock_speed[eeprom->target[0].period] % 10,
		eeprom->target[0].cfg0);
	dprintkl(KERN_INFO, "               AdaptMode=0x%02x, Tags=%i(%02i), DelayReset=%is\n",
		eeprom->channel_cfg, eeprom->max_tag,
		1 << eeprom->max_tag, eeprom->delay_time);
}


/* Free SG tables */
static void adapter_sg_tables_free(struct AdapterCtlBlk *acb)
{
	int i;
	const unsigned srbs_per_page = PAGE_SIZE/SEGMENTX_LEN;

	for (i = 0; i < DC395x_MAX_SRB_CNT; i += srbs_per_page)
		kfree(acb->srb_array[i].segment_x);
}


/*
 * Allocate SG tables; as we have to pci_map them, an SG list (struct SGentry*)
 * should never cross a page boundary */
static int adapter_sg_tables_alloc(struct AdapterCtlBlk *acb)
{
	const unsigned mem_needed = (DC395x_MAX_SRB_CNT+1)
	                            *SEGMENTX_LEN;
	int pages = (mem_needed+(PAGE_SIZE-1))/PAGE_SIZE;
	const unsigned srbs_per_page = PAGE_SIZE/SEGMENTX_LEN;
	int srb_idx = 0;
	unsigned i = 0;
	struct SGentry *ptr;

	for (i = 0; i < DC395x_MAX_SRB_CNT; i++)
		acb->srb_array[i].segment_x = NULL;

	dprintkdbg(DBG_1, "Allocate %i pages for SG tables\n", pages);
	while (pages--) {
		ptr = kmalloc(PAGE_SIZE, GFP_KERNEL);
		if (!ptr) {
			adapter_sg_tables_free(acb);
			return 1;
		}
		dprintkdbg(DBG_1, "Allocate %li bytes at %p for SG segments %i\n",
			PAGE_SIZE, ptr, srb_idx);
		i = 0;
		while (i < srbs_per_page && srb_idx < DC395x_MAX_SRB_CNT)
			acb->srb_array[srb_idx++].segment_x =
			    ptr + (i++ * DC395x_MAX_SG_LISTENTRY);
	}
	if (i < srbs_per_page)
		acb->srb.segment_x =
		    ptr + (i * DC395x_MAX_SG_LISTENTRY);
	else
		dprintkl(KERN_DEBUG, "No space for tmsrb SG table reserved?!\n");
	return 0;
}



/**
 * adapter_print_config - print adapter connection and termination
 * config
 *
 * The io port in the adapter needs to have been set before calling
 * this function.
 *
 * @acb: The adapter to print the information for.
 **/
static void adapter_print_config(struct AdapterCtlBlk *acb)
{
	u8 bval;

	bval = DC395x_read8(acb, TRM_S1040_GEN_STATUS);
	dprintkl(KERN_INFO, "%sConnectors: ",
		((bval & WIDESCSI) ? "(Wide) " : ""));
	if (!(bval & CON5068))
		printk("ext%s ", !(bval & EXT68HIGH) ? "68" : "50");
	if (!(bval & CON68))
		printk("int68%s ", !(bval & INT68HIGH) ? "" : "(50)");
	if (!(bval & CON50))
		printk("int50 ");
	if ((bval & (CON5068 | CON50 | CON68)) ==
	    0 /*(CON5068 | CON50 | CON68) */ )
		printk(" Oops! (All 3?) ");
	bval = DC395x_read8(acb, TRM_S1040_GEN_CONTROL);
	printk(" Termination: ");
	if (bval & DIS_TERM)
		printk("Disabled\n");
	else {
		if (bval & AUTOTERM)
			printk("Auto ");
		if (bval & LOW8TERM)
			printk("Low ");
		if (bval & UP8TERM)
			printk("High ");
		printk("\n");
	}
}


/**
 * adapter_init_params - Initialize the various parameters in the
 * adapter structure. Note that the pointer to the scsi_host is set
 * early (when this instance is created) and the io_port and irq
 * values are set later after they have been reserved. This just gets
 * everything set to a good starting position.
 *
 * The eeprom structure in the adapter needs to have been set before
 * calling this function.
 *
 * @acb: The adapter to initialize.
 **/
static void adapter_init_params(struct AdapterCtlBlk *acb)
{
	struct NvRamType *eeprom = &acb->eeprom;
	int i;

	/* NOTE: acb->scsi_host is set at scsi_host/acb creation time */
	/* NOTE: acb->io_port_base is set at port registration time */
	/* NOTE: acb->io_port_len is set at port registration time */

	INIT_LIST_HEAD(&acb->dcb_list);
	acb->dcb_run_robin = NULL;
	acb->active_dcb = NULL;

	INIT_LIST_HEAD(&acb->srb_free_list);
	/*  temp SRB for Q tag used or abort command used  */
	acb->tmp_srb = &acb->srb;
	timer_setup(&acb->waiting_timer, waiting_timeout, 0);
	timer_setup(&acb->selto_timer, NULL, 0);

	acb->srb_count = DC395x_MAX_SRB_CNT;

	acb->sel_timeout = DC395x_SEL_TIMEOUT;	/* timeout=250ms */
	/* NOTE: acb->irq_level is set at IRQ registration time */

	acb->tag_max_num = 1 << eeprom->max_tag;
	if (acb->tag_max_num > 30)
		acb->tag_max_num = 30;

	acb->acb_flag = 0;	/* RESET_DETECT, RESET_DONE, RESET_DEV */
	acb->gmode2 = eeprom->channel_cfg;
	acb->config = 0;	/* NOTE: actually set in adapter_init_chip */

	if (eeprom->channel_cfg & NAC_SCANLUN)
		acb->lun_chk = 1;
	acb->scan_devices = 1;

	acb->scsi_host->this_id = eeprom->scsi_id;
	acb->hostid_bit = (1 << acb->scsi_host->this_id);

	for (i = 0; i < DC395x_MAX_SCSI_ID; i++)
		acb->dcb_map[i] = 0;

	acb->msg_len = 0;
	
	/* link static array of srbs into the srb free list */
	for (i = 0; i < acb->srb_count - 1; i++)
		list_add_tail(&acb->srb_array[i].list, &acb->srb_free_list);
}


/**
 * adapter_init_scsi_host - Initialize the scsi host instance based on
 * values that we have already stored in the adapter instance. There's
 * some mention that a lot of these are deprecated, so we won't use
 * them (we'll use the ones in the adapter instance) but we'll fill
 * them in in case something else needs them.
 *
 * The eeprom structure, irq and io ports in the adapter need to have
 * been set before calling this function.
 *
 * @host: The scsi host instance to fill in the values for.
 **/
static void adapter_init_scsi_host(struct Scsi_Host *host)
{
        struct AdapterCtlBlk *acb = (struct AdapterCtlBlk *)host->hostdata;
	struct NvRamType *eeprom = &acb->eeprom;
        
	host->max_cmd_len = 24;
	host->can_queue = DC395x_MAX_CMD_QUEUE;
	host->cmd_per_lun = DC395x_MAX_CMD_PER_LUN;
	host->this_id = (int)eeprom->scsi_id;
	host->io_port = acb->io_port_base;
	host->n_io_port = acb->io_port_len;
	host->dma_channel = -1;
	host->unique_id = acb->io_port_base;
	host->irq = acb->irq_level;
	acb->last_reset = jiffies;

	host->max_id = 16;
	if (host->max_id - 1 == eeprom->scsi_id)
		host->max_id--;

	if (eeprom->channel_cfg & NAC_SCANLUN)
		host->max_lun = 8;
	else
		host->max_lun = 1;
}


/**
 * adapter_init_chip - Get the chip into a know state and figure out
 * some of the settings that apply to this adapter.
 *
 * The io port in the adapter needs to have been set before calling
 * this function. The config will be configured correctly on return.
 *
 * @acb: The adapter which we are to init.
 **/
static void adapter_init_chip(struct AdapterCtlBlk *acb)
{
        struct NvRamType *eeprom = &acb->eeprom;
        
        /* Mask all the interrupt */
	DC395x_write8(acb, TRM_S1040_DMA_INTEN, 0x00);
	DC395x_write8(acb, TRM_S1040_SCSI_INTEN, 0x00);

	/* Reset SCSI module */
	DC395x_write16(acb, TRM_S1040_SCSI_CONTROL, DO_RSTMODULE);

	/* Reset PCI/DMA module */
	DC395x_write8(acb, TRM_S1040_DMA_CONTROL, DMARESETMODULE);
	udelay(20);

	/* program configuration 0 */
	acb->config = HCC_AUTOTERM | HCC_PARITY;
	if (DC395x_read8(acb, TRM_S1040_GEN_STATUS) & WIDESCSI)
		acb->config |= HCC_WIDE_CARD;

	if (eeprom->channel_cfg & NAC_POWERON_SCSI_RESET)
		acb->config |= HCC_SCSI_RESET;

	if (acb->config & HCC_SCSI_RESET) {
		dprintkl(KERN_INFO, "Performing initial SCSI bus reset\n");
		DC395x_write8(acb, TRM_S1040_SCSI_CONTROL, DO_RSTSCSI);

		/*while (!( DC395x_read8(acb, TRM_S1040_SCSI_INTSTATUS) & INT_SCSIRESET )); */
		/*spin_unlock_irq (&io_request_lock); */
		udelay(500);

		acb->last_reset =
		    jiffies + HZ / 2 +
		    HZ * acb->eeprom.delay_time;

		/*spin_lock_irq (&io_request_lock); */
	}
}


/**
 * adapter_init - Grab the resource for the card, setup the adapter
 * information, set the card into a known state, create the various
 * tables etc etc. This basically gets all adapter information all up
 * to date, initialised and gets the chip in sync with it.
 *
 * @acb:	The adapter which we are to init.
 * @io_port:	The base I/O port
 * @io_port_len: The I/O port size
 * @irq:	IRQ
 *
 * Returns 0 if the initialization succeeds, any other value on
 * failure.
 **/
static int adapter_init(struct AdapterCtlBlk *acb, unsigned long io_port,
			u32 io_port_len, unsigned int irq)
{
	if (!request_region(io_port, io_port_len, DC395X_NAME)) {
		dprintkl(KERN_ERR, "Failed to reserve IO region 0x%lx\n", io_port);
		goto failed;
	}
	/* store port base to indicate we have registered it */
	acb->io_port_base = io_port;
	acb->io_port_len = io_port_len;
	
	if (request_irq(irq, dc395x_interrupt, IRQF_SHARED, DC395X_NAME, acb)) {
	    	/* release the region we just claimed */
		dprintkl(KERN_INFO, "Failed to register IRQ\n");
		goto failed;
	}
	/* store irq to indicate we have registered it */
	acb->irq_level = irq;

	/* get eeprom configuration information and command line settings etc */
	check_eeprom(&acb->eeprom, io_port);
 	print_eeprom_settings(&acb->eeprom);

	/* setup adapter control block */	
	adapter_init_params(acb);
	
	/* display card connectors/termination settings */
 	adapter_print_config(acb);

	if (adapter_sg_tables_alloc(acb)) {
		dprintkl(KERN_DEBUG, "Memory allocation for SG tables failed\n");
		goto failed;
	}
	adapter_init_scsi_host(acb->scsi_host);
	adapter_init_chip(acb);
	set_basic_config(acb);

	dprintkdbg(DBG_0,
		"adapter_init: acb=%p, pdcb_map=%p psrb_array=%p "
		"size{acb=0x%04x dcb=0x%04x srb=0x%04x}\n",
		acb, acb->dcb_map, acb->srb_array, sizeof(struct AdapterCtlBlk),
		sizeof(struct DeviceCtlBlk), sizeof(struct ScsiReqBlk));
	return 0;

failed:
	if (acb->irq_level)
		free_irq(acb->irq_level, acb);
	if (acb->io_port_base)
		release_region(acb->io_port_base, acb->io_port_len);
	adapter_sg_tables_free(acb);

	return 1;
}


/**
 * adapter_uninit_chip - cleanly shut down the scsi controller chip,
 * stopping all operations and disabling interrupt generation on the
 * card.
 *
 * @acb: The adapter which we are to shutdown.
 **/
static void adapter_uninit_chip(struct AdapterCtlBlk *acb)
{
	/* disable interrupts */
	DC395x_write8(acb, TRM_S1040_DMA_INTEN, 0);
	DC395x_write8(acb, TRM_S1040_SCSI_INTEN, 0);

	/* reset the scsi bus */
	if (acb->config & HCC_SCSI_RESET)
		reset_scsi_bus(acb);

	/* clear any pending interrupt state */
	DC395x_read8(acb, TRM_S1040_SCSI_INTSTATUS);
}



/**
 * adapter_uninit - Shut down the chip and release any resources that
 * we had allocated. Once this returns the adapter should not be used
 * anymore.
 *
 * @acb: The adapter which we are to un-initialize.
 **/
static void adapter_uninit(struct AdapterCtlBlk *acb)
{
	unsigned long flags;
	DC395x_LOCK_IO(acb->scsi_host, flags);

	/* remove timers */
	if (timer_pending(&acb->waiting_timer))
		del_timer(&acb->waiting_timer);
	if (timer_pending(&acb->selto_timer))
		del_timer(&acb->selto_timer);

	adapter_uninit_chip(acb);
	adapter_remove_and_free_all_devices(acb);
	DC395x_UNLOCK_IO(acb->scsi_host, flags);

	if (acb->irq_level)
		free_irq(acb->irq_level, acb);
	if (acb->io_port_base)
		release_region(acb->io_port_base, acb->io_port_len);

	adapter_sg_tables_free(acb);
}


#undef YESNO
#define YESNO(YN) \
 if (YN) seq_printf(m, " Yes ");\
 else seq_printf(m, " No  ")

static int dc395x_show_info(struct seq_file *m, struct Scsi_Host *host)
{
	struct AdapterCtlBlk *acb = (struct AdapterCtlBlk *)host->hostdata;
	int spd, spd1;
	struct DeviceCtlBlk *dcb;
	unsigned long flags;
	int dev;

	seq_puts(m, DC395X_BANNER " PCI SCSI Host Adapter\n"
		" Driver Version " DC395X_VERSION "\n");

	DC395x_LOCK_IO(acb->scsi_host, flags);

	seq_printf(m, "SCSI Host Nr %i, ", host->host_no);
	seq_printf(m, "DC395U/UW/F DC315/U %s\n",
		(acb->config & HCC_WIDE_CARD) ? "Wide" : "");
	seq_printf(m, "io_port_base 0x%04lx, ", acb->io_port_base);
	seq_printf(m, "irq_level 0x%04x, ", acb->irq_level);
	seq_printf(m, " SelTimeout %ims\n", (1638 * acb->sel_timeout) / 1000);

	seq_printf(m, "MaxID %i, MaxLUN %llu, ", host->max_id, host->max_lun);
	seq_printf(m, "AdapterID %i\n", host->this_id);

	seq_printf(m, "tag_max_num %i", acb->tag_max_num);
	/*seq_printf(m, ", DMA_Status %i\n", DC395x_read8(acb, TRM_S1040_DMA_STATUS)); */
	seq_printf(m, ", FilterCfg 0x%02x",
		DC395x_read8(acb, TRM_S1040_SCSI_CONFIG1));
	seq_printf(m, ", DelayReset %is\n", acb->eeprom.delay_time);
	/*seq_printf(m, "\n"); */

	seq_printf(m, "Nr of DCBs: %i\n", list_size(&acb->dcb_list));
	seq_printf(m, "Map of attached LUNs: %8ph\n", &acb->dcb_map[0]);
	seq_printf(m, "                      %8ph\n", &acb->dcb_map[8]);

	seq_puts(m,
		 "Un ID LUN Prty Sync Wide DsCn SndS TagQ nego_period SyncFreq SyncOffs MaxCmd\n");

	dev = 0;
	list_for_each_entry(dcb, &acb->dcb_list, list) {
		int nego_period;
		seq_printf(m, "%02i %02i  %02i ", dev, dcb->target_id,
			dcb->target_lun);
		YESNO(dcb->dev_mode & NTC_DO_PARITY_CHK);
		YESNO(dcb->sync_offset);
		YESNO(dcb->sync_period & WIDE_SYNC);
		YESNO(dcb->dev_mode & NTC_DO_DISCONNECT);
		YESNO(dcb->dev_mode & NTC_DO_SEND_START);
		YESNO(dcb->sync_mode & EN_TAG_QUEUEING);
		nego_period = clock_period[dcb->sync_period & 0x07] << 2;
		if (dcb->sync_offset)
			seq_printf(m, "  %03i ns ", nego_period);
		else
			seq_printf(m, " (%03i ns)", (dcb->min_nego_period << 2));

		if (dcb->sync_offset & 0x0f) {
			spd = 1000 / (nego_period);
			spd1 = 1000 % (nego_period);
			spd1 = (spd1 * 10 + nego_period / 2) / (nego_period);
			seq_printf(m, "   %2i.%1i M     %02i ", spd, spd1,
				(dcb->sync_offset & 0x0f));
		} else
			seq_puts(m, "                 ");

		/* Add more info ... */
		seq_printf(m, "     %02i\n", dcb->max_command);
		dev++;
	}

	if (timer_pending(&acb->waiting_timer))
		seq_puts(m, "Waiting queue timer running\n");
	else
		seq_putc(m, '\n');

	list_for_each_entry(dcb, &acb->dcb_list, list) {
		struct ScsiReqBlk *srb;
		if (!list_empty(&dcb->srb_waiting_list))
			seq_printf(m, "DCB (%02i-%i): Waiting: %i:",
				dcb->target_id, dcb->target_lun,
				list_size(&dcb->srb_waiting_list));
                list_for_each_entry(srb, &dcb->srb_waiting_list, list)
			seq_printf(m, " %p", srb->cmd);
		if (!list_empty(&dcb->srb_going_list))
			seq_printf(m, "\nDCB (%02i-%i): Going  : %i:",
				dcb->target_id, dcb->target_lun,
				list_size(&dcb->srb_going_list));
		list_for_each_entry(srb, &dcb->srb_going_list, list)
			seq_printf(m, " %p", srb->cmd);
		if (!list_empty(&dcb->srb_waiting_list) || !list_empty(&dcb->srb_going_list))
			seq_putc(m, '\n');
	}

	if (debug_enabled(DBG_1)) {
		seq_printf(m, "DCB list for ACB %p:\n", acb);
		list_for_each_entry(dcb, &acb->dcb_list, list) {
			seq_printf(m, "%p -> ", dcb);
		}
		seq_puts(m, "END\n");
	}

	DC395x_UNLOCK_IO(acb->scsi_host, flags);
	return 0;
}


static const struct scsi_host_template dc395x_driver_template = {
	.module                 = THIS_MODULE,
	.proc_name              = DC395X_NAME,
	.show_info              = dc395x_show_info,
	.name                   = DC395X_BANNER " " DC395X_VERSION,
	.queuecommand           = dc395x_queue_command,
	.slave_alloc            = dc395x_slave_alloc,
	.slave_destroy          = dc395x_slave_destroy,
	.can_queue              = DC395x_MAX_CAN_QUEUE,
	.this_id                = 7,
	.sg_tablesize           = DC395x_MAX_SG_TABLESIZE,
	.cmd_per_lun            = DC395x_MAX_CMD_PER_LUN,
	.eh_abort_handler       = dc395x_eh_abort,
	.eh_bus_reset_handler   = dc395x_eh_bus_reset,
	.dma_boundary		= PAGE_SIZE - 1,
};


/**
 * banner_display - Display banner on first instance of driver
 * initialized.
 **/
static void banner_display(void)
{
	static int banner_done = 0;
	if (!banner_done)
	{
		dprintkl(KERN_INFO, "%s %s\n", DC395X_BANNER, DC395X_VERSION);
		banner_done = 1;
	}
}


/**
 * dc395x_init_one - Initialise a single instance of the adapter.
 *
 * The PCI layer will call this once for each instance of the adapter
 * that it finds in the system. The pci_dev strcuture indicates which
 * instance we are being called from.
 * 
 * @dev: The PCI device to initialize.
 * @id: Looks like a pointer to the entry in our pci device table
 * that was actually matched by the PCI subsystem.
 *
 * Returns 0 on success, or an error code (-ve) on failure.
 **/
static int dc395x_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct Scsi_Host *scsi_host = NULL;
	struct AdapterCtlBlk *acb = NULL;
	unsigned long io_port_base;
	unsigned int io_port_len;
	unsigned int irq;
	
	dprintkdbg(DBG_0, "Init one instance (%s)\n", pci_name(dev));
	banner_display();

	if (pci_enable_device(dev))
	{
		dprintkl(KERN_INFO, "PCI Enable device failed.\n");
		return -ENODEV;
	}
	io_port_base = pci_resource_start(dev, 0) & PCI_BASE_ADDRESS_IO_MASK;
	io_port_len = pci_resource_len(dev, 0);
	irq = dev->irq;
	dprintkdbg(DBG_0, "IO_PORT=0x%04lx, IRQ=0x%x\n", io_port_base, dev->irq);

	/* allocate scsi host information (includes out adapter) */
	scsi_host = scsi_host_alloc(&dc395x_driver_template,
				    sizeof(struct AdapterCtlBlk));
	if (!scsi_host) {
		dprintkl(KERN_INFO, "scsi_host_alloc failed\n");
		goto fail;
	}
 	acb = (struct AdapterCtlBlk*)scsi_host->hostdata;
 	acb->scsi_host = scsi_host;
 	acb->dev = dev;

	/* initialise the adapter and everything we need */
 	if (adapter_init(acb, io_port_base, io_port_len, irq)) {
		dprintkl(KERN_INFO, "adapter init failed\n");
		acb = NULL;
		goto fail;
	}

	pci_set_master(dev);

	/* get the scsi mid level to scan for new devices on the bus */
	if (scsi_add_host(scsi_host, &dev->dev)) {
		dprintkl(KERN_ERR, "scsi_add_host failed\n");
		goto fail;
	}
	pci_set_drvdata(dev, scsi_host);
	scsi_scan_host(scsi_host);
        	
	return 0;

fail:
	if (acb != NULL)
		adapter_uninit(acb);
	if (scsi_host != NULL)
		scsi_host_put(scsi_host);
	pci_disable_device(dev);
	return -ENODEV;
}


/**
 * dc395x_remove_one - Called to remove a single instance of the
 * adapter.
 *
 * @dev: The PCI device to initialize.
 **/
static void dc395x_remove_one(struct pci_dev *dev)
{
	struct Scsi_Host *scsi_host = pci_get_drvdata(dev);
	struct AdapterCtlBlk *acb = (struct AdapterCtlBlk *)(scsi_host->hostdata);

	dprintkdbg(DBG_0, "dc395x_remove_one: acb=%p\n", acb);

	scsi_remove_host(scsi_host);
	adapter_uninit(acb);
	pci_disable_device(dev);
	scsi_host_put(scsi_host);
}


static struct pci_device_id dc395x_pci_table[] = {
	{
		.vendor		= PCI_VENDOR_ID_TEKRAM,
		.device		= PCI_DEVICE_ID_TEKRAM_TRMS1040,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
	 },
	{}			/* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, dc395x_pci_table);


static struct pci_driver dc395x_driver = {
	.name           = DC395X_NAME,
	.id_table       = dc395x_pci_table,
	.probe          = dc395x_init_one,
	.remove         = dc395x_remove_one,
};
module_pci_driver(dc395x_driver);

MODULE_AUTHOR("C.L. Huang / Erich Chen / Kurt Garloff");
MODULE_DESCRIPTION("SCSI host adapter driver for Tekram TRM-S1040 based adapters: Tekram DC395 and DC315 series");
MODULE_LICENSE("GPL");
