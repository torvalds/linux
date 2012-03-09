/*
 *  arch/arm/include/asm/opcodes.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARM_OPCODES_H
#define __ASM_ARM_OPCODES_H

#ifndef __ASSEMBLY__
extern asmlinkage unsigned int arm_check_condition(u32 opcode, u32 psr);
#endif

#define ARM_OPCODE_CONDTEST_FAIL   0
#define ARM_OPCODE_CONDTEST_PASS   1
#define ARM_OPCODE_CONDTEST_UNCOND 2

#endif /* __ASM_ARM_OPCODES_H */
