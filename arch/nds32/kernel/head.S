// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/ptrace.h>
#include <asm/asm-offsets.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/sizes.h>
#include <asm/thread_info.h>

#ifdef CONFIG_CPU_BIG_ENDIAN
#define OF_DT_MAGIC 0xd00dfeed
#else
#define OF_DT_MAGIC 0xedfe0dd0
#endif

	.globl  swapper_pg_dir
	.equ    swapper_pg_dir, TEXTADDR - 0x4000

/*
 * Kernel startup entry point.
 */
	.section ".head.text", "ax"
	.type   _stext, %function
ENTRY(_stext)
	setgie.d                            ! Disable interrupt
	isb
/*
 * Disable I/D-cache and enable it at a proper time
 */
	mfsr    $r0, $mr8
	li      $r1, #~(CACHE_CTL_mskIC_EN|CACHE_CTL_mskDC_EN)
	and     $r0, $r0, $r1
	mtsr    $r0, $mr8

/*
 * Process device tree blob
 */
	andi 	$r0,$r2,#0x3
	li	$r10, 0
	bne     $r0, $r10, _nodtb
	lwi	$r0, [$r2]
	li	$r1, OF_DT_MAGIC
	bne     $r0, $r1, _nodtb
	move	$r10, $r2
_nodtb:

/*
 * Create a temporary mapping area for booting, before start_kernel
 */
	sethi   $r4, hi20(swapper_pg_dir)
	li      $p0, (PAGE_OFFSET - PHYS_OFFSET)
	sub     $r4, $r4, $p0
	tlbop   FlushAll            ! invalidate TLB\n"
	isb
	mtsr    $r4, $L1_PPTB       ! load page table pointer\n"

#ifdef CONFIG_CPU_DCACHE_DISABLE
	#define MMU_CTL_NTCC MMU_CTL_CACHEABLE_NON
#else
	#ifdef CONFIG_CPU_DCACHE_WRITETHROUGH
		#define MMU_CTL_NTCC MMU_CTL_CACHEABLE_WT
	#else
		#define MMU_CTL_NTCC MMU_CTL_CACHEABLE_WB
	#endif
#endif

/* set NTC cacheability, mutliple page size in use */
	mfsr    $r3, $MMU_CTL
#if CONFIG_MEMORY_START >= 0xc0000000
	ori     $r3, $r3, (MMU_CTL_NTCC << MMU_CTL_offNTC3)
#elif CONFIG_MEMORY_START >= 0x80000000
	ori     $r3, $r3, (MMU_CTL_NTCC << MMU_CTL_offNTC2)
#elif CONFIG_MEMORY_START >= 0x40000000
	ori     $r3, $r3, (MMU_CTL_NTCC << MMU_CTL_offNTC1)
#else
	ori     $r3, $r3, (MMU_CTL_NTCC << MMU_CTL_offNTC0)
#endif

#ifdef CONFIG_ANDES_PAGE_SIZE_4KB
	ori     $r3, $r3, #(MMU_CTL_mskMPZIU)
#else
	ori     $r3, $r3, #(MMU_CTL_mskMPZIU|MMU_CTL_D8KB)
#endif
#ifdef CONFIG_HW_SUPPORT_UNALIGNMENT_ACCESS
	li      $r0, #MMU_CTL_UNA
	or      $r3, $r3, $r0
#endif
	mtsr    $r3, $MMU_CTL
	isb

/* set page size and size of kernel image */
        mfsr    $r0, $MMU_CFG
        srli    $r3, $r0, MMU_CFG_offfEPSZ
        zeb     $r3, $r3
        bnez    $r3, _extra_page_size_support
#ifdef CONFIG_ANDES_PAGE_SIZE_4KB
        li      $r5, #SZ_4K                 ! Use 4KB page size
#else
        li      $r5, #SZ_8K                 ! Use 8KB page size
        li      $r3, #1
#endif
        mtsr    $r3, $TLB_MISC
        b       _image_size_check

_extra_page_size_support:                    ! Use epzs pages size
        clz     $r6, $r3
        subri   $r2, $r6, #31
        li      $r3, #1
        sll     $r3, $r3, $r2
        /* MMU_CFG.EPSZ value -> meaning */
        mul     $r5, $r3, $r3
        slli    $r5, $r5, #14
        /* MMU_CFG.EPSZ  -> TLB_MISC.ACC_PSZ */
        addi    $r3, $r2, #0x2
        mtsr    $r3, $TLB_MISC

_image_size_check:
        /* calculate the image maximum size accepted by TLB config */
        andi    $r6, $r0, MMU_CFG_mskTBW
        andi    $r0, $r0, MMU_CFG_mskTBS
        srli    $r6, $r6, MMU_CFG_offTBW
        srli    $r0, $r0, MMU_CFG_offTBS
	addi    $r6, $r6, #0x1               ! MMU_CFG.TBW value -> meaning
        addi    $r0, $r0, #0x2               ! MMU_CFG.TBS value -> meaning
        sll     $r0, $r6, $r0                ! entries = k-way * n-set
        mul     $r6, $r0, $r5                ! max size = entries * page size
        /* check kernel image size */
        la      $r3, (_end - PAGE_OFFSET)
        bgt     $r3, $r6, __error

	li      $r2, #(PHYS_OFFSET + TLB_DATA_kernel_text_attr)
        li      $r3, PAGE_OFFSET
        add     $r6, $r6, $r3

_tlb:
	mtsr    $r3, $TLB_VPN
	dsb
	tlbop   $r2, RWR
	isb
	add     $r3, $r3, $r5
	add     $r2, $r2, $r5
	bgt     $r6, $r3, _tlb
	mfsr    $r3, $TLB_MISC      ! setup access page size
	li      $r2, #~0xf
	and     $r3, $r3, $r2
#ifdef CONFIG_ANDES_PAGE_SIZE_8KB
	ori    $r3, $r3, #0x1
#endif
	mtsr    $r3, $TLB_MISC

	mfsr    $r0, $MISC_CTL      ! Enable BTB and RTP and shadow sp
	ori     $r0, $r0, #MISC_init
	mtsr    $r0, $MISC_CTL

	mfsr    $p1, $PSW
	li      $r15, #~PSW_clr             ! clear WBNA|DME|IME|DT|IT|POM|INTL|GIE
	and     $p1, $p1, $r15
	ori     $p1, $p1, #PSW_init
	mtsr    $p1, $IPSW                  ! when iret, it will automatically enable MMU
	la      $lp, __mmap_switched
	mtsr    $lp, $IPC
	iret
	nop

	.type   __switch_data, %object
__switch_data:
	.long   __bss_start                 ! $r6
	.long   _end                        ! $r7
	.long	__atags_pointer 	    ! $atag_pointer
	.long   init_task                   ! $r9, move to $r25
	.long   init_thread_union + THREAD_SIZE    ! $sp


/*
 * The following fragment of code is executed with the MMU on in MMU mode,
 * and uses absolute addresses; this is not position independent.
 */
	.align
	.type   __mmap_switched, %function
__mmap_switched:
	la  $r3, __switch_data
	lmw.bim $r6, [$r3], $r9, #0b0001
	move	$r25, $r9
	move    $fp, #0             ! Clear  BSS (and zero $fp)
	beq $r7, $r6, _RRT
1:	swi.bi  $fp, [$r6], #4
	bne $r7, $r6, 1b
	swi	$r10, [$r8]

_RRT:
	b   start_kernel

__error:
	b   __error
