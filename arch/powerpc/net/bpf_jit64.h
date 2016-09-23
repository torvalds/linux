/*
 * bpf_jit64.h: BPF JIT compiler for PPC64
 *
 * Copyright 2016 Naveen N. Rao <naveen.n.rao@linux.vnet.ibm.com>
 *		  IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */
#ifndef _BPF_JIT64_H
#define _BPF_JIT64_H

#include "bpf_jit.h"

/*
 * Stack layout:
 * Ensure the top half (upto local_tmp_var) stays consistent
 * with our redzone usage.
 *
 *		[	prev sp		] <-------------
 *		[   nv gpr save area	] 8*8		|
 *		[    tail_call_cnt	] 8		|
 *		[    local_tmp_var	] 8		|
 * fp (r31) -->	[   ebpf stack space	] 512		|
 *		[     frame header	] 32/112	|
 * sp (r1) --->	[    stack pointer	] --------------
 */

/* for gpr non volatile registers BPG_REG_6 to 10, plus skb cache registers */
#define BPF_PPC_STACK_SAVE	(8*8)
/* for bpf JIT code internal usage */
#define BPF_PPC_STACK_LOCALS	16
/* Ensure this is quadword aligned */
#define BPF_PPC_STACKFRAME	(STACK_FRAME_MIN_SIZE + MAX_BPF_STACK + \
				 BPF_PPC_STACK_LOCALS + BPF_PPC_STACK_SAVE)

#ifndef __ASSEMBLY__

/* BPF register usage */
#define SKB_HLEN_REG	(MAX_BPF_REG + 0)
#define SKB_DATA_REG	(MAX_BPF_REG + 1)
#define TMP_REG_1	(MAX_BPF_REG + 2)
#define TMP_REG_2	(MAX_BPF_REG + 3)

/* BPF to ppc register mappings */
static const int b2p[] = {
	/* function return value */
	[BPF_REG_0] = 8,
	/* function arguments */
	[BPF_REG_1] = 3,
	[BPF_REG_2] = 4,
	[BPF_REG_3] = 5,
	[BPF_REG_4] = 6,
	[BPF_REG_5] = 7,
	/* non volatile registers */
	[BPF_REG_6] = 27,
	[BPF_REG_7] = 28,
	[BPF_REG_8] = 29,
	[BPF_REG_9] = 30,
	/* frame pointer aka BPF_REG_10 */
	[BPF_REG_FP] = 31,
	/* eBPF jit internal registers */
	[SKB_HLEN_REG] = 25,
	[SKB_DATA_REG] = 26,
	[TMP_REG_1] = 9,
	[TMP_REG_2] = 10
};

/* PPC NVR range -- update this if we ever use NVRs below r24 */
#define BPF_PPC_NVR_MIN		24

/* Assembly helpers */
#define DECLARE_LOAD_FUNC(func)	u64 func(u64 r3, u64 r4);			\
				u64 func##_negative_offset(u64 r3, u64 r4);	\
				u64 func##_positive_offset(u64 r3, u64 r4);

DECLARE_LOAD_FUNC(sk_load_word);
DECLARE_LOAD_FUNC(sk_load_half);
DECLARE_LOAD_FUNC(sk_load_byte);

#define CHOOSE_LOAD_FUNC(imm, func)						\
			(imm < 0 ?						\
			(imm >= SKF_LL_OFF ? func##_negative_offset : func) :	\
			func##_positive_offset)

#define SEEN_FUNC	0x1000 /* might call external helpers */
#define SEEN_STACK	0x2000 /* uses BPF stack */
#define SEEN_SKB	0x4000 /* uses sk_buff */
#define SEEN_TAILCALL	0x8000 /* uses tail calls */

struct codegen_context {
	/*
	 * This is used to track register usage as well
	 * as calls to external helpers.
	 * - register usage is tracked with corresponding
	 *   bits (r3-r10 and r25-r31)
	 * - rest of the bits can be used to track other
	 *   things -- for now, we use bits 16 to 23
	 *   encoded in SEEN_* macros above
	 */
	unsigned int seen;
	unsigned int idx;
};

#endif /* !__ASSEMBLY__ */

#endif
