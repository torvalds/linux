/*
 * Copyright (c) 2013-2018, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "pt_ild.h"
#include "pti-imm-defs.h"
#include "pti-imm.h"
#include "pti-modrm-defs.h"
#include "pti-modrm.h"
#include "pti-disp-defs.h"
#include "pti-disp.h"

#include <string.h>

/* SET UP 3 TABLES */

static uint8_t has_disp_regular[4][4][8];

static void init_has_disp_regular_table(void)
{
	uint8_t mod, rm;

	memset(has_disp_regular, 0, sizeof(has_disp_regular));

	/*fill eamode16 */
	has_disp_regular[ptem_16bit][0][6] = 2;
	for (rm = 0; rm < 8; rm++)
		for (mod = 1; mod <= 2; mod++)
			has_disp_regular[ptem_16bit][mod][rm] = mod;

	/*fill eamode32/64 */
	has_disp_regular[ptem_32bit][0][5] = 4;
	has_disp_regular[ptem_64bit][0][5] = 4;
	for (rm = 0; rm < 8; rm++) {
		has_disp_regular[ptem_32bit][1][rm] = 1;
		has_disp_regular[ptem_32bit][2][rm] = 4;

		has_disp_regular[ptem_64bit][1][rm] = 1;
		has_disp_regular[ptem_64bit][2][rm] = 4;
	}
}

static uint8_t eamode_table[2][4];

static void init_eamode_table(void)
{
	eamode_table[0][ptem_unknown] = ptem_unknown;
	eamode_table[0][ptem_16bit] = ptem_16bit;
	eamode_table[0][ptem_32bit] = ptem_32bit;
	eamode_table[0][ptem_64bit] = ptem_64bit;

	eamode_table[1][ptem_unknown] = ptem_unknown;
	eamode_table[1][ptem_16bit] = ptem_32bit;
	eamode_table[1][ptem_32bit] = ptem_16bit;
	eamode_table[1][ptem_64bit] = ptem_32bit;
}

static uint8_t has_sib_table[4][4][8];

static void init_has_sib_table(void)
{
	uint8_t mod;

	memset(has_sib_table, 0, sizeof(has_sib_table));

	/*for eamode32/64 there is sib byte for mod!=3 and rm==4 */
	for (mod = 0; mod <= 2; mod++) {
		has_sib_table[ptem_32bit][mod][4] = 1;
		has_sib_table[ptem_64bit][mod][4] = 1;
	}
}

/* SOME ACCESSORS */

static inline uint8_t get_byte(const struct pt_ild *ild, uint8_t i)
{
	return ild->itext[i];
}

static inline uint8_t const *get_byte_ptr(const struct pt_ild *ild, uint8_t i)
{
	return ild->itext + i;
}

static inline int mode_64b(const struct pt_ild *ild)
{
	return ild->mode == ptem_64bit;
}

static inline int mode_32b(const struct pt_ild *ild)
{
	return ild->mode == ptem_32bit;
}

static inline int bits_match(uint8_t x, uint8_t mask, uint8_t target)
{
	return (x & mask) == target;
}

static inline enum pt_exec_mode
pti_get_nominal_eosz_non64(const struct pt_ild *ild)
{
	if (mode_32b(ild)) {
		if (ild->u.s.osz)
			return ptem_16bit;
		return ptem_32bit;
	}
	if (ild->u.s.osz)
		return ptem_32bit;
	return ptem_16bit;
}

static inline enum pt_exec_mode
pti_get_nominal_eosz(const struct pt_ild *ild)
{
	if (mode_64b(ild)) {
		if (ild->u.s.rex_w)
			return ptem_64bit;
		if (ild->u.s.osz)
			return ptem_16bit;
		return ptem_32bit;
	}
	return pti_get_nominal_eosz_non64(ild);
}

static inline enum pt_exec_mode
pti_get_nominal_eosz_df64(const struct pt_ild *ild)
{
	if (mode_64b(ild)) {
		if (ild->u.s.rex_w)
			return ptem_64bit;
		if (ild->u.s.osz)
			return ptem_16bit;
		/* only this next line of code is different relative
		   to pti_get_nominal_eosz(), above */
		return ptem_64bit;
	}
	return pti_get_nominal_eosz_non64(ild);
}

static inline enum pt_exec_mode
pti_get_nominal_easz_non64(const struct pt_ild *ild)
{
	if (mode_32b(ild)) {
		if (ild->u.s.asz)
			return ptem_16bit;
		return ptem_32bit;
	}
	if (ild->u.s.asz)
		return ptem_32bit;
	return ptem_16bit;
}

static inline enum pt_exec_mode
pti_get_nominal_easz(const struct pt_ild *ild)
{
	if (mode_64b(ild)) {
		if (ild->u.s.asz)
			return ptem_32bit;
		return ptem_64bit;
	}
	return pti_get_nominal_easz_non64(ild);
}

static inline int resolve_z(uint8_t *pbytes, enum pt_exec_mode eosz)
{
	static const uint8_t bytes[] = { 2, 4, 4 };
	unsigned int idx;

	if (!pbytes)
		return -pte_internal;

	idx = (unsigned int) eosz - 1;
	if (sizeof(bytes) <= idx)
		return -pte_bad_insn;

	*pbytes = bytes[idx];
	return 0;
}

static inline int resolve_v(uint8_t *pbytes, enum pt_exec_mode eosz)
{
	static const uint8_t bytes[] = { 2, 4, 8 };
	unsigned int idx;

	if (!pbytes)
		return -pte_internal;

	idx = (unsigned int) eosz - 1;
	if (sizeof(bytes) <= idx)
		return -pte_bad_insn;

	*pbytes = bytes[idx];
	return 0;
}

/*  DECODERS */

static int set_imm_bytes(struct pt_ild *ild)
{
	/*: set ild->imm1_bytes and  ild->imm2_bytes for maps 0/1 */
	static uint8_t const *const map_map[] = {
		/* map 0 */ imm_bytes_map_0x0,
		/* map 1 */ imm_bytes_map_0x0F
	};
	uint8_t map, imm_code;

	if (!ild)
		return -pte_internal;

	map = ild->map;

	if ((sizeof(map_map) / sizeof(*map_map)) <= map)
		return 0;

	imm_code = map_map[map][ild->nominal_opcode];
	switch (imm_code) {
	case PTI_IMM_NONE:
	case PTI_0_IMM_WIDTH_CONST_l2:
	default:
		return 0;

	case PTI_UIMM8_IMM_WIDTH_CONST_l2:
		ild->imm1_bytes = 1;
		return 0;

	case PTI_SIMM8_IMM_WIDTH_CONST_l2:
		ild->imm1_bytes = 1;
		return 0;

	case PTI_SIMMz_IMM_WIDTH_OSZ_NONTERM_EOSZ_l2:
		/* SIMMz(eosz) */
		return resolve_z(&ild->imm1_bytes, pti_get_nominal_eosz(ild));

	case PTI_UIMMv_IMM_WIDTH_OSZ_NONTERM_EOSZ_l2:
		/* UIMMv(eosz) */
		return resolve_v(&ild->imm1_bytes, pti_get_nominal_eosz(ild));

	case PTI_UIMM16_IMM_WIDTH_CONST_l2:
		ild->imm1_bytes = 2;
		return 0;

	case PTI_SIMMz_IMM_WIDTH_OSZ_NONTERM_DF64_EOSZ_l2:
		/* push defaults to eosz64 in 64b mode, then uses SIMMz */
		return resolve_z(&ild->imm1_bytes,
				 pti_get_nominal_eosz_df64(ild));

	case PTI_RESOLVE_BYREG_IMM_WIDTH_map0x0_op0xf7_l1:
		if (ild->map == PTI_MAP_0 && pti_get_modrm_reg(ild) < 2) {
			return resolve_z(&ild->imm1_bytes,
					 pti_get_nominal_eosz(ild));
		}
		return 0;

	case PTI_RESOLVE_BYREG_IMM_WIDTH_map0x0_op0xc7_l1:
		if (ild->map == PTI_MAP_0 && pti_get_modrm_reg(ild) == 0) {
			return resolve_z(&ild->imm1_bytes,
					 pti_get_nominal_eosz(ild));
		}
		return 0;

	case PTI_RESOLVE_BYREG_IMM_WIDTH_map0x0_op0xf6_l1:
		if (ild->map == PTI_MAP_0 && pti_get_modrm_reg(ild) < 2)
			ild->imm1_bytes = 1;

		return 0;

	case PTI_IMM_hasimm_map0x0_op0xc8_l1:
		if (ild->map == PTI_MAP_0) {
			/*enter -> imm1=2, imm2=1 */
			ild->imm1_bytes = 2;
			ild->imm2_bytes = 1;
		}
		return 0;

	case PTI_IMM_hasimm_map0x0F_op0x78_l1:
		/* AMD SSE4a (insertq/extrq use  osz/f2) vs vmread
		 * (no prefixes)
		 */
		if (ild->map == PTI_MAP_1) {
			if (ild->u.s.osz || ild->u.s.last_f2f3 == 2) {
				ild->imm1_bytes = 1;
				ild->imm2_bytes = 1;
			}
		}
		return 0;
	}
}

static int imm_dec(struct pt_ild *ild, uint8_t length)
{
	int errcode;

	if (!ild)
		return -pte_internal;

	if (ild->map == PTI_MAP_AMD3DNOW) {
		if (ild->max_bytes <= length)
			return -pte_bad_insn;

		ild->nominal_opcode = get_byte(ild, length);
		return length + 1;
	}

	errcode = set_imm_bytes(ild);
	if (errcode < 0)
		return errcode;

	length += ild->imm1_bytes;
	length += ild->imm2_bytes;
	if (ild->max_bytes < length)
		return -pte_bad_insn;

	return length;
}

static int compute_disp_dec(struct pt_ild *ild)
{
	/* set ild->disp_bytes for maps 0 and 1. */
	static uint8_t const *const map_map[] = {
		/* map 0 */ disp_bytes_map_0x0,
		/* map 1 */ disp_bytes_map_0x0F
	};
	uint8_t map, disp_kind;

	if (!ild)
		return -pte_internal;

	if (0 < ild->disp_bytes)
		return 0;

	map = ild->map;

	if ((sizeof(map_map) / sizeof(*map_map)) <= map)
		return 0;

	disp_kind = map_map[map][ild->nominal_opcode];
	switch (disp_kind) {
	case PTI_DISP_NONE:
		ild->disp_bytes = 0;
		return 0;

	case PTI_PRESERVE_DEFAULT:
		/* nothing to do */
		return 0;

	case PTI_BRDISP8:
		ild->disp_bytes = 1;
		return 0;

	case PTI_DISP_BUCKET_0_l1:
		/* BRDISPz(eosz) for 16/32 modes, and BRDISP32 for 64b mode */
		if (mode_64b(ild)) {
			ild->disp_bytes = 4;
			return 0;
		}

		return resolve_z(&ild->disp_bytes,
				 pti_get_nominal_eosz(ild));

	case PTI_MEMDISPv_DISP_WIDTH_ASZ_NONTERM_EASZ_l2:
		/* MEMDISPv(easz) */
		return resolve_v(&ild->disp_bytes, pti_get_nominal_easz(ild));

	case PTI_BRDISPz_BRDISP_WIDTH_OSZ_NONTERM_EOSZ_l2:
		/* BRDISPz(eosz) for 16/32/64 modes */
		return resolve_z(&ild->disp_bytes, pti_get_nominal_eosz(ild));

	case PTI_RESOLVE_BYREG_DISP_map0x0_op0xc7_l1:
		/* reg=0 -> preserve, reg=7 -> BRDISPz(eosz) */
		if (ild->map == PTI_MAP_0 && pti_get_modrm_reg(ild) == 7) {
			return resolve_z(&ild->disp_bytes,
					 pti_get_nominal_eosz(ild));
		}
		return 0;

	default:
		return -pte_bad_insn;
	}
}

static int disp_dec(struct pt_ild *ild, uint8_t length)
{
	uint8_t disp_bytes;
	int errcode;

	if (!ild)
		return -pte_internal;

	errcode = compute_disp_dec(ild);
	if (errcode < 0)
		return errcode;

	disp_bytes = ild->disp_bytes;
	if (disp_bytes == 0)
		return imm_dec(ild, length);

	if (length + disp_bytes > ild->max_bytes)
		return -pte_bad_insn;

	/*Record only position; must be able to re-read itext bytes for actual
	   value. (SMC/CMC issue). */
	ild->disp_pos = length;

	return imm_dec(ild, length + disp_bytes);
}

static int sib_dec(struct pt_ild *ild, uint8_t length)
{
	uint8_t sib;

	if (!ild)
		return -pte_internal;

	if (ild->max_bytes <= length)
		return -pte_bad_insn;

	sib = get_byte(ild, length);
	if ((sib & 0x07) == 0x05 && pti_get_modrm_mod(ild) == 0)
		ild->disp_bytes = 4;

	return disp_dec(ild, length + 1);
}

static int modrm_dec(struct pt_ild *ild, uint8_t length)
{
	static uint8_t const *const has_modrm_2d[2] = {
		has_modrm_map_0x0,
		has_modrm_map_0x0F
	};
	int has_modrm = PTI_MODRM_FALSE;
	pti_map_enum_t map;

	if (!ild)
		return -pte_internal;

	map = pti_get_map(ild);
	if (map >= PTI_MAP_2)
		has_modrm = PTI_MODRM_TRUE;
	else
		has_modrm = has_modrm_2d[map][ild->nominal_opcode];

	if (has_modrm == PTI_MODRM_FALSE || has_modrm == PTI_MODRM_UNDEF)
		return disp_dec(ild, length);

	/* really >= here because we have not eaten the byte yet */
	if (length >= ild->max_bytes)
		return -pte_bad_insn;

	ild->modrm_byte = get_byte(ild, length);

	if (has_modrm != PTI_MODRM_IGNORE_MOD) {
		/* set disp_bytes and sib using simple tables */

		uint8_t eamode = eamode_table[ild->u.s.asz][ild->mode];
		uint8_t mod = (uint8_t) pti_get_modrm_mod(ild);
		uint8_t rm = (uint8_t) pti_get_modrm_rm(ild);
		uint8_t has_sib;

		ild->disp_bytes = has_disp_regular[eamode][mod][rm];

		has_sib = has_sib_table[eamode][mod][rm];
		if (has_sib)
			return sib_dec(ild, length + 1);
	}

	return disp_dec(ild, length + 1);
}

static inline int get_next_as_opcode(struct pt_ild *ild, uint8_t length)
{
	if (!ild)
		return -pte_internal;

	if (ild->max_bytes <= length)
		return -pte_bad_insn;

	ild->nominal_opcode = get_byte(ild, length);

	return modrm_dec(ild, length + 1);
}

static int opcode_dec(struct pt_ild *ild, uint8_t length)
{
	uint8_t b, m;

	if (!ild)
		return -pte_internal;

	/*no need to check max_bytes - it was checked in previous scanners */
	b = get_byte(ild, length);
	if (b != 0x0F) {	/* 1B opcodes, map 0 */
		ild->map = PTI_MAP_0;
		ild->nominal_opcode = b;

		return modrm_dec(ild, length + 1);
	}

	length++;		/* eat the 0x0F */

	if (ild->max_bytes <= length)
		return -pte_bad_insn;

	/* 0x0F opcodes MAPS 1,2,3 */
	m = get_byte(ild, length);
	if (m == 0x38) {
		ild->map = PTI_MAP_2;

		return get_next_as_opcode(ild, length + 1);
	} else if (m == 0x3A) {
		ild->map = PTI_MAP_3;
		ild->imm1_bytes = 1;

		return get_next_as_opcode(ild, length + 1);
	} else if (bits_match(m, 0xf8, 0x38)) {
		ild->map = PTI_MAP_INVALID;

		return get_next_as_opcode(ild, length + 1);
	} else if (m == 0x0F) {	/* 3dNow */
		ild->map = PTI_MAP_AMD3DNOW;
		ild->imm1_bytes = 1;
		/* real opcode is in immediate later on, but we need an
		 * opcode now. */
		ild->nominal_opcode = 0x0F;

		return modrm_dec(ild, length + 1);
	} else {	/* map 1 (simple two byte opcodes) */
		ild->nominal_opcode = m;
		ild->map = PTI_MAP_1;

		return modrm_dec(ild, length + 1);
	}
}

typedef int (*prefix_decoder)(struct pt_ild *ild, uint8_t length, uint8_t rex);
static prefix_decoder prefix_table[256];

static inline int prefix_decode(struct pt_ild *ild, uint8_t length, uint8_t rex)
{
	uint8_t byte;

	if (!ild)
		return -pte_internal;

	if (ild->max_bytes <= length)
		return -pte_bad_insn;

	byte = get_byte(ild, length);

	return prefix_table[byte](ild, length, rex);
}

static inline int prefix_next(struct pt_ild *ild, uint8_t length, uint8_t rex)
{
	return prefix_decode(ild, length + 1, rex);
}

static int prefix_osz(struct pt_ild *ild, uint8_t length, uint8_t rex)
{
	(void) rex;

	if (!ild)
		return -pte_internal;

	ild->u.s.osz = 1;

	return prefix_next(ild, length, 0);
}

static int prefix_asz(struct pt_ild *ild, uint8_t length, uint8_t rex)
{
	(void) rex;

	if (!ild)
		return -pte_internal;

	ild->u.s.asz = 1;

	return prefix_next(ild, length, 0);
}

static int prefix_lock(struct pt_ild *ild, uint8_t length, uint8_t rex)
{
	(void) rex;

	if (!ild)
		return -pte_internal;

	ild->u.s.lock = 1;

	return prefix_next(ild, length, 0);
}

static int prefix_f2(struct pt_ild *ild, uint8_t length, uint8_t rex)
{
	(void) rex;

	if (!ild)
		return -pte_internal;

	ild->u.s.f2 = 1;
	ild->u.s.last_f2f3 = 2;

	return prefix_next(ild, length, 0);
}

static int prefix_f3(struct pt_ild *ild, uint8_t length, uint8_t rex)
{
	(void) rex;

	if (!ild)
		return -pte_internal;

	ild->u.s.f3 = 1;
	ild->u.s.last_f2f3 = 3;

	return prefix_next(ild, length, 0);
}

static int prefix_ignore(struct pt_ild *ild, uint8_t length, uint8_t rex)
{
	(void) rex;

	return prefix_next(ild, length, 0);
}

static int prefix_done(struct pt_ild *ild, uint8_t length, uint8_t rex)
{
	if (!ild)
		return -pte_internal;

	if (rex & 0x04)
		ild->u.s.rex_r = 1;
	if (rex & 0x08)
		ild->u.s.rex_w = 1;

	return opcode_dec(ild, length);
}

static int prefix_rex(struct pt_ild *ild, uint8_t length, uint8_t rex)
{
	(void) rex;

	if (!ild)
		return -pte_internal;

	if (mode_64b(ild))
		return prefix_next(ild, length, get_byte(ild, length));
	else
		return opcode_dec(ild, length);
}

static inline int prefix_vex_done(struct pt_ild *ild, uint8_t length)
{
	if (!ild)
		return -pte_internal;

	ild->nominal_opcode = get_byte(ild, length);

	return modrm_dec(ild, length + 1);
}

static int prefix_vex_c5(struct pt_ild *ild, uint8_t length, uint8_t rex)
{
	uint8_t max_bytes;
	uint8_t p1;

	(void) rex;

	if (!ild)
		return -pte_internal;

	max_bytes = ild->max_bytes;

	/* Read the next byte to validate that this is indeed VEX. */
	if (max_bytes <= (length + 1))
		return -pte_bad_insn;

	p1 = get_byte(ild, length + 1);

	/* If p1[7:6] is not 11b in non-64-bit mode, this is LDS, not VEX. */
	if (!mode_64b(ild) && !bits_match(p1, 0xc0, 0xc0))
		return opcode_dec(ild, length);

	/* We need at least 3 bytes
	 * - 2 for the VEX prefix and payload and
	 * - 1 for the opcode.
	 */
	if (max_bytes < (length + 3))
		return -pte_bad_insn;

	ild->u.s.vex = 1;
	if (p1 & 0x80)
		ild->u.s.rex_r = 1;

	ild->map = PTI_MAP_1;

	/* Eat the VEX. */
	length += 2;
	return prefix_vex_done(ild, length);
}

static int prefix_vex_c4(struct pt_ild *ild, uint8_t length, uint8_t rex)
{
	uint8_t max_bytes;
	uint8_t p1, p2, map;

	(void) rex;

	if (!ild)
		return -pte_internal;

	max_bytes = ild->max_bytes;

	/* Read the next byte to validate that this is indeed VEX. */
	if (max_bytes <= (length + 1))
		return -pte_bad_insn;

	p1 = get_byte(ild, length + 1);

	/* If p1[7:6] is not 11b in non-64-bit mode, this is LES, not VEX. */
	if (!mode_64b(ild) && !bits_match(p1, 0xc0, 0xc0))
		return opcode_dec(ild, length);

	/* We need at least 4 bytes
	 * - 3 for the VEX prefix and payload and
	 * - 1 for the opcode.
	 */
	if (max_bytes < (length + 4))
		return -pte_bad_insn;

	p2 = get_byte(ild, length + 2);

	ild->u.s.vex = 1;
	if (p1 & 0x80)
		ild->u.s.rex_r = 1;
	if (p2 & 0x80)
		ild->u.s.rex_w = 1;

	map = p1 & 0x1f;
	if (PTI_MAP_INVALID <= map)
		return -pte_bad_insn;

	ild->map = map;
	if (map == PTI_MAP_3)
		ild->imm1_bytes = 1;

	/* Eat the VEX. */
	length += 3;
	return prefix_vex_done(ild, length);
}

static int prefix_evex(struct pt_ild *ild, uint8_t length, uint8_t rex)
{
	uint8_t max_bytes;
	uint8_t p1, p2, map;

	(void) rex;

	if (!ild)
		return -pte_internal;

	max_bytes = ild->max_bytes;

	/* Read the next byte to validate that this is indeed EVEX. */
	if (max_bytes <= (length + 1))
		return -pte_bad_insn;

	p1 = get_byte(ild, length + 1);

	/* If p1[7:6] is not 11b in non-64-bit mode, this is BOUND, not EVEX. */
	if (!mode_64b(ild) && !bits_match(p1, 0xc0, 0xc0))
		return opcode_dec(ild, length);

	/* We need at least 5 bytes
	 * - 4 for the EVEX prefix and payload and
	 * - 1 for the opcode.
	 */
	if (max_bytes < (length + 5))
		return -pte_bad_insn;

	p2 = get_byte(ild, length + 2);

	ild->u.s.vex = 1;
	if (p1 & 0x80)
		ild->u.s.rex_r = 1;
	if (p2 & 0x80)
		ild->u.s.rex_w = 1;

	map = p1 & 0x03;
	ild->map = map;

	if (map == PTI_MAP_3)
		ild->imm1_bytes = 1;

	/* Eat the EVEX. */
	length += 4;
	return prefix_vex_done(ild, length);
}

static void init_prefix_table(void)
{
	unsigned int byte;

	for (byte = 0; byte <= 0xff; ++byte)
		prefix_table[byte] = prefix_done;

	prefix_table[0x66] = prefix_osz;
	prefix_table[0x67] = prefix_asz;

	/* Segment prefixes. */
	prefix_table[0x2e] = prefix_ignore;
	prefix_table[0x3e] = prefix_ignore;
	prefix_table[0x26] = prefix_ignore;
	prefix_table[0x36] = prefix_ignore;
	prefix_table[0x64] = prefix_ignore;
	prefix_table[0x65] = prefix_ignore;

	prefix_table[0xf0] = prefix_lock;
	prefix_table[0xf2] = prefix_f2;
	prefix_table[0xf3] = prefix_f3;

	for (byte = 0x40; byte <= 0x4f; ++byte)
		prefix_table[byte] = prefix_rex;

	prefix_table[0xc4] = prefix_vex_c4;
	prefix_table[0xc5] = prefix_vex_c5;
	prefix_table[0x62] = prefix_evex;
}

static int decode(struct pt_ild *ild)
{
	return prefix_decode(ild, 0, 0);
}

static int set_branch_target(struct pt_insn_ext *iext, const struct pt_ild *ild)
{
	if (!iext || !ild)
		return -pte_internal;

	iext->variant.branch.is_direct = 1;

	if (ild->disp_bytes == 1) {
		const int8_t *b = (const int8_t *)
			get_byte_ptr(ild, ild->disp_pos);

		iext->variant.branch.displacement = *b;
	} else if (ild->disp_bytes == 2) {
		const int16_t *w = (const int16_t *)
			get_byte_ptr(ild, ild->disp_pos);

		iext->variant.branch.displacement = *w;
	} else if (ild->disp_bytes == 4) {
		const int32_t *d = (const int32_t *)
			get_byte_ptr(ild, ild->disp_pos);

		iext->variant.branch.displacement = *d;
	} else
		return -pte_bad_insn;

	return 0;
}

/*  MAIN ENTRY POINTS */

void pt_ild_init(void)
{	/* initialization */
	init_has_disp_regular_table();
	init_has_sib_table();
	init_eamode_table();
	init_prefix_table();
}

static int pt_instruction_length_decode(struct pt_ild *ild)
{
	if (!ild)
		return -pte_internal;

	ild->u.i = 0;
	ild->imm1_bytes = 0;
	ild->imm2_bytes = 0;
	ild->disp_bytes = 0;
	ild->modrm_byte = 0;
	ild->map = PTI_MAP_INVALID;

	if (!ild->mode)
		return -pte_bad_insn;

	return decode(ild);
}

static int pt_instruction_decode(struct pt_insn *insn, struct pt_insn_ext *iext,
				 const struct pt_ild *ild)
{
	uint8_t opcode, map;

	if (!iext || !ild)
		return -pte_internal;

	iext->iclass = PTI_INST_INVALID;
	memset(&iext->variant, 0, sizeof(iext->variant));

	insn->iclass = ptic_other;

	opcode = ild->nominal_opcode;
	map = ild->map;

	if (map > PTI_MAP_1)
		return 0;	/* uninteresting */
	if (ild->u.s.vex)
		return 0;	/* uninteresting */

	/* PTI_INST_JCC,   70...7F, 0F (0x80...0x8F) */
	if (opcode >= 0x70 && opcode <= 0x7F) {
		if (map == PTI_MAP_0) {
			insn->iclass = ptic_cond_jump;
			iext->iclass = PTI_INST_JCC;

			return set_branch_target(iext, ild);
		}
		return 0;
	}
	if (opcode >= 0x80 && opcode <= 0x8F) {
		if (map == PTI_MAP_1) {
			insn->iclass = ptic_cond_jump;
			iext->iclass = PTI_INST_JCC;

			return set_branch_target(iext, ild);
		}
		return 0;
	}

	switch (ild->nominal_opcode) {
	case 0x9A:
		if (map == PTI_MAP_0) {
			insn->iclass = ptic_far_call;
			iext->iclass = PTI_INST_CALL_9A;
		}
		return 0;

	case 0xFF:
		if (map == PTI_MAP_0) {
			uint8_t reg = pti_get_modrm_reg(ild);

			if (reg == 2) {
				insn->iclass = ptic_call;
				iext->iclass = PTI_INST_CALL_FFr2;
			} else if (reg == 3) {
				insn->iclass = ptic_far_call;
				iext->iclass = PTI_INST_CALL_FFr3;
			} else if (reg == 4) {
				insn->iclass = ptic_jump;
				iext->iclass = PTI_INST_JMP_FFr4;
			} else if (reg == 5) {
				insn->iclass = ptic_far_jump;
				iext->iclass = PTI_INST_JMP_FFr5;
			}
		}
		return 0;

	case 0xE8:
		if (map == PTI_MAP_0) {
			insn->iclass = ptic_call;
			iext->iclass = PTI_INST_CALL_E8;

			return set_branch_target(iext, ild);
		}
		return 0;

	case 0xCD:
		if (map == PTI_MAP_0) {
			insn->iclass = ptic_far_call;
			iext->iclass = PTI_INST_INT;
		}

		return 0;

	case 0xCC:
		if (map == PTI_MAP_0) {
			insn->iclass = ptic_far_call;
			iext->iclass = PTI_INST_INT3;
		}

		return 0;

	case 0xCE:
		if (map == PTI_MAP_0) {
			insn->iclass = ptic_far_call;
			iext->iclass = PTI_INST_INTO;
		}

		return 0;

	case 0xF1:
		if (map == PTI_MAP_0) {
			insn->iclass = ptic_far_call;
			iext->iclass = PTI_INST_INT1;
		}

		return 0;

	case 0xCF:
		if (map == PTI_MAP_0) {
			insn->iclass = ptic_far_return;
			iext->iclass = PTI_INST_IRET;
		}
		return 0;

	case 0xE9:
		if (map == PTI_MAP_0) {
			insn->iclass = ptic_jump;
			iext->iclass = PTI_INST_JMP_E9;

			return set_branch_target(iext, ild);
		}
		return 0;

	case 0xEA:
		if (map == PTI_MAP_0) {
			/* Far jumps are treated as indirect jumps. */
			insn->iclass = ptic_far_jump;
			iext->iclass = PTI_INST_JMP_EA;
		}
		return 0;

	case 0xEB:
		if (map == PTI_MAP_0) {
			insn->iclass = ptic_jump;
			iext->iclass = PTI_INST_JMP_EB;

			return set_branch_target(iext, ild);
		}
		return 0;

	case 0xE3:
		if (map == PTI_MAP_0) {
			insn->iclass = ptic_cond_jump;
			iext->iclass = PTI_INST_JrCXZ;

			return set_branch_target(iext, ild);
		}
		return 0;

	case 0xE0:
		if (map == PTI_MAP_0) {
			insn->iclass = ptic_cond_jump;
			iext->iclass = PTI_INST_LOOPNE;

			return set_branch_target(iext, ild);
		}
		return 0;

	case 0xE1:
		if (map == PTI_MAP_0) {
			insn->iclass = ptic_cond_jump;
			iext->iclass = PTI_INST_LOOPE;

			return set_branch_target(iext, ild);
		}
		return 0;

	case 0xE2:
		if (map == PTI_MAP_0) {
			insn->iclass = ptic_cond_jump;
			iext->iclass = PTI_INST_LOOP;

			return set_branch_target(iext, ild);
		}
		return 0;

	case 0x22:
		if (map == PTI_MAP_1)
			if (pti_get_modrm_reg(ild) == 3)
				if (!ild->u.s.rex_r)
					iext->iclass = PTI_INST_MOV_CR3;

		return 0;

	case 0xC3:
		if (map == PTI_MAP_0) {
			insn->iclass = ptic_return;
			iext->iclass = PTI_INST_RET_C3;
		}
		return 0;

	case 0xC2:
		if (map == PTI_MAP_0) {
			insn->iclass = ptic_return;
			iext->iclass = PTI_INST_RET_C2;
		}
		return 0;

	case 0xCB:
		if (map == PTI_MAP_0) {
			insn->iclass = ptic_far_return;
			iext->iclass = PTI_INST_RET_CB;
		}
		return 0;

	case 0xCA:
		if (map == PTI_MAP_0) {
			insn->iclass = ptic_far_return;
			iext->iclass = PTI_INST_RET_CA;
		}
		return 0;

	case 0x05:
		if (map == PTI_MAP_1) {
			insn->iclass = ptic_far_call;
			iext->iclass = PTI_INST_SYSCALL;
		}
		return 0;

	case 0x34:
		if (map == PTI_MAP_1) {
			insn->iclass = ptic_far_call;
			iext->iclass = PTI_INST_SYSENTER;
		}
		return 0;

	case 0x35:
		if (map == PTI_MAP_1) {
			insn->iclass = ptic_far_return;
			iext->iclass = PTI_INST_SYSEXIT;
		}
		return 0;

	case 0x07:
		if (map == PTI_MAP_1) {
			insn->iclass = ptic_far_return;
			iext->iclass = PTI_INST_SYSRET;
		}
		return 0;

	case 0x01:
		if (map == PTI_MAP_1) {
			switch (ild->modrm_byte) {
			case 0xc1:
				insn->iclass = ptic_far_call;
				iext->iclass = PTI_INST_VMCALL;
				break;

			case 0xc2:
				insn->iclass = ptic_far_return;
				iext->iclass = PTI_INST_VMLAUNCH;
				break;

			case 0xc3:
				insn->iclass = ptic_far_return;
				iext->iclass = PTI_INST_VMRESUME;
				break;

			default:
				break;
			}
		}
		return 0;

	case 0xc7:
		if (map == PTI_MAP_1 &&
		    pti_get_modrm_mod(ild) != 3 &&
		    pti_get_modrm_reg(ild) == 6)
			iext->iclass = PTI_INST_VMPTRLD;

		return 0;

	case 0xae:
		if (map == PTI_MAP_1 && ild->u.s.f3 && !ild->u.s.osz &&
		    pti_get_modrm_reg(ild) == 4) {
			insn->iclass = ptic_ptwrite;
			iext->iclass = PTI_INST_PTWRITE;
		}
		return 0;

	default:
		return 0;
	}
}

int pt_ild_decode(struct pt_insn *insn, struct pt_insn_ext *iext)
{
	struct pt_ild ild;
	int size;

	if (!insn || !iext)
		return -pte_internal;

	ild.mode = insn->mode;
	ild.itext = insn->raw;
	ild.max_bytes = insn->size;

	size = pt_instruction_length_decode(&ild);
	if (size < 0)
		return size;

	insn->size = (uint8_t) size;

	return pt_instruction_decode(insn, iext, &ild);
}
