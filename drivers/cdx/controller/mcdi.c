// SPDX-License-Identifier: GPL-2.0
/*
 * Management-Controller-to-Driver Interface
 *
 * Copyright 2008-2013 Solarflare Communications Inc.
 * Copyright (C) 2022-2023, Advanced Micro Devices, Inc.
 */
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/rwsem.h>
#include <linux/vmalloc.h>
#include <net/netevent.h>
#include <linux/log2.h>
#include <linux/net_tstamp.h>
#include <linux/wait.h>

#include "bitfield.h"
#include "mcdi.h"

static void cdx_mcdi_cancel_cmd(struct cdx_mcdi *cdx, struct cdx_mcdi_cmd *cmd);
static void cdx_mcdi_wait_for_cleanup(struct cdx_mcdi *cdx);
static int cdx_mcdi_rpc_async_internal(struct cdx_mcdi *cdx,
				       struct cdx_mcdi_cmd *cmd,
				       unsigned int *handle);
static void cdx_mcdi_start_or_queue(struct cdx_mcdi_iface *mcdi,
				    bool allow_retry);
static void cdx_mcdi_cmd_start_or_queue(struct cdx_mcdi_iface *mcdi,
					struct cdx_mcdi_cmd *cmd);
static bool cdx_mcdi_complete_cmd(struct cdx_mcdi_iface *mcdi,
				  struct cdx_mcdi_cmd *cmd,
				  struct cdx_dword *outbuf,
				  int len,
				  struct list_head *cleanup_list);
static void cdx_mcdi_timeout_cmd(struct cdx_mcdi_iface *mcdi,
				 struct cdx_mcdi_cmd *cmd,
				 struct list_head *cleanup_list);
static void cdx_mcdi_cmd_work(struct work_struct *context);
static void cdx_mcdi_mode_fail(struct cdx_mcdi *cdx, struct list_head *cleanup_list);
static void _cdx_mcdi_display_error(struct cdx_mcdi *cdx, unsigned int cmd,
				    size_t inlen, int raw, int arg, int err_no);

static bool cdx_cmd_cancelled(struct cdx_mcdi_cmd *cmd)
{
	return cmd->state == MCDI_STATE_RUNNING_CANCELLED;
}

static void cdx_mcdi_cmd_release(struct kref *ref)
{
	kfree(container_of(ref, struct cdx_mcdi_cmd, ref));
}

static unsigned int cdx_mcdi_cmd_handle(struct cdx_mcdi_cmd *cmd)
{
	return cmd->handle;
}

static void _cdx_mcdi_remove_cmd(struct cdx_mcdi_iface *mcdi,
				 struct cdx_mcdi_cmd *cmd,
				 struct list_head *cleanup_list)
{
	/* if cancelled, the completers have already been called */
	if (cdx_cmd_cancelled(cmd))
		return;

	if (cmd->completer) {
		list_add_tail(&cmd->cleanup_list, cleanup_list);
		++mcdi->outstanding_cleanups;
		kref_get(&cmd->ref);
	}
}

static void cdx_mcdi_remove_cmd(struct cdx_mcdi_iface *mcdi,
				struct cdx_mcdi_cmd *cmd,
				struct list_head *cleanup_list)
{
	list_del(&cmd->list);
	_cdx_mcdi_remove_cmd(mcdi, cmd, cleanup_list);
	cmd->state = MCDI_STATE_FINISHED;
	kref_put(&cmd->ref, cdx_mcdi_cmd_release);
	if (list_empty(&mcdi->cmd_list))
		wake_up(&mcdi->cmd_complete_wq);
}

static unsigned long cdx_mcdi_rpc_timeout(struct cdx_mcdi *cdx, unsigned int cmd)
{
	if (!cdx->mcdi_ops->mcdi_rpc_timeout)
		return MCDI_RPC_TIMEOUT;
	else
		return cdx->mcdi_ops->mcdi_rpc_timeout(cdx, cmd);
}

int cdx_mcdi_init(struct cdx_mcdi *cdx)
{
	struct cdx_mcdi_iface *mcdi;
	int rc = -ENOMEM;

	cdx->mcdi = kzalloc(sizeof(*cdx->mcdi), GFP_KERNEL);
	if (!cdx->mcdi)
		goto fail;

	mcdi = cdx_mcdi_if(cdx);
	mcdi->cdx = cdx;

	mcdi->workqueue = alloc_ordered_workqueue("mcdi_wq", 0);
	if (!mcdi->workqueue)
		goto fail2;
	mutex_init(&mcdi->iface_lock);
	mcdi->mode = MCDI_MODE_EVENTS;
	INIT_LIST_HEAD(&mcdi->cmd_list);
	init_waitqueue_head(&mcdi->cmd_complete_wq);

	mcdi->new_epoch = true;

	return 0;
fail2:
	kfree(cdx->mcdi);
	cdx->mcdi = NULL;
fail:
	return rc;
}

void cdx_mcdi_finish(struct cdx_mcdi *cdx)
{
	struct cdx_mcdi_iface *mcdi;

	mcdi = cdx_mcdi_if(cdx);
	if (!mcdi)
		return;

	cdx_mcdi_wait_for_cleanup(cdx);

	destroy_workqueue(mcdi->workqueue);
	kfree(cdx->mcdi);
	cdx->mcdi = NULL;
}

static bool cdx_mcdi_flushed(struct cdx_mcdi_iface *mcdi, bool ignore_cleanups)
{
	bool flushed;

	mutex_lock(&mcdi->iface_lock);
	flushed = list_empty(&mcdi->cmd_list) &&
		  (ignore_cleanups || !mcdi->outstanding_cleanups);
	mutex_unlock(&mcdi->iface_lock);
	return flushed;
}

/* Wait for outstanding MCDI commands to complete. */
static void cdx_mcdi_wait_for_cleanup(struct cdx_mcdi *cdx)
{
	struct cdx_mcdi_iface *mcdi = cdx_mcdi_if(cdx);

	if (!mcdi)
		return;

	wait_event(mcdi->cmd_complete_wq,
		   cdx_mcdi_flushed(mcdi, false));
}

int cdx_mcdi_wait_for_quiescence(struct cdx_mcdi *cdx,
				 unsigned int timeout_jiffies)
{
	struct cdx_mcdi_iface *mcdi = cdx_mcdi_if(cdx);
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	int rc = 0;

	if (!mcdi)
		return -EINVAL;

	flush_workqueue(mcdi->workqueue);

	add_wait_queue(&mcdi->cmd_complete_wq, &wait);

	while (!cdx_mcdi_flushed(mcdi, true)) {
		rc = wait_woken(&wait, TASK_IDLE, timeout_jiffies);
		if (rc)
			continue;
		break;
	}

	remove_wait_queue(&mcdi->cmd_complete_wq, &wait);

	if (rc > 0)
		rc = 0;
	else if (rc == 0)
		rc = -ETIMEDOUT;

	return rc;
}

static u8 cdx_mcdi_payload_csum(const struct cdx_dword *hdr, size_t hdr_len,
				const struct cdx_dword *sdu, size_t sdu_len)
{
	u8 *p = (u8 *)hdr;
	u8 csum = 0;
	int i;

	for (i = 0; i < hdr_len; i++)
		csum += p[i];

	p = (u8 *)sdu;
	for (i = 0; i < sdu_len; i++)
		csum += p[i];

	return ~csum & 0xff;
}

static void cdx_mcdi_send_request(struct cdx_mcdi *cdx,
				  struct cdx_mcdi_cmd *cmd)
{
	struct cdx_mcdi_iface *mcdi = cdx_mcdi_if(cdx);
	const struct cdx_dword *inbuf = cmd->inbuf;
	size_t inlen = cmd->inlen;
	struct cdx_dword hdr[2];
	size_t hdr_len;
	bool not_epoch;
	u32 xflags;

	if (!mcdi)
		return;

	mcdi->prev_seq = cmd->seq;
	mcdi->seq_held_by[cmd->seq] = cmd;
	mcdi->db_held_by = cmd;
	cmd->started = jiffies;

	not_epoch = !mcdi->new_epoch;
	xflags = 0;

	/* MCDI v2 */
	WARN_ON(inlen > MCDI_CTL_SDU_LEN_MAX_V2);
	CDX_POPULATE_DWORD_7(hdr[0],
			     MCDI_HEADER_RESPONSE, 0,
			     MCDI_HEADER_RESYNC, 1,
			     MCDI_HEADER_CODE, MC_CMD_V2_EXTN,
			     MCDI_HEADER_DATALEN, 0,
			     MCDI_HEADER_SEQ, cmd->seq,
			     MCDI_HEADER_XFLAGS, xflags,
			     MCDI_HEADER_NOT_EPOCH, not_epoch);
	CDX_POPULATE_DWORD_3(hdr[1],
			     MC_CMD_V2_EXTN_IN_EXTENDED_CMD, cmd->cmd,
			     MC_CMD_V2_EXTN_IN_ACTUAL_LEN, inlen,
			     MC_CMD_V2_EXTN_IN_MESSAGE_TYPE,
			     MC_CMD_V2_EXTN_IN_MCDI_MESSAGE_TYPE_PLATFORM);
	hdr_len = 8;

	hdr[0].cdx_u32 |= (__force __le32)(cdx_mcdi_payload_csum(hdr, hdr_len, inbuf, inlen) <<
			 MCDI_HEADER_XFLAGS_LBN);

	print_hex_dump_debug("MCDI REQ HEADER: ", DUMP_PREFIX_NONE, 32, 4, hdr, hdr_len, false);
	print_hex_dump_debug("MCDI REQ PAYLOAD: ", DUMP_PREFIX_NONE, 32, 4, inbuf, inlen, false);

	cdx->mcdi_ops->mcdi_request(cdx, hdr, hdr_len, inbuf, inlen);

	mcdi->new_epoch = false;
}

static int cdx_mcdi_errno(struct cdx_mcdi *cdx, unsigned int mcdi_err)
{
	switch (mcdi_err) {
	case 0:
	case MC_CMD_ERR_QUEUE_FULL:
		return mcdi_err;
	case MC_CMD_ERR_EPERM:
		return -EPERM;
	case MC_CMD_ERR_ENOENT:
		return -ENOENT;
	case MC_CMD_ERR_EINTR:
		return -EINTR;
	case MC_CMD_ERR_EAGAIN:
		return -EAGAIN;
	case MC_CMD_ERR_EACCES:
		return -EACCES;
	case MC_CMD_ERR_EBUSY:
		return -EBUSY;
	case MC_CMD_ERR_EINVAL:
		return -EINVAL;
	case MC_CMD_ERR_ERANGE:
		return -ERANGE;
	case MC_CMD_ERR_EDEADLK:
		return -EDEADLK;
	case MC_CMD_ERR_ENOSYS:
		return -EOPNOTSUPP;
	case MC_CMD_ERR_ETIME:
		return -ETIME;
	case MC_CMD_ERR_EALREADY:
		return -EALREADY;
	case MC_CMD_ERR_ENOSPC:
		return -ENOSPC;
	case MC_CMD_ERR_ENOMEM:
		return -ENOMEM;
	case MC_CMD_ERR_ENOTSUP:
		return -EOPNOTSUPP;
	case MC_CMD_ERR_ALLOC_FAIL:
		return -ENOBUFS;
	case MC_CMD_ERR_MAC_EXIST:
		return -EADDRINUSE;
	case MC_CMD_ERR_NO_EVB_PORT:
		return -EAGAIN;
	default:
		return -EPROTO;
	}
}

static void cdx_mcdi_process_cleanup_list(struct cdx_mcdi *cdx,
					  struct list_head *cleanup_list)
{
	struct cdx_mcdi_iface *mcdi = cdx_mcdi_if(cdx);
	unsigned int cleanups = 0;

	if (!mcdi)
		return;

	while (!list_empty(cleanup_list)) {
		struct cdx_mcdi_cmd *cmd =
			list_first_entry(cleanup_list,
					 struct cdx_mcdi_cmd, cleanup_list);
		cmd->completer(cdx, cmd->cookie, cmd->rc,
			       cmd->outbuf, cmd->outlen);
		list_del(&cmd->cleanup_list);
		kref_put(&cmd->ref, cdx_mcdi_cmd_release);
		++cleanups;
	}

	if (cleanups) {
		bool all_done;

		mutex_lock(&mcdi->iface_lock);
		CDX_WARN_ON_PARANOID(cleanups > mcdi->outstanding_cleanups);
		all_done = (mcdi->outstanding_cleanups -= cleanups) == 0;
		mutex_unlock(&mcdi->iface_lock);
		if (all_done)
			wake_up(&mcdi->cmd_complete_wq);
	}
}

static void _cdx_mcdi_cancel_cmd(struct cdx_mcdi_iface *mcdi,
				 unsigned int handle,
				 struct list_head *cleanup_list)
{
	struct cdx_mcdi_cmd *cmd;

	list_for_each_entry(cmd, &mcdi->cmd_list, list)
		if (cdx_mcdi_cmd_handle(cmd) == handle) {
			switch (cmd->state) {
			case MCDI_STATE_QUEUED:
			case MCDI_STATE_RETRY:
				pr_debug("command %#x inlen %zu cancelled in queue\n",
					 cmd->cmd, cmd->inlen);
				/* if not yet running, properly cancel it */
				cmd->rc = -EPIPE;
				cdx_mcdi_remove_cmd(mcdi, cmd, cleanup_list);
				break;
			case MCDI_STATE_RUNNING:
			case MCDI_STATE_RUNNING_CANCELLED:
			case MCDI_STATE_FINISHED:
			default:
				/* invalid state? */
				WARN_ON(1);
			}
			break;
		}
}

static void cdx_mcdi_cancel_cmd(struct cdx_mcdi *cdx, struct cdx_mcdi_cmd *cmd)
{
	struct cdx_mcdi_iface *mcdi = cdx_mcdi_if(cdx);
	LIST_HEAD(cleanup_list);

	if (!mcdi)
		return;

	mutex_lock(&mcdi->iface_lock);
	cdx_mcdi_timeout_cmd(mcdi, cmd, &cleanup_list);
	mutex_unlock(&mcdi->iface_lock);
	cdx_mcdi_process_cleanup_list(cdx, &cleanup_list);
}

struct cdx_mcdi_blocking_data {
	struct kref ref;
	bool done;
	wait_queue_head_t wq;
	int rc;
	struct cdx_dword *outbuf;
	size_t outlen;
	size_t outlen_actual;
};

static void cdx_mcdi_blocking_data_release(struct kref *ref)
{
	kfree(container_of(ref, struct cdx_mcdi_blocking_data, ref));
}

static void cdx_mcdi_rpc_completer(struct cdx_mcdi *cdx, unsigned long cookie,
				   int rc, struct cdx_dword *outbuf,
				   size_t outlen_actual)
{
	struct cdx_mcdi_blocking_data *wait_data =
		(struct cdx_mcdi_blocking_data *)cookie;

	wait_data->rc = rc;
	memcpy(wait_data->outbuf, outbuf,
	       min(outlen_actual, wait_data->outlen));
	wait_data->outlen_actual = outlen_actual;
	/* memory barrier */
	smp_wmb();
	wait_data->done = true;
	wake_up(&wait_data->wq);
	kref_put(&wait_data->ref, cdx_mcdi_blocking_data_release);
}

static int cdx_mcdi_rpc_sync(struct cdx_mcdi *cdx, unsigned int cmd,
			     const struct cdx_dword *inbuf, size_t inlen,
			     struct cdx_dword *outbuf, size_t outlen,
			     size_t *outlen_actual, bool quiet)
{
	struct cdx_mcdi_blocking_data *wait_data;
	struct cdx_mcdi_cmd *cmd_item;
	unsigned int handle;
	int rc;

	if (outlen_actual)
		*outlen_actual = 0;

	wait_data = kmalloc(sizeof(*wait_data), GFP_KERNEL);
	if (!wait_data)
		return -ENOMEM;

	cmd_item = kmalloc(sizeof(*cmd_item), GFP_KERNEL);
	if (!cmd_item) {
		kfree(wait_data);
		return -ENOMEM;
	}

	kref_init(&wait_data->ref);
	wait_data->done = false;
	init_waitqueue_head(&wait_data->wq);
	wait_data->outbuf = outbuf;
	wait_data->outlen = outlen;

	kref_init(&cmd_item->ref);
	cmd_item->quiet = quiet;
	cmd_item->cookie = (unsigned long)wait_data;
	cmd_item->completer = &cdx_mcdi_rpc_completer;
	cmd_item->cmd = cmd;
	cmd_item->inlen = inlen;
	cmd_item->inbuf = inbuf;

	/* Claim an extra reference for the completer to put. */
	kref_get(&wait_data->ref);
	rc = cdx_mcdi_rpc_async_internal(cdx, cmd_item, &handle);
	if (rc) {
		kref_put(&wait_data->ref, cdx_mcdi_blocking_data_release);
		goto out;
	}

	if (!wait_event_timeout(wait_data->wq, wait_data->done,
				cdx_mcdi_rpc_timeout(cdx, cmd)) &&
	    !wait_data->done) {
		pr_err("MC command 0x%x inlen %zu timed out (sync)\n",
		       cmd, inlen);

		cdx_mcdi_cancel_cmd(cdx, cmd_item);

		wait_data->rc = -ETIMEDOUT;
		wait_data->outlen_actual = 0;
	}

	if (outlen_actual)
		*outlen_actual = wait_data->outlen_actual;
	rc = wait_data->rc;

out:
	kref_put(&wait_data->ref, cdx_mcdi_blocking_data_release);

	return rc;
}

static bool cdx_mcdi_get_seq(struct cdx_mcdi_iface *mcdi, unsigned char *seq)
{
	*seq = mcdi->prev_seq;
	do {
		*seq = (*seq + 1) % ARRAY_SIZE(mcdi->seq_held_by);
	} while (mcdi->seq_held_by[*seq] && *seq != mcdi->prev_seq);
	return !mcdi->seq_held_by[*seq];
}

static int cdx_mcdi_rpc_async_internal(struct cdx_mcdi *cdx,
				       struct cdx_mcdi_cmd *cmd,
				       unsigned int *handle)
{
	struct cdx_mcdi_iface *mcdi = cdx_mcdi_if(cdx);
	LIST_HEAD(cleanup_list);

	if (!mcdi) {
		kref_put(&cmd->ref, cdx_mcdi_cmd_release);
		return -ENETDOWN;
	}

	if (mcdi->mode == MCDI_MODE_FAIL) {
		kref_put(&cmd->ref, cdx_mcdi_cmd_release);
		return -ENETDOWN;
	}

	cmd->mcdi = mcdi;
	INIT_WORK(&cmd->work, cdx_mcdi_cmd_work);
	INIT_LIST_HEAD(&cmd->list);
	INIT_LIST_HEAD(&cmd->cleanup_list);
	cmd->rc = 0;
	cmd->outbuf = NULL;
	cmd->outlen = 0;

	queue_work(mcdi->workqueue, &cmd->work);
	return 0;
}

static void cdx_mcdi_cmd_start_or_queue(struct cdx_mcdi_iface *mcdi,
					struct cdx_mcdi_cmd *cmd)
{
	struct cdx_mcdi *cdx = mcdi->cdx;
	u8 seq;

	if (!mcdi->db_held_by &&
	    cdx_mcdi_get_seq(mcdi, &seq)) {
		cmd->seq = seq;
		cmd->reboot_seen = false;
		cdx_mcdi_send_request(cdx, cmd);
		cmd->state = MCDI_STATE_RUNNING;
	} else {
		cmd->state = MCDI_STATE_QUEUED;
	}
}

/* try to advance other commands */
static void cdx_mcdi_start_or_queue(struct cdx_mcdi_iface *mcdi,
				    bool allow_retry)
{
	struct cdx_mcdi_cmd *cmd, *tmp;

	list_for_each_entry_safe(cmd, tmp, &mcdi->cmd_list, list)
		if (cmd->state == MCDI_STATE_QUEUED ||
		    (cmd->state == MCDI_STATE_RETRY && allow_retry))
			cdx_mcdi_cmd_start_or_queue(mcdi, cmd);
}

void cdx_mcdi_process_cmd(struct cdx_mcdi *cdx, struct cdx_dword *outbuf, int len)
{
	struct cdx_mcdi_iface *mcdi;
	struct cdx_mcdi_cmd *cmd;
	LIST_HEAD(cleanup_list);
	unsigned int respseq;

	if (!len || !outbuf) {
		pr_err("Got empty MC response\n");
		return;
	}

	mcdi = cdx_mcdi_if(cdx);
	if (!mcdi)
		return;

	respseq = CDX_DWORD_FIELD(outbuf[0], MCDI_HEADER_SEQ);

	mutex_lock(&mcdi->iface_lock);
	cmd = mcdi->seq_held_by[respseq];

	if (cmd) {
		if (cmd->state == MCDI_STATE_FINISHED) {
			mutex_unlock(&mcdi->iface_lock);
			kref_put(&cmd->ref, cdx_mcdi_cmd_release);
			return;
		}

		cdx_mcdi_complete_cmd(mcdi, cmd, outbuf, len, &cleanup_list);
	} else {
		pr_err("MC response unexpected for seq : %0X\n", respseq);
	}

	mutex_unlock(&mcdi->iface_lock);

	cdx_mcdi_process_cleanup_list(mcdi->cdx, &cleanup_list);
}

static void cdx_mcdi_cmd_work(struct work_struct *context)
{
	struct cdx_mcdi_cmd *cmd =
		container_of(context, struct cdx_mcdi_cmd, work);
	struct cdx_mcdi_iface *mcdi = cmd->mcdi;

	mutex_lock(&mcdi->iface_lock);

	cmd->handle = mcdi->prev_handle++;
	list_add_tail(&cmd->list, &mcdi->cmd_list);
	cdx_mcdi_cmd_start_or_queue(mcdi, cmd);

	mutex_unlock(&mcdi->iface_lock);
}

/*
 * Returns true if the MCDI module is finished with the command.
 * (examples of false would be if the command was proxied, or it was
 * rejected by the MC due to lack of resources and requeued).
 */
static bool cdx_mcdi_complete_cmd(struct cdx_mcdi_iface *mcdi,
				  struct cdx_mcdi_cmd *cmd,
				  struct cdx_dword *outbuf,
				  int len,
				  struct list_head *cleanup_list)
{
	size_t resp_hdr_len, resp_data_len;
	struct cdx_mcdi *cdx = mcdi->cdx;
	unsigned int respcmd, error;
	bool completed = false;
	int rc;

	/* ensure the command can't go away before this function returns */
	kref_get(&cmd->ref);

	respcmd = CDX_DWORD_FIELD(outbuf[0], MCDI_HEADER_CODE);
	error = CDX_DWORD_FIELD(outbuf[0], MCDI_HEADER_ERROR);

	if (respcmd != MC_CMD_V2_EXTN) {
		resp_hdr_len = 4;
		resp_data_len = CDX_DWORD_FIELD(outbuf[0], MCDI_HEADER_DATALEN);
	} else {
		resp_data_len = 0;
		resp_hdr_len = 8;
		if (len >= 8)
			resp_data_len =
				CDX_DWORD_FIELD(outbuf[1], MC_CMD_V2_EXTN_IN_ACTUAL_LEN);
	}

	if ((resp_hdr_len + resp_data_len) > len) {
		pr_warn("Incomplete MCDI response received %d. Expected %zu\n",
			len, (resp_hdr_len + resp_data_len));
		resp_data_len = 0;
	}

	print_hex_dump_debug("MCDI RESP HEADER: ", DUMP_PREFIX_NONE, 32, 4,
			     outbuf, resp_hdr_len, false);
	print_hex_dump_debug("MCDI RESP PAYLOAD: ", DUMP_PREFIX_NONE, 32, 4,
			     outbuf + (resp_hdr_len / 4), resp_data_len, false);

	if (error && resp_data_len == 0) {
		/* MC rebooted during command */
		rc = -EIO;
	} else {
		if (WARN_ON_ONCE(error && resp_data_len < 4))
			resp_data_len = 4;
		if (error) {
			rc = CDX_DWORD_FIELD(outbuf[resp_hdr_len / 4], CDX_DWORD);
			if (!cmd->quiet) {
				int err_arg = 0;

				if (resp_data_len >= MC_CMD_ERR_ARG_OFST + 4) {
					int offset = (resp_hdr_len + MC_CMD_ERR_ARG_OFST) / 4;

					err_arg = CDX_DWORD_VAL(outbuf[offset]);
				}

				_cdx_mcdi_display_error(cdx, cmd->cmd,
							cmd->inlen, rc, err_arg,
							cdx_mcdi_errno(cdx, rc));
			}
			rc = cdx_mcdi_errno(cdx, rc);
		} else {
			rc = 0;
		}
	}

	/* free doorbell */
	if (mcdi->db_held_by == cmd)
		mcdi->db_held_by = NULL;

	if (cdx_cmd_cancelled(cmd)) {
		list_del(&cmd->list);
		kref_put(&cmd->ref, cdx_mcdi_cmd_release);
		completed = true;
	} else if (rc == MC_CMD_ERR_QUEUE_FULL) {
		cmd->state = MCDI_STATE_RETRY;
	} else {
		cmd->rc = rc;
		cmd->outbuf = outbuf + DIV_ROUND_UP(resp_hdr_len, 4);
		cmd->outlen = resp_data_len;
		cdx_mcdi_remove_cmd(mcdi, cmd, cleanup_list);
		completed = true;
	}

	/* free sequence number and buffer */
	mcdi->seq_held_by[cmd->seq] = NULL;

	cdx_mcdi_start_or_queue(mcdi, rc != MC_CMD_ERR_QUEUE_FULL);

	/* wake up anyone waiting for flush */
	wake_up(&mcdi->cmd_complete_wq);

	kref_put(&cmd->ref, cdx_mcdi_cmd_release);

	return completed;
}

static void cdx_mcdi_timeout_cmd(struct cdx_mcdi_iface *mcdi,
				 struct cdx_mcdi_cmd *cmd,
				 struct list_head *cleanup_list)
{
	struct cdx_mcdi *cdx = mcdi->cdx;

	pr_err("MC command 0x%x inlen %zu state %d timed out after %u ms\n",
	       cmd->cmd, cmd->inlen, cmd->state,
	       jiffies_to_msecs(jiffies - cmd->started));

	cmd->rc = -ETIMEDOUT;
	cdx_mcdi_remove_cmd(mcdi, cmd, cleanup_list);

	cdx_mcdi_mode_fail(cdx, cleanup_list);
}

/**
 * cdx_mcdi_rpc - Issue an MCDI command and wait for completion
 * @cdx: NIC through which to issue the command
 * @cmd: Command type number
 * @inbuf: Command parameters
 * @inlen: Length of command parameters, in bytes. Must be a multiple
 *	of 4 and no greater than %MCDI_CTL_SDU_LEN_MAX_V1.
 * @outbuf: Response buffer. May be %NULL if @outlen is 0.
 * @outlen: Length of response buffer, in bytes. If the actual
 *	response is longer than @outlen & ~3, it will be truncated
 *	to that length.
 * @outlen_actual: Pointer through which to return the actual response
 *	length. May be %NULL if this is not needed.
 *
 * This function may sleep and therefore must be called in process
 * context.
 *
 * Return: A negative error code, or zero if successful. The error
 *	code may come from the MCDI response or may indicate a failure
 *	to communicate with the MC. In the former case, the response
 *	will still be copied to @outbuf and *@outlen_actual will be
 *	set accordingly. In the latter case, *@outlen_actual will be
 *	set to zero.
 */
int cdx_mcdi_rpc(struct cdx_mcdi *cdx, unsigned int cmd,
		 const struct cdx_dword *inbuf, size_t inlen,
		 struct cdx_dword *outbuf, size_t outlen,
		 size_t *outlen_actual)
{
	return cdx_mcdi_rpc_sync(cdx, cmd, inbuf, inlen, outbuf, outlen,
				 outlen_actual, false);
}

/**
 * cdx_mcdi_rpc_async - Schedule an MCDI command to run asynchronously
 * @cdx: NIC through which to issue the command
 * @cmd: Command type number
 * @inbuf: Command parameters
 * @inlen: Length of command parameters, in bytes
 * @complete: Function to be called on completion or cancellation.
 * @cookie: Arbitrary value to be passed to @complete.
 *
 * This function does not sleep and therefore may be called in atomic
 * context.  It will fail if event queues are disabled or if MCDI
 * event completions have been disabled due to an error.
 *
 * If it succeeds, the @complete function will be called exactly once
 * in process context, when one of the following occurs:
 * (a) the completion event is received (in process context)
 * (b) event queues are disabled (in the process that disables them)
 */
int
cdx_mcdi_rpc_async(struct cdx_mcdi *cdx, unsigned int cmd,
		   const struct cdx_dword *inbuf, size_t inlen,
		   cdx_mcdi_async_completer *complete, unsigned long cookie)
{
	struct cdx_mcdi_cmd *cmd_item =
		kmalloc(sizeof(struct cdx_mcdi_cmd) + inlen, GFP_ATOMIC);

	if (!cmd_item)
		return -ENOMEM;

	kref_init(&cmd_item->ref);
	cmd_item->quiet = true;
	cmd_item->cookie = cookie;
	cmd_item->completer = complete;
	cmd_item->cmd = cmd;
	cmd_item->inlen = inlen;
	/* inbuf is probably not valid after return, so take a copy */
	cmd_item->inbuf = (struct cdx_dword *)(cmd_item + 1);
	memcpy(cmd_item + 1, inbuf, inlen);

	return cdx_mcdi_rpc_async_internal(cdx, cmd_item, NULL);
}

static void _cdx_mcdi_display_error(struct cdx_mcdi *cdx, unsigned int cmd,
				    size_t inlen, int raw, int arg, int err_no)
{
	pr_err("MC command 0x%x inlen %d failed err_no=%d (raw=%d) arg=%d\n",
	       cmd, (int)inlen, err_no, raw, arg);
}

/*
 * Set MCDI mode to fail to prevent any new commands, then cancel any
 * outstanding commands.
 * Caller must hold the mcdi iface_lock.
 */
static void cdx_mcdi_mode_fail(struct cdx_mcdi *cdx, struct list_head *cleanup_list)
{
	struct cdx_mcdi_iface *mcdi = cdx_mcdi_if(cdx);

	if (!mcdi)
		return;

	mcdi->mode = MCDI_MODE_FAIL;

	while (!list_empty(&mcdi->cmd_list)) {
		struct cdx_mcdi_cmd *cmd;

		cmd = list_first_entry(&mcdi->cmd_list, struct cdx_mcdi_cmd,
				       list);
		_cdx_mcdi_cancel_cmd(mcdi, cdx_mcdi_cmd_handle(cmd), cleanup_list);
	}
}
