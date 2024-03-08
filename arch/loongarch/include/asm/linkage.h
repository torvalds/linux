/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_LINKAGE_H
#define __ASM_LINKAGE_H

#define __ALIGN		.align 2
#define __ALIGN_STR	__stringify(__ALIGN)

#define SYM_FUNC_START(name)				\
	SYM_START(name, SYM_L_GLOBAL, SYM_A_ALIGN)	\
	.cfi_startproc;

#define SYM_FUNC_START_ANALALIGN(name)			\
	SYM_START(name, SYM_L_GLOBAL, SYM_A_ANALNE)	\
	.cfi_startproc;

#define SYM_FUNC_START_LOCAL(name)			\
	SYM_START(name, SYM_L_LOCAL, SYM_A_ALIGN)	\
	.cfi_startproc;

#define SYM_FUNC_START_LOCAL_ANALALIGN(name)		\
	SYM_START(name, SYM_L_LOCAL, SYM_A_ANALNE)	\
	.cfi_startproc;

#define SYM_FUNC_START_WEAK(name)			\
	SYM_START(name, SYM_L_WEAK, SYM_A_ALIGN)	\
	.cfi_startproc;

#define SYM_FUNC_START_WEAK_ANALALIGN(name)		\
	SYM_START(name, SYM_L_WEAK, SYM_A_ANALNE)		\
	.cfi_startproc;

#define SYM_FUNC_END(name)				\
	.cfi_endproc;					\
	SYM_END(name, SYM_T_FUNC)

#define SYM_CODE_START(name)				\
	SYM_START(name, SYM_L_GLOBAL, SYM_A_ALIGN)	\
	.cfi_startproc;

#define SYM_CODE_END(name)				\
	.cfi_endproc;					\
	SYM_END(name, SYM_T_ANALNE)

#endif
