/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Various register offset definitions for debuggers, core file
 * examiners and whatnot.
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#ifndef __UAPI_ASM_LOONGARCH_REG_H
#define __UAPI_ASM_LOONGARCH_REG_H

#define LOONGARCH_EF_R0		0
#define LOONGARCH_EF_R1		1
#define LOONGARCH_EF_R2		2
#define LOONGARCH_EF_R3		3
#define LOONGARCH_EF_R4		4
#define LOONGARCH_EF_R5		5
#define LOONGARCH_EF_R6		6
#define LOONGARCH_EF_R7		7
#define LOONGARCH_EF_R8		8
#define LOONGARCH_EF_R9		9
#define LOONGARCH_EF_R10	10
#define LOONGARCH_EF_R11	11
#define LOONGARCH_EF_R12	12
#define LOONGARCH_EF_R13	13
#define LOONGARCH_EF_R14	14
#define LOONGARCH_EF_R15	15
#define LOONGARCH_EF_R16	16
#define LOONGARCH_EF_R17	17
#define LOONGARCH_EF_R18	18
#define LOONGARCH_EF_R19	19
#define LOONGARCH_EF_R20	20
#define LOONGARCH_EF_R21	21
#define LOONGARCH_EF_R22	22
#define LOONGARCH_EF_R23	23
#define LOONGARCH_EF_R24	24
#define LOONGARCH_EF_R25	25
#define LOONGARCH_EF_R26	26
#define LOONGARCH_EF_R27	27
#define LOONGARCH_EF_R28	28
#define LOONGARCH_EF_R29	29
#define LOONGARCH_EF_R30	30
#define LOONGARCH_EF_R31	31

/*
 * Saved special registers
 */
#define LOONGARCH_EF_ORIG_A0	32
#define LOONGARCH_EF_CSR_ERA	33
#define LOONGARCH_EF_CSR_BADV	34
#define LOONGARCH_EF_CSR_CRMD	35
#define LOONGARCH_EF_CSR_PRMD	36
#define LOONGARCH_EF_CSR_EUEN	37
#define LOONGARCH_EF_CSR_ECFG	38
#define LOONGARCH_EF_CSR_ESTAT	39

#define LOONGARCH_EF_SIZE	320	/* size in bytes */

#endif /* __UAPI_ASM_LOONGARCH_REG_H */
