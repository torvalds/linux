/*
 * Copyright (C) 2018 Etnaviv Project
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "etnaviv_gpu.h"

static const struct etnaviv_chip_identity etnaviv_chip_identities[] = {
	{
		.model = 0x7000,
		.revision = 0x6214,
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
		    etnaviv_chip_identities[i].revision == ident->revision) {
			memcpy(ident, &etnaviv_chip_identities[i],
			       sizeof(*ident));
			return true;
		}
	}

	return false;
}
