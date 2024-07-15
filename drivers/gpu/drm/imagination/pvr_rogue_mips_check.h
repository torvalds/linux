/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_ROGUE_MIPS_CHECK_H
#define PVR_ROGUE_MIPS_CHECK_H

#include <linux/build_bug.h>

static_assert(offsetof(struct rogue_mips_tlb_entry, tlb_page_mask) == 0,
	      "offsetof(struct rogue_mips_tlb_entry, tlb_page_mask) incorrect");
static_assert(offsetof(struct rogue_mips_tlb_entry, tlb_hi) == 4,
	      "offsetof(struct rogue_mips_tlb_entry, tlb_hi) incorrect");
static_assert(offsetof(struct rogue_mips_tlb_entry, tlb_lo0) == 8,
	      "offsetof(struct rogue_mips_tlb_entry, tlb_lo0) incorrect");
static_assert(offsetof(struct rogue_mips_tlb_entry, tlb_lo1) == 12,
	      "offsetof(struct rogue_mips_tlb_entry, tlb_lo1) incorrect");
static_assert(sizeof(struct rogue_mips_tlb_entry) == 16,
	      "struct rogue_mips_tlb_entry is incorrect size");

static_assert(offsetof(struct rogue_mips_remap_entry, remap_addr_in) == 0,
	      "offsetof(struct rogue_mips_remap_entry, remap_addr_in) incorrect");
static_assert(offsetof(struct rogue_mips_remap_entry, remap_addr_out) == 4,
	      "offsetof(struct rogue_mips_remap_entry, remap_addr_out) incorrect");
static_assert(offsetof(struct rogue_mips_remap_entry, remap_region_size) == 8,
	      "offsetof(struct rogue_mips_remap_entry, remap_region_size) incorrect");
static_assert(sizeof(struct rogue_mips_remap_entry) == 12,
	      "struct rogue_mips_remap_entry is incorrect size");

static_assert(offsetof(struct rogue_mips_state, error_state) == 0,
	      "offsetof(struct rogue_mips_state, error_state) incorrect");
static_assert(offsetof(struct rogue_mips_state, error_epc) == 4,
	      "offsetof(struct rogue_mips_state, error_epc) incorrect");
static_assert(offsetof(struct rogue_mips_state, status_register) == 8,
	      "offsetof(struct rogue_mips_state, status_register) incorrect");
static_assert(offsetof(struct rogue_mips_state, cause_register) == 12,
	      "offsetof(struct rogue_mips_state, cause_register) incorrect");
static_assert(offsetof(struct rogue_mips_state, bad_register) == 16,
	      "offsetof(struct rogue_mips_state, bad_register) incorrect");
static_assert(offsetof(struct rogue_mips_state, epc) == 20,
	      "offsetof(struct rogue_mips_state, epc) incorrect");
static_assert(offsetof(struct rogue_mips_state, sp) == 24,
	      "offsetof(struct rogue_mips_state, sp) incorrect");
static_assert(offsetof(struct rogue_mips_state, debug) == 28,
	      "offsetof(struct rogue_mips_state, debug) incorrect");
static_assert(offsetof(struct rogue_mips_state, depc) == 32,
	      "offsetof(struct rogue_mips_state, depc) incorrect");
static_assert(offsetof(struct rogue_mips_state, bad_instr) == 36,
	      "offsetof(struct rogue_mips_state, bad_instr) incorrect");
static_assert(offsetof(struct rogue_mips_state, unmapped_address) == 40,
	      "offsetof(struct rogue_mips_state, unmapped_address) incorrect");
static_assert(offsetof(struct rogue_mips_state, tlb) == 44,
	      "offsetof(struct rogue_mips_state, tlb) incorrect");
static_assert(offsetof(struct rogue_mips_state, remap) == 300,
	      "offsetof(struct rogue_mips_state, remap) incorrect");
static_assert(sizeof(struct rogue_mips_state) == 684,
	      "struct rogue_mips_state is incorrect size");

#endif /* PVR_ROGUE_MIPS_CHECK_H */
