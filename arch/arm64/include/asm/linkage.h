#ifndef __ASM_LINKAGE_H
#define __ASM_LINKAGE_H

#define __ALIGN		.align 2
#define __ALIGN_STR	".align 2"

#if defined(CONFIG_ARM64_BTI_KERNEL) && defined(__aarch64__)

/*
 * Since current versions of gas reject the BTI instruction unless we
 * set the architecture version to v8.5 we use the hint instruction
 * instead.
 */
#define BTI_C hint 34 ;
#define BTI_J hint 36 ;

/*
 * When using in-kernel BTI we need to ensure that PCS-conformant assembly
 * functions have suitable annotations.  Override SYM_FUNC_START to insert
 * a BTI landing pad at the start of everything.
 */
#define SYM_FUNC_START(name)				\
	SYM_START(name, SYM_L_GLOBAL, SYM_A_ALIGN)	\
	BTI_C

#define SYM_FUNC_START_NOALIGN(name)			\
	SYM_START(name, SYM_L_GLOBAL, SYM_A_NONE)	\
	BTI_C

#define SYM_FUNC_START_LOCAL(name)			\
	SYM_START(name, SYM_L_LOCAL, SYM_A_ALIGN)	\
	BTI_C

#define SYM_FUNC_START_LOCAL_NOALIGN(name)		\
	SYM_START(name, SYM_L_LOCAL, SYM_A_NONE)	\
	BTI_C

#define SYM_FUNC_START_WEAK(name)			\
	SYM_START(name, SYM_L_WEAK, SYM_A_ALIGN)	\
	BTI_C

#define SYM_FUNC_START_WEAK_NOALIGN(name)		\
	SYM_START(name, SYM_L_WEAK, SYM_A_NONE)		\
	BTI_C

#define SYM_INNER_LABEL(name, linkage)			\
	.type name SYM_T_NONE ASM_NL			\
	SYM_ENTRY(name, linkage, SYM_A_NONE)		\
	BTI_J

#endif

/*
 * Annotate a function as position independent, i.e., safe to be called before
 * the kernel virtual mapping is activated.
 */
#define SYM_FUNC_START_PI(x)			\
		SYM_FUNC_START_ALIAS(__pi_##x);	\
		SYM_FUNC_START(x)

#define SYM_FUNC_START_WEAK_PI(x)		\
		SYM_FUNC_START_ALIAS(__pi_##x);	\
		SYM_FUNC_START_WEAK(x)

#define SYM_FUNC_END_PI(x)			\
		SYM_FUNC_END(x);		\
		SYM_FUNC_END_ALIAS(__pi_##x)

#endif
