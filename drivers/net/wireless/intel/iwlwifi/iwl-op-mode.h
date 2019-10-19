/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
 * Copyright(c) 2015        Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
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
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
 * Copyright(c) 2015        Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
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
#ifndef __iwl_op_mode_h__
#define __iwl_op_mode_h__

#include <linux/netdevice.h>
#include <linux/debugfs.h>

struct iwl_op_mode;
struct iwl_trans;
struct sk_buff;
struct iwl_device_cmd;
struct iwl_rx_cmd_buffer;
struct iwl_fw;
struct iwl_cfg;

/**
 * DOC: Operational mode - what is it ?
 *
 * The operational mode (a.k.a. op_mode) is the layer that implements
 * mac80211's handlers. It knows two APIs: mac80211's and the fw's. It uses
 * the transport API to access the HW. The op_mode doesn't need to know how the
 * underlying HW works, since the transport layer takes care of that.
 *
 * There can be several op_mode: i.e. different fw APIs will require two
 * different op_modes. This is why the op_mode is virtualized.
 */

/**
 * DOC: Life cycle of the Operational mode
 *
 * The operational mode has a very simple life cycle.
 *
 *	1) The driver layer (iwl-drv.c) chooses the op_mode based on the
 *	   capabilities advertised by the fw file (in TLV format).
 *	2) The driver layer starts the op_mode (ops->start)
 *	3) The op_mode registers mac80211
 *	4) The op_mode is governed by mac80211
 *	5) The driver layer stops the op_mode
 */

/**
 * struct iwl_op_mode_ops - op_mode specific operations
 *
 * The op_mode exports its ops so that external components can start it and
 * interact with it. The driver layer typically calls the start and stop
 * handlers, the transport layer calls the others.
 *
 * All the handlers MUST be implemented, except @rx_rss which can be left
 * out *iff* the opmode will never run on hardware with multi-queue capability.
 *
 * @start: start the op_mode. The transport layer is already allocated.
 *	May sleep
 * @stop: stop the op_mode. Must free all the memory allocated.
 *	May sleep
 * @rx: Rx notification to the op_mode. rxb is the Rx buffer itself. Cmd is the
 *	HCMD this Rx responds to. Can't sleep.
 * @rx_rss: data queue RX notification to the op_mode, for (data) notifications
 *	received on the RSS queue(s). The queue parameter indicates which of the
 *	RSS queues received this frame; it will always be non-zero.
 *	This method must not sleep.
 * @async_cb: called when an ASYNC command with CMD_WANT_ASYNC_CALLBACK set
 *	completes. Must be atomic.
 * @queue_full: notifies that a HW queue is full.
 *	Must be atomic and called with BH disabled.
 * @queue_not_full: notifies that a HW queue is not full any more.
 *	Must be atomic and called with BH disabled.
 * @hw_rf_kill:notifies of a change in the HW rf kill switch. True means that
 *	the radio is killed. Return %true if the device should be stopped by
 *	the transport immediately after the call. May sleep.
 * @free_skb: allows the transport layer to free skbs that haven't been
 *	reclaimed by the op_mode. This can happen when the driver is freed and
 *	there are Tx packets pending in the transport layer.
 *	Must be atomic
 * @nic_error: error notification. Must be atomic and must be called with BH
 *	disabled.
 * @cmd_queue_full: Called when the command queue gets full. Must be atomic and
 *	called with BH disabled.
 * @nic_config: configure NIC, called before firmware is started.
 *	May sleep
 * @wimax_active: invoked when WiMax becomes active. May sleep
 */
struct iwl_op_mode_ops {
	struct iwl_op_mode *(*start)(struct iwl_trans *trans,
				     const struct iwl_cfg *cfg,
				     const struct iwl_fw *fw,
				     struct dentry *dbgfs_dir);
	void (*stop)(struct iwl_op_mode *op_mode);
	void (*rx)(struct iwl_op_mode *op_mode, struct napi_struct *napi,
		   struct iwl_rx_cmd_buffer *rxb);
	void (*rx_rss)(struct iwl_op_mode *op_mode, struct napi_struct *napi,
		       struct iwl_rx_cmd_buffer *rxb, unsigned int queue);
	void (*async_cb)(struct iwl_op_mode *op_mode,
			 const struct iwl_device_cmd *cmd);
	void (*queue_full)(struct iwl_op_mode *op_mode, int queue);
	void (*queue_not_full)(struct iwl_op_mode *op_mode, int queue);
	bool (*hw_rf_kill)(struct iwl_op_mode *op_mode, bool state);
	void (*free_skb)(struct iwl_op_mode *op_mode, struct sk_buff *skb);
	void (*nic_error)(struct iwl_op_mode *op_mode);
	void (*cmd_queue_full)(struct iwl_op_mode *op_mode);
	void (*nic_config)(struct iwl_op_mode *op_mode);
	void (*wimax_active)(struct iwl_op_mode *op_mode);
};

int iwl_opmode_register(const char *name, const struct iwl_op_mode_ops *ops);
void iwl_opmode_deregister(const char *name);

/**
 * struct iwl_op_mode - operational mode
 * @ops: pointer to its own ops
 *
 * This holds an implementation of the mac80211 / fw API.
 */
struct iwl_op_mode {
	const struct iwl_op_mode_ops *ops;

	char op_mode_specific[0] __aligned(sizeof(void *));
};

static inline void iwl_op_mode_stop(struct iwl_op_mode *op_mode)
{
	might_sleep();
	op_mode->ops->stop(op_mode);
}

static inline void iwl_op_mode_rx(struct iwl_op_mode *op_mode,
				  struct napi_struct *napi,
				  struct iwl_rx_cmd_buffer *rxb)
{
	return op_mode->ops->rx(op_mode, napi, rxb);
}

static inline void iwl_op_mode_rx_rss(struct iwl_op_mode *op_mode,
				      struct napi_struct *napi,
				      struct iwl_rx_cmd_buffer *rxb,
				      unsigned int queue)
{
	op_mode->ops->rx_rss(op_mode, napi, rxb, queue);
}

static inline void iwl_op_mode_async_cb(struct iwl_op_mode *op_mode,
					const struct iwl_device_cmd *cmd)
{
	if (op_mode->ops->async_cb)
		op_mode->ops->async_cb(op_mode, cmd);
}

static inline void iwl_op_mode_queue_full(struct iwl_op_mode *op_mode,
					  int queue)
{
	op_mode->ops->queue_full(op_mode, queue);
}

static inline void iwl_op_mode_queue_not_full(struct iwl_op_mode *op_mode,
					      int queue)
{
	op_mode->ops->queue_not_full(op_mode, queue);
}

static inline bool __must_check
iwl_op_mode_hw_rf_kill(struct iwl_op_mode *op_mode, bool state)
{
	might_sleep();
	return op_mode->ops->hw_rf_kill(op_mode, state);
}

static inline void iwl_op_mode_free_skb(struct iwl_op_mode *op_mode,
					struct sk_buff *skb)
{
	op_mode->ops->free_skb(op_mode, skb);
}

static inline void iwl_op_mode_nic_error(struct iwl_op_mode *op_mode)
{
	op_mode->ops->nic_error(op_mode);
}

static inline void iwl_op_mode_cmd_queue_full(struct iwl_op_mode *op_mode)
{
	op_mode->ops->cmd_queue_full(op_mode);
}

static inline void iwl_op_mode_nic_config(struct iwl_op_mode *op_mode)
{
	might_sleep();
	op_mode->ops->nic_config(op_mode);
}

static inline void iwl_op_mode_wimax_active(struct iwl_op_mode *op_mode)
{
	might_sleep();
	op_mode->ops->wimax_active(op_mode);
}

#endif /* __iwl_op_mode_h__ */
