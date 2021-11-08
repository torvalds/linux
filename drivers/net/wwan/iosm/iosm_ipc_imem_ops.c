// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-21 Intel Corporation.
 */

#include <linux/delay.h>

#include "iosm_ipc_chnl_cfg.h"
#include "iosm_ipc_devlink.h"
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

/* Open a SIO link to CP and return the channel instance */
struct ipc_mem_channel *ipc_imem_sys_devlink_open(struct iosm_imem *ipc_imem)
{
	struct ipc_mem_channel *channel;
	enum ipc_phase phase;
	int channel_id;

	phase = ipc_imem_phase_update(ipc_imem);
	switch (phase) {
	case IPC_P_OFF:
	case IPC_P_ROM:
		/* Get a channel id as flash id and reserve it. */
		channel_id = ipc_imem_channel_alloc(ipc_imem,
						    IPC_MEM_CTRL_CHL_ID_7,
						    IPC_CTYPE_CTRL);

		if (channel_id < 0) {
			dev_err(ipc_imem->dev,
				"reservation of a flash channel id failed");
			goto error;
		}

		ipc_imem->ipc_devlink->devlink_sio.channel_id = channel_id;
		channel = &ipc_imem->channels[channel_id];

		/* Enqueue chip info data to be read */
		if (ipc_imem_devlink_trigger_chip_info(ipc_imem)) {
			dev_err(ipc_imem->dev, "Enqueue of chip info failed");
			channel->state = IMEM_CHANNEL_FREE;
			goto error;
		}

		return channel;

	case IPC_P_PSI:
	case IPC_P_EBL:
		ipc_imem->cp_version = ipc_mmio_get_cp_version(ipc_imem->mmio);
		if (ipc_imem->cp_version == -1) {
			dev_err(ipc_imem->dev, "invalid CP version");
			goto error;
		}

		channel_id = ipc_imem->ipc_devlink->devlink_sio.channel_id;
		return ipc_imem_channel_open(ipc_imem, channel_id,
					     IPC_HP_CDEV_OPEN);

	default:
		/* CP is in the wrong state (e.g. CRASH or CD_READY) */
		dev_err(ipc_imem->dev, "SIO open refused, phase %d", phase);
	}
error:
	return NULL;
}

/* Release a SIO channel link to CP. */
void ipc_imem_sys_devlink_close(struct iosm_devlink *ipc_devlink)
{
	struct iosm_imem *ipc_imem = ipc_devlink->pcie->imem;
	int boot_check_timeout = BOOT_CHECK_DEFAULT_TIMEOUT;
	enum ipc_mem_exec_stage exec_stage;
	struct ipc_mem_channel *channel;
	enum ipc_phase curr_phase;
	int status = 0;
	u32 tail = 0;

	channel = ipc_imem->ipc_devlink->devlink_sio.channel;
	curr_phase = ipc_imem->phase;
	/* Increase the total wait time to boot_check_timeout */
	do {
		exec_stage = ipc_mmio_get_exec_stage(ipc_imem->mmio);
		if (exec_stage == IPC_MEM_EXEC_STAGE_RUN ||
		    exec_stage == IPC_MEM_EXEC_STAGE_PSI)
			break;
		msleep(20);
		boot_check_timeout -= 20;
	} while (boot_check_timeout > 0);

	/* If there are any pending TDs then wait for Timeout/Completion before
	 * closing pipe.
	 */
	if (channel->ul_pipe.old_tail != channel->ul_pipe.old_head) {
		status = wait_for_completion_interruptible_timeout
			(&ipc_imem->ul_pend_sem,
			 msecs_to_jiffies(IPC_PEND_DATA_TIMEOUT));
		if (status == 0) {
			dev_dbg(ipc_imem->dev,
				"Data Timeout on UL-Pipe:%d Head:%d Tail:%d",
				channel->ul_pipe.pipe_nr,
				channel->ul_pipe.old_head,
				channel->ul_pipe.old_tail);
		}
	}

	ipc_protocol_get_head_tail_index(ipc_imem->ipc_protocol,
					 &channel->dl_pipe, NULL, &tail);

	if (tail != channel->dl_pipe.old_tail) {
		status = wait_for_completion_interruptible_timeout
			(&ipc_imem->dl_pend_sem,
			 msecs_to_jiffies(IPC_PEND_DATA_TIMEOUT));
		if (status == 0) {
			dev_dbg(ipc_imem->dev,
				"Data Timeout on DL-Pipe:%d Head:%d Tail:%d",
				channel->dl_pipe.pipe_nr,
				channel->dl_pipe.old_head,
				channel->dl_pipe.old_tail);
		}
	}

	/* Due to wait for completion in messages, there is a small window
	 * between closing the pipe and updating the channel is closed. In this
	 * small window there could be HP update from Host Driver. Hence update
	 * the channel state as CLOSING to aviod unnecessary interrupt
	 * towards CP.
	 */
	channel->state = IMEM_CHANNEL_CLOSING;
	/* Release the pipe resources */
	ipc_imem_pipe_cleanup(ipc_imem, &channel->ul_pipe);
	ipc_imem_pipe_cleanup(ipc_imem, &channel->dl_pipe);
}

void ipc_imem_sys_devlink_notify_rx(struct iosm_devlink *ipc_devlink,
				    struct sk_buff *skb)
{
	skb_queue_tail(&ipc_devlink->devlink_sio.rx_list, skb);
	complete(&ipc_devlink->devlink_sio.read_sem);
}

/* PSI transfer */
static int ipc_imem_sys_psi_transfer(struct iosm_imem *ipc_imem,
				     struct ipc_mem_channel *channel,
				     unsigned char *buf, int count)
{
	int psi_start_timeout = PSI_START_DEFAULT_TIMEOUT;
	enum ipc_mem_exec_stage exec_stage;

	dma_addr_t mapping = 0;
	int ret;

	ret = ipc_pcie_addr_map(ipc_imem->pcie, buf, count, &mapping,
				DMA_TO_DEVICE);
	if (ret)
		goto pcie_addr_map_fail;

	/* Save the PSI information for the CP ROM driver on the doorbell
	 * scratchpad.
	 */
	ipc_mmio_set_psi_addr_and_size(ipc_imem->mmio, mapping, count);
	ipc_doorbell_fire(ipc_imem->pcie, 0, IPC_MEM_EXEC_STAGE_BOOT);

	ret = wait_for_completion_interruptible_timeout
		(&channel->ul_sem,
		 msecs_to_jiffies(IPC_PSI_TRANSFER_TIMEOUT));

	if (ret <= 0) {
		dev_err(ipc_imem->dev, "Failed PSI transfer to CP, Error-%d",
			ret);
		goto psi_transfer_fail;
	}
	/* If the PSI download fails, return the CP boot ROM exit code */
	if (ipc_imem->rom_exit_code != IMEM_ROM_EXIT_OPEN_EXT &&
	    ipc_imem->rom_exit_code != IMEM_ROM_EXIT_CERT_EXT) {
		ret = (-1) * ((int)ipc_imem->rom_exit_code);
		goto psi_transfer_fail;
	}

	dev_dbg(ipc_imem->dev, "PSI image successfully downloaded");

	/* Wait psi_start_timeout milliseconds until the CP PSI image is
	 * running and updates the execution_stage field with
	 * IPC_MEM_EXEC_STAGE_PSI. Verify the execution stage.
	 */
	do {
		exec_stage = ipc_mmio_get_exec_stage(ipc_imem->mmio);

		if (exec_stage == IPC_MEM_EXEC_STAGE_PSI)
			break;

		msleep(20);
		psi_start_timeout -= 20;
	} while (psi_start_timeout > 0);

	if (exec_stage != IPC_MEM_EXEC_STAGE_PSI)
		goto psi_transfer_fail; /* Unknown status of CP PSI process. */

	ipc_imem->phase = IPC_P_PSI;

	/* Enter the PSI phase. */
	dev_dbg(ipc_imem->dev, "execution_stage[%X] eq. PSI", exec_stage);

	/* Request the RUNNING state from CP and wait until it was reached
	 * or timeout.
	 */
	ipc_imem_ipc_init_check(ipc_imem);

	ret = wait_for_completion_interruptible_timeout
		(&channel->ul_sem, msecs_to_jiffies(IPC_PSI_TRANSFER_TIMEOUT));
	if (ret <= 0) {
		dev_err(ipc_imem->dev,
			"Failed PSI RUNNING state on CP, Error-%d", ret);
		goto psi_transfer_fail;
	}

	if (ipc_mmio_get_ipc_state(ipc_imem->mmio) !=
			IPC_MEM_DEVICE_IPC_RUNNING) {
		dev_err(ipc_imem->dev,
			"ch[%d] %s: unexpected CP IPC state %d, not RUNNING",
			channel->channel_id,
			ipc_imem_phase_get_string(ipc_imem->phase),
			ipc_mmio_get_ipc_state(ipc_imem->mmio));

		goto psi_transfer_fail;
	}

	/* Create the flash channel for the transfer of the images. */
	if (!ipc_imem_sys_devlink_open(ipc_imem)) {
		dev_err(ipc_imem->dev, "can't open flash_channel");
		goto psi_transfer_fail;
	}

	ret = 0;
psi_transfer_fail:
	ipc_pcie_addr_unmap(ipc_imem->pcie, count, mapping, DMA_TO_DEVICE);
pcie_addr_map_fail:
	return ret;
}

int ipc_imem_sys_devlink_write(struct iosm_devlink *ipc_devlink,
			       unsigned char *buf, int count)
{
	struct iosm_imem *ipc_imem = ipc_devlink->pcie->imem;
	struct ipc_mem_channel *channel;
	struct sk_buff *skb;
	dma_addr_t mapping;
	int ret;

	channel = ipc_imem->ipc_devlink->devlink_sio.channel;

	/* In the ROM phase the PSI image is passed to CP about a specific
	 *  shared memory area and doorbell scratchpad directly.
	 */
	if (ipc_imem->phase == IPC_P_ROM) {
		ret = ipc_imem_sys_psi_transfer(ipc_imem, channel, buf, count);
		/* If the PSI transfer fails then send crash
		 * Signature.
		 */
		if (ret > 0)
			ipc_imem_msg_send_feature_set(ipc_imem,
						      IPC_MEM_INBAND_CRASH_SIG,
						      false);
		goto out;
	}

	/* Allocate skb memory for the uplink buffer. */
	skb = ipc_pcie_alloc_skb(ipc_devlink->pcie, count, GFP_KERNEL, &mapping,
				 DMA_TO_DEVICE, 0);
	if (!skb) {
		ret = -ENOMEM;
		goto out;
	}

	memcpy(skb_put(skb, count), buf, count);

	IPC_CB(skb)->op_type = UL_USR_OP_BLOCKED;

	/* Add skb to the uplink skbuf accumulator. */
	skb_queue_tail(&channel->ul_list, skb);

	/* Inform the IPC tasklet to pass uplink IP packets to CP. */
	if (!ipc_imem_call_cdev_write(ipc_imem)) {
		ret = wait_for_completion_interruptible(&channel->ul_sem);

		if (ret < 0) {
			dev_err(ipc_imem->dev,
				"ch[%d] no CP confirmation, status = %d",
				channel->channel_id, ret);
			ipc_pcie_kfree_skb(ipc_devlink->pcie, skb);
			goto out;
		}
	}
	ret = 0;
out:
	return ret;
}

int ipc_imem_sys_devlink_read(struct iosm_devlink *devlink, u8 *data,
			      u32 bytes_to_read, u32 *bytes_read)
{
	struct sk_buff *skb = NULL;
	int rc = 0;

	/* check skb is available in rx_list or wait for skb */
	devlink->devlink_sio.devlink_read_pend = 1;
	while (!skb && !(skb = skb_dequeue(&devlink->devlink_sio.rx_list))) {
		if (!wait_for_completion_interruptible_timeout
				(&devlink->devlink_sio.read_sem,
				 msecs_to_jiffies(IPC_READ_TIMEOUT))) {
			dev_err(devlink->dev, "Read timedout");
			rc =  -ETIMEDOUT;
			goto devlink_read_fail;
		}
	}
	devlink->devlink_sio.devlink_read_pend = 0;
	if (bytes_to_read < skb->len) {
		dev_err(devlink->dev, "Invalid size,expected len %d", skb->len);
		rc = -EINVAL;
		goto devlink_read_fail;
	}
	*bytes_read = skb->len;
	memcpy(data, skb->data, skb->len);

devlink_read_fail:
	ipc_pcie_kfree_skb(devlink->pcie, skb);
	return rc;
}
