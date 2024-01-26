// SPDX-License-Identifier: GPL-2.0
/*
 * R8A779H0 processor support - PFC hardware block.
 *
 * Copyright (C) 2023 Renesas Electronics Corp.
 *
 * This file is based on the drivers/pinctrl/renesas/pfc-r8a779a0.c
 */

#include <linux/errno.h>
#include <linux/io.h>
#include <linux/kernel.h>

#include "sh_pfc.h"

#define CFG_FLAGS (SH_PFC_PIN_CFG_DRIVE_STRENGTH | SH_PFC_PIN_CFG_PULL_UP_DOWN)

#define CPU_ALL_GP(fn, sfx)								\
	PORT_GP_CFG_19(0,	fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_29(1,	fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(1, 29,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_16(2,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_1(2, 17,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_1(2, 19,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_13(3,	fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(3, 13,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_1(3, 14,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_1(3, 15,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_1(3, 16,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_1(3, 17,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_1(3, 18,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_1(3, 19,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_1(3, 20,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_1(3, 21,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_1(3, 22,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_1(3, 23,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_1(3, 24,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_1(3, 25,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_1(3, 26,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_1(3, 27,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_1(3, 28,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_1(3, 29,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_1(3, 30,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_1(3, 31,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_14(4,	fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(4, 14,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_1(4, 15,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_1(4, 21,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_1(4, 23,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_1(4, 24,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_21(5,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_21(6,	fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_21(7,	fn, sfx, CFG_FLAGS)

#define CPU_ALL_NOGP(fn)								\
	PIN_NOGP_CFG(VDDQ_AVB0, "VDDQ_AVB0", fn, SH_PFC_PIN_CFG_IO_VOLTAGE_18_25),	\
	PIN_NOGP_CFG(VDDQ_AVB1, "VDDQ_AVB1", fn, SH_PFC_PIN_CFG_IO_VOLTAGE_18_25),	\
	PIN_NOGP_CFG(VDDQ_AVB2, "VDDQ_AVB2", fn, SH_PFC_PIN_CFG_IO_VOLTAGE_18_25)

/*
 * F_() : just information
 * FM() : macro for FN_xxx / xxx_MARK
 */

/* GPSR0 */
#define GPSR0_18	F_(MSIOF2_RXD,		IP2SR0_11_8)
#define GPSR0_17	F_(MSIOF2_SCK,		IP2SR0_7_4)
#define GPSR0_16	F_(MSIOF2_TXD,		IP2SR0_3_0)
#define GPSR0_15	F_(MSIOF2_SYNC,		IP1SR0_31_28)
#define GPSR0_14	F_(MSIOF2_SS1,		IP1SR0_27_24)
#define GPSR0_13	F_(MSIOF2_SS2,		IP1SR0_23_20)
#define GPSR0_12	F_(MSIOF5_RXD,		IP1SR0_19_16)
#define GPSR0_11	F_(MSIOF5_SCK,		IP1SR0_15_12)
#define GPSR0_10	F_(MSIOF5_TXD,		IP1SR0_11_8)
#define GPSR0_9		F_(MSIOF5_SYNC,		IP1SR0_7_4)
#define GPSR0_8		F_(MSIOF5_SS1,		IP1SR0_3_0)
#define GPSR0_7		F_(MSIOF5_SS2,		IP0SR0_31_28)
#define GPSR0_6		F_(IRQ0,		IP0SR0_27_24)
#define GPSR0_5		F_(IRQ1,		IP0SR0_23_20)
#define GPSR0_4		F_(IRQ2,		IP0SR0_19_16)
#define GPSR0_3		F_(IRQ3,		IP0SR0_15_12)
#define GPSR0_2		F_(GP0_02,		IP0SR0_11_8)
#define GPSR0_1		F_(GP0_01,		IP0SR0_7_4)
#define GPSR0_0		F_(GP0_00,		IP0SR0_3_0)

/* GPSR1 */
#define GPSR1_29	F_(ERROROUTC_N_A,	IP3SR1_23_20)
#define GPSR1_28	F_(HTX3,		IP3SR1_19_16)
#define GPSR1_27	F_(HCTS3_N,		IP3SR1_15_12)
#define GPSR1_26	F_(HRTS3_N,		IP3SR1_11_8)
#define GPSR1_25	F_(HSCK3,		IP3SR1_7_4)
#define GPSR1_24	F_(HRX3,		IP3SR1_3_0)
#define GPSR1_23	F_(GP1_23,		IP2SR1_31_28)
#define GPSR1_22	F_(AUDIO_CLKIN,		IP2SR1_27_24)
#define GPSR1_21	F_(AUDIO_CLKOUT,	IP2SR1_23_20)
#define GPSR1_20	F_(SSI_SD,		IP2SR1_19_16)
#define GPSR1_19	F_(SSI_WS,		IP2SR1_15_12)
#define GPSR1_18	F_(SSI_SCK,		IP2SR1_11_8)
#define GPSR1_17	F_(SCIF_CLK,		IP2SR1_7_4)
#define GPSR1_16	F_(HRX0,		IP2SR1_3_0)
#define GPSR1_15	F_(HSCK0,		IP1SR1_31_28)
#define GPSR1_14	F_(HRTS0_N,		IP1SR1_27_24)
#define GPSR1_13	F_(HCTS0_N,		IP1SR1_23_20)
#define GPSR1_12	F_(HTX0,		IP1SR1_19_16)
#define GPSR1_11	F_(MSIOF0_RXD,		IP1SR1_15_12)
#define GPSR1_10	F_(MSIOF0_SCK,		IP1SR1_11_8)
#define GPSR1_9		F_(MSIOF0_TXD,		IP1SR1_7_4)
#define GPSR1_8		F_(MSIOF0_SYNC,		IP1SR1_3_0)
#define GPSR1_7		F_(MSIOF0_SS1,		IP0SR1_31_28)
#define GPSR1_6		F_(MSIOF0_SS2,		IP0SR1_27_24)
#define GPSR1_5		F_(MSIOF1_RXD,		IP0SR1_23_20)
#define GPSR1_4		F_(MSIOF1_TXD,		IP0SR1_19_16)
#define GPSR1_3		F_(MSIOF1_SCK,		IP0SR1_15_12)
#define GPSR1_2		F_(MSIOF1_SYNC,		IP0SR1_11_8)
#define GPSR1_1		F_(MSIOF1_SS1,		IP0SR1_7_4)
#define GPSR1_0		F_(MSIOF1_SS2,		IP0SR1_3_0)

/* GPSR2 */
#define GPSR2_19	F_(CANFD1_RX,		IP2SR2_15_12)
#define GPSR2_17	F_(CANFD1_TX,		IP2SR2_7_4)
#define GPSR2_15	F_(CANFD3_RX,		IP1SR2_31_28)
#define GPSR2_14	F_(CANFD3_TX,		IP1SR2_27_24)
#define GPSR2_13	F_(CANFD2_RX,		IP1SR2_23_20)
#define GPSR2_12	F_(CANFD2_TX,		IP1SR2_19_16)
#define GPSR2_11	F_(CANFD0_RX,		IP1SR2_15_12)
#define GPSR2_10	F_(CANFD0_TX,		IP1SR2_11_8)
#define GPSR2_9		F_(CAN_CLK,		IP1SR2_7_4)
#define GPSR2_8		F_(TPU0TO0,		IP1SR2_3_0)
#define GPSR2_7		F_(TPU0TO1,		IP0SR2_31_28)
#define GPSR2_6		F_(FXR_TXDB,		IP0SR2_27_24)
#define GPSR2_5		F_(FXR_TXENB_N_A,	IP0SR2_23_20)
#define GPSR2_4		F_(RXDB_EXTFXR,		IP0SR2_19_16)
#define GPSR2_3		F_(CLK_EXTFXR,		IP0SR2_15_12)
#define GPSR2_2		F_(RXDA_EXTFXR,		IP0SR2_11_8)
#define GPSR2_1		F_(FXR_TXENA_N_A,	IP0SR2_7_4)
#define GPSR2_0		F_(FXR_TXDA,		IP0SR2_3_0)

/* GPSR3 */
#define GPSR3_31	F_(TCLK4,		IP3SR3_31_28)
#define GPSR3_30	F_(TCLK3,		IP3SR3_27_24)
#define GPSR3_29	F_(RPC_INT_N,		IP3SR3_23_20)
#define GPSR3_28	F_(RPC_WP_N,		IP3SR3_19_16)
#define GPSR3_27	F_(RPC_RESET_N,		IP3SR3_15_12)
#define GPSR3_26	F_(QSPI1_IO3,		IP3SR3_11_8)
#define GPSR3_25	F_(QSPI1_SSL,		IP3SR3_7_4)
#define GPSR3_24	F_(QSPI1_IO2,		IP3SR3_3_0)
#define GPSR3_23	F_(QSPI1_MISO_IO1,	IP2SR3_31_28)
#define GPSR3_22	F_(QSPI1_SPCLK,		IP2SR3_27_24)
#define GPSR3_21	F_(QSPI1_MOSI_IO0,	IP2SR3_23_20)
#define GPSR3_20	F_(QSPI0_SPCLK,		IP2SR3_19_16)
#define GPSR3_19	F_(QSPI0_MOSI_IO0,	IP2SR3_15_12)
#define GPSR3_18	F_(QSPI0_MISO_IO1,	IP2SR3_11_8)
#define GPSR3_17	F_(QSPI0_IO2,		IP2SR3_7_4)
#define GPSR3_16	F_(QSPI0_IO3,		IP2SR3_3_0)
#define GPSR3_15	F_(QSPI0_SSL,		IP1SR3_31_28)
#define GPSR3_14	F_(PWM2,		IP1SR3_27_24)
#define GPSR3_13	F_(PWM1,		IP1SR3_23_20)
#define GPSR3_12	F_(SD_WP,		IP1SR3_19_16)
#define GPSR3_11	F_(SD_CD,		IP1SR3_15_12)
#define GPSR3_10	F_(MMC_SD_CMD,		IP1SR3_11_8)
#define GPSR3_9		F_(MMC_D6,		IP1SR3_7_4)
#define GPSR3_8		F_(MMC_D7,		IP1SR3_3_0)
#define GPSR3_7		F_(MMC_D4,		IP0SR3_31_28)
#define GPSR3_6		F_(MMC_D5,		IP0SR3_27_24)
#define GPSR3_5		F_(MMC_SD_D3,		IP0SR3_23_20)
#define GPSR3_4		F_(MMC_DS,		IP0SR3_19_16)
#define GPSR3_3		F_(MMC_SD_CLK,		IP0SR3_15_12)
#define GPSR3_2		F_(MMC_SD_D2,		IP0SR3_11_8)
#define GPSR3_1		F_(MMC_SD_D0,		IP0SR3_7_4)
#define GPSR3_0		F_(MMC_SD_D1,		IP0SR3_3_0)

/* GPSR4 */
#define GPSR4_24	F_(AVS1,		IP3SR4_3_0)
#define GPSR4_23	F_(AVS0,		IP2SR4_31_28)
#define GPSR4_21	F_(PCIE0_CLKREQ_N,	IP2SR4_23_20)
#define GPSR4_15	F_(PWM4,		IP1SR4_31_28)
#define GPSR4_14	F_(PWM3,		IP1SR4_27_24)
#define GPSR4_13	F_(HSCK2,		IP1SR4_23_20)
#define GPSR4_12	F_(HCTS2_N,		IP1SR4_19_16)
#define GPSR4_11	F_(SCIF_CLK2,		IP1SR4_15_12)
#define GPSR4_10	F_(HRTS2_N,		IP1SR4_11_8)
#define GPSR4_9		F_(HTX2,		IP1SR4_7_4)
#define GPSR4_8		F_(HRX2,		IP1SR4_3_0)
#define GPSR4_7		F_(SDA3,		IP0SR4_31_28)
#define GPSR4_6		F_(SCL3,		IP0SR4_27_24)
#define GPSR4_5		F_(SDA2,		IP0SR4_23_20)
#define GPSR4_4		F_(SCL2,		IP0SR4_19_16)
#define GPSR4_3		F_(SDA1,		IP0SR4_15_12)
#define GPSR4_2		F_(SCL1,		IP0SR4_11_8)
#define GPSR4_1		F_(SDA0,		IP0SR4_7_4)
#define GPSR4_0		F_(SCL0,		IP0SR4_3_0)

/* GPSR 5 */
#define GPSR5_20	F_(AVB2_RX_CTL,		IP2SR5_19_16)
#define GPSR5_19	F_(AVB2_TX_CTL,		IP2SR5_15_12)
#define GPSR5_18	F_(AVB2_RXC,		IP2SR5_11_8)
#define GPSR5_17	F_(AVB2_RD0,		IP2SR5_7_4)
#define GPSR5_16	F_(AVB2_TXC,		IP2SR5_3_0)
#define GPSR5_15	F_(AVB2_TD0,		IP1SR5_31_28)
#define GPSR5_14	F_(AVB2_RD1,		IP1SR5_27_24)
#define GPSR5_13	F_(AVB2_RD2,		IP1SR5_23_20)
#define GPSR5_12	F_(AVB2_TD1,		IP1SR5_19_16)
#define GPSR5_11	F_(AVB2_TD2,		IP1SR5_15_12)
#define GPSR5_10	F_(AVB2_MDIO,		IP1SR5_11_8)
#define GPSR5_9		F_(AVB2_RD3,		IP1SR5_7_4)
#define GPSR5_8		F_(AVB2_TD3,		IP1SR5_3_0)
#define GPSR5_7		F_(AVB2_TXCREFCLK,	IP0SR5_31_28)
#define GPSR5_6		F_(AVB2_MDC,		IP0SR5_27_24)
#define GPSR5_5		F_(AVB2_MAGIC,		IP0SR5_23_20)
#define GPSR5_4		F_(AVB2_PHY_INT,	IP0SR5_19_16)
#define GPSR5_3		F_(AVB2_LINK,		IP0SR5_15_12)
#define GPSR5_2		F_(AVB2_AVTP_MATCH,	IP0SR5_11_8)
#define GPSR5_1		F_(AVB2_AVTP_CAPTURE,	IP0SR5_7_4)
#define GPSR5_0		F_(AVB2_AVTP_PPS,	IP0SR5_3_0)

/* GPSR 6 */
#define GPSR6_20	F_(AVB1_TXCREFCLK,	IP2SR6_19_16)
#define GPSR6_19	F_(AVB1_RD3,		IP2SR6_15_12)
#define GPSR6_18	F_(AVB1_TD3,		IP2SR6_11_8)
#define GPSR6_17	F_(AVB1_RD2,		IP2SR6_7_4)
#define GPSR6_16	F_(AVB1_TD2,		IP2SR6_3_0)
#define GPSR6_15	F_(AVB1_RD0,		IP1SR6_31_28)
#define GPSR6_14	F_(AVB1_RD1,		IP1SR6_27_24)
#define GPSR6_13	F_(AVB1_TD0,		IP1SR6_23_20)
#define GPSR6_12	F_(AVB1_TD1,		IP1SR6_19_16)
#define GPSR6_11	F_(AVB1_AVTP_CAPTURE,	IP1SR6_15_12)
#define GPSR6_10	F_(AVB1_AVTP_PPS,	IP1SR6_11_8)
#define GPSR6_9		F_(AVB1_RX_CTL,		IP1SR6_7_4)
#define GPSR6_8		F_(AVB1_RXC,		IP1SR6_3_0)
#define GPSR6_7		F_(AVB1_TX_CTL,		IP0SR6_31_28)
#define GPSR6_6		F_(AVB1_TXC,		IP0SR6_27_24)
#define GPSR6_5		F_(AVB1_AVTP_MATCH,	IP0SR6_23_20)
#define GPSR6_4		F_(AVB1_LINK,		IP0SR6_19_16)
#define GPSR6_3		F_(AVB1_PHY_INT,	IP0SR6_15_12)
#define GPSR6_2		F_(AVB1_MDC,		IP0SR6_11_8)
#define GPSR6_1		F_(AVB1_MAGIC,		IP0SR6_7_4)
#define GPSR6_0		F_(AVB1_MDIO,		IP0SR6_3_0)

/* GPSR7 */
#define GPSR7_20	F_(AVB0_RX_CTL,		IP2SR7_19_16)
#define GPSR7_19	F_(AVB0_RXC,		IP2SR7_15_12)
#define GPSR7_18	F_(AVB0_RD0,		IP2SR7_11_8)
#define GPSR7_17	F_(AVB0_RD1,		IP2SR7_7_4)
#define GPSR7_16	F_(AVB0_TX_CTL,		IP2SR7_3_0)
#define GPSR7_15	F_(AVB0_TXC,		IP1SR7_31_28)
#define GPSR7_14	F_(AVB0_MDIO,		IP1SR7_27_24)
#define GPSR7_13	F_(AVB0_MDC,		IP1SR7_23_20)
#define GPSR7_12	F_(AVB0_RD2,		IP1SR7_19_16)
#define GPSR7_11	F_(AVB0_TD0,		IP1SR7_15_12)
#define GPSR7_10	F_(AVB0_MAGIC,		IP1SR7_11_8)
#define GPSR7_9		F_(AVB0_TXCREFCLK,	IP1SR7_7_4)
#define GPSR7_8		F_(AVB0_RD3,		IP1SR7_3_0)
#define GPSR7_7		F_(AVB0_TD1,		IP0SR7_31_28)
#define GPSR7_6		F_(AVB0_TD2,		IP0SR7_27_24)
#define GPSR7_5		F_(AVB0_PHY_INT,	IP0SR7_23_20)
#define GPSR7_4		F_(AVB0_LINK,		IP0SR7_19_16)
#define GPSR7_3		F_(AVB0_TD3,		IP0SR7_15_12)
#define GPSR7_2		F_(AVB0_AVTP_MATCH,	IP0SR7_11_8)
#define GPSR7_1		F_(AVB0_AVTP_CAPTURE,	IP0SR7_7_4)
#define GPSR7_0		F_(AVB0_AVTP_PPS,	IP0SR7_3_0)


/* SR0 */
/* IP0SR0 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP0SR0_3_0	F_(0, 0)		FM(ERROROUTC_N_B)	FM(TCLK2_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR0_7_4	F_(0, 0)		FM(MSIOF3_SS1)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR0_11_8	F_(0, 0)		FM(MSIOF3_SS2)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR0_15_12	FM(IRQ3)		FM(MSIOF3_SCK)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR0_19_16	FM(IRQ2)		FM(MSIOF3_TXD)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR0_23_20	FM(IRQ1)		FM(MSIOF3_RXD)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR0_27_24	FM(IRQ0)		FM(MSIOF3_SYNC)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR0_31_28	FM(MSIOF5_SS2)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP1SR0 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP1SR0_3_0	FM(MSIOF5_SS1)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR0_7_4	FM(MSIOF5_SYNC)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR0_11_8	FM(MSIOF5_TXD)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR0_15_12	FM(MSIOF5_SCK)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR0_19_16	FM(MSIOF5_RXD)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR0_23_20	FM(MSIOF2_SS2)		FM(TCLK1_A)		FM(IRQ2_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR0_27_24	FM(MSIOF2_SS1)		FM(HTX1_A)		FM(TX1_A)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR0_31_28	FM(MSIOF2_SYNC)		FM(HRX1_A)		FM(RX1_A)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP2SR0 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP2SR0_3_0	FM(MSIOF2_TXD)		FM(HCTS1_N_A)		FM(CTS1_N_A)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR0_7_4	FM(MSIOF2_SCK)		FM(HRTS1_N_A)		FM(RTS1_N_A)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR0_11_8	FM(MSIOF2_RXD)		FM(HSCK1_A)		FM(SCK1_A)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* SR1 */
/* IP0SR1 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP0SR1_3_0	FM(MSIOF1_SS2)		FM(HTX3_B)		FM(TX3_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_7_4	FM(MSIOF1_SS1)		FM(HCTS3_N_B)		FM(RX3_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_11_8	FM(MSIOF1_SYNC)		FM(HRTS3_N_B)		FM(RTS3_N_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_15_12	FM(MSIOF1_SCK)		FM(HSCK3_B)		FM(CTS3_N_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_19_16	FM(MSIOF1_TXD)		FM(HRX3_B)		FM(SCK3_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_23_20	FM(MSIOF1_RXD)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_27_24	FM(MSIOF0_SS2)		FM(HTX1_B)		FM(TX1_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_31_28	FM(MSIOF0_SS1)		FM(HRX1_B)		FM(RX1_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP1SR1 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP1SR1_3_0	FM(MSIOF0_SYNC)		FM(HCTS1_N_B)		FM(CTS1_N_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR1_7_4	FM(MSIOF0_TXD)		FM(HRTS1_N_B)		FM(RTS1_N_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR1_11_8	FM(MSIOF0_SCK)		FM(HSCK1_B)		FM(SCK1_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR1_15_12	FM(MSIOF0_RXD)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR1_19_16	FM(HTX0)		FM(TX0)			F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR1_23_20	FM(HCTS0_N)		FM(CTS0_N)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR1_27_24	FM(HRTS0_N)		FM(RTS0_N)		FM(PWM0_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR1_31_28	FM(HSCK0)		FM(SCK0)		FM(PWM0_A)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP2SR1 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP2SR1_3_0	FM(HRX0)		FM(RX0)			F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR1_7_4	FM(SCIF_CLK)		FM(IRQ4_A)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR1_11_8	FM(SSI_SCK)		FM(TCLK3_B)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR1_15_12	FM(SSI_WS)		FM(TCLK4_B)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR1_19_16	FM(SSI_SD)		FM(IRQ0_B)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR1_23_20	FM(AUDIO_CLKOUT)	FM(IRQ1_B)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR1_27_24	FM(AUDIO_CLKIN)		FM(PWM3_C)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR1_31_28	F_(0, 0)		FM(TCLK2_A)		FM(MSIOF4_SS1)	FM(IRQ3_B)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP3SR1 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP3SR1_3_0	FM(HRX3_A)		FM(SCK3_A)		FM(MSIOF4_SS2)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR1_7_4	FM(HSCK3_A)		FM(CTS3_N_A)		FM(MSIOF4_SCK)	FM(TPU0TO0_B)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR1_11_8	FM(HRTS3_N_A)		FM(RTS3_N_A)		FM(MSIOF4_TXD)	FM(TPU0TO1_B)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR1_15_12	FM(HCTS3_N_A)		FM(RX3_A)		FM(MSIOF4_RXD)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR1_19_16	FM(HTX3_A)		FM(TX3_A)		FM(MSIOF4_SYNC)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR1_23_20	FM(ERROROUTC_N_A)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* SR2 */
/* IP0SR2 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP0SR2_3_0	FM(FXR_TXDA)		F_(0, 0)		FM(TPU0TO2_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR2_7_4	FM(FXR_TXENA_N_A)	F_(0, 0)		FM(TPU0TO3_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR2_11_8	FM(RXDA_EXTFXR)		F_(0, 0)		FM(IRQ5)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR2_15_12	FM(CLK_EXTFXR)		F_(0, 0)		FM(IRQ4_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR2_19_16	FM(RXDB_EXTFXR)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR2_23_20	FM(FXR_TXENB_N_A)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR2_27_24	FM(FXR_TXDB)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR2_31_28	FM(TPU0TO1_A)		F_(0, 0)		F_(0, 0)	FM(TCLK2_C)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP1SR2 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP1SR2_3_0	FM(TPU0TO0_A)		F_(0, 0)		F_(0, 0)	FM(TCLK1_B)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR2_7_4	FM(CAN_CLK)		FM(FXR_TXENA_N_B)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR2_11_8	FM(CANFD0_TX)		FM(FXR_TXENB_N_B)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR2_15_12	FM(CANFD0_RX)		FM(STPWT_EXTFXR)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR2_19_16	FM(CANFD2_TX)		FM(TPU0TO2_A)		F_(0, 0)	FM(TCLK3_C)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR2_23_20	FM(CANFD2_RX)		FM(TPU0TO3_A)		FM(PWM1_B)	FM(TCLK4_C)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR2_27_24	FM(CANFD3_TX)		F_(0, 0)		FM(PWM2_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR2_31_28	FM(CANFD3_RX)		F_(0, 0)		FM(PWM3_B)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP2SR2 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP2SR2_7_4	FM(CANFD1_TX)		F_(0, 0)		FM(PWM1_C)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR2_15_12	FM(CANFD1_RX)		F_(0, 0)		FM(PWM2_C)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* SR3 */
/* IP0SR3 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP0SR3_3_0	FM(MMC_SD_D1)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR3_7_4	FM(MMC_SD_D0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR3_11_8	FM(MMC_SD_D2)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR3_15_12	FM(MMC_SD_CLK)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR3_19_16	FM(MMC_DS)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR3_23_20	FM(MMC_SD_D3)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR3_27_24	FM(MMC_D5)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR3_31_28	FM(MMC_D4)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP1SR3 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP1SR3_3_0	FM(MMC_D7)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR3_7_4	FM(MMC_D6)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR3_11_8	FM(MMC_SD_CMD)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR3_15_12	FM(SD_CD)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR3_19_16	FM(SD_WP)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR3_23_20	FM(PWM1_A)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR3_27_24	FM(PWM2_A)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR3_31_28	FM(QSPI0_SSL)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP2SR3 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP2SR3_3_0	FM(QSPI0_IO3)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR3_7_4	FM(QSPI0_IO2)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR3_11_8	FM(QSPI0_MISO_IO1)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR3_15_12	FM(QSPI0_MOSI_IO0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR3_19_16	FM(QSPI0_SPCLK)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR3_23_20	FM(QSPI1_MOSI_IO0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR3_27_24	FM(QSPI1_SPCLK)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR3_31_28	FM(QSPI1_MISO_IO1)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP3SR3 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP3SR3_3_0	FM(QSPI1_IO2)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR3_7_4	FM(QSPI1_SSL)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR3_11_8	FM(QSPI1_IO3)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR3_15_12	FM(RPC_RESET_N)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR3_19_16	FM(RPC_WP_N)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR3_23_20	FM(RPC_INT_N)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR3_27_24	FM(TCLK3_A)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR3_31_28	FM(TCLK4_A)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* SR4 */
/* IP0SR4 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP0SR4_3_0	FM(SCL0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_7_4	FM(SDA0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_11_8	FM(SCL1)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_15_12	FM(SDA1)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_19_16	FM(SCL2)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_23_20	FM(SDA2)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_27_24	FM(SCL3)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_31_28	FM(SDA3)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP1SR4 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP1SR4_3_0	FM(HRX2)		FM(SCK4)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR4_7_4	FM(HTX2)		FM(CTS4_N)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR4_11_8	FM(HRTS2_N)		FM(RTS4_N)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR4_15_12	FM(SCIF_CLK2)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR4_19_16	FM(HCTS2_N)		FM(TX4)			F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR4_23_20	FM(HSCK2)		FM(RX4)			F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR4_27_24	FM(PWM3_A)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR4_31_28	FM(PWM4)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP2SR4 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP2SR4_23_20	FM(PCIE0_CLKREQ_N)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR4_31_28	FM(AVS0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP3SR4 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP3SR4_3_0	FM(AVS1)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* SR5 */
/* IP0SR5 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP0SR5_3_0	FM(AVB2_AVTP_PPS)	FM(Ether_GPTP_PPS0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_7_4	FM(AVB2_AVTP_CAPTURE)	FM(Ether_GPTP_CAPTURE)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_11_8	FM(AVB2_AVTP_MATCH)	FM(Ether_GPTP_MATCH)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_15_12	FM(AVB2_LINK)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_19_16	FM(AVB2_PHY_INT)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_23_20	FM(AVB2_MAGIC)		FM(Ether_GPTP_PPS1)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_27_24	FM(AVB2_MDC)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_31_28	FM(AVB2_TXCREFCLK)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP1SR5 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP1SR5_3_0	FM(AVB2_TD3)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR5_7_4	FM(AVB2_RD3)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR5_11_8	FM(AVB2_MDIO)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR5_15_12	FM(AVB2_TD2)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR5_19_16	FM(AVB2_TD1)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR5_23_20	FM(AVB2_RD2)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR5_27_24	FM(AVB2_RD1)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR5_31_28	FM(AVB2_TD0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP2SR5 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP2SR5_3_0	FM(AVB2_TXC)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR5_7_4	FM(AVB2_RD0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR5_11_8	FM(AVB2_RXC)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR5_15_12	FM(AVB2_TX_CTL)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR5_19_16	FM(AVB2_RX_CTL)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* SR6 */
/* IP0SR6 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP0SR6_3_0	FM(AVB1_MDIO)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR6_7_4	FM(AVB1_MAGIC)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR6_11_8	FM(AVB1_MDC)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR6_15_12	FM(AVB1_PHY_INT)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR6_19_16	FM(AVB1_LINK)		FM(AVB1_MII_TX_ER)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR6_23_20	FM(AVB1_AVTP_MATCH)	FM(AVB1_MII_RX_ER)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR6_27_24	FM(AVB1_TXC)		FM(AVB1_MII_TXC)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR6_31_28	FM(AVB1_TX_CTL)		FM(AVB1_MII_TX_EN)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP1SR6 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP1SR6_3_0	FM(AVB1_RXC)		FM(AVB1_MII_RXC)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR6_7_4	FM(AVB1_RX_CTL)		FM(AVB1_MII_RX_DV)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR6_11_8	FM(AVB1_AVTP_PPS)	FM(AVB1_MII_COL)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR6_15_12	FM(AVB1_AVTP_CAPTURE)	FM(AVB1_MII_CRS)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR6_19_16	FM(AVB1_TD1)		FM(AVB1_MII_TD1)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR6_23_20	FM(AVB1_TD0)		FM(AVB1_MII_TD0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR6_27_24	FM(AVB1_RD1)		FM(AVB1_MII_RD1)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR6_31_28	FM(AVB1_RD0)		FM(AVB1_MII_RD0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP2SR6 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP2SR6_3_0	FM(AVB1_TD2)		FM(AVB1_MII_TD2)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR6_7_4	FM(AVB1_RD2)		FM(AVB1_MII_RD2)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR6_11_8	FM(AVB1_TD3)		FM(AVB1_MII_TD3)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR6_15_12	FM(AVB1_RD3)		FM(AVB1_MII_RD3)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR6_19_16	FM(AVB1_TXCREFCLK)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* SR7 */
/* IP0SR7 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP0SR7_3_0	FM(AVB0_AVTP_PPS)	FM(AVB0_MII_COL)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR7_7_4	FM(AVB0_AVTP_CAPTURE)	FM(AVB0_MII_CRS)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR7_11_8	FM(AVB0_AVTP_MATCH)	FM(AVB0_MII_RX_ER)	FM(CC5_OSCOUT)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR7_15_12	FM(AVB0_TD3)		FM(AVB0_MII_TD3)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR7_19_16	FM(AVB0_LINK)		FM(AVB0_MII_TX_ER)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR7_23_20	FM(AVB0_PHY_INT)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR7_27_24	FM(AVB0_TD2)		FM(AVB0_MII_TD2)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR7_31_28	FM(AVB0_TD1)		FM(AVB0_MII_TD1)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP1SR7 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP1SR7_3_0	FM(AVB0_RD3)		FM(AVB0_MII_RD3)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR7_7_4	FM(AVB0_TXCREFCLK)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR7_11_8	FM(AVB0_MAGIC)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR7_15_12	FM(AVB0_TD0)		FM(AVB0_MII_TD0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR7_19_16	FM(AVB0_RD2)		FM(AVB0_MII_RD2)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR7_23_20	FM(AVB0_MDC)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR7_27_24	FM(AVB0_MDIO)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR7_31_28	FM(AVB0_TXC)		FM(AVB0_MII_TXC)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP2SR7 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP2SR7_3_0	FM(AVB0_TX_CTL)		FM(AVB0_MII_TX_EN)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR7_7_4	FM(AVB0_RD1)		FM(AVB0_MII_RD1)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR7_11_8	FM(AVB0_RD0)		FM(AVB0_MII_RD0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR7_15_12	FM(AVB0_RXC)		FM(AVB0_MII_RXC)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR7_19_16	FM(AVB0_RX_CTL)		FM(AVB0_MII_RX_DV)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

#define PINMUX_GPSR	\
						GPSR3_31									\
						GPSR3_30									\
		GPSR1_29			GPSR3_29									\
		GPSR1_28			GPSR3_28									\
		GPSR1_27			GPSR3_27									\
		GPSR1_26			GPSR3_26									\
		GPSR1_25			GPSR3_25									\
		GPSR1_24			GPSR3_24	GPSR4_24							\
		GPSR1_23			GPSR3_23	GPSR4_23							\
		GPSR1_22			GPSR3_22									\
		GPSR1_21			GPSR3_21	GPSR4_21							\
		GPSR1_20			GPSR3_20			GPSR5_20	GPSR6_20	GPSR7_20	\
		GPSR1_19	GPSR2_19	GPSR3_19			GPSR5_19	GPSR6_19	GPSR7_19	\
GPSR0_18	GPSR1_18			GPSR3_18			GPSR5_18	GPSR6_18	GPSR7_18	\
GPSR0_17	GPSR1_17	GPSR2_17	GPSR3_17			GPSR5_17	GPSR6_17	GPSR7_17	\
GPSR0_16	GPSR1_16			GPSR3_16			GPSR5_16	GPSR6_16	GPSR7_16	\
GPSR0_15	GPSR1_15	GPSR2_15	GPSR3_15	GPSR4_15	GPSR5_15	GPSR6_15	GPSR7_15	\
GPSR0_14	GPSR1_14	GPSR2_14	GPSR3_14	GPSR4_14	GPSR5_14	GPSR6_14	GPSR7_14	\
GPSR0_13	GPSR1_13	GPSR2_13	GPSR3_13	GPSR4_13	GPSR5_13	GPSR6_13	GPSR7_13	\
GPSR0_12	GPSR1_12	GPSR2_12	GPSR3_12	GPSR4_12	GPSR5_12	GPSR6_12	GPSR7_12	\
GPSR0_11	GPSR1_11	GPSR2_11	GPSR3_11	GPSR4_11	GPSR5_11	GPSR6_11	GPSR7_11	\
GPSR0_10	GPSR1_10	GPSR2_10	GPSR3_10	GPSR4_10	GPSR5_10	GPSR6_10	GPSR7_10	\
GPSR0_9		GPSR1_9		GPSR2_9		GPSR3_9		GPSR4_9		GPSR5_9		GPSR6_9		GPSR7_9		\
GPSR0_8		GPSR1_8		GPSR2_8		GPSR3_8		GPSR4_8		GPSR5_8		GPSR6_8		GPSR7_8		\
GPSR0_7		GPSR1_7		GPSR2_7		GPSR3_7		GPSR4_7		GPSR5_7		GPSR6_7		GPSR7_7		\
GPSR0_6		GPSR1_6		GPSR2_6		GPSR3_6		GPSR4_6		GPSR5_6		GPSR6_6		GPSR7_6		\
GPSR0_5		GPSR1_5		GPSR2_5		GPSR3_5		GPSR4_5		GPSR5_5		GPSR6_5		GPSR7_5		\
GPSR0_4		GPSR1_4		GPSR2_4		GPSR3_4		GPSR4_4		GPSR5_4		GPSR6_4		GPSR7_4		\
GPSR0_3		GPSR1_3		GPSR2_3		GPSR3_3		GPSR4_3		GPSR5_3		GPSR6_3		GPSR7_3		\
GPSR0_2		GPSR1_2		GPSR2_2		GPSR3_2		GPSR4_2		GPSR5_2		GPSR6_2		GPSR7_2		\
GPSR0_1		GPSR1_1		GPSR2_1		GPSR3_1		GPSR4_1		GPSR5_1		GPSR6_1		GPSR7_1		\
GPSR0_0		GPSR1_0		GPSR2_0		GPSR3_0		GPSR4_0		GPSR5_0		GPSR6_0		GPSR7_0

#define PINMUX_IPSR	\
\
FM(IP0SR0_3_0)		IP0SR0_3_0	FM(IP1SR0_3_0)		IP1SR0_3_0	FM(IP2SR0_3_0)		IP2SR0_3_0	\
FM(IP0SR0_7_4)		IP0SR0_7_4	FM(IP1SR0_7_4)		IP1SR0_7_4	FM(IP2SR0_7_4)		IP2SR0_7_4	\
FM(IP0SR0_11_8)		IP0SR0_11_8	FM(IP1SR0_11_8)		IP1SR0_11_8	FM(IP2SR0_11_8)		IP2SR0_11_8	\
FM(IP0SR0_15_12)	IP0SR0_15_12	FM(IP1SR0_15_12)	IP1SR0_15_12	\
FM(IP0SR0_19_16)	IP0SR0_19_16	FM(IP1SR0_19_16)	IP1SR0_19_16	\
FM(IP0SR0_23_20)	IP0SR0_23_20	FM(IP1SR0_23_20)	IP1SR0_23_20	\
FM(IP0SR0_27_24)	IP0SR0_27_24	FM(IP1SR0_27_24)	IP1SR0_27_24	\
FM(IP0SR0_31_28)	IP0SR0_31_28	FM(IP1SR0_31_28)	IP1SR0_31_28	\
\
FM(IP0SR1_3_0)		IP0SR1_3_0	FM(IP1SR1_3_0)		IP1SR1_3_0	FM(IP2SR1_3_0)		IP2SR1_3_0	FM(IP3SR1_3_0)		IP3SR1_3_0	\
FM(IP0SR1_7_4)		IP0SR1_7_4	FM(IP1SR1_7_4)		IP1SR1_7_4	FM(IP2SR1_7_4)		IP2SR1_7_4	FM(IP3SR1_7_4)		IP3SR1_7_4	\
FM(IP0SR1_11_8)		IP0SR1_11_8	FM(IP1SR1_11_8)		IP1SR1_11_8	FM(IP2SR1_11_8)		IP2SR1_11_8	FM(IP3SR1_11_8)		IP3SR1_11_8	\
FM(IP0SR1_15_12)	IP0SR1_15_12	FM(IP1SR1_15_12)	IP1SR1_15_12	FM(IP2SR1_15_12)	IP2SR1_15_12	FM(IP3SR1_15_12)	IP3SR1_15_12	\
FM(IP0SR1_19_16)	IP0SR1_19_16	FM(IP1SR1_19_16)	IP1SR1_19_16	FM(IP2SR1_19_16)	IP2SR1_19_16	FM(IP3SR1_19_16)	IP3SR1_19_16	\
FM(IP0SR1_23_20)	IP0SR1_23_20	FM(IP1SR1_23_20)	IP1SR1_23_20	FM(IP2SR1_23_20)	IP2SR1_23_20	FM(IP3SR1_23_20)	IP3SR1_23_20	\
FM(IP0SR1_27_24)	IP0SR1_27_24	FM(IP1SR1_27_24)	IP1SR1_27_24	FM(IP2SR1_27_24)	IP2SR1_27_24	\
FM(IP0SR1_31_28)	IP0SR1_31_28	FM(IP1SR1_31_28)	IP1SR1_31_28	FM(IP2SR1_31_28)	IP2SR1_31_28	\
\
FM(IP0SR2_3_0)		IP0SR2_3_0	FM(IP1SR2_3_0)		IP1SR2_3_0	\
FM(IP0SR2_7_4)		IP0SR2_7_4	FM(IP1SR2_7_4)		IP1SR2_7_4	FM(IP2SR2_7_4)		IP2SR2_7_4	\
FM(IP0SR2_11_8)		IP0SR2_11_8	FM(IP1SR2_11_8)		IP1SR2_11_8	\
FM(IP0SR2_15_12)	IP0SR2_15_12	FM(IP1SR2_15_12)	IP1SR2_15_12	FM(IP2SR2_15_12)	IP2SR2_15_12	\
FM(IP0SR2_19_16)	IP0SR2_19_16	FM(IP1SR2_19_16)	IP1SR2_19_16	\
FM(IP0SR2_23_20)	IP0SR2_23_20	FM(IP1SR2_23_20)	IP1SR2_23_20	\
FM(IP0SR2_27_24)	IP0SR2_27_24	FM(IP1SR2_27_24)	IP1SR2_27_24	\
FM(IP0SR2_31_28)	IP0SR2_31_28	FM(IP1SR2_31_28)	IP1SR2_31_28	\
\
FM(IP0SR3_3_0)		IP0SR3_3_0	FM(IP1SR3_3_0)		IP1SR3_3_0	FM(IP2SR3_3_0)		IP2SR3_3_0	FM(IP3SR3_3_0)		IP3SR3_3_0	\
FM(IP0SR3_7_4)		IP0SR3_7_4	FM(IP1SR3_7_4)		IP1SR3_7_4	FM(IP2SR3_7_4)		IP2SR3_7_4	FM(IP3SR3_7_4)		IP3SR3_7_4	\
FM(IP0SR3_11_8)		IP0SR3_11_8	FM(IP1SR3_11_8)		IP1SR3_11_8	FM(IP2SR3_11_8)		IP2SR3_11_8	FM(IP3SR3_11_8)		IP3SR3_11_8	\
FM(IP0SR3_15_12)	IP0SR3_15_12	FM(IP1SR3_15_12)	IP1SR3_15_12	FM(IP2SR3_15_12)	IP2SR3_15_12	FM(IP3SR3_15_12)	IP3SR3_15_12	\
FM(IP0SR3_19_16)	IP0SR3_19_16	FM(IP1SR3_19_16)	IP1SR3_19_16	FM(IP2SR3_19_16)	IP2SR3_19_16	FM(IP3SR3_19_16)	IP3SR3_19_16	\
FM(IP0SR3_23_20)	IP0SR3_23_20	FM(IP1SR3_23_20)	IP1SR3_23_20	FM(IP2SR3_23_20)	IP2SR3_23_20	FM(IP3SR3_23_20)	IP3SR3_23_20	\
FM(IP0SR3_27_24)	IP0SR3_27_24	FM(IP1SR3_27_24)	IP1SR3_27_24	FM(IP2SR3_27_24)	IP2SR3_27_24	FM(IP3SR3_27_24)	IP3SR3_27_24	\
FM(IP0SR3_31_28)	IP0SR3_31_28	FM(IP1SR3_31_28)	IP1SR3_31_28	FM(IP2SR3_31_28)	IP2SR3_31_28	FM(IP3SR3_31_28)	IP3SR3_31_28	\
\
FM(IP0SR4_3_0)		IP0SR4_3_0	FM(IP1SR4_3_0)		IP1SR4_3_0						FM(IP3SR4_3_0)		IP3SR4_3_0	\
FM(IP0SR4_7_4)		IP0SR4_7_4	FM(IP1SR4_7_4)		IP1SR4_7_4	\
FM(IP0SR4_11_8)		IP0SR4_11_8	FM(IP1SR4_11_8)		IP1SR4_11_8	\
FM(IP0SR4_15_12)	IP0SR4_15_12	FM(IP1SR4_15_12)	IP1SR4_15_12	\
FM(IP0SR4_19_16)	IP0SR4_19_16	FM(IP1SR4_19_16)	IP1SR4_19_16	\
FM(IP0SR4_23_20)	IP0SR4_23_20	FM(IP1SR4_23_20)	IP1SR4_23_20	FM(IP2SR4_23_20)	IP2SR4_23_20	\
FM(IP0SR4_27_24)	IP0SR4_27_24	FM(IP1SR4_27_24)	IP1SR4_27_24	\
FM(IP0SR4_31_28)	IP0SR4_31_28	FM(IP1SR4_31_28)	IP1SR4_31_28	FM(IP2SR4_31_28)	IP2SR4_31_28	\
\
FM(IP0SR5_3_0)		IP0SR5_3_0	FM(IP1SR5_3_0)		IP1SR5_3_0	FM(IP2SR5_3_0)		IP2SR5_3_0	\
FM(IP0SR5_7_4)		IP0SR5_7_4	FM(IP1SR5_7_4)		IP1SR5_7_4	FM(IP2SR5_7_4)		IP2SR5_7_4	\
FM(IP0SR5_11_8)		IP0SR5_11_8	FM(IP1SR5_11_8)		IP1SR5_11_8	FM(IP2SR5_11_8)		IP2SR5_11_8	\
FM(IP0SR5_15_12)	IP0SR5_15_12	FM(IP1SR5_15_12)	IP1SR5_15_12	FM(IP2SR5_15_12)	IP2SR5_15_12	\
FM(IP0SR5_19_16)	IP0SR5_19_16	FM(IP1SR5_19_16)	IP1SR5_19_16	FM(IP2SR5_19_16)	IP2SR5_19_16	\
FM(IP0SR5_23_20)	IP0SR5_23_20	FM(IP1SR5_23_20)	IP1SR5_23_20	\
FM(IP0SR5_27_24)	IP0SR5_27_24	FM(IP1SR5_27_24)	IP1SR5_27_24	\
FM(IP0SR5_31_28)	IP0SR5_31_28	FM(IP1SR5_31_28)	IP1SR5_31_28	\
\
FM(IP0SR6_3_0)		IP0SR6_3_0	FM(IP1SR6_3_0)		IP1SR6_3_0	FM(IP2SR6_3_0)		IP2SR6_3_0	\
FM(IP0SR6_7_4)		IP0SR6_7_4	FM(IP1SR6_7_4)		IP1SR6_7_4	FM(IP2SR6_7_4)		IP2SR6_7_4	\
FM(IP0SR6_11_8)		IP0SR6_11_8	FM(IP1SR6_11_8)		IP1SR6_11_8	FM(IP2SR6_11_8)		IP2SR6_11_8	\
FM(IP0SR6_15_12)	IP0SR6_15_12	FM(IP1SR6_15_12)	IP1SR6_15_12	FM(IP2SR6_15_12)	IP2SR6_15_12	\
FM(IP0SR6_19_16)	IP0SR6_19_16	FM(IP1SR6_19_16)	IP1SR6_19_16	FM(IP2SR6_19_16)	IP2SR6_19_16	\
FM(IP0SR6_23_20)	IP0SR6_23_20	FM(IP1SR6_23_20)	IP1SR6_23_20	\
FM(IP0SR6_27_24)	IP0SR6_27_24	FM(IP1SR6_27_24)	IP1SR6_27_24	\
FM(IP0SR6_31_28)	IP0SR6_31_28	FM(IP1SR6_31_28)	IP1SR6_31_28	\
\
FM(IP0SR7_3_0)		IP0SR7_3_0	FM(IP1SR7_3_0)		IP1SR7_3_0	FM(IP2SR7_3_0)		IP2SR7_3_0	\
FM(IP0SR7_7_4)		IP0SR7_7_4	FM(IP1SR7_7_4)		IP1SR7_7_4	FM(IP2SR7_7_4)		IP2SR7_7_4	\
FM(IP0SR7_11_8)		IP0SR7_11_8	FM(IP1SR7_11_8)		IP1SR7_11_8	FM(IP2SR7_11_8)		IP2SR7_11_8	\
FM(IP0SR7_15_12)	IP0SR7_15_12	FM(IP1SR7_15_12)	IP1SR7_15_12	FM(IP2SR7_15_12)	IP2SR7_15_12	\
FM(IP0SR7_19_16)	IP0SR7_19_16	FM(IP1SR7_19_16)	IP1SR7_19_16	FM(IP2SR7_19_16)	IP2SR7_19_16	\
FM(IP0SR7_23_20)	IP0SR7_23_20	FM(IP1SR7_23_20)	IP1SR7_23_20	\
FM(IP0SR7_27_24)	IP0SR7_27_24	FM(IP1SR7_27_24)	IP1SR7_27_24	\
FM(IP0SR7_31_28)	IP0SR7_31_28	FM(IP1SR7_31_28)	IP1SR7_31_28	\

/* MOD_SEL4 */			/* 0 */				/* 1 */
#define MOD_SEL4_7		FM(SEL_SDA3_0)			FM(SEL_SDA3_1)
#define MOD_SEL4_6		FM(SEL_SCL3_0)			FM(SEL_SCL3_1)
#define MOD_SEL4_5		FM(SEL_SDA2_0)			FM(SEL_SDA2_1)
#define MOD_SEL4_4		FM(SEL_SCL2_0)			FM(SEL_SCL2_1)
#define MOD_SEL4_3		FM(SEL_SDA1_0)			FM(SEL_SDA1_1)
#define MOD_SEL4_2		FM(SEL_SCL1_0)			FM(SEL_SCL1_1)
#define MOD_SEL4_1		FM(SEL_SDA0_0)			FM(SEL_SDA0_1)
#define MOD_SEL4_0		FM(SEL_SCL0_0)			FM(SEL_SCL0_1)

#define PINMUX_MOD_SELS \
\
MOD_SEL4_7	\
MOD_SEL4_6	\
MOD_SEL4_5	\
MOD_SEL4_4	\
MOD_SEL4_3	\
MOD_SEL4_2	\
MOD_SEL4_1	\
MOD_SEL4_0

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

	/* IP0SR0 */
	PINMUX_IPSR_GPSR(IP0SR0_3_0,	ERROROUTC_N_B),
	PINMUX_IPSR_GPSR(IP0SR0_3_0,	TCLK2_B),

	PINMUX_IPSR_GPSR(IP0SR0_7_4,	MSIOF3_SS1),

	PINMUX_IPSR_GPSR(IP0SR0_11_8,	MSIOF3_SS2),

	PINMUX_IPSR_GPSR(IP0SR0_15_12,	IRQ3),
	PINMUX_IPSR_GPSR(IP0SR0_15_12,	MSIOF3_SCK),

	PINMUX_IPSR_GPSR(IP0SR0_19_16,	IRQ2),
	PINMUX_IPSR_GPSR(IP0SR0_19_16,	MSIOF3_TXD),

	PINMUX_IPSR_GPSR(IP0SR0_23_20,	IRQ1),
	PINMUX_IPSR_GPSR(IP0SR0_23_20,	MSIOF3_RXD),

	PINMUX_IPSR_GPSR(IP0SR0_27_24,	IRQ0),
	PINMUX_IPSR_GPSR(IP0SR0_27_24,	MSIOF3_SYNC),

	PINMUX_IPSR_GPSR(IP0SR0_31_28,	MSIOF5_SS2),

	/* IP1SR0 */
	PINMUX_IPSR_GPSR(IP1SR0_3_0,	MSIOF5_SS1),

	PINMUX_IPSR_GPSR(IP1SR0_7_4,	MSIOF5_SYNC),

	PINMUX_IPSR_GPSR(IP1SR0_11_8,	MSIOF5_TXD),

	PINMUX_IPSR_GPSR(IP1SR0_15_12,	MSIOF5_SCK),

	PINMUX_IPSR_GPSR(IP1SR0_19_16,	MSIOF5_RXD),

	PINMUX_IPSR_GPSR(IP1SR0_23_20,	MSIOF2_SS2),
	PINMUX_IPSR_GPSR(IP1SR0_23_20,	TCLK1_A),
	PINMUX_IPSR_GPSR(IP1SR0_23_20,	IRQ2_B),

	PINMUX_IPSR_GPSR(IP1SR0_27_24,	MSIOF2_SS1),
	PINMUX_IPSR_GPSR(IP1SR0_27_24,	HTX1_A),
	PINMUX_IPSR_GPSR(IP1SR0_27_24,	TX1_A),

	PINMUX_IPSR_GPSR(IP1SR0_31_28,	MSIOF2_SYNC),
	PINMUX_IPSR_GPSR(IP1SR0_31_28,	HRX1_A),
	PINMUX_IPSR_GPSR(IP1SR0_31_28,	RX1_A),

	/* IP2SR0 */
	PINMUX_IPSR_GPSR(IP2SR0_3_0,	MSIOF2_TXD),
	PINMUX_IPSR_GPSR(IP2SR0_3_0,	HCTS1_N_A),
	PINMUX_IPSR_GPSR(IP2SR0_3_0,	CTS1_N_A),

	PINMUX_IPSR_GPSR(IP2SR0_7_4,	MSIOF2_SCK),
	PINMUX_IPSR_GPSR(IP2SR0_7_4,	HRTS1_N_A),
	PINMUX_IPSR_GPSR(IP2SR0_7_4,	RTS1_N_A),

	PINMUX_IPSR_GPSR(IP2SR0_11_8,	MSIOF2_RXD),
	PINMUX_IPSR_GPSR(IP2SR0_11_8,	HSCK1_A),
	PINMUX_IPSR_GPSR(IP2SR0_11_8,	SCK1_A),

	/* IP0SR1 */
	PINMUX_IPSR_GPSR(IP0SR1_3_0,	MSIOF1_SS2),
	PINMUX_IPSR_GPSR(IP0SR1_3_0,	HTX3_B),
	PINMUX_IPSR_GPSR(IP0SR1_3_0,	TX3_B),

	PINMUX_IPSR_GPSR(IP0SR1_7_4,	MSIOF1_SS1),
	PINMUX_IPSR_GPSR(IP0SR1_7_4,	HCTS3_N_B),
	PINMUX_IPSR_GPSR(IP0SR1_7_4,	RX3_B),

	PINMUX_IPSR_GPSR(IP0SR1_11_8,	MSIOF1_SYNC),
	PINMUX_IPSR_GPSR(IP0SR1_11_8,	HRTS3_N_B),
	PINMUX_IPSR_GPSR(IP0SR1_11_8,	RTS3_N_B),

	PINMUX_IPSR_GPSR(IP0SR1_15_12,	MSIOF1_SCK),
	PINMUX_IPSR_GPSR(IP0SR1_15_12,	HSCK3_B),
	PINMUX_IPSR_GPSR(IP0SR1_15_12,	CTS3_N_B),

	PINMUX_IPSR_GPSR(IP0SR1_19_16,	MSIOF1_TXD),
	PINMUX_IPSR_GPSR(IP0SR1_19_16,	HRX3_B),
	PINMUX_IPSR_GPSR(IP0SR1_19_16,	SCK3_B),

	PINMUX_IPSR_GPSR(IP0SR1_23_20,	MSIOF1_RXD),

	PINMUX_IPSR_GPSR(IP0SR1_27_24,	MSIOF0_SS2),
	PINMUX_IPSR_GPSR(IP0SR1_27_24,	HTX1_B),
	PINMUX_IPSR_GPSR(IP0SR1_27_24,	TX1_B),

	PINMUX_IPSR_GPSR(IP0SR1_31_28,	MSIOF0_SS1),
	PINMUX_IPSR_GPSR(IP0SR1_31_28,	HRX1_B),
	PINMUX_IPSR_GPSR(IP0SR1_31_28,	RX1_B),

	/* IP1SR1 */
	PINMUX_IPSR_GPSR(IP1SR1_3_0,	MSIOF0_SYNC),
	PINMUX_IPSR_GPSR(IP1SR1_3_0,	HCTS1_N_B),
	PINMUX_IPSR_GPSR(IP1SR1_3_0,	CTS1_N_B),

	PINMUX_IPSR_GPSR(IP1SR1_7_4,	MSIOF0_TXD),
	PINMUX_IPSR_GPSR(IP1SR1_7_4,	HRTS1_N_B),
	PINMUX_IPSR_GPSR(IP1SR1_7_4,	RTS1_N_B),

	PINMUX_IPSR_GPSR(IP1SR1_11_8,	MSIOF0_SCK),
	PINMUX_IPSR_GPSR(IP1SR1_11_8,	HSCK1_B),
	PINMUX_IPSR_GPSR(IP1SR1_11_8,	SCK1_B),

	PINMUX_IPSR_GPSR(IP1SR1_15_12,	MSIOF0_RXD),

	PINMUX_IPSR_GPSR(IP1SR1_19_16,	HTX0),
	PINMUX_IPSR_GPSR(IP1SR1_19_16,	TX0),

	PINMUX_IPSR_GPSR(IP1SR1_23_20,	HCTS0_N),
	PINMUX_IPSR_GPSR(IP1SR1_23_20,	CTS0_N),

	PINMUX_IPSR_GPSR(IP1SR1_27_24,	HRTS0_N),
	PINMUX_IPSR_GPSR(IP1SR1_27_24,	RTS0_N),
	PINMUX_IPSR_GPSR(IP1SR1_27_24,	PWM0_B),

	PINMUX_IPSR_GPSR(IP1SR1_31_28,	HSCK0),
	PINMUX_IPSR_GPSR(IP1SR1_31_28,	SCK0),
	PINMUX_IPSR_GPSR(IP1SR1_31_28,	PWM0_A),

	/* IP2SR1 */
	PINMUX_IPSR_GPSR(IP2SR1_3_0,	HRX0),
	PINMUX_IPSR_GPSR(IP2SR1_3_0,	RX0),

	PINMUX_IPSR_GPSR(IP2SR1_7_4,	SCIF_CLK),
	PINMUX_IPSR_GPSR(IP2SR1_7_4,	IRQ4_A),

	PINMUX_IPSR_GPSR(IP2SR1_11_8,	SSI_SCK),
	PINMUX_IPSR_GPSR(IP2SR1_11_8,	TCLK3_B),

	PINMUX_IPSR_GPSR(IP2SR1_15_12,	SSI_WS),
	PINMUX_IPSR_GPSR(IP2SR1_15_12,	TCLK4_B),

	PINMUX_IPSR_GPSR(IP2SR1_19_16,	SSI_SD),
	PINMUX_IPSR_GPSR(IP2SR1_19_16,	IRQ0_B),

	PINMUX_IPSR_GPSR(IP2SR1_23_20,	AUDIO_CLKOUT),
	PINMUX_IPSR_GPSR(IP2SR1_23_20,	IRQ1_B),

	PINMUX_IPSR_GPSR(IP2SR1_27_24,	AUDIO_CLKIN),
	PINMUX_IPSR_GPSR(IP2SR1_27_24,	PWM3_C),

	PINMUX_IPSR_GPSR(IP2SR1_31_28,	TCLK2_A),
	PINMUX_IPSR_GPSR(IP2SR1_31_28,	MSIOF4_SS1),
	PINMUX_IPSR_GPSR(IP2SR1_31_28,	IRQ3_B),

	/* IP3SR1 */
	PINMUX_IPSR_GPSR(IP3SR1_3_0,	HRX3_A),
	PINMUX_IPSR_GPSR(IP3SR1_3_0,	SCK3_A),
	PINMUX_IPSR_GPSR(IP3SR1_3_0,	MSIOF4_SS2),

	PINMUX_IPSR_GPSR(IP3SR1_7_4,	HSCK3_A),
	PINMUX_IPSR_GPSR(IP3SR1_7_4,	CTS3_N_A),
	PINMUX_IPSR_GPSR(IP3SR1_7_4,	MSIOF4_SCK),
	PINMUX_IPSR_GPSR(IP3SR1_7_4,	TPU0TO0_B),

	PINMUX_IPSR_GPSR(IP3SR1_11_8,	HRTS3_N_A),
	PINMUX_IPSR_GPSR(IP3SR1_11_8,	RTS3_N_A),
	PINMUX_IPSR_GPSR(IP3SR1_11_8,	MSIOF4_TXD),
	PINMUX_IPSR_GPSR(IP3SR1_11_8,	TPU0TO1_B),

	PINMUX_IPSR_GPSR(IP3SR1_15_12,	HCTS3_N_A),
	PINMUX_IPSR_GPSR(IP3SR1_15_12,	RX3_A),
	PINMUX_IPSR_GPSR(IP3SR1_15_12,	MSIOF4_RXD),

	PINMUX_IPSR_GPSR(IP3SR1_19_16,	HTX3_A),
	PINMUX_IPSR_GPSR(IP3SR1_19_16,	TX3_A),
	PINMUX_IPSR_GPSR(IP3SR1_19_16,	MSIOF4_SYNC),

	PINMUX_IPSR_GPSR(IP3SR1_23_20,	ERROROUTC_N_A),

	/* IP0SR2 */
	PINMUX_IPSR_GPSR(IP0SR2_3_0,	FXR_TXDA),
	PINMUX_IPSR_GPSR(IP0SR2_3_0,	TPU0TO2_B),

	PINMUX_IPSR_GPSR(IP0SR2_7_4,	FXR_TXENA_N_A),
	PINMUX_IPSR_GPSR(IP0SR2_7_4,	TPU0TO3_B),

	PINMUX_IPSR_GPSR(IP0SR2_11_8,	RXDA_EXTFXR),
	PINMUX_IPSR_GPSR(IP0SR2_11_8,	IRQ5),

	PINMUX_IPSR_GPSR(IP0SR2_15_12,	CLK_EXTFXR),
	PINMUX_IPSR_GPSR(IP0SR2_15_12,	IRQ4_B),

	PINMUX_IPSR_GPSR(IP0SR2_19_16,	RXDB_EXTFXR),

	PINMUX_IPSR_GPSR(IP0SR2_23_20,	FXR_TXENB_N_A),

	PINMUX_IPSR_GPSR(IP0SR2_27_24,	FXR_TXDB),

	PINMUX_IPSR_GPSR(IP0SR2_31_28,	TPU0TO1_A),
	PINMUX_IPSR_GPSR(IP0SR2_31_28,	TCLK2_C),

	/* IP1SR2 */
	PINMUX_IPSR_GPSR(IP1SR2_3_0,	TPU0TO0_A),
	PINMUX_IPSR_GPSR(IP1SR2_3_0,	TCLK1_B),

	PINMUX_IPSR_GPSR(IP1SR2_7_4,	CAN_CLK),
	PINMUX_IPSR_GPSR(IP1SR2_7_4,	FXR_TXENA_N_B),

	PINMUX_IPSR_GPSR(IP1SR2_11_8,	CANFD0_TX),
	PINMUX_IPSR_GPSR(IP1SR2_11_8,	FXR_TXENB_N_B),

	PINMUX_IPSR_GPSR(IP1SR2_15_12,	CANFD0_RX),
	PINMUX_IPSR_GPSR(IP1SR2_15_12,	STPWT_EXTFXR),

	PINMUX_IPSR_GPSR(IP1SR2_19_16,	CANFD2_TX),
	PINMUX_IPSR_GPSR(IP1SR2_19_16,	TPU0TO2_A),
	PINMUX_IPSR_GPSR(IP1SR2_19_16,	TCLK3_C),

	PINMUX_IPSR_GPSR(IP1SR2_23_20,	CANFD2_RX),
	PINMUX_IPSR_GPSR(IP1SR2_23_20,	TPU0TO3_A),
	PINMUX_IPSR_GPSR(IP1SR2_23_20,	PWM1_B),
	PINMUX_IPSR_GPSR(IP1SR2_23_20,	TCLK4_C),

	PINMUX_IPSR_GPSR(IP1SR2_27_24,	CANFD3_TX),
	PINMUX_IPSR_GPSR(IP1SR2_27_24,	PWM2_B),

	PINMUX_IPSR_GPSR(IP1SR2_31_28,	CANFD3_RX),
	PINMUX_IPSR_GPSR(IP1SR2_31_28,	PWM3_B),

	/* IP2SR2 */
	PINMUX_IPSR_GPSR(IP2SR2_7_4,	CANFD1_TX),
	PINMUX_IPSR_GPSR(IP2SR2_7_4,	PWM1_C),

	PINMUX_IPSR_GPSR(IP2SR2_15_12,	CANFD1_RX),
	PINMUX_IPSR_GPSR(IP2SR2_15_12,	PWM2_C),

	/* IP0SR3 */
	PINMUX_IPSR_GPSR(IP0SR3_3_0,	MMC_SD_D1),

	PINMUX_IPSR_GPSR(IP0SR3_7_4,	MMC_SD_D0),

	PINMUX_IPSR_GPSR(IP0SR3_11_8,	MMC_SD_D2),

	PINMUX_IPSR_GPSR(IP0SR3_15_12,	MMC_SD_CLK),

	PINMUX_IPSR_GPSR(IP0SR3_19_16,	MMC_DS),

	PINMUX_IPSR_GPSR(IP0SR3_23_20,	MMC_SD_D3),

	PINMUX_IPSR_GPSR(IP0SR3_27_24,	MMC_D5),

	PINMUX_IPSR_GPSR(IP0SR3_31_28,	MMC_D4),

	/* IP1SR3 */
	PINMUX_IPSR_GPSR(IP1SR3_3_0,	MMC_D7),

	PINMUX_IPSR_GPSR(IP1SR3_7_4,	MMC_D6),

	PINMUX_IPSR_GPSR(IP1SR3_11_8,	MMC_SD_CMD),

	PINMUX_IPSR_GPSR(IP1SR3_15_12,	SD_CD),

	PINMUX_IPSR_GPSR(IP1SR3_19_16,	SD_WP),

	PINMUX_IPSR_GPSR(IP1SR3_23_20,	PWM1_A),

	PINMUX_IPSR_GPSR(IP1SR3_27_24,	PWM2_A),

	PINMUX_IPSR_GPSR(IP1SR3_31_28,	QSPI0_SSL),

	/* IP2SR3 */
	PINMUX_IPSR_GPSR(IP2SR3_3_0,	QSPI0_IO3),

	PINMUX_IPSR_GPSR(IP2SR3_7_4,	QSPI0_IO2),

	PINMUX_IPSR_GPSR(IP2SR3_11_8,	QSPI0_MISO_IO1),

	PINMUX_IPSR_GPSR(IP2SR3_15_12,	QSPI0_MOSI_IO0),

	PINMUX_IPSR_GPSR(IP2SR3_19_16,	QSPI0_SPCLK),

	PINMUX_IPSR_GPSR(IP2SR3_23_20,	QSPI1_MOSI_IO0),

	PINMUX_IPSR_GPSR(IP2SR3_27_24,	QSPI1_SPCLK),

	PINMUX_IPSR_GPSR(IP2SR3_31_28,	QSPI1_MISO_IO1),

	/* IP3SR3 */
	PINMUX_IPSR_GPSR(IP3SR3_3_0,	QSPI1_IO2),

	PINMUX_IPSR_GPSR(IP3SR3_7_4,	QSPI1_SSL),

	PINMUX_IPSR_GPSR(IP3SR3_11_8,	QSPI1_IO3),

	PINMUX_IPSR_GPSR(IP3SR3_15_12,	RPC_RESET_N),

	PINMUX_IPSR_GPSR(IP3SR3_19_16,	RPC_WP_N),

	PINMUX_IPSR_GPSR(IP3SR3_23_20,	RPC_INT_N),

	PINMUX_IPSR_GPSR(IP3SR3_27_24,	TCLK3_A),

	PINMUX_IPSR_GPSR(IP3SR3_31_28,	TCLK4_A),

	/* IP0SR4 */
	PINMUX_IPSR_MSEL(IP0SR4_3_0,	SCL0,			SEL_SCL0_0),

	PINMUX_IPSR_MSEL(IP0SR4_7_4,	SDA0,			SEL_SDA0_0),

	PINMUX_IPSR_MSEL(IP0SR4_11_8,	SCL1,			SEL_SCL1_0),

	PINMUX_IPSR_MSEL(IP0SR4_15_12,	SDA1,			SEL_SDA1_0),

	PINMUX_IPSR_MSEL(IP0SR4_19_16,	SCL2,			SEL_SCL2_0),

	PINMUX_IPSR_MSEL(IP0SR4_23_20,	SDA2,			SEL_SDA2_0),

	PINMUX_IPSR_MSEL(IP0SR4_27_24,	SCL3,			SEL_SCL3_0),

	PINMUX_IPSR_MSEL(IP0SR4_31_28,	SDA3,			SEL_SDA3_0),

	/* IP1SR4 */
	PINMUX_IPSR_GPSR(IP1SR4_3_0,	HRX2),
	PINMUX_IPSR_GPSR(IP1SR4_3_0,	SCK4),

	PINMUX_IPSR_GPSR(IP1SR4_7_4,	HTX2),
	PINMUX_IPSR_GPSR(IP1SR4_7_4,	CTS4_N),

	PINMUX_IPSR_GPSR(IP1SR4_11_8,	HRTS2_N),
	PINMUX_IPSR_GPSR(IP1SR4_11_8,	RTS4_N),

	PINMUX_IPSR_GPSR(IP1SR4_15_12,	SCIF_CLK2),

	PINMUX_IPSR_GPSR(IP1SR4_19_16,	HCTS2_N),
	PINMUX_IPSR_GPSR(IP1SR4_19_16,	TX4),

	PINMUX_IPSR_GPSR(IP1SR4_23_20,	HSCK2),
	PINMUX_IPSR_GPSR(IP1SR4_23_20,	RX4),

	PINMUX_IPSR_GPSR(IP1SR4_27_24,	PWM3_A),

	PINMUX_IPSR_GPSR(IP1SR4_31_28,	PWM4),

	/* IP2SR4 */
	PINMUX_IPSR_GPSR(IP2SR4_23_20,	PCIE0_CLKREQ_N),

	PINMUX_IPSR_GPSR(IP2SR4_31_28,	AVS0),

	/* IP3SR4 */
	PINMUX_IPSR_GPSR(IP3SR4_3_0,	AVS1),

	/* IP0SR5 */
	PINMUX_IPSR_GPSR(IP0SR5_3_0,	AVB2_AVTP_PPS),
	PINMUX_IPSR_GPSR(IP0SR5_3_0,	Ether_GPTP_PPS0),

	PINMUX_IPSR_GPSR(IP0SR5_7_4,	AVB2_AVTP_CAPTURE),
	PINMUX_IPSR_GPSR(IP0SR5_7_4,	Ether_GPTP_CAPTURE),

	PINMUX_IPSR_GPSR(IP0SR5_11_8,	AVB2_AVTP_MATCH),
	PINMUX_IPSR_GPSR(IP0SR5_11_8,	Ether_GPTP_MATCH),

	PINMUX_IPSR_GPSR(IP0SR5_15_12,	AVB2_LINK),

	PINMUX_IPSR_GPSR(IP0SR5_19_16,	AVB2_PHY_INT),

	PINMUX_IPSR_GPSR(IP0SR5_23_20,	AVB2_MAGIC),
	PINMUX_IPSR_GPSR(IP0SR5_23_20,	Ether_GPTP_PPS1),

	PINMUX_IPSR_GPSR(IP0SR5_27_24,	AVB2_MDC),

	PINMUX_IPSR_GPSR(IP0SR5_31_28,	AVB2_TXCREFCLK),

	/* IP1SR5 */
	PINMUX_IPSR_GPSR(IP1SR5_3_0,	AVB2_TD3),

	PINMUX_IPSR_GPSR(IP1SR5_7_4,	AVB2_RD3),

	PINMUX_IPSR_GPSR(IP1SR5_11_8,	AVB2_MDIO),

	PINMUX_IPSR_GPSR(IP1SR5_15_12,	AVB2_TD2),

	PINMUX_IPSR_GPSR(IP1SR5_19_16,	AVB2_TD1),

	PINMUX_IPSR_GPSR(IP1SR5_23_20,	AVB2_RD2),

	PINMUX_IPSR_GPSR(IP1SR5_27_24,	AVB2_RD1),

	PINMUX_IPSR_GPSR(IP1SR5_31_28,	AVB2_TD0),

	/* IP2SR5 */
	PINMUX_IPSR_GPSR(IP2SR5_3_0,	AVB2_TXC),

	PINMUX_IPSR_GPSR(IP2SR5_7_4,	AVB2_RD0),

	PINMUX_IPSR_GPSR(IP2SR5_11_8,	AVB2_RXC),

	PINMUX_IPSR_GPSR(IP2SR5_15_12,	AVB2_TX_CTL),

	PINMUX_IPSR_GPSR(IP2SR5_19_16,	AVB2_RX_CTL),

	/* IP0SR6 */
	PINMUX_IPSR_GPSR(IP0SR6_3_0,	AVB1_MDIO),

	PINMUX_IPSR_GPSR(IP0SR6_7_4,	AVB1_MAGIC),

	PINMUX_IPSR_GPSR(IP0SR6_11_8,	AVB1_MDC),

	PINMUX_IPSR_GPSR(IP0SR6_15_12,	AVB1_PHY_INT),

	PINMUX_IPSR_GPSR(IP0SR6_19_16,	AVB1_LINK),
	PINMUX_IPSR_GPSR(IP0SR6_19_16,	AVB1_MII_TX_ER),

	PINMUX_IPSR_GPSR(IP0SR6_23_20,	AVB1_AVTP_MATCH),
	PINMUX_IPSR_GPSR(IP0SR6_23_20,	AVB1_MII_RX_ER),

	PINMUX_IPSR_GPSR(IP0SR6_27_24,	AVB1_TXC),
	PINMUX_IPSR_GPSR(IP0SR6_27_24,	AVB1_MII_TXC),

	PINMUX_IPSR_GPSR(IP0SR6_31_28,	AVB1_TX_CTL),
	PINMUX_IPSR_GPSR(IP0SR6_31_28,	AVB1_MII_TX_EN),

	/* IP1SR6 */
	PINMUX_IPSR_GPSR(IP1SR6_3_0,	AVB1_RXC),
	PINMUX_IPSR_GPSR(IP1SR6_3_0,	AVB1_MII_RXC),

	PINMUX_IPSR_GPSR(IP1SR6_7_4,	AVB1_RX_CTL),
	PINMUX_IPSR_GPSR(IP1SR6_7_4,	AVB1_MII_RX_DV),

	PINMUX_IPSR_GPSR(IP1SR6_11_8,	AVB1_AVTP_PPS),
	PINMUX_IPSR_GPSR(IP1SR6_11_8,	AVB1_MII_COL),

	PINMUX_IPSR_GPSR(IP1SR6_15_12,	AVB1_AVTP_CAPTURE),
	PINMUX_IPSR_GPSR(IP1SR6_15_12,	AVB1_MII_CRS),

	PINMUX_IPSR_GPSR(IP1SR6_19_16,	AVB1_TD1),
	PINMUX_IPSR_GPSR(IP1SR6_19_16,	AVB1_MII_TD1),

	PINMUX_IPSR_GPSR(IP1SR6_23_20,	AVB1_TD0),
	PINMUX_IPSR_GPSR(IP1SR6_23_20,	AVB1_MII_TD0),

	PINMUX_IPSR_GPSR(IP1SR6_27_24,	AVB1_RD1),
	PINMUX_IPSR_GPSR(IP1SR6_27_24,	AVB1_MII_RD1),

	PINMUX_IPSR_GPSR(IP1SR6_31_28,	AVB1_RD0),
	PINMUX_IPSR_GPSR(IP1SR6_31_28,	AVB1_MII_RD0),

	/* IP2SR6 */
	PINMUX_IPSR_GPSR(IP2SR6_3_0,	AVB1_TD2),
	PINMUX_IPSR_GPSR(IP2SR6_3_0,	AVB1_MII_TD2),

	PINMUX_IPSR_GPSR(IP2SR6_7_4,	AVB1_RD2),
	PINMUX_IPSR_GPSR(IP2SR6_7_4,	AVB1_MII_RD2),

	PINMUX_IPSR_GPSR(IP2SR6_11_8,	AVB1_TD3),
	PINMUX_IPSR_GPSR(IP2SR6_11_8,	AVB1_MII_TD3),

	PINMUX_IPSR_GPSR(IP2SR6_15_12,	AVB1_RD3),
	PINMUX_IPSR_GPSR(IP2SR6_15_12,	AVB1_MII_RD3),

	PINMUX_IPSR_GPSR(IP2SR6_19_16,	AVB1_TXCREFCLK),

	/* IP0SR7 */
	PINMUX_IPSR_GPSR(IP0SR7_3_0,	AVB0_AVTP_PPS),
	PINMUX_IPSR_GPSR(IP0SR7_3_0,	AVB0_MII_COL),

	PINMUX_IPSR_GPSR(IP0SR7_7_4,	AVB0_AVTP_CAPTURE),
	PINMUX_IPSR_GPSR(IP0SR7_7_4,	AVB0_MII_CRS),

	PINMUX_IPSR_GPSR(IP0SR7_11_8,	AVB0_AVTP_MATCH),
	PINMUX_IPSR_GPSR(IP0SR7_11_8,	AVB0_MII_RX_ER),
	PINMUX_IPSR_GPSR(IP0SR7_11_8,	CC5_OSCOUT),

	PINMUX_IPSR_GPSR(IP0SR7_15_12,	AVB0_TD3),
	PINMUX_IPSR_GPSR(IP0SR7_15_12,	AVB0_MII_TD3),

	PINMUX_IPSR_GPSR(IP0SR7_19_16,	AVB0_LINK),
	PINMUX_IPSR_GPSR(IP0SR7_19_16,	AVB0_MII_TX_ER),

	PINMUX_IPSR_GPSR(IP0SR7_23_20,	AVB0_PHY_INT),

	PINMUX_IPSR_GPSR(IP0SR7_27_24,	AVB0_TD2),
	PINMUX_IPSR_GPSR(IP0SR7_27_24,	AVB0_MII_TD2),

	PINMUX_IPSR_GPSR(IP0SR7_31_28,	AVB0_TD1),
	PINMUX_IPSR_GPSR(IP0SR7_31_28,	AVB0_MII_TD1),

	/* IP1SR7 */
	PINMUX_IPSR_GPSR(IP1SR7_3_0,	AVB0_RD3),
	PINMUX_IPSR_GPSR(IP1SR7_3_0,	AVB0_MII_RD3),

	PINMUX_IPSR_GPSR(IP1SR7_7_4,	AVB0_TXCREFCLK),

	PINMUX_IPSR_GPSR(IP1SR7_11_8,	AVB0_MAGIC),

	PINMUX_IPSR_GPSR(IP1SR7_15_12,	AVB0_TD0),
	PINMUX_IPSR_GPSR(IP1SR7_15_12,	AVB0_MII_TD0),

	PINMUX_IPSR_GPSR(IP1SR7_19_16,	AVB0_RD2),
	PINMUX_IPSR_GPSR(IP1SR7_19_16,	AVB0_MII_RD2),

	PINMUX_IPSR_GPSR(IP1SR7_23_20,	AVB0_MDC),

	PINMUX_IPSR_GPSR(IP1SR7_27_24,	AVB0_MDIO),

	PINMUX_IPSR_GPSR(IP1SR7_31_28,	AVB0_TXC),
	PINMUX_IPSR_GPSR(IP1SR7_31_28,	AVB0_MII_TXC),

	/* IP2SR7 */
	PINMUX_IPSR_GPSR(IP2SR7_3_0,	AVB0_TX_CTL),
	PINMUX_IPSR_GPSR(IP2SR7_3_0,	AVB0_MII_TX_EN),

	PINMUX_IPSR_GPSR(IP2SR7_7_4,	AVB0_RD1),
	PINMUX_IPSR_GPSR(IP2SR7_7_4,	AVB0_MII_RD1),

	PINMUX_IPSR_GPSR(IP2SR7_11_8,	AVB0_RD0),
	PINMUX_IPSR_GPSR(IP2SR7_11_8,	AVB0_MII_RD0),

	PINMUX_IPSR_GPSR(IP2SR7_15_12,	AVB0_RXC),
	PINMUX_IPSR_GPSR(IP2SR7_15_12,	AVB0_MII_RXC),

	PINMUX_IPSR_GPSR(IP2SR7_19_16,	AVB0_RX_CTL),
	PINMUX_IPSR_GPSR(IP2SR7_19_16,	AVB0_MII_RX_DV),
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

/* - AVB0 ------------------------------------------------ */
static const unsigned int avb0_link_pins[] = {
	/* AVB0_LINK */
	RCAR_GP_PIN(7, 4),
};
static const unsigned int avb0_link_mux[] = {
	AVB0_LINK_MARK,
};
static const unsigned int avb0_magic_pins[] = {
	/* AVB0_MAGIC */
	RCAR_GP_PIN(7, 10),
};
static const unsigned int avb0_magic_mux[] = {
	AVB0_MAGIC_MARK,
};
static const unsigned int avb0_phy_int_pins[] = {
	/* AVB0_PHY_INT */
	RCAR_GP_PIN(7, 5),
};
static const unsigned int avb0_phy_int_mux[] = {
	AVB0_PHY_INT_MARK,
};
static const unsigned int avb0_mdio_pins[] = {
	/* AVB0_MDC, AVB0_MDIO */
	RCAR_GP_PIN(7, 13), RCAR_GP_PIN(7, 14),
};
static const unsigned int avb0_mdio_mux[] = {
	AVB0_MDC_MARK, AVB0_MDIO_MARK,
};
static const unsigned int avb0_rgmii_pins[] = {
	/*
	 * AVB0_TX_CTL, AVB0_TXC, AVB0_TD0, AVB0_TD1, AVB0_TD2, AVB0_TD3,
	 * AVB0_RX_CTL, AVB0_RXC, AVB0_RD0, AVB0_RD1, AVB0_RD2, AVB0_RD3,
	 */
	RCAR_GP_PIN(7, 16), RCAR_GP_PIN(7, 15),
	RCAR_GP_PIN(7, 11), RCAR_GP_PIN(7,  7),
	RCAR_GP_PIN(7,  6), RCAR_GP_PIN(7,  3),
	RCAR_GP_PIN(7, 20), RCAR_GP_PIN(7, 19),
	RCAR_GP_PIN(7, 18), RCAR_GP_PIN(7, 17),
	RCAR_GP_PIN(7, 12), RCAR_GP_PIN(7,  8),
};
static const unsigned int avb0_rgmii_mux[] = {
	AVB0_TX_CTL_MARK,	AVB0_TXC_MARK,
	AVB0_TD0_MARK,		AVB0_TD1_MARK,
	AVB0_TD2_MARK,		AVB0_TD3_MARK,
	AVB0_RX_CTL_MARK,	AVB0_RXC_MARK,
	AVB0_RD0_MARK,		AVB0_RD1_MARK,
	AVB0_RD2_MARK,		AVB0_RD3_MARK,
};
static const unsigned int avb0_txcrefclk_pins[] = {
	/* AVB0_TXCREFCLK */
	RCAR_GP_PIN(7, 9),
};
static const unsigned int avb0_txcrefclk_mux[] = {
	AVB0_TXCREFCLK_MARK,
};
static const unsigned int avb0_avtp_pps_pins[] = {
	/* AVB0_AVTP_PPS */
	RCAR_GP_PIN(7, 0),
};
static const unsigned int avb0_avtp_pps_mux[] = {
	AVB0_AVTP_PPS_MARK,
};
static const unsigned int avb0_avtp_capture_pins[] = {
	/* AVB0_AVTP_CAPTURE */
	RCAR_GP_PIN(7, 1),
};
static const unsigned int avb0_avtp_capture_mux[] = {
	AVB0_AVTP_CAPTURE_MARK,
};
static const unsigned int avb0_avtp_match_pins[] = {
	/* AVB0_AVTP_MATCH */
	RCAR_GP_PIN(7, 2),
};
static const unsigned int avb0_avtp_match_mux[] = {
	AVB0_AVTP_MATCH_MARK,
};

/* - AVB1 ------------------------------------------------ */
static const unsigned int avb1_link_pins[] = {
	/* AVB1_LINK */
	RCAR_GP_PIN(6, 4),
};
static const unsigned int avb1_link_mux[] = {
	AVB1_LINK_MARK,
};
static const unsigned int avb1_magic_pins[] = {
	/* AVB1_MAGIC */
	RCAR_GP_PIN(6, 1),
};
static const unsigned int avb1_magic_mux[] = {
	AVB1_MAGIC_MARK,
};
static const unsigned int avb1_phy_int_pins[] = {
	/* AVB1_PHY_INT */
	RCAR_GP_PIN(6, 3),
};
static const unsigned int avb1_phy_int_mux[] = {
	AVB1_PHY_INT_MARK,
};
static const unsigned int avb1_mdio_pins[] = {
	/* AVB1_MDC, AVB1_MDIO */
	RCAR_GP_PIN(6, 2), RCAR_GP_PIN(6, 0),
};
static const unsigned int avb1_mdio_mux[] = {
	AVB1_MDC_MARK, AVB1_MDIO_MARK,
};
static const unsigned int avb1_rgmii_pins[] = {
	/*
	 * AVB1_TX_CTL, AVB1_TXC, AVB1_TD0, AVB1_TD1, AVB1_TD2, AVB1_TD3,
	 * AVB1_RX_CTL, AVB1_RXC, AVB1_RD0, AVB1_RD1, AVB1_RD2, AVB1_RD3,
	 */
	RCAR_GP_PIN(6,  7), RCAR_GP_PIN(6,  6),
	RCAR_GP_PIN(6, 13), RCAR_GP_PIN(6, 12),
	RCAR_GP_PIN(6, 16), RCAR_GP_PIN(6, 18),
	RCAR_GP_PIN(6,  9), RCAR_GP_PIN(6,  8),
	RCAR_GP_PIN(6, 15), RCAR_GP_PIN(6, 14),
	RCAR_GP_PIN(6, 17), RCAR_GP_PIN(6, 19),
};
static const unsigned int avb1_rgmii_mux[] = {
	AVB1_TX_CTL_MARK,	AVB1_TXC_MARK,
	AVB1_TD0_MARK,		AVB1_TD1_MARK,
	AVB1_TD2_MARK,		AVB1_TD3_MARK,
	AVB1_RX_CTL_MARK,	AVB1_RXC_MARK,
	AVB1_RD0_MARK,		AVB1_RD1_MARK,
	AVB1_RD2_MARK,		AVB1_RD3_MARK,
};
static const unsigned int avb1_txcrefclk_pins[] = {
	/* AVB1_TXCREFCLK */
	RCAR_GP_PIN(6, 20),
};
static const unsigned int avb1_txcrefclk_mux[] = {
	AVB1_TXCREFCLK_MARK,
};
static const unsigned int avb1_avtp_pps_pins[] = {
	/* AVB1_AVTP_PPS */
	RCAR_GP_PIN(6, 10),
};
static const unsigned int avb1_avtp_pps_mux[] = {
	AVB1_AVTP_PPS_MARK,
};
static const unsigned int avb1_avtp_capture_pins[] = {
	/* AVB1_AVTP_CAPTURE */
	RCAR_GP_PIN(6, 11),
};
static const unsigned int avb1_avtp_capture_mux[] = {
	AVB1_AVTP_CAPTURE_MARK,
};
static const unsigned int avb1_avtp_match_pins[] = {
	/* AVB1_AVTP_MATCH */
	RCAR_GP_PIN(6, 5),
};
static const unsigned int avb1_avtp_match_mux[] = {
	AVB1_AVTP_MATCH_MARK,
};

/* - AVB2 ------------------------------------------------ */
static const unsigned int avb2_link_pins[] = {
	/* AVB2_LINK */
	RCAR_GP_PIN(5, 3),
};
static const unsigned int avb2_link_mux[] = {
	AVB2_LINK_MARK,
};
static const unsigned int avb2_magic_pins[] = {
	/* AVB2_MAGIC */
	RCAR_GP_PIN(5, 5),
};
static const unsigned int avb2_magic_mux[] = {
	AVB2_MAGIC_MARK,
};
static const unsigned int avb2_phy_int_pins[] = {
	/* AVB2_PHY_INT */
	RCAR_GP_PIN(5, 4),
};
static const unsigned int avb2_phy_int_mux[] = {
	AVB2_PHY_INT_MARK,
};
static const unsigned int avb2_mdio_pins[] = {
	/* AVB2_MDC, AVB2_MDIO */
	RCAR_GP_PIN(5, 6), RCAR_GP_PIN(5, 10),
};
static const unsigned int avb2_mdio_mux[] = {
	AVB2_MDC_MARK, AVB2_MDIO_MARK,
};
static const unsigned int avb2_rgmii_pins[] = {
	/*
	 * AVB2_TX_CTL, AVB2_TXC, AVB2_TD0, AVB2_TD1, AVB2_TD2, AVB2_TD3,
	 * AVB2_RX_CTL, AVB2_RXC, AVB2_RD0, AVB2_RD1, AVB2_RD2, AVB2_RD3,
	 */
	RCAR_GP_PIN(5, 19), RCAR_GP_PIN(5, 16),
	RCAR_GP_PIN(5, 15), RCAR_GP_PIN(5, 12),
	RCAR_GP_PIN(5, 11), RCAR_GP_PIN(5,  8),
	RCAR_GP_PIN(5, 20), RCAR_GP_PIN(5, 18),
	RCAR_GP_PIN(5, 17), RCAR_GP_PIN(5, 14),
	RCAR_GP_PIN(5, 13), RCAR_GP_PIN(5,  9),
};
static const unsigned int avb2_rgmii_mux[] = {
	AVB2_TX_CTL_MARK,	AVB2_TXC_MARK,
	AVB2_TD0_MARK,		AVB2_TD1_MARK,
	AVB2_TD2_MARK,		AVB2_TD3_MARK,
	AVB2_RX_CTL_MARK,	AVB2_RXC_MARK,
	AVB2_RD0_MARK,		AVB2_RD1_MARK,
	AVB2_RD2_MARK,		AVB2_RD3_MARK,
};
static const unsigned int avb2_txcrefclk_pins[] = {
	/* AVB2_TXCREFCLK */
	RCAR_GP_PIN(5, 7),
};
static const unsigned int avb2_txcrefclk_mux[] = {
	AVB2_TXCREFCLK_MARK,
};
static const unsigned int avb2_avtp_pps_pins[] = {
	/* AVB2_AVTP_PPS */
	RCAR_GP_PIN(5, 0),
};
static const unsigned int avb2_avtp_pps_mux[] = {
	AVB2_AVTP_PPS_MARK,
};
static const unsigned int avb2_avtp_capture_pins[] = {
	/* AVB2_AVTP_CAPTURE */
	RCAR_GP_PIN(5, 1),
};
static const unsigned int avb2_avtp_capture_mux[] = {
	AVB2_AVTP_CAPTURE_MARK,
};
static const unsigned int avb2_avtp_match_pins[] = {
	/* AVB2_AVTP_MATCH */
	RCAR_GP_PIN(5, 2),
};
static const unsigned int avb2_avtp_match_mux[] = {
	AVB2_AVTP_MATCH_MARK,
};

/* - MMC -------------------------------------------------------------------- */
static const unsigned int mmc_data_pins[] = {
	/* MMC_SD_D[0:3], MMC_D[4:7] */
	RCAR_GP_PIN(3, 1), RCAR_GP_PIN(3, 0),
	RCAR_GP_PIN(3, 2), RCAR_GP_PIN(3, 5),
	RCAR_GP_PIN(3, 7), RCAR_GP_PIN(3, 6),
	RCAR_GP_PIN(3, 9), RCAR_GP_PIN(3, 8),
};
static const unsigned int mmc_data_mux[] = {
	MMC_SD_D0_MARK, MMC_SD_D1_MARK,
	MMC_SD_D2_MARK, MMC_SD_D3_MARK,
	MMC_D4_MARK, MMC_D5_MARK,
	MMC_D6_MARK, MMC_D7_MARK,
};
static const unsigned int mmc_ctrl_pins[] = {
	/* MMC_SD_CLK, MMC_SD_CMD */
	RCAR_GP_PIN(3, 3), RCAR_GP_PIN(3, 10),
};
static const unsigned int mmc_ctrl_mux[] = {
	MMC_SD_CLK_MARK, MMC_SD_CMD_MARK,
};
static const unsigned int mmc_cd_pins[] = {
	/* SD_CD */
	RCAR_GP_PIN(3, 11),
};
static const unsigned int mmc_cd_mux[] = {
	SD_CD_MARK,
};
static const unsigned int mmc_wp_pins[] = {
	/* SD_WP */
	RCAR_GP_PIN(3, 12),
};
static const unsigned int mmc_wp_mux[] = {
	SD_WP_MARK,
};
static const unsigned int mmc_ds_pins[] = {
	/* MMC_DS */
	RCAR_GP_PIN(3, 4),
};
static const unsigned int mmc_ds_mux[] = {
	MMC_DS_MARK,
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

	SH_PFC_PIN_GROUP(avb1_link),
	SH_PFC_PIN_GROUP(avb1_magic),
	SH_PFC_PIN_GROUP(avb1_phy_int),
	SH_PFC_PIN_GROUP(avb1_mdio),
	SH_PFC_PIN_GROUP(avb1_rgmii),
	SH_PFC_PIN_GROUP(avb1_txcrefclk),
	SH_PFC_PIN_GROUP(avb1_avtp_pps),
	SH_PFC_PIN_GROUP(avb1_avtp_capture),
	SH_PFC_PIN_GROUP(avb1_avtp_match),

	SH_PFC_PIN_GROUP(avb2_link),
	SH_PFC_PIN_GROUP(avb2_magic),
	SH_PFC_PIN_GROUP(avb2_phy_int),
	SH_PFC_PIN_GROUP(avb2_mdio),
	SH_PFC_PIN_GROUP(avb2_rgmii),
	SH_PFC_PIN_GROUP(avb2_txcrefclk),
	SH_PFC_PIN_GROUP(avb2_avtp_pps),
	SH_PFC_PIN_GROUP(avb2_avtp_capture),
	SH_PFC_PIN_GROUP(avb2_avtp_match),

	BUS_DATA_PIN_GROUP(mmc_data, 1),
	BUS_DATA_PIN_GROUP(mmc_data, 4),
	BUS_DATA_PIN_GROUP(mmc_data, 8),
	SH_PFC_PIN_GROUP(mmc_ctrl),
	SH_PFC_PIN_GROUP(mmc_cd),
	SH_PFC_PIN_GROUP(mmc_wp),
	SH_PFC_PIN_GROUP(mmc_ds),
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

static const char * const avb1_groups[] = {
	"avb1_link",
	"avb1_magic",
	"avb1_phy_int",
	"avb1_mdio",
	"avb1_rgmii",
	"avb1_txcrefclk",
	"avb1_avtp_pps",
	"avb1_avtp_capture",
	"avb1_avtp_match",
};

static const char * const avb2_groups[] = {
	"avb2_link",
	"avb2_magic",
	"avb2_phy_int",
	"avb2_mdio",
	"avb2_rgmii",
	"avb2_txcrefclk",
	"avb2_avtp_pps",
	"avb2_avtp_capture",
	"avb2_avtp_match",
};

static const char * const mmc_groups[] = {
	"mmc_data1",
	"mmc_data4",
	"mmc_data8",
	"mmc_ctrl",
	"mmc_cd",
	"mmc_wp",
	"mmc_ds",
};

static const struct sh_pfc_function pinmux_functions[] = {
	SH_PFC_FUNCTION(avb0),
	SH_PFC_FUNCTION(avb1),
	SH_PFC_FUNCTION(avb2),

	SH_PFC_FUNCTION(mmc),
};

static const struct pinmux_cfg_reg pinmux_config_regs[] = {
#define F_(x, y)	FN_##y
#define FM(x)		FN_##x
	{ PINMUX_CFG_REG_VAR("GPSR0", 0xE6050040, 32,
			     GROUP(-13, 1, 1, 1, 1, 1, 1, 1, 1, 1,
				   1, 1, 1, 1, 1, 1, 1, 1, 1, 1),
			     GROUP(
		/* GP0_31_19 RESERVED */
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
	{ PINMUX_CFG_REG("GPSR1", 0xE6050840, 32, 1, GROUP(
		0, 0,
		0, 0,
		GP_1_29_FN,	GPSR1_29,
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
	{ PINMUX_CFG_REG_VAR("GPSR2", 0xE6058040, 32,
			     GROUP(-12, 1, -1, 1, -1, 1, 1, 1, 1, 1, 1,
				   1, 1, 1, 1, 1, 1, 1, 1, 1, 1),
			     GROUP(
		/* GP2_31_20 RESERVED */
		GP_2_19_FN,	GPSR2_19,
		/* GP2_18 RESERVED */
		GP_2_17_FN,	GPSR2_17,
		/* GP2_16 RESERVED */
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
	{ PINMUX_CFG_REG("GPSR3", 0xE6058840, 32, 1, GROUP(
		GP_3_31_FN,	GPSR3_31,
		GP_3_30_FN,	GPSR3_30,
		GP_3_29_FN,	GPSR3_29,
		GP_3_28_FN,	GPSR3_28,
		GP_3_27_FN,	GPSR3_27,
		GP_3_26_FN,	GPSR3_26,
		GP_3_25_FN,	GPSR3_25,
		GP_3_24_FN,	GPSR3_24,
		GP_3_23_FN,	GPSR3_23,
		GP_3_22_FN,	GPSR3_22,
		GP_3_21_FN,	GPSR3_21,
		GP_3_20_FN,	GPSR3_20,
		GP_3_19_FN,	GPSR3_19,
		GP_3_18_FN,	GPSR3_18,
		GP_3_17_FN,	GPSR3_17,
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
	{ PINMUX_CFG_REG_VAR("GPSR4", 0xE6060040, 32,
			     GROUP(-7, 1, 1, -1, 1, -5, 1, 1, 1, 1, 1,
				   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1),
			     GROUP(
		/* GP4_31_25 RESERVED */
		GP_4_24_FN,	GPSR4_24,
		GP_4_23_FN,	GPSR4_23,
		/* GP4_22 RESERVED */
		GP_4_21_FN,	GPSR4_21,
		/* GP4_20_16 RESERVED */
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
	{ PINMUX_CFG_REG_VAR("GPSR5", 0xE6060840, 32,
			     GROUP(-11, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
				   1, 1, 1, 1, 1, 1, 1, 1, 1, 1),
			     GROUP(
		/* GP5_31_21 RESERVED */
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
	{ PINMUX_CFG_REG_VAR("GPSR6", 0xE6061040, 32,
			     GROUP(-11, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
				   1, 1, 1, 1, 1, 1, 1, 1, 1, 1),
			     GROUP(
		/* GP6_31_21 RESERVED */
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
	{ PINMUX_CFG_REG_VAR("GPSR7", 0xE6061840, 32,
			     GROUP(-11, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
				   1, 1, 1, 1, 1, 1, 1, 1, 1, 1),
			     GROUP(
		/* GP7_31_21 RESERVED */
		GP_7_20_FN,	GPSR7_20,
		GP_7_19_FN,	GPSR7_19,
		GP_7_18_FN,	GPSR7_18,
		GP_7_17_FN,	GPSR7_17,
		GP_7_16_FN,	GPSR7_16,
		GP_7_15_FN,	GPSR7_15,
		GP_7_14_FN,	GPSR7_14,
		GP_7_13_FN,	GPSR7_13,
		GP_7_12_FN,	GPSR7_12,
		GP_7_11_FN,	GPSR7_11,
		GP_7_10_FN,	GPSR7_10,
		GP_7_9_FN,	GPSR7_9,
		GP_7_8_FN,	GPSR7_8,
		GP_7_7_FN,	GPSR7_7,
		GP_7_6_FN,	GPSR7_6,
		GP_7_5_FN,	GPSR7_5,
		GP_7_4_FN,	GPSR7_4,
		GP_7_3_FN,	GPSR7_3,
		GP_7_2_FN,	GPSR7_2,
		GP_7_1_FN,	GPSR7_1,
		GP_7_0_FN,	GPSR7_0, ))
	},
#undef F_
#undef FM

#define F_(x, y)	x,
#define FM(x)		FN_##x,
	{ PINMUX_CFG_REG("IP0SR0", 0xE6050060, 32, 4, GROUP(
		IP0SR0_31_28
		IP0SR0_27_24
		IP0SR0_23_20
		IP0SR0_19_16
		IP0SR0_15_12
		IP0SR0_11_8
		IP0SR0_7_4
		IP0SR0_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR0", 0xE6050064, 32, 4, GROUP(
		IP1SR0_31_28
		IP1SR0_27_24
		IP1SR0_23_20
		IP1SR0_19_16
		IP1SR0_15_12
		IP1SR0_11_8
		IP1SR0_7_4
		IP1SR0_3_0))
	},
	{ PINMUX_CFG_REG_VAR("IP2SR0", 0xE6050068, 32,
			     GROUP(-20, 4, 4, 4),
			     GROUP(
		/* IP2SR0_31_12 RESERVED */
		IP2SR0_11_8
		IP2SR0_7_4
		IP2SR0_3_0))
	},
	{ PINMUX_CFG_REG("IP0SR1", 0xE6050860, 32, 4, GROUP(
		IP0SR1_31_28
		IP0SR1_27_24
		IP0SR1_23_20
		IP0SR1_19_16
		IP0SR1_15_12
		IP0SR1_11_8
		IP0SR1_7_4
		IP0SR1_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR1", 0xE6050864, 32, 4, GROUP(
		IP1SR1_31_28
		IP1SR1_27_24
		IP1SR1_23_20
		IP1SR1_19_16
		IP1SR1_15_12
		IP1SR1_11_8
		IP1SR1_7_4
		IP1SR1_3_0))
	},
	{ PINMUX_CFG_REG("IP2SR1", 0xE6050868, 32, 4, GROUP(
		IP2SR1_31_28
		IP2SR1_27_24
		IP2SR1_23_20
		IP2SR1_19_16
		IP2SR1_15_12
		IP2SR1_11_8
		IP2SR1_7_4
		IP2SR1_3_0))
	},
	{ PINMUX_CFG_REG_VAR("IP3SR1", 0xE605086C, 32,
			     GROUP(-8, 4, 4, 4, 4, 4, 4),
			     GROUP(
		/* IP3SR1_31_24 RESERVED */
		IP3SR1_23_20
		IP3SR1_19_16
		IP3SR1_15_12
		IP3SR1_11_8
		IP3SR1_7_4
		IP3SR1_3_0))
	},
	{ PINMUX_CFG_REG("IP0SR2", 0xE6058060, 32, 4, GROUP(
		IP0SR2_31_28
		IP0SR2_27_24
		IP0SR2_23_20
		IP0SR2_19_16
		IP0SR2_15_12
		IP0SR2_11_8
		IP0SR2_7_4
		IP0SR2_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR2", 0xE6058064, 32, 4, GROUP(
		IP1SR2_31_28
		IP1SR2_27_24
		IP1SR2_23_20
		IP1SR2_19_16
		IP1SR2_15_12
		IP1SR2_11_8
		IP1SR2_7_4
		IP1SR2_3_0))
	},
	{ PINMUX_CFG_REG_VAR("IP2SR2", 0xE6058068, 32,
			     GROUP(-16, 4, -4, 4, -4),
			     GROUP(
		/* IP2SR2_31_16 RESERVED */
		IP2SR2_15_12
		/* IP2SR2_11_8 RESERVED */
		IP2SR2_7_4
		/* IP2SR2_3_0 RESERVED */))
	},
	{ PINMUX_CFG_REG("IP0SR3", 0xE6058860, 32, 4, GROUP(
		IP0SR3_31_28
		IP0SR3_27_24
		IP0SR3_23_20
		IP0SR3_19_16
		IP0SR3_15_12
		IP0SR3_11_8
		IP0SR3_7_4
		IP0SR3_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR3", 0xE6058864, 32, 4, GROUP(
		IP1SR3_31_28
		IP1SR3_27_24
		IP1SR3_23_20
		IP1SR3_19_16
		IP1SR3_15_12
		IP1SR3_11_8
		IP1SR3_7_4
		IP1SR3_3_0))
	},
	{ PINMUX_CFG_REG("IP2SR3", 0xE6058868, 32, 4, GROUP(
		IP2SR3_31_28
		IP2SR3_27_24
		IP2SR3_23_20
		IP2SR3_19_16
		IP2SR3_15_12
		IP2SR3_11_8
		IP2SR3_7_4
		IP2SR3_3_0))
	},
	{ PINMUX_CFG_REG("IP3SR3", 0xE605886C, 32, 4, GROUP(
		IP3SR3_31_28
		IP3SR3_27_24
		IP3SR3_23_20
		IP3SR3_19_16
		IP3SR3_15_12
		IP3SR3_11_8
		IP3SR3_7_4
		IP3SR3_3_0))
	},
	{ PINMUX_CFG_REG("IP0SR4", 0xE6060060, 32, 4, GROUP(
		IP0SR4_31_28
		IP0SR4_27_24
		IP0SR4_23_20
		IP0SR4_19_16
		IP0SR4_15_12
		IP0SR4_11_8
		IP0SR4_7_4
		IP0SR4_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR4", 0xE6060064, 32, 4, GROUP(
		IP1SR4_31_28
		IP1SR4_27_24
		IP1SR4_23_20
		IP1SR4_19_16
		IP1SR4_15_12
		IP1SR4_11_8
		IP1SR4_7_4
		IP1SR4_3_0))
	},
	{ PINMUX_CFG_REG_VAR("IP2SR4", 0xE6060068, 32,
			     GROUP(4, -4, 4, -20),
			     GROUP(
		IP2SR4_31_28
		/* IP2SR4_27_24 RESERVED */
		IP2SR4_23_20
		/* IP2SR4_19_0 RESERVED */))
	},
	{ PINMUX_CFG_REG_VAR("IP3SR4", 0xE606006C, 32,
			     GROUP(-28, 4),
			     GROUP(
		/* IP3SR4_31_4 RESERVED */
		IP3SR4_3_0))
	},
	{ PINMUX_CFG_REG("IP0SR5", 0xE6060860, 32, 4, GROUP(
		IP0SR5_31_28
		IP0SR5_27_24
		IP0SR5_23_20
		IP0SR5_19_16
		IP0SR5_15_12
		IP0SR5_11_8
		IP0SR5_7_4
		IP0SR5_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR5", 0xE6060864, 32, 4, GROUP(
		IP1SR5_31_28
		IP1SR5_27_24
		IP1SR5_23_20
		IP1SR5_19_16
		IP1SR5_15_12
		IP1SR5_11_8
		IP1SR5_7_4
		IP1SR5_3_0))
	},
	{ PINMUX_CFG_REG_VAR("IP2SR5", 0xE6060868, 32,
			     GROUP(-12, 4, 4, 4, 4, 4),
			     GROUP(
		/* IP2SR5_31_20 RESERVED */
		IP2SR5_19_16
		IP2SR5_15_12
		IP2SR5_11_8
		IP2SR5_7_4
		IP2SR5_3_0))
	},
	{ PINMUX_CFG_REG("IP0SR6", 0xE6061060, 32, 4, GROUP(
		IP0SR6_31_28
		IP0SR6_27_24
		IP0SR6_23_20
		IP0SR6_19_16
		IP0SR6_15_12
		IP0SR6_11_8
		IP0SR6_7_4
		IP0SR6_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR6", 0xE6061064, 32, 4, GROUP(
		IP1SR6_31_28
		IP1SR6_27_24
		IP1SR6_23_20
		IP1SR6_19_16
		IP1SR6_15_12
		IP1SR6_11_8
		IP1SR6_7_4
		IP1SR6_3_0))
	},
	{ PINMUX_CFG_REG_VAR("IP2SR6", 0xE6061068, 32,
			     GROUP(-12, 4, 4, 4, 4, 4),
			     GROUP(
		/* IP2SR6_31_20 RESERVED */
		IP2SR6_19_16
		IP2SR6_15_12
		IP2SR6_11_8
		IP2SR6_7_4
		IP2SR6_3_0))
	},
	{ PINMUX_CFG_REG("IP0SR7", 0xE6061860, 32, 4, GROUP(
		IP0SR7_31_28
		IP0SR7_27_24
		IP0SR7_23_20
		IP0SR7_19_16
		IP0SR7_15_12
		IP0SR7_11_8
		IP0SR7_7_4
		IP0SR7_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR7", 0xE6061864, 32, 4, GROUP(
		IP1SR7_31_28
		IP1SR7_27_24
		IP1SR7_23_20
		IP1SR7_19_16
		IP1SR7_15_12
		IP1SR7_11_8
		IP1SR7_7_4
		IP1SR7_3_0))
	},
	{ PINMUX_CFG_REG_VAR("IP2SR7", 0xE6061868, 32,
			     GROUP(-12, 4, 4, 4, 4, 4),
			     GROUP(
		/* IP2SR7_31_20 RESERVED */
		IP2SR7_19_16
		IP2SR7_15_12
		IP2SR7_11_8
		IP2SR7_7_4
		IP2SR7_3_0))
	},
#undef F_
#undef FM

#define F_(x, y)	x,
#define FM(x)		FN_##x,
	{ PINMUX_CFG_REG_VAR("MOD_SEL4", 0xE6060100, 32,
			     GROUP(-24, 1, 1, 1, 1, 1, 1, 1, 1),
			     GROUP(
		/* RESERVED 31-8 */
		MOD_SEL4_7
		MOD_SEL4_6
		MOD_SEL4_5
		MOD_SEL4_4
		MOD_SEL4_3
		MOD_SEL4_2
		MOD_SEL4_1
		MOD_SEL4_0))
	},
	{ },
};

static const struct pinmux_drive_reg pinmux_drive_regs[] = {
	{ PINMUX_DRIVE_REG("DRV0CTRL0", 0xE6050080) {
		{ RCAR_GP_PIN(0,  7), 28, 3 },	/* MSIOF5_SS2 */
		{ RCAR_GP_PIN(0,  6), 24, 3 },	/* IRQ0 */
		{ RCAR_GP_PIN(0,  5), 20, 3 },	/* IRQ1 */
		{ RCAR_GP_PIN(0,  4), 16, 3 },	/* IRQ2 */
		{ RCAR_GP_PIN(0,  3), 12, 3 },	/* IRQ3 */
		{ RCAR_GP_PIN(0,  2),  8, 3 },	/* GP0_02 */
		{ RCAR_GP_PIN(0,  1),  4, 3 },	/* GP0_01 */
		{ RCAR_GP_PIN(0,  0),  0, 3 },	/* GP0_00 */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL0", 0xE6050084) {
		{ RCAR_GP_PIN(0, 15), 28, 3 },	/* MSIOF2_SYNC */
		{ RCAR_GP_PIN(0, 14), 24, 3 },	/* MSIOF2_SS1 */
		{ RCAR_GP_PIN(0, 13), 20, 3 },	/* MSIOF2_SS2 */
		{ RCAR_GP_PIN(0, 12), 16, 3 },	/* MSIOF5_RXD */
		{ RCAR_GP_PIN(0, 11), 12, 3 },	/* MSIOF5_SCK */
		{ RCAR_GP_PIN(0, 10),  8, 3 },	/* MSIOF5_TXD */
		{ RCAR_GP_PIN(0,  9),  4, 3 },	/* MSIOF5_SYNC */
		{ RCAR_GP_PIN(0,  8),  0, 3 },	/* MSIOF5_SS1 */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL0", 0xE6050088) {
		{ RCAR_GP_PIN(0, 18),  8, 3 },	/* MSIOF2_RXD */
		{ RCAR_GP_PIN(0, 17),  4, 3 },	/* MSIOF2_SCK */
		{ RCAR_GP_PIN(0, 16),  0, 3 },	/* MSIOF2_TXD */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL1", 0xE6050880) {
		{ RCAR_GP_PIN(1,  7), 28, 3 },	/* MSIOF0_SS1 */
		{ RCAR_GP_PIN(1,  6), 24, 3 },	/* MSIOF0_SS2 */
		{ RCAR_GP_PIN(1,  5), 20, 3 },	/* MSIOF1_RXD */
		{ RCAR_GP_PIN(1,  4), 16, 3 },	/* MSIOF1_TXD */
		{ RCAR_GP_PIN(1,  3), 12, 3 },	/* MSIOF1_SCK */
		{ RCAR_GP_PIN(1,  2),  8, 3 },	/* MSIOF1_SYNC */
		{ RCAR_GP_PIN(1,  1),  4, 3 },	/* MSIOF1_SS1 */
		{ RCAR_GP_PIN(1,  0),  0, 3 },	/* MSIOF1_SS2 */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL1", 0xE6050884) {
		{ RCAR_GP_PIN(1, 15), 28, 3 },	/* HSCK0 */
		{ RCAR_GP_PIN(1, 14), 24, 3 },	/* HRTS0_N */
		{ RCAR_GP_PIN(1, 13), 20, 3 },	/* HCTS0_N */
		{ RCAR_GP_PIN(1, 12), 16, 3 },	/* HTX0 */
		{ RCAR_GP_PIN(1, 11), 12, 3 },	/* MSIOF0_RXD */
		{ RCAR_GP_PIN(1, 10),  8, 3 },	/* MSIOF0_SCK */
		{ RCAR_GP_PIN(1,  9),  4, 3 },	/* MSIOF0_TXD */
		{ RCAR_GP_PIN(1,  8),  0, 3 },	/* MSIOF0_SYNC */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL1", 0xE6050888) {
		{ RCAR_GP_PIN(1, 23), 28, 3 },	/* GP1_23 */
		{ RCAR_GP_PIN(1, 22), 24, 3 },	/* AUDIO_CLKIN */
		{ RCAR_GP_PIN(1, 21), 20, 3 },	/* AUDIO_CLKOUT */
		{ RCAR_GP_PIN(1, 20), 16, 3 },	/* SSI_SD */
		{ RCAR_GP_PIN(1, 19), 12, 3 },	/* SSI_WS */
		{ RCAR_GP_PIN(1, 18),  8, 3 },	/* SSI_SCK */
		{ RCAR_GP_PIN(1, 17),  4, 3 },	/* SCIF_CLK */
		{ RCAR_GP_PIN(1, 16),  0, 3 },	/* HRX0 */
	} },
	{ PINMUX_DRIVE_REG("DRV3CTRL1", 0xE605088C) {
		{ RCAR_GP_PIN(1, 29), 20, 2 },	/* ERROROUTC_N */
		{ RCAR_GP_PIN(1, 28), 16, 3 },	/* HTX3 */
		{ RCAR_GP_PIN(1, 27), 12, 3 },	/* HCTS3_N */
		{ RCAR_GP_PIN(1, 26),  8, 3 },	/* HRTS3_N */
		{ RCAR_GP_PIN(1, 25),  4, 3 },	/* HSCK3 */
		{ RCAR_GP_PIN(1, 24),  0, 3 },	/* HRX3 */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL2", 0xE6058080) {
		{ RCAR_GP_PIN(2,  7), 28, 3 },	/* TPU0TO1 */
		{ RCAR_GP_PIN(2,  6), 24, 3 },	/* FXR_TXDB */
		{ RCAR_GP_PIN(2,  5), 20, 3 },	/* FXR_TXENB_N */
		{ RCAR_GP_PIN(2,  4), 16, 3 },	/* RXDB_EXTFXR */
		{ RCAR_GP_PIN(2,  3), 12, 3 },	/* CLK_EXTFXR */
		{ RCAR_GP_PIN(2,  2),  8, 3 },	/* RXDA_EXTFXR */
		{ RCAR_GP_PIN(2,  1),  4, 3 },	/* FXR_TXENA_N */
		{ RCAR_GP_PIN(2,  0),  0, 3 },	/* FXR_TXDA */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL2", 0xE6058084) {
		{ RCAR_GP_PIN(2, 15), 28, 3 },	/* CANFD3_RX */
		{ RCAR_GP_PIN(2, 14), 24, 3 },	/* CANFD3_TX */
		{ RCAR_GP_PIN(2, 13), 20, 3 },	/* CANFD2_RX */
		{ RCAR_GP_PIN(2, 12), 16, 3 },	/* CANFD2_TX */
		{ RCAR_GP_PIN(2, 11), 12, 3 },	/* CANFD0_RX */
		{ RCAR_GP_PIN(2, 10),  8, 3 },	/* CANFD0_TX */
		{ RCAR_GP_PIN(2,  9),  4, 3 },	/* CAN_CLK */
		{ RCAR_GP_PIN(2,  8),  0, 3 },	/* TPU0TO0 */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL2", 0xE6058088) {
		{ RCAR_GP_PIN(2, 19), 12, 3 },	/* CANFD1_RX */
		{ RCAR_GP_PIN(2, 17),  4, 3 },	/* CANFD1_TX */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL3", 0xE6058880) {
		{ RCAR_GP_PIN(3,  7), 28, 3 },	/* MMC_D4 */
		{ RCAR_GP_PIN(3,  6), 24, 3 },	/* MMC_D5 */
		{ RCAR_GP_PIN(3,  5), 20, 3 },	/* MMC_SD_D3 */
		{ RCAR_GP_PIN(3,  4), 16, 3 },	/* MMC_DS */
		{ RCAR_GP_PIN(3,  3), 12, 3 },	/* MMC_SD_CLK */
		{ RCAR_GP_PIN(3,  2),  8, 3 },	/* MMC_SD_D2 */
		{ RCAR_GP_PIN(3,  1),  4, 3 },	/* MMC_SD_D0 */
		{ RCAR_GP_PIN(3,  0),  0, 3 },	/* MMC_SD_D1 */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL3", 0xE6058884) {
		{ RCAR_GP_PIN(3, 15), 28, 2 },	/* QSPI0_SSL */
		{ RCAR_GP_PIN(3, 14), 24, 2 },	/* PWM2 */
		{ RCAR_GP_PIN(3, 13), 20, 2 },	/* PWM1 */
		{ RCAR_GP_PIN(3, 12), 16, 3 },	/* SD_WP */
		{ RCAR_GP_PIN(3, 11), 12, 3 },	/* SD_CD */
		{ RCAR_GP_PIN(3, 10),  8, 3 },	/* MMC_SD_CMD */
		{ RCAR_GP_PIN(3,  9),  4, 3 },	/* MMC_D6*/
		{ RCAR_GP_PIN(3,  8),  0, 3 },	/* MMC_D7 */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL3", 0xE6058888) {
		{ RCAR_GP_PIN(3, 23), 28, 2 },	/* QSPI1_MISO_IO1 */
		{ RCAR_GP_PIN(3, 22), 24, 2 },	/* QSPI1_SPCLK */
		{ RCAR_GP_PIN(3, 21), 20, 2 },	/* QSPI1_MOSI_IO0 */
		{ RCAR_GP_PIN(3, 20), 16, 2 },	/* QSPI0_SPCLK */
		{ RCAR_GP_PIN(3, 19), 12, 2 },	/* QSPI0_MOSI_IO0 */
		{ RCAR_GP_PIN(3, 18),  8, 2 },	/* QSPI0_MISO_IO1 */
		{ RCAR_GP_PIN(3, 17),  4, 2 },	/* QSPI0_IO2 */
		{ RCAR_GP_PIN(3, 16),  0, 2 },	/* QSPI0_IO3 */
	} },
	{ PINMUX_DRIVE_REG("DRV3CTRL3", 0xE605888C) {
		{ RCAR_GP_PIN(3, 31), 28, 2 },	/* TCLK4 */
		{ RCAR_GP_PIN(3, 30), 24, 2 },	/* TCLK3 */
		{ RCAR_GP_PIN(3, 29), 20, 2 },	/* RPC_INT_N */
		{ RCAR_GP_PIN(3, 28), 16, 2 },	/* RPC_WP_N */
		{ RCAR_GP_PIN(3, 27), 12, 2 },	/* RPC_RESET_N */
		{ RCAR_GP_PIN(3, 26),  8, 2 },	/* QSPI1_IO3 */
		{ RCAR_GP_PIN(3, 25),  4, 2 },	/* QSPI1_SSL */
		{ RCAR_GP_PIN(3, 24),  0, 2 },	/* QSPI1_IO2 */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL4", 0xE6060080) {
		{ RCAR_GP_PIN(4,  7), 28, 3 },	/* SDA3 */
		{ RCAR_GP_PIN(4,  6), 24, 3 },	/* SCL3 */
		{ RCAR_GP_PIN(4,  5), 20, 3 },	/* SDA2 */
		{ RCAR_GP_PIN(4,  4), 16, 3 },	/* SCL2 */
		{ RCAR_GP_PIN(4,  3), 12, 3 },	/* SDA1 */
		{ RCAR_GP_PIN(4,  2),  8, 3 },	/* SCL1 */
		{ RCAR_GP_PIN(4,  1),  4, 3 },	/* SDA0 */
		{ RCAR_GP_PIN(4,  0),  0, 3 },	/* SCL0 */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL4", 0xE6060084) {
		{ RCAR_GP_PIN(4, 15), 28, 3 },	/* PWM4 */
		{ RCAR_GP_PIN(4, 14), 24, 3 },	/* PWM3 */
		{ RCAR_GP_PIN(4, 13), 20, 3 },	/* HSCK2 */
		{ RCAR_GP_PIN(4, 12), 16, 3 },	/* HCTS2_N */
		{ RCAR_GP_PIN(4, 11), 12, 3 },	/* SCIF_CLK2 */
		{ RCAR_GP_PIN(4, 10),  8, 3 },	/* HRTS2_N */
		{ RCAR_GP_PIN(4,  9),  4, 3 },	/* HTX2 */
		{ RCAR_GP_PIN(4,  8),  0, 3 },	/* HRX2 */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL4", 0xE6060088) {
		{ RCAR_GP_PIN(4, 23), 28, 3 },	/* AVS0 */
		{ RCAR_GP_PIN(4, 21), 20, 3 },	/* PCIE0_CLKREQ_N */
	} },
	{ PINMUX_DRIVE_REG("DRV3CTRL4", 0xE606008C) {
		{ RCAR_GP_PIN(4, 24),  0, 3 },	/* AVS1 */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL5", 0xE6060880) {
		{ RCAR_GP_PIN(5,  7), 28, 3 },	/* AVB2_TXCREFCLK */
		{ RCAR_GP_PIN(5,  6), 24, 3 },	/* AVB2_MDC */
		{ RCAR_GP_PIN(5,  5), 20, 3 },	/* AVB2_MAGIC */
		{ RCAR_GP_PIN(5,  4), 16, 3 },	/* AVB2_PHY_INT */
		{ RCAR_GP_PIN(5,  3), 12, 3 },	/* AVB2_LINK */
		{ RCAR_GP_PIN(5,  2),  8, 3 },	/* AVB2_AVTP_MATCH */
		{ RCAR_GP_PIN(5,  1),  4, 3 },	/* AVB2_AVTP_CAPTURE */
		{ RCAR_GP_PIN(5,  0),  0, 3 },	/* AVB2_AVTP_PPS */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL5", 0xE6060884) {
		{ RCAR_GP_PIN(5, 15), 28, 3 },	/* AVB2_TD0 */
		{ RCAR_GP_PIN(5, 14), 24, 3 },	/* AVB2_RD1 */
		{ RCAR_GP_PIN(5, 13), 20, 3 },	/* AVB2_RD2 */
		{ RCAR_GP_PIN(5, 12), 16, 3 },	/* AVB2_TD1 */
		{ RCAR_GP_PIN(5, 11), 12, 3 },	/* AVB2_TD2 */
		{ RCAR_GP_PIN(5, 10),  8, 3 },	/* AVB2_MDIO */
		{ RCAR_GP_PIN(5,  9),  4, 3 },	/* AVB2_RD3 */
		{ RCAR_GP_PIN(5,  8),  0, 3 },	/* AVB2_TD3 */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL5", 0xE6060888) {
		{ RCAR_GP_PIN(5, 20), 16, 3 },	/* AVB2_RX_CTL */
		{ RCAR_GP_PIN(5, 19), 12, 3 },	/* AVB2_TX_CTL */
		{ RCAR_GP_PIN(5, 18),  8, 3 },	/* AVB2_RXC */
		{ RCAR_GP_PIN(5, 17),  4, 3 },	/* AVB2_RD0 */
		{ RCAR_GP_PIN(5, 16),  0, 3 },	/* AVB2_TXC */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL6", 0xE6061080) {
		{ RCAR_GP_PIN(6,  7), 28, 3 },	/* AVB1_TX_CTL */
		{ RCAR_GP_PIN(6,  6), 24, 3 },	/* AVB1_TXC */
		{ RCAR_GP_PIN(6,  5), 20, 3 },	/* AVB1_AVTP_MATCH */
		{ RCAR_GP_PIN(6,  4), 16, 3 },	/* AVB1_LINK */
		{ RCAR_GP_PIN(6,  3), 12, 3 },	/* AVB1_PHY_INT */
		{ RCAR_GP_PIN(6,  2),  8, 3 },	/* AVB1_MDC */
		{ RCAR_GP_PIN(6,  1),  4, 3 },	/* AVB1_MAGIC */
		{ RCAR_GP_PIN(6,  0),  0, 3 },	/* AVB1_MDIO */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL6", 0xE6061084) {
		{ RCAR_GP_PIN(6, 15), 28, 3 },	/* AVB1_RD0 */
		{ RCAR_GP_PIN(6, 14), 24, 3 },	/* AVB1_RD1 */
		{ RCAR_GP_PIN(6, 13), 20, 3 },	/* AVB1_TD0 */
		{ RCAR_GP_PIN(6, 12), 16, 3 },	/* AVB1_TD1 */
		{ RCAR_GP_PIN(6, 11), 12, 3 },	/* AVB1_AVTP_CAPTURE */
		{ RCAR_GP_PIN(6, 10),  8, 3 },	/* AVB1_AVTP_PPS */
		{ RCAR_GP_PIN(6,  9),  4, 3 },	/* AVB1_RX_CTL */
		{ RCAR_GP_PIN(6,  8),  0, 3 },	/* AVB1_RXC */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL6", 0xE6061088) {
		{ RCAR_GP_PIN(6, 20), 16, 3 },	/* AVB1_TXCREFCLK */
		{ RCAR_GP_PIN(6, 19), 12, 3 },	/* AVB1_RD3 */
		{ RCAR_GP_PIN(6, 18),  8, 3 },	/* AVB1_TD3 */
		{ RCAR_GP_PIN(6, 17),  4, 3 },	/* AVB1_RD2 */
		{ RCAR_GP_PIN(6, 16),  0, 3 },	/* AVB1_TD2 */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL7", 0xE6061880) {
		{ RCAR_GP_PIN(7,  7), 28, 3 },	/* AVB0_TD1 */
		{ RCAR_GP_PIN(7,  6), 24, 3 },	/* AVB0_TD2 */
		{ RCAR_GP_PIN(7,  5), 20, 3 },	/* AVB0_PHY_INT */
		{ RCAR_GP_PIN(7,  4), 16, 3 },	/* AVB0_LINK */
		{ RCAR_GP_PIN(7,  3), 12, 3 },	/* AVB0_TD3 */
		{ RCAR_GP_PIN(7,  2),  8, 3 },	/* AVB0_AVTP_MATCH */
		{ RCAR_GP_PIN(7,  1),  4, 3 },	/* AVB0_AVTP_CAPTURE */
		{ RCAR_GP_PIN(7,  0),  0, 3 },	/* AVB0_AVTP_PPS */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL7", 0xE6061884) {
		{ RCAR_GP_PIN(7, 15), 28, 3 },	/* AVB0_TXC */
		{ RCAR_GP_PIN(7, 14), 24, 3 },	/* AVB0_MDIO */
		{ RCAR_GP_PIN(7, 13), 20, 3 },	/* AVB0_MDC */
		{ RCAR_GP_PIN(7, 12), 16, 3 },	/* AVB0_RD2 */
		{ RCAR_GP_PIN(7, 11), 12, 3 },	/* AVB0_TD0 */
		{ RCAR_GP_PIN(7, 10),  8, 3 },	/* AVB0_MAGIC */
		{ RCAR_GP_PIN(7,  9),  4, 3 },	/* AVB0_TXCREFCLK */
		{ RCAR_GP_PIN(7,  8),  0, 3 },	/* AVB0_RD3 */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL7", 0xE6061888) {
		{ RCAR_GP_PIN(7, 20), 16, 3 },	/* AVB0_RX_CTL */
		{ RCAR_GP_PIN(7, 19), 12, 3 },	/* AVB0_RXC */
		{ RCAR_GP_PIN(7, 18),  8, 3 },	/* AVB0_RD0 */
		{ RCAR_GP_PIN(7, 17),  4, 3 },	/* AVB0_RD1 */
		{ RCAR_GP_PIN(7, 16),  0, 3 },	/* AVB0_TX_CTL */
	} },
	{ },
};

enum ioctrl_regs {
	POC0,
	POC1,
	POC3,
	POC4,
	POC5,
	POC6,
	POC7,
};

static const struct pinmux_ioctrl_reg pinmux_ioctrl_regs[] = {
	[POC0]		= { 0xE60500A0, },
	[POC1]		= { 0xE60508A0, },
	[POC3]		= { 0xE60588A0, },
	[POC4]		= { 0xE60600A0, },
	[POC5]		= { 0xE60608A0, },
	[POC6]		= { 0xE60610A0, },
	[POC7]		= { 0xE60618A0, },
	{ /* sentinel */ },
};

static int r8a779h0_pin_to_pocctrl(unsigned int pin, u32 *pocctrl)
{
	int bit = pin & 0x1f;

	switch (pin) {
	case RCAR_GP_PIN(0, 0) ... RCAR_GP_PIN(0, 18):
		*pocctrl = pinmux_ioctrl_regs[POC0].reg;
		return bit;

	case RCAR_GP_PIN(1, 0) ... RCAR_GP_PIN(1, 28):
		*pocctrl = pinmux_ioctrl_regs[POC1].reg;
		return bit;

	case RCAR_GP_PIN(3, 0) ... RCAR_GP_PIN(3, 12):
		*pocctrl = pinmux_ioctrl_regs[POC3].reg;
		return bit;

	case RCAR_GP_PIN(4, 0) ... RCAR_GP_PIN(4, 13):
		*pocctrl = pinmux_ioctrl_regs[POC4].reg;
		return bit;

	case PIN_VDDQ_AVB2:
		*pocctrl = pinmux_ioctrl_regs[POC5].reg;
		return 0;

	case PIN_VDDQ_AVB1:
		*pocctrl = pinmux_ioctrl_regs[POC6].reg;
		return 0;

	case PIN_VDDQ_AVB0:
		*pocctrl = pinmux_ioctrl_regs[POC7].reg;
		return 0;

	default:
		return -EINVAL;
	}
}

static const struct pinmux_bias_reg pinmux_bias_regs[] = {
	{ PINMUX_BIAS_REG("PUEN0", 0xE60500C0, "PUD0", 0xE60500E0) {
		[ 0] = RCAR_GP_PIN(0,  0),	/* GP0_00 */
		[ 1] = RCAR_GP_PIN(0,  1),	/* GP0_01 */
		[ 2] = RCAR_GP_PIN(0,  2),	/* GP0_02 */
		[ 3] = RCAR_GP_PIN(0,  3),	/* IRQ3 */
		[ 4] = RCAR_GP_PIN(0,  4),	/* IRQ2 */
		[ 5] = RCAR_GP_PIN(0,  5),	/* IRQ1 */
		[ 6] = RCAR_GP_PIN(0,  6),	/* IRQ0 */
		[ 7] = RCAR_GP_PIN(0,  7),	/* MSIOF5_SS2 */
		[ 8] = RCAR_GP_PIN(0,  8),	/* MSIOF5_SS1 */
		[ 9] = RCAR_GP_PIN(0,  9),	/* MSIOF5_SYNC */
		[10] = RCAR_GP_PIN(0, 10),	/* MSIOF5_TXD */
		[11] = RCAR_GP_PIN(0, 11),	/* MSIOF5_SCK */
		[12] = RCAR_GP_PIN(0, 12),	/* MSIOF5_RXD */
		[13] = RCAR_GP_PIN(0, 13),	/* MSIOF2_SS2 */
		[14] = RCAR_GP_PIN(0, 14),	/* MSIOF2_SS1 */
		[15] = RCAR_GP_PIN(0, 15),	/* MSIOF2_SYNC */
		[16] = RCAR_GP_PIN(0, 16),	/* MSIOF2_TXD */
		[17] = RCAR_GP_PIN(0, 17),	/* MSIOF2_SCK */
		[18] = RCAR_GP_PIN(0, 18),	/* MSIOF2_RXD */
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
	{ PINMUX_BIAS_REG("PUEN1", 0xE60508C0, "PUD1", 0xE60508E0) {
		[ 0] = RCAR_GP_PIN(1,  0),	/* MSIOF1_SS2 */
		[ 1] = RCAR_GP_PIN(1,  1),	/* MSIOF1_SS1 */
		[ 2] = RCAR_GP_PIN(1,  2),	/* MSIOF1_SYNC */
		[ 3] = RCAR_GP_PIN(1,  3),	/* MSIOF1_SCK */
		[ 4] = RCAR_GP_PIN(1,  4),	/* MSIOF1_TXD */
		[ 5] = RCAR_GP_PIN(1,  5),	/* MSIOF1_RXD */
		[ 6] = RCAR_GP_PIN(1,  6),	/* MSIOF0_SS2 */
		[ 7] = RCAR_GP_PIN(1,  7),	/* MSIOF0_SS1 */
		[ 8] = RCAR_GP_PIN(1,  8),	/* MSIOF0_SYNC */
		[ 9] = RCAR_GP_PIN(1,  9),	/* MSIOF0_TXD */
		[10] = RCAR_GP_PIN(1, 10),	/* MSIOF0_SCK */
		[11] = RCAR_GP_PIN(1, 11),	/* MSIOF0_RXD */
		[12] = RCAR_GP_PIN(1, 12),	/* HTX0 */
		[13] = RCAR_GP_PIN(1, 13),	/* HCTS0_N */
		[14] = RCAR_GP_PIN(1, 14),	/* HRTS0_N */
		[15] = RCAR_GP_PIN(1, 15),	/* HSCK0 */
		[16] = RCAR_GP_PIN(1, 16),	/* HRX0 */
		[17] = RCAR_GP_PIN(1, 17),	/* SCIF_CLK */
		[18] = RCAR_GP_PIN(1, 18),	/* SSI_SCK */
		[19] = RCAR_GP_PIN(1, 19),	/* SSI_WS */
		[20] = RCAR_GP_PIN(1, 20),	/* SSI_SD */
		[21] = RCAR_GP_PIN(1, 21),	/* AUDIO_CLKOUT */
		[22] = RCAR_GP_PIN(1, 22),	/* AUDIO_CLKIN */
		[23] = RCAR_GP_PIN(1, 23),	/* GP1_23 */
		[24] = RCAR_GP_PIN(1, 24),	/* HRX3 */
		[25] = RCAR_GP_PIN(1, 25),	/* HSCK3 */
		[26] = RCAR_GP_PIN(1, 26),	/* HRTS3_N */
		[27] = RCAR_GP_PIN(1, 27),	/* HCTS3_N */
		[28] = RCAR_GP_PIN(1, 28),	/* HTX3 */
		[29] = RCAR_GP_PIN(1, 29),	/* ERROROUTC_N */
		[30] = SH_PFC_PIN_NONE,
		[31] = SH_PFC_PIN_NONE,
	} },
	{ PINMUX_BIAS_REG("PUEN2", 0xE60580C0, "PUD2", 0xE60580E0) {
		[ 0] = RCAR_GP_PIN(2,  0),	/* FXR_TXDA */
		[ 1] = RCAR_GP_PIN(2,  1),	/* FXR_TXENA_N */
		[ 2] = RCAR_GP_PIN(2,  2),	/* RXDA_EXTFXR */
		[ 3] = RCAR_GP_PIN(2,  3),	/* CLK_EXTFXR */
		[ 4] = RCAR_GP_PIN(2,  4),	/* RXDB_EXTFXR */
		[ 5] = RCAR_GP_PIN(2,  5),	/* FXR_TXENB_N */
		[ 6] = RCAR_GP_PIN(2,  6),	/* FXR_TXDB */
		[ 7] = RCAR_GP_PIN(2,  7),	/* TPU0TO1 */
		[ 8] = RCAR_GP_PIN(2,  8),	/* TPU0TO0 */
		[ 9] = RCAR_GP_PIN(2,  9),	/* CAN_CLK */
		[10] = RCAR_GP_PIN(2, 10),	/* CANFD0_TX */
		[11] = RCAR_GP_PIN(2, 11),	/* CANFD0_RX */
		[12] = RCAR_GP_PIN(2, 12),	/* CANFD2_TX */
		[13] = RCAR_GP_PIN(2, 13),	/* CANFD2_RX */
		[14] = RCAR_GP_PIN(2, 14),	/* CANFD3_TX */
		[15] = RCAR_GP_PIN(2, 15),	/* CANFD3_RX */
		[16] = SH_PFC_PIN_NONE,
		[17] = RCAR_GP_PIN(2, 17),	/* CANFD1_TX */
		[18] = SH_PFC_PIN_NONE,
		[19] = RCAR_GP_PIN(2, 19),	/* CANFD1_RX */
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
	{ PINMUX_BIAS_REG("PUEN3", 0xE60588C0, "PUD3", 0xE60588E0) {
		[ 0] = RCAR_GP_PIN(3,  0),	/* MMC_SD_D1 */
		[ 1] = RCAR_GP_PIN(3,  1),	/* MMC_SD_D0 */
		[ 2] = RCAR_GP_PIN(3,  2),	/* MMC_SD_D2 */
		[ 3] = RCAR_GP_PIN(3,  3),	/* MMC_SD_CLK */
		[ 4] = RCAR_GP_PIN(3,  4),	/* MMC_DS */
		[ 5] = RCAR_GP_PIN(3,  5),	/* MMC_SD_D3 */
		[ 6] = RCAR_GP_PIN(3,  6),	/* MMC_D5 */
		[ 7] = RCAR_GP_PIN(3,  7),	/* MMC_D4 */
		[ 8] = RCAR_GP_PIN(3,  8),	/* MMC_D7 */
		[ 9] = RCAR_GP_PIN(3,  9),	/* MMC_D6 */
		[10] = RCAR_GP_PIN(3, 10),	/* MMC_SD_CMD */
		[11] = RCAR_GP_PIN(3, 11),	/* SD_CD */
		[12] = RCAR_GP_PIN(3, 12),	/* SD_WP */
		[13] = RCAR_GP_PIN(3, 13),	/* PWM1 */
		[14] = RCAR_GP_PIN(3, 14),	/* PWM2 */
		[15] = RCAR_GP_PIN(3, 15),	/* QSPI0_SSL */
		[16] = RCAR_GP_PIN(3, 16),	/* QSPI0_IO3 */
		[17] = RCAR_GP_PIN(3, 17),	/* QSPI0_IO2 */
		[18] = RCAR_GP_PIN(3, 18),	/* QSPI0_MISO_IO1 */
		[19] = RCAR_GP_PIN(3, 19),	/* QSPI0_MOSI_IO0 */
		[20] = RCAR_GP_PIN(3, 20),	/* QSPI0_SPCLK */
		[21] = RCAR_GP_PIN(3, 21),	/* QSPI1_MOSI_IO0 */
		[22] = RCAR_GP_PIN(3, 22),	/* QSPI1_SPCLK */
		[23] = RCAR_GP_PIN(3, 23),	/* QSPI1_MISO_IO1 */
		[24] = RCAR_GP_PIN(3, 24),	/* QSPI1_IO2 */
		[25] = RCAR_GP_PIN(3, 25),	/* QSPI1_SSL */
		[26] = RCAR_GP_PIN(3, 26),	/* QSPI1_IO3 */
		[27] = RCAR_GP_PIN(3, 27),	/* RPC_RESET_N */
		[28] = RCAR_GP_PIN(3, 28),	/* RPC_WP_N */
		[29] = RCAR_GP_PIN(3, 29),	/* RPC_INT_N */
		[30] = RCAR_GP_PIN(3, 30),	/* TCLK3 */
		[31] = RCAR_GP_PIN(3, 31),	/* TCLK4 */
	} },
	{ PINMUX_BIAS_REG("PUEN4", 0xE60600C0, "PUD4", 0xE60600E0) {
		[ 0] = RCAR_GP_PIN(4,  0),	/* SCL0 */
		[ 1] = RCAR_GP_PIN(4,  1),	/* SDA0 */
		[ 2] = RCAR_GP_PIN(4,  2),	/* SCL1 */
		[ 3] = RCAR_GP_PIN(4,  3),	/* SDA1 */
		[ 4] = RCAR_GP_PIN(4,  4),	/* SCL2 */
		[ 5] = RCAR_GP_PIN(4,  5),	/* SDA2 */
		[ 6] = RCAR_GP_PIN(4,  6),	/* SCL3 */
		[ 7] = RCAR_GP_PIN(4,  7),	/* SDA3 */
		[ 8] = RCAR_GP_PIN(4,  8),	/* HRX2 */
		[ 9] = RCAR_GP_PIN(4,  9),	/* HTX2 */
		[10] = RCAR_GP_PIN(4, 10),	/* HRTS2_N */
		[11] = RCAR_GP_PIN(4, 11),	/* SCIF_CLK2 */
		[12] = RCAR_GP_PIN(4, 12),	/* HCTS2_N */
		[13] = RCAR_GP_PIN(4, 13),	/* HSCK2 */
		[14] = RCAR_GP_PIN(4, 14),	/* PWM3 */
		[15] = RCAR_GP_PIN(4, 15),	/* PWM4 */
		[16] = SH_PFC_PIN_NONE,
		[17] = SH_PFC_PIN_NONE,
		[18] = SH_PFC_PIN_NONE,
		[19] = SH_PFC_PIN_NONE,
		[20] = SH_PFC_PIN_NONE,
		[21] = RCAR_GP_PIN(4, 21),	/* PCIE0_CLKREQ_N */
		[22] = SH_PFC_PIN_NONE,
		[23] = RCAR_GP_PIN(4, 23),	/* AVS0 */
		[24] = RCAR_GP_PIN(4, 24),	/* AVS1 */
		[25] = SH_PFC_PIN_NONE,
		[26] = SH_PFC_PIN_NONE,
		[27] = SH_PFC_PIN_NONE,
		[28] = SH_PFC_PIN_NONE,
		[29] = SH_PFC_PIN_NONE,
		[30] = SH_PFC_PIN_NONE,
		[31] = SH_PFC_PIN_NONE,
	} },
	{ PINMUX_BIAS_REG("PUEN5", 0xE60608C0, "PUD5", 0xE60608E0) {
		[ 0] = RCAR_GP_PIN(5,  0),	/* AVB2_AVTP_PPS */
		[ 1] = RCAR_GP_PIN(5,  1),	/* AVB0_AVTP_CAPTURE */
		[ 2] = RCAR_GP_PIN(5,  2),	/* AVB2_AVTP_MATCH */
		[ 3] = RCAR_GP_PIN(5,  3),	/* AVB2_LINK */
		[ 4] = RCAR_GP_PIN(5,  4),	/* AVB2_PHY_INT */
		[ 5] = RCAR_GP_PIN(5,  5),	/* AVB2_MAGIC */
		[ 6] = RCAR_GP_PIN(5,  6),	/* AVB2_MDC */
		[ 7] = RCAR_GP_PIN(5,  7),	/* AVB2_TXCREFCLK */
		[ 8] = RCAR_GP_PIN(5,  8),	/* AVB2_TD3 */
		[ 9] = RCAR_GP_PIN(5,  9),	/* AVB2_RD3 */
		[10] = RCAR_GP_PIN(5, 10),	/* AVB2_MDIO */
		[11] = RCAR_GP_PIN(5, 11),	/* AVB2_TD2 */
		[12] = RCAR_GP_PIN(5, 12),	/* AVB2_TD1 */
		[13] = RCAR_GP_PIN(5, 13),	/* AVB2_RD2 */
		[14] = RCAR_GP_PIN(5, 14),	/* AVB2_RD1 */
		[15] = RCAR_GP_PIN(5, 15),	/* AVB2_TD0 */
		[16] = RCAR_GP_PIN(5, 16),	/* AVB2_TXC */
		[17] = RCAR_GP_PIN(5, 17),	/* AVB2_RD0 */
		[18] = RCAR_GP_PIN(5, 18),	/* AVB2_RXC */
		[19] = RCAR_GP_PIN(5, 19),	/* AVB2_TX_CTL */
		[20] = RCAR_GP_PIN(5, 20),	/* AVB2_RX_CTL */
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
	{ PINMUX_BIAS_REG("PUEN6", 0xE60610C0, "PUD6", 0xE60610E0) {
		[ 0] = RCAR_GP_PIN(6,  0),	/* AVB1_MDIO */
		[ 1] = RCAR_GP_PIN(6,  1),	/* AVB1_MAGIC */
		[ 2] = RCAR_GP_PIN(6,  2),	/* AVB1_MDC */
		[ 3] = RCAR_GP_PIN(6,  3),	/* AVB1_PHY_INT */
		[ 4] = RCAR_GP_PIN(6,  4),	/* AVB1_LINK */
		[ 5] = RCAR_GP_PIN(6,  5),	/* AVB1_AVTP_MATCH */
		[ 6] = RCAR_GP_PIN(6,  6),	/* AVB1_TXC */
		[ 7] = RCAR_GP_PIN(6,  7),	/* AVB1_TX_CTL */
		[ 8] = RCAR_GP_PIN(6,  8),	/* AVB1_RXC */
		[ 9] = RCAR_GP_PIN(6,  9),	/* AVB1_RX_CTL */
		[10] = RCAR_GP_PIN(6, 10),	/* AVB1_AVTP_PPS */
		[11] = RCAR_GP_PIN(6, 11),	/* AVB1_AVTP_CAPTURE */
		[12] = RCAR_GP_PIN(6, 12),	/* AVB1_TD1 */
		[13] = RCAR_GP_PIN(6, 13),	/* AVB1_TD0 */
		[14] = RCAR_GP_PIN(6, 14),	/* AVB1_RD1*/
		[15] = RCAR_GP_PIN(6, 15),	/* AVB1_RD0 */
		[16] = RCAR_GP_PIN(6, 16),	/* AVB1_TD2 */
		[17] = RCAR_GP_PIN(6, 17),	/* AVB1_RD2 */
		[18] = RCAR_GP_PIN(6, 18),	/* AVB1_TD3 */
		[19] = RCAR_GP_PIN(6, 19),	/* AVB1_RD3 */
		[20] = RCAR_GP_PIN(6, 20),	/* AVB1_TXCREFCLK */
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
	{ PINMUX_BIAS_REG("PUEN7", 0xE60618C0, "PUD7", 0xE60618E0) {
		[ 0] = RCAR_GP_PIN(7,  0),	/* AVB0_AVTP_PPS */
		[ 1] = RCAR_GP_PIN(7,  1),	/* AVB0_AVTP_CAPTURE */
		[ 2] = RCAR_GP_PIN(7,  2),	/* AVB0_AVTP_MATCH */
		[ 3] = RCAR_GP_PIN(7,  3),	/* AVB0_TD3 */
		[ 4] = RCAR_GP_PIN(7,  4),	/* AVB0_LINK */
		[ 5] = RCAR_GP_PIN(7,  5),	/* AVB0_PHY_INT */
		[ 6] = RCAR_GP_PIN(7,  6),	/* AVB0_TD2 */
		[ 7] = RCAR_GP_PIN(7,  7),	/* AVB0_TD1 */
		[ 8] = RCAR_GP_PIN(7,  8),	/* AVB0_RD3 */
		[ 9] = RCAR_GP_PIN(7,  9),	/* AVB0_TXCREFCLK */
		[10] = RCAR_GP_PIN(7, 10),	/* AVB0_MAGIC */
		[11] = RCAR_GP_PIN(7, 11),	/* AVB0_TD0 */
		[12] = RCAR_GP_PIN(7, 12),	/* AVB0_RD2 */
		[13] = RCAR_GP_PIN(7, 13),	/* AVB0_MDC */
		[14] = RCAR_GP_PIN(7, 14),	/* AVB0_MDIO */
		[15] = RCAR_GP_PIN(7, 15),	/* AVB0_TXC */
		[16] = RCAR_GP_PIN(7, 16),	/* AVB0_TX_CTL */
		[17] = RCAR_GP_PIN(7, 17),	/* AVB0_RD1 */
		[18] = RCAR_GP_PIN(7, 18),	/* AVB0_RD0 */
		[19] = RCAR_GP_PIN(7, 19),	/* AVB0_RXC */
		[20] = RCAR_GP_PIN(7, 20),	/* AVB0_RX_CTL */
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

static const struct sh_pfc_soc_operations r8a779h0_pin_ops = {
	.pin_to_pocctrl = r8a779h0_pin_to_pocctrl,
	.get_bias = rcar_pinmux_get_bias,
	.set_bias = rcar_pinmux_set_bias,
};

const struct sh_pfc_soc_info r8a779h0_pinmux_info = {
	.name = "r8a779h0_pfc",
	.ops = &r8a779h0_pin_ops,
	.unlock_reg = 0x1ff,	/* PMMRn mask */

	.function = { PINMUX_FUNCTION_BEGIN, PINMUX_FUNCTION_END },

	.pins = pinmux_pins,
	.nr_pins = ARRAY_SIZE(pinmux_pins),
	.groups = pinmux_groups,
	.nr_groups = ARRAY_SIZE(pinmux_groups),
	.functions = pinmux_functions,
	.nr_functions = ARRAY_SIZE(pinmux_functions),

	.cfg_regs = pinmux_config_regs,
	.drive_regs = pinmux_drive_regs,
	.bias_regs = pinmux_bias_regs,
	.ioctrl_regs = pinmux_ioctrl_regs,

	.pinmux_data = pinmux_data,
	.pinmux_data_size = ARRAY_SIZE(pinmux_data),
};
