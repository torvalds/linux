// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2015 - 2021 Intel Corporation */
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
	u32 msg = (ADF_VF2PF_MSGORIGIN_SYSTEM |
		(ADF_VF2PF_MSGTYPE_INIT << ADF_VF2PF_MSGTYPE_SHIFT));

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
	u32 msg = (ADF_VF2PF_MSGORIGIN_SYSTEM |
	    (ADF_VF2PF_MSGTYPE_SHUTDOWN << ADF_VF2PF_MSGTYPE_SHIFT));

	if (test_bit(ADF_STATUS_PF_RUNNING, &accel_dev->status))
		if (adf_send_vf2pf_msg(accel_dev, msg))
			dev_err(&GET_DEV(accel_dev),
				"Failed to send Shutdown event to PF\n");
}
EXPORT_SYMBOL_GPL(adf_vf2pf_notify_shutdown);

int adf_vf2pf_request_version(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 msg = 0;
	int ret;

	msg = ADF_VF2PF_MSGORIGIN_SYSTEM;
	msg |= ADF_VF2PF_MSGTYPE_COMPAT_VER_REQ << ADF_VF2PF_MSGTYPE_SHIFT;
	msg |= ADF_PFVF_COMPAT_THIS_VERSION << ADF_VF2PF_COMPAT_VER_REQ_SHIFT;
	BUILD_BUG_ON(ADF_PFVF_COMPAT_THIS_VERSION > 255);

	ret = adf_send_vf2pf_req(accel_dev, msg);
	if (ret) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to send Compatibility Version Request.\n");
		return ret;
	}

	/* Response from PF received, check compatibility */
	switch (accel_dev->vf.compatible) {
	case ADF_PF2VF_VF_COMPATIBLE:
		break;
	case ADF_PF2VF_VF_COMPAT_UNKNOWN:
		/* VF is newer than PF and decides whether it is compatible */
		if (accel_dev->vf.pf_version >= hw_data->min_iov_compat_ver) {
			accel_dev->vf.compatible = ADF_PF2VF_VF_COMPATIBLE;
			break;
		}
		fallthrough;
	case ADF_PF2VF_VF_INCOMPATIBLE:
		dev_err(&GET_DEV(accel_dev),
			"PF (vers %d) and VF (vers %d) are not compatible\n",
			accel_dev->vf.pf_version,
			ADF_PFVF_COMPAT_THIS_VERSION);
		return -EINVAL;
	default:
		dev_err(&GET_DEV(accel_dev),
			"Invalid response from PF; assume not compatible\n");
		return -EINVAL;
	}
	return ret;
}
