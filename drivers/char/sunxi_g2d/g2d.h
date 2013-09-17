/*
 * drivers/char/sunxi_g2d/g2d.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */


#ifndef __G2D_H__
#define __G2D_H__

#include"g2d_bsp.h"

/* Mixer status select */
#define G2D_FINISH_IRQ		(1<<8)
#define G2D_ERROR_IRQ		(1<<9)

typedef struct
{
	g2d_init_para init_para;

}g2d_dev_t;

int g2d_openclk(void);
int g2d_closeclk(void);
int g2d_clk_on(void);
int g2d_clk_off(void);
irqreturn_t g2d_handle_irq(int irq, void *dev_id);
int g2d_init(g2d_init_para *para);
int g2d_blit(g2d_blt * para);
int g2d_fill(g2d_fillrect * para);
int g2d_stretchblit(g2d_stretchblt * para);
int g2d_set_palette_table(g2d_palette *para);
int g2d_wait_cmd_finish(void);

#endif/* __G2D_H__ */
