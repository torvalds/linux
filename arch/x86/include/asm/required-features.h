#ifndef _ASM_X86_REQUIRED_FEATURES_H
#define _ASM_X86_REQUIRED_FEATURES_H

/* Define minimum CPUID feature set for kernel These bits are checked
   really early to actually display a visible error message before the
   kernel dies.  Make sure to assign features to the proper mask!

   Some requirements that are not in CPUID yet are also in the
   CONFIG_X86_MINIMUM_CPU_FAMILY which is checked too.

   The real information is in arch/x86/Kconfig.cpu, this just converts
   the CONFIGs into a bitmask */

#ifndef CONFIG_MATH_EMULATION
# define NEED_FPU	(1<<(X86_FEATURE_FPU & 31))
#else
# define NEED_FPU	0
#endif

#if defined(CONFIG_X86_PAE) || defined(CONFIG_X86_64)
# define NEED_PAE	(1<<(X86_FEATURE_PAE & 31))
#else
# define NEED_PAE	0
#endif

#ifdef CONFIG_X86_CMPXCHG64
# define NEED_CX8	(1<<(X86_FEATURE_CX8 & 31))
#else
# define NEED_CX8	0
#endif

#if defined(CONFIG_X86_CMOV) || defined(CONFIG_X86_64)
# define NEED_CMOV	(1<<(X86_FEATURE_CMOV & 31))
#else
# define NEED_CMOV	0
#endif

#ifdef CONFIG_X86_USE_3DNOW
# define NEED_3DNOW	(1<<(X86_FEATURE_3DNOW & 31))
#else
# define NEED_3DNOW	0
#endif

#if defined(CONFIG_X86_P6_NOP) || defined(CONFIG_X86_64)
# define NEED_NOPL	(1<<(X86_FEATURE_NOPL & 31))
#else
# define NEED_NOPL	0
#endif

#ifdef CONFIG_X86_64
#define NEED_PSE	0
#define NEED_MSR	(1<<(X86_FEATURE_MSR & 31))
#define NEED_PGE	(1<<(X86_FEATURE_PGE & 31))
#define NEED_FXSR	(1<<(X86_FEATURE_FXSR & 31))
#define NEED_XMM	(1<<(X86_FEATURE_XMM & 31))
#define NEED_XMM2	(1<<(X86_FEATURE_XMM2 & 31))
#define NEED_LM		(1<<(X86_FEATURE_LM & 31))
#else
#define NEED_PSE	0
#define NEED_MSR	0
#define NEED_PGE	0
#define NEED_FXSR	0
#define NEED_XMM	0
#define NEED_XMM2	0
#define NEED_LM		0
#endif

#define REQUIRED_MASK0	(NEED_FPU|NEED_PSE|NEED_MSR|NEED_PAE|\
			 NEED_CX8|NEED_PGE|NEED_FXSR|NEED_CMOV|\
			 NEED_XMM|NEED_XMM2)
#define SSE_MASK	(NEED_XMM|NEED_XMM2)

#define REQUIRED_MASK1	(NEED_LM|NEED_3DNOW)

#define REQUIRED_MASK2	0
#define REQUIRED_MASK3	(NEED_NOPL)
#define REQUIRED_MASK4	0
#define REQUIRED_MASK5	0
#define REQUIRED_MASK6	0
#define REQUIRED_MASK7	0

#endif /* _ASM_X86_REQUIRED_FEATURES_H */
