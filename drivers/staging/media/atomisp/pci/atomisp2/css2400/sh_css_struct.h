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

#ifndef __SH_CSS_STRUCT_H
#define __SH_CSS_STRUCT_H

/* This header files contains the definition of the
   sh_css struct and friends; locigally the file would
   probably be called sh_css.h after the pattern
   <type>.h but sh_css.h is the predecesssor of ia_css.h
   so this could cause confusion; hence the _struct
   in the filename
*/

#include <type_support.h>
#include <system_types.h>
#include "ia_css_pipeline.h"
#include "ia_css_pipe_public.h"
#include "ia_css_frame_public.h"
#include "ia_css_queue.h"
#include "ia_css_irq.h"

struct sh_css {
	struct ia_css_pipe            *active_pipes[IA_CSS_PIPELINE_NUM_MAX];
	/* All of the pipes created at any point of time. At this moment there can
	 * be no more than MAX_SP_THREADS of them because pipe_num is reused as SP
	 * thread_id to which a pipe's pipeline is associated. At a later point, if
	 * we support more pipe objects, we should add test code to test that
	 * possibility. Also, active_pipes[] should be able to hold only
	 * SH_CSS_MAX_SP_THREADS objects. Anything else is misleading. */
	struct ia_css_pipe            *all_pipes[IA_CSS_PIPELINE_NUM_MAX];
	void * (*malloc)(size_t bytes, bool zero_mem);
	void (*free)(void *ptr);
#ifdef ISP2401
	void * (*malloc_ex)(size_t bytes, bool zero_mem, const char *caller_func, int caller_line);
	void (*free_ex)(void *ptr, const char *caller_func, int caller_line);
#endif
	void (*flush)(struct ia_css_acc_fw *fw);
	bool                           check_system_idle;
#ifndef ISP2401
	bool stop_copy_preview;
#endif
	unsigned int                   num_cont_raw_frames;
#if defined(USE_INPUT_SYSTEM_VERSION_2) || defined(USE_INPUT_SYSTEM_VERSION_2401)
	unsigned int                   num_mipi_frames[N_CSI_PORTS];
	struct ia_css_frame           *mipi_frames[N_CSI_PORTS][NUM_MIPI_FRAMES_PER_STREAM];
	struct ia_css_metadata        *mipi_metadata[N_CSI_PORTS][NUM_MIPI_FRAMES_PER_STREAM];
	unsigned int                   mipi_sizes_for_check[N_CSI_PORTS][IA_CSS_MIPI_SIZE_CHECK_MAX_NOF_ENTRIES_PER_PORT];
	unsigned int                   mipi_frame_size[N_CSI_PORTS];
#endif
	hrt_vaddress                   sp_bin_addr;
	hrt_data                       page_table_base_index;
	unsigned int                   size_mem_words; /* \deprecated{Use ia_css_mipi_buffer_config instead.}*/
	enum ia_css_irq_type           irq_type;
	unsigned int                   pipe_counter;
	
	unsigned int		type;	/* 2400 or 2401 for now */
};

#define IPU_2400		1
#define IPU_2401		2

#define IS_2400()		(my_css.type == IPU_2400)
#define IS_2401()		(my_css.type == IPU_2401)

extern struct sh_css my_css;

#endif /* __SH_CSS_STRUCT_H */

