/* SPDX-License-Identifier: GPL-2.0 */
/*
 * head.S: The initial boot code for the Sparc port of Linux.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1995,1999 Pete Zaitcev   (zaitcev@yahoo.com)
 * Copyright (C) 1996 Miguel de Icaza (miguel@nuclecu.unam.mx)
 * Copyright (C) 1997 Jakub Jelinek   (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 Michael A. Griffith (grif@acm.org)
 *
 * CompactPCI platform by Eric Brower, 1999.
 */

#include <linux/export.h>
#include <linux/version.h>
#include <linux/init.h>

#include <asm/head.h>
#include <asm/asi.h>
#include <asm/contregs.h>
#include <asm/ptrace.h>
#include <asm/psr.h>
#include <asm/page.h>
#include <asm/kdebug.h>
#include <asm/winmacro.h>
#include <asm/thread_info.h>	/* TI_UWINMASK */
#include <asm/errno.h>
#include <asm/pgtable.h>	/* PGDIR_SHIFT */

	.data
/* The following are used with the prom_vector node-ops to figure out
 * the cpu-type
 */
	.align 4
	.globl cputypval
cputypval:
	.asciz "sun4m"
	.ascii "     "

/* Tested on SS-5, SS-10 */
	.align 4
cputypvar:
	.asciz "compatible"

	.align 4

notsup:
	.asciz	"Sparc-Linux sun4/sun4c or MMU-less not supported\n\n"
	.align 4

sun4e_notsup:
        .asciz  "Sparc-Linux sun4e support does not exist\n\n"
	.align 4

/* The trap-table - located in the __HEAD section */
#include "ttable_32.S"

	.align PAGE_SIZE

/* This was the only reasonable way I could think of to properly align
 * these page-table data structures.
 */
	.globl empty_zero_page
empty_zero_page:	.skip PAGE_SIZE
EXPORT_SYMBOL(empty_zero_page)

	.global root_flags
	.global ram_flags
	.global root_dev
	.global sparc_ramdisk_image
	.global sparc_ramdisk_size

/* This stuff has to be in sync with SILO and other potential boot loaders
 * Fields should be kept upward compatible and whenever any change is made,
 * HdrS version should be incremented.
 */
	.ascii	"HdrS"
	.word	LINUX_VERSION_CODE
	.half	0x0203		/* HdrS version */
root_flags:
	.half	1
root_dev:
	.half	0
ram_flags:
	.half	0
sparc_ramdisk_image:
	.word	0
sparc_ramdisk_size:
	.word	0
	.word	reboot_command
	.word	0, 0, 0
	.word	_end

/* Cool, here we go. Pick up the romvec pointer in %o0 and stash it in
 * %g7 and at prom_vector_p. And also quickly check whether we are on
 * a v0, v2, or v3 prom.
 */
gokernel:
		/* Ok, it's nice to know, as early as possible, if we
		 * are already mapped where we expect to be in virtual
		 * memory.  The Solaris /boot elf format bootloader
		 * will peek into our elf header and load us where
		 * we want to be, otherwise we have to re-map.
		 *
		 * Some boot loaders don't place the jmp'rs address
		 * in %o7, so we do a pc-relative call to a local
		 * label, then see what %o7 has.
		 */

		mov	%o7, %g4		! Save %o7

		/* Jump to it, and pray... */
current_pc:
		call	1f
		 nop

1:
		mov	%o7, %g3

		tst	%o0
		be	no_sun4u_here
		 mov	%g4, %o7		/* Previous %o7. */

		mov	%o0, %l0		! stash away romvec
		mov	%o0, %g7		! put it here too
		mov	%o1, %l1		! stash away debug_vec too

		/* Ok, let's check out our run time program counter. */
		set	current_pc, %g5
		cmp	%g3, %g5
		be	already_mapped
		 nop

		/* %l6 will hold the offset we have to subtract
		 * from absolute symbols in order to access areas
		 * in our own image.  If already mapped this is
		 * just plain zero, else it is KERNBASE.
		 */
		set	KERNBASE, %l6
		b	copy_prom_lvl14
		 nop

already_mapped:
		mov	0, %l6

		/* Copy over the Prom's level 14 clock handler. */
copy_prom_lvl14:
#if 1
		/* DJHR
		 * preserve our linked/calculated instructions
		 */
		set	lvl14_save, %g1
		set	t_irq14, %g3
		sub	%g1, %l6, %g1		! translate to physical
		sub	%g3, %l6, %g3		! translate to physical
		ldd	[%g3], %g4
		std	%g4, [%g1]
		ldd	[%g3+8], %g4
		std	%g4, [%g1+8]
#endif
		rd	%tbr, %g1
		andn	%g1, 0xfff, %g1		! proms trap table base
		or	%g0, (0x1e<<4), %g2	! offset to lvl14 intr
		or	%g1, %g2, %g2
		set	t_irq14, %g3
		sub	%g3, %l6, %g3
		ldd	[%g2], %g4
		std	%g4, [%g3]
		ldd	[%g2 + 0x8], %g4
		std	%g4, [%g3 + 0x8]	! Copy proms handler

/* DON'T TOUCH %l0 thru %l5 in these remapping routines,
 * we need their values afterwards!
 */

		/* Now check whether we are already mapped, if we
		 * are we can skip all this garbage coming up.
		 */
copy_prom_done:
		cmp	%l6, 0
		be	go_to_highmem		! this will be a nop then
		 nop

		/* Validate that we are in fact running on an
		 * SRMMU based cpu.
		 */
		set	0x4000, %g6
		cmp	%g7, %g6
		bne	not_a_sun4
		 nop

halt_notsup:
		ld	[%g7 + 0x68], %o1
		set	notsup, %o0
		sub	%o0, %l6, %o0
		call	%o1
		 nop
		ba	halt_me
		 nop

not_a_sun4:
		/* It looks like this is a machine we support.
		 * Now find out what MMU we are dealing with
		 * LEON - identified by the psr.impl field
		 * Viking - identified by the psr.impl field
		 * In all other cases a sun4m srmmu.
		 * We check that the MMU is enabled in all cases.
		 */

		/* Check if this is a LEON CPU */
		rd	%psr, %g3
		srl	%g3, PSR_IMPL_SHIFT, %g3
		and	%g3, PSR_IMPL_SHIFTED_MASK, %g3
		cmp	%g3, PSR_IMPL_LEON
		be	leon_remap		/* It is a LEON - jump */
		 nop

		/* Sanity-check, is MMU enabled */
		lda	[%g0] ASI_M_MMUREGS, %g1
		andcc	%g1, 1, %g0
		be	halt_notsup
		 nop

		/* Check for a viking (TI) module. */
		cmp	%g3, PSR_IMPL_TI
		bne	srmmu_not_viking
		 nop

		/* Figure out what kind of viking we are on.
		 * We need to know if we have to play with the
		 * AC bit and disable traps or not.
		 */

		/* I've only seen MicroSparc's on SparcClassics with this
		 * bit set.
		 */
		set	0x800, %g2
		lda	[%g0] ASI_M_MMUREGS, %g3	! peek in the control reg
		and	%g2, %g3, %g3
		subcc	%g3, 0x0, %g0
		bnz	srmmu_not_viking			! is in mbus mode
		 nop

		rd	%psr, %g3			! DO NOT TOUCH %g3
		andn	%g3, PSR_ET, %g2
		wr	%g2, 0x0, %psr
		WRITE_PAUSE

		/* Get context table pointer, then convert to
		 * a physical address, which is 36 bits.
		 */
		set	AC_M_CTPR, %g4
		lda	[%g4] ASI_M_MMUREGS, %g4
		sll	%g4, 0x4, %g4			! We use this below
							! DO NOT TOUCH %g4

		/* Set the AC bit in the Viking's MMU control reg. */
		lda	[%g0] ASI_M_MMUREGS, %g5	! DO NOT TOUCH %g5
		set	0x8000, %g6			! AC bit mask
		or	%g5, %g6, %g6			! Or it in...
		sta	%g6, [%g0] ASI_M_MMUREGS	! Close your eyes...

		/* Grrr, why does it seem like every other load/store
		 * on the sun4m is in some ASI space...
		 * Fine with me, let's get the pointer to the level 1
		 * page table directory and fetch its entry.
		 */
		lda	[%g4] ASI_M_BYPASS, %o1		! This is a level 1 ptr
		srl	%o1, 0x4, %o1			! Clear low 4 bits
		sll	%o1, 0x8, %o1			! Make physical

		/* Ok, pull in the PTD. */
		lda	[%o1] ASI_M_BYPASS, %o2		! This is the 0x0 16MB pgd

		/* Calculate to KERNBASE entry. */
		add	%o1, KERNBASE >> (PGDIR_SHIFT - 2), %o3

		/* Poke the entry into the calculated address. */
		sta	%o2, [%o3] ASI_M_BYPASS

		/* I don't get it Sun, if you engineered all these
		 * boot loaders and the PROM (thank you for the debugging
		 * features btw) why did you not have them load kernel
		 * images up in high address space, since this is necessary
		 * for ABI compliance anyways?  Does this low-mapping provide
		 * enhanced interoperability?
		 *
		 * "The PROM is the computer."
		 */

		/* Ok, restore the MMU control register we saved in %g5 */
		sta	%g5, [%g0] ASI_M_MMUREGS	! POW... ouch

		/* Turn traps back on.  We saved it in %g3 earlier. */
		wr	%g3, 0x0, %psr			! tick tock, tick tock

		/* Now we burn precious CPU cycles due to bad engineering. */
		WRITE_PAUSE

		/* Wow, all that just to move a 32-bit value from one
		 * place to another...  Jump to high memory.
		 */
		b	go_to_highmem
		 nop

srmmu_not_viking:
		/* This works on viking's in Mbus mode and all
		 * other MBUS modules.  It is virtually the same as
		 * the above madness sans turning traps off and flipping
		 * the AC bit.
		 */
		set	AC_M_CTPR, %g1
		lda	[%g1] ASI_M_MMUREGS, %g1	! get ctx table ptr
		sll	%g1, 0x4, %g1			! make physical addr
		lda	[%g1] ASI_M_BYPASS, %g1		! ptr to level 1 pg_table
		srl	%g1, 0x4, %g1
		sll	%g1, 0x8, %g1			! make phys addr for l1 tbl

		lda	[%g1] ASI_M_BYPASS, %g2		! get level1 entry for 0x0
		add	%g1, KERNBASE >> (PGDIR_SHIFT - 2), %g3
		sta	%g2, [%g3] ASI_M_BYPASS		! place at KERNBASE entry
		b	go_to_highmem
		 nop					! wheee....


leon_remap:
		/* Sanity-check, is MMU enabled */
		lda	[%g0] ASI_LEON_MMUREGS, %g1
		andcc	%g1, 1, %g0
		be	halt_notsup
		 nop

		/* Same code as in the srmmu_not_viking case,
		 * with the LEON ASI for mmuregs
		 */
		set	AC_M_CTPR, %g1
		lda	[%g1] ASI_LEON_MMUREGS, %g1	! get ctx table ptr
		sll	%g1, 0x4, %g1			! make physical addr
		lda	[%g1] ASI_M_BYPASS, %g1		! ptr to level 1 pg_table
		srl	%g1, 0x4, %g1
		sll	%g1, 0x8, %g1			! make phys addr for l1 tbl

		lda	[%g1] ASI_M_BYPASS, %g2		! get level1 entry for 0x0
		add	%g1, KERNBASE >> (PGDIR_SHIFT - 2), %g3
		sta	%g2, [%g3] ASI_M_BYPASS		! place at KERNBASE entry
		b	go_to_highmem
		 nop					! wheee....

/* Now do a non-relative jump so that PC is in high-memory */
go_to_highmem:
		set	execute_in_high_mem, %g1
		jmpl	%g1, %g0
		 nop

/* The code above should be at beginning and we have to take care about
 * short jumps, as branching to .init.text section from .text is usually
 * impossible */
		__INIT
/* Acquire boot time privileged register values, this will help debugging.
 * I figure out and store nwindows and nwindowsm1 later on.
 */
execute_in_high_mem:
		mov	%l0, %o0		! put back romvec
		mov	%l1, %o1		! and debug_vec

		sethi	%hi(prom_vector_p), %g1
		st	%o0, [%g1 + %lo(prom_vector_p)]

		sethi	%hi(linux_dbvec), %g1
		st	%o1, [%g1 + %lo(linux_dbvec)]

		/* Get the machine type via the romvec
		 * getprops node operation
		 */
		add	%g7, 0x1c, %l1
		ld	[%l1], %l0
		ld	[%l0], %l0
		call	%l0
		 or	%g0, %g0, %o0		! next_node(0) = first_node
		or	%o0, %g0, %g6

		sethi	%hi(cputypvar), %o1	! First node has cpu-arch
		or	%o1, %lo(cputypvar), %o1
		sethi	%hi(cputypval), %o2	! information, the string
		or	%o2, %lo(cputypval), %o2
		ld	[%l1], %l0		! 'compatible' tells
		ld	[%l0 + 0xc], %l0	! that we want 'sun4x' where
		call	%l0			! x is one of 'm', 'd' or 'e'.
		 nop				! %o2 holds pointer
						! to a buf where above string
						! will get stored by the prom.


		/* Check value of "compatible" property.
		 * "value" => "model"
		 * leon => sparc_leon
		 * sun4m => sun4m
		 * sun4s => sun4m
		 * sun4d => sun4d
		 * sun4e => "no_sun4e_here"
		 * '*'   => "no_sun4u_here"
		 * Check single letters only
		 */

		set	cputypval, %o2
		/* If cputypval[0] == 'l' (lower case letter L) this is leon */
		ldub	[%o2], %l1
		cmp	%l1, 'l'
		be	leon_init
		 nop

		/* Check cputypval[4] to find the sun model */
		ldub	[%o2 + 0x4], %l1

		cmp	%l1, 'm'
		be	sun4m_init
		 cmp	%l1, 's'
		be	sun4m_init
		 cmp	%l1, 'd'
		be	sun4d_init
		 cmp	%l1, 'e'
		be	no_sun4e_here		! Could be a sun4e.
		 nop
		b	no_sun4u_here		! AIEEE, a V9 sun4u... Get our BIG BROTHER kernel :))
		 nop

leon_init:
		/* LEON CPU - set boot_cpu_id */
		sethi	%hi(boot_cpu_id), %g2	! boot-cpu index

#ifdef CONFIG_SMP
		ldub	[%g2 + %lo(boot_cpu_id)], %g1
		cmp	%g1, 0xff		! unset means first CPU
		bne	leon_smp_cpu_startup	! continue only with master
		 nop
#endif
		/* Get CPU-ID from most significant 4-bit of ASR17 */
		rd     %asr17, %g1
		srl    %g1, 28, %g1

		/* Update boot_cpu_id only on boot cpu */
		stub	%g1, [%g2 + %lo(boot_cpu_id)]

		ba continue_boot
		 nop

/* CPUID in bootbus can be found at PA 0xff0140000 */
#define SUN4D_BOOTBUS_CPUID     0xf0140000

sun4d_init:
	/* Need to patch call to handler_irq */
	set	patch_handler_irq, %g4
	set	sun4d_handler_irq, %g5
	sethi	%hi(0x40000000), %g3		! call
	sub	%g5, %g4, %g5
	srl	%g5, 2, %g5
	or	%g5, %g3, %g5
	st	%g5, [%g4]

#ifdef CONFIG_SMP
	/* Get our CPU id out of bootbus */
	set     SUN4D_BOOTBUS_CPUID, %g3
	lduba   [%g3] ASI_M_CTL, %g3
	and     %g3, 0xf8, %g3
	srl     %g3, 3, %g4
	sta     %g4, [%g0] ASI_M_VIKING_TMP1
	sethi	%hi(boot_cpu_id), %g5
	stb	%g4, [%g5 + %lo(boot_cpu_id)]
#endif

	/* Fall through to sun4m_init */

sun4m_init:
/* Ok, the PROM could have done funny things and apple cider could still
 * be sitting in the fault status/address registers.  Read them all to
 * clear them so we don't get magic faults later on.
 */
/* This sucks, apparently this makes Vikings call prom panic, will fix later */
2:
		rd	%psr, %o1
		srl	%o1, PSR_IMPL_SHIFT, %o1	! Get a type of the CPU

		subcc	%o1, PSR_IMPL_TI, %g0		! TI: Viking or MicroSPARC
		be	continue_boot
		 nop

		set	AC_M_SFSR, %o0
		lda	[%o0] ASI_M_MMUREGS, %g0
		set	AC_M_SFAR, %o0
		lda	[%o0] ASI_M_MMUREGS, %g0

		/* Fujitsu MicroSPARC-II has no asynchronous flavors of FARs */
		subcc	%o1, 0, %g0
		be	continue_boot
		 nop

		set	AC_M_AFSR, %o0
		lda	[%o0] ASI_M_MMUREGS, %g0
		set	AC_M_AFAR, %o0
		lda	[%o0] ASI_M_MMUREGS, %g0
		 nop


continue_boot:

/* Aieee, now set PC and nPC, enable traps, give ourselves a stack and it's
 * show-time!
 */
		/* Turn on Supervisor, EnableFloating, and all the PIL bits.
		 * Also puts us in register window zero with traps off.
		 */
		set	(PSR_PS | PSR_S | PSR_PIL | PSR_EF), %g2
		wr	%g2, 0x0, %psr
		WRITE_PAUSE

		/* I want a kernel stack NOW! */
		set	init_thread_union, %g1
		set	(THREAD_SIZE - STACKFRAME_SZ - TRACEREG_SZ), %g2
		add	%g1, %g2, %sp
		mov	0, %fp			/* And for good luck */

		/* Zero out our BSS section. */
		set	__bss_start , %o0	! First address of BSS
		set	_end , %o1		! Last address of BSS
		add	%o0, 0x1, %o0
1:
		stb	%g0, [%o0]
		subcc	%o0, %o1, %g0
		bl	1b
		 add	%o0, 0x1, %o0

		/* If boot_cpu_id has not been setup by machine specific
		 * init-code above we default it to zero.
		 */
		sethi	%hi(boot_cpu_id), %g2
		ldub	[%g2 + %lo(boot_cpu_id)], %g3
		cmp	%g3, 0xff
		bne	1f
		 nop
		mov	%g0, %g3
		stub	%g3, [%g2 + %lo(boot_cpu_id)]

1:		sll	%g3, 2, %g3

		/* Initialize the uwinmask value for init task just in case.
		 * But first make current_set[boot_cpu_id] point to something useful.
		 */
		set	init_thread_union, %g6
		set	current_set, %g2
#ifdef CONFIG_SMP
		st	%g6, [%g2]
		add	%g2, %g3, %g2
#endif
		st	%g6, [%g2]

		st	%g0, [%g6 + TI_UWINMASK]

/* Compute NWINDOWS and stash it away. Now uses %wim trick explained
 * in the V8 manual. Ok, this method seems to work, Sparc is cool...
 * No, it doesn't work, have to play the save/readCWP/restore trick.
 */

		wr	%g0, 0x0, %wim			! so we do not get a trap
		WRITE_PAUSE

		save

		rd	%psr, %g3

		restore

		and	%g3, 0x1f, %g3
		add	%g3, 0x1, %g3

		mov	2, %g1
		wr	%g1, 0x0, %wim			! make window 1 invalid
		WRITE_PAUSE

		cmp	%g3, 0x7
		bne	2f
		 nop

		/* Adjust our window handling routines to
		 * do things correctly on 7 window Sparcs.
		 */

#define		PATCH_INSN(src, dest) \
		set	src, %g5; \
		set	dest, %g2; \
		ld	[%g5], %g4; \
		st	%g4, [%g2];

		/* Patch for window spills... */
		PATCH_INSN(spnwin_patch1_7win, spnwin_patch1)
		PATCH_INSN(spnwin_patch2_7win, spnwin_patch2)
		PATCH_INSN(spnwin_patch3_7win, spnwin_patch3)

		/* Patch for window fills... */
		PATCH_INSN(fnwin_patch1_7win, fnwin_patch1)
		PATCH_INSN(fnwin_patch2_7win, fnwin_patch2)

		/* Patch for trap entry setup... */
		PATCH_INSN(tsetup_7win_patch1, tsetup_patch1)
		PATCH_INSN(tsetup_7win_patch2, tsetup_patch2)
		PATCH_INSN(tsetup_7win_patch3, tsetup_patch3)
		PATCH_INSN(tsetup_7win_patch4, tsetup_patch4)
		PATCH_INSN(tsetup_7win_patch5, tsetup_patch5)
		PATCH_INSN(tsetup_7win_patch6, tsetup_patch6)

		/* Patch for returning from traps... */
		PATCH_INSN(rtrap_7win_patch1, rtrap_patch1)
		PATCH_INSN(rtrap_7win_patch2, rtrap_patch2)
		PATCH_INSN(rtrap_7win_patch3, rtrap_patch3)
		PATCH_INSN(rtrap_7win_patch4, rtrap_patch4)
		PATCH_INSN(rtrap_7win_patch5, rtrap_patch5)

		/* Patch for killing user windows from the register file. */
		PATCH_INSN(kuw_patch1_7win, kuw_patch1)

		/* Now patch the kernel window flush sequences.
		 * This saves 2 traps on every switch and fork.
		 */
		set	0x01000000, %g4
		set	flush_patch_one, %g5
		st	%g4, [%g5 + 0x18]
		st	%g4, [%g5 + 0x1c]
		set	flush_patch_two, %g5
		st	%g4, [%g5 + 0x18]
		st	%g4, [%g5 + 0x1c]
		set	flush_patch_three, %g5
		st	%g4, [%g5 + 0x18]
		st	%g4, [%g5 + 0x1c]
		set	flush_patch_four, %g5
		st	%g4, [%g5 + 0x18]
		st	%g4, [%g5 + 0x1c]
		set	flush_patch_exception, %g5
		st	%g4, [%g5 + 0x18]
		st	%g4, [%g5 + 0x1c]
		set	flush_patch_switch, %g5
		st	%g4, [%g5 + 0x18]
		st	%g4, [%g5 + 0x1c]

2:
		sethi	%hi(nwindows), %g4
		st	%g3, [%g4 + %lo(nwindows)]	! store final value
		sub	%g3, 0x1, %g3
		sethi	%hi(nwindowsm1), %g4
		st	%g3, [%g4 + %lo(nwindowsm1)]

		/* Here we go, start using Linux's trap table... */
		set	trapbase, %g3
		wr	%g3, 0x0, %tbr
		WRITE_PAUSE

		/* Finally, turn on traps so that we can call c-code. */
		rd	%psr, %g3
		wr	%g3, 0x0, %psr
		WRITE_PAUSE

		wr	%g3, PSR_ET, %psr
		WRITE_PAUSE

		/* Call sparc32_start_kernel(struct linux_romvec *rp) */
		sethi	%hi(prom_vector_p), %g5
		ld	[%g5 + %lo(prom_vector_p)], %o0
		call	sparc32_start_kernel
		 nop

		/* We should not get here. */
		call	halt_me
		 nop

no_sun4e_here:
		ld	[%g7 + 0x68], %o1
		set	sun4e_notsup, %o0
		call	%o1
		 nop
		b	halt_me
		 nop

		__INITDATA

sun4u_1:
		.asciz "finddevice"
		.align	4
sun4u_2:
		.asciz "/chosen"
		.align	4
sun4u_3:
		.asciz "getprop"
		.align	4
sun4u_4:
		.asciz "stdout"
		.align	4
sun4u_5:
		.asciz "write"
		.align	4
sun4u_6:
		.asciz  "\n\rOn sun4u you have to use sparc64 kernel\n\rand not a sparc32 version\n\r\n\r"
sun4u_6e:
		.align	4
sun4u_7:
		.asciz "exit"
		.align	8
sun4u_a1:
		.word	0, sun4u_1, 0, 1, 0, 1, 0, sun4u_2, 0
sun4u_r1:
		.word	0
sun4u_a2:
		.word	0, sun4u_3, 0, 4, 0, 1, 0
sun4u_i2:
		.word	0, 0, sun4u_4, 0, sun4u_1, 0, 8, 0
sun4u_r2:
		.word	0
sun4u_a3:
		.word	0, sun4u_5, 0, 3, 0, 1, 0
sun4u_i3:
		.word	0, 0, sun4u_6, 0, sun4u_6e - sun4u_6 - 1, 0
sun4u_r3:
		.word	0
sun4u_a4:
		.word	0, sun4u_7, 0, 0, 0, 0
sun4u_r4:

		__INIT
no_sun4u_here:
		set	sun4u_a1, %o0
		set	current_pc, %l2
		cmp	%l2, %g3
		be	1f
		 mov	%o4, %l0
		sub	%g3, %l2, %l6
		add	%o0, %l6, %o0
		mov	%o0, %l4
		mov	sun4u_r4 - sun4u_a1, %l3
		ld	[%l4], %l5
2:
		add	%l4, 4, %l4
		cmp	%l5, %l2
		add	%l5, %l6, %l5
		bgeu,a	3f
		 st	%l5, [%l4 - 4]
3:
		subcc	%l3, 4, %l3
		bne	2b
		 ld	[%l4], %l5
1:
		call	%l0
		 mov	%o0, %l1

		ld	[%l1 + (sun4u_r1 - sun4u_a1)], %o1
		add	%l1, (sun4u_a2 - sun4u_a1), %o0
		call	%l0
		 st	%o1, [%o0 + (sun4u_i2 - sun4u_a2)]

		ld	[%l1 + (sun4u_1 - sun4u_a1)], %o1
		add	%l1, (sun4u_a3 - sun4u_a1), %o0
		call	%l0
		st	%o1, [%o0 + (sun4u_i3 - sun4u_a3)]

		call	%l0
		 add	%l1, (sun4u_a4 - sun4u_a1), %o0

		/* Not reached */
halt_me:
		ld	[%g7 + 0x74], %o0
		call	%o0			! Get us out of here...
		 nop				! Apparently Solaris is better.

/* Ok, now we continue in the .data/.text sections */

	.data
	.align 4

/*
 * Fill up the prom vector, note in particular the kind first element,
 * no joke. I don't need all of them in here as the entire prom vector
 * gets initialized in c-code so all routines can use it.
 */

prom_vector_p:
		.word 0

/* We calculate the following at boot time, window fills/spills and trap entry
 * code uses these to keep track of the register windows.
 */

	.align 4
	.globl	nwindows
	.globl	nwindowsm1
nwindows:
	.word	8
nwindowsm1:
	.word	7

/* Boot time debugger vector value.  We need this later on. */

	.align 4
	.globl	linux_dbvec
linux_dbvec:
	.word	0
	.word	0

	.align 8

	.globl	lvl14_save
lvl14_save:
	.word	0
	.word	0
	.word	0
	.word	0
	.word	t_irq14
