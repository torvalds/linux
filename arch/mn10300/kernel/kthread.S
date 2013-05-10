/* MN10300 Kernel thread trampoline function
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by Mark Salter (msalter@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
	.text

###############################################################################
#
# kernel_thread_helper - trampoline for kernel_thread()
#
# On entry:
#   A2 = address of function to call
#   D2 = function argument
#
###############################################################################
	.globl	kernel_thread_helper
	.type	kernel_thread_helper,@function
kernel_thread_helper:
	mov	do_exit,d1
	mov	d1,(sp)
	mov	d1,mdr
	mov	d2,d0
	jmp	(a2)

	.size	kernel_thread_helper,.-kernel_thread_helper
