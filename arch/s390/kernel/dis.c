/*
 * Disassemble s390 instructions.
 *
 * Copyright IBM Corp. 2007
 * Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/kallsyms.h>
#include <linux/reboot.h>
#include <linux/kprobes.h>
#include <linux/kdebug.h>

#include <linux/uaccess.h>
#include <asm/dis.h>
#include <asm/io.h>
#include <linux/atomic.h>
#include <asm/cpcmd.h>
#include <asm/lowcore.h>
#include <asm/debug.h>
#include <asm/irq.h>

enum {
	UNUSED,	/* Indicates the end of the operand list */
	R_8,	/* GPR starting at position 8 */
	R_12,	/* GPR starting at position 12 */
	R_16,	/* GPR starting at position 16 */
	R_20,	/* GPR starting at position 20 */
	R_24,	/* GPR starting at position 24 */
	R_28,	/* GPR starting at position 28 */
	R_32,	/* GPR starting at position 32 */
	F_8,	/* FPR starting at position 8 */
	F_12,	/* FPR starting at position 12 */
	F_16,	/* FPR starting at position 16 */
	F_20,	/* FPR starting at position 16 */
	F_24,	/* FPR starting at position 24 */
	F_28,	/* FPR starting at position 28 */
	F_32,	/* FPR starting at position 32 */
	A_8,	/* Access reg. starting at position 8 */
	A_12,	/* Access reg. starting at position 12 */
	A_24,	/* Access reg. starting at position 24 */
	A_28,	/* Access reg. starting at position 28 */
	C_8,	/* Control reg. starting at position 8 */
	C_12,	/* Control reg. starting at position 12 */
	V_8,	/* Vector reg. starting at position 8, extension bit at 36 */
	V_12,	/* Vector reg. starting at position 12, extension bit at 37 */
	V_16,	/* Vector reg. starting at position 16, extension bit at 38 */
	V_32,	/* Vector reg. starting at position 32, extension bit at 39 */
	W_12,	/* Vector reg. at bit 12, extension at bit 37, used as index */
	B_16,	/* Base register starting at position 16 */
	B_32,	/* Base register starting at position 32 */
	X_12,	/* Index register starting at position 12 */
	D_20,	/* Displacement starting at position 20 */
	D_36,	/* Displacement starting at position 36 */
	D20_20,	/* 20 bit displacement starting at 20 */
	L4_8,	/* 4 bit length starting at position 8 */
	L4_12,	/* 4 bit length starting at position 12 */
	L8_8,	/* 8 bit length starting at position 8 */
	U4_8,	/* 4 bit unsigned value starting at 8 */
	U4_12,	/* 4 bit unsigned value starting at 12 */
	U4_16,	/* 4 bit unsigned value starting at 16 */
	U4_20,	/* 4 bit unsigned value starting at 20 */
	U4_24,	/* 4 bit unsigned value starting at 24 */
	U4_28,	/* 4 bit unsigned value starting at 28 */
	U4_32,	/* 4 bit unsigned value starting at 32 */
	U4_36,	/* 4 bit unsigned value starting at 36 */
	U8_8,	/* 8 bit unsigned value starting at 8 */
	U8_16,	/* 8 bit unsigned value starting at 16 */
	U8_24,	/* 8 bit unsigned value starting at 24 */
	U8_32,	/* 8 bit unsigned value starting at 32 */
	I8_8,	/* 8 bit signed value starting at 8 */
	I8_16,	/* 8 bit signed value starting at 16 */
	I8_24,	/* 8 bit signed value starting at 24 */
	I8_32,	/* 8 bit signed value starting at 32 */
	J12_12, /* PC relative offset at 12 */
	I16_16,	/* 16 bit signed value starting at 16 */
	I16_32,	/* 32 bit signed value starting at 16 */
	U16_16,	/* 16 bit unsigned value starting at 16 */
	U16_32,	/* 32 bit unsigned value starting at 16 */
	J16_16,	/* PC relative jump offset at 16 */
	J16_32, /* PC relative offset at 16 */
	I24_24, /* 24 bit signed value starting at 24 */
	J32_16,	/* PC relative long offset at 16 */
	I32_16,	/* 32 bit signed value starting at 16 */
	U32_16,	/* 32 bit unsigned value starting at 16 */
	M_16,	/* 4 bit optional mask starting at 16 */
	M_20,	/* 4 bit optional mask starting at 20 */
	M_24,	/* 4 bit optional mask starting at 24 */
	M_28,	/* 4 bit optional mask starting at 28 */
	M_32,	/* 4 bit optional mask starting at 32 */
	RO_28,	/* optional GPR starting at position 28 */
};

/*
 * Enumeration of the different instruction formats.
 * For details consult the principles of operation.
 */
enum {
	INSTR_INVALID,
	INSTR_E,
	INSTR_IE_UU,
	INSTR_MII_UPI,
	INSTR_RIE_R0IU, INSTR_RIE_R0UU, INSTR_RIE_RRP, INSTR_RIE_RRPU,
	INSTR_RIE_RRUUU, INSTR_RIE_RUPI, INSTR_RIE_RUPU, INSTR_RIE_RRI0,
	INSTR_RIL_RI, INSTR_RIL_RP, INSTR_RIL_RU, INSTR_RIL_UP,
	INSTR_RIS_R0RDU, INSTR_RIS_R0UU, INSTR_RIS_RURDI, INSTR_RIS_RURDU,
	INSTR_RI_RI, INSTR_RI_RP, INSTR_RI_RU, INSTR_RI_UP,
	INSTR_RRE_00, INSTR_RRE_0R, INSTR_RRE_AA, INSTR_RRE_AR, INSTR_RRE_F0,
	INSTR_RRE_FF, INSTR_RRE_FR, INSTR_RRE_R0, INSTR_RRE_RA, INSTR_RRE_RF,
	INSTR_RRE_RR, INSTR_RRE_RR_OPT,
	INSTR_RRF_0UFF, INSTR_RRF_F0FF, INSTR_RRF_F0FF2, INSTR_RRF_F0FR,
	INSTR_RRF_FFRU, INSTR_RRF_FUFF, INSTR_RRF_FUFF2, INSTR_RRF_M0RR,
	INSTR_RRF_R0RR,	INSTR_RRF_R0RR2, INSTR_RRF_RMRR, INSTR_RRF_RURR,
	INSTR_RRF_U0FF,	INSTR_RRF_U0RF, INSTR_RRF_U0RR, INSTR_RRF_UUFF,
	INSTR_RRF_UUFR, INSTR_RRF_UURF,
	INSTR_RRR_F0FF, INSTR_RRS_RRRDU,
	INSTR_RR_FF, INSTR_RR_R0, INSTR_RR_RR, INSTR_RR_U0, INSTR_RR_UR,
	INSTR_RSE_CCRD, INSTR_RSE_RRRD, INSTR_RSE_RURD,
	INSTR_RSI_RRP,
	INSTR_RSL_LRDFU, INSTR_RSL_R0RD,
	INSTR_RSY_AARD, INSTR_RSY_CCRD, INSTR_RSY_RRRD, INSTR_RSY_RURD,
	INSTR_RSY_RDRM, INSTR_RSY_RMRD,
	INSTR_RS_AARD, INSTR_RS_CCRD, INSTR_RS_R0RD, INSTR_RS_RRRD,
	INSTR_RS_RURD,
	INSTR_RXE_FRRD, INSTR_RXE_RRRD, INSTR_RXE_RRRDM,
	INSTR_RXF_FRRDF,
	INSTR_RXY_FRRD, INSTR_RXY_RRRD, INSTR_RXY_URRD,
	INSTR_RX_FRRD, INSTR_RX_RRRD, INSTR_RX_URRD,
	INSTR_SIL_RDI, INSTR_SIL_RDU,
	INSTR_SIY_IRD, INSTR_SIY_URD,
	INSTR_SI_URD,
	INSTR_SMI_U0RDP,
	INSTR_SSE_RDRD,
	INSTR_SSF_RRDRD, INSTR_SSF_RRDRD2,
	INSTR_SS_L0RDRD, INSTR_SS_LIRDRD, INSTR_SS_LLRDRD, INSTR_SS_RRRDRD,
	INSTR_SS_RRRDRD2, INSTR_SS_RRRDRD3,
	INSTR_S_00, INSTR_S_RD,
	INSTR_VRI_V0IM, INSTR_VRI_V0I0, INSTR_VRI_V0IIM, INSTR_VRI_VVIM,
	INSTR_VRI_VVV0IM, INSTR_VRI_VVV0I0, INSTR_VRI_VVIMM,
	INSTR_VRR_VV00MMM, INSTR_VRR_VV000MM, INSTR_VRR_VV0000M,
	INSTR_VRR_VV00000, INSTR_VRR_VVV0M0M, INSTR_VRR_VV00M0M,
	INSTR_VRR_VVV000M, INSTR_VRR_VVV000V, INSTR_VRR_VVV0000,
	INSTR_VRR_VVV0MMM, INSTR_VRR_VVV00MM, INSTR_VRR_VVVMM0V,
	INSTR_VRR_VVVM0MV, INSTR_VRR_VVVM00V, INSTR_VRR_VRR0000,
	INSTR_VRS_VVRDM, INSTR_VRS_VVRD0, INSTR_VRS_VRRDM, INSTR_VRS_VRRD0,
	INSTR_VRS_RVRDM,
	INSTR_VRV_VVRDM, INSTR_VRV_VWRDM,
	INSTR_VRX_VRRDM, INSTR_VRX_VRRD0,
};

static const struct s390_operand operands[] =
{
	[UNUSED]  = { 0, 0, 0 },
	[R_8]	 = {  4,  8, OPERAND_GPR },
	[R_12]	 = {  4, 12, OPERAND_GPR },
	[R_16]	 = {  4, 16, OPERAND_GPR },
	[R_20]	 = {  4, 20, OPERAND_GPR },
	[R_24]	 = {  4, 24, OPERAND_GPR },
	[R_28]	 = {  4, 28, OPERAND_GPR },
	[R_32]	 = {  4, 32, OPERAND_GPR },
	[F_8]	 = {  4,  8, OPERAND_FPR },
	[F_12]	 = {  4, 12, OPERAND_FPR },
	[F_16]	 = {  4, 16, OPERAND_FPR },
	[F_20]	 = {  4, 16, OPERAND_FPR },
	[F_24]	 = {  4, 24, OPERAND_FPR },
	[F_28]	 = {  4, 28, OPERAND_FPR },
	[F_32]	 = {  4, 32, OPERAND_FPR },
	[A_8]	 = {  4,  8, OPERAND_AR },
	[A_12]	 = {  4, 12, OPERAND_AR },
	[A_24]	 = {  4, 24, OPERAND_AR },
	[A_28]	 = {  4, 28, OPERAND_AR },
	[C_8]	 = {  4,  8, OPERAND_CR },
	[C_12]	 = {  4, 12, OPERAND_CR },
	[V_8]	 = {  4,  8, OPERAND_VR },
	[V_12]	 = {  4, 12, OPERAND_VR },
	[V_16]	 = {  4, 16, OPERAND_VR },
	[V_32]	 = {  4, 32, OPERAND_VR },
	[W_12]	 = {  4, 12, OPERAND_INDEX | OPERAND_VR },
	[B_16]	 = {  4, 16, OPERAND_BASE | OPERAND_GPR },
	[B_32]	 = {  4, 32, OPERAND_BASE | OPERAND_GPR },
	[X_12]	 = {  4, 12, OPERAND_INDEX | OPERAND_GPR },
	[D_20]	 = { 12, 20, OPERAND_DISP },
	[D_36]	 = { 12, 36, OPERAND_DISP },
	[D20_20] = { 20, 20, OPERAND_DISP | OPERAND_SIGNED },
	[L4_8]	 = {  4,  8, OPERAND_LENGTH },
	[L4_12]  = {  4, 12, OPERAND_LENGTH },
	[L8_8]	 = {  8,  8, OPERAND_LENGTH },
	[U4_8]	 = {  4,  8, 0 },
	[U4_12]  = {  4, 12, 0 },
	[U4_16]  = {  4, 16, 0 },
	[U4_20]  = {  4, 20, 0 },
	[U4_24]  = {  4, 24, 0 },
	[U4_28]  = {  4, 28, 0 },
	[U4_32]  = {  4, 32, 0 },
	[U4_36]  = {  4, 36, 0 },
	[U8_8]	 = {  8,  8, 0 },
	[U8_16]  = {  8, 16, 0 },
	[U8_24]  = {  8, 24, 0 },
	[U8_32]  = {  8, 32, 0 },
	[J12_12] = { 12, 12, OPERAND_PCREL },
	[I8_8]	 = {  8,  8, OPERAND_SIGNED },
	[I8_16]  = {  8, 16, OPERAND_SIGNED },
	[I8_24]  = {  8, 24, OPERAND_SIGNED },
	[I8_32]  = {  8, 32, OPERAND_SIGNED },
	[I16_32] = { 16, 32, OPERAND_SIGNED },
	[I16_16] = { 16, 16, OPERAND_SIGNED },
	[U16_16] = { 16, 16, 0 },
	[U16_32] = { 16, 32, 0 },
	[J16_16] = { 16, 16, OPERAND_PCREL },
	[J16_32] = { 16, 32, OPERAND_PCREL },
	[I24_24] = { 24, 24, OPERAND_SIGNED },
	[J32_16] = { 32, 16, OPERAND_PCREL },
	[I32_16] = { 32, 16, OPERAND_SIGNED },
	[U32_16] = { 32, 16, 0 },
	[M_16]	 = {  4, 16, 0 },
	[M_20]	 = {  4, 20, 0 },
	[M_24]	 = {  4, 24, 0 },
	[M_28]	 = {  4, 28, 0 },
	[M_32]	 = {  4, 32, 0 },
	[RO_28]  = {  4, 28, OPERAND_GPR }
};

static const unsigned char formats[][7] = {
	[INSTR_E]	  = { 0xff, 0,0,0,0,0,0 },
	[INSTR_IE_UU]	  = { 0xff, U4_24,U4_28,0,0,0,0 },
	[INSTR_MII_UPI]	  = { 0xff, U4_8,J12_12,I24_24 },
	[INSTR_RIE_R0IU]  = { 0xff, R_8,I16_16,U4_32,0,0,0 },
	[INSTR_RIE_R0UU]  = { 0xff, R_8,U16_16,U4_32,0,0,0 },
	[INSTR_RIE_RRI0]  = { 0xff, R_8,R_12,I16_16,0,0,0 },
	[INSTR_RIE_RRPU]  = { 0xff, R_8,R_12,U4_32,J16_16,0,0 },
	[INSTR_RIE_RRP]	  = { 0xff, R_8,R_12,J16_16,0,0,0 },
	[INSTR_RIE_RRUUU] = { 0xff, R_8,R_12,U8_16,U8_24,U8_32,0 },
	[INSTR_RIE_RUPI]  = { 0xff, R_8,I8_32,U4_12,J16_16,0,0 },
	[INSTR_RIE_RUPU]  = { 0xff, R_8,U8_32,U4_12,J16_16,0,0 },
	[INSTR_RIL_RI]	  = { 0x0f, R_8,I32_16,0,0,0,0 },
	[INSTR_RIL_RP]	  = { 0x0f, R_8,J32_16,0,0,0,0 },
	[INSTR_RIL_RU]	  = { 0x0f, R_8,U32_16,0,0,0,0 },
	[INSTR_RIL_UP]	  = { 0x0f, U4_8,J32_16,0,0,0,0 },
	[INSTR_RIS_R0RDU] = { 0xff, R_8,U8_32,D_20,B_16,0,0 },
	[INSTR_RIS_RURDI] = { 0xff, R_8,I8_32,U4_12,D_20,B_16,0 },
	[INSTR_RIS_RURDU] = { 0xff, R_8,U8_32,U4_12,D_20,B_16,0 },
	[INSTR_RI_RI]	  = { 0x0f, R_8,I16_16,0,0,0,0 },
	[INSTR_RI_RP]	  = { 0x0f, R_8,J16_16,0,0,0,0 },
	[INSTR_RI_RU]	  = { 0x0f, R_8,U16_16,0,0,0,0 },
	[INSTR_RI_UP]	  = { 0x0f, U4_8,J16_16,0,0,0,0 },
	[INSTR_RRE_00]	  = { 0xff, 0,0,0,0,0,0 },
	[INSTR_RRE_0R]	  = { 0xff, R_28,0,0,0,0,0 },
	[INSTR_RRE_AA]	  = { 0xff, A_24,A_28,0,0,0,0 },
	[INSTR_RRE_AR]	  = { 0xff, A_24,R_28,0,0,0,0 },
	[INSTR_RRE_F0]	  = { 0xff, F_24,0,0,0,0,0 },
	[INSTR_RRE_FF]	  = { 0xff, F_24,F_28,0,0,0,0 },
	[INSTR_RRE_FR]	  = { 0xff, F_24,R_28,0,0,0,0 },
	[INSTR_RRE_R0]	  = { 0xff, R_24,0,0,0,0,0 },
	[INSTR_RRE_RA]	  = { 0xff, R_24,A_28,0,0,0,0 },
	[INSTR_RRE_RF]	  = { 0xff, R_24,F_28,0,0,0,0 },
	[INSTR_RRE_RR]	  = { 0xff, R_24,R_28,0,0,0,0 },
	[INSTR_RRE_RR_OPT]= { 0xff, R_24,RO_28,0,0,0,0 },
	[INSTR_RRF_0UFF]  = { 0xff, F_24,F_28,U4_20,0,0,0 },
	[INSTR_RRF_F0FF2] = { 0xff, F_24,F_16,F_28,0,0,0 },
	[INSTR_RRF_F0FF]  = { 0xff, F_16,F_24,F_28,0,0,0 },
	[INSTR_RRF_F0FR]  = { 0xff, F_24,F_16,R_28,0,0,0 },
	[INSTR_RRF_FFRU]  = { 0xff, F_24,F_16,R_28,U4_20,0,0 },
	[INSTR_RRF_FUFF]  = { 0xff, F_24,F_16,F_28,U4_20,0,0 },
	[INSTR_RRF_FUFF2] = { 0xff, F_24,F_28,F_16,U4_20,0,0 },
	[INSTR_RRF_M0RR]  = { 0xff, R_24,R_28,M_16,0,0,0 },
	[INSTR_RRF_R0RR]  = { 0xff, R_24,R_16,R_28,0,0,0 },
	[INSTR_RRF_R0RR2] = { 0xff, R_24,R_28,R_16,0,0,0 },
	[INSTR_RRF_RMRR]  = { 0xff, R_24,R_16,R_28,M_20,0,0 },
	[INSTR_RRF_RURR]  = { 0xff, R_24,R_28,R_16,U4_20,0,0 },
	[INSTR_RRF_U0FF]  = { 0xff, F_24,U4_16,F_28,0,0,0 },
	[INSTR_RRF_U0RF]  = { 0xff, R_24,U4_16,F_28,0,0,0 },
	[INSTR_RRF_U0RR]  = { 0xff, R_24,R_28,U4_16,0,0,0 },
	[INSTR_RRF_UUFF]  = { 0xff, F_24,U4_16,F_28,U4_20,0,0 },
	[INSTR_RRF_UUFR]  = { 0xff, F_24,U4_16,R_28,U4_20,0,0 },
	[INSTR_RRF_UURF]  = { 0xff, R_24,U4_16,F_28,U4_20,0,0 },
	[INSTR_RRR_F0FF]  = { 0xff, F_24,F_28,F_16,0,0,0 },
	[INSTR_RRS_RRRDU] = { 0xff, R_8,R_12,U4_32,D_20,B_16,0 },
	[INSTR_RR_FF]	  = { 0xff, F_8,F_12,0,0,0,0 },
	[INSTR_RR_R0]	  = { 0xff, R_8, 0,0,0,0,0 },
	[INSTR_RR_RR]	  = { 0xff, R_8,R_12,0,0,0,0 },
	[INSTR_RR_U0]	  = { 0xff, U8_8, 0,0,0,0,0 },
	[INSTR_RR_UR]	  = { 0xff, U4_8,R_12,0,0,0,0 },
	[INSTR_RSE_CCRD]  = { 0xff, C_8,C_12,D_20,B_16,0,0 },
	[INSTR_RSE_RRRD]  = { 0xff, R_8,R_12,D_20,B_16,0,0 },
	[INSTR_RSE_RURD]  = { 0xff, R_8,U4_12,D_20,B_16,0,0 },
	[INSTR_RSI_RRP]	  = { 0xff, R_8,R_12,J16_16,0,0,0 },
	[INSTR_RSL_LRDFU] = { 0xff, F_32,D_20,L4_8,B_16,U4_36,0 },
	[INSTR_RSL_R0RD]  = { 0xff, D_20,L4_8,B_16,0,0,0 },
	[INSTR_RSY_AARD]  = { 0xff, A_8,A_12,D20_20,B_16,0,0 },
	[INSTR_RSY_CCRD]  = { 0xff, C_8,C_12,D20_20,B_16,0,0 },
	[INSTR_RSY_RDRM]  = { 0xff, R_8,D20_20,B_16,U4_12,0,0 },
	[INSTR_RSY_RMRD]  = { 0xff, R_8,U4_12,D20_20,B_16,0,0 },
	[INSTR_RSY_RRRD]  = { 0xff, R_8,R_12,D20_20,B_16,0,0 },
	[INSTR_RSY_RURD]  = { 0xff, R_8,U4_12,D20_20,B_16,0,0 },
	[INSTR_RS_AARD]	  = { 0xff, A_8,A_12,D_20,B_16,0,0 },
	[INSTR_RS_CCRD]	  = { 0xff, C_8,C_12,D_20,B_16,0,0 },
	[INSTR_RS_R0RD]	  = { 0xff, R_8,D_20,B_16,0,0,0 },
	[INSTR_RS_RRRD]	  = { 0xff, R_8,R_12,D_20,B_16,0,0 },
	[INSTR_RS_RURD]	  = { 0xff, R_8,U4_12,D_20,B_16,0,0 },
	[INSTR_RXE_FRRD]  = { 0xff, F_8,D_20,X_12,B_16,0,0 },
	[INSTR_RXE_RRRD]  = { 0xff, R_8,D_20,X_12,B_16,0,0 },
	[INSTR_RXE_RRRDM] = { 0xff, R_8,D_20,X_12,B_16,M_32,0 },
	[INSTR_RXF_FRRDF] = { 0xff, F_32,F_8,D_20,X_12,B_16,0 },
	[INSTR_RXY_FRRD]  = { 0xff, F_8,D20_20,X_12,B_16,0,0 },
	[INSTR_RXY_RRRD]  = { 0xff, R_8,D20_20,X_12,B_16,0,0 },
	[INSTR_RXY_URRD]  = { 0xff, U4_8,D20_20,X_12,B_16,0,0 },
	[INSTR_RX_FRRD]	  = { 0xff, F_8,D_20,X_12,B_16,0,0 },
	[INSTR_RX_RRRD]	  = { 0xff, R_8,D_20,X_12,B_16,0,0 },
	[INSTR_RX_URRD]	  = { 0xff, U4_8,D_20,X_12,B_16,0,0 },
	[INSTR_SIL_RDI]   = { 0xff, D_20,B_16,I16_32,0,0,0 },
	[INSTR_SIL_RDU]   = { 0xff, D_20,B_16,U16_32,0,0,0 },
	[INSTR_SIY_IRD]   = { 0xff, D20_20,B_16,I8_8,0,0,0 },
	[INSTR_SIY_URD]	  = { 0xff, D20_20,B_16,U8_8,0,0,0 },
	[INSTR_SI_URD]	  = { 0xff, D_20,B_16,U8_8,0,0,0 },
	[INSTR_SMI_U0RDP] = { 0xff, U4_8,J16_32,D_20,B_16,0,0 },
	[INSTR_SSE_RDRD]  = { 0xff, D_20,B_16,D_36,B_32,0,0 },
	[INSTR_SSF_RRDRD] = { 0x0f, D_20,B_16,D_36,B_32,R_8,0 },
	[INSTR_SSF_RRDRD2]= { 0x0f, R_8,D_20,B_16,D_36,B_32,0 },
	[INSTR_SS_L0RDRD] = { 0xff, D_20,L8_8,B_16,D_36,B_32,0 },
	[INSTR_SS_LIRDRD] = { 0xff, D_20,L4_8,B_16,D_36,B_32,U4_12 },
	[INSTR_SS_LLRDRD] = { 0xff, D_20,L4_8,B_16,D_36,L4_12,B_32 },
	[INSTR_SS_RRRDRD2]= { 0xff, R_8,D_20,B_16,R_12,D_36,B_32 },
	[INSTR_SS_RRRDRD3]= { 0xff, R_8,R_12,D_20,B_16,D_36,B_32 },
	[INSTR_SS_RRRDRD] = { 0xff, D_20,R_8,B_16,D_36,B_32,R_12 },
	[INSTR_S_00]	  = { 0xff, 0,0,0,0,0,0 },
	[INSTR_S_RD]	  = { 0xff, D_20,B_16,0,0,0,0 },
	[INSTR_VRI_V0IM]  = { 0xff, V_8,I16_16,M_32,0,0,0 },
	[INSTR_VRI_V0I0]  = { 0xff, V_8,I16_16,0,0,0,0 },
	[INSTR_VRI_V0IIM] = { 0xff, V_8,I8_16,I8_24,M_32,0,0 },
	[INSTR_VRI_VVIM]  = { 0xff, V_8,I16_16,V_12,M_32,0,0 },
	[INSTR_VRI_VVV0IM]= { 0xff, V_8,V_12,V_16,I8_24,M_32,0 },
	[INSTR_VRI_VVV0I0]= { 0xff, V_8,V_12,V_16,I8_24,0,0 },
	[INSTR_VRI_VVIMM] = { 0xff, V_8,V_12,I16_16,M_32,M_28,0 },
	[INSTR_VRR_VV00MMM]={ 0xff, V_8,V_12,M_32,M_28,M_24,0 },
	[INSTR_VRR_VV000MM]={ 0xff, V_8,V_12,M_32,M_28,0,0 },
	[INSTR_VRR_VV0000M]={ 0xff, V_8,V_12,M_32,0,0,0 },
	[INSTR_VRR_VV00000]={ 0xff, V_8,V_12,0,0,0,0 },
	[INSTR_VRR_VVV0M0M]={ 0xff, V_8,V_12,V_16,M_32,M_24,0 },
	[INSTR_VRR_VV00M0M]={ 0xff, V_8,V_12,M_32,M_24,0,0 },
	[INSTR_VRR_VVV000M]={ 0xff, V_8,V_12,V_16,M_32,0,0 },
	[INSTR_VRR_VVV000V]={ 0xff, V_8,V_12,V_16,V_32,0,0 },
	[INSTR_VRR_VVV0000]={ 0xff, V_8,V_12,V_16,0,0,0 },
	[INSTR_VRR_VVV0MMM]={ 0xff, V_8,V_12,V_16,M_32,M_28,M_24 },
	[INSTR_VRR_VVV00MM]={ 0xff, V_8,V_12,V_16,M_32,M_28,0 },
	[INSTR_VRR_VVVMM0V]={ 0xff, V_8,V_12,V_16,V_32,M_20,M_24 },
	[INSTR_VRR_VVVM0MV]={ 0xff, V_8,V_12,V_16,V_32,M_28,M_20 },
	[INSTR_VRR_VVVM00V]={ 0xff, V_8,V_12,V_16,V_32,M_20,0 },
	[INSTR_VRR_VRR0000]={ 0xff, V_8,R_12,R_16,0,0,0 },
	[INSTR_VRS_VVRDM] = { 0xff, V_8,V_12,D_20,B_16,M_32,0 },
	[INSTR_VRS_VVRD0] = { 0xff, V_8,V_12,D_20,B_16,0,0 },
	[INSTR_VRS_VRRDM] = { 0xff, V_8,R_12,D_20,B_16,M_32,0 },
	[INSTR_VRS_VRRD0] = { 0xff, V_8,R_12,D_20,B_16,0,0 },
	[INSTR_VRS_RVRDM] = { 0xff, R_8,V_12,D_20,B_16,M_32,0 },
	[INSTR_VRV_VVRDM] = { 0xff, V_8,V_12,D_20,B_16,M_32,0 },
	[INSTR_VRV_VWRDM] = { 0xff, V_8,D_20,W_12,B_16,M_32,0 },
	[INSTR_VRX_VRRDM] = { 0xff, V_8,D_20,X_12,B_16,M_32,0 },
	[INSTR_VRX_VRRD0] = { 0xff, V_8,D_20,X_12,B_16,0,0 },
};

enum {
	LONG_INSN_ALGHSIK,
	LONG_INSN_ALHHHR,
	LONG_INSN_ALHHLR,
	LONG_INSN_ALHSIK,
	LONG_INSN_ALSIHN,
	LONG_INSN_CDFBRA,
	LONG_INSN_CDGBRA,
	LONG_INSN_CDGTRA,
	LONG_INSN_CDLFBR,
	LONG_INSN_CDLFTR,
	LONG_INSN_CDLGBR,
	LONG_INSN_CDLGTR,
	LONG_INSN_CEFBRA,
	LONG_INSN_CEGBRA,
	LONG_INSN_CELFBR,
	LONG_INSN_CELGBR,
	LONG_INSN_CFDBRA,
	LONG_INSN_CFEBRA,
	LONG_INSN_CFXBRA,
	LONG_INSN_CGDBRA,
	LONG_INSN_CGDTRA,
	LONG_INSN_CGEBRA,
	LONG_INSN_CGXBRA,
	LONG_INSN_CGXTRA,
	LONG_INSN_CLFDBR,
	LONG_INSN_CLFDTR,
	LONG_INSN_CLFEBR,
	LONG_INSN_CLFHSI,
	LONG_INSN_CLFXBR,
	LONG_INSN_CLFXTR,
	LONG_INSN_CLGDBR,
	LONG_INSN_CLGDTR,
	LONG_INSN_CLGEBR,
	LONG_INSN_CLGFRL,
	LONG_INSN_CLGHRL,
	LONG_INSN_CLGHSI,
	LONG_INSN_CLGXBR,
	LONG_INSN_CLGXTR,
	LONG_INSN_CLHHSI,
	LONG_INSN_CXFBRA,
	LONG_INSN_CXGBRA,
	LONG_INSN_CXGTRA,
	LONG_INSN_CXLFBR,
	LONG_INSN_CXLFTR,
	LONG_INSN_CXLGBR,
	LONG_INSN_CXLGTR,
	LONG_INSN_FIDBRA,
	LONG_INSN_FIEBRA,
	LONG_INSN_FIXBRA,
	LONG_INSN_LDXBRA,
	LONG_INSN_LEDBRA,
	LONG_INSN_LEXBRA,
	LONG_INSN_LLGFAT,
	LONG_INSN_LLGFRL,
	LONG_INSN_LLGHRL,
	LONG_INSN_LLGTAT,
	LONG_INSN_POPCNT,
	LONG_INSN_RIEMIT,
	LONG_INSN_RINEXT,
	LONG_INSN_RISBGN,
	LONG_INSN_RISBHG,
	LONG_INSN_RISBLG,
	LONG_INSN_SLHHHR,
	LONG_INSN_SLHHLR,
	LONG_INSN_TABORT,
	LONG_INSN_TBEGIN,
	LONG_INSN_TBEGINC,
	LONG_INSN_PCISTG,
	LONG_INSN_MPCIFC,
	LONG_INSN_STPCIFC,
	LONG_INSN_PCISTB,
	LONG_INSN_VPOPCT,
	LONG_INSN_VERLLV,
	LONG_INSN_VESRAV,
	LONG_INSN_VESRLV,
	LONG_INSN_VSBCBI,
	LONG_INSN_STCCTM
};

static char *long_insn_name[] = {
	[LONG_INSN_ALGHSIK] = "alghsik",
	[LONG_INSN_ALHHHR] = "alhhhr",
	[LONG_INSN_ALHHLR] = "alhhlr",
	[LONG_INSN_ALHSIK] = "alhsik",
	[LONG_INSN_ALSIHN] = "alsihn",
	[LONG_INSN_CDFBRA] = "cdfbra",
	[LONG_INSN_CDGBRA] = "cdgbra",
	[LONG_INSN_CDGTRA] = "cdgtra",
	[LONG_INSN_CDLFBR] = "cdlfbr",
	[LONG_INSN_CDLFTR] = "cdlftr",
	[LONG_INSN_CDLGBR] = "cdlgbr",
	[LONG_INSN_CDLGTR] = "cdlgtr",
	[LONG_INSN_CEFBRA] = "cefbra",
	[LONG_INSN_CEGBRA] = "cegbra",
	[LONG_INSN_CELFBR] = "celfbr",
	[LONG_INSN_CELGBR] = "celgbr",
	[LONG_INSN_CFDBRA] = "cfdbra",
	[LONG_INSN_CFEBRA] = "cfebra",
	[LONG_INSN_CFXBRA] = "cfxbra",
	[LONG_INSN_CGDBRA] = "cgdbra",
	[LONG_INSN_CGDTRA] = "cgdtra",
	[LONG_INSN_CGEBRA] = "cgebra",
	[LONG_INSN_CGXBRA] = "cgxbra",
	[LONG_INSN_CGXTRA] = "cgxtra",
	[LONG_INSN_CLFDBR] = "clfdbr",
	[LONG_INSN_CLFDTR] = "clfdtr",
	[LONG_INSN_CLFEBR] = "clfebr",
	[LONG_INSN_CLFHSI] = "clfhsi",
	[LONG_INSN_CLFXBR] = "clfxbr",
	[LONG_INSN_CLFXTR] = "clfxtr",
	[LONG_INSN_CLGDBR] = "clgdbr",
	[LONG_INSN_CLGDTR] = "clgdtr",
	[LONG_INSN_CLGEBR] = "clgebr",
	[LONG_INSN_CLGFRL] = "clgfrl",
	[LONG_INSN_CLGHRL] = "clghrl",
	[LONG_INSN_CLGHSI] = "clghsi",
	[LONG_INSN_CLGXBR] = "clgxbr",
	[LONG_INSN_CLGXTR] = "clgxtr",
	[LONG_INSN_CLHHSI] = "clhhsi",
	[LONG_INSN_CXFBRA] = "cxfbra",
	[LONG_INSN_CXGBRA] = "cxgbra",
	[LONG_INSN_CXGTRA] = "cxgtra",
	[LONG_INSN_CXLFBR] = "cxlfbr",
	[LONG_INSN_CXLFTR] = "cxlftr",
	[LONG_INSN_CXLGBR] = "cxlgbr",
	[LONG_INSN_CXLGTR] = "cxlgtr",
	[LONG_INSN_FIDBRA] = "fidbra",
	[LONG_INSN_FIEBRA] = "fiebra",
	[LONG_INSN_FIXBRA] = "fixbra",
	[LONG_INSN_LDXBRA] = "ldxbra",
	[LONG_INSN_LEDBRA] = "ledbra",
	[LONG_INSN_LEXBRA] = "lexbra",
	[LONG_INSN_LLGFAT] = "llgfat",
	[LONG_INSN_LLGFRL] = "llgfrl",
	[LONG_INSN_LLGHRL] = "llghrl",
	[LONG_INSN_LLGTAT] = "llgtat",
	[LONG_INSN_POPCNT] = "popcnt",
	[LONG_INSN_RIEMIT] = "riemit",
	[LONG_INSN_RINEXT] = "rinext",
	[LONG_INSN_RISBGN] = "risbgn",
	[LONG_INSN_RISBHG] = "risbhg",
	[LONG_INSN_RISBLG] = "risblg",
	[LONG_INSN_SLHHHR] = "slhhhr",
	[LONG_INSN_SLHHLR] = "slhhlr",
	[LONG_INSN_TABORT] = "tabort",
	[LONG_INSN_TBEGIN] = "tbegin",
	[LONG_INSN_TBEGINC] = "tbeginc",
	[LONG_INSN_PCISTG] = "pcistg",
	[LONG_INSN_MPCIFC] = "mpcifc",
	[LONG_INSN_STPCIFC] = "stpcifc",
	[LONG_INSN_PCISTB] = "pcistb",
	[LONG_INSN_VPOPCT] = "vpopct",
	[LONG_INSN_VERLLV] = "verllv",
	[LONG_INSN_VESRAV] = "vesrav",
	[LONG_INSN_VESRLV] = "vesrlv",
	[LONG_INSN_VSBCBI] = "vsbcbi",
	[LONG_INSN_STCCTM] = "stcctm",
};

static struct s390_insn opcode[] = {
	{ "bprp", 0xc5, INSTR_MII_UPI },
	{ "bpp", 0xc7, INSTR_SMI_U0RDP },
	{ "trtr", 0xd0, INSTR_SS_L0RDRD },
	{ "lmd", 0xef, INSTR_SS_RRRDRD3 },
	{ "spm", 0x04, INSTR_RR_R0 },
	{ "balr", 0x05, INSTR_RR_RR },
	{ "bctr", 0x06, INSTR_RR_RR },
	{ "bcr", 0x07, INSTR_RR_UR },
	{ "svc", 0x0a, INSTR_RR_U0 },
	{ "bsm", 0x0b, INSTR_RR_RR },
	{ "bassm", 0x0c, INSTR_RR_RR },
	{ "basr", 0x0d, INSTR_RR_RR },
	{ "mvcl", 0x0e, INSTR_RR_RR },
	{ "clcl", 0x0f, INSTR_RR_RR },
	{ "lpr", 0x10, INSTR_RR_RR },
	{ "lnr", 0x11, INSTR_RR_RR },
	{ "ltr", 0x12, INSTR_RR_RR },
	{ "lcr", 0x13, INSTR_RR_RR },
	{ "nr", 0x14, INSTR_RR_RR },
	{ "clr", 0x15, INSTR_RR_RR },
	{ "or", 0x16, INSTR_RR_RR },
	{ "xr", 0x17, INSTR_RR_RR },
	{ "lr", 0x18, INSTR_RR_RR },
	{ "cr", 0x19, INSTR_RR_RR },
	{ "ar", 0x1a, INSTR_RR_RR },
	{ "sr", 0x1b, INSTR_RR_RR },
	{ "mr", 0x1c, INSTR_RR_RR },
	{ "dr", 0x1d, INSTR_RR_RR },
	{ "alr", 0x1e, INSTR_RR_RR },
	{ "slr", 0x1f, INSTR_RR_RR },
	{ "lpdr", 0x20, INSTR_RR_FF },
	{ "lndr", 0x21, INSTR_RR_FF },
	{ "ltdr", 0x22, INSTR_RR_FF },
	{ "lcdr", 0x23, INSTR_RR_FF },
	{ "hdr", 0x24, INSTR_RR_FF },
	{ "ldxr", 0x25, INSTR_RR_FF },
	{ "mxr", 0x26, INSTR_RR_FF },
	{ "mxdr", 0x27, INSTR_RR_FF },
	{ "ldr", 0x28, INSTR_RR_FF },
	{ "cdr", 0x29, INSTR_RR_FF },
	{ "adr", 0x2a, INSTR_RR_FF },
	{ "sdr", 0x2b, INSTR_RR_FF },
	{ "mdr", 0x2c, INSTR_RR_FF },
	{ "ddr", 0x2d, INSTR_RR_FF },
	{ "awr", 0x2e, INSTR_RR_FF },
	{ "swr", 0x2f, INSTR_RR_FF },
	{ "lper", 0x30, INSTR_RR_FF },
	{ "lner", 0x31, INSTR_RR_FF },
	{ "lter", 0x32, INSTR_RR_FF },
	{ "lcer", 0x33, INSTR_RR_FF },
	{ "her", 0x34, INSTR_RR_FF },
	{ "ledr", 0x35, INSTR_RR_FF },
	{ "axr", 0x36, INSTR_RR_FF },
	{ "sxr", 0x37, INSTR_RR_FF },
	{ "ler", 0x38, INSTR_RR_FF },
	{ "cer", 0x39, INSTR_RR_FF },
	{ "aer", 0x3a, INSTR_RR_FF },
	{ "ser", 0x3b, INSTR_RR_FF },
	{ "mder", 0x3c, INSTR_RR_FF },
	{ "der", 0x3d, INSTR_RR_FF },
	{ "aur", 0x3e, INSTR_RR_FF },
	{ "sur", 0x3f, INSTR_RR_FF },
	{ "sth", 0x40, INSTR_RX_RRRD },
	{ "la", 0x41, INSTR_RX_RRRD },
	{ "stc", 0x42, INSTR_RX_RRRD },
	{ "ic", 0x43, INSTR_RX_RRRD },
	{ "ex", 0x44, INSTR_RX_RRRD },
	{ "bal", 0x45, INSTR_RX_RRRD },
	{ "bct", 0x46, INSTR_RX_RRRD },
	{ "bc", 0x47, INSTR_RX_URRD },
	{ "lh", 0x48, INSTR_RX_RRRD },
	{ "ch", 0x49, INSTR_RX_RRRD },
	{ "ah", 0x4a, INSTR_RX_RRRD },
	{ "sh", 0x4b, INSTR_RX_RRRD },
	{ "mh", 0x4c, INSTR_RX_RRRD },
	{ "bas", 0x4d, INSTR_RX_RRRD },
	{ "cvd", 0x4e, INSTR_RX_RRRD },
	{ "cvb", 0x4f, INSTR_RX_RRRD },
	{ "st", 0x50, INSTR_RX_RRRD },
	{ "lae", 0x51, INSTR_RX_RRRD },
	{ "n", 0x54, INSTR_RX_RRRD },
	{ "cl", 0x55, INSTR_RX_RRRD },
	{ "o", 0x56, INSTR_RX_RRRD },
	{ "x", 0x57, INSTR_RX_RRRD },
	{ "l", 0x58, INSTR_RX_RRRD },
	{ "c", 0x59, INSTR_RX_RRRD },
	{ "a", 0x5a, INSTR_RX_RRRD },
	{ "s", 0x5b, INSTR_RX_RRRD },
	{ "m", 0x5c, INSTR_RX_RRRD },
	{ "d", 0x5d, INSTR_RX_RRRD },
	{ "al", 0x5e, INSTR_RX_RRRD },
	{ "sl", 0x5f, INSTR_RX_RRRD },
	{ "std", 0x60, INSTR_RX_FRRD },
	{ "mxd", 0x67, INSTR_RX_FRRD },
	{ "ld", 0x68, INSTR_RX_FRRD },
	{ "cd", 0x69, INSTR_RX_FRRD },
	{ "ad", 0x6a, INSTR_RX_FRRD },
	{ "sd", 0x6b, INSTR_RX_FRRD },
	{ "md", 0x6c, INSTR_RX_FRRD },
	{ "dd", 0x6d, INSTR_RX_FRRD },
	{ "aw", 0x6e, INSTR_RX_FRRD },
	{ "sw", 0x6f, INSTR_RX_FRRD },
	{ "ste", 0x70, INSTR_RX_FRRD },
	{ "ms", 0x71, INSTR_RX_RRRD },
	{ "le", 0x78, INSTR_RX_FRRD },
	{ "ce", 0x79, INSTR_RX_FRRD },
	{ "ae", 0x7a, INSTR_RX_FRRD },
	{ "se", 0x7b, INSTR_RX_FRRD },
	{ "mde", 0x7c, INSTR_RX_FRRD },
	{ "de", 0x7d, INSTR_RX_FRRD },
	{ "au", 0x7e, INSTR_RX_FRRD },
	{ "su", 0x7f, INSTR_RX_FRRD },
	{ "ssm", 0x80, INSTR_S_RD },
	{ "lpsw", 0x82, INSTR_S_RD },
	{ "diag", 0x83, INSTR_RS_RRRD },
	{ "brxh", 0x84, INSTR_RSI_RRP },
	{ "brxle", 0x85, INSTR_RSI_RRP },
	{ "bxh", 0x86, INSTR_RS_RRRD },
	{ "bxle", 0x87, INSTR_RS_RRRD },
	{ "srl", 0x88, INSTR_RS_R0RD },
	{ "sll", 0x89, INSTR_RS_R0RD },
	{ "sra", 0x8a, INSTR_RS_R0RD },
	{ "sla", 0x8b, INSTR_RS_R0RD },
	{ "srdl", 0x8c, INSTR_RS_R0RD },
	{ "sldl", 0x8d, INSTR_RS_R0RD },
	{ "srda", 0x8e, INSTR_RS_R0RD },
	{ "slda", 0x8f, INSTR_RS_R0RD },
	{ "stm", 0x90, INSTR_RS_RRRD },
	{ "tm", 0x91, INSTR_SI_URD },
	{ "mvi", 0x92, INSTR_SI_URD },
	{ "ts", 0x93, INSTR_S_RD },
	{ "ni", 0x94, INSTR_SI_URD },
	{ "cli", 0x95, INSTR_SI_URD },
	{ "oi", 0x96, INSTR_SI_URD },
	{ "xi", 0x97, INSTR_SI_URD },
	{ "lm", 0x98, INSTR_RS_RRRD },
	{ "trace", 0x99, INSTR_RS_RRRD },
	{ "lam", 0x9a, INSTR_RS_AARD },
	{ "stam", 0x9b, INSTR_RS_AARD },
	{ "mvcle", 0xa8, INSTR_RS_RRRD },
	{ "clcle", 0xa9, INSTR_RS_RRRD },
	{ "stnsm", 0xac, INSTR_SI_URD },
	{ "stosm", 0xad, INSTR_SI_URD },
	{ "sigp", 0xae, INSTR_RS_RRRD },
	{ "mc", 0xaf, INSTR_SI_URD },
	{ "lra", 0xb1, INSTR_RX_RRRD },
	{ "stctl", 0xb6, INSTR_RS_CCRD },
	{ "lctl", 0xb7, INSTR_RS_CCRD },
	{ "cs", 0xba, INSTR_RS_RRRD },
	{ "cds", 0xbb, INSTR_RS_RRRD },
	{ "clm", 0xbd, INSTR_RS_RURD },
	{ "stcm", 0xbe, INSTR_RS_RURD },
	{ "icm", 0xbf, INSTR_RS_RURD },
	{ "mvn", 0xd1, INSTR_SS_L0RDRD },
	{ "mvc", 0xd2, INSTR_SS_L0RDRD },
	{ "mvz", 0xd3, INSTR_SS_L0RDRD },
	{ "nc", 0xd4, INSTR_SS_L0RDRD },
	{ "clc", 0xd5, INSTR_SS_L0RDRD },
	{ "oc", 0xd6, INSTR_SS_L0RDRD },
	{ "xc", 0xd7, INSTR_SS_L0RDRD },
	{ "mvck", 0xd9, INSTR_SS_RRRDRD },
	{ "mvcp", 0xda, INSTR_SS_RRRDRD },
	{ "mvcs", 0xdb, INSTR_SS_RRRDRD },
	{ "tr", 0xdc, INSTR_SS_L0RDRD },
	{ "trt", 0xdd, INSTR_SS_L0RDRD },
	{ "ed", 0xde, INSTR_SS_L0RDRD },
	{ "edmk", 0xdf, INSTR_SS_L0RDRD },
	{ "pku", 0xe1, INSTR_SS_L0RDRD },
	{ "unpku", 0xe2, INSTR_SS_L0RDRD },
	{ "mvcin", 0xe8, INSTR_SS_L0RDRD },
	{ "pka", 0xe9, INSTR_SS_L0RDRD },
	{ "unpka", 0xea, INSTR_SS_L0RDRD },
	{ "plo", 0xee, INSTR_SS_RRRDRD2 },
	{ "srp", 0xf0, INSTR_SS_LIRDRD },
	{ "mvo", 0xf1, INSTR_SS_LLRDRD },
	{ "pack", 0xf2, INSTR_SS_LLRDRD },
	{ "unpk", 0xf3, INSTR_SS_LLRDRD },
	{ "zap", 0xf8, INSTR_SS_LLRDRD },
	{ "cp", 0xf9, INSTR_SS_LLRDRD },
	{ "ap", 0xfa, INSTR_SS_LLRDRD },
	{ "sp", 0xfb, INSTR_SS_LLRDRD },
	{ "mp", 0xfc, INSTR_SS_LLRDRD },
	{ "dp", 0xfd, INSTR_SS_LLRDRD },
	{ "", 0, INSTR_INVALID }
};

static struct s390_insn opcode_01[] = {
	{ "ptff", 0x04, INSTR_E },
	{ "pfpo", 0x0a, INSTR_E },
	{ "sam64", 0x0e, INSTR_E },
	{ "pr", 0x01, INSTR_E },
	{ "upt", 0x02, INSTR_E },
	{ "sckpf", 0x07, INSTR_E },
	{ "tam", 0x0b, INSTR_E },
	{ "sam24", 0x0c, INSTR_E },
	{ "sam31", 0x0d, INSTR_E },
	{ "trap2", 0xff, INSTR_E },
	{ "", 0, INSTR_INVALID }
};

static struct s390_insn opcode_a5[] = {
	{ "iihh", 0x00, INSTR_RI_RU },
	{ "iihl", 0x01, INSTR_RI_RU },
	{ "iilh", 0x02, INSTR_RI_RU },
	{ "iill", 0x03, INSTR_RI_RU },
	{ "nihh", 0x04, INSTR_RI_RU },
	{ "nihl", 0x05, INSTR_RI_RU },
	{ "nilh", 0x06, INSTR_RI_RU },
	{ "nill", 0x07, INSTR_RI_RU },
	{ "oihh", 0x08, INSTR_RI_RU },
	{ "oihl", 0x09, INSTR_RI_RU },
	{ "oilh", 0x0a, INSTR_RI_RU },
	{ "oill", 0x0b, INSTR_RI_RU },
	{ "llihh", 0x0c, INSTR_RI_RU },
	{ "llihl", 0x0d, INSTR_RI_RU },
	{ "llilh", 0x0e, INSTR_RI_RU },
	{ "llill", 0x0f, INSTR_RI_RU },
	{ "", 0, INSTR_INVALID }
};

static struct s390_insn opcode_a7[] = {
	{ "tmhh", 0x02, INSTR_RI_RU },
	{ "tmhl", 0x03, INSTR_RI_RU },
	{ "brctg", 0x07, INSTR_RI_RP },
	{ "lghi", 0x09, INSTR_RI_RI },
	{ "aghi", 0x0b, INSTR_RI_RI },
	{ "mghi", 0x0d, INSTR_RI_RI },
	{ "cghi", 0x0f, INSTR_RI_RI },
	{ "tmlh", 0x00, INSTR_RI_RU },
	{ "tmll", 0x01, INSTR_RI_RU },
	{ "brc", 0x04, INSTR_RI_UP },
	{ "bras", 0x05, INSTR_RI_RP },
	{ "brct", 0x06, INSTR_RI_RP },
	{ "lhi", 0x08, INSTR_RI_RI },
	{ "ahi", 0x0a, INSTR_RI_RI },
	{ "mhi", 0x0c, INSTR_RI_RI },
	{ "chi", 0x0e, INSTR_RI_RI },
	{ "", 0, INSTR_INVALID }
};

static struct s390_insn opcode_aa[] = {
	{ { 0, LONG_INSN_RINEXT }, 0x00, INSTR_RI_RI },
	{ "rion", 0x01, INSTR_RI_RI },
	{ "tric", 0x02, INSTR_RI_RI },
	{ "rioff", 0x03, INSTR_RI_RI },
	{ { 0, LONG_INSN_RIEMIT }, 0x04, INSTR_RI_RI },
	{ "", 0, INSTR_INVALID }
};

static struct s390_insn opcode_b2[] = {
	{ "stckf", 0x7c, INSTR_S_RD },
	{ "lpp", 0x80, INSTR_S_RD },
	{ "lcctl", 0x84, INSTR_S_RD },
	{ "lpctl", 0x85, INSTR_S_RD },
	{ "qsi", 0x86, INSTR_S_RD },
	{ "lsctl", 0x87, INSTR_S_RD },
	{ "qctri", 0x8e, INSTR_S_RD },
	{ "stfle", 0xb0, INSTR_S_RD },
	{ "lpswe", 0xb2, INSTR_S_RD },
	{ "srnmb", 0xb8, INSTR_S_RD },
	{ "srnmt", 0xb9, INSTR_S_RD },
	{ "lfas", 0xbd, INSTR_S_RD },
	{ "scctr", 0xe0, INSTR_RRE_RR },
	{ "spctr", 0xe1, INSTR_RRE_RR },
	{ "ecctr", 0xe4, INSTR_RRE_RR },
	{ "epctr", 0xe5, INSTR_RRE_RR },
	{ "ppa", 0xe8, INSTR_RRF_U0RR },
	{ "etnd", 0xec, INSTR_RRE_R0 },
	{ "ecpga", 0xed, INSTR_RRE_RR },
	{ "tend", 0xf8, INSTR_S_00 },
	{ "niai", 0xfa, INSTR_IE_UU },
	{ { 0, LONG_INSN_TABORT }, 0xfc, INSTR_S_RD },
	{ "stidp", 0x02, INSTR_S_RD },
	{ "sck", 0x04, INSTR_S_RD },
	{ "stck", 0x05, INSTR_S_RD },
	{ "sckc", 0x06, INSTR_S_RD },
	{ "stckc", 0x07, INSTR_S_RD },
	{ "spt", 0x08, INSTR_S_RD },
	{ "stpt", 0x09, INSTR_S_RD },
	{ "spka", 0x0a, INSTR_S_RD },
	{ "ipk", 0x0b, INSTR_S_00 },
	{ "ptlb", 0x0d, INSTR_S_00 },
	{ "spx", 0x10, INSTR_S_RD },
	{ "stpx", 0x11, INSTR_S_RD },
	{ "stap", 0x12, INSTR_S_RD },
	{ "sie", 0x14, INSTR_S_RD },
	{ "pc", 0x18, INSTR_S_RD },
	{ "sac", 0x19, INSTR_S_RD },
	{ "cfc", 0x1a, INSTR_S_RD },
	{ "servc", 0x20, INSTR_RRE_RR },
	{ "ipte", 0x21, INSTR_RRE_RR },
	{ "ipm", 0x22, INSTR_RRE_R0 },
	{ "ivsk", 0x23, INSTR_RRE_RR },
	{ "iac", 0x24, INSTR_RRE_R0 },
	{ "ssar", 0x25, INSTR_RRE_R0 },
	{ "epar", 0x26, INSTR_RRE_R0 },
	{ "esar", 0x27, INSTR_RRE_R0 },
	{ "pt", 0x28, INSTR_RRE_RR },
	{ "iske", 0x29, INSTR_RRE_RR },
	{ "rrbe", 0x2a, INSTR_RRE_RR },
	{ "sske", 0x2b, INSTR_RRF_M0RR },
	{ "tb", 0x2c, INSTR_RRE_0R },
	{ "dxr", 0x2d, INSTR_RRE_FF },
	{ "pgin", 0x2e, INSTR_RRE_RR },
	{ "pgout", 0x2f, INSTR_RRE_RR },
	{ "csch", 0x30, INSTR_S_00 },
	{ "hsch", 0x31, INSTR_S_00 },
	{ "msch", 0x32, INSTR_S_RD },
	{ "ssch", 0x33, INSTR_S_RD },
	{ "stsch", 0x34, INSTR_S_RD },
	{ "tsch", 0x35, INSTR_S_RD },
	{ "tpi", 0x36, INSTR_S_RD },
	{ "sal", 0x37, INSTR_S_00 },
	{ "rsch", 0x38, INSTR_S_00 },
	{ "stcrw", 0x39, INSTR_S_RD },
	{ "stcps", 0x3a, INSTR_S_RD },
	{ "rchp", 0x3b, INSTR_S_00 },
	{ "schm", 0x3c, INSTR_S_00 },
	{ "bakr", 0x40, INSTR_RRE_RR },
	{ "cksm", 0x41, INSTR_RRE_RR },
	{ "sqdr", 0x44, INSTR_RRE_FF },
	{ "sqer", 0x45, INSTR_RRE_FF },
	{ "stura", 0x46, INSTR_RRE_RR },
	{ "msta", 0x47, INSTR_RRE_R0 },
	{ "palb", 0x48, INSTR_RRE_00 },
	{ "ereg", 0x49, INSTR_RRE_RR },
	{ "esta", 0x4a, INSTR_RRE_RR },
	{ "lura", 0x4b, INSTR_RRE_RR },
	{ "tar", 0x4c, INSTR_RRE_AR },
	{ "cpya", 0x4d, INSTR_RRE_AA },
	{ "sar", 0x4e, INSTR_RRE_AR },
	{ "ear", 0x4f, INSTR_RRE_RA },
	{ "csp", 0x50, INSTR_RRE_RR },
	{ "msr", 0x52, INSTR_RRE_RR },
	{ "mvpg", 0x54, INSTR_RRE_RR },
	{ "mvst", 0x55, INSTR_RRE_RR },
	{ "cuse", 0x57, INSTR_RRE_RR },
	{ "bsg", 0x58, INSTR_RRE_RR },
	{ "bsa", 0x5a, INSTR_RRE_RR },
	{ "clst", 0x5d, INSTR_RRE_RR },
	{ "srst", 0x5e, INSTR_RRE_RR },
	{ "cmpsc", 0x63, INSTR_RRE_RR },
	{ "siga", 0x74, INSTR_S_RD },
	{ "xsch", 0x76, INSTR_S_00 },
	{ "rp", 0x77, INSTR_S_RD },
	{ "stcke", 0x78, INSTR_S_RD },
	{ "sacf", 0x79, INSTR_S_RD },
	{ "stsi", 0x7d, INSTR_S_RD },
	{ "srnm", 0x99, INSTR_S_RD },
	{ "stfpc", 0x9c, INSTR_S_RD },
	{ "lfpc", 0x9d, INSTR_S_RD },
	{ "tre", 0xa5, INSTR_RRE_RR },
	{ "cuutf", 0xa6, INSTR_RRF_M0RR },
	{ "cutfu", 0xa7, INSTR_RRF_M0RR },
	{ "stfl", 0xb1, INSTR_S_RD },
	{ "trap4", 0xff, INSTR_S_RD },
	{ "", 0, INSTR_INVALID }
};

static struct s390_insn opcode_b3[] = {
	{ "maylr", 0x38, INSTR_RRF_F0FF },
	{ "mylr", 0x39, INSTR_RRF_F0FF },
	{ "mayr", 0x3a, INSTR_RRF_F0FF },
	{ "myr", 0x3b, INSTR_RRF_F0FF },
	{ "mayhr", 0x3c, INSTR_RRF_F0FF },
	{ "myhr", 0x3d, INSTR_RRF_F0FF },
	{ "lpdfr", 0x70, INSTR_RRE_FF },
	{ "lndfr", 0x71, INSTR_RRE_FF },
	{ "cpsdr", 0x72, INSTR_RRF_F0FF2 },
	{ "lcdfr", 0x73, INSTR_RRE_FF },
	{ "sfasr", 0x85, INSTR_RRE_R0 },
	{ { 0, LONG_INSN_CELFBR }, 0x90, INSTR_RRF_UUFR },
	{ { 0, LONG_INSN_CDLFBR }, 0x91, INSTR_RRF_UUFR },
	{ { 0, LONG_INSN_CXLFBR }, 0x92, INSTR_RRF_UURF },
	{ { 0, LONG_INSN_CEFBRA }, 0x94, INSTR_RRF_UUFR },
	{ { 0, LONG_INSN_CDFBRA }, 0x95, INSTR_RRF_UUFR },
	{ { 0, LONG_INSN_CXFBRA }, 0x96, INSTR_RRF_UURF },
	{ { 0, LONG_INSN_CFEBRA }, 0x98, INSTR_RRF_UURF },
	{ { 0, LONG_INSN_CFDBRA }, 0x99, INSTR_RRF_UURF },
	{ { 0, LONG_INSN_CFXBRA }, 0x9a, INSTR_RRF_UUFR },
	{ { 0, LONG_INSN_CLFEBR }, 0x9c, INSTR_RRF_UURF },
	{ { 0, LONG_INSN_CLFDBR }, 0x9d, INSTR_RRF_UURF },
	{ { 0, LONG_INSN_CLFXBR }, 0x9e, INSTR_RRF_UUFR },
	{ { 0, LONG_INSN_CELGBR }, 0xa0, INSTR_RRF_UUFR },
	{ { 0, LONG_INSN_CDLGBR }, 0xa1, INSTR_RRF_UUFR },
	{ { 0, LONG_INSN_CXLGBR }, 0xa2, INSTR_RRF_UURF },
	{ { 0, LONG_INSN_CEGBRA }, 0xa4, INSTR_RRF_UUFR },
	{ { 0, LONG_INSN_CDGBRA }, 0xa5, INSTR_RRF_UUFR },
	{ { 0, LONG_INSN_CXGBRA }, 0xa6, INSTR_RRF_UURF },
	{ { 0, LONG_INSN_CGEBRA }, 0xa8, INSTR_RRF_UURF },
	{ { 0, LONG_INSN_CGDBRA }, 0xa9, INSTR_RRF_UURF },
	{ { 0, LONG_INSN_CGXBRA }, 0xaa, INSTR_RRF_UUFR },
	{ { 0, LONG_INSN_CLGEBR }, 0xac, INSTR_RRF_UURF },
	{ { 0, LONG_INSN_CLGDBR }, 0xad, INSTR_RRF_UURF },
	{ { 0, LONG_INSN_CLGXBR }, 0xae, INSTR_RRF_UUFR },
	{ "ldgr", 0xc1, INSTR_RRE_FR },
	{ "cegr", 0xc4, INSTR_RRE_FR },
	{ "cdgr", 0xc5, INSTR_RRE_FR },
	{ "cxgr", 0xc6, INSTR_RRE_FR },
	{ "cger", 0xc8, INSTR_RRF_U0RF },
	{ "cgdr", 0xc9, INSTR_RRF_U0RF },
	{ "cgxr", 0xca, INSTR_RRF_U0RF },
	{ "lgdr", 0xcd, INSTR_RRE_RF },
	{ "mdtra", 0xd0, INSTR_RRF_FUFF2 },
	{ "ddtra", 0xd1, INSTR_RRF_FUFF2 },
	{ "adtra", 0xd2, INSTR_RRF_FUFF2 },
	{ "sdtra", 0xd3, INSTR_RRF_FUFF2 },
	{ "ldetr", 0xd4, INSTR_RRF_0UFF },
	{ "ledtr", 0xd5, INSTR_RRF_UUFF },
	{ "ltdtr", 0xd6, INSTR_RRE_FF },
	{ "fidtr", 0xd7, INSTR_RRF_UUFF },
	{ "mxtra", 0xd8, INSTR_RRF_FUFF2 },
	{ "dxtra", 0xd9, INSTR_RRF_FUFF2 },
	{ "axtra", 0xda, INSTR_RRF_FUFF2 },
	{ "sxtra", 0xdb, INSTR_RRF_FUFF2 },
	{ "lxdtr", 0xdc, INSTR_RRF_0UFF },
	{ "ldxtr", 0xdd, INSTR_RRF_UUFF },
	{ "ltxtr", 0xde, INSTR_RRE_FF },
	{ "fixtr", 0xdf, INSTR_RRF_UUFF },
	{ "kdtr", 0xe0, INSTR_RRE_FF },
	{ { 0, LONG_INSN_CGDTRA }, 0xe1, INSTR_RRF_UURF },
	{ "cudtr", 0xe2, INSTR_RRE_RF },
	{ "csdtr", 0xe3, INSTR_RRE_RF },
	{ "cdtr", 0xe4, INSTR_RRE_FF },
	{ "eedtr", 0xe5, INSTR_RRE_RF },
	{ "esdtr", 0xe7, INSTR_RRE_RF },
	{ "kxtr", 0xe8, INSTR_RRE_FF },
	{ { 0, LONG_INSN_CGXTRA }, 0xe9, INSTR_RRF_UUFR },
	{ "cuxtr", 0xea, INSTR_RRE_RF },
	{ "csxtr", 0xeb, INSTR_RRE_RF },
	{ "cxtr", 0xec, INSTR_RRE_FF },
	{ "eextr", 0xed, INSTR_RRE_RF },
	{ "esxtr", 0xef, INSTR_RRE_RF },
	{ { 0, LONG_INSN_CDGTRA }, 0xf1, INSTR_RRF_UUFR },
	{ "cdutr", 0xf2, INSTR_RRE_FR },
	{ "cdstr", 0xf3, INSTR_RRE_FR },
	{ "cedtr", 0xf4, INSTR_RRE_FF },
	{ "qadtr", 0xf5, INSTR_RRF_FUFF },
	{ "iedtr", 0xf6, INSTR_RRF_F0FR },
	{ "rrdtr", 0xf7, INSTR_RRF_FFRU },
	{ { 0, LONG_INSN_CXGTRA }, 0xf9, INSTR_RRF_UURF },
	{ "cxutr", 0xfa, INSTR_RRE_FR },
	{ "cxstr", 0xfb, INSTR_RRE_FR },
	{ "cextr", 0xfc, INSTR_RRE_FF },
	{ "qaxtr", 0xfd, INSTR_RRF_FUFF },
	{ "iextr", 0xfe, INSTR_RRF_F0FR },
	{ "rrxtr", 0xff, INSTR_RRF_FFRU },
	{ "lpebr", 0x00, INSTR_RRE_FF },
	{ "lnebr", 0x01, INSTR_RRE_FF },
	{ "ltebr", 0x02, INSTR_RRE_FF },
	{ "lcebr", 0x03, INSTR_RRE_FF },
	{ "ldebr", 0x04, INSTR_RRE_FF },
	{ "lxdbr", 0x05, INSTR_RRE_FF },
	{ "lxebr", 0x06, INSTR_RRE_FF },
	{ "mxdbr", 0x07, INSTR_RRE_FF },
	{ "kebr", 0x08, INSTR_RRE_FF },
	{ "cebr", 0x09, INSTR_RRE_FF },
	{ "aebr", 0x0a, INSTR_RRE_FF },
	{ "sebr", 0x0b, INSTR_RRE_FF },
	{ "mdebr", 0x0c, INSTR_RRE_FF },
	{ "debr", 0x0d, INSTR_RRE_FF },
	{ "maebr", 0x0e, INSTR_RRF_F0FF },
	{ "msebr", 0x0f, INSTR_RRF_F0FF },
	{ "lpdbr", 0x10, INSTR_RRE_FF },
	{ "lndbr", 0x11, INSTR_RRE_FF },
	{ "ltdbr", 0x12, INSTR_RRE_FF },
	{ "lcdbr", 0x13, INSTR_RRE_FF },
	{ "sqebr", 0x14, INSTR_RRE_FF },
	{ "sqdbr", 0x15, INSTR_RRE_FF },
	{ "sqxbr", 0x16, INSTR_RRE_FF },
	{ "meebr", 0x17, INSTR_RRE_FF },
	{ "kdbr", 0x18, INSTR_RRE_FF },
	{ "cdbr", 0x19, INSTR_RRE_FF },
	{ "adbr", 0x1a, INSTR_RRE_FF },
	{ "sdbr", 0x1b, INSTR_RRE_FF },
	{ "mdbr", 0x1c, INSTR_RRE_FF },
	{ "ddbr", 0x1d, INSTR_RRE_FF },
	{ "madbr", 0x1e, INSTR_RRF_F0FF },
	{ "msdbr", 0x1f, INSTR_RRF_F0FF },
	{ "lder", 0x24, INSTR_RRE_FF },
	{ "lxdr", 0x25, INSTR_RRE_FF },
	{ "lxer", 0x26, INSTR_RRE_FF },
	{ "maer", 0x2e, INSTR_RRF_F0FF },
	{ "mser", 0x2f, INSTR_RRF_F0FF },
	{ "sqxr", 0x36, INSTR_RRE_FF },
	{ "meer", 0x37, INSTR_RRE_FF },
	{ "madr", 0x3e, INSTR_RRF_F0FF },
	{ "msdr", 0x3f, INSTR_RRF_F0FF },
	{ "lpxbr", 0x40, INSTR_RRE_FF },
	{ "lnxbr", 0x41, INSTR_RRE_FF },
	{ "ltxbr", 0x42, INSTR_RRE_FF },
	{ "lcxbr", 0x43, INSTR_RRE_FF },
	{ { 0, LONG_INSN_LEDBRA }, 0x44, INSTR_RRF_UUFF },
	{ { 0, LONG_INSN_LDXBRA }, 0x45, INSTR_RRF_UUFF },
	{ { 0, LONG_INSN_LEXBRA }, 0x46, INSTR_RRF_UUFF },
	{ { 0, LONG_INSN_FIXBRA }, 0x47, INSTR_RRF_UUFF },
	{ "kxbr", 0x48, INSTR_RRE_FF },
	{ "cxbr", 0x49, INSTR_RRE_FF },
	{ "axbr", 0x4a, INSTR_RRE_FF },
	{ "sxbr", 0x4b, INSTR_RRE_FF },
	{ "mxbr", 0x4c, INSTR_RRE_FF },
	{ "dxbr", 0x4d, INSTR_RRE_FF },
	{ "tbedr", 0x50, INSTR_RRF_U0FF },
	{ "tbdr", 0x51, INSTR_RRF_U0FF },
	{ "diebr", 0x53, INSTR_RRF_FUFF },
	{ { 0, LONG_INSN_FIEBRA }, 0x57, INSTR_RRF_UUFF },
	{ "thder", 0x58, INSTR_RRE_FF },
	{ "thdr", 0x59, INSTR_RRE_FF },
	{ "didbr", 0x5b, INSTR_RRF_FUFF },
	{ { 0, LONG_INSN_FIDBRA }, 0x5f, INSTR_RRF_UUFF },
	{ "lpxr", 0x60, INSTR_RRE_FF },
	{ "lnxr", 0x61, INSTR_RRE_FF },
	{ "ltxr", 0x62, INSTR_RRE_FF },
	{ "lcxr", 0x63, INSTR_RRE_FF },
	{ "lxr", 0x65, INSTR_RRE_FF },
	{ "lexr", 0x66, INSTR_RRE_FF },
	{ "fixr", 0x67, INSTR_RRE_FF },
	{ "cxr", 0x69, INSTR_RRE_FF },
	{ "lzer", 0x74, INSTR_RRE_F0 },
	{ "lzdr", 0x75, INSTR_RRE_F0 },
	{ "lzxr", 0x76, INSTR_RRE_F0 },
	{ "fier", 0x77, INSTR_RRE_FF },
	{ "fidr", 0x7f, INSTR_RRE_FF },
	{ "sfpc", 0x84, INSTR_RRE_RR_OPT },
	{ "efpc", 0x8c, INSTR_RRE_RR_OPT },
	{ "cefbr", 0x94, INSTR_RRE_RF },
	{ "cdfbr", 0x95, INSTR_RRE_RF },
	{ "cxfbr", 0x96, INSTR_RRE_RF },
	{ "cfebr", 0x98, INSTR_RRF_U0RF },
	{ "cfdbr", 0x99, INSTR_RRF_U0RF },
	{ "cfxbr", 0x9a, INSTR_RRF_U0RF },
	{ "cefr", 0xb4, INSTR_RRE_FR },
	{ "cdfr", 0xb5, INSTR_RRE_FR },
	{ "cxfr", 0xb6, INSTR_RRE_FR },
	{ "cfer", 0xb8, INSTR_RRF_U0RF },
	{ "cfdr", 0xb9, INSTR_RRF_U0RF },
	{ "cfxr", 0xba, INSTR_RRF_U0RF },
	{ "", 0, INSTR_INVALID }
};

static struct s390_insn opcode_b9[] = {
	{ "lpgr", 0x00, INSTR_RRE_RR },
	{ "lngr", 0x01, INSTR_RRE_RR },
	{ "ltgr", 0x02, INSTR_RRE_RR },
	{ "lcgr", 0x03, INSTR_RRE_RR },
	{ "lgr", 0x04, INSTR_RRE_RR },
	{ "lurag", 0x05, INSTR_RRE_RR },
	{ "lgbr", 0x06, INSTR_RRE_RR },
	{ "lghr", 0x07, INSTR_RRE_RR },
	{ "agr", 0x08, INSTR_RRE_RR },
	{ "sgr", 0x09, INSTR_RRE_RR },
	{ "algr", 0x0a, INSTR_RRE_RR },
	{ "slgr", 0x0b, INSTR_RRE_RR },
	{ "msgr", 0x0c, INSTR_RRE_RR },
	{ "dsgr", 0x0d, INSTR_RRE_RR },
	{ "eregg", 0x0e, INSTR_RRE_RR },
	{ "lrvgr", 0x0f, INSTR_RRE_RR },
	{ "lpgfr", 0x10, INSTR_RRE_RR },
	{ "lngfr", 0x11, INSTR_RRE_RR },
	{ "ltgfr", 0x12, INSTR_RRE_RR },
	{ "lcgfr", 0x13, INSTR_RRE_RR },
	{ "lgfr", 0x14, INSTR_RRE_RR },
	{ "llgfr", 0x16, INSTR_RRE_RR },
	{ "llgtr", 0x17, INSTR_RRE_RR },
	{ "agfr", 0x18, INSTR_RRE_RR },
	{ "sgfr", 0x19, INSTR_RRE_RR },
	{ "algfr", 0x1a, INSTR_RRE_RR },
	{ "slgfr", 0x1b, INSTR_RRE_RR },
	{ "msgfr", 0x1c, INSTR_RRE_RR },
	{ "dsgfr", 0x1d, INSTR_RRE_RR },
	{ "cgr", 0x20, INSTR_RRE_RR },
	{ "clgr", 0x21, INSTR_RRE_RR },
	{ "sturg", 0x25, INSTR_RRE_RR },
	{ "lbr", 0x26, INSTR_RRE_RR },
	{ "lhr", 0x27, INSTR_RRE_RR },
	{ "cgfr", 0x30, INSTR_RRE_RR },
	{ "clgfr", 0x31, INSTR_RRE_RR },
	{ "cfdtr", 0x41, INSTR_RRF_UURF },
	{ { 0, LONG_INSN_CLGDTR }, 0x42, INSTR_RRF_UURF },
	{ { 0, LONG_INSN_CLFDTR }, 0x43, INSTR_RRF_UURF },
	{ "bctgr", 0x46, INSTR_RRE_RR },
	{ "cfxtr", 0x49, INSTR_RRF_UURF },
	{ { 0, LONG_INSN_CLGXTR }, 0x4a, INSTR_RRF_UUFR },
	{ { 0, LONG_INSN_CLFXTR }, 0x4b, INSTR_RRF_UUFR },
	{ "cdftr", 0x51, INSTR_RRF_UUFR },
	{ { 0, LONG_INSN_CDLGTR }, 0x52, INSTR_RRF_UUFR },
	{ { 0, LONG_INSN_CDLFTR }, 0x53, INSTR_RRF_UUFR },
	{ "cxftr", 0x59, INSTR_RRF_UURF },
	{ { 0, LONG_INSN_CXLGTR }, 0x5a, INSTR_RRF_UURF },
	{ { 0, LONG_INSN_CXLFTR }, 0x5b, INSTR_RRF_UUFR },
	{ "cgrt", 0x60, INSTR_RRF_U0RR },
	{ "clgrt", 0x61, INSTR_RRF_U0RR },
	{ "crt", 0x72, INSTR_RRF_U0RR },
	{ "clrt", 0x73, INSTR_RRF_U0RR },
	{ "ngr", 0x80, INSTR_RRE_RR },
	{ "ogr", 0x81, INSTR_RRE_RR },
	{ "xgr", 0x82, INSTR_RRE_RR },
	{ "flogr", 0x83, INSTR_RRE_RR },
	{ "llgcr", 0x84, INSTR_RRE_RR },
	{ "llghr", 0x85, INSTR_RRE_RR },
	{ "mlgr", 0x86, INSTR_RRE_RR },
	{ "dlgr", 0x87, INSTR_RRE_RR },
	{ "alcgr", 0x88, INSTR_RRE_RR },
	{ "slbgr", 0x89, INSTR_RRE_RR },
	{ "cspg", 0x8a, INSTR_RRE_RR },
	{ "idte", 0x8e, INSTR_RRF_R0RR },
	{ "crdte", 0x8f, INSTR_RRF_RMRR },
	{ "llcr", 0x94, INSTR_RRE_RR },
	{ "llhr", 0x95, INSTR_RRE_RR },
	{ "esea", 0x9d, INSTR_RRE_R0 },
	{ "ptf", 0xa2, INSTR_RRE_R0 },
	{ "lptea", 0xaa, INSTR_RRF_RURR },
	{ "rrbm", 0xae, INSTR_RRE_RR },
	{ "pfmf", 0xaf, INSTR_RRE_RR },
	{ "cu14", 0xb0, INSTR_RRF_M0RR },
	{ "cu24", 0xb1, INSTR_RRF_M0RR },
	{ "cu41", 0xb2, INSTR_RRE_RR },
	{ "cu42", 0xb3, INSTR_RRE_RR },
	{ "trtre", 0xbd, INSTR_RRF_M0RR },
	{ "srstu", 0xbe, INSTR_RRE_RR },
	{ "trte", 0xbf, INSTR_RRF_M0RR },
	{ "ahhhr", 0xc8, INSTR_RRF_R0RR2 },
	{ "shhhr", 0xc9, INSTR_RRF_R0RR2 },
	{ { 0, LONG_INSN_ALHHHR }, 0xca, INSTR_RRF_R0RR2 },
	{ { 0, LONG_INSN_SLHHHR }, 0xcb, INSTR_RRF_R0RR2 },
	{ "chhr", 0xcd, INSTR_RRE_RR },
	{ "clhhr", 0xcf, INSTR_RRE_RR },
	{ { 0, LONG_INSN_PCISTG }, 0xd0, INSTR_RRE_RR },
	{ "pcilg", 0xd2, INSTR_RRE_RR },
	{ "rpcit", 0xd3, INSTR_RRE_RR },
	{ "ahhlr", 0xd8, INSTR_RRF_R0RR2 },
	{ "shhlr", 0xd9, INSTR_RRF_R0RR2 },
	{ { 0, LONG_INSN_ALHHLR }, 0xda, INSTR_RRF_R0RR2 },
	{ { 0, LONG_INSN_SLHHLR }, 0xdb, INSTR_RRF_R0RR2 },
	{ "chlr", 0xdd, INSTR_RRE_RR },
	{ "clhlr", 0xdf, INSTR_RRE_RR },
	{ { 0, LONG_INSN_POPCNT }, 0xe1, INSTR_RRE_RR },
	{ "locgr", 0xe2, INSTR_RRF_M0RR },
	{ "ngrk", 0xe4, INSTR_RRF_R0RR2 },
	{ "ogrk", 0xe6, INSTR_RRF_R0RR2 },
	{ "xgrk", 0xe7, INSTR_RRF_R0RR2 },
	{ "agrk", 0xe8, INSTR_RRF_R0RR2 },
	{ "sgrk", 0xe9, INSTR_RRF_R0RR2 },
	{ "algrk", 0xea, INSTR_RRF_R0RR2 },
	{ "slgrk", 0xeb, INSTR_RRF_R0RR2 },
	{ "locr", 0xf2, INSTR_RRF_M0RR },
	{ "nrk", 0xf4, INSTR_RRF_R0RR2 },
	{ "ork", 0xf6, INSTR_RRF_R0RR2 },
	{ "xrk", 0xf7, INSTR_RRF_R0RR2 },
	{ "ark", 0xf8, INSTR_RRF_R0RR2 },
	{ "srk", 0xf9, INSTR_RRF_R0RR2 },
	{ "alrk", 0xfa, INSTR_RRF_R0RR2 },
	{ "slrk", 0xfb, INSTR_RRF_R0RR2 },
	{ "kmac", 0x1e, INSTR_RRE_RR },
	{ "lrvr", 0x1f, INSTR_RRE_RR },
	{ "km", 0x2e, INSTR_RRE_RR },
	{ "kmc", 0x2f, INSTR_RRE_RR },
	{ "kimd", 0x3e, INSTR_RRE_RR },
	{ "klmd", 0x3f, INSTR_RRE_RR },
	{ "epsw", 0x8d, INSTR_RRE_RR },
	{ "trtt", 0x90, INSTR_RRF_M0RR },
	{ "trto", 0x91, INSTR_RRF_M0RR },
	{ "trot", 0x92, INSTR_RRF_M0RR },
	{ "troo", 0x93, INSTR_RRF_M0RR },
	{ "mlr", 0x96, INSTR_RRE_RR },
	{ "dlr", 0x97, INSTR_RRE_RR },
	{ "alcr", 0x98, INSTR_RRE_RR },
	{ "slbr", 0x99, INSTR_RRE_RR },
	{ "", 0, INSTR_INVALID }
};

static struct s390_insn opcode_c0[] = {
	{ "lgfi", 0x01, INSTR_RIL_RI },
	{ "xihf", 0x06, INSTR_RIL_RU },
	{ "xilf", 0x07, INSTR_RIL_RU },
	{ "iihf", 0x08, INSTR_RIL_RU },
	{ "iilf", 0x09, INSTR_RIL_RU },
	{ "nihf", 0x0a, INSTR_RIL_RU },
	{ "nilf", 0x0b, INSTR_RIL_RU },
	{ "oihf", 0x0c, INSTR_RIL_RU },
	{ "oilf", 0x0d, INSTR_RIL_RU },
	{ "llihf", 0x0e, INSTR_RIL_RU },
	{ "llilf", 0x0f, INSTR_RIL_RU },
	{ "larl", 0x00, INSTR_RIL_RP },
	{ "brcl", 0x04, INSTR_RIL_UP },
	{ "brasl", 0x05, INSTR_RIL_RP },
	{ "", 0, INSTR_INVALID }
};

static struct s390_insn opcode_c2[] = {
	{ "msgfi", 0x00, INSTR_RIL_RI },
	{ "msfi", 0x01, INSTR_RIL_RI },
	{ "slgfi", 0x04, INSTR_RIL_RU },
	{ "slfi", 0x05, INSTR_RIL_RU },
	{ "agfi", 0x08, INSTR_RIL_RI },
	{ "afi", 0x09, INSTR_RIL_RI },
	{ "algfi", 0x0a, INSTR_RIL_RU },
	{ "alfi", 0x0b, INSTR_RIL_RU },
	{ "cgfi", 0x0c, INSTR_RIL_RI },
	{ "cfi", 0x0d, INSTR_RIL_RI },
	{ "clgfi", 0x0e, INSTR_RIL_RU },
	{ "clfi", 0x0f, INSTR_RIL_RU },
	{ "", 0, INSTR_INVALID }
};

static struct s390_insn opcode_c4[] = {
	{ "llhrl", 0x02, INSTR_RIL_RP },
	{ "lghrl", 0x04, INSTR_RIL_RP },
	{ "lhrl", 0x05, INSTR_RIL_RP },
	{ { 0, LONG_INSN_LLGHRL }, 0x06, INSTR_RIL_RP },
	{ "sthrl", 0x07, INSTR_RIL_RP },
	{ "lgrl", 0x08, INSTR_RIL_RP },
	{ "stgrl", 0x0b, INSTR_RIL_RP },
	{ "lgfrl", 0x0c, INSTR_RIL_RP },
	{ "lrl", 0x0d, INSTR_RIL_RP },
	{ { 0, LONG_INSN_LLGFRL }, 0x0e, INSTR_RIL_RP },
	{ "strl", 0x0f, INSTR_RIL_RP },
	{ "", 0, INSTR_INVALID }
};

static struct s390_insn opcode_c6[] = {
	{ "exrl", 0x00, INSTR_RIL_RP },
	{ "pfdrl", 0x02, INSTR_RIL_UP },
	{ "cghrl", 0x04, INSTR_RIL_RP },
	{ "chrl", 0x05, INSTR_RIL_RP },
	{ { 0, LONG_INSN_CLGHRL }, 0x06, INSTR_RIL_RP },
	{ "clhrl", 0x07, INSTR_RIL_RP },
	{ "cgrl", 0x08, INSTR_RIL_RP },
	{ "clgrl", 0x0a, INSTR_RIL_RP },
	{ "cgfrl", 0x0c, INSTR_RIL_RP },
	{ "crl", 0x0d, INSTR_RIL_RP },
	{ { 0, LONG_INSN_CLGFRL }, 0x0e, INSTR_RIL_RP },
	{ "clrl", 0x0f, INSTR_RIL_RP },
	{ "", 0, INSTR_INVALID }
};

static struct s390_insn opcode_c8[] = {
	{ "mvcos", 0x00, INSTR_SSF_RRDRD },
	{ "ectg", 0x01, INSTR_SSF_RRDRD },
	{ "csst", 0x02, INSTR_SSF_RRDRD },
	{ "lpd", 0x04, INSTR_SSF_RRDRD2 },
	{ "lpdg", 0x05, INSTR_SSF_RRDRD2 },
	{ "", 0, INSTR_INVALID }
};

static struct s390_insn opcode_cc[] = {
	{ "brcth", 0x06, INSTR_RIL_RP },
	{ "aih", 0x08, INSTR_RIL_RI },
	{ "alsih", 0x0a, INSTR_RIL_RI },
	{ { 0, LONG_INSN_ALSIHN }, 0x0b, INSTR_RIL_RI },
	{ "cih", 0x0d, INSTR_RIL_RI },
	{ "clih", 0x0f, INSTR_RIL_RI },
	{ "", 0, INSTR_INVALID }
};

static struct s390_insn opcode_e3[] = {
	{ "ltg", 0x02, INSTR_RXY_RRRD },
	{ "lrag", 0x03, INSTR_RXY_RRRD },
	{ "lg", 0x04, INSTR_RXY_RRRD },
	{ "cvby", 0x06, INSTR_RXY_RRRD },
	{ "ag", 0x08, INSTR_RXY_RRRD },
	{ "sg", 0x09, INSTR_RXY_RRRD },
	{ "alg", 0x0a, INSTR_RXY_RRRD },
	{ "slg", 0x0b, INSTR_RXY_RRRD },
	{ "msg", 0x0c, INSTR_RXY_RRRD },
	{ "dsg", 0x0d, INSTR_RXY_RRRD },
	{ "cvbg", 0x0e, INSTR_RXY_RRRD },
	{ "lrvg", 0x0f, INSTR_RXY_RRRD },
	{ "lt", 0x12, INSTR_RXY_RRRD },
	{ "lray", 0x13, INSTR_RXY_RRRD },
	{ "lgf", 0x14, INSTR_RXY_RRRD },
	{ "lgh", 0x15, INSTR_RXY_RRRD },
	{ "llgf", 0x16, INSTR_RXY_RRRD },
	{ "llgt", 0x17, INSTR_RXY_RRRD },
	{ "agf", 0x18, INSTR_RXY_RRRD },
	{ "sgf", 0x19, INSTR_RXY_RRRD },
	{ "algf", 0x1a, INSTR_RXY_RRRD },
	{ "slgf", 0x1b, INSTR_RXY_RRRD },
	{ "msgf", 0x1c, INSTR_RXY_RRRD },
	{ "dsgf", 0x1d, INSTR_RXY_RRRD },
	{ "cg", 0x20, INSTR_RXY_RRRD },
	{ "clg", 0x21, INSTR_RXY_RRRD },
	{ "stg", 0x24, INSTR_RXY_RRRD },
	{ "ntstg", 0x25, INSTR_RXY_RRRD },
	{ "cvdy", 0x26, INSTR_RXY_RRRD },
	{ "cvdg", 0x2e, INSTR_RXY_RRRD },
	{ "strvg", 0x2f, INSTR_RXY_RRRD },
	{ "cgf", 0x30, INSTR_RXY_RRRD },
	{ "clgf", 0x31, INSTR_RXY_RRRD },
	{ "ltgf", 0x32, INSTR_RXY_RRRD },
	{ "cgh", 0x34, INSTR_RXY_RRRD },
	{ "pfd", 0x36, INSTR_RXY_URRD },
	{ "strvh", 0x3f, INSTR_RXY_RRRD },
	{ "bctg", 0x46, INSTR_RXY_RRRD },
	{ "sty", 0x50, INSTR_RXY_RRRD },
	{ "msy", 0x51, INSTR_RXY_RRRD },
	{ "ny", 0x54, INSTR_RXY_RRRD },
	{ "cly", 0x55, INSTR_RXY_RRRD },
	{ "oy", 0x56, INSTR_RXY_RRRD },
	{ "xy", 0x57, INSTR_RXY_RRRD },
	{ "ly", 0x58, INSTR_RXY_RRRD },
	{ "cy", 0x59, INSTR_RXY_RRRD },
	{ "ay", 0x5a, INSTR_RXY_RRRD },
	{ "sy", 0x5b, INSTR_RXY_RRRD },
	{ "mfy", 0x5c, INSTR_RXY_RRRD },
	{ "aly", 0x5e, INSTR_RXY_RRRD },
	{ "sly", 0x5f, INSTR_RXY_RRRD },
	{ "sthy", 0x70, INSTR_RXY_RRRD },
	{ "lay", 0x71, INSTR_RXY_RRRD },
	{ "stcy", 0x72, INSTR_RXY_RRRD },
	{ "icy", 0x73, INSTR_RXY_RRRD },
	{ "laey", 0x75, INSTR_RXY_RRRD },
	{ "lb", 0x76, INSTR_RXY_RRRD },
	{ "lgb", 0x77, INSTR_RXY_RRRD },
	{ "lhy", 0x78, INSTR_RXY_RRRD },
	{ "chy", 0x79, INSTR_RXY_RRRD },
	{ "ahy", 0x7a, INSTR_RXY_RRRD },
	{ "shy", 0x7b, INSTR_RXY_RRRD },
	{ "mhy", 0x7c, INSTR_RXY_RRRD },
	{ "ng", 0x80, INSTR_RXY_RRRD },
	{ "og", 0x81, INSTR_RXY_RRRD },
	{ "xg", 0x82, INSTR_RXY_RRRD },
	{ "lgat", 0x85, INSTR_RXY_RRRD },
	{ "mlg", 0x86, INSTR_RXY_RRRD },
	{ "dlg", 0x87, INSTR_RXY_RRRD },
	{ "alcg", 0x88, INSTR_RXY_RRRD },
	{ "slbg", 0x89, INSTR_RXY_RRRD },
	{ "stpq", 0x8e, INSTR_RXY_RRRD },
	{ "lpq", 0x8f, INSTR_RXY_RRRD },
	{ "llgc", 0x90, INSTR_RXY_RRRD },
	{ "llgh", 0x91, INSTR_RXY_RRRD },
	{ "llc", 0x94, INSTR_RXY_RRRD },
	{ "llh", 0x95, INSTR_RXY_RRRD },
	{ { 0, LONG_INSN_LLGTAT }, 0x9c, INSTR_RXY_RRRD },
	{ { 0, LONG_INSN_LLGFAT }, 0x9d, INSTR_RXY_RRRD },
	{ "lat", 0x9f, INSTR_RXY_RRRD },
	{ "lbh", 0xc0, INSTR_RXY_RRRD },
	{ "llch", 0xc2, INSTR_RXY_RRRD },
	{ "stch", 0xc3, INSTR_RXY_RRRD },
	{ "lhh", 0xc4, INSTR_RXY_RRRD },
	{ "llhh", 0xc6, INSTR_RXY_RRRD },
	{ "sthh", 0xc7, INSTR_RXY_RRRD },
	{ "lfhat", 0xc8, INSTR_RXY_RRRD },
	{ "lfh", 0xca, INSTR_RXY_RRRD },
	{ "stfh", 0xcb, INSTR_RXY_RRRD },
	{ "chf", 0xcd, INSTR_RXY_RRRD },
	{ "clhf", 0xcf, INSTR_RXY_RRRD },
	{ { 0, LONG_INSN_MPCIFC }, 0xd0, INSTR_RXY_RRRD },
	{ { 0, LONG_INSN_STPCIFC }, 0xd4, INSTR_RXY_RRRD },
	{ "lrv", 0x1e, INSTR_RXY_RRRD },
	{ "lrvh", 0x1f, INSTR_RXY_RRRD },
	{ "strv", 0x3e, INSTR_RXY_RRRD },
	{ "ml", 0x96, INSTR_RXY_RRRD },
	{ "dl", 0x97, INSTR_RXY_RRRD },
	{ "alc", 0x98, INSTR_RXY_RRRD },
	{ "slb", 0x99, INSTR_RXY_RRRD },
	{ "", 0, INSTR_INVALID }
};

static struct s390_insn opcode_e5[] = {
	{ "strag", 0x02, INSTR_SSE_RDRD },
	{ "mvhhi", 0x44, INSTR_SIL_RDI },
	{ "mvghi", 0x48, INSTR_SIL_RDI },
	{ "mvhi", 0x4c, INSTR_SIL_RDI },
	{ "chhsi", 0x54, INSTR_SIL_RDI },
	{ { 0, LONG_INSN_CLHHSI }, 0x55, INSTR_SIL_RDU },
	{ "cghsi", 0x58, INSTR_SIL_RDI },
	{ { 0, LONG_INSN_CLGHSI }, 0x59, INSTR_SIL_RDU },
	{ "chsi", 0x5c, INSTR_SIL_RDI },
	{ { 0, LONG_INSN_CLFHSI }, 0x5d, INSTR_SIL_RDU },
	{ { 0, LONG_INSN_TBEGIN }, 0x60, INSTR_SIL_RDU },
	{ { 0, LONG_INSN_TBEGINC }, 0x61, INSTR_SIL_RDU },
	{ "lasp", 0x00, INSTR_SSE_RDRD },
	{ "tprot", 0x01, INSTR_SSE_RDRD },
	{ "mvcsk", 0x0e, INSTR_SSE_RDRD },
	{ "mvcdk", 0x0f, INSTR_SSE_RDRD },
	{ "", 0, INSTR_INVALID }
};

static struct s390_insn opcode_e7[] = {
	{ "lcbb", 0x27, INSTR_RXE_RRRDM },
	{ "vgef", 0x13, INSTR_VRV_VVRDM },
	{ "vgeg", 0x12, INSTR_VRV_VVRDM },
	{ "vgbm", 0x44, INSTR_VRI_V0I0 },
	{ "vgm", 0x46, INSTR_VRI_V0IIM },
	{ "vl", 0x06, INSTR_VRX_VRRD0 },
	{ "vlr", 0x56, INSTR_VRR_VV00000 },
	{ "vlrp", 0x05, INSTR_VRX_VRRDM },
	{ "vleb", 0x00, INSTR_VRX_VRRDM },
	{ "vleh", 0x01, INSTR_VRX_VRRDM },
	{ "vlef", 0x03, INSTR_VRX_VRRDM },
	{ "vleg", 0x02, INSTR_VRX_VRRDM },
	{ "vleib", 0x40, INSTR_VRI_V0IM },
	{ "vleih", 0x41, INSTR_VRI_V0IM },
	{ "vleif", 0x43, INSTR_VRI_V0IM },
	{ "vleig", 0x42, INSTR_VRI_V0IM },
	{ "vlgv", 0x21, INSTR_VRS_RVRDM },
	{ "vllez", 0x04, INSTR_VRX_VRRDM },
	{ "vlm", 0x36, INSTR_VRS_VVRD0 },
	{ "vlbb", 0x07, INSTR_VRX_VRRDM },
	{ "vlvg", 0x22, INSTR_VRS_VRRDM },
	{ "vlvgp", 0x62, INSTR_VRR_VRR0000 },
	{ "vll", 0x37, INSTR_VRS_VRRD0 },
	{ "vmrh", 0x61, INSTR_VRR_VVV000M },
	{ "vmrl", 0x60, INSTR_VRR_VVV000M },
	{ "vpk", 0x94, INSTR_VRR_VVV000M },
	{ "vpks", 0x97, INSTR_VRR_VVV0M0M },
	{ "vpkls", 0x95, INSTR_VRR_VVV0M0M },
	{ "vperm", 0x8c, INSTR_VRR_VVV000V },
	{ "vpdi", 0x84, INSTR_VRR_VVV000M },
	{ "vrep", 0x4d, INSTR_VRI_VVIM },
	{ "vrepi", 0x45, INSTR_VRI_V0IM },
	{ "vscef", 0x1b, INSTR_VRV_VWRDM },
	{ "vsceg", 0x1a, INSTR_VRV_VWRDM },
	{ "vsel", 0x8d, INSTR_VRR_VVV000V },
	{ "vseg", 0x5f, INSTR_VRR_VV0000M },
	{ "vst", 0x0e, INSTR_VRX_VRRD0 },
	{ "vsteb", 0x08, INSTR_VRX_VRRDM },
	{ "vsteh", 0x09, INSTR_VRX_VRRDM },
	{ "vstef", 0x0b, INSTR_VRX_VRRDM },
	{ "vsteg", 0x0a, INSTR_VRX_VRRDM },
	{ "vstm", 0x3e, INSTR_VRS_VVRD0 },
	{ "vstl", 0x3f, INSTR_VRS_VRRD0 },
	{ "vuph", 0xd7, INSTR_VRR_VV0000M },
	{ "vuplh", 0xd5, INSTR_VRR_VV0000M },
	{ "vupl", 0xd6, INSTR_VRR_VV0000M },
	{ "vupll", 0xd4, INSTR_VRR_VV0000M },
	{ "va", 0xf3, INSTR_VRR_VVV000M },
	{ "vacc", 0xf1, INSTR_VRR_VVV000M },
	{ "vac", 0xbb, INSTR_VRR_VVVM00V },
	{ "vaccc", 0xb9, INSTR_VRR_VVVM00V },
	{ "vn", 0x68, INSTR_VRR_VVV0000 },
	{ "vnc", 0x69, INSTR_VRR_VVV0000 },
	{ "vavg", 0xf2, INSTR_VRR_VVV000M },
	{ "vavgl", 0xf0, INSTR_VRR_VVV000M },
	{ "vcksm", 0x66, INSTR_VRR_VVV0000 },
	{ "vec", 0xdb, INSTR_VRR_VV0000M },
	{ "vecl", 0xd9, INSTR_VRR_VV0000M },
	{ "vceq", 0xf8, INSTR_VRR_VVV0M0M },
	{ "vch", 0xfb, INSTR_VRR_VVV0M0M },
	{ "vchl", 0xf9, INSTR_VRR_VVV0M0M },
	{ "vclz", 0x53, INSTR_VRR_VV0000M },
	{ "vctz", 0x52, INSTR_VRR_VV0000M },
	{ "vx", 0x6d, INSTR_VRR_VVV0000 },
	{ "vgfm", 0xb4, INSTR_VRR_VVV000M },
	{ "vgfma", 0xbc, INSTR_VRR_VVVM00V },
	{ "vlc", 0xde, INSTR_VRR_VV0000M },
	{ "vlp", 0xdf, INSTR_VRR_VV0000M },
	{ "vmx", 0xff, INSTR_VRR_VVV000M },
	{ "vmxl", 0xfd, INSTR_VRR_VVV000M },
	{ "vmn", 0xfe, INSTR_VRR_VVV000M },
	{ "vmnl", 0xfc, INSTR_VRR_VVV000M },
	{ "vmal", 0xaa, INSTR_VRR_VVVM00V },
	{ "vmae", 0xae, INSTR_VRR_VVVM00V },
	{ "vmale", 0xac, INSTR_VRR_VVVM00V },
	{ "vmah", 0xab, INSTR_VRR_VVVM00V },
	{ "vmalh", 0xa9, INSTR_VRR_VVVM00V },
	{ "vmao", 0xaf, INSTR_VRR_VVVM00V },
	{ "vmalo", 0xad, INSTR_VRR_VVVM00V },
	{ "vmh", 0xa3, INSTR_VRR_VVV000M },
	{ "vmlh", 0xa1, INSTR_VRR_VVV000M },
	{ "vml", 0xa2, INSTR_VRR_VVV000M },
	{ "vme", 0xa6, INSTR_VRR_VVV000M },
	{ "vmle", 0xa4, INSTR_VRR_VVV000M },
	{ "vmo", 0xa7, INSTR_VRR_VVV000M },
	{ "vmlo", 0xa5, INSTR_VRR_VVV000M },
	{ "vno", 0x6b, INSTR_VRR_VVV0000 },
	{ "vo", 0x6a, INSTR_VRR_VVV0000 },
	{ { 0, LONG_INSN_VPOPCT }, 0x50, INSTR_VRR_VV0000M },
	{ { 0, LONG_INSN_VERLLV }, 0x73, INSTR_VRR_VVV000M },
	{ "verll", 0x33, INSTR_VRS_VVRDM },
	{ "verim", 0x72, INSTR_VRI_VVV0IM },
	{ "veslv", 0x70, INSTR_VRR_VVV000M },
	{ "vesl", 0x30, INSTR_VRS_VVRDM },
	{ { 0, LONG_INSN_VESRAV }, 0x7a, INSTR_VRR_VVV000M },
	{ "vesra", 0x3a, INSTR_VRS_VVRDM },
	{ { 0, LONG_INSN_VESRLV }, 0x78, INSTR_VRR_VVV000M },
	{ "vesrl", 0x38, INSTR_VRS_VVRDM },
	{ "vsl", 0x74, INSTR_VRR_VVV0000 },
	{ "vslb", 0x75, INSTR_VRR_VVV0000 },
	{ "vsldb", 0x77, INSTR_VRI_VVV0I0 },
	{ "vsra", 0x7e, INSTR_VRR_VVV0000 },
	{ "vsrab", 0x7f, INSTR_VRR_VVV0000 },
	{ "vsrl", 0x7c, INSTR_VRR_VVV0000 },
	{ "vsrlb", 0x7d, INSTR_VRR_VVV0000 },
	{ "vs", 0xf7, INSTR_VRR_VVV000M },
	{ "vscb", 0xf5, INSTR_VRR_VVV000M },
	{ "vsb", 0xbf, INSTR_VRR_VVVM00V },
	{ { 0, LONG_INSN_VSBCBI }, 0xbd, INSTR_VRR_VVVM00V },
	{ "vsumg", 0x65, INSTR_VRR_VVV000M },
	{ "vsumq", 0x67, INSTR_VRR_VVV000M },
	{ "vsum", 0x64, INSTR_VRR_VVV000M },
	{ "vtm", 0xd8, INSTR_VRR_VV00000 },
	{ "vfae", 0x82, INSTR_VRR_VVV0M0M },
	{ "vfee", 0x80, INSTR_VRR_VVV0M0M },
	{ "vfene", 0x81, INSTR_VRR_VVV0M0M },
	{ "vistr", 0x5c, INSTR_VRR_VV00M0M },
	{ "vstrc", 0x8a, INSTR_VRR_VVVMM0V },
	{ "vfa", 0xe3, INSTR_VRR_VVV00MM },
	{ "wfc", 0xcb, INSTR_VRR_VV000MM },
	{ "wfk", 0xca, INSTR_VRR_VV000MM },
	{ "vfce", 0xe8, INSTR_VRR_VVV0MMM },
	{ "vfch", 0xeb, INSTR_VRR_VVV0MMM },
	{ "vfche", 0xea, INSTR_VRR_VVV0MMM },
	{ "vcdg", 0xc3, INSTR_VRR_VV00MMM },
	{ "vcdlg", 0xc1, INSTR_VRR_VV00MMM },
	{ "vcgd", 0xc2, INSTR_VRR_VV00MMM },
	{ "vclgd", 0xc0, INSTR_VRR_VV00MMM },
	{ "vfd", 0xe5, INSTR_VRR_VVV00MM },
	{ "vfi", 0xc7, INSTR_VRR_VV00MMM },
	{ "vlde", 0xc4, INSTR_VRR_VV000MM },
	{ "vled", 0xc5, INSTR_VRR_VV00MMM },
	{ "vfm", 0xe7, INSTR_VRR_VVV00MM },
	{ "vfma", 0x8f, INSTR_VRR_VVVM0MV },
	{ "vfms", 0x8e, INSTR_VRR_VVVM0MV },
	{ "vfpso", 0xcc, INSTR_VRR_VV00MMM },
	{ "vfsq", 0xce, INSTR_VRR_VV000MM },
	{ "vfs", 0xe2, INSTR_VRR_VVV00MM },
	{ "vftci", 0x4a, INSTR_VRI_VVIMM },
	{ "", 0, INSTR_INVALID }
};

static struct s390_insn opcode_eb[] = {
	{ "lmg", 0x04, INSTR_RSY_RRRD },
	{ "srag", 0x0a, INSTR_RSY_RRRD },
	{ "slag", 0x0b, INSTR_RSY_RRRD },
	{ "srlg", 0x0c, INSTR_RSY_RRRD },
	{ "sllg", 0x0d, INSTR_RSY_RRRD },
	{ "tracg", 0x0f, INSTR_RSY_RRRD },
	{ "csy", 0x14, INSTR_RSY_RRRD },
	{ "rllg", 0x1c, INSTR_RSY_RRRD },
	{ "clmh", 0x20, INSTR_RSY_RURD },
	{ "clmy", 0x21, INSTR_RSY_RURD },
	{ "clt", 0x23, INSTR_RSY_RURD },
	{ "stmg", 0x24, INSTR_RSY_RRRD },
	{ "stctg", 0x25, INSTR_RSY_CCRD },
	{ "stmh", 0x26, INSTR_RSY_RRRD },
	{ "clgt", 0x2b, INSTR_RSY_RURD },
	{ "stcmh", 0x2c, INSTR_RSY_RURD },
	{ "stcmy", 0x2d, INSTR_RSY_RURD },
	{ "lctlg", 0x2f, INSTR_RSY_CCRD },
	{ "csg", 0x30, INSTR_RSY_RRRD },
	{ "cdsy", 0x31, INSTR_RSY_RRRD },
	{ "cdsg", 0x3e, INSTR_RSY_RRRD },
	{ "bxhg", 0x44, INSTR_RSY_RRRD },
	{ "bxleg", 0x45, INSTR_RSY_RRRD },
	{ "ecag", 0x4c, INSTR_RSY_RRRD },
	{ "tmy", 0x51, INSTR_SIY_URD },
	{ "mviy", 0x52, INSTR_SIY_URD },
	{ "niy", 0x54, INSTR_SIY_URD },
	{ "cliy", 0x55, INSTR_SIY_URD },
	{ "oiy", 0x56, INSTR_SIY_URD },
	{ "xiy", 0x57, INSTR_SIY_URD },
	{ "asi", 0x6a, INSTR_SIY_IRD },
	{ "alsi", 0x6e, INSTR_SIY_IRD },
	{ "agsi", 0x7a, INSTR_SIY_IRD },
	{ "algsi", 0x7e, INSTR_SIY_IRD },
	{ "icmh", 0x80, INSTR_RSY_RURD },
	{ "icmy", 0x81, INSTR_RSY_RURD },
	{ "clclu", 0x8f, INSTR_RSY_RRRD },
	{ "stmy", 0x90, INSTR_RSY_RRRD },
	{ "lmh", 0x96, INSTR_RSY_RRRD },
	{ "lmy", 0x98, INSTR_RSY_RRRD },
	{ "lamy", 0x9a, INSTR_RSY_AARD },
	{ "stamy", 0x9b, INSTR_RSY_AARD },
	{ { 0, LONG_INSN_PCISTB }, 0xd0, INSTR_RSY_RRRD },
	{ "sic", 0xd1, INSTR_RSY_RRRD },
	{ "srak", 0xdc, INSTR_RSY_RRRD },
	{ "slak", 0xdd, INSTR_RSY_RRRD },
	{ "srlk", 0xde, INSTR_RSY_RRRD },
	{ "sllk", 0xdf, INSTR_RSY_RRRD },
	{ "locg", 0xe2, INSTR_RSY_RDRM },
	{ "stocg", 0xe3, INSTR_RSY_RDRM },
	{ "lang", 0xe4, INSTR_RSY_RRRD },
	{ "laog", 0xe6, INSTR_RSY_RRRD },
	{ "laxg", 0xe7, INSTR_RSY_RRRD },
	{ "laag", 0xe8, INSTR_RSY_RRRD },
	{ "laalg", 0xea, INSTR_RSY_RRRD },
	{ "loc", 0xf2, INSTR_RSY_RDRM },
	{ "stoc", 0xf3, INSTR_RSY_RDRM },
	{ "lan", 0xf4, INSTR_RSY_RRRD },
	{ "lao", 0xf6, INSTR_RSY_RRRD },
	{ "lax", 0xf7, INSTR_RSY_RRRD },
	{ "laa", 0xf8, INSTR_RSY_RRRD },
	{ "laal", 0xfa, INSTR_RSY_RRRD },
	{ "lric", 0x60, INSTR_RSY_RDRM },
	{ "stric", 0x61, INSTR_RSY_RDRM },
	{ "mric", 0x62, INSTR_RSY_RDRM },
	{ { 0, LONG_INSN_STCCTM }, 0x17, INSTR_RSY_RMRD },
	{ "rll", 0x1d, INSTR_RSY_RRRD },
	{ "mvclu", 0x8e, INSTR_RSY_RRRD },
	{ "tp", 0xc0, INSTR_RSL_R0RD },
	{ "", 0, INSTR_INVALID }
};

static struct s390_insn opcode_ec[] = {
	{ "brxhg", 0x44, INSTR_RIE_RRP },
	{ "brxlg", 0x45, INSTR_RIE_RRP },
	{ { 0, LONG_INSN_RISBLG }, 0x51, INSTR_RIE_RRUUU },
	{ "rnsbg", 0x54, INSTR_RIE_RRUUU },
	{ "risbg", 0x55, INSTR_RIE_RRUUU },
	{ "rosbg", 0x56, INSTR_RIE_RRUUU },
	{ "rxsbg", 0x57, INSTR_RIE_RRUUU },
	{ { 0, LONG_INSN_RISBGN }, 0x59, INSTR_RIE_RRUUU },
	{ { 0, LONG_INSN_RISBHG }, 0x5D, INSTR_RIE_RRUUU },
	{ "cgrj", 0x64, INSTR_RIE_RRPU },
	{ "clgrj", 0x65, INSTR_RIE_RRPU },
	{ "cgit", 0x70, INSTR_RIE_R0IU },
	{ "clgit", 0x71, INSTR_RIE_R0UU },
	{ "cit", 0x72, INSTR_RIE_R0IU },
	{ "clfit", 0x73, INSTR_RIE_R0UU },
	{ "crj", 0x76, INSTR_RIE_RRPU },
	{ "clrj", 0x77, INSTR_RIE_RRPU },
	{ "cgij", 0x7c, INSTR_RIE_RUPI },
	{ "clgij", 0x7d, INSTR_RIE_RUPU },
	{ "cij", 0x7e, INSTR_RIE_RUPI },
	{ "clij", 0x7f, INSTR_RIE_RUPU },
	{ "ahik", 0xd8, INSTR_RIE_RRI0 },
	{ "aghik", 0xd9, INSTR_RIE_RRI0 },
	{ { 0, LONG_INSN_ALHSIK }, 0xda, INSTR_RIE_RRI0 },
	{ { 0, LONG_INSN_ALGHSIK }, 0xdb, INSTR_RIE_RRI0 },
	{ "cgrb", 0xe4, INSTR_RRS_RRRDU },
	{ "clgrb", 0xe5, INSTR_RRS_RRRDU },
	{ "crb", 0xf6, INSTR_RRS_RRRDU },
	{ "clrb", 0xf7, INSTR_RRS_RRRDU },
	{ "cgib", 0xfc, INSTR_RIS_RURDI },
	{ "clgib", 0xfd, INSTR_RIS_RURDU },
	{ "cib", 0xfe, INSTR_RIS_RURDI },
	{ "clib", 0xff, INSTR_RIS_RURDU },
	{ "", 0, INSTR_INVALID }
};

static struct s390_insn opcode_ed[] = {
	{ "mayl", 0x38, INSTR_RXF_FRRDF },
	{ "myl", 0x39, INSTR_RXF_FRRDF },
	{ "may", 0x3a, INSTR_RXF_FRRDF },
	{ "my", 0x3b, INSTR_RXF_FRRDF },
	{ "mayh", 0x3c, INSTR_RXF_FRRDF },
	{ "myh", 0x3d, INSTR_RXF_FRRDF },
	{ "sldt", 0x40, INSTR_RXF_FRRDF },
	{ "srdt", 0x41, INSTR_RXF_FRRDF },
	{ "slxt", 0x48, INSTR_RXF_FRRDF },
	{ "srxt", 0x49, INSTR_RXF_FRRDF },
	{ "tdcet", 0x50, INSTR_RXE_FRRD },
	{ "tdget", 0x51, INSTR_RXE_FRRD },
	{ "tdcdt", 0x54, INSTR_RXE_FRRD },
	{ "tdgdt", 0x55, INSTR_RXE_FRRD },
	{ "tdcxt", 0x58, INSTR_RXE_FRRD },
	{ "tdgxt", 0x59, INSTR_RXE_FRRD },
	{ "ley", 0x64, INSTR_RXY_FRRD },
	{ "ldy", 0x65, INSTR_RXY_FRRD },
	{ "stey", 0x66, INSTR_RXY_FRRD },
	{ "stdy", 0x67, INSTR_RXY_FRRD },
	{ "czdt", 0xa8, INSTR_RSL_LRDFU },
	{ "czxt", 0xa9, INSTR_RSL_LRDFU },
	{ "cdzt", 0xaa, INSTR_RSL_LRDFU },
	{ "cxzt", 0xab, INSTR_RSL_LRDFU },
	{ "ldeb", 0x04, INSTR_RXE_FRRD },
	{ "lxdb", 0x05, INSTR_RXE_FRRD },
	{ "lxeb", 0x06, INSTR_RXE_FRRD },
	{ "mxdb", 0x07, INSTR_RXE_FRRD },
	{ "keb", 0x08, INSTR_RXE_FRRD },
	{ "ceb", 0x09, INSTR_RXE_FRRD },
	{ "aeb", 0x0a, INSTR_RXE_FRRD },
	{ "seb", 0x0b, INSTR_RXE_FRRD },
	{ "mdeb", 0x0c, INSTR_RXE_FRRD },
	{ "deb", 0x0d, INSTR_RXE_FRRD },
	{ "maeb", 0x0e, INSTR_RXF_FRRDF },
	{ "mseb", 0x0f, INSTR_RXF_FRRDF },
	{ "tceb", 0x10, INSTR_RXE_FRRD },
	{ "tcdb", 0x11, INSTR_RXE_FRRD },
	{ "tcxb", 0x12, INSTR_RXE_FRRD },
	{ "sqeb", 0x14, INSTR_RXE_FRRD },
	{ "sqdb", 0x15, INSTR_RXE_FRRD },
	{ "meeb", 0x17, INSTR_RXE_FRRD },
	{ "kdb", 0x18, INSTR_RXE_FRRD },
	{ "cdb", 0x19, INSTR_RXE_FRRD },
	{ "adb", 0x1a, INSTR_RXE_FRRD },
	{ "sdb", 0x1b, INSTR_RXE_FRRD },
	{ "mdb", 0x1c, INSTR_RXE_FRRD },
	{ "ddb", 0x1d, INSTR_RXE_FRRD },
	{ "madb", 0x1e, INSTR_RXF_FRRDF },
	{ "msdb", 0x1f, INSTR_RXF_FRRDF },
	{ "lde", 0x24, INSTR_RXE_FRRD },
	{ "lxd", 0x25, INSTR_RXE_FRRD },
	{ "lxe", 0x26, INSTR_RXE_FRRD },
	{ "mae", 0x2e, INSTR_RXF_FRRDF },
	{ "mse", 0x2f, INSTR_RXF_FRRDF },
	{ "sqe", 0x34, INSTR_RXE_FRRD },
	{ "sqd", 0x35, INSTR_RXE_FRRD },
	{ "mee", 0x37, INSTR_RXE_FRRD },
	{ "mad", 0x3e, INSTR_RXF_FRRDF },
	{ "msd", 0x3f, INSTR_RXF_FRRDF },
	{ "", 0, INSTR_INVALID }
};

/* Extracts an operand value from an instruction.  */
static unsigned int extract_operand(unsigned char *code,
				    const struct s390_operand *operand)
{
	unsigned char *cp;
	unsigned int val;
	int bits;

	/* Extract fragments of the operand byte for byte.  */
	cp = code + operand->shift / 8;
	bits = (operand->shift & 7) + operand->bits;
	val = 0;
	do {
		val <<= 8;
		val |= (unsigned int) *cp++;
		bits -= 8;
	} while (bits > 0);
	val >>= -bits;
	val &= ((1U << (operand->bits - 1)) << 1) - 1;

	/* Check for special long displacement case.  */
	if (operand->bits == 20 && operand->shift == 20)
		val = (val & 0xff) << 12 | (val & 0xfff00) >> 8;

	/* Check for register extensions bits for vector registers. */
	if (operand->flags & OPERAND_VR) {
		if (operand->shift == 8)
			val |= (code[4] & 8) << 1;
		else if (operand->shift == 12)
			val |= (code[4] & 4) << 2;
		else if (operand->shift == 16)
			val |= (code[4] & 2) << 3;
		else if (operand->shift == 32)
			val |= (code[4] & 1) << 4;
	}

	/* Sign extend value if the operand is signed or pc relative.  */
	if ((operand->flags & (OPERAND_SIGNED | OPERAND_PCREL)) &&
	    (val & (1U << (operand->bits - 1))))
		val |= (-1U << (operand->bits - 1)) << 1;

	/* Double value if the operand is pc relative.	*/
	if (operand->flags & OPERAND_PCREL)
		val <<= 1;

	/* Length x in an instructions has real length x + 1.  */
	if (operand->flags & OPERAND_LENGTH)
		val++;
	return val;
}

struct s390_insn *find_insn(unsigned char *code)
{
	unsigned char opfrag = code[1];
	unsigned char opmask;
	struct s390_insn *table;

	switch (code[0]) {
	case 0x01:
		table = opcode_01;
		break;
	case 0xa5:
		table = opcode_a5;
		break;
	case 0xa7:
		table = opcode_a7;
		break;
	case 0xaa:
		table = opcode_aa;
		break;
	case 0xb2:
		table = opcode_b2;
		break;
	case 0xb3:
		table = opcode_b3;
		break;
	case 0xb9:
		table = opcode_b9;
		break;
	case 0xc0:
		table = opcode_c0;
		break;
	case 0xc2:
		table = opcode_c2;
		break;
	case 0xc4:
		table = opcode_c4;
		break;
	case 0xc6:
		table = opcode_c6;
		break;
	case 0xc8:
		table = opcode_c8;
		break;
	case 0xcc:
		table = opcode_cc;
		break;
	case 0xe3:
		table = opcode_e3;
		opfrag = code[5];
		break;
	case 0xe5:
		table = opcode_e5;
		break;
	case 0xe7:
		table = opcode_e7;
		opfrag = code[5];
		break;
	case 0xeb:
		table = opcode_eb;
		opfrag = code[5];
		break;
	case 0xec:
		table = opcode_ec;
		opfrag = code[5];
		break;
	case 0xed:
		table = opcode_ed;
		opfrag = code[5];
		break;
	default:
		table = opcode;
		opfrag = code[0];
		break;
	}
	while (table->format != INSTR_INVALID) {
		opmask = formats[table->format][0];
		if (table->opfrag == (opfrag & opmask))
			return table;
		table++;
	}
	return NULL;
}

/**
 * insn_to_mnemonic - decode an s390 instruction
 * @instruction: instruction to decode
 * @buf: buffer to fill with mnemonic
 * @len: length of buffer
 *
 * Decode the instruction at @instruction and store the corresponding
 * mnemonic into @buf of length @len.
 * @buf is left unchanged if the instruction could not be decoded.
 * Returns:
 *  %0 on success, %-ENOENT if the instruction was not found.
 */
int insn_to_mnemonic(unsigned char *instruction, char *buf, unsigned int len)
{
	struct s390_insn *insn;

	insn = find_insn(instruction);
	if (!insn)
		return -ENOENT;
	if (insn->name[0] == '\0')
		snprintf(buf, len, "%s",
			 long_insn_name[(int) insn->name[1]]);
	else
		snprintf(buf, len, "%.5s", insn->name);
	return 0;
}
EXPORT_SYMBOL_GPL(insn_to_mnemonic);

static int print_insn(char *buffer, unsigned char *code, unsigned long addr)
{
	struct s390_insn *insn;
	const unsigned char *ops;
	const struct s390_operand *operand;
	unsigned int value;
	char separator;
	char *ptr;
	int i;

	ptr = buffer;
	insn = find_insn(code);
	if (insn) {
		if (insn->name[0] == '\0')
			ptr += sprintf(ptr, "%s\t",
				       long_insn_name[(int) insn->name[1]]);
		else
			ptr += sprintf(ptr, "%.5s\t", insn->name);
		/* Extract the operands. */
		separator = 0;
		for (ops = formats[insn->format] + 1, i = 0;
		     *ops != 0 && i < 6; ops++, i++) {
			operand = operands + *ops;
			value = extract_operand(code, operand);
			if ((operand->flags & OPERAND_INDEX)  && value == 0)
				continue;
			if ((operand->flags & OPERAND_BASE) &&
			    value == 0 && separator == '(') {
				separator = ',';
				continue;
			}
			if (separator)
				ptr += sprintf(ptr, "%c", separator);
			if (operand->flags & OPERAND_GPR)
				ptr += sprintf(ptr, "%%r%i", value);
			else if (operand->flags & OPERAND_FPR)
				ptr += sprintf(ptr, "%%f%i", value);
			else if (operand->flags & OPERAND_AR)
				ptr += sprintf(ptr, "%%a%i", value);
			else if (operand->flags & OPERAND_CR)
				ptr += sprintf(ptr, "%%c%i", value);
			else if (operand->flags & OPERAND_VR)
				ptr += sprintf(ptr, "%%v%i", value);
			else if (operand->flags & OPERAND_PCREL)
				ptr += sprintf(ptr, "%lx", (signed int) value
								      + addr);
			else if (operand->flags & OPERAND_SIGNED)
				ptr += sprintf(ptr, "%i", value);
			else
				ptr += sprintf(ptr, "%u", value);
			if (operand->flags & OPERAND_DISP)
				separator = '(';
			else if (operand->flags & OPERAND_BASE) {
				ptr += sprintf(ptr, ")");
				separator = ',';
			} else
				separator = ',';
		}
	} else
		ptr += sprintf(ptr, "unknown");
	return (int) (ptr - buffer);
}

void show_code(struct pt_regs *regs)
{
	char *mode = user_mode(regs) ? "User" : "Krnl";
	unsigned char code[64];
	char buffer[128], *ptr;
	mm_segment_t old_fs;
	unsigned long addr;
	int start, end, opsize, hops, i;

	/* Get a snapshot of the 64 bytes surrounding the fault address. */
	old_fs = get_fs();
	set_fs(user_mode(regs) ? USER_DS : KERNEL_DS);
	for (start = 32; start && regs->psw.addr >= 34 - start; start -= 2) {
		addr = regs->psw.addr - 34 + start;
		if (__copy_from_user(code + start - 2,
				     (char __user *) addr, 2))
			break;
	}
	for (end = 32; end < 64; end += 2) {
		addr = regs->psw.addr + end - 32;
		if (__copy_from_user(code + end,
				     (char __user *) addr, 2))
			break;
	}
	set_fs(old_fs);
	/* Code snapshot useable ? */
	if ((regs->psw.addr & 1) || start >= end) {
		printk("%s Code: Bad PSW.\n", mode);
		return;
	}
	/* Find a starting point for the disassembly. */
	while (start < 32) {
		for (i = 0, hops = 0; start + i < 32 && hops < 3; hops++) {
			if (!find_insn(code + start + i))
				break;
			i += insn_length(code[start + i]);
		}
		if (start + i == 32)
			/* Looks good, sequence ends at PSW. */
			break;
		start += 2;
	}
	/* Decode the instructions. */
	ptr = buffer;
	ptr += sprintf(ptr, "%s Code:", mode);
	hops = 0;
	while (start < end && hops < 8) {
		opsize = insn_length(code[start]);
		if  (start + opsize == 32)
			*ptr++ = '#';
		else if (start == 32)
			*ptr++ = '>';
		else
			*ptr++ = ' ';
		addr = regs->psw.addr + start - 32;
		ptr += sprintf(ptr, "%016lx: ", addr);
		if (start + opsize >= end)
			break;
		for (i = 0; i < opsize; i++)
			ptr += sprintf(ptr, "%02x", code[start + i]);
		*ptr++ = '\t';
		if (i < 6)
			*ptr++ = '\t';
		ptr += print_insn(ptr, code + start, addr);
		start += opsize;
		pr_cont("%s", buffer);
		ptr = buffer;
		ptr += sprintf(ptr, "\n\t  ");
		hops++;
	}
	pr_cont("\n");
}

void print_fn_code(unsigned char *code, unsigned long len)
{
	char buffer[64], *ptr;
	int opsize, i;

	while (len) {
		ptr = buffer;
		opsize = insn_length(*code);
		if (opsize > len)
			break;
		ptr += sprintf(ptr, "%p: ", code);
		for (i = 0; i < opsize; i++)
			ptr += sprintf(ptr, "%02x", code[i]);
		*ptr++ = '\t';
		if (i < 4)
			*ptr++ = '\t';
		ptr += print_insn(ptr, code, (unsigned long) code);
		*ptr++ = '\n';
		*ptr++ = 0;
		printk("%s", buffer);
		code += opsize;
		len -= opsize;
	}
}
