/*
 *  linux/arch/x86_64/kernel/head.S -- start in 32bit and switch to 64bit
 *
 *  Copyright (C) 2000 Andrea Arcangeli <andrea@suse.de> SuSE
 *  Copyright (C) 2000 Pavel Machek <pavel@suse.cz>
 *  Copyright (C) 2000 Karsten Keil <kkeil@suse.de>
 *  Copyright (C) 2001,2002 Andi Kleen <ak@suse.de>
 *
 *  $Id: head.S,v 1.49 2002/03/19 17:39:25 ak Exp $
 */


#include <linux/linkage.h>
#include <linux/threads.h>
#include <linux/init.h>
#include <asm/desc.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/msr.h>
#include <asm/cache.h>
	
/* we are not able to switch in one step to the final KERNEL ADRESS SPACE
 * because we need identity-mapped pages on setup so define __START_KERNEL to
 * 0x100000 for this stage
 * 
 */

	.text
	.code32
	.globl startup_32
/* %bx:	 1 if coming from smp trampoline on secondary cpu */ 
startup_32:
	
	/*
	 * At this point the CPU runs in 32bit protected mode (CS.D = 1) with
	 * paging disabled and the point of this file is to switch to 64bit
	 * long mode with a kernel mapping for kerneland to jump into the
	 * kernel virtual addresses.
 	 * There is no stack until we set one up.
	 */

	/* Initialize the %ds segment register */
	movl $__KERNEL_DS,%eax
	movl %eax,%ds

	/* Load new GDT with the 64bit segments using 32bit descriptor */
	lgdt	pGDT32 - __START_KERNEL_map

	/* If the CPU doesn't support CPUID this will double fault.
	 * Unfortunately it is hard to check for CPUID without a stack. 
	 */
	
	/* Check if extended functions are implemented */		
	movl	$0x80000000, %eax
	cpuid
	cmpl	$0x80000000, %eax
	jbe	no_long_mode
	/* Check if long mode is implemented */
	mov	$0x80000001, %eax
	cpuid
	btl	$29, %edx
	jnc	no_long_mode

	/*
	 * Prepare for entering 64bits mode
	 */

	/* Enable PAE mode */
	xorl	%eax, %eax
	btsl	$5, %eax
	movl	%eax, %cr4

	/* Setup early boot stage 4 level pagetables */
	movl	$(boot_level4_pgt - __START_KERNEL_map), %eax
	movl	%eax, %cr3

	/* Setup EFER (Extended Feature Enable Register) */
	movl	$MSR_EFER, %ecx
	rdmsr

	/* Enable Long Mode */
	btsl	$_EFER_LME, %eax
				
	/* Make changes effective */
	wrmsr

	xorl	%eax, %eax
	btsl	$31, %eax			/* Enable paging and in turn activate Long Mode */
	btsl	$0, %eax			/* Enable protected mode */
	/* Make changes effective */
	movl	%eax, %cr0
	/*
	 * At this point we're in long mode but in 32bit compatibility mode
	 * with EFER.LME = 1, CS.L = 0, CS.D = 1 (and in turn
	 * EFER.LMA = 1). Now we want to jump in 64bit mode, to do that we use
	 * the new gdt/idt that has __KERNEL_CS with CS.L = 1.
	 */
	ljmp	$__KERNEL_CS, $(startup_64 - __START_KERNEL_map)

	.code64
	.org 0x100	
	.globl startup_64
startup_64:
	/* We come here either from startup_32
	 * or directly from a 64bit bootloader.
	 * Since we may have come directly from a bootloader we
	 * reload the page tables here.
	 */

	/* Enable PAE mode and PGE */
	xorq	%rax, %rax
	btsq	$5, %rax
	btsq	$7, %rax
	movq	%rax, %cr4

	/* Setup early boot stage 4 level pagetables. */
	movq	$(boot_level4_pgt - __START_KERNEL_map), %rax
	movq	%rax, %cr3

	/* Check if nx is implemented */
	movl	$0x80000001, %eax
	cpuid
	movl	%edx,%edi

	/* Setup EFER (Extended Feature Enable Register) */
	movl	$MSR_EFER, %ecx
	rdmsr

	/* Enable System Call */
	btsl	$_EFER_SCE, %eax

	/* No Execute supported? */
	btl	$20,%edi
	jnc     1f
	btsl	$_EFER_NX, %eax
1:
	/* Make changes effective */
	wrmsr

	/* Setup cr0 */
#define CR0_PM				1		/* protected mode */
#define CR0_MP				(1<<1)
#define CR0_ET				(1<<4)
#define CR0_NE				(1<<5)
#define CR0_WP				(1<<16)
#define CR0_AM				(1<<18)
#define CR0_PAGING 			(1<<31)
	movl $CR0_PM|CR0_MP|CR0_ET|CR0_NE|CR0_WP|CR0_AM|CR0_PAGING,%eax
	/* Make changes effective */
	movq	%rax, %cr0

	/* Setup a boot time stack */
	movq init_rsp(%rip),%rsp

	/* zero EFLAGS after setting rsp */
	pushq $0
	popfq

	/*
	 * We must switch to a new descriptor in kernel space for the GDT
	 * because soon the kernel won't have access anymore to the userspace
	 * addresses where we're currently running on. We have to do that here
	 * because in 32bit we couldn't load a 64bit linear address.
	 */
	lgdt	cpu_gdt_descr

	/* 
	 * Setup up a dummy PDA. this is just for some early bootup code
	 * that does in_interrupt() 
	 */ 
	movl	$MSR_GS_BASE,%ecx
	movq	$empty_zero_page,%rax
	movq    %rax,%rdx
	shrq	$32,%rdx
	wrmsr	

	/* set up data segments. actually 0 would do too */
	movl $__KERNEL_DS,%eax
	movl %eax,%ds	
	movl %eax,%ss
	movl %eax,%es
			
	/* esi is pointer to real mode structure with interesting info.
	   pass it to C */
	movl	%esi, %edi
	
	/* Finally jump to run C code and to be on real kernel address
	 * Since we are running on identity-mapped space we have to jump
	 * to the full 64bit address , this is only possible as indirect
	 * jump
	 */
	movq	initial_code(%rip),%rax
	jmp	*%rax

	/* SMP bootup changes these two */	
	.globl	initial_code
initial_code:
	.quad	x86_64_start_kernel
	.globl init_rsp
init_rsp:
	.quad  init_thread_union+THREAD_SIZE-8

ENTRY(early_idt_handler)
	cmpl $2,early_recursion_flag(%rip)
	jz  1f
	incl early_recursion_flag(%rip)
	xorl %eax,%eax
	movq 8(%rsp),%rsi	# get rip
	movq (%rsp),%rdx
	movq %cr2,%rcx
	leaq early_idt_msg(%rip),%rdi
	call early_printk
	cmpl $2,early_recursion_flag(%rip)
	jz  1f
	call dump_stack
#ifdef CONFIG_KALLSYMS	
	leaq early_idt_ripmsg(%rip),%rdi
	movq 8(%rsp),%rsi	# get rip again
	call __print_symbol
#endif
1:	hlt
	jmp 1b
early_recursion_flag:
	.long 0

early_idt_msg:
	.asciz "PANIC: early exception rip %lx error %lx cr2 %lx\n"
early_idt_ripmsg:
	.asciz "RIP %s\n"

.code32
ENTRY(no_long_mode)
	/* This isn't an x86-64 CPU so hang */
1:
	jmp	1b

.org 0xf00
	.globl pGDT32
pGDT32:
	.word	gdt_end-cpu_gdt_table
	.long	cpu_gdt_table-__START_KERNEL_map

.org 0xf10	
ljumpvector:
	.long	startup_64-__START_KERNEL_map
	.word	__KERNEL_CS

ENTRY(stext)
ENTRY(_stext)

	$page = 0
#define NEXT_PAGE(name) \
	$page = $page + 1; \
	.org $page * 0x1000; \
	phys_/**/name = $page * 0x1000 + __PHYSICAL_START; \
ENTRY(name)

NEXT_PAGE(init_level4_pgt)
	/* This gets initialized in x86_64_start_kernel */
	.fill	512,8,0

NEXT_PAGE(level3_ident_pgt)
	.quad	phys_level2_ident_pgt | 0x007
	.fill	511,8,0

NEXT_PAGE(level3_kernel_pgt)
	.fill	510,8,0
	/* (2^48-(2*1024*1024*1024)-((2^39)*511))/(2^30) = 510 */
	.quad	phys_level2_kernel_pgt | 0x007
	.fill	1,8,0

NEXT_PAGE(level2_ident_pgt)
	/* 40MB for bootup. 	*/
	i = 0
	.rept 20
	.quad	i << 21 | 0x083
	i = i + 1
	.endr
	/* Temporary mappings for the super early allocator in arch/x86_64/mm/init.c */
	.globl temp_boot_pmds
temp_boot_pmds:
	.fill	492,8,0
	
NEXT_PAGE(level2_kernel_pgt)
	/* 40MB kernel mapping. The kernel code cannot be bigger than that.
	   When you change this change KERNEL_TEXT_SIZE in page.h too. */
	/* (2^48-(2*1024*1024*1024)-((2^39)*511)-((2^30)*510)) = 0 */
	i = 0
	.rept 20
	.quad	i << 21 | 0x183
	i = i + 1
	.endr
	/* Module mapping starts here */
	.fill	492,8,0

NEXT_PAGE(empty_zero_page)

NEXT_PAGE(level3_physmem_pgt)
	.quad	phys_level2_kernel_pgt | 0x007	/* so that __va works even before pagetable_init */
	.fill	511,8,0

#undef NEXT_PAGE

	.data

#ifdef CONFIG_ACPI_SLEEP
	.align PAGE_SIZE
ENTRY(wakeup_level4_pgt)
	.quad	phys_level3_ident_pgt | 0x007
	.fill	255,8,0
	.quad	phys_level3_physmem_pgt | 0x007
	.fill	254,8,0
	/* (2^48-(2*1024*1024*1024))/(2^39) = 511 */
	.quad	phys_level3_kernel_pgt | 0x007
#endif

#ifndef CONFIG_HOTPLUG_CPU
	__INITDATA
#endif
	/*
	 * This default setting generates an ident mapping at address 0x100000
	 * and a mapping for the kernel that precisely maps virtual address
	 * 0xffffffff80000000 to physical address 0x000000. (always using
	 * 2Mbyte large pages provided by PAE mode)
	 */
	.align PAGE_SIZE
ENTRY(boot_level4_pgt)
	.quad	phys_level3_ident_pgt | 0x007
	.fill	255,8,0
	.quad	phys_level3_physmem_pgt | 0x007
	.fill	254,8,0
	/* (2^48-(2*1024*1024*1024))/(2^39) = 511 */
	.quad	phys_level3_kernel_pgt | 0x007

	.data

	.align 16
	.globl cpu_gdt_descr
cpu_gdt_descr:
	.word	gdt_end-cpu_gdt_table
gdt:
	.quad	cpu_gdt_table
#ifdef CONFIG_SMP
	.rept	NR_CPUS-1
	.word	0
	.quad	0
	.endr
#endif

/* We need valid kernel segments for data and code in long mode too
 * IRET will check the segment types  kkeil 2000/10/28
 * Also sysret mandates a special GDT layout 
 */
		 		
.align PAGE_SIZE

/* The TLS descriptors are currently at a different place compared to i386.
   Hopefully nobody expects them at a fixed place (Wine?) */
	
ENTRY(cpu_gdt_table)
	.quad	0x0000000000000000	/* NULL descriptor */
	.quad	0x0			/* unused */
	.quad	0x00af9a000000ffff	/* __KERNEL_CS */
	.quad	0x00cf92000000ffff	/* __KERNEL_DS */
	.quad	0x00cffa000000ffff	/* __USER32_CS */
	.quad	0x00cff2000000ffff	/* __USER_DS, __USER32_DS  */		
	.quad	0x00affa000000ffff	/* __USER_CS */
	.quad	0x00cf9a000000ffff	/* __KERNEL32_CS */
	.quad	0,0			/* TSS */
	.quad	0,0			/* LDT */
	.quad   0,0,0			/* three TLS descriptors */ 
	.quad	0			/* unused */
gdt_end:	
	/* asm/segment.h:GDT_ENTRIES must match this */	
	/* This should be a multiple of the cache line size */
	/* GDTs of other CPUs are now dynamically allocated */

	/* zero the remaining page */
	.fill PAGE_SIZE / 8 - GDT_ENTRIES,8,0

ENTRY(idt_table)	
	.rept   256
	.quad   0
	.quad 	0
	.endr

