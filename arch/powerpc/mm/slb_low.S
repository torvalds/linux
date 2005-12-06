/*
 * arch/ppc64/mm/slb_low.S
 *
 * Low-level SLB routines
 *
 * Copyright (C) 2004 David Gibson <dwg@au.ibm.com>, IBM
 *
 * Based on earlier C version:
 * Dave Engebretsen and Mike Corrigan {engebret|mikejc}@us.ibm.com
 *    Copyright (c) 2001 Dave Engebretsen
 * Copyright (C) 2002 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <asm/processor.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/cputable.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>

/* void slb_allocate_realmode(unsigned long ea);
 *
 * Create an SLB entry for the given EA (user or kernel).
 * 	r3 = faulting address, r13 = PACA
 *	r9, r10, r11 are clobbered by this function
 * No other registers are examined or changed.
 */
_GLOBAL(slb_allocate_realmode)
	/* r3 = faulting address */

	srdi	r9,r3,60		/* get region */
	srdi	r10,r3,28		/* get esid */
	cmpldi	cr7,r9,0xc		/* cmp KERNELBASE for later use */

	/* r3 = address, r10 = esid, cr7 = <>KERNELBASE */
	blt	cr7,0f			/* user or kernel? */

	/* kernel address: proto-VSID = ESID */
	/* WARNING - MAGIC: we don't use the VSID 0xfffffffff, but
	 * this code will generate the protoVSID 0xfffffffff for the
	 * top segment.  That's ok, the scramble below will translate
	 * it to VSID 0, which is reserved as a bad VSID - one which
	 * will never have any pages in it.  */

	/* Check if hitting the linear mapping of the vmalloc/ioremap
	 * kernel space
	*/
	bne	cr7,1f

	/* Linear mapping encoding bits, the "li" instruction below will
	 * be patched by the kernel at boot
	 */
_GLOBAL(slb_miss_kernel_load_linear)
	li	r11,0
	b	slb_finish_load

1:	/* vmalloc/ioremap mapping encoding bits, the "li" instruction below
	 * will be patched by the kernel at boot
	 */
_GLOBAL(slb_miss_kernel_load_virtual)
	li	r11,0
	b	slb_finish_load


0:	/* user address: proto-VSID = context << 15 | ESID. First check
	 * if the address is within the boundaries of the user region
	 */
	srdi.	r9,r10,USER_ESID_BITS
	bne-	8f			/* invalid ea bits set */

	/* Figure out if the segment contains huge pages */
#ifdef CONFIG_HUGETLB_PAGE
BEGIN_FTR_SECTION
	b	1f
END_FTR_SECTION_IFCLR(CPU_FTR_16M_PAGE)
	cmpldi	r10,16

	lhz	r9,PACALOWHTLBAREAS(r13)
	mr	r11,r10
	blt	5f

	lhz	r9,PACAHIGHHTLBAREAS(r13)
	srdi	r11,r10,(HTLB_AREA_SHIFT-SID_SHIFT)

5:	srd	r9,r9,r11
	andi.	r9,r9,1
	beq	1f
_GLOBAL(slb_miss_user_load_huge)
	li	r11,0
	b	2f
1:
#endif /* CONFIG_HUGETLB_PAGE */

_GLOBAL(slb_miss_user_load_normal)
	li	r11,0

2:
	ld	r9,PACACONTEXTID(r13)
	rldimi	r10,r9,USER_ESID_BITS,0
	b	slb_finish_load

8:	/* invalid EA */
	li	r10,0			/* BAD_VSID */
	li	r11,SLB_VSID_USER	/* flags don't much matter */
	b	slb_finish_load

#ifdef __DISABLED__

/* void slb_allocate_user(unsigned long ea);
 *
 * Create an SLB entry for the given EA (user or kernel).
 * 	r3 = faulting address, r13 = PACA
 *	r9, r10, r11 are clobbered by this function
 * No other registers are examined or changed.
 *
 * It is called with translation enabled in order to be able to walk the
 * page tables. This is not currently used.
 */
_GLOBAL(slb_allocate_user)
	/* r3 = faulting address */
	srdi	r10,r3,28		/* get esid */

	crset	4*cr7+lt		/* set "user" flag for later */

	/* check if we fit in the range covered by the pagetables*/
	srdi.	r9,r3,PGTABLE_EADDR_SIZE
	crnot	4*cr0+eq,4*cr0+eq
	beqlr

	/* now we need to get to the page tables in order to get the page
	 * size encoding from the PMD. In the future, we'll be able to deal
	 * with 1T segments too by getting the encoding from the PGD instead
	 */
	ld	r9,PACAPGDIR(r13)
	cmpldi	cr0,r9,0
	beqlr
	rlwinm	r11,r10,8,25,28
	ldx	r9,r9,r11		/* get pgd_t */
	cmpldi	cr0,r9,0
	beqlr
	rlwinm	r11,r10,3,17,28
	ldx	r9,r9,r11		/* get pmd_t */
	cmpldi	cr0,r9,0
	beqlr

	/* build vsid flags */
	andi.	r11,r9,SLB_VSID_LLP
	ori	r11,r11,SLB_VSID_USER

	/* get context to calculate proto-VSID */
	ld	r9,PACACONTEXTID(r13)
	rldimi	r10,r9,USER_ESID_BITS,0

	/* fall through slb_finish_load */

#endif /* __DISABLED__ */


/*
 * Finish loading of an SLB entry and return
 *
 * r3 = EA, r10 = proto-VSID, r11 = flags, clobbers r9, cr7 = <>KERNELBASE
 */
slb_finish_load:
	ASM_VSID_SCRAMBLE(r10,r9)
	rldimi	r11,r10,SLB_VSID_SHIFT,16	/* combine VSID and flags */

	/* r3 = EA, r11 = VSID data */
	/*
	 * Find a slot, round robin. Previously we tried to find a
	 * free slot first but that took too long. Unfortunately we
 	 * dont have any LRU information to help us choose a slot.
 	 */
#ifdef CONFIG_PPC_ISERIES
	/*
	 * On iSeries, the "bolted" stack segment can be cast out on
	 * shared processor switch so we need to check for a miss on
	 * it and restore it to the right slot.
	 */
	ld	r9,PACAKSAVE(r13)
	clrrdi	r9,r9,28
	clrrdi	r3,r3,28
	li	r10,SLB_NUM_BOLTED-1	/* Stack goes in last bolted slot */
	cmpld	r9,r3
	beq	3f
#endif /* CONFIG_PPC_ISERIES */

	ld	r10,PACASTABRR(r13)
	addi	r10,r10,1
	/* use a cpu feature mask if we ever change our slb size */
	cmpldi	r10,SLB_NUM_ENTRIES

	blt+	4f
	li	r10,SLB_NUM_BOLTED

4:
	std	r10,PACASTABRR(r13)

3:
	rldimi	r3,r10,0,36		/* r3= EA[0:35] | entry */
	oris	r10,r3,SLB_ESID_V@h	/* r3 |= SLB_ESID_V */

	/* r3 = ESID data, r11 = VSID data */

	/*
	 * No need for an isync before or after this slbmte. The exception
	 * we enter with and the rfid we exit with are context synchronizing.
	 */
	slbmte	r11,r10

	/* we're done for kernel addresses */
	crclr	4*cr0+eq		/* set result to "success" */
	bgelr	cr7

	/* Update the slb cache */
	lhz	r3,PACASLBCACHEPTR(r13)	/* offset = paca->slb_cache_ptr */
	cmpldi	r3,SLB_CACHE_ENTRIES
	bge	1f

	/* still room in the slb cache */
	sldi	r11,r3,1		/* r11 = offset * sizeof(u16) */
	rldicl	r10,r10,36,28		/* get low 16 bits of the ESID */
	add	r11,r11,r13		/* r11 = (u16 *)paca + offset */
	sth	r10,PACASLBCACHE(r11)	/* paca->slb_cache[offset] = esid */
	addi	r3,r3,1			/* offset++ */
	b	2f
1:					/* offset >= SLB_CACHE_ENTRIES */
	li	r3,SLB_CACHE_ENTRIES+1
2:
	sth	r3,PACASLBCACHEPTR(r13)	/* paca->slb_cache_ptr = offset */
	crclr	4*cr0+eq		/* set result to "success" */
	blr

