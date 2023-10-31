// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * LoongArch SIMD XOR operations
 *
 * Copyright (C) 2023 WANG Xuerui <git@xen0n.name>
 */

#include "xor_simd.h"

/*
 * Process one cache line (64 bytes) per loop. This is assuming all future
 * popular LoongArch cores are similar performance-characteristics-wise to the
 * current models.
 */
#define LINE_WIDTH 64

#ifdef CONFIG_CPU_HAS_LSX

#define LD(reg, base, offset)	\
	"vld $vr" #reg ", %[" #base "], " #offset "\n\t"
#define ST(reg, base, offset)	\
	"vst $vr" #reg ", %[" #base "], " #offset "\n\t"
#define XOR(dj, k)	"vxor.v $vr" #dj ", $vr" #dj ", $vr" #k "\n\t"

#define LD_INOUT_LINE(base)	\
	LD(0, base, 0)		\
	LD(1, base, 16)		\
	LD(2, base, 32)		\
	LD(3, base, 48)

#define LD_AND_XOR_LINE(base)	\
	LD(4, base, 0)		\
	LD(5, base, 16)		\
	LD(6, base, 32)		\
	LD(7, base, 48)		\
	XOR(0, 4)		\
	XOR(1, 5)		\
	XOR(2, 6)		\
	XOR(3, 7)

#define ST_LINE(base)		\
	ST(0, base, 0)		\
	ST(1, base, 16)		\
	ST(2, base, 32)		\
	ST(3, base, 48)

#define XOR_FUNC_NAME(nr) __xor_lsx_##nr
#include "xor_template.c"

#undef LD
#undef ST
#undef XOR
#undef LD_INOUT_LINE
#undef LD_AND_XOR_LINE
#undef ST_LINE
#undef XOR_FUNC_NAME

#endif /* CONFIG_CPU_HAS_LSX */

#ifdef CONFIG_CPU_HAS_LASX

#define LD(reg, base, offset)	\
	"xvld $xr" #reg ", %[" #base "], " #offset "\n\t"
#define ST(reg, base, offset)	\
	"xvst $xr" #reg ", %[" #base "], " #offset "\n\t"
#define XOR(dj, k)	"xvxor.v $xr" #dj ", $xr" #dj ", $xr" #k "\n\t"

#define LD_INOUT_LINE(base)	\
	LD(0, base, 0)		\
	LD(1, base, 32)

#define LD_AND_XOR_LINE(base)	\
	LD(2, base, 0)		\
	LD(3, base, 32)		\
	XOR(0, 2)		\
	XOR(1, 3)

#define ST_LINE(base)		\
	ST(0, base, 0)		\
	ST(1, base, 32)

#define XOR_FUNC_NAME(nr) __xor_lasx_##nr
#include "xor_template.c"

#undef LD
#undef ST
#undef XOR
#undef LD_INOUT_LINE
#undef LD_AND_XOR_LINE
#undef ST_LINE
#undef XOR_FUNC_NAME

#endif /* CONFIG_CPU_HAS_LASX */
