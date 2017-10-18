/*
 * Handle unaligned accesses by emulation.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1998, 1999, 2002 by Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) 2014 Imagination Technologies Ltd.
 *
 * This file contains exception handler for address error exception with the
 * special capability to execute faulting instructions in software.  The
 * handler does not try to handle the case when the program counter points
 * to an address not aligned to a word boundary.
 *
 * Putting data to unaligned addresses is a bad practice even on Intel where
 * only the performance is affected.  Much worse is that such code is non-
 * portable.  Due to several programs that die on MIPS due to alignment
 * problems I decided to implement this handler anyway though I originally
 * didn't intend to do this at all for user code.
 *
 * For now I enable fixing of address errors by default to make life easier.
 * I however intend to disable this somewhen in the future when the alignment
 * problems with user programs have been fixed.	 For programmers this is the
 * right way to go.
 *
 * Fixing address errors is a per process option.  The option is inherited
 * across fork(2) and execve(2) calls.	If you really want to use the
 * option in your user programs - I discourage the use of the software
 * emulation strongly - use the following code in your userland stuff:
 *
 * #include <sys/sysmips.h>
 *
 * ...
 * sysmips(MIPS_FIXADE, x);
 * ...
 *
 * The argument x is 0 for disabling software emulation, enabled otherwise.
 *
 * Below a little program to play around with this feature.
 *
 * #include <stdio.h>
 * #include <sys/sysmips.h>
 *
 * struct foo {
 *	   unsigned char bar[8];
 * };
 *
 * main(int argc, char *argv[])
 * {
 *	   struct foo x = {0, 1, 2, 3, 4, 5, 6, 7};
 *	   unsigned int *p = (unsigned int *) (x.bar + 3);
 *	   int i;
 *
 *	   if (argc > 1)
 *		   sysmips(MIPS_FIXADE, atoi(argv[1]));
 *
 *	   printf("*p = %08lx\n", *p);
 *
 *	   *p = 0xdeadface;
 *
 *	   for(i = 0; i <= 7; i++)
 *	   printf("%02x ", x.bar[i]);
 *	   printf("\n");
 * }
 *
 * Coprocessor loads are not supported; I think this case is unimportant
 * in the practice.
 *
 * TODO: Handle ndc (attempted store to doubleword in uncached memory)
 *	 exception for the R6000.
 *	 A store crossing a page boundary might be executed only partially.
 *	 Undo the partial store in this case.
 */
#include <linux/context_tracking.h>
#include <linux/mm.h>
#include <linux/signal.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/debugfs.h>
#include <linux/perf_event.h>

#include <asm/asm.h>
#include <asm/branch.h>
#include <asm/byteorder.h>
#include <asm/cop2.h>
#include <asm/debug.h>
#include <asm/fpu.h>
#include <asm/fpu_emulator.h>
#include <asm/inst.h>
#include <linux/uaccess.h>

#define STR(x)	__STR(x)
#define __STR(x)  #x

enum {
	UNALIGNED_ACTION_QUIET,
	UNALIGNED_ACTION_SIGNAL,
	UNALIGNED_ACTION_SHOW,
};
#ifdef CONFIG_DEBUG_FS
static u32 unaligned_instructions;
static u32 unaligned_action;
#else
#define unaligned_action UNALIGNED_ACTION_QUIET
#endif
extern void show_registers(struct pt_regs *regs);

#ifdef __BIG_ENDIAN
#define     _LoadHW(addr, value, res, type)  \
do {                                                        \
		__asm__ __volatile__ (".set\tnoat\n"        \
			"1:\t"type##_lb("%0", "0(%2)")"\n"  \
			"2:\t"type##_lbu("$1", "1(%2)")"\n\t"\
			"sll\t%0, 0x8\n\t"                  \
			"or\t%0, $1\n\t"                    \
			"li\t%1, 0\n"                       \
			"3:\t.set\tat\n\t"                  \
			".insn\n\t"                         \
			".section\t.fixup,\"ax\"\n\t"       \
			"4:\tli\t%1, %3\n\t"                \
			"j\t3b\n\t"                         \
			".previous\n\t"                     \
			".section\t__ex_table,\"a\"\n\t"    \
			STR(PTR)"\t1b, 4b\n\t"              \
			STR(PTR)"\t2b, 4b\n\t"              \
			".previous"                         \
			: "=&r" (value), "=r" (res)         \
			: "r" (addr), "i" (-EFAULT));       \
} while(0)

#ifndef CONFIG_CPU_MIPSR6
#define     _LoadW(addr, value, res, type)   \
do {                                                        \
		__asm__ __volatile__ (                      \
			"1:\t"type##_lwl("%0", "(%2)")"\n"   \
			"2:\t"type##_lwr("%0", "3(%2)")"\n\t"\
			"li\t%1, 0\n"                       \
			"3:\n\t"                            \
			".insn\n\t"                         \
			".section\t.fixup,\"ax\"\n\t"       \
			"4:\tli\t%1, %3\n\t"                \
			"j\t3b\n\t"                         \
			".previous\n\t"                     \
			".section\t__ex_table,\"a\"\n\t"    \
			STR(PTR)"\t1b, 4b\n\t"              \
			STR(PTR)"\t2b, 4b\n\t"              \
			".previous"                         \
			: "=&r" (value), "=r" (res)         \
			: "r" (addr), "i" (-EFAULT));       \
} while(0)

#else
/* MIPSR6 has no lwl instruction */
#define     _LoadW(addr, value, res, type) \
do {                                                        \
		__asm__ __volatile__ (			    \
			".set\tpush\n"			    \
			".set\tnoat\n\t"		    \
			"1:"type##_lb("%0", "0(%2)")"\n\t"  \
			"2:"type##_lbu("$1", "1(%2)")"\n\t" \
			"sll\t%0, 0x8\n\t"		    \
			"or\t%0, $1\n\t"		    \
			"3:"type##_lbu("$1", "2(%2)")"\n\t" \
			"sll\t%0, 0x8\n\t"		    \
			"or\t%0, $1\n\t"		    \
			"4:"type##_lbu("$1", "3(%2)")"\n\t" \
			"sll\t%0, 0x8\n\t"		    \
			"or\t%0, $1\n\t"		    \
			"li\t%1, 0\n"			    \
			".set\tpop\n"			    \
			"10:\n\t"			    \
			".insn\n\t"			    \
			".section\t.fixup,\"ax\"\n\t"	    \
			"11:\tli\t%1, %3\n\t"		    \
			"j\t10b\n\t"			    \
			".previous\n\t"			    \
			".section\t__ex_table,\"a\"\n\t"    \
			STR(PTR)"\t1b, 11b\n\t"		    \
			STR(PTR)"\t2b, 11b\n\t"		    \
			STR(PTR)"\t3b, 11b\n\t"		    \
			STR(PTR)"\t4b, 11b\n\t"		    \
			".previous"			    \
			: "=&r" (value), "=r" (res)	    \
			: "r" (addr), "i" (-EFAULT));       \
} while(0)

#endif /* CONFIG_CPU_MIPSR6 */

#define     _LoadHWU(addr, value, res, type) \
do {                                                        \
		__asm__ __volatile__ (                      \
			".set\tnoat\n"                      \
			"1:\t"type##_lbu("%0", "0(%2)")"\n" \
			"2:\t"type##_lbu("$1", "1(%2)")"\n\t"\
			"sll\t%0, 0x8\n\t"                  \
			"or\t%0, $1\n\t"                    \
			"li\t%1, 0\n"                       \
			"3:\n\t"                            \
			".insn\n\t"                         \
			".set\tat\n\t"                      \
			".section\t.fixup,\"ax\"\n\t"       \
			"4:\tli\t%1, %3\n\t"                \
			"j\t3b\n\t"                         \
			".previous\n\t"                     \
			".section\t__ex_table,\"a\"\n\t"    \
			STR(PTR)"\t1b, 4b\n\t"              \
			STR(PTR)"\t2b, 4b\n\t"              \
			".previous"                         \
			: "=&r" (value), "=r" (res)         \
			: "r" (addr), "i" (-EFAULT));       \
} while(0)

#ifndef CONFIG_CPU_MIPSR6
#define     _LoadWU(addr, value, res, type)  \
do {                                                        \
		__asm__ __volatile__ (                      \
			"1:\t"type##_lwl("%0", "(%2)")"\n"  \
			"2:\t"type##_lwr("%0", "3(%2)")"\n\t"\
			"dsll\t%0, %0, 32\n\t"              \
			"dsrl\t%0, %0, 32\n\t"              \
			"li\t%1, 0\n"                       \
			"3:\n\t"                            \
			".insn\n\t"                         \
			"\t.section\t.fixup,\"ax\"\n\t"     \
			"4:\tli\t%1, %3\n\t"                \
			"j\t3b\n\t"                         \
			".previous\n\t"                     \
			".section\t__ex_table,\"a\"\n\t"    \
			STR(PTR)"\t1b, 4b\n\t"              \
			STR(PTR)"\t2b, 4b\n\t"              \
			".previous"                         \
			: "=&r" (value), "=r" (res)         \
			: "r" (addr), "i" (-EFAULT));       \
} while(0)

#define     _LoadDW(addr, value, res)  \
do {                                                        \
		__asm__ __volatile__ (                      \
			"1:\tldl\t%0, (%2)\n"               \
			"2:\tldr\t%0, 7(%2)\n\t"            \
			"li\t%1, 0\n"                       \
			"3:\n\t"                            \
			".insn\n\t"                         \
			"\t.section\t.fixup,\"ax\"\n\t"     \
			"4:\tli\t%1, %3\n\t"                \
			"j\t3b\n\t"                         \
			".previous\n\t"                     \
			".section\t__ex_table,\"a\"\n\t"    \
			STR(PTR)"\t1b, 4b\n\t"              \
			STR(PTR)"\t2b, 4b\n\t"              \
			".previous"                         \
			: "=&r" (value), "=r" (res)         \
			: "r" (addr), "i" (-EFAULT));       \
} while(0)

#else
/* MIPSR6 has not lwl and ldl instructions */
#define	    _LoadWU(addr, value, res, type) \
do {                                                        \
		__asm__ __volatile__ (			    \
			".set\tpush\n\t"		    \
			".set\tnoat\n\t"		    \
			"1:"type##_lbu("%0", "0(%2)")"\n\t" \
			"2:"type##_lbu("$1", "1(%2)")"\n\t" \
			"sll\t%0, 0x8\n\t"		    \
			"or\t%0, $1\n\t"		    \
			"3:"type##_lbu("$1", "2(%2)")"\n\t" \
			"sll\t%0, 0x8\n\t"		    \
			"or\t%0, $1\n\t"		    \
			"4:"type##_lbu("$1", "3(%2)")"\n\t" \
			"sll\t%0, 0x8\n\t"		    \
			"or\t%0, $1\n\t"		    \
			"li\t%1, 0\n"			    \
			".set\tpop\n"			    \
			"10:\n\t"			    \
			".insn\n\t"			    \
			".section\t.fixup,\"ax\"\n\t"	    \
			"11:\tli\t%1, %3\n\t"		    \
			"j\t10b\n\t"			    \
			".previous\n\t"			    \
			".section\t__ex_table,\"a\"\n\t"    \
			STR(PTR)"\t1b, 11b\n\t"		    \
			STR(PTR)"\t2b, 11b\n\t"		    \
			STR(PTR)"\t3b, 11b\n\t"		    \
			STR(PTR)"\t4b, 11b\n\t"		    \
			".previous"			    \
			: "=&r" (value), "=r" (res)	    \
			: "r" (addr), "i" (-EFAULT));       \
} while(0)

#define     _LoadDW(addr, value, res)  \
do {                                                        \
		__asm__ __volatile__ (			    \
			".set\tpush\n\t"		    \
			".set\tnoat\n\t"		    \
			"1:lb\t%0, 0(%2)\n\t"    	    \
			"2:lbu\t $1, 1(%2)\n\t"   	    \
			"dsll\t%0, 0x8\n\t"		    \
			"or\t%0, $1\n\t"		    \
			"3:lbu\t$1, 2(%2)\n\t"   	    \
			"dsll\t%0, 0x8\n\t"		    \
			"or\t%0, $1\n\t"		    \
			"4:lbu\t$1, 3(%2)\n\t"   	    \
			"dsll\t%0, 0x8\n\t"		    \
			"or\t%0, $1\n\t"		    \
			"5:lbu\t$1, 4(%2)\n\t"   	    \
			"dsll\t%0, 0x8\n\t"		    \
			"or\t%0, $1\n\t"		    \
			"6:lbu\t$1, 5(%2)\n\t"   	    \
			"dsll\t%0, 0x8\n\t"		    \
			"or\t%0, $1\n\t"		    \
			"7:lbu\t$1, 6(%2)\n\t"   	    \
			"dsll\t%0, 0x8\n\t"		    \
			"or\t%0, $1\n\t"		    \
			"8:lbu\t$1, 7(%2)\n\t"   	    \
			"dsll\t%0, 0x8\n\t"		    \
			"or\t%0, $1\n\t"		    \
			"li\t%1, 0\n"			    \
			".set\tpop\n\t"			    \
			"10:\n\t"			    \
			".insn\n\t"			    \
			".section\t.fixup,\"ax\"\n\t"	    \
			"11:\tli\t%1, %3\n\t"		    \
			"j\t10b\n\t"			    \
			".previous\n\t"			    \
			".section\t__ex_table,\"a\"\n\t"    \
			STR(PTR)"\t1b, 11b\n\t"		    \
			STR(PTR)"\t2b, 11b\n\t"		    \
			STR(PTR)"\t3b, 11b\n\t"		    \
			STR(PTR)"\t4b, 11b\n\t"		    \
			STR(PTR)"\t5b, 11b\n\t"		    \
			STR(PTR)"\t6b, 11b\n\t"		    \
			STR(PTR)"\t7b, 11b\n\t"		    \
			STR(PTR)"\t8b, 11b\n\t"		    \
			".previous"			    \
			: "=&r" (value), "=r" (res)	    \
			: "r" (addr), "i" (-EFAULT));       \
} while(0)

#endif /* CONFIG_CPU_MIPSR6 */


#define     _StoreHW(addr, value, res, type) \
do {                                                        \
		__asm__ __volatile__ (                      \
			".set\tnoat\n"                      \
			"1:\t"type##_sb("%1", "1(%2)")"\n"  \
			"srl\t$1, %1, 0x8\n"                \
			"2:\t"type##_sb("$1", "0(%2)")"\n"  \
			".set\tat\n\t"                      \
			"li\t%0, 0\n"                       \
			"3:\n\t"                            \
			".insn\n\t"                         \
			".section\t.fixup,\"ax\"\n\t"       \
			"4:\tli\t%0, %3\n\t"                \
			"j\t3b\n\t"                         \
			".previous\n\t"                     \
			".section\t__ex_table,\"a\"\n\t"    \
			STR(PTR)"\t1b, 4b\n\t"              \
			STR(PTR)"\t2b, 4b\n\t"              \
			".previous"                         \
			: "=r" (res)                        \
			: "r" (value), "r" (addr), "i" (-EFAULT));\
} while(0)

#ifndef CONFIG_CPU_MIPSR6
#define     _StoreW(addr, value, res, type)  \
do {                                                        \
		__asm__ __volatile__ (                      \
			"1:\t"type##_swl("%1", "(%2)")"\n"  \
			"2:\t"type##_swr("%1", "3(%2)")"\n\t"\
			"li\t%0, 0\n"                       \
			"3:\n\t"                            \
			".insn\n\t"                         \
			".section\t.fixup,\"ax\"\n\t"       \
			"4:\tli\t%0, %3\n\t"                \
			"j\t3b\n\t"                         \
			".previous\n\t"                     \
			".section\t__ex_table,\"a\"\n\t"    \
			STR(PTR)"\t1b, 4b\n\t"              \
			STR(PTR)"\t2b, 4b\n\t"              \
			".previous"                         \
		: "=r" (res)                                \
		: "r" (value), "r" (addr), "i" (-EFAULT));  \
} while(0)

#define     _StoreDW(addr, value, res) \
do {                                                        \
		__asm__ __volatile__ (                      \
			"1:\tsdl\t%1,(%2)\n"                \
			"2:\tsdr\t%1, 7(%2)\n\t"            \
			"li\t%0, 0\n"                       \
			"3:\n\t"                            \
			".insn\n\t"                         \
			".section\t.fixup,\"ax\"\n\t"       \
			"4:\tli\t%0, %3\n\t"                \
			"j\t3b\n\t"                         \
			".previous\n\t"                     \
			".section\t__ex_table,\"a\"\n\t"    \
			STR(PTR)"\t1b, 4b\n\t"              \
			STR(PTR)"\t2b, 4b\n\t"              \
			".previous"                         \
		: "=r" (res)                                \
		: "r" (value), "r" (addr), "i" (-EFAULT));  \
} while(0)

#else
/* MIPSR6 has no swl and sdl instructions */
#define     _StoreW(addr, value, res, type)  \
do {                                                        \
		__asm__ __volatile__ (                      \
			".set\tpush\n\t"		    \
			".set\tnoat\n\t"		    \
			"1:"type##_sb("%1", "3(%2)")"\n\t"  \
			"srl\t$1, %1, 0x8\n\t"		    \
			"2:"type##_sb("$1", "2(%2)")"\n\t"  \
			"srl\t$1, $1,  0x8\n\t"		    \
			"3:"type##_sb("$1", "1(%2)")"\n\t"  \
			"srl\t$1, $1, 0x8\n\t"		    \
			"4:"type##_sb("$1", "0(%2)")"\n\t"  \
			".set\tpop\n\t"			    \
			"li\t%0, 0\n"			    \
			"10:\n\t"			    \
			".insn\n\t"			    \
			".section\t.fixup,\"ax\"\n\t"	    \
			"11:\tli\t%0, %3\n\t"		    \
			"j\t10b\n\t"			    \
			".previous\n\t"			    \
			".section\t__ex_table,\"a\"\n\t"    \
			STR(PTR)"\t1b, 11b\n\t"		    \
			STR(PTR)"\t2b, 11b\n\t"		    \
			STR(PTR)"\t3b, 11b\n\t"		    \
			STR(PTR)"\t4b, 11b\n\t"		    \
			".previous"			    \
		: "=&r" (res)			    	    \
		: "r" (value), "r" (addr), "i" (-EFAULT)    \
		: "memory");                                \
} while(0)

#define     _StoreDW(addr, value, res) \
do {                                                        \
		__asm__ __volatile__ (                      \
			".set\tpush\n\t"		    \
			".set\tnoat\n\t"		    \
			"1:sb\t%1, 7(%2)\n\t"    	    \
			"dsrl\t$1, %1, 0x8\n\t"		    \
			"2:sb\t$1, 6(%2)\n\t"    	    \
			"dsrl\t$1, $1, 0x8\n\t"		    \
			"3:sb\t$1, 5(%2)\n\t"    	    \
			"dsrl\t$1, $1, 0x8\n\t"		    \
			"4:sb\t$1, 4(%2)\n\t"    	    \
			"dsrl\t$1, $1, 0x8\n\t"		    \
			"5:sb\t$1, 3(%2)\n\t"    	    \
			"dsrl\t$1, $1, 0x8\n\t"		    \
			"6:sb\t$1, 2(%2)\n\t"    	    \
			"dsrl\t$1, $1, 0x8\n\t"		    \
			"7:sb\t$1, 1(%2)\n\t"    	    \
			"dsrl\t$1, $1, 0x8\n\t"		    \
			"8:sb\t$1, 0(%2)\n\t"    	    \
			"dsrl\t$1, $1, 0x8\n\t"		    \
			".set\tpop\n\t"			    \
			"li\t%0, 0\n"			    \
			"10:\n\t"			    \
			".insn\n\t"			    \
			".section\t.fixup,\"ax\"\n\t"	    \
			"11:\tli\t%0, %3\n\t"		    \
			"j\t10b\n\t"			    \
			".previous\n\t"			    \
			".section\t__ex_table,\"a\"\n\t"    \
			STR(PTR)"\t1b, 11b\n\t"		    \
			STR(PTR)"\t2b, 11b\n\t"		    \
			STR(PTR)"\t3b, 11b\n\t"		    \
			STR(PTR)"\t4b, 11b\n\t"		    \
			STR(PTR)"\t5b, 11b\n\t"		    \
			STR(PTR)"\t6b, 11b\n\t"		    \
			STR(PTR)"\t7b, 11b\n\t"		    \
			STR(PTR)"\t8b, 11b\n\t"		    \
			".previous"			    \
		: "=&r" (res)			    	    \
		: "r" (value), "r" (addr), "i" (-EFAULT)    \
		: "memory");                                \
} while(0)

#endif /* CONFIG_CPU_MIPSR6 */

#else /* __BIG_ENDIAN */

#define     _LoadHW(addr, value, res, type)  \
do {                                                        \
		__asm__ __volatile__ (".set\tnoat\n"        \
			"1:\t"type##_lb("%0", "1(%2)")"\n"  \
			"2:\t"type##_lbu("$1", "0(%2)")"\n\t"\
			"sll\t%0, 0x8\n\t"                  \
			"or\t%0, $1\n\t"                    \
			"li\t%1, 0\n"                       \
			"3:\t.set\tat\n\t"                  \
			".insn\n\t"                         \
			".section\t.fixup,\"ax\"\n\t"       \
			"4:\tli\t%1, %3\n\t"                \
			"j\t3b\n\t"                         \
			".previous\n\t"                     \
			".section\t__ex_table,\"a\"\n\t"    \
			STR(PTR)"\t1b, 4b\n\t"              \
			STR(PTR)"\t2b, 4b\n\t"              \
			".previous"                         \
			: "=&r" (value), "=r" (res)         \
			: "r" (addr), "i" (-EFAULT));       \
} while(0)

#ifndef CONFIG_CPU_MIPSR6
#define     _LoadW(addr, value, res, type)   \
do {                                                        \
		__asm__ __volatile__ (                      \
			"1:\t"type##_lwl("%0", "3(%2)")"\n" \
			"2:\t"type##_lwr("%0", "(%2)")"\n\t"\
			"li\t%1, 0\n"                       \
			"3:\n\t"                            \
			".insn\n\t"                         \
			".section\t.fixup,\"ax\"\n\t"       \
			"4:\tli\t%1, %3\n\t"                \
			"j\t3b\n\t"                         \
			".previous\n\t"                     \
			".section\t__ex_table,\"a\"\n\t"    \
			STR(PTR)"\t1b, 4b\n\t"              \
			STR(PTR)"\t2b, 4b\n\t"              \
			".previous"                         \
			: "=&r" (value), "=r" (res)         \
			: "r" (addr), "i" (-EFAULT));       \
} while(0)

#else
/* MIPSR6 has no lwl instruction */
#define     _LoadW(addr, value, res, type) \
do {                                                        \
		__asm__ __volatile__ (			    \
			".set\tpush\n"			    \
			".set\tnoat\n\t"		    \
			"1:"type##_lb("%0", "3(%2)")"\n\t"  \
			"2:"type##_lbu("$1", "2(%2)")"\n\t" \
			"sll\t%0, 0x8\n\t"		    \
			"or\t%0, $1\n\t"		    \
			"3:"type##_lbu("$1", "1(%2)")"\n\t" \
			"sll\t%0, 0x8\n\t"		    \
			"or\t%0, $1\n\t"		    \
			"4:"type##_lbu("$1", "0(%2)")"\n\t" \
			"sll\t%0, 0x8\n\t"		    \
			"or\t%0, $1\n\t"		    \
			"li\t%1, 0\n"			    \
			".set\tpop\n"			    \
			"10:\n\t"			    \
			".insn\n\t"			    \
			".section\t.fixup,\"ax\"\n\t"	    \
			"11:\tli\t%1, %3\n\t"		    \
			"j\t10b\n\t"			    \
			".previous\n\t"			    \
			".section\t__ex_table,\"a\"\n\t"    \
			STR(PTR)"\t1b, 11b\n\t"		    \
			STR(PTR)"\t2b, 11b\n\t"		    \
			STR(PTR)"\t3b, 11b\n\t"		    \
			STR(PTR)"\t4b, 11b\n\t"		    \
			".previous"			    \
			: "=&r" (value), "=r" (res)	    \
			: "r" (addr), "i" (-EFAULT));       \
} while(0)

#endif /* CONFIG_CPU_MIPSR6 */


#define     _LoadHWU(addr, value, res, type) \
do {                                                        \
		__asm__ __volatile__ (                      \
			".set\tnoat\n"                      \
			"1:\t"type##_lbu("%0", "1(%2)")"\n" \
			"2:\t"type##_lbu("$1", "0(%2)")"\n\t"\
			"sll\t%0, 0x8\n\t"                  \
			"or\t%0, $1\n\t"                    \
			"li\t%1, 0\n"                       \
			"3:\n\t"                            \
			".insn\n\t"                         \
			".set\tat\n\t"                      \
			".section\t.fixup,\"ax\"\n\t"       \
			"4:\tli\t%1, %3\n\t"                \
			"j\t3b\n\t"                         \
			".previous\n\t"                     \
			".section\t__ex_table,\"a\"\n\t"    \
			STR(PTR)"\t1b, 4b\n\t"              \
			STR(PTR)"\t2b, 4b\n\t"              \
			".previous"                         \
			: "=&r" (value), "=r" (res)         \
			: "r" (addr), "i" (-EFAULT));       \
} while(0)

#ifndef CONFIG_CPU_MIPSR6
#define     _LoadWU(addr, value, res, type)  \
do {                                                        \
		__asm__ __volatile__ (                      \
			"1:\t"type##_lwl("%0", "3(%2)")"\n" \
			"2:\t"type##_lwr("%0", "(%2)")"\n\t"\
			"dsll\t%0, %0, 32\n\t"              \
			"dsrl\t%0, %0, 32\n\t"              \
			"li\t%1, 0\n"                       \
			"3:\n\t"                            \
			".insn\n\t"                         \
			"\t.section\t.fixup,\"ax\"\n\t"     \
			"4:\tli\t%1, %3\n\t"                \
			"j\t3b\n\t"                         \
			".previous\n\t"                     \
			".section\t__ex_table,\"a\"\n\t"    \
			STR(PTR)"\t1b, 4b\n\t"              \
			STR(PTR)"\t2b, 4b\n\t"              \
			".previous"                         \
			: "=&r" (value), "=r" (res)         \
			: "r" (addr), "i" (-EFAULT));       \
} while(0)

#define     _LoadDW(addr, value, res)  \
do {                                                        \
		__asm__ __volatile__ (                      \
			"1:\tldl\t%0, 7(%2)\n"              \
			"2:\tldr\t%0, (%2)\n\t"             \
			"li\t%1, 0\n"                       \
			"3:\n\t"                            \
			".insn\n\t"                         \
			"\t.section\t.fixup,\"ax\"\n\t"     \
			"4:\tli\t%1, %3\n\t"                \
			"j\t3b\n\t"                         \
			".previous\n\t"                     \
			".section\t__ex_table,\"a\"\n\t"    \
			STR(PTR)"\t1b, 4b\n\t"              \
			STR(PTR)"\t2b, 4b\n\t"              \
			".previous"                         \
			: "=&r" (value), "=r" (res)         \
			: "r" (addr), "i" (-EFAULT));       \
} while(0)

#else
/* MIPSR6 has not lwl and ldl instructions */
#define	    _LoadWU(addr, value, res, type) \
do {                                                        \
		__asm__ __volatile__ (			    \
			".set\tpush\n\t"		    \
			".set\tnoat\n\t"		    \
			"1:"type##_lbu("%0", "3(%2)")"\n\t" \
			"2:"type##_lbu("$1", "2(%2)")"\n\t" \
			"sll\t%0, 0x8\n\t"		    \
			"or\t%0, $1\n\t"		    \
			"3:"type##_lbu("$1", "1(%2)")"\n\t" \
			"sll\t%0, 0x8\n\t"		    \
			"or\t%0, $1\n\t"		    \
			"4:"type##_lbu("$1", "0(%2)")"\n\t" \
			"sll\t%0, 0x8\n\t"		    \
			"or\t%0, $1\n\t"		    \
			"li\t%1, 0\n"			    \
			".set\tpop\n"			    \
			"10:\n\t"			    \
			".insn\n\t"			    \
			".section\t.fixup,\"ax\"\n\t"	    \
			"11:\tli\t%1, %3\n\t"		    \
			"j\t10b\n\t"			    \
			".previous\n\t"			    \
			".section\t__ex_table,\"a\"\n\t"    \
			STR(PTR)"\t1b, 11b\n\t"		    \
			STR(PTR)"\t2b, 11b\n\t"		    \
			STR(PTR)"\t3b, 11b\n\t"		    \
			STR(PTR)"\t4b, 11b\n\t"		    \
			".previous"			    \
			: "=&r" (value), "=r" (res)	    \
			: "r" (addr), "i" (-EFAULT));       \
} while(0)

#define     _LoadDW(addr, value, res)  \
do {                                                        \
		__asm__ __volatile__ (			    \
			".set\tpush\n\t"		    \
			".set\tnoat\n\t"		    \
			"1:lb\t%0, 7(%2)\n\t"    	    \
			"2:lbu\t$1, 6(%2)\n\t"   	    \
			"dsll\t%0, 0x8\n\t"		    \
			"or\t%0, $1\n\t"		    \
			"3:lbu\t$1, 5(%2)\n\t"   	    \
			"dsll\t%0, 0x8\n\t"		    \
			"or\t%0, $1\n\t"		    \
			"4:lbu\t$1, 4(%2)\n\t"   	    \
			"dsll\t%0, 0x8\n\t"		    \
			"or\t%0, $1\n\t"		    \
			"5:lbu\t$1, 3(%2)\n\t"   	    \
			"dsll\t%0, 0x8\n\t"		    \
			"or\t%0, $1\n\t"		    \
			"6:lbu\t$1, 2(%2)\n\t"   	    \
			"dsll\t%0, 0x8\n\t"		    \
			"or\t%0, $1\n\t"		    \
			"7:lbu\t$1, 1(%2)\n\t"   	    \
			"dsll\t%0, 0x8\n\t"		    \
			"or\t%0, $1\n\t"		    \
			"8:lbu\t$1, 0(%2)\n\t"   	    \
			"dsll\t%0, 0x8\n\t"		    \
			"or\t%0, $1\n\t"		    \
			"li\t%1, 0\n"			    \
			".set\tpop\n\t"			    \
			"10:\n\t"			    \
			".insn\n\t"			    \
			".section\t.fixup,\"ax\"\n\t"	    \
			"11:\tli\t%1, %3\n\t"		    \
			"j\t10b\n\t"			    \
			".previous\n\t"			    \
			".section\t__ex_table,\"a\"\n\t"    \
			STR(PTR)"\t1b, 11b\n\t"		    \
			STR(PTR)"\t2b, 11b\n\t"		    \
			STR(PTR)"\t3b, 11b\n\t"		    \
			STR(PTR)"\t4b, 11b\n\t"		    \
			STR(PTR)"\t5b, 11b\n\t"		    \
			STR(PTR)"\t6b, 11b\n\t"		    \
			STR(PTR)"\t7b, 11b\n\t"		    \
			STR(PTR)"\t8b, 11b\n\t"		    \
			".previous"			    \
			: "=&r" (value), "=r" (res)	    \
			: "r" (addr), "i" (-EFAULT));       \
} while(0)
#endif /* CONFIG_CPU_MIPSR6 */

#define     _StoreHW(addr, value, res, type) \
do {                                                        \
		__asm__ __volatile__ (                      \
			".set\tnoat\n"                      \
			"1:\t"type##_sb("%1", "0(%2)")"\n"  \
			"srl\t$1,%1, 0x8\n"                 \
			"2:\t"type##_sb("$1", "1(%2)")"\n"  \
			".set\tat\n\t"                      \
			"li\t%0, 0\n"                       \
			"3:\n\t"                            \
			".insn\n\t"                         \
			".section\t.fixup,\"ax\"\n\t"       \
			"4:\tli\t%0, %3\n\t"                \
			"j\t3b\n\t"                         \
			".previous\n\t"                     \
			".section\t__ex_table,\"a\"\n\t"    \
			STR(PTR)"\t1b, 4b\n\t"              \
			STR(PTR)"\t2b, 4b\n\t"              \
			".previous"                         \
			: "=r" (res)                        \
			: "r" (value), "r" (addr), "i" (-EFAULT));\
} while(0)

#ifndef CONFIG_CPU_MIPSR6
#define     _StoreW(addr, value, res, type)  \
do {                                                        \
		__asm__ __volatile__ (                      \
			"1:\t"type##_swl("%1", "3(%2)")"\n" \
			"2:\t"type##_swr("%1", "(%2)")"\n\t"\
			"li\t%0, 0\n"                       \
			"3:\n\t"                            \
			".insn\n\t"                         \
			".section\t.fixup,\"ax\"\n\t"       \
			"4:\tli\t%0, %3\n\t"                \
			"j\t3b\n\t"                         \
			".previous\n\t"                     \
			".section\t__ex_table,\"a\"\n\t"    \
			STR(PTR)"\t1b, 4b\n\t"              \
			STR(PTR)"\t2b, 4b\n\t"              \
			".previous"                         \
		: "=r" (res)                                \
		: "r" (value), "r" (addr), "i" (-EFAULT));  \
} while(0)

#define     _StoreDW(addr, value, res) \
do {                                                        \
		__asm__ __volatile__ (                      \
			"1:\tsdl\t%1, 7(%2)\n"              \
			"2:\tsdr\t%1, (%2)\n\t"             \
			"li\t%0, 0\n"                       \
			"3:\n\t"                            \
			".insn\n\t"                         \
			".section\t.fixup,\"ax\"\n\t"       \
			"4:\tli\t%0, %3\n\t"                \
			"j\t3b\n\t"                         \
			".previous\n\t"                     \
			".section\t__ex_table,\"a\"\n\t"    \
			STR(PTR)"\t1b, 4b\n\t"              \
			STR(PTR)"\t2b, 4b\n\t"              \
			".previous"                         \
		: "=r" (res)                                \
		: "r" (value), "r" (addr), "i" (-EFAULT));  \
} while(0)

#else
/* MIPSR6 has no swl and sdl instructions */
#define     _StoreW(addr, value, res, type)  \
do {                                                        \
		__asm__ __volatile__ (                      \
			".set\tpush\n\t"		    \
			".set\tnoat\n\t"		    \
			"1:"type##_sb("%1", "0(%2)")"\n\t"  \
			"srl\t$1, %1, 0x8\n\t"		    \
			"2:"type##_sb("$1", "1(%2)")"\n\t"  \
			"srl\t$1, $1,  0x8\n\t"		    \
			"3:"type##_sb("$1", "2(%2)")"\n\t"  \
			"srl\t$1, $1, 0x8\n\t"		    \
			"4:"type##_sb("$1", "3(%2)")"\n\t"  \
			".set\tpop\n\t"			    \
			"li\t%0, 0\n"			    \
			"10:\n\t"			    \
			".insn\n\t"			    \
			".section\t.fixup,\"ax\"\n\t"	    \
			"11:\tli\t%0, %3\n\t"		    \
			"j\t10b\n\t"			    \
			".previous\n\t"			    \
			".section\t__ex_table,\"a\"\n\t"    \
			STR(PTR)"\t1b, 11b\n\t"		    \
			STR(PTR)"\t2b, 11b\n\t"		    \
			STR(PTR)"\t3b, 11b\n\t"		    \
			STR(PTR)"\t4b, 11b\n\t"		    \
			".previous"			    \
		: "=&r" (res)			    	    \
		: "r" (value), "r" (addr), "i" (-EFAULT)    \
		: "memory");                                \
} while(0)

#define     _StoreDW(addr, value, res) \
do {                                                        \
		__asm__ __volatile__ (                      \
			".set\tpush\n\t"		    \
			".set\tnoat\n\t"		    \
			"1:sb\t%1, 0(%2)\n\t"    	    \
			"dsrl\t$1, %1, 0x8\n\t"		    \
			"2:sb\t$1, 1(%2)\n\t"    	    \
			"dsrl\t$1, $1, 0x8\n\t"		    \
			"3:sb\t$1, 2(%2)\n\t"    	    \
			"dsrl\t$1, $1, 0x8\n\t"		    \
			"4:sb\t$1, 3(%2)\n\t"    	    \
			"dsrl\t$1, $1, 0x8\n\t"		    \
			"5:sb\t$1, 4(%2)\n\t"    	    \
			"dsrl\t$1, $1, 0x8\n\t"		    \
			"6:sb\t$1, 5(%2)\n\t"    	    \
			"dsrl\t$1, $1, 0x8\n\t"		    \
			"7:sb\t$1, 6(%2)\n\t"    	    \
			"dsrl\t$1, $1, 0x8\n\t"		    \
			"8:sb\t$1, 7(%2)\n\t"    	    \
			"dsrl\t$1, $1, 0x8\n\t"		    \
			".set\tpop\n\t"			    \
			"li\t%0, 0\n"			    \
			"10:\n\t"			    \
			".insn\n\t"			    \
			".section\t.fixup,\"ax\"\n\t"	    \
			"11:\tli\t%0, %3\n\t"		    \
			"j\t10b\n\t"			    \
			".previous\n\t"			    \
			".section\t__ex_table,\"a\"\n\t"    \
			STR(PTR)"\t1b, 11b\n\t"		    \
			STR(PTR)"\t2b, 11b\n\t"		    \
			STR(PTR)"\t3b, 11b\n\t"		    \
			STR(PTR)"\t4b, 11b\n\t"		    \
			STR(PTR)"\t5b, 11b\n\t"		    \
			STR(PTR)"\t6b, 11b\n\t"		    \
			STR(PTR)"\t7b, 11b\n\t"		    \
			STR(PTR)"\t8b, 11b\n\t"		    \
			".previous"			    \
		: "=&r" (res)			    	    \
		: "r" (value), "r" (addr), "i" (-EFAULT)    \
		: "memory");                                \
} while(0)

#endif /* CONFIG_CPU_MIPSR6 */
#endif

#define LoadHWU(addr, value, res)	_LoadHWU(addr, value, res, kernel)
#define LoadHWUE(addr, value, res)	_LoadHWU(addr, value, res, user)
#define LoadWU(addr, value, res)	_LoadWU(addr, value, res, kernel)
#define LoadWUE(addr, value, res)	_LoadWU(addr, value, res, user)
#define LoadHW(addr, value, res)	_LoadHW(addr, value, res, kernel)
#define LoadHWE(addr, value, res)	_LoadHW(addr, value, res, user)
#define LoadW(addr, value, res)		_LoadW(addr, value, res, kernel)
#define LoadWE(addr, value, res)	_LoadW(addr, value, res, user)
#define LoadDW(addr, value, res)	_LoadDW(addr, value, res)

#define StoreHW(addr, value, res)	_StoreHW(addr, value, res, kernel)
#define StoreHWE(addr, value, res)	_StoreHW(addr, value, res, user)
#define StoreW(addr, value, res)	_StoreW(addr, value, res, kernel)
#define StoreWE(addr, value, res)	_StoreW(addr, value, res, user)
#define StoreDW(addr, value, res)	_StoreDW(addr, value, res)

static void emulate_load_store_insn(struct pt_regs *regs,
	void __user *addr, unsigned int __user *pc)
{
	union mips_instruction insn;
	unsigned long value;
	unsigned int res, preempted;
	unsigned long origpc;
	unsigned long orig31;
	void __user *fault_addr = NULL;
#ifdef	CONFIG_EVA
	mm_segment_t seg;
#endif
	union fpureg *fpr;
	enum msa_2b_fmt df;
	unsigned int wd;
	origpc = (unsigned long)pc;
	orig31 = regs->regs[31];

	perf_sw_event(PERF_COUNT_SW_EMULATION_FAULTS, 1, regs, 0);

	/*
	 * This load never faults.
	 */
	__get_user(insn.word, pc);

	switch (insn.i_format.opcode) {
		/*
		 * These are instructions that a compiler doesn't generate.  We
		 * can assume therefore that the code is MIPS-aware and
		 * really buggy.  Emulating these instructions would break the
		 * semantics anyway.
		 */
	case ll_op:
	case lld_op:
	case sc_op:
	case scd_op:

		/*
		 * For these instructions the only way to create an address
		 * error is an attempted access to kernel/supervisor address
		 * space.
		 */
	case ldl_op:
	case ldr_op:
	case lwl_op:
	case lwr_op:
	case sdl_op:
	case sdr_op:
	case swl_op:
	case swr_op:
	case lb_op:
	case lbu_op:
	case sb_op:
		goto sigbus;

		/*
		 * The remaining opcodes are the ones that are really of
		 * interest.
		 */
	case spec3_op:
		if (insn.dsp_format.func == lx_op) {
			switch (insn.dsp_format.op) {
			case lwx_op:
				if (!access_ok(VERIFY_READ, addr, 4))
					goto sigbus;
				LoadW(addr, value, res);
				if (res)
					goto fault;
				compute_return_epc(regs);
				regs->regs[insn.dsp_format.rd] = value;
				break;
			case lhx_op:
				if (!access_ok(VERIFY_READ, addr, 2))
					goto sigbus;
				LoadHW(addr, value, res);
				if (res)
					goto fault;
				compute_return_epc(regs);
				regs->regs[insn.dsp_format.rd] = value;
				break;
			default:
				goto sigill;
			}
		}
#ifdef CONFIG_EVA
		else {
			/*
			 * we can land here only from kernel accessing user
			 * memory, so we need to "switch" the address limit to
			 * user space, so that address check can work properly.
			 */
			seg = get_fs();
			set_fs(USER_DS);
			switch (insn.spec3_format.func) {
			case lhe_op:
				if (!access_ok(VERIFY_READ, addr, 2)) {
					set_fs(seg);
					goto sigbus;
				}
				LoadHWE(addr, value, res);
				if (res) {
					set_fs(seg);
					goto fault;
				}
				compute_return_epc(regs);
				regs->regs[insn.spec3_format.rt] = value;
				break;
			case lwe_op:
				if (!access_ok(VERIFY_READ, addr, 4)) {
					set_fs(seg);
					goto sigbus;
				}
				LoadWE(addr, value, res);
				if (res) {
					set_fs(seg);
					goto fault;
				}
				compute_return_epc(regs);
				regs->regs[insn.spec3_format.rt] = value;
				break;
			case lhue_op:
				if (!access_ok(VERIFY_READ, addr, 2)) {
					set_fs(seg);
					goto sigbus;
				}
				LoadHWUE(addr, value, res);
				if (res) {
					set_fs(seg);
					goto fault;
				}
				compute_return_epc(regs);
				regs->regs[insn.spec3_format.rt] = value;
				break;
			case she_op:
				if (!access_ok(VERIFY_WRITE, addr, 2)) {
					set_fs(seg);
					goto sigbus;
				}
				compute_return_epc(regs);
				value = regs->regs[insn.spec3_format.rt];
				StoreHWE(addr, value, res);
				if (res) {
					set_fs(seg);
					goto fault;
				}
				break;
			case swe_op:
				if (!access_ok(VERIFY_WRITE, addr, 4)) {
					set_fs(seg);
					goto sigbus;
				}
				compute_return_epc(regs);
				value = regs->regs[insn.spec3_format.rt];
				StoreWE(addr, value, res);
				if (res) {
					set_fs(seg);
					goto fault;
				}
				break;
			default:
				set_fs(seg);
				goto sigill;
			}
			set_fs(seg);
		}
#endif
		break;
	case lh_op:
		if (!access_ok(VERIFY_READ, addr, 2))
			goto sigbus;

		if (IS_ENABLED(CONFIG_EVA)) {
			if (uaccess_kernel())
				LoadHW(addr, value, res);
			else
				LoadHWE(addr, value, res);
		} else {
			LoadHW(addr, value, res);
		}

		if (res)
			goto fault;
		compute_return_epc(regs);
		regs->regs[insn.i_format.rt] = value;
		break;

	case lw_op:
		if (!access_ok(VERIFY_READ, addr, 4))
			goto sigbus;

		if (IS_ENABLED(CONFIG_EVA)) {
			if (uaccess_kernel())
				LoadW(addr, value, res);
			else
				LoadWE(addr, value, res);
		} else {
			LoadW(addr, value, res);
		}

		if (res)
			goto fault;
		compute_return_epc(regs);
		regs->regs[insn.i_format.rt] = value;
		break;

	case lhu_op:
		if (!access_ok(VERIFY_READ, addr, 2))
			goto sigbus;

		if (IS_ENABLED(CONFIG_EVA)) {
			if (uaccess_kernel())
				LoadHWU(addr, value, res);
			else
				LoadHWUE(addr, value, res);
		} else {
			LoadHWU(addr, value, res);
		}

		if (res)
			goto fault;
		compute_return_epc(regs);
		regs->regs[insn.i_format.rt] = value;
		break;

	case lwu_op:
#ifdef CONFIG_64BIT
		/*
		 * A 32-bit kernel might be running on a 64-bit processor.  But
		 * if we're on a 32-bit processor and an i-cache incoherency
		 * or race makes us see a 64-bit instruction here the sdl/sdr
		 * would blow up, so for now we don't handle unaligned 64-bit
		 * instructions on 32-bit kernels.
		 */
		if (!access_ok(VERIFY_READ, addr, 4))
			goto sigbus;

		LoadWU(addr, value, res);
		if (res)
			goto fault;
		compute_return_epc(regs);
		regs->regs[insn.i_format.rt] = value;
		break;
#endif /* CONFIG_64BIT */

		/* Cannot handle 64-bit instructions in 32-bit kernel */
		goto sigill;

	case ld_op:
#ifdef CONFIG_64BIT
		/*
		 * A 32-bit kernel might be running on a 64-bit processor.  But
		 * if we're on a 32-bit processor and an i-cache incoherency
		 * or race makes us see a 64-bit instruction here the sdl/sdr
		 * would blow up, so for now we don't handle unaligned 64-bit
		 * instructions on 32-bit kernels.
		 */
		if (!access_ok(VERIFY_READ, addr, 8))
			goto sigbus;

		LoadDW(addr, value, res);
		if (res)
			goto fault;
		compute_return_epc(regs);
		regs->regs[insn.i_format.rt] = value;
		break;
#endif /* CONFIG_64BIT */

		/* Cannot handle 64-bit instructions in 32-bit kernel */
		goto sigill;

	case sh_op:
		if (!access_ok(VERIFY_WRITE, addr, 2))
			goto sigbus;

		compute_return_epc(regs);
		value = regs->regs[insn.i_format.rt];

		if (IS_ENABLED(CONFIG_EVA)) {
			if (uaccess_kernel())
				StoreHW(addr, value, res);
			else
				StoreHWE(addr, value, res);
		} else {
			StoreHW(addr, value, res);
		}

		if (res)
			goto fault;
		break;

	case sw_op:
		if (!access_ok(VERIFY_WRITE, addr, 4))
			goto sigbus;

		compute_return_epc(regs);
		value = regs->regs[insn.i_format.rt];

		if (IS_ENABLED(CONFIG_EVA)) {
			if (uaccess_kernel())
				StoreW(addr, value, res);
			else
				StoreWE(addr, value, res);
		} else {
			StoreW(addr, value, res);
		}

		if (res)
			goto fault;
		break;

	case sd_op:
#ifdef CONFIG_64BIT
		/*
		 * A 32-bit kernel might be running on a 64-bit processor.  But
		 * if we're on a 32-bit processor and an i-cache incoherency
		 * or race makes us see a 64-bit instruction here the sdl/sdr
		 * would blow up, so for now we don't handle unaligned 64-bit
		 * instructions on 32-bit kernels.
		 */
		if (!access_ok(VERIFY_WRITE, addr, 8))
			goto sigbus;

		compute_return_epc(regs);
		value = regs->regs[insn.i_format.rt];
		StoreDW(addr, value, res);
		if (res)
			goto fault;
		break;
#endif /* CONFIG_64BIT */

		/* Cannot handle 64-bit instructions in 32-bit kernel */
		goto sigill;

	case lwc1_op:
	case ldc1_op:
	case swc1_op:
	case sdc1_op:
	case cop1x_op:
		die_if_kernel("Unaligned FP access in kernel code", regs);
		BUG_ON(!used_math());

		lose_fpu(1);	/* Save FPU state for the emulator. */
		res = fpu_emulator_cop1Handler(regs, &current->thread.fpu, 1,
					       &fault_addr);
		own_fpu(1);	/* Restore FPU state. */

		/* Signal if something went wrong. */
		process_fpemu_return(res, fault_addr, 0);

		if (res == 0)
			break;
		return;

	case msa_op:
		if (!cpu_has_msa)
			goto sigill;

		/*
		 * If we've reached this point then userland should have taken
		 * the MSA disabled exception & initialised vector context at
		 * some point in the past.
		 */
		BUG_ON(!thread_msa_context_live());

		df = insn.msa_mi10_format.df;
		wd = insn.msa_mi10_format.wd;
		fpr = &current->thread.fpu.fpr[wd];

		switch (insn.msa_mi10_format.func) {
		case msa_ld_op:
			if (!access_ok(VERIFY_READ, addr, sizeof(*fpr)))
				goto sigbus;

			do {
				/*
				 * If we have live MSA context keep track of
				 * whether we get preempted in order to avoid
				 * the register context we load being clobbered
				 * by the live context as it's saved during
				 * preemption. If we don't have live context
				 * then it can't be saved to clobber the value
				 * we load.
				 */
				preempted = test_thread_flag(TIF_USEDMSA);

				res = __copy_from_user_inatomic(fpr, addr,
								sizeof(*fpr));
				if (res)
					goto fault;

				/*
				 * Update the hardware register if it is in use
				 * by the task in this quantum, in order to
				 * avoid having to save & restore the whole
				 * vector context.
				 */
				preempt_disable();
				if (test_thread_flag(TIF_USEDMSA)) {
					write_msa_wr(wd, fpr, df);
					preempted = 0;
				}
				preempt_enable();
			} while (preempted);
			break;

		case msa_st_op:
			if (!access_ok(VERIFY_WRITE, addr, sizeof(*fpr)))
				goto sigbus;

			/*
			 * Update from the hardware register if it is in use by
			 * the task in this quantum, in order to avoid having to
			 * save & restore the whole vector context.
			 */
			preempt_disable();
			if (test_thread_flag(TIF_USEDMSA))
				read_msa_wr(wd, fpr, df);
			preempt_enable();

			res = __copy_to_user_inatomic(addr, fpr, sizeof(*fpr));
			if (res)
				goto fault;
			break;

		default:
			goto sigbus;
		}

		compute_return_epc(regs);
		break;

#ifndef CONFIG_CPU_MIPSR6
	/*
	 * COP2 is available to implementor for application specific use.
	 * It's up to applications to register a notifier chain and do
	 * whatever they have to do, including possible sending of signals.
	 *
	 * This instruction has been reallocated in Release 6
	 */
	case lwc2_op:
		cu2_notifier_call_chain(CU2_LWC2_OP, regs);
		break;

	case ldc2_op:
		cu2_notifier_call_chain(CU2_LDC2_OP, regs);
		break;

	case swc2_op:
		cu2_notifier_call_chain(CU2_SWC2_OP, regs);
		break;

	case sdc2_op:
		cu2_notifier_call_chain(CU2_SDC2_OP, regs);
		break;
#endif
	default:
		/*
		 * Pheeee...  We encountered an yet unknown instruction or
		 * cache coherence problem.  Die sucker, die ...
		 */
		goto sigill;
	}

#ifdef CONFIG_DEBUG_FS
	unaligned_instructions++;
#endif

	return;

fault:
	/* roll back jump/branch */
	regs->cp0_epc = origpc;
	regs->regs[31] = orig31;
	/* Did we have an exception handler installed? */
	if (fixup_exception(regs))
		return;

	die_if_kernel("Unhandled kernel unaligned access", regs);
	force_sig(SIGSEGV, current);

	return;

sigbus:
	die_if_kernel("Unhandled kernel unaligned access", regs);
	force_sig(SIGBUS, current);

	return;

sigill:
	die_if_kernel
	    ("Unhandled kernel unaligned access or invalid instruction", regs);
	force_sig(SIGILL, current);
}

/* Recode table from 16-bit register notation to 32-bit GPR. */
const int reg16to32[] = { 16, 17, 2, 3, 4, 5, 6, 7 };

/* Recode table from 16-bit STORE register notation to 32-bit GPR. */
const int reg16to32st[] = { 0, 17, 2, 3, 4, 5, 6, 7 };

static void emulate_load_store_microMIPS(struct pt_regs *regs,
					 void __user *addr)
{
	unsigned long value;
	unsigned int res;
	int i;
	unsigned int reg = 0, rvar;
	unsigned long orig31;
	u16 __user *pc16;
	u16 halfword;
	unsigned int word;
	unsigned long origpc, contpc;
	union mips_instruction insn;
	struct mm_decoded_insn mminsn;
	void __user *fault_addr = NULL;

	origpc = regs->cp0_epc;
	orig31 = regs->regs[31];

	mminsn.micro_mips_mode = 1;

	/*
	 * This load never faults.
	 */
	pc16 = (unsigned short __user *)msk_isa16_mode(regs->cp0_epc);
	__get_user(halfword, pc16);
	pc16++;
	contpc = regs->cp0_epc + 2;
	word = ((unsigned int)halfword << 16);
	mminsn.pc_inc = 2;

	if (!mm_insn_16bit(halfword)) {
		__get_user(halfword, pc16);
		pc16++;
		contpc = regs->cp0_epc + 4;
		mminsn.pc_inc = 4;
		word |= halfword;
	}
	mminsn.insn = word;

	if (get_user(halfword, pc16))
		goto fault;
	mminsn.next_pc_inc = 2;
	word = ((unsigned int)halfword << 16);

	if (!mm_insn_16bit(halfword)) {
		pc16++;
		if (get_user(halfword, pc16))
			goto fault;
		mminsn.next_pc_inc = 4;
		word |= halfword;
	}
	mminsn.next_insn = word;

	insn = (union mips_instruction)(mminsn.insn);
	if (mm_isBranchInstr(regs, mminsn, &contpc))
		insn = (union mips_instruction)(mminsn.next_insn);

	/*  Parse instruction to find what to do */

	switch (insn.mm_i_format.opcode) {

	case mm_pool32a_op:
		switch (insn.mm_x_format.func) {
		case mm_lwxs_op:
			reg = insn.mm_x_format.rd;
			goto loadW;
		}

		goto sigbus;

	case mm_pool32b_op:
		switch (insn.mm_m_format.func) {
		case mm_lwp_func:
			reg = insn.mm_m_format.rd;
			if (reg == 31)
				goto sigbus;

			if (!access_ok(VERIFY_READ, addr, 8))
				goto sigbus;

			LoadW(addr, value, res);
			if (res)
				goto fault;
			regs->regs[reg] = value;
			addr += 4;
			LoadW(addr, value, res);
			if (res)
				goto fault;
			regs->regs[reg + 1] = value;
			goto success;

		case mm_swp_func:
			reg = insn.mm_m_format.rd;
			if (reg == 31)
				goto sigbus;

			if (!access_ok(VERIFY_WRITE, addr, 8))
				goto sigbus;

			value = regs->regs[reg];
			StoreW(addr, value, res);
			if (res)
				goto fault;
			addr += 4;
			value = regs->regs[reg + 1];
			StoreW(addr, value, res);
			if (res)
				goto fault;
			goto success;

		case mm_ldp_func:
#ifdef CONFIG_64BIT
			reg = insn.mm_m_format.rd;
			if (reg == 31)
				goto sigbus;

			if (!access_ok(VERIFY_READ, addr, 16))
				goto sigbus;

			LoadDW(addr, value, res);
			if (res)
				goto fault;
			regs->regs[reg] = value;
			addr += 8;
			LoadDW(addr, value, res);
			if (res)
				goto fault;
			regs->regs[reg + 1] = value;
			goto success;
#endif /* CONFIG_64BIT */

			goto sigill;

		case mm_sdp_func:
#ifdef CONFIG_64BIT
			reg = insn.mm_m_format.rd;
			if (reg == 31)
				goto sigbus;

			if (!access_ok(VERIFY_WRITE, addr, 16))
				goto sigbus;

			value = regs->regs[reg];
			StoreDW(addr, value, res);
			if (res)
				goto fault;
			addr += 8;
			value = regs->regs[reg + 1];
			StoreDW(addr, value, res);
			if (res)
				goto fault;
			goto success;
#endif /* CONFIG_64BIT */

			goto sigill;

		case mm_lwm32_func:
			reg = insn.mm_m_format.rd;
			rvar = reg & 0xf;
			if ((rvar > 9) || !reg)
				goto sigill;
			if (reg & 0x10) {
				if (!access_ok
				    (VERIFY_READ, addr, 4 * (rvar + 1)))
					goto sigbus;
			} else {
				if (!access_ok(VERIFY_READ, addr, 4 * rvar))
					goto sigbus;
			}
			if (rvar == 9)
				rvar = 8;
			for (i = 16; rvar; rvar--, i++) {
				LoadW(addr, value, res);
				if (res)
					goto fault;
				addr += 4;
				regs->regs[i] = value;
			}
			if ((reg & 0xf) == 9) {
				LoadW(addr, value, res);
				if (res)
					goto fault;
				addr += 4;
				regs->regs[30] = value;
			}
			if (reg & 0x10) {
				LoadW(addr, value, res);
				if (res)
					goto fault;
				regs->regs[31] = value;
			}
			goto success;

		case mm_swm32_func:
			reg = insn.mm_m_format.rd;
			rvar = reg & 0xf;
			if ((rvar > 9) || !reg)
				goto sigill;
			if (reg & 0x10) {
				if (!access_ok
				    (VERIFY_WRITE, addr, 4 * (rvar + 1)))
					goto sigbus;
			} else {
				if (!access_ok(VERIFY_WRITE, addr, 4 * rvar))
					goto sigbus;
			}
			if (rvar == 9)
				rvar = 8;
			for (i = 16; rvar; rvar--, i++) {
				value = regs->regs[i];
				StoreW(addr, value, res);
				if (res)
					goto fault;
				addr += 4;
			}
			if ((reg & 0xf) == 9) {
				value = regs->regs[30];
				StoreW(addr, value, res);
				if (res)
					goto fault;
				addr += 4;
			}
			if (reg & 0x10) {
				value = regs->regs[31];
				StoreW(addr, value, res);
				if (res)
					goto fault;
			}
			goto success;

		case mm_ldm_func:
#ifdef CONFIG_64BIT
			reg = insn.mm_m_format.rd;
			rvar = reg & 0xf;
			if ((rvar > 9) || !reg)
				goto sigill;
			if (reg & 0x10) {
				if (!access_ok
				    (VERIFY_READ, addr, 8 * (rvar + 1)))
					goto sigbus;
			} else {
				if (!access_ok(VERIFY_READ, addr, 8 * rvar))
					goto sigbus;
			}
			if (rvar == 9)
				rvar = 8;

			for (i = 16; rvar; rvar--, i++) {
				LoadDW(addr, value, res);
				if (res)
					goto fault;
				addr += 4;
				regs->regs[i] = value;
			}
			if ((reg & 0xf) == 9) {
				LoadDW(addr, value, res);
				if (res)
					goto fault;
				addr += 8;
				regs->regs[30] = value;
			}
			if (reg & 0x10) {
				LoadDW(addr, value, res);
				if (res)
					goto fault;
				regs->regs[31] = value;
			}
			goto success;
#endif /* CONFIG_64BIT */

			goto sigill;

		case mm_sdm_func:
#ifdef CONFIG_64BIT
			reg = insn.mm_m_format.rd;
			rvar = reg & 0xf;
			if ((rvar > 9) || !reg)
				goto sigill;
			if (reg & 0x10) {
				if (!access_ok
				    (VERIFY_WRITE, addr, 8 * (rvar + 1)))
					goto sigbus;
			} else {
				if (!access_ok(VERIFY_WRITE, addr, 8 * rvar))
					goto sigbus;
			}
			if (rvar == 9)
				rvar = 8;

			for (i = 16; rvar; rvar--, i++) {
				value = regs->regs[i];
				StoreDW(addr, value, res);
				if (res)
					goto fault;
				addr += 8;
			}
			if ((reg & 0xf) == 9) {
				value = regs->regs[30];
				StoreDW(addr, value, res);
				if (res)
					goto fault;
				addr += 8;
			}
			if (reg & 0x10) {
				value = regs->regs[31];
				StoreDW(addr, value, res);
				if (res)
					goto fault;
			}
			goto success;
#endif /* CONFIG_64BIT */

			goto sigill;

			/*  LWC2, SWC2, LDC2, SDC2 are not serviced */
		}

		goto sigbus;

	case mm_pool32c_op:
		switch (insn.mm_m_format.func) {
		case mm_lwu_func:
			reg = insn.mm_m_format.rd;
			goto loadWU;
		}

		/*  LL,SC,LLD,SCD are not serviced */
		goto sigbus;

	case mm_pool32f_op:
		switch (insn.mm_x_format.func) {
		case mm_lwxc1_func:
		case mm_swxc1_func:
		case mm_ldxc1_func:
		case mm_sdxc1_func:
			goto fpu_emul;
		}

		goto sigbus;

	case mm_ldc132_op:
	case mm_sdc132_op:
	case mm_lwc132_op:
	case mm_swc132_op:
fpu_emul:
		/* roll back jump/branch */
		regs->cp0_epc = origpc;
		regs->regs[31] = orig31;

		die_if_kernel("Unaligned FP access in kernel code", regs);
		BUG_ON(!used_math());
		BUG_ON(!is_fpu_owner());

		lose_fpu(1);	/* save the FPU state for the emulator */
		res = fpu_emulator_cop1Handler(regs, &current->thread.fpu, 1,
					       &fault_addr);
		own_fpu(1);	/* restore FPU state */

		/* If something went wrong, signal */
		process_fpemu_return(res, fault_addr, 0);

		if (res == 0)
			goto success;
		return;

	case mm_lh32_op:
		reg = insn.mm_i_format.rt;
		goto loadHW;

	case mm_lhu32_op:
		reg = insn.mm_i_format.rt;
		goto loadHWU;

	case mm_lw32_op:
		reg = insn.mm_i_format.rt;
		goto loadW;

	case mm_sh32_op:
		reg = insn.mm_i_format.rt;
		goto storeHW;

	case mm_sw32_op:
		reg = insn.mm_i_format.rt;
		goto storeW;

	case mm_ld32_op:
		reg = insn.mm_i_format.rt;
		goto loadDW;

	case mm_sd32_op:
		reg = insn.mm_i_format.rt;
		goto storeDW;

	case mm_pool16c_op:
		switch (insn.mm16_m_format.func) {
		case mm_lwm16_op:
			reg = insn.mm16_m_format.rlist;
			rvar = reg + 1;
			if (!access_ok(VERIFY_READ, addr, 4 * rvar))
				goto sigbus;

			for (i = 16; rvar; rvar--, i++) {
				LoadW(addr, value, res);
				if (res)
					goto fault;
				addr += 4;
				regs->regs[i] = value;
			}
			LoadW(addr, value, res);
			if (res)
				goto fault;
			regs->regs[31] = value;

			goto success;

		case mm_swm16_op:
			reg = insn.mm16_m_format.rlist;
			rvar = reg + 1;
			if (!access_ok(VERIFY_WRITE, addr, 4 * rvar))
				goto sigbus;

			for (i = 16; rvar; rvar--, i++) {
				value = regs->regs[i];
				StoreW(addr, value, res);
				if (res)
					goto fault;
				addr += 4;
			}
			value = regs->regs[31];
			StoreW(addr, value, res);
			if (res)
				goto fault;

			goto success;

		}

		goto sigbus;

	case mm_lhu16_op:
		reg = reg16to32[insn.mm16_rb_format.rt];
		goto loadHWU;

	case mm_lw16_op:
		reg = reg16to32[insn.mm16_rb_format.rt];
		goto loadW;

	case mm_sh16_op:
		reg = reg16to32st[insn.mm16_rb_format.rt];
		goto storeHW;

	case mm_sw16_op:
		reg = reg16to32st[insn.mm16_rb_format.rt];
		goto storeW;

	case mm_lwsp16_op:
		reg = insn.mm16_r5_format.rt;
		goto loadW;

	case mm_swsp16_op:
		reg = insn.mm16_r5_format.rt;
		goto storeW;

	case mm_lwgp16_op:
		reg = reg16to32[insn.mm16_r3_format.rt];
		goto loadW;

	default:
		goto sigill;
	}

loadHW:
	if (!access_ok(VERIFY_READ, addr, 2))
		goto sigbus;

	LoadHW(addr, value, res);
	if (res)
		goto fault;
	regs->regs[reg] = value;
	goto success;

loadHWU:
	if (!access_ok(VERIFY_READ, addr, 2))
		goto sigbus;

	LoadHWU(addr, value, res);
	if (res)
		goto fault;
	regs->regs[reg] = value;
	goto success;

loadW:
	if (!access_ok(VERIFY_READ, addr, 4))
		goto sigbus;

	LoadW(addr, value, res);
	if (res)
		goto fault;
	regs->regs[reg] = value;
	goto success;

loadWU:
#ifdef CONFIG_64BIT
	/*
	 * A 32-bit kernel might be running on a 64-bit processor.  But
	 * if we're on a 32-bit processor and an i-cache incoherency
	 * or race makes us see a 64-bit instruction here the sdl/sdr
	 * would blow up, so for now we don't handle unaligned 64-bit
	 * instructions on 32-bit kernels.
	 */
	if (!access_ok(VERIFY_READ, addr, 4))
		goto sigbus;

	LoadWU(addr, value, res);
	if (res)
		goto fault;
	regs->regs[reg] = value;
	goto success;
#endif /* CONFIG_64BIT */

	/* Cannot handle 64-bit instructions in 32-bit kernel */
	goto sigill;

loadDW:
#ifdef CONFIG_64BIT
	/*
	 * A 32-bit kernel might be running on a 64-bit processor.  But
	 * if we're on a 32-bit processor and an i-cache incoherency
	 * or race makes us see a 64-bit instruction here the sdl/sdr
	 * would blow up, so for now we don't handle unaligned 64-bit
	 * instructions on 32-bit kernels.
	 */
	if (!access_ok(VERIFY_READ, addr, 8))
		goto sigbus;

	LoadDW(addr, value, res);
	if (res)
		goto fault;
	regs->regs[reg] = value;
	goto success;
#endif /* CONFIG_64BIT */

	/* Cannot handle 64-bit instructions in 32-bit kernel */
	goto sigill;

storeHW:
	if (!access_ok(VERIFY_WRITE, addr, 2))
		goto sigbus;

	value = regs->regs[reg];
	StoreHW(addr, value, res);
	if (res)
		goto fault;
	goto success;

storeW:
	if (!access_ok(VERIFY_WRITE, addr, 4))
		goto sigbus;

	value = regs->regs[reg];
	StoreW(addr, value, res);
	if (res)
		goto fault;
	goto success;

storeDW:
#ifdef CONFIG_64BIT
	/*
	 * A 32-bit kernel might be running on a 64-bit processor.  But
	 * if we're on a 32-bit processor and an i-cache incoherency
	 * or race makes us see a 64-bit instruction here the sdl/sdr
	 * would blow up, so for now we don't handle unaligned 64-bit
	 * instructions on 32-bit kernels.
	 */
	if (!access_ok(VERIFY_WRITE, addr, 8))
		goto sigbus;

	value = regs->regs[reg];
	StoreDW(addr, value, res);
	if (res)
		goto fault;
	goto success;
#endif /* CONFIG_64BIT */

	/* Cannot handle 64-bit instructions in 32-bit kernel */
	goto sigill;

success:
	regs->cp0_epc = contpc;	/* advance or branch */

#ifdef CONFIG_DEBUG_FS
	unaligned_instructions++;
#endif
	return;

fault:
	/* roll back jump/branch */
	regs->cp0_epc = origpc;
	regs->regs[31] = orig31;
	/* Did we have an exception handler installed? */
	if (fixup_exception(regs))
		return;

	die_if_kernel("Unhandled kernel unaligned access", regs);
	force_sig(SIGSEGV, current);

	return;

sigbus:
	die_if_kernel("Unhandled kernel unaligned access", regs);
	force_sig(SIGBUS, current);

	return;

sigill:
	die_if_kernel
	    ("Unhandled kernel unaligned access or invalid instruction", regs);
	force_sig(SIGILL, current);
}

static void emulate_load_store_MIPS16e(struct pt_regs *regs, void __user * addr)
{
	unsigned long value;
	unsigned int res;
	int reg;
	unsigned long orig31;
	u16 __user *pc16;
	unsigned long origpc;
	union mips16e_instruction mips16inst, oldinst;
	unsigned int opcode;
	int extended = 0;

	origpc = regs->cp0_epc;
	orig31 = regs->regs[31];
	pc16 = (unsigned short __user *)msk_isa16_mode(origpc);
	/*
	 * This load never faults.
	 */
	__get_user(mips16inst.full, pc16);
	oldinst = mips16inst;

	/* skip EXTEND instruction */
	if (mips16inst.ri.opcode == MIPS16e_extend_op) {
		extended = 1;
		pc16++;
		__get_user(mips16inst.full, pc16);
	} else if (delay_slot(regs)) {
		/*  skip jump instructions */
		/*  JAL/JALX are 32 bits but have OPCODE in first short int */
		if (mips16inst.ri.opcode == MIPS16e_jal_op)
			pc16++;
		pc16++;
		if (get_user(mips16inst.full, pc16))
			goto sigbus;
	}

	opcode = mips16inst.ri.opcode;
	switch (opcode) {
	case MIPS16e_i64_op:	/* I64 or RI64 instruction */
		switch (mips16inst.i64.func) {	/* I64/RI64 func field check */
		case MIPS16e_ldpc_func:
		case MIPS16e_ldsp_func:
			reg = reg16to32[mips16inst.ri64.ry];
			goto loadDW;

		case MIPS16e_sdsp_func:
			reg = reg16to32[mips16inst.ri64.ry];
			goto writeDW;

		case MIPS16e_sdrasp_func:
			reg = 29;	/* GPRSP */
			goto writeDW;
		}

		goto sigbus;

	case MIPS16e_swsp_op:
		reg = reg16to32[mips16inst.ri.rx];
		if (extended && cpu_has_mips16e2)
			switch (mips16inst.ri.imm >> 5) {
			case 0:		/* SWSP */
			case 1:		/* SWGP */
				break;
			case 2:		/* SHGP */
				opcode = MIPS16e_sh_op;
				break;
			default:
				goto sigbus;
			}
		break;

	case MIPS16e_lwpc_op:
		reg = reg16to32[mips16inst.ri.rx];
		break;

	case MIPS16e_lwsp_op:
		reg = reg16to32[mips16inst.ri.rx];
		if (extended && cpu_has_mips16e2)
			switch (mips16inst.ri.imm >> 5) {
			case 0:		/* LWSP */
			case 1:		/* LWGP */
				break;
			case 2:		/* LHGP */
				opcode = MIPS16e_lh_op;
				break;
			case 4:		/* LHUGP */
				opcode = MIPS16e_lhu_op;
				break;
			default:
				goto sigbus;
			}
		break;

	case MIPS16e_i8_op:
		if (mips16inst.i8.func != MIPS16e_swrasp_func)
			goto sigbus;
		reg = 29;	/* GPRSP */
		break;

	default:
		reg = reg16to32[mips16inst.rri.ry];
		break;
	}

	switch (opcode) {

	case MIPS16e_lb_op:
	case MIPS16e_lbu_op:
	case MIPS16e_sb_op:
		goto sigbus;

	case MIPS16e_lh_op:
		if (!access_ok(VERIFY_READ, addr, 2))
			goto sigbus;

		LoadHW(addr, value, res);
		if (res)
			goto fault;
		MIPS16e_compute_return_epc(regs, &oldinst);
		regs->regs[reg] = value;
		break;

	case MIPS16e_lhu_op:
		if (!access_ok(VERIFY_READ, addr, 2))
			goto sigbus;

		LoadHWU(addr, value, res);
		if (res)
			goto fault;
		MIPS16e_compute_return_epc(regs, &oldinst);
		regs->regs[reg] = value;
		break;

	case MIPS16e_lw_op:
	case MIPS16e_lwpc_op:
	case MIPS16e_lwsp_op:
		if (!access_ok(VERIFY_READ, addr, 4))
			goto sigbus;

		LoadW(addr, value, res);
		if (res)
			goto fault;
		MIPS16e_compute_return_epc(regs, &oldinst);
		regs->regs[reg] = value;
		break;

	case MIPS16e_lwu_op:
#ifdef CONFIG_64BIT
		/*
		 * A 32-bit kernel might be running on a 64-bit processor.  But
		 * if we're on a 32-bit processor and an i-cache incoherency
		 * or race makes us see a 64-bit instruction here the sdl/sdr
		 * would blow up, so for now we don't handle unaligned 64-bit
		 * instructions on 32-bit kernels.
		 */
		if (!access_ok(VERIFY_READ, addr, 4))
			goto sigbus;

		LoadWU(addr, value, res);
		if (res)
			goto fault;
		MIPS16e_compute_return_epc(regs, &oldinst);
		regs->regs[reg] = value;
		break;
#endif /* CONFIG_64BIT */

		/* Cannot handle 64-bit instructions in 32-bit kernel */
		goto sigill;

	case MIPS16e_ld_op:
loadDW:
#ifdef CONFIG_64BIT
		/*
		 * A 32-bit kernel might be running on a 64-bit processor.  But
		 * if we're on a 32-bit processor and an i-cache incoherency
		 * or race makes us see a 64-bit instruction here the sdl/sdr
		 * would blow up, so for now we don't handle unaligned 64-bit
		 * instructions on 32-bit kernels.
		 */
		if (!access_ok(VERIFY_READ, addr, 8))
			goto sigbus;

		LoadDW(addr, value, res);
		if (res)
			goto fault;
		MIPS16e_compute_return_epc(regs, &oldinst);
		regs->regs[reg] = value;
		break;
#endif /* CONFIG_64BIT */

		/* Cannot handle 64-bit instructions in 32-bit kernel */
		goto sigill;

	case MIPS16e_sh_op:
		if (!access_ok(VERIFY_WRITE, addr, 2))
			goto sigbus;

		MIPS16e_compute_return_epc(regs, &oldinst);
		value = regs->regs[reg];
		StoreHW(addr, value, res);
		if (res)
			goto fault;
		break;

	case MIPS16e_sw_op:
	case MIPS16e_swsp_op:
	case MIPS16e_i8_op:	/* actually - MIPS16e_swrasp_func */
		if (!access_ok(VERIFY_WRITE, addr, 4))
			goto sigbus;

		MIPS16e_compute_return_epc(regs, &oldinst);
		value = regs->regs[reg];
		StoreW(addr, value, res);
		if (res)
			goto fault;
		break;

	case MIPS16e_sd_op:
writeDW:
#ifdef CONFIG_64BIT
		/*
		 * A 32-bit kernel might be running on a 64-bit processor.  But
		 * if we're on a 32-bit processor and an i-cache incoherency
		 * or race makes us see a 64-bit instruction here the sdl/sdr
		 * would blow up, so for now we don't handle unaligned 64-bit
		 * instructions on 32-bit kernels.
		 */
		if (!access_ok(VERIFY_WRITE, addr, 8))
			goto sigbus;

		MIPS16e_compute_return_epc(regs, &oldinst);
		value = regs->regs[reg];
		StoreDW(addr, value, res);
		if (res)
			goto fault;
		break;
#endif /* CONFIG_64BIT */

		/* Cannot handle 64-bit instructions in 32-bit kernel */
		goto sigill;

	default:
		/*
		 * Pheeee...  We encountered an yet unknown instruction or
		 * cache coherence problem.  Die sucker, die ...
		 */
		goto sigill;
	}

#ifdef CONFIG_DEBUG_FS
	unaligned_instructions++;
#endif

	return;

fault:
	/* roll back jump/branch */
	regs->cp0_epc = origpc;
	regs->regs[31] = orig31;
	/* Did we have an exception handler installed? */
	if (fixup_exception(regs))
		return;

	die_if_kernel("Unhandled kernel unaligned access", regs);
	force_sig(SIGSEGV, current);

	return;

sigbus:
	die_if_kernel("Unhandled kernel unaligned access", regs);
	force_sig(SIGBUS, current);

	return;

sigill:
	die_if_kernel
	    ("Unhandled kernel unaligned access or invalid instruction", regs);
	force_sig(SIGILL, current);
}

asmlinkage void do_ade(struct pt_regs *regs)
{
	enum ctx_state prev_state;
	unsigned int __user *pc;
	mm_segment_t seg;

	prev_state = exception_enter();
	perf_sw_event(PERF_COUNT_SW_ALIGNMENT_FAULTS,
			1, regs, regs->cp0_badvaddr);
	/*
	 * Did we catch a fault trying to load an instruction?
	 */
	if (regs->cp0_badvaddr == regs->cp0_epc)
		goto sigbus;

	if (user_mode(regs) && !test_thread_flag(TIF_FIXADE))
		goto sigbus;
	if (unaligned_action == UNALIGNED_ACTION_SIGNAL)
		goto sigbus;

	/*
	 * Do branch emulation only if we didn't forward the exception.
	 * This is all so but ugly ...
	 */

	/*
	 * Are we running in microMIPS mode?
	 */
	if (get_isa16_mode(regs->cp0_epc)) {
		/*
		 * Did we catch a fault trying to load an instruction in
		 * 16-bit mode?
		 */
		if (regs->cp0_badvaddr == msk_isa16_mode(regs->cp0_epc))
			goto sigbus;
		if (unaligned_action == UNALIGNED_ACTION_SHOW)
			show_registers(regs);

		if (cpu_has_mmips) {
			seg = get_fs();
			if (!user_mode(regs))
				set_fs(KERNEL_DS);
			emulate_load_store_microMIPS(regs,
				(void __user *)regs->cp0_badvaddr);
			set_fs(seg);

			return;
		}

		if (cpu_has_mips16) {
			seg = get_fs();
			if (!user_mode(regs))
				set_fs(KERNEL_DS);
			emulate_load_store_MIPS16e(regs,
				(void __user *)regs->cp0_badvaddr);
			set_fs(seg);

			return;
	}

		goto sigbus;
	}

	if (unaligned_action == UNALIGNED_ACTION_SHOW)
		show_registers(regs);
	pc = (unsigned int __user *)exception_epc(regs);

	seg = get_fs();
	if (!user_mode(regs))
		set_fs(KERNEL_DS);
	emulate_load_store_insn(regs, (void __user *)regs->cp0_badvaddr, pc);
	set_fs(seg);

	return;

sigbus:
	die_if_kernel("Kernel unaligned instruction access", regs);
	force_sig(SIGBUS, current);

	/*
	 * XXX On return from the signal handler we should advance the epc
	 */
	exception_exit(prev_state);
}

#ifdef CONFIG_DEBUG_FS
static int __init debugfs_unaligned(void)
{
	struct dentry *d;

	if (!mips_debugfs_dir)
		return -ENODEV;
	d = debugfs_create_u32("unaligned_instructions", S_IRUGO,
			       mips_debugfs_dir, &unaligned_instructions);
	if (!d)
		return -ENOMEM;
	d = debugfs_create_u32("unaligned_action", S_IRUGO | S_IWUSR,
			       mips_debugfs_dir, &unaligned_action);
	if (!d)
		return -ENOMEM;
	return 0;
}
arch_initcall(debugfs_unaligned);
#endif
