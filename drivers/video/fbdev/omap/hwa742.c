// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Epson HWA742 LCD controller driver
 *
 * Copyright (C) 2004-2005 Nokia Corporation
 * Authors:     Juha Yrjölä   <juha.yrjola@nokia.com>
 *	        Imre Deak     <imre.deak@nokia.com>
 * YUV support: Jussi Laako   <jussi.laako@nokia.com>
 */
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/interrupt.h>

#include "omapfb.h"

#define HWA742_REV_CODE_REG       0x0
#define HWA742_CONFIG_REG         0x2
#define HWA742_PLL_DIV_REG        0x4
#define HWA742_PLL_0_REG          0x6
#define HWA742_PLL_1_REG          0x8
#define HWA742_PLL_2_REG          0xa
#define HWA742_PLL_3_REG          0xc
#define HWA742_PLL_4_REG          0xe
#define HWA742_CLK_SRC_REG        0x12
#define HWA742_PANEL_TYPE_REG     0x14
#define HWA742_H_DISP_REG         0x16
#define HWA742_H_NDP_REG          0x18
#define HWA742_V_DISP_1_REG       0x1a
#define HWA742_V_DISP_2_REG       0x1c
#define HWA742_V_NDP_REG          0x1e
#define HWA742_HS_W_REG           0x20
#define HWA742_HP_S_REG           0x22
#define HWA742_VS_W_REG           0x24
#define HWA742_VP_S_REG           0x26
#define HWA742_PCLK_POL_REG       0x28
#define HWA742_INPUT_MODE_REG     0x2a
#define HWA742_TRANSL_MODE_REG1   0x2e
#define HWA742_DISP_MODE_REG      0x34
#define HWA742_WINDOW_TYPE        0x36
#define HWA742_WINDOW_X_START_0   0x38
#define HWA742_WINDOW_X_START_1   0x3a
#define HWA742_WINDOW_Y_START_0   0x3c
#define HWA742_WINDOW_Y_START_1   0x3e
#define HWA742_WINDOW_X_END_0     0x40
#define HWA742_WINDOW_X_END_1     0x42
#define HWA742_WINDOW_Y_END_0     0x44
#define HWA742_WINDOW_Y_END_1     0x46
#define HWA742_MEMORY_WRITE_LSB   0x48
#define HWA742_MEMORY_WRITE_MSB   0x49
#define HWA742_MEMORY_READ_0      0x4a
#define HWA742_MEMORY_READ_1      0x4c
#define HWA742_MEMORY_READ_2      0x4e
#define HWA742_POWER_SAVE         0x56
#define HWA742_NDP_CTRL           0x58

#define HWA742_AUTO_UPDATE_TIME		(HZ / 20)

/* Reserve 4 request slots for requests in irq context */
#define REQ_POOL_SIZE			24
#define IRQ_REQ_POOL_SIZE		4

#define REQ_FROM_IRQ_POOL 0x01

#define REQ_COMPLETE	0
#define REQ_PENDING	1

struct update_param {
	int	x, y, width, height;
	int	color_mode;
	int	flags;
};

struct hwa742_request {
	struct list_head entry;
	unsigned int	 flags;

	int		 (*handler)(struct hwa742_request *req);
	void		 (*complete)(void *data);
	void		 *complete_data;

	union {
		struct update_param	update;
		struct completion	*sync;
	} par;
};

struct {
	enum omapfb_update_mode	update_mode;
	enum omapfb_update_mode	update_mode_before_suspend;

	struct timer_list	auto_update_timer;
	int			stop_auto_update;
	struct omapfb_update_window	auto_update_window;
	unsigned		te_connected:1;
	unsigned		vsync_only:1;

	struct hwa742_request	req_pool[REQ_POOL_SIZE];
	struct list_head	pending_req_list;
	struct list_head	free_req_list;

	/*
	 * @req_lock: protect request slots pool and its tracking lists
	 * @req_sema: counter; slot allocators from task contexts must
	 *            push it down before acquiring a slot. This
	 *            guarantees that atomic contexts will always have
	 *            a minimum of IRQ_REQ_POOL_SIZE slots available.
	 */
	struct semaphore	req_sema;
	spinlock_t		req_lock;

	struct extif_timings	reg_timings, lut_timings;

	int			prev_color_mode;
	int			prev_flags;
	int			window_type;

	u32			max_transmit_size;
	u32			extif_clk_period;
	unsigned long		pix_tx_time;
	unsigned long		line_upd_time;


	struct omapfb_device	*fbdev;
	struct lcd_ctrl_extif	*extif;
	const struct lcd_ctrl	*int_ctrl;

	struct clk		*sys_ck;
} hwa742;

struct lcd_ctrl hwa742_ctrl;

static u8 hwa742_read_reg(u8 reg)
{
	u8 data;

	hwa742.extif->set_bits_per_cycle(8);
	hwa742.extif->write_command(&reg, 1);
	hwa742.extif->read_data(&data, 1);

	return data;
}

static void hwa742_write_reg(u8 reg, u8 data)
{
	hwa742.extif->set_bits_per_cycle(8);
	hwa742.extif->write_command(&reg, 1);
	hwa742.extif->write_data(&data, 1);
}

static void set_window_regs(int x_start, int y_start, int x_end, int y_end)
{
	u8 tmp[8];
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

	hwa742.extif->set_bits_per_cycle(8);
	cmd = HWA742_WINDOW_X_START_0;

	hwa742.extif->write_command(&cmd, 1);

	hwa742.extif->write_data(tmp, 8);
}

static void set_format_regs(int conv, int transl, int flags)
{
	if (flags & OMAPFB_FORMAT_FLAG_DOUBLE) {
		hwa742.window_type = ((hwa742.window_type & 0xfc) | 0x01);
#ifdef VERBOSE
		dev_dbg(hwa742.fbdev->dev, "hwa742: enabled pixel doubling\n");
#endif
	} else {
		hwa742.window_type = (hwa742.window_type & 0xfc);
#ifdef VERBOSE
		dev_dbg(hwa742.fbdev->dev, "hwa742: disabled pixel doubling\n");
#endif
	}

	hwa742_write_reg(HWA742_INPUT_MODE_REG, conv);
	hwa742_write_reg(HWA742_TRANSL_MODE_REG1, transl);
	hwa742_write_reg(HWA742_WINDOW_TYPE, hwa742.window_type);
}

static void enable_tearsync(int y, int width, int height, int screen_height,
			    int force_vsync)
{
	u8 b;

	b = hwa742_read_reg(HWA742_NDP_CTRL);
	b |= 1 << 2;
	hwa742_write_reg(HWA742_NDP_CTRL, b);

	if (likely(hwa742.vsync_only || force_vsync)) {
		hwa742.extif->enable_tearsync(1, 0);
		return;
	}

	if (width * hwa742.pix_tx_time < hwa742.line_upd_time) {
		hwa742.extif->enable_tearsync(1, 0);
		return;
	}

	if ((width * hwa742.pix_tx_time / 1000) * height <
	    (y + height) * (hwa742.line_upd_time / 1000)) {
		hwa742.extif->enable_tearsync(1, 0);
		return;
	}

	hwa742.extif->enable_tearsync(1, y + 1);
}

static void disable_tearsync(void)
{
	u8 b;

	hwa742.extif->enable_tearsync(0, 0);

	b = hwa742_read_reg(HWA742_NDP_CTRL);
	b &= ~(1 << 2);
	hwa742_write_reg(HWA742_NDP_CTRL, b);
}

static inline struct hwa742_request *alloc_req(bool can_sleep)
{
	unsigned long flags;
	struct hwa742_request *req;
	int req_flags = 0;

	if (can_sleep)
		down(&hwa742.req_sema);
	else
		req_flags = REQ_FROM_IRQ_POOL;

	spin_lock_irqsave(&hwa742.req_lock, flags);
	BUG_ON(list_empty(&hwa742.free_req_list));
	req = list_entry(hwa742.free_req_list.next,
			 struct hwa742_request, entry);
	list_del(&req->entry);
	spin_unlock_irqrestore(&hwa742.req_lock, flags);

	INIT_LIST_HEAD(&req->entry);
	req->flags = req_flags;

	return req;
}

static inline void free_req(struct hwa742_request *req)
{
	unsigned long flags;

	spin_lock_irqsave(&hwa742.req_lock, flags);

	list_move(&req->entry, &hwa742.free_req_list);
	if (!(req->flags & REQ_FROM_IRQ_POOL))
		up(&hwa742.req_sema);

	spin_unlock_irqrestore(&hwa742.req_lock, flags);
}

static void process_pending_requests(void)
{
	unsigned long flags;

	spin_lock_irqsave(&hwa742.req_lock, flags);

	while (!list_empty(&hwa742.pending_req_list)) {
		struct hwa742_request *req;
		void (*complete)(void *);
		void *complete_data;

		req = list_entry(hwa742.pending_req_list.next,
				 struct hwa742_request, entry);
		spin_unlock_irqrestore(&hwa742.req_lock, flags);

		if (req->handler(req) == REQ_PENDING)
			return;

		complete = req->complete;
		complete_data = req->complete_data;
		free_req(req);

		if (complete)
			complete(complete_data);

		spin_lock_irqsave(&hwa742.req_lock, flags);
	}

	spin_unlock_irqrestore(&hwa742.req_lock, flags);
}

static void submit_req_list(struct list_head *head)
{
	unsigned long flags;
	int process = 1;

	spin_lock_irqsave(&hwa742.req_lock, flags);
	if (likely(!list_empty(&hwa742.pending_req_list)))
		process = 0;
	list_splice_init(head, hwa742.pending_req_list.prev);
	spin_unlock_irqrestore(&hwa742.req_lock, flags);

	if (process)
		process_pending_requests();
}

static void request_complete(void *data)
{
	struct hwa742_request	*req = (struct hwa742_request *)data;
	void			(*complete)(void *);
	void			*complete_data;

	complete = req->complete;
	complete_data = req->complete_data;

	free_req(req);

	if (complete)
		complete(complete_data);

	process_pending_requests();
}

static int send_frame_handler(struct hwa742_request *req)
{
	struct update_param *par = &req->par.update;
	int x = par->x;
	int y = par->y;
	int w = par->width;
	int h = par->height;
	int bpp;
	int conv, transl;
	unsigned long offset;
	int color_mode = par->color_mode;
	int flags = par->flags;
	int scr_width = hwa742.fbdev->panel->x_res;
	int scr_height = hwa742.fbdev->panel->y_res;

#ifdef VERBOSE
	dev_dbg(hwa742.fbdev->dev, "x %d y %d w %d h %d scr_width %d "
		"color_mode %d flags %d\n",
		x, y, w, h, scr_width, color_mode, flags);
#endif

	switch (color_mode) {
	case OMAPFB_COLOR_YUV422:
		bpp = 16;
		conv = 0x08;
		transl = 0x25;
		break;
	case OMAPFB_COLOR_YUV420:
		bpp = 12;
		conv = 0x09;
		transl = 0x25;
		break;
	case OMAPFB_COLOR_RGB565:
		bpp = 16;
		conv = 0x01;
		transl = 0x05;
		break;
	default:
		return -EINVAL;
	}

	if (hwa742.prev_flags != flags ||
	    hwa742.prev_color_mode != color_mode) {
		set_format_regs(conv, transl, flags);
		hwa742.prev_color_mode = color_mode;
		hwa742.prev_flags = flags;
	}
	flags = req->par.update.flags;
	if (flags & OMAPFB_FORMAT_FLAG_TEARSYNC)
		enable_tearsync(y, scr_width, h, scr_height,
				flags & OMAPFB_FORMAT_FLAG_FORCE_VSYNC);
	else
		disable_tearsync();

	set_window_regs(x, y, x + w, y + h);

	offset = (scr_width * y + x) * bpp / 8;

	hwa742.int_ctrl->setup_plane(OMAPFB_PLANE_GFX,
			OMAPFB_CHANNEL_OUT_LCD, offset, scr_width, 0, 0, w, h,
			color_mode);

	hwa742.extif->set_bits_per_cycle(16);

	hwa742.int_ctrl->enable_plane(OMAPFB_PLANE_GFX, 1);
	hwa742.extif->transfer_area(w, h, request_complete, req);

	return REQ_PENDING;
}

static void send_frame_complete(void *data)
{
	hwa742.int_ctrl->enable_plane(OMAPFB_PLANE_GFX, 0);
}

#define ADD_PREQ(_x, _y, _w, _h, can_sleep) do {\
	req = alloc_req(can_sleep);		\
	req->handler	= send_frame_handler;	\
	req->complete	= send_frame_complete;	\
	req->par.update.x = _x;			\
	req->par.update.y = _y;			\
	req->par.update.width  = _w;		\
	req->par.update.height = _h;		\
	req->par.update.color_mode = color_mode;\
	req->par.update.flags	  = flags;	\
	list_add_tail(&req->entry, req_head);	\
} while(0)

static void create_req_list(struct omapfb_update_window *win,
			    struct list_head *req_head,
			    bool can_sleep)
{
	struct hwa742_request *req;
	int x = win->x;
	int y = win->y;
	int width = win->width;
	int height = win->height;
	int color_mode;
	int flags;

	flags = win->format & ~OMAPFB_FORMAT_MASK;
	color_mode = win->format & OMAPFB_FORMAT_MASK;

	if (x & 1) {
		ADD_PREQ(x, y, 1, height, can_sleep);
		width--;
		x++;
		flags &= ~OMAPFB_FORMAT_FLAG_TEARSYNC;
	}
	if (width & ~1) {
		unsigned int xspan = width & ~1;
		unsigned int ystart = y;
		unsigned int yspan = height;

		if (xspan * height * 2 > hwa742.max_transmit_size) {
			yspan = hwa742.max_transmit_size / (xspan * 2);
			ADD_PREQ(x, ystart, xspan, yspan, can_sleep);
			ystart += yspan;
			yspan = height - yspan;
			flags &= ~OMAPFB_FORMAT_FLAG_TEARSYNC;
		}

		ADD_PREQ(x, ystart, xspan, yspan, can_sleep);
		x += xspan;
		width -= xspan;
		flags &= ~OMAPFB_FORMAT_FLAG_TEARSYNC;
	}
	if (width)
		ADD_PREQ(x, y, 1, height, can_sleep);
}

static void auto_update_complete(void *data)
{
	if (!hwa742.stop_auto_update)
		mod_timer(&hwa742.auto_update_timer,
			  jiffies + HWA742_AUTO_UPDATE_TIME);
}

static void __hwa742_update_window_auto(bool can_sleep)
{
	LIST_HEAD(req_list);
	struct hwa742_request *last;

	create_req_list(&hwa742.auto_update_window, &req_list, can_sleep);
	last = list_entry(req_list.prev, struct hwa742_request, entry);

	last->complete = auto_update_complete;
	last->complete_data = NULL;

	submit_req_list(&req_list);
}

static void hwa742_update_window_auto(struct timer_list *unused)
{
	__hwa742_update_window_auto(false);
}

static int hwa742_update_window_async(struct fb_info *fbi,
				 struct omapfb_update_window *win,
				 void (*complete_callback)(void *arg),
				 void *complete_callback_data)
{
	LIST_HEAD(req_list);
	struct hwa742_request *last;
	int r = 0;

	if (hwa742.update_mode != OMAPFB_MANUAL_UPDATE) {
		dev_dbg(hwa742.fbdev->dev, "invalid update mode\n");
		r = -EINVAL;
		goto out;
	}
	if (unlikely(win->format &
	    ~(0x03 | OMAPFB_FORMAT_FLAG_DOUBLE |
	    OMAPFB_FORMAT_FLAG_TEARSYNC | OMAPFB_FORMAT_FLAG_FORCE_VSYNC))) {
		dev_dbg(hwa742.fbdev->dev, "invalid window flag\n");
		r = -EINVAL;
		goto out;
	}

	create_req_list(win, &req_list, true);
	last = list_entry(req_list.prev, struct hwa742_request, entry);

	last->complete = complete_callback;
	last->complete_data = (void *)complete_callback_data;

	submit_req_list(&req_list);

out:
	return r;
}

static int hwa742_setup_plane(int plane, int channel_out,
				  unsigned long offset, int screen_width,
				  int pos_x, int pos_y, int width, int height,
				  int color_mode)
{
	if (plane != OMAPFB_PLANE_GFX ||
	    channel_out != OMAPFB_CHANNEL_OUT_LCD)
		return -EINVAL;

	return 0;
}

static int hwa742_enable_plane(int plane, int enable)
{
	if (plane != 0)
		return -EINVAL;

	hwa742.int_ctrl->enable_plane(plane, enable);

	return 0;
}

static int sync_handler(struct hwa742_request *req)
{
	complete(req->par.sync);
	return REQ_COMPLETE;
}

static void hwa742_sync(void)
{
	LIST_HEAD(req_list);
	struct hwa742_request *req;
	struct completion comp;

	req = alloc_req(true);

	req->handler = sync_handler;
	req->complete = NULL;
	init_completion(&comp);
	req->par.sync = &comp;

	list_add(&req->entry, &req_list);
	submit_req_list(&req_list);

	wait_for_completion(&comp);
}

static void hwa742_bind_client(struct omapfb_notifier_block *nb)
{
	dev_dbg(hwa742.fbdev->dev, "update_mode %d\n", hwa742.update_mode);
	if (hwa742.update_mode == OMAPFB_MANUAL_UPDATE) {
		omapfb_notify_clients(hwa742.fbdev, OMAPFB_EVENT_READY);
	}
}

static int hwa742_set_update_mode(enum omapfb_update_mode mode)
{
	if (mode != OMAPFB_MANUAL_UPDATE && mode != OMAPFB_AUTO_UPDATE &&
	    mode != OMAPFB_UPDATE_DISABLED)
		return -EINVAL;

	if (mode == hwa742.update_mode)
		return 0;

	dev_info(hwa742.fbdev->dev, "HWA742: setting update mode to %s\n",
			mode == OMAPFB_UPDATE_DISABLED ? "disabled" :
			(mode == OMAPFB_AUTO_UPDATE ? "auto" : "manual"));

	switch (hwa742.update_mode) {
	case OMAPFB_MANUAL_UPDATE:
		omapfb_notify_clients(hwa742.fbdev, OMAPFB_EVENT_DISABLED);
		break;
	case OMAPFB_AUTO_UPDATE:
		hwa742.stop_auto_update = 1;
		del_timer_sync(&hwa742.auto_update_timer);
		break;
	case OMAPFB_UPDATE_DISABLED:
		break;
	}

	hwa742.update_mode = mode;
	hwa742_sync();
	hwa742.stop_auto_update = 0;

	switch (mode) {
	case OMAPFB_MANUAL_UPDATE:
		omapfb_notify_clients(hwa742.fbdev, OMAPFB_EVENT_READY);
		break;
	case OMAPFB_AUTO_UPDATE:
		__hwa742_update_window_auto(true);
		break;
	case OMAPFB_UPDATE_DISABLED:
		break;
	}

	return 0;
}

static enum omapfb_update_mode hwa742_get_update_mode(void)
{
	return hwa742.update_mode;
}

static unsigned long round_to_extif_ticks(unsigned long ps, int div)
{
	int bus_tick = hwa742.extif_clk_period * div;
	return (ps + bus_tick - 1) / bus_tick * bus_tick;
}

static int calc_reg_timing(unsigned long sysclk, int div)
{
	struct extif_timings *t;
	unsigned long systim;

	/* CSOnTime 0, WEOnTime 2 ns, REOnTime 2 ns,
	 * AccessTime 2 ns + 12.2 ns (regs),
	 * WEOffTime = WEOnTime + 1 ns,
	 * REOffTime = REOnTime + 16 ns (regs),
	 * CSOffTime = REOffTime + 1 ns
	 * ReadCycle = 2ns + 2*SYSCLK  (regs),
	 * WriteCycle = 2*SYSCLK + 2 ns,
	 * CSPulseWidth = 10 ns */
	systim = 1000000000 / (sysclk / 1000);
	dev_dbg(hwa742.fbdev->dev, "HWA742 systim %lu ps extif_clk_period %u ps"
		  "extif_clk_div %d\n", systim, hwa742.extif_clk_period, div);

	t = &hwa742.reg_timings;
	memset(t, 0, sizeof(*t));
	t->clk_div = div;
	t->cs_on_time = 0;
	t->we_on_time = round_to_extif_ticks(t->cs_on_time + 2000, div);
	t->re_on_time = round_to_extif_ticks(t->cs_on_time + 2000, div);
	t->access_time = round_to_extif_ticks(t->re_on_time + 12200, div);
	t->we_off_time = round_to_extif_ticks(t->we_on_time + 1000, div);
	t->re_off_time = round_to_extif_ticks(t->re_on_time + 16000, div);
	t->cs_off_time = round_to_extif_ticks(t->re_off_time + 1000, div);
	t->we_cycle_time = round_to_extif_ticks(2 * systim + 2000, div);
	if (t->we_cycle_time < t->we_off_time)
		t->we_cycle_time = t->we_off_time;
	t->re_cycle_time = round_to_extif_ticks(2 * systim + 2000, div);
	if (t->re_cycle_time < t->re_off_time)
		t->re_cycle_time = t->re_off_time;
	t->cs_pulse_width = 0;

	dev_dbg(hwa742.fbdev->dev, "[reg]cson %d csoff %d reon %d reoff %d\n",
		 t->cs_on_time, t->cs_off_time, t->re_on_time, t->re_off_time);
	dev_dbg(hwa742.fbdev->dev, "[reg]weon %d weoff %d recyc %d wecyc %d\n",
		 t->we_on_time, t->we_off_time, t->re_cycle_time,
		 t->we_cycle_time);
	dev_dbg(hwa742.fbdev->dev, "[reg]rdaccess %d cspulse %d\n",
		 t->access_time, t->cs_pulse_width);

	return hwa742.extif->convert_timings(t);
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
	 * CSPulseWidth = 10 ns
	 */
	systim = 1000000000 / (sysclk / 1000);
	dev_dbg(hwa742.fbdev->dev, "HWA742 systim %lu ps extif_clk_period %u ps"
		  "extif_clk_div %d\n", systim, hwa742.extif_clk_period, div);

	t = &hwa742.lut_timings;
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

	dev_dbg(hwa742.fbdev->dev, "[lut]cson %d csoff %d reon %d reoff %d\n",
		 t->cs_on_time, t->cs_off_time, t->re_on_time, t->re_off_time);
	dev_dbg(hwa742.fbdev->dev, "[lut]weon %d weoff %d recyc %d wecyc %d\n",
		 t->we_on_time, t->we_off_time, t->re_cycle_time,
		 t->we_cycle_time);
	dev_dbg(hwa742.fbdev->dev, "[lut]rdaccess %d cspulse %d\n",
		 t->access_time, t->cs_pulse_width);

	return hwa742.extif->convert_timings(t);
}

static int calc_extif_timings(unsigned long sysclk, int *extif_mem_div)
{
	int max_clk_div;
	int div;

	hwa742.extif->get_clk_info(&hwa742.extif_clk_period, &max_clk_div);
	for (div = 1; div < max_clk_div; div++) {
		if (calc_reg_timing(sysclk, div) == 0)
			break;
	}
	if (div >= max_clk_div)
		goto err;

	*extif_mem_div = div;

	for (div = 1; div < max_clk_div; div++) {
		if (calc_lut_timing(sysclk, div) == 0)
			break;
	}

	if (div >= max_clk_div)
		goto err;

	return 0;

err:
	dev_err(hwa742.fbdev->dev, "can't setup timings\n");
	return -1;
}

static void calc_hwa742_clk_rates(unsigned long ext_clk,
				unsigned long *sys_clk, unsigned long *pix_clk)
{
	int pix_clk_src;
	int sys_div = 0, sys_mul = 0;
	int pix_div;

	pix_clk_src = hwa742_read_reg(HWA742_CLK_SRC_REG);
	pix_div = ((pix_clk_src >> 3) & 0x1f) + 1;
	if ((pix_clk_src & (0x3 << 1)) == 0) {
		/* Source is the PLL */
		sys_div = (hwa742_read_reg(HWA742_PLL_DIV_REG) & 0x3f) + 1;
		sys_mul = (hwa742_read_reg(HWA742_PLL_4_REG) & 0x7f) + 1;
		*sys_clk = ext_clk * sys_mul / sys_div;
	} else	/* else source is ext clk, or oscillator */
		*sys_clk = ext_clk;

	*pix_clk = *sys_clk / pix_div;			/* HZ */
	dev_dbg(hwa742.fbdev->dev,
		"ext_clk %ld pix_src %d pix_div %d sys_div %d sys_mul %d\n",
		ext_clk, pix_clk_src & (0x3 << 1), pix_div, sys_div, sys_mul);
	dev_dbg(hwa742.fbdev->dev, "sys_clk %ld pix_clk %ld\n",
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

	hsw = hwa742_read_reg(HWA742_HS_W_REG);
	vsw = hwa742_read_reg(HWA742_VS_W_REG);
	hs_pol_inv = !(hsw & 0x80);
	vs_pol_inv = !(vsw & 0x80);
	hsw = hsw & 0x7f;
	vsw = vsw & 0x3f;

	hdisp = (hwa742_read_reg(HWA742_H_DISP_REG) & 0x7f) * 8;
	vdisp = hwa742_read_reg(HWA742_V_DISP_1_REG) +
		((hwa742_read_reg(HWA742_V_DISP_2_REG) & 0x3) << 8);

	hndp = hwa742_read_reg(HWA742_H_NDP_REG) & 0x7f;
	vndp = hwa742_read_reg(HWA742_V_NDP_REG);

	/* time to transfer one pixel (16bpp) in ps */
	hwa742.pix_tx_time = hwa742.reg_timings.we_cycle_time;
	if (hwa742.extif->get_max_tx_rate != NULL) {
		/*
		 * The external interface might have a rate limitation,
		 * if so, we have to maximize our transfer rate.
		 */
		unsigned long min_tx_time;
		unsigned long max_tx_rate = hwa742.extif->get_max_tx_rate();

		dev_dbg(hwa742.fbdev->dev, "max_tx_rate %ld HZ\n",
			max_tx_rate);
		min_tx_time = 1000000000 / (max_tx_rate / 1000);  /* ps */
		if (hwa742.pix_tx_time < min_tx_time)
			hwa742.pix_tx_time = min_tx_time;
	}

	/* time to update one line in ps */
	hwa742.line_upd_time = (hdisp + hndp) * 1000000 / (pix_clk / 1000);
	hwa742.line_upd_time *= 1000;
	if (hdisp * hwa742.pix_tx_time > hwa742.line_upd_time)
		/*
		 * transfer speed too low, we might have to use both
		 * HS and VS
		 */
		use_hsvs = 1;
	else
		/* decent transfer speed, we'll always use only VS */
		use_hsvs = 0;

	if (use_hsvs && (hs_pol_inv || vs_pol_inv)) {
		/*
		 * HS or'ed with VS doesn't work, use the active high
		 * TE signal based on HNDP / VNDP
		 */
		use_ndp = 1;
		hs_pol_inv = 0;
		vs_pol_inv = 0;
		hs = hndp;
		vs = vndp;
	} else {
		/*
		 * Use HS or'ed with VS as a TE signal if both are needed
		 * or VNDP if only vsync is needed.
		 */
		use_ndp = 0;
		hs = hsw;
		vs = vsw;
		if (!use_hsvs) {
			hs_pol_inv = 0;
			vs_pol_inv = 0;
		}
	}

	hs = hs * 1000000 / (pix_clk / 1000);			/* ps */
	hs *= 1000;

	vs = vs * (hdisp + hndp) * 1000000 / (pix_clk / 1000);	/* ps */
	vs *= 1000;

	if (vs <= hs)
		return -EDOM;
	/* set VS to 120% of HS to minimize VS detection time */
	vs = hs * 12 / 10;
	/* minimize HS too */
	hs = 10000;

	b = hwa742_read_reg(HWA742_NDP_CTRL);
	b &= ~0x3;
	b |= use_hsvs ? 1 : 0;
	b |= (use_ndp && use_hsvs) ? 0 : 2;
	hwa742_write_reg(HWA742_NDP_CTRL, b);

	hwa742.vsync_only = !use_hsvs;

	dev_dbg(hwa742.fbdev->dev,
		"pix_clk %ld HZ pix_tx_time %ld ps line_upd_time %ld ps\n",
		pix_clk, hwa742.pix_tx_time, hwa742.line_upd_time);
	dev_dbg(hwa742.fbdev->dev,
		"hs %d ps vs %d ps mode %d vsync_only %d\n",
		hs, vs, (b & 0x3), !use_hsvs);

	return hwa742.extif->setup_tearsync(1, hs, vs,
					    hs_pol_inv, vs_pol_inv, extif_div);
}

static void hwa742_get_caps(int plane, struct omapfb_caps *caps)
{
	hwa742.int_ctrl->get_caps(plane, caps);
	caps->ctrl |= OMAPFB_CAPS_MANUAL_UPDATE |
		      OMAPFB_CAPS_WINDOW_PIXEL_DOUBLE;
	if (hwa742.te_connected)
		caps->ctrl |= OMAPFB_CAPS_TEARSYNC;
	caps->wnd_color |= (1 << OMAPFB_COLOR_RGB565) |
			   (1 << OMAPFB_COLOR_YUV420);
}

static void hwa742_suspend(void)
{
	hwa742.update_mode_before_suspend = hwa742.update_mode;
	hwa742_set_update_mode(OMAPFB_UPDATE_DISABLED);
	/* Enable sleep mode */
	hwa742_write_reg(HWA742_POWER_SAVE, 1 << 1);
	clk_disable(hwa742.sys_ck);
}

static void hwa742_resume(void)
{
	clk_enable(hwa742.sys_ck);

	/* Disable sleep mode */
	hwa742_write_reg(HWA742_POWER_SAVE, 0);
	while (1) {
		/* Loop until PLL output is stabilized */
		if (hwa742_read_reg(HWA742_PLL_DIV_REG) & (1 << 7))
			break;
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(5));
	}
	hwa742_set_update_mode(hwa742.update_mode_before_suspend);
}

static int hwa742_init(struct omapfb_device *fbdev, int ext_mode,
		       struct omapfb_mem_desc *req_vram)
{
	int r = 0, i;
	u8 rev, conf;
	unsigned long ext_clk;
	unsigned long sys_clk, pix_clk;
	int extif_mem_div;
	struct omapfb_platform_data *omapfb_conf;

	BUG_ON(!fbdev->ext_if || !fbdev->int_ctrl);

	hwa742.fbdev = fbdev;
	hwa742.extif = fbdev->ext_if;
	hwa742.int_ctrl = fbdev->int_ctrl;

	omapfb_conf = dev_get_platdata(fbdev->dev);

	hwa742.sys_ck = clk_get(NULL, "hwa_sys_ck");

	spin_lock_init(&hwa742.req_lock);

	if ((r = hwa742.int_ctrl->init(fbdev, 1, req_vram)) < 0)
		goto err1;

	if ((r = hwa742.extif->init(fbdev)) < 0)
		goto err2;

	ext_clk = clk_get_rate(hwa742.sys_ck);
	if ((r = calc_extif_timings(ext_clk, &extif_mem_div)) < 0)
		goto err3;
	hwa742.extif->set_timings(&hwa742.reg_timings);
	clk_prepare_enable(hwa742.sys_ck);

	calc_hwa742_clk_rates(ext_clk, &sys_clk, &pix_clk);
	if ((r = calc_extif_timings(sys_clk, &extif_mem_div)) < 0)
		goto err4;
	hwa742.extif->set_timings(&hwa742.reg_timings);

	rev = hwa742_read_reg(HWA742_REV_CODE_REG);
	if ((rev & 0xfc) != 0x80) {
		dev_err(fbdev->dev, "HWA742: invalid revision %02x\n", rev);
		r = -ENODEV;
		goto err4;
	}


	if (!(hwa742_read_reg(HWA742_PLL_DIV_REG) & 0x80)) {
		dev_err(fbdev->dev,
		      "HWA742: controller not initialized by the bootloader\n");
		r = -ENODEV;
		goto err4;
	}

	if ((r = setup_tearsync(pix_clk, extif_mem_div)) < 0) {
		dev_err(hwa742.fbdev->dev,
			"HWA742: can't setup tearing synchronization\n");
		goto err4;
	}
	hwa742.te_connected = 1;

	hwa742.max_transmit_size = hwa742.extif->max_transmit_size;

	hwa742.update_mode = OMAPFB_UPDATE_DISABLED;

	hwa742.auto_update_window.x = 0;
	hwa742.auto_update_window.y = 0;
	hwa742.auto_update_window.width = fbdev->panel->x_res;
	hwa742.auto_update_window.height = fbdev->panel->y_res;
	hwa742.auto_update_window.format = 0;

	timer_setup(&hwa742.auto_update_timer, hwa742_update_window_auto, 0);

	hwa742.prev_color_mode = -1;
	hwa742.prev_flags = 0;

	hwa742.fbdev = fbdev;

	INIT_LIST_HEAD(&hwa742.free_req_list);
	INIT_LIST_HEAD(&hwa742.pending_req_list);
	for (i = 0; i < ARRAY_SIZE(hwa742.req_pool); i++)
		list_add(&hwa742.req_pool[i].entry, &hwa742.free_req_list);
	BUG_ON(i <= IRQ_REQ_POOL_SIZE);
	sema_init(&hwa742.req_sema, i - IRQ_REQ_POOL_SIZE);

	conf = hwa742_read_reg(HWA742_CONFIG_REG);
	dev_info(fbdev->dev, ": Epson HWA742 LCD controller rev %d "
			"initialized (CNF pins %x)\n", rev & 0x03, conf & 0x07);

	return 0;
err4:
	clk_disable_unprepare(hwa742.sys_ck);
err3:
	hwa742.extif->cleanup();
err2:
	hwa742.int_ctrl->cleanup();
err1:
	return r;
}

static void hwa742_cleanup(void)
{
	hwa742_set_update_mode(OMAPFB_UPDATE_DISABLED);
	hwa742.extif->cleanup();
	hwa742.int_ctrl->cleanup();
	clk_disable_unprepare(hwa742.sys_ck);
}

struct lcd_ctrl hwa742_ctrl = {
	.name			= "hwa742",
	.init			= hwa742_init,
	.cleanup		= hwa742_cleanup,
	.bind_client		= hwa742_bind_client,
	.get_caps		= hwa742_get_caps,
	.set_update_mode	= hwa742_set_update_mode,
	.get_update_mode	= hwa742_get_update_mode,
	.setup_plane		= hwa742_setup_plane,
	.enable_plane		= hwa742_enable_plane,
	.update_window		= hwa742_update_window_async,
	.sync			= hwa742_sync,
	.suspend		= hwa742_suspend,
	.resume			= hwa742_resume,
};

