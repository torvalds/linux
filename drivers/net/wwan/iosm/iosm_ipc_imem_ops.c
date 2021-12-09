// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-21 Intel Corporation.
 */

#include <linux/delay.h>

#include "iosm_ipc_chnl_cfg.h"
#include "iosm_ipc_imem.h"
#include "iosm_ipc_imem_ops.h"
#include "iosm_ipc_port.h"
#include "iosm_ipc_task_queue.h"

/* Open a packet data online channel between the network layer and CP. */
int ipc_imem_sys_wwan_open(struct iosm_imem *ipc_imem, int if_id)
{
	dev_dbg(ipc_imem->dev, "%s if id: %d",
		ipc_imem_phase_get_string(ipc_imem->phase), if_id);

	/* The network interface is only supported in the runtime phase. */
	if (ipc_imem_phase_update(ipc_imem) != IPC_P_RUN) {
		dev_err(ipc_imem->dev, "net:%d : refused phase %s", if_id,
			ipc_imem_phase_get_string(ipc_imem->phase));
		return -EIO;
	}

	return ipc_mux_open_session(ipc_imem->mux, if_id);
}

/* Release a net link to CP. */
void ipc_imem_sys_wwan_close(struct iosm_imem *ipc_imem, int if_id,
			     int channel_id)
{
	if (ipc_imem->mux && if_id >= IP_MUX_SESSION_START &&
	    if_id <= IP_MUX_SESSION_END)
		ipc_mux_close_session(ipc_imem->mux, if_id);
}

/* Tasklet call to do uplink transfer. */
static int ipc_imem_tq_cdev_write(struct iosm_imem *ipc_imem, int arg,
				  void *msg, size_t size)
{
	ipc_imem->ev_cdev_write_pending = false;
	ipc_imem_ul_send(ipc_imem);

	return 0;
}

/* Through tasklet to do sio write. */
static int ipc_imem_call_cdev_write(struct iosm_imem *ipc_imem)
{
	if (ipc_imem->ev_cdev_write_pending)
		return -1;

	ipc_imem->ev_cdev_write_pending = true;

	return ipc_task_queue_send_task(ipc_imem, ipc_imem_tq_cdev_write, 0,
					NULL, 0, false);
}

/* Function for transfer UL data */
int ipc_imem_sys_wwan_transmit(struct iosm_imem *ipc_imem,
			       int if_id, int channel_id, struct sk_buff *skb)
{
	int ret = -EINVAL;

	if (!ipc_imem || channel_id < 0)
		goto out;

	/* Is CP Running? */
	if (ipc_imem->phase != IPC_P_RUN) {
		dev_dbg(ipc_imem->dev, "phase %s transmit",
			ipc_imem_phase_get_string(ipc_imem->phase));
		ret = -EIO;
		goto out;
	}

	/* Route the UL packet through IP MUX Layer */
	ret = ipc_mux_ul_trigger_encode(ipc_imem->mux, if_id, skb);
out:
	return ret;
}

/* Initialize wwan channel */
void ipc_imem_wwan_channel_init(struct iosm_imem *ipc_imem,
				enum ipc_mux_protocol mux_type)
{
	struct ipc_chnl_cfg chnl_cfg = { 0 };

	ipc_imem->cp_version = ipc_mmio_get_cp_version(ipc_imem->mmio);

	/* If modem version is invalid (0xffffffff), do not initialize WWAN. */
	if (ipc_imem->cp_version == -1) {
		dev_err(ipc_imem->dev, "invalid CP version");
		return;
	}

	ipc_chnl_cfg_get(&chnl_cfg, ipc_imem->nr_of_channels);
	ipc_imem_channel_init(ipc_imem, IPC_CTYPE_WWAN, chnl_cfg,
			      IRQ_MOD_OFF);

	/* WWAN registration. */
	ipc_imem->wwan = ipc_wwan_init(ipc_imem, ipc_imem->dev);
	if (!ipc_imem->wwan)
		dev_err(ipc_imem->dev,
			"failed to register the ipc_wwan interfaces");
}

/* Map SKB to DMA for transfer */
static int ipc_imem_map_skb_to_dma(struct iosm_imem *ipc_imem,
				   struct sk_buff *skb)
{
	struct iosm_pcie *ipc_pcie = ipc_imem->pcie;
	char *buf = skb->data;
	int len = skb->len;
	dma_addr_t mapping;
	int ret;

	ret = ipc_pcie_addr_map(ipc_pcie, buf, len, &mapping, DMA_TO_DEVICE);

	if (ret)
		goto err;

	BUILD_BUG_ON(sizeof(*IPC_CB(skb)) > sizeof(skb->cb));

	IPC_CB(skb)->mapping = mapping;
	IPC_CB(skb)->direction = DMA_TO_DEVICE;
	IPC_CB(skb)->len = len;
	IPC_CB(skb)->op_type = (u8)UL_DEFAULT;

err:
	return ret;
}

/* return true if channel is ready for use */
static bool ipc_imem_is_channel_active(struct iosm_imem *ipc_imem,
				       struct ipc_mem_channel *channel)
{
	enum ipc_phase phase;

	/* Update the current operation phase. */
	phase = ipc_imem->phase;

	/* Select the operation depending on the execution stage. */
	switch (phase) {
	case IPC_P_RUN:
	case IPC_P_PSI:
	case IPC_P_EBL:
		break;

	case IPC_P_ROM:
		/* Prepare the PSI image for the CP ROM driver and
		 * suspend the flash app.
		 */
		if (channel->state != IMEM_CHANNEL_RESERVED) {
			dev_err(ipc_imem->dev,
				"ch[%d]:invalid channel state %d,expected %d",
				channel->channel_id, channel->state,
				IMEM_CHANNEL_RESERVED);
			goto channel_unavailable;
		}
		goto channel_available;

	default:
		/* Ignore uplink actions in all other phases. */
		dev_err(ipc_imem->dev, "ch[%d]: confused phase %d",
			channel->channel_id, phase);
		goto channel_unavailable;
	}
	/* Check the full availability of the channel. */
	if (channel->state != IMEM_CHANNEL_ACTIVE) {
		dev_err(ipc_imem->dev, "ch[%d]: confused channel state %d",
			channel->channel_id, channel->state);
		goto channel_unavailable;
	}

channel_available:
	return true;

channel_unavailable:
	return false;
}

/* Release a sio link to CP. */
void ipc_imem_sys_cdev_close(struct iosm_cdev *ipc_cdev)
{
	struct iosm_imem *ipc_imem = ipc_cdev->ipc_imem;
	struct ipc_mem_channel *channel = ipc_cdev->channel;
	enum ipc_phase curr_phase;
	int status = 0;
	u32 tail = 0;

	curr_phase = ipc_imem->phase;

	/* If current phase is IPC_P_OFF or SIO ID is -ve then
	 * channel is already freed. Nothing to do.
	 */
	if (curr_phase == IPC_P_OFF) {
		dev_err(ipc_imem->dev,
			"nothing to do. Current Phase: %s",
			ipc_imem_phase_get_string(curr_phase));
		return;
	}

	if (channel->state == IMEM_CHANNEL_FREE) {
		dev_err(ipc_imem->dev, "ch[%d]: invalid channel state %d",
			channel->channel_id, channel->state);
		return;
	}

	/* If there are any pending TDs then wait for Timeout/Completion before
	 * closing pipe.
	 */
	if (channel->ul_pipe.old_tail != channel->ul_pipe.old_head) {
		ipc_imem->app_notify_ul_pend = 1;

		/* Suspend the user app and wait a certain time for processing
		 * UL Data.
		 */
		status = wait_for_completion_interruptible_timeout
			 (&ipc_imem->ul_pend_sem,
			  msecs_to_jiffies(IPC_PEND_DATA_TIMEOUT));
		if (status == 0) {
			dev_dbg(ipc_imem->dev,
				"Pend data Timeout UL-Pipe:%d Head:%d Tail:%d",
				channel->ul_pipe.pipe_nr,
				channel->ul_pipe.old_head,
				channel->ul_pipe.old_tail);
		}

		ipc_imem->app_notify_ul_pend = 0;
	}

	/* If there are any pending TDs then wait for Timeout/Completion before
	 * closing pipe.
	 */
	ipc_protocol_get_head_tail_index(ipc_imem->ipc_protocol,
					 &channel->dl_pipe, NULL, &tail);

	if (tail != channel->dl_pipe.old_tail) {
		ipc_imem->app_notify_dl_pend = 1;

		/* Suspend the user app and wait a certain time for processing
		 * DL Data.
		 */
		status = wait_for_completion_interruptible_timeout
			 (&ipc_imem->dl_pend_sem,
			  msecs_to_jiffies(IPC_PEND_DATA_TIMEOUT));
		if (status == 0) {
			dev_dbg(ipc_imem->dev,
				"Pend data Timeout DL-Pipe:%d Head:%d Tail:%d",
				channel->dl_pipe.pipe_nr,
				channel->dl_pipe.old_head,
				channel->dl_pipe.old_tail);
		}

		ipc_imem->app_notify_dl_pend = 0;
	}

	/* Due to wait for completion in messages, there is a small window
	 * between closing the pipe and updating the channel is closed. In this
	 * small window there could be HP update from Host Driver. Hence update
	 * the channel state as CLOSING to aviod unnecessary interrupt
	 * towards CP.
	 */
	channel->state = IMEM_CHANNEL_CLOSING;

	ipc_imem_pipe_close(ipc_imem, &channel->ul_pipe);
	ipc_imem_pipe_close(ipc_imem, &channel->dl_pipe);

	ipc_imem_channel_free(channel);
}

/* Open a PORT link to CP and return the channel */
struct ipc_mem_channel *ipc_imem_sys_port_open(struct iosm_imem *ipc_imem,
					       int chl_id, int hp_id)
{
	struct ipc_mem_channel *channel;
	int ch_id;

	/* The PORT interface is only supported in the runtime phase. */
	if (ipc_imem_phase_update(ipc_imem) != IPC_P_RUN) {
		dev_err(ipc_imem->dev, "PORT open refused, phase %s",
			ipc_imem_phase_get_string(ipc_imem->phase));
		return NULL;
	}

	ch_id = ipc_imem_channel_alloc(ipc_imem, chl_id, IPC_CTYPE_CTRL);

	if (ch_id < 0) {
		dev_err(ipc_imem->dev, "reservation of an PORT chnl id failed");
		return NULL;
	}

	channel = ipc_imem_channel_open(ipc_imem, ch_id, hp_id);

	if (!channel) {
		dev_err(ipc_imem->dev, "PORT channel id open failed");
		return NULL;
	}

	return channel;
}

/* transfer skb to modem */
int ipc_imem_sys_cdev_write(struct iosm_cdev *ipc_cdev, struct sk_buff *skb)
{
	struct ipc_mem_channel *channel = ipc_cdev->channel;
	struct iosm_imem *ipc_imem = ipc_cdev->ipc_imem;
	int ret = -EIO;

	if (!ipc_imem_is_channel_active(ipc_imem, channel) ||
	    ipc_imem->phase == IPC_P_OFF_REQ)
		goto out;

	ret = ipc_imem_map_skb_to_dma(ipc_imem, skb);

	if (ret)
		goto out;

	/* Add skb to the uplink skbuf accumulator. */
	skb_queue_tail(&channel->ul_list, skb);

	ret = ipc_imem_call_cdev_write(ipc_imem);

	if (ret) {
		skb_dequeue_tail(&channel->ul_list);
		dev_err(ipc_cdev->dev, "channel id[%d] write failed\n",
			ipc_cdev->channel->channel_id);
	}
out:
	return ret;
}
