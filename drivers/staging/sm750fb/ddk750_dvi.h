/* SPDX-License-Identifier: GPL-2.0 */
#ifndef DDK750_DVI_H__
#define DDK750_DVI_H__

/* dvi chip stuffs structros */

typedef long (*PFN_DVICTRL_INIT)(unsigned char edge_select,
				 unsigned char bus_select,
				 unsigned char dual_edge_clk_select,
				 unsigned char hsync_enable,
				 unsigned char vsync_enable,
				 unsigned char deskew_enable,
				 unsigned char deskew_setting,
				 unsigned char continuous_sync_enable,
				 unsigned char pll_filter_enable,
				 unsigned char pll_filter_value);

typedef void (*PFN_DVICTRL_RESETCHIP)(void);
typedef char* (*PFN_DVICTRL_GETCHIPSTRING)(void);
typedef unsigned short (*PFN_DVICTRL_GETVENDORID)(void);
typedef unsigned short (*PFN_DVICTRL_GETDEVICEID)(void);
typedef void (*PFN_DVICTRL_SETPOWER)(unsigned char power_up);
typedef void (*PFN_DVICTRL_HOTPLUGDETECTION)(unsigned char enable_hot_plug);
typedef unsigned char (*PFN_DVICTRL_ISCONNECTED)(void);
typedef unsigned char (*PFN_DVICTRL_CHECKINTERRUPT)(void);
typedef void (*PFN_DVICTRL_CLEARINTERRUPT)(void);

/* Structure to hold all the function pointer to the DVI Controller. */
struct dvi_ctrl_device {
	PFN_DVICTRL_INIT		init;
	PFN_DVICTRL_RESETCHIP		reset_chip;
	PFN_DVICTRL_GETCHIPSTRING	get_chip_string;
	PFN_DVICTRL_GETVENDORID		get_vendor_id;
	PFN_DVICTRL_GETDEVICEID		get_device_id;
	PFN_DVICTRL_SETPOWER		set_power;
	PFN_DVICTRL_HOTPLUGDETECTION	enable_hot_plug_detection;
	PFN_DVICTRL_ISCONNECTED		is_connected;
	PFN_DVICTRL_CHECKINTERRUPT	check_interrupt;
	PFN_DVICTRL_CLEARINTERRUPT	clear_interrupt;
};

#define DVI_CTRL_SII164

/* dvi functions prototype */
int dvi_init(unsigned char edge_select,
	     unsigned char bus_select,
	     unsigned char dual_edge_clk_select,
	     unsigned char hsync_enable,
	     unsigned char vsync_enable,
	     unsigned char deskew_enable,
	     unsigned char deskew_setting,
	     unsigned char continuous_sync_enable,
	     unsigned char pll_filter_enable,
	     unsigned char pll_filter_value);

#endif

