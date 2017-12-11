/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2011 Tilera Corporation. All Rights Reserved.
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

#ifndef __ARCH_INTERRUPTS_H__
#define __ARCH_INTERRUPTS_H__

#ifndef __KERNEL__
/** Mask for an interrupt. */
#ifdef __ASSEMBLER__
/* Note: must handle breaking interrupts into high and low words manually. */
#define INT_MASK(intno) (1 << (intno))
#else
#define INT_MASK(intno) (1ULL << (intno))
#endif
#endif


/** Where a given interrupt executes */
#define INTERRUPT_VECTOR(i, pl) (0xFC000000 + ((pl) << 24) + ((i) << 8))

/** Where to store a vector for a given interrupt. */
#define USER_INTERRUPT_VECTOR(i) INTERRUPT_VECTOR(i, 0)

/** The base address of user-level interrupts. */
#define USER_INTERRUPT_VECTOR_BASE INTERRUPT_VECTOR(0, 0)


/** Additional synthetic interrupt. */
#define INT_BREAKPOINT (63)

#define INT_MEM_ERROR    0
#define INT_SINGLE_STEP_3    1
#define INT_SINGLE_STEP_2    2
#define INT_SINGLE_STEP_1    3
#define INT_SINGLE_STEP_0    4
#define INT_IDN_COMPLETE    5
#define INT_UDN_COMPLETE    6
#define INT_ITLB_MISS    7
#define INT_ILL    8
#define INT_GPV    9
#define INT_IDN_ACCESS   10
#define INT_UDN_ACCESS   11
#define INT_SWINT_3   12
#define INT_SWINT_2   13
#define INT_SWINT_1   14
#define INT_SWINT_0   15
#define INT_ILL_TRANS   16
#define INT_UNALIGN_DATA   17
#define INT_DTLB_MISS   18
#define INT_DTLB_ACCESS   19
#define INT_IDN_FIREWALL   20
#define INT_UDN_FIREWALL   21
#define INT_TILE_TIMER   22
#define INT_AUX_TILE_TIMER   23
#define INT_IDN_TIMER   24
#define INT_UDN_TIMER   25
#define INT_IDN_AVAIL   26
#define INT_UDN_AVAIL   27
#define INT_IPI_3   28
#define INT_IPI_2   29
#define INT_IPI_1   30
#define INT_IPI_0   31
#define INT_PERF_COUNT   32
#define INT_AUX_PERF_COUNT   33
#define INT_INTCTRL_3   34
#define INT_INTCTRL_2   35
#define INT_INTCTRL_1   36
#define INT_INTCTRL_0   37
#define INT_BOOT_ACCESS   38
#define INT_WORLD_ACCESS   39
#define INT_I_ASID   40
#define INT_D_ASID   41
#define INT_DOUBLE_FAULT   42

#define NUM_INTERRUPTS 43

#ifndef __ASSEMBLER__
#define QUEUED_INTERRUPTS ( \
    (1ULL << INT_MEM_ERROR) | \
    (1ULL << INT_IDN_COMPLETE) | \
    (1ULL << INT_UDN_COMPLETE) | \
    (1ULL << INT_IDN_FIREWALL) | \
    (1ULL << INT_UDN_FIREWALL) | \
    (1ULL << INT_TILE_TIMER) | \
    (1ULL << INT_AUX_TILE_TIMER) | \
    (1ULL << INT_IDN_TIMER) | \
    (1ULL << INT_UDN_TIMER) | \
    (1ULL << INT_IDN_AVAIL) | \
    (1ULL << INT_UDN_AVAIL) | \
    (1ULL << INT_IPI_3) | \
    (1ULL << INT_IPI_2) | \
    (1ULL << INT_IPI_1) | \
    (1ULL << INT_IPI_0) | \
    (1ULL << INT_PERF_COUNT) | \
    (1ULL << INT_AUX_PERF_COUNT) | \
    (1ULL << INT_INTCTRL_3) | \
    (1ULL << INT_INTCTRL_2) | \
    (1ULL << INT_INTCTRL_1) | \
    (1ULL << INT_INTCTRL_0) | \
    (1ULL << INT_BOOT_ACCESS) | \
    (1ULL << INT_WORLD_ACCESS) | \
    (1ULL << INT_I_ASID) | \
    (1ULL << INT_D_ASID) | \
    (1ULL << INT_DOUBLE_FAULT) | \
    0)
#define NONQUEUED_INTERRUPTS ( \
    (1ULL << INT_SINGLE_STEP_3) | \
    (1ULL << INT_SINGLE_STEP_2) | \
    (1ULL << INT_SINGLE_STEP_1) | \
    (1ULL << INT_SINGLE_STEP_0) | \
    (1ULL << INT_ITLB_MISS) | \
    (1ULL << INT_ILL) | \
    (1ULL << INT_GPV) | \
    (1ULL << INT_IDN_ACCESS) | \
    (1ULL << INT_UDN_ACCESS) | \
    (1ULL << INT_SWINT_3) | \
    (1ULL << INT_SWINT_2) | \
    (1ULL << INT_SWINT_1) | \
    (1ULL << INT_SWINT_0) | \
    (1ULL << INT_ILL_TRANS) | \
    (1ULL << INT_UNALIGN_DATA) | \
    (1ULL << INT_DTLB_MISS) | \
    (1ULL << INT_DTLB_ACCESS) | \
    0)
#define CRITICAL_MASKED_INTERRUPTS ( \
    (1ULL << INT_MEM_ERROR) | \
    (1ULL << INT_SINGLE_STEP_3) | \
    (1ULL << INT_SINGLE_STEP_2) | \
    (1ULL << INT_SINGLE_STEP_1) | \
    (1ULL << INT_SINGLE_STEP_0) | \
    (1ULL << INT_IDN_COMPLETE) | \
    (1ULL << INT_UDN_COMPLETE) | \
    (1ULL << INT_IDN_FIREWALL) | \
    (1ULL << INT_UDN_FIREWALL) | \
    (1ULL << INT_TILE_TIMER) | \
    (1ULL << INT_AUX_TILE_TIMER) | \
    (1ULL << INT_IDN_TIMER) | \
    (1ULL << INT_UDN_TIMER) | \
    (1ULL << INT_IDN_AVAIL) | \
    (1ULL << INT_UDN_AVAIL) | \
    (1ULL << INT_IPI_3) | \
    (1ULL << INT_IPI_2) | \
    (1ULL << INT_IPI_1) | \
    (1ULL << INT_IPI_0) | \
    (1ULL << INT_PERF_COUNT) | \
    (1ULL << INT_AUX_PERF_COUNT) | \
    (1ULL << INT_INTCTRL_3) | \
    (1ULL << INT_INTCTRL_2) | \
    (1ULL << INT_INTCTRL_1) | \
    (1ULL << INT_INTCTRL_0) | \
    0)
#define CRITICAL_UNMASKED_INTERRUPTS ( \
    (1ULL << INT_ITLB_MISS) | \
    (1ULL << INT_ILL) | \
    (1ULL << INT_GPV) | \
    (1ULL << INT_IDN_ACCESS) | \
    (1ULL << INT_UDN_ACCESS) | \
    (1ULL << INT_SWINT_3) | \
    (1ULL << INT_SWINT_2) | \
    (1ULL << INT_SWINT_1) | \
    (1ULL << INT_SWINT_0) | \
    (1ULL << INT_ILL_TRANS) | \
    (1ULL << INT_UNALIGN_DATA) | \
    (1ULL << INT_DTLB_MISS) | \
    (1ULL << INT_DTLB_ACCESS) | \
    (1ULL << INT_BOOT_ACCESS) | \
    (1ULL << INT_WORLD_ACCESS) | \
    (1ULL << INT_I_ASID) | \
    (1ULL << INT_D_ASID) | \
    (1ULL << INT_DOUBLE_FAULT) | \
    0)
#define MASKABLE_INTERRUPTS ( \
    (1ULL << INT_MEM_ERROR) | \
    (1ULL << INT_SINGLE_STEP_3) | \
    (1ULL << INT_SINGLE_STEP_2) | \
    (1ULL << INT_SINGLE_STEP_1) | \
    (1ULL << INT_SINGLE_STEP_0) | \
    (1ULL << INT_IDN_COMPLETE) | \
    (1ULL << INT_UDN_COMPLETE) | \
    (1ULL << INT_IDN_FIREWALL) | \
    (1ULL << INT_UDN_FIREWALL) | \
    (1ULL << INT_TILE_TIMER) | \
    (1ULL << INT_AUX_TILE_TIMER) | \
    (1ULL << INT_IDN_TIMER) | \
    (1ULL << INT_UDN_TIMER) | \
    (1ULL << INT_IDN_AVAIL) | \
    (1ULL << INT_UDN_AVAIL) | \
    (1ULL << INT_IPI_3) | \
    (1ULL << INT_IPI_2) | \
    (1ULL << INT_IPI_1) | \
    (1ULL << INT_IPI_0) | \
    (1ULL << INT_PERF_COUNT) | \
    (1ULL << INT_AUX_PERF_COUNT) | \
    (1ULL << INT_INTCTRL_3) | \
    (1ULL << INT_INTCTRL_2) | \
    (1ULL << INT_INTCTRL_1) | \
    (1ULL << INT_INTCTRL_0) | \
    0)
#define UNMASKABLE_INTERRUPTS ( \
    (1ULL << INT_ITLB_MISS) | \
    (1ULL << INT_ILL) | \
    (1ULL << INT_GPV) | \
    (1ULL << INT_IDN_ACCESS) | \
    (1ULL << INT_UDN_ACCESS) | \
    (1ULL << INT_SWINT_3) | \
    (1ULL << INT_SWINT_2) | \
    (1ULL << INT_SWINT_1) | \
    (1ULL << INT_SWINT_0) | \
    (1ULL << INT_ILL_TRANS) | \
    (1ULL << INT_UNALIGN_DATA) | \
    (1ULL << INT_DTLB_MISS) | \
    (1ULL << INT_DTLB_ACCESS) | \
    (1ULL << INT_BOOT_ACCESS) | \
    (1ULL << INT_WORLD_ACCESS) | \
    (1ULL << INT_I_ASID) | \
    (1ULL << INT_D_ASID) | \
    (1ULL << INT_DOUBLE_FAULT) | \
    0)
#define SYNC_INTERRUPTS ( \
    (1ULL << INT_SINGLE_STEP_3) | \
    (1ULL << INT_SINGLE_STEP_2) | \
    (1ULL << INT_SINGLE_STEP_1) | \
    (1ULL << INT_SINGLE_STEP_0) | \
    (1ULL << INT_IDN_COMPLETE) | \
    (1ULL << INT_UDN_COMPLETE) | \
    (1ULL << INT_ITLB_MISS) | \
    (1ULL << INT_ILL) | \
    (1ULL << INT_GPV) | \
    (1ULL << INT_IDN_ACCESS) | \
    (1ULL << INT_UDN_ACCESS) | \
    (1ULL << INT_SWINT_3) | \
    (1ULL << INT_SWINT_2) | \
    (1ULL << INT_SWINT_1) | \
    (1ULL << INT_SWINT_0) | \
    (1ULL << INT_ILL_TRANS) | \
    (1ULL << INT_UNALIGN_DATA) | \
    (1ULL << INT_DTLB_MISS) | \
    (1ULL << INT_DTLB_ACCESS) | \
    0)
#define NON_SYNC_INTERRUPTS ( \
    (1ULL << INT_MEM_ERROR) | \
    (1ULL << INT_IDN_FIREWALL) | \
    (1ULL << INT_UDN_FIREWALL) | \
    (1ULL << INT_TILE_TIMER) | \
    (1ULL << INT_AUX_TILE_TIMER) | \
    (1ULL << INT_IDN_TIMER) | \
    (1ULL << INT_UDN_TIMER) | \
    (1ULL << INT_IDN_AVAIL) | \
    (1ULL << INT_UDN_AVAIL) | \
    (1ULL << INT_IPI_3) | \
    (1ULL << INT_IPI_2) | \
    (1ULL << INT_IPI_1) | \
    (1ULL << INT_IPI_0) | \
    (1ULL << INT_PERF_COUNT) | \
    (1ULL << INT_AUX_PERF_COUNT) | \
    (1ULL << INT_INTCTRL_3) | \
    (1ULL << INT_INTCTRL_2) | \
    (1ULL << INT_INTCTRL_1) | \
    (1ULL << INT_INTCTRL_0) | \
    (1ULL << INT_BOOT_ACCESS) | \
    (1ULL << INT_WORLD_ACCESS) | \
    (1ULL << INT_I_ASID) | \
    (1ULL << INT_D_ASID) | \
    (1ULL << INT_DOUBLE_FAULT) | \
    0)
#endif /* !__ASSEMBLER__ */
#endif /* !__ARCH_INTERRUPTS_H__ */
