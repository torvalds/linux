/*
 * M7memset.S: SPARC M7 optimized memset.
 *
 * Copyright (c) 2016, Oracle and/or its affiliates.  All rights reserved.
 */

/*
 * M7memset.S: M7 optimized memset.
 *
 * char *memset(sp, c, n)
 *
 * Set an array of n chars starting at sp to the character c.
 * Return sp.
 *
 * Fast assembler language version of the following C-program for memset
 * which represents the `standard' for the C-library.
 *
 *	void *
 *	memset(void *sp1, int c, size_t n)
 *	{
 *	    if (n != 0) {
 *		char *sp = sp1;
 *		do {
 *		    *sp++ = (char)c;
 *		} while (--n != 0);
 *	    }
 *	    return (sp1);
 *	}
 *
 * The algorithm is as follows :
 *
 *	For small 6 or fewer bytes stores, bytes will be stored.
 *
 *	For less than 32 bytes stores, align the address on 4 byte boundary.
 *	Then store as many 4-byte chunks, followed by trailing bytes.
 *
 *	For sizes greater than 32 bytes, align the address on 8 byte boundary.
 *	if (count >= 64) {
 *      	store 8-bytes chunks to align the address on 64 byte boundary
 *		if (value to be set is zero && count >= MIN_ZERO) {
 *              	Using BIS stores, set the first long word of each
 *			64-byte cache line to zero which will also clear the
 *			other seven long words of the cache line.
 *       	}
 *       	else if (count >= MIN_LOOP) {
 *       		Using BIS stores, set the first long word of each of
 *              	ST_CHUNK cache lines (64 bytes each) before the main
 *			loop is entered.
 *              	In the main loop, continue pre-setting the first long
 *              	word of each cache line ST_CHUNK lines in advance while
 *              	setting the other seven long words (56 bytes) of each
 * 			cache line until fewer than ST_CHUNK*64 bytes remain.
 *			Then set the remaining seven long words of each cache
 * 			line that has already had its first long word set.
 *       	}
 *       	store remaining data in 64-byte chunks until less than
 *       	64 bytes remain.
 *       }
 *       Store as many 8-byte chunks, followed by trailing bytes.
 *
 * BIS = Block Init Store
 *   Doing the advance store of the first element of the cache line
 *   initiates the displacement of a cache line while only using a single
 *   instruction in the pipeline. That avoids various pipeline delays,
 *   such as filling the miss buffer. The performance effect is
 *   similar to prefetching for normal stores.
 *   The special case for zero fills runs faster and uses fewer instruction
 *   cycles than the normal memset loop.
 *
 * We only use BIS for memset of greater than MIN_LOOP bytes because a sequence
 * BIS stores must be followed by a membar #StoreStore. The benefit of
 * the BIS store must be balanced against the cost of the membar operation.
 */

/*
 * ASI_STBI_P marks the cache line as "least recently used"
 * which means if many threads are active, it has a high chance
 * of being pushed out of the cache between the first initializing
 * store and the final stores.
 * Thus, we use ASI_STBIMRU_P which marks the cache line as
 * "most recently used" for all but the last store to the cache line.
 */

#include <asm/asi.h>
#include <asm/page.h>

#define ASI_STBI_P      ASI_BLK_INIT_QUAD_LDD_P
#define ASI_STBIMRU_P   ASI_ST_BLKINIT_MRU_P


#define ST_CHUNK        24   /* multiple of 4 due to loop unrolling */
#define MIN_LOOP        16320
#define MIN_ZERO        512

	.section	".text"
	.align		32

/*
 * Define clear_page(dest) as memset(dest, 0, PAGE_SIZE)
 * (can create a more optimized version later.)
 */
	.globl		M7clear_page
	.globl		M7clear_user_page
M7clear_page:		/* clear_page(dest) */
M7clear_user_page:
	set	PAGE_SIZE, %o1
	/* fall through into bzero code */

	.size		M7clear_page,.-M7clear_page
	.size		M7clear_user_page,.-M7clear_user_page

/*
 * Define bzero(dest, n) as memset(dest, 0, n)
 * (can create a more optimized version later.)
 */
	.globl		M7bzero
M7bzero:		/* bzero(dest, size) */
	mov	%o1, %o2
	mov	0, %o1
	/* fall through into memset code */

	.size		M7bzero,.-M7bzero

	.global		M7memset
	.type		M7memset, #function
	.register	%g3, #scratch
M7memset:
	mov     %o0, %o5                ! copy sp1 before using it
	cmp     %o2, 7                  ! if small counts, just write bytes
	bleu,pn %xcc, .wrchar
	 and     %o1, 0xff, %o1          ! o1 is (char)c

	sll     %o1, 8, %o3
	or      %o1, %o3, %o1           ! now o1 has 2 bytes of c
	sll     %o1, 16, %o3
	cmp     %o2, 32
	blu,pn  %xcc, .wdalign
	 or      %o1, %o3, %o1           ! now o1 has 4 bytes of c

	sllx    %o1, 32, %o3
	or      %o1, %o3, %o1           ! now o1 has 8 bytes of c

.dbalign:
	andcc   %o5, 7, %o3             ! is sp1 aligned on a 8 byte bound?
	bz,pt   %xcc, .blkalign         ! already long word aligned
	 sub     %o3, 8, %o3             ! -(bytes till long word aligned)

	add     %o2, %o3, %o2           ! update o2 with new count
	! Set -(%o3) bytes till sp1 long word aligned
1:	stb     %o1, [%o5]              ! there is at least 1 byte to set
	inccc   %o3                     ! byte clearing loop
	bl,pt   %xcc, 1b
	 inc     %o5

	! Now sp1 is long word aligned (sp1 is found in %o5)
.blkalign:
	cmp     %o2, 64                 ! check if there are 64 bytes to set
	blu,pn  %xcc, .wrshort
	 mov     %o2, %o3

	andcc   %o5, 63, %o3            ! is sp1 block aligned?
	bz,pt   %xcc, .blkwr            ! now block aligned
	 sub     %o3, 64, %o3            ! o3 is -(bytes till block aligned)
	add     %o2, %o3, %o2           ! o2 is the remainder

	! Store -(%o3) bytes till dst is block (64 byte) aligned.
	! Use long word stores.
	! Recall that dst is already long word aligned
1:
	addcc   %o3, 8, %o3
	stx     %o1, [%o5]
	bl,pt   %xcc, 1b
	 add     %o5, 8, %o5

	! Now sp1 is block aligned
.blkwr:
	andn    %o2, 63, %o4            ! calculate size of blocks in bytes
	brz,pn  %o1, .wrzero            ! special case if c == 0
	 and     %o2, 63, %o3            ! %o3 = bytes left after blk stores.

	set     MIN_LOOP, %g1
	cmp     %o4, %g1                ! check there are enough bytes to set
	blu,pn  %xcc, .short_set        ! to justify cost of membar
	                                ! must be > pre-cleared lines
	 nop

	! initial cache-clearing stores
	! get store pipeline moving
	rd	%asi, %g3		! save %asi to be restored later
	wr     %g0, ASI_STBIMRU_P, %asi

	! Primary memset loop for large memsets
.wr_loop:
	sub     %o5, 8, %o5		! adjust %o5 for ASI store alignment
	mov     ST_CHUNK, %g1
.wr_loop_start:
	stxa    %o1, [%o5+8]%asi
	subcc   %g1, 4, %g1
	stxa    %o1, [%o5+8+64]%asi
	add     %o5, 256, %o5
	stxa    %o1, [%o5+8-128]%asi
	bgu     %xcc, .wr_loop_start
	 stxa    %o1, [%o5+8-64]%asi

	sub     %o5, ST_CHUNK*64, %o5	! reset %o5
	mov     ST_CHUNK, %g1

.wr_loop_rest:
	stxa    %o1, [%o5+8+8]%asi
	sub     %o4, 64, %o4
	stxa    %o1, [%o5+16+8]%asi
	subcc   %g1, 1, %g1
	stxa    %o1, [%o5+24+8]%asi
	stxa    %o1, [%o5+32+8]%asi
	stxa    %o1, [%o5+40+8]%asi
	add     %o5, 64, %o5
	stxa    %o1, [%o5-8]%asi
	bgu     %xcc, .wr_loop_rest
	 stxa    %o1, [%o5]ASI_STBI_P

	! If more than ST_CHUNK*64 bytes remain to set, continue
	! setting the first long word of each cache line in advance
	! to keep the store pipeline moving.

	cmp     %o4, ST_CHUNK*64
	bge,pt  %xcc, .wr_loop_start
	 mov     ST_CHUNK, %g1

	brz,a,pn %o4, .asi_done
	 add     %o5, 8, %o5             ! restore %o5 offset

.wr_loop_small:
	stxa    %o1, [%o5+8]%asi
	stxa    %o1, [%o5+8+8]%asi
	stxa    %o1, [%o5+16+8]%asi
	stxa    %o1, [%o5+24+8]%asi
	stxa    %o1, [%o5+32+8]%asi
	subcc   %o4, 64, %o4
	stxa    %o1, [%o5+40+8]%asi
	add     %o5, 64, %o5
	stxa    %o1, [%o5-8]%asi
	bgu,pt  %xcc, .wr_loop_small
	 stxa    %o1, [%o5]ASI_STBI_P

	ba      .asi_done
	 add     %o5, 8, %o5             ! restore %o5 offset

	! Special case loop for zero fill memsets
	! For each 64 byte cache line, single STBI to first element
	! clears line
.wrzero:
	cmp     %o4, MIN_ZERO           ! check if enough bytes to set
					! to pay %asi + membar cost
	blu     %xcc, .short_set
	 nop
	sub     %o4, 256, %o4

.wrzero_loop:
	mov     64, %g3
	stxa    %o1, [%o5]ASI_STBI_P
	subcc   %o4, 256, %o4
	stxa    %o1, [%o5+%g3]ASI_STBI_P
	add     %o5, 256, %o5
	sub     %g3, 192, %g3
	stxa    %o1, [%o5+%g3]ASI_STBI_P
	add %g3, 64, %g3
	bge,pt  %xcc, .wrzero_loop
	 stxa    %o1, [%o5+%g3]ASI_STBI_P
	add     %o4, 256, %o4

	brz,pn  %o4, .bsi_done
	 nop

.wrzero_small:
	stxa    %o1, [%o5]ASI_STBI_P
	subcc   %o4, 64, %o4
	bgu,pt  %xcc, .wrzero_small
	 add     %o5, 64, %o5
	ba,a	.bsi_done

.asi_done:
	wr	%g3, 0x0, %asi		! restored saved %asi
.bsi_done:
	membar  #StoreStore             ! required by use of Block Store Init

.short_set:
	cmp     %o4, 64                 ! check if 64 bytes to set
	blu     %xcc, 5f
	 nop
4:                                      ! set final blocks of 64 bytes
	stx     %o1, [%o5]
	stx     %o1, [%o5+8]
	stx     %o1, [%o5+16]
	stx     %o1, [%o5+24]
	subcc   %o4, 64, %o4
	stx     %o1, [%o5+32]
	stx     %o1, [%o5+40]
	add     %o5, 64, %o5
	stx     %o1, [%o5-16]
	bgu,pt  %xcc, 4b
	 stx     %o1, [%o5-8]

5:
	! Set the remaining long words
.wrshort:
	subcc   %o3, 8, %o3             ! Can we store any long words?
	blu,pn  %xcc, .wrchars
	 and     %o2, 7, %o2             ! calc bytes left after long words
6:
	subcc   %o3, 8, %o3
	stx     %o1, [%o5]              ! store the long words
	bgeu,pt %xcc, 6b
	 add     %o5, 8, %o5

.wrchars:                               ! check for extra chars
	brnz    %o2, .wrfin
	 nop
	retl
	 nop

.wdalign:
	andcc   %o5, 3, %o3             ! is sp1 aligned on a word boundary
	bz,pn   %xcc, .wrword
	 andn    %o2, 3, %o3             ! create word sized count in %o3

	dec     %o2                     ! decrement count
	stb     %o1, [%o5]              ! clear a byte
	b       .wdalign
	 inc     %o5                     ! next byte

.wrword:
	subcc   %o3, 4, %o3
	st      %o1, [%o5]              ! 4-byte writing loop
	bnz,pt  %xcc, .wrword
	 add     %o5, 4, %o5

	and     %o2, 3, %o2             ! leftover count, if any

.wrchar:
	! Set the remaining bytes, if any
	brz     %o2, .exit
	 nop
.wrfin:
	deccc   %o2
	stb     %o1, [%o5]
	bgu,pt  %xcc, .wrfin
	 inc     %o5
.exit:
	retl                            ! %o0 was preserved
	 nop

	.size		M7memset,.-M7memset
