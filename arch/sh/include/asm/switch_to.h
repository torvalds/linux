/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003  Paul Mundt
 * Copyright (C) 2004  Richard Curnow
 */
#ifndef __ASM_SH_SWITCH_TO_H
#define __ASM_SH_SWITCH_TO_H

#ifdef CONFIG_SUPERH32
# include <asm/switch_to_32.h>
#else
# include <asm/switch_to_64.h>
#endif

#endif /* __ASM_SH_SWITCH_TO_H */
