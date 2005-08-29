/*
 * linux/drivers/video/imxfb.h
 *
 *  Freescale i.MX Frame Buffer device driver
 *
 *  Copyright (C) 2004 S.Hauer, Pengutronix
 *
 *  Copyright (C) 1999 Eric A. Thomas
 *   Based on acornfb.c Copyright (C) Russell King.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

/*
 * These are the bitfields for each
 * display depth that we support.
 */
struct imxfb_rgb {
	struct fb_bitfield	red;
	struct fb_bitfield	green;
	struct fb_bitfield	blue;
	struct fb_bitfield	transp;
};

#define RGB_16	(0)
#define RGB_8	(1)
#define NR_RGB	2

struct imxfb_info {
	struct device		*dev;
	struct imxfb_rgb	*rgb[NR_RGB];

	u_int			max_bpp;
	u_int			max_xres;
	u_int			max_yres;

	/*
	 * These are the addresses we mapped
	 * the framebuffer memory region to.
	 */
	dma_addr_t		map_dma;
	u_char *		map_cpu;
	u_int			map_size;

	u_char *		screen_cpu;
	dma_addr_t		screen_dma;
	u_int			palette_size;

	dma_addr_t		dbar1;
	dma_addr_t		dbar2;

	u_int			pcr;
	u_int			pwmr;
	u_int			lscr1;
	u_int			dmacr;
	u_int			cmap_inverse:1,
				cmap_static:1,
				unused:30;

	void (*lcd_power)(int);
	void (*backlight_power)(int);
};

#define IMX_NAME	"IMX"

/*
 * Minimum X and Y resolutions
 */
#define MIN_XRES	64
#define MIN_YRES	64

