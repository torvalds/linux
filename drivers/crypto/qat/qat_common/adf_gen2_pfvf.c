// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2021 Intel Corporation */
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_gen2_pfvf.h"
#include "adf_pfvf_msg.h"
#include "adf_pfvf_pf_proto.h"
#include "adf_pfvf_vf_proto.h"

 /* VF2PF interrupts */
#define ADF_GEN2_ERR_REG_VF2PF(vf_src)	(((vf_src) & 0x01FFFE00) >> 9)
#define ADF_GEN2_ERR_MSK_VF2PF(vf_mask)	(((vf_mask) & 0xFFFF) << 9)

#define ADF_GEN2_PF_PF2VF_OFFSET(i)	(0x3A000 + 0x280 + ((i) * 0x04))
#define ADF_GEN2_VF_PF2VF_OFFSET	0x200

#define ADF_PFVF_MSG_ACK_DELAY		2
#define ADF_PFVF_MSG_ACK_MAX_RETRY	100

#define ADF_PFVF_MSG_RETRY_DELAY	5
#define ADF_PFVF_MSG_MAX_RETRIES	3

static u32 adf_gen2_pf_get_pfvf_offset(u32 i)
{
	return ADF_GEN2_PF_PF2VF_OFFSET(i);
}

static u32 adf_gen2_vf_get_pfvf_offset(u32 i)
{
	return ADF_GEN2_VF_PF2VF_OFFSET;
}

static u32 adf_gen2_get_vf2pf_sources(void __iomem *pmisc_addr)
{
	u32 errsou3, errmsk3, vf_int_mask;

	/* Get the interrupt sources triggered by VFs */
	errsou3 = ADF_CSR_RD(pmisc_addr, ADF_GEN2_ERRSOU3);
	vf_int_mask = ADF_GEN2_ERR_REG_VF2PF(errsou3);

	/* To avoid adding duplicate entries to work queue, clear
	 * vf_int_mask_sets bits that are already masked in ERRMSK register.
	 */
	errmsk3 = ADF_CSR_RD(pmisc_addr, ADF_GEN2_ERRMSK3);
	vf_int_mask &= ~ADF_GEN2_ERR_REG_VF2PF(errmsk3);

	return vf_int_mask;
}

static void adf_gen2_enable_vf2pf_interrupts(void __iomem *pmisc_addr,
					     u32 vf_mask)
{
	/* Enable VF2PF Messaging Ints - VFs 0 through 15 per vf_mask[15:0] */
	if (vf_mask & 0xFFFF) {
		u32 val = ADF_CSR_RD(pmisc_addr, ADF_GEN2_ERRMSK3)
			  & ~ADF_GEN2_ERR_MSK_VF2PF(vf_mask);
		ADF_CSR_WR(pmisc_addr, ADF_GEN2_ERRMSK3, val);
	}
}

static void adf_gen2_disable_vf2pf_interrupts(void __iomem *pmisc_addr,
					      u32 vf_mask)
{
	/* Disable VF2PF interrupts for VFs 0 through 15 per vf_mask[15:0] */
	if (vf_mask & 0xFFFF) {
		u32 val = ADF_CSR_RD(pmisc_addr, ADF_GEN2_ERRMSK3)
			  | ADF_GEN2_ERR_MSK_VF2PF(vf_mask);
		ADF_CSR_WR(pmisc_addr, ADF_GEN2_ERRMSK3, val);
	}
}

static int adf_gen2_pfvf_send(struct adf_accel_dev *accel_dev, u32 msg,
			      u8 vf_nr)
{
	struct adf_accel_pci *pci_info = &accel_dev->accel_pci_dev;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	void __iomem *pmisc_bar_addr =
		pci_info->pci_bars[hw_data->get_misc_bar_id(hw_data)].virt_addr;
	u32 val, pfvf_offset, count = 0;
	u32 local_in_use_mask, local_in_use_pattern;
	u32 remote_in_use_mask, remote_in_use_pattern;
	struct mutex *lock;	/* lock preventing concurrent acces of CSR */
	unsigned int retries = ADF_PFVF_MSG_MAX_RETRIES;
	u32 int_bit;
	int ret;

	if (accel_dev->is_vf) {
		pfvf_offset = GET_PFVF_OPS(accel_dev)->get_vf2pf_offset(0);
		lock = &accel_dev->vf.vf2pf_lock;
		local_in_use_mask = ADF_VF2PF_IN_USE_BY_VF_MASK;
		local_in_use_pattern = ADF_VF2PF_IN_USE_BY_VF;
		remote_in_use_mask = ADF_PF2VF_IN_USE_BY_PF_MASK;
		remote_in_use_pattern = ADF_PF2VF_IN_USE_BY_PF;
		int_bit = ADF_VF2PF_INT;
	} else {
		pfvf_offset = GET_PFVF_OPS(accel_dev)->get_pf2vf_offset(vf_nr);
		lock = &accel_dev->pf.vf_info[vf_nr].pf2vf_lock;
		local_in_use_mask = ADF_PF2VF_IN_USE_BY_PF_MASK;
		local_in_use_pattern = ADF_PF2VF_IN_USE_BY_PF;
		remote_in_use_mask = ADF_VF2PF_IN_USE_BY_VF_MASK;
		remote_in_use_pattern = ADF_VF2PF_IN_USE_BY_VF;
		int_bit = ADF_PF2VF_INT;
	}

	msg &= ~local_in_use_mask;
	msg |= local_in_use_pattern;

	mutex_lock(lock);

start:
	ret = 0;

	/* Check if the PFVF CSR is in use by remote function */
	val = ADF_CSR_RD(pmisc_bar_addr, pfvf_offset);
	if ((val & remote_in_use_mask) == remote_in_use_pattern) {
		dev_dbg(&GET_DEV(accel_dev),
			"PFVF CSR in use by remote function\n");
		goto retry;
	}

	/* Attempt to get ownership of the PFVF CSR */
	ADF_CSR_WR(pmisc_bar_addr, pfvf_offset, msg | int_bit);

	/* Wait for confirmation from remote func it received the message */
	do {
		msleep(ADF_PFVF_MSG_ACK_DELAY);
		val = ADF_CSR_RD(pmisc_bar_addr, pfvf_offset);
	} while ((val & int_bit) && (count++ < ADF_PFVF_MSG_ACK_MAX_RETRY));

	if (val & int_bit) {
		dev_dbg(&GET_DEV(accel_dev), "ACK not received from remote\n");
		val &= ~int_bit;
		ret = -EIO;
	}

	if (val != msg) {
		dev_dbg(&GET_DEV(accel_dev),
			"Collision - PFVF CSR overwritten by remote function\n");
		goto retry;
	}

	/* Finished with the PFVF CSR; relinquish it and leave msg in CSR */
	ADF_CSR_WR(pmisc_bar_addr, pfvf_offset, val & ~local_in_use_mask);
out:
	mutex_unlock(lock);
	return ret;

retry:
	if (--retries) {
		msleep(ADF_PFVF_MSG_RETRY_DELAY);
		goto start;
	} else {
		ret = -EBUSY;
		goto out;
	}
}

static u32 adf_gen2_pfvf_recv(struct adf_accel_dev *accel_dev, u8 vf_nr)
{
	struct adf_accel_pci *pci_info = &accel_dev->accel_pci_dev;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	void __iomem *pmisc_addr =
		pci_info->pci_bars[hw_data->get_misc_bar_id(hw_data)].virt_addr;
	u32 pfvf_offset;
	u32 msg_origin;
	u32 int_bit;
	u32 msg;

	if (accel_dev->is_vf) {
		pfvf_offset = GET_PFVF_OPS(accel_dev)->get_pf2vf_offset(0);
		int_bit = ADF_PF2VF_INT;
		msg_origin = ADF_PF2VF_MSGORIGIN_SYSTEM;
	} else {
		pfvf_offset = GET_PFVF_OPS(accel_dev)->get_vf2pf_offset(vf_nr);
		int_bit = ADF_VF2PF_INT;
		msg_origin = ADF_VF2PF_MSGORIGIN_SYSTEM;
	}

	/* Read message */
	msg = ADF_CSR_RD(pmisc_addr, pfvf_offset);
	if (!(msg & int_bit)) {
		dev_info(&GET_DEV(accel_dev),
			 "Spurious PFVF interrupt, msg 0x%.8x. Ignored\n", msg);
		return 0;
	}

	/* Ignore legacy non-system (non-kernel) VF2PF messages */
	if (!(msg & msg_origin)) {
		dev_dbg(&GET_DEV(accel_dev),
			"Ignored non-system message (0x%.8x);\n", msg);
		return 0;
	}

	/* To ACK, clear the INT bit */
	msg &= ~int_bit;
	ADF_CSR_WR(pmisc_addr, pfvf_offset, msg);

	return msg;
}

void adf_gen2_init_pf_pfvf_ops(struct adf_pfvf_ops *pfvf_ops)
{
	pfvf_ops->enable_comms = adf_enable_pf2vf_comms;
	pfvf_ops->get_pf2vf_offset = adf_gen2_pf_get_pfvf_offset;
	pfvf_ops->get_vf2pf_offset = adf_gen2_pf_get_pfvf_offset;
	pfvf_ops->get_vf2pf_sources = adf_gen2_get_vf2pf_sources;
	pfvf_ops->enable_vf2pf_interrupts = adf_gen2_enable_vf2pf_interrupts;
	pfvf_ops->disable_vf2pf_interrupts = adf_gen2_disable_vf2pf_interrupts;
	pfvf_ops->send_msg = adf_gen2_pfvf_send;
	pfvf_ops->recv_msg = adf_gen2_pfvf_recv;
}
EXPORT_SYMBOL_GPL(adf_gen2_init_pf_pfvf_ops);

void adf_gen2_init_vf_pfvf_ops(struct adf_pfvf_ops *pfvf_ops)
{
	pfvf_ops->enable_comms = adf_enable_vf2pf_comms;
	pfvf_ops->get_pf2vf_offset = adf_gen2_vf_get_pfvf_offset;
	pfvf_ops->get_vf2pf_offset = adf_gen2_vf_get_pfvf_offset;
	pfvf_ops->send_msg = adf_gen2_pfvf_send;
	pfvf_ops->recv_msg = adf_gen2_pfvf_recv;
}
EXPORT_SYMBOL_GPL(adf_gen2_init_vf_pfvf_ops);
