###############################################################################
#
# MN10300 Context switch operation
#
# Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
# Written by David Howells (dhowells@redhat.com)
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public Licence
# as published by the Free Software Foundation; either version
# 2 of the Licence, or (at your option) any later version.
#
###############################################################################
#include <linux/sys.h>
#include <linux/linkage.h>
#include <asm/thread_info.h>
#include <asm/cpu-regs.h>
#ifdef CONFIG_SMP
#include <proc/smp-regs.h>
#endif /* CONFIG_SMP */

	.text

###############################################################################
#
# struct task_struct *__switch_to(struct thread_struct *prev,
#				  struct thread_struct *next,
#				  struct task_struct *prev_task)
#
###############################################################################
ENTRY(__switch_to)
	movm	[d2,d3,a2,a3,exreg1],(sp)
	or	EPSW_NMID,epsw

	mov	(44,sp),d2

	mov	d0,a0
	mov	d1,a1

	# save prev context
	mov	__switch_back,d0
	mov	sp,a2
	mov	a2,(THREAD_SP,a0)
	mov	a3,(THREAD_A3,a0)

#ifdef CONFIG_KGDB
	btst	0xff,(kgdb_single_step)
	bne	__switch_to__lift_sstep_bp
__switch_to__continue:
#endif
	mov	d0,(THREAD_PC,a0)

	mov	(THREAD_A3,a1),a3
	mov	(THREAD_SP,a1),a2

	# switch
	mov	a2,sp

	# load next context
	GET_THREAD_INFO a2
	mov	a2,(__current_ti)
	mov	(TI_task,a2),a2
	mov	a2,(__current)
#ifdef CONFIG_MN10300_CURRENT_IN_E2
	mov	a2,e2
#endif

	mov	(THREAD_PC,a1),a2
	mov	d2,d0			# for ret_from_fork
	mov	d0,a0			# for __switch_to

	jmp	(a2)

__switch_back:
	and	~EPSW_NMID,epsw
	ret	[d2,d3,a2,a3,exreg1],32

#ifdef CONFIG_KGDB
###############################################################################
#
# Lift the single-step breakpoints when the task being traced is switched out
# A0 = prev
# A1 = next
#
###############################################################################
__switch_to__lift_sstep_bp:
	add	-12,sp
	mov	a0,e4
	mov	a1,e5

	# Clear the single-step flag to prevent us coming this way until we get
	# switched back in
	bclr	0xff,(kgdb_single_step)

	# Remove first breakpoint
	mov	(kgdb_sstep_bp_addr),a2
	cmp	0,a2
	beq	1f
	movbu	(kgdb_sstep_bp),d0
	movbu	d0,(a2)
#if defined(CONFIG_MN10300_CACHE_FLUSH_ICACHE) || defined(CONFIG_MN10300_CACHE_INV_ICACHE)
	mov	a2,d0
	mov	a2,d1
	add	1,d1
	calls	flush_icache_range
#endif
1:

	# Remove second breakpoint
	mov	(kgdb_sstep_bp_addr+4),a2
	cmp	0,a2
	beq	2f
	movbu	(kgdb_sstep_bp+1),d0
	movbu	d0,(a2)
#if defined(CONFIG_MN10300_CACHE_FLUSH_ICACHE) || defined(CONFIG_MN10300_CACHE_INV_ICACHE)
	mov	a2,d0
	mov	a2,d1
	add	1,d1
	calls	flush_icache_range
#endif
2:

	# Change the resumption address and return
	mov	__switch_back__reinstall_sstep_bp,d0
	mov	e4,a0
	mov	e5,a1
	add	12,sp
	bra	__switch_to__continue

###############################################################################
#
# Reinstall the single-step breakpoints when the task being traced is switched
# back in (A1 points to the new thread_struct).
#
###############################################################################
__switch_back__reinstall_sstep_bp:
	add	-12,sp
	mov	a0,e4			# save the return value
	mov	0xff,d3

	# Reinstall first breakpoint
	mov	(kgdb_sstep_bp_addr),a2
	cmp	0,a2
	beq	1f
	movbu	(a2),d0
	movbu	d0,(kgdb_sstep_bp)
	movbu	d3,(a2)
#if defined(CONFIG_MN10300_CACHE_FLUSH_ICACHE) || defined(CONFIG_MN10300_CACHE_INV_ICACHE)
	mov	a2,d0
	mov	a2,d1
	add	1,d1
	calls	flush_icache_range
#endif
1:

	# Reinstall second breakpoint
	mov	(kgdb_sstep_bp_addr+4),a2
	cmp	0,a2
	beq	2f
	movbu	(a2),d0
	movbu	d0,(kgdb_sstep_bp+1)
	movbu	d3,(a2)
#if defined(CONFIG_MN10300_CACHE_FLUSH_ICACHE) || defined(CONFIG_MN10300_CACHE_INV_ICACHE)
	mov	a2,d0
	mov	a2,d1
	add	1,d1
	calls	flush_icache_range
#endif
2:

	mov	d3,(kgdb_single_step)

	# Restore the return value (the previous thread_struct pointer)
	mov	e4,a0
	mov	a0,d0
	add	12,sp
	bra	__switch_back

#endif /* CONFIG_KGDB */
