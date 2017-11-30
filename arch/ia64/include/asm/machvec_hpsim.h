/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IA64_MACHVEC_HPSIM_h
#define _ASM_IA64_MACHVEC_HPSIM_h

extern ia64_mv_setup_t hpsim_setup;
extern ia64_mv_irq_init_t hpsim_irq_init;

/*
 * This stuff has dual use!
 *
 * For a generic kernel, the macros are used to initialize the
 * platform's machvec structure.  When compiling a non-generic kernel,
 * the macros are used directly.
 */
#define ia64_platform_name	"hpsim"
#define platform_setup		hpsim_setup
#define platform_irq_init	hpsim_irq_init

#endif /* _ASM_IA64_MACHVEC_HPSIM_h */
