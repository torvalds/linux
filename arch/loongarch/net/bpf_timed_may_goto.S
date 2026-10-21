/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Author: George Guo <guodongtai@kylinos.cn>
 * Copyright (C) 2026 KylinSoft Corporation.
 */

#include <asm/asmmacro.h>
#include <asm/regdef.h>
#include <linux/export.h>
#include <linux/linkage.h>

SYM_FUNC_START(arch_bpf_timed_may_goto)
	addi.d		sp, sp, -64
	st.d		ra, sp, 56

	/* Save BPF registers R0 - R5 (a5, a0 - a4) */
	st.d		a5, sp, 8
	st.d		a0, sp, 16
	st.d		a1, sp, 24
	st.d		a2, sp, 32
	st.d		a3, sp, 40
	st.d		a4, sp, 48

	/*
	 * BPF_REG_AX (t0) holds the offset passed in by the verifier;
	 * add it to BPF_REG_FP (s4) to get the pointer to the count and
	 * timestamp, then pass it as the first argument in a0.
	 *
	 * The verifier emits a load using FP right before this call,
	 * so BPF_REG_FP (s4) is always set up by the JIT in this case.
	 */
	add.d		a0, t0, s4
	bl		bpf_check_timed_may_goto
	/* BPF_REG_AX (t0) will be stored into count, so move the return value to it. */
	move		t0, a0

	ld.d		ra, sp, 56
	ld.d		a5, sp, 8
	ld.d		a0, sp, 16
	ld.d		a1, sp, 24
	ld.d		a2, sp, 32
	ld.d		a3, sp, 40
	ld.d		a4, sp, 48
	addi.d		sp, sp, 64

	jr		ra
SYM_FUNC_END(arch_bpf_timed_may_goto)
