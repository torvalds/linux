// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2015 - 2020 Intel Corporation */
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_pf2vf_msg.h"

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
