/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_LINKAGE_H
#define _ASM_X86_LINKAGE_H

#include <linux/stringify.h>
#include <asm/ibt.h>

#undef notrace
#define notrace __attribute__((no_instrument_function))

#ifdef CONFIG_64BIT
/*
 * The generic version tends to create spurious ENDBR instructions under
 * certain conditions.
 */
#define _THIS_IP_ ({ unsigned long __here; asm ("lea 0(%%rip), %0" : "=r" (__here)); __here; })
#endif

#ifdef CONFIG_X86_32
#define asmlinkage CPP_ASMLINKAGE __attribute__((regparm(0)))
#endif /* CONFIG_X86_32 */

#define __ALIGN		.balign CONFIG_FUNCTION_ALIGNMENT, 0x90;
#define __ALIGN_STR	__stringify(__ALIGN)

#if defined(CONFIG_CALL_PADDING) && !defined(__DISABLE_EXPORTS) && !defined(BUILD_VDSO)
#define FUNCTION_PADDING	.skip CONFIG_FUNCTION_ALIGNMENT, 0x90;
#else
#define FUNCTION_PADDING
#endif

#if (CONFIG_FUNCTION_ALIGNMENT > 8) && !defined(__DISABLE_EXPORTS) && !defined(BUILD_VDSO)
# define __FUNC_ALIGN		__ALIGN; FUNCTION_PADDING
#else
# define __FUNC_ALIGN		__ALIGN
#endif

#define ASM_FUNC_ALIGN		__stringify(__FUNC_ALIGN)
#define SYM_F_ALIGN		__FUNC_ALIGN

#ifdef __ASSEMBLY__

#if defined(CONFIG_RETHUNK) && !defined(__DISABLE_EXPORTS) && !defined(BUILD_VDSO)
#define RET	jmp __x86_return_thunk
#else /* CONFIG_RETPOLINE */
#ifdef CONFIG_SLS
#define RET	ret; int3
#else
#define RET	ret
#endif
#endif /* CONFIG_RETPOLINE */

#else /* __ASSEMBLY__ */

#if defined(CONFIG_RETHUNK) && !defined(__DISABLE_EXPORTS) && !defined(BUILD_VDSO)
#define ASM_RET	"jmp __x86_return_thunk\n\t"
#else /* CONFIG_RETPOLINE */
#ifdef CONFIG_SLS
#define ASM_RET	"ret; int3\n\t"
#else
#define ASM_RET	"ret\n\t"
#endif
#endif /* CONFIG_RETPOLINE */

#endif /* __ASSEMBLY__ */

/*
 * Depending on -fpatchable-function-entry=N,N usage (CONFIG_CALL_PADDING) the
 * CFI symbol layout changes.
 *
 * Without CALL_THUNKS:
 *
 * 	.align	FUNCTION_ALIGNMENT
 * __cfi_##name:
 * 	.skip	FUNCTION_PADDING, 0x90
 * 	.byte   0xb8
 * 	.long	__kcfi_typeid_##name
 * name:
 *
 * With CALL_THUNKS:
 *
 * 	.align FUNCTION_ALIGNMENT
 * __cfi_##name:
 * 	.byte	0xb8
 * 	.long	__kcfi_typeid_##name
 * 	.skip	FUNCTION_PADDING, 0x90
 * name:
 *
 * In both cases the whole thing is FUNCTION_ALIGNMENT aligned and sized.
 */

#ifdef CONFIG_CALL_PADDING
#define CFI_PRE_PADDING
#define CFI_POST_PADDING	.skip	CONFIG_FUNCTION_PADDING_BYTES, 0x90;
#else
#define CFI_PRE_PADDING		.skip	CONFIG_FUNCTION_PADDING_BYTES, 0x90;
#define CFI_POST_PADDING
#endif

#define __CFI_TYPE(name)					\
	SYM_START(__cfi_##name, SYM_L_LOCAL, SYM_A_NONE)	\
	CFI_PRE_PADDING						\
	.byte 0xb8 ASM_NL					\
	.long __kcfi_typeid_##name ASM_NL			\
	CFI_POST_PADDING					\
	SYM_FUNC_END(__cfi_##name)

/* SYM_TYPED_FUNC_START -- use for indirectly called globals, w/ CFI type */
#define SYM_TYPED_FUNC_START(name)				\
	SYM_TYPED_START(name, SYM_L_GLOBAL, SYM_F_ALIGN)	\
	ENDBR

/* SYM_FUNC_START -- use for global functions */
#define SYM_FUNC_START(name)				\
	SYM_START(name, SYM_L_GLOBAL, SYM_F_ALIGN)	\
	ENDBR

/* SYM_FUNC_START_NOALIGN -- use for global functions, w/o alignment */
#define SYM_FUNC_START_NOALIGN(name)			\
	SYM_START(name, SYM_L_GLOBAL, SYM_A_NONE)	\
	ENDBR

/* SYM_FUNC_START_LOCAL -- use for local functions */
#define SYM_FUNC_START_LOCAL(name)			\
	SYM_START(name, SYM_L_LOCAL, SYM_F_ALIGN)	\
	ENDBR

/* SYM_FUNC_START_LOCAL_NOALIGN -- use for local functions, w/o alignment */
#define SYM_FUNC_START_LOCAL_NOALIGN(name)		\
	SYM_START(name, SYM_L_LOCAL, SYM_A_NONE)	\
	ENDBR

/* SYM_FUNC_START_WEAK -- use for weak functions */
#define SYM_FUNC_START_WEAK(name)			\
	SYM_START(name, SYM_L_WEAK, SYM_F_ALIGN)	\
	ENDBR

/* SYM_FUNC_START_WEAK_NOALIGN -- use for weak functions, w/o alignment */
#define SYM_FUNC_START_WEAK_NOALIGN(name)		\
	SYM_START(name, SYM_L_WEAK, SYM_A_NONE)		\
	ENDBR

#endif /* _ASM_X86_LINKAGE_H */

