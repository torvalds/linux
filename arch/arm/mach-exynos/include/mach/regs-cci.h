/* arch/arm/mach-exynos/include/mach/regs-cci.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS5 CCI configutation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_REGS_CCI_H
#define __ASM_ARCH_REGS_CCI_H __FILE__

/* Control interface register offsets */
#define CTLR_OVERRIDE_REG	0x0
#define SPEC_CTLR_REG		0x4
#define SECURE_ACCESS_REG	0x8
#define STATUS_REG		0xc
#define IMPRECISE_ERR_REG	0x10
#define PERF_MON_CTRL_REG	0x100

/* Slave interface */
#define CCI_A15_SL_IFACE(x)		((x) + 0x5000)
#define CCI_A7_SL_IFACE(x)		((x) + 0x4000)

/* Slave interface register */
#define SNOOP_CTLR_REG		0x0

/* CORE_MISC SFR */
#define BACKBONE_SEL_REG	0x0
#define MDMA_SHARED_CTRL	0x10
#define SSS_SHARED_CTRL		0x20
#define G2D_SHARED_CTRL		0x30

#endif /* __ASM_ARCH_REGS_CCI_H */
