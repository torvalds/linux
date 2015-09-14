/*
  This file is provided under a dual BSD/GPLv2 license.  When using or
  redistributing this file, you may do so under either license.

  GPL LICENSE SUMMARY
  Copyright(c) 2015 Intel Corporation.
  This program is free software; you can redistribute it and/or modify
  it under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  Contact Information:
  qat-linux@intel.com

  BSD LICENSE
  Copyright(c) 2015 Intel Corporation.
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <linux/pci.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_pf2vf_msg.h"

#define ADF_DH895XCC_EP_OFFSET	0x3A000
#define ADF_DH895XCC_ERRMSK3	(ADF_DH895XCC_EP_OFFSET + 0x1C)
#define ADF_DH895XCC_ERRMSK3_VF2PF_L_MASK(vf_mask) ((vf_mask & 0xFFFF) << 9)
#define ADF_DH895XCC_ERRMSK5	(ADF_DH895XCC_EP_OFFSET + 0xDC)
#define ADF_DH895XCC_ERRMSK5_VF2PF_U_MASK(vf_mask) (vf_mask >> 16)

/**
 * adf_enable_pf2vf_interrupts() - Enable PF to VF interrupts
 * @accel_dev:  Pointer to acceleration device.
 *
 * Function enables PF to VF interrupts
 */
void adf_enable_pf2vf_interrupts(struct adf_accel_dev *accel_dev)
{
	struct adf_accel_pci *pci_info = &accel_dev->accel_pci_dev;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	void __iomem *pmisc_bar_addr =
		pci_info->pci_bars[hw_data->get_misc_bar_id(hw_data)].virt_addr;

	ADF_CSR_WR(pmisc_bar_addr, hw_data->get_vintmsk_offset(0), 0x0);
}
EXPORT_SYMBOL_GPL(adf_enable_pf2vf_interrupts);

/**
 * adf_disable_pf2vf_interrupts() - Disable PF to VF interrupts
 * @accel_dev:  Pointer to acceleration device.
 *
 * Function disables PF to VF interrupts
 */
void adf_disable_pf2vf_interrupts(struct adf_accel_dev *accel_dev)
{
	struct adf_accel_pci *pci_info = &accel_dev->accel_pci_dev;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	void __iomem *pmisc_bar_addr =
		pci_info->pci_bars[hw_data->get_misc_bar_id(hw_data)].virt_addr;

	ADF_CSR_WR(pmisc_bar_addr, hw_data->get_vintmsk_offset(0), 0x2);
}
EXPORT_SYMBOL_GPL(adf_disable_pf2vf_interrupts);

void adf_enable_vf2pf_interrupts(struct adf_accel_dev *accel_dev,
				 u32 vf_mask)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_bar *pmisc =
			&GET_BARS(accel_dev)[hw_data->get_misc_bar_id(hw_data)];
	void __iomem *pmisc_addr = pmisc->virt_addr;
	u32 reg;

	/* Enable VF2PF Messaging Ints - VFs 1 through 16 per vf_mask[15:0] */
	if (vf_mask & 0xFFFF) {
		reg = ADF_CSR_RD(pmisc_addr, ADF_DH895XCC_ERRMSK3);
		reg &= ~ADF_DH895XCC_ERRMSK3_VF2PF_L_MASK(vf_mask);
		ADF_CSR_WR(pmisc_addr, ADF_DH895XCC_ERRMSK3, reg);
	}

	/* Enable VF2PF Messaging Ints - VFs 17 through 32 per vf_mask[31:16] */
	if (vf_mask >> 16) {
		reg = ADF_CSR_RD(pmisc_addr, ADF_DH895XCC_ERRMSK5);
		reg &= ~ADF_DH895XCC_ERRMSK5_VF2PF_U_MASK(vf_mask);
		ADF_CSR_WR(pmisc_addr, ADF_DH895XCC_ERRMSK5, reg);
	}
}

/**
 * adf_disable_pf2vf_interrupts() - Disable VF to PF interrupts
 * @accel_dev:  Pointer to acceleration device.
 *
 * Function disables VF to PF interrupts
 */
void adf_disable_vf2pf_interrupts(struct adf_accel_dev *accel_dev, u32 vf_mask)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_bar *pmisc =
			&GET_BARS(accel_dev)[hw_data->get_misc_bar_id(hw_data)];
	void __iomem *pmisc_addr = pmisc->virt_addr;
	u32 reg;

	/* Disable VF2PF interrupts for VFs 1 through 16 per vf_mask[15:0] */
	if (vf_mask & 0xFFFF) {
		reg = ADF_CSR_RD(pmisc_addr, ADF_DH895XCC_ERRMSK3) |
			ADF_DH895XCC_ERRMSK3_VF2PF_L_MASK(vf_mask);
		ADF_CSR_WR(pmisc_addr, ADF_DH895XCC_ERRMSK3, reg);
	}

	/* Disable VF2PF interrupts for VFs 17 through 32 per vf_mask[31:16] */
	if (vf_mask >> 16) {
		reg = ADF_CSR_RD(pmisc_addr, ADF_DH895XCC_ERRMSK5) |
			ADF_DH895XCC_ERRMSK5_VF2PF_U_MASK(vf_mask);
		ADF_CSR_WR(pmisc_addr, ADF_DH895XCC_ERRMSK5, reg);
	}
}
EXPORT_SYMBOL_GPL(adf_disable_vf2pf_interrupts);

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

	/* Check if PF2VF CSR is in use by remote function */
	val = ADF_CSR_RD(pmisc_bar_addr, pf2vf_offset);
	if ((val & remote_in_use_mask) == remote_in_use_pattern) {
		dev_dbg(&GET_DEV(accel_dev),
			"PF2VF CSR in use by remote function\n");
		ret = -EBUSY;
		goto out;
	}

	/* Attempt to get ownership of PF2VF CSR */
	msg &= ~local_in_use_mask;
	msg |= local_in_use_pattern;
	ADF_CSR_WR(pmisc_bar_addr, pf2vf_offset, msg);

	/* Wait in case remote func also attempting to get ownership */
	msleep(ADF_IOV_MSG_COLLISION_DETECT_DELAY);

	val = ADF_CSR_RD(pmisc_bar_addr, pf2vf_offset);
	if ((val & local_in_use_mask) != local_in_use_pattern) {
		dev_dbg(&GET_DEV(accel_dev),
			"PF2VF CSR in use by remote - collision detected\n");
		ret = -EBUSY;
		goto out;
	}

	/*
	 * This function now owns the PV2VF CSR.  The IN_USE_BY pattern must
	 * remain in the PF2VF CSR for all writes including ACK from remote
	 * until this local function relinquishes the CSR.  Send the message
	 * by interrupting the remote.
	 */
	ADF_CSR_WR(pmisc_bar_addr, pf2vf_offset, msg | int_bit);

	/* Wait for confirmation from remote func it received the message */
	do {
		msleep(ADF_IOV_MSG_ACK_DELAY);
		val = ADF_CSR_RD(pmisc_bar_addr, pf2vf_offset);
	} while ((val & int_bit) && (count++ < ADF_IOV_MSG_ACK_MAX_RETRY));

	if (val & int_bit) {
		dev_dbg(&GET_DEV(accel_dev), "ACK not received from remote\n");
		val &= ~int_bit;
		ret = -EIO;
	}

	/* Finished with PF2VF CSR; relinquish it and leave msg in CSR */
	ADF_CSR_WR(pmisc_bar_addr, pf2vf_offset, val & ~local_in_use_mask);
out:
	mutex_unlock(lock);
	return ret;
}

/**
 * adf_iov_putmsg() - send PF2VF message
 * @accel_dev:  Pointer to acceleration device.
 * @msg:	Message to send
 * @vf_nr:	VF number to which the message will be sent
 *
 * Function sends a messge from the PF to a VF
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_iov_putmsg(struct adf_accel_dev *accel_dev, u32 msg, u8 vf_nr)
{
	u32 count = 0;
	int ret;

	do {
		ret = __adf_iov_putmsg(accel_dev, msg, vf_nr);
		if (ret)
			msleep(ADF_IOV_MSG_RETRY_DELAY);
	} while (ret && (count++ < ADF_IOV_MSG_MAX_RETRIES));

	return ret;
}
EXPORT_SYMBOL_GPL(adf_iov_putmsg);

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
			 (ADF_PFVF_COMPATIBILITY_VERSION <<
			  ADF_PF2VF_VERSION_RESP_VERS_SHIFT));

		dev_dbg(&GET_DEV(accel_dev),
			"Compatibility Version Request from VF%d vers=%u\n",
			vf_nr + 1, vf_compat_ver);

		if (vf_compat_ver < hw_data->min_iov_compat_ver) {
			dev_err(&GET_DEV(accel_dev),
				"VF (vers %d) incompatible with PF (vers %d)\n",
				vf_compat_ver, ADF_PFVF_COMPATIBILITY_VERSION);
			resp |= ADF_PF2VF_VF_INCOMPATIBLE <<
				ADF_PF2VF_VERSION_RESP_RESULT_SHIFT;
		} else if (vf_compat_ver > ADF_PFVF_COMPATIBILITY_VERSION) {
			dev_err(&GET_DEV(accel_dev),
				"VF (vers %d) compat with PF (vers %d) unkn.\n",
				vf_compat_ver, ADF_PFVF_COMPATIBILITY_VERSION);
			resp |= ADF_PF2VF_VF_COMPAT_UNKNOWN <<
				ADF_PF2VF_VERSION_RESP_RESULT_SHIFT;
		} else {
			dev_dbg(&GET_DEV(accel_dev),
				"VF (vers %d) compatible with PF (vers %d)\n",
				vf_compat_ver, ADF_PFVF_COMPATIBILITY_VERSION);
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
			 (ADF_PFVF_COMPATIBILITY_VERSION <<
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

	if (resp && adf_iov_putmsg(accel_dev, resp, vf_nr))
		dev_err(&GET_DEV(accel_dev), "Failed to send response to VF\n");

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
		if (vf->init && adf_iov_putmsg(accel_dev, msg, i))
			dev_err(&GET_DEV(accel_dev),
				"Failed to send restarting msg to VF%d\n", i);
	}
}

static int adf_vf2pf_request_version(struct adf_accel_dev *accel_dev)
{
	unsigned long timeout = msecs_to_jiffies(ADF_IOV_MSG_RESP_TIMEOUT);
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 msg = 0;
	int ret;

	msg = ADF_VF2PF_MSGORIGIN_SYSTEM;
	msg |= ADF_VF2PF_MSGTYPE_COMPAT_VER_REQ << ADF_VF2PF_MSGTYPE_SHIFT;
	msg |= ADF_PFVF_COMPATIBILITY_VERSION << ADF_VF2PF_COMPAT_VER_REQ_SHIFT;
	BUILD_BUG_ON(ADF_PFVF_COMPATIBILITY_VERSION > 255);

	/* Send request from VF to PF */
	ret = adf_iov_putmsg(accel_dev, msg, 0);
	if (ret) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to send Compatibility Version Request.\n");
		return ret;
	}

	/* Wait for response */
	if (!wait_for_completion_timeout(&accel_dev->vf.iov_msg_completion,
					 timeout)) {
		dev_err(&GET_DEV(accel_dev),
			"IOV request/response message timeout expired\n");
		return -EIO;
	}

	/* Response from PF received, check compatibility */
	switch (accel_dev->vf.compatible) {
	case ADF_PF2VF_VF_COMPATIBLE:
		break;
	case ADF_PF2VF_VF_COMPAT_UNKNOWN:
		/* VF is newer than PF and decides whether it is compatible */
		if (accel_dev->vf.pf_version >= hw_data->min_iov_compat_ver)
			break;
		/* fall through */
	case ADF_PF2VF_VF_INCOMPATIBLE:
		dev_err(&GET_DEV(accel_dev),
			"PF (vers %d) and VF (vers %d) are not compatible\n",
			accel_dev->vf.pf_version,
			ADF_PFVF_COMPATIBILITY_VERSION);
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
