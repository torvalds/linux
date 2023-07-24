/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2020 Marvell.
 */

#ifndef __OTX2_CPT_COMMON_H
#define __OTX2_CPT_COMMON_H

#include <linux/pci.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/crypto.h>
#include <net/devlink.h>
#include "otx2_cpt_hw_types.h"
#include "rvu.h"
#include "mbox.h"

#define OTX2_CPT_MAX_VFS_NUM 128
#define OTX2_CPT_RVU_FUNC_ADDR_S(blk, slot, offs) \
		(((blk) << 20) | ((slot) << 12) | (offs))
#define OTX2_CPT_RVU_PFFUNC(pf, func)	\
		((((pf) & RVU_PFVF_PF_MASK) << RVU_PFVF_PF_SHIFT) | \
		(((func) & RVU_PFVF_FUNC_MASK) << RVU_PFVF_FUNC_SHIFT))

#define OTX2_CPT_INVALID_CRYPTO_ENG_GRP 0xFF
#define OTX2_CPT_NAME_LENGTH 64
#define OTX2_CPT_DMA_MINALIGN 128

/* HW capability flags */
#define CN10K_MBOX  0
#define CN10K_LMTST 1

#define BAD_OTX2_CPT_ENG_TYPE OTX2_CPT_MAX_ENG_TYPES

enum otx2_cpt_eng_type {
	OTX2_CPT_AE_TYPES = 1,
	OTX2_CPT_SE_TYPES = 2,
	OTX2_CPT_IE_TYPES = 3,
	OTX2_CPT_MAX_ENG_TYPES,
};

/* Take mbox id from end of CPT mbox range in AF (range 0xA00 - 0xBFF) */
#define MBOX_MSG_RX_INLINE_IPSEC_LF_CFG 0xBFE
#define MBOX_MSG_GET_ENG_GRP_NUM        0xBFF
#define MBOX_MSG_GET_CAPS               0xBFD
#define MBOX_MSG_GET_KVF_LIMITS         0xBFC

/*
 * Message request to config cpt lf for inline inbound ipsec.
 * This message is only used between CPT PF <-> CPT VF
 */
struct otx2_cpt_rx_inline_lf_cfg {
	struct mbox_msghdr hdr;
	u16 sso_pf_func;
	u16 param1;
	u16 param2;
	u16 opcode;
	u32 credit;
	u32 reserved;
};

/*
 * Message request and response to get engine group number
 * which has attached a given type of engines (SE, AE, IE)
 * This messages are only used between CPT PF <=> CPT VF
 */
struct otx2_cpt_egrp_num_msg {
	struct mbox_msghdr hdr;
	u8 eng_type;
};

struct otx2_cpt_egrp_num_rsp {
	struct mbox_msghdr hdr;
	u8 eng_type;
	u8 eng_grp_num;
};

/*
 * Message request and response to get kernel crypto limits
 * This messages are only used between CPT PF <-> CPT VF
 */
struct otx2_cpt_kvf_limits_msg {
	struct mbox_msghdr hdr;
};

struct otx2_cpt_kvf_limits_rsp {
	struct mbox_msghdr hdr;
	u8 kvf_limits;
};

/* CPT HW capabilities */
union otx2_cpt_eng_caps {
	u64 u;
	struct {
		u64 reserved_0_4:5;
		u64 mul:1;
		u64 sha1_sha2:1;
		u64 chacha20:1;
		u64 zuc_snow3g:1;
		u64 sha3:1;
		u64 aes:1;
		u64 kasumi:1;
		u64 des:1;
		u64 crc:1;
		u64 reserved_14_63:50;
	};
};

/*
 * Message request and response to get HW capabilities for each
 * engine type (SE, IE, AE).
 * This messages are only used between CPT PF <=> CPT VF
 */
struct otx2_cpt_caps_msg {
	struct mbox_msghdr hdr;
};

struct otx2_cpt_caps_rsp {
	struct mbox_msghdr hdr;
	u16 cpt_pf_drv_version;
	u8 cpt_revision;
	union otx2_cpt_eng_caps eng_caps[OTX2_CPT_MAX_ENG_TYPES];
};

static inline void otx2_cpt_write64(void __iomem *reg_base, u64 blk, u64 slot,
				    u64 offs, u64 val)
{
	writeq_relaxed(val, reg_base +
		       OTX2_CPT_RVU_FUNC_ADDR_S(blk, slot, offs));
}

static inline u64 otx2_cpt_read64(void __iomem *reg_base, u64 blk, u64 slot,
				  u64 offs)
{
	return readq_relaxed(reg_base +
			     OTX2_CPT_RVU_FUNC_ADDR_S(blk, slot, offs));
}

static inline bool is_dev_otx2(struct pci_dev *pdev)
{
	if (pdev->device == OTX2_CPT_PCI_PF_DEVICE_ID ||
	    pdev->device == OTX2_CPT_PCI_VF_DEVICE_ID)
		return true;

	return false;
}

static inline void otx2_cpt_set_hw_caps(struct pci_dev *pdev,
					unsigned long *cap_flag)
{
	if (!is_dev_otx2(pdev)) {
		__set_bit(CN10K_MBOX, cap_flag);
		__set_bit(CN10K_LMTST, cap_flag);
	}
}


int otx2_cpt_send_ready_msg(struct otx2_mbox *mbox, struct pci_dev *pdev);
int otx2_cpt_send_mbox_msg(struct otx2_mbox *mbox, struct pci_dev *pdev);

int otx2_cpt_send_af_reg_requests(struct otx2_mbox *mbox,
				  struct pci_dev *pdev);
int otx2_cpt_add_write_af_reg(struct otx2_mbox *mbox, struct pci_dev *pdev,
			      u64 reg, u64 val, int blkaddr);
int otx2_cpt_read_af_reg(struct otx2_mbox *mbox, struct pci_dev *pdev,
			 u64 reg, u64 *val, int blkaddr);
int otx2_cpt_write_af_reg(struct otx2_mbox *mbox, struct pci_dev *pdev,
			  u64 reg, u64 val, int blkaddr);
struct otx2_cptlfs_info;
int otx2_cpt_attach_rscrs_msg(struct otx2_cptlfs_info *lfs);
int otx2_cpt_detach_rsrcs_msg(struct otx2_cptlfs_info *lfs);
int otx2_cpt_msix_offset_msg(struct otx2_cptlfs_info *lfs);
int otx2_cpt_sync_mbox_msg(struct otx2_mbox *mbox);

#endif /* __OTX2_CPT_COMMON_H */
