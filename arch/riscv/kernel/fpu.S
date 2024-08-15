/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#include <linux/linkage.h>

#include <asm/asm.h>
#include <asm/csr.h>
#include <asm/asm-offsets.h>

ENTRY(__fstate_save)
	li  a2,  TASK_THREAD_F0
	add a0, a0, a2
	li t1, SR_FS
	csrs CSR_STATUS, t1
	frcsr t0
	fsd f0,  TASK_THREAD_F0_F0(a0)
	fsd f1,  TASK_THREAD_F1_F0(a0)
	fsd f2,  TASK_THREAD_F2_F0(a0)
	fsd f3,  TASK_THREAD_F3_F0(a0)
	fsd f4,  TASK_THREAD_F4_F0(a0)
	fsd f5,  TASK_THREAD_F5_F0(a0)
	fsd f6,  TASK_THREAD_F6_F0(a0)
	fsd f7,  TASK_THREAD_F7_F0(a0)
	fsd f8,  TASK_THREAD_F8_F0(a0)
	fsd f9,  TASK_THREAD_F9_F0(a0)
	fsd f10, TASK_THREAD_F10_F0(a0)
	fsd f11, TASK_THREAD_F11_F0(a0)
	fsd f12, TASK_THREAD_F12_F0(a0)
	fsd f13, TASK_THREAD_F13_F0(a0)
	fsd f14, TASK_THREAD_F14_F0(a0)
	fsd f15, TASK_THREAD_F15_F0(a0)
	fsd f16, TASK_THREAD_F16_F0(a0)
	fsd f17, TASK_THREAD_F17_F0(a0)
	fsd f18, TASK_THREAD_F18_F0(a0)
	fsd f19, TASK_THREAD_F19_F0(a0)
	fsd f20, TASK_THREAD_F20_F0(a0)
	fsd f21, TASK_THREAD_F21_F0(a0)
	fsd f22, TASK_THREAD_F22_F0(a0)
	fsd f23, TASK_THREAD_F23_F0(a0)
	fsd f24, TASK_THREAD_F24_F0(a0)
	fsd f25, TASK_THREAD_F25_F0(a0)
	fsd f26, TASK_THREAD_F26_F0(a0)
	fsd f27, TASK_THREAD_F27_F0(a0)
	fsd f28, TASK_THREAD_F28_F0(a0)
	fsd f29, TASK_THREAD_F29_F0(a0)
	fsd f30, TASK_THREAD_F30_F0(a0)
	fsd f31, TASK_THREAD_F31_F0(a0)
	sw t0, TASK_THREAD_FCSR_F0(a0)
	csrc CSR_STATUS, t1
	ret
ENDPROC(__fstate_save)

ENTRY(__fstate_restore)
	li  a2,  TASK_THREAD_F0
	add a0, a0, a2
	li t1, SR_FS
	lw t0, TASK_THREAD_FCSR_F0(a0)
	csrs CSR_STATUS, t1
	fld f0,  TASK_THREAD_F0_F0(a0)
	fld f1,  TASK_THREAD_F1_F0(a0)
	fld f2,  TASK_THREAD_F2_F0(a0)
	fld f3,  TASK_THREAD_F3_F0(a0)
	fld f4,  TASK_THREAD_F4_F0(a0)
	fld f5,  TASK_THREAD_F5_F0(a0)
	fld f6,  TASK_THREAD_F6_F0(a0)
	fld f7,  TASK_THREAD_F7_F0(a0)
	fld f8,  TASK_THREAD_F8_F0(a0)
	fld f9,  TASK_THREAD_F9_F0(a0)
	fld f10, TASK_THREAD_F10_F0(a0)
	fld f11, TASK_THREAD_F11_F0(a0)
	fld f12, TASK_THREAD_F12_F0(a0)
	fld f13, TASK_THREAD_F13_F0(a0)
	fld f14, TASK_THREAD_F14_F0(a0)
	fld f15, TASK_THREAD_F15_F0(a0)
	fld f16, TASK_THREAD_F16_F0(a0)
	fld f17, TASK_THREAD_F17_F0(a0)
	fld f18, TASK_THREAD_F18_F0(a0)
	fld f19, TASK_THREAD_F19_F0(a0)
	fld f20, TASK_THREAD_F20_F0(a0)
	fld f21, TASK_THREAD_F21_F0(a0)
	fld f22, TASK_THREAD_F22_F0(a0)
	fld f23, TASK_THREAD_F23_F0(a0)
	fld f24, TASK_THREAD_F24_F0(a0)
	fld f25, TASK_THREAD_F25_F0(a0)
	fld f26, TASK_THREAD_F26_F0(a0)
	fld f27, TASK_THREAD_F27_F0(a0)
	fld f28, TASK_THREAD_F28_F0(a0)
	fld f29, TASK_THREAD_F29_F0(a0)
	fld f30, TASK_THREAD_F30_F0(a0)
	fld f31, TASK_THREAD_F31_F0(a0)
	fscsr t0
	csrc CSR_STATUS, t1
	ret
ENDPROC(__fstate_restore)
