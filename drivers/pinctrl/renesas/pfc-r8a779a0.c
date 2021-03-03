// SPDX-License-Identifier: GPL-2.0
/*
 * R8A779A0 processor support - PFC hardware block.
 *
 * Copyright (C) 2020 Renesas Electronics Corp.
 *
 * This file is based on the drivers/pinctrl/renesas/pfc-r8a7795.c
 */

#include <linux/errno.h>
#include <linux/io.h>
#include <linux/kernel.h>

#include "sh_pfc.h"

#define CFG_FLAGS (SH_PFC_PIN_CFG_DRIVE_STRENGTH | SH_PFC_PIN_CFG_PULL_UP_DOWN)

#define CPU_ALL_GP(fn, sfx)	\
	PORT_GP_CFG_15(0, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(0, 15, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(0, 16, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(0, 17, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(0, 18, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(0, 19, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(0, 20, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(0, 21, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(0, 22, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(0, 23, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(0, 24, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(0, 25, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(0, 26, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(0, 27, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_31(1, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_2(2, fn, sfx, CFG_FLAGS),					\
	PORT_GP_CFG_1(2, 2, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(2, 3, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(2, 4, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(2, 5, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(2, 6, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(2, 7, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(2, 8, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(2, 9, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(2, 10, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(2, 11, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(2, 12, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(2, 13, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(2, 14, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(2, 15, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_1(2, 16, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(2, 17, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(2, 18, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(2, 19, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(2, 20, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(2, 21, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(2, 22, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(2, 23, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(2, 24, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_17(3, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_18(4, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_25_33),\
	PORT_GP_CFG_1(4, 18, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(4, 19, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(4, 20, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(4, 21, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(4, 22, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(4, 23, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(4, 24, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(4, 25, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(4, 26, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_18(5, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_25_33),\
	PORT_GP_CFG_1(5, 18, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(5, 19, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(5, 20, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_18(6, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_25_33),\
	PORT_GP_CFG_1(6, 18, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(6, 19, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(6, 20, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_18(7, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_25_33),\
	PORT_GP_CFG_1(7, 18, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(7, 19, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(7, 20, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_18(8, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_25_33),\
	PORT_GP_CFG_1(8, 18, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(8, 19, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(8, 20, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_18(9, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_25_33),\
	PORT_GP_CFG_1(9, 18, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(9, 19, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_1(9, 20, fn, sfx, CFG_FLAGS)

#define CPU_ALL_NOGP(fn)									\
	PIN_NOGP_CFG(PRESETOUT_N, "PRESETOUT#", fn, SH_PFC_PIN_CFG_PULL_UP_DOWN),		\
	PIN_NOGP_CFG(EXTALR, "EXTALR", fn, SH_PFC_PIN_CFG_PULL_UP_DOWN),			\
	PIN_NOGP_CFG(DCUTRST_N_LPDRST_N, "DCUTRST#_LPDRST#", fn, SH_PFC_PIN_CFG_PULL_UP_DOWN),	\
	PIN_NOGP_CFG(DCUTCK_LPDCLK, "DCUTCK_LPDCLK", fn, SH_PFC_PIN_CFG_PULL_UP_DOWN),		\
	PIN_NOGP_CFG(DCUTMS, "DCUTMS", fn, SH_PFC_PIN_CFG_PULL_UP_DOWN),			\
	PIN_NOGP_CFG(DCUTDI_LPDI, "DCUTDI_LPDI", fn, SH_PFC_PIN_CFG_PULL_UP_DOWN)

/*
 * F_() : just information
 * FM() : macro for FN_xxx / xxx_MARK
 */

/* GPSR0 */
#define GPSR0_27	FM(MMC_D7)
#define GPSR0_26	FM(MMC_D6)
#define GPSR0_25	FM(MMC_D5)
#define GPSR0_24	FM(MMC_D4)
#define GPSR0_23	FM(MMC_SD_CLK)
#define GPSR0_22	FM(MMC_SD_D3)
#define GPSR0_21	FM(MMC_SD_D2)
#define GPSR0_20	FM(MMC_SD_D1)
#define GPSR0_19	FM(MMC_SD_D0)
#define GPSR0_18	FM(MMC_SD_CMD)
#define GPSR0_17	FM(MMC_DS)
#define GPSR0_16	FM(SD_CD)
#define GPSR0_15	FM(SD_WP)
#define GPSR0_14	FM(RPC_INT_N)
#define GPSR0_13	FM(RPC_WP_N)
#define GPSR0_12	FM(RPC_RESET_N)
#define GPSR0_11	FM(QSPI1_SSL)
#define GPSR0_10	FM(QSPI1_IO3)
#define GPSR0_9		FM(QSPI1_IO2)
#define GPSR0_8		FM(QSPI1_MISO_IO1)
#define GPSR0_7		FM(QSPI1_MOSI_IO0)
#define GPSR0_6		FM(QSPI1_SPCLK)
#define GPSR0_5		FM(QSPI0_SSL)
#define GPSR0_4		FM(QSPI0_IO3)
#define GPSR0_3		FM(QSPI0_IO2)
#define GPSR0_2		FM(QSPI0_MISO_IO1)
#define GPSR0_1		FM(QSPI0_MOSI_IO0)
#define GPSR0_0		FM(QSPI0_SPCLK)

/* GPSR1 */
#define GPSR1_30	F_(GP1_30,	IP3SR1_27_24)
#define GPSR1_29	F_(GP1_29,	IP3SR1_23_20)
#define GPSR1_28	F_(GP1_28,	IP3SR1_19_16)
#define GPSR1_27	F_(IRQ3,	IP3SR1_15_12)
#define GPSR1_26	F_(IRQ2,	IP3SR1_11_8)
#define GPSR1_25	F_(IRQ1,	IP3SR1_7_4)
#define GPSR1_24	F_(IRQ0,	IP3SR1_3_0)
#define GPSR1_23	F_(MSIOF2_SS2,	IP2SR1_31_28)
#define GPSR1_22	F_(MSIOF2_SS1,	IP2SR1_27_24)
#define GPSR1_21	F_(MSIOF2_SYNC,	IP2SR1_23_20)
#define GPSR1_20	F_(MSIOF2_SCK,	IP2SR1_19_16)
#define GPSR1_19	F_(MSIOF2_TXD,	IP2SR1_15_12)
#define GPSR1_18	F_(MSIOF2_RXD,	IP2SR1_11_8)
#define GPSR1_17	F_(MSIOF1_SS2,	IP2SR1_7_4)
#define GPSR1_16	F_(MSIOF1_SS1,	IP2SR1_3_0)
#define GPSR1_15	F_(MSIOF1_SYNC,	IP1SR1_31_28)
#define GPSR1_14	F_(MSIOF1_SCK,	IP1SR1_27_24)
#define GPSR1_13	F_(MSIOF1_TXD,	IP1SR1_23_20)
#define GPSR1_12	F_(MSIOF1_RXD,	IP1SR1_19_16)
#define GPSR1_11	F_(MSIOF0_SS2,	IP1SR1_15_12)
#define GPSR1_10	F_(MSIOF0_SS1,	IP1SR1_11_8)
#define GPSR1_9		F_(MSIOF0_SYNC,	IP1SR1_7_4)
#define GPSR1_8		F_(MSIOF0_SCK,	IP1SR1_3_0)
#define GPSR1_7		F_(MSIOF0_TXD,	IP0SR1_31_28)
#define GPSR1_6		F_(MSIOF0_RXD,	IP0SR1_27_24)
#define GPSR1_5		F_(HTX0,	IP0SR1_23_20)
#define GPSR1_4		F_(HCTS0_N,	IP0SR1_19_16)
#define GPSR1_3		F_(HRTS0_N,	IP0SR1_15_12)
#define GPSR1_2		F_(HSCK0,	IP0SR1_11_8)
#define GPSR1_1		F_(HRX0,	IP0SR1_7_4)
#define GPSR1_0		F_(SCIF_CLK,	IP0SR1_3_0)

/* GPSR2 */
#define GPSR2_24	FM(TCLK2_A)
#define GPSR2_23	F_(TCLK1_A,		IP2SR2_31_28)
#define GPSR2_22	F_(TPU0TO1,		IP2SR2_27_24)
#define GPSR2_21	F_(TPU0TO0,		IP2SR2_23_20)
#define GPSR2_20	F_(CLK_EXTFXR,		IP2SR2_19_16)
#define GPSR2_19	F_(RXDB_EXTFXR,		IP2SR2_15_12)
#define GPSR2_18	F_(FXR_TXDB,		IP2SR2_11_8)
#define GPSR2_17	F_(RXDA_EXTFXR_A,	IP2SR2_7_4)
#define GPSR2_16	F_(FXR_TXDA_A,		IP2SR2_3_0)
#define GPSR2_15	F_(GP2_15,		IP1SR2_31_28)
#define GPSR2_14	F_(GP2_14,		IP1SR2_27_24)
#define GPSR2_13	F_(GP2_13,		IP1SR2_23_20)
#define GPSR2_12	F_(GP2_12,		IP1SR2_19_16)
#define GPSR2_11	F_(GP2_11,		IP1SR2_15_12)
#define GPSR2_10	F_(GP2_10,		IP1SR2_11_8)
#define GPSR2_9		F_(GP2_09,		IP1SR2_7_4)
#define GPSR2_8		F_(GP2_08,		IP1SR2_3_0)
#define GPSR2_7		F_(GP2_07,		IP0SR2_31_28)
#define GPSR2_6		F_(GP2_06,		IP0SR2_27_24)
#define GPSR2_5		F_(GP2_05,		IP0SR2_23_20)
#define GPSR2_4		F_(GP2_04,		IP0SR2_19_16)
#define GPSR2_3		F_(GP2_03,		IP0SR2_15_12)
#define GPSR2_2		F_(GP2_02,		IP0SR2_11_8)
#define GPSR2_1		F_(IPC_CLKOUT,		IP0SR2_7_4)
#define GPSR2_0		F_(IPC_CLKIN,		IP0SR2_3_0)

/* GPSR3 */
#define GPSR3_16	FM(CANFD7_RX)
#define GPSR3_15	FM(CANFD7_TX)
#define GPSR3_14	FM(CANFD6_RX)
#define GPSR3_13	F_(CANFD6_TX,	IP1SR3_23_20)
#define GPSR3_12	F_(CANFD5_RX,	IP1SR3_19_16)
#define GPSR3_11	F_(CANFD5_TX,	IP1SR3_15_12)
#define GPSR3_10	F_(CANFD4_RX,	IP1SR3_11_8)
#define GPSR3_9		F_(CANFD4_TX,	IP1SR3_7_4)
#define GPSR3_8		F_(CANFD3_RX,	IP1SR3_3_0)
#define GPSR3_7		F_(CANFD3_TX,	IP0SR3_31_28)
#define GPSR3_6		F_(CANFD2_RX,	IP0SR3_27_24)
#define GPSR3_5		F_(CANFD2_TX,	IP0SR3_23_20)
#define GPSR3_4		FM(CANFD1_RX)
#define GPSR3_3		FM(CANFD1_TX)
#define GPSR3_2		F_(CANFD0_RX,	IP0SR3_11_8)
#define GPSR3_1		F_(CANFD0_TX,	IP0SR3_7_4)
#define GPSR3_0		FM(CAN_CLK)

/* GPSR4 */
#define GPSR4_26	FM(AVS1)
#define GPSR4_25	FM(AVS0)
#define GPSR4_24	FM(PCIE3_CLKREQ_N)
#define GPSR4_23	FM(PCIE2_CLKREQ_N)
#define GPSR4_22	FM(PCIE1_CLKREQ_N)
#define GPSR4_21	FM(PCIE0_CLKREQ_N)
#define GPSR4_20	F_(AVB0_AVTP_PPS,	IP2SR4_19_16)
#define GPSR4_19	F_(AVB0_AVTP_CAPTURE,	IP2SR4_15_12)
#define GPSR4_18	F_(AVB0_AVTP_MATCH,	IP2SR4_11_8)
#define GPSR4_17	F_(AVB0_LINK,		IP2SR4_7_4)
#define GPSR4_16	FM(AVB0_PHY_INT)
#define GPSR4_15	F_(AVB0_MAGIC,		IP1SR4_31_28)
#define GPSR4_14	F_(AVB0_MDC,		IP1SR4_27_24)
#define GPSR4_13	F_(AVB0_MDIO,		IP1SR4_23_20)
#define GPSR4_12	F_(AVB0_TXCREFCLK,	IP1SR4_19_16)
#define GPSR4_11	F_(AVB0_TD3,		IP1SR4_15_12)
#define GPSR4_10	F_(AVB0_TD2,		IP1SR4_11_8)
#define GPSR4_9		F_(AVB0_TD1,		IP1SR4_7_4)
#define GPSR4_8		F_(AVB0_TD0,		IP1SR4_3_0)
#define GPSR4_7		F_(AVB0_TXC,		IP0SR4_31_28)
#define GPSR4_6		F_(AVB0_TX_CTL,		IP0SR4_27_24)
#define GPSR4_5		F_(AVB0_RD3,		IP0SR4_23_20)
#define GPSR4_4		F_(AVB0_RD2,		IP0SR4_19_16)
#define GPSR4_3		F_(AVB0_RD1,		IP0SR4_15_12)
#define GPSR4_2		F_(AVB0_RD0,		IP0SR4_11_8)
#define GPSR4_1		F_(AVB0_RXC,		IP0SR4_7_4)
#define GPSR4_0		F_(AVB0_RX_CTL,		IP0SR4_3_0)

/* GPSR5 */
#define GPSR5_20	F_(AVB1_AVTP_PPS,	IP2SR5_19_16)
#define GPSR5_19	F_(AVB1_AVTP_CAPTURE,	IP2SR5_15_12)
#define GPSR5_18	F_(AVB1_AVTP_MATCH,	IP2SR5_11_8)
#define GPSR5_17	F_(AVB1_LINK,		IP2SR5_7_4)
#define GPSR5_16	FM(AVB1_PHY_INT)
#define GPSR5_15	F_(AVB1_MAGIC,		IP1SR5_31_28)
#define GPSR5_14	F_(AVB1_MDC,		IP1SR5_27_24)
#define GPSR5_13	F_(AVB1_MDIO,		IP1SR5_23_20)
#define GPSR5_12	F_(AVB1_TXCREFCLK,	IP1SR5_19_16)
#define GPSR5_11	F_(AVB1_TD3,		IP1SR5_15_12)
#define GPSR5_10	F_(AVB1_TD2,		IP1SR5_11_8)
#define GPSR5_9		F_(AVB1_TD1,		IP1SR5_7_4)
#define GPSR5_8		F_(AVB1_TD0,		IP1SR5_3_0)
#define GPSR5_7		F_(AVB1_TXC,		IP0SR5_31_28)
#define GPSR5_6		F_(AVB1_TX_CTL,		IP0SR5_27_24)
#define GPSR5_5		F_(AVB1_RD3,		IP0SR5_23_20)
#define GPSR5_4		F_(AVB1_RD2,		IP0SR5_19_16)
#define GPSR5_3		F_(AVB1_RD1,		IP0SR5_15_12)
#define GPSR5_2		F_(AVB1_RD0,		IP0SR5_11_8)
#define GPSR5_1		F_(AVB1_RXC,		IP0SR5_7_4)
#define GPSR5_0		F_(AVB1_RX_CTL,		IP0SR5_3_0)

/* GPSR6 */
#define GPSR6_20	FM(AVB2_AVTP_PPS)
#define GPSR6_19	FM(AVB2_AVTP_CAPTURE)
#define GPSR6_18	FM(AVB2_AVTP_MATCH)
#define GPSR6_17	FM(AVB2_LINK)
#define GPSR6_16	FM(AVB2_PHY_INT)
#define GPSR6_15	FM(AVB2_MAGIC)
#define GPSR6_14	FM(AVB2_MDC)
#define GPSR6_13	FM(AVB2_MDIO)
#define GPSR6_12	FM(AVB2_TXCREFCLK)
#define GPSR6_11	FM(AVB2_TD3)
#define GPSR6_10	FM(AVB2_TD2)
#define GPSR6_9		FM(AVB2_TD1)
#define GPSR6_8		FM(AVB2_TD0)
#define GPSR6_7		FM(AVB2_TXC)
#define GPSR6_6		FM(AVB2_TX_CTL)
#define GPSR6_5		FM(AVB2_RD3)
#define GPSR6_4		FM(AVB2_RD2)
#define GPSR6_3		FM(AVB2_RD1)
#define GPSR6_2		FM(AVB2_RD0)
#define GPSR6_1		FM(AVB2_RXC)
#define GPSR6_0		FM(AVB2_RX_CTL)

/* GPSR7 */
#define GPSR7_20	FM(AVB3_AVTP_PPS)
#define GPSR7_19	FM(AVB3_AVTP_CAPTURE)
#define GPSR7_18	FM(AVB3_AVTP_MATCH)
#define GPSR7_17	FM(AVB3_LINK)
#define GPSR7_16	FM(AVB3_PHY_INT)
#define GPSR7_15	FM(AVB3_MAGIC)
#define GPSR7_14	FM(AVB3_MDC)
#define GPSR7_13	FM(AVB3_MDIO)
#define GPSR7_12	FM(AVB3_TXCREFCLK)
#define GPSR7_11	FM(AVB3_TD3)
#define GPSR7_10	FM(AVB3_TD2)
#define GPSR7_9		FM(AVB3_TD1)
#define GPSR7_8		FM(AVB3_TD0)
#define GPSR7_7		FM(AVB3_TXC)
#define GPSR7_6		FM(AVB3_TX_CTL)
#define GPSR7_5		FM(AVB3_RD3)
#define GPSR7_4		FM(AVB3_RD2)
#define GPSR7_3		FM(AVB3_RD1)
#define GPSR7_2		FM(AVB3_RD0)
#define GPSR7_1		FM(AVB3_RXC)
#define GPSR7_0		FM(AVB3_RX_CTL)

/* GPSR8 */
#define GPSR8_20	FM(AVB4_AVTP_PPS)
#define GPSR8_19	FM(AVB4_AVTP_CAPTURE)
#define GPSR8_18	FM(AVB4_AVTP_MATCH)
#define GPSR8_17	FM(AVB4_LINK)
#define GPSR8_16	FM(AVB4_PHY_INT)
#define GPSR8_15	FM(AVB4_MAGIC)
#define GPSR8_14	FM(AVB4_MDC)
#define GPSR8_13	FM(AVB4_MDIO)
#define GPSR8_12	FM(AVB4_TXCREFCLK)
#define GPSR8_11	FM(AVB4_TD3)
#define GPSR8_10	FM(AVB4_TD2)
#define GPSR8_9		FM(AVB4_TD1)
#define GPSR8_8		FM(AVB4_TD0)
#define GPSR8_7		FM(AVB4_TXC)
#define GPSR8_6		FM(AVB4_TX_CTL)
#define GPSR8_5		FM(AVB4_RD3)
#define GPSR8_4		FM(AVB4_RD2)
#define GPSR8_3		FM(AVB4_RD1)
#define GPSR8_2		FM(AVB4_RD0)
#define GPSR8_1		FM(AVB4_RXC)
#define GPSR8_0		FM(AVB4_RX_CTL)

/* GPSR9 */
#define GPSR9_20	FM(AVB5_AVTP_PPS)
#define GPSR9_19	FM(AVB5_AVTP_CAPTURE)
#define GPSR9_18	FM(AVB5_AVTP_MATCH)
#define GPSR9_17	FM(AVB5_LINK)
#define GPSR9_16	FM(AVB5_PHY_INT)
#define GPSR9_15	FM(AVB5_MAGIC)
#define GPSR9_14	FM(AVB5_MDC)
#define GPSR9_13	FM(AVB5_MDIO)
#define GPSR9_12	FM(AVB5_TXCREFCLK)
#define GPSR9_11	FM(AVB5_TD3)
#define GPSR9_10	FM(AVB5_TD2)
#define GPSR9_9		FM(AVB5_TD1)
#define GPSR9_8		FM(AVB5_TD0)
#define GPSR9_7		FM(AVB5_TXC)
#define GPSR9_6		FM(AVB5_TX_CTL)
#define GPSR9_5		FM(AVB5_RD3)
#define GPSR9_4		FM(AVB5_RD2)
#define GPSR9_3		FM(AVB5_RD1)
#define GPSR9_2		FM(AVB5_RD0)
#define GPSR9_1		FM(AVB5_RXC)
#define GPSR9_0		FM(AVB5_RX_CTL)

/* IP0SR1 */		/* 0 */		/* 1 */		/* 2 */		/* 3 */		/* 4 */		/* 5 */		/* 6 - F */
#define IP0SR1_3_0	FM(SCIF_CLK)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	FM(A0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_7_4	FM(HRX0)	FM(RX0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	FM(A1)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_11_8	FM(HSCK0)	FM(SCK0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	FM(A2)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_15_12	FM(HRTS0_N)	FM(RTS0_N)	F_(0, 0)	F_(0, 0)	F_(0, 0)	FM(A3)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_19_16	FM(HCTS0_N)	FM(CTS0_N)	F_(0, 0)	F_(0, 0)	F_(0, 0)	FM(A4)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_23_20	FM(HTX0)	FM(TX0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	FM(A5)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_27_24	FM(MSIOF0_RXD)	F_(0, 0)	F_(0, 0)	F_(0, 0)	FM(DU_DR2)	FM(A6)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_31_28	FM(MSIOF0_TXD)	F_(0, 0)	F_(0, 0)	F_(0, 0)	FM(DU_DR3)	FM(A7)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
/* IP1SR1 */		/* 0 */		/* 1 */		/* 2 */		/* 3 */		/* 4 */		/* 5 */		/* 6 - F */
#define IP1SR1_3_0	FM(MSIOF0_SCK)	F_(0, 0)	F_(0, 0)	F_(0, 0)	FM(DU_DR4)	FM(A8)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR1_7_4	FM(MSIOF0_SYNC)	F_(0, 0)	F_(0, 0)	F_(0, 0)	FM(DU_DR5)	FM(A9)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR1_11_8	FM(MSIOF0_SS1)	F_(0, 0)	F_(0, 0)	F_(0, 0)	FM(DU_DR6)	FM(A10)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR1_15_12	FM(MSIOF0_SS2)	F_(0, 0)	F_(0, 0)	F_(0, 0)	FM(DU_DR7)	FM(A11)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR1_19_16	FM(MSIOF1_RXD)	F_(0, 0)	F_(0, 0)	F_(0, 0)	FM(DU_DG2)	FM(A12)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR1_23_20	FM(MSIOF1_TXD)	FM(HRX3)	FM(SCK3)	F_(0, 0)	FM(DU_DG3)	FM(A13)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR1_27_24	FM(MSIOF1_SCK)	FM(HSCK3)	FM(CTS3_N)	F_(0, 0)	FM(DU_DG4)	FM(A14)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR1_31_28	FM(MSIOF1_SYNC)	FM(HRTS3_N)	FM(RTS3_N)	F_(0, 0)	FM(DU_DG5)	FM(A15)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
/* IP2SR1 */		/* 0 */		/* 1 */		/* 2 */		/* 3 */		/* 4 */		/* 5 */		/* 6 - F */
#define IP2SR1_3_0	FM(MSIOF1_SS1)	FM(HCTS3_N)	FM(RX3)		F_(0, 0)	FM(DU_DG6)	FM(A16)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR1_7_4	FM(MSIOF1_SS2)	FM(HTX3)	FM(TX3)		F_(0, 0)	FM(DU_DG7)	FM(A17)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR1_11_8	FM(MSIOF2_RXD)	FM(HSCK1)	FM(SCK1)	F_(0, 0)	FM(DU_DB2)	FM(A18)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR1_15_12	FM(MSIOF2_TXD)	FM(HCTS1_N)	FM(CTS1_N)	F_(0, 0)	FM(DU_DB3)	FM(A19)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR1_19_16	FM(MSIOF2_SCK)	FM(HRTS1_N)	FM(RTS1_N)	F_(0, 0)	FM(DU_DB4)	FM(A20)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR1_23_20	FM(MSIOF2_SYNC)	FM(HRX1)	FM(RX1_A)	F_(0, 0)	FM(DU_DB5)	FM(A21)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR1_27_24	FM(MSIOF2_SS1)	FM(HTX1)	FM(TX1_A)	F_(0, 0)	FM(DU_DB6)	FM(A22)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR1_31_28	FM(MSIOF2_SS2)	FM(TCLK1_B)	F_(0, 0)	F_(0, 0)	FM(DU_DB7)	FM(A23)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP3SR1 */		/* 0 */			/* 1 */		/* 2 */		/* 3 */		/* 4 */			/* 5 */		/* 6 - F */
#define IP3SR1_3_0	FM(IRQ0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	FM(DU_DOTCLKOUT)	FM(A24)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR1_7_4	FM(IRQ1)		F_(0, 0)	F_(0, 0)	F_(0, 0)	FM(DU_HSYNC)		FM(A25)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR1_11_8	FM(IRQ2)		F_(0, 0)	F_(0, 0)	F_(0, 0)	FM(DU_VSYNC)		FM(CS1_N_A26)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR1_15_12	FM(IRQ3)		F_(0, 0)	F_(0, 0)	F_(0, 0)	FM(DU_ODDF_DISP_CDE)	FM(CS0_N)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR1_19_16	FM(GP1_28)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)		FM(D0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR1_23_20	FM(GP1_29)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)		FM(D1)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR1_27_24	FM(GP1_30)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)		FM(D2)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR1_31_28	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP0SR2 */		/* 0 */			/* 1 */			/* 2 */		/* 3 */		/* 4 */		/* 5 */		/* 6 - F */
#define IP0SR2_3_0	FM(IPC_CLKIN)		FM(IPC_CLKEN_IN)	F_(0, 0)	F_(0, 0)	FM(DU_DOTCLKIN)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR2_7_4	FM(IPC_CLKOUT)		FM(IPC_CLKEN_OUT)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR2_11_8	FM(GP2_02)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	FM(D3)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR2_15_12	FM(GP2_03)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	FM(D4)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR2_19_16	FM(GP2_04)		F_(0, 0)		FM(MSIOF4_RXD)	F_(0, 0)	F_(0, 0)	FM(D5)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR2_23_20	FM(GP2_05)		FM(HSCK2)		FM(MSIOF4_TXD)	FM(SCK4)	F_(0, 0)	FM(D6)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR2_27_24	FM(GP2_06)		FM(HCTS2_N)		FM(MSIOF4_SCK)	FM(CTS4_N)	F_(0, 0)	FM(D7)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR2_31_28	FM(GP2_07)		FM(HRTS2_N)		FM(MSIOF4_SYNC)	FM(RTS4_N)	F_(0, 0)	FM(D8)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
/* IP1SR2 */		/* 0 */			/* 1 */			/* 2 */		/* 3 */		/* 4 */		/* 5 */		/* 6 - F */
#define IP1SR2_3_0	FM(GP2_08)		FM(HRX2)		FM(MSIOF4_SS1)	FM(RX4)		F_(0, 0)	FM(D9)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR2_7_4	FM(GP2_09)		FM(HTX2)		FM(MSIOF4_SS2)	FM(TX4)		F_(0, 0)	FM(D10)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR2_11_8	FM(GP2_10)		FM(TCLK2_B)		FM(MSIOF5_RXD)	F_(0, 0)	F_(0, 0)	FM(D11)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR2_15_12	FM(GP2_11)		FM(TCLK3)		FM(MSIOF5_TXD)	F_(0, 0)	F_(0, 0)	FM(D12)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR2_19_16	FM(GP2_12)		FM(TCLK4)		FM(MSIOF5_SCK)	F_(0, 0)	F_(0, 0)	FM(D13)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR2_23_20	FM(GP2_13)		F_(0, 0)		FM(MSIOF5_SYNC)	F_(0, 0)	F_(0, 0)	FM(D14)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR2_27_24	FM(GP2_14)		FM(IRQ4)		FM(MSIOF5_SS1)	F_(0, 0)	F_(0, 0)	FM(D15)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR2_31_28	FM(GP2_15)		FM(IRQ5)		FM(MSIOF5_SS2)	FM(CPG_CPCKOUT)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
/* IP2SR2 */		/* 0 */			/* 1 */			/* 2 */		/* 3 */		/* 4 */		/* 5 */		/* 6 - F */
#define IP2SR2_3_0	FM(FXR_TXDA_A)		FM(MSIOF3_SS1)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR2_7_4	FM(RXDA_EXTFXR_A)	FM(MSIOF3_SS2)		F_(0, 0)	F_(0, 0)	F_(0, 0)	FM(BS_N)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR2_11_8	FM(FXR_TXDB)		FM(MSIOF3_RXD)		F_(0, 0)	F_(0, 0)	F_(0, 0)	FM(RD_N)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR2_15_12	FM(RXDB_EXTFXR)		FM(MSIOF3_TXD)		F_(0, 0)	F_(0, 0)	F_(0, 0)	FM(WE0_N)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR2_19_16	FM(CLK_EXTFXR)		FM(MSIOF3_SCK)		F_(0, 0)	F_(0, 0)	F_(0, 0)	FM(WE1_N)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR2_23_20	FM(TPU0TO0)		FM(MSIOF3_SYNC)		F_(0, 0)	F_(0, 0)	F_(0, 0)	FM(RD_WR_N)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR2_27_24	FM(TPU0TO1)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	FM(CLKOUT)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR2_31_28	FM(TCLK1_A)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	FM(EX_WAIT0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP0SR3 */		/* 0 */		/* 1 */			/* 2 */		/* 3 */			/* 4 */		/* 5 */		/* 6 - F */
#define IP0SR3_3_0	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR3_7_4	FM(CANFD0_TX)	FM(FXR_TXDA_B)		FM(TX1_B)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR3_11_8	FM(CANFD0_RX)	FM(RXDA_EXTFXR_B)	FM(RX1_B)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR3_15_12	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR3_19_16	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR3_23_20	FM(CANFD2_TX)	FM(TPU0TO2)		FM(PWM0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR3_27_24	FM(CANFD2_RX)	FM(TPU0TO3)		FM(PWM1)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR3_31_28	FM(CANFD3_TX)	F_(0, 0)		FM(PWM2)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
/* IP1SR3 */		/* 0 */		/* 1 */			/* 2 */		/* 3 */			/* 4 */		/* 5 */		/* 6 - F */
#define IP1SR3_3_0	FM(CANFD3_RX)	F_(0, 0)		FM(PWM3)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR3_7_4	FM(CANFD4_TX)	F_(0, 0)		FM(PWM4)	FM(FXR_CLKOUT1)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR3_11_8	FM(CANFD4_RX)	F_(0, 0)		F_(0, 0)	FM(FXR_CLKOUT2)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR3_15_12	FM(CANFD5_TX)	F_(0, 0)		F_(0, 0)	FM(FXR_TXENA_N)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR3_19_16	FM(CANFD5_RX)	F_(0, 0)		F_(0, 0)	FM(FXR_TXENB_N)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR3_23_20	FM(CANFD6_TX)	F_(0, 0)		F_(0, 0)	FM(STPWT_EXTFXR)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR3_27_24	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR3_31_28	F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP0SR4 */		/* 0 */		/* 1 */			/* 2 */		/* 3 */		/* 4 */		/* 5 */		/* 6 - F */
#define IP0SR4_3_0	FM(AVB0_RX_CTL)	FM(AVB0_MII_RX_DV)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_7_4	FM(AVB0_RXC)	FM(AVB0_MII_RXC)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_11_8	FM(AVB0_RD0)	FM(AVB0_MII_RD0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_15_12	FM(AVB0_RD1)	FM(AVB0_MII_RD1)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_19_16	FM(AVB0_RD2)	FM(AVB0_MII_RD2)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_23_20	FM(AVB0_RD3)	FM(AVB0_MII_RD3)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_27_24	FM(AVB0_TX_CTL)	FM(AVB0_MII_TX_EN)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_31_28	FM(AVB0_TXC)	FM(AVB0_MII_TXC)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
/* IP1SR4 */		/* 0 */			/* 1 */			/* 2 */		/* 3 */		/* 4 */		/* 5 */		/* 6 - F */
#define IP1SR4_3_0	FM(AVB0_TD0)		FM(AVB0_MII_TD0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR4_7_4	FM(AVB0_TD1)		FM(AVB0_MII_TD1)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR4_11_8	FM(AVB0_TD2)		FM(AVB0_MII_TD2)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR4_15_12	FM(AVB0_TD3)		FM(AVB0_MII_TD3)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR4_19_16	FM(AVB0_TXCREFCLK)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR4_23_20	FM(AVB0_MDIO)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR4_27_24	FM(AVB0_MDC)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR4_31_28	FM(AVB0_MAGIC)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
/* IP2SR4 */		/* 0 */			/* 1 */			/* 2 */		/* 3 */		/* 4 */		/* 5 */		/* 6 - F */
#define IP2SR4_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR4_7_4	FM(AVB0_LINK)		FM(AVB0_MII_TX_ER)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR4_11_8	FM(AVB0_AVTP_MATCH)	FM(AVB0_MII_RX_ER)	FM(CC5_OSCOUT)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR4_15_12	FM(AVB0_AVTP_CAPTURE)	FM(AVB0_MII_CRS)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR4_19_16	FM(AVB0_AVTP_PPS)	FM(AVB0_MII_COL)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR4_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR4_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR4_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP0SR5 */		/* 0 */			/* 1 */			/* 2 */		/* 3 */		/* 4 */		/* 5 */		/* 6 - F */
#define IP0SR5_3_0	FM(AVB1_RX_CTL)		FM(AVB1_MII_RX_DV)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_7_4	FM(AVB1_RXC)		FM(AVB1_MII_RXC)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_11_8	FM(AVB1_RD0)		FM(AVB1_MII_RD0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_15_12	FM(AVB1_RD1)		FM(AVB1_MII_RD1)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_19_16	FM(AVB1_RD2)		FM(AVB1_MII_RD2)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_23_20	FM(AVB1_RD3)		FM(AVB1_MII_RD3)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_27_24	FM(AVB1_TX_CTL)		FM(AVB1_MII_TX_EN)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_31_28	FM(AVB1_TXC)		FM(AVB1_MII_TXC)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
/* IP1SR5 */		/* 0 */			/* 1 */			/* 2 */		/* 3 */		/* 4 */		/* 5 */		/* 6 - F */
#define IP1SR5_3_0	FM(AVB1_TD0)		FM(AVB1_MII_TD0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR5_7_4	FM(AVB1_TD1)		FM(AVB1_MII_TD1)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR5_11_8	FM(AVB1_TD2)		FM(AVB1_MII_TD2)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR5_15_12	FM(AVB1_TD3)		FM(AVB1_MII_TD3)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR5_19_16	FM(AVB1_TXCREFCLK)	F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR5_23_20	FM(AVB1_MDIO)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR5_27_24	FM(AVB1_MDC)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR5_31_28	FM(AVB1_MAGIC)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
/* IP2SR5 */		/* 0 */			/* 1 */			/* 2 */		/* 3 */		/* 4 */		/* 5 */		/* 6 - F */
#define IP2SR5_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR5_7_4	FM(AVB1_LINK)		FM(AVB1_MII_TX_ER)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR5_11_8	FM(AVB1_AVTP_MATCH)	FM(AVB1_MII_RX_ER)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR5_15_12	FM(AVB1_AVTP_CAPTURE)	FM(AVB1_MII_CRS)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR5_19_16	FM(AVB1_AVTP_PPS)	FM(AVB1_MII_COL)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR5_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR5_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR5_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

#define PINMUX_GPSR		\
				\
		GPSR1_30	\
		GPSR1_29	\
		GPSR1_28	\
GPSR0_27	GPSR1_27	\
GPSR0_26	GPSR1_26					GPSR4_26 \
GPSR0_25	GPSR1_25					GPSR4_25 \
GPSR0_24	GPSR1_24	GPSR2_24			GPSR4_24 \
GPSR0_23	GPSR1_23	GPSR2_23			GPSR4_23 \
GPSR0_22	GPSR1_22	GPSR2_22			GPSR4_22 \
GPSR0_21	GPSR1_21	GPSR2_21			GPSR4_21 \
GPSR0_20	GPSR1_20	GPSR2_20			GPSR4_20	GPSR5_20	GPSR6_20	GPSR7_20	GPSR8_20	GPSR9_20 \
GPSR0_19	GPSR1_19	GPSR2_19			GPSR4_19	GPSR5_19	GPSR6_19	GPSR7_19	GPSR8_19	GPSR9_19 \
GPSR0_18	GPSR1_18	GPSR2_18			GPSR4_18	GPSR5_18	GPSR6_18	GPSR7_18	GPSR8_18	GPSR9_18 \
GPSR0_17	GPSR1_17	GPSR2_17			GPSR4_17	GPSR5_17	GPSR6_17	GPSR7_17	GPSR8_17	GPSR9_17 \
GPSR0_16	GPSR1_16	GPSR2_16	GPSR3_16	GPSR4_16	GPSR5_16	GPSR6_16	GPSR7_16	GPSR8_16	GPSR9_16 \
GPSR0_15	GPSR1_15	GPSR2_15	GPSR3_15	GPSR4_15	GPSR5_15	GPSR6_15	GPSR7_15	GPSR8_15	GPSR9_15 \
GPSR0_14	GPSR1_14	GPSR2_14	GPSR3_14	GPSR4_14	GPSR5_14	GPSR6_14	GPSR7_14	GPSR8_14	GPSR9_14 \
GPSR0_13	GPSR1_13	GPSR2_13	GPSR3_13	GPSR4_13	GPSR5_13	GPSR6_13	GPSR7_13	GPSR8_13	GPSR9_13 \
GPSR0_12	GPSR1_12	GPSR2_12	GPSR3_12	GPSR4_12	GPSR5_12	GPSR6_12	GPSR7_12	GPSR8_12	GPSR9_12 \
GPSR0_11	GPSR1_11	GPSR2_11	GPSR3_11	GPSR4_11	GPSR5_11	GPSR6_11	GPSR7_11	GPSR8_11	GPSR9_11 \
GPSR0_10	GPSR1_10	GPSR2_10	GPSR3_10	GPSR4_10	GPSR5_10	GPSR6_10	GPSR7_10	GPSR8_10	GPSR9_10 \
GPSR0_9		GPSR1_9		GPSR2_9		GPSR3_9		GPSR4_9		GPSR5_9		GPSR6_9		GPSR7_9		GPSR8_9		GPSR9_9 \
GPSR0_8		GPSR1_8		GPSR2_8		GPSR3_8		GPSR4_8		GPSR5_8		GPSR6_8		GPSR7_8		GPSR8_8		GPSR9_8 \
GPSR0_7		GPSR1_7		GPSR2_7		GPSR3_7		GPSR4_7		GPSR5_7		GPSR6_7		GPSR7_7		GPSR8_7		GPSR9_7 \
GPSR0_6		GPSR1_6		GPSR2_6		GPSR3_6		GPSR4_6		GPSR5_6		GPSR6_6		GPSR7_6		GPSR8_6		GPSR9_6 \
GPSR0_5		GPSR1_5		GPSR2_5		GPSR3_5		GPSR4_5		GPSR5_5		GPSR6_5		GPSR7_5		GPSR8_5		GPSR9_5 \
GPSR0_4		GPSR1_4		GPSR2_4		GPSR3_4		GPSR4_4		GPSR5_4		GPSR6_4		GPSR7_4		GPSR8_4		GPSR9_4 \
GPSR0_3		GPSR1_3		GPSR2_3		GPSR3_3		GPSR4_3		GPSR5_3		GPSR6_3		GPSR7_3		GPSR8_3		GPSR9_3 \
GPSR0_2		GPSR1_2		GPSR2_2		GPSR3_2		GPSR4_2		GPSR5_2		GPSR6_2		GPSR7_2		GPSR8_2		GPSR9_2 \
GPSR0_1		GPSR1_1		GPSR2_1		GPSR3_1		GPSR4_1		GPSR5_1		GPSR6_1		GPSR7_1		GPSR8_1		GPSR9_1 \
GPSR0_0		GPSR1_0		GPSR2_0		GPSR3_0		GPSR4_0		GPSR5_0		GPSR6_0		GPSR7_0		GPSR8_0		GPSR9_0

#define PINMUX_IPSR	\
\
FM(IP0SR1_3_0)		IP0SR1_3_0	FM(IP1SR1_3_0)		IP1SR1_3_0	FM(IP2SR1_3_0)		IP2SR1_3_0	FM(IP3SR1_3_0)		IP3SR1_3_0 \
FM(IP0SR1_7_4)		IP0SR1_7_4	FM(IP1SR1_7_4)		IP1SR1_7_4	FM(IP2SR1_7_4)		IP2SR1_7_4	FM(IP3SR1_7_4)		IP3SR1_7_4 \
FM(IP0SR1_11_8)		IP0SR1_11_8	FM(IP1SR1_11_8)		IP1SR1_11_8	FM(IP2SR1_11_8)		IP2SR1_11_8	FM(IP3SR1_11_8)		IP3SR1_11_8 \
FM(IP0SR1_15_12)	IP0SR1_15_12	FM(IP1SR1_15_12)	IP1SR1_15_12	FM(IP2SR1_15_12)	IP2SR1_15_12	FM(IP3SR1_15_12)	IP3SR1_15_12 \
FM(IP0SR1_19_16)	IP0SR1_19_16	FM(IP1SR1_19_16)	IP1SR1_19_16	FM(IP2SR1_19_16)	IP2SR1_19_16	FM(IP3SR1_19_16)	IP3SR1_19_16 \
FM(IP0SR1_23_20)	IP0SR1_23_20	FM(IP1SR1_23_20)	IP1SR1_23_20	FM(IP2SR1_23_20)	IP2SR1_23_20	FM(IP3SR1_23_20)	IP3SR1_23_20 \
FM(IP0SR1_27_24)	IP0SR1_27_24	FM(IP1SR1_27_24)	IP1SR1_27_24	FM(IP2SR1_27_24)	IP2SR1_27_24	FM(IP3SR1_27_24)	IP3SR1_27_24 \
FM(IP0SR1_31_28)	IP0SR1_31_28	FM(IP1SR1_31_28)	IP1SR1_31_28	FM(IP2SR1_31_28)	IP2SR1_31_28	FM(IP3SR1_31_28)	IP3SR1_31_28 \
\
FM(IP0SR2_3_0)		IP0SR2_3_0	FM(IP1SR2_3_0)		IP1SR2_3_0	FM(IP2SR2_3_0)		IP2SR2_3_0 \
FM(IP0SR2_7_4)		IP0SR2_7_4	FM(IP1SR2_7_4)		IP1SR2_7_4	FM(IP2SR2_7_4)		IP2SR2_7_4 \
FM(IP0SR2_11_8)		IP0SR2_11_8	FM(IP1SR2_11_8)		IP1SR2_11_8	FM(IP2SR2_11_8)		IP2SR2_11_8 \
FM(IP0SR2_15_12)	IP0SR2_15_12	FM(IP1SR2_15_12)	IP1SR2_15_12	FM(IP2SR2_15_12)	IP2SR2_15_12 \
FM(IP0SR2_19_16)	IP0SR2_19_16	FM(IP1SR2_19_16)	IP1SR2_19_16	FM(IP2SR2_19_16)	IP2SR2_19_16 \
FM(IP0SR2_23_20)	IP0SR2_23_20	FM(IP1SR2_23_20)	IP1SR2_23_20	FM(IP2SR2_23_20)	IP2SR2_23_20 \
FM(IP0SR2_27_24)	IP0SR2_27_24	FM(IP1SR2_27_24)	IP1SR2_27_24	FM(IP2SR2_27_24)	IP2SR2_27_24 \
FM(IP0SR2_31_28)	IP0SR2_31_28	FM(IP1SR2_31_28)	IP1SR2_31_28	FM(IP2SR2_31_28)	IP2SR2_31_28 \
\
FM(IP0SR3_3_0)		IP0SR3_3_0	FM(IP1SR3_3_0)		IP1SR3_3_0	\
FM(IP0SR3_7_4)		IP0SR3_7_4	FM(IP1SR3_7_4)		IP1SR3_7_4	\
FM(IP0SR3_11_8)		IP0SR3_11_8	FM(IP1SR3_11_8)		IP1SR3_11_8	\
FM(IP0SR3_15_12)	IP0SR3_15_12	FM(IP1SR3_15_12)	IP1SR3_15_12	\
FM(IP0SR3_19_16)	IP0SR3_19_16	FM(IP1SR3_19_16)	IP1SR3_19_16	\
FM(IP0SR3_23_20)	IP0SR3_23_20	FM(IP1SR3_23_20)	IP1SR3_23_20	\
FM(IP0SR3_27_24)	IP0SR3_27_24	FM(IP1SR3_27_24)	IP1SR3_27_24	\
FM(IP0SR3_31_28)	IP0SR3_31_28	FM(IP1SR3_31_28)	IP1SR3_31_28	\
\
FM(IP0SR4_3_0)		IP0SR4_3_0	FM(IP1SR4_3_0)		IP1SR4_3_0	FM(IP2SR4_3_0)		IP2SR4_3_0 \
FM(IP0SR4_7_4)		IP0SR4_7_4	FM(IP1SR4_7_4)		IP1SR4_7_4	FM(IP2SR4_7_4)		IP2SR4_7_4 \
FM(IP0SR4_11_8)		IP0SR4_11_8	FM(IP1SR4_11_8)		IP1SR4_11_8	FM(IP2SR4_11_8)		IP2SR4_11_8 \
FM(IP0SR4_15_12)	IP0SR4_15_12	FM(IP1SR4_15_12)	IP1SR4_15_12	FM(IP2SR4_15_12)	IP2SR4_15_12 \
FM(IP0SR4_19_16)	IP0SR4_19_16	FM(IP1SR4_19_16)	IP1SR4_19_16	FM(IP2SR4_19_16)	IP2SR4_19_16 \
FM(IP0SR4_23_20)	IP0SR4_23_20	FM(IP1SR4_23_20)	IP1SR4_23_20	FM(IP2SR4_23_20)	IP2SR4_23_20 \
FM(IP0SR4_27_24)	IP0SR4_27_24	FM(IP1SR4_27_24)	IP1SR4_27_24	FM(IP2SR4_27_24)	IP2SR4_27_24 \
FM(IP0SR4_31_28)	IP0SR4_31_28	FM(IP1SR4_31_28)	IP1SR4_31_28	FM(IP2SR4_31_28)	IP2SR4_31_28 \
\
FM(IP0SR5_3_0)		IP0SR5_3_0	FM(IP1SR5_3_0)		IP1SR5_3_0	FM(IP2SR5_3_0)		IP2SR5_3_0 \
FM(IP0SR5_7_4)		IP0SR5_7_4	FM(IP1SR5_7_4)		IP1SR5_7_4	FM(IP2SR5_7_4)		IP2SR5_7_4 \
FM(IP0SR5_11_8)		IP0SR5_11_8	FM(IP1SR5_11_8)		IP1SR5_11_8	FM(IP2SR5_11_8)		IP2SR5_11_8 \
FM(IP0SR5_15_12)	IP0SR5_15_12	FM(IP1SR5_15_12)	IP1SR5_15_12	FM(IP2SR5_15_12)	IP2SR5_15_12 \
FM(IP0SR5_19_16)	IP0SR5_19_16	FM(IP1SR5_19_16)	IP1SR5_19_16	FM(IP2SR5_19_16)	IP2SR5_19_16 \
FM(IP0SR5_23_20)	IP0SR5_23_20	FM(IP1SR5_23_20)	IP1SR5_23_20	FM(IP2SR5_23_20)	IP2SR5_23_20 \
FM(IP0SR5_27_24)	IP0SR5_27_24	FM(IP1SR5_27_24)	IP1SR5_27_24	FM(IP2SR5_27_24)	IP2SR5_27_24 \
FM(IP0SR5_31_28)	IP0SR5_31_28	FM(IP1SR5_31_28)	IP1SR5_31_28	FM(IP2SR5_31_28)	IP2SR5_31_28

/* MOD_SEL2 */			/* 0 */		/* 1 */		/* 2 */		/* 3 */
#define MOD_SEL2_14_15		FM(SEL_I2C6_0)	F_(0, 0)	F_(0, 0)	FM(SEL_I2C6_3)
#define MOD_SEL2_12_13		FM(SEL_I2C5_0)	F_(0, 0)	F_(0, 0)	FM(SEL_I2C5_3)
#define MOD_SEL2_10_11		FM(SEL_I2C4_0)	F_(0, 0)	F_(0, 0)	FM(SEL_I2C4_3)
#define MOD_SEL2_8_9		FM(SEL_I2C3_0)	F_(0, 0)	F_(0, 0)	FM(SEL_I2C3_3)
#define MOD_SEL2_6_7		FM(SEL_I2C2_0)	F_(0, 0)	F_(0, 0)	FM(SEL_I2C2_3)
#define MOD_SEL2_4_5		FM(SEL_I2C1_0)	F_(0, 0)	F_(0, 0)	FM(SEL_I2C1_3)
#define MOD_SEL2_2_3		FM(SEL_I2C0_0)	F_(0, 0)	F_(0, 0)	FM(SEL_I2C0_3)

#define PINMUX_MOD_SELS \
\
MOD_SEL2_14_15 \
MOD_SEL2_12_13 \
MOD_SEL2_10_11 \
MOD_SEL2_8_9 \
MOD_SEL2_6_7 \
MOD_SEL2_4_5 \
MOD_SEL2_2_3

#define PINMUX_PHYS \
	FM(SCL0) FM(SDA0) FM(SCL1) FM(SDA1) FM(SCL2) FM(SDA2) FM(SCL3) FM(SDA3) \
	FM(SCL4) FM(SDA4) FM(SCL5) FM(SDA5) FM(SCL6) FM(SDA6)

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
	PINMUX_PHYS
	PINMUX_MARK_END,
#undef F_
#undef FM
};

static const u16 pinmux_data[] = {
	PINMUX_DATA_GP_ALL(),

	PINMUX_SINGLE(MMC_D7),
	PINMUX_SINGLE(MMC_D6),
	PINMUX_SINGLE(MMC_D5),
	PINMUX_SINGLE(MMC_D4),
	PINMUX_SINGLE(MMC_SD_CLK),
	PINMUX_SINGLE(MMC_SD_D3),
	PINMUX_SINGLE(MMC_SD_D2),
	PINMUX_SINGLE(MMC_SD_D1),
	PINMUX_SINGLE(MMC_SD_D0),
	PINMUX_SINGLE(MMC_SD_CMD),
	PINMUX_SINGLE(MMC_DS),

	PINMUX_SINGLE(SD_CD),
	PINMUX_SINGLE(SD_WP),

	PINMUX_SINGLE(RPC_INT_N),
	PINMUX_SINGLE(RPC_WP_N),
	PINMUX_SINGLE(RPC_RESET_N),

	PINMUX_SINGLE(QSPI1_SSL),
	PINMUX_SINGLE(QSPI1_IO3),
	PINMUX_SINGLE(QSPI1_IO2),
	PINMUX_SINGLE(QSPI1_MISO_IO1),
	PINMUX_SINGLE(QSPI1_MOSI_IO0),
	PINMUX_SINGLE(QSPI1_SPCLK),
	PINMUX_SINGLE(QSPI0_SSL),
	PINMUX_SINGLE(QSPI0_IO3),
	PINMUX_SINGLE(QSPI0_IO2),
	PINMUX_SINGLE(QSPI0_MISO_IO1),
	PINMUX_SINGLE(QSPI0_MOSI_IO0),
	PINMUX_SINGLE(QSPI0_SPCLK),

	PINMUX_SINGLE(TCLK2_A),

	PINMUX_SINGLE(CANFD7_RX),
	PINMUX_SINGLE(CANFD7_TX),
	PINMUX_SINGLE(CANFD6_RX),
	PINMUX_SINGLE(CANFD1_RX),
	PINMUX_SINGLE(CANFD1_TX),
	PINMUX_SINGLE(CAN_CLK),

	PINMUX_SINGLE(AVS1),
	PINMUX_SINGLE(AVS0),

	PINMUX_SINGLE(PCIE3_CLKREQ_N),
	PINMUX_SINGLE(PCIE2_CLKREQ_N),
	PINMUX_SINGLE(PCIE1_CLKREQ_N),
	PINMUX_SINGLE(PCIE0_CLKREQ_N),

	PINMUX_SINGLE(AVB0_PHY_INT),
	PINMUX_SINGLE(AVB0_MAGIC),
	PINMUX_SINGLE(AVB0_MDC),
	PINMUX_SINGLE(AVB0_MDIO),
	PINMUX_SINGLE(AVB0_TXCREFCLK),

	PINMUX_SINGLE(AVB1_PHY_INT),
	PINMUX_SINGLE(AVB1_MAGIC),
	PINMUX_SINGLE(AVB1_MDC),
	PINMUX_SINGLE(AVB1_MDIO),
	PINMUX_SINGLE(AVB1_TXCREFCLK),

	PINMUX_SINGLE(AVB2_AVTP_PPS),
	PINMUX_SINGLE(AVB2_AVTP_CAPTURE),
	PINMUX_SINGLE(AVB2_AVTP_MATCH),
	PINMUX_SINGLE(AVB2_LINK),
	PINMUX_SINGLE(AVB2_PHY_INT),
	PINMUX_SINGLE(AVB2_MAGIC),
	PINMUX_SINGLE(AVB2_MDC),
	PINMUX_SINGLE(AVB2_MDIO),
	PINMUX_SINGLE(AVB2_TXCREFCLK),
	PINMUX_SINGLE(AVB2_TD3),
	PINMUX_SINGLE(AVB2_TD2),
	PINMUX_SINGLE(AVB2_TD1),
	PINMUX_SINGLE(AVB2_TD0),
	PINMUX_SINGLE(AVB2_TXC),
	PINMUX_SINGLE(AVB2_TX_CTL),
	PINMUX_SINGLE(AVB2_RD3),
	PINMUX_SINGLE(AVB2_RD2),
	PINMUX_SINGLE(AVB2_RD1),
	PINMUX_SINGLE(AVB2_RD0),
	PINMUX_SINGLE(AVB2_RXC),
	PINMUX_SINGLE(AVB2_RX_CTL),

	PINMUX_SINGLE(AVB3_AVTP_PPS),
	PINMUX_SINGLE(AVB3_AVTP_CAPTURE),
	PINMUX_SINGLE(AVB3_AVTP_MATCH),
	PINMUX_SINGLE(AVB3_LINK),
	PINMUX_SINGLE(AVB3_PHY_INT),
	PINMUX_SINGLE(AVB3_MAGIC),
	PINMUX_SINGLE(AVB3_MDC),
	PINMUX_SINGLE(AVB3_MDIO),
	PINMUX_SINGLE(AVB3_TXCREFCLK),
	PINMUX_SINGLE(AVB3_TD3),
	PINMUX_SINGLE(AVB3_TD2),
	PINMUX_SINGLE(AVB3_TD1),
	PINMUX_SINGLE(AVB3_TD0),
	PINMUX_SINGLE(AVB3_TXC),
	PINMUX_SINGLE(AVB3_TX_CTL),
	PINMUX_SINGLE(AVB3_RD3),
	PINMUX_SINGLE(AVB3_RD2),
	PINMUX_SINGLE(AVB3_RD1),
	PINMUX_SINGLE(AVB3_RD0),
	PINMUX_SINGLE(AVB3_RXC),
	PINMUX_SINGLE(AVB3_RX_CTL),

	PINMUX_SINGLE(AVB4_AVTP_PPS),
	PINMUX_SINGLE(AVB4_AVTP_CAPTURE),
	PINMUX_SINGLE(AVB4_AVTP_MATCH),
	PINMUX_SINGLE(AVB4_LINK),
	PINMUX_SINGLE(AVB4_PHY_INT),
	PINMUX_SINGLE(AVB4_MAGIC),
	PINMUX_SINGLE(AVB4_MDC),
	PINMUX_SINGLE(AVB4_MDIO),
	PINMUX_SINGLE(AVB4_TXCREFCLK),
	PINMUX_SINGLE(AVB4_TD3),
	PINMUX_SINGLE(AVB4_TD2),
	PINMUX_SINGLE(AVB4_TD1),
	PINMUX_SINGLE(AVB4_TD0),
	PINMUX_SINGLE(AVB4_TXC),
	PINMUX_SINGLE(AVB4_TX_CTL),
	PINMUX_SINGLE(AVB4_RD3),
	PINMUX_SINGLE(AVB4_RD2),
	PINMUX_SINGLE(AVB4_RD1),
	PINMUX_SINGLE(AVB4_RD0),
	PINMUX_SINGLE(AVB4_RXC),
	PINMUX_SINGLE(AVB4_RX_CTL),

	PINMUX_SINGLE(AVB5_AVTP_PPS),
	PINMUX_SINGLE(AVB5_AVTP_CAPTURE),
	PINMUX_SINGLE(AVB5_AVTP_MATCH),
	PINMUX_SINGLE(AVB5_LINK),
	PINMUX_SINGLE(AVB5_PHY_INT),
	PINMUX_SINGLE(AVB5_MAGIC),
	PINMUX_SINGLE(AVB5_MDC),
	PINMUX_SINGLE(AVB5_MDIO),
	PINMUX_SINGLE(AVB5_TXCREFCLK),
	PINMUX_SINGLE(AVB5_TD3),
	PINMUX_SINGLE(AVB5_TD2),
	PINMUX_SINGLE(AVB5_TD1),
	PINMUX_SINGLE(AVB5_TD0),
	PINMUX_SINGLE(AVB5_TXC),
	PINMUX_SINGLE(AVB5_TX_CTL),
	PINMUX_SINGLE(AVB5_RD3),
	PINMUX_SINGLE(AVB5_RD2),
	PINMUX_SINGLE(AVB5_RD1),
	PINMUX_SINGLE(AVB5_RD0),
	PINMUX_SINGLE(AVB5_RXC),
	PINMUX_SINGLE(AVB5_RX_CTL),

	/* IP0SR1 */
	PINMUX_IPSR_GPSR(IP0SR1_3_0,	SCIF_CLK),
	PINMUX_IPSR_GPSR(IP0SR1_3_0,	A0),

	PINMUX_IPSR_GPSR(IP0SR1_7_4,	HRX0),
	PINMUX_IPSR_GPSR(IP0SR1_7_4,	RX0),
	PINMUX_IPSR_GPSR(IP0SR1_7_4,	A1),

	PINMUX_IPSR_GPSR(IP0SR1_11_8,	HSCK0),
	PINMUX_IPSR_GPSR(IP0SR1_11_8,	SCK0),
	PINMUX_IPSR_GPSR(IP0SR1_11_8,	A2),

	PINMUX_IPSR_GPSR(IP0SR1_15_12,	HRTS0_N),
	PINMUX_IPSR_GPSR(IP0SR1_15_12,	RTS0_N),
	PINMUX_IPSR_GPSR(IP0SR1_15_12,	A3),

	PINMUX_IPSR_GPSR(IP0SR1_19_16,	HCTS0_N),
	PINMUX_IPSR_GPSR(IP0SR1_19_16,	CTS0_N),
	PINMUX_IPSR_GPSR(IP0SR1_19_16,	A4),

	PINMUX_IPSR_GPSR(IP0SR1_23_20,	HTX0),
	PINMUX_IPSR_GPSR(IP0SR1_23_20,	TX0),
	PINMUX_IPSR_GPSR(IP0SR1_23_20,	A5),

	PINMUX_IPSR_GPSR(IP0SR1_27_24,	MSIOF0_RXD),
	PINMUX_IPSR_GPSR(IP0SR1_27_24,	DU_DR2),
	PINMUX_IPSR_GPSR(IP0SR1_27_24,	A6),

	PINMUX_IPSR_GPSR(IP0SR1_31_28,	MSIOF0_TXD),
	PINMUX_IPSR_GPSR(IP0SR1_31_28,	DU_DR3),
	PINMUX_IPSR_GPSR(IP0SR1_31_28,	A7),

	/* IP1SR1 */
	PINMUX_IPSR_GPSR(IP1SR1_3_0,	MSIOF0_SCK),
	PINMUX_IPSR_GPSR(IP1SR1_3_0,	DU_DR4),
	PINMUX_IPSR_GPSR(IP1SR1_3_0,	A8),

	PINMUX_IPSR_GPSR(IP1SR1_7_4,	MSIOF0_SYNC),
	PINMUX_IPSR_GPSR(IP1SR1_7_4,	DU_DR5),
	PINMUX_IPSR_GPSR(IP1SR1_7_4,	A9),

	PINMUX_IPSR_GPSR(IP1SR1_11_8,	MSIOF0_SS1),
	PINMUX_IPSR_GPSR(IP1SR1_11_8,	DU_DR6),
	PINMUX_IPSR_GPSR(IP1SR1_11_8,	A10),

	PINMUX_IPSR_GPSR(IP1SR1_15_12,	MSIOF0_SS2),
	PINMUX_IPSR_GPSR(IP1SR1_15_12,	DU_DR7),
	PINMUX_IPSR_GPSR(IP1SR1_15_12,	A11),

	PINMUX_IPSR_GPSR(IP1SR1_19_16,	MSIOF1_RXD),
	PINMUX_IPSR_GPSR(IP1SR1_19_16,	DU_DG2),
	PINMUX_IPSR_GPSR(IP1SR1_19_16,	A12),

	PINMUX_IPSR_GPSR(IP1SR1_23_20,	MSIOF1_TXD),
	PINMUX_IPSR_GPSR(IP1SR1_23_20,	HRX3),
	PINMUX_IPSR_GPSR(IP1SR1_23_20,	SCK3),
	PINMUX_IPSR_GPSR(IP1SR1_23_20,	DU_DG3),
	PINMUX_IPSR_GPSR(IP1SR1_23_20,	A13),

	PINMUX_IPSR_GPSR(IP1SR1_27_24,	MSIOF1_SCK),
	PINMUX_IPSR_GPSR(IP1SR1_27_24,	HSCK3),
	PINMUX_IPSR_GPSR(IP1SR1_27_24,	CTS3_N),
	PINMUX_IPSR_GPSR(IP1SR1_27_24,	DU_DG4),
	PINMUX_IPSR_GPSR(IP1SR1_27_24,	A14),

	PINMUX_IPSR_GPSR(IP1SR1_31_28,	MSIOF1_SYNC),
	PINMUX_IPSR_GPSR(IP1SR1_31_28,	HRTS3_N),
	PINMUX_IPSR_GPSR(IP1SR1_31_28,	RTS3_N),
	PINMUX_IPSR_GPSR(IP1SR1_31_28,	DU_DG5),
	PINMUX_IPSR_GPSR(IP1SR1_31_28,	A15),

	/* IP2SR1 */
	PINMUX_IPSR_GPSR(IP2SR1_3_0,	MSIOF1_SS1),
	PINMUX_IPSR_GPSR(IP2SR1_3_0,	HCTS3_N),
	PINMUX_IPSR_GPSR(IP2SR1_3_0,	RX3),
	PINMUX_IPSR_GPSR(IP2SR1_3_0,	DU_DG6),
	PINMUX_IPSR_GPSR(IP2SR1_3_0,	A16),

	PINMUX_IPSR_GPSR(IP2SR1_7_4,	MSIOF1_SS2),
	PINMUX_IPSR_GPSR(IP2SR1_7_4,	HTX3),
	PINMUX_IPSR_GPSR(IP2SR1_7_4,	TX3),
	PINMUX_IPSR_GPSR(IP2SR1_7_4,	DU_DG7),
	PINMUX_IPSR_GPSR(IP2SR1_7_4,	A17),

	PINMUX_IPSR_GPSR(IP2SR1_11_8,	MSIOF2_RXD),
	PINMUX_IPSR_GPSR(IP2SR1_11_8,	HSCK1),
	PINMUX_IPSR_GPSR(IP2SR1_11_8,	SCK1),
	PINMUX_IPSR_GPSR(IP2SR1_11_8,	DU_DB2),
	PINMUX_IPSR_GPSR(IP2SR1_11_8,	A18),

	PINMUX_IPSR_GPSR(IP2SR1_15_12,	MSIOF2_TXD),
	PINMUX_IPSR_GPSR(IP2SR1_15_12,	HCTS1_N),
	PINMUX_IPSR_GPSR(IP2SR1_15_12,	CTS1_N),
	PINMUX_IPSR_GPSR(IP2SR1_15_12,	DU_DB3),
	PINMUX_IPSR_GPSR(IP2SR1_15_12,	A19),

	PINMUX_IPSR_GPSR(IP2SR1_19_16,	MSIOF2_SCK),
	PINMUX_IPSR_GPSR(IP2SR1_19_16,	HRTS1_N),
	PINMUX_IPSR_GPSR(IP2SR1_19_16,	RTS1_N),
	PINMUX_IPSR_GPSR(IP2SR1_19_16,	DU_DB4),
	PINMUX_IPSR_GPSR(IP2SR1_19_16,	A20),

	PINMUX_IPSR_GPSR(IP2SR1_23_20,	MSIOF2_SYNC),
	PINMUX_IPSR_GPSR(IP2SR1_23_20,	HRX1),
	PINMUX_IPSR_GPSR(IP2SR1_23_20,	RX1_A),
	PINMUX_IPSR_GPSR(IP2SR1_23_20,	DU_DB5),
	PINMUX_IPSR_GPSR(IP2SR1_23_20,	A21),

	PINMUX_IPSR_GPSR(IP2SR1_27_24,	MSIOF2_SS1),
	PINMUX_IPSR_GPSR(IP2SR1_27_24,	HTX1),
	PINMUX_IPSR_GPSR(IP2SR1_27_24,	TX1_A),
	PINMUX_IPSR_GPSR(IP2SR1_27_24,	DU_DB6),
	PINMUX_IPSR_GPSR(IP2SR1_27_24,	A22),

	PINMUX_IPSR_GPSR(IP2SR1_31_28,	MSIOF2_SS2),
	PINMUX_IPSR_GPSR(IP2SR1_31_28,	TCLK1_B),
	PINMUX_IPSR_GPSR(IP2SR1_31_28,	DU_DB7),
	PINMUX_IPSR_GPSR(IP2SR1_31_28,	A23),

	/* IP3SR1 */
	PINMUX_IPSR_GPSR(IP3SR1_3_0,	IRQ0),
	PINMUX_IPSR_GPSR(IP3SR1_3_0,	DU_DOTCLKOUT),
	PINMUX_IPSR_GPSR(IP3SR1_3_0,	A24),

	PINMUX_IPSR_GPSR(IP3SR1_7_4,	IRQ1),
	PINMUX_IPSR_GPSR(IP3SR1_7_4,	DU_HSYNC),
	PINMUX_IPSR_GPSR(IP3SR1_7_4,	A25),

	PINMUX_IPSR_GPSR(IP3SR1_11_8,	IRQ2),
	PINMUX_IPSR_GPSR(IP3SR1_11_8,	DU_VSYNC),
	PINMUX_IPSR_GPSR(IP3SR1_11_8,	CS1_N_A26),

	PINMUX_IPSR_GPSR(IP3SR1_15_12,	IRQ3),
	PINMUX_IPSR_GPSR(IP3SR1_15_12,	DU_ODDF_DISP_CDE),
	PINMUX_IPSR_GPSR(IP3SR1_15_12,	CS0_N),

	PINMUX_IPSR_GPSR(IP3SR1_19_16,	GP1_28),
	PINMUX_IPSR_GPSR(IP3SR1_19_16,	D0),

	PINMUX_IPSR_GPSR(IP3SR1_23_20,	GP1_29),
	PINMUX_IPSR_GPSR(IP3SR1_23_20,	D1),

	PINMUX_IPSR_GPSR(IP3SR1_27_24,	GP1_30),
	PINMUX_IPSR_GPSR(IP3SR1_27_24,	D2),

	/* IP0SR2 */
	PINMUX_IPSR_GPSR(IP0SR2_3_0,	IPC_CLKIN),
	PINMUX_IPSR_GPSR(IP0SR2_3_0,	IPC_CLKEN_IN),
	PINMUX_IPSR_GPSR(IP0SR2_3_0,	DU_DOTCLKIN),

	PINMUX_IPSR_GPSR(IP0SR2_7_4,	IPC_CLKOUT),
	PINMUX_IPSR_GPSR(IP0SR2_7_4,	IPC_CLKEN_OUT),

	/* GP2_02 = SCL0 */
	PINMUX_IPSR_MSEL(IP0SR2_11_8,	GP2_02,	SEL_I2C0_0),
	PINMUX_IPSR_MSEL(IP0SR2_11_8,	D3,	SEL_I2C0_0),
	PINMUX_IPSR_PHYS(IP0SR2_11_8,	SCL0,	SEL_I2C0_3),

	/* GP2_03 = SDA0 */
	PINMUX_IPSR_MSEL(IP0SR2_15_12,	GP2_03,	SEL_I2C0_0),
	PINMUX_IPSR_MSEL(IP0SR2_15_12,	D4,	SEL_I2C0_0),
	PINMUX_IPSR_PHYS(IP0SR2_15_12,	SDA0,	SEL_I2C0_3),

	/* GP2_04 = SCL1 */
	PINMUX_IPSR_MSEL(IP0SR2_19_16,	GP2_04,		SEL_I2C1_0),
	PINMUX_IPSR_MSEL(IP0SR2_19_16,	MSIOF4_RXD,	SEL_I2C1_0),
	PINMUX_IPSR_MSEL(IP0SR2_19_16,	D5,		SEL_I2C1_0),
	PINMUX_IPSR_PHYS(IP0SR2_19_16,	SCL1,		SEL_I2C1_3),

	/* GP2_05 = SDA1 */
	PINMUX_IPSR_MSEL(IP0SR2_23_20,	GP2_05,		SEL_I2C1_0),
	PINMUX_IPSR_MSEL(IP0SR2_23_20,	HSCK2,		SEL_I2C1_0),
	PINMUX_IPSR_MSEL(IP0SR2_23_20,	MSIOF4_TXD,	SEL_I2C1_0),
	PINMUX_IPSR_MSEL(IP0SR2_23_20,	SCK4,		SEL_I2C1_0),
	PINMUX_IPSR_MSEL(IP0SR2_23_20,	D6,		SEL_I2C1_0),
	PINMUX_IPSR_PHYS(IP0SR2_23_20,	SDA1,		SEL_I2C1_3),

	/* GP2_06 = SCL2 */
	PINMUX_IPSR_MSEL(IP0SR2_27_24,	GP2_06,		SEL_I2C2_0),
	PINMUX_IPSR_MSEL(IP0SR2_27_24,	HCTS2_N,	SEL_I2C2_0),
	PINMUX_IPSR_MSEL(IP0SR2_27_24,	MSIOF4_SCK,	SEL_I2C2_0),
	PINMUX_IPSR_MSEL(IP0SR2_27_24,	CTS4_N,		SEL_I2C2_0),
	PINMUX_IPSR_MSEL(IP0SR2_27_24,	D7,		SEL_I2C2_0),
	PINMUX_IPSR_PHYS(IP0SR2_27_24,	SCL2,		SEL_I2C2_3),

	/* GP2_07 = SDA2 */
	PINMUX_IPSR_MSEL(IP0SR2_31_28,	GP2_07,		SEL_I2C2_0),
	PINMUX_IPSR_MSEL(IP0SR2_31_28,	HRTS2_N,	SEL_I2C2_0),
	PINMUX_IPSR_MSEL(IP0SR2_31_28,	MSIOF4_SYNC,	SEL_I2C2_0),
	PINMUX_IPSR_MSEL(IP0SR2_31_28,	RTS4_N,		SEL_I2C2_0),
	PINMUX_IPSR_MSEL(IP0SR2_31_28,	D8,		SEL_I2C2_0),
	PINMUX_IPSR_PHYS(IP0SR2_31_28,	SDA2,		SEL_I2C2_3),

	/* GP2_08 = SCL3 */
	PINMUX_IPSR_MSEL(IP1SR2_3_0,	GP2_08,		SEL_I2C3_0),
	PINMUX_IPSR_MSEL(IP1SR2_3_0,	HRX2,		SEL_I2C3_0),
	PINMUX_IPSR_MSEL(IP1SR2_3_0,	MSIOF4_SS1,	SEL_I2C3_0),
	PINMUX_IPSR_MSEL(IP1SR2_3_0,	RX4,		SEL_I2C3_0),
	PINMUX_IPSR_MSEL(IP1SR2_3_0,	D9,		SEL_I2C3_0),
	PINMUX_IPSR_PHYS(IP1SR2_3_0,	SCL3,		SEL_I2C3_3),

	/* GP2_09 = SDA3 */
	PINMUX_IPSR_MSEL(IP1SR2_7_4,	GP2_09,		SEL_I2C3_0),
	PINMUX_IPSR_MSEL(IP1SR2_7_4,	HTX2,		SEL_I2C3_0),
	PINMUX_IPSR_MSEL(IP1SR2_7_4,	MSIOF4_SS2,	SEL_I2C3_0),
	PINMUX_IPSR_MSEL(IP1SR2_7_4,	TX4,		SEL_I2C3_0),
	PINMUX_IPSR_MSEL(IP1SR2_7_4,	D10,		SEL_I2C3_0),
	PINMUX_IPSR_PHYS(IP1SR2_7_4,	SDA3,		SEL_I2C3_3),

	/* GP2_10 = SCL4 */
	PINMUX_IPSR_MSEL(IP1SR2_11_8,	GP2_10,		SEL_I2C4_0),
	PINMUX_IPSR_MSEL(IP1SR2_11_8,	TCLK2_B,	SEL_I2C4_0),
	PINMUX_IPSR_MSEL(IP1SR2_11_8,	MSIOF5_RXD,	SEL_I2C4_0),
	PINMUX_IPSR_MSEL(IP1SR2_11_8,	D11,		SEL_I2C4_0),
	PINMUX_IPSR_PHYS(IP1SR2_11_8,	SCL4,		SEL_I2C4_3),

	/* GP2_11 = SDA4 */
	PINMUX_IPSR_MSEL(IP1SR2_15_12,	GP2_11,		SEL_I2C4_0),
	PINMUX_IPSR_MSEL(IP1SR2_15_12,	TCLK3,		SEL_I2C4_0),
	PINMUX_IPSR_MSEL(IP1SR2_15_12,	MSIOF5_TXD,	SEL_I2C4_0),
	PINMUX_IPSR_MSEL(IP1SR2_15_12,	D12,		SEL_I2C4_0),
	PINMUX_IPSR_PHYS(IP1SR2_15_12,	SDA4,		SEL_I2C4_3),

	/* GP2_12 = SCL5 */
	PINMUX_IPSR_MSEL(IP1SR2_19_16,	GP2_12,		SEL_I2C5_0),
	PINMUX_IPSR_MSEL(IP1SR2_19_16,	TCLK4,		SEL_I2C5_0),
	PINMUX_IPSR_MSEL(IP1SR2_19_16,	MSIOF5_SCK,	SEL_I2C5_0),
	PINMUX_IPSR_MSEL(IP1SR2_19_16,	D13,		SEL_I2C5_0),
	PINMUX_IPSR_PHYS(IP1SR2_19_16,	SCL5,		SEL_I2C5_3),

	/* GP2_13 = SDA5 */
	PINMUX_IPSR_MSEL(IP1SR2_23_20,	GP2_13,		SEL_I2C5_0),
	PINMUX_IPSR_MSEL(IP1SR2_23_20,	MSIOF5_SYNC,	SEL_I2C5_0),
	PINMUX_IPSR_MSEL(IP1SR2_23_20,	D14,		SEL_I2C5_0),
	PINMUX_IPSR_PHYS(IP1SR2_23_20,	SDA5,		SEL_I2C5_3),

	/* GP2_14 = SCL6 */
	PINMUX_IPSR_MSEL(IP1SR2_27_24,	GP2_14,		SEL_I2C6_0),
	PINMUX_IPSR_MSEL(IP1SR2_27_24,	IRQ4,		SEL_I2C6_0),
	PINMUX_IPSR_MSEL(IP1SR2_27_24,	MSIOF5_SS1,	SEL_I2C6_0),
	PINMUX_IPSR_MSEL(IP1SR2_27_24,	D15,		SEL_I2C6_0),
	PINMUX_IPSR_PHYS(IP1SR2_27_24,	SCL6,		SEL_I2C6_3),

	/* GP2_15 = SDA6 */
	PINMUX_IPSR_MSEL(IP1SR2_31_28,	GP2_15,		SEL_I2C6_0),
	PINMUX_IPSR_MSEL(IP1SR2_31_28,	IRQ5,		SEL_I2C6_0),
	PINMUX_IPSR_MSEL(IP1SR2_31_28,	MSIOF5_SS2,	SEL_I2C6_0),
	PINMUX_IPSR_MSEL(IP1SR2_31_28,	CPG_CPCKOUT,	SEL_I2C6_0),
	PINMUX_IPSR_PHYS(IP1SR2_31_28,	SDA6,		SEL_I2C6_3),

	/* IP2SR2 */
	PINMUX_IPSR_GPSR(IP2SR2_3_0,	FXR_TXDA_A),
	PINMUX_IPSR_GPSR(IP2SR2_3_0,	MSIOF3_SS1),

	PINMUX_IPSR_GPSR(IP2SR2_7_4,	RXDA_EXTFXR_A),
	PINMUX_IPSR_GPSR(IP2SR2_7_4,	MSIOF3_SS2),
	PINMUX_IPSR_GPSR(IP2SR2_7_4,	BS_N),

	PINMUX_IPSR_GPSR(IP2SR2_11_8,	FXR_TXDB),
	PINMUX_IPSR_GPSR(IP2SR2_11_8,	MSIOF3_RXD),
	PINMUX_IPSR_GPSR(IP2SR2_11_8,	RD_N),

	PINMUX_IPSR_GPSR(IP2SR2_15_12,	RXDB_EXTFXR),
	PINMUX_IPSR_GPSR(IP2SR2_15_12,	MSIOF3_TXD),
	PINMUX_IPSR_GPSR(IP2SR2_15_12,	WE0_N),

	PINMUX_IPSR_GPSR(IP2SR2_19_16,	CLK_EXTFXR),
	PINMUX_IPSR_GPSR(IP2SR2_19_16,	MSIOF3_SCK),
	PINMUX_IPSR_GPSR(IP2SR2_19_16,	WE1_N),

	PINMUX_IPSR_GPSR(IP2SR2_23_20,	TPU0TO0),
	PINMUX_IPSR_GPSR(IP2SR2_23_20,	MSIOF3_SYNC),
	PINMUX_IPSR_GPSR(IP2SR2_23_20,	RD_WR_N),

	PINMUX_IPSR_GPSR(IP2SR2_27_24,	TPU0TO1),
	PINMUX_IPSR_GPSR(IP2SR2_27_24,	CLKOUT),

	PINMUX_IPSR_GPSR(IP2SR2_31_28,	TCLK1_A),
	PINMUX_IPSR_GPSR(IP2SR2_31_28,	EX_WAIT0),

	/* IP0SR3 */
	PINMUX_IPSR_GPSR(IP0SR3_7_4,	CANFD0_TX),
	PINMUX_IPSR_GPSR(IP0SR3_7_4,	FXR_TXDA_B),
	PINMUX_IPSR_GPSR(IP0SR3_7_4,	TX1_B),

	PINMUX_IPSR_GPSR(IP0SR3_11_8,	CANFD0_RX),
	PINMUX_IPSR_GPSR(IP0SR3_11_8,	RXDA_EXTFXR_B),
	PINMUX_IPSR_GPSR(IP0SR3_11_8,	RX1_B),

	PINMUX_IPSR_GPSR(IP0SR3_23_20,	CANFD2_TX),
	PINMUX_IPSR_GPSR(IP0SR3_23_20,	TPU0TO2),
	PINMUX_IPSR_GPSR(IP0SR3_23_20,	PWM0),

	PINMUX_IPSR_GPSR(IP0SR3_27_24,	CANFD2_RX),
	PINMUX_IPSR_GPSR(IP0SR3_27_24,	TPU0TO3),
	PINMUX_IPSR_GPSR(IP0SR3_27_24,	PWM1),

	PINMUX_IPSR_GPSR(IP0SR3_31_28,	CANFD3_TX),
	PINMUX_IPSR_GPSR(IP0SR3_31_28,	PWM2),

	/* IP1SR3 */
	PINMUX_IPSR_GPSR(IP1SR3_3_0,	CANFD3_RX),
	PINMUX_IPSR_GPSR(IP1SR3_3_0,	PWM3),

	PINMUX_IPSR_GPSR(IP1SR3_7_4,	CANFD4_TX),
	PINMUX_IPSR_GPSR(IP1SR3_7_4,	PWM4),
	PINMUX_IPSR_GPSR(IP1SR3_7_4,	FXR_CLKOUT1),

	PINMUX_IPSR_GPSR(IP1SR3_11_8,	CANFD4_RX),
	PINMUX_IPSR_GPSR(IP1SR3_11_8,	FXR_CLKOUT2),

	PINMUX_IPSR_GPSR(IP1SR3_15_12,	CANFD5_TX),
	PINMUX_IPSR_GPSR(IP1SR3_15_12,	FXR_TXENA_N),

	PINMUX_IPSR_GPSR(IP1SR3_19_16,	CANFD5_RX),
	PINMUX_IPSR_GPSR(IP1SR3_19_16,	FXR_TXENB_N),

	PINMUX_IPSR_GPSR(IP1SR3_23_20,	CANFD6_TX),
	PINMUX_IPSR_GPSR(IP1SR3_23_20,	STPWT_EXTFXR),

	/* IP0SR4 */
	PINMUX_IPSR_GPSR(IP0SR4_3_0,	AVB0_RX_CTL),
	PINMUX_IPSR_GPSR(IP0SR4_3_0,	AVB0_MII_RX_DV),

	PINMUX_IPSR_GPSR(IP0SR4_7_4,	AVB0_RXC),
	PINMUX_IPSR_GPSR(IP0SR4_7_4,	AVB0_MII_RXC),

	PINMUX_IPSR_GPSR(IP0SR4_11_8,	AVB0_RD0),
	PINMUX_IPSR_GPSR(IP0SR4_11_8,	AVB0_MII_RD0),

	PINMUX_IPSR_GPSR(IP0SR4_15_12,	AVB0_RD1),
	PINMUX_IPSR_GPSR(IP0SR4_15_12,	AVB0_MII_RD1),

	PINMUX_IPSR_GPSR(IP0SR4_19_16,	AVB0_RD2),
	PINMUX_IPSR_GPSR(IP0SR4_19_16,	AVB0_MII_RD2),

	PINMUX_IPSR_GPSR(IP0SR4_23_20,	AVB0_RD3),
	PINMUX_IPSR_GPSR(IP0SR4_23_20,	AVB0_MII_RD3),

	PINMUX_IPSR_GPSR(IP0SR4_27_24,	AVB0_TX_CTL),
	PINMUX_IPSR_GPSR(IP0SR4_27_24,	AVB0_MII_TX_EN),

	PINMUX_IPSR_GPSR(IP0SR4_31_28,	AVB0_TXC),
	PINMUX_IPSR_GPSR(IP0SR4_31_28,	AVB0_MII_TXC),

	/* IP1SR4 */
	PINMUX_IPSR_GPSR(IP1SR4_3_0,	AVB0_TD0),
	PINMUX_IPSR_GPSR(IP1SR4_3_0,	AVB0_MII_TD0),

	PINMUX_IPSR_GPSR(IP1SR4_7_4,	AVB0_TD1),
	PINMUX_IPSR_GPSR(IP1SR4_7_4,	AVB0_MII_TD1),

	PINMUX_IPSR_GPSR(IP1SR4_11_8,	AVB0_TD2),
	PINMUX_IPSR_GPSR(IP1SR4_11_8,	AVB0_MII_TD2),

	PINMUX_IPSR_GPSR(IP1SR4_15_12,	AVB0_TD3),
	PINMUX_IPSR_GPSR(IP1SR4_15_12,	AVB0_MII_TD3),

	PINMUX_IPSR_GPSR(IP1SR4_19_16,	AVB0_TXCREFCLK),

	PINMUX_IPSR_GPSR(IP1SR4_23_20,	AVB0_MDIO),

	PINMUX_IPSR_GPSR(IP1SR4_27_24,	AVB0_MDC),

	PINMUX_IPSR_GPSR(IP1SR4_31_28,	AVB0_MAGIC),

	/* IP2SR4 */
	PINMUX_IPSR_GPSR(IP2SR4_7_4,	AVB0_LINK),
	PINMUX_IPSR_GPSR(IP2SR4_7_4,	AVB0_MII_TX_ER),

	PINMUX_IPSR_GPSR(IP2SR4_11_8,	AVB0_AVTP_MATCH),
	PINMUX_IPSR_GPSR(IP2SR4_11_8,	AVB0_MII_RX_ER),
	PINMUX_IPSR_GPSR(IP2SR4_11_8,	CC5_OSCOUT),

	PINMUX_IPSR_GPSR(IP2SR4_15_12,	AVB0_AVTP_CAPTURE),
	PINMUX_IPSR_GPSR(IP2SR4_15_12,	AVB0_MII_CRS),

	PINMUX_IPSR_GPSR(IP2SR4_19_16,	AVB0_AVTP_PPS),
	PINMUX_IPSR_GPSR(IP2SR4_19_16,	AVB0_MII_COL),

	/* IP0SR5 */
	PINMUX_IPSR_GPSR(IP0SR5_3_0,	AVB1_RX_CTL),
	PINMUX_IPSR_GPSR(IP0SR5_3_0,	AVB1_MII_RX_DV),

	PINMUX_IPSR_GPSR(IP0SR5_7_4,	AVB1_RXC),
	PINMUX_IPSR_GPSR(IP0SR5_7_4,	AVB1_MII_RXC),

	PINMUX_IPSR_GPSR(IP0SR5_11_8,	AVB1_RD0),
	PINMUX_IPSR_GPSR(IP0SR5_11_8,	AVB1_MII_RD0),

	PINMUX_IPSR_GPSR(IP0SR5_15_12,	AVB1_RD1),
	PINMUX_IPSR_GPSR(IP0SR5_15_12,	AVB1_MII_RD1),

	PINMUX_IPSR_GPSR(IP0SR5_19_16,	AVB1_RD2),
	PINMUX_IPSR_GPSR(IP0SR5_19_16,	AVB1_MII_RD2),

	PINMUX_IPSR_GPSR(IP0SR5_23_20,	AVB1_RD3),
	PINMUX_IPSR_GPSR(IP0SR5_23_20,	AVB1_MII_RD3),

	PINMUX_IPSR_GPSR(IP0SR5_27_24,	AVB1_TX_CTL),
	PINMUX_IPSR_GPSR(IP0SR5_27_24,	AVB1_MII_TX_EN),

	PINMUX_IPSR_GPSR(IP0SR5_31_28,	AVB1_TXC),
	PINMUX_IPSR_GPSR(IP0SR5_31_28,	AVB1_MII_TXC),

	/* IP1SR5 */
	PINMUX_IPSR_GPSR(IP1SR5_3_0,	AVB1_TD0),
	PINMUX_IPSR_GPSR(IP1SR5_3_0,	AVB1_MII_TD0),

	PINMUX_IPSR_GPSR(IP1SR5_7_4,	AVB1_TD1),
	PINMUX_IPSR_GPSR(IP1SR5_7_4,	AVB1_MII_TD1),

	PINMUX_IPSR_GPSR(IP1SR5_11_8,	AVB1_TD2),
	PINMUX_IPSR_GPSR(IP1SR5_11_8,	AVB1_MII_TD2),

	PINMUX_IPSR_GPSR(IP1SR5_15_12,	AVB1_TD3),
	PINMUX_IPSR_GPSR(IP1SR5_15_12,	AVB1_MII_TD3),

	PINMUX_IPSR_GPSR(IP1SR5_19_16,	AVB1_TXCREFCLK),

	PINMUX_IPSR_GPSR(IP1SR5_23_20,	AVB1_MDIO),

	PINMUX_IPSR_GPSR(IP1SR5_27_24,	AVB1_MDC),

	PINMUX_IPSR_GPSR(IP1SR5_31_28,	AVB1_MAGIC),

	/* IP2SR5 */
	PINMUX_IPSR_GPSR(IP2SR5_7_4,	AVB1_LINK),
	PINMUX_IPSR_GPSR(IP2SR5_7_4,	AVB1_MII_TX_ER),

	PINMUX_IPSR_GPSR(IP2SR5_11_8,	AVB1_AVTP_MATCH),
	PINMUX_IPSR_GPSR(IP2SR5_11_8,	AVB1_MII_RX_ER),

	PINMUX_IPSR_GPSR(IP2SR5_15_12,	AVB1_AVTP_CAPTURE),
	PINMUX_IPSR_GPSR(IP2SR5_15_12,	AVB1_MII_CRS),

	PINMUX_IPSR_GPSR(IP2SR5_19_16,	AVB1_AVTP_PPS),
	PINMUX_IPSR_GPSR(IP2SR5_19_16,	AVB1_MII_COL),
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
};

/* - AVB0 ------------------------------------------------ */
static const unsigned int avb0_link_pins[] = {
	/* AVB0_LINK */
	RCAR_GP_PIN(4, 17),
};
static const unsigned int avb0_link_mux[] = {
	AVB0_LINK_MARK,
};
static const unsigned int avb0_magic_pins[] = {
	/* AVB0_MAGIC */
	RCAR_GP_PIN(4, 15),
};
static const unsigned int avb0_magic_mux[] = {
	AVB0_MAGIC_MARK,
};
static const unsigned int avb0_phy_int_pins[] = {
	/* AVB0_PHY_INT */
	RCAR_GP_PIN(4, 16),
};
static const unsigned int avb0_phy_int_mux[] = {
	AVB0_PHY_INT_MARK,
};
static const unsigned int avb0_mdio_pins[] = {
	/* AVB0_MDC, AVB0_MDIO */
	RCAR_GP_PIN(4, 14), RCAR_GP_PIN(4, 13),
};
static const unsigned int avb0_mdio_mux[] = {
	AVB0_MDC_MARK, AVB0_MDIO_MARK,
};
static const unsigned int avb0_rgmii_pins[] = {
	/*
	 * AVB0_TX_CTL, AVB0_TXC, AVB0_TD0, AVB0_TD1, AVB0_TD2, AVB0_TD3,
	 * AVB0_RX_CTL, AVB0_RXC, AVB0_RD0, AVB0_RD1, AVB0_RD2, AVB0_RD3,
	 */
	RCAR_GP_PIN(4, 6), RCAR_GP_PIN(4, 7),
	RCAR_GP_PIN(4, 8), RCAR_GP_PIN(4, 9),
	RCAR_GP_PIN(4, 10), RCAR_GP_PIN(4, 11),
	RCAR_GP_PIN(4, 0), RCAR_GP_PIN(4, 1),
	RCAR_GP_PIN(4, 2), RCAR_GP_PIN(4, 3),
	RCAR_GP_PIN(4, 4), RCAR_GP_PIN(4, 5),
};
static const unsigned int avb0_rgmii_mux[] = {
	AVB0_TX_CTL_MARK, AVB0_TXC_MARK,
	AVB0_TD0_MARK, AVB0_TD1_MARK, AVB0_TD2_MARK, AVB0_TD3_MARK,
	AVB0_RX_CTL_MARK, AVB0_RXC_MARK,
	AVB0_RD0_MARK, AVB0_RD1_MARK, AVB0_RD2_MARK, AVB0_RD3_MARK,
};
static const unsigned int avb0_txcrefclk_pins[] = {
	/* AVB0_TXCREFCLK */
	RCAR_GP_PIN(4, 12),
};
static const unsigned int avb0_txcrefclk_mux[] = {
	AVB0_TXCREFCLK_MARK,
};
static const unsigned int avb0_avtp_pps_pins[] = {
	/* AVB0_AVTP_PPS */
	RCAR_GP_PIN(4, 20),
};
static const unsigned int avb0_avtp_pps_mux[] = {
	AVB0_AVTP_PPS_MARK,
};
static const unsigned int avb0_avtp_capture_pins[] = {
	/* AVB0_AVTP_CAPTURE */
	RCAR_GP_PIN(4, 19),
};
static const unsigned int avb0_avtp_capture_mux[] = {
	AVB0_AVTP_CAPTURE_MARK,
};
static const unsigned int avb0_avtp_match_pins[] = {
	/* AVB0_AVTP_MATCH */
	RCAR_GP_PIN(4, 18),
};
static const unsigned int avb0_avtp_match_mux[] = {
	AVB0_AVTP_MATCH_MARK,
};

/* - AVB1 ------------------------------------------------ */
static const unsigned int avb1_link_pins[] = {
	/* AVB1_LINK */
	RCAR_GP_PIN(5, 17),
};
static const unsigned int avb1_link_mux[] = {
	AVB1_LINK_MARK,
};
static const unsigned int avb1_magic_pins[] = {
	/* AVB1_MAGIC */
	RCAR_GP_PIN(5, 15),
};
static const unsigned int avb1_magic_mux[] = {
	AVB1_MAGIC_MARK,
};
static const unsigned int avb1_phy_int_pins[] = {
	/* AVB1_PHY_INT */
	RCAR_GP_PIN(5, 16),
};
static const unsigned int avb1_phy_int_mux[] = {
	AVB1_PHY_INT_MARK,
};
static const unsigned int avb1_mdio_pins[] = {
	/* AVB1_MDC, AVB1_MDIO */
	RCAR_GP_PIN(5, 14), RCAR_GP_PIN(5, 13),
};
static const unsigned int avb1_mdio_mux[] = {
	AVB1_MDC_MARK, AVB1_MDIO_MARK,
};
static const unsigned int avb1_rgmii_pins[] = {
	/*
	 * AVB1_TX_CTL, AVB1_TXC, AVB1_TD0, AVB1_TD1, AVB1_TD2, AVB1_TD3,
	 * AVB1_RX_CTL, AVB1_RXC, AVB1_RD0, AVB1_RD1, AVB1_RD2, AVB1_RD3,
	 */
	RCAR_GP_PIN(5, 6), RCAR_GP_PIN(5, 7),
	RCAR_GP_PIN(5, 8), RCAR_GP_PIN(5, 9),
	RCAR_GP_PIN(5, 10), RCAR_GP_PIN(5, 11),
	RCAR_GP_PIN(5, 0), RCAR_GP_PIN(5, 1),
	RCAR_GP_PIN(5, 2), RCAR_GP_PIN(5, 3),
	RCAR_GP_PIN(5, 4), RCAR_GP_PIN(5, 5),
};
static const unsigned int avb1_rgmii_mux[] = {
	AVB1_TX_CTL_MARK, AVB1_TXC_MARK,
	AVB1_TD0_MARK, AVB1_TD1_MARK, AVB1_TD2_MARK, AVB1_TD3_MARK,
	AVB1_RX_CTL_MARK, AVB1_RXC_MARK,
	AVB1_RD0_MARK, AVB1_RD1_MARK, AVB1_RD2_MARK, AVB1_RD3_MARK,
};
static const unsigned int avb1_txcrefclk_pins[] = {
	/* AVB1_TXCREFCLK */
	RCAR_GP_PIN(5, 12),
};
static const unsigned int avb1_txcrefclk_mux[] = {
	AVB1_TXCREFCLK_MARK,
};
static const unsigned int avb1_avtp_pps_pins[] = {
	/* AVB1_AVTP_PPS */
	RCAR_GP_PIN(5, 20),
};
static const unsigned int avb1_avtp_pps_mux[] = {
	AVB1_AVTP_PPS_MARK,
};
static const unsigned int avb1_avtp_capture_pins[] = {
	/* AVB1_AVTP_CAPTURE */
	RCAR_GP_PIN(5, 19),
};
static const unsigned int avb1_avtp_capture_mux[] = {
	AVB1_AVTP_CAPTURE_MARK,
};
static const unsigned int avb1_avtp_match_pins[] = {
	/* AVB1_AVTP_MATCH */
	RCAR_GP_PIN(5, 18),
};
static const unsigned int avb1_avtp_match_mux[] = {
	AVB1_AVTP_MATCH_MARK,
};

/* - AVB2 ------------------------------------------------ */
static const unsigned int avb2_link_pins[] = {
	/* AVB2_LINK */
	RCAR_GP_PIN(6, 17),
};
static const unsigned int avb2_link_mux[] = {
	AVB2_LINK_MARK,
};
static const unsigned int avb2_magic_pins[] = {
	/* AVB2_MAGIC */
	RCAR_GP_PIN(6, 15),
};
static const unsigned int avb2_magic_mux[] = {
	AVB2_MAGIC_MARK,
};
static const unsigned int avb2_phy_int_pins[] = {
	/* AVB2_PHY_INT */
	RCAR_GP_PIN(6, 16),
};
static const unsigned int avb2_phy_int_mux[] = {
	AVB2_PHY_INT_MARK,
};
static const unsigned int avb2_mdio_pins[] = {
	/* AVB2_MDC, AVB2_MDIO */
	RCAR_GP_PIN(6, 14), RCAR_GP_PIN(6, 13),
};
static const unsigned int avb2_mdio_mux[] = {
	AVB2_MDC_MARK, AVB2_MDIO_MARK,
};
static const unsigned int avb2_rgmii_pins[] = {
	/*
	 * AVB2_TX_CTL, AVB2_TXC, AVB2_TD0, AVB2_TD1, AVB2_TD2, AVB2_TD3,
	 * AVB2_RX_CTL, AVB2_RXC, AVB2_RD0, AVB2_RD1, AVB2_RD2, AVB2_RD3,
	 */
	RCAR_GP_PIN(6, 6), RCAR_GP_PIN(6, 7),
	RCAR_GP_PIN(6, 8), RCAR_GP_PIN(6, 9),
	RCAR_GP_PIN(6, 10), RCAR_GP_PIN(6, 11),
	RCAR_GP_PIN(6, 0), RCAR_GP_PIN(6, 1),
	RCAR_GP_PIN(6, 2), RCAR_GP_PIN(6, 3),
	RCAR_GP_PIN(6, 4), RCAR_GP_PIN(6, 5),
};
static const unsigned int avb2_rgmii_mux[] = {
	AVB2_TX_CTL_MARK, AVB2_TXC_MARK,
	AVB2_TD0_MARK, AVB2_TD1_MARK, AVB2_TD2_MARK, AVB2_TD3_MARK,
	AVB2_RX_CTL_MARK, AVB2_RXC_MARK,
	AVB2_RD0_MARK, AVB2_RD1_MARK, AVB2_RD2_MARK, AVB2_RD3_MARK,
};
static const unsigned int avb2_txcrefclk_pins[] = {
	/* AVB2_TXCREFCLK */
	RCAR_GP_PIN(6, 12),
};
static const unsigned int avb2_txcrefclk_mux[] = {
	AVB2_TXCREFCLK_MARK,
};
static const unsigned int avb2_avtp_pps_pins[] = {
	/* AVB2_AVTP_PPS */
	RCAR_GP_PIN(6, 20),
};
static const unsigned int avb2_avtp_pps_mux[] = {
	AVB2_AVTP_PPS_MARK,
};
static const unsigned int avb2_avtp_capture_pins[] = {
	/* AVB2_AVTP_CAPTURE */
	RCAR_GP_PIN(6, 19),
};
static const unsigned int avb2_avtp_capture_mux[] = {
	AVB2_AVTP_CAPTURE_MARK,
};
static const unsigned int avb2_avtp_match_pins[] = {
	/* AVB2_AVTP_MATCH */
	RCAR_GP_PIN(6, 18),
};
static const unsigned int avb2_avtp_match_mux[] = {
	AVB2_AVTP_MATCH_MARK,
};

/* - AVB3 ------------------------------------------------ */
static const unsigned int avb3_link_pins[] = {
	/* AVB3_LINK */
	RCAR_GP_PIN(7, 17),
};
static const unsigned int avb3_link_mux[] = {
	AVB3_LINK_MARK,
};
static const unsigned int avb3_magic_pins[] = {
	/* AVB3_MAGIC */
	RCAR_GP_PIN(7, 15),
};
static const unsigned int avb3_magic_mux[] = {
	AVB3_MAGIC_MARK,
};
static const unsigned int avb3_phy_int_pins[] = {
	/* AVB3_PHY_INT */
	RCAR_GP_PIN(7, 16),
};
static const unsigned int avb3_phy_int_mux[] = {
	AVB3_PHY_INT_MARK,
};
static const unsigned int avb3_mdio_pins[] = {
	/* AVB3_MDC, AVB3_MDIO */
	RCAR_GP_PIN(7, 14), RCAR_GP_PIN(7, 13),
};
static const unsigned int avb3_mdio_mux[] = {
	AVB3_MDC_MARK, AVB3_MDIO_MARK,
};
static const unsigned int avb3_rgmii_pins[] = {
	/*
	 * AVB3_TX_CTL, AVB3_TXC, AVB3_TD0, AVB3_TD1, AVB3_TD2, AVB3_TD3,
	 * AVB3_RX_CTL, AVB3_RXC, AVB3_RD0, AVB3_RD1, AVB3_RD2, AVB3_RD3,
	 */
	RCAR_GP_PIN(7, 6), RCAR_GP_PIN(7, 7),
	RCAR_GP_PIN(7, 8), RCAR_GP_PIN(7, 9),
	RCAR_GP_PIN(7, 10), RCAR_GP_PIN(7, 11),
	RCAR_GP_PIN(7, 0), RCAR_GP_PIN(7, 1),
	RCAR_GP_PIN(7, 2), RCAR_GP_PIN(7, 3),
	RCAR_GP_PIN(7, 4), RCAR_GP_PIN(7, 5),
};
static const unsigned int avb3_rgmii_mux[] = {
	AVB3_TX_CTL_MARK, AVB3_TXC_MARK,
	AVB3_TD0_MARK, AVB3_TD1_MARK, AVB3_TD2_MARK, AVB3_TD3_MARK,
	AVB3_RX_CTL_MARK, AVB3_RXC_MARK,
	AVB3_RD0_MARK, AVB3_RD1_MARK, AVB3_RD2_MARK, AVB3_RD3_MARK,
};
static const unsigned int avb3_txcrefclk_pins[] = {
	/* AVB3_TXCREFCLK */
	RCAR_GP_PIN(7, 12),
};
static const unsigned int avb3_txcrefclk_mux[] = {
	AVB3_TXCREFCLK_MARK,
};
static const unsigned int avb3_avtp_pps_pins[] = {
	/* AVB3_AVTP_PPS */
	RCAR_GP_PIN(7, 20),
};
static const unsigned int avb3_avtp_pps_mux[] = {
	AVB3_AVTP_PPS_MARK,
};
static const unsigned int avb3_avtp_capture_pins[] = {
	/* AVB3_AVTP_CAPTURE */
	RCAR_GP_PIN(7, 19),
};
static const unsigned int avb3_avtp_capture_mux[] = {
	AVB3_AVTP_CAPTURE_MARK,
};
static const unsigned int avb3_avtp_match_pins[] = {
	/* AVB3_AVTP_MATCH */
	RCAR_GP_PIN(7, 18),
};
static const unsigned int avb3_avtp_match_mux[] = {
	AVB3_AVTP_MATCH_MARK,
};

/* - AVB4 ------------------------------------------------ */
static const unsigned int avb4_link_pins[] = {
	/* AVB4_LINK */
	RCAR_GP_PIN(8, 17),
};
static const unsigned int avb4_link_mux[] = {
	AVB4_LINK_MARK,
};
static const unsigned int avb4_magic_pins[] = {
	/* AVB4_MAGIC */
	RCAR_GP_PIN(8, 15),
};
static const unsigned int avb4_magic_mux[] = {
	AVB4_MAGIC_MARK,
};
static const unsigned int avb4_phy_int_pins[] = {
	/* AVB4_PHY_INT */
	RCAR_GP_PIN(8, 16),
};
static const unsigned int avb4_phy_int_mux[] = {
	AVB4_PHY_INT_MARK,
};
static const unsigned int avb4_mdio_pins[] = {
	/* AVB4_MDC, AVB4_MDIO */
	RCAR_GP_PIN(8, 14), RCAR_GP_PIN(8, 13),
};
static const unsigned int avb4_mdio_mux[] = {
	AVB4_MDC_MARK, AVB4_MDIO_MARK,
};
static const unsigned int avb4_rgmii_pins[] = {
	/*
	 * AVB4_TX_CTL, AVB4_TXC, AVB4_TD0, AVB4_TD1, AVB4_TD2, AVB4_TD3,
	 * AVB4_RX_CTL, AVB4_RXC, AVB4_RD0, AVB4_RD1, AVB4_RD2, AVB4_RD3,
	 */
	RCAR_GP_PIN(8, 6), RCAR_GP_PIN(8, 7),
	RCAR_GP_PIN(8, 8), RCAR_GP_PIN(8, 9),
	RCAR_GP_PIN(8, 10), RCAR_GP_PIN(8, 11),
	RCAR_GP_PIN(8, 0), RCAR_GP_PIN(8, 1),
	RCAR_GP_PIN(8, 2), RCAR_GP_PIN(8, 3),
	RCAR_GP_PIN(8, 4), RCAR_GP_PIN(8, 5),
};
static const unsigned int avb4_rgmii_mux[] = {
	AVB4_TX_CTL_MARK, AVB4_TXC_MARK,
	AVB4_TD0_MARK, AVB4_TD1_MARK, AVB4_TD2_MARK, AVB4_TD3_MARK,
	AVB4_RX_CTL_MARK, AVB4_RXC_MARK,
	AVB4_RD0_MARK, AVB4_RD1_MARK, AVB4_RD2_MARK, AVB4_RD3_MARK,
};
static const unsigned int avb4_txcrefclk_pins[] = {
	/* AVB4_TXCREFCLK */
	RCAR_GP_PIN(8, 12),
};
static const unsigned int avb4_txcrefclk_mux[] = {
	AVB4_TXCREFCLK_MARK,
};
static const unsigned int avb4_avtp_pps_pins[] = {
	/* AVB4_AVTP_PPS */
	RCAR_GP_PIN(8, 20),
};
static const unsigned int avb4_avtp_pps_mux[] = {
	AVB4_AVTP_PPS_MARK,
};
static const unsigned int avb4_avtp_capture_pins[] = {
	/* AVB4_AVTP_CAPTURE */
	RCAR_GP_PIN(8, 19),
};
static const unsigned int avb4_avtp_capture_mux[] = {
	AVB4_AVTP_CAPTURE_MARK,
};
static const unsigned int avb4_avtp_match_pins[] = {
	/* AVB4_AVTP_MATCH */
	RCAR_GP_PIN(8, 18),
};
static const unsigned int avb4_avtp_match_mux[] = {
	AVB4_AVTP_MATCH_MARK,
};

/* - AVB5 ------------------------------------------------ */
static const unsigned int avb5_link_pins[] = {
	/* AVB5_LINK */
	RCAR_GP_PIN(9, 17),
};
static const unsigned int avb5_link_mux[] = {
	AVB5_LINK_MARK,
};
static const unsigned int avb5_magic_pins[] = {
	/* AVB5_MAGIC */
	RCAR_GP_PIN(9, 15),
};
static const unsigned int avb5_magic_mux[] = {
	AVB5_MAGIC_MARK,
};
static const unsigned int avb5_phy_int_pins[] = {
	/* AVB5_PHY_INT */
	RCAR_GP_PIN(9, 16),
};
static const unsigned int avb5_phy_int_mux[] = {
	AVB5_PHY_INT_MARK,
};
static const unsigned int avb5_mdio_pins[] = {
	/* AVB5_MDC, AVB5_MDIO */
	RCAR_GP_PIN(9, 14), RCAR_GP_PIN(9, 13),
};
static const unsigned int avb5_mdio_mux[] = {
	AVB5_MDC_MARK, AVB5_MDIO_MARK,
};
static const unsigned int avb5_rgmii_pins[] = {
	/*
	 * AVB5_TX_CTL, AVB5_TXC, AVB5_TD0, AVB5_TD1, AVB5_TD2, AVB5_TD3,
	 * AVB5_RX_CTL, AVB5_RXC, AVB5_RD0, AVB5_RD1, AVB5_RD2, AVB5_RD3,
	 */
	RCAR_GP_PIN(9, 6), RCAR_GP_PIN(9, 7),
	RCAR_GP_PIN(9, 8), RCAR_GP_PIN(9, 9),
	RCAR_GP_PIN(9, 10), RCAR_GP_PIN(9, 11),
	RCAR_GP_PIN(9, 0), RCAR_GP_PIN(9, 1),
	RCAR_GP_PIN(9, 2), RCAR_GP_PIN(9, 3),
	RCAR_GP_PIN(9, 4), RCAR_GP_PIN(9, 5),
};
static const unsigned int avb5_rgmii_mux[] = {
	AVB5_TX_CTL_MARK, AVB5_TXC_MARK,
	AVB5_TD0_MARK, AVB5_TD1_MARK, AVB5_TD2_MARK, AVB5_TD3_MARK,
	AVB5_RX_CTL_MARK, AVB5_RXC_MARK,
	AVB5_RD0_MARK, AVB5_RD1_MARK, AVB5_RD2_MARK, AVB5_RD3_MARK,
};
static const unsigned int avb5_txcrefclk_pins[] = {
	/* AVB5_TXCREFCLK */
	RCAR_GP_PIN(9, 12),
};
static const unsigned int avb5_txcrefclk_mux[] = {
	AVB5_TXCREFCLK_MARK,
};
static const unsigned int avb5_avtp_pps_pins[] = {
	/* AVB5_AVTP_PPS */
	RCAR_GP_PIN(9, 20),
};
static const unsigned int avb5_avtp_pps_mux[] = {
	AVB5_AVTP_PPS_MARK,
};
static const unsigned int avb5_avtp_capture_pins[] = {
	/* AVB5_AVTP_CAPTURE */
	RCAR_GP_PIN(9, 19),
};
static const unsigned int avb5_avtp_capture_mux[] = {
	AVB5_AVTP_CAPTURE_MARK,
};
static const unsigned int avb5_avtp_match_pins[] = {
	/* AVB5_AVTP_MATCH */
	RCAR_GP_PIN(9, 18),
};
static const unsigned int avb5_avtp_match_mux[] = {
	AVB5_AVTP_MATCH_MARK,
};

/* - CANFD0 ----------------------------------------------------------------- */
static const unsigned int canfd0_data_pins[] = {
	/* CANFD0_TX, CANFD0_RX */
	RCAR_GP_PIN(3, 1), RCAR_GP_PIN(3, 2),
};
static const unsigned int canfd0_data_mux[] = {
	CANFD0_TX_MARK, CANFD0_RX_MARK,
};

/* - CANFD1 ----------------------------------------------------------------- */
static const unsigned int canfd1_data_pins[] = {
	/* CANFD1_TX, CANFD1_RX */
	RCAR_GP_PIN(3, 3), RCAR_GP_PIN(3, 4),
};
static const unsigned int canfd1_data_mux[] = {
	CANFD1_TX_MARK, CANFD1_RX_MARK,
};

/* - CANFD2 ----------------------------------------------------------------- */
static const unsigned int canfd2_data_pins[] = {
	/* CANFD2_TX, CANFD2_RX */
	RCAR_GP_PIN(3, 5), RCAR_GP_PIN(3, 6),
};
static const unsigned int canfd2_data_mux[] = {
	CANFD2_TX_MARK, CANFD2_RX_MARK,
};

/* - CANFD3 ----------------------------------------------------------------- */
static const unsigned int canfd3_data_pins[] = {
	/* CANFD3_TX, CANFD3_RX */
	RCAR_GP_PIN(3, 7), RCAR_GP_PIN(3, 8),
};
static const unsigned int canfd3_data_mux[] = {
	CANFD3_TX_MARK, CANFD3_RX_MARK,
};

/* - CANFD4 ----------------------------------------------------------------- */
static const unsigned int canfd4_data_pins[] = {
	/* CANFD4_TX, CANFD4_RX */
	RCAR_GP_PIN(3, 9), RCAR_GP_PIN(3, 10),
};
static const unsigned int canfd4_data_mux[] = {
	CANFD4_TX_MARK, CANFD4_RX_MARK,
};

/* - CANFD5 ----------------------------------------------------------------- */
static const unsigned int canfd5_data_pins[] = {
	/* CANFD5_TX, CANFD5_RX */
	RCAR_GP_PIN(3, 11), RCAR_GP_PIN(3, 12),
};
static const unsigned int canfd5_data_mux[] = {
	CANFD5_TX_MARK, CANFD5_RX_MARK,
};

/* - CANFD6 ----------------------------------------------------------------- */
static const unsigned int canfd6_data_pins[] = {
	/* CANFD6_TX, CANFD6_RX */
	RCAR_GP_PIN(3, 13), RCAR_GP_PIN(3, 14),
};
static const unsigned int canfd6_data_mux[] = {
	CANFD6_TX_MARK, CANFD6_RX_MARK,
};

/* - CANFD7 ----------------------------------------------------------------- */
static const unsigned int canfd7_data_pins[] = {
	/* CANFD7_TX, CANFD7_RX */
	RCAR_GP_PIN(3, 15), RCAR_GP_PIN(3, 16),
};
static const unsigned int canfd7_data_mux[] = {
	CANFD7_TX_MARK, CANFD7_RX_MARK,
};

/* - CANFD Clock ------------------------------------------------------------ */
static const unsigned int can_clk_pins[] = {
	/* CAN_CLK */
	RCAR_GP_PIN(3, 0),
};
static const unsigned int can_clk_mux[] = {
	CAN_CLK_MARK,
};

/* - DU --------------------------------------------------------------------- */
static const unsigned int du_rgb888_pins[] = {
	/* DU_DR[7:2], DU_DG[7:2], DU_DB[7:2] */
	RCAR_GP_PIN(1, 11), RCAR_GP_PIN(1, 10), RCAR_GP_PIN(1, 9),
	RCAR_GP_PIN(1, 8), RCAR_GP_PIN(1, 7), RCAR_GP_PIN(1, 6),
	RCAR_GP_PIN(1, 17), RCAR_GP_PIN(1, 16), RCAR_GP_PIN(1, 15),
	RCAR_GP_PIN(1, 14), RCAR_GP_PIN(1, 13), RCAR_GP_PIN(1, 12),
	RCAR_GP_PIN(1, 23), RCAR_GP_PIN(1, 22), RCAR_GP_PIN(1, 21),
	RCAR_GP_PIN(1, 20), RCAR_GP_PIN(1, 19), RCAR_GP_PIN(1, 18),
};
static const unsigned int du_rgb888_mux[] = {
	DU_DR7_MARK, DU_DR6_MARK, DU_DR5_MARK,
	DU_DR4_MARK, DU_DR3_MARK, DU_DR2_MARK,
	DU_DG7_MARK, DU_DG6_MARK, DU_DG5_MARK,
	DU_DG4_MARK, DU_DG3_MARK, DU_DG2_MARK,
	DU_DB7_MARK, DU_DB6_MARK, DU_DB5_MARK,
	DU_DB4_MARK, DU_DB3_MARK, DU_DB2_MARK,
};
static const unsigned int du_clk_out_pins[] = {
	/* DU_DOTCLKOUT */
	RCAR_GP_PIN(1, 24),
};
static const unsigned int du_clk_out_mux[] = {
	DU_DOTCLKOUT_MARK,
};
static const unsigned int du_sync_pins[] = {
	/* DU_HSYNC, DU_VSYNC */
	RCAR_GP_PIN(1, 25), RCAR_GP_PIN(1, 26),
};
static const unsigned int du_sync_mux[] = {
	DU_HSYNC_MARK, DU_VSYNC_MARK,
};
static const unsigned int du_oddf_pins[] = {
	/* DU_EXODDF/DU_ODDF/DISP/CDE */
	RCAR_GP_PIN(1, 27),
};
static const unsigned int du_oddf_mux[] = {
	DU_ODDF_DISP_CDE_MARK,
};

/* - HSCIF0 ----------------------------------------------------------------- */
static const unsigned int hscif0_data_pins[] = {
	/* HRX0, HTX0 */
	RCAR_GP_PIN(1, 1), RCAR_GP_PIN(1, 5),
};
static const unsigned int hscif0_data_mux[] = {
	HRX0_MARK, HTX0_MARK,
};
static const unsigned int hscif0_clk_pins[] = {
	/* HSCK0 */
	RCAR_GP_PIN(1, 2),
};
static const unsigned int hscif0_clk_mux[] = {
	HSCK0_MARK,
};
static const unsigned int hscif0_ctrl_pins[] = {
	/* HRTS0#, HCTS0# */
	RCAR_GP_PIN(1, 3), RCAR_GP_PIN(1, 4),
};
static const unsigned int hscif0_ctrl_mux[] = {
	HRTS0_N_MARK, HCTS0_N_MARK,
};

/* - HSCIF1 ----------------------------------------------------------------- */
static const unsigned int hscif1_data_pins[] = {
	/* HRX1, HTX1 */
	RCAR_GP_PIN(1, 21), RCAR_GP_PIN(1, 22),
};
static const unsigned int hscif1_data_mux[] = {
	HRX1_MARK, HTX1_MARK,
};
static const unsigned int hscif1_clk_pins[] = {
	/* HSCK1 */
	RCAR_GP_PIN(1, 18),
};
static const unsigned int hscif1_clk_mux[] = {
	HSCK1_MARK,
};
static const unsigned int hscif1_ctrl_pins[] = {
	/* HRTS1#, HCTS1# */
	RCAR_GP_PIN(1, 20), RCAR_GP_PIN(1, 19),
};
static const unsigned int hscif1_ctrl_mux[] = {
	HRTS1_N_MARK, HCTS1_N_MARK,
};

/* - HSCIF2 ----------------------------------------------------------------- */
static const unsigned int hscif2_data_pins[] = {
	/* HRX2, HTX2 */
	RCAR_GP_PIN(2, 8), RCAR_GP_PIN(2, 9),
};
static const unsigned int hscif2_data_mux[] = {
	HRX2_MARK, HTX2_MARK,
};
static const unsigned int hscif2_clk_pins[] = {
	/* HSCK2 */
	RCAR_GP_PIN(2, 5),
};
static const unsigned int hscif2_clk_mux[] = {
	HSCK2_MARK,
};
static const unsigned int hscif2_ctrl_pins[] = {
	/* HRTS2#, HCTS2# */
	RCAR_GP_PIN(2, 7), RCAR_GP_PIN(2, 6),
};
static const unsigned int hscif2_ctrl_mux[] = {
	HRTS2_N_MARK, HCTS2_N_MARK,
};

/* - HSCIF3 ----------------------------------------------------------------- */
static const unsigned int hscif3_data_pins[] = {
	/* HRX3, HTX3 */
	RCAR_GP_PIN(1, 13), RCAR_GP_PIN(1, 17),
};
static const unsigned int hscif3_data_mux[] = {
	HRX3_MARK, HTX3_MARK,
};
static const unsigned int hscif3_clk_pins[] = {
	/* HSCK3 */
	RCAR_GP_PIN(1, 14),
};
static const unsigned int hscif3_clk_mux[] = {
	HSCK3_MARK,
};
static const unsigned int hscif3_ctrl_pins[] = {
	/* HRTS3#, HCTS3# */
	RCAR_GP_PIN(1, 15), RCAR_GP_PIN(1, 16),
};
static const unsigned int hscif3_ctrl_mux[] = {
	HRTS3_N_MARK, HCTS3_N_MARK,
};

/* - I2C0 ------------------------------------------------------------------- */
static const unsigned int i2c0_pins[] = {
	/* SDA0, SCL0 */
	RCAR_GP_PIN(2, 3), RCAR_GP_PIN(2, 2),
};
static const unsigned int i2c0_mux[] = {
	SDA0_MARK, SCL0_MARK,
};

/* - I2C1 ------------------------------------------------------------------- */
static const unsigned int i2c1_pins[] = {
	/* SDA1, SCL1 */
	RCAR_GP_PIN(2, 5), RCAR_GP_PIN(2, 4),
};
static const unsigned int i2c1_mux[] = {
	SDA1_MARK, SCL1_MARK,
};

/* - I2C2 ------------------------------------------------------------------- */
static const unsigned int i2c2_pins[] = {
	/* SDA2, SCL2 */
	RCAR_GP_PIN(2, 7), RCAR_GP_PIN(2, 6),
};
static const unsigned int i2c2_mux[] = {
	SDA2_MARK, SCL2_MARK,
};

/* - I2C3 ------------------------------------------------------------------- */
static const unsigned int i2c3_pins[] = {
	/* SDA3, SCL3 */
	RCAR_GP_PIN(2, 9), RCAR_GP_PIN(2, 8),
};
static const unsigned int i2c3_mux[] = {
	SDA3_MARK, SCL3_MARK,
};

/* - I2C4 ------------------------------------------------------------------- */
static const unsigned int i2c4_pins[] = {
	/* SDA4, SCL4 */
	RCAR_GP_PIN(2, 11), RCAR_GP_PIN(2, 10),
};
static const unsigned int i2c4_mux[] = {
	SDA4_MARK, SCL4_MARK,
};

/* - I2C5 ------------------------------------------------------------------- */
static const unsigned int i2c5_pins[] = {
	/* SDA5, SCL5 */
	RCAR_GP_PIN(2, 13), RCAR_GP_PIN(2, 12),
};
static const unsigned int i2c5_mux[] = {
	SDA5_MARK, SCL5_MARK,
};

/* - I2C6 ------------------------------------------------------------------- */
static const unsigned int i2c6_pins[] = {
	/* SDA6, SCL6 */
	RCAR_GP_PIN(2, 15), RCAR_GP_PIN(2, 14),
};
static const unsigned int i2c6_mux[] = {
	SDA6_MARK, SCL6_MARK,
};

/* - INTC-EX ---------------------------------------------------------------- */
static const unsigned int intc_ex_irq0_pins[] = {
	/* IRQ0 */
	RCAR_GP_PIN(1, 24),
};
static const unsigned int intc_ex_irq0_mux[] = {
	IRQ0_MARK,
};
static const unsigned int intc_ex_irq1_pins[] = {
	/* IRQ1 */
	RCAR_GP_PIN(1, 25),
};
static const unsigned int intc_ex_irq1_mux[] = {
	IRQ1_MARK,
};
static const unsigned int intc_ex_irq2_pins[] = {
	/* IRQ2 */
	RCAR_GP_PIN(1, 26),
};
static const unsigned int intc_ex_irq2_mux[] = {
	IRQ2_MARK,
};
static const unsigned int intc_ex_irq3_pins[] = {
	/* IRQ3 */
	RCAR_GP_PIN(1, 27),
};
static const unsigned int intc_ex_irq3_mux[] = {
	IRQ3_MARK,
};
static const unsigned int intc_ex_irq4_pins[] = {
	/* IRQ4 */
	RCAR_GP_PIN(2, 14),
};
static const unsigned int intc_ex_irq4_mux[] = {
	IRQ4_MARK,
};
static const unsigned int intc_ex_irq5_pins[] = {
	/* IRQ5 */
	RCAR_GP_PIN(2, 15),
};
static const unsigned int intc_ex_irq5_mux[] = {
	IRQ5_MARK,
};

/* - MMC -------------------------------------------------------------------- */
static const unsigned int mmc_data1_pins[] = {
	/* MMC_SD_D0 */
	RCAR_GP_PIN(0, 19),
};
static const unsigned int mmc_data1_mux[] = {
	MMC_SD_D0_MARK,
};
static const unsigned int mmc_data4_pins[] = {
	/* MMC_SD_D[0:3] */
	RCAR_GP_PIN(0, 19), RCAR_GP_PIN(0, 20),
	RCAR_GP_PIN(0, 21), RCAR_GP_PIN(0, 22),
};
static const unsigned int mmc_data4_mux[] = {
	MMC_SD_D0_MARK, MMC_SD_D1_MARK,
	MMC_SD_D2_MARK, MMC_SD_D3_MARK,
};
static const unsigned int mmc_data8_pins[] = {
	/* MMC_SD_D[0:3], MMC_D[4:7] */
	RCAR_GP_PIN(0, 19), RCAR_GP_PIN(0, 20),
	RCAR_GP_PIN(0, 21), RCAR_GP_PIN(0, 22),
	RCAR_GP_PIN(0, 24), RCAR_GP_PIN(0, 25),
	RCAR_GP_PIN(0, 26), RCAR_GP_PIN(0, 27),
};
static const unsigned int mmc_data8_mux[] = {
	MMC_SD_D0_MARK, MMC_SD_D1_MARK,
	MMC_SD_D2_MARK, MMC_SD_D3_MARK,
	MMC_D4_MARK, MMC_D5_MARK,
	MMC_D6_MARK, MMC_D7_MARK,
};
static const unsigned int mmc_ctrl_pins[] = {
	/* MMC_SD_CLK, MMC_SD_CMD */
	RCAR_GP_PIN(0, 23), RCAR_GP_PIN(0, 18),
};
static const unsigned int mmc_ctrl_mux[] = {
	MMC_SD_CLK_MARK, MMC_SD_CMD_MARK,
};
static const unsigned int mmc_cd_pins[] = {
	/* SD_CD */
	RCAR_GP_PIN(0, 16),
};
static const unsigned int mmc_cd_mux[] = {
	SD_CD_MARK,
};
static const unsigned int mmc_wp_pins[] = {
	/* SD_WP */
	RCAR_GP_PIN(0, 15),
};
static const unsigned int mmc_wp_mux[] = {
	SD_WP_MARK,
};
static const unsigned int mmc_ds_pins[] = {
	/* MMC_DS */
	RCAR_GP_PIN(0, 17),
};
static const unsigned int mmc_ds_mux[] = {
	MMC_DS_MARK,
};

/* - MSIOF0 ----------------------------------------------------------------- */
static const unsigned int msiof0_clk_pins[] = {
	/* MSIOF0_SCK */
	RCAR_GP_PIN(1, 8),
};
static const unsigned int msiof0_clk_mux[] = {
	MSIOF0_SCK_MARK,
};
static const unsigned int msiof0_sync_pins[] = {
	/* MSIOF0_SYNC */
	RCAR_GP_PIN(1, 9),
};
static const unsigned int msiof0_sync_mux[] = {
	MSIOF0_SYNC_MARK,
};
static const unsigned int msiof0_ss1_pins[] = {
	/* MSIOF0_SS1 */
	RCAR_GP_PIN(1, 10),
};
static const unsigned int msiof0_ss1_mux[] = {
	MSIOF0_SS1_MARK,
};
static const unsigned int msiof0_ss2_pins[] = {
	/* MSIOF0_SS2 */
	RCAR_GP_PIN(1, 11),
};
static const unsigned int msiof0_ss2_mux[] = {
	MSIOF0_SS2_MARK,
};
static const unsigned int msiof0_txd_pins[] = {
	/* MSIOF0_TXD */
	RCAR_GP_PIN(1, 7),
};
static const unsigned int msiof0_txd_mux[] = {
	MSIOF0_TXD_MARK,
};
static const unsigned int msiof0_rxd_pins[] = {
	/* MSIOF0_RXD */
	RCAR_GP_PIN(1, 6),
};
static const unsigned int msiof0_rxd_mux[] = {
	MSIOF0_RXD_MARK,
};

/* - MSIOF1 ----------------------------------------------------------------- */
static const unsigned int msiof1_clk_pins[] = {
	/* MSIOF1_SCK */
	RCAR_GP_PIN(1, 14),
};
static const unsigned int msiof1_clk_mux[] = {
	MSIOF1_SCK_MARK,
};
static const unsigned int msiof1_sync_pins[] = {
	/* MSIOF1_SYNC */
	RCAR_GP_PIN(1, 15),
};
static const unsigned int msiof1_sync_mux[] = {
	MSIOF1_SYNC_MARK,
};
static const unsigned int msiof1_ss1_pins[] = {
	/* MSIOF1_SS1 */
	RCAR_GP_PIN(1, 16),
};
static const unsigned int msiof1_ss1_mux[] = {
	MSIOF1_SS1_MARK,
};
static const unsigned int msiof1_ss2_pins[] = {
	/* MSIOF1_SS2 */
	RCAR_GP_PIN(1, 17),
};
static const unsigned int msiof1_ss2_mux[] = {
	MSIOF1_SS2_MARK,
};
static const unsigned int msiof1_txd_pins[] = {
	/* MSIOF1_TXD */
	RCAR_GP_PIN(1, 13),
};
static const unsigned int msiof1_txd_mux[] = {
	MSIOF1_TXD_MARK,
};
static const unsigned int msiof1_rxd_pins[] = {
	/* MSIOF1_RXD */
	RCAR_GP_PIN(1, 12),
};
static const unsigned int msiof1_rxd_mux[] = {
	MSIOF1_RXD_MARK,
};

/* - MSIOF2 ----------------------------------------------------------------- */
static const unsigned int msiof2_clk_pins[] = {
	/* MSIOF2_SCK */
	RCAR_GP_PIN(1, 20),
};
static const unsigned int msiof2_clk_mux[] = {
	MSIOF2_SCK_MARK,
};
static const unsigned int msiof2_sync_pins[] = {
	/* MSIOF2_SYNC */
	RCAR_GP_PIN(1, 21),
};
static const unsigned int msiof2_sync_mux[] = {
	MSIOF2_SYNC_MARK,
};
static const unsigned int msiof2_ss1_pins[] = {
	/* MSIOF2_SS1 */
	RCAR_GP_PIN(1, 22),
};
static const unsigned int msiof2_ss1_mux[] = {
	MSIOF2_SS1_MARK,
};
static const unsigned int msiof2_ss2_pins[] = {
	/* MSIOF2_SS2 */
	RCAR_GP_PIN(1, 23),
};
static const unsigned int msiof2_ss2_mux[] = {
	MSIOF2_SS2_MARK,
};
static const unsigned int msiof2_txd_pins[] = {
	/* MSIOF2_TXD */
	RCAR_GP_PIN(1, 19),
};
static const unsigned int msiof2_txd_mux[] = {
	MSIOF2_TXD_MARK,
};
static const unsigned int msiof2_rxd_pins[] = {
	/* MSIOF2_RXD */
	RCAR_GP_PIN(1, 18),
};
static const unsigned int msiof2_rxd_mux[] = {
	MSIOF2_RXD_MARK,
};

/* - MSIOF3 ----------------------------------------------------------------- */
static const unsigned int msiof3_clk_pins[] = {
	/* MSIOF3_SCK */
	RCAR_GP_PIN(2, 20),
};
static const unsigned int msiof3_clk_mux[] = {
	MSIOF3_SCK_MARK,
};
static const unsigned int msiof3_sync_pins[] = {
	/* MSIOF3_SYNC */
	RCAR_GP_PIN(2, 21),
};
static const unsigned int msiof3_sync_mux[] = {
	MSIOF3_SYNC_MARK,
};
static const unsigned int msiof3_ss1_pins[] = {
	/* MSIOF3_SS1 */
	RCAR_GP_PIN(2, 16),
};
static const unsigned int msiof3_ss1_mux[] = {
	MSIOF3_SS1_MARK,
};
static const unsigned int msiof3_ss2_pins[] = {
	/* MSIOF3_SS2 */
	RCAR_GP_PIN(2, 17),
};
static const unsigned int msiof3_ss2_mux[] = {
	MSIOF3_SS2_MARK,
};
static const unsigned int msiof3_txd_pins[] = {
	/* MSIOF3_TXD */
	RCAR_GP_PIN(2, 19),
};
static const unsigned int msiof3_txd_mux[] = {
	MSIOF3_TXD_MARK,
};
static const unsigned int msiof3_rxd_pins[] = {
	/* MSIOF3_RXD */
	RCAR_GP_PIN(2, 18),
};
static const unsigned int msiof3_rxd_mux[] = {
	MSIOF3_RXD_MARK,
};

/* - MSIOF4 ----------------------------------------------------------------- */
static const unsigned int msiof4_clk_pins[] = {
	/* MSIOF4_SCK */
	RCAR_GP_PIN(2, 6),
};
static const unsigned int msiof4_clk_mux[] = {
	MSIOF4_SCK_MARK,
};
static const unsigned int msiof4_sync_pins[] = {
	/* MSIOF4_SYNC */
	RCAR_GP_PIN(2, 7),
};
static const unsigned int msiof4_sync_mux[] = {
	MSIOF4_SYNC_MARK,
};
static const unsigned int msiof4_ss1_pins[] = {
	/* MSIOF4_SS1 */
	RCAR_GP_PIN(2, 8),
};
static const unsigned int msiof4_ss1_mux[] = {
	MSIOF4_SS1_MARK,
};
static const unsigned int msiof4_ss2_pins[] = {
	/* MSIOF4_SS2 */
	RCAR_GP_PIN(2, 9),
};
static const unsigned int msiof4_ss2_mux[] = {
	MSIOF4_SS2_MARK,
};
static const unsigned int msiof4_txd_pins[] = {
	/* MSIOF4_TXD */
	RCAR_GP_PIN(2, 5),
};
static const unsigned int msiof4_txd_mux[] = {
	MSIOF4_TXD_MARK,
};
static const unsigned int msiof4_rxd_pins[] = {
	/* MSIOF4_RXD */
	RCAR_GP_PIN(2, 4),
};
static const unsigned int msiof4_rxd_mux[] = {
	MSIOF4_RXD_MARK,
};

/* - MSIOF5 ----------------------------------------------------------------- */
static const unsigned int msiof5_clk_pins[] = {
	/* MSIOF5_SCK */
	RCAR_GP_PIN(2, 12),
};
static const unsigned int msiof5_clk_mux[] = {
	MSIOF5_SCK_MARK,
};
static const unsigned int msiof5_sync_pins[] = {
	/* MSIOF5_SYNC */
	RCAR_GP_PIN(2, 13),
};
static const unsigned int msiof5_sync_mux[] = {
	MSIOF5_SYNC_MARK,
};
static const unsigned int msiof5_ss1_pins[] = {
	/* MSIOF5_SS1 */
	RCAR_GP_PIN(2, 14),
};
static const unsigned int msiof5_ss1_mux[] = {
	MSIOF5_SS1_MARK,
};
static const unsigned int msiof5_ss2_pins[] = {
	/* MSIOF5_SS2 */
	RCAR_GP_PIN(2, 15),
};
static const unsigned int msiof5_ss2_mux[] = {
	MSIOF5_SS2_MARK,
};
static const unsigned int msiof5_txd_pins[] = {
	/* MSIOF5_TXD */
	RCAR_GP_PIN(2, 11),
};
static const unsigned int msiof5_txd_mux[] = {
	MSIOF5_TXD_MARK,
};
static const unsigned int msiof5_rxd_pins[] = {
	/* MSIOF5_RXD */
	RCAR_GP_PIN(2, 10),
};
static const unsigned int msiof5_rxd_mux[] = {
	MSIOF5_RXD_MARK,
};

/* - PWM0 ------------------------------------------------------------------- */
static const unsigned int pwm0_pins[] = {
	/* PWM0 */
	RCAR_GP_PIN(3, 5),
};
static const unsigned int pwm0_mux[] = {
	PWM0_MARK,
};

/* - PWM1 ------------------------------------------------------------------- */
static const unsigned int pwm1_pins[] = {
	/* PWM1 */
	RCAR_GP_PIN(3, 6),
};
static const unsigned int pwm1_mux[] = {
	PWM1_MARK,
};

/* - PWM2 ------------------------------------------------------------------- */
static const unsigned int pwm2_pins[] = {
	/* PWM2 */
	RCAR_GP_PIN(3, 7),
};
static const unsigned int pwm2_mux[] = {
	PWM2_MARK,
};

/* - PWM3 ------------------------------------------------------------------- */
static const unsigned int pwm3_pins[] = {
	/* PWM3 */
	RCAR_GP_PIN(3, 8),
};
static const unsigned int pwm3_mux[] = {
	PWM3_MARK,
};

/* - PWM4 ------------------------------------------------------------------- */
static const unsigned int pwm4_pins[] = {
	/* PWM4 */
	RCAR_GP_PIN(3, 9),
};
static const unsigned int pwm4_mux[] = {
	PWM4_MARK,
};

/* - QSPI0 ------------------------------------------------------------------ */
static const unsigned int qspi0_ctrl_pins[] = {
	/* SPCLK, SSL */
	RCAR_GP_PIN(0, 0), RCAR_GP_PIN(0, 5),
};
static const unsigned int qspi0_ctrl_mux[] = {
	QSPI0_SPCLK_MARK, QSPI0_SSL_MARK,
};
static const unsigned int qspi0_data2_pins[] = {
	/* MOSI_IO0, MISO_IO1 */
	RCAR_GP_PIN(0, 1), RCAR_GP_PIN(0, 2),
};
static const unsigned int qspi0_data2_mux[] = {
	QSPI0_MOSI_IO0_MARK, QSPI0_MISO_IO1_MARK,
};
static const unsigned int qspi0_data4_pins[] = {
	/* MOSI_IO0, MISO_IO1, IO2, IO3 */
	RCAR_GP_PIN(0, 1), RCAR_GP_PIN(0, 2),
	RCAR_GP_PIN(0, 3), RCAR_GP_PIN(0, 4),
};
static const unsigned int qspi0_data4_mux[] = {
	QSPI0_MOSI_IO0_MARK, QSPI0_MISO_IO1_MARK,
	QSPI0_IO2_MARK, QSPI0_IO3_MARK
};

/* - QSPI1 ------------------------------------------------------------------ */
static const unsigned int qspi1_ctrl_pins[] = {
	/* SPCLK, SSL */
	RCAR_GP_PIN(0, 6), RCAR_GP_PIN(0, 11),
};
static const unsigned int qspi1_ctrl_mux[] = {
	QSPI1_SPCLK_MARK, QSPI1_SSL_MARK,
};
static const unsigned int qspi1_data2_pins[] = {
	/* MOSI_IO0, MISO_IO1 */
	RCAR_GP_PIN(0, 7), RCAR_GP_PIN(0, 8),
};
static const unsigned int qspi1_data2_mux[] = {
	QSPI1_MOSI_IO0_MARK, QSPI1_MISO_IO1_MARK,
};
static const unsigned int qspi1_data4_pins[] = {
	/* MOSI_IO0, MISO_IO1, IO2, IO3 */
	RCAR_GP_PIN(0, 7), RCAR_GP_PIN(0, 8),
	RCAR_GP_PIN(0, 9), RCAR_GP_PIN(0, 10),
};
static const unsigned int qspi1_data4_mux[] = {
	QSPI1_MOSI_IO0_MARK, QSPI1_MISO_IO1_MARK,
	QSPI1_IO2_MARK, QSPI1_IO3_MARK
};

/* - SCIF0 ------------------------------------------------------------------ */
static const unsigned int scif0_data_pins[] = {
	/* RX0, TX0 */
	RCAR_GP_PIN(1, 1), RCAR_GP_PIN(1, 5),
};
static const unsigned int scif0_data_mux[] = {
	RX0_MARK, TX0_MARK,
};
static const unsigned int scif0_clk_pins[] = {
	/* SCK0 */
	RCAR_GP_PIN(1, 2),
};
static const unsigned int scif0_clk_mux[] = {
	SCK0_MARK,
};
static const unsigned int scif0_ctrl_pins[] = {
	/* RTS0#, CTS0# */
	RCAR_GP_PIN(1, 3), RCAR_GP_PIN(1, 4),
};
static const unsigned int scif0_ctrl_mux[] = {
	RTS0_N_MARK, CTS0_N_MARK,
};

/* - SCIF1 ------------------------------------------------------------------ */
static const unsigned int scif1_data_a_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(1, 21), RCAR_GP_PIN(1, 22),
};
static const unsigned int scif1_data_a_mux[] = {
	RX1_A_MARK, TX1_A_MARK,
};
static const unsigned int scif1_data_b_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(3, 2), RCAR_GP_PIN(3, 1),
};
static const unsigned int scif1_data_b_mux[] = {
	RX1_B_MARK, TX1_B_MARK,
};
static const unsigned int scif1_clk_pins[] = {
	/* SCK1 */
	RCAR_GP_PIN(1, 18),
};
static const unsigned int scif1_clk_mux[] = {
	SCK1_MARK,
};
static const unsigned int scif1_ctrl_pins[] = {
	/* RTS1#, CTS1# */
	RCAR_GP_PIN(1, 20), RCAR_GP_PIN(1, 19),
};
static const unsigned int scif1_ctrl_mux[] = {
	RTS1_N_MARK, CTS1_N_MARK,
};

/* - SCIF3 ------------------------------------------------------------------ */
static const unsigned int scif3_data_pins[] = {
	/* RX3, TX3 */
	RCAR_GP_PIN(1, 16), RCAR_GP_PIN(1, 17),
};
static const unsigned int scif3_data_mux[] = {
	RX3_MARK, TX3_MARK,
};
static const unsigned int scif3_clk_pins[] = {
	/* SCK3 */
	RCAR_GP_PIN(1, 13),
};
static const unsigned int scif3_clk_mux[] = {
	SCK3_MARK,
};
static const unsigned int scif3_ctrl_pins[] = {
	/* RTS3#, CTS3# */
	RCAR_GP_PIN(1, 15), RCAR_GP_PIN(1, 14),
};
static const unsigned int scif3_ctrl_mux[] = {
	RTS3_N_MARK, CTS3_N_MARK,
};

/* - SCIF4 ------------------------------------------------------------------ */
static const unsigned int scif4_data_pins[] = {
	/* RX4, TX4 */
	RCAR_GP_PIN(2, 8), RCAR_GP_PIN(2, 9),
};
static const unsigned int scif4_data_mux[] = {
	RX4_MARK, TX4_MARK,
};
static const unsigned int scif4_clk_pins[] = {
	/* SCK4 */
	RCAR_GP_PIN(2, 5),
};
static const unsigned int scif4_clk_mux[] = {
	SCK4_MARK,
};
static const unsigned int scif4_ctrl_pins[] = {
	/* RTS4#, CTS4# */
	RCAR_GP_PIN(2, 7), RCAR_GP_PIN(2, 6),
};
static const unsigned int scif4_ctrl_mux[] = {
	RTS4_N_MARK, CTS4_N_MARK,
};

/* - SCIF Clock ------------------------------------------------------------- */
static const unsigned int scif_clk_pins[] = {
	/* SCIF_CLK */
	RCAR_GP_PIN(1, 0),
};
static const unsigned int scif_clk_mux[] = {
	SCIF_CLK_MARK,
};

/* - TMU -------------------------------------------------------------------- */
static const unsigned int tmu_tclk1_a_pins[] = {
	/* TCLK1 */
	RCAR_GP_PIN(2, 23),
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
	RCAR_GP_PIN(2, 24),
};
static const unsigned int tmu_tclk2_a_mux[] = {
	TCLK2_A_MARK,
};
static const unsigned int tmu_tclk2_b_pins[] = {
	/* TCLK2 */
	RCAR_GP_PIN(2, 10),
};
static const unsigned int tmu_tclk2_b_mux[] = {
	TCLK2_B_MARK,
};

static const unsigned int tmu_tclk3_pins[] = {
	/* TCLK3 */
	RCAR_GP_PIN(2, 11),
};
static const unsigned int tmu_tclk3_mux[] = {
	TCLK3_MARK,
};

static const unsigned int tmu_tclk4_pins[] = {
	/* TCLK4 */
	RCAR_GP_PIN(2, 12),
};
static const unsigned int tmu_tclk4_mux[] = {
	TCLK4_MARK,
};

/* - TPU ------------------------------------------------------------------- */
static const unsigned int tpu_to0_pins[] = {
	/* TPU0TO0 */
	RCAR_GP_PIN(2, 21),
};
static const unsigned int tpu_to0_mux[] = {
	TPU0TO0_MARK,
};
static const unsigned int tpu_to1_pins[] = {
	/* TPU0TO1 */
	RCAR_GP_PIN(2, 22),
};
static const unsigned int tpu_to1_mux[] = {
	TPU0TO1_MARK,
};
static const unsigned int tpu_to2_pins[] = {
	/* TPU0TO2 */
	RCAR_GP_PIN(3, 5),
};
static const unsigned int tpu_to2_mux[] = {
	TPU0TO2_MARK,
};
static const unsigned int tpu_to3_pins[] = {
	/* TPU0TO3 */
	RCAR_GP_PIN(3, 6),
};
static const unsigned int tpu_to3_mux[] = {
	TPU0TO3_MARK,
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

	SH_PFC_PIN_GROUP(avb3_link),
	SH_PFC_PIN_GROUP(avb3_magic),
	SH_PFC_PIN_GROUP(avb3_phy_int),
	SH_PFC_PIN_GROUP(avb3_mdio),
	SH_PFC_PIN_GROUP(avb3_rgmii),
	SH_PFC_PIN_GROUP(avb3_txcrefclk),
	SH_PFC_PIN_GROUP(avb3_avtp_pps),
	SH_PFC_PIN_GROUP(avb3_avtp_capture),
	SH_PFC_PIN_GROUP(avb3_avtp_match),

	SH_PFC_PIN_GROUP(avb4_link),
	SH_PFC_PIN_GROUP(avb4_magic),
	SH_PFC_PIN_GROUP(avb4_phy_int),
	SH_PFC_PIN_GROUP(avb4_mdio),
	SH_PFC_PIN_GROUP(avb4_rgmii),
	SH_PFC_PIN_GROUP(avb4_txcrefclk),
	SH_PFC_PIN_GROUP(avb4_avtp_pps),
	SH_PFC_PIN_GROUP(avb4_avtp_capture),
	SH_PFC_PIN_GROUP(avb4_avtp_match),

	SH_PFC_PIN_GROUP(avb5_link),
	SH_PFC_PIN_GROUP(avb5_magic),
	SH_PFC_PIN_GROUP(avb5_phy_int),
	SH_PFC_PIN_GROUP(avb5_mdio),
	SH_PFC_PIN_GROUP(avb5_rgmii),
	SH_PFC_PIN_GROUP(avb5_txcrefclk),
	SH_PFC_PIN_GROUP(avb5_avtp_pps),
	SH_PFC_PIN_GROUP(avb5_avtp_capture),
	SH_PFC_PIN_GROUP(avb5_avtp_match),

	SH_PFC_PIN_GROUP(canfd0_data),
	SH_PFC_PIN_GROUP(canfd1_data),
	SH_PFC_PIN_GROUP(canfd2_data),
	SH_PFC_PIN_GROUP(canfd3_data),
	SH_PFC_PIN_GROUP(canfd4_data),
	SH_PFC_PIN_GROUP(canfd5_data),
	SH_PFC_PIN_GROUP(canfd6_data),
	SH_PFC_PIN_GROUP(canfd7_data),
	SH_PFC_PIN_GROUP(can_clk),

	SH_PFC_PIN_GROUP(du_rgb888),
	SH_PFC_PIN_GROUP(du_clk_out),
	SH_PFC_PIN_GROUP(du_sync),
	SH_PFC_PIN_GROUP(du_oddf),

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
	SH_PFC_PIN_GROUP(i2c3),
	SH_PFC_PIN_GROUP(i2c4),
	SH_PFC_PIN_GROUP(i2c5),
	SH_PFC_PIN_GROUP(i2c6),

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
	SH_PFC_PIN_GROUP(mmc_ds),

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
	SH_PFC_PIN_GROUP(msiof4_clk),
	SH_PFC_PIN_GROUP(msiof4_sync),
	SH_PFC_PIN_GROUP(msiof4_ss1),
	SH_PFC_PIN_GROUP(msiof4_ss2),
	SH_PFC_PIN_GROUP(msiof4_txd),
	SH_PFC_PIN_GROUP(msiof4_rxd),
	SH_PFC_PIN_GROUP(msiof5_clk),
	SH_PFC_PIN_GROUP(msiof5_sync),
	SH_PFC_PIN_GROUP(msiof5_ss1),
	SH_PFC_PIN_GROUP(msiof5_ss2),
	SH_PFC_PIN_GROUP(msiof5_txd),
	SH_PFC_PIN_GROUP(msiof5_rxd),

	SH_PFC_PIN_GROUP(pwm0),
	SH_PFC_PIN_GROUP(pwm1),
	SH_PFC_PIN_GROUP(pwm2),
	SH_PFC_PIN_GROUP(pwm3),
	SH_PFC_PIN_GROUP(pwm4),

	SH_PFC_PIN_GROUP(qspi0_ctrl),
	SH_PFC_PIN_GROUP(qspi0_data2),
	SH_PFC_PIN_GROUP(qspi0_data4),
	SH_PFC_PIN_GROUP(qspi1_ctrl),
	SH_PFC_PIN_GROUP(qspi1_data2),
	SH_PFC_PIN_GROUP(qspi1_data4),

	SH_PFC_PIN_GROUP(scif0_data),
	SH_PFC_PIN_GROUP(scif0_clk),
	SH_PFC_PIN_GROUP(scif0_ctrl),
	SH_PFC_PIN_GROUP(scif1_data_a),
	SH_PFC_PIN_GROUP(scif1_data_b),
	SH_PFC_PIN_GROUP(scif1_clk),
	SH_PFC_PIN_GROUP(scif1_ctrl),
	SH_PFC_PIN_GROUP(scif3_data),
	SH_PFC_PIN_GROUP(scif3_clk),
	SH_PFC_PIN_GROUP(scif3_ctrl),
	SH_PFC_PIN_GROUP(scif4_data),
	SH_PFC_PIN_GROUP(scif4_clk),
	SH_PFC_PIN_GROUP(scif4_ctrl),
	SH_PFC_PIN_GROUP(scif_clk),

	SH_PFC_PIN_GROUP(tmu_tclk1_a),
	SH_PFC_PIN_GROUP(tmu_tclk1_b),
	SH_PFC_PIN_GROUP(tmu_tclk2_a),
	SH_PFC_PIN_GROUP(tmu_tclk2_b),
	SH_PFC_PIN_GROUP(tmu_tclk3),
	SH_PFC_PIN_GROUP(tmu_tclk4),

	SH_PFC_PIN_GROUP(tpu_to0),
	SH_PFC_PIN_GROUP(tpu_to1),
	SH_PFC_PIN_GROUP(tpu_to2),
	SH_PFC_PIN_GROUP(tpu_to3),
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

static const char * const avb3_groups[] = {
	"avb3_link",
	"avb3_magic",
	"avb3_phy_int",
	"avb3_mdio",
	"avb3_rgmii",
	"avb3_txcrefclk",
	"avb3_avtp_pps",
	"avb3_avtp_capture",
	"avb3_avtp_match",
};

static const char * const avb4_groups[] = {
	"avb4_link",
	"avb4_magic",
	"avb4_phy_int",
	"avb4_mdio",
	"avb4_rgmii",
	"avb4_txcrefclk",
	"avb4_avtp_pps",
	"avb4_avtp_capture",
	"avb4_avtp_match",
};

static const char * const avb5_groups[] = {
	"avb5_link",
	"avb5_magic",
	"avb5_phy_int",
	"avb5_mdio",
	"avb5_rgmii",
	"avb5_txcrefclk",
	"avb5_avtp_pps",
	"avb5_avtp_capture",
	"avb5_avtp_match",
};

static const char * const canfd0_groups[] = {
	"canfd0_data",
};

static const char * const canfd1_groups[] = {
	"canfd1_data",
};

static const char * const canfd2_groups[] = {
	"canfd2_data",
};

static const char * const canfd3_groups[] = {
	"canfd3_data",
};

static const char * const canfd4_groups[] = {
	"canfd4_data",
};

static const char * const canfd5_groups[] = {
	"canfd5_data",
};

static const char * const canfd6_groups[] = {
	"canfd6_data",
};

static const char * const canfd7_groups[] = {
	"canfd7_data",
};

static const char * const can_clk_groups[] = {
	"can_clk",
};

static const char * const du_groups[] = {
	"du_rgb888",
	"du_clk_out",
	"du_sync",
	"du_oddf",
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
	"i2c3",
};

static const char * const i2c4_groups[] = {
	"i2c4",
};

static const char * const i2c5_groups[] = {
	"i2c5",
};

static const char * const i2c6_groups[] = {
	"i2c6",
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
	"mmc_ds",
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

static const char * const msiof4_groups[] = {
	"msiof4_clk",
	"msiof4_sync",
	"msiof4_ss1",
	"msiof4_ss2",
	"msiof4_txd",
	"msiof4_rxd",
};

static const char * const msiof5_groups[] = {
	"msiof5_clk",
	"msiof5_sync",
	"msiof5_ss1",
	"msiof5_ss2",
	"msiof5_txd",
	"msiof5_rxd",
};

static const char * const pwm0_groups[] = {
	"pwm0",
};

static const char * const pwm1_groups[] = {
	"pwm1",
};

static const char * const pwm2_groups[] = {
	"pwm2",
};

static const char * const pwm3_groups[] = {
	"pwm3",
};

static const char * const pwm4_groups[] = {
	"pwm4",
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
	"scif0_data",
	"scif0_clk",
	"scif0_ctrl",
};

static const char * const scif1_groups[] = {
	"scif1_data_a",
	"scif1_data_b",
	"scif1_clk",
	"scif1_ctrl",
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

static const char * const scif_clk_groups[] = {
	"scif_clk",
};

static const char * const tmu_groups[] = {
	"tmu_tclk1_a",
	"tmu_tclk1_b",
	"tmu_tclk2_a",
	"tmu_tclk2_b",
	"tmu_tclk3",
	"tmu_tclk4",
};

static const char * const tpu_groups[] = {
	"tpu_to0",
	"tpu_to1",
	"tpu_to2",
	"tpu_to3",
};

static const struct sh_pfc_function pinmux_functions[] = {
	SH_PFC_FUNCTION(avb0),
	SH_PFC_FUNCTION(avb1),
	SH_PFC_FUNCTION(avb2),
	SH_PFC_FUNCTION(avb3),
	SH_PFC_FUNCTION(avb4),
	SH_PFC_FUNCTION(avb5),

	SH_PFC_FUNCTION(canfd0),
	SH_PFC_FUNCTION(canfd1),
	SH_PFC_FUNCTION(canfd2),
	SH_PFC_FUNCTION(canfd3),
	SH_PFC_FUNCTION(canfd4),
	SH_PFC_FUNCTION(canfd5),
	SH_PFC_FUNCTION(canfd6),
	SH_PFC_FUNCTION(canfd7),
	SH_PFC_FUNCTION(can_clk),

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
	SH_PFC_FUNCTION(i2c5),
	SH_PFC_FUNCTION(i2c6),

	SH_PFC_FUNCTION(intc_ex),

	SH_PFC_FUNCTION(mmc),

	SH_PFC_FUNCTION(msiof0),
	SH_PFC_FUNCTION(msiof1),
	SH_PFC_FUNCTION(msiof2),
	SH_PFC_FUNCTION(msiof3),
	SH_PFC_FUNCTION(msiof4),
	SH_PFC_FUNCTION(msiof5),

	SH_PFC_FUNCTION(pwm0),
	SH_PFC_FUNCTION(pwm1),
	SH_PFC_FUNCTION(pwm2),
	SH_PFC_FUNCTION(pwm3),
	SH_PFC_FUNCTION(pwm4),

	SH_PFC_FUNCTION(qspi0),
	SH_PFC_FUNCTION(qspi1),

	SH_PFC_FUNCTION(scif0),
	SH_PFC_FUNCTION(scif1),
	SH_PFC_FUNCTION(scif3),
	SH_PFC_FUNCTION(scif4),
	SH_PFC_FUNCTION(scif_clk),

	SH_PFC_FUNCTION(tmu),

	SH_PFC_FUNCTION(tpu),
};

static const struct pinmux_cfg_reg pinmux_config_regs[] = {
#define F_(x, y)	FN_##y
#define FM(x)		FN_##x
	{ PINMUX_CFG_REG("GPSR0", 0xe6058040, 32, 1, GROUP(
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		GP_0_27_FN,	GPSR0_27,
		GP_0_26_FN,	GPSR0_26,
		GP_0_25_FN,	GPSR0_25,
		GP_0_24_FN,	GPSR0_24,
		GP_0_23_FN,	GPSR0_23,
		GP_0_22_FN,	GPSR0_22,
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
	{ PINMUX_CFG_REG("GPSR1", 0xe6050040, 32, 1, GROUP(
		0, 0,
		GP_1_30_FN,	GPSR1_30,
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
	{ PINMUX_CFG_REG("GPSR2", 0xe6050840, 32, 1, GROUP(
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
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
	{ PINMUX_CFG_REG("GPSR3", 0xe6058840, 32, 1, GROUP(
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
	{ PINMUX_CFG_REG("GPSR4", 0xe6060040, 32, 1, GROUP(
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		GP_4_26_FN,	GPSR4_26,
		GP_4_25_FN,	GPSR4_25,
		GP_4_24_FN,	GPSR4_24,
		GP_4_23_FN,	GPSR4_23,
		GP_4_22_FN,	GPSR4_22,
		GP_4_21_FN,	GPSR4_21,
		GP_4_20_FN,	GPSR4_20,
		GP_4_19_FN,	GPSR4_19,
		GP_4_18_FN,	GPSR4_18,
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
	{ PINMUX_CFG_REG("GPSR5", 0xe6060840, 32, 1, GROUP(
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
	{ PINMUX_CFG_REG("GPSR6", 0xe6068040, 32, 1, GROUP(
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
	{ PINMUX_CFG_REG("GPSR7", 0xe6068840, 32, 1, GROUP(
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
	{ PINMUX_CFG_REG("GPSR8", 0xe6069040, 32, 1, GROUP(
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
		GP_8_20_FN,	GPSR8_20,
		GP_8_19_FN,	GPSR8_19,
		GP_8_18_FN,	GPSR8_18,
		GP_8_17_FN,	GPSR8_17,
		GP_8_16_FN,	GPSR8_16,
		GP_8_15_FN,	GPSR8_15,
		GP_8_14_FN,	GPSR8_14,
		GP_8_13_FN,	GPSR8_13,
		GP_8_12_FN,	GPSR8_12,
		GP_8_11_FN,	GPSR8_11,
		GP_8_10_FN,	GPSR8_10,
		GP_8_9_FN,	GPSR8_9,
		GP_8_8_FN,	GPSR8_8,
		GP_8_7_FN,	GPSR8_7,
		GP_8_6_FN,	GPSR8_6,
		GP_8_5_FN,	GPSR8_5,
		GP_8_4_FN,	GPSR8_4,
		GP_8_3_FN,	GPSR8_3,
		GP_8_2_FN,	GPSR8_2,
		GP_8_1_FN,	GPSR8_1,
		GP_8_0_FN,	GPSR8_0, ))
	},
	{ PINMUX_CFG_REG("GPSR9", 0xe6069840, 32, 1, GROUP(
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
		GP_9_20_FN,	GPSR9_20,
		GP_9_19_FN,	GPSR9_19,
		GP_9_18_FN,	GPSR9_18,
		GP_9_17_FN,	GPSR9_17,
		GP_9_16_FN,	GPSR9_16,
		GP_9_15_FN,	GPSR9_15,
		GP_9_14_FN,	GPSR9_14,
		GP_9_13_FN,	GPSR9_13,
		GP_9_12_FN,	GPSR9_12,
		GP_9_11_FN,	GPSR9_11,
		GP_9_10_FN,	GPSR9_10,
		GP_9_9_FN,	GPSR9_9,
		GP_9_8_FN,	GPSR9_8,
		GP_9_7_FN,	GPSR9_7,
		GP_9_6_FN,	GPSR9_6,
		GP_9_5_FN,	GPSR9_5,
		GP_9_4_FN,	GPSR9_4,
		GP_9_3_FN,	GPSR9_3,
		GP_9_2_FN,	GPSR9_2,
		GP_9_1_FN,	GPSR9_1,
		GP_9_0_FN,	GPSR9_0, ))
	},
#undef F_
#undef FM

#define F_(x, y)	x,
#define FM(x)		FN_##x,
	{ PINMUX_CFG_REG("IP0SR1", 0xe6050060, 32, 4, GROUP(
		IP0SR1_31_28
		IP0SR1_27_24
		IP0SR1_23_20
		IP0SR1_19_16
		IP0SR1_15_12
		IP0SR1_11_8
		IP0SR1_7_4
		IP0SR1_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR1", 0xe6050064, 32, 4, GROUP(
		IP1SR1_31_28
		IP1SR1_27_24
		IP1SR1_23_20
		IP1SR1_19_16
		IP1SR1_15_12
		IP1SR1_11_8
		IP1SR1_7_4
		IP1SR1_3_0))
	},
	{ PINMUX_CFG_REG("IP2SR1", 0xe6050068, 32, 4, GROUP(
		IP2SR1_31_28
		IP2SR1_27_24
		IP2SR1_23_20
		IP2SR1_19_16
		IP2SR1_15_12
		IP2SR1_11_8
		IP2SR1_7_4
		IP2SR1_3_0))
	},
	{ PINMUX_CFG_REG("IP3SR1", 0xe605006c, 32, 4, GROUP(
		IP3SR1_31_28
		IP3SR1_27_24
		IP3SR1_23_20
		IP3SR1_19_16
		IP3SR1_15_12
		IP3SR1_11_8
		IP3SR1_7_4
		IP3SR1_3_0))
	},
	{ PINMUX_CFG_REG("IP0SR2", 0xe6050860, 32, 4, GROUP(
		IP0SR2_31_28
		IP0SR2_27_24
		IP0SR2_23_20
		IP0SR2_19_16
		IP0SR2_15_12
		IP0SR2_11_8
		IP0SR2_7_4
		IP0SR2_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR2", 0xe6050864, 32, 4, GROUP(
		IP1SR2_31_28
		IP1SR2_27_24
		IP1SR2_23_20
		IP1SR2_19_16
		IP1SR2_15_12
		IP1SR2_11_8
		IP1SR2_7_4
		IP1SR2_3_0))
	},
	{ PINMUX_CFG_REG("IP2SR2", 0xe6050868, 32, 4, GROUP(
		IP2SR2_31_28
		IP2SR2_27_24
		IP2SR2_23_20
		IP2SR2_19_16
		IP2SR2_15_12
		IP2SR2_11_8
		IP2SR2_7_4
		IP2SR2_3_0))
	},
	{ PINMUX_CFG_REG("IP0SR3", 0xe6058860, 32, 4, GROUP(
		IP0SR3_31_28
		IP0SR3_27_24
		IP0SR3_23_20
		IP0SR3_19_16
		IP0SR3_15_12
		IP0SR3_11_8
		IP0SR3_7_4
		IP0SR3_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR3", 0xe6058864, 32, 4, GROUP(
		IP1SR3_31_28
		IP1SR3_27_24
		IP1SR3_23_20
		IP1SR3_19_16
		IP1SR3_15_12
		IP1SR3_11_8
		IP1SR3_7_4
		IP1SR3_3_0))
	},
	{ PINMUX_CFG_REG("IP0SR4", 0xe6060060, 32, 4, GROUP(
		IP0SR4_31_28
		IP0SR4_27_24
		IP0SR4_23_20
		IP0SR4_19_16
		IP0SR4_15_12
		IP0SR4_11_8
		IP0SR4_7_4
		IP0SR4_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR4", 0xe6060064, 32, 4, GROUP(
		IP1SR4_31_28
		IP1SR4_27_24
		IP1SR4_23_20
		IP1SR4_19_16
		IP1SR4_15_12
		IP1SR4_11_8
		IP1SR4_7_4
		IP1SR4_3_0))
	},
	{ PINMUX_CFG_REG("IP2SR4", 0xe6060068, 32, 4, GROUP(
		IP2SR4_31_28
		IP2SR4_27_24
		IP2SR4_23_20
		IP2SR4_19_16
		IP2SR4_15_12
		IP2SR4_11_8
		IP2SR4_7_4
		IP2SR4_3_0))
	},
	{ PINMUX_CFG_REG("IP0SR5", 0xe6060860, 32, 4, GROUP(
		IP0SR5_31_28
		IP0SR5_27_24
		IP0SR5_23_20
		IP0SR5_19_16
		IP0SR5_15_12
		IP0SR5_11_8
		IP0SR5_7_4
		IP0SR5_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR5", 0xe6060864, 32, 4, GROUP(
		IP1SR5_31_28
		IP1SR5_27_24
		IP1SR5_23_20
		IP1SR5_19_16
		IP1SR5_15_12
		IP1SR5_11_8
		IP1SR5_7_4
		IP1SR5_3_0))
	},
	{ PINMUX_CFG_REG("IP2SR5", 0xe6060868, 32, 4, GROUP(
		IP2SR5_31_28
		IP2SR5_27_24
		IP2SR5_23_20
		IP2SR5_19_16
		IP2SR5_15_12
		IP2SR5_11_8
		IP2SR5_7_4
		IP2SR5_3_0))
	},
#undef F_
#undef FM

#define F_(x, y)	x,
#define FM(x)		FN_##x,
	{ PINMUX_CFG_REG_VAR("MOD_SEL2", 0xe6050900, 32,
			     GROUP(4, 4, 4, 4, 2, 2, 2, 2, 2, 2, 2, 1, 1),
			     GROUP(
		/* RESERVED 31, 30, 29, 28 */
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		/* RESERVED 27, 26, 25, 24 */
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		/* RESERVED 23, 22, 21, 20 */
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		/* RESERVED 19, 18, 17, 16 */
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		MOD_SEL2_14_15
		MOD_SEL2_12_13
		MOD_SEL2_10_11
		MOD_SEL2_8_9
		MOD_SEL2_6_7
		MOD_SEL2_4_5
		MOD_SEL2_2_3
		0, 0,
		0, 0, ))
	},
	{ },
};

static const struct pinmux_drive_reg pinmux_drive_regs[] = {
	{ PINMUX_DRIVE_REG("DRV0CTRL0", 0xe6058080) {
		{ RCAR_GP_PIN(0,  7), 28, 2 },	/* QSPI1_MOSI_IO0 */
		{ RCAR_GP_PIN(0,  6), 24, 2 },	/* QSPI1_SPCLK */
		{ RCAR_GP_PIN(0,  5), 20, 2 },	/* QSPI0_SSL */
		{ RCAR_GP_PIN(0,  4), 16, 2 },	/* QSPI0_IO3 */
		{ RCAR_GP_PIN(0,  3), 12, 2 },	/* QSPI0_IO2 */
		{ RCAR_GP_PIN(0,  2),  8, 2 },	/* QSPI0_MISO_IO1 */
		{ RCAR_GP_PIN(0,  1),  4, 2 },	/* QSPI0_MOSI_IO0 */
		{ RCAR_GP_PIN(0,  0),  0, 2 },	/* QSPI0_SPCLK */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL0", 0xe6058084) {
		{ RCAR_GP_PIN(0, 15), 28, 3 },	/* SD_WP */
		{ RCAR_GP_PIN(0, 14), 24, 2 },	/* RPC_INT_N */
		{ RCAR_GP_PIN(0, 13), 20, 2 },	/* RPC_WP_N */
		{ RCAR_GP_PIN(0, 12), 16, 2 },	/* RPC_RESET_N */
		{ RCAR_GP_PIN(0, 11), 12, 2 },	/* QSPI1_SSL */
		{ RCAR_GP_PIN(0, 10),  8, 2 },	/* QSPI1_IO3 */
		{ RCAR_GP_PIN(0,  9),  4, 2 },	/* QSPI1_IO2 */
		{ RCAR_GP_PIN(0,  8),  0, 2 },	/* QSPI1_MISO_IO1 */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL0", 0xe6058088) {
		{ RCAR_GP_PIN(0, 23), 28, 3 },	/* MMC_SD_CLK */
		{ RCAR_GP_PIN(0, 22), 24, 3 },	/* MMC_SD_D3 */
		{ RCAR_GP_PIN(0, 21), 20, 3 },	/* MMC_SD_D2 */
		{ RCAR_GP_PIN(0, 20), 16, 3 },	/* MMC_SD_D1 */
		{ RCAR_GP_PIN(0, 19), 12, 3 },	/* MMC_SD_D0 */
		{ RCAR_GP_PIN(0, 18),  8, 3 },	/* MMC_SD_CMD */
		{ RCAR_GP_PIN(0, 17),  4, 3 },	/* MMC_DS */
		{ RCAR_GP_PIN(0, 16),  0, 3 },	/* SD_CD */
	} },
	{ PINMUX_DRIVE_REG("DRV3CTRL0", 0xe605808c) {
		{ RCAR_GP_PIN(0, 27), 12, 3 },	/* MMC_D7 */
		{ RCAR_GP_PIN(0, 26),  8, 3 },	/* MMC_D6 */
		{ RCAR_GP_PIN(0, 25),  4, 3 },	/* MMC_D5 */
		{ RCAR_GP_PIN(0, 24),  0, 3 },	/* MMC_D4 */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL1", 0xe6050080) {
		{ RCAR_GP_PIN(1,  7), 28, 3 },	/* MSIOF0_TXD */
		{ RCAR_GP_PIN(1,  6), 24, 3 },	/* MSIOF0_RXD */
		{ RCAR_GP_PIN(1,  5), 20, 3 },	/* HTX0 */
		{ RCAR_GP_PIN(1,  4), 16, 3 },	/* HCTS0_N */
		{ RCAR_GP_PIN(1,  3), 12, 3 },	/* HRTS0_N */
		{ RCAR_GP_PIN(1,  2),  8, 3 },	/* HSCK0 */
		{ RCAR_GP_PIN(1,  1),  4, 3 },	/* HRX0 */
		{ RCAR_GP_PIN(1,  0),  0, 3 },	/* SCIF_CLK */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL1", 0xe6050084) {
		{ RCAR_GP_PIN(1, 15), 28, 3 },	/* MSIOF1_SYNC */
		{ RCAR_GP_PIN(1, 14), 24, 3 },	/* MSIOF1_SCK */
		{ RCAR_GP_PIN(1, 13), 20, 3 },	/* MSIOF1_TXD */
		{ RCAR_GP_PIN(1, 12), 16, 3 },	/* MSIOF1_RXD */
		{ RCAR_GP_PIN(1, 11), 12, 3 },	/* MSIOF0_SS2 */
		{ RCAR_GP_PIN(1, 10),  8, 3 },	/* MSIOF0_SS1 */
		{ RCAR_GP_PIN(1,  9),  4, 3 },	/* MSIOF0_SYNC */
		{ RCAR_GP_PIN(1,  8),  0, 3 },	/* MSIOF0_SCK */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL1", 0xe6050088) {
		{ RCAR_GP_PIN(1, 23), 28, 3 },	/* MSIOF2_SS2 */
		{ RCAR_GP_PIN(1, 22), 24, 3 },	/* MSIOF2_SS1 */
		{ RCAR_GP_PIN(1, 21), 20, 3 },	/* MSIOF2_SYNC */
		{ RCAR_GP_PIN(1, 20), 16, 3 },	/* MSIOF2_SCK */
		{ RCAR_GP_PIN(1, 19), 12, 3 },	/* MSIOF2_TXD */
		{ RCAR_GP_PIN(1, 18),  8, 3 },	/* MSIOF2_RXD */
		{ RCAR_GP_PIN(1, 17),  4, 3 },	/* MSIOF1_SS2 */
		{ RCAR_GP_PIN(1, 16),  0, 3 },	/* MSIOF1_SS1 */
	} },
	{ PINMUX_DRIVE_REG("DRV3CTRL1", 0xe605008c) {
		{ RCAR_GP_PIN(1, 30), 24, 3 },	/* GP1_30 */
		{ RCAR_GP_PIN(1, 29), 20, 3 },	/* GP1_29 */
		{ RCAR_GP_PIN(1, 28), 16, 3 },	/* GP1_28 */
		{ RCAR_GP_PIN(1, 27), 12, 3 },	/* IRQ3 */
		{ RCAR_GP_PIN(1, 26),  8, 3 },	/* IRQ2 */
		{ RCAR_GP_PIN(1, 25),  4, 3 },	/* IRQ1 */
		{ RCAR_GP_PIN(1, 24),  0, 3 },	/* IRQ0 */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL2", 0xe6050880) {
		{ RCAR_GP_PIN(2,  7), 28, 3 },	/* GP2_07 */
		{ RCAR_GP_PIN(2,  6), 24, 3 },	/* GP2_06 */
		{ RCAR_GP_PIN(2,  5), 20, 3 },	/* GP2_05 */
		{ RCAR_GP_PIN(2,  4), 16, 3 },	/* GP2_04 */
		{ RCAR_GP_PIN(2,  3), 12, 3 },	/* GP2_03 */
		{ RCAR_GP_PIN(2,  2),  8, 3 },	/* GP2_02 */
		{ RCAR_GP_PIN(2,  1),  4, 2 },	/* IPC_CLKOUT */
		{ RCAR_GP_PIN(2,  0),  0, 2 },	/* IPC_CLKIN */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL2", 0xe6050884) {
		{ RCAR_GP_PIN(2, 15), 28, 3 },	/* GP2_15 */
		{ RCAR_GP_PIN(2, 14), 24, 3 },	/* GP2_14 */
		{ RCAR_GP_PIN(2, 13), 20, 3 },	/* GP2_13 */
		{ RCAR_GP_PIN(2, 12), 16, 3 },	/* GP2_12 */
		{ RCAR_GP_PIN(2, 11), 12, 3 },	/* GP2_11 */
		{ RCAR_GP_PIN(2, 10),  8, 3 },	/* GP2_10 */
		{ RCAR_GP_PIN(2,  9),  4, 3 },	/* GP2_9 */
		{ RCAR_GP_PIN(2,  8),  0, 3 },	/* GP2_8 */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL2", 0xe6050888) {
		{ RCAR_GP_PIN(2, 23), 28, 3 },	/* TCLK1_A */
		{ RCAR_GP_PIN(2, 22), 24, 3 },	/* TPU0TO1 */
		{ RCAR_GP_PIN(2, 21), 20, 3 },	/* TPU0TO0 */
		{ RCAR_GP_PIN(2, 20), 16, 3 },	/* CLK_EXTFXR */
		{ RCAR_GP_PIN(2, 19), 12, 3 },	/* RXDB_EXTFXR */
		{ RCAR_GP_PIN(2, 18),  8, 3 },	/* FXR_TXDB */
		{ RCAR_GP_PIN(2, 17),  4, 3 },	/* RXDA_EXTFXR_A */
		{ RCAR_GP_PIN(2, 16),  0, 3 },	/* FXR_TXDA_A */
	} },
	{ PINMUX_DRIVE_REG("DRV3CTRL2", 0xe605088c) {
		{ RCAR_GP_PIN(2, 24), 0, 3 },	/* TCLK2_A */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL3", 0xe6058880) {
		{ RCAR_GP_PIN(3,  7), 28, 3 },	/* CANFD3_TX */
		{ RCAR_GP_PIN(3,  6), 24, 3 },	/* CANFD2_RX */
		{ RCAR_GP_PIN(3,  5), 20, 3 },	/* CANFD2_TX */
		{ RCAR_GP_PIN(3,  4), 16, 3 },	/* CANFD1_RX */
		{ RCAR_GP_PIN(3,  3), 12, 3 },	/* CANFD1_TX */
		{ RCAR_GP_PIN(3,  2),  8, 3 },	/* CANFD0_RX */
		{ RCAR_GP_PIN(3,  1),  4, 2 },	/* CANFD0_TX */
		{ RCAR_GP_PIN(3,  0),  0, 2 },	/* CAN_CLK */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL3", 0xe6058884) {
		{ RCAR_GP_PIN(3, 15), 28, 3 },	/* CANFD7_TX */
		{ RCAR_GP_PIN(3, 14), 24, 3 },	/* CANFD6_RX */
		{ RCAR_GP_PIN(3, 13), 20, 3 },	/* CANFD6_TX */
		{ RCAR_GP_PIN(3, 12), 16, 3 },	/* CANFD5_RX */
		{ RCAR_GP_PIN(3, 11), 12, 3 },	/* CANFD5_TX */
		{ RCAR_GP_PIN(3, 10),  8, 3 },	/* CANFD4_RX */
		{ RCAR_GP_PIN(3,  9),  4, 3 },	/* CANFD4_TX*/
		{ RCAR_GP_PIN(3,  8),  0, 3 },	/* CANFD3_RX */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL3", 0xe6058888) {
		{ RCAR_GP_PIN(3,  16),  0, 3 },	/* CANFD7_RX */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL4", 0xe6060080) {
		{ RCAR_GP_PIN(4,  7), 28, 3 },	/* AVB0_TXC */
		{ RCAR_GP_PIN(4,  6), 24, 3 },	/* AVB0_TX_CTL */
		{ RCAR_GP_PIN(4,  5), 20, 3 },	/* AVB0_RD3 */
		{ RCAR_GP_PIN(4,  4), 16, 3 },	/* AVB0_RD2 */
		{ RCAR_GP_PIN(4,  3), 12, 3 },	/* AVB0_RD1 */
		{ RCAR_GP_PIN(4,  2),  8, 3 },	/* AVB0_RD0 */
		{ RCAR_GP_PIN(4,  1),  4, 3 },	/* AVB0_RXC */
		{ RCAR_GP_PIN(4,  0),  0, 3 },	/* AVB0_RX_CTL */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL4", 0xe6060084) {
		{ RCAR_GP_PIN(4, 15), 28, 3 },	/* AVB0_MAGIC */
		{ RCAR_GP_PIN(4, 14), 24, 3 },	/* AVB0_MDC */
		{ RCAR_GP_PIN(4, 13), 20, 3 },	/* AVB0_MDIO */
		{ RCAR_GP_PIN(4, 12), 16, 3 },	/* AVB0_TXCREFCLK */
		{ RCAR_GP_PIN(4, 11), 12, 3 },	/* AVB0_TD3 */
		{ RCAR_GP_PIN(4, 10),  8, 3 },	/* AVB0_TD2 */
		{ RCAR_GP_PIN(4,  9),  4, 3 },	/* AVB0_TD1*/
		{ RCAR_GP_PIN(4,  8),  0, 3 },	/* AVB0_TD0 */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL4", 0xe6060088) {
		{ RCAR_GP_PIN(4, 23), 28, 3 },	/* PCIE2_CLKREQ_N */
		{ RCAR_GP_PIN(4, 22), 24, 3 },	/* PCIE1_CLKREQ_N */
		{ RCAR_GP_PIN(4, 21), 20, 3 },	/* PCIE0_CLKREQ_N */
		{ RCAR_GP_PIN(4, 20), 16, 3 },	/* AVB0_AVTP_PPS */
		{ RCAR_GP_PIN(4, 19), 12, 3 },	/* AVB0_AVTP_CAPTURE */
		{ RCAR_GP_PIN(4, 18),  8, 3 },	/* AVB0_AVTP_MATCH */
		{ RCAR_GP_PIN(4, 17),  4, 3 },	/* AVB0_LINK */
		{ RCAR_GP_PIN(4, 16),  0, 3 },	/* AVB0_PHY_INT */
	} },
	{ PINMUX_DRIVE_REG("DRV3CTRL4", 0xe606008c) {
		{ RCAR_GP_PIN(4, 26),  8, 3 },	/* AVS1 */
		{ RCAR_GP_PIN(4, 25),  4, 3 },	/* AVS0 */
		{ RCAR_GP_PIN(4, 24),  0, 3 },	/* PCIE3_CLKREQ_N */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL5", 0xe6060880) {
		{ RCAR_GP_PIN(5,  7), 28, 3 },	/* AVB1_TXC */
		{ RCAR_GP_PIN(5,  6), 24, 3 },	/* AVB1_TX_CTL */
		{ RCAR_GP_PIN(5,  5), 20, 3 },	/* AVB1_RD3 */
		{ RCAR_GP_PIN(5,  4), 16, 3 },	/* AVB1_RD2 */
		{ RCAR_GP_PIN(5,  3), 12, 3 },	/* AVB1_RD1 */
		{ RCAR_GP_PIN(5,  2),  8, 3 },	/* AVB1_RD0 */
		{ RCAR_GP_PIN(5,  1),  4, 3 },	/* AVB1_RXC */
		{ RCAR_GP_PIN(5,  0),  0, 3 },	/* AVB1_RX_CTL */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL5", 0xe6060884) {
		{ RCAR_GP_PIN(5, 15), 28, 3 },	/* AVB1_MAGIC */
		{ RCAR_GP_PIN(5, 14), 24, 3 },	/* AVB1_MDC */
		{ RCAR_GP_PIN(5, 13), 20, 3 },	/* AVB1_MDIO */
		{ RCAR_GP_PIN(5, 12), 16, 3 },	/* AVB1_TXCREFCLK */
		{ RCAR_GP_PIN(5, 11), 12, 3 },	/* AVB1_TD3 */
		{ RCAR_GP_PIN(5, 10),  8, 3 },	/* AVB1_TD2 */
		{ RCAR_GP_PIN(5,  9),  4, 3 },	/* AVB1_TD1*/
		{ RCAR_GP_PIN(5,  8),  0, 3 },	/* AVB1_TD0 */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL5", 0xe6060888) {
		{ RCAR_GP_PIN(5, 20), 16, 3 },	/* AVB1_AVTP_PPS */
		{ RCAR_GP_PIN(5, 19), 12, 3 },	/* AVB1_AVTP_CAPTURE */
		{ RCAR_GP_PIN(5, 18),  8, 3 },	/* AVB1_AVTP_MATCH */
		{ RCAR_GP_PIN(5, 17),  4, 3 },	/* AVB1_LINK */
		{ RCAR_GP_PIN(5, 16),  0, 3 },	/* AVB1_PHY_INT */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL6", 0xe6068080) {
		{ RCAR_GP_PIN(6,  7), 28, 3 },	/* AVB2_TXC */
		{ RCAR_GP_PIN(6,  6), 24, 3 },	/* AVB2_TX_CTL */
		{ RCAR_GP_PIN(6,  5), 20, 3 },	/* AVB2_RD3 */
		{ RCAR_GP_PIN(6,  4), 16, 3 },	/* AVB2_RD2 */
		{ RCAR_GP_PIN(6,  3), 12, 3 },	/* AVB2_RD1 */
		{ RCAR_GP_PIN(6,  2),  8, 3 },	/* AVB2_RD0 */
		{ RCAR_GP_PIN(6,  1),  4, 3 },	/* AVB2_RXC */
		{ RCAR_GP_PIN(6,  0),  0, 3 },	/* AVB2_RX_CTL */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL6", 0xe6068084) {
		{ RCAR_GP_PIN(6, 15), 28, 3 },	/* AVB2_MAGIC */
		{ RCAR_GP_PIN(6, 14), 24, 3 },	/* AVB2_MDC */
		{ RCAR_GP_PIN(6, 13), 20, 3 },	/* AVB2_MDIO */
		{ RCAR_GP_PIN(6, 12), 16, 3 },	/* AVB2_TXCREFCLK */
		{ RCAR_GP_PIN(6, 11), 12, 3 },	/* AVB2_TD3 */
		{ RCAR_GP_PIN(6, 10),  8, 3 },	/* AVB2_TD2 */
		{ RCAR_GP_PIN(6,  9),  4, 3 },	/* AVB2_TD1*/
		{ RCAR_GP_PIN(6,  8),  0, 3 },	/* AVB2_TD0 */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL6", 0xe6068088) {
		{ RCAR_GP_PIN(6, 20), 16, 3 },	/* AVB2_AVTP_PPS */
		{ RCAR_GP_PIN(6, 19), 12, 3 },	/* AVB2_AVTP_CAPTURE */
		{ RCAR_GP_PIN(6, 18),  8, 3 },	/* AVB2_AVTP_MATCH */
		{ RCAR_GP_PIN(6, 17),  4, 3 },	/* AVB2_LINK */
		{ RCAR_GP_PIN(6, 16),  0, 3 },	/* AVB2_PHY_INT */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL7", 0xe6068880) {
		{ RCAR_GP_PIN(7,  7), 28, 3 },	/* AVB3_TXC */
		{ RCAR_GP_PIN(7,  6), 24, 3 },	/* AVB3_TX_CTL */
		{ RCAR_GP_PIN(7,  5), 20, 3 },	/* AVB3_RD3 */
		{ RCAR_GP_PIN(7,  4), 16, 3 },	/* AVB3_RD2 */
		{ RCAR_GP_PIN(7,  3), 12, 3 },	/* AVB3_RD1 */
		{ RCAR_GP_PIN(7,  2),  8, 3 },	/* AVB3_RD0 */
		{ RCAR_GP_PIN(7,  1),  4, 3 },	/* AVB3_RXC */
		{ RCAR_GP_PIN(7,  0),  0, 3 },	/* AVB3_RX_CTL */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL7", 0xe6068884) {
		{ RCAR_GP_PIN(7, 15), 28, 3 },	/* AVB3_MAGIC */
		{ RCAR_GP_PIN(7, 14), 24, 3 },	/* AVB3_MDC */
		{ RCAR_GP_PIN(7, 13), 20, 3 },	/* AVB3_MDIO */
		{ RCAR_GP_PIN(7, 12), 16, 3 },	/* AVB3_TXCREFCLK */
		{ RCAR_GP_PIN(7, 11), 12, 3 },	/* AVB3_TD3 */
		{ RCAR_GP_PIN(7, 10),  8, 3 },	/* AVB3_TD2 */
		{ RCAR_GP_PIN(7,  9),  4, 3 },	/* AVB3_TD1*/
		{ RCAR_GP_PIN(7,  8),  0, 3 },	/* AVB3_TD0 */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL7", 0xe6068888) {
		{ RCAR_GP_PIN(7, 20), 16, 3 },	/* AVB3_AVTP_PPS */
		{ RCAR_GP_PIN(7, 19), 12, 3 },	/* AVB3_AVTP_CAPTURE */
		{ RCAR_GP_PIN(7, 18),  8, 3 },	/* AVB3_AVTP_MATCH */
		{ RCAR_GP_PIN(7, 17),  4, 3 },	/* AVB3_LINK */
		{ RCAR_GP_PIN(7, 16),  0, 3 },	/* AVB3_PHY_INT */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL8", 0xe6069080) {
		{ RCAR_GP_PIN(8,  7), 28, 3 },	/* AVB4_TXC */
		{ RCAR_GP_PIN(8,  6), 24, 3 },	/* AVB4_TX_CTL */
		{ RCAR_GP_PIN(8,  5), 20, 3 },	/* AVB4_RD3 */
		{ RCAR_GP_PIN(8,  4), 16, 3 },	/* AVB4_RD2 */
		{ RCAR_GP_PIN(8,  3), 12, 3 },	/* AVB4_RD1 */
		{ RCAR_GP_PIN(8,  2),  8, 3 },	/* AVB4_RD0 */
		{ RCAR_GP_PIN(8,  1),  4, 3 },	/* AVB4_RXC */
		{ RCAR_GP_PIN(8,  0),  0, 3 },	/* AVB4_RX_CTL */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL8", 0xe6069084) {
		{ RCAR_GP_PIN(8, 15), 28, 3 },	/* AVB4_MAGIC */
		{ RCAR_GP_PIN(8, 14), 24, 3 },	/* AVB4_MDC */
		{ RCAR_GP_PIN(8, 13), 20, 3 },	/* AVB4_MDIO */
		{ RCAR_GP_PIN(8, 12), 16, 3 },	/* AVB4_TXCREFCLK */
		{ RCAR_GP_PIN(8, 11), 12, 3 },	/* AVB4_TD3 */
		{ RCAR_GP_PIN(8, 10),  8, 3 },	/* AVB4_TD2 */
		{ RCAR_GP_PIN(8,  9),  4, 3 },	/* AVB4_TD1*/
		{ RCAR_GP_PIN(8,  8),  0, 3 },	/* AVB4_TD0 */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL8", 0xe6069088) {
		{ RCAR_GP_PIN(8, 20), 16, 3 },	/* AVB4_AVTP_PPS */
		{ RCAR_GP_PIN(8, 19), 12, 3 },	/* AVB4_AVTP_CAPTURE */
		{ RCAR_GP_PIN(8, 18),  8, 3 },	/* AVB4_AVTP_MATCH */
		{ RCAR_GP_PIN(8, 17),  4, 3 },	/* AVB4_LINK */
		{ RCAR_GP_PIN(8, 16),  0, 3 },	/* AVB4_PHY_INT */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL9", 0xe6069880) {
		{ RCAR_GP_PIN(9,  7), 28, 3 },	/* AVB5_TXC */
		{ RCAR_GP_PIN(9,  6), 24, 3 },	/* AVB5_TX_CTL */
		{ RCAR_GP_PIN(9,  5), 20, 3 },	/* AVB5_RD3 */
		{ RCAR_GP_PIN(9,  4), 16, 3 },	/* AVB5_RD2 */
		{ RCAR_GP_PIN(9,  3), 12, 3 },	/* AVB5_RD1 */
		{ RCAR_GP_PIN(9,  2),  8, 3 },	/* AVB5_RD0 */
		{ RCAR_GP_PIN(9,  1),  4, 3 },	/* AVB5_RXC */
		{ RCAR_GP_PIN(9,  0),  0, 3 },	/* AVB5_RX_CTL */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL9", 0xe6069884) {
		{ RCAR_GP_PIN(9, 15), 28, 3 },	/* AVB5_MAGIC */
		{ RCAR_GP_PIN(9, 14), 24, 3 },	/* AVB5_MDC */
		{ RCAR_GP_PIN(9, 13), 20, 3 },	/* AVB5_MDIO */
		{ RCAR_GP_PIN(9, 12), 16, 3 },	/* AVB5_TXCREFCLK */
		{ RCAR_GP_PIN(9, 11), 12, 3 },	/* AVB5_TD3 */
		{ RCAR_GP_PIN(9, 10),  8, 3 },	/* AVB5_TD2 */
		{ RCAR_GP_PIN(9,  9),  4, 3 },	/* AVB5_TD1*/
		{ RCAR_GP_PIN(9,  8),  0, 3 },	/* AVB5_TD0 */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL9", 0xe6069888) {
		{ RCAR_GP_PIN(9, 20), 16, 3 },	/* AVB5_AVTP_PPS */
		{ RCAR_GP_PIN(9, 19), 12, 3 },	/* AVB5_AVTP_CAPTURE */
		{ RCAR_GP_PIN(9, 18),  8, 3 },	/* AVB5_AVTP_MATCH */
		{ RCAR_GP_PIN(9, 17),  4, 3 },	/* AVB5_LINK */
		{ RCAR_GP_PIN(9, 16),  0, 3 },	/* AVB5_PHY_INT */
	} },
	{ },
};

enum ioctrl_regs {
	POC0,
	POC1,
	POC2,
	POC4,
	POC5,
	POC6,
	POC7,
	POC8,
	POC9,
	TD1SEL0,
};

static const struct pinmux_ioctrl_reg pinmux_ioctrl_regs[] = {
	[POC0] = { 0xe60580a0, },
	[POC1] = { 0xe60500a0, },
	[POC2] = { 0xe60508a0, },
	[POC4] = { 0xe60600a0, },
	[POC5] = { 0xe60608a0, },
	[POC6] = { 0xe60680a0, },
	[POC7] = { 0xe60688a0, },
	[POC8] = { 0xe60690a0, },
	[POC9] = { 0xe60698a0, },
	[TD1SEL0] = { 0xe6058124, },
	{ /* sentinel */ },
};

static int r8a779a0_pin_to_pocctrl(struct sh_pfc *pfc, unsigned int pin,
				   u32 *pocctrl)
{
	int bit = pin & 0x1f;

	*pocctrl = pinmux_ioctrl_regs[POC0].reg;
	if (pin >= RCAR_GP_PIN(0, 15) && pin <= RCAR_GP_PIN(0, 27))
		return bit;

	*pocctrl = pinmux_ioctrl_regs[POC1].reg;
	if (pin >= RCAR_GP_PIN(1, 0) && pin <= RCAR_GP_PIN(1, 30))
		return bit;

	*pocctrl = pinmux_ioctrl_regs[POC2].reg;
	if (pin >= RCAR_GP_PIN(2, 2) && pin <= RCAR_GP_PIN(2, 15))
		return bit;

	*pocctrl = pinmux_ioctrl_regs[POC4].reg;
	if (pin >= RCAR_GP_PIN(4, 0) && pin <= RCAR_GP_PIN(4, 17))
		return bit;

	*pocctrl = pinmux_ioctrl_regs[POC5].reg;
	if (pin >= RCAR_GP_PIN(5, 0) && pin <= RCAR_GP_PIN(5, 17))
		return bit;

	*pocctrl = pinmux_ioctrl_regs[POC6].reg;
	if (pin >= RCAR_GP_PIN(6, 0) && pin <= RCAR_GP_PIN(6, 17))
		return bit;

	*pocctrl = pinmux_ioctrl_regs[POC7].reg;
	if (pin >= RCAR_GP_PIN(7, 0) && pin <= RCAR_GP_PIN(7, 17))
		return bit;

	*pocctrl = pinmux_ioctrl_regs[POC8].reg;
	if (pin >= RCAR_GP_PIN(8, 0) && pin <= RCAR_GP_PIN(8, 17))
		return bit;

	*pocctrl = pinmux_ioctrl_regs[POC9].reg;
	if (pin >= RCAR_GP_PIN(9, 0) && pin <= RCAR_GP_PIN(9, 17))
		return bit;

	return -EINVAL;
}

static const struct pinmux_bias_reg pinmux_bias_regs[] = {
	{ PINMUX_BIAS_REG("PUEN0", 0xe60580c0, "PUD0", 0xe60580e0) {
		[ 0] = RCAR_GP_PIN(0,  0),	/* QSPI0_SPCLK */
		[ 1] = RCAR_GP_PIN(0,  1),	/* QSPI0_MOSI_IO0 */
		[ 2] = RCAR_GP_PIN(0,  2),	/* QSPI0_MISO_IO1 */
		[ 3] = RCAR_GP_PIN(0,  3),	/* QSPI0_IO2 */
		[ 4] = RCAR_GP_PIN(0,  4),	/* QSPI0_IO3 */
		[ 5] = RCAR_GP_PIN(0,  5),	/* QSPI0_SSL */
		[ 6] = RCAR_GP_PIN(0,  6),	/* QSPI1_SPCLK */
		[ 7] = RCAR_GP_PIN(0,  7),	/* QSPI1_MOSI_IO0 */
		[ 8] = RCAR_GP_PIN(0,  8),	/* QSPI1_MISO_IO1 */
		[ 9] = RCAR_GP_PIN(0,  9),	/* QSPI1_IO2 */
		[10] = RCAR_GP_PIN(0, 10),	/* QSPI1_IO3 */
		[11] = RCAR_GP_PIN(0, 11),	/* QSPI1_SSL */
		[12] = RCAR_GP_PIN(0, 12),	/* RPC_RESET_N */
		[13] = RCAR_GP_PIN(0, 13),	/* RPC_WP_N */
		[14] = RCAR_GP_PIN(0, 14),	/* RPC_INT_N */
		[15] = RCAR_GP_PIN(0, 15),	/* SD_WP */
		[16] = RCAR_GP_PIN(0, 16),	/* SD_CD */
		[17] = RCAR_GP_PIN(0, 17),	/* MMC_DS */
		[18] = RCAR_GP_PIN(0, 18),	/* MMC_SD_CMD */
		[19] = RCAR_GP_PIN(0, 19),	/* MMC_SD_D0 */
		[20] = RCAR_GP_PIN(0, 20),	/* MMC_SD_D1 */
		[21] = RCAR_GP_PIN(0, 21),	/* MMC_SD_D2 */
		[22] = RCAR_GP_PIN(0, 22),	/* MMC_SD_D3 */
		[23] = RCAR_GP_PIN(0, 23),	/* MMC_SD_CLK */
		[24] = RCAR_GP_PIN(0, 24),	/* MMC_D4 */
		[25] = RCAR_GP_PIN(0, 25),	/* MMC_D5 */
		[26] = RCAR_GP_PIN(0, 26),	/* MMC_D6 */
		[27] = RCAR_GP_PIN(0, 27),	/* MMC_D7 */
		[28] = SH_PFC_PIN_NONE,
		[29] = SH_PFC_PIN_NONE,
		[30] = SH_PFC_PIN_NONE,
		[31] = SH_PFC_PIN_NONE,
	} },
	{ PINMUX_BIAS_REG("PUEN1", 0xe60500c0, "PUD1", 0xe60500e0) {
		[ 0] = RCAR_GP_PIN(1,  0),	/* SCIF_CLK */
		[ 1] = RCAR_GP_PIN(1,  1),	/* HRX0 */
		[ 2] = RCAR_GP_PIN(1,  2),	/* HSCK0 */
		[ 3] = RCAR_GP_PIN(1,  3),	/* HRTS0_N */
		[ 4] = RCAR_GP_PIN(1,  4),	/* HCTS0_N */
		[ 5] = RCAR_GP_PIN(1,  5),	/* HTX0 */
		[ 6] = RCAR_GP_PIN(1,  6),	/* MSIOF0_RXD */
		[ 7] = RCAR_GP_PIN(1,  7),	/* MSIOF0_TXD */
		[ 8] = RCAR_GP_PIN(1,  8),	/* MSIOF0_SCK */
		[ 9] = RCAR_GP_PIN(1,  9),	/* MSIOF0_SYNC */
		[10] = RCAR_GP_PIN(1, 10),	/* MSIOF0_SS1 */
		[11] = RCAR_GP_PIN(1, 11),	/* MSIOF0_SS2 */
		[12] = RCAR_GP_PIN(1, 12),	/* MSIOF1_RXD */
		[13] = RCAR_GP_PIN(1, 13),	/* MSIOF1_TXD */
		[14] = RCAR_GP_PIN(1, 14),	/* MSIOF1_SCK */
		[15] = RCAR_GP_PIN(1, 15),	/* MSIOF1_SYNC */
		[16] = RCAR_GP_PIN(1, 16),	/* MSIOF1_SS1 */
		[17] = RCAR_GP_PIN(1, 17),	/* MSIOF1_SS2 */
		[18] = RCAR_GP_PIN(1, 18),	/* MSIOF2_RXD */
		[19] = RCAR_GP_PIN(1, 19),	/* MSIOF2_TXD */
		[20] = RCAR_GP_PIN(1, 20),	/* MSIOF2_SCK */
		[21] = RCAR_GP_PIN(1, 21),	/* MSIOF2_SYNC */
		[22] = RCAR_GP_PIN(1, 22),	/* MSIOF2_SS1 */
		[23] = RCAR_GP_PIN(1, 23),	/* MSIOF2_SS2 */
		[24] = RCAR_GP_PIN(1, 24),	/* IRQ0 */
		[25] = RCAR_GP_PIN(1, 25),	/* IRQ1 */
		[26] = RCAR_GP_PIN(1, 26),	/* IRQ2 */
		[27] = RCAR_GP_PIN(1, 27),	/* IRQ3 */
		[28] = RCAR_GP_PIN(1, 28),	/* GP1_28 */
		[29] = RCAR_GP_PIN(1, 29),	/* GP1_29 */
		[30] = RCAR_GP_PIN(1, 30),	/* GP1_30 */
		[31] = SH_PFC_PIN_NONE,
	} },
	{ PINMUX_BIAS_REG("PUEN2", 0xe60508c0, "PUD2", 0xe60508e0) {
		[ 0] = RCAR_GP_PIN(2,  0),	/* IPC_CLKIN */
		[ 1] = RCAR_GP_PIN(2,  1),	/* IPC_CLKOUT */
		[ 2] = RCAR_GP_PIN(2,  2),	/* GP2_02 */
		[ 3] = RCAR_GP_PIN(2,  3),	/* GP2_03 */
		[ 4] = RCAR_GP_PIN(2,  4),	/* GP2_04 */
		[ 5] = RCAR_GP_PIN(2,  5),	/* GP2_05 */
		[ 6] = RCAR_GP_PIN(2,  6),	/* GP2_06 */
		[ 7] = RCAR_GP_PIN(2,  7),	/* GP2_07 */
		[ 8] = RCAR_GP_PIN(2,  8),	/* GP2_08 */
		[ 9] = RCAR_GP_PIN(2,  9),	/* GP2_09 */
		[10] = RCAR_GP_PIN(2, 10),	/* GP2_10 */
		[11] = RCAR_GP_PIN(2, 11),	/* GP2_11 */
		[12] = RCAR_GP_PIN(2, 12),	/* GP2_12 */
		[13] = RCAR_GP_PIN(2, 13),	/* GP2_13 */
		[14] = RCAR_GP_PIN(2, 14),	/* GP2_14 */
		[15] = RCAR_GP_PIN(2, 15),	/* GP2_15 */
		[16] = RCAR_GP_PIN(2, 16),	/* FXR_TXDA_A */
		[17] = RCAR_GP_PIN(2, 17),	/* RXDA_EXTFXR_A */
		[18] = RCAR_GP_PIN(2, 18),	/* FXR_TXDB */
		[19] = RCAR_GP_PIN(2, 19),	/* RXDB_EXTFXR */
		[20] = RCAR_GP_PIN(2, 20),	/* CLK_EXTFXR */
		[21] = RCAR_GP_PIN(2, 21),	/* TPU0TO0 */
		[22] = RCAR_GP_PIN(2, 22),	/* TPU0TO1 */
		[23] = RCAR_GP_PIN(2, 23),	/* TCLK1_A */
		[24] = RCAR_GP_PIN(2, 24),	/* TCLK2_A */
		[25] = SH_PFC_PIN_NONE,
		[26] = SH_PFC_PIN_NONE,
		[27] = SH_PFC_PIN_NONE,
		[28] = SH_PFC_PIN_NONE,
		[29] = SH_PFC_PIN_NONE,
		[30] = SH_PFC_PIN_NONE,
		[31] = SH_PFC_PIN_NONE,
	} },
	{ PINMUX_BIAS_REG("PUEN3", 0xe60588c0, "PUD3", 0xe60588e0) {
		[ 0] = RCAR_GP_PIN(3,  0),	/* CAN_CLK */
		[ 1] = RCAR_GP_PIN(3,  1),	/* CANFD0_TX */
		[ 2] = RCAR_GP_PIN(3,  2),	/* CANFD0_RX */
		[ 3] = RCAR_GP_PIN(3,  3),	/* CANFD1_TX */
		[ 4] = RCAR_GP_PIN(3,  4),	/* CANFD1_RX */
		[ 5] = RCAR_GP_PIN(3,  5),	/* CANFD2_TX */
		[ 6] = RCAR_GP_PIN(3,  6),	/* CANFD2_RX */
		[ 7] = RCAR_GP_PIN(3,  7),	/* CANFD3_TX */
		[ 8] = RCAR_GP_PIN(3,  8),	/* CANFD3_RX */
		[ 9] = RCAR_GP_PIN(3,  9),	/* CANFD4_TX */
		[10] = RCAR_GP_PIN(3, 10),	/* CANFD4_RX */
		[11] = RCAR_GP_PIN(3, 11),	/* CANFD5_TX */
		[12] = RCAR_GP_PIN(3, 12),	/* CANFD5_RX */
		[13] = RCAR_GP_PIN(3, 13),	/* CANFD6_TX */
		[14] = RCAR_GP_PIN(3, 14),	/* CANFD6_RX */
		[15] = RCAR_GP_PIN(3, 15),	/* CANFD7_TX */
		[16] = RCAR_GP_PIN(3, 16),	/* CANFD7_RX */
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
	{ PINMUX_BIAS_REG("PUEN4", 0xe60600c0, "PUD4", 0xe60600e0) {
		[ 0] = RCAR_GP_PIN(4,  0),	/* AVB0_RX_CTL */
		[ 1] = RCAR_GP_PIN(4,  1),	/* AVB0_RXC */
		[ 2] = RCAR_GP_PIN(4,  2),	/* AVB0_RD0 */
		[ 3] = RCAR_GP_PIN(4,  3),	/* AVB0_RD1 */
		[ 4] = RCAR_GP_PIN(4,  4),	/* AVB0_RD2 */
		[ 5] = RCAR_GP_PIN(4,  5),	/* AVB0_RD3 */
		[ 6] = RCAR_GP_PIN(4,  6),	/* AVB0_TX_CTL */
		[ 7] = RCAR_GP_PIN(4,  7),	/* AVB0_TXC */
		[ 8] = RCAR_GP_PIN(4,  8),	/* AVB0_TD0 */
		[ 9] = RCAR_GP_PIN(4,  9),	/* AVB0_TD1 */
		[10] = RCAR_GP_PIN(4, 10),	/* AVB0_TD2 */
		[11] = RCAR_GP_PIN(4, 11),	/* AVB0_TD3 */
		[12] = RCAR_GP_PIN(4, 12),	/* AVB0_TXREFCLK */
		[13] = RCAR_GP_PIN(4, 13),	/* AVB0_MDIO */
		[14] = RCAR_GP_PIN(4, 14),	/* AVB0_MDC */
		[15] = RCAR_GP_PIN(4, 15),	/* AVB0_MAGIC */
		[16] = RCAR_GP_PIN(4, 16),	/* AVB0_PHY_INT */
		[17] = RCAR_GP_PIN(4, 17),	/* AVB0_LINK */
		[18] = RCAR_GP_PIN(4, 18),	/* AVB0_AVTP_MATCH */
		[19] = RCAR_GP_PIN(4, 19),	/* AVB0_AVTP_CAPTURE */
		[20] = RCAR_GP_PIN(4, 20),	/* AVB0_AVTP_PPS */
		[21] = RCAR_GP_PIN(4, 21),	/* PCIE0_CLKREQ_N */
		[22] = RCAR_GP_PIN(4, 22),	/* PCIE1_CLKREQ_N */
		[23] = RCAR_GP_PIN(4, 23),	/* PCIE2_CLKREQ_N */
		[24] = RCAR_GP_PIN(4, 24),	/* PCIE3_CLKREQ_N */
		[25] = RCAR_GP_PIN(4, 25),	/* AVS0 */
		[26] = RCAR_GP_PIN(4, 26),	/* AVS1 */
		[27] = SH_PFC_PIN_NONE,
		[28] = SH_PFC_PIN_NONE,
		[29] = SH_PFC_PIN_NONE,
		[30] = SH_PFC_PIN_NONE,
		[31] = SH_PFC_PIN_NONE,
	} },
	{ PINMUX_BIAS_REG("PUEN5", 0xe60608c0, "PUD5", 0xe60608e0) {
		[ 0] = RCAR_GP_PIN(5,  0),	/* AVB1_RX_CTL */
		[ 1] = RCAR_GP_PIN(5,  1),	/* AVB1_RXC */
		[ 2] = RCAR_GP_PIN(5,  2),	/* AVB1_RD0 */
		[ 3] = RCAR_GP_PIN(5,  3),	/* AVB1_RD1 */
		[ 4] = RCAR_GP_PIN(5,  4),	/* AVB1_RD2 */
		[ 5] = RCAR_GP_PIN(5,  5),	/* AVB1_RD3 */
		[ 6] = RCAR_GP_PIN(5,  6),	/* AVB1_TX_CTL */
		[ 7] = RCAR_GP_PIN(5,  7),	/* AVB1_TXC */
		[ 8] = RCAR_GP_PIN(5,  8),	/* AVB1_TD0 */
		[ 9] = RCAR_GP_PIN(5,  9),	/* AVB1_TD1 */
		[10] = RCAR_GP_PIN(5, 10),	/* AVB1_TD2 */
		[11] = RCAR_GP_PIN(5, 11),	/* AVB1_TD3 */
		[12] = RCAR_GP_PIN(5, 12),	/* AVB1_TXCREFCLK */
		[13] = RCAR_GP_PIN(5, 13),	/* AVB1_MDIO */
		[14] = RCAR_GP_PIN(5, 14),	/* AVB1_MDC */
		[15] = RCAR_GP_PIN(5, 15),	/* AVB1_MAGIC */
		[16] = RCAR_GP_PIN(5, 16),	/* AVB1_PHY_INT */
		[17] = RCAR_GP_PIN(5, 17),	/* AVB1_LINK */
		[18] = RCAR_GP_PIN(5, 18),	/* AVB1_AVTP_MATCH */
		[19] = RCAR_GP_PIN(5, 19),	/* AVB1_AVTP_CAPTURE */
		[20] = RCAR_GP_PIN(5, 20),	/* AVB1_AVTP_PPS */
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
	{ PINMUX_BIAS_REG("PUEN6", 0xe60680c0, "PUD6", 0xe60680e0) {
		[ 0] = RCAR_GP_PIN(6,  0),	/* AVB2_RX_CTL */
		[ 1] = RCAR_GP_PIN(6,  1),	/* AVB2_RXC */
		[ 2] = RCAR_GP_PIN(6,  2),	/* AVB2_RD0 */
		[ 3] = RCAR_GP_PIN(6,  3),	/* AVB2_RD1 */
		[ 4] = RCAR_GP_PIN(6,  4),	/* AVB2_RD2 */
		[ 5] = RCAR_GP_PIN(6,  5),	/* AVB2_RD3 */
		[ 6] = RCAR_GP_PIN(6,  6),	/* AVB2_TX_CTL */
		[ 7] = RCAR_GP_PIN(6,  7),	/* AVB2_TXC */
		[ 8] = RCAR_GP_PIN(6,  8),	/* AVB2_TD0 */
		[ 9] = RCAR_GP_PIN(6,  9),	/* AVB2_TD1 */
		[10] = RCAR_GP_PIN(6, 10),	/* AVB2_TD2 */
		[11] = RCAR_GP_PIN(6, 11),	/* AVB2_TD3 */
		[12] = RCAR_GP_PIN(6, 12),	/* AVB2_TXCREFCLK */
		[13] = RCAR_GP_PIN(6, 13),	/* AVB2_MDIO */
		[14] = RCAR_GP_PIN(6, 14),	/* AVB2_MDC*/
		[15] = RCAR_GP_PIN(6, 15),	/* AVB2_MAGIC */
		[16] = RCAR_GP_PIN(6, 16),	/* AVB2_PHY_INT */
		[17] = RCAR_GP_PIN(6, 17),	/* AVB2_LINK */
		[18] = RCAR_GP_PIN(6, 18),	/* AVB2_AVTP_MATCH */
		[19] = RCAR_GP_PIN(6, 19),	/* AVB2_AVTP_CAPTURE */
		[20] = RCAR_GP_PIN(6, 20),	/* AVB2_AVTP_PPS */
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
	{ PINMUX_BIAS_REG("PUEN7", 0xe60688c0, "PUD7", 0xe60688e0) {
		[ 0] = RCAR_GP_PIN(7,  0),	/* AVB3_RX_CTL */
		[ 1] = RCAR_GP_PIN(7,  1),	/* AVB3_RXC */
		[ 2] = RCAR_GP_PIN(7,  2),	/* AVB3_RD0 */
		[ 3] = RCAR_GP_PIN(7,  3),	/* AVB3_RD1 */
		[ 4] = RCAR_GP_PIN(7,  4),	/* AVB3_RD2 */
		[ 5] = RCAR_GP_PIN(7,  5),	/* AVB3_RD3 */
		[ 6] = RCAR_GP_PIN(7,  6),	/* AVB3_TX_CTL */
		[ 7] = RCAR_GP_PIN(7,  7),	/* AVB3_TXC */
		[ 8] = RCAR_GP_PIN(7,  8),	/* AVB3_TD0 */
		[ 9] = RCAR_GP_PIN(7,  9),	/* AVB3_TD1 */
		[10] = RCAR_GP_PIN(7, 10),	/* AVB3_TD2 */
		[11] = RCAR_GP_PIN(7, 11),	/* AVB3_TD3 */
		[12] = RCAR_GP_PIN(7, 12),	/* AVB3_TXCREFCLK */
		[13] = RCAR_GP_PIN(7, 13),	/* AVB3_MDIO */
		[14] = RCAR_GP_PIN(7, 14),	/* AVB3_MDC */
		[15] = RCAR_GP_PIN(7, 15),	/* AVB3_MAGIC */
		[16] = RCAR_GP_PIN(7, 16),	/* AVB3_PHY_INT */
		[17] = RCAR_GP_PIN(7, 17),	/* AVB3_LINK */
		[18] = RCAR_GP_PIN(7, 18),	/* AVB3_AVTP_MATCH */
		[19] = RCAR_GP_PIN(7, 19),	/* AVB3_AVTP_CAPTURE */
		[20] = RCAR_GP_PIN(7, 20),	/* AVB3_AVTP_PPS */
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
	{ PINMUX_BIAS_REG("PUEN8", 0xe60690c0, "PUD8", 0xe60690e0) {
		[ 0] = RCAR_GP_PIN(8,  0),	/* AVB4_RX_CTL */
		[ 1] = RCAR_GP_PIN(8,  1),	/* AVB4_RXC */
		[ 2] = RCAR_GP_PIN(8,  2),	/* AVB4_RD0 */
		[ 3] = RCAR_GP_PIN(8,  3),	/* AVB4_RD1 */
		[ 4] = RCAR_GP_PIN(8,  4),	/* AVB4_RD2 */
		[ 5] = RCAR_GP_PIN(8,  5),	/* AVB4_RD3 */
		[ 6] = RCAR_GP_PIN(8,  6),	/* AVB4_TX_CTL */
		[ 7] = RCAR_GP_PIN(8,  7),	/* AVB4_TXC */
		[ 8] = RCAR_GP_PIN(8,  8),	/* AVB4_TD0 */
		[ 9] = RCAR_GP_PIN(8,  9),	/* AVB4_TD1 */
		[10] = RCAR_GP_PIN(8, 10),	/* AVB4_TD2 */
		[11] = RCAR_GP_PIN(8, 11),	/* AVB4_TD3 */
		[12] = RCAR_GP_PIN(8, 12),	/* AVB4_TXCREFCLK */
		[13] = RCAR_GP_PIN(8, 13),	/* AVB4_MDIO */
		[14] = RCAR_GP_PIN(8, 14),	/* AVB4_MDC */
		[15] = RCAR_GP_PIN(8, 15),	/* AVB4_MAGIC */
		[16] = RCAR_GP_PIN(8, 16),	/* AVB4_PHY_INT */
		[17] = RCAR_GP_PIN(8, 17),	/* AVB4_LINK */
		[18] = RCAR_GP_PIN(8, 18),	/* AVB4_AVTP_MATCH */
		[19] = RCAR_GP_PIN(8, 19),	/* AVB4_AVTP_CAPTURE */
		[20] = RCAR_GP_PIN(8, 20),	/* AVB4_AVTP_PPS */
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
	{ PINMUX_BIAS_REG("PUEN9", 0xe60698c0, "PUD9", 0xe60698e0) {
		[ 0] = RCAR_GP_PIN(9,  0),	/* AVB5_RX_CTL */
		[ 1] = RCAR_GP_PIN(9,  1),	/* AVB5_RXC */
		[ 2] = RCAR_GP_PIN(9,  2),	/* AVB5_RD0 */
		[ 3] = RCAR_GP_PIN(9,  3),	/* AVB5_RD1 */
		[ 4] = RCAR_GP_PIN(9,  4),	/* AVB5_RD2 */
		[ 5] = RCAR_GP_PIN(9,  5),	/* AVB5_RD3 */
		[ 6] = RCAR_GP_PIN(9,  6),	/* AVB5_TX_CTL */
		[ 7] = RCAR_GP_PIN(9,  7),	/* AVB5_TXC */
		[ 8] = RCAR_GP_PIN(9,  8),	/* AVB5_TD0 */
		[ 9] = RCAR_GP_PIN(9,  9),	/* AVB5_TD1 */
		[10] = RCAR_GP_PIN(9, 10),	/* AVB5_TD2 */
		[11] = RCAR_GP_PIN(9, 11),	/* AVB5_TD3 */
		[12] = RCAR_GP_PIN(9, 12),	/* AVB5_TXCREFCLK */
		[13] = RCAR_GP_PIN(9, 13),	/* AVB5_MDIO */
		[14] = RCAR_GP_PIN(9, 14),	/* AVB5_MDC */
		[15] = RCAR_GP_PIN(9, 15),	/* AVB5_MAGIC */
		[16] = RCAR_GP_PIN(9, 16),	/* AVB5_PHY_INT */
		[17] = RCAR_GP_PIN(9, 17),	/* AVB5_LINK */
		[18] = RCAR_GP_PIN(9, 18),	/* AVB5_AVTP_MATCH */
		[19] = RCAR_GP_PIN(9, 19),	/* AVB5_AVTP_CAPTURE */
		[20] = RCAR_GP_PIN(9, 20),	/* AVB5_AVTP_PPS */
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

static const struct sh_pfc_soc_operations pinmux_ops = {
	.pin_to_pocctrl = r8a779a0_pin_to_pocctrl,
	.get_bias = rcar_pinmux_get_bias,
	.set_bias = rcar_pinmux_set_bias,
};

const struct sh_pfc_soc_info r8a779a0_pinmux_info = {
	.name = "r8a779a0_pfc",
	.ops = &pinmux_ops,
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
