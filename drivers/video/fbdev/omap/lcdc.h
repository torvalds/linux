/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LCDC_H
#define LCDC_H
/*
 * Copyright (C) 2004 Nokia Corporation
 * Author: Imre Deak <imre.deak@nokia.com>
 */
#define OMAP_LCDC_BASE			0xfffec000
#define OMAP_LCDC_SIZE			256
#define OMAP_LCDC_IRQ			INT_LCD_CTRL

#define OMAP_LCDC_CONTROL		(OMAP_LCDC_BASE + 0x00)
#define OMAP_LCDC_TIMING0		(OMAP_LCDC_BASE + 0x04)
#define OMAP_LCDC_TIMING1		(OMAP_LCDC_BASE + 0x08)
#define OMAP_LCDC_TIMING2		(OMAP_LCDC_BASE + 0x0c)
#define OMAP_LCDC_STATUS		(OMAP_LCDC_BASE + 0x10)
#define OMAP_LCDC_SUBPANEL		(OMAP_LCDC_BASE + 0x14)
#define OMAP_LCDC_LINE_INT		(OMAP_LCDC_BASE + 0x18)
#define OMAP_LCDC_DISPLAY_STATUS	(OMAP_LCDC_BASE + 0x1c)

#define OMAP_LCDC_STAT_DONE		(1 << 0)
#define OMAP_LCDC_STAT_VSYNC		(1 << 1)
#define OMAP_LCDC_STAT_SYNC_LOST	(1 << 2)
#define OMAP_LCDC_STAT_ABC		(1 << 3)
#define OMAP_LCDC_STAT_LINE_INT		(1 << 4)
#define OMAP_LCDC_STAT_FUF		(1 << 5)
#define OMAP_LCDC_STAT_LOADED_PALETTE	(1 << 6)

#define OMAP_LCDC_CTRL_LCD_EN		(1 << 0)
#define OMAP_LCDC_CTRL_LCD_TFT		(1 << 7)
#define OMAP_LCDC_CTRL_LINE_IRQ_CLR_SEL	(1 << 10)

#define OMAP_LCDC_IRQ_VSYNC		(1 << 2)
#define OMAP_LCDC_IRQ_DONE		(1 << 3)
#define OMAP_LCDC_IRQ_LOADED_PALETTE	(1 << 4)
#define OMAP_LCDC_IRQ_LINE_NIRQ		(1 << 5)
#define OMAP_LCDC_IRQ_LINE		(1 << 6)
#define OMAP_LCDC_IRQ_MASK		(((1 << 5) - 1) << 2)

int omap_lcdc_set_dma_callback(void (*callback)(void *data), void *data);
void omap_lcdc_free_dma_callback(void);

extern const struct lcd_ctrl omap1_int_ctrl;

#endif
