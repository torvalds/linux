/*
 * We need constants.h for:
 *  VMA_VM_MM
 *  VMA_VM_FLAGS
 *  VM_EXEC
 */
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>

/*
 * vma_vm_mm - get mm pointer from vma pointer (vma->vm_mm)
 */
	.macro	vma_vm_mm, rd, rn
	ldr	\rd, [\rn, #VMA_VM_MM]
	.endm

/*
 * vma_vm_flags - get vma->vm_flags
 */
	.macro	vma_vm_flags, rd, rn
	ldr	\rd, [\rn, #VMA_VM_FLAGS]
	.endm

	.macro	tsk_mm, rd, rn
	ldr	\rd, [\rn, #TI_TASK]
	ldr	\rd, [\rd, #TSK_ACTIVE_MM]
	.endm

/*
 * act_mm - get current->active_mm
 */
	.macro	act_mm, rd
	bic	\rd, sp, #8128
	bic	\rd, \rd, #63
	ldr	\rd, [\rd, #TI_TASK]
	ldr	\rd, [\rd, #TSK_ACTIVE_MM]
	.endm

/*
 * mmid - get context id from mm pointer (mm->context.id)
 */
	.macro	mmid, rd, rn
	ldr	\rd, [\rn, #MM_CONTEXT_ID]
	.endm

/*
 * mask_asid - mask the ASID from the context ID
 */
	.macro	asid, rd, rn
	and	\rd, \rn, #255
	.endm

	.macro	crval, clear, mmuset, ucset
#ifdef CONFIG_MMU
	.word	\clear
	.word	\mmuset
#else
	.word	\clear
	.word	\ucset
#endif
	.endm

/*
 * dcache_line_size - get the minimum D-cache line size from the CTR register
 * on ARMv7.
 */
	.macro	dcache_line_size, reg, tmp
	mrc	p15, 0, \tmp, c0, c0, 1		@ read ctr
	lsr	\tmp, \tmp, #16
	and	\tmp, \tmp, #0xf		@ cache line size encoding
	mov	\reg, #4			@ bytes per word
	mov	\reg, \reg, lsl \tmp		@ actual cache line size
	.endm

/*
 * icache_line_size - get the minimum I-cache line size from the CTR register
 * on ARMv7.
 */
	.macro	icache_line_size, reg, tmp
	mrc	p15, 0, \tmp, c0, c0, 1		@ read ctr
	and	\tmp, \tmp, #0xf		@ cache line size encoding
	mov	\reg, #4			@ bytes per word
	mov	\reg, \reg, lsl \tmp		@ actual cache line size
	.endm

/*
 * Sanity check the PTE configuration for the code below - which makes
 * certain assumptions about how these bits are laid out.
 */
#ifdef CONFIG_MMU
#if L_PTE_SHARED != PTE_EXT_SHARED
#error PTE shared bit mismatch
#endif
#if !defined (CONFIG_ARM_LPAE) && \
	(L_PTE_XN+L_PTE_USER+L_PTE_RDONLY+L_PTE_DIRTY+L_PTE_YOUNG+\
	 L_PTE_FILE+L_PTE_PRESENT) > L_PTE_SHARED
#error Invalid Linux PTE bit settings
#endif
#endif	/* CONFIG_MMU */

/*
 * The ARMv6 and ARMv7 set_pte_ext translation function.
 *
 * Permission translation:
 *  YUWD  APX AP1 AP0	SVC	User
 *  0xxx   0   0   0	no acc	no acc
 *  100x   1   0   1	r/o	no acc
 *  10x0   1   0   1	r/o	no acc
 *  1011   0   0   1	r/w	no acc
 *  110x   0   1   0	r/w	r/o
 *  11x0   0   1   0	r/w	r/o
 *  1111   0   1   1	r/w	r/w
 *
 * If !CONFIG_CPU_USE_DOMAINS, the following permissions are changed:
 *  110x   1   1   1	r/o	r/o
 *  11x0   1   1   1	r/o	r/o
 */
	.macro	armv6_mt_table pfx
\pfx\()_mt_table:
	.long	0x00						@ L_PTE_MT_UNCACHED
	.long	PTE_EXT_TEX(1)					@ L_PTE_MT_BUFFERABLE
	.long	PTE_CACHEABLE					@ L_PTE_MT_WRITETHROUGH
	.long	PTE_CACHEABLE | PTE_BUFFERABLE			@ L_PTE_MT_WRITEBACK
	.long	PTE_BUFFERABLE					@ L_PTE_MT_DEV_SHARED
	.long	0x00						@ unused
	.long	0x00						@ L_PTE_MT_MINICACHE (not present)
	.long	PTE_EXT_TEX(1) | PTE_CACHEABLE | PTE_BUFFERABLE	@ L_PTE_MT_WRITEALLOC
	.long	0x00						@ unused
	.long	PTE_EXT_TEX(1)					@ L_PTE_MT_DEV_WC
	.long	0x00						@ unused
	.long	PTE_CACHEABLE | PTE_BUFFERABLE			@ L_PTE_MT_DEV_CACHED
	.long	PTE_EXT_TEX(2)					@ L_PTE_MT_DEV_NONSHARED
	.long	0x00						@ unused
	.long	0x00						@ unused
	.long	0x00						@ unused
	.endm

	.macro	armv6_set_pte_ext pfx
	str	r1, [r0], #2048			@ linux version

	bic	r3, r1, #0x000003fc
	bic	r3, r3, #PTE_TYPE_MASK
	orr	r3, r3, r2
	orr	r3, r3, #PTE_EXT_AP0 | 2

	adr	ip, \pfx\()_mt_table
	and	r2, r1, #L_PTE_MT_MASK
	ldr	r2, [ip, r2]

	eor	r1, r1, #L_PTE_DIRTY
	tst	r1, #L_PTE_DIRTY|L_PTE_RDONLY
	orrne	r3, r3, #PTE_EXT_APX

	tst	r1, #L_PTE_USER
	orrne	r3, r3, #PTE_EXT_AP1
#ifdef CONFIG_CPU_USE_DOMAINS
	@ allow kernel read/write access to read-only user pages
	tstne	r3, #PTE_EXT_APX
	bicne	r3, r3, #PTE_EXT_APX | PTE_EXT_AP0
#endif

	tst	r1, #L_PTE_XN
	orrne	r3, r3, #PTE_EXT_XN

	orr	r3, r3, r2

	tst	r1, #L_PTE_YOUNG
	tstne	r1, #L_PTE_PRESENT
	moveq	r3, #0
#ifndef CONFIG_CPU_USE_DOMAINS
	tstne	r1, #L_PTE_NONE
	movne	r3, #0
#endif

	str	r3, [r0]
	mcr	p15, 0, r0, c7, c10, 1		@ flush_pte
	.endm


/*
 * The ARMv3, ARMv4 and ARMv5 set_pte_ext translation function,
 * covering most CPUs except Xscale and Xscale 3.
 *
 * Permission translation:
 *  YUWD   AP	SVC	User
 *  0xxx  0x00	no acc	no acc
 *  100x  0x00	r/o	no acc
 *  10x0  0x00	r/o	no acc
 *  1011  0x55	r/w	no acc
 *  110x  0xaa	r/w	r/o
 *  11x0  0xaa	r/w	r/o
 *  1111  0xff	r/w	r/w
 */
	.macro	armv3_set_pte_ext wc_disable=1
	str	r1, [r0], #2048			@ linux version

	eor	r3, r1, #L_PTE_PRESENT | L_PTE_YOUNG | L_PTE_DIRTY

	bic	r2, r1, #PTE_SMALL_AP_MASK	@ keep C, B bits
	bic	r2, r2, #PTE_TYPE_MASK
	orr	r2, r2, #PTE_TYPE_SMALL

	tst	r3, #L_PTE_USER			@ user?
	orrne	r2, r2, #PTE_SMALL_AP_URO_SRW

	tst	r3, #L_PTE_RDONLY | L_PTE_DIRTY	@ write and dirty?
	orreq	r2, r2, #PTE_SMALL_AP_UNO_SRW

	tst	r3, #L_PTE_PRESENT | L_PTE_YOUNG	@ present and young?
	movne	r2, #0

	.if	\wc_disable
#ifdef CONFIG_CPU_DCACHE_WRITETHROUGH
	tst	r2, #PTE_CACHEABLE
	bicne	r2, r2, #PTE_BUFFERABLE
#endif
	.endif
	str	r2, [r0]		@ hardware version
	.endm


/*
 * Xscale set_pte_ext translation, split into two halves to cope
 * with work-arounds.  r3 must be preserved by code between these
 * two macros.
 *
 * Permission translation:
 *  YUWD  AP	SVC	User
 *  0xxx  00	no acc	no acc
 *  100x  00	r/o	no acc
 *  10x0  00	r/o	no acc
 *  1011  01	r/w	no acc
 *  110x  10	r/w	r/o
 *  11x0  10	r/w	r/o
 *  1111  11	r/w	r/w
 */
	.macro	xscale_set_pte_ext_prologue
	str	r1, [r0]			@ linux version

	eor	r3, r1, #L_PTE_PRESENT | L_PTE_YOUNG | L_PTE_DIRTY

	bic	r2, r1, #PTE_SMALL_AP_MASK	@ keep C, B bits
	orr	r2, r2, #PTE_TYPE_EXT		@ extended page

	tst	r3, #L_PTE_USER			@ user?
	orrne	r2, r2, #PTE_EXT_AP_URO_SRW	@ yes -> user r/o, system r/w

	tst	r3, #L_PTE_RDONLY | L_PTE_DIRTY	@ write and dirty?
	orreq	r2, r2, #PTE_EXT_AP_UNO_SRW	@ yes -> user n/a, system r/w
						@ combined with user -> user r/w
	.endm

	.macro	xscale_set_pte_ext_epilogue
	tst	r3, #L_PTE_PRESENT | L_PTE_YOUNG	@ present and young?
	movne	r2, #0				@ no -> fault

	str	r2, [r0, #2048]!		@ hardware version
	mov	ip, #0
	mcr	p15, 0, r0, c7, c10, 1		@ clean L1 D line
	mcr	p15, 0, ip, c7, c10, 4		@ data write barrier
	.endm

.macro define_processor_functions name:req, dabort:req, pabort:req, nommu=0, suspend=0
	.type	\name\()_processor_functions, #object
	.align 2
ENTRY(\name\()_processor_functions)
	.word	\dabort
	.word	\pabort
	.word	cpu_\name\()_proc_init
	.word	cpu_\name\()_proc_fin
	.word	cpu_\name\()_reset
	.word	cpu_\name\()_do_idle
	.word	cpu_\name\()_dcache_clean_area
	.word	cpu_\name\()_switch_mm

	.if \nommu
	.word	0
	.else
	.word	cpu_\name\()_set_pte_ext
	.endif

	.if \suspend
	.word	cpu_\name\()_suspend_size
#ifdef CONFIG_PM_SLEEP
	.word	cpu_\name\()_do_suspend
	.word	cpu_\name\()_do_resume
#else
	.word	0
	.word	0
#endif
	.else
	.word	0
	.word	0
	.word	0
	.endif

	.size	\name\()_processor_functions, . - \name\()_processor_functions
.endm

.macro define_cache_functions name:req
	.align 2
	.type	\name\()_cache_fns, #object
ENTRY(\name\()_cache_fns)
	.long	\name\()_flush_icache_all
	.long	\name\()_flush_kern_cache_all
	.long   \name\()_flush_kern_cache_louis
	.long	\name\()_flush_user_cache_all
	.long	\name\()_flush_user_cache_range
	.long	\name\()_coherent_kern_range
	.long	\name\()_coherent_user_range
	.long	\name\()_flush_kern_dcache_area
	.long	\name\()_dma_map_area
	.long	\name\()_dma_unmap_area
	.long	\name\()_dma_flush_range
	.size	\name\()_cache_fns, . - \name\()_cache_fns
.endm

.macro define_tlb_functions name:req, flags_up:req, flags_smp
	.type	\name\()_tlb_fns, #object
ENTRY(\name\()_tlb_fns)
	.long	\name\()_flush_user_tlb_range
	.long	\name\()_flush_kern_tlb_range
	.ifnb \flags_smp
		ALT_SMP(.long	\flags_smp )
		ALT_UP(.long	\flags_up )
	.else
		.long	\flags_up
	.endif
	.size	\name\()_tlb_fns, . - \name\()_tlb_fns
.endm
