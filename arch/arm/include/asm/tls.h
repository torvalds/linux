/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASMARM_TLS_H
#define __ASMARM_TLS_H

#include <linux/compiler.h>
#include <asm/thread_info.h>

#ifdef __ASSEMBLY__
#include <asm/asm-offsets.h>
	.macro switch_tls_none, base, tp, tpuser, tmp1, tmp2
	.endm

	.macro switch_tls_v6k, base, tp, tpuser, tmp1, tmp2
	mrc	p15, 0, \tmp2, c13, c0, 2	@ get the user r/w register
	@ TLS register update is deferred until return to user space
	mcr	p15, 0, \tpuser, c13, c0, 2	@ set the user r/w register
	str	\tmp2, [\base, #TI_TP_VALUE + 4] @ save it
	.endm

	.macro switch_tls_v6, base, tp, tpuser, tmp1, tmp2
#ifdef CONFIG_SMP
ALT_SMP(nop)
ALT_UP_B(.L0_\@)
	.subsection 1
#endif
.L0_\@:
	ldr_va	\tmp1, elf_hwcap
	mov	\tmp2, #0xffff0fff
	tst	\tmp1, #HWCAP_TLS		@ hardware TLS available?
	streq	\tp, [\tmp2, #-15]		@ set TLS value at 0xffff0ff0
	beq	.L2_\@
	mcr	p15, 0, \tp, c13, c0, 3		@ yes, set TLS register
#ifdef CONFIG_SMP
	b	.L1_\@
	.previous
#endif
.L1_\@: switch_tls_v6k \base, \tp, \tpuser, \tmp1, \tmp2
.L2_\@:
	.endm

	.macro switch_tls_software, base, tp, tpuser, tmp1, tmp2
	mov	\tmp1, #0xffff0fff
	str	\tp, [\tmp1, #-15]		@ set TLS value at 0xffff0ff0
	.endm
#else
#include <asm/smp_plat.h>
#endif

#ifdef CONFIG_TLS_REG_EMUL
#define tls_emu		1
#define has_tls_reg		1
#define defer_tls_reg_update	0
#define switch_tls	switch_tls_none
#elif defined(CONFIG_CPU_V6)
#define tls_emu		0
#define has_tls_reg		(elf_hwcap & HWCAP_TLS)
#define defer_tls_reg_update	is_smp()
#define switch_tls	switch_tls_v6
#elif defined(CONFIG_CPU_32v6K)
#define tls_emu		0
#define has_tls_reg		1
#define defer_tls_reg_update	1
#define switch_tls	switch_tls_v6k
#else
#define tls_emu		0
#define has_tls_reg		0
#define defer_tls_reg_update	0
#define switch_tls	switch_tls_software
#endif

#ifndef __ASSEMBLY__

static inline void set_tls(unsigned long val)
{
	struct thread_info *thread;

	thread = current_thread_info();

	thread->tp_value[0] = val;

	/*
	 * This code runs with preemption enabled and therefore must
	 * be reentrant with respect to switch_tls.
	 *
	 * We need to ensure ordering between the shadow state and the
	 * hardware state, so that we don't corrupt the hardware state
	 * with a stale shadow state during context switch.
	 *
	 * If we're preempted here, switch_tls will load TPIDRURO from
	 * thread_info upon resuming execution and the following mcr
	 * is merely redundant.
	 */
	barrier();

	if (!tls_emu) {
		if (has_tls_reg && !defer_tls_reg_update) {
			asm("mcr p15, 0, %0, c13, c0, 3"
			    : : "r" (val));
		} else if (!has_tls_reg) {
#ifdef CONFIG_KUSER_HELPERS
			/*
			 * User space must never try to access this
			 * directly.  Expect your app to break
			 * eventually if you do so.  The user helper
			 * at 0xffff0fe0 must be used instead.  (see
			 * entry-armv.S for details)
			 */
			*((unsigned int *)0xffff0ff0) = val;
#endif
		}

	}
}

static inline unsigned long get_tpuser(void)
{
	unsigned long reg = 0;

	if (has_tls_reg && !tls_emu)
		__asm__("mrc p15, 0, %0, c13, c0, 2" : "=r" (reg));

	return reg;
}

static inline void set_tpuser(unsigned long val)
{
	/* Since TPIDRURW is fully context-switched (unlike TPIDRURO),
	 * we need not update thread_info.
	 */
	if (has_tls_reg && !tls_emu) {
		asm("mcr p15, 0, %0, c13, c0, 2"
		    : : "r" (val));
	}
}

static inline void flush_tls(void)
{
	set_tls(0);
	set_tpuser(0);
}

#endif
#endif	/* __ASMARM_TLS_H */
