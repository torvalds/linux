/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __ASM_INSN_DEF_H
#define __ASM_INSN_DEF_H

#include <asm/asm.h>

#define INSN_R_FUNC7_SHIFT		25
#define INSN_R_RS2_SHIFT		20
#define INSN_R_RS1_SHIFT		15
#define INSN_R_FUNC3_SHIFT		12
#define INSN_R_RD_SHIFT			 7
#define INSN_R_OPCODE_SHIFT		 0

#ifdef __ASSEMBLY__

#ifdef CONFIG_AS_HAS_INSN

	.macro insn_r, opcode, func3, func7, rd, rs1, rs2
	.insn	r \opcode, \func3, \func7, \rd, \rs1, \rs2
	.endm

#else

#include <asm/gpr-num.h>

	.macro insn_r, opcode, func3, func7, rd, rs1, rs2
	.4byte	((\opcode << INSN_R_OPCODE_SHIFT) |		\
		 (\func3 << INSN_R_FUNC3_SHIFT) |		\
		 (\func7 << INSN_R_FUNC7_SHIFT) |		\
		 (.L__gpr_num_\rd << INSN_R_RD_SHIFT) |		\
		 (.L__gpr_num_\rs1 << INSN_R_RS1_SHIFT) |	\
		 (.L__gpr_num_\rs2 << INSN_R_RS2_SHIFT))
	.endm

#endif

#define __INSN_R(...)	insn_r __VA_ARGS__

#else /* ! __ASSEMBLY__ */

#ifdef CONFIG_AS_HAS_INSN

#define __INSN_R(opcode, func3, func7, rd, rs1, rs2)	\
	".insn	r " opcode ", " func3 ", " func7 ", " rd ", " rs1 ", " rs2 "\n"

#else

#include <linux/stringify.h>
#include <asm/gpr-num.h>

#define DEFINE_INSN_R							\
	__DEFINE_ASM_GPR_NUMS						\
"	.macro insn_r, opcode, func3, func7, rd, rs1, rs2\n"		\
"	.4byte	((\\opcode << " __stringify(INSN_R_OPCODE_SHIFT) ") |"	\
"		 (\\func3 << " __stringify(INSN_R_FUNC3_SHIFT) ") |"	\
"		 (\\func7 << " __stringify(INSN_R_FUNC7_SHIFT) ") |"	\
"		 (.L__gpr_num_\\rd << " __stringify(INSN_R_RD_SHIFT) ") |"    \
"		 (.L__gpr_num_\\rs1 << " __stringify(INSN_R_RS1_SHIFT) ") |"  \
"		 (.L__gpr_num_\\rs2 << " __stringify(INSN_R_RS2_SHIFT) "))\n" \
"	.endm\n"

#define UNDEFINE_INSN_R							\
"	.purgem insn_r\n"

#define __INSN_R(opcode, func3, func7, rd, rs1, rs2)			\
	DEFINE_INSN_R							\
	"insn_r " opcode ", " func3 ", " func7 ", " rd ", " rs1 ", " rs2 "\n" \
	UNDEFINE_INSN_R

#endif

#endif /* ! __ASSEMBLY__ */

#define INSN_R(opcode, func3, func7, rd, rs1, rs2)		\
	__INSN_R(RV_##opcode, RV_##func3, RV_##func7,		\
		 RV_##rd, RV_##rs1, RV_##rs2)

#define RV_OPCODE(v)		__ASM_STR(v)
#define RV_FUNC3(v)		__ASM_STR(v)
#define RV_FUNC7(v)		__ASM_STR(v)
#define RV_RD(v)		__ASM_STR(v)
#define RV_RS1(v)		__ASM_STR(v)
#define RV_RS2(v)		__ASM_STR(v)
#define __RV_REG(v)		__ASM_STR(x ## v)
#define RV___RD(v)		__RV_REG(v)
#define RV___RS1(v)		__RV_REG(v)
#define RV___RS2(v)		__RV_REG(v)

#define RV_OPCODE_SYSTEM	RV_OPCODE(115)

#define HFENCE_VVMA(vaddr, asid)				\
	INSN_R(OPCODE_SYSTEM, FUNC3(0), FUNC7(17),		\
	       __RD(0), RS1(vaddr), RS2(asid))

#define HFENCE_GVMA(gaddr, vmid)				\
	INSN_R(OPCODE_SYSTEM, FUNC3(0), FUNC7(49),		\
	       __RD(0), RS1(gaddr), RS2(vmid))

#define HLVX_HU(dest, addr)					\
	INSN_R(OPCODE_SYSTEM, FUNC3(4), FUNC7(50),		\
	       RD(dest), RS1(addr), __RS2(3))

#define HLV_W(dest, addr)					\
	INSN_R(OPCODE_SYSTEM, FUNC3(4), FUNC7(52),		\
	       RD(dest), RS1(addr), __RS2(0))

#ifdef CONFIG_64BIT
#define HLV_D(dest, addr)					\
	INSN_R(OPCODE_SYSTEM, FUNC3(4), FUNC7(54),		\
	       RD(dest), RS1(addr), __RS2(0))
#else
#define HLV_D(dest, addr)					\
	__ASM_STR(.error "hlv.d requires 64-bit support")
#endif

#define SINVAL_VMA(vaddr, asid)					\
	INSN_R(OPCODE_SYSTEM, FUNC3(0), FUNC7(11),		\
	       __RD(0), RS1(vaddr), RS2(asid))

#define SFENCE_W_INVAL()					\
	INSN_R(OPCODE_SYSTEM, FUNC3(0), FUNC7(12),		\
	       __RD(0), __RS1(0), __RS2(0))

#define SFENCE_INVAL_IR()					\
	INSN_R(OPCODE_SYSTEM, FUNC3(0), FUNC7(12),		\
	       __RD(0), __RS1(0), __RS2(1))

#define HINVAL_VVMA(vaddr, asid)				\
	INSN_R(OPCODE_SYSTEM, FUNC3(0), FUNC7(19),		\
	       __RD(0), RS1(vaddr), RS2(asid))

#define HINVAL_GVMA(gaddr, vmid)				\
	INSN_R(OPCODE_SYSTEM, FUNC3(0), FUNC7(51),		\
	       __RD(0), RS1(gaddr), RS2(vmid))

#endif /* __ASM_INSN_DEF_H */
