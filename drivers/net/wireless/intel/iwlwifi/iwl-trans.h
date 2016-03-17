/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016        Intel Deutschland GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016        Intel Deutschland GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
#ifndef __iwl_trans_h__
#define __iwl_trans_h__

#include <linux/ieee80211.h>
#include <linux/mm.h> /* for page_address */
#include <linux/lockdep.h>
#include <linux/kernel.h>

#include "iwl-debug.h"
#include "iwl-config.h"
#include "iwl-fw.h"
#include "iwl-op-mode.h"

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

/**
 * DOC: Host command section
 *
 * A host command is a command issued by the upper layer to the fw. There are
 * several versions of fw that have several APIs. The transport layer is
 * completely agnostic to these differences.
 * The transport does provide helper functionality (i.e. SYNC / ASYNC mode),
 */
#define SEQ_TO_QUEUE(s)	(((s) >> 8) & 0x1f)
#define QUEUE_TO_SEQ(q)	(((q) & 0x1f) << 8)
#define SEQ_TO_INDEX(s)	((s) & 0xff)
#define INDEX_TO_SEQ(i)	((i) & 0xff)
#define SEQ_RX_FRAME	cpu_to_le16(0x8000)

/*
 * those functions retrieve specific information from
 * the id field in the iwl_host_cmd struct which contains
 * the command id, the group id and the version of the command
 * and vice versa
*/
static inline u8 iwl_cmd_opcode(u32 cmdid)
{
	return cmdid & 0xFF;
}

static inline u8 iwl_cmd_groupid(u32 cmdid)
{
	return ((cmdid & 0xFF00) >> 8);
}

static inline u8 iwl_cmd_version(u32 cmdid)
{
	return ((cmdid & 0xFF0000) >> 16);
}

static inline u32 iwl_cmd_id(u8 opcode, u8 groupid, u8 version)
{
	return opcode + (groupid << 8) + (version << 16);
}

/* make u16 wide id out of u8 group and opcode */
#define WIDE_ID(grp, opcode) ((grp << 8) | opcode)

/* due to the conversion, this group is special; new groups
 * should be defined in the appropriate fw-api header files
 */
#define IWL_ALWAYS_LONG_GROUP	1

/**
 * struct iwl_cmd_header
 *
 * This header format appears in the beginning of each command sent from the
 * driver, and each response/notification received from uCode.
 */
struct iwl_cmd_header {
	u8 cmd;		/* Command ID:  REPLY_RXON, etc. */
	u8 group_id;
	/*
	 * The driver sets up the sequence number to values of its choosing.
	 * uCode does not use this value, but passes it back to the driver
	 * when sending the response to each driver-originated command, so
	 * the driver can match the response to the command.  Since the values
	 * don't get used by uCode, the driver may set up an arbitrary format.
	 *
	 * There is one exception:  uCode sets bit 15 when it originates
	 * the response/notification, i.e. when the response/notification
	 * is not a direct response to a command sent by the driver.  For
	 * example, uCode issues REPLY_RX when it sends a received frame
	 * to the driver; it is not a direct response to any driver command.
	 *
	 * The Linux driver uses the following format:
	 *
	 *  0:7		tfd index - position within TX queue
	 *  8:12	TX queue id
	 *  13:14	reserved
	 *  15		unsolicited RX or uCode-originated notification
	 */
	__le16 sequence;
} __packed;

/**
 * struct iwl_cmd_header_wide
 *
 * This header format appears in the beginning of each command sent from the
 * driver, and each response/notification received from uCode.
 * this is the wide version that contains more information about the command
 * like length, version and command type
 */
struct iwl_cmd_header_wide {
	u8 cmd;
	u8 group_id;
	__le16 sequence;
	__le16 length;
	u8 reserved;
	u8 version;
} __packed;

#define FH_RSCSR_FRAME_SIZE_MSK		0x00003FFF	/* bits 0-13 */
#define FH_RSCSR_FRAME_INVALID		0x55550000
#define FH_RSCSR_FRAME_ALIGN		0x40

struct iwl_rx_packet {
	/*
	 * The first 4 bytes of the RX frame header contain both the RX frame
	 * size and some flags.
	 * Bit fields:
	 * 31:    flag flush RB request
	 * 30:    flag ignore TC (terminal counter) request
	 * 29:    flag fast IRQ request
	 * 28-14: Reserved
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
 * @CMD_HIGH_PRIO: The command is high priority - it goes to the front of the
 *	command queue, but after other high priority commands. Valid only
 *	with CMD_ASYNC.
 * @CMD_SEND_IN_IDLE: The command should be sent even when the trans is idle.
 * @CMD_MAKE_TRANS_IDLE: The command response should mark the trans as idle.
 * @CMD_WAKE_UP_TRANS: The command response should wake up the trans
 *	(i.e. mark it as non-idle).
 * @CMD_WANT_ASYNC_CALLBACK: the op_mode's async callback function must be
 *	called after this command completes. Valid only with CMD_ASYNC.
 * @CMD_TB_BITMAP_POS: Position of the first bit for the TB bitmap. We need to
 *	check that we leave enough room for the TBs bitmap which needs 20 bits.
 */
enum CMD_MODE {
	CMD_ASYNC		= BIT(0),
	CMD_WANT_SKB		= BIT(1),
	CMD_SEND_IN_RFKILL	= BIT(2),
	CMD_HIGH_PRIO		= BIT(3),
	CMD_SEND_IN_IDLE	= BIT(4),
	CMD_MAKE_TRANS_IDLE	= BIT(5),
	CMD_WAKE_UP_TRANS	= BIT(6),
	CMD_WANT_ASYNC_CALLBACK	= BIT(7),

	CMD_TB_BITMAP_POS	= 11,
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

#define TFD_MAX_PAYLOAD_SIZE (sizeof(struct iwl_device_cmd))

/*
 * number of transfer buffers (fragments) per transmit frame descriptor;
 * this is just the driver's idea, the hardware supports 20
 */
#define IWL_MAX_CMD_TBS_PER_TFD	2

/**
 * struct iwl_hcmd_dataflag - flag for each one of the chunks of the command
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

/*
 * The first entry in driver_data array in ieee80211_tx_info
 * that can be used by the transport.
 */
#define IWL_TRANS_FIRST_DRIVER_DATA 2
#define IWL_MASK(lo, hi) ((1 << (hi)) | ((1 << (hi)) - (1 << (lo))))

/*
 * Maximum number of HW queues the transport layer
 * currently supports
 */
#define IWL_MAX_HW_QUEUES		32
#define IWL_MAX_TID_COUNT	8
#define IWL_FRAME_LIMIT	64
#define IWL_MAX_RX_HW_QUEUES	16

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
 * @STATUS_RFKILL: the HW RFkill switch is in KILL position
 * @STATUS_FW_ERROR: the fw is in error state
 * @STATUS_TRANS_GOING_IDLE: shutting down the trans, only special commands
 *	are sent
 * @STATUS_TRANS_IDLE: the trans is idle - general commands are not to be sent
 * @STATUS_TRANS_DEAD: trans is dead - avoid any read/write operation
 */
enum iwl_trans_status {
	STATUS_SYNC_HCMD_ACTIVE,
	STATUS_DEVICE_ENABLED,
	STATUS_TPOWER_PMI,
	STATUS_INT_ENABLED,
	STATUS_RFKILL,
	STATUS_FW_ERROR,
	STATUS_TRANS_GOING_IDLE,
	STATUS_TRANS_IDLE,
	STATUS_TRANS_DEAD,
};

static inline int
iwl_trans_get_rb_size_order(enum iwl_amsdu_size rb_size)
{
	switch (rb_size) {
	case IWL_AMSDU_4K:
		return get_order(4 * 1024);
	case IWL_AMSDU_8K:
		return get_order(8 * 1024);
	case IWL_AMSDU_12K:
		return get_order(12 * 1024);
	default:
		WARN_ON(1);
		return -1;
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
 * @wide_cmd_header: firmware supports wide host command header
 * @sw_csum_tx: transport should compute the TCP checksum
 * @command_groups: array of command groups, each member is an array of the
 *	commands in the group; for debugging only
 * @command_groups_size: number of command groups, to avoid illegal access
 * @sdio_adma_addr: the default address to set for the ADMA in SDIO mode until
 *	we get the ALIVE from the uCode
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
	bool wide_cmd_header;
	bool sw_csum_tx;
	const struct iwl_hcmd_arr *command_groups;
	int command_groups_size;

	u32 sdio_adma_addr;
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
 * struct iwl_trans_ops - transport specific operations
 *
 * All the handlers MUST be implemented
 *
 * @start_hw: starts the HW. If low_power is true, the NIC needs to be taken
 *	out of a low power state. From that point on, the HW can send
 *	interrupts. May sleep.
 * @op_mode_leave: Turn off the HW RF kill indication if on
 *	May sleep
 * @start_fw: allocates and inits all the resources for the transport
 *	layer. Also kick a fw image.
 *	May sleep
 * @fw_alive: called when the fw sends alive notification. If the fw provides
 *	the SCD base address in SRAM, then provide it here, or 0 otherwise.
 *	May sleep
 * @stop_device: stops the whole device (embedded CPU put to reset) and stops
 *	the HW. If low_power is true, the NIC will be put in low power state.
 *	From that point on, the HW will be stopped but will still issue an
 *	interrupt if the HW RF kill switch is triggered.
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
 *	configured. May sleep.
 * @txq_disable: de-configure a Tx queue to send AMPDUs
 *	Must be atomic
 * @wait_tx_queue_empty: wait until tx queues are empty. May sleep.
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
 * @ref: grab a reference to the transport/FW layers, disallowing
 *	certain low power states
 * @unref: release a reference previously taken with @ref. Note that
 *	initially the reference count is 1, making an initial @unref
 *	necessary to allow low power states.
 * @dump_data: return a vmalloc'ed buffer with debug data, maybe containing last
 *	TX'ed commands and similar. The buffer will be vfree'd by the caller.
 *	Note that the transport must fill in the proper file headers.
 */
struct iwl_trans_ops {

	int (*start_hw)(struct iwl_trans *iwl_trans, bool low_power);
	void (*op_mode_leave)(struct iwl_trans *iwl_trans);
	int (*start_fw)(struct iwl_trans *trans, const struct fw_img *fw,
			bool run_in_rfkill);
	int (*update_sf)(struct iwl_trans *trans,
			 struct iwl_sf_region *st_fwrd_space);
	void (*fw_alive)(struct iwl_trans *trans, u32 scd_addr);
	void (*stop_device)(struct iwl_trans *trans, bool low_power);

	void (*d3_suspend)(struct iwl_trans *trans, bool test, bool reset);
	int (*d3_resume)(struct iwl_trans *trans, enum iwl_d3_status *status,
			 bool test, bool reset);

	int (*send_cmd)(struct iwl_trans *trans, struct iwl_host_cmd *cmd);

	int (*tx)(struct iwl_trans *trans, struct sk_buff *skb,
		  struct iwl_device_cmd *dev_cmd, int queue);
	void (*reclaim)(struct iwl_trans *trans, int queue, int ssn,
			struct sk_buff_head *skbs);

	void (*txq_enable)(struct iwl_trans *trans, int queue, u16 ssn,
			   const struct iwl_trans_txq_scd_cfg *cfg,
			   unsigned int queue_wdg_timeout);
	void (*txq_disable)(struct iwl_trans *trans, int queue,
			    bool configure_scd);

	int (*wait_tx_queue_empty)(struct iwl_trans *trans, u32 txq_bm);
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
	void (*configure)(struct iwl_trans *trans,
			  const struct iwl_trans_config *trans_cfg);
	void (*set_pmi)(struct iwl_trans *trans, bool state);
	bool (*grab_nic_access)(struct iwl_trans *trans, unsigned long *flags);
	void (*release_nic_access)(struct iwl_trans *trans,
				   unsigned long *flags);
	void (*set_bits_mask)(struct iwl_trans *trans, u32 reg, u32 mask,
			      u32 value);
	void (*ref)(struct iwl_trans *trans);
	void (*unref)(struct iwl_trans *trans);
	int  (*suspend)(struct iwl_trans *trans);
	void (*resume)(struct iwl_trans *trans);

	struct iwl_trans_dump_data *(*dump_data)(struct iwl_trans *trans,
						 const struct iwl_fw_dbg_trigger_tlv
						 *trigger);
};

/**
 * enum iwl_trans_state - state of the transport layer
 *
 * @IWL_TRANS_NO_FW: no fw has sent an alive response
 * @IWL_TRANS_FW_ALIVE: a fw has sent an alive response
 */
enum iwl_trans_state {
	IWL_TRANS_NO_FW = 0,
	IWL_TRANS_FW_ALIVE	= 1,
};

/**
 * DOC: Platform power management
 *
 * There are two types of platform power management: system-wide
 * (WoWLAN) and runtime.
 *
 * In system-wide power management the entire platform goes into a low
 * power state (e.g. idle or suspend to RAM) at the same time and the
 * device is configured as a wakeup source for the entire platform.
 * This is usually triggered by userspace activity (e.g. the user
 * presses the suspend button or a power management daemon decides to
 * put the platform in low power mode).  The device's behavior in this
 * mode is dictated by the wake-on-WLAN configuration.
 *
 * In runtime power management, only the devices which are themselves
 * idle enter a low power state.  This is done at runtime, which means
 * that the entire system is still running normally.  This mode is
 * usually triggered automatically by the device driver and requires
 * the ability to enter and exit the low power modes in a very short
 * time, so there is not much impact in usability.
 *
 * The terms used for the device's behavior are as follows:
 *
 *	- D0: the device is fully powered and the host is awake;
 *	- D3: the device is in low power mode and only reacts to
 *		specific events (e.g. magic-packet received or scan
 *		results found);
 *	- D0I3: the device is in low power mode and reacts to any
 *		activity (e.g. RX);
 *
 * These terms reflect the power modes in the firmware and are not to
 * be confused with the physical device power state.  The NIC can be
 * in D0I3 mode even if, for instance, the PCI device is in D3 state.
 */

/**
 * enum iwl_plat_pm_mode - platform power management mode
 *
 * This enumeration describes the device's platform power management
 * behavior when in idle mode (i.e. runtime power management) or when
 * in system-wide suspend (i.e WoWLAN).
 *
 * @IWL_PLAT_PM_MODE_DISABLED: power management is disabled for this
 *	device.  At runtime, this means that nothing happens and the
 *	device always remains in active.  In system-wide suspend mode,
 *	it means that the all connections will be closed automatically
 *	by mac80211 before the platform is suspended.
 * @IWL_PLAT_PM_MODE_D3: the device goes into D3 mode (i.e. WoWLAN).
 *	For runtime power management, this mode is not officially
 *	supported.
 * @IWL_PLAT_PM_MODE_D0I3: the device goes into D0I3 mode.
 */
enum iwl_plat_pm_mode {
	IWL_PLAT_PM_MODE_DISABLED,
	IWL_PLAT_PM_MODE_D3,
	IWL_PLAT_PM_MODE_D0I3,
};

/* Max time to wait for trans to become idle/non-idle on d0i3
 * enter/exit (in msecs).
 */
#define IWL_TRANS_IDLE_TIMEOUT 2000

/**
 * struct iwl_trans - transport common data
 *
 * @ops - pointer to iwl_trans_ops
 * @op_mode - pointer to the op_mode
 * @cfg - pointer to the configuration
 * @status: a bit-mask of transport status flags
 * @dev - pointer to struct device * that represents the device
 * @max_skb_frags: maximum number of fragments an SKB can have when transmitted.
 *	0 indicates that frag SKBs (NETIF_F_SG) aren't supported.
 * @hw_id: a u32 with the ID of the device / sub-device.
 *	Set during transport allocation.
 * @hw_id_str: a string with info about HW ID. Set during transport allocation.
 * @pm_support: set to true in start_hw if link pm is supported
 * @ltr_enabled: set to true if the LTR is enabled
 * @num_rx_queues: number of RX queues allocated by the transport;
 *	the transport must set this before calling iwl_drv_start()
 * @dev_cmd_pool: pool for Tx cmd allocation - for internal use only.
 *	The user should use iwl_trans_{alloc,free}_tx_cmd.
 * @dev_cmd_headroom: room needed for the transport's private use before the
 *	device_cmd for Tx - for internal use only
 *	The user should use iwl_trans_{alloc,free}_tx_cmd.
 * @rx_mpdu_cmd: MPDU RX command ID, must be assigned by opmode before
 *	starting the firmware, used for tracing
 * @rx_mpdu_cmd_hdr_size: used for tracing, amount of data before the
 *	start of the 802.11 header in the @rx_mpdu_cmd
 * @dflt_pwr_limit: default power limit fetched from the platform (ACPI)
 * @dbg_dest_tlv: points to the destination TLV for debug
 * @dbg_conf_tlv: array of pointers to configuration TLVs for debug
 * @dbg_trigger_tlv: array of pointers to triggers TLVs for debug
 * @dbg_dest_reg_num: num of reg_ops in %dbg_dest_tlv
 * @paging_req_addr: The location were the FW will upload / download the pages
 *	from. The address is set by the opmode
 * @paging_db: Pointer to the opmode paging data base, the pointer is set by
 *	the opmode.
 * @paging_download_buf: Buffer used for copying all of the pages before
 *	downloading them to the FW. The buffer is allocated in the opmode
 * @system_pm_mode: the system-wide power management mode in use.
 *	This mode is set dynamically, depending on the WoWLAN values
 *	configured from the userspace at runtime.
 * @runtime_pm_mode: the runtime power management mode in use.  This
 *	mode is set during the initialization phase and is not
 *	supposed to change during runtime.
 */
struct iwl_trans {
	const struct iwl_trans_ops *ops;
	struct iwl_op_mode *op_mode;
	const struct iwl_cfg *cfg;
	enum iwl_trans_state state;
	unsigned long status;

	struct device *dev;
	u32 max_skb_frags;
	u32 hw_rev;
	u32 hw_id;
	char hw_id_str[52];

	u8 rx_mpdu_cmd, rx_mpdu_cmd_hdr_size;

	bool pm_support;
	bool ltr_enabled;

	const struct iwl_hcmd_arr *command_groups;
	int command_groups_size;

	u8 num_rx_queues;

	/* The following fields are internal only */
	struct kmem_cache *dev_cmd_pool;
	size_t dev_cmd_headroom;
	char dev_cmd_pool_name[50];

	struct dentry *dbgfs_dir;

#ifdef CONFIG_LOCKDEP
	struct lockdep_map sync_cmd_lockdep_map;
#endif

	u64 dflt_pwr_limit;

	const struct iwl_fw_dbg_dest_tlv *dbg_dest_tlv;
	const struct iwl_fw_dbg_conf_tlv *dbg_conf_tlv[FW_DBG_CONF_MAX];
	struct iwl_fw_dbg_trigger_tlv * const *dbg_trigger_tlv;
	u8 dbg_dest_reg_num;

	/*
	 * Paging parameters - All of the parameters should be set by the
	 * opmode when paging is enabled
	 */
	u32 paging_req_addr;
	struct iwl_fw_paging *paging_db;
	void *paging_download_buf;

	enum iwl_plat_pm_mode system_pm_mode;
	enum iwl_plat_pm_mode runtime_pm_mode;
	bool suspending;

	/* pointer to trans specific struct */
	/*Ensure that this pointer will always be aligned to sizeof pointer */
	char trans_specific[0] __aligned(sizeof(void *));
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

static inline int _iwl_trans_start_hw(struct iwl_trans *trans, bool low_power)
{
	might_sleep();

	return trans->ops->start_hw(trans, low_power);
}

static inline int iwl_trans_start_hw(struct iwl_trans *trans)
{
	return trans->ops->start_hw(trans, true);
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
	might_sleep();

	WARN_ON_ONCE(!trans->rx_mpdu_cmd);

	clear_bit(STATUS_FW_ERROR, &trans->status);
	return trans->ops->start_fw(trans, fw, run_in_rfkill);
}

static inline int iwl_trans_update_sf(struct iwl_trans *trans,
				      struct iwl_sf_region *st_fwrd_space)
{
	might_sleep();

	if (trans->ops->update_sf)
		return trans->ops->update_sf(trans, st_fwrd_space);

	return 0;
}

static inline void _iwl_trans_stop_device(struct iwl_trans *trans,
					  bool low_power)
{
	might_sleep();

	trans->ops->stop_device(trans, low_power);

	trans->state = IWL_TRANS_NO_FW;
}

static inline void iwl_trans_stop_device(struct iwl_trans *trans)
{
	_iwl_trans_stop_device(trans, true);
}

static inline void iwl_trans_d3_suspend(struct iwl_trans *trans, bool test,
					bool reset)
{
	might_sleep();
	if (trans->ops->d3_suspend)
		trans->ops->d3_suspend(trans, test, reset);
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

static inline void iwl_trans_ref(struct iwl_trans *trans)
{
	if (trans->ops->ref)
		trans->ops->ref(trans);
}

static inline void iwl_trans_unref(struct iwl_trans *trans)
{
	if (trans->ops->unref)
		trans->ops->unref(trans);
}

static inline int iwl_trans_suspend(struct iwl_trans *trans)
{
	if (!trans->ops->suspend)
		return 0;

	return trans->ops->suspend(trans);
}

static inline void iwl_trans_resume(struct iwl_trans *trans)
{
	if (trans->ops->resume)
		trans->ops->resume(trans);
}

static inline struct iwl_trans_dump_data *
iwl_trans_dump_data(struct iwl_trans *trans,
		    const struct iwl_fw_dbg_trigger_tlv *trigger)
{
	if (!trans->ops->dump_data)
		return NULL;
	return trans->ops->dump_data(trans, trigger);
}

static inline struct iwl_device_cmd *
iwl_trans_alloc_tx_cmd(struct iwl_trans *trans)
{
	u8 *dev_cmd_ptr = kmem_cache_alloc(trans->dev_cmd_pool, GFP_ATOMIC);

	if (unlikely(dev_cmd_ptr == NULL))
		return NULL;

	return (struct iwl_device_cmd *)
			(dev_cmd_ptr + trans->dev_cmd_headroom);
}

int iwl_trans_send_cmd(struct iwl_trans *trans, struct iwl_host_cmd *cmd);

static inline void iwl_trans_free_tx_cmd(struct iwl_trans *trans,
					 struct iwl_device_cmd *dev_cmd)
{
	u8 *dev_cmd_ptr = (u8 *)dev_cmd - trans->dev_cmd_headroom;

	kmem_cache_free(trans->dev_cmd_pool, dev_cmd_ptr);
}

static inline int iwl_trans_tx(struct iwl_trans *trans, struct sk_buff *skb,
			       struct iwl_device_cmd *dev_cmd, int queue)
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
				     int ssn, struct sk_buff_head *skbs)
{
	if (WARN_ON_ONCE(trans->state != IWL_TRANS_FW_ALIVE)) {
		IWL_ERR(trans, "%s bad state = %d\n", __func__, trans->state);
		return;
	}

	trans->ops->reclaim(trans, queue, ssn, skbs);
}

static inline void iwl_trans_txq_disable(struct iwl_trans *trans, int queue,
					 bool configure_scd)
{
	trans->ops->txq_disable(trans, queue, configure_scd);
}

static inline void
iwl_trans_txq_enable_cfg(struct iwl_trans *trans, int queue, u16 ssn,
			 const struct iwl_trans_txq_scd_cfg *cfg,
			 unsigned int queue_wdg_timeout)
{
	might_sleep();

	if (WARN_ON_ONCE(trans->state != IWL_TRANS_FW_ALIVE)) {
		IWL_ERR(trans, "%s bad state = %d\n", __func__, trans->state);
		return;
	}

	trans->ops->txq_enable(trans, queue, ssn, cfg, queue_wdg_timeout);
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

static inline int iwl_trans_wait_tx_queue_empty(struct iwl_trans *trans,
						u32 txqs)
{
	if (WARN_ON_ONCE(trans->state != IWL_TRANS_FW_ALIVE)) {
		IWL_ERR(trans, "%s bad state = %d\n", __func__, trans->state);
		return -EIO;
	}

	return trans->ops->wait_tx_queue_empty(trans, txqs);
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

static inline void
iwl_trans_set_bits_mask(struct iwl_trans *trans, u32 reg, u32 mask, u32 value)
{
	trans->ops->set_bits_mask(trans, reg, mask, value);
}

#define iwl_trans_grab_nic_access(trans, flags)	\
	__cond_lock(nic_access,				\
		    likely((trans)->ops->grab_nic_access(trans, flags)))

static inline void __releases(nic_access)
iwl_trans_release_nic_access(struct iwl_trans *trans, unsigned long *flags)
{
	trans->ops->release_nic_access(trans, flags);
	__release(nic_access);
}

static inline void iwl_trans_fw_error(struct iwl_trans *trans)
{
	if (WARN_ON_ONCE(!trans->op_mode))
		return;

	/* prevent double restarts due to the same erroneous FW */
	if (!test_and_set_bit(STATUS_FW_ERROR, &trans->status))
		iwl_op_mode_nic_error(trans->op_mode);
}

/*****************************************************
 * transport helper functions
 *****************************************************/
struct iwl_trans *iwl_trans_alloc(unsigned int priv_size,
				  struct device *dev,
				  const struct iwl_cfg *cfg,
				  const struct iwl_trans_ops *ops,
				  size_t dev_cmd_headroom);
void iwl_trans_free(struct iwl_trans *trans);

/*****************************************************
* driver (transport) register/unregister functions
******************************************************/
int __must_check iwl_pci_register_driver(void);
void iwl_pci_unregister_driver(void);

#endif /* __iwl_trans_h__ */
