/*
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 */
#include <linux/list.h>
#include "amdgpu.h"
#include "amdgpu_xgmi.h"
#include "amdgpu_ras.h"
#include "soc15.h"
#include "df/df_3_6_offset.h"
#include "xgmi/xgmi_4_0_0_smn.h"
#include "xgmi/xgmi_4_0_0_sh_mask.h"
#include "xgmi/xgmi_6_1_0_sh_mask.h"
#include "wafl/wafl2_4_0_0_smn.h"
#include "wafl/wafl2_4_0_0_sh_mask.h"

#include "amdgpu_reset.h"

#define smnPCS_XGMI3X16_PCS_ERROR_STATUS 0x11a0020c
#define smnPCS_XGMI3X16_PCS_ERROR_NONCORRECTABLE_MASK   0x11a00218
#define smnPCS_GOPX1_PCS_ERROR_STATUS    0x12200210
#define smnPCS_GOPX1_PCS_ERROR_NONCORRECTABLE_MASK      0x12200218

#define XGMI_STATE_DISABLE                      0xD1
#define XGMI_STATE_LS0                          0x81
#define XGMI_LINK_ACTIVE			1
#define XGMI_LINK_INACTIVE			0

static DEFINE_MUTEX(xgmi_mutex);

#define AMDGPU_MAX_XGMI_DEVICE_PER_HIVE		4

static LIST_HEAD(xgmi_hive_list);

static const int xgmi_pcs_err_status_reg_vg20[] = {
	smnXGMI0_PCS_GOPX16_PCS_ERROR_STATUS,
	smnXGMI0_PCS_GOPX16_PCS_ERROR_STATUS + 0x100000,
};

static const int wafl_pcs_err_status_reg_vg20[] = {
	smnPCS_GOPX1_0_PCS_GOPX1_PCS_ERROR_STATUS,
	smnPCS_GOPX1_0_PCS_GOPX1_PCS_ERROR_STATUS + 0x100000,
};

static const int xgmi_pcs_err_status_reg_arct[] = {
	smnXGMI0_PCS_GOPX16_PCS_ERROR_STATUS,
	smnXGMI0_PCS_GOPX16_PCS_ERROR_STATUS + 0x100000,
	smnXGMI0_PCS_GOPX16_PCS_ERROR_STATUS + 0x500000,
	smnXGMI0_PCS_GOPX16_PCS_ERROR_STATUS + 0x600000,
	smnXGMI0_PCS_GOPX16_PCS_ERROR_STATUS + 0x700000,
	smnXGMI0_PCS_GOPX16_PCS_ERROR_STATUS + 0x800000,
};

/* same as vg20*/
static const int wafl_pcs_err_status_reg_arct[] = {
	smnPCS_GOPX1_0_PCS_GOPX1_PCS_ERROR_STATUS,
	smnPCS_GOPX1_0_PCS_GOPX1_PCS_ERROR_STATUS + 0x100000,
};

static const int xgmi3x16_pcs_err_status_reg_aldebaran[] = {
	smnPCS_XGMI3X16_PCS_ERROR_STATUS,
	smnPCS_XGMI3X16_PCS_ERROR_STATUS + 0x100000,
	smnPCS_XGMI3X16_PCS_ERROR_STATUS + 0x200000,
	smnPCS_XGMI3X16_PCS_ERROR_STATUS + 0x300000,
	smnPCS_XGMI3X16_PCS_ERROR_STATUS + 0x400000,
	smnPCS_XGMI3X16_PCS_ERROR_STATUS + 0x500000,
	smnPCS_XGMI3X16_PCS_ERROR_STATUS + 0x600000,
	smnPCS_XGMI3X16_PCS_ERROR_STATUS + 0x700000
};

static const int xgmi3x16_pcs_err_noncorrectable_mask_reg_aldebaran[] = {
	smnPCS_XGMI3X16_PCS_ERROR_NONCORRECTABLE_MASK,
	smnPCS_XGMI3X16_PCS_ERROR_NONCORRECTABLE_MASK + 0x100000,
	smnPCS_XGMI3X16_PCS_ERROR_NONCORRECTABLE_MASK + 0x200000,
	smnPCS_XGMI3X16_PCS_ERROR_NONCORRECTABLE_MASK + 0x300000,
	smnPCS_XGMI3X16_PCS_ERROR_NONCORRECTABLE_MASK + 0x400000,
	smnPCS_XGMI3X16_PCS_ERROR_NONCORRECTABLE_MASK + 0x500000,
	smnPCS_XGMI3X16_PCS_ERROR_NONCORRECTABLE_MASK + 0x600000,
	smnPCS_XGMI3X16_PCS_ERROR_NONCORRECTABLE_MASK + 0x700000
};

static const int walf_pcs_err_status_reg_aldebaran[] = {
	smnPCS_GOPX1_PCS_ERROR_STATUS,
	smnPCS_GOPX1_PCS_ERROR_STATUS + 0x100000
};

static const int walf_pcs_err_noncorrectable_mask_reg_aldebaran[] = {
	smnPCS_GOPX1_PCS_ERROR_NONCORRECTABLE_MASK,
	smnPCS_GOPX1_PCS_ERROR_NONCORRECTABLE_MASK + 0x100000
};

static const int xgmi3x16_pcs_err_status_reg_v6_4[] = {
	smnPCS_XGMI3X16_PCS_ERROR_STATUS,
	smnPCS_XGMI3X16_PCS_ERROR_STATUS + 0x100000
};

static const int xgmi3x16_pcs_err_noncorrectable_mask_reg_v6_4[] = {
	smnPCS_XGMI3X16_PCS_ERROR_NONCORRECTABLE_MASK,
	smnPCS_XGMI3X16_PCS_ERROR_NONCORRECTABLE_MASK + 0x100000
};

static const u64 xgmi_v6_4_0_mca_base_array[] = {
	0x11a09200,
	0x11b09200,
};

static const char *xgmi_v6_4_0_ras_error_code_ext[32] = {
	[0x00] = "XGMI PCS DataLossErr",
	[0x01] = "XGMI PCS TrainingErr",
	[0x02] = "XGMI PCS FlowCtrlAckErr",
	[0x03] = "XGMI PCS RxFifoUnderflowErr",
	[0x04] = "XGMI PCS RxFifoOverflowErr",
	[0x05] = "XGMI PCS CRCErr",
	[0x06] = "XGMI PCS BERExceededErr",
	[0x07] = "XGMI PCS TxMetaDataErr",
	[0x08] = "XGMI PCS ReplayBufParityErr",
	[0x09] = "XGMI PCS DataParityErr",
	[0x0a] = "XGMI PCS ReplayFifoOverflowErr",
	[0x0b] = "XGMI PCS ReplayFifoUnderflowErr",
	[0x0c] = "XGMI PCS ElasticFifoOverflowErr",
	[0x0d] = "XGMI PCS DeskewErr",
	[0x0e] = "XGMI PCS FlowCtrlCRCErr",
	[0x0f] = "XGMI PCS DataStartupLimitErr",
	[0x10] = "XGMI PCS FCInitTimeoutErr",
	[0x11] = "XGMI PCS RecoveryTimeoutErr",
	[0x12] = "XGMI PCS ReadySerialTimeoutErr",
	[0x13] = "XGMI PCS ReadySerialAttemptErr",
	[0x14] = "XGMI PCS RecoveryAttemptErr",
	[0x15] = "XGMI PCS RecoveryRelockAttemptErr",
	[0x16] = "XGMI PCS ReplayAttemptErr",
	[0x17] = "XGMI PCS SyncHdrErr",
	[0x18] = "XGMI PCS TxReplayTimeoutErr",
	[0x19] = "XGMI PCS RxReplayTimeoutErr",
	[0x1a] = "XGMI PCS LinkSubTxTimeoutErr",
	[0x1b] = "XGMI PCS LinkSubRxTimeoutErr",
	[0x1c] = "XGMI PCS RxCMDPktErr",
};

static const struct amdgpu_pcs_ras_field xgmi_pcs_ras_fields[] = {
	{"XGMI PCS DataLossErr",
	 SOC15_REG_FIELD(XGMI0_PCS_GOPX16_PCS_ERROR_STATUS, DataLossErr)},
	{"XGMI PCS TrainingErr",
	 SOC15_REG_FIELD(XGMI0_PCS_GOPX16_PCS_ERROR_STATUS, TrainingErr)},
	{"XGMI PCS CRCErr",
	 SOC15_REG_FIELD(XGMI0_PCS_GOPX16_PCS_ERROR_STATUS, CRCErr)},
	{"XGMI PCS BERExceededErr",
	 SOC15_REG_FIELD(XGMI0_PCS_GOPX16_PCS_ERROR_STATUS, BERExceededErr)},
	{"XGMI PCS TxMetaDataErr",
	 SOC15_REG_FIELD(XGMI0_PCS_GOPX16_PCS_ERROR_STATUS, TxMetaDataErr)},
	{"XGMI PCS ReplayBufParityErr",
	 SOC15_REG_FIELD(XGMI0_PCS_GOPX16_PCS_ERROR_STATUS, ReplayBufParityErr)},
	{"XGMI PCS DataParityErr",
	 SOC15_REG_FIELD(XGMI0_PCS_GOPX16_PCS_ERROR_STATUS, DataParityErr)},
	{"XGMI PCS ReplayFifoOverflowErr",
	 SOC15_REG_FIELD(XGMI0_PCS_GOPX16_PCS_ERROR_STATUS, ReplayFifoOverflowErr)},
	{"XGMI PCS ReplayFifoUnderflowErr",
	 SOC15_REG_FIELD(XGMI0_PCS_GOPX16_PCS_ERROR_STATUS, ReplayFifoUnderflowErr)},
	{"XGMI PCS ElasticFifoOverflowErr",
	 SOC15_REG_FIELD(XGMI0_PCS_GOPX16_PCS_ERROR_STATUS, ElasticFifoOverflowErr)},
	{"XGMI PCS DeskewErr",
	 SOC15_REG_FIELD(XGMI0_PCS_GOPX16_PCS_ERROR_STATUS, DeskewErr)},
	{"XGMI PCS DataStartupLimitErr",
	 SOC15_REG_FIELD(XGMI0_PCS_GOPX16_PCS_ERROR_STATUS, DataStartupLimitErr)},
	{"XGMI PCS FCInitTimeoutErr",
	 SOC15_REG_FIELD(XGMI0_PCS_GOPX16_PCS_ERROR_STATUS, FCInitTimeoutErr)},
	{"XGMI PCS RecoveryTimeoutErr",
	 SOC15_REG_FIELD(XGMI0_PCS_GOPX16_PCS_ERROR_STATUS, RecoveryTimeoutErr)},
	{"XGMI PCS ReadySerialTimeoutErr",
	 SOC15_REG_FIELD(XGMI0_PCS_GOPX16_PCS_ERROR_STATUS, ReadySerialTimeoutErr)},
	{"XGMI PCS ReadySerialAttemptErr",
	 SOC15_REG_FIELD(XGMI0_PCS_GOPX16_PCS_ERROR_STATUS, ReadySerialAttemptErr)},
	{"XGMI PCS RecoveryAttemptErr",
	 SOC15_REG_FIELD(XGMI0_PCS_GOPX16_PCS_ERROR_STATUS, RecoveryAttemptErr)},
	{"XGMI PCS RecoveryRelockAttemptErr",
	 SOC15_REG_FIELD(XGMI0_PCS_GOPX16_PCS_ERROR_STATUS, RecoveryRelockAttemptErr)},
};

static const struct amdgpu_pcs_ras_field wafl_pcs_ras_fields[] = {
	{"WAFL PCS DataLossErr",
	 SOC15_REG_FIELD(PCS_GOPX1_0_PCS_GOPX1_PCS_ERROR_STATUS, DataLossErr)},
	{"WAFL PCS TrainingErr",
	 SOC15_REG_FIELD(PCS_GOPX1_0_PCS_GOPX1_PCS_ERROR_STATUS, TrainingErr)},
	{"WAFL PCS CRCErr",
	 SOC15_REG_FIELD(PCS_GOPX1_0_PCS_GOPX1_PCS_ERROR_STATUS, CRCErr)},
	{"WAFL PCS BERExceededErr",
	 SOC15_REG_FIELD(PCS_GOPX1_0_PCS_GOPX1_PCS_ERROR_STATUS, BERExceededErr)},
	{"WAFL PCS TxMetaDataErr",
	 SOC15_REG_FIELD(PCS_GOPX1_0_PCS_GOPX1_PCS_ERROR_STATUS, TxMetaDataErr)},
	{"WAFL PCS ReplayBufParityErr",
	 SOC15_REG_FIELD(PCS_GOPX1_0_PCS_GOPX1_PCS_ERROR_STATUS, ReplayBufParityErr)},
	{"WAFL PCS DataParityErr",
	 SOC15_REG_FIELD(PCS_GOPX1_0_PCS_GOPX1_PCS_ERROR_STATUS, DataParityErr)},
	{"WAFL PCS ReplayFifoOverflowErr",
	 SOC15_REG_FIELD(PCS_GOPX1_0_PCS_GOPX1_PCS_ERROR_STATUS, ReplayFifoOverflowErr)},
	{"WAFL PCS ReplayFifoUnderflowErr",
	 SOC15_REG_FIELD(PCS_GOPX1_0_PCS_GOPX1_PCS_ERROR_STATUS, ReplayFifoUnderflowErr)},
	{"WAFL PCS ElasticFifoOverflowErr",
	 SOC15_REG_FIELD(PCS_GOPX1_0_PCS_GOPX1_PCS_ERROR_STATUS, ElasticFifoOverflowErr)},
	{"WAFL PCS DeskewErr",
	 SOC15_REG_FIELD(PCS_GOPX1_0_PCS_GOPX1_PCS_ERROR_STATUS, DeskewErr)},
	{"WAFL PCS DataStartupLimitErr",
	 SOC15_REG_FIELD(PCS_GOPX1_0_PCS_GOPX1_PCS_ERROR_STATUS, DataStartupLimitErr)},
	{"WAFL PCS FCInitTimeoutErr",
	 SOC15_REG_FIELD(PCS_GOPX1_0_PCS_GOPX1_PCS_ERROR_STATUS, FCInitTimeoutErr)},
	{"WAFL PCS RecoveryTimeoutErr",
	 SOC15_REG_FIELD(PCS_GOPX1_0_PCS_GOPX1_PCS_ERROR_STATUS, RecoveryTimeoutErr)},
	{"WAFL PCS ReadySerialTimeoutErr",
	 SOC15_REG_FIELD(PCS_GOPX1_0_PCS_GOPX1_PCS_ERROR_STATUS, ReadySerialTimeoutErr)},
	{"WAFL PCS ReadySerialAttemptErr",
	 SOC15_REG_FIELD(PCS_GOPX1_0_PCS_GOPX1_PCS_ERROR_STATUS, ReadySerialAttemptErr)},
	{"WAFL PCS RecoveryAttemptErr",
	 SOC15_REG_FIELD(PCS_GOPX1_0_PCS_GOPX1_PCS_ERROR_STATUS, RecoveryAttemptErr)},
	{"WAFL PCS RecoveryRelockAttemptErr",
	 SOC15_REG_FIELD(PCS_GOPX1_0_PCS_GOPX1_PCS_ERROR_STATUS, RecoveryRelockAttemptErr)},
};

static const struct amdgpu_pcs_ras_field xgmi3x16_pcs_ras_fields[] = {
	{"XGMI3X16 PCS DataLossErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, DataLossErr)},
	{"XGMI3X16 PCS TrainingErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, TrainingErr)},
	{"XGMI3X16 PCS FlowCtrlAckErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, FlowCtrlAckErr)},
	{"XGMI3X16 PCS RxFifoUnderflowErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, RxFifoUnderflowErr)},
	{"XGMI3X16 PCS RxFifoOverflowErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, RxFifoOverflowErr)},
	{"XGMI3X16 PCS CRCErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, CRCErr)},
	{"XGMI3X16 PCS BERExceededErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, BERExceededErr)},
	{"XGMI3X16 PCS TxVcidDataErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, TxVcidDataErr)},
	{"XGMI3X16 PCS ReplayBufParityErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, ReplayBufParityErr)},
	{"XGMI3X16 PCS DataParityErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, DataParityErr)},
	{"XGMI3X16 PCS ReplayFifoOverflowErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, ReplayFifoOverflowErr)},
	{"XGMI3X16 PCS ReplayFifoUnderflowErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, ReplayFifoUnderflowErr)},
	{"XGMI3X16 PCS ElasticFifoOverflowErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, ElasticFifoOverflowErr)},
	{"XGMI3X16 PCS DeskewErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, DeskewErr)},
	{"XGMI3X16 PCS FlowCtrlCRCErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, FlowCtrlCRCErr)},
	{"XGMI3X16 PCS DataStartupLimitErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, DataStartupLimitErr)},
	{"XGMI3X16 PCS FCInitTimeoutErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, FCInitTimeoutErr)},
	{"XGMI3X16 PCS RecoveryTimeoutErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, RecoveryTimeoutErr)},
	{"XGMI3X16 PCS ReadySerialTimeoutErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, ReadySerialTimeoutErr)},
	{"XGMI3X16 PCS ReadySerialAttemptErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, ReadySerialAttemptErr)},
	{"XGMI3X16 PCS RecoveryAttemptErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, RecoveryAttemptErr)},
	{"XGMI3X16 PCS RecoveryRelockAttemptErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, RecoveryRelockAttemptErr)},
	{"XGMI3X16 PCS ReplayAttemptErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, ReplayAttemptErr)},
	{"XGMI3X16 PCS SyncHdrErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, SyncHdrErr)},
	{"XGMI3X16 PCS TxReplayTimeoutErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, TxReplayTimeoutErr)},
	{"XGMI3X16 PCS RxReplayTimeoutErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, RxReplayTimeoutErr)},
	{"XGMI3X16 PCS LinkSubTxTimeoutErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, LinkSubTxTimeoutErr)},
	{"XGMI3X16 PCS LinkSubRxTimeoutErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, LinkSubRxTimeoutErr)},
	{"XGMI3X16 PCS RxCMDPktErr",
	 SOC15_REG_FIELD(PCS_XGMI3X16_PCS_ERROR_STATUS, RxCMDPktErr)},
};

static u32 xgmi_v6_4_get_link_status(struct amdgpu_device *adev, int global_link_num)
{
	const u32 smnpcs_xgmi3x16_pcs_state_hist1 = 0x11a00070;
	const int xgmi_inst = 2;
	u32 link_inst;
	u64 addr;

	link_inst = global_link_num % xgmi_inst;

	addr = (smnpcs_xgmi3x16_pcs_state_hist1 | (link_inst << 20)) +
		adev->asic_funcs->encode_ext_smn_addressing(global_link_num / xgmi_inst);

	return RREG32_PCIE_EXT(addr);
}

int amdgpu_get_xgmi_link_status(struct amdgpu_device *adev, int global_link_num)
{
	u32 xgmi_state_reg_val;

	switch (amdgpu_ip_version(adev, XGMI_HWIP, 0)) {
	case IP_VERSION(6, 4, 0):
	case IP_VERSION(6, 4, 1):
		xgmi_state_reg_val = xgmi_v6_4_get_link_status(adev, global_link_num);
		break;
	default:
		return -EOPNOTSUPP;
	}

	if ((xgmi_state_reg_val & 0xFF) == XGMI_STATE_DISABLE)
		return -ENOLINK;

	if ((xgmi_state_reg_val & 0xFF) == XGMI_STATE_LS0)
		return XGMI_LINK_ACTIVE;

	return XGMI_LINK_INACTIVE;
}

/**
 * DOC: AMDGPU XGMI Support
 *
 * XGMI is a high speed interconnect that joins multiple GPU cards
 * into a homogeneous memory space that is organized by a collective
 * hive ID and individual node IDs, both of which are 64-bit numbers.
 *
 * The file xgmi_device_id contains the unique per GPU device ID and
 * is stored in the /sys/class/drm/card${cardno}/device/ directory.
 *
 * Inside the device directory a sub-directory 'xgmi_hive_info' is
 * created which contains the hive ID and the list of nodes.
 *
 * The hive ID is stored in:
 *   /sys/class/drm/card${cardno}/device/xgmi_hive_info/xgmi_hive_id
 *
 * The node information is stored in numbered directories:
 *   /sys/class/drm/card${cardno}/device/xgmi_hive_info/node${nodeno}/xgmi_device_id
 *
 * Each device has their own xgmi_hive_info direction with a mirror
 * set of node sub-directories.
 *
 * The XGMI memory space is built by contiguously adding the power of
 * two padded VRAM space from each node to each other.
 *
 */

static struct attribute amdgpu_xgmi_hive_id = {
	.name = "xgmi_hive_id",
	.mode = S_IRUGO
};

static struct attribute *amdgpu_xgmi_hive_attrs[] = {
	&amdgpu_xgmi_hive_id,
	NULL
};
ATTRIBUTE_GROUPS(amdgpu_xgmi_hive);

static ssize_t amdgpu_xgmi_show_attrs(struct kobject *kobj,
	struct attribute *attr, char *buf)
{
	struct amdgpu_hive_info *hive = container_of(
		kobj, struct amdgpu_hive_info, kobj);

	if (attr == &amdgpu_xgmi_hive_id)
		return snprintf(buf, PAGE_SIZE, "%llu\n", hive->hive_id);

	return 0;
}

static void amdgpu_xgmi_hive_release(struct kobject *kobj)
{
	struct amdgpu_hive_info *hive = container_of(
		kobj, struct amdgpu_hive_info, kobj);

	amdgpu_reset_put_reset_domain(hive->reset_domain);
	hive->reset_domain = NULL;

	mutex_destroy(&hive->hive_lock);
	kfree(hive);
}

static const struct sysfs_ops amdgpu_xgmi_hive_ops = {
	.show = amdgpu_xgmi_show_attrs,
};

static const struct kobj_type amdgpu_xgmi_hive_type = {
	.release = amdgpu_xgmi_hive_release,
	.sysfs_ops = &amdgpu_xgmi_hive_ops,
	.default_groups = amdgpu_xgmi_hive_groups,
};

static ssize_t amdgpu_xgmi_show_device_id(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);

	return sysfs_emit(buf, "%llu\n", adev->gmc.xgmi.node_id);

}

static ssize_t amdgpu_xgmi_show_physical_id(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);

	return sysfs_emit(buf, "%u\n", adev->gmc.xgmi.physical_node_id);

}

static ssize_t amdgpu_xgmi_show_num_hops(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	struct psp_xgmi_topology_info *top = &adev->psp.xgmi_context.top_info;
	int i;

	for (i = 0; i < top->num_nodes; i++)
		sprintf(buf + 3 * i, "%02x ", top->nodes[i].num_hops);

	return sysfs_emit(buf, "%s\n", buf);
}

static ssize_t amdgpu_xgmi_show_num_links(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	struct psp_xgmi_topology_info *top = &adev->psp.xgmi_context.top_info;
	int i;

	for (i = 0; i < top->num_nodes; i++)
		sprintf(buf + 3 * i, "%02x ", top->nodes[i].num_links);

	return sysfs_emit(buf, "%s\n", buf);
}

static ssize_t amdgpu_xgmi_show_connected_port_num(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	struct psp_xgmi_topology_info *top = &adev->psp.xgmi_context.top_info;
	int i, j, size = 0;
	int current_node;
	/*
	 * get the node id in the sysfs for the current socket and show
	 * it in the port num info output in the sysfs for easy reading.
	 * it is NOT the one retrieved from xgmi ta.
	 */
	for (i = 0; i < top->num_nodes; i++) {
		if (top->nodes[i].node_id == adev->gmc.xgmi.node_id) {
			current_node = i;
			break;
		}
	}

	if (i == top->num_nodes)
		return -EINVAL;

	for (i = 0; i < top->num_nodes; i++) {
		for (j = 0; j < top->nodes[i].num_links; j++)
			/* node id in sysfs starts from 1 rather than 0 so +1 here */
			size += sysfs_emit_at(buf, size, "%02x:%02x ->  %02x:%02x\n", current_node + 1,
					      top->nodes[i].port_num[j].src_xgmi_port_num, i + 1,
					      top->nodes[i].port_num[j].dst_xgmi_port_num);
	}

	return size;
}

#define AMDGPU_XGMI_SET_FICAA(o)	((o) | 0x456801)
static ssize_t amdgpu_xgmi_show_error(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	uint32_t ficaa_pie_ctl_in, ficaa_pie_status_in;
	uint64_t fica_out;
	unsigned int error_count = 0;

	ficaa_pie_ctl_in = AMDGPU_XGMI_SET_FICAA(0x200);
	ficaa_pie_status_in = AMDGPU_XGMI_SET_FICAA(0x208);

	if ((!adev->df.funcs) ||
	    (!adev->df.funcs->get_fica) ||
	    (!adev->df.funcs->set_fica))
		return -EINVAL;

	fica_out = adev->df.funcs->get_fica(adev, ficaa_pie_ctl_in);
	if (fica_out != 0x1f)
		pr_err("xGMI error counters not enabled!\n");

	fica_out = adev->df.funcs->get_fica(adev, ficaa_pie_status_in);

	if ((fica_out & 0xffff) == 2)
		error_count = ((fica_out >> 62) & 0x1) + (fica_out >> 63);

	adev->df.funcs->set_fica(adev, ficaa_pie_status_in, 0, 0);

	return sysfs_emit(buf, "%u\n", error_count);
}


static DEVICE_ATTR(xgmi_device_id, S_IRUGO, amdgpu_xgmi_show_device_id, NULL);
static DEVICE_ATTR(xgmi_physical_id, 0444, amdgpu_xgmi_show_physical_id, NULL);
static DEVICE_ATTR(xgmi_error, S_IRUGO, amdgpu_xgmi_show_error, NULL);
static DEVICE_ATTR(xgmi_num_hops, S_IRUGO, amdgpu_xgmi_show_num_hops, NULL);
static DEVICE_ATTR(xgmi_num_links, S_IRUGO, amdgpu_xgmi_show_num_links, NULL);
static DEVICE_ATTR(xgmi_port_num, S_IRUGO, amdgpu_xgmi_show_connected_port_num, NULL);

static int amdgpu_xgmi_sysfs_add_dev_info(struct amdgpu_device *adev,
					 struct amdgpu_hive_info *hive)
{
	int ret = 0;
	char node[10] = { 0 };

	/* Create xgmi device id file */
	ret = device_create_file(adev->dev, &dev_attr_xgmi_device_id);
	if (ret) {
		dev_err(adev->dev, "XGMI: Failed to create device file xgmi_device_id\n");
		return ret;
	}

	ret = device_create_file(adev->dev, &dev_attr_xgmi_physical_id);
	if (ret) {
		dev_err(adev->dev, "XGMI: Failed to create device file xgmi_physical_id\n");
		return ret;
	}

	/* Create xgmi error file */
	ret = device_create_file(adev->dev, &dev_attr_xgmi_error);
	if (ret)
		pr_err("failed to create xgmi_error\n");

	/* Create xgmi num hops file */
	ret = device_create_file(adev->dev, &dev_attr_xgmi_num_hops);
	if (ret)
		pr_err("failed to create xgmi_num_hops\n");

	/* Create xgmi num links file */
	ret = device_create_file(adev->dev, &dev_attr_xgmi_num_links);
	if (ret)
		pr_err("failed to create xgmi_num_links\n");

	/* Create xgmi port num file if supported */
	if (adev->psp.xgmi_context.xgmi_ta_caps & EXTEND_PEER_LINK_INFO_CMD_FLAG) {
		ret = device_create_file(adev->dev, &dev_attr_xgmi_port_num);
		if (ret)
			dev_err(adev->dev, "failed to create xgmi_port_num\n");
	}

	/* Create sysfs link to hive info folder on the first device */
	if (hive->kobj.parent != (&adev->dev->kobj)) {
		ret = sysfs_create_link(&adev->dev->kobj, &hive->kobj,
					"xgmi_hive_info");
		if (ret) {
			dev_err(adev->dev, "XGMI: Failed to create link to hive info");
			goto remove_file;
		}
	}

	sprintf(node, "node%d", atomic_read(&hive->number_devices));
	/* Create sysfs link form the hive folder to yourself */
	ret = sysfs_create_link(&hive->kobj, &adev->dev->kobj, node);
	if (ret) {
		dev_err(adev->dev, "XGMI: Failed to create link from hive info");
		goto remove_link;
	}

	goto success;


remove_link:
	sysfs_remove_link(&adev->dev->kobj, adev_to_drm(adev)->unique);

remove_file:
	device_remove_file(adev->dev, &dev_attr_xgmi_device_id);
	device_remove_file(adev->dev, &dev_attr_xgmi_physical_id);
	device_remove_file(adev->dev, &dev_attr_xgmi_error);
	device_remove_file(adev->dev, &dev_attr_xgmi_num_hops);
	device_remove_file(adev->dev, &dev_attr_xgmi_num_links);
	if (adev->psp.xgmi_context.xgmi_ta_caps & EXTEND_PEER_LINK_INFO_CMD_FLAG)
		device_remove_file(adev->dev, &dev_attr_xgmi_port_num);

success:
	return ret;
}

static void amdgpu_xgmi_sysfs_rem_dev_info(struct amdgpu_device *adev,
					  struct amdgpu_hive_info *hive)
{
	char node[10];
	memset(node, 0, sizeof(node));

	device_remove_file(adev->dev, &dev_attr_xgmi_device_id);
	device_remove_file(adev->dev, &dev_attr_xgmi_physical_id);
	device_remove_file(adev->dev, &dev_attr_xgmi_error);
	device_remove_file(adev->dev, &dev_attr_xgmi_num_hops);
	device_remove_file(adev->dev, &dev_attr_xgmi_num_links);
	if (adev->psp.xgmi_context.xgmi_ta_caps & EXTEND_PEER_LINK_INFO_CMD_FLAG)
		device_remove_file(adev->dev, &dev_attr_xgmi_port_num);

	if (hive->kobj.parent != (&adev->dev->kobj))
		sysfs_remove_link(&adev->dev->kobj,"xgmi_hive_info");

	sprintf(node, "node%d", atomic_read(&hive->number_devices));
	sysfs_remove_link(&hive->kobj, node);

}



struct amdgpu_hive_info *amdgpu_get_xgmi_hive(struct amdgpu_device *adev)
{
	struct amdgpu_hive_info *hive = NULL;
	int ret;

	if (!adev->gmc.xgmi.hive_id)
		return NULL;

	if (adev->hive) {
		kobject_get(&adev->hive->kobj);
		return adev->hive;
	}

	mutex_lock(&xgmi_mutex);

	list_for_each_entry(hive, &xgmi_hive_list, node)  {
		if (hive->hive_id == adev->gmc.xgmi.hive_id)
			goto pro_end;
	}

	hive = kzalloc(sizeof(*hive), GFP_KERNEL);
	if (!hive) {
		dev_err(adev->dev, "XGMI: allocation failed\n");
		ret = -ENOMEM;
		hive = NULL;
		goto pro_end;
	}

	/* initialize new hive if not exist */
	ret = kobject_init_and_add(&hive->kobj,
			&amdgpu_xgmi_hive_type,
			&adev->dev->kobj,
			"%s", "xgmi_hive_info");
	if (ret) {
		dev_err(adev->dev, "XGMI: failed initializing kobject for xgmi hive\n");
		kobject_put(&hive->kobj);
		hive = NULL;
		goto pro_end;
	}

	/**
	 * Only init hive->reset_domain for none SRIOV configuration. For SRIOV,
	 * Host driver decide how to reset the GPU either through FLR or chain reset.
	 * Guest side will get individual notifications from the host for the FLR
	 * if necessary.
	 */
	if (!amdgpu_sriov_vf(adev)) {
	/**
	 * Avoid recreating reset domain when hive is reconstructed for the case
	 * of reset the devices in the XGMI hive during probe for passthrough GPU
	 * See https://www.spinics.net/lists/amd-gfx/msg58836.html
	 */
		if (adev->reset_domain->type != XGMI_HIVE) {
			hive->reset_domain =
				amdgpu_reset_create_reset_domain(XGMI_HIVE, "amdgpu-reset-hive");
			if (!hive->reset_domain) {
				dev_err(adev->dev, "XGMI: failed initializing reset domain for xgmi hive\n");
				ret = -ENOMEM;
				kobject_put(&hive->kobj);
				hive = NULL;
				goto pro_end;
			}
		} else {
			amdgpu_reset_get_reset_domain(adev->reset_domain);
			hive->reset_domain = adev->reset_domain;
		}
	}

	hive->hive_id = adev->gmc.xgmi.hive_id;
	INIT_LIST_HEAD(&hive->device_list);
	INIT_LIST_HEAD(&hive->node);
	mutex_init(&hive->hive_lock);
	atomic_set(&hive->number_devices, 0);
	task_barrier_init(&hive->tb);
	hive->pstate = AMDGPU_XGMI_PSTATE_UNKNOWN;
	hive->hi_req_gpu = NULL;
	atomic_set(&hive->requested_nps_mode, UNKNOWN_MEMORY_PARTITION_MODE);

	/*
	 * hive pstate on boot is high in vega20 so we have to go to low
	 * pstate on after boot.
	 */
	hive->hi_req_count = AMDGPU_MAX_XGMI_DEVICE_PER_HIVE;
	list_add_tail(&hive->node, &xgmi_hive_list);

pro_end:
	if (hive)
		kobject_get(&hive->kobj);
	mutex_unlock(&xgmi_mutex);
	return hive;
}

void amdgpu_put_xgmi_hive(struct amdgpu_hive_info *hive)
{
	if (hive)
		kobject_put(&hive->kobj);
}

int amdgpu_xgmi_set_pstate(struct amdgpu_device *adev, int pstate)
{
	int ret = 0;
	struct amdgpu_hive_info *hive;
	struct amdgpu_device *request_adev;
	bool is_hi_req = pstate == AMDGPU_XGMI_PSTATE_MAX_VEGA20;
	bool init_low;

	hive = amdgpu_get_xgmi_hive(adev);
	if (!hive)
		return 0;

	request_adev = hive->hi_req_gpu ? hive->hi_req_gpu : adev;
	init_low = hive->pstate == AMDGPU_XGMI_PSTATE_UNKNOWN;
	amdgpu_put_xgmi_hive(hive);
	/* fw bug so temporarily disable pstate switching */
	return 0;

	if (!hive || adev->asic_type != CHIP_VEGA20)
		return 0;

	mutex_lock(&hive->hive_lock);

	if (is_hi_req)
		hive->hi_req_count++;
	else
		hive->hi_req_count--;

	/*
	 * Vega20 only needs single peer to request pstate high for the hive to
	 * go high but all peers must request pstate low for the hive to go low
	 */
	if (hive->pstate == pstate ||
			(!is_hi_req && hive->hi_req_count && !init_low))
		goto out;

	dev_dbg(request_adev->dev, "Set xgmi pstate %d.\n", pstate);

	ret = amdgpu_dpm_set_xgmi_pstate(request_adev, pstate);
	if (ret) {
		dev_err(request_adev->dev,
			"XGMI: Set pstate failure on device %llx, hive %llx, ret %d",
			request_adev->gmc.xgmi.node_id,
			request_adev->gmc.xgmi.hive_id, ret);
		goto out;
	}

	if (init_low)
		hive->pstate = hive->hi_req_count ?
					hive->pstate : AMDGPU_XGMI_PSTATE_MIN;
	else {
		hive->pstate = pstate;
		hive->hi_req_gpu = pstate != AMDGPU_XGMI_PSTATE_MIN ?
							adev : NULL;
	}
out:
	mutex_unlock(&hive->hive_lock);
	return ret;
}

int amdgpu_xgmi_update_topology(struct amdgpu_hive_info *hive, struct amdgpu_device *adev)
{
	int ret;

	if (amdgpu_sriov_vf(adev))
		return 0;

	/* Each psp need to set the latest topology */
	ret = psp_xgmi_set_topology_info(&adev->psp,
					 atomic_read(&hive->number_devices),
					 &adev->psp.xgmi_context.top_info);
	if (ret)
		dev_err(adev->dev,
			"XGMI: Set topology failure on device %llx, hive %llx, ret %d",
			adev->gmc.xgmi.node_id,
			adev->gmc.xgmi.hive_id, ret);

	return ret;
}


/*
 * NOTE psp_xgmi_node_info.num_hops layout is as follows:
 * num_hops[7:6] = link type (0 = xGMI2, 1 = xGMI3, 2/3 = reserved)
 * num_hops[5:3] = reserved
 * num_hops[2:0] = number of hops
 */
int amdgpu_xgmi_get_hops_count(struct amdgpu_device *adev,
			       struct amdgpu_device *peer_adev)
{
	struct psp_xgmi_topology_info *top = &adev->psp.xgmi_context.top_info;
	uint8_t num_hops_mask = 0x7;
	int i;

	if (!adev->gmc.xgmi.supported)
		return 0;

	for (i = 0 ; i < top->num_nodes; ++i)
		if (top->nodes[i].node_id == peer_adev->gmc.xgmi.node_id)
			return top->nodes[i].num_hops & num_hops_mask;

	dev_err(adev->dev, "Failed to get xgmi hops count for peer %d.\n",
		peer_adev->gmc.xgmi.physical_node_id);

	return 0;
}

int amdgpu_xgmi_get_bandwidth(struct amdgpu_device *adev, struct amdgpu_device *peer_adev,
			      enum amdgpu_xgmi_bw_mode bw_mode, enum amdgpu_xgmi_bw_unit bw_unit,
			      uint32_t *min_bw, uint32_t *max_bw)
{
	bool peer_mode = bw_mode == AMDGPU_XGMI_BW_MODE_PER_PEER;
	int unit_scale = bw_unit == AMDGPU_XGMI_BW_UNIT_MBYTES ? 1000 : 1;
	int num_lanes = adev->gmc.xgmi.max_width;
	int speed = adev->gmc.xgmi.max_speed;
	int num_links = !peer_mode ? 1 : -1;

	if (!(min_bw && max_bw))
		return -EINVAL;

	*min_bw = 0;
	*max_bw = 0;

	if (!adev->gmc.xgmi.supported)
		return -ENODATA;

	if (peer_mode && !peer_adev)
		return -EINVAL;

	if (peer_mode) {
		struct psp_xgmi_topology_info *top = &adev->psp.xgmi_context.top_info;
		int i;

		for (i = 0 ; i < top->num_nodes; ++i) {
			if (top->nodes[i].node_id != peer_adev->gmc.xgmi.node_id)
				continue;

			num_links =  top->nodes[i].num_links;
			break;
		}
	}

	if (num_links == -1) {
		dev_err(adev->dev, "Failed to get number of xgmi links for peer %d.\n",
			peer_adev->gmc.xgmi.physical_node_id);
	} else if (num_links) {
		int per_link_bw = (speed * num_lanes * unit_scale)/BITS_PER_BYTE;

		*min_bw = per_link_bw;
		*max_bw = num_links * per_link_bw;
	}

	return 0;
}

bool amdgpu_xgmi_get_is_sharing_enabled(struct amdgpu_device *adev,
					struct amdgpu_device *peer_adev)
{
	struct psp_xgmi_topology_info *top = &adev->psp.xgmi_context.top_info;
	int i;

	/* Sharing should always be enabled for non-SRIOV. */
	if (!amdgpu_sriov_vf(adev))
		return true;

	for (i = 0 ; i < top->num_nodes; ++i)
		if (top->nodes[i].node_id == peer_adev->gmc.xgmi.node_id)
			return !!top->nodes[i].is_sharing_enabled;

	return false;
}

/*
 * Devices that support extended data require the entire hive to initialize with
 * the shared memory buffer flag set.
 *
 * Hive locks and conditions apply - see amdgpu_xgmi_add_device
 */
static int amdgpu_xgmi_initialize_hive_get_data_partition(struct amdgpu_hive_info *hive,
							bool set_extended_data)
{
	struct amdgpu_device *tmp_adev;
	int ret;

	list_for_each_entry(tmp_adev, &hive->device_list, gmc.xgmi.head) {
		ret = psp_xgmi_initialize(&tmp_adev->psp, set_extended_data, false);
		if (ret) {
			dev_err(tmp_adev->dev,
				"XGMI: Failed to initialize xgmi session for data partition %i\n",
				set_extended_data);
			return ret;
		}

	}

	return 0;
}

static void amdgpu_xgmi_fill_topology_info(struct amdgpu_device *adev,
	struct amdgpu_device *peer_adev)
{
	struct psp_xgmi_topology_info *top_info = &adev->psp.xgmi_context.top_info;
	struct psp_xgmi_topology_info *peer_info = &peer_adev->psp.xgmi_context.top_info;

	for (int i = 0; i < peer_info->num_nodes; i++) {
		if (peer_info->nodes[i].node_id == adev->gmc.xgmi.node_id) {
			for (int j = 0; j < top_info->num_nodes; j++) {
				if (top_info->nodes[j].node_id == peer_adev->gmc.xgmi.node_id) {
					peer_info->nodes[i].num_hops = top_info->nodes[j].num_hops;
					peer_info->nodes[i].is_sharing_enabled =
							top_info->nodes[j].is_sharing_enabled;
					peer_info->nodes[i].num_links =
							top_info->nodes[j].num_links;
					return;
				}
			}
		}
	}
}

int amdgpu_xgmi_add_device(struct amdgpu_device *adev)
{
	struct psp_xgmi_topology_info *top_info;
	struct amdgpu_hive_info *hive;
	struct amdgpu_xgmi	*entry;
	struct amdgpu_device *tmp_adev = NULL;

	int count = 0, ret = 0;

	if (!adev->gmc.xgmi.supported)
		return 0;

	if (amdgpu_device_ip_get_ip_block(adev, AMD_IP_BLOCK_TYPE_PSP)) {
		ret = psp_xgmi_initialize(&adev->psp, false, true);
		if (ret) {
			dev_err(adev->dev,
				"XGMI: Failed to initialize xgmi session\n");
			return ret;
		}

		ret = psp_xgmi_get_hive_id(&adev->psp, &adev->gmc.xgmi.hive_id);
		if (ret) {
			dev_err(adev->dev,
				"XGMI: Failed to get hive id\n");
			return ret;
		}

		ret = psp_xgmi_get_node_id(&adev->psp, &adev->gmc.xgmi.node_id);
		if (ret) {
			dev_err(adev->dev,
				"XGMI: Failed to get node id\n");
			return ret;
		}
	} else {
		adev->gmc.xgmi.hive_id = 16;
		adev->gmc.xgmi.node_id = adev->gmc.xgmi.physical_node_id + 16;
	}

	hive = amdgpu_get_xgmi_hive(adev);
	if (!hive) {
		ret = -EINVAL;
		dev_err(adev->dev,
			"XGMI: node 0x%llx, can not match hive 0x%llx in the hive list.\n",
			adev->gmc.xgmi.node_id, adev->gmc.xgmi.hive_id);
		goto exit;
	}
	mutex_lock(&hive->hive_lock);

	top_info = &adev->psp.xgmi_context.top_info;

	list_add_tail(&adev->gmc.xgmi.head, &hive->device_list);
	list_for_each_entry(entry, &hive->device_list, head)
		top_info->nodes[count++].node_id = entry->node_id;
	top_info->num_nodes = count;
	atomic_set(&hive->number_devices, count);

	task_barrier_add_task(&hive->tb);

	if (amdgpu_device_ip_get_ip_block(adev, AMD_IP_BLOCK_TYPE_PSP)) {
		list_for_each_entry(tmp_adev, &hive->device_list, gmc.xgmi.head) {
			/* update node list for other device in the hive */
			if (tmp_adev != adev) {
				top_info = &tmp_adev->psp.xgmi_context.top_info;
				top_info->nodes[count - 1].node_id =
					adev->gmc.xgmi.node_id;
				top_info->num_nodes = count;
			}
			ret = amdgpu_xgmi_update_topology(hive, tmp_adev);
			if (ret)
				goto exit_unlock;
		}

		if (amdgpu_sriov_vf(adev) &&
			adev->psp.xgmi_context.xgmi_ta_caps & EXTEND_PEER_LINK_INFO_CMD_FLAG) {
			/* only get topology for VF being init if it can support full duplex */
			ret = psp_xgmi_get_topology_info(&adev->psp, count,
						&adev->psp.xgmi_context.top_info, false);
			if (ret) {
				dev_err(adev->dev,
					"XGMI: Get topology failure on device %llx, hive %llx, ret %d",
					adev->gmc.xgmi.node_id,
					adev->gmc.xgmi.hive_id, ret);
				/* To do: continue with some node failed or disable the whole hive*/
				goto exit_unlock;
			}

			/* fill the topology info for peers instead of getting from PSP */
			list_for_each_entry(tmp_adev, &hive->device_list, gmc.xgmi.head) {
				amdgpu_xgmi_fill_topology_info(adev, tmp_adev);
			}
		} else {
			/* get latest topology info for each device from psp */
			list_for_each_entry(tmp_adev, &hive->device_list, gmc.xgmi.head) {
				ret = psp_xgmi_get_topology_info(&tmp_adev->psp, count,
					&tmp_adev->psp.xgmi_context.top_info, false);
				if (ret) {
					dev_err(tmp_adev->dev,
						"XGMI: Get topology failure on device %llx, hive %llx, ret %d",
						tmp_adev->gmc.xgmi.node_id,
						tmp_adev->gmc.xgmi.hive_id, ret);
					/* To do : continue with some node failed or disable the whole hive */
					goto exit_unlock;
				}
			}
		}

		/* get topology again for hives that support extended data */
		if (adev->psp.xgmi_context.supports_extended_data) {

			/* initialize the hive to get extended data.  */
			ret = amdgpu_xgmi_initialize_hive_get_data_partition(hive, true);
			if (ret)
				goto exit_unlock;

			/* get the extended data. */
			list_for_each_entry(tmp_adev, &hive->device_list, gmc.xgmi.head) {
				ret = psp_xgmi_get_topology_info(&tmp_adev->psp, count,
						&tmp_adev->psp.xgmi_context.top_info, true);
				if (ret) {
					dev_err(tmp_adev->dev,
						"XGMI: Get topology for extended data failure on device %llx, hive %llx, ret %d",
						tmp_adev->gmc.xgmi.node_id,
						tmp_adev->gmc.xgmi.hive_id, ret);
					goto exit_unlock;
				}
			}

			/* initialize the hive to get non-extended data for the next round. */
			ret = amdgpu_xgmi_initialize_hive_get_data_partition(hive, false);
			if (ret)
				goto exit_unlock;

		}
	}

	if (!ret)
		ret = amdgpu_xgmi_sysfs_add_dev_info(adev, hive);

exit_unlock:
	mutex_unlock(&hive->hive_lock);
exit:
	if (!ret) {
		adev->hive = hive;
		dev_info(adev->dev, "XGMI: Add node %d, hive 0x%llx.\n",
			 adev->gmc.xgmi.physical_node_id, adev->gmc.xgmi.hive_id);
	} else {
		amdgpu_put_xgmi_hive(hive);
		dev_err(adev->dev, "XGMI: Failed to add node %d, hive 0x%llx ret: %d\n",
			adev->gmc.xgmi.physical_node_id, adev->gmc.xgmi.hive_id,
			ret);
	}

	return ret;
}

int amdgpu_xgmi_remove_device(struct amdgpu_device *adev)
{
	struct amdgpu_hive_info *hive = adev->hive;

	if (!adev->gmc.xgmi.supported)
		return -EINVAL;

	if (!hive)
		return -EINVAL;

	mutex_lock(&hive->hive_lock);
	task_barrier_rem_task(&hive->tb);
	amdgpu_xgmi_sysfs_rem_dev_info(adev, hive);
	if (hive->hi_req_gpu == adev)
		hive->hi_req_gpu = NULL;
	list_del(&adev->gmc.xgmi.head);
	mutex_unlock(&hive->hive_lock);

	amdgpu_put_xgmi_hive(hive);
	adev->hive = NULL;

	if (atomic_dec_return(&hive->number_devices) == 0) {
		/* Remove the hive from global hive list */
		mutex_lock(&xgmi_mutex);
		list_del(&hive->node);
		mutex_unlock(&xgmi_mutex);

		amdgpu_put_xgmi_hive(hive);
	}

	return 0;
}

static int xgmi_v6_4_0_aca_bank_parser(struct aca_handle *handle, struct aca_bank *bank,
				       enum aca_smu_type type, void *data)
{
	struct amdgpu_device *adev = handle->adev;
	struct aca_bank_info info;
	const char *error_str;
	u64 status, count;
	int ret, ext_error_code;

	ret = aca_bank_info_decode(bank, &info);
	if (ret)
		return ret;

	status = bank->regs[ACA_REG_IDX_STATUS];
	ext_error_code = ACA_REG__STATUS__ERRORCODEEXT(status);

	error_str = ext_error_code < ARRAY_SIZE(xgmi_v6_4_0_ras_error_code_ext) ?
		xgmi_v6_4_0_ras_error_code_ext[ext_error_code] : NULL;
	if (error_str)
		dev_info(adev->dev, "%s detected\n", error_str);

	count = ACA_REG__MISC0__ERRCNT(bank->regs[ACA_REG_IDX_MISC0]);

	switch (type) {
	case ACA_SMU_TYPE_UE:
		if (ext_error_code != 0 && ext_error_code != 9)
			count = 0ULL;

		bank->aca_err_type = ACA_ERROR_TYPE_UE;
		ret = aca_error_cache_log_bank_error(handle, &info, ACA_ERROR_TYPE_UE, count);
		break;
	case ACA_SMU_TYPE_CE:
		count = ext_error_code == 6 ? count : 0ULL;
		bank->aca_err_type = ACA_ERROR_TYPE_CE;
		ret = aca_error_cache_log_bank_error(handle, &info, bank->aca_err_type, count);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static const struct aca_bank_ops xgmi_v6_4_0_aca_bank_ops = {
	.aca_bank_parser = xgmi_v6_4_0_aca_bank_parser,
};

static const struct aca_info xgmi_v6_4_0_aca_info = {
	.hwip = ACA_HWIP_TYPE_PCS_XGMI,
	.mask = ACA_ERROR_UE_MASK | ACA_ERROR_CE_MASK,
	.bank_ops = &xgmi_v6_4_0_aca_bank_ops,
};

static int amdgpu_xgmi_ras_late_init(struct amdgpu_device *adev, struct ras_common_if *ras_block)
{
	int r;

	if (!adev->gmc.xgmi.supported ||
	    adev->gmc.xgmi.num_physical_nodes == 0)
		return 0;

	amdgpu_ras_reset_error_count(adev, AMDGPU_RAS_BLOCK__XGMI_WAFL);

	r = amdgpu_ras_block_late_init(adev, ras_block);
	if (r)
		return r;

	switch (amdgpu_ip_version(adev, XGMI_HWIP, 0)) {
	case IP_VERSION(6, 4, 0):
	case IP_VERSION(6, 4, 1):
		r = amdgpu_ras_bind_aca(adev, AMDGPU_RAS_BLOCK__XGMI_WAFL,
					&xgmi_v6_4_0_aca_info, NULL);
		if (r)
			goto late_fini;
		break;
	default:
		break;
	}

	return 0;

late_fini:
	amdgpu_ras_block_late_fini(adev, ras_block);

	return r;
}

uint64_t amdgpu_xgmi_get_relative_phy_addr(struct amdgpu_device *adev,
					   uint64_t addr)
{
	struct amdgpu_xgmi *xgmi = &adev->gmc.xgmi;
	return (addr + xgmi->physical_node_id * xgmi->node_segment_size);
}

static void pcs_clear_status(struct amdgpu_device *adev, uint32_t pcs_status_reg)
{
	WREG32_PCIE(pcs_status_reg, 0xFFFFFFFF);
	WREG32_PCIE(pcs_status_reg, 0);
}

static void amdgpu_xgmi_legacy_reset_ras_error_count(struct amdgpu_device *adev)
{
	uint32_t i;

	switch (adev->asic_type) {
	case CHIP_ARCTURUS:
		for (i = 0; i < ARRAY_SIZE(xgmi_pcs_err_status_reg_arct); i++)
			pcs_clear_status(adev,
					 xgmi_pcs_err_status_reg_arct[i]);
		break;
	case CHIP_VEGA20:
		for (i = 0; i < ARRAY_SIZE(xgmi_pcs_err_status_reg_vg20); i++)
			pcs_clear_status(adev,
					 xgmi_pcs_err_status_reg_vg20[i]);
		break;
	case CHIP_ALDEBARAN:
		for (i = 0; i < ARRAY_SIZE(xgmi3x16_pcs_err_status_reg_aldebaran); i++)
			pcs_clear_status(adev,
					 xgmi3x16_pcs_err_status_reg_aldebaran[i]);
		for (i = 0; i < ARRAY_SIZE(walf_pcs_err_status_reg_aldebaran); i++)
			pcs_clear_status(adev,
					 walf_pcs_err_status_reg_aldebaran[i]);
		break;
	default:
		break;
	}

	switch (amdgpu_ip_version(adev, XGMI_HWIP, 0)) {
	case IP_VERSION(6, 4, 0):
	case IP_VERSION(6, 4, 1):
		for (i = 0; i < ARRAY_SIZE(xgmi3x16_pcs_err_status_reg_v6_4); i++)
			pcs_clear_status(adev,
					xgmi3x16_pcs_err_status_reg_v6_4[i]);
		break;
	default:
		break;
	}
}

static void __xgmi_v6_4_0_reset_error_count(struct amdgpu_device *adev, int xgmi_inst, u64 mca_base)
{
	WREG64_MCA(xgmi_inst, mca_base, ACA_REG_IDX_STATUS, 0ULL);
}

static void xgmi_v6_4_0_reset_error_count(struct amdgpu_device *adev, int xgmi_inst)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(xgmi_v6_4_0_mca_base_array); i++)
		__xgmi_v6_4_0_reset_error_count(adev, xgmi_inst, xgmi_v6_4_0_mca_base_array[i]);
}

static void xgmi_v6_4_0_reset_ras_error_count(struct amdgpu_device *adev)
{
	int i;

	for_each_inst(i, adev->aid_mask)
		xgmi_v6_4_0_reset_error_count(adev, i);
}

static void amdgpu_xgmi_reset_ras_error_count(struct amdgpu_device *adev)
{
	switch (amdgpu_ip_version(adev, XGMI_HWIP, 0)) {
	case IP_VERSION(6, 4, 0):
	case IP_VERSION(6, 4, 1):
		xgmi_v6_4_0_reset_ras_error_count(adev);
		break;
	default:
		amdgpu_xgmi_legacy_reset_ras_error_count(adev);
		break;
	}
}

static int amdgpu_xgmi_query_pcs_error_status(struct amdgpu_device *adev,
					      uint32_t value,
						  uint32_t mask_value,
					      uint32_t *ue_count,
					      uint32_t *ce_count,
					      bool is_xgmi_pcs,
						  bool check_mask)
{
	int i;
	int ue_cnt = 0;
	const struct amdgpu_pcs_ras_field *pcs_ras_fields = NULL;
	uint32_t field_array_size = 0;

	if (is_xgmi_pcs) {
		if (amdgpu_ip_version(adev, XGMI_HWIP, 0) ==
		    IP_VERSION(6, 1, 0) ||
		    amdgpu_ip_version(adev, XGMI_HWIP, 0) ==
		    IP_VERSION(6, 4, 0) ||
		    amdgpu_ip_version(adev, XGMI_HWIP, 0) ==
		    IP_VERSION(6, 4, 1)) {
			pcs_ras_fields = &xgmi3x16_pcs_ras_fields[0];
			field_array_size = ARRAY_SIZE(xgmi3x16_pcs_ras_fields);
		} else {
			pcs_ras_fields = &xgmi_pcs_ras_fields[0];
			field_array_size = ARRAY_SIZE(xgmi_pcs_ras_fields);
		}
	} else {
		pcs_ras_fields = &wafl_pcs_ras_fields[0];
		field_array_size = ARRAY_SIZE(wafl_pcs_ras_fields);
	}

	if (check_mask)
		value = value & ~mask_value;

	/* query xgmi/walf pcs error status,
	 * only ue is supported */
	for (i = 0; value && i < field_array_size; i++) {
		ue_cnt = (value &
				pcs_ras_fields[i].pcs_err_mask) >>
				pcs_ras_fields[i].pcs_err_shift;
		if (ue_cnt) {
			dev_info(adev->dev, "%s detected\n",
				 pcs_ras_fields[i].err_name);
			*ue_count += ue_cnt;
		}

		/* reset bit value if the bit is checked */
		value &= ~(pcs_ras_fields[i].pcs_err_mask);
	}

	return 0;
}

static void amdgpu_xgmi_legacy_query_ras_error_count(struct amdgpu_device *adev,
						     void *ras_error_status)
{
	struct ras_err_data *err_data = (struct ras_err_data *)ras_error_status;
	int i, supported = 1;
	uint32_t data, mask_data = 0;
	uint32_t ue_cnt = 0, ce_cnt = 0;

	if (!amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__XGMI_WAFL))
		return ;

	err_data->ue_count = 0;
	err_data->ce_count = 0;

	switch (adev->asic_type) {
	case CHIP_ARCTURUS:
		/* check xgmi pcs error */
		for (i = 0; i < ARRAY_SIZE(xgmi_pcs_err_status_reg_arct); i++) {
			data = RREG32_PCIE(xgmi_pcs_err_status_reg_arct[i]);
			if (data)
				amdgpu_xgmi_query_pcs_error_status(adev, data,
						mask_data, &ue_cnt, &ce_cnt, true, false);
		}
		/* check wafl pcs error */
		for (i = 0; i < ARRAY_SIZE(wafl_pcs_err_status_reg_arct); i++) {
			data = RREG32_PCIE(wafl_pcs_err_status_reg_arct[i]);
			if (data)
				amdgpu_xgmi_query_pcs_error_status(adev, data,
						mask_data, &ue_cnt, &ce_cnt, false, false);
		}
		break;
	case CHIP_VEGA20:
		/* check xgmi pcs error */
		for (i = 0; i < ARRAY_SIZE(xgmi_pcs_err_status_reg_vg20); i++) {
			data = RREG32_PCIE(xgmi_pcs_err_status_reg_vg20[i]);
			if (data)
				amdgpu_xgmi_query_pcs_error_status(adev, data,
						mask_data, &ue_cnt, &ce_cnt, true, false);
		}
		/* check wafl pcs error */
		for (i = 0; i < ARRAY_SIZE(wafl_pcs_err_status_reg_vg20); i++) {
			data = RREG32_PCIE(wafl_pcs_err_status_reg_vg20[i]);
			if (data)
				amdgpu_xgmi_query_pcs_error_status(adev, data,
						mask_data, &ue_cnt, &ce_cnt, false, false);
		}
		break;
	case CHIP_ALDEBARAN:
		/* check xgmi3x16 pcs error */
		for (i = 0; i < ARRAY_SIZE(xgmi3x16_pcs_err_status_reg_aldebaran); i++) {
			data = RREG32_PCIE(xgmi3x16_pcs_err_status_reg_aldebaran[i]);
			mask_data =
				RREG32_PCIE(xgmi3x16_pcs_err_noncorrectable_mask_reg_aldebaran[i]);
			if (data)
				amdgpu_xgmi_query_pcs_error_status(adev, data,
						mask_data, &ue_cnt, &ce_cnt, true, true);
		}
		/* check wafl pcs error */
		for (i = 0; i < ARRAY_SIZE(walf_pcs_err_status_reg_aldebaran); i++) {
			data = RREG32_PCIE(walf_pcs_err_status_reg_aldebaran[i]);
			mask_data =
				RREG32_PCIE(walf_pcs_err_noncorrectable_mask_reg_aldebaran[i]);
			if (data)
				amdgpu_xgmi_query_pcs_error_status(adev, data,
						mask_data, &ue_cnt, &ce_cnt, false, true);
		}
		break;
	default:
		supported = 0;
		break;
	}

	switch (amdgpu_ip_version(adev, XGMI_HWIP, 0)) {
	case IP_VERSION(6, 4, 0):
	case IP_VERSION(6, 4, 1):
		/* check xgmi3x16 pcs error */
		for (i = 0; i < ARRAY_SIZE(xgmi3x16_pcs_err_status_reg_v6_4); i++) {
			data = RREG32_PCIE(xgmi3x16_pcs_err_status_reg_v6_4[i]);
			mask_data =
				RREG32_PCIE(xgmi3x16_pcs_err_noncorrectable_mask_reg_v6_4[i]);
			if (data)
				amdgpu_xgmi_query_pcs_error_status(adev, data,
						mask_data, &ue_cnt, &ce_cnt, true, true);
		}
		break;
	default:
		if (!supported)
			dev_warn(adev->dev, "XGMI RAS error query not supported");
		break;
	}

	amdgpu_ras_reset_error_count(adev, AMDGPU_RAS_BLOCK__XGMI_WAFL);

	err_data->ue_count += ue_cnt;
	err_data->ce_count += ce_cnt;
}

static enum aca_error_type xgmi_v6_4_0_pcs_mca_get_error_type(struct amdgpu_device *adev, u64 status)
{
	const char *error_str;
	int ext_error_code;

	ext_error_code = ACA_REG__STATUS__ERRORCODEEXT(status);

	error_str = ext_error_code < ARRAY_SIZE(xgmi_v6_4_0_ras_error_code_ext) ?
		xgmi_v6_4_0_ras_error_code_ext[ext_error_code] : NULL;
	if (error_str)
		dev_info(adev->dev, "%s detected\n", error_str);

	switch (ext_error_code) {
	case 0:
		return ACA_ERROR_TYPE_UE;
	case 6:
		return ACA_ERROR_TYPE_CE;
	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static void __xgmi_v6_4_0_query_error_count(struct amdgpu_device *adev, struct amdgpu_smuio_mcm_config_info *mcm_info,
					    u64 mca_base, struct ras_err_data *err_data)
{
	int xgmi_inst = mcm_info->die_id;
	u64 status = 0;

	status = RREG64_MCA(xgmi_inst, mca_base, ACA_REG_IDX_STATUS);
	if (!ACA_REG__STATUS__VAL(status))
		return;

	switch (xgmi_v6_4_0_pcs_mca_get_error_type(adev, status)) {
	case ACA_ERROR_TYPE_UE:
		amdgpu_ras_error_statistic_ue_count(err_data, mcm_info, 1ULL);
		break;
	case ACA_ERROR_TYPE_CE:
		amdgpu_ras_error_statistic_ce_count(err_data, mcm_info, 1ULL);
		break;
	default:
		break;
	}

	WREG64_MCA(xgmi_inst, mca_base, ACA_REG_IDX_STATUS, 0ULL);
}

static void xgmi_v6_4_0_query_error_count(struct amdgpu_device *adev, int xgmi_inst, struct ras_err_data *err_data)
{
	struct amdgpu_smuio_mcm_config_info mcm_info = {
		.socket_id = adev->smuio.funcs->get_socket_id(adev),
		.die_id = xgmi_inst,
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(xgmi_v6_4_0_mca_base_array); i++)
		__xgmi_v6_4_0_query_error_count(adev, &mcm_info, xgmi_v6_4_0_mca_base_array[i], err_data);
}

static void xgmi_v6_4_0_query_ras_error_count(struct amdgpu_device *adev, void *ras_error_status)
{
	struct ras_err_data *err_data = (struct ras_err_data *)ras_error_status;
	int i;

	for_each_inst(i, adev->aid_mask)
		xgmi_v6_4_0_query_error_count(adev, i, err_data);
}

static void amdgpu_xgmi_query_ras_error_count(struct amdgpu_device *adev,
					      void *ras_error_status)
{
	switch (amdgpu_ip_version(adev, XGMI_HWIP, 0)) {
	case IP_VERSION(6, 4, 0):
	case IP_VERSION(6, 4, 1):
		xgmi_v6_4_0_query_ras_error_count(adev, ras_error_status);
		break;
	default:
		amdgpu_xgmi_legacy_query_ras_error_count(adev, ras_error_status);
		break;
	}
}

/* Trigger XGMI/WAFL error */
static int amdgpu_ras_error_inject_xgmi(struct amdgpu_device *adev,
			void *inject_if, uint32_t instance_mask)
{
	int ret1, ret2;
	struct ta_ras_trigger_error_input *block_info =
				(struct ta_ras_trigger_error_input *)inject_if;

	if (amdgpu_dpm_set_df_cstate(adev, DF_CSTATE_DISALLOW))
		dev_warn(adev->dev, "Failed to disallow df cstate");

	ret1 = amdgpu_dpm_set_pm_policy(adev, PP_PM_POLICY_XGMI_PLPD, XGMI_PLPD_DISALLOW);
	if (ret1 && ret1 != -EOPNOTSUPP)
		dev_warn(adev->dev, "Failed to disallow XGMI power down");

	ret2 = psp_ras_trigger_error(&adev->psp, block_info, instance_mask);

	if (amdgpu_ras_intr_triggered())
		return ret2;

	ret1 = amdgpu_dpm_set_pm_policy(adev, PP_PM_POLICY_XGMI_PLPD, XGMI_PLPD_DEFAULT);
	if (ret1 && ret1 != -EOPNOTSUPP)
		dev_warn(adev->dev, "Failed to allow XGMI power down");

	if (amdgpu_dpm_set_df_cstate(adev, DF_CSTATE_ALLOW))
		dev_warn(adev->dev, "Failed to allow df cstate");

	return ret2;
}

struct amdgpu_ras_block_hw_ops  xgmi_ras_hw_ops = {
	.query_ras_error_count = amdgpu_xgmi_query_ras_error_count,
	.reset_ras_error_count = amdgpu_xgmi_reset_ras_error_count,
	.ras_error_inject = amdgpu_ras_error_inject_xgmi,
};

struct amdgpu_xgmi_ras xgmi_ras = {
	.ras_block = {
		.hw_ops = &xgmi_ras_hw_ops,
		.ras_late_init = amdgpu_xgmi_ras_late_init,
	},
};

int amdgpu_xgmi_ras_sw_init(struct amdgpu_device *adev)
{
	int err;
	struct amdgpu_xgmi_ras *ras;

	if (!adev->gmc.xgmi.ras)
		return 0;

	ras = adev->gmc.xgmi.ras;
	err = amdgpu_ras_register_ras_block(adev, &ras->ras_block);
	if (err) {
		dev_err(adev->dev, "Failed to register xgmi_wafl_pcs ras block!\n");
		return err;
	}

	strcpy(ras->ras_block.ras_comm.name, "xgmi_wafl");
	ras->ras_block.ras_comm.block = AMDGPU_RAS_BLOCK__XGMI_WAFL;
	ras->ras_block.ras_comm.type = AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE;
	adev->gmc.xgmi.ras_if = &ras->ras_block.ras_comm;

	return 0;
}

static void amdgpu_xgmi_reset_on_init_work(struct work_struct *work)
{
	struct amdgpu_hive_info *hive =
		container_of(work, struct amdgpu_hive_info, reset_on_init_work);
	struct amdgpu_reset_context reset_context;
	struct amdgpu_device *tmp_adev;
	struct list_head device_list;
	int r;

	mutex_lock(&hive->hive_lock);

	INIT_LIST_HEAD(&device_list);
	list_for_each_entry(tmp_adev, &hive->device_list, gmc.xgmi.head)
		list_add_tail(&tmp_adev->reset_list, &device_list);

	tmp_adev = list_first_entry(&device_list, struct amdgpu_device,
				    reset_list);
	amdgpu_device_lock_reset_domain(tmp_adev->reset_domain);

	reset_context.method = AMD_RESET_METHOD_ON_INIT;
	reset_context.reset_req_dev = tmp_adev;
	reset_context.hive = hive;
	reset_context.reset_device_list = &device_list;
	set_bit(AMDGPU_NEED_FULL_RESET, &reset_context.flags);
	set_bit(AMDGPU_SKIP_COREDUMP, &reset_context.flags);

	amdgpu_reset_do_xgmi_reset_on_init(&reset_context);
	mutex_unlock(&hive->hive_lock);
	amdgpu_device_unlock_reset_domain(tmp_adev->reset_domain);

	list_for_each_entry(tmp_adev, &hive->device_list, gmc.xgmi.head) {
		r = amdgpu_ras_init_badpage_info(tmp_adev);
		if (r && r != -EHWPOISON)
			dev_err(tmp_adev->dev,
				"error during bad page data initialization");
	}
}

static void amdgpu_xgmi_schedule_reset_on_init(struct amdgpu_hive_info *hive)
{
	INIT_WORK(&hive->reset_on_init_work, amdgpu_xgmi_reset_on_init_work);
	amdgpu_reset_domain_schedule(hive->reset_domain,
				     &hive->reset_on_init_work);
}

int amdgpu_xgmi_reset_on_init(struct amdgpu_device *adev)
{
	struct amdgpu_hive_info *hive;
	bool reset_scheduled;
	int num_devs;

	hive = amdgpu_get_xgmi_hive(adev);
	if (!hive)
		return -EINVAL;

	mutex_lock(&hive->hive_lock);
	num_devs = atomic_read(&hive->number_devices);
	reset_scheduled = false;
	if (num_devs == adev->gmc.xgmi.num_physical_nodes) {
		amdgpu_xgmi_schedule_reset_on_init(hive);
		reset_scheduled = true;
	}

	mutex_unlock(&hive->hive_lock);
	amdgpu_put_xgmi_hive(hive);

	if (reset_scheduled)
		flush_work(&hive->reset_on_init_work);

	return 0;
}

int amdgpu_xgmi_request_nps_change(struct amdgpu_device *adev,
				   struct amdgpu_hive_info *hive,
				   int req_nps_mode)
{
	struct amdgpu_device *tmp_adev;
	int cur_nps_mode, r;

	/* This is expected to be called only during unload of driver. The
	 * request needs to be placed only once for all devices in the hive. If
	 * one of them fail, revert the request for previous successful devices.
	 * After placing the request, make hive mode as UNKNOWN so that other
	 * devices don't request anymore.
	 */
	mutex_lock(&hive->hive_lock);
	if (atomic_read(&hive->requested_nps_mode) ==
	    UNKNOWN_MEMORY_PARTITION_MODE) {
		dev_dbg(adev->dev, "Unexpected entry for hive NPS change");
		mutex_unlock(&hive->hive_lock);
		return 0;
	}
	list_for_each_entry(tmp_adev, &hive->device_list, gmc.xgmi.head) {
		r = adev->gmc.gmc_funcs->request_mem_partition_mode(
			tmp_adev, req_nps_mode);
		if (r)
			break;
	}
	if (r) {
		/* Request back current mode if one of the requests failed */
		cur_nps_mode =
			adev->gmc.gmc_funcs->query_mem_partition_mode(tmp_adev);
		list_for_each_entry_continue_reverse(
			tmp_adev, &hive->device_list, gmc.xgmi.head)
			adev->gmc.gmc_funcs->request_mem_partition_mode(
				tmp_adev, cur_nps_mode);
	}
	/* Set to UNKNOWN so that other devices don't request anymore */
	atomic_set(&hive->requested_nps_mode, UNKNOWN_MEMORY_PARTITION_MODE);
	mutex_unlock(&hive->hive_lock);

	return r;
}

bool amdgpu_xgmi_same_hive(struct amdgpu_device *adev,
			   struct amdgpu_device *bo_adev)
{
	return (amdgpu_use_xgmi_p2p && adev != bo_adev &&
		adev->gmc.xgmi.hive_id &&
		adev->gmc.xgmi.hive_id == bo_adev->gmc.xgmi.hive_id);
}

void amdgpu_xgmi_early_init(struct amdgpu_device *adev)
{
	if (!adev->gmc.xgmi.supported)
		return;

	switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
	case IP_VERSION(9, 4, 0):
	case IP_VERSION(9, 4, 1):
	case IP_VERSION(9, 4, 2):
		adev->gmc.xgmi.max_speed = XGMI_SPEED_25GT;
		adev->gmc.xgmi.max_width = 16;
		break;
	case IP_VERSION(9, 4, 3):
	case IP_VERSION(9, 4, 4):
	case IP_VERSION(9, 5, 0):
		adev->gmc.xgmi.max_speed = XGMI_SPEED_32GT;
		adev->gmc.xgmi.max_width = 16;
		break;
	default:
		break;
	}
}
