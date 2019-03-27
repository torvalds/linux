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

#if !defined(PT_ILD_H)
#define PT_ILD_H

#include "pt_insn.h"

#include "intel-pt.h"


typedef enum {
	PTI_MAP_0,	/* 1-byte opcodes.           may have modrm */
	PTI_MAP_1,	/* 2-byte opcodes (0x0f).    may have modrm */
	PTI_MAP_2,	/* 3-byte opcodes (0x0f38).  has modrm */
	PTI_MAP_3,	/* 3-byte opcodes (0x0f3a).  has modrm */
	PTI_MAP_AMD3DNOW,	/* 3d-now opcodes (0x0f0f).  has modrm */
	PTI_MAP_INVALID
} pti_map_enum_t;

struct pt_ild {
	/* inputs */
	uint8_t const *itext;
	uint8_t max_bytes;	/*1..15 bytes  */
	enum pt_exec_mode mode;

	union {
		struct {
			uint32_t osz:1;
			uint32_t asz:1;
			uint32_t lock:1;
			uint32_t f3:1;
			uint32_t f2:1;
			uint32_t last_f2f3:2;	/* 2 or 3 */
			/* The vex bit is set for c4/c5 VEX and EVEX. */
			uint32_t vex:1;
			/* The REX.R and REX.W bits in REX, VEX, or EVEX. */
			uint32_t rex_r:1;
			uint32_t rex_w:1;
		} s;
		uint32_t i;
	} u;
	uint8_t imm1_bytes;	/* # of bytes in 1st immediate */
	uint8_t imm2_bytes;	/* # of bytes in 2nd immediate */
	uint8_t disp_bytes;	/* # of displacement bytes */
	uint8_t modrm_byte;
	/* 5b but valid values=  0,1,2,3 could be in bit union */
	uint8_t map;
	uint8_t rex;	/* 0b0100wrxb */
	uint8_t nominal_opcode;
	uint8_t disp_pos;
	/* imm_pos can be derived from disp_pos + disp_bytes. */
};

static inline pti_map_enum_t pti_get_map(const struct pt_ild *ild)
{
	return (pti_map_enum_t) ild->map;
}

static inline uint8_t pti_get_modrm_mod(const struct pt_ild *ild)
{
	return ild->modrm_byte >> 6;
}

static inline uint8_t pti_get_modrm_reg(const struct pt_ild *ild)
{
	return (ild->modrm_byte >> 3) & 7;
}

static inline uint8_t pti_get_modrm_rm(const struct pt_ild *ild)
{
	return ild->modrm_byte & 7;
}

/* MAIN ENTRANCE POINTS */

/* one time call. not thread safe init. call when single threaded. */
extern void pt_ild_init(void);

/* all decoding is multithread safe. */

/* Decode one instruction.
 *
 * Input:
 *
 *   @insn->ip:      the virtual address of the instruction
 *   @insn->raw:     the memory at that virtual address
 *   @insn->size:    the maximal size of the instruction
 *   @insn->mode:    the execution mode
 *
 * Output:
 *
 *   @insn->size:    the actual size of the instruction
 *   @insn->iclass:  a coarse classification
 *
 *   @iext->iclass:  a finer grain classification
 *   @iext->variant: instruction class dependent information
 *
 * Returns zero on success, a negative error code otherwise.
 */
extern int pt_ild_decode(struct pt_insn *insn, struct pt_insn_ext *iext);

#endif /* PT_ILD_H */
