// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#ifndef __DML2_UTM_SOC_BB_DCN6_H__
#define __DML2_UTM_SOC_BB_DCN6_H__
#include "dml2_internal_shared_types.h"
bool dml2_utm_soc_bb_dcn6a_create(struct dml2_utm_soc_bb *utm_soc_bb,
		const struct dml2_soc_bb *soc_bb, const struct utm_qos_model *explicit_qos_model);
bool dml2_utm_soc_bb_dcn6b_create(struct dml2_utm_soc_bb *utm_soc_bb,
		const struct dml2_soc_bb *soc_bb, const struct utm_qos_model *explicit_qos_model);
#endif /* __DML2_UTM_SOC_BB_DCN6_H__ */
