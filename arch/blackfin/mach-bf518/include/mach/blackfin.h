/*
 * Copyright 2008-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#ifndef _MACH_BLACKFIN_H_
#define _MACH_BLACKFIN_H_

#include "bf518.h"
#include "defBF512.h"
#include "anomaly.h"

#if defined(CONFIG_BF518)
#include "defBF518.h"
#endif

#if defined(CONFIG_BF516)
#include "defBF516.h"
#endif

#if defined(CONFIG_BF514)
#include "defBF514.h"
#endif

#if defined(CONFIG_BF512)
#include "defBF512.h"
#endif

#if !defined(__ASSEMBLY__)
#include "cdefBF512.h"

#if defined(CONFIG_BF518)
#include "cdefBF518.h"
#endif

#if defined(CONFIG_BF516)
#include "cdefBF516.h"
#endif

#if defined(CONFIG_BF514)
#include "cdefBF514.h"
#endif
#endif

#define BFIN_UART_NR_PORTS	2

#define OFFSET_THR              0x00	/* Transmit Holding register            */
#define OFFSET_RBR              0x00	/* Receive Buffer register              */
#define OFFSET_DLL              0x00	/* Divisor Latch (Low-Byte)             */
#define OFFSET_IER              0x04	/* Interrupt Enable Register            */
#define OFFSET_DLH              0x04	/* Divisor Latch (High-Byte)            */
#define OFFSET_IIR              0x08	/* Interrupt Identification Register    */
#define OFFSET_LCR              0x0C	/* Line Control Register                */
#define OFFSET_MCR              0x10	/* Modem Control Register               */
#define OFFSET_LSR              0x14	/* Line Status Register                 */
#define OFFSET_MSR              0x18	/* Modem Status Register                */
#define OFFSET_SCR              0x1C	/* SCR Scratch Register                 */
#define OFFSET_GCTL             0x24	/* Global Control Register              */

/* PLL_DIV Masks													*/
#define CCLK_DIV1 CSEL_DIV1	/*          CCLK = VCO / 1                                  */
#define CCLK_DIV2 CSEL_DIV2	/*          CCLK = VCO / 2                                  */
#define CCLK_DIV4 CSEL_DIV4	/*          CCLK = VCO / 4                                  */
#define CCLK_DIV8 CSEL_DIV8	/*          CCLK = VCO / 8                                  */

#endif
