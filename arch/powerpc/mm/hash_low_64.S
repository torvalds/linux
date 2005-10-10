/*
 * ppc64 MMU hashtable management routines
 *
 * (c) Copyright IBM Corp. 2003
 *
 * Maintained by: Benjamin Herrenschmidt
 *                <benh@kernel.crashing.org>
 *
 * This file is covered by the GNU Public Licence v2 as
 * described in the kernel's COPYING file.
 */

#include <asm/reg.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/page.h>
#include <asm/types.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/cputable.h>

	.text

/*
 * Stackframe:
 *		
 *         +-> Back chain			(SP + 256)
 *         |   General register save area	(SP + 112)
 *         |   Parameter save area		(SP + 48)
 *         |   TOC save area			(SP + 40)
 *         |   link editor doubleword		(SP + 32)
 *         |   compiler doubleword		(SP + 24)
 *         |   LR save area			(SP + 16)
 *         |   CR save area			(SP + 8)
 * SP ---> +-- Back chain			(SP + 0)
 */
#define STACKFRAMESIZE	256

/* Save parameters offsets */
#define STK_PARM(i)	(STACKFRAMESIZE + 48 + ((i)-3)*8)

/* Save non-volatile offsets */
#define STK_REG(i)	(112 + ((i)-14)*8)

/*
 * _hash_page(unsigned long ea, unsigned long access, unsigned long vsid,
 *		pte_t *ptep, unsigned long trap, int local)
 *
 * Adds a page to the hash table. This is the non-LPAR version for now
 */

_GLOBAL(__hash_page)
	mflr	r0
	std	r0,16(r1)
	stdu	r1,-STACKFRAMESIZE(r1)
	/* Save all params that we need after a function call */
	std	r6,STK_PARM(r6)(r1)
	std	r8,STK_PARM(r8)(r1)
	
	/* Add _PAGE_PRESENT to access */
	ori	r4,r4,_PAGE_PRESENT

	/* Save non-volatile registers.
	 * r31 will hold "old PTE"
	 * r30 is "new PTE"
	 * r29 is "va"
	 * r28 is a hash value
	 * r27 is hashtab mask (maybe dynamic patched instead ?)
	 */
	std	r27,STK_REG(r27)(r1)
	std	r28,STK_REG(r28)(r1)
	std	r29,STK_REG(r29)(r1)
	std	r30,STK_REG(r30)(r1)
	std	r31,STK_REG(r31)(r1)
	
	/* Step 1:
	 *
	 * Check permissions, atomically mark the linux PTE busy
	 * and hashed.
	 */ 
1:
	ldarx	r31,0,r6
	/* Check access rights (access & ~(pte_val(*ptep))) */
	andc.	r0,r4,r31
	bne-	htab_wrong_access
	/* Check if PTE is busy */
	andi.	r0,r31,_PAGE_BUSY
	/* If so, just bail out and refault if needed. Someone else
	 * is changing this PTE anyway and might hash it.
	 */
	bne-	bail_ok
	/* Prepare new PTE value (turn access RW into DIRTY, then
	 * add BUSY,HASHPTE and ACCESSED)
	 */
	rlwinm	r30,r4,32-9+7,31-7,31-7	/* _PAGE_RW -> _PAGE_DIRTY */
	or	r30,r30,r31
	ori	r30,r30,_PAGE_BUSY | _PAGE_ACCESSED | _PAGE_HASHPTE
	/* Write the linux PTE atomically (setting busy) */
	stdcx.	r30,0,r6
	bne-	1b
	isync

	/* Step 2:
	 *
	 * Insert/Update the HPTE in the hash table. At this point,
	 * r4 (access) is re-useable, we use it for the new HPTE flags
	 */

	/* Calc va and put it in r29 */
	rldicr	r29,r5,28,63-28
	rldicl	r3,r3,0,36
	or	r29,r3,r29

	/* Calculate hash value for primary slot and store it in r28 */
	rldicl	r5,r5,0,25		/* vsid & 0x0000007fffffffff */
	rldicl	r0,r3,64-12,48		/* (ea >> 12) & 0xffff */
	xor	r28,r5,r0

	/* Convert linux PTE bits into HW equivalents */
	andi.	r3,r30,0x1fe		/* Get basic set of flags */
	xori	r3,r3,HW_NO_EXEC	/* _PAGE_EXEC -> NOEXEC */
	rlwinm	r0,r30,32-9+1,30,30	/* _PAGE_RW -> _PAGE_USER (r0) */
	rlwinm	r4,r30,32-7+1,30,30	/* _PAGE_DIRTY -> _PAGE_USER (r4) */
	and	r0,r0,r4		/* _PAGE_RW & _PAGE_DIRTY -> r0 bit 30 */
	andc	r0,r30,r0		/* r0 = pte & ~r0 */
	rlwimi	r3,r0,32-1,31,31	/* Insert result into PP lsb */

	/* We eventually do the icache sync here (maybe inline that
	 * code rather than call a C function...) 
	 */
BEGIN_FTR_SECTION
	mr	r4,r30
	mr	r5,r7
	bl	.hash_page_do_lazy_icache
END_FTR_SECTION(CPU_FTR_NOEXECUTE|CPU_FTR_COHERENT_ICACHE, CPU_FTR_NOEXECUTE)

	/* At this point, r3 contains new PP bits, save them in
	 * place of "access" in the param area (sic)
	 */
	std	r3,STK_PARM(r4)(r1)

	/* Get htab_hash_mask */
	ld	r4,htab_hash_mask@got(2)
	ld	r27,0(r4)	/* htab_hash_mask -> r27 */

	/* Check if we may already be in the hashtable, in this case, we
	 * go to out-of-line code to try to modify the HPTE
	 */
	andi.	r0,r31,_PAGE_HASHPTE
	bne	htab_modify_pte

htab_insert_pte:
	/* Clear hpte bits in new pte (we also clear BUSY btw) and
	 * add _PAGE_HASHPTE
	 */
	lis	r0,_PAGE_HPTEFLAGS@h
	ori	r0,r0,_PAGE_HPTEFLAGS@l
	andc	r30,r30,r0
	ori	r30,r30,_PAGE_HASHPTE

	/* page number in r5 */
	rldicl	r5,r31,64-PTE_SHIFT,PTE_SHIFT

	/* Calculate primary group hash */
	and	r0,r28,r27
	rldicr	r3,r0,3,63-3	/* r0 = (hash & mask) << 3 */

	/* Call ppc_md.hpte_insert */
	ld	r7,STK_PARM(r4)(r1)	/* Retreive new pp bits */
	mr	r4,r29			/* Retreive va */
	li	r6,0			/* no vflags */
_GLOBAL(htab_call_hpte_insert1)
	bl	.			/* Will be patched by htab_finish_init() */
	cmpdi	0,r3,0
	bge	htab_pte_insert_ok	/* Insertion successful */
	cmpdi	0,r3,-2			/* Critical failure */
	beq-	htab_pte_insert_failure

	/* Now try secondary slot */
	
	/* page number in r5 */
	rldicl	r5,r31,64-PTE_SHIFT,PTE_SHIFT

	/* Calculate secondary group hash */
	andc	r0,r27,r28
	rldicr	r3,r0,3,63-3	/* r0 = (~hash & mask) << 3 */
	
	/* Call ppc_md.hpte_insert */
	ld	r7,STK_PARM(r4)(r1)	/* Retreive new pp bits */
	mr	r4,r29			/* Retreive va */
	li	r6,HPTE_V_SECONDARY@l	/* secondary slot */
_GLOBAL(htab_call_hpte_insert2)
	bl	.			/* Will be patched by htab_finish_init() */
	cmpdi	0,r3,0
	bge+	htab_pte_insert_ok	/* Insertion successful */
	cmpdi	0,r3,-2			/* Critical failure */
	beq-	htab_pte_insert_failure

	/* Both are full, we need to evict something */
	mftb	r0
	/* Pick a random group based on TB */
	andi.	r0,r0,1
	mr	r5,r28
	bne	2f
	not	r5,r5
2:	and	r0,r5,r27
	rldicr	r3,r0,3,63-3	/* r0 = (hash & mask) << 3 */	
	/* Call ppc_md.hpte_remove */
_GLOBAL(htab_call_hpte_remove)
	bl	.			/* Will be patched by htab_finish_init() */

	/* Try all again */
	b	htab_insert_pte	

bail_ok:
	li	r3,0
	b	bail

htab_pte_insert_ok:
	/* Insert slot number & secondary bit in PTE */
	rldimi	r30,r3,12,63-15
		
	/* Write out the PTE with a normal write
	 * (maybe add eieio may be good still ?)
	 */
htab_write_out_pte:
	ld	r6,STK_PARM(r6)(r1)
	std	r30,0(r6)
	li	r3, 0
bail:
	ld	r27,STK_REG(r27)(r1)
	ld	r28,STK_REG(r28)(r1)
	ld	r29,STK_REG(r29)(r1)
	ld      r30,STK_REG(r30)(r1)
	ld      r31,STK_REG(r31)(r1)
	addi    r1,r1,STACKFRAMESIZE
	ld      r0,16(r1)
	mtlr    r0
	blr

htab_modify_pte:
	/* Keep PP bits in r4 and slot idx from the PTE around in r3 */
	mr	r4,r3
	rlwinm	r3,r31,32-12,29,31

	/* Secondary group ? if yes, get a inverted hash value */
	mr	r5,r28
	andi.	r0,r31,_PAGE_SECONDARY
	beq	1f
	not	r5,r5
1:
	/* Calculate proper slot value for ppc_md.hpte_updatepp */
	and	r0,r5,r27
	rldicr	r0,r0,3,63-3	/* r0 = (hash & mask) << 3 */
	add	r3,r0,r3	/* add slot idx */

	/* Call ppc_md.hpte_updatepp */
	mr	r5,r29			/* va */
	li	r6,0			/* large is 0 */
	ld	r7,STK_PARM(r8)(r1)	/* get "local" param */
_GLOBAL(htab_call_hpte_updatepp)
	bl	.			/* Will be patched by htab_finish_init() */

	/* if we failed because typically the HPTE wasn't really here
	 * we try an insertion. 
	 */
	cmpdi	0,r3,-1
	beq-	htab_insert_pte

	/* Clear the BUSY bit and Write out the PTE */
	li	r0,_PAGE_BUSY
	andc	r30,r30,r0
	b	htab_write_out_pte

htab_wrong_access:
	/* Bail out clearing reservation */
	stdcx.	r31,0,r6
	li	r3,1
	b	bail

htab_pte_insert_failure:
	/* Bail out restoring old PTE */
	ld	r6,STK_PARM(r6)(r1)
	std	r31,0(r6)
	li	r3,-1
	b	bail


