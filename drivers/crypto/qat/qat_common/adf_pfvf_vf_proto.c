// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2015 - 2021 Intel Corporation */
#include <linux/completion.h>
#include <linux/types.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_pfvf_msg.h"
#include "adf_pfvf_vf_msg.h"
#include "adf_pfvf_vf_proto.h"

#define ADF_PFVF_MSG_COLLISION_DETECT_DELAY	10
#define ADF_PFVF_MSG_ACK_DELAY			2
#define ADF_PFVF_MSG_ACK_MAX_RETRY		100

#define ADF_PFVF_MSG_RESP_TIMEOUT	(ADF_PFVF_MSG_ACK_DELAY * \
					 ADF_PFVF_MSG_ACK_MAX_RETRY + \
					 ADF_PFVF_MSG_COLLISION_DETECT_DELAY)

/**
 * adf_send_vf2pf_msg() - send VF to PF message
 * @accel_dev:	Pointer to acceleration device
 * @msg:	Message to send
 *
 * This function allows the VF to send a message to the PF.
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_send_vf2pf_msg(struct adf_accel_dev *accel_dev, struct pfvf_message msg)
{
	struct adf_pfvf_ops *pfvf_ops = GET_PFVF_OPS(accel_dev);
	u32 pfvf_offset = pfvf_ops->get_vf2pf_offset(0);

	return pfvf_ops->send_msg(accel_dev, msg, pfvf_offset,
				  &accel_dev->vf.vf2pf_lock);
}

/**
 * adf_recv_pf2vf_msg() - receive a PF to VF message
 * @accel_dev:	Pointer to acceleration device
 *
 * This function allows the VF to receive a message from the PF.
 *
 * Return: a valid message on success, zero otherwise.
 */
static struct pfvf_message adf_recv_pf2vf_msg(struct adf_accel_dev *accel_dev)
{
	struct adf_pfvf_ops *pfvf_ops = GET_PFVF_OPS(accel_dev);
	u32 pfvf_offset = pfvf_ops->get_pf2vf_offset(0);

	return pfvf_ops->recv_msg(accel_dev, pfvf_offset);
}

/**
 * adf_send_vf2pf_req() - send VF2PF request message
 * @accel_dev:	Pointer to acceleration device.
 * @msg:	Request message to send
 * @resp:	Returned PF response
 *
 * This function sends a message that requires a response from the VF to the PF
 * and waits for a reply.
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_send_vf2pf_req(struct adf_accel_dev *accel_dev, struct pfvf_message msg,
		       struct pfvf_message *resp)
{
	unsigned long timeout = msecs_to_jiffies(ADF_PFVF_MSG_RESP_TIMEOUT);
	int ret;

	reinit_completion(&accel_dev->vf.msg_received);

	/* Send request from VF to PF */
	ret = adf_send_vf2pf_msg(accel_dev, msg);
	if (ret) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to send request msg to PF\n");
		return ret;
	}

	/* Wait for response */
	if (!wait_for_completion_timeout(&accel_dev->vf.msg_received,
					 timeout)) {
		dev_err(&GET_DEV(accel_dev),
			"PFVF request/response message timeout expired\n");
		return -EIO;
	}

	if (likely(resp))
		*resp = accel_dev->vf.response;

	/* Once copied, set to an invalid value */
	accel_dev->vf.response.type = 0;

	return 0;
}

static bool adf_handle_pf2vf_msg(struct adf_accel_dev *accel_dev,
				 struct pfvf_message msg)
{
	switch (msg.type) {
	case ADF_PF2VF_MSGTYPE_RESTARTING:
		dev_dbg(&GET_DEV(accel_dev), "Restarting message received from PF\n");

		adf_pf2vf_handle_pf_restarting(accel_dev);
		return false;
	case ADF_PF2VF_MSGTYPE_VERSION_RESP:
		dev_dbg(&GET_DEV(accel_dev),
			"Response Message received from PF (type 0x%.4x, data 0x%.4x)\n",
			msg.type, msg.data);
		accel_dev->vf.response = msg;
		complete(&accel_dev->vf.msg_received);
		return true;
	default:
		dev_err(&GET_DEV(accel_dev),
			"Unknown message from PF (type 0x%.4x, data: 0x%.4x)\n",
			msg.type, msg.data);
	}

	return false;
}

bool adf_recv_and_handle_pf2vf_msg(struct adf_accel_dev *accel_dev)
{
	struct pfvf_message msg;

	msg = adf_recv_pf2vf_msg(accel_dev);
	if (msg.type)  /* Invalid or no message */
		return adf_handle_pf2vf_msg(accel_dev, msg);

	/* No replies for PF->VF messages at present */

	return true;
}

/**
 * adf_enable_vf2pf_comms() - Function enables communication from vf to pf
 *
 * @accel_dev: Pointer to acceleration device virtual function.
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_enable_vf2pf_comms(struct adf_accel_dev *accel_dev)
{
	adf_enable_pf2vf_interrupts(accel_dev);
	return adf_vf2pf_request_version(accel_dev);
}
EXPORT_SYMBOL_GPL(adf_enable_vf2pf_comms);
