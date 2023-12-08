/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OMAP WakeupGen header file
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 */
#ifndef OMAP_ARCH_WAKEUPGEN_H
#define OMAP_ARCH_WAKEUPGEN_H

/* OMAP4 and OMAP5 has same base address */
#define OMAP_WKUPGEN_BASE			0x48281000

#define OMAP_WKG_CONTROL_0			0x00
#define OMAP_WKG_ENB_A_0			0x10
#define OMAP_WKG_ENB_B_0			0x14
#define OMAP_WKG_ENB_C_0			0x18
#define OMAP_WKG_ENB_D_0			0x1c
#define OMAP_WKG_ENB_E_0			0x20
#define OMAP_WKG_ENB_A_1			0x410
#define OMAP_WKG_ENB_B_1			0x414
#define OMAP_WKG_ENB_C_1			0x418
#define OMAP_WKG_ENB_D_1			0x41c
#define OMAP_WKG_ENB_E_1			0x420
#define OMAP_AUX_CORE_BOOT_0			0x800
#define OMAP_AUX_CORE_BOOT_1			0x804
#define OMAP_AMBA_IF_MODE			0x80c
#define OMAP_PTMSYNCREQ_MASK			0xc00
#define OMAP_PTMSYNCREQ_EN			0xc04
#define OMAP_TIMESTAMPCYCLELO			0xc08
#define OMAP_TIMESTAMPCYCLEHI			0xc0c

extern void __iomem *omap_get_wakeupgen_base(void);
extern int omap_secure_apis_support(void);
#endif
