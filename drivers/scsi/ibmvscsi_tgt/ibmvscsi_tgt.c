/*******************************************************************************
 * IBM Virtual SCSI Target Driver
 * Copyright (C) 2003-2005 Dave Boutcher (boutcher@us.ibm.com) IBM Corp.
 *			   Santiago Leon (santil@us.ibm.com) IBM Corp.
 *			   Linda Xie (lxie@us.ibm.com) IBM Corp.
 *
 * Copyright (C) 2005-2011 FUJITA Tomonori <tomof@acm.org>
 * Copyright (C) 2010 Nicholas A. Bellinger <nab@kernel.org>
 *
 * Authors: Bryant G. Ly <bryantly@linux.vnet.ibm.com>
 * Authors: Michael Cyr <mikecyr@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 ****************************************************************************/

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/delay.h>

#include <target/target_core_base.h>
#include <target/target_core_fabric.h>

#include <asm/hvcall.h>
#include <asm/vio.h>

#include <scsi/viosrp.h>

#include "ibmvscsi_tgt.h"

#define IBMVSCSIS_VERSION	"v0.2"

#define	INITIAL_SRP_LIMIT	800
#define	DEFAULT_MAX_SECTORS	256

static uint max_vdma_size = MAX_H_COPY_RDMA;

static char system_id[SYS_ID_NAME_LEN] = "";
static char partition_name[PARTITION_NAMELEN] = "UNKNOWN";
static uint partition_number = -1;

/* Adapter list and lock to control it */
static DEFINE_SPINLOCK(ibmvscsis_dev_lock);
static LIST_HEAD(ibmvscsis_dev_list);

static long ibmvscsis_parse_command(struct scsi_info *vscsi,
				    struct viosrp_crq *crq);

static void ibmvscsis_adapter_idle(struct scsi_info *vscsi);

static void ibmvscsis_determine_resid(struct se_cmd *se_cmd,
				      struct srp_rsp *rsp)
{
	u32 residual_count = se_cmd->residual_count;

	if (!residual_count)
		return;

	if (se_cmd->se_cmd_flags & SCF_UNDERFLOW_BIT) {
		if (se_cmd->data_direction == DMA_TO_DEVICE) {
			/* residual data from an underflow write */
			rsp->flags = SRP_RSP_FLAG_DOUNDER;
			rsp->data_out_res_cnt = cpu_to_be32(residual_count);
		} else if (se_cmd->data_direction == DMA_FROM_DEVICE) {
			/* residual data from an underflow read */
			rsp->flags = SRP_RSP_FLAG_DIUNDER;
			rsp->data_in_res_cnt = cpu_to_be32(residual_count);
		}
	} else if (se_cmd->se_cmd_flags & SCF_OVERFLOW_BIT) {
		if (se_cmd->data_direction == DMA_TO_DEVICE) {
			/* residual data from an overflow write */
			rsp->flags = SRP_RSP_FLAG_DOOVER;
			rsp->data_out_res_cnt = cpu_to_be32(residual_count);
		} else if (se_cmd->data_direction == DMA_FROM_DEVICE) {
			/* residual data from an overflow read */
			rsp->flags = SRP_RSP_FLAG_DIOVER;
			rsp->data_in_res_cnt = cpu_to_be32(residual_count);
		}
	}
}

/**
 * connection_broken() - Determine if the connection to the client is good
 * @vscsi:	Pointer to our adapter structure
 *
 * This function attempts to send a ping MAD to the client. If the call to
 * queue the request returns H_CLOSED then the connection has been broken
 * and the function returns TRUE.
 *
 * EXECUTION ENVIRONMENT:
 *	Interrupt or Process environment
 */
static bool connection_broken(struct scsi_info *vscsi)
{
	struct viosrp_crq *crq;
	u64 buffer[2] = { 0, 0 };
	long h_return_code;
	bool rc = false;

	/* create a PING crq */
	crq = (struct viosrp_crq *)&buffer;
	crq->valid = VALID_CMD_RESP_EL;
	crq->format = MESSAGE_IN_CRQ;
	crq->status = PING;

	h_return_code = h_send_crq(vscsi->dds.unit_id,
				   cpu_to_be64(buffer[MSG_HI]),
				   cpu_to_be64(buffer[MSG_LOW]));

	pr_debug("connection_broken: rc %ld\n", h_return_code);

	if (h_return_code == H_CLOSED)
		rc = true;

	return rc;
}

/**
 * ibmvscsis_unregister_command_q() - Helper Function-Unregister Command Queue
 * @vscsi:	Pointer to our adapter structure
 *
 * This function calls h_free_q then frees the interrupt bit etc.
 * It must release the lock before doing so because of the time it can take
 * for h_free_crq in PHYP
 * NOTE: the caller must make sure that state and or flags will prevent
 *	 interrupt handler from scheduling work.
 * NOTE: anyone calling this function may need to set the CRQ_CLOSED flag
 *	 we can't do it here, because we don't have the lock
 *
 * EXECUTION ENVIRONMENT:
 *	Process level
 */
static long ibmvscsis_unregister_command_q(struct scsi_info *vscsi)
{
	long qrc;
	long rc = ADAPT_SUCCESS;
	int ticks = 0;

	do {
		qrc = h_free_crq(vscsi->dds.unit_id);
		switch (qrc) {
		case H_SUCCESS:
			break;

		case H_HARDWARE:
		case H_PARAMETER:
			dev_err(&vscsi->dev, "unregister_command_q: error from h_free_crq %ld\n",
				qrc);
			rc = ERROR;
			break;

		case H_BUSY:
		case H_LONG_BUSY_ORDER_1_MSEC:
			/* msleep not good for small values */
			usleep_range(1000, 2000);
			ticks += 1;
			break;
		case H_LONG_BUSY_ORDER_10_MSEC:
			usleep_range(10000, 20000);
			ticks += 10;
			break;
		case H_LONG_BUSY_ORDER_100_MSEC:
			msleep(100);
			ticks += 100;
			break;
		case H_LONG_BUSY_ORDER_1_SEC:
			ssleep(1);
			ticks += 1000;
			break;
		case H_LONG_BUSY_ORDER_10_SEC:
			ssleep(10);
			ticks += 10000;
			break;
		case H_LONG_BUSY_ORDER_100_SEC:
			ssleep(100);
			ticks += 100000;
			break;
		default:
			dev_err(&vscsi->dev, "unregister_command_q: unknown error %ld from h_free_crq\n",
				qrc);
			rc = ERROR;
			break;
		}

		/*
		 * dont wait more then 300 seconds
		 * ticks are in milliseconds more or less
		 */
		if (ticks > 300000 && qrc != H_SUCCESS) {
			rc = ERROR;
			dev_err(&vscsi->dev, "Excessive wait for h_free_crq\n");
		}
	} while (qrc != H_SUCCESS && rc == ADAPT_SUCCESS);

	pr_debug("Freeing CRQ: phyp rc %ld, rc %ld\n", qrc, rc);

	return rc;
}

/**
 * ibmvscsis_delete_client_info() - Helper function to Delete Client Info
 * @vscsi:	Pointer to our adapter structure
 * @client_closed:	True if client closed its queue
 *
 * Deletes information specific to the client when the client goes away
 *
 * EXECUTION ENVIRONMENT:
 *	Interrupt or Process
 */
static void ibmvscsis_delete_client_info(struct scsi_info *vscsi,
					 bool client_closed)
{
	vscsi->client_cap = 0;

	/*
	 * Some things we don't want to clear if we're closing the queue,
	 * because some clients don't resend the host handshake when they
	 * get a transport event.
	 */
	if (client_closed)
		vscsi->client_data.os_type = 0;
}

/**
 * ibmvscsis_free_command_q() - Free Command Queue
 * @vscsi:	Pointer to our adapter structure
 *
 * This function calls unregister_command_q, then clears interrupts and
 * any pending interrupt acknowledgments associated with the command q.
 * It also clears memory if there is no error.
 *
 * PHYP did not meet the PAPR architecture so that we must give up the
 * lock. This causes a timing hole regarding state change.  To close the
 * hole this routine does accounting on any change that occurred during
 * the time the lock is not held.
 * NOTE: must give up and then acquire the interrupt lock, the caller must
 *	 make sure that state and or flags will prevent interrupt handler from
 *	 scheduling work.
 *
 * EXECUTION ENVIRONMENT:
 *	Process level, interrupt lock is held
 */
static long ibmvscsis_free_command_q(struct scsi_info *vscsi)
{
	int bytes;
	u32 flags_under_lock;
	u16 state_under_lock;
	long rc = ADAPT_SUCCESS;

	if (!(vscsi->flags & CRQ_CLOSED)) {
		vio_disable_interrupts(vscsi->dma_dev);

		state_under_lock = vscsi->new_state;
		flags_under_lock = vscsi->flags;
		vscsi->phyp_acr_state = 0;
		vscsi->phyp_acr_flags = 0;

		spin_unlock_bh(&vscsi->intr_lock);
		rc = ibmvscsis_unregister_command_q(vscsi);
		spin_lock_bh(&vscsi->intr_lock);

		if (state_under_lock != vscsi->new_state)
			vscsi->phyp_acr_state = vscsi->new_state;

		vscsi->phyp_acr_flags = ((~flags_under_lock) & vscsi->flags);

		if (rc == ADAPT_SUCCESS) {
			bytes = vscsi->cmd_q.size * PAGE_SIZE;
			memset(vscsi->cmd_q.base_addr, 0, bytes);
			vscsi->cmd_q.index = 0;
			vscsi->flags |= CRQ_CLOSED;

			ibmvscsis_delete_client_info(vscsi, false);
		}

		pr_debug("free_command_q: flags 0x%x, state 0x%hx, acr_flags 0x%x, acr_state 0x%hx\n",
			 vscsi->flags, vscsi->state, vscsi->phyp_acr_flags,
			 vscsi->phyp_acr_state);
	}
	return rc;
}

/**
 * ibmvscsis_cmd_q_dequeue() - Get valid Command element
 * @mask:	Mask to use in case index wraps
 * @current_index:	Current index into command queue
 * @base_addr:	Pointer to start of command queue
 *
 * Returns a pointer to a valid command element or NULL, if the command
 * queue is empty
 *
 * EXECUTION ENVIRONMENT:
 *	Interrupt environment, interrupt lock held
 */
static struct viosrp_crq *ibmvscsis_cmd_q_dequeue(uint mask,
						  uint *current_index,
						  struct viosrp_crq *base_addr)
{
	struct viosrp_crq *ptr;

	ptr = base_addr + *current_index;

	if (ptr->valid) {
		*current_index = (*current_index + 1) & mask;
		dma_rmb();
	} else {
		ptr = NULL;
	}

	return ptr;
}

/**
 * ibmvscsis_send_init_message() - send initialize message to the client
 * @vscsi:	Pointer to our adapter structure
 * @format:	Which Init Message format to send
 *
 * EXECUTION ENVIRONMENT:
 *	Interrupt environment interrupt lock held
 */
static long ibmvscsis_send_init_message(struct scsi_info *vscsi, u8 format)
{
	struct viosrp_crq *crq;
	u64 buffer[2] = { 0, 0 };
	long rc;

	crq = (struct viosrp_crq *)&buffer;
	crq->valid = VALID_INIT_MSG;
	crq->format = format;
	rc = h_send_crq(vscsi->dds.unit_id, cpu_to_be64(buffer[MSG_HI]),
			cpu_to_be64(buffer[MSG_LOW]));

	return rc;
}

/**
 * ibmvscsis_check_init_msg() - Check init message valid
 * @vscsi:	Pointer to our adapter structure
 * @format:	Pointer to return format of Init Message, if any.
 *		Set to UNUSED_FORMAT if no Init Message in queue.
 *
 * Checks if an initialize message was queued by the initiatior
 * after the queue was created and before the interrupt was enabled.
 *
 * EXECUTION ENVIRONMENT:
 *	Process level only, interrupt lock held
 */
static long ibmvscsis_check_init_msg(struct scsi_info *vscsi, uint *format)
{
	struct viosrp_crq *crq;
	long rc = ADAPT_SUCCESS;

	crq = ibmvscsis_cmd_q_dequeue(vscsi->cmd_q.mask, &vscsi->cmd_q.index,
				      vscsi->cmd_q.base_addr);
	if (!crq) {
		*format = (uint)UNUSED_FORMAT;
	} else if (crq->valid == VALID_INIT_MSG && crq->format == INIT_MSG) {
		*format = (uint)INIT_MSG;
		crq->valid = INVALIDATE_CMD_RESP_EL;
		dma_rmb();

		/*
		 * the caller has ensured no initialize message was
		 * sent after the queue was
		 * created so there should be no other message on the queue.
		 */
		crq = ibmvscsis_cmd_q_dequeue(vscsi->cmd_q.mask,
					      &vscsi->cmd_q.index,
					      vscsi->cmd_q.base_addr);
		if (crq) {
			*format = (uint)(crq->format);
			rc = ERROR;
			crq->valid = INVALIDATE_CMD_RESP_EL;
			dma_rmb();
		}
	} else {
		*format = (uint)(crq->format);
		rc = ERROR;
		crq->valid = INVALIDATE_CMD_RESP_EL;
		dma_rmb();
	}

	return rc;
}

/**
 * ibmvscsis_disconnect() - Helper function to disconnect
 * @work:	Pointer to work_struct, gives access to our adapter structure
 *
 * An error has occurred or the driver received a Transport event,
 * and the driver is requesting that the command queue be de-registered
 * in a safe manner. If there is no outstanding I/O then we can stop the
 * queue. If we are restarting the queue it will be reflected in the
 * the state of the adapter.
 *
 * EXECUTION ENVIRONMENT:
 *	Process environment
 */
static void ibmvscsis_disconnect(struct work_struct *work)
{
	struct scsi_info *vscsi = container_of(work, struct scsi_info,
					       proc_work);
	u16 new_state;
	bool wait_idle = false;

	spin_lock_bh(&vscsi->intr_lock);
	new_state = vscsi->new_state;
	vscsi->new_state = 0;

	pr_debug("disconnect: flags 0x%x, state 0x%hx\n", vscsi->flags,
		 vscsi->state);

	/*
	 * check which state we are in and see if we
	 * should transitition to the new state
	 */
	switch (vscsi->state) {
	/* Should never be called while in this state. */
	case NO_QUEUE:
	/*
	 * Can never transition from this state;
	 * igonore errors and logout.
	 */
	case UNCONFIGURING:
		break;

	/* can transition from this state to UNCONFIGURING */
	case ERR_DISCONNECT:
		if (new_state == UNCONFIGURING)
			vscsi->state = new_state;
		break;

	/*
	 * Can transition from this state to to unconfiguring
	 * or err disconnect.
	 */
	case ERR_DISCONNECT_RECONNECT:
		switch (new_state) {
		case UNCONFIGURING:
		case ERR_DISCONNECT:
			vscsi->state = new_state;
			break;

		case WAIT_IDLE:
			break;
		default:
			break;
		}
		break;

	/* can transition from this state to UNCONFIGURING */
	case ERR_DISCONNECTED:
		if (new_state == UNCONFIGURING)
			vscsi->state = new_state;
		break;

	case WAIT_ENABLED:
		switch (new_state) {
		case UNCONFIGURING:
			vscsi->state = new_state;
			vscsi->flags |= RESPONSE_Q_DOWN;
			vscsi->flags &= ~(SCHEDULE_DISCONNECT |
					  DISCONNECT_SCHEDULED);
			dma_rmb();
			if (vscsi->flags & CFG_SLEEPING) {
				vscsi->flags &= ~CFG_SLEEPING;
				complete(&vscsi->unconfig);
			}
			break;

		/* should never happen */
		case ERR_DISCONNECT:
		case ERR_DISCONNECT_RECONNECT:
		case WAIT_IDLE:
			dev_err(&vscsi->dev, "disconnect: invalid state %d for WAIT_IDLE\n",
				vscsi->state);
			break;
		}
		break;

	case WAIT_IDLE:
		switch (new_state) {
		case UNCONFIGURING:
			vscsi->flags |= RESPONSE_Q_DOWN;
			vscsi->state = new_state;
			vscsi->flags &= ~(SCHEDULE_DISCONNECT |
					  DISCONNECT_SCHEDULED);
			ibmvscsis_free_command_q(vscsi);
			break;
		case ERR_DISCONNECT:
		case ERR_DISCONNECT_RECONNECT:
			vscsi->state = new_state;
			break;
		}
		break;

	/*
	 * Initiator has not done a successful srp login
	 * or has done a successful srp logout ( adapter was not
	 * busy). In the first case there can be responses queued
	 * waiting for space on the initiators response queue (MAD)
	 * The second case the adapter is idle. Assume the worse case,
	 * i.e. the second case.
	 */
	case WAIT_CONNECTION:
	case CONNECTED:
	case SRP_PROCESSING:
		wait_idle = true;
		vscsi->state = new_state;
		break;

	/* can transition from this state to UNCONFIGURING */
	case UNDEFINED:
		if (new_state == UNCONFIGURING)
			vscsi->state = new_state;
		break;
	default:
		break;
	}

	if (wait_idle) {
		pr_debug("disconnect start wait, active %d, sched %d\n",
			 (int)list_empty(&vscsi->active_q),
			 (int)list_empty(&vscsi->schedule_q));
		if (!list_empty(&vscsi->active_q) ||
		    !list_empty(&vscsi->schedule_q)) {
			vscsi->flags |= WAIT_FOR_IDLE;
			pr_debug("disconnect flags 0x%x\n", vscsi->flags);
			/*
			 * This routine is can not be called with the interrupt
			 * lock held.
			 */
			spin_unlock_bh(&vscsi->intr_lock);
			wait_for_completion(&vscsi->wait_idle);
			spin_lock_bh(&vscsi->intr_lock);
		}
		pr_debug("disconnect stop wait\n");

		ibmvscsis_adapter_idle(vscsi);
	}

	spin_unlock_bh(&vscsi->intr_lock);
}

/**
 * ibmvscsis_post_disconnect() - Schedule the disconnect
 * @vscsi:	Pointer to our adapter structure
 * @new_state:	State to move to after disconnecting
 * @flag_bits:	Flags to turn on in adapter structure
 *
 * If it's already been scheduled, then see if we need to "upgrade"
 * the new state (if the one passed in is more "severe" than the
 * previous one).
 *
 * PRECONDITION:
 *	interrupt lock is held
 */
static void ibmvscsis_post_disconnect(struct scsi_info *vscsi, uint new_state,
				      uint flag_bits)
{
	uint state;

	/* check the validity of the new state */
	switch (new_state) {
	case UNCONFIGURING:
	case ERR_DISCONNECT:
	case ERR_DISCONNECT_RECONNECT:
	case WAIT_IDLE:
		break;

	default:
		dev_err(&vscsi->dev, "post_disconnect: Invalid new state %d\n",
			new_state);
		return;
	}

	vscsi->flags |= flag_bits;

	pr_debug("post_disconnect: new_state 0x%x, flag_bits 0x%x, vscsi->flags 0x%x, state %hx\n",
		 new_state, flag_bits, vscsi->flags, vscsi->state);

	if (!(vscsi->flags & (DISCONNECT_SCHEDULED | SCHEDULE_DISCONNECT))) {
		vscsi->flags |= SCHEDULE_DISCONNECT;
		vscsi->new_state = new_state;

		INIT_WORK(&vscsi->proc_work, ibmvscsis_disconnect);
		(void)queue_work(vscsi->work_q, &vscsi->proc_work);
	} else {
		if (vscsi->new_state)
			state = vscsi->new_state;
		else
			state = vscsi->state;

		switch (state) {
		case NO_QUEUE:
		case UNCONFIGURING:
			break;

		case ERR_DISCONNECTED:
		case ERR_DISCONNECT:
		case UNDEFINED:
			if (new_state == UNCONFIGURING)
				vscsi->new_state = new_state;
			break;

		case ERR_DISCONNECT_RECONNECT:
			switch (new_state) {
			case UNCONFIGURING:
			case ERR_DISCONNECT:
				vscsi->new_state = new_state;
				break;
			default:
				break;
			}
			break;

		case WAIT_ENABLED:
		case WAIT_IDLE:
		case WAIT_CONNECTION:
		case CONNECTED:
		case SRP_PROCESSING:
			vscsi->new_state = new_state;
			break;

		default:
			break;
		}
	}

	pr_debug("Leaving post_disconnect: flags 0x%x, new_state 0x%x\n",
		 vscsi->flags, vscsi->new_state);
}

/**
 * ibmvscsis_handle_init_compl_msg() - Respond to an Init Complete Message
 * @vscsi:	Pointer to our adapter structure
 *
 * Must be called with interrupt lock held.
 */
static long ibmvscsis_handle_init_compl_msg(struct scsi_info *vscsi)
{
	long rc = ADAPT_SUCCESS;

	switch (vscsi->state) {
	case NO_QUEUE:
	case ERR_DISCONNECT:
	case ERR_DISCONNECT_RECONNECT:
	case ERR_DISCONNECTED:
	case UNCONFIGURING:
	case UNDEFINED:
		rc = ERROR;
		break;

	case WAIT_CONNECTION:
		vscsi->state = CONNECTED;
		break;

	case WAIT_IDLE:
	case SRP_PROCESSING:
	case CONNECTED:
	case WAIT_ENABLED:
	default:
		rc = ERROR;
		dev_err(&vscsi->dev, "init_msg: invalid state %d to get init compl msg\n",
			vscsi->state);
		ibmvscsis_post_disconnect(vscsi, ERR_DISCONNECT_RECONNECT, 0);
		break;
	}

	return rc;
}

/**
 * ibmvscsis_handle_init_msg() - Respond to an Init Message
 * @vscsi:	Pointer to our adapter structure
 *
 * Must be called with interrupt lock held.
 */
static long ibmvscsis_handle_init_msg(struct scsi_info *vscsi)
{
	long rc = ADAPT_SUCCESS;

	switch (vscsi->state) {
	case WAIT_CONNECTION:
		rc = ibmvscsis_send_init_message(vscsi, INIT_COMPLETE_MSG);
		switch (rc) {
		case H_SUCCESS:
			vscsi->state = CONNECTED;
			break;

		case H_PARAMETER:
			dev_err(&vscsi->dev, "init_msg: failed to send, rc %ld\n",
				rc);
			ibmvscsis_post_disconnect(vscsi, ERR_DISCONNECT, 0);
			break;

		case H_DROPPED:
			dev_err(&vscsi->dev, "init_msg: failed to send, rc %ld\n",
				rc);
			rc = ERROR;
			ibmvscsis_post_disconnect(vscsi,
						  ERR_DISCONNECT_RECONNECT, 0);
			break;

		case H_CLOSED:
			pr_warn("init_msg: failed to send, rc %ld\n", rc);
			rc = 0;
			break;
		}
		break;

	case UNDEFINED:
		rc = ERROR;
		break;

	case UNCONFIGURING:
		break;

	case WAIT_ENABLED:
	case CONNECTED:
	case SRP_PROCESSING:
	case WAIT_IDLE:
	case NO_QUEUE:
	case ERR_DISCONNECT:
	case ERR_DISCONNECT_RECONNECT:
	case ERR_DISCONNECTED:
	default:
		rc = ERROR;
		dev_err(&vscsi->dev, "init_msg: invalid state %d to get init msg\n",
			vscsi->state);
		ibmvscsis_post_disconnect(vscsi, ERR_DISCONNECT_RECONNECT, 0);
		break;
	}

	return rc;
}

/**
 * ibmvscsis_init_msg() - Respond to an init message
 * @vscsi:	Pointer to our adapter structure
 * @crq:	Pointer to CRQ element containing the Init Message
 *
 * EXECUTION ENVIRONMENT:
 *	Interrupt, interrupt lock held
 */
static long ibmvscsis_init_msg(struct scsi_info *vscsi, struct viosrp_crq *crq)
{
	long rc = ADAPT_SUCCESS;

	pr_debug("init_msg: state 0x%hx\n", vscsi->state);

	rc = h_vioctl(vscsi->dds.unit_id, H_GET_PARTNER_INFO,
		      (u64)vscsi->map_ioba | ((u64)PAGE_SIZE << 32), 0, 0, 0,
		      0);
	if (rc == H_SUCCESS) {
		vscsi->client_data.partition_number =
			be64_to_cpu(*(u64 *)vscsi->map_buf);
		pr_debug("init_msg, part num %d\n",
			 vscsi->client_data.partition_number);
	} else {
		pr_debug("init_msg h_vioctl rc %ld\n", rc);
		rc = ADAPT_SUCCESS;
	}

	if (crq->format == INIT_MSG) {
		rc = ibmvscsis_handle_init_msg(vscsi);
	} else if (crq->format == INIT_COMPLETE_MSG) {
		rc = ibmvscsis_handle_init_compl_msg(vscsi);
	} else {
		rc = ERROR;
		dev_err(&vscsi->dev, "init_msg: invalid format %d\n",
			(uint)crq->format);
		ibmvscsis_post_disconnect(vscsi, ERR_DISCONNECT_RECONNECT, 0);
	}

	return rc;
}

/**
 * ibmvscsis_establish_new_q() - Establish new CRQ queue
 * @vscsi:	Pointer to our adapter structure
 *
 * Must be called with interrupt lock held.
 */
static long ibmvscsis_establish_new_q(struct scsi_info *vscsi)
{
	long rc = ADAPT_SUCCESS;
	uint format;

	vscsi->flags &= PRESERVE_FLAG_FIELDS;
	vscsi->rsp_q_timer.timer_pops = 0;
	vscsi->debit = 0;
	vscsi->credit = 0;

	rc = vio_enable_interrupts(vscsi->dma_dev);
	if (rc) {
		pr_warn("establish_new_q: failed to enable interrupts, rc %ld\n",
			rc);
		return rc;
	}

	rc = ibmvscsis_check_init_msg(vscsi, &format);
	if (rc) {
		dev_err(&vscsi->dev, "establish_new_q: check_init_msg failed, rc %ld\n",
			rc);
		return rc;
	}

	if (format == UNUSED_FORMAT) {
		rc = ibmvscsis_send_init_message(vscsi, INIT_MSG);
		switch (rc) {
		case H_SUCCESS:
		case H_DROPPED:
		case H_CLOSED:
			rc = ADAPT_SUCCESS;
			break;

		case H_PARAMETER:
		case H_HARDWARE:
			break;

		default:
			vscsi->state = UNDEFINED;
			rc = H_HARDWARE;
			break;
		}
	} else if (format == INIT_MSG) {
		rc = ibmvscsis_handle_init_msg(vscsi);
	}

	return rc;
}

/**
 * ibmvscsis_reset_queue() - Reset CRQ Queue
 * @vscsi:	Pointer to our adapter structure
 *
 * This function calls h_free_q and then calls h_reg_q and does all
 * of the bookkeeping to get us back to where we can communicate.
 *
 * Actually, we don't always call h_free_crq.  A problem was discovered
 * where one partition would close and reopen his queue, which would
 * cause his partner to get a transport event, which would cause him to
 * close and reopen his queue, which would cause the original partition
 * to get a transport event, etc., etc.  To prevent this, we don't
 * actually close our queue if the client initiated the reset, (i.e.
 * either we got a transport event or we have detected that the client's
 * queue is gone)
 *
 * EXECUTION ENVIRONMENT:
 *	Process environment, called with interrupt lock held
 */
static void ibmvscsis_reset_queue(struct scsi_info *vscsi)
{
	int bytes;
	long rc = ADAPT_SUCCESS;

	pr_debug("reset_queue: flags 0x%x\n", vscsi->flags);

	/* don't reset, the client did it for us */
	if (vscsi->flags & (CLIENT_FAILED | TRANS_EVENT)) {
		vscsi->flags &= PRESERVE_FLAG_FIELDS;
		vscsi->rsp_q_timer.timer_pops = 0;
		vscsi->debit = 0;
		vscsi->credit = 0;
		vscsi->state = WAIT_CONNECTION;
		vio_enable_interrupts(vscsi->dma_dev);
	} else {
		rc = ibmvscsis_free_command_q(vscsi);
		if (rc == ADAPT_SUCCESS) {
			vscsi->state = WAIT_CONNECTION;

			bytes = vscsi->cmd_q.size * PAGE_SIZE;
			rc = h_reg_crq(vscsi->dds.unit_id,
				       vscsi->cmd_q.crq_token, bytes);
			if (rc == H_CLOSED || rc == H_SUCCESS) {
				rc = ibmvscsis_establish_new_q(vscsi);
			}

			if (rc != ADAPT_SUCCESS) {
				pr_debug("reset_queue: reg_crq rc %ld\n", rc);

				vscsi->state = ERR_DISCONNECTED;
				vscsi->flags |= RESPONSE_Q_DOWN;
				ibmvscsis_free_command_q(vscsi);
			}
		} else {
			vscsi->state = ERR_DISCONNECTED;
			vscsi->flags |= RESPONSE_Q_DOWN;
		}
	}
}

/**
 * ibmvscsis_free_cmd_resources() - Free command resources
 * @vscsi:	Pointer to our adapter structure
 * @cmd:	Command which is not longer in use
 *
 * Must be called with interrupt lock held.
 */
static void ibmvscsis_free_cmd_resources(struct scsi_info *vscsi,
					 struct ibmvscsis_cmd *cmd)
{
	struct iu_entry *iue = cmd->iue;

	switch (cmd->type) {
	case TASK_MANAGEMENT:
	case SCSI_CDB:
		/*
		 * When the queue goes down this value is cleared, so it
		 * cannot be cleared in this general purpose function.
		 */
		if (vscsi->debit)
			vscsi->debit -= 1;
		break;
	case ADAPTER_MAD:
		vscsi->flags &= ~PROCESSING_MAD;
		break;
	case UNSET_TYPE:
		break;
	default:
		dev_err(&vscsi->dev, "free_cmd_resources unknown type %d\n",
			cmd->type);
		break;
	}

	cmd->iue = NULL;
	list_add_tail(&cmd->list, &vscsi->free_cmd);
	srp_iu_put(iue);

	if (list_empty(&vscsi->active_q) && list_empty(&vscsi->schedule_q) &&
	    list_empty(&vscsi->waiting_rsp) && (vscsi->flags & WAIT_FOR_IDLE)) {
		vscsi->flags &= ~WAIT_FOR_IDLE;
		complete(&vscsi->wait_idle);
	}
}

/**
 * ibmvscsis_trans_event() - Handle a Transport Event
 * @vscsi:	Pointer to our adapter structure
 * @crq:	Pointer to CRQ entry containing the Transport Event
 *
 * Do the logic to close the I_T nexus.  This function may not
 * behave to specification.
 *
 * EXECUTION ENVIRONMENT:
 *	Interrupt, interrupt lock held
 */
static long ibmvscsis_trans_event(struct scsi_info *vscsi,
				  struct viosrp_crq *crq)
{
	long rc = ADAPT_SUCCESS;

	pr_debug("trans_event: format %d, flags 0x%x, state 0x%hx\n",
		 (int)crq->format, vscsi->flags, vscsi->state);

	switch (crq->format) {
	case MIGRATED:
	case PARTNER_FAILED:
	case PARTNER_DEREGISTER:
		ibmvscsis_delete_client_info(vscsi, true);
		break;

	default:
		rc = ERROR;
		dev_err(&vscsi->dev, "trans_event: invalid format %d\n",
			(uint)crq->format);
		ibmvscsis_post_disconnect(vscsi, ERR_DISCONNECT,
					  RESPONSE_Q_DOWN);
		break;
	}

	if (rc == ADAPT_SUCCESS) {
		switch (vscsi->state) {
		case NO_QUEUE:
		case ERR_DISCONNECTED:
		case UNDEFINED:
			break;

		case UNCONFIGURING:
			vscsi->flags |= (RESPONSE_Q_DOWN | TRANS_EVENT);
			break;

		case WAIT_ENABLED:
			break;

		case WAIT_CONNECTION:
			break;

		case CONNECTED:
			ibmvscsis_post_disconnect(vscsi, WAIT_IDLE,
						  (RESPONSE_Q_DOWN |
						   TRANS_EVENT));
			break;

		case SRP_PROCESSING:
			if ((vscsi->debit > 0) ||
			    !list_empty(&vscsi->schedule_q) ||
			    !list_empty(&vscsi->waiting_rsp) ||
			    !list_empty(&vscsi->active_q)) {
				pr_debug("debit %d, sched %d, wait %d, active %d\n",
					 vscsi->debit,
					 (int)list_empty(&vscsi->schedule_q),
					 (int)list_empty(&vscsi->waiting_rsp),
					 (int)list_empty(&vscsi->active_q));
				pr_warn("connection lost with outstanding work\n");
			} else {
				pr_debug("trans_event: SRP Processing, but no outstanding work\n");
			}

			ibmvscsis_post_disconnect(vscsi, WAIT_IDLE,
						  (RESPONSE_Q_DOWN |
						   TRANS_EVENT));
			break;

		case ERR_DISCONNECT:
		case ERR_DISCONNECT_RECONNECT:
		case WAIT_IDLE:
			vscsi->flags |= (RESPONSE_Q_DOWN | TRANS_EVENT);
			break;
		}
	}

	rc = vscsi->flags & SCHEDULE_DISCONNECT;

	pr_debug("Leaving trans_event: flags 0x%x, state 0x%hx, rc %ld\n",
		 vscsi->flags, vscsi->state, rc);

	return rc;
}

/**
 * ibmvscsis_poll_cmd_q() - Poll Command Queue
 * @vscsi:	Pointer to our adapter structure
 *
 * Called to handle command elements that may have arrived while
 * interrupts were disabled.
 *
 * EXECUTION ENVIRONMENT:
 *	intr_lock must be held
 */
static void ibmvscsis_poll_cmd_q(struct scsi_info *vscsi)
{
	struct viosrp_crq *crq;
	long rc;
	bool ack = true;
	volatile u8 valid;

	pr_debug("poll_cmd_q: flags 0x%x, state 0x%hx, q index %ud\n",
		 vscsi->flags, vscsi->state, vscsi->cmd_q.index);

	rc = vscsi->flags & SCHEDULE_DISCONNECT;
	crq = vscsi->cmd_q.base_addr + vscsi->cmd_q.index;
	valid = crq->valid;
	dma_rmb();

	while (valid) {
poll_work:
		vscsi->cmd_q.index =
			(vscsi->cmd_q.index + 1) & vscsi->cmd_q.mask;

		if (!rc) {
			rc = ibmvscsis_parse_command(vscsi, crq);
		} else {
			if ((uint)crq->valid == VALID_TRANS_EVENT) {
				/*
				 * must service the transport layer events even
				 * in an error state, dont break out until all
				 * the consecutive transport events have been
				 * processed
				 */
				rc = ibmvscsis_trans_event(vscsi, crq);
			} else if (vscsi->flags & TRANS_EVENT) {
				/*
				 * if a tranport event has occurred leave
				 * everything but transport events on the queue
				 */
				pr_debug("poll_cmd_q, ignoring\n");

				/*
				 * need to decrement the queue index so we can
				 * look at the elment again
				 */
				if (vscsi->cmd_q.index)
					vscsi->cmd_q.index -= 1;
				else
					/*
					 * index is at 0 it just wrapped.
					 * have it index last element in q
					 */
					vscsi->cmd_q.index = vscsi->cmd_q.mask;
				break;
			}
		}

		crq->valid = INVALIDATE_CMD_RESP_EL;

		crq = vscsi->cmd_q.base_addr + vscsi->cmd_q.index;
		valid = crq->valid;
		dma_rmb();
	}

	if (!rc) {
		if (ack) {
			vio_enable_interrupts(vscsi->dma_dev);
			ack = false;
			pr_debug("poll_cmd_q, reenabling interrupts\n");
		}
		valid = crq->valid;
		dma_rmb();
		if (valid)
			goto poll_work;
	}

	pr_debug("Leaving poll_cmd_q: rc %ld\n", rc);
}

/**
 * ibmvscsis_free_cmd_qs() - Free elements in queue
 * @vscsi:	Pointer to our adapter structure
 *
 * Free all of the elements on all queues that are waiting for
 * whatever reason.
 *
 * PRECONDITION:
 *	Called with interrupt lock held
 */
static void ibmvscsis_free_cmd_qs(struct scsi_info *vscsi)
{
	struct ibmvscsis_cmd *cmd, *nxt;

	pr_debug("free_cmd_qs: waiting_rsp empty %d, timer starter %d\n",
		 (int)list_empty(&vscsi->waiting_rsp),
		 vscsi->rsp_q_timer.started);

	list_for_each_entry_safe(cmd, nxt, &vscsi->waiting_rsp, list) {
		list_del(&cmd->list);
		ibmvscsis_free_cmd_resources(vscsi, cmd);
	}
}

/**
 * ibmvscsis_get_free_cmd() - Get free command from list
 * @vscsi:	Pointer to our adapter structure
 *
 * Must be called with interrupt lock held.
 */
static struct ibmvscsis_cmd *ibmvscsis_get_free_cmd(struct scsi_info *vscsi)
{
	struct ibmvscsis_cmd *cmd = NULL;
	struct iu_entry *iue;

	iue = srp_iu_get(&vscsi->target);
	if (iue) {
		cmd = list_first_entry_or_null(&vscsi->free_cmd,
					       struct ibmvscsis_cmd, list);
		if (cmd) {
			list_del(&cmd->list);
			cmd->iue = iue;
			cmd->type = UNSET_TYPE;
			memset(&cmd->se_cmd, 0, sizeof(cmd->se_cmd));
		} else {
			srp_iu_put(iue);
		}
	}

	return cmd;
}

/**
 * ibmvscsis_adapter_idle() - Helper function to handle idle adapter
 * @vscsi:	Pointer to our adapter structure
 *
 * This function is called when the adapter is idle when the driver
 * is attempting to clear an error condition.
 * The adapter is considered busy if any of its cmd queues
 * are non-empty. This function can be invoked
 * from the off level disconnect function.
 *
 * EXECUTION ENVIRONMENT:
 *	Process environment called with interrupt lock held
 */
static void ibmvscsis_adapter_idle(struct scsi_info *vscsi)
{
	int free_qs = false;

	pr_debug("adapter_idle: flags 0x%x, state 0x%hx\n", vscsi->flags,
		 vscsi->state);

	/* Only need to free qs if we're disconnecting from client */
	if (vscsi->state != WAIT_CONNECTION || vscsi->flags & TRANS_EVENT)
		free_qs = true;

	switch (vscsi->state) {
	case UNCONFIGURING:
		ibmvscsis_free_command_q(vscsi);
		dma_rmb();
		isync();
		if (vscsi->flags & CFG_SLEEPING) {
			vscsi->flags &= ~CFG_SLEEPING;
			complete(&vscsi->unconfig);
		}
		break;
	case ERR_DISCONNECT_RECONNECT:
		ibmvscsis_reset_queue(vscsi);
		pr_debug("adapter_idle, disc_rec: flags 0x%x\n", vscsi->flags);
		break;

	case ERR_DISCONNECT:
		ibmvscsis_free_command_q(vscsi);
		vscsi->flags &= ~(SCHEDULE_DISCONNECT | DISCONNECT_SCHEDULED);
		vscsi->flags |= RESPONSE_Q_DOWN;
		if (vscsi->tport.enabled)
			vscsi->state = ERR_DISCONNECTED;
		else
			vscsi->state = WAIT_ENABLED;
		pr_debug("adapter_idle, disc: flags 0x%x, state 0x%hx\n",
			 vscsi->flags, vscsi->state);
		break;

	case WAIT_IDLE:
		vscsi->rsp_q_timer.timer_pops = 0;
		vscsi->debit = 0;
		vscsi->credit = 0;
		if (vscsi->flags & TRANS_EVENT) {
			vscsi->state = WAIT_CONNECTION;
			vscsi->flags &= PRESERVE_FLAG_FIELDS;
		} else {
			vscsi->state = CONNECTED;
			vscsi->flags &= ~DISCONNECT_SCHEDULED;
		}

		pr_debug("adapter_idle, wait: flags 0x%x, state 0x%hx\n",
			 vscsi->flags, vscsi->state);
		ibmvscsis_poll_cmd_q(vscsi);
		break;

	case ERR_DISCONNECTED:
		vscsi->flags &= ~DISCONNECT_SCHEDULED;
		pr_debug("adapter_idle, disconnected: flags 0x%x, state 0x%hx\n",
			 vscsi->flags, vscsi->state);
		break;

	default:
		dev_err(&vscsi->dev, "adapter_idle: in invalid state %d\n",
			vscsi->state);
		break;
	}

	if (free_qs)
		ibmvscsis_free_cmd_qs(vscsi);

	/*
	 * There is a timing window where we could lose a disconnect request.
	 * The known path to this window occurs during the DISCONNECT_RECONNECT
	 * case above: reset_queue calls free_command_q, which will release the
	 * interrupt lock.  During that time, a new post_disconnect call can be
	 * made with a "more severe" state (DISCONNECT or UNCONFIGURING).
	 * Because the DISCONNECT_SCHEDULED flag is already set, post_disconnect
	 * will only set the new_state.  Now free_command_q reacquires the intr
	 * lock and clears the DISCONNECT_SCHEDULED flag (using PRESERVE_FLAG_
	 * FIELDS), and the disconnect is lost.  This is particularly bad when
	 * the new disconnect was for UNCONFIGURING, since the unconfigure hangs
	 * forever.
	 * Fix is that free command queue sets acr state and acr flags if there
	 * is a change under the lock
	 * note free command queue writes to this state it clears it
	 * before releasing the lock, different drivers call the free command
	 * queue different times so dont initialize above
	 */
	if (vscsi->phyp_acr_state != 0)	{
		/*
		 * set any bits in flags that may have been cleared by
		 * a call to free command queue in switch statement
		 * or reset queue
		 */
		vscsi->flags |= vscsi->phyp_acr_flags;
		ibmvscsis_post_disconnect(vscsi, vscsi->phyp_acr_state, 0);
		vscsi->phyp_acr_state = 0;
		vscsi->phyp_acr_flags = 0;

		pr_debug("adapter_idle: flags 0x%x, state 0x%hx, acr_flags 0x%x, acr_state 0x%hx\n",
			 vscsi->flags, vscsi->state, vscsi->phyp_acr_flags,
			 vscsi->phyp_acr_state);
	}

	pr_debug("Leaving adapter_idle: flags 0x%x, state 0x%hx, new_state 0x%x\n",
		 vscsi->flags, vscsi->state, vscsi->new_state);
}

/**
 * ibmvscsis_copy_crq_packet() - Copy CRQ Packet
 * @vscsi:	Pointer to our adapter structure
 * @cmd:	Pointer to command element to use to process the request
 * @crq:	Pointer to CRQ entry containing the request
 *
 * Copy the srp information unit from the hosted
 * partition using remote dma
 *
 * EXECUTION ENVIRONMENT:
 *	Interrupt, interrupt lock held
 */
static long ibmvscsis_copy_crq_packet(struct scsi_info *vscsi,
				      struct ibmvscsis_cmd *cmd,
				      struct viosrp_crq *crq)
{
	struct iu_entry *iue = cmd->iue;
	long rc = 0;
	u16 len;

	len = be16_to_cpu(crq->IU_length);
	if ((len > SRP_MAX_IU_LEN) || (len == 0)) {
		dev_err(&vscsi->dev, "copy_crq: Invalid len %d passed", len);
		ibmvscsis_post_disconnect(vscsi, ERR_DISCONNECT_RECONNECT, 0);
		return SRP_VIOLATION;
	}

	rc = h_copy_rdma(len, vscsi->dds.window[REMOTE].liobn,
			 be64_to_cpu(crq->IU_data_ptr),
			 vscsi->dds.window[LOCAL].liobn, iue->sbuf->dma);

	switch (rc) {
	case H_SUCCESS:
		cmd->init_time = mftb();
		iue->remote_token = crq->IU_data_ptr;
		iue->iu_len = len;
		pr_debug("copy_crq: ioba 0x%llx, init_time 0x%llx\n",
			 be64_to_cpu(crq->IU_data_ptr), cmd->init_time);
		break;
	case H_PERMISSION:
		if (connection_broken(vscsi))
			ibmvscsis_post_disconnect(vscsi,
						  ERR_DISCONNECT_RECONNECT,
						  (RESPONSE_Q_DOWN |
						   CLIENT_FAILED));
		else
			ibmvscsis_post_disconnect(vscsi,
						  ERR_DISCONNECT_RECONNECT, 0);

		dev_err(&vscsi->dev, "copy_crq: h_copy_rdma failed, rc %ld\n",
			rc);
		break;
	case H_DEST_PARM:
	case H_SOURCE_PARM:
	default:
		dev_err(&vscsi->dev, "copy_crq: h_copy_rdma failed, rc %ld\n",
			rc);
		ibmvscsis_post_disconnect(vscsi, ERR_DISCONNECT_RECONNECT, 0);
		break;
	}

	return rc;
}

/**
 * ibmvscsis_adapter_info - Service an Adapter Info MAnagement Data gram
 * @vscsi:	Pointer to our adapter structure
 * @iue:	Information Unit containing the Adapter Info MAD request
 *
 * EXECUTION ENVIRONMENT:
 *	Interrupt adapter lock is held
 */
static long ibmvscsis_adapter_info(struct scsi_info *vscsi,
				   struct iu_entry *iue)
{
	struct viosrp_adapter_info *mad = &vio_iu(iue)->mad.adapter_info;
	struct mad_adapter_info_data *info;
	uint flag_bits = 0;
	dma_addr_t token;
	long rc;

	mad->common.status = cpu_to_be16(VIOSRP_MAD_SUCCESS);

	if (be16_to_cpu(mad->common.length) > sizeof(*info)) {
		mad->common.status = cpu_to_be16(VIOSRP_MAD_FAILED);
		return 0;
	}

	info = dma_alloc_coherent(&vscsi->dma_dev->dev, sizeof(*info), &token,
				  GFP_KERNEL);
	if (!info) {
		dev_err(&vscsi->dev, "bad dma_alloc_coherent %p\n",
			iue->target);
		mad->common.status = cpu_to_be16(VIOSRP_MAD_FAILED);
		return 0;
	}

	/* Get remote info */
	rc = h_copy_rdma(be16_to_cpu(mad->common.length),
			 vscsi->dds.window[REMOTE].liobn,
			 be64_to_cpu(mad->buffer),
			 vscsi->dds.window[LOCAL].liobn, token);

	if (rc != H_SUCCESS) {
		if (rc == H_PERMISSION) {
			if (connection_broken(vscsi))
				flag_bits = (RESPONSE_Q_DOWN | CLIENT_FAILED);
		}
		pr_warn("adapter_info: h_copy_rdma from client failed, rc %ld\n",
			rc);
		pr_debug("adapter_info: ioba 0x%llx, flags 0x%x, flag_bits 0x%x\n",
			 be64_to_cpu(mad->buffer), vscsi->flags, flag_bits);
		ibmvscsis_post_disconnect(vscsi, ERR_DISCONNECT_RECONNECT,
					  flag_bits);
		goto free_dma;
	}

	/*
	 * Copy client info, but ignore partition number, which we
	 * already got from phyp - unless we failed to get it from
	 * phyp (e.g. if we're running on a p5 system).
	 */
	if (vscsi->client_data.partition_number == 0)
		vscsi->client_data.partition_number =
			be32_to_cpu(info->partition_number);
	strncpy(vscsi->client_data.srp_version, info->srp_version,
		sizeof(vscsi->client_data.srp_version));
	strncpy(vscsi->client_data.partition_name, info->partition_name,
		sizeof(vscsi->client_data.partition_name));
	vscsi->client_data.mad_version = be32_to_cpu(info->mad_version);
	vscsi->client_data.os_type = be32_to_cpu(info->os_type);

	/* Copy our info */
	strncpy(info->srp_version, SRP_VERSION,
		sizeof(info->srp_version));
	strncpy(info->partition_name, vscsi->dds.partition_name,
		sizeof(info->partition_name));
	info->partition_number = cpu_to_be32(vscsi->dds.partition_num);
	info->mad_version = cpu_to_be32(MAD_VERSION_1);
	info->os_type = cpu_to_be32(LINUX);
	memset(&info->port_max_txu[0], 0, sizeof(info->port_max_txu));
	info->port_max_txu[0] = cpu_to_be32(128 * PAGE_SIZE);

	dma_wmb();
	rc = h_copy_rdma(sizeof(*info), vscsi->dds.window[LOCAL].liobn,
			 token, vscsi->dds.window[REMOTE].liobn,
			 be64_to_cpu(mad->buffer));
	switch (rc) {
	case H_SUCCESS:
		break;

	case H_SOURCE_PARM:
	case H_DEST_PARM:
	case H_PERMISSION:
		if (connection_broken(vscsi))
			flag_bits = (RESPONSE_Q_DOWN | CLIENT_FAILED);
	default:
		dev_err(&vscsi->dev, "adapter_info: h_copy_rdma to client failed, rc %ld\n",
			rc);
		ibmvscsis_post_disconnect(vscsi,
					  ERR_DISCONNECT_RECONNECT,
					  flag_bits);
		break;
	}

free_dma:
	dma_free_coherent(&vscsi->dma_dev->dev, sizeof(*info), info, token);
	pr_debug("Leaving adapter_info, rc %ld\n", rc);

	return rc;
}

/**
 * ibmvscsis_cap_mad() - Service a Capabilities MAnagement Data gram
 * @vscsi:	Pointer to our adapter structure
 * @iue:	Information Unit containing the Capabilities MAD request
 *
 * NOTE: if you return an error from this routine you must be
 * disconnecting or you will cause a hang
 *
 * EXECUTION ENVIRONMENT:
 *	Interrupt called with adapter lock held
 */
static int ibmvscsis_cap_mad(struct scsi_info *vscsi, struct iu_entry *iue)
{
	struct viosrp_capabilities *mad = &vio_iu(iue)->mad.capabilities;
	struct capabilities *cap;
	struct mad_capability_common *common;
	dma_addr_t token;
	u16 olen, len, status, min_len, cap_len;
	u32 flag;
	uint flag_bits = 0;
	long rc = 0;

	olen = be16_to_cpu(mad->common.length);
	/*
	 * struct capabilities hardcodes a couple capabilities after the
	 * header, but the capabilities can actually be in any order.
	 */
	min_len = offsetof(struct capabilities, migration);
	if ((olen < min_len) || (olen > PAGE_SIZE)) {
		pr_warn("cap_mad: invalid len %d\n", olen);
		mad->common.status = cpu_to_be16(VIOSRP_MAD_FAILED);
		return 0;
	}

	cap = dma_alloc_coherent(&vscsi->dma_dev->dev, olen, &token,
				 GFP_KERNEL);
	if (!cap) {
		dev_err(&vscsi->dev, "bad dma_alloc_coherent %p\n",
			iue->target);
		mad->common.status = cpu_to_be16(VIOSRP_MAD_FAILED);
		return 0;
	}
	rc = h_copy_rdma(olen, vscsi->dds.window[REMOTE].liobn,
			 be64_to_cpu(mad->buffer),
			 vscsi->dds.window[LOCAL].liobn, token);
	if (rc == H_SUCCESS) {
		strncpy(cap->name, dev_name(&vscsi->dma_dev->dev),
			SRP_MAX_LOC_LEN);

		len = olen - min_len;
		status = VIOSRP_MAD_SUCCESS;
		common = (struct mad_capability_common *)&cap->migration;

		while ((len > 0) && (status == VIOSRP_MAD_SUCCESS) && !rc) {
			pr_debug("cap_mad: len left %hd, cap type %d, cap len %hd\n",
				 len, be32_to_cpu(common->cap_type),
				 be16_to_cpu(common->length));

			cap_len = be16_to_cpu(common->length);
			if (cap_len > len) {
				dev_err(&vscsi->dev, "cap_mad: cap len mismatch with total len\n");
				status = VIOSRP_MAD_FAILED;
				break;
			}

			if (cap_len == 0) {
				dev_err(&vscsi->dev, "cap_mad: cap len is 0\n");
				status = VIOSRP_MAD_FAILED;
				break;
			}

			switch (common->cap_type) {
			default:
				pr_debug("cap_mad: unsupported capability\n");
				common->server_support = 0;
				flag = cpu_to_be32((u32)CAP_LIST_SUPPORTED);
				cap->flags &= ~flag;
				break;
			}

			len = len - cap_len;
			common = (struct mad_capability_common *)
				((char *)common + cap_len);
		}

		mad->common.status = cpu_to_be16(status);

		dma_wmb();
		rc = h_copy_rdma(olen, vscsi->dds.window[LOCAL].liobn, token,
				 vscsi->dds.window[REMOTE].liobn,
				 be64_to_cpu(mad->buffer));

		if (rc != H_SUCCESS) {
			pr_debug("cap_mad: failed to copy to client, rc %ld\n",
				 rc);

			if (rc == H_PERMISSION) {
				if (connection_broken(vscsi))
					flag_bits = (RESPONSE_Q_DOWN |
						     CLIENT_FAILED);
			}

			pr_warn("cap_mad: error copying data to client, rc %ld\n",
				rc);
			ibmvscsis_post_disconnect(vscsi,
						  ERR_DISCONNECT_RECONNECT,
						  flag_bits);
		}
	}

	dma_free_coherent(&vscsi->dma_dev->dev, olen, cap, token);

	pr_debug("Leaving cap_mad, rc %ld, client_cap 0x%x\n",
		 rc, vscsi->client_cap);

	return rc;
}

/**
 * ibmvscsis_process_mad() - Service a MAnagement Data gram
 * @vscsi:	Pointer to our adapter structure
 * @iue:	Information Unit containing the MAD request
 *
 * Must be called with interrupt lock held.
 */
static long ibmvscsis_process_mad(struct scsi_info *vscsi, struct iu_entry *iue)
{
	struct mad_common *mad = (struct mad_common *)&vio_iu(iue)->mad;
	struct viosrp_empty_iu *empty;
	long rc = ADAPT_SUCCESS;

	switch (be32_to_cpu(mad->type)) {
	case VIOSRP_EMPTY_IU_TYPE:
		empty = &vio_iu(iue)->mad.empty_iu;
		vscsi->empty_iu_id = be64_to_cpu(empty->buffer);
		vscsi->empty_iu_tag = be64_to_cpu(empty->common.tag);
		mad->status = cpu_to_be16(VIOSRP_MAD_SUCCESS);
		break;
	case VIOSRP_ADAPTER_INFO_TYPE:
		rc = ibmvscsis_adapter_info(vscsi, iue);
		break;
	case VIOSRP_CAPABILITIES_TYPE:
		rc = ibmvscsis_cap_mad(vscsi, iue);
		break;
	case VIOSRP_ENABLE_FAST_FAIL:
		if (vscsi->state == CONNECTED) {
			vscsi->fast_fail = true;
			mad->status = cpu_to_be16(VIOSRP_MAD_SUCCESS);
		} else {
			pr_warn("fast fail mad sent after login\n");
			mad->status = cpu_to_be16(VIOSRP_MAD_FAILED);
		}
		break;
	default:
		mad->status = cpu_to_be16(VIOSRP_MAD_NOT_SUPPORTED);
		break;
	}

	return rc;
}

/**
 * srp_snd_msg_failed() - Handle an error when sending a response
 * @vscsi:	Pointer to our adapter structure
 * @rc:		The return code from the h_send_crq command
 *
 * Must be called with interrupt lock held.
 */
static void srp_snd_msg_failed(struct scsi_info *vscsi, long rc)
{
	ktime_t kt;

	if (rc != H_DROPPED) {
		ibmvscsis_free_cmd_qs(vscsi);

		if (rc == H_CLOSED)
			vscsi->flags |= CLIENT_FAILED;

		/* don't flag the same problem multiple times */
		if (!(vscsi->flags & RESPONSE_Q_DOWN)) {
			vscsi->flags |= RESPONSE_Q_DOWN;
			if (!(vscsi->state & (ERR_DISCONNECT |
					      ERR_DISCONNECT_RECONNECT |
					      ERR_DISCONNECTED | UNDEFINED))) {
				dev_err(&vscsi->dev, "snd_msg_failed: setting RESPONSE_Q_DOWN, state 0x%hx, flags 0x%x, rc %ld\n",
					vscsi->state, vscsi->flags, rc);
			}
			ibmvscsis_post_disconnect(vscsi,
						  ERR_DISCONNECT_RECONNECT, 0);
		}
		return;
	}

	/*
	 * The response queue is full.
	 * If the server is processing SRP requests, i.e.
	 * the client has successfully done an
	 * SRP_LOGIN, then it will wait forever for room in
	 * the queue.  However if the system admin
	 * is attempting to unconfigure the server then one
	 * or more children will be in a state where
	 * they are being removed. So if there is even one
	 * child being removed then the driver assumes
	 * the system admin is attempting to break the
	 * connection with the client and MAX_TIMER_POPS
	 * is honored.
	 */
	if ((vscsi->rsp_q_timer.timer_pops < MAX_TIMER_POPS) ||
	    (vscsi->state == SRP_PROCESSING)) {
		pr_debug("snd_msg_failed: response queue full, flags 0x%x, timer started %d, pops %d\n",
			 vscsi->flags, (int)vscsi->rsp_q_timer.started,
			 vscsi->rsp_q_timer.timer_pops);

		/*
		 * Check if the timer is running; if it
		 * is not then start it up.
		 */
		if (!vscsi->rsp_q_timer.started) {
			if (vscsi->rsp_q_timer.timer_pops <
			    MAX_TIMER_POPS) {
				kt = WAIT_NANO_SECONDS;
			} else {
				/*
				 * slide the timeslice if the maximum
				 * timer pops have already happened
				 */
				kt = ktime_set(WAIT_SECONDS, 0);
			}

			vscsi->rsp_q_timer.started = true;
			hrtimer_start(&vscsi->rsp_q_timer.timer, kt,
				      HRTIMER_MODE_REL);
		}
	} else {
		/*
		 * TBD: Do we need to worry about this? Need to get
		 *      remove working.
		 */
		/*
		 * waited a long time and it appears the system admin
		 * is bring this driver down
		 */
		vscsi->flags |= RESPONSE_Q_DOWN;
		ibmvscsis_free_cmd_qs(vscsi);
		/*
		 * if the driver is already attempting to disconnect
		 * from the client and has already logged an error
		 * trace this event but don't put it in the error log
		 */
		if (!(vscsi->state & (ERR_DISCONNECT |
				      ERR_DISCONNECT_RECONNECT |
				      ERR_DISCONNECTED | UNDEFINED))) {
			dev_err(&vscsi->dev, "client crq full too long\n");
			ibmvscsis_post_disconnect(vscsi,
						  ERR_DISCONNECT_RECONNECT,
						  0);
		}
	}
}

/**
 * ibmvscsis_send_messages() - Send a Response
 * @vscsi:	Pointer to our adapter structure
 *
 * Send a response, first checking the waiting queue. Responses are
 * sent in order they are received. If the response cannot be sent,
 * because the client queue is full, it stays on the waiting queue.
 *
 * PRECONDITION:
 *	Called with interrupt lock held
 */
static void ibmvscsis_send_messages(struct scsi_info *vscsi)
{
	u64 msg_hi = 0;
	/* note do not attmempt to access the IU_data_ptr with this pointer
	 * it is not valid
	 */
	struct viosrp_crq *crq = (struct viosrp_crq *)&msg_hi;
	struct ibmvscsis_cmd *cmd, *nxt;
	struct iu_entry *iue;
	long rc = ADAPT_SUCCESS;

	if (!(vscsi->flags & RESPONSE_Q_DOWN)) {
		list_for_each_entry_safe(cmd, nxt, &vscsi->waiting_rsp, list) {
			iue = cmd->iue;

			crq->valid = VALID_CMD_RESP_EL;
			crq->format = cmd->rsp.format;

			if (cmd->flags & CMD_FAST_FAIL)
				crq->status = VIOSRP_ADAPTER_FAIL;

			crq->IU_length = cpu_to_be16(cmd->rsp.len);

			rc = h_send_crq(vscsi->dma_dev->unit_address,
					be64_to_cpu(msg_hi),
					be64_to_cpu(cmd->rsp.tag));

			pr_debug("send_messages: cmd %p, tag 0x%llx, rc %ld\n",
				 cmd, be64_to_cpu(cmd->rsp.tag), rc);

			/* if all ok free up the command element resources */
			if (rc == H_SUCCESS) {
				/* some movement has occurred */
				vscsi->rsp_q_timer.timer_pops = 0;
				list_del(&cmd->list);

				ibmvscsis_free_cmd_resources(vscsi, cmd);
			} else {
				srp_snd_msg_failed(vscsi, rc);
				break;
			}
		}

		if (!rc) {
			/*
			 * The timer could pop with the queue empty.  If
			 * this happens, rc will always indicate a
			 * success; clear the pop count.
			 */
			vscsi->rsp_q_timer.timer_pops = 0;
		}
	} else {
		ibmvscsis_free_cmd_qs(vscsi);
	}
}

/* Called with intr lock held */
static void ibmvscsis_send_mad_resp(struct scsi_info *vscsi,
				    struct ibmvscsis_cmd *cmd,
				    struct viosrp_crq *crq)
{
	struct iu_entry *iue = cmd->iue;
	struct mad_common *mad = (struct mad_common *)&vio_iu(iue)->mad;
	uint flag_bits = 0;
	long rc;

	dma_wmb();
	rc = h_copy_rdma(sizeof(struct mad_common),
			 vscsi->dds.window[LOCAL].liobn, iue->sbuf->dma,
			 vscsi->dds.window[REMOTE].liobn,
			 be64_to_cpu(crq->IU_data_ptr));
	if (!rc) {
		cmd->rsp.format = VIOSRP_MAD_FORMAT;
		cmd->rsp.len = sizeof(struct mad_common);
		cmd->rsp.tag = mad->tag;
		list_add_tail(&cmd->list, &vscsi->waiting_rsp);
		ibmvscsis_send_messages(vscsi);
	} else {
		pr_debug("Error sending mad response, rc %ld\n", rc);
		if (rc == H_PERMISSION) {
			if (connection_broken(vscsi))
				flag_bits = (RESPONSE_Q_DOWN | CLIENT_FAILED);
		}
		dev_err(&vscsi->dev, "mad: failed to copy to client, rc %ld\n",
			rc);

		ibmvscsis_free_cmd_resources(vscsi, cmd);
		ibmvscsis_post_disconnect(vscsi, ERR_DISCONNECT_RECONNECT,
					  flag_bits);
	}
}

/**
 * ibmvscsis_mad() - Service a MAnagement Data gram.
 * @vscsi:	Pointer to our adapter structure
 * @crq:	Pointer to the CRQ entry containing the MAD request
 *
 * EXECUTION ENVIRONMENT:
 *	Interrupt, called with adapter lock held
 */
static long ibmvscsis_mad(struct scsi_info *vscsi, struct viosrp_crq *crq)
{
	struct iu_entry *iue;
	struct ibmvscsis_cmd *cmd;
	struct mad_common *mad;
	long rc = ADAPT_SUCCESS;

	switch (vscsi->state) {
		/*
		 * We have not exchanged Init Msgs yet, so this MAD was sent
		 * before the last Transport Event; client will not be
		 * expecting a response.
		 */
	case WAIT_CONNECTION:
		pr_debug("mad: in Wait Connection state, ignoring MAD, flags %d\n",
			 vscsi->flags);
		return ADAPT_SUCCESS;

	case SRP_PROCESSING:
	case CONNECTED:
		break;

		/*
		 * We should never get here while we're in these states.
		 * Just log an error and get out.
		 */
	case UNCONFIGURING:
	case WAIT_IDLE:
	case ERR_DISCONNECT:
	case ERR_DISCONNECT_RECONNECT:
	default:
		dev_err(&vscsi->dev, "mad: invalid adapter state %d for mad\n",
			vscsi->state);
		return ADAPT_SUCCESS;
	}

	cmd = ibmvscsis_get_free_cmd(vscsi);
	if (!cmd) {
		dev_err(&vscsi->dev, "mad: failed to get cmd, debit %d\n",
			vscsi->debit);
		ibmvscsis_post_disconnect(vscsi, ERR_DISCONNECT_RECONNECT, 0);
		return ERROR;
	}
	iue = cmd->iue;
	cmd->type = ADAPTER_MAD;

	rc = ibmvscsis_copy_crq_packet(vscsi, cmd, crq);
	if (!rc) {
		mad = (struct mad_common *)&vio_iu(iue)->mad;

		pr_debug("mad: type %d\n", be32_to_cpu(mad->type));

		rc = ibmvscsis_process_mad(vscsi, iue);

		pr_debug("mad: status %hd, rc %ld\n", be16_to_cpu(mad->status),
			 rc);

		if (!rc)
			ibmvscsis_send_mad_resp(vscsi, cmd, crq);
	} else {
		ibmvscsis_free_cmd_resources(vscsi, cmd);
	}

	pr_debug("Leaving mad, rc %ld\n", rc);
	return rc;
}

/**
 * ibmvscsis_login_rsp() - Create/copy a login response notice to the client
 * @vscsi:	Pointer to our adapter structure
 * @cmd:	Pointer to the command for the SRP Login request
 *
 * EXECUTION ENVIRONMENT:
 *	Interrupt, interrupt lock held
 */
static long ibmvscsis_login_rsp(struct scsi_info *vscsi,
				struct ibmvscsis_cmd *cmd)
{
	struct iu_entry *iue = cmd->iue;
	struct srp_login_rsp *rsp = &vio_iu(iue)->srp.login_rsp;
	struct format_code *fmt;
	uint flag_bits = 0;
	long rc = ADAPT_SUCCESS;

	memset(rsp, 0, sizeof(struct srp_login_rsp));

	rsp->opcode = SRP_LOGIN_RSP;
	rsp->req_lim_delta = cpu_to_be32(vscsi->request_limit);
	rsp->tag = cmd->rsp.tag;
	rsp->max_it_iu_len = cpu_to_be32(SRP_MAX_IU_LEN);
	rsp->max_ti_iu_len = cpu_to_be32(SRP_MAX_IU_LEN);
	fmt = (struct format_code *)&rsp->buf_fmt;
	fmt->buffers = SUPPORTED_FORMATS;
	vscsi->credit = 0;

	cmd->rsp.len = sizeof(struct srp_login_rsp);

	dma_wmb();
	rc = h_copy_rdma(cmd->rsp.len, vscsi->dds.window[LOCAL].liobn,
			 iue->sbuf->dma, vscsi->dds.window[REMOTE].liobn,
			 be64_to_cpu(iue->remote_token));

	switch (rc) {
	case H_SUCCESS:
		break;

	case H_PERMISSION:
		if (connection_broken(vscsi))
			flag_bits = RESPONSE_Q_DOWN | CLIENT_FAILED;
		dev_err(&vscsi->dev, "login_rsp: error copying to client, rc %ld\n",
			rc);
		ibmvscsis_post_disconnect(vscsi, ERR_DISCONNECT_RECONNECT,
					  flag_bits);
		break;
	case H_SOURCE_PARM:
	case H_DEST_PARM:
	default:
		dev_err(&vscsi->dev, "login_rsp: error copying to client, rc %ld\n",
			rc);
		ibmvscsis_post_disconnect(vscsi, ERR_DISCONNECT_RECONNECT, 0);
		break;
	}

	return rc;
}

/**
 * ibmvscsis_srp_login_rej() - Create/copy a login rejection notice to client
 * @vscsi:	Pointer to our adapter structure
 * @cmd:	Pointer to the command for the SRP Login request
 * @reason:	The reason the SRP Login is being rejected, per SRP protocol
 *
 * EXECUTION ENVIRONMENT:
 *	Interrupt, interrupt lock held
 */
static long ibmvscsis_srp_login_rej(struct scsi_info *vscsi,
				    struct ibmvscsis_cmd *cmd, u32 reason)
{
	struct iu_entry *iue = cmd->iue;
	struct srp_login_rej *rej = &vio_iu(iue)->srp.login_rej;
	struct format_code *fmt;
	uint flag_bits = 0;
	long rc = ADAPT_SUCCESS;

	memset(rej, 0, sizeof(*rej));

	rej->opcode = SRP_LOGIN_REJ;
	rej->reason = cpu_to_be32(reason);
	rej->tag = cmd->rsp.tag;
	fmt = (struct format_code *)&rej->buf_fmt;
	fmt->buffers = SUPPORTED_FORMATS;

	cmd->rsp.len = sizeof(*rej);

	dma_wmb();
	rc = h_copy_rdma(cmd->rsp.len, vscsi->dds.window[LOCAL].liobn,
			 iue->sbuf->dma, vscsi->dds.window[REMOTE].liobn,
			 be64_to_cpu(iue->remote_token));

	switch (rc) {
	case H_SUCCESS:
		break;
	case H_PERMISSION:
		if (connection_broken(vscsi))
			flag_bits = RESPONSE_Q_DOWN | CLIENT_FAILED;
		dev_err(&vscsi->dev, "login_rej: error copying to client, rc %ld\n",
			rc);
		ibmvscsis_post_disconnect(vscsi, ERR_DISCONNECT_RECONNECT,
					  flag_bits);
		break;
	case H_SOURCE_PARM:
	case H_DEST_PARM:
	default:
		dev_err(&vscsi->dev, "login_rej: error copying to client, rc %ld\n",
			rc);
		ibmvscsis_post_disconnect(vscsi, ERR_DISCONNECT_RECONNECT, 0);
		break;
	}

	return rc;
}

static int ibmvscsis_make_nexus(struct ibmvscsis_tport *tport)
{
	char *name = tport->tport_name;
	struct ibmvscsis_nexus *nexus;
	int rc;

	if (tport->ibmv_nexus) {
		pr_debug("tport->ibmv_nexus already exists\n");
		return 0;
	}

	nexus = kzalloc(sizeof(*nexus), GFP_KERNEL);
	if (!nexus) {
		pr_err("Unable to allocate struct ibmvscsis_nexus\n");
		return -ENOMEM;
	}

	nexus->se_sess = target_alloc_session(&tport->se_tpg, 0, 0,
					      TARGET_PROT_NORMAL, name, nexus,
					      NULL);
	if (IS_ERR(nexus->se_sess)) {
		rc = PTR_ERR(nexus->se_sess);
		goto transport_init_fail;
	}

	tport->ibmv_nexus = nexus;

	return 0;

transport_init_fail:
	kfree(nexus);
	return rc;
}

static int ibmvscsis_drop_nexus(struct ibmvscsis_tport *tport)
{
	struct se_session *se_sess;
	struct ibmvscsis_nexus *nexus;

	nexus = tport->ibmv_nexus;
	if (!nexus)
		return -ENODEV;

	se_sess = nexus->se_sess;
	if (!se_sess)
		return -ENODEV;

	/*
	 * Release the SCSI I_T Nexus to the emulated ibmvscsis Target Port
	 */
	target_wait_for_sess_cmds(se_sess);
	transport_deregister_session_configfs(se_sess);
	transport_deregister_session(se_sess);
	tport->ibmv_nexus = NULL;
	kfree(nexus);

	return 0;
}

/**
 * ibmvscsis_srp_login() - Process an SRP Login Request
 * @vscsi:	Pointer to our adapter structure
 * @cmd:	Command element to use to process the SRP Login request
 * @crq:	Pointer to CRQ entry containing the SRP Login request
 *
 * EXECUTION ENVIRONMENT:
 *	Interrupt, called with interrupt lock held
 */
static long ibmvscsis_srp_login(struct scsi_info *vscsi,
				struct ibmvscsis_cmd *cmd,
				struct viosrp_crq *crq)
{
	struct iu_entry *iue = cmd->iue;
	struct srp_login_req *req = &vio_iu(iue)->srp.login_req;
	struct port_id {
		__be64 id_extension;
		__be64 io_guid;
	} *iport, *tport;
	struct format_code *fmt;
	u32 reason = 0x0;
	long rc = ADAPT_SUCCESS;

	iport = (struct port_id *)req->initiator_port_id;
	tport = (struct port_id *)req->target_port_id;
	fmt = (struct format_code *)&req->req_buf_fmt;
	if (be32_to_cpu(req->req_it_iu_len) > SRP_MAX_IU_LEN)
		reason = SRP_LOGIN_REJ_REQ_IT_IU_LENGTH_TOO_LARGE;
	else if (be32_to_cpu(req->req_it_iu_len) < 64)
		reason = SRP_LOGIN_REJ_UNABLE_ESTABLISH_CHANNEL;
	else if ((be64_to_cpu(iport->id_extension) > (MAX_NUM_PORTS - 1)) ||
		 (be64_to_cpu(tport->id_extension) > (MAX_NUM_PORTS - 1)))
		reason = SRP_LOGIN_REJ_UNABLE_ASSOCIATE_CHANNEL;
	else if (req->req_flags & SRP_MULTICHAN_MULTI)
		reason = SRP_LOGIN_REJ_MULTI_CHANNEL_UNSUPPORTED;
	else if (fmt->buffers & (~SUPPORTED_FORMATS))
		reason = SRP_LOGIN_REJ_UNSUPPORTED_DESCRIPTOR_FMT;
	else if ((fmt->buffers & SUPPORTED_FORMATS) == 0)
		reason = SRP_LOGIN_REJ_UNSUPPORTED_DESCRIPTOR_FMT;

	if (vscsi->state == SRP_PROCESSING)
		reason = SRP_LOGIN_REJ_CHANNEL_LIMIT_REACHED;

	rc = ibmvscsis_make_nexus(&vscsi->tport);
	if (rc)
		reason = SRP_LOGIN_REJ_UNABLE_ESTABLISH_CHANNEL;

	cmd->rsp.format = VIOSRP_SRP_FORMAT;
	cmd->rsp.tag = req->tag;

	pr_debug("srp_login: reason 0x%x\n", reason);

	if (reason)
		rc = ibmvscsis_srp_login_rej(vscsi, cmd, reason);
	else
		rc = ibmvscsis_login_rsp(vscsi, cmd);

	if (!rc) {
		if (!reason)
			vscsi->state = SRP_PROCESSING;

		list_add_tail(&cmd->list, &vscsi->waiting_rsp);
		ibmvscsis_send_messages(vscsi);
	} else {
		ibmvscsis_free_cmd_resources(vscsi, cmd);
	}

	pr_debug("Leaving srp_login, rc %ld\n", rc);
	return rc;
}

/**
 * ibmvscsis_srp_i_logout() - Helper Function to close I_T Nexus
 * @vscsi:	Pointer to our adapter structure
 * @cmd:	Command element to use to process the Implicit Logout request
 * @crq:	Pointer to CRQ entry containing the Implicit Logout request
 *
 * Do the logic to close the I_T nexus.  This function may not
 * behave to specification.
 *
 * EXECUTION ENVIRONMENT:
 *	Interrupt, interrupt lock held
 */
static long ibmvscsis_srp_i_logout(struct scsi_info *vscsi,
				   struct ibmvscsis_cmd *cmd,
				   struct viosrp_crq *crq)
{
	struct iu_entry *iue = cmd->iue;
	struct srp_i_logout *log_out = &vio_iu(iue)->srp.i_logout;
	long rc = ADAPT_SUCCESS;

	if ((vscsi->debit > 0) || !list_empty(&vscsi->schedule_q) ||
	    !list_empty(&vscsi->waiting_rsp)) {
		dev_err(&vscsi->dev, "i_logout: outstanding work\n");
		ibmvscsis_post_disconnect(vscsi, ERR_DISCONNECT, 0);
	} else {
		cmd->rsp.format = SRP_FORMAT;
		cmd->rsp.tag = log_out->tag;
		cmd->rsp.len = sizeof(struct mad_common);
		list_add_tail(&cmd->list, &vscsi->waiting_rsp);
		ibmvscsis_send_messages(vscsi);

		ibmvscsis_post_disconnect(vscsi, WAIT_IDLE, 0);
	}

	return rc;
}

/* Called with intr lock held */
static void ibmvscsis_srp_cmd(struct scsi_info *vscsi, struct viosrp_crq *crq)
{
	struct ibmvscsis_cmd *cmd;
	struct iu_entry *iue;
	struct srp_cmd *srp;
	struct srp_tsk_mgmt *tsk;
	long rc;

	if (vscsi->request_limit - vscsi->debit <= 0) {
		/* Client has exceeded request limit */
		dev_err(&vscsi->dev, "Client exceeded the request limit (%d), debit %d\n",
			vscsi->request_limit, vscsi->debit);
		ibmvscsis_post_disconnect(vscsi, ERR_DISCONNECT_RECONNECT, 0);
		return;
	}

	cmd = ibmvscsis_get_free_cmd(vscsi);
	if (!cmd) {
		dev_err(&vscsi->dev, "srp_cmd failed to get cmd, debit %d\n",
			vscsi->debit);
		ibmvscsis_post_disconnect(vscsi, ERR_DISCONNECT_RECONNECT, 0);
		return;
	}
	iue = cmd->iue;
	srp = &vio_iu(iue)->srp.cmd;

	rc = ibmvscsis_copy_crq_packet(vscsi, cmd, crq);
	if (rc) {
		ibmvscsis_free_cmd_resources(vscsi, cmd);
		return;
	}

	if (vscsi->state == SRP_PROCESSING) {
		switch (srp->opcode) {
		case SRP_LOGIN_REQ:
			rc = ibmvscsis_srp_login(vscsi, cmd, crq);
			break;

		case SRP_TSK_MGMT:
			tsk = &vio_iu(iue)->srp.tsk_mgmt;
			pr_debug("tsk_mgmt tag: %llu (0x%llx)\n", tsk->tag,
				 tsk->tag);
			cmd->rsp.tag = tsk->tag;
			vscsi->debit += 1;
			cmd->type = TASK_MANAGEMENT;
			list_add_tail(&cmd->list, &vscsi->schedule_q);
			queue_work(vscsi->work_q, &cmd->work);
			break;

		case SRP_CMD:
			pr_debug("srp_cmd tag: %llu (0x%llx)\n", srp->tag,
				 srp->tag);
			cmd->rsp.tag = srp->tag;
			vscsi->debit += 1;
			cmd->type = SCSI_CDB;
			/*
			 * We want to keep track of work waiting for
			 * the workqueue.
			 */
			list_add_tail(&cmd->list, &vscsi->schedule_q);
			queue_work(vscsi->work_q, &cmd->work);
			break;

		case SRP_I_LOGOUT:
			rc = ibmvscsis_srp_i_logout(vscsi, cmd, crq);
			break;

		case SRP_CRED_RSP:
		case SRP_AER_RSP:
		default:
			ibmvscsis_free_cmd_resources(vscsi, cmd);
			dev_err(&vscsi->dev, "invalid srp cmd, opcode %d\n",
				(uint)srp->opcode);
			ibmvscsis_post_disconnect(vscsi,
						  ERR_DISCONNECT_RECONNECT, 0);
			break;
		}
	} else if (srp->opcode == SRP_LOGIN_REQ && vscsi->state == CONNECTED) {
		rc = ibmvscsis_srp_login(vscsi, cmd, crq);
	} else {
		ibmvscsis_free_cmd_resources(vscsi, cmd);
		dev_err(&vscsi->dev, "Invalid state %d to handle srp cmd\n",
			vscsi->state);
		ibmvscsis_post_disconnect(vscsi, ERR_DISCONNECT_RECONNECT, 0);
	}
}

/**
 * ibmvscsis_ping_response() - Respond to a ping request
 * @vscsi:	Pointer to our adapter structure
 *
 * Let the client know that the server is alive and waiting on
 * its native I/O stack.
 * If any type of error occurs from the call to queue a ping
 * response then the client is either not accepting or receiving
 * interrupts.  Disconnect with an error.
 *
 * EXECUTION ENVIRONMENT:
 *	Interrupt, interrupt lock held
 */
static long ibmvscsis_ping_response(struct scsi_info *vscsi)
{
	struct viosrp_crq *crq;
	u64 buffer[2] = { 0, 0 };
	long rc;

	crq = (struct viosrp_crq *)&buffer;
	crq->valid = VALID_CMD_RESP_EL;
	crq->format = (u8)MESSAGE_IN_CRQ;
	crq->status = PING_RESPONSE;

	rc = h_send_crq(vscsi->dds.unit_id, cpu_to_be64(buffer[MSG_HI]),
			cpu_to_be64(buffer[MSG_LOW]));

	switch (rc) {
	case H_SUCCESS:
		break;
	case H_CLOSED:
		vscsi->flags |= CLIENT_FAILED;
	case H_DROPPED:
		vscsi->flags |= RESPONSE_Q_DOWN;
	case H_REMOTE_PARM:
		dev_err(&vscsi->dev, "ping_response: h_send_crq failed, rc %ld\n",
			rc);
		ibmvscsis_post_disconnect(vscsi, ERR_DISCONNECT_RECONNECT, 0);
		break;
	default:
		dev_err(&vscsi->dev, "ping_response: h_send_crq returned unknown rc %ld\n",
			rc);
		ibmvscsis_post_disconnect(vscsi, ERR_DISCONNECT, 0);
		break;
	}

	return rc;
}

/**
 * ibmvscsis_parse_command() - Parse an element taken from the cmd rsp queue.
 * @vscsi:	Pointer to our adapter structure
 * @crq:	Pointer to CRQ element containing the SRP request
 *
 * This function will return success if the command queue element is valid
 * and the srp iu or MAD request it pointed to was also valid.  That does
 * not mean that an error was not returned to the client.
 *
 * EXECUTION ENVIRONMENT:
 *	Interrupt, intr lock held
 */
static long ibmvscsis_parse_command(struct scsi_info *vscsi,
				    struct viosrp_crq *crq)
{
	long rc = ADAPT_SUCCESS;

	switch (crq->valid) {
	case VALID_CMD_RESP_EL:
		switch (crq->format) {
		case OS400_FORMAT:
		case AIX_FORMAT:
		case LINUX_FORMAT:
		case MAD_FORMAT:
			if (vscsi->flags & PROCESSING_MAD) {
				rc = ERROR;
				dev_err(&vscsi->dev, "parse_command: already processing mad\n");
				ibmvscsis_post_disconnect(vscsi,
						       ERR_DISCONNECT_RECONNECT,
						       0);
			} else {
				vscsi->flags |= PROCESSING_MAD;
				rc = ibmvscsis_mad(vscsi, crq);
			}
			break;

		case SRP_FORMAT:
			ibmvscsis_srp_cmd(vscsi, crq);
			break;

		case MESSAGE_IN_CRQ:
			if (crq->status == PING)
				ibmvscsis_ping_response(vscsi);
			break;

		default:
			dev_err(&vscsi->dev, "parse_command: invalid format %d\n",
				(uint)crq->format);
			ibmvscsis_post_disconnect(vscsi,
						  ERR_DISCONNECT_RECONNECT, 0);
			break;
		}
		break;

	case VALID_TRANS_EVENT:
		rc = ibmvscsis_trans_event(vscsi, crq);
		break;

	case VALID_INIT_MSG:
		rc = ibmvscsis_init_msg(vscsi, crq);
		break;

	default:
		dev_err(&vscsi->dev, "parse_command: invalid valid field %d\n",
			(uint)crq->valid);
		ibmvscsis_post_disconnect(vscsi, ERR_DISCONNECT_RECONNECT, 0);
		break;
	}

	/*
	 * Return only what the interrupt handler cares
	 * about. Most errors we keep right on trucking.
	 */
	rc = vscsi->flags & SCHEDULE_DISCONNECT;

	return rc;
}

static int read_dma_window(struct scsi_info *vscsi)
{
	struct vio_dev *vdev = vscsi->dma_dev;
	const __be32 *dma_window;
	const __be32 *prop;

	/* TODO Using of_parse_dma_window would be better, but it doesn't give
	 * a way to read multiple windows without already knowing the size of
	 * a window or the number of windows.
	 */
	dma_window = (const __be32 *)vio_get_attribute(vdev,
						       "ibm,my-dma-window",
						       NULL);
	if (!dma_window) {
		pr_err("Couldn't find ibm,my-dma-window property\n");
		return -1;
	}

	vscsi->dds.window[LOCAL].liobn = be32_to_cpu(*dma_window);
	dma_window++;

	prop = (const __be32 *)vio_get_attribute(vdev, "ibm,#dma-address-cells",
						 NULL);
	if (!prop) {
		pr_warn("Couldn't find ibm,#dma-address-cells property\n");
		dma_window++;
	} else {
		dma_window += be32_to_cpu(*prop);
	}

	prop = (const __be32 *)vio_get_attribute(vdev, "ibm,#dma-size-cells",
						 NULL);
	if (!prop) {
		pr_warn("Couldn't find ibm,#dma-size-cells property\n");
		dma_window++;
	} else {
		dma_window += be32_to_cpu(*prop);
	}

	/* dma_window should point to the second window now */
	vscsi->dds.window[REMOTE].liobn = be32_to_cpu(*dma_window);

	return 0;
}

static struct ibmvscsis_tport *ibmvscsis_lookup_port(const char *name)
{
	struct ibmvscsis_tport *tport = NULL;
	struct vio_dev *vdev;
	struct scsi_info *vscsi;

	spin_lock_bh(&ibmvscsis_dev_lock);
	list_for_each_entry(vscsi, &ibmvscsis_dev_list, list) {
		vdev = vscsi->dma_dev;
		if (!strcmp(dev_name(&vdev->dev), name)) {
			tport = &vscsi->tport;
			break;
		}
	}
	spin_unlock_bh(&ibmvscsis_dev_lock);

	return tport;
}

/**
 * ibmvscsis_parse_cmd() - Parse SRP Command
 * @vscsi:	Pointer to our adapter structure
 * @cmd:	Pointer to command element with SRP command
 *
 * Parse the srp command; if it is valid then submit it to tcm.
 * Note: The return code does not reflect the status of the SCSI CDB.
 *
 * EXECUTION ENVIRONMENT:
 *	Process level
 */
static void ibmvscsis_parse_cmd(struct scsi_info *vscsi,
				struct ibmvscsis_cmd *cmd)
{
	struct iu_entry *iue = cmd->iue;
	struct srp_cmd *srp = (struct srp_cmd *)iue->sbuf->buf;
	struct ibmvscsis_nexus *nexus;
	u64 data_len = 0;
	enum dma_data_direction dir;
	int attr = 0;
	int rc = 0;

	nexus = vscsi->tport.ibmv_nexus;
	/*
	 * additional length in bytes.  Note that the SRP spec says that
	 * additional length is in 4-byte words, but technically the
	 * additional length field is only the upper 6 bits of the byte.
	 * The lower 2 bits are reserved.  If the lower 2 bits are 0 (as
	 * all reserved fields should be), then interpreting the byte as
	 * an int will yield the length in bytes.
	 */
	if (srp->add_cdb_len & 0x03) {
		dev_err(&vscsi->dev, "parse_cmd: reserved bits set in IU\n");
		spin_lock_bh(&vscsi->intr_lock);
		ibmvscsis_post_disconnect(vscsi, ERR_DISCONNECT_RECONNECT, 0);
		ibmvscsis_free_cmd_resources(vscsi, cmd);
		spin_unlock_bh(&vscsi->intr_lock);
		return;
	}

	if (srp_get_desc_table(srp, &dir, &data_len)) {
		dev_err(&vscsi->dev, "0x%llx: parsing SRP descriptor table failed.\n",
			srp->tag);
		goto fail;
	}

	cmd->rsp.sol_not = srp->sol_not;

	switch (srp->task_attr) {
	case SRP_SIMPLE_TASK:
		attr = TCM_SIMPLE_TAG;
		break;
	case SRP_ORDERED_TASK:
		attr = TCM_ORDERED_TAG;
		break;
	case SRP_HEAD_TASK:
		attr = TCM_HEAD_TAG;
		break;
	case SRP_ACA_TASK:
		attr = TCM_ACA_TAG;
		break;
	default:
		dev_err(&vscsi->dev, "Invalid task attribute %d\n",
			srp->task_attr);
		goto fail;
	}

	cmd->se_cmd.tag = be64_to_cpu(srp->tag);

	spin_lock_bh(&vscsi->intr_lock);
	list_add_tail(&cmd->list, &vscsi->active_q);
	spin_unlock_bh(&vscsi->intr_lock);

	srp->lun.scsi_lun[0] &= 0x3f;

	rc = target_submit_cmd(&cmd->se_cmd, nexus->se_sess, srp->cdb,
			       cmd->sense_buf, scsilun_to_int(&srp->lun),
			       data_len, attr, dir, 0);
	if (rc) {
		dev_err(&vscsi->dev, "target_submit_cmd failed, rc %d\n", rc);
		spin_lock_bh(&vscsi->intr_lock);
		list_del(&cmd->list);
		ibmvscsis_free_cmd_resources(vscsi, cmd);
		spin_unlock_bh(&vscsi->intr_lock);
		goto fail;
	}
	return;

fail:
	spin_lock_bh(&vscsi->intr_lock);
	ibmvscsis_post_disconnect(vscsi, ERR_DISCONNECT_RECONNECT, 0);
	spin_unlock_bh(&vscsi->intr_lock);
}

/**
 * ibmvscsis_parse_task() - Parse SRP Task Management Request
 * @vscsi:	Pointer to our adapter structure
 * @cmd:	Pointer to command element with SRP task management request
 *
 * Parse the srp task management request; if it is valid then submit it to tcm.
 * Note: The return code does not reflect the status of the task management
 * request.
 *
 * EXECUTION ENVIRONMENT:
 *	Processor level
 */
static void ibmvscsis_parse_task(struct scsi_info *vscsi,
				 struct ibmvscsis_cmd *cmd)
{
	struct iu_entry *iue = cmd->iue;
	struct srp_tsk_mgmt *srp_tsk = &vio_iu(iue)->srp.tsk_mgmt;
	int tcm_type;
	u64 tag_to_abort = 0;
	int rc = 0;
	struct ibmvscsis_nexus *nexus;

	nexus = vscsi->tport.ibmv_nexus;

	cmd->rsp.sol_not = srp_tsk->sol_not;

	switch (srp_tsk->tsk_mgmt_func) {
	case SRP_TSK_ABORT_TASK:
		tcm_type = TMR_ABORT_TASK;
		tag_to_abort = be64_to_cpu(srp_tsk->task_tag);
		break;
	case SRP_TSK_ABORT_TASK_SET:
		tcm_type = TMR_ABORT_TASK_SET;
		break;
	case SRP_TSK_CLEAR_TASK_SET:
		tcm_type = TMR_CLEAR_TASK_SET;
		break;
	case SRP_TSK_LUN_RESET:
		tcm_type = TMR_LUN_RESET;
		break;
	case SRP_TSK_CLEAR_ACA:
		tcm_type = TMR_CLEAR_ACA;
		break;
	default:
		dev_err(&vscsi->dev, "unknown task mgmt func %d\n",
			srp_tsk->tsk_mgmt_func);
		cmd->se_cmd.se_tmr_req->response =
			TMR_TASK_MGMT_FUNCTION_NOT_SUPPORTED;
		rc = -1;
		break;
	}

	if (!rc) {
		cmd->se_cmd.tag = be64_to_cpu(srp_tsk->tag);

		spin_lock_bh(&vscsi->intr_lock);
		list_add_tail(&cmd->list, &vscsi->active_q);
		spin_unlock_bh(&vscsi->intr_lock);

		srp_tsk->lun.scsi_lun[0] &= 0x3f;

		pr_debug("calling submit_tmr, func %d\n",
			 srp_tsk->tsk_mgmt_func);
		rc = target_submit_tmr(&cmd->se_cmd, nexus->se_sess, NULL,
				       scsilun_to_int(&srp_tsk->lun), srp_tsk,
				       tcm_type, GFP_KERNEL, tag_to_abort, 0);
		if (rc) {
			dev_err(&vscsi->dev, "target_submit_tmr failed, rc %d\n",
				rc);
			spin_lock_bh(&vscsi->intr_lock);
			list_del(&cmd->list);
			spin_unlock_bh(&vscsi->intr_lock);
			cmd->se_cmd.se_tmr_req->response =
				TMR_FUNCTION_REJECTED;
		}
	}

	if (rc)
		transport_send_check_condition_and_sense(&cmd->se_cmd, 0, 0);
}

static void ibmvscsis_scheduler(struct work_struct *work)
{
	struct ibmvscsis_cmd *cmd = container_of(work, struct ibmvscsis_cmd,
						 work);
	struct scsi_info *vscsi = cmd->adapter;

	spin_lock_bh(&vscsi->intr_lock);

	/* Remove from schedule_q */
	list_del(&cmd->list);

	/* Don't submit cmd if we're disconnecting */
	if (vscsi->flags & (SCHEDULE_DISCONNECT | DISCONNECT_SCHEDULED)) {
		ibmvscsis_free_cmd_resources(vscsi, cmd);

		/* ibmvscsis_disconnect might be waiting for us */
		if (list_empty(&vscsi->active_q) &&
		    list_empty(&vscsi->schedule_q) &&
		    (vscsi->flags & WAIT_FOR_IDLE)) {
			vscsi->flags &= ~WAIT_FOR_IDLE;
			complete(&vscsi->wait_idle);
		}

		spin_unlock_bh(&vscsi->intr_lock);
		return;
	}

	spin_unlock_bh(&vscsi->intr_lock);

	switch (cmd->type) {
	case SCSI_CDB:
		ibmvscsis_parse_cmd(vscsi, cmd);
		break;
	case TASK_MANAGEMENT:
		ibmvscsis_parse_task(vscsi, cmd);
		break;
	default:
		dev_err(&vscsi->dev, "scheduler, invalid cmd type %d\n",
			cmd->type);
		spin_lock_bh(&vscsi->intr_lock);
		ibmvscsis_free_cmd_resources(vscsi, cmd);
		spin_unlock_bh(&vscsi->intr_lock);
		break;
	}
}

static int ibmvscsis_alloc_cmds(struct scsi_info *vscsi, int num)
{
	struct ibmvscsis_cmd *cmd;
	int i;

	INIT_LIST_HEAD(&vscsi->free_cmd);
	vscsi->cmd_pool = kcalloc(num, sizeof(struct ibmvscsis_cmd),
				  GFP_KERNEL);
	if (!vscsi->cmd_pool)
		return -ENOMEM;

	for (i = 0, cmd = (struct ibmvscsis_cmd *)vscsi->cmd_pool; i < num;
	     i++, cmd++) {
		cmd->adapter = vscsi;
		INIT_WORK(&cmd->work, ibmvscsis_scheduler);
		list_add_tail(&cmd->list, &vscsi->free_cmd);
	}

	return 0;
}

static void ibmvscsis_free_cmds(struct scsi_info *vscsi)
{
	kfree(vscsi->cmd_pool);
	vscsi->cmd_pool = NULL;
	INIT_LIST_HEAD(&vscsi->free_cmd);
}

/**
 * ibmvscsis_service_wait_q() - Service Waiting Queue
 * @timer:	Pointer to timer which has expired
 *
 * This routine is called when the timer pops to service the waiting
 * queue. Elements on the queue have completed, their responses have been
 * copied to the client, but the client's response queue was full so
 * the queue message could not be sent. The routine grabs the proper locks
 * and calls send messages.
 *
 * EXECUTION ENVIRONMENT:
 *	called at interrupt level
 */
static enum hrtimer_restart ibmvscsis_service_wait_q(struct hrtimer *timer)
{
	struct timer_cb *p_timer = container_of(timer, struct timer_cb, timer);
	struct scsi_info *vscsi = container_of(p_timer, struct scsi_info,
					       rsp_q_timer);

	spin_lock_bh(&vscsi->intr_lock);
	p_timer->timer_pops += 1;
	p_timer->started = false;
	ibmvscsis_send_messages(vscsi);
	spin_unlock_bh(&vscsi->intr_lock);

	return HRTIMER_NORESTART;
}

static long ibmvscsis_alloctimer(struct scsi_info *vscsi)
{
	struct timer_cb *p_timer;

	p_timer = &vscsi->rsp_q_timer;
	hrtimer_init(&p_timer->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);

	p_timer->timer.function = ibmvscsis_service_wait_q;
	p_timer->started = false;
	p_timer->timer_pops = 0;

	return ADAPT_SUCCESS;
}

static void ibmvscsis_freetimer(struct scsi_info *vscsi)
{
	struct timer_cb *p_timer;

	p_timer = &vscsi->rsp_q_timer;

	(void)hrtimer_cancel(&p_timer->timer);

	p_timer->started = false;
	p_timer->timer_pops = 0;
}

static irqreturn_t ibmvscsis_interrupt(int dummy, void *data)
{
	struct scsi_info *vscsi = data;

	vio_disable_interrupts(vscsi->dma_dev);
	tasklet_schedule(&vscsi->work_task);

	return IRQ_HANDLED;
}

/**
 * ibmvscsis_enable_change_state() - Set new state based on enabled status
 * @vscsi:	Pointer to our adapter structure
 *
 * This function determines our new state now that we are enabled.  This
 * may involve sending an Init Complete message to the client.
 *
 * Must be called with interrupt lock held.
 */
static long ibmvscsis_enable_change_state(struct scsi_info *vscsi)
{
	int bytes;
	long rc = ADAPT_SUCCESS;

	bytes = vscsi->cmd_q.size * PAGE_SIZE;
	rc = h_reg_crq(vscsi->dds.unit_id, vscsi->cmd_q.crq_token, bytes);
	if (rc == H_CLOSED || rc == H_SUCCESS) {
		vscsi->state = WAIT_CONNECTION;
		rc = ibmvscsis_establish_new_q(vscsi);
	}

	if (rc != ADAPT_SUCCESS) {
		vscsi->state = ERR_DISCONNECTED;
		vscsi->flags |= RESPONSE_Q_DOWN;
	}

	return rc;
}

/**
 * ibmvscsis_create_command_q() - Create Command Queue
 * @vscsi:	Pointer to our adapter structure
 * @num_cmds:	Currently unused.  In the future, may be used to determine
 *		the size of the CRQ.
 *
 * Allocates memory for command queue maps remote memory into an ioba
 * initializes the command response queue
 *
 * EXECUTION ENVIRONMENT:
 *	Process level only
 */
static long ibmvscsis_create_command_q(struct scsi_info *vscsi, int num_cmds)
{
	int pages;
	struct vio_dev *vdev = vscsi->dma_dev;

	/* We might support multiple pages in the future, but just 1 for now */
	pages = 1;

	vscsi->cmd_q.size = pages;

	vscsi->cmd_q.base_addr =
		(struct viosrp_crq *)get_zeroed_page(GFP_KERNEL);
	if (!vscsi->cmd_q.base_addr)
		return -ENOMEM;

	vscsi->cmd_q.mask = ((uint)pages * CRQ_PER_PAGE) - 1;

	vscsi->cmd_q.crq_token = dma_map_single(&vdev->dev,
						vscsi->cmd_q.base_addr,
						PAGE_SIZE, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(&vdev->dev, vscsi->cmd_q.crq_token)) {
		free_page((unsigned long)vscsi->cmd_q.base_addr);
		return -ENOMEM;
	}

	return 0;
}

/**
 * ibmvscsis_destroy_command_q - Destroy Command Queue
 * @vscsi:	Pointer to our adapter structure
 *
 * Releases memory for command queue and unmaps mapped remote memory.
 *
 * EXECUTION ENVIRONMENT:
 *	Process level only
 */
static void ibmvscsis_destroy_command_q(struct scsi_info *vscsi)
{
	dma_unmap_single(&vscsi->dma_dev->dev, vscsi->cmd_q.crq_token,
			 PAGE_SIZE, DMA_BIDIRECTIONAL);
	free_page((unsigned long)vscsi->cmd_q.base_addr);
	vscsi->cmd_q.base_addr = NULL;
	vscsi->state = NO_QUEUE;
}

static u8 ibmvscsis_fast_fail(struct scsi_info *vscsi,
			      struct ibmvscsis_cmd *cmd)
{
	struct iu_entry *iue = cmd->iue;
	struct se_cmd *se_cmd = &cmd->se_cmd;
	struct srp_cmd *srp = (struct srp_cmd *)iue->sbuf->buf;
	struct scsi_sense_hdr sshdr;
	u8 rc = se_cmd->scsi_status;

	if (vscsi->fast_fail && (READ_CMD(srp->cdb) || WRITE_CMD(srp->cdb)))
		if (scsi_normalize_sense(se_cmd->sense_buffer,
					 se_cmd->scsi_sense_length, &sshdr))
			if (sshdr.sense_key == HARDWARE_ERROR &&
			    (se_cmd->residual_count == 0 ||
			     se_cmd->residual_count == se_cmd->data_length)) {
				rc = NO_SENSE;
				cmd->flags |= CMD_FAST_FAIL;
			}

	return rc;
}

/**
 * srp_build_response() - Build an SRP response buffer
 * @vscsi:	Pointer to our adapter structure
 * @cmd:	Pointer to command for which to send the response
 * @len_p:	Where to return the length of the IU response sent.  This
 *		is needed to construct the CRQ response.
 *
 * Build the SRP response buffer and copy it to the client's memory space.
 */
static long srp_build_response(struct scsi_info *vscsi,
			       struct ibmvscsis_cmd *cmd, uint *len_p)
{
	struct iu_entry *iue = cmd->iue;
	struct se_cmd *se_cmd = &cmd->se_cmd;
	struct srp_rsp *rsp;
	uint len;
	u32 rsp_code;
	char *data;
	u32 *tsk_status;
	long rc = ADAPT_SUCCESS;

	spin_lock_bh(&vscsi->intr_lock);

	rsp = &vio_iu(iue)->srp.rsp;
	len = sizeof(*rsp);
	memset(rsp, 0, len);
	data = rsp->data;

	rsp->opcode = SRP_RSP;

	if (vscsi->credit > 0 && vscsi->state == SRP_PROCESSING)
		rsp->req_lim_delta = cpu_to_be32(vscsi->credit);
	else
		rsp->req_lim_delta = cpu_to_be32(1 + vscsi->credit);
	rsp->tag = cmd->rsp.tag;
	rsp->flags = 0;

	if (cmd->type == SCSI_CDB) {
		rsp->status = ibmvscsis_fast_fail(vscsi, cmd);
		if (rsp->status) {
			pr_debug("build_resp: cmd %p, scsi status %d\n", cmd,
				 (int)rsp->status);
			ibmvscsis_determine_resid(se_cmd, rsp);
			if (se_cmd->scsi_sense_length && se_cmd->sense_buffer) {
				rsp->sense_data_len =
					cpu_to_be32(se_cmd->scsi_sense_length);
				rsp->flags |= SRP_RSP_FLAG_SNSVALID;
				len += se_cmd->scsi_sense_length;
				memcpy(data, se_cmd->sense_buffer,
				       se_cmd->scsi_sense_length);
			}
			rsp->sol_not = (cmd->rsp.sol_not & UCSOLNT) >>
				UCSOLNT_RESP_SHIFT;
		} else if (cmd->flags & CMD_FAST_FAIL) {
			pr_debug("build_resp: cmd %p, fast fail\n", cmd);
			rsp->sol_not = (cmd->rsp.sol_not & UCSOLNT) >>
				UCSOLNT_RESP_SHIFT;
		} else {
			rsp->sol_not = (cmd->rsp.sol_not & SCSOLNT) >>
				SCSOLNT_RESP_SHIFT;
		}
	} else {
		/* this is task management */
		rsp->status = 0;
		rsp->resp_data_len = cpu_to_be32(4);
		rsp->flags |= SRP_RSP_FLAG_RSPVALID;

		switch (se_cmd->se_tmr_req->response) {
		case TMR_FUNCTION_COMPLETE:
		case TMR_TASK_DOES_NOT_EXIST:
			rsp_code = SRP_TASK_MANAGEMENT_FUNCTION_COMPLETE;
			rsp->sol_not = (cmd->rsp.sol_not & SCSOLNT) >>
				SCSOLNT_RESP_SHIFT;
			break;
		case TMR_TASK_MGMT_FUNCTION_NOT_SUPPORTED:
		case TMR_LUN_DOES_NOT_EXIST:
			rsp_code = SRP_TASK_MANAGEMENT_FUNCTION_NOT_SUPPORTED;
			rsp->sol_not = (cmd->rsp.sol_not & UCSOLNT) >>
				UCSOLNT_RESP_SHIFT;
			break;
		case TMR_FUNCTION_FAILED:
		case TMR_FUNCTION_REJECTED:
		default:
			rsp_code = SRP_TASK_MANAGEMENT_FUNCTION_FAILED;
			rsp->sol_not = (cmd->rsp.sol_not & UCSOLNT) >>
				UCSOLNT_RESP_SHIFT;
			break;
		}

		tsk_status = (u32 *)data;
		*tsk_status = cpu_to_be32(rsp_code);
		data = (char *)(tsk_status + 1);
		len += 4;
	}

	dma_wmb();
	rc = h_copy_rdma(len, vscsi->dds.window[LOCAL].liobn, iue->sbuf->dma,
			 vscsi->dds.window[REMOTE].liobn,
			 be64_to_cpu(iue->remote_token));

	switch (rc) {
	case H_SUCCESS:
		vscsi->credit = 0;
		*len_p = len;
		break;
	case H_PERMISSION:
		if (connection_broken(vscsi))
			vscsi->flags |= RESPONSE_Q_DOWN | CLIENT_FAILED;

		dev_err(&vscsi->dev, "build_response: error copying to client, rc %ld, flags 0x%x, state 0x%hx\n",
			rc, vscsi->flags, vscsi->state);
		break;
	case H_SOURCE_PARM:
	case H_DEST_PARM:
	default:
		dev_err(&vscsi->dev, "build_response: error copying to client, rc %ld\n",
			rc);
		break;
	}

	spin_unlock_bh(&vscsi->intr_lock);

	return rc;
}

static int ibmvscsis_rdma(struct ibmvscsis_cmd *cmd, struct scatterlist *sg,
			  int nsg, struct srp_direct_buf *md, int nmd,
			  enum dma_data_direction dir, unsigned int bytes)
{
	struct iu_entry *iue = cmd->iue;
	struct srp_target *target = iue->target;
	struct scsi_info *vscsi = target->ldata;
	struct scatterlist *sgp;
	dma_addr_t client_ioba, server_ioba;
	ulong buf_len;
	ulong client_len, server_len;
	int md_idx;
	long tx_len;
	long rc = 0;

	if (bytes == 0)
		return 0;

	sgp = sg;
	client_len = 0;
	server_len = 0;
	md_idx = 0;
	tx_len = bytes;

	do {
		if (client_len == 0) {
			if (md_idx >= nmd) {
				dev_err(&vscsi->dev, "rdma: ran out of client memory descriptors\n");
				rc = -EIO;
				break;
			}
			client_ioba = be64_to_cpu(md[md_idx].va);
			client_len = be32_to_cpu(md[md_idx].len);
		}
		if (server_len == 0) {
			if (!sgp) {
				dev_err(&vscsi->dev, "rdma: ran out of scatter/gather list\n");
				rc = -EIO;
				break;
			}
			server_ioba = sg_dma_address(sgp);
			server_len = sg_dma_len(sgp);
		}

		buf_len = tx_len;

		if (buf_len > client_len)
			buf_len = client_len;

		if (buf_len > server_len)
			buf_len = server_len;

		if (buf_len > max_vdma_size)
			buf_len = max_vdma_size;

		if (dir == DMA_TO_DEVICE) {
			/* read from client */
			rc = h_copy_rdma(buf_len,
					 vscsi->dds.window[REMOTE].liobn,
					 client_ioba,
					 vscsi->dds.window[LOCAL].liobn,
					 server_ioba);
		} else {
			/* The h_copy_rdma will cause phyp, running in another
			 * partition, to read memory, so we need to make sure
			 * the data has been written out, hence these syncs.
			 */
			/* ensure that everything is in memory */
			isync();
			/* ensure that memory has been made visible */
			dma_wmb();
			rc = h_copy_rdma(buf_len,
					 vscsi->dds.window[LOCAL].liobn,
					 server_ioba,
					 vscsi->dds.window[REMOTE].liobn,
					 client_ioba);
		}
		switch (rc) {
		case H_SUCCESS:
			break;
		case H_PERMISSION:
		case H_SOURCE_PARM:
		case H_DEST_PARM:
			if (connection_broken(vscsi)) {
				spin_lock_bh(&vscsi->intr_lock);
				vscsi->flags |=
					(RESPONSE_Q_DOWN | CLIENT_FAILED);
				spin_unlock_bh(&vscsi->intr_lock);
			}
			dev_err(&vscsi->dev, "rdma: h_copy_rdma failed, rc %ld\n",
				rc);
			break;

		default:
			dev_err(&vscsi->dev, "rdma: unknown error %ld from h_copy_rdma\n",
				rc);
			break;
		}

		if (!rc) {
			tx_len -= buf_len;
			if (tx_len) {
				client_len -= buf_len;
				if (client_len == 0)
					md_idx++;
				else
					client_ioba += buf_len;

				server_len -= buf_len;
				if (server_len == 0)
					sgp = sg_next(sgp);
				else
					server_ioba += buf_len;
			} else {
				break;
			}
		}
	} while (!rc);

	return rc;
}

/**
 * ibmvscsis_handle_crq() - Handle CRQ
 * @data:	Pointer to our adapter structure
 *
 * Read the command elements from the command queue and copy the payloads
 * associated with the command elements to local memory and execute the
 * SRP requests.
 *
 * Note: this is an edge triggered interrupt. It can not be shared.
 */
static void ibmvscsis_handle_crq(unsigned long data)
{
	struct scsi_info *vscsi = (struct scsi_info *)data;
	struct viosrp_crq *crq;
	long rc;
	bool ack = true;
	volatile u8 valid;

	spin_lock_bh(&vscsi->intr_lock);

	pr_debug("got interrupt\n");

	/*
	 * if we are in a path where we are waiting for all pending commands
	 * to complete because we received a transport event and anything in
	 * the command queue is for a new connection, do nothing
	 */
	if (TARGET_STOP(vscsi)) {
		vio_enable_interrupts(vscsi->dma_dev);

		pr_debug("handle_crq, don't process: flags 0x%x, state 0x%hx\n",
			 vscsi->flags, vscsi->state);
		spin_unlock_bh(&vscsi->intr_lock);
		return;
	}

	rc = vscsi->flags & SCHEDULE_DISCONNECT;
	crq = vscsi->cmd_q.base_addr + vscsi->cmd_q.index;
	valid = crq->valid;
	dma_rmb();

	while (valid) {
		/*
		 * These are edege triggered interrupts. After dropping out of
		 * the while loop, the code must check for work since an
		 * interrupt could be lost, and an elment be left on the queue,
		 * hence the label.
		 */
cmd_work:
		vscsi->cmd_q.index =
			(vscsi->cmd_q.index + 1) & vscsi->cmd_q.mask;

		if (!rc) {
			rc = ibmvscsis_parse_command(vscsi, crq);
		} else {
			if ((uint)crq->valid == VALID_TRANS_EVENT) {
				/*
				 * must service the transport layer events even
				 * in an error state, dont break out until all
				 * the consecutive transport events have been
				 * processed
				 */
				rc = ibmvscsis_trans_event(vscsi, crq);
			} else if (vscsi->flags & TRANS_EVENT) {
				/*
				 * if a transport event has occurred leave
				 * everything but transport events on the queue
				 *
				 * need to decrement the queue index so we can
				 * look at the element again
				 */
				if (vscsi->cmd_q.index)
					vscsi->cmd_q.index -= 1;
				else
					/*
					 * index is at 0 it just wrapped.
					 * have it index last element in q
					 */
					vscsi->cmd_q.index = vscsi->cmd_q.mask;
				break;
			}
		}

		crq->valid = INVALIDATE_CMD_RESP_EL;

		crq = vscsi->cmd_q.base_addr + vscsi->cmd_q.index;
		valid = crq->valid;
		dma_rmb();
	}

	if (!rc) {
		if (ack) {
			vio_enable_interrupts(vscsi->dma_dev);
			ack = false;
			pr_debug("handle_crq, reenabling interrupts\n");
		}
		valid = crq->valid;
		dma_rmb();
		if (valid)
			goto cmd_work;
	} else {
		pr_debug("handle_crq, error: flags 0x%x, state 0x%hx, crq index 0x%x\n",
			 vscsi->flags, vscsi->state, vscsi->cmd_q.index);
	}

	pr_debug("Leaving handle_crq: schedule_q empty %d, flags 0x%x, state 0x%hx\n",
		 (int)list_empty(&vscsi->schedule_q), vscsi->flags,
		 vscsi->state);

	spin_unlock_bh(&vscsi->intr_lock);
}

static int ibmvscsis_probe(struct vio_dev *vdev,
			   const struct vio_device_id *id)
{
	struct scsi_info *vscsi;
	int rc = 0;
	long hrc = 0;
	char wq_name[24];

	vscsi = kzalloc(sizeof(*vscsi), GFP_KERNEL);
	if (!vscsi) {
		rc = -ENOMEM;
		pr_err("probe: allocation of adapter failed\n");
		return rc;
	}

	vscsi->dma_dev = vdev;
	vscsi->dev = vdev->dev;
	INIT_LIST_HEAD(&vscsi->schedule_q);
	INIT_LIST_HEAD(&vscsi->waiting_rsp);
	INIT_LIST_HEAD(&vscsi->active_q);

	snprintf(vscsi->tport.tport_name, IBMVSCSIS_NAMELEN, "%s",
		 dev_name(&vdev->dev));

	pr_debug("probe tport_name: %s\n", vscsi->tport.tport_name);

	rc = read_dma_window(vscsi);
	if (rc)
		goto free_adapter;
	pr_debug("Probe: liobn 0x%x, riobn 0x%x\n",
		 vscsi->dds.window[LOCAL].liobn,
		 vscsi->dds.window[REMOTE].liobn);

	strcpy(vscsi->eye, "VSCSI ");
	strncat(vscsi->eye, vdev->name, MAX_EYE);

	vscsi->dds.unit_id = vdev->unit_address;
	strncpy(vscsi->dds.partition_name, partition_name,
		sizeof(vscsi->dds.partition_name));
	vscsi->dds.partition_num = partition_number;

	spin_lock_bh(&ibmvscsis_dev_lock);
	list_add_tail(&vscsi->list, &ibmvscsis_dev_list);
	spin_unlock_bh(&ibmvscsis_dev_lock);

	/*
	 * TBD: How do we determine # of cmds to request?  Do we know how
	 * many "children" we have?
	 */
	vscsi->request_limit = INITIAL_SRP_LIMIT;
	rc = srp_target_alloc(&vscsi->target, &vdev->dev, vscsi->request_limit,
			      SRP_MAX_IU_LEN);
	if (rc)
		goto rem_list;

	vscsi->target.ldata = vscsi;

	rc = ibmvscsis_alloc_cmds(vscsi, vscsi->request_limit);
	if (rc) {
		dev_err(&vscsi->dev, "alloc_cmds failed, rc %d, num %d\n",
			rc, vscsi->request_limit);
		goto free_target;
	}

	/*
	 * Note: the lock is used in freeing timers, so must initialize
	 * first so that ordering in case of error is correct.
	 */
	spin_lock_init(&vscsi->intr_lock);

	rc = ibmvscsis_alloctimer(vscsi);
	if (rc) {
		dev_err(&vscsi->dev, "probe: alloctimer failed, rc %d\n", rc);
		goto free_cmds;
	}

	rc = ibmvscsis_create_command_q(vscsi, 256);
	if (rc) {
		dev_err(&vscsi->dev, "probe: create_command_q failed, rc %d\n",
			rc);
		goto free_timer;
	}

	vscsi->map_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!vscsi->map_buf) {
		rc = -ENOMEM;
		dev_err(&vscsi->dev, "probe: allocating cmd buffer failed\n");
		goto destroy_queue;
	}

	vscsi->map_ioba = dma_map_single(&vdev->dev, vscsi->map_buf, PAGE_SIZE,
					 DMA_BIDIRECTIONAL);
	if (dma_mapping_error(&vdev->dev, vscsi->map_ioba)) {
		rc = -ENOMEM;
		dev_err(&vscsi->dev, "probe: error mapping command buffer\n");
		goto free_buf;
	}

	hrc = h_vioctl(vscsi->dds.unit_id, H_GET_PARTNER_INFO,
		       (u64)vscsi->map_ioba | ((u64)PAGE_SIZE << 32), 0, 0, 0,
		       0);
	if (hrc == H_SUCCESS)
		vscsi->client_data.partition_number =
			be64_to_cpu(*(u64 *)vscsi->map_buf);
	/*
	 * We expect the VIOCTL to fail if we're configured as "any
	 * client can connect" and the client isn't activated yet.
	 * We'll make the call again when he sends an init msg.
	 */
	pr_debug("probe hrc %ld, client partition num %d\n",
		 hrc, vscsi->client_data.partition_number);

	tasklet_init(&vscsi->work_task, ibmvscsis_handle_crq,
		     (unsigned long)vscsi);

	init_completion(&vscsi->wait_idle);
	init_completion(&vscsi->unconfig);

	snprintf(wq_name, 24, "ibmvscsis%s", dev_name(&vdev->dev));
	vscsi->work_q = create_workqueue(wq_name);
	if (!vscsi->work_q) {
		rc = -ENOMEM;
		dev_err(&vscsi->dev, "create_workqueue failed\n");
		goto unmap_buf;
	}

	rc = request_irq(vdev->irq, ibmvscsis_interrupt, 0, "ibmvscsis", vscsi);
	if (rc) {
		rc = -EPERM;
		dev_err(&vscsi->dev, "probe: request_irq failed, rc %d\n", rc);
		goto destroy_WQ;
	}

	vscsi->state = WAIT_ENABLED;

	dev_set_drvdata(&vdev->dev, vscsi);

	return 0;

destroy_WQ:
	destroy_workqueue(vscsi->work_q);
unmap_buf:
	dma_unmap_single(&vdev->dev, vscsi->map_ioba, PAGE_SIZE,
			 DMA_BIDIRECTIONAL);
free_buf:
	kfree(vscsi->map_buf);
destroy_queue:
	tasklet_kill(&vscsi->work_task);
	ibmvscsis_unregister_command_q(vscsi);
	ibmvscsis_destroy_command_q(vscsi);
free_timer:
	ibmvscsis_freetimer(vscsi);
free_cmds:
	ibmvscsis_free_cmds(vscsi);
free_target:
	srp_target_free(&vscsi->target);
rem_list:
	spin_lock_bh(&ibmvscsis_dev_lock);
	list_del(&vscsi->list);
	spin_unlock_bh(&ibmvscsis_dev_lock);
free_adapter:
	kfree(vscsi);

	return rc;
}

static int ibmvscsis_remove(struct vio_dev *vdev)
{
	struct scsi_info *vscsi = dev_get_drvdata(&vdev->dev);

	pr_debug("remove (%s)\n", dev_name(&vscsi->dma_dev->dev));

	spin_lock_bh(&vscsi->intr_lock);
	ibmvscsis_post_disconnect(vscsi, UNCONFIGURING, 0);
	vscsi->flags |= CFG_SLEEPING;
	spin_unlock_bh(&vscsi->intr_lock);
	wait_for_completion(&vscsi->unconfig);

	vio_disable_interrupts(vdev);
	free_irq(vdev->irq, vscsi);
	destroy_workqueue(vscsi->work_q);
	dma_unmap_single(&vdev->dev, vscsi->map_ioba, PAGE_SIZE,
			 DMA_BIDIRECTIONAL);
	kfree(vscsi->map_buf);
	tasklet_kill(&vscsi->work_task);
	ibmvscsis_destroy_command_q(vscsi);
	ibmvscsis_freetimer(vscsi);
	ibmvscsis_free_cmds(vscsi);
	srp_target_free(&vscsi->target);
	spin_lock_bh(&ibmvscsis_dev_lock);
	list_del(&vscsi->list);
	spin_unlock_bh(&ibmvscsis_dev_lock);
	kfree(vscsi);

	return 0;
}

static ssize_t system_id_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", system_id);
}

static ssize_t partition_number_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%x\n", partition_number);
}

static ssize_t unit_address_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct scsi_info *vscsi = container_of(dev, struct scsi_info, dev);

	return snprintf(buf, PAGE_SIZE, "%x\n", vscsi->dma_dev->unit_address);
}

static int ibmvscsis_get_system_info(void)
{
	struct device_node *rootdn, *vdevdn;
	const char *id, *model, *name;
	const uint *num;

	rootdn = of_find_node_by_path("/");
	if (!rootdn)
		return -ENOENT;

	model = of_get_property(rootdn, "model", NULL);
	id = of_get_property(rootdn, "system-id", NULL);
	if (model && id)
		snprintf(system_id, sizeof(system_id), "%s-%s", model, id);

	name = of_get_property(rootdn, "ibm,partition-name", NULL);
	if (name)
		strncpy(partition_name, name, sizeof(partition_name));

	num = of_get_property(rootdn, "ibm,partition-no", NULL);
	if (num)
		partition_number = of_read_number(num, 1);

	of_node_put(rootdn);

	vdevdn = of_find_node_by_path("/vdevice");
	if (vdevdn) {
		const uint *mvds;

		mvds = of_get_property(vdevdn, "ibm,max-virtual-dma-size",
				       NULL);
		if (mvds)
			max_vdma_size = *mvds;
		of_node_put(vdevdn);
	}

	return 0;
}

static char *ibmvscsis_get_fabric_name(void)
{
	return "ibmvscsis";
}

static char *ibmvscsis_get_fabric_wwn(struct se_portal_group *se_tpg)
{
	struct ibmvscsis_tport *tport =
		container_of(se_tpg, struct ibmvscsis_tport, se_tpg);

	return tport->tport_name;
}

static u16 ibmvscsis_get_tag(struct se_portal_group *se_tpg)
{
	struct ibmvscsis_tport *tport =
		container_of(se_tpg, struct ibmvscsis_tport, se_tpg);

	return tport->tport_tpgt;
}

static u32 ibmvscsis_get_default_depth(struct se_portal_group *se_tpg)
{
	return 1;
}

static int ibmvscsis_check_true(struct se_portal_group *se_tpg)
{
	return 1;
}

static int ibmvscsis_check_false(struct se_portal_group *se_tpg)
{
	return 0;
}

static u32 ibmvscsis_tpg_get_inst_index(struct se_portal_group *se_tpg)
{
	return 1;
}

static int ibmvscsis_check_stop_free(struct se_cmd *se_cmd)
{
	return target_put_sess_cmd(se_cmd);
}

static void ibmvscsis_release_cmd(struct se_cmd *se_cmd)
{
	struct ibmvscsis_cmd *cmd = container_of(se_cmd, struct ibmvscsis_cmd,
						 se_cmd);
	struct scsi_info *vscsi = cmd->adapter;

	spin_lock_bh(&vscsi->intr_lock);
	/* Remove from active_q */
	list_move_tail(&cmd->list, &vscsi->waiting_rsp);
	ibmvscsis_send_messages(vscsi);
	spin_unlock_bh(&vscsi->intr_lock);
}

static u32 ibmvscsis_sess_get_index(struct se_session *se_sess)
{
	return 0;
}

static int ibmvscsis_write_pending(struct se_cmd *se_cmd)
{
	struct ibmvscsis_cmd *cmd = container_of(se_cmd, struct ibmvscsis_cmd,
						 se_cmd);
	struct iu_entry *iue = cmd->iue;
	int rc;

	rc = srp_transfer_data(cmd, &vio_iu(iue)->srp.cmd, ibmvscsis_rdma,
			       1, 1);
	if (rc) {
		pr_err("srp_transfer_data() failed: %d\n", rc);
		return -EAGAIN;
	}
	/*
	 * We now tell TCM to add this WRITE CDB directly into the TCM storage
	 * object execution queue.
	 */
	target_execute_cmd(se_cmd);
	return 0;
}

static int ibmvscsis_write_pending_status(struct se_cmd *se_cmd)
{
	return 0;
}

static void ibmvscsis_set_default_node_attrs(struct se_node_acl *nacl)
{
}

static int ibmvscsis_get_cmd_state(struct se_cmd *se_cmd)
{
	return 0;
}

static int ibmvscsis_queue_data_in(struct se_cmd *se_cmd)
{
	struct ibmvscsis_cmd *cmd = container_of(se_cmd, struct ibmvscsis_cmd,
						 se_cmd);
	struct iu_entry *iue = cmd->iue;
	struct scsi_info *vscsi = cmd->adapter;
	char *sd;
	uint len = 0;
	int rc;

	rc = srp_transfer_data(cmd, &vio_iu(iue)->srp.cmd, ibmvscsis_rdma, 1,
			       1);
	if (rc) {
		pr_err("srp_transfer_data failed: %d\n", rc);
		sd = se_cmd->sense_buffer;
		se_cmd->scsi_sense_length = 18;
		memset(se_cmd->sense_buffer, 0, se_cmd->scsi_sense_length);
		/* Logical Unit Communication Time-out asc/ascq = 0x0801 */
		scsi_build_sense_buffer(0, se_cmd->sense_buffer, MEDIUM_ERROR,
					0x08, 0x01);
	}

	srp_build_response(vscsi, cmd, &len);
	cmd->rsp.format = SRP_FORMAT;
	cmd->rsp.len = len;

	return 0;
}

static int ibmvscsis_queue_status(struct se_cmd *se_cmd)
{
	struct ibmvscsis_cmd *cmd = container_of(se_cmd, struct ibmvscsis_cmd,
						 se_cmd);
	struct scsi_info *vscsi = cmd->adapter;
	uint len;

	pr_debug("queue_status %p\n", se_cmd);

	srp_build_response(vscsi, cmd, &len);
	cmd->rsp.format = SRP_FORMAT;
	cmd->rsp.len = len;

	return 0;
}

static void ibmvscsis_queue_tm_rsp(struct se_cmd *se_cmd)
{
	struct ibmvscsis_cmd *cmd = container_of(se_cmd, struct ibmvscsis_cmd,
						 se_cmd);
	struct scsi_info *vscsi = cmd->adapter;
	uint len;

	pr_debug("queue_tm_rsp %p, status %d\n",
		 se_cmd, (int)se_cmd->se_tmr_req->response);

	srp_build_response(vscsi, cmd, &len);
	cmd->rsp.format = SRP_FORMAT;
	cmd->rsp.len = len;
}

static void ibmvscsis_aborted_task(struct se_cmd *se_cmd)
{
	/* TBD: What (if anything) should we do here? */
	pr_debug("ibmvscsis_aborted_task %p\n", se_cmd);
}

static struct se_wwn *ibmvscsis_make_tport(struct target_fabric_configfs *tf,
					   struct config_group *group,
					   const char *name)
{
	struct ibmvscsis_tport *tport;

	tport = ibmvscsis_lookup_port(name);
	if (tport) {
		tport->tport_proto_id = SCSI_PROTOCOL_SRP;
		pr_debug("make_tport(%s), pointer:%p, tport_id:%x\n",
			 name, tport, tport->tport_proto_id);
		return &tport->tport_wwn;
	}

	return ERR_PTR(-EINVAL);
}

static void ibmvscsis_drop_tport(struct se_wwn *wwn)
{
	struct ibmvscsis_tport *tport = container_of(wwn,
						     struct ibmvscsis_tport,
						     tport_wwn);

	pr_debug("drop_tport(%s)\n",
		 config_item_name(&tport->tport_wwn.wwn_group.cg_item));
}

static struct se_portal_group *ibmvscsis_make_tpg(struct se_wwn *wwn,
						  struct config_group *group,
						  const char *name)
{
	struct ibmvscsis_tport *tport =
		container_of(wwn, struct ibmvscsis_tport, tport_wwn);
	int rc;

	tport->releasing = false;

	rc = core_tpg_register(&tport->tport_wwn, &tport->se_tpg,
			       tport->tport_proto_id);
	if (rc)
		return ERR_PTR(rc);

	return &tport->se_tpg;
}

static void ibmvscsis_drop_tpg(struct se_portal_group *se_tpg)
{
	struct ibmvscsis_tport *tport = container_of(se_tpg,
						     struct ibmvscsis_tport,
						     se_tpg);

	tport->releasing = true;
	tport->enabled = false;

	/*
	 * Release the virtual I_T Nexus for this ibmvscsis TPG
	 */
	ibmvscsis_drop_nexus(tport);
	/*
	 * Deregister the se_tpg from TCM..
	 */
	core_tpg_deregister(se_tpg);
}

static ssize_t ibmvscsis_wwn_version_show(struct config_item *item,
					  char *page)
{
	return scnprintf(page, PAGE_SIZE, "%s\n", IBMVSCSIS_VERSION);
}
CONFIGFS_ATTR_RO(ibmvscsis_wwn_, version);

static struct configfs_attribute *ibmvscsis_wwn_attrs[] = {
	&ibmvscsis_wwn_attr_version,
	NULL,
};

static ssize_t ibmvscsis_tpg_enable_show(struct config_item *item,
					 char *page)
{
	struct se_portal_group *se_tpg = to_tpg(item);
	struct ibmvscsis_tport *tport = container_of(se_tpg,
						     struct ibmvscsis_tport,
						     se_tpg);

	return snprintf(page, PAGE_SIZE, "%d\n", (tport->enabled) ? 1 : 0);
}

static ssize_t ibmvscsis_tpg_enable_store(struct config_item *item,
					  const char *page, size_t count)
{
	struct se_portal_group *se_tpg = to_tpg(item);
	struct ibmvscsis_tport *tport = container_of(se_tpg,
						     struct ibmvscsis_tport,
						     se_tpg);
	struct scsi_info *vscsi = container_of(tport, struct scsi_info, tport);
	unsigned long tmp;
	int rc;
	long lrc;

	rc = kstrtoul(page, 0, &tmp);
	if (rc < 0) {
		pr_err("Unable to extract srpt_tpg_store_enable\n");
		return -EINVAL;
	}

	if ((tmp != 0) && (tmp != 1)) {
		pr_err("Illegal value for srpt_tpg_store_enable\n");
		return -EINVAL;
	}

	if (tmp) {
		spin_lock_bh(&vscsi->intr_lock);
		tport->enabled = true;
		lrc = ibmvscsis_enable_change_state(vscsi);
		if (lrc)
			pr_err("enable_change_state failed, rc %ld state %d\n",
			       lrc, vscsi->state);
		spin_unlock_bh(&vscsi->intr_lock);
	} else {
		spin_lock_bh(&vscsi->intr_lock);
		tport->enabled = false;
		/* This simulates the server going down */
		ibmvscsis_post_disconnect(vscsi, ERR_DISCONNECT, 0);
		spin_unlock_bh(&vscsi->intr_lock);
	}

	pr_debug("tpg_enable_store, tmp %ld, state %d\n", tmp, vscsi->state);

	return count;
}
CONFIGFS_ATTR(ibmvscsis_tpg_, enable);

static struct configfs_attribute *ibmvscsis_tpg_attrs[] = {
	&ibmvscsis_tpg_attr_enable,
	NULL,
};

static const struct target_core_fabric_ops ibmvscsis_ops = {
	.module				= THIS_MODULE,
	.name				= "ibmvscsis",
	.get_fabric_name		= ibmvscsis_get_fabric_name,
	.tpg_get_wwn			= ibmvscsis_get_fabric_wwn,
	.tpg_get_tag			= ibmvscsis_get_tag,
	.tpg_get_default_depth		= ibmvscsis_get_default_depth,
	.tpg_check_demo_mode		= ibmvscsis_check_true,
	.tpg_check_demo_mode_cache	= ibmvscsis_check_true,
	.tpg_check_demo_mode_write_protect = ibmvscsis_check_false,
	.tpg_check_prod_mode_write_protect = ibmvscsis_check_false,
	.tpg_get_inst_index		= ibmvscsis_tpg_get_inst_index,
	.check_stop_free		= ibmvscsis_check_stop_free,
	.release_cmd			= ibmvscsis_release_cmd,
	.sess_get_index			= ibmvscsis_sess_get_index,
	.write_pending			= ibmvscsis_write_pending,
	.write_pending_status		= ibmvscsis_write_pending_status,
	.set_default_node_attributes	= ibmvscsis_set_default_node_attrs,
	.get_cmd_state			= ibmvscsis_get_cmd_state,
	.queue_data_in			= ibmvscsis_queue_data_in,
	.queue_status			= ibmvscsis_queue_status,
	.queue_tm_rsp			= ibmvscsis_queue_tm_rsp,
	.aborted_task			= ibmvscsis_aborted_task,
	/*
	 * Setup function pointers for logic in target_core_fabric_configfs.c
	 */
	.fabric_make_wwn		= ibmvscsis_make_tport,
	.fabric_drop_wwn		= ibmvscsis_drop_tport,
	.fabric_make_tpg		= ibmvscsis_make_tpg,
	.fabric_drop_tpg		= ibmvscsis_drop_tpg,

	.tfc_wwn_attrs			= ibmvscsis_wwn_attrs,
	.tfc_tpg_base_attrs		= ibmvscsis_tpg_attrs,
};

static void ibmvscsis_dev_release(struct device *dev) {};

static struct class_attribute ibmvscsis_class_attrs[] = {
	__ATTR_NULL,
};

static struct device_attribute dev_attr_system_id =
	__ATTR(system_id, S_IRUGO, system_id_show, NULL);

static struct device_attribute dev_attr_partition_number =
	__ATTR(partition_number, S_IRUGO, partition_number_show, NULL);

static struct device_attribute dev_attr_unit_address =
	__ATTR(unit_address, S_IRUGO, unit_address_show, NULL);

static struct attribute *ibmvscsis_dev_attrs[] = {
	&dev_attr_system_id.attr,
	&dev_attr_partition_number.attr,
	&dev_attr_unit_address.attr,
};
ATTRIBUTE_GROUPS(ibmvscsis_dev);

static struct class ibmvscsis_class = {
	.name		= "ibmvscsis",
	.dev_release	= ibmvscsis_dev_release,
	.class_attrs	= ibmvscsis_class_attrs,
	.dev_groups	= ibmvscsis_dev_groups,
};

static struct vio_device_id ibmvscsis_device_table[] = {
	{ "v-scsi-host", "IBM,v-scsi-host" },
	{ "", "" }
};
MODULE_DEVICE_TABLE(vio, ibmvscsis_device_table);

static struct vio_driver ibmvscsis_driver = {
	.name = "ibmvscsis",
	.id_table = ibmvscsis_device_table,
	.probe = ibmvscsis_probe,
	.remove = ibmvscsis_remove,
};

/*
 * ibmvscsis_init() - Kernel Module initialization
 *
 * Note: vio_register_driver() registers callback functions, and at least one
 * of those callback functions calls TCM - Linux IO Target Subsystem, thus
 * the SCSI Target template must be registered before vio_register_driver()
 * is called.
 */
static int __init ibmvscsis_init(void)
{
	int rc = 0;

	rc = ibmvscsis_get_system_info();
	if (rc) {
		pr_err("rc %d from get_system_info\n", rc);
		goto out;
	}

	rc = class_register(&ibmvscsis_class);
	if (rc) {
		pr_err("failed class register\n");
		goto out;
	}

	rc = target_register_template(&ibmvscsis_ops);
	if (rc) {
		pr_err("rc %d from target_register_template\n", rc);
		goto unregister_class;
	}

	rc = vio_register_driver(&ibmvscsis_driver);
	if (rc) {
		pr_err("rc %d from vio_register_driver\n", rc);
		goto unregister_target;
	}

	return 0;

unregister_target:
	target_unregister_template(&ibmvscsis_ops);
unregister_class:
	class_unregister(&ibmvscsis_class);
out:
	return rc;
}

static void __exit ibmvscsis_exit(void)
{
	pr_info("Unregister IBM virtual SCSI host driver\n");
	vio_unregister_driver(&ibmvscsis_driver);
	target_unregister_template(&ibmvscsis_ops);
	class_unregister(&ibmvscsis_class);
}

MODULE_DESCRIPTION("IBMVSCSIS fabric driver");
MODULE_AUTHOR("Bryant G. Ly and Michael Cyr");
MODULE_LICENSE("GPL");
MODULE_VERSION(IBMVSCSIS_VERSION);
module_init(ibmvscsis_init);
module_exit(ibmvscsis_exit);
