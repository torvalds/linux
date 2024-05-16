/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2021 Marvell.
 */
#ifndef __CN10K_CPT_H
#define __CN10K_CPT_H

#include "otx2_cpt_common.h"
#include "otx2_cptpf.h"
#include "otx2_cptvf.h"

#define CN10K_CPT_HW_CTX_SIZE  256

union cn10k_cpt_hw_ctx {
	u64 u;
	struct {
		u64 reserved_0_47:48;
		u64 ctx_push_sz:7;
		u64 reserved_55:1;
		u64 ctx_hdr_sz:2;
		u64 aop_valid:1;
		u64 reserved_59:1;
		u64 ctx_sz:4;
	} w0;
};

struct cn10k_cpt_errata_ctx {
	union cn10k_cpt_hw_ctx *hw_ctx;
	u64 cptr_dma;
};

static inline u8 cn10k_cpt_get_compcode(union otx2_cpt_res_s *result)
{
	return ((struct cn10k_cpt_res_s *)result)->compcode;
}

static inline u8 cn10k_cpt_get_uc_compcode(union otx2_cpt_res_s *result)
{
	return ((struct cn10k_cpt_res_s *)result)->uc_compcode;
}

static inline u8 otx2_cpt_get_compcode(union otx2_cpt_res_s *result)
{
	return ((struct cn9k_cpt_res_s *)result)->compcode;
}

static inline u8 otx2_cpt_get_uc_compcode(union otx2_cpt_res_s *result)
{
	return ((struct cn9k_cpt_res_s *)result)->uc_compcode;
}

int cn10k_cptpf_lmtst_init(struct otx2_cptpf_dev *cptpf);
int cn10k_cptvf_lmtst_init(struct otx2_cptvf_dev *cptvf);
void cn10k_cpt_ctx_flush(struct pci_dev *pdev, u64 cptr, bool inval);
int cn10k_cpt_hw_ctx_init(struct pci_dev *pdev,
			  struct cn10k_cpt_errata_ctx *er_ctx);
void cn10k_cpt_hw_ctx_clear(struct pci_dev *pdev,
			    struct cn10k_cpt_errata_ctx *er_ctx);
void cn10k_cpt_hw_ctx_set(union cn10k_cpt_hw_ctx *hctx, u16 ctx_sz);
void cptvf_hw_ops_get(struct otx2_cptvf_dev *cptvf);

#endif /* __CN10K_CPTLF_H */
