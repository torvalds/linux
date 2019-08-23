/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *   Application Peripheral Bus Clock Unit
 */

#ifndef __ASM_MACH_REGS_APBC_H
#define __ASM_MACH_REGS_APBC_H

#include "addr-map.h"

/* Common APB clock register bit definitions */
#define APBC_APBCLK	(1 << 0)  /* APB Bus Clock Enable */
#define APBC_FNCLK	(1 << 1)  /* Functional Clock Enable */
#define APBC_RST	(1 << 2)  /* Reset Generation */

/* Functional Clock Selection Mask */
#define APBC_FNCLKSEL(x)	(((x) & 0xf) << 4)

#endif /* __ASM_MACH_REGS_APBC_H */
