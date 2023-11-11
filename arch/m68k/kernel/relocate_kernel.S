/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>

#include <asm/asm-offsets.h>
#include <asm/page.h>
#include <asm/setup.h>


#define MMU_BASE	8		/* MMU flags base in cpu_mmu_flags */

.text

ENTRY(relocate_new_kernel)
	movel %sp@(4),%a0		/* a0 = ptr */
	movel %sp@(8),%a1		/* a1 = start */
	movel %sp@(12),%d1		/* d1 = cpu_mmu_flags */
	movew #PAGE_MASK,%d2		/* d2 = PAGE_MASK */

	/* Disable MMU */

	btst #MMU_BASE + MMUB_68851,%d1
	jeq 3f

1:	/* 68851 or 68030 */

	lea %pc@(.Lcopy),%a4
2:	addl #0x00000000,%a4		/* virt_to_phys() */

	.section ".m68k_fixup","aw"
	.long M68K_FIXUP_MEMOFFSET, 2b+2
	.previous

	.chip 68030
	pmove %tc,%d0			/* Disable MMU */
	bclr #7,%d0
	pmove %d0,%tc
	jmp %a4@			/* Jump to physical .Lcopy */
	.chip 68k

3:
	btst #MMU_BASE + MMUB_68030,%d1
	jne 1b

	btst #MMU_BASE + MMUB_68040,%d1
	jeq 6f

4:	/* 68040 or 68060 */

	lea %pc@(.Lcont040),%a4
5:	addl #0x00000000,%a4		/* virt_to_phys() */

	.section ".m68k_fixup","aw"
	.long M68K_FIXUP_MEMOFFSET, 5b+2
	.previous

	movel %a4,%d0
	andl #0xff000000,%d0
	orw #0xe020,%d0			/* Map 16 MiB, enable, cacheable */
	.chip 68040
	movec %d0,%itt0
	movec %d0,%dtt0
	.chip 68k
	jmp %a4@			/* Jump to physical .Lcont040 */

.Lcont040:
	moveq #0,%d0
	.chip 68040
	movec %d0,%tc			/* Disable MMU */
	movec %d0,%itt0
	movec %d0,%itt1
	movec %d0,%dtt0
	movec %d0,%dtt1
	.chip 68k
	jra .Lcopy

6:
	btst #MMU_BASE + MMUB_68060,%d1
	jne 4b

.Lcopy:
	movel %a0@+,%d0			/* d0 = entry = *ptr */
	jeq .Lflush

	btst #2,%d0			/* entry & IND_DONE? */
	jne .Lflush

	btst #1,%d0			/* entry & IND_INDIRECTION? */
	jeq 1f
	andw %d2,%d0
	movel %d0,%a0			/* ptr = entry & PAGE_MASK */
	jra .Lcopy

1:
	btst #0,%d0			/* entry & IND_DESTINATION? */
	jeq 2f
	andw %d2,%d0
	movel %d0,%a2			/* a2 = dst = entry & PAGE_MASK */
	jra .Lcopy

2:
	btst #3,%d0			/* entry & IND_SOURCE? */
	jeq .Lcopy

	andw %d2,%d0
	movel %d0,%a3			/* a3 = src = entry & PAGE_MASK */
	movew #PAGE_SIZE/32 - 1,%d0	/* d0 = PAGE_SIZE/32 - 1 */
3:
	movel %a3@+,%a2@+		/* *dst++ = *src++ */
	movel %a3@+,%a2@+		/* *dst++ = *src++ */
	movel %a3@+,%a2@+		/* *dst++ = *src++ */
	movel %a3@+,%a2@+		/* *dst++ = *src++ */
	movel %a3@+,%a2@+		/* *dst++ = *src++ */
	movel %a3@+,%a2@+		/* *dst++ = *src++ */
	movel %a3@+,%a2@+		/* *dst++ = *src++ */
	movel %a3@+,%a2@+		/* *dst++ = *src++ */
	dbf %d0, 3b
	jra .Lcopy

.Lflush:
	/* Flush all caches */

	btst #CPUB_68020,%d1
	jeq 2f

1:	/* 68020 or 68030 */
	.chip 68030
	movec %cacr,%d0
	orw #0x808,%d0
	movec %d0,%cacr
	.chip 68k
	jra .Lreincarnate

2:
	btst #CPUB_68030,%d1
	jne 1b

	btst #CPUB_68040,%d1
	jeq 4f

3:	/* 68040 or 68060 */
	.chip 68040
	nop
	cpusha %bc
	nop
	cinva %bc
	nop
	.chip 68k
	jra .Lreincarnate

4:
	btst #CPUB_68060,%d1
	jne 3b

.Lreincarnate:
	jmp %a1@

relocate_new_kernel_end:

ENTRY(relocate_new_kernel_size)
	.long relocate_new_kernel_end - relocate_new_kernel
