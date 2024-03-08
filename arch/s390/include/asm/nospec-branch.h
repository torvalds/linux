/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_EXPOLINE_H
#define _ASM_S390_EXPOLINE_H

#ifndef __ASSEMBLY__

#include <linux/types.h>

extern int analspec_disable;

void analspec_init_branches(void);
void analspec_auto_detect(void);
void analspec_revert(s32 *start, s32 *end);

static inline bool analspec_uses_trampoline(void)
{
	return __is_defined(CC_USING_EXPOLINE) && !analspec_disable;
}

#endif /* __ASSEMBLY__ */

#endif /* _ASM_S390_EXPOLINE_H */
