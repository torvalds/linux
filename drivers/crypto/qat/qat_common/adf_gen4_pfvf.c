// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2021 Intel Corporation */
#include <linux/iopoll.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_gen4_pfvf.h"
#include "adf_pfvf_pf_proto.h"
#include "adf_pfvf_utils.h"

#define ADF_4XXX_MAX_NUM_VFS		16

#define ADF_4XXX_PF2VM_OFFSET(i)	(0x40B010 + ((i) * 0x20))
#define ADF_4XXX_VM2PF_OFFSET(i)	(0x40B014 + ((i) * 0x20))

/* VF2PF interrupt source registers */
#define ADF_4XXX_VM2PF_SOU(i)		(0x41A180 + ((i) * 4))
#define ADF_4XXX_VM2PF_MSK(i)		(0x41A1C0 + ((i) * 4))
#define ADF_4XXX_VM2PF_INT_EN_MSK	BIT(0)

#define ADF_PFVF_GEN4_MSGTYPE_SHIFT	2
#define ADF_PFVF_GEN4_MSGTYPE_MASK	0x3F
#define ADF_PFVF_GEN4_MSGDATA_SHIFT	8
#define ADF_PFVF_GEN4_MSGDATA_MASK	0xFFFFFF

static const struct pfvf_csr_format csr_gen4_fmt = {
	{ ADF_PFVF_GEN4_MSGTYPE_SHIFT, ADF_PFVF_GEN4_MSGTYPE_MASK },
	{ ADF_PFVF_GEN4_MSGDATA_SHIFT, ADF_PFVF_GEN4_MSGDATA_MASK },
};

static u32 adf_gen4_pf_get_pf2vf_offset(u32 i)
{
	return ADF_4XXX_PF2VM_OFFSET(i);
}

static u32 adf_gen4_pf_get_vf2pf_offset(u32 i)
{
	return ADF_4XXX_VM2PF_OFFSET(i);
}

static u32 adf_gen4_get_vf2pf_sources(void __iomem *pmisc_addr)
{
	int i;
	u32 sou, mask;
	int num_csrs = ADF_4XXX_MAX_NUM_VFS;
	u32 vf_mask = 0;

	for (i = 0; i < num_csrs; i++) {
		sou = ADF_CSR_RD(pmisc_addr, ADF_4XXX_VM2PF_SOU(i));
		mask = ADF_CSR_RD(pmisc_addr, ADF_4XXX_VM2PF_MSK(i));
		sou &= ~mask;
		vf_mask |= sou << i;
	}

	return vf_mask;
}

static void adf_gen4_enable_vf2pf_interrupts(void __iomem *pmisc_addr,
					     u32 vf_mask)
{
	int num_csrs = ADF_4XXX_MAX_NUM_VFS;
	unsigned long mask = vf_mask;
	unsigned int val;
	int i;

	for_each_set_bit(i, &mask, num_csrs) {
		unsigned int offset = ADF_4XXX_VM2PF_MSK(i);

		val = ADF_CSR_RD(pmisc_addr, offset) & ~ADF_4XXX_VM2PF_INT_EN_MSK;
		ADF_CSR_WR(pmisc_addr, offset, val);
	}
}

static void adf_gen4_disable_vf2pf_interrupts(void __iomem *pmisc_addr,
					      u32 vf_mask)
{
	int num_csrs = ADF_4XXX_MAX_NUM_VFS;
	unsigned long mask = vf_mask;
	unsigned int val;
	int i;

	for_each_set_bit(i, &mask, num_csrs) {
		unsigned int offset = ADF_4XXX_VM2PF_MSK(i);

		val = ADF_CSR_RD(pmisc_addr, offset) | ADF_4XXX_VM2PF_INT_EN_MSK;
		ADF_CSR_WR(pmisc_addr, offset, val);
	}
}

static int adf_gen4_pfvf_send(struct adf_accel_dev *accel_dev,
			      struct pfvf_message msg, u32 pfvf_offset,
			      struct mutex *csr_lock)
{
	void __iomem *pmisc_addr = adf_get_pmisc_base(accel_dev);
	u32 csr_val;
	int ret;

	csr_val = adf_pfvf_csr_msg_of(accel_dev, msg, &csr_gen4_fmt);
	if (unlikely(!csr_val))
		return -EINVAL;

	mutex_lock(csr_lock);

	ADF_CSR_WR(pmisc_addr, pfvf_offset, csr_val | ADF_PFVF_INT);

	/* Wait for confirmation from remote that it received the message */
	ret = read_poll_timeout(ADF_CSR_RD, csr_val, !(csr_val & ADF_PFVF_INT),
				ADF_PFVF_MSG_ACK_DELAY_US,
				ADF_PFVF_MSG_ACK_MAX_DELAY_US,
				true, pmisc_addr, pfvf_offset);
	if (ret < 0)
		dev_dbg(&GET_DEV(accel_dev), "ACK not received from remote\n");

	mutex_unlock(csr_lock);
	return ret;
}

static struct pfvf_message adf_gen4_pfvf_recv(struct adf_accel_dev *accel_dev,
					      u32 pfvf_offset, u8 compat_ver)
{
	void __iomem *pmisc_addr = adf_get_pmisc_base(accel_dev);
	u32 csr_val;

	/* Read message from the CSR */
	csr_val = ADF_CSR_RD(pmisc_addr, pfvf_offset);

	/* We can now acknowledge the message reception by clearing the
	 * interrupt bit
	 */
	ADF_CSR_WR(pmisc_addr, pfvf_offset, csr_val & ~ADF_PFVF_INT);

	/* Return the pfvf_message format */
	return adf_pfvf_message_of(accel_dev, csr_val, &csr_gen4_fmt);
}

void adf_gen4_init_pf_pfvf_ops(struct adf_pfvf_ops *pfvf_ops)
{
	pfvf_ops->enable_comms = adf_enable_pf2vf_comms;
	pfvf_ops->get_pf2vf_offset = adf_gen4_pf_get_pf2vf_offset;
	pfvf_ops->get_vf2pf_offset = adf_gen4_pf_get_vf2pf_offset;
	pfvf_ops->get_vf2pf_sources = adf_gen4_get_vf2pf_sources;
	pfvf_ops->enable_vf2pf_interrupts = adf_gen4_enable_vf2pf_interrupts;
	pfvf_ops->disable_vf2pf_interrupts = adf_gen4_disable_vf2pf_interrupts;
	pfvf_ops->send_msg = adf_gen4_pfvf_send;
	pfvf_ops->recv_msg = adf_gen4_pfvf_recv;
}
EXPORT_SYMBOL_GPL(adf_gen4_init_pf_pfvf_ops);
