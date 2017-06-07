/*
 *  Driver for the Conexant CX25821 PCIe bridge
 *
 *  Copyright (C) 2009 Conexant Systems Inc.
 *  Authors  <shu.lin@conexant.com>, <hiep.huynh@conexant.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 */

#ifndef __MEDUSA_REGISTERS__
#define __MEDUSA_REGISTERS__

/* Serial Slave Registers */
#define	HOST_REGISTER1				0x0000
#define	HOST_REGISTER2				0x0001

/* Chip Configuration Registers */
#define	CHIP_CTRL				0x0100
#define	AFE_AB_CTRL				0x0104
#define	AFE_CD_CTRL				0x0108
#define	AFE_EF_CTRL				0x010C
#define	AFE_GH_CTRL				0x0110
#define	DENC_AB_CTRL				0x0114
#define	BYP_AB_CTRL				0x0118
#define	MON_A_CTRL				0x011C
#define	DISP_SEQ_A				0x0120
#define	DISP_SEQ_B				0x0124
#define	DISP_AB_CNT				0x0128
#define	DISP_CD_CNT				0x012C
#define	DISP_EF_CNT				0x0130
#define	DISP_GH_CNT				0x0134
#define	DISP_IJ_CNT				0x0138
#define	PIN_OE_CTRL				0x013C
#define	PIN_SPD_CTRL				0x0140
#define	PIN_SPD_CTRL2				0x0144
#define	IRQ_STAT_CTRL				0x0148
#define	POWER_CTRL_AB				0x014C
#define	POWER_CTRL_CD				0x0150
#define	POWER_CTRL_EF				0x0154
#define	POWER_CTRL_GH				0x0158
#define	TUNE_CTRL				0x015C
#define	BIAS_CTRL				0x0160
#define	AFE_AB_DIAG_CTRL			0x0164
#define	AFE_CD_DIAG_CTRL			0x0168
#define	AFE_EF_DIAG_CTRL			0x016C
#define	AFE_GH_DIAG_CTRL			0x0170
#define	PLL_AB_DIAG_CTRL			0x0174
#define	PLL_CD_DIAG_CTRL			0x0178
#define	PLL_EF_DIAG_CTRL			0x017C
#define	PLL_GH_DIAG_CTRL			0x0180
#define	TEST_CTRL				0x0184
#define	BIST_STAT				0x0188
#define	BIST_STAT2				0x018C
#define	BIST_VID_PLL_AB_STAT			0x0190
#define	BIST_VID_PLL_CD_STAT			0x0194
#define	BIST_VID_PLL_EF_STAT			0x0198
#define	BIST_VID_PLL_GH_STAT			0x019C
#define	DLL_DIAG_CTRL				0x01A0
#define	DEV_CH_ID_CTRL				0x01A4
#define	ABIST_CTRL_STATUS			0x01A8
#define	ABIST_FREQ				0x01AC
#define	ABIST_GOERT_SHIFT			0x01B0
#define	ABIST_COEF12				0x01B4
#define	ABIST_COEF34				0x01B8
#define	ABIST_COEF56				0x01BC
#define	ABIST_COEF7_SNR				0x01C0
#define	ABIST_ADC_CAL				0x01C4
#define	ABIST_BIN1_VGA0				0x01C8
#define	ABIST_BIN2_VGA1				0x01CC
#define	ABIST_BIN3_VGA2				0x01D0
#define	ABIST_BIN4_VGA3				0x01D4
#define	ABIST_BIN5_VGA4				0x01D8
#define	ABIST_BIN6_VGA5				0x01DC
#define	ABIST_BIN7_VGA6				0x01E0
#define	ABIST_CLAMP_A				0x01E4
#define	ABIST_CLAMP_B				0x01E8
#define	ABIST_CLAMP_C				0x01EC
#define	ABIST_CLAMP_D				0x01F0
#define	ABIST_CLAMP_E				0x01F4
#define	ABIST_CLAMP_F				0x01F8

/* Digital Video Encoder A Registers */
#define	DENC_A_REG_1				0x0200
#define	DENC_A_REG_2				0x0204
#define	DENC_A_REG_3				0x0208
#define	DENC_A_REG_4				0x020C
#define	DENC_A_REG_5				0x0210
#define	DENC_A_REG_6				0x0214
#define	DENC_A_REG_7				0x0218
#define	DENC_A_REG_8				0x021C

/* Digital Video Encoder B Registers */
#define	DENC_B_REG_1				0x0300
#define	DENC_B_REG_2				0x0304
#define	DENC_B_REG_3				0x0308
#define	DENC_B_REG_4				0x030C
#define	DENC_B_REG_5				0x0310
#define	DENC_B_REG_6				0x0314
#define	DENC_B_REG_7				0x0318
#define	DENC_B_REG_8				0x031C

/* Video Decoder A Registers */
#define	MODE_CTRL				0x1000
#define	OUT_CTRL1				0x1004
#define	OUT_CTRL_NS				0x1008
#define	GEN_STAT				0x100C
#define	INT_STAT_MASK				0x1010
#define	LUMA_CTRL				0x1014
#define	CHROMA_CTRL				0x1018
#define	CRUSH_CTRL				0x101C
#define	HORIZ_TIM_CTRL				0x1020
#define	VERT_TIM_CTRL				0x1024
#define	MISC_TIM_CTRL				0x1028
#define	FIELD_COUNT				0x102C
#define	HSCALE_CTRL				0x1030
#define	VSCALE_CTRL				0x1034
#define	MAN_VGA_CTRL				0x1038
#define	MAN_AGC_CTRL				0x103C
#define	DFE_CTRL1				0x1040
#define	DFE_CTRL2				0x1044
#define	DFE_CTRL3				0x1048
#define	PLL_CTRL				0x104C
#define	PLL_CTRL_FAST				0x1050
#define	HTL_CTRL				0x1054
#define	SRC_CFG					0x1058
#define	SC_STEP_SIZE				0x105C
#define	SC_CONVERGE_CTRL			0x1060
#define	SC_LOOP_CTRL				0x1064
#define	COMB_2D_HFS_CFG				0x1068
#define	COMB_2D_HFD_CFG				0x106C
#define	COMB_2D_LF_CFG				0x1070
#define	COMB_2D_BLEND				0x1074
#define	COMB_MISC_CTRL				0x1078
#define	COMB_FLAT_THRESH_CTRL			0x107C
#define	COMB_TEST				0x1080
#define	BP_MISC_CTRL				0x1084
#define	VCR_DET_CTRL				0x1088
#define	NOISE_DET_CTRL				0x108C
#define	COMB_FLAT_NOISE_CTRL			0x1090
#define	VERSION					0x11F8
#define	SOFT_RST_CTRL				0x11FC

/* Video Decoder B Registers */
#define	VDEC_B_MODE_CTRL			0x1200
#define	VDEC_B_OUT_CTRL1			0x1204
#define	VDEC_B_OUT_CTRL_NS			0x1208
#define	VDEC_B_GEN_STAT				0x120C
#define	VDEC_B_INT_STAT_MASK			0x1210
#define	VDEC_B_LUMA_CTRL			0x1214
#define	VDEC_B_CHROMA_CTRL			0x1218
#define	VDEC_B_CRUSH_CTRL			0x121C
#define	VDEC_B_HORIZ_TIM_CTRL			0x1220
#define	VDEC_B_VERT_TIM_CTRL			0x1224
#define	VDEC_B_MISC_TIM_CTRL			0x1228
#define	VDEC_B_FIELD_COUNT			0x122C
#define	VDEC_B_HSCALE_CTRL			0x1230
#define	VDEC_B_VSCALE_CTRL			0x1234
#define	VDEC_B_MAN_VGA_CTRL			0x1238
#define	VDEC_B_MAN_AGC_CTRL			0x123C
#define	VDEC_B_DFE_CTRL1			0x1240
#define	VDEC_B_DFE_CTRL2			0x1244
#define	VDEC_B_DFE_CTRL3			0x1248
#define	VDEC_B_PLL_CTRL				0x124C
#define	VDEC_B_PLL_CTRL_FAST			0x1250
#define	VDEC_B_HTL_CTRL				0x1254
#define	VDEC_B_SRC_CFG				0x1258
#define	VDEC_B_SC_STEP_SIZE			0x125C
#define	VDEC_B_SC_CONVERGE_CTRL			0x1260
#define	VDEC_B_SC_LOOP_CTRL			0x1264
#define	VDEC_B_COMB_2D_HFS_CFG			0x1268
#define	VDEC_B_COMB_2D_HFD_CFG			0x126C
#define	VDEC_B_COMB_2D_LF_CFG			0x1270
#define	VDEC_B_COMB_2D_BLEND			0x1274
#define	VDEC_B_COMB_MISC_CTRL			0x1278
#define	VDEC_B_COMB_FLAT_THRESH_CTRL		0x127C
#define	VDEC_B_COMB_TEST			0x1280
#define	VDEC_B_BP_MISC_CTRL			0x1284
#define	VDEC_B_VCR_DET_CTRL			0x1288
#define	VDEC_B_NOISE_DET_CTRL			0x128C
#define	VDEC_B_COMB_FLAT_NOISE_CTRL		0x1290
#define	VDEC_B_VERSION				0x13F8
#define	VDEC_B_SOFT_RST_CTRL			0x13FC

/* Video Decoder C Registers */
#define	VDEC_C_MODE_CTRL			0x1400
#define	VDEC_C_OUT_CTRL1			0x1404
#define	VDEC_C_OUT_CTRL_NS			0x1408
#define	VDEC_C_GEN_STAT				0x140C
#define	VDEC_C_INT_STAT_MASK			0x1410
#define VDEC_C_LUMA_CTRL			0x1414
#define VDEC_C_CHROMA_CTRL			0x1418
#define	VDEC_C_CRUSH_CTRL			0x141C
#define	VDEC_C_HORIZ_TIM_CTRL			0x1420
#define	VDEC_C_VERT_TIM_CTRL			0x1424
#define	VDEC_C_MISC_TIM_CTRL			0x1428
#define	VDEC_C_FIELD_COUNT			0x142C
#define	VDEC_C_HSCALE_CTRL			0x1430
#define	VDEC_C_VSCALE_CTRL			0x1434
#define	VDEC_C_MAN_VGA_CTRL			0x1438
#define	VDEC_C_MAN_AGC_CTRL			0x143C
#define	VDEC_C_DFE_CTRL1			0x1440
#define	VDEC_C_DFE_CTRL2			0x1444
#define	VDEC_C_DFE_CTRL3			0x1448
#define	VDEC_C_PLL_CTRL				0x144C
#define	VDEC_C_PLL_CTRL_FAST			0x1450
#define	VDEC_C_HTL_CTRL				0x1454
#define	VDEC_C_SRC_CFG				0x1458
#define	VDEC_C_SC_STEP_SIZE			0x145C
#define	VDEC_C_SC_CONVERGE_CTRL			0x1460
#define	VDEC_C_SC_LOOP_CTRL			0x1464
#define	VDEC_C_COMB_2D_HFS_CFG			0x1468
#define	VDEC_C_COMB_2D_HFD_CFG			0x146C
#define	VDEC_C_COMB_2D_LF_CFG			0x1470
#define	VDEC_C_COMB_2D_BLEND			0x1474
#define	VDEC_C_COMB_MISC_CTRL			0x1478
#define	VDEC_C_COMB_FLAT_THRESH_CTRL		0x147C
#define	VDEC_C_COMB_TEST			0x1480
#define	VDEC_C_BP_MISC_CTRL			0x1484
#define	VDEC_C_VCR_DET_CTRL			0x1488
#define	VDEC_C_NOISE_DET_CTRL			0x148C
#define	VDEC_C_COMB_FLAT_NOISE_CTRL		0x1490
#define	VDEC_C_VERSION				0x15F8
#define	VDEC_C_SOFT_RST_CTRL			0x15FC

/* Video Decoder D Registers */
#define VDEC_D_MODE_CTRL			0x1600
#define VDEC_D_OUT_CTRL1			0x1604
#define VDEC_D_OUT_CTRL_NS			0x1608
#define VDEC_D_GEN_STAT				0x160C
#define VDEC_D_INT_STAT_MASK			0x1610
#define VDEC_D_LUMA_CTRL			0x1614
#define VDEC_D_CHROMA_CTRL			0x1618
#define VDEC_D_CRUSH_CTRL			0x161C
#define VDEC_D_HORIZ_TIM_CTRL			0x1620
#define VDEC_D_VERT_TIM_CTRL			0x1624
#define VDEC_D_MISC_TIM_CTRL			0x1628
#define VDEC_D_FIELD_COUNT			0x162C
#define VDEC_D_HSCALE_CTRL			0x1630
#define VDEC_D_VSCALE_CTRL			0x1634
#define VDEC_D_MAN_VGA_CTRL			0x1638
#define VDEC_D_MAN_AGC_CTRL			0x163C
#define VDEC_D_DFE_CTRL1			0x1640
#define VDEC_D_DFE_CTRL2			0x1644
#define VDEC_D_DFE_CTRL3			0x1648
#define VDEC_D_PLL_CTRL				0x164C
#define VDEC_D_PLL_CTRL_FAST			0x1650
#define VDEC_D_HTL_CTRL				0x1654
#define VDEC_D_SRC_CFG				0x1658
#define VDEC_D_SC_STEP_SIZE			0x165C
#define VDEC_D_SC_CONVERGE_CTRL			0x1660
#define VDEC_D_SC_LOOP_CTRL			0x1664
#define VDEC_D_COMB_2D_HFS_CFG			0x1668
#define VDEC_D_COMB_2D_HFD_CFG			0x166C
#define VDEC_D_COMB_2D_LF_CFG			0x1670
#define VDEC_D_COMB_2D_BLEND			0x1674
#define VDEC_D_COMB_MISC_CTRL			0x1678
#define VDEC_D_COMB_FLAT_THRESH_CTRL		0x167C
#define VDEC_D_COMB_TEST			0x1680
#define VDEC_D_BP_MISC_CTRL			0x1684
#define VDEC_D_VCR_DET_CTRL			0x1688
#define VDEC_D_NOISE_DET_CTRL			0x168C
#define VDEC_D_COMB_FLAT_NOISE_CTRL		0x1690
#define VDEC_D_VERSION				0x17F8
#define VDEC_D_SOFT_RST_CTRL			0x17FC

/* Video Decoder E Registers */
#define	VDEC_E_MODE_CTRL			0x1800
#define	VDEC_E_OUT_CTRL1			0x1804
#define	VDEC_E_OUT_CTRL_NS			0x1808
#define	VDEC_E_GEN_STAT				0x180C
#define	VDEC_E_INT_STAT_MASK			0x1810
#define	VDEC_E_LUMA_CTRL			0x1814
#define	VDEC_E_CHROMA_CTRL			0x1818
#define	VDEC_E_CRUSH_CTRL			0x181C
#define	VDEC_E_HORIZ_TIM_CTRL			0x1820
#define	VDEC_E_VERT_TIM_CTRL			0x1824
#define	VDEC_E_MISC_TIM_CTRL			0x1828
#define	VDEC_E_FIELD_COUNT			0x182C
#define	VDEC_E_HSCALE_CTRL			0x1830
#define	VDEC_E_VSCALE_CTRL			0x1834
#define	VDEC_E_MAN_VGA_CTRL			0x1838
#define	VDEC_E_MAN_AGC_CTRL			0x183C
#define	VDEC_E_DFE_CTRL1			0x1840
#define	VDEC_E_DFE_CTRL2			0x1844
#define	VDEC_E_DFE_CTRL3			0x1848
#define	VDEC_E_PLL_CTRL				0x184C
#define	VDEC_E_PLL_CTRL_FAST			0x1850
#define	VDEC_E_HTL_CTRL				0x1854
#define	VDEC_E_SRC_CFG				0x1858
#define	VDEC_E_SC_STEP_SIZE			0x185C
#define	VDEC_E_SC_CONVERGE_CTRL			0x1860
#define	VDEC_E_SC_LOOP_CTRL			0x1864
#define	VDEC_E_COMB_2D_HFS_CFG			0x1868
#define	VDEC_E_COMB_2D_HFD_CFG			0x186C
#define	VDEC_E_COMB_2D_LF_CFG			0x1870
#define	VDEC_E_COMB_2D_BLEND			0x1874
#define	VDEC_E_COMB_MISC_CTRL			0x1878
#define	VDEC_E_COMB_FLAT_THRESH_CTRL		0x187C
#define	VDEC_E_COMB_TEST			0x1880
#define	VDEC_E_BP_MISC_CTRL			0x1884
#define	VDEC_E_VCR_DET_CTRL			0x1888
#define	VDEC_E_NOISE_DET_CTRL			0x188C
#define	VDEC_E_COMB_FLAT_NOISE_CTRL		0x1890
#define	VDEC_E_VERSION				0x19F8
#define	VDEC_E_SOFT_RST_CTRL			0x19FC

/* Video Decoder F Registers */
#define	VDEC_F_MODE_CTRL			0x1A00
#define	VDEC_F_OUT_CTRL1			0x1A04
#define	VDEC_F_OUT_CTRL_NS			0x1A08
#define	VDEC_F_GEN_STAT				0x1A0C
#define	VDEC_F_INT_STAT_MASK			0x1A10
#define	VDEC_F_LUMA_CTRL			0x1A14
#define	VDEC_F_CHROMA_CTRL			0x1A18
#define	VDEC_F_CRUSH_CTRL			0x1A1C
#define	VDEC_F_HORIZ_TIM_CTRL			0x1A20
#define	VDEC_F_VERT_TIM_CTRL			0x1A24
#define	VDEC_F_MISC_TIM_CTRL			0x1A28
#define	VDEC_F_FIELD_COUNT			0x1A2C
#define	VDEC_F_HSCALE_CTRL			0x1A30
#define	VDEC_F_VSCALE_CTRL			0x1A34
#define	VDEC_F_MAN_VGA_CTRL			0x1A38
#define	VDEC_F_MAN_AGC_CTRL			0x1A3C
#define	VDEC_F_DFE_CTRL1			0x1A40
#define	VDEC_F_DFE_CTRL2			0x1A44
#define	VDEC_F_DFE_CTRL3			0x1A48
#define	VDEC_F_PLL_CTRL				0x1A4C
#define	VDEC_F_PLL_CTRL_FAST			0x1A50
#define	VDEC_F_HTL_CTRL				0x1A54
#define	VDEC_F_SRC_CFG				0x1A58
#define	VDEC_F_SC_STEP_SIZE			0x1A5C
#define	VDEC_F_SC_CONVERGE_CTRL			0x1A60
#define	VDEC_F_SC_LOOP_CTRL			0x1A64
#define	VDEC_F_COMB_2D_HFS_CFG			0x1A68
#define	VDEC_F_COMB_2D_HFD_CFG			0x1A6C
#define	VDEC_F_COMB_2D_LF_CFG			0x1A70
#define	VDEC_F_COMB_2D_BLEND			0x1A74
#define	VDEC_F_COMB_MISC_CTRL			0x1A78
#define	VDEC_F_COMB_FLAT_THRESH_CTRL		0x1A7C
#define	VDEC_F_COMB_TEST			0x1A80
#define	VDEC_F_BP_MISC_CTRL			0x1A84
#define	VDEC_F_VCR_DET_CTRL			0x1A88
#define	VDEC_F_NOISE_DET_CTRL			0x1A8C
#define	VDEC_F_COMB_FLAT_NOISE_CTRL		0x1A90
#define	VDEC_F_VERSION				0x1BF8
#define	VDEC_F_SOFT_RST_CTRL			0x1BFC

/* Video Decoder G Registers */
#define	VDEC_G_MODE_CTRL			0x1C00
#define	VDEC_G_OUT_CTRL1			0x1C04
#define	VDEC_G_OUT_CTRL_NS			0x1C08
#define	VDEC_G_GEN_STAT				0x1C0C
#define	VDEC_G_INT_STAT_MASK			0x1C10
#define	VDEC_G_LUMA_CTRL			0x1C14
#define	VDEC_G_CHROMA_CTRL			0x1C18
#define	VDEC_G_CRUSH_CTRL			0x1C1C
#define	VDEC_G_HORIZ_TIM_CTRL			0x1C20
#define	VDEC_G_VERT_TIM_CTRL			0x1C24
#define	VDEC_G_MISC_TIM_CTRL			0x1C28
#define	VDEC_G_FIELD_COUNT			0x1C2C
#define	VDEC_G_HSCALE_CTRL			0x1C30
#define	VDEC_G_VSCALE_CTRL			0x1C34
#define	VDEC_G_MAN_VGA_CTRL			0x1C38
#define	VDEC_G_MAN_AGC_CTRL			0x1C3C
#define	VDEC_G_DFE_CTRL1			0x1C40
#define	VDEC_G_DFE_CTRL2			0x1C44
#define	VDEC_G_DFE_CTRL3			0x1C48
#define	VDEC_G_PLL_CTRL				0x1C4C
#define	VDEC_G_PLL_CTRL_FAST			0x1C50
#define	VDEC_G_HTL_CTRL				0x1C54
#define	VDEC_G_SRC_CFG				0x1C58
#define	VDEC_G_SC_STEP_SIZE			0x1C5C
#define	VDEC_G_SC_CONVERGE_CTRL			0x1C60
#define	VDEC_G_SC_LOOP_CTRL			0x1C64
#define	VDEC_G_COMB_2D_HFS_CFG			0x1C68
#define	VDEC_G_COMB_2D_HFD_CFG			0x1C6C
#define	VDEC_G_COMB_2D_LF_CFG			0x1C70
#define	VDEC_G_COMB_2D_BLEND			0x1C74
#define	VDEC_G_COMB_MISC_CTRL			0x1C78
#define	VDEC_G_COMB_FLAT_THRESH_CTRL		0x1C7C
#define	VDEC_G_COMB_TEST			0x1C80
#define	VDEC_G_BP_MISC_CTRL			0x1C84
#define	VDEC_G_VCR_DET_CTRL			0x1C88
#define	VDEC_G_NOISE_DET_CTRL			0x1C8C
#define	VDEC_G_COMB_FLAT_NOISE_CTRL		0x1C90
#define	VDEC_G_VERSION				0x1DF8
#define	VDEC_G_SOFT_RST_CTRL			0x1DFC

/* Video Decoder H Registers  */
#define	VDEC_H_MODE_CTRL			0x1E00
#define	VDEC_H_OUT_CTRL1			0x1E04
#define	VDEC_H_OUT_CTRL_NS			0x1E08
#define	VDEC_H_GEN_STAT				0x1E0C
#define	VDEC_H_INT_STAT_MASK			0x1E1E
#define	VDEC_H_LUMA_CTRL			0x1E14
#define	VDEC_H_CHROMA_CTRL			0x1E18
#define	VDEC_H_CRUSH_CTRL			0x1E1C
#define	VDEC_H_HORIZ_TIM_CTRL			0x1E20
#define	VDEC_H_VERT_TIM_CTRL			0x1E24
#define	VDEC_H_MISC_TIM_CTRL			0x1E28
#define	VDEC_H_FIELD_COUNT			0x1E2C
#define	VDEC_H_HSCALE_CTRL			0x1E30
#define	VDEC_H_VSCALE_CTRL			0x1E34
#define	VDEC_H_MAN_VGA_CTRL			0x1E38
#define	VDEC_H_MAN_AGC_CTRL			0x1E3C
#define	VDEC_H_DFE_CTRL1			0x1E40
#define	VDEC_H_DFE_CTRL2			0x1E44
#define	VDEC_H_DFE_CTRL3			0x1E48
#define	VDEC_H_PLL_CTRL				0x1E4C
#define	VDEC_H_PLL_CTRL_FAST			0x1E50
#define	VDEC_H_HTL_CTRL				0x1E54
#define	VDEC_H_SRC_CFG				0x1E58
#define	VDEC_H_SC_STEP_SIZE			0x1E5C
#define	VDEC_H_SC_CONVERGE_CTRL			0x1E60
#define	VDEC_H_SC_LOOP_CTRL			0x1E64
#define	VDEC_H_COMB_2D_HFS_CFG			0x1E68
#define	VDEC_H_COMB_2D_HFD_CFG			0x1E6C
#define	VDEC_H_COMB_2D_LF_CFG			0x1E70
#define	VDEC_H_COMB_2D_BLEND			0x1E74
#define	VDEC_H_COMB_MISC_CTRL			0x1E78
#define	VDEC_H_COMB_FLAT_THRESH_CTRL		0x1E7C
#define	VDEC_H_COMB_TEST			0x1E80
#define	VDEC_H_BP_MISC_CTRL			0x1E84
#define	VDEC_H_VCR_DET_CTRL			0x1E88
#define	VDEC_H_NOISE_DET_CTRL			0x1E8C
#define	VDEC_H_COMB_FLAT_NOISE_CTRL		0x1E90
#define	VDEC_H_VERSION				0x1FF8
#define	VDEC_H_SOFT_RST_CTRL			0x1FFC

/*****************************************************************************/
/* LUMA_CTRL register fields */
#define VDEC_A_BRITE_CTRL			0x1014
#define VDEC_A_CNTRST_CTRL			0x1015
#define VDEC_A_PEAK_SEL				0x1016

/*****************************************************************************/
/* CHROMA_CTRL register fields */
#define VDEC_A_USAT_CTRL			0x1018
#define VDEC_A_VSAT_CTRL			0x1019
#define VDEC_A_HUE_CTRL				0x101A

#endif
