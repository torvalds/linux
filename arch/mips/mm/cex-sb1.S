/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2001,2002,2003 Broadcom Corporation
 */

#include <asm/asm.h>
#include <asm/regdef.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>
#include <asm/cacheops.h>
#include <asm/sibyte/board.h>

#define C0_ERRCTL     $26	      /* CP0: Error info */
#define C0_CERR_I     $27	      /* CP0: Icache error */
#define C0_CERR_D     $27,1	      /* CP0: Dcache error */

	/*
	 * Based on SiByte sample software cache-err/cerr.S
	 * CVS revision 1.8.  Only the 'unrecoverable' case
	 * is changed.
	 */

	.set	mips64
	.set	noreorder
	.set	noat

	/*
	 * sb1_cerr_vec: code to be copied to the Cache Error
	 * Exception vector.  The code must be pushed out to memory
	 * (either by copying to Kseg0 and Kseg1 both, or by flushing
	 * the L1 and L2) since it is fetched as 0xa0000100.
	 *
	 * NOTE: Be sure this handler is at most 28 instructions long
	 * since the final 16 bytes of the exception vector memory
	 * (0x170-0x17f) are used to preserve k0, k1, and ra.
	 */

LEAF(except_vec2_sb1)
	/*
	 * If this error is recoverable, we need to exit the handler
	 * without having dirtied any registers.  To do this,
	 * save/restore k0 and k1 from low memory (Useg is direct
	 * mapped while ERL=1). Note that we can't save to a
	 * CPU-specific location without ruining a register in the
	 * process.  This means we are vulnerable to data corruption
	 * whenever the handler is reentered by a second CPU.
	 */
	sd	k0,0x170($0)
	sd	k1,0x178($0)

#ifdef CONFIG_SB1_CEX_ALWAYS_FATAL
	j	handle_vec2_sb1
	 nop
#else
	/*
	 * M_ERRCTL_RECOVERABLE is bit 31, which makes it easy to tell
	 * if we can fast-path out of here for a h/w-recovered error.
	 */
	mfc0	k1,C0_ERRCTL
	bgtz	k1,attempt_recovery
	 sll	k0,k1,1

recovered_dcache:
	/*
	 * Unlock CacheErr-D (which in turn unlocks CacheErr-DPA).
	 * Ought to log the occurrence of this recovered dcache error.
	 */
	b	recovered
	 mtc0	$0,C0_CERR_D

attempt_recovery:
	/*
	 * k0 has C0_ERRCTL << 1, which puts 'DC' at bit 31.  Any
	 * Dcache errors we can recover from will take more extensive
	 * processing.	For now, they are considered "unrecoverable".
	 * Note that 'DC' becoming set (outside of ERL mode) will
	 * cause 'IC' to clear; so if there's an Icache error, we'll
	 * only find out about it if we recover from this error and
	 * continue executing.
	 */
	bltz	k0,unrecoverable
	 sll	k0,1

	/*
	 * k0 has C0_ERRCTL << 2, which puts 'IC' at bit 31.  If an
	 * Icache error isn't indicated, I'm not sure why we got here.
	 * Consider that case "unrecoverable" for now.
	 */
	bgez	k0,unrecoverable

attempt_icache_recovery:
	/*
	 * External icache errors are due to uncorrectable ECC errors
	 * in the L2 cache or Memory Controller and cannot be
	 * recovered here.
	 */
	 mfc0	k0,C0_CERR_I		/* delay slot */
	li	k1,1 << 26		/* ICACHE_EXTERNAL */
	and	k1,k0
	bnez	k1,unrecoverable
	 andi	k0,0x1fe0

	/*
	 * Since the error is internal, the 'IDX' field from
	 * CacheErr-I is valid and we can just invalidate all blocks
	 * in that set.
	 */
	cache	Index_Invalidate_I,(0<<13)(k0)
	cache	Index_Invalidate_I,(1<<13)(k0)
	cache	Index_Invalidate_I,(2<<13)(k0)
	cache	Index_Invalidate_I,(3<<13)(k0)

	/* Ought to log this recovered icache error */

recovered:
	/* Restore the saved registers */
	ld	k0,0x170($0)
	ld	k1,0x178($0)
	eret

unrecoverable:
	/* Unrecoverable Icache or Dcache error; log it and/or fail */
	j	handle_vec2_sb1
	 nop
#endif

END(except_vec2_sb1)

	LEAF(handle_vec2_sb1)
	mfc0	k0,CP0_CONFIG
	li	k1,~CONF_CM_CMASK
	and	k0,k0,k1
	ori	k0,k0,CONF_CM_UNCACHED
	mtc0	k0,CP0_CONFIG

	SSNOP
	SSNOP
	SSNOP
	SSNOP
	bnezl	$0, 1f
1:
	mfc0	k0, CP0_STATUS
	sll	k0, k0, 3			# check CU0 (kernel?)
	bltz	k0, 2f
	 nop

	/* Get a valid Kseg0 stack pointer.  Any task's stack pointer
	 * will do, although if we ever want to resume execution we
	 * better not have corrupted any state. */
	get_saved_sp
	move	sp, k1

2:
	j	sb1_cache_error
	 nop

	END(handle_vec2_sb1)
