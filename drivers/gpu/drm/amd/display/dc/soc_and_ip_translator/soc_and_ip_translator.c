// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#include "soc_and_ip_translator.h"
#include "soc_and_ip_translator/dcn401/dcn401_soc_and_ip_translator.h"

static void dc_construct_soc_and_ip_translator(struct soc_and_ip_translator *soc_and_ip_translator,
		enum dce_version dc_version)
{
	switch (dc_version) {
	case DCN_VERSION_4_01:
		dcn401_construct_soc_and_ip_translator(soc_and_ip_translator);
		break;
	default:
		break;
	}
}

struct soc_and_ip_translator *dc_create_soc_and_ip_translator(enum dce_version dc_version)
{
	struct soc_and_ip_translator *soc_and_ip_translator;

	soc_and_ip_translator = kzalloc(sizeof(*soc_and_ip_translator), GFP_KERNEL);
	if (!soc_and_ip_translator)
		return NULL;

	dc_construct_soc_and_ip_translator(soc_and_ip_translator, dc_version);

	return soc_and_ip_translator;
}

void dc_destroy_soc_and_ip_translator(struct soc_and_ip_translator **soc_and_ip_translator)
{
	kfree(*soc_and_ip_translator);
	*soc_and_ip_translator = NULL;
}
