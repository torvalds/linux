/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __X86_KERNEL_FPU_INTERNAL_H
#define __X86_KERNEL_FPU_INTERNAL_H

/* CPU feature check wrappers */
static __always_inline __pure bool use_xsave(void)
{
	return cpu_feature_enabled(X86_FEATURE_XSAVE);
}

static __always_inline __pure bool use_fxsr(void)
{
	return cpu_feature_enabled(X86_FEATURE_FXSR);
}

#ifdef CONFIG_X86_DEBUG_FPU
# define WARN_ON_FPU(x) WARN_ON_ONCE(x)
#else
# define WARN_ON_FPU(x) ({ (void)(x); 0; })
#endif

/* Init functions */
extern void fpu__init_prepare_fx_sw_frame(void);

/* Used in init.c */
extern void fpstate_init_user(union fpregs_state *state);

#endif
