/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/mach-netx/include/mach/xc.h
 *
 * Copyright (C) 2005 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 */

#ifndef __ASM_ARCH_XC_H
#define __ASM_ARCH_XC_H

struct xc {
	int no;
	unsigned int type;
	unsigned int version;
	void __iomem *xpec_base;
	void __iomem *xmac_base;
	void __iomem *sram_base;
	int irq;
	struct device *dev;
};

int xc_reset(struct xc *x);
int xc_stop(struct xc* x);
int xc_start(struct xc *x);
int xc_running(struct xc *x);
int xc_request_firmware(struct xc* x);
struct xc* request_xc(int xcno, struct device *dev);
void free_xc(struct xc *x);

#endif /* __ASM_ARCH_XC_H */
