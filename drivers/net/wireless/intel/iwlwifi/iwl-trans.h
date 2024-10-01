/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2005-2014, 2018-2022 Intel Corporation
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
 * an abstraction of the underlying HW to the upper layer. The transport layer
 * doesn't provide any policy, algorithm or anything of this kind, but only
 * mechanisms to make the HW do something. It is not completely stateless but
 * close to it.
 * We will have an implementation for each different supported bus.
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
 * @CMD_WANT_ASYNC_CALLBACK: the op_mode's async callback function must be
 *	called after this command completes. Valid only with CMD_ASYNC.
 * @CMD_SEND_IN_D3: Allow the command to be sent in D3 mode, relevant to
 *	SUSPEND and RESUME commands. We are in D3 mode when we set
 *	trans->system_pm_mode to IWL_PLAT_PM_MODE_D3.
 */
enum CMD_MODE {
	CMD_ASYNC		= BIT(0),
	CMD_WANT_SKB		= BIT(1),
	CMD_SEND_IN_RFKILL	= BIT(2),
	CMD_WANT_ASYNC_CALLBACK	= BIT(3),
	CMD_SEND_IN_D3          = BIT(4),
};

#define DEF_CMD_PAYLOAD_SIZE 320

/**
 * struct iwl_device_cmd
 *
 * For allocation of the command and tx queues, this establishes the overall
 * size of the largest command we send to uCode, except for commands that
 * aren't fully copied and use other TFD space.
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

/* We need 2 entries for the TX command and header, and another one might
 * be needed for potential data in the SKB's head. The remaining ones can
 * be used for frags.
 */
#define IWL_TRANS_MAX_FRAGS(trans) ((trans)->txqs.tfd.max_tbs - 3)

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
#define IWL_9000_MAX_RX_HW_QUEUES	6

/**
 * enum iwl_wowlan_status - WoWLAN image/device status
 * @IWL_D3_STATUS_ALIVE: firmware is still running after resume
 * @IWL_D3_STATUS_RESET: device was reset while suspended
 */
enum iwl_d3_status {
	IWL_D3_STATUS_ALIVE,
	IWL_D3_STATUS_RESET,
};

/**
 * enum iwl_trans_status: transport status flags
 * @STATUS_SYNC_HCMD_ACTIVE: a SYNC command is being processed
 * @STATUS_DEVICE_ENABLED: APM is enabled
 * @STATUS_TPOWER_PMI: the device might be asleep (need to wake it up)
 * @STATUS_INT_ENABLED: interrupts are enabled
 * @STATUS_RFKILL_HW: the actual HW state of the RF-kill switch
 * @STATUS_RFKILL_OPMODE: RF-kill state reported to opmode
 * @STATUS_FW_ERROR: the fw is in error state
 * @STATUS_TRANS_GOING_IDLE: shutting down the trans, only special commands
 *	are sent
 * @STATUS_TRANS_IDLE: the trans is idle - general commands are not to be sent
 * @STATUS_TRANS_DEAD: trans is dead - avoid any read/write operation
 * @STATUS_SUPPRESS_CMD_ERROR_ONCE: suppress "FW error in SYNC CMD" once,
 *	e.g. for testing
 */
enum iwl_trans_status {
	STATUS_SYNC_HCMD_ACTIVE,
	STATUS_DEVICE_ENABLED,
	STATUS_TPOWER_PMI,
	STATUS_INT_ENABLED,
	STATUS_RFKILL_HW,
	STATUS_RFKILL_OPMODE,
	STATUS_FW_ERROR,
	STATUS_TRANS_GOING_IDLE,
	STATUS_TRANS_IDLE,
	STATUS_TRANS_DEAD,
	STATUS_SUPPRESS_CMD_ERROR_ONCE,
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
 * @op_mode: pointer to the upper layer.
 * @cmd_queue: the index of the command queue.
 *	Must be set before start_fw.
 * @cmd_fifo: the fifo for host commands
 * @cmd_q_wdg_timeout: the timeout of the watchdog timer for the command queue.
 * @no_reclaim_cmds: Some devices erroneously don't set the
 *	SEQ_RX_FRAME bit on some notifications, this is the
 *	list of such notifications to filter. Max length is
 *	%MAX_NO_RECLAIM_CMDS.
 * @n_no_reclaim_cmds: # of commands in list
 * @rx_buf_size: RX buffer size needed for A-MSDUs
 *	if unset 4k will be the RX buffer size
 * @bc_table_dword: set to true if the BC table expects the byte count to be
 *	in DWORD (as opposed to bytes)
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
 */
struct iwl_trans_config {
	struct iwl_op_mode *op_mode;

	u8 cmd_queue;
	u8 cmd_fifo;
	unsigned int cmd_q_wdg_timeout;
	const u8 *no_reclaim_cmds;
	unsigned int n_no_reclaim_cmds;

	enum iwl_amsdu_size rx_buf_size;
	bool bc_table_dword;
	bool scd_set_active;
	const struct iwl_hcmd_arr *command_groups;
	int command_groups_size;

	u8 cb_data_offs;
	bool fw_reset_handshake;
	u8 queue_alloc_cmd_ver;
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

/**
 * struct iwl_trans_ops - transport specific operations
 *
 * All the handlers MUST be implemented
 *
 * @start_hw: starts the HW. From that point on, the HW can send interrupts.
 *	May sleep.
 * @op_mode_leave: Turn off the HW RF kill indication if on
 *	May sleep
 * @start_fw: allocates and inits all the resources for the transport
 *	layer. Also kick a fw image.
 *	May sleep
 * @fw_alive: called when the fw sends alive notification. If the fw provides
 *	the SCD base address in SRAM, then provide it here, or 0 otherwise.
 *	May sleep
 * @stop_device: stops the whole device (embedded CPU put to reset) and stops
 *	the HW. From that point on, the HW will be stopped but will still issue
 *	an interrupt if the HW RF kill switch is triggered.
 *	This callback must do the right thing and not crash even if %start_hw()
 *	was called but not &start_fw(). May sleep.
 * @d3_suspend: put the device into the correct mode for WoWLAN during
 *	suspend. This is optional, if not implemented WoWLAN will not be
 *	supported. This callback may sleep.
 * @d3_resume: resume the device after WoWLAN, enabling the opmode to
 *	talk to the WoWLAN image to get its status. This is optional, if not
 *	implemented WoWLAN will not be supported. This callback may sleep.
 * @send_cmd:send a host command. Must return -ERFKILL if RFkill is asserted.
 *	If RFkill is asserted in the middle of a SYNC host command, it must
 *	return -ERFKILL straight away.
 *	May sleep only if CMD_ASYNC is not set
 * @tx: send an skb. The transport relies on the op_mode to zero the
 *	the ieee80211_tx_info->driver_data. If the MPDU is an A-MSDU, all
 *	the CSUM will be taken care of (TCP CSUM and IP header in case of
 *	IPv4). If the MPDU is a single MSDU, the op_mode must compute the IP
 *	header if it is IPv4.
 *	Must be atomic
 * @reclaim: free packet until ssn. Returns a list of freed packets.
 *	Must be atomic
 * @txq_enable: setup a queue. To setup an AC queue, use the
 *	iwl_trans_ac_txq_enable wrapper. fw_alive must have been called before
 *	this one. The op_mode must not configure the HCMD queue. The scheduler
 *	configuration may be %NULL, in which case the hardware will not be
 *	configured. If true is returned, the operation mode needs to increment
 *	the sequence number of the packets routed to this queue because of a
 *	hardware scheduler bug. May sleep.
 * @txq_disable: de-configure a Tx queue to send AMPDUs
 *	Must be atomic
 * @txq_set_shared_mode: change Tx queue shared/unshared marking
 * @wait_tx_queues_empty: wait until tx queues are empty. May sleep.
 * @wait_txq_empty: wait until specific tx queue is empty. May sleep.
 * @freeze_txq_timer: prevents the timer of the queue from firing until the
 *	queue is set to awake. Must be atomic.
 * @block_txq_ptrs: stop updating the write pointers of the Tx queues. Note
 *	that the transport needs to refcount the calls since this function
 *	will be called several times with block = true, and then the queues
 *	need to be unblocked only after the same number of calls with
 *	block = false.
 * @write8: write a u8 to a register at offset ofs from the BAR
 * @write32: write a u32 to a register at offset ofs from the BAR
 * @read32: read a u32 register at offset ofs from the BAR
 * @read_prph: read a DWORD from a periphery register
 * @write_prph: write a DWORD to a periphery register
 * @read_mem: read device's SRAM in DWORD
 * @write_mem: write device's SRAM in DWORD. If %buf is %NULL, then the memory
 *	will be zeroed.
 * @read_config32: read a u32 value from the device's config space at
 *	the given offset.
 * @configure: configure parameters required by the transport layer from
 *	the op_mode. May be called several times before start_fw, can't be
 *	called after that.
 * @set_pmi: set the power pmi state
 * @grab_nic_access: wake the NIC to be able to access non-HBUS regs.
 *	Sleeping is not allowed between grab_nic_access and
 *	release_nic_access.
 * @release_nic_access: let the NIC go to sleep. The "flags" parameter
 *	must be the same one that was sent before to the grab_nic_access.
 * @set_bits_mask - set SRAM register according to value and mask.
 * @dump_data: return a vmalloc'ed buffer with debug data, maybe containing last
 *	TX'ed commands and similar. The buffer will be vfree'd by the caller.
 *	Note that the transport must fill in the proper file headers.
 * @debugfs_cleanup: used in the driver unload flow to make a proper cleanup
 *	of the trans debugfs
 * @set_pnvm: set the pnvm data in the prph scratch buffer, inside the
 *	context info.
 * @interrupts: disable/enable interrupts to transport
 */
struct iwl_trans_ops {

	int (*start_hw)(struct iwl_trans *iwl_trans);
	void (*op_mode_leave)(struct iwl_trans *iwl_trans);
	int (*start_fw)(struct iwl_trans *trans, const struct fw_img *fw,
			bool run_in_rfkill);
	void (*fw_alive)(struct iwl_trans *trans, u32 scd_addr);
	void (*stop_device)(struct iwl_trans *trans);

	int (*d3_suspend)(struct iwl_trans *trans, bool test, bool reset);
	int (*d3_resume)(struct iwl_trans *trans, enum iwl_d3_status *status,
			 bool test, bool reset);

	int (*send_cmd)(struct iwl_trans *trans, struct iwl_host_cmd *cmd);

	int (*tx)(struct iwl_trans *trans, struct sk_buff *skb,
		  struct iwl_device_tx_cmd *dev_cmd, int queue);
	void (*reclaim)(struct iwl_trans *trans, int queue, int ssn,
			struct sk_buff_head *skbs, bool is_flush);

	void (*set_q_ptrs)(struct iwl_trans *trans, int queue, int ptr);

	bool (*txq_enable)(struct iwl_trans *trans, int queue, u16 ssn,
			   const struct iwl_trans_txq_scd_cfg *cfg,
			   unsigned int queue_wdg_timeout);
	void (*txq_disable)(struct iwl_trans *trans, int queue,
			    bool configure_scd);
	/* 22000 functions */
	int (*txq_alloc)(struct iwl_trans *trans, u32 flags,
			 u32 sta_mask, u8 tid,
			 int size, unsigned int queue_wdg_timeout);
	void (*txq_free)(struct iwl_trans *trans, int queue);
	int (*rxq_dma_data)(struct iwl_trans *trans, int queue,
			    struct iwl_trans_rxq_dma_data *data);

	void (*txq_set_shared_mode)(struct iwl_trans *trans, u32 txq_id,
				    bool shared);

	int (*wait_tx_queues_empty)(struct iwl_trans *trans, u32 txq_bm);
	int (*wait_txq_empty)(struct iwl_trans *trans, int queue);
	void (*freeze_txq_timer)(struct iwl_trans *trans, unsigned long txqs,
				 bool freeze);
	void (*block_txq_ptrs)(struct iwl_trans *trans, bool block);

	void (*write8)(struct iwl_trans *trans, u32 ofs, u8 val);
	void (*write32)(struct iwl_trans *trans, u32 ofs, u32 val);
	u32 (*read32)(struct iwl_trans *trans, u32 ofs);
	u32 (*read_prph)(struct iwl_trans *trans, u32 ofs);
	void (*write_prph)(struct iwl_trans *trans, u32 ofs, u32 val);
	int (*read_mem)(struct iwl_trans *trans, u32 addr,
			void *buf, int dwords);
	int (*write_mem)(struct iwl_trans *trans, u32 addr,
			 const void *buf, int dwords);
	int (*read_config32)(struct iwl_trans *trans, u32 ofs, u32 *val);
	void (*configure)(struct iwl_trans *trans,
			  const struct iwl_trans_config *trans_cfg);
	void (*set_pmi)(struct iwl_trans *trans, bool state);
	int (*sw_reset)(struct iwl_trans *trans, bool retake_ownership);
	bool (*grab_nic_access)(struct iwl_trans *trans);
	void (*release_nic_access)(struct iwl_trans *trans);
	void (*set_bits_mask)(struct iwl_trans *trans, u32 reg, u32 mask,
			      u32 value);

	struct iwl_trans_dump_data *(*dump_data)(struct iwl_trans *trans,
						 u32 dump_mask,
						 const struct iwl_dump_sanitize_ops *sanitize_ops,
						 void *sanitize_ctx);
	void (*debugfs_cleanup)(struct iwl_trans *trans);
	void (*sync_nmi)(struct iwl_trans *trans);
	int (*set_pnvm)(struct iwl_trans *trans, const void *data, u32 len);
	int (*set_reduce_power)(struct iwl_trans *trans,
				const void *data, u32 len);
	void (*interrupts)(struct iwl_trans *trans, bool enable);
	int (*imr_dma_data)(struct iwl_trans *trans,
			    u32 dst_addr, u64 src_addr,
			    u32 byte_cnt);

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
 * enum iwl_plat_pm_mode - platform power management mode
 *
 * This enumeration describes the device's platform power management
 * behavior when in system-wide suspend (i.e WoWLAN).
 *
 * @IWL_PLAT_PM_MODE_DISABLED: power management is disabled for this
 *	device.  In system-wide suspend mode, it means that the all
 *	connections will be closed automatically by mac80211 before
 *	the platform is suspended.
 * @IWL_PLAT_PM_MODE_D3: the device goes into D3 mode (i.e. WoWLAN).
 */
enum iwl_plat_pm_mode {
	IWL_PLAT_PM_MODE_DISABLED,
	IWL_PLAT_PM_MODE_D3,
};

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
 * @imr2sram_remainbyte`: size remained after each dma transfer
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

/**
 * struct iwl_trans_debug - transport debug related data
 *
 * @n_dest_reg: num of reg_ops in %dbg_dest_tlv
 * @rec_on: true iff there is a fw debug recording currently active
 * @dest_tlv: points to the destination TLV for debug
 * @conf_tlv: array of pointers to configuration TLVs for debug
 * @trigger_tlv: array of pointers to triggers TLVs for debug
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
 * @active_regions: active regions
 * @debug_info_tlv_list: list of debug info TLVs
 * @time_point: array of debug time points
 * @periodic_trig_list: periodic triggers list
 * @domains_bitmap: bitmap of active domains other than &IWL_FW_INI_DOMAIN_ALWAYS_ON
 * @ucode_preset: preset based on ucode
 */
struct iwl_trans_debug {
	u8 n_dest_reg;
	bool rec_on;

	const struct iwl_fw_dbg_dest_tlv_v1 *dest_tlv;
	const struct iwl_fw_dbg_conf_tlv *conf_tlv[FW_DBG_CONF_MAX];
	struct iwl_fw_dbg_trigger_tlv * const *trigger_tlv;

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
	struct iwl_dbg_tlv_time_point_data
		time_point[IWL_FW_INI_TIME_POINT_NUM];
	struct list_head periodic_trig_list;

	u32 domains_bitmap;
	u32 ucode_preset;
	bool restart_required;
	u32 last_tp_resetfw;
	struct iwl_imr_data imr_data;
};

struct iwl_dma_ptr {
	dma_addr_t dma;
	void *addr;
	size_t size;
};

struct iwl_cmd_meta {
	/* only for SYNC commands, iff the reply skb is wanted */
	struct iwl_host_cmd *source;
	u32 flags;
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
 * @q: generic Rx/Tx queue descriptor
 * @tfds: transmit frame descriptors (DMA memory)
 * @first_tb_bufs: start of command headers, including scratch buffers, for
 *	the writeback -- this is DMA memory and an array holding one buffer
 *	for each command on the queue
 * @first_tb_dma: DMA address for the first_tb_bufs start
 * @entries: transmit entries (driver state)
 * @lock: queue lock
 * @stuck_timer: timer that fires if queue gets stuck
 * @trans: pointer back to transport (for timer)
 * @need_update: indicates need to update read/write index
 * @ampdu: true if this queue is an ampdu queue for an specific RA/TID
 * @wd_timeout: queue watchdog timeout (jiffies) - per queue
 * @frozen: tx stuck queue timer is frozen
 * @frozen_expiry_remainder: remember how long until the timer fires
 * @bc_tbl: byte count table of the queue (relevant only for gen2 transport)
 * @write_ptr: 1-st empty entry (index) host_w
 * @read_ptr: last used entry (index) host_r
 * @dma_addr:  physical addr for BD's
 * @n_window: safe queue window
 * @id: queue id
 * @low_mark: low watermark, resume queue if free space more than this
 * @high_mark: high watermark, stop queue if free space less than this
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
 * struct iwl_trans_txqs - transport tx queues data
 *
 * @bc_table_dword: true if the BC table expects DWORD (as opposed to bytes)
 * @page_offs: offset from skb->cb to mac header page pointer
 * @dev_cmd_offs: offset from skb->cb to iwl_device_tx_cmd pointer
 * @queue_used - bit mask of used queues
 * @queue_stopped - bit mask of stopped queues
 * @scd_bc_tbls: gen1 pointer to the byte count table of the scheduler
 * @queue_alloc_cmd_ver: queue allocation command version
 */
struct iwl_trans_txqs {
	unsigned long queue_used[BITS_TO_LONGS(IWL_MAX_TVQM_QUEUES)];
	unsigned long queue_stopped[BITS_TO_LONGS(IWL_MAX_TVQM_QUEUES)];
	struct iwl_txq *txq[IWL_MAX_TVQM_QUEUES];
	struct dma_pool *bc_pool;
	size_t bc_tbl_size;
	bool bc_table_dword;
	u8 page_offs;
	u8 dev_cmd_offs;
	struct iwl_tso_hdr_page __percpu *tso_hdr_page;

	struct {
		u8 fifo;
		u8 q_id;
		unsigned int wdg_timeout;
	} cmd;

	struct {
		u8 max_tbs;
		u16 size;
		u8 addr_size;
	} tfd;

	struct iwl_dma_ptr scd_bc_tbls;

	u8 queue_alloc_cmd_ver;
};

/**
 * struct iwl_trans - transport common data
 *
 * @csme_own - true if we couldn't get ownership on the device
 * @ops - pointer to iwl_trans_ops
 * @op_mode - pointer to the op_mode
 * @trans_cfg: the trans-specific configuration part
 * @cfg - pointer to the configuration
 * @drv - pointer to iwl_drv
 * @status: a bit-mask of transport status flags
 * @dev - pointer to struct device * that represents the device
 * @max_skb_frags: maximum number of fragments an SKB can have when transmitted.
 *	0 indicates that frag SKBs (NETIF_F_SG) aren't supported.
 * @hw_rf_id a u32 with the device RF ID
 * @hw_id: a u32 with the ID of the device / sub-device.
 *	Set during transport allocation.
 * @hw_id_str: a string with info about HW ID. Set during transport allocation.
 * @hw_rev_step: The mac step of the HW
 * @pm_support: set to true in start_hw if link pm is supported
 * @ltr_enabled: set to true if the LTR is enabled
 * @wide_cmd_header: true when ucode supports wide command header format
 * @wait_command_queue: wait queue for sync commands
 * @num_rx_queues: number of RX queues allocated by the transport;
 *	the transport must set this before calling iwl_drv_start()
 * @iml_len: the length of the image loader
 * @iml: a pointer to the image loader itself
 * @dev_cmd_pool: pool for Tx cmd allocation - for internal use only.
 *	The user should use iwl_trans_{alloc,free}_tx_cmd.
 * @rx_mpdu_cmd: MPDU RX command ID, must be assigned by opmode before
 *	starting the firmware, used for tracing
 * @rx_mpdu_cmd_hdr_size: used for tracing, amount of data before the
 *	start of the 802.11 header in the @rx_mpdu_cmd
 * @dflt_pwr_limit: default power limit fetched from the platform (ACPI)
 * @system_pm_mode: the system-wide power management mode in use.
 *	This mode is set dynamically, depending on the WoWLAN values
 *	configured from the userspace at runtime.
 * @iwl_trans_txqs: transport tx queues data.
 */
struct iwl_trans {
	bool csme_own;
	const struct iwl_trans_ops *ops;
	struct iwl_op_mode *op_mode;
	const struct iwl_cfg_trans_params *trans_cfg;
	const struct iwl_cfg *cfg;
	struct iwl_drv *drv;
	enum iwl_trans_state state;
	unsigned long status;

	struct device *dev;
	u32 max_skb_frags;
	u32 hw_rev;
	u32 hw_rev_step;
	u32 hw_rf_id;
	u32 hw_id;
	char hw_id_str[52];
	u32 sku_id[3];

	u8 rx_mpdu_cmd, rx_mpdu_cmd_hdr_size;

	bool pm_support;
	bool ltr_enabled;
	u8 pnvm_loaded:1;
	u8 reduce_power_loaded:1;

	const struct iwl_hcmd_arr *command_groups;
	int command_groups_size;
	bool wide_cmd_header;

	wait_queue_head_t wait_command_queue;
	u8 num_rx_queues;

	size_t iml_len;
	u8 *iml;

	/* The following fields are internal only */
	struct kmem_cache *dev_cmd_pool;
	char dev_cmd_pool_name[50];

	struct dentry *dbgfs_dir;

#ifdef CONFIG_LOCKDEP
	struct lockdep_map sync_cmd_lockdep_map;
#endif

	struct iwl_trans_debug dbg;
	struct iwl_self_init_dram init_dram;

	enum iwl_plat_pm_mode system_pm_mode;

	const char *name;
	struct iwl_trans_txqs txqs;

	/* pointer to trans specific struct */
	/*Ensure that this pointer will always be aligned to sizeof pointer */
	char trans_specific[] __aligned(sizeof(void *));
};

const char *iwl_get_cmd_string(struct iwl_trans *trans, u32 id);
int iwl_cmd_groups_verify_sorted(const struct iwl_trans_config *trans);

static inline void iwl_trans_configure(struct iwl_trans *trans,
				       const struct iwl_trans_config *trans_cfg)
{
	trans->op_mode = trans_cfg->op_mode;

	trans->ops->configure(trans, trans_cfg);
	WARN_ON(iwl_cmd_groups_verify_sorted(trans_cfg));
}

static inline int iwl_trans_start_hw(struct iwl_trans *trans)
{
	might_sleep();

	return trans->ops->start_hw(trans);
}

static inline void iwl_trans_op_mode_leave(struct iwl_trans *trans)
{
	might_sleep();

	if (trans->ops->op_mode_leave)
		trans->ops->op_mode_leave(trans);

	trans->op_mode = NULL;

	trans->state = IWL_TRANS_NO_FW;
}

static inline void iwl_trans_fw_alive(struct iwl_trans *trans, u32 scd_addr)
{
	might_sleep();

	trans->state = IWL_TRANS_FW_ALIVE;

	trans->ops->fw_alive(trans, scd_addr);
}

static inline int iwl_trans_start_fw(struct iwl_trans *trans,
				     const struct fw_img *fw,
				     bool run_in_rfkill)
{
	int ret;

	might_sleep();

	WARN_ON_ONCE(!trans->rx_mpdu_cmd);

	clear_bit(STATUS_FW_ERROR, &trans->status);
	ret = trans->ops->start_fw(trans, fw, run_in_rfkill);
	if (ret == 0)
		trans->state = IWL_TRANS_FW_STARTED;

	return ret;
}

static inline void iwl_trans_stop_device(struct iwl_trans *trans)
{
	might_sleep();

	trans->ops->stop_device(trans);

	trans->state = IWL_TRANS_NO_FW;
}

static inline int iwl_trans_d3_suspend(struct iwl_trans *trans, bool test,
				       bool reset)
{
	might_sleep();
	if (!trans->ops->d3_suspend)
		return 0;

	return trans->ops->d3_suspend(trans, test, reset);
}

static inline int iwl_trans_d3_resume(struct iwl_trans *trans,
				      enum iwl_d3_status *status,
				      bool test, bool reset)
{
	might_sleep();
	if (!trans->ops->d3_resume)
		return 0;

	return trans->ops->d3_resume(trans, status, test, reset);
}

static inline struct iwl_trans_dump_data *
iwl_trans_dump_data(struct iwl_trans *trans, u32 dump_mask,
		    const struct iwl_dump_sanitize_ops *sanitize_ops,
		    void *sanitize_ctx)
{
	if (!trans->ops->dump_data)
		return NULL;
	return trans->ops->dump_data(trans, dump_mask,
				     sanitize_ops, sanitize_ctx);
}

static inline struct iwl_device_tx_cmd *
iwl_trans_alloc_tx_cmd(struct iwl_trans *trans)
{
	return kmem_cache_zalloc(trans->dev_cmd_pool, GFP_ATOMIC);
}

int iwl_trans_send_cmd(struct iwl_trans *trans, struct iwl_host_cmd *cmd);

static inline void iwl_trans_free_tx_cmd(struct iwl_trans *trans,
					 struct iwl_device_tx_cmd *dev_cmd)
{
	kmem_cache_free(trans->dev_cmd_pool, dev_cmd);
}

static inline int iwl_trans_tx(struct iwl_trans *trans, struct sk_buff *skb,
			       struct iwl_device_tx_cmd *dev_cmd, int queue)
{
	if (unlikely(test_bit(STATUS_FW_ERROR, &trans->status)))
		return -EIO;

	if (WARN_ON_ONCE(trans->state != IWL_TRANS_FW_ALIVE)) {
		IWL_ERR(trans, "%s bad state = %d\n", __func__, trans->state);
		return -EIO;
	}

	return trans->ops->tx(trans, skb, dev_cmd, queue);
}

static inline void iwl_trans_reclaim(struct iwl_trans *trans, int queue,
				     int ssn, struct sk_buff_head *skbs,
				     bool is_flush)
{
	if (WARN_ON_ONCE(trans->state != IWL_TRANS_FW_ALIVE)) {
		IWL_ERR(trans, "%s bad state = %d\n", __func__, trans->state);
		return;
	}

	trans->ops->reclaim(trans, queue, ssn, skbs, is_flush);
}

static inline void iwl_trans_set_q_ptrs(struct iwl_trans *trans, int queue,
					int ptr)
{
	if (WARN_ON_ONCE(trans->state != IWL_TRANS_FW_ALIVE)) {
		IWL_ERR(trans, "%s bad state = %d\n", __func__, trans->state);
		return;
	}

	trans->ops->set_q_ptrs(trans, queue, ptr);
}

static inline void iwl_trans_txq_disable(struct iwl_trans *trans, int queue,
					 bool configure_scd)
{
	trans->ops->txq_disable(trans, queue, configure_scd);
}

static inline bool
iwl_trans_txq_enable_cfg(struct iwl_trans *trans, int queue, u16 ssn,
			 const struct iwl_trans_txq_scd_cfg *cfg,
			 unsigned int queue_wdg_timeout)
{
	might_sleep();

	if (WARN_ON_ONCE(trans->state != IWL_TRANS_FW_ALIVE)) {
		IWL_ERR(trans, "%s bad state = %d\n", __func__, trans->state);
		return false;
	}

	return trans->ops->txq_enable(trans, queue, ssn,
				      cfg, queue_wdg_timeout);
}

static inline int
iwl_trans_get_rxq_dma_data(struct iwl_trans *trans, int queue,
			   struct iwl_trans_rxq_dma_data *data)
{
	if (WARN_ON_ONCE(!trans->ops->rxq_dma_data))
		return -ENOTSUPP;

	return trans->ops->rxq_dma_data(trans, queue, data);
}

static inline void
iwl_trans_txq_free(struct iwl_trans *trans, int queue)
{
	if (WARN_ON_ONCE(!trans->ops->txq_free))
		return;

	trans->ops->txq_free(trans, queue);
}

static inline int
iwl_trans_txq_alloc(struct iwl_trans *trans,
		    u32 flags, u32 sta_mask, u8 tid,
		    int size, unsigned int wdg_timeout)
{
	might_sleep();

	if (WARN_ON_ONCE(!trans->ops->txq_alloc))
		return -ENOTSUPP;

	if (WARN_ON_ONCE(trans->state != IWL_TRANS_FW_ALIVE)) {
		IWL_ERR(trans, "%s bad state = %d\n", __func__, trans->state);
		return -EIO;
	}

	return trans->ops->txq_alloc(trans, flags, sta_mask, tid,
				     size, wdg_timeout);
}

static inline void iwl_trans_txq_set_shared_mode(struct iwl_trans *trans,
						 int queue, bool shared_mode)
{
	if (trans->ops->txq_set_shared_mode)
		trans->ops->txq_set_shared_mode(trans, queue, shared_mode);
}

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

static inline void iwl_trans_freeze_txq_timer(struct iwl_trans *trans,
					      unsigned long txqs,
					      bool freeze)
{
	if (WARN_ON_ONCE(trans->state != IWL_TRANS_FW_ALIVE)) {
		IWL_ERR(trans, "%s bad state = %d\n", __func__, trans->state);
		return;
	}

	if (trans->ops->freeze_txq_timer)
		trans->ops->freeze_txq_timer(trans, txqs, freeze);
}

static inline void iwl_trans_block_txq_ptrs(struct iwl_trans *trans,
					    bool block)
{
	if (WARN_ON_ONCE(trans->state != IWL_TRANS_FW_ALIVE)) {
		IWL_ERR(trans, "%s bad state = %d\n", __func__, trans->state);
		return;
	}

	if (trans->ops->block_txq_ptrs)
		trans->ops->block_txq_ptrs(trans, block);
}

static inline int iwl_trans_wait_tx_queues_empty(struct iwl_trans *trans,
						 u32 txqs)
{
	if (WARN_ON_ONCE(!trans->ops->wait_tx_queues_empty))
		return -ENOTSUPP;

	/* No need to wait if the firmware is not alive */
	if (trans->state != IWL_TRANS_FW_ALIVE) {
		IWL_ERR(trans, "%s bad state = %d\n", __func__, trans->state);
		return -EIO;
	}

	return trans->ops->wait_tx_queues_empty(trans, txqs);
}

static inline int iwl_trans_wait_txq_empty(struct iwl_trans *trans, int queue)
{
	if (WARN_ON_ONCE(!trans->ops->wait_txq_empty))
		return -ENOTSUPP;

	if (WARN_ON_ONCE(trans->state != IWL_TRANS_FW_ALIVE)) {
		IWL_ERR(trans, "%s bad state = %d\n", __func__, trans->state);
		return -EIO;
	}

	return trans->ops->wait_txq_empty(trans, queue);
}

static inline void iwl_trans_write8(struct iwl_trans *trans, u32 ofs, u8 val)
{
	trans->ops->write8(trans, ofs, val);
}

static inline void iwl_trans_write32(struct iwl_trans *trans, u32 ofs, u32 val)
{
	trans->ops->write32(trans, ofs, val);
}

static inline u32 iwl_trans_read32(struct iwl_trans *trans, u32 ofs)
{
	return trans->ops->read32(trans, ofs);
}

static inline u32 iwl_trans_read_prph(struct iwl_trans *trans, u32 ofs)
{
	return trans->ops->read_prph(trans, ofs);
}

static inline void iwl_trans_write_prph(struct iwl_trans *trans, u32 ofs,
					u32 val)
{
	return trans->ops->write_prph(trans, ofs, val);
}

static inline int iwl_trans_read_mem(struct iwl_trans *trans, u32 addr,
				     void *buf, int dwords)
{
	return trans->ops->read_mem(trans, addr, buf, dwords);
}

#define iwl_trans_read_mem_bytes(trans, addr, buf, bufsize)		      \
	do {								      \
		if (__builtin_constant_p(bufsize))			      \
			BUILD_BUG_ON((bufsize) % sizeof(u32));		      \
		iwl_trans_read_mem(trans, addr, buf, (bufsize) / sizeof(u32));\
	} while (0)

static inline int iwl_trans_write_imr_mem(struct iwl_trans *trans,
					  u32 dst_addr, u64 src_addr,
					  u32 byte_cnt)
{
	if (trans->ops->imr_dma_data)
		return trans->ops->imr_dma_data(trans, dst_addr, src_addr, byte_cnt);
	return 0;
}

static inline u32 iwl_trans_read_mem32(struct iwl_trans *trans, u32 addr)
{
	u32 value;

	if (WARN_ON(iwl_trans_read_mem(trans, addr, &value, 1)))
		return 0xa5a5a5a5;

	return value;
}

static inline int iwl_trans_write_mem(struct iwl_trans *trans, u32 addr,
				      const void *buf, int dwords)
{
	return trans->ops->write_mem(trans, addr, buf, dwords);
}

static inline u32 iwl_trans_write_mem32(struct iwl_trans *trans, u32 addr,
					u32 val)
{
	return iwl_trans_write_mem(trans, addr, &val, 1);
}

static inline void iwl_trans_set_pmi(struct iwl_trans *trans, bool state)
{
	if (trans->ops->set_pmi)
		trans->ops->set_pmi(trans, state);
}

static inline int iwl_trans_sw_reset(struct iwl_trans *trans,
				     bool retake_ownership)
{
	if (trans->ops->sw_reset)
		return trans->ops->sw_reset(trans, retake_ownership);
	return 0;
}

static inline void
iwl_trans_set_bits_mask(struct iwl_trans *trans, u32 reg, u32 mask, u32 value)
{
	trans->ops->set_bits_mask(trans, reg, mask, value);
}

#define iwl_trans_grab_nic_access(trans)		\
	__cond_lock(nic_access,				\
		    likely((trans)->ops->grab_nic_access(trans)))

static inline void __releases(nic_access)
iwl_trans_release_nic_access(struct iwl_trans *trans)
{
	trans->ops->release_nic_access(trans);
	__release(nic_access);
}

static inline void iwl_trans_fw_error(struct iwl_trans *trans, bool sync)
{
	if (WARN_ON_ONCE(!trans->op_mode))
		return;

	/* prevent double restarts due to the same erroneous FW */
	if (!test_and_set_bit(STATUS_FW_ERROR, &trans->status)) {
		iwl_op_mode_nic_error(trans->op_mode, sync);
		trans->state = IWL_TRANS_NO_FW;
	}
}

static inline bool iwl_trans_fw_running(struct iwl_trans *trans)
{
	return trans->state == IWL_TRANS_FW_ALIVE;
}

static inline void iwl_trans_sync_nmi(struct iwl_trans *trans)
{
	if (trans->ops->sync_nmi)
		trans->ops->sync_nmi(trans);
}

void iwl_trans_sync_nmi_with_addr(struct iwl_trans *trans, u32 inta_addr,
				  u32 sw_err_bit);

static inline int iwl_trans_set_pnvm(struct iwl_trans *trans,
				     const void *data, u32 len)
{
	if (trans->ops->set_pnvm) {
		int ret = trans->ops->set_pnvm(trans, data, len);

		if (ret)
			return ret;
	}

	trans->pnvm_loaded = true;

	return 0;
}

static inline int iwl_trans_set_reduce_power(struct iwl_trans *trans,
					     const void *data, u32 len)
{
	if (trans->ops->set_reduce_power) {
		int ret = trans->ops->set_reduce_power(trans, data, len);

		if (ret)
			return ret;
	}

	trans->reduce_power_loaded = true;
	return 0;
}

static inline bool iwl_trans_dbg_ini_valid(struct iwl_trans *trans)
{
	return trans->dbg.internal_ini_cfg != IWL_INI_CFG_STATE_NOT_LOADED ||
		trans->dbg.external_ini_cfg != IWL_INI_CFG_STATE_NOT_LOADED;
}

static inline void iwl_trans_interrupts(struct iwl_trans *trans, bool enable)
{
	if (trans->ops->interrupts)
		trans->ops->interrupts(trans, enable);
}

/*****************************************************
 * transport helper functions
 *****************************************************/
struct iwl_trans *iwl_trans_alloc(unsigned int priv_size,
			  struct device *dev,
			  const struct iwl_trans_ops *ops,
			  const struct iwl_cfg_trans_params *cfg_trans);
int iwl_trans_init(struct iwl_trans *trans);
void iwl_trans_free(struct iwl_trans *trans);

/*****************************************************
* driver (transport) register/unregister functions
******************************************************/
int __must_check iwl_pci_register_driver(void);
void iwl_pci_unregister_driver(void);

#endif /* __iwl_trans_h__ */
