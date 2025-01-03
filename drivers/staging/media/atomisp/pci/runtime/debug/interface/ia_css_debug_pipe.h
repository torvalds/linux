/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010 - 2015, Intel Corporation.
 */

#ifndef _IA_CSS_DEBUG_PIPE_H_
#define _IA_CSS_DEBUG_PIPE_H_

/*! \file */

#include <ia_css_frame_public.h>
#include <ia_css_stream_public.h>
#include "ia_css_pipeline.h"

/**
 * @brief Internal debug support for constructing a pipe graph.
 *
 * @return	None
 */
void ia_css_debug_pipe_graph_dump_prologue(void);

/**
 * @brief Internal debug support for constructing a pipe graph.
 *
 * @return	None
 */
void ia_css_debug_pipe_graph_dump_epilogue(void);

/**
 * @brief Internal debug support for constructing a pipe graph.
 * @param[in]	stage		Pipeline stage.
 * @param[in]	id		Pipe id.
 *
 * @return	None
 */
void ia_css_debug_pipe_graph_dump_stage(
    struct ia_css_pipeline_stage *stage,
    enum ia_css_pipe_id id);

/**
 * @brief Internal debug support for constructing a pipe graph.
 * @param[in]	out_frame	Output frame of SP raw copy.
 *
 * @return	None
 */
void ia_css_debug_pipe_graph_dump_sp_raw_copy(
    struct ia_css_frame *out_frame);

/**
 * @brief Internal debug support for constructing a pipe graph.
 * @param[in]	stream_config	info about sensor and input formatter.
 *
 * @return	None
 */
void ia_css_debug_pipe_graph_dump_stream_config(
    const struct ia_css_stream_config *stream_config);

#endif /* _IA_CSS_DEBUG_PIPE_H_ */
