/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 1998-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <asm/asmmacro.h>
#include <asm/pal.h>

	.bss
	.align 16
stack_mem:
	.skip 16834

	.text

/* This needs to be defined because lib/string.c:strlcat() calls it in case of error... */
GLOBAL_ENTRY(printk)
	break 0
END(printk)

GLOBAL_ENTRY(_start)
	.prologue
	.save rp, r0
	.body
	movl gp = __gp
	movl sp = stack_mem+16384-16
	bsw.1
	br.call.sptk.many rp=start_bootloader
0:	nop 0		  /* dummy nop to make unwinding work */
END(_start)

/*
 * Set a break point on this function so that symbols are available to set breakpoints in
 * the kernel being debugged.
 */
GLOBAL_ENTRY(debug_break)
	br.ret.sptk.many b0
END(debug_break)

GLOBAL_ENTRY(ssc)
	.regstk 5,0,0,0
	mov r15=in4
	break 0x80001
	br.ret.sptk.many b0
END(ssc)

GLOBAL_ENTRY(jmp_to_kernel)
	.regstk 2,0,0,0
	mov r28=in0
	mov b7=in1
	br.sptk.few b7
END(jmp_to_kernel)

/*
 * r28 contains the index of the PAL function
 * r29--31 the args
 * Return values in ret0--3 (r8--11)
 */
GLOBAL_ENTRY(pal_emulator_static)
	mov r8=-1
	mov r9=256
	;;
	cmp.gtu p6,p7=r9,r28		/* r28 <= 255? */
(p6)	br.cond.sptk.few static
	;;
	mov r9=512
	;;
	cmp.gtu p6,p7=r9,r28
(p6)	br.cond.sptk.few stacked
	;;
static:	cmp.eq p6,p7=PAL_PTCE_INFO,r28
(p7)	br.cond.sptk.few 1f
	;;
	mov r8=0			/* status = 0 */
	movl r9=0x100000000		/* tc.base */
	movl r10=0x0000000200000003	/* count[0], count[1] */
	movl r11=0x1000000000002000	/* stride[0], stride[1] */
	br.cond.sptk.few rp
1:	cmp.eq p6,p7=PAL_FREQ_RATIOS,r28
(p7)	br.cond.sptk.few 1f
	mov r8=0			/* status = 0 */
	movl r9 =0x100000064		/* proc_ratio (1/100) */
	movl r10=0x100000100		/* bus_ratio<<32 (1/256) */
	movl r11=0x100000064		/* itc_ratio<<32 (1/100) */
	;;
1:	cmp.eq p6,p7=PAL_RSE_INFO,r28
(p7)	br.cond.sptk.few 1f
	mov r8=0			/* status = 0 */
	mov r9=96			/* num phys stacked */
	mov r10=0			/* hints */
	mov r11=0
	br.cond.sptk.few rp
1:	cmp.eq p6,p7=PAL_CACHE_FLUSH,r28		/* PAL_CACHE_FLUSH */
(p7)	br.cond.sptk.few 1f
	mov r9=ar.lc
	movl r8=524288			/* flush 512k million cache lines (16MB) */
	;;
	mov ar.lc=r8
	movl r8=0xe000000000000000
	;;
.loop:	fc r8
	add r8=32,r8
	br.cloop.sptk.few .loop
	sync.i
	;;
	srlz.i
	;;
	mov ar.lc=r9
	mov r8=r0
	;;
1:	cmp.eq p6,p7=PAL_PERF_MON_INFO,r28
(p7)	br.cond.sptk.few 1f
	mov r8=0			/* status = 0 */
	movl r9 =0x08122f04		/* generic=4 width=47 retired=8 cycles=18 */
	mov r10=0			/* reserved */
	mov r11=0			/* reserved */
	mov r16=0xffff			/* implemented PMC */
	mov r17=0x3ffff			/* implemented PMD */
	add r18=8,r29			/* second index */
	;;
	st8 [r29]=r16,16		/* store implemented PMC */
	st8 [r18]=r0,16			/* clear remaining bits  */
	;;
	st8 [r29]=r0,16			/* clear remaining bits  */
	st8 [r18]=r0,16			/* clear remaining bits  */
	;;
	st8 [r29]=r17,16		/* store implemented PMD */
	st8 [r18]=r0,16			/* clear remaining bits  */
	mov r16=0xf0			/* cycles count capable PMC */
	;;
	st8 [r29]=r0,16			/* clear remaining bits  */
	st8 [r18]=r0,16			/* clear remaining bits  */
	mov r17=0xf0			/* retired bundles capable PMC */
	;;
	st8 [r29]=r16,16		/* store cycles capable */
	st8 [r18]=r0,16			/* clear remaining bits  */
	;;
	st8 [r29]=r0,16			/* clear remaining bits  */
	st8 [r18]=r0,16			/* clear remaining bits  */
	;;
	st8 [r29]=r17,16		/* store retired bundle capable */
	st8 [r18]=r0,16			/* clear remaining bits  */
	;;
	st8 [r29]=r0,16			/* clear remaining bits  */
	st8 [r18]=r0,16			/* clear remaining bits  */
	;;
1:	cmp.eq p6,p7=PAL_VM_SUMMARY,r28
(p7)	br.cond.sptk.few 1f
	mov	r8=0			/* status = 0  */
	movl	r9=0x2044040020F1865	/* num_tc_levels=2, num_unique_tcs=4 */
					/* max_itr_entry=64, max_dtr_entry=64 */
					/* hash_tag_id=2, max_pkr=15 */
					/* key_size=24, phys_add_size=50, vw=1 */
	movl	r10=0x183C		/* rid_size=24, impl_va_msb=60 */
	;;
1:	cmp.eq p6,p7=PAL_MEM_ATTRIB,r28
(p7)	br.cond.sptk.few 1f
	mov	r8=0			/* status = 0 */
	mov	r9=0x80|0x01		/* NatPage|WB */
	;;
1:	br.cond.sptk.few rp
stacked:
	br.ret.sptk.few rp
END(pal_emulator_static)
