/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * bpf_jit64.h: BPF JIT compiler for PPC64
 *
 * Copyright 2016 Naveen N. Rao <naveen.n.rao@linux.vnet.ibm.com>
 *		  IBM Corporation
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
 *		[   nv gpr save area	] 5*8		|
 *		[    tail_call_cnt	] 8		|
 *		[    local_tmp_var	] 16		|
 * fp (r31) -->	[   ebpf stack space	] upto 512	|
 *		[     frame header	] 32/112	|
 * sp (r1) --->	[    stack pointer	] --------------
 */

/* for gpr non volatile registers BPG_REG_6 to 10 */
#define BPF_PPC_STACK_SAVE	(5*8)
/* for bpf JIT code internal usage */
#define BPF_PPC_STACK_LOCALS	24
/* stack frame excluding BPF stack, ensure this is quadword aligned */
#define BPF_PPC_STACKFRAME	(STACK_FRAME_MIN_SIZE + \
				 BPF_PPC_STACK_LOCALS + BPF_PPC_STACK_SAVE)

#ifndef __ASSEMBLY__

/* BPF register usage */
#define TMP_REG_1	(MAX_BPF_JIT_REG + 0)
#define TMP_REG_2	(MAX_BPF_JIT_REG + 1)

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
	[BPF_REG_AX] = 2,
	[TMP_REG_1] = 9,
	[TMP_REG_2] = 10
};

/* PPC NVR range -- update this if we ever use NVRs below r27 */
#define BPF_PPC_NVR_MIN		27

/*
 * WARNING: These can use TMP_REG_2 if the offset is not at word boundary,
 * so ensure that it isn't in use already.
 */
#define PPC_BPF_LL(r, base, i) do {					      \
				if ((i) % 4) {				      \
					EMIT(PPC_RAW_LI(b2p[TMP_REG_2], (i)));\
					EMIT(PPC_RAW_LDX(r, base,	      \
							b2p[TMP_REG_2]));     \
				} else					      \
					EMIT(PPC_RAW_LD(r, base, i));	      \
				} while(0)
#define PPC_BPF_STL(r, base, i) do {					      \
				if ((i) % 4) {				      \
					EMIT(PPC_RAW_LI(b2p[TMP_REG_2], (i)));\
					EMIT(PPC_RAW_STDX(r, base,	      \
							b2p[TMP_REG_2]));     \
				} else					      \
					EMIT(PPC_RAW_STD(r, base, i));	      \
				} while(0)
#define PPC_BPF_STLU(r, base, i) do { EMIT(PPC_RAW_STDU(r, base, i)); } while(0)

#define SEEN_FUNC	0x1000 /* might call external helpers */
#define SEEN_STACK	0x2000 /* uses BPF stack */
#define SEEN_TAILCALL	0x4000 /* uses tail calls */

struct codegen_context {
	/*
	 * This is used to track register usage as well
	 * as calls to external helpers.
	 * - register usage is tracked with corresponding
	 *   bits (r3-r10 and r27-r31)
	 * - rest of the bits can be used to track other
	 *   things -- for now, we use bits 16 to 23
	 *   encoded in SEEN_* macros above
	 */
	unsigned int seen;
	unsigned int idx;
	unsigned int stack_size;
};

#endif /* !__ASSEMBLY__ */

#endif
