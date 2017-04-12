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

#include "debug.h"
#include "memory_access.h"

#ifndef __INLINE_INPUT_SYSTEM__
#define __INLINE_INPUT_SYSTEM__
#endif
#ifndef __INLINE_IBUF_CTRL__
#define __INLINE_IBUF_CTRL__
#endif
#ifndef __INLINE_CSI_RX__
#define __INLINE_CSI_RX__
#endif
#ifndef __INLINE_PIXELGEN__
#define __INLINE_PIXELGEN__
#endif
#ifndef __INLINE_STREAM2MMIO__
#define __INLINE_STREAM2MMIO__
#endif

#include "ia_css_debug.h"
#include "ia_css_debug_pipe.h"
#include "ia_css_irq.h"
#include "ia_css_stream.h"
#include "ia_css_pipeline.h"
#include "ia_css_isp_param.h"
#include "sh_css_params.h"
#include "ia_css_bufq.h"
#ifdef ISP2401
#include "ia_css_queue.h"
#endif

#include "ia_css_isp_params.h"

#include "system_local.h"
#include "assert_support.h"
#include "print_support.h"
#include "string_support.h"
#ifdef ISP2401
#include "ia_css_system_ctrl.h"
#endif

#include "fifo_monitor.h"

#if !defined(HAS_NO_INPUT_FORMATTER)
#include "input_formatter.h"
#endif
#include "dma.h"
#include "irq.h"
#include "gp_device.h"
#include "sp.h"
#include "isp.h"
#include "type_support.h"
#include "math_support.h" /* CEIL_DIV */
#if defined(HAS_INPUT_FORMATTER_VERSION_2) || defined(USE_INPUT_SYSTEM_VERSION_2401)
#include "input_system.h"	/* input_formatter_reg_load */
#endif
#if defined(USE_INPUT_SYSTEM_VERSION_2) || defined(USE_INPUT_SYSTEM_VERSION_2401)
#include "ia_css_tagger_common.h"
#endif

#include "sh_css_internal.h"
#if !defined(HAS_NO_INPUT_SYSTEM)
#include "ia_css_isys.h"
#endif
#include "sh_css_sp.h"		/* sh_css_sp_get_debug_state() */

#include "css_trace.h"      /* tracer */

#include "device_access.h"	/* for ia_css_device_load_uint32 */

/* Include all kernel host interfaces for ISP1 */
#include "anr/anr_1.0/ia_css_anr.host.h"
#include "cnr/cnr_1.0/ia_css_cnr.host.h"
#include "csc/csc_1.0/ia_css_csc.host.h"
#include "de/de_1.0/ia_css_de.host.h"
#include "dp/dp_1.0/ia_css_dp.host.h"
#include "bnr/bnr_1.0/ia_css_bnr.host.h"
#include "fpn/fpn_1.0/ia_css_fpn.host.h"
#include "gc/gc_1.0/ia_css_gc.host.h"
#include "ob/ob_1.0/ia_css_ob.host.h"
#include "s3a/s3a_1.0/ia_css_s3a.host.h"
#include "sc/sc_1.0/ia_css_sc.host.h"
#include "tnr/tnr_1.0/ia_css_tnr.host.h"
#include "uds/uds_1.0/ia_css_uds_param.h"
#include "wb/wb_1.0/ia_css_wb.host.h"
#include "ynr/ynr_1.0/ia_css_ynr.host.h"

/* Include additional kernel host interfaces for ISP2 */
#include "aa/aa_2/ia_css_aa2.host.h"
#include "anr/anr_2/ia_css_anr2.host.h"
#include "cnr/cnr_2/ia_css_cnr2.host.h"
#include "de/de_2/ia_css_de2.host.h"
#include "gc/gc_2/ia_css_gc2.host.h"
#include "ynr/ynr_2/ia_css_ynr2.host.h"

/* Global variable to store the dtrace verbosity level */
unsigned int ia_css_debug_trace_level = IA_CSS_DEBUG_WARNING;

/* Assumes that IA_CSS_STREAM_FORMAT_BINARY_8 is last */
#define N_IA_CSS_STREAM_FORMAT (IA_CSS_STREAM_FORMAT_BINARY_8+1)

#define DPG_START "ia_css_debug_pipe_graph_dump_start "
#define DPG_END   " ia_css_debug_pipe_graph_dump_end\n"

#define ENABLE_LINE_MAX_LENGTH (25)

#ifdef ISP2401
#define DBG_EXT_CMD_TRACE_PNTS_DUMP (1 << 8)
#define DBG_EXT_CMD_PUB_CFG_DUMP (1 << 9)
#define DBG_EXT_CMD_GAC_REG_DUMP (1 << 10)
#define DBG_EXT_CMD_GAC_ACB_REG_DUMP (1 << 11)
#define DBG_EXT_CMD_FIFO_DUMP (1 << 12)
#define DBG_EXT_CMD_QUEUE_DUMP (1 << 13)
#define DBG_EXT_CMD_DMA_DUMP (1 << 14)
#define DBG_EXT_CMD_MASK 0xAB0000CD

#endif
/*
 * TODO:SH_CSS_MAX_SP_THREADS is not the max number of sp threads
 * future rework should fix this and remove the define MAX_THREAD_NUM
 */
#define MAX_THREAD_NUM (SH_CSS_MAX_SP_THREADS + SH_CSS_MAX_SP_INTERNAL_THREADS)

static struct pipe_graph_class {
	bool do_init;
	int height;
	int width;
	int eff_height;
	int eff_width;
	enum ia_css_stream_format stream_format;
} pg_inst = {true, 0, 0, 0, 0, N_IA_CSS_STREAM_FORMAT};

static const char * const queue_id_to_str[] = {
	/* [SH_CSS_QUEUE_A_ID]     =*/ "queue_A",
	/* [SH_CSS_QUEUE_B_ID]     =*/ "queue_B",
	/* [SH_CSS_QUEUE_C_ID]     =*/ "queue_C",
	/* [SH_CSS_QUEUE_D_ID]     =*/ "queue_D",
	/* [SH_CSS_QUEUE_E_ID]     =*/ "queue_E",
	/* [SH_CSS_QUEUE_F_ID]     =*/ "queue_F",
	/* [SH_CSS_QUEUE_G_ID]     =*/ "queue_G",
	/* [SH_CSS_QUEUE_H_ID]     =*/ "queue_H"
};

static const char * const pipe_id_to_str[] = {
	/* [IA_CSS_PIPE_ID_PREVIEW]   =*/ "preview",
	/* [IA_CSS_PIPE_ID_COPY]      =*/ "copy",
	/* [IA_CSS_PIPE_ID_VIDEO]     =*/ "video",
	/* [IA_CSS_PIPE_ID_CAPTURE]   =*/ "capture",
	/* [IA_CSS_PIPE_ID_YUVPP]     =*/ "yuvpp",
	/* [IA_CSS_PIPE_ID_ACC]       =*/ "accelerator"
};

static char dot_id_input_bin[SH_CSS_MAX_BINARY_NAME+10];
static char ring_buffer[200];

void ia_css_debug_dtrace(unsigned int level, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	ia_css_debug_vdtrace(level, fmt, ap);
	va_end(ap);
}

#if !defined(HRT_UNSCHED)
static void debug_dump_long_array_formatted(
	const sp_ID_t sp_id,
	hrt_address stack_sp_addr,
	unsigned stack_size)
{
	unsigned int i;
	uint32_t val;
	uint32_t addr = (uint32_t) stack_sp_addr;
	uint32_t stack_size_words = CEIL_DIV(stack_size, sizeof(uint32_t));

	/* When size is not multiple of four, last word is only relevant for
	 * remaining bytes */
	for (i = 0; i < stack_size_words; i++) {
		val = sp_dmem_load_uint32(sp_id, (hrt_address)addr);
		if ((i%8) == 0)
			ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE, "\n");

		ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE, "0x%08x ", val);
		addr += sizeof(uint32_t);
	}

	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE, "\n");
}

static void debug_dump_sp_stack_info(
	const sp_ID_t sp_id)
{
	const struct ia_css_fw_info *fw;
	unsigned int HIVE_ADDR_sp_threads_stack;
	unsigned int HIVE_ADDR_sp_threads_stack_size;
	uint32_t stack_sizes[MAX_THREAD_NUM];
	uint32_t stack_sp_addr[MAX_THREAD_NUM];
	unsigned int i;

	fw = &sh_css_sp_fw;

	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE, "sp_id(%u) stack info\n", sp_id);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
		"from objects stack_addr_offset:0x%x stack_size_offset:0x%x\n",
		fw->info.sp.threads_stack,
		fw->info.sp.threads_stack_size);

	HIVE_ADDR_sp_threads_stack = fw->info.sp.threads_stack;
	HIVE_ADDR_sp_threads_stack_size = fw->info.sp.threads_stack_size;

	if (fw->info.sp.threads_stack == 0 ||
		fw->info.sp.threads_stack_size == 0)
		return;

	(void) HIVE_ADDR_sp_threads_stack;
	(void) HIVE_ADDR_sp_threads_stack_size;

	sp_dmem_load(sp_id,
		(unsigned int)sp_address_of(sp_threads_stack),
		&stack_sp_addr, sizeof(stack_sp_addr));
	sp_dmem_load(sp_id,
		(unsigned int)sp_address_of(sp_threads_stack_size),
		&stack_sizes, sizeof(stack_sizes));

	for (i = 0 ; i < MAX_THREAD_NUM; i++) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			"thread: %u stack_addr: 0x%08x stack_size: %u\n",
			i, stack_sp_addr[i], stack_sizes[i]);
		debug_dump_long_array_formatted(sp_id, (hrt_address)stack_sp_addr[i],
			stack_sizes[i]);
	}
}

void ia_css_debug_dump_sp_stack_info(void)
{
	debug_dump_sp_stack_info(SP0_ID);
}
#else
/* Empty def for crun */
void ia_css_debug_dump_sp_stack_info(void)
{
}
#endif /* #if !HRT_UNSCHED */


void ia_css_debug_set_dtrace_level(const unsigned int trace_level)
{
	ia_css_debug_trace_level = trace_level;
	return;
}

unsigned int ia_css_debug_get_dtrace_level(void)
{
	return ia_css_debug_trace_level;
}

static const char *debug_stream_format2str(const enum ia_css_stream_format stream_format)
{
	switch (stream_format) {
	case IA_CSS_STREAM_FORMAT_YUV420_8_LEGACY:
		return "yuv420-8-legacy";
	case IA_CSS_STREAM_FORMAT_YUV420_8:
		return "yuv420-8";
	case IA_CSS_STREAM_FORMAT_YUV420_10:
		return "yuv420-10";
	case IA_CSS_STREAM_FORMAT_YUV420_16:
		return "yuv420-16";
	case IA_CSS_STREAM_FORMAT_YUV422_8:
		return "yuv422-8";
	case IA_CSS_STREAM_FORMAT_YUV422_10:
		return "yuv422-10";
	case IA_CSS_STREAM_FORMAT_YUV422_16:
		return "yuv422-16";
	case IA_CSS_STREAM_FORMAT_RGB_444:
		return "rgb444";
	case IA_CSS_STREAM_FORMAT_RGB_555:
		return "rgb555";
	case IA_CSS_STREAM_FORMAT_RGB_565:
		return "rgb565";
	case IA_CSS_STREAM_FORMAT_RGB_666:
		return "rgb666";
	case IA_CSS_STREAM_FORMAT_RGB_888:
		return "rgb888";
	case IA_CSS_STREAM_FORMAT_RAW_6:
		return "raw6";
	case IA_CSS_STREAM_FORMAT_RAW_7:
		return "raw7";
	case IA_CSS_STREAM_FORMAT_RAW_8:
		return "raw8";
	case IA_CSS_STREAM_FORMAT_RAW_10:
		return "raw10";
	case IA_CSS_STREAM_FORMAT_RAW_12:
		return "raw12";
	case IA_CSS_STREAM_FORMAT_RAW_14:
		return "raw14";
	case IA_CSS_STREAM_FORMAT_RAW_16:
		return "raw16";
	case IA_CSS_STREAM_FORMAT_BINARY_8:
		return "binary8";
	case IA_CSS_STREAM_FORMAT_GENERIC_SHORT1:
		return "generic-short1";
	case IA_CSS_STREAM_FORMAT_GENERIC_SHORT2:
		return "generic-short2";
	case IA_CSS_STREAM_FORMAT_GENERIC_SHORT3:
		return "generic-short3";
	case IA_CSS_STREAM_FORMAT_GENERIC_SHORT4:
		return "generic-short4";
	case IA_CSS_STREAM_FORMAT_GENERIC_SHORT5:
		return "generic-short5";
	case IA_CSS_STREAM_FORMAT_GENERIC_SHORT6:
		return "generic-short6";
	case IA_CSS_STREAM_FORMAT_GENERIC_SHORT7:
		return "generic-short7";
	case IA_CSS_STREAM_FORMAT_GENERIC_SHORT8:
		return "generic-short8";
	case IA_CSS_STREAM_FORMAT_YUV420_8_SHIFT:
		return "yuv420-8-shift";
	case IA_CSS_STREAM_FORMAT_YUV420_10_SHIFT:
		return "yuv420-10-shift";
	case IA_CSS_STREAM_FORMAT_EMBEDDED:
		return "embedded-8";
	case IA_CSS_STREAM_FORMAT_USER_DEF1:
		return "user-def-8-type-1";
	case IA_CSS_STREAM_FORMAT_USER_DEF2:
		return "user-def-8-type-2";
	case IA_CSS_STREAM_FORMAT_USER_DEF3:
		return "user-def-8-type-3";
	case IA_CSS_STREAM_FORMAT_USER_DEF4:
		return "user-def-8-type-4";
	case IA_CSS_STREAM_FORMAT_USER_DEF5:
		return "user-def-8-type-5";
	case IA_CSS_STREAM_FORMAT_USER_DEF6:
		return "user-def-8-type-6";
	case IA_CSS_STREAM_FORMAT_USER_DEF7:
		return "user-def-8-type-7";
	case IA_CSS_STREAM_FORMAT_USER_DEF8:
		return "user-def-8-type-8";

	default:
		assert(!"Unknown stream format");
		return "unknown-stream-format";
	}
};

static const char *debug_frame_format2str(const enum ia_css_frame_format frame_format)
{
	switch (frame_format) {

	case IA_CSS_FRAME_FORMAT_NV11:
		return "NV11";
	case IA_CSS_FRAME_FORMAT_NV12:
		return "NV12";
	case IA_CSS_FRAME_FORMAT_NV12_16:
		return "NV12_16";
	case IA_CSS_FRAME_FORMAT_NV12_TILEY:
		return "NV12_TILEY";
	case IA_CSS_FRAME_FORMAT_NV16:
		return "NV16";
	case IA_CSS_FRAME_FORMAT_NV21:
		return "NV21";
	case IA_CSS_FRAME_FORMAT_NV61:
		return "NV61";
	case IA_CSS_FRAME_FORMAT_YV12:
		return "YV12";
	case IA_CSS_FRAME_FORMAT_YV16:
		return "YV16";
	case IA_CSS_FRAME_FORMAT_YUV420:
		return "YUV420";
	case IA_CSS_FRAME_FORMAT_YUV420_16:
		return "YUV420_16";
	case IA_CSS_FRAME_FORMAT_YUV422:
		return "YUV422";
	case IA_CSS_FRAME_FORMAT_YUV422_16:
		return "YUV422_16";
	case IA_CSS_FRAME_FORMAT_UYVY:
		return "UYVY";
	case IA_CSS_FRAME_FORMAT_YUYV:
		return "YUYV";
	case IA_CSS_FRAME_FORMAT_YUV444:
		return "YUV444";
	case IA_CSS_FRAME_FORMAT_YUV_LINE:
		return "YUV_LINE";
	case IA_CSS_FRAME_FORMAT_RAW:
		return "RAW";
	case IA_CSS_FRAME_FORMAT_RGB565:
		return "RGB565";
	case IA_CSS_FRAME_FORMAT_PLANAR_RGB888:
		return "PLANAR_RGB888";
	case IA_CSS_FRAME_FORMAT_RGBA888:
		return "RGBA888";
	case IA_CSS_FRAME_FORMAT_QPLANE6:
		return "QPLANE6";
	case IA_CSS_FRAME_FORMAT_BINARY_8:
		return "BINARY_8";
	case IA_CSS_FRAME_FORMAT_MIPI:
		return "MIPI";
	case IA_CSS_FRAME_FORMAT_RAW_PACKED:
		return "RAW_PACKED";
	case IA_CSS_FRAME_FORMAT_CSI_MIPI_YUV420_8:
		return "CSI_MIPI_YUV420_8";
	case IA_CSS_FRAME_FORMAT_CSI_MIPI_LEGACY_YUV420_8:
		return "CSI_MIPI_LEGACY_YUV420_8";
	case IA_CSS_FRAME_FORMAT_CSI_MIPI_YUV420_10:
		return "CSI_MIPI_YUV420_10";

	default:
		assert(!"Unknown frame format");
		return "unknown-frame-format";
	}
}

static void debug_print_sp_state(const sp_state_t *state, const char *cell)
{
	assert(cell != NULL);
	assert(state != NULL);

	ia_css_debug_dtrace(2, "%s state:\n", cell);
	ia_css_debug_dtrace(2, "\t%-32s: 0x%X\n", "PC", state->pc);
	ia_css_debug_dtrace(2, "\t%-32s: 0x%X\n", "Status register",
			    state->status_register);
	ia_css_debug_dtrace(2, "\t%-32s: %d\n", "Is broken", state->is_broken);
	ia_css_debug_dtrace(2, "\t%-32s: %d\n", "Is idle", state->is_idle);
	ia_css_debug_dtrace(2, "\t%-32s: %d\n", "Is sleeping",
			    state->is_sleeping);
	ia_css_debug_dtrace(2, "\t%-32s: %d\n", "Is stalling",
			    state->is_stalling);
	return;
}

static void debug_print_isp_state(const isp_state_t *state, const char *cell)
{
	assert(state != NULL);
	assert(cell != NULL);

	ia_css_debug_dtrace(2, "%s state:\n", cell);
	ia_css_debug_dtrace(2, "\t%-32s: 0x%X\n", "PC", state->pc);
	ia_css_debug_dtrace(2, "\t%-32s: 0x%X\n", "Status register",
			    state->status_register);
	ia_css_debug_dtrace(2, "\t%-32s: %d\n", "Is broken", state->is_broken);
	ia_css_debug_dtrace(2, "\t%-32s: %d\n", "Is idle", state->is_idle);
	ia_css_debug_dtrace(2, "\t%-32s: %d\n", "Is sleeping",
			    state->is_sleeping);
	ia_css_debug_dtrace(2, "\t%-32s: %d\n", "Is stalling",
			    state->is_stalling);
	return;
}

void ia_css_debug_dump_isp_state(void)
{
	isp_state_t state;
	isp_stall_t stall;

	isp_get_state(ISP0_ID, &state, &stall);

	debug_print_isp_state(&state, "ISP");

	if (state.is_stalling) {
#if !defined(HAS_NO_INPUT_FORMATTER)
		ia_css_debug_dtrace(2, "\t%-32s: %d\n",
				    "[0] if_prim_a_FIFO stalled", stall.fifo0);
		ia_css_debug_dtrace(2, "\t%-32s: %d\n",
				    "[1] if_prim_b_FIFO stalled", stall.fifo1);
#endif
		ia_css_debug_dtrace(2, "\t%-32s: %d\n", "[2] dma_FIFO stalled",
				    stall.fifo2);
#if defined(HAS_ISP_2400_MAMOIADA) || defined(HAS_ISP_2401_MAMOIADA) || defined(IS_ISP_2500_SYSTEM)

		ia_css_debug_dtrace(2, "\t%-32s: %d\n", "[3] gdc0_FIFO stalled",
				    stall.fifo3);
#if !defined(IS_ISP_2500_SYSTEM)
		ia_css_debug_dtrace(2, "\t%-32s: %d\n", "[4] gdc1_FIFO stalled",
				    stall.fifo4);
		ia_css_debug_dtrace(2, "\t%-32s: %d\n", "[5] gpio_FIFO stalled",
				    stall.fifo5);
#endif
		ia_css_debug_dtrace(2, "\t%-32s: %d\n", "[6] sp_FIFO stalled",
				    stall.fifo6);
#else
#error "ia_css_debug: ISP cell must be one of {2400_MAMOIADA,, 2401_MAMOIADA, 2500_SKYCAM}"
#endif
		ia_css_debug_dtrace(2, "\t%-32s: %d\n",
				    "status & control stalled",
				    stall.stat_ctrl);
		ia_css_debug_dtrace(2, "\t%-32s: %d\n", "dmem stalled",
				    stall.dmem);
		ia_css_debug_dtrace(2, "\t%-32s: %d\n", "vmem stalled",
				    stall.vmem);
		ia_css_debug_dtrace(2, "\t%-32s: %d\n", "vamem1 stalled",
				    stall.vamem1);
		ia_css_debug_dtrace(2, "\t%-32s: %d\n", "vamem2 stalled",
				    stall.vamem2);
#if defined(HAS_ISP_2400_MAMOIADA) || defined(HAS_ISP_2401_MAMOIADA)
		ia_css_debug_dtrace(2, "\t%-32s: %d\n", "vamem3 stalled",
				    stall.vamem3);
		ia_css_debug_dtrace(2, "\t%-32s: %d\n", "hmem stalled",
				    stall.hmem);
		ia_css_debug_dtrace(2, "\t%-32s: %d\n", "pmem stalled",
				    stall.pmem);
#endif
	}
	return;
}

void ia_css_debug_dump_sp_state(void)
{
	sp_state_t state;
	sp_stall_t stall;
	sp_get_state(SP0_ID, &state, &stall);
	debug_print_sp_state(&state, "SP");
	if (state.is_stalling) {
#if defined(HAS_SP_2400) || defined(IS_ISP_2500_SYSTEM)
#if !defined(HAS_NO_INPUT_SYSTEM)
		ia_css_debug_dtrace(2, "\t%-32s: %d\n", "isys_FIFO stalled",
				    stall.fifo0);
		ia_css_debug_dtrace(2, "\t%-32s: %d\n", "if_sec_FIFO stalled",
				    stall.fifo1);
#endif
		ia_css_debug_dtrace(2, "\t%-32s: %d\n",
				    "str_to_mem_FIFO stalled", stall.fifo2);
		ia_css_debug_dtrace(2, "\t%-32s: %d\n", "dma_FIFO stalled",
				    stall.fifo3);
#if !defined(HAS_NO_INPUT_FORMATTER)
		ia_css_debug_dtrace(2, "\t%-32s: %d\n",
				    "if_prim_a_FIFO stalled", stall.fifo4);
#endif
		ia_css_debug_dtrace(2, "\t%-32s: %d\n", "isp_FIFO stalled",
				    stall.fifo5);
		ia_css_debug_dtrace(2, "\t%-32s: %d\n", "gp_FIFO stalled",
				    stall.fifo6);
#if !defined(HAS_NO_INPUT_FORMATTER)
		ia_css_debug_dtrace(2, "\t%-32s: %d\n",
				    "if_prim_b_FIFO stalled", stall.fifo7);
#endif
		ia_css_debug_dtrace(2, "\t%-32s: %d\n", "gdc0_FIFO stalled",
				    stall.fifo8);
#if !defined(IS_ISP_2500_SYSTEM)
		ia_css_debug_dtrace(2, "\t%-32s: %d\n", "gdc1_FIFO stalled",
				    stall.fifo9);
#endif
		ia_css_debug_dtrace(2, "\t%-32s: %d\n", "irq FIFO stalled",
				    stall.fifoa);
#else
#error "ia_css_debug: SP cell must be one of {SP2400, SP2500}"
#endif
		ia_css_debug_dtrace(2, "\t%-32s: %d\n", "dmem stalled",
				    stall.dmem);
		ia_css_debug_dtrace(2, "\t%-32s: %d\n",
				    "control master stalled",
				    stall.control_master);
		ia_css_debug_dtrace(2, "\t%-32s: %d\n",
				    "i-cache master stalled",
				    stall.icache_master);
	}
	ia_css_debug_dump_trace();
	return;
}

static void debug_print_fifo_channel_state(const fifo_channel_state_t *state,
					   const char *descr)
{
	assert(state != NULL);
	assert(descr != NULL);

	ia_css_debug_dtrace(2, "FIFO channel: %s\n", descr);
	ia_css_debug_dtrace(2, "\t%-32s: %d\n", "source valid",
			    state->src_valid);
	ia_css_debug_dtrace(2, "\t%-32s: %d\n", "fifo accept",
			    state->fifo_accept);
	ia_css_debug_dtrace(2, "\t%-32s: %d\n", "fifo valid",
			    state->fifo_valid);
	ia_css_debug_dtrace(2, "\t%-32s: %d\n", "sink accept",
			    state->sink_accept);
	return;
}

#if !defined(HAS_NO_INPUT_FORMATTER) && defined(USE_INPUT_SYSTEM_VERSION_2)
void ia_css_debug_dump_pif_a_isp_fifo_state(void)
{
	fifo_channel_state_t pif_to_isp, isp_to_pif;
	fifo_channel_get_state(FIFO_MONITOR0_ID,
			       FIFO_CHANNEL_IF0_TO_ISP0, &pif_to_isp);
	fifo_channel_get_state(FIFO_MONITOR0_ID,
			       FIFO_CHANNEL_ISP0_TO_IF0, &isp_to_pif);
	debug_print_fifo_channel_state(&pif_to_isp, "Primary IF A to ISP");
	debug_print_fifo_channel_state(&isp_to_pif, "ISP to Primary IF A");
}

void ia_css_debug_dump_pif_b_isp_fifo_state(void)
{
	fifo_channel_state_t pif_to_isp, isp_to_pif;
	fifo_channel_get_state(FIFO_MONITOR0_ID,
			       FIFO_CHANNEL_IF1_TO_ISP0, &pif_to_isp);
	fifo_channel_get_state(FIFO_MONITOR0_ID,
			       FIFO_CHANNEL_ISP0_TO_IF1, &isp_to_pif);
	debug_print_fifo_channel_state(&pif_to_isp, "Primary IF B to ISP");
	debug_print_fifo_channel_state(&isp_to_pif, "ISP to Primary IF B");
}

void ia_css_debug_dump_str2mem_sp_fifo_state(void)
{
	fifo_channel_state_t s2m_to_sp, sp_to_s2m;
	fifo_channel_get_state(FIFO_MONITOR0_ID,
			       FIFO_CHANNEL_STREAM2MEM0_TO_SP0, &s2m_to_sp);
	fifo_channel_get_state(FIFO_MONITOR0_ID,
			       FIFO_CHANNEL_SP0_TO_STREAM2MEM0, &sp_to_s2m);
	debug_print_fifo_channel_state(&s2m_to_sp, "Stream-to-memory to SP");
	debug_print_fifo_channel_state(&sp_to_s2m, "SP to stream-to-memory");
}

static void debug_print_if_state(input_formatter_state_t *state, const char *id)
{
	unsigned int val;

#if defined(HAS_INPUT_FORMATTER_VERSION_1)
	const char *st_reset = (state->reset ? "Active" : "Not active");
#endif
	const char *st_vsync_active_low =
	    (state->vsync_active_low ? "low" : "high");
	const char *st_hsync_active_low =
	    (state->hsync_active_low ? "low" : "high");

	const char *fsm_sync_status_str = "unknown";
	const char *fsm_crop_status_str = "unknown";
	const char *fsm_padding_status_str = "unknown";

	int st_stline = state->start_line;
	int st_stcol = state->start_column;
	int st_crpht = state->cropped_height;
	int st_crpwd = state->cropped_width;
	int st_verdcm = state->ver_decimation;
	int st_hordcm = state->hor_decimation;
	int st_ver_deinterleaving = state->ver_deinterleaving;
	int st_hor_deinterleaving = state->hor_deinterleaving;
	int st_leftpd = state->left_padding;
	int st_eoloff = state->eol_offset;
	int st_vmstartaddr = state->vmem_start_address;
	int st_vmendaddr = state->vmem_end_address;
	int st_vmincr = state->vmem_increment;
	int st_yuv420 = state->is_yuv420;
	int st_allow_fifo_overflow = state->allow_fifo_overflow;
	int st_block_fifo_when_no_req = state->block_fifo_when_no_req;

	assert(state != NULL);
	ia_css_debug_dtrace(2, "InputFormatter State (%s):\n", id);

	ia_css_debug_dtrace(2, "\tConfiguration:\n");

#if defined(HAS_INPUT_FORMATTER_VERSION_1)
	ia_css_debug_dtrace(2, "\t\t%-32s: %s\n", "Software reset", st_reset);
#endif
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "Start line", st_stline);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "Start column", st_stcol);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "Cropped height", st_crpht);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "Cropped width", st_crpwd);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "Ver decimation", st_verdcm);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "Hor decimation", st_hordcm);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "Ver deinterleaving", st_ver_deinterleaving);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "Hor deinterleaving", st_hor_deinterleaving);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "Left padding", st_leftpd);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "EOL offset (bytes)", st_eoloff);
	ia_css_debug_dtrace(2, "\t\t%-32s: 0x%06X\n",
			    "VMEM start address", st_vmstartaddr);
	ia_css_debug_dtrace(2, "\t\t%-32s: 0x%06X\n",
			    "VMEM end address", st_vmendaddr);
	ia_css_debug_dtrace(2, "\t\t%-32s: 0x%06X\n",
			    "VMEM increment", st_vmincr);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "YUV 420 format", st_yuv420);
	ia_css_debug_dtrace(2, "\t\t%-32s: Active %s\n",
			    "Vsync", st_vsync_active_low);
	ia_css_debug_dtrace(2, "\t\t%-32s: Active %s\n",
			    "Hsync", st_hsync_active_low);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "Allow FIFO overflow", st_allow_fifo_overflow);
/* Flag that tells whether the IF gives backpressure on frames */
/*
 * FYI, this is only on the frame request (indicate), when the IF has
 * synch'd on a frame it will always give back pressure
 */
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "Block when no request", st_block_fifo_when_no_req);

#if defined(HAS_INPUT_FORMATTER_VERSION_2)
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "IF_BLOCKED_FIFO_NO_REQ_ADDRESS",
			    input_formatter_reg_load(INPUT_FORMATTER0_ID,
			    HIVE_IF_BLOCK_FIFO_NO_REQ_ADDRESS)
	    );

	ia_css_debug_dtrace(2, "\t%-32s:\n", "InputSwitch State");

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "_REG_GP_IFMT_input_switch_lut_reg0",
			    gp_device_reg_load(GP_DEVICE0_ID,
			    _REG_GP_IFMT_input_switch_lut_reg0));

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "_REG_GP_IFMT_input_switch_lut_reg1",
			    gp_device_reg_load(GP_DEVICE0_ID,
				_REG_GP_IFMT_input_switch_lut_reg1));

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "_REG_GP_IFMT_input_switch_lut_reg2",
			    gp_device_reg_load(GP_DEVICE0_ID,
				_REG_GP_IFMT_input_switch_lut_reg2));

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "_REG_GP_IFMT_input_switch_lut_reg3",
			    gp_device_reg_load(GP_DEVICE0_ID,
				_REG_GP_IFMT_input_switch_lut_reg3));

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "_REG_GP_IFMT_input_switch_lut_reg4",
			    gp_device_reg_load(GP_DEVICE0_ID,
				_REG_GP_IFMT_input_switch_lut_reg4));

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "_REG_GP_IFMT_input_switch_lut_reg5",
			    gp_device_reg_load(GP_DEVICE0_ID,
				_REG_GP_IFMT_input_switch_lut_reg5));

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "_REG_GP_IFMT_input_switch_lut_reg6",
			    gp_device_reg_load(GP_DEVICE0_ID,
				_REG_GP_IFMT_input_switch_lut_reg6));

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "_REG_GP_IFMT_input_switch_lut_reg7",
			    gp_device_reg_load(GP_DEVICE0_ID,
				_REG_GP_IFMT_input_switch_lut_reg7));

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "_REG_GP_IFMT_input_switch_fsync_lut",
			    gp_device_reg_load(GP_DEVICE0_ID,
				_REG_GP_IFMT_input_switch_fsync_lut));

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "_REG_GP_IFMT_srst",
			    gp_device_reg_load(GP_DEVICE0_ID,
				_REG_GP_IFMT_srst));

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "_REG_GP_IFMT_slv_reg_srst",
			    gp_device_reg_load(GP_DEVICE0_ID,
				 _REG_GP_IFMT_slv_reg_srst));
#endif

	ia_css_debug_dtrace(2, "\tFSM Status:\n");

	val = state->fsm_sync_status;

	if (val > 7)
		fsm_sync_status_str = "ERROR";

	switch (val & 0x7) {
	case 0:
		fsm_sync_status_str = "idle";
		break;
	case 1:
		fsm_sync_status_str = "request frame";
		break;
	case 2:
		fsm_sync_status_str = "request lines";
		break;
	case 3:
		fsm_sync_status_str = "request vectors";
		break;
	case 4:
		fsm_sync_status_str = "send acknowledge";
		break;
	default:
		fsm_sync_status_str = "unknown";
		break;
	}

	ia_css_debug_dtrace(2, "\t\t%-32s: (0x%X: %s)\n",
			    "FSM Synchronization Status", val,
			    fsm_sync_status_str);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "FSM Synchronization Counter",
			    state->fsm_sync_counter);

	val = state->fsm_crop_status;

	if (val > 7)
		fsm_crop_status_str = "ERROR";

	switch (val & 0x7) {
	case 0:
		fsm_crop_status_str = "idle";
		break;
	case 1:
		fsm_crop_status_str = "wait line";
		break;
	case 2:
		fsm_crop_status_str = "crop line";
		break;
	case 3:
		fsm_crop_status_str = "crop pixel";
		break;
	case 4:
		fsm_crop_status_str = "pass pixel";
		break;
	case 5:
		fsm_crop_status_str = "pass line";
		break;
	case 6:
		fsm_crop_status_str = "lost line";
		break;
	default:
		fsm_crop_status_str = "unknown";
		break;
	}
	ia_css_debug_dtrace(2, "\t\t%-32s: (0x%X: %s)\n",
			    "FSM Crop Status", val, fsm_crop_status_str);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "FSM Crop Line Counter",
			    state->fsm_crop_line_counter);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "FSM Crop Pixel Counter",
			    state->fsm_crop_pixel_counter);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "FSM Deinterleaving idx buffer",
			    state->fsm_deinterleaving_index);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "FSM H decimation counter",
			    state->fsm_dec_h_counter);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "FSM V decimation counter",
			    state->fsm_dec_v_counter);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "FSM block V decimation counter",
			    state->fsm_dec_block_v_counter);

	val = state->fsm_padding_status;

	if (val > 7)
		fsm_padding_status_str = "ERROR";

	switch (val & 0x7) {
	case 0:
		fsm_padding_status_str = "idle";
		break;
	case 1:
		fsm_padding_status_str = "left pad";
		break;
	case 2:
		fsm_padding_status_str = "write";
		break;
	case 3:
		fsm_padding_status_str = "right pad";
		break;
	case 4:
		fsm_padding_status_str = "send end of line";
		break;
	default:
		fsm_padding_status_str = "unknown";
		break;
	}

	ia_css_debug_dtrace(2, "\t\t%-32s: (0x%X: %s)\n", "FSM Padding Status",
			    val, fsm_padding_status_str);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "FSM Padding element idx counter",
			    state->fsm_padding_elem_counter);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "FSM Vector support error",
			    state->fsm_vector_support_error);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "FSM Vector support buf full",
			    state->fsm_vector_buffer_full);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "FSM Vector support",
			    state->vector_support);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "Fifo sensor data lost",
			    state->sensor_data_lost);
	return;
}

static void debug_print_if_bin_state(input_formatter_bin_state_t *state)
{
	ia_css_debug_dtrace(2, "Stream-to-memory state:\n");
	ia_css_debug_dtrace(2, "\t%-32s: %d\n", "reset", state->reset);
	ia_css_debug_dtrace(2, "\t%-32s: %d\n", "input endianness",
			    state->input_endianness);
	ia_css_debug_dtrace(2, "\t%-32s: %d\n", "output endianness",
			    state->output_endianness);
	ia_css_debug_dtrace(2, "\t%-32s: %d\n", "bitswap", state->bitswap);
	ia_css_debug_dtrace(2, "\t%-32s: %d\n", "block_synch",
			    state->block_synch);
	ia_css_debug_dtrace(2, "\t%-32s: %d\n", "packet_synch",
			    state->packet_synch);
	ia_css_debug_dtrace(2, "\t%-32s: %d\n", "readpostwrite_sync",
			    state->readpostwrite_synch);
	ia_css_debug_dtrace(2, "\t%-32s: %d\n", "is_2ppc", state->is_2ppc);
	ia_css_debug_dtrace(2, "\t%-32s: %d\n", "en_status_update",
			    state->en_status_update);
}

void ia_css_debug_dump_if_state(void)
{
	input_formatter_state_t if_state;
	input_formatter_bin_state_t if_bin_state;

	input_formatter_get_state(INPUT_FORMATTER0_ID, &if_state);
	debug_print_if_state(&if_state, "Primary IF A");
	ia_css_debug_dump_pif_a_isp_fifo_state();

	input_formatter_get_state(INPUT_FORMATTER1_ID, &if_state);
	debug_print_if_state(&if_state, "Primary IF B");
	ia_css_debug_dump_pif_b_isp_fifo_state();

	input_formatter_bin_get_state(INPUT_FORMATTER3_ID, &if_bin_state);
	debug_print_if_bin_state(&if_bin_state);
	ia_css_debug_dump_str2mem_sp_fifo_state();
}
#endif

void ia_css_debug_dump_dma_state(void)
{
	/* note: the var below is made static as it is quite large;
	   if it is not static it ends up on the stack which could
	   cause issues for drivers
	*/
	static dma_state_t state;
	int i, ch_id;

	const char *fsm_cmd_st_lbl = "FSM Command flag state";
	const char *fsm_ctl_st_lbl = "FSM Control flag state";
	const char *fsm_ctl_state = NULL;
	const char *fsm_ctl_flag = NULL;
	const char *fsm_pack_st = NULL;
	const char *fsm_read_st = NULL;
	const char *fsm_write_st = NULL;
	char last_cmd_str[64];

	dma_get_state(DMA0_ID, &state);
	/* Print header for DMA dump status */
	ia_css_debug_dtrace(2, "DMA dump status:\n");

	/* Print FSM command flag state */
	if (state.fsm_command_idle)
		ia_css_debug_dtrace(2, "\t%-32s: %s\n", fsm_cmd_st_lbl, "IDLE");
	if (state.fsm_command_run)
		ia_css_debug_dtrace(2, "\t%-32s: %s\n", fsm_cmd_st_lbl, "RUN");
	if (state.fsm_command_stalling)
		ia_css_debug_dtrace(2, "\t%-32s: %s\n", fsm_cmd_st_lbl,
				    "STALL");
	if (state.fsm_command_error)
		ia_css_debug_dtrace(2, "\t%-32s: %s\n", fsm_cmd_st_lbl,
				    "ERROR");

	/* Print last command along with the channel */
	ch_id = state.last_command_channel;

	switch (state.last_command) {
	case DMA_COMMAND_READ:
		snprintf(last_cmd_str, 64,
			 "Read 2D Block [Channel: %d]", ch_id);
		break;
	case DMA_COMMAND_WRITE:
		snprintf(last_cmd_str, 64,
			 "Write 2D Block [Channel: %d]", ch_id);
		break;
	case DMA_COMMAND_SET_CHANNEL:
		snprintf(last_cmd_str, 64, "Set Channel [Channel: %d]", ch_id);
		break;
	case DMA_COMMAND_SET_PARAM:
		snprintf(last_cmd_str, 64,
			 "Set Param: %d [Channel: %d]",
			 state.last_command_param, ch_id);
		break;
	case DMA_COMMAND_READ_SPECIFIC:
		snprintf(last_cmd_str, 64,
			 "Read Specific 2D Block [Channel: %d]", ch_id);
		break;
	case DMA_COMMAND_WRITE_SPECIFIC:
		snprintf(last_cmd_str, 64,
			 "Write Specific 2D Block [Channel: %d]", ch_id);
		break;
	case DMA_COMMAND_INIT:
		snprintf(last_cmd_str, 64,
			 "Init 2D Block on Device A [Channel: %d]", ch_id);
		break;
	case DMA_COMMAND_INIT_SPECIFIC:
		snprintf(last_cmd_str, 64,
			 "Init Specific 2D Block [Channel: %d]", ch_id);
		break;
	case DMA_COMMAND_RST:
		snprintf(last_cmd_str, 64, "DMA SW Reset");
		break;
	case N_DMA_COMMANDS:
		snprintf(last_cmd_str, 64, "UNKNOWN");
		break;
	default:
		snprintf(last_cmd_str, 64,
		  "unknown [Channel: %d]", ch_id);
		break;
	}
	ia_css_debug_dtrace(2, "\t%-32s: (0x%X : %s)\n",
			    "last command received", state.last_command,
			    last_cmd_str);

	/* Print DMA registers */
	ia_css_debug_dtrace(2, "\t%-32s\n",
			    "DMA registers, connection group 0");
	ia_css_debug_dtrace(2, "\t\t%-32s: 0x%X\n", "Cmd Fifo Command",
			    state.current_command);
	ia_css_debug_dtrace(2, "\t\t%-32s: 0x%X\n", "Cmd Fifo Address A",
			    state.current_addr_a);
	ia_css_debug_dtrace(2, "\t\t%-32s: 0x%X\n", "Cmd Fifo Address B",
			    state.current_addr_b);

	if (state.fsm_ctrl_idle)
		fsm_ctl_flag = "IDLE";
	else if (state.fsm_ctrl_run)
		fsm_ctl_flag = "RUN";
	else if (state.fsm_ctrl_stalling)
		fsm_ctl_flag = "STAL";
	else if (state.fsm_ctrl_error)
		fsm_ctl_flag = "ERROR";
	else
		fsm_ctl_flag = "UNKNOWN";

	switch (state.fsm_ctrl_state) {
	case DMA_CTRL_STATE_IDLE:
		fsm_ctl_state = "Idle state";
		break;
	case DMA_CTRL_STATE_REQ_RCV:
		fsm_ctl_state = "Req Rcv state";
		break;
	case DMA_CTRL_STATE_RCV:
		fsm_ctl_state = "Rcv state";
		break;
	case DMA_CTRL_STATE_RCV_REQ:
		fsm_ctl_state = "Rcv Req state";
		break;
	case DMA_CTRL_STATE_INIT:
		fsm_ctl_state = "Init state";
		break;
	case N_DMA_CTRL_STATES:
		fsm_ctl_state = "Unknown";
		break;
	}

	ia_css_debug_dtrace(2, "\t\t%-32s: %s -> %s\n", fsm_ctl_st_lbl,
			    fsm_ctl_flag, fsm_ctl_state);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "FSM Ctrl source dev",
			    state.fsm_ctrl_source_dev);
	ia_css_debug_dtrace(2, "\t\t%-32s: 0x%X\n", "FSM Ctrl source addr",
			    state.fsm_ctrl_source_addr);
	ia_css_debug_dtrace(2, "\t\t%-32s: 0x%X\n", "FSM Ctrl source stride",
			    state.fsm_ctrl_source_stride);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "FSM Ctrl source width",
			    state.fsm_ctrl_source_width);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "FSM Ctrl source height",
			    state.fsm_ctrl_source_height);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "FSM Ctrl pack source dev",
			    state.fsm_ctrl_pack_source_dev);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "FSM Ctrl pack dest dev",
			    state.fsm_ctrl_pack_dest_dev);
	ia_css_debug_dtrace(2, "\t\t%-32s: 0x%X\n", "FSM Ctrl dest addr",
			    state.fsm_ctrl_dest_addr);
	ia_css_debug_dtrace(2, "\t\t%-32s: 0x%X\n", "FSM Ctrl dest stride",
			    state.fsm_ctrl_dest_stride);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "FSM Ctrl pack source width",
			    state.fsm_ctrl_pack_source_width);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "FSM Ctrl pack dest height",
			    state.fsm_ctrl_pack_dest_height);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "FSM Ctrl pack dest width",
			    state.fsm_ctrl_pack_dest_width);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "FSM Ctrl pack source elems",
			    state.fsm_ctrl_pack_source_elems);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "FSM Ctrl pack dest elems",
			    state.fsm_ctrl_pack_dest_elems);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "FSM Ctrl pack extension",
			    state.fsm_ctrl_pack_extension);

	if (state.pack_idle)
		fsm_pack_st = "IDLE";
	if (state.pack_run)
		fsm_pack_st = "RUN";
	if (state.pack_stalling)
		fsm_pack_st = "STALL";
	if (state.pack_error)
		fsm_pack_st = "ERROR";

	ia_css_debug_dtrace(2, "\t\t%-32s: %s\n", "FSM Pack flag state",
			    fsm_pack_st);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "FSM Pack cnt height",
			    state.pack_cnt_height);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "FSM Pack src cnt width",
			    state.pack_src_cnt_width);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "FSM Pack dest cnt width",
			    state.pack_dest_cnt_width);

	if (state.read_state == DMA_RW_STATE_IDLE)
		fsm_read_st = "Idle state";
	if (state.read_state == DMA_RW_STATE_REQ)
		fsm_read_st = "Req state";
	if (state.read_state == DMA_RW_STATE_NEXT_LINE)
		fsm_read_st = "Next line";
	if (state.read_state == DMA_RW_STATE_UNLOCK_CHANNEL)
		fsm_read_st = "Unlock channel";

	ia_css_debug_dtrace(2, "\t\t%-32s: %s\n", "FSM Read state",
			    fsm_read_st);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "FSM Read cnt height",
			    state.read_cnt_height);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "FSM Read cnt width",
			    state.read_cnt_width);

	if (state.write_state == DMA_RW_STATE_IDLE)
		fsm_write_st = "Idle state";
	if (state.write_state == DMA_RW_STATE_REQ)
		fsm_write_st = "Req state";
	if (state.write_state == DMA_RW_STATE_NEXT_LINE)
		fsm_write_st = "Next line";
	if (state.write_state == DMA_RW_STATE_UNLOCK_CHANNEL)
		fsm_write_st = "Unlock channel";

	ia_css_debug_dtrace(2, "\t\t%-32s: %s\n", "FSM Write state",
			    fsm_write_st);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "FSM Write height",
			    state.write_height);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "FSM Write width",
			    state.write_width);

	for (i = 0; i < HIVE_ISP_NUM_DMA_CONNS; i++) {
		dma_port_state_t *port = &(state.port_states[i]);
		ia_css_debug_dtrace(2, "\tDMA device interface %d\n", i);
		ia_css_debug_dtrace(2, "\t\tDMA internal side state\n");
		ia_css_debug_dtrace(2,
				    "\t\t\tCS:%d - We_n:%d - Run:%d - Ack:%d\n",
				    port->req_cs, port->req_we_n, port->req_run,
				    port->req_ack);
		ia_css_debug_dtrace(2, "\t\tMaster Output side state\n");
		ia_css_debug_dtrace(2,
				    "\t\t\tCS:%d - We_n:%d - Run:%d - Ack:%d\n",
				    port->send_cs, port->send_we_n,
				    port->send_run, port->send_ack);
		ia_css_debug_dtrace(2, "\t\tFifo state\n");
		if (port->fifo_state == DMA_FIFO_STATE_WILL_BE_FULL)
			ia_css_debug_dtrace(2, "\t\t\tFiFo will be full\n");
		else if (port->fifo_state == DMA_FIFO_STATE_FULL)
			ia_css_debug_dtrace(2, "\t\t\tFifo Full\n");
		else if (port->fifo_state == DMA_FIFO_STATE_EMPTY)
			ia_css_debug_dtrace(2, "\t\t\tFifo Empty\n");
		else
			ia_css_debug_dtrace(2, "\t\t\tFifo state unknown\n");

		ia_css_debug_dtrace(2, "\t\tFifo counter %d\n\n",
				    port->fifo_counter);
	}

	for (i = 0; i < HIVE_DMA_NUM_CHANNELS; i++) {
		dma_channel_state_t *ch = &(state.channel_states[i]);
		ia_css_debug_dtrace(2, "\t%-32s: %d\n", "DMA channel register",
				    i);
		ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "Connection",
				    ch->connection);
		ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "Sign extend",
				    ch->sign_extend);
		ia_css_debug_dtrace(2, "\t\t%-32s: 0x%X\n", "Stride Dev A",
				    ch->stride_a);
		ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "Elems Dev A",
				    ch->elems_a);
		ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "Cropping Dev A",
				    ch->cropping_a);
		ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "Width Dev A",
				    ch->width_a);
		ia_css_debug_dtrace(2, "\t\t%-32s: 0x%X\n", "Stride Dev B",
				    ch->stride_b);
		ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "Elems Dev B",
				    ch->elems_b);
		ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "Cropping Dev B",
				    ch->cropping_b);
		ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "Width Dev B",
				    ch->width_b);
		ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "Height", ch->height);
	}
	ia_css_debug_dtrace(2, "\n");
	return;
}

void ia_css_debug_dump_dma_sp_fifo_state(void)
{
	fifo_channel_state_t dma_to_sp, sp_to_dma;
	fifo_channel_get_state(FIFO_MONITOR0_ID,
			       FIFO_CHANNEL_DMA0_TO_SP0, &dma_to_sp);
	fifo_channel_get_state(FIFO_MONITOR0_ID,
			       FIFO_CHANNEL_SP0_TO_DMA0, &sp_to_dma);
	debug_print_fifo_channel_state(&dma_to_sp, "DMA to SP");
	debug_print_fifo_channel_state(&sp_to_dma, "SP to DMA");
	return;
}

void ia_css_debug_dump_dma_isp_fifo_state(void)
{
	fifo_channel_state_t dma_to_isp, isp_to_dma;
	fifo_channel_get_state(FIFO_MONITOR0_ID,
			       FIFO_CHANNEL_DMA0_TO_ISP0, &dma_to_isp);
	fifo_channel_get_state(FIFO_MONITOR0_ID,
			       FIFO_CHANNEL_ISP0_TO_DMA0, &isp_to_dma);
	debug_print_fifo_channel_state(&dma_to_isp, "DMA to ISP");
	debug_print_fifo_channel_state(&isp_to_dma, "ISP to DMA");
	return;
}

void ia_css_debug_dump_isp_sp_fifo_state(void)
{
	fifo_channel_state_t sp_to_isp, isp_to_sp;
	fifo_channel_get_state(FIFO_MONITOR0_ID,
			       FIFO_CHANNEL_SP0_TO_ISP0, &sp_to_isp);
	fifo_channel_get_state(FIFO_MONITOR0_ID,
			       FIFO_CHANNEL_ISP0_TO_SP0, &isp_to_sp);
	debug_print_fifo_channel_state(&sp_to_isp, "SP to ISP");
	debug_print_fifo_channel_state(&isp_to_sp, "ISP to SP");
	return;
}

void ia_css_debug_dump_isp_gdc_fifo_state(void)
{
	fifo_channel_state_t gdc_to_isp, isp_to_gdc;

	fifo_channel_get_state(FIFO_MONITOR0_ID,
			       FIFO_CHANNEL_GDC0_TO_ISP0, &gdc_to_isp);
	fifo_channel_get_state(FIFO_MONITOR0_ID,
			       FIFO_CHANNEL_ISP0_TO_GDC0, &isp_to_gdc);
	debug_print_fifo_channel_state(&gdc_to_isp, "GDC to ISP");
	debug_print_fifo_channel_state(&isp_to_gdc, "ISP to GDC");
	return;
}

void ia_css_debug_dump_all_fifo_state(void)
{
	int i;
	fifo_monitor_state_t state;
	fifo_monitor_get_state(FIFO_MONITOR0_ID, &state);

	for (i = 0; i < N_FIFO_CHANNEL; i++)
		debug_print_fifo_channel_state(&(state.fifo_channels[i]),
					       "squepfstqkt");
	return;
}

static void debug_binary_info_print(const struct ia_css_binary_xinfo *info)
{
	assert(info != NULL);
	ia_css_debug_dtrace(2, "id = %d\n", info->sp.id);
	ia_css_debug_dtrace(2, "mode = %d\n", info->sp.pipeline.mode);
	ia_css_debug_dtrace(2, "max_input_width = %d\n", info->sp.input.max_width);
	ia_css_debug_dtrace(2, "min_output_width = %d\n",
			    info->sp.output.min_width);
	ia_css_debug_dtrace(2, "max_output_width = %d\n",
			    info->sp.output.max_width);
	ia_css_debug_dtrace(2, "top_cropping = %d\n", info->sp.pipeline.top_cropping);
	ia_css_debug_dtrace(2, "left_cropping = %d\n", info->sp.pipeline.left_cropping);
	ia_css_debug_dtrace(2, "xmem_addr = %d\n", info->xmem_addr);
	ia_css_debug_dtrace(2, "enable_vf_veceven = %d\n",
			    info->sp.enable.vf_veceven);
	ia_css_debug_dtrace(2, "enable_dis = %d\n", info->sp.enable.dis);
	ia_css_debug_dtrace(2, "enable_uds = %d\n", info->sp.enable.uds);
	ia_css_debug_dtrace(2, "enable ds = %d\n", info->sp.enable.ds);
	ia_css_debug_dtrace(2, "s3atbl_use_dmem = %d\n", info->sp.s3a.s3atbl_use_dmem);
	return;
}

void ia_css_debug_binary_print(const struct ia_css_binary *bi)
{
	unsigned int i;
	debug_binary_info_print(bi->info);
	ia_css_debug_dtrace(2,
			    "input:  %dx%d, format = %d, padded width = %d\n",
			    bi->in_frame_info.res.width,
			    bi->in_frame_info.res.height,
			    bi->in_frame_info.format,
			    bi->in_frame_info.padded_width);
	ia_css_debug_dtrace(2,
			    "internal :%dx%d, format = %d, padded width = %d\n",
			    bi->internal_frame_info.res.width,
			    bi->internal_frame_info.res.height,
			    bi->internal_frame_info.format,
			    bi->internal_frame_info.padded_width);
	for (i = 0; i < IA_CSS_BINARY_MAX_OUTPUT_PORTS; i++) {
		if (bi->out_frame_info[i].res.width != 0) {
			ia_css_debug_dtrace(2,
				    "out%d:    %dx%d, format = %d, padded width = %d\n",
					i,
				    bi->out_frame_info[i].res.width,
				    bi->out_frame_info[i].res.height,
				    bi->out_frame_info[i].format,
				    bi->out_frame_info[i].padded_width);
		}
	}
	ia_css_debug_dtrace(2,
			    "vf out: %dx%d, format = %d, padded width = %d\n",
			    bi->vf_frame_info.res.width,
			    bi->vf_frame_info.res.height,
			    bi->vf_frame_info.format,
			    bi->vf_frame_info.padded_width);
	ia_css_debug_dtrace(2, "online = %d\n", bi->online);
	ia_css_debug_dtrace(2, "input_buf_vectors = %d\n",
			    bi->input_buf_vectors);
	ia_css_debug_dtrace(2, "deci_factor_log2 = %d\n", bi->deci_factor_log2);
	ia_css_debug_dtrace(2, "vf_downscale_log2 = %d\n",
			    bi->vf_downscale_log2);
	ia_css_debug_dtrace(2, "dis_deci_factor_log2 = %d\n",
			    bi->dis.deci_factor_log2);
	ia_css_debug_dtrace(2, "dis hor coef num = %d\n",
			    bi->dis.coef.pad.width);
	ia_css_debug_dtrace(2, "dis ver coef num = %d\n",
			    bi->dis.coef.pad.height);
	ia_css_debug_dtrace(2, "dis hor proj num = %d\n",
			    bi->dis.proj.pad.height);
	ia_css_debug_dtrace(2, "sctbl_width_per_color = %d\n",
			    bi->sctbl_width_per_color);
	ia_css_debug_dtrace(2, "s3atbl_width = %d\n", bi->s3atbl_width);
	ia_css_debug_dtrace(2, "s3atbl_height = %d\n", bi->s3atbl_height);
	return;
}

void ia_css_debug_frame_print(const struct ia_css_frame *frame,
			      const char *descr)
{
	char *data = NULL;

	assert(frame != NULL);
	assert(descr != NULL);

	data = (char *)HOST_ADDRESS(frame->data);
	ia_css_debug_dtrace(2, "frame %s (%p):\n", descr, frame);
	ia_css_debug_dtrace(2, "  resolution    = %dx%d\n",
			    frame->info.res.width, frame->info.res.height);
	ia_css_debug_dtrace(2, "  padded width  = %d\n",
			    frame->info.padded_width);
	ia_css_debug_dtrace(2, "  format        = %d\n", frame->info.format);
	ia_css_debug_dtrace(2, "  is contiguous = %s\n",
			    frame->contiguous ? "yes" : "no");
	switch (frame->info.format) {
	case IA_CSS_FRAME_FORMAT_NV12:
	case IA_CSS_FRAME_FORMAT_NV16:
	case IA_CSS_FRAME_FORMAT_NV21:
	case IA_CSS_FRAME_FORMAT_NV61:
		ia_css_debug_dtrace(2, "  Y = %p\n",
				    data + frame->planes.nv.y.offset);
		ia_css_debug_dtrace(2, "  UV = %p\n",
				    data + frame->planes.nv.uv.offset);
		break;
	case IA_CSS_FRAME_FORMAT_YUYV:
	case IA_CSS_FRAME_FORMAT_UYVY:
	case IA_CSS_FRAME_FORMAT_CSI_MIPI_YUV420_8:
	case IA_CSS_FRAME_FORMAT_CSI_MIPI_LEGACY_YUV420_8:
	case IA_CSS_FRAME_FORMAT_YUV_LINE:
		ia_css_debug_dtrace(2, "  YUYV = %p\n",
				    data + frame->planes.yuyv.offset);
		break;
	case IA_CSS_FRAME_FORMAT_YUV420:
	case IA_CSS_FRAME_FORMAT_YUV422:
	case IA_CSS_FRAME_FORMAT_YUV444:
	case IA_CSS_FRAME_FORMAT_YV12:
	case IA_CSS_FRAME_FORMAT_YV16:
	case IA_CSS_FRAME_FORMAT_YUV420_16:
	case IA_CSS_FRAME_FORMAT_YUV422_16:
		ia_css_debug_dtrace(2, "  Y = %p\n",
				    data + frame->planes.yuv.y.offset);
		ia_css_debug_dtrace(2, "  U = %p\n",
				    data + frame->planes.yuv.u.offset);
		ia_css_debug_dtrace(2, "  V = %p\n",
				    data + frame->planes.yuv.v.offset);
		break;
	case IA_CSS_FRAME_FORMAT_RAW_PACKED:
		ia_css_debug_dtrace(2, "  RAW PACKED = %p\n",
				    data + frame->planes.raw.offset);
		break;
	case IA_CSS_FRAME_FORMAT_RAW:
		ia_css_debug_dtrace(2, "  RAW = %p\n",
				    data + frame->planes.raw.offset);
		break;
	case IA_CSS_FRAME_FORMAT_RGBA888:
	case IA_CSS_FRAME_FORMAT_RGB565:
		ia_css_debug_dtrace(2, "  RGB = %p\n",
				    data + frame->planes.rgb.offset);
		break;
	case IA_CSS_FRAME_FORMAT_QPLANE6:
		ia_css_debug_dtrace(2, "  R    = %p\n",
				    data + frame->planes.plane6.r.offset);
		ia_css_debug_dtrace(2, "  RatB = %p\n",
				    data + frame->planes.plane6.r_at_b.offset);
		ia_css_debug_dtrace(2, "  Gr   = %p\n",
				    data + frame->planes.plane6.gr.offset);
		ia_css_debug_dtrace(2, "  Gb   = %p\n",
				    data + frame->planes.plane6.gb.offset);
		ia_css_debug_dtrace(2, "  B    = %p\n",
				    data + frame->planes.plane6.b.offset);
		ia_css_debug_dtrace(2, "  BatR = %p\n",
				    data + frame->planes.plane6.b_at_r.offset);
		break;
	case IA_CSS_FRAME_FORMAT_BINARY_8:
		ia_css_debug_dtrace(2, "  Binary data = %p\n",
				    data + frame->planes.binary.data.offset);
		break;
	default:
		ia_css_debug_dtrace(2, "  unknown frame type\n");
		break;
	}
	return;
}

#if SP_DEBUG != SP_DEBUG_NONE

void ia_css_debug_print_sp_debug_state(const struct sh_css_sp_debug_state
				       *state)
{

#endif

#if SP_DEBUG == SP_DEBUG_DUMP

	assert(state != NULL);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "current SP software counter: %d\n",
			    state->debug[0]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "empty output buffer queue head: 0x%x\n",
			    state->debug[1]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "empty output buffer queue tail: 0x%x\n",
			    state->debug[2]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "empty s3a buffer queue head: 0x%x\n",
			    state->debug[3]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "empty s3a buffer queue tail: 0x%x\n",
			    state->debug[4]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "full output buffer queue head: 0x%x\n",
			    state->debug[5]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "full output buffer queue tail: 0x%x\n",
			    state->debug[6]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "full s3a buffer queue head: 0x%x\n",
			    state->debug[7]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "full s3a buffer queue tail: 0x%x\n",
			    state->debug[8]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE, "event queue head: 0x%x\n",
			    state->debug[9]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE, "event queue tail: 0x%x\n",
			    state->debug[10]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "num of stages of current pipeline: 0x%x\n",
			    state->debug[11]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE, "DDR address of stage 1: 0x%x\n",
			    state->debug[12]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE, "DDR address of stage 2: 0x%x\n",
			    state->debug[13]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "current stage out_vf buffer idx: 0x%x\n",
			    state->debug[14]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "current stage output buffer idx: 0x%x\n",
			    state->debug[15]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "current stage s3a buffer idx: 0x%x\n",
			    state->debug[16]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "first char of current stage name: 0x%x\n",
			    state->debug[17]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE, "current SP thread id: 0x%x\n",
			    state->debug[18]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "empty output buffer address 1: 0x%x\n",
			    state->debug[19]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "empty output buffer address 2: 0x%x\n",
			    state->debug[20]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "empty out_vf buffer address 1: 0x%x\n",
			    state->debug[21]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "empty out_vf buffer address 2: 0x%x\n",
			    state->debug[22]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "empty s3a_hi buffer address 1: 0x%x\n",
			    state->debug[23]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "empty s3a_hi buffer address 2: 0x%x\n",
			    state->debug[24]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "empty s3a_lo buffer address 1: 0x%x\n",
			    state->debug[25]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "empty s3a_lo buffer address 2: 0x%x\n",
			    state->debug[26]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "empty dis_hor buffer address 1: 0x%x\n",
			    state->debug[27]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "empty dis_hor buffer address 2: 0x%x\n",
			    state->debug[28]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "empty dis_ver buffer address 1: 0x%x\n",
			    state->debug[29]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "empty dis_ver buffer address 2: 0x%x\n",
			    state->debug[30]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "empty param buffer address: 0x%x\n",
			    state->debug[31]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "first incorrect frame address: 0x%x\n",
			    state->debug[32]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "first incorrect frame container address: 0x%x\n",
			    state->debug[33]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "first incorrect frame container payload: 0x%x\n",
			    state->debug[34]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "first incorrect s3a_hi address: 0x%x\n",
			    state->debug[35]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "first incorrect s3a_hi container address: 0x%x\n",
			    state->debug[36]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "first incorrect s3a_hi container payload: 0x%x\n",
			    state->debug[37]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "first incorrect s3a_lo address: 0x%x\n",
			    state->debug[38]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "first incorrect s3a_lo container address: 0x%x\n",
			    state->debug[39]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "first incorrect s3a_lo container payload: 0x%x\n",
			    state->debug[40]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "number of calling flash start function: 0x%x\n",
			    state->debug[41]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "number of calling flash close function: 0x%x\n",
			    state->debug[42]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE, "number of flashed frame: 0x%x\n",
			    state->debug[43]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE, "flash in use flag: 0x%x\n",
			    state->debug[44]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "number of update frame flashed flag: 0x%x\n",
			    state->debug[46]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			    "number of active threads: 0x%x\n",
			    state->debug[45]);

#elif SP_DEBUG == SP_DEBUG_COPY

	/* Remember last_index because we only want to print new entries */
	static int last_index;
	int sp_index = state->index;
	int n;

	assert(state != NULL);
	if (sp_index < last_index) {
		/* SP has been reset */
		last_index = 0;
	}

	if (last_index == 0) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
				    "copy-trace init: sp_dbg_if_start_line=%d, "
				    "sp_dbg_if_start_column=%d, "
				    "sp_dbg_if_cropped_height=%d, "
				    "sp_debg_if_cropped_width=%d\n",
				    state->if_start_line,
				    state->if_start_column,
				    state->if_cropped_height,
				    state->if_cropped_width);
	}

	if ((last_index + SH_CSS_SP_DBG_TRACE_DEPTH) < sp_index) {
		/* last index can be multiple rounds behind */
		/* while trace size is only SH_CSS_SP_DBG_TRACE_DEPTH */
		last_index = sp_index - SH_CSS_SP_DBG_TRACE_DEPTH;
	}

	for (n = last_index; n < sp_index; n++) {
		int i = n % SH_CSS_SP_DBG_TRACE_DEPTH;
		if (state->trace[i].frame != 0) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
					    "copy-trace: frame=%d, line=%d, "
					    "pixel_distance=%d, "
					    "mipi_used_dword=%d, "
					    "sp_index=%d\n",
					    state->trace[i].frame,
					    state->trace[i].line,
					    state->trace[i].pixel_distance,
					    state->trace[i].mipi_used_dword,
					    state->trace[i].sp_index);
		}
	}

	last_index = sp_index;

#elif SP_DEBUG == SP_DEBUG_TRACE

/**
 * This is just an example how TRACE_FILE_ID (see ia_css_debug.sp.h) will
 * me mapped on the file name string.
 *
 * Adjust this to your trace case!
 */
	static char const * const id2filename[8] = {
		"param_buffer.sp.c | tagger.sp.c | pipe_data.sp.c",
		"isp_init.sp.c",
		"sp_raw_copy.hive.c",
		"dma_configure.sp.c",
		"sp.hive.c",
		"event_proxy_sp.hive.c",
		"circular_buffer.sp.c",
		"frame_buffer.sp.c"
	};

#if 1
	/* Example SH_CSS_SP_DBG_NR_OF_TRACES==1 */
	/* Adjust this to your trace case */
	static char const *trace_name[SH_CSS_SP_DBG_NR_OF_TRACES] = {
		"default"
	};
#else
	/* Example SH_CSS_SP_DBG_NR_OF_TRACES==4 */
	/* Adjust this to your trace case */
	static char const *trace_name[SH_CSS_SP_DBG_NR_OF_TRACES] = {
		"copy", "preview/video", "capture", "acceleration"
	};
#endif

	/* Remember host_index_last because we only want to print new entries */
	static int host_index_last[SH_CSS_SP_DBG_NR_OF_TRACES] = { 0 };
	int t, n;

	assert(state != NULL);

	for (t = 0; t < SH_CSS_SP_DBG_NR_OF_TRACES; t++) {
		int sp_index_last = state->index_last[t];

		if (sp_index_last < host_index_last[t]) {
			/* SP has been reset */
			host_index_last[t] = 0;
		}

		if ((host_index_last[t] + SH_CSS_SP_DBG_TRACE_DEPTH) <
		    sp_index_last) {
			/* last index can be multiple rounds behind */
			/* while trace size is only SH_CSS_SP_DBG_TRACE_DEPTH */
			ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
					    "Warning: trace %s has gap of %d "
					    "traces\n",
					    trace_name[t],
					    (sp_index_last -
					     (host_index_last[t] +
					      SH_CSS_SP_DBG_TRACE_DEPTH)));

			host_index_last[t] =
			    sp_index_last - SH_CSS_SP_DBG_TRACE_DEPTH;
		}

		for (n = host_index_last[t]; n < sp_index_last; n++) {
			int i = n % SH_CSS_SP_DBG_TRACE_DEPTH;
			int l = state->trace[t][i].location &
			    ((1 << SH_CSS_SP_DBG_TRACE_FILE_ID_BIT_POS) - 1);
			int fid = state->trace[t][i].location >>
			    SH_CSS_SP_DBG_TRACE_FILE_ID_BIT_POS;
			int ts = state->trace[t][i].time_stamp;

			if (ts) {
				ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
						    "%05d trace=%s, file=%s:%d, "
						    "data=0x%08x\n",
						    ts,
						    trace_name[t],
						    id2filename[fid], l,
						    state->trace[t][i].data);
			}
		}
		host_index_last[t] = sp_index_last;
	}

#elif SP_DEBUG == SP_DEBUG_MINIMAL
	int i;
	int base = 0;
	int limit = SH_CSS_NUM_SP_DEBUG;
	int step = 1;

	assert(state != NULL);

	for (i = base; i < limit; i += step) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
				    "sp_dbg_trace[%d] = %d\n",
				    i, state->debug[i]);
	}
#endif

#if SP_DEBUG != SP_DEBUG_NONE

	return;
}
#endif

#if defined(HAS_INPUT_FORMATTER_VERSION_2) && !defined(HAS_NO_INPUT_FORMATTER)
static void debug_print_rx_mipi_port_state(mipi_port_state_t *state)
{
	int i;
	unsigned int bits, infos;

	assert(state != NULL);

	bits = state->irq_status;
	infos = ia_css_isys_rx_translate_irq_infos(bits);

	ia_css_debug_dtrace(2, "\t\t%-32s: (irq reg = 0x%X)\n",
			    "receiver errors", bits);

	if (infos & IA_CSS_RX_IRQ_INFO_BUFFER_OVERRUN)
		ia_css_debug_dtrace(2, "\t\t\tbuffer overrun\n");
	if (infos & IA_CSS_RX_IRQ_INFO_ERR_SOT)
		ia_css_debug_dtrace(2, "\t\t\tstart-of-transmission error\n");
	if (infos & IA_CSS_RX_IRQ_INFO_ERR_SOT_SYNC)
		ia_css_debug_dtrace(2, "\t\t\tstart-of-transmission sync error\n");
	if (infos & IA_CSS_RX_IRQ_INFO_ERR_CONTROL)
		ia_css_debug_dtrace(2, "\t\t\tcontrol error\n");
	if (infos & IA_CSS_RX_IRQ_INFO_ERR_ECC_DOUBLE)
		ia_css_debug_dtrace(2, "\t\t\t2 or more ECC errors\n");
	if (infos & IA_CSS_RX_IRQ_INFO_ERR_CRC)
		ia_css_debug_dtrace(2, "\t\t\tCRC mismatch\n");
	if (infos & IA_CSS_RX_IRQ_INFO_ERR_UNKNOWN_ID)
		ia_css_debug_dtrace(2, "\t\t\tunknown error\n");
	if (infos & IA_CSS_RX_IRQ_INFO_ERR_FRAME_SYNC)
		ia_css_debug_dtrace(2, "\t\t\tframe sync error\n");
	if (infos & IA_CSS_RX_IRQ_INFO_ERR_FRAME_DATA)
		ia_css_debug_dtrace(2, "\t\t\tframe data error\n");
	if (infos & IA_CSS_RX_IRQ_INFO_ERR_DATA_TIMEOUT)
		ia_css_debug_dtrace(2, "\t\t\tdata timeout\n");
	if (infos & IA_CSS_RX_IRQ_INFO_ERR_UNKNOWN_ESC)
		ia_css_debug_dtrace(2, "\t\t\tunknown escape command entry\n");
	if (infos & IA_CSS_RX_IRQ_INFO_ERR_LINE_SYNC)
		ia_css_debug_dtrace(2, "\t\t\tline sync error\n");

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "device_ready", state->device_ready);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "irq_status", state->irq_status);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "irq_enable", state->irq_enable);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "timeout_count", state->timeout_count);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "init_count", state->init_count);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "raw16_18", state->raw16_18);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "sync_count", state->sync_count);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "rx_count", state->rx_count);

	for (i = 0; i < MIPI_4LANE_CFG; i++) {
		ia_css_debug_dtrace(2, "\t\t%-32s%d%-32s: %d\n",
				    "lane_sync_count[", i, "]",
				    state->lane_sync_count[i]);
	}

	for (i = 0; i < MIPI_4LANE_CFG; i++) {
		ia_css_debug_dtrace(2, "\t\t%-32s%d%-32s: %d\n",
				    "lane_rx_count[", i, "]",
				    state->lane_rx_count[i]);
	}

	return;
}

static void debug_print_rx_channel_state(rx_channel_state_t *state)
{
	int i;

	assert(state != NULL);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "compression_scheme0", state->comp_scheme0);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "compression_scheme1", state->comp_scheme1);

	for (i = 0; i < N_MIPI_FORMAT_CUSTOM; i++) {
		ia_css_debug_dtrace(2, "\t\t%-32s%d: %d\n",
				    "MIPI Predictor ", i, state->pred[i]);
	}

	for (i = 0; i < N_MIPI_FORMAT_CUSTOM; i++) {
		ia_css_debug_dtrace(2, "\t\t%-32s%d: %d\n",
				    "MIPI Compressor ", i, state->comp[i]);
	}

	return;
}

static void debug_print_rx_state(receiver_state_t *state)
{
	int i;

	assert(state != NULL);
	ia_css_debug_dtrace(2, "CSI Receiver State:\n");

	ia_css_debug_dtrace(2, "\tConfiguration:\n");

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "fs_to_ls_delay", state->fs_to_ls_delay);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "ls_to_data_delay", state->ls_to_data_delay);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "data_to_le_delay", state->data_to_le_delay);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "le_to_fe_delay", state->le_to_fe_delay);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "fe_to_fs_delay", state->fe_to_fs_delay);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "le_to_fs_delay", state->le_to_fs_delay);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "is_two_ppc", state->is_two_ppc);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "backend_rst", state->backend_rst);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "raw18", state->raw18);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "force_raw8", state->force_raw8);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "raw16", state->raw16);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "be_gsp_acc_ovl", state->be_gsp_acc_ovl);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "be_srst", state->be_srst);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "be_is_two_ppc", state->be_is_two_ppc);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "be_comp_format0", state->be_comp_format0);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "be_comp_format1", state->be_comp_format1);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "be_comp_format2", state->be_comp_format2);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "be_comp_format3", state->be_comp_format3);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "be_sel", state->be_sel);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "be_raw16_config", state->be_raw16_config);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "be_raw18_config", state->be_raw18_config);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "be_force_raw8", state->be_force_raw8);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "be_irq_status", state->be_irq_status);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "be_irq_clear", state->be_irq_clear);

	/* mipi port state */
	for (i = 0; i < N_MIPI_PORT_ID; i++) {
		ia_css_debug_dtrace(2, "\tMIPI Port %d State:\n", i);

		debug_print_rx_mipi_port_state(&state->mipi_port_state[i]);
	}
	/* end of mipi port state */

	/* rx channel state */
	for (i = 0; i < N_RX_CHANNEL_ID; i++) {
		ia_css_debug_dtrace(2, "\tRX Channel %d State:\n", i);

		debug_print_rx_channel_state(&state->rx_channel_state[i]);
	}
	/* end of rx channel state */

	return;
}
#endif

#if !defined(HAS_NO_INPUT_SYSTEM) && defined(USE_INPUT_SYSTEM_VERSION_2)
void ia_css_debug_dump_rx_state(void)
{
#if defined(HAS_INPUT_FORMATTER_VERSION_2) && !defined(HAS_NO_INPUT_FORMATTER)
	receiver_state_t state;

	receiver_get_state(RX0_ID, &state);
	debug_print_rx_state(&state);
#endif
}
#endif

void ia_css_debug_dump_sp_sw_debug_info(void)
{
#if SP_DEBUG != SP_DEBUG_NONE
	struct sh_css_sp_debug_state state;

	sh_css_sp_get_debug_state(&state);
	ia_css_debug_print_sp_debug_state(&state);
#endif
	ia_css_bufq_dump_queue_info();
	ia_css_pipeline_dump_thread_map_info();
	return;
}

#if defined(USE_INPUT_SYSTEM_VERSION_2)
static void debug_print_isys_capture_unit_state(capture_unit_state_t *state)
{
	assert(state != NULL);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "Packet_Length", state->Packet_Length);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "Received_Length", state->Received_Length);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "Received_Short_Packets",
			    state->Received_Short_Packets);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "Received_Long_Packets",
			    state->Received_Long_Packets);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "Last_Command", state->Last_Command);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "Next_Command", state->Next_Command);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "Last_Acknowledge", state->Last_Acknowledge);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "Next_Acknowledge", state->Next_Acknowledge);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "FSM_State_Info", state->FSM_State_Info);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "StartMode", state->StartMode);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "Start_Addr", state->Start_Addr);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "Mem_Region_Size", state->Mem_Region_Size);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "Num_Mem_Regions", state->Num_Mem_Regions);
	return;
}

static void debug_print_isys_acquisition_unit_state(
				acquisition_unit_state_t *state)
{
	assert(state != NULL);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "Received_Short_Packets",
			    state->Received_Short_Packets);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "Received_Long_Packets",
			    state->Received_Long_Packets);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "Last_Command", state->Last_Command);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "Next_Command", state->Next_Command);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "Last_Acknowledge", state->Last_Acknowledge);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "Next_Acknowledge", state->Next_Acknowledge);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "FSM_State_Info", state->FSM_State_Info);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "Int_Cntr_Info", state->Int_Cntr_Info);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "Start_Addr", state->Start_Addr);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "Mem_Region_Size", state->Mem_Region_Size);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "Num_Mem_Regions", state->Num_Mem_Regions);
}

static void debug_print_isys_ctrl_unit_state(ctrl_unit_state_t *state)
{
	assert(state != NULL);
	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "last_cmd", state->last_cmd);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "next_cmd", state->next_cmd);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "last_ack", state->last_ack);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n", "next_ack", state->next_ack);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "top_fsm_state", state->top_fsm_state);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "captA_fsm_state", state->captA_fsm_state);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "captB_fsm_state", state->captB_fsm_state);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "captC_fsm_state", state->captC_fsm_state);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "acq_fsm_state", state->acq_fsm_state);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "captA_start_addr", state->captA_start_addr);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "captB_start_addr", state->captB_start_addr);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "captC_start_addr", state->captC_start_addr);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "captA_mem_region_size",
			    state->captA_mem_region_size);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "captB_mem_region_size",
			    state->captB_mem_region_size);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "captC_mem_region_size",
			    state->captC_mem_region_size);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "captA_num_mem_regions",
			    state->captA_num_mem_regions);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "captB_num_mem_regions",
			    state->captB_num_mem_regions);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "captC_num_mem_regions",
			    state->captC_num_mem_regions);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "acq_start_addr", state->acq_start_addr);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "acq_mem_region_size", state->acq_mem_region_size);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "acq_num_mem_regions", state->acq_num_mem_regions);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "capt_reserve_one_mem_region",
			    state->capt_reserve_one_mem_region);

	return;
}

static void debug_print_isys_state(input_system_state_t *state)
{
	int i;

	assert(state != NULL);
	ia_css_debug_dtrace(2, "InputSystem State:\n");

	/* configuration */
	ia_css_debug_dtrace(2, "\tConfiguration:\n");

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "str_multiCastA_sel", state->str_multicastA_sel);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "str_multicastB_sel", state->str_multicastB_sel);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "str_multicastC_sel", state->str_multicastC_sel);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "str_mux_sel", state->str_mux_sel);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "str_mon_status", state->str_mon_status);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "str_mon_irq_cond", state->str_mon_irq_cond);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "str_mon_irq_en", state->str_mon_irq_en);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "isys_srst", state->isys_srst);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "isys_slv_reg_srst", state->isys_slv_reg_srst);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "str_deint_portA_cnt", state->str_deint_portA_cnt);

	ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
			    "str_deint_portB_cnd", state->str_deint_portB_cnt);
	/* end of configuration */

	/* capture unit state */
	for (i = 0; i < N_CAPTURE_UNIT_ID; i++) {
		capture_unit_state_t *capture_unit_state;

		ia_css_debug_dtrace(2, "\tCaptureUnit %d State:\n", i);

		capture_unit_state = &state->capture_unit[i];
		debug_print_isys_capture_unit_state(capture_unit_state);
	}
	/* end of capture unit state */

	/* acquisition unit state */
	for (i = 0; i < N_ACQUISITION_UNIT_ID; i++) {
		acquisition_unit_state_t *acquisition_unit_state;

		ia_css_debug_dtrace(2, "\tAcquisitionUnit %d State:\n", i);

		acquisition_unit_state = &state->acquisition_unit[i];
		debug_print_isys_acquisition_unit_state(acquisition_unit_state);
	}
	/* end of acquisition unit state */

	/* control unit state */
	for (i = 0; i < N_CTRL_UNIT_ID; i++) {
		ia_css_debug_dtrace(2, "\tControlUnit %d State:\n", i);

		debug_print_isys_ctrl_unit_state(&state->ctrl_unit_state[i]);
	}
	/* end of control unit state */
}

void ia_css_debug_dump_isys_state(void)
{
	input_system_state_t state;

	input_system_get_state(INPUT_SYSTEM0_ID, &state);
	debug_print_isys_state(&state);

	return;
}
#endif
#if !defined(HAS_NO_INPUT_SYSTEM) && defined(USE_INPUT_SYSTEM_VERSION_2401)
void ia_css_debug_dump_isys_state(void)
{
	/* Android compilation fails if made a local variable
	stack size on android is limited to 2k and this structure
	is around 3.5K, in place of static malloc can be done but
	if this call is made too often it will lead to fragment memory
	versus a fixed allocation */
	static input_system_state_t state;

	input_system_get_state(INPUT_SYSTEM0_ID, &state);
	input_system_dump_state(INPUT_SYSTEM0_ID, &state);
}
#endif

void ia_css_debug_dump_debug_info(const char *context)
{
	if (context == NULL)
		context = "No Context provided";

	ia_css_debug_dtrace(2, "CSS Debug Info dump [Context = %s]\n", context);
#if !defined(HAS_NO_INPUT_SYSTEM) && defined(USE_INPUT_SYSTEM_VERSION_2)
	ia_css_debug_dump_rx_state();
#endif
#if !defined(HAS_NO_INPUT_FORMATTER) && defined(USE_INPUT_SYSTEM_VERSION_2)
	ia_css_debug_dump_if_state();
#endif
	ia_css_debug_dump_isp_state();
	ia_css_debug_dump_isp_sp_fifo_state();
	ia_css_debug_dump_isp_gdc_fifo_state();
	ia_css_debug_dump_sp_state();
	ia_css_debug_dump_perf_counters();

#ifdef HAS_WATCHDOG_SP_THREAD_DEBUG
	sh_css_dump_thread_wait_info();
	sh_css_dump_pipe_stage_info();
	sh_css_dump_pipe_stripe_info();
#endif
	ia_css_debug_dump_dma_isp_fifo_state();
	ia_css_debug_dump_dma_sp_fifo_state();
	ia_css_debug_dump_dma_state();
#if defined(USE_INPUT_SYSTEM_VERSION_2)
	ia_css_debug_dump_isys_state();

	{
		irq_controller_state_t state;
		irq_controller_get_state(IRQ2_ID, &state);

		ia_css_debug_dtrace(2, "\t%-32s:\n",
				    "Input System IRQ Controller State");

		ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
				    "irq_edge", state.irq_edge);

		ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
				    "irq_mask", state.irq_mask);

		ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
				    "irq_status", state.irq_status);

		ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
				    "irq_enable", state.irq_enable);

		ia_css_debug_dtrace(2, "\t\t%-32s: %d\n",
				    "irq_level_not_pulse",
				    state.irq_level_not_pulse);
	}
#endif
#if !defined(HAS_NO_INPUT_SYSTEM) && defined(USE_INPUT_SYSTEM_VERSION_2401)
	ia_css_debug_dump_isys_state();
#endif
#if defined(USE_INPUT_SYSTEM_VERSION_2) || defined(USE_INPUT_SYSTEM_VERSION_2401)
	ia_css_debug_tagger_state();
#endif
	return;
}

/** this function is for debug use, it can make SP go to sleep
  state after each frame, then user can dump the stable SP dmem.
  this function can be called after ia_css_start_sp()
  and before sh_css_init_buffer_queues()
*/
void ia_css_debug_enable_sp_sleep_mode(enum ia_css_sp_sleep_mode mode)
{
	const struct ia_css_fw_info *fw;
	unsigned int HIVE_ADDR_sp_sleep_mode;

	fw = &sh_css_sp_fw;
	HIVE_ADDR_sp_sleep_mode = fw->info.sp.sleep_mode;

	(void)HIVE_ADDR_sp_sleep_mode;	/* Suppres warnings in CRUN */

	sp_dmem_store_uint32(SP0_ID,
			     (unsigned int)sp_address_of(sp_sleep_mode),
			     (uint32_t) mode);
}

void ia_css_debug_wake_up_sp(void)
{
	/*hrt_ctl_start(SP); */
	sp_ctrl_setbit(SP0_ID, SP_SC_REG, SP_START_BIT);
}

#if !defined(IS_ISP_2500_SYSTEM)
#define FIND_DMEM_PARAMS_TYPE(stream, kernel, type) \
	(struct HRTCAT(HRTCAT(sh_css_isp_, type), _params) *) \
	findf_dmem_params(stream, offsetof(struct ia_css_memory_offsets, dmem.kernel))

#define FIND_DMEM_PARAMS(stream, kernel) FIND_DMEM_PARAMS_TYPE(stream, kernel, kernel)

/* Find a stage that support the kernel and return the parameters for that kernel */
static char *
findf_dmem_params(struct ia_css_stream *stream, short idx)
{
	int i;
	for (i = 0; i < stream->num_pipes; i++) {
		struct ia_css_pipe *pipe = stream->pipes[i];
		struct ia_css_pipeline *pipeline = ia_css_pipe_get_pipeline(pipe);
		struct ia_css_pipeline_stage *stage;
		for (stage = pipeline->stages; stage; stage = stage->next) {
			struct ia_css_binary *binary = stage->binary;
			short *offsets = (short *)&binary->info->mem_offsets.offsets.param->dmem;
			short dmem_offset = offsets[idx];
			const struct ia_css_host_data *isp_data =
				ia_css_isp_param_get_mem_init(&binary->mem_params,
							IA_CSS_PARAM_CLASS_PARAM, IA_CSS_ISP_DMEM0);
			if (dmem_offset < 0)
				continue;
			return &isp_data->address[dmem_offset];
		}
	}
	return NULL;
}
#endif

void ia_css_debug_dump_isp_params(struct ia_css_stream *stream,
				  unsigned int enable)
{
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE, "ISP PARAMETERS:\n");
#if defined(IS_ISP_2500_SYSTEM)
	(void)enable;
	(void)stream;
#else

	assert(stream != NULL);
	if ((enable & IA_CSS_DEBUG_DUMP_FPN)
	    || (enable & IA_CSS_DEBUG_DUMP_ALL)) {
		ia_css_fpn_dump(FIND_DMEM_PARAMS(stream, fpn), IA_CSS_DEBUG_VERBOSE);
	}
	if ((enable & IA_CSS_DEBUG_DUMP_OB)
	    || (enable & IA_CSS_DEBUG_DUMP_ALL)) {
		ia_css_ob_dump(FIND_DMEM_PARAMS(stream, ob), IA_CSS_DEBUG_VERBOSE);
	}
	if ((enable & IA_CSS_DEBUG_DUMP_SC)
	    || (enable & IA_CSS_DEBUG_DUMP_ALL)) {
		ia_css_sc_dump(FIND_DMEM_PARAMS(stream, sc), IA_CSS_DEBUG_VERBOSE);
	}
	if ((enable & IA_CSS_DEBUG_DUMP_WB)
	    || (enable & IA_CSS_DEBUG_DUMP_ALL)) {
		ia_css_wb_dump(FIND_DMEM_PARAMS(stream, wb), IA_CSS_DEBUG_VERBOSE);
	}
	if ((enable & IA_CSS_DEBUG_DUMP_DP)
	    || (enable & IA_CSS_DEBUG_DUMP_ALL)) {
		ia_css_dp_dump(FIND_DMEM_PARAMS(stream, dp), IA_CSS_DEBUG_VERBOSE);
	}
	if ((enable & IA_CSS_DEBUG_DUMP_BNR)
	    || (enable & IA_CSS_DEBUG_DUMP_ALL)) {
		ia_css_bnr_dump(FIND_DMEM_PARAMS(stream, bnr), IA_CSS_DEBUG_VERBOSE);
	}
	if ((enable & IA_CSS_DEBUG_DUMP_S3A)
	    || (enable & IA_CSS_DEBUG_DUMP_ALL)) {
		ia_css_s3a_dump(FIND_DMEM_PARAMS(stream, s3a), IA_CSS_DEBUG_VERBOSE);
	}
	if ((enable & IA_CSS_DEBUG_DUMP_DE)
	    || (enable & IA_CSS_DEBUG_DUMP_ALL)) {
		ia_css_de_dump(FIND_DMEM_PARAMS(stream, de), IA_CSS_DEBUG_VERBOSE);
	}
	if ((enable & IA_CSS_DEBUG_DUMP_YNR)
	    || (enable & IA_CSS_DEBUG_DUMP_ALL)) {
		ia_css_nr_dump(FIND_DMEM_PARAMS_TYPE(stream, nr, ynr),  IA_CSS_DEBUG_VERBOSE);
		ia_css_yee_dump(FIND_DMEM_PARAMS(stream, yee), IA_CSS_DEBUG_VERBOSE);
	}
	if ((enable & IA_CSS_DEBUG_DUMP_CSC)
	    || (enable & IA_CSS_DEBUG_DUMP_ALL)) {
		ia_css_csc_dump(FIND_DMEM_PARAMS(stream, csc), IA_CSS_DEBUG_VERBOSE);
		ia_css_yuv2rgb_dump(FIND_DMEM_PARAMS_TYPE(stream, yuv2rgb, csc), IA_CSS_DEBUG_VERBOSE);
		ia_css_rgb2yuv_dump(FIND_DMEM_PARAMS_TYPE(stream, rgb2yuv, csc), IA_CSS_DEBUG_VERBOSE);
	}
	if ((enable & IA_CSS_DEBUG_DUMP_GC)
	    || (enable & IA_CSS_DEBUG_DUMP_ALL)) {
		ia_css_gc_dump(FIND_DMEM_PARAMS(stream, gc), IA_CSS_DEBUG_VERBOSE);
	}
	if ((enable & IA_CSS_DEBUG_DUMP_TNR)
	    || (enable & IA_CSS_DEBUG_DUMP_ALL)) {
		ia_css_tnr_dump(FIND_DMEM_PARAMS(stream, tnr), IA_CSS_DEBUG_VERBOSE);
	}
	if ((enable & IA_CSS_DEBUG_DUMP_ANR)
	    || (enable & IA_CSS_DEBUG_DUMP_ALL)) {
		ia_css_anr_dump(FIND_DMEM_PARAMS(stream, anr), IA_CSS_DEBUG_VERBOSE);
	}
	if ((enable & IA_CSS_DEBUG_DUMP_CE)
	    || (enable & IA_CSS_DEBUG_DUMP_ALL)) {
		ia_css_ce_dump(FIND_DMEM_PARAMS(stream, ce), IA_CSS_DEBUG_VERBOSE);
	}
#endif
}

void sh_css_dump_sp_raw_copy_linecount(bool reduced)
{
	const struct ia_css_fw_info *fw;
	unsigned int HIVE_ADDR_raw_copy_line_count;
	int32_t raw_copy_line_count;
	static int32_t prev_raw_copy_line_count = -1;

	fw = &sh_css_sp_fw;
	HIVE_ADDR_raw_copy_line_count =
			fw->info.sp.raw_copy_line_count;

	(void)HIVE_ADDR_raw_copy_line_count;

	sp_dmem_load(SP0_ID,
		(unsigned int)sp_address_of(raw_copy_line_count),
		     &raw_copy_line_count,
		     sizeof(raw_copy_line_count));

	/* only indicate if copy loop is active */
	if (reduced)
		raw_copy_line_count = (raw_copy_line_count < 0)?raw_copy_line_count:1;
	/* do the handling */
	if (prev_raw_copy_line_count != raw_copy_line_count) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			"sh_css_dump_sp_raw_copy_linecount() "
			"line_count=%d\n",
			raw_copy_line_count);
		prev_raw_copy_line_count = raw_copy_line_count;
	}
}

void ia_css_debug_dump_isp_binary(void)
{
	const struct ia_css_fw_info *fw;
	unsigned int HIVE_ADDR_pipeline_sp_curr_binary_id;
	uint32_t curr_binary_id;
	static uint32_t prev_binary_id = 0xFFFFFFFF;
	static uint32_t sample_count;

	fw = &sh_css_sp_fw;
	HIVE_ADDR_pipeline_sp_curr_binary_id = fw->info.sp.curr_binary_id;

	(void)HIVE_ADDR_pipeline_sp_curr_binary_id;

	sp_dmem_load(SP0_ID,
		     (unsigned int)sp_address_of(pipeline_sp_curr_binary_id),
		     &curr_binary_id,
		     sizeof(curr_binary_id));

	/* do the handling */
	sample_count++;
	if (prev_binary_id != curr_binary_id) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
				    "sh_css_dump_isp_binary() "
				    "pipe_id=%d, binary_id=%d, sample_count=%d\n",
				    (curr_binary_id >> 16),
				    (curr_binary_id & 0x0ffff),
				    sample_count);
		sample_count = 0;
		prev_binary_id = curr_binary_id;
	}
}

void ia_css_debug_dump_perf_counters(void)
{
#if !defined(HAS_NO_INPUT_SYSTEM) && defined(USE_INPUT_SYSTEM_VERSION_2)
	const struct ia_css_fw_info *fw;
	int i;
	unsigned int HIVE_ADDR_ia_css_isys_sp_error_cnt;
	int32_t ia_css_sp_input_system_error_cnt[N_MIPI_PORT_ID + 1]; /* 3 Capture Units and 1 Acquire Unit. */

	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE, "Input System Error Counters:\n");

	fw = &sh_css_sp_fw;
	HIVE_ADDR_ia_css_isys_sp_error_cnt = fw->info.sp.perf_counter_input_system_error;

	(void)HIVE_ADDR_ia_css_isys_sp_error_cnt;

	sp_dmem_load(SP0_ID,
		     (unsigned int)sp_address_of(ia_css_isys_sp_error_cnt),
		     &ia_css_sp_input_system_error_cnt,
		     sizeof(ia_css_sp_input_system_error_cnt));

	for (i = 0; i < N_MIPI_PORT_ID + 1; i++) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE, "\tport[%d] = %d\n",
				i, ia_css_sp_input_system_error_cnt[i]);
	}
#endif
}

/*

void sh_css_init_ddr_debug_queue(void)
{
	hrt_vaddress ddr_debug_queue_addr =
			mmgr_malloc(sizeof(debug_data_ddr_t));
	const struct ia_css_fw_info *fw;
	unsigned int HIVE_ADDR_debug_buffer_ddr_address;

	fw = &sh_css_sp_fw;
	HIVE_ADDR_debug_buffer_ddr_address =
			fw->info.sp.debug_buffer_ddr_address;

	(void)HIVE_ADDR_debug_buffer_ddr_address;

	debug_buffer_ddr_init(ddr_debug_queue_addr);

	sp_dmem_store_uint32(SP0_ID,
		(unsigned int)sp_address_of(debug_buffer_ddr_address),
		(uint32_t)(ddr_debug_queue_addr));
}

void sh_css_load_ddr_debug_queue(void)
{
	debug_synch_queue_ddr();
}

void ia_css_debug_dump_ddr_debug_queue(void)
{
	int i;
	sh_css_load_ddr_debug_queue();
	for (i = 0; i < DEBUG_BUF_SIZE; i++) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			"ddr_debug_queue[%d] = 0x%x\n",
			i, debug_data_ptr->buf[i]);
	}
}
*/

/**
 * @brief Initialize the debug mode.
 * Refer to "ia_css_debug.h" for more details.
 */
bool ia_css_debug_mode_init(void)
{
	bool rc;
	rc = sh_css_sp_init_dma_sw_reg(0);
	return rc;
}

/**
 * @brief Disable the DMA channel.
 * Refer to "ia_css_debug.h" for more details.
 */
bool
ia_css_debug_mode_disable_dma_channel(int dma_id,
				      int channel_id, int request_type)
{
	bool rc;

	rc = sh_css_sp_set_dma_sw_reg(dma_id, channel_id, request_type, false);

	return rc;
}

/**
 * @brief Enable the DMA channel.
 * Refer to "ia_css_debug.h" for more details.
 */
bool
ia_css_debug_mode_enable_dma_channel(int dma_id,
				     int channel_id, int request_type)
{
	bool rc;

	rc = sh_css_sp_set_dma_sw_reg(dma_id, channel_id, request_type, true);

	return rc;
}

void dtrace_dot(const char *fmt, ...)
{
	va_list ap;

	assert(fmt != NULL);
	va_start(ap, fmt);

	ia_css_debug_dtrace(IA_CSS_DEBUG_INFO, "%s", DPG_START);
	ia_css_debug_vdtrace(IA_CSS_DEBUG_INFO, fmt, ap);
	ia_css_debug_dtrace(IA_CSS_DEBUG_INFO, "%s", DPG_END);
	va_end(ap);
}
#ifdef HAS_WATCHDOG_SP_THREAD_DEBUG
void sh_css_dump_thread_wait_info(void)
{
	const struct ia_css_fw_info *fw;
	int i;
	unsigned int HIVE_ADDR_sp_thread_wait;
	int32_t sp_thread_wait[MAX_THREAD_NUM];
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE, "SEM WAITS:\n");

	fw = &sh_css_sp_fw;
	HIVE_ADDR_sp_thread_wait =
			fw->info.sp.debug_wait;

	(void)HIVE_ADDR_sp_thread_wait;

	sp_dmem_load(SP0_ID,
		(unsigned int)sp_address_of(sp_thread_wait),
		     &sp_thread_wait,
		     sizeof(sp_thread_wait));
	for (i = 0; i < MAX_THREAD_NUM; i++) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			"\twait[%d] = 0x%X\n",
			i, sp_thread_wait[i]);
	}

}

void sh_css_dump_pipe_stage_info(void)
{
	const struct ia_css_fw_info *fw;
	int i;
	unsigned int HIVE_ADDR_sp_pipe_stage;
	int32_t sp_pipe_stage[MAX_THREAD_NUM];
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE, "PIPE STAGE:\n");

	fw = &sh_css_sp_fw;
	HIVE_ADDR_sp_pipe_stage =
			fw->info.sp.debug_stage;

	(void)HIVE_ADDR_sp_pipe_stage;

	sp_dmem_load(SP0_ID,
		(unsigned int)sp_address_of(sp_pipe_stage),
		     &sp_pipe_stage,
		     sizeof(sp_pipe_stage));
	for (i = 0; i < MAX_THREAD_NUM; i++) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			"\tstage[%d] = %d\n",
			i, sp_pipe_stage[i]);
	}

}

void sh_css_dump_pipe_stripe_info(void)
{
	const struct ia_css_fw_info *fw;
	int i;
	unsigned int HIVE_ADDR_sp_pipe_stripe;
	int32_t sp_pipe_stripe[MAX_THREAD_NUM];
	ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE, "PIPE STRIPE:\n");

	fw = &sh_css_sp_fw;
	HIVE_ADDR_sp_pipe_stripe =
			fw->info.sp.debug_stripe;

	(void)HIVE_ADDR_sp_pipe_stripe;

	sp_dmem_load(SP0_ID,
		(unsigned int)sp_address_of(sp_pipe_stripe),
		     &sp_pipe_stripe,
		     sizeof(sp_pipe_stripe));
	for (i = 0; i < MAX_THREAD_NUM; i++) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_VERBOSE,
			"\tstripe[%d] = %d\n",
			i, sp_pipe_stripe[i]);
	}

}
#endif

static void
ia_css_debug_pipe_graph_dump_frame(
	struct ia_css_frame *frame,
	enum ia_css_pipe_id id,
	char const *blob_name,
	char const *frame_name,
	bool in_frame)
{
	char bufinfo[100];

	if (frame->dynamic_queue_id == SH_CSS_INVALID_QUEUE_ID) {
		snprintf(bufinfo, sizeof(bufinfo), "Internal");
	} else {
		snprintf(bufinfo, sizeof(bufinfo), "Queue: %s %s",
			pipe_id_to_str[id],
			queue_id_to_str[frame->dynamic_queue_id]);
	}
	dtrace_dot(
		"node [shape = box, "
		"fixedsize=true, width=2, height=0.7]; \"0x%08lx\" "
		"[label = \"%s\\n%d(%d) x %d, %dbpp\\n%s\"];",
		HOST_ADDRESS(frame),
		debug_frame_format2str(frame->info.format),
		frame->info.res.width,
		frame->info.padded_width,
		frame->info.res.height,
		frame->info.raw_bit_depth,
		bufinfo);

	if (in_frame) {
		dtrace_dot(
			"\"0x%08lx\"->\"%s(pipe%d)\" "
			"[label = %s_frame];",
			HOST_ADDRESS(frame),
			blob_name, id, frame_name);
	} else {
		dtrace_dot(
			"\"%s(pipe%d)\"->\"0x%08lx\" "
			"[label = %s_frame];",
			blob_name, id,
			HOST_ADDRESS(frame),
			frame_name);
	}
}

void
ia_css_debug_pipe_graph_dump_prologue(void)
{
	dtrace_dot("digraph sh_css_pipe_graph {");
	dtrace_dot("rankdir=LR;");

	dtrace_dot("fontsize=9;");
	dtrace_dot("label = \"\\nEnable options: rp=reduced pipe, vfve=vf_veceven, "
		"dvse=dvs_envelope, dvs6=dvs_6axis, bo=block_out, "
		"fbds=fixed_bayer_ds, bf6=bayer_fir_6db, "
		"rawb=raw_binning, cont=continuous, disc=dis_crop\\n"
		"dp2a=dp_2adjacent, outp=output, outt=out_table, "
		"reff=ref_frame, par=params, gam=gamma, "
		"cagdc=ca_gdc, ispa=isp_addresses, inf=in_frame, "
		"outf=out_frame, hs=high_speed, inpc=input_chunking\"");
}

void ia_css_debug_pipe_graph_dump_epilogue(void)
{

	if (strlen(ring_buffer) > 0) {
		dtrace_dot(ring_buffer);
	}


	if (pg_inst.stream_format != N_IA_CSS_STREAM_FORMAT) {
		/* An input stream format has been set so assume we have
		 * an input system and sensor
		 */


		dtrace_dot(
			"node [shape = doublecircle, "
			"fixedsize=true, width=2.5]; \"input_system\" "
			"[label = \"Input system\"];");

		dtrace_dot(
			"\"input_system\"->\"%s\" "
			"[label = \"%s\"];",
			dot_id_input_bin, debug_stream_format2str(pg_inst.stream_format));

		dtrace_dot(
			"node [shape = doublecircle, "
			"fixedsize=true, width=2.5]; \"sensor\" "
			"[label = \"Sensor\"];");

		dtrace_dot(
			"\"sensor\"->\"input_system\" "
			"[label = \"%s\\n%d x %d\\n(%d x %d)\"];",
			debug_stream_format2str(pg_inst.stream_format),
			pg_inst.width, pg_inst.height,
			pg_inst.eff_width, pg_inst.eff_height);
	}

	dtrace_dot("}");

	/* Reset temp strings */
	memset(dot_id_input_bin, 0, sizeof(dot_id_input_bin));
	memset(ring_buffer, 0, sizeof(ring_buffer));

	pg_inst.do_init = true;
	pg_inst.width = 0;
	pg_inst.height = 0;
	pg_inst.eff_width = 0;
	pg_inst.eff_height = 0;
	pg_inst.stream_format = N_IA_CSS_STREAM_FORMAT;
}

void
ia_css_debug_pipe_graph_dump_stage(
	struct ia_css_pipeline_stage *stage,
	enum ia_css_pipe_id id)
{
	char blob_name[SH_CSS_MAX_BINARY_NAME+10] = "<unknown type>";
	char const *bin_type = "<unknown type>";
	int i;

	assert(stage != NULL);
	if (stage->sp_func != IA_CSS_PIPELINE_NO_FUNC)
		return;

	if (pg_inst.do_init) {
		ia_css_debug_pipe_graph_dump_prologue();
		pg_inst.do_init = false;
	}

	if (stage->binary) {
		bin_type = "binary";
		if (stage->binary->info->blob)
			snprintf(blob_name, sizeof(blob_name), "%s_stage%d",
				stage->binary->info->blob->name, stage->stage_num);
	} else if (stage->firmware) {
		bin_type = "firmware";
		strncpy_s(blob_name, sizeof(blob_name), IA_CSS_EXT_ISP_PROG_NAME(stage->firmware), sizeof(blob_name));
	}

	/* Guard in case of binaries that don't have any binary_info */
	if (stage->binary_info != NULL) {
		char enable_info1[100];
		char enable_info2[100];
		char enable_info3[100];
		char enable_info[200];
		struct ia_css_binary_info *bi = stage->binary_info;

		/* Split it in 2 function-calls to keep the amount of
		 * parameters per call "reasonable"
		 */
		snprintf(enable_info1, sizeof(enable_info1),
			"%s%s%s%s%s%s%s%s%s%s%s%s%s%s",
			bi->enable.reduced_pipe ?	"rp," : "",
			bi->enable.vf_veceven ?		"vfve," : "",
			bi->enable.dis ?		"dis," : "",
			bi->enable.dvs_envelope ?	"dvse," : "",
			bi->enable.uds ?		"uds," : "",
			bi->enable.dvs_6axis ?		"dvs6," : "",
			bi->enable.block_output ?	"bo," : "",
			bi->enable.ds ?			"ds," : "",
			bi->enable.bayer_fir_6db ?	"bf6," : "",
			bi->enable.raw_binning ?	"rawb," : "",
			bi->enable.continuous ?		"cont," : "",
			bi->enable.s3a ?		"s3a," : "",
			bi->enable.fpnr ?		"fpnr," : "",
			bi->enable.sc ?			"sc," : ""
			);

		snprintf(enable_info2, sizeof(enable_info2),
			"%s%s%s%s%s%s%s%s%s%s%s",
			bi->enable.macc ?		"macc," : "",
			bi->enable.output ?		"outp," : "",
			bi->enable.ref_frame ?		"reff," : "",
			bi->enable.tnr ?		"tnr," : "",
			bi->enable.xnr ?		"xnr," : "",
			bi->enable.params ?		"par," : "",
			bi->enable.ca_gdc ?		"cagdc," : "",
			bi->enable.isp_addresses ?	"ispa," : "",
			bi->enable.in_frame ?		"inf," : "",
			bi->enable.out_frame ?		"outf," : "",
			bi->enable.high_speed ?		"hs," : ""
			);

		/* And merge them into one string */
		snprintf(enable_info, sizeof(enable_info), "%s%s",
						enable_info1, enable_info2);
		{
			int l, p;
			char *ei = enable_info;

			l = strlen(ei);

			/* Replace last ',' with \0 if present */
			if (l && enable_info[l-1] == ',')
				enable_info[--l] = '\0';

			if (l <= ENABLE_LINE_MAX_LENGTH) {
				/* It fits on one line, copy string and init */
				/* other helper strings with empty string */
				strcpy_s(enable_info,
					sizeof(enable_info),
					ei);
			} else {
				/* Too big for one line, find last comma */
				p = ENABLE_LINE_MAX_LENGTH;
				while (ei[p] != ',')
					p--;
				/* Last comma found, copy till that comma */
				strncpy_s(enable_info1,
					sizeof(enable_info1),
					ei, p);
				enable_info1[p] = '\0';

				ei += p+1;
				l = strlen(ei);

				if (l <= ENABLE_LINE_MAX_LENGTH) {
					/* The 2nd line fits */
					/* we cannot use ei as argument because
					 * it is not guarenteed dword aligned
					 */
					strncpy_s(enable_info2,
						sizeof(enable_info2),
						ei, l);
					enable_info2[l] = '\0';
					snprintf(enable_info, sizeof(enable_info), "%s\\n%s",
						enable_info1, enable_info2);

				} else {
					/* 2nd line is still too long */
					p = ENABLE_LINE_MAX_LENGTH;
					while (ei[p] != ',')
						p--;
					strncpy_s(enable_info2,
						sizeof(enable_info2),
						ei, p);
					enable_info2[p] = '\0';
					ei += p+1;
					l = strlen(ei);

					if (l <= ENABLE_LINE_MAX_LENGTH) {
						/* The 3rd line fits */
						/* we cannot use ei as argument because
						* it is not guarenteed dword aligned
						*/
						strcpy_s(enable_info3,
							sizeof(enable_info3), ei);
						enable_info3[l] = '\0';
						snprintf(enable_info, sizeof(enable_info),
							"%s\\n%s\\n%s",
							enable_info1, enable_info2,
							enable_info3);
					} else {
						/* 3rd line is still too long */
						p = ENABLE_LINE_MAX_LENGTH;
						while (ei[p] != ',')
							p--;
						strncpy_s(enable_info3,
							sizeof(enable_info3),
							ei, p);
						enable_info3[p] = '\0';
						ei += p+1;
						strcpy_s(enable_info3,
							sizeof(enable_info3), ei);
						snprintf(enable_info, sizeof(enable_info),
							"%s\\n%s\\n%s",
							enable_info1, enable_info2,
							enable_info3);
					}
				}
			}
		}

		dtrace_dot("node [shape = circle, fixedsize=true, width=2.5, "
			"label=\"%s\\n%s\\n\\n%s\"]; \"%s(pipe%d)\"",
			bin_type, blob_name, enable_info, blob_name, id);

	}
	else {
		dtrace_dot("node [shape = circle, fixedsize=true, width=2.5, "
			"label=\"%s\\n%s\\n\"]; \"%s(pipe%d)\"",
			bin_type, blob_name, blob_name, id);
	}

	if (stage->stage_num == 0) {
		/*
		 * There are some implicite assumptions about which bin is the
		 * input binary e.g. which one is connected to the input system
		 * Priority:
		 * 1) sp_raw_copy bin has highest priority
		 * 2) First stage==0 binary of preview, video or capture
		 */
		if (strlen(dot_id_input_bin) == 0) {
			snprintf(dot_id_input_bin, sizeof(dot_id_input_bin),
				"%s(pipe%d)", blob_name, id);
		}
	}

	if (stage->args.in_frame) {
		ia_css_debug_pipe_graph_dump_frame(
			stage->args.in_frame, id, blob_name,
			"in", true);
	}

#ifndef ISP2401
	for (i = 0; i < NUM_VIDEO_TNR_FRAMES; i++) {
#else
	for (i = 0; i < NUM_TNR_FRAMES; i++) {
#endif
		if (stage->args.tnr_frames[i]) {
			ia_css_debug_pipe_graph_dump_frame(
					stage->args.tnr_frames[i], id,
					blob_name, "tnr_frame", true);
		}
	}

	for (i = 0; i < MAX_NUM_VIDEO_DELAY_FRAMES; i++) {
		if (stage->args.delay_frames[i]) {
			ia_css_debug_pipe_graph_dump_frame(
					stage->args.delay_frames[i], id,
					blob_name, "delay_frame", true);
		}
	}

	for (i = 0; i < IA_CSS_BINARY_MAX_OUTPUT_PORTS; i++) {
		if (stage->args.out_frame[i]) {
			ia_css_debug_pipe_graph_dump_frame(
				stage->args.out_frame[i], id, blob_name,
				"out", false);
		}
	}

	if (stage->args.out_vf_frame) {
		ia_css_debug_pipe_graph_dump_frame(
			stage->args.out_vf_frame, id, blob_name,
			"out_vf", false);
	}
}

void
ia_css_debug_pipe_graph_dump_sp_raw_copy(
	struct ia_css_frame *out_frame)
{
	assert(out_frame != NULL);
	if (pg_inst.do_init) {
		ia_css_debug_pipe_graph_dump_prologue();
		pg_inst.do_init = false;
	}

	dtrace_dot("node [shape = circle, fixedsize=true, width=2.5, "
		"label=\"%s\\n%s\"]; \"%s(pipe%d)\"",
		"sp-binary", "sp_raw_copy", "sp_raw_copy", 1);

	snprintf(ring_buffer, sizeof(ring_buffer),
		"node [shape = box, "
		"fixedsize=true, width=2, height=0.7]; \"0x%08lx\" "
		"[label = \"%s\\n%d(%d) x %d\\nRingbuffer\"];",
		HOST_ADDRESS(out_frame),
		debug_frame_format2str(out_frame->info.format),
		out_frame->info.res.width,
		out_frame->info.padded_width,
		out_frame->info.res.height);

	dtrace_dot(ring_buffer);

	dtrace_dot(
		"\"%s(pipe%d)\"->\"0x%08lx\" "
		"[label = out_frame];",
		"sp_raw_copy", 1, HOST_ADDRESS(out_frame));

	snprintf(dot_id_input_bin, sizeof(dot_id_input_bin), "%s(pipe%d)", "sp_raw_copy", 1);
}

void
ia_css_debug_pipe_graph_dump_stream_config(
	const struct ia_css_stream_config *stream_config)
{
	pg_inst.width = stream_config->input_config.input_res.width;
	pg_inst.height = stream_config->input_config.input_res.height;
	pg_inst.eff_width = stream_config->input_config.effective_res.width;
	pg_inst.eff_height = stream_config->input_config.effective_res.height;
	pg_inst.stream_format = stream_config->input_config.format;
}

void
ia_css_debug_dump_resolution(
	const struct ia_css_resolution *res,
	const char *label)
{
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "%s: =%d x =%d\n",
			label, res->width, res->height);
}

void
ia_css_debug_dump_frame_info(
	const struct ia_css_frame_info *info,
	const char *label)
{
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "%s\n", label);
	ia_css_debug_dump_resolution(&info->res, "res");
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "padded_width: %d\n",
			info->padded_width);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "format: %d\n", info->format);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "raw_bit_depth: %d\n",
			info->raw_bit_depth);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "raw_bayer_order: %d\n",
			info->raw_bayer_order);
}

void
ia_css_debug_dump_capture_config(
	const struct ia_css_capture_config *config)
{
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "%s\n", __func__);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "mode: %d\n", config->mode);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "enable_xnr:  %d\n",
			config->enable_xnr);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "enable_raw_output: %d\n",
			config->enable_raw_output);
}

void
ia_css_debug_dump_pipe_extra_config(
	const struct ia_css_pipe_extra_config *extra_config)
{
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "%s\n", __func__);
	if (extra_config) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"enable_raw_binning: %d\n",
				extra_config->enable_raw_binning);
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "enable_yuv_ds: %d\n",
				extra_config->enable_yuv_ds);
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"enable_high_speed:  %d\n",
				extra_config->enable_high_speed);
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"enable_dvs_6axis: %d\n",
				extra_config->enable_dvs_6axis);
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"enable_reduced_pipe: %d\n",
				extra_config->enable_reduced_pipe);
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"enable_fractional_ds: %d\n",
				extra_config->enable_fractional_ds);
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "disable_vf_pp: %d\n",
				extra_config->disable_vf_pp);
	}
}

void
ia_css_debug_dump_pipe_config(
	const struct ia_css_pipe_config *config)
{
	unsigned int i;

	IA_CSS_ENTER_PRIVATE("config = %p", config);
	if (!config) {
		IA_CSS_ERROR("NULL input parameter");
		IA_CSS_LEAVE_PRIVATE("");
		return;
	}
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "mode: %d\n", config->mode);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "isp_pipe_version: %d\n",
			config->isp_pipe_version);
	ia_css_debug_dump_resolution(&config->bayer_ds_out_res,
			"bayer_ds_out_res");
	ia_css_debug_dump_resolution(&config->capt_pp_in_res,
			"capt_pp_in_res");
	ia_css_debug_dump_resolution(&config->vf_pp_in_res, "vf_pp_in_res");
#ifdef ISP2401
	ia_css_debug_dump_resolution(&config->output_system_in_res,
				     "output_system_in_res");
#endif
	ia_css_debug_dump_resolution(&config->dvs_crop_out_res,
			"dvs_crop_out_res");
	for (i = 0; i < IA_CSS_PIPE_MAX_OUTPUT_STAGE; i++) {
		ia_css_debug_dump_frame_info(&config->output_info[i], "output_info");
		ia_css_debug_dump_frame_info(&config->vf_output_info[i],
				"vf_output_info");
	}
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "acc_extension: 0x%x\n",
			config->acc_extension);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "num_acc_stages: %d\n",
			config->num_acc_stages);
	ia_css_debug_dump_capture_config(&config->default_capture_config);
	ia_css_debug_dump_resolution(&config->dvs_envelope, "dvs_envelope");
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "dvs_frame_delay: %d\n",
			config->dvs_frame_delay);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "acc_num_execs: %d\n",
			config->acc_num_execs);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "enable_dz: %d\n",
			config->enable_dz);
	IA_CSS_LEAVE_PRIVATE("");
}

void
ia_css_debug_dump_stream_config_source(
	const struct ia_css_stream_config *config)
{
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "%s()\n", __func__);
	switch (config->mode) {
	case IA_CSS_INPUT_MODE_SENSOR:
	case IA_CSS_INPUT_MODE_BUFFERED_SENSOR:
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "source.port\n");
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "port: %d\n",
				config->source.port.port);
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "num_lanes: %d\n",
				config->source.port.num_lanes);
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "timeout: %d\n",
				config->source.port.timeout);
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "compression: %d\n",
				config->source.port.compression);
		break;
	case IA_CSS_INPUT_MODE_TPG:
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "source.tpg\n");
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "id: %d\n",
				config->source.tpg.id);
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "mode: %d\n",
				config->source.tpg.mode);
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "x_mask: 0x%x\n",
				config->source.tpg.x_mask);
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "x_delta: %d\n",
				config->source.tpg.x_delta);
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "y_mask: 0x%x\n",
				config->source.tpg.y_mask);
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "y_delta: %d\n",
				config->source.tpg.y_delta);
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "xy_mask: 0x%x\n",
				config->source.tpg.xy_mask);
		break;
	case IA_CSS_INPUT_MODE_PRBS:
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "source.prbs\n");
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "id: %d\n",
				config->source.prbs.id);
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "h_blank: %d\n",
				config->source.prbs.h_blank);
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "v_blank: %d\n",
				config->source.prbs.v_blank);
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "seed: 0x%x\n",
				config->source.prbs.seed);
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "seed1: 0x%x\n",
				config->source.prbs.seed1);
		break;
	default:
	case IA_CSS_INPUT_MODE_FIFO:
	case IA_CSS_INPUT_MODE_MEMORY:
		break;
	}
}

void
ia_css_debug_dump_mipi_buffer_config(
	const struct ia_css_mipi_buffer_config *config)
{
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "%s()\n", __func__);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "size_mem_words: %d\n",
			config->size_mem_words);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "nof_mipi_buffers: %d\n",
			config->nof_mipi_buffers);
}

void
ia_css_debug_dump_metadata_config(
	const struct ia_css_metadata_config *config)
{
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "%s()\n", __func__);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "data_type: %d\n",
			config->data_type);
	ia_css_debug_dump_resolution(&config->resolution, "resolution");
}

void
ia_css_debug_dump_stream_config(
	const struct ia_css_stream_config *config,
	int num_pipes)
{
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "%s()\n", __func__);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "num_pipes: %d\n", num_pipes);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "mode: %d\n", config->mode);
	ia_css_debug_dump_stream_config_source(config);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "channel_id: %d\n",
			config->channel_id);
	ia_css_debug_dump_resolution(&config->input_config.input_res, "input_res");
	ia_css_debug_dump_resolution(&config->input_config.effective_res, "effective_res");
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "format: %d\n",
			config->input_config.format);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "bayer_order: %d\n",
			config->input_config.bayer_order);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "sensor_binning_factor: %d\n",
			config->sensor_binning_factor);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "pixels_per_clock: %d\n",
			config->pixels_per_clock);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "online: %d\n",
			config->online);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "init_num_cont_raw_buf: %d\n",
			config->init_num_cont_raw_buf);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			"target_num_cont_raw_buf: %d\n",
			config->target_num_cont_raw_buf);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "pack_raw_pixels: %d\n",
			config->pack_raw_pixels);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "continuous: %d\n",
			config->continuous);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "flash_gpio_pin: %d\n",
			config->flash_gpio_pin);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "left_padding: %d\n",
			config->left_padding);
	ia_css_debug_dump_mipi_buffer_config(&config->mipi_buffer_config);
	ia_css_debug_dump_metadata_config(&config->metadata_config);
}

/*
    Trace support.

    This tracer is using a buffer to trace the flow of the FW and dump misc values (see below for details).
    Currently, support is only for SKC.
    To enable support for other platforms:
     - Allocate a buffer for tracing in DMEM. The longer the better.
     - Use the DBG_init routine in sp.hive.c to initiatilize the tracer with the address and size selected.
     - Add trace points in the SP code wherever needed.
     - Enable the dump below with the required address and required adjustments.
	   Dump is called at the end of ia_css_debug_dump_sp_state().
*/

/*
 dump_trace() : dump the trace points from DMEM2.
 for every trace point, the following are printed: index, major:minor and the 16-bit attached value.
 The routine looks for the first 0, and then prints from it cyclically.
 Data forma in DMEM2:
  first 4 DWORDS: header
   DWORD 0: data description
    byte 0: version
    byte 1: number of threads (for future use)
    byte 2+3: number ot TPs
   DWORD 1: command byte + data (for future use)
    byte 0: command
    byte 1-3: command signature
   DWORD 2-3: additional data (for future use)
  Following data is 4-byte oriented:
    byte 0:   major
	byte 1:   minor
	byte 2-3: data
*/
#if TRACE_ENABLE_SP0 || TRACE_ENABLE_SP1 || TRACE_ENABLE_ISP
#ifndef ISP2401
static void debug_dump_one_trace(TRACE_CORE_ID proc_id)
#else
static void debug_dump_one_trace(enum TRACE_CORE_ID proc_id)
#endif
{
#if defined(HAS_TRACER_V2)
	uint32_t start_addr;
	uint32_t start_addr_data;
	uint32_t item_size;
#ifndef ISP2401
	uint32_t tmp;
#else
	uint8_t tid_val;
	enum TRACE_DUMP_FORMAT dump_format;
#endif
	int i, j, max_trace_points, point_num, limit = -1;
	/* using a static buffer here as the driver has issues allocating memory */
	static uint32_t trace_read_buf[TRACE_BUFF_SIZE] = {0};
#ifdef ISP2401
	static struct trace_header_t header;
	uint8_t *header_arr;
#endif

	/* read the header and parse it */
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "~~~ Tracer ");
	switch (proc_id)
	{
	case TRACE_SP0_ID:
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "SP0");
		start_addr = TRACE_SP0_ADDR;
		start_addr_data = TRACE_SP0_DATA_ADDR;
		item_size = TRACE_SP0_ITEM_SIZE;
		max_trace_points = TRACE_SP0_MAX_POINTS;
		break;
	case TRACE_SP1_ID:
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "SP1");
		start_addr = TRACE_SP1_ADDR;
		start_addr_data = TRACE_SP1_DATA_ADDR;
		item_size = TRACE_SP1_ITEM_SIZE;
		max_trace_points = TRACE_SP1_MAX_POINTS;
		break;
	case TRACE_ISP_ID:
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "ISP");
		start_addr = TRACE_ISP_ADDR;
		start_addr_data = TRACE_ISP_DATA_ADDR;
		item_size = TRACE_ISP_ITEM_SIZE;
		max_trace_points = TRACE_ISP_MAX_POINTS;
		break;
	default:
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "\t\ttraces are not supported for this processor ID - exiting\n");
		return;
	}
#ifndef ISP2401
	tmp = ia_css_device_load_uint32(start_addr);
	point_num = (tmp >> 16) & 0xFFFF;
#endif

#ifndef ISP2401
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, " ver %d %d points\n", tmp & 0xFF, point_num);
	if ((tmp & 0xFF) != TRACER_VER) {
#else
	/* Loading byte-by-byte as using the master routine had issues */
	header_arr = (uint8_t *)&header;
	for (i = 0; i < (int)sizeof(struct trace_header_t); i++)
		header_arr[i] = ia_css_device_load_uint8(start_addr + (i));

	point_num = header.max_tracer_points;

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, " ver %d %d points\n", header.version, point_num);
	if ((header.version & 0xFF) != TRACER_VER) {
#endif
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "\t\tUnknown version - exiting\n");
		return;
	}
	if (point_num > max_trace_points) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "\t\tToo many points - exiting\n");
		return;
	}
	/* copy the TPs and find the first 0 */
	for (i = 0; i < point_num; i++) {
		trace_read_buf[i] = ia_css_device_load_uint32(start_addr_data + (i * item_size));
		if ((limit == (-1)) && (trace_read_buf[i] == 0))
			limit = i;
	}
#ifdef ISP2401
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "Status:\n");
	for (i = 0; i < SH_CSS_MAX_SP_THREADS; i++)
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "\tT%d: %3d (%02x)  %6d (%04x)  %10d (%08x)\n", i,
				header.thr_status_byte[i], header.thr_status_byte[i],
				header.thr_status_word[i], header.thr_status_word[i],
				header.thr_status_dword[i], header.thr_status_dword[i]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "Scratch:\n");
	for (i = 0; i < MAX_SCRATCH_DATA; i++)
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "%10d (%08x)  ",
			header.scratch_debug[i], header.scratch_debug[i]);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "\n");

#endif
	/* two 0s in the beginning: empty buffer */
	if ((trace_read_buf[0] == 0) && (trace_read_buf[1] == 0)) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "\t\tEmpty tracer - exiting\n");
		return;
	}
	/* no overrun: start from 0 */
	if ((limit == point_num-1) ||         /* first 0 is at the end - border case */
	    (trace_read_buf[limit+1] == 0))   /* did not make a full cycle after the memset */
		limit = 0;
	/* overrun: limit is the first non-zero after the first zero */
	else
		limit++;

	/* print the TPs */
	for (i = 0; i < point_num; i++) {
		j = (limit + i) % point_num;
		if (trace_read_buf[j])
		{
#ifndef ISP2401
			TRACE_DUMP_FORMAT dump_format = FIELD_FORMAT_UNPACK(trace_read_buf[j]);
#else

			tid_val = FIELD_TID_UNPACK(trace_read_buf[j]);
			dump_format = TRACE_DUMP_FORMAT_POINT;

			/*
			 * When tid value is 111b, the data will be interpreted differently:
			 * tid val is ignored, major field contains 2 bits (msb) for format type
			 */
			if (tid_val == FIELD_TID_SEL_FORMAT_PAT) {
				dump_format = FIELD_FORMAT_UNPACK(trace_read_buf[j]);
			}
#endif
			switch (dump_format)
			{
			case TRACE_DUMP_FORMAT_POINT:
				ia_css_debug_dtrace(
#ifndef ISP2401
						IA_CSS_DEBUG_TRACE,	"\t\t%d %d:%d value - %d\n",
						j, FIELD_MAJOR_UNPACK(trace_read_buf[j]),
#else
						IA_CSS_DEBUG_TRACE,	"\t\t%d T%d %d:%d value - %x (%d)\n",
						j,
						tid_val,
						FIELD_MAJOR_UNPACK(trace_read_buf[j]),
#endif
						FIELD_MINOR_UNPACK(trace_read_buf[j]),
#ifdef ISP2401
						FIELD_VALUE_UNPACK(trace_read_buf[j]),
#endif
						FIELD_VALUE_UNPACK(trace_read_buf[j]));
				break;
#ifndef ISP2401
			case TRACE_DUMP_FORMAT_VALUE24_HEX:
#else
			case TRACE_DUMP_FORMAT_POINT_NO_TID:
#endif
				ia_css_debug_dtrace(
#ifndef ISP2401
						IA_CSS_DEBUG_TRACE,	"\t\t%d, %d, 24bit value %x H\n",
#else
						IA_CSS_DEBUG_TRACE,	"\t\t%d %d:%d value - %x (%d)\n",
#endif
						j,
#ifndef ISP2401
						FIELD_MAJOR_UNPACK(trace_read_buf[j]),
						FIELD_VALUE_24_UNPACK(trace_read_buf[j]));
#else
						FIELD_MAJOR_W_FMT_UNPACK(trace_read_buf[j]),
						FIELD_MINOR_UNPACK(trace_read_buf[j]),
						FIELD_VALUE_UNPACK(trace_read_buf[j]),
						FIELD_VALUE_UNPACK(trace_read_buf[j]));
#endif
				break;
#ifndef ISP2401
			case TRACE_DUMP_FORMAT_VALUE24_DEC:
#else
			case TRACE_DUMP_FORMAT_VALUE24:
#endif
				ia_css_debug_dtrace(
#ifndef ISP2401
						IA_CSS_DEBUG_TRACE,	"\t\t%d, %d, 24bit value %d D\n",
#else
						IA_CSS_DEBUG_TRACE,	"\t\t%d, %d, 24bit value %x (%d)\n",
#endif
						j,
						FIELD_MAJOR_UNPACK(trace_read_buf[j]),
#ifdef ISP2401
						FIELD_MAJOR_W_FMT_UNPACK(trace_read_buf[j]),
						FIELD_VALUE_24_UNPACK(trace_read_buf[j]),
#endif
						FIELD_VALUE_24_UNPACK(trace_read_buf[j]));
				break;
#ifdef ISP2401

#endif
			case TRACE_DUMP_FORMAT_VALUE24_TIMING:
				ia_css_debug_dtrace(
						IA_CSS_DEBUG_TRACE,	"\t\t%d, %d, timing %x\n",
						j,
#ifndef ISP2401
						FIELD_MAJOR_UNPACK(trace_read_buf[j]),
#else
						FIELD_MAJOR_W_FMT_UNPACK(trace_read_buf[j]),
#endif
						FIELD_VALUE_24_UNPACK(trace_read_buf[j]));
				break;
			case TRACE_DUMP_FORMAT_VALUE24_TIMING_DELTA:
				ia_css_debug_dtrace(
						IA_CSS_DEBUG_TRACE,	"\t\t%d, %d, timing delta %x\n",
						j,
#ifndef ISP2401
						FIELD_MAJOR_UNPACK(trace_read_buf[j]),
#else
						FIELD_MAJOR_W_FMT_UNPACK(trace_read_buf[j]),
#endif
						FIELD_VALUE_24_UNPACK(trace_read_buf[j]));
				break;
			default:
				ia_css_debug_dtrace(
						IA_CSS_DEBUG_TRACE,
						"no such trace dump format %d",
#ifndef ISP2401
						FIELD_FORMAT_UNPACK(trace_read_buf[j]));
#else
						dump_format);
#endif
				break;
			}
		}
	}
#else
	(void)proc_id;
#endif /* HAS_TRACER_V2 */
}
#endif /* TRACE_ENABLE_SP0 || TRACE_ENABLE_SP1 || TRACE_ENABLE_ISP */

void ia_css_debug_dump_trace(void)
{
#if TRACE_ENABLE_SP0
	debug_dump_one_trace(TRACE_SP0_ID);
#endif
#if TRACE_ENABLE_SP1
	debug_dump_one_trace(TRACE_SP1_ID);
#endif
#if TRACE_ENABLE_ISP
	debug_dump_one_trace(TRACE_ISP_ID);
#endif
}

#if defined(USE_INPUT_SYSTEM_VERSION_2) || defined(USE_INPUT_SYSTEM_VERSION_2401)
/* Tagger state dump function. The tagger is only available when the CSS
 * contains an input system (2400 or 2401). */
void ia_css_debug_tagger_state(void)
{
	unsigned int i;
	unsigned int HIVE_ADDR_tagger_frames;
	ia_css_tagger_buf_sp_elem_t tbuf_frames[MAX_CB_ELEMS_FOR_TAGGER];

	HIVE_ADDR_tagger_frames = sh_css_sp_fw.info.sp.tagger_frames_addr;

	/* This variable is not used in crun */
	(void)HIVE_ADDR_tagger_frames;

	/* 2400 and 2401 only have 1 SP, so the tagger lives on SP0 */
	sp_dmem_load(SP0_ID,
		     (unsigned int)sp_address_of(tagger_frames),
		     tbuf_frames,
		     sizeof(tbuf_frames));

	ia_css_debug_dtrace(2, "Tagger Info:\n");
	for (i = 0; i < MAX_CB_ELEMS_FOR_TAGGER; i++) {
		ia_css_debug_dtrace(2, "\t tagger frame[%d]: exp_id=%d, marked=%d, locked=%d\n",
				i, tbuf_frames[i].exp_id, tbuf_frames[i].mark, tbuf_frames[i].lock);
	}

}
#endif /* defined(USE_INPUT_SYSTEM_VERSION_2) || defined(USE_INPUT_SYSTEM_VERSION_2401) */

#ifdef ISP2401
void ia_css_debug_pc_dump(sp_ID_t id, unsigned int num_of_dumps)
{
	unsigned int pc;
	unsigned int i;
	hrt_data sc = sp_ctrl_load(id, SP_SC_REG);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "SP%-1d Status reg: 0x%X\n", id, sc);
	sc = sp_ctrl_load(id, SP_CTRL_SINK_REG);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "SP%-1d Stall reg: 0x%X\n", id, sc);
	for (i = 0; i < num_of_dumps; i++) {
		pc = sp_ctrl_load(id, SP_PC_REG);
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "SP%-1d PC: 0x%X\n", id, pc);
	}
}
#endif

#if defined(HRT_SCHED) || defined(SH_CSS_DEBUG_SPMEM_DUMP_SUPPORT)
#include "spmem_dump.c"
#endif
