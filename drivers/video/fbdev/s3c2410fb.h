/*
 * linux/drivers/video/s3c2410fb.h
 *	Copyright (c) 2004 Arnaud Patard
 *
 *  S3C2410 LCD Framebuffer Driver
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 *
*/

#ifndef __S3C2410FB_H
#define __S3C2410FB_H

enum s3c_drv_type {
	DRV_S3C2410,
	DRV_S3C2412,
};

struct s3c2410fb_info {
	struct device		*dev;
	struct clk		*clk;

	struct resource		*mem;
	void __iomem		*io;
	void __iomem		*irq_base;

	enum s3c_drv_type	drv_type;
	struct s3c2410fb_hw	regs;

	unsigned long		clk_rate;
	unsigned int		palette_ready;

#ifdef CONFIG_ARM_S3C24XX_CPUFREQ
	struct notifier_block	freq_transition;
#endif

	/* keep these registers in case we need to re-write palette */
	u32			palette_buffer[256];
	u32			pseudo_pal[16];
};

#define PALETTE_BUFF_CLEAR (0x80000000)	/* entry is clear/invalid */

int s3c2410fb_init(void);

#endif
