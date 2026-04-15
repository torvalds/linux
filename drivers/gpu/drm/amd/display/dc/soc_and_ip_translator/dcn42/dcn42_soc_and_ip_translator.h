// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#ifndef _DCN42_SOC_AND_IP_TRANSLATOR_H_
#define _DCN42_SOC_AND_IP_TRANSLATOR_H_

#include "core_types.h"
#include "dc.h"
#include "clk_mgr.h"
#include "dml_top_soc_parameter_types.h"
#include "soc_and_ip_translator.h"

void dcn42_construct_soc_and_ip_translator(struct soc_and_ip_translator *soc_and_ip_translator);
void dcn42_get_soc_bb(struct dml2_soc_bb *soc_bb, const struct dc *dc, const struct dml2_configuration_options *config);

#endif /* _DCN42_SOC_AND_IP_TRANSLATOR_H_ */
