.text
#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/msr.h>

# Copyright 2003 Pavel Machek <pavel@suse.cz>, distribute under GPLv2
#
# wakeup_code runs in real mode, and at unknown address (determined at run-time).
# Therefore it must only use relative jumps/calls. 
#
# Do we need to deal with A20? It is okay: ACPI specs says A20 must be enabled
#
# If physical address of wakeup_code is 0x12345, BIOS should call us with
# cs = 0x1234, eip = 0x05
#


ALIGN
	.align	16
ENTRY(wakeup_start)
wakeup_code:
	wakeup_code_start = .
	.code16

# Running in *copy* of this code, somewhere in low 1MB.

	movb	$0xa1, %al	;  outb %al, $0x80
	cli
	cld
	# setup data segment
	movw	%cs, %ax
	movw	%ax, %ds					# Make ds:0 point to wakeup_start
	movw	%ax, %ss
	mov	$(wakeup_stack - wakeup_code), %sp		# Private stack is needed for ASUS board

	pushl	$0						# Kill any dangerous flags
	popfl

	movl	real_magic - wakeup_code, %eax
	cmpl	$0x12345678, %eax
	jne	bogus_real_magic

	testl	$1, video_flags - wakeup_code
	jz	1f
	lcall   $0xc000,$3
	movw	%cs, %ax
	movw	%ax, %ds					# Bios might have played with that
	movw	%ax, %ss
1:

	testl	$2, video_flags - wakeup_code
	jz	1f
	mov	video_mode - wakeup_code, %ax
	call	mode_seta
1:

 	movw	$0xb800, %ax
	movw	%ax,%fs
	movw	$0x0e00 + 'L', %fs:(0x10)

	movb	$0xa2, %al	;  outb %al, $0x80
	
	lidt	%ds:idt_48a - wakeup_code
	xorl	%eax, %eax
	movw	%ds, %ax			# (Convert %ds:gdt to a linear ptr)
	shll	$4, %eax
	addl	$(gdta - wakeup_code), %eax
	movl	%eax, gdt_48a +2 - wakeup_code
	lgdtl	%ds:gdt_48a - wakeup_code	# load gdt with whatever is
						# appropriate

	movl	$1, %eax			# protected mode (PE) bit
	lmsw	%ax				# This is it!
	jmp	1f
1:

	.byte 0x66, 0xea			# prefix + jmpi-opcode
	.long	wakeup_32 - __START_KERNEL_map
	.word	__KERNEL_CS

	.code32
wakeup_32:
# Running in this code, but at low address; paging is not yet turned on.
	movb	$0xa5, %al	;  outb %al, $0x80

	/* Check if extended functions are implemented */		
	movl	$0x80000000, %eax
	cpuid
	cmpl	$0x80000000, %eax
	jbe	bogus_cpu
	wbinvd
	mov	$0x80000001, %eax
	cpuid
	btl	$29, %edx
	jnc	bogus_cpu
	movl	%edx,%edi
	
	movw	$__KERNEL_DS, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs

	movw	$__KERNEL_DS, %ax	
	movw	%ax, %ss

	mov	$(wakeup_stack - __START_KERNEL_map), %esp
	movl	saved_magic - __START_KERNEL_map, %eax
	cmpl	$0x9abcdef0, %eax
	jne	bogus_32_magic

	/*
	 * Prepare for entering 64bits mode
	 */

	/* Enable PAE mode and PGE */
	xorl	%eax, %eax
	btsl	$5, %eax
	btsl	$7, %eax
	movl	%eax, %cr4

	/* Setup early boot stage 4 level pagetables */
	movl	$(wakeup_level4_pgt - __START_KERNEL_map), %eax
	movl	%eax, %cr3

	/* Setup EFER (Extended Feature Enable Register) */
	movl	$MSR_EFER, %ecx
	rdmsr
	/* Fool rdmsr and reset %eax to avoid dependences */
	xorl	%eax, %eax
	/* Enable Long Mode */
	btsl	$_EFER_LME, %eax
	/* Enable System Call */
	btsl	$_EFER_SCE, %eax

	/* No Execute supported? */	
	btl	$20,%edi
	jnc     1f
	btsl	$_EFER_NX, %eax
1:	
				
	/* Make changes effective */
	wrmsr
	wbinvd

	xorl	%eax, %eax
	btsl	$31, %eax			/* Enable paging and in turn activate Long Mode */
	btsl	$0, %eax			/* Enable protected mode */
	btsl	$1, %eax			/* Enable MP */
	btsl	$4, %eax			/* Enable ET */
	btsl	$5, %eax			/* Enable NE */
	btsl	$16, %eax			/* Enable WP */
	btsl	$18, %eax			/* Enable AM */

	/* Make changes effective */
	movl	%eax, %cr0
	/* At this point:
		CR4.PAE must be 1
		CS.L must be 0
		CR3 must point to PML4
		Next instruction must be a branch
		This must be on identity-mapped page
	*/
	jmp	reach_compatibility_mode
reach_compatibility_mode:
	movw	$0x0e00 + 'i', %ds:(0xb8012)
	movb	$0xa8, %al	;  outb %al, $0x80; 	
		
	/*
	 * At this point we're in long mode but in 32bit compatibility mode
	 * with EFER.LME = 1, CS.L = 0, CS.D = 1 (and in turn
	 * EFER.LMA = 1). Now we want to jump in 64bit mode, to do that we load
	 * the new gdt/idt that has __KERNEL_CS with CS.L = 1.
	 */

	movw	$0x0e00 + 'n', %ds:(0xb8014)
	movb	$0xa9, %al	;  outb %al, $0x80
	
	/* Load new GDT with the 64bit segment using 32bit descriptor */
	movl	$(pGDT32 - __START_KERNEL_map), %eax
	lgdt	(%eax)

	movl    $(wakeup_jumpvector - __START_KERNEL_map), %eax
	/* Finally jump in 64bit mode */
	ljmp	*(%eax)

wakeup_jumpvector:
	.long	wakeup_long64 - __START_KERNEL_map
	.word	__KERNEL_CS

.code64

	/*	Hooray, we are in Long 64-bit mode (but still running in low memory) */
wakeup_long64:
	/*
	 * We must switch to a new descriptor in kernel space for the GDT
	 * because soon the kernel won't have access anymore to the userspace
	 * addresses where we're currently running on. We have to do that here
	 * because in 32bit we couldn't load a 64bit linear address.
	 */
	lgdt	cpu_gdt_descr - __START_KERNEL_map

	movw	$0x0e00 + 'u', %ds:(0xb8016)
	
	nop
	nop
	movw	$__KERNEL_DS, %ax
	movw	%ax, %ss	
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs
	movq	saved_esp, %rsp

	movw	$0x0e00 + 'x', %ds:(0xb8018)
	movq	saved_ebx, %rbx
	movq	saved_edi, %rdi
	movq	saved_esi, %rsi
	movq	saved_ebp, %rbp

	movw	$0x0e00 + '!', %ds:(0xb801a)
	movq	saved_eip, %rax
	jmp	*%rax

.code32

	.align	64	
gdta:
	.word	0, 0, 0, 0			# dummy

	.word	0, 0, 0, 0			# unused

	.word	0xFFFF				# 4Gb - (0x100000*0x1000 = 4Gb)
	.word	0				# base address = 0
	.word	0x9B00				# code read/exec. ??? Why I need 0x9B00 (as opposed to 0x9A00 in order for this to work?)
	.word	0x00CF				# granularity = 4096, 386
						#  (+5th nibble of limit)

	.word	0xFFFF				# 4Gb - (0x100000*0x1000 = 4Gb)
	.word	0				# base address = 0
	.word	0x9200				# data read/write
	.word	0x00CF				# granularity = 4096, 386
						#  (+5th nibble of limit)
# this is 64bit descriptor for code
	.word	0xFFFF
	.word	0
	.word	0x9A00				# code read/exec
	.word	0x00AF				# as above, but it is long mode and with D=0

idt_48a:
	.word	0				# idt limit = 0
	.word	0, 0				# idt base = 0L

gdt_48a:
	.word	0x8000				# gdt limit=2048,
						#  256 GDT entries
	.word	0, 0				# gdt base (filled in later)
	
	
real_save_gdt:	.word 0
		.quad 0
real_magic:	.quad 0
video_mode:	.quad 0
video_flags:	.quad 0

bogus_real_magic:
	movb	$0xba,%al	;  outb %al,$0x80		
	jmp bogus_real_magic

bogus_32_magic:
	movb	$0xb3,%al	;  outb %al,$0x80
	jmp bogus_32_magic

bogus_31_magic:
	movb	$0xb1,%al	;  outb %al,$0x80
	jmp bogus_31_magic

bogus_cpu:
	movb	$0xbc,%al	;  outb %al,$0x80
	jmp bogus_cpu

	
/* This code uses an extended set of video mode numbers. These include:
 * Aliases for standard modes
 *	NORMAL_VGA (-1)
 *	EXTENDED_VGA (-2)
 *	ASK_VGA (-3)
 * Video modes numbered by menu position -- NOT RECOMMENDED because of lack
 * of compatibility when extending the table. These are between 0x00 and 0xff.
 */
#define VIDEO_FIRST_MENU 0x0000

/* Standard BIOS video modes (BIOS number + 0x0100) */
#define VIDEO_FIRST_BIOS 0x0100

/* VESA BIOS video modes (VESA number + 0x0200) */
#define VIDEO_FIRST_VESA 0x0200

/* Video7 special modes (BIOS number + 0x0900) */
#define VIDEO_FIRST_V7 0x0900

# Setting of user mode (AX=mode ID) => CF=success
mode_seta:
	movw	%ax, %bx
#if 0
	cmpb	$0xff, %ah
	jz	setalias

	testb	$VIDEO_RECALC>>8, %ah
	jnz	_setrec

	cmpb	$VIDEO_FIRST_RESOLUTION>>8, %ah
	jnc	setres
	
	cmpb	$VIDEO_FIRST_SPECIAL>>8, %ah
	jz	setspc

	cmpb	$VIDEO_FIRST_V7>>8, %ah
	jz	setv7
#endif
	
	cmpb	$VIDEO_FIRST_VESA>>8, %ah
	jnc	check_vesaa
#if 0	
	orb	%ah, %ah
	jz	setmenu
#endif
	
	decb	%ah
#	jz	setbios				  Add bios modes later

setbada:	clc
	ret

check_vesaa:
	subb	$VIDEO_FIRST_VESA>>8, %bh
	orw	$0x4000, %bx			# Use linear frame buffer
	movw	$0x4f02, %ax			# VESA BIOS mode set call
	int	$0x10
	cmpw	$0x004f, %ax			# AL=4f if implemented
	jnz	_setbada				# AH=0 if OK

	stc
	ret

_setbada: jmp setbada

	.code64
bogus_magic:
	movw	$0x0e00 + 'B', %ds:(0xb8018)
	jmp bogus_magic

bogus_magic2:
	movw	$0x0e00 + '2', %ds:(0xb8018)
	jmp bogus_magic2
	

wakeup_stack_begin:	# Stack grows down

.org	0xff0
wakeup_stack:		# Just below end of page

ENTRY(wakeup_end)
	
##
# acpi_copy_wakeup_routine
#
# Copy the above routine to low memory.
#
# Parameters:
# %rdi:	place to copy wakeup routine to
#
# Returned address is location of code in low memory (past data and stack)
#
ENTRY(acpi_copy_wakeup_routine)
	pushq	%rax
	pushq	%rcx
	pushq	%rdx

	sgdt	saved_gdt
	sidt	saved_idt
	sldt	saved_ldt
	str	saved_tss

	movq    %cr3, %rdx
	movq    %rdx, saved_cr3
	movq    %cr4, %rdx
	movq    %rdx, saved_cr4
	movq	%cr0, %rdx
	movq	%rdx, saved_cr0
	sgdt    real_save_gdt - wakeup_start (,%rdi)
	movl	$MSR_EFER, %ecx
	rdmsr
	movl	%eax, saved_efer
	movl	%edx, saved_efer2

	movl	saved_video_mode, %edx
	movl	%edx, video_mode - wakeup_start (,%rdi)
	movl	acpi_video_flags, %edx
	movl	%edx, video_flags - wakeup_start (,%rdi)
	movq	$0x12345678, real_magic - wakeup_start (,%rdi)
	movq	$0x123456789abcdef0, %rdx
	movq	%rdx, saved_magic

	movl	saved_magic - __START_KERNEL_map, %eax
	cmpl	$0x9abcdef0, %eax
	jne	bogus_32_magic

	# make sure %cr4 is set correctly (features, etc)
	movl	saved_cr4 - __START_KERNEL_map, %eax
	movq	%rax, %cr4

	movl	saved_cr0 - __START_KERNEL_map, %eax
	movq	%rax, %cr0
	jmp	1f		# Flush pipelines
1:
	# restore the regs we used
	popq	%rdx
	popq	%rcx
	popq	%rax
ENTRY(do_suspend_lowlevel_s4bios)
	ret

	.align 2
	.p2align 4,,15
.globl do_suspend_lowlevel
	.type	do_suspend_lowlevel,@function
do_suspend_lowlevel:
.LFB5:
	subq	$8, %rsp
	xorl	%eax, %eax
	call	save_processor_state

	movq %rsp, saved_context_esp(%rip)
	movq %rax, saved_context_eax(%rip)
	movq %rbx, saved_context_ebx(%rip)
	movq %rcx, saved_context_ecx(%rip)
	movq %rdx, saved_context_edx(%rip)
	movq %rbp, saved_context_ebp(%rip)
	movq %rsi, saved_context_esi(%rip)
	movq %rdi, saved_context_edi(%rip)
	movq %r8,  saved_context_r08(%rip)
	movq %r9,  saved_context_r09(%rip)
	movq %r10, saved_context_r10(%rip)
	movq %r11, saved_context_r11(%rip)
	movq %r12, saved_context_r12(%rip)
	movq %r13, saved_context_r13(%rip)
	movq %r14, saved_context_r14(%rip)
	movq %r15, saved_context_r15(%rip)
	pushfq ; popq saved_context_eflags(%rip)

	movq	$.L97, saved_eip(%rip)

	movq %rsp,saved_esp
	movq %rbp,saved_ebp
	movq %rbx,saved_ebx
	movq %rdi,saved_edi
	movq %rsi,saved_esi

	addq	$8, %rsp
	movl	$3, %edi
	xorl	%eax, %eax
	jmp	acpi_enter_sleep_state
.L97:
	.p2align 4,,7
.L99:
	.align 4
	movl	$24, %eax
	movw %ax, %ds
	movq	saved_context+58(%rip), %rax
	movq %rax, %cr4
	movq	saved_context+50(%rip), %rax
	movq %rax, %cr3
	movq	saved_context+42(%rip), %rax
	movq %rax, %cr2
	movq	saved_context+34(%rip), %rax
	movq %rax, %cr0
	pushq saved_context_eflags(%rip) ; popfq
	movq saved_context_esp(%rip), %rsp
	movq saved_context_ebp(%rip), %rbp
	movq saved_context_eax(%rip), %rax
	movq saved_context_ebx(%rip), %rbx
	movq saved_context_ecx(%rip), %rcx
	movq saved_context_edx(%rip), %rdx
	movq saved_context_esi(%rip), %rsi
	movq saved_context_edi(%rip), %rdi
	movq saved_context_r08(%rip), %r8
	movq saved_context_r09(%rip), %r9
	movq saved_context_r10(%rip), %r10
	movq saved_context_r11(%rip), %r11
	movq saved_context_r12(%rip), %r12
	movq saved_context_r13(%rip), %r13
	movq saved_context_r14(%rip), %r14
	movq saved_context_r15(%rip), %r15

	xorl	%eax, %eax
	addq	$8, %rsp
	jmp	restore_processor_state
.LFE5:
.Lfe5:
	.size	do_suspend_lowlevel,.Lfe5-do_suspend_lowlevel
	
.data
ALIGN
ENTRY(saved_ebp)	.quad	0
ENTRY(saved_esi)	.quad	0
ENTRY(saved_edi)	.quad	0
ENTRY(saved_ebx)	.quad	0

ENTRY(saved_eip)	.quad	0
ENTRY(saved_esp)	.quad	0

ENTRY(saved_magic)	.quad	0

ALIGN
# saved registers
saved_gdt:	.quad	0,0
saved_idt:	.quad	0,0
saved_ldt:	.quad	0
saved_tss:	.quad	0

saved_cr0:	.quad 0
saved_cr3:	.quad 0
saved_cr4:	.quad 0
saved_efer:	.quad 0
saved_efer2:	.quad 0
