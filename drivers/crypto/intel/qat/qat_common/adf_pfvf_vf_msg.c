// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2015 - 2021 Intel Corporation */
#include <linux/bitfield.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_pfvf_msg.h"
#include "adf_pfvf_vf_msg.h"
#include "adf_pfvf_vf_proto.h"

/**
 * adf_vf2pf_notify_init() - send init msg to PF
 * @accel_dev:  Pointer to acceleration VF device.
 *
 * Function sends an init message from the VF to a PF
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_vf2pf_notify_init(struct adf_accel_dev *accel_dev)
{
	struct pfvf_message msg = { .type = ADF_VF2PF_MSGTYPE_INIT };

	if (adf_send_vf2pf_msg(accel_dev, msg)) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to send Init event to PF\n");
		return -EFAULT;
	}
	set_bit(ADF_STATUS_PF_RUNNING, &accel_dev->status);
	return 0;
}
EXPORT_SYMBOL_GPL(adf_vf2pf_notify_init);

/**
 * adf_vf2pf_notify_shutdown() - send shutdown msg to PF
 * @accel_dev:  Pointer to acceleration VF device.
 *
 * Function sends a shutdown message from the VF to a PF
 *
 * Return: void
 */
void adf_vf2pf_notify_shutdown(struct adf_accel_dev *accel_dev)
{
	struct pfvf_message msg = { .type = ADF_VF2PF_MSGTYPE_SHUTDOWN };

	if (test_bit(ADF_STATUS_PF_RUNNING, &accel_dev->status))
		if (adf_send_vf2pf_msg(accel_dev, msg))
			dev_err(&GET_DEV(accel_dev),
				"Failed to send Shutdown event to PF\n");
}
EXPORT_SYMBOL_GPL(adf_vf2pf_notify_shutdown);

void adf_vf2pf_notify_restart_complete(struct adf_accel_dev *accel_dev)
{
	struct pfvf_message msg = { .type = ADF_VF2PF_MSGTYPE_RESTARTING_COMPLETE };

	/* Check compatibility version */
	if (accel_dev->vf.pf_compat_ver < ADF_PFVF_COMPAT_FALLBACK)
		return;

	if (adf_send_vf2pf_msg(accel_dev, msg))
		dev_err(&GET_DEV(accel_dev),
			"Failed to send Restarting complete event to PF\n");
}
EXPORT_SYMBOL_GPL(adf_vf2pf_notify_restart_complete);

int adf_vf2pf_request_version(struct adf_accel_dev *accel_dev)
{
	u8 pf_version;
	int compat;
	int ret;
	struct pfvf_message resp;
	struct pfvf_message msg = {
		.type = ADF_VF2PF_MSGTYPE_COMPAT_VER_REQ,
		.data = ADF_PFVF_COMPAT_THIS_VERSION,
	};

	BUILD_BUG_ON(ADF_PFVF_COMPAT_THIS_VERSION > 255);

	ret = adf_send_vf2pf_req(accel_dev, msg, &resp);
	if (ret) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to send Compatibility Version Request.\n");
		return ret;
	}

	pf_version = FIELD_GET(ADF_PF2VF_VERSION_RESP_VERS_MASK, resp.data);
	compat = FIELD_GET(ADF_PF2VF_VERSION_RESP_RESULT_MASK, resp.data);

	/* Response from PF received, check compatibility */
	switch (compat) {
	case ADF_PF2VF_VF_COMPATIBLE:
		break;
	case ADF_PF2VF_VF_COMPAT_UNKNOWN:
		/* VF is newer than PF - compatible for now */
		break;
	case ADF_PF2VF_VF_INCOMPATIBLE:
		dev_err(&GET_DEV(accel_dev),
			"PF (vers %d) and VF (vers %d) are not compatible\n",
			pf_version, ADF_PFVF_COMPAT_THIS_VERSION);
		return -EINVAL;
	default:
		dev_err(&GET_DEV(accel_dev),
			"Invalid response from PF; assume not compatible\n");
		return -EINVAL;
	}

	accel_dev->vf.pf_compat_ver = pf_version;
	return 0;
}

int adf_vf2pf_get_capabilities(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct capabilities_v3 cap_msg = { 0 };
	unsigned int len = sizeof(cap_msg);

	if (accel_dev->vf.pf_compat_ver < ADF_PFVF_COMPAT_CAPABILITIES)
		/* The PF is too old to support the extended capabilities */
		return 0;

	if (adf_send_vf2pf_blkmsg_req(accel_dev, ADF_VF2PF_BLKMSG_REQ_CAP_SUMMARY,
				      (u8 *)&cap_msg, &len)) {
		dev_err(&GET_DEV(accel_dev),
			"QAT: Failed to get block message response\n");
		return -EFAULT;
	}

	switch (cap_msg.hdr.version) {
	default:
		/* Newer version received, handle only the know parts */
		fallthrough;
	case ADF_PFVF_CAPABILITIES_V3_VERSION:
		if (likely(len >= sizeof(struct capabilities_v3)))
			hw_data->clock_frequency = cap_msg.frequency;
		else
			dev_info(&GET_DEV(accel_dev), "Could not get frequency");
		fallthrough;
	case ADF_PFVF_CAPABILITIES_V2_VERSION:
		if (likely(len >= sizeof(struct capabilities_v2)))
			hw_data->accel_capabilities_mask = cap_msg.capabilities;
		else
			dev_info(&GET_DEV(accel_dev), "Could not get capabilities");
		fallthrough;
	case ADF_PFVF_CAPABILITIES_V1_VERSION:
		if (likely(len >= sizeof(struct capabilities_v1))) {
			hw_data->extended_dc_capabilities = cap_msg.ext_dc_caps;
		} else {
			dev_err(&GET_DEV(accel_dev),
				"Capabilities message truncated to %d bytes\n", len);
			return -EFAULT;
		}
	}

	return 0;
}

int adf_vf2pf_get_ring_to_svc(struct adf_accel_dev *accel_dev)
{
	struct ring_to_svc_map_v1 rts_map_msg = { 0 };
	unsigned int len = sizeof(rts_map_msg);

	if (accel_dev->vf.pf_compat_ver < ADF_PFVF_COMPAT_RING_TO_SVC_MAP)
		/* Use already set default mappings */
		return 0;

	if (adf_send_vf2pf_blkmsg_req(accel_dev, ADF_VF2PF_BLKMSG_REQ_RING_SVC_MAP,
				      (u8 *)&rts_map_msg, &len)) {
		dev_err(&GET_DEV(accel_dev),
			"QAT: Failed to get block message response\n");
		return -EFAULT;
	}

	if (unlikely(len < sizeof(struct ring_to_svc_map_v1))) {
		dev_err(&GET_DEV(accel_dev),
			"RING_TO_SVC message truncated to %d bytes\n", len);
		return -EFAULT;
	}

	/* Only v1 at present */
	accel_dev->hw_device->ring_to_svc_map = rts_map_msg.map;
	return 0;
}
