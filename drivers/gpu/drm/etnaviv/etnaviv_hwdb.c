// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Etnaviv Project
 */

#include "etnaviv_gpu.h"

static const struct etnaviv_chip_identity etnaviv_chip_identities[] = {
	{
		.model = 0x400,
		.revision = 0x4652,
		.product_id = 0x70001,
		.customer_id = 0x100,
		.eco_id = 0,
		.stream_count = 4,
		.register_max = 64,
		.thread_count = 128,
		.shader_core_count = 1,
		.vertex_cache_size = 8,
		.vertex_output_buffer_size = 1024,
		.pixel_pipes = 1,
		.instruction_count = 256,
		.num_constants = 320,
		.buffer_size = 0,
		.varyings_count = 8,
		.features = 0xa0e9e004,
		.minor_features0 = 0xe1299fff,
		.minor_features1 = 0xbe13b219,
		.minor_features2 = 0xce110010,
		.minor_features3 = 0x8000001,
		.minor_features4 = 0x20102,
		.minor_features5 = 0x120000,
		.minor_features6 = 0x0,
		.minor_features7 = 0x0,
		.minor_features8 = 0x0,
		.minor_features9 = 0x0,
		.minor_features10 = 0x0,
		.minor_features11 = 0x0,
	},
	{
		.model = 0x7000,
		.revision = 0x6202,
		.product_id = 0x70003,
		.customer_id = 0,
		.eco_id = 0,
		.stream_count = 8,
		.register_max = 64,
		.thread_count = 512,
		.shader_core_count = 2,
		.vertex_cache_size = 16,
		.vertex_output_buffer_size = 1024,
		.pixel_pipes = 1,
		.instruction_count = 512,
		.num_constants = 320,
		.buffer_size = 0,
		.varyings_count = 16,
		.features = 0xe0287cad,
		.minor_features0 = 0xc1489eff,
		.minor_features1 = 0xfefbfad9,
		.minor_features2 = 0xeb9d4fbf,
		.minor_features3 = 0xedfffced,
		.minor_features4 = 0xdb0dafc7,
		.minor_features5 = 0x3b5ac333,
		.minor_features6 = 0xfccee201,
		.minor_features7 = 0x03fffa6f,
		.minor_features8 = 0x00e10ef0,
		.minor_features9 = 0x0088003c,
		.minor_features10 = 0x00004040,
		.minor_features11 = 0x00000024,
	},
	{
		.model = 0x7000,
		.revision = 0x6204,
		.product_id = ~0U,
		.customer_id = ~0U,
		.eco_id = 0,
		.stream_count = 16,
		.register_max = 64,
		.thread_count = 512,
		.shader_core_count = 2,
		.vertex_cache_size = 16,
		.vertex_output_buffer_size = 1024,
		.pixel_pipes = 1,
		.instruction_count = 512,
		.num_constants = 320,
		.buffer_size = 0,
		.varyings_count = 16,
		.features = 0xe0287c8d,
		.minor_features0 = 0xc1589eff,
		.minor_features1 = 0xfefbfad9,
		.minor_features2 = 0xeb9d4fbf,
		.minor_features3 = 0xedfffced,
		.minor_features4 = 0xdb0dafc7,
		.minor_features5 = 0x3b5ac333,
		.minor_features6 = 0xfcce6000,
		.minor_features7 = 0xfffbfa6f,
		.minor_features8 = 0x00e10ef3,
		.minor_features9 = 0x04c8003c,
		.minor_features10 = 0x00004060,
		.minor_features11 = 0x00000024,
	},
	{
		.model = 0x7000,
		.revision = 0x6214,
		.product_id = ~0U,
		.customer_id = ~0U,
		.eco_id = ~0U,
		.stream_count = 16,
		.register_max = 64,
		.thread_count = 1024,
		.shader_core_count = 4,
		.vertex_cache_size = 16,
		.vertex_output_buffer_size = 1024,
		.pixel_pipes = 2,
		.instruction_count = 512,
		.num_constants = 320,
		.buffer_size = 0,
		.varyings_count = 16,
		.features = 0xe0287cad,
		.minor_features0 = 0xc1799eff,
		.minor_features1 = 0xfefbfad9,
		.minor_features2 = 0xeb9d4fbf,
		.minor_features3 = 0xedfffced,
		.minor_features4 = 0xdb0dafc7,
		.minor_features5 = 0xbb5ac333,
		.minor_features6 = 0xfc8ee200,
		.minor_features7 = 0x03fbfa6f,
		.minor_features8 = 0x00ef0ef0,
		.minor_features9 = 0x0edbf03c,
		.minor_features10 = 0x90044250,
		.minor_features11 = 0x00000024,
	},
};

bool etnaviv_fill_identity_from_hwdb(struct etnaviv_gpu *gpu)
{
	struct etnaviv_chip_identity *ident = &gpu->identity;
	int i;

	for (i = 0; i < ARRAY_SIZE(etnaviv_chip_identities); i++) {
		if (etnaviv_chip_identities[i].model == ident->model &&
		    etnaviv_chip_identities[i].revision == ident->revision &&
		    (etnaviv_chip_identities[i].product_id == ident->product_id ||
			 etnaviv_chip_identities[i].product_id == ~0U) &&
		    (etnaviv_chip_identities[i].customer_id == ident->customer_id ||
			 etnaviv_chip_identities[i].customer_id == ~0U) &&
		    (etnaviv_chip_identities[i].eco_id == ident->eco_id ||
			 etnaviv_chip_identities[i].eco_id == ~0U)) {
			memcpy(ident, &etnaviv_chip_identities[i],
			       sizeof(*ident));
			return true;
		}
	}

	return false;
}
