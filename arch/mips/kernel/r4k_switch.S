/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995, 1996, 1998, 1999, 2002, 2003 Ralf Baechle
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1994, 1995, 1996, by Andreas Busse
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) 2000 MIPS Technologies, Inc.
 *    written by Carsten Langgaard, carstenl@mips.com
 */
#include <asm/asm.h>
#include <asm/cachectl.h>
#include <asm/mipsregs.h>
#include <asm/asm-offsets.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/thread_info.h>

#include <asm/asmmacro.h>

/*
 * task_struct *resume(task_struct *prev, task_struct *next,
 *		       struct thread_info *next_ti)
 */
	.align	5
	LEAF(resume)
	mfc0	t1, CP0_STATUS
	LONG_S	t1, THREAD_STATUS(a0)
	cpu_save_nonscratch a0
	LONG_S	ra, THREAD_REG31(a0)

#if defined(CONFIG_STACKPROTECTOR) && !defined(CONFIG_SMP)
	PTR_LA	t8, __stack_chk_guard
	LONG_L	t9, TASK_STACK_CANARY(a1)
	LONG_S	t9, 0(t8)
#endif

	/*
	 * The order of restoring the registers takes care of the race
	 * updating $28, $29 and kernelsp without disabling ints.
	 */
	move	$28, a2
	cpu_restore_nonscratch a1

	PTR_ADDU	t0, $28, _THREAD_SIZE - 32
	set_saved_sp	t0, t1, t2
	mfc0	t1, CP0_STATUS		/* Do we really need this? */
	li	a3, 0xff01
	and	t1, a3
	LONG_L	a2, THREAD_STATUS(a1)
	nor	a3, $0, a3
	and	a2, a3
	or	a2, t1
	mtc0	a2, CP0_STATUS
	move	v0, a0
	jr	ra
	END(resume)
