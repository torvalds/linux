/*
 * linux/arch/m68knommu/platform/68328/head-pilot.S
 * - A startup file for the MC68328
 *
 * Copyright (C) 1998  D. Jeff Dionne <jeff@ryeham.ee.ryerson.ca>,
 *                     Kenneth Albanowski <kjahds@kjahds.com>,
 *                     The Silver Hammer Group, Ltd.
 *
 * (c) 1995, Dionne & Associates
 * (c) 1995, DKG Display Tech.
 */

#define ASSEMBLY

#define IMMED #
#define	DBG_PUTC(x)	moveb IMMED x, 0xfffff907


.global _stext
.global _start

.global _rambase
.global _ramvec
.global _ramstart
.global _ramend

.global bootlogo_bits

/*****************************************************************************/

.data

/*
 *      Set up the usable of RAM stuff. Size of RAM is determined then
 *      an initial stack set up at the end.
 */
.align 4
_ramvec:
.long   0
_rambase:
.long   0
_ramstart:
.long   0
_ramend:
.long   0

.text
	
_start:
_stext:


#ifdef CONFIG_M68328

#ifdef CONFIG_PILOT
	.byte 0x4e, 0xfa, 0x00, 0x0a /* Jmp +X bytes */
	.byte 'b', 'o', 'o', 't'
	.word 10000

	nop
#endif

	moveq	#0, %d0
	movew   %d0, 0xfffff618 /* Watchdog off */
	movel	#0x00011f07, 0xfffff114 /* CS A1 Mask */

	movew	#0x0800, 0xfffff906 /* Ignore CTS */
	movew	#0x010b, 0xfffff902 /* BAUD to 9600 */

	movew	#0x2410, 0xfffff200 /* PLLCR */
	movew	#0x123, 0xfffff202 /* PLLFSR */

#ifdef CONFIG_PILOT
	moveb	#0, 0xfffffA27 /* LCKCON */
	movel   #_start, 0xfffffA00 /* LSSA */
	moveb   #0xa, 0xfffffA05 /* LVPW */
	movew	#0x9f, 0xFFFFFa08 /* LXMAX */
	movew	#0x9f, 0xFFFFFa0a /* LYMAX */
	moveb   #9, 0xfffffa29 /* LBAR */
	moveb   #0, 0xfffffa25 /* LPXCD */
	moveb	#0x04, 0xFFFFFa20 /* LPICF */
	moveb	#0x58, 0xfffffA27 /* LCKCON */
	moveb	#0x85, 0xfffff429 /* PFDATA */
	moveb	#0xd8, 0xfffffA27 /* LCKCON */
	moveb	#0xc5, 0xfffff429 /* PFDATA */
	moveb	#0xd5, 0xfffff429 /* PFDATA */

	moveal	#0x00100000, %a3
	moveal	#0x100ffc00, %a4
#endif /* CONFIG_PILOT */

#endif /* CONFIG_M68328 */

	movew   #0x2700, %sr
	lea	%a4@(-4), %sp

	DBG_PUTC('\r')
	DBG_PUTC('\n')
	DBG_PUTC('A')

   	moveq   #0,%d0
	movew	#16384, %d0  /* PLL settle wait loop */
L0:
	subw	#1, %d0
	bne	L0

	DBG_PUTC('B')

	/* Copy command line from beginning of RAM (+16) to end of bss */
	movel	#CONFIG_VECTORBASE, %d7
	addl	#16, %d7
	moveal	%d7, %a0
	moveal	#__bss_stop, %a1
	lea	%a1@(512), %a2

	DBG_PUTC('C')

	/* Copy %a0 to %a1 until %a1 == %a2 */
L2:
	movel	%a0@+, %d0
	movel	%d0, %a1@+
	cmpal	%a1, %a2
	bhi	L2

	/* Copy data+init segment from ROM to RAM */
	moveal	#_etext, %a0
	moveal	#_sdata, %a1
	moveal	#__init_end, %a2

	DBG_PUTC('D')

	/* Copy %a0 to %a1 until %a1 == %a2 */
LD1:
	movel	%a0@+, %d0
	movel	%d0, %a1@+
	cmpal	%a1, %a2
	bhi	LD1

	DBG_PUTC('E')

	moveal	#__bss_start, %a0
	moveal	#__bss_stop, %a1

	/* Copy 0 to %a0 until %a0 == %a1 */
L1:
	movel	#0, %a0@+
	cmpal	%a0, %a1
	bhi	L1

	DBG_PUTC('F')

	/* Copy command line from end of bss to command line */
	moveal	#__bss_stop, %a0
	moveal	#command_line, %a1
	lea	%a1@(512), %a2

	DBG_PUTC('G')

	/* Copy %a0 to %a1 until %a1 == %a2 */
L3:
	movel	%a0@+, %d0
	movel	%d0, %a1@+
	cmpal	%a1, %a2
	bhi	L3

	movel	#_sdata, %d0	
	movel	%d0, _rambase	
	movel	#__bss_stop, %d0
	movel	%d0, _ramstart

	movel	%a4, %d0
	subl	#4096, %d0	/* Reserve 4K of stack */
	moveq	#79, %d7
	movel	%d0, _ramend

	pea	0
	pea	env
	pea	%sp@(4)
	pea	0

	DBG_PUTC('H')

#ifdef CONFIG_PILOT
	movel	#bootlogo_bits, 0xFFFFFA00
	moveb	#10, 0xFFFFFA05
	movew	#160, 0xFFFFFA08
	movew	#160, 0xFFFFFA0A
#endif /* CONFIG_PILOT */

	DBG_PUTC('I')

	lea	init_thread_union, %a0
	lea	0x2000(%a0), %sp

	DBG_PUTC('J')
	DBG_PUTC('\r')
	DBG_PUTC('\n')

	jsr	start_kernel
_exit:

	jmp	_exit


	.data
env:
	.long	0
