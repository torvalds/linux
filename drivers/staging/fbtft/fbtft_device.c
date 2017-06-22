/*
 *
 * Copyright (C) 2013, Noralf Tronnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) "fbtft_device: " fmt
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <video/mipi_display.h>

#include "fbtft.h"

#define MAX_GPIOS 32

static struct spi_device *spi_device;
static struct platform_device *p_device;

static char *name;
module_param(name, charp, 0000);
MODULE_PARM_DESC(name, "Devicename (required). name=list => list all supported devices.");

static unsigned int rotate;
module_param(rotate, uint, 0000);
MODULE_PARM_DESC(rotate,
"Angle to rotate display counter clockwise: 0, 90, 180, 270");

static unsigned int busnum;
module_param(busnum, uint, 0000);
MODULE_PARM_DESC(busnum, "SPI bus number (default=0)");

static unsigned int cs;
module_param(cs, uint, 0000);
MODULE_PARM_DESC(cs, "SPI chip select (default=0)");

static unsigned int speed;
module_param(speed, uint, 0000);
MODULE_PARM_DESC(speed, "SPI speed (override device default)");

static int mode = -1;
module_param(mode, int, 0000);
MODULE_PARM_DESC(mode, "SPI mode (override device default)");

static char *gpios;
module_param(gpios, charp, 0000);
MODULE_PARM_DESC(gpios,
"List of gpios. Comma separated with the form: reset:23,dc:24 (when overriding the default, all gpios must be specified)");

static unsigned int fps;
module_param(fps, uint, 0000);
MODULE_PARM_DESC(fps, "Frames per second (override driver default)");

static char *gamma;
module_param(gamma, charp, 0000);
MODULE_PARM_DESC(gamma,
"String representation of Gamma Curve(s). Driver specific.");

static int txbuflen;
module_param(txbuflen, int, 0000);
MODULE_PARM_DESC(txbuflen, "txbuflen (override driver default)");

static int bgr = -1;
module_param(bgr, int, 0000);
MODULE_PARM_DESC(bgr,
"BGR bit (supported by some drivers).");

static unsigned int startbyte;
module_param(startbyte, uint, 0000);
MODULE_PARM_DESC(startbyte, "Sets the Start byte used by some SPI displays.");

static bool custom;
module_param(custom, bool, 0000);
MODULE_PARM_DESC(custom, "Add a custom display device. Use speed= argument to make it a SPI device, else platform_device");

static unsigned int width;
module_param(width, uint, 0000);
MODULE_PARM_DESC(width, "Display width, used with the custom argument");

static unsigned int height;
module_param(height, uint, 0000);
MODULE_PARM_DESC(height, "Display height, used with the custom argument");

static unsigned int buswidth = 8;
module_param(buswidth, uint, 0000);
MODULE_PARM_DESC(buswidth, "Display bus width, used with the custom argument");

static s16 init[FBTFT_MAX_INIT_SEQUENCE];
static int init_num;
module_param_array(init, short, &init_num, 0000);
MODULE_PARM_DESC(init, "Init sequence, used with the custom argument");

static unsigned long debug;
module_param(debug, ulong, 0000);
MODULE_PARM_DESC(debug,
"level: 0-7 (the remaining 29 bits is for advanced usage)");

static unsigned int verbose = 3;
module_param(verbose, uint, 0000);
MODULE_PARM_DESC(verbose,
"0 silent, >0 show gpios, >1 show devices, >2 show devices before (default=3)");

struct fbtft_device_display {
	char *name;
	struct spi_board_info *spi;
	struct platform_device *pdev;
};

static void fbtft_device_pdev_release(struct device *dev);

static int write_gpio16_wr_slow(struct fbtft_par *par, void *buf, size_t len);
static void adafruit18_green_tab_set_addr_win(struct fbtft_par *par,
	int xs, int ys, int xe, int ye);

#define ADAFRUIT18_GAMMA \
		"02 1c 07 12 37 32 29 2d 29 25 2B 39 00 01 03 10\n" \
		"03 1d 07 06 2E 2C 29 2D 2E 2E 37 3F 00 00 02 10"

#define CBERRY28_GAMMA \
		"D0 00 14 15 13 2C 42 43 4E 09 16 14 18 21\n" \
		"D0 00 14 15 13 0B 43 55 53 0C 17 14 23 20"

static s16 cberry28_init_sequence[] = {
	/* turn off sleep mode */
	-1, MIPI_DCS_EXIT_SLEEP_MODE,
	-2, 120,

	/* set pixel format to RGB-565 */
	-1, MIPI_DCS_SET_PIXEL_FORMAT, MIPI_DCS_PIXEL_FMT_16BIT,

	-1, 0xB2, 0x0C, 0x0C, 0x00, 0x33, 0x33,

	/*
	 * VGH = 13.26V
	 * VGL = -10.43V
	 */
	-1, 0xB7, 0x35,

	/*
	 * VDV and VRH register values come from command write
	 * (instead of NVM)
	 */
	-1, 0xC2, 0x01, 0xFF,

	/*
	 * VAP =  4.7V + (VCOM + VCOM offset + 0.5 * VDV)
	 * VAN = -4.7V + (VCOM + VCOM offset + 0.5 * VDV)
	 */
	-1, 0xC3, 0x17,

	/* VDV = 0V */
	-1, 0xC4, 0x20,

	/* VCOM = 0.675V */
	-1, 0xBB, 0x17,

	/* VCOM offset = 0V */
	-1, 0xC5, 0x20,

	/*
	 * AVDD = 6.8V
	 * AVCL = -4.8V
	 * VDS = 2.3V
	 */
	-1, 0xD0, 0xA4, 0xA1,

	-1, MIPI_DCS_SET_DISPLAY_ON,

	-3,
};

static s16 hy28b_init_sequence[] = {
	-1, 0x00e7, 0x0010, -1, 0x0000, 0x0001,
	-1, 0x0001, 0x0100, -1, 0x0002, 0x0700,
	-1, 0x0003, 0x1030, -1, 0x0004, 0x0000,
	-1, 0x0008, 0x0207, -1, 0x0009, 0x0000,
	-1, 0x000a, 0x0000, -1, 0x000c, 0x0001,
	-1, 0x000d, 0x0000, -1, 0x000f, 0x0000,
	-1, 0x0010, 0x0000, -1, 0x0011, 0x0007,
	-1, 0x0012, 0x0000, -1, 0x0013, 0x0000,
	-2, 50, -1, 0x0010, 0x1590, -1, 0x0011,
	0x0227, -2, 50, -1, 0x0012, 0x009c, -2, 50,
	-1, 0x0013, 0x1900, -1, 0x0029, 0x0023,
	-1, 0x002b, 0x000e, -2, 50,
	-1, 0x0020, 0x0000, -1, 0x0021, 0x0000,
	-2, 50, -1, 0x0050, 0x0000,
	-1, 0x0051, 0x00ef, -1, 0x0052, 0x0000,
	-1, 0x0053, 0x013f, -1, 0x0060, 0xa700,
	-1, 0x0061, 0x0001, -1, 0x006a, 0x0000,
	-1, 0x0080, 0x0000, -1, 0x0081, 0x0000,
	-1, 0x0082, 0x0000, -1, 0x0083, 0x0000,
	-1, 0x0084, 0x0000, -1, 0x0085, 0x0000,
	-1, 0x0090, 0x0010, -1, 0x0092, 0x0000,
	-1, 0x0093, 0x0003, -1, 0x0095, 0x0110,
	-1, 0x0097, 0x0000, -1, 0x0098, 0x0000,
	-1, 0x0007, 0x0133, -1, 0x0020, 0x0000,
	-1, 0x0021, 0x0000, -2, 100, -3 };

#define HY28B_GAMMA \
	"04 1F 4 7 7 0 7 7 6 0\n" \
	"0F 00 1 7 4 0 0 0 6 7"

static s16 pitft_init_sequence[] = {
	-1, MIPI_DCS_SOFT_RESET,
	-2, 5,
	-1, MIPI_DCS_SET_DISPLAY_OFF,
	-1, 0xEF, 0x03, 0x80, 0x02,
	-1, 0xCF, 0x00, 0xC1, 0x30,
	-1, 0xED, 0x64, 0x03, 0x12, 0x81,
	-1, 0xE8, 0x85, 0x00, 0x78,
	-1, 0xCB, 0x39, 0x2C, 0x00, 0x34, 0x02,
	-1, 0xF7, 0x20,
	-1, 0xEA, 0x00, 0x00,
	-1, 0xC0, 0x23,
	-1, 0xC1, 0x10,
	-1, 0xC5, 0x3E, 0x28,
	-1, 0xC7, 0x86,
	-1, MIPI_DCS_SET_PIXEL_FORMAT, 0x55,
	-1, 0xB1, 0x00, 0x18,
	-1, 0xB6, 0x08, 0x82, 0x27,
	-1, 0xF2, 0x00,
	-1, MIPI_DCS_SET_GAMMA_CURVE, 0x01,
	-1, 0xE0, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E,
		0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00,
	-1, 0xE1, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31,
		0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F,
	-1, MIPI_DCS_EXIT_SLEEP_MODE,
	-2, 100,
	-1, MIPI_DCS_SET_DISPLAY_ON,
	-2, 20,
	-3
};

static s16 waveshare32b_init_sequence[] = {
	-1, 0xCB, 0x39, 0x2C, 0x00, 0x34, 0x02,
	-1, 0xCF, 0x00, 0xC1, 0x30,
	-1, 0xE8, 0x85, 0x00, 0x78,
	-1, 0xEA, 0x00, 0x00,
	-1, 0xED, 0x64, 0x03, 0x12, 0x81,
	-1, 0xF7, 0x20,
	-1, 0xC0, 0x23,
	-1, 0xC1, 0x10,
	-1, 0xC5, 0x3E, 0x28,
	-1, 0xC7, 0x86,
	-1, MIPI_DCS_SET_ADDRESS_MODE, 0x28,
	-1, MIPI_DCS_SET_PIXEL_FORMAT, 0x55,
	-1, 0xB1, 0x00, 0x18,
	-1, 0xB6, 0x08, 0x82, 0x27,
	-1, 0xF2, 0x00,
	-1, MIPI_DCS_SET_GAMMA_CURVE, 0x01,
	-1, 0xE0, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E,
		0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00,
	-1, 0xE1, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31,
		0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F,
	-1, MIPI_DCS_EXIT_SLEEP_MODE,
	-2, 120,
	-1, MIPI_DCS_SET_DISPLAY_ON,
	-1, MIPI_DCS_WRITE_MEMORY_START,
	-3
};

/* Supported displays in alphabetical order */
static struct fbtft_device_display displays[] = {
	{
		.name = "adafruit18",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_st7735r",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = 1,
				},
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{ "led", 18 },
					{},
				},
				.gamma = ADAFRUIT18_GAMMA,
			}
		}
	}, {
		.name = "adafruit18_green",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_st7735r",
			.max_speed_hz = 4000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = 1,
					.fbtftops.set_addr_win =
					    adafruit18_green_tab_set_addr_win,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{ "led", 18 },
					{},
				},
				.gamma = ADAFRUIT18_GAMMA,
			}
		}
	}, {
		.name = "adafruit22",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_hx8340bn",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 9,
					.backlight = 1,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "led", 23 },
					{},
				},
			}
		}
	}, {
		.name = "adafruit22a",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_ili9340",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = 1,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{ "led", 18 },
					{},
				},
			}
		}
	}, {
		.name = "adafruit28",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_ili9341",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = 1,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{ "led", 18 },
					{},
				},
			}
		}
	}, {
		.name = "adafruit13m",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_ssd1306",
			.max_speed_hz = 16000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
				},
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{},
				},
			}
		}
	}, {
		.name = "admatec_c-berry28",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_st7789v",
			.max_speed_hz = 48000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = 1,
					.init_sequence = cberry28_init_sequence,
				},
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 22 },
					{ "led", 18 },
					{},
				},
				.gamma = CBERRY28_GAMMA,
			}
		}
	}, {
		.name = "agm1264k-fl",
		.pdev = &(struct platform_device) {
			.name = "fb_agm1264k-fl",
			.id = 0,
			.dev = {
			.release = fbtft_device_pdev_release,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = FBTFT_ONBOARD_BACKLIGHT,
				},
				.gpios = (const struct fbtft_gpio []) {
					{},
				},
			},
			}
		}
	}, {
		.name = "dogs102",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_uc1701",
			.max_speed_hz = 8000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 13 },
					{ "dc", 6 },
					{},
				},
			}
		}
	}, {
		.name = "er_tftm050_2",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_ra8875",
			.max_speed_hz = 5000000,
			.mode = SPI_MODE_3,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = 1,
					.width = 480,
					.height = 272,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{},
				},
			}
		}
	}, {
		.name = "er_tftm070_5",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_ra8875",
			.max_speed_hz = 5000000,
			.mode = SPI_MODE_3,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = 1,
					.width = 800,
					.height = 480,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{},
				},
			}
		}
	}, {
		.name = "ew24ha0",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_uc1611",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_3,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
				},
				.gpios = (const struct fbtft_gpio []) {
					{ "dc", 24 },
					{},
				},
			}
		}
	}, {
		.name = "ew24ha0_9bit",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_uc1611",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_3,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 9,
				},
				.gpios = (const struct fbtft_gpio []) {
					{},
				},
			}
		}
	}, {
		.name = "flexfb",
		.spi = &(struct spi_board_info) {
			.modalias = "flexfb",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{},
				},
			}
		}
	}, {
		.name = "flexpfb",
		.pdev = &(struct platform_device) {
			.name = "flexpfb",
			.id = 0,
			.dev = {
			.release = fbtft_device_pdev_release,
			.platform_data = &(struct fbtft_platform_data) {
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 17 },
					{ "dc", 1 },
					{ "wr", 0 },
					{ "cs", 21 },
					{ "db00", 9 },
					{ "db01", 11 },
					{ "db02", 18 },
					{ "db03", 23 },
					{ "db04", 24 },
					{ "db05", 25 },
					{ "db06", 8 },
					{ "db07", 7 },
					{ "led", 4 },
					{},
				},
			},
			}
		}
	}, {
		.name = "freetronicsoled128",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_ssd1351",
			.max_speed_hz = 20000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = FBTFT_ONBOARD_BACKLIGHT,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 24 },
					{ "dc", 25 },
					{},
				},
			}
		}
	}, {
		.name = "hx8353d",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_hx8353d",
			.max_speed_hz = 16000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = 1,
				},
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{ "led", 23 },
					{},
				},
			}
		}
	}, {
		.name = "hy28a",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_ili9320",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_3,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = 1,
				},
				.startbyte = 0x70,
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "led", 18 },
					{},
				},
			}
		}
	}, {
		.name = "hy28b",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_ili9325",
			.max_speed_hz = 48000000,
			.mode = SPI_MODE_3,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = 1,
					.init_sequence = hy28b_init_sequence,
				},
				.startbyte = 0x70,
				.bgr = true,
				.fps = 50,
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "led", 18 },
					{},
				},
				.gamma = HY28B_GAMMA,
			}
		}
	}, {
		.name = "ili9481",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_ili9481",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.regwidth = 16,
					.buswidth = 8,
					.backlight = 1,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{ "led", 22 },
					{},
				},
			}
		}
	}, {
		.name = "itdb24",
		.pdev = &(struct platform_device) {
			.name = "fb_s6d1121",
			.id = 0,
			.dev = {
			.release = fbtft_device_pdev_release,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = 1,
				},
				.bgr = false,
				.gpios = (const struct fbtft_gpio []) {
					/* Wiring for LCD adapter kit */
					{ "reset", 7 },
					{ "dc", 0 },	/* rev 2: 2 */
					{ "wr", 1 },	/* rev 2: 3 */
					{ "cs", 8 },
					{ "db00", 17 },
					{ "db01", 18 },
					{ "db02", 21 }, /* rev 2: 27 */
					{ "db03", 22 },
					{ "db04", 23 },
					{ "db05", 24 },
					{ "db06", 25 },
					{ "db07", 4 },
					{}
				},
			},
			}
		}
	}, {
		.name = "itdb28",
		.pdev = &(struct platform_device) {
			.name = "fb_ili9325",
			.id = 0,
			.dev = {
			.release = fbtft_device_pdev_release,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = 1,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{},
				},
			},
			}
		}
	}, {
		.name = "itdb28_spi",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_ili9325",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = 1,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{},
				},
			}
		}
	}, {
		.name = "mi0283qt-2",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_hx8347d",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = 1,
				},
				.startbyte = 0x70,
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{ "led", 18 },
					{},
				},
			}
		}
	}, {
		.name = "mi0283qt-9a",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_ili9341",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 9,
					.backlight = 1,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "led", 18 },
					{},
				},
			}
		}
	}, {
		.name = "mi0283qt-v2",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_watterott",
			.max_speed_hz = 4000000,
			.mode = SPI_MODE_3,
			.platform_data = &(struct fbtft_platform_data) {
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{},
				},
			}
		}
	}, {
		.name = "nokia3310",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_pcd8544",
			.max_speed_hz = 400000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
				},
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{ "led", 23 },
					{},
				},
			}
		}
	}, {
		.name = "nokia3310a",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_tls8204",
			.max_speed_hz = 1000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
				},
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{ "led", 23 },
					{},
				},
			}
		}
	}, {
		.name = "nokia5110",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_ili9163",
			.max_speed_hz = 12000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = 1,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{},
				},
			}
		}
	}, {

		.name = "piscreen",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_ili9486",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.regwidth = 16,
					.buswidth = 8,
					.backlight = 1,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{ "led", 22 },
					{},
				},
			}
		}
	}, {
		.name = "pitft",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_ili9340",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_0,
			.chip_select = 0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = 1,
					.init_sequence = pitft_init_sequence,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{ "dc", 25 },
					{},
				},
			}
		}
	}, {
		.name = "pioled",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_ssd1351",
			.max_speed_hz = 20000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 24 },
					{ "dc", 25 },
					{},
				},
				.gamma =	"0 2 2 2 2 2 2 2 "
						"2 2 2 2 2 2 2 2 "
						"2 2 2 2 2 2 2 2 "
						"2 2 2 2 2 2 2 3 "
						"3 3 3 3 3 3 3 3 "
						"3 3 3 3 3 3 3 3 "
						"3 3 3 4 4 4 4 4 "
						"4 4 4 4 4 4 4"
			}
		}
	}, {
		.name = "rpi-display",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_ili9341",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = 1,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 23 },
					{ "dc", 24 },
					{ "led", 18 },
					{},
				},
			}
		}
	}, {
		.name = "s6d02a1",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_s6d02a1",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = 1,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{ "led", 23 },
					{},
				},
			}
		}
	}, {
		.name = "sainsmart18",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_st7735r",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
				},
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{},
				},
			}
		}
	}, {
		.name = "sainsmart32",
		.pdev = &(struct platform_device) {
			.name = "fb_ssd1289",
			.id = 0,
			.dev = {
			.release = fbtft_device_pdev_release,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 16,
					.txbuflen = -2, /* disable buffer */
					.backlight = 1,
					.fbtftops.write = write_gpio16_wr_slow,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{},
				},
			},
		},
		}
	}, {
		.name = "sainsmart32_fast",
		.pdev = &(struct platform_device) {
			.name = "fb_ssd1289",
			.id = 0,
			.dev = {
			.release = fbtft_device_pdev_release,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 16,
					.txbuflen = -2, /* disable buffer */
					.backlight = 1,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{},
				},
			},
		},
		}
	}, {
		.name = "sainsmart32_latched",
		.pdev = &(struct platform_device) {
			.name = "fb_ssd1289",
			.id = 0,
			.dev = {
			.release = fbtft_device_pdev_release,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 16,
					.txbuflen = -2, /* disable buffer */
					.backlight = 1,
					.fbtftops.write =
						fbtft_write_gpio16_wr_latched,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{},
				},
			},
		},
		}
	}, {
		.name = "sainsmart32_spi",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_ssd1289",
			.max_speed_hz = 16000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = 1,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{},
				},
			}
		}
	}, {
		.name = "spidev",
		.spi = &(struct spi_board_info) {
			.modalias = "spidev",
			.max_speed_hz = 500000,
			.bus_num = 0,
			.chip_select = 0,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.gpios = (const struct fbtft_gpio []) {
					{},
				},
			}
		}
	}, {
		.name = "ssd1331",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_ssd1331",
			.max_speed_hz = 20000000,
			.mode = SPI_MODE_3,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
				},
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 24 },
					{ "dc", 25 },
					{},
				},
			}
		}
	}, {
		.name = "tinylcd35",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_tinylcd",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = 1,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{ "led", 18 },
					{},
				},
			}
		}
	}, {
		.name = "tm022hdh26",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_ili9341",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = 1,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 25 },
					{ "dc", 24 },
					{ "led", 18 },
					{},
				},
			}
		}
	}, {
		.name = "tontec35_9481", /* boards before 02 July 2014 */
		.spi = &(struct spi_board_info) {
			.modalias = "fb_ili9481",
			.max_speed_hz = 128000000,
			.mode = SPI_MODE_3,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = 1,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 15 },
					{ "dc", 25 },
					{ "led_", 18 },
					{},
				},
			}
		}
	}, {
		.name = "tontec35_9486", /* boards after 02 July 2014 */
		.spi = &(struct spi_board_info) {
			.modalias = "fb_ili9486",
			.max_speed_hz = 128000000,
			.mode = SPI_MODE_3,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = 1,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 15 },
					{ "dc", 25 },
					{ "led_", 18 },
					{},
				},
			}
		}
	}, {
		.name = "upd161704",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_upd161704",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
				},
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 24 },
					{ "dc", 25 },
					{},
				},
			}
		}
	}, {
		.name = "waveshare32b",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_ili9340",
			.max_speed_hz = 48000000,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
					.backlight = 1,
					.init_sequence =
						waveshare32b_init_sequence,
				},
				.bgr = true,
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 27 },
					{ "dc", 22 },
					{},
				},
			}
		}
	}, {
		.name = "waveshare22",
		.spi = &(struct spi_board_info) {
			.modalias = "fb_bd663474",
			.max_speed_hz = 32000000,
			.mode = SPI_MODE_3,
			.platform_data = &(struct fbtft_platform_data) {
				.display = {
					.buswidth = 8,
				},
				.gpios = (const struct fbtft_gpio []) {
					{ "reset", 24 },
					{ "dc", 25 },
					{},
				},
			}
		}
	}, {
		/* This should be the last item.
		 * Used with the custom argument
		 */
		.name = "",
		.spi = &(struct spi_board_info) {
			.modalias = "",
			.max_speed_hz = 0,
			.mode = SPI_MODE_0,
			.platform_data = &(struct fbtft_platform_data) {
				.gpios = (const struct fbtft_gpio []) {
					{},
				},
			}
		},
		.pdev = &(struct platform_device) {
			.name = "",
			.id = 0,
			.dev = {
			.release = fbtft_device_pdev_release,
			.platform_data = &(struct fbtft_platform_data) {
				.gpios = (const struct fbtft_gpio []) {
					{},
				},
			},
		},
		},
	}
};

static int write_gpio16_wr_slow(struct fbtft_par *par, void *buf, size_t len)
{
	u16 data;
	int i;
#ifndef DO_NOT_OPTIMIZE_FBTFT_WRITE_GPIO
	static u16 prev_data;
#endif

	fbtft_par_dbg_hex(DEBUG_WRITE, par, par->info->device, u8, buf, len,
		"%s(len=%d): ", __func__, len);

	while (len) {
		data = *(u16 *)buf;

		/* Start writing by pulling down /WR */
		gpio_set_value(par->gpio.wr, 0);

		/* Set data */
#ifndef DO_NOT_OPTIMIZE_FBTFT_WRITE_GPIO
		if (data == prev_data) {
			gpio_set_value(par->gpio.wr, 0); /* used as delay */
		} else {
			for (i = 0; i < 16; i++) {
				if ((data & 1) != (prev_data & 1))
					gpio_set_value(par->gpio.db[i],
								data & 1);
				data >>= 1;
				prev_data >>= 1;
			}
		}
#else
		for (i = 0; i < 16; i++) {
			gpio_set_value(par->gpio.db[i], data & 1);
			data >>= 1;
		}
#endif

		/* Pullup /WR */
		gpio_set_value(par->gpio.wr, 1);

#ifndef DO_NOT_OPTIMIZE_FBTFT_WRITE_GPIO
		prev_data = *(u16 *)buf;
#endif
		buf += 2;
		len -= 2;
	}

	return 0;
}

static void adafruit18_green_tab_set_addr_win(struct fbtft_par *par,
						int xs, int ys, int xe, int ye)
{
	write_reg(par, 0x2A, 0, xs + 2, 0, xe + 2);
	write_reg(par, 0x2B, 0, ys + 1, 0, ye + 1);
	write_reg(par, 0x2C);
}

/* used if gpios parameter is present */
static struct fbtft_gpio fbtft_device_param_gpios[MAX_GPIOS + 1] = { };

static void fbtft_device_pdev_release(struct device *dev)
{
/* Needed to silence this message:
 * Device 'xxx' does not have a release() function,
 * it is broken and must be fixed
 */
}

static int spi_device_found(struct device *dev, void *data)
{
	struct spi_device *spi = to_spi_device(dev);

	dev_info(dev, "%s %s %dkHz %d bits mode=0x%02X\n", spi->modalias,
		 dev_name(dev), spi->max_speed_hz / 1000, spi->bits_per_word,
		 spi->mode);

	return 0;
}

static void pr_spi_devices(void)
{
	pr_debug("SPI devices registered:\n");
	bus_for_each_dev(&spi_bus_type, NULL, NULL, spi_device_found);
}

static int p_device_found(struct device *dev, void *data)
{
	struct platform_device
	*pdev = to_platform_device(dev);

	if (strstr(pdev->name, "fb"))
		dev_info(dev, "%s id=%d pdata? %s\n", pdev->name, pdev->id,
			 pdev->dev.platform_data ? "yes" : "no");

	return 0;
}

static void pr_p_devices(void)
{
	pr_debug("'fb' Platform devices registered:\n");
	bus_for_each_dev(&platform_bus_type, NULL, NULL, p_device_found);
}

#ifdef MODULE
static void fbtft_device_spi_delete(struct spi_master *master, unsigned int cs)
{
	struct device *dev;
	char str[32];

	snprintf(str, sizeof(str), "%s.%u", dev_name(&master->dev), cs);

	dev = bus_find_device_by_name(&spi_bus_type, NULL, str);
	if (dev) {
		if (verbose)
			dev_info(dev, "Deleting %s\n", str);
		device_del(dev);
	}
}

static int fbtft_device_spi_device_register(struct spi_board_info *spi)
{
	struct spi_master *master;

	master = spi_busnum_to_master(spi->bus_num);
	if (!master) {
		pr_err("spi_busnum_to_master(%d) returned NULL\n",
		       spi->bus_num);
		return -EINVAL;
	}
	/* make sure it's available */
	fbtft_device_spi_delete(master, spi->chip_select);
	spi_device = spi_new_device(master, spi);
	put_device(&master->dev);
	if (!spi_device) {
		dev_err(&master->dev, "spi_new_device() returned NULL\n");
		return -EPERM;
	}
	return 0;
}
#else
static int fbtft_device_spi_device_register(struct spi_board_info *spi)
{
	return spi_register_board_info(spi, 1);
}
#endif

static int __init fbtft_device_init(void)
{
	struct spi_board_info *spi = NULL;
	struct fbtft_platform_data *pdata;
	const struct fbtft_gpio *gpio = NULL;
	char *p_gpio, *p_name, *p_num;
	bool found = false;
	int i = 0;
	long val;
	int ret = 0;

	if (!name) {
#ifdef MODULE
		pr_err("missing module parameter: 'name'\n");
		return -EINVAL;
#else
		return 0;
#endif
	}

	if (init_num > FBTFT_MAX_INIT_SEQUENCE) {
		pr_err("init parameter: exceeded max array size: %d\n",
		       FBTFT_MAX_INIT_SEQUENCE);
		return -EINVAL;
	}

	/* parse module parameter: gpios */
	while ((p_gpio = strsep(&gpios, ","))) {
		if (!strchr(p_gpio, ':')) {
			pr_err("error: missing ':' in gpios parameter: %s\n",
			       p_gpio);
			return -EINVAL;
		}
		p_num = p_gpio;
		p_name = strsep(&p_num, ":");
		if (!p_name || !p_num) {
			pr_err("something bad happened parsing gpios parameter: %s\n",
			       p_gpio);
			return -EINVAL;
		}
		ret = kstrtol(p_num, 10, &val);
		if (ret) {
			pr_err("could not parse number in gpios parameter: %s:%s\n",
			       p_name, p_num);
			return -EINVAL;
		}
		strncpy(fbtft_device_param_gpios[i].name, p_name,
			FBTFT_GPIO_NAME_SIZE - 1);
		fbtft_device_param_gpios[i++].gpio = (int)val;
		if (i == MAX_GPIOS) {
			pr_err("gpios parameter: exceeded max array size: %d\n",
			       MAX_GPIOS);
			return -EINVAL;
		}
	}
	if (fbtft_device_param_gpios[0].name[0])
		gpio = fbtft_device_param_gpios;

	if (verbose > 2)
		pr_spi_devices(); /* print list of registered SPI devices */

	if (verbose > 2)
		pr_p_devices(); /* print list of 'fb' platform devices */

	pr_debug("name='%s', busnum=%d, cs=%d\n", name, busnum, cs);

	if (rotate > 0 && rotate < 4) {
		rotate = (4 - rotate) * 90;
		pr_warn("argument 'rotate' should be an angle. Values 1-3 is deprecated. Setting it to %d.\n",
			rotate);
	}
	if (rotate != 0 && rotate != 90 && rotate != 180 && rotate != 270) {
		pr_warn("argument 'rotate' illegal value: %d. Setting it to 0.\n",
			rotate);
		rotate = 0;
	}

	/* name=list lists all supported displays */
	if (strncmp(name, "list", FBTFT_GPIO_NAME_SIZE) == 0) {
		pr_info("Supported displays:\n");

		for (i = 0; i < ARRAY_SIZE(displays); i++)
			pr_info("%s\n", displays[i].name);
		return -ECANCELED;
	}

	if (custom) {
		i = ARRAY_SIZE(displays) - 1;
		displays[i].name = name;
		if (speed == 0) {
			displays[i].pdev->name = name;
			displays[i].spi = NULL;
		} else {
			strncpy(displays[i].spi->modalias, name, SPI_NAME_SIZE);
			displays[i].pdev = NULL;
		}
	}

	for (i = 0; i < ARRAY_SIZE(displays); i++) {
		if (strncmp(name, displays[i].name, 32) == 0) {
			if (displays[i].spi) {
				spi = displays[i].spi;
				spi->chip_select = cs;
				spi->bus_num = busnum;
				if (speed)
					spi->max_speed_hz = speed;
				if (mode != -1)
					spi->mode = mode;
				pdata = (void *)spi->platform_data;
			} else if (displays[i].pdev) {
				p_device = displays[i].pdev;
				pdata = p_device->dev.platform_data;
			} else {
				pr_err("broken displays array\n");
				return -EINVAL;
			}

			pdata->rotate = rotate;
			if (bgr == 0)
				pdata->bgr = false;
			else if (bgr == 1)
				pdata->bgr = true;
			if (startbyte)
				pdata->startbyte = startbyte;
			if (gamma)
				pdata->gamma = gamma;
			pdata->display.debug = debug;
			if (fps)
				pdata->fps = fps;
			if (txbuflen)
				pdata->txbuflen = txbuflen;
			if (init_num)
				pdata->display.init_sequence = init;
			if (gpio)
				pdata->gpios = gpio;
			if (custom) {
				pdata->display.width = width;
				pdata->display.height = height;
				pdata->display.buswidth = buswidth;
				pdata->display.backlight = 1;
			}

			if (displays[i].spi) {
				ret = fbtft_device_spi_device_register(spi);
				if (ret) {
					pr_err("failed to register SPI device\n");
					return ret;
				}
			} else {
				ret = platform_device_register(p_device);
				if (ret < 0) {
					pr_err("platform_device_register() returned %d\n",
					       ret);
					return ret;
				}
			}
			found = true;
			break;
		}
	}

	if (!found) {
		pr_err("display not supported: '%s'\n", name);
		return -EINVAL;
	}

	if (verbose && pdata && pdata->gpios) {
		gpio = pdata->gpios;
		pr_info("GPIOS used by '%s':\n", name);
		found = false;
		while (verbose && gpio->name[0]) {
			pr_info("'%s' = GPIO%d\n", gpio->name, gpio->gpio);
			gpio++;
			found = true;
		}
		if (!found)
			pr_info("(none)\n");
	}

	if (spi_device && (verbose > 1))
		pr_spi_devices();
	if (p_device && (verbose > 1))
		pr_p_devices();

	return 0;
}

static void __exit fbtft_device_exit(void)
{
	if (spi_device) {
		device_del(&spi_device->dev);
		kfree(spi_device);
	}

	if (p_device)
		platform_device_unregister(p_device);

}

arch_initcall(fbtft_device_init);
module_exit(fbtft_device_exit);

MODULE_DESCRIPTION("Add a FBTFT device.");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
