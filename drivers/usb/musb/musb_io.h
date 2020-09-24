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
 * @get_toggle: platform specific function to get toggle
 * @set_toggle: platform specific function to set toggle
 */
struct musb_io {
	u32	(*ep_offset)(u8 epnum, u16 offset);
	void	(*ep_select)(void __iomem *mbase, u8 epnum);
	u32	(*fifo_offset)(u8 epnum);
	void	(*read_fifo)(struct musb_hw_ep *hw_ep, u16 len, u8 *buf);
	void	(*write_fifo)(struct musb_hw_ep *hw_ep, u16 len, const u8 *buf);
	u32	(*busctl_offset)(u8 epnum, u16 offset);
	u16	(*get_toggle)(struct musb_qh *qh, int is_out);
	u16	(*set_toggle)(struct musb_qh *qh, int is_out, struct urb *urb);
};

/* Do not add new entries here, add them the struct musb_io instead */
extern u8 (*musb_readb)(void __iomem *addr, u32 offset);
extern void (*musb_writeb)(void __iomem *addr, u32 offset, u8 data);
extern u8 (*musb_clearb)(void __iomem *addr, u32 offset);
extern u16 (*musb_readw)(void __iomem *addr, u32 offset);
extern void (*musb_writew)(void __iomem *addr, u32 offset, u16 data);
extern u16 (*musb_clearw)(void __iomem *addr, u32 offset);
extern u32 musb_readl(void __iomem *addr, u32 offset);
extern void musb_writel(void __iomem *addr, u32 offset, u32 data);

#endif
