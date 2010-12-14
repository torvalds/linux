/*
 * linux/arch/arm/mach-tegra/pinmux-t2-tables.c
 *
 * Common pinmux configurations for Tegra 2 SoCs
 *
 * Copyright (C) 2010 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/string.h>

#include <mach/iomap.h>
#include <mach/pinmux.h>

#define DRIVE_PINGROUP(pg_name, r)				\
	[TEGRA_DRIVE_PINGROUP_ ## pg_name] = {			\
		.name = #pg_name,				\
		.reg = r					\
	}

const struct tegra_drive_pingroup_desc tegra_soc_drive_pingroups[TEGRA_MAX_DRIVE_PINGROUP] = {
	DRIVE_PINGROUP(AO1,		0x868),
	DRIVE_PINGROUP(AO2,		0x86c),
	DRIVE_PINGROUP(AT1,		0x870),
	DRIVE_PINGROUP(AT2,		0x874),
	DRIVE_PINGROUP(CDEV1,		0x878),
	DRIVE_PINGROUP(CDEV2,		0x87c),
	DRIVE_PINGROUP(CSUS,		0x880),
	DRIVE_PINGROUP(DAP1,		0x884),
	DRIVE_PINGROUP(DAP2,		0x888),
	DRIVE_PINGROUP(DAP3,		0x88c),
	DRIVE_PINGROUP(DAP4,		0x890),
	DRIVE_PINGROUP(DBG,		0x894),
	DRIVE_PINGROUP(LCD1,		0x898),
	DRIVE_PINGROUP(LCD2,		0x89c),
	DRIVE_PINGROUP(SDMMC2,		0x8a0),
	DRIVE_PINGROUP(SDMMC3,		0x8a4),
	DRIVE_PINGROUP(SPI,		0x8a8),
	DRIVE_PINGROUP(UAA,		0x8ac),
	DRIVE_PINGROUP(UAB,		0x8b0),
	DRIVE_PINGROUP(UART2,		0x8b4),
	DRIVE_PINGROUP(UART3,		0x8b8),
	DRIVE_PINGROUP(VI1,		0x8bc),
	DRIVE_PINGROUP(VI2,		0x8c0),
	DRIVE_PINGROUP(XM2A,		0x8c4),
	DRIVE_PINGROUP(XM2C,		0x8c8),
	DRIVE_PINGROUP(XM2D,		0x8cc),
	DRIVE_PINGROUP(XM2CLK,		0x8d0),
	DRIVE_PINGROUP(MEMCOMP,		0x8d4),
};

#define PINGROUP(pg_name, vdd, f0, f1, f2, f3, f_safe,		\
		 tri_r, tri_b, mux_r, mux_b, pupd_r, pupd_b)	\
	[TEGRA_PINGROUP_ ## pg_name] = {			\
		.name = #pg_name,				\
		.vddio = TEGRA_VDDIO_ ## vdd,			\
		.funcs = {					\
			TEGRA_MUX_ ## f0,			\
			TEGRA_MUX_ ## f1,			\
			TEGRA_MUX_ ## f2,			\
			TEGRA_MUX_ ## f3,			\
		},						\
		.func_safe = TEGRA_MUX_ ## f_safe,		\
		.tri_reg = tri_r,				\
		.tri_bit = tri_b,				\
		.mux_reg = mux_r,				\
		.mux_bit = mux_b,				\
		.pupd_reg = pupd_r,				\
		.pupd_bit = pupd_b,				\
	}

const struct tegra_pingroup_desc tegra_soc_pingroups[TEGRA_MAX_PINGROUP] = {
	PINGROUP(ATA,   NAND,  IDE,       NAND,      GMI,       RSVD,          IDE,       0x14, 0,  0x80, 24, 0xA0, 0),
	PINGROUP(ATB,   NAND,  IDE,       NAND,      GMI,       SDIO4,         IDE,       0x14, 1,  0x80, 16, 0xA0, 2),
	PINGROUP(ATC,   NAND,  IDE,       NAND,      GMI,       SDIO4,         IDE,       0x14, 2,  0x80, 22, 0xA0, 4),
	PINGROUP(ATD,   NAND,  IDE,       NAND,      GMI,       SDIO4,         IDE,       0x14, 3,  0x80, 20, 0xA0, 6),
	PINGROUP(ATE,   NAND,  IDE,       NAND,      GMI,       RSVD,          IDE,       0x18, 25, 0x80, 12, 0xA0, 8),
	PINGROUP(CDEV1, AUDIO, OSC,       PLLA_OUT,  PLLM_OUT1, AUDIO_SYNC,    OSC,       0x14, 4,  0x88, 2,  0xA8, 0),
	PINGROUP(CDEV2, AUDIO, OSC,       AHB_CLK,   APB_CLK,   PLLP_OUT4,     OSC,       0x14, 5,  0x88, 4,  0xA8, 2),
	PINGROUP(CRTP,  LCD,   CRT,       RSVD,      RSVD,      RSVD,          RSVD,      0x20, 14, 0x98, 20, 0xA4, 24),
	PINGROUP(CSUS,  VI,    PLLC_OUT1, PLLP_OUT2, PLLP_OUT3, VI_SENSOR_CLK, PLLC_OUT1, 0x14, 6,  0x88, 6,  0xAC, 24),
	PINGROUP(DAP1,  AUDIO, DAP1,      RSVD,      GMI,       SDIO2,         DAP1,      0x14, 7,  0x88, 20, 0xA0, 10),
	PINGROUP(DAP2,  AUDIO, DAP2,      TWC,       RSVD,      GMI,           DAP2,      0x14, 8,  0x88, 22, 0xA0, 12),
	PINGROUP(DAP3,  BB,    DAP3,      RSVD,      RSVD,      RSVD,          DAP3,      0x14, 9,  0x88, 24, 0xA0, 14),
	PINGROUP(DAP4,  UART,  DAP4,      RSVD,      GMI,       RSVD,          DAP4,      0x14, 10, 0x88, 26, 0xA0, 16),
	PINGROUP(DDC,   LCD,   I2C2,      RSVD,      RSVD,      RSVD,          RSVD4,     0x18, 31, 0x88, 0,  0xB0, 28),
	PINGROUP(DTA,   VI,    RSVD,      SDIO2,     VI,        RSVD,          RSVD4,     0x14, 11, 0x84, 20, 0xA0, 18),
	PINGROUP(DTB,   VI,    RSVD,      RSVD,      VI,        SPI1,          RSVD1,     0x14, 12, 0x84, 22, 0xA0, 20),
	PINGROUP(DTC,   VI,    RSVD,      RSVD,      VI,        RSVD,          RSVD1,     0x14, 13, 0x84, 26, 0xA0, 22),
	PINGROUP(DTD,   VI,    RSVD,      SDIO2,     VI,        RSVD,          RSVD1,     0x14, 14, 0x84, 28, 0xA0, 24),
	PINGROUP(DTE,   VI,    RSVD,      RSVD,      VI,        SPI1,          RSVD1,     0x14, 15, 0x84, 30, 0xA0, 26),
	PINGROUP(DTF,   VI,    I2C3,      RSVD,      VI,        RSVD,          RSVD4,     0x20, 12, 0x98, 30, 0xA0, 28),
	PINGROUP(GMA,   NAND,  UARTE,     SPI3,      GMI,       SDIO4,         SPI3,      0x14, 28, 0x84, 0,  0xB0, 20),
	PINGROUP(GMB,   NAND,  IDE,       NAND,      GMI,       GMI_INT,       GMI,       0x18, 29, 0x88, 28, 0xB0, 22),
	PINGROUP(GMC,   NAND,  UARTD,     SPI4,      GMI,       SFLASH,        SPI4,      0x14, 29, 0x84, 2,  0xB0, 24),
	PINGROUP(GMD,   NAND,  RSVD,      NAND,      GMI,       SFLASH,        GMI,       0x18, 30, 0x88, 30, 0xB0, 26),
	PINGROUP(GME,   NAND,  RSVD,      DAP5,      GMI,       SDIO4,         GMI,       0x18, 0,  0x8C, 0,  0xA8, 24),
	PINGROUP(GPU,   UART,  PWM,       UARTA,     GMI,       RSVD,          RSVD4,     0x14, 16, 0x8C, 4,  0xA4, 20),
	PINGROUP(GPU7,  SYS,   RTCK,      RSVD,      RSVD,      RSVD,          RTCK,      0x20, 11, 0x98, 28, 0xA4, 6),
	PINGROUP(GPV,   SD,    PCIE,      RSVD,      RSVD,      RSVD,          PCIE,      0x14, 17, 0x8C, 2,  0xA0, 30),
	PINGROUP(HDINT, LCD,   HDMI,      RSVD,      RSVD,      RSVD,          HDMI,      0x1C, 23, 0x84, 4,  0xAC, 22),
	PINGROUP(I2CP,  SYS,   I2C,       RSVD,      RSVD,      RSVD,          RSVD4,     0x14, 18, 0x88, 8,  0xA4, 2),
	PINGROUP(IRRX,  UART,  UARTA,     UARTB,     GMI,       SPI4,          UARTB,     0x14, 20, 0x88, 18, 0xA8, 22),
	PINGROUP(IRTX,  UART,  UARTA,     UARTB,     GMI,       SPI4,          UARTB,     0x14, 19, 0x88, 16, 0xA8, 20),
	PINGROUP(KBCA,  SYS,   KBC,       NAND,      SDIO2,     EMC_TEST0_DLL, KBC,       0x14, 22, 0x88, 10, 0xA4, 8),
	PINGROUP(KBCB,  SYS,   KBC,       NAND,      SDIO2,     MIO,           KBC,       0x14, 21, 0x88, 12, 0xA4, 10),
	PINGROUP(KBCC,  SYS,   KBC,       NAND,      TRACE,     EMC_TEST1_DLL, KBC,       0x18, 26, 0x88, 14, 0xA4, 12),
	PINGROUP(KBCD,  SYS,   KBC,       NAND,      SDIO2,     MIO,           KBC,       0x20, 10, 0x98, 26, 0xA4, 14),
	PINGROUP(KBCE,  SYS,   KBC,       NAND,      OWR,       RSVD,          KBC,       0x14, 26, 0x80, 28, 0xB0, 2),
	PINGROUP(KBCF,  SYS,   KBC,       NAND,      TRACE,     MIO,           KBC,       0x14, 27, 0x80, 26, 0xB0, 0),
	PINGROUP(LCSN,  LCD,   DISPLAYA,  DISPLAYB,  SPI3,      RSVD,          RSVD4,     0x1C, 31, 0x90, 12, 0xAC, 20),
	PINGROUP(LD0,   LCD,   DISPLAYA,  DISPLAYB,  XIO,       RSVD,          RSVD4,     0x1C, 0,  0x94, 0,  0xAC, 12),
	PINGROUP(LD1,   LCD,   DISPLAYA,  DISPLAYB,  XIO,       RSVD,          RSVD4,     0x1C, 1,  0x94, 2,  0xAC, 12),
	PINGROUP(LD10,  LCD,   DISPLAYA,  DISPLAYB,  XIO,       RSVD,          RSVD4,     0x1C, 10, 0x94, 20, 0xAC, 12),
	PINGROUP(LD11,  LCD,   DISPLAYA,  DISPLAYB,  XIO,       RSVD,          RSVD4,     0x1C, 11, 0x94, 22, 0xAC, 12),
	PINGROUP(LD12,  LCD,   DISPLAYA,  DISPLAYB,  XIO,       RSVD,          RSVD4,     0x1C, 12, 0x94, 24, 0xAC, 12),
	PINGROUP(LD13,  LCD,   DISPLAYA,  DISPLAYB,  XIO,       RSVD,          RSVD4,     0x1C, 13, 0x94, 26, 0xAC, 12),
	PINGROUP(LD14,  LCD,   DISPLAYA,  DISPLAYB,  XIO,       RSVD,          RSVD4,     0x1C, 14, 0x94, 28, 0xAC, 12),
	PINGROUP(LD15,  LCD,   DISPLAYA,  DISPLAYB,  XIO,       RSVD,          RSVD4,     0x1C, 15, 0x94, 30, 0xAC, 12),
	PINGROUP(LD16,  LCD,   DISPLAYA,  DISPLAYB,  XIO,       RSVD,          RSVD4,     0x1C, 16, 0x98, 0,  0xAC, 12),
	PINGROUP(LD17,  LCD,   DISPLAYA,  DISPLAYB,  RSVD,      RSVD,          RSVD4,     0x1C, 17, 0x98, 2,  0xAC, 12),
	PINGROUP(LD2,   LCD,   DISPLAYA,  DISPLAYB,  XIO,       RSVD,          RSVD4,     0x1C, 2,  0x94, 4,  0xAC, 12),
	PINGROUP(LD3,   LCD,   DISPLAYA,  DISPLAYB,  XIO,       RSVD,          RSVD4,     0x1C, 3,  0x94, 6,  0xAC, 12),
	PINGROUP(LD4,   LCD,   DISPLAYA,  DISPLAYB,  XIO,       RSVD,          RSVD4,     0x1C, 4,  0x94, 8,  0xAC, 12),
	PINGROUP(LD5,   LCD,   DISPLAYA,  DISPLAYB,  XIO,       RSVD,          RSVD4,     0x1C, 5,  0x94, 10, 0xAC, 12),
	PINGROUP(LD6,   LCD,   DISPLAYA,  DISPLAYB,  XIO,       RSVD,          RSVD4,     0x1C, 6,  0x94, 12, 0xAC, 12),
	PINGROUP(LD7,   LCD,   DISPLAYA,  DISPLAYB,  XIO,       RSVD,          RSVD4,     0x1C, 7,  0x94, 14, 0xAC, 12),
	PINGROUP(LD8,   LCD,   DISPLAYA,  DISPLAYB,  XIO,       RSVD,          RSVD4,     0x1C, 8,  0x94, 16, 0xAC, 12),
	PINGROUP(LD9,   LCD,   DISPLAYA,  DISPLAYB,  XIO,       RSVD,          RSVD4,     0x1C, 9,  0x94, 18, 0xAC, 12),
	PINGROUP(LDC,   LCD,   DISPLAYA,  DISPLAYB,  RSVD,      RSVD,          RSVD4,     0x1C, 30, 0x90, 14, 0xAC, 20),
	PINGROUP(LDI,   LCD,   DISPLAYA,  DISPLAYB,  RSVD,      RSVD,          RSVD4,     0x20, 6,  0x98, 16, 0xAC, 18),
	PINGROUP(LHP0,  LCD,   DISPLAYA,  DISPLAYB,  RSVD,      RSVD,          RSVD4,     0x1C, 18, 0x98, 10, 0xAC, 16),
	PINGROUP(LHP1,  LCD,   DISPLAYA,  DISPLAYB,  RSVD,      RSVD,          RSVD4,     0x1C, 19, 0x98, 4,  0xAC, 14),
	PINGROUP(LHP2,  LCD,   DISPLAYA,  DISPLAYB,  RSVD,      RSVD,          RSVD4,     0x1C, 20, 0x98, 6,  0xAC, 14),
	PINGROUP(LHS,   LCD,   DISPLAYA,  DISPLAYB,  XIO,       RSVD,          RSVD4,     0x20, 7,  0x90, 22, 0xAC, 22),
	PINGROUP(LM0,   LCD,   DISPLAYA,  DISPLAYB,  SPI3,      RSVD,          RSVD4,     0x1C, 24, 0x90, 26, 0xAC, 22),
	PINGROUP(LM1,   LCD,   DISPLAYA,  DISPLAYB,  RSVD,      CRT,           RSVD3,     0x1C, 25, 0x90, 28, 0xAC, 22),
	PINGROUP(LPP,   LCD,   DISPLAYA,  DISPLAYB,  RSVD,      RSVD,          RSVD4,     0x20, 8,  0x98, 14, 0xAC, 18),
	PINGROUP(LPW0,  LCD,   DISPLAYA,  DISPLAYB,  SPI3,      HDMI,          DISPLAYA,  0x20, 3,  0x90, 0,  0xAC, 20),
	PINGROUP(LPW1,  LCD,   DISPLAYA,  DISPLAYB,  RSVD,      RSVD,          RSVD4,     0x20, 4,  0x90, 2,  0xAC, 20),
	PINGROUP(LPW2,  LCD,   DISPLAYA,  DISPLAYB,  SPI3,      HDMI,          DISPLAYA,  0x20, 5,  0x90, 4,  0xAC, 20),
	PINGROUP(LSC0,  LCD,   DISPLAYA,  DISPLAYB,  XIO,       RSVD,          RSVD4,     0x1C, 27, 0x90, 18, 0xAC, 22),
	PINGROUP(LSC1,  LCD,   DISPLAYA,  DISPLAYB,  SPI3,      HDMI,          DISPLAYA,  0x1C, 28, 0x90, 20, 0xAC, 20),
	PINGROUP(LSCK,  LCD,   DISPLAYA,  DISPLAYB,  SPI3,      HDMI,          DISPLAYA,  0x1C, 29, 0x90, 16, 0xAC, 20),
	PINGROUP(LSDA,  LCD,   DISPLAYA,  DISPLAYB,  SPI3,      HDMI,          DISPLAYA,  0x20, 1,  0x90, 8,  0xAC, 20),
	PINGROUP(LSDI,  LCD,   DISPLAYA,  DISPLAYB,  SPI3,      RSVD,          DISPLAYA,  0x20, 2,  0x90, 6,  0xAC, 20),
	PINGROUP(LSPI,  LCD,   DISPLAYA,  DISPLAYB,  XIO,       HDMI,          DISPLAYA,  0x20, 0,  0x90, 10, 0xAC, 22),
	PINGROUP(LVP0,  LCD,   DISPLAYA,  DISPLAYB,  RSVD,      RSVD,          RSVD4,     0x1C, 21, 0x90, 30, 0xAC, 22),
	PINGROUP(LVP1,  LCD,   DISPLAYA,  DISPLAYB,  RSVD,      RSVD,          RSVD4,     0x1C, 22, 0x98, 8,  0xAC, 16),
	PINGROUP(LVS,   LCD,   DISPLAYA,  DISPLAYB,  XIO,       RSVD,          RSVD4,     0x1C, 26, 0x90, 24, 0xAC, 22),
	PINGROUP(OWC,   SYS,   OWR,       RSVD,      RSVD,      RSVD,          OWR,       0x14, 31, 0x84, 8,  0xB0, 30),
	PINGROUP(PMC,   SYS,   PWR_ON,    PWR_INTR,  RSVD,      RSVD,          PWR_ON,    0x14, 23, 0x98, 18, -1,   -1),
	PINGROUP(PTA,   NAND,  I2C2,      HDMI,      GMI,       RSVD,          RSVD4,     0x14, 24, 0x98, 22, 0xA4, 4),
	PINGROUP(RM,    UART,  I2C,       RSVD,      RSVD,      RSVD,          RSVD4,     0x14, 25, 0x80, 14, 0xA4, 0),
	PINGROUP(SDB,   SD,    UARTA,     PWM,       SDIO3,     SPI2,          PWM,       0x20, 15, 0x8C, 10, -1,   -1),
	PINGROUP(SDC,   SD,    PWM,       TWC,       SDIO3,     SPI3,          TWC,       0x18, 1,  0x8C, 12, 0xAC, 28),
	PINGROUP(SDD,   SD,    UARTA,     PWM,       SDIO3,     SPI3,          PWM,       0x18, 2,  0x8C, 14, 0xAC, 30),
	PINGROUP(SDIO1, BB,    SDIO1,     RSVD,      UARTE,     UARTA,         RSVD2,     0x14, 30, 0x80, 30, 0xB0, 18),
	PINGROUP(SLXA,  SD,    PCIE,      SPI4,      SDIO3,     SPI2,          PCIE,      0x18, 3,  0x84, 6,  0xA4, 22),
	PINGROUP(SLXC,  SD,    SPDIF,     SPI4,      SDIO3,     SPI2,          SPI4,      0x18, 5,  0x84, 10, 0xA4, 26),
	PINGROUP(SLXD,  SD,    SPDIF,     SPI4,      SDIO3,     SPI2,          SPI4,      0x18, 6,  0x84, 12, 0xA4, 28),
	PINGROUP(SLXK,  SD,    PCIE,      SPI4,      SDIO3,     SPI2,          PCIE,      0x18, 7,  0x84, 14, 0xA4, 30),
	PINGROUP(SPDI,  AUDIO, SPDIF,     RSVD,      I2C,       SDIO2,         RSVD2,     0x18, 8,  0x8C, 8,  0xA4, 16),
	PINGROUP(SPDO,  AUDIO, SPDIF,     RSVD,      I2C,       SDIO2,         RSVD2,     0x18, 9,  0x8C, 6,  0xA4, 18),
	PINGROUP(SPIA,  AUDIO, SPI1,      SPI2,      SPI3,      GMI,           GMI,       0x18, 10, 0x8C, 30, 0xA8, 4),
	PINGROUP(SPIB,  AUDIO, SPI1,      SPI2,      SPI3,      GMI,           GMI,       0x18, 11, 0x8C, 28, 0xA8, 6),
	PINGROUP(SPIC,  AUDIO, SPI1,      SPI2,      SPI3,      GMI,           GMI,       0x18, 12, 0x8C, 26, 0xA8, 8),
	PINGROUP(SPID,  AUDIO, SPI2,      SPI1,      SPI2_ALT,  GMI,           GMI,       0x18, 13, 0x8C, 24, 0xA8, 10),
	PINGROUP(SPIE,  AUDIO, SPI2,      SPI1,      SPI2_ALT,  GMI,           GMI,       0x18, 14, 0x8C, 22, 0xA8, 12),
	PINGROUP(SPIF,  AUDIO, SPI3,      SPI1,      SPI2,      RSVD,          RSVD4,     0x18, 15, 0x8C, 20, 0xA8, 14),
	PINGROUP(SPIG,  AUDIO, SPI3,      SPI2,      SPI2_ALT,  I2C,           SPI2_ALT,  0x18, 16, 0x8C, 18, 0xA8, 16),
	PINGROUP(SPIH,  AUDIO, SPI3,      SPI2,      SPI2_ALT,  I2C,           SPI2_ALT,  0x18, 17, 0x8C, 16, 0xA8, 18),
	PINGROUP(UAA,   BB,    SPI3,      MIPI_HS,   UARTA,     ULPI,          MIPI_HS,   0x18, 18, 0x80, 0,  0xAC, 0),
	PINGROUP(UAB,   BB,    SPI2,      MIPI_HS,   UARTA,     ULPI,          MIPI_HS,   0x18, 19, 0x80, 2,  0xAC, 2),
	PINGROUP(UAC,   BB,    OWR,       RSVD,      RSVD,      RSVD,          RSVD4,     0x18, 20, 0x80, 4,  0xAC, 4),
	PINGROUP(UAD,   UART,  IRDA,      SPDIF,     UARTA,     SPI4,          SPDIF,     0x18, 21, 0x80, 6,  0xAC, 6),
	PINGROUP(UCA,   UART,  UARTC,     RSVD,      GMI,       RSVD,          RSVD4,     0x18, 22, 0x84, 16, 0xAC, 8),
	PINGROUP(UCB,   UART,  UARTC,     PWM,       GMI,       RSVD,          RSVD4,     0x18, 23, 0x84, 18, 0xAC, 10),
	PINGROUP(UDA,   BB,    SPI1,      RSVD,      UARTD,     ULPI,          RSVD2,     0x20, 13, 0x80, 8,  0xB0, 16),
	/* these pin groups only have pullup and pull down control */
	PINGROUP(CK32,  SYS,   RSVD,      RSVD,      RSVD,      RSVD,          RSVD,      -1,   -1, -1,   -1, 0xB0, 14),
	PINGROUP(DDRC,  DDR,   RSVD,      RSVD,      RSVD,      RSVD,          RSVD,      -1,   -1, -1,   -1, 0xAC, 26),
	PINGROUP(PMCA,  SYS,   RSVD,      RSVD,      RSVD,      RSVD,          RSVD,      -1,   -1, -1,   -1, 0xB0, 4),
	PINGROUP(PMCB,  SYS,   RSVD,      RSVD,      RSVD,      RSVD,          RSVD,      -1,   -1, -1,   -1, 0xB0, 6),
	PINGROUP(PMCC,  SYS,   RSVD,      RSVD,      RSVD,      RSVD,          RSVD,      -1,   -1, -1,   -1, 0xB0, 8),
	PINGROUP(PMCD,  SYS,   RSVD,      RSVD,      RSVD,      RSVD,          RSVD,      -1,   -1, -1,   -1, 0xB0, 10),
	PINGROUP(PMCE,  SYS,   RSVD,      RSVD,      RSVD,      RSVD,          RSVD,      -1,   -1, -1,   -1, 0xB0, 12),
	PINGROUP(XM2C,  DDR,   RSVD,      RSVD,      RSVD,      RSVD,          RSVD,      -1,   -1, -1,   -1, 0xA8, 30),
	PINGROUP(XM2D,  DDR,   RSVD,      RSVD,      RSVD,      RSVD,          RSVD,      -1,   -1, -1,   -1, 0xA8, 28),
};

#ifdef CONFIG_PM
#define TRISTATE_REG_A         0x14
#define TRISTATE_REG_NUM       4
#define PIN_MUX_CTL_REG_A      0x80
#define PIN_MUX_CTL_REG_NUM    8
#define PULLUPDOWN_REG_A       0xa0
#define PULLUPDOWN_REG_NUM     5

static u32 pinmux_reg[TRISTATE_REG_NUM + PIN_MUX_CTL_REG_NUM +
		     PULLUPDOWN_REG_NUM];

static inline unsigned long pg_readl(unsigned long offset)
{
	return readl(IO_TO_VIRT(TEGRA_APB_MISC_BASE + offset));
}

static inline void pg_writel(unsigned long value, unsigned long offset)
{
	writel(value, IO_TO_VIRT(TEGRA_APB_MISC_BASE + offset));
}

void tegra_pinmux_suspend(void)
{
	unsigned int i;
	u32 *ctx = pinmux_reg;

	for (i = 0; i < TRISTATE_REG_NUM; i++)
		*ctx++ = pg_readl(TRISTATE_REG_A + i*4);

	for (i = 0; i < PIN_MUX_CTL_REG_NUM; i++)
		*ctx++ = pg_readl(PIN_MUX_CTL_REG_A + i*4);

	for (i = 0; i < PULLUPDOWN_REG_NUM; i++)
		*ctx++ = pg_readl(PULLUPDOWN_REG_A + i*4);
}

void tegra_pinmux_resume(void)
{
	unsigned int i;
	u32 *ctx = pinmux_reg;

	for (i = 0; i < PIN_MUX_CTL_REG_NUM; i++)
		pg_writel(*ctx++, PIN_MUX_CTL_REG_A + i*4);

	for (i = 0; i < PULLUPDOWN_REG_NUM; i++)
		pg_writel(*ctx++, PULLUPDOWN_REG_A + i*4);

	for (i = 0; i < TRISTATE_REG_NUM; i++)
		pg_writel(*ctx++, TRISTATE_REG_A + i*4);
}
#endif
