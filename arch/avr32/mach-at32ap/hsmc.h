/*
 * Register definitions for Atmel Static Memory Controller (SMC)
 *
 * Copyright (C) 2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_HSMC_H__
#define __ASM_AVR32_HSMC_H__

/* HSMC register offsets */
#define HSMC_SETUP0				0x0000
#define HSMC_PULSE0				0x0004
#define HSMC_CYCLE0				0x0008
#define HSMC_MODE0				0x000c
#define HSMC_SETUP1				0x0010
#define HSMC_PULSE1				0x0014
#define HSMC_CYCLE1				0x0018
#define HSMC_MODE1				0x001c
#define HSMC_SETUP2				0x0020
#define HSMC_PULSE2				0x0024
#define HSMC_CYCLE2				0x0028
#define HSMC_MODE2				0x002c
#define HSMC_SETUP3				0x0030
#define HSMC_PULSE3				0x0034
#define HSMC_CYCLE3				0x0038
#define HSMC_MODE3				0x003c
#define HSMC_SETUP4				0x0040
#define HSMC_PULSE4				0x0044
#define HSMC_CYCLE4				0x0048
#define HSMC_MODE4				0x004c
#define HSMC_SETUP5				0x0050
#define HSMC_PULSE5				0x0054
#define HSMC_CYCLE5				0x0058
#define HSMC_MODE5				0x005c

/* Bitfields in SETUP0 */
#define HSMC_NWE_SETUP_OFFSET			0
#define HSMC_NWE_SETUP_SIZE			6
#define HSMC_NCS_WR_SETUP_OFFSET		8
#define HSMC_NCS_WR_SETUP_SIZE			6
#define HSMC_NRD_SETUP_OFFSET			16
#define HSMC_NRD_SETUP_SIZE			6
#define HSMC_NCS_RD_SETUP_OFFSET		24
#define HSMC_NCS_RD_SETUP_SIZE			6

/* Bitfields in PULSE0 */
#define HSMC_NWE_PULSE_OFFSET			0
#define HSMC_NWE_PULSE_SIZE			7
#define HSMC_NCS_WR_PULSE_OFFSET		8
#define HSMC_NCS_WR_PULSE_SIZE			7
#define HSMC_NRD_PULSE_OFFSET			16
#define HSMC_NRD_PULSE_SIZE			7
#define HSMC_NCS_RD_PULSE_OFFSET		24
#define HSMC_NCS_RD_PULSE_SIZE			7

/* Bitfields in CYCLE0 */
#define HSMC_NWE_CYCLE_OFFSET			0
#define HSMC_NWE_CYCLE_SIZE			9
#define HSMC_NRD_CYCLE_OFFSET			16
#define HSMC_NRD_CYCLE_SIZE			9

/* Bitfields in MODE0 */
#define HSMC_READ_MODE_OFFSET			0
#define HSMC_READ_MODE_SIZE			1
#define HSMC_WRITE_MODE_OFFSET			1
#define HSMC_WRITE_MODE_SIZE			1
#define HSMC_EXNW_MODE_OFFSET			4
#define HSMC_EXNW_MODE_SIZE			2
#define HSMC_BAT_OFFSET				8
#define HSMC_BAT_SIZE				1
#define HSMC_DBW_OFFSET				12
#define HSMC_DBW_SIZE				2
#define HSMC_TDF_CYCLES_OFFSET			16
#define HSMC_TDF_CYCLES_SIZE			4
#define HSMC_TDF_MODE_OFFSET			20
#define HSMC_TDF_MODE_SIZE			1
#define HSMC_PMEN_OFFSET			24
#define HSMC_PMEN_SIZE				1
#define HSMC_PS_OFFSET				28
#define HSMC_PS_SIZE				2

/* Constants for READ_MODE */
#define HSMC_READ_MODE_NCS_CONTROLLED		0
#define HSMC_READ_MODE_NRD_CONTROLLED		1

/* Constants for WRITE_MODE */
#define HSMC_WRITE_MODE_NCS_CONTROLLED		0
#define HSMC_WRITE_MODE_NWE_CONTROLLED		1

/* Constants for EXNW_MODE */
#define HSMC_EXNW_MODE_DISABLED			0
#define HSMC_EXNW_MODE_RESERVED			1
#define HSMC_EXNW_MODE_FROZEN			2
#define HSMC_EXNW_MODE_READY			3

/* Constants for BAT */
#define HSMC_BAT_BYTE_SELECT			0
#define HSMC_BAT_BYTE_WRITE			1

/* Constants for DBW */
#define HSMC_DBW_8_BITS				0
#define HSMC_DBW_16_BITS			1
#define HSMC_DBW_32_BITS			2

/* Bit manipulation macros */
#define HSMC_BIT(name)							\
	(1 << HSMC_##name##_OFFSET)
#define HSMC_BF(name,value)						\
	(((value) & ((1 << HSMC_##name##_SIZE) - 1))			\
	 << HSMC_##name##_OFFSET)
#define HSMC_BFEXT(name,value)						\
	(((value) >> HSMC_##name##_OFFSET)				\
	 & ((1 << HSMC_##name##_SIZE) - 1))
#define HSMC_BFINS(name,value,old)					\
	(((old) & ~(((1 << HSMC_##name##_SIZE) - 1)			\
		    << HSMC_##name##_OFFSET)) | HSMC_BF(name,value))

/* Register access macros */
#define hsmc_readl(port,reg)						\
	__raw_readl((port)->regs + HSMC_##reg)
#define hsmc_writel(port,reg,value)					\
	__raw_writel((value), (port)->regs + HSMC_##reg)

#endif /* __ASM_AVR32_HSMC_H__ */
