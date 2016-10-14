/* Optimized version of the standard memset() function.

   Copyright (c) 2002 Hewlett-Packard Co/CERN
	Sverre Jarp <Sverre.Jarp@cern.ch>

   Return: dest

   Inputs:
        in0:    dest
        in1:    value
        in2:    count

   The algorithm is fairly straightforward: set byte by byte until we
   we get to a 16B-aligned address, then loop on 128 B chunks using an
   early store as prefetching, then loop on 32B chucks, then clear remaining
   words, finally clear remaining bytes.
   Since a stf.spill f0 can store 16B in one go, we use this instruction
   to get peak speed when value = 0.  */

#include <asm/asmmacro.h>
#include <asm/export.h>
#undef ret

#define dest		in0
#define value		in1
#define	cnt		in2

#define tmp		r31
#define save_lc		r30
#define ptr0		r29
#define ptr1		r28
#define ptr2		r27
#define ptr3		r26
#define ptr9 		r24
#define	loopcnt		r23
#define linecnt		r22
#define bytecnt		r21

#define fvalue		f6

// This routine uses only scratch predicate registers (p6 - p15)
#define p_scr		p6			// default register for same-cycle branches
#define p_nz		p7
#define p_zr		p8
#define p_unalgn	p9
#define p_y		p11
#define p_n		p12
#define p_yy		p13
#define p_nn		p14

#define MIN1		15
#define MIN1P1HALF	8
#define LINE_SIZE	128
#define LSIZE_SH        7			// shift amount
#define PREF_AHEAD	8

GLOBAL_ENTRY(memset)
{ .mmi
	.prologue
	alloc	tmp = ar.pfs, 3, 0, 0, 0
	lfetch.nt1 [dest]			//
	.save   ar.lc, save_lc
	mov.i	save_lc = ar.lc
	.body
} { .mmi
	mov	ret0 = dest			// return value
	cmp.ne	p_nz, p_zr = value, r0		// use stf.spill if value is zero
	cmp.eq	p_scr, p0 = cnt, r0
;; }
{ .mmi
	and	ptr2 = -(MIN1+1), dest		// aligned address
	and	tmp = MIN1, dest		// prepare to check for correct alignment
	tbit.nz p_y, p_n = dest, 0		// Do we have an odd address? (M_B_U)
} { .mib
	mov	ptr1 = dest
	mux1	value = value, @brcst		// create 8 identical bytes in word
(p_scr)	br.ret.dpnt.many rp			// return immediately if count = 0
;; }
{ .mib
	cmp.ne	p_unalgn, p0 = tmp, r0		//
} { .mib
	sub	bytecnt = (MIN1+1), tmp		// NB: # of bytes to move is 1 higher than loopcnt
	cmp.gt	p_scr, p0 = 16, cnt		// is it a minimalistic task?
(p_scr)	br.cond.dptk.many .move_bytes_unaligned	// go move just a few (M_B_U)
;; }
{ .mmi
(p_unalgn) add	ptr1 = (MIN1+1), ptr2		// after alignment
(p_unalgn) add	ptr2 = MIN1P1HALF, ptr2		// after alignment
(p_unalgn) tbit.nz.unc p_y, p_n = bytecnt, 3	// should we do a st8 ?
;; }
{ .mib
(p_y)	add	cnt = -8, cnt			//
(p_unalgn) tbit.nz.unc p_yy, p_nn = bytecnt, 2	// should we do a st4 ?
} { .mib
(p_y)	st8	[ptr2] = value,-4		//
(p_n)	add	ptr2 = 4, ptr2			//
;; }
{ .mib
(p_yy)	add	cnt = -4, cnt			//
(p_unalgn) tbit.nz.unc p_y, p_n = bytecnt, 1	// should we do a st2 ?
} { .mib
(p_yy)	st4	[ptr2] = value,-2		//
(p_nn)	add	ptr2 = 2, ptr2			//
;; }
{ .mmi
	mov	tmp = LINE_SIZE+1		// for compare
(p_y)	add	cnt = -2, cnt			//
(p_unalgn) tbit.nz.unc p_yy, p_nn = bytecnt, 0	// should we do a st1 ?
} { .mmi
	setf.sig fvalue=value			// transfer value to FLP side
(p_y)	st2	[ptr2] = value,-1		//
(p_n)	add	ptr2 = 1, ptr2			//
;; }

{ .mmi
(p_yy)	st1	[ptr2] = value 			//
  	cmp.gt	p_scr, p0 = tmp, cnt		// is it a minimalistic task?
} { .mbb
(p_yy)	add	cnt = -1, cnt			//
(p_scr)	br.cond.dpnt.many .fraction_of_line	// go move just a few
;; }

{ .mib
	nop.m 0
	shr.u	linecnt = cnt, LSIZE_SH
(p_zr)	br.cond.dptk.many .l1b			// Jump to use stf.spill
;; }

	TEXT_ALIGN(32) // --------------------- //  L1A: store ahead into cache lines; fill later
{ .mmi
	and	tmp = -(LINE_SIZE), cnt		// compute end of range
	mov	ptr9 = ptr1			// used for prefetching
	and	cnt = (LINE_SIZE-1), cnt	// remainder
} { .mmi
	mov	loopcnt = PREF_AHEAD-1		// default prefetch loop
	cmp.gt	p_scr, p0 = PREF_AHEAD, linecnt	// check against actual value
;; }
{ .mmi
(p_scr)	add	loopcnt = -1, linecnt		//
	add	ptr2 = 8, ptr1			// start of stores (beyond prefetch stores)
	add	ptr1 = tmp, ptr1		// first address beyond total range
;; }
{ .mmi
	add	tmp = -1, linecnt		// next loop count
	mov.i	ar.lc = loopcnt			//
;; }
.pref_l1a:
{ .mib
	stf8 [ptr9] = fvalue, 128		// Do stores one cache line apart
	nop.i	0
	br.cloop.dptk.few .pref_l1a
;; }
{ .mmi
	add	ptr0 = 16, ptr2			// Two stores in parallel
	mov.i	ar.lc = tmp			//
;; }
.l1ax:
 { .mmi
	stf8 [ptr2] = fvalue, 8
	stf8 [ptr0] = fvalue, 8
 ;; }
 { .mmi
	stf8 [ptr2] = fvalue, 24
	stf8 [ptr0] = fvalue, 24
 ;; }
 { .mmi
	stf8 [ptr2] = fvalue, 8
	stf8 [ptr0] = fvalue, 8
 ;; }
 { .mmi
	stf8 [ptr2] = fvalue, 24
	stf8 [ptr0] = fvalue, 24
 ;; }
 { .mmi
	stf8 [ptr2] = fvalue, 8
	stf8 [ptr0] = fvalue, 8
 ;; }
 { .mmi
	stf8 [ptr2] = fvalue, 24
	stf8 [ptr0] = fvalue, 24
 ;; }
 { .mmi
	stf8 [ptr2] = fvalue, 8
	stf8 [ptr0] = fvalue, 32
 	cmp.lt	p_scr, p0 = ptr9, ptr1		// do we need more prefetching?
 ;; }
{ .mmb
	stf8 [ptr2] = fvalue, 24
(p_scr)	stf8 [ptr9] = fvalue, 128
	br.cloop.dptk.few .l1ax
;; }
{ .mbb
	cmp.le  p_scr, p0 = 8, cnt		// just a few bytes left ?
(p_scr) br.cond.dpnt.many  .fraction_of_line	// Branch no. 2
	br.cond.dpnt.many  .move_bytes_from_alignment	// Branch no. 3
;; }

	TEXT_ALIGN(32)
.l1b:	// ------------------------------------ //  L1B: store ahead into cache lines; fill later
{ .mmi
	and	tmp = -(LINE_SIZE), cnt		// compute end of range
	mov	ptr9 = ptr1			// used for prefetching
	and	cnt = (LINE_SIZE-1), cnt	// remainder
} { .mmi
	mov	loopcnt = PREF_AHEAD-1		// default prefetch loop
	cmp.gt	p_scr, p0 = PREF_AHEAD, linecnt	// check against actual value
;; }
{ .mmi
(p_scr)	add	loopcnt = -1, linecnt
	add	ptr2 = 16, ptr1			// start of stores (beyond prefetch stores)
	add	ptr1 = tmp, ptr1		// first address beyond total range
;; }
{ .mmi
	add	tmp = -1, linecnt		// next loop count
	mov.i	ar.lc = loopcnt
;; }
.pref_l1b:
{ .mib
	stf.spill [ptr9] = f0, 128		// Do stores one cache line apart
	nop.i   0
	br.cloop.dptk.few .pref_l1b
;; }
{ .mmi
	add	ptr0 = 16, ptr2			// Two stores in parallel
	mov.i	ar.lc = tmp
;; }
.l1bx:
 { .mmi
	stf.spill [ptr2] = f0, 32
	stf.spill [ptr0] = f0, 32
 ;; }
 { .mmi
	stf.spill [ptr2] = f0, 32
	stf.spill [ptr0] = f0, 32
 ;; }
 { .mmi
	stf.spill [ptr2] = f0, 32
	stf.spill [ptr0] = f0, 64
 	cmp.lt	p_scr, p0 = ptr9, ptr1		// do we need more prefetching?
 ;; }
{ .mmb
	stf.spill [ptr2] = f0, 32
(p_scr)	stf.spill [ptr9] = f0, 128
	br.cloop.dptk.few .l1bx
;; }
{ .mib
	cmp.gt  p_scr, p0 = 8, cnt		// just a few bytes left ?
(p_scr)	br.cond.dpnt.many  .move_bytes_from_alignment	//
;; }

.fraction_of_line:
{ .mib
	add	ptr2 = 16, ptr1
	shr.u	loopcnt = cnt, 5   		// loopcnt = cnt / 32
;; }
{ .mib
	cmp.eq	p_scr, p0 = loopcnt, r0
	add	loopcnt = -1, loopcnt
(p_scr)	br.cond.dpnt.many .store_words
;; }
{ .mib
	and	cnt = 0x1f, cnt			// compute the remaining cnt
	mov.i   ar.lc = loopcnt
;; }
	TEXT_ALIGN(32)
.l2:	// ------------------------------------ //  L2A:  store 32B in 2 cycles
{ .mmb
	stf8	[ptr1] = fvalue, 8
	stf8	[ptr2] = fvalue, 8
;; } { .mmb
	stf8	[ptr1] = fvalue, 24
	stf8	[ptr2] = fvalue, 24
	br.cloop.dptk.many .l2
;; }
.store_words:
{ .mib
	cmp.gt	p_scr, p0 = 8, cnt		// just a few bytes left ?
(p_scr)	br.cond.dpnt.many .move_bytes_from_alignment	// Branch
;; }

{ .mmi
	stf8	[ptr1] = fvalue, 8		// store
	cmp.le	p_y, p_n = 16, cnt
	add	cnt = -8, cnt			// subtract
;; }
{ .mmi
(p_y)	stf8	[ptr1] = fvalue, 8		// store
(p_y)	cmp.le.unc p_yy, p_nn = 16, cnt
(p_y)	add	cnt = -8, cnt			// subtract
;; }
{ .mmi						// store
(p_yy)	stf8	[ptr1] = fvalue, 8
(p_yy)	add	cnt = -8, cnt			// subtract
;; }

.move_bytes_from_alignment:
{ .mib
	cmp.eq	p_scr, p0 = cnt, r0
	tbit.nz.unc p_y, p0 = cnt, 2		// should we terminate with a st4 ?
(p_scr)	br.cond.dpnt.few .restore_and_exit
;; }
{ .mib
(p_y)	st4	[ptr1] = value,4
	tbit.nz.unc p_yy, p0 = cnt, 1		// should we terminate with a st2 ?
;; }
{ .mib
(p_yy)	st2	[ptr1] = value,2
	tbit.nz.unc p_y, p0 = cnt, 0		// should we terminate with a st1 ?
;; }

{ .mib
(p_y)	st1	[ptr1] = value
;; }
.restore_and_exit:
{ .mib
	nop.m	0
	mov.i	ar.lc = save_lc
	br.ret.sptk.many rp
;; }

.move_bytes_unaligned:
{ .mmi
       .pred.rel "mutex",p_y, p_n
       .pred.rel "mutex",p_yy, p_nn
(p_n)	cmp.le  p_yy, p_nn = 4, cnt
(p_y)	cmp.le  p_yy, p_nn = 5, cnt
(p_n)	add	ptr2 = 2, ptr1
} { .mmi
(p_y)	add	ptr2 = 3, ptr1
(p_y)	st1	[ptr1] = value, 1		// fill 1 (odd-aligned) byte [15, 14 (or less) left]
(p_y)	add	cnt = -1, cnt
;; }
{ .mmi
(p_yy)	cmp.le.unc p_y, p0 = 8, cnt
	add	ptr3 = ptr1, cnt		// prepare last store
	mov.i	ar.lc = save_lc
} { .mmi
(p_yy)	st2	[ptr1] = value, 4		// fill 2 (aligned) bytes
(p_yy)	st2	[ptr2] = value, 4		// fill 2 (aligned) bytes [11, 10 (o less) left]
(p_yy)	add	cnt = -4, cnt
;; }
{ .mmi
(p_y)	cmp.le.unc p_yy, p0 = 8, cnt
	add	ptr3 = -1, ptr3			// last store
	tbit.nz p_scr, p0 = cnt, 1		// will there be a st2 at the end ?
} { .mmi
(p_y)	st2	[ptr1] = value, 4		// fill 2 (aligned) bytes
(p_y)	st2	[ptr2] = value, 4		// fill 2 (aligned) bytes [7, 6 (or less) left]
(p_y)	add	cnt = -4, cnt
;; }
{ .mmi
(p_yy)	st2	[ptr1] = value, 4		// fill 2 (aligned) bytes
(p_yy)	st2	[ptr2] = value, 4		// fill 2 (aligned) bytes [3, 2 (or less) left]
	tbit.nz p_y, p0 = cnt, 0		// will there be a st1 at the end ?
} { .mmi
(p_yy)	add	cnt = -4, cnt
;; }
{ .mmb
(p_scr)	st2	[ptr1] = value			// fill 2 (aligned) bytes
(p_y)	st1	[ptr3] = value			// fill last byte (using ptr3)
	br.ret.sptk.many rp
}
END(memset)
EXPORT_SYMBOL(memset)
