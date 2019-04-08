/*
 * Copyright (C) 2014-15 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/cache.h>

/*
 * The memset implementation below is optimized to use prefetchw and prealloc
 * instruction in case of CPU with 64B L1 data cache line (L1_CACHE_SHIFT == 6)
 * If you want to implement optimized memset for other possible L1 data cache
 * line lengths (32B and 128B) you should rewrite code carefully checking
 * we don't call any prefetchw/prealloc instruction for L1 cache lines which
 * don't belongs to memset area.
 */

#if L1_CACHE_SHIFT == 6

.macro PREALLOC_INSTR	reg, off
	prealloc	[\reg, \off]
.endm

.macro PREFETCHW_INSTR	reg, off
	prefetchw	[\reg, \off]
.endm

#else

.macro PREALLOC_INSTR	reg, off
.endm

.macro PREFETCHW_INSTR	reg, off
.endm

#endif

ENTRY_CFI(memset)
	PREFETCHW_INSTR	r0, 0	; Prefetch the first write location
	mov.f	0, r2
;;; if size is zero
	jz.d	[blink]
	mov	r3, r0		; don't clobber ret val

;;; if length < 8
	brls.d.nt	r2, 8, .Lsmallchunk
	mov.f	lp_count,r2

	and.f	r4, r0, 0x03
	rsub	lp_count, r4, 4
	lpnz	@.Laligndestination
	;; LOOP BEGIN
	stb.ab	r1, [r3,1]
	sub	r2, r2, 1
.Laligndestination:

;;; Destination is aligned
	and	r1, r1, 0xFF
	asl	r4, r1, 8
	or	r4, r4, r1
	asl	r5, r4, 16
	or	r5, r5, r4
	mov	r4, r5

	sub3	lp_count, r2, 8
	cmp     r2, 64
	bmsk.hi	r2, r2, 5
	mov.ls	lp_count, 0
	add3.hi	r2, r2, 8

;;; Convert len to Dwords, unfold x8
	lsr.f	lp_count, lp_count, 6

	lpnz	@.Lset64bytes
	;; LOOP START
	PREALLOC_INSTR	r3, 64	; alloc next line w/o fetching

#ifdef CONFIG_ARC_HAS_LL64
	std.ab	r4, [r3, 8]
	std.ab	r4, [r3, 8]
	std.ab	r4, [r3, 8]
	std.ab	r4, [r3, 8]
	std.ab	r4, [r3, 8]
	std.ab	r4, [r3, 8]
	std.ab	r4, [r3, 8]
	std.ab	r4, [r3, 8]
#else
	st.ab	r4, [r3, 4]
	st.ab	r4, [r3, 4]
	st.ab	r4, [r3, 4]
	st.ab	r4, [r3, 4]
	st.ab	r4, [r3, 4]
	st.ab	r4, [r3, 4]
	st.ab	r4, [r3, 4]
	st.ab	r4, [r3, 4]
	st.ab	r4, [r3, 4]
	st.ab	r4, [r3, 4]
	st.ab	r4, [r3, 4]
	st.ab	r4, [r3, 4]
	st.ab	r4, [r3, 4]
	st.ab	r4, [r3, 4]
	st.ab	r4, [r3, 4]
	st.ab	r4, [r3, 4]
#endif
.Lset64bytes:

	lsr.f	lp_count, r2, 5 ;Last remaining  max 124 bytes
	lpnz	.Lset32bytes
	;; LOOP START
#ifdef CONFIG_ARC_HAS_LL64
	std.ab	r4, [r3, 8]
	std.ab	r4, [r3, 8]
	std.ab	r4, [r3, 8]
	std.ab	r4, [r3, 8]
#else
	st.ab	r4, [r3, 4]
	st.ab	r4, [r3, 4]
	st.ab	r4, [r3, 4]
	st.ab	r4, [r3, 4]
	st.ab	r4, [r3, 4]
	st.ab	r4, [r3, 4]
	st.ab	r4, [r3, 4]
	st.ab	r4, [r3, 4]
#endif
.Lset32bytes:

	and.f	lp_count, r2, 0x1F ;Last remaining 31 bytes
.Lsmallchunk:
	lpnz	.Lcopy3bytes
	;; LOOP START
	stb.ab	r1, [r3, 1]
.Lcopy3bytes:

	j	[blink]

END_CFI(memset)

ENTRY_CFI(memzero)
    ; adjust bzero args to memset args
    mov r2, r1
    b.d  memset    ;tail call so need to tinker with blink
    mov r1, 0
END_CFI(memzero)
