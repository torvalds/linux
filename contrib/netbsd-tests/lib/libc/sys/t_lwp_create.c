/* $NetBSD: t_lwp_create.c,v 1.2 2012/05/22 09:23:39 martin Exp $ */

/*-
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This code is partly based on code by Joel Sing <joel at sing.id.au>
 */

#include <atf-c.h>
#include <lwp.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <inttypes.h>
#include <errno.h>

#ifdef __alpha__
#include <machine/alpha_cpu.h>
#endif
#ifdef __amd64__
#include <machine/vmparam.h>
#include <machine/psl.h>
#endif
#ifdef __hppa__
#include <machine/psl.h>
#endif
#ifdef __i386__
#include <machine/segments.h>
#include <machine/psl.h>
#endif
#if defined(__m68k__) || defined(__sh3__) || defined __vax__
#include <machine/psl.h>
#endif

volatile lwpid_t the_lwp_id = 0;

static void lwp_main_func(void* arg)
{
	the_lwp_id = _lwp_self();
	_lwp_exit();
}

/*
 * Hard to document - see usage examples below.
 */
#define INVALID_UCONTEXT(ARCH,NAME,DESC)	\
static void ARCH##_##NAME(ucontext_t *);	\
ATF_TC(lwp_create_##ARCH##_fail_##NAME);	\
ATF_TC_HEAD(lwp_create_##ARCH##_fail_##NAME, tc)	\
{	\
	atf_tc_set_md_var(tc, "descr", "verify rejection of invalid ucontext " \
		"on " #ARCH " due to " DESC);	\
}	\
	\
ATF_TC_BODY(lwp_create_##ARCH##_fail_##NAME, tc)	\
{	\
	ucontext_t uc;		\
	lwpid_t lid;		\
	int error;		\
				\
	getcontext(&uc);	\
	uc.uc_flags = _UC_CPU;	\
	ARCH##_##NAME(&uc);	\
				\
	error = _lwp_create(&uc, 0, &lid);	\
	ATF_REQUIRE(error != 0 && errno == EINVAL);	\
}	\
static void ARCH##_##NAME(ucontext_t *uc)	\
{


ATF_TC(lwp_create_works);
ATF_TC_HEAD(lwp_create_works, tc)
{
	atf_tc_set_md_var(tc, "descr", "Verify creation of a lwp and waiting"
	    " for it to finish");
}

ATF_TC_BODY(lwp_create_works, tc)
{
	ucontext_t uc;
	lwpid_t lid;
	int error;
	void *stack;
	static const size_t ssize = 16*1024;

	stack = malloc(ssize);
	_lwp_makecontext(&uc, lwp_main_func, NULL, NULL, stack, ssize);

	error = _lwp_create(&uc, 0, &lid);
	ATF_REQUIRE(error == 0);

	error = _lwp_wait(lid, NULL);
	ATF_REQUIRE(error == 0);
	ATF_REQUIRE(lid == the_lwp_id);
}

INVALID_UCONTEXT(generic, no_uc_cpu, "not setting cpu registers")
	uc->uc_flags &= ~_UC_CPU;
}

#ifdef __alpha__
INVALID_UCONTEXT(alpha, pslset, "trying to clear the USERMODE flag")
	uc->uc_mcontext.__gregs[_REG_PS] &= ~ALPHA_PSL_USERMODE;
}
INVALID_UCONTEXT(alpha, pslclr, "trying to set a 'must be zero' flag")
	uc->uc_mcontext.__gregs[_REG_PS] |= ALPHA_PSL_IPL_HIGH;
}
#endif
#ifdef __amd64__
INVALID_UCONTEXT(amd64, untouchable_rflags, "forbidden rflags changed")
	uc->uc_mcontext.__gregs[_REG_RFLAGS] |= PSL_MBZ;
}
/*
 * XXX: add invalid GS/DS selector tests
 */
INVALID_UCONTEXT(amd64, pc_too_high,
     "instruction pointer outside userland address space")
	uc->uc_mcontext.__gregs[_REG_RIP] = VM_MAXUSER_ADDRESS;
}
#endif
#ifdef __arm__
INVALID_UCONTEXT(arm, invalid_mode, "psr or r15 set to non-user-mode")
	uc->uc_mcontext.__gregs[_REG_PC] |= 0x1f /*PSR_SYS32_MODE*/;
	uc->uc_mcontext.__gregs[_REG_CPSR] |= 0x03 /*R15_MODE_SVC*/;
}
#endif
#ifdef __hppa__
INVALID_UCONTEXT(hppa, invalid_1, "set illegal bits in psw")
	uc->uc_mcontext.__gregs[_REG_PSW] |= PSW_MBZ;
}
INVALID_UCONTEXT(hppa, invalid_0, "clear illegal bits in psw")
	uc->uc_mcontext.__gregs[_REG_PSW] &= ~PSW_MBS;
}
#endif
#ifdef __i386__
INVALID_UCONTEXT(i386, untouchable_eflags, "changing forbidden eflags")
	uc->uc_mcontext.__gregs[_REG_EFL] |= PSL_IOPL;
}
INVALID_UCONTEXT(i386, priv_escalation, "modifying priviledge level")
	uc->uc_mcontext.__gregs[_REG_CS] &= ~SEL_RPL;
}
#endif
#ifdef __m68k__
INVALID_UCONTEXT(m68k, invalid_ps_bits,
    "setting forbidden bits in the ps register")
	uc->uc_mcontext.__gregs[_REG_PS] |= (PSL_MBZ|PSL_IPL|PSL_S);
}
#endif
#ifdef __sh3__
INVALID_UCONTEXT(sh3, modify_userstatic,
    "modifying illegal bits in the status register")
	uc->uc_mcontext.__gregs[_REG_SR] |= PSL_MD;
}
#endif
#ifdef __sparc__
INVALID_UCONTEXT(sparc, pc_odd, "mis-aligned instruction pointer")
	uc->uc_mcontext.__gregs[_REG_PC] = 0x100002;
}
INVALID_UCONTEXT(sparc, npc_odd, "mis-aligned next instruction pointer")
	uc->uc_mcontext.__gregs[_REG_nPC] = 0x100002;
}
INVALID_UCONTEXT(sparc, pc_null, "NULL instruction pointer")
	uc->uc_mcontext.__gregs[_REG_PC] = 0;
}
INVALID_UCONTEXT(sparc, npc_null, "NULL next instruction pointer")
	uc->uc_mcontext.__gregs[_REG_nPC] = 0;
}
#endif
#ifdef __vax__
INVALID_UCONTEXT(vax, psl_0, "clearing forbidden bits in psl")
	uc->uc_mcontext.__gregs[_REG_PSL] &= ~(PSL_U | PSL_PREVU);
}
INVALID_UCONTEXT(vax, psl_1, "setting forbidden bits in psl")
	uc->uc_mcontext.__gregs[_REG_PSL] |= PSL_IPL | PSL_IS;
}
INVALID_UCONTEXT(vax, psl_cm, "setting CM bit in psl")
	uc->uc_mcontext.__gregs[_REG_PSL] |= PSL_CM;
}
#endif

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, lwp_create_works);
	ATF_TP_ADD_TC(tp, lwp_create_generic_fail_no_uc_cpu);
#ifdef __alpha__
	ATF_TP_ADD_TC(tp, lwp_create_alpha_fail_pslset);
	ATF_TP_ADD_TC(tp, lwp_create_alpha_fail_pslclr);
#endif
#ifdef __amd64__
	ATF_TP_ADD_TC(tp, lwp_create_amd64_fail_untouchable_rflags);
	ATF_TP_ADD_TC(tp, lwp_create_amd64_fail_pc_too_high);
#endif
#ifdef __arm__
	ATF_TP_ADD_TC(tp, lwp_create_arm_fail_invalid_mode);
#endif
#ifdef __hppa__
	ATF_TP_ADD_TC(tp, lwp_create_hppa_fail_invalid_1);
	ATF_TP_ADD_TC(tp, lwp_create_hppa_fail_invalid_0);
#endif
#ifdef __i386__
	ATF_TP_ADD_TC(tp, lwp_create_i386_fail_untouchable_eflags);
	ATF_TP_ADD_TC(tp, lwp_create_i386_fail_priv_escalation);
#endif
#ifdef __m68k__
	ATF_TP_ADD_TC(tp, lwp_create_m68k_fail_invalid_ps_bits);
#endif
#ifdef __sh3__
	ATF_TP_ADD_TC(tp, lwp_create_sh3_fail_modify_userstatic);
#endif
#ifdef __sparc__
	ATF_TP_ADD_TC(tp, lwp_create_sparc_fail_pc_odd);
	ATF_TP_ADD_TC(tp, lwp_create_sparc_fail_npc_odd);
	ATF_TP_ADD_TC(tp, lwp_create_sparc_fail_pc_null);
	ATF_TP_ADD_TC(tp, lwp_create_sparc_fail_npc_null);
#endif
#ifdef __vax__
	ATF_TP_ADD_TC(tp, lwp_create_vax_fail_psl_0);
	ATF_TP_ADD_TC(tp, lwp_create_vax_fail_psl_1);
	ATF_TP_ADD_TC(tp, lwp_create_vax_fail_psl_cm);
#endif
	return atf_no_error();
}
