/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2005-2014, 2018-2025 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#ifndef __iwl_trans_h__
#define __iwl_trans_h__

#include <linux/ieee80211.h>
#include <linux/mm.h> /* for page_address */
#include <linux/lockdep.h>
#include <linux/kernel.h>

#include "iwl-debug.h"
#include "iwl-config.h"
#include "fw/img.h"
#include "iwl-op-mode.h"
#include <linux/firmware.h>
#include "fw/api/cmdhdr.h"
#include "fw/api/txq.h"
#include "fw/api/dbg-tlv.h"
#include "iwl-dbg-tlv.h"

/**
 * DOC: Transport layer - what is it ?
 *
 * The transport layer is the layer that deals with the HW directly. It provides
 * the PCIe access to the underlying hardwarwe. The transport layer doesn't
 * provide any policy, algorithm or anything of this kind, but only mechanisms
 * to make the HW do something. It is not completely stateless but close to it.
 */

/**
 * DOC: Life cycle of the transport layer
 *
 * The transport layer has a very precise life cycle.
 *
 *	1) A helper function is called during the module initialization and
 *	   registers the bus driver's ops with the transport's alloc function.
 *	2) Bus's probe calls to the transport layer's allocation functions.
 *	   Of course this function is bus specific.
 *	3) This allocation functions will spawn the upper layer which will
 *	   register mac80211.
 *
 *	4) At some point (i.e. mac80211's start call), the op_mode will call
 *	   the following sequence:
 *	   start_hw
 *	   start_fw
 *
 *	5) Then when finished (or reset):
 *	   stop_device
 *
 *	6) Eventually, the free function will be called.
 */

/* default preset 0 (start from bit 16)*/
#define IWL_FW_DBG_DOMAIN_POS	16
#define IWL_FW_DBG_DOMAIN	BIT(IWL_FW_DBG_DOMAIN_POS)

#define IWL_TRANS_FW_DBG_DOMAIN(trans)	IWL_FW_INI_DOMAIN_ALWAYS_ON

#define FH_RSCSR_FRAME_SIZE_MSK		0x00003FFF	/* bits 0-13 */
#define FH_RSCSR_FRAME_INVALID		0x55550000
#define FH_RSCSR_FRAME_ALIGN		0x40
#define FH_RSCSR_RPA_EN			BIT(25)
#define FH_RSCSR_RADA_EN		BIT(26)
#define FH_RSCSR_RXQ_POS		16
#define FH_RSCSR_RXQ_MASK		0x3F0000

struct iwl_rx_packet {
	/*
	 * The first 4 bytes of the RX frame header contain both the RX frame
	 * size and some flags.
	 * Bit fields:
	 * 31:    flag flush RB request
	 * 30:    flag ignore TC (terminal counter) request
	 * 29:    flag fast IRQ request
	 * 28-27: Reserved
	 * 26:    RADA enabled
	 * 25:    Offload enabled
	 * 24:    RPF enabled
	 * 23:    RSS enabled
	 * 22:    Checksum enabled
	 * 21-16: RX queue
	 * 15-14: Reserved
	 * 13-00: RX frame size
	 */
	__le32 len_n_flags;
	struct iwl_cmd_header hdr;
	u8 data[];
} __packed;

static inline u32 iwl_rx_packet_len(const struct iwl_rx_packet *pkt)
{
	return le32_to_cpu(pkt->len_n_flags) & FH_RSCSR_FRAME_SIZE_MSK;
}

static inline u32 iwl_rx_packet_payload_len(const struct iwl_rx_packet *pkt)
{
	return iwl_rx_packet_len(pkt) - sizeof(pkt->hdr);
}

/**
 * enum CMD_MODE - how to send the host commands ?
 *
 * @CMD_ASYNC: Return right away and don't wait for the response
 * @CMD_WANT_SKB: Not valid with CMD_ASYNC. The caller needs the buffer of
 *	the response. The caller needs to call iwl_free_resp when done.
 * @CMD_SEND_IN_RFKILL: Send the command even if the NIC is in RF-kill.
 * @CMD_BLOCK_TXQS: Block TXQs while the comment is executing.
 */
enum CMD_MODE {
	CMD_ASYNC		= BIT(0),
	CMD_WANT_SKB		= BIT(1),
	CMD_SEND_IN_RFKILL	= BIT(2),
	CMD_BLOCK_TXQS		= BIT(3),
};
#define CMD_MODE_BITS 5

#define DEF_CMD_PAYLOAD_SIZE 320

/**
 * struct iwl_device_cmd
 *
 * For allocation of the command and tx queues, this establishes the overall
 * size of the largest command we send to uCode, except for commands that
 * aren't fully copied and use other TFD space.
 *
 * @hdr: command header
 * @payload: payload for the command
 * @hdr_wide: wide command header
 * @payload_wide: payload for the wide command
 */
struct iwl_device_cmd {
	union {
		struct {
			struct iwl_cmd_header hdr;	/* uCode API */
			u8 payload[DEF_CMD_PAYLOAD_SIZE];
		};
		struct {
			struct iwl_cmd_header_wide hdr_wide;
			u8 payload_wide[DEF_CMD_PAYLOAD_SIZE -
					sizeof(struct iwl_cmd_header_wide) +
					sizeof(struct iwl_cmd_header)];
		};
	};
} __packed;

/**
 * struct iwl_device_tx_cmd - buffer for TX command
 * @hdr: the header
 * @payload: the payload placeholder
 *
 * The actual structure is sized dynamically according to need.
 */
struct iwl_device_tx_cmd {
	struct iwl_cmd_header hdr;
	u8 payload[];
} __packed;

#define TFD_MAX_PAYLOAD_SIZE (sizeof(struct iwl_device_cmd))

/*
 * number of transfer buffers (fragments) per transmit frame descriptor;
 * this is just the driver's idea, the hardware supports 20
 */
#define IWL_MAX_CMD_TBS_PER_TFD	2

/**
 * enum iwl_hcmd_dataflag - flag for each one of the chunks of the command
 *
 * @IWL_HCMD_DFL_NOCOPY: By default, the command is copied to the host command's
 *	ring. The transport layer doesn't map the command's buffer to DMA, but
 *	rather copies it to a previously allocated DMA buffer. This flag tells
 *	the transport layer not to copy the command, but to map the existing
 *	buffer (that is passed in) instead. This saves the memcpy and allows
 *	commands that are bigger than the fixed buffer to be submitted.
 *	Note that a TFD entry after a NOCOPY one cannot be a normal copied one.
 * @IWL_HCMD_DFL_DUP: Only valid without NOCOPY, duplicate the memory for this
 *	chunk internally and free it again after the command completes. This
 *	can (currently) be used only once per command.
 *	Note that a TFD entry after a DUP one cannot be a normal copied one.
 */
enum iwl_hcmd_dataflag {
	IWL_HCMD_DFL_NOCOPY	= BIT(0),
	IWL_HCMD_DFL_DUP	= BIT(1),
};

enum iwl_error_event_table_status {
	IWL_ERROR_EVENT_TABLE_LMAC1 = BIT(0),
	IWL_ERROR_EVENT_TABLE_LMAC2 = BIT(1),
	IWL_ERROR_EVENT_TABLE_UMAC = BIT(2),
	IWL_ERROR_EVENT_TABLE_TCM1 = BIT(3),
	IWL_ERROR_EVENT_TABLE_TCM2 = BIT(4),
	IWL_ERROR_EVENT_TABLE_RCM1 = BIT(5),
	IWL_ERROR_EVENT_TABLE_RCM2 = BIT(6),
};

/**
 * struct iwl_host_cmd - Host command to the uCode
 *
 * @data: array of chunks that composes the data of the host command
 * @resp_pkt: response packet, if %CMD_WANT_SKB was set
 * @_rx_page_order: (internally used to free response packet)
 * @_rx_page_addr: (internally used to free response packet)
 * @flags: can be CMD_*
 * @len: array of the lengths of the chunks in data
 * @dataflags: IWL_HCMD_DFL_*
 * @id: command id of the host command, for wide commands encoding the
 *	version and group as well
 */
struct iwl_host_cmd {
	const void *data[IWL_MAX_CMD_TBS_PER_TFD];
	struct iwl_rx_packet *resp_pkt;
	unsigned long _rx_page_addr;
	u32 _rx_page_order;

	u32 flags;
	u32 id;
	u16 len[IWL_MAX_CMD_TBS_PER_TFD];
	u8 dataflags[IWL_MAX_CMD_TBS_PER_TFD];
};

static inline void iwl_free_resp(struct iwl_host_cmd *cmd)
{
	free_pages(cmd->_rx_page_addr, cmd->_rx_page_order);
}

struct iwl_rx_cmd_buffer {
	struct page *_page;
	int _offset;
	bool _page_stolen;
	u32 _rx_page_order;
	unsigned int truesize;
};

static inline void *rxb_addr(struct iwl_rx_cmd_buffer *r)
{
	return (void *)((unsigned long)page_address(r->_page) + r->_offset);
}

static inline int rxb_offset(struct iwl_rx_cmd_buffer *r)
{
	return r->_offset;
}

static inline struct page *rxb_steal_page(struct iwl_rx_cmd_buffer *r)
{
	r->_page_stolen = true;
	get_page(r->_page);
	return r->_page;
}

static inline void iwl_free_rxb(struct iwl_rx_cmd_buffer *r)
{
	__free_pages(r->_page, r->_rx_page_order);
}

#define MAX_NO_RECLAIM_CMDS	6

#define IWL_MASK(lo, hi) ((1 << (hi)) | ((1 << (hi)) - (1 << (lo))))

/*
 * Maximum number of HW queues the transport layer
 * currently supports
 */
#define IWL_MAX_HW_QUEUES		32
#define IWL_MAX_TVQM_QUEUES		512

#define IWL_MAX_TID_COUNT	8
#define IWL_MGMT_TID		15
#define IWL_FRAME_LIMIT	64
#define IWL_MAX_RX_HW_QUEUES	16
#define IWL_9000_MAX_RX_HW_QUEUES	1

/**
 * enum iwl_trans_status: transport status flags
 * @STATUS_SYNC_HCMD_ACTIVE: a SYNC command is being processed
 * @STATUS_DEVICE_ENABLED: APM is enabled
 * @STATUS_TPOWER_PMI: the device might be asleep (need to wake it up)
 * @STATUS_INT_ENABLED: interrupts are enabled
 * @STATUS_RFKILL_HW: the actual HW state of the RF-kill switch
 * @STATUS_RFKILL_OPMODE: RF-kill state reported to opmode
 * @STATUS_FW_ERROR: the fw is in error state
 * @STATUS_TRANS_DEAD: trans is dead - avoid any read/write operation
 * @STATUS_IN_SW_RESET: device is undergoing reset, cleared by opmode
 *	via iwl_trans_finish_sw_reset()
 * @STATUS_RESET_PENDING: reset worker was scheduled, but didn't dump
 *	the firmware state yet
 * @STATUS_TRANS_RESET_IN_PROGRESS: reset is still in progress, don't
 *	attempt another reset yet
 */
enum iwl_trans_status {
	STATUS_SYNC_HCMD_ACTIVE,
	STATUS_DEVICE_ENABLED,
	STATUS_TPOWER_PMI,
	STATUS_INT_ENABLED,
	STATUS_RFKILL_HW,
	STATUS_RFKILL_OPMODE,
	STATUS_FW_ERROR,
	STATUS_TRANS_DEAD,
	STATUS_IN_SW_RESET,
	STATUS_RESET_PENDING,
	STATUS_TRANS_RESET_IN_PROGRESS,
};

static inline int
iwl_trans_get_rb_size_order(enum iwl_amsdu_size rb_size)
{
	switch (rb_size) {
	case IWL_AMSDU_2K:
		return get_order(2 * 1024);
	case IWL_AMSDU_4K:
		return get_order(4 * 1024);
	case IWL_AMSDU_8K:
		return get_order(8 * 1024);
	case IWL_AMSDU_12K:
		return get_order(16 * 1024);
	default:
		WARN_ON(1);
		return -1;
	}
}

static inline int
iwl_trans_get_rb_size(enum iwl_amsdu_size rb_size)
{
	switch (rb_size) {
	case IWL_AMSDU_2K:
		return 2 * 1024;
	case IWL_AMSDU_4K:
		return 4 * 1024;
	case IWL_AMSDU_8K:
		return 8 * 1024;
	case IWL_AMSDU_12K:
		return 16 * 1024;
	default:
		WARN_ON(1);
		return 0;
	}
}

struct iwl_hcmd_names {
	u8 cmd_id;
	const char *const cmd_name;
};

#define HCMD_NAME(x)	\
	{ .cmd_id = x, .cmd_name = #x }

struct iwl_hcmd_arr {
	const struct iwl_hcmd_names *arr;
	int size;
};

#define HCMD_ARR(x)	\
	{ .arr = x, .size = ARRAY_SIZE(x) }

/**
 * struct iwl_dump_sanitize_ops - dump sanitization operations
 * @frob_txf: Scrub the TX FIFO data
 * @frob_hcmd: Scrub a host command, the %hcmd pointer is to the header
 *	but that might be short or long (&struct iwl_cmd_header or
 *	&struct iwl_cmd_header_wide)
 * @frob_mem: Scrub memory data
 */
struct iwl_dump_sanitize_ops {
	void (*frob_txf)(void *ctx, void *buf, size_t buflen);
	void (*frob_hcmd)(void *ctx, void *hcmd, size_t buflen);
	void (*frob_mem)(void *ctx, u32 mem_addr, void *mem, size_t buflen);
};

/**
 * struct iwl_trans_config - transport configuration
 *
 * These values should be set before iwl_trans_op_mode_enter().
 *
 * @cmd_queue: the index of the command queue.
 *	Must be set before start_fw.
 * @cmd_fifo: the fifo for host commands
 * @no_reclaim_cmds: Some devices erroneously don't set the
 *	SEQ_RX_FRAME bit on some notifications, this is the
 *	list of such notifications to filter. Max length is
 *	%MAX_NO_RECLAIM_CMDS.
 * @n_no_reclaim_cmds: # of commands in list
 * @rx_buf_size: RX buffer size needed for A-MSDUs
 *	if unset 4k will be the RX buffer size
 * @scd_set_active: should the transport configure the SCD for HCMD queue
 * @command_groups: array of command groups, each member is an array of the
 *	commands in the group; for debugging only
 * @command_groups_size: number of command groups, to avoid illegal access
 * @cb_data_offs: offset inside skb->cb to store transport data at, must have
 *	space for at least two pointers
 * @fw_reset_handshake: firmware supports reset flow handshake
 * @queue_alloc_cmd_ver: queue allocation command version, set to 0
 *	for using the older SCD_QUEUE_CFG, set to the version of
 *	SCD_QUEUE_CONFIG_CMD otherwise.
 * @wide_cmd_header: true when ucode supports wide command header format
 * @rx_mpdu_cmd: MPDU RX command ID, must be assigned by opmode before
 *	starting the firmware, used for tracing
 * @rx_mpdu_cmd_hdr_size: used for tracing, amount of data before the
 *	start of the 802.11 header in the @rx_mpdu_cmd
 * @dsbr_urm_fw_dependent: switch to URM based on fw settings
 * @dsbr_urm_permanent: switch to URM permanently
 * @mbx_addr_0_step: step address data 0
 * @mbx_addr_1_step: step address data 1
 * @ext_32khz_clock_valid: if true, the external 32 KHz clock can be used
 */
struct iwl_trans_config {
	u8 cmd_queue;
	u8 cmd_fifo;
	u8 n_no_reclaim_cmds;
	u8 no_reclaim_cmds[MAX_NO_RECLAIM_CMDS];

	enum iwl_amsdu_size rx_buf_size;
	bool scd_set_active;
	const struct iwl_hcmd_arr *command_groups;
	int command_groups_size;

	u8 cb_data_offs;
	bool fw_reset_handshake;
	u8 queue_alloc_cmd_ver;

	bool wide_cmd_header;
	u8 rx_mpdu_cmd, rx_mpdu_cmd_hdr_size;

	u8 dsbr_urm_fw_dependent:1,
	   dsbr_urm_permanent:1,
	   ext_32khz_clock_valid:1;

	u32 mbx_addr_0_step;
	u32 mbx_addr_1_step;
};

struct iwl_trans_dump_data {
	u32 len;
	u8 data[];
};

struct iwl_trans;

struct iwl_trans_txq_scd_cfg {
	u8 fifo;
	u8 sta_id;
	u8 tid;
	bool aggregate;
	int frame_limit;
};

/**
 * struct iwl_trans_rxq_dma_data - RX queue DMA data
 * @fr_bd_cb: DMA address of free BD cyclic buffer
 * @fr_bd_wid: Initial write index of the free BD cyclic buffer
 * @urbd_stts_wrptr: DMA address of urbd_stts_wrptr
 * @ur_bd_cb: DMA address of used BD cyclic buffer
 */
struct iwl_trans_rxq_dma_data {
	u64 fr_bd_cb;
	u32 fr_bd_wid;
	u64 urbd_stts_wrptr;
	u64 ur_bd_cb;
};

/* maximal number of DRAM MAP entries supported by FW */
#define IPC_DRAM_MAP_ENTRY_NUM_MAX 64

/**
 * struct iwl_pnvm_image - contains info about the parsed pnvm image
 * @chunks: array of pointers to pnvm payloads and their sizes
 * @n_chunks: the number of the pnvm payloads.
 * @version: the version of the loaded PNVM image
 */
struct iwl_pnvm_image {
	struct {
		const void *data;
		u32 len;
	} chunks[IPC_DRAM_MAP_ENTRY_NUM_MAX];
	u32 n_chunks;
	u32 version;
};

/**
 * enum iwl_trans_state - state of the transport layer
 *
 * @IWL_TRANS_NO_FW: firmware wasn't started yet, or crashed
 * @IWL_TRANS_FW_STARTED: FW was started, but not alive yet
 * @IWL_TRANS_FW_ALIVE: FW has sent an alive response
 */
enum iwl_trans_state {
	IWL_TRANS_NO_FW,
	IWL_TRANS_FW_STARTED,
	IWL_TRANS_FW_ALIVE,
};

/**
 * DOC: Platform power management
 *
 * In system-wide power management the entire platform goes into a low
 * power state (e.g. idle or suspend to RAM) at the same time and the
 * device is configured as a wakeup source for the entire platform.
 * This is usually triggered by userspace activity (e.g. the user
 * presses the suspend button or a power management daemon decides to
 * put the platform in low power mode).  The device's behavior in this
 * mode is dictated by the wake-on-WLAN configuration.
 *
 * The terms used for the device's behavior are as follows:
 *
 *	- D0: the device is fully powered and the host is awake;
 *	- D3: the device is in low power mode and only reacts to
 *		specific events (e.g. magic-packet received or scan
 *		results found);
 *
 * These terms reflect the power modes in the firmware and are not to
 * be confused with the physical device power state.
 */

/**
 * enum iwl_ini_cfg_state
 * @IWL_INI_CFG_STATE_NOT_LOADED: no debug cfg was given
 * @IWL_INI_CFG_STATE_LOADED: debug cfg was found and loaded
 * @IWL_INI_CFG_STATE_CORRUPTED: debug cfg was found and some of the TLVs
 *	are corrupted. The rest of the debug TLVs will still be used
 */
enum iwl_ini_cfg_state {
	IWL_INI_CFG_STATE_NOT_LOADED,
	IWL_INI_CFG_STATE_LOADED,
	IWL_INI_CFG_STATE_CORRUPTED,
};

/* Max time to wait for nmi interrupt */
#define IWL_TRANS_NMI_TIMEOUT (HZ / 4)

/**
 * struct iwl_dram_data
 * @physical: page phy pointer
 * @block: pointer to the allocated block/page
 * @size: size of the block/page
 */
struct iwl_dram_data {
	dma_addr_t physical;
	void *block;
	int size;
};

/**
 * struct iwl_dram_regions - DRAM regions container structure
 * @drams: array of several DRAM areas that contains the pnvm and power
 *	reduction table payloads.
 * @n_regions: number of DRAM regions that were allocated
 * @prph_scratch_mem_desc: points to a structure allocated in dram,
 *	designed to show FW where all the payloads are.
 */
struct iwl_dram_regions {
	struct iwl_dram_data drams[IPC_DRAM_MAP_ENTRY_NUM_MAX];
	struct iwl_dram_data prph_scratch_mem_desc;
	u8 n_regions;
};

/**
 * struct iwl_fw_mon - fw monitor per allocation id
 * @num_frags: number of fragments
 * @frags: an array of DRAM buffer fragments
 */
struct iwl_fw_mon {
	u32 num_frags;
	struct iwl_dram_data *frags;
};

/**
 * struct iwl_self_init_dram - dram data used by self init process
 * @fw: lmac and umac dram data
 * @fw_cnt: total number of items in array
 * @paging: paging dram data
 * @paging_cnt: total number of items in array
 */
struct iwl_self_init_dram {
	struct iwl_dram_data *fw;
	int fw_cnt;
	struct iwl_dram_data *paging;
	int paging_cnt;
};

/**
 * struct iwl_imr_data - imr dram data used during debug process
 * @imr_enable: imr enable status received from fw
 * @imr_size: imr dram size received from fw
 * @sram_addr: sram address from debug tlv
 * @sram_size: sram size from debug tlv
 * @imr2sram_remainbyte: size remained after each dma transfer
 * @imr_curr_addr: current dst address used during dma transfer
 * @imr_base_addr: imr address received from fw
 */
struct iwl_imr_data {
	u32 imr_enable;
	u32 imr_size;
	u32 sram_addr;
	u32 sram_size;
	u32 imr2sram_remainbyte;
	u64 imr_curr_addr;
	__le64 imr_base_addr;
};

#define IWL_TRANS_CURRENT_PC_NAME_MAX_BYTES      32

/**
 * struct iwl_pc_data - program counter details
 * @pc_name: cpu name
 * @pc_address: cpu program counter
 */
struct iwl_pc_data {
	u8  pc_name[IWL_TRANS_CURRENT_PC_NAME_MAX_BYTES];
	u32 pc_address;
};

/**
 * struct iwl_trans_debug - transport debug related data
 *
 * @n_dest_reg: num of reg_ops in %dbg_dest_tlv
 * @rec_on: true iff there is a fw debug recording currently active
 * @dest_tlv: points to the destination TLV for debug
 * @lmac_error_event_table: addrs of lmacs error tables
 * @umac_error_event_table: addr of umac error table
 * @tcm_error_event_table: address(es) of TCM error table(s)
 * @rcm_error_event_table: address(es) of RCM error table(s)
 * @error_event_table_tlv_status: bitmap that indicates what error table
 *	pointers was recevied via TLV. uses enum &iwl_error_event_table_status
 * @internal_ini_cfg: internal debug cfg state. Uses &enum iwl_ini_cfg_state
 * @external_ini_cfg: external debug cfg state. Uses &enum iwl_ini_cfg_state
 * @fw_mon_cfg: debug buffer allocation configuration
 * @fw_mon_ini: DRAM buffer fragments per allocation id
 * @fw_mon: DRAM buffer for firmware monitor
 * @hw_error: equals true if hw error interrupt was received from the FW
 * @ini_dest: debug monitor destination uses &enum iwl_fw_ini_buffer_location
 * @unsupported_region_msk: unsupported regions out of active_regions
 * @active_regions: active regions
 * @debug_info_tlv_list: list of debug info TLVs
 * @time_point: array of debug time points
 * @periodic_trig_list: periodic triggers list
 * @domains_bitmap: bitmap of active domains other than &IWL_FW_INI_DOMAIN_ALWAYS_ON
 * @ucode_preset: preset based on ucode
 * @restart_required: indicates debug restart is required
 * @last_tp_resetfw: last handling of reset during debug timepoint
 * @imr_data: IMR debug data allocation
 * @num_pc: number of program counter for cpu
 * @pc_data: details of the program counter
 * @yoyo_bin_loaded: tells if a yoyo debug file has been loaded
 */
struct iwl_trans_debug {
	u8 n_dest_reg;
	bool rec_on;

	const struct iwl_fw_dbg_dest_tlv_v1 *dest_tlv;

	u32 lmac_error_event_table[2];
	u32 umac_error_event_table;
	u32 tcm_error_event_table[2];
	u32 rcm_error_event_table[2];
	unsigned int error_event_table_tlv_status;

	enum iwl_ini_cfg_state internal_ini_cfg;
	enum iwl_ini_cfg_state external_ini_cfg;

	struct iwl_fw_ini_allocation_tlv fw_mon_cfg[IWL_FW_INI_ALLOCATION_NUM];
	struct iwl_fw_mon fw_mon_ini[IWL_FW_INI_ALLOCATION_NUM];

	struct iwl_dram_data fw_mon;

	bool hw_error;
	enum iwl_fw_ini_buffer_location ini_dest;

	u64 unsupported_region_msk;
	struct iwl_ucode_tlv *active_regions[IWL_FW_INI_MAX_REGION_ID];
	struct list_head debug_info_tlv_list;
	struct iwl_dbg_tlv_time_point_data time_point[IWL_FW_INI_TIME_POINT_NUM];
	struct list_head periodic_trig_list;

	u32 domains_bitmap;
	u32 ucode_preset;
	bool restart_required;
	u32 last_tp_resetfw;
	struct iwl_imr_data imr_data;
	u32 num_pc;
	struct iwl_pc_data *pc_data;
	bool yoyo_bin_loaded;
};

struct iwl_dma_ptr {
	dma_addr_t dma;
	void *addr;
	size_t size;
};

struct iwl_cmd_meta {
	/* only for SYNC commands, iff the reply skb is wanted */
	struct iwl_host_cmd *source;
	u32 flags: CMD_MODE_BITS;
	/* sg_offset is valid if it is non-zero */
	u32 sg_offset: PAGE_SHIFT;
	u32 tbs;
};

/*
 * The FH will write back to the first TB only, so we need to copy some data
 * into the buffer regardless of whether it should be mapped or not.
 * This indicates how big the first TB must be to include the scratch buffer
 * and the assigned PN.
 * Since PN location is 8 bytes at offset 12, it's 20 now.
 * If we make it bigger then allocations will be bigger and copy slower, so
 * that's probably not useful.
 */
#define IWL_FIRST_TB_SIZE	20
#define IWL_FIRST_TB_SIZE_ALIGN ALIGN(IWL_FIRST_TB_SIZE, 64)

struct iwl_pcie_txq_entry {
	void *cmd;
	struct sk_buff *skb;
	/* buffer to free after command completes */
	const void *free_buf;
	struct iwl_cmd_meta meta;
};

struct iwl_pcie_first_tb_buf {
	u8 buf[IWL_FIRST_TB_SIZE_ALIGN];
};

/**
 * struct iwl_txq - Tx Queue for DMA
 * @tfds: transmit frame descriptors (DMA memory)
 * @first_tb_bufs: start of command headers, including scratch buffers, for
 *	the writeback -- this is DMA memory and an array holding one buffer
 *	for each command on the queue
 * @first_tb_dma: DMA address for the first_tb_bufs start
 * @entries: transmit entries (driver state)
 * @lock: queue lock
 * @reclaim_lock: reclaim lock
 * @stuck_timer: timer that fires if queue gets stuck
 * @trans: pointer back to transport (for timer)
 * @need_update: indicates need to update read/write index
 * @ampdu: true if this queue is an ampdu queue for an specific RA/TID
 * @wd_timeout: queue watchdog timeout (jiffies) - per queue
 * @frozen: tx stuck queue timer is frozen
 * @frozen_expiry_remainder: remember how long until the timer fires
 * @block: queue is blocked
 * @bc_tbl: byte count table of the queue (relevant only for gen2 transport)
 * @write_ptr: 1-st empty entry (index) host_w
 * @read_ptr: last used entry (index) host_r
 * @dma_addr:  physical addr for BD's
 * @n_window: safe queue window
 * @id: queue id
 * @low_mark: low watermark, resume queue if free space more than this
 * @high_mark: high watermark, stop queue if free space less than this
 * @overflow_q: overflow queue for handling frames that didn't fit on HW queue
 * @overflow_tx: need to transmit from overflow
 *
 * A Tx queue consists of circular buffer of BDs (a.k.a. TFDs, transmit frame
 * descriptors) and required locking structures.
 *
 * Note the difference between TFD_QUEUE_SIZE_MAX and n_window: the hardware
 * always assumes 256 descriptors, so TFD_QUEUE_SIZE_MAX is always 256 (unless
 * there might be HW changes in the future). For the normal TX
 * queues, n_window, which is the size of the software queue data
 * is also 256; however, for the command queue, n_window is only
 * 32 since we don't need so many commands pending. Since the HW
 * still uses 256 BDs for DMA though, TFD_QUEUE_SIZE_MAX stays 256.
 * This means that we end up with the following:
 *  HW entries: | 0 | ... | N * 32 | ... | N * 32 + 31 | ... | 255 |
 *  SW entries:           | 0      | ... | 31          |
 * where N is a number between 0 and 7. This means that the SW
 * data is a window overlayed over the HW queue.
 */
struct iwl_txq {
	void *tfds;
	struct iwl_pcie_first_tb_buf *first_tb_bufs;
	dma_addr_t first_tb_dma;
	struct iwl_pcie_txq_entry *entries;
	/* lock for syncing changes on the queue */
	spinlock_t lock;
	/* lock to prevent concurrent reclaim */
	spinlock_t reclaim_lock;
	unsigned long frozen_expiry_remainder;
	struct timer_list stuck_timer;
	struct iwl_trans *trans;
	bool need_update;
	bool frozen;
	bool ampdu;
	int block;
	unsigned long wd_timeout;
	struct sk_buff_head overflow_q;
	struct iwl_dma_ptr bc_tbl;

	int write_ptr;
	int read_ptr;
	dma_addr_t dma_addr;
	int n_window;
	u32 id;
	int low_mark;
	int high_mark;

	bool overflow_tx;
};

/**
 * struct iwl_trans_info - transport info for outside use
 * @name: the device name
 * @max_skb_frags: maximum number of fragments an SKB can have when transmitted.
 *	0 indicates that frag SKBs (NETIF_F_SG) aren't supported.
 * @hw_rev: the revision data of the HW
 * @hw_rev_step: The mac step of the HW
 * @hw_rf_id: the device RF ID
 * @hw_cnv_id: the device CNV ID
 * @hw_crf_id: the device CRF ID
 * @hw_id: the ID of the device / sub-device
 *	Bits 0:15 represent the sub-device ID
 *	Bits 16:31 represent the device ID.
 * @pcie_link_speed: current PCIe link speed (%PCI_EXP_LNKSTA_CLS_*),
 *	only valid for discrete (not integrated) NICs
 * @num_rxqs: number of RX queues allocated by the transport
 */
struct iwl_trans_info {
	const char *name;
	u32 max_skb_frags;
	u32 hw_rev;
	u32 hw_rev_step;
	u32 hw_rf_id;
	u32 hw_crf_id;
	u32 hw_cnv_id;
	u32 hw_id;
	u8 pcie_link_speed;
	u8 num_rxqs;
};

/**
 * struct iwl_trans - transport common data
 *
 * @csme_own: true if we couldn't get ownership on the device
 * @op_mode: pointer to the op_mode
 * @mac_cfg: the trans-specific configuration part
 * @cfg: pointer to the configuration
 * @drv: pointer to iwl_drv
 * @conf: configuration set by the opmode before enter
 * @state: current device state
 * @status: a bit-mask of transport status flags
 * @dev: pointer to struct device * that represents the device
 * @info: device information for use by other layers
 * @pnvm_loaded: indicates PNVM was loaded
 * @suppress_cmd_error_once: suppress "FW error in SYNC CMD" once,
 *	e.g. for testing
 * @fail_to_parse_pnvm_image: set to true if pnvm parsing failed
 * @reduce_power_loaded: indicates reduced power section was loaded
 * @failed_to_load_reduce_power_image: set to true if pnvm loading failed
 * @dbgfs_dir: iwlwifi debugfs base dir for this device
 * @sync_cmd_lockdep_map: lockdep map for checking sync commands
 * @dbg: additional debug data, see &struct iwl_trans_debug
 * @init_dram: FW initialization DMA data
 * @reduced_cap_sku: reduced capability supported SKU
 * @step_urm: STEP is in URM, no support for MCS>9 in 320 MHz
 * @restart: restart worker data
 * @restart.wk: restart worker
 * @restart.mode: reset/restart error mode information
 * @restart.during_reset: error occurred during previous software reset
 * @trans_specific: data for the specific transport this is allocated for/with
 * @request_top_reset: TOP reset was requested, used by the reset
 *	worker that should be scheduled (with appropriate reason)
 * @do_top_reset: indication to the (PCIe) transport/context-info
 *	to do the TOP reset
 */
struct iwl_trans {
	bool csme_own;
	struct iwl_op_mode *op_mode;
	const struct iwl_mac_cfg *mac_cfg;
	const struct iwl_rf_cfg *cfg;
	struct iwl_drv *drv;
	struct iwl_trans_config conf;
	enum iwl_trans_state state;
	unsigned long status;

	struct device *dev;

	const struct iwl_trans_info info;
	bool reduced_cap_sku;
	bool step_urm;
	bool suppress_cmd_error_once;

	u8 pnvm_loaded:1;
	u8 fail_to_parse_pnvm_image:1;
	u8 reduce_power_loaded:1;
	u8 failed_to_load_reduce_power_image:1;

	struct dentry *dbgfs_dir;

#ifdef CONFIG_LOCKDEP
	struct lockdep_map sync_cmd_lockdep_map;
#endif

	struct iwl_trans_debug dbg;
	struct iwl_self_init_dram init_dram;

	struct {
		struct delayed_work wk;
		struct iwl_fw_error_dump_mode mode;
		bool during_reset;
	} restart;

	u8 request_top_reset:1,
	   do_top_reset:1;

	/* pointer to trans specific struct */
	/*Ensure that this pointer will always be aligned to sizeof pointer */
	char trans_specific[] __aligned(sizeof(void *));
};

const char *iwl_get_cmd_string(struct iwl_trans *trans, u32 id);

void iwl_trans_op_mode_enter(struct iwl_trans *trans,
			     struct iwl_op_mode *op_mode);

int iwl_trans_start_hw(struct iwl_trans *trans);

void iwl_trans_op_mode_leave(struct iwl_trans *trans);

void iwl_trans_fw_alive(struct iwl_trans *trans);

int iwl_trans_start_fw(struct iwl_trans *trans, const struct iwl_fw *fw,
		       enum iwl_ucode_type ucode_type, bool run_in_rfkill);

void iwl_trans_stop_device(struct iwl_trans *trans);

int iwl_trans_d3_suspend(struct iwl_trans *trans, bool reset);

int iwl_trans_d3_resume(struct iwl_trans *trans, bool reset);

struct iwl_trans_dump_data *
iwl_trans_dump_data(struct iwl_trans *trans, u32 dump_mask,
		    const struct iwl_dump_sanitize_ops *sanitize_ops,
		    void *sanitize_ctx);

struct iwl_device_tx_cmd *iwl_trans_alloc_tx_cmd(struct iwl_trans *trans);

int iwl_trans_send_cmd(struct iwl_trans *trans, struct iwl_host_cmd *cmd);

void iwl_trans_free_tx_cmd(struct iwl_trans *trans,
			   struct iwl_device_tx_cmd *dev_cmd);

int iwl_trans_tx(struct iwl_trans *trans, struct sk_buff *skb,
		 struct iwl_device_tx_cmd *dev_cmd, int queue);

void iwl_trans_reclaim(struct iwl_trans *trans, int queue, int ssn,
		       struct sk_buff_head *skbs, bool is_flush);

void iwl_trans_set_q_ptrs(struct iwl_trans *trans, int queue, int ptr);

void iwl_trans_txq_disable(struct iwl_trans *trans, int queue,
			   bool configure_scd);

bool iwl_trans_txq_enable_cfg(struct iwl_trans *trans, int queue, u16 ssn,
			      const struct iwl_trans_txq_scd_cfg *cfg,
			      unsigned int queue_wdg_timeout);

int iwl_trans_get_rxq_dma_data(struct iwl_trans *trans, int queue,
			       struct iwl_trans_rxq_dma_data *data);

void iwl_trans_txq_free(struct iwl_trans *trans, int queue);

int iwl_trans_txq_alloc(struct iwl_trans *trans, u32 flags, u32 sta_mask,
			u8 tid, int size, unsigned int wdg_timeout);

void iwl_trans_txq_set_shared_mode(struct iwl_trans *trans,
				   int txq_id, bool shared_mode);

static inline void iwl_trans_txq_enable(struct iwl_trans *trans, int queue,
					int fifo, int sta_id, int tid,
					int frame_limit, u16 ssn,
					unsigned int queue_wdg_timeout)
{
	struct iwl_trans_txq_scd_cfg cfg = {
		.fifo = fifo,
		.sta_id = sta_id,
		.tid = tid,
		.frame_limit = frame_limit,
		.aggregate = sta_id >= 0,
	};

	iwl_trans_txq_enable_cfg(trans, queue, ssn, &cfg, queue_wdg_timeout);
}

static inline
void iwl_trans_ac_txq_enable(struct iwl_trans *trans, int queue, int fifo,
			     unsigned int queue_wdg_timeout)
{
	struct iwl_trans_txq_scd_cfg cfg = {
		.fifo = fifo,
		.sta_id = -1,
		.tid = IWL_MAX_TID_COUNT,
		.frame_limit = IWL_FRAME_LIMIT,
		.aggregate = false,
	};

	iwl_trans_txq_enable_cfg(trans, queue, 0, &cfg, queue_wdg_timeout);
}

void iwl_trans_freeze_txq_timer(struct iwl_trans *trans,
				unsigned long txqs, bool freeze);

int iwl_trans_wait_tx_queues_empty(struct iwl_trans *trans, u32 txqs);

int iwl_trans_wait_txq_empty(struct iwl_trans *trans, int queue);

void iwl_trans_write8(struct iwl_trans *trans, u32 ofs, u8 val);

void iwl_trans_write32(struct iwl_trans *trans, u32 ofs, u32 val);

u32 iwl_trans_read32(struct iwl_trans *trans, u32 ofs);

u32 iwl_trans_read_prph(struct iwl_trans *trans, u32 ofs);

void iwl_trans_write_prph(struct iwl_trans *trans, u32 ofs, u32 val);

int iwl_trans_read_mem(struct iwl_trans *trans, u32 addr,
		       void *buf, int dwords);

int iwl_trans_read_config32(struct iwl_trans *trans, u32 ofs,
			    u32 *val);

#ifdef CONFIG_IWLWIFI_DEBUGFS
void iwl_trans_debugfs_cleanup(struct iwl_trans *trans);
#endif

#define iwl_trans_read_mem_bytes(trans, addr, buf, bufsize)	\
	({							\
		if (__builtin_constant_p(bufsize))		\
			BUILD_BUG_ON((bufsize) % sizeof(u32));	\
		iwl_trans_read_mem(trans, addr, buf,		\
				   (bufsize) / sizeof(u32));	\
	})

int iwl_trans_write_imr_mem(struct iwl_trans *trans, u32 dst_addr,
			    u64 src_addr, u32 byte_cnt);

static inline u32 iwl_trans_read_mem32(struct iwl_trans *trans, u32 addr)
{
	u32 value;

	if (iwl_trans_read_mem(trans, addr, &value, 1))
		return 0xa5a5a5a5;

	return value;
}

int iwl_trans_write_mem(struct iwl_trans *trans, u32 addr,
			const void *buf, int dwords);

static inline u32 iwl_trans_write_mem32(struct iwl_trans *trans, u32 addr,
					u32 val)
{
	return iwl_trans_write_mem(trans, addr, &val, 1);
}

void iwl_trans_set_pmi(struct iwl_trans *trans, bool state);

int iwl_trans_sw_reset(struct iwl_trans *trans);

void iwl_trans_set_bits_mask(struct iwl_trans *trans, u32 reg,
			     u32 mask, u32 value);

bool _iwl_trans_grab_nic_access(struct iwl_trans *trans);

#define iwl_trans_grab_nic_access(trans)		\
	__cond_lock(nic_access,				\
		    likely(_iwl_trans_grab_nic_access(trans)))

void __releases(nic_access)
iwl_trans_release_nic_access(struct iwl_trans *trans);

static inline void iwl_trans_schedule_reset(struct iwl_trans *trans,
					    enum iwl_fw_error_type type)
{
	if (test_bit(STATUS_TRANS_DEAD, &trans->status))
		return;
	/* clear this on device init, not cleared on any unbind/reprobe */
	if (test_and_set_bit(STATUS_TRANS_RESET_IN_PROGRESS, &trans->status))
		return;

	trans->restart.mode.type = type;
	trans->restart.mode.context = IWL_ERR_CONTEXT_WORKER;

	set_bit(STATUS_RESET_PENDING, &trans->status);

	/*
	 * keep track of whether or not this happened while resetting,
	 * by the timer the worker runs it might have finished
	 */
	trans->restart.during_reset = test_bit(STATUS_IN_SW_RESET,
					       &trans->status);
	queue_delayed_work(system_unbound_wq, &trans->restart.wk, 0);
}

static inline void iwl_trans_fw_error(struct iwl_trans *trans,
				      enum iwl_fw_error_type type)
{
	if (WARN_ON_ONCE(!trans->op_mode))
		return;

	/* prevent double restarts due to the same erroneous FW */
	if (!test_and_set_bit(STATUS_FW_ERROR, &trans->status)) {
		trans->state = IWL_TRANS_NO_FW;
		iwl_op_mode_nic_error(trans->op_mode, type);
		iwl_trans_schedule_reset(trans, type);
	}
}

static inline void iwl_trans_opmode_sw_reset(struct iwl_trans *trans,
					     enum iwl_fw_error_type type)
{
	if (WARN_ON_ONCE(!trans->op_mode))
		return;

	set_bit(STATUS_IN_SW_RESET, &trans->status);

	if (WARN_ON(type == IWL_ERR_TYPE_TOP_RESET_BY_BT))
		return;

	if (!trans->op_mode->ops->sw_reset ||
	    !trans->op_mode->ops->sw_reset(trans->op_mode, type))
		clear_bit(STATUS_IN_SW_RESET, &trans->status);
}

static inline bool iwl_trans_fw_running(struct iwl_trans *trans)
{
	return trans->state == IWL_TRANS_FW_ALIVE;
}

void iwl_trans_sync_nmi(struct iwl_trans *trans);

void iwl_trans_sync_nmi_with_addr(struct iwl_trans *trans, u32 inta_addr,
				  u32 sw_err_bit);

int iwl_trans_load_pnvm(struct iwl_trans *trans,
			const struct iwl_pnvm_image *pnvm_data,
			const struct iwl_ucode_capabilities *capa);

void iwl_trans_set_pnvm(struct iwl_trans *trans,
			const struct iwl_ucode_capabilities *capa);

int iwl_trans_load_reduce_power(struct iwl_trans *trans,
				const struct iwl_pnvm_image *payloads,
				const struct iwl_ucode_capabilities *capa);

void iwl_trans_set_reduce_power(struct iwl_trans *trans,
				const struct iwl_ucode_capabilities *capa);

static inline bool iwl_trans_dbg_ini_valid(struct iwl_trans *trans)
{
	return trans->dbg.internal_ini_cfg != IWL_INI_CFG_STATE_NOT_LOADED ||
		trans->dbg.external_ini_cfg != IWL_INI_CFG_STATE_NOT_LOADED;
}

void iwl_trans_interrupts(struct iwl_trans *trans, bool enable);

static inline void iwl_trans_finish_sw_reset(struct iwl_trans *trans)
{
	clear_bit(STATUS_IN_SW_RESET, &trans->status);
}

/*****************************************************
 * transport helper functions
 *****************************************************/
struct iwl_trans *iwl_trans_alloc(unsigned int priv_size,
				  struct device *dev,
				  const struct iwl_mac_cfg *mac_cfg);
void iwl_trans_free(struct iwl_trans *trans);

static inline bool iwl_trans_is_hw_error_value(u32 val)
{
	return ((val & ~0xf) == 0xa5a5a5a0) || ((val & ~0xf) == 0x5a5a5a50);
}

void iwl_trans_free_restart_list(void);

static inline u16 iwl_trans_get_num_rbds(struct iwl_trans *trans)
{
	u16 result = trans->cfg->num_rbds;

	/*
	 * Since AX210 family (So/Ty) the device cannot put mutliple
	 * frames into the same buffer, so double the value for them.
	 */
	if (trans->mac_cfg->device_family >= IWL_DEVICE_FAMILY_AX210)
		return 2 * result;
	return result;
}

static inline bool iwl_trans_device_enabled(struct iwl_trans *trans)
{
	return test_bit(STATUS_DEVICE_ENABLED, &trans->status);
}

static inline bool iwl_trans_is_dead(struct iwl_trans *trans)
{
	return test_bit(STATUS_TRANS_DEAD, &trans->status);
}

static inline bool iwl_trans_is_fw_error(struct iwl_trans *trans)
{
	return test_bit(STATUS_FW_ERROR, &trans->status);
}

/*
 * This function notifies the transport layer of firmware error, the recovery
 * will be handled by the op mode
 */
static inline void iwl_trans_notify_fw_error(struct iwl_trans *trans)
{
	trans->state = IWL_TRANS_NO_FW;
	set_bit(STATUS_FW_ERROR, &trans->status);
}
/*****************************************************
 * PCIe handling
 *****************************************************/
int __must_check iwl_pci_register_driver(void);
void iwl_pci_unregister_driver(void);

/* Note: order matters */
enum iwl_reset_mode {
	/* upper level modes: */
	IWL_RESET_MODE_SW_RESET,
	IWL_RESET_MODE_REPROBE,
	/* TOP reset doesn't require PCIe remove */
	IWL_RESET_MODE_TOP_RESET,
	/* PCIE level modes: */
	IWL_RESET_MODE_REMOVE_ONLY,
	IWL_RESET_MODE_RESCAN,
	IWL_RESET_MODE_FUNC_RESET,
	IWL_RESET_MODE_PROD_RESET,

	/* keep last - special backoff value */
	IWL_RESET_MODE_BACKOFF,
};

void iwl_trans_pcie_reset(struct iwl_trans *trans, enum iwl_reset_mode mode);
void iwl_trans_pcie_fw_reset_handshake(struct iwl_trans *trans);

int iwl_trans_pcie_send_hcmd(struct iwl_trans *trans,
			     struct iwl_host_cmd *cmd);

/* Internal helper */
static inline void iwl_trans_set_info(struct iwl_trans *trans,
				      struct iwl_trans_info *info)
{
	struct iwl_trans_info *write;

	write = (void *)(uintptr_t)&trans->info;
	*write = *info;
}

static inline u16 iwl_trans_get_device_id(struct iwl_trans *trans)
{
	return u32_get_bits(trans->info.hw_id, GENMASK(31, 16));
}

bool iwl_trans_is_pm_supported(struct iwl_trans *trans);

bool iwl_trans_is_ltr_enabled(struct iwl_trans *trans);

#endif /* __iwl_trans_h__ */
