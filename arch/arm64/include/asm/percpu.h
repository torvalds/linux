/*
 * Copyright (C) 2013 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_PERCPU_H
#define __ASM_PERCPU_H

#include <linux/preempt.h>

#include <asm/alternative.h>
#include <asm/cmpxchg.h>
#include <asm/stack_pointer.h>

static inline void set_my_cpu_offset(unsigned long off)
{
	asm volatile(ALTERNATIVE("msr tpidr_el1, %0",
				 "msr tpidr_el2, %0",
				 ARM64_HAS_VIRT_HOST_EXTN)
			:: "r" (off) : "memory");
}

static inline unsigned long __my_cpu_offset(void)
{
	unsigned long off;

	/*
	 * We want to allow caching the value, so avoid using volatile and
	 * instead use a fake stack read to hazard against barrier().
	 */
	asm(ALTERNATIVE("mrs %0, tpidr_el1",
			"mrs %0, tpidr_el2",
			ARM64_HAS_VIRT_HOST_EXTN)
		: "=r" (off) :
		"Q" (*(const unsigned long *)current_stack_pointer));

	return off;
}
#define __my_cpu_offset __my_cpu_offset()

#define PERCPU_OP(op, asm_op)						\
static inline unsigned long __percpu_##op(void *ptr,			\
			unsigned long val, int size)			\
{									\
	unsigned long loop, ret;					\
									\
	switch (size) {							\
	case 1:								\
		asm ("//__per_cpu_" #op "_1\n"				\
		"1:	ldxrb	  %w[ret], %[ptr]\n"			\
			#asm_op " %w[ret], %w[ret], %w[val]\n"		\
		"	stxrb	  %w[loop], %w[ret], %[ptr]\n"		\
		"	cbnz	  %w[loop], 1b"				\
		: [loop] "=&r" (loop), [ret] "=&r" (ret),		\
		  [ptr] "+Q"(*(u8 *)ptr)				\
		: [val] "Ir" (val));					\
		break;							\
	case 2:								\
		asm ("//__per_cpu_" #op "_2\n"				\
		"1:	ldxrh	  %w[ret], %[ptr]\n"			\
			#asm_op " %w[ret], %w[ret], %w[val]\n"		\
		"	stxrh	  %w[loop], %w[ret], %[ptr]\n"		\
		"	cbnz	  %w[loop], 1b"				\
		: [loop] "=&r" (loop), [ret] "=&r" (ret),		\
		  [ptr]  "+Q"(*(u16 *)ptr)				\
		: [val] "Ir" (val));					\
		break;							\
	case 4:								\
		asm ("//__per_cpu_" #op "_4\n"				\
		"1:	ldxr	  %w[ret], %[ptr]\n"			\
			#asm_op " %w[ret], %w[ret], %w[val]\n"		\
		"	stxr	  %w[loop], %w[ret], %[ptr]\n"		\
		"	cbnz	  %w[loop], 1b"				\
		: [loop] "=&r" (loop), [ret] "=&r" (ret),		\
		  [ptr] "+Q"(*(u32 *)ptr)				\
		: [val] "Ir" (val));					\
		break;							\
	case 8:								\
		asm ("//__per_cpu_" #op "_8\n"				\
		"1:	ldxr	  %[ret], %[ptr]\n"			\
			#asm_op " %[ret], %[ret], %[val]\n"		\
		"	stxr	  %w[loop], %[ret], %[ptr]\n"		\
		"	cbnz	  %w[loop], 1b"				\
		: [loop] "=&r" (loop), [ret] "=&r" (ret),		\
		  [ptr] "+Q"(*(u64 *)ptr)				\
		: [val] "Ir" (val));					\
		break;							\
	default:							\
		ret = 0;						\
		BUILD_BUG();						\
	}								\
									\
	return ret;							\
}

PERCPU_OP(add, add)
PERCPU_OP(and, and)
PERCPU_OP(or, orr)
#undef PERCPU_OP

static inline unsigned long __percpu_read(void *ptr, int size)
{
	unsigned long ret;

	switch (size) {
	case 1:
		ret = READ_ONCE(*(u8 *)ptr);
		break;
	case 2:
		ret = READ_ONCE(*(u16 *)ptr);
		break;
	case 4:
		ret = READ_ONCE(*(u32 *)ptr);
		break;
	case 8:
		ret = READ_ONCE(*(u64 *)ptr);
		break;
	default:
		ret = 0;
		BUILD_BUG();
	}

	return ret;
}

static inline void __percpu_write(void *ptr, unsigned long val, int size)
{
	switch (size) {
	case 1:
		WRITE_ONCE(*(u8 *)ptr, (u8)val);
		break;
	case 2:
		WRITE_ONCE(*(u16 *)ptr, (u16)val);
		break;
	case 4:
		WRITE_ONCE(*(u32 *)ptr, (u32)val);
		break;
	case 8:
		WRITE_ONCE(*(u64 *)ptr, (u64)val);
		break;
	default:
		BUILD_BUG();
	}
}

static inline unsigned long __percpu_xchg(void *ptr, unsigned long val,
						int size)
{
	unsigned long ret, loop;

	switch (size) {
	case 1:
		asm ("//__percpu_xchg_1\n"
		"1:	ldxrb	%w[ret], %[ptr]\n"
		"	stxrb	%w[loop], %w[val], %[ptr]\n"
		"	cbnz	%w[loop], 1b"
		: [loop] "=&r"(loop), [ret] "=&r"(ret),
		  [ptr] "+Q"(*(u8 *)ptr)
		: [val] "r" (val));
		break;
	case 2:
		asm ("//__percpu_xchg_2\n"
		"1:	ldxrh	%w[ret], %[ptr]\n"
		"	stxrh	%w[loop], %w[val], %[ptr]\n"
		"	cbnz	%w[loop], 1b"
		: [loop] "=&r"(loop), [ret] "=&r"(ret),
		  [ptr] "+Q"(*(u16 *)ptr)
		: [val] "r" (val));
		break;
	case 4:
		asm ("//__percpu_xchg_4\n"
		"1:	ldxr	%w[ret], %[ptr]\n"
		"	stxr	%w[loop], %w[val], %[ptr]\n"
		"	cbnz	%w[loop], 1b"
		: [loop] "=&r"(loop), [ret] "=&r"(ret),
		  [ptr] "+Q"(*(u32 *)ptr)
		: [val] "r" (val));
		break;
	case 8:
		asm ("//__percpu_xchg_8\n"
		"1:	ldxr	%[ret], %[ptr]\n"
		"	stxr	%w[loop], %[val], %[ptr]\n"
		"	cbnz	%w[loop], 1b"
		: [loop] "=&r"(loop), [ret] "=&r"(ret),
		  [ptr] "+Q"(*(u64 *)ptr)
		: [val] "r" (val));
		break;
	default:
		ret = 0;
		BUILD_BUG();
	}

	return ret;
}

/* this_cpu_cmpxchg */
#define _protect_cmpxchg_local(pcp, o, n)			\
({								\
	typeof(*raw_cpu_ptr(&(pcp))) __ret;			\
	preempt_disable();					\
	__ret = cmpxchg_local(raw_cpu_ptr(&(pcp)), o, n);	\
	preempt_enable();					\
	__ret;							\
})

#define this_cpu_cmpxchg_1(ptr, o, n) _protect_cmpxchg_local(ptr, o, n)
#define this_cpu_cmpxchg_2(ptr, o, n) _protect_cmpxchg_local(ptr, o, n)
#define this_cpu_cmpxchg_4(ptr, o, n) _protect_cmpxchg_local(ptr, o, n)
#define this_cpu_cmpxchg_8(ptr, o, n) _protect_cmpxchg_local(ptr, o, n)

#define this_cpu_cmpxchg_double_8(ptr1, ptr2, o1, o2, n1, n2)		\
({									\
	int __ret;							\
	preempt_disable();						\
	__ret = cmpxchg_double_local(	raw_cpu_ptr(&(ptr1)),		\
					raw_cpu_ptr(&(ptr2)),		\
					o1, o2, n1, n2);		\
	preempt_enable();						\
	__ret;								\
})

#define _percpu_read(pcp)						\
({									\
	typeof(pcp) __retval;						\
	preempt_disable_notrace();					\
	__retval = (typeof(pcp))__percpu_read(raw_cpu_ptr(&(pcp)), 	\
					      sizeof(pcp));		\
	preempt_enable_notrace();					\
	__retval;							\
})

#define _percpu_write(pcp, val)						\
do {									\
	preempt_disable_notrace();					\
	__percpu_write(raw_cpu_ptr(&(pcp)), (unsigned long)(val), 	\
				sizeof(pcp));				\
	preempt_enable_notrace();					\
} while(0)								\

#define _pcp_protect(operation, pcp, val)			\
({								\
	typeof(pcp) __retval;					\
	preempt_disable();					\
	__retval = (typeof(pcp))operation(raw_cpu_ptr(&(pcp)),	\
					  (val), sizeof(pcp));	\
	preempt_enable();					\
	__retval;						\
})

#define _percpu_add(pcp, val) \
	_pcp_protect(__percpu_add, pcp, val)

#define _percpu_add_return(pcp, val) _percpu_add(pcp, val)

#define _percpu_and(pcp, val) \
	_pcp_protect(__percpu_and, pcp, val)

#define _percpu_or(pcp, val) \
	_pcp_protect(__percpu_or, pcp, val)

#define _percpu_xchg(pcp, val) (typeof(pcp)) \
	_pcp_protect(__percpu_xchg, pcp, (unsigned long)(val))

#define this_cpu_add_1(pcp, val) _percpu_add(pcp, val)
#define this_cpu_add_2(pcp, val) _percpu_add(pcp, val)
#define this_cpu_add_4(pcp, val) _percpu_add(pcp, val)
#define this_cpu_add_8(pcp, val) _percpu_add(pcp, val)

#define this_cpu_add_return_1(pcp, val) _percpu_add_return(pcp, val)
#define this_cpu_add_return_2(pcp, val) _percpu_add_return(pcp, val)
#define this_cpu_add_return_4(pcp, val) _percpu_add_return(pcp, val)
#define this_cpu_add_return_8(pcp, val) _percpu_add_return(pcp, val)

#define this_cpu_and_1(pcp, val) _percpu_and(pcp, val)
#define this_cpu_and_2(pcp, val) _percpu_and(pcp, val)
#define this_cpu_and_4(pcp, val) _percpu_and(pcp, val)
#define this_cpu_and_8(pcp, val) _percpu_and(pcp, val)

#define this_cpu_or_1(pcp, val) _percpu_or(pcp, val)
#define this_cpu_or_2(pcp, val) _percpu_or(pcp, val)
#define this_cpu_or_4(pcp, val) _percpu_or(pcp, val)
#define this_cpu_or_8(pcp, val) _percpu_or(pcp, val)

#define this_cpu_read_1(pcp) _percpu_read(pcp)
#define this_cpu_read_2(pcp) _percpu_read(pcp)
#define this_cpu_read_4(pcp) _percpu_read(pcp)
#define this_cpu_read_8(pcp) _percpu_read(pcp)

#define this_cpu_write_1(pcp, val) _percpu_write(pcp, val)
#define this_cpu_write_2(pcp, val) _percpu_write(pcp, val)
#define this_cpu_write_4(pcp, val) _percpu_write(pcp, val)
#define this_cpu_write_8(pcp, val) _percpu_write(pcp, val)

#define this_cpu_xchg_1(pcp, val) _percpu_xchg(pcp, val)
#define this_cpu_xchg_2(pcp, val) _percpu_xchg(pcp, val)
#define this_cpu_xchg_4(pcp, val) _percpu_xchg(pcp, val)
#define this_cpu_xchg_8(pcp, val) _percpu_xchg(pcp, val)

#include <asm-generic/percpu.h>

#endif /* __ASM_PERCPU_H */
