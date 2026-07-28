// SPDX-License-Identifier: MIT
//
// Copyright 2026 Advanced Micro Devices, Inc.

#include "../dcn42/dcn42_soc_and_ip_translator.h"
#include "dcn42b_soc_and_ip_translator.h"
#include "../dcn401/dcn401_soc_and_ip_translator.h"
#include "bounding_boxes/dcn42b_soc_bb.h"

/* soc_and_ip_translator component used to get up-to-date values for bounding box.
 * Bounding box values are stored in several locations and locations can vary with DCN revision.
 * This component provides an interface to get DCN-specific bounding box values.
 */

static void get_default_soc_bb(struct dml2_soc_bb *soc_bb)
{
	memcpy(soc_bb, &dml2_socbb_dcn42b, sizeof(struct dml2_soc_bb));
	memcpy(&soc_bb->qos_parameters, &dml_dcn42b_variant_a_soc_qos_params, sizeof(struct dml2_soc_qos_parameters));
}

void dcn42b_get_soc_bb(struct dml2_soc_bb *soc_bb, const struct dc *dc, const struct dml2_configuration_options *config)
{
	//get default soc_bb with static values
	get_default_soc_bb(soc_bb);
	//update soc_bb values with more accurate values
	dcn42_apply_soc_bb_updates(soc_bb, dc, config);
}

static void dcn42b_get_ip_caps(struct dml2_ip_capabilities *ip_caps)
{
	*ip_caps = dml2_dcn42b_max_ip_caps;
}

static struct soc_and_ip_translator_funcs dcn42b_translator_funcs = {
	.get_soc_bb = dcn42b_get_soc_bb,
	.get_ip_caps = dcn42b_get_ip_caps,
};

void dcn42b_construct_soc_and_ip_translator(struct soc_and_ip_translator *soc_and_ip_translator)
{
	soc_and_ip_translator->translator_funcs = &dcn42b_translator_funcs;
}
