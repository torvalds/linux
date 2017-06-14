#ifndef ISP2401
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
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
#else
/**
Support for Intel Camera Imaging ISP subsystem.
Copyright (c) 2010 - 2015, Intel Corporation.

This program is free software; you can redistribute it and/or modify it
under the terms and conditions of the GNU General Public License,
version 2, as published by the Free Software Foundation.

This program is distributed in the hope it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
more details.
*/
#endif

#include "memory_access.h"
#include "ia_css_pipeline.h"
#include "ia_css_isp_param.h"

/* Set functions for parameter memory descriptors */

void
ia_css_isp_param_set_mem_init(
	struct ia_css_isp_param_host_segments *mem_init,
	enum ia_css_param_class pclass,
	enum ia_css_isp_memories mem,
	char *address, size_t size)
{
	mem_init->params[pclass][mem].address = address;
	mem_init->params[pclass][mem].size = (uint32_t)size;
}

void
ia_css_isp_param_set_css_mem_init(
	struct ia_css_isp_param_css_segments *mem_init,
	enum ia_css_param_class pclass,
	enum ia_css_isp_memories mem,
	hrt_vaddress address, size_t size)
{
	mem_init->params[pclass][mem].address = address;
	mem_init->params[pclass][mem].size = (uint32_t)size;
}

void
ia_css_isp_param_set_isp_mem_init(
	struct ia_css_isp_param_isp_segments *mem_init,
	enum ia_css_param_class pclass,
	enum ia_css_isp_memories mem,
	uint32_t address, size_t size)
{
	mem_init->params[pclass][mem].address = address;
	mem_init->params[pclass][mem].size = (uint32_t)size;
}

/* Get functions for parameter memory descriptors */
const struct ia_css_host_data*
ia_css_isp_param_get_mem_init(
	const struct ia_css_isp_param_host_segments *mem_init,
	enum ia_css_param_class pclass,
	enum ia_css_isp_memories mem)
{
	return &mem_init->params[pclass][mem];
}

const struct ia_css_data*
ia_css_isp_param_get_css_mem_init(
	const struct ia_css_isp_param_css_segments *mem_init,
	enum ia_css_param_class pclass,
	enum ia_css_isp_memories mem)
{
	return &mem_init->params[pclass][mem];
}

const struct ia_css_isp_data*
ia_css_isp_param_get_isp_mem_init(
	const struct ia_css_isp_param_isp_segments *mem_init,
	enum ia_css_param_class pclass,
	enum ia_css_isp_memories mem)
{
	return &mem_init->params[pclass][mem];
}

void
ia_css_init_memory_interface(
	struct ia_css_isp_param_css_segments *isp_mem_if,
	const struct ia_css_isp_param_host_segments *mem_params,
	const struct ia_css_isp_param_css_segments *css_params)
{
	unsigned pclass, mem;
	for (pclass = 0; pclass < IA_CSS_NUM_PARAM_CLASSES; pclass++) {
		memset(isp_mem_if->params[pclass], 0, sizeof(isp_mem_if->params[pclass]));
		for (mem = 0; mem < IA_CSS_NUM_MEMORIES; mem++) {
			if (!mem_params->params[pclass][mem].address)
				continue;
			isp_mem_if->params[pclass][mem].size = mem_params->params[pclass][mem].size;
			if (pclass != IA_CSS_PARAM_CLASS_PARAM)
				isp_mem_if->params[pclass][mem].address = css_params->params[pclass][mem].address;
		}
	}
}

enum ia_css_err
ia_css_isp_param_allocate_isp_parameters(
	struct ia_css_isp_param_host_segments *mem_params,
	struct ia_css_isp_param_css_segments *css_params,
	const struct ia_css_isp_param_isp_segments *mem_initializers)
{
	enum ia_css_err err = IA_CSS_SUCCESS;
	unsigned mem, pclass;

	pclass = IA_CSS_PARAM_CLASS_PARAM;
	for (mem = 0; mem < IA_CSS_NUM_MEMORIES; mem++) {
		for (pclass = 0; pclass < IA_CSS_NUM_PARAM_CLASSES; pclass++) {
			uint32_t size = 0;
			if (mem_initializers)
				size = mem_initializers->params[pclass][mem].size;
			mem_params->params[pclass][mem].size = size;
			mem_params->params[pclass][mem].address = NULL;
			css_params->params[pclass][mem].size = size;
			css_params->params[pclass][mem].address = 0x0;
			if (size) {
				mem_params->params[pclass][mem].address = sh_css_calloc(1, size);
				if (!mem_params->params[pclass][mem].address) {
					err = IA_CSS_ERR_CANNOT_ALLOCATE_MEMORY;
					goto cleanup;
				}
				if (pclass != IA_CSS_PARAM_CLASS_PARAM) {
					css_params->params[pclass][mem].address = mmgr_malloc(size);
					if (!css_params->params[pclass][mem].address) {
						err = IA_CSS_ERR_CANNOT_ALLOCATE_MEMORY;
						goto cleanup;
					}
				}
			}
		}
	}
	return err;
cleanup:
	ia_css_isp_param_destroy_isp_parameters(mem_params, css_params);
	return err;
}

void
ia_css_isp_param_destroy_isp_parameters(
	struct ia_css_isp_param_host_segments *mem_params,
	struct ia_css_isp_param_css_segments *css_params)
{
	unsigned mem, pclass;

	for (mem = 0; mem < IA_CSS_NUM_MEMORIES; mem++) {
		for (pclass = 0; pclass < IA_CSS_NUM_PARAM_CLASSES; pclass++) {
			if (mem_params->params[pclass][mem].address)
				sh_css_free(mem_params->params[pclass][mem].address);
			if (css_params->params[pclass][mem].address)
				hmm_free(css_params->params[pclass][mem].address);
			mem_params->params[pclass][mem].address = NULL;
			css_params->params[pclass][mem].address = 0x0;
		}
	}
}

void
ia_css_isp_param_load_fw_params(
	const char *fw,
	union ia_css_all_memory_offsets *mem_offsets,
	const struct ia_css_isp_param_memory_offsets *memory_offsets,
	bool init)
{
	unsigned pclass;
	for (pclass = 0; pclass < IA_CSS_NUM_PARAM_CLASSES; pclass++) {
		mem_offsets->array[pclass].ptr = NULL;
		if (init)
			mem_offsets->array[pclass].ptr = (void *)(fw + memory_offsets->offsets[pclass]);
	}
}

enum ia_css_err
ia_css_isp_param_copy_isp_mem_if_to_ddr(
	struct ia_css_isp_param_css_segments *ddr,
	const struct ia_css_isp_param_host_segments *host,
	enum ia_css_param_class pclass)
{
	unsigned mem;

	for (mem = 0; mem < N_IA_CSS_ISP_MEMORIES; mem++) {
		size_t       size	  = host->params[pclass][mem].size;
		hrt_vaddress ddr_mem_ptr  = ddr->params[pclass][mem].address;
		char	    *host_mem_ptr = host->params[pclass][mem].address;
		if (size != ddr->params[pclass][mem].size)
			return IA_CSS_ERR_INTERNAL_ERROR;
		if (!size)
			continue;
		mmgr_store(ddr_mem_ptr, host_mem_ptr, size);
	}
	return IA_CSS_SUCCESS;
}

void
ia_css_isp_param_enable_pipeline(
	const struct ia_css_isp_param_host_segments *mem_params)
{
	/* By protocol b0 of the mandatory uint32_t first field of the
	   input parameter is a disable bit*/
	short dmem_offset = 0;

	if (mem_params->params[IA_CSS_PARAM_CLASS_PARAM][IA_CSS_ISP_DMEM0].size == 0)
		return;

	*(uint32_t *)&mem_params->params[IA_CSS_PARAM_CLASS_PARAM][IA_CSS_ISP_DMEM0].address[dmem_offset] = 0x0;
}


