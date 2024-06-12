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

#ifdef CONFIG_EXPOLINE_EXTERN

void __s390_indirect_jump_r1(void);
void __s390_indirect_jump_r2(void);
void __s390_indirect_jump_r3(void);
void __s390_indirect_jump_r4(void);
void __s390_indirect_jump_r5(void);
void __s390_indirect_jump_r6(void);
void __s390_indirect_jump_r7(void);
void __s390_indirect_jump_r8(void);
void __s390_indirect_jump_r9(void);
void __s390_indirect_jump_r10(void);
void __s390_indirect_jump_r11(void);
void __s390_indirect_jump_r12(void);
void __s390_indirect_jump_r13(void);
void __s390_indirect_jump_r14(void);
void __s390_indirect_jump_r15(void);

#endif

#endif /* __ASSEMBLY__ */

#endif /* _ASM_S390_EXPOLINE_H */
