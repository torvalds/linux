/*
 * Just-In-Time compiler for BPF filters on 32bit ARM
 *
 * Copyright (c) 2011 Mircea Gherzan <mgherzan@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License.
 */

#ifndef PFILTER_OPCODES_ARM_H
#define PFILTER_OPCODES_ARM_H

/* ARM 32bit Registers */
#define ARM_R0	0
#define ARM_R1	1
#define ARM_R2	2
#define ARM_R3	3
#define ARM_R4	4
#define ARM_R5	5
#define ARM_R6	6
#define ARM_R7	7
#define ARM_R8	8
#define ARM_R9	9
#define ARM_R10	10
#define ARM_FP	11	/* Frame Pointer */
#define ARM_IP	12	/* Intra-procedure scratch register */
#define ARM_SP	13	/* Stack pointer: as load/store base reg */
#define ARM_LR	14	/* Link Register */
#define ARM_PC	15	/* Program counter */

#define ARM_COND_EQ		0x0	/* == */
#define ARM_COND_NE		0x1	/* != */
#define ARM_COND_CS		0x2	/* unsigned >= */
#define ARM_COND_HS		ARM_COND_CS
#define ARM_COND_CC		0x3	/* unsigned < */
#define ARM_COND_LO		ARM_COND_CC
#define ARM_COND_MI		0x4	/* < 0 */
#define ARM_COND_PL		0x5	/* >= 0 */
#define ARM_COND_VS		0x6	/* Signed Overflow */
#define ARM_COND_VC		0x7	/* No Signed Overflow */
#define ARM_COND_HI		0x8	/* unsigned > */
#define ARM_COND_LS		0x9	/* unsigned <= */
#define ARM_COND_GE		0xa	/* Signed >= */
#define ARM_COND_LT		0xb	/* Signed < */
#define ARM_COND_GT		0xc	/* Signed > */
#define ARM_COND_LE		0xd	/* Signed <= */
#define ARM_COND_AL		0xe	/* None */

/* register shift types */
#define SRTYPE_LSL		0
#define SRTYPE_LSR		1
#define SRTYPE_ASR		2
#define SRTYPE_ROR		3
#define SRTYPE_ASL		(SRTYPE_LSL)

#define ARM_INST_ADD_R		0x00800000
#define ARM_INST_ADDS_R		0x00900000
#define ARM_INST_ADC_R		0x00a00000
#define ARM_INST_ADC_I		0x02a00000
#define ARM_INST_ADD_I		0x02800000
#define ARM_INST_ADDS_I		0x02900000

#define ARM_INST_AND_R		0x00000000
#define ARM_INST_AND_I		0x02000000

#define ARM_INST_BIC_R		0x01c00000
#define ARM_INST_BIC_I		0x03c00000

#define ARM_INST_B		0x0a000000
#define ARM_INST_BX		0x012FFF10
#define ARM_INST_BLX_R		0x012fff30

#define ARM_INST_CMP_R		0x01500000
#define ARM_INST_CMP_I		0x03500000

#define ARM_INST_EOR_R		0x00200000
#define ARM_INST_EOR_I		0x02200000

#define ARM_INST_LDRB_I		0x05d00000
#define ARM_INST_LDRB_R		0x07d00000
#define ARM_INST_LDRH_I		0x01d000b0
#define ARM_INST_LDRH_R		0x019000b0
#define ARM_INST_LDR_I		0x05900000
#define ARM_INST_LDR_R		0x07900000

#define ARM_INST_LDM		0x08900000
#define ARM_INST_LDM_IA		0x08b00000

#define ARM_INST_LSL_I		0x01a00000
#define ARM_INST_LSL_R		0x01a00010

#define ARM_INST_LSR_I		0x01a00020
#define ARM_INST_LSR_R		0x01a00030

#define ARM_INST_MOV_R		0x01a00000
#define ARM_INST_MOVS_R		0x01b00000
#define ARM_INST_MOV_I		0x03a00000
#define ARM_INST_MOVW		0x03000000
#define ARM_INST_MOVT		0x03400000

#define ARM_INST_MUL		0x00000090

#define ARM_INST_POP		0x08bd0000
#define ARM_INST_PUSH		0x092d0000

#define ARM_INST_ORR_R		0x01800000
#define ARM_INST_ORRS_R		0x01900000
#define ARM_INST_ORR_I		0x03800000

#define ARM_INST_REV		0x06bf0f30
#define ARM_INST_REV16		0x06bf0fb0

#define ARM_INST_RSB_I		0x02600000
#define ARM_INST_RSBS_I		0x02700000
#define ARM_INST_RSC_I		0x02e00000

#define ARM_INST_SUB_R		0x00400000
#define ARM_INST_SUBS_R		0x00500000
#define ARM_INST_RSB_R		0x00600000
#define ARM_INST_SUB_I		0x02400000
#define ARM_INST_SUBS_I		0x02500000
#define ARM_INST_SBC_I		0x02c00000
#define ARM_INST_SBC_R		0x00c00000
#define ARM_INST_SBCS_R		0x00d00000

#define ARM_INST_STR_I		0x05800000
#define ARM_INST_STRB_I		0x05c00000
#define ARM_INST_STRH_I		0x01c000b0

#define ARM_INST_TST_R		0x01100000
#define ARM_INST_TST_I		0x03100000

#define ARM_INST_UDIV		0x0730f010

#define ARM_INST_UMULL		0x00800090

#define ARM_INST_MLS		0x00600090

#define ARM_INST_UXTH		0x06ff0070

/*
 * Use a suitable undefined instruction to use for ARM/Thumb2 faulting.
 * We need to be careful not to conflict with those used by other modules
 * (BUG, kprobes, etc) and the register_undef_hook() system.
 *
 * The ARM architecture reference manual guarantees that the following
 * instruction space will produce an undefined instruction exception on
 * all CPUs:
 *
 * ARM:   xxxx 0111 1111 xxxx xxxx xxxx 1111 xxxx	ARMv7-AR, section A5.4
 * Thumb: 1101 1110 xxxx xxxx				ARMv7-M, section A5.2.6
 */
#define ARM_INST_UDF		0xe7fddef1

/* register */
#define _AL3_R(op, rd, rn, rm)	((op ## _R) | (rd) << 12 | (rn) << 16 | (rm))
/* immediate */
#define _AL3_I(op, rd, rn, imm)	((op ## _I) | (rd) << 12 | (rn) << 16 | (imm))
/* register with register-shift */
#define _AL3_SR(inst)	(inst | (1 << 4))

#define ARM_ADD_R(rd, rn, rm)	_AL3_R(ARM_INST_ADD, rd, rn, rm)
#define ARM_ADDS_R(rd, rn, rm)	_AL3_R(ARM_INST_ADDS, rd, rn, rm)
#define ARM_ADD_I(rd, rn, imm)	_AL3_I(ARM_INST_ADD, rd, rn, imm)
#define ARM_ADDS_I(rd, rn, imm)	_AL3_I(ARM_INST_ADDS, rd, rn, imm)
#define ARM_ADC_R(rd, rn, rm)	_AL3_R(ARM_INST_ADC, rd, rn, rm)
#define ARM_ADC_I(rd, rn, imm)	_AL3_I(ARM_INST_ADC, rd, rn, imm)

#define ARM_AND_R(rd, rn, rm)	_AL3_R(ARM_INST_AND, rd, rn, rm)
#define ARM_AND_I(rd, rn, imm)	_AL3_I(ARM_INST_AND, rd, rn, imm)

#define ARM_BIC_R(rd, rn, rm)	_AL3_R(ARM_INST_BIC, rd, rn, rm)
#define ARM_BIC_I(rd, rn, imm)	_AL3_I(ARM_INST_BIC, rd, rn, imm)

#define ARM_B(imm24)		(ARM_INST_B | ((imm24) & 0xffffff))
#define ARM_BX(rm)		(ARM_INST_BX | (rm))
#define ARM_BLX_R(rm)		(ARM_INST_BLX_R | (rm))

#define ARM_CMP_R(rn, rm)	_AL3_R(ARM_INST_CMP, 0, rn, rm)
#define ARM_CMP_I(rn, imm)	_AL3_I(ARM_INST_CMP, 0, rn, imm)

#define ARM_EOR_R(rd, rn, rm)	_AL3_R(ARM_INST_EOR, rd, rn, rm)
#define ARM_EOR_I(rd, rn, imm)	_AL3_I(ARM_INST_EOR, rd, rn, imm)

#define ARM_LDR_I(rt, rn, off)	(ARM_INST_LDR_I | (rt) << 12 | (rn) << 16 \
				 | ((off) & 0xfff))
#define ARM_LDR_R(rt, rn, rm)	(ARM_INST_LDR_R | (rt) << 12 | (rn) << 16 \
				 | (rm))
#define ARM_LDRB_I(rt, rn, off)	(ARM_INST_LDRB_I | (rt) << 12 | (rn) << 16 \
				 | (off))
#define ARM_LDRB_R(rt, rn, rm)	(ARM_INST_LDRB_R | (rt) << 12 | (rn) << 16 \
				 | (rm))
#define ARM_LDRH_I(rt, rn, off)	(ARM_INST_LDRH_I | (rt) << 12 | (rn) << 16 \
				 | (((off) & 0xf0) << 4) | ((off) & 0xf))
#define ARM_LDRH_R(rt, rn, rm)	(ARM_INST_LDRH_R | (rt) << 12 | (rn) << 16 \
				 | (rm))

#define ARM_LDM(rn, regs)	(ARM_INST_LDM | (rn) << 16 | (regs))
#define ARM_LDM_IA(rn, regs)	(ARM_INST_LDM_IA | (rn) << 16 | (regs))

#define ARM_LSL_R(rd, rn, rm)	(_AL3_R(ARM_INST_LSL, rd, 0, rn) | (rm) << 8)
#define ARM_LSL_I(rd, rn, imm)	(_AL3_I(ARM_INST_LSL, rd, 0, rn) | (imm) << 7)

#define ARM_LSR_R(rd, rn, rm)	(_AL3_R(ARM_INST_LSR, rd, 0, rn) | (rm) << 8)
#define ARM_LSR_I(rd, rn, imm)	(_AL3_I(ARM_INST_LSR, rd, 0, rn) | (imm) << 7)
#define ARM_ASR_R(rd, rn, rm)   (_AL3_R(ARM_INST_ASR, rd, 0, rn) | (rm) << 8)
#define ARM_ASR_I(rd, rn, imm)  (_AL3_I(ARM_INST_ASR, rd, 0, rn) | (imm) << 7)

#define ARM_MOV_R(rd, rm)	_AL3_R(ARM_INST_MOV, rd, 0, rm)
#define ARM_MOVS_R(rd, rm)	_AL3_R(ARM_INST_MOVS, rd, 0, rm)
#define ARM_MOV_I(rd, imm)	_AL3_I(ARM_INST_MOV, rd, 0, imm)
#define ARM_MOV_SR(rd, rm, type, rs)	\
	(_AL3_SR(ARM_MOV_R(rd, rm)) | (type) << 5 | (rs) << 8)
#define ARM_MOV_SI(rd, rm, type, imm6)	\
	(ARM_MOV_R(rd, rm) | (type) << 5 | (imm6) << 7)

#define ARM_MOVW(rd, imm)	\
	(ARM_INST_MOVW | ((imm) >> 12) << 16 | (rd) << 12 | ((imm) & 0x0fff))

#define ARM_MOVT(rd, imm)	\
	(ARM_INST_MOVT | ((imm) >> 12) << 16 | (rd) << 12 | ((imm) & 0x0fff))

#define ARM_MUL(rd, rm, rn)	(ARM_INST_MUL | (rd) << 16 | (rm) << 8 | (rn))

#define ARM_POP(regs)		(ARM_INST_POP | (regs))
#define ARM_PUSH(regs)		(ARM_INST_PUSH | (regs))

#define ARM_ORR_R(rd, rn, rm)	_AL3_R(ARM_INST_ORR, rd, rn, rm)
#define ARM_ORR_I(rd, rn, imm)	_AL3_I(ARM_INST_ORR, rd, rn, imm)
#define ARM_ORR_SR(rd, rn, rm, type, rs)	\
	(_AL3_SR(ARM_ORR_R(rd, rn, rm)) | (type) << 5 | (rs) << 8)
#define ARM_ORRS_R(rd, rn, rm)	_AL3_R(ARM_INST_ORRS, rd, rn, rm)
#define ARM_ORRS_SR(rd, rn, rm, type, rs)	\
	(_AL3_SR(ARM_ORRS_R(rd, rn, rm)) | (type) << 5 | (rs) << 8)
#define ARM_ORR_SI(rd, rn, rm, type, imm6)	\
	(ARM_ORR_R(rd, rn, rm) | (type) << 5 | (imm6) << 7)
#define ARM_ORRS_SI(rd, rn, rm, type, imm6)	\
	(ARM_ORRS_R(rd, rn, rm) | (type) << 5 | (imm6) << 7)

#define ARM_REV(rd, rm)		(ARM_INST_REV | (rd) << 12 | (rm))
#define ARM_REV16(rd, rm)	(ARM_INST_REV16 | (rd) << 12 | (rm))

#define ARM_RSB_I(rd, rn, imm)	_AL3_I(ARM_INST_RSB, rd, rn, imm)
#define ARM_RSBS_I(rd, rn, imm)	_AL3_I(ARM_INST_RSBS, rd, rn, imm)
#define ARM_RSC_I(rd, rn, imm)	_AL3_I(ARM_INST_RSC, rd, rn, imm)

#define ARM_SUB_R(rd, rn, rm)	_AL3_R(ARM_INST_SUB, rd, rn, rm)
#define ARM_SUBS_R(rd, rn, rm)	_AL3_R(ARM_INST_SUBS, rd, rn, rm)
#define ARM_RSB_R(rd, rn, rm)	_AL3_R(ARM_INST_RSB, rd, rn, rm)
#define ARM_SBC_R(rd, rn, rm)	_AL3_R(ARM_INST_SBC, rd, rn, rm)
#define ARM_SBCS_R(rd, rn, rm)	_AL3_R(ARM_INST_SBCS, rd, rn, rm)
#define ARM_SUB_I(rd, rn, imm)	_AL3_I(ARM_INST_SUB, rd, rn, imm)
#define ARM_SUBS_I(rd, rn, imm)	_AL3_I(ARM_INST_SUBS, rd, rn, imm)
#define ARM_SBC_I(rd, rn, imm)	_AL3_I(ARM_INST_SBC, rd, rn, imm)

#define ARM_STR_I(rt, rn, off)	(ARM_INST_STR_I | (rt) << 12 | (rn) << 16 \
				 | ((off) & 0xfff))
#define ARM_STRH_I(rt, rn, off)	(ARM_INST_STRH_I | (rt) << 12 | (rn) << 16 \
				 | (((off) & 0xf0) << 4) | ((off) & 0xf))
#define ARM_STRB_I(rt, rn, off)	(ARM_INST_STRB_I | (rt) << 12 | (rn) << 16 \
				 | (((off) & 0xf0) << 4) | ((off) & 0xf))

#define ARM_TST_R(rn, rm)	_AL3_R(ARM_INST_TST, 0, rn, rm)
#define ARM_TST_I(rn, imm)	_AL3_I(ARM_INST_TST, 0, rn, imm)

#define ARM_UDIV(rd, rn, rm)	(ARM_INST_UDIV | (rd) << 16 | (rn) | (rm) << 8)

#define ARM_UMULL(rd_lo, rd_hi, rn, rm)	(ARM_INST_UMULL | (rd_hi) << 16 \
					 | (rd_lo) << 12 | (rm) << 8 | rn)

#define ARM_MLS(rd, rn, rm, ra)	(ARM_INST_MLS | (rd) << 16 | (rn) | (rm) << 8 \
				 | (ra) << 12)
#define ARM_UXTH(rd, rm)	(ARM_INST_UXTH | (rd) << 12 | (rm))

#endif /* PFILTER_OPCODES_ARM_H */
