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

#ifndef _IA_CSS_DEBUG_H_
#define _IA_CSS_DEBUG_H_

/*! \file */

#include <type_support.h>
#include <stdarg.h>
#include "ia_css_types.h"
#include "ia_css_binary.h"
#include "ia_css_frame_public.h"
#include "ia_css_pipe_public.h"
#include "ia_css_stream_public.h"
#include "ia_css_metadata.h"
#include "sh_css_internal.h"
#ifdef ISP2401
#if defined(IS_ISP_2500_SYSTEM)
#include "ia_css_pipe.h"
#endif
#endif

/* available levels */
/*! Level for tracing errors */
#define IA_CSS_DEBUG_ERROR   1
/*! Level for tracing warnings */
#define IA_CSS_DEBUG_WARNING 3
/*! Level for tracing debug messages */
#define IA_CSS_DEBUG_VERBOSE   5
/*! Level for tracing trace messages a.o. ia_css public function calls */
#define IA_CSS_DEBUG_TRACE   6
/*! Level for tracing trace messages a.o. ia_css private function calls */
#define IA_CSS_DEBUG_TRACE_PRIVATE   7
/*! Level for tracing parameter messages e.g. in and out params of functions */
#define IA_CSS_DEBUG_PARAM   8
/*! Level for tracing info messages */
#define IA_CSS_DEBUG_INFO    9
/* Global variable which controls the verbosity levels of the debug tracing */
extern unsigned int ia_css_debug_trace_level;

/*! @brief Enum defining the different isp parameters to dump.
 *  Values can be combined to dump a combination of sets.
 */
enum ia_css_debug_enable_param_dump {
	IA_CSS_DEBUG_DUMP_FPN = 1 << 0, /**< FPN table */
	IA_CSS_DEBUG_DUMP_OB = 1 << 1,  /**< OB table */
	IA_CSS_DEBUG_DUMP_SC = 1 << 2,  /**< Shading table */
	IA_CSS_DEBUG_DUMP_WB = 1 << 3,  /**< White balance */
	IA_CSS_DEBUG_DUMP_DP = 1 << 4,  /**< Defect Pixel */
	IA_CSS_DEBUG_DUMP_BNR = 1 << 5,  /**< Bayer Noise Reductions */
	IA_CSS_DEBUG_DUMP_S3A = 1 << 6,  /**< 3A Statistics */
	IA_CSS_DEBUG_DUMP_DE = 1 << 7,  /**< De Mosaicing */
	IA_CSS_DEBUG_DUMP_YNR = 1 << 8,  /**< Luma Noise Reduction */
	IA_CSS_DEBUG_DUMP_CSC = 1 << 9,  /**< Color Space Conversion */
	IA_CSS_DEBUG_DUMP_GC = 1 << 10,  /**< Gamma Correction */
	IA_CSS_DEBUG_DUMP_TNR = 1 << 11,  /**< Temporal Noise Reduction */
	IA_CSS_DEBUG_DUMP_ANR = 1 << 12,  /**< Advanced Noise Reduction */
	IA_CSS_DEBUG_DUMP_CE = 1 << 13,  /**< Chroma Enhancement */
	IA_CSS_DEBUG_DUMP_ALL = 1 << 14  /**< Dump all device parameters */
};

#define IA_CSS_ERROR(fmt, ...) \
	ia_css_debug_dtrace(IA_CSS_DEBUG_ERROR, \
		"%s() %d: error: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)

#define IA_CSS_WARNING(fmt, ...) \
	ia_css_debug_dtrace(IA_CSS_DEBUG_WARNING, \
		"%s() %d: warning: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)

/* Logging macros for public functions (API functions) */
#define IA_CSS_ENTER(fmt, ...) \
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, \
		"%s(): enter: " fmt "\n", __func__, ##__VA_ARGS__)

/* Use this macro for small functions that do not call other functions. */
#define IA_CSS_ENTER_LEAVE(fmt, ...) \
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, \
		"%s(): enter: leave: " fmt "\n", __func__, ##__VA_ARGS__)

#define IA_CSS_LEAVE(fmt, ...) \
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, \
		"%s(): leave: " fmt "\n", __func__, ##__VA_ARGS__)

/* Shorthand for returning an enum ia_css_err return value */
#define IA_CSS_LEAVE_ERR(__err) \
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, \
		"%s() %d: leave: return_err=%d\n", __func__, __LINE__, __err)

/* Use this macro for logging other than enter/leave.
 * Note that this macro always uses the PRIVATE logging level.
 */
#define IA_CSS_LOG(fmt, ...) \
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE, \
		"%s(): " fmt "\n", __func__, ##__VA_ARGS__)

/* Logging macros for non-API functions. These have a lower trace level */
#define IA_CSS_ENTER_PRIVATE(fmt, ...) \
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE, \
		"%s(): enter: " fmt "\n", __func__, ##__VA_ARGS__)

#define IA_CSS_LEAVE_PRIVATE(fmt, ...) \
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE, \
		"%s(): leave: " fmt "\n", __func__, ##__VA_ARGS__)

/* Shorthand for returning an enum ia_css_err return value */
#define IA_CSS_LEAVE_ERR_PRIVATE(__err) \
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE, \
		"%s() %d: leave: return_err=%d\n", __func__, __LINE__, __err)

/* Use this macro for small functions that do not call other functions. */
#define IA_CSS_ENTER_LEAVE_PRIVATE(fmt, ...) \
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE, \
		"%s(): enter: leave: " fmt "\n", __func__, ##__VA_ARGS__)

/*! @brief Function for tracing to the provided printf function in the
 *	environment.
 * @param[in]	level		Level of the message.
 * @param[in]	fmt		printf like format string
 * @param[in]	args		arguments for the format string
 */
STORAGE_CLASS_INLINE void
ia_css_debug_vdtrace(unsigned int level, const char *fmt, va_list args)
{
	if (ia_css_debug_trace_level >= level)
		sh_css_vprint(fmt, args);
}

__printf(2, 3)
extern void ia_css_debug_dtrace(unsigned int level, const char *fmt, ...);

/*! @brief Dump sp thread's stack contents
 * SP thread's stack contents are set to 0xcafecafe. This function dumps the
 * stack to inspect if the stack's boundaries are compromised.
 * @return	None
 */
void ia_css_debug_dump_sp_stack_info(void);

/*! @brief Function to set the global dtrace verbosity level.
 * @param[in]	trace_level	Maximum level of the messages to be traced.
 * @return	None
 */
void ia_css_debug_set_dtrace_level(
	const unsigned int	trace_level);

/*! @brief Function to get the global dtrace verbosity level.
 * @return	global dtrace verbosity level
 */
unsigned int ia_css_debug_get_dtrace_level(void);

/*! @brief Dump input formatter state.
 * Dumps the input formatter state to tracing output.
 * @return	None
 */
void ia_css_debug_dump_if_state(void);

/*! @brief Dump isp hardware state.
 * Dumps the isp hardware state to tracing output.
 * @return	None
 */
void ia_css_debug_dump_isp_state(void);

/*! @brief Dump sp hardware state.
 * Dumps the sp hardware state to tracing output.
 * @return	None
 */
void ia_css_debug_dump_sp_state(void);

#ifdef ISP2401
/*! @brief Dump GAC hardware state.
 * Dumps the GAC ACB hardware registers. may be useful for
 * detecting a GAC which got hang.
 * @return	None
 */
void ia_css_debug_dump_gac_state(void);

#endif
/*! @brief Dump dma controller state.
 * Dumps the dma controller state to tracing output.
 * @return	None
 */
void ia_css_debug_dump_dma_state(void);

/*! @brief Dump internal sp software state.
 * Dumps the sp software state to tracing output.
 * @return	None
 */
void ia_css_debug_dump_sp_sw_debug_info(void);

/*! @brief Dump all related hardware state to the trace output
 * @param[in]  context	String to identify context in output.
 * @return	None
 */
void ia_css_debug_dump_debug_info(
	const char	*context);

#if SP_DEBUG != SP_DEBUG_NONE
void ia_css_debug_print_sp_debug_state(
	const struct sh_css_sp_debug_state *state);
#endif

/*! @brief Dump all related binary info data
 * @param[in]  bi	Binary info struct.
 * @return	None
 */
void ia_css_debug_binary_print(
	const struct ia_css_binary *bi);

void ia_css_debug_sp_dump_mipi_fifo_high_water(void);

/*! @brief Dump isp gdc fifo state to the trace output
 * Dumps the isp gdc fifo state to tracing output.
 * @return	None
 */
void ia_css_debug_dump_isp_gdc_fifo_state(void);

/*! @brief Dump dma isp fifo state
 * Dumps the dma isp fifo state to tracing output.
 * @return	None
 */
void ia_css_debug_dump_dma_isp_fifo_state(void);

/*! @brief Dump dma sp fifo state
 * Dumps the dma sp fifo state to tracing output.
 * @return	None
 */
void ia_css_debug_dump_dma_sp_fifo_state(void);

/*! \brief Dump pif A isp fifo state
 * Dumps the primary input formatter state to tracing output.
 * @return	None
 */
void ia_css_debug_dump_pif_a_isp_fifo_state(void);

/*! \brief Dump pif B isp fifo state
 * Dumps the primary input formatter state to tracing output.
 * \return	None
 */
void ia_css_debug_dump_pif_b_isp_fifo_state(void);

/*! @brief Dump stream-to-memory sp fifo state
 * Dumps the stream-to-memory block state to tracing output.
 * @return	None
 */
void ia_css_debug_dump_str2mem_sp_fifo_state(void);

/*! @brief Dump isp sp fifo state
 * Dumps the isp sp fifo state to tracing output.
 * @return	None
 */
void ia_css_debug_dump_isp_sp_fifo_state(void);

/*! @brief Dump all fifo state info to the output
 * Dumps all fifo state to tracing output.
 * @return	None
 */
void ia_css_debug_dump_all_fifo_state(void);

/*! @brief Dump the rx state to the output
 * Dumps the rx state to tracing output.
 * @return	None
 */
void ia_css_debug_dump_rx_state(void);

/*! @brief Dump the input system state to the output
 * Dumps the input system state to tracing output.
 * @return	None
 */
void ia_css_debug_dump_isys_state(void);

/*! @brief Dump the frame info to the trace output
 * Dumps the frame info to tracing output.
 * @param[in]	frame		pointer to struct ia_css_frame
 * @param[in]	descr		description output along with the frame info
 * @return	None
 */
void ia_css_debug_frame_print(
	const struct ia_css_frame	*frame,
	const char	*descr);

/*! @brief Function to enable sp sleep mode.
 * Function that enables sp sleep mode
 * @param[in]	mode		indicates when to put sp to sleep
 * @return	None
 */
void ia_css_debug_enable_sp_sleep_mode(enum ia_css_sp_sleep_mode mode);

/*! @brief Function to wake up sp when in sleep mode.
 * After sp has been put to sleep, use this function to let it continue
 * to run again.
 * @return	None
 */
void ia_css_debug_wake_up_sp(void);

/*! @brief Function to dump isp parameters.
 * Dump isp parameters to tracing output
 * @param[in]	stream		pointer to ia_css_stream struct
 * @param[in]	enable		flag indicating which parameters to dump.
 * @return	None
 */
void ia_css_debug_dump_isp_params(struct ia_css_stream *stream, unsigned int enable);

/*! @brief Function to dump some sp performance counters.
 * Dump sp performance counters, currently input system errors.
 * @return	None
 */
void ia_css_debug_dump_perf_counters(void);

#ifdef HAS_WATCHDOG_SP_THREAD_DEBUG
void sh_css_dump_thread_wait_info(void);
void sh_css_dump_pipe_stage_info(void);
void sh_css_dump_pipe_stripe_info(void);
#endif

void ia_css_debug_dump_isp_binary(void);

void sh_css_dump_sp_raw_copy_linecount(bool reduced);

/*! @brief Dump the resolution info to the trace output
 * Dumps the resolution info to the trace output.
 * @param[in]	res	pointer to struct ia_css_resolution
 * @param[in]	label	description of resolution output
 * @return	None
 */
void ia_css_debug_dump_resolution(
	const struct ia_css_resolution *res,
	const char *label);

/*! @brief Dump the frame info to the trace output
 * Dumps the frame info to the trace output.
 * @param[in]	info	pointer to struct ia_css_frame_info
 * @param[in]	label	description of frame_info output
 * @return	None
 */
void ia_css_debug_dump_frame_info(
	const struct ia_css_frame_info *info,
	const char *label);

/*! @brief Dump the capture config info to the trace output
 * Dumps the capture config info to the trace output.
 * @param[in]	config	pointer to struct ia_css_capture_config
 * @return	None
 */
void ia_css_debug_dump_capture_config(
	const struct ia_css_capture_config *config);

/*! @brief Dump the pipe extra config info to the trace output
 * Dumps the pipe extra config info to the trace output.
 * @param[in]	extra_config	pointer to struct ia_css_pipe_extra_config
 * @return	None
 */
void ia_css_debug_dump_pipe_extra_config(
	const struct ia_css_pipe_extra_config *extra_config);

/*! @brief Dump the pipe config info to the trace output
 * Dumps the pipe config info to the trace output.
 * @param[in]	config	pointer to struct ia_css_pipe_config
 * @return	None
 */
void ia_css_debug_dump_pipe_config(
	const struct ia_css_pipe_config *config);


/*! @brief Dump the stream config source info to the trace output
 * Dumps the stream config source info to the trace output.
 * @param[in]	config	pointer to struct ia_css_stream_config
 * @return	None
 */
void ia_css_debug_dump_stream_config_source(
	const struct ia_css_stream_config *config);

/*! @brief Dump the mipi buffer config info to the trace output
 * Dumps the mipi buffer config info to the trace output.
 * @param[in]	config	pointer to struct ia_css_mipi_buffer_config
 * @return	None
 */
void ia_css_debug_dump_mipi_buffer_config(
	const struct ia_css_mipi_buffer_config *config);

/*! @brief Dump the metadata config info to the trace output
 * Dumps the metadata config info to the trace output.
 * @param[in]	config	pointer to struct ia_css_metadata_config
 * @return	None
 */
void ia_css_debug_dump_metadata_config(
	const struct ia_css_metadata_config *config);

/*! @brief Dump the stream config info to the trace output
 * Dumps the stream config info to the trace output.
 * @param[in]	config		pointer to struct ia_css_stream_config
 * @param[in]	num_pipes	number of pipes for the stream
 * @return	None
 */
void ia_css_debug_dump_stream_config(
	const struct ia_css_stream_config *config,
	int num_pipes);

/*! @brief Dump the state of the SP tagger
 * Dumps the internal state of the SP tagger
 * @return	None
 */
void ia_css_debug_tagger_state(void);

/**
 * @brief Initialize the debug mode.
 *
 * WARNING:
 * This API should be called ONLY once in the debug mode.
 *
 * @return
 *	- true, if it is successful.
 *	- false, otherwise.
 */
bool ia_css_debug_mode_init(void);

/**
 * @brief Disable the DMA channel.
 *
 * @param[in]	dma_ID		The ID of the target DMA.
 * @param[in]	channel_id	The ID of the target DMA channel.
 * @param[in]	request_type	The type of the DMA request.
 *				For example:
 *				- "0" indicates the writing request.
 *				- "1" indicates the reading request.
 *
 * This is part of the DMA API -> dma.h
 *
 * @return
 *	- true, if it is successful.
 *	- false, otherwise.
 */
bool ia_css_debug_mode_disable_dma_channel(
	int dma_ID,
	int channel_id,
	int request_type);
/**
 * @brief Enable the DMA channel.
 *
 * @param[in]	dma_ID		The ID of the target DMA.
 * @param[in]	channel_id	The ID of the target DMA channel.
 * @param[in]	request_type	The type of the DMA request.
 *				For example:
 *				- "0" indicates the writing request.
 *				- "1" indicates the reading request.
 *
 * @return
 *	- true, if it is successful.
 *	- false, otherwise.
 */
bool ia_css_debug_mode_enable_dma_channel(
	int dma_ID,
	int channel_id,
	int request_type);

/**
 * @brief Dump tracer data.
 * [Currently support is only for SKC]
 *
 * @return
 *	- none.
 */
void ia_css_debug_dump_trace(void);

#ifdef ISP2401
/**
 * @brief Program counter dumping (in loop)
 *
 * @param[in]	id		The ID of the SP
 * @param[in]	num_of_dumps	The number of dumps
 *
 * @return
 *	- none
 */
void ia_css_debug_pc_dump(sp_ID_t id, unsigned int num_of_dumps);

#if defined(IS_ISP_2500_SYSTEM)
/*! @brief Dump all states for ISP hang case.
 * Dumps the ISP previous and current configurations
 * GACs status, SP0/1 statuses.
 *
 * @param[in]	pipe	The current pipe
 *
 * @return	None
 */
void ia_css_debug_dump_hang_status(
	struct ia_css_pipe *pipe);

/*! @brief External command handler
 * External command handler
 *
 * @return	None
 */
void ia_css_debug_ext_command_handler(void);

#endif
#endif

#endif /* _IA_CSS_DEBUG_H_ */
