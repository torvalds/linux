/*
 *  A2 specific assembly support code
 *
 *  Copyright 2009 Ben Herrenschmidt, IBM Corp.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <asm/asm-offsets.h>
#include <asm/ppc_asm.h>
#include <asm/ppc-opcode.h>
#include <asm/processor.h>
#include <asm/reg_a2.h>
#include <asm/reg.h>
#include <asm/thread_info.h>

/*
 * Disable thdid and class fields in ERATs to bump PID to full 14 bits capacity.
 * This also prevents external LPID accesses but that isn't a problem when not a
 * guest. Under PV, this setting will be ignored and MMUCR will return the right
 * number of PID bits we can use.
 */
#define MMUCR1_EXTEND_PID \
	(MMUCR1_ICTID | MMUCR1_ITTID | MMUCR1_DCTID | \
	 MMUCR1_DTTID | MMUCR1_DCCD)

/*
 * Use extended PIDs if enabled.
 * Don't clear the ERATs on context sync events and enable I & D LRU.
 * Enable ERAT back invalidate when tlbwe overwrites an entry.
 */
#define INITIAL_MMUCR1 \
	(MMUCR1_EXTEND_PID | MMUCR1_CSINV_NEVER | MMUCR1_IRRE | \
	 MMUCR1_DRRE | MMUCR1_TLBWE_BINV)

_GLOBAL(__setup_cpu_a2)
	/* Some of these are actually thread local and some are
	 * core local but doing it always won't hurt
	 */

#ifdef CONFIG_PPC_ICSWX
	/* Make sure ACOP starts out as zero */
	li	r3,0
	mtspr   SPRN_ACOP,r3

	/* Skip the following if we are in Guest mode */
	mfmsr	r3
	andis.	r0,r3,MSR_GS@h
	bne	_icswx_skip_guest

	/* Enable icswx instruction */
	mfspr   r3,SPRN_A2_CCR2
	ori     r3,r3,A2_CCR2_ENABLE_ICSWX
	mtspr   SPRN_A2_CCR2,r3

	/* Unmask all CTs in HACOP */
	li      r3,-1
	mtspr   SPRN_HACOP,r3
_icswx_skip_guest:
#endif /* CONFIG_PPC_ICSWX */

	/* Enable doorbell */
	mfspr   r3,SPRN_A2_CCR2
	oris     r3,r3,A2_CCR2_ENABLE_PC@h
	mtspr   SPRN_A2_CCR2,r3
	isync

	/* Setup CCR0 to disable power saving for now as it's busted
	 * in the current implementations. Setup CCR1 to wake on
	 * interrupts normally (we write the default value but who
	 * knows what FW may have clobbered...)
	 */
	li	r3,0
	mtspr	SPRN_A2_CCR0, r3
	LOAD_REG_IMMEDIATE(r3,0x0f0f0f0f)
	mtspr	SPRN_A2_CCR1, r3

	/* Initialise MMUCR1 */
	lis	r3,INITIAL_MMUCR1@h
	ori	r3,r3,INITIAL_MMUCR1@l
	mtspr	SPRN_MMUCR1,r3

	/* Set MMUCR2 to enable 4K, 64K, 1M, 16M and 1G pages */
	LOAD_REG_IMMEDIATE(r3, 0x000a7531)
	mtspr	SPRN_MMUCR2,r3

	/* Set MMUCR3 to write all thids bit to the TLB */
	LOAD_REG_IMMEDIATE(r3, 0x0000000f)
	mtspr	SPRN_MMUCR3,r3

	/* Don't do ERAT stuff if running guest mode */
	mfmsr	r3
	andis.	r0,r3,MSR_GS@h
	bne	1f

	/* Now set the I-ERAT watermark to 15 */
	lis	r4,(MMUCR0_TLBSEL_I|MMUCR0_ECL)@h
	mtspr	SPRN_MMUCR0, r4
	li	r4,A2_IERAT_SIZE-1
	PPC_ERATWE(R4,R4,3)

	/* Now set the D-ERAT watermark to 31 */
	lis	r4,(MMUCR0_TLBSEL_D|MMUCR0_ECL)@h
	mtspr	SPRN_MMUCR0, r4
	li	r4,A2_DERAT_SIZE-1
	PPC_ERATWE(R4,R4,3)

	/* And invalidate the beast just in case. That won't get rid of
	 * a bolted entry though it will be in LRU and so will go away eventually
	 * but let's not bother for now
	 */
	PPC_ERATILX(0,R0,R0)
1:
	blr

_GLOBAL(__restore_cpu_a2)
	b	__setup_cpu_a2
