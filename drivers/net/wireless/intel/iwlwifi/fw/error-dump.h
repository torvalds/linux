/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2014, 2018-2021 Intel Corporation
 * Copyright (C) 2014-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#ifndef __fw_error_dump_h__
#define __fw_error_dump_h__

#include <linux/types.h>
#include "fw/api/cmdhdr.h"

#define IWL_FW_ERROR_DUMP_BARKER	0x14789632
#define IWL_FW_INI_ERROR_DUMP_BARKER	0x14789633

/**
 * enum iwl_fw_error_dump_type - types of data in the dump file
 * @IWL_FW_ERROR_DUMP_CSR: Control Status Registers - from offset 0
 * @IWL_FW_ERROR_DUMP_RXF:
 * @IWL_FW_ERROR_DUMP_TXCMD: last TX command data, structured as
 *	&struct iwl_fw_error_dump_txcmd packets
 * @IWL_FW_ERROR_DUMP_DEV_FW_INFO:  struct %iwl_fw_error_dump_info
 *	info on the device / firmware.
 * @IWL_FW_ERROR_DUMP_FW_MONITOR: firmware monitor
 * @IWL_FW_ERROR_DUMP_PRPH: range of periphery registers - there can be several
 *	sections like this in a single file.
 * @IWL_FW_ERROR_DUMP_FH_REGS: range of FH registers
 * @IWL_FW_ERROR_DUMP_MEM: chunk of memory
 * @IWL_FW_ERROR_DUMP_ERROR_INFO: description of what triggered this dump.
 *	Structured as &struct iwl_fw_error_dump_trigger_desc.
 * @IWL_FW_ERROR_DUMP_RB: the content of an RB structured as
 *	&struct iwl_fw_error_dump_rb
 * @IWL_FW_ERROR_PAGING: UMAC's image memory segments which were
 *	paged to the DRAM.
 * @IWL_FW_ERROR_DUMP_RADIO_REG: Dump the radio registers.
 * @IWL_FW_ERROR_DUMP_EXTERNAL: used only by external code utilities, and
 *	for that reason is not in use in any other place in the Linux Wi-Fi
 *	stack.
 * @IWL_FW_ERROR_DUMP_MEM_CFG: the addresses and sizes of fifos in the smem,
 *	which we get from the fw after ALIVE. The content is structured as
 *	&struct iwl_fw_error_dump_smem_cfg.
 */
enum iwl_fw_error_dump_type {
	/* 0 is deprecated */
	IWL_FW_ERROR_DUMP_CSR = 1,
	IWL_FW_ERROR_DUMP_RXF = 2,
	IWL_FW_ERROR_DUMP_TXCMD = 3,
	IWL_FW_ERROR_DUMP_DEV_FW_INFO = 4,
	IWL_FW_ERROR_DUMP_FW_MONITOR = 5,
	IWL_FW_ERROR_DUMP_PRPH = 6,
	IWL_FW_ERROR_DUMP_TXF = 7,
	IWL_FW_ERROR_DUMP_FH_REGS = 8,
	IWL_FW_ERROR_DUMP_MEM = 9,
	IWL_FW_ERROR_DUMP_ERROR_INFO = 10,
	IWL_FW_ERROR_DUMP_RB = 11,
	IWL_FW_ERROR_DUMP_PAGING = 12,
	IWL_FW_ERROR_DUMP_RADIO_REG = 13,
	IWL_FW_ERROR_DUMP_INTERNAL_TXF = 14,
	IWL_FW_ERROR_DUMP_EXTERNAL = 15, /* Do not move */
	IWL_FW_ERROR_DUMP_MEM_CFG = 16,
	IWL_FW_ERROR_DUMP_D3_DEBUG_DATA = 17,

	IWL_FW_ERROR_DUMP_MAX,
};

/**
 * struct iwl_fw_error_dump_data - data for one type
 * @type: &enum iwl_fw_error_dump_type
 * @len: the length starting from %data
 * @data: the data itself
 */
struct iwl_fw_error_dump_data {
	__le32 type;
	__le32 len;
	__u8 data[];
} __packed;

/**
 * struct iwl_fw_error_dump_file - the layout of the header of the file
 * @barker: must be %IWL_FW_ERROR_DUMP_BARKER
 * @file_len: the length of all the file starting from %barker
 * @data: array of &struct iwl_fw_error_dump_data
 */
struct iwl_fw_error_dump_file {
	__le32 barker;
	__le32 file_len;
	u8 data[];
} __packed;

/**
 * struct iwl_fw_error_dump_txcmd - TX command data
 * @cmdlen: original length of command
 * @caplen: captured length of command (may be less)
 * @data: captured command data, @caplen bytes
 */
struct iwl_fw_error_dump_txcmd {
	__le32 cmdlen;
	__le32 caplen;
	u8 data[];
} __packed;

/**
 * struct iwl_fw_error_dump_fifo - RX/TX FIFO data
 * @fifo_num: number of FIFO (starting from 0)
 * @available_bytes: num of bytes available in FIFO (may be less than FIFO size)
 * @wr_ptr: position of write pointer
 * @rd_ptr: position of read pointer
 * @fence_ptr: position of fence pointer
 * @fence_mode: the current mode of the fence (before locking) -
 *	0=follow RD pointer ; 1 = freeze
 * @data: all of the FIFO's data
 */
struct iwl_fw_error_dump_fifo {
	__le32 fifo_num;
	__le32 available_bytes;
	__le32 wr_ptr;
	__le32 rd_ptr;
	__le32 fence_ptr;
	__le32 fence_mode;
	u8 data[];
} __packed;

enum iwl_fw_error_dump_family {
	IWL_FW_ERROR_DUMP_FAMILY_7 = 7,
	IWL_FW_ERROR_DUMP_FAMILY_8 = 8,
};

#define MAX_NUM_LMAC 2

/**
 * struct iwl_fw_error_dump_info - info on the device / firmware
 * @hw_type: the type of the device
 * @hw_step: the step of the device
 * @fw_human_readable: human readable FW version
 * @dev_human_readable: name of the device
 * @bus_human_readable: name of the bus used
 * @num_of_lmacs: the number of lmacs
 * @lmac_err_id: the lmac 0/1 error_id/rt_status that triggered the latest dump
 *	if the dump collection was not initiated by an assert, the value is 0
 * @umac_err_id: the umac error_id/rt_status that triggered the latest dump
 *	if the dump collection was not initiated by an assert, the value is 0
 */
struct iwl_fw_error_dump_info {
	__le32 hw_type;
	__le32 hw_step;
	u8 fw_human_readable[FW_VER_HUMAN_READABLE_SZ];
	u8 dev_human_readable[64];
	u8 bus_human_readable[8];
	u8 num_of_lmacs;
	__le32 umac_err_id;
	__le32 lmac_err_id[MAX_NUM_LMAC];
} __packed;

/**
 * struct iwl_fw_error_dump_fw_mon - FW monitor data
 * @fw_mon_wr_ptr: the position of the write pointer in the cyclic buffer
 * @fw_mon_base_ptr: base pointer of the data
 * @fw_mon_cycle_cnt: number of wraparounds
 * @fw_mon_base_high_ptr: used in AX210 devices, the base adderss is 64 bit
 *	so fw_mon_base_ptr holds LSB 32 bits and fw_mon_base_high_ptr hold
 *	MSB 32 bits
 * @reserved: for future use
 * @data: captured data
 */
struct iwl_fw_error_dump_fw_mon {
	__le32 fw_mon_wr_ptr;
	__le32 fw_mon_base_ptr;
	__le32 fw_mon_cycle_cnt;
	__le32 fw_mon_base_high_ptr;
	__le32 reserved[2];
	u8 data[];
} __packed;

#define MAX_NUM_LMAC 2
#define TX_FIFO_INTERNAL_MAX_NUM	6
#define TX_FIFO_MAX_NUM			15
/**
 * struct iwl_fw_error_dump_smem_cfg - Dump SMEM configuration
 *	This must follow &struct iwl_fwrt_shared_mem_cfg.
 * @num_lmacs: number of lmacs
 * @num_txfifo_entries: number of tx fifos
 * @lmac: sizes of lmacs txfifos and rxfifo1
 * @rxfifo2_size: size of rxfifo2
 * @internal_txfifo_addr: address of internal tx fifo
 * @internal_txfifo_size: size of internal tx fifo
 */
struct iwl_fw_error_dump_smem_cfg {
	__le32 num_lmacs;
	__le32 num_txfifo_entries;
	struct {
		__le32 txfifo_size[TX_FIFO_MAX_NUM];
		__le32 rxfifo1_size;
	} lmac[MAX_NUM_LMAC];
	__le32 rxfifo2_size;
	__le32 internal_txfifo_addr;
	__le32 internal_txfifo_size[TX_FIFO_INTERNAL_MAX_NUM];
} __packed;
/**
 * struct iwl_fw_error_dump_prph - periphery registers data
 * @prph_start: address of the first register in this chunk
 * @data: the content of the registers
 */
struct iwl_fw_error_dump_prph {
	__le32 prph_start;
	__le32 data[];
};

enum iwl_fw_error_dump_mem_type {
	IWL_FW_ERROR_DUMP_MEM_SRAM,
	IWL_FW_ERROR_DUMP_MEM_SMEM,
	IWL_FW_ERROR_DUMP_MEM_NAMED_MEM = 10,
};

/**
 * struct iwl_fw_error_dump_mem - chunk of memory
 * @type: &enum iwl_fw_error_dump_mem_type
 * @offset: the offset from which the memory was read
 * @data: the content of the memory
 */
struct iwl_fw_error_dump_mem {
	__le32 type;
	__le32 offset;
	u8 data[];
};

/* Dump version, used by the dump parser to differentiate between
 * different dump formats
 */
#define IWL_INI_DUMP_VER 1

/* Use bit 31 as dump info type to avoid colliding with region types */
#define IWL_INI_DUMP_INFO_TYPE BIT(31)

/**
 * struct iwl_fw_error_dump_data - data for one type
 * @type: &enum iwl_fw_ini_region_type
 * @sub_type: sub type id
 * @sub_type_ver: sub type version
 * @reserved: not in use
 * @len: the length starting from %data
 * @data: the data itself
 */
struct iwl_fw_ini_error_dump_data {
	u8 type;
	u8 sub_type;
	u8 sub_type_ver;
	u8 reserved;
	__le32 len;
	__u8 data[];
} __packed;

/**
 * struct iwl_fw_ini_dump_entry
 * @list: list of dump entries
 * @size: size of the data
 * @data: entry data
 */
struct iwl_fw_ini_dump_entry {
	struct list_head list;
	u32 size;
	u8 data[];
} __packed;

/**
 * struct iwl_fw_error_dump_file - header of dump file
 * @barker: must be %IWL_FW_INI_ERROR_DUMP_BARKER
 * @file_len: the length of all the file including the header
 */
struct iwl_fw_ini_dump_file_hdr {
	__le32 barker;
	__le32 file_len;
} __packed;

/**
 * struct iwl_fw_ini_fifo_hdr - fifo range header
 * @fifo_num: the fifo number. In case of umac rx fifo, set BIT(31) to
 *	distinguish between lmac and umac rx fifos
 * @num_of_registers: num of registers to dump, dword size each
 */
struct iwl_fw_ini_fifo_hdr {
	__le32 fifo_num;
	__le32 num_of_registers;
} __packed;

/**
 * struct iwl_fw_ini_error_dump_range - range of memory
 * @range_data_size: the size of this range, in bytes
 * @internal_base_addr: base address of internal memory range
 * @dram_base_addr: base address of dram monitor range
 * @page_num: page number of memory range
 * @fifo_hdr: fifo header of memory range
 * @fw_pkt: FW packet header of memory range
 * @data: the actual memory
 */
struct iwl_fw_ini_error_dump_range {
	__le32 range_data_size;
	union {
		__le32 internal_base_addr;
		__le64 dram_base_addr;
		__le32 page_num;
		struct iwl_fw_ini_fifo_hdr fifo_hdr;
		struct iwl_cmd_header fw_pkt_hdr;
	};
	__le32 data[];
} __packed;

/**
 * struct iwl_fw_ini_error_dump_header - ini region dump header
 * @version: dump version
 * @region_id: id of the region
 * @num_of_ranges: number of ranges in this region
 * @name_len: number of bytes allocated to the name string of this region
 * @name: name of the region
 */
struct iwl_fw_ini_error_dump_header {
	__le32 version;
	__le32 region_id;
	__le32 num_of_ranges;
	__le32 name_len;
	u8 name[IWL_FW_INI_MAX_NAME];
};

/**
 * struct iwl_fw_ini_error_dump - ini region dump
 * @header: the header of this region
 * @data: data of memory ranges in this region,
 *	see &struct iwl_fw_ini_error_dump_range
 */
struct iwl_fw_ini_error_dump {
	struct iwl_fw_ini_error_dump_header header;
	u8 data[];
} __packed;

/* This bit is used to differentiate between lmac and umac rxf */
#define IWL_RXF_UMAC_BIT BIT(31)

/**
 * struct iwl_fw_ini_error_dump_register - ini register dump
 * @addr: address of the register
 * @data: data of the register
 */
struct iwl_fw_ini_error_dump_register {
	__le32 addr;
	__le32 data;
} __packed;

/**
 * struct iwl_fw_ini_dump_cfg_name - configuration name
 * @image_type: image type the configuration is related to
 * @cfg_name_len: length of the configuration name
 * @cfg_name: name of the configuraiton
 */
struct iwl_fw_ini_dump_cfg_name {
	__le32 image_type;
	__le32 cfg_name_len;
	u8 cfg_name[IWL_FW_INI_MAX_CFG_NAME];
} __packed;

/* AX210's HW type */
#define IWL_AX210_HW_TYPE 0x42
/* How many bits to roll when adding to the HW type of AX210 HW */
#define IWL_AX210_HW_TYPE_ADDITION_SHIFT 12

/* struct iwl_fw_ini_dump_info - ini dump information
 * @version: dump version
 * @time_point: time point that caused the dump collection
 * @trigger_reason: reason of the trigger
 * @external_cfg_state: &enum iwl_ini_cfg_state
 * @ver_type: FW version type
 * @ver_subtype: FW version subype
 * @hw_step: HW step
 * @hw_type: HW type
 * @rf_id_flavor: HW RF id flavor
 * @rf_id_dash: HW RF id dash
 * @rf_id_step: HW RF id step
 * @rf_id_type: HW RF id type
 * @lmac_major: lmac major version
 * @lmac_minor: lmac minor version
 * @umac_major: umac major version
 * @umac_minor: umac minor version
 * @fw_mon_mode: FW monitor mode &enum iwl_fw_ini_buffer_location
 * @regions_mask: bitmap mask of regions ids in the dump
 * @build_tag_len: length of the build tag
 * @build_tag: build tag string
 * @num_of_cfg_names: number of configuration name structs
 * @cfg_names: configuration names
 */
struct iwl_fw_ini_dump_info {
	__le32 version;
	__le32 time_point;
	__le32 trigger_reason;
	__le32 external_cfg_state;
	__le32 ver_type;
	__le32 ver_subtype;
	__le32 hw_step;
	__le32 hw_type;
	__le32 rf_id_flavor;
	__le32 rf_id_dash;
	__le32 rf_id_step;
	__le32 rf_id_type;
	__le32 lmac_major;
	__le32 lmac_minor;
	__le32 umac_major;
	__le32 umac_minor;
	__le32 fw_mon_mode;
	__le64 regions_mask;
	__le32 build_tag_len;
	u8 build_tag[FW_VER_HUMAN_READABLE_SZ];
	__le32 num_of_cfg_names;
	struct iwl_fw_ini_dump_cfg_name cfg_names[];
} __packed;

/**
 * struct iwl_fw_ini_err_table_dump - ini error table dump
 * @header: header of the region
 * @version: error table version
 * @data: data of memory ranges in this region,
 *	see &struct iwl_fw_ini_error_dump_range
 */
struct iwl_fw_ini_err_table_dump {
	struct iwl_fw_ini_error_dump_header header;
	__le32 version;
	u8 data[];
} __packed;

/**
 * struct iwl_fw_error_dump_rb - content of an Receive Buffer
 * @index: the index of the Receive Buffer in the Rx queue
 * @rxq: the RB's Rx queue
 * @reserved:
 * @data: the content of the Receive Buffer
 */
struct iwl_fw_error_dump_rb {
	__le32 index;
	__le32 rxq;
	__le32 reserved;
	u8 data[];
};

/**
 * struct iwl_fw_ini_monitor_dump - ini monitor dump
 * @header: header of the region
 * @write_ptr: write pointer position in the buffer
 * @cycle_cnt: cycles count
 * @cur_frag: current fragment in use
 * @data: data of memory ranges in this region,
 *	see &struct iwl_fw_ini_error_dump_range
 */
struct iwl_fw_ini_monitor_dump {
	struct iwl_fw_ini_error_dump_header header;
	__le32 write_ptr;
	__le32 cycle_cnt;
	__le32 cur_frag;
	u8 data[];
} __packed;

/**
 * struct iwl_fw_ini_special_device_memory - special device memory
 * @header: header of the region
 * @type: type of special memory
 * @version: struct special memory version
 * @data: data of memory ranges in this region,
 *	see &struct iwl_fw_ini_error_dump_range
 */
struct iwl_fw_ini_special_device_memory {
	struct iwl_fw_ini_error_dump_header header;
	__le16 type;
	__le16 version;
	u8 data[];
} __packed;

/**
 * struct iwl_fw_error_dump_paging - content of the UMAC's image page
 *	block on DRAM
 * @index: the index of the page block
 * @reserved:
 * @data: the content of the page block
 */
struct iwl_fw_error_dump_paging {
	__le32 index;
	__le32 reserved;
	u8 data[];
};

/**
 * iwl_fw_error_next_data - advance fw error dump data pointer
 * @data: previous data block
 * Returns: next data block
 */
static inline struct iwl_fw_error_dump_data *
iwl_fw_error_next_data(struct iwl_fw_error_dump_data *data)
{
	return (void *)(data->data + le32_to_cpu(data->len));
}

/**
 * enum iwl_fw_dbg_trigger - triggers available
 *
 * @FW_DBG_TRIGGER_USER: trigger log collection by user
 *	This should not be defined as a trigger to the driver, but a value the
 *	driver should set to indicate that the trigger was initiated by the
 *	user.
 * @FW_DBG_TRIGGER_FW_ASSERT: trigger log collection when the firmware asserts
 * @FW_DBG_TRIGGER_MISSED_BEACONS: trigger log collection when beacons are
 *	missed.
 * @FW_DBG_TRIGGER_CHANNEL_SWITCH: trigger log collection upon channel switch.
 * @FW_DBG_TRIGGER_FW_NOTIF: trigger log collection when the firmware sends a
 *	command response or a notification.
 * @FW_DBG_TRIGGER_MLME: trigger log collection upon MLME event.
 * @FW_DBG_TRIGGER_STATS: trigger log collection upon statistics threshold.
 * @FW_DBG_TRIGGER_RSSI: trigger log collection when the rssi of the beacon
 *	goes below a threshold.
 * @FW_DBG_TRIGGER_TXQ_TIMERS: configures the timers for the Tx queue hang
 *	detection.
 * @FW_DBG_TRIGGER_TIME_EVENT: trigger log collection upon time events related
 *	events.
 * @FW_DBG_TRIGGER_BA: trigger log collection upon BlockAck related events.
 * @FW_DBG_TX_LATENCY: trigger log collection when the tx latency goes above a
 *	threshold.
 * @FW_DBG_TDLS: trigger log collection upon TDLS related events.
 * @FW_DBG_TRIGGER_TX_STATUS: trigger log collection upon tx status when
 *  the firmware sends a tx reply.
 * @FW_DBG_TRIGGER_ALIVE_TIMEOUT: trigger log collection if alive flow timeouts
 * @FW_DBG_TRIGGER_DRIVER: trigger log collection upon a flow failure
 *	in the driver.
 */
enum iwl_fw_dbg_trigger {
	FW_DBG_TRIGGER_INVALID = 0,
	FW_DBG_TRIGGER_USER,
	FW_DBG_TRIGGER_FW_ASSERT,
	FW_DBG_TRIGGER_MISSED_BEACONS,
	FW_DBG_TRIGGER_CHANNEL_SWITCH,
	FW_DBG_TRIGGER_FW_NOTIF,
	FW_DBG_TRIGGER_MLME,
	FW_DBG_TRIGGER_STATS,
	FW_DBG_TRIGGER_RSSI,
	FW_DBG_TRIGGER_TXQ_TIMERS,
	FW_DBG_TRIGGER_TIME_EVENT,
	FW_DBG_TRIGGER_BA,
	FW_DBG_TRIGGER_TX_LATENCY,
	FW_DBG_TRIGGER_TDLS,
	FW_DBG_TRIGGER_TX_STATUS,
	FW_DBG_TRIGGER_ALIVE_TIMEOUT,
	FW_DBG_TRIGGER_DRIVER,

	/* must be last */
	FW_DBG_TRIGGER_MAX,
};

/**
 * struct iwl_fw_error_dump_trigger_desc - describes the trigger condition
 * @type: &enum iwl_fw_dbg_trigger
 * @data: raw data about what happened
 */
struct iwl_fw_error_dump_trigger_desc {
	__le32 type;
	u8 data[];
};

#endif /* __fw_error_dump_h__ */
