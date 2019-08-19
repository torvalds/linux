/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Joshua Henderson <joshua.henderson@microchip.com>
 * Copyright (C) 2015 Microchip Technology Inc.  All rights reserved.
 */
#ifndef _ASM_MACH_PIC32_H
#define _ASM_MACH_PIC32_H

#include <linux/io.h>

/*
 * PIC32 register offsets for SET/CLR/INV where supported.
 */
#define PIC32_CLR(_reg)		((_reg) + 0x04)
#define PIC32_SET(_reg)		((_reg) + 0x08)
#define PIC32_INV(_reg)		((_reg) + 0x0C)

/*
 * PIC32 Base Register Offsets
 */
#define PIC32_BASE_CONFIG	0x1f800000
#define PIC32_BASE_OSC		0x1f801200
#define PIC32_BASE_RESET	0x1f801240
#define PIC32_BASE_PPS		0x1f801400
#define PIC32_BASE_UART		0x1f822000
#define PIC32_BASE_PORT		0x1f860000
#define PIC32_BASE_DEVCFG2	0x1fc4ff44

/*
 * Register unlock sequence required for some register access.
 */
void pic32_syskey_unlock_debug(const char *fn, const ulong ln);
#define pic32_syskey_unlock()	\
	pic32_syskey_unlock_debug(__func__, __LINE__)

#endif /* _ASM_MACH_PIC32_H */
