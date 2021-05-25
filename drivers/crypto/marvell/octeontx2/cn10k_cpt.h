/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2021 Marvell.
 */
#ifndef __CN10K_CPT_H
#define __CN10K_CPT_H

#include "otx2_cptpf.h"
#include "otx2_cptvf.h"

int cn10k_cptpf_lmtst_init(struct otx2_cptpf_dev *cptpf);
int cn10k_cptvf_lmtst_init(struct otx2_cptvf_dev *cptvf);

#endif /* __CN10K_CPTLF_H */
