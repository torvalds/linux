/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

/*
 * In addition to including the proper base SPR definition file, depending
 * on machine architecture, this file defines several macros which allow
 * kernel code to use protection-level dependent SPRs without worrying
 * about which PL it's running at.  In these macros, the PL that the SPR
 * or interrupt number applies to is replaced by K.
 */

#if CONFIG_KERNEL_PL != 1 && CONFIG_KERNEL_PL != 2
#error CONFIG_KERNEL_PL must be 1 or 2
#endif

/* Concatenate 4 strings. */
#define __concat4(a, b, c, d) a ## b ## c ## d
#define _concat4(a, b, c, d)  __concat4(a, b, c, d)

#ifdef __tilegx__
#include <arch/spr_def_64.h>

/* TILE-Gx dependent, protection-level dependent SPRs. */

#define SPR_INTERRUPT_MASK_K \
	_concat4(SPR_INTERRUPT_MASK_, CONFIG_KERNEL_PL,,)
#define SPR_INTERRUPT_MASK_SET_K \
	_concat4(SPR_INTERRUPT_MASK_SET_, CONFIG_KERNEL_PL,,)
#define SPR_INTERRUPT_MASK_RESET_K \
	_concat4(SPR_INTERRUPT_MASK_RESET_, CONFIG_KERNEL_PL,,)
#define SPR_INTERRUPT_VECTOR_BASE_K \
	_concat4(SPR_INTERRUPT_VECTOR_BASE_, CONFIG_KERNEL_PL,,)

#define SPR_IPI_MASK_K \
	_concat4(SPR_IPI_MASK_, CONFIG_KERNEL_PL,,)
#define SPR_IPI_MASK_RESET_K \
	_concat4(SPR_IPI_MASK_RESET_, CONFIG_KERNEL_PL,,)
#define SPR_IPI_MASK_SET_K \
	_concat4(SPR_IPI_MASK_SET_, CONFIG_KERNEL_PL,,)
#define SPR_IPI_EVENT_K \
	_concat4(SPR_IPI_EVENT_, CONFIG_KERNEL_PL,,)
#define SPR_IPI_EVENT_RESET_K \
	_concat4(SPR_IPI_EVENT_RESET_, CONFIG_KERNEL_PL,,)
#define SPR_IPI_MASK_SET_K \
	_concat4(SPR_IPI_MASK_SET_, CONFIG_KERNEL_PL,,)
#define INT_IPI_K \
	_concat4(INT_IPI_, CONFIG_KERNEL_PL,,)

#define SPR_SINGLE_STEP_CONTROL_K \
	_concat4(SPR_SINGLE_STEP_CONTROL_, CONFIG_KERNEL_PL,,)
#define SPR_SINGLE_STEP_EN_K_K \
	_concat4(SPR_SINGLE_STEP_EN_, CONFIG_KERNEL_PL, _, CONFIG_KERNEL_PL)
#define INT_SINGLE_STEP_K \
	_concat4(INT_SINGLE_STEP_, CONFIG_KERNEL_PL,,)

#else
#include <arch/spr_def_32.h>

/* TILEPro dependent, protection-level dependent SPRs. */

#define SPR_INTERRUPT_MASK_K_0 \
	_concat4(SPR_INTERRUPT_MASK_, CONFIG_KERNEL_PL, _0,)
#define SPR_INTERRUPT_MASK_K_1 \
	_concat4(SPR_INTERRUPT_MASK_, CONFIG_KERNEL_PL, _1,)
#define SPR_INTERRUPT_MASK_SET_K_0 \
	_concat4(SPR_INTERRUPT_MASK_SET_, CONFIG_KERNEL_PL, _0,)
#define SPR_INTERRUPT_MASK_SET_K_1 \
	_concat4(SPR_INTERRUPT_MASK_SET_, CONFIG_KERNEL_PL, _1,)
#define SPR_INTERRUPT_MASK_RESET_K_0 \
	_concat4(SPR_INTERRUPT_MASK_RESET_, CONFIG_KERNEL_PL, _0,)
#define SPR_INTERRUPT_MASK_RESET_K_1 \
	_concat4(SPR_INTERRUPT_MASK_RESET_, CONFIG_KERNEL_PL, _1,)

#endif

/* Generic protection-level dependent SPRs. */

#define SPR_SYSTEM_SAVE_K_0 \
	_concat4(SPR_SYSTEM_SAVE_, CONFIG_KERNEL_PL, _0,)
#define SPR_SYSTEM_SAVE_K_1 \
	_concat4(SPR_SYSTEM_SAVE_, CONFIG_KERNEL_PL, _1,)
#define SPR_SYSTEM_SAVE_K_2 \
	_concat4(SPR_SYSTEM_SAVE_, CONFIG_KERNEL_PL, _2,)
#define SPR_SYSTEM_SAVE_K_3 \
	_concat4(SPR_SYSTEM_SAVE_, CONFIG_KERNEL_PL, _3,)
#define SPR_EX_CONTEXT_K_0 \
	_concat4(SPR_EX_CONTEXT_, CONFIG_KERNEL_PL, _0,)
#define SPR_EX_CONTEXT_K_1 \
	_concat4(SPR_EX_CONTEXT_, CONFIG_KERNEL_PL, _1,)
#define SPR_INTCTRL_K_STATUS \
	_concat4(SPR_INTCTRL_, CONFIG_KERNEL_PL, _STATUS,)
#define INT_INTCTRL_K \
	_concat4(INT_INTCTRL_, CONFIG_KERNEL_PL,,)
