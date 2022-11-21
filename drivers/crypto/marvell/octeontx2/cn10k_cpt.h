/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2021 Marvell.
 */
#ifndef __CN10K_CPT_H
#define __CN10K_CPT_H

#include "otx2_cpt_common.h"
#include "otx2_cptpf.h"
#include "otx2_cptvf.h"

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

void cn10k_cpt_send_cmd(union otx2_cpt_inst_s *cptinst, u32 insts_num,
			struct otx2_cptlf_info *lf);
int cn10k_cptpf_lmtst_init(struct otx2_cptpf_dev *cptpf);
int cn10k_cptvf_lmtst_init(struct otx2_cptvf_dev *cptvf);

#endif /* __CN10K_CPTLF_H */
