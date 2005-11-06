/*
 * linux/drivers/s3c2410fb.h
 * Copyright (c) Arnaud Patard
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 *	    S3C2410 LCD Controller Frame Buffer Driver
 *	    based on skeletonfb.c, sa1100fb.h
 *
 * ChangeLog
 *
 * 2004-12-04: Arnaud Patard <arnaud.patard@rtp-net.org>
 *      - Moved dprintk to s3c2410fb.c
 *
 * 2004-09-07: Arnaud Patard <arnaud.patard@rtp-net.org>
 * 	- Renamed from h1940fb.h to s3c2410fb.h
 * 	- Chenged h1940 to s3c2410
 *
 * 2004-07-15: Arnaud Patard <arnaud.patard@rtp-net.org>
 *	- First version
 */

#ifndef __S3C2410FB_H
#define __S3C2410FB_H

struct s3c2410fb_info {
	struct fb_info		*fb;
	struct device		*dev;
	struct clk		*clk;

	struct s3c2410fb_mach_info *mach_info;

	/* raw memory addresses */
	dma_addr_t		map_dma;	/* physical */
	u_char *		map_cpu;	/* virtual */
	u_int			map_size;

	struct s3c2410fb_hw	regs;

	/* addresses of pieces placed in raw buffer */
	u_char *		screen_cpu;	/* virtual address of buffer */
	dma_addr_t		screen_dma;	/* physical address of buffer */
	unsigned int		palette_ready;

	/* keep these registers in case we need to re-write palette */
	u32			palette_buffer[256];
	u32			pseudo_pal[16];
};

#define PALETTE_BUFF_CLEAR (0x80000000)	/* entry is clear/invalid */

int s3c2410fb_init(void);

#endif
