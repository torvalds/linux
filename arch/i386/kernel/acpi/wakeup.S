.text
#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/page.h>

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
	.align	4096
ENTRY(wakeup_start)
wakeup_code:
	wakeup_code_start = .
	.code16

 	movw	$0xb800, %ax
	movw	%ax,%fs
	movw	$0x0e00 + 'L', %fs:(0x10)

	cli
	cld

	# setup data segment
	movw	%cs, %ax
	movw	%ax, %ds					# Make ds:0 point to wakeup_start
	movw	%ax, %ss
	mov	$(wakeup_stack - wakeup_code), %sp		# Private stack is needed for ASUS board
	movw	$0x0e00 + 'S', %fs:(0x12)

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
	call	mode_set
1:

	# set up page table
	movl	$swsusp_pg_dir-__PAGE_OFFSET, %eax
	movl	%eax, %cr3

	testl	$1, real_efer_save_restore - wakeup_code
	jz	4f
	# restore efer setting
	movl	real_save_efer_edx - wakeup_code, %edx
	movl	real_save_efer_eax - wakeup_code, %eax
	mov     $0xc0000080, %ecx
	wrmsr
4:
	# make sure %cr4 is set correctly (features, etc)
	movl	real_save_cr4 - wakeup_code, %eax
	movl	%eax, %cr4
	movw	$0xb800, %ax
	movw	%ax,%fs
	movw	$0x0e00 + 'i', %fs:(0x12)
	
	# need a gdt -- use lgdtl to force 32-bit operands, in case
	# the GDT is located past 16 megabytes.
	lgdtl	real_save_gdt - wakeup_code

	movl	real_save_cr0 - wakeup_code, %eax
	movl	%eax, %cr0
	jmp 1f
1:
	movw	$0x0e00 + 'n', %fs:(0x14)

	movl	real_magic - wakeup_code, %eax
	cmpl	$0x12345678, %eax
	jne	bogus_real_magic

	ljmpl	$__KERNEL_CS,$wakeup_pmode_return

real_save_gdt:	.word 0
		.long 0
real_save_cr0:	.long 0
real_save_cr3:	.long 0
real_save_cr4:	.long 0
real_magic:	.long 0
video_mode:	.long 0
video_flags:	.long 0
real_efer_save_restore:	.long 0
real_save_efer_edx: 	.long 0
real_save_efer_eax: 	.long 0

bogus_real_magic:
	movw	$0x0e00 + 'B', %fs:(0x12)
	jmp bogus_real_magic

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
mode_set:
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
	jnc	check_vesa
#if 0	
	orb	%ah, %ah
	jz	setmenu
#endif
	
	decb	%ah
#	jz	setbios				  Add bios modes later

setbad:	clc
	ret

check_vesa:
	subb	$VIDEO_FIRST_VESA>>8, %bh
	orw	$0x4000, %bx			# Use linear frame buffer
	movw	$0x4f02, %ax			# VESA BIOS mode set call
	int	$0x10
	cmpw	$0x004f, %ax			# AL=4f if implemented
	jnz	_setbad				# AH=0 if OK

	stc
	ret

_setbad: jmp setbad

	.code32
	ALIGN

.org	0x800
wakeup_stack_begin:	# Stack grows down

.org	0xff0		# Just below end of page
wakeup_stack:
ENTRY(wakeup_end)
	
.org	0x1000

wakeup_pmode_return:
	movw	$__KERNEL_DS, %ax
	movw	%ax, %ss
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs
	movw	$0x0e00 + 'u', 0xb8016

	# reload the gdt, as we need the full 32 bit address
	lgdt	saved_gdt
	lidt	saved_idt
	lldt	saved_ldt
	ljmp	$(__KERNEL_CS),$1f
1:
	movl	%cr3, %eax
	movl	%eax, %cr3
	wbinvd

	# and restore the stack ... but you need gdt for this to work
	movl	saved_context_esp, %esp

	movl	%cs:saved_magic, %eax
	cmpl	$0x12345678, %eax
	jne	bogus_magic

	# jump to place where we left off
	movl	saved_eip,%eax
	jmp	*%eax

bogus_magic:
	movw	$0x0e00 + 'B', 0xb8018
	jmp	bogus_magic


##
# acpi_copy_wakeup_routine
#
# Copy the above routine to low memory.
#
# Parameters:
# %eax:	place to copy wakeup routine to
#
# Returned address is location of code in low memory (past data and stack)
#
ENTRY(acpi_copy_wakeup_routine)

	sgdt	saved_gdt
	sidt	saved_idt
	sldt	saved_ldt
	str	saved_tss

	movl	nx_enabled, %edx
	movl	%edx, real_efer_save_restore - wakeup_start (%eax)
	testl	$1, real_efer_save_restore - wakeup_start (%eax)
	jz	2f
	# save efer setting
	pushl	%eax
	movl	%eax, %ebx
	mov     $0xc0000080, %ecx
	rdmsr
	movl	%edx, real_save_efer_edx - wakeup_start (%ebx)
	movl	%eax, real_save_efer_eax - wakeup_start (%ebx)
	popl	%eax
2:

	movl    %cr3, %edx
	movl    %edx, real_save_cr3 - wakeup_start (%eax)
	movl    %cr4, %edx
	movl    %edx, real_save_cr4 - wakeup_start (%eax)
	movl	%cr0, %edx
	movl	%edx, real_save_cr0 - wakeup_start (%eax)
	sgdt    real_save_gdt - wakeup_start (%eax)

	movl	saved_videomode, %edx
	movl	%edx, video_mode - wakeup_start (%eax)
	movl	acpi_video_flags, %edx
	movl	%edx, video_flags - wakeup_start (%eax)
	movl	$0x12345678, real_magic - wakeup_start (%eax)
	movl	$0x12345678, saved_magic
	ret

.data
ALIGN
ENTRY(saved_magic)	.long	0
ENTRY(saved_eip)	.long	0

save_registers:
	leal	4(%esp), %eax
	movl	%eax, saved_context_esp
	movl %ebx, saved_context_ebx
	movl %ebp, saved_context_ebp
	movl %esi, saved_context_esi
	movl %edi, saved_context_edi
	pushfl ; popl saved_context_eflags

	movl $ret_point, saved_eip
	ret


restore_registers:
	movl saved_context_ebp, %ebp
	movl saved_context_ebx, %ebx
	movl saved_context_esi, %esi
	movl saved_context_edi, %edi
	pushl saved_context_eflags ; popfl
	ret	

ENTRY(do_suspend_lowlevel)
	call	save_processor_state
	call	save_registers
	pushl	$3
	call	acpi_enter_sleep_state
	addl	$4, %esp
	ret
	.p2align 4,,7
ret_point:
	call	restore_registers
	call	restore_processor_state
	ret

ALIGN
# saved registers
saved_gdt:	.long	0,0
saved_idt:	.long	0,0
saved_ldt:	.long	0
saved_tss:	.long	0

