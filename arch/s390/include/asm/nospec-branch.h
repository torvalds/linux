/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_EXPOLINE_H
#define _ASM_S390_EXPOLINE_H

#ifndef __ASSEMBLY__

#include <linux/types.h>

extern int yesspec_disable;

void yesspec_init_branches(void);
void yesspec_auto_detect(void);
void yesspec_revert(s32 *start, s32 *end);

#endif /* __ASSEMBLY__ */

#endif /* _ASM_S390_EXPOLINE_H */
