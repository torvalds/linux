// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2015 - 2021 Intel Corporation */
#include <linux/spinlock.h>
#include <linux/types.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_pfvf_msg.h"
#include "adf_pfvf_pf_proto.h"

/**
 * adf_send_pf2vf_msg() - send PF to VF message
 * @accel_dev:	Pointer to acceleration device
 * @vf_nr:	VF number to which the message will be sent
 * @msg:	Message to send
 *
 * This function allows the PF to send a message to a specific VF.
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_send_pf2vf_msg(struct adf_accel_dev *accel_dev, u8 vf_nr, u32 msg)
{
	return GET_PFVF_OPS(accel_dev)->send_msg(accel_dev, msg, vf_nr);
}

/**
 * adf_recv_vf2pf_msg() - receive a VF to PF message
 * @accel_dev:	Pointer to acceleration device
 * @vf_nr:	Number of the VF from where the message will be received
 *
 * This function allows the PF to receive a message from a specific VF.
 *
 * Return: a valid message on success, zero otherwise.
 */
static u32 adf_recv_vf2pf_msg(struct adf_accel_dev *accel_dev, u8 vf_nr)
{
	return GET_PFVF_OPS(accel_dev)->recv_msg(accel_dev, vf_nr);
}

static int adf_handle_vf2pf_msg(struct adf_accel_dev *accel_dev, u32 vf_nr,
				u32 msg, u32 *response)
{
	struct adf_accel_vf_info *vf_info = &accel_dev->pf.vf_info[vf_nr];
	u32 resp = 0;

	switch ((msg & ADF_VF2PF_MSGTYPE_MASK) >> ADF_VF2PF_MSGTYPE_SHIFT) {
	case ADF_VF2PF_MSGTYPE_COMPAT_VER_REQ:
		{
		u8 vf_compat_ver = msg >> ADF_VF2PF_COMPAT_VER_REQ_SHIFT;
		u8 compat;

		dev_dbg(&GET_DEV(accel_dev),
			"VersionRequest received from VF%d (vers %d) to PF (vers %d)\n",
			vf_nr, vf_compat_ver, ADF_PFVF_COMPAT_THIS_VERSION);

		if (vf_compat_ver <= ADF_PFVF_COMPAT_THIS_VERSION)
			compat = ADF_PF2VF_VF_COMPATIBLE;
		else
			compat = ADF_PF2VF_VF_COMPAT_UNKNOWN;

		resp =  ADF_PF2VF_MSGORIGIN_SYSTEM;
		resp |= ADF_PF2VF_MSGTYPE_VERSION_RESP <<
			ADF_PF2VF_MSGTYPE_SHIFT;
		resp |= ADF_PFVF_COMPAT_THIS_VERSION <<
			ADF_PF2VF_VERSION_RESP_VERS_SHIFT;
		resp |= compat << ADF_PF2VF_VERSION_RESP_RESULT_SHIFT;
		}
		break;
	case ADF_VF2PF_MSGTYPE_VERSION_REQ:
		{
		u8 compat;

		dev_dbg(&GET_DEV(accel_dev),
			"Legacy VersionRequest received from VF%d to PF (vers 1.1)\n",
			vf_nr);

		/* PF always newer than legacy VF */
		compat = ADF_PF2VF_VF_COMPATIBLE;

		resp = ADF_PF2VF_MSGORIGIN_SYSTEM;
		resp |= ADF_PF2VF_MSGTYPE_VERSION_RESP <<
			ADF_PF2VF_MSGTYPE_SHIFT;
		/* Set legacy major and minor version num */
		resp |= 1 << ADF_PF2VF_MAJORVERSION_SHIFT |
			1 << ADF_PF2VF_MINORVERSION_SHIFT;
		resp |= compat << ADF_PF2VF_VERSION_RESP_RESULT_SHIFT;
		}
		break;
	case ADF_VF2PF_MSGTYPE_INIT:
		{
		dev_dbg(&GET_DEV(accel_dev),
			"Init message received from VF%d\n", vf_nr);
		vf_info->init = true;
		}
		break;
	case ADF_VF2PF_MSGTYPE_SHUTDOWN:
		{
		dev_dbg(&GET_DEV(accel_dev),
			"Shutdown message received from VF%d\n", vf_nr);
		vf_info->init = false;
		}
		break;
	default:
		dev_dbg(&GET_DEV(accel_dev), "Unknown message from VF%d (0x%.8x)\n",
			vf_nr, msg);
		return -ENOMSG;
	}

	*response = resp;

	return 0;
}

bool adf_recv_and_handle_vf2pf_msg(struct adf_accel_dev *accel_dev, u32 vf_nr)
{
	u32 resp = 0;
	u32 msg;

	msg = adf_recv_vf2pf_msg(accel_dev, vf_nr);
	if (!msg)
		return true;

	if (adf_handle_vf2pf_msg(accel_dev, vf_nr, msg, &resp))
		return false;

	if (resp && adf_send_pf2vf_msg(accel_dev, vf_nr, resp))
		dev_err(&GET_DEV(accel_dev),
			"Failed to send response to VF%d\n", vf_nr);

	return true;
}

/**
 * adf_enable_pf2vf_comms() - Function enables communication from pf to vf
 *
 * @accel_dev: Pointer to acceleration device virtual function.
 *
 * This function carries out the necessary steps to setup and start the PFVF
 * communication channel, if any.
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_enable_pf2vf_comms(struct adf_accel_dev *accel_dev)
{
	spin_lock_init(&accel_dev->pf.vf2pf_ints_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(adf_enable_pf2vf_comms);
