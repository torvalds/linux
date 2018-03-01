/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2017 Andes Technology Corporation */

/*
 * The graph frame test is not possible if CONFIG_FRAME_POINTER is not enabled.
 * Check arch/riscv/kernel/mcount.S for detail.
 */
#if defined(CONFIG_FUNCTION_GRAPH_TRACER) && defined(CONFIG_FRAME_POINTER)
#define HAVE_FUNCTION_GRAPH_FP_TEST
#endif
