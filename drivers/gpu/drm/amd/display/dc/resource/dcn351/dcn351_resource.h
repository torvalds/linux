/* SPDX-License-Identifier: MIT */
/* Copyright 2024 Advanced Micro Devices, Inc. */

#ifndef _DCN351_RESOURCE_H_
#define _DCN351_RESOURCE_H_

#include "core_types.h"

extern struct _vcs_dpi_ip_params_st dcn3_51_ip;
extern struct _vcs_dpi_soc_bounding_box_st dcn3_51_soc;

#define TO_DCN351_RES_POOL(pool)\
	container_of(pool, struct dcn351_resource_pool, base)

struct dcn351_resource_pool {
	struct resource_pool base;
};

struct resource_pool *dcn351_create_resource_pool(
		const struct dc_init_data *init_data,
		struct dc *dc);

struct pipe_ctx *dcn351_acquire_free_pipe_as_secondary_dpp_pipe(
		const struct dc_state *cur_ctx,
		struct dc_state *new_ctx,
		const struct resource_pool *pool,
		const struct pipe_ctx *opp_head_pipe);

#endif /* _DCN351_RESOURCE_H_ */
