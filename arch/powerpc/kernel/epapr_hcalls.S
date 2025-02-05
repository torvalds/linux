/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2012 Freescale Semiconductor, Inc.
 */

#include <linux/export.h>
#include <linux/threads.h>
#include <asm/epapr_hcalls.h>
#include <asm/reg.h>
#include <asm/page.h>
#include <asm/cputable.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/asm-compat.h>
#include <asm/asm-offsets.h>

#ifndef CONFIG_PPC64
/* epapr_ev_idle() was derived from e500_idle() */
_GLOBAL(epapr_ev_idle)
	PPC_LL	r4, TI_LOCAL_FLAGS(r2)	/* set napping bit */
	ori	r4, r4,_TLF_NAPPING	/* so when we take an exception */
	PPC_STL	r4, TI_LOCAL_FLAGS(r2)	/* it will return to our caller */

#ifdef CONFIG_BOOKE
	wrteei	1
#else
	mfmsr	r4
	ori	r4, r4, MSR_EE
	mtmsr	r4
#endif

idle_loop:
	LOAD_REG_IMMEDIATE(r11, EV_HCALL_TOKEN(EV_IDLE))

.global epapr_ev_idle_start
epapr_ev_idle_start:
	li	r3, -1
	nop
	nop
	nop

	/*
	 * Guard against spurious wakeups from a hypervisor --
	 * only interrupt will cause us to return to LR due to
	 * _TLF_NAPPING.
	 */
	b	idle_loop
#endif

/* Hypercall entry point. Will be patched with device tree instructions. */
.global epapr_hypercall_start
epapr_hypercall_start:
	li	r3, -1
	nop
	nop
	nop
	blr
EXPORT_SYMBOL(epapr_hypercall_start)
