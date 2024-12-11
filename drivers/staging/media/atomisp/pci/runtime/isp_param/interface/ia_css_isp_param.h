/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010 - 2015, Intel Corporation.
 */

#ifndef _IA_CSS_ISP_PARAM_H_
#define _IA_CSS_ISP_PARAM_H_

#include <ia_css_err.h>
#include "ia_css_isp_param_types.h"

/* Set functions for parameter memory descriptors */
void
ia_css_isp_param_set_mem_init(
    struct ia_css_isp_param_host_segments *mem_init,
    enum ia_css_param_class pclass,
    enum ia_css_isp_memories mem,
    char *address, size_t size);

void
ia_css_isp_param_set_css_mem_init(
    struct ia_css_isp_param_css_segments *mem_init,
    enum ia_css_param_class pclass,
    enum ia_css_isp_memories mem,
    ia_css_ptr address, size_t size);

void
ia_css_isp_param_set_isp_mem_init(
    struct ia_css_isp_param_isp_segments *mem_init,
    enum ia_css_param_class pclass,
    enum ia_css_isp_memories mem,
    u32 address, size_t size);

/* Get functions for parameter memory descriptors */
const struct ia_css_host_data *
ia_css_isp_param_get_mem_init(
    const struct ia_css_isp_param_host_segments *mem_init,
    enum ia_css_param_class pclass,
    enum ia_css_isp_memories mem);

const struct ia_css_data *
ia_css_isp_param_get_css_mem_init(
    const struct ia_css_isp_param_css_segments *mem_init,
    enum ia_css_param_class pclass,
    enum ia_css_isp_memories mem);

const struct ia_css_isp_data *
ia_css_isp_param_get_isp_mem_init(
    const struct ia_css_isp_param_isp_segments *mem_init,
    enum ia_css_param_class pclass,
    enum ia_css_isp_memories mem);

/* Initialize the memory interface sizes and addresses */
void
ia_css_init_memory_interface(
    struct ia_css_isp_param_css_segments *isp_mem_if,
    const struct ia_css_isp_param_host_segments *mem_params,
    const struct ia_css_isp_param_css_segments *css_params);

/* Allocate memory parameters */
int
ia_css_isp_param_allocate_isp_parameters(
    struct ia_css_isp_param_host_segments *mem_params,
    struct ia_css_isp_param_css_segments *css_params,
    const struct ia_css_isp_param_isp_segments *mem_initializers);

/* Destroy memory parameters */
void
ia_css_isp_param_destroy_isp_parameters(
    struct ia_css_isp_param_host_segments *mem_params,
    struct ia_css_isp_param_css_segments *css_params);

/* Load fw parameters */
void
ia_css_isp_param_load_fw_params(
    const char *fw,
    union ia_css_all_memory_offsets *mem_offsets,
    const struct ia_css_isp_param_memory_offsets *memory_offsets,
    bool init);

/* Copy host parameter images to ddr */
int
ia_css_isp_param_copy_isp_mem_if_to_ddr(
    struct ia_css_isp_param_css_segments *ddr,
    const struct ia_css_isp_param_host_segments *host,
    enum ia_css_param_class pclass);

/* Enable a pipeline by setting the control field in the isp dmem parameters */
void
ia_css_isp_param_enable_pipeline(
    const struct ia_css_isp_param_host_segments *mem_params);

#endif /* _IA_CSS_ISP_PARAM_H_ */
