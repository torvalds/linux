/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1991, 1992  Linus Torvalds
 * Copyright (C) 1994 - 2000  Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */


static inline int
setup_sigcontext(struct pt_regs *regs, struct sigcontext __user *sc)
{
	int err = 0;
	int i;

	err |= __put_user(regs->cp0_epc, &sc->sc_pc);

	err |= __put_user(0, &sc->sc_regs[0]);
	for (i = 1; i < 32; i++)
		err |= __put_user(regs->regs[i], &sc->sc_regs[i]);

	err |= __put_user(regs->hi, &sc->sc_mdhi);
	err |= __put_user(regs->lo, &sc->sc_mdlo);
	if (cpu_has_dsp) {
		err |= __put_user(mfhi1(), &sc->sc_hi1);
		err |= __put_user(mflo1(), &sc->sc_lo1);
		err |= __put_user(mfhi2(), &sc->sc_hi2);
		err |= __put_user(mflo2(), &sc->sc_lo2);
		err |= __put_user(mfhi3(), &sc->sc_hi3);
		err |= __put_user(mflo3(), &sc->sc_lo3);
		err |= __put_user(rddsp(DSP_MASK), &sc->sc_dsp);
	}

	err |= __put_user(!!used_math(), &sc->sc_used_math);

	if (used_math()) {
		/*
		 * Save FPU state to signal context.  Signal handler
		 * will "inherit" current FPU state.
		 */
		preempt_disable();

		if (!is_fpu_owner()) {
			own_fpu();
			restore_fp(current);
		}
		err |= save_fp_context(sc);

		preempt_enable();
	}
	return err;
}

static inline int
restore_sigcontext(struct pt_regs *regs, struct sigcontext __user *sc)
{
	unsigned int used_math;
	unsigned long treg;
	int err = 0;
	int i;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

	err |= __get_user(regs->cp0_epc, &sc->sc_pc);
	err |= __get_user(regs->hi, &sc->sc_mdhi);
	err |= __get_user(regs->lo, &sc->sc_mdlo);
	if (cpu_has_dsp) {
		err |= __get_user(treg, &sc->sc_hi1); mthi1(treg);
		err |= __get_user(treg, &sc->sc_lo1); mtlo1(treg);
		err |= __get_user(treg, &sc->sc_hi2); mthi2(treg);
		err |= __get_user(treg, &sc->sc_lo2); mtlo2(treg);
		err |= __get_user(treg, &sc->sc_hi3); mthi3(treg);
		err |= __get_user(treg, &sc->sc_lo3); mtlo3(treg);
		err |= __get_user(treg, &sc->sc_dsp); wrdsp(treg, DSP_MASK);
	}

	for (i = 1; i < 32; i++)
		err |= __get_user(regs->regs[i], &sc->sc_regs[i]);

	err |= __get_user(used_math, &sc->sc_used_math);
	conditional_used_math(used_math);

	preempt_disable();

	if (used_math()) {
		/* restore fpu context if we have used it before */
		own_fpu();
		err |= restore_fp_context(sc);
	} else {
		/* signal handler may have used FPU.  Give it up. */
		lose_fpu();
	}

	preempt_enable();

	return err;
}

/*
 * Determine which stack to use..
 */
static inline void __user *
get_sigframe(struct k_sigaction *ka, struct pt_regs *regs, size_t frame_size)
{
	unsigned long sp;

	/* Default to using normal stack */
	sp = regs->regs[29];

	/*
	 * FPU emulator may have it's own trampoline active just
	 * above the user stack, 16-bytes before the next lowest
	 * 16 byte boundary.  Try to avoid trashing it.
	 */
	sp -= 32;

	/* This is the X/Open sanctioned signal stack switching.  */
	if ((ka->sa.sa_flags & SA_ONSTACK) && (sas_ss_flags (sp) == 0))
		sp = current->sas_ss_sp + current->sas_ss_size;

	return (void __user *)((sp - frame_size) & (ICACHE_REFILLS_WORKAROUND_WAR ? ~(cpu_icache_line_size()-1) : ALMASK));
}

static inline int install_sigtramp(unsigned int __user *tramp,
	unsigned int syscall)
{
	int err;

	/*
	 * Set up the return code ...
	 *
	 *         li      v0, __NR__foo_sigreturn
	 *         syscall
	 */

	err = __put_user(0x24020000 + syscall, tramp + 0);
	err |= __put_user(0x0000000c          , tramp + 1);
	if (ICACHE_REFILLS_WORKAROUND_WAR) {
		err |= __put_user(0, tramp + 2);
		err |= __put_user(0, tramp + 3);
		err |= __put_user(0, tramp + 4);
		err |= __put_user(0, tramp + 5);
		err |= __put_user(0, tramp + 6);
		err |= __put_user(0, tramp + 7);
	}
	flush_cache_sigtramp((unsigned long) tramp);

	return err;
}
