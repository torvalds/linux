// SPDX-License-Identifier: GPL-2.0
/*
 * R8A77990 processor support - PFC hardware block.
 *
 * Copyright (C) 2018-2019 Renesas Electronics Corp.
 *
 * This file is based on the drivers/pinctrl/renesas/pfc-r8a7796.c
 *
 * R8A7796 processor support - PFC hardware block.
 *
 * Copyright (C) 2016-2017 Renesas Electronics Corp.
 */

#include <linux/errno.h>
#include <linux/kernel.h>

#include "core.h"
#include "sh_pfc.h"

#define CFG_FLAGS (SH_PFC_PIN_CFG_PULL_UP_DOWN)

#define CPU_ALL_GP(fn, sfx) \
	PORT_GP_CFG_18(0, fn, sfx, CFG_FLAGS), \
	PORT_GP_CFG_23(1, fn, sfx, CFG_FLAGS), \
	PORT_GP_CFG_26(2, fn, sfx, CFG_FLAGS), \
	PORT_GP_CFG_12(3, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE), \
	PORT_GP_CFG_1(3, 12, fn, sfx, CFG_FLAGS), \
	PORT_GP_CFG_1(3, 13, fn, sfx, CFG_FLAGS), \
	PORT_GP_CFG_1(3, 14, fn, sfx, CFG_FLAGS), \
	PORT_GP_CFG_1(3, 15, fn, sfx, CFG_FLAGS), \
	PORT_GP_CFG_11(4, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE), \
	PORT_GP_CFG_20(5, fn, sfx, CFG_FLAGS), \
	PORT_GP_CFG_9(6, fn, sfx, CFG_FLAGS), \
	PORT_GP_CFG_1(6, 9, fn, sfx, SH_PFC_PIN_CFG_PULL_UP), \
	PORT_GP_CFG_1(6, 10, fn, sfx, CFG_FLAGS), \
	PORT_GP_CFG_1(6, 11, fn, sfx, CFG_FLAGS), \
	PORT_GP_CFG_1(6, 12, fn, sfx, CFG_FLAGS), \
	PORT_GP_CFG_1(6, 13, fn, sfx, CFG_FLAGS), \
	PORT_GP_CFG_1(6, 14, fn, sfx, CFG_FLAGS), \
	PORT_GP_CFG_1(6, 15, fn, sfx, CFG_FLAGS), \
	PORT_GP_CFG_1(6, 16, fn, sfx, CFG_FLAGS), \
	PORT_GP_CFG_1(6, 17, fn, sfx, CFG_FLAGS)

#define CPU_ALL_NOGP(fn)						\
	PIN_NOGP_CFG(ASEBRK, "ASEBRK", fn, CFG_FLAGS),			\
	PIN_NOGP_CFG(AVB_MDC, "AVB_MDC", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(AVB_MDIO, "AVB_MDIO", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(AVB_TD0, "AVB_TD0", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(AVB_TD1, "AVB_TD1", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(AVB_TD2, "AVB_TD2", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(AVB_TD3, "AVB_TD3", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(AVB_TXC, "AVB_TXC", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(AVB_TX_CTL, "AVB_TX_CTL", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(FSCLKST_N, "FSCLKST_N", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(MLB_REF, "MLB_REF", fn, CFG_FLAGS),		\
	PIN_NOGP_CFG(PRESETOUT_N, "PRESETOUT_N", fn, CFG_FLAGS),	\
	PIN_NOGP_CFG(TCK, "TCK", fn, CFG_FLAGS),			\
	PIN_NOGP_CFG(TDI, "TDI", fn, CFG_FLAGS),			\
	PIN_NOGP_CFG(TMS, "TMS", fn, CFG_FLAGS),			\
	PIN_NOGP_CFG(TRST_N, "TRST_N", fn, CFG_FLAGS)

/*
 * F_() : just information
 * FM() : macro for FN_xxx / xxx_MARK
 */

/* GPSR0 */
#define GPSR0_17	F_(SDA4,		IP7_27_24)
#define GPSR0_16	F_(SCL4,		IP7_23_20)
#define GPSR0_15	F_(D15,			IP7_19_16)
#define GPSR0_14	F_(D14,			IP7_15_12)
#define GPSR0_13	F_(D13,			IP7_11_8)
#define GPSR0_12	F_(D12,			IP7_7_4)
#define GPSR0_11	F_(D11,			IP7_3_0)
#define GPSR0_10	F_(D10,			IP6_31_28)
#define GPSR0_9		F_(D9,			IP6_27_24)
#define GPSR0_8		F_(D8,			IP6_23_20)
#define GPSR0_7		F_(D7,			IP6_19_16)
#define GPSR0_6		F_(D6,			IP6_15_12)
#define GPSR0_5		F_(D5,			IP6_11_8)
#define GPSR0_4		F_(D4,			IP6_7_4)
#define GPSR0_3		F_(D3,			IP6_3_0)
#define GPSR0_2		F_(D2,			IP5_31_28)
#define GPSR0_1		F_(D1,			IP5_27_24)
#define GPSR0_0		F_(D0,			IP5_23_20)

/* GPSR1 */
#define GPSR1_22	F_(WE0_N,		IP5_19_16)
#define GPSR1_21	F_(CS0_N,		IP5_15_12)
#define GPSR1_20	FM(CLKOUT)
#define GPSR1_19	F_(A19,			IP5_11_8)
#define GPSR1_18	F_(A18,			IP5_7_4)
#define GPSR1_17	F_(A17,			IP5_3_0)
#define GPSR1_16	F_(A16,			IP4_31_28)
#define GPSR1_15	F_(A15,			IP4_27_24)
#define GPSR1_14	F_(A14,			IP4_23_20)
#define GPSR1_13	F_(A13,			IP4_19_16)
#define GPSR1_12	F_(A12,			IP4_15_12)
#define GPSR1_11	F_(A11,			IP4_11_8)
#define GPSR1_10	F_(A10,			IP4_7_4)
#define GPSR1_9		F_(A9,			IP4_3_0)
#define GPSR1_8		F_(A8,			IP3_31_28)
#define GPSR1_7		F_(A7,			IP3_27_24)
#define GPSR1_6		F_(A6,			IP3_23_20)
#define GPSR1_5		F_(A5,			IP3_19_16)
#define GPSR1_4		F_(A4,			IP3_15_12)
#define GPSR1_3		F_(A3,			IP3_11_8)
#define GPSR1_2		F_(A2,			IP3_7_4)
#define GPSR1_1		F_(A1,			IP3_3_0)
#define GPSR1_0		F_(A0,			IP2_31_28)

/* GPSR2 */
#define GPSR2_25	F_(EX_WAIT0,		IP2_27_24)
#define GPSR2_24	F_(RD_WR_N,		IP2_23_20)
#define GPSR2_23	F_(RD_N,		IP2_19_16)
#define GPSR2_22	F_(BS_N,		IP2_15_12)
#define GPSR2_21	FM(AVB_PHY_INT)
#define GPSR2_20	F_(AVB_TXCREFCLK,	IP2_3_0)
#define GPSR2_19	FM(AVB_RD3)
#define GPSR2_18	F_(AVB_RD2,		IP1_31_28)
#define GPSR2_17	F_(AVB_RD1,		IP1_27_24)
#define GPSR2_16	F_(AVB_RD0,		IP1_23_20)
#define GPSR2_15	FM(AVB_RXC)
#define GPSR2_14	FM(AVB_RX_CTL)
#define GPSR2_13	F_(RPC_RESET_N,		IP1_19_16)
#define GPSR2_12	F_(RPC_INT_N,		IP1_15_12)
#define GPSR2_11	F_(QSPI1_SSL,		IP1_11_8)
#define GPSR2_10	F_(QSPI1_IO3,		IP1_7_4)
#define GPSR2_9		F_(QSPI1_IO2,		IP1_3_0)
#define GPSR2_8		F_(QSPI1_MISO_IO1,	IP0_31_28)
#define GPSR2_7		F_(QSPI1_MOSI_IO0,	IP0_27_24)
#define GPSR2_6		F_(QSPI1_SPCLK,		IP0_23_20)
#define GPSR2_5		FM(QSPI0_SSL)
#define GPSR2_4		F_(QSPI0_IO3,		IP0_19_16)
#define GPSR2_3		F_(QSPI0_IO2,		IP0_15_12)
#define GPSR2_2		F_(QSPI0_MISO_IO1,	IP0_11_8)
#define GPSR2_1		F_(QSPI0_MOSI_IO0,	IP0_7_4)
#define GPSR2_0		F_(QSPI0_SPCLK,		IP0_3_0)

/* GPSR3 */
#define GPSR3_15	F_(SD1_WP,		IP11_7_4)
#define GPSR3_14	F_(SD1_CD,		IP11_3_0)
#define GPSR3_13	F_(SD0_WP,		IP10_31_28)
#define GPSR3_12	F_(SD0_CD,		IP10_27_24)
#define GPSR3_11	F_(SD1_DAT3,		IP9_11_8)
#define GPSR3_10	F_(SD1_DAT2,		IP9_7_4)
#define GPSR3_9		F_(SD1_DAT1,		IP9_3_0)
#define GPSR3_8		F_(SD1_DAT0,		IP8_31_28)
#define GPSR3_7		F_(SD1_CMD,		IP8_27_24)
#define GPSR3_6		F_(SD1_CLK,		IP8_23_20)
#define GPSR3_5		F_(SD0_DAT3,		IP8_19_16)
#define GPSR3_4		F_(SD0_DAT2,		IP8_15_12)
#define GPSR3_3		F_(SD0_DAT1,		IP8_11_8)
#define GPSR3_2		F_(SD0_DAT0,		IP8_7_4)
#define GPSR3_1		F_(SD0_CMD,		IP8_3_0)
#define GPSR3_0		F_(SD0_CLK,		IP7_31_28)

/* GPSR4 */
#define GPSR4_10	F_(SD3_DS,		IP10_23_20)
#define GPSR4_9		F_(SD3_DAT7,		IP10_19_16)
#define GPSR4_8		F_(SD3_DAT6,		IP10_15_12)
#define GPSR4_7		F_(SD3_DAT5,		IP10_11_8)
#define GPSR4_6		F_(SD3_DAT4,		IP10_7_4)
#define GPSR4_5		F_(SD3_DAT3,		IP10_3_0)
#define GPSR4_4		F_(SD3_DAT2,		IP9_31_28)
#define GPSR4_3		F_(SD3_DAT1,		IP9_27_24)
#define GPSR4_2		F_(SD3_DAT0,		IP9_23_20)
#define GPSR4_1		F_(SD3_CMD,		IP9_19_16)
#define GPSR4_0		F_(SD3_CLK,		IP9_15_12)

/* GPSR5 */
#define GPSR5_19	F_(MLB_DAT,		IP13_23_20)
#define GPSR5_18	F_(MLB_SIG,		IP13_19_16)
#define GPSR5_17	F_(MLB_CLK,		IP13_15_12)
#define GPSR5_16	F_(SSI_SDATA9,		IP13_11_8)
#define GPSR5_15	F_(MSIOF0_SS2,		IP13_7_4)
#define GPSR5_14	F_(MSIOF0_SS1,		IP13_3_0)
#define GPSR5_13	F_(MSIOF0_SYNC,		IP12_31_28)
#define GPSR5_12	F_(MSIOF0_TXD,		IP12_27_24)
#define GPSR5_11	F_(MSIOF0_RXD,		IP12_23_20)
#define GPSR5_10	F_(MSIOF0_SCK,		IP12_19_16)
#define GPSR5_9		F_(RX2_A,		IP12_15_12)
#define GPSR5_8		F_(TX2_A,		IP12_11_8)
#define GPSR5_7		F_(SCK2_A,		IP12_7_4)
#define GPSR5_6		F_(TX1,			IP12_3_0)
#define GPSR5_5		F_(RX1,			IP11_31_28)
#define GPSR5_4		F_(RTS0_N_A,		IP11_23_20)
#define GPSR5_3		F_(CTS0_N_A,		IP11_19_16)
#define GPSR5_2		F_(TX0_A,		IP11_15_12)
#define GPSR5_1		F_(RX0_A,		IP11_11_8)
#define GPSR5_0		F_(SCK0_A,		IP11_27_24)

/* GPSR6 */
#define GPSR6_17	F_(USB30_PWEN,		IP15_27_24)
#define GPSR6_16	F_(SSI_SDATA6,		IP15_19_16)
#define GPSR6_15	F_(SSI_WS6,		IP15_15_12)
#define GPSR6_14	F_(SSI_SCK6,		IP15_11_8)
#define GPSR6_13	F_(SSI_SDATA5,		IP15_7_4)
#define GPSR6_12	F_(SSI_WS5,		IP15_3_0)
#define GPSR6_11	F_(SSI_SCK5,		IP14_31_28)
#define GPSR6_10	F_(SSI_SDATA4,		IP14_27_24)
#define GPSR6_9		F_(USB30_OVC,		IP15_31_28)
#define GPSR6_8		F_(AUDIO_CLKA,		IP15_23_20)
#define GPSR6_7		F_(SSI_SDATA3,		IP14_23_20)
#define GPSR6_6		F_(SSI_WS349,		IP14_19_16)
#define GPSR6_5		F_(SSI_SCK349,		IP14_15_12)
#define GPSR6_4		F_(SSI_SDATA2,		IP14_11_8)
#define GPSR6_3		F_(SSI_SDATA1,		IP14_7_4)
#define GPSR6_2		F_(SSI_SDATA0,		IP14_3_0)
#define GPSR6_1		F_(SSI_WS01239,		IP13_31_28)
#define GPSR6_0		F_(SSI_SCK01239,	IP13_27_24)

/* IPSRx */		/* 0 */			/* 1 */			/* 2 */			/* 3 */			/* 4 */			/* 5 */		/* 6 */		/* 7 */		/* 8 */		/* 9 - F */
#define IP0_3_0		FM(QSPI0_SPCLK)		FM(HSCK4_A)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0_7_4		FM(QSPI0_MOSI_IO0)	FM(HCTS4_N_A)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0_11_8	FM(QSPI0_MISO_IO1)	FM(HRTS4_N_A)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0_15_12	FM(QSPI0_IO2)		FM(HTX4_A)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0_19_16	FM(QSPI0_IO3)		FM(HRX4_A)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0_23_20	FM(QSPI1_SPCLK)		FM(RIF2_CLK_A)		FM(HSCK4_B)		FM(VI4_DATA0_A)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0_27_24	FM(QSPI1_MOSI_IO0)	FM(RIF2_SYNC_A)		FM(HTX4_B)		FM(VI4_DATA1_A)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0_31_28	FM(QSPI1_MISO_IO1)	FM(RIF2_D0_A)		FM(HRX4_B)		FM(VI4_DATA2_A)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1_3_0		FM(QSPI1_IO2)		FM(RIF2_D1_A)		FM(HTX3_C)		FM(VI4_DATA3_A)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1_7_4		FM(QSPI1_IO3)		FM(RIF3_CLK_A)		FM(HRX3_C)		FM(VI4_DATA4_A)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1_11_8	FM(QSPI1_SSL)		FM(RIF3_SYNC_A)		FM(HSCK3_C)		FM(VI4_DATA5_A)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1_15_12	FM(RPC_INT_N)		FM(RIF3_D0_A)		FM(HCTS3_N_C)		FM(VI4_DATA6_A)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1_19_16	FM(RPC_RESET_N)		FM(RIF3_D1_A)		FM(HRTS3_N_C)		FM(VI4_DATA7_A)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1_23_20	FM(AVB_RD0)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1_27_24	FM(AVB_RD1)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1_31_28	FM(AVB_RD2)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2_3_0		FM(AVB_TXCREFCLK)	F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2_7_4		FM(AVB_MDIO)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2_11_8	FM(AVB_MDC)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2_15_12	FM(BS_N)		FM(PWM0_A)		FM(AVB_MAGIC)		FM(VI4_CLK)		F_(0, 0)		FM(TX3_C)	F_(0, 0)	FM(VI5_CLK_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2_19_16	FM(RD_N)		FM(PWM1_A)		FM(AVB_LINK)		FM(VI4_FIELD)		F_(0, 0)		FM(RX3_C)	FM(FSCLKST2_N_A) FM(VI5_DATA0_B) F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2_23_20	FM(RD_WR_N)		FM(SCL7_A)		FM(AVB_AVTP_MATCH)	FM(VI4_VSYNC_N)		FM(TX5_B)		FM(SCK3_C)	FM(PWM5_A)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2_27_24	FM(EX_WAIT0)		FM(SDA7_A)		FM(AVB_AVTP_CAPTURE)	FM(VI4_HSYNC_N)		FM(RX5_B)		FM(PWM6_A)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2_31_28	FM(A0)			FM(IRQ0)		FM(PWM2_A)		FM(MSIOF3_SS1_B)	FM(VI5_CLK_A)		FM(DU_CDE)	FM(HRX3_D)	FM(IERX)	FM(QSTB_QHE)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3_3_0		FM(A1)			FM(IRQ1)		FM(PWM3_A)		FM(DU_DOTCLKIN1)	FM(VI5_DATA0_A)		FM(DU_DISP_CDE) FM(SDA6_B)	FM(IETX)	FM(QCPV_QDE)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3_7_4		FM(A2)			FM(IRQ2)		FM(AVB_AVTP_PPS)	FM(VI4_CLKENB)		FM(VI5_DATA1_A)		FM(DU_DISP)	FM(SCL6_B)	F_(0, 0)	FM(QSTVB_QVE)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3_11_8	FM(A3)			FM(CTS4_N_A)		FM(PWM4_A)		FM(VI4_DATA12)		F_(0, 0)		FM(DU_DOTCLKOUT0) FM(HTX3_D)	FM(IECLK)	FM(LCDOUT12)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3_15_12	FM(A4)			FM(RTS4_N_A)		FM(MSIOF3_SYNC_B)	FM(VI4_DATA8)		FM(PWM2_B)		FM(DU_DG4)	FM(RIF2_CLK_B)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3_19_16	FM(A5)			FM(SCK4_A)		FM(MSIOF3_SCK_B)	FM(VI4_DATA9)		FM(PWM3_B)		F_(0, 0)	FM(RIF2_SYNC_B)	F_(0, 0)	FM(QPOLA)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3_23_20	FM(A6)			FM(RX4_A)		FM(MSIOF3_RXD_B)	FM(VI4_DATA10)		F_(0, 0)		F_(0, 0)	FM(RIF2_D0_B)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3_27_24	FM(A7)			FM(TX4_A)		FM(MSIOF3_TXD_B)	FM(VI4_DATA11)		F_(0, 0)		F_(0, 0)	FM(RIF2_D1_B)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3_31_28	FM(A8)			FM(SDA6_A)		FM(RX3_B)		FM(HRX4_C)		FM(VI5_HSYNC_N_A)	FM(DU_HSYNC)	FM(VI4_DATA0_B)	F_(0, 0)	FM(QSTH_QHS)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IPSRx */		/* 0 */			/* 1 */			/* 2 */			/* 3 */			/* 4 */			/* 5 */		/* 6 */		/* 7 */		/* 8 */		/* 9 - F */
#define IP4_3_0		FM(A9)			FM(TX5_A)		FM(IRQ3)		FM(VI4_DATA16)		FM(VI5_VSYNC_N_A)	FM(DU_DG7)	F_(0, 0)	F_(0, 0)	FM(LCDOUT15)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP4_7_4		FM(A10)			FM(IRQ4)		FM(MSIOF2_SYNC_B)	FM(VI4_DATA13)		FM(VI5_FIELD_A)		FM(DU_DG5)	FM(FSCLKST2_N_B) F_(0, 0)	FM(LCDOUT13)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP4_11_8	FM(A11)			FM(SCL6_A)		FM(TX3_B)		FM(HTX4_C)		F_(0, 0)		FM(DU_VSYNC)	FM(VI4_DATA1_B)	F_(0, 0)	FM(QSTVA_QVS)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP4_15_12	FM(A12)			FM(RX5_A)		FM(MSIOF2_SS2_B)	FM(VI4_DATA17)		FM(VI5_DATA3_A)		FM(DU_DG6)	F_(0, 0)	F_(0, 0)	FM(LCDOUT14)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP4_19_16	FM(A13)			FM(SCK5_A)		FM(MSIOF2_SCK_B)	FM(VI4_DATA14)		FM(HRX4_D)		FM(DU_DB2)	F_(0, 0)	F_(0, 0)	FM(LCDOUT2)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP4_23_20	FM(A14)			FM(MSIOF1_SS1)		FM(MSIOF2_RXD_B)	FM(VI4_DATA15)		FM(HTX4_D)		FM(DU_DB3)	F_(0, 0)	F_(0, 0)	FM(LCDOUT3)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP4_27_24	FM(A15)			FM(MSIOF1_SS2)		FM(MSIOF2_TXD_B)	FM(VI4_DATA18)		FM(VI5_DATA4_A)		FM(DU_DB4)	F_(0, 0)	F_(0, 0)	FM(LCDOUT4)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP4_31_28	FM(A16)			FM(MSIOF1_SYNC)		FM(MSIOF2_SS1_B)	FM(VI4_DATA19)		FM(VI5_DATA5_A)		FM(DU_DB5)	F_(0, 0)	F_(0, 0)	FM(LCDOUT5)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP5_3_0		FM(A17)			FM(MSIOF1_RXD)		F_(0, 0)		FM(VI4_DATA20)		FM(VI5_DATA6_A)		FM(DU_DB6)	F_(0, 0)	F_(0, 0)	FM(LCDOUT6)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP5_7_4		FM(A18)			FM(MSIOF1_TXD)		F_(0, 0)		FM(VI4_DATA21)		FM(VI5_DATA7_A)		FM(DU_DB0)	F_(0, 0)	FM(HRX4_E)	FM(LCDOUT0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP5_11_8	FM(A19)			FM(MSIOF1_SCK)		F_(0, 0)		FM(VI4_DATA22)		FM(VI5_DATA2_A)		FM(DU_DB1)	F_(0, 0)	FM(HTX4_E)	FM(LCDOUT1)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP5_15_12	FM(CS0_N)		FM(SCL5)		F_(0, 0)		F_(0, 0)		F_(0, 0)		FM(DU_DR0)	FM(VI4_DATA2_B)	F_(0, 0)	FM(LCDOUT16)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP5_19_16	FM(WE0_N)		FM(SDA5)		F_(0, 0)		F_(0, 0)		F_(0, 0)		FM(DU_DR1)	FM(VI4_DATA3_B)	F_(0, 0)	FM(LCDOUT17)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP5_23_20	FM(D0)			FM(MSIOF3_SCK_A)	F_(0, 0)		F_(0, 0)		F_(0, 0)		FM(DU_DR2)	FM(CTS4_N_C)	F_(0, 0)	FM(LCDOUT18)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP5_27_24	FM(D1)			FM(MSIOF3_SYNC_A)	FM(SCK3_A)		FM(VI4_DATA23)		FM(VI5_CLKENB_A)	FM(DU_DB7)	FM(RTS4_N_C)	F_(0, 0)	FM(LCDOUT7)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP5_31_28	FM(D2)			FM(MSIOF3_RXD_A)	FM(RX5_C)		F_(0, 0)		FM(VI5_DATA14_A)	FM(DU_DR3)	FM(RX4_C)	F_(0, 0)	FM(LCDOUT19)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP6_3_0		FM(D3)			FM(MSIOF3_TXD_A)	FM(TX5_C)		F_(0, 0)		FM(VI5_DATA15_A)	FM(DU_DR4)	FM(TX4_C)	F_(0, 0)	FM(LCDOUT20)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP6_7_4		FM(D4)			FM(CANFD1_TX)		FM(HSCK3_B)		FM(CAN1_TX)		FM(RTS3_N_A)		FM(MSIOF3_SS2_A) F_(0, 0)	FM(VI5_DATA1_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP6_11_8	FM(D5)			FM(RX3_A)		FM(HRX3_B)		F_(0, 0)		F_(0, 0)		FM(DU_DR5)	FM(VI4_DATA4_B)	F_(0, 0)	FM(LCDOUT21)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP6_15_12	FM(D6)			FM(TX3_A)		FM(HTX3_B)		F_(0, 0)		F_(0, 0)		FM(DU_DR6)	FM(VI4_DATA5_B)	F_(0, 0)	FM(LCDOUT22)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP6_19_16	FM(D7)			FM(CANFD1_RX)		FM(IRQ5)		FM(CAN1_RX)		FM(CTS3_N_A)		F_(0, 0)	F_(0, 0)	FM(VI5_DATA2_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP6_23_20	FM(D8)			FM(MSIOF2_SCK_A)	FM(SCK4_B)		F_(0, 0)		FM(VI5_DATA12_A)	FM(DU_DR7)	FM(RIF3_CLK_B)	FM(HCTS3_N_E)	FM(LCDOUT23)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP6_27_24	FM(D9)			FM(MSIOF2_SYNC_A)	F_(0, 0)		F_(0, 0)		FM(VI5_DATA10_A)	FM(DU_DG0)	FM(RIF3_SYNC_B)	FM(HRX3_E)	FM(LCDOUT8)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP6_31_28	FM(D10)			FM(MSIOF2_RXD_A)	F_(0, 0)		F_(0, 0)		FM(VI5_DATA13_A)	FM(DU_DG1)	FM(RIF3_D0_B)	FM(HTX3_E)	FM(LCDOUT9)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP7_3_0		FM(D11)			FM(MSIOF2_TXD_A)	F_(0, 0)		F_(0, 0)		FM(VI5_DATA11_A)	FM(DU_DG2)	FM(RIF3_D1_B)	FM(HRTS3_N_E)	FM(LCDOUT10)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP7_7_4		FM(D12)			FM(CANFD0_TX)		FM(TX4_B)		FM(CAN0_TX)		FM(VI5_DATA8_A)		F_(0, 0)	F_(0, 0)	FM(VI5_DATA3_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP7_11_8	FM(D13)			FM(CANFD0_RX)		FM(RX4_B)		FM(CAN0_RX)		FM(VI5_DATA9_A)		FM(SCL7_B)	F_(0, 0)	FM(VI5_DATA4_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP7_15_12	FM(D14)			FM(CAN_CLK)		FM(HRX3_A)		FM(MSIOF2_SS2_A)	F_(0, 0)		FM(SDA7_B)	F_(0, 0)	FM(VI5_DATA5_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP7_19_16	FM(D15)			FM(MSIOF2_SS1_A)	FM(HTX3_A)		FM(MSIOF3_SS1_A)	F_(0, 0)		FM(DU_DG3)	F_(0, 0)	F_(0, 0)	FM(LCDOUT11)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP7_23_20	FM(SCL4)		FM(CS1_N_A26)		F_(0, 0)		F_(0, 0)		F_(0, 0)		FM(DU_DOTCLKIN0) FM(VI4_DATA6_B) FM(VI5_DATA6_B) FM(QCLK)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP7_27_24	FM(SDA4)		FM(WE1_N)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	FM(VI4_DATA7_B)	FM(VI5_DATA7_B)	FM(QPOLB)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP7_31_28	FM(SD0_CLK)		FM(NFDATA8)		FM(SCL1_C)		FM(HSCK1_B)		FM(SDA2_E)		FM(FMCLK_B)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IPSRx */		/* 0 */			/* 1 */			/* 2 */			/* 3 */			/* 4 */			/* 5 */		/* 6 */		/* 7 */		/* 8 */		/* 9 - F */
#define IP8_3_0		FM(SD0_CMD)		FM(NFDATA9)		F_(0, 0)		FM(HRX1_B)		F_(0, 0)		FM(SPEEDIN_B)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP8_7_4		FM(SD0_DAT0)		FM(NFDATA10)		F_(0, 0)		FM(HTX1_B)		F_(0, 0)		FM(REMOCON_B)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP8_11_8	FM(SD0_DAT1)		FM(NFDATA11)		FM(SDA2_C)		FM(HCTS1_N_B)		F_(0, 0)		FM(FMIN_B)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP8_15_12	FM(SD0_DAT2)		FM(NFDATA12)		FM(SCL2_C)		FM(HRTS1_N_B)		F_(0, 0)		FM(BPFCLK_B)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP8_19_16	FM(SD0_DAT3)		FM(NFDATA13)		FM(SDA1_C)		FM(SCL2_E)		FM(SPEEDIN_C)		FM(REMOCON_C)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP8_23_20	FM(SD1_CLK)		FM(NFDATA14_B)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP8_27_24	FM(SD1_CMD)		FM(NFDATA15_B)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP8_31_28	FM(SD1_DAT0)		FM(NFWP_N_B)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP9_3_0		FM(SD1_DAT1)		FM(NFCE_N_B)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP9_7_4		FM(SD1_DAT2)		FM(NFALE_B)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP9_11_8	FM(SD1_DAT3)		FM(NFRB_N_B)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP9_15_12	FM(SD3_CLK)		FM(NFWE_N)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP9_19_16	FM(SD3_CMD)		FM(NFRE_N)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP9_23_20	FM(SD3_DAT0)		FM(NFDATA0)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP9_27_24	FM(SD3_DAT1)		FM(NFDATA1)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP9_31_28	FM(SD3_DAT2)		FM(NFDATA2)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP10_3_0	FM(SD3_DAT3)		FM(NFDATA3)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP10_7_4	FM(SD3_DAT4)		FM(NFDATA4)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP10_11_8	FM(SD3_DAT5)		FM(NFDATA5)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP10_15_12	FM(SD3_DAT6)		FM(NFDATA6)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP10_19_16	FM(SD3_DAT7)		FM(NFDATA7)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP10_23_20	FM(SD3_DS)		FM(NFCLE)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP10_27_24	FM(SD0_CD)		FM(NFALE_A)		FM(SD3_CD)		FM(RIF0_CLK_B)		FM(SCL2_B)		FM(TCLK1_A)	FM(SSI_SCK2_B)	FM(TS_SCK0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP10_31_28	FM(SD0_WP)		FM(NFRB_N_A)		FM(SD3_WP)		FM(RIF0_D0_B)		FM(SDA2_B)		FM(TCLK2_A)	FM(SSI_WS2_B)	FM(TS_SDAT0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP11_3_0	FM(SD1_CD)		FM(NFCE_N_A)		FM(SSI_SCK1)		FM(RIF0_D1_B)		F_(0, 0)		F_(0, 0)	F_(0, 0)	FM(TS_SDEN0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP11_7_4	FM(SD1_WP)		FM(NFWP_N_A)		FM(SSI_WS1)		FM(RIF0_SYNC_B)		F_(0, 0)		F_(0, 0)	F_(0, 0)	FM(TS_SPSYNC0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP11_11_8	FM(RX0_A)		FM(HRX1_A)		FM(SSI_SCK2_A)		FM(RIF1_SYNC)		F_(0, 0)		F_(0, 0)	F_(0, 0)	FM(TS_SCK1)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP11_15_12	FM(TX0_A)		FM(HTX1_A)		FM(SSI_WS2_A)		FM(RIF1_D0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	FM(TS_SDAT1)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP11_19_16	FM(CTS0_N_A)		FM(NFDATA14_A)		FM(AUDIO_CLKOUT_A)	FM(RIF1_D1)		FM(SCIF_CLK_A)		FM(FMCLK_A)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP11_23_20	FM(RTS0_N_A)		FM(NFDATA15_A)		FM(AUDIO_CLKOUT1_A)	FM(RIF1_CLK)		FM(SCL2_A)		FM(FMIN_A)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP11_27_24	FM(SCK0_A)		FM(HSCK1_A)		FM(USB3HS0_ID)		FM(RTS1_N)		FM(SDA2_A)		FM(FMCLK_C)	F_(0, 0)	F_(0, 0)	FM(USB0_ID)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP11_31_28	FM(RX1)			FM(HRX2_B)		FM(SSI_SCK9_B)		FM(AUDIO_CLKOUT1_B)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IPSRx */		/* 0 */			/* 1 */			/* 2 */			/* 3 */			/* 4 */			/* 5 */		/* 6 */		/* 7 */		/* 8 */		/* 9 - F */
#define IP12_3_0	FM(TX1)			FM(HTX2_B)		FM(SSI_WS9_B)		FM(AUDIO_CLKOUT3_B)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP12_7_4	FM(SCK2_A)		FM(HSCK0_A)		FM(AUDIO_CLKB_A)	FM(CTS1_N)		FM(RIF0_CLK_A)		FM(REMOCON_A)	FM(SCIF_CLK_B)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP12_11_8	FM(TX2_A)		FM(HRX0_A)		FM(AUDIO_CLKOUT2_A)	F_(0, 0)		FM(SCL1_A)		F_(0, 0)	FM(FSO_CFE_0_N_A) FM(TS_SDEN1)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP12_15_12	FM(RX2_A)		FM(HTX0_A)		FM(AUDIO_CLKOUT3_A)	F_(0, 0)		FM(SDA1_A)		F_(0, 0)	FM(FSO_CFE_1_N_A) FM(TS_SPSYNC1) F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP12_19_16	FM(MSIOF0_SCK)		F_(0, 0)		FM(SSI_SCK78)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP12_23_20	FM(MSIOF0_RXD)		F_(0, 0)		FM(SSI_WS78)		F_(0, 0)		F_(0, 0)		FM(TX2_B)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP12_27_24	FM(MSIOF0_TXD)		F_(0, 0)		FM(SSI_SDATA7)		F_(0, 0)		F_(0, 0)		FM(RX2_B)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP12_31_28	FM(MSIOF0_SYNC)		FM(AUDIO_CLKOUT_B)	FM(SSI_SDATA8)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP13_3_0	FM(MSIOF0_SS1)		FM(HRX2_A)		FM(SSI_SCK4)		FM(HCTS0_N_A)		FM(BPFCLK_C)		FM(SPEEDIN_A)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP13_7_4	FM(MSIOF0_SS2)		FM(HTX2_A)		FM(SSI_WS4)		FM(HRTS0_N_A)		FM(FMIN_C)		FM(BPFCLK_A)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP13_11_8	FM(SSI_SDATA9)		F_(0, 0)		FM(AUDIO_CLKC_A)	FM(SCK1)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP13_15_12	FM(MLB_CLK)		FM(RX0_B)		F_(0, 0)		FM(RIF0_D0_A)		FM(SCL1_B)		FM(TCLK1_B)	F_(0, 0)	F_(0, 0)	FM(SIM0_RST_A)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP13_19_16	FM(MLB_SIG)		FM(SCK0_B)		F_(0, 0)		FM(RIF0_D1_A)		FM(SDA1_B)		FM(TCLK2_B)	F_(0, 0)	F_(0, 0)	FM(SIM0_D_A)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP13_23_20	FM(MLB_DAT)		FM(TX0_B)		F_(0, 0)		FM(RIF0_SYNC_A)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	FM(SIM0_CLK_A)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP13_27_24	FM(SSI_SCK01239)	F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP13_31_28	FM(SSI_WS01239)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP14_3_0	FM(SSI_SDATA0)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP14_7_4	FM(SSI_SDATA1)		FM(AUDIO_CLKC_B)	F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	FM(PWM0_B)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP14_11_8	FM(SSI_SDATA2)		FM(AUDIO_CLKOUT2_B)	FM(SSI_SCK9_A)		F_(0, 0)		F_(0, 0)		F_(0, 0)	FM(PWM1_B)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP14_15_12	FM(SSI_SCK349)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	FM(PWM2_C)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP14_19_16	FM(SSI_WS349)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	FM(PWM3_C)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP14_23_20	FM(SSI_SDATA3)		FM(AUDIO_CLKOUT1_C)	FM(AUDIO_CLKB_B)	F_(0, 0)		F_(0, 0)		F_(0, 0)	FM(PWM4_B)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP14_27_24	FM(SSI_SDATA4)		F_(0, 0)		FM(SSI_WS9_A)		F_(0, 0)		F_(0, 0)		F_(0, 0)	FM(PWM5_B)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP14_31_28	FM(SSI_SCK5)		FM(HRX0_B)		F_(0, 0)		FM(USB0_PWEN_B)		FM(SCL2_D)		F_(0, 0)	FM(PWM6_B)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP15_3_0	FM(SSI_WS5)		FM(HTX0_B)		F_(0, 0)		FM(USB0_OVC_B)		FM(SDA2_D)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP15_7_4	FM(SSI_SDATA5)		FM(HSCK0_B)		FM(AUDIO_CLKB_C)	FM(TPU0TO0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP15_11_8	FM(SSI_SCK6)		FM(HSCK2_A)		FM(AUDIO_CLKC_C)	FM(TPU0TO1)		F_(0, 0)		F_(0, 0)	FM(FSO_CFE_0_N_B) F_(0, 0)	FM(SIM0_RST_B)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP15_15_12	FM(SSI_WS6)		FM(HCTS2_N_A)		FM(AUDIO_CLKOUT2_C)	FM(TPU0TO2)		FM(SDA1_D)		F_(0, 0)	FM(FSO_CFE_1_N_B) F_(0, 0)	FM(SIM0_D_B)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP15_19_16	FM(SSI_SDATA6)		FM(HRTS2_N_A)		FM(AUDIO_CLKOUT3_C)	FM(TPU0TO3)		FM(SCL1_D)		F_(0, 0)	FM(FSO_TOE_N_B)	F_(0, 0)	FM(SIM0_CLK_B)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP15_23_20	FM(AUDIO_CLKA)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP15_27_24	FM(USB30_PWEN)		FM(USB0_PWEN_A)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP15_31_28	FM(USB30_OVC)		FM(USB0_OVC_A)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	FM(FSO_TOE_N_A)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

#define PINMUX_GPSR	\
\
													 \
													 \
													 \
													 \
													 \
													 \
				GPSR2_25								 \
				GPSR2_24								 \
				GPSR2_23								 \
		GPSR1_22	GPSR2_22								 \
		GPSR1_21	GPSR2_21								 \
		GPSR1_20	GPSR2_20								 \
		GPSR1_19	GPSR2_19					GPSR5_19		 \
		GPSR1_18	GPSR2_18					GPSR5_18		 \
GPSR0_17	GPSR1_17	GPSR2_17					GPSR5_17	GPSR6_17 \
GPSR0_16	GPSR1_16	GPSR2_16					GPSR5_16	GPSR6_16 \
GPSR0_15	GPSR1_15	GPSR2_15	GPSR3_15			GPSR5_15	GPSR6_15 \
GPSR0_14	GPSR1_14	GPSR2_14	GPSR3_14			GPSR5_14	GPSR6_14 \
GPSR0_13	GPSR1_13	GPSR2_13	GPSR3_13			GPSR5_13	GPSR6_13 \
GPSR0_12	GPSR1_12	GPSR2_12	GPSR3_12			GPSR5_12	GPSR6_12 \
GPSR0_11	GPSR1_11	GPSR2_11	GPSR3_11			GPSR5_11	GPSR6_11 \
GPSR0_10	GPSR1_10	GPSR2_10	GPSR3_10	GPSR4_10	GPSR5_10	GPSR6_10 \
GPSR0_9		GPSR1_9		GPSR2_9		GPSR3_9		GPSR4_9		GPSR5_9		GPSR6_9 \
GPSR0_8		GPSR1_8		GPSR2_8		GPSR3_8		GPSR4_8		GPSR5_8		GPSR6_8 \
GPSR0_7		GPSR1_7		GPSR2_7		GPSR3_7		GPSR4_7		GPSR5_7		GPSR6_7 \
GPSR0_6		GPSR1_6		GPSR2_6		GPSR3_6		GPSR4_6		GPSR5_6		GPSR6_6 \
GPSR0_5		GPSR1_5		GPSR2_5		GPSR3_5		GPSR4_5		GPSR5_5		GPSR6_5 \
GPSR0_4		GPSR1_4		GPSR2_4		GPSR3_4		GPSR4_4		GPSR5_4		GPSR6_4 \
GPSR0_3		GPSR1_3		GPSR2_3		GPSR3_3		GPSR4_3		GPSR5_3		GPSR6_3 \
GPSR0_2		GPSR1_2		GPSR2_2		GPSR3_2		GPSR4_2		GPSR5_2		GPSR6_2 \
GPSR0_1		GPSR1_1		GPSR2_1		GPSR3_1		GPSR4_1		GPSR5_1		GPSR6_1 \
GPSR0_0		GPSR1_0		GPSR2_0		GPSR3_0		GPSR4_0		GPSR5_0		GPSR6_0

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
FM(IP4_15_12)	IP4_15_12	FM(IP5_15_12)	IP5_15_12	FM(IP6_15_12)	IP6_15_12	FM(IP7_15_12)	IP7_15_12 \
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
FM(IP12_31_28)	IP12_31_28	FM(IP13_31_28)	IP13_31_28	FM(IP14_31_28)	IP14_31_28	FM(IP15_31_28)	IP15_31_28

/* The bit numbering in MOD_SEL fields is reversed */
#define REV4(f0, f1, f2, f3)			f0 f2 f1 f3
#define REV8(f0, f1, f2, f3, f4, f5, f6, f7)	f0 f4 f2 f6 f1 f5 f3 f7

/* MOD_SEL0 */			/* 0 */				/* 1 */				/* 2 */				/* 3 */			/* 4 */			/* 5 */		/* 6 */		/* 7 */
#define MOD_SEL0_30_29	   REV4(FM(SEL_ADGB_0),			FM(SEL_ADGB_1),			FM(SEL_ADGB_2),			F_(0, 0))
#define MOD_SEL0_28		FM(SEL_DRIF0_0)			FM(SEL_DRIF0_1)
#define MOD_SEL0_27_26	   REV4(FM(SEL_FM_0),			FM(SEL_FM_1),			FM(SEL_FM_2),			F_(0, 0))
#define MOD_SEL0_25		FM(SEL_FSO_0)			FM(SEL_FSO_1)
#define MOD_SEL0_24		FM(SEL_HSCIF0_0)		FM(SEL_HSCIF0_1)
#define MOD_SEL0_23		FM(SEL_HSCIF1_0)		FM(SEL_HSCIF1_1)
#define MOD_SEL0_22		FM(SEL_HSCIF2_0)		FM(SEL_HSCIF2_1)
#define MOD_SEL0_21_20	   REV4(FM(SEL_I2C1_0),			FM(SEL_I2C1_1),			FM(SEL_I2C1_2),			FM(SEL_I2C1_3))
#define MOD_SEL0_19_18_17  REV8(FM(SEL_I2C2_0),			FM(SEL_I2C2_1),			FM(SEL_I2C2_2),			FM(SEL_I2C2_3),		FM(SEL_I2C2_4),		F_(0, 0),	F_(0, 0),	F_(0, 0))
#define MOD_SEL0_16		FM(SEL_NDF_0)			FM(SEL_NDF_1)
#define MOD_SEL0_15		FM(SEL_PWM0_0)			FM(SEL_PWM0_1)
#define MOD_SEL0_14		FM(SEL_PWM1_0)			FM(SEL_PWM1_1)
#define MOD_SEL0_13_12	   REV4(FM(SEL_PWM2_0),			FM(SEL_PWM2_1),			FM(SEL_PWM2_2),			F_(0, 0))
#define MOD_SEL0_11_10	   REV4(FM(SEL_PWM3_0),			FM(SEL_PWM3_1),			FM(SEL_PWM3_2),			F_(0, 0))
#define MOD_SEL0_9		FM(SEL_PWM4_0)			FM(SEL_PWM4_1)
#define MOD_SEL0_8		FM(SEL_PWM5_0)			FM(SEL_PWM5_1)
#define MOD_SEL0_7		FM(SEL_PWM6_0)			FM(SEL_PWM6_1)
#define MOD_SEL0_6_5	   REV4(FM(SEL_REMOCON_0),		FM(SEL_REMOCON_1),		FM(SEL_REMOCON_2),		F_(0, 0))
#define MOD_SEL0_4		FM(SEL_SCIF_0)			FM(SEL_SCIF_1)
#define MOD_SEL0_3		FM(SEL_SCIF0_0)			FM(SEL_SCIF0_1)
#define MOD_SEL0_2		FM(SEL_SCIF2_0)			FM(SEL_SCIF2_1)
#define MOD_SEL0_1_0	   REV4(FM(SEL_SPEED_PULSE_IF_0),	FM(SEL_SPEED_PULSE_IF_1),	FM(SEL_SPEED_PULSE_IF_2),	F_(0, 0))

/* MOD_SEL1 */			/* 0 */				/* 1 */				/* 2 */				/* 3 */			/* 4 */			/* 5 */		/* 6 */		/* 7 */
#define MOD_SEL1_31		FM(SEL_SIMCARD_0)		FM(SEL_SIMCARD_1)
#define MOD_SEL1_30		FM(SEL_SSI2_0)			FM(SEL_SSI2_1)
#define MOD_SEL1_29		FM(SEL_TIMER_TMU_0)		FM(SEL_TIMER_TMU_1)
#define MOD_SEL1_28		FM(SEL_USB_20_CH0_0)		FM(SEL_USB_20_CH0_1)
#define MOD_SEL1_26		FM(SEL_DRIF2_0)			FM(SEL_DRIF2_1)
#define MOD_SEL1_25		FM(SEL_DRIF3_0)			FM(SEL_DRIF3_1)
#define MOD_SEL1_24_23_22  REV8(FM(SEL_HSCIF3_0),		FM(SEL_HSCIF3_1),		FM(SEL_HSCIF3_2),		FM(SEL_HSCIF3_3),	FM(SEL_HSCIF3_4),	F_(0, 0),	F_(0, 0),	F_(0, 0))
#define MOD_SEL1_21_20_19  REV8(FM(SEL_HSCIF4_0),		FM(SEL_HSCIF4_1),		FM(SEL_HSCIF4_2),		FM(SEL_HSCIF4_3),	FM(SEL_HSCIF4_4),	F_(0, 0),	F_(0, 0),	F_(0, 0))
#define MOD_SEL1_18		FM(SEL_I2C6_0)			FM(SEL_I2C6_1)
#define MOD_SEL1_17		FM(SEL_I2C7_0)			FM(SEL_I2C7_1)
#define MOD_SEL1_16		FM(SEL_MSIOF2_0)		FM(SEL_MSIOF2_1)
#define MOD_SEL1_15		FM(SEL_MSIOF3_0)		FM(SEL_MSIOF3_1)
#define MOD_SEL1_14_13	   REV4(FM(SEL_SCIF3_0),		FM(SEL_SCIF3_1),		FM(SEL_SCIF3_2),		F_(0, 0))
#define MOD_SEL1_12_11	   REV4(FM(SEL_SCIF4_0),		FM(SEL_SCIF4_1),		FM(SEL_SCIF4_2),		F_(0, 0))
#define MOD_SEL1_10_9	   REV4(FM(SEL_SCIF5_0),		FM(SEL_SCIF5_1),		FM(SEL_SCIF5_2),		F_(0, 0))
#define MOD_SEL1_8		FM(SEL_VIN4_0)			FM(SEL_VIN4_1)
#define MOD_SEL1_7		FM(SEL_VIN5_0)			FM(SEL_VIN5_1)
#define MOD_SEL1_6_5	   REV4(FM(SEL_ADGC_0),			FM(SEL_ADGC_1),			FM(SEL_ADGC_2),			F_(0, 0))
#define MOD_SEL1_4		FM(SEL_SSI9_0)			FM(SEL_SSI9_1)

#define PINMUX_MOD_SELS	\
\
			MOD_SEL1_31 \
MOD_SEL0_30_29		MOD_SEL1_30 \
			MOD_SEL1_29 \
MOD_SEL0_28		MOD_SEL1_28 \
MOD_SEL0_27_26 \
			MOD_SEL1_26 \
MOD_SEL0_25		MOD_SEL1_25 \
MOD_SEL0_24		MOD_SEL1_24_23_22 \
MOD_SEL0_23 \
MOD_SEL0_22 \
MOD_SEL0_21_20		MOD_SEL1_21_20_19 \
MOD_SEL0_19_18_17	MOD_SEL1_18 \
			MOD_SEL1_17 \
MOD_SEL0_16		MOD_SEL1_16 \
MOD_SEL0_15		MOD_SEL1_15 \
MOD_SEL0_14		MOD_SEL1_14_13 \
MOD_SEL0_13_12 \
			MOD_SEL1_12_11 \
MOD_SEL0_11_10 \
			MOD_SEL1_10_9 \
MOD_SEL0_9 \
MOD_SEL0_8		MOD_SEL1_8 \
MOD_SEL0_7		MOD_SEL1_7 \
MOD_SEL0_6_5		MOD_SEL1_6_5 \
MOD_SEL0_4		MOD_SEL1_4 \
MOD_SEL0_3 \
MOD_SEL0_2 \
MOD_SEL0_1_0

/*
 * These pins are not able to be muxed but have other properties
 * that can be set, such as pull-up/pull-down enable.
 */
#define PINMUX_STATIC \
	FM(AVB_TX_CTL) FM(AVB_TXC) FM(AVB_TD0) FM(AVB_TD1) FM(AVB_TD2) \
	FM(AVB_TD3) \
	FM(PRESETOUT_N) FM(FSCLKST_N) FM(TRST_N) FM(TCK) FM(TMS) FM(TDI) \
	FM(ASEBRK) \
	FM(MLB_REF)

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
	PINMUX_MARK_END,
#undef F_
#undef FM
};

static const u16 pinmux_data[] = {
	PINMUX_DATA_GP_ALL(),

	PINMUX_SINGLE(CLKOUT),
	PINMUX_SINGLE(AVB_PHY_INT),
	PINMUX_SINGLE(AVB_RD3),
	PINMUX_SINGLE(AVB_RXC),
	PINMUX_SINGLE(AVB_RX_CTL),
	PINMUX_SINGLE(QSPI0_SSL),

	/* IPSR0 */
	PINMUX_IPSR_GPSR(IP0_3_0,		QSPI0_SPCLK),
	PINMUX_IPSR_MSEL(IP0_3_0,		HSCK4_A,	SEL_HSCIF4_0),

	PINMUX_IPSR_GPSR(IP0_7_4,		QSPI0_MOSI_IO0),
	PINMUX_IPSR_MSEL(IP0_7_4,		HCTS4_N_A,	SEL_HSCIF4_0),

	PINMUX_IPSR_GPSR(IP0_11_8,		QSPI0_MISO_IO1),
	PINMUX_IPSR_MSEL(IP0_11_8,		HRTS4_N_A,	SEL_HSCIF4_0),

	PINMUX_IPSR_GPSR(IP0_15_12,		QSPI0_IO2),
	PINMUX_IPSR_GPSR(IP0_15_12,		HTX4_A),

	PINMUX_IPSR_GPSR(IP0_19_16,		QSPI0_IO3),
	PINMUX_IPSR_MSEL(IP0_19_16,		HRX4_A,		SEL_HSCIF4_0),

	PINMUX_IPSR_GPSR(IP0_23_20,		QSPI1_SPCLK),
	PINMUX_IPSR_MSEL(IP0_23_20,		RIF2_CLK_A,	SEL_DRIF2_0),
	PINMUX_IPSR_MSEL(IP0_23_20,		HSCK4_B,	SEL_HSCIF4_1),
	PINMUX_IPSR_MSEL(IP0_23_20,		VI4_DATA0_A,	SEL_VIN4_0),

	PINMUX_IPSR_GPSR(IP0_27_24,		QSPI1_MOSI_IO0),
	PINMUX_IPSR_MSEL(IP0_27_24,		RIF2_SYNC_A,	SEL_DRIF2_0),
	PINMUX_IPSR_GPSR(IP0_27_24,		HTX4_B),
	PINMUX_IPSR_MSEL(IP0_27_24,		VI4_DATA1_A,	SEL_VIN4_0),

	PINMUX_IPSR_GPSR(IP0_31_28,		QSPI1_MISO_IO1),
	PINMUX_IPSR_MSEL(IP0_31_28,		RIF2_D0_A,	SEL_DRIF2_0),
	PINMUX_IPSR_MSEL(IP0_31_28,		HRX4_B,		SEL_HSCIF4_1),
	PINMUX_IPSR_MSEL(IP0_31_28,		VI4_DATA2_A,	SEL_VIN4_0),

	/* IPSR1 */
	PINMUX_IPSR_GPSR(IP1_3_0,		QSPI1_IO2),
	PINMUX_IPSR_MSEL(IP1_3_0,		RIF2_D1_A,	SEL_DRIF2_0),
	PINMUX_IPSR_GPSR(IP1_3_0,		HTX3_C),
	PINMUX_IPSR_MSEL(IP1_3_0,		VI4_DATA3_A,	SEL_VIN4_0),

	PINMUX_IPSR_GPSR(IP1_7_4,		QSPI1_IO3),
	PINMUX_IPSR_MSEL(IP1_7_4,		RIF3_CLK_A,	SEL_DRIF3_0),
	PINMUX_IPSR_MSEL(IP1_7_4,		HRX3_C,		SEL_HSCIF3_2),
	PINMUX_IPSR_MSEL(IP1_7_4,		VI4_DATA4_A,	SEL_VIN4_0),

	PINMUX_IPSR_GPSR(IP1_11_8,		QSPI1_SSL),
	PINMUX_IPSR_MSEL(IP1_11_8,		RIF3_SYNC_A,	SEL_DRIF3_0),
	PINMUX_IPSR_MSEL(IP1_11_8,		HSCK3_C,	SEL_HSCIF3_2),
	PINMUX_IPSR_MSEL(IP1_11_8,		VI4_DATA5_A,	SEL_VIN4_0),

	PINMUX_IPSR_GPSR(IP1_15_12,		RPC_INT_N),
	PINMUX_IPSR_MSEL(IP1_15_12,		RIF3_D0_A,	SEL_DRIF3_0),
	PINMUX_IPSR_MSEL(IP1_15_12,		HCTS3_N_C,	SEL_HSCIF3_2),
	PINMUX_IPSR_MSEL(IP1_15_12,		VI4_DATA6_A,	SEL_VIN4_0),

	PINMUX_IPSR_GPSR(IP1_19_16,		RPC_RESET_N),
	PINMUX_IPSR_MSEL(IP1_19_16,		RIF3_D1_A,	SEL_DRIF3_0),
	PINMUX_IPSR_MSEL(IP1_19_16,		HRTS3_N_C,	SEL_HSCIF3_2),
	PINMUX_IPSR_MSEL(IP1_19_16,		VI4_DATA7_A,	SEL_VIN4_0),

	PINMUX_IPSR_GPSR(IP1_23_20,		AVB_RD0),

	PINMUX_IPSR_GPSR(IP1_27_24,		AVB_RD1),

	PINMUX_IPSR_GPSR(IP1_31_28,		AVB_RD2),

	/* IPSR2 */
	PINMUX_IPSR_GPSR(IP2_3_0,		AVB_TXCREFCLK),

	PINMUX_IPSR_GPSR(IP2_7_4,		AVB_MDIO),

	PINMUX_IPSR_GPSR(IP2_11_8,		AVB_MDC),

	PINMUX_IPSR_GPSR(IP2_15_12,		BS_N),
	PINMUX_IPSR_MSEL(IP2_15_12,		PWM0_A,		SEL_PWM0_0),
	PINMUX_IPSR_GPSR(IP2_15_12,		AVB_MAGIC),
	PINMUX_IPSR_GPSR(IP2_15_12,		VI4_CLK),
	PINMUX_IPSR_GPSR(IP2_15_12,		TX3_C),
	PINMUX_IPSR_MSEL(IP2_15_12,		VI5_CLK_B,	SEL_VIN5_1),

	PINMUX_IPSR_GPSR(IP2_19_16,		RD_N),
	PINMUX_IPSR_MSEL(IP2_19_16,		PWM1_A,		SEL_PWM1_0),
	PINMUX_IPSR_GPSR(IP2_19_16,		AVB_LINK),
	PINMUX_IPSR_GPSR(IP2_19_16,		VI4_FIELD),
	PINMUX_IPSR_MSEL(IP2_19_16,		RX3_C,		SEL_SCIF3_2),
	PINMUX_IPSR_GPSR(IP2_19_16,		FSCLKST2_N_A),
	PINMUX_IPSR_MSEL(IP2_19_16,		VI5_DATA0_B,	SEL_VIN5_1),

	PINMUX_IPSR_GPSR(IP2_23_20,		RD_WR_N),
	PINMUX_IPSR_MSEL(IP2_23_20,		SCL7_A,		SEL_I2C7_0),
	PINMUX_IPSR_GPSR(IP2_23_20,		AVB_AVTP_MATCH),
	PINMUX_IPSR_GPSR(IP2_23_20,		VI4_VSYNC_N),
	PINMUX_IPSR_GPSR(IP2_23_20,		TX5_B),
	PINMUX_IPSR_MSEL(IP2_23_20,		SCK3_C,		SEL_SCIF3_2),
	PINMUX_IPSR_MSEL(IP2_23_20,		PWM5_A,		SEL_PWM5_0),

	PINMUX_IPSR_GPSR(IP2_27_24,		EX_WAIT0),
	PINMUX_IPSR_MSEL(IP2_27_24,		SDA7_A,		SEL_I2C7_0),
	PINMUX_IPSR_GPSR(IP2_27_24,		AVB_AVTP_CAPTURE),
	PINMUX_IPSR_GPSR(IP2_27_24,		VI4_HSYNC_N),
	PINMUX_IPSR_MSEL(IP2_27_24,		RX5_B,		SEL_SCIF5_1),
	PINMUX_IPSR_MSEL(IP2_27_24,		PWM6_A,		SEL_PWM6_0),

	PINMUX_IPSR_GPSR(IP2_31_28,		A0),
	PINMUX_IPSR_GPSR(IP2_31_28,		IRQ0),
	PINMUX_IPSR_MSEL(IP2_31_28,		PWM2_A,		SEL_PWM2_0),
	PINMUX_IPSR_MSEL(IP2_31_28,		MSIOF3_SS1_B,	SEL_MSIOF3_1),
	PINMUX_IPSR_MSEL(IP2_31_28,		VI5_CLK_A,	SEL_VIN5_0),
	PINMUX_IPSR_GPSR(IP2_31_28,		DU_CDE),
	PINMUX_IPSR_MSEL(IP2_31_28,		HRX3_D,		SEL_HSCIF3_3),
	PINMUX_IPSR_GPSR(IP2_31_28,		IERX),
	PINMUX_IPSR_GPSR(IP2_31_28,		QSTB_QHE),

	/* IPSR3 */
	PINMUX_IPSR_GPSR(IP3_3_0,		A1),
	PINMUX_IPSR_GPSR(IP3_3_0,		IRQ1),
	PINMUX_IPSR_MSEL(IP3_3_0,		PWM3_A,		SEL_PWM3_0),
	PINMUX_IPSR_GPSR(IP3_3_0,		DU_DOTCLKIN1),
	PINMUX_IPSR_MSEL(IP3_3_0,		VI5_DATA0_A,	SEL_VIN5_0),
	PINMUX_IPSR_GPSR(IP3_3_0,		DU_DISP_CDE),
	PINMUX_IPSR_MSEL(IP3_3_0,		SDA6_B,		SEL_I2C6_1),
	PINMUX_IPSR_GPSR(IP3_3_0,		IETX),
	PINMUX_IPSR_GPSR(IP3_3_0,		QCPV_QDE),

	PINMUX_IPSR_GPSR(IP3_7_4,		A2),
	PINMUX_IPSR_GPSR(IP3_7_4,		IRQ2),
	PINMUX_IPSR_GPSR(IP3_7_4,		AVB_AVTP_PPS),
	PINMUX_IPSR_GPSR(IP3_7_4,		VI4_CLKENB),
	PINMUX_IPSR_MSEL(IP3_7_4,		VI5_DATA1_A,	SEL_VIN5_0),
	PINMUX_IPSR_GPSR(IP3_7_4,		DU_DISP),
	PINMUX_IPSR_MSEL(IP3_7_4,		SCL6_B,		SEL_I2C6_1),
	PINMUX_IPSR_GPSR(IP3_7_4,		QSTVB_QVE),

	PINMUX_IPSR_GPSR(IP3_11_8,		A3),
	PINMUX_IPSR_MSEL(IP3_11_8,		CTS4_N_A,	SEL_SCIF4_0),
	PINMUX_IPSR_MSEL(IP3_11_8,		PWM4_A,		SEL_PWM4_0),
	PINMUX_IPSR_GPSR(IP3_11_8,		VI4_DATA12),
	PINMUX_IPSR_GPSR(IP3_11_8,		DU_DOTCLKOUT0),
	PINMUX_IPSR_GPSR(IP3_11_8,		HTX3_D),
	PINMUX_IPSR_GPSR(IP3_11_8,		IECLK),
	PINMUX_IPSR_GPSR(IP3_11_8,		LCDOUT12),

	PINMUX_IPSR_GPSR(IP3_15_12,		A4),
	PINMUX_IPSR_MSEL(IP3_15_12,		RTS4_N_A,	SEL_SCIF4_0),
	PINMUX_IPSR_MSEL(IP3_15_12,		MSIOF3_SYNC_B,	SEL_MSIOF3_1),
	PINMUX_IPSR_GPSR(IP3_15_12,		VI4_DATA8),
	PINMUX_IPSR_MSEL(IP3_15_12,		PWM2_B,		SEL_PWM2_1),
	PINMUX_IPSR_GPSR(IP3_15_12,		DU_DG4),
	PINMUX_IPSR_MSEL(IP3_15_12,		RIF2_CLK_B,	SEL_DRIF2_1),

	PINMUX_IPSR_GPSR(IP3_19_16,		A5),
	PINMUX_IPSR_MSEL(IP3_19_16,		SCK4_A,		SEL_SCIF4_0),
	PINMUX_IPSR_MSEL(IP3_19_16,		MSIOF3_SCK_B,	SEL_MSIOF3_1),
	PINMUX_IPSR_GPSR(IP3_19_16,		VI4_DATA9),
	PINMUX_IPSR_MSEL(IP3_19_16,		PWM3_B,		SEL_PWM3_1),
	PINMUX_IPSR_MSEL(IP3_19_16,		RIF2_SYNC_B,	SEL_DRIF2_1),
	PINMUX_IPSR_GPSR(IP3_19_16,		QPOLA),

	PINMUX_IPSR_GPSR(IP3_23_20,		A6),
	PINMUX_IPSR_MSEL(IP3_23_20,		RX4_A,		SEL_SCIF4_0),
	PINMUX_IPSR_MSEL(IP3_23_20,		MSIOF3_RXD_B,	SEL_MSIOF3_1),
	PINMUX_IPSR_GPSR(IP3_23_20,		VI4_DATA10),
	PINMUX_IPSR_MSEL(IP3_23_20,		RIF2_D0_B,	SEL_DRIF2_1),

	PINMUX_IPSR_GPSR(IP3_27_24,		A7),
	PINMUX_IPSR_GPSR(IP3_27_24,		TX4_A),
	PINMUX_IPSR_GPSR(IP3_27_24,		MSIOF3_TXD_B),
	PINMUX_IPSR_GPSR(IP3_27_24,		VI4_DATA11),
	PINMUX_IPSR_MSEL(IP3_27_24,		RIF2_D1_B,	SEL_DRIF2_1),

	PINMUX_IPSR_GPSR(IP3_31_28,		A8),
	PINMUX_IPSR_MSEL(IP3_31_28,		SDA6_A,		SEL_I2C6_0),
	PINMUX_IPSR_MSEL(IP3_31_28,		RX3_B,		SEL_SCIF3_1),
	PINMUX_IPSR_MSEL(IP3_31_28,		HRX4_C,		SEL_HSCIF4_2),
	PINMUX_IPSR_MSEL(IP3_31_28,		VI5_HSYNC_N_A,	SEL_VIN5_0),
	PINMUX_IPSR_GPSR(IP3_31_28,		DU_HSYNC),
	PINMUX_IPSR_MSEL(IP3_31_28,		VI4_DATA0_B,	SEL_VIN4_1),
	PINMUX_IPSR_GPSR(IP3_31_28,		QSTH_QHS),

	/* IPSR4 */
	PINMUX_IPSR_GPSR(IP4_3_0,		A9),
	PINMUX_IPSR_GPSR(IP4_3_0,		TX5_A),
	PINMUX_IPSR_GPSR(IP4_3_0,		IRQ3),
	PINMUX_IPSR_GPSR(IP4_3_0,		VI4_DATA16),
	PINMUX_IPSR_MSEL(IP4_3_0,		VI5_VSYNC_N_A,	SEL_VIN5_0),
	PINMUX_IPSR_GPSR(IP4_3_0,		DU_DG7),
	PINMUX_IPSR_GPSR(IP4_3_0,		LCDOUT15),

	PINMUX_IPSR_GPSR(IP4_7_4,		A10),
	PINMUX_IPSR_GPSR(IP4_7_4,		IRQ4),
	PINMUX_IPSR_MSEL(IP4_7_4,		MSIOF2_SYNC_B,	SEL_MSIOF2_1),
	PINMUX_IPSR_GPSR(IP4_7_4,		VI4_DATA13),
	PINMUX_IPSR_MSEL(IP4_7_4,		VI5_FIELD_A,	SEL_VIN5_0),
	PINMUX_IPSR_GPSR(IP4_7_4,		DU_DG5),
	PINMUX_IPSR_GPSR(IP4_7_4,		FSCLKST2_N_B),
	PINMUX_IPSR_GPSR(IP4_7_4,		LCDOUT13),

	PINMUX_IPSR_GPSR(IP4_11_8,		A11),
	PINMUX_IPSR_MSEL(IP4_11_8,		SCL6_A,		SEL_I2C6_0),
	PINMUX_IPSR_GPSR(IP4_11_8,		TX3_B),
	PINMUX_IPSR_GPSR(IP4_11_8,		HTX4_C),
	PINMUX_IPSR_GPSR(IP4_11_8,		DU_VSYNC),
	PINMUX_IPSR_MSEL(IP4_11_8,		VI4_DATA1_B,	SEL_VIN4_1),
	PINMUX_IPSR_GPSR(IP4_11_8,		QSTVA_QVS),

	PINMUX_IPSR_GPSR(IP4_15_12,		A12),
	PINMUX_IPSR_MSEL(IP4_15_12,		RX5_A,		SEL_SCIF5_0),
	PINMUX_IPSR_GPSR(IP4_15_12,		MSIOF2_SS2_B),
	PINMUX_IPSR_GPSR(IP4_15_12,		VI4_DATA17),
	PINMUX_IPSR_MSEL(IP4_15_12,		VI5_DATA3_A,	SEL_VIN5_0),
	PINMUX_IPSR_GPSR(IP4_15_12,		DU_DG6),
	PINMUX_IPSR_GPSR(IP4_15_12,		LCDOUT14),

	PINMUX_IPSR_GPSR(IP4_19_16,		A13),
	PINMUX_IPSR_MSEL(IP4_19_16,		SCK5_A,		SEL_SCIF5_0),
	PINMUX_IPSR_MSEL(IP4_19_16,		MSIOF2_SCK_B,	SEL_MSIOF2_1),
	PINMUX_IPSR_GPSR(IP4_19_16,		VI4_DATA14),
	PINMUX_IPSR_MSEL(IP4_19_16,		HRX4_D,		SEL_HSCIF4_3),
	PINMUX_IPSR_GPSR(IP4_19_16,		DU_DB2),
	PINMUX_IPSR_GPSR(IP4_19_16,		LCDOUT2),

	PINMUX_IPSR_GPSR(IP4_23_20,		A14),
	PINMUX_IPSR_GPSR(IP4_23_20,		MSIOF1_SS1),
	PINMUX_IPSR_MSEL(IP4_23_20,		MSIOF2_RXD_B,	SEL_MSIOF2_1),
	PINMUX_IPSR_GPSR(IP4_23_20,		VI4_DATA15),
	PINMUX_IPSR_GPSR(IP4_23_20,		HTX4_D),
	PINMUX_IPSR_GPSR(IP4_23_20,		DU_DB3),
	PINMUX_IPSR_GPSR(IP4_23_20,		LCDOUT3),

	PINMUX_IPSR_GPSR(IP4_27_24,		A15),
	PINMUX_IPSR_GPSR(IP4_27_24,		MSIOF1_SS2),
	PINMUX_IPSR_GPSR(IP4_27_24,		MSIOF2_TXD_B),
	PINMUX_IPSR_GPSR(IP4_27_24,		VI4_DATA18),
	PINMUX_IPSR_MSEL(IP4_27_24,		VI5_DATA4_A,	SEL_VIN5_0),
	PINMUX_IPSR_GPSR(IP4_27_24,		DU_DB4),
	PINMUX_IPSR_GPSR(IP4_27_24,		LCDOUT4),

	PINMUX_IPSR_GPSR(IP4_31_28,		A16),
	PINMUX_IPSR_GPSR(IP4_31_28,		MSIOF1_SYNC),
	PINMUX_IPSR_GPSR(IP4_31_28,		MSIOF2_SS1_B),
	PINMUX_IPSR_GPSR(IP4_31_28,		VI4_DATA19),
	PINMUX_IPSR_MSEL(IP4_31_28,		VI5_DATA5_A,	SEL_VIN5_0),
	PINMUX_IPSR_GPSR(IP4_31_28,		DU_DB5),
	PINMUX_IPSR_GPSR(IP4_31_28,		LCDOUT5),

	/* IPSR5 */
	PINMUX_IPSR_GPSR(IP5_3_0,		A17),
	PINMUX_IPSR_GPSR(IP5_3_0,		MSIOF1_RXD),
	PINMUX_IPSR_GPSR(IP5_3_0,		VI4_DATA20),
	PINMUX_IPSR_MSEL(IP5_3_0,		VI5_DATA6_A,	SEL_VIN5_0),
	PINMUX_IPSR_GPSR(IP5_3_0,		DU_DB6),
	PINMUX_IPSR_GPSR(IP5_3_0,		LCDOUT6),

	PINMUX_IPSR_GPSR(IP5_7_4,		A18),
	PINMUX_IPSR_GPSR(IP5_7_4,		MSIOF1_TXD),
	PINMUX_IPSR_GPSR(IP5_7_4,		VI4_DATA21),
	PINMUX_IPSR_MSEL(IP5_7_4,		VI5_DATA7_A,	SEL_VIN5_0),
	PINMUX_IPSR_GPSR(IP5_7_4,		DU_DB0),
	PINMUX_IPSR_MSEL(IP5_7_4,		HRX4_E,		SEL_HSCIF4_4),
	PINMUX_IPSR_GPSR(IP5_7_4,		LCDOUT0),

	PINMUX_IPSR_GPSR(IP5_11_8,		A19),
	PINMUX_IPSR_GPSR(IP5_11_8,		MSIOF1_SCK),
	PINMUX_IPSR_GPSR(IP5_11_8,		VI4_DATA22),
	PINMUX_IPSR_MSEL(IP5_11_8,		VI5_DATA2_A,	SEL_VIN5_0),
	PINMUX_IPSR_GPSR(IP5_11_8,		DU_DB1),
	PINMUX_IPSR_GPSR(IP5_11_8,		HTX4_E),
	PINMUX_IPSR_GPSR(IP5_11_8,		LCDOUT1),

	PINMUX_IPSR_GPSR(IP5_15_12,		CS0_N),
	PINMUX_IPSR_GPSR(IP5_15_12,		SCL5),
	PINMUX_IPSR_GPSR(IP5_15_12,		DU_DR0),
	PINMUX_IPSR_MSEL(IP5_15_12,		VI4_DATA2_B,	SEL_VIN4_1),
	PINMUX_IPSR_GPSR(IP5_15_12,		LCDOUT16),

	PINMUX_IPSR_GPSR(IP5_19_16,		WE0_N),
	PINMUX_IPSR_GPSR(IP5_19_16,		SDA5),
	PINMUX_IPSR_GPSR(IP5_19_16,		DU_DR1),
	PINMUX_IPSR_MSEL(IP5_19_16,		VI4_DATA3_B,	SEL_VIN4_1),
	PINMUX_IPSR_GPSR(IP5_19_16,		LCDOUT17),

	PINMUX_IPSR_GPSR(IP5_23_20,		D0),
	PINMUX_IPSR_MSEL(IP5_23_20,		MSIOF3_SCK_A,	SEL_MSIOF3_0),
	PINMUX_IPSR_GPSR(IP5_23_20,		DU_DR2),
	PINMUX_IPSR_MSEL(IP5_23_20,		CTS4_N_C,	SEL_SCIF4_2),
	PINMUX_IPSR_GPSR(IP5_23_20,		LCDOUT18),

	PINMUX_IPSR_GPSR(IP5_27_24,		D1),
	PINMUX_IPSR_MSEL(IP5_27_24,		MSIOF3_SYNC_A,	SEL_MSIOF3_0),
	PINMUX_IPSR_MSEL(IP5_27_24,		SCK3_A,		SEL_SCIF3_0),
	PINMUX_IPSR_GPSR(IP5_27_24,		VI4_DATA23),
	PINMUX_IPSR_MSEL(IP5_27_24,		VI5_CLKENB_A,	SEL_VIN5_0),
	PINMUX_IPSR_GPSR(IP5_27_24,		DU_DB7),
	PINMUX_IPSR_MSEL(IP5_27_24,		RTS4_N_C,	SEL_SCIF4_2),
	PINMUX_IPSR_GPSR(IP5_27_24,		LCDOUT7),

	PINMUX_IPSR_GPSR(IP5_31_28,		D2),
	PINMUX_IPSR_MSEL(IP5_31_28,		MSIOF3_RXD_A,	SEL_MSIOF3_0),
	PINMUX_IPSR_MSEL(IP5_31_28,		RX5_C,		SEL_SCIF5_2),
	PINMUX_IPSR_MSEL(IP5_31_28,		VI5_DATA14_A,	SEL_VIN5_0),
	PINMUX_IPSR_GPSR(IP5_31_28,		DU_DR3),
	PINMUX_IPSR_MSEL(IP5_31_28,		RX4_C,		SEL_SCIF4_2),
	PINMUX_IPSR_GPSR(IP5_31_28,		LCDOUT19),

	/* IPSR6 */
	PINMUX_IPSR_GPSR(IP6_3_0,		D3),
	PINMUX_IPSR_GPSR(IP6_3_0,		MSIOF3_TXD_A),
	PINMUX_IPSR_GPSR(IP6_3_0,		TX5_C),
	PINMUX_IPSR_MSEL(IP6_3_0,		VI5_DATA15_A,	SEL_VIN5_0),
	PINMUX_IPSR_GPSR(IP6_3_0,		DU_DR4),
	PINMUX_IPSR_GPSR(IP6_3_0,		TX4_C),
	PINMUX_IPSR_GPSR(IP6_3_0,		LCDOUT20),

	PINMUX_IPSR_GPSR(IP6_7_4,		D4),
	PINMUX_IPSR_GPSR(IP6_7_4,		CANFD1_TX),
	PINMUX_IPSR_MSEL(IP6_7_4,		HSCK3_B,	SEL_HSCIF3_1),
	PINMUX_IPSR_GPSR(IP6_7_4,		CAN1_TX),
	PINMUX_IPSR_MSEL(IP6_7_4,		RTS3_N_A,	SEL_SCIF3_0),
	PINMUX_IPSR_GPSR(IP6_7_4,		MSIOF3_SS2_A),
	PINMUX_IPSR_MSEL(IP6_7_4,		VI5_DATA1_B,	SEL_VIN5_1),

	PINMUX_IPSR_GPSR(IP6_11_8,		D5),
	PINMUX_IPSR_MSEL(IP6_11_8,		RX3_A,		SEL_SCIF3_0),
	PINMUX_IPSR_MSEL(IP6_11_8,		HRX3_B,		SEL_HSCIF3_1),
	PINMUX_IPSR_GPSR(IP6_11_8,		DU_DR5),
	PINMUX_IPSR_MSEL(IP6_11_8,		VI4_DATA4_B,	SEL_VIN4_1),
	PINMUX_IPSR_GPSR(IP6_11_8,		LCDOUT21),

	PINMUX_IPSR_GPSR(IP6_15_12,		D6),
	PINMUX_IPSR_GPSR(IP6_15_12,		TX3_A),
	PINMUX_IPSR_GPSR(IP6_15_12,		HTX3_B),
	PINMUX_IPSR_GPSR(IP6_15_12,		DU_DR6),
	PINMUX_IPSR_MSEL(IP6_15_12,		VI4_DATA5_B,	SEL_VIN4_1),
	PINMUX_IPSR_GPSR(IP6_15_12,		LCDOUT22),

	PINMUX_IPSR_GPSR(IP6_19_16,		D7),
	PINMUX_IPSR_GPSR(IP6_19_16,		CANFD1_RX),
	PINMUX_IPSR_GPSR(IP6_19_16,		IRQ5),
	PINMUX_IPSR_GPSR(IP6_19_16,		CAN1_RX),
	PINMUX_IPSR_MSEL(IP6_19_16,		CTS3_N_A,	SEL_SCIF3_0),
	PINMUX_IPSR_MSEL(IP6_19_16,		VI5_DATA2_B,	SEL_VIN5_1),

	PINMUX_IPSR_GPSR(IP6_23_20,		D8),
	PINMUX_IPSR_MSEL(IP6_23_20,		MSIOF2_SCK_A,	SEL_MSIOF2_0),
	PINMUX_IPSR_MSEL(IP6_23_20,		SCK4_B,		SEL_SCIF4_1),
	PINMUX_IPSR_MSEL(IP6_23_20,		VI5_DATA12_A,	SEL_VIN5_0),
	PINMUX_IPSR_GPSR(IP6_23_20,		DU_DR7),
	PINMUX_IPSR_MSEL(IP6_23_20,		RIF3_CLK_B,	SEL_DRIF3_1),
	PINMUX_IPSR_MSEL(IP6_23_20,		HCTS3_N_E,	SEL_HSCIF3_4),
	PINMUX_IPSR_GPSR(IP6_23_20,		LCDOUT23),

	PINMUX_IPSR_GPSR(IP6_27_24,		D9),
	PINMUX_IPSR_MSEL(IP6_27_24,		MSIOF2_SYNC_A,	SEL_MSIOF2_0),
	PINMUX_IPSR_MSEL(IP6_27_24,		VI5_DATA10_A,	SEL_VIN5_0),
	PINMUX_IPSR_GPSR(IP6_27_24,		DU_DG0),
	PINMUX_IPSR_MSEL(IP6_27_24,		RIF3_SYNC_B,	SEL_DRIF3_1),
	PINMUX_IPSR_MSEL(IP6_27_24,		HRX3_E,		SEL_HSCIF3_4),
	PINMUX_IPSR_GPSR(IP6_27_24,		LCDOUT8),

	PINMUX_IPSR_GPSR(IP6_31_28,		D10),
	PINMUX_IPSR_MSEL(IP6_31_28,		MSIOF2_RXD_A,	SEL_MSIOF2_0),
	PINMUX_IPSR_MSEL(IP6_31_28,		VI5_DATA13_A,	SEL_VIN5_0),
	PINMUX_IPSR_GPSR(IP6_31_28,		DU_DG1),
	PINMUX_IPSR_MSEL(IP6_31_28,		RIF3_D0_B,	SEL_DRIF3_1),
	PINMUX_IPSR_GPSR(IP6_31_28,		HTX3_E),
	PINMUX_IPSR_GPSR(IP6_31_28,		LCDOUT9),

	/* IPSR7 */
	PINMUX_IPSR_GPSR(IP7_3_0,		D11),
	PINMUX_IPSR_GPSR(IP7_3_0,		MSIOF2_TXD_A),
	PINMUX_IPSR_MSEL(IP7_3_0,		VI5_DATA11_A,	SEL_VIN5_0),
	PINMUX_IPSR_GPSR(IP7_3_0,		DU_DG2),
	PINMUX_IPSR_MSEL(IP7_3_0,		RIF3_D1_B,	SEL_DRIF3_1),
	PINMUX_IPSR_MSEL(IP7_3_0,		HRTS3_N_E,	SEL_HSCIF3_4),
	PINMUX_IPSR_GPSR(IP7_3_0,		LCDOUT10),

	PINMUX_IPSR_GPSR(IP7_7_4,		D12),
	PINMUX_IPSR_GPSR(IP7_7_4,		CANFD0_TX),
	PINMUX_IPSR_GPSR(IP7_7_4,		TX4_B),
	PINMUX_IPSR_GPSR(IP7_7_4,		CAN0_TX),
	PINMUX_IPSR_MSEL(IP7_7_4,		VI5_DATA8_A,	SEL_VIN5_0),
	PINMUX_IPSR_MSEL(IP7_7_4,		VI5_DATA3_B,	SEL_VIN5_1),

	PINMUX_IPSR_GPSR(IP7_11_8,		D13),
	PINMUX_IPSR_GPSR(IP7_11_8,		CANFD0_RX),
	PINMUX_IPSR_MSEL(IP7_11_8,		RX4_B,		SEL_SCIF4_1),
	PINMUX_IPSR_GPSR(IP7_11_8,		CAN0_RX),
	PINMUX_IPSR_MSEL(IP7_11_8,		VI5_DATA9_A,	SEL_VIN5_0),
	PINMUX_IPSR_MSEL(IP7_11_8,		SCL7_B,		SEL_I2C7_1),
	PINMUX_IPSR_MSEL(IP7_11_8,		VI5_DATA4_B,	SEL_VIN5_1),

	PINMUX_IPSR_GPSR(IP7_15_12,		D14),
	PINMUX_IPSR_GPSR(IP7_15_12,		CAN_CLK),
	PINMUX_IPSR_MSEL(IP7_15_12,		HRX3_A,		SEL_HSCIF3_0),
	PINMUX_IPSR_GPSR(IP7_15_12,		MSIOF2_SS2_A),
	PINMUX_IPSR_MSEL(IP7_15_12,		SDA7_B,		SEL_I2C7_1),
	PINMUX_IPSR_MSEL(IP7_15_12,		VI5_DATA5_B,	SEL_VIN5_1),

	PINMUX_IPSR_GPSR(IP7_19_16,		D15),
	PINMUX_IPSR_GPSR(IP7_19_16,		MSIOF2_SS1_A),
	PINMUX_IPSR_GPSR(IP7_19_16,		HTX3_A),
	PINMUX_IPSR_GPSR(IP7_19_16,		MSIOF3_SS1_A),
	PINMUX_IPSR_GPSR(IP7_19_16,		DU_DG3),
	PINMUX_IPSR_GPSR(IP7_19_16,		LCDOUT11),

	PINMUX_IPSR_GPSR(IP7_23_20,		SCL4),
	PINMUX_IPSR_GPSR(IP7_23_20,		CS1_N_A26),
	PINMUX_IPSR_GPSR(IP7_23_20,		DU_DOTCLKIN0),
	PINMUX_IPSR_MSEL(IP7_23_20,		VI4_DATA6_B,	SEL_VIN4_1),
	PINMUX_IPSR_MSEL(IP7_23_20,		VI5_DATA6_B,	SEL_VIN5_1),
	PINMUX_IPSR_GPSR(IP7_23_20,		QCLK),

	PINMUX_IPSR_GPSR(IP7_27_24,		SDA4),
	PINMUX_IPSR_GPSR(IP7_27_24,		WE1_N),
	PINMUX_IPSR_MSEL(IP7_27_24,		VI4_DATA7_B,	SEL_VIN4_1),
	PINMUX_IPSR_MSEL(IP7_27_24,		VI5_DATA7_B,	SEL_VIN5_1),
	PINMUX_IPSR_GPSR(IP7_27_24,		QPOLB),

	PINMUX_IPSR_GPSR(IP7_31_28,		SD0_CLK),
	PINMUX_IPSR_GPSR(IP7_31_28,		NFDATA8),
	PINMUX_IPSR_MSEL(IP7_31_28,		SCL1_C,		SEL_I2C1_2),
	PINMUX_IPSR_MSEL(IP7_31_28,		HSCK1_B,	SEL_HSCIF1_1),
	PINMUX_IPSR_MSEL(IP7_31_28,		SDA2_E,		SEL_I2C2_4),
	PINMUX_IPSR_MSEL(IP7_31_28,		FMCLK_B,	SEL_FM_1),

	/* IPSR8 */
	PINMUX_IPSR_GPSR(IP8_3_0,		SD0_CMD),
	PINMUX_IPSR_GPSR(IP8_3_0,		NFDATA9),
	PINMUX_IPSR_MSEL(IP8_3_0,		HRX1_B,		SEL_HSCIF1_1),
	PINMUX_IPSR_MSEL(IP8_3_0,		SPEEDIN_B,	SEL_SPEED_PULSE_IF_1),

	PINMUX_IPSR_GPSR(IP8_7_4,		SD0_DAT0),
	PINMUX_IPSR_GPSR(IP8_7_4,		NFDATA10),
	PINMUX_IPSR_GPSR(IP8_7_4,		HTX1_B),
	PINMUX_IPSR_MSEL(IP8_7_4,		REMOCON_B,	SEL_REMOCON_1),

	PINMUX_IPSR_GPSR(IP8_11_8,		SD0_DAT1),
	PINMUX_IPSR_GPSR(IP8_11_8,		NFDATA11),
	PINMUX_IPSR_MSEL(IP8_11_8,		SDA2_C,		SEL_I2C2_2),
	PINMUX_IPSR_MSEL(IP8_11_8,		HCTS1_N_B,	SEL_HSCIF1_1),
	PINMUX_IPSR_MSEL(IP8_11_8,		FMIN_B,		SEL_FM_1),

	PINMUX_IPSR_GPSR(IP8_15_12,		SD0_DAT2),
	PINMUX_IPSR_GPSR(IP8_15_12,		NFDATA12),
	PINMUX_IPSR_MSEL(IP8_15_12,		SCL2_C,		SEL_I2C2_2),
	PINMUX_IPSR_MSEL(IP8_15_12,		HRTS1_N_B,	SEL_HSCIF1_1),
	PINMUX_IPSR_GPSR(IP8_15_12,		BPFCLK_B),

	PINMUX_IPSR_GPSR(IP8_19_16,		SD0_DAT3),
	PINMUX_IPSR_GPSR(IP8_19_16,		NFDATA13),
	PINMUX_IPSR_MSEL(IP8_19_16,		SDA1_C,		SEL_I2C1_2),
	PINMUX_IPSR_MSEL(IP8_19_16,		SCL2_E,		SEL_I2C2_4),
	PINMUX_IPSR_MSEL(IP8_19_16,		SPEEDIN_C,	SEL_SPEED_PULSE_IF_2),
	PINMUX_IPSR_MSEL(IP8_19_16,		REMOCON_C,	SEL_REMOCON_2),

	PINMUX_IPSR_GPSR(IP8_23_20,		SD1_CLK),
	PINMUX_IPSR_MSEL(IP8_23_20,		NFDATA14_B,	SEL_NDF_1),

	PINMUX_IPSR_GPSR(IP8_27_24,		SD1_CMD),
	PINMUX_IPSR_MSEL(IP8_27_24,		NFDATA15_B,	SEL_NDF_1),

	PINMUX_IPSR_GPSR(IP8_31_28,		SD1_DAT0),
	PINMUX_IPSR_MSEL(IP8_31_28,		NFWP_N_B,	SEL_NDF_1),

	/* IPSR9 */
	PINMUX_IPSR_GPSR(IP9_3_0,		SD1_DAT1),
	PINMUX_IPSR_MSEL(IP9_3_0,		NFCE_N_B,	SEL_NDF_1),

	PINMUX_IPSR_GPSR(IP9_7_4,		SD1_DAT2),
	PINMUX_IPSR_MSEL(IP9_7_4,		NFALE_B,	SEL_NDF_1),

	PINMUX_IPSR_GPSR(IP9_11_8,		SD1_DAT3),
	PINMUX_IPSR_MSEL(IP9_11_8,		NFRB_N_B,	SEL_NDF_1),

	PINMUX_IPSR_GPSR(IP9_15_12,		SD3_CLK),
	PINMUX_IPSR_GPSR(IP9_15_12,		NFWE_N),

	PINMUX_IPSR_GPSR(IP9_19_16,		SD3_CMD),
	PINMUX_IPSR_GPSR(IP9_19_16,		NFRE_N),

	PINMUX_IPSR_GPSR(IP9_23_20,		SD3_DAT0),
	PINMUX_IPSR_GPSR(IP9_23_20,		NFDATA0),

	PINMUX_IPSR_GPSR(IP9_27_24,		SD3_DAT1),
	PINMUX_IPSR_GPSR(IP9_27_24,		NFDATA1),

	PINMUX_IPSR_GPSR(IP9_31_28,		SD3_DAT2),
	PINMUX_IPSR_GPSR(IP9_31_28,		NFDATA2),

	/* IPSR10 */
	PINMUX_IPSR_GPSR(IP10_3_0,		SD3_DAT3),
	PINMUX_IPSR_GPSR(IP10_3_0,		NFDATA3),

	PINMUX_IPSR_GPSR(IP10_7_4,		SD3_DAT4),
	PINMUX_IPSR_GPSR(IP10_7_4,		NFDATA4),

	PINMUX_IPSR_GPSR(IP10_11_8,		SD3_DAT5),
	PINMUX_IPSR_GPSR(IP10_11_8,		NFDATA5),

	PINMUX_IPSR_GPSR(IP10_15_12,		SD3_DAT6),
	PINMUX_IPSR_GPSR(IP10_15_12,		NFDATA6),

	PINMUX_IPSR_GPSR(IP10_19_16,		SD3_DAT7),
	PINMUX_IPSR_GPSR(IP10_19_16,		NFDATA7),

	PINMUX_IPSR_GPSR(IP10_23_20,		SD3_DS),
	PINMUX_IPSR_GPSR(IP10_23_20,		NFCLE),

	PINMUX_IPSR_GPSR(IP10_27_24,		SD0_CD),
	PINMUX_IPSR_MSEL(IP10_27_24,		NFALE_A,	SEL_NDF_0),
	PINMUX_IPSR_GPSR(IP10_27_24,		SD3_CD),
	PINMUX_IPSR_MSEL(IP10_27_24,		RIF0_CLK_B,	SEL_DRIF0_1),
	PINMUX_IPSR_MSEL(IP10_27_24,		SCL2_B,		SEL_I2C2_1),
	PINMUX_IPSR_MSEL(IP10_27_24,		TCLK1_A,	SEL_TIMER_TMU_0),
	PINMUX_IPSR_MSEL(IP10_27_24,		SSI_SCK2_B,	SEL_SSI2_1),
	PINMUX_IPSR_GPSR(IP10_27_24,		TS_SCK0),

	PINMUX_IPSR_GPSR(IP10_31_28,		SD0_WP),
	PINMUX_IPSR_MSEL(IP10_31_28,		NFRB_N_A,	SEL_NDF_0),
	PINMUX_IPSR_GPSR(IP10_31_28,		SD3_WP),
	PINMUX_IPSR_MSEL(IP10_31_28,		RIF0_D0_B,	SEL_DRIF0_1),
	PINMUX_IPSR_MSEL(IP10_31_28,		SDA2_B,		SEL_I2C2_1),
	PINMUX_IPSR_MSEL(IP10_31_28,		TCLK2_A,	SEL_TIMER_TMU_0),
	PINMUX_IPSR_MSEL(IP10_31_28,		SSI_WS2_B,	SEL_SSI2_1),
	PINMUX_IPSR_GPSR(IP10_31_28,		TS_SDAT0),

	/* IPSR11 */
	PINMUX_IPSR_GPSR(IP11_3_0,		SD1_CD),
	PINMUX_IPSR_MSEL(IP11_3_0,		NFCE_N_A,	SEL_NDF_0),
	PINMUX_IPSR_GPSR(IP11_3_0,		SSI_SCK1),
	PINMUX_IPSR_MSEL(IP11_3_0,		RIF0_D1_B,	SEL_DRIF0_1),
	PINMUX_IPSR_GPSR(IP11_3_0,		TS_SDEN0),

	PINMUX_IPSR_GPSR(IP11_7_4,		SD1_WP),
	PINMUX_IPSR_MSEL(IP11_7_4,		NFWP_N_A,	SEL_NDF_0),
	PINMUX_IPSR_GPSR(IP11_7_4,		SSI_WS1),
	PINMUX_IPSR_MSEL(IP11_7_4,		RIF0_SYNC_B,	SEL_DRIF0_1),
	PINMUX_IPSR_GPSR(IP11_7_4,		TS_SPSYNC0),

	PINMUX_IPSR_MSEL(IP11_11_8,		RX0_A,		SEL_SCIF0_0),
	PINMUX_IPSR_MSEL(IP11_11_8,		HRX1_A,		SEL_HSCIF1_0),
	PINMUX_IPSR_MSEL(IP11_11_8,		SSI_SCK2_A,	SEL_SSI2_0),
	PINMUX_IPSR_GPSR(IP11_11_8,		RIF1_SYNC),
	PINMUX_IPSR_GPSR(IP11_11_8,		TS_SCK1),

	PINMUX_IPSR_MSEL(IP11_15_12,		TX0_A,		SEL_SCIF0_0),
	PINMUX_IPSR_GPSR(IP11_15_12,		HTX1_A),
	PINMUX_IPSR_MSEL(IP11_15_12,		SSI_WS2_A,	SEL_SSI2_0),
	PINMUX_IPSR_GPSR(IP11_15_12,		RIF1_D0),
	PINMUX_IPSR_GPSR(IP11_15_12,		TS_SDAT1),

	PINMUX_IPSR_MSEL(IP11_19_16,		CTS0_N_A,	SEL_SCIF0_0),
	PINMUX_IPSR_MSEL(IP11_19_16,		NFDATA14_A,	SEL_NDF_0),
	PINMUX_IPSR_GPSR(IP11_19_16,		AUDIO_CLKOUT_A),
	PINMUX_IPSR_GPSR(IP11_19_16,		RIF1_D1),
	PINMUX_IPSR_MSEL(IP11_19_16,		SCIF_CLK_A,	SEL_SCIF_0),
	PINMUX_IPSR_MSEL(IP11_19_16,		FMCLK_A,	SEL_FM_0),

	PINMUX_IPSR_MSEL(IP11_23_20,		RTS0_N_A,	SEL_SCIF0_0),
	PINMUX_IPSR_MSEL(IP11_23_20,		NFDATA15_A,	SEL_NDF_0),
	PINMUX_IPSR_GPSR(IP11_23_20,		AUDIO_CLKOUT1_A),
	PINMUX_IPSR_GPSR(IP11_23_20,		RIF1_CLK),
	PINMUX_IPSR_MSEL(IP11_23_20,		SCL2_A,		SEL_I2C2_0),
	PINMUX_IPSR_MSEL(IP11_23_20,		FMIN_A,		SEL_FM_0),

	PINMUX_IPSR_MSEL(IP11_27_24,		SCK0_A,		SEL_SCIF0_0),
	PINMUX_IPSR_MSEL(IP11_27_24,		HSCK1_A,	SEL_HSCIF1_0),
	PINMUX_IPSR_GPSR(IP11_27_24,		USB3HS0_ID),
	PINMUX_IPSR_GPSR(IP11_27_24,		RTS1_N),
	PINMUX_IPSR_MSEL(IP11_27_24,		SDA2_A,		SEL_I2C2_0),
	PINMUX_IPSR_MSEL(IP11_27_24,		FMCLK_C,	SEL_FM_2),
	PINMUX_IPSR_GPSR(IP11_27_24,		USB0_ID),

	PINMUX_IPSR_GPSR(IP11_31_28,		RX1),
	PINMUX_IPSR_MSEL(IP11_31_28,		HRX2_B,		SEL_HSCIF2_1),
	PINMUX_IPSR_MSEL(IP11_31_28,		SSI_SCK9_B,	SEL_SSI9_1),
	PINMUX_IPSR_GPSR(IP11_31_28,		AUDIO_CLKOUT1_B),

	/* IPSR12 */
	PINMUX_IPSR_GPSR(IP12_3_0,		TX1),
	PINMUX_IPSR_GPSR(IP12_3_0,		HTX2_B),
	PINMUX_IPSR_MSEL(IP12_3_0,		SSI_WS9_B,	SEL_SSI9_1),
	PINMUX_IPSR_GPSR(IP12_3_0,		AUDIO_CLKOUT3_B),

	PINMUX_IPSR_MSEL(IP12_7_4,		SCK2_A,		SEL_SCIF2_0),
	PINMUX_IPSR_MSEL(IP12_7_4,		HSCK0_A,	SEL_HSCIF0_0),
	PINMUX_IPSR_MSEL(IP12_7_4,		AUDIO_CLKB_A,	SEL_ADGB_0),
	PINMUX_IPSR_GPSR(IP12_7_4,		CTS1_N),
	PINMUX_IPSR_MSEL(IP12_7_4,		RIF0_CLK_A,	SEL_DRIF0_0),
	PINMUX_IPSR_MSEL(IP12_7_4,		REMOCON_A,	SEL_REMOCON_0),
	PINMUX_IPSR_MSEL(IP12_7_4,		SCIF_CLK_B,	SEL_SCIF_1),

	PINMUX_IPSR_MSEL(IP12_11_8,		TX2_A,		SEL_SCIF2_0),
	PINMUX_IPSR_MSEL(IP12_11_8,		HRX0_A,		SEL_HSCIF0_0),
	PINMUX_IPSR_GPSR(IP12_11_8,		AUDIO_CLKOUT2_A),
	PINMUX_IPSR_MSEL(IP12_11_8,		SCL1_A,		SEL_I2C1_0),
	PINMUX_IPSR_MSEL(IP12_11_8,		FSO_CFE_0_N_A,	SEL_FSO_0),
	PINMUX_IPSR_GPSR(IP12_11_8,		TS_SDEN1),

	PINMUX_IPSR_MSEL(IP12_15_12,		RX2_A,		SEL_SCIF2_0),
	PINMUX_IPSR_GPSR(IP12_15_12,		HTX0_A),
	PINMUX_IPSR_GPSR(IP12_15_12,		AUDIO_CLKOUT3_A),
	PINMUX_IPSR_MSEL(IP12_15_12,		SDA1_A,		SEL_I2C1_0),
	PINMUX_IPSR_MSEL(IP12_15_12,		FSO_CFE_1_N_A,	SEL_FSO_0),
	PINMUX_IPSR_GPSR(IP12_15_12,		TS_SPSYNC1),

	PINMUX_IPSR_GPSR(IP12_19_16,		MSIOF0_SCK),
	PINMUX_IPSR_GPSR(IP12_19_16,		SSI_SCK78),

	PINMUX_IPSR_GPSR(IP12_23_20,		MSIOF0_RXD),
	PINMUX_IPSR_GPSR(IP12_23_20,		SSI_WS78),
	PINMUX_IPSR_MSEL(IP12_23_20,		TX2_B,		SEL_SCIF2_1),

	PINMUX_IPSR_GPSR(IP12_27_24,		MSIOF0_TXD),
	PINMUX_IPSR_GPSR(IP12_27_24,		SSI_SDATA7),
	PINMUX_IPSR_MSEL(IP12_27_24,		RX2_B,		SEL_SCIF2_1),

	PINMUX_IPSR_GPSR(IP12_31_28,		MSIOF0_SYNC),
	PINMUX_IPSR_GPSR(IP12_31_28,		AUDIO_CLKOUT_B),
	PINMUX_IPSR_GPSR(IP12_31_28,		SSI_SDATA8),

	/* IPSR13 */
	PINMUX_IPSR_GPSR(IP13_3_0,		MSIOF0_SS1),
	PINMUX_IPSR_MSEL(IP13_3_0,		HRX2_A,		SEL_HSCIF2_0),
	PINMUX_IPSR_GPSR(IP13_3_0,		SSI_SCK4),
	PINMUX_IPSR_MSEL(IP13_3_0,		HCTS0_N_A,	SEL_HSCIF0_0),
	PINMUX_IPSR_GPSR(IP13_3_0,		BPFCLK_C),
	PINMUX_IPSR_MSEL(IP13_3_0,		SPEEDIN_A,	SEL_SPEED_PULSE_IF_0),

	PINMUX_IPSR_GPSR(IP13_7_4,		MSIOF0_SS2),
	PINMUX_IPSR_GPSR(IP13_7_4,		HTX2_A),
	PINMUX_IPSR_GPSR(IP13_7_4,		SSI_WS4),
	PINMUX_IPSR_MSEL(IP13_7_4,		HRTS0_N_A,	SEL_HSCIF0_0),
	PINMUX_IPSR_MSEL(IP13_7_4,		FMIN_C,		SEL_FM_2),
	PINMUX_IPSR_GPSR(IP13_7_4,		BPFCLK_A),

	PINMUX_IPSR_GPSR(IP13_11_8,		SSI_SDATA9),
	PINMUX_IPSR_MSEL(IP13_11_8,		AUDIO_CLKC_A,	SEL_ADGC_0),
	PINMUX_IPSR_GPSR(IP13_11_8,		SCK1),

	PINMUX_IPSR_GPSR(IP13_15_12,		MLB_CLK),
	PINMUX_IPSR_MSEL(IP13_15_12,		RX0_B,		SEL_SCIF0_1),
	PINMUX_IPSR_MSEL(IP13_15_12,		RIF0_D0_A,	SEL_DRIF0_0),
	PINMUX_IPSR_MSEL(IP13_15_12,		SCL1_B,		SEL_I2C1_1),
	PINMUX_IPSR_MSEL(IP13_15_12,		TCLK1_B,	SEL_TIMER_TMU_1),
	PINMUX_IPSR_GPSR(IP13_15_12,		SIM0_RST_A),

	PINMUX_IPSR_GPSR(IP13_19_16,		MLB_SIG),
	PINMUX_IPSR_MSEL(IP13_19_16,		SCK0_B,		SEL_SCIF0_1),
	PINMUX_IPSR_MSEL(IP13_19_16,		RIF0_D1_A,	SEL_DRIF0_0),
	PINMUX_IPSR_MSEL(IP13_19_16,		SDA1_B,		SEL_I2C1_1),
	PINMUX_IPSR_MSEL(IP13_19_16,		TCLK2_B,	SEL_TIMER_TMU_1),
	PINMUX_IPSR_MSEL(IP13_19_16,		SIM0_D_A,	SEL_SIMCARD_0),

	PINMUX_IPSR_GPSR(IP13_23_20,		MLB_DAT),
	PINMUX_IPSR_MSEL(IP13_23_20,		TX0_B,		SEL_SCIF0_1),
	PINMUX_IPSR_MSEL(IP13_23_20,		RIF0_SYNC_A,	SEL_DRIF0_0),
	PINMUX_IPSR_GPSR(IP13_23_20,		SIM0_CLK_A),

	PINMUX_IPSR_GPSR(IP13_27_24,		SSI_SCK01239),

	PINMUX_IPSR_GPSR(IP13_31_28,		SSI_WS01239),

	/* IPSR14 */
	PINMUX_IPSR_GPSR(IP14_3_0,		SSI_SDATA0),

	PINMUX_IPSR_GPSR(IP14_7_4,		SSI_SDATA1),
	PINMUX_IPSR_MSEL(IP14_7_4,		AUDIO_CLKC_B,	SEL_ADGC_1),
	PINMUX_IPSR_MSEL(IP14_7_4,		PWM0_B,		SEL_PWM0_1),

	PINMUX_IPSR_GPSR(IP14_11_8,		SSI_SDATA2),
	PINMUX_IPSR_GPSR(IP14_11_8,		AUDIO_CLKOUT2_B),
	PINMUX_IPSR_MSEL(IP14_11_8,		SSI_SCK9_A,	SEL_SSI9_0),
	PINMUX_IPSR_MSEL(IP14_11_8,		PWM1_B,		SEL_PWM1_1),

	PINMUX_IPSR_GPSR(IP14_15_12,		SSI_SCK349),
	PINMUX_IPSR_MSEL(IP14_15_12,		PWM2_C,		SEL_PWM2_2),

	PINMUX_IPSR_GPSR(IP14_19_16,		SSI_WS349),
	PINMUX_IPSR_MSEL(IP14_19_16,		PWM3_C,		SEL_PWM3_2),

	PINMUX_IPSR_GPSR(IP14_23_20,		SSI_SDATA3),
	PINMUX_IPSR_GPSR(IP14_23_20,		AUDIO_CLKOUT1_C),
	PINMUX_IPSR_MSEL(IP14_23_20,		AUDIO_CLKB_B,	SEL_ADGB_1),
	PINMUX_IPSR_MSEL(IP14_23_20,		PWM4_B,		SEL_PWM4_1),

	PINMUX_IPSR_GPSR(IP14_27_24,		SSI_SDATA4),
	PINMUX_IPSR_MSEL(IP14_27_24,		SSI_WS9_A,	SEL_SSI9_0),
	PINMUX_IPSR_MSEL(IP14_27_24,		PWM5_B,		SEL_PWM5_1),

	PINMUX_IPSR_GPSR(IP14_31_28,		SSI_SCK5),
	PINMUX_IPSR_MSEL(IP14_31_28,		HRX0_B,		SEL_HSCIF0_1),
	PINMUX_IPSR_GPSR(IP14_31_28,		USB0_PWEN_B),
	PINMUX_IPSR_MSEL(IP14_31_28,		SCL2_D,		SEL_I2C2_3),
	PINMUX_IPSR_MSEL(IP14_31_28,		PWM6_B,		SEL_PWM6_1),

	/* IPSR15 */
	PINMUX_IPSR_GPSR(IP15_3_0,		SSI_WS5),
	PINMUX_IPSR_GPSR(IP15_3_0,		HTX0_B),
	PINMUX_IPSR_MSEL(IP15_3_0,		USB0_OVC_B,	SEL_USB_20_CH0_1),
	PINMUX_IPSR_MSEL(IP15_3_0,		SDA2_D,		SEL_I2C2_3),

	PINMUX_IPSR_GPSR(IP15_7_4,		SSI_SDATA5),
	PINMUX_IPSR_MSEL(IP15_7_4,		HSCK0_B,	SEL_HSCIF0_1),
	PINMUX_IPSR_MSEL(IP15_7_4,		AUDIO_CLKB_C,	SEL_ADGB_2),
	PINMUX_IPSR_GPSR(IP15_7_4,		TPU0TO0),

	PINMUX_IPSR_GPSR(IP15_11_8,		SSI_SCK6),
	PINMUX_IPSR_MSEL(IP15_11_8,		HSCK2_A,	SEL_HSCIF2_0),
	PINMUX_IPSR_MSEL(IP15_11_8,		AUDIO_CLKC_C,	SEL_ADGC_2),
	PINMUX_IPSR_GPSR(IP15_11_8,		TPU0TO1),
	PINMUX_IPSR_MSEL(IP15_11_8,		FSO_CFE_0_N_B,	SEL_FSO_1),
	PINMUX_IPSR_GPSR(IP15_11_8,		SIM0_RST_B),

	PINMUX_IPSR_GPSR(IP15_15_12,		SSI_WS6),
	PINMUX_IPSR_MSEL(IP15_15_12,		HCTS2_N_A,	SEL_HSCIF2_0),
	PINMUX_IPSR_GPSR(IP15_15_12,		AUDIO_CLKOUT2_C),
	PINMUX_IPSR_GPSR(IP15_15_12,		TPU0TO2),
	PINMUX_IPSR_MSEL(IP15_15_12,		SDA1_D,		SEL_I2C1_3),
	PINMUX_IPSR_MSEL(IP15_15_12,		FSO_CFE_1_N_B,	SEL_FSO_1),
	PINMUX_IPSR_MSEL(IP15_15_12,		SIM0_D_B,	SEL_SIMCARD_1),

	PINMUX_IPSR_GPSR(IP15_19_16,		SSI_SDATA6),
	PINMUX_IPSR_MSEL(IP15_19_16,		HRTS2_N_A,	SEL_HSCIF2_0),
	PINMUX_IPSR_GPSR(IP15_19_16,		AUDIO_CLKOUT3_C),
	PINMUX_IPSR_GPSR(IP15_19_16,		TPU0TO3),
	PINMUX_IPSR_MSEL(IP15_19_16,		SCL1_D,		SEL_I2C1_3),
	PINMUX_IPSR_MSEL(IP15_19_16,		FSO_TOE_N_B,	SEL_FSO_1),
	PINMUX_IPSR_GPSR(IP15_19_16,		SIM0_CLK_B),

	PINMUX_IPSR_GPSR(IP15_23_20,		AUDIO_CLKA),

	PINMUX_IPSR_GPSR(IP15_27_24,		USB30_PWEN),
	PINMUX_IPSR_GPSR(IP15_27_24,		USB0_PWEN_A),

	PINMUX_IPSR_GPSR(IP15_31_28,		USB30_OVC),
	PINMUX_IPSR_MSEL(IP15_31_28,		USB0_OVC_A,	SEL_USB_20_CH0_0),

/*
 * Static pins can not be muxed between different functions but
 * still need mark entries in the pinmux list. Add each static
 * pin to the list without an associated function. The sh-pfc
 * core will do the right thing and skip trying to mux the pin
 * while still applying configuration to it.
 */
#define FM(x)   PINMUX_DATA(x##_MARK, 0),
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
static const unsigned int audio_clk_a_pins[] = {
	/* CLK A */
	RCAR_GP_PIN(6, 8),
};

static const unsigned int audio_clk_a_mux[] = {
	AUDIO_CLKA_MARK,
};

static const unsigned int audio_clk_b_a_pins[] = {
	/* CLK B_A */
	RCAR_GP_PIN(5, 7),
};

static const unsigned int audio_clk_b_a_mux[] = {
	AUDIO_CLKB_A_MARK,
};

static const unsigned int audio_clk_b_b_pins[] = {
	/* CLK B_B */
	RCAR_GP_PIN(6, 7),
};

static const unsigned int audio_clk_b_b_mux[] = {
	AUDIO_CLKB_B_MARK,
};

static const unsigned int audio_clk_b_c_pins[] = {
	/* CLK B_C */
	RCAR_GP_PIN(6, 13),
};

static const unsigned int audio_clk_b_c_mux[] = {
	AUDIO_CLKB_C_MARK,
};

static const unsigned int audio_clk_c_a_pins[] = {
	/* CLK C_A */
	RCAR_GP_PIN(5, 16),
};

static const unsigned int audio_clk_c_a_mux[] = {
	AUDIO_CLKC_A_MARK,
};

static const unsigned int audio_clk_c_b_pins[] = {
	/* CLK C_B */
	RCAR_GP_PIN(6, 3),
};

static const unsigned int audio_clk_c_b_mux[] = {
	AUDIO_CLKC_B_MARK,
};

static const unsigned int audio_clk_c_c_pins[] = {
	/* CLK C_C */
	RCAR_GP_PIN(6, 14),
};

static const unsigned int audio_clk_c_c_mux[] = {
	AUDIO_CLKC_C_MARK,
};

static const unsigned int audio_clkout_a_pins[] = {
	/* CLKOUT_A */
	RCAR_GP_PIN(5, 3),
};

static const unsigned int audio_clkout_a_mux[] = {
	AUDIO_CLKOUT_A_MARK,
};

static const unsigned int audio_clkout_b_pins[] = {
	/* CLKOUT_B */
	RCAR_GP_PIN(5, 13),
};

static const unsigned int audio_clkout_b_mux[] = {
	AUDIO_CLKOUT_B_MARK,
};

static const unsigned int audio_clkout1_a_pins[] = {
	/* CLKOUT1_A */
	RCAR_GP_PIN(5, 4),
};

static const unsigned int audio_clkout1_a_mux[] = {
	AUDIO_CLKOUT1_A_MARK,
};

static const unsigned int audio_clkout1_b_pins[] = {
	/* CLKOUT1_B */
	RCAR_GP_PIN(5, 5),
};

static const unsigned int audio_clkout1_b_mux[] = {
	AUDIO_CLKOUT1_B_MARK,
};

static const unsigned int audio_clkout1_c_pins[] = {
	/* CLKOUT1_C */
	RCAR_GP_PIN(6, 7),
};

static const unsigned int audio_clkout1_c_mux[] = {
	AUDIO_CLKOUT1_C_MARK,
};

static const unsigned int audio_clkout2_a_pins[] = {
	/* CLKOUT2_A */
	RCAR_GP_PIN(5, 8),
};

static const unsigned int audio_clkout2_a_mux[] = {
	AUDIO_CLKOUT2_A_MARK,
};

static const unsigned int audio_clkout2_b_pins[] = {
	/* CLKOUT2_B */
	RCAR_GP_PIN(6, 4),
};

static const unsigned int audio_clkout2_b_mux[] = {
	AUDIO_CLKOUT2_B_MARK,
};

static const unsigned int audio_clkout2_c_pins[] = {
	/* CLKOUT2_C */
	RCAR_GP_PIN(6, 15),
};

static const unsigned int audio_clkout2_c_mux[] = {
	AUDIO_CLKOUT2_C_MARK,
};

static const unsigned int audio_clkout3_a_pins[] = {
	/* CLKOUT3_A */
	RCAR_GP_PIN(5, 9),
};

static const unsigned int audio_clkout3_a_mux[] = {
	AUDIO_CLKOUT3_A_MARK,
};

static const unsigned int audio_clkout3_b_pins[] = {
	/* CLKOUT3_B */
	RCAR_GP_PIN(5, 6),
};

static const unsigned int audio_clkout3_b_mux[] = {
	AUDIO_CLKOUT3_B_MARK,
};

static const unsigned int audio_clkout3_c_pins[] = {
	/* CLKOUT3_C */
	RCAR_GP_PIN(6, 16),
};

static const unsigned int audio_clkout3_c_mux[] = {
	AUDIO_CLKOUT3_C_MARK,
};

/* - EtherAVB --------------------------------------------------------------- */
static const unsigned int avb_link_pins[] = {
	/* AVB_LINK */
	RCAR_GP_PIN(2, 23),
};

static const unsigned int avb_link_mux[] = {
	AVB_LINK_MARK,
};

static const unsigned int avb_magic_pins[] = {
	/* AVB_MAGIC */
	RCAR_GP_PIN(2, 22),
};

static const unsigned int avb_magic_mux[] = {
	AVB_MAGIC_MARK,
};

static const unsigned int avb_phy_int_pins[] = {
	/* AVB_PHY_INT */
	RCAR_GP_PIN(2, 21),
};

static const unsigned int avb_phy_int_mux[] = {
	AVB_PHY_INT_MARK,
};

static const unsigned int avb_mii_pins[] = {
	/*
	 * AVB_RX_CTL, AVB_RXC, AVB_RD0,
	 * AVB_RD1, AVB_RD2, AVB_RD3,
	 * AVB_TXCREFCLK
	 */
	RCAR_GP_PIN(2, 14), RCAR_GP_PIN(2, 15), RCAR_GP_PIN(2, 16),
	RCAR_GP_PIN(2, 17), RCAR_GP_PIN(2, 18), RCAR_GP_PIN(2, 19),
	RCAR_GP_PIN(2, 20),
};

static const unsigned int avb_mii_mux[] = {
	AVB_RX_CTL_MARK, AVB_RXC_MARK, AVB_RD0_MARK,
	AVB_RD1_MARK, AVB_RD2_MARK, AVB_RD3_MARK,
	AVB_TXCREFCLK_MARK,
};

static const unsigned int avb_avtp_pps_pins[] = {
	/* AVB_AVTP_PPS */
	RCAR_GP_PIN(1, 2),
};

static const unsigned int avb_avtp_pps_mux[] = {
	AVB_AVTP_PPS_MARK,
};

static const unsigned int avb_avtp_match_pins[] = {
	/* AVB_AVTP_MATCH */
	RCAR_GP_PIN(2, 24),
};

static const unsigned int avb_avtp_match_mux[] = {
	AVB_AVTP_MATCH_MARK,
};

static const unsigned int avb_avtp_capture_pins[] = {
	/* AVB_AVTP_CAPTURE */
	RCAR_GP_PIN(2, 25),
};

static const unsigned int avb_avtp_capture_mux[] = {
	AVB_AVTP_CAPTURE_MARK,
};

/* - CAN ------------------------------------------------------------------ */
static const unsigned int can0_data_pins[] = {
	/* TX, RX */
	RCAR_GP_PIN(0, 12), RCAR_GP_PIN(0, 13),
};

static const unsigned int can0_data_mux[] = {
	CAN0_TX_MARK, CAN0_RX_MARK,
};

static const unsigned int can1_data_pins[] = {
	/* TX, RX */
	RCAR_GP_PIN(0, 4), RCAR_GP_PIN(0, 7),
};

static const unsigned int can1_data_mux[] = {
	CAN1_TX_MARK, CAN1_RX_MARK,
};

/* - CAN Clock -------------------------------------------------------------- */
static const unsigned int can_clk_pins[] = {
	/* CLK */
	RCAR_GP_PIN(0, 14),
};

static const unsigned int can_clk_mux[] = {
	CAN_CLK_MARK,
};

/* - CAN FD --------------------------------------------------------------- */
static const unsigned int canfd0_data_pins[] = {
	/* TX, RX */
	RCAR_GP_PIN(0, 12), RCAR_GP_PIN(0, 13),
};

static const unsigned int canfd0_data_mux[] = {
	CANFD0_TX_MARK, CANFD0_RX_MARK,
};

static const unsigned int canfd1_data_pins[] = {
	/* TX, RX */
	RCAR_GP_PIN(0, 4), RCAR_GP_PIN(0, 7),
};

static const unsigned int canfd1_data_mux[] = {
	CANFD1_TX_MARK, CANFD1_RX_MARK,
};

#ifdef CONFIG_PINCTRL_PFC_R8A77990
/* - DRIF0 --------------------------------------------------------------- */
static const unsigned int drif0_ctrl_a_pins[] = {
	/* CLK, SYNC */
	RCAR_GP_PIN(5, 7), RCAR_GP_PIN(5, 19),
};

static const unsigned int drif0_ctrl_a_mux[] = {
	RIF0_CLK_A_MARK, RIF0_SYNC_A_MARK,
};

static const unsigned int drif0_data0_a_pins[] = {
	/* D0 */
	RCAR_GP_PIN(5, 17),
};

static const unsigned int drif0_data0_a_mux[] = {
	RIF0_D0_A_MARK,
};

static const unsigned int drif0_data1_a_pins[] = {
	/* D1 */
	RCAR_GP_PIN(5, 18),
};

static const unsigned int drif0_data1_a_mux[] = {
	RIF0_D1_A_MARK,
};

static const unsigned int drif0_ctrl_b_pins[] = {
	/* CLK, SYNC */
	RCAR_GP_PIN(3, 12), RCAR_GP_PIN(3, 15),
};

static const unsigned int drif0_ctrl_b_mux[] = {
	RIF0_CLK_B_MARK, RIF0_SYNC_B_MARK,
};

static const unsigned int drif0_data0_b_pins[] = {
	/* D0 */
	RCAR_GP_PIN(3, 13),
};

static const unsigned int drif0_data0_b_mux[] = {
	RIF0_D0_B_MARK,
};

static const unsigned int drif0_data1_b_pins[] = {
	/* D1 */
	RCAR_GP_PIN(3, 14),
};

static const unsigned int drif0_data1_b_mux[] = {
	RIF0_D1_B_MARK,
};

/* - DRIF1 --------------------------------------------------------------- */
static const unsigned int drif1_ctrl_pins[] = {
	/* CLK, SYNC */
	RCAR_GP_PIN(5, 4), RCAR_GP_PIN(5, 1),
};

static const unsigned int drif1_ctrl_mux[] = {
	RIF1_CLK_MARK, RIF1_SYNC_MARK,
};

static const unsigned int drif1_data0_pins[] = {
	/* D0 */
	RCAR_GP_PIN(5, 2),
};

static const unsigned int drif1_data0_mux[] = {
	RIF1_D0_MARK,
};

static const unsigned int drif1_data1_pins[] = {
	/* D1 */
	RCAR_GP_PIN(5, 3),
};

static const unsigned int drif1_data1_mux[] = {
	RIF1_D1_MARK,
};

/* - DRIF2 --------------------------------------------------------------- */
static const unsigned int drif2_ctrl_a_pins[] = {
	/* CLK, SYNC */
	RCAR_GP_PIN(2, 6), RCAR_GP_PIN(2, 7),
};

static const unsigned int drif2_ctrl_a_mux[] = {
	RIF2_CLK_A_MARK, RIF2_SYNC_A_MARK,
};

static const unsigned int drif2_data0_a_pins[] = {
	/* D0 */
	RCAR_GP_PIN(2, 8),
};

static const unsigned int drif2_data0_a_mux[] = {
	RIF2_D0_A_MARK,
};

static const unsigned int drif2_data1_a_pins[] = {
	/* D1 */
	RCAR_GP_PIN(2, 9),
};

static const unsigned int drif2_data1_a_mux[] = {
	RIF2_D1_A_MARK,
};

static const unsigned int drif2_ctrl_b_pins[] = {
	/* CLK, SYNC */
	RCAR_GP_PIN(1, 4), RCAR_GP_PIN(1, 5),
};

static const unsigned int drif2_ctrl_b_mux[] = {
	RIF2_CLK_B_MARK, RIF2_SYNC_B_MARK,
};

static const unsigned int drif2_data0_b_pins[] = {
	/* D0 */
	RCAR_GP_PIN(1, 6),
};

static const unsigned int drif2_data0_b_mux[] = {
	RIF2_D0_B_MARK,
};

static const unsigned int drif2_data1_b_pins[] = {
	/* D1 */
	RCAR_GP_PIN(1, 7),
};

static const unsigned int drif2_data1_b_mux[] = {
	RIF2_D1_B_MARK,
};

/* - DRIF3 --------------------------------------------------------------- */
static const unsigned int drif3_ctrl_a_pins[] = {
	/* CLK, SYNC */
	RCAR_GP_PIN(2, 10), RCAR_GP_PIN(2, 11),
};

static const unsigned int drif3_ctrl_a_mux[] = {
	RIF3_CLK_A_MARK, RIF3_SYNC_A_MARK,
};

static const unsigned int drif3_data0_a_pins[] = {
	/* D0 */
	RCAR_GP_PIN(2, 12),
};

static const unsigned int drif3_data0_a_mux[] = {
	RIF3_D0_A_MARK,
};

static const unsigned int drif3_data1_a_pins[] = {
	/* D1 */
	RCAR_GP_PIN(2, 13),
};

static const unsigned int drif3_data1_a_mux[] = {
	RIF3_D1_A_MARK,
};

static const unsigned int drif3_ctrl_b_pins[] = {
	/* CLK, SYNC */
	RCAR_GP_PIN(0, 8), RCAR_GP_PIN(0, 9),
};

static const unsigned int drif3_ctrl_b_mux[] = {
	RIF3_CLK_B_MARK, RIF3_SYNC_B_MARK,
};

static const unsigned int drif3_data0_b_pins[] = {
	/* D0 */
	RCAR_GP_PIN(0, 10),
};

static const unsigned int drif3_data0_b_mux[] = {
	RIF3_D0_B_MARK,
};

static const unsigned int drif3_data1_b_pins[] = {
	/* D1 */
	RCAR_GP_PIN(0, 11),
};

static const unsigned int drif3_data1_b_mux[] = {
	RIF3_D1_B_MARK,
};
#endif /* CONFIG_PINCTRL_PFC_R8A77990 */

/* - DU --------------------------------------------------------------------- */
static const unsigned int du_rgb666_pins[] = {
	/* R[7:2], G[7:2], B[7:2] */
	RCAR_GP_PIN(0, 8),  RCAR_GP_PIN(0, 6),  RCAR_GP_PIN(0, 5),
	RCAR_GP_PIN(0, 3),  RCAR_GP_PIN(0, 2),  RCAR_GP_PIN(0, 0),
	RCAR_GP_PIN(1, 9),  RCAR_GP_PIN(1, 12), RCAR_GP_PIN(1, 10),
	RCAR_GP_PIN(1, 4),  RCAR_GP_PIN(0, 15), RCAR_GP_PIN(0, 11),
	RCAR_GP_PIN(0, 1),  RCAR_GP_PIN(1, 17), RCAR_GP_PIN(1, 16),
	RCAR_GP_PIN(1, 15), RCAR_GP_PIN(1, 14), RCAR_GP_PIN(1, 13),
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
	RCAR_GP_PIN(0, 8),  RCAR_GP_PIN(0, 6),  RCAR_GP_PIN(0, 5),
	RCAR_GP_PIN(0, 3),  RCAR_GP_PIN(0, 2),  RCAR_GP_PIN(0, 0),
	RCAR_GP_PIN(1, 22), RCAR_GP_PIN(1, 21),
	RCAR_GP_PIN(1, 9),  RCAR_GP_PIN(1, 12), RCAR_GP_PIN(1, 10),
	RCAR_GP_PIN(1, 4),  RCAR_GP_PIN(0, 15), RCAR_GP_PIN(0, 11),
	RCAR_GP_PIN(0, 10), RCAR_GP_PIN(0, 9),
	RCAR_GP_PIN(0, 1),  RCAR_GP_PIN(1, 17), RCAR_GP_PIN(1, 16),
	RCAR_GP_PIN(1, 15), RCAR_GP_PIN(1, 14), RCAR_GP_PIN(1, 13),
	RCAR_GP_PIN(1, 19), RCAR_GP_PIN(1, 18),
};
static const unsigned int du_rgb888_mux[] = {
	DU_DR7_MARK, DU_DR6_MARK, DU_DR5_MARK, DU_DR4_MARK,
	DU_DR3_MARK, DU_DR2_MARK, DU_DR1_MARK, DU_DR0_MARK,
	DU_DG7_MARK, DU_DG6_MARK, DU_DG5_MARK, DU_DG4_MARK,
	DU_DG3_MARK, DU_DG2_MARK, DU_DG1_MARK, DU_DG0_MARK,
	DU_DB7_MARK, DU_DB6_MARK, DU_DB5_MARK, DU_DB4_MARK,
	DU_DB3_MARK, DU_DB2_MARK, DU_DB1_MARK, DU_DB0_MARK,
};
static const unsigned int du_clk_in_0_pins[] = {
	/* CLKIN0 */
	RCAR_GP_PIN(0, 16),
};
static const unsigned int du_clk_in_0_mux[] = {
	DU_DOTCLKIN0_MARK
};
static const unsigned int du_clk_in_1_pins[] = {
	/* CLKIN1 */
	RCAR_GP_PIN(1, 1),
};
static const unsigned int du_clk_in_1_mux[] = {
	DU_DOTCLKIN1_MARK
};
static const unsigned int du_clk_out_0_pins[] = {
	/* CLKOUT */
	RCAR_GP_PIN(1, 3),
};
static const unsigned int du_clk_out_0_mux[] = {
	DU_DOTCLKOUT0_MARK
};
static const unsigned int du_sync_pins[] = {
	/* VSYNC, HSYNC */
	RCAR_GP_PIN(1, 11), RCAR_GP_PIN(1, 8),
};
static const unsigned int du_sync_mux[] = {
	DU_VSYNC_MARK, DU_HSYNC_MARK
};
static const unsigned int du_disp_cde_pins[] = {
	/* DISP_CDE */
	RCAR_GP_PIN(1, 1),
};
static const unsigned int du_disp_cde_mux[] = {
	DU_DISP_CDE_MARK,
};
static const unsigned int du_cde_pins[] = {
	/* CDE */
	RCAR_GP_PIN(1, 0),
};
static const unsigned int du_cde_mux[] = {
	DU_CDE_MARK,
};
static const unsigned int du_disp_pins[] = {
	/* DISP */
	RCAR_GP_PIN(1, 2),
};
static const unsigned int du_disp_mux[] = {
	DU_DISP_MARK,
};

/* - HSCIF0 --------------------------------------------------*/
static const unsigned int hscif0_data_a_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(5, 8), RCAR_GP_PIN(5, 9),
};

static const unsigned int hscif0_data_a_mux[] = {
	HRX0_A_MARK, HTX0_A_MARK,
};

static const unsigned int hscif0_clk_a_pins[] = {
	/* SCK */
	RCAR_GP_PIN(5, 7),
};

static const unsigned int hscif0_clk_a_mux[] = {
	HSCK0_A_MARK,
};

static const unsigned int hscif0_ctrl_a_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(5, 15), RCAR_GP_PIN(5, 14),
};

static const unsigned int hscif0_ctrl_a_mux[] = {
	HRTS0_N_A_MARK, HCTS0_N_A_MARK,
};

static const unsigned int hscif0_data_b_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(6, 11), RCAR_GP_PIN(6, 12),
};

static const unsigned int hscif0_data_b_mux[] = {
	HRX0_B_MARK, HTX0_B_MARK,
};

static const unsigned int hscif0_clk_b_pins[] = {
	/* SCK */
	RCAR_GP_PIN(6, 13),
};

static const unsigned int hscif0_clk_b_mux[] = {
	HSCK0_B_MARK,
};

/* - HSCIF1 ------------------------------------------------- */
static const unsigned int hscif1_data_a_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(5, 1), RCAR_GP_PIN(5, 2),
};

static const unsigned int hscif1_data_a_mux[] = {
	HRX1_A_MARK, HTX1_A_MARK,
};

static const unsigned int hscif1_clk_a_pins[] = {
	/* SCK */
	RCAR_GP_PIN(5, 0),
};

static const unsigned int hscif1_clk_a_mux[] = {
	HSCK1_A_MARK,
};

static const unsigned int hscif1_data_b_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(3, 1), RCAR_GP_PIN(3, 2),
};

static const unsigned int hscif1_data_b_mux[] = {
	HRX1_B_MARK, HTX1_B_MARK,
};

static const unsigned int hscif1_clk_b_pins[] = {
	/* SCK */
	RCAR_GP_PIN(3, 0),
};

static const unsigned int hscif1_clk_b_mux[] = {
	HSCK1_B_MARK,
};

static const unsigned int hscif1_ctrl_b_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(3, 4), RCAR_GP_PIN(3, 3),
};

static const unsigned int hscif1_ctrl_b_mux[] = {
	HRTS1_N_B_MARK, HCTS1_N_B_MARK,
};

/* - HSCIF2 ------------------------------------------------- */
static const unsigned int hscif2_data_a_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(5, 14), RCAR_GP_PIN(5, 15),
};

static const unsigned int hscif2_data_a_mux[] = {
	HRX2_A_MARK, HTX2_A_MARK,
};

static const unsigned int hscif2_clk_a_pins[] = {
	/* SCK */
	RCAR_GP_PIN(6, 14),
};

static const unsigned int hscif2_clk_a_mux[] = {
	HSCK2_A_MARK,
};

static const unsigned int hscif2_ctrl_a_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(6, 16), RCAR_GP_PIN(6, 15),
};

static const unsigned int hscif2_ctrl_a_mux[] = {
	HRTS2_N_A_MARK, HCTS2_N_A_MARK,
};

static const unsigned int hscif2_data_b_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(5, 5), RCAR_GP_PIN(5, 6),
};

static const unsigned int hscif2_data_b_mux[] = {
	HRX2_B_MARK, HTX2_B_MARK,
};

/* - HSCIF3 ------------------------------------------------*/
static const unsigned int hscif3_data_a_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(0, 14), RCAR_GP_PIN(0, 15),
};

static const unsigned int hscif3_data_a_mux[] = {
	HRX3_A_MARK, HTX3_A_MARK,
};

static const unsigned int hscif3_data_b_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(0, 5), RCAR_GP_PIN(0, 6),
};

static const unsigned int hscif3_data_b_mux[] = {
	HRX3_B_MARK, HTX3_B_MARK,
};

static const unsigned int hscif3_clk_b_pins[] = {
	/* SCK */
	RCAR_GP_PIN(0, 4),
};

static const unsigned int hscif3_clk_b_mux[] = {
	HSCK3_B_MARK,
};

static const unsigned int hscif3_data_c_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(2, 10), RCAR_GP_PIN(2, 9),
};

static const unsigned int hscif3_data_c_mux[] = {
	HRX3_C_MARK, HTX3_C_MARK,
};

static const unsigned int hscif3_clk_c_pins[] = {
	/* SCK */
	RCAR_GP_PIN(2, 11),
};

static const unsigned int hscif3_clk_c_mux[] = {
	HSCK3_C_MARK,
};

static const unsigned int hscif3_ctrl_c_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(2, 13), RCAR_GP_PIN(2, 12),
};

static const unsigned int hscif3_ctrl_c_mux[] = {
	HRTS3_N_C_MARK, HCTS3_N_C_MARK,
};

static const unsigned int hscif3_data_d_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(1, 0), RCAR_GP_PIN(1, 3),
};

static const unsigned int hscif3_data_d_mux[] = {
	HRX3_D_MARK, HTX3_D_MARK,
};

static const unsigned int hscif3_data_e_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(0, 9), RCAR_GP_PIN(0, 10),
};

static const unsigned int hscif3_data_e_mux[] = {
	HRX3_E_MARK, HTX3_E_MARK,
};

static const unsigned int hscif3_ctrl_e_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(0, 11), RCAR_GP_PIN(0, 8),
};

static const unsigned int hscif3_ctrl_e_mux[] = {
	HRTS3_N_E_MARK, HCTS3_N_E_MARK,
};

/* - HSCIF4 -------------------------------------------------- */
static const unsigned int hscif4_data_a_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(2, 4), RCAR_GP_PIN(2, 3),
};

static const unsigned int hscif4_data_a_mux[] = {
	HRX4_A_MARK, HTX4_A_MARK,
};

static const unsigned int hscif4_clk_a_pins[] = {
	/* SCK */
	RCAR_GP_PIN(2, 0),
};

static const unsigned int hscif4_clk_a_mux[] = {
	HSCK4_A_MARK,
};

static const unsigned int hscif4_ctrl_a_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(2, 2), RCAR_GP_PIN(2, 1),
};

static const unsigned int hscif4_ctrl_a_mux[] = {
	HRTS4_N_A_MARK, HCTS4_N_A_MARK,
};

static const unsigned int hscif4_data_b_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(2, 8), RCAR_GP_PIN(2, 7),
};

static const unsigned int hscif4_data_b_mux[] = {
	HRX4_B_MARK, HTX4_B_MARK,
};

static const unsigned int hscif4_clk_b_pins[] = {
	/* SCK */
	RCAR_GP_PIN(2, 6),
};

static const unsigned int hscif4_clk_b_mux[] = {
	HSCK4_B_MARK,
};

static const unsigned int hscif4_data_c_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(1, 8), RCAR_GP_PIN(1, 11),
};

static const unsigned int hscif4_data_c_mux[] = {
	HRX4_C_MARK, HTX4_C_MARK,
};

static const unsigned int hscif4_data_d_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(1, 13), RCAR_GP_PIN(1, 14),
};

static const unsigned int hscif4_data_d_mux[] = {
	HRX4_D_MARK, HTX4_D_MARK,
};

static const unsigned int hscif4_data_e_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(1, 18), RCAR_GP_PIN(1, 19),
};

static const unsigned int hscif4_data_e_mux[] = {
	HRX4_E_MARK, HTX4_E_MARK,
};

/* - I2C -------------------------------------------------------------------- */
static const unsigned int i2c1_a_pins[] = {
	/* SCL, SDA */
	RCAR_GP_PIN(5, 8), RCAR_GP_PIN(5, 9),
};

static const unsigned int i2c1_a_mux[] = {
	SCL1_A_MARK, SDA1_A_MARK,
};

static const unsigned int i2c1_b_pins[] = {
	/* SCL, SDA */
	RCAR_GP_PIN(5, 17), RCAR_GP_PIN(5, 18),
};

static const unsigned int i2c1_b_mux[] = {
	SCL1_B_MARK, SDA1_B_MARK,
};

static const unsigned int i2c1_c_pins[] = {
	/* SCL, SDA */
	RCAR_GP_PIN(3, 0), RCAR_GP_PIN(3, 5),
};

static const unsigned int i2c1_c_mux[] = {
	SCL1_C_MARK, SDA1_C_MARK,
};

static const unsigned int i2c1_d_pins[] = {
	/* SCL, SDA */
	RCAR_GP_PIN(6, 16), RCAR_GP_PIN(6, 15),
};

static const unsigned int i2c1_d_mux[] = {
	SCL1_D_MARK, SDA1_D_MARK,
};

static const unsigned int i2c2_a_pins[] = {
	/* SCL, SDA */
	RCAR_GP_PIN(5, 4), RCAR_GP_PIN(5, 0),
};

static const unsigned int i2c2_a_mux[] = {
	SCL2_A_MARK, SDA2_A_MARK,
};

static const unsigned int i2c2_b_pins[] = {
	/* SCL, SDA */
	RCAR_GP_PIN(3, 12), RCAR_GP_PIN(3, 13),
};

static const unsigned int i2c2_b_mux[] = {
	SCL2_B_MARK, SDA2_B_MARK,
};

static const unsigned int i2c2_c_pins[] = {
	/* SCL, SDA */
	RCAR_GP_PIN(3, 4), RCAR_GP_PIN(3, 3),
};

static const unsigned int i2c2_c_mux[] = {
	SCL2_C_MARK, SDA2_C_MARK,
};

static const unsigned int i2c2_d_pins[] = {
	/* SCL, SDA */
	RCAR_GP_PIN(6, 11), RCAR_GP_PIN(6, 12),
};

static const unsigned int i2c2_d_mux[] = {
	SCL2_D_MARK, SDA2_D_MARK,
};

static const unsigned int i2c2_e_pins[] = {
	/* SCL, SDA */
	RCAR_GP_PIN(3, 5), RCAR_GP_PIN(3, 0),
};

static const unsigned int i2c2_e_mux[] = {
	SCL2_E_MARK, SDA2_E_MARK,
};

static const unsigned int i2c4_pins[] = {
	/* SCL, SDA */
	RCAR_GP_PIN(0, 16), RCAR_GP_PIN(0, 17),
};

static const unsigned int i2c4_mux[] = {
	SCL4_MARK, SDA4_MARK,
};

static const unsigned int i2c5_pins[] = {
	/* SCL, SDA */
	RCAR_GP_PIN(1, 21), RCAR_GP_PIN(1, 22),
};

static const unsigned int i2c5_mux[] = {
	SCL5_MARK, SDA5_MARK,
};

static const unsigned int i2c6_a_pins[] = {
	/* SCL, SDA */
	RCAR_GP_PIN(1, 11), RCAR_GP_PIN(1, 8),
};

static const unsigned int i2c6_a_mux[] = {
	SCL6_A_MARK, SDA6_A_MARK,
};

static const unsigned int i2c6_b_pins[] = {
	/* SCL, SDA */
	RCAR_GP_PIN(1, 2), RCAR_GP_PIN(1, 1),
};

static const unsigned int i2c6_b_mux[] = {
	SCL6_B_MARK, SDA6_B_MARK,
};

static const unsigned int i2c7_a_pins[] = {
	/* SCL, SDA */
	RCAR_GP_PIN(2, 24), RCAR_GP_PIN(2, 25),
};

static const unsigned int i2c7_a_mux[] = {
	SCL7_A_MARK, SDA7_A_MARK,
};

static const unsigned int i2c7_b_pins[] = {
	/* SCL, SDA */
	RCAR_GP_PIN(0, 13), RCAR_GP_PIN(0, 14),
};

static const unsigned int i2c7_b_mux[] = {
	SCL7_B_MARK, SDA7_B_MARK,
};

/* - INTC-EX ---------------------------------------------------------------- */
static const unsigned int intc_ex_irq0_pins[] = {
	/* IRQ0 */
	RCAR_GP_PIN(1, 0),
};
static const unsigned int intc_ex_irq0_mux[] = {
	IRQ0_MARK,
};
static const unsigned int intc_ex_irq1_pins[] = {
	/* IRQ1 */
	RCAR_GP_PIN(1, 1),
};
static const unsigned int intc_ex_irq1_mux[] = {
	IRQ1_MARK,
};
static const unsigned int intc_ex_irq2_pins[] = {
	/* IRQ2 */
	RCAR_GP_PIN(1, 2),
};
static const unsigned int intc_ex_irq2_mux[] = {
	IRQ2_MARK,
};
static const unsigned int intc_ex_irq3_pins[] = {
	/* IRQ3 */
	RCAR_GP_PIN(1, 9),
};
static const unsigned int intc_ex_irq3_mux[] = {
	IRQ3_MARK,
};
static const unsigned int intc_ex_irq4_pins[] = {
	/* IRQ4 */
	RCAR_GP_PIN(1, 10),
};
static const unsigned int intc_ex_irq4_mux[] = {
	IRQ4_MARK,
};
static const unsigned int intc_ex_irq5_pins[] = {
	/* IRQ5 */
	RCAR_GP_PIN(0, 7),
};
static const unsigned int intc_ex_irq5_mux[] = {
	IRQ5_MARK,
};

/* - MSIOF0 ----------------------------------------------------------------- */
static const unsigned int msiof0_clk_pins[] = {
	/* SCK */
	RCAR_GP_PIN(5, 10),
};

static const unsigned int msiof0_clk_mux[] = {
	MSIOF0_SCK_MARK,
};

static const unsigned int msiof0_sync_pins[] = {
	/* SYNC */
	RCAR_GP_PIN(5, 13),
};

static const unsigned int msiof0_sync_mux[] = {
	MSIOF0_SYNC_MARK,
};

static const unsigned int msiof0_ss1_pins[] = {
	/* SS1 */
	RCAR_GP_PIN(5, 14),
};

static const unsigned int msiof0_ss1_mux[] = {
	MSIOF0_SS1_MARK,
};

static const unsigned int msiof0_ss2_pins[] = {
	/* SS2 */
	RCAR_GP_PIN(5, 15),
};

static const unsigned int msiof0_ss2_mux[] = {
	MSIOF0_SS2_MARK,
};

static const unsigned int msiof0_txd_pins[] = {
	/* TXD */
	RCAR_GP_PIN(5, 12),
};

static const unsigned int msiof0_txd_mux[] = {
	MSIOF0_TXD_MARK,
};

static const unsigned int msiof0_rxd_pins[] = {
	/* RXD */
	RCAR_GP_PIN(5, 11),
};

static const unsigned int msiof0_rxd_mux[] = {
	MSIOF0_RXD_MARK,
};

/* - MSIOF1 ----------------------------------------------------------------- */
static const unsigned int msiof1_clk_pins[] = {
	/* SCK */
	RCAR_GP_PIN(1, 19),
};

static const unsigned int msiof1_clk_mux[] = {
	MSIOF1_SCK_MARK,
};

static const unsigned int msiof1_sync_pins[] = {
	/* SYNC */
	RCAR_GP_PIN(1, 16),
};

static const unsigned int msiof1_sync_mux[] = {
	MSIOF1_SYNC_MARK,
};

static const unsigned int msiof1_ss1_pins[] = {
	/* SS1 */
	RCAR_GP_PIN(1, 14),
};

static const unsigned int msiof1_ss1_mux[] = {
	MSIOF1_SS1_MARK,
};

static const unsigned int msiof1_ss2_pins[] = {
	/* SS2 */
	RCAR_GP_PIN(1, 15),
};

static const unsigned int msiof1_ss2_mux[] = {
	MSIOF1_SS2_MARK,
};

static const unsigned int msiof1_txd_pins[] = {
	/* TXD */
	RCAR_GP_PIN(1, 18),
};

static const unsigned int msiof1_txd_mux[] = {
	MSIOF1_TXD_MARK,
};

static const unsigned int msiof1_rxd_pins[] = {
	/* RXD */
	RCAR_GP_PIN(1, 17),
};

static const unsigned int msiof1_rxd_mux[] = {
	MSIOF1_RXD_MARK,
};

/* - MSIOF2 ----------------------------------------------------------------- */
static const unsigned int msiof2_clk_a_pins[] = {
	/* SCK */
	RCAR_GP_PIN(0, 8),
};

static const unsigned int msiof2_clk_a_mux[] = {
	MSIOF2_SCK_A_MARK,
};

static const unsigned int msiof2_sync_a_pins[] = {
	/* SYNC */
	RCAR_GP_PIN(0, 9),
};

static const unsigned int msiof2_sync_a_mux[] = {
	MSIOF2_SYNC_A_MARK,
};

static const unsigned int msiof2_ss1_a_pins[] = {
	/* SS1 */
	RCAR_GP_PIN(0, 15),
};

static const unsigned int msiof2_ss1_a_mux[] = {
	MSIOF2_SS1_A_MARK,
};

static const unsigned int msiof2_ss2_a_pins[] = {
	/* SS2 */
	RCAR_GP_PIN(0, 14),
};

static const unsigned int msiof2_ss2_a_mux[] = {
	MSIOF2_SS2_A_MARK,
};

static const unsigned int msiof2_txd_a_pins[] = {
	/* TXD */
	RCAR_GP_PIN(0, 11),
};

static const unsigned int msiof2_txd_a_mux[] = {
	MSIOF2_TXD_A_MARK,
};

static const unsigned int msiof2_rxd_a_pins[] = {
	/* RXD */
	RCAR_GP_PIN(0, 10),
};

static const unsigned int msiof2_rxd_a_mux[] = {
	MSIOF2_RXD_A_MARK,
};

static const unsigned int msiof2_clk_b_pins[] = {
	/* SCK */
	RCAR_GP_PIN(1, 13),
};

static const unsigned int msiof2_clk_b_mux[] = {
	MSIOF2_SCK_B_MARK,
};

static const unsigned int msiof2_sync_b_pins[] = {
	/* SYNC */
	RCAR_GP_PIN(1, 10),
};

static const unsigned int msiof2_sync_b_mux[] = {
	MSIOF2_SYNC_B_MARK,
};

static const unsigned int msiof2_ss1_b_pins[] = {
	/* SS1 */
	RCAR_GP_PIN(1, 16),
};

static const unsigned int msiof2_ss1_b_mux[] = {
	MSIOF2_SS1_B_MARK,
};

static const unsigned int msiof2_ss2_b_pins[] = {
	/* SS2 */
	RCAR_GP_PIN(1, 12),
};

static const unsigned int msiof2_ss2_b_mux[] = {
	MSIOF2_SS2_B_MARK,
};

static const unsigned int msiof2_txd_b_pins[] = {
	/* TXD */
	RCAR_GP_PIN(1, 15),
};

static const unsigned int msiof2_txd_b_mux[] = {
	MSIOF2_TXD_B_MARK,
};

static const unsigned int msiof2_rxd_b_pins[] = {
	/* RXD */
	RCAR_GP_PIN(1, 14),
};

static const unsigned int msiof2_rxd_b_mux[] = {
	MSIOF2_RXD_B_MARK,
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
	RCAR_GP_PIN(0, 15),
};

static const unsigned int msiof3_ss1_a_mux[] = {
	MSIOF3_SS1_A_MARK,
};

static const unsigned int msiof3_ss2_a_pins[] = {
	/* SS2 */
	RCAR_GP_PIN(0, 4),
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
	RCAR_GP_PIN(1, 5),
};

static const unsigned int msiof3_clk_b_mux[] = {
	MSIOF3_SCK_B_MARK,
};

static const unsigned int msiof3_sync_b_pins[] = {
	/* SYNC */
	RCAR_GP_PIN(1, 4),
};

static const unsigned int msiof3_sync_b_mux[] = {
	MSIOF3_SYNC_B_MARK,
};

static const unsigned int msiof3_ss1_b_pins[] = {
	/* SS1 */
	RCAR_GP_PIN(1, 0),
};

static const unsigned int msiof3_ss1_b_mux[] = {
	MSIOF3_SS1_B_MARK,
};

static const unsigned int msiof3_txd_b_pins[] = {
	/* TXD */
	RCAR_GP_PIN(1, 7),
};

static const unsigned int msiof3_txd_b_mux[] = {
	MSIOF3_TXD_B_MARK,
};

static const unsigned int msiof3_rxd_b_pins[] = {
	/* RXD */
	RCAR_GP_PIN(1, 6),
};

static const unsigned int msiof3_rxd_b_mux[] = {
	MSIOF3_RXD_B_MARK,
};

/* - PWM0 --------------------------------------------------------------------*/
static const unsigned int pwm0_a_pins[] = {
	/* PWM */
	RCAR_GP_PIN(2, 22),
};

static const unsigned int pwm0_a_mux[] = {
	PWM0_A_MARK,
};

static const unsigned int pwm0_b_pins[] = {
	/* PWM */
	RCAR_GP_PIN(6, 3),
};

static const unsigned int pwm0_b_mux[] = {
	PWM0_B_MARK,
};

/* - PWM1 --------------------------------------------------------------------*/
static const unsigned int pwm1_a_pins[] = {
	/* PWM */
	RCAR_GP_PIN(2, 23),
};

static const unsigned int pwm1_a_mux[] = {
	PWM1_A_MARK,
};

static const unsigned int pwm1_b_pins[] = {
	/* PWM */
	RCAR_GP_PIN(6, 4),
};

static const unsigned int pwm1_b_mux[] = {
	PWM1_B_MARK,
};

/* - PWM2 --------------------------------------------------------------------*/
static const unsigned int pwm2_a_pins[] = {
	/* PWM */
	RCAR_GP_PIN(1, 0),
};

static const unsigned int pwm2_a_mux[] = {
	PWM2_A_MARK,
};

static const unsigned int pwm2_b_pins[] = {
	/* PWM */
	RCAR_GP_PIN(1, 4),
};

static const unsigned int pwm2_b_mux[] = {
	PWM2_B_MARK,
};

static const unsigned int pwm2_c_pins[] = {
	/* PWM */
	RCAR_GP_PIN(6, 5),
};

static const unsigned int pwm2_c_mux[] = {
	PWM2_C_MARK,
};

/* - PWM3 --------------------------------------------------------------------*/
static const unsigned int pwm3_a_pins[] = {
	/* PWM */
	RCAR_GP_PIN(1, 1),
};

static const unsigned int pwm3_a_mux[] = {
	PWM3_A_MARK,
};

static const unsigned int pwm3_b_pins[] = {
	/* PWM */
	RCAR_GP_PIN(1, 5),
};

static const unsigned int pwm3_b_mux[] = {
	PWM3_B_MARK,
};

static const unsigned int pwm3_c_pins[] = {
	/* PWM */
	RCAR_GP_PIN(6, 6),
};

static const unsigned int pwm3_c_mux[] = {
	PWM3_C_MARK,
};

/* - PWM4 --------------------------------------------------------------------*/
static const unsigned int pwm4_a_pins[] = {
	/* PWM */
	RCAR_GP_PIN(1, 3),
};

static const unsigned int pwm4_a_mux[] = {
	PWM4_A_MARK,
};

static const unsigned int pwm4_b_pins[] = {
	/* PWM */
	RCAR_GP_PIN(6, 7),
};

static const unsigned int pwm4_b_mux[] = {
	PWM4_B_MARK,
};

/* - PWM5 --------------------------------------------------------------------*/
static const unsigned int pwm5_a_pins[] = {
	/* PWM */
	RCAR_GP_PIN(2, 24),
};

static const unsigned int pwm5_a_mux[] = {
	PWM5_A_MARK,
};

static const unsigned int pwm5_b_pins[] = {
	/* PWM */
	RCAR_GP_PIN(6, 10),
};

static const unsigned int pwm5_b_mux[] = {
	PWM5_B_MARK,
};

/* - PWM6 --------------------------------------------------------------------*/
static const unsigned int pwm6_a_pins[] = {
	/* PWM */
	RCAR_GP_PIN(2, 25),
};

static const unsigned int pwm6_a_mux[] = {
	PWM6_A_MARK,
};

static const unsigned int pwm6_b_pins[] = {
	/* PWM */
	RCAR_GP_PIN(6, 11),
};

static const unsigned int pwm6_b_mux[] = {
	PWM6_B_MARK,
};

/* - QSPI0 ------------------------------------------------------------------ */
static const unsigned int qspi0_ctrl_pins[] = {
	/* QSPI0_SPCLK, QSPI0_SSL */
	RCAR_GP_PIN(2, 0), RCAR_GP_PIN(2, 5),
};
static const unsigned int qspi0_ctrl_mux[] = {
	QSPI0_SPCLK_MARK, QSPI0_SSL_MARK,
};
static const unsigned int qspi0_data2_pins[] = {
	/* QSPI0_MOSI_IO0, QSPI0_MISO_IO1 */
	RCAR_GP_PIN(2, 1), RCAR_GP_PIN(2, 2),
};
static const unsigned int qspi0_data2_mux[] = {
	QSPI0_MOSI_IO0_MARK, QSPI0_MISO_IO1_MARK,
};
static const unsigned int qspi0_data4_pins[] = {
	/* QSPI0_MOSI_IO0, QSPI0_MISO_IO1 */
	RCAR_GP_PIN(2, 1), RCAR_GP_PIN(2, 2),
	/* QSPI0_IO2, QSPI0_IO3 */
	RCAR_GP_PIN(2, 3), RCAR_GP_PIN(2, 4),
};
static const unsigned int qspi0_data4_mux[] = {
	QSPI0_MOSI_IO0_MARK, QSPI0_MISO_IO1_MARK,
	QSPI0_IO2_MARK, QSPI0_IO3_MARK,
};
/* - QSPI1 ------------------------------------------------------------------ */
static const unsigned int qspi1_ctrl_pins[] = {
	/* QSPI1_SPCLK, QSPI1_SSL */
	RCAR_GP_PIN(2, 6), RCAR_GP_PIN(2, 11),
};
static const unsigned int qspi1_ctrl_mux[] = {
	QSPI1_SPCLK_MARK, QSPI1_SSL_MARK,
};
static const unsigned int qspi1_data2_pins[] = {
	/* QSPI1_MOSI_IO0, QSPI1_MISO_IO1 */
	RCAR_GP_PIN(2, 7), RCAR_GP_PIN(2, 8),
};
static const unsigned int qspi1_data2_mux[] = {
	QSPI1_MOSI_IO0_MARK, QSPI1_MISO_IO1_MARK,
};
static const unsigned int qspi1_data4_pins[] = {
	/* QSPI1_MOSI_IO0, QSPI1_MISO_IO1 */
	RCAR_GP_PIN(2, 7), RCAR_GP_PIN(2, 8),
	/* QSPI1_IO2, QSPI1_IO3 */
	RCAR_GP_PIN(2, 9), RCAR_GP_PIN(2, 10),
};
static const unsigned int qspi1_data4_mux[] = {
	QSPI1_MOSI_IO0_MARK, QSPI1_MISO_IO1_MARK,
	QSPI1_IO2_MARK, QSPI1_IO3_MARK,
};

/* - SCIF0 ------------------------------------------------------------------ */
static const unsigned int scif0_data_a_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(5, 1), RCAR_GP_PIN(5, 2),
};

static const unsigned int scif0_data_a_mux[] = {
	RX0_A_MARK, TX0_A_MARK,
};

static const unsigned int scif0_clk_a_pins[] = {
	/* SCK */
	RCAR_GP_PIN(5, 0),
};

static const unsigned int scif0_clk_a_mux[] = {
	SCK0_A_MARK,
};

static const unsigned int scif0_ctrl_a_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(5, 4), RCAR_GP_PIN(5, 3),
};

static const unsigned int scif0_ctrl_a_mux[] = {
	RTS0_N_A_MARK, CTS0_N_A_MARK,
};

static const unsigned int scif0_data_b_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(5, 17), RCAR_GP_PIN(5, 19),
};

static const unsigned int scif0_data_b_mux[] = {
	RX0_B_MARK, TX0_B_MARK,
};

static const unsigned int scif0_clk_b_pins[] = {
	/* SCK */
	RCAR_GP_PIN(5, 18),
};

static const unsigned int scif0_clk_b_mux[] = {
	SCK0_B_MARK,
};

/* - SCIF1 ------------------------------------------------------------------ */
static const unsigned int scif1_data_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(5, 5), RCAR_GP_PIN(5, 6),
};

static const unsigned int scif1_data_mux[] = {
	RX1_MARK, TX1_MARK,
};

static const unsigned int scif1_clk_pins[] = {
	/* SCK */
	RCAR_GP_PIN(5, 16),
};

static const unsigned int scif1_clk_mux[] = {
	SCK1_MARK,
};

static const unsigned int scif1_ctrl_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(5, 0), RCAR_GP_PIN(5, 7),
};

static const unsigned int scif1_ctrl_mux[] = {
	RTS1_N_MARK, CTS1_N_MARK,
};

/* - SCIF2 ------------------------------------------------------------------ */
static const unsigned int scif2_data_a_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(5, 9), RCAR_GP_PIN(5, 8),
};

static const unsigned int scif2_data_a_mux[] = {
	RX2_A_MARK, TX2_A_MARK,
};

static const unsigned int scif2_clk_a_pins[] = {
	/* SCK */
	RCAR_GP_PIN(5, 7),
};

static const unsigned int scif2_clk_a_mux[] = {
	SCK2_A_MARK,
};

static const unsigned int scif2_data_b_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(5, 12), RCAR_GP_PIN(5, 11),
};

static const unsigned int scif2_data_b_mux[] = {
	RX2_B_MARK, TX2_B_MARK,
};

/* - SCIF3 ------------------------------------------------------------------ */
static const unsigned int scif3_data_a_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(0, 5), RCAR_GP_PIN(0, 6),
};

static const unsigned int scif3_data_a_mux[] = {
	RX3_A_MARK, TX3_A_MARK,
};

static const unsigned int scif3_clk_a_pins[] = {
	/* SCK */
	RCAR_GP_PIN(0, 1),
};

static const unsigned int scif3_clk_a_mux[] = {
	SCK3_A_MARK,
};

static const unsigned int scif3_ctrl_a_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(0, 4), RCAR_GP_PIN(0, 7),
};

static const unsigned int scif3_ctrl_a_mux[] = {
	RTS3_N_A_MARK, CTS3_N_A_MARK,
};

static const unsigned int scif3_data_b_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(1, 8), RCAR_GP_PIN(1, 11),
};

static const unsigned int scif3_data_b_mux[] = {
	RX3_B_MARK, TX3_B_MARK,
};

static const unsigned int scif3_data_c_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(2, 23), RCAR_GP_PIN(2, 22),
};

static const unsigned int scif3_data_c_mux[] = {
	RX3_C_MARK, TX3_C_MARK,
};

static const unsigned int scif3_clk_c_pins[] = {
	/* SCK */
	RCAR_GP_PIN(2, 24),
};

static const unsigned int scif3_clk_c_mux[] = {
	SCK3_C_MARK,
};

/* - SCIF4 ------------------------------------------------------------------ */
static const unsigned int scif4_data_a_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(1, 6), RCAR_GP_PIN(1, 7),
};

static const unsigned int scif4_data_a_mux[] = {
	RX4_A_MARK, TX4_A_MARK,
};

static const unsigned int scif4_clk_a_pins[] = {
	/* SCK */
	RCAR_GP_PIN(1, 5),
};

static const unsigned int scif4_clk_a_mux[] = {
	SCK4_A_MARK,
};

static const unsigned int scif4_ctrl_a_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(1, 4), RCAR_GP_PIN(1, 3),
};

static const unsigned int scif4_ctrl_a_mux[] = {
	RTS4_N_A_MARK, CTS4_N_A_MARK,
};

static const unsigned int scif4_data_b_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(0, 13), RCAR_GP_PIN(0, 12),
};

static const unsigned int scif4_data_b_mux[] = {
	RX4_B_MARK, TX4_B_MARK,
};

static const unsigned int scif4_clk_b_pins[] = {
	/* SCK */
	RCAR_GP_PIN(0, 8),
};

static const unsigned int scif4_clk_b_mux[] = {
	SCK4_B_MARK,
};

static const unsigned int scif4_data_c_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(0, 2), RCAR_GP_PIN(0, 3),
};

static const unsigned int scif4_data_c_mux[] = {
	RX4_C_MARK, TX4_C_MARK,
};

static const unsigned int scif4_ctrl_c_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(0, 1), RCAR_GP_PIN(0, 0),
};

static const unsigned int scif4_ctrl_c_mux[] = {
	RTS4_N_C_MARK, CTS4_N_C_MARK,
};

/* - SCIF5 ------------------------------------------------------------------ */
static const unsigned int scif5_data_a_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(1, 12), RCAR_GP_PIN(1, 9),
};

static const unsigned int scif5_data_a_mux[] = {
	RX5_A_MARK, TX5_A_MARK,
};

static const unsigned int scif5_clk_a_pins[] = {
	/* SCK */
	RCAR_GP_PIN(1, 13),
};

static const unsigned int scif5_clk_a_mux[] = {
	SCK5_A_MARK,
};

static const unsigned int scif5_data_b_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(2, 25), RCAR_GP_PIN(2, 24),
};

static const unsigned int scif5_data_b_mux[] = {
	RX5_B_MARK, TX5_B_MARK,
};

static const unsigned int scif5_data_c_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(0, 2), RCAR_GP_PIN(0, 3),
};

static const unsigned int scif5_data_c_mux[] = {
	RX5_C_MARK, TX5_C_MARK,
};

/* - SCIF Clock ------------------------------------------------------------- */
static const unsigned int scif_clk_a_pins[] = {
	/* SCIF_CLK */
	RCAR_GP_PIN(5, 3),
};

static const unsigned int scif_clk_a_mux[] = {
	SCIF_CLK_A_MARK,
};

static const unsigned int scif_clk_b_pins[] = {
	/* SCIF_CLK */
	RCAR_GP_PIN(5, 7),
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

/* - SDHI3 ------------------------------------------------------------------ */
static const unsigned int sdhi3_data1_pins[] = {
	/* D0 */
	RCAR_GP_PIN(4, 2),
};

static const unsigned int sdhi3_data1_mux[] = {
	SD3_DAT0_MARK,
};

static const unsigned int sdhi3_data4_pins[] = {
	/* D[0:3] */
	RCAR_GP_PIN(4, 2), RCAR_GP_PIN(4, 3),
	RCAR_GP_PIN(4, 4), RCAR_GP_PIN(4, 5),
};

static const unsigned int sdhi3_data4_mux[] = {
	SD3_DAT0_MARK, SD3_DAT1_MARK,
	SD3_DAT2_MARK, SD3_DAT3_MARK,
};

static const unsigned int sdhi3_data8_pins[] = {
	/* D[0:7] */
	RCAR_GP_PIN(4, 2), RCAR_GP_PIN(4, 3),
	RCAR_GP_PIN(4, 4), RCAR_GP_PIN(4, 5),
	RCAR_GP_PIN(4, 6), RCAR_GP_PIN(4, 7),
	RCAR_GP_PIN(4, 8), RCAR_GP_PIN(4, 9),
};

static const unsigned int sdhi3_data8_mux[] = {
	SD3_DAT0_MARK, SD3_DAT1_MARK,
	SD3_DAT2_MARK, SD3_DAT3_MARK,
	SD3_DAT4_MARK, SD3_DAT5_MARK,
	SD3_DAT6_MARK, SD3_DAT7_MARK,
};

static const unsigned int sdhi3_ctrl_pins[] = {
	/* CLK, CMD */
	RCAR_GP_PIN(4, 0), RCAR_GP_PIN(4, 1),
};

static const unsigned int sdhi3_ctrl_mux[] = {
	SD3_CLK_MARK, SD3_CMD_MARK,
};

static const unsigned int sdhi3_cd_pins[] = {
	/* CD */
	RCAR_GP_PIN(3, 12),
};

static const unsigned int sdhi3_cd_mux[] = {
	SD3_CD_MARK,
};

static const unsigned int sdhi3_wp_pins[] = {
	/* WP */
	RCAR_GP_PIN(3, 13),
};

static const unsigned int sdhi3_wp_mux[] = {
	SD3_WP_MARK,
};

static const unsigned int sdhi3_ds_pins[] = {
	/* DS */
	RCAR_GP_PIN(4, 10),
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

static const unsigned int ssi1_data_pins[] = {
	/* SDATA */
	RCAR_GP_PIN(6, 3),
};

static const unsigned int ssi1_data_mux[] = {
	SSI_SDATA1_MARK,
};

static const unsigned int ssi1_ctrl_pins[] = {
	/* SCK, WS */
	RCAR_GP_PIN(3, 14), RCAR_GP_PIN(3, 15),
};

static const unsigned int ssi1_ctrl_mux[] = {
	SSI_SCK1_MARK, SSI_WS1_MARK,
};

static const unsigned int ssi2_data_pins[] = {
	/* SDATA */
	RCAR_GP_PIN(6, 4),
};

static const unsigned int ssi2_data_mux[] = {
	SSI_SDATA2_MARK,
};

static const unsigned int ssi2_ctrl_a_pins[] = {
	/* SCK, WS */
	RCAR_GP_PIN(5, 1), RCAR_GP_PIN(5, 2),
};

static const unsigned int ssi2_ctrl_a_mux[] = {
	SSI_SCK2_A_MARK, SSI_WS2_A_MARK,
};

static const unsigned int ssi2_ctrl_b_pins[] = {
	/* SCK, WS */
	RCAR_GP_PIN(3, 12), RCAR_GP_PIN(3, 13),
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
	RCAR_GP_PIN(5, 14), RCAR_GP_PIN(5, 15),
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
	RCAR_GP_PIN(5, 12),
};

static const unsigned int ssi7_data_mux[] = {
	SSI_SDATA7_MARK,
};

static const unsigned int ssi78_ctrl_pins[] = {
	/* SCK, WS */
	RCAR_GP_PIN(5, 10), RCAR_GP_PIN(5, 11),
};

static const unsigned int ssi78_ctrl_mux[] = {
	SSI_SCK78_MARK, SSI_WS78_MARK,
};

static const unsigned int ssi8_data_pins[] = {
	/* SDATA */
	RCAR_GP_PIN(5, 13),
};

static const unsigned int ssi8_data_mux[] = {
	SSI_SDATA8_MARK,
};

static const unsigned int ssi9_data_pins[] = {
	/* SDATA */
	RCAR_GP_PIN(5, 16),
};

static const unsigned int ssi9_data_mux[] = {
	SSI_SDATA9_MARK,
};

static const unsigned int ssi9_ctrl_a_pins[] = {
	/* SCK, WS */
	RCAR_GP_PIN(6, 4), RCAR_GP_PIN(6, 10),
};

static const unsigned int ssi9_ctrl_a_mux[] = {
	SSI_SCK9_A_MARK, SSI_WS9_A_MARK,
};

static const unsigned int ssi9_ctrl_b_pins[] = {
	/* SCK, WS */
	RCAR_GP_PIN(5, 5), RCAR_GP_PIN(5, 6),
};

static const unsigned int ssi9_ctrl_b_mux[] = {
	SSI_SCK9_B_MARK, SSI_WS9_B_MARK,
};

/* - TMU -------------------------------------------------------------------- */
static const unsigned int tmu_tclk1_a_pins[] = {
	/* TCLK */
	RCAR_GP_PIN(3, 12),
};

static const unsigned int tmu_tclk1_a_mux[] = {
	TCLK1_A_MARK,
};

static const unsigned int tmu_tclk1_b_pins[] = {
	/* TCLK */
	RCAR_GP_PIN(5, 17),
};

static const unsigned int tmu_tclk1_b_mux[] = {
	TCLK1_B_MARK,
};

static const unsigned int tmu_tclk2_a_pins[] = {
	/* TCLK */
	RCAR_GP_PIN(3, 13),
};

static const unsigned int tmu_tclk2_a_mux[] = {
	TCLK2_A_MARK,
};

static const unsigned int tmu_tclk2_b_pins[] = {
	/* TCLK */
	RCAR_GP_PIN(5, 18),
};

static const unsigned int tmu_tclk2_b_mux[] = {
	TCLK2_B_MARK,
};

/* - USB0 ------------------------------------------------------------------- */
static const unsigned int usb0_a_pins[] = {
	/* PWEN, OVC */
	RCAR_GP_PIN(6, 17), RCAR_GP_PIN(6, 9),
};

static const unsigned int usb0_a_mux[] = {
	USB0_PWEN_A_MARK, USB0_OVC_A_MARK,
};

static const unsigned int usb0_b_pins[] = {
	/* PWEN, OVC */
	RCAR_GP_PIN(6, 11), RCAR_GP_PIN(6, 12),
};

static const unsigned int usb0_b_mux[] = {
	USB0_PWEN_B_MARK, USB0_OVC_B_MARK,
};

static const unsigned int usb0_id_pins[] = {
	/* ID */
	RCAR_GP_PIN(5, 0)
};

static const unsigned int usb0_id_mux[] = {
	USB0_ID_MARK,
};

/* - USB30 ------------------------------------------------------------------ */
static const unsigned int usb30_pins[] = {
	/* PWEN, OVC */
	RCAR_GP_PIN(6, 17), RCAR_GP_PIN(6, 9),
};

static const unsigned int usb30_mux[] = {
	USB30_PWEN_MARK, USB30_OVC_MARK,
};

static const unsigned int usb30_id_pins[] = {
	/* ID */
	RCAR_GP_PIN(5, 0),
};

static const unsigned int usb30_id_mux[] = {
	USB3HS0_ID_MARK,
};

/* - VIN4 ------------------------------------------------------------------- */
static const unsigned int vin4_data18_a_pins[] = {
	RCAR_GP_PIN(2, 8),  RCAR_GP_PIN(2, 9),
	RCAR_GP_PIN(2, 10), RCAR_GP_PIN(2, 11),
	RCAR_GP_PIN(2, 12), RCAR_GP_PIN(2, 13),
	RCAR_GP_PIN(1, 6),  RCAR_GP_PIN(1, 7),
	RCAR_GP_PIN(1, 3),  RCAR_GP_PIN(1, 10),
	RCAR_GP_PIN(1, 13), RCAR_GP_PIN(1, 14),
	RCAR_GP_PIN(1, 15), RCAR_GP_PIN(1, 16),
	RCAR_GP_PIN(1, 17), RCAR_GP_PIN(1, 18),
	RCAR_GP_PIN(1, 19), RCAR_GP_PIN(0, 1),
};

static const unsigned int vin4_data18_a_mux[] = {
	VI4_DATA2_A_MARK, VI4_DATA3_A_MARK,
	VI4_DATA4_A_MARK, VI4_DATA5_A_MARK,
	VI4_DATA6_A_MARK, VI4_DATA7_A_MARK,
	VI4_DATA10_MARK,  VI4_DATA11_MARK,
	VI4_DATA12_MARK,  VI4_DATA13_MARK,
	VI4_DATA14_MARK,  VI4_DATA15_MARK,
	VI4_DATA18_MARK,  VI4_DATA19_MARK,
	VI4_DATA20_MARK,  VI4_DATA21_MARK,
	VI4_DATA22_MARK,  VI4_DATA23_MARK,
};

static const union vin_data vin4_data_a_pins = {
	.data24 = {
		RCAR_GP_PIN(2, 6),  RCAR_GP_PIN(2, 7),
		RCAR_GP_PIN(2, 8),  RCAR_GP_PIN(2, 9),
		RCAR_GP_PIN(2, 10), RCAR_GP_PIN(2, 11),
		RCAR_GP_PIN(2, 12), RCAR_GP_PIN(2, 13),
		RCAR_GP_PIN(1, 4),  RCAR_GP_PIN(1, 5),
		RCAR_GP_PIN(1, 6),  RCAR_GP_PIN(1, 7),
		RCAR_GP_PIN(1, 3),  RCAR_GP_PIN(1, 10),
		RCAR_GP_PIN(1, 13), RCAR_GP_PIN(1, 14),
		RCAR_GP_PIN(1, 9),  RCAR_GP_PIN(1, 12),
		RCAR_GP_PIN(1, 15), RCAR_GP_PIN(1, 16),
		RCAR_GP_PIN(1, 17), RCAR_GP_PIN(1, 18),
		RCAR_GP_PIN(1, 19), RCAR_GP_PIN(0, 1),
	},
};

static const union vin_data vin4_data_a_mux = {
	.data24 = {
		VI4_DATA0_A_MARK, VI4_DATA1_A_MARK,
		VI4_DATA2_A_MARK, VI4_DATA3_A_MARK,
		VI4_DATA4_A_MARK, VI4_DATA5_A_MARK,
		VI4_DATA6_A_MARK, VI4_DATA7_A_MARK,
		VI4_DATA8_MARK,   VI4_DATA9_MARK,
		VI4_DATA10_MARK,  VI4_DATA11_MARK,
		VI4_DATA12_MARK,  VI4_DATA13_MARK,
		VI4_DATA14_MARK,  VI4_DATA15_MARK,
		VI4_DATA16_MARK,  VI4_DATA17_MARK,
		VI4_DATA18_MARK,  VI4_DATA19_MARK,
		VI4_DATA20_MARK,  VI4_DATA21_MARK,
		VI4_DATA22_MARK,  VI4_DATA23_MARK,
	},
};

static const unsigned int vin4_data18_b_pins[] = {
	RCAR_GP_PIN(1, 21), RCAR_GP_PIN(1, 22),
	RCAR_GP_PIN(0, 5),  RCAR_GP_PIN(0, 6),
	RCAR_GP_PIN(0, 16), RCAR_GP_PIN(0, 17),
	RCAR_GP_PIN(1, 6),  RCAR_GP_PIN(1, 7),
	RCAR_GP_PIN(1, 3),  RCAR_GP_PIN(1, 10),
	RCAR_GP_PIN(1, 13), RCAR_GP_PIN(1, 14),
	RCAR_GP_PIN(1, 15), RCAR_GP_PIN(1, 16),
	RCAR_GP_PIN(1, 17), RCAR_GP_PIN(1, 18),
	RCAR_GP_PIN(1, 19), RCAR_GP_PIN(0, 1),
};

static const unsigned int vin4_data18_b_mux[] = {
	VI4_DATA2_B_MARK, VI4_DATA3_B_MARK,
	VI4_DATA4_B_MARK, VI4_DATA5_B_MARK,
	VI4_DATA6_B_MARK, VI4_DATA7_B_MARK,
	VI4_DATA10_MARK,  VI4_DATA11_MARK,
	VI4_DATA12_MARK,  VI4_DATA13_MARK,
	VI4_DATA14_MARK,  VI4_DATA15_MARK,
	VI4_DATA18_MARK,  VI4_DATA19_MARK,
	VI4_DATA20_MARK,  VI4_DATA21_MARK,
	VI4_DATA22_MARK,  VI4_DATA23_MARK,
};

static const union vin_data vin4_data_b_pins = {
	.data24 = {
		RCAR_GP_PIN(1, 8),  RCAR_GP_PIN(1, 11),
		RCAR_GP_PIN(1, 21), RCAR_GP_PIN(1, 22),
		RCAR_GP_PIN(0, 5),  RCAR_GP_PIN(0, 6),
		RCAR_GP_PIN(0, 16), RCAR_GP_PIN(0, 17),
		RCAR_GP_PIN(1, 4),  RCAR_GP_PIN(1, 5),
		RCAR_GP_PIN(1, 6),  RCAR_GP_PIN(1, 7),
		RCAR_GP_PIN(1, 3),  RCAR_GP_PIN(1, 10),
		RCAR_GP_PIN(1, 13), RCAR_GP_PIN(1, 14),
		RCAR_GP_PIN(1, 9),  RCAR_GP_PIN(1, 12),
		RCAR_GP_PIN(1, 15), RCAR_GP_PIN(1, 16),
		RCAR_GP_PIN(1, 17), RCAR_GP_PIN(1, 18),
		RCAR_GP_PIN(1, 19), RCAR_GP_PIN(0, 1),
	},
};

static const union vin_data vin4_data_b_mux = {
	.data24 = {
		VI4_DATA0_B_MARK, VI4_DATA1_B_MARK,
		VI4_DATA2_B_MARK, VI4_DATA3_B_MARK,
		VI4_DATA4_B_MARK, VI4_DATA5_B_MARK,
		VI4_DATA6_B_MARK, VI4_DATA7_B_MARK,
		VI4_DATA8_MARK,   VI4_DATA9_MARK,
		VI4_DATA10_MARK,  VI4_DATA11_MARK,
		VI4_DATA12_MARK,  VI4_DATA13_MARK,
		VI4_DATA14_MARK,  VI4_DATA15_MARK,
		VI4_DATA16_MARK,  VI4_DATA17_MARK,
		VI4_DATA18_MARK,  VI4_DATA19_MARK,
		VI4_DATA20_MARK,  VI4_DATA21_MARK,
		VI4_DATA22_MARK,  VI4_DATA23_MARK,
	},
};

static const unsigned int vin4_sync_pins[] = {
	/* HSYNC, VSYNC */
	RCAR_GP_PIN(2, 25), RCAR_GP_PIN(2, 24),
};

static const unsigned int vin4_sync_mux[] = {
	VI4_HSYNC_N_MARK, VI4_VSYNC_N_MARK,
};

static const unsigned int vin4_field_pins[] = {
	RCAR_GP_PIN(2, 23),
};

static const unsigned int vin4_field_mux[] = {
	VI4_FIELD_MARK,
};

static const unsigned int vin4_clkenb_pins[] = {
	RCAR_GP_PIN(1, 2),
};

static const unsigned int vin4_clkenb_mux[] = {
	VI4_CLKENB_MARK,
};

static const unsigned int vin4_clk_pins[] = {
	RCAR_GP_PIN(2, 22),
};

static const unsigned int vin4_clk_mux[] = {
	VI4_CLK_MARK,
};

/* - VIN5 ------------------------------------------------------------------- */
static const union vin_data16 vin5_data_a_pins = {
	.data16 = {
		RCAR_GP_PIN(1, 1),  RCAR_GP_PIN(1, 2),
		RCAR_GP_PIN(1, 19), RCAR_GP_PIN(1, 12),
		RCAR_GP_PIN(1, 15), RCAR_GP_PIN(1, 16),
		RCAR_GP_PIN(1, 17), RCAR_GP_PIN(1, 18),
		RCAR_GP_PIN(0, 12), RCAR_GP_PIN(0, 13),
		RCAR_GP_PIN(0, 9),  RCAR_GP_PIN(0, 11),
		RCAR_GP_PIN(0, 8),  RCAR_GP_PIN(0, 10),
		RCAR_GP_PIN(0, 2),  RCAR_GP_PIN(0, 3),
	},
};

static const union vin_data16 vin5_data_a_mux = {
	.data16 = {
		VI5_DATA0_A_MARK,  VI5_DATA1_A_MARK,
		VI5_DATA2_A_MARK,  VI5_DATA3_A_MARK,
		VI5_DATA4_A_MARK,  VI5_DATA5_A_MARK,
		VI5_DATA6_A_MARK,  VI5_DATA7_A_MARK,
		VI5_DATA8_A_MARK,  VI5_DATA9_A_MARK,
		VI5_DATA10_A_MARK, VI5_DATA11_A_MARK,
		VI5_DATA12_A_MARK, VI5_DATA13_A_MARK,
		VI5_DATA14_A_MARK, VI5_DATA15_A_MARK,
	},
};

static const unsigned int vin5_data8_b_pins[] = {
	RCAR_GP_PIN(2, 23), RCAR_GP_PIN(0, 4),
	RCAR_GP_PIN(0, 7),  RCAR_GP_PIN(0, 12),
	RCAR_GP_PIN(0, 13), RCAR_GP_PIN(0, 14),
	RCAR_GP_PIN(0, 16), RCAR_GP_PIN(0, 17),
};

static const unsigned int vin5_data8_b_mux[] = {
	VI5_DATA0_B_MARK,  VI5_DATA1_B_MARK,
	VI5_DATA2_B_MARK,  VI5_DATA3_B_MARK,
	VI5_DATA4_B_MARK,  VI5_DATA5_B_MARK,
	VI5_DATA6_B_MARK,  VI5_DATA7_B_MARK,
};

static const unsigned int vin5_sync_a_pins[] = {
	/* HSYNC_N, VSYNC_N */
	RCAR_GP_PIN(1, 8), RCAR_GP_PIN(1, 9),
};

static const unsigned int vin5_sync_a_mux[] = {
	VI5_HSYNC_N_A_MARK, VI5_VSYNC_N_A_MARK,
};

static const unsigned int vin5_field_a_pins[] = {
	RCAR_GP_PIN(1, 10),
};

static const unsigned int vin5_field_a_mux[] = {
	VI5_FIELD_A_MARK,
};

static const unsigned int vin5_clkenb_a_pins[] = {
	RCAR_GP_PIN(0, 1),
};

static const unsigned int vin5_clkenb_a_mux[] = {
	VI5_CLKENB_A_MARK,
};

static const unsigned int vin5_clk_a_pins[] = {
	RCAR_GP_PIN(1, 0),
};

static const unsigned int vin5_clk_a_mux[] = {
	VI5_CLK_A_MARK,
};

static const unsigned int vin5_clk_b_pins[] = {
	RCAR_GP_PIN(2, 22),
};

static const unsigned int vin5_clk_b_mux[] = {
	VI5_CLK_B_MARK,
};

static const struct {
	struct sh_pfc_pin_group common[253];
#ifdef CONFIG_PINCTRL_PFC_R8A77990
	struct sh_pfc_pin_group automotive[21];
#endif
} pinmux_groups = {
	.common = {
		SH_PFC_PIN_GROUP(audio_clk_a),
		SH_PFC_PIN_GROUP(audio_clk_b_a),
		SH_PFC_PIN_GROUP(audio_clk_b_b),
		SH_PFC_PIN_GROUP(audio_clk_b_c),
		SH_PFC_PIN_GROUP(audio_clk_c_a),
		SH_PFC_PIN_GROUP(audio_clk_c_b),
		SH_PFC_PIN_GROUP(audio_clk_c_c),
		SH_PFC_PIN_GROUP(audio_clkout_a),
		SH_PFC_PIN_GROUP(audio_clkout_b),
		SH_PFC_PIN_GROUP(audio_clkout1_a),
		SH_PFC_PIN_GROUP(audio_clkout1_b),
		SH_PFC_PIN_GROUP(audio_clkout1_c),
		SH_PFC_PIN_GROUP(audio_clkout2_a),
		SH_PFC_PIN_GROUP(audio_clkout2_b),
		SH_PFC_PIN_GROUP(audio_clkout2_c),
		SH_PFC_PIN_GROUP(audio_clkout3_a),
		SH_PFC_PIN_GROUP(audio_clkout3_b),
		SH_PFC_PIN_GROUP(audio_clkout3_c),
		SH_PFC_PIN_GROUP(avb_link),
		SH_PFC_PIN_GROUP(avb_magic),
		SH_PFC_PIN_GROUP(avb_phy_int),
		SH_PFC_PIN_GROUP(avb_mii),
		SH_PFC_PIN_GROUP(avb_avtp_pps),
		SH_PFC_PIN_GROUP(avb_avtp_match),
		SH_PFC_PIN_GROUP(avb_avtp_capture),
		SH_PFC_PIN_GROUP(can0_data),
		SH_PFC_PIN_GROUP(can1_data),
		SH_PFC_PIN_GROUP(can_clk),
		SH_PFC_PIN_GROUP(canfd0_data),
		SH_PFC_PIN_GROUP(canfd1_data),
		SH_PFC_PIN_GROUP(du_rgb666),
		SH_PFC_PIN_GROUP(du_rgb888),
		SH_PFC_PIN_GROUP(du_clk_in_0),
		SH_PFC_PIN_GROUP(du_clk_in_1),
		SH_PFC_PIN_GROUP(du_clk_out_0),
		SH_PFC_PIN_GROUP(du_sync),
		SH_PFC_PIN_GROUP(du_disp_cde),
		SH_PFC_PIN_GROUP(du_cde),
		SH_PFC_PIN_GROUP(du_disp),
		SH_PFC_PIN_GROUP(hscif0_data_a),
		SH_PFC_PIN_GROUP(hscif0_clk_a),
		SH_PFC_PIN_GROUP(hscif0_ctrl_a),
		SH_PFC_PIN_GROUP(hscif0_data_b),
		SH_PFC_PIN_GROUP(hscif0_clk_b),
		SH_PFC_PIN_GROUP(hscif1_data_a),
		SH_PFC_PIN_GROUP(hscif1_clk_a),
		SH_PFC_PIN_GROUP(hscif1_data_b),
		SH_PFC_PIN_GROUP(hscif1_clk_b),
		SH_PFC_PIN_GROUP(hscif1_ctrl_b),
		SH_PFC_PIN_GROUP(hscif2_data_a),
		SH_PFC_PIN_GROUP(hscif2_clk_a),
		SH_PFC_PIN_GROUP(hscif2_ctrl_a),
		SH_PFC_PIN_GROUP(hscif2_data_b),
		SH_PFC_PIN_GROUP(hscif3_data_a),
		SH_PFC_PIN_GROUP(hscif3_data_b),
		SH_PFC_PIN_GROUP(hscif3_clk_b),
		SH_PFC_PIN_GROUP(hscif3_data_c),
		SH_PFC_PIN_GROUP(hscif3_clk_c),
		SH_PFC_PIN_GROUP(hscif3_ctrl_c),
		SH_PFC_PIN_GROUP(hscif3_data_d),
		SH_PFC_PIN_GROUP(hscif3_data_e),
		SH_PFC_PIN_GROUP(hscif3_ctrl_e),
		SH_PFC_PIN_GROUP(hscif4_data_a),
		SH_PFC_PIN_GROUP(hscif4_clk_a),
		SH_PFC_PIN_GROUP(hscif4_ctrl_a),
		SH_PFC_PIN_GROUP(hscif4_data_b),
		SH_PFC_PIN_GROUP(hscif4_clk_b),
		SH_PFC_PIN_GROUP(hscif4_data_c),
		SH_PFC_PIN_GROUP(hscif4_data_d),
		SH_PFC_PIN_GROUP(hscif4_data_e),
		SH_PFC_PIN_GROUP(i2c1_a),
		SH_PFC_PIN_GROUP(i2c1_b),
		SH_PFC_PIN_GROUP(i2c1_c),
		SH_PFC_PIN_GROUP(i2c1_d),
		SH_PFC_PIN_GROUP(i2c2_a),
		SH_PFC_PIN_GROUP(i2c2_b),
		SH_PFC_PIN_GROUP(i2c2_c),
		SH_PFC_PIN_GROUP(i2c2_d),
		SH_PFC_PIN_GROUP(i2c2_e),
		SH_PFC_PIN_GROUP(i2c4),
		SH_PFC_PIN_GROUP(i2c5),
		SH_PFC_PIN_GROUP(i2c6_a),
		SH_PFC_PIN_GROUP(i2c6_b),
		SH_PFC_PIN_GROUP(i2c7_a),
		SH_PFC_PIN_GROUP(i2c7_b),
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
		SH_PFC_PIN_GROUP(msiof1_clk),
		SH_PFC_PIN_GROUP(msiof1_sync),
		SH_PFC_PIN_GROUP(msiof1_ss1),
		SH_PFC_PIN_GROUP(msiof1_ss2),
		SH_PFC_PIN_GROUP(msiof1_txd),
		SH_PFC_PIN_GROUP(msiof1_rxd),
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
		SH_PFC_PIN_GROUP(msiof3_clk_a),
		SH_PFC_PIN_GROUP(msiof3_sync_a),
		SH_PFC_PIN_GROUP(msiof3_ss1_a),
		SH_PFC_PIN_GROUP(msiof3_ss2_a),
		SH_PFC_PIN_GROUP(msiof3_txd_a),
		SH_PFC_PIN_GROUP(msiof3_rxd_a),
		SH_PFC_PIN_GROUP(msiof3_clk_b),
		SH_PFC_PIN_GROUP(msiof3_sync_b),
		SH_PFC_PIN_GROUP(msiof3_ss1_b),
		SH_PFC_PIN_GROUP(msiof3_txd_b),
		SH_PFC_PIN_GROUP(msiof3_rxd_b),
		SH_PFC_PIN_GROUP(pwm0_a),
		SH_PFC_PIN_GROUP(pwm0_b),
		SH_PFC_PIN_GROUP(pwm1_a),
		SH_PFC_PIN_GROUP(pwm1_b),
		SH_PFC_PIN_GROUP(pwm2_a),
		SH_PFC_PIN_GROUP(pwm2_b),
		SH_PFC_PIN_GROUP(pwm2_c),
		SH_PFC_PIN_GROUP(pwm3_a),
		SH_PFC_PIN_GROUP(pwm3_b),
		SH_PFC_PIN_GROUP(pwm3_c),
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
		SH_PFC_PIN_GROUP(scif0_data_a),
		SH_PFC_PIN_GROUP(scif0_clk_a),
		SH_PFC_PIN_GROUP(scif0_ctrl_a),
		SH_PFC_PIN_GROUP(scif0_data_b),
		SH_PFC_PIN_GROUP(scif0_clk_b),
		SH_PFC_PIN_GROUP(scif1_data),
		SH_PFC_PIN_GROUP(scif1_clk),
		SH_PFC_PIN_GROUP(scif1_ctrl),
		SH_PFC_PIN_GROUP(scif2_data_a),
		SH_PFC_PIN_GROUP(scif2_clk_a),
		SH_PFC_PIN_GROUP(scif2_data_b),
		SH_PFC_PIN_GROUP(scif3_data_a),
		SH_PFC_PIN_GROUP(scif3_clk_a),
		SH_PFC_PIN_GROUP(scif3_ctrl_a),
		SH_PFC_PIN_GROUP(scif3_data_b),
		SH_PFC_PIN_GROUP(scif3_data_c),
		SH_PFC_PIN_GROUP(scif3_clk_c),
		SH_PFC_PIN_GROUP(scif4_data_a),
		SH_PFC_PIN_GROUP(scif4_clk_a),
		SH_PFC_PIN_GROUP(scif4_ctrl_a),
		SH_PFC_PIN_GROUP(scif4_data_b),
		SH_PFC_PIN_GROUP(scif4_clk_b),
		SH_PFC_PIN_GROUP(scif4_data_c),
		SH_PFC_PIN_GROUP(scif4_ctrl_c),
		SH_PFC_PIN_GROUP(scif5_data_a),
		SH_PFC_PIN_GROUP(scif5_clk_a),
		SH_PFC_PIN_GROUP(scif5_data_b),
		SH_PFC_PIN_GROUP(scif5_data_c),
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
		SH_PFC_PIN_GROUP(sdhi3_data1),
		SH_PFC_PIN_GROUP(sdhi3_data4),
		SH_PFC_PIN_GROUP(sdhi3_data8),
		SH_PFC_PIN_GROUP(sdhi3_ctrl),
		SH_PFC_PIN_GROUP(sdhi3_cd),
		SH_PFC_PIN_GROUP(sdhi3_wp),
		SH_PFC_PIN_GROUP(sdhi3_ds),
		SH_PFC_PIN_GROUP(ssi0_data),
		SH_PFC_PIN_GROUP(ssi01239_ctrl),
		SH_PFC_PIN_GROUP(ssi1_data),
		SH_PFC_PIN_GROUP(ssi1_ctrl),
		SH_PFC_PIN_GROUP(ssi2_data),
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
		SH_PFC_PIN_GROUP(ssi9_data),
		SH_PFC_PIN_GROUP(ssi9_ctrl_a),
		SH_PFC_PIN_GROUP(ssi9_ctrl_b),
		SH_PFC_PIN_GROUP(tmu_tclk1_a),
		SH_PFC_PIN_GROUP(tmu_tclk1_b),
		SH_PFC_PIN_GROUP(tmu_tclk2_a),
		SH_PFC_PIN_GROUP(tmu_tclk2_b),
		SH_PFC_PIN_GROUP(usb0_a),
		SH_PFC_PIN_GROUP(usb0_b),
		SH_PFC_PIN_GROUP(usb0_id),
		SH_PFC_PIN_GROUP(usb30),
		SH_PFC_PIN_GROUP(usb30_id),
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
		SH_PFC_PIN_GROUP(vin4_sync),
		SH_PFC_PIN_GROUP(vin4_field),
		SH_PFC_PIN_GROUP(vin4_clkenb),
		SH_PFC_PIN_GROUP(vin4_clk),
		VIN_DATA_PIN_GROUP(vin5_data, 8, _a),
		VIN_DATA_PIN_GROUP(vin5_data, 10, _a),
		VIN_DATA_PIN_GROUP(vin5_data, 12, _a),
		VIN_DATA_PIN_GROUP(vin5_data, 16, _a),
		SH_PFC_PIN_GROUP(vin5_data8_b),
		SH_PFC_PIN_GROUP(vin5_sync_a),
		SH_PFC_PIN_GROUP(vin5_field_a),
		SH_PFC_PIN_GROUP(vin5_clkenb_a),
		SH_PFC_PIN_GROUP(vin5_clk_a),
		SH_PFC_PIN_GROUP(vin5_clk_b),
	},
#ifdef CONFIG_PINCTRL_PFC_R8A77990
	.automotive = {
		SH_PFC_PIN_GROUP(drif0_ctrl_a),
		SH_PFC_PIN_GROUP(drif0_data0_a),
		SH_PFC_PIN_GROUP(drif0_data1_a),
		SH_PFC_PIN_GROUP(drif0_ctrl_b),
		SH_PFC_PIN_GROUP(drif0_data0_b),
		SH_PFC_PIN_GROUP(drif0_data1_b),
		SH_PFC_PIN_GROUP(drif1_ctrl),
		SH_PFC_PIN_GROUP(drif1_data0),
		SH_PFC_PIN_GROUP(drif1_data1),
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
	}
#endif /* CONFIG_PINCTRL_PFC_R8A77990 */
};

static const char * const audio_clk_groups[] = {
	"audio_clk_a",
	"audio_clk_b_a",
	"audio_clk_b_b",
	"audio_clk_b_c",
	"audio_clk_c_a",
	"audio_clk_c_b",
	"audio_clk_c_c",
	"audio_clkout_a",
	"audio_clkout_b",
	"audio_clkout1_a",
	"audio_clkout1_b",
	"audio_clkout1_c",
	"audio_clkout2_a",
	"audio_clkout2_b",
	"audio_clkout2_c",
	"audio_clkout3_a",
	"audio_clkout3_b",
	"audio_clkout3_c",
};

static const char * const avb_groups[] = {
	"avb_link",
	"avb_magic",
	"avb_phy_int",
	"avb_mii",
	"avb_avtp_pps",
	"avb_avtp_match",
	"avb_avtp_capture",
};

static const char * const can0_groups[] = {
	"can0_data",
};

static const char * const can1_groups[] = {
	"can1_data",
};

static const char * const can_clk_groups[] = {
	"can_clk",
};

static const char * const canfd0_groups[] = {
	"canfd0_data",
};

static const char * const canfd1_groups[] = {
	"canfd1_data",
};

#ifdef CONFIG_PINCTRL_PFC_R8A77990
static const char * const drif0_groups[] = {
	"drif0_ctrl_a",
	"drif0_data0_a",
	"drif0_data1_a",
	"drif0_ctrl_b",
	"drif0_data0_b",
	"drif0_data1_b",
};

static const char * const drif1_groups[] = {
	"drif1_ctrl",
	"drif1_data0",
	"drif1_data1",
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
#endif /* CONFIG_PINCTRL_PFC_R8A77990 */

static const char * const du_groups[] = {
	"du_rgb666",
	"du_rgb888",
	"du_clk_in_0",
	"du_clk_in_1",
	"du_clk_out_0",
	"du_sync",
	"du_disp_cde",
	"du_cde",
	"du_disp",
};

static const char * const hscif0_groups[] = {
	"hscif0_data_a",
	"hscif0_clk_a",
	"hscif0_ctrl_a",
	"hscif0_data_b",
	"hscif0_clk_b",
};

static const char * const hscif1_groups[] = {
	"hscif1_data_a",
	"hscif1_clk_a",
	"hscif1_data_b",
	"hscif1_clk_b",
	"hscif1_ctrl_b",
};

static const char * const hscif2_groups[] = {
	"hscif2_data_a",
	"hscif2_clk_a",
	"hscif2_ctrl_a",
	"hscif2_data_b",
};

static const char * const hscif3_groups[] = {
	"hscif3_data_a",
	"hscif3_data_b",
	"hscif3_clk_b",
	"hscif3_data_c",
	"hscif3_clk_c",
	"hscif3_ctrl_c",
	"hscif3_data_d",
	"hscif3_data_e",
	"hscif3_ctrl_e",
};

static const char * const hscif4_groups[] = {
	"hscif4_data_a",
	"hscif4_clk_a",
	"hscif4_ctrl_a",
	"hscif4_data_b",
	"hscif4_clk_b",
	"hscif4_data_c",
	"hscif4_data_d",
	"hscif4_data_e",
};

static const char * const i2c1_groups[] = {
	"i2c1_a",
	"i2c1_b",
	"i2c1_c",
	"i2c1_d",
};

static const char * const i2c2_groups[] = {
	"i2c2_a",
	"i2c2_b",
	"i2c2_c",
	"i2c2_d",
	"i2c2_e",
};

static const char * const i2c4_groups[] = {
	"i2c4",
};

static const char * const i2c5_groups[] = {
	"i2c5",
};

static const char * const i2c6_groups[] = {
	"i2c6_a",
	"i2c6_b",
};

static const char * const i2c7_groups[] = {
	"i2c7_a",
	"i2c7_b",
};

static const char * const intc_ex_groups[] = {
	"intc_ex_irq0",
	"intc_ex_irq1",
	"intc_ex_irq2",
	"intc_ex_irq3",
	"intc_ex_irq4",
	"intc_ex_irq5",
};

static const char * const msiof0_groups[] = {
	"msiof0_clk",
	"msiof0_sync",
	"msiof0_ss1",
	"msiof0_ss2",
	"msiof0_txd",
	"msiof0_rxd",
};

static const char * const msiof1_groups[] = {
	"msiof1_clk",
	"msiof1_sync",
	"msiof1_ss1",
	"msiof1_ss2",
	"msiof1_txd",
	"msiof1_rxd",
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
	"msiof3_txd_b",
	"msiof3_rxd_b",
};

static const char * const pwm0_groups[] = {
	"pwm0_a",
	"pwm0_b",
};

static const char * const pwm1_groups[] = {
	"pwm1_a",
	"pwm1_b",
};

static const char * const pwm2_groups[] = {
	"pwm2_a",
	"pwm2_b",
	"pwm2_c",
};

static const char * const pwm3_groups[] = {
	"pwm3_a",
	"pwm3_b",
	"pwm3_c",
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

static const char * const scif0_groups[] = {
	"scif0_data_a",
	"scif0_clk_a",
	"scif0_ctrl_a",
	"scif0_data_b",
	"scif0_clk_b",
};

static const char * const scif1_groups[] = {
	"scif1_data",
	"scif1_clk",
	"scif1_ctrl",
};

static const char * const scif2_groups[] = {
	"scif2_data_a",
	"scif2_clk_a",
	"scif2_data_b",
};

static const char * const scif3_groups[] = {
	"scif3_data_a",
	"scif3_clk_a",
	"scif3_ctrl_a",
	"scif3_data_b",
	"scif3_data_c",
	"scif3_clk_c",
};

static const char * const scif4_groups[] = {
	"scif4_data_a",
	"scif4_clk_a",
	"scif4_ctrl_a",
	"scif4_data_b",
	"scif4_clk_b",
	"scif4_data_c",
	"scif4_ctrl_c",
};

static const char * const scif5_groups[] = {
	"scif5_data_a",
	"scif5_clk_a",
	"scif5_data_b",
	"scif5_data_c",
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
	"ssi1_data",
	"ssi1_ctrl",
	"ssi2_data",
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
	"ssi9_data",
	"ssi9_ctrl_a",
	"ssi9_ctrl_b",
};

static const char * const tmu_groups[] = {
	"tmu_tclk1_a",
	"tmu_tclk1_b",
	"tmu_tclk2_a",
	"tmu_tclk2_b",
};

static const char * const usb0_groups[] = {
	"usb0_a",
	"usb0_b",
	"usb0_id",
};

static const char * const usb30_groups[] = {
	"usb30",
	"usb30_id",
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
	"vin4_sync",
	"vin4_field",
	"vin4_clkenb",
	"vin4_clk",
};

static const char * const vin5_groups[] = {
	"vin5_data8_a",
	"vin5_data10_a",
	"vin5_data12_a",
	"vin5_data16_a",
	"vin5_data8_b",
	"vin5_sync_a",
	"vin5_field_a",
	"vin5_clkenb_a",
	"vin5_clk_a",
	"vin5_clk_b",
};

static const struct {
	struct sh_pfc_function common[49];
#ifdef CONFIG_PINCTRL_PFC_R8A77990
	struct sh_pfc_function automotive[4];
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
		SH_PFC_FUNCTION(i2c1),
		SH_PFC_FUNCTION(i2c2),
		SH_PFC_FUNCTION(i2c4),
		SH_PFC_FUNCTION(i2c5),
		SH_PFC_FUNCTION(i2c6),
		SH_PFC_FUNCTION(i2c7),
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
		SH_PFC_FUNCTION(scif0),
		SH_PFC_FUNCTION(scif1),
		SH_PFC_FUNCTION(scif2),
		SH_PFC_FUNCTION(scif3),
		SH_PFC_FUNCTION(scif4),
		SH_PFC_FUNCTION(scif5),
		SH_PFC_FUNCTION(scif_clk),
		SH_PFC_FUNCTION(sdhi0),
		SH_PFC_FUNCTION(sdhi1),
		SH_PFC_FUNCTION(sdhi3),
		SH_PFC_FUNCTION(ssi),
		SH_PFC_FUNCTION(tmu),
		SH_PFC_FUNCTION(usb0),
		SH_PFC_FUNCTION(usb30),
		SH_PFC_FUNCTION(vin4),
		SH_PFC_FUNCTION(vin5),
	},
#ifdef CONFIG_PINCTRL_PFC_R8A77990
	.automotive = {
		SH_PFC_FUNCTION(drif0),
		SH_PFC_FUNCTION(drif1),
		SH_PFC_FUNCTION(drif2),
		SH_PFC_FUNCTION(drif3),
	}
#endif /* CONFIG_PINCTRL_PFC_R8A77990 */
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
		GP_0_17_FN,	GPSR0_17,
		GP_0_16_FN,	GPSR0_16,
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
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
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
		GP_2_25_FN,	GPSR2_25,
		GP_2_24_FN,	GPSR2_24,
		GP_2_23_FN,	GPSR2_23,
		GP_2_22_FN,	GPSR2_22,
		GP_2_21_FN,	GPSR2_21,
		GP_2_20_FN,	GPSR2_20,
		GP_2_19_FN,	GPSR2_19,
		GP_2_18_FN,	GPSR2_18,
		GP_2_17_FN,	GPSR2_17,
		GP_2_16_FN,	GPSR2_16,
		GP_2_15_FN,	GPSR2_15,
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
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
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
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
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
		IP7_15_12
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
#undef F_
#undef FM

#define F_(x, y)	x,
#define FM(x)		FN_##x,
	{ PINMUX_CFG_REG_VAR("MOD_SEL0", 0xe6060500, 32,
			     GROUP(1, 2, 1, 2, 1, 1, 1, 1, 2, 3, 1, 1,
				   1, 2, 2, 1, 1, 1, 2, 1, 1, 1, 2),
			     GROUP(
		/* RESERVED 31 */
		0, 0,
		MOD_SEL0_30_29
		MOD_SEL0_28
		MOD_SEL0_27_26
		MOD_SEL0_25
		MOD_SEL0_24
		MOD_SEL0_23
		MOD_SEL0_22
		MOD_SEL0_21_20
		MOD_SEL0_19_18_17
		MOD_SEL0_16
		MOD_SEL0_15
		MOD_SEL0_14
		MOD_SEL0_13_12
		MOD_SEL0_11_10
		MOD_SEL0_9
		MOD_SEL0_8
		MOD_SEL0_7
		MOD_SEL0_6_5
		MOD_SEL0_4
		MOD_SEL0_3
		MOD_SEL0_2
		MOD_SEL0_1_0 ))
	},
	{ PINMUX_CFG_REG_VAR("MOD_SEL1", 0xe6060504, 32,
			     GROUP(1, 1, 1, 1, 1, 1, 1, 3, 3, 1, 1, 1,
				   1, 2, 2, 2, 1, 1, 2, 1, 4),
			     GROUP(
		MOD_SEL1_31
		MOD_SEL1_30
		MOD_SEL1_29
		MOD_SEL1_28
		/* RESERVED 27 */
		0, 0,
		MOD_SEL1_26
		MOD_SEL1_25
		MOD_SEL1_24_23_22
		MOD_SEL1_21_20_19
		MOD_SEL1_18
		MOD_SEL1_17
		MOD_SEL1_16
		MOD_SEL1_15
		MOD_SEL1_14_13
		MOD_SEL1_12_11
		MOD_SEL1_10_9
		MOD_SEL1_8
		MOD_SEL1_7
		MOD_SEL1_6_5
		MOD_SEL1_4
		/* RESERVED 3, 2, 1, 0  */
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 ))
	},
	{ },
};

enum ioctrl_regs {
	POCCTRL0,
	TDSELCTRL,
};

static const struct pinmux_ioctrl_reg pinmux_ioctrl_regs[] = {
	[POCCTRL0] = { 0xe6060380, },
	[TDSELCTRL] = { 0xe60603c0, },
	{ /* sentinel */ },
};

static int r8a77990_pin_to_pocctrl(struct sh_pfc *pfc, unsigned int pin,
				   u32 *pocctrl)
{
	int bit = -EINVAL;

	*pocctrl = pinmux_ioctrl_regs[POCCTRL0].reg;

	if (pin >= RCAR_GP_PIN(3, 0) && pin <= RCAR_GP_PIN(3, 11))
		bit = pin & 0x1f;

	if (pin >= RCAR_GP_PIN(4, 0) && pin <= RCAR_GP_PIN(4, 10))
		bit = (pin & 0x1f) + 19;

	return bit;
}

static const struct pinmux_bias_reg pinmux_bias_regs[] = {
	{ PINMUX_BIAS_REG("PUEN0", 0xe6060400, "PUD0", 0xe6060440) {
		 [0] = RCAR_GP_PIN(2, 23),	/* RD# */
		 [1] = RCAR_GP_PIN(2, 22),	/* BS# */
		 [2] = RCAR_GP_PIN(2, 21),	/* AVB_PHY_INT */
		 [3] = PIN_AVB_MDC,		/* AVB_MDC */
		 [4] = PIN_AVB_MDIO,		/* AVB_MDIO */
		 [5] = RCAR_GP_PIN(2, 20),	/* AVB_TXCREFCLK */
		 [6] = PIN_AVB_TD3,		/* AVB_TD3 */
		 [7] = PIN_AVB_TD2,		/* AVB_TD2 */
		 [8] = PIN_AVB_TD1,		/* AVB_TD1 */
		 [9] = PIN_AVB_TD0,		/* AVB_TD0 */
		[10] = PIN_AVB_TXC,		/* AVB_TXC */
		[11] = PIN_AVB_TX_CTL,		/* AVB_TX_CTL */
		[12] = RCAR_GP_PIN(2, 19),	/* AVB_RD3 */
		[13] = RCAR_GP_PIN(2, 18),	/* AVB_RD2 */
		[14] = RCAR_GP_PIN(2, 17),	/* AVB_RD1 */
		[15] = RCAR_GP_PIN(2, 16),	/* AVB_RD0 */
		[16] = RCAR_GP_PIN(2, 15),	/* AVB_RXC */
		[17] = RCAR_GP_PIN(2, 14),	/* AVB_RX_CTL */
		[18] = RCAR_GP_PIN(2, 13),	/* RPC_RESET# */
		[19] = RCAR_GP_PIN(2, 12),	/* RPC_INT# */
		[20] = RCAR_GP_PIN(2, 11),	/* QSPI1_SSL */
		[21] = RCAR_GP_PIN(2, 10),	/* QSPI1_IO3 */
		[22] = RCAR_GP_PIN(2,  9),	/* QSPI1_IO2 */
		[23] = RCAR_GP_PIN(2,  8),	/* QSPI1_MISO/IO1 */
		[24] = RCAR_GP_PIN(2,  7),	/* QSPI1_MOSI/IO0 */
		[25] = RCAR_GP_PIN(2,  6),	/* QSPI1_SPCLK */
		[26] = RCAR_GP_PIN(2,  5),	/* QSPI0_SSL */
		[27] = RCAR_GP_PIN(2,  4),	/* QSPI0_IO3 */
		[28] = RCAR_GP_PIN(2,  3),	/* QSPI0_IO2 */
		[29] = RCAR_GP_PIN(2,  2),	/* QSPI0_MISO/IO1 */
		[30] = RCAR_GP_PIN(2,  1),	/* QSPI0_MOSI/IO0 */
		[31] = RCAR_GP_PIN(2,  0),	/* QSPI0_SPCLK */
	} },
	{ PINMUX_BIAS_REG("PUEN1", 0xe6060404, "PUD1", 0xe6060444) {
		 [0] = RCAR_GP_PIN(0,  4),	/* D4 */
		 [1] = RCAR_GP_PIN(0,  3),	/* D3 */
		 [2] = RCAR_GP_PIN(0,  2),	/* D2 */
		 [3] = RCAR_GP_PIN(0,  1),	/* D1 */
		 [4] = RCAR_GP_PIN(0,  0),	/* D0 */
		 [5] = RCAR_GP_PIN(1, 22),	/* WE0# */
		 [6] = RCAR_GP_PIN(1, 21),	/* CS0# */
		 [7] = RCAR_GP_PIN(1, 20),	/* CLKOUT */
		 [8] = RCAR_GP_PIN(1, 19),	/* A19 */
		 [9] = RCAR_GP_PIN(1, 18),	/* A18 */
		[10] = RCAR_GP_PIN(1, 17),	/* A17 */
		[11] = RCAR_GP_PIN(1, 16),	/* A16 */
		[12] = RCAR_GP_PIN(1, 15),	/* A15 */
		[13] = RCAR_GP_PIN(1, 14),	/* A14 */
		[14] = RCAR_GP_PIN(1, 13),	/* A13 */
		[15] = RCAR_GP_PIN(1, 12),	/* A12 */
		[16] = RCAR_GP_PIN(1, 11),	/* A11 */
		[17] = RCAR_GP_PIN(1, 10),	/* A10 */
		[18] = RCAR_GP_PIN(1,  9),	/* A9 */
		[19] = RCAR_GP_PIN(1,  8),	/* A8 */
		[20] = RCAR_GP_PIN(1,  7),	/* A7 */
		[21] = RCAR_GP_PIN(1,  6),	/* A6 */
		[22] = RCAR_GP_PIN(1,  5),	/* A5 */
		[23] = RCAR_GP_PIN(1,  4),	/* A4 */
		[24] = RCAR_GP_PIN(1,  3),	/* A3 */
		[25] = RCAR_GP_PIN(1,  2),	/* A2 */
		[26] = RCAR_GP_PIN(1,  1),	/* A1 */
		[27] = RCAR_GP_PIN(1,  0),	/* A0 */
		[28] = SH_PFC_PIN_NONE,
		[29] = SH_PFC_PIN_NONE,
		[30] = RCAR_GP_PIN(2, 25),	/* PUEN_EX_WAIT0 */
		[31] = RCAR_GP_PIN(2, 24),	/* PUEN_RD/WR# */
	} },
	{ PINMUX_BIAS_REG("PUEN2", 0xe6060408, "PUD2", 0xe6060448) {
		 [0] = RCAR_GP_PIN(3,  1),	/* SD0_CMD */
		 [1] = RCAR_GP_PIN(3,  0),	/* SD0_CLK */
		 [2] = PIN_ASEBRK,		/* ASEBRK */
		 [3] = SH_PFC_PIN_NONE,
		 [4] = PIN_TDI,			/* TDI */
		 [5] = PIN_TMS,			/* TMS */
		 [6] = PIN_TCK,			/* TCK */
		 [7] = PIN_TRST_N,		/* TRST# */
		 [8] = SH_PFC_PIN_NONE,
		 [9] = SH_PFC_PIN_NONE,
		[10] = SH_PFC_PIN_NONE,
		[11] = SH_PFC_PIN_NONE,
		[12] = SH_PFC_PIN_NONE,
		[13] = SH_PFC_PIN_NONE,
		[14] = SH_PFC_PIN_NONE,
		[15] = PIN_FSCLKST_N,		/* FSCLKST# */
		[16] = RCAR_GP_PIN(0, 17),	/* SDA4 */
		[17] = RCAR_GP_PIN(0, 16),	/* SCL4 */
		[18] = SH_PFC_PIN_NONE,
		[19] = SH_PFC_PIN_NONE,
		[20] = PIN_PRESETOUT_N,		/* PRESETOUT# */
		[21] = RCAR_GP_PIN(0, 15),	/* D15 */
		[22] = RCAR_GP_PIN(0, 14),	/* D14 */
		[23] = RCAR_GP_PIN(0, 13),	/* D13 */
		[24] = RCAR_GP_PIN(0, 12),	/* D12 */
		[25] = RCAR_GP_PIN(0, 11),	/* D11 */
		[26] = RCAR_GP_PIN(0, 10),	/* D10 */
		[27] = RCAR_GP_PIN(0,  9),	/* D9 */
		[28] = RCAR_GP_PIN(0,  8),	/* D8 */
		[29] = RCAR_GP_PIN(0,  7),	/* D7 */
		[30] = RCAR_GP_PIN(0,  6),	/* D6 */
		[31] = RCAR_GP_PIN(0,  5),	/* D5 */
	} },
	{ PINMUX_BIAS_REG("PUEN3", 0xe606040c, "PUD3", 0xe606044c) {
		 [0] = RCAR_GP_PIN(5,  0),	/* SCK0_A */
		 [1] = RCAR_GP_PIN(5,  4),	/* RTS0#_A */
		 [2] = RCAR_GP_PIN(5,  3),	/* CTS0#_A */
		 [3] = RCAR_GP_PIN(5,  2),	/* TX0_A */
		 [4] = RCAR_GP_PIN(5,  1),	/* RX0_A */
		 [5] = SH_PFC_PIN_NONE,
		 [6] = SH_PFC_PIN_NONE,
		 [7] = RCAR_GP_PIN(3, 15),	/* SD1_WP */
		 [8] = RCAR_GP_PIN(3, 14),	/* SD1_CD */
		 [9] = RCAR_GP_PIN(3, 13),	/* SD0_WP */
		[10] = RCAR_GP_PIN(3, 12),	/* SD0_CD */
		[11] = RCAR_GP_PIN(4, 10),	/* SD3_DS */
		[12] = RCAR_GP_PIN(4,  9),	/* SD3_DAT7 */
		[13] = RCAR_GP_PIN(4,  8),	/* SD3_DAT6 */
		[14] = RCAR_GP_PIN(4,  7),	/* SD3_DAT5 */
		[15] = RCAR_GP_PIN(4,  6),	/* SD3_DAT4 */
		[16] = RCAR_GP_PIN(4,  5),	/* SD3_DAT3 */
		[17] = RCAR_GP_PIN(4,  4),	/* SD3_DAT2 */
		[18] = RCAR_GP_PIN(4,  3),	/* SD3_DAT1 */
		[19] = RCAR_GP_PIN(4,  2),	/* SD3_DAT0 */
		[20] = RCAR_GP_PIN(4,  1),	/* SD3_CMD */
		[21] = RCAR_GP_PIN(4,  0),	/* SD3_CLK */
		[22] = RCAR_GP_PIN(3, 11),	/* SD1_DAT3 */
		[23] = RCAR_GP_PIN(3, 10),	/* SD1_DAT2 */
		[24] = RCAR_GP_PIN(3,  9),	/* SD1_DAT1 */
		[25] = RCAR_GP_PIN(3,  8),	/* SD1_DAT0 */
		[26] = RCAR_GP_PIN(3,  7),	/* SD1_CMD */
		[27] = RCAR_GP_PIN(3,  6),	/* SD1_CLK */
		[28] = RCAR_GP_PIN(3,  5),	/* SD0_DAT3 */
		[29] = RCAR_GP_PIN(3,  4),	/* SD0_DAT2 */
		[30] = RCAR_GP_PIN(3,  3),	/* SD0_DAT1 */
		[31] = RCAR_GP_PIN(3,  2),	/* SD0_DAT0 */
	} },
	{ PINMUX_BIAS_REG("PUEN4", 0xe6060410, "PUD4", 0xe6060450) {
		 [0] = RCAR_GP_PIN(6,  8),	/* AUDIO_CLKA */
		 [1] = RCAR_GP_PIN(6, 16),	/* SSI_SDATA6 */
		 [2] = RCAR_GP_PIN(6, 15),	/* SSI_WS6 */
		 [3] = RCAR_GP_PIN(6, 14),	/* SSI_SCK6 */
		 [4] = RCAR_GP_PIN(6, 13),	/* SSI_SDATA5 */
		 [5] = RCAR_GP_PIN(6, 12),	/* SSI_WS5 */
		 [6] = RCAR_GP_PIN(6, 11),	/* SSI_SCK5 */
		 [7] = RCAR_GP_PIN(6, 10),	/* SSI_SDATA4 */
		 [8] = RCAR_GP_PIN(6,  7),	/* SSI_SDATA3 */
		 [9] = RCAR_GP_PIN(6,  6),	/* SSI_WS349 */
		[10] = RCAR_GP_PIN(6,  5),	/* SSI_SCK349 */
		[11] = RCAR_GP_PIN(6,  4),	/* SSI_SDATA2 */
		[12] = RCAR_GP_PIN(6,  3),	/* SSI_SDATA1 */
		[13] = RCAR_GP_PIN(6,  2),	/* SSI_SDATA0 */
		[14] = RCAR_GP_PIN(6,  1),	/* SSI_WS01239 */
		[15] = RCAR_GP_PIN(6,  0),	/* SSI_SCK01239 */
		[16] = PIN_MLB_REF,		/* MLB_REF */
		[17] = RCAR_GP_PIN(5, 19),	/* MLB_DAT */
		[18] = RCAR_GP_PIN(5, 18),	/* MLB_SIG */
		[19] = RCAR_GP_PIN(5, 17),	/* MLB_CLK */
		[20] = RCAR_GP_PIN(5, 16),	/* SSI_SDATA9 */
		[21] = RCAR_GP_PIN(5, 15),	/* MSIOF0_SS2 */
		[22] = RCAR_GP_PIN(5, 14),	/* MSIOF0_SS1 */
		[23] = RCAR_GP_PIN(5, 13),	/* MSIOF0_SYNC */
		[24] = RCAR_GP_PIN(5, 12),	/* MSIOF0_TXD */
		[25] = RCAR_GP_PIN(5, 11),	/* MSIOF0_RXD */
		[26] = RCAR_GP_PIN(5, 10),	/* MSIOF0_SCK */
		[27] = RCAR_GP_PIN(5,  9),	/* RX2_A */
		[28] = RCAR_GP_PIN(5,  8),	/* TX2_A */
		[29] = RCAR_GP_PIN(5,  7),	/* SCK2_A */
		[30] = RCAR_GP_PIN(5,  6),	/* TX1 */
		[31] = RCAR_GP_PIN(5,  5),	/* RX1 */
	} },
	{ PINMUX_BIAS_REG("PUEN5", 0xe6060414, "PUD5", 0xe6060454) {
		 [0] = SH_PFC_PIN_NONE,
		 [1] = SH_PFC_PIN_NONE,
		 [2] = SH_PFC_PIN_NONE,
		 [3] = SH_PFC_PIN_NONE,
		 [4] = SH_PFC_PIN_NONE,
		 [5] = SH_PFC_PIN_NONE,
		 [6] = SH_PFC_PIN_NONE,
		 [7] = SH_PFC_PIN_NONE,
		 [8] = SH_PFC_PIN_NONE,
		 [9] = SH_PFC_PIN_NONE,
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
		[30] = RCAR_GP_PIN(6,  9),	/* PUEN_USB30_OVC */
		[31] = RCAR_GP_PIN(6, 17),	/* PUEN_USB30_PWEN */
	} },
	{ /* sentinel */ },
};

static const struct sh_pfc_soc_operations r8a77990_pinmux_ops = {
	.pin_to_pocctrl = r8a77990_pin_to_pocctrl,
	.get_bias = rcar_pinmux_get_bias,
	.set_bias = rcar_pinmux_set_bias,
};

#ifdef CONFIG_PINCTRL_PFC_R8A774C0
const struct sh_pfc_soc_info r8a774c0_pinmux_info = {
	.name = "r8a774c0_pfc",
	.ops = &r8a77990_pinmux_ops,
	.unlock_reg = 0xe6060000, /* PMMR */

	.function = { PINMUX_FUNCTION_BEGIN, PINMUX_FUNCTION_END },

	.pins = pinmux_pins,
	.nr_pins = ARRAY_SIZE(pinmux_pins),
	.groups = pinmux_groups.common,
	.nr_groups = ARRAY_SIZE(pinmux_groups.common),
	.functions = pinmux_functions.common,
	.nr_functions = ARRAY_SIZE(pinmux_functions.common),

	.cfg_regs = pinmux_config_regs,
	.bias_regs = pinmux_bias_regs,
	.ioctrl_regs = pinmux_ioctrl_regs,

	.pinmux_data = pinmux_data,
	.pinmux_data_size = ARRAY_SIZE(pinmux_data),
};
#endif

#ifdef CONFIG_PINCTRL_PFC_R8A77990
const struct sh_pfc_soc_info r8a77990_pinmux_info = {
	.name = "r8a77990_pfc",
	.ops = &r8a77990_pinmux_ops,
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
	.bias_regs = pinmux_bias_regs,
	.ioctrl_regs = pinmux_ioctrl_regs,

	.pinmux_data = pinmux_data,
	.pinmux_data_size = ARRAY_SIZE(pinmux_data),
};
#endif
