// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2015 - 2021 Intel Corporation */
#include <linux/bitfield.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_pfvf_msg.h"
#include "adf_pfvf_pf_msg.h"
#include "adf_pfvf_pf_proto.h"
#include "adf_pfvf_utils.h"

typedef u8 (*pf2vf_blkmsg_data_getter_fn)(u8 const *blkmsg, u8 byte);

static const adf_pf2vf_blkmsg_provider pf2vf_blkmsg_providers[] = {
	NULL,				  /* no message type defined for value 0 */
	NULL,				  /* no message type defined for value 1 */
	adf_pf_capabilities_msg_provider, /* ADF_VF2PF_BLKMSG_REQ_CAP_SUMMARY */
	adf_pf_ring_to_svc_msg_provider,  /* ADF_VF2PF_BLKMSG_REQ_RING_SVC_MAP */
};

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
int adf_send_pf2vf_msg(struct adf_accel_dev *accel_dev, u8 vf_nr, struct pfvf_message msg)
{
	struct adf_pfvf_ops *pfvf_ops = GET_PFVF_OPS(accel_dev);
	u32 pfvf_offset = pfvf_ops->get_pf2vf_offset(vf_nr);

	return pfvf_ops->send_msg(accel_dev, msg, pfvf_offset,
				  &accel_dev->pf.vf_info[vf_nr].pf2vf_lock);
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
static struct pfvf_message adf_recv_vf2pf_msg(struct adf_accel_dev *accel_dev, u8 vf_nr)
{
	struct adf_accel_vf_info *vf_info = &accel_dev->pf.vf_info[vf_nr];
	struct adf_pfvf_ops *pfvf_ops = GET_PFVF_OPS(accel_dev);
	u32 pfvf_offset = pfvf_ops->get_vf2pf_offset(vf_nr);

	return pfvf_ops->recv_msg(accel_dev, pfvf_offset, vf_info->vf_compat_ver);
}

static adf_pf2vf_blkmsg_provider get_blkmsg_response_provider(u8 type)
{
	if (type >= ARRAY_SIZE(pf2vf_blkmsg_providers))
		return NULL;

	return pf2vf_blkmsg_providers[type];
}

/* Byte pf2vf_blkmsg_data_getter_fn callback */
static u8 adf_pf2vf_blkmsg_get_byte(u8 const *blkmsg, u8 index)
{
	return blkmsg[index];
}

/* CRC pf2vf_blkmsg_data_getter_fn callback */
static u8 adf_pf2vf_blkmsg_get_crc(u8 const *blkmsg, u8 count)
{
	/* count is 0-based, turn it into a length */
	return adf_pfvf_calc_blkmsg_crc(blkmsg, count + 1);
}

static int adf_pf2vf_blkmsg_get_data(struct adf_accel_vf_info *vf_info,
				     u8 type, u8 byte, u8 max_size, u8 *data,
				     pf2vf_blkmsg_data_getter_fn data_getter)
{
	u8 blkmsg[ADF_PFVF_BLKMSG_MSG_MAX_SIZE] = { 0 };
	struct adf_accel_dev *accel_dev = vf_info->accel_dev;
	adf_pf2vf_blkmsg_provider provider;
	u8 msg_size;

	provider = get_blkmsg_response_provider(type);

	if (unlikely(!provider)) {
		pr_err("QAT: No registered provider for message %d\n", type);
		*data = ADF_PF2VF_INVALID_BLOCK_TYPE;
		return -EINVAL;
	}

	if (unlikely((*provider)(accel_dev, blkmsg, vf_info->vf_compat_ver))) {
		pr_err("QAT: unknown error from provider for message %d\n", type);
		*data = ADF_PF2VF_UNSPECIFIED_ERROR;
		return -EINVAL;
	}

	msg_size = ADF_PFVF_BLKMSG_HEADER_SIZE + blkmsg[ADF_PFVF_BLKMSG_LEN_BYTE];

	if (unlikely(msg_size >= max_size)) {
		pr_err("QAT: Invalid size %d provided for message type %d\n",
		       msg_size, type);
		*data = ADF_PF2VF_PAYLOAD_TRUNCATED;
		return -EINVAL;
	}

	if (unlikely(byte >= msg_size)) {
		pr_err("QAT: Out-of-bound byte number %d (msg size %d)\n",
		       byte, msg_size);
		*data = ADF_PF2VF_INVALID_BYTE_NUM_REQ;
		return -EINVAL;
	}

	*data = data_getter(blkmsg, byte);
	return 0;
}

static struct pfvf_message handle_blkmsg_req(struct adf_accel_vf_info *vf_info,
					     struct pfvf_message req)
{
	u8 resp_type = ADF_PF2VF_BLKMSG_RESP_TYPE_ERROR;
	struct pfvf_message resp = { 0 };
	u8 resp_data = 0;
	u8 blk_type;
	u8 blk_byte;
	u8 byte_max;

	switch (req.type) {
	case ADF_VF2PF_MSGTYPE_LARGE_BLOCK_REQ:
		blk_type = FIELD_GET(ADF_VF2PF_LARGE_BLOCK_TYPE_MASK, req.data)
			   + ADF_VF2PF_MEDIUM_BLOCK_TYPE_MAX + 1;
		blk_byte = FIELD_GET(ADF_VF2PF_LARGE_BLOCK_BYTE_MASK, req.data);
		byte_max = ADF_VF2PF_LARGE_BLOCK_BYTE_MAX;
		break;
	case ADF_VF2PF_MSGTYPE_MEDIUM_BLOCK_REQ:
		blk_type = FIELD_GET(ADF_VF2PF_MEDIUM_BLOCK_TYPE_MASK, req.data)
			   + ADF_VF2PF_SMALL_BLOCK_TYPE_MAX + 1;
		blk_byte = FIELD_GET(ADF_VF2PF_MEDIUM_BLOCK_BYTE_MASK, req.data);
		byte_max = ADF_VF2PF_MEDIUM_BLOCK_BYTE_MAX;
		break;
	case ADF_VF2PF_MSGTYPE_SMALL_BLOCK_REQ:
		blk_type = FIELD_GET(ADF_VF2PF_SMALL_BLOCK_TYPE_MASK, req.data);
		blk_byte = FIELD_GET(ADF_VF2PF_SMALL_BLOCK_BYTE_MASK, req.data);
		byte_max = ADF_VF2PF_SMALL_BLOCK_BYTE_MAX;
		break;
	}

	/* Is this a request for CRC or data? */
	if (FIELD_GET(ADF_VF2PF_BLOCK_CRC_REQ_MASK, req.data)) {
		dev_dbg(&GET_DEV(vf_info->accel_dev),
			"BlockMsg of type %d for CRC over %d bytes received from VF%d\n",
			blk_type, blk_byte + 1, vf_info->vf_nr);

		if (!adf_pf2vf_blkmsg_get_data(vf_info, blk_type, blk_byte,
					       byte_max, &resp_data,
					       adf_pf2vf_blkmsg_get_crc))
			resp_type = ADF_PF2VF_BLKMSG_RESP_TYPE_CRC;
	} else {
		dev_dbg(&GET_DEV(vf_info->accel_dev),
			"BlockMsg of type %d for data byte %d received from VF%d\n",
			blk_type, blk_byte, vf_info->vf_nr);

		if (!adf_pf2vf_blkmsg_get_data(vf_info, blk_type, blk_byte,
					       byte_max, &resp_data,
					       adf_pf2vf_blkmsg_get_byte))
			resp_type = ADF_PF2VF_BLKMSG_RESP_TYPE_DATA;
	}

	resp.type = ADF_PF2VF_MSGTYPE_BLKMSG_RESP;
	resp.data = FIELD_PREP(ADF_PF2VF_BLKMSG_RESP_TYPE_MASK, resp_type) |
		    FIELD_PREP(ADF_PF2VF_BLKMSG_RESP_DATA_MASK, resp_data);

	return resp;
}

static struct pfvf_message handle_rp_reset_req(struct adf_accel_dev *accel_dev, u8 vf_nr,
					       struct pfvf_message req)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct pfvf_message resp = {
		.type = ADF_PF2VF_MSGTYPE_RP_RESET_RESP,
		.data = RPRESET_SUCCESS
	};
	u32 bank_number;
	u32 rsvd_field;

	bank_number = FIELD_GET(ADF_VF2PF_RNG_RESET_RP_MASK, req.data);
	rsvd_field = FIELD_GET(ADF_VF2PF_RNG_RESET_RSVD_MASK, req.data);

	dev_dbg(&GET_DEV(accel_dev),
		"Ring Pair Reset Message received from VF%d for bank 0x%x\n",
		vf_nr, bank_number);

	if (!hw_data->ring_pair_reset || rsvd_field) {
		dev_dbg(&GET_DEV(accel_dev),
			"Ring Pair Reset for VF%d is not supported\n", vf_nr);
		resp.data = RPRESET_NOT_SUPPORTED;
		goto out;
	}

	if (bank_number >= hw_data->num_banks_per_vf) {
		dev_err(&GET_DEV(accel_dev),
			"Invalid bank number (0x%x) from VF%d for Ring Reset\n",
			bank_number, vf_nr);
		resp.data = RPRESET_INVAL_BANK;
		goto out;
	}

	/* Convert the VF provided value to PF bank number */
	bank_number = vf_nr * hw_data->num_banks_per_vf + bank_number;
	if (hw_data->ring_pair_reset(accel_dev, bank_number)) {
		dev_dbg(&GET_DEV(accel_dev),
			"Ring pair reset for VF%d failure\n", vf_nr);
		resp.data = RPRESET_TIMEOUT;
		goto out;
	}

	dev_dbg(&GET_DEV(accel_dev),
		"Ring pair reset for VF%d successfully\n", vf_nr);

out:
	return resp;
}

static int adf_handle_vf2pf_msg(struct adf_accel_dev *accel_dev, u8 vf_nr,
				struct pfvf_message msg, struct pfvf_message *resp)
{
	struct adf_accel_vf_info *vf_info = &accel_dev->pf.vf_info[vf_nr];

	switch (msg.type) {
	case ADF_VF2PF_MSGTYPE_COMPAT_VER_REQ:
		{
		u8 vf_compat_ver = msg.data;
		u8 compat;

		dev_dbg(&GET_DEV(accel_dev),
			"VersionRequest received from VF%d (vers %d) to PF (vers %d)\n",
			vf_nr, vf_compat_ver, ADF_PFVF_COMPAT_THIS_VERSION);

		if (vf_compat_ver == 0)
			compat = ADF_PF2VF_VF_INCOMPATIBLE;
		else if (vf_compat_ver <= ADF_PFVF_COMPAT_THIS_VERSION)
			compat = ADF_PF2VF_VF_COMPATIBLE;
		else
			compat = ADF_PF2VF_VF_COMPAT_UNKNOWN;

		vf_info->vf_compat_ver = vf_compat_ver;

		resp->type = ADF_PF2VF_MSGTYPE_VERSION_RESP;
		resp->data = FIELD_PREP(ADF_PF2VF_VERSION_RESP_VERS_MASK,
					ADF_PFVF_COMPAT_THIS_VERSION) |
			     FIELD_PREP(ADF_PF2VF_VERSION_RESP_RESULT_MASK, compat);
		}
		break;
	case ADF_VF2PF_MSGTYPE_VERSION_REQ:
		{
		u8 compat;

		dev_dbg(&GET_DEV(accel_dev),
			"Legacy VersionRequest received from VF%d to PF (vers 1.1)\n",
			vf_nr);

		/* legacy driver, VF compat_ver is 0 */
		vf_info->vf_compat_ver = 0;

		/* PF always newer than legacy VF */
		compat = ADF_PF2VF_VF_COMPATIBLE;

		/* Set legacy major and minor version to the latest, 1.1 */
		resp->type = ADF_PF2VF_MSGTYPE_VERSION_RESP;
		resp->data = FIELD_PREP(ADF_PF2VF_VERSION_RESP_VERS_MASK, 0x11) |
			     FIELD_PREP(ADF_PF2VF_VERSION_RESP_RESULT_MASK, compat);
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
	case ADF_VF2PF_MSGTYPE_LARGE_BLOCK_REQ:
	case ADF_VF2PF_MSGTYPE_MEDIUM_BLOCK_REQ:
	case ADF_VF2PF_MSGTYPE_SMALL_BLOCK_REQ:
		*resp = handle_blkmsg_req(vf_info, msg);
		break;
	case ADF_VF2PF_MSGTYPE_RP_RESET:
		*resp = handle_rp_reset_req(accel_dev, vf_nr, msg);
		break;
	default:
		dev_dbg(&GET_DEV(accel_dev),
			"Unknown message from VF%d (type 0x%.4x, data: 0x%.4x)\n",
			vf_nr, msg.type, msg.data);
		return -ENOMSG;
	}

	return 0;
}

bool adf_recv_and_handle_vf2pf_msg(struct adf_accel_dev *accel_dev, u32 vf_nr)
{
	struct pfvf_message req;
	struct pfvf_message resp = {0};

	req = adf_recv_vf2pf_msg(accel_dev, vf_nr);
	if (!req.type)  /* Legacy or no message */
		return true;

	if (adf_handle_vf2pf_msg(accel_dev, vf_nr, req, &resp))
		return false;

	if (resp.type && adf_send_pf2vf_msg(accel_dev, vf_nr, resp))
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
	adf_pfvf_crc_init();
	spin_lock_init(&accel_dev->pf.vf2pf_ints_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(adf_enable_pf2vf_comms);
