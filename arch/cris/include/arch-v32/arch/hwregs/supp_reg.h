/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SUPP_REG_H__
#define __SUPP_REG_H__

/* Macros for reading and writing support/special registers. */

#ifndef STRINGIFYFY
#define STRINGIFYFY(i) #i
#endif

#ifndef STRINGIFY
#define STRINGIFY(i) STRINGIFYFY(i)
#endif

#define SPEC_REG_BZ     "BZ"
#define SPEC_REG_VR     "VR"
#define SPEC_REG_PID    "PID"
#define SPEC_REG_SRS    "SRS"
#define SPEC_REG_WZ     "WZ"
#define SPEC_REG_EXS    "EXS"
#define SPEC_REG_EDA    "EDA"
#define SPEC_REG_MOF    "MOF"
#define SPEC_REG_DZ     "DZ"
#define SPEC_REG_EBP    "EBP"
#define SPEC_REG_ERP    "ERP"
#define SPEC_REG_SRP    "SRP"
#define SPEC_REG_NRP    "NRP"
#define SPEC_REG_CCS    "CCS"
#define SPEC_REG_USP    "USP"
#define SPEC_REG_SPC    "SPC"

#define RW_MM_CFG       0
#define RW_MM_KBASE_LO  1
#define RW_MM_KBASE_HI  2
#define RW_MM_CAUSE     3
#define RW_MM_TLB_SEL   4
#define RW_MM_TLB_LO    5
#define RW_MM_TLB_HI    6
#define RW_MM_TLB_PGD   7

#define BANK_GC		0
#define BANK_IM		1
#define BANK_DM		2
#define BANK_BP		3

#define RW_GC_CFG       0
#define RW_GC_CCS       1
#define RW_GC_SRS       2
#define RW_GC_NRP       3
#define RW_GC_EXS       4
#define RW_GC_R0        8
#define RW_GC_R1        9

#define SPEC_REG_WR(r,v) \
__asm__ __volatile__ ("move %0, $" r : : "r" (v));

#define SPEC_REG_RD(r,v) \
__asm__ __volatile__ ("move $" r ",%0" : "=r" (v));

#define NOP() \
	__asm__ __volatile__ ("nop");

#define SUPP_BANK_SEL(b) 		\
	SPEC_REG_WR(SPEC_REG_SRS,b);	\
	NOP();				\
	NOP();				\
	NOP();

#define SUPP_REG_WR(r,v) \
__asm__ __volatile__ ("move %0, $S" STRINGIFYFY(r) "\n\t"	\
		      "nop\n\t"					\
		      "nop\n\t"					\
		      "nop\n\t"					\
		      : : "r" (v));

#define SUPP_REG_RD(r,v) \
__asm__ __volatile__ ("move $S" STRINGIFYFY(r) ",%0" : "=r" (v));

#endif /* __SUPP_REG_H__ */
