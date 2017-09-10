/*
 * M7memcpy: Optimized SPARC M7 memcpy
 *
 * Copyright (c) 2016, Oracle and/or its affiliates. All rights reserved.
 */

	.file	"M7memcpy.S"

/*
 * memcpy(s1, s2, len)
 *
 * Copy s2 to s1, always copy n bytes.
 * Note: this C code does not work for overlapped copies.
 *
 * Fast assembler language version of the following C-program for memcpy
 * which represents the `standard' for the C-library.
 *
 *	void *
 *	memcpy(void *s, const void *s0, size_t n)
 *	{
 *		if (n != 0) {
 *		    char *s1 = s;
 *		    const char *s2 = s0;
 *		    do {
 *			*s1++ = *s2++;
 *		    } while (--n != 0);
 *		}
 *		return (s);
 *	}
 *
 *
 * SPARC T7/M7 Flow :
 *
 * if (count < SMALL_MAX) {
 *   if count < SHORTCOPY              (SHORTCOPY=3)
 *	copy bytes; exit with dst addr
 *   if src & dst aligned on word boundary but not long word boundary,
 *     copy with ldw/stw; branch to finish_up
 *   if src & dst aligned on long word boundary
 *     copy with ldx/stx; branch to finish_up
 *   if src & dst not aligned and length <= SHORTCHECK   (SHORTCHECK=14)
 *     copy bytes; exit with dst addr
 *   move enough bytes to get src to word boundary
 *   if dst now on word boundary
 * move_words:
 *     copy words; branch to finish_up
 *   if dst now on half word boundary
 *     load words, shift half words, store words; branch to finish_up
 *   if dst on byte 1
 *     load words, shift 3 bytes, store words; branch to finish_up
 *   if dst on byte 3
 *     load words, shift 1 byte, store words; branch to finish_up
 * finish_up:
 *     copy bytes; exit with dst addr
 * } else {                                         More than SMALL_MAX bytes
 *   move bytes until dst is on long word boundary
 *   if( src is on long word boundary ) {
 *     if (count < MED_MAX) {
 * finish_long:					   src/dst aligned on 8 bytes
 *       copy with ldx/stx in 8-way unrolled loop;
 *       copy final 0-63 bytes; exit with dst addr
 *     } else {				     src/dst aligned; count > MED_MAX
 *       align dst on 64 byte boundary; for main data movement:
 *       prefetch src data to L2 cache; let HW prefetch move data to L1 cache
 *       Use BIS (block initializing store) to avoid copying store cache
 *       lines from memory. But pre-store first element of each cache line
 *       ST_CHUNK lines in advance of the rest of that cache line. That
 *       gives time for replacement cache lines to be written back without
 *       excess STQ and Miss Buffer filling. Repeat until near the end,
 *       then finish up storing before going to finish_long.
 *     }
 *   } else {                                   src/dst not aligned on 8 bytes
 *     if src is word aligned and count < MED_WMAX
 *       move words in 8-way unrolled loop
 *       move final 0-31 bytes; exit with dst addr
 *     if count < MED_UMAX
 *       use alignaddr/faligndata combined with ldd/std in 8-way
 *       unrolled loop to move data.
 *       go to unalign_done
 *     else
 *       setup alignaddr for faligndata instructions
 *       align dst on 64 byte boundary; prefetch src data to L1 cache
 *       loadx8, falign, block-store, prefetch loop
 *	 (only use block-init-store when src/dst on 8 byte boundaries.)
 * unalign_done:
 *       move remaining bytes for unaligned cases. exit with dst addr.
 * }
 *
 */

#include <asm/visasm.h>
#include <asm/asi.h>

#if !defined(EX_LD) && !defined(EX_ST)
#define NON_USER_COPY
#endif

#ifndef EX_LD
#define EX_LD(x,y)	x
#endif
#ifndef EX_LD_FP
#define EX_LD_FP(x,y)	x
#endif

#ifndef EX_ST
#define EX_ST(x,y)	x
#endif
#ifndef EX_ST_FP
#define EX_ST_FP(x,y)	x
#endif

#ifndef EX_RETVAL
#define EX_RETVAL(x)    x
#endif

#ifndef LOAD
#define LOAD(type,addr,dest)	type [addr], dest
#endif

#ifndef STORE
#define STORE(type,src,addr)	type src, [addr]
#endif

/*
 * ASI_BLK_INIT_QUAD_LDD_P/ASI_BLK_INIT_QUAD_LDD_S marks the cache
 * line as "least recently used" which means if many threads are
 * active, it has a high probability of being pushed out of the cache
 * between the first initializing store and the final stores.
 * Thus, we use ASI_ST_BLKINIT_MRU_P/ASI_ST_BLKINIT_MRU_S which
 * marks the cache line as "most recently used" for all
 * but the last cache line
 */
#ifndef STORE_ASI
#ifndef SIMULATE_NIAGARA_ON_NON_NIAGARA
#define STORE_ASI	ASI_BLK_INIT_QUAD_LDD_P
#else
#define STORE_ASI	0x80		/* ASI_P */
#endif
#endif

#ifndef STORE_MRU_ASI
#ifndef SIMULATE_NIAGARA_ON_NON_NIAGARA
#define STORE_MRU_ASI	ASI_ST_BLKINIT_MRU_P
#else
#define STORE_MRU_ASI	0x80		/* ASI_P */
#endif
#endif

#ifndef STORE_INIT
#define STORE_INIT(src,addr)	stxa src, [addr] STORE_ASI
#endif

#ifndef STORE_INIT_MRU
#define STORE_INIT_MRU(src,addr)	stxa src, [addr] STORE_MRU_ASI
#endif

#ifndef FUNC_NAME
#define FUNC_NAME	M7memcpy
#endif

#ifndef PREAMBLE
#define PREAMBLE
#endif

#define	BLOCK_SIZE	64
#define	SHORTCOPY	3
#define	SHORTCHECK	14
#define	SHORT_LONG	64	/* max copy for short longword-aligned case */
				/* must be at least 64 */
#define	SMALL_MAX	128
#define	MED_UMAX	1024	/* max copy for medium un-aligned case */
#define	MED_WMAX	1024	/* max copy for medium word-aligned case */
#define	MED_MAX		1024	/* max copy for medium longword-aligned case */
#define ST_CHUNK	24	/* ST_CHUNK - block of values for BIS Store */
#define ALIGN_PRE	24	/* distance for aligned prefetch loop */

	.register	%g2,#scratch

	.section	".text"
	.global		FUNC_NAME
	.type		FUNC_NAME, #function
	.align		16
FUNC_NAME:
	srlx            %o2, 31, %g2
	cmp             %g2, 0
	tne             %xcc, 5
	PREAMBLE
	mov		%o0, %g1	! save %o0
	brz,pn          %o2, .Lsmallx
	 cmp            %o2, 3
	ble,pn          %icc, .Ltiny_cp
	 cmp            %o2, 19
	ble,pn          %icc, .Lsmall_cp
	 or             %o0, %o1, %g2
	cmp             %o2, SMALL_MAX
	bl,pn           %icc, .Lmedium_cp
	 nop

.Lmedium:
	neg	%o0, %o5
	andcc	%o5, 7, %o5		! bytes till DST 8 byte aligned
	brz,pt	%o5, .Ldst_aligned_on_8

	! %o5 has the bytes to be written in partial store.
	 sub	%o2, %o5, %o2
	sub	%o1, %o0, %o1		! %o1 gets the difference
7:					! dst aligning loop
	add	%o1, %o0, %o4
	EX_LD(LOAD(ldub, %o4, %o4), memcpy_retl_o2_plus_o5)	! load one byte
	subcc	%o5, 1, %o5
	EX_ST(STORE(stb, %o4, %o0), memcpy_retl_o2_plus_o5_plus_1)
	bgu,pt	%xcc, 7b
	 add	%o0, 1, %o0		! advance dst
	add	%o1, %o0, %o1		! restore %o1
.Ldst_aligned_on_8:
	andcc	%o1, 7, %o5
	brnz,pt	%o5, .Lsrc_dst_unaligned_on_8
	 nop

.Lsrc_dst_aligned_on_8:
	! check if we are copying MED_MAX or more bytes
	set MED_MAX, %o3
	cmp %o2, %o3 			! limit to store buffer size
	bgu,pn	%xcc, .Llarge_align8_copy
	 nop

/*
 * Special case for handling when src and dest are both long word aligned
 * and total data to move is less than MED_MAX bytes
 */
.Lmedlong:
	subcc	%o2, 63, %o2		! adjust length to allow cc test
	ble,pn	%xcc, .Lmedl63		! skip big loop if less than 64 bytes
	 nop
.Lmedl64:
	EX_LD(LOAD(ldx, %o1, %o4), memcpy_retl_o2_plus_63)	! load
	subcc	%o2, 64, %o2		! decrement length count
	EX_ST(STORE(stx, %o4, %o0), memcpy_retl_o2_plus_63_64)	! and store
	EX_LD(LOAD(ldx, %o1+8, %o3), memcpy_retl_o2_plus_63_56)	! a block of 64
	EX_ST(STORE(stx, %o3, %o0+8), memcpy_retl_o2_plus_63_56)
	EX_LD(LOAD(ldx, %o1+16, %o4), memcpy_retl_o2_plus_63_48)
	EX_ST(STORE(stx, %o4, %o0+16), memcpy_retl_o2_plus_63_48)
	EX_LD(LOAD(ldx, %o1+24, %o3), memcpy_retl_o2_plus_63_40)
	EX_ST(STORE(stx, %o3, %o0+24), memcpy_retl_o2_plus_63_40)
	EX_LD(LOAD(ldx, %o1+32, %o4), memcpy_retl_o2_plus_63_32)! load and store
	EX_ST(STORE(stx, %o4, %o0+32), memcpy_retl_o2_plus_63_32)
	EX_LD(LOAD(ldx, %o1+40, %o3), memcpy_retl_o2_plus_63_24)! a block of 64
	add	%o1, 64, %o1		! increase src ptr by 64
	EX_ST(STORE(stx, %o3, %o0+40), memcpy_retl_o2_plus_63_24)
	EX_LD(LOAD(ldx, %o1-16, %o4), memcpy_retl_o2_plus_63_16)
	add	%o0, 64, %o0		! increase dst ptr by 64
	EX_ST(STORE(stx, %o4, %o0-16), memcpy_retl_o2_plus_63_16)
	EX_LD(LOAD(ldx, %o1-8, %o3), memcpy_retl_o2_plus_63_8)
	bgu,pt	%xcc, .Lmedl64		! repeat if at least 64 bytes left
	 EX_ST(STORE(stx, %o3, %o0-8), memcpy_retl_o2_plus_63_8)
.Lmedl63:
	addcc	%o2, 32, %o2		! adjust remaining count
	ble,pt	%xcc, .Lmedl31		! to skip if 31 or fewer bytes left
	 nop
	EX_LD(LOAD(ldx, %o1, %o4), memcpy_retl_o2_plus_31)	! load
	sub	%o2, 32, %o2		! decrement length count
	EX_ST(STORE(stx, %o4, %o0), memcpy_retl_o2_plus_31_32)	! and store
	EX_LD(LOAD(ldx, %o1+8, %o3), memcpy_retl_o2_plus_31_24)	! a block of 32
	add	%o1, 32, %o1		! increase src ptr by 32
	EX_ST(STORE(stx, %o3, %o0+8), memcpy_retl_o2_plus_31_24)
	EX_LD(LOAD(ldx, %o1-16, %o4), memcpy_retl_o2_plus_31_16)
	add	%o0, 32, %o0		! increase dst ptr by 32
	EX_ST(STORE(stx, %o4, %o0-16), memcpy_retl_o2_plus_31_16)
	EX_LD(LOAD(ldx, %o1-8, %o3), memcpy_retl_o2_plus_31_8)
	EX_ST(STORE(stx, %o3, %o0-8), memcpy_retl_o2_plus_31_8)
.Lmedl31:
	addcc	%o2, 16, %o2		! adjust remaining count
	ble,pt	%xcc, .Lmedl15		! skip if 15 or fewer bytes left
	 nop				!
	EX_LD(LOAD(ldx, %o1, %o4), memcpy_retl_o2_plus_15)
	add	%o1, 16, %o1		! increase src ptr by 16
	EX_ST(STORE(stx, %o4, %o0), memcpy_retl_o2_plus_15)
	sub	%o2, 16, %o2		! decrease count by 16
	EX_LD(LOAD(ldx, %o1-8, %o3), memcpy_retl_o2_plus_15_8)
	add	%o0, 16, %o0		! increase dst ptr by 16
	EX_ST(STORE(stx, %o3, %o0-8), memcpy_retl_o2_plus_15_8)
.Lmedl15:
	addcc	%o2, 15, %o2		! restore count
	bz,pt	%xcc, .Lsmallx	! exit if finished
	 cmp	%o2, 8
	blt,pt	%xcc, .Lmedw7		! skip if 7 or fewer bytes left
	 tst	%o2
	EX_LD(LOAD(ldx, %o1, %o4), memcpy_retl_o2)	! load 8 bytes
	add	%o1, 8, %o1		! increase src ptr by 8
	add	%o0, 8, %o0		! increase dst ptr by 8
	subcc	%o2, 8, %o2		! decrease count by 8
	bnz,pn	%xcc, .Lmedw7
	 EX_ST(STORE(stx, %o4, %o0-8), memcpy_retl_o2_plus_8)	! and store 8
	retl
	 mov	EX_RETVAL(%g1), %o0	! restore %o0

	.align 16
.Lsrc_dst_unaligned_on_8:
	! DST is 8-byte aligned, src is not
2:
	andcc	%o1, 0x3, %o5		! test word alignment
	bnz,pt	%xcc, .Lunalignsetup	! branch to skip if not word aligned
	 nop

/*
 * Handle all cases where src and dest are aligned on word
 * boundaries. Use unrolled loops for better performance.
 * This option wins over standard large data move when
 * source and destination is in cache for.Lmedium
 * to short data moves.
 */
	set MED_WMAX, %o3
	cmp %o2, %o3 			! limit to store buffer size
	bge,pt	%xcc, .Lunalignrejoin	! otherwise rejoin main loop
	 nop

	subcc	%o2, 31, %o2		! adjust length to allow cc test
					! for end of loop
	ble,pt	%xcc, .Lmedw31		! skip big loop if less than 16
.Lmedw32:
	EX_LD(LOAD(ld, %o1, %o4), memcpy_retl_o2_plus_31)! move a block of 32
	sllx	%o4, 32, %o5
	EX_LD(LOAD(ld, %o1+4, %o4), memcpy_retl_o2_plus_31)
	or	%o4, %o5, %o5
	EX_ST(STORE(stx, %o5, %o0), memcpy_retl_o2_plus_31)
	subcc	%o2, 32, %o2		! decrement length count
	EX_LD(LOAD(ld, %o1+8, %o4), memcpy_retl_o2_plus_31_24)
	sllx	%o4, 32, %o5
	EX_LD(LOAD(ld, %o1+12, %o4), memcpy_retl_o2_plus_31_24)
	or	%o4, %o5, %o5
	EX_ST(STORE(stx, %o5, %o0+8), memcpy_retl_o2_plus_31_24)
	add	%o1, 32, %o1		! increase src ptr by 32
	EX_LD(LOAD(ld, %o1-16, %o4), memcpy_retl_o2_plus_31_16)
	sllx	%o4, 32, %o5
	EX_LD(LOAD(ld, %o1-12, %o4), memcpy_retl_o2_plus_31_16)
	or	%o4, %o5, %o5
	EX_ST(STORE(stx, %o5, %o0+16), memcpy_retl_o2_plus_31_16)
	add	%o0, 32, %o0		! increase dst ptr by 32
	EX_LD(LOAD(ld, %o1-8, %o4), memcpy_retl_o2_plus_31_8)
	sllx	%o4, 32, %o5
	EX_LD(LOAD(ld, %o1-4, %o4), memcpy_retl_o2_plus_31_8)
	or	%o4, %o5, %o5
	bgu,pt	%xcc, .Lmedw32		! repeat if at least 32 bytes left
	 EX_ST(STORE(stx, %o5, %o0-8), memcpy_retl_o2_plus_31_8)
.Lmedw31:
	addcc	%o2, 31, %o2		! restore count

	bz,pt	%xcc, .Lsmallx	! exit if finished
	 nop
	cmp	%o2, 16
	blt,pt	%xcc, .Lmedw15
	 nop
	EX_LD(LOAD(ld, %o1, %o4), memcpy_retl_o2)! move a block of 16 bytes
	sllx	%o4, 32, %o5
	subcc	%o2, 16, %o2		! decrement length count
	EX_LD(LOAD(ld, %o1+4, %o4), memcpy_retl_o2_plus_16)
	or	%o4, %o5, %o5
	EX_ST(STORE(stx, %o5, %o0), memcpy_retl_o2_plus_16)
	add	%o1, 16, %o1		! increase src ptr by 16
	EX_LD(LOAD(ld, %o1-8, %o4), memcpy_retl_o2_plus_8)
	add	%o0, 16, %o0		! increase dst ptr by 16
	sllx	%o4, 32, %o5
	EX_LD(LOAD(ld, %o1-4, %o4), memcpy_retl_o2_plus_8)
	or	%o4, %o5, %o5
	EX_ST(STORE(stx, %o5, %o0-8), memcpy_retl_o2_plus_8)
.Lmedw15:
	bz,pt	%xcc, .Lsmallx	! exit if finished
	 cmp	%o2, 8
	blt,pn	%xcc, .Lmedw7		! skip if 7 or fewer bytes left
	 tst	%o2
	EX_LD(LOAD(ld, %o1, %o4), memcpy_retl_o2)	! load 4 bytes
	subcc	%o2, 8, %o2		! decrease count by 8
	EX_ST(STORE(stw, %o4, %o0), memcpy_retl_o2_plus_8)! and store 4 bytes
	add	%o1, 8, %o1		! increase src ptr by 8
	EX_LD(LOAD(ld, %o1-4, %o3), memcpy_retl_o2_plus_4)	! load 4 bytes
	add	%o0, 8, %o0		! increase dst ptr by 8
	EX_ST(STORE(stw, %o3, %o0-4), memcpy_retl_o2_plus_4)! and store 4 bytes
	bz,pt	%xcc, .Lsmallx	! exit if finished
.Lmedw7:				! count is ge 1, less than 8
	cmp	%o2, 4			! check for 4 bytes left
	blt,pn	%xcc, .Lsmallleft3	! skip if 3 or fewer bytes left
	 nop				!
	EX_LD(LOAD(ld, %o1, %o4), memcpy_retl_o2)	! load 4 bytes
	add	%o1, 4, %o1		! increase src ptr by 4
	add	%o0, 4, %o0		! increase dst ptr by 4
	subcc	%o2, 4, %o2		! decrease count by 4
	bnz	.Lsmallleft3
	 EX_ST(STORE(stw, %o4, %o0-4), memcpy_retl_o2_plus_4)! and store 4 bytes
	retl
	 mov	EX_RETVAL(%g1), %o0

	.align 16
.Llarge_align8_copy:			! Src and dst share 8 byte alignment
	! align dst to 64 byte boundary
	andcc	%o0, 0x3f, %o3		! %o3 == 0 means dst is 64 byte aligned
	brz,pn	%o3, .Laligned_to_64
	 andcc	%o0, 8, %o3		! odd long words to move?
	brz,pt	%o3, .Laligned_to_16
	 nop
	EX_LD(LOAD(ldx, %o1, %o4), memcpy_retl_o2)
	sub	%o2, 8, %o2
	add	%o1, 8, %o1		! increment src ptr
	add	%o0, 8, %o0		! increment dst ptr
	EX_ST(STORE(stx, %o4, %o0-8), memcpy_retl_o2_plus_8)
.Laligned_to_16:
	andcc	%o0, 16, %o3		! pair of long words to move?
	brz,pt	%o3, .Laligned_to_32
	 nop
	EX_LD(LOAD(ldx, %o1, %o4), memcpy_retl_o2)
	sub	%o2, 16, %o2
	EX_ST(STORE(stx, %o4, %o0), memcpy_retl_o2_plus_16)
	add	%o1, 16, %o1		! increment src ptr
	EX_LD(LOAD(ldx, %o1-8, %o4), memcpy_retl_o2_plus_8)
	add	%o0, 16, %o0		! increment dst ptr
	EX_ST(STORE(stx, %o4, %o0-8), memcpy_retl_o2_plus_8)
.Laligned_to_32:
	andcc	%o0, 32, %o3		! four long words to move?
	brz,pt	%o3, .Laligned_to_64
	 nop
	EX_LD(LOAD(ldx, %o1, %o4), memcpy_retl_o2)
	sub	%o2, 32, %o2
	EX_ST(STORE(stx, %o4, %o0), memcpy_retl_o2_plus_32)
	EX_LD(LOAD(ldx, %o1+8, %o4), memcpy_retl_o2_plus_24)
	EX_ST(STORE(stx, %o4, %o0+8), memcpy_retl_o2_plus_24)
	EX_LD(LOAD(ldx, %o1+16, %o4), memcpy_retl_o2_plus_16)
	EX_ST(STORE(stx, %o4, %o0+16), memcpy_retl_o2_plus_16)
	add	%o1, 32, %o1		! increment src ptr
	EX_LD(LOAD(ldx, %o1-8, %o4), memcpy_retl_o2_plus_8)
	add	%o0, 32, %o0		! increment dst ptr
	EX_ST(STORE(stx, %o4, %o0-8), memcpy_retl_o2_plus_8)
.Laligned_to_64:
!
!	Using block init store (BIS) instructions to avoid fetching cache
!	lines from memory. Use ST_CHUNK stores to first element of each cache
!	line (similar to prefetching) to avoid overfilling STQ or miss buffers.
!	Gives existing cache lines time to be moved out of L1/L2/L3 cache.
!	Initial stores using MRU version of BIS to keep cache line in
!	cache until we are ready to store final element of cache line.
!	Then store last element using the LRU version of BIS.
!
	andn	%o2, 0x3f, %o5		! %o5 is multiple of block size
	and	%o2, 0x3f, %o2		! residue bytes in %o2
!
!	We use STORE_MRU_ASI for the first seven stores to each cache line
!	followed by STORE_ASI (mark as LRU) for the last store. That
!	mixed approach reduces the probability that the cache line is removed
!	before we finish setting it, while minimizing the effects on
!	other cached values during a large memcpy
!
!	ST_CHUNK batches up initial BIS operations for several cache lines
!	to allow multiple requests to not be blocked by overflowing the
!	the store miss buffer. Then the matching stores for all those
!	BIS operations are executed.
!

	sub	%o0, 8, %o0		! adjust %o0 for ASI alignment
.Lalign_loop:
	cmp	%o5, ST_CHUNK*64
	blu,pt	%xcc, .Lalign_loop_fin
	 mov	ST_CHUNK,%o3
.Lalign_loop_start:
	prefetch [%o1 + (ALIGN_PRE * BLOCK_SIZE)], 21
	subcc	%o3, 1, %o3
	EX_LD(LOAD(ldx, %o1, %o4), memcpy_retl_o2_plus_o5)
	add	%o1, 64, %o1
	add	%o0, 8, %o0
	EX_ST(STORE_INIT_MRU(%o4, %o0), memcpy_retl_o2_plus_o5)
	bgu	%xcc,.Lalign_loop_start
	 add	%o0, 56, %o0

	mov	ST_CHUNK,%o3
	sllx	%o3, 6, %o4		! ST_CHUNK*64
	sub	%o1, %o4, %o1		! reset %o1
	sub	%o0, %o4, %o0		! reset %o0

.Lalign_loop_rest:
	EX_LD(LOAD(ldx, %o1+8, %o4), memcpy_retl_o2_plus_o5)
	add	%o0, 16, %o0
	EX_ST(STORE_INIT_MRU(%o4, %o0), memcpy_retl_o2_plus_o5)
	EX_LD(LOAD(ldx, %o1+16, %o4), memcpy_retl_o2_plus_o5)
	add	%o0, 8, %o0
	EX_ST(STORE_INIT_MRU(%o4, %o0), memcpy_retl_o2_plus_o5)
	subcc	%o3, 1, %o3
	EX_LD(LOAD(ldx, %o1+24, %o4), memcpy_retl_o2_plus_o5)
	add	%o0, 8, %o0
	EX_ST(STORE_INIT_MRU(%o4, %o0), memcpy_retl_o2_plus_o5)
	EX_LD(LOAD(ldx, %o1+32, %o4), memcpy_retl_o2_plus_o5)
	add	%o0, 8, %o0
	EX_ST(STORE_INIT_MRU(%o4, %o0), memcpy_retl_o2_plus_o5)
	EX_LD(LOAD(ldx, %o1+40, %o4), memcpy_retl_o2_plus_o5)
	add	%o0, 8, %o0
	EX_ST(STORE_INIT_MRU(%o4, %o0), memcpy_retl_o2_plus_o5)
	EX_LD(LOAD(ldx, %o1+48, %o4), memcpy_retl_o2_plus_o5)
	add	%o1, 64, %o1
	add	%o0, 8, %o0
	EX_ST(STORE_INIT_MRU(%o4, %o0), memcpy_retl_o2_plus_o5)
	add	%o0, 8, %o0
	EX_LD(LOAD(ldx, %o1-8, %o4), memcpy_retl_o2_plus_o5)
	sub	%o5, 64, %o5
	bgu	%xcc,.Lalign_loop_rest
	! mark cache line as LRU
	 EX_ST(STORE_INIT(%o4, %o0), memcpy_retl_o2_plus_o5_plus_64)

	cmp	%o5, ST_CHUNK*64
	bgu,pt	%xcc, .Lalign_loop_start
	 mov	ST_CHUNK,%o3

	cmp	%o5, 0
	beq	.Lalign_done
	 nop
.Lalign_loop_fin:
	EX_LD(LOAD(ldx, %o1, %o4), memcpy_retl_o2_plus_o5)
	EX_ST(STORE(stx, %o4, %o0+8), memcpy_retl_o2_plus_o5)
	EX_LD(LOAD(ldx, %o1+8, %o4), memcpy_retl_o2_plus_o5)
	EX_ST(STORE(stx, %o4, %o0+8+8), memcpy_retl_o2_plus_o5)
	EX_LD(LOAD(ldx, %o1+16, %o4), memcpy_retl_o2_plus_o5)
	EX_ST(STORE(stx, %o4, %o0+8+16), memcpy_retl_o2_plus_o5)
	subcc	%o5, 64, %o5
	EX_LD(LOAD(ldx, %o1+24, %o4), memcpy_retl_o2_plus_o5_64)
	EX_ST(STORE(stx, %o4, %o0+8+24), memcpy_retl_o2_plus_o5_64)
	EX_LD(LOAD(ldx, %o1+32, %o4), memcpy_retl_o2_plus_o5_64)
	EX_ST(STORE(stx, %o4, %o0+8+32), memcpy_retl_o2_plus_o5_64)
	EX_LD(LOAD(ldx, %o1+40, %o4), memcpy_retl_o2_plus_o5_64)
	EX_ST(STORE(stx, %o4, %o0+8+40), memcpy_retl_o2_plus_o5_64)
	EX_LD(LOAD(ldx, %o1+48, %o4), memcpy_retl_o2_plus_o5_64)
	add	%o1, 64, %o1
	EX_ST(STORE(stx, %o4, %o0+8+48), memcpy_retl_o2_plus_o5_64)
	add	%o0, 64, %o0
	EX_LD(LOAD(ldx, %o1-8, %o4), memcpy_retl_o2_plus_o5_64)
	bgu	%xcc,.Lalign_loop_fin
	 EX_ST(STORE(stx, %o4, %o0), memcpy_retl_o2_plus_o5_64)

.Lalign_done:
	add	%o0, 8, %o0		! restore %o0 from ASI alignment
	membar	#StoreStore
	sub	%o2, 63, %o2		! adjust length to allow cc test
	ba	.Lmedl63		! in .Lmedl63
	 nop

	.align 16
	! Dst is on 8 byte boundary; src is not; remaining count > SMALL_MAX
.Lunalignsetup:
.Lunalignrejoin:
	mov	%g1, %o3	! save %g1 as VISEntryHalf clobbers it
#ifdef NON_USER_COPY
	VISEntryHalfFast(.Lmedium_vis_entry_fail_cp)
#else
	VISEntryHalf
#endif
	mov	%o3, %g1	! restore %g1

	set MED_UMAX, %o3
	cmp %o2, %o3 		! check for.Lmedium unaligned limit
	bge,pt	%xcc,.Lunalign_large
	 prefetch [%o1 + (4 * BLOCK_SIZE)], 20
	andn	%o2, 0x3f, %o5		! %o5 is multiple of block size
	and	%o2, 0x3f, %o2		! residue bytes in %o2
	cmp	%o2, 8			! Insure we do not load beyond
	bgt	.Lunalign_adjust	! end of source buffer
	 andn	%o1, 0x7, %o4		! %o4 has long word aligned src address
	add	%o2, 64, %o2		! adjust to leave loop
	sub	%o5, 64, %o5		! early if necessary
.Lunalign_adjust:
	alignaddr %o1, %g0, %g0		! generate %gsr
	add	%o1, %o5, %o1		! advance %o1 to after blocks
	EX_LD_FP(LOAD(ldd, %o4, %f0), memcpy_retl_o2_plus_o5)
.Lunalign_loop:
	EX_LD_FP(LOAD(ldd, %o4+8, %f2), memcpy_retl_o2_plus_o5)
	faligndata %f0, %f2, %f16
	EX_LD_FP(LOAD(ldd, %o4+16, %f4), memcpy_retl_o2_plus_o5)
	subcc	%o5, BLOCK_SIZE, %o5
	EX_ST_FP(STORE(std, %f16, %o0), memcpy_retl_o2_plus_o5_plus_64)
	faligndata %f2, %f4, %f18
	EX_LD_FP(LOAD(ldd, %o4+24, %f6), memcpy_retl_o2_plus_o5_plus_56)
	EX_ST_FP(STORE(std, %f18, %o0+8), memcpy_retl_o2_plus_o5_plus_56)
	faligndata %f4, %f6, %f20
	EX_LD_FP(LOAD(ldd, %o4+32, %f8), memcpy_retl_o2_plus_o5_plus_48)
	EX_ST_FP(STORE(std, %f20, %o0+16), memcpy_retl_o2_plus_o5_plus_48)
	faligndata %f6, %f8, %f22
	EX_LD_FP(LOAD(ldd, %o4+40, %f10), memcpy_retl_o2_plus_o5_plus_40)
	EX_ST_FP(STORE(std, %f22, %o0+24), memcpy_retl_o2_plus_o5_plus_40)
	faligndata %f8, %f10, %f24
	EX_LD_FP(LOAD(ldd, %o4+48, %f12), memcpy_retl_o2_plus_o5_plus_32)
	EX_ST_FP(STORE(std, %f24, %o0+32), memcpy_retl_o2_plus_o5_plus_32)
	faligndata %f10, %f12, %f26
	EX_LD_FP(LOAD(ldd, %o4+56, %f14), memcpy_retl_o2_plus_o5_plus_24)
	add	%o4, BLOCK_SIZE, %o4
	EX_ST_FP(STORE(std, %f26, %o0+40), memcpy_retl_o2_plus_o5_plus_24)
	faligndata %f12, %f14, %f28
	EX_LD_FP(LOAD(ldd, %o4, %f0), memcpy_retl_o2_plus_o5_plus_16)
	EX_ST_FP(STORE(std, %f28, %o0+48), memcpy_retl_o2_plus_o5_plus_16)
	faligndata %f14, %f0, %f30
	EX_ST_FP(STORE(std, %f30, %o0+56), memcpy_retl_o2_plus_o5_plus_8)
	add	%o0, BLOCK_SIZE, %o0
	bgu,pt	%xcc, .Lunalign_loop
	 prefetch [%o4 + (5 * BLOCK_SIZE)], 20
	ba	.Lunalign_done
	 nop

.Lunalign_large:
	andcc	%o0, 0x3f, %o3		! is dst 64-byte block aligned?
	bz	%xcc, .Lunalignsrc
	 sub	%o3, 64, %o3		! %o3 will be multiple of 8
	neg	%o3			! bytes until dest is 64 byte aligned
	sub	%o2, %o3, %o2		! update cnt with bytes to be moved
	! Move bytes according to source alignment
	andcc	%o1, 0x1, %o5
	bnz	%xcc, .Lunalignbyte	! check for byte alignment
	 nop
	andcc	%o1, 2, %o5		! check for half word alignment
	bnz	%xcc, .Lunalignhalf
	 nop
	! Src is word aligned
.Lunalignword:
	EX_LD_FP(LOAD(ld, %o1, %o4), memcpy_retl_o2_plus_o3)	! load 4 bytes
	add	%o1, 8, %o1		! increase src ptr by 8
	EX_ST_FP(STORE(stw, %o4, %o0), memcpy_retl_o2_plus_o3)	! and store 4
	subcc	%o3, 8, %o3		! decrease count by 8
	EX_LD_FP(LOAD(ld, %o1-4, %o4), memcpy_retl_o2_plus_o3_plus_4)! load 4
	add	%o0, 8, %o0		! increase dst ptr by 8
	bnz	%xcc, .Lunalignword
	 EX_ST_FP(STORE(stw, %o4, %o0-4), memcpy_retl_o2_plus_o3_plus_4)
	ba	.Lunalignsrc
	 nop

	! Src is half-word aligned
.Lunalignhalf:
	EX_LD_FP(LOAD(lduh, %o1, %o4), memcpy_retl_o2_plus_o3)	! load 2 bytes
	sllx	%o4, 32, %o5		! shift left
	EX_LD_FP(LOAD(lduw, %o1+2, %o4), memcpy_retl_o2_plus_o3)
	or	%o4, %o5, %o5
	sllx	%o5, 16, %o5
	EX_LD_FP(LOAD(lduh, %o1+6, %o4), memcpy_retl_o2_plus_o3)
	or	%o4, %o5, %o5
	EX_ST_FP(STORE(stx, %o5, %o0), memcpy_retl_o2_plus_o3)
	add	%o1, 8, %o1
	subcc	%o3, 8, %o3
	bnz	%xcc, .Lunalignhalf
	 add	%o0, 8, %o0
	ba	.Lunalignsrc
	 nop

	! Src is Byte aligned
.Lunalignbyte:
	sub	%o0, %o1, %o0		! share pointer advance
.Lunalignbyte_loop:
	EX_LD_FP(LOAD(ldub, %o1, %o4), memcpy_retl_o2_plus_o3)
	sllx	%o4, 56, %o5
	EX_LD_FP(LOAD(lduh, %o1+1, %o4), memcpy_retl_o2_plus_o3)
	sllx	%o4, 40, %o4
	or	%o4, %o5, %o5
	EX_LD_FP(LOAD(lduh, %o1+3, %o4), memcpy_retl_o2_plus_o3)
	sllx	%o4, 24, %o4
	or	%o4, %o5, %o5
	EX_LD_FP(LOAD(lduh, %o1+5, %o4), memcpy_retl_o2_plus_o3)
	sllx	%o4,  8, %o4
	or	%o4, %o5, %o5
	EX_LD_FP(LOAD(ldub, %o1+7, %o4), memcpy_retl_o2_plus_o3)
	or	%o4, %o5, %o5
	add	%o0, %o1, %o0
	EX_ST_FP(STORE(stx, %o5, %o0), memcpy_retl_o2_plus_o3)
	sub	%o0, %o1, %o0
	subcc	%o3, 8, %o3
	bnz	%xcc, .Lunalignbyte_loop
	 add	%o1, 8, %o1
	add	%o0,%o1, %o0 		! restore pointer

	! Destination is now block (64 byte aligned)
.Lunalignsrc:
	andn	%o2, 0x3f, %o5		! %o5 is multiple of block size
	and	%o2, 0x3f, %o2		! residue bytes in %o2
	add	%o2, 64, %o2		! Insure we do not load beyond
	sub	%o5, 64, %o5		! end of source buffer

	andn	%o1, 0x7, %o4		! %o4 has long word aligned src address
	alignaddr %o1, %g0, %g0		! generate %gsr
	add	%o1, %o5, %o1		! advance %o1 to after blocks

	EX_LD_FP(LOAD(ldd, %o4, %f14), memcpy_retl_o2_plus_o5)
	add	%o4, 8, %o4
.Lunalign_sloop:
	EX_LD_FP(LOAD(ldd, %o4, %f16), memcpy_retl_o2_plus_o5)
	faligndata %f14, %f16, %f0
	EX_LD_FP(LOAD(ldd, %o4+8, %f18), memcpy_retl_o2_plus_o5)
	faligndata %f16, %f18, %f2
	EX_LD_FP(LOAD(ldd, %o4+16, %f20), memcpy_retl_o2_plus_o5)
	faligndata %f18, %f20, %f4
	EX_ST_FP(STORE(std, %f0, %o0), memcpy_retl_o2_plus_o5)
	subcc	%o5, 64, %o5
	EX_LD_FP(LOAD(ldd, %o4+24, %f22), memcpy_retl_o2_plus_o5_plus_56)
	faligndata %f20, %f22, %f6
	EX_ST_FP(STORE(std, %f2, %o0+8), memcpy_retl_o2_plus_o5_plus_56)
	EX_LD_FP(LOAD(ldd, %o4+32, %f24), memcpy_retl_o2_plus_o5_plus_48)
	faligndata %f22, %f24, %f8
	EX_ST_FP(STORE(std, %f4, %o0+16), memcpy_retl_o2_plus_o5_plus_48)
	EX_LD_FP(LOAD(ldd, %o4+40, %f26), memcpy_retl_o2_plus_o5_plus_40)
	faligndata %f24, %f26, %f10
	EX_ST_FP(STORE(std, %f6, %o0+24), memcpy_retl_o2_plus_o5_plus_40)
	EX_LD_FP(LOAD(ldd, %o4+48, %f28), memcpy_retl_o2_plus_o5_plus_40)
	faligndata %f26, %f28, %f12
	EX_ST_FP(STORE(std, %f8, %o0+32), memcpy_retl_o2_plus_o5_plus_40)
	add	%o4, 64, %o4
	EX_LD_FP(LOAD(ldd, %o4-8, %f30), memcpy_retl_o2_plus_o5_plus_40)
	faligndata %f28, %f30, %f14
	EX_ST_FP(STORE(std, %f10, %o0+40), memcpy_retl_o2_plus_o5_plus_40)
	EX_ST_FP(STORE(std, %f12, %o0+48), memcpy_retl_o2_plus_o5_plus_40)
	add	%o0, 64, %o0
	EX_ST_FP(STORE(std, %f14, %o0-8), memcpy_retl_o2_plus_o5_plus_40)
	fsrc2	%f30, %f14
	bgu,pt	%xcc, .Lunalign_sloop
	 prefetch [%o4 + (8 * BLOCK_SIZE)], 20

.Lunalign_done:
	! Handle trailing bytes, 64 to 127
	! Dest long word aligned, Src not long word aligned
	cmp	%o2, 15
	bleu	%xcc, .Lunalign_short

	 andn	%o2, 0x7, %o5		! %o5 is multiple of 8
	and	%o2, 0x7, %o2		! residue bytes in %o2
	add	%o2, 8, %o2
	sub	%o5, 8, %o5		! insure we do not load past end of src
	andn	%o1, 0x7, %o4		! %o4 has long word aligned src address
	add	%o1, %o5, %o1		! advance %o1 to after multiple of 8
	EX_LD_FP(LOAD(ldd, %o4, %f0), memcpy_retl_o2_plus_o5)! fetch partialword
.Lunalign_by8:
	EX_LD_FP(LOAD(ldd, %o4+8, %f2), memcpy_retl_o2_plus_o5)
	add	%o4, 8, %o4
	faligndata %f0, %f2, %f16
	subcc	%o5, 8, %o5
	EX_ST_FP(STORE(std, %f16, %o0), memcpy_retl_o2_plus_o5)
	fsrc2	%f2, %f0
	bgu,pt	%xcc, .Lunalign_by8
	 add	%o0, 8, %o0

.Lunalign_short:
#ifdef NON_USER_COPY
	VISExitHalfFast
#else
	VISExitHalf
#endif
	ba	.Lsmallrest
	 nop

/*
 * This is a special case of nested memcpy. This can happen when kernel
 * calls unaligned memcpy back to back without saving FP registers. We need
 * traps(context switch) to save/restore FP registers. If the kernel calls
 * memcpy without this trap sequence we will hit FP corruption. Let's use
 * the normal integer load/store method in this case.
 */

#ifdef NON_USER_COPY
.Lmedium_vis_entry_fail_cp:
	or	%o0, %o1, %g2
#endif
.Lmedium_cp:
	LOAD(prefetch, %o1 + 0x40, #n_reads_strong)
	andcc	%g2, 0x7, %g0
	bne,pn	%xcc, .Lmedium_unaligned_cp
	 nop

.Lmedium_noprefetch_cp:
	andncc	%o2, 0x20 - 1, %o5
	be,pn	%xcc, 2f
	 sub	%o2, %o5, %o2
1:	EX_LD(LOAD(ldx, %o1 + 0x00, %o3), memcpy_retl_o2_plus_o5)
	EX_LD(LOAD(ldx, %o1 + 0x08, %g2), memcpy_retl_o2_plus_o5)
	EX_LD(LOAD(ldx, %o1 + 0x10, %g7), memcpy_retl_o2_plus_o5)
	EX_LD(LOAD(ldx, %o1 + 0x18, %o4), memcpy_retl_o2_plus_o5)
	add	%o1, 0x20, %o1
	subcc	%o5, 0x20, %o5
	EX_ST(STORE(stx, %o3, %o0 + 0x00), memcpy_retl_o2_plus_o5_plus_32)
	EX_ST(STORE(stx, %g2, %o0 + 0x08), memcpy_retl_o2_plus_o5_plus_24)
	EX_ST(STORE(stx, %g7, %o0 + 0x10), memcpy_retl_o2_plus_o5_plus_24)
	EX_ST(STORE(stx, %o4, %o0 + 0x18), memcpy_retl_o2_plus_o5_plus_8)
	bne,pt	%xcc, 1b
	 add	%o0, 0x20, %o0
2:	andcc	%o2, 0x18, %o5
	be,pt	%xcc, 3f
	 sub	%o2, %o5, %o2
1:	EX_LD(LOAD(ldx, %o1 + 0x00, %o3), memcpy_retl_o2_plus_o5)
	add	%o1, 0x08, %o1
	add	%o0, 0x08, %o0
	subcc	%o5, 0x08, %o5
	bne,pt	%xcc, 1b
	 EX_ST(STORE(stx, %o3, %o0 - 0x08), memcpy_retl_o2_plus_o5_plus_8)
3:	brz,pt	%o2, .Lexit_cp
	 cmp	%o2, 0x04
	bl,pn	%xcc, .Ltiny_cp
	 nop
	EX_LD(LOAD(lduw, %o1 + 0x00, %o3), memcpy_retl_o2)
	add	%o1, 0x04, %o1
	add	%o0, 0x04, %o0
	subcc	%o2, 0x04, %o2
	bne,pn	%xcc, .Ltiny_cp
	 EX_ST(STORE(stw, %o3, %o0 - 0x04), memcpy_retl_o2_plus_4)
	ba,a,pt	%xcc, .Lexit_cp

.Lmedium_unaligned_cp:
	/* First get dest 8 byte aligned.  */
	sub	%g0, %o0, %o3
	and	%o3, 0x7, %o3
	brz,pt	%o3, 2f
	 sub	%o2, %o3, %o2

1:	EX_LD(LOAD(ldub, %o1 + 0x00, %g2), memcpy_retl_o2_plus_g1)
	add	%o1, 1, %o1
	subcc	%o3, 1, %o3
	add	%o0, 1, %o0
	bne,pt	%xcc, 1b
	 EX_ST(STORE(stb, %g2, %o0 - 0x01), memcpy_retl_o2_plus_g1_plus_1)
2:
	and	%o1, 0x7, %o3
	brz,pn	%o3, .Lmedium_noprefetch_cp
	 sll	%o3, 3, %o3
	mov	64, %g2
	sub	%g2, %o3, %g2
	andn	%o1, 0x7, %o1
	EX_LD(LOAD(ldx, %o1 + 0x00, %o4), memcpy_retl_o2)
	sllx	%o4, %o3, %o4
	andn	%o2, 0x08 - 1, %o5
	sub	%o2, %o5, %o2

1:	EX_LD(LOAD(ldx, %o1 + 0x08, %g3), memcpy_retl_o2_plus_o5)
	add	%o1, 0x08, %o1
	subcc	%o5, 0x08, %o5
	srlx	%g3, %g2, %g7
	or	%g7, %o4, %g7
	EX_ST(STORE(stx, %g7, %o0 + 0x00), memcpy_retl_o2_plus_o5_plus_8)
	add	%o0, 0x08, %o0
	bne,pt	%xcc, 1b
	 sllx	%g3, %o3, %o4
	srl	%o3, 3, %o3
	add	%o1, %o3, %o1
	brz,pn	%o2, .Lexit_cp
	 nop
	ba,pt	%xcc, .Lsmall_unaligned_cp

.Ltiny_cp:
	EX_LD(LOAD(ldub, %o1 + 0x00, %o3), memcpy_retl_o2)
	subcc	%o2, 1, %o2
	be,pn	%xcc, .Lexit_cp
	 EX_ST(STORE(stb, %o3, %o0 + 0x00), memcpy_retl_o2_plus_1)
	EX_LD(LOAD(ldub, %o1 + 0x01, %o3), memcpy_retl_o2)
	subcc	%o2, 1, %o2
	be,pn	%xcc, .Lexit_cp
	 EX_ST(STORE(stb, %o3, %o0 + 0x01), memcpy_retl_o2_plus_1)
	EX_LD(LOAD(ldub, %o1 + 0x02, %o3), memcpy_retl_o2)
	ba,pt	%xcc, .Lexit_cp
	 EX_ST(STORE(stb, %o3, %o0 + 0x02), memcpy_retl_o2)

.Lsmall_cp:
	andcc	%g2, 0x3, %g0
	bne,pn	%xcc, .Lsmall_unaligned_cp
	 andn	%o2, 0x4 - 1, %o5
	sub	%o2, %o5, %o2
1:
	EX_LD(LOAD(lduw, %o1 + 0x00, %o3), memcpy_retl_o2_plus_o5)
	add	%o1, 0x04, %o1
	subcc	%o5, 0x04, %o5
	add	%o0, 0x04, %o0
	bne,pt	%xcc, 1b
	 EX_ST(STORE(stw, %o3, %o0 - 0x04), memcpy_retl_o2_plus_o5_plus_4)
	brz,pt	%o2, .Lexit_cp
	 nop
	ba,a,pt	%xcc, .Ltiny_cp

.Lsmall_unaligned_cp:
1:	EX_LD(LOAD(ldub, %o1 + 0x00, %o3), memcpy_retl_o2)
	add	%o1, 1, %o1
	add	%o0, 1, %o0
	subcc	%o2, 1, %o2
	bne,pt	%xcc, 1b
	 EX_ST(STORE(stb, %o3, %o0 - 0x01), memcpy_retl_o2_plus_1)
	ba,a,pt	%xcc, .Lexit_cp

.Lsmallrest:
	tst	%o2
	bz,pt	%xcc, .Lsmallx
	 cmp	%o2, 4
	blt,pn	%xcc, .Lsmallleft3
	 nop
	sub	%o2, 3, %o2
.Lsmallnotalign4:
	EX_LD(LOAD(ldub, %o1, %o3), memcpy_retl_o2_plus_3)! read byte
	subcc	%o2, 4, %o2		! reduce count by 4
	EX_ST(STORE(stb, %o3, %o0), memcpy_retl_o2_plus_7)! write byte & repeat
	EX_LD(LOAD(ldub, %o1+1, %o3), memcpy_retl_o2_plus_6)! for total of 4
	add	%o1, 4, %o1		! advance SRC by 4
	EX_ST(STORE(stb, %o3, %o0+1), memcpy_retl_o2_plus_6)
	EX_LD(LOAD(ldub, %o1-2, %o3), memcpy_retl_o2_plus_5)
	add	%o0, 4, %o0		! advance DST by 4
	EX_ST(STORE(stb, %o3, %o0-2), memcpy_retl_o2_plus_5)
	EX_LD(LOAD(ldub, %o1-1, %o3), memcpy_retl_o2_plus_4)
	bgu,pt	%xcc, .Lsmallnotalign4	! loop til 3 or fewer bytes remain
	EX_ST(STORE(stb, %o3, %o0-1), memcpy_retl_o2_plus_4)
	addcc	%o2, 3, %o2		! restore count
	bz,pt	%xcc, .Lsmallx
.Lsmallleft3:				! 1, 2, or 3 bytes remain
	subcc	%o2, 1, %o2
	EX_LD(LOAD(ldub, %o1, %o3), memcpy_retl_o2_plus_1)	! load one byte
	bz,pt	%xcc, .Lsmallx
	EX_ST(STORE(stb, %o3, %o0), memcpy_retl_o2_plus_1)	! store one byte
	EX_LD(LOAD(ldub, %o1+1, %o3), memcpy_retl_o2)	! load second byte
	subcc	%o2, 1, %o2
	bz,pt	%xcc, .Lsmallx
	EX_ST(STORE(stb, %o3, %o0+1), memcpy_retl_o2_plus_1)! store second byte
	EX_LD(LOAD(ldub, %o1+2, %o3), memcpy_retl_o2)	! load third byte
	EX_ST(STORE(stb, %o3, %o0+2), memcpy_retl_o2)	! store third byte
.Lsmallx:
	retl
	 mov	EX_RETVAL(%g1), %o0
.Lsmallfin:
	tst	%o2
	bnz,pn	%xcc, .Lsmallleft3
	 nop
	retl
	 mov	EX_RETVAL(%g1), %o0	! restore %o0
.Lexit_cp:
	retl
	 mov	EX_RETVAL(%g1), %o0
	.size  FUNC_NAME, .-FUNC_NAME
