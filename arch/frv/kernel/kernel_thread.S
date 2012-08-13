/* kernel_thread.S: kernel thread creation
 *
 * Copyright (C) 2003 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/linkage.h>
#include <linux/kern_levels.h>
#include <asm/unistd.h>

#define CLONE_VM	0x00000100	/* set if VM shared between processes */

	.section .rodata
kernel_thread_emsg:
	.asciz	KERN_ERR "failed to create kernel thread: error=%d\n"

	.text
	.balign		4

###############################################################################
#
# Create a kernel thread
#
# int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
#
###############################################################################
	.globl		kernel_thread
	.type		kernel_thread,@function
kernel_thread:
	or.p		gr8,gr0,gr4
	or		gr9,gr0,gr5

	# start by forking the current process, but with shared VM
	setlos.p	#__NR_clone,gr7		; syscall number
	ori		gr10,#CLONE_VM,gr8	; first syscall arg	[clone_flags]
	sethi.p		#0xe4e4,gr9		; second syscall arg	[newsp]
	setlo		#0xe4e4,gr9
	setlos.p	#0,gr10			; third syscall arg	[parent_tidptr]
	setlos		#0,gr11			; fourth syscall arg	[child_tidptr]
	tira		gr0,#0
	setlos.p	#4095,gr7
	andcc		gr8,gr8,gr0,icc0
	addcc.p		gr8,gr7,gr0,icc1
	bnelr		icc0,#2
	bc		icc1,#0,kernel_thread_error

	# now invoke the work function
	or		gr5,gr0,gr8
	calll		@(gr4,gr0)

	# and finally exit the thread
	setlos		#__NR_exit,gr7		; syscall number
	tira		gr0,#0

kernel_thread_error:
	subi		sp,#8,sp
	movsg		lr,gr4
	sti		gr8,@(sp,#0)
	sti.p		gr4,@(sp,#4)

	or		gr8,gr0,gr9
	sethi.p		%hi(kernel_thread_emsg),gr8
	setlo		%lo(kernel_thread_emsg),gr8

	call		printk

	ldi		@(sp,#4),gr4
	ldi		@(sp,#0),gr8
	subi		sp,#8,sp
	jmpl		@(gr4,gr0)

	.size		kernel_thread,.-kernel_thread
