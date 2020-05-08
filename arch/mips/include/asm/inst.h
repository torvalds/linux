/*
 * Format of an instruction in memory.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 2000 by Ralf Baechle
 * Copyright (C) 2006 by Thiemo Seufer
 */
#ifndef _ASM_INST_H
#define _ASM_INST_H

#include <uapi/asm/inst.h>

#if (_MIPS_SZPTR == 32)
#define PTR_STR		".word"
#endif
#if (_MIPS_SZPTR == 64)
#define PTR_STR		".dword"
#endif

/* HACHACHAHCAHC ...  */

/* In case some other massaging is needed, keep MIPSInst as wrapper */

#define MIPSInst(x) x

#define I_OPCODE_SFT	26
#define MIPSInst_OPCODE(x) (MIPSInst(x) >> I_OPCODE_SFT)

#define I_JTARGET_SFT	0
#define MIPSInst_JTARGET(x) (MIPSInst(x) & 0x03ffffff)

#define I_RS_SFT	21
#define MIPSInst_RS(x) ((MIPSInst(x) & 0x03e00000) >> I_RS_SFT)

#define I_RT_SFT	16
#define MIPSInst_RT(x) ((MIPSInst(x) & 0x001f0000) >> I_RT_SFT)

#define I_IMM_SFT	0
#define MIPSInst_SIMM(x) ((int)((short)(MIPSInst(x) & 0xffff)))
#define MIPSInst_UIMM(x) (MIPSInst(x) & 0xffff)

#define I_CACHEOP_SFT	18
#define MIPSInst_CACHEOP(x) ((MIPSInst(x) & 0x001c0000) >> I_CACHEOP_SFT)

#define I_CACHESEL_SFT	16
#define MIPSInst_CACHESEL(x) ((MIPSInst(x) & 0x00030000) >> I_CACHESEL_SFT)

#define I_RD_SFT	11
#define MIPSInst_RD(x) ((MIPSInst(x) & 0x0000f800) >> I_RD_SFT)

#define I_RE_SFT	6
#define MIPSInst_RE(x) ((MIPSInst(x) & 0x000007c0) >> I_RE_SFT)

#define I_FUNC_SFT	0
#define MIPSInst_FUNC(x) (MIPSInst(x) & 0x0000003f)

#define I_FFMT_SFT	21
#define MIPSInst_FFMT(x) ((MIPSInst(x) & 0x01e00000) >> I_FFMT_SFT)

#define I_FT_SFT	16
#define MIPSInst_FT(x) ((MIPSInst(x) & 0x001f0000) >> I_FT_SFT)

#define I_FS_SFT	11
#define MIPSInst_FS(x) ((MIPSInst(x) & 0x0000f800) >> I_FS_SFT)

#define I_FD_SFT	6
#define MIPSInst_FD(x) ((MIPSInst(x) & 0x000007c0) >> I_FD_SFT)

#define I_FR_SFT	21
#define MIPSInst_FR(x) ((MIPSInst(x) & 0x03e00000) >> I_FR_SFT)

#define I_FMA_FUNC_SFT	2
#define MIPSInst_FMA_FUNC(x) ((MIPSInst(x) & 0x0000003c) >> I_FMA_FUNC_SFT)

#define I_FMA_FFMT_SFT	0
#define MIPSInst_FMA_FFMT(x) (MIPSInst(x) & 0x00000003)

typedef unsigned int mips_instruction;

/* microMIPS instruction decode structure. Do NOT export!!! */
struct mm_decoded_insn {
	mips_instruction insn;
	mips_instruction next_insn;
	int pc_inc;
	int next_pc_inc;
	int micro_mips_mode;
};

/* Recode table from 16-bit register notation to 32-bit GPR. Do NOT export!!! */
extern const int reg16to32[];

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
		PTR_STR"\t1b, 4b\n\t"               \
		PTR_STR"\t2b, 4b\n\t"               \
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
		PTR_STR"\t1b, 4b\n\t"               \
		PTR_STR"\t2b, 4b\n\t"               \
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
		PTR_STR"\t1b, 11b\n\t"		    \
		PTR_STR"\t2b, 11b\n\t"		    \
		PTR_STR"\t3b, 11b\n\t"		    \
		PTR_STR"\t4b, 11b\n\t"		    \
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
		PTR_STR"\t1b, 4b\n\t"               \
		PTR_STR"\t2b, 4b\n\t"               \
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
		PTR_STR"\t1b, 4b\n\t"               \
		PTR_STR"\t2b, 4b\n\t"               \
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
		PTR_STR"\t1b, 4b\n\t"               \
		PTR_STR"\t2b, 4b\n\t"               \
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
		PTR_STR"\t1b, 11b\n\t"		    \
		PTR_STR"\t2b, 11b\n\t"		    \
		PTR_STR"\t3b, 11b\n\t"		    \
		PTR_STR"\t4b, 11b\n\t"		    \
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
		PTR_STR"\t1b, 11b\n\t"		    \
		PTR_STR"\t2b, 11b\n\t"		    \
		PTR_STR"\t3b, 11b\n\t"		    \
		PTR_STR"\t4b, 11b\n\t"		    \
		PTR_STR"\t5b, 11b\n\t"		    \
		PTR_STR"\t6b, 11b\n\t"		    \
		PTR_STR"\t7b, 11b\n\t"		    \
		PTR_STR"\t8b, 11b\n\t"		    \
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
		PTR_STR"\t1b, 4b\n\t"              \
		PTR_STR"\t2b, 4b\n\t"              \
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
		PTR_STR"\t1b, 4b\n\t"               \
		PTR_STR"\t2b, 4b\n\t"               \
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
		PTR_STR"\t1b, 4b\n\t"               \
		PTR_STR"\t2b, 4b\n\t"               \
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
		PTR_STR"\t1b, 11b\n\t"		    \
		PTR_STR"\t2b, 11b\n\t"		    \
		PTR_STR"\t3b, 11b\n\t"		    \
		PTR_STR"\t4b, 11b\n\t"		    \
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
		PTR_STR"\t1b, 11b\n\t"		    \
		PTR_STR"\t2b, 11b\n\t"		    \
		PTR_STR"\t3b, 11b\n\t"		    \
		PTR_STR"\t4b, 11b\n\t"		    \
		PTR_STR"\t5b, 11b\n\t"		    \
		PTR_STR"\t6b, 11b\n\t"		    \
		PTR_STR"\t7b, 11b\n\t"		    \
		PTR_STR"\t8b, 11b\n\t"		    \
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
		PTR_STR"\t1b, 4b\n\t"               \
		PTR_STR"\t2b, 4b\n\t"               \
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
		PTR_STR"\t1b, 4b\n\t"               \
		PTR_STR"\t2b, 4b\n\t"               \
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
		PTR_STR"\t1b, 11b\n\t"		    \
		PTR_STR"\t2b, 11b\n\t"		    \
		PTR_STR"\t3b, 11b\n\t"		    \
		PTR_STR"\t4b, 11b\n\t"		    \
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
		PTR_STR"\t1b, 4b\n\t"               \
		PTR_STR"\t2b, 4b\n\t"               \
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
		PTR_STR"\t1b, 4b\n\t"               \
		PTR_STR"\t2b, 4b\n\t"               \
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
		PTR_STR"\t1b, 4b\n\t"               \
		PTR_STR"\t2b, 4b\n\t"               \
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
		PTR_STR"\t1b, 11b\n\t"		    \
		PTR_STR"\t2b, 11b\n\t"		    \
		PTR_STR"\t3b, 11b\n\t"		    \
		PTR_STR"\t4b, 11b\n\t"		    \
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
		PTR_STR"\t1b, 11b\n\t"		    \
		PTR_STR"\t2b, 11b\n\t"		    \
		PTR_STR"\t3b, 11b\n\t"		    \
		PTR_STR"\t4b, 11b\n\t"		    \
		PTR_STR"\t5b, 11b\n\t"		    \
		PTR_STR"\t6b, 11b\n\t"		    \
		PTR_STR"\t7b, 11b\n\t"		    \
		PTR_STR"\t8b, 11b\n\t"		    \
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
		PTR_STR"\t1b, 4b\n\t"               \
		PTR_STR"\t2b, 4b\n\t"               \
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
		PTR_STR"\t1b, 4b\n\t"               \
		PTR_STR"\t2b, 4b\n\t"               \
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
		PTR_STR"\t1b, 4b\n\t"               \
		PTR_STR"\t2b, 4b\n\t"               \
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
		PTR_STR"\t1b, 11b\n\t"		    \
		PTR_STR"\t2b, 11b\n\t"		    \
		PTR_STR"\t3b, 11b\n\t"		    \
		PTR_STR"\t4b, 11b\n\t"		    \
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
		PTR_STR"\t1b, 11b\n\t"		    \
		PTR_STR"\t2b, 11b\n\t"		    \
		PTR_STR"\t3b, 11b\n\t"		    \
		PTR_STR"\t4b, 11b\n\t"		    \
		PTR_STR"\t5b, 11b\n\t"		    \
		PTR_STR"\t6b, 11b\n\t"		    \
		PTR_STR"\t7b, 11b\n\t"		    \
		PTR_STR"\t8b, 11b\n\t"		    \
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

#endif /* _ASM_INST_H */
