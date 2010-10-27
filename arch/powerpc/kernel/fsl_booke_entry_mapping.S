
/* 1. Find the index of the entry we're executing in */
	bl	invstr				/* Find our address */
invstr:	mflr	r6				/* Make it accessible */
	mfmsr	r7
	rlwinm	r4,r7,27,31,31			/* extract MSR[IS] */
	mfspr	r7, SPRN_PID0
	slwi	r7,r7,16
	or	r7,r7,r4
	mtspr	SPRN_MAS6,r7
	tlbsx	0,r6				/* search MSR[IS], SPID=PID0 */
	mfspr	r7,SPRN_MAS1
	andis.	r7,r7,MAS1_VALID@h
	bne	match_TLB

	mfspr	r7,SPRN_MMUCFG
	rlwinm	r7,r7,21,28,31			/* extract MMUCFG[NPIDS] */
	cmpwi	r7,3
	bne	match_TLB			/* skip if NPIDS != 3 */

	mfspr	r7,SPRN_PID1
	slwi	r7,r7,16
	or	r7,r7,r4
	mtspr	SPRN_MAS6,r7
	tlbsx	0,r6				/* search MSR[IS], SPID=PID1 */
	mfspr	r7,SPRN_MAS1
	andis.	r7,r7,MAS1_VALID@h
	bne	match_TLB
	mfspr	r7, SPRN_PID2
	slwi	r7,r7,16
	or	r7,r7,r4
	mtspr	SPRN_MAS6,r7
	tlbsx	0,r6				/* Fall through, we had to match */

match_TLB:
	mfspr	r7,SPRN_MAS0
	rlwinm	r3,r7,16,20,31			/* Extract MAS0(Entry) */

	mfspr	r7,SPRN_MAS1			/* Insure IPROT set */
	oris	r7,r7,MAS1_IPROT@h
	mtspr	SPRN_MAS1,r7
	tlbwe

/* 2. Invalidate all entries except the entry we're executing in */
	mfspr	r9,SPRN_TLB1CFG
	andi.	r9,r9,0xfff
	li	r6,0				/* Set Entry counter to 0 */
1:	lis	r7,0x1000			/* Set MAS0(TLBSEL) = 1 */
	rlwimi	r7,r6,16,4,15			/* Setup MAS0 = TLBSEL | ESEL(r6) */
	mtspr	SPRN_MAS0,r7
	tlbre
	mfspr	r7,SPRN_MAS1
	rlwinm	r7,r7,0,2,31			/* Clear MAS1 Valid and IPROT */
	cmpw	r3,r6
	beq	skpinv				/* Dont update the current execution TLB */
	mtspr	SPRN_MAS1,r7
	tlbwe
	isync
skpinv:	addi	r6,r6,1				/* Increment */
	cmpw	r6,r9				/* Are we done? */
	bne	1b				/* If not, repeat */

	/* Invalidate TLB0 */
	li	r6,0x04
	tlbivax 0,r6
	TLBSYNC
	/* Invalidate TLB1 */
	li	r6,0x0c
	tlbivax 0,r6
	TLBSYNC

/* 3. Setup a temp mapping and jump to it */
	andi.	r5, r3, 0x1	/* Find an entry not used and is non-zero */
	addi	r5, r5, 0x1
	lis	r7,0x1000	/* Set MAS0(TLBSEL) = 1 */
	rlwimi	r7,r3,16,4,15	/* Setup MAS0 = TLBSEL | ESEL(r3) */
	mtspr	SPRN_MAS0,r7
	tlbre

	/* grab and fixup the RPN */
	mfspr	r6,SPRN_MAS1	/* extract MAS1[SIZE] */
	rlwinm	r6,r6,25,27,31
	li	r8,-1
	addi	r6,r6,10
	slw	r6,r8,r6	/* convert to mask */

	bl	1f		/* Find our address */
1:	mflr	r7

	mfspr	r8,SPRN_MAS3
#ifdef CONFIG_PHYS_64BIT
	mfspr	r23,SPRN_MAS7
#endif
	and	r8,r6,r8
	subfic	r9,r6,-4096
	and	r9,r9,r7

	or	r25,r8,r9
	ori	r8,r25,(MAS3_SX|MAS3_SW|MAS3_SR)

	/* Just modify the entry ID and EPN for the temp mapping */
	lis	r7,0x1000	/* Set MAS0(TLBSEL) = 1 */
	rlwimi	r7,r5,16,4,15	/* Setup MAS0 = TLBSEL | ESEL(r5) */
	mtspr	SPRN_MAS0,r7
	xori	r6,r4,1		/* Setup TMP mapping in the other Address space */
	slwi	r6,r6,12
	oris	r6,r6,(MAS1_VALID|MAS1_IPROT)@h
	ori	r6,r6,(MAS1_TSIZE(BOOK3E_PAGESZ_4K))@l
	mtspr	SPRN_MAS1,r6
	mfspr	r6,SPRN_MAS2
	li	r7,0		/* temp EPN = 0 */
	rlwimi	r7,r6,0,20,31
	mtspr	SPRN_MAS2,r7
	mtspr	SPRN_MAS3,r8
	tlbwe

	xori	r6,r4,1
	slwi	r6,r6,5		/* setup new context with other address space */
	bl	1f		/* Find our address */
1:	mflr	r9
	rlwimi	r7,r9,0,20,31
	addi	r7,r7,(2f - 1b)
	mtspr	SPRN_SRR0,r7
	mtspr	SPRN_SRR1,r6
	rfi
2:
/* 4. Clear out PIDs & Search info */
	li	r6,0
	mtspr   SPRN_MAS6,r6
	mtspr	SPRN_PID0,r6

	mfspr	r7,SPRN_MMUCFG
	rlwinm	r7,r7,21,28,31			/* extract MMUCFG[NPIDS] */
	cmpwi	r7,3
	bne	2f				/* skip if NPIDS != 3 */

	mtspr	SPRN_PID1,r6
	mtspr	SPRN_PID2,r6

/* 5. Invalidate mapping we started in */
2:
	lis	r7,0x1000	/* Set MAS0(TLBSEL) = 1 */
	rlwimi	r7,r3,16,4,15	/* Setup MAS0 = TLBSEL | ESEL(r3) */
	mtspr	SPRN_MAS0,r7
	tlbre
	mfspr	r6,SPRN_MAS1
	rlwinm	r6,r6,0,2,0	/* clear IPROT */
	mtspr	SPRN_MAS1,r6
	tlbwe
	/* Invalidate TLB1 */
	li	r9,0x0c
	tlbivax 0,r9
	TLBSYNC

/* The mapping only needs to be cache-coherent on SMP */
#ifdef CONFIG_SMP
#define M_IF_SMP	MAS2_M
#else
#define M_IF_SMP	0
#endif

#if defined(ENTRY_MAPPING_BOOT_SETUP)

/* 6. Setup KERNELBASE mapping in TLB1[0] */
	lis	r6,0x1000		/* Set MAS0(TLBSEL) = TLB1(1), ESEL = 0 */
	mtspr	SPRN_MAS0,r6
	lis	r6,(MAS1_VALID|MAS1_IPROT)@h
	ori	r6,r6,(MAS1_TSIZE(BOOK3E_PAGESZ_64M))@l
	mtspr	SPRN_MAS1,r6
	lis	r6,MAS2_VAL(PAGE_OFFSET, BOOK3E_PAGESZ_64M, M_IF_SMP)@h
	ori	r6,r6,MAS2_VAL(PAGE_OFFSET, BOOK3E_PAGESZ_64M, M_IF_SMP)@l
	mtspr	SPRN_MAS2,r6
	mtspr	SPRN_MAS3,r8
	tlbwe

/* 7. Jump to KERNELBASE mapping */
	lis	r6,(KERNELBASE & ~0xfff)@h
	ori	r6,r6,(KERNELBASE & ~0xfff)@l

#elif defined(ENTRY_MAPPING_KEXEC_SETUP)
/*
 * 6. Setup a 1:1 mapping in TLB1. Esel 0 is unsued, 1 or 2 contains the tmp
 * mapping so we start at 3. We setup 8 mappings, each 256MiB in size. This
 * will cover the first 2GiB of memory.
 */

	lis r10, (MAS1_VALID|MAS1_IPROT)@h
	ori r10,r10, (MAS1_TSIZE(BOOK3E_PAGESZ_256M))@l
	li  r11, 0
	li  r0, 8
	mtctr   r0

next_tlb_setup:
	addi	r0, r11, 3
	rlwinm  r0, r0, 16, 4, 15  // Compute esel
	rlwinm  r9, r11, 28, 0, 3   // Compute [ER]PN
	oris    r0, r0, (MAS0_TLBSEL(1))@h
	mtspr   SPRN_MAS0,r0
	mtspr   SPRN_MAS1,r10
	mtspr   SPRN_MAS2,r9
	ori r9, r9, (MAS3_SX|MAS3_SW|MAS3_SR)
	mtspr   SPRN_MAS3,r9
	tlbwe
	addi    r11, r11, 1
	bdnz+   next_tlb_setup

/* 7. Jump to our 1:1 mapping */
	mr	r6, r25
#else
	#error You need to specify the mapping or not use this at all.
#endif

	lis	r7,MSR_KERNEL@h
	ori	r7,r7,MSR_KERNEL@l
	bl	1f			/* Find our address */
1:	mflr	r9
	rlwimi	r6,r9,0,20,31
	addi	r6,r6,(2f - 1b)
	mtspr	SPRN_SRR0,r6
	mtspr	SPRN_SRR1,r7
	rfi				/* start execution out of TLB1[0] entry */

/* 8. Clear out the temp mapping */
2:	lis	r7,0x1000	/* Set MAS0(TLBSEL) = 1 */
	rlwimi	r7,r5,16,4,15	/* Setup MAS0 = TLBSEL | ESEL(r5) */
	mtspr	SPRN_MAS0,r7
	tlbre
	mfspr	r8,SPRN_MAS1
	rlwinm	r8,r8,0,2,0	/* clear IPROT */
	mtspr	SPRN_MAS1,r8
	tlbwe
	/* Invalidate TLB1 */
	li	r9,0x0c
	tlbivax 0,r9
	TLBSYNC
