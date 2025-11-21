// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2021 Intel Corporation.
 */

#include <linux/wwan.h>
#include "iosm_ipc_trace.h"

/* sub buffer size and number of sub buffer */
#define IOSM_TRC_SUB_BUFF_SIZE 131072
#define IOSM_TRC_N_SUB_BUFF 32

#define IOSM_TRC_FILE_PERM 0600

#define IOSM_TRC_DEBUGFS_TRACE "trace"
#define IOSM_TRC_DEBUGFS_TRACE_CTRL "trace_ctrl"

/**
 * ipc_trace_port_rx - Receive trace packet from cp and write to relay buffer
 * @ipc_imem:   Pointer to iosm_imem structure
 * @skb:        Pointer to struct sk_buff
 */
void ipc_trace_port_rx(struct iosm_imem *ipc_imem, struct sk_buff *skb)
{
	struct iosm_trace *ipc_trace = ipc_imem->trace;

	if (ipc_trace->ipc_rchan)
		relay_write(ipc_trace->ipc_rchan, skb->data, skb->len);

	dev_kfree_skb(skb);
}

/* Creates relay file in debugfs. */
static struct dentry *
ipc_trace_create_buf_file_handler(const char *filename,
				  struct dentry *parent,
				  umode_t mode,
				  struct rchan_buf *buf,
				  int *is_global)
{
	*is_global = 1;
	return debugfs_create_file(filename, mode, parent, buf,
				   &relay_file_operations);
}

/* Removes relay file from debugfs. */
static int ipc_trace_remove_buf_file_handler(struct dentry *dentry)
{
	debugfs_remove(dentry);
	return 0;
}

static int ipc_trace_subbuf_start_handler(struct rchan_buf *buf, void *subbuf,
					  void *prev_subbuf)
{
	if (relay_buf_full(buf)) {
		pr_err_ratelimited("Relay_buf full dropping traces");
		return 0;
	}

	return 1;
}

/* Relay interface callbacks */
static struct rchan_callbacks relay_callbacks = {
	.subbuf_start = ipc_trace_subbuf_start_handler,
	.create_buf_file = ipc_trace_create_buf_file_handler,
	.remove_buf_file = ipc_trace_remove_buf_file_handler,
};

/* Copy the trace control mode to user buffer */
static ssize_t ipc_trace_ctrl_file_read(struct file *filp, char __user *buffer,
					size_t count, loff_t *ppos)
{
	struct iosm_trace *ipc_trace = filp->private_data;
	char buf[16];
	int len;

	mutex_lock(&ipc_trace->trc_mutex);
	len = snprintf(buf, sizeof(buf), "%d\n", ipc_trace->mode);
	mutex_unlock(&ipc_trace->trc_mutex);

	return simple_read_from_buffer(buffer, count, ppos, buf, len);
}

/* Open and close the trace channel depending on user input */
static ssize_t ipc_trace_ctrl_file_write(struct file *filp,
					 const char __user *buffer,
					 size_t count, loff_t *ppos)
{
	struct iosm_trace *ipc_trace = filp->private_data;
	unsigned long val;
	int ret;

	ret = kstrtoul_from_user(buffer, count, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&ipc_trace->trc_mutex);
	if (val == TRACE_ENABLE && ipc_trace->mode != TRACE_ENABLE) {
		ipc_trace->channel = ipc_imem_sys_port_open(ipc_trace->ipc_imem,
							    ipc_trace->chl_id,
							    IPC_HP_CDEV_OPEN);
		if (!ipc_trace->channel) {
			ret = -EIO;
			goto unlock;
		}
		ipc_trace->mode = TRACE_ENABLE;
	} else if (val == TRACE_DISABLE && ipc_trace->mode != TRACE_DISABLE) {
		ipc_trace->mode = TRACE_DISABLE;
		/* close trace channel */
		ipc_imem_sys_port_close(ipc_trace->ipc_imem,
					ipc_trace->channel);
		relay_flush(ipc_trace->ipc_rchan);
	}
	ret = count;
unlock:
	mutex_unlock(&ipc_trace->trc_mutex);
	return ret;
}

static const struct file_operations ipc_trace_fops = {
	.open = simple_open,
	.write = ipc_trace_ctrl_file_write,
	.read  = ipc_trace_ctrl_file_read,
};

/**
 * ipc_trace_init - Create trace interface & debugfs entries
 * @ipc_imem:   Pointer to iosm_imem structure
 *
 * Returns: Pointer to trace instance on success else NULL
 */
struct iosm_trace *ipc_trace_init(struct iosm_imem *ipc_imem)
{
	struct ipc_chnl_cfg chnl_cfg = { 0 };
	struct iosm_trace *ipc_trace;

	ipc_chnl_cfg_get(&chnl_cfg, IPC_MEM_CTRL_CHL_ID_3);
	ipc_imem_channel_init(ipc_imem, IPC_CTYPE_CTRL, chnl_cfg,
			      IRQ_MOD_OFF);

	ipc_trace = kzalloc(sizeof(*ipc_trace), GFP_KERNEL);
	if (!ipc_trace)
		return NULL;

	ipc_trace->mode = TRACE_DISABLE;
	ipc_trace->dev = ipc_imem->dev;
	ipc_trace->ipc_imem = ipc_imem;
	ipc_trace->chl_id = IPC_MEM_CTRL_CHL_ID_3;

	mutex_init(&ipc_trace->trc_mutex);

	ipc_trace->ctrl_file = debugfs_create_file(IOSM_TRC_DEBUGFS_TRACE_CTRL,
						   IOSM_TRC_FILE_PERM,
						   ipc_imem->debugfs_dir,
						   ipc_trace, &ipc_trace_fops);

	ipc_trace->ipc_rchan = relay_open(IOSM_TRC_DEBUGFS_TRACE,
					  ipc_imem->debugfs_dir,
					  IOSM_TRC_SUB_BUFF_SIZE,
					  IOSM_TRC_N_SUB_BUFF,
					  &relay_callbacks, NULL);

	return ipc_trace;
}

/**
 * ipc_trace_deinit - Closing relayfs, removing debugfs entries
 * @ipc_trace: Pointer to the iosm_trace data struct
 */
void ipc_trace_deinit(struct iosm_trace *ipc_trace)
{
	if (!ipc_trace)
		return;

	debugfs_remove(ipc_trace->ctrl_file);
	relay_close(ipc_trace->ipc_rchan);
	mutex_destroy(&ipc_trace->trc_mutex);
	kfree(ipc_trace);
}
