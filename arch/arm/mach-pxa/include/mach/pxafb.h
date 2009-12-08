/*
 *  arch/arm/mach-pxa/include/mach/pxafb.h
 *
 *  Support for the xscale frame buffer.
 *
 *  Author:     Jean-Frederic Clere
 *  Created:    Sep 22, 2003
 *  Copyright:  jfclere@sinix.net
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/fb.h>
#include <mach/regs-lcd.h>

/*
 * Supported LCD connections
 *
 * bits 0 - 3: for LCD panel type:
 *
 *   STN  - for passive matrix
 *   DSTN - for dual scan passive matrix
 *   TFT  - for active matrix
 *
 * bits 4 - 9 : for bus width
 * bits 10-17 : for AC Bias Pin Frequency
 * bit     18 : for output enable polarity
 * bit     19 : for pixel clock edge
 * bit     20 : for output pixel format when base is RGBT16
 */
#define LCD_CONN_TYPE(_x)	((_x) & 0x0f)
#define LCD_CONN_WIDTH(_x)	(((_x) >> 4) & 0x1f)

#define LCD_TYPE_MASK		0xf
#define LCD_TYPE_UNKNOWN	0
#define LCD_TYPE_MONO_STN	1
#define LCD_TYPE_MONO_DSTN	2
#define LCD_TYPE_COLOR_STN	3
#define LCD_TYPE_COLOR_DSTN	4
#define LCD_TYPE_COLOR_TFT	5
#define LCD_TYPE_SMART_PANEL	6
#define LCD_TYPE_MAX		7

#define LCD_MONO_STN_4BPP	((4  << 4) | LCD_TYPE_MONO_STN)
#define LCD_MONO_STN_8BPP	((8  << 4) | LCD_TYPE_MONO_STN)
#define LCD_MONO_DSTN_8BPP	((8  << 4) | LCD_TYPE_MONO_DSTN)
#define LCD_COLOR_STN_8BPP	((8  << 4) | LCD_TYPE_COLOR_STN)
#define LCD_COLOR_DSTN_16BPP	((16 << 4) | LCD_TYPE_COLOR_DSTN)
#define LCD_COLOR_TFT_8BPP	((8  << 4) | LCD_TYPE_COLOR_TFT)
#define LCD_COLOR_TFT_16BPP	((16 << 4) | LCD_TYPE_COLOR_TFT)
#define LCD_COLOR_TFT_18BPP	((18 << 4) | LCD_TYPE_COLOR_TFT)
#define LCD_SMART_PANEL_8BPP	((8  << 4) | LCD_TYPE_SMART_PANEL)
#define LCD_SMART_PANEL_16BPP	((16 << 4) | LCD_TYPE_SMART_PANEL)
#define LCD_SMART_PANEL_18BPP	((18 << 4) | LCD_TYPE_SMART_PANEL)

#define LCD_AC_BIAS_FREQ(x)	(((x) & 0xff) << 10)
#define LCD_BIAS_ACTIVE_HIGH	(0 << 18)
#define LCD_BIAS_ACTIVE_LOW	(1 << 18)
#define LCD_PCLK_EDGE_RISE	(0 << 19)
#define LCD_PCLK_EDGE_FALL	(1 << 19)
#define LCD_ALTERNATE_MAPPING	(1 << 20)

/*
 * This structure describes the machine which we are running on.
 * It is set in linux/arch/arm/mach-pxa/machine_name.c and used in the probe routine
 * of linux/drivers/video/pxafb.c
 */
struct pxafb_mode_info {
	u_long		pixclock;

	u_short		xres;
	u_short		yres;

	u_char		bpp;
	u_int		cmap_greyscale:1,
			depth:8,
			unused:23;

	/* Parallel Mode Timing */
	u_char		hsync_len;
	u_char		left_margin;
	u_char		right_margin;

	u_char		vsync_len;
	u_char		upper_margin;
	u_char		lower_margin;
	u_char		sync;

	/* Smart Panel Mode Timing - see PXA27x DM 7.4.15.0.3 for details
	 * Note:
	 * 1. all parameters in nanosecond (ns)
	 * 2. a0cs{rd,wr}_set_hld are controlled by the same register bits
	 *    in pxa27x and pxa3xx, initialize them to the same value or
	 *    the larger one will be used
	 * 3. same to {rd,wr}_pulse_width
	 *
	 * 4. LCD_PCLK_EDGE_{RISE,FALL} controls the L_PCLK_WR polarity
	 * 5. sync & FB_SYNC_HOR_HIGH_ACT controls the L_LCLK_A0
	 * 6. sync & FB_SYNC_VERT_HIGH_ACT controls the L_LCLK_RD
	 */
	unsigned	a0csrd_set_hld;	/* A0 and CS Setup/Hold Time before/after L_FCLK_RD */
	unsigned	a0cswr_set_hld;	/* A0 and CS Setup/Hold Time before/after L_PCLK_WR */
	unsigned	wr_pulse_width;	/* L_PCLK_WR pulse width */
	unsigned	rd_pulse_width;	/* L_FCLK_RD pulse width */
	unsigned	cmd_inh_time;	/* Command Inhibit time between two writes */
	unsigned	op_hold_time;	/* Output Hold time from L_FCLK_RD negation */
};

struct pxafb_mach_info {
	struct pxafb_mode_info *modes;
	unsigned int num_modes;

	unsigned int	lcd_conn;
	unsigned long	video_mem_size;

	u_int		fixed_modes:1,
			cmap_inverse:1,
			cmap_static:1,
			acceleration_enabled:1,
			unused:28;

	/* The following should be defined in LCCR0
	 *      LCCR0_Act or LCCR0_Pas          Active or Passive
	 *      LCCR0_Sngl or LCCR0_Dual        Single/Dual panel
	 *      LCCR0_Mono or LCCR0_Color       Mono/Color
	 *      LCCR0_4PixMono or LCCR0_8PixMono (in mono single mode)
	 *      LCCR0_DMADel(Tcpu) (optional)   DMA request delay
	 *
	 * The following should not be defined in LCCR0:
	 *      LCCR0_OUM, LCCR0_BM, LCCR0_QDM, LCCR0_DIS, LCCR0_EFM
	 *      LCCR0_IUM, LCCR0_SFM, LCCR0_LDM, LCCR0_ENB
	 */
	u_int		lccr0;
	/* The following should be defined in LCCR3
	 *      LCCR3_OutEnH or LCCR3_OutEnL    Output enable polarity
	 *      LCCR3_PixRsEdg or LCCR3_PixFlEdg Pixel clock edge type
	 *      LCCR3_Acb(X)                    AB Bias pin frequency
	 *      LCCR3_DPC (optional)            Double Pixel Clock mode (untested)
	 *
	 * The following should not be defined in LCCR3
	 *      LCCR3_HSP, LCCR3_VSP, LCCR0_Pcd(x), LCCR3_Bpp
	 */
	u_int		lccr3;
	/* The following should be defined in LCCR4
	 *	LCCR4_PAL_FOR_0 or LCCR4_PAL_FOR_1 or LCCR4_PAL_FOR_2
	 *
	 * All other bits in LCCR4 should be left alone.
	 */
	u_int		lccr4;
	void (*pxafb_backlight_power)(int);
	void (*pxafb_lcd_power)(int, struct fb_var_screeninfo *);
	void (*smart_update)(struct fb_info *);
};
void set_pxa_fb_info(struct pxafb_mach_info *hard_pxa_fb_info);
void set_pxa_fb_parent(struct device *parent_dev);
unsigned long pxafb_get_hsync_time(struct device *dev);

extern int pxafb_smart_queue(struct fb_info *info, uint16_t *cmds, int);
extern int pxafb_smart_flush(struct fb_info *info);
