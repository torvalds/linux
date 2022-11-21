/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_EXPOLINE_H
#define _ASM_S390_EXPOLINE_H

#ifndef __ASSEMBLY__

#include <linux/types.h>

extern int nospec_disable;

void nospec_init_branches(void);
void nospec_auto_detect(void);
void nospec_revert(s32 *start, s32 *end);

static inline bool nospec_uses_trampoline(void)
{
	return __is_defined(CC_USING_EXPOLINE) && !nospec_disable;
}

#endif /* __ASSEMBLY__ */

#endif /* _ASM_S390_EXPOLINE_H */
