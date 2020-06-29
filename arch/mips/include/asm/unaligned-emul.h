/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_MIPS_UNALIGNED_EMUL_H
#define _ASM_MIPS_UNALIGNED_EMUL_H

#include <asm/asm.h>

#ifdef __BIG_ENDIAN
#define  _LoadHW(addr, value, res, type)  \
do {                                                \
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
		STR(PTR)"\t1b, 4b\n\t"               \
		STR(PTR)"\t2b, 4b\n\t"               \
		".previous"                         \
		: "=&r" (value), "=r" (res)         \
		: "r" (addr), "i" (-EFAULT));       \
} while (0)

#ifndef CONFIG_CPU_NO_LOAD_STORE_LR
#define  _LoadW(addr, value, res, type)   \
do {                                                \
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
		STR(PTR)"\t1b, 4b\n\t"               \
		STR(PTR)"\t2b, 4b\n\t"               \
		".previous"                         \
		: "=&r" (value), "=r" (res)         \
		: "r" (addr), "i" (-EFAULT));       \
} while (0)

#else /* CONFIG_CPU_NO_LOAD_STORE_LR */
/* For CPUs without lwl instruction */
#define  _LoadW(addr, value, res, type) \
do {                                                \
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
} while (0)

#endif /* CONFIG_CPU_NO_LOAD_STORE_LR */

#define  _LoadHWU(addr, value, res, type) \
do {                                                \
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
		STR(PTR)"\t1b, 4b\n\t"               \
		STR(PTR)"\t2b, 4b\n\t"               \
		".previous"                         \
		: "=&r" (value), "=r" (res)         \
		: "r" (addr), "i" (-EFAULT));       \
} while (0)

#ifndef CONFIG_CPU_NO_LOAD_STORE_LR
#define  _LoadWU(addr, value, res, type)  \
do {                                                \
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
		STR(PTR)"\t1b, 4b\n\t"               \
		STR(PTR)"\t2b, 4b\n\t"               \
		".previous"                         \
		: "=&r" (value), "=r" (res)         \
		: "r" (addr), "i" (-EFAULT));       \
} while (0)

#define  _LoadDW(addr, value, res)  \
do {                                                \
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
		STR(PTR)"\t1b, 4b\n\t"               \
		STR(PTR)"\t2b, 4b\n\t"               \
		".previous"                         \
		: "=&r" (value), "=r" (res)         \
		: "r" (addr), "i" (-EFAULT));       \
} while (0)

#else /* CONFIG_CPU_NO_LOAD_STORE_LR */
/* For CPUs without lwl and ldl instructions */
#define  _LoadWU(addr, value, res, type) \
do {                                                \
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
} while (0)

#define  _LoadDW(addr, value, res)  \
do {                                                \
	__asm__ __volatile__ (			    \
		".set\tpush\n\t"		    \
		".set\tnoat\n\t"		    \
		"1:lb\t%0, 0(%2)\n\t"		    \
		"2:lbu\t $1, 1(%2)\n\t"		    \
		"dsll\t%0, 0x8\n\t"		    \
		"or\t%0, $1\n\t"		    \
		"3:lbu\t$1, 2(%2)\n\t"		    \
		"dsll\t%0, 0x8\n\t"		    \
		"or\t%0, $1\n\t"		    \
		"4:lbu\t$1, 3(%2)\n\t"		    \
		"dsll\t%0, 0x8\n\t"		    \
		"or\t%0, $1\n\t"		    \
		"5:lbu\t$1, 4(%2)\n\t"		    \
		"dsll\t%0, 0x8\n\t"		    \
		"or\t%0, $1\n\t"		    \
		"6:lbu\t$1, 5(%2)\n\t"		    \
		"dsll\t%0, 0x8\n\t"		    \
		"or\t%0, $1\n\t"		    \
		"7:lbu\t$1, 6(%2)\n\t"		    \
		"dsll\t%0, 0x8\n\t"		    \
		"or\t%0, $1\n\t"		    \
		"8:lbu\t$1, 7(%2)\n\t"		    \
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
} while (0)

#endif /* CONFIG_CPU_NO_LOAD_STORE_LR */


#define  _StoreHW(addr, value, res, type) \
do {                                                \
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
} while (0)

#ifndef CONFIG_CPU_NO_LOAD_STORE_LR
#define  _StoreW(addr, value, res, type)  \
do {                                                \
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
		STR(PTR)"\t1b, 4b\n\t"               \
		STR(PTR)"\t2b, 4b\n\t"               \
		".previous"                         \
		: "=r" (res)                                \
		: "r" (value), "r" (addr), "i" (-EFAULT));  \
} while (0)

#define  _StoreDW(addr, value, res) \
do {                                                \
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
		STR(PTR)"\t1b, 4b\n\t"               \
		STR(PTR)"\t2b, 4b\n\t"               \
		".previous"                         \
		: "=r" (res)                                \
		: "r" (value), "r" (addr), "i" (-EFAULT));  \
} while (0)

#else /* CONFIG_CPU_NO_LOAD_STORE_LR */
#define  _StoreW(addr, value, res, type)  \
do {                                                \
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
		: "=&r" (res)				    \
		: "r" (value), "r" (addr), "i" (-EFAULT)    \
		: "memory");                                \
} while (0)

#define  _StoreDW(addr, value, res) \
do {                                                \
	__asm__ __volatile__ (                      \
		".set\tpush\n\t"		    \
		".set\tnoat\n\t"		    \
		"1:sb\t%1, 7(%2)\n\t"		    \
		"dsrl\t$1, %1, 0x8\n\t"		    \
		"2:sb\t$1, 6(%2)\n\t"		    \
		"dsrl\t$1, $1, 0x8\n\t"		    \
		"3:sb\t$1, 5(%2)\n\t"		    \
		"dsrl\t$1, $1, 0x8\n\t"		    \
		"4:sb\t$1, 4(%2)\n\t"		    \
		"dsrl\t$1, $1, 0x8\n\t"		    \
		"5:sb\t$1, 3(%2)\n\t"		    \
		"dsrl\t$1, $1, 0x8\n\t"		    \
		"6:sb\t$1, 2(%2)\n\t"		    \
		"dsrl\t$1, $1, 0x8\n\t"		    \
		"7:sb\t$1, 1(%2)\n\t"		    \
		"dsrl\t$1, $1, 0x8\n\t"		    \
		"8:sb\t$1, 0(%2)\n\t"		    \
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
		: "=&r" (res)				    \
		: "r" (value), "r" (addr), "i" (-EFAULT)    \
		: "memory");                                \
} while (0)

#endif /* CONFIG_CPU_NO_LOAD_STORE_LR */

#else /* __BIG_ENDIAN */

#define  _LoadHW(addr, value, res, type)  \
do {                                                \
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
		STR(PTR)"\t1b, 4b\n\t"               \
		STR(PTR)"\t2b, 4b\n\t"               \
		".previous"                         \
		: "=&r" (value), "=r" (res)         \
		: "r" (addr), "i" (-EFAULT));       \
} while (0)

#ifndef CONFIG_CPU_NO_LOAD_STORE_LR
#define  _LoadW(addr, value, res, type)   \
do {                                                \
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
		STR(PTR)"\t1b, 4b\n\t"               \
		STR(PTR)"\t2b, 4b\n\t"               \
		".previous"                         \
		: "=&r" (value), "=r" (res)         \
		: "r" (addr), "i" (-EFAULT));       \
} while (0)

#else /* CONFIG_CPU_NO_LOAD_STORE_LR */
/* For CPUs without lwl instruction */
#define  _LoadW(addr, value, res, type) \
do {                                                \
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
} while (0)

#endif /* CONFIG_CPU_NO_LOAD_STORE_LR */


#define  _LoadHWU(addr, value, res, type) \
do {                                                \
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
		STR(PTR)"\t1b, 4b\n\t"               \
		STR(PTR)"\t2b, 4b\n\t"               \
		".previous"                         \
		: "=&r" (value), "=r" (res)         \
		: "r" (addr), "i" (-EFAULT));       \
} while (0)

#ifndef CONFIG_CPU_NO_LOAD_STORE_LR
#define  _LoadWU(addr, value, res, type)  \
do {                                                \
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
		STR(PTR)"\t1b, 4b\n\t"               \
		STR(PTR)"\t2b, 4b\n\t"               \
		".previous"                         \
		: "=&r" (value), "=r" (res)         \
		: "r" (addr), "i" (-EFAULT));       \
} while (0)

#define  _LoadDW(addr, value, res)  \
do {                                                \
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
		STR(PTR)"\t1b, 4b\n\t"               \
		STR(PTR)"\t2b, 4b\n\t"               \
		".previous"                         \
		: "=&r" (value), "=r" (res)         \
		: "r" (addr), "i" (-EFAULT));       \
} while (0)

#else /* CONFIG_CPU_NO_LOAD_STORE_LR */
/* For CPUs without lwl and ldl instructions */
#define  _LoadWU(addr, value, res, type) \
do {                                                \
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
} while (0)

#define  _LoadDW(addr, value, res)  \
do {                                                \
	__asm__ __volatile__ (			    \
		".set\tpush\n\t"		    \
		".set\tnoat\n\t"		    \
		"1:lb\t%0, 7(%2)\n\t"		    \
		"2:lbu\t$1, 6(%2)\n\t"		    \
		"dsll\t%0, 0x8\n\t"		    \
		"or\t%0, $1\n\t"		    \
		"3:lbu\t$1, 5(%2)\n\t"		    \
		"dsll\t%0, 0x8\n\t"		    \
		"or\t%0, $1\n\t"		    \
		"4:lbu\t$1, 4(%2)\n\t"		    \
		"dsll\t%0, 0x8\n\t"		    \
		"or\t%0, $1\n\t"		    \
		"5:lbu\t$1, 3(%2)\n\t"		    \
		"dsll\t%0, 0x8\n\t"		    \
		"or\t%0, $1\n\t"		    \
		"6:lbu\t$1, 2(%2)\n\t"		    \
		"dsll\t%0, 0x8\n\t"		    \
		"or\t%0, $1\n\t"		    \
		"7:lbu\t$1, 1(%2)\n\t"		    \
		"dsll\t%0, 0x8\n\t"		    \
		"or\t%0, $1\n\t"		    \
		"8:lbu\t$1, 0(%2)\n\t"		    \
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
} while (0)
#endif /* CONFIG_CPU_NO_LOAD_STORE_LR */

#define  _StoreHW(addr, value, res, type) \
do {                                                 \
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
		STR(PTR)"\t1b, 4b\n\t"               \
		STR(PTR)"\t2b, 4b\n\t"               \
		".previous"                         \
		: "=r" (res)                        \
		: "r" (value), "r" (addr), "i" (-EFAULT));\
} while (0)

#ifndef CONFIG_CPU_NO_LOAD_STORE_LR
#define  _StoreW(addr, value, res, type)  \
do {                                                \
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
		STR(PTR)"\t1b, 4b\n\t"               \
		STR(PTR)"\t2b, 4b\n\t"               \
		".previous"                         \
		: "=r" (res)                                \
		: "r" (value), "r" (addr), "i" (-EFAULT));  \
} while (0)

#define  _StoreDW(addr, value, res) \
do {                                                \
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
		STR(PTR)"\t1b, 4b\n\t"               \
		STR(PTR)"\t2b, 4b\n\t"               \
		".previous"                         \
		: "=r" (res)                                \
		: "r" (value), "r" (addr), "i" (-EFAULT));  \
} while (0)

#else /* CONFIG_CPU_NO_LOAD_STORE_LR */
/* For CPUs without swl and sdl instructions */
#define  _StoreW(addr, value, res, type)  \
do {                                                \
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
		: "=&r" (res)				    \
		: "r" (value), "r" (addr), "i" (-EFAULT)    \
		: "memory");                                \
} while (0)

#define  _StoreDW(addr, value, res) \
do {                                                \
	__asm__ __volatile__ (                      \
		".set\tpush\n\t"		    \
		".set\tnoat\n\t"		    \
		"1:sb\t%1, 0(%2)\n\t"		    \
		"dsrl\t$1, %1, 0x8\n\t"		    \
		"2:sb\t$1, 1(%2)\n\t"		    \
		"dsrl\t$1, $1, 0x8\n\t"		    \
		"3:sb\t$1, 2(%2)\n\t"		    \
		"dsrl\t$1, $1, 0x8\n\t"		    \
		"4:sb\t$1, 3(%2)\n\t"		    \
		"dsrl\t$1, $1, 0x8\n\t"		    \
		"5:sb\t$1, 4(%2)\n\t"		    \
		"dsrl\t$1, $1, 0x8\n\t"		    \
		"6:sb\t$1, 5(%2)\n\t"		    \
		"dsrl\t$1, $1, 0x8\n\t"		    \
		"7:sb\t$1, 6(%2)\n\t"		    \
		"dsrl\t$1, $1, 0x8\n\t"		    \
		"8:sb\t$1, 7(%2)\n\t"		    \
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
		: "=&r" (res)				    \
		: "r" (value), "r" (addr), "i" (-EFAULT)    \
		: "memory");                                \
} while (0)

#endif /* CONFIG_CPU_NO_LOAD_STORE_LR */
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

#endif /* _ASM_MIPS_UNALIGNED_EMUL_H */
