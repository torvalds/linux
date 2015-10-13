/*
 * Copyright (C) 2014-15 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>

#ifdef __LITTLE_ENDIAN__
# define SHIFT_1(RX,RY,IMM)	asl	RX, RY, IMM	; <<
# define SHIFT_2(RX,RY,IMM)	lsr	RX, RY, IMM	; >>
# define MERGE_1(RX,RY,IMM)	asl	RX, RY, IMM
# define MERGE_2(RX,RY,IMM)
# define EXTRACT_1(RX,RY,IMM)	and	RX, RY, 0xFFFF
# define EXTRACT_2(RX,RY,IMM)	lsr	RX, RY, IMM
#else
# define SHIFT_1(RX,RY,IMM)	lsr	RX, RY, IMM	; >>
# define SHIFT_2(RX,RY,IMM)	asl	RX, RY, IMM	; <<
# define MERGE_1(RX,RY,IMM)	asl	RX, RY, IMM	; <<
# define MERGE_2(RX,RY,IMM)	asl	RX, RY, IMM	; <<
# define EXTRACT_1(RX,RY,IMM)	lsr	RX, RY, IMM
# define EXTRACT_2(RX,RY,IMM)	lsr	RX, RY, 0x08
#endif

#ifdef CONFIG_ARC_HAS_LL64
# define PREFETCH_READ(RX)	prefetch    [RX, 56]
# define PREFETCH_WRITE(RX)	prefetchw   [RX, 64]
# define LOADX(DST,RX)		ldd.ab	DST, [RX, 8]
# define STOREX(SRC,RX)		std.ab	SRC, [RX, 8]
# define ZOLSHFT		5
# define ZOLAND			0x1F
#else
# define PREFETCH_READ(RX)	prefetch    [RX, 28]
# define PREFETCH_WRITE(RX)	prefetchw   [RX, 32]
# define LOADX(DST,RX)		ld.ab	DST, [RX, 4]
# define STOREX(SRC,RX)		st.ab	SRC, [RX, 4]
# define ZOLSHFT		4
# define ZOLAND			0xF
#endif

ENTRY(memcpy)
	prefetch [r1]		; Prefetch the read location
	prefetchw [r0]		; Prefetch the write location
	mov.f	0, r2
;;; if size is zero
	jz.d	[blink]
	mov	r3, r0		; don;t clobber ret val

;;; if size <= 8
	cmp	r2, 8
	bls.d	@smallchunk
	mov.f	lp_count, r2

	and.f	r4, r0, 0x03
	rsub	lp_count, r4, 4
	lpnz	@aligndestination
	;; LOOP BEGIN
	ldb.ab	r5, [r1,1]
	sub	r2, r2, 1
	stb.ab	r5, [r3,1]
aligndestination:

;;; Check the alignment of the source
	and.f	r4, r1, 0x03
	bnz.d	@sourceunaligned

;;; CASE 0: Both source and destination are 32bit aligned
;;; Convert len to Dwords, unfold x4
	lsr.f	lp_count, r2, ZOLSHFT
	lpnz	@copy32_64bytes
	;; LOOP START
	LOADX (r6, r1)
	PREFETCH_READ (r1)
	PREFETCH_WRITE (r3)
	LOADX (r8, r1)
	LOADX (r10, r1)
	LOADX (r4, r1)
	STOREX (r6, r3)
	STOREX (r8, r3)
	STOREX (r10, r3)
	STOREX (r4, r3)
copy32_64bytes:

	and.f	lp_count, r2, ZOLAND ;Last remaining 31 bytes
smallchunk:
	lpnz	@copyremainingbytes
	;; LOOP START
	ldb.ab	r5, [r1,1]
	stb.ab	r5, [r3,1]
copyremainingbytes:

	j	[blink]
;;; END CASE 0

sourceunaligned:
	cmp	r4, 2
	beq.d	@unalignedOffby2
	sub	r2, r2, 1

	bhi.d	@unalignedOffby3
	ldb.ab	r5, [r1, 1]

;;; CASE 1: The source is unaligned, off by 1
	;; Hence I need to read 1 byte for a 16bit alignment
	;; and 2bytes to reach 32bit alignment
	ldh.ab	r6, [r1, 2]
	sub	r2, r2, 2
	;; Convert to words, unfold x2
	lsr.f	lp_count, r2, 3
	MERGE_1 (r6, r6, 8)
	MERGE_2 (r5, r5, 24)
	or	r5, r5, r6

	;; Both src and dst are aligned
	lpnz	@copy8bytes_1
	;; LOOP START
	ld.ab	r6, [r1, 4]
	prefetch [r1, 28]	;Prefetch the next read location
	ld.ab	r8, [r1,4]
	prefetchw [r3, 32]	;Prefetch the next write location

	SHIFT_1	(r7, r6, 24)
	or	r7, r7, r5
	SHIFT_2	(r5, r6, 8)

	SHIFT_1	(r9, r8, 24)
	or	r9, r9, r5
	SHIFT_2	(r5, r8, 8)

	st.ab	r7, [r3, 4]
	st.ab	r9, [r3, 4]
copy8bytes_1:

	;; Write back the remaining 16bits
	EXTRACT_1 (r6, r5, 16)
	sth.ab	r6, [r3, 2]
	;; Write back the remaining 8bits
	EXTRACT_2 (r5, r5, 16)
	stb.ab	r5, [r3, 1]

	and.f	lp_count, r2, 0x07 ;Last 8bytes
	lpnz	@copybytewise_1
	;; LOOP START
	ldb.ab	r6, [r1,1]
	stb.ab	r6, [r3,1]
copybytewise_1:
	j	[blink]

unalignedOffby2:
;;; CASE 2: The source is unaligned, off by 2
	ldh.ab	r5, [r1, 2]
	sub	r2, r2, 1

	;; Both src and dst are aligned
	;; Convert to words, unfold x2
	lsr.f	lp_count, r2, 3
#ifdef __BIG_ENDIAN__
	asl.nz	r5, r5, 16
#endif
	lpnz	@copy8bytes_2
	;; LOOP START
	ld.ab	r6, [r1, 4]
	prefetch [r1, 28]	;Prefetch the next read location
	ld.ab	r8, [r1,4]
	prefetchw [r3, 32]	;Prefetch the next write location

	SHIFT_1	(r7, r6, 16)
	or	r7, r7, r5
	SHIFT_2	(r5, r6, 16)

	SHIFT_1	(r9, r8, 16)
	or	r9, r9, r5
	SHIFT_2	(r5, r8, 16)

	st.ab	r7, [r3, 4]
	st.ab	r9, [r3, 4]
copy8bytes_2:

#ifdef __BIG_ENDIAN__
	lsr.nz	r5, r5, 16
#endif
	sth.ab	r5, [r3, 2]

	and.f	lp_count, r2, 0x07 ;Last 8bytes
	lpnz	@copybytewise_2
	;; LOOP START
	ldb.ab	r6, [r1,1]
	stb.ab	r6, [r3,1]
copybytewise_2:
	j	[blink]

unalignedOffby3:
;;; CASE 3: The source is unaligned, off by 3
;;; Hence, I need to read 1byte for achieve the 32bit alignment

	;; Both src and dst are aligned
	;; Convert to words, unfold x2
	lsr.f	lp_count, r2, 3
#ifdef __BIG_ENDIAN__
	asl.ne	r5, r5, 24
#endif
	lpnz	@copy8bytes_3
	;; LOOP START
	ld.ab	r6, [r1, 4]
	prefetch [r1, 28]	;Prefetch the next read location
	ld.ab	r8, [r1,4]
	prefetchw [r3, 32]	;Prefetch the next write location

	SHIFT_1	(r7, r6, 8)
	or	r7, r7, r5
	SHIFT_2	(r5, r6, 24)

	SHIFT_1	(r9, r8, 8)
	or	r9, r9, r5
	SHIFT_2	(r5, r8, 24)

	st.ab	r7, [r3, 4]
	st.ab	r9, [r3, 4]
copy8bytes_3:

#ifdef __BIG_ENDIAN__
	lsr.nz	r5, r5, 24
#endif
	stb.ab	r5, [r3, 1]

	and.f	lp_count, r2, 0x07 ;Last 8bytes
	lpnz	@copybytewise_3
	;; LOOP START
	ldb.ab	r6, [r1,1]
	stb.ab	r6, [r3,1]
copybytewise_3:
	j	[blink]

END(memcpy)
