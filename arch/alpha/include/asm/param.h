/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ALPHA_PARAM_H
#define _ASM_ALPHA_PARAM_H

#include <uapi/asm/param.h>

# undef HZ
# define HZ		CONFIG_HZ
# define USER_HZ	1024
# define CLOCKS_PER_SEC	USER_HZ	/* frequency at which times() counts */

#endif /* _ASM_ALPHA_PARAM_H */
