// SPDX-License-Identifier: GPL-2.0
/*
 * R8A77951 processor support - PFC hardware block.
 *
 * Copyright (C) 2015-2019 Renesas Electronics Corporation
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sys_soc.h>

#include "sh_pfc.h"

#define CFG_FLAGS (SH_PFC_PIN_CFG_DRIVE_STRENGTH | SH_PFC_PIN_CFG_PULL_UP_DOWN)

#define CPU_ALL_GP(fn, sfx)						\
	PORT_GP_CFG_16(0, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_29(1, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_15(2, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_12(3, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE),	\
	PORT_GP_CFG_1(3, 12, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(3, 13, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(3, 14, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(3, 15, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_18(4, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE),	\
	PORT_GP_CFG_26(5, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_32(6, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_4(7, fn, sfx, CFG_FLAGS)

#define CPU_ALL_NOGP(fn)						\
	PIN_NOGP_CFG(ASEBRK, "ASEBRK", fn, CFG_FLAGS),			\
	PIN_NOGP_CFG(AVB_MDIO, "AVB_MDIO", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(AVB_RD0, "AVB_RD0", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(AVB_RD1, "AVB_RD1", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(AVB_RD2, "AVB_RD2", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(AVB_RD3, "AVB_RD3", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(AVB_RXC, "AVB_RXC", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(AVB_RX_CTL, "AVB_RX_CTL", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(AVB_TD0, "AVB_TD0", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(AVB_TD1, "AVB_TD1", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(AVB_TD2, "AVB_TD2", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(AVB_TD3, "AVB_TD3", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(AVB_TXC, "AVB_TXC", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(AVB_TXCREFCLK, "AVB_TXCREFCLK", fn, CFG_FLAGS),	\
	PIN_NOGP_CFG(AVB_TX_CTL, "AVB_TX_CTL", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(DU_DOTCLKIN0, "DU_DOTCLKIN0", fn, CFG_FLAGS),	\
	PIN_NOGP_CFG(DU_DOTCLKIN1, "DU_DOTCLKIN1", fn, CFG_FLAGS),	\
	PIN_NOGP_CFG(DU_DOTCLKIN2, "DU_DOTCLKIN2", fn, CFG_FLAGS),	\
	PIN_NOGP_CFG(DU_DOTCLKIN3, "DU_DOTCLKIN3", fn, CFG_FLAGS),	\
	PIN_NOGP_CFG(EXTALR, "EXTALR", fn, SH_PFC_PIN_CFG_PULL_UP_DOWN),\
	PIN_NOGP_CFG(FSCLKST_N, "FSCLKST#", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(MLB_REF, "MLB_REF", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(PRESETOUT_N, "PRESETOUT#", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(QSPI0_IO2, "QSPI0_IO2", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(QSPI0_IO3, "QSPI0_IO3", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(QSPI0_MISO_IO1, "QSPI0_MISO_IO1", fn, CFG_FLAGS),	\
	PIN_NOGP_CFG(QSPI0_MOSI_IO0, "QSPI0_MOSI_IO0", fn, CFG_FLAGS),	\
	PIN_NOGP_CFG(QSPI0_SPCLK, "QSPI0_SPCLK", fn, CFG_FLAGS),	\
	PIN_NOGP_CFG(QSPI0_SSL, "QSPI0_SSL", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(QSPI1_IO2, "QSPI1_IO2", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(QSPI1_IO3, "QSPI1_IO3", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(QSPI1_MISO_IO1, "QSPI1_MISO_IO1", fn, CFG_FLAGS),	\
	PIN_NOGP_CFG(QSPI1_MOSI_IO0, "QSPI1_MOSI_IO0", fn, CFG_FLAGS),	\
	PIN_NOGP_CFG(QSPI1_SPCLK, "QSPI1_SPCLK", fn, CFG_FLAGS),	\
	PIN_NOGP_CFG(QSPI1_SSL, "QSPI1_SSL", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(RPC_INT_N, "RPC_INT#", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(RPC_RESET_N, "RPC_RESET#", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(RPC_WP_N, "RPC_WP#", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(TCK, "TCK", fn, SH_PFC_PIN_CFG_PULL_UP_DOWN),	\
	PIN_NOGP_CFG(TDI, "TDI", fn, SH_PFC_PIN_CFG_PULL_UP_DOWN),	\
	PIN_NOGP_CFG(TDO, "TDO", fn, SH_PFC_PIN_CFG_DRIVE_STRENGTH),	\
	PIN_NOGP_CFG(TMS, "TMS", fn, CFG_FLAGS),			\
	PIN_NOGP_CFG(TRST_N, "TRST#", fn, SH_PFC_PIN_CFG_PULL_UP_DOWN)

/*
 * F_() : just information
 * FM() : macro for FN_xxx / xxx_MARK
 */

/* GPSR0 */
#define GPSR0_15	F_(D15,			IP7_11_8)
#define GPSR0_14	F_(D14,			IP7_7_4)
#define GPSR0_13	F_(D13,			IP7_3_0)
#define GPSR0_12	F_(D12,			IP6_31_28)
#define GPSR0_11	F_(D11,			IP6_27_24)
#define GPSR0_10	F_(D10,			IP6_23_20)
#define GPSR0_9		F_(D9,			IP6_19_16)
#define GPSR0_8		F_(D8,			IP6_15_12)
#define GPSR0_7		F_(D7,			IP6_11_8)
#define GPSR0_6		F_(D6,			IP6_7_4)
#define GPSR0_5		F_(D5,			IP6_3_0)
#define GPSR0_4		F_(D4,			IP5_31_28)
#define GPSR0_3		F_(D3,			IP5_27_24)
#define GPSR0_2		F_(D2,			IP5_23_20)
#define GPSR0_1		F_(D1,			IP5_19_16)
#define GPSR0_0		F_(D0,			IP5_15_12)

/* GPSR1 */
#define GPSR1_28	FM(CLKOUT)
#define GPSR1_27	F_(EX_WAIT0_A,		IP5_11_8)
#define GPSR1_26	F_(WE1_N,		IP5_7_4)
#define GPSR1_25	F_(WE0_N,		IP5_3_0)
#define GPSR1_24	F_(RD_WR_N,		IP4_31_28)
#define GPSR1_23	F_(RD_N,		IP4_27_24)
#define GPSR1_22	F_(BS_N,		IP4_23_20)
#define GPSR1_21	F_(CS1_N,		IP4_19_16)
#define GPSR1_20	F_(CS0_N,		IP4_15_12)
#define GPSR1_19	F_(A19,			IP4_11_8)
#define GPSR1_18	F_(A18,			IP4_7_4)
#define GPSR1_17	F_(A17,			IP4_3_0)
#define GPSR1_16	F_(A16,			IP3_31_28)
#define GPSR1_15	F_(A15,			IP3_27_24)
#define GPSR1_14	F_(A14,			IP3_23_20)
#define GPSR1_13	F_(A13,			IP3_19_16)
#define GPSR1_12	F_(A12,			IP3_15_12)
#define GPSR1_11	F_(A11,			IP3_11_8)
#define GPSR1_10	F_(A10,			IP3_7_4)
#define GPSR1_9		F_(A9,			IP3_3_0)
#define GPSR1_8		F_(A8,			IP2_31_28)
#define GPSR1_7		F_(A7,			IP2_27_24)
#define GPSR1_6		F_(A6,			IP2_23_20)
#define GPSR1_5		F_(A5,			IP2_19_16)
#define GPSR1_4		F_(A4,			IP2_15_12)
#define GPSR1_3		F_(A3,			IP2_11_8)
#define GPSR1_2		F_(A2,			IP2_7_4)
#define GPSR1_1		F_(A1,			IP2_3_0)
#define GPSR1_0		F_(A0,			IP1_31_28)

/* GPSR2 */
#define GPSR2_14	F_(AVB_AVTP_CAPTURE_A,	IP0_23_20)
#define GPSR2_13	F_(AVB_AVTP_MATCH_A,	IP0_19_16)
#define GPSR2_12	F_(AVB_LINK,		IP0_15_12)
#define GPSR2_11	F_(AVB_PHY_INT,		IP0_11_8)
#define GPSR2_10	F_(AVB_MAGIC,		IP0_7_4)
#define GPSR2_9		F_(AVB_MDC,		IP0_3_0)
#define GPSR2_8		F_(PWM2_A,		IP1_27_24)
#define GPSR2_7		F_(PWM1_A,		IP1_23_20)
#define GPSR2_6		F_(PWM0,		IP1_19_16)
#define GPSR2_5		F_(IRQ5,		IP1_15_12)
#define GPSR2_4		F_(IRQ4,		IP1_11_8)
#define GPSR2_3		F_(IRQ3,		IP1_7_4)
#define GPSR2_2		F_(IRQ2,		IP1_3_0)
#define GPSR2_1		F_(IRQ1,		IP0_31_28)
#define GPSR2_0		F_(IRQ0,		IP0_27_24)

/* GPSR3 */
#define GPSR3_15	F_(SD1_WP,		IP11_23_20)
#define GPSR3_14	F_(SD1_CD,		IP11_19_16)
#define GPSR3_13	F_(SD0_WP,		IP11_15_12)
#define GPSR3_12	F_(SD0_CD,		IP11_11_8)
#define GPSR3_11	F_(SD1_DAT3,		IP8_31_28)
#define GPSR3_10	F_(SD1_DAT2,		IP8_27_24)
#define GPSR3_9		F_(SD1_DAT1,		IP8_23_20)
#define GPSR3_8		F_(SD1_DAT0,		IP8_19_16)
#define GPSR3_7		F_(SD1_CMD,		IP8_15_12)
#define GPSR3_6		F_(SD1_CLK,		IP8_11_8)
#define GPSR3_5		F_(SD0_DAT3,		IP8_7_4)
#define GPSR3_4		F_(SD0_DAT2,		IP8_3_0)
#define GPSR3_3		F_(SD0_DAT1,		IP7_31_28)
#define GPSR3_2		F_(SD0_DAT0,		IP7_27_24)
#define GPSR3_1		F_(SD0_CMD,		IP7_23_20)
#define GPSR3_0		F_(SD0_CLK,		IP7_19_16)

/* GPSR4 */
#define GPSR4_17	F_(SD3_DS,		IP11_7_4)
#define GPSR4_16	F_(SD3_DAT7,		IP11_3_0)
#define GPSR4_15	F_(SD3_DAT6,		IP10_31_28)
#define GPSR4_14	F_(SD3_DAT5,		IP10_27_24)
#define GPSR4_13	F_(SD3_DAT4,		IP10_23_20)
#define GPSR4_12	F_(SD3_DAT3,		IP10_19_16)
#define GPSR4_11	F_(SD3_DAT2,		IP10_15_12)
#define GPSR4_10	F_(SD3_DAT1,		IP10_11_8)
#define GPSR4_9		F_(SD3_DAT0,		IP10_7_4)
#define GPSR4_8		F_(SD3_CMD,		IP10_3_0)
#define GPSR4_7		F_(SD3_CLK,		IP9_31_28)
#define GPSR4_6		F_(SD2_DS,		IP9_27_24)
#define GPSR4_5		F_(SD2_DAT3,		IP9_23_20)
#define GPSR4_4		F_(SD2_DAT2,		IP9_19_16)
#define GPSR4_3		F_(SD2_DAT1,		IP9_15_12)
#define GPSR4_2		F_(SD2_DAT0,		IP9_11_8)
#define GPSR4_1		F_(SD2_CMD,		IP9_7_4)
#define GPSR4_0		F_(SD2_CLK,		IP9_3_0)

/* GPSR5 */
#define GPSR5_25	F_(MLB_DAT,		IP14_19_16)
#define GPSR5_24	F_(MLB_SIG,		IP14_15_12)
#define GPSR5_23	F_(MLB_CLK,		IP14_11_8)
#define GPSR5_22	FM(MSIOF0_RXD)
#define GPSR5_21	F_(MSIOF0_SS2,		IP14_7_4)
#define GPSR5_20	FM(MSIOF0_TXD)
#define GPSR5_19	F_(MSIOF0_SS1,		IP14_3_0)
#define GPSR5_18	F_(MSIOF0_SYNC,		IP13_31_28)
#define GPSR5_17	FM(MSIOF0_SCK)
#define GPSR5_16	F_(HRTS0_N,		IP13_27_24)
#define GPSR5_15	F_(HCTS0_N,		IP13_23_20)
#define GPSR5_14	F_(HTX0,		IP13_19_16)
#define GPSR5_13	F_(HRX0,		IP13_15_12)
#define GPSR5_12	F_(HSCK0,		IP13_11_8)
#define GPSR5_11	F_(RX2_A,		IP13_7_4)
#define GPSR5_10	F_(TX2_A,		IP13_3_0)
#define GPSR5_9		F_(SCK2,		IP12_31_28)
#define GPSR5_8		F_(RTS1_N,		IP12_27_24)
#define GPSR5_7		F_(CTS1_N,		IP12_23_20)
#define GPSR5_6		F_(TX1_A,		IP12_19_16)
#define GPSR5_5		F_(RX1_A,		IP12_15_12)
#define GPSR5_4		F_(RTS0_N,		IP12_11_8)
#define GPSR5_3		F_(CTS0_N,		IP12_7_4)
#define GPSR5_2		F_(TX0,			IP12_3_0)
#define GPSR5_1		F_(RX0,			IP11_31_28)
#define GPSR5_0		F_(SCK0,		IP11_27_24)

/* GPSR6 */
#define GPSR6_31	F_(USB2_CH3_OVC,	IP18_7_4)
#define GPSR6_30	F_(USB2_CH3_PWEN,	IP18_3_0)
#define GPSR6_29	F_(USB30_OVC,		IP17_31_28)
#define GPSR6_28	F_(USB30_PWEN,		IP17_27_24)
#define GPSR6_27	F_(USB1_OVC,		IP17_23_20)
#define GPSR6_26	F_(USB1_PWEN,		IP17_19_16)
#define GPSR6_25	F_(USB0_OVC,		IP17_15_12)
#define GPSR6_24	F_(USB0_PWEN,		IP17_11_8)
#define GPSR6_23	F_(AUDIO_CLKB_B,	IP17_7_4)
#define GPSR6_22	F_(AUDIO_CLKA_A,	IP17_3_0)
#define GPSR6_21	F_(SSI_SDATA9_A,	IP16_31_28)
#define GPSR6_20	F_(SSI_SDATA8,		IP16_27_24)
#define GPSR6_19	F_(SSI_SDATA7,		IP16_23_20)
#define GPSR6_18	F_(SSI_WS78,		IP16_19_16)
#define GPSR6_17	F_(SSI_SCK78,		IP16_15_12)
#define GPSR6_16	F_(SSI_SDATA6,		IP16_11_8)
#define GPSR6_15	F_(SSI_WS6,		IP16_7_4)
#define GPSR6_14	F_(SSI_SCK6,		IP16_3_0)
#define GPSR6_13	FM(SSI_SDATA5)
#define GPSR6_12	FM(SSI_WS5)
#define GPSR6_11	FM(SSI_SCK5)
#define GPSR6_10	F_(SSI_SDATA4,		IP15_31_28)
#define GPSR6_9		F_(SSI_WS4,		IP15_27_24)
#define GPSR6_8		F_(SSI_SCK4,		IP15_23_20)
#define GPSR6_7		F_(SSI_SDATA3,		IP15_19_16)
#define GPSR6_6		F_(SSI_WS349,		IP15_15_12)
#define GPSR6_5		F_(SSI_SCK349,		IP15_11_8)
#define GPSR6_4		F_(SSI_SDATA2_A,	IP15_7_4)
#define GPSR6_3		F_(SSI_SDATA1_A,	IP15_3_0)
#define GPSR6_2		F_(SSI_SDATA0,		IP14_31_28)
#define GPSR6_1		F_(SSI_WS01239,		IP14_27_24)
#define GPSR6_0		F_(SSI_SCK01239,	IP14_23_20)

/* GPSR7 */
#define GPSR7_3		FM(GP7_03)
#define GPSR7_2		FM(GP7_02)
#define GPSR7_1		FM(AVS2)
#define GPSR7_0		FM(AVS1)


/* IPSRx */		/* 0 */			/* 1 */		/* 2 */			/* 3 */				/* 4 */		/* 5 */		/* 6 */			/* 7 */		/* 8 */			/* 9 */		/* A */		/* B */		/* C - F */
#define IP0_3_0		FM(AVB_MDC)		F_(0, 0)	FM(MSIOF2_SS2_C)	F_(0, 0)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0_7_4		FM(AVB_MAGIC)		F_(0, 0)	FM(MSIOF2_SS1_C)	FM(SCK4_A)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0_11_8	FM(AVB_PHY_INT)		F_(0, 0)	FM(MSIOF2_SYNC_C)	FM(RX4_A)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0_15_12	FM(AVB_LINK)		F_(0, 0)	FM(MSIOF2_SCK_C)	FM(TX4_A)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0_19_16	FM(AVB_AVTP_MATCH_A)	F_(0, 0)	FM(MSIOF2_RXD_C)	FM(CTS4_N_A)			F_(0, 0)	FM(FSCLKST2_N_A) F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0_23_20	FM(AVB_AVTP_CAPTURE_A)	F_(0, 0)	FM(MSIOF2_TXD_C)	FM(RTS4_N_A)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0_27_24	FM(IRQ0)		FM(QPOLB)	F_(0, 0)		FM(DU_CDE)			FM(VI4_DATA0_B) FM(CAN0_TX_B)	FM(CANFD0_TX_B)		FM(MSIOF3_SS2_E) F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0_31_28	FM(IRQ1)		FM(QPOLA)	F_(0, 0)		FM(DU_DISP)			FM(VI4_DATA1_B) FM(CAN0_RX_B)	FM(CANFD0_RX_B)		FM(MSIOF3_SS1_E) F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1_3_0		FM(IRQ2)		FM(QCPV_QDE)	F_(0, 0)		FM(DU_EXODDF_DU_ODDF_DISP_CDE)	FM(VI4_DATA2_B) F_(0, 0)	F_(0, 0)		FM(MSIOF3_SYNC_E) F_(0, 0)		FM(PWM3_B)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1_7_4		FM(IRQ3)		FM(QSTVB_QVE)	F_(0, 0)		FM(DU_DOTCLKOUT1)		FM(VI4_DATA3_B) F_(0, 0)	F_(0, 0)		FM(MSIOF3_SCK_E) F_(0, 0)		FM(PWM4_B)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1_11_8	FM(IRQ4)		FM(QSTH_QHS)	F_(0, 0)		FM(DU_EXHSYNC_DU_HSYNC)		FM(VI4_DATA4_B) F_(0, 0)	F_(0, 0)		FM(MSIOF3_RXD_E) F_(0, 0)		FM(PWM5_B)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1_15_12	FM(IRQ5)		FM(QSTB_QHE)	F_(0, 0)		FM(DU_EXVSYNC_DU_VSYNC)		FM(VI4_DATA5_B) FM(FSCLKST2_N_B) F_(0, 0)		FM(MSIOF3_TXD_E) F_(0, 0)		FM(PWM6_B)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1_19_16	FM(PWM0)		FM(AVB_AVTP_PPS)F_(0, 0)		F_(0, 0)			FM(VI4_DATA6_B)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		FM(IECLK_B)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1_23_20	FM(PWM1_A)		F_(0, 0)	F_(0, 0)		FM(HRX3_D)			FM(VI4_DATA7_B)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		FM(IERX_B)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1_27_24	FM(PWM2_A)		F_(0, 0)	F_(0, 0)		FM(HTX3_D)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		FM(IETX_B)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1_31_28	FM(A0)			FM(LCDOUT16)	FM(MSIOF3_SYNC_B)	F_(0, 0)			FM(VI4_DATA8)	F_(0, 0)	FM(DU_DB0)		F_(0, 0)	F_(0, 0)		FM(PWM3_A)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2_3_0		FM(A1)			FM(LCDOUT17)	FM(MSIOF3_TXD_B)	F_(0, 0)			FM(VI4_DATA9)	F_(0, 0)	FM(DU_DB1)		F_(0, 0)	F_(0, 0)		FM(PWM4_A)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2_7_4		FM(A2)			FM(LCDOUT18)	FM(MSIOF3_SCK_B)	F_(0, 0)			FM(VI4_DATA10)	F_(0, 0)	FM(DU_DB2)		F_(0, 0)	F_(0, 0)		FM(PWM5_A)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2_11_8	FM(A3)			FM(LCDOUT19)	FM(MSIOF3_RXD_B)	F_(0, 0)			FM(VI4_DATA11)	F_(0, 0)	FM(DU_DB3)		F_(0, 0)	F_(0, 0)		FM(PWM6_A)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IPSRx */		/* 0 */			/* 1 */		/* 2 */			/* 3 */				/* 4 */		/* 5 */		/* 6 */			/* 7 */		/* 8 */			/* 9 */		/* A */		/* B */		/* C - F */
#define IP2_15_12	FM(A4)			FM(LCDOUT20)	FM(MSIOF3_SS1_B)	F_(0, 0)			FM(VI4_DATA12)	FM(VI5_DATA12)	FM(DU_DB4)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2_19_16	FM(A5)			FM(LCDOUT21)	FM(MSIOF3_SS2_B)	FM(SCK4_B)			FM(VI4_DATA13)	FM(VI5_DATA13)	FM(DU_DB5)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2_23_20	FM(A6)			FM(LCDOUT22)	FM(MSIOF2_SS1_A)	FM(RX4_B)			FM(VI4_DATA14)	FM(VI5_DATA14)	FM(DU_DB6)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2_27_24	FM(A7)			FM(LCDOUT23)	FM(MSIOF2_SS2_A)	FM(TX4_B)			FM(VI4_DATA15)	FM(VI5_DATA15)	FM(DU_DB7)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2_31_28	FM(A8)			FM(RX3_B)	FM(MSIOF2_SYNC_A)	FM(HRX4_B)			F_(0, 0)	F_(0, 0)	F_(0, 0)		FM(SDA6_A)	FM(AVB_AVTP_MATCH_B)	FM(PWM1_B)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3_3_0		FM(A9)			F_(0, 0)	FM(MSIOF2_SCK_A)	FM(CTS4_N_B)			F_(0, 0)	FM(VI5_VSYNC_N)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3_7_4		FM(A10)			F_(0, 0)	FM(MSIOF2_RXD_A)	FM(RTS4_N_B)			F_(0, 0)	FM(VI5_HSYNC_N)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3_11_8	FM(A11)			FM(TX3_B)	FM(MSIOF2_TXD_A)	FM(HTX4_B)			FM(HSCK4)	FM(VI5_FIELD)	F_(0, 0)		FM(SCL6_A)	FM(AVB_AVTP_CAPTURE_B)	FM(PWM2_B)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3_15_12	FM(A12)			FM(LCDOUT12)	FM(MSIOF3_SCK_C)	F_(0, 0)			FM(HRX4_A)	FM(VI5_DATA8)	FM(DU_DG4)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3_19_16	FM(A13)			FM(LCDOUT13)	FM(MSIOF3_SYNC_C)	F_(0, 0)			FM(HTX4_A)	FM(VI5_DATA9)	FM(DU_DG5)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3_23_20	FM(A14)			FM(LCDOUT14)	FM(MSIOF3_RXD_C)	F_(0, 0)			FM(HCTS4_N)	FM(VI5_DATA10)	FM(DU_DG6)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3_27_24	FM(A15)			FM(LCDOUT15)	FM(MSIOF3_TXD_C)	F_(0, 0)			FM(HRTS4_N)	FM(VI5_DATA11)	FM(DU_DG7)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3_31_28	FM(A16)			FM(LCDOUT8)	F_(0, 0)		F_(0, 0)			FM(VI4_FIELD)	F_(0, 0)	FM(DU_DG0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP4_3_0		FM(A17)			FM(LCDOUT9)	F_(0, 0)		F_(0, 0)			FM(VI4_VSYNC_N)	F_(0, 0)	FM(DU_DG1)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP4_7_4		FM(A18)			FM(LCDOUT10)	F_(0, 0)		F_(0, 0)			FM(VI4_HSYNC_N)	F_(0, 0)	FM(DU_DG2)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP4_11_8	FM(A19)			FM(LCDOUT11)	F_(0, 0)		F_(0, 0)			FM(VI4_CLKENB)	F_(0, 0)	FM(DU_DG3)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP4_15_12	FM(CS0_N)		F_(0, 0)	F_(0, 0)		F_(0, 0)			F_(0, 0)	FM(VI5_CLKENB)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP4_19_16	FM(CS1_N)		F_(0, 0)	F_(0, 0)		F_(0, 0)			F_(0, 0)	FM(VI5_CLK)	F_(0, 0)		FM(EX_WAIT0_B)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP4_23_20	FM(BS_N)		FM(QSTVA_QVS)	FM(MSIOF3_SCK_D)	FM(SCK3)			FM(HSCK3)	F_(0, 0)	F_(0, 0)		F_(0, 0)	FM(CAN1_TX)		FM(CANFD1_TX)	FM(IETX_A)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP4_27_24	FM(RD_N)		F_(0, 0)	FM(MSIOF3_SYNC_D)	FM(RX3_A)			FM(HRX3_A)	F_(0, 0)	F_(0, 0)		F_(0, 0)	FM(CAN0_TX_A)		FM(CANFD0_TX_A)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP4_31_28	FM(RD_WR_N)		F_(0, 0)	FM(MSIOF3_RXD_D)	FM(TX3_A)			FM(HTX3_A)	F_(0, 0)	F_(0, 0)		F_(0, 0)	FM(CAN0_RX_A)		FM(CANFD0_RX_A)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP5_3_0		FM(WE0_N)		F_(0, 0)	FM(MSIOF3_TXD_D)	FM(CTS3_N)			FM(HCTS3_N)	F_(0, 0)	F_(0, 0)		FM(SCL6_B)	FM(CAN_CLK)		F_(0, 0)	FM(IECLK_A)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP5_7_4		FM(WE1_N)		F_(0, 0)	FM(MSIOF3_SS1_D)	FM(RTS3_N)			FM(HRTS3_N)	F_(0, 0)	F_(0, 0)		FM(SDA6_B)	FM(CAN1_RX)		FM(CANFD1_RX)	FM(IERX_A)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP5_11_8	FM(EX_WAIT0_A)		FM(QCLK)	F_(0, 0)		F_(0, 0)			FM(VI4_CLK)	F_(0, 0)	FM(DU_DOTCLKOUT0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP5_15_12	FM(D0)			FM(MSIOF2_SS1_B)FM(MSIOF3_SCK_A)	F_(0, 0)			FM(VI4_DATA16)	FM(VI5_DATA0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP5_19_16	FM(D1)			FM(MSIOF2_SS2_B)FM(MSIOF3_SYNC_A)	F_(0, 0)			FM(VI4_DATA17)	FM(VI5_DATA1)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP5_23_20	FM(D2)			F_(0, 0)	FM(MSIOF3_RXD_A)	F_(0, 0)			FM(VI4_DATA18)	FM(VI5_DATA2)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP5_27_24	FM(D3)			F_(0, 0)	FM(MSIOF3_TXD_A)	F_(0, 0)			FM(VI4_DATA19)	FM(VI5_DATA3)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP5_31_28	FM(D4)			FM(MSIOF2_SCK_B)F_(0, 0)		F_(0, 0)			FM(VI4_DATA20)	FM(VI5_DATA4)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP6_3_0		FM(D5)			FM(MSIOF2_SYNC_B)F_(0, 0)		F_(0, 0)			FM(VI4_DATA21)	FM(VI5_DATA5)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP6_7_4		FM(D6)			FM(MSIOF2_RXD_B)F_(0, 0)		F_(0, 0)			FM(VI4_DATA22)	FM(VI5_DATA6)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP6_11_8	FM(D7)			FM(MSIOF2_TXD_B)F_(0, 0)		F_(0, 0)			FM(VI4_DATA23)	FM(VI5_DATA7)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP6_15_12	FM(D8)			FM(LCDOUT0)	FM(MSIOF2_SCK_D)	FM(SCK4_C)			FM(VI4_DATA0_A)	F_(0, 0)	FM(DU_DR0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP6_19_16	FM(D9)			FM(LCDOUT1)	FM(MSIOF2_SYNC_D)	F_(0, 0)			FM(VI4_DATA1_A)	F_(0, 0)	FM(DU_DR1)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP6_23_20	FM(D10)			FM(LCDOUT2)	FM(MSIOF2_RXD_D)	FM(HRX3_B)			FM(VI4_DATA2_A)	FM(CTS4_N_C)	FM(DU_DR2)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP6_27_24	FM(D11)			FM(LCDOUT3)	FM(MSIOF2_TXD_D)	FM(HTX3_B)			FM(VI4_DATA3_A)	FM(RTS4_N_C)	FM(DU_DR3)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP6_31_28	FM(D12)			FM(LCDOUT4)	FM(MSIOF2_SS1_D)	FM(RX4_C)			FM(VI4_DATA4_A)	F_(0, 0)	FM(DU_DR4)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP7_3_0		FM(D13)			FM(LCDOUT5)	FM(MSIOF2_SS2_D)	FM(TX4_C)			FM(VI4_DATA5_A)	F_(0, 0)	FM(DU_DR5)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP7_7_4		FM(D14)			FM(LCDOUT6)	FM(MSIOF3_SS1_A)	FM(HRX3_C)			FM(VI4_DATA6_A)	F_(0, 0)	FM(DU_DR6)		FM(SCL6_C)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP7_11_8	FM(D15)			FM(LCDOUT7)	FM(MSIOF3_SS2_A)	FM(HTX3_C)			FM(VI4_DATA7_A)	F_(0, 0)	FM(DU_DR7)		FM(SDA6_C)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP7_19_16	FM(SD0_CLK)		F_(0, 0)	FM(MSIOF1_SCK_E)	F_(0, 0)			F_(0, 0)	F_(0, 0)	FM(STP_OPWM_0_B)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IPSRx */		/* 0 */			/* 1 */		/* 2 */			/* 3 */				/* 4 */		/* 5 */		/* 6 */			/* 7 */		/* 8 */			/* 9 */		/* A */		/* B */		/* C - F */
#define IP7_23_20	FM(SD0_CMD)		F_(0, 0)	FM(MSIOF1_SYNC_E)	F_(0, 0)			F_(0, 0)	F_(0, 0)	FM(STP_IVCXO27_0_B)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP7_27_24	FM(SD0_DAT0)		F_(0, 0)	FM(MSIOF1_RXD_E)	F_(0, 0)			F_(0, 0)	FM(TS_SCK0_B)	FM(STP_ISCLK_0_B)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP7_31_28	FM(SD0_DAT1)		F_(0, 0)	FM(MSIOF1_TXD_E)	F_(0, 0)			F_(0, 0)	FM(TS_SPSYNC0_B)FM(STP_ISSYNC_0_B)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP8_3_0		FM(SD0_DAT2)		F_(0, 0)	FM(MSIOF1_SS1_E)	F_(0, 0)			F_(0, 0)	FM(TS_SDAT0_B)	FM(STP_ISD_0_B)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP8_7_4		FM(SD0_DAT3)		F_(0, 0)	FM(MSIOF1_SS2_E)	F_(0, 0)			F_(0, 0)	FM(TS_SDEN0_B)	FM(STP_ISEN_0_B)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP8_11_8	FM(SD1_CLK)		F_(0, 0)	FM(MSIOF1_SCK_G)	F_(0, 0)			F_(0, 0)	FM(SIM0_CLK_A)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP8_15_12	FM(SD1_CMD)		F_(0, 0)	FM(MSIOF1_SYNC_G)	FM(NFCE_N_B)			F_(0, 0)	FM(SIM0_D_A)	FM(STP_IVCXO27_1_B)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP8_19_16	FM(SD1_DAT0)		FM(SD2_DAT4)	FM(MSIOF1_RXD_G)	FM(NFWP_N_B)			F_(0, 0)	FM(TS_SCK1_B)	FM(STP_ISCLK_1_B)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP8_23_20	FM(SD1_DAT1)		FM(SD2_DAT5)	FM(MSIOF1_TXD_G)	FM(NFDATA14_B)			F_(0, 0)	FM(TS_SPSYNC1_B)FM(STP_ISSYNC_1_B)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP8_27_24	FM(SD1_DAT2)		FM(SD2_DAT6)	FM(MSIOF1_SS1_G)	FM(NFDATA15_B)			F_(0, 0)	FM(TS_SDAT1_B)	FM(STP_ISD_1_B)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP8_31_28	FM(SD1_DAT3)		FM(SD2_DAT7)	FM(MSIOF1_SS2_G)	FM(NFRB_N_B)			F_(0, 0)	FM(TS_SDEN1_B)	FM(STP_ISEN_1_B)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP9_3_0		FM(SD2_CLK)		F_(0, 0)	FM(NFDATA8)		F_(0, 0)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP9_7_4		FM(SD2_CMD)		F_(0, 0)	FM(NFDATA9)		F_(0, 0)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP9_11_8	FM(SD2_DAT0)		F_(0, 0)	FM(NFDATA10)		F_(0, 0)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP9_15_12	FM(SD2_DAT1)		F_(0, 0)	FM(NFDATA11)		F_(0, 0)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP9_19_16	FM(SD2_DAT2)		F_(0, 0)	FM(NFDATA12)		F_(0, 0)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP9_23_20	FM(SD2_DAT3)		F_(0, 0)	FM(NFDATA13)		F_(0, 0)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP9_27_24	FM(SD2_DS)		F_(0, 0)	FM(NFALE)		F_(0, 0)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	FM(SATA_DEVSLP_B)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP9_31_28	FM(SD3_CLK)		F_(0, 0)	FM(NFWE_N)		F_(0, 0)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP10_3_0	FM(SD3_CMD)		F_(0, 0)	FM(NFRE_N)		F_(0, 0)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP10_7_4	FM(SD3_DAT0)		F_(0, 0)	FM(NFDATA0)		F_(0, 0)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP10_11_8	FM(SD3_DAT1)		F_(0, 0)	FM(NFDATA1)		F_(0, 0)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP10_15_12	FM(SD3_DAT2)		F_(0, 0)	FM(NFDATA2)		F_(0, 0)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP10_19_16	FM(SD3_DAT3)		F_(0, 0)	FM(NFDATA3)		F_(0, 0)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP10_23_20	FM(SD3_DAT4)		FM(SD2_CD_A)	FM(NFDATA4)		F_(0, 0)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP10_27_24	FM(SD3_DAT5)		FM(SD2_WP_A)	FM(NFDATA5)		F_(0, 0)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP10_31_28	FM(SD3_DAT6)		FM(SD3_CD)	FM(NFDATA6)		F_(0, 0)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP11_3_0	FM(SD3_DAT7)		FM(SD3_WP)	FM(NFDATA7)		F_(0, 0)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP11_7_4	FM(SD3_DS)		F_(0, 0)	FM(NFCLE)		F_(0, 0)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP11_11_8	FM(SD0_CD)		F_(0, 0)	FM(NFDATA14_A)		F_(0, 0)			FM(SCL2_B)	FM(SIM0_RST_A)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IPSRx */		/* 0 */			/* 1 */		/* 2 */			/* 3 */				/* 4 */		/* 5 */		/* 6 */			/* 7 */		/* 8 */			/* 9 */		/* A */		/* B */		/* C - F */
#define IP11_15_12	FM(SD0_WP)		F_(0, 0)	FM(NFDATA15_A)		F_(0, 0)			FM(SDA2_B)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP11_19_16	FM(SD1_CD)		F_(0, 0)	FM(NFRB_N_A)		F_(0, 0)			F_(0, 0)	FM(SIM0_CLK_B)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP11_23_20	FM(SD1_WP)		F_(0, 0)	FM(NFCE_N_A)		F_(0, 0)			F_(0, 0)	FM(SIM0_D_B)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP11_27_24	FM(SCK0)		FM(HSCK1_B)	FM(MSIOF1_SS2_B)	FM(AUDIO_CLKC_B)		FM(SDA2_A)	FM(SIM0_RST_B)	FM(STP_OPWM_0_C)	FM(RIF0_CLK_B)	F_(0, 0)		FM(ADICHS2)	FM(SCK5_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP11_31_28	FM(RX0)			FM(HRX1_B)	F_(0, 0)		F_(0, 0)			F_(0, 0)	FM(TS_SCK0_C)	FM(STP_ISCLK_0_C)	FM(RIF0_D0_B)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP12_3_0	FM(TX0)			FM(HTX1_B)	F_(0, 0)		F_(0, 0)			F_(0, 0)	FM(TS_SPSYNC0_C)FM(STP_ISSYNC_0_C)	FM(RIF0_D1_B)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP12_7_4	FM(CTS0_N)		FM(HCTS1_N_B)	FM(MSIOF1_SYNC_B)	F_(0, 0)			F_(0, 0)	FM(TS_SPSYNC1_C)FM(STP_ISSYNC_1_C)	FM(RIF1_SYNC_B)	FM(AUDIO_CLKOUT_C)	FM(ADICS_SAMP)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP12_11_8	FM(RTS0_N)		FM(HRTS1_N_B)	FM(MSIOF1_SS1_B)	FM(AUDIO_CLKA_B)		FM(SCL2_A)	F_(0, 0)	FM(STP_IVCXO27_1_C)	FM(RIF0_SYNC_B)	F_(0, 0)		FM(ADICHS1)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP12_15_12	FM(RX1_A)		FM(HRX1_A)	F_(0, 0)		F_(0, 0)			F_(0, 0)	FM(TS_SDAT0_C)	FM(STP_ISD_0_C)		FM(RIF1_CLK_C)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP12_19_16	FM(TX1_A)		FM(HTX1_A)	F_(0, 0)		F_(0, 0)			F_(0, 0)	FM(TS_SDEN0_C)	FM(STP_ISEN_0_C)	FM(RIF1_D0_C)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP12_23_20	FM(CTS1_N)		FM(HCTS1_N_A)	FM(MSIOF1_RXD_B)	F_(0, 0)			F_(0, 0)	FM(TS_SDEN1_C)	FM(STP_ISEN_1_C)	FM(RIF1_D0_B)	F_(0, 0)		FM(ADIDATA)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP12_27_24	FM(RTS1_N)		FM(HRTS1_N_A)	FM(MSIOF1_TXD_B)	F_(0, 0)			F_(0, 0)	FM(TS_SDAT1_C)	FM(STP_ISD_1_C)		FM(RIF1_D1_B)	F_(0, 0)		FM(ADICHS0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP12_31_28	FM(SCK2)		FM(SCIF_CLK_B)	FM(MSIOF1_SCK_B)	F_(0, 0)			F_(0, 0)	FM(TS_SCK1_C)	FM(STP_ISCLK_1_C)	FM(RIF1_CLK_B)	F_(0, 0)		FM(ADICLK)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP13_3_0	FM(TX2_A)		F_(0, 0)	F_(0, 0)		FM(SD2_CD_B)			FM(SCL1_A)	F_(0, 0)	FM(FMCLK_A)		FM(RIF1_D1_C)	F_(0, 0)		FM(FSO_CFE_0_N)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP13_7_4	FM(RX2_A)		F_(0, 0)	F_(0, 0)		FM(SD2_WP_B)			FM(SDA1_A)	F_(0, 0)	FM(FMIN_A)		FM(RIF1_SYNC_C)	F_(0, 0)		FM(FSO_CFE_1_N)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP13_11_8	FM(HSCK0)		F_(0, 0)	FM(MSIOF1_SCK_D)	FM(AUDIO_CLKB_A)		FM(SSI_SDATA1_B)FM(TS_SCK0_D)	FM(STP_ISCLK_0_D)	FM(RIF0_CLK_C)	F_(0, 0)		F_(0, 0)	FM(RX5_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP13_15_12	FM(HRX0)		F_(0, 0)	FM(MSIOF1_RXD_D)	F_(0, 0)			FM(SSI_SDATA2_B)FM(TS_SDEN0_D)	FM(STP_ISEN_0_D)	FM(RIF0_D0_C)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP13_19_16	FM(HTX0)		F_(0, 0)	FM(MSIOF1_TXD_D)	F_(0, 0)			FM(SSI_SDATA9_B)FM(TS_SDAT0_D)	FM(STP_ISD_0_D)		FM(RIF0_D1_C)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP13_23_20	FM(HCTS0_N)		FM(RX2_B)	FM(MSIOF1_SYNC_D)	F_(0, 0)			FM(SSI_SCK9_A)	FM(TS_SPSYNC0_D)FM(STP_ISSYNC_0_D)	FM(RIF0_SYNC_C)	FM(AUDIO_CLKOUT1_A)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP13_27_24	FM(HRTS0_N)		FM(TX2_B)	FM(MSIOF1_SS1_D)	F_(0, 0)			FM(SSI_WS9_A)	F_(0, 0)	FM(STP_IVCXO27_0_D)	FM(BPFCLK_A)	FM(AUDIO_CLKOUT2_A)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP13_31_28	FM(MSIOF0_SYNC)		F_(0, 0)	F_(0, 0)		F_(0, 0)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	FM(AUDIO_CLKOUT_A)	F_(0, 0)	FM(TX5_B)	F_(0, 0)	F_(0, 0) FM(BPFCLK_D) F_(0, 0) F_(0, 0)
#define IP14_3_0	FM(MSIOF0_SS1)		FM(RX5_A)	FM(NFWP_N_A)		FM(AUDIO_CLKA_C)		FM(SSI_SCK2_A)	F_(0, 0)	FM(STP_IVCXO27_0_C)	F_(0, 0)	FM(AUDIO_CLKOUT3_A)	F_(0, 0)	FM(TCLK1_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP14_7_4	FM(MSIOF0_SS2)		FM(TX5_A)	FM(MSIOF1_SS2_D)	FM(AUDIO_CLKC_A)		FM(SSI_WS2_A)	F_(0, 0)	FM(STP_OPWM_0_D)	F_(0, 0)	FM(AUDIO_CLKOUT_D)	F_(0, 0)	FM(SPEEDIN_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP14_11_8	FM(MLB_CLK)		F_(0, 0)	FM(MSIOF1_SCK_F)	F_(0, 0)			FM(SCL1_B)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP14_15_12	FM(MLB_SIG)		FM(RX1_B)	FM(MSIOF1_SYNC_F)	F_(0, 0)			FM(SDA1_B)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP14_19_16	FM(MLB_DAT)		FM(TX1_B)	FM(MSIOF1_RXD_F)	F_(0, 0)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP14_23_20	FM(SSI_SCK01239)	F_(0, 0)	FM(MSIOF1_TXD_F)	F_(0, 0)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP14_27_24	FM(SSI_WS01239)		F_(0, 0)	FM(MSIOF1_SS1_F)	F_(0, 0)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IPSRx */		/* 0 */			/* 1 */		/* 2 */			/* 3 */				/* 4 */		/* 5 */		/* 6 */			/* 7 */		/* 8 */			/* 9 */		/* A */		/* B */		/* C - F */
#define IP14_31_28	FM(SSI_SDATA0)		F_(0, 0)	FM(MSIOF1_SS2_F)	F_(0, 0)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP15_3_0	FM(SSI_SDATA1_A)	F_(0, 0)	F_(0, 0)		F_(0, 0)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP15_7_4	FM(SSI_SDATA2_A)	F_(0, 0)	F_(0, 0)		F_(0, 0)			FM(SSI_SCK1_B)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP15_11_8	FM(SSI_SCK349)		F_(0, 0)	FM(MSIOF1_SS1_A)	F_(0, 0)			F_(0, 0)	F_(0, 0)	FM(STP_OPWM_0_A)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP15_15_12	FM(SSI_WS349)		FM(HCTS2_N_A)	FM(MSIOF1_SS2_A)	F_(0, 0)			F_(0, 0)	F_(0, 0)	FM(STP_IVCXO27_0_A)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP15_19_16	FM(SSI_SDATA3)		FM(HRTS2_N_A)	FM(MSIOF1_TXD_A)	F_(0, 0)			F_(0, 0)	FM(TS_SCK0_A)	FM(STP_ISCLK_0_A)	FM(RIF0_D1_A)	FM(RIF2_D0_A)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP15_23_20	FM(SSI_SCK4)		FM(HRX2_A)	FM(MSIOF1_SCK_A)	F_(0, 0)			F_(0, 0)	FM(TS_SDAT0_A)	FM(STP_ISD_0_A)		FM(RIF0_CLK_A)	FM(RIF2_CLK_A)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP15_27_24	FM(SSI_WS4)		FM(HTX2_A)	FM(MSIOF1_SYNC_A)	F_(0, 0)			F_(0, 0)	FM(TS_SDEN0_A)	FM(STP_ISEN_0_A)	FM(RIF0_SYNC_A)	FM(RIF2_SYNC_A)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP15_31_28	FM(SSI_SDATA4)		FM(HSCK2_A)	FM(MSIOF1_RXD_A)	F_(0, 0)			F_(0, 0)	FM(TS_SPSYNC0_A)FM(STP_ISSYNC_0_A)	FM(RIF0_D0_A)	FM(RIF2_D1_A)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP16_3_0	FM(SSI_SCK6)		FM(USB2_PWEN)	F_(0, 0)		FM(SIM0_RST_D)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP16_7_4	FM(SSI_WS6)		FM(USB2_OVC)	F_(0, 0)		FM(SIM0_D_D)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP16_11_8	FM(SSI_SDATA6)		F_(0, 0)	F_(0, 0)		FM(SIM0_CLK_D)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	FM(SATA_DEVSLP_A)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP16_15_12	FM(SSI_SCK78)		FM(HRX2_B)	FM(MSIOF1_SCK_C)	F_(0, 0)			F_(0, 0)	FM(TS_SCK1_A)	FM(STP_ISCLK_1_A)	FM(RIF1_CLK_A)	FM(RIF3_CLK_A)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP16_19_16	FM(SSI_WS78)		FM(HTX2_B)	FM(MSIOF1_SYNC_C)	F_(0, 0)			F_(0, 0)	FM(TS_SDAT1_A)	FM(STP_ISD_1_A)		FM(RIF1_SYNC_A)	FM(RIF3_SYNC_A)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP16_23_20	FM(SSI_SDATA7)		FM(HCTS2_N_B)	FM(MSIOF1_RXD_C)	F_(0, 0)			F_(0, 0)	FM(TS_SDEN1_A)	FM(STP_ISEN_1_A)	FM(RIF1_D0_A)	FM(RIF3_D0_A)		F_(0, 0)	FM(TCLK2_A)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP16_27_24	FM(SSI_SDATA8)		FM(HRTS2_N_B)	FM(MSIOF1_TXD_C)	F_(0, 0)			F_(0, 0)	FM(TS_SPSYNC1_A)FM(STP_ISSYNC_1_A)	FM(RIF1_D1_A)	FM(RIF3_D1_A)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP16_31_28	FM(SSI_SDATA9_A)	FM(HSCK2_B)	FM(MSIOF1_SS1_C)	FM(HSCK1_A)			FM(SSI_WS1_B)	FM(SCK1)	FM(STP_IVCXO27_1_A)	FM(SCK5_A)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP17_3_0	FM(AUDIO_CLKA_A)	F_(0, 0)	F_(0, 0)		F_(0, 0)			F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP17_7_4	FM(AUDIO_CLKB_B)	FM(SCIF_CLK_A)	F_(0, 0)		F_(0, 0)			F_(0, 0)	F_(0, 0)	FM(STP_IVCXO27_1_D)	FM(REMOCON_A)	F_(0, 0)		F_(0, 0)	FM(TCLK1_A)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP17_11_8	FM(USB0_PWEN)		F_(0, 0)	F_(0, 0)		FM(SIM0_RST_C)			F_(0, 0)	FM(TS_SCK1_D)	FM(STP_ISCLK_1_D)	FM(BPFCLK_B)	FM(RIF3_CLK_B)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) FM(HSCK2_C) F_(0, 0) F_(0, 0)
#define IP17_15_12	FM(USB0_OVC)		F_(0, 0)	F_(0, 0)		FM(SIM0_D_C)			F_(0, 0)	FM(TS_SDAT1_D)	FM(STP_ISD_1_D)		F_(0, 0)	FM(RIF3_SYNC_B)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) FM(HRX2_C) F_(0, 0) F_(0, 0)
#define IP17_19_16	FM(USB1_PWEN)		F_(0, 0)	F_(0, 0)		FM(SIM0_CLK_C)			FM(SSI_SCK1_A)	FM(TS_SCK0_E)	FM(STP_ISCLK_0_E)	FM(FMCLK_B)	FM(RIF2_CLK_B)		F_(0, 0)	FM(SPEEDIN_A)	F_(0, 0)	F_(0, 0) FM(HTX2_C) F_(0, 0) F_(0, 0)
#define IP17_23_20	FM(USB1_OVC)		F_(0, 0)	FM(MSIOF1_SS2_C)	F_(0, 0)			FM(SSI_WS1_A)	FM(TS_SDAT0_E)	FM(STP_ISD_0_E)		FM(FMIN_B)	FM(RIF2_SYNC_B)		F_(0, 0)	FM(REMOCON_B)	F_(0, 0)	F_(0, 0) FM(HCTS2_N_C) F_(0, 0) F_(0, 0)
#define IP17_27_24	FM(USB30_PWEN)		F_(0, 0)	F_(0, 0)		FM(AUDIO_CLKOUT_B)		FM(SSI_SCK2_B)	FM(TS_SDEN1_D)	FM(STP_ISEN_1_D)	FM(STP_OPWM_0_E)FM(RIF3_D0_B)		F_(0, 0)	FM(TCLK2_B)	FM(TPU0TO0)	FM(BPFCLK_C) FM(HRTS2_N_C) F_(0, 0) F_(0, 0)
#define IP17_31_28	FM(USB30_OVC)		F_(0, 0)	F_(0, 0)		FM(AUDIO_CLKOUT1_B)		FM(SSI_WS2_B)	FM(TS_SPSYNC1_D)FM(STP_ISSYNC_1_D)	FM(STP_IVCXO27_0_E)FM(RIF3_D1_B)	F_(0, 0)	FM(FSO_TOE_N)	FM(TPU0TO1)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP18_3_0	FM(USB2_CH3_PWEN)	F_(0, 0)	F_(0, 0)		FM(AUDIO_CLKOUT2_B)		FM(SSI_SCK9_B)	FM(TS_SDEN0_E)	FM(STP_ISEN_0_E)	F_(0, 0)	FM(RIF2_D0_B)		F_(0, 0)	F_(0, 0)	FM(TPU0TO2)	FM(FMCLK_C) FM(FMCLK_D) F_(0, 0) F_(0, 0)
#define IP18_7_4	FM(USB2_CH3_OVC)	F_(0, 0)	F_(0, 0)		FM(AUDIO_CLKOUT3_B)		FM(SSI_WS9_B)	FM(TS_SPSYNC0_E)FM(STP_ISSYNC_0_E)	F_(0, 0)	FM(RIF2_D1_B)		F_(0, 0)	F_(0, 0)	FM(TPU0TO3)	FM(FMIN_C) FM(FMIN_D) F_(0, 0) F_(0, 0)

#define PINMUX_GPSR	\
\
												GPSR6_31 \
												GPSR6_30 \
												GPSR6_29 \
		GPSR1_28									GPSR6_28 \
		GPSR1_27									GPSR6_27 \
		GPSR1_26									GPSR6_26 \
		GPSR1_25							GPSR5_25	GPSR6_25 \
		GPSR1_24							GPSR5_24	GPSR6_24 \
		GPSR1_23							GPSR5_23	GPSR6_23 \
		GPSR1_22							GPSR5_22	GPSR6_22 \
		GPSR1_21							GPSR5_21	GPSR6_21 \
		GPSR1_20							GPSR5_20	GPSR6_20 \
		GPSR1_19							GPSR5_19	GPSR6_19 \
		GPSR1_18							GPSR5_18	GPSR6_18 \
		GPSR1_17					GPSR4_17	GPSR5_17	GPSR6_17 \
		GPSR1_16					GPSR4_16	GPSR5_16	GPSR6_16 \
GPSR0_15	GPSR1_15			GPSR3_15	GPSR4_15	GPSR5_15	GPSR6_15 \
GPSR0_14	GPSR1_14	GPSR2_14	GPSR3_14	GPSR4_14	GPSR5_14	GPSR6_14 \
GPSR0_13	GPSR1_13	GPSR2_13	GPSR3_13	GPSR4_13	GPSR5_13	GPSR6_13 \
GPSR0_12	GPSR1_12	GPSR2_12	GPSR3_12	GPSR4_12	GPSR5_12	GPSR6_12 \
GPSR0_11	GPSR1_11	GPSR2_11	GPSR3_11	GPSR4_11	GPSR5_11	GPSR6_11 \
GPSR0_10	GPSR1_10	GPSR2_10	GPSR3_10	GPSR4_10	GPSR5_10	GPSR6_10 \
GPSR0_9		GPSR1_9		GPSR2_9		GPSR3_9		GPSR4_9		GPSR5_9		GPSR6_9 \
GPSR0_8		GPSR1_8		GPSR2_8		GPSR3_8		GPSR4_8		GPSR5_8		GPSR6_8 \
GPSR0_7		GPSR1_7		GPSR2_7		GPSR3_7		GPSR4_7		GPSR5_7		GPSR6_7 \
GPSR0_6		GPSR1_6		GPSR2_6		GPSR3_6		GPSR4_6		GPSR5_6		GPSR6_6 \
GPSR0_5		GPSR1_5		GPSR2_5		GPSR3_5		GPSR4_5		GPSR5_5		GPSR6_5 \
GPSR0_4		GPSR1_4		GPSR2_4		GPSR3_4		GPSR4_4		GPSR5_4		GPSR6_4 \
GPSR0_3		GPSR1_3		GPSR2_3		GPSR3_3		GPSR4_3		GPSR5_3		GPSR6_3		GPSR7_3 \
GPSR0_2		GPSR1_2		GPSR2_2		GPSR3_2		GPSR4_2		GPSR5_2		GPSR6_2		GPSR7_2 \
GPSR0_1		GPSR1_1		GPSR2_1		GPSR3_1		GPSR4_1		GPSR5_1		GPSR6_1		GPSR7_1 \
GPSR0_0		GPSR1_0		GPSR2_0		GPSR3_0		GPSR4_0		GPSR5_0		GPSR6_0		GPSR7_0

#define PINMUX_IPSR				\
\
FM(IP0_3_0)	IP0_3_0		FM(IP1_3_0)	IP1_3_0		FM(IP2_3_0)	IP2_3_0		FM(IP3_3_0)	IP3_3_0 \
FM(IP0_7_4)	IP0_7_4		FM(IP1_7_4)	IP1_7_4		FM(IP2_7_4)	IP2_7_4		FM(IP3_7_4)	IP3_7_4 \
FM(IP0_11_8)	IP0_11_8	FM(IP1_11_8)	IP1_11_8	FM(IP2_11_8)	IP2_11_8	FM(IP3_11_8)	IP3_11_8 \
FM(IP0_15_12)	IP0_15_12	FM(IP1_15_12)	IP1_15_12	FM(IP2_15_12)	IP2_15_12	FM(IP3_15_12)	IP3_15_12 \
FM(IP0_19_16)	IP0_19_16	FM(IP1_19_16)	IP1_19_16	FM(IP2_19_16)	IP2_19_16	FM(IP3_19_16)	IP3_19_16 \
FM(IP0_23_20)	IP0_23_20	FM(IP1_23_20)	IP1_23_20	FM(IP2_23_20)	IP2_23_20	FM(IP3_23_20)	IP3_23_20 \
FM(IP0_27_24)	IP0_27_24	FM(IP1_27_24)	IP1_27_24	FM(IP2_27_24)	IP2_27_24	FM(IP3_27_24)	IP3_27_24 \
FM(IP0_31_28)	IP0_31_28	FM(IP1_31_28)	IP1_31_28	FM(IP2_31_28)	IP2_31_28	FM(IP3_31_28)	IP3_31_28 \
\
FM(IP4_3_0)	IP4_3_0		FM(IP5_3_0)	IP5_3_0		FM(IP6_3_0)	IP6_3_0		FM(IP7_3_0)	IP7_3_0 \
FM(IP4_7_4)	IP4_7_4		FM(IP5_7_4)	IP5_7_4		FM(IP6_7_4)	IP6_7_4		FM(IP7_7_4)	IP7_7_4 \
FM(IP4_11_8)	IP4_11_8	FM(IP5_11_8)	IP5_11_8	FM(IP6_11_8)	IP6_11_8	FM(IP7_11_8)	IP7_11_8 \
FM(IP4_15_12)	IP4_15_12	FM(IP5_15_12)	IP5_15_12	FM(IP6_15_12)	IP6_15_12 \
FM(IP4_19_16)	IP4_19_16	FM(IP5_19_16)	IP5_19_16	FM(IP6_19_16)	IP6_19_16	FM(IP7_19_16)	IP7_19_16 \
FM(IP4_23_20)	IP4_23_20	FM(IP5_23_20)	IP5_23_20	FM(IP6_23_20)	IP6_23_20	FM(IP7_23_20)	IP7_23_20 \
FM(IP4_27_24)	IP4_27_24	FM(IP5_27_24)	IP5_27_24	FM(IP6_27_24)	IP6_27_24	FM(IP7_27_24)	IP7_27_24 \
FM(IP4_31_28)	IP4_31_28	FM(IP5_31_28)	IP5_31_28	FM(IP6_31_28)	IP6_31_28	FM(IP7_31_28)	IP7_31_28 \
\
FM(IP8_3_0)	IP8_3_0		FM(IP9_3_0)	IP9_3_0		FM(IP10_3_0)	IP10_3_0	FM(IP11_3_0)	IP11_3_0 \
FM(IP8_7_4)	IP8_7_4		FM(IP9_7_4)	IP9_7_4		FM(IP10_7_4)	IP10_7_4	FM(IP11_7_4)	IP11_7_4 \
FM(IP8_11_8)	IP8_11_8	FM(IP9_11_8)	IP9_11_8	FM(IP10_11_8)	IP10_11_8	FM(IP11_11_8)	IP11_11_8 \
FM(IP8_15_12)	IP8_15_12	FM(IP9_15_12)	IP9_15_12	FM(IP10_15_12)	IP10_15_12	FM(IP11_15_12)	IP11_15_12 \
FM(IP8_19_16)	IP8_19_16	FM(IP9_19_16)	IP9_19_16	FM(IP10_19_16)	IP10_19_16	FM(IP11_19_16)	IP11_19_16 \
FM(IP8_23_20)	IP8_23_20	FM(IP9_23_20)	IP9_23_20	FM(IP10_23_20)	IP10_23_20	FM(IP11_23_20)	IP11_23_20 \
FM(IP8_27_24)	IP8_27_24	FM(IP9_27_24)	IP9_27_24	FM(IP10_27_24)	IP10_27_24	FM(IP11_27_24)	IP11_27_24 \
FM(IP8_31_28)	IP8_31_28	FM(IP9_31_28)	IP9_31_28	FM(IP10_31_28)	IP10_31_28	FM(IP11_31_28)	IP11_31_28 \
\
FM(IP12_3_0)	IP12_3_0	FM(IP13_3_0)	IP13_3_0	FM(IP14_3_0)	IP14_3_0	FM(IP15_3_0)	IP15_3_0 \
FM(IP12_7_4)	IP12_7_4	FM(IP13_7_4)	IP13_7_4	FM(IP14_7_4)	IP14_7_4	FM(IP15_7_4)	IP15_7_4 \
FM(IP12_11_8)	IP12_11_8	FM(IP13_11_8)	IP13_11_8	FM(IP14_11_8)	IP14_11_8	FM(IP15_11_8)	IP15_11_8 \
FM(IP12_15_12)	IP12_15_12	FM(IP13_15_12)	IP13_15_12	FM(IP14_15_12)	IP14_15_12	FM(IP15_15_12)	IP15_15_12 \
FM(IP12_19_16)	IP12_19_16	FM(IP13_19_16)	IP13_19_16	FM(IP14_19_16)	IP14_19_16	FM(IP15_19_16)	IP15_19_16 \
FM(IP12_23_20)	IP12_23_20	FM(IP13_23_20)	IP13_23_20	FM(IP14_23_20)	IP14_23_20	FM(IP15_23_20)	IP15_23_20 \
FM(IP12_27_24)	IP12_27_24	FM(IP13_27_24)	IP13_27_24	FM(IP14_27_24)	IP14_27_24	FM(IP15_27_24)	IP15_27_24 \
FM(IP12_31_28)	IP12_31_28	FM(IP13_31_28)	IP13_31_28	FM(IP14_31_28)	IP14_31_28	FM(IP15_31_28)	IP15_31_28 \
\
FM(IP16_3_0)	IP16_3_0	FM(IP17_3_0)	IP17_3_0	FM(IP18_3_0)	IP18_3_0 \
FM(IP16_7_4)	IP16_7_4	FM(IP17_7_4)	IP17_7_4	FM(IP18_7_4)	IP18_7_4 \
FM(IP16_11_8)	IP16_11_8	FM(IP17_11_8)	IP17_11_8 \
FM(IP16_15_12)	IP16_15_12	FM(IP17_15_12)	IP17_15_12 \
FM(IP16_19_16)	IP16_19_16	FM(IP17_19_16)	IP17_19_16 \
FM(IP16_23_20)	IP16_23_20	FM(IP17_23_20)	IP17_23_20 \
FM(IP16_27_24)	IP16_27_24	FM(IP17_27_24)	IP17_27_24 \
FM(IP16_31_28)	IP16_31_28	FM(IP17_31_28)	IP17_31_28

/* MOD_SEL0 */			/* 0 */			/* 1 */			/* 2 */			/* 3 */			/* 4 */			/* 5 */			/* 6 */			/* 7 */
#define MOD_SEL0_31_30_29	FM(SEL_MSIOF3_0)	FM(SEL_MSIOF3_1)	FM(SEL_MSIOF3_2)	FM(SEL_MSIOF3_3)	FM(SEL_MSIOF3_4)	F_(0, 0)		F_(0, 0)		F_(0, 0)
#define MOD_SEL0_28_27		FM(SEL_MSIOF2_0)	FM(SEL_MSIOF2_1)	FM(SEL_MSIOF2_2)	FM(SEL_MSIOF2_3)
#define MOD_SEL0_26_25_24	FM(SEL_MSIOF1_0)	FM(SEL_MSIOF1_1)	FM(SEL_MSIOF1_2)	FM(SEL_MSIOF1_3)	FM(SEL_MSIOF1_4)	FM(SEL_MSIOF1_5)	FM(SEL_MSIOF1_6)	F_(0, 0)
#define MOD_SEL0_23		FM(SEL_LBSC_0)		FM(SEL_LBSC_1)
#define MOD_SEL0_22		FM(SEL_IEBUS_0)		FM(SEL_IEBUS_1)
#define MOD_SEL0_21		FM(SEL_I2C2_0)		FM(SEL_I2C2_1)
#define MOD_SEL0_20		FM(SEL_I2C1_0)		FM(SEL_I2C1_1)
#define MOD_SEL0_19		FM(SEL_HSCIF4_0)	FM(SEL_HSCIF4_1)
#define MOD_SEL0_18_17		FM(SEL_HSCIF3_0)	FM(SEL_HSCIF3_1)	FM(SEL_HSCIF3_2)	FM(SEL_HSCIF3_3)
#define MOD_SEL0_16		FM(SEL_HSCIF1_0)	FM(SEL_HSCIF1_1)
#define MOD_SEL0_14_13		FM(SEL_HSCIF2_0)	FM(SEL_HSCIF2_1)	FM(SEL_HSCIF2_2)	F_(0, 0)
#define MOD_SEL0_12		FM(SEL_ETHERAVB_0)	FM(SEL_ETHERAVB_1)
#define MOD_SEL0_11		FM(SEL_DRIF3_0)		FM(SEL_DRIF3_1)
#define MOD_SEL0_10		FM(SEL_DRIF2_0)		FM(SEL_DRIF2_1)
#define MOD_SEL0_9_8		FM(SEL_DRIF1_0)		FM(SEL_DRIF1_1)		FM(SEL_DRIF1_2)		F_(0, 0)
#define MOD_SEL0_7_6		FM(SEL_DRIF0_0)		FM(SEL_DRIF0_1)		FM(SEL_DRIF0_2)		F_(0, 0)
#define MOD_SEL0_5		FM(SEL_CANFD0_0)	FM(SEL_CANFD0_1)
#define MOD_SEL0_4_3		FM(SEL_ADGA_0)		FM(SEL_ADGA_1)		FM(SEL_ADGA_2)		FM(SEL_ADGA_3)

/* MOD_SEL1 */			/* 0 */			/* 1 */			/* 2 */			/* 3 */			/* 4 */			/* 5 */			/* 6 */			/* 7 */
#define MOD_SEL1_31_30		FM(SEL_TSIF1_0)		FM(SEL_TSIF1_1)		FM(SEL_TSIF1_2)		FM(SEL_TSIF1_3)
#define MOD_SEL1_29_28_27	FM(SEL_TSIF0_0)		FM(SEL_TSIF0_1)		FM(SEL_TSIF0_2)		FM(SEL_TSIF0_3)		FM(SEL_TSIF0_4)		F_(0, 0)		F_(0, 0)		F_(0, 0)
#define MOD_SEL1_26		FM(SEL_TIMER_TMU1_0)	FM(SEL_TIMER_TMU1_1)
#define MOD_SEL1_25_24		FM(SEL_SSP1_1_0)	FM(SEL_SSP1_1_1)	FM(SEL_SSP1_1_2)	FM(SEL_SSP1_1_3)
#define MOD_SEL1_23_22_21	FM(SEL_SSP1_0_0)	FM(SEL_SSP1_0_1)	FM(SEL_SSP1_0_2)	FM(SEL_SSP1_0_3)	FM(SEL_SSP1_0_4)	F_(0, 0)		F_(0, 0)		F_(0, 0)
#define MOD_SEL1_20		FM(SEL_SSI1_0)		FM(SEL_SSI1_1)
#define MOD_SEL1_19		FM(SEL_SPEED_PULSE_0)	FM(SEL_SPEED_PULSE_1)
#define MOD_SEL1_18_17		FM(SEL_SIMCARD_0)	FM(SEL_SIMCARD_1)	FM(SEL_SIMCARD_2)	FM(SEL_SIMCARD_3)
#define MOD_SEL1_16		FM(SEL_SDHI2_0)		FM(SEL_SDHI2_1)
#define MOD_SEL1_15_14		FM(SEL_SCIF4_0)		FM(SEL_SCIF4_1)		FM(SEL_SCIF4_2)		F_(0, 0)
#define MOD_SEL1_13		FM(SEL_SCIF3_0)		FM(SEL_SCIF3_1)
#define MOD_SEL1_12		FM(SEL_SCIF2_0)		FM(SEL_SCIF2_1)
#define MOD_SEL1_11		FM(SEL_SCIF1_0)		FM(SEL_SCIF1_1)
#define MOD_SEL1_10		FM(SEL_SCIF_0)		FM(SEL_SCIF_1)
#define MOD_SEL1_9		FM(SEL_REMOCON_0)	FM(SEL_REMOCON_1)
#define MOD_SEL1_6		FM(SEL_RCAN0_0)		FM(SEL_RCAN0_1)
#define MOD_SEL1_5		FM(SEL_PWM6_0)		FM(SEL_PWM6_1)
#define MOD_SEL1_4		FM(SEL_PWM5_0)		FM(SEL_PWM5_1)
#define MOD_SEL1_3		FM(SEL_PWM4_0)		FM(SEL_PWM4_1)
#define MOD_SEL1_2		FM(SEL_PWM3_0)		FM(SEL_PWM3_1)
#define MOD_SEL1_1		FM(SEL_PWM2_0)		FM(SEL_PWM2_1)
#define MOD_SEL1_0		FM(SEL_PWM1_0)		FM(SEL_PWM1_1)

/* MOD_SEL2 */			/* 0 */			/* 1 */			/* 2 */			/* 3 */
#define MOD_SEL2_31		FM(I2C_SEL_5_0)		FM(I2C_SEL_5_1)
#define MOD_SEL2_30		FM(I2C_SEL_3_0)		FM(I2C_SEL_3_1)
#define MOD_SEL2_29		FM(I2C_SEL_0_0)		FM(I2C_SEL_0_1)
#define MOD_SEL2_28_27		FM(SEL_FM_0)		FM(SEL_FM_1)		FM(SEL_FM_2)		FM(SEL_FM_3)
#define MOD_SEL2_26		FM(SEL_SCIF5_0)		FM(SEL_SCIF5_1)
#define MOD_SEL2_25_24_23	FM(SEL_I2C6_0)		FM(SEL_I2C6_1)		FM(SEL_I2C6_2)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)
#define MOD_SEL2_21		FM(SEL_SSI2_0)		FM(SEL_SSI2_1)
#define MOD_SEL2_20		FM(SEL_SSI9_0)		FM(SEL_SSI9_1)
#define MOD_SEL2_19		FM(SEL_TIMER_TMU2_0)	FM(SEL_TIMER_TMU2_1)
#define MOD_SEL2_18		FM(SEL_ADGB_0)		FM(SEL_ADGB_1)
#define MOD_SEL2_17		FM(SEL_ADGC_0)		FM(SEL_ADGC_1)
#define MOD_SEL2_0		FM(SEL_VIN4_0)		FM(SEL_VIN4_1)

#define PINMUX_MOD_SELS	\
\
MOD_SEL0_31_30_29	MOD_SEL1_31_30		MOD_SEL2_31 \
						MOD_SEL2_30 \
			MOD_SEL1_29_28_27	MOD_SEL2_29 \
MOD_SEL0_28_27					MOD_SEL2_28_27 \
MOD_SEL0_26_25_24	MOD_SEL1_26		MOD_SEL2_26 \
			MOD_SEL1_25_24		MOD_SEL2_25_24_23 \
MOD_SEL0_23		MOD_SEL1_23_22_21 \
MOD_SEL0_22 \
MOD_SEL0_21					MOD_SEL2_21 \
MOD_SEL0_20		MOD_SEL1_20		MOD_SEL2_20 \
MOD_SEL0_19		MOD_SEL1_19		MOD_SEL2_19 \
MOD_SEL0_18_17		MOD_SEL1_18_17		MOD_SEL2_18 \
						MOD_SEL2_17 \
MOD_SEL0_16		MOD_SEL1_16 \
			MOD_SEL1_15_14 \
MOD_SEL0_14_13 \
			MOD_SEL1_13 \
MOD_SEL0_12		MOD_SEL1_12 \
MOD_SEL0_11		MOD_SEL1_11 \
MOD_SEL0_10		MOD_SEL1_10 \
MOD_SEL0_9_8		MOD_SEL1_9 \
MOD_SEL0_7_6 \
			MOD_SEL1_6 \
MOD_SEL0_5		MOD_SEL1_5 \
MOD_SEL0_4_3		MOD_SEL1_4 \
			MOD_SEL1_3 \
			MOD_SEL1_2 \
			MOD_SEL1_1 \
			MOD_SEL1_0		MOD_SEL2_0

/*
 * These pins are not able to be muxed but have other properties
 * that can be set, such as drive-strength or pull-up/pull-down enable.
 */
#define PINMUX_STATIC \
	FM(QSPI0_SPCLK) FM(QSPI0_SSL) FM(QSPI0_MOSI_IO0) FM(QSPI0_MISO_IO1) \
	FM(QSPI0_IO2) FM(QSPI0_IO3) \
	FM(QSPI1_SPCLK) FM(QSPI1_SSL) FM(QSPI1_MOSI_IO0) FM(QSPI1_MISO_IO1) \
	FM(QSPI1_IO2) FM(QSPI1_IO3) \
	FM(RPC_INT) FM(RPC_WP) FM(RPC_RESET) \
	FM(AVB_TX_CTL) FM(AVB_TXC) FM(AVB_TD0) FM(AVB_TD1) FM(AVB_TD2) FM(AVB_TD3) \
	FM(AVB_RX_CTL) FM(AVB_RXC) FM(AVB_RD0) FM(AVB_RD1) FM(AVB_RD2) FM(AVB_RD3) \
	FM(AVB_TXCREFCLK) FM(AVB_MDIO) \
	FM(PRESETOUT) \
	FM(DU_DOTCLKIN0) FM(DU_DOTCLKIN1) FM(DU_DOTCLKIN2) FM(DU_DOTCLKIN3) \
	FM(TMS) FM(TDO) FM(ASEBRK) FM(MLB_REF) FM(TDI) FM(TCK) FM(TRST) FM(EXTALR)

#define PINMUX_PHYS \
	FM(SCL0) FM(SDA0) FM(SCL3) FM(SDA3) FM(SCL5) FM(SDA5)

enum {
	PINMUX_RESERVED = 0,

	PINMUX_DATA_BEGIN,
	GP_ALL(DATA),
	PINMUX_DATA_END,

#define F_(x, y)
#define FM(x)	FN_##x,
	PINMUX_FUNCTION_BEGIN,
	GP_ALL(FN),
	PINMUX_GPSR
	PINMUX_IPSR
	PINMUX_MOD_SELS
	PINMUX_FUNCTION_END,
#undef F_
#undef FM

#define F_(x, y)
#define FM(x)	x##_MARK,
	PINMUX_MARK_BEGIN,
	PINMUX_GPSR
	PINMUX_IPSR
	PINMUX_MOD_SELS
	PINMUX_STATIC
	PINMUX_PHYS
	PINMUX_MARK_END,
#undef F_
#undef FM
};

static const u16 pinmux_data[] = {
	PINMUX_DATA_GP_ALL(),

	PINMUX_SINGLE(AVS1),
	PINMUX_SINGLE(AVS2),
	PINMUX_SINGLE(CLKOUT),
	PINMUX_SINGLE(GP7_02),
	PINMUX_SINGLE(GP7_03),
	PINMUX_SINGLE(MSIOF0_RXD),
	PINMUX_SINGLE(MSIOF0_SCK),
	PINMUX_SINGLE(MSIOF0_TXD),
	PINMUX_SINGLE(SSI_SCK5),
	PINMUX_SINGLE(SSI_SDATA5),
	PINMUX_SINGLE(SSI_WS5),

	/* IPSR0 */
	PINMUX_IPSR_GPSR(IP0_3_0,	AVB_MDC),
	PINMUX_IPSR_MSEL(IP0_3_0,	MSIOF2_SS2_C,		SEL_MSIOF2_2),

	PINMUX_IPSR_GPSR(IP0_7_4,	AVB_MAGIC),
	PINMUX_IPSR_MSEL(IP0_7_4,	MSIOF2_SS1_C,		SEL_MSIOF2_2),
	PINMUX_IPSR_MSEL(IP0_7_4,	SCK4_A,			SEL_SCIF4_0),

	PINMUX_IPSR_GPSR(IP0_11_8,	AVB_PHY_INT),
	PINMUX_IPSR_MSEL(IP0_11_8,	MSIOF2_SYNC_C,		SEL_MSIOF2_2),
	PINMUX_IPSR_MSEL(IP0_11_8,	RX4_A,			SEL_SCIF4_0),

	PINMUX_IPSR_GPSR(IP0_15_12,	AVB_LINK),
	PINMUX_IPSR_MSEL(IP0_15_12,	MSIOF2_SCK_C,		SEL_MSIOF2_2),
	PINMUX_IPSR_MSEL(IP0_15_12,	TX4_A,			SEL_SCIF4_0),

	PINMUX_IPSR_PHYS_MSEL(IP0_19_16, AVB_AVTP_MATCH_A,	I2C_SEL_5_0,	SEL_ETHERAVB_0),
	PINMUX_IPSR_PHYS_MSEL(IP0_19_16, MSIOF2_RXD_C,		I2C_SEL_5_0,	SEL_MSIOF2_2),
	PINMUX_IPSR_PHYS_MSEL(IP0_19_16, CTS4_N_A,		I2C_SEL_5_0,	SEL_SCIF4_0),
	PINMUX_IPSR_MSEL(IP0_19_16,	FSCLKST2_N_A,		I2C_SEL_5_0),
	PINMUX_IPSR_PHYS(IP0_19_16,     SCL5,                   I2C_SEL_5_1),

	PINMUX_IPSR_PHYS_MSEL(IP0_23_20, AVB_AVTP_CAPTURE_A,	I2C_SEL_5_0,	SEL_ETHERAVB_0),
	PINMUX_IPSR_PHYS_MSEL(IP0_23_20, MSIOF2_TXD_C,		I2C_SEL_5_0,	SEL_MSIOF2_2),
	PINMUX_IPSR_PHYS_MSEL(IP0_23_20, RTS4_N_A,		I2C_SEL_5_0,	SEL_SCIF4_0),
	PINMUX_IPSR_PHYS(IP0_23_20,	SDA5,			I2C_SEL_5_1),

	PINMUX_IPSR_GPSR(IP0_27_24,	IRQ0),
	PINMUX_IPSR_GPSR(IP0_27_24,	QPOLB),
	PINMUX_IPSR_GPSR(IP0_27_24,	DU_CDE),
	PINMUX_IPSR_MSEL(IP0_27_24,	VI4_DATA0_B,		SEL_VIN4_1),
	PINMUX_IPSR_MSEL(IP0_27_24,	CAN0_TX_B,		SEL_RCAN0_1),
	PINMUX_IPSR_MSEL(IP0_27_24,	CANFD0_TX_B,		SEL_CANFD0_1),
	PINMUX_IPSR_MSEL(IP0_27_24,	MSIOF3_SS2_E,		SEL_MSIOF3_4),

	PINMUX_IPSR_GPSR(IP0_31_28,	IRQ1),
	PINMUX_IPSR_GPSR(IP0_31_28,	QPOLA),
	PINMUX_IPSR_GPSR(IP0_31_28,	DU_DISP),
	PINMUX_IPSR_MSEL(IP0_31_28,	VI4_DATA1_B,		SEL_VIN4_1),
	PINMUX_IPSR_MSEL(IP0_31_28,	CAN0_RX_B,		SEL_RCAN0_1),
	PINMUX_IPSR_MSEL(IP0_31_28,	CANFD0_RX_B,		SEL_CANFD0_1),
	PINMUX_IPSR_MSEL(IP0_31_28,	MSIOF3_SS1_E,		SEL_MSIOF3_4),

	/* IPSR1 */
	PINMUX_IPSR_GPSR(IP1_3_0,	IRQ2),
	PINMUX_IPSR_GPSR(IP1_3_0,	QCPV_QDE),
	PINMUX_IPSR_GPSR(IP1_3_0,	DU_EXODDF_DU_ODDF_DISP_CDE),
	PINMUX_IPSR_MSEL(IP1_3_0,	VI4_DATA2_B,		SEL_VIN4_1),
	PINMUX_IPSR_MSEL(IP1_3_0,	PWM3_B,			SEL_PWM3_1),
	PINMUX_IPSR_MSEL(IP1_3_0,	MSIOF3_SYNC_E,		SEL_MSIOF3_4),

	PINMUX_IPSR_GPSR(IP1_7_4,	IRQ3),
	PINMUX_IPSR_GPSR(IP1_7_4,	QSTVB_QVE),
	PINMUX_IPSR_GPSR(IP1_7_4,	DU_DOTCLKOUT1),
	PINMUX_IPSR_MSEL(IP1_7_4,	VI4_DATA3_B,		SEL_VIN4_1),
	PINMUX_IPSR_MSEL(IP1_7_4,	PWM4_B,			SEL_PWM4_1),
	PINMUX_IPSR_MSEL(IP1_7_4,	MSIOF3_SCK_E,		SEL_MSIOF3_4),

	PINMUX_IPSR_GPSR(IP1_11_8,	IRQ4),
	PINMUX_IPSR_GPSR(IP1_11_8,	QSTH_QHS),
	PINMUX_IPSR_GPSR(IP1_11_8,	DU_EXHSYNC_DU_HSYNC),
	PINMUX_IPSR_MSEL(IP1_11_8,	VI4_DATA4_B,		SEL_VIN4_1),
	PINMUX_IPSR_MSEL(IP1_11_8,	PWM5_B,			SEL_PWM5_1),
	PINMUX_IPSR_MSEL(IP1_11_8,	MSIOF3_RXD_E,		SEL_MSIOF3_4),

	PINMUX_IPSR_GPSR(IP1_15_12,	IRQ5),
	PINMUX_IPSR_GPSR(IP1_15_12,	QSTB_QHE),
	PINMUX_IPSR_GPSR(IP1_15_12,	DU_EXVSYNC_DU_VSYNC),
	PINMUX_IPSR_MSEL(IP1_15_12,	VI4_DATA5_B,		SEL_VIN4_1),
	PINMUX_IPSR_MSEL(IP1_15_12,	PWM6_B,			SEL_PWM6_1),
	PINMUX_IPSR_GPSR(IP1_15_12,	FSCLKST2_N_B),
	PINMUX_IPSR_MSEL(IP1_15_12,	MSIOF3_TXD_E,		SEL_MSIOF3_4),

	PINMUX_IPSR_GPSR(IP1_19_16,	PWM0),
	PINMUX_IPSR_GPSR(IP1_19_16,	AVB_AVTP_PPS),
	PINMUX_IPSR_MSEL(IP1_19_16,	VI4_DATA6_B,		SEL_VIN4_1),
	PINMUX_IPSR_MSEL(IP1_19_16,	IECLK_B,		SEL_IEBUS_1),

	PINMUX_IPSR_PHYS_MSEL(IP1_23_20, PWM1_A,		I2C_SEL_3_0,	SEL_PWM1_0),
	PINMUX_IPSR_PHYS_MSEL(IP1_23_20, HRX3_D,		I2C_SEL_3_0,	SEL_HSCIF3_3),
	PINMUX_IPSR_PHYS_MSEL(IP1_23_20, VI4_DATA7_B,		I2C_SEL_3_0,	SEL_VIN4_1),
	PINMUX_IPSR_PHYS_MSEL(IP1_23_20, IERX_B,		I2C_SEL_3_0,	SEL_IEBUS_1),
	PINMUX_IPSR_PHYS(IP1_23_20,	SCL3,			I2C_SEL_3_1),

	PINMUX_IPSR_PHYS_MSEL(IP1_27_24, PWM2_A,		I2C_SEL_3_0,	SEL_PWM2_0),
	PINMUX_IPSR_PHYS_MSEL(IP1_27_24, HTX3_D,		I2C_SEL_3_0,	SEL_HSCIF3_3),
	PINMUX_IPSR_PHYS_MSEL(IP1_27_24, IETX_B,		I2C_SEL_3_0,	SEL_IEBUS_1),
	PINMUX_IPSR_PHYS(IP1_27_24,	SDA3,			I2C_SEL_3_1),

	PINMUX_IPSR_GPSR(IP1_31_28,	A0),
	PINMUX_IPSR_GPSR(IP1_31_28,	LCDOUT16),
	PINMUX_IPSR_MSEL(IP1_31_28,	MSIOF3_SYNC_B,		SEL_MSIOF3_1),
	PINMUX_IPSR_GPSR(IP1_31_28,	VI4_DATA8),
	PINMUX_IPSR_GPSR(IP1_31_28,	DU_DB0),
	PINMUX_IPSR_MSEL(IP1_31_28,	PWM3_A,			SEL_PWM3_0),

	/* IPSR2 */
	PINMUX_IPSR_GPSR(IP2_3_0,	A1),
	PINMUX_IPSR_GPSR(IP2_3_0,	LCDOUT17),
	PINMUX_IPSR_MSEL(IP2_3_0,	MSIOF3_TXD_B,		SEL_MSIOF3_1),
	PINMUX_IPSR_GPSR(IP2_3_0,	VI4_DATA9),
	PINMUX_IPSR_GPSR(IP2_3_0,	DU_DB1),
	PINMUX_IPSR_MSEL(IP2_3_0,	PWM4_A,			SEL_PWM4_0),

	PINMUX_IPSR_GPSR(IP2_7_4,	A2),
	PINMUX_IPSR_GPSR(IP2_7_4,	LCDOUT18),
	PINMUX_IPSR_MSEL(IP2_7_4,	MSIOF3_SCK_B,		SEL_MSIOF3_1),
	PINMUX_IPSR_GPSR(IP2_7_4,	VI4_DATA10),
	PINMUX_IPSR_GPSR(IP2_7_4,	DU_DB2),
	PINMUX_IPSR_MSEL(IP2_7_4,	PWM5_A,			SEL_PWM5_0),

	PINMUX_IPSR_GPSR(IP2_11_8,	A3),
	PINMUX_IPSR_GPSR(IP2_11_8,	LCDOUT19),
	PINMUX_IPSR_MSEL(IP2_11_8,	MSIOF3_RXD_B,		SEL_MSIOF3_1),
	PINMUX_IPSR_GPSR(IP2_11_8,	VI4_DATA11),
	PINMUX_IPSR_GPSR(IP2_11_8,	DU_DB3),
	PINMUX_IPSR_MSEL(IP2_11_8,	PWM6_A,			SEL_PWM6_0),

	PINMUX_IPSR_GPSR(IP2_15_12,	A4),
	PINMUX_IPSR_GPSR(IP2_15_12,	LCDOUT20),
	PINMUX_IPSR_MSEL(IP2_15_12,	MSIOF3_SS1_B,		SEL_MSIOF3_1),
	PINMUX_IPSR_GPSR(IP2_15_12,	VI4_DATA12),
	PINMUX_IPSR_GPSR(IP2_15_12,	VI5_DATA12),
	PINMUX_IPSR_GPSR(IP2_15_12,	DU_DB4),

	PINMUX_IPSR_GPSR(IP2_19_16,	A5),
	PINMUX_IPSR_GPSR(IP2_19_16,	LCDOUT21),
	PINMUX_IPSR_MSEL(IP2_19_16,	MSIOF3_SS2_B,		SEL_MSIOF3_1),
	PINMUX_IPSR_MSEL(IP2_19_16,	SCK4_B,			SEL_SCIF4_1),
	PINMUX_IPSR_GPSR(IP2_19_16,	VI4_DATA13),
	PINMUX_IPSR_GPSR(IP2_19_16,	VI5_DATA13),
	PINMUX_IPSR_GPSR(IP2_19_16,	DU_DB5),

	PINMUX_IPSR_GPSR(IP2_23_20,	A6),
	PINMUX_IPSR_GPSR(IP2_23_20,	LCDOUT22),
	PINMUX_IPSR_MSEL(IP2_23_20,	MSIOF2_SS1_A,		SEL_MSIOF2_0),
	PINMUX_IPSR_MSEL(IP2_23_20,	RX4_B,			SEL_SCIF4_1),
	PINMUX_IPSR_GPSR(IP2_23_20,	VI4_DATA14),
	PINMUX_IPSR_GPSR(IP2_23_20,	VI5_DATA14),
	PINMUX_IPSR_GPSR(IP2_23_20,	DU_DB6),

	PINMUX_IPSR_GPSR(IP2_27_24,	A7),
	PINMUX_IPSR_GPSR(IP2_27_24,	LCDOUT23),
	PINMUX_IPSR_MSEL(IP2_27_24,	MSIOF2_SS2_A,		SEL_MSIOF2_0),
	PINMUX_IPSR_MSEL(IP2_27_24,	TX4_B,			SEL_SCIF4_1),
	PINMUX_IPSR_GPSR(IP2_27_24,	VI4_DATA15),
	PINMUX_IPSR_GPSR(IP2_27_24,	VI5_DATA15),
	PINMUX_IPSR_GPSR(IP2_27_24,	DU_DB7),

	PINMUX_IPSR_GPSR(IP2_31_28,	A8),
	PINMUX_IPSR_MSEL(IP2_31_28,	RX3_B,			SEL_SCIF3_1),
	PINMUX_IPSR_MSEL(IP2_31_28,	MSIOF2_SYNC_A,		SEL_MSIOF2_0),
	PINMUX_IPSR_MSEL(IP2_31_28,	HRX4_B,			SEL_HSCIF4_1),
	PINMUX_IPSR_MSEL(IP2_31_28,	SDA6_A,			SEL_I2C6_0),
	PINMUX_IPSR_MSEL(IP2_31_28,	AVB_AVTP_MATCH_B,	SEL_ETHERAVB_1),
	PINMUX_IPSR_MSEL(IP2_31_28,	PWM1_B,			SEL_PWM1_1),

	/* IPSR3 */
	PINMUX_IPSR_GPSR(IP3_3_0,	A9),
	PINMUX_IPSR_MSEL(IP3_3_0,	MSIOF2_SCK_A,		SEL_MSIOF2_0),
	PINMUX_IPSR_MSEL(IP3_3_0,	CTS4_N_B,		SEL_SCIF4_1),
	PINMUX_IPSR_GPSR(IP3_3_0,	VI5_VSYNC_N),

	PINMUX_IPSR_GPSR(IP3_7_4,	A10),
	PINMUX_IPSR_MSEL(IP3_7_4,	MSIOF2_RXD_A,		SEL_MSIOF2_0),
	PINMUX_IPSR_MSEL(IP3_7_4,	RTS4_N_B,		SEL_SCIF4_1),
	PINMUX_IPSR_GPSR(IP3_7_4,	VI5_HSYNC_N),

	PINMUX_IPSR_GPSR(IP3_11_8,	A11),
	PINMUX_IPSR_MSEL(IP3_11_8,	TX3_B,			SEL_SCIF3_1),
	PINMUX_IPSR_MSEL(IP3_11_8,	MSIOF2_TXD_A,		SEL_MSIOF2_0),
	PINMUX_IPSR_MSEL(IP3_11_8,	HTX4_B,			SEL_HSCIF4_1),
	PINMUX_IPSR_GPSR(IP3_11_8,	HSCK4),
	PINMUX_IPSR_GPSR(IP3_11_8,	VI5_FIELD),
	PINMUX_IPSR_MSEL(IP3_11_8,	SCL6_A,			SEL_I2C6_0),
	PINMUX_IPSR_MSEL(IP3_11_8,	AVB_AVTP_CAPTURE_B,	SEL_ETHERAVB_1),
	PINMUX_IPSR_MSEL(IP3_11_8,	PWM2_B,			SEL_PWM2_1),

	PINMUX_IPSR_GPSR(IP3_15_12,	A12),
	PINMUX_IPSR_GPSR(IP3_15_12,	LCDOUT12),
	PINMUX_IPSR_MSEL(IP3_15_12,	MSIOF3_SCK_C,		SEL_MSIOF3_2),
	PINMUX_IPSR_MSEL(IP3_15_12,	HRX4_A,			SEL_HSCIF4_0),
	PINMUX_IPSR_GPSR(IP3_15_12,	VI5_DATA8),
	PINMUX_IPSR_GPSR(IP3_15_12,	DU_DG4),

	PINMUX_IPSR_GPSR(IP3_19_16,	A13),
	PINMUX_IPSR_GPSR(IP3_19_16,	LCDOUT13),
	PINMUX_IPSR_MSEL(IP3_19_16,	MSIOF3_SYNC_C,		SEL_MSIOF3_2),
	PINMUX_IPSR_MSEL(IP3_19_16,	HTX4_A,			SEL_HSCIF4_0),
	PINMUX_IPSR_GPSR(IP3_19_16,	VI5_DATA9),
	PINMUX_IPSR_GPSR(IP3_19_16,	DU_DG5),

	PINMUX_IPSR_GPSR(IP3_23_20,	A14),
	PINMUX_IPSR_GPSR(IP3_23_20,	LCDOUT14),
	PINMUX_IPSR_MSEL(IP3_23_20,	MSIOF3_RXD_C,		SEL_MSIOF3_2),
	PINMUX_IPSR_GPSR(IP3_23_20,	HCTS4_N),
	PINMUX_IPSR_GPSR(IP3_23_20,	VI5_DATA10),
	PINMUX_IPSR_GPSR(IP3_23_20,	DU_DG6),

	PINMUX_IPSR_GPSR(IP3_27_24,	A15),
	PINMUX_IPSR_GPSR(IP3_27_24,	LCDOUT15),
	PINMUX_IPSR_MSEL(IP3_27_24,	MSIOF3_TXD_C,		SEL_MSIOF3_2),
	PINMUX_IPSR_GPSR(IP3_27_24,	HRTS4_N),
	PINMUX_IPSR_GPSR(IP3_27_24,	VI5_DATA11),
	PINMUX_IPSR_GPSR(IP3_27_24,	DU_DG7),

	PINMUX_IPSR_GPSR(IP3_31_28,	A16),
	PINMUX_IPSR_GPSR(IP3_31_28,	LCDOUT8),
	PINMUX_IPSR_GPSR(IP3_31_28,	VI4_FIELD),
	PINMUX_IPSR_GPSR(IP3_31_28,	DU_DG0),

	/* IPSR4 */
	PINMUX_IPSR_GPSR(IP4_3_0,	A17),
	PINMUX_IPSR_GPSR(IP4_3_0,	LCDOUT9),
	PINMUX_IPSR_GPSR(IP4_3_0,	VI4_VSYNC_N),
	PINMUX_IPSR_GPSR(IP4_3_0,	DU_DG1),

	PINMUX_IPSR_GPSR(IP4_7_4,	A18),
	PINMUX_IPSR_GPSR(IP4_7_4,	LCDOUT10),
	PINMUX_IPSR_GPSR(IP4_7_4,	VI4_HSYNC_N),
	PINMUX_IPSR_GPSR(IP4_7_4,	DU_DG2),

	PINMUX_IPSR_GPSR(IP4_11_8,	A19),
	PINMUX_IPSR_GPSR(IP4_11_8,	LCDOUT11),
	PINMUX_IPSR_GPSR(IP4_11_8,	VI4_CLKENB),
	PINMUX_IPSR_GPSR(IP4_11_8,	DU_DG3),

	PINMUX_IPSR_GPSR(IP4_15_12,	CS0_N),
	PINMUX_IPSR_GPSR(IP4_15_12,	VI5_CLKENB),

	PINMUX_IPSR_GPSR(IP4_19_16,	CS1_N),
	PINMUX_IPSR_GPSR(IP4_19_16,	VI5_CLK),
	PINMUX_IPSR_MSEL(IP4_19_16,	EX_WAIT0_B,		SEL_LBSC_1),

	PINMUX_IPSR_GPSR(IP4_23_20,	BS_N),
	PINMUX_IPSR_GPSR(IP4_23_20,	QSTVA_QVS),
	PINMUX_IPSR_MSEL(IP4_23_20,	MSIOF3_SCK_D,		SEL_MSIOF3_3),
	PINMUX_IPSR_GPSR(IP4_23_20,	SCK3),
	PINMUX_IPSR_GPSR(IP4_23_20,	HSCK3),
	PINMUX_IPSR_GPSR(IP4_23_20,	CAN1_TX),
	PINMUX_IPSR_GPSR(IP4_23_20,	CANFD1_TX),
	PINMUX_IPSR_MSEL(IP4_23_20,	IETX_A,			SEL_IEBUS_0),

	PINMUX_IPSR_GPSR(IP4_27_24,	RD_N),
	PINMUX_IPSR_MSEL(IP4_27_24,	MSIOF3_SYNC_D,		SEL_MSIOF3_3),
	PINMUX_IPSR_MSEL(IP4_27_24,	RX3_A,			SEL_SCIF3_0),
	PINMUX_IPSR_MSEL(IP4_27_24,	HRX3_A,			SEL_HSCIF3_0),
	PINMUX_IPSR_MSEL(IP4_27_24,	CAN0_TX_A,		SEL_RCAN0_0),
	PINMUX_IPSR_MSEL(IP4_27_24,	CANFD0_TX_A,		SEL_CANFD0_0),

	PINMUX_IPSR_GPSR(IP4_31_28,	RD_WR_N),
	PINMUX_IPSR_MSEL(IP4_31_28,	MSIOF3_RXD_D,		SEL_MSIOF3_3),
	PINMUX_IPSR_MSEL(IP4_31_28,	TX3_A,			SEL_SCIF3_0),
	PINMUX_IPSR_MSEL(IP4_31_28,	HTX3_A,			SEL_HSCIF3_0),
	PINMUX_IPSR_MSEL(IP4_31_28,	CAN0_RX_A,		SEL_RCAN0_0),
	PINMUX_IPSR_MSEL(IP4_31_28,	CANFD0_RX_A,		SEL_CANFD0_0),

	/* IPSR5 */
	PINMUX_IPSR_GPSR(IP5_3_0,	WE0_N),
	PINMUX_IPSR_MSEL(IP5_3_0,	MSIOF3_TXD_D,		SEL_MSIOF3_3),
	PINMUX_IPSR_GPSR(IP5_3_0,	CTS3_N),
	PINMUX_IPSR_GPSR(IP5_3_0,	HCTS3_N),
	PINMUX_IPSR_MSEL(IP5_3_0,	SCL6_B,			SEL_I2C6_1),
	PINMUX_IPSR_GPSR(IP5_3_0,	CAN_CLK),
	PINMUX_IPSR_MSEL(IP5_3_0,	IECLK_A,		SEL_IEBUS_0),

	PINMUX_IPSR_GPSR(IP5_7_4,	WE1_N),
	PINMUX_IPSR_MSEL(IP5_7_4,	MSIOF3_SS1_D,		SEL_MSIOF3_3),
	PINMUX_IPSR_GPSR(IP5_7_4,	RTS3_N),
	PINMUX_IPSR_GPSR(IP5_7_4,	HRTS3_N),
	PINMUX_IPSR_MSEL(IP5_7_4,	SDA6_B,			SEL_I2C6_1),
	PINMUX_IPSR_GPSR(IP5_7_4,	CAN1_RX),
	PINMUX_IPSR_GPSR(IP5_7_4,	CANFD1_RX),
	PINMUX_IPSR_MSEL(IP5_7_4,	IERX_A,			SEL_IEBUS_0),

	PINMUX_IPSR_MSEL(IP5_11_8,	EX_WAIT0_A,		SEL_LBSC_0),
	PINMUX_IPSR_GPSR(IP5_11_8,	QCLK),
	PINMUX_IPSR_GPSR(IP5_11_8,	VI4_CLK),
	PINMUX_IPSR_GPSR(IP5_11_8,	DU_DOTCLKOUT0),

	PINMUX_IPSR_GPSR(IP5_15_12,	D0),
	PINMUX_IPSR_MSEL(IP5_15_12,	MSIOF2_SS1_B,		SEL_MSIOF2_1),
	PINMUX_IPSR_MSEL(IP5_15_12,	MSIOF3_SCK_A,		SEL_MSIOF3_0),
	PINMUX_IPSR_GPSR(IP5_15_12,	VI4_DATA16),
	PINMUX_IPSR_GPSR(IP5_15_12,	VI5_DATA0),

	PINMUX_IPSR_GPSR(IP5_19_16,	D1),
	PINMUX_IPSR_MSEL(IP5_19_16,	MSIOF2_SS2_B,		SEL_MSIOF2_1),
	PINMUX_IPSR_MSEL(IP5_19_16,	MSIOF3_SYNC_A,		SEL_MSIOF3_0),
	PINMUX_IPSR_GPSR(IP5_19_16,	VI4_DATA17),
	PINMUX_IPSR_GPSR(IP5_19_16,	VI5_DATA1),

	PINMUX_IPSR_GPSR(IP5_23_20,	D2),
	PINMUX_IPSR_MSEL(IP5_23_20,	MSIOF3_RXD_A,		SEL_MSIOF3_0),
	PINMUX_IPSR_GPSR(IP5_23_20,	VI4_DATA18),
	PINMUX_IPSR_GPSR(IP5_23_20,	VI5_DATA2),

	PINMUX_IPSR_GPSR(IP5_27_24,	D3),
	PINMUX_IPSR_MSEL(IP5_27_24,	MSIOF3_TXD_A,		SEL_MSIOF3_0),
	PINMUX_IPSR_GPSR(IP5_27_24,	VI4_DATA19),
	PINMUX_IPSR_GPSR(IP5_27_24,	VI5_DATA3),

	PINMUX_IPSR_GPSR(IP5_31_28,	D4),
	PINMUX_IPSR_MSEL(IP5_31_28,	MSIOF2_SCK_B,		SEL_MSIOF2_1),
	PINMUX_IPSR_GPSR(IP5_31_28,	VI4_DATA20),
	PINMUX_IPSR_GPSR(IP5_31_28,	VI5_DATA4),

	/* IPSR6 */
	PINMUX_IPSR_GPSR(IP6_3_0,	D5),
	PINMUX_IPSR_MSEL(IP6_3_0,	MSIOF2_SYNC_B,		SEL_MSIOF2_1),
	PINMUX_IPSR_GPSR(IP6_3_0,	VI4_DATA21),
	PINMUX_IPSR_GPSR(IP6_3_0,	VI5_DATA5),

	PINMUX_IPSR_GPSR(IP6_7_4,	D6),
	PINMUX_IPSR_MSEL(IP6_7_4,	MSIOF2_RXD_B,		SEL_MSIOF2_1),
	PINMUX_IPSR_GPSR(IP6_7_4,	VI4_DATA22),
	PINMUX_IPSR_GPSR(IP6_7_4,	VI5_DATA6),

	PINMUX_IPSR_GPSR(IP6_11_8,	D7),
	PINMUX_IPSR_MSEL(IP6_11_8,	MSIOF2_TXD_B,		SEL_MSIOF2_1),
	PINMUX_IPSR_GPSR(IP6_11_8,	VI4_DATA23),
	PINMUX_IPSR_GPSR(IP6_11_8,	VI5_DATA7),

	PINMUX_IPSR_GPSR(IP6_15_12,	D8),
	PINMUX_IPSR_GPSR(IP6_15_12,	LCDOUT0),
	PINMUX_IPSR_MSEL(IP6_15_12,	MSIOF2_SCK_D,		SEL_MSIOF2_3),
	PINMUX_IPSR_MSEL(IP6_15_12,	SCK4_C,			SEL_SCIF4_2),
	PINMUX_IPSR_MSEL(IP6_15_12,	VI4_DATA0_A,		SEL_VIN4_0),
	PINMUX_IPSR_GPSR(IP6_15_12,	DU_DR0),

	PINMUX_IPSR_GPSR(IP6_19_16,	D9),
	PINMUX_IPSR_GPSR(IP6_19_16,	LCDOUT1),
	PINMUX_IPSR_MSEL(IP6_19_16,	MSIOF2_SYNC_D,		SEL_MSIOF2_3),
	PINMUX_IPSR_MSEL(IP6_19_16,	VI4_DATA1_A,		SEL_VIN4_0),
	PINMUX_IPSR_GPSR(IP6_19_16,	DU_DR1),

	PINMUX_IPSR_GPSR(IP6_23_20,	D10),
	PINMUX_IPSR_GPSR(IP6_23_20,	LCDOUT2),
	PINMUX_IPSR_MSEL(IP6_23_20,	MSIOF2_RXD_D,		SEL_MSIOF2_3),
	PINMUX_IPSR_MSEL(IP6_23_20,	HRX3_B,			SEL_HSCIF3_1),
	PINMUX_IPSR_MSEL(IP6_23_20,	VI4_DATA2_A,		SEL_VIN4_0),
	PINMUX_IPSR_MSEL(IP6_23_20,	CTS4_N_C,		SEL_SCIF4_2),
	PINMUX_IPSR_GPSR(IP6_23_20,	DU_DR2),

	PINMUX_IPSR_GPSR(IP6_27_24,	D11),
	PINMUX_IPSR_GPSR(IP6_27_24,	LCDOUT3),
	PINMUX_IPSR_MSEL(IP6_27_24,	MSIOF2_TXD_D,		SEL_MSIOF2_3),
	PINMUX_IPSR_MSEL(IP6_27_24,	HTX3_B,			SEL_HSCIF3_1),
	PINMUX_IPSR_MSEL(IP6_27_24,	VI4_DATA3_A,		SEL_VIN4_0),
	PINMUX_IPSR_MSEL(IP6_27_24,	RTS4_N_C,		SEL_SCIF4_2),
	PINMUX_IPSR_GPSR(IP6_27_24,	DU_DR3),

	PINMUX_IPSR_GPSR(IP6_31_28,	D12),
	PINMUX_IPSR_GPSR(IP6_31_28,	LCDOUT4),
	PINMUX_IPSR_MSEL(IP6_31_28,	MSIOF2_SS1_D,		SEL_MSIOF2_3),
	PINMUX_IPSR_MSEL(IP6_31_28,	RX4_C,			SEL_SCIF4_2),
	PINMUX_IPSR_MSEL(IP6_31_28,	VI4_DATA4_A,		SEL_VIN4_0),
	PINMUX_IPSR_GPSR(IP6_31_28,	DU_DR4),

	/* IPSR7 */
	PINMUX_IPSR_GPSR(IP7_3_0,	D13),
	PINMUX_IPSR_GPSR(IP7_3_0,	LCDOUT5),
	PINMUX_IPSR_MSEL(IP7_3_0,	MSIOF2_SS2_D,		SEL_MSIOF2_3),
	PINMUX_IPSR_MSEL(IP7_3_0,	TX4_C,			SEL_SCIF4_2),
	PINMUX_IPSR_MSEL(IP7_3_0,	VI4_DATA5_A,		SEL_VIN4_0),
	PINMUX_IPSR_GPSR(IP7_3_0,	DU_DR5),

	PINMUX_IPSR_GPSR(IP7_7_4,	D14),
	PINMUX_IPSR_GPSR(IP7_7_4,	LCDOUT6),
	PINMUX_IPSR_MSEL(IP7_7_4,	MSIOF3_SS1_A,		SEL_MSIOF3_0),
	PINMUX_IPSR_MSEL(IP7_7_4,	HRX3_C,			SEL_HSCIF3_2),
	PINMUX_IPSR_MSEL(IP7_7_4,	VI4_DATA6_A,		SEL_VIN4_0),
	PINMUX_IPSR_GPSR(IP7_7_4,	DU_DR6),
	PINMUX_IPSR_MSEL(IP7_7_4,	SCL6_C,			SEL_I2C6_2),

	PINMUX_IPSR_GPSR(IP7_11_8,	D15),
	PINMUX_IPSR_GPSR(IP7_11_8,	LCDOUT7),
	PINMUX_IPSR_MSEL(IP7_11_8,	MSIOF3_SS2_A,		SEL_MSIOF3_0),
	PINMUX_IPSR_MSEL(IP7_11_8,	HTX3_C,			SEL_HSCIF3_2),
	PINMUX_IPSR_MSEL(IP7_11_8,	VI4_DATA7_A,		SEL_VIN4_0),
	PINMUX_IPSR_GPSR(IP7_11_8,	DU_DR7),
	PINMUX_IPSR_MSEL(IP7_11_8,	SDA6_C,			SEL_I2C6_2),

	PINMUX_IPSR_GPSR(IP7_19_16,	SD0_CLK),
	PINMUX_IPSR_MSEL(IP7_19_16,	MSIOF1_SCK_E,		SEL_MSIOF1_4),
	PINMUX_IPSR_MSEL(IP7_19_16,	STP_OPWM_0_B,		SEL_SSP1_0_1),

	PINMUX_IPSR_GPSR(IP7_23_20,	SD0_CMD),
	PINMUX_IPSR_MSEL(IP7_23_20,	MSIOF1_SYNC_E,		SEL_MSIOF1_4),
	PINMUX_IPSR_MSEL(IP7_23_20,	STP_IVCXO27_0_B,	SEL_SSP1_0_1),

	PINMUX_IPSR_GPSR(IP7_27_24,	SD0_DAT0),
	PINMUX_IPSR_MSEL(IP7_27_24,	MSIOF1_RXD_E,		SEL_MSIOF1_4),
	PINMUX_IPSR_MSEL(IP7_27_24,	TS_SCK0_B,		SEL_TSIF0_1),
	PINMUX_IPSR_MSEL(IP7_27_24,	STP_ISCLK_0_B,		SEL_SSP1_0_1),

	PINMUX_IPSR_GPSR(IP7_31_28,	SD0_DAT1),
	PINMUX_IPSR_MSEL(IP7_31_28,	MSIOF1_TXD_E,		SEL_MSIOF1_4),
	PINMUX_IPSR_MSEL(IP7_31_28,	TS_SPSYNC0_B,		SEL_TSIF0_1),
	PINMUX_IPSR_MSEL(IP7_31_28,	STP_ISSYNC_0_B,		SEL_SSP1_0_1),

	/* IPSR8 */
	PINMUX_IPSR_GPSR(IP8_3_0,	SD0_DAT2),
	PINMUX_IPSR_MSEL(IP8_3_0,	MSIOF1_SS1_E,		SEL_MSIOF1_4),
	PINMUX_IPSR_MSEL(IP8_3_0,	TS_SDAT0_B,		SEL_TSIF0_1),
	PINMUX_IPSR_MSEL(IP8_3_0,	STP_ISD_0_B,		SEL_SSP1_0_1),

	PINMUX_IPSR_GPSR(IP8_7_4,	SD0_DAT3),
	PINMUX_IPSR_MSEL(IP8_7_4,	MSIOF1_SS2_E,		SEL_MSIOF1_4),
	PINMUX_IPSR_MSEL(IP8_7_4,	TS_SDEN0_B,		SEL_TSIF0_1),
	PINMUX_IPSR_MSEL(IP8_7_4,	STP_ISEN_0_B,		SEL_SSP1_0_1),

	PINMUX_IPSR_GPSR(IP8_11_8,	SD1_CLK),
	PINMUX_IPSR_MSEL(IP8_11_8,	MSIOF1_SCK_G,		SEL_MSIOF1_6),
	PINMUX_IPSR_MSEL(IP8_11_8,	SIM0_CLK_A,		SEL_SIMCARD_0),

	PINMUX_IPSR_GPSR(IP8_15_12,	SD1_CMD),
	PINMUX_IPSR_MSEL(IP8_15_12,	MSIOF1_SYNC_G,		SEL_MSIOF1_6),
	PINMUX_IPSR_GPSR(IP8_15_12,	NFCE_N_B),
	PINMUX_IPSR_MSEL(IP8_15_12,	SIM0_D_A,		SEL_SIMCARD_0),
	PINMUX_IPSR_MSEL(IP8_15_12,	STP_IVCXO27_1_B,	SEL_SSP1_1_1),

	PINMUX_IPSR_GPSR(IP8_19_16,	SD1_DAT0),
	PINMUX_IPSR_GPSR(IP8_19_16,	SD2_DAT4),
	PINMUX_IPSR_MSEL(IP8_19_16,	MSIOF1_RXD_G,		SEL_MSIOF1_6),
	PINMUX_IPSR_GPSR(IP8_19_16,	NFWP_N_B),
	PINMUX_IPSR_MSEL(IP8_19_16,	TS_SCK1_B,		SEL_TSIF1_1),
	PINMUX_IPSR_MSEL(IP8_19_16,	STP_ISCLK_1_B,		SEL_SSP1_1_1),

	PINMUX_IPSR_GPSR(IP8_23_20,	SD1_DAT1),
	PINMUX_IPSR_GPSR(IP8_23_20,	SD2_DAT5),
	PINMUX_IPSR_MSEL(IP8_23_20,	MSIOF1_TXD_G,		SEL_MSIOF1_6),
	PINMUX_IPSR_GPSR(IP8_23_20,	NFDATA14_B),
	PINMUX_IPSR_MSEL(IP8_23_20,	TS_SPSYNC1_B,		SEL_TSIF1_1),
	PINMUX_IPSR_MSEL(IP8_23_20,	STP_ISSYNC_1_B,		SEL_SSP1_1_1),

	PINMUX_IPSR_GPSR(IP8_27_24,	SD1_DAT2),
	PINMUX_IPSR_GPSR(IP8_27_24,	SD2_DAT6),
	PINMUX_IPSR_MSEL(IP8_27_24,	MSIOF1_SS1_G,		SEL_MSIOF1_6),
	PINMUX_IPSR_GPSR(IP8_27_24,	NFDATA15_B),
	PINMUX_IPSR_MSEL(IP8_27_24,	TS_SDAT1_B,		SEL_TSIF1_1),
	PINMUX_IPSR_MSEL(IP8_27_24,	STP_ISD_1_B,		SEL_SSP1_1_1),

	PINMUX_IPSR_GPSR(IP8_31_28,	SD1_DAT3),
	PINMUX_IPSR_GPSR(IP8_31_28,	SD2_DAT7),
	PINMUX_IPSR_MSEL(IP8_31_28,	MSIOF1_SS2_G,		SEL_MSIOF1_6),
	PINMUX_IPSR_GPSR(IP8_31_28,	NFRB_N_B),
	PINMUX_IPSR_MSEL(IP8_31_28,	TS_SDEN1_B,		SEL_TSIF1_1),
	PINMUX_IPSR_MSEL(IP8_31_28,	STP_ISEN_1_B,		SEL_SSP1_1_1),

	/* IPSR9 */
	PINMUX_IPSR_GPSR(IP9_3_0,	SD2_CLK),
	PINMUX_IPSR_GPSR(IP9_3_0,	NFDATA8),

	PINMUX_IPSR_GPSR(IP9_7_4,	SD2_CMD),
	PINMUX_IPSR_GPSR(IP9_7_4,	NFDATA9),

	PINMUX_IPSR_GPSR(IP9_11_8,	SD2_DAT0),
	PINMUX_IPSR_GPSR(IP9_11_8,	NFDATA10),

	PINMUX_IPSR_GPSR(IP9_15_12,	SD2_DAT1),
	PINMUX_IPSR_GPSR(IP9_15_12,	NFDATA11),

	PINMUX_IPSR_GPSR(IP9_19_16,	SD2_DAT2),
	PINMUX_IPSR_GPSR(IP9_19_16,	NFDATA12),

	PINMUX_IPSR_GPSR(IP9_23_20,	SD2_DAT3),
	PINMUX_IPSR_GPSR(IP9_23_20,	NFDATA13),

	PINMUX_IPSR_GPSR(IP9_27_24,	SD2_DS),
	PINMUX_IPSR_GPSR(IP9_27_24,	NFALE),
	PINMUX_IPSR_GPSR(IP9_27_24,	SATA_DEVSLP_B),

	PINMUX_IPSR_GPSR(IP9_31_28,	SD3_CLK),
	PINMUX_IPSR_GPSR(IP9_31_28,	NFWE_N),

	/* IPSR10 */
	PINMUX_IPSR_GPSR(IP10_3_0,	SD3_CMD),
	PINMUX_IPSR_GPSR(IP10_3_0,	NFRE_N),

	PINMUX_IPSR_GPSR(IP10_7_4,	SD3_DAT0),
	PINMUX_IPSR_GPSR(IP10_7_4,	NFDATA0),

	PINMUX_IPSR_GPSR(IP10_11_8,	SD3_DAT1),
	PINMUX_IPSR_GPSR(IP10_11_8,	NFDATA1),

	PINMUX_IPSR_GPSR(IP10_15_12,	SD3_DAT2),
	PINMUX_IPSR_GPSR(IP10_15_12,	NFDATA2),

	PINMUX_IPSR_GPSR(IP10_19_16,	SD3_DAT3),
	PINMUX_IPSR_GPSR(IP10_19_16,	NFDATA3),

	PINMUX_IPSR_GPSR(IP10_23_20,	SD3_DAT4),
	PINMUX_IPSR_MSEL(IP10_23_20,	SD2_CD_A,		SEL_SDHI2_0),
	PINMUX_IPSR_GPSR(IP10_23_20,	NFDATA4),

	PINMUX_IPSR_GPSR(IP10_27_24,	SD3_DAT5),
	PINMUX_IPSR_MSEL(IP10_27_24,	SD2_WP_A,		SEL_SDHI2_0),
	PINMUX_IPSR_GPSR(IP10_27_24,	NFDATA5),

	PINMUX_IPSR_GPSR(IP10_31_28,	SD3_DAT6),
	PINMUX_IPSR_GPSR(IP10_31_28,	SD3_CD),
	PINMUX_IPSR_GPSR(IP10_31_28,	NFDATA6),

	/* IPSR11 */
	PINMUX_IPSR_GPSR(IP11_3_0,	SD3_DAT7),
	PINMUX_IPSR_GPSR(IP11_3_0,	SD3_WP),
	PINMUX_IPSR_GPSR(IP11_3_0,	NFDATA7),

	PINMUX_IPSR_GPSR(IP11_7_4,	SD3_DS),
	PINMUX_IPSR_GPSR(IP11_7_4,	NFCLE),

	PINMUX_IPSR_GPSR(IP11_11_8,	SD0_CD),
	PINMUX_IPSR_MSEL(IP11_11_8,	SCL2_B,			SEL_I2C2_1),
	PINMUX_IPSR_MSEL(IP11_11_8,	SIM0_RST_A,		SEL_SIMCARD_0),

	PINMUX_IPSR_GPSR(IP11_15_12,	SD0_WP),
	PINMUX_IPSR_MSEL(IP11_15_12,	SDA2_B,			SEL_I2C2_1),

	PINMUX_IPSR_MSEL(IP11_19_16,	SD1_CD,			I2C_SEL_0_0),
	PINMUX_IPSR_PHYS_MSEL(IP11_19_16, SIM0_CLK_B,		I2C_SEL_0_0,	SEL_SIMCARD_1),
	PINMUX_IPSR_PHYS(IP11_19_16,	SCL0,			I2C_SEL_0_1),

	PINMUX_IPSR_MSEL(IP11_23_20,	SD1_WP,			I2C_SEL_0_0),
	PINMUX_IPSR_PHYS_MSEL(IP11_23_20, SIM0_D_B,		I2C_SEL_0_0,	SEL_SIMCARD_1),
	PINMUX_IPSR_PHYS(IP11_23_20,	SDA0,			I2C_SEL_0_1),

	PINMUX_IPSR_GPSR(IP11_27_24,	SCK0),
	PINMUX_IPSR_MSEL(IP11_27_24,	HSCK1_B,		SEL_HSCIF1_1),
	PINMUX_IPSR_MSEL(IP11_27_24,	MSIOF1_SS2_B,		SEL_MSIOF1_1),
	PINMUX_IPSR_MSEL(IP11_27_24,	AUDIO_CLKC_B,		SEL_ADGC_1),
	PINMUX_IPSR_MSEL(IP11_27_24,	SDA2_A,			SEL_I2C2_0),
	PINMUX_IPSR_MSEL(IP11_27_24,	SIM0_RST_B,		SEL_SIMCARD_1),
	PINMUX_IPSR_MSEL(IP11_27_24,	STP_OPWM_0_C,		SEL_SSP1_0_2),
	PINMUX_IPSR_MSEL(IP11_27_24,	RIF0_CLK_B,		SEL_DRIF0_1),
	PINMUX_IPSR_GPSR(IP11_27_24,	ADICHS2),
	PINMUX_IPSR_MSEL(IP11_27_24,	SCK5_B,			SEL_SCIF5_1),

	PINMUX_IPSR_GPSR(IP11_31_28,	RX0),
	PINMUX_IPSR_MSEL(IP11_31_28,	HRX1_B,			SEL_HSCIF1_1),
	PINMUX_IPSR_MSEL(IP11_31_28,	TS_SCK0_C,		SEL_TSIF0_2),
	PINMUX_IPSR_MSEL(IP11_31_28,	STP_ISCLK_0_C,		SEL_SSP1_0_2),
	PINMUX_IPSR_MSEL(IP11_31_28,	RIF0_D0_B,		SEL_DRIF0_1),

	/* IPSR12 */
	PINMUX_IPSR_GPSR(IP12_3_0,	TX0),
	PINMUX_IPSR_MSEL(IP12_3_0,	HTX1_B,			SEL_HSCIF1_1),
	PINMUX_IPSR_MSEL(IP12_3_0,	TS_SPSYNC0_C,		SEL_TSIF0_2),
	PINMUX_IPSR_MSEL(IP12_3_0,	STP_ISSYNC_0_C,		SEL_SSP1_0_2),
	PINMUX_IPSR_MSEL(IP12_3_0,	RIF0_D1_B,		SEL_DRIF0_1),

	PINMUX_IPSR_GPSR(IP12_7_4,	CTS0_N),
	PINMUX_IPSR_MSEL(IP12_7_4,	HCTS1_N_B,		SEL_HSCIF1_1),
	PINMUX_IPSR_MSEL(IP12_7_4,	MSIOF1_SYNC_B,		SEL_MSIOF1_1),
	PINMUX_IPSR_MSEL(IP12_7_4,	TS_SPSYNC1_C,		SEL_TSIF1_2),
	PINMUX_IPSR_MSEL(IP12_7_4,	STP_ISSYNC_1_C,		SEL_SSP1_1_2),
	PINMUX_IPSR_MSEL(IP12_7_4,	RIF1_SYNC_B,		SEL_DRIF1_1),
	PINMUX_IPSR_GPSR(IP12_7_4,	AUDIO_CLKOUT_C),
	PINMUX_IPSR_GPSR(IP12_7_4,	ADICS_SAMP),

	PINMUX_IPSR_GPSR(IP12_11_8,	RTS0_N),
	PINMUX_IPSR_MSEL(IP12_11_8,	HRTS1_N_B,		SEL_HSCIF1_1),
	PINMUX_IPSR_MSEL(IP12_11_8,	MSIOF1_SS1_B,		SEL_MSIOF1_1),
	PINMUX_IPSR_MSEL(IP12_11_8,	AUDIO_CLKA_B,		SEL_ADGA_1),
	PINMUX_IPSR_MSEL(IP12_11_8,	SCL2_A,			SEL_I2C2_0),
	PINMUX_IPSR_MSEL(IP12_11_8,	STP_IVCXO27_1_C,	SEL_SSP1_1_2),
	PINMUX_IPSR_MSEL(IP12_11_8,	RIF0_SYNC_B,		SEL_DRIF0_1),
	PINMUX_IPSR_GPSR(IP12_11_8,	ADICHS1),

	PINMUX_IPSR_MSEL(IP12_15_12,	RX1_A,			SEL_SCIF1_0),
	PINMUX_IPSR_MSEL(IP12_15_12,	HRX1_A,			SEL_HSCIF1_0),
	PINMUX_IPSR_MSEL(IP12_15_12,	TS_SDAT0_C,		SEL_TSIF0_2),
	PINMUX_IPSR_MSEL(IP12_15_12,	STP_ISD_0_C,		SEL_SSP1_0_2),
	PINMUX_IPSR_MSEL(IP12_15_12,	RIF1_CLK_C,		SEL_DRIF1_2),

	PINMUX_IPSR_MSEL(IP12_19_16,	TX1_A,			SEL_SCIF1_0),
	PINMUX_IPSR_MSEL(IP12_19_16,	HTX1_A,			SEL_HSCIF1_0),
	PINMUX_IPSR_MSEL(IP12_19_16,	TS_SDEN0_C,		SEL_TSIF0_2),
	PINMUX_IPSR_MSEL(IP12_19_16,	STP_ISEN_0_C,		SEL_SSP1_0_2),
	PINMUX_IPSR_MSEL(IP12_19_16,	RIF1_D0_C,		SEL_DRIF1_2),

	PINMUX_IPSR_GPSR(IP12_23_20,	CTS1_N),
	PINMUX_IPSR_MSEL(IP12_23_20,	HCTS1_N_A,		SEL_HSCIF1_0),
	PINMUX_IPSR_MSEL(IP12_23_20,	MSIOF1_RXD_B,		SEL_MSIOF1_1),
	PINMUX_IPSR_MSEL(IP12_23_20,	TS_SDEN1_C,		SEL_TSIF1_2),
	PINMUX_IPSR_MSEL(IP12_23_20,	STP_ISEN_1_C,		SEL_SSP1_1_2),
	PINMUX_IPSR_MSEL(IP12_23_20,	RIF1_D0_B,		SEL_DRIF1_1),
	PINMUX_IPSR_GPSR(IP12_23_20,	ADIDATA),

	PINMUX_IPSR_GPSR(IP12_27_24,	RTS1_N),
	PINMUX_IPSR_MSEL(IP12_27_24,	HRTS1_N_A,		SEL_HSCIF1_0),
	PINMUX_IPSR_MSEL(IP12_27_24,	MSIOF1_TXD_B,		SEL_MSIOF1_1),
	PINMUX_IPSR_MSEL(IP12_27_24,	TS_SDAT1_C,		SEL_TSIF1_2),
	PINMUX_IPSR_MSEL(IP12_27_24,	STP_ISD_1_C,		SEL_SSP1_1_2),
	PINMUX_IPSR_MSEL(IP12_27_24,	RIF1_D1_B,		SEL_DRIF1_1),
	PINMUX_IPSR_GPSR(IP12_27_24,	ADICHS0),

	PINMUX_IPSR_GPSR(IP12_31_28,	SCK2),
	PINMUX_IPSR_MSEL(IP12_31_28,	SCIF_CLK_B,		SEL_SCIF_1),
	PINMUX_IPSR_MSEL(IP12_31_28,	MSIOF1_SCK_B,		SEL_MSIOF1_1),
	PINMUX_IPSR_MSEL(IP12_31_28,	TS_SCK1_C,		SEL_TSIF1_2),
	PINMUX_IPSR_MSEL(IP12_31_28,	STP_ISCLK_1_C,		SEL_SSP1_1_2),
	PINMUX_IPSR_MSEL(IP12_31_28,	RIF1_CLK_B,		SEL_DRIF1_1),
	PINMUX_IPSR_GPSR(IP12_31_28,	ADICLK),

	/* IPSR13 */
	PINMUX_IPSR_MSEL(IP13_3_0,	TX2_A,			SEL_SCIF2_0),
	PINMUX_IPSR_MSEL(IP13_3_0,	SD2_CD_B,		SEL_SDHI2_1),
	PINMUX_IPSR_MSEL(IP13_3_0,	SCL1_A,			SEL_I2C1_0),
	PINMUX_IPSR_MSEL(IP13_3_0,	FMCLK_A,		SEL_FM_0),
	PINMUX_IPSR_MSEL(IP13_3_0,	RIF1_D1_C,		SEL_DRIF1_2),
	PINMUX_IPSR_GPSR(IP13_3_0,	FSO_CFE_0_N),

	PINMUX_IPSR_MSEL(IP13_7_4,	RX2_A,			SEL_SCIF2_0),
	PINMUX_IPSR_MSEL(IP13_7_4,	SD2_WP_B,		SEL_SDHI2_1),
	PINMUX_IPSR_MSEL(IP13_7_4,	SDA1_A,			SEL_I2C1_0),
	PINMUX_IPSR_MSEL(IP13_7_4,	FMIN_A,			SEL_FM_0),
	PINMUX_IPSR_MSEL(IP13_7_4,	RIF1_SYNC_C,		SEL_DRIF1_2),
	PINMUX_IPSR_GPSR(IP13_7_4,	FSO_CFE_1_N),

	PINMUX_IPSR_GPSR(IP13_11_8,	HSCK0),
	PINMUX_IPSR_MSEL(IP13_11_8,	MSIOF1_SCK_D,		SEL_MSIOF1_3),
	PINMUX_IPSR_MSEL(IP13_11_8,	AUDIO_CLKB_A,		SEL_ADGB_0),
	PINMUX_IPSR_MSEL(IP13_11_8,	SSI_SDATA1_B,		SEL_SSI1_1),
	PINMUX_IPSR_MSEL(IP13_11_8,	TS_SCK0_D,		SEL_TSIF0_3),
	PINMUX_IPSR_MSEL(IP13_11_8,	STP_ISCLK_0_D,		SEL_SSP1_0_3),
	PINMUX_IPSR_MSEL(IP13_11_8,	RIF0_CLK_C,		SEL_DRIF0_2),
	PINMUX_IPSR_MSEL(IP13_11_8,	RX5_B,			SEL_SCIF5_1),

	PINMUX_IPSR_GPSR(IP13_15_12,	HRX0),
	PINMUX_IPSR_MSEL(IP13_15_12,	MSIOF1_RXD_D,		SEL_MSIOF1_3),
	PINMUX_IPSR_MSEL(IP13_15_12,	SSI_SDATA2_B,		SEL_SSI2_1),
	PINMUX_IPSR_MSEL(IP13_15_12,	TS_SDEN0_D,		SEL_TSIF0_3),
	PINMUX_IPSR_MSEL(IP13_15_12,	STP_ISEN_0_D,		SEL_SSP1_0_3),
	PINMUX_IPSR_MSEL(IP13_15_12,	RIF0_D0_C,		SEL_DRIF0_2),

	PINMUX_IPSR_GPSR(IP13_19_16,	HTX0),
	PINMUX_IPSR_MSEL(IP13_19_16,	MSIOF1_TXD_D,		SEL_MSIOF1_3),
	PINMUX_IPSR_MSEL(IP13_19_16,	SSI_SDATA9_B,		SEL_SSI9_1),
	PINMUX_IPSR_MSEL(IP13_19_16,	TS_SDAT0_D,		SEL_TSIF0_3),
	PINMUX_IPSR_MSEL(IP13_19_16,	STP_ISD_0_D,		SEL_SSP1_0_3),
	PINMUX_IPSR_MSEL(IP13_19_16,	RIF0_D1_C,		SEL_DRIF0_2),

	PINMUX_IPSR_GPSR(IP13_23_20,	HCTS0_N),
	PINMUX_IPSR_MSEL(IP13_23_20,	RX2_B,			SEL_SCIF2_1),
	PINMUX_IPSR_MSEL(IP13_23_20,	MSIOF1_SYNC_D,		SEL_MSIOF1_3),
	PINMUX_IPSR_MSEL(IP13_23_20,	SSI_SCK9_A,		SEL_SSI9_0),
	PINMUX_IPSR_MSEL(IP13_23_20,	TS_SPSYNC0_D,		SEL_TSIF0_3),
	PINMUX_IPSR_MSEL(IP13_23_20,	STP_ISSYNC_0_D,		SEL_SSP1_0_3),
	PINMUX_IPSR_MSEL(IP13_23_20,	RIF0_SYNC_C,		SEL_DRIF0_2),
	PINMUX_IPSR_GPSR(IP13_23_20,	AUDIO_CLKOUT1_A),

	PINMUX_IPSR_GPSR(IP13_27_24,	HRTS0_N),
	PINMUX_IPSR_MSEL(IP13_27_24,	TX2_B,			SEL_SCIF2_1),
	PINMUX_IPSR_MSEL(IP13_27_24,	MSIOF1_SS1_D,		SEL_MSIOF1_3),
	PINMUX_IPSR_MSEL(IP13_27_24,	SSI_WS9_A,		SEL_SSI9_0),
	PINMUX_IPSR_MSEL(IP13_27_24,	STP_IVCXO27_0_D,	SEL_SSP1_0_3),
	PINMUX_IPSR_MSEL(IP13_27_24,	BPFCLK_A,		SEL_FM_0),
	PINMUX_IPSR_GPSR(IP13_27_24,	AUDIO_CLKOUT2_A),

	PINMUX_IPSR_GPSR(IP13_31_28,	MSIOF0_SYNC),
	PINMUX_IPSR_GPSR(IP13_31_28,	AUDIO_CLKOUT_A),
	PINMUX_IPSR_MSEL(IP13_31_28,	TX5_B,			SEL_SCIF5_1),
	PINMUX_IPSR_MSEL(IP13_31_28,	BPFCLK_D,		SEL_FM_3),

	/* IPSR14 */
	PINMUX_IPSR_GPSR(IP14_3_0,	MSIOF0_SS1),
	PINMUX_IPSR_MSEL(IP14_3_0,	RX5_A,			SEL_SCIF5_0),
	PINMUX_IPSR_GPSR(IP14_3_0,	NFWP_N_A),
	PINMUX_IPSR_MSEL(IP14_3_0,	AUDIO_CLKA_C,		SEL_ADGA_2),
	PINMUX_IPSR_MSEL(IP14_3_0,	SSI_SCK2_A,		SEL_SSI2_0),
	PINMUX_IPSR_MSEL(IP14_3_0,	STP_IVCXO27_0_C,	SEL_SSP1_0_2),
	PINMUX_IPSR_GPSR(IP14_3_0,	AUDIO_CLKOUT3_A),
	PINMUX_IPSR_MSEL(IP14_3_0,	TCLK1_B,		SEL_TIMER_TMU1_1),

	PINMUX_IPSR_GPSR(IP14_7_4,	MSIOF0_SS2),
	PINMUX_IPSR_MSEL(IP14_7_4,	TX5_A,			SEL_SCIF5_0),
	PINMUX_IPSR_MSEL(IP14_7_4,	MSIOF1_SS2_D,		SEL_MSIOF1_3),
	PINMUX_IPSR_MSEL(IP14_7_4,	AUDIO_CLKC_A,		SEL_ADGC_0),
	PINMUX_IPSR_MSEL(IP14_7_4,	SSI_WS2_A,		SEL_SSI2_0),
	PINMUX_IPSR_MSEL(IP14_7_4,	STP_OPWM_0_D,		SEL_SSP1_0_3),
	PINMUX_IPSR_GPSR(IP14_7_4,	AUDIO_CLKOUT_D),
	PINMUX_IPSR_MSEL(IP14_7_4,	SPEEDIN_B,		SEL_SPEED_PULSE_1),

	PINMUX_IPSR_GPSR(IP14_11_8,	MLB_CLK),
	PINMUX_IPSR_MSEL(IP14_11_8,	MSIOF1_SCK_F,		SEL_MSIOF1_5),
	PINMUX_IPSR_MSEL(IP14_11_8,	SCL1_B,			SEL_I2C1_1),

	PINMUX_IPSR_GPSR(IP14_15_12,	MLB_SIG),
	PINMUX_IPSR_MSEL(IP14_15_12,	RX1_B,			SEL_SCIF1_1),
	PINMUX_IPSR_MSEL(IP14_15_12,	MSIOF1_SYNC_F,		SEL_MSIOF1_5),
	PINMUX_IPSR_MSEL(IP14_15_12,	SDA1_B,			SEL_I2C1_1),

	PINMUX_IPSR_GPSR(IP14_19_16,	MLB_DAT),
	PINMUX_IPSR_MSEL(IP14_19_16,	TX1_B,			SEL_SCIF1_1),
	PINMUX_IPSR_MSEL(IP14_19_16,	MSIOF1_RXD_F,		SEL_MSIOF1_5),

	PINMUX_IPSR_GPSR(IP14_23_20,	SSI_SCK01239),
	PINMUX_IPSR_MSEL(IP14_23_20,	MSIOF1_TXD_F,		SEL_MSIOF1_5),

	PINMUX_IPSR_GPSR(IP14_27_24,	SSI_WS01239),
	PINMUX_IPSR_MSEL(IP14_27_24,	MSIOF1_SS1_F,		SEL_MSIOF1_5),

	PINMUX_IPSR_GPSR(IP14_31_28,	SSI_SDATA0),
	PINMUX_IPSR_MSEL(IP14_31_28,	MSIOF1_SS2_F,		SEL_MSIOF1_5),

	/* IPSR15 */
	PINMUX_IPSR_MSEL(IP15_3_0,	SSI_SDATA1_A,		SEL_SSI1_0),

	PINMUX_IPSR_MSEL(IP15_7_4,	SSI_SDATA2_A,		SEL_SSI2_0),
	PINMUX_IPSR_MSEL(IP15_7_4,	SSI_SCK1_B,		SEL_SSI1_1),

	PINMUX_IPSR_GPSR(IP15_11_8,	SSI_SCK349),
	PINMUX_IPSR_MSEL(IP15_11_8,	MSIOF1_SS1_A,		SEL_MSIOF1_0),
	PINMUX_IPSR_MSEL(IP15_11_8,	STP_OPWM_0_A,		SEL_SSP1_0_0),

	PINMUX_IPSR_GPSR(IP15_15_12,	SSI_WS349),
	PINMUX_IPSR_MSEL(IP15_15_12,	HCTS2_N_A,		SEL_HSCIF2_0),
	PINMUX_IPSR_MSEL(IP15_15_12,	MSIOF1_SS2_A,		SEL_MSIOF1_0),
	PINMUX_IPSR_MSEL(IP15_15_12,	STP_IVCXO27_0_A,	SEL_SSP1_0_0),

	PINMUX_IPSR_GPSR(IP15_19_16,	SSI_SDATA3),
	PINMUX_IPSR_MSEL(IP15_19_16,	HRTS2_N_A,		SEL_HSCIF2_0),
	PINMUX_IPSR_MSEL(IP15_19_16,	MSIOF1_TXD_A,		SEL_MSIOF1_0),
	PINMUX_IPSR_MSEL(IP15_19_16,	TS_SCK0_A,		SEL_TSIF0_0),
	PINMUX_IPSR_MSEL(IP15_19_16,	STP_ISCLK_0_A,		SEL_SSP1_0_0),
	PINMUX_IPSR_MSEL(IP15_19_16,	RIF0_D1_A,		SEL_DRIF0_0),
	PINMUX_IPSR_MSEL(IP15_19_16,	RIF2_D0_A,		SEL_DRIF2_0),

	PINMUX_IPSR_GPSR(IP15_23_20,	SSI_SCK4),
	PINMUX_IPSR_MSEL(IP15_23_20,	HRX2_A,			SEL_HSCIF2_0),
	PINMUX_IPSR_MSEL(IP15_23_20,	MSIOF1_SCK_A,		SEL_MSIOF1_0),
	PINMUX_IPSR_MSEL(IP15_23_20,	TS_SDAT0_A,		SEL_TSIF0_0),
	PINMUX_IPSR_MSEL(IP15_23_20,	STP_ISD_0_A,		SEL_SSP1_0_0),
	PINMUX_IPSR_MSEL(IP15_23_20,	RIF0_CLK_A,		SEL_DRIF0_0),
	PINMUX_IPSR_MSEL(IP15_23_20,	RIF2_CLK_A,		SEL_DRIF2_0),

	PINMUX_IPSR_GPSR(IP15_27_24,	SSI_WS4),
	PINMUX_IPSR_MSEL(IP15_27_24,	HTX2_A,			SEL_HSCIF2_0),
	PINMUX_IPSR_MSEL(IP15_27_24,	MSIOF1_SYNC_A,		SEL_MSIOF1_0),
	PINMUX_IPSR_MSEL(IP15_27_24,	TS_SDEN0_A,		SEL_TSIF0_0),
	PINMUX_IPSR_MSEL(IP15_27_24,	STP_ISEN_0_A,		SEL_SSP1_0_0),
	PINMUX_IPSR_MSEL(IP15_27_24,	RIF0_SYNC_A,		SEL_DRIF0_0),
	PINMUX_IPSR_MSEL(IP15_27_24,	RIF2_SYNC_A,		SEL_DRIF2_0),

	PINMUX_IPSR_GPSR(IP15_31_28,	SSI_SDATA4),
	PINMUX_IPSR_MSEL(IP15_31_28,	HSCK2_A,		SEL_HSCIF2_0),
	PINMUX_IPSR_MSEL(IP15_31_28,	MSIOF1_RXD_A,		SEL_MSIOF1_0),
	PINMUX_IPSR_MSEL(IP15_31_28,	TS_SPSYNC0_A,		SEL_TSIF0_0),
	PINMUX_IPSR_MSEL(IP15_31_28,	STP_ISSYNC_0_A,		SEL_SSP1_0_0),
	PINMUX_IPSR_MSEL(IP15_31_28,	RIF0_D0_A,		SEL_DRIF0_0),
	PINMUX_IPSR_MSEL(IP15_31_28,	RIF2_D1_A,		SEL_DRIF2_0),

	/* IPSR16 */
	PINMUX_IPSR_GPSR(IP16_3_0,	SSI_SCK6),
	PINMUX_IPSR_GPSR(IP16_3_0,	USB2_PWEN),
	PINMUX_IPSR_MSEL(IP16_3_0,	SIM0_RST_D,		SEL_SIMCARD_3),

	PINMUX_IPSR_GPSR(IP16_7_4,	SSI_WS6),
	PINMUX_IPSR_GPSR(IP16_7_4,	USB2_OVC),
	PINMUX_IPSR_MSEL(IP16_7_4,	SIM0_D_D,		SEL_SIMCARD_3),

	PINMUX_IPSR_GPSR(IP16_11_8,	SSI_SDATA6),
	PINMUX_IPSR_MSEL(IP16_11_8,	SIM0_CLK_D,		SEL_SIMCARD_3),
	PINMUX_IPSR_GPSR(IP16_11_8,	SATA_DEVSLP_A),

	PINMUX_IPSR_GPSR(IP16_15_12,	SSI_SCK78),
	PINMUX_IPSR_MSEL(IP16_15_12,	HRX2_B,			SEL_HSCIF2_1),
	PINMUX_IPSR_MSEL(IP16_15_12,	MSIOF1_SCK_C,		SEL_MSIOF1_2),
	PINMUX_IPSR_MSEL(IP16_15_12,	TS_SCK1_A,		SEL_TSIF1_0),
	PINMUX_IPSR_MSEL(IP16_15_12,	STP_ISCLK_1_A,		SEL_SSP1_1_0),
	PINMUX_IPSR_MSEL(IP16_15_12,	RIF1_CLK_A,		SEL_DRIF1_0),
	PINMUX_IPSR_MSEL(IP16_15_12,	RIF3_CLK_A,		SEL_DRIF3_0),

	PINMUX_IPSR_GPSR(IP16_19_16,	SSI_WS78),
	PINMUX_IPSR_MSEL(IP16_19_16,	HTX2_B,			SEL_HSCIF2_1),
	PINMUX_IPSR_MSEL(IP16_19_16,	MSIOF1_SYNC_C,		SEL_MSIOF1_2),
	PINMUX_IPSR_MSEL(IP16_19_16,	TS_SDAT1_A,		SEL_TSIF1_0),
	PINMUX_IPSR_MSEL(IP16_19_16,	STP_ISD_1_A,		SEL_SSP1_1_0),
	PINMUX_IPSR_MSEL(IP16_19_16,	RIF1_SYNC_A,		SEL_DRIF1_0),
	PINMUX_IPSR_MSEL(IP16_19_16,	RIF3_SYNC_A,		SEL_DRIF3_0),

	PINMUX_IPSR_GPSR(IP16_23_20,	SSI_SDATA7),
	PINMUX_IPSR_MSEL(IP16_23_20,	HCTS2_N_B,		SEL_HSCIF2_1),
	PINMUX_IPSR_MSEL(IP16_23_20,	MSIOF1_RXD_C,		SEL_MSIOF1_2),
	PINMUX_IPSR_MSEL(IP16_23_20,	TS_SDEN1_A,		SEL_TSIF1_0),
	PINMUX_IPSR_MSEL(IP16_23_20,	STP_ISEN_1_A,		SEL_SSP1_1_0),
	PINMUX_IPSR_MSEL(IP16_23_20,	RIF1_D0_A,		SEL_DRIF1_0),
	PINMUX_IPSR_MSEL(IP16_23_20,	RIF3_D0_A,		SEL_DRIF3_0),
	PINMUX_IPSR_MSEL(IP16_23_20,	TCLK2_A,		SEL_TIMER_TMU2_0),

	PINMUX_IPSR_GPSR(IP16_27_24,	SSI_SDATA8),
	PINMUX_IPSR_MSEL(IP16_27_24,	HRTS2_N_B,		SEL_HSCIF2_1),
	PINMUX_IPSR_MSEL(IP16_27_24,	MSIOF1_TXD_C,		SEL_MSIOF1_2),
	PINMUX_IPSR_MSEL(IP16_27_24,	TS_SPSYNC1_A,		SEL_TSIF1_0),
	PINMUX_IPSR_MSEL(IP16_27_24,	STP_ISSYNC_1_A,		SEL_SSP1_1_0),
	PINMUX_IPSR_MSEL(IP16_27_24,	RIF1_D1_A,		SEL_DRIF1_0),
	PINMUX_IPSR_MSEL(IP16_27_24,	RIF3_D1_A,		SEL_DRIF3_0),

	PINMUX_IPSR_MSEL(IP16_31_28,	SSI_SDATA9_A,		SEL_SSI9_0),
	PINMUX_IPSR_MSEL(IP16_31_28,	HSCK2_B,		SEL_HSCIF2_1),
	PINMUX_IPSR_MSEL(IP16_31_28,	MSIOF1_SS1_C,		SEL_MSIOF1_2),
	PINMUX_IPSR_MSEL(IP16_31_28,	HSCK1_A,		SEL_HSCIF1_0),
	PINMUX_IPSR_MSEL(IP16_31_28,	SSI_WS1_B,		SEL_SSI1_1),
	PINMUX_IPSR_GPSR(IP16_31_28,	SCK1),
	PINMUX_IPSR_MSEL(IP16_31_28,	STP_IVCXO27_1_A,	SEL_SSP1_1_0),
	PINMUX_IPSR_MSEL(IP16_31_28,	SCK5_A,			SEL_SCIF5_0),

	/* IPSR17 */
	PINMUX_IPSR_MSEL(IP17_3_0,	AUDIO_CLKA_A,		SEL_ADGA_0),

	PINMUX_IPSR_MSEL(IP17_7_4,	AUDIO_CLKB_B,		SEL_ADGB_1),
	PINMUX_IPSR_MSEL(IP17_7_4,	SCIF_CLK_A,		SEL_SCIF_0),
	PINMUX_IPSR_MSEL(IP17_7_4,	STP_IVCXO27_1_D,	SEL_SSP1_1_3),
	PINMUX_IPSR_MSEL(IP17_7_4,	REMOCON_A,		SEL_REMOCON_0),
	PINMUX_IPSR_MSEL(IP17_7_4,	TCLK1_A,		SEL_TIMER_TMU1_0),

	PINMUX_IPSR_GPSR(IP17_11_8,	USB0_PWEN),
	PINMUX_IPSR_MSEL(IP17_11_8,	SIM0_RST_C,		SEL_SIMCARD_2),
	PINMUX_IPSR_MSEL(IP17_11_8,	TS_SCK1_D,		SEL_TSIF1_3),
	PINMUX_IPSR_MSEL(IP17_11_8,	STP_ISCLK_1_D,		SEL_SSP1_1_3),
	PINMUX_IPSR_MSEL(IP17_11_8,	BPFCLK_B,		SEL_FM_1),
	PINMUX_IPSR_MSEL(IP17_11_8,	RIF3_CLK_B,		SEL_DRIF3_1),
	PINMUX_IPSR_MSEL(IP17_11_8,	HSCK2_C,		SEL_HSCIF2_2),

	PINMUX_IPSR_GPSR(IP17_15_12,	USB0_OVC),
	PINMUX_IPSR_MSEL(IP17_15_12,	SIM0_D_C,		SEL_SIMCARD_2),
	PINMUX_IPSR_MSEL(IP17_15_12,	TS_SDAT1_D,		SEL_TSIF1_3),
	PINMUX_IPSR_MSEL(IP17_15_12,	STP_ISD_1_D,		SEL_SSP1_1_3),
	PINMUX_IPSR_MSEL(IP17_15_12,	RIF3_SYNC_B,		SEL_DRIF3_1),
	PINMUX_IPSR_MSEL(IP17_15_12,	HRX2_C,			SEL_HSCIF2_2),

	PINMUX_IPSR_GPSR(IP17_19_16,	USB1_PWEN),
	PINMUX_IPSR_MSEL(IP17_19_16,	SIM0_CLK_C,		SEL_SIMCARD_2),
	PINMUX_IPSR_MSEL(IP17_19_16,	SSI_SCK1_A,		SEL_SSI1_0),
	PINMUX_IPSR_MSEL(IP17_19_16,	TS_SCK0_E,		SEL_TSIF0_4),
	PINMUX_IPSR_MSEL(IP17_19_16,	STP_ISCLK_0_E,		SEL_SSP1_0_4),
	PINMUX_IPSR_MSEL(IP17_19_16,	FMCLK_B,		SEL_FM_1),
	PINMUX_IPSR_MSEL(IP17_19_16,	RIF2_CLK_B,		SEL_DRIF2_1),
	PINMUX_IPSR_MSEL(IP17_19_16,	SPEEDIN_A,		SEL_SPEED_PULSE_0),
	PINMUX_IPSR_MSEL(IP17_19_16,	HTX2_C,			SEL_HSCIF2_2),

	PINMUX_IPSR_GPSR(IP17_23_20,	USB1_OVC),
	PINMUX_IPSR_MSEL(IP17_23_20,	MSIOF1_SS2_C,		SEL_MSIOF1_2),
	PINMUX_IPSR_MSEL(IP17_23_20,	SSI_WS1_A,		SEL_SSI1_0),
	PINMUX_IPSR_MSEL(IP17_23_20,	TS_SDAT0_E,		SEL_TSIF0_4),
	PINMUX_IPSR_MSEL(IP17_23_20,	STP_ISD_0_E,		SEL_SSP1_0_4),
	PINMUX_IPSR_MSEL(IP17_23_20,	FMIN_B,			SEL_FM_1),
	PINMUX_IPSR_MSEL(IP17_23_20,	RIF2_SYNC_B,		SEL_DRIF2_1),
	PINMUX_IPSR_MSEL(IP17_23_20,	REMOCON_B,		SEL_REMOCON_1),
	PINMUX_IPSR_MSEL(IP17_23_20,	HCTS2_N_C,		SEL_HSCIF2_2),

	PINMUX_IPSR_GPSR(IP17_27_24,	USB30_PWEN),
	PINMUX_IPSR_GPSR(IP17_27_24,	AUDIO_CLKOUT_B),
	PINMUX_IPSR_MSEL(IP17_27_24,	SSI_SCK2_B,		SEL_SSI2_1),
	PINMUX_IPSR_MSEL(IP17_27_24,	TS_SDEN1_D,		SEL_TSIF1_3),
	PINMUX_IPSR_MSEL(IP17_27_24,	STP_ISEN_1_D,		SEL_SSP1_1_3),
	PINMUX_IPSR_MSEL(IP17_27_24,	STP_OPWM_0_E,		SEL_SSP1_0_4),
	PINMUX_IPSR_MSEL(IP17_27_24,	RIF3_D0_B,		SEL_DRIF3_1),
	PINMUX_IPSR_MSEL(IP17_27_24,	TCLK2_B,		SEL_TIMER_TMU2_1),
	PINMUX_IPSR_GPSR(IP17_27_24,	TPU0TO0),
	PINMUX_IPSR_MSEL(IP17_27_24,	BPFCLK_C,		SEL_FM_2),
	PINMUX_IPSR_MSEL(IP17_27_24,	HRTS2_N_C,		SEL_HSCIF2_2),

	PINMUX_IPSR_GPSR(IP17_31_28,	USB30_OVC),
	PINMUX_IPSR_GPSR(IP17_31_28,	AUDIO_CLKOUT1_B),
	PINMUX_IPSR_MSEL(IP17_31_28,	SSI_WS2_B,		SEL_SSI2_1),
	PINMUX_IPSR_MSEL(IP17_31_28,	TS_SPSYNC1_D,		SEL_TSIF1_3),
	PINMUX_IPSR_MSEL(IP17_31_28,	STP_ISSYNC_1_D,		SEL_SSP1_1_3),
	PINMUX_IPSR_MSEL(IP17_31_28,	STP_IVCXO27_0_E,	SEL_SSP1_0_4),
	PINMUX_IPSR_MSEL(IP17_31_28,	RIF3_D1_B,		SEL_DRIF3_1),
	PINMUX_IPSR_GPSR(IP17_31_28,	FSO_TOE_N),
	PINMUX_IPSR_GPSR(IP17_31_28,	TPU0TO1),

	/* IPSR18 */
	PINMUX_IPSR_GPSR(IP18_3_0,	USB2_CH3_PWEN),
	PINMUX_IPSR_GPSR(IP18_3_0,	AUDIO_CLKOUT2_B),
	PINMUX_IPSR_MSEL(IP18_3_0,	SSI_SCK9_B,		SEL_SSI9_1),
	PINMUX_IPSR_MSEL(IP18_3_0,	TS_SDEN0_E,		SEL_TSIF0_4),
	PINMUX_IPSR_MSEL(IP18_3_0,	STP_ISEN_0_E,		SEL_SSP1_0_4),
	PINMUX_IPSR_MSEL(IP18_3_0,	RIF2_D0_B,		SEL_DRIF2_1),
	PINMUX_IPSR_GPSR(IP18_3_0,	TPU0TO2),
	PINMUX_IPSR_MSEL(IP18_3_0,	FMCLK_C,		SEL_FM_2),
	PINMUX_IPSR_MSEL(IP18_3_0,	FMCLK_D,		SEL_FM_3),

	PINMUX_IPSR_GPSR(IP18_7_4,	USB2_CH3_OVC),
	PINMUX_IPSR_GPSR(IP18_7_4,	AUDIO_CLKOUT3_B),
	PINMUX_IPSR_MSEL(IP18_7_4,	SSI_WS9_B,		SEL_SSI9_1),
	PINMUX_IPSR_MSEL(IP18_7_4,	TS_SPSYNC0_E,		SEL_TSIF0_4),
	PINMUX_IPSR_MSEL(IP18_7_4,	STP_ISSYNC_0_E,		SEL_SSP1_0_4),
	PINMUX_IPSR_MSEL(IP18_7_4,	RIF2_D1_B,		SEL_DRIF2_1),
	PINMUX_IPSR_GPSR(IP18_7_4,	TPU0TO3),
	PINMUX_IPSR_MSEL(IP18_7_4,	FMIN_C,			SEL_FM_2),
	PINMUX_IPSR_MSEL(IP18_7_4,	FMIN_D,			SEL_FM_3),

/*
 * Static pins can not be muxed between different functions but
 * still need mark entries in the pinmux list. Add each static
 * pin to the list without an associated function. The sh-pfc
 * core will do the right thing and skip trying to mux the pin
 * while still applying configuration to it.
 */
#define FM(x)	PINMUX_DATA(x##_MARK, 0),
	PINMUX_STATIC
#undef FM
};

/*
 * Pins not associated with a GPIO port.
 */
enum {
	GP_ASSIGN_LAST(),
	NOGP_ALL(),
};

static const struct sh_pfc_pin pinmux_pins[] = {
	PINMUX_GPIO_GP_ALL(),
	PINMUX_NOGP_ALL(),
};

/* - AUDIO CLOCK ------------------------------------------------------------ */
static const unsigned int audio_clk_a_a_pins[] = {
	/* CLK A */
	RCAR_GP_PIN(6, 22),
};
static const unsigned int audio_clk_a_a_mux[] = {
	AUDIO_CLKA_A_MARK,
};
static const unsigned int audio_clk_a_b_pins[] = {
	/* CLK A */
	RCAR_GP_PIN(5, 4),
};
static const unsigned int audio_clk_a_b_mux[] = {
	AUDIO_CLKA_B_MARK,
};
static const unsigned int audio_clk_a_c_pins[] = {
	/* CLK A */
	RCAR_GP_PIN(5, 19),
};
static const unsigned int audio_clk_a_c_mux[] = {
	AUDIO_CLKA_C_MARK,
};
static const unsigned int audio_clk_b_a_pins[] = {
	/* CLK B */
	RCAR_GP_PIN(5, 12),
};
static const unsigned int audio_clk_b_a_mux[] = {
	AUDIO_CLKB_A_MARK,
};
static const unsigned int audio_clk_b_b_pins[] = {
	/* CLK B */
	RCAR_GP_PIN(6, 23),
};
static const unsigned int audio_clk_b_b_mux[] = {
	AUDIO_CLKB_B_MARK,
};
static const unsigned int audio_clk_c_a_pins[] = {
	/* CLK C */
	RCAR_GP_PIN(5, 21),
};
static const unsigned int audio_clk_c_a_mux[] = {
	AUDIO_CLKC_A_MARK,
};
static const unsigned int audio_clk_c_b_pins[] = {
	/* CLK C */
	RCAR_GP_PIN(5, 0),
};
static const unsigned int audio_clk_c_b_mux[] = {
	AUDIO_CLKC_B_MARK,
};
static const unsigned int audio_clkout_a_pins[] = {
	/* CLKOUT */
	RCAR_GP_PIN(5, 18),
};
static const unsigned int audio_clkout_a_mux[] = {
	AUDIO_CLKOUT_A_MARK,
};
static const unsigned int audio_clkout_b_pins[] = {
	/* CLKOUT */
	RCAR_GP_PIN(6, 28),
};
static const unsigned int audio_clkout_b_mux[] = {
	AUDIO_CLKOUT_B_MARK,
};
static const unsigned int audio_clkout_c_pins[] = {
	/* CLKOUT */
	RCAR_GP_PIN(5, 3),
};
static const unsigned int audio_clkout_c_mux[] = {
	AUDIO_CLKOUT_C_MARK,
};
static const unsigned int audio_clkout_d_pins[] = {
	/* CLKOUT */
	RCAR_GP_PIN(5, 21),
};
static const unsigned int audio_clkout_d_mux[] = {
	AUDIO_CLKOUT_D_MARK,
};
static const unsigned int audio_clkout1_a_pins[] = {
	/* CLKOUT1 */
	RCAR_GP_PIN(5, 15),
};
static const unsigned int audio_clkout1_a_mux[] = {
	AUDIO_CLKOUT1_A_MARK,
};
static const unsigned int audio_clkout1_b_pins[] = {
	/* CLKOUT1 */
	RCAR_GP_PIN(6, 29),
};
static const unsigned int audio_clkout1_b_mux[] = {
	AUDIO_CLKOUT1_B_MARK,
};
static const unsigned int audio_clkout2_a_pins[] = {
	/* CLKOUT2 */
	RCAR_GP_PIN(5, 16),
};
static const unsigned int audio_clkout2_a_mux[] = {
	AUDIO_CLKOUT2_A_MARK,
};
static const unsigned int audio_clkout2_b_pins[] = {
	/* CLKOUT2 */
	RCAR_GP_PIN(6, 30),
};
static const unsigned int audio_clkout2_b_mux[] = {
	AUDIO_CLKOUT2_B_MARK,
};
static const unsigned int audio_clkout3_a_pins[] = {
	/* CLKOUT3 */
	RCAR_GP_PIN(5, 19),
};
static const unsigned int audio_clkout3_a_mux[] = {
	AUDIO_CLKOUT3_A_MARK,
};
static const unsigned int audio_clkout3_b_pins[] = {
	/* CLKOUT3 */
	RCAR_GP_PIN(6, 31),
};
static const unsigned int audio_clkout3_b_mux[] = {
	AUDIO_CLKOUT3_B_MARK,
};

/* - EtherAVB --------------------------------------------------------------- */
static const unsigned int avb_link_pins[] = {
	/* AVB_LINK */
	RCAR_GP_PIN(2, 12),
};
static const unsigned int avb_link_mux[] = {
	AVB_LINK_MARK,
};
static const unsigned int avb_magic_pins[] = {
	/* AVB_MAGIC_ */
	RCAR_GP_PIN(2, 10),
};
static const unsigned int avb_magic_mux[] = {
	AVB_MAGIC_MARK,
};
static const unsigned int avb_phy_int_pins[] = {
	/* AVB_PHY_INT */
	RCAR_GP_PIN(2, 11),
};
static const unsigned int avb_phy_int_mux[] = {
	AVB_PHY_INT_MARK,
};
static const unsigned int avb_mdio_pins[] = {
	/* AVB_MDC, AVB_MDIO */
	RCAR_GP_PIN(2, 9), PIN_AVB_MDIO,
};
static const unsigned int avb_mdio_mux[] = {
	AVB_MDC_MARK, AVB_MDIO_MARK,
};
static const unsigned int avb_mii_pins[] = {
	/*
	 * AVB_TX_CTL, AVB_TXC, AVB_TD0,
	 * AVB_TD1, AVB_TD2, AVB_TD3,
	 * AVB_RX_CTL, AVB_RXC, AVB_RD0,
	 * AVB_RD1, AVB_RD2, AVB_RD3,
	 * AVB_TXCREFCLK
	 */
	PIN_AVB_TX_CTL, PIN_AVB_TXC, PIN_AVB_TD0,
	PIN_AVB_TD1, PIN_AVB_TD2, PIN_AVB_TD3,
	PIN_AVB_RX_CTL, PIN_AVB_RXC, PIN_AVB_RD0,
	PIN_AVB_RD1, PIN_AVB_RD2, PIN_AVB_RD3,
	PIN_AVB_TXCREFCLK,
};
static const unsigned int avb_mii_mux[] = {
	AVB_TX_CTL_MARK, AVB_TXC_MARK, AVB_TD0_MARK,
	AVB_TD1_MARK, AVB_TD2_MARK, AVB_TD3_MARK,
	AVB_RX_CTL_MARK, AVB_RXC_MARK, AVB_RD0_MARK,
	AVB_RD1_MARK, AVB_RD2_MARK, AVB_RD3_MARK,
	AVB_TXCREFCLK_MARK,
};
static const unsigned int avb_avtp_pps_pins[] = {
	/* AVB_AVTP_PPS */
	RCAR_GP_PIN(2, 6),
};
static const unsigned int avb_avtp_pps_mux[] = {
	AVB_AVTP_PPS_MARK,
};
static const unsigned int avb_avtp_match_a_pins[] = {
	/* AVB_AVTP_MATCH_A */
	RCAR_GP_PIN(2, 13),
};
static const unsigned int avb_avtp_match_a_mux[] = {
	AVB_AVTP_MATCH_A_MARK,
};
static const unsigned int avb_avtp_capture_a_pins[] = {
	/* AVB_AVTP_CAPTURE_A */
	RCAR_GP_PIN(2, 14),
};
static const unsigned int avb_avtp_capture_a_mux[] = {
	AVB_AVTP_CAPTURE_A_MARK,
};
static const unsigned int avb_avtp_match_b_pins[] = {
	/*  AVB_AVTP_MATCH_B */
	RCAR_GP_PIN(1, 8),
};
static const unsigned int avb_avtp_match_b_mux[] = {
	AVB_AVTP_MATCH_B_MARK,
};
static const unsigned int avb_avtp_capture_b_pins[] = {
	/* AVB_AVTP_CAPTURE_B */
	RCAR_GP_PIN(1, 11),
};
static const unsigned int avb_avtp_capture_b_mux[] = {
	AVB_AVTP_CAPTURE_B_MARK,
};

/* - CAN ------------------------------------------------------------------ */
static const unsigned int can0_data_a_pins[] = {
	/* TX, RX */
	RCAR_GP_PIN(1, 23),	RCAR_GP_PIN(1, 24),
};
static const unsigned int can0_data_a_mux[] = {
	CAN0_TX_A_MARK,		CAN0_RX_A_MARK,
};
static const unsigned int can0_data_b_pins[] = {
	/* TX, RX */
	RCAR_GP_PIN(2, 0),	RCAR_GP_PIN(2, 1),
};
static const unsigned int can0_data_b_mux[] = {
	CAN0_TX_B_MARK,		CAN0_RX_B_MARK,
};
static const unsigned int can1_data_pins[] = {
	/* TX, RX */
	RCAR_GP_PIN(1, 22),	RCAR_GP_PIN(1, 26),
};
static const unsigned int can1_data_mux[] = {
	CAN1_TX_MARK,		CAN1_RX_MARK,
};

/* - CAN Clock -------------------------------------------------------------- */
static const unsigned int can_clk_pins[] = {
	/* CLK */
	RCAR_GP_PIN(1, 25),
};
static const unsigned int can_clk_mux[] = {
	CAN_CLK_MARK,
};

/* - CAN FD --------------------------------------------------------------- */
static const unsigned int canfd0_data_a_pins[] = {
	/* TX, RX */
	RCAR_GP_PIN(1, 23),     RCAR_GP_PIN(1, 24),
};
static const unsigned int canfd0_data_a_mux[] = {
	CANFD0_TX_A_MARK,       CANFD0_RX_A_MARK,
};
static const unsigned int canfd0_data_b_pins[] = {
	/* TX, RX */
	RCAR_GP_PIN(2, 0),      RCAR_GP_PIN(2, 1),
};
static const unsigned int canfd0_data_b_mux[] = {
	CANFD0_TX_B_MARK,       CANFD0_RX_B_MARK,
};
static const unsigned int canfd1_data_pins[] = {
	/* TX, RX */
	RCAR_GP_PIN(1, 22),     RCAR_GP_PIN(1, 26),
};
static const unsigned int canfd1_data_mux[] = {
	CANFD1_TX_MARK,         CANFD1_RX_MARK,
};

#ifdef CONFIG_PINCTRL_PFC_R8A77951
/* - DRIF0 --------------------------------------------------------------- */
static const unsigned int drif0_ctrl_a_pins[] = {
	/* CLK, SYNC */
	RCAR_GP_PIN(6, 8), RCAR_GP_PIN(6, 9),
};
static const unsigned int drif0_ctrl_a_mux[] = {
	RIF0_CLK_A_MARK, RIF0_SYNC_A_MARK,
};
static const unsigned int drif0_data0_a_pins[] = {
	/* D0 */
	RCAR_GP_PIN(6, 10),
};
static const unsigned int drif0_data0_a_mux[] = {
	RIF0_D0_A_MARK,
};
static const unsigned int drif0_data1_a_pins[] = {
	/* D1 */
	RCAR_GP_PIN(6, 7),
};
static const unsigned int drif0_data1_a_mux[] = {
	RIF0_D1_A_MARK,
};
static const unsigned int drif0_ctrl_b_pins[] = {
	/* CLK, SYNC */
	RCAR_GP_PIN(5, 0), RCAR_GP_PIN(5, 4),
};
static const unsigned int drif0_ctrl_b_mux[] = {
	RIF0_CLK_B_MARK, RIF0_SYNC_B_MARK,
};
static const unsigned int drif0_data0_b_pins[] = {
	/* D0 */
	RCAR_GP_PIN(5, 1),
};
static const unsigned int drif0_data0_b_mux[] = {
	RIF0_D0_B_MARK,
};
static const unsigned int drif0_data1_b_pins[] = {
	/* D1 */
	RCAR_GP_PIN(5, 2),
};
static const unsigned int drif0_data1_b_mux[] = {
	RIF0_D1_B_MARK,
};
static const unsigned int drif0_ctrl_c_pins[] = {
	/* CLK, SYNC */
	RCAR_GP_PIN(5, 12), RCAR_GP_PIN(5, 15),
};
static const unsigned int drif0_ctrl_c_mux[] = {
	RIF0_CLK_C_MARK, RIF0_SYNC_C_MARK,
};
static const unsigned int drif0_data0_c_pins[] = {
	/* D0 */
	RCAR_GP_PIN(5, 13),
};
static const unsigned int drif0_data0_c_mux[] = {
	RIF0_D0_C_MARK,
};
static const unsigned int drif0_data1_c_pins[] = {
	/* D1 */
	RCAR_GP_PIN(5, 14),
};
static const unsigned int drif0_data1_c_mux[] = {
	RIF0_D1_C_MARK,
};
/* - DRIF1 --------------------------------------------------------------- */
static const unsigned int drif1_ctrl_a_pins[] = {
	/* CLK, SYNC */
	RCAR_GP_PIN(6, 17), RCAR_GP_PIN(6, 18),
};
static const unsigned int drif1_ctrl_a_mux[] = {
	RIF1_CLK_A_MARK, RIF1_SYNC_A_MARK,
};
static const unsigned int drif1_data0_a_pins[] = {
	/* D0 */
	RCAR_GP_PIN(6, 19),
};
static const unsigned int drif1_data0_a_mux[] = {
	RIF1_D0_A_MARK,
};
static const unsigned int drif1_data1_a_pins[] = {
	/* D1 */
	RCAR_GP_PIN(6, 20),
};
static const unsigned int drif1_data1_a_mux[] = {
	RIF1_D1_A_MARK,
};
static const unsigned int drif1_ctrl_b_pins[] = {
	/* CLK, SYNC */
	RCAR_GP_PIN(5, 9), RCAR_GP_PIN(5, 3),
};
static const unsigned int drif1_ctrl_b_mux[] = {
	RIF1_CLK_B_MARK, RIF1_SYNC_B_MARK,
};
static const unsigned int drif1_data0_b_pins[] = {
	/* D0 */
	RCAR_GP_PIN(5, 7),
};
static const unsigned int drif1_data0_b_mux[] = {
	RIF1_D0_B_MARK,
};
static const unsigned int drif1_data1_b_pins[] = {
	/* D1 */
	RCAR_GP_PIN(5, 8),
};
static const unsigned int drif1_data1_b_mux[] = {
	RIF1_D1_B_MARK,
};
static const unsigned int drif1_ctrl_c_pins[] = {
	/* CLK, SYNC */
	RCAR_GP_PIN(5, 5), RCAR_GP_PIN(5, 11),
};
static const unsigned int drif1_ctrl_c_mux[] = {
	RIF1_CLK_C_MARK, RIF1_SYNC_C_MARK,
};
static const unsigned int drif1_data0_c_pins[] = {
	/* D0 */
	RCAR_GP_PIN(5, 6),
};
static const unsigned int drif1_data0_c_mux[] = {
	RIF1_D0_C_MARK,
};
static const unsigned int drif1_data1_c_pins[] = {
	/* D1 */
	RCAR_GP_PIN(5, 10),
};
static const unsigned int drif1_data1_c_mux[] = {
	RIF1_D1_C_MARK,
};
/* - DRIF2 --------------------------------------------------------------- */
static const unsigned int drif2_ctrl_a_pins[] = {
	/* CLK, SYNC */
	RCAR_GP_PIN(6, 8), RCAR_GP_PIN(6, 9),
};
static const unsigned int drif2_ctrl_a_mux[] = {
	RIF2_CLK_A_MARK, RIF2_SYNC_A_MARK,
};
static const unsigned int drif2_data0_a_pins[] = {
	/* D0 */
	RCAR_GP_PIN(6, 7),
};
static const unsigned int drif2_data0_a_mux[] = {
	RIF2_D0_A_MARK,
};
static const unsigned int drif2_data1_a_pins[] = {
	/* D1 */
	RCAR_GP_PIN(6, 10),
};
static const unsigned int drif2_data1_a_mux[] = {
	RIF2_D1_A_MARK,
};
static const unsigned int drif2_ctrl_b_pins[] = {
	/* CLK, SYNC */
	RCAR_GP_PIN(6, 26), RCAR_GP_PIN(6, 27),
};
static const unsigned int drif2_ctrl_b_mux[] = {
	RIF2_CLK_B_MARK, RIF2_SYNC_B_MARK,
};
static const unsigned int drif2_data0_b_pins[] = {
	/* D0 */
	RCAR_GP_PIN(6, 30),
};
static const unsigned int drif2_data0_b_mux[] = {
	RIF2_D0_B_MARK,
};
static const unsigned int drif2_data1_b_pins[] = {
	/* D1 */
	RCAR_GP_PIN(6, 31),
};
static const unsigned int drif2_data1_b_mux[] = {
	RIF2_D1_B_MARK,
};
/* - DRIF3 --------------------------------------------------------------- */
static const unsigned int drif3_ctrl_a_pins[] = {
	/* CLK, SYNC */
	RCAR_GP_PIN(6, 17), RCAR_GP_PIN(6, 18),
};
static const unsigned int drif3_ctrl_a_mux[] = {
	RIF3_CLK_A_MARK, RIF3_SYNC_A_MARK,
};
static const unsigned int drif3_data0_a_pins[] = {
	/* D0 */
	RCAR_GP_PIN(6, 19),
};
static const unsigned int drif3_data0_a_mux[] = {
	RIF3_D0_A_MARK,
};
static const unsigned int drif3_data1_a_pins[] = {
	/* D1 */
	RCAR_GP_PIN(6, 20),
};
static const unsigned int drif3_data1_a_mux[] = {
	RIF3_D1_A_MARK,
};
static const unsigned int drif3_ctrl_b_pins[] = {
	/* CLK, SYNC */
	RCAR_GP_PIN(6, 24), RCAR_GP_PIN(6, 25),
};
static const unsigned int drif3_ctrl_b_mux[] = {
	RIF3_CLK_B_MARK, RIF3_SYNC_B_MARK,
};
static const unsigned int drif3_data0_b_pins[] = {
	/* D0 */
	RCAR_GP_PIN(6, 28),
};
static const unsigned int drif3_data0_b_mux[] = {
	RIF3_D0_B_MARK,
};
static const unsigned int drif3_data1_b_pins[] = {
	/* D1 */
	RCAR_GP_PIN(6, 29),
};
static const unsigned int drif3_data1_b_mux[] = {
	RIF3_D1_B_MARK,
};
#endif /* CONFIG_PINCTRL_PFC_R8A77951 */

/* - DU --------------------------------------------------------------------- */
static const unsigned int du_rgb666_pins[] = {
	/* R[7:2], G[7:2], B[7:2] */
	RCAR_GP_PIN(0, 15), RCAR_GP_PIN(0, 14), RCAR_GP_PIN(0, 13),
	RCAR_GP_PIN(0, 12), RCAR_GP_PIN(0, 11), RCAR_GP_PIN(0, 10),
	RCAR_GP_PIN(1, 15), RCAR_GP_PIN(1, 14), RCAR_GP_PIN(1, 13),
	RCAR_GP_PIN(1, 12), RCAR_GP_PIN(1, 19), RCAR_GP_PIN(1, 18),
	RCAR_GP_PIN(1, 7),  RCAR_GP_PIN(1, 6),  RCAR_GP_PIN(1, 5),
	RCAR_GP_PIN(1, 4),  RCAR_GP_PIN(1, 3),  RCAR_GP_PIN(1, 2),
};
static const unsigned int du_rgb666_mux[] = {
	DU_DR7_MARK, DU_DR6_MARK, DU_DR5_MARK, DU_DR4_MARK,
	DU_DR3_MARK, DU_DR2_MARK,
	DU_DG7_MARK, DU_DG6_MARK, DU_DG5_MARK, DU_DG4_MARK,
	DU_DG3_MARK, DU_DG2_MARK,
	DU_DB7_MARK, DU_DB6_MARK, DU_DB5_MARK, DU_DB4_MARK,
	DU_DB3_MARK, DU_DB2_MARK,
};
static const unsigned int du_rgb888_pins[] = {
	/* R[7:0], G[7:0], B[7:0] */
	RCAR_GP_PIN(0, 15), RCAR_GP_PIN(0, 14), RCAR_GP_PIN(0, 13),
	RCAR_GP_PIN(0, 12), RCAR_GP_PIN(0, 11), RCAR_GP_PIN(0, 10),
	RCAR_GP_PIN(0, 9),  RCAR_GP_PIN(0, 8),
	RCAR_GP_PIN(1, 15), RCAR_GP_PIN(1, 14), RCAR_GP_PIN(1, 13),
	RCAR_GP_PIN(1, 12), RCAR_GP_PIN(1, 19), RCAR_GP_PIN(1, 18),
	RCAR_GP_PIN(1, 17), RCAR_GP_PIN(1, 16),
	RCAR_GP_PIN(1, 7),  RCAR_GP_PIN(1, 6),  RCAR_GP_PIN(1, 5),
	RCAR_GP_PIN(1, 4),  RCAR_GP_PIN(1, 3),  RCAR_GP_PIN(1, 2),
	RCAR_GP_PIN(1, 1),  RCAR_GP_PIN(1, 0),
};
static const unsigned int du_rgb888_mux[] = {
	DU_DR7_MARK, DU_DR6_MARK, DU_DR5_MARK, DU_DR4_MARK,
	DU_DR3_MARK, DU_DR2_MARK, DU_DR1_MARK, DU_DR0_MARK,
	DU_DG7_MARK, DU_DG6_MARK, DU_DG5_MARK, DU_DG4_MARK,
	DU_DG3_MARK, DU_DG2_MARK, DU_DG1_MARK, DU_DG0_MARK,
	DU_DB7_MARK, DU_DB6_MARK, DU_DB5_MARK, DU_DB4_MARK,
	DU_DB3_MARK, DU_DB2_MARK, DU_DB1_MARK, DU_DB0_MARK,
};
static const unsigned int du_clk_out_0_pins[] = {
	/* CLKOUT */
	RCAR_GP_PIN(1, 27),
};
static const unsigned int du_clk_out_0_mux[] = {
	DU_DOTCLKOUT0_MARK
};
static const unsigned int du_clk_out_1_pins[] = {
	/* CLKOUT */
	RCAR_GP_PIN(2, 3),
};
static const unsigned int du_clk_out_1_mux[] = {
	DU_DOTCLKOUT1_MARK
};
static const unsigned int du_sync_pins[] = {
	/* EXVSYNC/VSYNC, EXHSYNC/HSYNC */
	RCAR_GP_PIN(2, 5), RCAR_GP_PIN(2, 4),
};
static const unsigned int du_sync_mux[] = {
	DU_EXVSYNC_DU_VSYNC_MARK, DU_EXHSYNC_DU_HSYNC_MARK
};
static const unsigned int du_oddf_pins[] = {
	/* EXDISP/EXODDF/EXCDE */
	RCAR_GP_PIN(2, 2),
};
static const unsigned int du_oddf_mux[] = {
	DU_EXODDF_DU_ODDF_DISP_CDE_MARK,
};
static const unsigned int du_cde_pins[] = {
	/* CDE */
	RCAR_GP_PIN(2, 0),
};
static const unsigned int du_cde_mux[] = {
	DU_CDE_MARK,
};
static const unsigned int du_disp_pins[] = {
	/* DISP */
	RCAR_GP_PIN(2, 1),
};
static const unsigned int du_disp_mux[] = {
	DU_DISP_MARK,
};

/* - HSCIF0 ----------------------------------------------------------------- */
static const unsigned int hscif0_data_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(5, 13), RCAR_GP_PIN(5, 14),
};
static const unsigned int hscif0_data_mux[] = {
	HRX0_MARK, HTX0_MARK,
};
static const unsigned int hscif0_clk_pins[] = {
	/* SCK */
	RCAR_GP_PIN(5, 12),
};
static const unsigned int hscif0_clk_mux[] = {
	HSCK0_MARK,
};
static const unsigned int hscif0_ctrl_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(5, 16), RCAR_GP_PIN(5, 15),
};
static const unsigned int hscif0_ctrl_mux[] = {
	HRTS0_N_MARK, HCTS0_N_MARK,
};
/* - HSCIF1 ----------------------------------------------------------------- */
static const unsigned int hscif1_data_a_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(5, 5), RCAR_GP_PIN(5, 6),
};
static const unsigned int hscif1_data_a_mux[] = {
	HRX1_A_MARK, HTX1_A_MARK,
};
static const unsigned int hscif1_clk_a_pins[] = {
	/* SCK */
	RCAR_GP_PIN(6, 21),
};
static const unsigned int hscif1_clk_a_mux[] = {
	HSCK1_A_MARK,
};
static const unsigned int hscif1_ctrl_a_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(5, 8), RCAR_GP_PIN(5, 7),
};
static const unsigned int hscif1_ctrl_a_mux[] = {
	HRTS1_N_A_MARK, HCTS1_N_A_MARK,
};

static const unsigned int hscif1_data_b_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(5, 1), RCAR_GP_PIN(5, 2),
};
static const unsigned int hscif1_data_b_mux[] = {
	HRX1_B_MARK, HTX1_B_MARK,
};
static const unsigned int hscif1_clk_b_pins[] = {
	/* SCK */
	RCAR_GP_PIN(5, 0),
};
static const unsigned int hscif1_clk_b_mux[] = {
	HSCK1_B_MARK,
};
static const unsigned int hscif1_ctrl_b_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(5, 4), RCAR_GP_PIN(5, 3),
};
static const unsigned int hscif1_ctrl_b_mux[] = {
	HRTS1_N_B_MARK, HCTS1_N_B_MARK,
};
/* - HSCIF2 ----------------------------------------------------------------- */
static const unsigned int hscif2_data_a_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(6, 8), RCAR_GP_PIN(6, 9),
};
static const unsigned int hscif2_data_a_mux[] = {
	HRX2_A_MARK, HTX2_A_MARK,
};
static const unsigned int hscif2_clk_a_pins[] = {
	/* SCK */
	RCAR_GP_PIN(6, 10),
};
static const unsigned int hscif2_clk_a_mux[] = {
	HSCK2_A_MARK,
};
static const unsigned int hscif2_ctrl_a_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(6, 7), RCAR_GP_PIN(6, 6),
};
static const unsigned int hscif2_ctrl_a_mux[] = {
	HRTS2_N_A_MARK, HCTS2_N_A_MARK,
};

static const unsigned int hscif2_data_b_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(6, 17), RCAR_GP_PIN(6, 18),
};
static const unsigned int hscif2_data_b_mux[] = {
	HRX2_B_MARK, HTX2_B_MARK,
};
static const unsigned int hscif2_clk_b_pins[] = {
	/* SCK */
	RCAR_GP_PIN(6, 21),
};
static const unsigned int hscif2_clk_b_mux[] = {
	HSCK2_B_MARK,
};
static const unsigned int hscif2_ctrl_b_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(6, 20), RCAR_GP_PIN(6, 19),
};
static const unsigned int hscif2_ctrl_b_mux[] = {
	HRTS2_N_B_MARK, HCTS2_N_B_MARK,
};

static const unsigned int hscif2_data_c_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(6, 25), RCAR_GP_PIN(6, 26),
};
static const unsigned int hscif2_data_c_mux[] = {
	HRX2_C_MARK, HTX2_C_MARK,
};
static const unsigned int hscif2_clk_c_pins[] = {
	/* SCK */
	RCAR_GP_PIN(6, 24),
};
static const unsigned int hscif2_clk_c_mux[] = {
	HSCK2_C_MARK,
};
static const unsigned int hscif2_ctrl_c_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(6, 28), RCAR_GP_PIN(6, 27),
};
static const unsigned int hscif2_ctrl_c_mux[] = {
	HRTS2_N_C_MARK, HCTS2_N_C_MARK,
};
/* - HSCIF3 ----------------------------------------------------------------- */
static const unsigned int hscif3_data_a_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(1, 23), RCAR_GP_PIN(1, 24),
};
static const unsigned int hscif3_data_a_mux[] = {
	HRX3_A_MARK, HTX3_A_MARK,
};
static const unsigned int hscif3_clk_pins[] = {
	/* SCK */
	RCAR_GP_PIN(1, 22),
};
static const unsigned int hscif3_clk_mux[] = {
	HSCK3_MARK,
};
static const unsigned int hscif3_ctrl_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(1, 26), RCAR_GP_PIN(1, 25),
};
static const unsigned int hscif3_ctrl_mux[] = {
	HRTS3_N_MARK, HCTS3_N_MARK,
};

static const unsigned int hscif3_data_b_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(0, 10), RCAR_GP_PIN(0, 11),
};
static const unsigned int hscif3_data_b_mux[] = {
	HRX3_B_MARK, HTX3_B_MARK,
};
static const unsigned int hscif3_data_c_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(0, 14), RCAR_GP_PIN(0, 15),
};
static const unsigned int hscif3_data_c_mux[] = {
	HRX3_C_MARK, HTX3_C_MARK,
};
static const unsigned int hscif3_data_d_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(2, 7), RCAR_GP_PIN(2, 8),
};
static const unsigned int hscif3_data_d_mux[] = {
	HRX3_D_MARK, HTX3_D_MARK,
};
/* - HSCIF4 ----------------------------------------------------------------- */
static const unsigned int hscif4_data_a_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(1, 12), RCAR_GP_PIN(1, 13),
};
static const unsigned int hscif4_data_a_mux[] = {
	HRX4_A_MARK, HTX4_A_MARK,
};
static const unsigned int hscif4_clk_pins[] = {
	/* SCK */
	RCAR_GP_PIN(1, 11),
};
static const unsigned int hscif4_clk_mux[] = {
	HSCK4_MARK,
};
static const unsigned int hscif4_ctrl_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(1, 15), RCAR_GP_PIN(1, 14),
};
static const unsigned int hscif4_ctrl_mux[] = {
	HRTS4_N_MARK, HCTS4_N_MARK,
};

static const unsigned int hscif4_data_b_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(1, 8), RCAR_GP_PIN(1, 11),
};
static const unsigned int hscif4_data_b_mux[] = {
	HRX4_B_MARK, HTX4_B_MARK,
};

/* - I2C -------------------------------------------------------------------- */
static const unsigned int i2c0_pins[] = {
	/* SCL, SDA */
	RCAR_GP_PIN(3, 14), RCAR_GP_PIN(3, 15),
};

static const unsigned int i2c0_mux[] = {
	SCL0_MARK, SDA0_MARK,
};

static const unsigned int i2c1_a_pins[] = {
	/* SDA, SCL */
	RCAR_GP_PIN(5, 11), RCAR_GP_PIN(5, 10),
};
static const unsigned int i2c1_a_mux[] = {
	SDA1_A_MARK, SCL1_A_MARK,
};
static const unsigned int i2c1_b_pins[] = {
	/* SDA, SCL */
	RCAR_GP_PIN(5, 24), RCAR_GP_PIN(5, 23),
};
static const unsigned int i2c1_b_mux[] = {
	SDA1_B_MARK, SCL1_B_MARK,
};
static const unsigned int i2c2_a_pins[] = {
	/* SDA, SCL */
	RCAR_GP_PIN(5, 0), RCAR_GP_PIN(5, 4),
};
static const unsigned int i2c2_a_mux[] = {
	SDA2_A_MARK, SCL2_A_MARK,
};
static const unsigned int i2c2_b_pins[] = {
	/* SDA, SCL */
	RCAR_GP_PIN(3, 13), RCAR_GP_PIN(3, 12),
};
static const unsigned int i2c2_b_mux[] = {
	SDA2_B_MARK, SCL2_B_MARK,
};

static const unsigned int i2c3_pins[] = {
	/* SCL, SDA */
	RCAR_GP_PIN(2, 7), RCAR_GP_PIN(2, 8),
};

static const unsigned int i2c3_mux[] = {
	SCL3_MARK, SDA3_MARK,
};

static const unsigned int i2c5_pins[] = {
	/* SCL, SDA */
	RCAR_GP_PIN(2, 13), RCAR_GP_PIN(2, 14),
};

static const unsigned int i2c5_mux[] = {
	SCL5_MARK, SDA5_MARK,
};

static const unsigned int i2c6_a_pins[] = {
	/* SDA, SCL */
	RCAR_GP_PIN(1, 8), RCAR_GP_PIN(1, 11),
};
static const unsigned int i2c6_a_mux[] = {
	SDA6_A_MARK, SCL6_A_MARK,
};
static const unsigned int i2c6_b_pins[] = {
	/* SDA, SCL */
	RCAR_GP_PIN(1, 26), RCAR_GP_PIN(1, 25),
};
static const unsigned int i2c6_b_mux[] = {
	SDA6_B_MARK, SCL6_B_MARK,
};
static const unsigned int i2c6_c_pins[] = {
	/* SDA, SCL */
	RCAR_GP_PIN(0, 15), RCAR_GP_PIN(0, 14),
};
static const unsigned int i2c6_c_mux[] = {
	SDA6_C_MARK, SCL6_C_MARK,
};

/* - INTC-EX ---------------------------------------------------------------- */
static const unsigned int intc_ex_irq0_pins[] = {
	/* IRQ0 */
	RCAR_GP_PIN(2, 0),
};
static const unsigned int intc_ex_irq0_mux[] = {
	IRQ0_MARK,
};
static const unsigned int intc_ex_irq1_pins[] = {
	/* IRQ1 */
	RCAR_GP_PIN(2, 1),
};
static const unsigned int intc_ex_irq1_mux[] = {
	IRQ1_MARK,
};
static const unsigned int intc_ex_irq2_pins[] = {
	/* IRQ2 */
	RCAR_GP_PIN(2, 2),
};
static const unsigned int intc_ex_irq2_mux[] = {
	IRQ2_MARK,
};
static const unsigned int intc_ex_irq3_pins[] = {
	/* IRQ3 */
	RCAR_GP_PIN(2, 3),
};
static const unsigned int intc_ex_irq3_mux[] = {
	IRQ3_MARK,
};
static const unsigned int intc_ex_irq4_pins[] = {
	/* IRQ4 */
	RCAR_GP_PIN(2, 4),
};
static const unsigned int intc_ex_irq4_mux[] = {
	IRQ4_MARK,
};
static const unsigned int intc_ex_irq5_pins[] = {
	/* IRQ5 */
	RCAR_GP_PIN(2, 5),
};
static const unsigned int intc_ex_irq5_mux[] = {
	IRQ5_MARK,
};

#ifdef CONFIG_PINCTRL_PFC_R8A77951
/* - MLB+ ------------------------------------------------------------------- */
static const unsigned int mlb_3pin_pins[] = {
	RCAR_GP_PIN(5, 23), RCAR_GP_PIN(5, 24), RCAR_GP_PIN(5, 25),
};
static const unsigned int mlb_3pin_mux[] = {
	MLB_CLK_MARK, MLB_SIG_MARK, MLB_DAT_MARK,
};
#endif /* CONFIG_PINCTRL_PFC_R8A77951 */

/* - MSIOF0 ----------------------------------------------------------------- */
static const unsigned int msiof0_clk_pins[] = {
	/* SCK */
	RCAR_GP_PIN(5, 17),
};
static const unsigned int msiof0_clk_mux[] = {
	MSIOF0_SCK_MARK,
};
static const unsigned int msiof0_sync_pins[] = {
	/* SYNC */
	RCAR_GP_PIN(5, 18),
};
static const unsigned int msiof0_sync_mux[] = {
	MSIOF0_SYNC_MARK,
};
static const unsigned int msiof0_ss1_pins[] = {
	/* SS1 */
	RCAR_GP_PIN(5, 19),
};
static const unsigned int msiof0_ss1_mux[] = {
	MSIOF0_SS1_MARK,
};
static const unsigned int msiof0_ss2_pins[] = {
	/* SS2 */
	RCAR_GP_PIN(5, 21),
};
static const unsigned int msiof0_ss2_mux[] = {
	MSIOF0_SS2_MARK,
};
static const unsigned int msiof0_txd_pins[] = {
	/* TXD */
	RCAR_GP_PIN(5, 20),
};
static const unsigned int msiof0_txd_mux[] = {
	MSIOF0_TXD_MARK,
};
static const unsigned int msiof0_rxd_pins[] = {
	/* RXD */
	RCAR_GP_PIN(5, 22),
};
static const unsigned int msiof0_rxd_mux[] = {
	MSIOF0_RXD_MARK,
};
/* - MSIOF1 ----------------------------------------------------------------- */
static const unsigned int msiof1_clk_a_pins[] = {
	/* SCK */
	RCAR_GP_PIN(6, 8),
};
static const unsigned int msiof1_clk_a_mux[] = {
	MSIOF1_SCK_A_MARK,
};
static const unsigned int msiof1_sync_a_pins[] = {
	/* SYNC */
	RCAR_GP_PIN(6, 9),
};
static const unsigned int msiof1_sync_a_mux[] = {
	MSIOF1_SYNC_A_MARK,
};
static const unsigned int msiof1_ss1_a_pins[] = {
	/* SS1 */
	RCAR_GP_PIN(6, 5),
};
static const unsigned int msiof1_ss1_a_mux[] = {
	MSIOF1_SS1_A_MARK,
};
static const unsigned int msiof1_ss2_a_pins[] = {
	/* SS2 */
	RCAR_GP_PIN(6, 6),
};
static const unsigned int msiof1_ss2_a_mux[] = {
	MSIOF1_SS2_A_MARK,
};
static const unsigned int msiof1_txd_a_pins[] = {
	/* TXD */
	RCAR_GP_PIN(6, 7),
};
static const unsigned int msiof1_txd_a_mux[] = {
	MSIOF1_TXD_A_MARK,
};
static const unsigned int msiof1_rxd_a_pins[] = {
	/* RXD */
	RCAR_GP_PIN(6, 10),
};
static const unsigned int msiof1_rxd_a_mux[] = {
	MSIOF1_RXD_A_MARK,
};
static const unsigned int msiof1_clk_b_pins[] = {
	/* SCK */
	RCAR_GP_PIN(5, 9),
};
static const unsigned int msiof1_clk_b_mux[] = {
	MSIOF1_SCK_B_MARK,
};
static const unsigned int msiof1_sync_b_pins[] = {
	/* SYNC */
	RCAR_GP_PIN(5, 3),
};
static const unsigned int msiof1_sync_b_mux[] = {
	MSIOF1_SYNC_B_MARK,
};
static const unsigned int msiof1_ss1_b_pins[] = {
	/* SS1 */
	RCAR_GP_PIN(5, 4),
};
static const unsigned int msiof1_ss1_b_mux[] = {
	MSIOF1_SS1_B_MARK,
};
static const unsigned int msiof1_ss2_b_pins[] = {
	/* SS2 */
	RCAR_GP_PIN(5, 0),
};
static const unsigned int msiof1_ss2_b_mux[] = {
	MSIOF1_SS2_B_MARK,
};
static const unsigned int msiof1_txd_b_pins[] = {
	/* TXD */
	RCAR_GP_PIN(5, 8),
};
static const unsigned int msiof1_txd_b_mux[] = {
	MSIOF1_TXD_B_MARK,
};
static const unsigned int msiof1_rxd_b_pins[] = {
	/* RXD */
	RCAR_GP_PIN(5, 7),
};
static const unsigned int msiof1_rxd_b_mux[] = {
	MSIOF1_RXD_B_MARK,
};
static const unsigned int msiof1_clk_c_pins[] = {
	/* SCK */
	RCAR_GP_PIN(6, 17),
};
static const unsigned int msiof1_clk_c_mux[] = {
	MSIOF1_SCK_C_MARK,
};
static const unsigned int msiof1_sync_c_pins[] = {
	/* SYNC */
	RCAR_GP_PIN(6, 18),
};
static const unsigned int msiof1_sync_c_mux[] = {
	MSIOF1_SYNC_C_MARK,
};
static const unsigned int msiof1_ss1_c_pins[] = {
	/* SS1 */
	RCAR_GP_PIN(6, 21),
};
static const unsigned int msiof1_ss1_c_mux[] = {
	MSIOF1_SS1_C_MARK,
};
static const unsigned int msiof1_ss2_c_pins[] = {
	/* SS2 */
	RCAR_GP_PIN(6, 27),
};
static const unsigned int msiof1_ss2_c_mux[] = {
	MSIOF1_SS2_C_MARK,
};
static const unsigned int msiof1_txd_c_pins[] = {
	/* TXD */
	RCAR_GP_PIN(6, 20),
};
static const unsigned int msiof1_txd_c_mux[] = {
	MSIOF1_TXD_C_MARK,
};
static const unsigned int msiof1_rxd_c_pins[] = {
	/* RXD */
	RCAR_GP_PIN(6, 19),
};
static const unsigned int msiof1_rxd_c_mux[] = {
	MSIOF1_RXD_C_MARK,
};
static const unsigned int msiof1_clk_d_pins[] = {
	/* SCK */
	RCAR_GP_PIN(5, 12),
};
static const unsigned int msiof1_clk_d_mux[] = {
	MSIOF1_SCK_D_MARK,
};
static const unsigned int msiof1_sync_d_pins[] = {
	/* SYNC */
	RCAR_GP_PIN(5, 15),
};
static const unsigned int msiof1_sync_d_mux[] = {
	MSIOF1_SYNC_D_MARK,
};
static const unsigned int msiof1_ss1_d_pins[] = {
	/* SS1 */
	RCAR_GP_PIN(5, 16),
};
static const unsigned int msiof1_ss1_d_mux[] = {
	MSIOF1_SS1_D_MARK,
};
static const unsigned int msiof1_ss2_d_pins[] = {
	/* SS2 */
	RCAR_GP_PIN(5, 21),
};
static const unsigned int msiof1_ss2_d_mux[] = {
	MSIOF1_SS2_D_MARK,
};
static const unsigned int msiof1_txd_d_pins[] = {
	/* TXD */
	RCAR_GP_PIN(5, 14),
};
static const unsigned int msiof1_txd_d_mux[] = {
	MSIOF1_TXD_D_MARK,
};
static const unsigned int msiof1_rxd_d_pins[] = {
	/* RXD */
	RCAR_GP_PIN(5, 13),
};
static const unsigned int msiof1_rxd_d_mux[] = {
	MSIOF1_RXD_D_MARK,
};
static const unsigned int msiof1_clk_e_pins[] = {
	/* SCK */
	RCAR_GP_PIN(3, 0),
};
static const unsigned int msiof1_clk_e_mux[] = {
	MSIOF1_SCK_E_MARK,
};
static const unsigned int msiof1_sync_e_pins[] = {
	/* SYNC */
	RCAR_GP_PIN(3, 1),
};
static const unsigned int msiof1_sync_e_mux[] = {
	MSIOF1_SYNC_E_MARK,
};
static const unsigned int msiof1_ss1_e_pins[] = {
	/* SS1 */
	RCAR_GP_PIN(3, 4),
};
static const unsigned int msiof1_ss1_e_mux[] = {
	MSIOF1_SS1_E_MARK,
};
static const unsigned int msiof1_ss2_e_pins[] = {
	/* SS2 */
	RCAR_GP_PIN(3, 5),
};
static const unsigned int msiof1_ss2_e_mux[] = {
	MSIOF1_SS2_E_MARK,
};
static const unsigned int msiof1_txd_e_pins[] = {
	/* TXD */
	RCAR_GP_PIN(3, 3),
};
static const unsigned int msiof1_txd_e_mux[] = {
	MSIOF1_TXD_E_MARK,
};
static const unsigned int msiof1_rxd_e_pins[] = {
	/* RXD */
	RCAR_GP_PIN(3, 2),
};
static const unsigned int msiof1_rxd_e_mux[] = {
	MSIOF1_RXD_E_MARK,
};
static const unsigned int msiof1_clk_f_pins[] = {
	/* SCK */
	RCAR_GP_PIN(5, 23),
};
static const unsigned int msiof1_clk_f_mux[] = {
	MSIOF1_SCK_F_MARK,
};
static const unsigned int msiof1_sync_f_pins[] = {
	/* SYNC */
	RCAR_GP_PIN(5, 24),
};
static const unsigned int msiof1_sync_f_mux[] = {
	MSIOF1_SYNC_F_MARK,
};
static const unsigned int msiof1_ss1_f_pins[] = {
	/* SS1 */
	RCAR_GP_PIN(6, 1),
};
static const unsigned int msiof1_ss1_f_mux[] = {
	MSIOF1_SS1_F_MARK,
};
static const unsigned int msiof1_ss2_f_pins[] = {
	/* SS2 */
	RCAR_GP_PIN(6, 2),
};
static const unsigned int msiof1_ss2_f_mux[] = {
	MSIOF1_SS2_F_MARK,
};
static const unsigned int msiof1_txd_f_pins[] = {
	/* TXD */
	RCAR_GP_PIN(6, 0),
};
static const unsigned int msiof1_txd_f_mux[] = {
	MSIOF1_TXD_F_MARK,
};
static const unsigned int msiof1_rxd_f_pins[] = {
	/* RXD */
	RCAR_GP_PIN(5, 25),
};
static const unsigned int msiof1_rxd_f_mux[] = {
	MSIOF1_RXD_F_MARK,
};
static const unsigned int msiof1_clk_g_pins[] = {
	/* SCK */
	RCAR_GP_PIN(3, 6),
};
static const unsigned int msiof1_clk_g_mux[] = {
	MSIOF1_SCK_G_MARK,
};
static const unsigned int msiof1_sync_g_pins[] = {
	/* SYNC */
	RCAR_GP_PIN(3, 7),
};
static const unsigned int msiof1_sync_g_mux[] = {
	MSIOF1_SYNC_G_MARK,
};
static const unsigned int msiof1_ss1_g_pins[] = {
	/* SS1 */
	RCAR_GP_PIN(3, 10),
};
static const unsigned int msiof1_ss1_g_mux[] = {
	MSIOF1_SS1_G_MARK,
};
static const unsigned int msiof1_ss2_g_pins[] = {
	/* SS2 */
	RCAR_GP_PIN(3, 11),
};
static const unsigned int msiof1_ss2_g_mux[] = {
	MSIOF1_SS2_G_MARK,
};
static const unsigned int msiof1_txd_g_pins[] = {
	/* TXD */
	RCAR_GP_PIN(3, 9),
};
static const unsigned int msiof1_txd_g_mux[] = {
	MSIOF1_TXD_G_MARK,
};
static const unsigned int msiof1_rxd_g_pins[] = {
	/* RXD */
	RCAR_GP_PIN(3, 8),
};
static const unsigned int msiof1_rxd_g_mux[] = {
	MSIOF1_RXD_G_MARK,
};
/* - MSIOF2 ----------------------------------------------------------------- */
static const unsigned int msiof2_clk_a_pins[] = {
	/* SCK */
	RCAR_GP_PIN(1, 9),
};
static const unsigned int msiof2_clk_a_mux[] = {
	MSIOF2_SCK_A_MARK,
};
static const unsigned int msiof2_sync_a_pins[] = {
	/* SYNC */
	RCAR_GP_PIN(1, 8),
};
static const unsigned int msiof2_sync_a_mux[] = {
	MSIOF2_SYNC_A_MARK,
};
static const unsigned int msiof2_ss1_a_pins[] = {
	/* SS1 */
	RCAR_GP_PIN(1, 6),
};
static const unsigned int msiof2_ss1_a_mux[] = {
	MSIOF2_SS1_A_MARK,
};
static const unsigned int msiof2_ss2_a_pins[] = {
	/* SS2 */
	RCAR_GP_PIN(1, 7),
};
static const unsigned int msiof2_ss2_a_mux[] = {
	MSIOF2_SS2_A_MARK,
};
static const unsigned int msiof2_txd_a_pins[] = {
	/* TXD */
	RCAR_GP_PIN(1, 11),
};
static const unsigned int msiof2_txd_a_mux[] = {
	MSIOF2_TXD_A_MARK,
};
static const unsigned int msiof2_rxd_a_pins[] = {
	/* RXD */
	RCAR_GP_PIN(1, 10),
};
static const unsigned int msiof2_rxd_a_mux[] = {
	MSIOF2_RXD_A_MARK,
};
static const unsigned int msiof2_clk_b_pins[] = {
	/* SCK */
	RCAR_GP_PIN(0, 4),
};
static const unsigned int msiof2_clk_b_mux[] = {
	MSIOF2_SCK_B_MARK,
};
static const unsigned int msiof2_sync_b_pins[] = {
	/* SYNC */
	RCAR_GP_PIN(0, 5),
};
static const unsigned int msiof2_sync_b_mux[] = {
	MSIOF2_SYNC_B_MARK,
};
static const unsigned int msiof2_ss1_b_pins[] = {
	/* SS1 */
	RCAR_GP_PIN(0, 0),
};
static const unsigned int msiof2_ss1_b_mux[] = {
	MSIOF2_SS1_B_MARK,
};
static const unsigned int msiof2_ss2_b_pins[] = {
	/* SS2 */
	RCAR_GP_PIN(0, 1),
};
static const unsigned int msiof2_ss2_b_mux[] = {
	MSIOF2_SS2_B_MARK,
};
static const unsigned int msiof2_txd_b_pins[] = {
	/* TXD */
	RCAR_GP_PIN(0, 7),
};
static const unsigned int msiof2_txd_b_mux[] = {
	MSIOF2_TXD_B_MARK,
};
static const unsigned int msiof2_rxd_b_pins[] = {
	/* RXD */
	RCAR_GP_PIN(0, 6),
};
static const unsigned int msiof2_rxd_b_mux[] = {
	MSIOF2_RXD_B_MARK,
};
static const unsigned int msiof2_clk_c_pins[] = {
	/* SCK */
	RCAR_GP_PIN(2, 12),
};
static const unsigned int msiof2_clk_c_mux[] = {
	MSIOF2_SCK_C_MARK,
};
static const unsigned int msiof2_sync_c_pins[] = {
	/* SYNC */
	RCAR_GP_PIN(2, 11),
};
static const unsigned int msiof2_sync_c_mux[] = {
	MSIOF2_SYNC_C_MARK,
};
static const unsigned int msiof2_ss1_c_pins[] = {
	/* SS1 */
	RCAR_GP_PIN(2, 10),
};
static const unsigned int msiof2_ss1_c_mux[] = {
	MSIOF2_SS1_C_MARK,
};
static const unsigned int msiof2_ss2_c_pins[] = {
	/* SS2 */
	RCAR_GP_PIN(2, 9),
};
static const unsigned int msiof2_ss2_c_mux[] = {
	MSIOF2_SS2_C_MARK,
};
static const unsigned int msiof2_txd_c_pins[] = {
	/* TXD */
	RCAR_GP_PIN(2, 14),
};
static const unsigned int msiof2_txd_c_mux[] = {
	MSIOF2_TXD_C_MARK,
};
static const unsigned int msiof2_rxd_c_pins[] = {
	/* RXD */
	RCAR_GP_PIN(2, 13),
};
static const unsigned int msiof2_rxd_c_mux[] = {
	MSIOF2_RXD_C_MARK,
};
static const unsigned int msiof2_clk_d_pins[] = {
	/* SCK */
	RCAR_GP_PIN(0, 8),
};
static const unsigned int msiof2_clk_d_mux[] = {
	MSIOF2_SCK_D_MARK,
};
static const unsigned int msiof2_sync_d_pins[] = {
	/* SYNC */
	RCAR_GP_PIN(0, 9),
};
static const unsigned int msiof2_sync_d_mux[] = {
	MSIOF2_SYNC_D_MARK,
};
static const unsigned int msiof2_ss1_d_pins[] = {
	/* SS1 */
	RCAR_GP_PIN(0, 12),
};
static const unsigned int msiof2_ss1_d_mux[] = {
	MSIOF2_SS1_D_MARK,
};
static const unsigned int msiof2_ss2_d_pins[] = {
	/* SS2 */
	RCAR_GP_PIN(0, 13),
};
static const unsigned int msiof2_ss2_d_mux[] = {
	MSIOF2_SS2_D_MARK,
};
static const unsigned int msiof2_txd_d_pins[] = {
	/* TXD */
	RCAR_GP_PIN(0, 11),
};
static const unsigned int msiof2_txd_d_mux[] = {
	MSIOF2_TXD_D_MARK,
};
static const unsigned int msiof2_rxd_d_pins[] = {
	/* RXD */
	RCAR_GP_PIN(0, 10),
};
static const unsigned int msiof2_rxd_d_mux[] = {
	MSIOF2_RXD_D_MARK,
};
/* - MSIOF3 ----------------------------------------------------------------- */
static const unsigned int msiof3_clk_a_pins[] = {
	/* SCK */
	RCAR_GP_PIN(0, 0),
};
static const unsigned int msiof3_clk_a_mux[] = {
	MSIOF3_SCK_A_MARK,
};
static const unsigned int msiof3_sync_a_pins[] = {
	/* SYNC */
	RCAR_GP_PIN(0, 1),
};
static const unsigned int msiof3_sync_a_mux[] = {
	MSIOF3_SYNC_A_MARK,
};
static const unsigned int msiof3_ss1_a_pins[] = {
	/* SS1 */
	RCAR_GP_PIN(0, 14),
};
static const unsigned int msiof3_ss1_a_mux[] = {
	MSIOF3_SS1_A_MARK,
};
static const unsigned int msiof3_ss2_a_pins[] = {
	/* SS2 */
	RCAR_GP_PIN(0, 15),
};
static const unsigned int msiof3_ss2_a_mux[] = {
	MSIOF3_SS2_A_MARK,
};
static const unsigned int msiof3_txd_a_pins[] = {
	/* TXD */
	RCAR_GP_PIN(0, 3),
};
static const unsigned int msiof3_txd_a_mux[] = {
	MSIOF3_TXD_A_MARK,
};
static const unsigned int msiof3_rxd_a_pins[] = {
	/* RXD */
	RCAR_GP_PIN(0, 2),
};
static const unsigned int msiof3_rxd_a_mux[] = {
	MSIOF3_RXD_A_MARK,
};
static const unsigned int msiof3_clk_b_pins[] = {
	/* SCK */
	RCAR_GP_PIN(1, 2),
};
static const unsigned int msiof3_clk_b_mux[] = {
	MSIOF3_SCK_B_MARK,
};
static const unsigned int msiof3_sync_b_pins[] = {
	/* SYNC */
	RCAR_GP_PIN(1, 0),
};
static const unsigned int msiof3_sync_b_mux[] = {
	MSIOF3_SYNC_B_MARK,
};
static const unsigned int msiof3_ss1_b_pins[] = {
	/* SS1 */
	RCAR_GP_PIN(1, 4),
};
static const unsigned int msiof3_ss1_b_mux[] = {
	MSIOF3_SS1_B_MARK,
};
static const unsigned int msiof3_ss2_b_pins[] = {
	/* SS2 */
	RCAR_GP_PIN(1, 5),
};
static const unsigned int msiof3_ss2_b_mux[] = {
	MSIOF3_SS2_B_MARK,
};
static const unsigned int msiof3_txd_b_pins[] = {
	/* TXD */
	RCAR_GP_PIN(1, 1),
};
static const unsigned int msiof3_txd_b_mux[] = {
	MSIOF3_TXD_B_MARK,
};
static const unsigned int msiof3_rxd_b_pins[] = {
	/* RXD */
	RCAR_GP_PIN(1, 3),
};
static const unsigned int msiof3_rxd_b_mux[] = {
	MSIOF3_RXD_B_MARK,
};
static const unsigned int msiof3_clk_c_pins[] = {
	/* SCK */
	RCAR_GP_PIN(1, 12),
};
static const unsigned int msiof3_clk_c_mux[] = {
	MSIOF3_SCK_C_MARK,
};
static const unsigned int msiof3_sync_c_pins[] = {
	/* SYNC */
	RCAR_GP_PIN(1, 13),
};
static const unsigned int msiof3_sync_c_mux[] = {
	MSIOF3_SYNC_C_MARK,
};
static const unsigned int msiof3_txd_c_pins[] = {
	/* TXD */
	RCAR_GP_PIN(1, 15),
};
static const unsigned int msiof3_txd_c_mux[] = {
	MSIOF3_TXD_C_MARK,
};
static const unsigned int msiof3_rxd_c_pins[] = {
	/* RXD */
	RCAR_GP_PIN(1, 14),
};
static const unsigned int msiof3_rxd_c_mux[] = {
	MSIOF3_RXD_C_MARK,
};
static const unsigned int msiof3_clk_d_pins[] = {
	/* SCK */
	RCAR_GP_PIN(1, 22),
};
static const unsigned int msiof3_clk_d_mux[] = {
	MSIOF3_SCK_D_MARK,
};
static const unsigned int msiof3_sync_d_pins[] = {
	/* SYNC */
	RCAR_GP_PIN(1, 23),
};
static const unsigned int msiof3_sync_d_mux[] = {
	MSIOF3_SYNC_D_MARK,
};
static const unsigned int msiof3_ss1_d_pins[] = {
	/* SS1 */
	RCAR_GP_PIN(1, 26),
};
static const unsigned int msiof3_ss1_d_mux[] = {
	MSIOF3_SS1_D_MARK,
};
static const unsigned int msiof3_txd_d_pins[] = {
	/* TXD */
	RCAR_GP_PIN(1, 25),
};
static const unsigned int msiof3_txd_d_mux[] = {
	MSIOF3_TXD_D_MARK,
};
static const unsigned int msiof3_rxd_d_pins[] = {
	/* RXD */
	RCAR_GP_PIN(1, 24),
};
static const unsigned int msiof3_rxd_d_mux[] = {
	MSIOF3_RXD_D_MARK,
};
static const unsigned int msiof3_clk_e_pins[] = {
	/* SCK */
	RCAR_GP_PIN(2, 3),
};
static const unsigned int msiof3_clk_e_mux[] = {
	MSIOF3_SCK_E_MARK,
};
static const unsigned int msiof3_sync_e_pins[] = {
	/* SYNC */
	RCAR_GP_PIN(2, 2),
};
static const unsigned int msiof3_sync_e_mux[] = {
	MSIOF3_SYNC_E_MARK,
};
static const unsigned int msiof3_ss1_e_pins[] = {
	/* SS1 */
	RCAR_GP_PIN(2, 1),
};
static const unsigned int msiof3_ss1_e_mux[] = {
	MSIOF3_SS1_E_MARK,
};
static const unsigned int msiof3_ss2_e_pins[] = {
	/* SS2 */
	RCAR_GP_PIN(2, 0),
};
static const unsigned int msiof3_ss2_e_mux[] = {
	MSIOF3_SS2_E_MARK,
};
static const unsigned int msiof3_txd_e_pins[] = {
	/* TXD */
	RCAR_GP_PIN(2, 5),
};
static const unsigned int msiof3_txd_e_mux[] = {
	MSIOF3_TXD_E_MARK,
};
static const unsigned int msiof3_rxd_e_pins[] = {
	/* RXD */
	RCAR_GP_PIN(2, 4),
};
static const unsigned int msiof3_rxd_e_mux[] = {
	MSIOF3_RXD_E_MARK,
};

/* - PWM0 --------------------------------------------------------------------*/
static const unsigned int pwm0_pins[] = {
	/* PWM */
	RCAR_GP_PIN(2, 6),
};
static const unsigned int pwm0_mux[] = {
	PWM0_MARK,
};
/* - PWM1 --------------------------------------------------------------------*/
static const unsigned int pwm1_a_pins[] = {
	/* PWM */
	RCAR_GP_PIN(2, 7),
};
static const unsigned int pwm1_a_mux[] = {
	PWM1_A_MARK,
};
static const unsigned int pwm1_b_pins[] = {
	/* PWM */
	RCAR_GP_PIN(1, 8),
};
static const unsigned int pwm1_b_mux[] = {
	PWM1_B_MARK,
};
/* - PWM2 --------------------------------------------------------------------*/
static const unsigned int pwm2_a_pins[] = {
	/* PWM */
	RCAR_GP_PIN(2, 8),
};
static const unsigned int pwm2_a_mux[] = {
	PWM2_A_MARK,
};
static const unsigned int pwm2_b_pins[] = {
	/* PWM */
	RCAR_GP_PIN(1, 11),
};
static const unsigned int pwm2_b_mux[] = {
	PWM2_B_MARK,
};
/* - PWM3 --------------------------------------------------------------------*/
static const unsigned int pwm3_a_pins[] = {
	/* PWM */
	RCAR_GP_PIN(1, 0),
};
static const unsigned int pwm3_a_mux[] = {
	PWM3_A_MARK,
};
static const unsigned int pwm3_b_pins[] = {
	/* PWM */
	RCAR_GP_PIN(2, 2),
};
static const unsigned int pwm3_b_mux[] = {
	PWM3_B_MARK,
};
/* - PWM4 --------------------------------------------------------------------*/
static const unsigned int pwm4_a_pins[] = {
	/* PWM */
	RCAR_GP_PIN(1, 1),
};
static const unsigned int pwm4_a_mux[] = {
	PWM4_A_MARK,
};
static const unsigned int pwm4_b_pins[] = {
	/* PWM */
	RCAR_GP_PIN(2, 3),
};
static const unsigned int pwm4_b_mux[] = {
	PWM4_B_MARK,
};
/* - PWM5 --------------------------------------------------------------------*/
static const unsigned int pwm5_a_pins[] = {
	/* PWM */
	RCAR_GP_PIN(1, 2),
};
static const unsigned int pwm5_a_mux[] = {
	PWM5_A_MARK,
};
static const unsigned int pwm5_b_pins[] = {
	/* PWM */
	RCAR_GP_PIN(2, 4),
};
static const unsigned int pwm5_b_mux[] = {
	PWM5_B_MARK,
};
/* - PWM6 --------------------------------------------------------------------*/
static const unsigned int pwm6_a_pins[] = {
	/* PWM */
	RCAR_GP_PIN(1, 3),
};
static const unsigned int pwm6_a_mux[] = {
	PWM6_A_MARK,
};
static const unsigned int pwm6_b_pins[] = {
	/* PWM */
	RCAR_GP_PIN(2, 5),
};
static const unsigned int pwm6_b_mux[] = {
	PWM6_B_MARK,
};

/* - QSPI0 ------------------------------------------------------------------ */
static const unsigned int qspi0_ctrl_pins[] = {
	/* QSPI0_SPCLK, QSPI0_SSL */
	PIN_QSPI0_SPCLK, PIN_QSPI0_SSL,
};
static const unsigned int qspi0_ctrl_mux[] = {
	QSPI0_SPCLK_MARK, QSPI0_SSL_MARK,
};
static const unsigned int qspi0_data2_pins[] = {
	/* QSPI0_MOSI_IO0, QSPI0_MISO_IO1 */
	PIN_QSPI0_MOSI_IO0, PIN_QSPI0_MISO_IO1,
};
static const unsigned int qspi0_data2_mux[] = {
	QSPI0_MOSI_IO0_MARK, QSPI0_MISO_IO1_MARK,
};
static const unsigned int qspi0_data4_pins[] = {
	/* QSPI0_MOSI_IO0, QSPI0_MISO_IO1 */
	PIN_QSPI0_MOSI_IO0, PIN_QSPI0_MISO_IO1,
	/* QSPI0_IO2, QSPI0_IO3 */
	PIN_QSPI0_IO2, PIN_QSPI0_IO3,
};
static const unsigned int qspi0_data4_mux[] = {
	QSPI0_MOSI_IO0_MARK, QSPI0_MISO_IO1_MARK,
	QSPI0_IO2_MARK, QSPI0_IO3_MARK,
};
/* - QSPI1 ------------------------------------------------------------------ */
static const unsigned int qspi1_ctrl_pins[] = {
	/* QSPI1_SPCLK, QSPI1_SSL */
	PIN_QSPI1_SPCLK, PIN_QSPI1_SSL,
};
static const unsigned int qspi1_ctrl_mux[] = {
	QSPI1_SPCLK_MARK, QSPI1_SSL_MARK,
};
static const unsigned int qspi1_data2_pins[] = {
	/* QSPI1_MOSI_IO0, QSPI1_MISO_IO1 */
	PIN_QSPI1_MOSI_IO0, PIN_QSPI1_MISO_IO1,
};
static const unsigned int qspi1_data2_mux[] = {
	QSPI1_MOSI_IO0_MARK, QSPI1_MISO_IO1_MARK,
};
static const unsigned int qspi1_data4_pins[] = {
	/* QSPI1_MOSI_IO0, QSPI1_MISO_IO1 */
	PIN_QSPI1_MOSI_IO0, PIN_QSPI1_MISO_IO1,
	/* QSPI1_IO2, QSPI1_IO3 */
	PIN_QSPI1_IO2, PIN_QSPI1_IO3,
};
static const unsigned int qspi1_data4_mux[] = {
	QSPI1_MOSI_IO0_MARK, QSPI1_MISO_IO1_MARK,
	QSPI1_IO2_MARK, QSPI1_IO3_MARK,
};

/* - SATA --------------------------------------------------------------------*/
static const unsigned int sata0_devslp_a_pins[] = {
	/* DEVSLP */
	RCAR_GP_PIN(6, 16),
};
static const unsigned int sata0_devslp_a_mux[] = {
	SATA_DEVSLP_A_MARK,
};
static const unsigned int sata0_devslp_b_pins[] = {
	/* DEVSLP */
	RCAR_GP_PIN(4, 6),
};
static const unsigned int sata0_devslp_b_mux[] = {
	SATA_DEVSLP_B_MARK,
};

/* - SCIF0 ------------------------------------------------------------------ */
static const unsigned int scif0_data_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(5, 1), RCAR_GP_PIN(5, 2),
};
static const unsigned int scif0_data_mux[] = {
	RX0_MARK, TX0_MARK,
};
static const unsigned int scif0_clk_pins[] = {
	/* SCK */
	RCAR_GP_PIN(5, 0),
};
static const unsigned int scif0_clk_mux[] = {
	SCK0_MARK,
};
static const unsigned int scif0_ctrl_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(5, 4), RCAR_GP_PIN(5, 3),
};
static const unsigned int scif0_ctrl_mux[] = {
	RTS0_N_MARK, CTS0_N_MARK,
};
/* - SCIF1 ------------------------------------------------------------------ */
static const unsigned int scif1_data_a_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(5, 5), RCAR_GP_PIN(5, 6),
};
static const unsigned int scif1_data_a_mux[] = {
	RX1_A_MARK, TX1_A_MARK,
};
static const unsigned int scif1_clk_pins[] = {
	/* SCK */
	RCAR_GP_PIN(6, 21),
};
static const unsigned int scif1_clk_mux[] = {
	SCK1_MARK,
};
static const unsigned int scif1_ctrl_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(5, 8), RCAR_GP_PIN(5, 7),
};
static const unsigned int scif1_ctrl_mux[] = {
	RTS1_N_MARK, CTS1_N_MARK,
};

static const unsigned int scif1_data_b_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(5, 24), RCAR_GP_PIN(5, 25),
};
static const unsigned int scif1_data_b_mux[] = {
	RX1_B_MARK, TX1_B_MARK,
};
/* - SCIF2 ------------------------------------------------------------------ */
static const unsigned int scif2_data_a_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(5, 11), RCAR_GP_PIN(5, 10),
};
static const unsigned int scif2_data_a_mux[] = {
	RX2_A_MARK, TX2_A_MARK,
};
static const unsigned int scif2_clk_pins[] = {
	/* SCK */
	RCAR_GP_PIN(5, 9),
};
static const unsigned int scif2_clk_mux[] = {
	SCK2_MARK,
};
static const unsigned int scif2_data_b_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(5, 15), RCAR_GP_PIN(5, 16),
};
static const unsigned int scif2_data_b_mux[] = {
	RX2_B_MARK, TX2_B_MARK,
};
/* - SCIF3 ------------------------------------------------------------------ */
static const unsigned int scif3_data_a_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(1, 23), RCAR_GP_PIN(1, 24),
};
static const unsigned int scif3_data_a_mux[] = {
	RX3_A_MARK, TX3_A_MARK,
};
static const unsigned int scif3_clk_pins[] = {
	/* SCK */
	RCAR_GP_PIN(1, 22),
};
static const unsigned int scif3_clk_mux[] = {
	SCK3_MARK,
};
static const unsigned int scif3_ctrl_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(1, 26), RCAR_GP_PIN(1, 25),
};
static const unsigned int scif3_ctrl_mux[] = {
	RTS3_N_MARK, CTS3_N_MARK,
};
static const unsigned int scif3_data_b_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(1, 8), RCAR_GP_PIN(1, 11),
};
static const unsigned int scif3_data_b_mux[] = {
	RX3_B_MARK, TX3_B_MARK,
};
/* - SCIF4 ------------------------------------------------------------------ */
static const unsigned int scif4_data_a_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(2, 11), RCAR_GP_PIN(2, 12),
};
static const unsigned int scif4_data_a_mux[] = {
	RX4_A_MARK, TX4_A_MARK,
};
static const unsigned int scif4_clk_a_pins[] = {
	/* SCK */
	RCAR_GP_PIN(2, 10),
};
static const unsigned int scif4_clk_a_mux[] = {
	SCK4_A_MARK,
};
static const unsigned int scif4_ctrl_a_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(2, 14), RCAR_GP_PIN(2, 13),
};
static const unsigned int scif4_ctrl_a_mux[] = {
	RTS4_N_A_MARK, CTS4_N_A_MARK,
};
static const unsigned int scif4_data_b_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(1, 6), RCAR_GP_PIN(1, 7),
};
static const unsigned int scif4_data_b_mux[] = {
	RX4_B_MARK, TX4_B_MARK,
};
static const unsigned int scif4_clk_b_pins[] = {
	/* SCK */
	RCAR_GP_PIN(1, 5),
};
static const unsigned int scif4_clk_b_mux[] = {
	SCK4_B_MARK,
};
static const unsigned int scif4_ctrl_b_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(1, 10), RCAR_GP_PIN(1, 9),
};
static const unsigned int scif4_ctrl_b_mux[] = {
	RTS4_N_B_MARK, CTS4_N_B_MARK,
};
static const unsigned int scif4_data_c_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(0, 12), RCAR_GP_PIN(0, 13),
};
static const unsigned int scif4_data_c_mux[] = {
	RX4_C_MARK, TX4_C_MARK,
};
static const unsigned int scif4_clk_c_pins[] = {
	/* SCK */
	RCAR_GP_PIN(0, 8),
};
static const unsigned int scif4_clk_c_mux[] = {
	SCK4_C_MARK,
};
static const unsigned int scif4_ctrl_c_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(0, 11), RCAR_GP_PIN(0, 10),
};
static const unsigned int scif4_ctrl_c_mux[] = {
	RTS4_N_C_MARK, CTS4_N_C_MARK,
};
/* - SCIF5 ------------------------------------------------------------------ */
static const unsigned int scif5_data_a_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(5, 19), RCAR_GP_PIN(5, 21),
};
static const unsigned int scif5_data_a_mux[] = {
	RX5_A_MARK, TX5_A_MARK,
};
static const unsigned int scif5_clk_a_pins[] = {
	/* SCK */
	RCAR_GP_PIN(6, 21),
};
static const unsigned int scif5_clk_a_mux[] = {
	SCK5_A_MARK,
};
static const unsigned int scif5_data_b_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(5, 12), RCAR_GP_PIN(5, 18),
};
static const unsigned int scif5_data_b_mux[] = {
	RX5_B_MARK, TX5_B_MARK,
};
static const unsigned int scif5_clk_b_pins[] = {
	/* SCK */
	RCAR_GP_PIN(5, 0),
};
static const unsigned int scif5_clk_b_mux[] = {
	SCK5_B_MARK,
};

/* - SCIF Clock ------------------------------------------------------------- */
static const unsigned int scif_clk_a_pins[] = {
	/* SCIF_CLK */
	RCAR_GP_PIN(6, 23),
};
static const unsigned int scif_clk_a_mux[] = {
	SCIF_CLK_A_MARK,
};
static const unsigned int scif_clk_b_pins[] = {
	/* SCIF_CLK */
	RCAR_GP_PIN(5, 9),
};
static const unsigned int scif_clk_b_mux[] = {
	SCIF_CLK_B_MARK,
};

/* - SDHI0 ------------------------------------------------------------------ */
static const unsigned int sdhi0_data1_pins[] = {
	/* D0 */
	RCAR_GP_PIN(3, 2),
};
static const unsigned int sdhi0_data1_mux[] = {
	SD0_DAT0_MARK,
};
static const unsigned int sdhi0_data4_pins[] = {
	/* D[0:3] */
	RCAR_GP_PIN(3, 2), RCAR_GP_PIN(3, 3),
	RCAR_GP_PIN(3, 4), RCAR_GP_PIN(3, 5),
};
static const unsigned int sdhi0_data4_mux[] = {
	SD0_DAT0_MARK, SD0_DAT1_MARK,
	SD0_DAT2_MARK, SD0_DAT3_MARK,
};
static const unsigned int sdhi0_ctrl_pins[] = {
	/* CLK, CMD */
	RCAR_GP_PIN(3, 0), RCAR_GP_PIN(3, 1),
};
static const unsigned int sdhi0_ctrl_mux[] = {
	SD0_CLK_MARK, SD0_CMD_MARK,
};
static const unsigned int sdhi0_cd_pins[] = {
	/* CD */
	RCAR_GP_PIN(3, 12),
};
static const unsigned int sdhi0_cd_mux[] = {
	SD0_CD_MARK,
};
static const unsigned int sdhi0_wp_pins[] = {
	/* WP */
	RCAR_GP_PIN(3, 13),
};
static const unsigned int sdhi0_wp_mux[] = {
	SD0_WP_MARK,
};
/* - SDHI1 ------------------------------------------------------------------ */
static const unsigned int sdhi1_data1_pins[] = {
	/* D0 */
	RCAR_GP_PIN(3, 8),
};
static const unsigned int sdhi1_data1_mux[] = {
	SD1_DAT0_MARK,
};
static const unsigned int sdhi1_data4_pins[] = {
	/* D[0:3] */
	RCAR_GP_PIN(3, 8),  RCAR_GP_PIN(3, 9),
	RCAR_GP_PIN(3, 10), RCAR_GP_PIN(3, 11),
};
static const unsigned int sdhi1_data4_mux[] = {
	SD1_DAT0_MARK, SD1_DAT1_MARK,
	SD1_DAT2_MARK, SD1_DAT3_MARK,
};
static const unsigned int sdhi1_ctrl_pins[] = {
	/* CLK, CMD */
	RCAR_GP_PIN(3, 6), RCAR_GP_PIN(3, 7),
};
static const unsigned int sdhi1_ctrl_mux[] = {
	SD1_CLK_MARK, SD1_CMD_MARK,
};
static const unsigned int sdhi1_cd_pins[] = {
	/* CD */
	RCAR_GP_PIN(3, 14),
};
static const unsigned int sdhi1_cd_mux[] = {
	SD1_CD_MARK,
};
static const unsigned int sdhi1_wp_pins[] = {
	/* WP */
	RCAR_GP_PIN(3, 15),
};
static const unsigned int sdhi1_wp_mux[] = {
	SD1_WP_MARK,
};
/* - SDHI2 ------------------------------------------------------------------ */
static const unsigned int sdhi2_data1_pins[] = {
	/* D0 */
	RCAR_GP_PIN(4, 2),
};
static const unsigned int sdhi2_data1_mux[] = {
	SD2_DAT0_MARK,
};
static const unsigned int sdhi2_data4_pins[] = {
	/* D[0:3] */
	RCAR_GP_PIN(4, 2), RCAR_GP_PIN(4, 3),
	RCAR_GP_PIN(4, 4), RCAR_GP_PIN(4, 5),
};
static const unsigned int sdhi2_data4_mux[] = {
	SD2_DAT0_MARK, SD2_DAT1_MARK,
	SD2_DAT2_MARK, SD2_DAT3_MARK,
};
static const unsigned int sdhi2_data8_pins[] = {
	/* D[0:7] */
	RCAR_GP_PIN(4, 2),  RCAR_GP_PIN(4, 3),
	RCAR_GP_PIN(4, 4),  RCAR_GP_PIN(4, 5),
	RCAR_GP_PIN(3, 8),  RCAR_GP_PIN(3, 9),
	RCAR_GP_PIN(3, 10), RCAR_GP_PIN(3, 11),
};
static const unsigned int sdhi2_data8_mux[] = {
	SD2_DAT0_MARK, SD2_DAT1_MARK,
	SD2_DAT2_MARK, SD2_DAT3_MARK,
	SD2_DAT4_MARK, SD2_DAT5_MARK,
	SD2_DAT6_MARK, SD2_DAT7_MARK,
};
static const unsigned int sdhi2_ctrl_pins[] = {
	/* CLK, CMD */
	RCAR_GP_PIN(4, 0), RCAR_GP_PIN(4, 1),
};
static const unsigned int sdhi2_ctrl_mux[] = {
	SD2_CLK_MARK, SD2_CMD_MARK,
};
static const unsigned int sdhi2_cd_a_pins[] = {
	/* CD */
	RCAR_GP_PIN(4, 13),
};
static const unsigned int sdhi2_cd_a_mux[] = {
	SD2_CD_A_MARK,
};
static const unsigned int sdhi2_cd_b_pins[] = {
	/* CD */
	RCAR_GP_PIN(5, 10),
};
static const unsigned int sdhi2_cd_b_mux[] = {
	SD2_CD_B_MARK,
};
static const unsigned int sdhi2_wp_a_pins[] = {
	/* WP */
	RCAR_GP_PIN(4, 14),
};
static const unsigned int sdhi2_wp_a_mux[] = {
	SD2_WP_A_MARK,
};
static const unsigned int sdhi2_wp_b_pins[] = {
	/* WP */
	RCAR_GP_PIN(5, 11),
};
static const unsigned int sdhi2_wp_b_mux[] = {
	SD2_WP_B_MARK,
};
static const unsigned int sdhi2_ds_pins[] = {
	/* DS */
	RCAR_GP_PIN(4, 6),
};
static const unsigned int sdhi2_ds_mux[] = {
	SD2_DS_MARK,
};
/* - SDHI3 ------------------------------------------------------------------ */
static const unsigned int sdhi3_data1_pins[] = {
	/* D0 */
	RCAR_GP_PIN(4, 9),
};
static const unsigned int sdhi3_data1_mux[] = {
	SD3_DAT0_MARK,
};
static const unsigned int sdhi3_data4_pins[] = {
	/* D[0:3] */
	RCAR_GP_PIN(4, 9),  RCAR_GP_PIN(4, 10),
	RCAR_GP_PIN(4, 11), RCAR_GP_PIN(4, 12),
};
static const unsigned int sdhi3_data4_mux[] = {
	SD3_DAT0_MARK, SD3_DAT1_MARK,
	SD3_DAT2_MARK, SD3_DAT3_MARK,
};
static const unsigned int sdhi3_data8_pins[] = {
	/* D[0:7] */
	RCAR_GP_PIN(4, 9),  RCAR_GP_PIN(4, 10),
	RCAR_GP_PIN(4, 11), RCAR_GP_PIN(4, 12),
	RCAR_GP_PIN(4, 13), RCAR_GP_PIN(4, 14),
	RCAR_GP_PIN(4, 15), RCAR_GP_PIN(4, 16),
};
static const unsigned int sdhi3_data8_mux[] = {
	SD3_DAT0_MARK, SD3_DAT1_MARK,
	SD3_DAT2_MARK, SD3_DAT3_MARK,
	SD3_DAT4_MARK, SD3_DAT5_MARK,
	SD3_DAT6_MARK, SD3_DAT7_MARK,
};
static const unsigned int sdhi3_ctrl_pins[] = {
	/* CLK, CMD */
	RCAR_GP_PIN(4, 7), RCAR_GP_PIN(4, 8),
};
static const unsigned int sdhi3_ctrl_mux[] = {
	SD3_CLK_MARK, SD3_CMD_MARK,
};
static const unsigned int sdhi3_cd_pins[] = {
	/* CD */
	RCAR_GP_PIN(4, 15),
};
static const unsigned int sdhi3_cd_mux[] = {
	SD3_CD_MARK,
};
static const unsigned int sdhi3_wp_pins[] = {
	/* WP */
	RCAR_GP_PIN(4, 16),
};
static const unsigned int sdhi3_wp_mux[] = {
	SD3_WP_MARK,
};
static const unsigned int sdhi3_ds_pins[] = {
	/* DS */
	RCAR_GP_PIN(4, 17),
};
static const unsigned int sdhi3_ds_mux[] = {
	SD3_DS_MARK,
};

/* - SSI -------------------------------------------------------------------- */
static const unsigned int ssi0_data_pins[] = {
	/* SDATA */
	RCAR_GP_PIN(6, 2),
};
static const unsigned int ssi0_data_mux[] = {
	SSI_SDATA0_MARK,
};
static const unsigned int ssi01239_ctrl_pins[] = {
	/* SCK, WS */
	RCAR_GP_PIN(6, 0), RCAR_GP_PIN(6, 1),
};
static const unsigned int ssi01239_ctrl_mux[] = {
	SSI_SCK01239_MARK, SSI_WS01239_MARK,
};
static const unsigned int ssi1_data_a_pins[] = {
	/* SDATA */
	RCAR_GP_PIN(6, 3),
};
static const unsigned int ssi1_data_a_mux[] = {
	SSI_SDATA1_A_MARK,
};
static const unsigned int ssi1_data_b_pins[] = {
	/* SDATA */
	RCAR_GP_PIN(5, 12),
};
static const unsigned int ssi1_data_b_mux[] = {
	SSI_SDATA1_B_MARK,
};
static const unsigned int ssi1_ctrl_a_pins[] = {
	/* SCK, WS */
	RCAR_GP_PIN(6, 26), RCAR_GP_PIN(6, 27),
};
static const unsigned int ssi1_ctrl_a_mux[] = {
	SSI_SCK1_A_MARK, SSI_WS1_A_MARK,
};
static const unsigned int ssi1_ctrl_b_pins[] = {
	/* SCK, WS */
	RCAR_GP_PIN(6, 4), RCAR_GP_PIN(6, 21),
};
static const unsigned int ssi1_ctrl_b_mux[] = {
	SSI_SCK1_B_MARK, SSI_WS1_B_MARK,
};
static const unsigned int ssi2_data_a_pins[] = {
	/* SDATA */
	RCAR_GP_PIN(6, 4),
};
static const unsigned int ssi2_data_a_mux[] = {
	SSI_SDATA2_A_MARK,
};
static const unsigned int ssi2_data_b_pins[] = {
	/* SDATA */
	RCAR_GP_PIN(5, 13),
};
static const unsigned int ssi2_data_b_mux[] = {
	SSI_SDATA2_B_MARK,
};
static const unsigned int ssi2_ctrl_a_pins[] = {
	/* SCK, WS */
	RCAR_GP_PIN(5, 19), RCAR_GP_PIN(5, 21),
};
static const unsigned int ssi2_ctrl_a_mux[] = {
	SSI_SCK2_A_MARK, SSI_WS2_A_MARK,
};
static const unsigned int ssi2_ctrl_b_pins[] = {
	/* SCK, WS */
	RCAR_GP_PIN(6, 28), RCAR_GP_PIN(6, 29),
};
static const unsigned int ssi2_ctrl_b_mux[] = {
	SSI_SCK2_B_MARK, SSI_WS2_B_MARK,
};
static const unsigned int ssi3_data_pins[] = {
	/* SDATA */
	RCAR_GP_PIN(6, 7),
};
static const unsigned int ssi3_data_mux[] = {
	SSI_SDATA3_MARK,
};
static const unsigned int ssi349_ctrl_pins[] = {
	/* SCK, WS */
	RCAR_GP_PIN(6, 5), RCAR_GP_PIN(6, 6),
};
static const unsigned int ssi349_ctrl_mux[] = {
	SSI_SCK349_MARK, SSI_WS349_MARK,
};
static const unsigned int ssi4_data_pins[] = {
	/* SDATA */
	RCAR_GP_PIN(6, 10),
};
static const unsigned int ssi4_data_mux[] = {
	SSI_SDATA4_MARK,
};
static const unsigned int ssi4_ctrl_pins[] = {
	/* SCK, WS */
	RCAR_GP_PIN(6, 8), RCAR_GP_PIN(6, 9),
};
static const unsigned int ssi4_ctrl_mux[] = {
	SSI_SCK4_MARK, SSI_WS4_MARK,
};
static const unsigned int ssi5_data_pins[] = {
	/* SDATA */
	RCAR_GP_PIN(6, 13),
};
static const unsigned int ssi5_data_mux[] = {
	SSI_SDATA5_MARK,
};
static const unsigned int ssi5_ctrl_pins[] = {
	/* SCK, WS */
	RCAR_GP_PIN(6, 11), RCAR_GP_PIN(6, 12),
};
static const unsigned int ssi5_ctrl_mux[] = {
	SSI_SCK5_MARK, SSI_WS5_MARK,
};
static const unsigned int ssi6_data_pins[] = {
	/* SDATA */
	RCAR_GP_PIN(6, 16),
};
static const unsigned int ssi6_data_mux[] = {
	SSI_SDATA6_MARK,
};
static const unsigned int ssi6_ctrl_pins[] = {
	/* SCK, WS */
	RCAR_GP_PIN(6, 14), RCAR_GP_PIN(6, 15),
};
static const unsigned int ssi6_ctrl_mux[] = {
	SSI_SCK6_MARK, SSI_WS6_MARK,
};
static const unsigned int ssi7_data_pins[] = {
	/* SDATA */
	RCAR_GP_PIN(6, 19),
};
static const unsigned int ssi7_data_mux[] = {
	SSI_SDATA7_MARK,
};
static const unsigned int ssi78_ctrl_pins[] = {
	/* SCK, WS */
	RCAR_GP_PIN(6, 17), RCAR_GP_PIN(6, 18),
};
static const unsigned int ssi78_ctrl_mux[] = {
	SSI_SCK78_MARK, SSI_WS78_MARK,
};
static const unsigned int ssi8_data_pins[] = {
	/* SDATA */
	RCAR_GP_PIN(6, 20),
};
static const unsigned int ssi8_data_mux[] = {
	SSI_SDATA8_MARK,
};
static const unsigned int ssi9_data_a_pins[] = {
	/* SDATA */
	RCAR_GP_PIN(6, 21),
};
static const unsigned int ssi9_data_a_mux[] = {
	SSI_SDATA9_A_MARK,
};
static const unsigned int ssi9_data_b_pins[] = {
	/* SDATA */
	RCAR_GP_PIN(5, 14),
};
static const unsigned int ssi9_data_b_mux[] = {
	SSI_SDATA9_B_MARK,
};
static const unsigned int ssi9_ctrl_a_pins[] = {
	/* SCK, WS */
	RCAR_GP_PIN(5, 15), RCAR_GP_PIN(5, 16),
};
static const unsigned int ssi9_ctrl_a_mux[] = {
	SSI_SCK9_A_MARK, SSI_WS9_A_MARK,
};
static const unsigned int ssi9_ctrl_b_pins[] = {
	/* SCK, WS */
	RCAR_GP_PIN(6, 30), RCAR_GP_PIN(6, 31),
};
static const unsigned int ssi9_ctrl_b_mux[] = {
	SSI_SCK9_B_MARK, SSI_WS9_B_MARK,
};

/* - TMU -------------------------------------------------------------------- */
static const unsigned int tmu_tclk1_a_pins[] = {
	/* TCLK */
	RCAR_GP_PIN(6, 23),
};
static const unsigned int tmu_tclk1_a_mux[] = {
	TCLK1_A_MARK,
};
static const unsigned int tmu_tclk1_b_pins[] = {
	/* TCLK */
	RCAR_GP_PIN(5, 19),
};
static const unsigned int tmu_tclk1_b_mux[] = {
	TCLK1_B_MARK,
};
static const unsigned int tmu_tclk2_a_pins[] = {
	/* TCLK */
	RCAR_GP_PIN(6, 19),
};
static const unsigned int tmu_tclk2_a_mux[] = {
	TCLK2_A_MARK,
};
static const unsigned int tmu_tclk2_b_pins[] = {
	/* TCLK */
	RCAR_GP_PIN(6, 28),
};
static const unsigned int tmu_tclk2_b_mux[] = {
	TCLK2_B_MARK,
};

/* - TPU ------------------------------------------------------------------- */
static const unsigned int tpu_to0_pins[] = {
	/* TPU0TO0 */
	RCAR_GP_PIN(6, 28),
};
static const unsigned int tpu_to0_mux[] = {
	TPU0TO0_MARK,
};
static const unsigned int tpu_to1_pins[] = {
	/* TPU0TO1 */
	RCAR_GP_PIN(6, 29),
};
static const unsigned int tpu_to1_mux[] = {
	TPU0TO1_MARK,
};
static const unsigned int tpu_to2_pins[] = {
	/* TPU0TO2 */
	RCAR_GP_PIN(6, 30),
};
static const unsigned int tpu_to2_mux[] = {
	TPU0TO2_MARK,
};
static const unsigned int tpu_to3_pins[] = {
	/* TPU0TO3 */
	RCAR_GP_PIN(6, 31),
};
static const unsigned int tpu_to3_mux[] = {
	TPU0TO3_MARK,
};

/* - USB0 ------------------------------------------------------------------- */
static const unsigned int usb0_pins[] = {
	/* PWEN, OVC */
	RCAR_GP_PIN(6, 24), RCAR_GP_PIN(6, 25),
};
static const unsigned int usb0_mux[] = {
	USB0_PWEN_MARK, USB0_OVC_MARK,
};
/* - USB1 ------------------------------------------------------------------- */
static const unsigned int usb1_pins[] = {
	/* PWEN, OVC */
	RCAR_GP_PIN(6, 26), RCAR_GP_PIN(6, 27),
};
static const unsigned int usb1_mux[] = {
	USB1_PWEN_MARK, USB1_OVC_MARK,
};
/* - USB2 ------------------------------------------------------------------- */
static const unsigned int usb2_pins[] = {
	/* PWEN, OVC */
	RCAR_GP_PIN(6, 14), RCAR_GP_PIN(6, 15),
};
static const unsigned int usb2_mux[] = {
	USB2_PWEN_MARK, USB2_OVC_MARK,
};
/* - USB2_CH3 --------------------------------------------------------------- */
static const unsigned int usb2_ch3_pins[] = {
	/* PWEN, OVC */
	RCAR_GP_PIN(6, 30), RCAR_GP_PIN(6, 31),
};
static const unsigned int usb2_ch3_mux[] = {
	USB2_CH3_PWEN_MARK, USB2_CH3_OVC_MARK,
};

/* - USB30 ------------------------------------------------------------------ */
static const unsigned int usb30_pins[] = {
	/* PWEN, OVC */
	RCAR_GP_PIN(6, 28), RCAR_GP_PIN(6, 29),
};
static const unsigned int usb30_mux[] = {
	USB30_PWEN_MARK, USB30_OVC_MARK,
};

/* - VIN4 ------------------------------------------------------------------- */
static const unsigned int vin4_data18_a_pins[] = {
	RCAR_GP_PIN(0, 10), RCAR_GP_PIN(0, 11),
	RCAR_GP_PIN(0, 12), RCAR_GP_PIN(0, 13),
	RCAR_GP_PIN(0, 14), RCAR_GP_PIN(0, 15),
	RCAR_GP_PIN(1, 2), RCAR_GP_PIN(1, 3),
	RCAR_GP_PIN(1, 4), RCAR_GP_PIN(1, 5),
	RCAR_GP_PIN(1, 6), RCAR_GP_PIN(1, 7),
	RCAR_GP_PIN(0, 2), RCAR_GP_PIN(0, 3),
	RCAR_GP_PIN(0, 4), RCAR_GP_PIN(0, 5),
	RCAR_GP_PIN(0, 6), RCAR_GP_PIN(0, 7),
};
static const unsigned int vin4_data18_a_mux[] = {
	VI4_DATA2_A_MARK, VI4_DATA3_A_MARK,
	VI4_DATA4_A_MARK, VI4_DATA5_A_MARK,
	VI4_DATA6_A_MARK, VI4_DATA7_A_MARK,
	VI4_DATA10_MARK, VI4_DATA11_MARK,
	VI4_DATA12_MARK, VI4_DATA13_MARK,
	VI4_DATA14_MARK, VI4_DATA15_MARK,
	VI4_DATA18_MARK, VI4_DATA19_MARK,
	VI4_DATA20_MARK, VI4_DATA21_MARK,
	VI4_DATA22_MARK, VI4_DATA23_MARK,
};
static const unsigned int vin4_data18_b_pins[] = {
	RCAR_GP_PIN(2, 2), RCAR_GP_PIN(2, 3),
	RCAR_GP_PIN(2, 4), RCAR_GP_PIN(2, 5),
	RCAR_GP_PIN(2, 6), RCAR_GP_PIN(2, 7),
	RCAR_GP_PIN(1, 2), RCAR_GP_PIN(1, 3),
	RCAR_GP_PIN(1, 4), RCAR_GP_PIN(1, 5),
	RCAR_GP_PIN(1, 6), RCAR_GP_PIN(1, 7),
	RCAR_GP_PIN(0, 2), RCAR_GP_PIN(0, 3),
	RCAR_GP_PIN(0, 4), RCAR_GP_PIN(0, 5),
	RCAR_GP_PIN(0, 6), RCAR_GP_PIN(0, 7),
};
static const unsigned int vin4_data18_b_mux[] = {
	VI4_DATA2_B_MARK, VI4_DATA3_B_MARK,
	VI4_DATA4_B_MARK, VI4_DATA5_B_MARK,
	VI4_DATA6_B_MARK, VI4_DATA7_B_MARK,
	VI4_DATA10_MARK, VI4_DATA11_MARK,
	VI4_DATA12_MARK, VI4_DATA13_MARK,
	VI4_DATA14_MARK, VI4_DATA15_MARK,
	VI4_DATA18_MARK, VI4_DATA19_MARK,
	VI4_DATA20_MARK, VI4_DATA21_MARK,
	VI4_DATA22_MARK, VI4_DATA23_MARK,
};
static const union vin_data vin4_data_a_pins = {
	.data24 = {
		RCAR_GP_PIN(0, 8), RCAR_GP_PIN(0, 9),
		RCAR_GP_PIN(0, 10), RCAR_GP_PIN(0, 11),
		RCAR_GP_PIN(0, 12), RCAR_GP_PIN(0, 13),
		RCAR_GP_PIN(0, 14), RCAR_GP_PIN(0, 15),
		RCAR_GP_PIN(1, 0), RCAR_GP_PIN(1, 1),
		RCAR_GP_PIN(1, 2), RCAR_GP_PIN(1, 3),
		RCAR_GP_PIN(1, 4), RCAR_GP_PIN(1, 5),
		RCAR_GP_PIN(1, 6), RCAR_GP_PIN(1, 7),
		RCAR_GP_PIN(0, 0), RCAR_GP_PIN(0, 1),
		RCAR_GP_PIN(0, 2), RCAR_GP_PIN(0, 3),
		RCAR_GP_PIN(0, 4), RCAR_GP_PIN(0, 5),
		RCAR_GP_PIN(0, 6), RCAR_GP_PIN(0, 7),
	},
};
static const union vin_data vin4_data_a_mux = {
	.data24 = {
		VI4_DATA0_A_MARK, VI4_DATA1_A_MARK,
		VI4_DATA2_A_MARK, VI4_DATA3_A_MARK,
		VI4_DATA4_A_MARK, VI4_DATA5_A_MARK,
		VI4_DATA6_A_MARK, VI4_DATA7_A_MARK,
		VI4_DATA8_MARK,  VI4_DATA9_MARK,
		VI4_DATA10_MARK, VI4_DATA11_MARK,
		VI4_DATA12_MARK, VI4_DATA13_MARK,
		VI4_DATA14_MARK, VI4_DATA15_MARK,
		VI4_DATA16_MARK, VI4_DATA17_MARK,
		VI4_DATA18_MARK, VI4_DATA19_MARK,
		VI4_DATA20_MARK, VI4_DATA21_MARK,
		VI4_DATA22_MARK, VI4_DATA23_MARK,
	},
};
static const union vin_data vin4_data_b_pins = {
	.data24 = {
		RCAR_GP_PIN(2, 0), RCAR_GP_PIN(2, 1),
		RCAR_GP_PIN(2, 2), RCAR_GP_PIN(2, 3),
		RCAR_GP_PIN(2, 4), RCAR_GP_PIN(2, 5),
		RCAR_GP_PIN(2, 6), RCAR_GP_PIN(2, 7),
		RCAR_GP_PIN(1, 0), RCAR_GP_PIN(1, 1),
		RCAR_GP_PIN(1, 2), RCAR_GP_PIN(1, 3),
		RCAR_GP_PIN(1, 4), RCAR_GP_PIN(1, 5),
		RCAR_GP_PIN(1, 6), RCAR_GP_PIN(1, 7),
		RCAR_GP_PIN(0, 0), RCAR_GP_PIN(0, 1),
		RCAR_GP_PIN(0, 2), RCAR_GP_PIN(0, 3),
		RCAR_GP_PIN(0, 4), RCAR_GP_PIN(0, 5),
		RCAR_GP_PIN(0, 6), RCAR_GP_PIN(0, 7),
	},
};
static const union vin_data vin4_data_b_mux = {
	.data24 = {
		VI4_DATA0_B_MARK, VI4_DATA1_B_MARK,
		VI4_DATA2_B_MARK, VI4_DATA3_B_MARK,
		VI4_DATA4_B_MARK, VI4_DATA5_B_MARK,
		VI4_DATA6_B_MARK, VI4_DATA7_B_MARK,
		VI4_DATA8_MARK,  VI4_DATA9_MARK,
		VI4_DATA10_MARK, VI4_DATA11_MARK,
		VI4_DATA12_MARK, VI4_DATA13_MARK,
		VI4_DATA14_MARK, VI4_DATA15_MARK,
		VI4_DATA16_MARK, VI4_DATA17_MARK,
		VI4_DATA18_MARK, VI4_DATA19_MARK,
		VI4_DATA20_MARK, VI4_DATA21_MARK,
		VI4_DATA22_MARK, VI4_DATA23_MARK,
	},
};
static const unsigned int vin4_g8_pins[] = {
	RCAR_GP_PIN(1, 0),  RCAR_GP_PIN(1, 1),
	RCAR_GP_PIN(1, 2),  RCAR_GP_PIN(1, 3),
	RCAR_GP_PIN(1, 4),  RCAR_GP_PIN(1, 5),
	RCAR_GP_PIN(1, 6),  RCAR_GP_PIN(1, 7),
};
static const unsigned int vin4_g8_mux[] = {
	VI4_DATA8_MARK,  VI4_DATA9_MARK,
	VI4_DATA10_MARK, VI4_DATA11_MARK,
	VI4_DATA12_MARK, VI4_DATA13_MARK,
	VI4_DATA14_MARK, VI4_DATA15_MARK,
};
static const unsigned int vin4_sync_pins[] = {
	/* HSYNC#, VSYNC# */
	RCAR_GP_PIN(1, 18), RCAR_GP_PIN(1, 17),
};
static const unsigned int vin4_sync_mux[] = {
	VI4_HSYNC_N_MARK, VI4_VSYNC_N_MARK,
};
static const unsigned int vin4_field_pins[] = {
	/* FIELD */
	RCAR_GP_PIN(1, 16),
};
static const unsigned int vin4_field_mux[] = {
	VI4_FIELD_MARK,
};
static const unsigned int vin4_clkenb_pins[] = {
	/* CLKENB */
	RCAR_GP_PIN(1, 19),
};
static const unsigned int vin4_clkenb_mux[] = {
	VI4_CLKENB_MARK,
};
static const unsigned int vin4_clk_pins[] = {
	/* CLK */
	RCAR_GP_PIN(1, 27),
};
static const unsigned int vin4_clk_mux[] = {
	VI4_CLK_MARK,
};

/* - VIN5 ------------------------------------------------------------------- */
static const union vin_data16 vin5_data_pins = {
	.data16 = {
		RCAR_GP_PIN(0, 0), RCAR_GP_PIN(0, 1),
		RCAR_GP_PIN(0, 2), RCAR_GP_PIN(0, 3),
		RCAR_GP_PIN(0, 4), RCAR_GP_PIN(0, 5),
		RCAR_GP_PIN(0, 6), RCAR_GP_PIN(0, 7),
		RCAR_GP_PIN(1, 12), RCAR_GP_PIN(1, 13),
		RCAR_GP_PIN(1, 14), RCAR_GP_PIN(1, 15),
		RCAR_GP_PIN(1, 4), RCAR_GP_PIN(1, 5),
		RCAR_GP_PIN(1, 6), RCAR_GP_PIN(1, 7),
	},
};
static const union vin_data16 vin5_data_mux = {
	.data16 = {
		VI5_DATA0_MARK, VI5_DATA1_MARK,
		VI5_DATA2_MARK, VI5_DATA3_MARK,
		VI5_DATA4_MARK, VI5_DATA5_MARK,
		VI5_DATA6_MARK, VI5_DATA7_MARK,
		VI5_DATA8_MARK,  VI5_DATA9_MARK,
		VI5_DATA10_MARK, VI5_DATA11_MARK,
		VI5_DATA12_MARK, VI5_DATA13_MARK,
		VI5_DATA14_MARK, VI5_DATA15_MARK,
	},
};
static const unsigned int vin5_high8_pins[] = {
	RCAR_GP_PIN(1, 12), RCAR_GP_PIN(1, 13),
	RCAR_GP_PIN(1, 14), RCAR_GP_PIN(1, 15),
	RCAR_GP_PIN(1, 4),  RCAR_GP_PIN(1, 5),
	RCAR_GP_PIN(1, 6),  RCAR_GP_PIN(1, 7),
};
static const unsigned int vin5_high8_mux[] = {
	VI5_DATA8_MARK,  VI5_DATA9_MARK,
	VI5_DATA10_MARK, VI5_DATA11_MARK,
	VI5_DATA12_MARK, VI5_DATA13_MARK,
	VI5_DATA14_MARK, VI5_DATA15_MARK,
};
static const unsigned int vin5_sync_pins[] = {
	/* HSYNC#, VSYNC# */
	RCAR_GP_PIN(1, 10), RCAR_GP_PIN(1, 9),
};
static const unsigned int vin5_sync_mux[] = {
	VI5_HSYNC_N_MARK, VI5_VSYNC_N_MARK,
};
static const unsigned int vin5_field_pins[] = {
	RCAR_GP_PIN(1, 11),
};
static const unsigned int vin5_field_mux[] = {
	/* FIELD */
	VI5_FIELD_MARK,
};
static const unsigned int vin5_clkenb_pins[] = {
	RCAR_GP_PIN(1, 20),
};
static const unsigned int vin5_clkenb_mux[] = {
	/* CLKENB */
	VI5_CLKENB_MARK,
};
static const unsigned int vin5_clk_pins[] = {
	RCAR_GP_PIN(1, 21),
};
static const unsigned int vin5_clk_mux[] = {
	/* CLK */
	VI5_CLK_MARK,
};

static const struct {
	struct sh_pfc_pin_group common[328];
#ifdef CONFIG_PINCTRL_PFC_R8A77951
	struct sh_pfc_pin_group automotive[31];
#endif
} pinmux_groups = {
	.common = {
		SH_PFC_PIN_GROUP(audio_clk_a_a),
		SH_PFC_PIN_GROUP(audio_clk_a_b),
		SH_PFC_PIN_GROUP(audio_clk_a_c),
		SH_PFC_PIN_GROUP(audio_clk_b_a),
		SH_PFC_PIN_GROUP(audio_clk_b_b),
		SH_PFC_PIN_GROUP(audio_clk_c_a),
		SH_PFC_PIN_GROUP(audio_clk_c_b),
		SH_PFC_PIN_GROUP(audio_clkout_a),
		SH_PFC_PIN_GROUP(audio_clkout_b),
		SH_PFC_PIN_GROUP(audio_clkout_c),
		SH_PFC_PIN_GROUP(audio_clkout_d),
		SH_PFC_PIN_GROUP(audio_clkout1_a),
		SH_PFC_PIN_GROUP(audio_clkout1_b),
		SH_PFC_PIN_GROUP(audio_clkout2_a),
		SH_PFC_PIN_GROUP(audio_clkout2_b),
		SH_PFC_PIN_GROUP(audio_clkout3_a),
		SH_PFC_PIN_GROUP(audio_clkout3_b),
		SH_PFC_PIN_GROUP(avb_link),
		SH_PFC_PIN_GROUP(avb_magic),
		SH_PFC_PIN_GROUP(avb_phy_int),
		SH_PFC_PIN_GROUP_ALIAS(avb_mdc, avb_mdio),	/* Deprecated */
		SH_PFC_PIN_GROUP(avb_mdio),
		SH_PFC_PIN_GROUP(avb_mii),
		SH_PFC_PIN_GROUP(avb_avtp_pps),
		SH_PFC_PIN_GROUP(avb_avtp_match_a),
		SH_PFC_PIN_GROUP(avb_avtp_capture_a),
		SH_PFC_PIN_GROUP(avb_avtp_match_b),
		SH_PFC_PIN_GROUP(avb_avtp_capture_b),
		SH_PFC_PIN_GROUP(can0_data_a),
		SH_PFC_PIN_GROUP(can0_data_b),
		SH_PFC_PIN_GROUP(can1_data),
		SH_PFC_PIN_GROUP(can_clk),
		SH_PFC_PIN_GROUP(canfd0_data_a),
		SH_PFC_PIN_GROUP(canfd0_data_b),
		SH_PFC_PIN_GROUP(canfd1_data),
		SH_PFC_PIN_GROUP(du_rgb666),
		SH_PFC_PIN_GROUP(du_rgb888),
		SH_PFC_PIN_GROUP(du_clk_out_0),
		SH_PFC_PIN_GROUP(du_clk_out_1),
		SH_PFC_PIN_GROUP(du_sync),
		SH_PFC_PIN_GROUP(du_oddf),
		SH_PFC_PIN_GROUP(du_cde),
		SH_PFC_PIN_GROUP(du_disp),
		SH_PFC_PIN_GROUP(hscif0_data),
		SH_PFC_PIN_GROUP(hscif0_clk),
		SH_PFC_PIN_GROUP(hscif0_ctrl),
		SH_PFC_PIN_GROUP(hscif1_data_a),
		SH_PFC_PIN_GROUP(hscif1_clk_a),
		SH_PFC_PIN_GROUP(hscif1_ctrl_a),
		SH_PFC_PIN_GROUP(hscif1_data_b),
		SH_PFC_PIN_GROUP(hscif1_clk_b),
		SH_PFC_PIN_GROUP(hscif1_ctrl_b),
		SH_PFC_PIN_GROUP(hscif2_data_a),
		SH_PFC_PIN_GROUP(hscif2_clk_a),
		SH_PFC_PIN_GROUP(hscif2_ctrl_a),
		SH_PFC_PIN_GROUP(hscif2_data_b),
		SH_PFC_PIN_GROUP(hscif2_clk_b),
		SH_PFC_PIN_GROUP(hscif2_ctrl_b),
		SH_PFC_PIN_GROUP(hscif2_data_c),
		SH_PFC_PIN_GROUP(hscif2_clk_c),
		SH_PFC_PIN_GROUP(hscif2_ctrl_c),
		SH_PFC_PIN_GROUP(hscif3_data_a),
		SH_PFC_PIN_GROUP(hscif3_clk),
		SH_PFC_PIN_GROUP(hscif3_ctrl),
		SH_PFC_PIN_GROUP(hscif3_data_b),
		SH_PFC_PIN_GROUP(hscif3_data_c),
		SH_PFC_PIN_GROUP(hscif3_data_d),
		SH_PFC_PIN_GROUP(hscif4_data_a),
		SH_PFC_PIN_GROUP(hscif4_clk),
		SH_PFC_PIN_GROUP(hscif4_ctrl),
		SH_PFC_PIN_GROUP(hscif4_data_b),
		SH_PFC_PIN_GROUP(i2c0),
		SH_PFC_PIN_GROUP(i2c1_a),
		SH_PFC_PIN_GROUP(i2c1_b),
		SH_PFC_PIN_GROUP(i2c2_a),
		SH_PFC_PIN_GROUP(i2c2_b),
		SH_PFC_PIN_GROUP(i2c3),
		SH_PFC_PIN_GROUP(i2c5),
		SH_PFC_PIN_GROUP(i2c6_a),
		SH_PFC_PIN_GROUP(i2c6_b),
		SH_PFC_PIN_GROUP(i2c6_c),
		SH_PFC_PIN_GROUP(intc_ex_irq0),
		SH_PFC_PIN_GROUP(intc_ex_irq1),
		SH_PFC_PIN_GROUP(intc_ex_irq2),
		SH_PFC_PIN_GROUP(intc_ex_irq3),
		SH_PFC_PIN_GROUP(intc_ex_irq4),
		SH_PFC_PIN_GROUP(intc_ex_irq5),
		SH_PFC_PIN_GROUP(msiof0_clk),
		SH_PFC_PIN_GROUP(msiof0_sync),
		SH_PFC_PIN_GROUP(msiof0_ss1),
		SH_PFC_PIN_GROUP(msiof0_ss2),
		SH_PFC_PIN_GROUP(msiof0_txd),
		SH_PFC_PIN_GROUP(msiof0_rxd),
		SH_PFC_PIN_GROUP(msiof1_clk_a),
		SH_PFC_PIN_GROUP(msiof1_sync_a),
		SH_PFC_PIN_GROUP(msiof1_ss1_a),
		SH_PFC_PIN_GROUP(msiof1_ss2_a),
		SH_PFC_PIN_GROUP(msiof1_txd_a),
		SH_PFC_PIN_GROUP(msiof1_rxd_a),
		SH_PFC_PIN_GROUP(msiof1_clk_b),
		SH_PFC_PIN_GROUP(msiof1_sync_b),
		SH_PFC_PIN_GROUP(msiof1_ss1_b),
		SH_PFC_PIN_GROUP(msiof1_ss2_b),
		SH_PFC_PIN_GROUP(msiof1_txd_b),
		SH_PFC_PIN_GROUP(msiof1_rxd_b),
		SH_PFC_PIN_GROUP(msiof1_clk_c),
		SH_PFC_PIN_GROUP(msiof1_sync_c),
		SH_PFC_PIN_GROUP(msiof1_ss1_c),
		SH_PFC_PIN_GROUP(msiof1_ss2_c),
		SH_PFC_PIN_GROUP(msiof1_txd_c),
		SH_PFC_PIN_GROUP(msiof1_rxd_c),
		SH_PFC_PIN_GROUP(msiof1_clk_d),
		SH_PFC_PIN_GROUP(msiof1_sync_d),
		SH_PFC_PIN_GROUP(msiof1_ss1_d),
		SH_PFC_PIN_GROUP(msiof1_ss2_d),
		SH_PFC_PIN_GROUP(msiof1_txd_d),
		SH_PFC_PIN_GROUP(msiof1_rxd_d),
		SH_PFC_PIN_GROUP(msiof1_clk_e),
		SH_PFC_PIN_GROUP(msiof1_sync_e),
		SH_PFC_PIN_GROUP(msiof1_ss1_e),
		SH_PFC_PIN_GROUP(msiof1_ss2_e),
		SH_PFC_PIN_GROUP(msiof1_txd_e),
		SH_PFC_PIN_GROUP(msiof1_rxd_e),
		SH_PFC_PIN_GROUP(msiof1_clk_f),
		SH_PFC_PIN_GROUP(msiof1_sync_f),
		SH_PFC_PIN_GROUP(msiof1_ss1_f),
		SH_PFC_PIN_GROUP(msiof1_ss2_f),
		SH_PFC_PIN_GROUP(msiof1_txd_f),
		SH_PFC_PIN_GROUP(msiof1_rxd_f),
		SH_PFC_PIN_GROUP(msiof1_clk_g),
		SH_PFC_PIN_GROUP(msiof1_sync_g),
		SH_PFC_PIN_GROUP(msiof1_ss1_g),
		SH_PFC_PIN_GROUP(msiof1_ss2_g),
		SH_PFC_PIN_GROUP(msiof1_txd_g),
		SH_PFC_PIN_GROUP(msiof1_rxd_g),
		SH_PFC_PIN_GROUP(msiof2_clk_a),
		SH_PFC_PIN_GROUP(msiof2_sync_a),
		SH_PFC_PIN_GROUP(msiof2_ss1_a),
		SH_PFC_PIN_GROUP(msiof2_ss2_a),
		SH_PFC_PIN_GROUP(msiof2_txd_a),
		SH_PFC_PIN_GROUP(msiof2_rxd_a),
		SH_PFC_PIN_GROUP(msiof2_clk_b),
		SH_PFC_PIN_GROUP(msiof2_sync_b),
		SH_PFC_PIN_GROUP(msiof2_ss1_b),
		SH_PFC_PIN_GROUP(msiof2_ss2_b),
		SH_PFC_PIN_GROUP(msiof2_txd_b),
		SH_PFC_PIN_GROUP(msiof2_rxd_b),
		SH_PFC_PIN_GROUP(msiof2_clk_c),
		SH_PFC_PIN_GROUP(msiof2_sync_c),
		SH_PFC_PIN_GROUP(msiof2_ss1_c),
		SH_PFC_PIN_GROUP(msiof2_ss2_c),
		SH_PFC_PIN_GROUP(msiof2_txd_c),
		SH_PFC_PIN_GROUP(msiof2_rxd_c),
		SH_PFC_PIN_GROUP(msiof2_clk_d),
		SH_PFC_PIN_GROUP(msiof2_sync_d),
		SH_PFC_PIN_GROUP(msiof2_ss1_d),
		SH_PFC_PIN_GROUP(msiof2_ss2_d),
		SH_PFC_PIN_GROUP(msiof2_txd_d),
		SH_PFC_PIN_GROUP(msiof2_rxd_d),
		SH_PFC_PIN_GROUP(msiof3_clk_a),
		SH_PFC_PIN_GROUP(msiof3_sync_a),
		SH_PFC_PIN_GROUP(msiof3_ss1_a),
		SH_PFC_PIN_GROUP(msiof3_ss2_a),
		SH_PFC_PIN_GROUP(msiof3_txd_a),
		SH_PFC_PIN_GROUP(msiof3_rxd_a),
		SH_PFC_PIN_GROUP(msiof3_clk_b),
		SH_PFC_PIN_GROUP(msiof3_sync_b),
		SH_PFC_PIN_GROUP(msiof3_ss1_b),
		SH_PFC_PIN_GROUP(msiof3_ss2_b),
		SH_PFC_PIN_GROUP(msiof3_txd_b),
		SH_PFC_PIN_GROUP(msiof3_rxd_b),
		SH_PFC_PIN_GROUP(msiof3_clk_c),
		SH_PFC_PIN_GROUP(msiof3_sync_c),
		SH_PFC_PIN_GROUP(msiof3_txd_c),
		SH_PFC_PIN_GROUP(msiof3_rxd_c),
		SH_PFC_PIN_GROUP(msiof3_clk_d),
		SH_PFC_PIN_GROUP(msiof3_sync_d),
		SH_PFC_PIN_GROUP(msiof3_ss1_d),
		SH_PFC_PIN_GROUP(msiof3_txd_d),
		SH_PFC_PIN_GROUP(msiof3_rxd_d),
		SH_PFC_PIN_GROUP(msiof3_clk_e),
		SH_PFC_PIN_GROUP(msiof3_sync_e),
		SH_PFC_PIN_GROUP(msiof3_ss1_e),
		SH_PFC_PIN_GROUP(msiof3_ss2_e),
		SH_PFC_PIN_GROUP(msiof3_txd_e),
		SH_PFC_PIN_GROUP(msiof3_rxd_e),
		SH_PFC_PIN_GROUP(pwm0),
		SH_PFC_PIN_GROUP(pwm1_a),
		SH_PFC_PIN_GROUP(pwm1_b),
		SH_PFC_PIN_GROUP(pwm2_a),
		SH_PFC_PIN_GROUP(pwm2_b),
		SH_PFC_PIN_GROUP(pwm3_a),
		SH_PFC_PIN_GROUP(pwm3_b),
		SH_PFC_PIN_GROUP(pwm4_a),
		SH_PFC_PIN_GROUP(pwm4_b),
		SH_PFC_PIN_GROUP(pwm5_a),
		SH_PFC_PIN_GROUP(pwm5_b),
		SH_PFC_PIN_GROUP(pwm6_a),
		SH_PFC_PIN_GROUP(pwm6_b),
		SH_PFC_PIN_GROUP(qspi0_ctrl),
		SH_PFC_PIN_GROUP(qspi0_data2),
		SH_PFC_PIN_GROUP(qspi0_data4),
		SH_PFC_PIN_GROUP(qspi1_ctrl),
		SH_PFC_PIN_GROUP(qspi1_data2),
		SH_PFC_PIN_GROUP(qspi1_data4),
		SH_PFC_PIN_GROUP(sata0_devslp_a),
		SH_PFC_PIN_GROUP(sata0_devslp_b),
		SH_PFC_PIN_GROUP(scif0_data),
		SH_PFC_PIN_GROUP(scif0_clk),
		SH_PFC_PIN_GROUP(scif0_ctrl),
		SH_PFC_PIN_GROUP(scif1_data_a),
		SH_PFC_PIN_GROUP(scif1_clk),
		SH_PFC_PIN_GROUP(scif1_ctrl),
		SH_PFC_PIN_GROUP(scif1_data_b),
		SH_PFC_PIN_GROUP(scif2_data_a),
		SH_PFC_PIN_GROUP(scif2_clk),
		SH_PFC_PIN_GROUP(scif2_data_b),
		SH_PFC_PIN_GROUP(scif3_data_a),
		SH_PFC_PIN_GROUP(scif3_clk),
		SH_PFC_PIN_GROUP(scif3_ctrl),
		SH_PFC_PIN_GROUP(scif3_data_b),
		SH_PFC_PIN_GROUP(scif4_data_a),
		SH_PFC_PIN_GROUP(scif4_clk_a),
		SH_PFC_PIN_GROUP(scif4_ctrl_a),
		SH_PFC_PIN_GROUP(scif4_data_b),
		SH_PFC_PIN_GROUP(scif4_clk_b),
		SH_PFC_PIN_GROUP(scif4_ctrl_b),
		SH_PFC_PIN_GROUP(scif4_data_c),
		SH_PFC_PIN_GROUP(scif4_clk_c),
		SH_PFC_PIN_GROUP(scif4_ctrl_c),
		SH_PFC_PIN_GROUP(scif5_data_a),
		SH_PFC_PIN_GROUP(scif5_clk_a),
		SH_PFC_PIN_GROUP(scif5_data_b),
		SH_PFC_PIN_GROUP(scif5_clk_b),
		SH_PFC_PIN_GROUP(scif_clk_a),
		SH_PFC_PIN_GROUP(scif_clk_b),
		SH_PFC_PIN_GROUP(sdhi0_data1),
		SH_PFC_PIN_GROUP(sdhi0_data4),
		SH_PFC_PIN_GROUP(sdhi0_ctrl),
		SH_PFC_PIN_GROUP(sdhi0_cd),
		SH_PFC_PIN_GROUP(sdhi0_wp),
		SH_PFC_PIN_GROUP(sdhi1_data1),
		SH_PFC_PIN_GROUP(sdhi1_data4),
		SH_PFC_PIN_GROUP(sdhi1_ctrl),
		SH_PFC_PIN_GROUP(sdhi1_cd),
		SH_PFC_PIN_GROUP(sdhi1_wp),
		SH_PFC_PIN_GROUP(sdhi2_data1),
		SH_PFC_PIN_GROUP(sdhi2_data4),
		SH_PFC_PIN_GROUP(sdhi2_data8),
		SH_PFC_PIN_GROUP(sdhi2_ctrl),
		SH_PFC_PIN_GROUP(sdhi2_cd_a),
		SH_PFC_PIN_GROUP(sdhi2_wp_a),
		SH_PFC_PIN_GROUP(sdhi2_cd_b),
		SH_PFC_PIN_GROUP(sdhi2_wp_b),
		SH_PFC_PIN_GROUP(sdhi2_ds),
		SH_PFC_PIN_GROUP(sdhi3_data1),
		SH_PFC_PIN_GROUP(sdhi3_data4),
		SH_PFC_PIN_GROUP(sdhi3_data8),
		SH_PFC_PIN_GROUP(sdhi3_ctrl),
		SH_PFC_PIN_GROUP(sdhi3_cd),
		SH_PFC_PIN_GROUP(sdhi3_wp),
		SH_PFC_PIN_GROUP(sdhi3_ds),
		SH_PFC_PIN_GROUP(ssi0_data),
		SH_PFC_PIN_GROUP(ssi01239_ctrl),
		SH_PFC_PIN_GROUP(ssi1_data_a),
		SH_PFC_PIN_GROUP(ssi1_data_b),
		SH_PFC_PIN_GROUP(ssi1_ctrl_a),
		SH_PFC_PIN_GROUP(ssi1_ctrl_b),
		SH_PFC_PIN_GROUP(ssi2_data_a),
		SH_PFC_PIN_GROUP(ssi2_data_b),
		SH_PFC_PIN_GROUP(ssi2_ctrl_a),
		SH_PFC_PIN_GROUP(ssi2_ctrl_b),
		SH_PFC_PIN_GROUP(ssi3_data),
		SH_PFC_PIN_GROUP(ssi349_ctrl),
		SH_PFC_PIN_GROUP(ssi4_data),
		SH_PFC_PIN_GROUP(ssi4_ctrl),
		SH_PFC_PIN_GROUP(ssi5_data),
		SH_PFC_PIN_GROUP(ssi5_ctrl),
		SH_PFC_PIN_GROUP(ssi6_data),
		SH_PFC_PIN_GROUP(ssi6_ctrl),
		SH_PFC_PIN_GROUP(ssi7_data),
		SH_PFC_PIN_GROUP(ssi78_ctrl),
		SH_PFC_PIN_GROUP(ssi8_data),
		SH_PFC_PIN_GROUP(ssi9_data_a),
		SH_PFC_PIN_GROUP(ssi9_data_b),
		SH_PFC_PIN_GROUP(ssi9_ctrl_a),
		SH_PFC_PIN_GROUP(ssi9_ctrl_b),
		SH_PFC_PIN_GROUP(tmu_tclk1_a),
		SH_PFC_PIN_GROUP(tmu_tclk1_b),
		SH_PFC_PIN_GROUP(tmu_tclk2_a),
		SH_PFC_PIN_GROUP(tmu_tclk2_b),
		SH_PFC_PIN_GROUP(tpu_to0),
		SH_PFC_PIN_GROUP(tpu_to1),
		SH_PFC_PIN_GROUP(tpu_to2),
		SH_PFC_PIN_GROUP(tpu_to3),
		SH_PFC_PIN_GROUP(usb0),
		SH_PFC_PIN_GROUP(usb1),
		SH_PFC_PIN_GROUP(usb2),
		SH_PFC_PIN_GROUP(usb2_ch3),
		SH_PFC_PIN_GROUP(usb30),
		VIN_DATA_PIN_GROUP(vin4_data, 8, _a),
		VIN_DATA_PIN_GROUP(vin4_data, 10, _a),
		VIN_DATA_PIN_GROUP(vin4_data, 12, _a),
		VIN_DATA_PIN_GROUP(vin4_data, 16, _a),
		SH_PFC_PIN_GROUP(vin4_data18_a),
		VIN_DATA_PIN_GROUP(vin4_data, 20, _a),
		VIN_DATA_PIN_GROUP(vin4_data, 24, _a),
		VIN_DATA_PIN_GROUP(vin4_data, 8, _b),
		VIN_DATA_PIN_GROUP(vin4_data, 10, _b),
		VIN_DATA_PIN_GROUP(vin4_data, 12, _b),
		VIN_DATA_PIN_GROUP(vin4_data, 16, _b),
		SH_PFC_PIN_GROUP(vin4_data18_b),
		VIN_DATA_PIN_GROUP(vin4_data, 20, _b),
		VIN_DATA_PIN_GROUP(vin4_data, 24, _b),
		SH_PFC_PIN_GROUP(vin4_g8),
		SH_PFC_PIN_GROUP(vin4_sync),
		SH_PFC_PIN_GROUP(vin4_field),
		SH_PFC_PIN_GROUP(vin4_clkenb),
		SH_PFC_PIN_GROUP(vin4_clk),
		VIN_DATA_PIN_GROUP(vin5_data, 8),
		VIN_DATA_PIN_GROUP(vin5_data, 10),
		VIN_DATA_PIN_GROUP(vin5_data, 12),
		VIN_DATA_PIN_GROUP(vin5_data, 16),
		SH_PFC_PIN_GROUP(vin5_high8),
		SH_PFC_PIN_GROUP(vin5_sync),
		SH_PFC_PIN_GROUP(vin5_field),
		SH_PFC_PIN_GROUP(vin5_clkenb),
		SH_PFC_PIN_GROUP(vin5_clk),
	},
#ifdef CONFIG_PINCTRL_PFC_R8A77951
	.automotive = {
		SH_PFC_PIN_GROUP(drif0_ctrl_a),
		SH_PFC_PIN_GROUP(drif0_data0_a),
		SH_PFC_PIN_GROUP(drif0_data1_a),
		SH_PFC_PIN_GROUP(drif0_ctrl_b),
		SH_PFC_PIN_GROUP(drif0_data0_b),
		SH_PFC_PIN_GROUP(drif0_data1_b),
		SH_PFC_PIN_GROUP(drif0_ctrl_c),
		SH_PFC_PIN_GROUP(drif0_data0_c),
		SH_PFC_PIN_GROUP(drif0_data1_c),
		SH_PFC_PIN_GROUP(drif1_ctrl_a),
		SH_PFC_PIN_GROUP(drif1_data0_a),
		SH_PFC_PIN_GROUP(drif1_data1_a),
		SH_PFC_PIN_GROUP(drif1_ctrl_b),
		SH_PFC_PIN_GROUP(drif1_data0_b),
		SH_PFC_PIN_GROUP(drif1_data1_b),
		SH_PFC_PIN_GROUP(drif1_ctrl_c),
		SH_PFC_PIN_GROUP(drif1_data0_c),
		SH_PFC_PIN_GROUP(drif1_data1_c),
		SH_PFC_PIN_GROUP(drif2_ctrl_a),
		SH_PFC_PIN_GROUP(drif2_data0_a),
		SH_PFC_PIN_GROUP(drif2_data1_a),
		SH_PFC_PIN_GROUP(drif2_ctrl_b),
		SH_PFC_PIN_GROUP(drif2_data0_b),
		SH_PFC_PIN_GROUP(drif2_data1_b),
		SH_PFC_PIN_GROUP(drif3_ctrl_a),
		SH_PFC_PIN_GROUP(drif3_data0_a),
		SH_PFC_PIN_GROUP(drif3_data1_a),
		SH_PFC_PIN_GROUP(drif3_ctrl_b),
		SH_PFC_PIN_GROUP(drif3_data0_b),
		SH_PFC_PIN_GROUP(drif3_data1_b),
		SH_PFC_PIN_GROUP(mlb_3pin),
	}
#endif /* CONFIG_PINCTRL_PFC_R8A77951 */
};

static const char * const audio_clk_groups[] = {
	"audio_clk_a_a",
	"audio_clk_a_b",
	"audio_clk_a_c",
	"audio_clk_b_a",
	"audio_clk_b_b",
	"audio_clk_c_a",
	"audio_clk_c_b",
	"audio_clkout_a",
	"audio_clkout_b",
	"audio_clkout_c",
	"audio_clkout_d",
	"audio_clkout1_a",
	"audio_clkout1_b",
	"audio_clkout2_a",
	"audio_clkout2_b",
	"audio_clkout3_a",
	"audio_clkout3_b",
};

static const char * const avb_groups[] = {
	"avb_link",
	"avb_magic",
	"avb_phy_int",
	"avb_mdc",	/* Deprecated, please use "avb_mdio" instead */
	"avb_mdio",
	"avb_mii",
	"avb_avtp_pps",
	"avb_avtp_match_a",
	"avb_avtp_capture_a",
	"avb_avtp_match_b",
	"avb_avtp_capture_b",
};

static const char * const can0_groups[] = {
	"can0_data_a",
	"can0_data_b",
};

static const char * const can1_groups[] = {
	"can1_data",
};

static const char * const can_clk_groups[] = {
	"can_clk",
};

static const char * const canfd0_groups[] = {
	"canfd0_data_a",
	"canfd0_data_b",
};

static const char * const canfd1_groups[] = {
	"canfd1_data",
};

#ifdef CONFIG_PINCTRL_PFC_R8A77951
static const char * const drif0_groups[] = {
	"drif0_ctrl_a",
	"drif0_data0_a",
	"drif0_data1_a",
	"drif0_ctrl_b",
	"drif0_data0_b",
	"drif0_data1_b",
	"drif0_ctrl_c",
	"drif0_data0_c",
	"drif0_data1_c",
};

static const char * const drif1_groups[] = {
	"drif1_ctrl_a",
	"drif1_data0_a",
	"drif1_data1_a",
	"drif1_ctrl_b",
	"drif1_data0_b",
	"drif1_data1_b",
	"drif1_ctrl_c",
	"drif1_data0_c",
	"drif1_data1_c",
};

static const char * const drif2_groups[] = {
	"drif2_ctrl_a",
	"drif2_data0_a",
	"drif2_data1_a",
	"drif2_ctrl_b",
	"drif2_data0_b",
	"drif2_data1_b",
};

static const char * const drif3_groups[] = {
	"drif3_ctrl_a",
	"drif3_data0_a",
	"drif3_data1_a",
	"drif3_ctrl_b",
	"drif3_data0_b",
	"drif3_data1_b",
};
#endif /* CONFIG_PINCTRL_PFC_R8A77951 */

static const char * const du_groups[] = {
	"du_rgb666",
	"du_rgb888",
	"du_clk_out_0",
	"du_clk_out_1",
	"du_sync",
	"du_oddf",
	"du_cde",
	"du_disp",
};

static const char * const hscif0_groups[] = {
	"hscif0_data",
	"hscif0_clk",
	"hscif0_ctrl",
};

static const char * const hscif1_groups[] = {
	"hscif1_data_a",
	"hscif1_clk_a",
	"hscif1_ctrl_a",
	"hscif1_data_b",
	"hscif1_clk_b",
	"hscif1_ctrl_b",
};

static const char * const hscif2_groups[] = {
	"hscif2_data_a",
	"hscif2_clk_a",
	"hscif2_ctrl_a",
	"hscif2_data_b",
	"hscif2_clk_b",
	"hscif2_ctrl_b",
	"hscif2_data_c",
	"hscif2_clk_c",
	"hscif2_ctrl_c",
};

static const char * const hscif3_groups[] = {
	"hscif3_data_a",
	"hscif3_clk",
	"hscif3_ctrl",
	"hscif3_data_b",
	"hscif3_data_c",
	"hscif3_data_d",
};

static const char * const hscif4_groups[] = {
	"hscif4_data_a",
	"hscif4_clk",
	"hscif4_ctrl",
	"hscif4_data_b",
};

static const char * const i2c0_groups[] = {
	"i2c0",
};

static const char * const i2c1_groups[] = {
	"i2c1_a",
	"i2c1_b",
};

static const char * const i2c2_groups[] = {
	"i2c2_a",
	"i2c2_b",
};

static const char * const i2c3_groups[] = {
	"i2c3",
};

static const char * const i2c5_groups[] = {
	"i2c5",
};

static const char * const i2c6_groups[] = {
	"i2c6_a",
	"i2c6_b",
	"i2c6_c",
};

static const char * const intc_ex_groups[] = {
	"intc_ex_irq0",
	"intc_ex_irq1",
	"intc_ex_irq2",
	"intc_ex_irq3",
	"intc_ex_irq4",
	"intc_ex_irq5",
};

#ifdef CONFIG_PINCTRL_PFC_R8A77951
static const char * const mlb_3pin_groups[] = {
	"mlb_3pin",
};
#endif /* CONFIG_PINCTRL_PFC_R8A77951 */

static const char * const msiof0_groups[] = {
	"msiof0_clk",
	"msiof0_sync",
	"msiof0_ss1",
	"msiof0_ss2",
	"msiof0_txd",
	"msiof0_rxd",
};

static const char * const msiof1_groups[] = {
	"msiof1_clk_a",
	"msiof1_sync_a",
	"msiof1_ss1_a",
	"msiof1_ss2_a",
	"msiof1_txd_a",
	"msiof1_rxd_a",
	"msiof1_clk_b",
	"msiof1_sync_b",
	"msiof1_ss1_b",
	"msiof1_ss2_b",
	"msiof1_txd_b",
	"msiof1_rxd_b",
	"msiof1_clk_c",
	"msiof1_sync_c",
	"msiof1_ss1_c",
	"msiof1_ss2_c",
	"msiof1_txd_c",
	"msiof1_rxd_c",
	"msiof1_clk_d",
	"msiof1_sync_d",
	"msiof1_ss1_d",
	"msiof1_ss2_d",
	"msiof1_txd_d",
	"msiof1_rxd_d",
	"msiof1_clk_e",
	"msiof1_sync_e",
	"msiof1_ss1_e",
	"msiof1_ss2_e",
	"msiof1_txd_e",
	"msiof1_rxd_e",
	"msiof1_clk_f",
	"msiof1_sync_f",
	"msiof1_ss1_f",
	"msiof1_ss2_f",
	"msiof1_txd_f",
	"msiof1_rxd_f",
	"msiof1_clk_g",
	"msiof1_sync_g",
	"msiof1_ss1_g",
	"msiof1_ss2_g",
	"msiof1_txd_g",
	"msiof1_rxd_g",
};

static const char * const msiof2_groups[] = {
	"msiof2_clk_a",
	"msiof2_sync_a",
	"msiof2_ss1_a",
	"msiof2_ss2_a",
	"msiof2_txd_a",
	"msiof2_rxd_a",
	"msiof2_clk_b",
	"msiof2_sync_b",
	"msiof2_ss1_b",
	"msiof2_ss2_b",
	"msiof2_txd_b",
	"msiof2_rxd_b",
	"msiof2_clk_c",
	"msiof2_sync_c",
	"msiof2_ss1_c",
	"msiof2_ss2_c",
	"msiof2_txd_c",
	"msiof2_rxd_c",
	"msiof2_clk_d",
	"msiof2_sync_d",
	"msiof2_ss1_d",
	"msiof2_ss2_d",
	"msiof2_txd_d",
	"msiof2_rxd_d",
};

static const char * const msiof3_groups[] = {
	"msiof3_clk_a",
	"msiof3_sync_a",
	"msiof3_ss1_a",
	"msiof3_ss2_a",
	"msiof3_txd_a",
	"msiof3_rxd_a",
	"msiof3_clk_b",
	"msiof3_sync_b",
	"msiof3_ss1_b",
	"msiof3_ss2_b",
	"msiof3_txd_b",
	"msiof3_rxd_b",
	"msiof3_clk_c",
	"msiof3_sync_c",
	"msiof3_txd_c",
	"msiof3_rxd_c",
	"msiof3_clk_d",
	"msiof3_sync_d",
	"msiof3_ss1_d",
	"msiof3_txd_d",
	"msiof3_rxd_d",
	"msiof3_clk_e",
	"msiof3_sync_e",
	"msiof3_ss1_e",
	"msiof3_ss2_e",
	"msiof3_txd_e",
	"msiof3_rxd_e",
};

static const char * const pwm0_groups[] = {
	"pwm0",
};

static const char * const pwm1_groups[] = {
	"pwm1_a",
	"pwm1_b",
};

static const char * const pwm2_groups[] = {
	"pwm2_a",
	"pwm2_b",
};

static const char * const pwm3_groups[] = {
	"pwm3_a",
	"pwm3_b",
};

static const char * const pwm4_groups[] = {
	"pwm4_a",
	"pwm4_b",
};

static const char * const pwm5_groups[] = {
	"pwm5_a",
	"pwm5_b",
};

static const char * const pwm6_groups[] = {
	"pwm6_a",
	"pwm6_b",
};

static const char * const qspi0_groups[] = {
	"qspi0_ctrl",
	"qspi0_data2",
	"qspi0_data4",
};

static const char * const qspi1_groups[] = {
	"qspi1_ctrl",
	"qspi1_data2",
	"qspi1_data4",
};

static const char * const sata0_groups[] = {
	"sata0_devslp_a",
	"sata0_devslp_b",
};

static const char * const scif0_groups[] = {
	"scif0_data",
	"scif0_clk",
	"scif0_ctrl",
};

static const char * const scif1_groups[] = {
	"scif1_data_a",
	"scif1_clk",
	"scif1_ctrl",
	"scif1_data_b",
};

static const char * const scif2_groups[] = {
	"scif2_data_a",
	"scif2_clk",
	"scif2_data_b",
};

static const char * const scif3_groups[] = {
	"scif3_data_a",
	"scif3_clk",
	"scif3_ctrl",
	"scif3_data_b",
};

static const char * const scif4_groups[] = {
	"scif4_data_a",
	"scif4_clk_a",
	"scif4_ctrl_a",
	"scif4_data_b",
	"scif4_clk_b",
	"scif4_ctrl_b",
	"scif4_data_c",
	"scif4_clk_c",
	"scif4_ctrl_c",
};

static const char * const scif5_groups[] = {
	"scif5_data_a",
	"scif5_clk_a",
	"scif5_data_b",
	"scif5_clk_b",
};

static const char * const scif_clk_groups[] = {
	"scif_clk_a",
	"scif_clk_b",
};

static const char * const sdhi0_groups[] = {
	"sdhi0_data1",
	"sdhi0_data4",
	"sdhi0_ctrl",
	"sdhi0_cd",
	"sdhi0_wp",
};

static const char * const sdhi1_groups[] = {
	"sdhi1_data1",
	"sdhi1_data4",
	"sdhi1_ctrl",
	"sdhi1_cd",
	"sdhi1_wp",
};

static const char * const sdhi2_groups[] = {
	"sdhi2_data1",
	"sdhi2_data4",
	"sdhi2_data8",
	"sdhi2_ctrl",
	"sdhi2_cd_a",
	"sdhi2_wp_a",
	"sdhi2_cd_b",
	"sdhi2_wp_b",
	"sdhi2_ds",
};

static const char * const sdhi3_groups[] = {
	"sdhi3_data1",
	"sdhi3_data4",
	"sdhi3_data8",
	"sdhi3_ctrl",
	"sdhi3_cd",
	"sdhi3_wp",
	"sdhi3_ds",
};

static const char * const ssi_groups[] = {
	"ssi0_data",
	"ssi01239_ctrl",
	"ssi1_data_a",
	"ssi1_data_b",
	"ssi1_ctrl_a",
	"ssi1_ctrl_b",
	"ssi2_data_a",
	"ssi2_data_b",
	"ssi2_ctrl_a",
	"ssi2_ctrl_b",
	"ssi3_data",
	"ssi349_ctrl",
	"ssi4_data",
	"ssi4_ctrl",
	"ssi5_data",
	"ssi5_ctrl",
	"ssi6_data",
	"ssi6_ctrl",
	"ssi7_data",
	"ssi78_ctrl",
	"ssi8_data",
	"ssi9_data_a",
	"ssi9_data_b",
	"ssi9_ctrl_a",
	"ssi9_ctrl_b",
};

static const char * const tmu_groups[] = {
	"tmu_tclk1_a",
	"tmu_tclk1_b",
	"tmu_tclk2_a",
	"tmu_tclk2_b",
};

static const char * const tpu_groups[] = {
	"tpu_to0",
	"tpu_to1",
	"tpu_to2",
	"tpu_to3",
};

static const char * const usb0_groups[] = {
	"usb0",
};

static const char * const usb1_groups[] = {
	"usb1",
};

static const char * const usb2_groups[] = {
	"usb2",
};

static const char * const usb2_ch3_groups[] = {
	"usb2_ch3",
};

static const char * const usb30_groups[] = {
	"usb30",
};

static const char * const vin4_groups[] = {
	"vin4_data8_a",
	"vin4_data10_a",
	"vin4_data12_a",
	"vin4_data16_a",
	"vin4_data18_a",
	"vin4_data20_a",
	"vin4_data24_a",
	"vin4_data8_b",
	"vin4_data10_b",
	"vin4_data12_b",
	"vin4_data16_b",
	"vin4_data18_b",
	"vin4_data20_b",
	"vin4_data24_b",
	"vin4_g8",
	"vin4_sync",
	"vin4_field",
	"vin4_clkenb",
	"vin4_clk",
};

static const char * const vin5_groups[] = {
	"vin5_data8",
	"vin5_data10",
	"vin5_data12",
	"vin5_data16",
	"vin5_high8",
	"vin5_sync",
	"vin5_field",
	"vin5_clkenb",
	"vin5_clk",
};

static const struct {
	struct sh_pfc_function common[55];
#ifdef CONFIG_PINCTRL_PFC_R8A77951
	struct sh_pfc_function automotive[5];
#endif
} pinmux_functions = {
	.common = {
		SH_PFC_FUNCTION(audio_clk),
		SH_PFC_FUNCTION(avb),
		SH_PFC_FUNCTION(can0),
		SH_PFC_FUNCTION(can1),
		SH_PFC_FUNCTION(can_clk),
		SH_PFC_FUNCTION(canfd0),
		SH_PFC_FUNCTION(canfd1),
		SH_PFC_FUNCTION(du),
		SH_PFC_FUNCTION(hscif0),
		SH_PFC_FUNCTION(hscif1),
		SH_PFC_FUNCTION(hscif2),
		SH_PFC_FUNCTION(hscif3),
		SH_PFC_FUNCTION(hscif4),
		SH_PFC_FUNCTION(i2c0),
		SH_PFC_FUNCTION(i2c1),
		SH_PFC_FUNCTION(i2c2),
		SH_PFC_FUNCTION(i2c3),
		SH_PFC_FUNCTION(i2c5),
		SH_PFC_FUNCTION(i2c6),
		SH_PFC_FUNCTION(intc_ex),
		SH_PFC_FUNCTION(msiof0),
		SH_PFC_FUNCTION(msiof1),
		SH_PFC_FUNCTION(msiof2),
		SH_PFC_FUNCTION(msiof3),
		SH_PFC_FUNCTION(pwm0),
		SH_PFC_FUNCTION(pwm1),
		SH_PFC_FUNCTION(pwm2),
		SH_PFC_FUNCTION(pwm3),
		SH_PFC_FUNCTION(pwm4),
		SH_PFC_FUNCTION(pwm5),
		SH_PFC_FUNCTION(pwm6),
		SH_PFC_FUNCTION(qspi0),
		SH_PFC_FUNCTION(qspi1),
		SH_PFC_FUNCTION(sata0),
		SH_PFC_FUNCTION(scif0),
		SH_PFC_FUNCTION(scif1),
		SH_PFC_FUNCTION(scif2),
		SH_PFC_FUNCTION(scif3),
		SH_PFC_FUNCTION(scif4),
		SH_PFC_FUNCTION(scif5),
		SH_PFC_FUNCTION(scif_clk),
		SH_PFC_FUNCTION(sdhi0),
		SH_PFC_FUNCTION(sdhi1),
		SH_PFC_FUNCTION(sdhi2),
		SH_PFC_FUNCTION(sdhi3),
		SH_PFC_FUNCTION(ssi),
		SH_PFC_FUNCTION(tmu),
		SH_PFC_FUNCTION(tpu),
		SH_PFC_FUNCTION(usb0),
		SH_PFC_FUNCTION(usb1),
		SH_PFC_FUNCTION(usb2),
		SH_PFC_FUNCTION(usb2_ch3),
		SH_PFC_FUNCTION(usb30),
		SH_PFC_FUNCTION(vin4),
		SH_PFC_FUNCTION(vin5),
	},
#ifdef CONFIG_PINCTRL_PFC_R8A77951
	.automotive = {
		SH_PFC_FUNCTION(drif0),
		SH_PFC_FUNCTION(drif1),
		SH_PFC_FUNCTION(drif2),
		SH_PFC_FUNCTION(drif3),
		SH_PFC_FUNCTION(mlb_3pin),
	}
#endif /* CONFIG_PINCTRL_PFC_R8A77951 */
};

static const struct pinmux_cfg_reg pinmux_config_regs[] = {
#define F_(x, y)	FN_##y
#define FM(x)		FN_##x
	{ PINMUX_CFG_REG("GPSR0", 0xe6060100, 32, 1, GROUP(
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		GP_0_15_FN,	GPSR0_15,
		GP_0_14_FN,	GPSR0_14,
		GP_0_13_FN,	GPSR0_13,
		GP_0_12_FN,	GPSR0_12,
		GP_0_11_FN,	GPSR0_11,
		GP_0_10_FN,	GPSR0_10,
		GP_0_9_FN,	GPSR0_9,
		GP_0_8_FN,	GPSR0_8,
		GP_0_7_FN,	GPSR0_7,
		GP_0_6_FN,	GPSR0_6,
		GP_0_5_FN,	GPSR0_5,
		GP_0_4_FN,	GPSR0_4,
		GP_0_3_FN,	GPSR0_3,
		GP_0_2_FN,	GPSR0_2,
		GP_0_1_FN,	GPSR0_1,
		GP_0_0_FN,	GPSR0_0, ))
	},
	{ PINMUX_CFG_REG("GPSR1", 0xe6060104, 32, 1, GROUP(
		0, 0,
		0, 0,
		0, 0,
		GP_1_28_FN,	GPSR1_28,
		GP_1_27_FN,	GPSR1_27,
		GP_1_26_FN,	GPSR1_26,
		GP_1_25_FN,	GPSR1_25,
		GP_1_24_FN,	GPSR1_24,
		GP_1_23_FN,	GPSR1_23,
		GP_1_22_FN,	GPSR1_22,
		GP_1_21_FN,	GPSR1_21,
		GP_1_20_FN,	GPSR1_20,
		GP_1_19_FN,	GPSR1_19,
		GP_1_18_FN,	GPSR1_18,
		GP_1_17_FN,	GPSR1_17,
		GP_1_16_FN,	GPSR1_16,
		GP_1_15_FN,	GPSR1_15,
		GP_1_14_FN,	GPSR1_14,
		GP_1_13_FN,	GPSR1_13,
		GP_1_12_FN,	GPSR1_12,
		GP_1_11_FN,	GPSR1_11,
		GP_1_10_FN,	GPSR1_10,
		GP_1_9_FN,	GPSR1_9,
		GP_1_8_FN,	GPSR1_8,
		GP_1_7_FN,	GPSR1_7,
		GP_1_6_FN,	GPSR1_6,
		GP_1_5_FN,	GPSR1_5,
		GP_1_4_FN,	GPSR1_4,
		GP_1_3_FN,	GPSR1_3,
		GP_1_2_FN,	GPSR1_2,
		GP_1_1_FN,	GPSR1_1,
		GP_1_0_FN,	GPSR1_0, ))
	},
	{ PINMUX_CFG_REG("GPSR2", 0xe6060108, 32, 1, GROUP(
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		GP_2_14_FN,	GPSR2_14,
		GP_2_13_FN,	GPSR2_13,
		GP_2_12_FN,	GPSR2_12,
		GP_2_11_FN,	GPSR2_11,
		GP_2_10_FN,	GPSR2_10,
		GP_2_9_FN,	GPSR2_9,
		GP_2_8_FN,	GPSR2_8,
		GP_2_7_FN,	GPSR2_7,
		GP_2_6_FN,	GPSR2_6,
		GP_2_5_FN,	GPSR2_5,
		GP_2_4_FN,	GPSR2_4,
		GP_2_3_FN,	GPSR2_3,
		GP_2_2_FN,	GPSR2_2,
		GP_2_1_FN,	GPSR2_1,
		GP_2_0_FN,	GPSR2_0, ))
	},
	{ PINMUX_CFG_REG("GPSR3", 0xe606010c, 32, 1, GROUP(
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		GP_3_15_FN,	GPSR3_15,
		GP_3_14_FN,	GPSR3_14,
		GP_3_13_FN,	GPSR3_13,
		GP_3_12_FN,	GPSR3_12,
		GP_3_11_FN,	GPSR3_11,
		GP_3_10_FN,	GPSR3_10,
		GP_3_9_FN,	GPSR3_9,
		GP_3_8_FN,	GPSR3_8,
		GP_3_7_FN,	GPSR3_7,
		GP_3_6_FN,	GPSR3_6,
		GP_3_5_FN,	GPSR3_5,
		GP_3_4_FN,	GPSR3_4,
		GP_3_3_FN,	GPSR3_3,
		GP_3_2_FN,	GPSR3_2,
		GP_3_1_FN,	GPSR3_1,
		GP_3_0_FN,	GPSR3_0, ))
	},
	{ PINMUX_CFG_REG("GPSR4", 0xe6060110, 32, 1, GROUP(
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		GP_4_17_FN,	GPSR4_17,
		GP_4_16_FN,	GPSR4_16,
		GP_4_15_FN,	GPSR4_15,
		GP_4_14_FN,	GPSR4_14,
		GP_4_13_FN,	GPSR4_13,
		GP_4_12_FN,	GPSR4_12,
		GP_4_11_FN,	GPSR4_11,
		GP_4_10_FN,	GPSR4_10,
		GP_4_9_FN,	GPSR4_9,
		GP_4_8_FN,	GPSR4_8,
		GP_4_7_FN,	GPSR4_7,
		GP_4_6_FN,	GPSR4_6,
		GP_4_5_FN,	GPSR4_5,
		GP_4_4_FN,	GPSR4_4,
		GP_4_3_FN,	GPSR4_3,
		GP_4_2_FN,	GPSR4_2,
		GP_4_1_FN,	GPSR4_1,
		GP_4_0_FN,	GPSR4_0, ))
	},
	{ PINMUX_CFG_REG("GPSR5", 0xe6060114, 32, 1, GROUP(
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		GP_5_25_FN,	GPSR5_25,
		GP_5_24_FN,	GPSR5_24,
		GP_5_23_FN,	GPSR5_23,
		GP_5_22_FN,	GPSR5_22,
		GP_5_21_FN,	GPSR5_21,
		GP_5_20_FN,	GPSR5_20,
		GP_5_19_FN,	GPSR5_19,
		GP_5_18_FN,	GPSR5_18,
		GP_5_17_FN,	GPSR5_17,
		GP_5_16_FN,	GPSR5_16,
		GP_5_15_FN,	GPSR5_15,
		GP_5_14_FN,	GPSR5_14,
		GP_5_13_FN,	GPSR5_13,
		GP_5_12_FN,	GPSR5_12,
		GP_5_11_FN,	GPSR5_11,
		GP_5_10_FN,	GPSR5_10,
		GP_5_9_FN,	GPSR5_9,
		GP_5_8_FN,	GPSR5_8,
		GP_5_7_FN,	GPSR5_7,
		GP_5_6_FN,	GPSR5_6,
		GP_5_5_FN,	GPSR5_5,
		GP_5_4_FN,	GPSR5_4,
		GP_5_3_FN,	GPSR5_3,
		GP_5_2_FN,	GPSR5_2,
		GP_5_1_FN,	GPSR5_1,
		GP_5_0_FN,	GPSR5_0, ))
	},
	{ PINMUX_CFG_REG("GPSR6", 0xe6060118, 32, 1, GROUP(
		GP_6_31_FN,	GPSR6_31,
		GP_6_30_FN,	GPSR6_30,
		GP_6_29_FN,	GPSR6_29,
		GP_6_28_FN,	GPSR6_28,
		GP_6_27_FN,	GPSR6_27,
		GP_6_26_FN,	GPSR6_26,
		GP_6_25_FN,	GPSR6_25,
		GP_6_24_FN,	GPSR6_24,
		GP_6_23_FN,	GPSR6_23,
		GP_6_22_FN,	GPSR6_22,
		GP_6_21_FN,	GPSR6_21,
		GP_6_20_FN,	GPSR6_20,
		GP_6_19_FN,	GPSR6_19,
		GP_6_18_FN,	GPSR6_18,
		GP_6_17_FN,	GPSR6_17,
		GP_6_16_FN,	GPSR6_16,
		GP_6_15_FN,	GPSR6_15,
		GP_6_14_FN,	GPSR6_14,
		GP_6_13_FN,	GPSR6_13,
		GP_6_12_FN,	GPSR6_12,
		GP_6_11_FN,	GPSR6_11,
		GP_6_10_FN,	GPSR6_10,
		GP_6_9_FN,	GPSR6_9,
		GP_6_8_FN,	GPSR6_8,
		GP_6_7_FN,	GPSR6_7,
		GP_6_6_FN,	GPSR6_6,
		GP_6_5_FN,	GPSR6_5,
		GP_6_4_FN,	GPSR6_4,
		GP_6_3_FN,	GPSR6_3,
		GP_6_2_FN,	GPSR6_2,
		GP_6_1_FN,	GPSR6_1,
		GP_6_0_FN,	GPSR6_0, ))
	},
	{ PINMUX_CFG_REG("GPSR7", 0xe606011c, 32, 1, GROUP(
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		GP_7_3_FN, GPSR7_3,
		GP_7_2_FN, GPSR7_2,
		GP_7_1_FN, GPSR7_1,
		GP_7_0_FN, GPSR7_0, ))
	},
#undef F_
#undef FM

#define F_(x, y)	x,
#define FM(x)		FN_##x,
	{ PINMUX_CFG_REG("IPSR0", 0xe6060200, 32, 4, GROUP(
		IP0_31_28
		IP0_27_24
		IP0_23_20
		IP0_19_16
		IP0_15_12
		IP0_11_8
		IP0_7_4
		IP0_3_0 ))
	},
	{ PINMUX_CFG_REG("IPSR1", 0xe6060204, 32, 4, GROUP(
		IP1_31_28
		IP1_27_24
		IP1_23_20
		IP1_19_16
		IP1_15_12
		IP1_11_8
		IP1_7_4
		IP1_3_0 ))
	},
	{ PINMUX_CFG_REG("IPSR2", 0xe6060208, 32, 4, GROUP(
		IP2_31_28
		IP2_27_24
		IP2_23_20
		IP2_19_16
		IP2_15_12
		IP2_11_8
		IP2_7_4
		IP2_3_0 ))
	},
	{ PINMUX_CFG_REG("IPSR3", 0xe606020c, 32, 4, GROUP(
		IP3_31_28
		IP3_27_24
		IP3_23_20
		IP3_19_16
		IP3_15_12
		IP3_11_8
		IP3_7_4
		IP3_3_0 ))
	},
	{ PINMUX_CFG_REG("IPSR4", 0xe6060210, 32, 4, GROUP(
		IP4_31_28
		IP4_27_24
		IP4_23_20
		IP4_19_16
		IP4_15_12
		IP4_11_8
		IP4_7_4
		IP4_3_0 ))
	},
	{ PINMUX_CFG_REG("IPSR5", 0xe6060214, 32, 4, GROUP(
		IP5_31_28
		IP5_27_24
		IP5_23_20
		IP5_19_16
		IP5_15_12
		IP5_11_8
		IP5_7_4
		IP5_3_0 ))
	},
	{ PINMUX_CFG_REG("IPSR6", 0xe6060218, 32, 4, GROUP(
		IP6_31_28
		IP6_27_24
		IP6_23_20
		IP6_19_16
		IP6_15_12
		IP6_11_8
		IP6_7_4
		IP6_3_0 ))
	},
	{ PINMUX_CFG_REG("IPSR7", 0xe606021c, 32, 4, GROUP(
		IP7_31_28
		IP7_27_24
		IP7_23_20
		IP7_19_16
		/* IP7_15_12 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		IP7_11_8
		IP7_7_4
		IP7_3_0 ))
	},
	{ PINMUX_CFG_REG("IPSR8", 0xe6060220, 32, 4, GROUP(
		IP8_31_28
		IP8_27_24
		IP8_23_20
		IP8_19_16
		IP8_15_12
		IP8_11_8
		IP8_7_4
		IP8_3_0 ))
	},
	{ PINMUX_CFG_REG("IPSR9", 0xe6060224, 32, 4, GROUP(
		IP9_31_28
		IP9_27_24
		IP9_23_20
		IP9_19_16
		IP9_15_12
		IP9_11_8
		IP9_7_4
		IP9_3_0 ))
	},
	{ PINMUX_CFG_REG("IPSR10", 0xe6060228, 32, 4, GROUP(
		IP10_31_28
		IP10_27_24
		IP10_23_20
		IP10_19_16
		IP10_15_12
		IP10_11_8
		IP10_7_4
		IP10_3_0 ))
	},
	{ PINMUX_CFG_REG("IPSR11", 0xe606022c, 32, 4, GROUP(
		IP11_31_28
		IP11_27_24
		IP11_23_20
		IP11_19_16
		IP11_15_12
		IP11_11_8
		IP11_7_4
		IP11_3_0 ))
	},
	{ PINMUX_CFG_REG("IPSR12", 0xe6060230, 32, 4, GROUP(
		IP12_31_28
		IP12_27_24
		IP12_23_20
		IP12_19_16
		IP12_15_12
		IP12_11_8
		IP12_7_4
		IP12_3_0 ))
	},
	{ PINMUX_CFG_REG("IPSR13", 0xe6060234, 32, 4, GROUP(
		IP13_31_28
		IP13_27_24
		IP13_23_20
		IP13_19_16
		IP13_15_12
		IP13_11_8
		IP13_7_4
		IP13_3_0 ))
	},
	{ PINMUX_CFG_REG("IPSR14", 0xe6060238, 32, 4, GROUP(
		IP14_31_28
		IP14_27_24
		IP14_23_20
		IP14_19_16
		IP14_15_12
		IP14_11_8
		IP14_7_4
		IP14_3_0 ))
	},
	{ PINMUX_CFG_REG("IPSR15", 0xe606023c, 32, 4, GROUP(
		IP15_31_28
		IP15_27_24
		IP15_23_20
		IP15_19_16
		IP15_15_12
		IP15_11_8
		IP15_7_4
		IP15_3_0 ))
	},
	{ PINMUX_CFG_REG("IPSR16", 0xe6060240, 32, 4, GROUP(
		IP16_31_28
		IP16_27_24
		IP16_23_20
		IP16_19_16
		IP16_15_12
		IP16_11_8
		IP16_7_4
		IP16_3_0 ))
	},
	{ PINMUX_CFG_REG("IPSR17", 0xe6060244, 32, 4, GROUP(
		IP17_31_28
		IP17_27_24
		IP17_23_20
		IP17_19_16
		IP17_15_12
		IP17_11_8
		IP17_7_4
		IP17_3_0 ))
	},
	{ PINMUX_CFG_REG("IPSR18", 0xe6060248, 32, 4, GROUP(
		/* IP18_31_28 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP18_27_24 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP18_23_20 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP18_19_16 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP18_15_12 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP18_11_8  */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		IP18_7_4
		IP18_3_0 ))
	},
#undef F_
#undef FM

#define F_(x, y)	x,
#define FM(x)		FN_##x,
	{ PINMUX_CFG_REG_VAR("MOD_SEL0", 0xe6060500, 32,
			     GROUP(3, 2, 3, 1, 1, 1, 1, 1, 2, 1, 1, 2,
				   1, 1, 1, 2, 2, 1, 2, 3),
			     GROUP(
		MOD_SEL0_31_30_29
		MOD_SEL0_28_27
		MOD_SEL0_26_25_24
		MOD_SEL0_23
		MOD_SEL0_22
		MOD_SEL0_21
		MOD_SEL0_20
		MOD_SEL0_19
		MOD_SEL0_18_17
		MOD_SEL0_16
		0, 0, /* RESERVED 15 */
		MOD_SEL0_14_13
		MOD_SEL0_12
		MOD_SEL0_11
		MOD_SEL0_10
		MOD_SEL0_9_8
		MOD_SEL0_7_6
		MOD_SEL0_5
		MOD_SEL0_4_3
		/* RESERVED 2, 1, 0 */
		0, 0, 0, 0, 0, 0, 0, 0 ))
	},
	{ PINMUX_CFG_REG_VAR("MOD_SEL1", 0xe6060504, 32,
			     GROUP(2, 3, 1, 2, 3, 1, 1, 2, 1, 2, 1, 1,
				   1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1),
			     GROUP(
		MOD_SEL1_31_30
		MOD_SEL1_29_28_27
		MOD_SEL1_26
		MOD_SEL1_25_24
		MOD_SEL1_23_22_21
		MOD_SEL1_20
		MOD_SEL1_19
		MOD_SEL1_18_17
		MOD_SEL1_16
		MOD_SEL1_15_14
		MOD_SEL1_13
		MOD_SEL1_12
		MOD_SEL1_11
		MOD_SEL1_10
		MOD_SEL1_9
		0, 0, 0, 0, /* RESERVED 8, 7 */
		MOD_SEL1_6
		MOD_SEL1_5
		MOD_SEL1_4
		MOD_SEL1_3
		MOD_SEL1_2
		MOD_SEL1_1
		MOD_SEL1_0 ))
	},
	{ PINMUX_CFG_REG_VAR("MOD_SEL2", 0xe6060508, 32,
			     GROUP(1, 1, 1, 2, 1, 3, 1, 1, 1, 1, 1, 1,
				   1, 4, 4, 4, 3, 1),
			     GROUP(
		MOD_SEL2_31
		MOD_SEL2_30
		MOD_SEL2_29
		MOD_SEL2_28_27
		MOD_SEL2_26
		MOD_SEL2_25_24_23
		/* RESERVED 22 */
		0, 0,
		MOD_SEL2_21
		MOD_SEL2_20
		MOD_SEL2_19
		MOD_SEL2_18
		MOD_SEL2_17
		/* RESERVED 16 */
		0, 0,
		/* RESERVED 15, 14, 13, 12 */
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		/* RESERVED 11, 10, 9, 8 */
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		/* RESERVED 7, 6, 5, 4 */
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		/* RESERVED 3, 2, 1 */
		0, 0, 0, 0, 0, 0, 0, 0,
		MOD_SEL2_0 ))
	},
	{ },
};

static const struct pinmux_drive_reg pinmux_drive_regs[] = {
	{ PINMUX_DRIVE_REG("DRVCTRL0", 0xe6060300) {
		{ PIN_QSPI0_SPCLK,    28, 2 },	/* QSPI0_SPCLK */
		{ PIN_QSPI0_MOSI_IO0, 24, 2 },	/* QSPI0_MOSI_IO0 */
		{ PIN_QSPI0_MISO_IO1, 20, 2 },	/* QSPI0_MISO_IO1 */
		{ PIN_QSPI0_IO2,      16, 2 },	/* QSPI0_IO2 */
		{ PIN_QSPI0_IO3,      12, 2 },	/* QSPI0_IO3 */
		{ PIN_QSPI0_SSL,       8, 2 },	/* QSPI0_SSL */
		{ PIN_QSPI1_SPCLK,     4, 2 },	/* QSPI1_SPCLK */
		{ PIN_QSPI1_MOSI_IO0,  0, 2 },	/* QSPI1_MOSI_IO0 */
	} },
	{ PINMUX_DRIVE_REG("DRVCTRL1", 0xe6060304) {
		{ PIN_QSPI1_MISO_IO1, 28, 2 },	/* QSPI1_MISO_IO1 */
		{ PIN_QSPI1_IO2,      24, 2 },	/* QSPI1_IO2 */
		{ PIN_QSPI1_IO3,      20, 2 },	/* QSPI1_IO3 */
		{ PIN_QSPI1_SSL,      16, 2 },	/* QSPI1_SSL */
		{ PIN_RPC_INT_N,      12, 2 },	/* RPC_INT# */
		{ PIN_RPC_WP_N,        8, 2 },	/* RPC_WP# */
		{ PIN_RPC_RESET_N,     4, 2 },	/* RPC_RESET# */
		{ PIN_AVB_RX_CTL,      0, 3 },	/* AVB_RX_CTL */
	} },
	{ PINMUX_DRIVE_REG("DRVCTRL2", 0xe6060308) {
		{ PIN_AVB_RXC,        28, 3 },	/* AVB_RXC */
		{ PIN_AVB_RD0,        24, 3 },	/* AVB_RD0 */
		{ PIN_AVB_RD1,        20, 3 },	/* AVB_RD1 */
		{ PIN_AVB_RD2,        16, 3 },	/* AVB_RD2 */
		{ PIN_AVB_RD3,        12, 3 },	/* AVB_RD3 */
		{ PIN_AVB_TX_CTL,      8, 3 },	/* AVB_TX_CTL */
		{ PIN_AVB_TXC,         4, 3 },	/* AVB_TXC */
		{ PIN_AVB_TD0,         0, 3 },	/* AVB_TD0 */
	} },
	{ PINMUX_DRIVE_REG("DRVCTRL3", 0xe606030c) {
		{ PIN_AVB_TD1,        28, 3 },	/* AVB_TD1 */
		{ PIN_AVB_TD2,        24, 3 },	/* AVB_TD2 */
		{ PIN_AVB_TD3,        20, 3 },	/* AVB_TD3 */
		{ PIN_AVB_TXCREFCLK,  16, 3 },	/* AVB_TXCREFCLK */
		{ PIN_AVB_MDIO,       12, 3 },	/* AVB_MDIO */
		{ RCAR_GP_PIN(2,  9),  8, 3 },	/* AVB_MDC */
		{ RCAR_GP_PIN(2, 10),  4, 3 },	/* AVB_MAGIC */
		{ RCAR_GP_PIN(2, 11),  0, 3 },	/* AVB_PHY_INT */
	} },
	{ PINMUX_DRIVE_REG("DRVCTRL4", 0xe6060310) {
		{ RCAR_GP_PIN(2, 12), 28, 3 },	/* AVB_LINK */
		{ RCAR_GP_PIN(2, 13), 24, 3 },	/* AVB_AVTP_MATCH */
		{ RCAR_GP_PIN(2, 14), 20, 3 },	/* AVB_AVTP_CAPTURE */
		{ RCAR_GP_PIN(2,  0), 16, 3 },	/* IRQ0 */
		{ RCAR_GP_PIN(2,  1), 12, 3 },	/* IRQ1 */
		{ RCAR_GP_PIN(2,  2),  8, 3 },	/* IRQ2 */
		{ RCAR_GP_PIN(2,  3),  4, 3 },	/* IRQ3 */
		{ RCAR_GP_PIN(2,  4),  0, 3 },	/* IRQ4 */
	} },
	{ PINMUX_DRIVE_REG("DRVCTRL5", 0xe6060314) {
		{ RCAR_GP_PIN(2,  5), 28, 3 },	/* IRQ5 */
		{ RCAR_GP_PIN(2,  6), 24, 3 },	/* PWM0 */
		{ RCAR_GP_PIN(2,  7), 20, 3 },	/* PWM1 */
		{ RCAR_GP_PIN(2,  8), 16, 3 },	/* PWM2 */
		{ RCAR_GP_PIN(1,  0), 12, 3 },	/* A0 */
		{ RCAR_GP_PIN(1,  1),  8, 3 },	/* A1 */
		{ RCAR_GP_PIN(1,  2),  4, 3 },	/* A2 */
		{ RCAR_GP_PIN(1,  3),  0, 3 },	/* A3 */
	} },
	{ PINMUX_DRIVE_REG("DRVCTRL6", 0xe6060318) {
		{ RCAR_GP_PIN(1,  4), 28, 3 },	/* A4 */
		{ RCAR_GP_PIN(1,  5), 24, 3 },	/* A5 */
		{ RCAR_GP_PIN(1,  6), 20, 3 },	/* A6 */
		{ RCAR_GP_PIN(1,  7), 16, 3 },	/* A7 */
		{ RCAR_GP_PIN(1,  8), 12, 3 },	/* A8 */
		{ RCAR_GP_PIN(1,  9),  8, 3 },	/* A9 */
		{ RCAR_GP_PIN(1, 10),  4, 3 },	/* A10 */
		{ RCAR_GP_PIN(1, 11),  0, 3 },	/* A11 */
	} },
	{ PINMUX_DRIVE_REG("DRVCTRL7", 0xe606031c) {
		{ RCAR_GP_PIN(1, 12), 28, 3 },	/* A12 */
		{ RCAR_GP_PIN(1, 13), 24, 3 },	/* A13 */
		{ RCAR_GP_PIN(1, 14), 20, 3 },	/* A14 */
		{ RCAR_GP_PIN(1, 15), 16, 3 },	/* A15 */
		{ RCAR_GP_PIN(1, 16), 12, 3 },	/* A16 */
		{ RCAR_GP_PIN(1, 17),  8, 3 },	/* A17 */
		{ RCAR_GP_PIN(1, 18),  4, 3 },	/* A18 */
		{ RCAR_GP_PIN(1, 19),  0, 3 },	/* A19 */
	} },
	{ PINMUX_DRIVE_REG("DRVCTRL8", 0xe6060320) {
		{ RCAR_GP_PIN(1, 28), 28, 3 },	/* CLKOUT */
		{ RCAR_GP_PIN(1, 20), 24, 3 },	/* CS0 */
		{ RCAR_GP_PIN(1, 21), 20, 3 },	/* CS1_A26 */
		{ RCAR_GP_PIN(1, 22), 16, 3 },	/* BS */
		{ RCAR_GP_PIN(1, 23), 12, 3 },	/* RD */
		{ RCAR_GP_PIN(1, 24),  8, 3 },	/* RD_WR */
		{ RCAR_GP_PIN(1, 25),  4, 3 },	/* WE0 */
		{ RCAR_GP_PIN(1, 26),  0, 3 },	/* WE1 */
	} },
	{ PINMUX_DRIVE_REG("DRVCTRL9", 0xe6060324) {
		{ RCAR_GP_PIN(1, 27), 28, 3 },	/* EX_WAIT0 */
		{ PIN_PRESETOUT_N,    24, 3 },	/* PRESETOUT# */
		{ RCAR_GP_PIN(0,  0), 20, 3 },	/* D0 */
		{ RCAR_GP_PIN(0,  1), 16, 3 },	/* D1 */
		{ RCAR_GP_PIN(0,  2), 12, 3 },	/* D2 */
		{ RCAR_GP_PIN(0,  3),  8, 3 },	/* D3 */
		{ RCAR_GP_PIN(0,  4),  4, 3 },	/* D4 */
		{ RCAR_GP_PIN(0,  5),  0, 3 },	/* D5 */
	} },
	{ PINMUX_DRIVE_REG("DRVCTRL10", 0xe6060328) {
		{ RCAR_GP_PIN(0,  6), 28, 3 },	/* D6 */
		{ RCAR_GP_PIN(0,  7), 24, 3 },	/* D7 */
		{ RCAR_GP_PIN(0,  8), 20, 3 },	/* D8 */
		{ RCAR_GP_PIN(0,  9), 16, 3 },	/* D9 */
		{ RCAR_GP_PIN(0, 10), 12, 3 },	/* D10 */
		{ RCAR_GP_PIN(0, 11),  8, 3 },	/* D11 */
		{ RCAR_GP_PIN(0, 12),  4, 3 },	/* D12 */
		{ RCAR_GP_PIN(0, 13),  0, 3 },	/* D13 */
	} },
	{ PINMUX_DRIVE_REG("DRVCTRL11", 0xe606032c) {
		{ RCAR_GP_PIN(0, 14), 28, 3 },	/* D14 */
		{ RCAR_GP_PIN(0, 15), 24, 3 },	/* D15 */
		{ RCAR_GP_PIN(7,  0), 20, 3 },	/* AVS1 */
		{ RCAR_GP_PIN(7,  1), 16, 3 },	/* AVS2 */
		{ RCAR_GP_PIN(7,  2), 12, 3 },	/* GP7_02 */
		{ RCAR_GP_PIN(7,  3),  8, 3 },	/* GP7_03 */
		{ PIN_DU_DOTCLKIN0,    4, 2 },	/* DU_DOTCLKIN0 */
		{ PIN_DU_DOTCLKIN1,    0, 2 },	/* DU_DOTCLKIN1 */
	} },
	{ PINMUX_DRIVE_REG("DRVCTRL12", 0xe6060330) {
#ifdef CONFIG_PINCTRL_PFC_R8A77951
		{ PIN_DU_DOTCLKIN2,   28, 2 },	/* DU_DOTCLKIN2 */
#endif
		{ PIN_DU_DOTCLKIN3,   24, 2 },	/* DU_DOTCLKIN3 */
		{ PIN_FSCLKST_N,      20, 2 },	/* FSCLKST# */
		{ PIN_TMS,             4, 2 },	/* TMS */
	} },
	{ PINMUX_DRIVE_REG("DRVCTRL13", 0xe6060334) {
		{ PIN_TDO,            28, 2 },	/* TDO */
		{ PIN_ASEBRK,         24, 2 },	/* ASEBRK */
		{ RCAR_GP_PIN(3,  0), 20, 3 },	/* SD0_CLK */
		{ RCAR_GP_PIN(3,  1), 16, 3 },	/* SD0_CMD */
		{ RCAR_GP_PIN(3,  2), 12, 3 },	/* SD0_DAT0 */
		{ RCAR_GP_PIN(3,  3),  8, 3 },	/* SD0_DAT1 */
		{ RCAR_GP_PIN(3,  4),  4, 3 },	/* SD0_DAT2 */
		{ RCAR_GP_PIN(3,  5),  0, 3 },	/* SD0_DAT3 */
	} },
	{ PINMUX_DRIVE_REG("DRVCTRL14", 0xe6060338) {
		{ RCAR_GP_PIN(3,  6), 28, 3 },	/* SD1_CLK */
		{ RCAR_GP_PIN(3,  7), 24, 3 },	/* SD1_CMD */
		{ RCAR_GP_PIN(3,  8), 20, 3 },	/* SD1_DAT0 */
		{ RCAR_GP_PIN(3,  9), 16, 3 },	/* SD1_DAT1 */
		{ RCAR_GP_PIN(3, 10), 12, 3 },	/* SD1_DAT2 */
		{ RCAR_GP_PIN(3, 11),  8, 3 },	/* SD1_DAT3 */
		{ RCAR_GP_PIN(4,  0),  4, 3 },	/* SD2_CLK */
		{ RCAR_GP_PIN(4,  1),  0, 3 },	/* SD2_CMD */
	} },
	{ PINMUX_DRIVE_REG("DRVCTRL15", 0xe606033c) {
		{ RCAR_GP_PIN(4,  2), 28, 3 },	/* SD2_DAT0 */
		{ RCAR_GP_PIN(4,  3), 24, 3 },	/* SD2_DAT1 */
		{ RCAR_GP_PIN(4,  4), 20, 3 },	/* SD2_DAT2 */
		{ RCAR_GP_PIN(4,  5), 16, 3 },	/* SD2_DAT3 */
		{ RCAR_GP_PIN(4,  6), 12, 3 },	/* SD2_DS */
		{ RCAR_GP_PIN(4,  7),  8, 3 },	/* SD3_CLK */
		{ RCAR_GP_PIN(4,  8),  4, 3 },	/* SD3_CMD */
		{ RCAR_GP_PIN(4,  9),  0, 3 },	/* SD3_DAT0 */
	} },
	{ PINMUX_DRIVE_REG("DRVCTRL16", 0xe6060340) {
		{ RCAR_GP_PIN(4, 10), 28, 3 },	/* SD3_DAT1 */
		{ RCAR_GP_PIN(4, 11), 24, 3 },	/* SD3_DAT2 */
		{ RCAR_GP_PIN(4, 12), 20, 3 },	/* SD3_DAT3 */
		{ RCAR_GP_PIN(4, 13), 16, 3 },	/* SD3_DAT4 */
		{ RCAR_GP_PIN(4, 14), 12, 3 },	/* SD3_DAT5 */
		{ RCAR_GP_PIN(4, 15),  8, 3 },	/* SD3_DAT6 */
		{ RCAR_GP_PIN(4, 16),  4, 3 },	/* SD3_DAT7 */
		{ RCAR_GP_PIN(4, 17),  0, 3 },	/* SD3_DS */
	} },
	{ PINMUX_DRIVE_REG("DRVCTRL17", 0xe6060344) {
		{ RCAR_GP_PIN(3, 12), 28, 3 },	/* SD0_CD */
		{ RCAR_GP_PIN(3, 13), 24, 3 },	/* SD0_WP */
		{ RCAR_GP_PIN(3, 14), 20, 3 },	/* SD1_CD */
		{ RCAR_GP_PIN(3, 15), 16, 3 },	/* SD1_WP */
		{ RCAR_GP_PIN(5,  0), 12, 3 },	/* SCK0 */
		{ RCAR_GP_PIN(5,  1),  8, 3 },	/* RX0 */
		{ RCAR_GP_PIN(5,  2),  4, 3 },	/* TX0 */
		{ RCAR_GP_PIN(5,  3),  0, 3 },	/* CTS0 */
	} },
	{ PINMUX_DRIVE_REG("DRVCTRL18", 0xe6060348) {
		{ RCAR_GP_PIN(5,  4), 28, 3 },	/* RTS0 */
		{ RCAR_GP_PIN(5,  5), 24, 3 },	/* RX1 */
		{ RCAR_GP_PIN(5,  6), 20, 3 },	/* TX1 */
		{ RCAR_GP_PIN(5,  7), 16, 3 },	/* CTS1 */
		{ RCAR_GP_PIN(5,  8), 12, 3 },	/* RTS1 */
		{ RCAR_GP_PIN(5,  9),  8, 3 },	/* SCK2 */
		{ RCAR_GP_PIN(5, 10),  4, 3 },	/* TX2 */
		{ RCAR_GP_PIN(5, 11),  0, 3 },	/* RX2 */
	} },
	{ PINMUX_DRIVE_REG("DRVCTRL19", 0xe606034c) {
		{ RCAR_GP_PIN(5, 12), 28, 3 },	/* HSCK0 */
		{ RCAR_GP_PIN(5, 13), 24, 3 },	/* HRX0 */
		{ RCAR_GP_PIN(5, 14), 20, 3 },	/* HTX0 */
		{ RCAR_GP_PIN(5, 15), 16, 3 },	/* HCTS0 */
		{ RCAR_GP_PIN(5, 16), 12, 3 },	/* HRTS0 */
		{ RCAR_GP_PIN(5, 17),  8, 3 },	/* MSIOF0_SCK */
		{ RCAR_GP_PIN(5, 18),  4, 3 },	/* MSIOF0_SYNC */
		{ RCAR_GP_PIN(5, 19),  0, 3 },	/* MSIOF0_SS1 */
	} },
	{ PINMUX_DRIVE_REG("DRVCTRL20", 0xe6060350) {
		{ RCAR_GP_PIN(5, 20), 28, 3 },	/* MSIOF0_TXD */
		{ RCAR_GP_PIN(5, 21), 24, 3 },	/* MSIOF0_SS2 */
		{ RCAR_GP_PIN(5, 22), 20, 3 },	/* MSIOF0_RXD */
		{ RCAR_GP_PIN(5, 23), 16, 3 },	/* MLB_CLK */
		{ RCAR_GP_PIN(5, 24), 12, 3 },	/* MLB_SIG */
		{ RCAR_GP_PIN(5, 25),  8, 3 },	/* MLB_DAT */
		{ PIN_MLB_REF,         4, 3 },	/* MLB_REF */
		{ RCAR_GP_PIN(6,  0),  0, 3 },	/* SSI_SCK01239 */
	} },
	{ PINMUX_DRIVE_REG("DRVCTRL21", 0xe6060354) {
		{ RCAR_GP_PIN(6,  1), 28, 3 },	/* SSI_WS01239 */
		{ RCAR_GP_PIN(6,  2), 24, 3 },	/* SSI_SDATA0 */
		{ RCAR_GP_PIN(6,  3), 20, 3 },	/* SSI_SDATA1 */
		{ RCAR_GP_PIN(6,  4), 16, 3 },	/* SSI_SDATA2 */
		{ RCAR_GP_PIN(6,  5), 12, 3 },	/* SSI_SCK349 */
		{ RCAR_GP_PIN(6,  6),  8, 3 },	/* SSI_WS349 */
		{ RCAR_GP_PIN(6,  7),  4, 3 },	/* SSI_SDATA3 */
		{ RCAR_GP_PIN(6,  8),  0, 3 },	/* SSI_SCK4 */
	} },
	{ PINMUX_DRIVE_REG("DRVCTRL22", 0xe6060358) {
		{ RCAR_GP_PIN(6,  9), 28, 3 },	/* SSI_WS4 */
		{ RCAR_GP_PIN(6, 10), 24, 3 },	/* SSI_SDATA4 */
		{ RCAR_GP_PIN(6, 11), 20, 3 },	/* SSI_SCK5 */
		{ RCAR_GP_PIN(6, 12), 16, 3 },	/* SSI_WS5 */
		{ RCAR_GP_PIN(6, 13), 12, 3 },	/* SSI_SDATA5 */
		{ RCAR_GP_PIN(6, 14),  8, 3 },	/* SSI_SCK6 */
		{ RCAR_GP_PIN(6, 15),  4, 3 },	/* SSI_WS6 */
		{ RCAR_GP_PIN(6, 16),  0, 3 },	/* SSI_SDATA6 */
	} },
	{ PINMUX_DRIVE_REG("DRVCTRL23", 0xe606035c) {
		{ RCAR_GP_PIN(6, 17), 28, 3 },	/* SSI_SCK78 */
		{ RCAR_GP_PIN(6, 18), 24, 3 },	/* SSI_WS78 */
		{ RCAR_GP_PIN(6, 19), 20, 3 },	/* SSI_SDATA7 */
		{ RCAR_GP_PIN(6, 20), 16, 3 },	/* SSI_SDATA8 */
		{ RCAR_GP_PIN(6, 21), 12, 3 },	/* SSI_SDATA9 */
		{ RCAR_GP_PIN(6, 22),  8, 3 },	/* AUDIO_CLKA */
		{ RCAR_GP_PIN(6, 23),  4, 3 },	/* AUDIO_CLKB */
		{ RCAR_GP_PIN(6, 24),  0, 3 },	/* USB0_PWEN */
	} },
	{ PINMUX_DRIVE_REG("DRVCTRL24", 0xe6060360) {
		{ RCAR_GP_PIN(6, 25), 28, 3 },	/* USB0_OVC */
		{ RCAR_GP_PIN(6, 26), 24, 3 },	/* USB1_PWEN */
		{ RCAR_GP_PIN(6, 27), 20, 3 },	/* USB1_OVC */
		{ RCAR_GP_PIN(6, 28), 16, 3 },	/* USB30_PWEN */
		{ RCAR_GP_PIN(6, 29), 12, 3 },	/* USB30_OVC */
		{ RCAR_GP_PIN(6, 30),  8, 3 },	/* GP6_30/USB2_CH3_PWEN */
		{ RCAR_GP_PIN(6, 31),  4, 3 },	/* GP6_31/USB2_CH3_OVC */
	} },
	{ },
};

enum ioctrl_regs {
	POCCTRL,
	TDSELCTRL,
};

static const struct pinmux_ioctrl_reg pinmux_ioctrl_regs[] = {
	[POCCTRL] = { 0xe6060380, },
	[TDSELCTRL] = { 0xe60603c0, },
	{ /* sentinel */ },
};

static int r8a77951_pin_to_pocctrl(struct sh_pfc *pfc,
				   unsigned int pin, u32 *pocctrl)
{
	int bit = -EINVAL;

	*pocctrl = pinmux_ioctrl_regs[POCCTRL].reg;

	if (pin >= RCAR_GP_PIN(3, 0) && pin <= RCAR_GP_PIN(3, 11))
		bit = pin & 0x1f;

	if (pin >= RCAR_GP_PIN(4, 0) && pin <= RCAR_GP_PIN(4, 17))
		bit = (pin & 0x1f) + 12;

	return bit;
}

static const struct pinmux_bias_reg pinmux_bias_regs[] = {
	{ PINMUX_BIAS_REG("PUEN0", 0xe6060400, "PUD0", 0xe6060440) {
		[ 0] = PIN_QSPI0_SPCLK,		/* QSPI0_SPCLK */
		[ 1] = PIN_QSPI0_MOSI_IO0,	/* QSPI0_MOSI_IO0 */
		[ 2] = PIN_QSPI0_MISO_IO1,	/* QSPI0_MISO_IO1 */
		[ 3] = PIN_QSPI0_IO2,		/* QSPI0_IO2 */
		[ 4] = PIN_QSPI0_IO3,		/* QSPI0_IO3 */
		[ 5] = PIN_QSPI0_SSL,		/* QSPI0_SSL */
		[ 6] = PIN_QSPI1_SPCLK,		/* QSPI1_SPCLK */
		[ 7] = PIN_QSPI1_MOSI_IO0,	/* QSPI1_MOSI_IO0 */
		[ 8] = PIN_QSPI1_MISO_IO1,	/* QSPI1_MISO_IO1 */
		[ 9] = PIN_QSPI1_IO2,		/* QSPI1_IO2 */
		[10] = PIN_QSPI1_IO3,		/* QSPI1_IO3 */
		[11] = PIN_QSPI1_SSL,		/* QSPI1_SSL */
		[12] = PIN_RPC_INT_N,		/* RPC_INT# */
		[13] = PIN_RPC_WP_N,		/* RPC_WP# */
		[14] = PIN_RPC_RESET_N,		/* RPC_RESET# */
		[15] = PIN_AVB_RX_CTL,		/* AVB_RX_CTL */
		[16] = PIN_AVB_RXC,		/* AVB_RXC */
		[17] = PIN_AVB_RD0,		/* AVB_RD0 */
		[18] = PIN_AVB_RD1,		/* AVB_RD1 */
		[19] = PIN_AVB_RD2,		/* AVB_RD2 */
		[20] = PIN_AVB_RD3,		/* AVB_RD3 */
		[21] = PIN_AVB_TX_CTL,		/* AVB_TX_CTL */
		[22] = PIN_AVB_TXC,		/* AVB_TXC */
		[23] = PIN_AVB_TD0,		/* AVB_TD0 */
		[24] = PIN_AVB_TD1,		/* AVB_TD1 */
		[25] = PIN_AVB_TD2,		/* AVB_TD2 */
		[26] = PIN_AVB_TD3,		/* AVB_TD3 */
		[27] = PIN_AVB_TXCREFCLK,	/* AVB_TXCREFCLK */
		[28] = PIN_AVB_MDIO,		/* AVB_MDIO */
		[29] = RCAR_GP_PIN(2,  9),	/* AVB_MDC */
		[30] = RCAR_GP_PIN(2, 10),	/* AVB_MAGIC */
		[31] = RCAR_GP_PIN(2, 11),	/* AVB_PHY_INT */
	} },
	{ PINMUX_BIAS_REG("PUEN1", 0xe6060404, "PUD1", 0xe6060444) {
		[ 0] = RCAR_GP_PIN(2, 12),	/* AVB_LINK */
		[ 1] = RCAR_GP_PIN(2, 13),	/* AVB_AVTP_MATCH_A */
		[ 2] = RCAR_GP_PIN(2, 14),	/* AVB_AVTP_CAPTURE_A */
		[ 3] = RCAR_GP_PIN(2,  0),	/* IRQ0 */
		[ 4] = RCAR_GP_PIN(2,  1),	/* IRQ1 */
		[ 5] = RCAR_GP_PIN(2,  2),	/* IRQ2 */
		[ 6] = RCAR_GP_PIN(2,  3),	/* IRQ3 */
		[ 7] = RCAR_GP_PIN(2,  4),	/* IRQ4 */
		[ 8] = RCAR_GP_PIN(2,  5),	/* IRQ5 */
		[ 9] = RCAR_GP_PIN(2,  6),	/* PWM0 */
		[10] = RCAR_GP_PIN(2,  7),	/* PWM1_A */
		[11] = RCAR_GP_PIN(2,  8),	/* PWM2_A */
		[12] = RCAR_GP_PIN(1,  0),	/* A0 */
		[13] = RCAR_GP_PIN(1,  1),	/* A1 */
		[14] = RCAR_GP_PIN(1,  2),	/* A2 */
		[15] = RCAR_GP_PIN(1,  3),	/* A3 */
		[16] = RCAR_GP_PIN(1,  4),	/* A4 */
		[17] = RCAR_GP_PIN(1,  5),	/* A5 */
		[18] = RCAR_GP_PIN(1,  6),	/* A6 */
		[19] = RCAR_GP_PIN(1,  7),	/* A7 */
		[20] = RCAR_GP_PIN(1,  8),	/* A8 */
		[21] = RCAR_GP_PIN(1,  9),	/* A9 */
		[22] = RCAR_GP_PIN(1, 10),	/* A10 */
		[23] = RCAR_GP_PIN(1, 11),	/* A11 */
		[24] = RCAR_GP_PIN(1, 12),	/* A12 */
		[25] = RCAR_GP_PIN(1, 13),	/* A13 */
		[26] = RCAR_GP_PIN(1, 14),	/* A14 */
		[27] = RCAR_GP_PIN(1, 15),	/* A15 */
		[28] = RCAR_GP_PIN(1, 16),	/* A16 */
		[29] = RCAR_GP_PIN(1, 17),	/* A17 */
		[30] = RCAR_GP_PIN(1, 18),	/* A18 */
		[31] = RCAR_GP_PIN(1, 19),	/* A19 */
	} },
	{ PINMUX_BIAS_REG("PUEN2", 0xe6060408, "PUD2", 0xe6060448) {
		[ 0] = RCAR_GP_PIN(1, 28),	/* CLKOUT */
		[ 1] = RCAR_GP_PIN(1, 20),	/* CS0_N */
		[ 2] = RCAR_GP_PIN(1, 21),	/* CS1_N */
		[ 3] = RCAR_GP_PIN(1, 22),	/* BS_N */
		[ 4] = RCAR_GP_PIN(1, 23),	/* RD_N */
		[ 5] = RCAR_GP_PIN(1, 24),	/* RD_WR_N */
		[ 6] = RCAR_GP_PIN(1, 25),	/* WE0_N */
		[ 7] = RCAR_GP_PIN(1, 26),	/* WE1_N */
		[ 8] = RCAR_GP_PIN(1, 27),	/* EX_WAIT0_A */
		[ 9] = PIN_PRESETOUT_N,		/* PRESETOUT# */
		[10] = RCAR_GP_PIN(0,  0),	/* D0 */
		[11] = RCAR_GP_PIN(0,  1),	/* D1 */
		[12] = RCAR_GP_PIN(0,  2),	/* D2 */
		[13] = RCAR_GP_PIN(0,  3),	/* D3 */
		[14] = RCAR_GP_PIN(0,  4),	/* D4 */
		[15] = RCAR_GP_PIN(0,  5),	/* D5 */
		[16] = RCAR_GP_PIN(0,  6),	/* D6 */
		[17] = RCAR_GP_PIN(0,  7),	/* D7 */
		[18] = RCAR_GP_PIN(0,  8),	/* D8 */
		[19] = RCAR_GP_PIN(0,  9),	/* D9 */
		[20] = RCAR_GP_PIN(0, 10),	/* D10 */
		[21] = RCAR_GP_PIN(0, 11),	/* D11 */
		[22] = RCAR_GP_PIN(0, 12),	/* D12 */
		[23] = RCAR_GP_PIN(0, 13),	/* D13 */
		[24] = RCAR_GP_PIN(0, 14),	/* D14 */
		[25] = RCAR_GP_PIN(0, 15),	/* D15 */
		[26] = RCAR_GP_PIN(7,  0),	/* AVS1 */
		[27] = RCAR_GP_PIN(7,  1),	/* AVS2 */
		[28] = RCAR_GP_PIN(7,  2),	/* GP7_02 */
		[29] = RCAR_GP_PIN(7,  3),	/* GP7_03 */
		[30] = PIN_DU_DOTCLKIN0,	/* DU_DOTCLKIN0 */
		[31] = PIN_DU_DOTCLKIN1,	/* DU_DOTCLKIN1 */
	} },
	{ PINMUX_BIAS_REG("PUEN3", 0xe606040c, "PUD3", 0xe606044c) {
		[ 0] = PIN_DU_DOTCLKIN2,	/* DU_DOTCLKIN2 */
		[ 1] = PIN_DU_DOTCLKIN3,	/* DU_DOTCLKIN3 */
		[ 2] = PIN_FSCLKST_N,		/* FSCLKST# */
		[ 3] = PIN_EXTALR,		/* EXTALR*/
		[ 4] = PIN_TRST_N,		/* TRST# */
		[ 5] = PIN_TCK,			/* TCK */
		[ 6] = PIN_TMS,			/* TMS */
		[ 7] = PIN_TDI,			/* TDI */
		[ 8] = SH_PFC_PIN_NONE,
		[ 9] = PIN_ASEBRK,		/* ASEBRK */
		[10] = RCAR_GP_PIN(3,  0),	/* SD0_CLK */
		[11] = RCAR_GP_PIN(3,  1),	/* SD0_CMD */
		[12] = RCAR_GP_PIN(3,  2),	/* SD0_DAT0 */
		[13] = RCAR_GP_PIN(3,  3),	/* SD0_DAT1 */
		[14] = RCAR_GP_PIN(3,  4),	/* SD0_DAT2 */
		[15] = RCAR_GP_PIN(3,  5),	/* SD0_DAT3 */
		[16] = RCAR_GP_PIN(3,  6),	/* SD1_CLK */
		[17] = RCAR_GP_PIN(3,  7),	/* SD1_CMD */
		[18] = RCAR_GP_PIN(3,  8),	/* SD1_DAT0 */
		[19] = RCAR_GP_PIN(3,  9),	/* SD1_DAT1 */
		[20] = RCAR_GP_PIN(3, 10),	/* SD1_DAT2 */
		[21] = RCAR_GP_PIN(3, 11),	/* SD1_DAT3 */
		[22] = RCAR_GP_PIN(4,  0),	/* SD2_CLK */
		[23] = RCAR_GP_PIN(4,  1),	/* SD2_CMD */
		[24] = RCAR_GP_PIN(4,  2),	/* SD2_DAT0 */
		[25] = RCAR_GP_PIN(4,  3),	/* SD2_DAT1 */
		[26] = RCAR_GP_PIN(4,  4),	/* SD2_DAT2 */
		[27] = RCAR_GP_PIN(4,  5),	/* SD2_DAT3 */
		[28] = RCAR_GP_PIN(4,  6),	/* SD2_DS */
		[29] = RCAR_GP_PIN(4,  7),	/* SD3_CLK */
		[30] = RCAR_GP_PIN(4,  8),	/* SD3_CMD */
		[31] = RCAR_GP_PIN(4,  9),	/* SD3_DAT0 */
	} },
	{ PINMUX_BIAS_REG("PUEN4", 0xe6060410, "PUD4", 0xe6060450) {
		[ 0] = RCAR_GP_PIN(4, 10),	/* SD3_DAT1 */
		[ 1] = RCAR_GP_PIN(4, 11),	/* SD3_DAT2 */
		[ 2] = RCAR_GP_PIN(4, 12),	/* SD3_DAT3 */
		[ 3] = RCAR_GP_PIN(4, 13),	/* SD3_DAT4 */
		[ 4] = RCAR_GP_PIN(4, 14),	/* SD3_DAT5 */
		[ 5] = RCAR_GP_PIN(4, 15),	/* SD3_DAT6 */
		[ 6] = RCAR_GP_PIN(4, 16),	/* SD3_DAT7 */
		[ 7] = RCAR_GP_PIN(4, 17),	/* SD3_DS */
		[ 8] = RCAR_GP_PIN(3, 12),	/* SD0_CD */
		[ 9] = RCAR_GP_PIN(3, 13),	/* SD0_WP */
		[10] = RCAR_GP_PIN(3, 14),	/* SD1_CD */
		[11] = RCAR_GP_PIN(3, 15),	/* SD1_WP */
		[12] = RCAR_GP_PIN(5,  0),	/* SCK0 */
		[13] = RCAR_GP_PIN(5,  1),	/* RX0 */
		[14] = RCAR_GP_PIN(5,  2),	/* TX0 */
		[15] = RCAR_GP_PIN(5,  3),	/* CTS0_N */
		[16] = RCAR_GP_PIN(5,  4),	/* RTS0_N */
		[17] = RCAR_GP_PIN(5,  5),	/* RX1_A */
		[18] = RCAR_GP_PIN(5,  6),	/* TX1_A */
		[19] = RCAR_GP_PIN(5,  7),	/* CTS1_N */
		[20] = RCAR_GP_PIN(5,  8),	/* RTS1_N */
		[21] = RCAR_GP_PIN(5,  9),	/* SCK2 */
		[22] = RCAR_GP_PIN(5, 10),	/* TX2_A */
		[23] = RCAR_GP_PIN(5, 11),	/* RX2_A */
		[24] = RCAR_GP_PIN(5, 12),	/* HSCK0 */
		[25] = RCAR_GP_PIN(5, 13),	/* HRX0 */
		[26] = RCAR_GP_PIN(5, 14),	/* HTX0 */
		[27] = RCAR_GP_PIN(5, 15),	/* HCTS0_N */
		[28] = RCAR_GP_PIN(5, 16),	/* HRTS0_N */
		[29] = RCAR_GP_PIN(5, 17),	/* MSIOF0_SCK */
		[30] = RCAR_GP_PIN(5, 18),	/* MSIOF0_SYNC */
		[31] = RCAR_GP_PIN(5, 19),	/* MSIOF0_SS1 */
	} },
	{ PINMUX_BIAS_REG("PUEN5", 0xe6060414, "PUD5", 0xe6060454) {
		[ 0] = RCAR_GP_PIN(5, 20),	/* MSIOF0_TXD */
		[ 1] = RCAR_GP_PIN(5, 21),	/* MSIOF0_SS2 */
		[ 2] = RCAR_GP_PIN(5, 22),	/* MSIOF0_RXD */
		[ 3] = RCAR_GP_PIN(5, 23),	/* MLB_CLK */
		[ 4] = RCAR_GP_PIN(5, 24),	/* MLB_SIG */
		[ 5] = RCAR_GP_PIN(5, 25),	/* MLB_DAT */
		[ 6] = PIN_MLB_REF,		/* MLB_REF */
		[ 7] = RCAR_GP_PIN(6,  0),	/* SSI_SCK01239 */
		[ 8] = RCAR_GP_PIN(6,  1),	/* SSI_WS01239 */
		[ 9] = RCAR_GP_PIN(6,  2),	/* SSI_SDATA0 */
		[10] = RCAR_GP_PIN(6,  3),	/* SSI_SDATA1_A */
		[11] = RCAR_GP_PIN(6,  4),	/* SSI_SDATA2_A */
		[12] = RCAR_GP_PIN(6,  5),	/* SSI_SCK349 */
		[13] = RCAR_GP_PIN(6,  6),	/* SSI_WS349 */
		[14] = RCAR_GP_PIN(6,  7),	/* SSI_SDATA3 */
		[15] = RCAR_GP_PIN(6,  8),	/* SSI_SCK4 */
		[16] = RCAR_GP_PIN(6,  9),	/* SSI_WS4 */
		[17] = RCAR_GP_PIN(6, 10),	/* SSI_SDATA4 */
		[18] = RCAR_GP_PIN(6, 11),	/* SSI_SCK5 */
		[19] = RCAR_GP_PIN(6, 12),	/* SSI_WS5 */
		[20] = RCAR_GP_PIN(6, 13),	/* SSI_SDATA5 */
		[21] = RCAR_GP_PIN(6, 14),	/* SSI_SCK6 */
		[22] = RCAR_GP_PIN(6, 15),	/* SSI_WS6 */
		[23] = RCAR_GP_PIN(6, 16),	/* SSI_SDATA6 */
		[24] = RCAR_GP_PIN(6, 17),	/* SSI_SCK78 */
		[25] = RCAR_GP_PIN(6, 18),	/* SSI_WS78 */
		[26] = RCAR_GP_PIN(6, 19),	/* SSI_SDATA7 */
		[27] = RCAR_GP_PIN(6, 20),	/* SSI_SDATA8 */
		[28] = RCAR_GP_PIN(6, 21),	/* SSI_SDATA9_A */
		[29] = RCAR_GP_PIN(6, 22),	/* AUDIO_CLKA_A */
		[30] = RCAR_GP_PIN(6, 23),	/* AUDIO_CLKB_B */
		[31] = RCAR_GP_PIN(6, 24),	/* USB0_PWEN */
	} },
	{ PINMUX_BIAS_REG("PUEN6", 0xe6060418, "PUD6", 0xe6060458) {
		[ 0] = RCAR_GP_PIN(6, 25),	/* USB0_OVC */
		[ 1] = RCAR_GP_PIN(6, 26),	/* USB1_PWEN */
		[ 2] = RCAR_GP_PIN(6, 27),	/* USB1_OVC */
		[ 3] = RCAR_GP_PIN(6, 28),	/* USB30_PWEN */
		[ 4] = RCAR_GP_PIN(6, 29),	/* USB30_OVC */
		[ 5] = RCAR_GP_PIN(6, 30),	/* USB2_CH3_PWEN */
		[ 6] = RCAR_GP_PIN(6, 31),	/* USB2_CH3_OVC */
		[ 7] = SH_PFC_PIN_NONE,
		[ 8] = SH_PFC_PIN_NONE,
		[ 9] = SH_PFC_PIN_NONE,
		[10] = SH_PFC_PIN_NONE,
		[11] = SH_PFC_PIN_NONE,
		[12] = SH_PFC_PIN_NONE,
		[13] = SH_PFC_PIN_NONE,
		[14] = SH_PFC_PIN_NONE,
		[15] = SH_PFC_PIN_NONE,
		[16] = SH_PFC_PIN_NONE,
		[17] = SH_PFC_PIN_NONE,
		[18] = SH_PFC_PIN_NONE,
		[19] = SH_PFC_PIN_NONE,
		[20] = SH_PFC_PIN_NONE,
		[21] = SH_PFC_PIN_NONE,
		[22] = SH_PFC_PIN_NONE,
		[23] = SH_PFC_PIN_NONE,
		[24] = SH_PFC_PIN_NONE,
		[25] = SH_PFC_PIN_NONE,
		[26] = SH_PFC_PIN_NONE,
		[27] = SH_PFC_PIN_NONE,
		[28] = SH_PFC_PIN_NONE,
		[29] = SH_PFC_PIN_NONE,
		[30] = SH_PFC_PIN_NONE,
		[31] = SH_PFC_PIN_NONE,
	} },
	{ /* sentinel */ },
};

static const struct sh_pfc_soc_operations r8a77951_pinmux_ops = {
	.pin_to_pocctrl = r8a77951_pin_to_pocctrl,
	.get_bias = rcar_pinmux_get_bias,
	.set_bias = rcar_pinmux_set_bias,
};

#ifdef CONFIG_PINCTRL_PFC_R8A774E1
const struct sh_pfc_soc_info r8a774e1_pinmux_info = {
	.name = "r8a774e1_pfc",
	.ops = &r8a77951_pinmux_ops,
	.unlock_reg = 0xe6060000, /* PMMR */

	.function = { PINMUX_FUNCTION_BEGIN, PINMUX_FUNCTION_END },

	.pins = pinmux_pins,
	.nr_pins = ARRAY_SIZE(pinmux_pins),
	.groups = pinmux_groups.common,
	.nr_groups = ARRAY_SIZE(pinmux_groups.common),
	.functions = pinmux_functions.common,
	.nr_functions = ARRAY_SIZE(pinmux_functions.common),

	.cfg_regs = pinmux_config_regs,
	.drive_regs = pinmux_drive_regs,
	.bias_regs = pinmux_bias_regs,
	.ioctrl_regs = pinmux_ioctrl_regs,

	.pinmux_data = pinmux_data,
	.pinmux_data_size = ARRAY_SIZE(pinmux_data),
};
#endif

#ifdef CONFIG_PINCTRL_PFC_R8A77951
const struct sh_pfc_soc_info r8a77951_pinmux_info = {
	.name = "r8a77951_pfc",
	.ops = &r8a77951_pinmux_ops,
	.unlock_reg = 0xe6060000, /* PMMR */

	.function = { PINMUX_FUNCTION_BEGIN, PINMUX_FUNCTION_END },

	.pins = pinmux_pins,
	.nr_pins = ARRAY_SIZE(pinmux_pins),
	.groups = pinmux_groups.common,
	.nr_groups = ARRAY_SIZE(pinmux_groups.common) +
			ARRAY_SIZE(pinmux_groups.automotive),
	.functions = pinmux_functions.common,
	.nr_functions = ARRAY_SIZE(pinmux_functions.common) +
			ARRAY_SIZE(pinmux_functions.automotive),

	.cfg_regs = pinmux_config_regs,
	.drive_regs = pinmux_drive_regs,
	.bias_regs = pinmux_bias_regs,
	.ioctrl_regs = pinmux_ioctrl_regs,

	.pinmux_data = pinmux_data,
	.pinmux_data_size = ARRAY_SIZE(pinmux_data),
};
#endif
