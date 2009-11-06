/*
 * Copyright 2007-2008 Analog Devices Inc.
 *              Philippe Gerum <rpm@xenomai.org>
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/linkage.h>
#include <asm/blackfin.h>
#include <asm/cache.h>
#include <asm/asm-offsets.h>
#include <asm/rwlock.h>
#include <asm/cplb.h>

.text

.macro coreslot_loadaddr reg:req
	\reg\().l = _corelock;
	\reg\().h = _corelock;
.endm

/*
 * r0 = address of atomic data to flush and invalidate (32bit).
 *
 * Clear interrupts and return the old mask.
 * We assume that no atomic data can span cachelines.
 *
 * Clobbers: r2:0, p0
 */
ENTRY(_get_core_lock)
	r1 = -L1_CACHE_BYTES;
	r1 = r0 & r1;
	cli r0;
	coreslot_loadaddr p0;
.Lretry_corelock:
	testset (p0);
	if cc jump .Ldone_corelock;
	SSYNC(r2);
	jump .Lretry_corelock
.Ldone_corelock:
	p0 = r1;
	CSYNC(r2);
	flushinv[p0];
	SSYNC(r2);
	rts;
ENDPROC(_get_core_lock)

/*
 * r0 = address of atomic data in uncacheable memory region (32bit).
 *
 * Clear interrupts and return the old mask.
 *
 * Clobbers: r0, p0
 */
ENTRY(_get_core_lock_noflush)
	cli r0;
	coreslot_loadaddr p0;
.Lretry_corelock_noflush:
	testset (p0);
	if cc jump .Ldone_corelock_noflush;
	SSYNC(r2);
	jump .Lretry_corelock_noflush
.Ldone_corelock_noflush:
	rts;
ENDPROC(_get_core_lock_noflush)

/*
 * r0 = interrupt mask to restore.
 * r1 = address of atomic data to flush and invalidate (32bit).
 *
 * Interrupts are masked on entry (see _get_core_lock).
 * Clobbers: r2:0, p0
 */
ENTRY(_put_core_lock)
	/* Write-through cache assumed, so no flush needed here. */
	coreslot_loadaddr p0;
	r1 = 0;
	[p0] = r1;
	SSYNC(r2);
	sti r0;
	rts;
ENDPROC(_put_core_lock)

#ifdef __ARCH_SYNC_CORE_DCACHE

ENTRY(___raw_smp_mark_barrier_asm)
	[--sp] = rets;
	[--sp] = ( r7:5 );
	[--sp] = r0;
	[--sp] = p1;
	[--sp] = p0;
	call _get_core_lock_noflush;

	/*
	 * Calculate current core mask
	 */
	GET_CPUID(p1, r7);
	r6 = 1;
	r6 <<= r7;

	/*
	 * Set bit of other cores in barrier mask. Don't change current core bit.
	 */
	p1.l = _barrier_mask;
	p1.h = _barrier_mask;
	r7 = [p1];
	r5 = r7 & r6;
	r7 = ~r6;
	cc = r5 == 0;
	if cc jump 1f;
	r7 = r7 | r6;
1:
	[p1] = r7;
	SSYNC(r2);

	call _put_core_lock;
	p0 = [sp++];
	p1 = [sp++];
	r0 = [sp++];
	( r7:5 ) = [sp++];
	rets = [sp++];
	rts;
ENDPROC(___raw_smp_mark_barrier_asm)

ENTRY(___raw_smp_check_barrier_asm)
	[--sp] = rets;
	[--sp] = ( r7:5 );
	[--sp] = r0;
	[--sp] = p1;
	[--sp] = p0;
	call _get_core_lock_noflush;

	/*
	 * Calculate current core mask
	 */
	GET_CPUID(p1, r7);
	r6 = 1;
	r6 <<= r7;

	/*
	 * Clear current core bit in barrier mask if it is set.
	 */
	p1.l = _barrier_mask;
	p1.h = _barrier_mask;
	r7 = [p1];
	r5 = r7 & r6;
	cc = r5 == 0;
	if cc jump 1f;
	r6 = ~r6;
	r7 = r7 & r6;
	[p1] = r7;
	SSYNC(r2);

	call _put_core_lock;

	/*
	 * Invalidate the entire D-cache of current core.
	 */
	sp += -12;
	call _resync_core_dcache
	sp += 12;
	jump 2f;
1:
	call _put_core_lock;
2:
	p0 = [sp++];
	p1 = [sp++];
	r0 = [sp++];
	( r7:5 ) = [sp++];
	rets = [sp++];
	rts;
ENDPROC(___raw_smp_check_barrier_asm)

/*
 * r0 = irqflags
 * r1 = address of atomic data
 *
 * Clobbers: r2:0, p1:0
 */
_start_lock_coherent:

	[--sp] = rets;
	[--sp] = ( r7:6 );
	r7 = r0;
	p1 = r1;

	/*
	 * Determine whether the atomic data was previously
	 * owned by another CPU (=r6).
	 */
	GET_CPUID(p0, r2);
	r1 = 1;
	r1 <<= r2;
	r2 = ~r1;

	r1 = [p1];
	r1 >>= 28;   /* CPU fingerprints are stored in the high nibble. */
	r6 = r1 & r2;
	r1 = [p1];
	r1 <<= 4;
	r1 >>= 4;
	[p1] = r1;

	/*
	 * Release the core lock now, but keep IRQs disabled while we are
	 * performing the remaining housekeeping chores for the current CPU.
	 */
	coreslot_loadaddr p0;
	r1 = 0;
	[p0] = r1;

	/*
	 * If another CPU has owned the same atomic section before us,
	 * then our D-cached copy of the shared data protected by the
	 * current spin/write_lock may be obsolete.
	 */
	cc = r6 == 0;
	if cc jump .Lcache_synced

	/*
	 * Invalidate the entire D-cache of the current core.
	 */
	sp += -12;
	call _resync_core_dcache
	sp += 12;

.Lcache_synced:
	SSYNC(r2);
	sti r7;
	( r7:6 ) = [sp++];
	rets = [sp++];
	rts

/*
 * r0 = irqflags
 * r1 = address of atomic data
 *
 * Clobbers: r2:0, p1:0
 */
_end_lock_coherent:

	p1 = r1;
	GET_CPUID(p0, r2);
	r2 += 28;
	r1 = 1;
	r1 <<= r2;
	r2 = [p1];
	r2 = r1 | r2;
	[p1] = r2;
	r1 = p1;
	jump _put_core_lock;

#endif /* __ARCH_SYNC_CORE_DCACHE */

/*
 * r0 = &spinlock->lock
 *
 * Clobbers: r3:0, p1:0
 */
ENTRY(___raw_spin_is_locked_asm)
	p1 = r0;
	[--sp] = rets;
	call _get_core_lock;
	r3 = [p1];
	cc = bittst( r3, 0 );
	r3 = cc;
	r1 = p1;
	call _put_core_lock;
	rets = [sp++];
	r0 = r3;
	rts;
ENDPROC(___raw_spin_is_locked_asm)

/*
 * r0 = &spinlock->lock
 *
 * Clobbers: r3:0, p1:0
 */
ENTRY(___raw_spin_lock_asm)
	p1 = r0;
	[--sp] = rets;
.Lretry_spinlock:
	call _get_core_lock;
	r1 = p1;
	r2 = [p1];
	cc = bittst( r2, 0 );
	if cc jump .Lbusy_spinlock
#ifdef __ARCH_SYNC_CORE_DCACHE
	r3 = p1;
	bitset ( r2, 0 ); /* Raise the lock bit. */
	[p1] = r2;
	call _start_lock_coherent
#else
	r2 = 1;
	[p1] = r2;
	call _put_core_lock;
#endif
	rets = [sp++];
	rts;

.Lbusy_spinlock:
	/* We don't touch the atomic area if busy, so that flush
	   will behave like nop in _put_core_lock. */
	call _put_core_lock;
	SSYNC(r2);
	r0 = p1;
	jump .Lretry_spinlock
ENDPROC(___raw_spin_lock_asm)

/*
 * r0 = &spinlock->lock
 *
 * Clobbers: r3:0, p1:0
 */
ENTRY(___raw_spin_trylock_asm)
	p1 = r0;
	[--sp] = rets;
	call _get_core_lock;
	r1 = p1;
	r3 = [p1];
	cc = bittst( r3, 0 );
	if cc jump .Lfailed_trylock
#ifdef __ARCH_SYNC_CORE_DCACHE
	bitset ( r3, 0 ); /* Raise the lock bit. */
	[p1] = r3;
	call _start_lock_coherent
#else
	r2 = 1;
	[p1] = r2;
	call _put_core_lock;
#endif
	r0 = 1;
	rets = [sp++];
	rts;
.Lfailed_trylock:
	call _put_core_lock;
	r0 = 0;
	rets = [sp++];
	rts;
ENDPROC(___raw_spin_trylock_asm)

/*
 * r0 = &spinlock->lock
 *
 * Clobbers: r2:0, p1:0
 */
ENTRY(___raw_spin_unlock_asm)
	p1 = r0;
	[--sp] = rets;
	call _get_core_lock;
	r2 = [p1];
	bitclr ( r2, 0 );
	[p1] = r2;
	r1 = p1;
#ifdef __ARCH_SYNC_CORE_DCACHE
	call _end_lock_coherent
#else
	call _put_core_lock;
#endif
	rets = [sp++];
	rts;
ENDPROC(___raw_spin_unlock_asm)

/*
 * r0 = &rwlock->lock
 *
 * Clobbers: r2:0, p1:0
 */
ENTRY(___raw_read_lock_asm)
	p1 = r0;
	[--sp] = rets;
	call _get_core_lock;
.Lrdlock_try:
	r1 = [p1];
	r1 += -1;
	[p1] = r1;
	cc = r1 < 0;
	if cc jump .Lrdlock_failed
	r1 = p1;
#ifdef __ARCH_SYNC_CORE_DCACHE
	call _start_lock_coherent
#else
	call _put_core_lock;
#endif
	rets = [sp++];
	rts;

.Lrdlock_failed:
	r1 += 1;
	[p1] = r1;
.Lrdlock_wait:
	r1 = p1;
	call _put_core_lock;
	SSYNC(r2);
	r0 = p1;
	call _get_core_lock;
	r1 = [p1];
	cc = r1 < 2;
	if cc jump .Lrdlock_wait;
	jump .Lrdlock_try
ENDPROC(___raw_read_lock_asm)

/*
 * r0 = &rwlock->lock
 *
 * Clobbers: r3:0, p1:0
 */
ENTRY(___raw_read_trylock_asm)
	p1 = r0;
	[--sp] = rets;
	call _get_core_lock;
	r1 = [p1];
	cc = r1 <= 0;
	if cc jump .Lfailed_tryrdlock;
	r1 += -1;
	[p1] = r1;
	r1 = p1;
#ifdef __ARCH_SYNC_CORE_DCACHE
	call _start_lock_coherent
#else
	call _put_core_lock;
#endif
	rets = [sp++];
	r0 = 1;
	rts;
.Lfailed_tryrdlock:
	r1 = p1;
	call _put_core_lock;
	rets = [sp++];
	r0 = 0;
	rts;
ENDPROC(___raw_read_trylock_asm)

/*
 * r0 = &rwlock->lock
 *
 * Note: Processing controlled by a reader lock should not have
 * any side-effect on cache issues with the other core, so we
 * just release the core lock and exit (no _end_lock_coherent).
 *
 * Clobbers: r3:0, p1:0
 */
ENTRY(___raw_read_unlock_asm)
	p1 = r0;
	[--sp] = rets;
	call _get_core_lock;
	r1 = [p1];
	r1 += 1;
	[p1] = r1;
	r1 = p1;
	call _put_core_lock;
	rets = [sp++];
	rts;
ENDPROC(___raw_read_unlock_asm)

/*
 * r0 = &rwlock->lock
 *
 * Clobbers: r3:0, p1:0
 */
ENTRY(___raw_write_lock_asm)
	p1 = r0;
	r3.l = lo(RW_LOCK_BIAS);
	r3.h = hi(RW_LOCK_BIAS);
	[--sp] = rets;
	call _get_core_lock;
.Lwrlock_try:
	r1 = [p1];
	r1 = r1 - r3;
#ifdef __ARCH_SYNC_CORE_DCACHE
	r2 = r1;
	r2 <<= 4;
	r2 >>= 4;
	cc = r2 == 0;
#else
	cc = r1 == 0;
#endif
	if !cc jump .Lwrlock_wait
	[p1] = r1;
	r1 = p1;
#ifdef __ARCH_SYNC_CORE_DCACHE
	call _start_lock_coherent
#else
	call _put_core_lock;
#endif
	rets = [sp++];
	rts;

.Lwrlock_wait:
	r1 = p1;
	call _put_core_lock;
	SSYNC(r2);
	r0 = p1;
	call _get_core_lock;
	r1 = [p1];
#ifdef __ARCH_SYNC_CORE_DCACHE
	r1 <<= 4;
	r1 >>= 4;
#endif
	cc = r1 == r3;
	if !cc jump .Lwrlock_wait;
	jump .Lwrlock_try
ENDPROC(___raw_write_lock_asm)

/*
 * r0 = &rwlock->lock
 *
 * Clobbers: r3:0, p1:0
 */
ENTRY(___raw_write_trylock_asm)
	p1 = r0;
	[--sp] = rets;
	call _get_core_lock;
	r1 = [p1];
	r2.l = lo(RW_LOCK_BIAS);
	r2.h = hi(RW_LOCK_BIAS);
	cc = r1 == r2;
	if !cc jump .Lfailed_trywrlock;
#ifdef __ARCH_SYNC_CORE_DCACHE
	r1 >>= 28;
	r1 <<= 28;
#else
	r1 = 0;
#endif
	[p1] = r1;
	r1 = p1;
#ifdef __ARCH_SYNC_CORE_DCACHE
	call _start_lock_coherent
#else
	call _put_core_lock;
#endif
	rets = [sp++];
	r0 = 1;
	rts;

.Lfailed_trywrlock:
	r1 = p1;
	call _put_core_lock;
	rets = [sp++];
	r0 = 0;
	rts;
ENDPROC(___raw_write_trylock_asm)

/*
 * r0 = &rwlock->lock
 *
 * Clobbers: r3:0, p1:0
 */
ENTRY(___raw_write_unlock_asm)
	p1 = r0;
	r3.l = lo(RW_LOCK_BIAS);
	r3.h = hi(RW_LOCK_BIAS);
	[--sp] = rets;
	call _get_core_lock;
	r1 = [p1];
	r1 = r1 + r3;
	[p1] = r1;
	r1 = p1;
#ifdef __ARCH_SYNC_CORE_DCACHE
	call _end_lock_coherent
#else
	call _put_core_lock;
#endif
	rets = [sp++];
	rts;
ENDPROC(___raw_write_unlock_asm)

/*
 * r0 = ptr
 * r1 = value
 *
 * Add a signed value to a 32bit word and return the new value atomically.
 * Clobbers: r3:0, p1:0
 */
ENTRY(___raw_atomic_update_asm)
	p1 = r0;
	r3 = r1;
	[--sp] = rets;
	call _get_core_lock;
	r2 = [p1];
	r3 = r3 + r2;
	[p1] = r3;
	r1 = p1;
	call _put_core_lock;
	r0 = r3;
	rets = [sp++];
	rts;
ENDPROC(___raw_atomic_update_asm)

/*
 * r0 = ptr
 * r1 = mask
 *
 * Clear the mask bits from a 32bit word and return the old 32bit value
 * atomically.
 * Clobbers: r3:0, p1:0
 */
ENTRY(___raw_atomic_clear_asm)
	p1 = r0;
	r3 = ~r1;
	[--sp] = rets;
	call _get_core_lock;
	r2 = [p1];
	r3 = r2 & r3;
	[p1] = r3;
	r3 = r2;
	r1 = p1;
	call _put_core_lock;
	r0 = r3;
	rets = [sp++];
	rts;
ENDPROC(___raw_atomic_clear_asm)

/*
 * r0 = ptr
 * r1 = mask
 *
 * Set the mask bits into a 32bit word and return the old 32bit value
 * atomically.
 * Clobbers: r3:0, p1:0
 */
ENTRY(___raw_atomic_set_asm)
	p1 = r0;
	r3 = r1;
	[--sp] = rets;
	call _get_core_lock;
	r2 = [p1];
	r3 = r2 | r3;
	[p1] = r3;
	r3 = r2;
	r1 = p1;
	call _put_core_lock;
	r0 = r3;
	rets = [sp++];
	rts;
ENDPROC(___raw_atomic_set_asm)

/*
 * r0 = ptr
 * r1 = mask
 *
 * XOR the mask bits with a 32bit word and return the old 32bit value
 * atomically.
 * Clobbers: r3:0, p1:0
 */
ENTRY(___raw_atomic_xor_asm)
	p1 = r0;
	r3 = r1;
	[--sp] = rets;
	call _get_core_lock;
	r2 = [p1];
	r3 = r2 ^ r3;
	[p1] = r3;
	r3 = r2;
	r1 = p1;
	call _put_core_lock;
	r0 = r3;
	rets = [sp++];
	rts;
ENDPROC(___raw_atomic_xor_asm)

/*
 * r0 = ptr
 * r1 = mask
 *
 * Perform a logical AND between the mask bits and a 32bit word, and
 * return the masked value. We need this on this architecture in
 * order to invalidate the local cache before testing.
 *
 * Clobbers: r3:0, p1:0
 */
ENTRY(___raw_atomic_test_asm)
	p1 = r0;
	r3 = r1;
	r1 = -L1_CACHE_BYTES;
	r1 = r0 & r1;
	p0 = r1;
	flushinv[p0];
	SSYNC(r2);
	r0 = [p1];
	r0 = r0 & r3;
	rts;
ENDPROC(___raw_atomic_test_asm)

/*
 * r0 = ptr
 * r1 = value
 *
 * Swap *ptr with value and return the old 32bit value atomically.
 * Clobbers: r3:0, p1:0
 */
#define	__do_xchg(src, dst) 		\
	p1 = r0;			\
	r3 = r1;			\
	[--sp] = rets;			\
	call _get_core_lock;		\
	r2 = src;			\
	dst = r3;			\
	r3 = r2;			\
	r1 = p1;			\
	call _put_core_lock;		\
	r0 = r3;			\
	rets = [sp++];			\
	rts;

ENTRY(___raw_xchg_1_asm)
	__do_xchg(b[p1] (z), b[p1])
ENDPROC(___raw_xchg_1_asm)

ENTRY(___raw_xchg_2_asm)
	__do_xchg(w[p1] (z), w[p1])
ENDPROC(___raw_xchg_2_asm)

ENTRY(___raw_xchg_4_asm)
	__do_xchg([p1], [p1])
ENDPROC(___raw_xchg_4_asm)

/*
 * r0 = ptr
 * r1 = new
 * r2 = old
 *
 * Swap *ptr with new if *ptr == old and return the previous *ptr
 * value atomically.
 *
 * Clobbers: r3:0, p1:0
 */
#define	__do_cmpxchg(src, dst) 		\
	[--sp] = rets;			\
	[--sp] = r4;			\
	p1 = r0;			\
	r3 = r1;			\
	r4 = r2;			\
	call _get_core_lock;		\
	r2 = src;			\
	cc = r2 == r4;			\
	if !cc jump 1f;			\
	dst = r3;			\
     1: r3 = r2;			\
	r1 = p1;			\
	call _put_core_lock;		\
	r0 = r3;			\
	r4 = [sp++];			\
	rets = [sp++];			\
	rts;

ENTRY(___raw_cmpxchg_1_asm)
	__do_cmpxchg(b[p1] (z), b[p1])
ENDPROC(___raw_cmpxchg_1_asm)

ENTRY(___raw_cmpxchg_2_asm)
	__do_cmpxchg(w[p1] (z), w[p1])
ENDPROC(___raw_cmpxchg_2_asm)

ENTRY(___raw_cmpxchg_4_asm)
	__do_cmpxchg([p1], [p1])
ENDPROC(___raw_cmpxchg_4_asm)

/*
 * r0 = ptr
 * r1 = bitnr
 *
 * Set a bit in a 32bit word and return the old 32bit value atomically.
 * Clobbers: r3:0, p1:0
 */
ENTRY(___raw_bit_set_asm)
	r2 = r1;
	r1 = 1;
	r1 <<= r2;
	jump ___raw_atomic_set_asm
ENDPROC(___raw_bit_set_asm)

/*
 * r0 = ptr
 * r1 = bitnr
 *
 * Clear a bit in a 32bit word and return the old 32bit value atomically.
 * Clobbers: r3:0, p1:0
 */
ENTRY(___raw_bit_clear_asm)
	r2 = r1;
	r1 = 1;
	r1 <<= r2;
	jump ___raw_atomic_clear_asm
ENDPROC(___raw_bit_clear_asm)

/*
 * r0 = ptr
 * r1 = bitnr
 *
 * Toggle a bit in a 32bit word and return the old 32bit value atomically.
 * Clobbers: r3:0, p1:0
 */
ENTRY(___raw_bit_toggle_asm)
	r2 = r1;
	r1 = 1;
	r1 <<= r2;
	jump ___raw_atomic_xor_asm
ENDPROC(___raw_bit_toggle_asm)

/*
 * r0 = ptr
 * r1 = bitnr
 *
 * Test-and-set a bit in a 32bit word and return the old bit value atomically.
 * Clobbers: r3:0, p1:0
 */
ENTRY(___raw_bit_test_set_asm)
	[--sp] = rets;
	[--sp] = r1;
	call ___raw_bit_set_asm
	r1 = [sp++];
	r2 = 1;
	r2 <<= r1;
	r0 = r0 & r2;
	cc = r0 == 0;
	if cc jump 1f
	r0 = 1;
1:
	rets = [sp++];
	rts;
ENDPROC(___raw_bit_test_set_asm)

/*
 * r0 = ptr
 * r1 = bitnr
 *
 * Test-and-clear a bit in a 32bit word and return the old bit value atomically.
 * Clobbers: r3:0, p1:0
 */
ENTRY(___raw_bit_test_clear_asm)
	[--sp] = rets;
	[--sp] = r1;
	call ___raw_bit_clear_asm
	r1 = [sp++];
	r2 = 1;
	r2 <<= r1;
	r0 = r0 & r2;
	cc = r0 == 0;
	if cc jump 1f
	r0 = 1;
1:
	rets = [sp++];
	rts;
ENDPROC(___raw_bit_test_clear_asm)

/*
 * r0 = ptr
 * r1 = bitnr
 *
 * Test-and-toggle a bit in a 32bit word,
 * and return the old bit value atomically.
 * Clobbers: r3:0, p1:0
 */
ENTRY(___raw_bit_test_toggle_asm)
	[--sp] = rets;
	[--sp] = r1;
	call ___raw_bit_toggle_asm
	r1 = [sp++];
	r2 = 1;
	r2 <<= r1;
	r0 = r0 & r2;
	cc = r0 == 0;
	if cc jump 1f
	r0 = 1;
1:
	rets = [sp++];
	rts;
ENDPROC(___raw_bit_test_toggle_asm)

/*
 * r0 = ptr
 * r1 = bitnr
 *
 * Test a bit in a 32bit word and return its value.
 * We need this on this architecture in order to invalidate
 * the local cache before testing.
 *
 * Clobbers: r3:0, p1:0
 */
ENTRY(___raw_bit_test_asm)
	r2 = r1;
	r1 = 1;
	r1 <<= r2;
	jump ___raw_atomic_test_asm
ENDPROC(___raw_bit_test_asm)

/*
 * r0 = ptr
 *
 * Fetch and return an uncached 32bit value.
 *
 * Clobbers: r2:0, p1:0
 */
ENTRY(___raw_uncached_fetch_asm)
	p1 = r0;
	r1 = -L1_CACHE_BYTES;
	r1 = r0 & r1;
	p0 = r1;
	flushinv[p0];
	SSYNC(r2);
	r0 = [p1];
	rts;
ENDPROC(___raw_uncached_fetch_asm)
