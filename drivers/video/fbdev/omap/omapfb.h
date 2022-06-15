/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * File: drivers/video/omap/omapfb.h
 *
 * Framebuffer driver for TI OMAP boards
 *
 * Copyright (C) 2004 Nokia Corporation
 * Author: Imre Deak <imre.deak@nokia.com>
 */

#ifndef __OMAPFB_H
#define __OMAPFB_H

#include <linux/fb.h>
#include <linux/mutex.h>
#include <linux/omapfb.h>

#define OMAPFB_EVENT_READY	1
#define OMAPFB_EVENT_DISABLED	2

#define OMAP_LCDC_INV_VSYNC             0x0001
#define OMAP_LCDC_INV_HSYNC             0x0002
#define OMAP_LCDC_INV_PIX_CLOCK         0x0004
#define OMAP_LCDC_INV_OUTPUT_EN         0x0008
#define OMAP_LCDC_HSVS_RISING_EDGE      0x0010
#define OMAP_LCDC_HSVS_OPPOSITE         0x0020

#define OMAP_LCDC_SIGNAL_MASK		0x003f

#define OMAP_LCDC_PANEL_TFT		0x0100

#define OMAPFB_PLANE_XRES_MIN		8
#define OMAPFB_PLANE_YRES_MIN		8

struct omapfb_device;

#define OMAPFB_PLANE_NUM		1

struct omapfb_mem_region {
	u32		paddr;
	void __iomem	*vaddr;
	unsigned long	size;
	u8		type;		/* OMAPFB_PLANE_MEM_* */
	enum omapfb_color_format format;/* OMAPFB_COLOR_* */
	unsigned	format_used:1;	/* Must be set when format is set.
					 * Needed b/c of the badly chosen 0
					 * base for OMAPFB_COLOR_* values
					 */
	unsigned	alloc:1;	/* allocated by the driver */
	unsigned	map:1;		/* kernel mapped by the driver */
};

struct omapfb_mem_desc {
	int				region_cnt;
	struct omapfb_mem_region	region[OMAPFB_PLANE_NUM];
};

struct lcd_panel {
	const char	*name;
	int		config;		/* TFT/STN, signal inversion */
	int		bpp;		/* Pixel format in fb mem */
	int		data_lines;	/* Lines on LCD HW interface */

	int		x_res, y_res;
	int		pixel_clock;	/* In kHz */
	int		hsw;		/* Horizontal synchronization
					   pulse width */
	int		hfp;		/* Horizontal front porch */
	int		hbp;		/* Horizontal back porch */
	int		vsw;		/* Vertical synchronization
					   pulse width */
	int		vfp;		/* Vertical front porch */
	int		vbp;		/* Vertical back porch */
	int		acb;		/* ac-bias pin frequency */
	int		pcd;		/* pixel clock divider.
					   Obsolete use pixel_clock instead */

	int		(*init)		(struct lcd_panel *panel,
					 struct omapfb_device *fbdev);
	void		(*cleanup)	(struct lcd_panel *panel);
	int		(*enable)	(struct lcd_panel *panel);
	void		(*disable)	(struct lcd_panel *panel);
	unsigned long	(*get_caps)	(struct lcd_panel *panel);
	int		(*set_bklight_level)(struct lcd_panel *panel,
					     unsigned int level);
	unsigned int	(*get_bklight_level)(struct lcd_panel *panel);
	unsigned int	(*get_bklight_max)  (struct lcd_panel *panel);
	int		(*run_test)	(struct lcd_panel *panel, int test_num);
};

struct extif_timings {
	int cs_on_time;
	int cs_off_time;
	int we_on_time;
	int we_off_time;
	int re_on_time;
	int re_off_time;
	int we_cycle_time;
	int re_cycle_time;
	int cs_pulse_width;
	int access_time;

	int clk_div;

	u32 tim[5];		/* set by extif->convert_timings */

	int converted;
};

struct lcd_ctrl_extif {
	int  (*init)		(struct omapfb_device *fbdev);
	void (*cleanup)		(void);
	void (*get_clk_info)	(u32 *clk_period, u32 *max_clk_div);
	unsigned long (*get_max_tx_rate)(void);
	int  (*convert_timings)	(struct extif_timings *timings);
	void (*set_timings)	(const struct extif_timings *timings);
	void (*set_bits_per_cycle)(int bpc);
	void (*write_command)	(const void *buf, unsigned int len);
	void (*read_data)	(void *buf, unsigned int len);
	void (*write_data)	(const void *buf, unsigned int len);
	void (*transfer_area)	(int width, int height,
				 void (callback)(void *data), void *data);
	int  (*setup_tearsync)	(unsigned pin_cnt,
				 unsigned hs_pulse_time, unsigned vs_pulse_time,
				 int hs_pol_inv, int vs_pol_inv, int div);
	int  (*enable_tearsync) (int enable, unsigned line);

	unsigned long		max_transmit_size;
};

struct omapfb_notifier_block {
	struct notifier_block	nb;
	void			*data;
	int			plane_idx;
};

typedef int (*omapfb_notifier_callback_t)(struct notifier_block *,
					  unsigned long event,
					  void *fbi);

struct lcd_ctrl {
	const char	*name;
	void		*data;

	int		(*init)		  (struct omapfb_device *fbdev,
					   int ext_mode,
					   struct omapfb_mem_desc *req_md);
	void		(*cleanup)	  (void);
	void		(*bind_client)	  (struct omapfb_notifier_block *nb);
	void		(*get_caps)	  (int plane, struct omapfb_caps *caps);
	int		(*set_update_mode)(enum omapfb_update_mode mode);
	enum omapfb_update_mode (*get_update_mode)(void);
	int		(*setup_plane)	  (int plane, int channel_out,
					   unsigned long offset,
					   int screen_width,
					   int pos_x, int pos_y, int width,
					   int height, int color_mode);
	int		(*set_rotate)	  (int angle);
	int		(*setup_mem)	  (int plane, size_t size,
					   int mem_type, unsigned long *paddr);
	int		(*mmap)		  (struct fb_info *info,
					   struct vm_area_struct *vma);
	int		(*set_scale)	  (int plane,
					   int orig_width, int orig_height,
					   int out_width, int out_height);
	int		(*enable_plane)	  (int plane, int enable);
	int		(*update_window)  (struct fb_info *fbi,
					   struct omapfb_update_window *win,
					   void (*callback)(void *),
					   void *callback_data);
	void		(*sync)		  (void);
	void		(*suspend)	  (void);
	void		(*resume)	  (void);
	int		(*run_test)	  (int test_num);
	int		(*setcolreg)	  (u_int regno, u16 red, u16 green,
					   u16 blue, u16 transp,
					   int update_hw_mem);
	int		(*set_color_key)  (struct omapfb_color_key *ck);
	int		(*get_color_key)  (struct omapfb_color_key *ck);
};

enum omapfb_state {
	OMAPFB_DISABLED		= 0,
	OMAPFB_SUSPENDED	= 99,
	OMAPFB_ACTIVE		= 100
};

struct omapfb_plane_struct {
	int				idx;
	struct omapfb_plane_info	info;
	enum omapfb_color_format	color_mode;
	struct omapfb_device		*fbdev;
};

struct omapfb_device {
	int			state;
	int                     ext_lcdc;		/* Using external
							   LCD controller */
	struct mutex		rqueue_mutex;

	int			palette_size;
	u32			pseudo_palette[17];

	struct lcd_panel	*panel;			/* LCD panel */
	const struct lcd_ctrl	*ctrl;			/* LCD controller */
	const struct lcd_ctrl	*int_ctrl;		/* internal LCD ctrl */
	int			ext_irq;
	int			int_irq;
	struct lcd_ctrl_extif	*ext_if;		/* LCD ctrl external
							   interface */
	struct device		*dev;
	struct fb_var_screeninfo	new_var;	/* for mode changes */

	struct omapfb_mem_desc		mem_desc;
	struct fb_info			*fb_info[OMAPFB_PLANE_NUM];

	struct platform_device	*dssdev;	/* dummy dev for clocks */
};

extern struct lcd_ctrl omap1_lcd_ctrl;

extern void omapfb_register_panel(struct lcd_panel *panel);
extern void omapfb_write_first_pixel(struct omapfb_device *fbdev, u16 pixval);
extern void omapfb_notify_clients(struct omapfb_device *fbdev,
				  unsigned long event);
extern int  omapfb_register_client(struct omapfb_notifier_block *nb,
				   omapfb_notifier_callback_t callback,
				   void *callback_data);
extern int  omapfb_unregister_client(struct omapfb_notifier_block *nb);
extern int  omapfb_update_window_async(struct fb_info *fbi,
				       struct omapfb_update_window *win,
				       void (*callback)(void *),
				       void *callback_data);
extern int  hwa742_update_window_async(struct fb_info *fbi,
				       struct omapfb_update_window *win,
				       void (*callback)(void *),
				       void *callback_data);

#endif /* __OMAPFB_H */
