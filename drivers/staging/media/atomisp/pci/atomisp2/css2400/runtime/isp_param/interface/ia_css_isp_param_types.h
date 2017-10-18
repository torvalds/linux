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

#ifndef _IA_CSS_ISP_PARAM_TYPES_H_
#define _IA_CSS_ISP_PARAM_TYPES_H_

#include "ia_css_types.h"
#include <platform_support.h>
#include <system_global.h>

/* Short hands */
#define IA_CSS_ISP_DMEM IA_CSS_ISP_DMEM0
#define IA_CSS_ISP_VMEM IA_CSS_ISP_VMEM0

/* The driver depends on this, to be removed later. */
#define IA_CSS_NUM_ISP_MEMORIES IA_CSS_NUM_MEMORIES

/* Explicit member numbering to avoid fish type checker bug */
enum ia_css_param_class {
	IA_CSS_PARAM_CLASS_PARAM  = 0,	/* Late binding parameters, like 3A */
	IA_CSS_PARAM_CLASS_CONFIG = 1,	/* Pipe config time parameters, like resolution */
	IA_CSS_PARAM_CLASS_STATE  = 2,  /* State parameters, like tnr buffer index */
#if 0 /* Not yet implemented */
	IA_CSS_PARAM_CLASS_FRAME  = 3,  /* Frame time parameters, like output buffer */
#endif
};
#define IA_CSS_NUM_PARAM_CLASSES (IA_CSS_PARAM_CLASS_STATE + 1)

/** ISP parameter descriptor */
struct ia_css_isp_parameter {
	uint32_t offset; /* Offset in isp_<mem>)parameters, etc. */
	uint32_t size;   /* Disabled if 0 */
};


/* Address/size of each parameter class in each isp memory, host memory pointers */
struct ia_css_isp_param_host_segments {
	struct ia_css_host_data params[IA_CSS_NUM_PARAM_CLASSES][IA_CSS_NUM_MEMORIES];
};

/* Address/size of each parameter class in each isp memory, css memory pointers */
struct ia_css_isp_param_css_segments {
	struct ia_css_data      params[IA_CSS_NUM_PARAM_CLASSES][IA_CSS_NUM_MEMORIES];
};

/* Address/size of each parameter class in each isp memory, isp memory pointers */
struct ia_css_isp_param_isp_segments {
	struct ia_css_isp_data  params[IA_CSS_NUM_PARAM_CLASSES][IA_CSS_NUM_MEMORIES];
};

/* Memory offsets in binary info */
struct ia_css_isp_param_memory_offsets {
	uint32_t offsets[IA_CSS_NUM_PARAM_CLASSES];  /**< offset wrt hdr in bytes */
};

/** Offsets for ISP kernel parameters per isp memory.
 * Only relevant for standard ISP binaries, not ACC or SP.
 */
union ia_css_all_memory_offsets {
	struct {
		CSS_ALIGN(struct ia_css_memory_offsets	      *param, 8);
		CSS_ALIGN(struct ia_css_config_memory_offsets *config, 8);
		CSS_ALIGN(struct ia_css_state_memory_offsets  *state, 8);
	} offsets;
	struct {
		CSS_ALIGN(void *ptr, 8);
	} array[IA_CSS_NUM_PARAM_CLASSES];
};

#define IA_CSS_DEFAULT_ISP_MEM_PARAMS \
		{ { { { 0, 0 } } } }

#define IA_CSS_DEFAULT_ISP_CSS_PARAMS \
		{ { { { 0, 0 } } } }

#define IA_CSS_DEFAULT_ISP_ISP_PARAMS \
		{ { { { 0, 0 } } } }

#endif /* _IA_CSS_ISP_PARAM_TYPES_H_ */

