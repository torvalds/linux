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

#include "ia_css_mipi.h"
#include "sh_css_mipi.h"
#include <type_support.h>
#include "system_global.h"
#include "ia_css_err.h"
#include "ia_css_pipe.h"
#include "ia_css_stream_format.h"
#include "sh_css_stream_format.h"
#include "ia_css_stream_public.h"
#include "ia_css_frame_public.h"
#include "ia_css_input_port.h"
#include "ia_css_debug.h"
#include "sh_css_struct.h"
#include "sh_css_defs.h"
#include "sh_css_sp.h" /* sh_css_update_host2sp_mipi_frame sh_css_update_host2sp_num_mipi_frames ... */
#include "sw_event_global.h" /* IA_CSS_PSYS_SW_EVENT_MIPI_BUFFERS_READY */

#if defined(USE_INPUT_SYSTEM_VERSION_2) || defined(USE_INPUT_SYSTEM_VERSION_2401)
static uint32_t ref_count_mipi_allocation[N_CSI_PORTS]; /* Initialized in mipi_init */
#endif

enum ia_css_err
ia_css_mipi_frame_specify(const unsigned int size_mem_words,
				const bool contiguous)
{
	enum ia_css_err err = IA_CSS_SUCCESS;

	my_css.size_mem_words = size_mem_words;
	(void)contiguous;

	return err;
}

#ifdef ISP2401
#if defined(USE_INPUT_SYSTEM_VERSION_2) || defined(USE_INPUT_SYSTEM_VERSION_2401)
/*
 * Check if a source port or TPG/PRBS ID is valid
 */
static bool ia_css_mipi_is_source_port_valid(struct ia_css_pipe *pipe,
						unsigned int *pport)
{
	bool ret = true;
	unsigned int port = 0;
	unsigned int max_ports = 0;

	switch (pipe->stream->config.mode) {
	case IA_CSS_INPUT_MODE_BUFFERED_SENSOR:
		port = (unsigned int) pipe->stream->config.source.port.port;
		max_ports = N_CSI_PORTS;
		break;
	case IA_CSS_INPUT_MODE_TPG:
		port = (unsigned int) pipe->stream->config.source.tpg.id;
		max_ports = N_CSS_TPG_IDS;
		break;
	case IA_CSS_INPUT_MODE_PRBS:
		port = (unsigned int) pipe->stream->config.source.prbs.id;
		max_ports = N_CSS_PRBS_IDS;
		break;
	default:
		assert(false);
		ret = false;
		break;
	}

	if (ret) {
		assert(port < max_ports);

		if (port >= max_ports)
			ret = false;
	}

	*pport = port;

	return ret;
}
#endif

#endif
/* Assumptions:
 *	- A line is multiple of 4 bytes = 1 word.
 *	- Each frame has SOF and EOF (each 1 word).
 *	- Each line has format header and optionally SOL and EOL (each 1 word).
 *	- Odd and even lines of YUV420 format are different in bites per pixel size.
 *	- Custom size of embedded data.
 *  -- Interleaved frames are not taken into account.
 *  -- Lines are multiples of 8B, and not necessary of (custom 3B, or 7B
 *  etc.).
 * Result is given in DDR mem words, 32B or 256 bits
 */
enum ia_css_err
ia_css_mipi_frame_calculate_size(const unsigned int width,
				const unsigned int height,
				const enum ia_css_stream_format format,
				const bool hasSOLandEOL,
				const unsigned int embedded_data_size_words,
				unsigned int *size_mem_words)
{
	enum ia_css_err err = IA_CSS_SUCCESS;

	unsigned int bits_per_pixel = 0;
	unsigned int even_line_bytes = 0;
	unsigned int odd_line_bytes = 0;
	unsigned int words_per_odd_line = 0;
	unsigned int words_for_first_line = 0;
	unsigned int words_per_even_line = 0;
	unsigned int mem_words_per_even_line = 0;
	unsigned int mem_words_per_odd_line = 0;
	unsigned int mem_words_for_first_line = 0;
	unsigned int mem_words_for_EOF = 0;
	unsigned int mem_words = 0;
	unsigned int width_padded = width;

#if defined(USE_INPUT_SYSTEM_VERSION_2401)
	/* The changes will be reverted as soon as RAW
	 * Buffers are deployed by the 2401 Input System
	 * in the non-continuous use scenario.
	 */
	width_padded += (2 * ISP_VEC_NELEMS);
#endif

	IA_CSS_ENTER("padded_width=%d, height=%d, format=%d, hasSOLandEOL=%d, embedded_data_size_words=%d\n",
		     width_padded, height, format, hasSOLandEOL, embedded_data_size_words);

	switch (format) {
	case IA_CSS_STREAM_FORMAT_RAW_6:		/* 4p, 3B, 24bits */
		bits_per_pixel = 6;	break;
	case IA_CSS_STREAM_FORMAT_RAW_7:		/* 8p, 7B, 56bits */
		bits_per_pixel = 7;		break;
	case IA_CSS_STREAM_FORMAT_RAW_8:		/* 1p, 1B, 8bits */
	case IA_CSS_STREAM_FORMAT_BINARY_8:		/*  8bits, TODO: check. */
	case IA_CSS_STREAM_FORMAT_YUV420_8:		/* odd 2p, 2B, 16bits, even 2p, 4B, 32bits */
		bits_per_pixel = 8;		break;
	case IA_CSS_STREAM_FORMAT_YUV420_10:		/* odd 4p, 5B, 40bits, even 4p, 10B, 80bits */
	case IA_CSS_STREAM_FORMAT_RAW_10:		/* 4p, 5B, 40bits */
#if !defined(HAS_NO_PACKED_RAW_PIXELS)
		/* The changes will be reverted as soon as RAW
		 * Buffers are deployed by the 2401 Input System
		 * in the non-continuous use scenario.
		 */
		bits_per_pixel = 10;
#else
		bits_per_pixel = 16;
#endif
		break;
	case IA_CSS_STREAM_FORMAT_YUV420_8_LEGACY:	/* 2p, 3B, 24bits */
	case IA_CSS_STREAM_FORMAT_RAW_12:		/* 2p, 3B, 24bits */
		bits_per_pixel = 12;	break;
	case IA_CSS_STREAM_FORMAT_RAW_14:		/* 4p, 7B, 56bits */
		bits_per_pixel = 14;	break;
	case IA_CSS_STREAM_FORMAT_RGB_444:		/* 1p, 2B, 16bits */
	case IA_CSS_STREAM_FORMAT_RGB_555:		/* 1p, 2B, 16bits */
	case IA_CSS_STREAM_FORMAT_RGB_565:		/* 1p, 2B, 16bits */
	case IA_CSS_STREAM_FORMAT_YUV422_8:		/* 2p, 4B, 32bits */
		bits_per_pixel = 16;	break;
	case IA_CSS_STREAM_FORMAT_RGB_666:		/* 4p, 9B, 72bits */
		bits_per_pixel = 18;	break;
	case IA_CSS_STREAM_FORMAT_YUV422_10:		/* 2p, 5B, 40bits */
		bits_per_pixel = 20;	break;
	case IA_CSS_STREAM_FORMAT_RGB_888:		/* 1p, 3B, 24bits */
		bits_per_pixel = 24;	break;

	case IA_CSS_STREAM_FORMAT_YUV420_16:		/* Not supported */
	case IA_CSS_STREAM_FORMAT_YUV422_16:		/* Not supported */
	case IA_CSS_STREAM_FORMAT_RAW_16:		/* TODO: not specified in MIPI SPEC, check */
	default:
		return IA_CSS_ERR_INVALID_ARGUMENTS;
	}

	odd_line_bytes = (width_padded * bits_per_pixel + 7) >> 3; /* ceil ( bits per line / 8) */

	/* Even lines for YUV420 formats are double in bits_per_pixel. */
	if (format == IA_CSS_STREAM_FORMAT_YUV420_8
			|| format == IA_CSS_STREAM_FORMAT_YUV420_10
			|| format == IA_CSS_STREAM_FORMAT_YUV420_16) {
		even_line_bytes = (width_padded * 2 * bits_per_pixel + 7) >> 3; /* ceil ( bits per line / 8) */
	} else {
		even_line_bytes = odd_line_bytes;
	}

   /*  a frame represented in memory:  ()- optional; data - payload words.
	*  addr		0	1	2	3	4	5	6	7:
	*  first	SOF	(SOL)	PACK_H	data	data	data	data	data
	*		data	data	data	data	data	data	data	data
	*		...
	*		data	data	0	0	0	0	0	0
	*  second	(EOL)	(SOL)	PACK_H	data	data	data	data	data
	*		data	data	data	data	data	data	data	data
	*		...
	*		data	data	0	0	0	0	0	0
	*  ...
	*  last		(EOL)	EOF	0	0	0	0	0	0
	*
	*  Embedded lines are regular lines stored before the first and after
	*  payload lines.
	*/

	words_per_odd_line = (odd_line_bytes + 3) >> 2;
		/* ceil(odd_line_bytes/4); word = 4 bytes */
	words_per_even_line  = (even_line_bytes  + 3) >> 2;
	words_for_first_line = words_per_odd_line + 2 + (hasSOLandEOL ? 1 : 0);
		/* + SOF +packet header + optionally (SOL), but (EOL) is not in the first line */
	words_per_odd_line	+= (1 + (hasSOLandEOL ? 2 : 0));
		/* each non-first line has format header, and optionally (SOL) and (EOL). */
	words_per_even_line += (1 + (hasSOLandEOL ? 2 : 0));

	mem_words_per_odd_line	 = (words_per_odd_line + 7) >> 3;
		/* ceil(words_per_odd_line/8); mem_word = 32 bytes, 8 words */
	mem_words_for_first_line = (words_for_first_line + 7) >> 3;
	mem_words_per_even_line  = (words_per_even_line + 7) >> 3;
	mem_words_for_EOF        = 1; /* last line consisit of the optional (EOL) and EOF */

	mem_words = ((embedded_data_size_words + 7) >> 3) +
		mem_words_for_first_line +
				(((height + 1) >> 1) - 1) * mem_words_per_odd_line +
				/* ceil (height/2) - 1 (first line is calculated separatelly) */
				  (height      >> 1) * mem_words_per_even_line + /* floor(height/2) */
				mem_words_for_EOF;

	*size_mem_words = mem_words; /* ceil(words/8); mem word is 32B = 8words. */
	/* Check if the above is still needed. */

	IA_CSS_LEAVE_ERR(err);
	return err;
}

#if !defined(HAS_NO_INPUT_SYSTEM) && defined(USE_INPUT_SYSTEM_VERSION_2)
enum ia_css_err
ia_css_mipi_frame_enable_check_on_size(const enum mipi_port_id port,
				const unsigned int	size_mem_words)
{
	uint32_t idx;

	enum ia_css_err err = IA_CSS_ERR_RESOURCE_NOT_AVAILABLE;

	OP___assert(port < N_CSI_PORTS);
	OP___assert(size_mem_words != 0);

	for (idx = 0; idx < IA_CSS_MIPI_SIZE_CHECK_MAX_NOF_ENTRIES_PER_PORT &&
		my_css.mipi_sizes_for_check[port][idx] != 0;
		idx++) { /* do nothing */
	}
	if (idx < IA_CSS_MIPI_SIZE_CHECK_MAX_NOF_ENTRIES_PER_PORT) {
		my_css.mipi_sizes_for_check[port][idx] = size_mem_words;
		err = IA_CSS_SUCCESS;
	}

	return err;
}
#endif

void
mipi_init(void)
{
#if defined(USE_INPUT_SYSTEM_VERSION_2) || defined(USE_INPUT_SYSTEM_VERSION_2401)
	unsigned int i;

	for (i = 0; i < N_CSI_PORTS; i++)
		ref_count_mipi_allocation[i] = 0;
#endif
}

enum ia_css_err
calculate_mipi_buff_size(
		struct ia_css_stream_config *stream_cfg,
		unsigned int *size_mem_words)
{
#if !defined(USE_INPUT_SYSTEM_VERSION_2401)
	enum ia_css_err err = IA_CSS_ERR_INTERNAL_ERROR;
	(void)stream_cfg;
	(void)size_mem_words;
#else
	unsigned int width;
	unsigned int height;
	enum ia_css_stream_format format;
	bool pack_raw_pixels;

	unsigned int width_padded;
	unsigned int bits_per_pixel = 0;

	unsigned int even_line_bytes = 0;
	unsigned int odd_line_bytes = 0;

	unsigned int words_per_odd_line = 0;
	unsigned int words_per_even_line = 0;

	unsigned int mem_words_per_even_line = 0;
	unsigned int mem_words_per_odd_line = 0;

	unsigned int mem_words_per_buff_line = 0;
	unsigned int mem_words_per_buff = 0;
	enum ia_css_err err = IA_CSS_SUCCESS;

	/**
#ifndef ISP2401
	 * zhengjie.lu@intel.com
	 *
#endif
	 * NOTE
	 * - In the struct "ia_css_stream_config", there
	 *   are two members: "input_config" and "isys_config".
	 *   Both of them provide the same information, e.g.
	 *   input_res and format.
	 *
	 *   Question here is that: which one shall be used?
	 */
	width = stream_cfg->input_config.input_res.width;
	height = stream_cfg->input_config.input_res.height;
	format = stream_cfg->input_config.format;
	pack_raw_pixels = stream_cfg->pack_raw_pixels;
	/* end of NOTE */

	/**
#ifndef ISP2401
	 * zhengjie.lu@intel.com
	 *
#endif
	 * NOTE
	 * - The following code is derived from the
	 *   existing code "ia_css_mipi_frame_calculate_size()".
	 *
	 *   Question here is: why adding "2 * ISP_VEC_NELEMS"
	 *   to "width_padded", but not making "width_padded"
	 *   aligned with "2 * ISP_VEC_NELEMS"?
	 */
	/* The changes will be reverted as soon as RAW
	 * Buffers are deployed by the 2401 Input System
	 * in the non-continuous use scenario.
	 */
	width_padded = width + (2 * ISP_VEC_NELEMS);
	/* end of NOTE */

	IA_CSS_ENTER("padded_width=%d, height=%d, format=%d\n",
		     width_padded, height, format);

	bits_per_pixel = sh_css_stream_format_2_bits_per_subpixel(format);
	bits_per_pixel =
		(format == IA_CSS_STREAM_FORMAT_RAW_10 && pack_raw_pixels) ? bits_per_pixel : 16;
	if (bits_per_pixel == 0)
		return IA_CSS_ERR_INTERNAL_ERROR;

	odd_line_bytes = (width_padded * bits_per_pixel + 7) >> 3; /* ceil ( bits per line / 8) */

	/* Even lines for YUV420 formats are double in bits_per_pixel. */
	if (format == IA_CSS_STREAM_FORMAT_YUV420_8
		|| format == IA_CSS_STREAM_FORMAT_YUV420_10) {
		even_line_bytes = (width_padded * 2 * bits_per_pixel + 7) >> 3; /* ceil ( bits per line / 8) */
	} else {
		even_line_bytes = odd_line_bytes;
	}

	words_per_odd_line	 = (odd_line_bytes   + 3) >> 2;
		/* ceil(odd_line_bytes/4); word = 4 bytes */
	words_per_even_line  = (even_line_bytes  + 3) >> 2;

	mem_words_per_odd_line	 = (words_per_odd_line + 7) >> 3;
		/* ceil(words_per_odd_line/8); mem_word = 32 bytes, 8 words */
	mem_words_per_even_line  = (words_per_even_line + 7) >> 3;

	mem_words_per_buff_line =
		(mem_words_per_odd_line > mem_words_per_even_line) ? mem_words_per_odd_line : mem_words_per_even_line;
	mem_words_per_buff = mem_words_per_buff_line * height;

	*size_mem_words = mem_words_per_buff;

	IA_CSS_LEAVE_ERR(err);
#endif
	return err;
}

enum ia_css_err
allocate_mipi_frames(struct ia_css_pipe *pipe, struct ia_css_stream_info *info)
{
#if defined(USE_INPUT_SYSTEM_VERSION_2) || defined(USE_INPUT_SYSTEM_VERSION_2401)
	enum ia_css_err err = IA_CSS_ERR_INTERNAL_ERROR;
#ifndef ISP2401
	unsigned int port;
#else
	unsigned int port = 0;
#endif
	struct ia_css_frame_info mipi_intermediate_info;

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
		"allocate_mipi_frames(%p) enter:\n", pipe);

	assert(pipe != NULL);
	assert(pipe->stream != NULL);
	if ((pipe == NULL) || (pipe->stream == NULL)) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			"allocate_mipi_frames(%p) exit: pipe or stream is null.\n",
			pipe);
		return IA_CSS_ERR_INVALID_ARGUMENTS;
	}

#ifdef USE_INPUT_SYSTEM_VERSION_2401
	if (pipe->stream->config.online) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			"allocate_mipi_frames(%p) exit: no buffers needed for 2401 pipe mode.\n",
			pipe);
		return IA_CSS_SUCCESS;
	}

#endif
#ifndef ISP2401
	if (pipe->stream->config.mode != IA_CSS_INPUT_MODE_BUFFERED_SENSOR) {
#else
	if (!(pipe->stream->config.mode == IA_CSS_INPUT_MODE_BUFFERED_SENSOR ||
		pipe->stream->config.mode == IA_CSS_INPUT_MODE_TPG ||
		pipe->stream->config.mode == IA_CSS_INPUT_MODE_PRBS)) {
#endif
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			"allocate_mipi_frames(%p) exit: no buffers needed for pipe mode.\n",
			pipe);
		return IA_CSS_SUCCESS; /* AM TODO: Check  */
	}

#ifndef ISP2401
	port = (unsigned int) pipe->stream->config.source.port.port;
	assert(port < N_CSI_PORTS);
	if (port >= N_CSI_PORTS) {
#else
	if (!ia_css_mipi_is_source_port_valid(pipe, &port)) {
#endif
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			"allocate_mipi_frames(%p) exit: error: port is not correct (port=%d).\n",
			pipe, port);
		return IA_CSS_ERR_INTERNAL_ERROR;
	}

#ifdef USE_INPUT_SYSTEM_VERSION_2401
	err = calculate_mipi_buff_size(
			&(pipe->stream->config),
			&(my_css.mipi_frame_size[port]));
#endif

#if defined(USE_INPUT_SYSTEM_VERSION_2)
	if (ref_count_mipi_allocation[port] != 0) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			"allocate_mipi_frames(%p) exit: already allocated for this port (port=%d).\n",
			pipe, port);
		return IA_CSS_SUCCESS;
	}
#else
	/* 2401 system allows multiple streams to use same physical port. This is not
	 * true for 2400 system. Currently 2401 uses MIPI buffers as a temporary solution.
	 * TODO AM: Once that is changed (removed) this code should be removed as well.
	 * In that case only 2400 related code should remain.
	 */
	if (ref_count_mipi_allocation[port] != 0) {
		ref_count_mipi_allocation[port]++;
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			"allocate_mipi_frames(%p) leave: nothing to do, already allocated for this port (port=%d).\n",
			pipe, port);
		return IA_CSS_SUCCESS;
	}
#endif

	ref_count_mipi_allocation[port]++;

	/* TODO: Cleaning needed. */
	/* This code needs to modified to allocate the MIPI frames in the correct normal way
	  with an allocate from info, by justin */
	mipi_intermediate_info = pipe->pipe_settings.video.video_binary.internal_frame_info;
	mipi_intermediate_info.res.width = 0;
	mipi_intermediate_info.res.height = 0;
	/* To indicate it is not (yet) valid format. */
	mipi_intermediate_info.format = IA_CSS_FRAME_FORMAT_NUM;
	mipi_intermediate_info.padded_width = 0;
	mipi_intermediate_info.raw_bit_depth = 0;

	/* AM TODO: mipi frames number should come from stream struct. */
	my_css.num_mipi_frames[port] = NUM_MIPI_FRAMES_PER_STREAM;

	/* Incremental allocation (per stream), not for all streams at once. */
	{ /* limit the scope of i,j */
		unsigned i, j;
		for (i = 0; i < my_css.num_mipi_frames[port]; i++) {
			/* free previous frame */
			if (my_css.mipi_frames[port][i]) {
				ia_css_frame_free(my_css.mipi_frames[port][i]);
				my_css.mipi_frames[port][i] = NULL;
			}
			/* check if new frame is needed */
			if (i < my_css.num_mipi_frames[port]) {
				/* allocate new frame */
				err = ia_css_frame_allocate_with_buffer_size(
					&my_css.mipi_frames[port][i],
					my_css.mipi_frame_size[port] * HIVE_ISP_DDR_WORD_BYTES,
					false);
				if (err != IA_CSS_SUCCESS) {
					for (j = 0; j < i; j++) {
						if (my_css.mipi_frames[port][j]) {
							ia_css_frame_free(my_css.mipi_frames[port][j]);
							my_css.mipi_frames[port][j] = NULL;
						}
					}
					ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
						"allocate_mipi_frames(%p, %d) exit: error: allocation failed.\n",
						pipe, port);
					return err;
				}
			}
			if (info->metadata_info.size > 0) {
				/* free previous metadata buffer */
				if (my_css.mipi_metadata[port][i] != NULL) {
					ia_css_metadata_free(my_css.mipi_metadata[port][i]);
					my_css.mipi_metadata[port][i] = NULL;
				}
				/* check if need to allocate a new metadata buffer */
				if (i < my_css.num_mipi_frames[port]) {
					/* allocate new metadata buffer */
					my_css.mipi_metadata[port][i] = ia_css_metadata_allocate(&info->metadata_info);
					if (my_css.mipi_metadata[port][i] == NULL) {
						ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
							"allocate_mipi_metadata(%p, %d) failed.\n",
							pipe, port);
						return err;
					}
				}
			}
		}
	}
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
		"allocate_mipi_frames(%p) exit:\n", pipe);

	return err;
#else
	(void)pipe;
	(void)info;
	return IA_CSS_SUCCESS;
#endif
}

enum ia_css_err
free_mipi_frames(struct ia_css_pipe *pipe)
{
#if defined(USE_INPUT_SYSTEM_VERSION_2) || defined(USE_INPUT_SYSTEM_VERSION_2401)
	enum ia_css_err err = IA_CSS_ERR_INTERNAL_ERROR;
#ifndef ISP2401
	unsigned int port;
#else
	unsigned int port = 0;
#endif
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
		"free_mipi_frames(%p) enter:\n", pipe);

	/* assert(pipe != NULL); TEMP: TODO: Should be assert only. */
	if (pipe != NULL) {
		assert(pipe->stream != NULL);
		if ((pipe == NULL) || (pipe->stream == NULL)) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
				"free_mipi_frames(%p) exit: error: pipe or stream is null.\n",
				pipe);
			return IA_CSS_ERR_INVALID_ARGUMENTS;
		}

#ifndef ISP2401
		if (pipe->stream->config.mode != IA_CSS_INPUT_MODE_BUFFERED_SENSOR) {
#else
		if (!(pipe->stream->config.mode == IA_CSS_INPUT_MODE_BUFFERED_SENSOR ||
			pipe->stream->config.mode == IA_CSS_INPUT_MODE_TPG ||
			pipe->stream->config.mode == IA_CSS_INPUT_MODE_PRBS)) {
#endif
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
				"free_mipi_frames(%p) exit: error: wrong mode.\n",
				pipe);
			return err;
		}

#ifndef ISP2401
		port = (unsigned int) pipe->stream->config.source.port.port;
		assert(port < N_CSI_PORTS);
		if (port >= N_CSI_PORTS) {
#else
		if (!ia_css_mipi_is_source_port_valid(pipe, &port)) {
#endif
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
#ifndef ISP2401
				"free_mipi_frames(%p, %d) exit: error: pipe port is not correct.\n",
#else
				"free_mipi_frames(%p) exit: error: pipe port is not correct (port=%d).\n",
#endif
				pipe, port);
			return err;
		}
#ifdef ISP2401

#endif
		if (ref_count_mipi_allocation[port] > 0) {
#if defined(USE_INPUT_SYSTEM_VERSION_2)
			assert(ref_count_mipi_allocation[port] == 1);
			if (ref_count_mipi_allocation[port] != 1) {
				ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
					"free_mipi_frames(%p) exit: error: wrong ref_count (ref_count=%d).\n",
					pipe, ref_count_mipi_allocation[port]);
				return err;
			}
#endif

			ref_count_mipi_allocation[port]--;

			if (ref_count_mipi_allocation[port] == 0) {
				/* no streams are using this buffer, so free it */
				unsigned int i;
				for (i = 0; i < my_css.num_mipi_frames[port]; i++) {
					if (my_css.mipi_frames[port][i] != NULL) {
						ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
							"free_mipi_frames(port=%d, num=%d).\n", port, i);
						ia_css_frame_free(my_css.mipi_frames[port][i]);
						my_css.mipi_frames[port][i] = NULL;
					}
					if (my_css.mipi_metadata[port][i] != NULL) {
						ia_css_metadata_free(my_css.mipi_metadata[port][i]);
						my_css.mipi_metadata[port][i] = NULL;
					}
				}

				ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
					"free_mipi_frames(%p) exit (deallocated).\n", pipe);
			}
#if defined(USE_INPUT_SYSTEM_VERSION_2401)
			else {
				/* 2401 system allows multiple streams to use same physical port. This is not
				 * true for 2400 system. Currently 2401 uses MIPI buffers as a temporary solution.
				 * TODO AM: Once that is changed (removed) this code should be removed as well.
				 * In that case only 2400 related code should remain.
				 */
				ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
					"free_mipi_frames(%p) leave: nothing to do, other streams still use this port (port=%d).\n",
					pipe, port);
			}
#endif
		}
	} else { /* pipe ==NULL */
		/* AM TEMP: free-ing all mipi buffers just like a legacy code. */
		for (port = CSI_PORT0_ID; port < N_CSI_PORTS; port++) {
			unsigned int i;
			for (i = 0; i < my_css.num_mipi_frames[port]; i++) {
				if (my_css.mipi_frames[port][i] != NULL) {
					ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
						"free_mipi_frames(port=%d, num=%d).\n", port, i);
					ia_css_frame_free(my_css.mipi_frames[port][i]);
					my_css.mipi_frames[port][i] = NULL;
				}
				if (my_css.mipi_metadata[port][i] != NULL) {
					ia_css_metadata_free(my_css.mipi_metadata[port][i]);
					my_css.mipi_metadata[port][i] = NULL;
				}
			}
			ref_count_mipi_allocation[port] = 0;
		}
	}
#else
	(void)pipe;
#endif
	return IA_CSS_SUCCESS;
}

enum ia_css_err
send_mipi_frames(struct ia_css_pipe *pipe)
{
#if defined(USE_INPUT_SYSTEM_VERSION_2) || defined(USE_INPUT_SYSTEM_VERSION_2401)
	enum ia_css_err err = IA_CSS_ERR_INTERNAL_ERROR;
	unsigned int i;
#ifndef ISP2401
	unsigned int port;
#else
	unsigned int port = 0;
#endif

	IA_CSS_ENTER_PRIVATE("pipe=%p", pipe);

	assert(pipe != NULL);
	assert(pipe->stream != NULL);
	if (pipe == NULL || pipe->stream == NULL) {
		IA_CSS_ERROR("pipe or stream is null");
		return IA_CSS_ERR_INVALID_ARGUMENTS;
	}

	/* multi stream video needs mipi buffers */
	/* nothing to be done in other cases. */
#ifndef ISP2401
	if (pipe->stream->config.mode != IA_CSS_INPUT_MODE_BUFFERED_SENSOR) {
#else
	if (!(pipe->stream->config.mode == IA_CSS_INPUT_MODE_BUFFERED_SENSOR ||
		pipe->stream->config.mode == IA_CSS_INPUT_MODE_TPG ||
		pipe->stream->config.mode == IA_CSS_INPUT_MODE_PRBS)) {
#endif
		IA_CSS_LOG("nothing to be done for this mode");
		return IA_CSS_SUCCESS;
		/* TODO: AM: maybe this should be returning an error. */
	}

#ifndef ISP2401
	port = (unsigned int) pipe->stream->config.source.port.port;
	assert(port < N_CSI_PORTS);
	if (port >= N_CSI_PORTS) {
		IA_CSS_ERROR("invalid port specified (%d)", port);
#else
	if (!ia_css_mipi_is_source_port_valid(pipe, &port)) {
		IA_CSS_ERROR("send_mipi_frames(%p) exit: invalid port specified (port=%d).\n", pipe, port);
#endif
		return err;
	}

	/* Hand-over the SP-internal mipi buffers */
	for (i = 0; i < my_css.num_mipi_frames[port]; i++) {
		/* Need to include the ofset for port. */
		sh_css_update_host2sp_mipi_frame(port * NUM_MIPI_FRAMES_PER_STREAM + i,
			my_css.mipi_frames[port][i]);
		sh_css_update_host2sp_mipi_metadata(port * NUM_MIPI_FRAMES_PER_STREAM + i,
			my_css.mipi_metadata[port][i]);
	}
	sh_css_update_host2sp_num_mipi_frames(my_css.num_mipi_frames[port]);

	/**********************************
	 * Send an event to inform the SP
	 * that all MIPI frames are passed.
	 **********************************/
	if (!sh_css_sp_is_running()) {
		/* SP is not running. The queues are not valid */
		IA_CSS_ERROR("sp is not running");
		return err;
	}

	ia_css_bufq_enqueue_psys_event(
			IA_CSS_PSYS_SW_EVENT_MIPI_BUFFERS_READY,
			(uint8_t)port,
			(uint8_t)my_css.num_mipi_frames[port],
			0 /* not used */);
	IA_CSS_LEAVE_ERR_PRIVATE(IA_CSS_SUCCESS);
#else
	(void)pipe;
#endif
	return IA_CSS_SUCCESS;
}
