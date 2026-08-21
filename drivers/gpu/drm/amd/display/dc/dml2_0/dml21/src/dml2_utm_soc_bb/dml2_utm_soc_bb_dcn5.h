// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DML2_UTM_SOC_BB_DCN5_H__
#define __DML2_UTM_SOC_BB_DCN5_H__
#include "dml2_internal_shared_types.h"
bool dml2_utm_soc_bb_dcn5_create(struct dml2_utm_soc_bb *utm_soc_bb,
		const struct dml2_soc_bb *soc_bb, const struct utm_qos_model *explicit_qos_model);
void dml2_utm_soc_bb_dcn5_build_sop_table(struct dml2_sop_table *table, const struct dml2_utm_soc_bb *utm_soc_bb);
#endif /* __DML2_UTM_SOC_BB_DCN5_H__ */
