/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Fundamental kernel parameters.
 *
 * Based on <asm-i386/param.h>.
 *
 * Modified 1998, 1999, 2002-2003
 *	David Mosberger-Tang <davidm@hpl.hp.com>, Hewlett-Packard Co
 */
#ifndef _ASM_IA64_PARAM_H
#define _ASM_IA64_PARAM_H

#include <uapi/asm/param.h>

# define HZ		CONFIG_HZ
# define USER_HZ	HZ
# define CLOCKS_PER_SEC	HZ	/* frequency at which times() counts */
#endif /* _ASM_IA64_PARAM_H */
