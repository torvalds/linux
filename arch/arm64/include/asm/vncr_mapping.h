/* SPDX-License-Identifier: GPL-2.0 */
/*
 * System register offsets in the VNCR page
 * All offsets are *byte* displacements!
 */

#ifndef __ARM64_VNCR_MAPPING_H__
#define __ARM64_VNCR_MAPPING_H__

#define VNCR_VTTBR_EL2          0x020
#define VNCR_VTCR_EL2           0x040
#define VNCR_VMPIDR_EL2         0x050
#define VNCR_CNTVOFF_EL2        0x060
#define VNCR_HCR_EL2            0x078
#define VNCR_HSTR_EL2           0x080
#define VNCR_VPIDR_EL2          0x088
#define VNCR_TPIDR_EL2          0x090
#define VNCR_HCRX_EL2           0x0A0
#define VNCR_VNCR_EL2           0x0B0
#define VNCR_CPACR_EL1          0x100
#define VNCR_CONTEXTIDR_EL1     0x108
#define VNCR_SCTLR_EL1          0x110
#define VNCR_ACTLR_EL1          0x118
#define VNCR_TCR_EL1            0x120
#define VNCR_AFSR0_EL1          0x128
#define VNCR_AFSR1_EL1          0x130
#define VNCR_ESR_EL1            0x138
#define VNCR_MAIR_EL1           0x140
#define VNCR_AMAIR_EL1          0x148
#define VNCR_MDSCR_EL1          0x158
#define VNCR_SPSR_EL1           0x160
#define VNCR_CNTV_CVAL_EL0      0x168
#define VNCR_CNTV_CTL_EL0       0x170
#define VNCR_CNTP_CVAL_EL0      0x178
#define VNCR_CNTP_CTL_EL0       0x180
#define VNCR_SCXTNUM_EL1        0x188
#define VNCR_TFSR_EL1		0x190
#define VNCR_HDFGRTR2_EL2	0x1A0
#define VNCR_HDFGWTR2_EL2	0x1B0
#define VNCR_HFGRTR_EL2		0x1B8
#define VNCR_HFGWTR_EL2		0x1C0
#define VNCR_HFGITR_EL2		0x1C8
#define VNCR_HDFGRTR_EL2	0x1D0
#define VNCR_HDFGWTR_EL2	0x1D8
#define VNCR_ZCR_EL1            0x1E0
#define VNCR_HAFGRTR_EL2	0x1E8
#define VNCR_TTBR0_EL1          0x200
#define VNCR_TTBR1_EL1          0x210
#define VNCR_FAR_EL1            0x220
#define VNCR_ELR_EL1            0x230
#define VNCR_SP_EL1             0x240
#define VNCR_VBAR_EL1           0x250
#define VNCR_TCR2_EL1		0x270
#define VNCR_PIRE0_EL1		0x290
#define VNCR_PIR_EL1		0x2A0
#define VNCR_POR_EL1		0x2A8
#define VNCR_HFGRTR2_EL2	0x2C0
#define VNCR_HFGWTR2_EL2	0x2C8
#define VNCR_HFGITR2_EL2	0x310
#define VNCR_ICH_LR0_EL2        0x400
#define VNCR_ICH_LR1_EL2        0x408
#define VNCR_ICH_LR2_EL2        0x410
#define VNCR_ICH_LR3_EL2        0x418
#define VNCR_ICH_LR4_EL2        0x420
#define VNCR_ICH_LR5_EL2        0x428
#define VNCR_ICH_LR6_EL2        0x430
#define VNCR_ICH_LR7_EL2        0x438
#define VNCR_ICH_LR8_EL2        0x440
#define VNCR_ICH_LR9_EL2        0x448
#define VNCR_ICH_LR10_EL2       0x450
#define VNCR_ICH_LR11_EL2       0x458
#define VNCR_ICH_LR12_EL2       0x460
#define VNCR_ICH_LR13_EL2       0x468
#define VNCR_ICH_LR14_EL2       0x470
#define VNCR_ICH_LR15_EL2       0x478
#define VNCR_ICH_AP0R0_EL2      0x480
#define VNCR_ICH_AP0R1_EL2      0x488
#define VNCR_ICH_AP0R2_EL2      0x490
#define VNCR_ICH_AP0R3_EL2      0x498
#define VNCR_ICH_AP1R0_EL2      0x4A0
#define VNCR_ICH_AP1R1_EL2      0x4A8
#define VNCR_ICH_AP1R2_EL2      0x4B0
#define VNCR_ICH_AP1R3_EL2      0x4B8
#define VNCR_ICH_HCR_EL2        0x4C0
#define VNCR_ICH_VMCR_EL2       0x4C8
#define VNCR_VDISR_EL2          0x500
#define VNCR_VSESR_EL2		0x508
#define VNCR_PMBLIMITR_EL1      0x800
#define VNCR_PMBPTR_EL1         0x810
#define VNCR_PMBSR_EL1          0x820
#define VNCR_PMSCR_EL1          0x828
#define VNCR_PMSEVFR_EL1        0x830
#define VNCR_PMSICR_EL1         0x838
#define VNCR_PMSIRR_EL1         0x840
#define VNCR_PMSLATFR_EL1       0x848
#define VNCR_TRFCR_EL1          0x880
#define VNCR_MPAM1_EL1          0x900
#define VNCR_MPAMHCR_EL2        0x930
#define VNCR_MPAMVPMV_EL2       0x938
#define VNCR_MPAMVPM0_EL2       0x940
#define VNCR_MPAMVPM1_EL2       0x948
#define VNCR_MPAMVPM2_EL2       0x950
#define VNCR_MPAMVPM3_EL2       0x958
#define VNCR_MPAMVPM4_EL2       0x960
#define VNCR_MPAMVPM5_EL2       0x968
#define VNCR_MPAMVPM6_EL2       0x970
#define VNCR_MPAMVPM7_EL2       0x978

#endif /* __ARM64_VNCR_MAPPING_H__ */
