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
int adf_send_vf2pf_msg(struct adf_accel_dev *accel_dev, u32 msg)
{
	return GET_PFVF_OPS(accel_dev)->send_msg(accel_dev, msg, 0);
}

/**
 * adf_recv_pf2vf_msg() - receive a PF to VF message
 * @accel_dev:	Pointer to acceleration device
 *
 * This function allows the VF to receive a message from the PF.
 *
 * Return: a valid message on success, zero otherwise.
 */
static u32 adf_recv_pf2vf_msg(struct adf_accel_dev *accel_dev)
{
	return GET_PFVF_OPS(accel_dev)->recv_msg(accel_dev, 0);
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
int adf_send_vf2pf_req(struct adf_accel_dev *accel_dev, u32 msg, u32 *resp)
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
	accel_dev->vf.response = 0;

	return 0;
}

static bool adf_handle_pf2vf_msg(struct adf_accel_dev *accel_dev, u32 msg)
{
	switch ((msg & ADF_PF2VF_MSGTYPE_MASK) >> ADF_PF2VF_MSGTYPE_SHIFT) {
	case ADF_PF2VF_MSGTYPE_RESTARTING:
		dev_dbg(&GET_DEV(accel_dev),
			"Restarting msg received from PF 0x%x\n", msg);

		adf_pf2vf_handle_pf_restarting(accel_dev);
		return false;
	case ADF_PF2VF_MSGTYPE_VERSION_RESP:
		accel_dev->vf.response = msg;
		complete(&accel_dev->vf.msg_received);
		return true;
	default:
		dev_err(&GET_DEV(accel_dev),
			"Unknown PF2VF message(0x%x)\n", msg);
	}

	return false;
}

bool adf_recv_and_handle_pf2vf_msg(struct adf_accel_dev *accel_dev)
{
	u32 msg;

	msg = adf_recv_pf2vf_msg(accel_dev);
	if (msg)
		return adf_handle_pf2vf_msg(accel_dev, msg);

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
