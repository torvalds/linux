// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DC_SPL_TRANSLATE_H__
#define __DC_SPL_TRANSLATE_H__
#include "dc.h"
#include "resource.h"
#include "dm_helpers.h"

/* Map SPL input parameters to pipe context
 * @pipe_ctx: pipe context
 * @spl_in: spl input structure
 */
void translate_SPL_in_params_from_pipe_ctx(struct pipe_ctx *pipe_ctx, struct spl_in *spl_in);

/* Map SPL output parameters to pipe context
 * @pipe_ctx: pipe context
 * @spl_out: spl output structure
 */
void translate_SPL_out_params_to_pipe_ctx(struct pipe_ctx *pipe_ctx, struct spl_out *spl_out);

#endif /* __DC_SPL_TRANSLATE_H__ */
