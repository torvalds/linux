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

static bool adf_handle_pf2vf_msg(struct adf_accel_dev *accel_dev, u32 msg)
{
	switch ((msg & ADF_PF2VF_MSGTYPE_MASK) >> ADF_PF2VF_MSGTYPE_SHIFT) {
	case ADF_PF2VF_MSGTYPE_RESTARTING:
		dev_dbg(&GET_DEV(accel_dev),
			"Restarting msg received from PF 0x%x\n", msg);

		adf_pf2vf_handle_pf_restarting(accel_dev);
		return false;
	case ADF_PF2VF_MSGTYPE_VERSION_RESP:
		dev_dbg(&GET_DEV(accel_dev),
			"Version resp received from PF 0x%x\n", msg);
		accel_dev->vf.pf_version =
			(msg & ADF_PF2VF_VERSION_RESP_VERS_MASK) >>
			ADF_PF2VF_VERSION_RESP_VERS_SHIFT;
		accel_dev->vf.compatible =
			(msg & ADF_PF2VF_VERSION_RESP_RESULT_MASK) >>
			ADF_PF2VF_VERSION_RESP_RESULT_SHIFT;
		complete(&accel_dev->vf.iov_msg_completion);
		return true;
	default:
		dev_err(&GET_DEV(accel_dev),
			"Unknown PF2VF message(0x%x)\n", msg);
	}

	return false;
}

bool adf_recv_and_handle_pf2vf_msg(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_bar *pmisc =
			&GET_BARS(accel_dev)[hw_data->get_misc_bar_id(hw_data)];
	void __iomem *pmisc_bar_addr = pmisc->virt_addr;
	u32 offset = hw_data->get_pf2vf_offset(0);
	u32 msg;

	/* Read the message from PF */
	msg = ADF_CSR_RD(pmisc_bar_addr, offset);
	if (!(msg & ADF_PF2VF_INT)) {
		dev_info(&GET_DEV(accel_dev),
			 "Spurious PF2VF interrupt, msg %X. Ignored\n", msg);
		return true;
	}

	if (!(msg & ADF_PF2VF_MSGORIGIN_SYSTEM))
		/* Ignore legacy non-system (non-kernel) PF2VF messages */
		return true;

	/* To ack, clear the PF2VFINT bit */
	msg &= ~ADF_PF2VF_INT;
	ADF_CSR_WR(pmisc_bar_addr, offset, msg);

	return adf_handle_pf2vf_msg(accel_dev, msg);
}
