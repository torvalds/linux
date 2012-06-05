/*
 * drivers/video/sun3i/disp/drv_disp.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Danling <danliang@allwinnertech.com>
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

#ifndef __DRV_DISP_H__
#define __DRV_DISP_H__

#include "drv_disp_i.h"


typedef struct
{
	struct device		*dev;
	struct resource		*mem[DISP_IO_NUM];
	void __iomem		*io[DISP_IO_NUM];

	__u32 base_ccmu;
	__u32 base_sdram;
	__u32 base_pio;

	unsigned fb_screen_id[FB_MAX];
    unsigned layer_hdl[FB_MAX];
	void * fbinfo[FB_MAX];
	__u8 fb_num;
}fb_info_t;

#define MAX_EVENT_SEM 20
typedef struct
{
    __u32         	mid;
    __u32         	used;
    __u32         	status;
    __u32    		exit_mode;//0:clean all  1:disable interrupt
    struct semaphore *scaler_finished_sem[2];
    struct semaphore *event_sem[2][MAX_EVENT_SEM];
    __bool			de_cfg_rdy[2][MAX_EVENT_SEM];
    __bool			event_used[2][MAX_EVENT_SEM];
    __bool          b_cache[2];

	__bool			b_lcd_open[2];
}__disp_drv_t;

extern __s32 DRV_lcd_open(__u32 sel);
extern __s32 DRV_lcd_close(__u32 sel);
extern __s32 Fb_Init(void);
extern __s32 Fb_Exit(void);
#endif

