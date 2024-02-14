/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2021 SiFive
 */
#include <linux/linkage.h>
#include <linux/export.h>
#include <asm/asm.h>

SYM_FUNC_START(xor_regs_2_)
	vsetvli a3, a0, e8, m8, ta, ma
	vle8.v v0, (a1)
	vle8.v v8, (a2)
	sub a0, a0, a3
	vxor.vv v16, v0, v8
	add a2, a2, a3
	vse8.v v16, (a1)
	add a1, a1, a3
	bnez a0, xor_regs_2_
	ret
SYM_FUNC_END(xor_regs_2_)
EXPORT_SYMBOL(xor_regs_2_)

SYM_FUNC_START(xor_regs_3_)
	vsetvli a4, a0, e8, m8, ta, ma
	vle8.v v0, (a1)
	vle8.v v8, (a2)
	sub a0, a0, a4
	vxor.vv v0, v0, v8
	vle8.v v16, (a3)
	add a2, a2, a4
	vxor.vv v16, v0, v16
	add a3, a3, a4
	vse8.v v16, (a1)
	add a1, a1, a4
	bnez a0, xor_regs_3_
	ret
SYM_FUNC_END(xor_regs_3_)
EXPORT_SYMBOL(xor_regs_3_)

SYM_FUNC_START(xor_regs_4_)
	vsetvli a5, a0, e8, m8, ta, ma
	vle8.v v0, (a1)
	vle8.v v8, (a2)
	sub a0, a0, a5
	vxor.vv v0, v0, v8
	vle8.v v16, (a3)
	add a2, a2, a5
	vxor.vv v0, v0, v16
	vle8.v v24, (a4)
	add a3, a3, a5
	vxor.vv v16, v0, v24
	add a4, a4, a5
	vse8.v v16, (a1)
	add a1, a1, a5
	bnez a0, xor_regs_4_
	ret
SYM_FUNC_END(xor_regs_4_)
EXPORT_SYMBOL(xor_regs_4_)

SYM_FUNC_START(xor_regs_5_)
	vsetvli a6, a0, e8, m8, ta, ma
	vle8.v v0, (a1)
	vle8.v v8, (a2)
	sub a0, a0, a6
	vxor.vv v0, v0, v8
	vle8.v v16, (a3)
	add a2, a2, a6
	vxor.vv v0, v0, v16
	vle8.v v24, (a4)
	add a3, a3, a6
	vxor.vv v0, v0, v24
	vle8.v v8, (a5)
	add a4, a4, a6
	vxor.vv v16, v0, v8
	add a5, a5, a6
	vse8.v v16, (a1)
	add a1, a1, a6
	bnez a0, xor_regs_5_
	ret
SYM_FUNC_END(xor_regs_5_)
EXPORT_SYMBOL(xor_regs_5_)
