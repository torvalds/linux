// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#ifndef __SOC_AND_IP_TRANSLATOR_H__
#define __SOC_AND_IP_TRANSLATOR_H__

#include "dc.h"
#include "dml_top_soc_parameter_types.h"

struct soc_and_ip_translator_funcs {
	void (*get_soc_bb)(struct dml2_soc_bb *soc_bb, const struct dc *dc, const struct dml2_configuration_options *config);
	void (*get_ip_caps)(struct dml2_ip_capabilities *dml_ip_caps);
};

struct soc_and_ip_translator {
	const struct soc_and_ip_translator_funcs *translator_funcs;
};

struct soc_and_ip_translator *dc_create_soc_and_ip_translator(enum dce_version dc_version);
void dc_destroy_soc_and_ip_translator(struct soc_and_ip_translator **soc_and_ip_translator);


#endif // __SOC_AND_IP_TRANSLATOR_H__
