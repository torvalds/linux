/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Linux/PA-RISC Project (http://www.parisc-linux.org/)
 *
 * Floating-point emulation code
 *  Copyright (C) 2001 Hewlett-Packard (Paul Bame) <bame@debian.org>
 */

#ifndef _MACHINE_FPU_INCLUDED /* allows multiple inclusion */
#define _MACHINE_FPU_INCLUDED

#define PA83_FPU_FLAG    0x00000001
#define PA89_FPU_FLAG    0x00000002
#define PA2_0_FPU_FLAG   0x00000010

#define TIMEX_EXTEN_FLAG 0x00000004

#define ROLEX_EXTEN_FLAG 0x00000008
#define COPR_FP 	0x00000080	/* Floating point -- Coprocessor 0 */
#define SFU_MPY_DIVIDE	0x00008000	/* Multiply/Divide __ SFU 0 */

#define EM_FPU_TYPE_OFFSET 272

/* version of EMULATION software for COPR,0,0 instruction */
#define EMULATION_VERSION 4

/*
 * The only way to differentiate between TIMEX and ROLEX (or PCX-S and PCX-T)
 * is through the potential type field from the PDC_MODEL call.
 * The following flags are used to assist this differentiation.
 */

#define ROLEX_POTENTIAL_KEY_FLAGS	PDC_MODEL_CPU_KEY_WORD_TO_IO
#define TIMEX_POTENTIAL_KEY_FLAGS	(PDC_MODEL_CPU_KEY_QUAD_STORE | \
					 PDC_MODEL_CPU_KEY_RECIP_SQRT)

#endif /* ! _MACHINE_FPU_INCLUDED */
