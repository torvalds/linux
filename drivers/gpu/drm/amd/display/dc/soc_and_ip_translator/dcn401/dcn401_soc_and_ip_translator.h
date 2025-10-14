// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#ifndef _DCN401_SOC_AND_IP_TRANSLATOR_H_
#define _DCN401_SOC_AND_IP_TRANSLATOR_H_

#include "core_types.h"
#include "dc.h"
#include "clk_mgr.h"
#include "soc_and_ip_translator.h"
#include "dml2/dml21/inc/dml_top_soc_parameter_types.h"

void dcn401_construct_soc_and_ip_translator(struct soc_and_ip_translator *soc_and_ip_translator);

/* Functions that can be re-used by higher DCN revisions of this component */
void dcn401_get_soc_bb(struct dml2_soc_bb *soc_bb, const struct dc *dc, const struct dml2_configuration_options *config);
void dcn401_update_soc_bb_with_values_from_clk_mgr(struct dml2_soc_bb *soc_bb, const struct dc *dc, const struct dml2_configuration_options *config);
void dcn401_update_soc_bb_with_values_from_vbios(struct dml2_soc_bb *soc_bb, const struct dc *dc);
void dcn401_update_soc_bb_with_values_from_software_policy(struct dml2_soc_bb *soc_bb, const struct dc *dc);

#endif /* _DCN401_SOC_AND_IP_TRANSLATOR_H_ */
