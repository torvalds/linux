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

#ifndef __IA_CSS_ISYS_COMM_H
#define __IA_CSS_ISYS_COMM_H

#include <type_support.h>
#include <input_system.h>

#ifdef USE_INPUT_SYSTEM_VERSION_2401
#include <platform_support.h>		/* inline */
#include <input_system_global.h>
#include <ia_css_stream_public.h>	/* IA_CSS_STREAM_MAX_ISYS_STREAM_PER_CH */

#define SH_CSS_NODES_PER_THREAD		2
#define SH_CSS_MAX_ISYS_CHANNEL_NODES	(SH_CSS_MAX_SP_THREADS * SH_CSS_NODES_PER_THREAD)

/*
 * a) ia_css_isys_stream_h & ia_css_isys_stream_cfg_t come from host.
 *
 * b) Here it is better  to use actual structures for stream handle
 * instead of opaque handles. Otherwise, we need to have another
 * communication channel to interpret that opaque handle(this handle is
 * maintained by host and needs to be populated to sp for every stream open)
 * */
typedef virtual_input_system_stream_t		*ia_css_isys_stream_h;
typedef virtual_input_system_stream_cfg_t	ia_css_isys_stream_cfg_t;

/*
 * error check for ISYS APIs.
 * */
typedef bool ia_css_isys_error_t;

static inline uint32_t ia_css_isys_generate_stream_id(
	uint32_t	sp_thread_id,
	uint32_t	stream_id)
{
	return sp_thread_id * IA_CSS_STREAM_MAX_ISYS_STREAM_PER_CH + stream_id;
}

#endif  /* USE_INPUT_SYSTEM_VERSION_2401*/
#endif  /*_IA_CSS_ISYS_COMM_H */
