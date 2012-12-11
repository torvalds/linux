/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2012 Intel Corporation. All rights reserved.
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
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2012 Intel Corporation. All rights reserved.
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

#include "iwl-debug.h"
#include "iwl-config.h"
#include "iwl-fw.h"

/**
 * DOC: Transport layer - what is it ?
 *
 * The tranport layer is the layer that deals with the HW directly. It provides
 * an abstraction of the underlying HW to the upper layer. The transport layer
 * doesn't provide any policy, algorithm or anything of this kind, but only
 * mechanisms to make the HW do something.It is not completely stateless but
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
 *	   stop_fw (a.k.a. stop device for the moment)
 *	   stop_hw
 *
 *	6) Eventually, the free function will be called.
 */

/**
 * DOC: Host command section
 *
 * A host command is a commaned issued by the upper layer to the fw. There are
 * several versions of fw that have several APIs. The transport layer is
 * completely agnostic to these differences.
 * The transport does provide helper functionnality (i.e. SYNC / ASYNC mode),
 */
#define SEQ_TO_SN(seq) (((seq) & IEEE80211_SCTL_SEQ) >> 4)
#define SN_TO_SEQ(ssn) (((ssn) << 4) & IEEE80211_SCTL_SEQ)
#define MAX_SN ((IEEE80211_SCTL_SEQ) >> 4)
#define SEQ_TO_QUEUE(s)	(((s) >> 8) & 0x1f)
#define QUEUE_TO_SEQ(q)	(((q) & 0x1f) << 8)
#define SEQ_TO_INDEX(s)	((s) & 0xff)
#define INDEX_TO_SEQ(i)	((i) & 0xff)
#define SEQ_RX_FRAME	cpu_to_le16(0x8000)

/**
 * struct iwl_cmd_header
 *
 * This header format appears in the beginning of each command sent from the
 * driver, and each response/notification received from uCode.
 */
struct iwl_cmd_header {
	u8 cmd;		/* Command ID:  REPLY_RXON, etc. */
	u8 flags;	/* 0:5 reserved, 6 abort, 7 internal */
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

/* iwl_cmd_header flags value */
#define IWL_CMD_FAILED_MSK 0x40


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

/**
 * enum CMD_MODE - how to send the host commands ?
 *
 * @CMD_SYNC: The caller will be stalled until the fw responds to the command
 * @CMD_ASYNC: Return right away and don't want for the response
 * @CMD_WANT_SKB: valid only with CMD_SYNC. The caller needs the buffer of the
 *	response. The caller needs to call iwl_free_resp when done.
 * @CMD_WANT_HCMD: The caller needs to get the HCMD that was sent in the
 *	response handler. Chunks flagged by %IWL_HCMD_DFL_NOCOPY won't be
 *	copied. The pointer passed to the response handler is in the transport
 *	ownership and don't need to be freed by the op_mode. This also means
 *	that the pointer is invalidated after the op_mode's handler returns.
 * @CMD_ON_DEMAND: This command is sent by the test mode pipe.
 */
enum CMD_MODE {
	CMD_SYNC = 0,
	CMD_ASYNC = BIT(0),
	CMD_WANT_SKB = BIT(1),
	CMD_WANT_HCMD = BIT(2),
	CMD_ON_DEMAND = BIT(3),
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
	struct iwl_cmd_header hdr;	/* uCode API */
	u8 payload[DEF_CMD_PAYLOAD_SIZE];
} __packed;

#define TFD_MAX_PAYLOAD_SIZE (sizeof(struct iwl_device_cmd))

#define IWL_MAX_CMD_TFDS	2

/**
 * struct iwl_hcmd_dataflag - flag for each one of the chunks of the command
 *
 * @IWL_HCMD_DFL_NOCOPY: By default, the command is copied to the host command's
 *	ring. The transport layer doesn't map the command's buffer to DMA, but
 *	rather copies it to an previously allocated DMA buffer. This flag tells
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
 * @handler_status: return value of the handler of the command
 *	(put in setup_rx_handlers) - valid for SYNC mode only
 * @flags: can be CMD_*
 * @len: array of the lenths of the chunks in data
 * @dataflags: IWL_HCMD_DFL_*
 * @id: id of the host command
 */
struct iwl_host_cmd {
	const void *data[IWL_MAX_CMD_TFDS];
	struct iwl_rx_packet *resp_pkt;
	unsigned long _rx_page_addr;
	u32 _rx_page_order;
	int handler_status;

	u32 flags;
	u16 len[IWL_MAX_CMD_TFDS];
	u8 dataflags[IWL_MAX_CMD_TFDS];
	u8 id;
};

static inline void iwl_free_resp(struct iwl_host_cmd *cmd)
{
	free_pages(cmd->_rx_page_addr, cmd->_rx_page_order);
}

struct iwl_rx_cmd_buffer {
	struct page *_page;
	int _offset;
	bool _page_stolen;
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

#define MAX_NO_RECLAIM_CMDS	6

#define IWL_MASK(lo, hi) ((1 << (hi)) | ((1 << (hi)) - (1 << (lo))))

/*
 * Maximum number of HW queues the transport layer
 * currently supports
 */
#define IWL_MAX_HW_QUEUES		32
#define IWL_INVALID_STATION	255
#define IWL_MAX_TID_COUNT	8
#define IWL_FRAME_LIMIT	64

/**
 * struct iwl_trans_config - transport configuration
 *
 * @op_mode: pointer to the upper layer.
 * @cmd_queue: the index of the command queue.
 *	Must be set before start_fw.
 * @cmd_fifo: the fifo for host commands
 * @no_reclaim_cmds: Some devices erroneously don't set the
 *	SEQ_RX_FRAME bit on some notifications, this is the
 *	list of such notifications to filter. Max length is
 *	%MAX_NO_RECLAIM_CMDS.
 * @n_no_reclaim_cmds: # of commands in list
 * @rx_buf_size_8k: 8 kB RX buffer size needed for A-MSDUs,
 *	if unset 4k will be the RX buffer size
 * @queue_watchdog_timeout: time (in ms) after which queues
 *	are considered stuck and will trigger device restart
 * @command_names: array of command names, must be 256 entries
 *	(one for each command); for debugging only
 */
struct iwl_trans_config {
	struct iwl_op_mode *op_mode;

	u8 cmd_queue;
	u8 cmd_fifo;
	const u8 *no_reclaim_cmds;
	int n_no_reclaim_cmds;

	bool rx_buf_size_8k;
	unsigned int queue_watchdog_timeout;
	const char **command_names;
};

struct iwl_trans;

/**
 * struct iwl_trans_ops - transport specific operations
 *
 * All the handlers MUST be implemented
 *
 * @start_hw: starts the HW- from that point on, the HW can send interrupts
 *	May sleep
 * @stop_hw: stops the HW- from that point on, the HW will be in low power but
 *	will still issue interrupt if the HW RF kill is triggered unless
 *	op_mode_leaving is true.
 *	May sleep
 * @start_fw: allocates and inits all the resources for the transport
 *	layer. Also kick a fw image.
 *	May sleep
 * @fw_alive: called when the fw sends alive notification. If the fw provides
 *	the SCD base address in SRAM, then provide it here, or 0 otherwise.
 *	May sleep
 * @stop_device:stops the whole device (embedded CPU put to reset)
 *	May sleep
 * @wowlan_suspend: put the device into the correct mode for WoWLAN during
 *	suspend. This is optional, if not implemented WoWLAN will not be
 *	supported. This callback may sleep.
 * @send_cmd:send a host command. Must return -ERFKILL if RFkill is asserted.
 *	If RFkill is asserted in the middle of a SYNC host command, it must
 *	return -ERFKILL straight away.
 *	May sleep only if CMD_SYNC is set
 * @tx: send an skb
 *	Must be atomic
 * @reclaim: free packet until ssn. Returns a list of freed packets.
 *	Must be atomic
 * @txq_enable: setup a queue. To setup an AC queue, use the
 *	iwl_trans_ac_txq_enable wrapper. fw_alive must have been called before
 *	this one. The op_mode must not configure the HCMD queue. May sleep.
 * @txq_disable: de-configure a Tx queue to send AMPDUs
 *	Must be atomic
 * @wait_tx_queue_empty: wait until all tx queues are empty
 *	May sleep
 * @dbgfs_register: add the dbgfs files under this directory. Files will be
 *	automatically deleted.
 * @suspend: stop the device unless WoWLAN is configured
 * @resume: resume activity of the device
 * @write8: write a u8 to a register at offset ofs from the BAR
 * @write32: write a u32 to a register at offset ofs from the BAR
 * @read32: read a u32 register at offset ofs from the BAR
 * @read_prph: read a DWORD from a periphery register
 * @write_prph: write a DWORD to a periphery register
 * @configure: configure parameters required by the transport layer from
 *	the op_mode. May be called several times before start_fw, can't be
 *	called after that.
 * @set_pmi: set the power pmi state
 */
struct iwl_trans_ops {

	int (*start_hw)(struct iwl_trans *iwl_trans);
	void (*stop_hw)(struct iwl_trans *iwl_trans, bool op_mode_leaving);
	int (*start_fw)(struct iwl_trans *trans, const struct fw_img *fw);
	void (*fw_alive)(struct iwl_trans *trans, u32 scd_addr);
	void (*stop_device)(struct iwl_trans *trans);

	void (*wowlan_suspend)(struct iwl_trans *trans);

	int (*send_cmd)(struct iwl_trans *trans, struct iwl_host_cmd *cmd);

	int (*tx)(struct iwl_trans *trans, struct sk_buff *skb,
		  struct iwl_device_cmd *dev_cmd, int queue);
	void (*reclaim)(struct iwl_trans *trans, int queue, int ssn,
			struct sk_buff_head *skbs);

	void (*txq_enable)(struct iwl_trans *trans, int queue, int fifo,
			   int sta_id, int tid, int frame_limit, u16 ssn);
	void (*txq_disable)(struct iwl_trans *trans, int queue);

	int (*dbgfs_register)(struct iwl_trans *trans, struct dentry* dir);
	int (*wait_tx_queue_empty)(struct iwl_trans *trans);
#ifdef CONFIG_PM_SLEEP
	int (*suspend)(struct iwl_trans *trans);
	int (*resume)(struct iwl_trans *trans);
#endif
	void (*write8)(struct iwl_trans *trans, u32 ofs, u8 val);
	void (*write32)(struct iwl_trans *trans, u32 ofs, u32 val);
	u32 (*read32)(struct iwl_trans *trans, u32 ofs);
	u32 (*read_prph)(struct iwl_trans *trans, u32 ofs);
	void (*write_prph)(struct iwl_trans *trans, u32 ofs, u32 val);
	void (*configure)(struct iwl_trans *trans,
			  const struct iwl_trans_config *trans_cfg);
	void (*set_pmi)(struct iwl_trans *trans, bool state);
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
 * struct iwl_trans - transport common data
 *
 * @ops - pointer to iwl_trans_ops
 * @op_mode - pointer to the op_mode
 * @cfg - pointer to the configuration
 * @reg_lock - protect hw register access
 * @dev - pointer to struct device * that represents the device
 * @hw_id: a u32 with the ID of the device / subdevice.
 *	Set during transport allocation.
 * @hw_id_str: a string with info about HW ID. Set during transport allocation.
 * @pm_support: set to true in start_hw if link pm is supported
 * @dev_cmd_pool: pool for Tx cmd allocation - for internal use only.
 *	The user should use iwl_trans_{alloc,free}_tx_cmd.
 * @dev_cmd_headroom: room needed for the transport's private use before the
 *	device_cmd for Tx - for internal use only
 *	The user should use iwl_trans_{alloc,free}_tx_cmd.
 * @rx_mpdu_cmd: MPDU RX command ID, must be assigned by opmode before
 *	starting the firmware, used for tracing
 * @rx_mpdu_cmd_hdr_size: used for tracing, amount of data before the
 *	start of the 802.11 header in the @rx_mpdu_cmd
 */
struct iwl_trans {
	const struct iwl_trans_ops *ops;
	struct iwl_op_mode *op_mode;
	const struct iwl_cfg *cfg;
	enum iwl_trans_state state;
	spinlock_t reg_lock;

	struct device *dev;
	u32 hw_rev;
	u32 hw_id;
	char hw_id_str[52];

	u8 rx_mpdu_cmd, rx_mpdu_cmd_hdr_size;

	bool pm_support;

	/* The following fields are internal only */
	struct kmem_cache *dev_cmd_pool;
	size_t dev_cmd_headroom;
	char dev_cmd_pool_name[50];

	struct dentry *dbgfs_dir;

	/* pointer to trans specific struct */
	/*Ensure that this pointer will always be aligned to sizeof pointer */
	char trans_specific[0] __aligned(sizeof(void *));
};

static inline void iwl_trans_configure(struct iwl_trans *trans,
				       const struct iwl_trans_config *trans_cfg)
{
	trans->op_mode = trans_cfg->op_mode;

	trans->ops->configure(trans, trans_cfg);
}

static inline int iwl_trans_start_hw(struct iwl_trans *trans)
{
	might_sleep();

	return trans->ops->start_hw(trans);
}

static inline void iwl_trans_stop_hw(struct iwl_trans *trans,
				     bool op_mode_leaving)
{
	might_sleep();

	trans->ops->stop_hw(trans, op_mode_leaving);

	if (op_mode_leaving)
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
				     const struct fw_img *fw)
{
	might_sleep();

	WARN_ON_ONCE(!trans->rx_mpdu_cmd);

	return trans->ops->start_fw(trans, fw);
}

static inline void iwl_trans_stop_device(struct iwl_trans *trans)
{
	might_sleep();

	trans->ops->stop_device(trans);

	trans->state = IWL_TRANS_NO_FW;
}

static inline void iwl_trans_wowlan_suspend(struct iwl_trans *trans)
{
	might_sleep();
	trans->ops->wowlan_suspend(trans);
}

static inline int iwl_trans_send_cmd(struct iwl_trans *trans,
				struct iwl_host_cmd *cmd)
{
	WARN_ONCE(trans->state != IWL_TRANS_FW_ALIVE,
		  "%s bad state = %d", __func__, trans->state);

	return trans->ops->send_cmd(trans, cmd);
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

static inline void iwl_trans_free_tx_cmd(struct iwl_trans *trans,
					 struct iwl_device_cmd *dev_cmd)
{
	u8 *dev_cmd_ptr = (u8 *)dev_cmd - trans->dev_cmd_headroom;

	kmem_cache_free(trans->dev_cmd_pool, dev_cmd_ptr);
}

static inline int iwl_trans_tx(struct iwl_trans *trans, struct sk_buff *skb,
			       struct iwl_device_cmd *dev_cmd, int queue)
{
	WARN_ONCE(trans->state != IWL_TRANS_FW_ALIVE,
		  "%s bad state = %d", __func__, trans->state);

	return trans->ops->tx(trans, skb, dev_cmd, queue);
}

static inline void iwl_trans_reclaim(struct iwl_trans *trans, int queue,
				     int ssn, struct sk_buff_head *skbs)
{
	WARN_ONCE(trans->state != IWL_TRANS_FW_ALIVE,
		  "%s bad state = %d", __func__, trans->state);

	trans->ops->reclaim(trans, queue, ssn, skbs);
}

static inline void iwl_trans_txq_disable(struct iwl_trans *trans, int queue)
{
	WARN_ONCE(trans->state != IWL_TRANS_FW_ALIVE,
		  "%s bad state = %d", __func__, trans->state);

	trans->ops->txq_disable(trans, queue);
}

static inline void iwl_trans_txq_enable(struct iwl_trans *trans, int queue,
					int fifo, int sta_id, int tid,
					int frame_limit, u16 ssn)
{
	might_sleep();

	WARN_ONCE(trans->state != IWL_TRANS_FW_ALIVE,
		  "%s bad state = %d", __func__, trans->state);

	trans->ops->txq_enable(trans, queue, fifo, sta_id, tid,
				 frame_limit, ssn);
}

static inline void iwl_trans_ac_txq_enable(struct iwl_trans *trans, int queue,
					   int fifo)
{
	iwl_trans_txq_enable(trans, queue, fifo, IWL_INVALID_STATION,
			     IWL_MAX_TID_COUNT, IWL_FRAME_LIMIT, 0);
}

static inline int iwl_trans_wait_tx_queue_empty(struct iwl_trans *trans)
{
	WARN_ONCE(trans->state != IWL_TRANS_FW_ALIVE,
		  "%s bad state = %d", __func__, trans->state);

	return trans->ops->wait_tx_queue_empty(trans);
}

static inline int iwl_trans_dbgfs_register(struct iwl_trans *trans,
					    struct dentry *dir)
{
	return trans->ops->dbgfs_register(trans, dir);
}

#ifdef CONFIG_PM_SLEEP
static inline int iwl_trans_suspend(struct iwl_trans *trans)
{
	return trans->ops->suspend(trans);
}

static inline int iwl_trans_resume(struct iwl_trans *trans)
{
	return trans->ops->resume(trans);
}
#endif

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

static inline void iwl_trans_set_pmi(struct iwl_trans *trans, bool state)
{
	trans->ops->set_pmi(trans, state);
}

/*****************************************************
* driver (transport) register/unregister functions
******************************************************/
int __must_check iwl_pci_register_driver(void);
void iwl_pci_unregister_driver(void);

#endif /* __iwl_trans_h__ */
