/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2021 HiSilicon Ltd. */

#ifndef HISI_ACC_VFIO_PCI_H
#define HISI_ACC_VFIO_PCI_H

#include <linux/hisi_acc_qm.h>

#define MB_POLL_PERIOD_US		10
#define MB_POLL_TIMEOUT_US		1000
#define QM_CACHE_WB_START		0x204
#define QM_CACHE_WB_DONE		0x208
#define QM_MB_CMD_PAUSE_QM		0xe
#define QM_ABNORMAL_INT_STATUS		0x100008
#define QM_IFC_INT_STATUS		0x0028
#define SEC_CORE_INT_STATUS		0x301008
#define HPRE_HAC_INT_STATUS		0x301800
#define HZIP_CORE_INT_STATUS		0x3010AC

#define QM_VFT_CFG_RDY			0x10006c
#define QM_VFT_CFG_OP_WR		0x100058
#define QM_VFT_CFG_TYPE			0x10005c
#define QM_VFT_CFG			0x100060
#define QM_VFT_CFG_OP_ENABLE		0x100054
#define QM_VFT_CFG_DATA_L		0x100064
#define QM_VFT_CFG_DATA_H		0x100068

#define ERROR_CHECK_TIMEOUT		100
#define CHECK_DELAY_TIME		100

#define QM_SQC_VFT_BASE_SHIFT_V2	28
#define QM_SQC_VFT_BASE_MASK_V2		GENMASK(15, 0)
#define QM_SQC_VFT_NUM_SHIFT_V2		45
#define QM_SQC_VFT_NUM_MASK_V2		GENMASK(9, 0)

/* RW regs */
#define QM_REGS_MAX_LEN		7
#define QM_REG_ADDR_OFFSET	0x0004

#define QM_XQC_ADDR_OFFSET	32U
#define QM_VF_AEQ_INT_MASK	0x0004
#define QM_VF_EQ_INT_MASK	0x000c
#define QM_IFC_INT_SOURCE_V	0x0020
#define QM_IFC_INT_MASK		0x0024
#define QM_IFC_INT_SET_V	0x002c
#define QM_QUE_ISO_CFG_V	0x0030
#define QM_PAGE_SIZE		0x0034

#define QM_EQC_DW0		0X8000
#define QM_AEQC_DW0		0X8020

struct acc_vf_data {
#define QM_MATCH_SIZE offsetofend(struct acc_vf_data, qm_rsv_state)
	/* QM match information */
#define ACC_DEV_MAGIC	0XCDCDCDCDFEEDAACC
	u64 acc_magic;
	u32 qp_num;
	u32 dev_id;
	u32 que_iso_cfg;
	u32 qp_base;
	u32 vf_qm_state;
	/* QM reserved match information */
	u32 qm_rsv_state[3];

	/* QM RW regs */
	u32 aeq_int_mask;
	u32 eq_int_mask;
	u32 ifc_int_source;
	u32 ifc_int_mask;
	u32 ifc_int_set;
	u32 page_size;

	/* QM_EQC_DW has 7 regs */
	u32 qm_eqc_dw[7];

	/* QM_AEQC_DW has 7 regs */
	u32 qm_aeqc_dw[7];

	/* QM reserved 5 regs */
	u32 qm_rsv_regs[5];
	u32 padding;
	/* QM memory init information */
	u64 eqe_dma;
	u64 aeqe_dma;
	u64 sqc_dma;
	u64 cqc_dma;
};

struct hisi_acc_vf_migration_file {
	struct file *filp;
	struct mutex lock;
	bool disabled;

	struct acc_vf_data vf_data;
	size_t total_length;
};

struct hisi_acc_vf_core_device {
	struct vfio_pci_core_device core_device;
	u8 deferred_reset:1;
	/* For migration state */
	struct mutex state_mutex;
	enum vfio_device_mig_state mig_state;
	struct pci_dev *pf_dev;
	struct pci_dev *vf_dev;
	struct hisi_qm *pf_qm;
	struct hisi_qm vf_qm;
	u32 vf_qm_state;
	int vf_id;
	/* For reset handler */
	spinlock_t reset_lock;
	struct hisi_acc_vf_migration_file *resuming_migf;
	struct hisi_acc_vf_migration_file *saving_migf;
};
#endif /* HISI_ACC_VFIO_PCI_H */
