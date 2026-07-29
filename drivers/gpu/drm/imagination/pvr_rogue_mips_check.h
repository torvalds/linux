/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_ROGUE_MIPS_CHECK_H
#define PVR_ROGUE_MIPS_CHECK_H

#include "pvr_check.h"

OFFSET_CHECK(struct rogue_mips_tlb_entry, tlb_page_mask, 0);
OFFSET_CHECK(struct rogue_mips_tlb_entry, tlb_hi, 4);
OFFSET_CHECK(struct rogue_mips_tlb_entry, tlb_lo0, 8);
OFFSET_CHECK(struct rogue_mips_tlb_entry, tlb_lo1, 12);
SIZE_CHECK(struct rogue_mips_tlb_entry, 16);

OFFSET_CHECK(struct rogue_mips_remap_entry, remap_addr_in, 0);
OFFSET_CHECK(struct rogue_mips_remap_entry, remap_addr_out, 4);
OFFSET_CHECK(struct rogue_mips_remap_entry, remap_region_size, 8);
SIZE_CHECK(struct rogue_mips_remap_entry, 12);

OFFSET_CHECK(struct rogue_mips_state, error_state, 0);
OFFSET_CHECK(struct rogue_mips_state, error_epc, 4);
OFFSET_CHECK(struct rogue_mips_state, status_register, 8);
OFFSET_CHECK(struct rogue_mips_state, cause_register, 12);
OFFSET_CHECK(struct rogue_mips_state, bad_register, 16);
OFFSET_CHECK(struct rogue_mips_state, epc, 20);
OFFSET_CHECK(struct rogue_mips_state, sp, 24);
OFFSET_CHECK(struct rogue_mips_state, debug, 28);
OFFSET_CHECK(struct rogue_mips_state, depc, 32);
OFFSET_CHECK(struct rogue_mips_state, bad_instr, 36);
OFFSET_CHECK(struct rogue_mips_state, unmapped_address, 40);
OFFSET_CHECK(struct rogue_mips_state, tlb, 44);
OFFSET_CHECK(struct rogue_mips_state, remap, 300);
SIZE_CHECK(struct rogue_mips_state, 684);

#endif /* PVR_ROGUE_MIPS_CHECK_H */
