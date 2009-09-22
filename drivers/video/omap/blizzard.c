/*
 * Epson Blizzard LCD controller driver
 *
 * Copyright (C) 2004-2005 Nokia Corporation
 * Authors:     Juha Yrjola   <juha.yrjola@nokia.com>
 *	        Imre Deak     <imre.deak@nokia.com>
 * YUV support: Jussi Laako   <jussi.laako@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/clk.h>

#include <mach/dma.h>
#include <mach/omapfb.h>
#include <mach/blizzard.h>

#include "dispc.h"

#define MODULE_NAME				"blizzard"

#define BLIZZARD_REV_CODE			0x00
#define BLIZZARD_CONFIG				0x02
#define BLIZZARD_PLL_DIV			0x04
#define BLIZZARD_PLL_LOCK_RANGE			0x06
#define BLIZZARD_PLL_CLOCK_SYNTH_0		0x08
#define BLIZZARD_PLL_CLOCK_SYNTH_1		0x0a
#define BLIZZARD_PLL_MODE			0x0c
#define BLIZZARD_CLK_SRC			0x0e
#define BLIZZARD_MEM_BANK0_ACTIVATE		0x10
#define BLIZZARD_MEM_BANK0_STATUS		0x14
#define BLIZZARD_PANEL_CONFIGURATION		0x28
#define BLIZZARD_HDISP				0x2a
#define BLIZZARD_HNDP				0x2c
#define BLIZZARD_VDISP0				0x2e
#define BLIZZARD_VDISP1				0x30
#define BLIZZARD_VNDP				0x32
#define BLIZZARD_HSW				0x34
#define BLIZZARD_VSW				0x38
#define BLIZZARD_DISPLAY_MODE			0x68
#define BLIZZARD_INPUT_WIN_X_START_0		0x6c
#define BLIZZARD_DATA_SOURCE_SELECT		0x8e
#define BLIZZARD_DISP_MEM_DATA_PORT		0x90
#define BLIZZARD_DISP_MEM_READ_ADDR0		0x92
#define BLIZZARD_POWER_SAVE			0xE6
#define BLIZZARD_NDISP_CTRL_STATUS		0xE8

/* Data source select */
/* For S1D13745 */
#define BLIZZARD_SRC_WRITE_LCD_BACKGROUND	0x00
#define BLIZZARD_SRC_WRITE_LCD_DESTRUCTIVE	0x01
#define BLIZZARD_SRC_WRITE_OVERLAY_ENABLE	0x04
#define BLIZZARD_SRC_DISABLE_OVERLAY		0x05
/* For S1D13744 */
#define BLIZZARD_SRC_WRITE_LCD			0x00
#define BLIZZARD_SRC_BLT_LCD			0x06

#define BLIZZARD_COLOR_RGB565			0x01
#define BLIZZARD_COLOR_YUV420			0x09

#define BLIZZARD_VERSION_S1D13745		0x01	/* Hailstorm */
#define BLIZZARD_VERSION_S1D13744		0x02	/* Blizzard */

#define BLIZZARD_AUTO_UPDATE_TIME		(HZ / 20)

/* Reserve 4 request slots for requests in irq context */
#define REQ_POOL_SIZE			24
#define IRQ_REQ_POOL_SIZE		4

#define REQ_FROM_IRQ_POOL 0x01

#define REQ_COMPLETE	0
#define REQ_PENDING	1

struct blizzard_reg_list {
	int	start;
	int	end;
};

/* These need to be saved / restored separately from the rest. */
static struct blizzard_reg_list blizzard_pll_regs[] = {
	{
		.start	= 0x04,		/* Don't save PLL ctrl (0x0C) */
		.end	= 0x0a,
	},
	{
		.start	= 0x0e,		/* Clock configuration */
		.end	= 0x0e,
	},
};

static struct blizzard_reg_list blizzard_gen_regs[] = {
	{
		.start	= 0x18,		/* SDRAM control */
		.end	= 0x20,
	},
	{
		.start	= 0x28,		/* LCD Panel configuration */
		.end	= 0x5a,		/* HSSI interface, TV configuration */
	},
};

static u8 blizzard_reg_cache[0x5a / 2];

struct update_param {
	int	plane;
	int	x, y, width, height;
	int	out_x, out_y;
	int	out_width, out_height;
	int	color_mode;
	int	bpp;
	int	flags;
};

struct blizzard_request {
	struct list_head entry;
	unsigned int	 flags;

	int		 (*handler)(struct blizzard_request *req);
	void		 (*complete)(void *data);
	void		 *complete_data;

	union {
		struct update_param	update;
		struct completion	*sync;
	} par;
};

struct plane_info {
	unsigned long offset;
	int pos_x, pos_y;
	int width, height;
	int out_width, out_height;
	int scr_width;
	int color_mode;
	int bpp;
};

struct blizzard_struct {
	enum omapfb_update_mode	update_mode;
	enum omapfb_update_mode	update_mode_before_suspend;

	struct timer_list	auto_update_timer;
	int			stop_auto_update;
	struct omapfb_update_window	auto_update_window;
	int			enabled_planes;
	int			vid_nonstd_color;
	int			vid_scaled;
	int			last_color_mode;
	int			zoom_on;
	int			zoom_area_gx1;
	int			zoom_area_gx2;
	int			zoom_area_gy1;
	int			zoom_area_gy2;
	int			screen_width;
	int			screen_height;
	unsigned		te_connected:1;
	unsigned		vsync_only:1;

	struct plane_info	plane[OMAPFB_PLANE_NUM];

	struct blizzard_request	req_pool[REQ_POOL_SIZE];
	struct list_head	pending_req_list;
	struct list_head	free_req_list;
	struct semaphore	req_sema;
	spinlock_t		req_lock;

	unsigned long		sys_ck_rate;
	struct extif_timings	reg_timings, lut_timings;

	u32			max_transmit_size;
	u32			extif_clk_period;
	int			extif_clk_div;
	unsigned long		pix_tx_time;
	unsigned long		line_upd_time;

	struct omapfb_device	*fbdev;
	struct lcd_ctrl_extif	*extif;
	struct lcd_ctrl		*int_ctrl;

	void			(*power_up)(struct device *dev);
	void			(*power_down)(struct device *dev);

	int			version;
} blizzard;

struct lcd_ctrl blizzard_ctrl;

static u8 blizzard_read_reg(u8 reg)
{
	u8 data;

	blizzard.extif->set_bits_per_cycle(8);
	blizzard.extif->write_command(&reg, 1);
	blizzard.extif->read_data(&data, 1);

	return data;
}

static void blizzard_write_reg(u8 reg, u8 val)
{
	blizzard.extif->set_bits_per_cycle(8);
	blizzard.extif->write_command(&reg, 1);
	blizzard.extif->write_data(&val, 1);
}

static void blizzard_restart_sdram(void)
{
	unsigned long tmo;

	blizzard_write_reg(BLIZZARD_MEM_BANK0_ACTIVATE, 0);
	udelay(50);
	blizzard_write_reg(BLIZZARD_MEM_BANK0_ACTIVATE, 1);
	tmo = jiffies + msecs_to_jiffies(200);
	while (!(blizzard_read_reg(BLIZZARD_MEM_BANK0_STATUS) & 0x01)) {
		if (time_after(jiffies, tmo)) {
			dev_err(blizzard.fbdev->dev,
					"s1d1374x: SDRAM not ready\n");
			break;
		}
		msleep(1);
	}
}

static void blizzard_stop_sdram(void)
{
	blizzard_write_reg(BLIZZARD_MEM_BANK0_ACTIVATE, 0);
}

/* Wait until the last window was completely written into the controllers
 * SDRAM and we can start transferring the next window.
 */
static void blizzard_wait_line_buffer(void)
{
	unsigned long tmo = jiffies + msecs_to_jiffies(30);

	while (blizzard_read_reg(BLIZZARD_NDISP_CTRL_STATUS) & (1 << 7)) {
		if (time_after(jiffies, tmo)) {
			if (printk_ratelimit())
				dev_err(blizzard.fbdev->dev,
					"s1d1374x: line buffer not ready\n");
			break;
		}
	}
}

/* Wait until the YYC color space converter is idle. */
static void blizzard_wait_yyc(void)
{
	unsigned long tmo = jiffies + msecs_to_jiffies(30);

	while (blizzard_read_reg(BLIZZARD_NDISP_CTRL_STATUS) & (1 << 4)) {
		if (time_after(jiffies, tmo)) {
			if (printk_ratelimit())
				dev_err(blizzard.fbdev->dev,
					"s1d1374x: YYC not ready\n");
			break;
		}
	}
}

static void disable_overlay(void)
{
	blizzard_write_reg(BLIZZARD_DATA_SOURCE_SELECT,
				BLIZZARD_SRC_DISABLE_OVERLAY);
}

static void set_window_regs(int x_start, int y_start, int x_end, int y_end,
			    int x_out_start, int y_out_start,
			    int x_out_end, int y_out_end, int color_mode,
			    int zoom_off, int flags)
{
	u8 tmp[18];
	u8 cmd;

	x_end--;
	y_end--;
	tmp[0] = x_start;
	tmp[1] = x_start >> 8;
	tmp[2] = y_start;
	tmp[3] = y_start >> 8;
	tmp[4] = x_end;
	tmp[5] = x_end >> 8;
	tmp[6] = y_end;
	tmp[7] = y_end >> 8;

	x_out_end--;
	y_out_end--;
	tmp[8]  = x_out_start;
	tmp[9]  = x_out_start >> 8;
	tmp[10] = y_out_start;
	tmp[11] = y_out_start >> 8;
	tmp[12] = x_out_end;
	tmp[13] = x_out_end >> 8;
	tmp[14] = y_out_end;
	tmp[15] = y_out_end >> 8;

	tmp[16] = color_mode;
	if (zoom_off && blizzard.version == BLIZZARD_VERSION_S1D13745)
		tmp[17] = BLIZZARD_SRC_WRITE_LCD_BACKGROUND;
	else if (flags & OMAPFB_FORMAT_FLAG_ENABLE_OVERLAY)
		tmp[17] = BLIZZARD_SRC_WRITE_OVERLAY_ENABLE;
	else
		tmp[17] = blizzard.version == BLIZZARD_VERSION_S1D13744 ?
				BLIZZARD_SRC_WRITE_LCD :
				BLIZZARD_SRC_WRITE_LCD_DESTRUCTIVE;

	blizzard.extif->set_bits_per_cycle(8);
	cmd = BLIZZARD_INPUT_WIN_X_START_0;
	blizzard.extif->write_command(&cmd, 1);
	blizzard.extif->write_data(tmp, 18);
}

static void enable_tearsync(int y, int width, int height, int screen_height,
			    int out_height, int force_vsync)
{
	u8 b;

	b = blizzard_read_reg(BLIZZARD_NDISP_CTRL_STATUS);
	b |= 1 << 3;
	blizzard_write_reg(BLIZZARD_NDISP_CTRL_STATUS, b);

	if (likely(blizzard.vsync_only || force_vsync)) {
		blizzard.extif->enable_tearsync(1, 0);
		return;
	}

	if (width * blizzard.pix_tx_time < blizzard.line_upd_time) {
		blizzard.extif->enable_tearsync(1, 0);
		return;
	}

	if ((width * blizzard.pix_tx_time / 1000) * height <
	    (y + out_height) * (blizzard.line_upd_time / 1000)) {
		blizzard.extif->enable_tearsync(1, 0);
		return;
	}

	blizzard.extif->enable_tearsync(1, y + 1);
}

static void disable_tearsync(void)
{
	u8 b;

	blizzard.extif->enable_tearsync(0, 0);
	b = blizzard_read_reg(BLIZZARD_NDISP_CTRL_STATUS);
	b &= ~(1 << 3);
	blizzard_write_reg(BLIZZARD_NDISP_CTRL_STATUS, b);
	b = blizzard_read_reg(BLIZZARD_NDISP_CTRL_STATUS);
}

static inline void set_extif_timings(const struct extif_timings *t);

static inline struct blizzard_request *alloc_req(void)
{
	unsigned long flags;
	struct blizzard_request *req;
	int req_flags = 0;

	if (!in_interrupt())
		down(&blizzard.req_sema);
	else
		req_flags = REQ_FROM_IRQ_POOL;

	spin_lock_irqsave(&blizzard.req_lock, flags);
	BUG_ON(list_empty(&blizzard.free_req_list));
	req = list_entry(blizzard.free_req_list.next,
			 struct blizzard_request, entry);
	list_del(&req->entry);
	spin_unlock_irqrestore(&blizzard.req_lock, flags);

	INIT_LIST_HEAD(&req->entry);
	req->flags = req_flags;

	return req;
}

static inline void free_req(struct blizzard_request *req)
{
	unsigned long flags;

	spin_lock_irqsave(&blizzard.req_lock, flags);

	list_del(&req->entry);
	list_add(&req->entry, &blizzard.free_req_list);
	if (!(req->flags & REQ_FROM_IRQ_POOL))
		up(&blizzard.req_sema);

	spin_unlock_irqrestore(&blizzard.req_lock, flags);
}

static void process_pending_requests(void)
{
	unsigned long flags;

	spin_lock_irqsave(&blizzard.req_lock, flags);

	while (!list_empty(&blizzard.pending_req_list)) {
		struct blizzard_request *req;
		void (*complete)(void *);
		void *complete_data;

		req = list_entry(blizzard.pending_req_list.next,
				 struct blizzard_request, entry);
		spin_unlock_irqrestore(&blizzard.req_lock, flags);

		if (req->handler(req) == REQ_PENDING)
			return;

		complete = req->complete;
		complete_data = req->complete_data;
		free_req(req);

		if (complete)
			complete(complete_data);

		spin_lock_irqsave(&blizzard.req_lock, flags);
	}

	spin_unlock_irqrestore(&blizzard.req_lock, flags);
}

static void submit_req_list(struct list_head *head)
{
	unsigned long flags;
	int process = 1;

	spin_lock_irqsave(&blizzard.req_lock, flags);
	if (likely(!list_empty(&blizzard.pending_req_list)))
		process = 0;
	list_splice_init(head, blizzard.pending_req_list.prev);
	spin_unlock_irqrestore(&blizzard.req_lock, flags);

	if (process)
		process_pending_requests();
}

static void request_complete(void *data)
{
	struct blizzard_request	*req = (struct blizzard_request *)data;
	void			(*complete)(void *);
	void			*complete_data;

	complete = req->complete;
	complete_data = req->complete_data;

	free_req(req);

	if (complete)
		complete(complete_data);

	process_pending_requests();
}


static int do_full_screen_update(struct blizzard_request *req)
{
	int i;
	int flags;

	for (i = 0; i < 3; i++) {
		struct plane_info *p = &blizzard.plane[i];
		if (!(blizzard.enabled_planes & (1 << i))) {
			blizzard.int_ctrl->enable_plane(i, 0);
			continue;
		}
		dev_dbg(blizzard.fbdev->dev, "pw %d ph %d\n",
			p->width, p->height);
		blizzard.int_ctrl->setup_plane(i,
				OMAPFB_CHANNEL_OUT_LCD, p->offset,
				p->scr_width, p->pos_x, p->pos_y,
				p->width, p->height,
				p->color_mode);
		blizzard.int_ctrl->enable_plane(i, 1);
	}

	dev_dbg(blizzard.fbdev->dev, "sw %d sh %d\n",
		blizzard.screen_width, blizzard.screen_height);
	blizzard_wait_line_buffer();
	flags = req->par.update.flags;
	if (flags & OMAPFB_FORMAT_FLAG_TEARSYNC)
		enable_tearsync(0, blizzard.screen_width,
				blizzard.screen_height,
				blizzard.screen_height,
				blizzard.screen_height,
				flags & OMAPFB_FORMAT_FLAG_FORCE_VSYNC);
	else
		disable_tearsync();

	set_window_regs(0, 0, blizzard.screen_width, blizzard.screen_height,
			0, 0, blizzard.screen_width, blizzard.screen_height,
			BLIZZARD_COLOR_RGB565, blizzard.zoom_on, flags);
	blizzard.zoom_on = 0;

	blizzard.extif->set_bits_per_cycle(16);
	/* set_window_regs has left the register index at the right
	 * place, so no need to set it here.
	 */
	blizzard.extif->transfer_area(blizzard.screen_width,
				      blizzard.screen_height,
				      request_complete, req);
	return REQ_PENDING;
}

static int check_1d_intersect(int a1, int a2, int b1, int b2)
{
	if (a2 <= b1 || b2 <= a1)
		return 0;
	return 1;
}

/* Setup all planes with an overlapping area with the update window. */
static int do_partial_update(struct blizzard_request *req, int plane,
			     int x, int y, int w, int h,
			     int x_out, int y_out, int w_out, int h_out,
			     int wnd_color_mode, int bpp)
{
	int i;
	int gx1, gy1, gx2, gy2;
	int gx1_out, gy1_out, gx2_out, gy2_out;
	int color_mode;
	int flags;
	int zoom_off;
	int have_zoom_for_this_update = 0;

	/* Global coordinates, relative to pixel 0,0 of the LCD */
	gx1 = x + blizzard.plane[plane].pos_x;
	gy1 = y + blizzard.plane[plane].pos_y;
	gx2 = gx1 + w;
	gy2 = gy1 + h;

	flags = req->par.update.flags;
	if (flags & OMAPFB_FORMAT_FLAG_DOUBLE) {
		gx1_out = gx1;
		gy1_out = gy1;
		gx2_out = gx1 + w * 2;
		gy2_out = gy1 + h * 2;
	} else {
		gx1_out = x_out + blizzard.plane[plane].pos_x;
		gy1_out = y_out + blizzard.plane[plane].pos_y;
		gx2_out = gx1_out + w_out;
		gy2_out = gy1_out + h_out;
	}

	for (i = 0; i < OMAPFB_PLANE_NUM; i++) {
		struct plane_info *p = &blizzard.plane[i];
		int px1, py1;
		int px2, py2;
		int pw, ph;
		int pposx, pposy;
		unsigned long offset;

		if (!(blizzard.enabled_planes & (1 << i))  ||
		    (wnd_color_mode && i != plane)) {
			blizzard.int_ctrl->enable_plane(i, 0);
			continue;
		}
		/* Plane coordinates */
		if (i == plane) {
			/* Plane in which we are doing the update.
			 * Local coordinates are the one in the update
			 * request.
			 */
			px1 = x;
			py1 = y;
			px2 = x + w;
			py2 = y + h;
			pposx = 0;
			pposy = 0;
		} else {
			/* Check if this plane has an overlapping part */
			px1 = gx1 - p->pos_x;
			py1 = gy1 - p->pos_y;
			px2 = gx2 - p->pos_x;
			py2 = gy2 - p->pos_y;
			if (px1 >= p->width || py1 >= p->height ||
			    px2 <= 0 || py2 <= 0) {
				blizzard.int_ctrl->enable_plane(i, 0);
				continue;
			}
			/* Calculate the coordinates for the overlapping
			 * part in the plane's local coordinates.
			 */
			pposx = -px1;
			pposy = -py1;
			if (px1 < 0)
				px1 = 0;
			if (py1 < 0)
				py1 = 0;
			if (px2 > p->width)
				px2 = p->width;
			if (py2 > p->height)
				py2 = p->height;
			if (pposx < 0)
				pposx = 0;
			if (pposy < 0)
				pposy = 0;
		}
		pw = px2 - px1;
		ph = py2 - py1;
		offset = p->offset + (p->scr_width * py1 + px1) * p->bpp / 8;
		if (wnd_color_mode)
			/* Window embedded in the plane with a differing
			 * color mode / bpp. Calculate the number of DMA
			 * transfer elements in terms of the plane's bpp.
			 */
			pw = (pw + 1) * bpp / p->bpp;
#ifdef VERBOSE
		dev_dbg(blizzard.fbdev->dev,
			"plane %d offset %#08lx pposx %d pposy %d "
			"px1 %d py1 %d pw %d ph %d\n",
			i, offset, pposx, pposy, px1, py1, pw, ph);
#endif
		blizzard.int_ctrl->setup_plane(i,
				OMAPFB_CHANNEL_OUT_LCD, offset,
				p->scr_width,
				pposx, pposy, pw, ph,
				p->color_mode);

		blizzard.int_ctrl->enable_plane(i, 1);
	}

	switch (wnd_color_mode) {
	case OMAPFB_COLOR_YUV420:
		color_mode = BLIZZARD_COLOR_YUV420;
		/* Currently only the 16 bits/pixel cycle format is
		 * supported on the external interface. Adjust the number
		 * of transfer elements per line for 12bpp format.
		 */
		w = (w + 1) * 3 / 4;
		break;
	default:
		color_mode = BLIZZARD_COLOR_RGB565;
		break;
	}

	blizzard_wait_line_buffer();
	if (blizzard.last_color_mode == BLIZZARD_COLOR_YUV420)
		blizzard_wait_yyc();
	blizzard.last_color_mode = color_mode;
	if (flags & OMAPFB_FORMAT_FLAG_TEARSYNC)
		enable_tearsync(gy1, w, h,
				blizzard.screen_height,
				h_out,
				flags & OMAPFB_FORMAT_FLAG_FORCE_VSYNC);
	else
		disable_tearsync();

	if ((gx2_out - gx1_out) != (gx2 - gx1) ||
	    (gy2_out - gy1_out) != (gy2 - gy1))
		have_zoom_for_this_update = 1;

	/* 'background' type of screen update (as opposed to 'destructive')
	   can be used to disable scaling if scaling is active */
	zoom_off = blizzard.zoom_on && !have_zoom_for_this_update &&
	    (gx1_out == 0) && (gx2_out == blizzard.screen_width) &&
	    (gy1_out == 0) && (gy2_out == blizzard.screen_height) &&
	    (gx1 == 0) && (gy1 == 0);

	if (blizzard.zoom_on && !have_zoom_for_this_update && !zoom_off &&
	    check_1d_intersect(blizzard.zoom_area_gx1, blizzard.zoom_area_gx2,
			       gx1_out, gx2_out) &&
	    check_1d_intersect(blizzard.zoom_area_gy1, blizzard.zoom_area_gy2,
			       gy1_out, gy2_out)) {
		/* Previous screen update was using scaling, current update
		 * is not using it. Additionally, current screen update is
		 * going to overlap with the scaled area. Scaling needs to be
		 * disabled in order to avoid 'magnifying glass' effect.
		 * Dummy setup of background window can be used for this.
		 */
		set_window_regs(0, 0, blizzard.screen_width,
				blizzard.screen_height,
				0, 0, blizzard.screen_width,
				blizzard.screen_height,
				BLIZZARD_COLOR_RGB565, 1, flags);
		blizzard.zoom_on = 0;
	}

	/* remember scaling settings if we have scaled update */
	if (have_zoom_for_this_update) {
		blizzard.zoom_on = 1;
		blizzard.zoom_area_gx1 = gx1_out;
		blizzard.zoom_area_gx2 = gx2_out;
		blizzard.zoom_area_gy1 = gy1_out;
		blizzard.zoom_area_gy2 = gy2_out;
	}

	set_window_regs(gx1, gy1, gx2, gy2, gx1_out, gy1_out, gx2_out, gy2_out,
			color_mode, zoom_off, flags);
	if (zoom_off)
		blizzard.zoom_on = 0;

	blizzard.extif->set_bits_per_cycle(16);
	/* set_window_regs has left the register index at the right
	 * place, so no need to set it here.
	 */
	blizzard.extif->transfer_area(w, h, request_complete, req);

	return REQ_PENDING;
}

static int send_frame_handler(struct blizzard_request *req)
{
	struct update_param *par = &req->par.update;
	int plane = par->plane;

#ifdef VERBOSE
	dev_dbg(blizzard.fbdev->dev,
		"send_frame: x %d y %d w %d h %d "
		"x_out %d y_out %d w_out %d h_out %d "
		"color_mode %04x flags %04x planes %01x\n",
		par->x, par->y, par->width, par->height,
		par->out_x, par->out_y, par->out_width, par->out_height,
		par->color_mode, par->flags, blizzard.enabled_planes);
#endif
	if (par->flags & OMAPFB_FORMAT_FLAG_DISABLE_OVERLAY)
		disable_overlay();

	if ((blizzard.enabled_planes & blizzard.vid_nonstd_color) ||
	     (blizzard.enabled_planes & blizzard.vid_scaled))
		return do_full_screen_update(req);

	return do_partial_update(req, plane, par->x, par->y,
				 par->width, par->height,
				 par->out_x, par->out_y,
				 par->out_width, par->out_height,
				 par->color_mode, par->bpp);
}

static void send_frame_complete(void *data)
{
}

#define ADD_PREQ(_x, _y, _w, _h, _x_out, _y_out, _w_out, _h_out) do {	\
	req = alloc_req();			\
	req->handler	= send_frame_handler;	\
	req->complete	= send_frame_complete;	\
	req->par.update.plane = plane_idx;	\
	req->par.update.x = _x;			\
	req->par.update.y = _y;			\
	req->par.update.width  = _w;		\
	req->par.update.height = _h;		\
	req->par.update.out_x = _x_out;		\
	req->par.update.out_y = _y_out;		\
	req->par.update.out_width = _w_out;	\
	req->par.update.out_height = _h_out;	\
	req->par.update.bpp = bpp;		\
	req->par.update.color_mode = color_mode;\
	req->par.update.flags	  = flags;	\
	list_add_tail(&req->entry, req_head);	\
} while(0)

static void create_req_list(int plane_idx,
			    struct omapfb_update_window *win,
			    struct list_head *req_head)
{
	struct blizzard_request *req;
	int x = win->x;
	int y = win->y;
	int width = win->width;
	int height = win->height;
	int x_out = win->out_x;
	int y_out = win->out_y;
	int width_out = win->out_width;
	int height_out = win->out_height;
	int color_mode;
	int bpp;
	int flags;
	unsigned int ystart = y;
	unsigned int yspan = height;
	unsigned int ystart_out = y_out;
	unsigned int yspan_out = height_out;

	flags = win->format & ~OMAPFB_FORMAT_MASK;
	color_mode = win->format & OMAPFB_FORMAT_MASK;
	switch (color_mode) {
	case OMAPFB_COLOR_YUV420:
		/* Embedded window with different color mode */
		bpp = 12;
		/* X, Y, height must be aligned at 2, width at 4 pixels */
		x &= ~1;
		y &= ~1;
		height = yspan = height & ~1;
		width = width & ~3;
		break;
	default:
		/* Same as the plane color mode */
		bpp = blizzard.plane[plane_idx].bpp;
		break;
	}
	if (width * height * bpp / 8 > blizzard.max_transmit_size) {
		yspan = blizzard.max_transmit_size / (width * bpp / 8);
		yspan_out = yspan * height_out / height;
		ADD_PREQ(x, ystart, width, yspan, x_out, ystart_out,
			 width_out, yspan_out);
		ystart += yspan;
		ystart_out += yspan_out;
		yspan = height - yspan;
		yspan_out = height_out - yspan_out;
		flags &= ~OMAPFB_FORMAT_FLAG_TEARSYNC;
	}

	ADD_PREQ(x, ystart, width, yspan, x_out, ystart_out,
		 width_out, yspan_out);
}

static void auto_update_complete(void *data)
{
	if (!blizzard.stop_auto_update)
		mod_timer(&blizzard.auto_update_timer,
			  jiffies + BLIZZARD_AUTO_UPDATE_TIME);
}

static void blizzard_update_window_auto(unsigned long arg)
{
	LIST_HEAD(req_list);
	struct blizzard_request *last;
	struct omapfb_plane_struct *plane;

	plane = blizzard.fbdev->fb_info[0]->par;
	create_req_list(plane->idx,
			&blizzard.auto_update_window, &req_list);
	last = list_entry(req_list.prev, struct blizzard_request, entry);

	last->complete = auto_update_complete;
	last->complete_data = NULL;

	submit_req_list(&req_list);
}

int blizzard_update_window_async(struct fb_info *fbi,
				 struct omapfb_update_window *win,
				 void (*complete_callback)(void *arg),
				 void *complete_callback_data)
{
	LIST_HEAD(req_list);
	struct blizzard_request *last;
	struct omapfb_plane_struct *plane = fbi->par;

	if (unlikely(blizzard.update_mode != OMAPFB_MANUAL_UPDATE))
		return -EINVAL;
	if (unlikely(!blizzard.te_connected &&
		     (win->format & OMAPFB_FORMAT_FLAG_TEARSYNC)))
		return -EINVAL;

	create_req_list(plane->idx, win, &req_list);
	last = list_entry(req_list.prev, struct blizzard_request, entry);

	last->complete = complete_callback;
	last->complete_data = (void *)complete_callback_data;

	submit_req_list(&req_list);

	return 0;
}
EXPORT_SYMBOL(blizzard_update_window_async);

static int update_full_screen(void)
{
	return blizzard_update_window_async(blizzard.fbdev->fb_info[0],
				     &blizzard.auto_update_window, NULL, NULL);

}

static int blizzard_setup_plane(int plane, int channel_out,
				  unsigned long offset, int screen_width,
				  int pos_x, int pos_y, int width, int height,
				  int color_mode)
{
	struct plane_info *p;

#ifdef VERBOSE
	dev_dbg(blizzard.fbdev->dev,
		    "plane %d ch_out %d offset %#08lx scr_width %d "
		    "pos_x %d pos_y %d width %d height %d color_mode %d\n",
		    plane, channel_out, offset, screen_width,
		    pos_x, pos_y, width, height, color_mode);
#endif
	if ((unsigned)plane > OMAPFB_PLANE_NUM)
		return -EINVAL;
	p = &blizzard.plane[plane];

	switch (color_mode) {
	case OMAPFB_COLOR_YUV422:
	case OMAPFB_COLOR_YUY422:
		p->bpp = 16;
		blizzard.vid_nonstd_color &= ~(1 << plane);
		break;
	case OMAPFB_COLOR_YUV420:
		p->bpp = 12;
		blizzard.vid_nonstd_color |= 1 << plane;
		break;
	case OMAPFB_COLOR_RGB565:
		p->bpp = 16;
		blizzard.vid_nonstd_color &= ~(1 << plane);
		break;
	default:
		return -EINVAL;
	}

	p->offset = offset;
	p->pos_x = pos_x;
	p->pos_y = pos_y;
	p->width = width;
	p->height = height;
	p->scr_width = screen_width;
	if (!p->out_width)
		p->out_width = width;
	if (!p->out_height)
		p->out_height = height;

	p->color_mode = color_mode;

	return 0;
}

static int blizzard_set_scale(int plane, int orig_w, int orig_h,
			      int out_w, int out_h)
{
	struct plane_info *p = &blizzard.plane[plane];
	int r;

	dev_dbg(blizzard.fbdev->dev,
		"plane %d orig_w %d orig_h %d out_w %d out_h %d\n",
		plane, orig_w, orig_h, out_w, out_h);
	if ((unsigned)plane > OMAPFB_PLANE_NUM)
		return -ENODEV;

	r = blizzard.int_ctrl->set_scale(plane, orig_w, orig_h, out_w, out_h);
	if (r < 0)
		return r;

	p->width = orig_w;
	p->height = orig_h;
	p->out_width = out_w;
	p->out_height = out_h;
	if (orig_w == out_w && orig_h == out_h)
		blizzard.vid_scaled &= ~(1 << plane);
	else
		blizzard.vid_scaled |= 1 << plane;

	return 0;
}

static int blizzard_set_rotate(int angle)
{
	u32 l;

	l = blizzard_read_reg(BLIZZARD_PANEL_CONFIGURATION);
	l &= ~0x03;

	switch (angle) {
	case 0:
		l = l | 0x00;
		break;
	case 90:
		l = l | 0x03;
		break;
	case 180:
		l = l | 0x02;
		break;
	case 270:
		l = l | 0x01;
		break;
	default:
		return -EINVAL;
	}

	blizzard_write_reg(BLIZZARD_PANEL_CONFIGURATION, l);

	return 0;
}

static int blizzard_enable_plane(int plane, int enable)
{
	if (enable)
		blizzard.enabled_planes |= 1 << plane;
	else
		blizzard.enabled_planes &= ~(1 << plane);

	return 0;
}

static int sync_handler(struct blizzard_request *req)
{
	complete(req->par.sync);
	return REQ_COMPLETE;
}

static void blizzard_sync(void)
{
	LIST_HEAD(req_list);
	struct blizzard_request *req;
	struct completion comp;

	req = alloc_req();

	req->handler = sync_handler;
	req->complete = NULL;
	init_completion(&comp);
	req->par.sync = &comp;

	list_add(&req->entry, &req_list);
	submit_req_list(&req_list);

	wait_for_completion(&comp);
}


static void blizzard_bind_client(struct omapfb_notifier_block *nb)
{
	if (blizzard.update_mode == OMAPFB_MANUAL_UPDATE) {
		omapfb_notify_clients(blizzard.fbdev, OMAPFB_EVENT_READY);
	}
}

static int blizzard_set_update_mode(enum omapfb_update_mode mode)
{
	if (unlikely(mode != OMAPFB_MANUAL_UPDATE &&
		     mode != OMAPFB_AUTO_UPDATE &&
		     mode != OMAPFB_UPDATE_DISABLED))
		return -EINVAL;

	if (mode == blizzard.update_mode)
		return 0;

	dev_info(blizzard.fbdev->dev, "s1d1374x: setting update mode to %s\n",
			mode == OMAPFB_UPDATE_DISABLED ? "disabled" :
			(mode == OMAPFB_AUTO_UPDATE ? "auto" : "manual"));

	switch (blizzard.update_mode) {
	case OMAPFB_MANUAL_UPDATE:
		omapfb_notify_clients(blizzard.fbdev, OMAPFB_EVENT_DISABLED);
		break;
	case OMAPFB_AUTO_UPDATE:
		blizzard.stop_auto_update = 1;
		del_timer_sync(&blizzard.auto_update_timer);
		break;
	case OMAPFB_UPDATE_DISABLED:
		break;
	}

	blizzard.update_mode = mode;
	blizzard_sync();
	blizzard.stop_auto_update = 0;

	switch (mode) {
	case OMAPFB_MANUAL_UPDATE:
		omapfb_notify_clients(blizzard.fbdev, OMAPFB_EVENT_READY);
		break;
	case OMAPFB_AUTO_UPDATE:
		blizzard_update_window_auto(0);
		break;
	case OMAPFB_UPDATE_DISABLED:
		break;
	}

	return 0;
}

static enum omapfb_update_mode blizzard_get_update_mode(void)
{
	return blizzard.update_mode;
}

static inline void set_extif_timings(const struct extif_timings *t)
{
	blizzard.extif->set_timings(t);
}

static inline unsigned long round_to_extif_ticks(unsigned long ps, int div)
{
	int bus_tick = blizzard.extif_clk_period * div;
	return (ps + bus_tick - 1) / bus_tick * bus_tick;
}

static int calc_reg_timing(unsigned long sysclk, int div)
{
	struct extif_timings *t;
	unsigned long systim;

	/* CSOnTime 0, WEOnTime 2 ns, REOnTime 2 ns,
	 * AccessTime 2 ns + 12.2 ns (regs),
	 * WEOffTime = WEOnTime + 1 ns,
	 * REOffTime = REOnTime + 12 ns (regs),
	 * CSOffTime = REOffTime + 1 ns
	 * ReadCycle = 2ns + 2*SYSCLK  (regs),
	 * WriteCycle = 2*SYSCLK + 2 ns,
	 * CSPulseWidth = 10 ns */

	systim = 1000000000 / (sysclk / 1000);
	dev_dbg(blizzard.fbdev->dev,
		  "Blizzard systim %lu ps extif_clk_period %u div %d\n",
		  systim, blizzard.extif_clk_period, div);

	t = &blizzard.reg_timings;
	memset(t, 0, sizeof(*t));

	t->clk_div = div;

	t->cs_on_time = 0;
	t->we_on_time = round_to_extif_ticks(t->cs_on_time + 2000, div);
	t->re_on_time = round_to_extif_ticks(t->cs_on_time + 2000, div);
	t->access_time = round_to_extif_ticks(t->re_on_time + 12200, div);
	t->we_off_time = round_to_extif_ticks(t->we_on_time + 1000, div);
	t->re_off_time = round_to_extif_ticks(t->re_on_time + 13000, div);
	t->cs_off_time = round_to_extif_ticks(t->re_off_time + 1000, div);
	t->we_cycle_time = round_to_extif_ticks(2 * systim + 2000, div);
	if (t->we_cycle_time < t->we_off_time)
		t->we_cycle_time = t->we_off_time;
	t->re_cycle_time = round_to_extif_ticks(2 * systim + 2000, div);
	if (t->re_cycle_time < t->re_off_time)
		t->re_cycle_time = t->re_off_time;
	t->cs_pulse_width = 0;

	dev_dbg(blizzard.fbdev->dev, "[reg]cson %d csoff %d reon %d reoff %d\n",
		 t->cs_on_time, t->cs_off_time, t->re_on_time, t->re_off_time);
	dev_dbg(blizzard.fbdev->dev, "[reg]weon %d weoff %d recyc %d wecyc %d\n",
		 t->we_on_time, t->we_off_time, t->re_cycle_time,
		 t->we_cycle_time);
	dev_dbg(blizzard.fbdev->dev, "[reg]rdaccess %d cspulse %d\n",
		 t->access_time, t->cs_pulse_width);

	return blizzard.extif->convert_timings(t);
}

static int calc_lut_timing(unsigned long sysclk, int div)
{
	struct extif_timings *t;
	unsigned long systim;

	/* CSOnTime 0, WEOnTime 2 ns, REOnTime 2 ns,
	 * AccessTime 2 ns + 4 * SYSCLK + 26 (lut),
	 * WEOffTime = WEOnTime + 1 ns,
	 * REOffTime = REOnTime + 4*SYSCLK + 26 ns (lut),
	 * CSOffTime = REOffTime + 1 ns
	 * ReadCycle = 2ns + 4*SYSCLK + 26 ns (lut),
	 * WriteCycle = 2*SYSCLK + 2 ns,
	 * CSPulseWidth = 10 ns */

	systim = 1000000000 / (sysclk / 1000);
	dev_dbg(blizzard.fbdev->dev,
		"Blizzard systim %lu ps extif_clk_period %u div %d\n",
		systim, blizzard.extif_clk_period, div);

	t = &blizzard.lut_timings;
	memset(t, 0, sizeof(*t));

	t->clk_div = div;

	t->cs_on_time = 0;
	t->we_on_time = round_to_extif_ticks(t->cs_on_time + 2000, div);
	t->re_on_time = round_to_extif_ticks(t->cs_on_time + 2000, div);
	t->access_time = round_to_extif_ticks(t->re_on_time + 4 * systim +
					      26000, div);
	t->we_off_time = round_to_extif_ticks(t->we_on_time + 1000, div);
	t->re_off_time = round_to_extif_ticks(t->re_on_time + 4 * systim +
					      26000, div);
	t->cs_off_time = round_to_extif_ticks(t->re_off_time + 1000, div);
	t->we_cycle_time = round_to_extif_ticks(2 * systim + 2000, div);
	if (t->we_cycle_time < t->we_off_time)
		t->we_cycle_time = t->we_off_time;
	t->re_cycle_time = round_to_extif_ticks(2000 + 4 * systim + 26000, div);
	if (t->re_cycle_time < t->re_off_time)
		t->re_cycle_time = t->re_off_time;
	t->cs_pulse_width = 0;

	dev_dbg(blizzard.fbdev->dev,
		 "[lut]cson %d csoff %d reon %d reoff %d\n",
		 t->cs_on_time, t->cs_off_time, t->re_on_time, t->re_off_time);
	dev_dbg(blizzard.fbdev->dev,
		 "[lut]weon %d weoff %d recyc %d wecyc %d\n",
		 t->we_on_time, t->we_off_time, t->re_cycle_time,
		 t->we_cycle_time);
	dev_dbg(blizzard.fbdev->dev, "[lut]rdaccess %d cspulse %d\n",
		 t->access_time, t->cs_pulse_width);

	return blizzard.extif->convert_timings(t);
}

static int calc_extif_timings(unsigned long sysclk, int *extif_mem_div)
{
	int max_clk_div;
	int div;

	blizzard.extif->get_clk_info(&blizzard.extif_clk_period, &max_clk_div);
	for (div = 1; div <= max_clk_div; div++) {
		if (calc_reg_timing(sysclk, div) == 0)
			break;
	}
	if (div > max_clk_div) {
		dev_dbg(blizzard.fbdev->dev, "reg timing failed\n");
		goto err;
	}
	*extif_mem_div = div;

	for (div = 1; div <= max_clk_div; div++) {
		if (calc_lut_timing(sysclk, div) == 0)
			break;
	}

	if (div > max_clk_div)
		goto err;

	blizzard.extif_clk_div = div;

	return 0;
err:
	dev_err(blizzard.fbdev->dev, "can't setup timings\n");
	return -1;
}

static void calc_blizzard_clk_rates(unsigned long ext_clk,
				unsigned long *sys_clk, unsigned long *pix_clk)
{
	int pix_clk_src;
	int sys_div = 0, sys_mul = 0;
	int pix_div;

	pix_clk_src = blizzard_read_reg(BLIZZARD_CLK_SRC);
	pix_div = ((pix_clk_src >> 3) & 0x1f) + 1;
	if ((pix_clk_src & (0x3 << 1)) == 0) {
		/* Source is the PLL */
		sys_div = (blizzard_read_reg(BLIZZARD_PLL_DIV) & 0x3f) + 1;
		sys_mul = blizzard_read_reg(BLIZZARD_PLL_CLOCK_SYNTH_0);
		sys_mul |= ((blizzard_read_reg(BLIZZARD_PLL_CLOCK_SYNTH_1)
				& 0x0f)	<< 11);
		*sys_clk = ext_clk * sys_mul / sys_div;
	} else	/* else source is ext clk, or oscillator */
		*sys_clk = ext_clk;

	*pix_clk = *sys_clk / pix_div;			/* HZ */
	dev_dbg(blizzard.fbdev->dev,
		"ext_clk %ld pix_src %d pix_div %d sys_div %d sys_mul %d\n",
		ext_clk, pix_clk_src & (0x3 << 1), pix_div, sys_div, sys_mul);
	dev_dbg(blizzard.fbdev->dev, "sys_clk %ld pix_clk %ld\n",
		*sys_clk, *pix_clk);
}

static int setup_tearsync(unsigned long pix_clk, int extif_div)
{
	int hdisp, vdisp;
	int hndp, vndp;
	int hsw, vsw;
	int hs, vs;
	int hs_pol_inv, vs_pol_inv;
	int use_hsvs, use_ndp;
	u8  b;

	hsw = blizzard_read_reg(BLIZZARD_HSW);
	vsw = blizzard_read_reg(BLIZZARD_VSW);
	hs_pol_inv = !(hsw & 0x80);
	vs_pol_inv = !(vsw & 0x80);
	hsw = hsw & 0x7f;
	vsw = vsw & 0x3f;

	hdisp = blizzard_read_reg(BLIZZARD_HDISP) * 8;
	vdisp = blizzard_read_reg(BLIZZARD_VDISP0) +
		((blizzard_read_reg(BLIZZARD_VDISP1) & 0x3) << 8);

	hndp = blizzard_read_reg(BLIZZARD_HNDP) & 0x3f;
	vndp = blizzard_read_reg(BLIZZARD_VNDP);

	/* time to transfer one pixel (16bpp) in ps */
	blizzard.pix_tx_time = blizzard.reg_timings.we_cycle_time;
	if (blizzard.extif->get_max_tx_rate != NULL) {
		/* The external interface might have a rate limitation,
		 * if so, we have to maximize our transfer rate.
		 */
		unsigned long min_tx_time;
		unsigned long max_tx_rate = blizzard.extif->get_max_tx_rate();

		dev_dbg(blizzard.fbdev->dev, "max_tx_rate %ld HZ\n",
			max_tx_rate);
		min_tx_time = 1000000000 / (max_tx_rate / 1000);  /* ps */
		if (blizzard.pix_tx_time < min_tx_time)
			blizzard.pix_tx_time = min_tx_time;
	}

	/* time to update one line in ps */
	blizzard.line_upd_time = (hdisp + hndp) * 1000000 / (pix_clk / 1000);
	blizzard.line_upd_time *= 1000;
	if (hdisp * blizzard.pix_tx_time > blizzard.line_upd_time)
		/* transfer speed too low, we might have to use both
		 * HS and VS */
		use_hsvs = 1;
	else
		/* decent transfer speed, we'll always use only VS */
		use_hsvs = 0;

	if (use_hsvs && (hs_pol_inv || vs_pol_inv)) {
		/* HS or'ed with VS doesn't work, use the active high
		 * TE signal based on HNDP / VNDP */
		use_ndp = 1;
		hs_pol_inv = 0;
		vs_pol_inv = 0;
		hs = hndp;
		vs = vndp;
	} else {
		/* Use HS or'ed with VS as a TE signal if both are needed
		 * or VNDP if only vsync is needed. */
		use_ndp = 0;
		hs = hsw;
		vs = vsw;
		if (!use_hsvs) {
			hs_pol_inv = 0;
			vs_pol_inv = 0;
		}
	}

	hs = hs * 1000000 / (pix_clk / 1000);		  /* ps */
	hs *= 1000;

	vs = vs * (hdisp + hndp) * 1000000 / (pix_clk / 1000); /* ps */
	vs *= 1000;

	if (vs <= hs)
		return -EDOM;
	/* set VS to 120% of HS to minimize VS detection time */
	vs = hs * 12 / 10;
	/* minimize HS too */
	if (hs > 10000)
		hs = 10000;

	b = blizzard_read_reg(BLIZZARD_NDISP_CTRL_STATUS);
	b &= ~0x3;
	b |= use_hsvs ? 1 : 0;
	b |= (use_ndp && use_hsvs) ? 0 : 2;
	blizzard_write_reg(BLIZZARD_NDISP_CTRL_STATUS, b);

	blizzard.vsync_only = !use_hsvs;

	dev_dbg(blizzard.fbdev->dev,
		"pix_clk %ld HZ pix_tx_time %ld ps line_upd_time %ld ps\n",
		pix_clk, blizzard.pix_tx_time, blizzard.line_upd_time);
	dev_dbg(blizzard.fbdev->dev,
		"hs %d ps vs %d ps mode %d vsync_only %d\n",
		hs, vs, b & 0x3, !use_hsvs);

	return blizzard.extif->setup_tearsync(1, hs, vs,
					      hs_pol_inv, vs_pol_inv,
					      extif_div);
}

static void blizzard_get_caps(int plane, struct omapfb_caps *caps)
{
	blizzard.int_ctrl->get_caps(plane, caps);
	caps->ctrl |= OMAPFB_CAPS_MANUAL_UPDATE |
		OMAPFB_CAPS_WINDOW_PIXEL_DOUBLE |
		OMAPFB_CAPS_WINDOW_SCALE |
		OMAPFB_CAPS_WINDOW_OVERLAY |
		OMAPFB_CAPS_WINDOW_ROTATE;
	if (blizzard.te_connected)
		caps->ctrl |= OMAPFB_CAPS_TEARSYNC;
	caps->wnd_color |= (1 << OMAPFB_COLOR_RGB565) |
			   (1 << OMAPFB_COLOR_YUV420);
}

static void _save_regs(struct blizzard_reg_list *list, int cnt)
{
	int i;

	for (i = 0; i < cnt; i++, list++) {
		int reg;
		for (reg = list->start; reg <= list->end; reg += 2)
			blizzard_reg_cache[reg / 2] = blizzard_read_reg(reg);
	}
}

static void _restore_regs(struct blizzard_reg_list *list, int cnt)
{
	int i;

	for (i = 0; i < cnt; i++, list++) {
		int reg;
		for (reg = list->start; reg <= list->end; reg += 2)
			blizzard_write_reg(reg, blizzard_reg_cache[reg / 2]);
	}
}

static void blizzard_save_all_regs(void)
{
	_save_regs(blizzard_pll_regs, ARRAY_SIZE(blizzard_pll_regs));
	_save_regs(blizzard_gen_regs, ARRAY_SIZE(blizzard_gen_regs));
}

static void blizzard_restore_pll_regs(void)
{
	_restore_regs(blizzard_pll_regs, ARRAY_SIZE(blizzard_pll_regs));
}

static void blizzard_restore_gen_regs(void)
{
	_restore_regs(blizzard_gen_regs, ARRAY_SIZE(blizzard_gen_regs));
}

static void blizzard_suspend(void)
{
	u32 l;
	unsigned long tmo;

	if (blizzard.last_color_mode) {
		update_full_screen();
		blizzard_sync();
	}
	blizzard.update_mode_before_suspend = blizzard.update_mode;
	/* the following will disable clocks as well */
	blizzard_set_update_mode(OMAPFB_UPDATE_DISABLED);

	blizzard_save_all_regs();

	blizzard_stop_sdram();

	l = blizzard_read_reg(BLIZZARD_POWER_SAVE);
	/* Standby, Sleep. We assume we use an external clock. */
	l |= 0x03;
	blizzard_write_reg(BLIZZARD_POWER_SAVE, l);

	tmo = jiffies + msecs_to_jiffies(100);
	while (!(blizzard_read_reg(BLIZZARD_PLL_MODE) & (1 << 1))) {
		if (time_after(jiffies, tmo)) {
			dev_err(blizzard.fbdev->dev,
				"s1d1374x: sleep timeout, stopping PLL manually\n");
			l = blizzard_read_reg(BLIZZARD_PLL_MODE);
			l &= ~0x03;
			/* Disable PLL, counter function */
			l |= 0x2;
			blizzard_write_reg(BLIZZARD_PLL_MODE, l);
			break;
		}
		msleep(1);
	}

	if (blizzard.power_down != NULL)
		blizzard.power_down(blizzard.fbdev->dev);
}

static void blizzard_resume(void)
{
	u32 l;

	if (blizzard.power_up != NULL)
		blizzard.power_up(blizzard.fbdev->dev);

	l = blizzard_read_reg(BLIZZARD_POWER_SAVE);
	/* Standby, Sleep */
	l &= ~0x03;
	blizzard_write_reg(BLIZZARD_POWER_SAVE, l);

	blizzard_restore_pll_regs();
	l = blizzard_read_reg(BLIZZARD_PLL_MODE);
	l &= ~0x03;
	/* Enable PLL, counter function */
	l |= 0x1;
	blizzard_write_reg(BLIZZARD_PLL_MODE, l);

	while (!(blizzard_read_reg(BLIZZARD_PLL_DIV) & (1 << 7)))
		msleep(1);

	blizzard_restart_sdram();

	blizzard_restore_gen_regs();

	/* Enable display */
	blizzard_write_reg(BLIZZARD_DISPLAY_MODE, 0x01);

	/* the following will enable clocks as necessary */
	blizzard_set_update_mode(blizzard.update_mode_before_suspend);

	/* Force a background update */
	blizzard.zoom_on = 1;
	update_full_screen();
	blizzard_sync();
}

static int blizzard_init(struct omapfb_device *fbdev, int ext_mode,
			 struct omapfb_mem_desc *req_vram)
{
	int r = 0, i;
	u8 rev, conf;
	unsigned long ext_clk;
	int extif_div;
	unsigned long sys_clk, pix_clk;
	struct omapfb_platform_data *omapfb_conf;
	struct blizzard_platform_data *ctrl_conf;

	blizzard.fbdev = fbdev;

	BUG_ON(!fbdev->ext_if || !fbdev->int_ctrl);

	blizzard.fbdev = fbdev;
	blizzard.extif = fbdev->ext_if;
	blizzard.int_ctrl = fbdev->int_ctrl;

	omapfb_conf = fbdev->dev->platform_data;
	ctrl_conf = omapfb_conf->ctrl_platform_data;
	if (ctrl_conf == NULL || ctrl_conf->get_clock_rate == NULL) {
		dev_err(fbdev->dev, "s1d1374x: missing platform data\n");
		r = -ENOENT;
		goto err1;
	}

	blizzard.power_down = ctrl_conf->power_down;
	blizzard.power_up = ctrl_conf->power_up;

	spin_lock_init(&blizzard.req_lock);

	if ((r = blizzard.int_ctrl->init(fbdev, 1, req_vram)) < 0)
		goto err1;

	if ((r = blizzard.extif->init(fbdev)) < 0)
		goto err2;

	blizzard_ctrl.set_color_key = blizzard.int_ctrl->set_color_key;
	blizzard_ctrl.get_color_key = blizzard.int_ctrl->get_color_key;
	blizzard_ctrl.setup_mem = blizzard.int_ctrl->setup_mem;
	blizzard_ctrl.mmap = blizzard.int_ctrl->mmap;

	ext_clk = ctrl_conf->get_clock_rate(fbdev->dev);
	if ((r = calc_extif_timings(ext_clk, &extif_div)) < 0)
		goto err3;

	set_extif_timings(&blizzard.reg_timings);

	if (blizzard.power_up != NULL)
		blizzard.power_up(fbdev->dev);

	calc_blizzard_clk_rates(ext_clk, &sys_clk, &pix_clk);

	if ((r = calc_extif_timings(sys_clk, &extif_div)) < 0)
		goto err3;
	set_extif_timings(&blizzard.reg_timings);

	if (!(blizzard_read_reg(BLIZZARD_PLL_DIV) & 0x80)) {
		dev_err(fbdev->dev,
			"controller not initialized by the bootloader\n");
		r = -ENODEV;
		goto err3;
	}

	if (ctrl_conf->te_connected) {
		if ((r = setup_tearsync(pix_clk, extif_div)) < 0)
			goto err3;
		blizzard.te_connected = 1;
	}

	rev = blizzard_read_reg(BLIZZARD_REV_CODE);
	conf = blizzard_read_reg(BLIZZARD_CONFIG);

	switch (rev & 0xfc) {
	case 0x9c:
		blizzard.version = BLIZZARD_VERSION_S1D13744;
		pr_info("omapfb: s1d13744 LCD controller rev %d "
			"initialized (CNF pins %x)\n", rev & 0x03, conf & 0x07);
		break;
	case 0xa4:
		blizzard.version = BLIZZARD_VERSION_S1D13745;
		pr_info("omapfb: s1d13745 LCD controller rev %d "
			"initialized (CNF pins %x)\n", rev & 0x03, conf & 0x07);
		break;
	default:
		dev_err(fbdev->dev, "invalid s1d1374x revision %02x\n",
			rev);
		r = -ENODEV;
		goto err3;
	}

	blizzard.max_transmit_size = blizzard.extif->max_transmit_size;

	blizzard.update_mode = OMAPFB_UPDATE_DISABLED;

	blizzard.auto_update_window.x = 0;
	blizzard.auto_update_window.y = 0;
	blizzard.auto_update_window.width = fbdev->panel->x_res;
	blizzard.auto_update_window.height = fbdev->panel->y_res;
	blizzard.auto_update_window.out_x = 0;
	blizzard.auto_update_window.out_x = 0;
	blizzard.auto_update_window.out_width = fbdev->panel->x_res;
	blizzard.auto_update_window.out_height = fbdev->panel->y_res;
	blizzard.auto_update_window.format = 0;

	blizzard.screen_width = fbdev->panel->x_res;
	blizzard.screen_height = fbdev->panel->y_res;

	init_timer(&blizzard.auto_update_timer);
	blizzard.auto_update_timer.function = blizzard_update_window_auto;
	blizzard.auto_update_timer.data = 0;

	INIT_LIST_HEAD(&blizzard.free_req_list);
	INIT_LIST_HEAD(&blizzard.pending_req_list);
	for (i = 0; i < ARRAY_SIZE(blizzard.req_pool); i++)
		list_add(&blizzard.req_pool[i].entry, &blizzard.free_req_list);
	BUG_ON(i <= IRQ_REQ_POOL_SIZE);
	sema_init(&blizzard.req_sema, i - IRQ_REQ_POOL_SIZE);

	return 0;
err3:
	if (blizzard.power_down != NULL)
		blizzard.power_down(fbdev->dev);
	blizzard.extif->cleanup();
err2:
	blizzard.int_ctrl->cleanup();
err1:
	return r;
}

static void blizzard_cleanup(void)
{
	blizzard_set_update_mode(OMAPFB_UPDATE_DISABLED);
	blizzard.extif->cleanup();
	blizzard.int_ctrl->cleanup();
	if (blizzard.power_down != NULL)
		blizzard.power_down(blizzard.fbdev->dev);
}

struct lcd_ctrl blizzard_ctrl = {
	.name			= "blizzard",
	.init			= blizzard_init,
	.cleanup		= blizzard_cleanup,
	.bind_client		= blizzard_bind_client,
	.get_caps		= blizzard_get_caps,
	.set_update_mode	= blizzard_set_update_mode,
	.get_update_mode	= blizzard_get_update_mode,
	.setup_plane		= blizzard_setup_plane,
	.set_scale		= blizzard_set_scale,
	.enable_plane		= blizzard_enable_plane,
	.set_rotate		= blizzard_set_rotate,
	.update_window		= blizzard_update_window_async,
	.sync			= blizzard_sync,
	.suspend		= blizzard_suspend,
	.resume			= blizzard_resume,
};

