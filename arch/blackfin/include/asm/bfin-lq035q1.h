/*
 * Blackfin LCD Framebuffer driver SHARP LQ035Q1DH02
 *
 * Copyright 2008-2009 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

#ifndef BFIN_LQ035Q1_H
#define BFIN_LQ035Q1_H

/*
 * LCD Modes
 */
#define LQ035_RL	(0 << 8)	/* Right -> Left Scan */
#define LQ035_LR	(1 << 8)	/* Left -> Right Scan */
#define LQ035_TB	(1 << 9)	/* Top -> Botton Scan */
#define LQ035_BT	(0 << 9)	/* Botton -> Top Scan */
#define LQ035_BGR	(1 << 11)	/* Use BGR format */
#define LQ035_RGB	(0 << 11)	/* Use RGB format */
#define LQ035_NORM	(1 << 13)	/* Reversal */
#define LQ035_REV	(0 << 13)	/* Reversal */

/*
 * PPI Modes
 */

#define USE_RGB565_16_BIT_PPI	1
#define USE_RGB565_8_BIT_PPI	2
#define USE_RGB888_8_BIT_PPI	3

struct bfin_lq035q1fb_disp_info {

	unsigned	mode;
	unsigned	ppi_mode;
	/* GPIOs */
	int		use_bl;
	unsigned 	gpio_bl;
};

#endif /* BFIN_LQ035Q1_H */
