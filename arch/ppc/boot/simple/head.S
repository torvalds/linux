/*
 * Initial board bringup code for many different boards.
 *
 * Author: Tom Rini
 *	   trini@mvista.com
 * Derived from arch/ppc/boot/prep/head.S (Cort Dougan, many others).
 *
 * 2001-2004 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/config.h>
#include <asm/reg.h>
#include <asm/cache.h>
#include <asm/ppc_asm.h>

	.text

/*
 *      Begin at some arbitrary location in RAM or Flash
 *	  Initialize core registers
 *	  Configure memory controller (Not executing from RAM)
 *	Move the boot code to the link address (8M)
 *	  Setup C stack
 *	  Initialize UART
 *      Decompress the kernel to 0x0
 *      Jump to the kernel entry
 *
 */

	.globl	start
start:
	bl	start_
#ifdef CONFIG_IBM_OPENBIOS
	/* The IBM "Tree" bootrom knows that the address of the bootrom
	 * read only structure is 4 bytes after _start.
	 */
	.long	0x62726f6d		# structure ID - "brom"
	.long	0x5f726f00		#              - "_ro\0"
	.long	1			# structure version
	.long	bootrom_cmdline		# address of *bootrom_cmdline
#endif

start_:
#ifdef CONFIG_FORCE
	/* We have some really bad firmware.  We must disable the L1
	 * icache/dcache now or the board won't boot.
	 */
	li	r4,0x0000
	isync
	mtspr	SPRN_HID0,r4
	sync
	isync
#endif

#if defined(CONFIG_MBX) || defined(CONFIG_RPX8260) || defined(CONFIG_PPC_PREP)
	mr	r29,r3	/* On the MBX860, r3 is the board info pointer.
			 * On the RPXSUPER, r3 points to the NVRAM
			 * configuration keys.
			 * On PReP, r3 is the pointer to the residual data.
			 */
#endif

#if defined(CONFIG_XILINX_VIRTEX_4_FX)
	/* PPC errata 213: only for Virtex-4 FX */
	mfccr0  0
	oris    0,0,0x50000000@h
	mtccr0  0
#endif

	mflr	r3	/* Save our actual starting address. */

	/* The following functions we call must not modify r3 or r4.....
	*/
#ifdef CONFIG_6xx
	/* On PReP we must look at the OpenFirmware pointer and sanity
	 * test it.  On other platforms, we disable the MMU right now
	 * and other bits.
	 */
#ifdef CONFIG_PPC_PREP
/*
 * Save the OF pointer to r25, but only if the entry point is in a sane
 * location; if not we store 0.  If there is no entry point, or it is
 * invalid, we establish the default MSR value immediately.  Otherwise,
 * we defer doing that, to allow OF functions to be called, until we
 * begin uncompressing the kernel.
 */
	lis	r8,0x0fff		/* r8 = 0x0fffffff */
	ori	r8,r8,0xffff

	subc	r8,r8,r5		/* r8 = (r5 <= r8) ? ~0 : 0 */
	subfe	r8,r8,r8
	nand	r8,r8,r8

	and.	r5,r5,r8		/* r5 will be cleared if (r5 > r8) */
	bne+	haveOF

	li	r8,MSR_IP|MSR_FP	/* Not OF: set MSR immediately */
  	mtmsr	r8
	isync
haveOF:
	mr	r25,r5
#else
	bl	disable_6xx_mmu
#endif
	bl	disable_6xx_l1cache

	CLEAR_CACHES
#endif

#ifdef CONFIG_8xx
	mfmsr	r8		/* Turn off interrupts */
	li	r9,0
	ori	r9,r9,MSR_EE
	andc	r8,r8,r9
	mtmsr	r8

	/* We do this because some boot roms don't initialize the
	 * processor correctly. Don't do this if you want to debug
	 * using a BDM device.
	 */
	li	r4,0		/* Zero DER to prevent FRZ */
	mtspr	SPRN_DER,r4
#endif

#if defined(CONFIG_MBX) || defined(CONFIG_RPX8260) || defined(CONFIG_PPC_PREP)
	mr	r4,r29	/* put the board info pointer where the relocate
			 * routine will find it
			 */
#endif

	/* Get the load address.
	*/
	subi	r3, r3, 4	/* Get the actual IP, not NIP */
	b	relocate

