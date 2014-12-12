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

#ifdef CONFIG_SMP

static inline void set_my_cpu_offset(unsigned long off)
{
	asm volatile("msr tpidr_el1, %0" :: "r" (off) : "memory");
}

static inline unsigned long __my_cpu_offset(void)
{
	unsigned long off;

	/*
	 * We want to allow caching the value, so avoid using volatile and
	 * instead use a fake stack read to hazard against barrier().
	 */
	asm("mrs %0, tpidr_el1" : "=r" (off) :
		"Q" (*(const unsigned long *)current_stack_pointer));

	return off;
}
#define __my_cpu_offset __my_cpu_offset()

#else	/* !CONFIG_SMP */

#define set_my_cpu_offset(x)	do { } while (0)

#endif /* CONFIG_SMP */

#define PERCPU_OP(op, asm_op)						\
static inline unsigned long __percpu_##op(void *ptr,			\
			unsigned long val, int size)			\
{									\
	unsigned long loop, ret;					\
									\
	switch (size) {							\
	case 1:								\
		do {							\
			asm ("//__per_cpu_" #op "_1\n"			\
			"ldxrb	  %w[ret], %[ptr]\n"			\
			#asm_op " %w[ret], %w[ret], %w[val]\n"		\
			"stxrb	  %w[loop], %w[ret], %[ptr]\n"		\
			: [loop] "=&r" (loop), [ret] "=&r" (ret),	\
			  [ptr] "+Q"(*(u8 *)ptr)			\
			: [val] "Ir" (val));				\
		} while (loop);						\
		break;							\
	case 2:								\
		do {							\
			asm ("//__per_cpu_" #op "_2\n"			\
			"ldxrh	  %w[ret], %[ptr]\n"			\
			#asm_op " %w[ret], %w[ret], %w[val]\n"		\
			"stxrh	  %w[loop], %w[ret], %[ptr]\n"		\
			: [loop] "=&r" (loop), [ret] "=&r" (ret),	\
			  [ptr]  "+Q"(*(u16 *)ptr)			\
			: [val] "Ir" (val));				\
		} while (loop);						\
		break;							\
	case 4:								\
		do {							\
			asm ("//__per_cpu_" #op "_4\n"			\
			"ldxr	  %w[ret], %[ptr]\n"			\
			#asm_op " %w[ret], %w[ret], %w[val]\n"		\
			"stxr	  %w[loop], %w[ret], %[ptr]\n"		\
			: [loop] "=&r" (loop), [ret] "=&r" (ret),	\
			  [ptr] "+Q"(*(u32 *)ptr)			\
			: [val] "Ir" (val));				\
		} while (loop);						\
		break;							\
	case 8:								\
		do {							\
			asm ("//__per_cpu_" #op "_8\n"			\
			"ldxr	  %[ret], %[ptr]\n"			\
			#asm_op " %[ret], %[ret], %[val]\n"		\
			"stxr	  %w[loop], %[ret], %[ptr]\n"		\
			: [loop] "=&r" (loop), [ret] "=&r" (ret),	\
			  [ptr] "+Q"(*(u64 *)ptr)			\
			: [val] "Ir" (val));				\
		} while (loop);						\
		break;							\
	default:							\
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
		ret = ACCESS_ONCE(*(u8 *)ptr);
		break;
	case 2:
		ret = ACCESS_ONCE(*(u16 *)ptr);
		break;
	case 4:
		ret = ACCESS_ONCE(*(u32 *)ptr);
		break;
	case 8:
		ret = ACCESS_ONCE(*(u64 *)ptr);
		break;
	default:
		BUILD_BUG();
	}

	return ret;
}

static inline void __percpu_write(void *ptr, unsigned long val, int size)
{
	switch (size) {
	case 1:
		ACCESS_ONCE(*(u8 *)ptr) = (u8)val;
		break;
	case 2:
		ACCESS_ONCE(*(u16 *)ptr) = (u16)val;
		break;
	case 4:
		ACCESS_ONCE(*(u32 *)ptr) = (u32)val;
		break;
	case 8:
		ACCESS_ONCE(*(u64 *)ptr) = (u64)val;
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
		do {
			asm ("//__percpu_xchg_1\n"
			"ldxrb %w[ret], %[ptr]\n"
			"stxrb %w[loop], %w[val], %[ptr]\n"
			: [loop] "=&r"(loop), [ret] "=&r"(ret),
			  [ptr] "+Q"(*(u8 *)ptr)
			: [val] "r" (val));
		} while (loop);
		break;
	case 2:
		do {
			asm ("//__percpu_xchg_2\n"
			"ldxrh %w[ret], %[ptr]\n"
			"stxrh %w[loop], %w[val], %[ptr]\n"
			: [loop] "=&r"(loop), [ret] "=&r"(ret),
			  [ptr] "+Q"(*(u16 *)ptr)
			: [val] "r" (val));
		} while (loop);
		break;
	case 4:
		do {
			asm ("//__percpu_xchg_4\n"
			"ldxr %w[ret], %[ptr]\n"
			"stxr %w[loop], %w[val], %[ptr]\n"
			: [loop] "=&r"(loop), [ret] "=&r"(ret),
			  [ptr] "+Q"(*(u32 *)ptr)
			: [val] "r" (val));
		} while (loop);
		break;
	case 8:
		do {
			asm ("//__percpu_xchg_8\n"
			"ldxr %[ret], %[ptr]\n"
			"stxr %w[loop], %[val], %[ptr]\n"
			: [loop] "=&r"(loop), [ret] "=&r"(ret),
			  [ptr] "+Q"(*(u64 *)ptr)
			: [val] "r" (val));
		} while (loop);
		break;
	default:
		BUILD_BUG();
	}

	return ret;
}

#define _percpu_add(pcp, val) \
	__percpu_add(raw_cpu_ptr(&(pcp)), val, sizeof(pcp))

#define _percpu_add_return(pcp, val) (typeof(pcp)) (_percpu_add(pcp, val))

#define _percpu_and(pcp, val) \
	__percpu_and(raw_cpu_ptr(&(pcp)), val, sizeof(pcp))

#define _percpu_or(pcp, val) \
	__percpu_or(raw_cpu_ptr(&(pcp)), val, sizeof(pcp))

#define _percpu_read(pcp) (typeof(pcp))	\
	(__percpu_read(raw_cpu_ptr(&(pcp)), sizeof(pcp)))

#define _percpu_write(pcp, val) \
	__percpu_write(raw_cpu_ptr(&(pcp)), (unsigned long)(val), sizeof(pcp))

#define _percpu_xchg(pcp, val) (typeof(pcp)) \
	(__percpu_xchg(raw_cpu_ptr(&(pcp)), (unsigned long)(val), sizeof(pcp)))

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
