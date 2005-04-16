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
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/ppc_asm.h>
#include <asm/offsets.h>
#include <asm/cputable.h>

/* void slb_allocate(unsigned long ea);
 *
 * Create an SLB entry for the given EA (user or kernel).
 * 	r3 = faulting address, r13 = PACA
 *	r9, r10, r11 are clobbered by this function
 * No other registers are examined or changed.
 */
_GLOBAL(slb_allocate)
	/*
	 * First find a slot, round robin. Previously we tried to find
	 * a free slot first but that took too long. Unfortunately we
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
	clrrdi	r11,r3,28
	li	r10,SLB_NUM_BOLTED-1	/* Stack goes in last bolted slot */
	cmpld	r9,r11
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
	/* r3 = faulting address, r10 = entry */

	srdi	r9,r3,60		/* get region */
	srdi	r3,r3,28		/* get esid */
	cmpldi	cr7,r9,0xc		/* cmp KERNELBASE for later use */

	rldimi	r10,r3,28,0		/* r10= ESID<<28 | entry */
	oris	r10,r10,SLB_ESID_V@h	/* r10 |= SLB_ESID_V */

	/* r3 = esid, r10 = esid_data, cr7 = <>KERNELBASE */

	blt	cr7,0f			/* user or kernel? */

	/* kernel address: proto-VSID = ESID */
	/* WARNING - MAGIC: we don't use the VSID 0xfffffffff, but
	 * this code will generate the protoVSID 0xfffffffff for the
	 * top segment.  That's ok, the scramble below will translate
	 * it to VSID 0, which is reserved as a bad VSID - one which
	 * will never have any pages in it.  */
	li	r11,SLB_VSID_KERNEL
BEGIN_FTR_SECTION
	bne	cr7,9f
	li	r11,(SLB_VSID_KERNEL|SLB_VSID_L)
END_FTR_SECTION_IFSET(CPU_FTR_16M_PAGE)
	b	9f

0:	/* user address: proto-VSID = context<<15 | ESID */
	li	r11,SLB_VSID_USER

	srdi.	r9,r3,13
	bne-	8f			/* invalid ea bits set */

#ifdef CONFIG_HUGETLB_PAGE
BEGIN_FTR_SECTION
	/* check against the hugepage ranges */
	cmpldi	r3,(TASK_HPAGE_END>>SID_SHIFT)
	bge	6f			/* >= TASK_HPAGE_END */
	cmpldi	r3,(TASK_HPAGE_BASE>>SID_SHIFT)
	bge	5f			/* TASK_HPAGE_BASE..TASK_HPAGE_END */
	cmpldi	r3,16
	bge	6f			/* 4GB..TASK_HPAGE_BASE */

	lhz	r9,PACAHTLBSEGS(r13)
	srd	r9,r9,r3
	andi.	r9,r9,1
	beq	6f

5:	/* this is a hugepage user address */
	li	r11,(SLB_VSID_USER|SLB_VSID_L)
END_FTR_SECTION_IFSET(CPU_FTR_16M_PAGE)
#endif /* CONFIG_HUGETLB_PAGE */

6:	ld	r9,PACACONTEXTID(r13)
	rldimi	r3,r9,USER_ESID_BITS,0

9:	/* r3 = protovsid, r11 = flags, r10 = esid_data, cr7 = <>KERNELBASE */
	ASM_VSID_SCRAMBLE(r3,r9)

	rldimi	r11,r3,SLB_VSID_SHIFT,16	/* combine VSID and flags */

	/*
	 * No need for an isync before or after this slbmte. The exception
	 * we enter with and the rfid we exit with are context synchronizing.
	 */
	slbmte	r11,r10

	bgelr	cr7			/* we're done for kernel addresses */

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
	blr

8:	/* invalid EA */
	li	r3,0			/* BAD_VSID */
	li	r11,SLB_VSID_USER	/* flags don't much matter */
	b	9b
