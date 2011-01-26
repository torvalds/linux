/*
 * SBE 2T3E3 synchronous serial card driver for Linux
 *
 * Copyright (C) 2009-2010 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * This code is based on a driver written by SBE Inc.
 */

#include <linux/kernel.h>
#include "2t3e3.h"

const u32 cpld_reg_map[][2] =
{
	{ 0x0000, 0x0080 }, /* 0 - Port Control Register A (PCRA) */
	{ 0x0004, 0x0084 }, /* 1 - Port Control Register B (PCRB) */
	{ 0x0008, 0x0088 }, /* 2 - LCV Count Register (PLCR) */
	{ 0x000c, 0x008c }, /* 3 - LCV Threshold register (PLTR) */
	{ 0x0010, 0x0090 }, /* 4 - Payload Fill Register (PPFR) */
	{ 0x0200, 0x0200 }, /* 5 - Board ID / FPGA Programming Status Register */
	{ 0x0204, 0x0204 }, /* 6 - FPGA Version Register */
	{ 0x0800, 0x1000 }, /* 7 - Framer Registers Base Address */
	{ 0x2000, 0x2000 }, /* 8 - Serial Chip Select Register */
	{ 0x2004, 0x2004 }, /* 9 - Static Reset Register */
	{ 0x2008, 0x2008 }, /* 10 - Pulse Reset Register */
	{ 0x200c, 0x200c }, /* 11 - FPGA Reconfiguration Register */
	{ 0x2010, 0x2014 }, /* 12 - LED Register (LEDR) */
	{ 0x2018, 0x201c }, /* 13 - LIU Control and Status Register (PISCR) */
	{ 0x2020, 0x2024 }, /* 14 - Interrupt Enable Register (PIER) */
	{ 0x0068, 0x00e8 }, /* 15 - Port Control Register C (PCRC) */
	{ 0x006c, 0x00ec }, /* 16 - Port Bandwidth Start (PBWF) */
	{ 0x0070, 0x00f0 }, /* 17 - Port Bandwidth Stop (PBWL) */
};

const u32 cpld_val_map[][2] =
{
	{ 0x01, 0x02 }, /* LIU1 / LIU2 select for Serial Chip Select */
	{ 0x04, 0x08 }, /* DAC1 / DAC2 select for Serial Chip Select */
	{ 0x00, 0x04 }, /* LOOP1 / LOOP2 - select of loop timing source */
	{ 0x01, 0x02 }  /* PORT1 / PORT2 - select LIU and Framer for reset */
};

const u32 t3e3_framer_reg_map[] = {
	0x00, /* 0 - OPERATING_MODE */
	0x01, /* 1 - IO_CONTROL */
	0x04, /* 2 - BLOCK_INTERRUPT_ENABLE */
	0x05, /* 3 - BLOCK_INTERRUPT_STATUS */
	0x10, /* 4 - T3_RX_CONFIGURATION_STATUS, E3_RX_CONFIGURATION_STATUS_1 */
	0x11, /* 5 - T3_RX_STATUS, E3_RX_CONFIGURATION_STATUS_2 */
	0x12, /* 6 - T3_RX_INTERRUPT_ENABLE, E3_RX_INTERRUPT_ENABLE_1 */
	0x13, /* 7 - T3_RX_INTERRUPT_STATUS, E3_RX_INTERRUPT_ENABLE_2 */
	0x14, /* 8 - T3_RX_SYNC_DETECT_ENABLE, E3_RX_INTERRUPT_STATUS_1 */
	0x15, /* 9 - E3_RX_INTERRUPT_STATUS_2 */
	0x16, /* 10 - T3_RX_FEAC */
	0x17, /* 11 - T3_RX_FEAC_INTERRUPT_ENABLE_STATUS */
	0x18, /* 12 - T3_RX_LAPD_CONTROL, E3_RX_LAPD_CONTROL */
	0x19, /* 13 - T3_RX_LAPD_STATUS, E3_RX_LAPD_STATUS */
	0x1a, /* 14 - E3_RX_NR_BYTE, E3_RX_SERVICE_BITS */
	0x1b, /* 15 - E3_RX_GC_BYTE */
	0x30, /* 16 - T3_TX_CONFIGURATION, E3_TX_CONFIGURATION */
	0x31, /* 17 - T3_TX_FEAC_CONFIGURATION_STATUS */
	0x32, /* 18 - T3_TX_FEAC */
	0x33, /* 19 - T3_TX_LAPD_CONFIGURATION, E3_TX_LAPD_CONFIGURATION */
	0x34, /* 20 - T3_TX_LAPD_STATUS, E3_TX_LAPD_STATUS_INTERRUPT */
	0x35, /* 21 - T3_TX_MBIT_MASK, E3_TX_GC_BYTE, E3_TX_SERVICE_BITS */
	0x36, /* 22 - T3_TX_FBIT_MASK, E3_TX_MA_BYTE */
	0x37, /* 23 - T3_TX_FBIT_MASK_2, E3_TX_NR_BYTE */
	0x38, /* 24 - T3_TX_FBIT_MASK_3 */
	0x48, /* 25 - E3_TX_FA1_ERROR_MASK, E3_TX_FAS_ERROR_MASK_UPPER */
	0x49, /* 26 - E3_TX_FA2_ERROR_MASK, E3_TX_FAS_ERROR_MASK_LOWER */
	0x4a, /* 27 - E3_TX_BIP8_MASK, E3_TX_BIP4_MASK */
	0x50, /* 28 - PMON_LCV_EVENT_COUNT_MSB */
	0x51, /* 29 - PMON_LCV_EVENT_COUNT_LSB */
	0x52, /* 30 - PMON_FRAMING_BIT_ERROR_EVENT_COUNT_MSB */
	0x53, /* 31 - PMON_FRAMING_BIT_ERROR_EVENT_COUNT_LSB */
	0x54, /* 32 - PMON_PARITY_ERROR_EVENT_COUNT_MSB */
	0x55, /* 33 - PMON_PARITY_ERROR_EVENT_COUNT_LSB */
	0x56, /* 34 - PMON_FEBE_EVENT_COUNT_MSB */
	0x57, /* 35 - PMON_FEBE_EVENT_COUNT_LSB */
	0x58, /* 36 - PMON_CP_BIT_ERROR_EVENT_COUNT_MSB */
	0x59, /* 37 - PMON_CP_BIT_ERROR_EVENT_COUNT_LSB */
	0x6c, /* 38 - PMON_HOLDING_REGISTER */
	0x6d, /* 39 - ONE_SECOND_ERROR_STATUS */
	0x6e, /* 40 - LCV_ONE_SECOND_ACCUMULATOR_MSB */
	0x6f, /* 41 - LCV_ONE_SECOND_ACCUMULATOR_LSB */
	0x70, /* 42 - FRAME_PARITY_ERROR_ONE_SECOND_ACCUMULATOR_MSB */
	0x71, /* 43 - FRAME_PARITY_ERROR_ONE_SECOND_ACCUMULATOR_LSB */
	0x72, /* 44 - FRAME_CP_BIT_ERROR_ONE_SECOND_ACCUMULATOR_MSB */
	0x73, /* 45 - FRAME_CP_BIT_ERROR_ONE_SECOND_ACCUMULATOR_LSB */
	0x80, /* 46 - LINE_INTERFACE_DRIVE */
	0x81  /* 47 - LINE_INTERFACE_SCAN */
};

const u32 t3e3_liu_reg_map[] =
{
	0x00, /* REG0 */
	0x01, /* REG1 */
	0x02, /* REG2 */
	0x03, /* REG3 */
	0x04 /* REG4 */
};
