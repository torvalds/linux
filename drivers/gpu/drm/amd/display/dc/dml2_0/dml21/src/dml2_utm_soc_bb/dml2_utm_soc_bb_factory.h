// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DML2_UTM_SOC_BB_FACTORY_H__
#define __DML2_UTM_SOC_BB_FACTORY_H__

#include "dml2_internal_shared_types.h"
bool dml2_utm_soc_bb_create(enum dml2_project_id project_id, struct dml2_utm_soc_bb *utm_soc_bb,
		const struct dml2_soc_bb *soc_bb, const struct utm_qos_model *qos_model);
#endif /* __DML2_UTM_SOC_BB_FACTORY_H__ */
