/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
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

#ifndef __ARCH_INTERRUPTS_H__
#define __ARCH_INTERRUPTS_H__

#ifndef __KERNEL__
/** Mask for an interrupt. */
/* Note: must handle breaking interrupts into high and low words manually. */
#define INT_MASK_LO(intno) (1 << (intno))
#define INT_MASK_HI(intno) (1 << ((intno) - 32))

#ifndef __ASSEMBLER__
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

#define INT_ITLB_MISS    0
#define INT_MEM_ERROR    1
#define INT_ILL    2
#define INT_GPV    3
#define INT_SN_ACCESS    4
#define INT_IDN_ACCESS    5
#define INT_UDN_ACCESS    6
#define INT_IDN_REFILL    7
#define INT_UDN_REFILL    8
#define INT_IDN_COMPLETE    9
#define INT_UDN_COMPLETE   10
#define INT_SWINT_3   11
#define INT_SWINT_2   12
#define INT_SWINT_1   13
#define INT_SWINT_0   14
#define INT_UNALIGN_DATA   15
#define INT_DTLB_MISS   16
#define INT_DTLB_ACCESS   17
#define INT_DMATLB_MISS   18
#define INT_DMATLB_ACCESS   19
#define INT_SNITLB_MISS   20
#define INT_SN_NOTIFY   21
#define INT_SN_FIREWALL   22
#define INT_IDN_FIREWALL   23
#define INT_UDN_FIREWALL   24
#define INT_TILE_TIMER   25
#define INT_IDN_TIMER   26
#define INT_UDN_TIMER   27
#define INT_DMA_NOTIFY   28
#define INT_IDN_CA   29
#define INT_UDN_CA   30
#define INT_IDN_AVAIL   31
#define INT_UDN_AVAIL   32
#define INT_PERF_COUNT   33
#define INT_INTCTRL_3   34
#define INT_INTCTRL_2   35
#define INT_INTCTRL_1   36
#define INT_INTCTRL_0   37
#define INT_BOOT_ACCESS   38
#define INT_WORLD_ACCESS   39
#define INT_I_ASID   40
#define INT_D_ASID   41
#define INT_DMA_ASID   42
#define INT_SNI_ASID   43
#define INT_DMA_CPL   44
#define INT_SN_CPL   45
#define INT_DOUBLE_FAULT   46
#define INT_SN_STATIC_ACCESS   47
#define INT_AUX_PERF_COUNT   48

#define NUM_INTERRUPTS 49

#ifndef __ASSEMBLER__
#define QUEUED_INTERRUPTS ( \
    (1ULL << INT_MEM_ERROR) | \
    (1ULL << INT_DMATLB_MISS) | \
    (1ULL << INT_DMATLB_ACCESS) | \
    (1ULL << INT_SNITLB_MISS) | \
    (1ULL << INT_SN_NOTIFY) | \
    (1ULL << INT_SN_FIREWALL) | \
    (1ULL << INT_IDN_FIREWALL) | \
    (1ULL << INT_UDN_FIREWALL) | \
    (1ULL << INT_TILE_TIMER) | \
    (1ULL << INT_IDN_TIMER) | \
    (1ULL << INT_UDN_TIMER) | \
    (1ULL << INT_DMA_NOTIFY) | \
    (1ULL << INT_IDN_CA) | \
    (1ULL << INT_UDN_CA) | \
    (1ULL << INT_IDN_AVAIL) | \
    (1ULL << INT_UDN_AVAIL) | \
    (1ULL << INT_PERF_COUNT) | \
    (1ULL << INT_INTCTRL_3) | \
    (1ULL << INT_INTCTRL_2) | \
    (1ULL << INT_INTCTRL_1) | \
    (1ULL << INT_INTCTRL_0) | \
    (1ULL << INT_BOOT_ACCESS) | \
    (1ULL << INT_WORLD_ACCESS) | \
    (1ULL << INT_I_ASID) | \
    (1ULL << INT_D_ASID) | \
    (1ULL << INT_DMA_ASID) | \
    (1ULL << INT_SNI_ASID) | \
    (1ULL << INT_DMA_CPL) | \
    (1ULL << INT_SN_CPL) | \
    (1ULL << INT_DOUBLE_FAULT) | \
    (1ULL << INT_AUX_PERF_COUNT) | \
    0)
#define NONQUEUED_INTERRUPTS ( \
    (1ULL << INT_ITLB_MISS) | \
    (1ULL << INT_ILL) | \
    (1ULL << INT_GPV) | \
    (1ULL << INT_SN_ACCESS) | \
    (1ULL << INT_IDN_ACCESS) | \
    (1ULL << INT_UDN_ACCESS) | \
    (1ULL << INT_IDN_REFILL) | \
    (1ULL << INT_UDN_REFILL) | \
    (1ULL << INT_IDN_COMPLETE) | \
    (1ULL << INT_UDN_COMPLETE) | \
    (1ULL << INT_SWINT_3) | \
    (1ULL << INT_SWINT_2) | \
    (1ULL << INT_SWINT_1) | \
    (1ULL << INT_SWINT_0) | \
    (1ULL << INT_UNALIGN_DATA) | \
    (1ULL << INT_DTLB_MISS) | \
    (1ULL << INT_DTLB_ACCESS) | \
    (1ULL << INT_SN_STATIC_ACCESS) | \
    0)
#define CRITICAL_MASKED_INTERRUPTS ( \
    (1ULL << INT_MEM_ERROR) | \
    (1ULL << INT_DMATLB_MISS) | \
    (1ULL << INT_DMATLB_ACCESS) | \
    (1ULL << INT_SNITLB_MISS) | \
    (1ULL << INT_SN_NOTIFY) | \
    (1ULL << INT_SN_FIREWALL) | \
    (1ULL << INT_IDN_FIREWALL) | \
    (1ULL << INT_UDN_FIREWALL) | \
    (1ULL << INT_TILE_TIMER) | \
    (1ULL << INT_IDN_TIMER) | \
    (1ULL << INT_UDN_TIMER) | \
    (1ULL << INT_DMA_NOTIFY) | \
    (1ULL << INT_IDN_CA) | \
    (1ULL << INT_UDN_CA) | \
    (1ULL << INT_IDN_AVAIL) | \
    (1ULL << INT_UDN_AVAIL) | \
    (1ULL << INT_PERF_COUNT) | \
    (1ULL << INT_INTCTRL_3) | \
    (1ULL << INT_INTCTRL_2) | \
    (1ULL << INT_INTCTRL_1) | \
    (1ULL << INT_INTCTRL_0) | \
    (1ULL << INT_AUX_PERF_COUNT) | \
    0)
#define CRITICAL_UNMASKED_INTERRUPTS ( \
    (1ULL << INT_ITLB_MISS) | \
    (1ULL << INT_ILL) | \
    (1ULL << INT_GPV) | \
    (1ULL << INT_SN_ACCESS) | \
    (1ULL << INT_IDN_ACCESS) | \
    (1ULL << INT_UDN_ACCESS) | \
    (1ULL << INT_IDN_REFILL) | \
    (1ULL << INT_UDN_REFILL) | \
    (1ULL << INT_IDN_COMPLETE) | \
    (1ULL << INT_UDN_COMPLETE) | \
    (1ULL << INT_SWINT_3) | \
    (1ULL << INT_SWINT_2) | \
    (1ULL << INT_SWINT_1) | \
    (1ULL << INT_SWINT_0) | \
    (1ULL << INT_UNALIGN_DATA) | \
    (1ULL << INT_DTLB_MISS) | \
    (1ULL << INT_DTLB_ACCESS) | \
    (1ULL << INT_BOOT_ACCESS) | \
    (1ULL << INT_WORLD_ACCESS) | \
    (1ULL << INT_I_ASID) | \
    (1ULL << INT_D_ASID) | \
    (1ULL << INT_DMA_ASID) | \
    (1ULL << INT_SNI_ASID) | \
    (1ULL << INT_DMA_CPL) | \
    (1ULL << INT_SN_CPL) | \
    (1ULL << INT_DOUBLE_FAULT) | \
    (1ULL << INT_SN_STATIC_ACCESS) | \
    0)
#define MASKABLE_INTERRUPTS ( \
    (1ULL << INT_MEM_ERROR) | \
    (1ULL << INT_IDN_REFILL) | \
    (1ULL << INT_UDN_REFILL) | \
    (1ULL << INT_IDN_COMPLETE) | \
    (1ULL << INT_UDN_COMPLETE) | \
    (1ULL << INT_DMATLB_MISS) | \
    (1ULL << INT_DMATLB_ACCESS) | \
    (1ULL << INT_SNITLB_MISS) | \
    (1ULL << INT_SN_NOTIFY) | \
    (1ULL << INT_SN_FIREWALL) | \
    (1ULL << INT_IDN_FIREWALL) | \
    (1ULL << INT_UDN_FIREWALL) | \
    (1ULL << INT_TILE_TIMER) | \
    (1ULL << INT_IDN_TIMER) | \
    (1ULL << INT_UDN_TIMER) | \
    (1ULL << INT_DMA_NOTIFY) | \
    (1ULL << INT_IDN_CA) | \
    (1ULL << INT_UDN_CA) | \
    (1ULL << INT_IDN_AVAIL) | \
    (1ULL << INT_UDN_AVAIL) | \
    (1ULL << INT_PERF_COUNT) | \
    (1ULL << INT_INTCTRL_3) | \
    (1ULL << INT_INTCTRL_2) | \
    (1ULL << INT_INTCTRL_1) | \
    (1ULL << INT_INTCTRL_0) | \
    (1ULL << INT_AUX_PERF_COUNT) | \
    0)
#define UNMASKABLE_INTERRUPTS ( \
    (1ULL << INT_ITLB_MISS) | \
    (1ULL << INT_ILL) | \
    (1ULL << INT_GPV) | \
    (1ULL << INT_SN_ACCESS) | \
    (1ULL << INT_IDN_ACCESS) | \
    (1ULL << INT_UDN_ACCESS) | \
    (1ULL << INT_SWINT_3) | \
    (1ULL << INT_SWINT_2) | \
    (1ULL << INT_SWINT_1) | \
    (1ULL << INT_SWINT_0) | \
    (1ULL << INT_UNALIGN_DATA) | \
    (1ULL << INT_DTLB_MISS) | \
    (1ULL << INT_DTLB_ACCESS) | \
    (1ULL << INT_BOOT_ACCESS) | \
    (1ULL << INT_WORLD_ACCESS) | \
    (1ULL << INT_I_ASID) | \
    (1ULL << INT_D_ASID) | \
    (1ULL << INT_DMA_ASID) | \
    (1ULL << INT_SNI_ASID) | \
    (1ULL << INT_DMA_CPL) | \
    (1ULL << INT_SN_CPL) | \
    (1ULL << INT_DOUBLE_FAULT) | \
    (1ULL << INT_SN_STATIC_ACCESS) | \
    0)
#define SYNC_INTERRUPTS ( \
    (1ULL << INT_ITLB_MISS) | \
    (1ULL << INT_ILL) | \
    (1ULL << INT_GPV) | \
    (1ULL << INT_SN_ACCESS) | \
    (1ULL << INT_IDN_ACCESS) | \
    (1ULL << INT_UDN_ACCESS) | \
    (1ULL << INT_IDN_REFILL) | \
    (1ULL << INT_UDN_REFILL) | \
    (1ULL << INT_IDN_COMPLETE) | \
    (1ULL << INT_UDN_COMPLETE) | \
    (1ULL << INT_SWINT_3) | \
    (1ULL << INT_SWINT_2) | \
    (1ULL << INT_SWINT_1) | \
    (1ULL << INT_SWINT_0) | \
    (1ULL << INT_UNALIGN_DATA) | \
    (1ULL << INT_DTLB_MISS) | \
    (1ULL << INT_DTLB_ACCESS) | \
    (1ULL << INT_SN_STATIC_ACCESS) | \
    0)
#define NON_SYNC_INTERRUPTS ( \
    (1ULL << INT_MEM_ERROR) | \
    (1ULL << INT_DMATLB_MISS) | \
    (1ULL << INT_DMATLB_ACCESS) | \
    (1ULL << INT_SNITLB_MISS) | \
    (1ULL << INT_SN_NOTIFY) | \
    (1ULL << INT_SN_FIREWALL) | \
    (1ULL << INT_IDN_FIREWALL) | \
    (1ULL << INT_UDN_FIREWALL) | \
    (1ULL << INT_TILE_TIMER) | \
    (1ULL << INT_IDN_TIMER) | \
    (1ULL << INT_UDN_TIMER) | \
    (1ULL << INT_DMA_NOTIFY) | \
    (1ULL << INT_IDN_CA) | \
    (1ULL << INT_UDN_CA) | \
    (1ULL << INT_IDN_AVAIL) | \
    (1ULL << INT_UDN_AVAIL) | \
    (1ULL << INT_PERF_COUNT) | \
    (1ULL << INT_INTCTRL_3) | \
    (1ULL << INT_INTCTRL_2) | \
    (1ULL << INT_INTCTRL_1) | \
    (1ULL << INT_INTCTRL_0) | \
    (1ULL << INT_BOOT_ACCESS) | \
    (1ULL << INT_WORLD_ACCESS) | \
    (1ULL << INT_I_ASID) | \
    (1ULL << INT_D_ASID) | \
    (1ULL << INT_DMA_ASID) | \
    (1ULL << INT_SNI_ASID) | \
    (1ULL << INT_DMA_CPL) | \
    (1ULL << INT_SN_CPL) | \
    (1ULL << INT_DOUBLE_FAULT) | \
    (1ULL << INT_AUX_PERF_COUNT) | \
    0)
#endif /* !__ASSEMBLER__ */
#endif /* !__ARCH_INTERRUPTS_H__ */
