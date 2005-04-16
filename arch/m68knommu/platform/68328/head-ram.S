#include <linux/config.h>

	.global __main
	.global __ram_start
	.global __ram_end
	.global __rom_start
	.global __rom_end

        .global _rambase
        .global _ramstart
	
	.global splash_bits
	.global _start
	.global _stext

#define DEBUG
#define ROM_OFFSET 0x10C00000
#define STACK_GAURD 0x10

	.text
	
_start:
_stext:
	movew	#0x2700, %sr            /* Exceptions off! */

#if 0
	/* Init chip registers.  uCsimm specific */
	moveb	#0x00,   0xfffffb0b	/* Watchdog off */
	moveb	#0x10,   0xfffff000	/* SCR */

	movew   #0x2400, 0xfffff200	/* PLLCR */
	movew   #0x0123, 0xfffff202	/* PLLFSR */

	moveb	#0x00,   0xfffff40b	/* enable chip select */
	moveb	#0x00,   0xfffff423	/* enable /DWE */
	moveb	#0x08,   0xfffffd0d	/* disable hardmap */
	moveb	#0x07,   0xfffffd0e	/* level 7 interrupt clear */

	movew	#0x8600, 0xfffff100	/* FLASH at 0x10c00000 */
	movew	#0x018b, 0xfffff110	/* 2Meg, enable, 0ws */

	movew	#0x8f00, 0xfffffc00	/* DRAM configuration */
	movew	#0x9667, 0xfffffc02	/* DRAM control */
	movew	#0x0000, 0xfffff106	/* DRAM at 0x00000000 */
	movew	#0x068f, 0xfffff116	/* 8Meg, enable, 0ws */

	moveb	#0x40,   0xfffff300	/* IVR */
	movel	#0x007FFFFF, %d0	/* IMR */
	movel	%d0,     0xfffff304

	moveb	0xfffff42b, %d0
	andb	#0xe0,	 %d0
	moveb	%d0,	 0xfffff42b

	moveb	#0x08,   0xfffff907	/* Ignore CTS */
	movew	#0x010b, 0xfffff902	/* BAUD to 9600 */
	movew	#0xe100, 0xfffff900	/* enable */
#endif

	movew	#16384, %d0  /* PLL settle wait loop */
L0:
	subw	#1, %d0
	bne	L0
#ifdef DEBUG
	moveq	#70, %d7		/* 'F' */
	moveb	%d7,0xfffff907          /* No absolute addresses */
pclp1:
	movew	0xfffff906, %d7
	andw	#0x2000, %d7
	beq	pclp1
#endif /* DEBUG */

#ifdef CONFIG_RELOCATE
	/* Copy me to RAM */
	moveal	#__rom_start, %a0
	moveal	#__ram_start, %a1
	moveal	#_edata, %a2

	/* Copy %a0 to %a1 until %a1 == %a2 */
LD1:
	movel	%a0@+, %d0
	movel	%d0, %a1@+
	cmpal	%a1, %a2
	bhi	LD1
	
#ifdef DEBUG
	moveq	#74, %d7		/* 'J' */
	moveb	%d7,0xfffff907          /* No absolute addresses */
pclp2:
	movew	0xfffff906, %d7
	andw	#0x2000, %d7
	beq	pclp2
#endif /* DEBUG */
	/* jump into the RAM copy */
	jmp     ram_jump
ram_jump:

#endif /* CONFIG_RELOCATE */

#ifdef DEBUG
	moveq	#82, %d7		/* 'R' */
	moveb	%d7,0xfffff907          /* No absolute addresses */
pclp3:
	movew	0xfffff906, %d7
	andw	#0x2000, %d7
	beq	pclp3
#endif /* DEBUG */
	moveal	#0x007ffff0, %ssp
	moveal	#_sbss, %a0
	moveal	#_ebss, %a1

	/* Copy 0 to %a0 until %a0 >= %a1 */
L1:
	movel	#0, %a0@+
	cmpal	%a0, %a1
	bhi	L1

#ifdef DEBUG
	moveq	#67, %d7                /* 'C' */
	jsr	putc
#endif /* DEBUG */

	pea	0
	pea	env
	pea	%sp@(4)
	pea	0

#ifdef DEBUG
	moveq	#70, %d7		/* 'F' */
	jsr	putc
#endif /* DEBUG */

lp:
	jsr	start_kernel
        jmp lp
_exit:

	jmp	_exit

__main:
	/* nothing */
	rts

#ifdef DEBUG
putc:
	moveb	%d7,0xfffff907
pclp:
	movew	0xfffff906, %d7
	andw	#0x2000, %d7
	beq	pclp
	rts
#endif /* DEBUG */

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

env:
	.long	0
