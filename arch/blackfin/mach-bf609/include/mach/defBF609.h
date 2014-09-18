/*
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the Clear BSD license or the GPL-2 (or later)
 */

#ifndef _DEF_BF609_H
#define _DEF_BF609_H

/* Include defBF60x_base.h for the set of #defines that are common to all ADSP-BF60x processors */
#include "defBF60x_base.h"

/* The following are the #defines needed by ADSP-BF609 that are not in the common header */
/* =========================
	PIXC Registers
   ========================= */

/* =========================
	PIXC0
   ========================= */
#define PIXC0_CTL                   0xFFC19000         /* PIXC0 Control Register */
#define PIXC0_PPL                   0xFFC19004         /* PIXC0 Pixels Per Line Register */
#define PIXC0_LPF                   0xFFC19008         /* PIXC0 Line Per Frame Register */
#define PIXC0_HSTART_A              0xFFC1900C         /* PIXC0 Overlay A Horizontal Start Register */
#define PIXC0_HEND_A                0xFFC19010         /* PIXC0 Overlay A Horizontal End Register */
#define PIXC0_VSTART_A              0xFFC19014         /* PIXC0 Overlay A Vertical Start Register */
#define PIXC0_VEND_A                0xFFC19018         /* PIXC0 Overlay A Vertical End Register */
#define PIXC0_TRANSP_A              0xFFC1901C         /* PIXC0 Overlay A Transparency Ratio Register */
#define PIXC0_HSTART_B              0xFFC19020         /* PIXC0 Overlay B Horizontal Start Register */
#define PIXC0_HEND_B                0xFFC19024         /* PIXC0 Overlay B Horizontal End Register */
#define PIXC0_VSTART_B              0xFFC19028         /* PIXC0 Overlay B Vertical Start Register */
#define PIXC0_VEND_B                0xFFC1902C         /* PIXC0 Overlay B Vertical End Register */
#define PIXC0_TRANSP_B              0xFFC19030         /* PIXC0 Overlay B Transparency Ratio Register */
#define PIXC0_IRQSTAT               0xFFC1903C         /* PIXC0 Interrupt Status Register */
#define PIXC0_CONRY                 0xFFC19040         /* PIXC0 RY Conversion Component Register */
#define PIXC0_CONGU                 0xFFC19044         /* PIXC0 GU Conversion Component Register */
#define PIXC0_CONBV                 0xFFC19048         /* PIXC0 BV Conversion Component Register */
#define PIXC0_CCBIAS                0xFFC1904C         /* PIXC0 Conversion Bias Register */
#define PIXC0_TC                    0xFFC19050         /* PIXC0 Transparency Register */
#define PIXC0_REVID                 0xFFC19054         /* PIXC0 PIXC Revision Id */

/* =========================
	PVP Registers
   ========================= */

/* =========================
	PVP0
   ========================= */
#define PVP0_REVID                  0xFFC1A000         /* PVP0 Revision ID */
#define PVP0_CTL                    0xFFC1A004         /* PVP0 Control */
#define PVP0_IMSK0                  0xFFC1A008         /* PVP0 INTn interrupt line masks */
#define PVP0_IMSK1                  0xFFC1A00C         /* PVP0 INTn interrupt line masks */
#define PVP0_STAT                   0xFFC1A010         /* PVP0 Status */
#define PVP0_ILAT                   0xFFC1A014         /* PVP0 Latched status */
#define PVP0_IREQ0                  0xFFC1A018         /* PVP0 INT0 masked latched status */
#define PVP0_IREQ1                  0xFFC1A01C         /* PVP0 INT0 masked latched status */
#define PVP0_OPF0_CFG               0xFFC1A020         /* PVP0 Config */
#define PVP0_OPF1_CFG               0xFFC1A040         /* PVP0 Config */
#define PVP0_OPF2_CFG               0xFFC1A060         /* PVP0 Config */
#define PVP0_OPF0_CTL               0xFFC1A024         /* PVP0 Control */
#define PVP0_OPF1_CTL               0xFFC1A044         /* PVP0 Control */
#define PVP0_OPF2_CTL               0xFFC1A064         /* PVP0 Control */
#define PVP0_OPF3_CFG               0xFFC1A080         /* PVP0 Config */
#define PVP0_OPF3_CTL               0xFFC1A084         /* PVP0 Control */
#define PVP0_PEC_CFG                0xFFC1A0A0         /* PVP0 Config */
#define PVP0_PEC_CTL                0xFFC1A0A4         /* PVP0 Control */
#define PVP0_PEC_D1TH0              0xFFC1A0A8         /* PVP0 Lower Hysteresis Threshold */
#define PVP0_PEC_D1TH1              0xFFC1A0AC         /* PVP0 Upper Hysteresis Threshold */
#define PVP0_PEC_D2TH0              0xFFC1A0B0         /* PVP0 Weak Zero Crossing Threshold */
#define PVP0_PEC_D2TH1              0xFFC1A0B4         /* PVP0 Strong Zero Crossing Threshold */
#define PVP0_IIM0_CFG               0xFFC1A0C0         /* PVP0 Config */
#define PVP0_IIM1_CFG               0xFFC1A0E0         /* PVP0 Config */
#define PVP0_IIM0_CTL               0xFFC1A0C4         /* PVP0 Control */
#define PVP0_IIM1_CTL               0xFFC1A0E4         /* PVP0 Control */
#define PVP0_IIM0_SCALE             0xFFC1A0C8         /* PVP0 Scaler Values */
#define PVP0_IIM1_SCALE             0xFFC1A0E8         /* PVP0 Scaler Values */
#define PVP0_IIM0_SOVF_STAT         0xFFC1A0CC         /* PVP0 Signed Overflow Status */
#define PVP0_IIM1_SOVF_STAT         0xFFC1A0EC         /* PVP0 Signed Overflow Status */
#define PVP0_IIM0_UOVF_STAT         0xFFC1A0D0         /* PVP0 Unsigned Overflow Status */
#define PVP0_IIM1_UOVF_STAT         0xFFC1A0F0         /* PVP0 Unsigned Overflow Status */
#define PVP0_ACU_CFG                0xFFC1A100         /* PVP0 ACU Configuration Register */
#define PVP0_ACU_CTL                0xFFC1A104         /* PVP0 ACU Control Register */
#define PVP0_ACU_OFFSET             0xFFC1A108         /* PVP0 SUM constant register */
#define PVP0_ACU_FACTOR             0xFFC1A10C         /* PVP0 PROD constant register */
#define PVP0_ACU_SHIFT              0xFFC1A110         /* PVP0 Shift constant register */
#define PVP0_ACU_MIN                0xFFC1A114         /* PVP0 Lower saturation threshold set to MIN */
#define PVP0_ACU_MAX                0xFFC1A118         /* PVP0 Upper saturation threshold set to MAX */
#define PVP0_UDS_CFG                0xFFC1A140         /* PVP0 UDS Configuration Register */
#define PVP0_UDS_CTL                0xFFC1A144         /* PVP0 UDS Control Register */
#define PVP0_UDS_OHCNT              0xFFC1A148         /* PVP0 UDS Output H Dimension */
#define PVP0_UDS_OVCNT              0xFFC1A14C         /* PVP0 UDS Output V Dimension */
#define PVP0_UDS_HAVG               0xFFC1A150         /* PVP0 UDS H Taps */
#define PVP0_UDS_VAVG               0xFFC1A154         /* PVP0 UDS V Taps */
#define PVP0_IPF0_CFG               0xFFC1A180         /* PVP0 Configuration */
#define PVP0_IPF0_PIPECTL           0xFFC1A184         /* PVP0 Pipe Control */
#define PVP0_IPF1_PIPECTL           0xFFC1A1C4         /* PVP0 Pipe Control */
#define PVP0_IPF0_CTL               0xFFC1A188         /* PVP0 Control */
#define PVP0_IPF1_CTL               0xFFC1A1C8         /* PVP0 Control */
#define PVP0_IPF0_TAG               0xFFC1A18C         /* PVP0 TAG Value */
#define PVP0_IPF1_TAG               0xFFC1A1CC         /* PVP0 TAG Value */
#define PVP0_IPF0_FCNT              0xFFC1A190         /* PVP0 Frame Count */
#define PVP0_IPF1_FCNT              0xFFC1A1D0         /* PVP0 Frame Count */
#define PVP0_IPF0_HCNT              0xFFC1A194         /* PVP0 Horizontal Count */
#define PVP0_IPF1_HCNT              0xFFC1A1D4         /* PVP0 Horizontal Count */
#define PVP0_IPF0_VCNT              0xFFC1A198         /* PVP0 Vertical Count */
#define PVP0_IPF1_VCNT              0xFFC1A1D8         /* PVP0 Vertical Count */
#define PVP0_IPF0_HPOS              0xFFC1A19C         /* PVP0 Horizontal Position */
#define PVP0_IPF0_VPOS              0xFFC1A1A0         /* PVP0 Vertical Position */
#define PVP0_IPF0_TAG_STAT          0xFFC1A1A4         /* PVP0 TAG Status */
#define PVP0_IPF1_TAG_STAT          0xFFC1A1E4         /* PVP0 TAG Status */
#define PVP0_IPF1_CFG               0xFFC1A1C0         /* PVP0 Configuration */
#define PVP0_CNV0_CFG               0xFFC1A200         /* PVP0 Configuration */
#define PVP0_CNV1_CFG               0xFFC1A280         /* PVP0 Configuration */
#define PVP0_CNV2_CFG               0xFFC1A300         /* PVP0 Configuration */
#define PVP0_CNV3_CFG               0xFFC1A380         /* PVP0 Configuration */
#define PVP0_CNV0_CTL               0xFFC1A204         /* PVP0 Control */
#define PVP0_CNV1_CTL               0xFFC1A284         /* PVP0 Control */
#define PVP0_CNV2_CTL               0xFFC1A304         /* PVP0 Control */
#define PVP0_CNV3_CTL               0xFFC1A384         /* PVP0 Control */
#define PVP0_CNV0_C00C01            0xFFC1A208         /* PVP0 Coefficients 0, 0 and 0, 1 */
#define PVP0_CNV1_C00C01            0xFFC1A288         /* PVP0 Coefficients 0, 0 and 0, 1 */
#define PVP0_CNV2_C00C01            0xFFC1A308         /* PVP0 Coefficients 0, 0 and 0, 1 */
#define PVP0_CNV3_C00C01            0xFFC1A388         /* PVP0 Coefficients 0, 0 and 0, 1 */
#define PVP0_CNV0_C02C03            0xFFC1A20C         /* PVP0 Coefficients 0, 2 and 0, 3 */
#define PVP0_CNV1_C02C03            0xFFC1A28C         /* PVP0 Coefficients 0, 2 and 0, 3 */
#define PVP0_CNV2_C02C03            0xFFC1A30C         /* PVP0 Coefficients 0, 2 and 0, 3 */
#define PVP0_CNV3_C02C03            0xFFC1A38C         /* PVP0 Coefficients 0, 2 and 0, 3 */
#define PVP0_CNV0_C04               0xFFC1A210         /* PVP0 Coefficient 0, 4 */
#define PVP0_CNV1_C04               0xFFC1A290         /* PVP0 Coefficient 0, 4 */
#define PVP0_CNV2_C04               0xFFC1A310         /* PVP0 Coefficient 0, 4 */
#define PVP0_CNV3_C04               0xFFC1A390         /* PVP0 Coefficient 0, 4 */
#define PVP0_CNV0_C10C11            0xFFC1A214         /* PVP0 Coefficients 1, 0 and 1, 1 */
#define PVP0_CNV1_C10C11            0xFFC1A294         /* PVP0 Coefficients 1, 0 and 1, 1 */
#define PVP0_CNV2_C10C11            0xFFC1A314         /* PVP0 Coefficients 1, 0 and 1, 1 */
#define PVP0_CNV3_C10C11            0xFFC1A394         /* PVP0 Coefficients 1, 0 and 1, 1 */
#define PVP0_CNV0_C12C13            0xFFC1A218         /* PVP0 Coefficients 1, 2 and 1, 3 */
#define PVP0_CNV1_C12C13            0xFFC1A298         /* PVP0 Coefficients 1, 2 and 1, 3 */
#define PVP0_CNV2_C12C13            0xFFC1A318         /* PVP0 Coefficients 1, 2 and 1, 3 */
#define PVP0_CNV3_C12C13            0xFFC1A398         /* PVP0 Coefficients 1, 2 and 1, 3 */
#define PVP0_CNV0_C14               0xFFC1A21C         /* PVP0 Coefficient 1, 4 */
#define PVP0_CNV1_C14               0xFFC1A29C         /* PVP0 Coefficient 1, 4 */
#define PVP0_CNV2_C14               0xFFC1A31C         /* PVP0 Coefficient 1, 4 */
#define PVP0_CNV3_C14               0xFFC1A39C         /* PVP0 Coefficient 1, 4 */
#define PVP0_CNV0_C20C21            0xFFC1A220         /* PVP0 Coefficients 2, 0 and 2, 1 */
#define PVP0_CNV1_C20C21            0xFFC1A2A0         /* PVP0 Coefficients 2, 0 and 2, 1 */
#define PVP0_CNV2_C20C21            0xFFC1A320         /* PVP0 Coefficients 2, 0 and 2, 1 */
#define PVP0_CNV3_C20C21            0xFFC1A3A0         /* PVP0 Coefficients 2, 0 and 2, 1 */
#define PVP0_CNV0_C22C23            0xFFC1A224         /* PVP0 Coefficients 2, 2 and 2, 3 */
#define PVP0_CNV1_C22C23            0xFFC1A2A4         /* PVP0 Coefficients 2, 2 and 2, 3 */
#define PVP0_CNV2_C22C23            0xFFC1A324         /* PVP0 Coefficients 2, 2 and 2, 3 */
#define PVP0_CNV3_C22C23            0xFFC1A3A4         /* PVP0 Coefficients 2, 2 and 2, 3 */
#define PVP0_CNV0_C24               0xFFC1A228         /* PVP0 Coefficient 2,4 */
#define PVP0_CNV1_C24               0xFFC1A2A8         /* PVP0 Coefficient 2,4 */
#define PVP0_CNV2_C24               0xFFC1A328         /* PVP0 Coefficient 2,4 */
#define PVP0_CNV3_C24               0xFFC1A3A8         /* PVP0 Coefficient 2,4 */
#define PVP0_CNV0_C30C31            0xFFC1A22C         /* PVP0 Coefficients 3, 0 and 3, 1 */
#define PVP0_CNV1_C30C31            0xFFC1A2AC         /* PVP0 Coefficients 3, 0 and 3, 1 */
#define PVP0_CNV2_C30C31            0xFFC1A32C         /* PVP0 Coefficients 3, 0 and 3, 1 */
#define PVP0_CNV3_C30C31            0xFFC1A3AC         /* PVP0 Coefficients 3, 0 and 3, 1 */
#define PVP0_CNV0_C32C33            0xFFC1A230         /* PVP0 Coefficients 3, 2 and 3, 3 */
#define PVP0_CNV1_C32C33            0xFFC1A2B0         /* PVP0 Coefficients 3, 2 and 3, 3 */
#define PVP0_CNV2_C32C33            0xFFC1A330         /* PVP0 Coefficients 3, 2 and 3, 3 */
#define PVP0_CNV3_C32C33            0xFFC1A3B0         /* PVP0 Coefficients 3, 2 and 3, 3 */
#define PVP0_CNV0_C34               0xFFC1A234         /* PVP0 Coefficient 3, 4 */
#define PVP0_CNV1_C34               0xFFC1A2B4         /* PVP0 Coefficient 3, 4 */
#define PVP0_CNV2_C34               0xFFC1A334         /* PVP0 Coefficient 3, 4 */
#define PVP0_CNV3_C34               0xFFC1A3B4         /* PVP0 Coefficient 3, 4 */
#define PVP0_CNV0_C40C41            0xFFC1A238         /* PVP0 Coefficients 4, 0 and 4, 1 */
#define PVP0_CNV1_C40C41            0xFFC1A2B8         /* PVP0 Coefficients 4, 0 and 4, 1 */
#define PVP0_CNV2_C40C41            0xFFC1A338         /* PVP0 Coefficients 4, 0 and 4, 1 */
#define PVP0_CNV3_C40C41            0xFFC1A3B8         /* PVP0 Coefficients 4, 0 and 4, 1 */
#define PVP0_CNV0_C42C43            0xFFC1A23C         /* PVP0 Coefficients 4, 2 and 4, 3 */
#define PVP0_CNV1_C42C43            0xFFC1A2BC         /* PVP0 Coefficients 4, 2 and 4, 3 */
#define PVP0_CNV2_C42C43            0xFFC1A33C         /* PVP0 Coefficients 4, 2 and 4, 3 */
#define PVP0_CNV3_C42C43            0xFFC1A3BC         /* PVP0 Coefficients 4, 2 and 4, 3 */
#define PVP0_CNV0_C44               0xFFC1A240         /* PVP0 Coefficient 4, 4 */
#define PVP0_CNV1_C44               0xFFC1A2C0         /* PVP0 Coefficient 4, 4 */
#define PVP0_CNV2_C44               0xFFC1A340         /* PVP0 Coefficient 4, 4 */
#define PVP0_CNV3_C44               0xFFC1A3C0         /* PVP0 Coefficient 4, 4 */
#define PVP0_CNV0_SCALE             0xFFC1A244         /* PVP0 Scaling factor */
#define PVP0_CNV1_SCALE             0xFFC1A2C4         /* PVP0 Scaling factor */
#define PVP0_CNV2_SCALE             0xFFC1A344         /* PVP0 Scaling factor */
#define PVP0_CNV3_SCALE             0xFFC1A3C4         /* PVP0 Scaling factor */
#define PVP0_THC0_CFG               0xFFC1A400         /* PVP0 Configuration */
#define PVP0_THC1_CFG               0xFFC1A500         /* PVP0 Configuration */
#define PVP0_THC0_CTL               0xFFC1A404         /* PVP0 Control */
#define PVP0_THC1_CTL               0xFFC1A504         /* PVP0 Control */
#define PVP0_THC0_HFCNT             0xFFC1A408         /* PVP0 Number of frames */
#define PVP0_THC1_HFCNT             0xFFC1A508         /* PVP0 Number of frames */
#define PVP0_THC0_RMAXREP           0xFFC1A40C         /* PVP0 Maximum number of RLE reports */
#define PVP0_THC1_RMAXREP           0xFFC1A50C         /* PVP0 Maximum number of RLE reports */
#define PVP0_THC0_CMINVAL           0xFFC1A410         /* PVP0 Min clip value */
#define PVP0_THC1_CMINVAL           0xFFC1A510         /* PVP0 Min clip value */
#define PVP0_THC0_CMINTH            0xFFC1A414         /* PVP0 Clip Min Threshold */
#define PVP0_THC1_CMINTH            0xFFC1A514         /* PVP0 Clip Min Threshold */
#define PVP0_THC0_CMAXTH            0xFFC1A418         /* PVP0 Clip Max Threshold */
#define PVP0_THC1_CMAXTH            0xFFC1A518         /* PVP0 Clip Max Threshold */
#define PVP0_THC0_CMAXVAL           0xFFC1A41C         /* PVP0 Max clip value */
#define PVP0_THC1_CMAXVAL           0xFFC1A51C         /* PVP0 Max clip value */
#define PVP0_THC0_TH0               0xFFC1A420         /* PVP0 Threshold Value */
#define PVP0_THC1_TH0               0xFFC1A520         /* PVP0 Threshold Value */
#define PVP0_THC0_TH1               0xFFC1A424         /* PVP0 Threshold Value */
#define PVP0_THC1_TH1               0xFFC1A524         /* PVP0 Threshold Value */
#define PVP0_THC0_TH2               0xFFC1A428         /* PVP0 Threshold Value */
#define PVP0_THC1_TH2               0xFFC1A528         /* PVP0 Threshold Value */
#define PVP0_THC0_TH3               0xFFC1A42C         /* PVP0 Threshold Value */
#define PVP0_THC1_TH3               0xFFC1A52C         /* PVP0 Threshold Value */
#define PVP0_THC0_TH4               0xFFC1A430         /* PVP0 Threshold Value */
#define PVP0_THC1_TH4               0xFFC1A530         /* PVP0 Threshold Value */
#define PVP0_THC0_TH5               0xFFC1A434         /* PVP0 Threshold Value */
#define PVP0_THC1_TH5               0xFFC1A534         /* PVP0 Threshold Value */
#define PVP0_THC0_TH6               0xFFC1A438         /* PVP0 Threshold Value */
#define PVP0_THC1_TH6               0xFFC1A538         /* PVP0 Threshold Value */
#define PVP0_THC0_TH7               0xFFC1A43C         /* PVP0 Threshold Value */
#define PVP0_THC1_TH7               0xFFC1A53C         /* PVP0 Threshold Value */
#define PVP0_THC0_TH8               0xFFC1A440         /* PVP0 Threshold Value */
#define PVP0_THC1_TH8               0xFFC1A540         /* PVP0 Threshold Value */
#define PVP0_THC0_TH9               0xFFC1A444         /* PVP0 Threshold Value */
#define PVP0_THC1_TH9               0xFFC1A544         /* PVP0 Threshold Value */
#define PVP0_THC0_TH10              0xFFC1A448         /* PVP0 Threshold Value */
#define PVP0_THC1_TH10              0xFFC1A548         /* PVP0 Threshold Value */
#define PVP0_THC0_TH11              0xFFC1A44C         /* PVP0 Threshold Value */
#define PVP0_THC1_TH11              0xFFC1A54C         /* PVP0 Threshold Value */
#define PVP0_THC0_TH12              0xFFC1A450         /* PVP0 Threshold Value */
#define PVP0_THC1_TH12              0xFFC1A550         /* PVP0 Threshold Value */
#define PVP0_THC0_TH13              0xFFC1A454         /* PVP0 Threshold Value */
#define PVP0_THC1_TH13              0xFFC1A554         /* PVP0 Threshold Value */
#define PVP0_THC0_TH14              0xFFC1A458         /* PVP0 Threshold Value */
#define PVP0_THC1_TH14              0xFFC1A558         /* PVP0 Threshold Value */
#define PVP0_THC0_TH15              0xFFC1A45C         /* PVP0 Threshold Value */
#define PVP0_THC1_TH15              0xFFC1A55C         /* PVP0 Threshold Value */
#define PVP0_THC0_HHPOS             0xFFC1A460         /* PVP0 Window start X-coordinate */
#define PVP0_THC1_HHPOS             0xFFC1A560         /* PVP0 Window start X-coordinate */
#define PVP0_THC0_HVPOS             0xFFC1A464         /* PVP0 Window start Y-coordinate */
#define PVP0_THC1_HVPOS             0xFFC1A564         /* PVP0 Window start Y-coordinate */
#define PVP0_THC0_HHCNT             0xFFC1A468         /* PVP0 Window width in X dimension */
#define PVP0_THC1_HHCNT             0xFFC1A568         /* PVP0 Window width in X dimension */
#define PVP0_THC0_HVCNT             0xFFC1A46C         /* PVP0 Window width in Y dimension */
#define PVP0_THC1_HVCNT             0xFFC1A56C         /* PVP0 Window width in Y dimension */
#define PVP0_THC0_RHPOS             0xFFC1A470         /* PVP0 Window start X-coordinate */
#define PVP0_THC1_RHPOS             0xFFC1A570         /* PVP0 Window start X-coordinate */
#define PVP0_THC0_RVPOS             0xFFC1A474         /* PVP0 Window start Y-coordinate */
#define PVP0_THC1_RVPOS             0xFFC1A574         /* PVP0 Window start Y-coordinate */
#define PVP0_THC0_RHCNT             0xFFC1A478         /* PVP0 Window width in X dimension */
#define PVP0_THC1_RHCNT             0xFFC1A578         /* PVP0 Window width in X dimension */
#define PVP0_THC0_RVCNT             0xFFC1A47C         /* PVP0 Window width in Y dimension */
#define PVP0_THC1_RVCNT             0xFFC1A57C         /* PVP0 Window width in Y dimension */
#define PVP0_THC0_HFCNT_STAT        0xFFC1A480         /* PVP0 Current Frame counter */
#define PVP0_THC1_HFCNT_STAT        0xFFC1A580         /* PVP0 Current Frame counter */
#define PVP0_THC0_HCNT0_STAT        0xFFC1A484         /* PVP0 Histogram counter value */
#define PVP0_THC1_HCNT0_STAT        0xFFC1A584         /* PVP0 Histogram counter value */
#define PVP0_THC0_HCNT1_STAT        0xFFC1A488         /* PVP0 Histogram counter value */
#define PVP0_THC1_HCNT1_STAT        0xFFC1A588         /* PVP0 Histogram counter value */
#define PVP0_THC0_HCNT2_STAT        0xFFC1A48C         /* PVP0 Histogram counter value */
#define PVP0_THC1_HCNT2_STAT        0xFFC1A58C         /* PVP0 Histogram counter value */
#define PVP0_THC0_HCNT3_STAT        0xFFC1A490         /* PVP0 Histogram counter value */
#define PVP0_THC1_HCNT3_STAT        0xFFC1A590         /* PVP0 Histogram counter value */
#define PVP0_THC0_HCNT4_STAT        0xFFC1A494         /* PVP0 Histogram counter value */
#define PVP0_THC1_HCNT4_STAT        0xFFC1A594         /* PVP0 Histogram counter value */
#define PVP0_THC0_HCNT5_STAT        0xFFC1A498         /* PVP0 Histogram counter value */
#define PVP0_THC1_HCNT5_STAT        0xFFC1A598         /* PVP0 Histogram counter value */
#define PVP0_THC0_HCNT6_STAT        0xFFC1A49C         /* PVP0 Histogram counter value */
#define PVP0_THC1_HCNT6_STAT        0xFFC1A59C         /* PVP0 Histogram counter value */
#define PVP0_THC0_HCNT7_STAT        0xFFC1A4A0         /* PVP0 Histogram counter value */
#define PVP0_THC1_HCNT7_STAT        0xFFC1A5A0         /* PVP0 Histogram counter value */
#define PVP0_THC0_HCNT8_STAT        0xFFC1A4A4         /* PVP0 Histogram counter value */
#define PVP0_THC1_HCNT8_STAT        0xFFC1A5A4         /* PVP0 Histogram counter value */
#define PVP0_THC0_HCNT9_STAT        0xFFC1A4A8         /* PVP0 Histogram counter value */
#define PVP0_THC1_HCNT9_STAT        0xFFC1A5A8         /* PVP0 Histogram counter value */
#define PVP0_THC0_HCNT10_STAT       0xFFC1A4AC         /* PVP0 Histogram counter value */
#define PVP0_THC1_HCNT10_STAT       0xFFC1A5AC         /* PVP0 Histogram counter value */
#define PVP0_THC0_HCNT11_STAT       0xFFC1A4B0         /* PVP0 Histogram counter value */
#define PVP0_THC1_HCNT11_STAT       0xFFC1A5B0         /* PVP0 Histogram counter value */
#define PVP0_THC0_HCNT12_STAT       0xFFC1A4B4         /* PVP0 Histogram counter value */
#define PVP0_THC1_HCNT12_STAT       0xFFC1A5B4         /* PVP0 Histogram counter value */
#define PVP0_THC0_HCNT13_STAT       0xFFC1A4B8         /* PVP0 Histogram counter value */
#define PVP0_THC1_HCNT13_STAT       0xFFC1A5B8         /* PVP0 Histogram counter value */
#define PVP0_THC0_HCNT14_STAT       0xFFC1A4BC         /* PVP0 Histogram counter value */
#define PVP0_THC1_HCNT14_STAT       0xFFC1A5BC         /* PVP0 Histogram counter value */
#define PVP0_THC0_HCNT15_STAT       0xFFC1A4C0         /* PVP0 Histogram counter value */
#define PVP0_THC1_HCNT15_STAT       0xFFC1A5C0         /* PVP0 Histogram counter value */
#define PVP0_THC0_RREP_STAT         0xFFC1A4C4         /* PVP0 Number of RLE Reports */
#define PVP0_THC1_RREP_STAT         0xFFC1A5C4         /* PVP0 Number of RLE Reports */
#define PVP0_PMA_CFG                0xFFC1A600         /* PVP0 PMA Configuration Register */

#endif /* _DEF_BF609_H */
