// SPDX-License-Identifier: GPL-2.0
/*
 * MUSB OTG driver register I/O
 *
 * Copyright 2005 Mentor Graphics Corporation
 * Copyright (C) 2005-2006 by Texas Instruments
 * Copyright (C) 2006-2007 Nokia Corporation
 */

#ifndef __MUSB_LINUX_PLATFORM_ARCH_H__
#define __MUSB_LINUX_PLATFORM_ARCH_H__

#include <linux/io.h>

#define musb_ep_select(_mbase, _epnum)	musb->io.ep_select((_mbase), (_epnum))

/**
 * struct musb_io - IO functions for MUSB
 * @ep_offset:	platform specific function to get end point offset
 * @ep_select:	platform specific function to select end point
 * @fifo_offset: platform specific function to get fifo offset
 * @read_fifo:	platform specific function to read fifo
 * @write_fifo:	platform specific function to write fifo
 * @busctl_offset: platform specific function to get busctl offset
 */
struct musb_io {
	u32	(*ep_offset)(u8 epnum, u16 offset);
	void	(*ep_select)(void __iomem *mbase, u8 epnum);
	u32	(*fifo_offset)(u8 epnum);
	void	(*read_fifo)(struct musb_hw_ep *hw_ep, u16 len, u8 *buf);
	void	(*write_fifo)(struct musb_hw_ep *hw_ep, u16 len, const u8 *buf);
	u32	(*busctl_offset)(u8 epnum, u16 offset);
};

/* Do not add new entries here, add them the struct musb_io instead */
extern u8 (*musb_readb)(const void __iomem *addr, unsigned offset);
extern void (*musb_writeb)(void __iomem *addr, unsigned offset, u8 data);
extern u16 (*musb_readw)(const void __iomem *addr, unsigned offset);
extern void (*musb_writew)(void __iomem *addr, unsigned offset, u16 data);
extern u32 musb_readl(const void __iomem *addr, unsigned offset);
extern void musb_writel(void __iomem *addr, unsigned offset, u32 data);

#endif
