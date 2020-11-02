/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010 - 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __IA_CSS_PIPELINE_H__
#define __IA_CSS_PIPELINE_H__

#include "sh_css_internal.h"
#include "ia_css_pipe_public.h"
#include "ia_css_pipeline_common.h"

#define IA_CSS_PIPELINE_NUM_MAX		(20)

/* Pipeline stage to be executed on SP/ISP */
struct ia_css_pipeline_stage {
	unsigned int stage_num;
	struct ia_css_binary *binary;	/* built-in binary */
	struct ia_css_binary_info *binary_info;
	const struct ia_css_fw_info *firmware;	/* acceleration binary */
	/* SP function for SP stage */
	enum ia_css_pipeline_stage_sp_func sp_func;
	unsigned int max_input_width;	/* For SP raw copy */
	struct sh_css_binary_args args;
	int mode;
	bool out_frame_allocated[IA_CSS_BINARY_MAX_OUTPUT_PORTS];
	bool vf_frame_allocated;
	struct ia_css_pipeline_stage *next;
	bool enable_zoom;
};

/* Pipeline of n stages to be executed on SP/ISP per stage */
struct ia_css_pipeline {
	enum ia_css_pipe_id pipe_id;
	u8 pipe_num;
	bool stop_requested;
	struct ia_css_pipeline_stage *stages;
	struct ia_css_pipeline_stage *current_stage;
	unsigned int num_stages;
	struct ia_css_frame in_frame;
	struct ia_css_frame out_frame[IA_CSS_PIPE_MAX_OUTPUT_STAGE];
	struct ia_css_frame vf_frame[IA_CSS_PIPE_MAX_OUTPUT_STAGE];
	unsigned int dvs_frame_delay;
	unsigned int inout_port_config;
	int num_execs;
	bool acquire_isp_each_stage;
	u32 pipe_qos_config;
};

#define DEFAULT_PIPELINE { \
	.pipe_id		= IA_CSS_PIPE_ID_PREVIEW, \
	.in_frame		= DEFAULT_FRAME, \
	.out_frame		= {DEFAULT_FRAME}, \
	.vf_frame		= {DEFAULT_FRAME}, \
	.dvs_frame_delay	= IA_CSS_FRAME_DELAY_1, \
	.num_execs		= -1, \
	.acquire_isp_each_stage	= true, \
	.pipe_qos_config	= QOS_INVALID \
}

/* Stage descriptor used to create a new stage in the pipeline */
struct ia_css_pipeline_stage_desc {
	struct ia_css_binary *binary;
	const struct ia_css_fw_info *firmware;
	enum ia_css_pipeline_stage_sp_func sp_func;
	unsigned int max_input_width;
	unsigned int mode;
	struct ia_css_frame *in_frame;
	struct ia_css_frame *out_frame[IA_CSS_BINARY_MAX_OUTPUT_PORTS];
	struct ia_css_frame *vf_frame;
};

/* @brief initialize the pipeline module
 *
 * @return    None
 *
 * Initializes the pipeline module. This API has to be called
 * before any operation on the pipeline module is done
 */
void ia_css_pipeline_init(void);

/* @brief initialize the pipeline structure with default values
 *
 * @param[out] pipeline  structure to be initialized with defaults
 * @param[in] pipe_id
 * @param[in] pipe_num Number that uniquely identifies a pipeline.
 * @return                     0 or error code upon error.
 *
 * Initializes the pipeline structure with a set of default values.
 * This API is expected to be used when a pipeline structure is allocated
 * externally and needs sane defaults
 */
int ia_css_pipeline_create(
    struct ia_css_pipeline *pipeline,
    enum ia_css_pipe_id pipe_id,
    unsigned int pipe_num,
    unsigned int dvs_frame_delay);

/* @brief destroy a pipeline
 *
 * @param[in] pipeline
 * @return    None
 *
 */
void ia_css_pipeline_destroy(struct ia_css_pipeline *pipeline);

/* @brief Starts a pipeline
 *
 * @param[in] pipe_id
 * @param[in] pipeline
 * @return    None
 *
 */
void ia_css_pipeline_start(enum ia_css_pipe_id pipe_id,
			   struct ia_css_pipeline *pipeline);

/* @brief Request to stop a pipeline
 *
 * @param[in] pipeline
 * @return                     0 or error code upon error.
 *
 */
int ia_css_pipeline_request_stop(struct ia_css_pipeline *pipeline);

/* @brief Check whether pipeline has stopped
 *
 * @param[in] pipeline
 * @return    true if the pipeline has stopped
 *
 */
bool ia_css_pipeline_has_stopped(struct ia_css_pipeline *pipe);

/* @brief clean all the stages pipeline and make it as new
 *
 * @param[in] pipeline
 * @return    None
 *
 */
void ia_css_pipeline_clean(struct ia_css_pipeline *pipeline);

/* @brief Add a stage to pipeline.
 *
 * @param     pipeline               Pointer to the pipeline to be added to.
 * @param[in] stage_desc       The description of the stage
 * @param[out] stage            The successor of the stage.
 * @return                     0 or error code upon error.
 *
 * Add a new stage to a non-NULL pipeline.
 * The stage consists of an ISP binary or firmware and input and output
 * arguments.
*/
int ia_css_pipeline_create_and_add_stage(
    struct ia_css_pipeline *pipeline,
    struct ia_css_pipeline_stage_desc *stage_desc,
    struct ia_css_pipeline_stage **stage);

/* @brief Finalize the stages in a pipeline
 *
 * @param     pipeline               Pointer to the pipeline to be added to.
 * @return                     None
 *
 * This API is expected to be called after adding all stages
*/
void ia_css_pipeline_finalize_stages(struct ia_css_pipeline *pipeline,
				     bool continuous);

/* @brief gets a stage from the pipeline
 *
 * @param[in] pipeline
 * @return                     0 or error code upon error.
 *
 */
int ia_css_pipeline_get_stage(struct ia_css_pipeline *pipeline,
	int mode,
	struct ia_css_pipeline_stage **stage);

/* @brief Gets a pipeline stage corresponding Firmware handle from the pipeline
 *
 * @param[in] pipeline
 * @param[in] fw_handle
 * @param[out] stage Pointer to Stage
 *
 * @return   0 or error code upon error.
 *
 */
int ia_css_pipeline_get_stage_from_fw(struct ia_css_pipeline
	*pipeline,
	u32 fw_handle,
	struct ia_css_pipeline_stage **stage);

/* @brief Gets the Firmware handle corresponding the stage num from the pipeline
 *
 * @param[in] pipeline
 * @param[in] stage_num
 * @param[out] fw_handle
 *
 * @return   0 or error code upon error.
 *
 */
int ia_css_pipeline_get_fw_from_stage(struct ia_css_pipeline
	*pipeline,
	u32 stage_num,
	uint32_t *fw_handle);

/* @brief gets the output stage from the pipeline
 *
 * @param[in] pipeline
 * @return                     0 or error code upon error.
 *
 */
int ia_css_pipeline_get_output_stage(
    struct ia_css_pipeline *pipeline,
    int mode,
    struct ia_css_pipeline_stage **stage);

/* @brief Checks whether the pipeline uses params
 *
 * @param[in] pipeline
 * @return    true if the pipeline uses params
 *
 */
bool ia_css_pipeline_uses_params(struct ia_css_pipeline *pipeline);

/**
 * @brief get the SP thread ID.
 *
 * @param[in]	key	The query key, typical use is pipe_num.
 * @param[out]	val	The query value.
 *
 * @return
 *	true, if the query succeeds;
 *	false, if the query fails.
 */
bool ia_css_pipeline_get_sp_thread_id(unsigned int key, unsigned int *val);

#if defined(ISP2401)
/**
 * @brief Get the pipeline io status
 *
 * @param[in] None
 * @return
 *	Pointer to pipe_io_status
 */
struct sh_css_sp_pipeline_io_status *ia_css_pipeline_get_pipe_io_status(void);
#endif

/**
 * @brief Map an SP thread to this pipeline
 *
 * @param[in]	pipe_num
 * @param[in]	map true for mapping and false for unmapping sp threads.
 *
 */
void ia_css_pipeline_map(unsigned int pipe_num, bool map);

/**
 * @brief Checks whether the pipeline is mapped to SP threads
 *
 * @param[in]	Query key, typical use is pipe_num
 *
 * return
 *	true, pipeline is mapped to SP threads
 *	false, pipeline is not mapped to SP threads
 */
bool ia_css_pipeline_is_mapped(unsigned int key);

/**
 * @brief Print pipeline thread mapping
 *
 * @param[in]	none
 *
 * return none
 */
void ia_css_pipeline_dump_thread_map_info(void);

#endif /*__IA_CSS_PIPELINE_H__*/
