// SPDX-License-Identifier: GPL-2.0
/*
 * R8A77970 processor support - PFC hardware block.
 *
 * Copyright (C) 2016 Renesas Electronics Corp.
 * Copyright (C) 2017 Cogent Embedded, Inc. <source@cogentembedded.com>
 *
 * This file is based on the drivers/pinctrl/sh-pfc/pfc-r8a7795.c
 *
 * R-Car Gen3 processor support - PFC hardware block.
 *
 * Copyright (C) 2015  Renesas Electronics Corporation
 */

#include <linux/errno.h>
#include <linux/io.h>
#include <linux/kernel.h>

#include "core.h"
#include "sh_pfc.h"

#define CPU_ALL_PORT(fn, sfx)						\
	PORT_GP_CFG_22(0, fn, sfx, SH_PFC_PIN_CFG_IO_VOLTAGE),		\
	PORT_GP_28(1, fn, sfx),						\
	PORT_GP_CFG_17(2, fn, sfx, SH_PFC_PIN_CFG_IO_VOLTAGE),		\
	PORT_GP_CFG_17(3, fn, sfx, SH_PFC_PIN_CFG_IO_VOLTAGE),		\
	PORT_GP_6(4,  fn, sfx),						\
	PORT_GP_15(5, fn, sfx)
/*
 * F_() : just information
 * FM() : macro for FN_xxx / xxx_MARK
 */

/* GPSR0 */
#define GPSR0_21	F_(DU_EXODDF_DU_ODDF_DISP_CDE,	IP2_23_20)
#define GPSR0_20	F_(DU_EXVSYNC_DU_VSYNC,		IP2_19_16)
#define GPSR0_19	F_(DU_EXHSYNC_DU_HSYNC,		IP2_15_12)
#define GPSR0_18	F_(DU_DOTCLKOUT,		IP2_11_8)
#define GPSR0_17	F_(DU_DB7,			IP2_7_4)
#define GPSR0_16	F_(DU_DB6,			IP2_3_0)
#define GPSR0_15	F_(DU_DB5,			IP1_31_28)
#define GPSR0_14	F_(DU_DB4,			IP1_27_24)
#define GPSR0_13	F_(DU_DB3,			IP1_23_20)
#define GPSR0_12	F_(DU_DB2,			IP1_19_16)
#define GPSR0_11	F_(DU_DG7,			IP1_15_12)
#define GPSR0_10	F_(DU_DG6,			IP1_11_8)
#define GPSR0_9		F_(DU_DG5,			IP1_7_4)
#define GPSR0_8		F_(DU_DG4,			IP1_3_0)
#define GPSR0_7		F_(DU_DG3,			IP0_31_28)
#define GPSR0_6		F_(DU_DG2,			IP0_27_24)
#define GPSR0_5		F_(DU_DR7,			IP0_23_20)
#define GPSR0_4		F_(DU_DR6,			IP0_19_16)
#define GPSR0_3		F_(DU_DR5,			IP0_15_12)
#define GPSR0_2		F_(DU_DR4,			IP0_11_8)
#define GPSR0_1		F_(DU_DR3,			IP0_7_4)
#define GPSR0_0		F_(DU_DR2,			IP0_3_0)

/* GPSR1 */
#define GPSR1_27	F_(DIGRF_CLKOUT,	IP8_27_24)
#define GPSR1_26	F_(DIGRF_CLKIN,		IP8_23_20)
#define GPSR1_25	F_(CANFD_CLK_A,		IP8_19_16)
#define GPSR1_24	F_(CANFD1_RX,		IP8_15_12)
#define GPSR1_23	F_(CANFD1_TX,		IP8_11_8)
#define GPSR1_22	F_(CANFD0_RX_A,		IP8_7_4)
#define GPSR1_21	F_(CANFD0_TX_A,		IP8_3_0)
#define GPSR1_20	F_(AVB0_AVTP_CAPTURE,	IP7_31_28)
#define GPSR1_19	FM(AVB0_AVTP_MATCH)
#define GPSR1_18	FM(AVB0_LINK)
#define GPSR1_17	FM(AVB0_PHY_INT)
#define GPSR1_16	FM(AVB0_MAGIC)
#define GPSR1_15	FM(AVB0_MDC)
#define GPSR1_14	FM(AVB0_MDIO)
#define GPSR1_13	FM(AVB0_TXCREFCLK)
#define GPSR1_12	FM(AVB0_TD3)
#define GPSR1_11	FM(AVB0_TD2)
#define GPSR1_10	FM(AVB0_TD1)
#define GPSR1_9		FM(AVB0_TD0)
#define GPSR1_8		FM(AVB0_TXC)
#define GPSR1_7		FM(AVB0_TX_CTL)
#define GPSR1_6		FM(AVB0_RD3)
#define GPSR1_5		FM(AVB0_RD2)
#define GPSR1_4		FM(AVB0_RD1)
#define GPSR1_3		FM(AVB0_RD0)
#define GPSR1_2		FM(AVB0_RXC)
#define GPSR1_1		FM(AVB0_RX_CTL)
#define GPSR1_0		F_(IRQ0,		IP2_27_24)

/* GPSR2 */
#define GPSR2_16	F_(VI0_FIELD,		IP4_31_28)
#define GPSR2_15	F_(VI0_DATA11,		IP4_27_24)
#define GPSR2_14	F_(VI0_DATA10,		IP4_23_20)
#define GPSR2_13	F_(VI0_DATA9,		IP4_19_16)
#define GPSR2_12	F_(VI0_DATA8,		IP4_15_12)
#define GPSR2_11	F_(VI0_DATA7,		IP4_11_8)
#define GPSR2_10	F_(VI0_DATA6,		IP4_7_4)
#define GPSR2_9		F_(VI0_DATA5,		IP4_3_0)
#define GPSR2_8		F_(VI0_DATA4,		IP3_31_28)
#define GPSR2_7		F_(VI0_DATA3,		IP3_27_24)
#define GPSR2_6		F_(VI0_DATA2,		IP3_23_20)
#define GPSR2_5		F_(VI0_DATA1,		IP3_19_16)
#define GPSR2_4		F_(VI0_DATA0,		IP3_15_12)
#define GPSR2_3		F_(VI0_VSYNC_N,		IP3_11_8)
#define GPSR2_2		F_(VI0_HSYNC_N,		IP3_7_4)
#define GPSR2_1		F_(VI0_CLKENB,		IP3_3_0)
#define GPSR2_0		F_(VI0_CLK,		IP2_31_28)

/* GPSR3 */
#define GPSR3_16	F_(VI1_FIELD,		IP7_3_0)
#define GPSR3_15	F_(VI1_DATA11,		IP6_31_28)
#define GPSR3_14	F_(VI1_DATA10,		IP6_27_24)
#define GPSR3_13	F_(VI1_DATA9,		IP6_23_20)
#define GPSR3_12	F_(VI1_DATA8,		IP6_19_16)
#define GPSR3_11	F_(VI1_DATA7,		IP6_15_12)
#define GPSR3_10	F_(VI1_DATA6,		IP6_11_8)
#define GPSR3_9		F_(VI1_DATA5,		IP6_7_4)
#define GPSR3_8		F_(VI1_DATA4,		IP6_3_0)
#define GPSR3_7		F_(VI1_DATA3,		IP5_31_28)
#define GPSR3_6		F_(VI1_DATA2,		IP5_27_24)
#define GPSR3_5		F_(VI1_DATA1,		IP5_23_20)
#define GPSR3_4		F_(VI1_DATA0,		IP5_19_16)
#define GPSR3_3		F_(VI1_VSYNC_N,		IP5_15_12)
#define GPSR3_2		F_(VI1_HSYNC_N,		IP5_11_8)
#define GPSR3_1		F_(VI1_CLKENB,		IP5_7_4)
#define GPSR3_0		F_(VI1_CLK,		IP5_3_0)

/* GPSR4 */
#define GPSR4_5		F_(SDA2,		IP7_27_24)
#define GPSR4_4		F_(SCL2,		IP7_23_20)
#define GPSR4_3		F_(SDA1,		IP7_19_16)
#define GPSR4_2		F_(SCL1,		IP7_15_12)
#define GPSR4_1		F_(SDA0,		IP7_11_8)
#define GPSR4_0		F_(SCL0,		IP7_7_4)

/* GPSR5 */
#define GPSR5_14	FM(RPC_INT_N)
#define GPSR5_13	FM(RPC_WP_N)
#define GPSR5_12	FM(RPC_RESET_N)
#define GPSR5_11	FM(QSPI1_SSL)
#define GPSR5_10	FM(QSPI1_IO3)
#define GPSR5_9		FM(QSPI1_IO2)
#define GPSR5_8		FM(QSPI1_MISO_IO1)
#define GPSR5_7		FM(QSPI1_MOSI_IO0)
#define GPSR5_6		FM(QSPI1_SPCLK)
#define GPSR5_5		FM(QSPI0_SSL)
#define GPSR5_4		FM(QSPI0_IO3)
#define GPSR5_3		FM(QSPI0_IO2)
#define GPSR5_2		FM(QSPI0_MISO_IO1)
#define GPSR5_1		FM(QSPI0_MOSI_IO0)
#define GPSR5_0		FM(QSPI0_SPCLK)


/* IPSRx */		/* 0 */				/* 1 */			/* 2 */		/* 3 */		/* 4 */			/* 5 */		/* 6 - F */
#define IP0_3_0		FM(DU_DR2)			FM(HSCK0)		F_(0, 0)	FM(A0)		F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0_7_4		FM(DU_DR3)			FM(HRTS0_N)		F_(0, 0)	FM(A1)		F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0_11_8	FM(DU_DR4)			FM(HCTS0_N)		F_(0, 0)	FM(A2)		F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)	F_(0, 0) F_(0, 0)
#define IP0_15_12	FM(DU_DR5)			FM(HTX0)		F_(0, 0)	FM(A3)		F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0_19_16	FM(DU_DR6)			FM(MSIOF3_RXD)		F_(0, 0)	FM(A4)		F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0_23_20	FM(DU_DR7)			FM(MSIOF3_TXD)		F_(0, 0)	FM(A5)		F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0_27_24	FM(DU_DG2)			FM(MSIOF3_SS1)		F_(0, 0)	FM(A6)		F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0_31_28	FM(DU_DG3)			FM(MSIOF3_SS2)		F_(0, 0)	FM(A7)		FM(PWMFSW0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1_3_0		FM(DU_DG4)			F_(0, 0)		F_(0, 0)	FM(A8)		FM(FSO_CFE_0_N_A)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1_7_4		FM(DU_DG5)			F_(0, 0)		F_(0, 0)	FM(A9)		FM(FSO_CFE_1_N_A)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1_11_8	FM(DU_DG6)			F_(0, 0)		F_(0, 0)	FM(A10)		FM(FSO_TOE_N_A) 	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1_15_12	FM(DU_DG7)			F_(0, 0)		F_(0, 0)	FM(A11)		FM(IRQ1)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1_19_16	FM(DU_DB2)			F_(0, 0)		F_(0, 0)	FM(A12)		FM(IRQ2)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1_23_20	FM(DU_DB3)			F_(0, 0)		F_(0, 0)	FM(A13)		FM(FXR_CLKOUT1)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1_27_24	FM(DU_DB4)			F_(0, 0)		F_(0, 0)	FM(A14)		FM(FXR_CLKOUT2)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1_31_28	FM(DU_DB5)			F_(0, 0)		F_(0, 0)	FM(A15)		FM(FXR_TXENA_N)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2_3_0		FM(DU_DB6)			F_(0, 0)		F_(0, 0)	FM(A16)		FM(FXR_TXENB_N)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2_7_4		FM(DU_DB7)			F_(0, 0)		F_(0, 0)	FM(A17)		F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2_11_8	FM(DU_DOTCLKOUT)		FM(SCIF_CLK_A)		F_(0, 0)	FM(A18)		F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2_15_12	FM(DU_EXHSYNC_DU_HSYNC)		FM(HRX0)		F_(0, 0)	FM(A19)		FM(IRQ3)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2_19_16	FM(DU_EXVSYNC_DU_VSYNC)		FM(MSIOF3_SCK)		F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2_23_20	FM(DU_EXODDF_DU_ODDF_DISP_CDE)	FM(MSIOF3_SYNC)		F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2_27_24	FM(IRQ0)			F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2_31_28	FM(VI0_CLK)			FM(MSIOF2_SCK)		FM(SCK3)	F_(0, 0)	FM(HSCK3)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3_3_0		FM(VI0_CLKENB)			FM(MSIOF2_RXD)		FM(RX3)		FM(RD_WR_N)	FM(HCTS3_N)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3_7_4		FM(VI0_HSYNC_N)			FM(MSIOF2_TXD)		FM(TX3)		F_(0, 0)	FM(HRTS3_N)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3_11_8	FM(VI0_VSYNC_N)			FM(MSIOF2_SYNC)		FM(CTS3_N)	F_(0, 0)	FM(HTX3)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3_15_12	FM(VI0_DATA0)			FM(MSIOF2_SS1)		FM(RTS3_N)	F_(0, 0)	FM(HRX3)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3_19_16	FM(VI0_DATA1)			FM(MSIOF2_SS2)		FM(SCK1)	F_(0, 0)	FM(SPEEDIN_A)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3_23_20	FM(VI0_DATA2)			FM(AVB0_AVTP_PPS)	FM(SDA3_A)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3_27_24	FM(VI0_DATA3)			FM(HSCK1)		FM(SCL3_A)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3_31_28	FM(VI0_DATA4)			FM(HRTS1_N)		FM(RX1_A)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP4_3_0		FM(VI0_DATA5)			FM(HCTS1_N)		FM(TX1_A)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP4_7_4		FM(VI0_DATA6)			FM(HTX1)		FM(CTS1_N)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP4_11_8	FM(VI0_DATA7)			FM(HRX1)		FM(RTS1_N)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP4_15_12	FM(VI0_DATA8)			FM(HSCK2)		FM(PWM0_A)	FM(A22)		F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP4_19_16	FM(VI0_DATA9)			FM(HCTS2_N)		FM(PWM1_A)	FM(A23)		FM(FSO_CFE_0_N_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP4_23_20	FM(VI0_DATA10)			FM(HRTS2_N)		FM(PWM2_A)	FM(A24)		FM(FSO_CFE_1_N_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP4_27_24	FM(VI0_DATA11)			FM(HTX2)		FM(PWM3_A)	FM(A25)		FM(FSO_TOE_N_B)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP4_31_28	FM(VI0_FIELD)			FM(HRX2)		FM(PWM4_A)	FM(CS1_N)	FM(FSCLKST2_N_A)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP5_3_0		FM(VI1_CLK)			FM(MSIOF1_RXD)		F_(0, 0)	FM(CS0_N)	F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP5_7_4		FM(VI1_CLKENB)			FM(MSIOF1_TXD)		F_(0, 0)	FM(D0)		F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP5_11_8	FM(VI1_HSYNC_N)			FM(MSIOF1_SCK)		F_(0, 0)	FM(D1)		F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP5_15_12	FM(VI1_VSYNC_N)			FM(MSIOF1_SYNC)		F_(0, 0)	FM(D2)		F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP5_19_16	FM(VI1_DATA0)			FM(MSIOF1_SS1)		F_(0, 0)	FM(D3)		F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP5_23_20	FM(VI1_DATA1)			FM(MSIOF1_SS2)		F_(0, 0)	FM(D4)		FM(MMC_CMD)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP5_27_24	FM(VI1_DATA2)			FM(CANFD0_TX_B)		F_(0, 0)	FM(D5)		FM(MMC_D0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP5_31_28	FM(VI1_DATA3)			FM(CANFD0_RX_B)		F_(0, 0)	FM(D6)		FM(MMC_D1)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP6_3_0		FM(VI1_DATA4)			FM(CANFD_CLK_B)		F_(0, 0)	FM(D7)		FM(MMC_D2)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP6_7_4		FM(VI1_DATA5)			F_(0,0)			FM(SCK4)	FM(D8)		FM(MMC_D3)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP6_11_8	FM(VI1_DATA6)			F_(0,0)			FM(RX4)		FM(D9)		FM(MMC_CLK)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP6_15_12	FM(VI1_DATA7)			F_(0,0)			FM(TX4)		FM(D10)		FM(MMC_D4)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP6_19_16	FM(VI1_DATA8)			F_(0,0)			FM(CTS4_N)	FM(D11)		FM(MMC_D5)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP6_23_20	FM(VI1_DATA9)			F_(0,0)			FM(RTS4_N)	FM(D12)		FM(MMC_D6)		FM(SCL3_B)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP6_27_24	FM(VI1_DATA10)			F_(0,0)			F_(0, 0)	FM(D13)		FM(MMC_D7)		FM(SDA3_B)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP6_31_28	FM(VI1_DATA11)			FM(SCL4)		FM(IRQ4)	FM(D14)		FM(MMC_WP)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP7_3_0		FM(VI1_FIELD)			FM(SDA4)		FM(IRQ5)	FM(D15)		FM(MMC_CD)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP7_7_4		FM(SCL0)			FM(DU_DR0)		FM(TPU0TO0)	FM(CLKOUT)	F_(0, 0)		FM(MSIOF0_RXD)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP7_11_8	FM(SDA0)			FM(DU_DR1)		FM(TPU0TO1)	FM(BS_N)	FM(SCK0)		FM(MSIOF0_TXD)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP7_15_12	FM(SCL1)			FM(DU_DG0)		FM(TPU0TO2)	FM(RD_N)	FM(CTS0_N)		FM(MSIOF0_SCK)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP7_19_16	FM(SDA1)			FM(DU_DG1)		FM(TPU0TO3)	FM(WE0_N)	FM(RTS0_N)		FM(MSIOF0_SYNC)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP7_23_20	FM(SCL2)			FM(DU_DB0)		FM(TCLK1_A)	FM(WE1_N)	FM(RX0)			FM(MSIOF0_SS1)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP7_27_24	FM(SDA2)			FM(DU_DB1)		FM(TCLK2_A)	FM(EX_WAIT0)	FM(TX0)			FM(MSIOF0_SS2)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP7_31_28	FM(AVB0_AVTP_CAPTURE)		F_(0, 0)		F_(0, 0)	F_(0, 0)	FM(FSCLKST2_N_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP8_3_0		FM(CANFD0_TX_A)			FM(FXR_TXDA)		FM(PWM0_B)	FM(DU_DISP)	FM(FSCLKST2_N_C)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP8_7_4		FM(CANFD0_RX_A)			FM(RXDA_EXTFXR)		FM(PWM1_B)	FM(DU_CDE)	F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP8_11_8	FM(CANFD1_TX)			FM(FXR_TXDB)		FM(PWM2_B)	FM(TCLK1_B)	FM(TX1_B)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP8_15_12	FM(CANFD1_RX)			FM(RXDB_EXTFXR)		FM(PWM3_B)	FM(TCLK2_B)	FM(RX1_B)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP8_19_16	FM(CANFD_CLK_A)			FM(CLK_EXTFXR)		FM(PWM4_B)	FM(SPEEDIN_B)	FM(SCIF_CLK_B)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP8_23_20	FM(DIGRF_CLKIN)			FM(DIGRF_CLKEN_IN)	F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP8_27_24	FM(DIGRF_CLKOUT)		FM(DIGRF_CLKEN_OUT)	F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP8_31_28	F_(0, 0)			F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0)

#define PINMUX_GPSR	\
\
		GPSR1_27 \
		GPSR1_26 \
		GPSR1_25 \
		GPSR1_24 \
		GPSR1_23 \
		GPSR1_22 \
GPSR0_21	GPSR1_21 \
GPSR0_20	GPSR1_20 \
GPSR0_19	GPSR1_19 \
GPSR0_18	GPSR1_18 \
GPSR0_17	GPSR1_17 \
GPSR0_16	GPSR1_16	GPSR2_16	GPSR3_16 \
GPSR0_15	GPSR1_15	GPSR2_15	GPSR3_15 \
GPSR0_14	GPSR1_14	GPSR2_14	GPSR3_14			GPSR5_14 \
GPSR0_13	GPSR1_13	GPSR2_13	GPSR3_13			GPSR5_13 \
GPSR0_12	GPSR1_12	GPSR2_12	GPSR3_12			GPSR5_12 \
GPSR0_11	GPSR1_11	GPSR2_11	GPSR3_11			GPSR5_11 \
GPSR0_10	GPSR1_10	GPSR2_10	GPSR3_10			GPSR5_10 \
GPSR0_9		GPSR1_9		GPSR2_9		GPSR3_9				GPSR5_9 \
GPSR0_8		GPSR1_8		GPSR2_8		GPSR3_8				GPSR5_8 \
GPSR0_7		GPSR1_7		GPSR2_7		GPSR3_7				GPSR5_7 \
GPSR0_6		GPSR1_6		GPSR2_6		GPSR3_6				GPSR5_6 \
GPSR0_5		GPSR1_5		GPSR2_5		GPSR3_5		GPSR4_5		GPSR5_5 \
GPSR0_4		GPSR1_4		GPSR2_4		GPSR3_4		GPSR4_4		GPSR5_4 \
GPSR0_3		GPSR1_3		GPSR2_3		GPSR3_3		GPSR4_3		GPSR5_3 \
GPSR0_2		GPSR1_2		GPSR2_2		GPSR3_2		GPSR4_2		GPSR5_2 \
GPSR0_1		GPSR1_1		GPSR2_1		GPSR3_1		GPSR4_1		GPSR5_1 \
GPSR0_0		GPSR1_0		GPSR2_0		GPSR3_0		GPSR4_0		GPSR5_0

#define PINMUX_IPSR	\
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
FM(IP8_3_0)	IP8_3_0 \
FM(IP8_7_4)	IP8_7_4 \
FM(IP8_11_8)	IP8_11_8 \
FM(IP8_15_12)	IP8_15_12 \
FM(IP8_19_16)	IP8_19_16 \
FM(IP8_23_20)	IP8_23_20 \
FM(IP8_27_24)	IP8_27_24 \
FM(IP8_31_28)	IP8_31_28

/* MOD_SEL0 */		/* 0 */			/* 1 */
#define MOD_SEL0_11	FM(SEL_I2C3_0)		FM(SEL_I2C3_1)
#define MOD_SEL0_10	FM(SEL_HSCIF0_0)	FM(SEL_HSCIF0_1)
#define MOD_SEL0_9	FM(SEL_SCIF1_0)		FM(SEL_SCIF1_1)
#define MOD_SEL0_8	FM(SEL_CANFD0_0)	FM(SEL_CANFD0_1)
#define MOD_SEL0_7	FM(SEL_PWM4_0)		FM(SEL_PWM4_1)
#define MOD_SEL0_6	FM(SEL_PWM3_0)		FM(SEL_PWM3_1)
#define MOD_SEL0_5	FM(SEL_PWM2_0)		FM(SEL_PWM2_1)
#define MOD_SEL0_4	FM(SEL_PWM1_0)		FM(SEL_PWM1_1)
#define MOD_SEL0_3	FM(SEL_PWM0_0)		FM(SEL_PWM0_1)
#define MOD_SEL0_2	FM(SEL_RFSO_0)		FM(SEL_RFSO_1)
#define MOD_SEL0_1	FM(SEL_RSP_0)		FM(SEL_RSP_1)
#define MOD_SEL0_0	FM(SEL_TMU_0)		FM(SEL_TMU_1)

#define PINMUX_MOD_SELS \
\
MOD_SEL0_11 \
MOD_SEL0_10 \
MOD_SEL0_9 \
MOD_SEL0_8 \
MOD_SEL0_7 \
MOD_SEL0_6 \
MOD_SEL0_5 \
MOD_SEL0_4 \
MOD_SEL0_3 \
MOD_SEL0_2 \
MOD_SEL0_1 \
MOD_SEL0_0

enum {
	PINMUX_RESERVED = 0,

	PINMUX_DATA_BEGIN,
	GP_ALL(DATA),
	PINMUX_DATA_END,

#define F_(x, y)
#define FM(x)   FN_##x,
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
	PINMUX_MARK_END,
#undef F_
#undef FM
};

static const u16 pinmux_data[] = {
	PINMUX_DATA_GP_ALL(),

	PINMUX_SINGLE(AVB0_RX_CTL),
	PINMUX_SINGLE(AVB0_RXC),
	PINMUX_SINGLE(AVB0_RD0),
	PINMUX_SINGLE(AVB0_RD1),
	PINMUX_SINGLE(AVB0_RD2),
	PINMUX_SINGLE(AVB0_RD3),
	PINMUX_SINGLE(AVB0_TX_CTL),
	PINMUX_SINGLE(AVB0_TXC),
	PINMUX_SINGLE(AVB0_TD0),
	PINMUX_SINGLE(AVB0_TD1),
	PINMUX_SINGLE(AVB0_TD2),
	PINMUX_SINGLE(AVB0_TD3),
	PINMUX_SINGLE(AVB0_TXCREFCLK),
	PINMUX_SINGLE(AVB0_MDIO),
	PINMUX_SINGLE(AVB0_MDC),
	PINMUX_SINGLE(AVB0_MAGIC),
	PINMUX_SINGLE(AVB0_PHY_INT),
	PINMUX_SINGLE(AVB0_LINK),
	PINMUX_SINGLE(AVB0_AVTP_MATCH),

	PINMUX_SINGLE(QSPI0_SPCLK),
	PINMUX_SINGLE(QSPI0_MOSI_IO0),
	PINMUX_SINGLE(QSPI0_MISO_IO1),
	PINMUX_SINGLE(QSPI0_IO2),
	PINMUX_SINGLE(QSPI0_IO3),
	PINMUX_SINGLE(QSPI0_SSL),
	PINMUX_SINGLE(QSPI1_SPCLK),
	PINMUX_SINGLE(QSPI1_MOSI_IO0),
	PINMUX_SINGLE(QSPI1_MISO_IO1),
	PINMUX_SINGLE(QSPI1_IO2),
	PINMUX_SINGLE(QSPI1_IO3),
	PINMUX_SINGLE(QSPI1_SSL),
	PINMUX_SINGLE(RPC_RESET_N),
	PINMUX_SINGLE(RPC_WP_N),
	PINMUX_SINGLE(RPC_INT_N),

	/* IPSR0 */
	PINMUX_IPSR_GPSR(IP0_3_0,	DU_DR2),
	PINMUX_IPSR_GPSR(IP0_3_0,	HSCK0),
	PINMUX_IPSR_GPSR(IP0_3_0,	A0),

	PINMUX_IPSR_GPSR(IP0_7_4,	DU_DR3),
	PINMUX_IPSR_GPSR(IP0_7_4,	HRTS0_N),
	PINMUX_IPSR_GPSR(IP0_7_4,	A1),

	PINMUX_IPSR_GPSR(IP0_11_8,	DU_DR4),
	PINMUX_IPSR_GPSR(IP0_11_8,	HCTS0_N),
	PINMUX_IPSR_GPSR(IP0_11_8,	A2),

	PINMUX_IPSR_GPSR(IP0_15_12,	DU_DR5),
	PINMUX_IPSR_GPSR(IP0_15_12,	HTX0),
	PINMUX_IPSR_GPSR(IP0_15_12,	A3),

	PINMUX_IPSR_GPSR(IP0_19_16,	DU_DR6),
	PINMUX_IPSR_GPSR(IP0_19_16,	MSIOF3_RXD),
	PINMUX_IPSR_GPSR(IP0_19_16,	A4),

	PINMUX_IPSR_GPSR(IP0_23_20,	DU_DR7),
	PINMUX_IPSR_GPSR(IP0_23_20,	MSIOF3_TXD),
	PINMUX_IPSR_GPSR(IP0_23_20,	A5),

	PINMUX_IPSR_GPSR(IP0_27_24,	DU_DG2),
	PINMUX_IPSR_GPSR(IP0_27_24,	MSIOF3_SS1),
	PINMUX_IPSR_GPSR(IP0_27_24,	A6),

	PINMUX_IPSR_GPSR(IP0_31_28,	DU_DG3),
	PINMUX_IPSR_GPSR(IP0_31_28,	MSIOF3_SS2),
	PINMUX_IPSR_GPSR(IP0_31_28,	A7),
	PINMUX_IPSR_GPSR(IP0_31_28,	PWMFSW0),

	/* IPSR1 */
	PINMUX_IPSR_GPSR(IP1_3_0,	DU_DG4),
	PINMUX_IPSR_GPSR(IP1_3_0,	A8),
	PINMUX_IPSR_MSEL(IP1_3_0,	FSO_CFE_0_N_A,	SEL_RFSO_0),

	PINMUX_IPSR_GPSR(IP1_7_4,	DU_DG5),
	PINMUX_IPSR_GPSR(IP1_7_4,	A9),
	PINMUX_IPSR_MSEL(IP1_7_4,	FSO_CFE_1_N_A,	SEL_RFSO_0),

	PINMUX_IPSR_GPSR(IP1_11_8,	DU_DG6),
	PINMUX_IPSR_GPSR(IP1_11_8,	A10),
	PINMUX_IPSR_MSEL(IP1_11_8,	FSO_TOE_N_A,	SEL_RFSO_0),

	PINMUX_IPSR_GPSR(IP1_15_12,	DU_DG7),
	PINMUX_IPSR_GPSR(IP1_15_12,	A11),
	PINMUX_IPSR_GPSR(IP1_15_12,	IRQ1),

	PINMUX_IPSR_GPSR(IP1_19_16,	DU_DB2),
	PINMUX_IPSR_GPSR(IP1_19_16,	A12),
	PINMUX_IPSR_GPSR(IP1_19_16,	IRQ2),

	PINMUX_IPSR_GPSR(IP1_23_20,	DU_DB3),
	PINMUX_IPSR_GPSR(IP1_23_20,	A13),
	PINMUX_IPSR_GPSR(IP1_23_20,	FXR_CLKOUT1),

	PINMUX_IPSR_GPSR(IP1_27_24,	DU_DB4),
	PINMUX_IPSR_GPSR(IP1_27_24,	A14),
	PINMUX_IPSR_GPSR(IP1_27_24,	FXR_CLKOUT2),

	PINMUX_IPSR_GPSR(IP1_31_28,	DU_DB5),
	PINMUX_IPSR_GPSR(IP1_31_28,	A15),
	PINMUX_IPSR_GPSR(IP1_31_28,	FXR_TXENA_N),

	/* IPSR2 */
	PINMUX_IPSR_GPSR(IP2_3_0,	DU_DB6),
	PINMUX_IPSR_GPSR(IP2_3_0,	A16),
	PINMUX_IPSR_GPSR(IP2_3_0,	FXR_TXENB_N),

	PINMUX_IPSR_GPSR(IP2_7_4,	DU_DB7),
	PINMUX_IPSR_GPSR(IP2_7_4,	A17),

	PINMUX_IPSR_GPSR(IP2_11_8,	DU_DOTCLKOUT),
	PINMUX_IPSR_MSEL(IP2_11_8,	SCIF_CLK_A,	SEL_HSCIF0_0),
	PINMUX_IPSR_GPSR(IP2_11_8,	A18),

	PINMUX_IPSR_GPSR(IP2_15_12,	DU_EXHSYNC_DU_HSYNC),
	PINMUX_IPSR_GPSR(IP2_15_12,	HRX0),
	PINMUX_IPSR_GPSR(IP2_15_12,	A19),
	PINMUX_IPSR_GPSR(IP2_15_12,	IRQ3),

	PINMUX_IPSR_GPSR(IP2_19_16,	DU_EXVSYNC_DU_VSYNC),
	PINMUX_IPSR_GPSR(IP2_19_16,	MSIOF3_SCK),

	PINMUX_IPSR_GPSR(IP2_23_20,	DU_EXODDF_DU_ODDF_DISP_CDE),
	PINMUX_IPSR_GPSR(IP2_23_20,	MSIOF3_SYNC),

	PINMUX_IPSR_GPSR(IP2_27_24,	IRQ0),

	PINMUX_IPSR_GPSR(IP2_31_28,	VI0_CLK),
	PINMUX_IPSR_GPSR(IP2_31_28,	MSIOF2_SCK),
	PINMUX_IPSR_GPSR(IP2_31_28,	SCK3),
	PINMUX_IPSR_GPSR(IP2_31_28,	HSCK3),

	/* IPSR3 */
	PINMUX_IPSR_GPSR(IP3_3_0,	VI0_CLKENB),
	PINMUX_IPSR_GPSR(IP3_3_0,	MSIOF2_RXD),
	PINMUX_IPSR_GPSR(IP3_3_0,	RX3),
	PINMUX_IPSR_GPSR(IP3_3_0,	RD_WR_N),
	PINMUX_IPSR_GPSR(IP3_3_0,	HCTS3_N),

	PINMUX_IPSR_GPSR(IP3_7_4,	VI0_HSYNC_N),
	PINMUX_IPSR_GPSR(IP3_7_4,	MSIOF2_TXD),
	PINMUX_IPSR_GPSR(IP3_7_4,	TX3),
	PINMUX_IPSR_GPSR(IP3_7_4,	HRTS3_N),

	PINMUX_IPSR_GPSR(IP3_11_8,	VI0_VSYNC_N),
	PINMUX_IPSR_GPSR(IP3_11_8,	MSIOF2_SYNC),
	PINMUX_IPSR_GPSR(IP3_11_8,	CTS3_N),
	PINMUX_IPSR_GPSR(IP3_11_8,	HTX3),

	PINMUX_IPSR_GPSR(IP3_15_12,	VI0_DATA0),
	PINMUX_IPSR_GPSR(IP3_15_12,	MSIOF2_SS1),
	PINMUX_IPSR_GPSR(IP3_15_12,	RTS3_N),
	PINMUX_IPSR_GPSR(IP3_15_12,	HRX3),

	PINMUX_IPSR_GPSR(IP3_19_16,	VI0_DATA1),
	PINMUX_IPSR_GPSR(IP3_19_16,	MSIOF2_SS2),
	PINMUX_IPSR_GPSR(IP3_19_16,	SCK1),
	PINMUX_IPSR_MSEL(IP3_19_16,	SPEEDIN_A,	SEL_RSP_0),

	PINMUX_IPSR_GPSR(IP3_23_20,	VI0_DATA2),
	PINMUX_IPSR_GPSR(IP3_23_20,	AVB0_AVTP_PPS),
	PINMUX_IPSR_MSEL(IP3_23_20,	SDA3_A,		SEL_I2C3_0),

	PINMUX_IPSR_GPSR(IP3_27_24,	VI0_DATA3),
	PINMUX_IPSR_GPSR(IP3_27_24,	HSCK1),
	PINMUX_IPSR_MSEL(IP3_27_24,	SCL3_A,		SEL_I2C3_0),

	PINMUX_IPSR_GPSR(IP3_31_28,	VI0_DATA4),
	PINMUX_IPSR_GPSR(IP3_31_28,	HRTS1_N),
	PINMUX_IPSR_MSEL(IP3_31_28,	RX1_A,	SEL_SCIF1_0),

	/* IPSR4 */
	PINMUX_IPSR_GPSR(IP4_3_0,	VI0_DATA5),
	PINMUX_IPSR_GPSR(IP4_3_0,	HCTS1_N),
	PINMUX_IPSR_MSEL(IP4_3_0,	TX1_A,	SEL_SCIF1_0),

	PINMUX_IPSR_GPSR(IP4_7_4,	VI0_DATA6),
	PINMUX_IPSR_GPSR(IP4_7_4,	HTX1),
	PINMUX_IPSR_GPSR(IP4_7_4,	CTS1_N),

	PINMUX_IPSR_GPSR(IP4_11_8,	VI0_DATA7),
	PINMUX_IPSR_GPSR(IP4_11_8,	HRX1),
	PINMUX_IPSR_GPSR(IP4_11_8,	RTS1_N),

	PINMUX_IPSR_GPSR(IP4_15_12,	VI0_DATA8),
	PINMUX_IPSR_GPSR(IP4_15_12,	HSCK2),
	PINMUX_IPSR_MSEL(IP4_15_12,	PWM0_A,	SEL_PWM0_0),

	PINMUX_IPSR_GPSR(IP4_19_16,	VI0_DATA9),
	PINMUX_IPSR_GPSR(IP4_19_16,	HCTS2_N),
	PINMUX_IPSR_MSEL(IP4_19_16,	PWM1_A,	SEL_PWM1_0),
	PINMUX_IPSR_MSEL(IP4_19_16,	FSO_CFE_0_N_B,	SEL_RFSO_1),

	PINMUX_IPSR_GPSR(IP4_23_20,	VI0_DATA10),
	PINMUX_IPSR_GPSR(IP4_23_20,	HRTS2_N),
	PINMUX_IPSR_MSEL(IP4_23_20,	PWM2_A,	SEL_PWM2_0),
	PINMUX_IPSR_MSEL(IP4_23_20,	FSO_CFE_1_N_B,	SEL_RFSO_1),

	PINMUX_IPSR_GPSR(IP4_27_24,	VI0_DATA11),
	PINMUX_IPSR_GPSR(IP4_27_24,	HTX2),
	PINMUX_IPSR_MSEL(IP4_27_24,	PWM3_A,	SEL_PWM3_0),
	PINMUX_IPSR_MSEL(IP4_27_24,	FSO_TOE_N_B,	SEL_RFSO_1),

	PINMUX_IPSR_GPSR(IP4_31_28,	VI0_FIELD),
	PINMUX_IPSR_GPSR(IP4_31_28,	HRX2),
	PINMUX_IPSR_MSEL(IP4_31_28,	PWM4_A,	SEL_PWM4_0),
	PINMUX_IPSR_GPSR(IP4_31_28,	CS1_N),
	PINMUX_IPSR_GPSR(IP4_31_28,	FSCLKST2_N_A),

	/* IPSR5 */
	PINMUX_IPSR_GPSR(IP5_3_0,	VI1_CLK),
	PINMUX_IPSR_GPSR(IP5_3_0,	MSIOF1_RXD),
	PINMUX_IPSR_GPSR(IP5_3_0,	CS0_N),

	PINMUX_IPSR_GPSR(IP5_7_4,	VI1_CLKENB),
	PINMUX_IPSR_GPSR(IP5_7_4,	MSIOF1_TXD),
	PINMUX_IPSR_GPSR(IP5_7_4,	D0),

	PINMUX_IPSR_GPSR(IP5_11_8,	VI1_HSYNC_N),
	PINMUX_IPSR_GPSR(IP5_11_8,	MSIOF1_SCK),
	PINMUX_IPSR_GPSR(IP5_11_8,	D1),

	PINMUX_IPSR_GPSR(IP5_15_12,	VI1_VSYNC_N),
	PINMUX_IPSR_GPSR(IP5_15_12,	MSIOF1_SYNC),
	PINMUX_IPSR_GPSR(IP5_15_12,	D2),

	PINMUX_IPSR_GPSR(IP5_19_16,	VI1_DATA0),
	PINMUX_IPSR_GPSR(IP5_19_16,	MSIOF1_SS1),
	PINMUX_IPSR_GPSR(IP5_19_16,	D3),

	PINMUX_IPSR_GPSR(IP5_23_20,	VI1_DATA1),
	PINMUX_IPSR_GPSR(IP5_23_20,	MSIOF1_SS2),
	PINMUX_IPSR_GPSR(IP5_23_20,	D4),
	PINMUX_IPSR_GPSR(IP5_23_20,	MMC_CMD),

	PINMUX_IPSR_GPSR(IP5_27_24,	VI1_DATA2),
	PINMUX_IPSR_MSEL(IP5_27_24,	CANFD0_TX_B,	SEL_CANFD0_1),
	PINMUX_IPSR_GPSR(IP5_27_24,	D5),
	PINMUX_IPSR_GPSR(IP5_27_24,	MMC_D0),

	PINMUX_IPSR_GPSR(IP5_31_28,	VI1_DATA3),
	PINMUX_IPSR_MSEL(IP5_31_28,	CANFD0_RX_B,	SEL_CANFD0_1),
	PINMUX_IPSR_GPSR(IP5_31_28,	D6),
	PINMUX_IPSR_GPSR(IP5_31_28,	MMC_D1),

	/* IPSR6 */
	PINMUX_IPSR_GPSR(IP6_3_0,	VI1_DATA4),
	PINMUX_IPSR_MSEL(IP6_3_0,	CANFD_CLK_B,	SEL_CANFD0_1),
	PINMUX_IPSR_GPSR(IP6_3_0,	D7),
	PINMUX_IPSR_GPSR(IP6_3_0,	MMC_D2),

	PINMUX_IPSR_GPSR(IP6_7_4,	VI1_DATA5),
	PINMUX_IPSR_GPSR(IP6_7_4,	SCK4),
	PINMUX_IPSR_GPSR(IP6_7_4,	D8),
	PINMUX_IPSR_GPSR(IP6_7_4,	MMC_D3),

	PINMUX_IPSR_GPSR(IP6_11_8,	VI1_DATA6),
	PINMUX_IPSR_GPSR(IP6_11_8,	RX4),
	PINMUX_IPSR_GPSR(IP6_11_8,	D9),
	PINMUX_IPSR_GPSR(IP6_11_8,	MMC_CLK),

	PINMUX_IPSR_GPSR(IP6_15_12,	VI1_DATA7),
	PINMUX_IPSR_GPSR(IP6_15_12,	TX4),
	PINMUX_IPSR_GPSR(IP6_15_12,	D10),
	PINMUX_IPSR_GPSR(IP6_15_12,	MMC_D4),

	PINMUX_IPSR_GPSR(IP6_19_16,	VI1_DATA8),
	PINMUX_IPSR_GPSR(IP6_19_16,	CTS4_N),
	PINMUX_IPSR_GPSR(IP6_19_16,	D11),
	PINMUX_IPSR_GPSR(IP6_19_16,	MMC_D5),

	PINMUX_IPSR_GPSR(IP6_23_20,	VI1_DATA9),
	PINMUX_IPSR_GPSR(IP6_23_20,	RTS4_N),
	PINMUX_IPSR_GPSR(IP6_23_20,	D12),
	PINMUX_IPSR_GPSR(IP6_23_20,	MMC_D6),
	PINMUX_IPSR_MSEL(IP6_23_20,	SCL3_B,	SEL_I2C3_1),

	PINMUX_IPSR_GPSR(IP6_27_24,	VI1_DATA10),
	PINMUX_IPSR_GPSR(IP6_27_24,	D13),
	PINMUX_IPSR_GPSR(IP6_27_24,	MMC_D7),
	PINMUX_IPSR_MSEL(IP6_27_24,	SDA3_B,	SEL_I2C3_1),

	PINMUX_IPSR_GPSR(IP6_31_28,	VI1_DATA11),
	PINMUX_IPSR_GPSR(IP6_31_28,	SCL4),
	PINMUX_IPSR_GPSR(IP6_31_28,	IRQ4),
	PINMUX_IPSR_GPSR(IP6_31_28,	D14),
	PINMUX_IPSR_GPSR(IP6_31_28,	MMC_WP),

	/* IPSR7 */
	PINMUX_IPSR_GPSR(IP7_3_0,	VI1_FIELD),
	PINMUX_IPSR_GPSR(IP7_3_0,	SDA4),
	PINMUX_IPSR_GPSR(IP7_3_0,	IRQ5),
	PINMUX_IPSR_GPSR(IP7_3_0,	D15),
	PINMUX_IPSR_GPSR(IP7_3_0,	MMC_CD),

	PINMUX_IPSR_GPSR(IP7_7_4,	SCL0),
	PINMUX_IPSR_GPSR(IP7_7_4,	DU_DR0),
	PINMUX_IPSR_GPSR(IP7_7_4,	TPU0TO0),
	PINMUX_IPSR_GPSR(IP7_7_4,	CLKOUT),
	PINMUX_IPSR_GPSR(IP7_7_4,	MSIOF0_RXD),

	PINMUX_IPSR_GPSR(IP7_11_8,	SDA0),
	PINMUX_IPSR_GPSR(IP7_11_8,	DU_DR1),
	PINMUX_IPSR_GPSR(IP7_11_8,	TPU0TO1),
	PINMUX_IPSR_GPSR(IP7_11_8,	BS_N),
	PINMUX_IPSR_GPSR(IP7_11_8,	SCK0),
	PINMUX_IPSR_GPSR(IP7_11_8,	MSIOF0_TXD),

	PINMUX_IPSR_GPSR(IP7_15_12,	SCL1),
	PINMUX_IPSR_GPSR(IP7_15_12,	DU_DG0),
	PINMUX_IPSR_GPSR(IP7_15_12,	TPU0TO2),
	PINMUX_IPSR_GPSR(IP7_15_12,	RD_N),
	PINMUX_IPSR_GPSR(IP7_15_12,	CTS0_N),
	PINMUX_IPSR_GPSR(IP7_15_12,	MSIOF0_SCK),

	PINMUX_IPSR_GPSR(IP7_19_16,	SDA1),
	PINMUX_IPSR_GPSR(IP7_19_16,	DU_DG1),
	PINMUX_IPSR_GPSR(IP7_19_16,	TPU0TO3),
	PINMUX_IPSR_GPSR(IP7_19_16,	WE0_N),
	PINMUX_IPSR_GPSR(IP7_19_16,	RTS0_N),
	PINMUX_IPSR_GPSR(IP7_19_16,	MSIOF0_SYNC),

	PINMUX_IPSR_GPSR(IP7_23_20,	SCL2),
	PINMUX_IPSR_GPSR(IP7_23_20,	DU_DB0),
	PINMUX_IPSR_MSEL(IP7_23_20,	TCLK1_A,	SEL_TMU_0),
	PINMUX_IPSR_GPSR(IP7_23_20,	WE1_N),
	PINMUX_IPSR_GPSR(IP7_23_20,	RX0),
	PINMUX_IPSR_GPSR(IP7_23_20,	MSIOF0_SS1),

	PINMUX_IPSR_GPSR(IP7_27_24,	SDA2),
	PINMUX_IPSR_GPSR(IP7_27_24,	DU_DB1),
	PINMUX_IPSR_MSEL(IP7_27_24,	TCLK2_A,	SEL_TMU_0),
	PINMUX_IPSR_GPSR(IP7_27_24,	EX_WAIT0),
	PINMUX_IPSR_GPSR(IP7_27_24,	TX0),
	PINMUX_IPSR_GPSR(IP7_27_24,	MSIOF0_SS2),

	PINMUX_IPSR_GPSR(IP7_31_28,	AVB0_AVTP_CAPTURE),
	PINMUX_IPSR_GPSR(IP7_31_28,	FSCLKST2_N_B),

	/* IPSR8 */
	PINMUX_IPSR_MSEL(IP8_3_0,	CANFD0_TX_A,	SEL_CANFD0_0),
	PINMUX_IPSR_GPSR(IP8_3_0,	FXR_TXDA),
	PINMUX_IPSR_MSEL(IP8_3_0,	PWM0_B,		SEL_PWM0_1),
	PINMUX_IPSR_GPSR(IP8_3_0,	DU_DISP),
	PINMUX_IPSR_GPSR(IP8_3_0,	FSCLKST2_N_C),

	PINMUX_IPSR_MSEL(IP8_7_4,	CANFD0_RX_A,	SEL_CANFD0_0),
	PINMUX_IPSR_GPSR(IP8_7_4,	RXDA_EXTFXR),
	PINMUX_IPSR_MSEL(IP8_7_4,	PWM1_B,		SEL_PWM1_1),
	PINMUX_IPSR_GPSR(IP8_7_4,	DU_CDE),

	PINMUX_IPSR_GPSR(IP8_11_8,	CANFD1_TX),
	PINMUX_IPSR_GPSR(IP8_11_8,	FXR_TXDB),
	PINMUX_IPSR_MSEL(IP8_11_8,	PWM2_B,		SEL_PWM2_1),
	PINMUX_IPSR_MSEL(IP8_11_8,	TCLK1_B,	SEL_TMU_1),
	PINMUX_IPSR_MSEL(IP8_11_8,	TX1_B,		SEL_SCIF1_1),

	PINMUX_IPSR_GPSR(IP8_15_12,	CANFD1_RX),
	PINMUX_IPSR_GPSR(IP8_15_12,	RXDB_EXTFXR),
	PINMUX_IPSR_MSEL(IP8_15_12,	PWM3_B,		SEL_PWM3_1),
	PINMUX_IPSR_MSEL(IP8_15_12,	TCLK2_B,	SEL_TMU_1),
	PINMUX_IPSR_MSEL(IP8_15_12,	RX1_B,		SEL_SCIF1_1),

	PINMUX_IPSR_MSEL(IP8_19_16,	CANFD_CLK_A,	SEL_CANFD0_0),
	PINMUX_IPSR_GPSR(IP8_19_16,	CLK_EXTFXR),
	PINMUX_IPSR_MSEL(IP8_19_16,	PWM4_B,		SEL_PWM4_1),
	PINMUX_IPSR_MSEL(IP8_19_16,	SPEEDIN_B,	SEL_RSP_1),
	PINMUX_IPSR_MSEL(IP8_19_16,	SCIF_CLK_B,	SEL_HSCIF0_1),

	PINMUX_IPSR_GPSR(IP8_23_20,	DIGRF_CLKIN),
	PINMUX_IPSR_GPSR(IP8_23_20,	DIGRF_CLKEN_IN),

	PINMUX_IPSR_GPSR(IP8_27_24,	DIGRF_CLKOUT),
	PINMUX_IPSR_GPSR(IP8_27_24,	DIGRF_CLKEN_OUT),
};

static const struct sh_pfc_pin pinmux_pins[] = {
	PINMUX_GPIO_GP_ALL(),
};

/* - AVB0 ------------------------------------------------------------------- */
static const unsigned int avb0_link_pins[] = {
	/* AVB0_LINK */
	RCAR_GP_PIN(1, 18),
};
static const unsigned int avb0_link_mux[] = {
	AVB0_LINK_MARK,
};
static const unsigned int avb0_magic_pins[] = {
	/* AVB0_MAGIC */
	RCAR_GP_PIN(1, 16),
};
static const unsigned int avb0_magic_mux[] = {
	AVB0_MAGIC_MARK,
};
static const unsigned int avb0_phy_int_pins[] = {
	/* AVB0_PHY_INT */
	RCAR_GP_PIN(1, 17),
};
static const unsigned int avb0_phy_int_mux[] = {
	AVB0_PHY_INT_MARK,
};
static const unsigned int avb0_mdio_pins[] = {
	/* AVB0_MDC, AVB0_MDIO */
	RCAR_GP_PIN(1, 15), RCAR_GP_PIN(1, 14),
};
static const unsigned int avb0_mdio_mux[] = {
	AVB0_MDC_MARK, AVB0_MDIO_MARK,
};
static const unsigned int avb0_rgmii_pins[] = {
	/*
	 * AVB0_TX_CTL, AVB0_TXC, AVB0_TD0, AVB0_TD1, AVB0_TD2, AVB0_TD3,
	 * AVB0_RX_CTL, AVB0_RXC, AVB0_RD0, AVB0_RD1, AVB0_RD2, AVB0_RD3
	 */
	RCAR_GP_PIN(1, 7), RCAR_GP_PIN(1, 8),
	RCAR_GP_PIN(1, 9), RCAR_GP_PIN(1, 10),
	RCAR_GP_PIN(1, 11), RCAR_GP_PIN(1, 12),
	RCAR_GP_PIN(1, 1), RCAR_GP_PIN(1, 2),
	RCAR_GP_PIN(1, 3), RCAR_GP_PIN(1, 4),
	RCAR_GP_PIN(1, 5), RCAR_GP_PIN(1, 6),
};
static const unsigned int avb0_rgmii_mux[] = {
	AVB0_TX_CTL_MARK, AVB0_TXC_MARK,
	AVB0_TD0_MARK, AVB0_TD1_MARK, AVB0_TD2_MARK, AVB0_TD3_MARK,
	AVB0_RX_CTL_MARK, AVB0_RXC_MARK,
	AVB0_RD0_MARK, AVB0_RD1_MARK, AVB0_RD2_MARK, AVB0_RD3_MARK,
};
static const unsigned int avb0_txcrefclk_pins[] = {
	/* AVB0_TXCREFCLK */
	RCAR_GP_PIN(1, 13),
};
static const unsigned int avb0_txcrefclk_mux[] = {
	AVB0_TXCREFCLK_MARK,
};
static const unsigned int avb0_avtp_pps_pins[] = {
	/* AVB0_AVTP_PPS */
	RCAR_GP_PIN(2, 6),
};
static const unsigned int avb0_avtp_pps_mux[] = {
	AVB0_AVTP_PPS_MARK,
};
static const unsigned int avb0_avtp_capture_pins[] = {
	/* AVB0_AVTP_CAPTURE */
	RCAR_GP_PIN(1, 20),
};
static const unsigned int avb0_avtp_capture_mux[] = {
	AVB0_AVTP_CAPTURE_MARK,
};
static const unsigned int avb0_avtp_match_pins[] = {
	/* AVB0_AVTP_MATCH */
	RCAR_GP_PIN(1, 19),
};
static const unsigned int avb0_avtp_match_mux[] = {
	AVB0_AVTP_MATCH_MARK,
};

/* - CANFD Clock ------------------------------------------------------------ */
static const unsigned int canfd_clk_a_pins[] = {
	/* CANFD_CLK */
	RCAR_GP_PIN(1, 25),
};
static const unsigned int canfd_clk_a_mux[] = {
	CANFD_CLK_A_MARK,
};
static const unsigned int canfd_clk_b_pins[] = {
	/* CANFD_CLK */
	RCAR_GP_PIN(3, 8),
};
static const unsigned int canfd_clk_b_mux[] = {
	CANFD_CLK_B_MARK,
};

/* - CANFD0 ----------------------------------------------------------------- */
static const unsigned int canfd0_data_a_pins[] = {
	/* TX, RX */
	RCAR_GP_PIN(1, 21), RCAR_GP_PIN(1, 22),
};
static const unsigned int canfd0_data_a_mux[] = {
	CANFD0_TX_A_MARK, CANFD0_RX_A_MARK,
};
static const unsigned int canfd0_data_b_pins[] = {
	/* TX, RX */
	RCAR_GP_PIN(3, 6), RCAR_GP_PIN(3, 7),
};
static const unsigned int canfd0_data_b_mux[] = {
	CANFD0_TX_B_MARK, CANFD0_RX_B_MARK,
};

/* - CANFD1 ----------------------------------------------------------------- */
static const unsigned int canfd1_data_pins[] = {
	/* TX, RX */
	RCAR_GP_PIN(1, 23), RCAR_GP_PIN(1, 24),
};
static const unsigned int canfd1_data_mux[] = {
	CANFD1_TX_MARK, CANFD1_RX_MARK,
};

/* - DU --------------------------------------------------------------------- */
static const unsigned int du_rgb666_pins[] = {
	/* R[7:2], G[7:2], B[7:2] */
	RCAR_GP_PIN(0, 5), RCAR_GP_PIN(0, 4), RCAR_GP_PIN(0, 3),
	RCAR_GP_PIN(0, 2), RCAR_GP_PIN(0, 1), RCAR_GP_PIN(0, 0),
	RCAR_GP_PIN(0, 11), RCAR_GP_PIN(0, 10), RCAR_GP_PIN(0, 9),
	RCAR_GP_PIN(0, 8), RCAR_GP_PIN(0, 7), RCAR_GP_PIN(0, 6),
	RCAR_GP_PIN(0, 17), RCAR_GP_PIN(0, 16), RCAR_GP_PIN(0, 15),
	RCAR_GP_PIN(0, 14), RCAR_GP_PIN(0, 13), RCAR_GP_PIN(0, 12),
};
static const unsigned int du_rgb666_mux[] = {
	DU_DR7_MARK, DU_DR6_MARK, DU_DR5_MARK,
	DU_DR4_MARK, DU_DR3_MARK, DU_DR2_MARK,
	DU_DG7_MARK, DU_DG6_MARK, DU_DG5_MARK,
	DU_DG4_MARK, DU_DG3_MARK, DU_DG2_MARK,
	DU_DB7_MARK, DU_DB6_MARK, DU_DB5_MARK,
	DU_DB4_MARK, DU_DB3_MARK, DU_DB2_MARK,
};
static const unsigned int du_clk_out_pins[] = {
	/* DOTCLKOUT */
	RCAR_GP_PIN(0, 18),
};
static const unsigned int du_clk_out_mux[] = {
	DU_DOTCLKOUT_MARK,
};
static const unsigned int du_sync_pins[] = {
	/* EXVSYNC/VSYNC, EXHSYNC/HSYNC */
	RCAR_GP_PIN(0, 20), RCAR_GP_PIN(0, 19),
};
static const unsigned int du_sync_mux[] = {
	DU_EXVSYNC_DU_VSYNC_MARK, DU_EXHSYNC_DU_HSYNC_MARK
};
static const unsigned int du_oddf_pins[] = {
	/* EXODDF/ODDF/DISP/CDE */
	RCAR_GP_PIN(0, 21),
};
static const unsigned int du_oddf_mux[] = {
	DU_EXODDF_DU_ODDF_DISP_CDE_MARK,
};
static const unsigned int du_cde_pins[] = {
	/* CDE */
	RCAR_GP_PIN(1, 22),
};
static const unsigned int du_cde_mux[] = {
	DU_CDE_MARK,
};
static const unsigned int du_disp_pins[] = {
	/* DISP */
	RCAR_GP_PIN(1, 21),
};
static const unsigned int du_disp_mux[] = {
	DU_DISP_MARK,
};

/* - HSCIF0 ----------------------------------------------------------------- */
static const unsigned int hscif0_data_pins[] = {
	/* HRX, HTX */
	RCAR_GP_PIN(0, 19), RCAR_GP_PIN(0, 3),
};
static const unsigned int hscif0_data_mux[] = {
	HRX0_MARK, HTX0_MARK,
};
static const unsigned int hscif0_clk_pins[] = {
	/* HSCK */
	RCAR_GP_PIN(0, 0),
};
static const unsigned int hscif0_clk_mux[] = {
	HSCK0_MARK,
};
static const unsigned int hscif0_ctrl_pins[] = {
	/* HRTS#, HCTS# */
	RCAR_GP_PIN(0, 1), RCAR_GP_PIN(0, 2),
};
static const unsigned int hscif0_ctrl_mux[] = {
	HRTS0_N_MARK, HCTS0_N_MARK,
};

/* - HSCIF1 ----------------------------------------------------------------- */
static const unsigned int hscif1_data_pins[] = {
	/* HRX, HTX */
	RCAR_GP_PIN(2, 11), RCAR_GP_PIN(2, 10),
};
static const unsigned int hscif1_data_mux[] = {
	HRX1_MARK, HTX1_MARK,
};
static const unsigned int hscif1_clk_pins[] = {
	/* HSCK */
	RCAR_GP_PIN(2, 7),
};
static const unsigned int hscif1_clk_mux[] = {
	HSCK1_MARK,
};
static const unsigned int hscif1_ctrl_pins[] = {
	/* HRTS#, HCTS# */
	RCAR_GP_PIN(2, 8), RCAR_GP_PIN(2, 9),
};
static const unsigned int hscif1_ctrl_mux[] = {
	HRTS1_N_MARK, HCTS1_N_MARK,
};

/* - HSCIF2 ----------------------------------------------------------------- */
static const unsigned int hscif2_data_pins[] = {
	/* HRX, HTX */
	RCAR_GP_PIN(2, 16), RCAR_GP_PIN(2, 15),
};
static const unsigned int hscif2_data_mux[] = {
	HRX2_MARK, HTX2_MARK,
};
static const unsigned int hscif2_clk_pins[] = {
	/* HSCK */
	RCAR_GP_PIN(2, 12),
};
static const unsigned int hscif2_clk_mux[] = {
	HSCK2_MARK,
};
static const unsigned int hscif2_ctrl_pins[] = {
	/* HRTS#, HCTS# */
	RCAR_GP_PIN(2, 14), RCAR_GP_PIN(2, 13),
};
static const unsigned int hscif2_ctrl_mux[] = {
	HRTS2_N_MARK, HCTS2_N_MARK,
};

/* - HSCIF3 ----------------------------------------------------------------- */
static const unsigned int hscif3_data_pins[] = {
	/* HRX, HTX */
	RCAR_GP_PIN(2, 4), RCAR_GP_PIN(2, 3),
};
static const unsigned int hscif3_data_mux[] = {
	HRX3_MARK, HTX3_MARK,
};
static const unsigned int hscif3_clk_pins[] = {
	/* HSCK */
	RCAR_GP_PIN(2, 0),
};
static const unsigned int hscif3_clk_mux[] = {
	HSCK3_MARK,
};
static const unsigned int hscif3_ctrl_pins[] = {
	/* HRTS#, HCTS# */
	RCAR_GP_PIN(2, 2), RCAR_GP_PIN(2, 1),
};
static const unsigned int hscif3_ctrl_mux[] = {
	HRTS3_N_MARK, HCTS3_N_MARK,
};

/* - I2C0 ------------------------------------------------------------------- */
static const unsigned int i2c0_pins[] = {
	/* SDA, SCL */
	RCAR_GP_PIN(4, 1), RCAR_GP_PIN(4, 0),
};
static const unsigned int i2c0_mux[] = {
	SDA0_MARK, SCL0_MARK,
};

/* - I2C1 ------------------------------------------------------------------- */
static const unsigned int i2c1_pins[] = {
	/* SDA, SCL */
	RCAR_GP_PIN(4, 3), RCAR_GP_PIN(4, 2),
};
static const unsigned int i2c1_mux[] = {
	SDA1_MARK, SCL1_MARK,
};

/* - I2C2 ------------------------------------------------------------------- */
static const unsigned int i2c2_pins[] = {
	/* SDA, SCL */
	RCAR_GP_PIN(4, 5), RCAR_GP_PIN(4, 4),
};
static const unsigned int i2c2_mux[] = {
	SDA2_MARK, SCL2_MARK,
};

/* - I2C3 ------------------------------------------------------------------- */
static const unsigned int i2c3_a_pins[] = {
	/* SDA, SCL */
	RCAR_GP_PIN(2, 6), RCAR_GP_PIN(2, 7),
};
static const unsigned int i2c3_a_mux[] = {
	SDA3_A_MARK, SCL3_A_MARK,
};
static const unsigned int i2c3_b_pins[] = {
	/* SDA, SCL */
	RCAR_GP_PIN(3, 14), RCAR_GP_PIN(3, 13),
};
static const unsigned int i2c3_b_mux[] = {
	SDA3_B_MARK, SCL3_B_MARK,
};

/* - I2C4 ------------------------------------------------------------------- */
static const unsigned int i2c4_pins[] = {
	/* SDA, SCL */
	RCAR_GP_PIN(3, 16), RCAR_GP_PIN(3, 15),
};
static const unsigned int i2c4_mux[] = {
	SDA4_MARK, SCL4_MARK,
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
	RCAR_GP_PIN(0, 11),
};
static const unsigned int intc_ex_irq1_mux[] = {
	IRQ1_MARK,
};
static const unsigned int intc_ex_irq2_pins[] = {
	/* IRQ2 */
	RCAR_GP_PIN(0, 12),
};
static const unsigned int intc_ex_irq2_mux[] = {
	IRQ2_MARK,
};
static const unsigned int intc_ex_irq3_pins[] = {
	/* IRQ3 */
	RCAR_GP_PIN(0, 19),
};
static const unsigned int intc_ex_irq3_mux[] = {
	IRQ3_MARK,
};
static const unsigned int intc_ex_irq4_pins[] = {
	/* IRQ4 */
	RCAR_GP_PIN(3, 15),
};
static const unsigned int intc_ex_irq4_mux[] = {
	IRQ4_MARK,
};
static const unsigned int intc_ex_irq5_pins[] = {
	/* IRQ5 */
	RCAR_GP_PIN(3, 16),
};
static const unsigned int intc_ex_irq5_mux[] = {
	IRQ5_MARK,
};

/* - MMC -------------------------------------------------------------------- */
static const unsigned int mmc_data1_pins[] = {
	/* D0 */
	RCAR_GP_PIN(3, 6),
};
static const unsigned int mmc_data1_mux[] = {
	MMC_D0_MARK,
};
static const unsigned int mmc_data4_pins[] = {
	/* D[0:3] */
	RCAR_GP_PIN(3, 6), RCAR_GP_PIN(3, 7),
	RCAR_GP_PIN(3, 8), RCAR_GP_PIN(3, 9),
};
static const unsigned int mmc_data4_mux[] = {
	MMC_D0_MARK, MMC_D1_MARK,
	MMC_D2_MARK, MMC_D3_MARK,
};
static const unsigned int mmc_data8_pins[] = {
	/* D[0:7] */
	RCAR_GP_PIN(3, 6), RCAR_GP_PIN(3, 7),
	RCAR_GP_PIN(3, 8), RCAR_GP_PIN(3, 9),
	RCAR_GP_PIN(3, 11), RCAR_GP_PIN(3, 12),
	RCAR_GP_PIN(3, 13), RCAR_GP_PIN(3, 14),
};
static const unsigned int mmc_data8_mux[] = {
	MMC_D0_MARK, MMC_D1_MARK,
	MMC_D2_MARK, MMC_D3_MARK,
	MMC_D4_MARK, MMC_D5_MARK,
	MMC_D6_MARK, MMC_D7_MARK,
};
static const unsigned int mmc_ctrl_pins[] = {
	/* CLK, CMD */
	RCAR_GP_PIN(3, 10), RCAR_GP_PIN(3, 5),
};
static const unsigned int mmc_ctrl_mux[] = {
	MMC_CLK_MARK, MMC_CMD_MARK,
};
static const unsigned int mmc_cd_pins[] = {
	/* CD */
	RCAR_GP_PIN(3, 16),
};
static const unsigned int mmc_cd_mux[] = {
	MMC_CD_MARK,
};
static const unsigned int mmc_wp_pins[] = {
	/* WP */
	RCAR_GP_PIN(3, 15),
};
static const unsigned int mmc_wp_mux[] = {
	MMC_WP_MARK,
};

/* - MSIOF0 ----------------------------------------------------------------- */
static const unsigned int msiof0_clk_pins[] = {
	/* SCK */
	RCAR_GP_PIN(4, 2),
};
static const unsigned int msiof0_clk_mux[] = {
	MSIOF0_SCK_MARK,
};
static const unsigned int msiof0_sync_pins[] = {
	/* SYNC */
	RCAR_GP_PIN(4, 3),
};
static const unsigned int msiof0_sync_mux[] = {
	MSIOF0_SYNC_MARK,
};
static const unsigned int msiof0_ss1_pins[] = {
	/* SS1 */
	RCAR_GP_PIN(4, 4),
};
static const unsigned int msiof0_ss1_mux[] = {
	MSIOF0_SS1_MARK,
};
static const unsigned int msiof0_ss2_pins[] = {
	/* SS2 */
	RCAR_GP_PIN(4, 5),
};
static const unsigned int msiof0_ss2_mux[] = {
	MSIOF0_SS2_MARK,
};
static const unsigned int msiof0_txd_pins[] = {
	/* TXD */
	RCAR_GP_PIN(4, 1),
};
static const unsigned int msiof0_txd_mux[] = {
	MSIOF0_TXD_MARK,
};
static const unsigned int msiof0_rxd_pins[] = {
	/* RXD */
	RCAR_GP_PIN(4, 0),
};
static const unsigned int msiof0_rxd_mux[] = {
	MSIOF0_RXD_MARK,
};

/* - MSIOF1 ----------------------------------------------------------------- */
static const unsigned int msiof1_clk_pins[] = {
	/* SCK */
	RCAR_GP_PIN(3, 2),
};
static const unsigned int msiof1_clk_mux[] = {
	MSIOF1_SCK_MARK,
};
static const unsigned int msiof1_sync_pins[] = {
	/* SYNC */
	RCAR_GP_PIN(3, 3),
};
static const unsigned int msiof1_sync_mux[] = {
	MSIOF1_SYNC_MARK,
};
static const unsigned int msiof1_ss1_pins[] = {
	/* SS1 */
	RCAR_GP_PIN(3, 4),
};
static const unsigned int msiof1_ss1_mux[] = {
	MSIOF1_SS1_MARK,
};
static const unsigned int msiof1_ss2_pins[] = {
	/* SS2 */
	RCAR_GP_PIN(3, 5),
};
static const unsigned int msiof1_ss2_mux[] = {
	MSIOF1_SS2_MARK,
};
static const unsigned int msiof1_txd_pins[] = {
	/* TXD */
	RCAR_GP_PIN(3, 1),
};
static const unsigned int msiof1_txd_mux[] = {
	MSIOF1_TXD_MARK,
};
static const unsigned int msiof1_rxd_pins[] = {
	/* RXD */
	RCAR_GP_PIN(3, 0),
};
static const unsigned int msiof1_rxd_mux[] = {
	MSIOF1_RXD_MARK,
};

/* - MSIOF2 ----------------------------------------------------------------- */
static const unsigned int msiof2_clk_pins[] = {
	/* SCK */
	RCAR_GP_PIN(2, 0),
};
static const unsigned int msiof2_clk_mux[] = {
	MSIOF2_SCK_MARK,
};
static const unsigned int msiof2_sync_pins[] = {
	/* SYNC */
	RCAR_GP_PIN(2, 3),
};
static const unsigned int msiof2_sync_mux[] = {
	MSIOF2_SYNC_MARK,
};
static const unsigned int msiof2_ss1_pins[] = {
	/* SS1 */
	RCAR_GP_PIN(2, 4),
};
static const unsigned int msiof2_ss1_mux[] = {
	MSIOF2_SS1_MARK,
};
static const unsigned int msiof2_ss2_pins[] = {
	/* SS2 */
	RCAR_GP_PIN(2, 5),
};
static const unsigned int msiof2_ss2_mux[] = {
	MSIOF2_SS2_MARK,
};
static const unsigned int msiof2_txd_pins[] = {
	/* TXD */
	RCAR_GP_PIN(2, 2),
};
static const unsigned int msiof2_txd_mux[] = {
	MSIOF2_TXD_MARK,
};
static const unsigned int msiof2_rxd_pins[] = {
	/* RXD */
	RCAR_GP_PIN(2, 1),
};
static const unsigned int msiof2_rxd_mux[] = {
	MSIOF2_RXD_MARK,
};

/* - MSIOF3 ----------------------------------------------------------------- */
static const unsigned int msiof3_clk_pins[] = {
	/* SCK */
	RCAR_GP_PIN(0, 20),
};
static const unsigned int msiof3_clk_mux[] = {
	MSIOF3_SCK_MARK,
};
static const unsigned int msiof3_sync_pins[] = {
	/* SYNC */
	RCAR_GP_PIN(0, 21),
};
static const unsigned int msiof3_sync_mux[] = {
	MSIOF3_SYNC_MARK,
};
static const unsigned int msiof3_ss1_pins[] = {
	/* SS1 */
	RCAR_GP_PIN(0, 6),
};
static const unsigned int msiof3_ss1_mux[] = {
	MSIOF3_SS1_MARK,
};
static const unsigned int msiof3_ss2_pins[] = {
	/* SS2 */
	RCAR_GP_PIN(0, 7),
};
static const unsigned int msiof3_ss2_mux[] = {
	MSIOF3_SS2_MARK,
};
static const unsigned int msiof3_txd_pins[] = {
	/* TXD */
	RCAR_GP_PIN(0, 5),
};
static const unsigned int msiof3_txd_mux[] = {
	MSIOF3_TXD_MARK,
};
static const unsigned int msiof3_rxd_pins[] = {
	/* RXD */
	RCAR_GP_PIN(0, 4),
};
static const unsigned int msiof3_rxd_mux[] = {
	MSIOF3_RXD_MARK,
};

/* - PWM0 ------------------------------------------------------------------- */
static const unsigned int pwm0_a_pins[] = {
	RCAR_GP_PIN(2, 12),
};
static const unsigned int pwm0_a_mux[] = {
	PWM0_A_MARK,
};
static const unsigned int pwm0_b_pins[] = {
	RCAR_GP_PIN(1, 21),
};
static const unsigned int pwm0_b_mux[] = {
	PWM0_B_MARK,
};

/* - PWM1 ------------------------------------------------------------------- */
static const unsigned int pwm1_a_pins[] = {
	RCAR_GP_PIN(2, 13),
};
static const unsigned int pwm1_a_mux[] = {
	PWM1_A_MARK,
};
static const unsigned int pwm1_b_pins[] = {
	RCAR_GP_PIN(1, 22),
};
static const unsigned int pwm1_b_mux[] = {
	PWM1_B_MARK,
};

/* - PWM2 ------------------------------------------------------------------- */
static const unsigned int pwm2_a_pins[] = {
	RCAR_GP_PIN(2, 14),
};
static const unsigned int pwm2_a_mux[] = {
	PWM2_A_MARK,
};
static const unsigned int pwm2_b_pins[] = {
	RCAR_GP_PIN(1, 23),
};
static const unsigned int pwm2_b_mux[] = {
	PWM2_B_MARK,
};

/* - PWM3 ------------------------------------------------------------------- */
static const unsigned int pwm3_a_pins[] = {
	RCAR_GP_PIN(2, 15),
};
static const unsigned int pwm3_a_mux[] = {
	PWM3_A_MARK,
};
static const unsigned int pwm3_b_pins[] = {
	RCAR_GP_PIN(1, 24),
};
static const unsigned int pwm3_b_mux[] = {
	PWM3_B_MARK,
};

/* - PWM4 ------------------------------------------------------------------- */
static const unsigned int pwm4_a_pins[] = {
	RCAR_GP_PIN(2, 16),
};
static const unsigned int pwm4_a_mux[] = {
	PWM4_A_MARK,
};
static const unsigned int pwm4_b_pins[] = {
	RCAR_GP_PIN(1, 25),
};
static const unsigned int pwm4_b_mux[] = {
	PWM4_B_MARK,
};

/* - QSPI0 ------------------------------------------------------------------ */
static const unsigned int qspi0_ctrl_pins[] = {
	/* SPCLK, SSL */
	RCAR_GP_PIN(5, 0), RCAR_GP_PIN(5, 5),
};
static const unsigned int qspi0_ctrl_mux[] = {
	QSPI0_SPCLK_MARK, QSPI0_SSL_MARK,
};
static const unsigned int qspi0_data2_pins[] = {
	/* MOSI_IO0, MISO_IO1 */
	RCAR_GP_PIN(5, 1), RCAR_GP_PIN(5, 2),
};
static const unsigned int qspi0_data2_mux[] = {
	QSPI0_MOSI_IO0_MARK, QSPI0_MISO_IO1_MARK,
};
static const unsigned int qspi0_data4_pins[] = {
	/* MOSI_IO0, MISO_IO1, IO2, IO3 */
	RCAR_GP_PIN(5, 1), RCAR_GP_PIN(5, 2),
	RCAR_GP_PIN(5, 3), RCAR_GP_PIN(5, 4),
};
static const unsigned int qspi0_data4_mux[] = {
	QSPI0_MOSI_IO0_MARK, QSPI0_MISO_IO1_MARK,
	QSPI0_IO2_MARK, QSPI0_IO3_MARK
};

/* - QSPI1 ------------------------------------------------------------------ */
static const unsigned int qspi1_ctrl_pins[] = {
	/* SPCLK, SSL */
	RCAR_GP_PIN(5, 6), RCAR_GP_PIN(5, 11),
};
static const unsigned int qspi1_ctrl_mux[] = {
	QSPI1_SPCLK_MARK, QSPI1_SSL_MARK,
};
static const unsigned int qspi1_data2_pins[] = {
	/* MOSI_IO0, MISO_IO1 */
	RCAR_GP_PIN(5, 7), RCAR_GP_PIN(5, 8),
};
static const unsigned int qspi1_data2_mux[] = {
	QSPI1_MOSI_IO0_MARK, QSPI1_MISO_IO1_MARK,
};
static const unsigned int qspi1_data4_pins[] = {
	/* MOSI_IO0, MISO_IO1, IO2, IO3 */
	RCAR_GP_PIN(5, 7), RCAR_GP_PIN(5, 8),
	RCAR_GP_PIN(5, 9), RCAR_GP_PIN(5, 10),
};
static const unsigned int qspi1_data4_mux[] = {
	QSPI1_MOSI_IO0_MARK, QSPI1_MISO_IO1_MARK,
	QSPI1_IO2_MARK, QSPI1_IO3_MARK
};

/* - SCIF Clock ------------------------------------------------------------- */
static const unsigned int scif_clk_a_pins[] = {
	/* SCIF_CLK */
	RCAR_GP_PIN(0, 18),
};
static const unsigned int scif_clk_a_mux[] = {
	SCIF_CLK_A_MARK,
};
static const unsigned int scif_clk_b_pins[] = {
	/* SCIF_CLK */
	RCAR_GP_PIN(1, 25),
};
static const unsigned int scif_clk_b_mux[] = {
	SCIF_CLK_B_MARK,
};

/* - SCIF0 ------------------------------------------------------------------ */
static const unsigned int scif0_data_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(4, 4), RCAR_GP_PIN(4, 5),
};
static const unsigned int scif0_data_mux[] = {
	RX0_MARK, TX0_MARK,
};
static const unsigned int scif0_clk_pins[] = {
	/* SCK */
	RCAR_GP_PIN(4, 1),
};
static const unsigned int scif0_clk_mux[] = {
	SCK0_MARK,
};
static const unsigned int scif0_ctrl_pins[] = {
	/* RTS#, CTS# */
	RCAR_GP_PIN(4, 3), RCAR_GP_PIN(4, 2),
};
static const unsigned int scif0_ctrl_mux[] = {
	RTS0_N_MARK, CTS0_N_MARK,
};

/* - SCIF1 ------------------------------------------------------------------ */
static const unsigned int scif1_data_a_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(2, 8), RCAR_GP_PIN(2, 9),
};
static const unsigned int scif1_data_a_mux[] = {
	RX1_A_MARK, TX1_A_MARK,
};
static const unsigned int scif1_clk_pins[] = {
	/* SCK */
	RCAR_GP_PIN(2, 5),
};
static const unsigned int scif1_clk_mux[] = {
	SCK1_MARK,
};
static const unsigned int scif1_ctrl_pins[] = {
	/* RTS#, CTS# */
	RCAR_GP_PIN(2, 11), RCAR_GP_PIN(2, 10),
};
static const unsigned int scif1_ctrl_mux[] = {
	RTS1_N_MARK, CTS1_N_MARK,
};
static const unsigned int scif1_data_b_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(1, 24), RCAR_GP_PIN(1, 23),
};
static const unsigned int scif1_data_b_mux[] = {
	RX1_B_MARK, TX1_B_MARK,
};

/* - SCIF3 ------------------------------------------------------------------ */
static const unsigned int scif3_data_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(2, 1), RCAR_GP_PIN(2, 2),
};
static const unsigned int scif3_data_mux[] = {
	RX3_MARK, TX3_MARK,
};
static const unsigned int scif3_clk_pins[] = {
	/* SCK */
	RCAR_GP_PIN(2, 0),
};
static const unsigned int scif3_clk_mux[] = {
	SCK3_MARK,
};
static const unsigned int scif3_ctrl_pins[] = {
	/* RTS#, CTS# */
	RCAR_GP_PIN(2, 4), RCAR_GP_PIN(2, 3),
};
static const unsigned int scif3_ctrl_mux[] = {
	RTS3_N_MARK, CTS3_N_MARK,
};

/* - SCIF4 ------------------------------------------------------------------ */
static const unsigned int scif4_data_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(3, 10), RCAR_GP_PIN(3, 11),
};
static const unsigned int scif4_data_mux[] = {
	RX4_MARK, TX4_MARK,
};
static const unsigned int scif4_clk_pins[] = {
	/* SCK */
	RCAR_GP_PIN(3, 9),
};
static const unsigned int scif4_clk_mux[] = {
	SCK4_MARK,
};
static const unsigned int scif4_ctrl_pins[] = {
	/* RTS#, CTS# */
	RCAR_GP_PIN(3, 13), RCAR_GP_PIN(3, 12),
};
static const unsigned int scif4_ctrl_mux[] = {
	RTS4_N_MARK, CTS4_N_MARK,
};

/* - TMU -------------------------------------------------------------------- */
static const unsigned int tmu_tclk1_a_pins[] = {
	/* TCLK1 */
	RCAR_GP_PIN(4, 4),
};
static const unsigned int tmu_tclk1_a_mux[] = {
	TCLK1_A_MARK,
};
static const unsigned int tmu_tclk1_b_pins[] = {
	/* TCLK1 */
	RCAR_GP_PIN(1, 23),
};
static const unsigned int tmu_tclk1_b_mux[] = {
	TCLK1_B_MARK,
};
static const unsigned int tmu_tclk2_a_pins[] = {
	/* TCLK2 */
	RCAR_GP_PIN(4, 5),
};
static const unsigned int tmu_tclk2_a_mux[] = {
	TCLK2_A_MARK,
};
static const unsigned int tmu_tclk2_b_pins[] = {
	/* TCLK2 */
	RCAR_GP_PIN(1, 24),
};
static const unsigned int tmu_tclk2_b_mux[] = {
	TCLK2_B_MARK,
};

/* - VIN0 ------------------------------------------------------------------- */
static const union vin_data12 vin0_data_pins = {
	.data12 = {
		RCAR_GP_PIN(2, 4), RCAR_GP_PIN(2, 5),
		RCAR_GP_PIN(2, 6), RCAR_GP_PIN(2, 7),
		RCAR_GP_PIN(2, 8), RCAR_GP_PIN(2, 9),
		RCAR_GP_PIN(2, 10), RCAR_GP_PIN(2, 11),
		RCAR_GP_PIN(2, 12), RCAR_GP_PIN(2, 13),
		RCAR_GP_PIN(2, 14), RCAR_GP_PIN(2, 15),
	},
};
static const union vin_data12 vin0_data_mux = {
	.data12 = {
		VI0_DATA0_MARK, VI0_DATA1_MARK,
		VI0_DATA2_MARK, VI0_DATA3_MARK,
		VI0_DATA4_MARK, VI0_DATA5_MARK,
		VI0_DATA6_MARK, VI0_DATA7_MARK,
		VI0_DATA8_MARK,  VI0_DATA9_MARK,
		VI0_DATA10_MARK, VI0_DATA11_MARK,
	},
};
static const unsigned int vin0_sync_pins[] = {
	/* HSYNC#, VSYNC# */
	RCAR_GP_PIN(2, 2), RCAR_GP_PIN(2, 3),
};
static const unsigned int vin0_sync_mux[] = {
	VI0_HSYNC_N_MARK, VI0_VSYNC_N_MARK,
};
static const unsigned int vin0_field_pins[] = {
	/* FIELD */
	RCAR_GP_PIN(2, 16),
};
static const unsigned int vin0_field_mux[] = {
	VI0_FIELD_MARK,
};
static const unsigned int vin0_clkenb_pins[] = {
	/* CLKENB */
	RCAR_GP_PIN(2, 1),
};
static const unsigned int vin0_clkenb_mux[] = {
	VI0_CLKENB_MARK,
};
static const unsigned int vin0_clk_pins[] = {
	/* CLK */
	RCAR_GP_PIN(2, 0),
};
static const unsigned int vin0_clk_mux[] = {
	VI0_CLK_MARK,
};

/* - VIN1 ------------------------------------------------------------------- */
static const union vin_data12 vin1_data_pins = {
	.data12 = {
		RCAR_GP_PIN(3, 4), RCAR_GP_PIN(3, 5),
		RCAR_GP_PIN(3, 6), RCAR_GP_PIN(3, 7),
		RCAR_GP_PIN(3, 8), RCAR_GP_PIN(3, 9),
		RCAR_GP_PIN(3, 10), RCAR_GP_PIN(3, 11),
		RCAR_GP_PIN(3, 12), RCAR_GP_PIN(3, 13),
		RCAR_GP_PIN(3, 14), RCAR_GP_PIN(3, 15),
	},
};
static const union vin_data12 vin1_data_mux = {
	.data12 = {
		VI1_DATA0_MARK, VI1_DATA1_MARK,
		VI1_DATA2_MARK, VI1_DATA3_MARK,
		VI1_DATA4_MARK, VI1_DATA5_MARK,
		VI1_DATA6_MARK, VI1_DATA7_MARK,
		VI1_DATA8_MARK,  VI1_DATA9_MARK,
		VI1_DATA10_MARK, VI1_DATA11_MARK,
	},
};
static const unsigned int vin1_sync_pins[] = {
	/* HSYNC#, VSYNC# */
	RCAR_GP_PIN(3, 2), RCAR_GP_PIN(3, 3),
};
static const unsigned int vin1_sync_mux[] = {
	VI1_HSYNC_N_MARK, VI1_VSYNC_N_MARK,
};
static const unsigned int vin1_field_pins[] = {
	RCAR_GP_PIN(3, 16),
};
static const unsigned int vin1_field_mux[] = {
	/* FIELD */
	VI1_FIELD_MARK,
};
static const unsigned int vin1_clkenb_pins[] = {
	RCAR_GP_PIN(3, 1),
};
static const unsigned int vin1_clkenb_mux[] = {
	/* CLKENB */
	VI1_CLKENB_MARK,
};
static const unsigned int vin1_clk_pins[] = {
	RCAR_GP_PIN(3, 0),
};
static const unsigned int vin1_clk_mux[] = {
	/* CLK */
	VI1_CLK_MARK,
};

static const struct sh_pfc_pin_group pinmux_groups[] = {
	SH_PFC_PIN_GROUP(avb0_link),
	SH_PFC_PIN_GROUP(avb0_magic),
	SH_PFC_PIN_GROUP(avb0_phy_int),
	SH_PFC_PIN_GROUP(avb0_mdio),
	SH_PFC_PIN_GROUP(avb0_rgmii),
	SH_PFC_PIN_GROUP(avb0_txcrefclk),
	SH_PFC_PIN_GROUP(avb0_avtp_pps),
	SH_PFC_PIN_GROUP(avb0_avtp_capture),
	SH_PFC_PIN_GROUP(avb0_avtp_match),
	SH_PFC_PIN_GROUP(canfd_clk_a),
	SH_PFC_PIN_GROUP(canfd_clk_b),
	SH_PFC_PIN_GROUP(canfd0_data_a),
	SH_PFC_PIN_GROUP(canfd0_data_b),
	SH_PFC_PIN_GROUP(canfd1_data),
	SH_PFC_PIN_GROUP(du_rgb666),
	SH_PFC_PIN_GROUP(du_clk_out),
	SH_PFC_PIN_GROUP(du_sync),
	SH_PFC_PIN_GROUP(du_oddf),
	SH_PFC_PIN_GROUP(du_cde),
	SH_PFC_PIN_GROUP(du_disp),
	SH_PFC_PIN_GROUP(hscif0_data),
	SH_PFC_PIN_GROUP(hscif0_clk),
	SH_PFC_PIN_GROUP(hscif0_ctrl),
	SH_PFC_PIN_GROUP(hscif1_data),
	SH_PFC_PIN_GROUP(hscif1_clk),
	SH_PFC_PIN_GROUP(hscif1_ctrl),
	SH_PFC_PIN_GROUP(hscif2_data),
	SH_PFC_PIN_GROUP(hscif2_clk),
	SH_PFC_PIN_GROUP(hscif2_ctrl),
	SH_PFC_PIN_GROUP(hscif3_data),
	SH_PFC_PIN_GROUP(hscif3_clk),
	SH_PFC_PIN_GROUP(hscif3_ctrl),
	SH_PFC_PIN_GROUP(i2c0),
	SH_PFC_PIN_GROUP(i2c1),
	SH_PFC_PIN_GROUP(i2c2),
	SH_PFC_PIN_GROUP(i2c3_a),
	SH_PFC_PIN_GROUP(i2c3_b),
	SH_PFC_PIN_GROUP(i2c4),
	SH_PFC_PIN_GROUP(intc_ex_irq0),
	SH_PFC_PIN_GROUP(intc_ex_irq1),
	SH_PFC_PIN_GROUP(intc_ex_irq2),
	SH_PFC_PIN_GROUP(intc_ex_irq3),
	SH_PFC_PIN_GROUP(intc_ex_irq4),
	SH_PFC_PIN_GROUP(intc_ex_irq5),
	SH_PFC_PIN_GROUP(mmc_data1),
	SH_PFC_PIN_GROUP(mmc_data4),
	SH_PFC_PIN_GROUP(mmc_data8),
	SH_PFC_PIN_GROUP(mmc_ctrl),
	SH_PFC_PIN_GROUP(mmc_cd),
	SH_PFC_PIN_GROUP(mmc_wp),
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
	SH_PFC_PIN_GROUP(msiof2_clk),
	SH_PFC_PIN_GROUP(msiof2_sync),
	SH_PFC_PIN_GROUP(msiof2_ss1),
	SH_PFC_PIN_GROUP(msiof2_ss2),
	SH_PFC_PIN_GROUP(msiof2_txd),
	SH_PFC_PIN_GROUP(msiof2_rxd),
	SH_PFC_PIN_GROUP(msiof3_clk),
	SH_PFC_PIN_GROUP(msiof3_sync),
	SH_PFC_PIN_GROUP(msiof3_ss1),
	SH_PFC_PIN_GROUP(msiof3_ss2),
	SH_PFC_PIN_GROUP(msiof3_txd),
	SH_PFC_PIN_GROUP(msiof3_rxd),
	SH_PFC_PIN_GROUP(pwm0_a),
	SH_PFC_PIN_GROUP(pwm0_b),
	SH_PFC_PIN_GROUP(pwm1_a),
	SH_PFC_PIN_GROUP(pwm1_b),
	SH_PFC_PIN_GROUP(pwm2_a),
	SH_PFC_PIN_GROUP(pwm2_b),
	SH_PFC_PIN_GROUP(pwm3_a),
	SH_PFC_PIN_GROUP(pwm3_b),
	SH_PFC_PIN_GROUP(pwm4_a),
	SH_PFC_PIN_GROUP(pwm4_b),
	SH_PFC_PIN_GROUP(qspi0_ctrl),
	SH_PFC_PIN_GROUP(qspi0_data2),
	SH_PFC_PIN_GROUP(qspi0_data4),
	SH_PFC_PIN_GROUP(qspi1_ctrl),
	SH_PFC_PIN_GROUP(qspi1_data2),
	SH_PFC_PIN_GROUP(qspi1_data4),
	SH_PFC_PIN_GROUP(scif_clk_a),
	SH_PFC_PIN_GROUP(scif_clk_b),
	SH_PFC_PIN_GROUP(scif0_data),
	SH_PFC_PIN_GROUP(scif0_clk),
	SH_PFC_PIN_GROUP(scif0_ctrl),
	SH_PFC_PIN_GROUP(scif1_data_a),
	SH_PFC_PIN_GROUP(scif1_clk),
	SH_PFC_PIN_GROUP(scif1_ctrl),
	SH_PFC_PIN_GROUP(scif1_data_b),
	SH_PFC_PIN_GROUP(scif3_data),
	SH_PFC_PIN_GROUP(scif3_clk),
	SH_PFC_PIN_GROUP(scif3_ctrl),
	SH_PFC_PIN_GROUP(scif4_data),
	SH_PFC_PIN_GROUP(scif4_clk),
	SH_PFC_PIN_GROUP(scif4_ctrl),
	SH_PFC_PIN_GROUP(tmu_tclk1_a),
	SH_PFC_PIN_GROUP(tmu_tclk1_b),
	SH_PFC_PIN_GROUP(tmu_tclk2_a),
	SH_PFC_PIN_GROUP(tmu_tclk2_b),
	VIN_DATA_PIN_GROUP(vin0_data, 8),
	VIN_DATA_PIN_GROUP(vin0_data, 10),
	VIN_DATA_PIN_GROUP(vin0_data, 12),
	SH_PFC_PIN_GROUP(vin0_sync),
	SH_PFC_PIN_GROUP(vin0_field),
	SH_PFC_PIN_GROUP(vin0_clkenb),
	SH_PFC_PIN_GROUP(vin0_clk),
	VIN_DATA_PIN_GROUP(vin1_data, 8),
	VIN_DATA_PIN_GROUP(vin1_data, 10),
	VIN_DATA_PIN_GROUP(vin1_data, 12),
	SH_PFC_PIN_GROUP(vin1_sync),
	SH_PFC_PIN_GROUP(vin1_field),
	SH_PFC_PIN_GROUP(vin1_clkenb),
	SH_PFC_PIN_GROUP(vin1_clk),
};

static const char * const avb0_groups[] = {
	"avb0_link",
	"avb0_magic",
	"avb0_phy_int",
	"avb0_mdio",
	"avb0_rgmii",
	"avb0_txcrefclk",
	"avb0_avtp_pps",
	"avb0_avtp_capture",
	"avb0_avtp_match",
};

static const char * const canfd_clk_groups[] = {
	"canfd_clk_a",
	"canfd_clk_b",
};

static const char * const canfd0_groups[] = {
	"canfd0_data_a",
	"canfd0_data_b",
};

static const char * const canfd1_groups[] = {
	"canfd1_data",
};

static const char * const du_groups[] = {
	"du_rgb666",
	"du_clk_out",
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
	"hscif1_data",
	"hscif1_clk",
	"hscif1_ctrl",
};

static const char * const hscif2_groups[] = {
	"hscif2_data",
	"hscif2_clk",
	"hscif2_ctrl",
};

static const char * const hscif3_groups[] = {
	"hscif3_data",
	"hscif3_clk",
	"hscif3_ctrl",
};

static const char * const i2c0_groups[] = {
	"i2c0",
};

static const char * const i2c1_groups[] = {
	"i2c1",
};

static const char * const i2c2_groups[] = {
	"i2c2",
};

static const char * const i2c3_groups[] = {
	"i2c3_a",
	"i2c3_b",
};

static const char * const i2c4_groups[] = {
	"i2c4",
};

static const char * const intc_ex_groups[] = {
	"intc_ex_irq0",
	"intc_ex_irq1",
	"intc_ex_irq2",
	"intc_ex_irq3",
	"intc_ex_irq4",
	"intc_ex_irq5",
};

static const char * const mmc_groups[] = {
	"mmc_data1",
	"mmc_data4",
	"mmc_data8",
	"mmc_ctrl",
	"mmc_cd",
	"mmc_wp",
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
	"msiof2_clk",
	"msiof2_sync",
	"msiof2_ss1",
	"msiof2_ss2",
	"msiof2_txd",
	"msiof2_rxd",
};

static const char * const msiof3_groups[] = {
	"msiof3_clk",
	"msiof3_sync",
	"msiof3_ss1",
	"msiof3_ss2",
	"msiof3_txd",
	"msiof3_rxd",
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
};

static const char * const pwm3_groups[] = {
	"pwm3_a",
	"pwm3_b",
};

static const char * const pwm4_groups[] = {
	"pwm4_a",
	"pwm4_b",
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

static const char * const scif_clk_groups[] = {
	"scif_clk_a",
	"scif_clk_b",
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

static const char * const scif3_groups[] = {
	"scif3_data",
	"scif3_clk",
	"scif3_ctrl",
};

static const char * const scif4_groups[] = {
	"scif4_data",
	"scif4_clk",
	"scif4_ctrl",
};

static const char * const tmu_groups[] = {
	"tmu_tclk1_a",
	"tmu_tclk1_b",
	"tmu_tclk2_a",
	"tmu_tclk2_b",
};

static const char * const vin0_groups[] = {
	"vin0_data8",
	"vin0_data10",
	"vin0_data12",
	"vin0_sync",
	"vin0_field",
	"vin0_clkenb",
	"vin0_clk",
};

static const char * const vin1_groups[] = {
	"vin1_data8",
	"vin1_data10",
	"vin1_data12",
	"vin1_sync",
	"vin1_field",
	"vin1_clkenb",
	"vin1_clk",
};

static const struct sh_pfc_function pinmux_functions[] = {
	SH_PFC_FUNCTION(avb0),
	SH_PFC_FUNCTION(canfd_clk),
	SH_PFC_FUNCTION(canfd0),
	SH_PFC_FUNCTION(canfd1),
	SH_PFC_FUNCTION(du),
	SH_PFC_FUNCTION(hscif0),
	SH_PFC_FUNCTION(hscif1),
	SH_PFC_FUNCTION(hscif2),
	SH_PFC_FUNCTION(hscif3),
	SH_PFC_FUNCTION(i2c0),
	SH_PFC_FUNCTION(i2c1),
	SH_PFC_FUNCTION(i2c2),
	SH_PFC_FUNCTION(i2c3),
	SH_PFC_FUNCTION(i2c4),
	SH_PFC_FUNCTION(intc_ex),
	SH_PFC_FUNCTION(mmc),
	SH_PFC_FUNCTION(msiof0),
	SH_PFC_FUNCTION(msiof1),
	SH_PFC_FUNCTION(msiof2),
	SH_PFC_FUNCTION(msiof3),
	SH_PFC_FUNCTION(pwm0),
	SH_PFC_FUNCTION(pwm1),
	SH_PFC_FUNCTION(pwm2),
	SH_PFC_FUNCTION(pwm3),
	SH_PFC_FUNCTION(pwm4),
	SH_PFC_FUNCTION(qspi0),
	SH_PFC_FUNCTION(qspi1),
	SH_PFC_FUNCTION(scif_clk),
	SH_PFC_FUNCTION(scif0),
	SH_PFC_FUNCTION(scif1),
	SH_PFC_FUNCTION(scif3),
	SH_PFC_FUNCTION(scif4),
	SH_PFC_FUNCTION(tmu),
	SH_PFC_FUNCTION(vin0),
	SH_PFC_FUNCTION(vin1),
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
		GP_0_21_FN,	GPSR0_21,
		GP_0_20_FN,	GPSR0_20,
		GP_0_19_FN,	GPSR0_19,
		GP_0_18_FN,	GPSR0_18,
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
		GP_3_16_FN,	GPSR3_16,
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
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
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
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
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
#undef F_
#undef FM

#define F_(x, y)	x,
#define FM(x)		FN_##x,
	{ PINMUX_CFG_REG_VAR("MOD_SEL0", 0xe6060500, 32,
			     GROUP(4, 4, 4, 4, 4, 1, 1, 1, 1, 1, 1, 1,
				   1, 1, 1, 1, 1),
			     GROUP(
		/* RESERVED 31, 30, 29, 28 */
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		/* RESERVED 27, 26, 25, 24 */
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		/* RESERVED 23, 22, 21, 20 */
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		/* RESERVED 19, 18, 17, 16 */
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		/* RESERVED 15, 14, 13, 12 */
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		MOD_SEL0_11
		MOD_SEL0_10
		MOD_SEL0_9
		MOD_SEL0_8
		MOD_SEL0_7
		MOD_SEL0_6
		MOD_SEL0_5
		MOD_SEL0_4
		MOD_SEL0_3
		MOD_SEL0_2
		MOD_SEL0_1
		MOD_SEL0_0 ))
	},
	{ },
};

enum ioctrl_regs {
	POCCTRL0,
	POCCTRL1,
	POCCTRL2,
	TDSELCTRL,
};

static const struct pinmux_ioctrl_reg pinmux_ioctrl_regs[] = {
	[POCCTRL0] = { 0xe6060380 },
	[POCCTRL1] = { 0xe6060384 },
	[POCCTRL2] = { 0xe6060388 },
	[TDSELCTRL] = { 0xe60603c0, },
	{ /* sentinel */ },
};

static int r8a77970_pin_to_pocctrl(struct sh_pfc *pfc, unsigned int pin,
				   u32 *pocctrl)
{
	int bit = pin & 0x1f;

	*pocctrl = pinmux_ioctrl_regs[POCCTRL0].reg;
	if (pin >= RCAR_GP_PIN(0, 0) && pin <= RCAR_GP_PIN(0, 21))
		return bit;
	if (pin >= RCAR_GP_PIN(2, 0) && pin <= RCAR_GP_PIN(2, 9))
		return bit + 22;

	*pocctrl = pinmux_ioctrl_regs[POCCTRL1].reg;
	if (pin >= RCAR_GP_PIN(2, 10) && pin <= RCAR_GP_PIN(2, 16))
		return bit - 10;
	if (pin >= RCAR_GP_PIN(3, 0) && pin <= RCAR_GP_PIN(3, 16))
		return bit + 7;

	return -EINVAL;
}

static const struct sh_pfc_soc_operations pinmux_ops = {
	.pin_to_pocctrl = r8a77970_pin_to_pocctrl,
};

const struct sh_pfc_soc_info r8a77970_pinmux_info = {
	.name = "r8a77970_pfc",
	.ops = &pinmux_ops,
	.unlock_reg = 0xe6060000, /* PMMR */

	.function = { PINMUX_FUNCTION_BEGIN, PINMUX_FUNCTION_END },

	.pins = pinmux_pins,
	.nr_pins = ARRAY_SIZE(pinmux_pins),
	.groups = pinmux_groups,
	.nr_groups = ARRAY_SIZE(pinmux_groups),
	.functions = pinmux_functions,
	.nr_functions = ARRAY_SIZE(pinmux_functions),

	.cfg_regs = pinmux_config_regs,
	.ioctrl_regs = pinmux_ioctrl_regs,

	.pinmux_data = pinmux_data,
	.pinmux_data_size = ARRAY_SIZE(pinmux_data),
};
