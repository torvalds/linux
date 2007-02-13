/*
 *
 *			Linux MegaRAID Unified device driver
 *
 * Copyright (c) 2003-2004  LSI Logic Corporation.
 *
 *	   This program is free software; you can redistribute it and/or
 *	   modify it under the terms of the GNU General Public License
 *	   as published by the Free Software Foundation; either version
 *	   2 of the License, or (at your option) any later version.
 *
 * FILE		: mbox_defs.h
 *
 */
#ifndef _MRAID_MBOX_DEFS_H_
#define _MRAID_MBOX_DEFS_H_

#include <linux/types.h>

/*
 * Commands and states for mailbox based controllers
 */

#define MBOXCMD_LREAD		0x01
#define MBOXCMD_LWRITE		0x02
#define MBOXCMD_PASSTHRU	0x03
#define MBOXCMD_ADPEXTINQ	0x04
#define MBOXCMD_ADAPTERINQ	0x05
#define MBOXCMD_LREAD64		0xA7
#define MBOXCMD_LWRITE64	0xA8
#define MBOXCMD_PASSTHRU64	0xC3
#define MBOXCMD_EXTPTHRU	0xE3

#define MAIN_MISC_OPCODE	0xA4
#define GET_MAX_SG_SUPPORT	0x01
#define SUPPORT_EXT_CDB		0x16

#define FC_NEW_CONFIG		0xA1
#define NC_SUBOP_PRODUCT_INFO	0x0E
#define NC_SUBOP_ENQUIRY3	0x0F
#define ENQ3_GET_SOLICITED_FULL	0x02
#define OP_DCMD_READ_CONFIG	0x04
#define NEW_READ_CONFIG_8LD	0x67
#define READ_CONFIG_8LD		0x07
#define FLUSH_ADAPTER		0x0A
#define FLUSH_SYSTEM		0xFE

/*
 * Command for random deletion of logical drives
 */
#define	FC_DEL_LOGDRV		0xA4
#define	OP_SUP_DEL_LOGDRV	0x2A
#define OP_GET_LDID_MAP		0x18
#define OP_DEL_LOGDRV		0x1C

/*
 * BIOS commands
 */
#define IS_BIOS_ENABLED		0x62
#define GET_BIOS		0x01
#define CHNL_CLASS		0xA9
#define GET_CHNL_CLASS		0x00
#define SET_CHNL_CLASS		0x01
#define CH_RAID			0x01
#define CH_SCSI			0x00
#define BIOS_PVT_DATA		0x40
#define GET_BIOS_PVT_DATA	0x00


/*
 * Commands to support clustering
 */
#define GET_TARGET_ID		0x7D
#define CLUSTER_OP		0x70
#define GET_CLUSTER_MODE	0x02
#define CLUSTER_CMD		0x6E
#define RESERVE_LD		0x01
#define RELEASE_LD		0x02
#define RESET_RESERVATIONS	0x03
#define RESERVATION_STATUS	0x04
#define RESERVE_PD		0x05
#define RELEASE_PD		0x06


/*
 * Module battery status
 */
#define BATTERY_MODULE_MISSING		0x01
#define BATTERY_LOW_VOLTAGE		0x02
#define BATTERY_TEMP_HIGH		0x04
#define BATTERY_PACK_MISSING		0x08
#define BATTERY_CHARGE_MASK		0x30
#define BATTERY_CHARGE_DONE		0x00
#define BATTERY_CHARGE_INPROG		0x10
#define BATTERY_CHARGE_FAIL		0x20
#define BATTERY_CYCLES_EXCEEDED		0x40

/*
 * Physical drive states.
 */
#define PDRV_UNCNF	0
#define PDRV_ONLINE	3
#define PDRV_FAILED	4
#define PDRV_RBLD	5
#define PDRV_HOTSPARE	6


/*
 * Raid logical drive states.
 */
#define RDRV_OFFLINE	0
#define RDRV_DEGRADED	1
#define RDRV_OPTIMAL	2
#define RDRV_DELETED	3

/*
 * Read, write and cache policies
 */
#define NO_READ_AHEAD		0
#define READ_AHEAD		1
#define ADAP_READ_AHEAD		2
#define WRMODE_WRITE_THRU	0
#define WRMODE_WRITE_BACK	1
#define CACHED_IO		0
#define DIRECT_IO		1

#define MAX_LOGICAL_DRIVES_8LD		8
#define MAX_LOGICAL_DRIVES_40LD		40
#define FC_MAX_PHYSICAL_DEVICES		256
#define MAX_MBOX_CHANNELS		5
#define MAX_MBOX_TARGET			15
#define MBOX_MAX_PHYSICAL_DRIVES	MAX_MBOX_CHANNELS*MAX_MBOX_TARGET
#define MAX_ROW_SIZE_40LD		32
#define MAX_ROW_SIZE_8LD		8
#define SPAN_DEPTH_8_SPANS		8
#define SPAN_DEPTH_4_SPANS		4
#define MAX_REQ_SENSE_LEN		0x20



/**
 * struct mbox_t - Driver and f/w handshake structure.
 * @cmd		: firmware command
 * @cmdid	: command id
 * @numsectors	: number of sectors to be transferred
 * @lba		: Logical Block Address on LD
 * @xferaddr	: DMA address for data transfer
 * @logdrv	: logical drive number
 * @numsge	: number of scatter gather elements in sg list
 * @resvd	: reserved
 * @busy	: f/w busy, must wait to issue more commands.
 * @numstatus	: number of commands completed.
 * @status	: status of the commands completed
 * @completed	: array of completed command ids.
 * @poll	: poll and ack sequence
 * @ack		: poll and ack sequence
 *
 * The central handshake structure between the driver and the firmware. This
 * structure must be allocated by the driver and aligned at 8-byte boundary.
 */
#define MBOX_MAX_FIRMWARE_STATUS	46
typedef struct {
	uint8_t		cmd;
	uint8_t		cmdid;
	uint16_t	numsectors;
	uint32_t	lba;
	uint32_t	xferaddr;
	uint8_t		logdrv;
	uint8_t		numsge;
	uint8_t		resvd;
	uint8_t		busy;
	uint8_t		numstatus;
	uint8_t		status;
	uint8_t		completed[MBOX_MAX_FIRMWARE_STATUS];
	uint8_t		poll;
	uint8_t		ack;
} __attribute__ ((packed)) mbox_t;


/**
 * mbox64_t - 64-bit extension for the mailbox
 * @segment_lo	: the low 32-bits of the address of the scatter-gather list
 * @segment_hi	: the upper 32-bits of the address of the scatter-gather list
 * @mbox	: 32-bit mailbox, whose xferadder field must be set to
 *		0xFFFFFFFF
 *
 * This is the extension of the 32-bit mailbox to be able to perform DMA
 * beyond 4GB address range.
 */
typedef struct {
	uint32_t	xferaddr_lo;
	uint32_t	xferaddr_hi;
	mbox_t		mbox32;
} __attribute__ ((packed)) mbox64_t;

/*
 * mailbox structure used for internal commands
 */
typedef struct {
	u8	cmd;
	u8	cmdid;
	u8	opcode;
	u8	subopcode;
	u32	lba;
	u32	xferaddr;
	u8	logdrv;
	u8	rsvd[3];
	u8	numstatus;
	u8	status;
} __attribute__ ((packed)) int_mbox_t;

/**
 * mraid_passthru_t - passthru structure to issue commands to physical devices
 * @timeout		: command timeout, 0=6sec, 1=60sec, 2=10min, 3=3hr
 * @ars			: set if ARS required after check condition
 * @islogical		: set if command meant for logical devices
 * @logdrv		: logical drive number if command for LD
 * @channel		: Channel on which physical device is located
 * @target		: SCSI target of the device
 * @queuetag		: unused
 * @queueaction		: unused
 * @cdb			: SCSI CDB
 * @cdblen		: length of the CDB
 * @reqsenselen		: amount of request sense data to be returned
 * @reqsensearea	: Sense information buffer
 * @numsge		: number of scatter-gather elements in the sg list
 * @scsistatus		: SCSI status of the command completed.
 * @dataxferaddr	: DMA data transfer address
 * @dataxferlen		: amount of the data to be transferred.
 */
typedef struct {
	uint8_t		timeout		:3;
	uint8_t		ars		:1;
	uint8_t		reserved	:3;
	uint8_t		islogical	:1;
	uint8_t		logdrv;
	uint8_t		channel;
	uint8_t		target;
	uint8_t		queuetag;
	uint8_t		queueaction;
	uint8_t		cdb[10];
	uint8_t		cdblen;
	uint8_t		reqsenselen;
	uint8_t		reqsensearea[MAX_REQ_SENSE_LEN];
	uint8_t		numsge;
	uint8_t		scsistatus;
	uint32_t	dataxferaddr;
	uint32_t	dataxferlen;
} __attribute__ ((packed)) mraid_passthru_t;

typedef struct {

	uint32_t		dataxferaddr_lo;
	uint32_t		dataxferaddr_hi;
	mraid_passthru_t	pthru32;

} __attribute__ ((packed)) mega_passthru64_t;

/**
 * mraid_epassthru_t - passthru structure to issue commands to physical devices
 * @timeout		: command timeout, 0=6sec, 1=60sec, 2=10min, 3=3hr
 * @ars			: set if ARS required after check condition
 * @rsvd1		: reserved field
 * @cd_rom		: (?)
 * @rsvd2		: reserved field
 * @islogical		: set if command meant for logical devices
 * @logdrv		: logical drive number if command for LD
 * @channel		: Channel on which physical device is located
 * @target		: SCSI target of the device
 * @queuetag		: unused
 * @queueaction		: unused
 * @cdblen		: length of the CDB
 * @rsvd3		: reserved field
 * @cdb			: SCSI CDB
 * @numsge		: number of scatter-gather elements in the sg list
 * @status		: SCSI status of the command completed.
 * @reqsenselen		: amount of request sense data to be returned
 * @reqsensearea	: Sense information buffer
 * @rsvd4		: reserved field
 * @dataxferaddr	: DMA data transfer address
 * @dataxferlen		: amount of the data to be transferred.
 */
typedef struct {
	uint8_t		timeout		:3;
	uint8_t		ars		:1;
	uint8_t		rsvd1		:1;
	uint8_t		cd_rom		:1;
	uint8_t		rsvd2		:1;
	uint8_t		islogical	:1;
	uint8_t		logdrv;
	uint8_t		channel;
	uint8_t		target;
	uint8_t		queuetag;
	uint8_t		queueaction;
	uint8_t		cdblen;
	uint8_t		rsvd3;
	uint8_t		cdb[16];
	uint8_t		numsge;
	uint8_t		status;
	uint8_t		reqsenselen;
	uint8_t		reqsensearea[MAX_REQ_SENSE_LEN];
	uint8_t		rsvd4;
	uint32_t	dataxferaddr;
	uint32_t	dataxferlen;
} __attribute__ ((packed)) mraid_epassthru_t;


/**
 * mraid_pinfo_t - product info, static information about the controller
 * @data_size		: current size in bytes (not including resvd)
 * @config_signature	: Current value is 0x00282008
 * @fw_version		: Firmware version
 * @bios_version	: version of the BIOS
 * @product_name	: Name given to the controller
 * @max_commands	: Maximum concurrent commands supported
 * @nchannels		: Number of SCSI Channels detected
 * @fc_loop_present	: Number of Fibre Loops detected
 * @mem_type		: EDO, FPM, SDRAM etc
 * @signature		:
 * @dram_size		: In terms of MB
 * @subsysid		: device PCI subsystem ID
 * @subsysvid		: device PCI subsystem vendor ID
 * @notify_counters	:
 * @pad1k		: 135 + 889 resvd = 1024 total size
 *
 * This structures holds the information about the controller which is not
 * expected to change dynamically.
 *
 * The current value of config signature is 0x00282008:
 * 0x28 = MAX_LOGICAL_DRIVES,
 * 0x20 = Number of stripes and
 * 0x08 = Number of spans
 */
typedef struct {
	uint32_t	data_size;
	uint32_t	config_signature;
	uint8_t		fw_version[16];
	uint8_t		bios_version[16];
	uint8_t		product_name[80];
	uint8_t		max_commands;
	uint8_t		nchannels;
	uint8_t		fc_loop_present;
	uint8_t		mem_type;
	uint32_t	signature;
	uint16_t	dram_size;
	uint16_t	subsysid;
	uint16_t	subsysvid;
	uint8_t		notify_counters;
	uint8_t		pad1k[889];
} __attribute__ ((packed)) mraid_pinfo_t;


/**
 * mraid_notify_t - the notification structure
 * @global_counter		: Any change increments this counter
 * @param_counter		: Indicates any params changed
 * @param_id			: Param modified - defined below
 * @param_val			: New val of last param modified
 * @write_config_counter	: write config occurred
 * @write_config_rsvd		:
 * @ldrv_op_counter		: Indicates ldrv op started/completed
 * @ldrv_opid			: ldrv num
 * @ldrv_opcmd			: ldrv operation - defined below
 * @ldrv_opstatus		: status of the operation
 * @ldrv_state_counter		: Indicates change of ldrv state
 * @ldrv_state_id		: ldrv num
 * @ldrv_state_new		: New state
 * @ldrv_state_old		: old state
 * @pdrv_state_counter		: Indicates change of ldrv state
 * @pdrv_state_id		: pdrv id
 * @pdrv_state_new		: New state
 * @pdrv_state_old		: old state
 * @pdrv_fmt_counter		: Indicates pdrv format started/over
 * @pdrv_fmt_id			: pdrv id
 * @pdrv_fmt_val		: format started/over
 * @pdrv_fmt_rsvd		:
 * @targ_xfer_counter		: Indicates SCSI-2 Xfer rate change
 * @targ_xfer_id		: pdrv Id
 * @targ_xfer_val		: new Xfer params of last pdrv
 * @targ_xfer_rsvd		:
 * @fcloop_id_chg_counter	: Indicates loopid changed
 * @fcloopid_pdrvid		: pdrv id
 * @fcloop_id0			: loopid on fc loop 0
 * @fcloop_id1			: loopid on fc loop 1
 * @fcloop_state_counter	: Indicates loop state changed
 * @fcloop_state0		: state of fc loop 0
 * @fcloop_state1		: state of fc loop 1
 * @fcloop_state_rsvd		:
 */
typedef struct {
	uint32_t	global_counter;
	uint8_t		param_counter;
	uint8_t		param_id;
	uint16_t	param_val;
	uint8_t		write_config_counter;
	uint8_t		write_config_rsvd[3];
	uint8_t		ldrv_op_counter;
	uint8_t		ldrv_opid;
	uint8_t		ldrv_opcmd;
	uint8_t		ldrv_opstatus;
	uint8_t		ldrv_state_counter;
	uint8_t		ldrv_state_id;
	uint8_t		ldrv_state_new;
	uint8_t		ldrv_state_old;
	uint8_t		pdrv_state_counter;
	uint8_t		pdrv_state_id;
	uint8_t		pdrv_state_new;
	uint8_t		pdrv_state_old;
	uint8_t		pdrv_fmt_counter;
	uint8_t		pdrv_fmt_id;
	uint8_t		pdrv_fmt_val;
	uint8_t		pdrv_fmt_rsvd;
	uint8_t		targ_xfer_counter;
	uint8_t		targ_xfer_id;
	uint8_t		targ_xfer_val;
	uint8_t		targ_xfer_rsvd;
	uint8_t		fcloop_id_chg_counter;
	uint8_t		fcloopid_pdrvid;
	uint8_t		fcloop_id0;
	uint8_t		fcloop_id1;
	uint8_t		fcloop_state_counter;
	uint8_t		fcloop_state0;
	uint8_t		fcloop_state1;
	uint8_t		fcloop_state_rsvd;
} __attribute__ ((packed)) mraid_notify_t;


/**
 * mraid_inquiry3_t - enquiry for device information
 *
 * @data_size		: current size in bytes (not including resvd)
 * @notify		:
 * @notify_rsvd		:
 * @rebuild_rate	: rebuild rate (0% - 100%)
 * @cache_flush_int	: cache flush interval in seconds
 * @sense_alert		:
 * @drive_insert_count	: drive insertion count
 * @battery_status	:
 * @num_ldrv		: no. of Log Drives configured
 * @recon_state		: state of reconstruct
 * @ldrv_op_status	: logdrv Status
 * @ldrv_size		: size of each log drv
 * @ldrv_prop		:
 * @ldrv_state		: state of log drives
 * @pdrv_state		: state of phys drvs.
 * @pdrv_format		:
 * @targ_xfer		: phys device transfer rate
 * @pad1k		: 761 + 263reserved = 1024 bytes total size
 */
#define MAX_NOTIFY_SIZE		0x80
#define CUR_NOTIFY_SIZE		sizeof(mraid_notify_t)

typedef struct {
	uint32_t	data_size;

	mraid_notify_t	notify;

	uint8_t		notify_rsvd[MAX_NOTIFY_SIZE - CUR_NOTIFY_SIZE];

	uint8_t		rebuild_rate;
	uint8_t		cache_flush_int;
	uint8_t		sense_alert;
	uint8_t		drive_insert_count;

	uint8_t		battery_status;
	uint8_t		num_ldrv;
	uint8_t		recon_state[MAX_LOGICAL_DRIVES_40LD / 8];
	uint16_t	ldrv_op_status[MAX_LOGICAL_DRIVES_40LD / 8];

	uint32_t	ldrv_size[MAX_LOGICAL_DRIVES_40LD];
	uint8_t		ldrv_prop[MAX_LOGICAL_DRIVES_40LD];
	uint8_t		ldrv_state[MAX_LOGICAL_DRIVES_40LD];
	uint8_t		pdrv_state[FC_MAX_PHYSICAL_DEVICES];
	uint16_t	pdrv_format[FC_MAX_PHYSICAL_DEVICES / 16];

	uint8_t		targ_xfer[80];
	uint8_t		pad1k[263];
} __attribute__ ((packed)) mraid_inquiry3_t;


/**
 * mraid_adapinfo_t - information about the adapter
 * @max_commands		: max concurrent commands supported
 * @rebuild_rate		: rebuild rate - 0% thru 100%
 * @max_targ_per_chan		: max targ per channel
 * @nchannels			: number of channels on HBA
 * @fw_version			: firmware version
 * @age_of_flash		: number of times FW has been flashed
 * @chip_set_value		: contents of 0xC0000832
 * @dram_size			: in MB
 * @cache_flush_interval	: in seconds
 * @bios_version		:
 * @board_type			:
 * @sense_alert			:
 * @write_config_count		: increase with every configuration change
 * @drive_inserted_count	: increase with every drive inserted
 * @inserted_drive		: channel:Id of inserted drive
 * @battery_status		: bit 0: battery module missing
 *				bit 1: VBAD
 *				bit 2: temprature high
 *				bit 3: battery pack missing
 *				bit 4,5:
 *					00 - charge complete
 *					01 - fast charge in progress
 *					10 - fast charge fail
 *					11 - undefined
 *				bit 6: counter > 1000
 *				bit 7: Undefined
 * @dec_fault_bus_info		:
 */
typedef struct {
	uint8_t		max_commands;
	uint8_t		rebuild_rate;
	uint8_t		max_targ_per_chan;
	uint8_t		nchannels;
	uint8_t		fw_version[4];
	uint16_t	age_of_flash;
	uint8_t		chip_set_value;
	uint8_t		dram_size;
	uint8_t		cache_flush_interval;
	uint8_t		bios_version[4];
	uint8_t		board_type;
	uint8_t		sense_alert;
	uint8_t		write_config_count;
	uint8_t		battery_status;
	uint8_t		dec_fault_bus_info;
} __attribute__ ((packed)) mraid_adapinfo_t;


/**
 * mraid_ldrv_info_t - information about the logical drives
 * @nldrv	: Number of logical drives configured
 * @rsvd	:
 * @size	: size of each logical drive
 * @prop	:
 * @state	: state of each logical drive
 */
typedef struct {
	uint8_t		nldrv;
	uint8_t		rsvd[3];
	uint32_t	size[MAX_LOGICAL_DRIVES_8LD];
	uint8_t		prop[MAX_LOGICAL_DRIVES_8LD];
	uint8_t		state[MAX_LOGICAL_DRIVES_8LD];
} __attribute__ ((packed)) mraid_ldrv_info_t;


/**
 * mraid_pdrv_info_t - information about the physical drives
 * @pdrv_state	: state of each physical drive
 */
typedef struct {
	uint8_t		pdrv_state[MBOX_MAX_PHYSICAL_DRIVES];
	uint8_t		rsvd;
} __attribute__ ((packed)) mraid_pdrv_info_t;


/**
 * mraid_inquiry_t - RAID inquiry, mailbox command 0x05
 * @mraid_adapinfo_t	: adapter information
 * @mraid_ldrv_info_t	: logical drives information
 * @mraid_pdrv_info_t	: physical drives information
 */
typedef struct {
	mraid_adapinfo_t	adapter_info;
	mraid_ldrv_info_t	logdrv_info;
	mraid_pdrv_info_t	pdrv_info;
} __attribute__ ((packed)) mraid_inquiry_t;


/**
 * mraid_extinq_t - RAID extended inquiry, mailbox command 0x04
 *
 * @raid_inq		: raid inquiry
 * @phys_drv_format	:
 * @stack_attn		:
 * @modem_status	:
 * @rsvd		:
 */
typedef struct {
	mraid_inquiry_t	raid_inq;
	uint16_t	phys_drv_format[MAX_MBOX_CHANNELS];
	uint8_t		stack_attn;
	uint8_t		modem_status;
	uint8_t		rsvd[2];
} __attribute__ ((packed)) mraid_extinq_t;


/**
 * adap_device_t - device information
 * @channel	: channel fpor the device
 * @target	: target ID of the device
 */
typedef struct {
	uint8_t		channel;
	uint8_t		target;
}__attribute__ ((packed)) adap_device_t;


/**
 * adap_span_40ld_t - 40LD span
 * @start_blk	: starting block
 * @num_blks	: number of blocks
 */
typedef struct {
	uint32_t	start_blk;
	uint32_t	num_blks;
	adap_device_t	device[MAX_ROW_SIZE_40LD];
}__attribute__ ((packed)) adap_span_40ld_t;


/**
 * adap_span_8ld_t - 8LD span
 * @start_blk	: starting block
 * @num_blks	: number of blocks
 */
typedef struct {
	uint32_t	start_blk;
	uint32_t	num_blks;
	adap_device_t	device[MAX_ROW_SIZE_8LD];
}__attribute__ ((packed)) adap_span_8ld_t;


/**
 * logdrv_param_t - logical drives parameters
 *
 * @span_depth	: total number of spans
 * @level	: RAID level
 * @read_ahead	: read ahead, no read ahead, adaptive read ahead
 * @stripe_sz	: encoded stripe size
 * @status	: status of the logical drive
 * @write_mode	: write mode, write_through/write_back
 * @direct_io	: direct io or through cache
 * @row_size	: number of stripes in a row
 */
typedef struct {
	uint8_t		span_depth;
	uint8_t		level;
	uint8_t		read_ahead;
	uint8_t		stripe_sz;
	uint8_t		status;
	uint8_t		write_mode;
	uint8_t		direct_io;
	uint8_t		row_size;
} __attribute__ ((packed)) logdrv_param_t;


/**
 * logdrv_40ld_t - logical drive definition for 40LD controllers
 * @lparam	: logical drives parameters
 * @span	: span
 */
typedef struct {
	logdrv_param_t		lparam;
	adap_span_40ld_t	span[SPAN_DEPTH_8_SPANS];
}__attribute__ ((packed)) logdrv_40ld_t;


/**
 * logdrv_8ld_span8_t - logical drive definition for 8LD controllers
 * @lparam	: logical drives parameters
 * @span	: span
 *
 * 8-LD logical drive with upto 8 spans
 */
typedef struct {
	logdrv_param_t	lparam;
	adap_span_8ld_t	span[SPAN_DEPTH_8_SPANS];
}__attribute__ ((packed)) logdrv_8ld_span8_t;


/**
 * logdrv_8ld_span4_t - logical drive definition for 8LD controllers
 * @lparam	: logical drives parameters
 * @span	: span
 *
 * 8-LD logical drive with upto 4 spans
 */
typedef struct {
	logdrv_param_t	lparam;
	adap_span_8ld_t	span[SPAN_DEPTH_4_SPANS];
}__attribute__ ((packed)) logdrv_8ld_span4_t;


/**
 * phys_drive_t - physical device information
 * @type	: Type of the device
 * @cur_status	: current status of the device
 * @tag_depth	: Level of tagging
 * @sync_neg	: sync negotiation - ENABLE or DISBALE
 * @size	: configurable size in terms of 512 byte
 */
typedef struct {
	uint8_t		type;
	uint8_t		cur_status;
	uint8_t		tag_depth;
	uint8_t		sync_neg;
	uint32_t	size;
}__attribute__ ((packed)) phys_drive_t;


/**
 * disk_array_40ld_t - disk array for 40LD controllers
 * @numldrv	: number of logical drives
 * @resvd	:
 * @ldrv	: logical drives information
 * @pdrv	: physical drives information
 */
typedef struct {
	uint8_t		numldrv;
	uint8_t		resvd[3];
	logdrv_40ld_t	ldrv[MAX_LOGICAL_DRIVES_40LD];
	phys_drive_t	pdrv[MBOX_MAX_PHYSICAL_DRIVES];
}__attribute__ ((packed)) disk_array_40ld_t;


/**
 * disk_array_8ld_span8_t - disk array for 8LD controllers
 * @numldrv	: number of logical drives
 * @resvd	:
 * @ldrv	: logical drives information
 * @pdrv	: physical drives information
 *
 * Disk array for 8LD logical drives with upto 8 spans
 */
typedef struct {
	uint8_t			numldrv;
	uint8_t			resvd[3];
	logdrv_8ld_span8_t	ldrv[MAX_LOGICAL_DRIVES_8LD];
	phys_drive_t		pdrv[MBOX_MAX_PHYSICAL_DRIVES];
}__attribute__ ((packed)) disk_array_8ld_span8_t;


/**
 * disk_array_8ld_span4_t - disk array for 8LD controllers
 * @numldrv	: number of logical drives
 * @resvd	:
 * @ldrv	: logical drives information
 * @pdrv	: physical drives information
 *
 * Disk array for 8LD logical drives with upto 4 spans
 */
typedef struct {
	uint8_t			numldrv;
	uint8_t			resvd[3];
	logdrv_8ld_span4_t	ldrv[MAX_LOGICAL_DRIVES_8LD];
	phys_drive_t		pdrv[MBOX_MAX_PHYSICAL_DRIVES];
}__attribute__ ((packed)) disk_array_8ld_span4_t;


/**
 * struct private_bios_data - bios private data for boot devices
 * @geometry	: bits 0-3 - BIOS geometry, 0x0001 - 1GB, 0x0010 - 2GB,
 *		0x1000 - 8GB, Others values are invalid
 * @unused	: bits 4-7 are unused
 * @boot_drv	: logical drive set as boot drive, 0..7 - for 8LD cards,
 * 		0..39 - for 40LD cards
 * @cksum	: 0-(sum of first 13 bytes of this structure)
 */
struct private_bios_data {
	uint8_t		geometry	:4;
	uint8_t		unused		:4;
	uint8_t		boot_drv;
	uint8_t		rsvd[12];
	uint16_t	cksum;
} __attribute__ ((packed));


/**
 * mbox_sgl64 - 64-bit scatter list for mailbox based controllers
 * @address	: address of the buffer
 * @length	: data transfer length
 */
typedef struct {
	uint64_t	address;
	uint32_t	length;
} __attribute__ ((packed)) mbox_sgl64;

/**
 * mbox_sgl32 - 32-bit scatter list for mailbox based controllers
 * @address	: address of the buffer
 * @length	: data transfer length
 */
typedef struct {
	uint32_t	address;
	uint32_t	length;
} __attribute__ ((packed)) mbox_sgl32;

#endif		// _MRAID_MBOX_DEFS_H_

/* vim: set ts=8 sw=8 tw=78: */
