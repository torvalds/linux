// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2015 - 2020 Intel Corporation */
#include <linux/delay.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_pf2vf_msg.h"

#define ADF_PFVF_MSG_COLLISION_DETECT_DELAY	10
#define ADF_PFVF_MSG_ACK_DELAY			2
#define ADF_PFVF_MSG_ACK_MAX_RETRY		100
#define ADF_PFVF_MSG_RETRY_DELAY		5
#define ADF_PFVF_MSG_MAX_RETRIES		3
#define ADF_PFVF_MSG_RESP_TIMEOUT	(ADF_PFVF_MSG_ACK_DELAY * \
					 ADF_PFVF_MSG_ACK_MAX_RETRY + \
					 ADF_PFVF_MSG_COLLISION_DETECT_DELAY)

void adf_enable_vf2pf_interrupts(struct adf_accel_dev *accel_dev, u32 vf_mask)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 misc_bar_id = hw_data->get_misc_bar_id(hw_data);
	struct adf_bar *pmisc = &GET_BARS(accel_dev)[misc_bar_id];
	void __iomem *pmisc_addr = pmisc->virt_addr;
	unsigned long flags;

	spin_lock_irqsave(&accel_dev->pf.vf2pf_ints_lock, flags);
	hw_data->enable_vf2pf_interrupts(pmisc_addr, vf_mask);
	spin_unlock_irqrestore(&accel_dev->pf.vf2pf_ints_lock, flags);
}

void adf_disable_vf2pf_interrupts(struct adf_accel_dev *accel_dev, u32 vf_mask)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 misc_bar_id = hw_data->get_misc_bar_id(hw_data);
	struct adf_bar *pmisc = &GET_BARS(accel_dev)[misc_bar_id];
	void __iomem *pmisc_addr = pmisc->virt_addr;
	unsigned long flags;

	spin_lock_irqsave(&accel_dev->pf.vf2pf_ints_lock, flags);
	hw_data->disable_vf2pf_interrupts(pmisc_addr, vf_mask);
	spin_unlock_irqrestore(&accel_dev->pf.vf2pf_ints_lock, flags);
}

void adf_disable_vf2pf_interrupts_irq(struct adf_accel_dev *accel_dev,
				      u32 vf_mask)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 misc_bar_id = hw_data->get_misc_bar_id(hw_data);
	struct adf_bar *pmisc = &GET_BARS(accel_dev)[misc_bar_id];
	void __iomem *pmisc_addr = pmisc->virt_addr;

	spin_lock(&accel_dev->pf.vf2pf_ints_lock);
	hw_data->disable_vf2pf_interrupts(pmisc_addr, vf_mask);
	spin_unlock(&accel_dev->pf.vf2pf_ints_lock);
}

static int __adf_iov_putmsg(struct adf_accel_dev *accel_dev, u32 msg, u8 vf_nr)
{
	struct adf_accel_pci *pci_info = &accel_dev->accel_pci_dev;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	void __iomem *pmisc_bar_addr =
		pci_info->pci_bars[hw_data->get_misc_bar_id(hw_data)].virt_addr;
	u32 val, pf2vf_offset, count = 0;
	u32 local_in_use_mask, local_in_use_pattern;
	u32 remote_in_use_mask, remote_in_use_pattern;
	struct mutex *lock;	/* lock preventing concurrent acces of CSR */
	u32 int_bit;
	int ret = 0;

	if (accel_dev->is_vf) {
		pf2vf_offset = hw_data->get_pf2vf_offset(0);
		lock = &accel_dev->vf.vf2pf_lock;
		local_in_use_mask = ADF_VF2PF_IN_USE_BY_VF_MASK;
		local_in_use_pattern = ADF_VF2PF_IN_USE_BY_VF;
		remote_in_use_mask = ADF_PF2VF_IN_USE_BY_PF_MASK;
		remote_in_use_pattern = ADF_PF2VF_IN_USE_BY_PF;
		int_bit = ADF_VF2PF_INT;
	} else {
		pf2vf_offset = hw_data->get_pf2vf_offset(vf_nr);
		lock = &accel_dev->pf.vf_info[vf_nr].pf2vf_lock;
		local_in_use_mask = ADF_PF2VF_IN_USE_BY_PF_MASK;
		local_in_use_pattern = ADF_PF2VF_IN_USE_BY_PF;
		remote_in_use_mask = ADF_VF2PF_IN_USE_BY_VF_MASK;
		remote_in_use_pattern = ADF_VF2PF_IN_USE_BY_VF;
		int_bit = ADF_PF2VF_INT;
	}

	mutex_lock(lock);

	/* Check if the PFVF CSR is in use by remote function */
	val = ADF_CSR_RD(pmisc_bar_addr, pf2vf_offset);
	if ((val & remote_in_use_mask) == remote_in_use_pattern) {
		dev_dbg(&GET_DEV(accel_dev),
			"PFVF CSR in use by remote function\n");
		ret = -EBUSY;
		goto out;
	}

	msg &= ~local_in_use_mask;
	msg |= local_in_use_pattern;

	/* Attempt to get ownership of the PFVF CSR */
	ADF_CSR_WR(pmisc_bar_addr, pf2vf_offset, msg | int_bit);

	/* Wait for confirmation from remote func it received the message */
	do {
		msleep(ADF_PFVF_MSG_ACK_DELAY);
		val = ADF_CSR_RD(pmisc_bar_addr, pf2vf_offset);
	} while ((val & int_bit) && (count++ < ADF_PFVF_MSG_ACK_MAX_RETRY));

	if (val != msg) {
		dev_dbg(&GET_DEV(accel_dev),
			"Collision - PFVF CSR overwritten by remote function\n");
		ret = -EIO;
		goto out;
	}

	if (val & int_bit) {
		dev_dbg(&GET_DEV(accel_dev), "ACK not received from remote\n");
		val &= ~int_bit;
		ret = -EIO;
	}

	/* Finished with the PFVF CSR; relinquish it and leave msg in CSR */
	ADF_CSR_WR(pmisc_bar_addr, pf2vf_offset, val & ~local_in_use_mask);
out:
	mutex_unlock(lock);
	return ret;
}

/**
 * adf_iov_putmsg() - send PFVF message
 * @accel_dev:  Pointer to acceleration device.
 * @msg:	Message to send
 * @vf_nr:	VF number to which the message will be sent if on PF, ignored
 *		otherwise
 *
 * Function sends a message through the PFVF channel
 *
 * Return: 0 on success, error code otherwise.
 */
static int adf_iov_putmsg(struct adf_accel_dev *accel_dev, u32 msg, u8 vf_nr)
{
	u32 count = 0;
	int ret;

	do {
		ret = __adf_iov_putmsg(accel_dev, msg, vf_nr);
		if (ret)
			msleep(ADF_PFVF_MSG_RETRY_DELAY);
	} while (ret && (count++ < ADF_PFVF_MSG_MAX_RETRIES));

	return ret;
}

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
static int adf_send_pf2vf_msg(struct adf_accel_dev *accel_dev, u8 vf_nr, u32 msg)
{
	return adf_iov_putmsg(accel_dev, msg, vf_nr);
}

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
	return adf_iov_putmsg(accel_dev, msg, 0);
}

/**
 * adf_send_vf2pf_req() - send VF2PF request message
 * @accel_dev:	Pointer to acceleration device.
 * @msg:	Request message to send
 *
 * This function sends a message that requires a response from the VF to the PF
 * and waits for a reply.
 *
 * Return: 0 on success, error code otherwise.
 */
static int adf_send_vf2pf_req(struct adf_accel_dev *accel_dev, u32 msg)
{
	unsigned long timeout = msecs_to_jiffies(ADF_PFVF_MSG_RESP_TIMEOUT);
	int ret;

	reinit_completion(&accel_dev->vf.iov_msg_completion);

	/* Send request from VF to PF */
	ret = adf_send_vf2pf_msg(accel_dev, msg);
	if (ret) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to send request msg to PF\n");
		return ret;
	}

	/* Wait for response */
	if (!wait_for_completion_timeout(&accel_dev->vf.iov_msg_completion,
					 timeout)) {
		dev_err(&GET_DEV(accel_dev),
			"PFVF request/response message timeout expired\n");
		return -EIO;
	}

	return 0;
}

void adf_vf2pf_req_hndl(struct adf_accel_vf_info *vf_info)
{
	struct adf_accel_dev *accel_dev = vf_info->accel_dev;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	int bar_id = hw_data->get_misc_bar_id(hw_data);
	struct adf_bar *pmisc = &GET_BARS(accel_dev)[bar_id];
	void __iomem *pmisc_addr = pmisc->virt_addr;
	u32 msg, resp = 0, vf_nr = vf_info->vf_nr;

	/* Read message from the VF */
	msg = ADF_CSR_RD(pmisc_addr, hw_data->get_pf2vf_offset(vf_nr));
	if (!(msg & ADF_VF2PF_INT)) {
		dev_info(&GET_DEV(accel_dev),
			 "Spurious VF2PF interrupt, msg %X. Ignored\n", msg);
		goto out;
	}

	/* To ACK, clear the VF2PFINT bit */
	msg &= ~ADF_VF2PF_INT;
	ADF_CSR_WR(pmisc_addr, hw_data->get_pf2vf_offset(vf_nr), msg);

	if (!(msg & ADF_VF2PF_MSGORIGIN_SYSTEM))
		/* Ignore legacy non-system (non-kernel) VF2PF messages */
		goto err;

	switch ((msg & ADF_VF2PF_MSGTYPE_MASK) >> ADF_VF2PF_MSGTYPE_SHIFT) {
	case ADF_VF2PF_MSGTYPE_COMPAT_VER_REQ:
		{
		u8 vf_compat_ver = msg >> ADF_VF2PF_COMPAT_VER_REQ_SHIFT;

		resp = (ADF_PF2VF_MSGORIGIN_SYSTEM |
			 (ADF_PF2VF_MSGTYPE_VERSION_RESP <<
			  ADF_PF2VF_MSGTYPE_SHIFT) |
			 (ADF_PFVF_COMPAT_THIS_VERSION <<
			  ADF_PF2VF_VERSION_RESP_VERS_SHIFT));

		dev_dbg(&GET_DEV(accel_dev),
			"Compatibility Version Request from VF%d vers=%u\n",
			vf_nr + 1, vf_compat_ver);

		if (vf_compat_ver < hw_data->min_iov_compat_ver) {
			dev_err(&GET_DEV(accel_dev),
				"VF (vers %d) incompatible with PF (vers %d)\n",
				vf_compat_ver, ADF_PFVF_COMPAT_THIS_VERSION);
			resp |= ADF_PF2VF_VF_INCOMPATIBLE <<
				ADF_PF2VF_VERSION_RESP_RESULT_SHIFT;
		} else if (vf_compat_ver > ADF_PFVF_COMPAT_THIS_VERSION) {
			dev_err(&GET_DEV(accel_dev),
				"VF (vers %d) compat with PF (vers %d) unkn.\n",
				vf_compat_ver, ADF_PFVF_COMPAT_THIS_VERSION);
			resp |= ADF_PF2VF_VF_COMPAT_UNKNOWN <<
				ADF_PF2VF_VERSION_RESP_RESULT_SHIFT;
		} else {
			dev_dbg(&GET_DEV(accel_dev),
				"VF (vers %d) compatible with PF (vers %d)\n",
				vf_compat_ver, ADF_PFVF_COMPAT_THIS_VERSION);
			resp |= ADF_PF2VF_VF_COMPATIBLE <<
				ADF_PF2VF_VERSION_RESP_RESULT_SHIFT;
		}
		}
		break;
	case ADF_VF2PF_MSGTYPE_VERSION_REQ:
		dev_dbg(&GET_DEV(accel_dev),
			"Legacy VersionRequest received from VF%d 0x%x\n",
			vf_nr + 1, msg);
		resp = (ADF_PF2VF_MSGORIGIN_SYSTEM |
			 (ADF_PF2VF_MSGTYPE_VERSION_RESP <<
			  ADF_PF2VF_MSGTYPE_SHIFT) |
			 (ADF_PFVF_COMPAT_THIS_VERSION <<
			  ADF_PF2VF_VERSION_RESP_VERS_SHIFT));
		resp |= ADF_PF2VF_VF_COMPATIBLE <<
			ADF_PF2VF_VERSION_RESP_RESULT_SHIFT;
		/* Set legacy major and minor version num */
		resp |= 1 << ADF_PF2VF_MAJORVERSION_SHIFT |
			1 << ADF_PF2VF_MINORVERSION_SHIFT;
		break;
	case ADF_VF2PF_MSGTYPE_INIT:
		{
		dev_dbg(&GET_DEV(accel_dev),
			"Init message received from VF%d 0x%x\n",
			vf_nr + 1, msg);
		vf_info->init = true;
		}
		break;
	case ADF_VF2PF_MSGTYPE_SHUTDOWN:
		{
		dev_dbg(&GET_DEV(accel_dev),
			"Shutdown message received from VF%d 0x%x\n",
			vf_nr + 1, msg);
		vf_info->init = false;
		}
		break;
	default:
		goto err;
	}

	if (resp && adf_send_pf2vf_msg(accel_dev, vf_nr, resp))
		dev_err(&GET_DEV(accel_dev), "Failed to send response to VF\n");

out:
	/* re-enable interrupt on PF from this VF */
	adf_enable_vf2pf_interrupts(accel_dev, (1 << vf_nr));

	return;
err:
	dev_dbg(&GET_DEV(accel_dev), "Unknown message from VF%d (0x%x);\n",
		vf_nr + 1, msg);
}

void adf_pf2vf_notify_restarting(struct adf_accel_dev *accel_dev)
{
	struct adf_accel_vf_info *vf;
	u32 msg = (ADF_PF2VF_MSGORIGIN_SYSTEM |
		(ADF_PF2VF_MSGTYPE_RESTARTING << ADF_PF2VF_MSGTYPE_SHIFT));
	int i, num_vfs = pci_num_vf(accel_to_pci_dev(accel_dev));

	for (i = 0, vf = accel_dev->pf.vf_info; i < num_vfs; i++, vf++) {
		if (vf->init && adf_send_pf2vf_msg(accel_dev, i, msg))
			dev_err(&GET_DEV(accel_dev),
				"Failed to send restarting msg to VF%d\n", i);
	}
}

static int adf_vf2pf_request_version(struct adf_accel_dev *accel_dev)
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
