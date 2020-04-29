/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * opcodes-virt.h: Opcode definitions for the ARM virtualization extensions
 * Copyright (C) 2012  Linaro Limited
 */
#ifndef __ASM_ARM_OPCODES_VIRT_H
#define __ASM_ARM_OPCODES_VIRT_H

#include <asm/opcodes.h>

#define __HVC(imm16) __inst_arm_thumb32(				\
	0xE1400070 | (((imm16) & 0xFFF0) << 4) | ((imm16) & 0x000F),	\
	0xF7E08000 | (((imm16) & 0xF000) << 4) | ((imm16) & 0x0FFF)	\
)

#define __ERET	__inst_arm_thumb32(					\
	0xE160006E,							\
	0xF3DE8F00							\
)

#define __MSR_ELR_HYP(regnum)	__inst_arm_thumb32(			\
	0xE12EF300 | regnum,						\
	0xF3808E30 | (regnum << 16)					\
)

#endif /* ! __ASM_ARM_OPCODES_VIRT_H */
