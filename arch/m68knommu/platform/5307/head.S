/*****************************************************************************/

/*
 *	head.S -- common startup code for ColdFire CPUs.
 *
 *	(C) Copyright 1999-2004, Greg Ungerer (gerg@snapgear.com).
 */

/*****************************************************************************/

#include <linux/config.h>
#include <linux/sys.h>
#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/coldfire.h>
#include <asm/mcfcache.h>
#include <asm/mcfsim.h>

/*****************************************************************************/

/*
 *	Define fixed memory sizes. Configuration of a fixed memory size
 *	overrides everything else. If the user defined a size we just
 *	blindly use it (they know what they are doing right :-)
 */
#if defined(CONFIG_RAM32MB)
#define MEM_SIZE	0x02000000	/* memory size 32Mb */
#elif defined(CONFIG_RAM16MB)
#define MEM_SIZE	0x01000000	/* memory size 16Mb */
#elif defined(CONFIG_RAM8MB)
#define MEM_SIZE	0x00800000	/* memory size 8Mb */
#elif defined(CONFIG_RAM4MB)
#define MEM_SIZE	0x00400000	/* memory size 4Mb */
#elif defined(CONFIG_RAM1MB)
#define MEM_SIZE	0x00100000	/* memory size 1Mb */
#endif

/*
 *	Memory size exceptions for special cases. Some boards may be set
 *	for auto memory sizing, but we can't do it that way for some reason.
 *	For example the 5206eLITE board has static RAM, and auto-detecting
 *	the SDRAM will do you no good at all. Same goes for the MOD5272.
 */
#ifdef CONFIG_RAMAUTO
#if defined(CONFIG_M5206eLITE)
#define	MEM_SIZE	0x00100000	/* 1MiB default memory */
#endif
#if defined(CONFIG_MOD5272)
#define MEM_SIZE	0x00800000	/* 8MiB default memory */
#endif
#endif /* CONFIG_RAMAUTO */


/*
 *	If we don't have a fixed memory size now, then lets build in code
 *	to auto detect the DRAM size. Obviously this is the prefered
 *	method, and should work for most boards (it won't work for those
 *	that do not have their RAM starting at address 0).
 */
#if defined(MEM_SIZE)
.macro GET_MEM_SIZE
	movel	#MEM_SIZE,%d0		/* hard coded memory size */
.endm

#elif defined(CONFIG_M5206) || defined(CONFIG_M5206e) || \
      defined(CONFIG_M5249) || defined(CONFIG_M527x) || \
      defined(CONFIG_M528x) || defined(CONFIG_M5307) || \
      defined(CONFIG_M5407)
/*
 *	Not all these devices have exactly the same DRAM controller,
 *	but the DCMR register is virtually identical - give or take
 *	a couple of bits. The only exception is the 5272 devices, their
 *	DRAM controller is quite different.
 */
.macro GET_MEM_SIZE
	movel	MCF_MBAR+MCFSIM_DMR0,%d0 /* get mask for 1st bank */
	btst	#0,%d0			/* check if region enabled */
	beq	1f
	andl	#0xfffc0000,%d0
	beq	1f
	addl	#0x00040000,%d0		/* convert mask to size */
1:
	movel	MCF_MBAR+MCFSIM_DMR1,%d1 /* get mask for 2nd bank */
	btst	#0,%d1			/* check if region enabled */
	beq	2f
	andl	#0xfffc0000, %d1
	beq	2f
	addl	#0x00040000,%d1
	addl	%d1,%d0			/* total mem size in d0 */
2:
.endm

#elif defined(CONFIG_M5272)
.macro GET_MEM_SIZE
	movel	MCF_MBAR+MCFSIM_CSOR7,%d0 /* get SDRAM address mask */
	andil	#0xfffff000,%d0		/* mask out chip select options */
	negl	%d0			/* negate bits */
.endm

#else
#error "ERROR: I don't know how to determine your boards memory size?"
#endif


/*
 *	Most ColdFire boards have their DRAM starting at address 0.
 *	Notable exception is the 5206eLITE board, another is the MOD5272.
 */
#if defined(CONFIG_M5206eLITE)
#define	MEM_BASE	0x30000000
#endif
#if defined(CONFIG_MOD5272)
#define MEM_BASE	0x02000000
#define VBR_BASE	0x20000000	/* vectors in SRAM */
#endif
#if defined(CONFIG_M5208EVB)
#define MEM_BASE	0x40000000
#endif

#ifndef MEM_BASE
#define	MEM_BASE	0x00000000	/* memory base at address 0 */
#endif

/*
 *	The default location for the vectors is at the base of RAM.
 *	Some boards might like to use internal SRAM or something like
 *	that. If no board specific header defines an alternative then
 *	use the base of RAM.
 */
#ifndef	VBR_BASE
#define	VBR_BASE	MEM_BASE	/* vector address */
#endif

/*****************************************************************************/

/*
 *	Boards and platforms can do specific early hardware setup if
 *	they need to. Most don't need this, define away if not required.
 */
#ifndef PLATFORM_SETUP
#define	PLATFORM_SETUP
#endif

/*****************************************************************************/

.global	_start
.global _rambase
.global _ramvec
.global	_ramstart
.global	_ramend

/*****************************************************************************/

.data

/*
 *	During startup we store away the RAM setup. These are not in the
 *	bss, since their values are determined and written before the bss
 *	has been cleared.
 */
_rambase:
.long	0
_ramvec:
.long	0
_ramstart:
.long	0
_ramend:
.long	0

/*****************************************************************************/

.text

/*
 *	This is the codes first entry point. This is where it all
 *	begins...
 */

_start:
	nop					/* filler */
	movew	#0x2700, %sr			/* no interrupts */

	/*
	 *	Do any platform or board specific setup now. Most boards
	 *	don't need anything. Those exceptions are define this in
	 *	their board specific includes.
	 */
	PLATFORM_SETUP

	/*
	 *	Create basic memory configuration. Set VBR accordingly,
	 *	and size memory.
	 */
	movel	#VBR_BASE,%a7
	movec   %a7,%VBR			/* set vectors addr */
	movel	%a7,_ramvec

	movel	#MEM_BASE,%a7			/* mark the base of RAM */
	movel	%a7,_rambase

	GET_MEM_SIZE				/* macro code determines size */
	addl	%a7,%d0
	movel	%d0,_ramend			/* set end ram addr */

	/*
	 *	Now that we know what the memory is, lets enable cache
	 *	and get things moving. This is Coldfire CPU specific.
	 */
	CACHE_ENABLE				/* enable CPU cache */


#ifdef CONFIG_ROMFS_FS
	/*
	 *	Move ROM filesystem above bss :-)
	 */
	lea	_sbss,%a0			/* get start of bss */
	lea	_ebss,%a1			/* set up destination  */
	movel	%a0,%a2				/* copy of bss start */

	movel	8(%a0),%d0			/* get size of ROMFS */
	addql	#8,%d0				/* allow for rounding */
	andl	#0xfffffffc, %d0		/* whole words */

	addl	%d0,%a0				/* copy from end */
	addl	%d0,%a1				/* copy from end */
	movel	%a1,_ramstart			/* set start of ram */

_copy_romfs:
	movel	-(%a0),%d0			/* copy dword */
	movel	%d0,-(%a1)
	cmpl	%a0,%a2				/* check if at end */
	bne	_copy_romfs

#else /* CONFIG_ROMFS_FS */
	lea	_ebss,%a1
	movel	%a1,_ramstart
#endif /* CONFIG_ROMFS_FS */


	/*
	 *	Zero out the bss region.
	 */
	lea	_sbss,%a0			/* get start of bss */
	lea	_ebss,%a1			/* get end of bss */
	clrl	%d0				/* set value */
_clear_bss:
	movel	%d0,(%a0)+			/* clear each word */
	cmpl	%a0,%a1				/* check if at end */
	bne	_clear_bss

	/*
	 *	Load the current task pointer and stack.
	 */
	lea	init_thread_union,%a0
	lea	THREAD_SIZE(%a0),%sp

	/*
	 *	Assember start up done, start code proper.
	 */
	jsr	start_kernel			/* start Linux kernel */

_exit:
	jmp	_exit				/* should never get here */

/*****************************************************************************/
