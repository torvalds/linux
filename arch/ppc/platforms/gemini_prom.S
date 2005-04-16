/*
 *  arch/ppc/platforms/gemini_prom.S
 *
 *  Not really prom support code (yet), but sort of anti-prom code.  The current
 *  bootloader does a number of things it shouldn't and doesn't do things that it
 *  should.  The stuff in here is mainly a hodge-podge collection of setup code
 *  to get the board up and running.
 *    ---Dan
 */

#include <linux/config.h>
#include <asm/reg.h>
#include <asm/page.h>
#include <platforms/gemini.h>
#include <asm/ppc_asm.h>

/*
 *  On 750's the MMU is on when Linux is booted, so we need to clear out the
 *  bootloader's BAT settings, make sure we're in supervisor state (gotcha!),
 *  and turn off the MMU.
 *
 */

_GLOBAL(gemini_prom_init)
#ifdef CONFIG_SMP
	/* Since the MMU's on, get stuff in rom space that we'll need */
	lis	r4,GEMINI_CPUSTAT@h
	ori	r4,r4,GEMINI_CPUSTAT@l
	lbz	r5,0(r4)
	andi.	r5,r5,3
	mr	r24,r5		/* cpu # used later on */
#endif
	mfmsr	r4
	li	r3,MSR_PR	/* ensure supervisor! */
	ori	r3,r3,MSR_IR|MSR_DR
	andc	r4,r4,r3
	mtmsr	r4
	isync
#if 0
	/* zero out the bats now that the MMU is off */
prom_no_mmu:	
	li	r3,0
        mtspr   SPRN_IBAT0U,r3
        mtspr   SPRN_IBAT0L,r3
        mtspr   SPRN_IBAT1U,r3
        mtspr   SPRN_IBAT1L,r3
        mtspr   SPRN_IBAT2U,r3
        mtspr   SPRN_IBAT2L,r3
        mtspr   SPRN_IBAT3U,r3
        mtspr   SPRN_IBAT3L,r3

        mtspr   SPRN_DBAT0U,r3
        mtspr   SPRN_DBAT0L,r3
        mtspr   SPRN_DBAT1U,r3
        mtspr   SPRN_DBAT1L,r3
        mtspr   SPRN_DBAT2U,r3
	mtspr   SPRN_DBAT2L,r3
        mtspr   SPRN_DBAT3U,r3
        mtspr   SPRN_DBAT3L,r3
#endif

	/* the bootloader (as far as I'm currently aware) doesn't mess with page
	   tables, but since we're already here, might as well zap these, too */
	li	r4,0
	mtspr	SPRN_SDR1,r4

	li	r4,16
	mtctr	r4
	li	r3,0
	li	r4,0
3:	mtsrin	r3,r4
	addi	r3,r3,1
	bdnz	3b

#ifdef CONFIG_SMP
	/* The 750 book (and Mot/IBM support) says that this will "assist" snooping
	   when in SMP.  Not sure yet whether this should stay or leave... */
	mfspr	r4,SPRN_HID0
	ori	r4,r4,HID0_ABE
	mtspr	SPRN_HID0,r4
	sync
#endif /* CONFIG_SMP */
	blr

/*  apparently, SMon doesn't pay attention to HID0[SRST].  Disable the MMU and
    branch to 0xfff00100 */
_GLOBAL(_gemini_reboot)
	lis	r5,GEMINI_BOOT_INIT@h
	ori	r5,r5,GEMINI_BOOT_INIT@l
	li	r6,MSR_IP
	mtspr	SPRN_SRR0,r5
	mtspr	SPRN_SRR1,r6
	rfi
