/*
 * AVR32 System Registers
 *
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_SYSREG_H
#define __ASM_AVR32_SYSREG_H

/* sysreg register offsets */
#define SYSREG_SR				0x0000
#define SYSREG_EVBA				0x0004
#define SYSREG_ACBA				0x0008
#define SYSREG_CPUCR				0x000c
#define SYSREG_ECR				0x0010
#define SYSREG_RSR_SUP				0x0014
#define SYSREG_RSR_INT0				0x0018
#define SYSREG_RSR_INT1				0x001c
#define SYSREG_RSR_INT2				0x0020
#define SYSREG_RSR_INT3				0x0024
#define SYSREG_RSR_EX				0x0028
#define SYSREG_RSR_NMI				0x002c
#define SYSREG_RSR_DBG				0x0030
#define SYSREG_RAR_SUP				0x0034
#define SYSREG_RAR_INT0				0x0038
#define SYSREG_RAR_INT1				0x003c
#define SYSREG_RAR_INT2				0x0040
#define SYSREG_RAR_INT3				0x0044
#define SYSREG_RAR_EX				0x0048
#define SYSREG_RAR_NMI				0x004c
#define SYSREG_RAR_DBG				0x0050
#define SYSREG_JECR				0x0054
#define SYSREG_JOSP				0x0058
#define SYSREG_JAVA_LV0				0x005c
#define SYSREG_JAVA_LV1				0x0060
#define SYSREG_JAVA_LV2				0x0064
#define SYSREG_JAVA_LV3				0x0068
#define SYSREG_JAVA_LV4				0x006c
#define SYSREG_JAVA_LV5				0x0070
#define SYSREG_JAVA_LV6				0x0074
#define SYSREG_JAVA_LV7				0x0078
#define SYSREG_JTBA				0x007c
#define SYSREG_JBCR				0x0080
#define SYSREG_CONFIG0				0x0100
#define SYSREG_CONFIG1				0x0104
#define SYSREG_COUNT				0x0108
#define SYSREG_COMPARE				0x010c
#define SYSREG_TLBEHI				0x0110
#define SYSREG_TLBELO				0x0114
#define SYSREG_PTBR				0x0118
#define SYSREG_TLBEAR				0x011c
#define SYSREG_MMUCR				0x0120
#define SYSREG_TLBARLO				0x0124
#define SYSREG_TLBARHI				0x0128
#define SYSREG_PCCNT				0x012c
#define SYSREG_PCNT0				0x0130
#define SYSREG_PCNT1				0x0134
#define SYSREG_PCCR				0x0138
#define SYSREG_BEAR				0x013c
#define SYSREG_SABAL				0x0300
#define SYSREG_SABAH				0x0304
#define SYSREG_SABD				0x0308

/* Bitfields in SR */
#define SYSREG_SR_C_OFFSET			0
#define SYSREG_SR_C_SIZE			1
#define SYSREG_Z_OFFSET				1
#define SYSREG_Z_SIZE				1
#define SYSREG_SR_N_OFFSET			2
#define SYSREG_SR_N_SIZE			1
#define SYSREG_SR_V_OFFSET			3
#define SYSREG_SR_V_SIZE			1
#define SYSREG_Q_OFFSET				4
#define SYSREG_Q_SIZE				1
#define SYSREG_L_OFFSET				5
#define SYSREG_L_SIZE				1
#define SYSREG_T_OFFSET				14
#define SYSREG_T_SIZE				1
#define SYSREG_SR_R_OFFSET			15
#define SYSREG_SR_R_SIZE			1
#define SYSREG_GM_OFFSET			16
#define SYSREG_GM_SIZE				1
#define SYSREG_I0M_OFFSET			17
#define SYSREG_I0M_SIZE				1
#define SYSREG_I1M_OFFSET			18
#define SYSREG_I1M_SIZE				1
#define SYSREG_I2M_OFFSET			19
#define SYSREG_I2M_SIZE				1
#define SYSREG_I3M_OFFSET			20
#define SYSREG_I3M_SIZE				1
#define SYSREG_EM_OFFSET			21
#define SYSREG_EM_SIZE				1
#define SYSREG_MODE_OFFSET			22
#define SYSREG_MODE_SIZE			3
#define SYSREG_M0_OFFSET			22
#define SYSREG_M0_SIZE				1
#define SYSREG_M1_OFFSET			23
#define SYSREG_M1_SIZE				1
#define SYSREG_M2_OFFSET			24
#define SYSREG_M2_SIZE				1
#define SYSREG_SR_D_OFFSET			26
#define SYSREG_SR_D_SIZE			1
#define SYSREG_DM_OFFSET			27
#define SYSREG_DM_SIZE				1
#define SYSREG_SR_J_OFFSET			28
#define SYSREG_SR_J_SIZE			1
#define SYSREG_H_OFFSET				29
#define SYSREG_H_SIZE				1

/* Bitfields in CPUCR */
#define SYSREG_BI_OFFSET			0
#define SYSREG_BI_SIZE				1
#define SYSREG_BE_OFFSET			1
#define SYSREG_BE_SIZE				1
#define SYSREG_FE_OFFSET			2
#define SYSREG_FE_SIZE				1
#define SYSREG_RE_OFFSET			3
#define SYSREG_RE_SIZE				1
#define SYSREG_IBE_OFFSET			4
#define SYSREG_IBE_SIZE				1
#define SYSREG_IEE_OFFSET			5
#define SYSREG_IEE_SIZE				1

/* Bitfields in CONFIG0 */
#define SYSREG_CONFIG0_R_OFFSET			0
#define SYSREG_CONFIG0_R_SIZE			1
#define SYSREG_CONFIG0_D_OFFSET			1
#define SYSREG_CONFIG0_D_SIZE			1
#define SYSREG_CONFIG0_S_OFFSET			2
#define SYSREG_CONFIG0_S_SIZE			1
#define SYSREG_CONFIG0_O_OFFSET			3
#define SYSREG_CONFIG0_O_SIZE			1
#define SYSREG_CONFIG0_P_OFFSET			4
#define SYSREG_CONFIG0_P_SIZE			1
#define SYSREG_CONFIG0_J_OFFSET			5
#define SYSREG_CONFIG0_J_SIZE			1
#define SYSREG_CONFIG0_F_OFFSET			6
#define SYSREG_CONFIG0_F_SIZE			1
#define SYSREG_MMUT_OFFSET			7
#define SYSREG_MMUT_SIZE			3
#define SYSREG_AR_OFFSET			10
#define SYSREG_AR_SIZE				3
#define SYSREG_AT_OFFSET			13
#define SYSREG_AT_SIZE				3
#define SYSREG_PROCESSORREVISION_OFFSET		16
#define SYSREG_PROCESSORREVISION_SIZE		8
#define SYSREG_PROCESSORID_OFFSET		24
#define SYSREG_PROCESSORID_SIZE			8

/* Bitfields in CONFIG1 */
#define SYSREG_DASS_OFFSET			0
#define SYSREG_DASS_SIZE			3
#define SYSREG_DLSZ_OFFSET			3
#define SYSREG_DLSZ_SIZE			3
#define SYSREG_DSET_OFFSET			6
#define SYSREG_DSET_SIZE			4
#define SYSREG_IASS_OFFSET			10
#define SYSREG_IASS_SIZE			3
#define SYSREG_ILSZ_OFFSET			13
#define SYSREG_ILSZ_SIZE			3
#define SYSREG_ISET_OFFSET			16
#define SYSREG_ISET_SIZE			4
#define SYSREG_DMMUSZ_OFFSET			20
#define SYSREG_DMMUSZ_SIZE			6
#define SYSREG_IMMUSZ_OFFSET			26
#define SYSREG_IMMUSZ_SIZE			6

/* Bitfields in TLBEHI */
#define SYSREG_ASID_OFFSET			0
#define SYSREG_ASID_SIZE			8
#define SYSREG_TLBEHI_I_OFFSET			8
#define SYSREG_TLBEHI_I_SIZE			1
#define SYSREG_TLBEHI_V_OFFSET			9
#define SYSREG_TLBEHI_V_SIZE			1
#define SYSREG_VPN_OFFSET			10
#define SYSREG_VPN_SIZE				22

/* Bitfields in TLBELO */
#define SYSREG_W_OFFSET				0
#define SYSREG_W_SIZE				1
#define SYSREG_TLBELO_D_OFFSET			1
#define SYSREG_TLBELO_D_SIZE			1
#define SYSREG_SZ_OFFSET			2
#define SYSREG_SZ_SIZE				2
#define SYSREG_AP_OFFSET			4
#define SYSREG_AP_SIZE				3
#define SYSREG_B_OFFSET				7
#define SYSREG_B_SIZE				1
#define SYSREG_G_OFFSET				8
#define SYSREG_G_SIZE				1
#define SYSREG_TLBELO_C_OFFSET			9
#define SYSREG_TLBELO_C_SIZE			1
#define SYSREG_PFN_OFFSET			10
#define SYSREG_PFN_SIZE				22

/* Bitfields in MMUCR */
#define SYSREG_E_OFFSET				0
#define SYSREG_E_SIZE				1
#define SYSREG_M_OFFSET				1
#define SYSREG_M_SIZE				1
#define SYSREG_MMUCR_I_OFFSET			2
#define SYSREG_MMUCR_I_SIZE			1
#define SYSREG_MMUCR_N_OFFSET			3
#define SYSREG_MMUCR_N_SIZE			1
#define SYSREG_MMUCR_S_OFFSET			4
#define SYSREG_MMUCR_S_SIZE			1
#define SYSREG_DLA_OFFSET			8
#define SYSREG_DLA_SIZE				6
#define SYSREG_DRP_OFFSET			14
#define SYSREG_DRP_SIZE				6
#define SYSREG_ILA_OFFSET			20
#define SYSREG_ILA_SIZE				6
#define SYSREG_IRP_OFFSET			26
#define SYSREG_IRP_SIZE				6

/* Bitfields in PCCR */
#define SYSREG_PCCR_E_OFFSET			0
#define SYSREG_PCCR_E_SIZE			1
#define SYSREG_PCCR_R_OFFSET			1
#define SYSREG_PCCR_R_SIZE			1
#define SYSREG_PCCR_C_OFFSET			2
#define SYSREG_PCCR_C_SIZE			1
#define SYSREG_PCCR_S_OFFSET			3
#define SYSREG_PCCR_S_SIZE			1
#define SYSREG_IEC_OFFSET			4
#define SYSREG_IEC_SIZE				1
#define SYSREG_IE0_OFFSET			5
#define SYSREG_IE0_SIZE				1
#define SYSREG_IE1_OFFSET			6
#define SYSREG_IE1_SIZE				1
#define SYSREG_FC_OFFSET			8
#define SYSREG_FC_SIZE				1
#define SYSREG_F0_OFFSET			9
#define SYSREG_F0_SIZE				1
#define SYSREG_F1_OFFSET			10
#define SYSREG_F1_SIZE				1
#define SYSREG_CONF0_OFFSET			12
#define SYSREG_CONF0_SIZE			6
#define SYSREG_CONF1_OFFSET			18
#define SYSREG_CONF1_SIZE			6

/* Constants for ECR */
#define ECR_UNRECOVERABLE			0
#define ECR_TLB_MULTIPLE			1
#define ECR_BUS_ERROR_WRITE			2
#define ECR_BUS_ERROR_READ			3
#define ECR_NMI					4
#define ECR_ADDR_ALIGN_X			5
#define ECR_PROTECTION_X			6
#define ECR_DEBUG				7
#define ECR_ILLEGAL_OPCODE			8
#define ECR_UNIMPL_INSTRUCTION			9
#define ECR_PRIVILEGE_VIOLATION			10
#define ECR_FPE					11
#define ECR_COPROC_ABSENT			12
#define ECR_ADDR_ALIGN_R			13
#define ECR_ADDR_ALIGN_W			14
#define ECR_PROTECTION_R			15
#define ECR_PROTECTION_W			16
#define ECR_DTLB_MODIFIED			17
#define ECR_TLB_MISS_X				20
#define ECR_TLB_MISS_R				24
#define ECR_TLB_MISS_W				28

/* Bit manipulation macros */
#define SYSREG_BIT(name)				\
	(1 << SYSREG_##name##_OFFSET)
#define SYSREG_BF(name,value)				\
	(((value) & ((1 << SYSREG_##name##_SIZE) - 1))	\
	 << SYSREG_##name##_OFFSET)
#define SYSREG_BFEXT(name,value)\
	(((value) >> SYSREG_##name##_OFFSET)		\
	 & ((1 << SYSREG_##name##_SIZE) - 1))
#define SYSREG_BFINS(name,value,old)			\
	(((old) & ~(((1 << SYSREG_##name##_SIZE) - 1)	\
		    << SYSREG_##name##_OFFSET))		\
	 | SYSREG_BF(name,value))

/* Register access macros */
#ifdef __CHECKER__
extern unsigned long __builtin_mfsr(unsigned long reg);
extern void __builtin_mtsr(unsigned long reg, unsigned long value);
#endif

#define sysreg_read(reg)		__builtin_mfsr(SYSREG_##reg)
#define sysreg_write(reg, value)	__builtin_mtsr(SYSREG_##reg, value)

#endif /* __ASM_AVR32_SYSREG_H */
