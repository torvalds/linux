/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2020-2021 Intel Corporation.
 */

#ifndef IOSM_IPC_TRACE_H
#define IOSM_IPC_TRACE_H

#include <linux/debugfs.h>
#include <linux/relay.h>

#include "iosm_ipc_chnl_cfg.h"
#include "iosm_ipc_imem_ops.h"

/**
 * enum trace_ctrl_mode - State of trace channel
 * @TRACE_DISABLE:	mode for disable trace
 * @TRACE_ENABLE:	mode for enable trace
 */
enum trace_ctrl_mode {
	TRACE_DISABLE = 0,
	TRACE_ENABLE,
};

/**
 * struct iosm_trace - Struct for trace interface
 * @ipc_rchan:		Pointer to relay channel
 * @ctrl_file:		Pointer to trace control file
 * @ipc_imem:		Imem instance
 * @dev:		Pointer to device struct
 * @channel:		Channel instance
 * @chl_id:		Channel Indentifier
 * @trc_mutex:		Mutex used for read and write mode
 * @mode:		Mode for enable and disable trace
 */

struct iosm_trace {
	struct rchan *ipc_rchan;
	struct dentry *ctrl_file;
	struct iosm_imem *ipc_imem;
	struct device *dev;
	struct ipc_mem_channel *channel;
	enum ipc_channel_id chl_id;
	struct mutex trc_mutex;	/* Mutex used for read and write mode */
	enum trace_ctrl_mode mode;
};

#ifdef CONFIG_WWAN_DEBUGFS

static inline bool ipc_is_trace_channel(struct iosm_imem *ipc_mem, u16 chl_id)
{
	return ipc_mem->trace && ipc_mem->trace->chl_id == chl_id;
}

struct iosm_trace *ipc_trace_init(struct iosm_imem *ipc_imem);
void ipc_trace_deinit(struct iosm_trace *ipc_trace);
void ipc_trace_port_rx(struct iosm_imem *ipc_imem, struct sk_buff *skb);

#else

static inline bool ipc_is_trace_channel(struct iosm_imem *ipc_mem, u16 chl_id)
{
	return false;
}

static inline void ipc_trace_port_rx(struct iosm_imem *ipc_imem,
				     struct sk_buff *skb)
{
	dev_kfree_skb(skb);
}

#endif

#endif
