/*
 * Copyright (C) 2005-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * Stubs for syscalls that require access to pt_regs or that take more
 * than five parameters.
 */

#define ARG6	r3

	.text
	.global __sys_rt_sigsuspend
	.type	__sys_rt_sigsuspend,@function
__sys_rt_sigsuspend:
	mov	r10, sp
	rjmp	sys_rt_sigsuspend

	.global	__sys_rt_sigreturn
	.type	__sys_rt_sigreturn,@function
__sys_rt_sigreturn:
	mov	r12, sp
	rjmp	sys_rt_sigreturn

	.global	__sys_mmap2
	.type	__sys_mmap2,@function
__sys_mmap2:
	pushm	lr
	st.w	--sp, ARG6
	call	sys_mmap_pgoff
	sub	sp, -4
	popm	pc

	.global	__sys_sendto
	.type	__sys_sendto,@function
__sys_sendto:
	pushm	lr
	st.w	--sp, ARG6
	call	sys_sendto
	sub	sp, -4
	popm	pc

	.global	__sys_recvfrom
	.type	__sys_recvfrom,@function
__sys_recvfrom:
	pushm	lr
	st.w	--sp, ARG6
	call	sys_recvfrom
	sub	sp, -4
	popm	pc

	.global	__sys_pselect6
	.type	__sys_pselect6,@function
__sys_pselect6:
	pushm	lr
	st.w	--sp, ARG6
	call	sys_pselect6
	sub	sp, -4
	popm	pc

	.global	__sys_splice
	.type	__sys_splice,@function
__sys_splice:
	pushm	lr
	st.w	--sp, ARG6
	call	sys_splice
	sub	sp, -4
	popm	pc

	.global	__sys_epoll_pwait
	.type	__sys_epoll_pwait,@function
__sys_epoll_pwait:
	pushm	lr
	st.w	--sp, ARG6
	call	sys_epoll_pwait
	sub	sp, -4
	popm	pc

	.global __sys_sync_file_range
	.type	__sys_sync_file_range,@function
__sys_sync_file_range:
	pushm	lr
	st.w	--sp, ARG6
	call	sys_sync_file_range
	sub	sp, -4
	popm	pc

	.global __sys_fallocate
	.type	__sys_fallocate,@function
__sys_fallocate:
	pushm	lr
	st.w	--sp, ARG6
	call	sys_fallocate
	sub	sp, -4
	popm	pc

	.global __sys_fanotify_mark
	.type	__sys_fanotify_mark,@function
__sys_fanotify_mark:
	pushm	lr
	st.w	--sp, ARG6
	call	sys_fanotify_mark
	sub	sp, -4
	popm	pc

	.global __sys_process_vm_readv
	.type	__sys_process_vm_readv,@function
__sys_process_vm_readv:
	pushm	lr
	st.w	--sp, ARG6
	call	sys_process_vm_readv
	sub	sp, -4
	popm	pc

	.global __sys_process_vm_writev
	.type	__sys_process_vm_writev,@function
__sys_process_vm_writev:
	pushm	lr
	st.w	--sp, ARG6
	call	sys_process_vm_writev
	sub	sp, -4
	popm	pc
