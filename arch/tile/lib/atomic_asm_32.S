/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * Support routines for atomic operations.  Each function takes:
 *
 * r0: address to manipulate
 * r1: pointer to atomic lock guarding this operation (for ATOMIC_LOCK_REG)
 * r2: new value to write, or for cmpxchg/add_unless, value to compare against
 * r3: (cmpxchg/xchg_add_unless) new value to write or add;
 *     (atomic64 ops) high word of value to write
 * r4/r5: (cmpxchg64/add_unless64) new value to write or add
 *
 * The 32-bit routines return a "struct __get_user" so that the futex code
 * has an opportunity to return -EFAULT to the user if needed.
 * The 64-bit routines just return a "long long" with the value,
 * since they are only used from kernel space and don't expect to fault.
 * Support for 16-bit ops is included in the framework but we don't provide
 * any (x86_64 has an atomic_inc_short(), so we might want to some day).
 *
 * Note that the caller is advised to issue a suitable L1 or L2
 * prefetch on the address being manipulated to avoid extra stalls.
 * In addition, the hot path is on two icache lines, and we start with
 * a jump to the second line to make sure they are both in cache so
 * that we never stall waiting on icache fill while holding the lock.
 * (This doesn't work out with most 64-bit ops, since they consume
 * too many bundles, so may take an extra i-cache stall.)
 *
 * These routines set the INTERRUPT_CRITICAL_SECTION bit, just
 * like sys_cmpxchg(), so that NMIs like PERF_COUNT will not interrupt
 * the code, just page faults.
 *
 * If the load or store faults in a way that can be directly fixed in
 * the do_page_fault_ics() handler (e.g. a vmalloc reference) we fix it
 * directly, return to the instruction that faulted, and retry it.
 *
 * If the load or store faults in a way that potentially requires us
 * to release the atomic lock, then retry (e.g. a migrating PTE), we
 * reset the PC in do_page_fault_ics() to the "tns" instruction so
 * that on return we will reacquire the lock and restart the op.  We
 * are somewhat overloading the exception_table_entry notion by doing
 * this, since those entries are not normally used for migrating PTEs.
 *
 * If the main page fault handler discovers a bad address, it will see
 * the PC pointing to the "tns" instruction (due to the earlier
 * exception_table_entry processing in do_page_fault_ics), and
 * re-reset the PC to the fault handler, atomic_bad_address(), which
 * effectively takes over from the atomic op and can either return a
 * bad "struct __get_user" (for user addresses) or can just panic (for
 * bad kernel addresses).
 *
 * Note that if the value we would store is the same as what we
 * loaded, we bypass the load.  Other platforms with true atomics can
 * make the guarantee that a non-atomic __clear_bit(), for example,
 * can safely race with an atomic test_and_set_bit(); this example is
 * from bit_spinlock.h in slub_lock() / slub_unlock().  We can't do
 * that on Tile since the "atomic" op is really just a
 * read/modify/write, and can race with the non-atomic
 * read/modify/write.  However, if we can short-circuit the write when
 * it is not needed, in the atomic case, we avoid the race.
 */

#include <linux/linkage.h>
#include <asm/atomic.h>
#include <asm/page.h>
#include <asm/processor.h>

	.section .text.atomic,"ax"
ENTRY(__start_atomic_asm_code)

	.macro  atomic_op, name, bitwidth, body
	.align  64
STD_ENTRY_SECTION(__atomic\name, .text.atomic)
	{
	 movei  r24, 1
	 j      4f		/* branch to second cache line */
	}
1:	{
	 .ifc \bitwidth,16
	 lh     r22, r0
	 .else
	 lw     r22, r0
	 addi   r28, r0, 4
	 .endif
	}
	.ifc \bitwidth,64
	lw      r23, r28
	.endif
	\body /* set r24, and r25 if 64-bit */
	{
	 seq    r26, r22, r24
	 seq    r27, r23, r25
	}
	.ifc \bitwidth,64
	bbnst   r27, 2f
	.endif
	bbs     r26, 3f		/* skip write-back if it's the same value */
2:	{
	 .ifc \bitwidth,16
	 sh     r0, r24
	 .else
	 sw     r0, r24
	 .endif
	}
	.ifc \bitwidth,64
	sw      r28, r25
	.endif
	mf
3:	{
	 move   r0, r22
	 .ifc \bitwidth,64
	 move   r1, r23
	 .else
	 move   r1, zero
	 .endif
	 sw     ATOMIC_LOCK_REG_NAME, zero
	}
	mtspr   INTERRUPT_CRITICAL_SECTION, zero
	jrp     lr
4:	{
	 move   ATOMIC_LOCK_REG_NAME, r1
	 mtspr  INTERRUPT_CRITICAL_SECTION, r24
	}
#ifndef CONFIG_SMP
	j       1b		/* no atomic locks */
#else
	{
	 tns    r21, ATOMIC_LOCK_REG_NAME
	 moveli r23, 2048       /* maximum backoff time in cycles */
	}
	{
	 bzt    r21, 1b		/* branch if lock acquired */
	 moveli r25, 32         /* starting backoff time in cycles */
	}
5:	mtspr   INTERRUPT_CRITICAL_SECTION, zero
	mfspr   r26, CYCLE_LOW  /* get start point for this backoff */
6:	mfspr   r22, CYCLE_LOW  /* test to see if we've backed off enough */
	sub     r22, r22, r26
	slt     r22, r22, r25
	bbst    r22, 6b
	{
	 mtspr  INTERRUPT_CRITICAL_SECTION, r24
	 shli   r25, r25, 1     /* double the backoff; retry the tns */
	}
	{
	 tns    r21, ATOMIC_LOCK_REG_NAME
	 slt    r26, r23, r25   /* is the proposed backoff too big? */
	}
	{
	 bzt    r21, 1b		/* branch if lock acquired */
	 mvnz   r25, r26, r23
	}
	j       5b
#endif
	STD_ENDPROC(__atomic\name)
	.ifc \bitwidth,32
	.pushsection __ex_table,"a"
	.word   1b, __atomic\name
	.word   2b, __atomic\name
	.word   __atomic\name, __atomic_bad_address
	.popsection
	.endif
	.endm

atomic_op _cmpxchg, 32, "seq r26, r22, r2; { bbns r26, 3f; move r24, r3 }"
atomic_op _xchg, 32, "move r24, r2"
atomic_op _xchg_add, 32, "add r24, r22, r2"
atomic_op _xchg_add_unless, 32, \
	"sne r26, r22, r2; { bbns r26, 3f; add r24, r22, r3 }"
atomic_op _or, 32, "or r24, r22, r2"
atomic_op _andn, 32, "nor r2, r2, zero; and r24, r22, r2"
atomic_op _xor, 32, "xor r24, r22, r2"

atomic_op 64_cmpxchg, 64, "{ seq r26, r22, r2; seq r27, r23, r3 }; \
	{ bbns r26, 3f; move r24, r4 }; { bbns r27, 3f; move r25, r5 }"
atomic_op 64_xchg, 64, "{ move r24, r2; move r25, r3 }"
atomic_op 64_xchg_add, 64, "{ add r24, r22, r2; add r25, r23, r3 }; \
	slt_u r26, r24, r22; add r25, r25, r26"
atomic_op 64_xchg_add_unless, 64, \
	"{ sne r26, r22, r2; sne r27, r23, r3 }; \
	{ bbns r26, 3f; add r24, r22, r4 }; \
	{ bbns r27, 3f; add r25, r23, r5 }; \
	slt_u r26, r24, r22; add r25, r25, r26"

	jrp     lr              /* happy backtracer */

ENTRY(__end_atomic_asm_code)
