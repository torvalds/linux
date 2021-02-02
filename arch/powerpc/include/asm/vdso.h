/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_VDSO_H
#define _ASM_POWERPC_VDSO_H

/* Default map addresses for 32bit vDSO */
#define VDSO32_MBASE	0x100000

#define VDSO_VERSION_STRING	LINUX_2.6.15

#ifndef __ASSEMBLY__

#ifdef CONFIG_PPC64
#include <generated/vdso64-offsets.h>
#endif

#ifdef CONFIG_VDSO32
#include <generated/vdso32-offsets.h>
#endif

#define VDSO64_SYMBOL(base, name) ((unsigned long)(base) + (vdso64_offset_##name))

#define VDSO32_SYMBOL(base, name) ((unsigned long)(base) + (vdso32_offset_##name))

int vdso_getcpu_init(void);

#else /* __ASSEMBLY__ */

#ifdef __VDSO64__
#define V_FUNCTION_BEGIN(name)		\
	.globl name;			\
	name:				\

#define V_FUNCTION_END(name)		\
	.size name,.-name;

#define V_LOCAL_FUNC(name) (name)
#endif /* __VDSO64__ */

#ifdef __VDSO32__

#define V_FUNCTION_BEGIN(name)		\
	.globl name;			\
	.type name,@function; 		\
	name:				\

#define V_FUNCTION_END(name)		\
	.size name,.-name;

#define V_LOCAL_FUNC(name) (name)

#endif /* __VDSO32__ */

#endif /* __ASSEMBLY__ */

#endif /* _ASM_POWERPC_VDSO_H */
