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

#include "iwl-shared.h"
#include "iwl-debug.h"

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

struct iwl_priv;
struct iwl_shared;
struct iwl_op_mode;
struct fw_img;
struct sk_buff;
struct dentry;

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


#define FH_RSCSR_FRAME_SIZE_MSK		0x00003FFF	/* bits 0-13 */

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
 *	response.
 * @CMD_ON_DEMAND: This command is sent by the test mode pipe.
 */
enum CMD_MODE {
	CMD_SYNC = 0,
	CMD_ASYNC = BIT(0),
	CMD_WANT_SKB = BIT(1),
	CMD_ON_DEMAND = BIT(2),
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
 * IWL_HCMD_DFL_NOCOPY: By default, the command is copied to the host command's
 *	ring. The transport layer doesn't map the command's buffer to DMA, but
 *	rather copies it to an previously allocated DMA buffer. This flag tells
 *	the transport layer not to copy the command, but to map the existing
 *	buffer. This can save memcpy and is worth with very big comamnds.
 */
enum iwl_hcmd_dataflag {
	IWL_HCMD_DFL_NOCOPY	= BIT(0),
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
};

static inline void *rxb_addr(struct iwl_rx_cmd_buffer *r)
{
	return page_address(r->_page);
}

static inline struct page *rxb_steal_page(struct iwl_rx_cmd_buffer *r)
{
	struct page *p = r->_page;
	r->_page = NULL;
	return p;
}

#define MAX_NO_RECLAIM_CMDS	6

/**
 * struct iwl_trans_config - transport configuration
 *
 * @op_mode: pointer to the upper layer.
 *	Must be set before any other call.
 * @cmd_queue: the index of the command queue.
 *	Must be set before start_fw.
 * @no_reclaim_cmds: Some devices erroneously don't set the
 *	SEQ_RX_FRAME bit on some notifications, this is the
 *	list of such notifications to filter. Max length is
 *	%MAX_NO_RECLAIM_CMDS.
 * @n_no_reclaim_cmds: # of commands in list
 */
struct iwl_trans_config {
	struct iwl_op_mode *op_mode;
	u8 cmd_queue;
	const u8 *no_reclaim_cmds;
	int n_no_reclaim_cmds;
};

/**
 * struct iwl_trans_ops - transport specific operations
 *
 * All the handlers MUST be implemented
 *
 * @start_hw: starts the HW- from that point on, the HW can send interrupts
 *	May sleep
 * @stop_hw: stops the HW- from that point on, the HW will be in low power but
 *	will still issue interrupt if the HW RF kill is triggered.
 *	May sleep
 * @start_fw: allocates and inits all the resources for the transport
 *	layer. Also kick a fw image.
 *	May sleep
 * @fw_alive: called when the fw sends alive notification
 *	May sleep
 * @stop_device:stops the whole device (embedded CPU put to reset)
 *	May sleep
 * @wowlan_suspend: put the device into the correct mode for WoWLAN during
 *	suspend. This is optional, if not implemented WoWLAN will not be
 *	supported. This callback may sleep.
 * @send_cmd:send a host command
 *	May sleep only if CMD_SYNC is set
 * @tx: send an skb
 *	Must be atomic
 * @reclaim: free packet until ssn. Returns a list of freed packets.
 *	Must be atomic
 * @tx_agg_alloc: allocate resources for a TX BA session
 *	Must be atomic
 * @tx_agg_setup: setup a tx queue for AMPDU - will be called once the HW is
 *	ready and a successful ADDBA response has been received.
 *	May sleep
 * @tx_agg_disable: de-configure a Tx queue to send AMPDUs
 *	Must be atomic
 * @free: release all the ressource for the transport layer itself such as
 *	irq, tasklet etc... From this point on, the device may not issue
 *	any interrupt (incl. RFKILL).
 *	May sleep
 * @check_stuck_queue: check if a specific queue is stuck
 * @wait_tx_queue_empty: wait until all tx queues are empty
 *	May sleep
 * @dbgfs_register: add the dbgfs files under this directory. Files will be
 *	automatically deleted.
 * @suspend: stop the device unless WoWLAN is configured
 * @resume: resume activity of the device
 * @write8: write a u8 to a register at offset ofs from the BAR
 * @write32: write a u32 to a register at offset ofs from the BAR
 * @read32: read a u32 register at offset ofs from the BAR
 * @configure: configure parameters required by the transport layer from
 *	the op_mode. May be called several times before start_fw, can't be
 *	called after that.
 */
struct iwl_trans_ops {

	int (*start_hw)(struct iwl_trans *iwl_trans);
	void (*stop_hw)(struct iwl_trans *iwl_trans);
	int (*start_fw)(struct iwl_trans *trans, const struct fw_img *fw);
	void (*fw_alive)(struct iwl_trans *trans);
	void (*stop_device)(struct iwl_trans *trans);

	void (*wowlan_suspend)(struct iwl_trans *trans);

	int (*send_cmd)(struct iwl_trans *trans, struct iwl_host_cmd *cmd);

	int (*tx)(struct iwl_trans *trans, struct sk_buff *skb,
		struct iwl_device_cmd *dev_cmd, enum iwl_rxon_context_id ctx,
		u8 sta_id, u8 tid);
	int (*reclaim)(struct iwl_trans *trans, int sta_id, int tid,
			int txq_id, int ssn, struct sk_buff_head *skbs);

	int (*tx_agg_disable)(struct iwl_trans *trans,
			      int sta_id, int tid);
	int (*tx_agg_alloc)(struct iwl_trans *trans,
			    int sta_id, int tid);
	void (*tx_agg_setup)(struct iwl_trans *trans,
			     enum iwl_rxon_context_id ctx, int sta_id, int tid,
			     int frame_limit, u16 ssn);

	void (*free)(struct iwl_trans *trans);

	int (*dbgfs_register)(struct iwl_trans *trans, struct dentry* dir);
	int (*check_stuck_queue)(struct iwl_trans *trans, int q);
	int (*wait_tx_queue_empty)(struct iwl_trans *trans);
#ifdef CONFIG_PM_SLEEP
	int (*suspend)(struct iwl_trans *trans);
	int (*resume)(struct iwl_trans *trans);
#endif
	void (*write8)(struct iwl_trans *trans, u32 ofs, u8 val);
	void (*write32)(struct iwl_trans *trans, u32 ofs, u32 val);
	u32 (*read32)(struct iwl_trans *trans, u32 ofs);
	void (*configure)(struct iwl_trans *trans,
			  const struct iwl_trans_config *trans_cfg);
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
 * @shrd - pointer to iwl_shared which holds shared data from the upper layer
 * @reg_lock - protect hw register access
 * @dev - pointer to struct device * that represents the device
 * @hw_id: a u32 with the ID of the device / subdevice.
 *	Set during transport allocation.
 * @hw_id_str: a string with info about HW ID. Set during transport allocation.
 * @nvm_device_type: indicates OTP or eeprom
 * @pm_support: set to true in start_hw if link pm is supported
 * @wait_command_queue: the wait_queue for SYNC host commands
 */
struct iwl_trans {
	const struct iwl_trans_ops *ops;
	struct iwl_op_mode *op_mode;
	struct iwl_shared *shrd;
	enum iwl_trans_state state;
	spinlock_t reg_lock;

	struct device *dev;
	u32 hw_rev;
	u32 hw_id;
	char hw_id_str[52];

	int    nvm_device_type;
	bool pm_support;

	wait_queue_head_t wait_command_queue;

	/* pointer to trans specific struct */
	/*Ensure that this pointer will always be aligned to sizeof pointer */
	char trans_specific[0] __aligned(sizeof(void *));
};

static inline void iwl_trans_configure(struct iwl_trans *trans,
				       const struct iwl_trans_config *trans_cfg)
{
	/*
	 * only set the op_mode for the moment. Later on, this function will do
	 * more
	 */
	trans->op_mode = trans_cfg->op_mode;

	trans->ops->configure(trans, trans_cfg);
}

static inline int iwl_trans_start_hw(struct iwl_trans *trans)
{
	might_sleep();

	return trans->ops->start_hw(trans);
}

static inline void iwl_trans_stop_hw(struct iwl_trans *trans)
{
	might_sleep();

	trans->ops->stop_hw(trans);

	trans->state = IWL_TRANS_NO_FW;
}

static inline void iwl_trans_fw_alive(struct iwl_trans *trans)
{
	might_sleep();

	trans->ops->fw_alive(trans);

	trans->state = IWL_TRANS_FW_ALIVE;
}

static inline int iwl_trans_start_fw(struct iwl_trans *trans,
				     const struct fw_img *fw)
{
	might_sleep();

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

static inline int iwl_trans_tx(struct iwl_trans *trans, struct sk_buff *skb,
		struct iwl_device_cmd *dev_cmd, enum iwl_rxon_context_id ctx,
		u8 sta_id, u8 tid)
{
	if (trans->state != IWL_TRANS_FW_ALIVE)
		IWL_ERR(trans, "%s bad state = %d", __func__, trans->state);

	return trans->ops->tx(trans, skb, dev_cmd, ctx, sta_id, tid);
}

static inline int iwl_trans_reclaim(struct iwl_trans *trans, int sta_id,
				 int tid, int txq_id, int ssn,
				 struct sk_buff_head *skbs)
{
	WARN_ONCE(trans->state != IWL_TRANS_FW_ALIVE,
		  "%s bad state = %d", __func__, trans->state);

	return trans->ops->reclaim(trans, sta_id, tid, txq_id, ssn, skbs);
}

static inline int iwl_trans_tx_agg_disable(struct iwl_trans *trans,
					    int sta_id, int tid)
{
	WARN_ONCE(trans->state != IWL_TRANS_FW_ALIVE,
		  "%s bad state = %d", __func__, trans->state);

	return trans->ops->tx_agg_disable(trans, sta_id, tid);
}

static inline int iwl_trans_tx_agg_alloc(struct iwl_trans *trans,
					 int sta_id, int tid)
{
	WARN_ONCE(trans->state != IWL_TRANS_FW_ALIVE,
		  "%s bad state = %d", __func__, trans->state);

	return trans->ops->tx_agg_alloc(trans, sta_id, tid);
}


static inline void iwl_trans_tx_agg_setup(struct iwl_trans *trans,
					   enum iwl_rxon_context_id ctx,
					   int sta_id, int tid,
					   int frame_limit, u16 ssn)
{
	might_sleep();

	WARN_ONCE(trans->state != IWL_TRANS_FW_ALIVE,
		  "%s bad state = %d", __func__, trans->state);

	trans->ops->tx_agg_setup(trans, ctx, sta_id, tid, frame_limit, ssn);
}

static inline void iwl_trans_free(struct iwl_trans *trans)
{
	trans->ops->free(trans);
}

static inline int iwl_trans_wait_tx_queue_empty(struct iwl_trans *trans)
{
	WARN_ONCE(trans->state != IWL_TRANS_FW_ALIVE,
		  "%s bad state = %d", __func__, trans->state);

	return trans->ops->wait_tx_queue_empty(trans);
}

static inline int iwl_trans_check_stuck_queue(struct iwl_trans *trans, int q)
{
	WARN_ONCE(trans->state != IWL_TRANS_FW_ALIVE,
		  "%s bad state = %d", __func__, trans->state);

	return trans->ops->check_stuck_queue(trans, q);
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

/*****************************************************
* Transport layers implementations + their allocation function
******************************************************/
struct pci_dev;
struct pci_device_id;
extern const struct iwl_trans_ops trans_ops_pcie;
struct iwl_trans *iwl_trans_pcie_alloc(struct iwl_shared *shrd,
				       struct pci_dev *pdev,
				       const struct pci_device_id *ent);
int __must_check iwl_pci_register_driver(void);
void iwl_pci_unregister_driver(void);

extern const struct iwl_trans_ops trans_ops_idi;
struct iwl_trans *iwl_trans_idi_alloc(struct iwl_shared *shrd,
				      void *pdev_void,
				      const void *ent_void);
#endif /* __iwl_trans_h__ */
