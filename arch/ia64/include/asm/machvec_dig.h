/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IA64_MACHVEC_DIG_h
#define _ASM_IA64_MACHVEC_DIG_h

extern ia64_mv_setup_t dig_setup;

/*
 * This stuff has dual use!
 *
 * For a generic kernel, the macros are used to initialize the
 * platform's machvec structure.  When compiling a non-generic kernel,
 * the macros are used directly.
 */
#define ia64_platform_name	"dig"
#define platform_setup		dig_setup

#endif /* _ASM_IA64_MACHVEC_DIG_h */
