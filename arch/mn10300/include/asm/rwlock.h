/*
 * Helpers used by both rw spinlocks and rw semaphores.
 *
 * Based in part on code from semaphore.h and
 * spinlock.h Copyright 1996 Linus Torvalds.
 *
 * Copyright 1999 Red Hat, Inc.
 *
 * Written by Benjamin LaHaise.
 *
 * Modified by Matsushita Electric Industrial Co., Ltd.
 * Modifications:
 * 13-Nov-2006 MEI Temporarily delete lock functions for SMP support.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */
#ifndef _ASM_RWLOCK_H
#define _ASM_RWLOCK_H

#define RW_LOCK_BIAS		 0x01000000

#ifndef CONFIG_SMP

typedef struct { unsigned long a[100]; } __dummy_lock_t;
#define __dummy_lock(lock) (*(__dummy_lock_t *)(lock))

#define RW_LOCK_BIAS_STR	"0x01000000"

#define __build_read_lock_ptr(rw, helper)				\
	do {								\
		asm volatile(						\
			"	mov	(%0),d3			\n"	\
			"	sub	1,d3			\n"	\
			"	mov	d3,(%0)			\n"	\
			"	blt	1f			\n"	\
			"	bra	2f			\n"	\
			"1:	jmp	3f			\n"	\
			"2:					\n"	\
			"	.section .text.lock,\"ax\"	\n"	\
			"3:	call	"helper"[],0		\n"	\
			"	jmp	2b			\n"	\
			"	.previous"				\
			:						\
			: "d" (rw)					\
			: "memory", "d3", "cc");			\
	} while (0)

#define __build_read_lock_const(rw, helper)				\
	do {								\
		asm volatile(						\
			"	mov	(%0),d3			\n"	\
			"	sub	1,d3			\n"	\
			"	mov	d3,(%0)			\n"	\
			"	blt	1f			\n"	\
			"	bra	2f			\n"	\
			"1:	jmp	3f			\n"	\
			"2:					\n"	\
			"	.section .text.lock,\"ax\"	\n"	\
			"3:	call	"helper"[],0		\n"	\
			"	jmp	2b			\n"	\
			"	.previous"				\
			:						\
			: "d" (rw)					\
			: "memory", "d3", "cc");			\
	} while (0)

#define __build_read_lock(rw, helper) \
	do {								\
		if (__builtin_constant_p(rw))				\
			__build_read_lock_const(rw, helper);		\
		else							\
			__build_read_lock_ptr(rw, helper);		\
	} while (0)

#define __build_write_lock_ptr(rw, helper)				\
	do {								\
		asm volatile(						\
			"	mov	(%0),d3			\n"	\
			"	sub	1,d3			\n"	\
			"	mov	d3,(%0)			\n"	\
			"	blt	1f			\n"	\
			"	bra	2f			\n"	\
			"1:	jmp	3f			\n"	\
			"2:					\n"	\
			"	.section .text.lock,\"ax\"	\n"	\
			"3:	call	"helper"[],0		\n"	\
			"	jmp	2b			\n"	\
			"	.previous"				\
			:						\
			: "d" (rw)					\
			: "memory", "d3", "cc");			\
	} while (0)

#define __build_write_lock_const(rw, helper)				\
	do {								\
		asm volatile(						\
			"	mov	(%0),d3			\n"	\
			"	sub	1,d3			\n"	\
			"	mov	d3,(%0)			\n"	\
			"	blt	1f			\n"	\
			"	bra	2f			\n"	\
			"1:	jmp	3f			\n"	\
			"2:					\n"	\
			"	.section .text.lock,\"ax\"	\n"	\
			"3:	call	"helper"[],0		\n"	\
			"	jmp	2b			\n"	\
			"	.previous"				\
			:						\
			: "d" (rw)					\
			: "memory", "d3", "cc");			\
	} while (0)

#define __build_write_lock(rw, helper)					\
	do {								\
		if (__builtin_constant_p(rw))				\
			__build_write_lock_const(rw, helper);		\
		else							\
			__build_write_lock_ptr(rw, helper);		\
	} while (0)

#endif /* CONFIG_SMP */
#endif /* _ASM_RWLOCK_H */
