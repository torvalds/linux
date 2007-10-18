.text
#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/pgtable.h>
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

#define BEEP \
	inb	$97, %al; 	\
	outb	%al, $0x80; 	\
	movb	$3, %al; 	\
	outb	%al, $97; 	\
	outb	%al, $0x80; 	\
	movb	$-74, %al; 	\
	outb	%al, $67; 	\
	outb	%al, $0x80; 	\
	movb	$-119, %al; 	\
	outb	%al, $66; 	\
	outb	%al, $0x80; 	\
	movb	$15, %al; 	\
	outb	%al, $66;


ALIGN
	.align	16
ENTRY(wakeup_start)
wakeup_code:
	wakeup_code_start = .
	.code16

# Running in *copy* of this code, somewhere in low 1MB.

	cli
	cld
	# setup data segment
	movw	%cs, %ax
	movw	%ax, %ds		# Make ds:0 point to wakeup_start
	movw	%ax, %ss

	# Data segment must be set up before we can see whether to beep.
	testl   $4, realmode_flags - wakeup_code
	jz      1f
	BEEP
1:

					# Private stack is needed for ASUS board
	mov	$(wakeup_stack - wakeup_code), %sp

	pushl	$0			# Kill any dangerous flags
	popfl

	movl	real_magic - wakeup_code, %eax
	cmpl	$0x12345678, %eax
	jne	bogus_real_magic

	testl	$1, realmode_flags - wakeup_code
	jz	1f
	lcall   $0xc000,$3
	movw	%cs, %ax
	movw	%ax, %ds		# Bios might have played with that
	movw	%ax, %ss
1:

	testl	$2, realmode_flags - wakeup_code
	jz	1f
	mov	video_mode - wakeup_code, %ax
	call	mode_set
1:

	mov	%ds, %ax			# Find 32bit wakeup_code addr
	movzx   %ax, %esi			# (Convert %ds:gdt to a liner ptr)
	shll    $4, %esi
						# Fix up the vectors
	addl    %esi, wakeup_32_vector - wakeup_code
	addl    %esi, wakeup_long64_vector - wakeup_code
	addl    %esi, gdt_48a + 2 - wakeup_code # Fixup the gdt pointer

	lidtl	%ds:idt_48a - wakeup_code
	lgdtl	%ds:gdt_48a - wakeup_code	# load gdt with whatever is
						# appropriate

	movl	$1, %eax			# protected mode (PE) bit
	lmsw	%ax				# This is it!
	jmp	1f
1:

	ljmpl   *(wakeup_32_vector - wakeup_code)

	.balign 4
wakeup_32_vector:
	.long   wakeup_32 - wakeup_code
	.word   __KERNEL32_CS, 0

	.code32
wakeup_32:
# Running in this code, but at low address; paging is not yet turned on.

	movl	$__KERNEL_DS, %eax
	movl	%eax, %ds

	/*
	 * Prepare for entering 64bits mode
	 */

	/* Enable PAE */
	xorl	%eax, %eax
	btsl	$5, %eax
	movl	%eax, %cr4

	/* Setup early boot stage 4 level pagetables */
	leal    (wakeup_level4_pgt - wakeup_code)(%esi), %eax
	movl	%eax, %cr3

        /* Check if nx is implemented */
        movl    $0x80000001, %eax
        cpuid
        movl    %edx,%edi

	/* Enable Long Mode */
	xorl    %eax, %eax
	btsl	$_EFER_LME, %eax

	/* No Execute supported? */
	btl	$20,%edi
	jnc     1f
	btsl	$_EFER_NX, %eax
				
	/* Make changes effective */
1:	movl    $MSR_EFER, %ecx
	xorl    %edx, %edx
	wrmsr

	xorl	%eax, %eax
	btsl	$31, %eax			/* Enable paging and in turn activate Long Mode */
	btsl	$0, %eax			/* Enable protected mode */

	/* Make changes effective */
	movl	%eax, %cr0

	/* At this point:
		CR4.PAE must be 1
		CS.L must be 0
		CR3 must point to PML4
		Next instruction must be a branch
		This must be on identity-mapped page
	*/
	/*
	 * At this point we're in long mode but in 32bit compatibility mode
	 * with EFER.LME = 1, CS.L = 0, CS.D = 1 (and in turn
	 * EFER.LMA = 1). Now we want to jump in 64bit mode, to do that we load
	 * the new gdt/idt that has __KERNEL_CS with CS.L = 1.
	 */

	/* Finally jump in 64bit mode */
        ljmp    *(wakeup_long64_vector - wakeup_code)(%esi)

	.balign 4
wakeup_long64_vector:
	.long   wakeup_long64 - wakeup_code
	.word   __KERNEL_CS, 0

.code64

	/* Hooray, we are in Long 64-bit mode (but still running in
	 * low memory)
	 */
wakeup_long64:
	/*
	 * We must switch to a new descriptor in kernel space for the GDT
	 * because soon the kernel won't have access anymore to the userspace
	 * addresses where we're currently running on. We have to do that here
	 * because in 32bit we couldn't load a 64bit linear address.
	 */
	lgdt	cpu_gdt_descr

	movq    saved_magic, %rax
	movq    $0x123456789abcdef0, %rdx
	cmpq    %rdx, %rax
	jne     bogus_64_magic

	nop
	nop
	movw	$__KERNEL_DS, %ax
	movw	%ax, %ss	
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs
	movq	saved_rsp, %rsp

	movq	saved_rbx, %rbx
	movq	saved_rdi, %rdi
	movq	saved_rsi, %rsi
	movq	saved_rbp, %rbp

	movq	saved_rip, %rax
	jmp	*%rax

.code32

	.align	64	
gdta:
	/* Its good to keep gdt in sync with one in trampoline.S */
	.word	0, 0, 0, 0			# dummy
	/* ??? Why I need the accessed bit set in order for this to work? */
	.quad   0x00cf9b000000ffff              # __KERNEL32_CS
	.quad   0x00af9b000000ffff              # __KERNEL_CS
	.quad   0x00cf93000000ffff              # __KERNEL_DS

idt_48a:
	.word	0				# idt limit = 0
	.word	0, 0				# idt base = 0L

gdt_48a:
	.word	0x800				# gdt limit=2048,
						#  256 GDT entries
	.long   gdta - wakeup_code              # gdt base (relocated in later)
	
real_magic:	.quad 0
video_mode:	.quad 0
realmode_flags:	.quad 0

.code16
bogus_real_magic:
	jmp bogus_real_magic

.code64
bogus_64_magic:
	jmp bogus_64_magic

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

# For now, we only handle VESA modes (0x0200..0x03ff).  To handle other
# modes, we should probably compile in the video code from the boot
# directory.
.code16
mode_set:
	movw	%ax, %bx
	subb	$VIDEO_FIRST_VESA>>8, %bh
	cmpb	$2, %bh
	jb	check_vesa

setbad:
	clc
	ret

check_vesa:
	orw	$0x4000, %bx			# Use linear frame buffer
	movw	$0x4f02, %ax			# VESA BIOS mode set call
	int	$0x10
	cmpw	$0x004f, %ax			# AL=4f if implemented
	jnz	setbad				# AH=0 if OK

	stc
	ret

wakeup_stack_begin:	# Stack grows down

.org	0xff0
wakeup_stack:		# Just below end of page

.org   0x1000
ENTRY(wakeup_level4_pgt)
	.quad   level3_ident_pgt - __START_KERNEL_map + _KERNPG_TABLE
	.fill   510,8,0
	/* (2^48-(2*1024*1024*1024))/(2^39) = 511 */
	.quad   level3_kernel_pgt - __START_KERNEL_map + _KERNPG_TABLE

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
	.code64
ENTRY(acpi_copy_wakeup_routine)
	pushq	%rax
	pushq	%rdx

	movl	saved_video_mode, %edx
	movl	%edx, video_mode - wakeup_start (,%rdi)
	movl	acpi_realmode_flags, %edx
	movl	%edx, realmode_flags - wakeup_start (,%rdi)
	movq	$0x12345678, real_magic - wakeup_start (,%rdi)
	movq	$0x123456789abcdef0, %rdx
	movq	%rdx, saved_magic

	movq    saved_magic, %rax
	movq    $0x123456789abcdef0, %rdx
	cmpq    %rdx, %rax
	jne     bogus_64_magic

	# restore the regs we used
	popq	%rdx
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

	movq	$.L97, saved_rip(%rip)

	movq %rsp,saved_rsp
	movq %rbp,saved_rbp
	movq %rbx,saved_rbx
	movq %rdi,saved_rdi
	movq %rsi,saved_rsi

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
ENTRY(saved_rbp)	.quad	0
ENTRY(saved_rsi)	.quad	0
ENTRY(saved_rdi)	.quad	0
ENTRY(saved_rbx)	.quad	0

ENTRY(saved_rip)	.quad	0
ENTRY(saved_rsp)	.quad	0

ENTRY(saved_magic)	.quad	0
