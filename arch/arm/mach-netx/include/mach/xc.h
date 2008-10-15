/*
 *  arch/arm/mach-netx/include/mach/xc.h
 *
 * Copyright (C) 2005 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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
