// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DCN5_SOC_BB_H__
#define __DCN5_SOC_BB_H__

#include "dml2_external_lib_deps.h"
#include "utm_qos_model_dchub_v1.h"
#include "dml_top_soc_parameter_types.h"

static inline void dcn5_initialize_soc_bb(struct dml2_soc_bb *soc_bb)
{
	memset(soc_bb, 0, sizeof(struct dml2_soc_bb));
}

static inline void dcn5_initialize_ip_caps(struct dml2_ip_capabilities *ip_caps)
{
	memset(ip_caps, 0, sizeof(struct dml2_ip_capabilities));
}

static inline void dcn5_initialize_utm_qos_model(struct utm_qos_model *qos_model, struct utm_qos_model_dchub_v1 *dchub)
{
	memset(qos_model, 0, sizeof(struct utm_qos_model));
	memset(dchub, 0, sizeof(struct utm_qos_model_dchub_v1));
	qos_model->dchub_v1 = dchub;
}

static inline void dcn5or_initialize_utm_qos_model(struct utm_qos_model *qos_model, struct utm_qos_model_dchub_v1 *dchub)
{
	memset(qos_model, 0, sizeof(struct utm_qos_model));
	memset(dchub, 0, sizeof(struct utm_qos_model_dchub_v1));
	qos_model->dchub_v1 = dchub;
}

#endif /* __DCN5_SOC_BB_H__ */
