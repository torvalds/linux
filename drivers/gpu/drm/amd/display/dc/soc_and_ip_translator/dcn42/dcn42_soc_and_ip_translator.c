// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#include "dcn42_soc_and_ip_translator.h"
#include "soc_and_ip_translator/dcn401/dcn401_soc_and_ip_translator.h"
#include "bounding_boxes/dcn42_soc_bb.h"

/* soc_and_ip_translator component used to get up-to-date values for bounding box.
 * Bounding box values are stored in several locations and locations can vary with DCN revision.
 * This component provides an interface to get DCN-specific bounding box values.
 */

static void dcn42_get_ip_caps(struct dml2_ip_capabilities *ip_caps)
{
	*ip_caps = dml2_dcn42_max_ip_caps;
}

static struct soc_and_ip_translator_funcs dcn42_translator_funcs = {
	.get_soc_bb = dcn401_get_soc_bb,
	.get_ip_caps = dcn42_get_ip_caps,
};

void dcn42_construct_soc_and_ip_translator(struct soc_and_ip_translator *soc_and_ip_translator)
{
	soc_and_ip_translator->translator_funcs = &dcn42_translator_funcs;
}
