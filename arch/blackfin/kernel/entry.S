/*
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#include <linux/linkage.h>
#include <asm/thread_info.h>
#include <asm/errno.h>
#include <asm/blackfin.h>
#include <asm/asm-offsets.h>

#include <asm/context.S>

#ifdef CONFIG_EXCPT_IRQ_SYSC_L1
.section .l1.text
#else
.text
#endif

ENTRY(_ret_from_fork)
#ifdef CONFIG_IPIPE
	/*
	 * Hw IRQs are off on entry, and we don't want the scheduling tail
	 * code to starve high priority domains from interrupts while it
	 * runs. Therefore we first stall the root stage to have the
	 * virtual interrupt state reflect IMASK.
	 */
	p0.l = ___ipipe_root_status;
	p0.h = ___ipipe_root_status;
	r4 = [p0];
	bitset(r4, 0);
	[p0] = r4;
	/*
	 * Then we may enable hw IRQs, allowing preemption from high
	 * priority domains. schedule_tail() will do local_irq_enable()
	 * since Blackfin does not define __ARCH_WANT_UNLOCKED_CTXSW, so
	 * there is no need to unstall the root domain by ourselves
	 * afterwards.
	 */
	p0.l = _bfin_irq_flags;
	p0.h = _bfin_irq_flags;
	r4 = [p0];
	sti r4;
#endif /* CONFIG_IPIPE */
	SP += -12;
	pseudo_long_call _schedule_tail, p5;
	SP += 12;
	p1 = [sp++];
	r0 = [sp++];
	cc = p1 == 0;
	if cc jump .Lfork;
	sp += -12;
	call (p1);
	sp += 12;
.Lfork:
	RESTORE_CONTEXT
	rti;
ENDPROC(_ret_from_fork)

ENTRY(_sys_vfork)
	r0 = sp;
	r0 += 24;
	[--sp] = rets;
	SP += -12;
	pseudo_long_call _bfin_vfork, p2;
	SP += 12;
	rets = [sp++];
	rts;
ENDPROC(_sys_vfork)

ENTRY(_sys_clone)
	r0 = sp;
	r0 += 24;
	[--sp] = rets;
	SP += -12;
	pseudo_long_call _bfin_clone, p2;
	SP += 12;
	rets = [sp++];
	rts;
ENDPROC(_sys_clone)

ENTRY(_sys_rt_sigreturn)
	r0 = sp;
	r0 += 24;
	[--sp] = rets;
	SP += -12;
	pseudo_long_call _do_rt_sigreturn, p2;
	SP += 12;
	rets = [sp++];
	rts;
ENDPROC(_sys_rt_sigreturn)
