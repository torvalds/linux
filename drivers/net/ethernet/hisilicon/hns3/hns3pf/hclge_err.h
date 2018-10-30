/* SPDX-License-Identifier: GPL-2.0+ */
/*  Copyright (c) 2016-2017 Hisilicon Limited. */

#ifndef __HCLGE_ERR_H
#define __HCLGE_ERR_H

#include "hclge_main.h"

#define HCLGE_RAS_PF_OTHER_INT_STS_REG   0x20B00
#define HCLGE_RAS_REG_FE_MASK    0xFF
#define HCLGE_RAS_REG_NFE_MASK   0xFF00
#define HCLGE_RAS_REG_NFE_SHIFT	8

#define HCLGE_IMP_TCM_ECC_ERR_INT_EN	0xFFFF0000
#define HCLGE_IMP_TCM_ECC_ERR_INT_EN_MASK	0xFFFF0000
#define HCLGE_IMP_ITCM4_ECC_ERR_INT_EN	0x300
#define HCLGE_IMP_ITCM4_ECC_ERR_INT_EN_MASK	0x300
#define HCLGE_CMDQ_NIC_ECC_ERR_INT_EN	0xFFFF
#define HCLGE_CMDQ_NIC_ECC_ERR_INT_EN_MASK	0xFFFF
#define HCLGE_CMDQ_ROCEE_ECC_ERR_INT_EN	0xFFFF0000
#define HCLGE_CMDQ_ROCEE_ECC_ERR_INT_EN_MASK	0xFFFF0000
#define HCLGE_IMP_RD_POISON_ERR_INT_EN	0x0100
#define HCLGE_IMP_RD_POISON_ERR_INT_EN_MASK	0x0100
#define HCLGE_TQP_ECC_ERR_INT_EN	0x0FFF
#define HCLGE_TQP_ECC_ERR_INT_EN_MASK	0x0FFF
#define HCLGE_IGU_ERR_INT_EN	0x0000066F
#define HCLGE_IGU_ERR_INT_EN_MASK	0x000F
#define HCLGE_IGU_TNL_ERR_INT_EN    0x0002AABF
#define HCLGE_IGU_TNL_ERR_INT_EN_MASK  0x003F
#define HCLGE_PPP_MPF_ECC_ERR_INT0_EN	0xFFFFFFFF
#define HCLGE_PPP_MPF_ECC_ERR_INT0_EN_MASK	0xFFFFFFFF
#define HCLGE_PPP_MPF_ECC_ERR_INT1_EN	0xFFFFFFFF
#define HCLGE_PPP_MPF_ECC_ERR_INT1_EN_MASK	0xFFFFFFFF
#define HCLGE_PPP_PF_ERR_INT_EN	0x0003
#define HCLGE_PPP_PF_ERR_INT_EN_MASK	0x0003
#define HCLGE_PPP_MPF_ECC_ERR_INT2_EN	0x003F
#define HCLGE_PPP_MPF_ECC_ERR_INT2_EN_MASK	0x003F
#define HCLGE_PPP_MPF_ECC_ERR_INT3_EN	0x003F
#define HCLGE_PPP_MPF_ECC_ERR_INT3_EN_MASK	0x003F
#define HCLGE_TM_SCH_ECC_ERR_INT_EN	0x3
#define HCLGE_TM_QCN_MEM_ERR_INT_EN	0xFFFFFF
#define HCLGE_NCSI_ERR_INT_EN	0x3
#define HCLGE_NCSI_ERR_INT_TYPE	0x9

#define HCLGE_IMP_TCM_ECC_INT_MASK	0xFFFF
#define HCLGE_IMP_ITCM4_ECC_INT_MASK	0x3
#define HCLGE_CMDQ_ECC_INT_MASK		0xFFFF
#define HCLGE_CMDQ_ROC_ECC_INT_SHIFT	16
#define HCLGE_TQP_ECC_INT_MASK		0xFFF
#define HCLGE_TQP_ECC_INT_SHIFT		16
#define HCLGE_IMP_TCM_ECC_CLR_MASK	0xFFFF
#define HCLGE_IMP_ITCM4_ECC_CLR_MASK	0x3
#define HCLGE_CMDQ_NIC_ECC_CLR_MASK	0xFFFF
#define HCLGE_CMDQ_ROCEE_ECC_CLR_MASK	0xFFFF0000
#define HCLGE_TQP_IMP_ERR_CLR_MASK	0x0FFF0001
#define HCLGE_IGU_COM_INT_MASK		0xF
#define HCLGE_IGU_EGU_TNL_INT_MASK	0x3F
#define HCLGE_PPP_PF_INT_MASK		0x100

enum hclge_err_int_type {
	HCLGE_ERR_INT_MSIX = 0,
	HCLGE_ERR_INT_RAS_CE = 1,
	HCLGE_ERR_INT_RAS_NFE = 2,
	HCLGE_ERR_INT_RAS_FE = 3,
};

struct hclge_hw_blk {
	u32 msk;
	const char *name;
	int (*enable_error)(struct hclge_dev *hdev, bool en);
	void (*process_error)(struct hclge_dev *hdev,
			      enum hclge_err_int_type type);
};

struct hclge_hw_error {
	u32 int_msk;
	const char *msg;
};

int hclge_hw_error_set_state(struct hclge_dev *hdev, bool state);
int hclge_enable_tm_hw_error(struct hclge_dev *hdev, bool en);
pci_ers_result_t hclge_process_ras_hw_error(struct hnae3_ae_dev *ae_dev);
#endif
