/*
 * Copyright 2007-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _MACH_BLACKFIN_H_
#define _MACH_BLACKFIN_H_

#include "bf548.h"
#include "anomaly.h"

#ifdef CONFIG_BF542
#include "defBF542.h"
#endif

#ifdef CONFIG_BF544
#include "defBF544.h"
#endif

#ifdef CONFIG_BF547
#include "defBF547.h"
#endif

#ifdef CONFIG_BF548
#include "defBF548.h"
#endif

#ifdef CONFIG_BF549
#include "defBF549.h"
#endif

#if !defined(__ASSEMBLY__)
#ifdef CONFIG_BF542
#include "cdefBF542.h"
#endif
#ifdef CONFIG_BF544
#include "cdefBF544.h"
#endif
#ifdef CONFIG_BF547
#include "cdefBF547.h"
#endif
#ifdef CONFIG_BF548
#include "cdefBF548.h"
#endif
#ifdef CONFIG_BF549
#include "cdefBF549.h"
#endif

#endif

#define BFIN_UART_NR_PORTS	4

#define OFFSET_DLL              0x00	/* Divisor Latch (Low-Byte)             */
#define OFFSET_DLH              0x04	/* Divisor Latch (High-Byte)            */
#define OFFSET_GCTL             0x08	/* Global Control Register              */
#define OFFSET_LCR              0x0C	/* Line Control Register                */
#define OFFSET_MCR              0x10	/* Modem Control Register               */
#define OFFSET_LSR              0x14	/* Line Status Register                 */
#define OFFSET_MSR              0x18	/* Modem Status Register                */
#define OFFSET_SCR              0x1C	/* SCR Scratch Register                 */
#define OFFSET_IER_SET          0x20	/* Set Interrupt Enable Register        */
#define OFFSET_IER_CLEAR        0x24	/* Clear Interrupt Enable Register      */
#define OFFSET_THR              0x28	/* Transmit Holding register            */
#define OFFSET_RBR              0x2C	/* Receive Buffer register              */

#endif
