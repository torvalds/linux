/*
 * Copyright (C) 2011-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#ifndef __MIPI_DSI_H__
#define __MIPI_DSI_H__

#include <linux/regmap.h>
#include "mxc_dispdrv.h"

#ifdef DEBUG
#define mipi_dbg(fmt, ...) printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#else
#define mipi_dbg(fmt, ...)
#endif

#define	DSI_CMD_BUF_MAXSIZE         (32)

/* DPI interface pixel color coding map */
enum mipi_dsi_dpi_fmt {
	MIPI_RGB565_PACKED = 0,
	MIPI_RGB565_LOOSELY,
	MIPI_RGB565_CONFIG3,
	MIPI_RGB666_PACKED,
	MIPI_RGB666_LOOSELY,
	MIPI_RGB888,
};

struct mipi_lcd_config {
	u32				virtual_ch;
	u32				data_lane_num;
	/* device max DPHY clock in MHz unit */
	u32				max_phy_clk;
	enum mipi_dsi_dpi_fmt		dpi_fmt;
};

struct mipi_dsi_info;
struct mipi_dsi_lcd_callback {
	/* callback for lcd panel operation */
	void (*get_mipi_lcd_videomode)(struct fb_videomode **, int *,
			struct mipi_lcd_config **);
	int  (*mipi_lcd_setup)(struct mipi_dsi_info *);

};

struct mipi_dsi_match_lcd {
	char *lcd_panel;
	struct mipi_dsi_lcd_callback lcd_callback;
};

struct mipi_dsi_bus_mux {
	int reg;
	int mask;
	int (*get_mux) (int dev_id, int disp_id);
};

/* driver private data */
struct mipi_dsi_info {
	struct platform_device		*pdev;
	void __iomem			*mmio_base;
	struct regmap			*regmap;
	const struct mipi_dsi_bus_mux	*bus_mux;
	int				dsi_power_on;
	int				lcd_inited;
	u32				dphy_pll_config;
	int				dev_id;
	int				disp_id;
	char				*lcd_panel;
	int				irq;
	struct clk			*dphy_clk;
	struct clk			*cfg_clk;
	struct mxc_dispdrv_handle	*disp_mipi;
	struct  fb_videomode		*mode;
	struct regulator		*disp_power_on;
	struct  mipi_lcd_config		*lcd_config;
	/* board related power control */
	struct backlight_device		*bl;
	/* callback for lcd panel operation */
	struct mipi_dsi_lcd_callback	*lcd_callback;

	int (*mipi_dsi_pkt_read)(struct mipi_dsi_info *mipi,
			u8 data_type, u32 *buf, int len);
	int (*mipi_dsi_pkt_write)(struct mipi_dsi_info *mipi_dsi,
			u8 data_type, const u32 *buf, int len);
	int (*mipi_dsi_dcs_cmd)(struct mipi_dsi_info *mipi,
			u8 cmd, const u32 *param, int num);
};

#ifdef CONFIG_FB_MXC_TRULY_WVGA_SYNC_PANEL
void mipid_hx8369_get_lcd_videomode(struct fb_videomode **mode, int *size,
		struct mipi_lcd_config **data);
int mipid_hx8369_lcd_setup(struct mipi_dsi_info *);
#endif
#ifdef CONFIG_FB_MXC_TRULY_PANEL_TFT3P5079E
void mipid_otm8018b_get_lcd_videomode(struct fb_videomode **mode, int *size,
		struct mipi_lcd_config **data);
int mipid_otm8018b_lcd_setup(struct mipi_dsi_info *);
#endif

#ifndef CONFIG_FB_MXC_TRULY_WVGA_SYNC_PANEL
#error "Please configure MIPI LCD panel, we cannot find one!"
#endif

#endif
