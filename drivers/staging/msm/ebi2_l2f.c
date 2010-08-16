/* Copyright (c) 2008-2009, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "msm_fb.h"

#include <linux/memory.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include "linux/proc_fs.h"

#include <linux/delay.h>

#include <mach/hardware.h>
#include <linux/io.h>

#include <asm/system.h>
#include <asm/mach-types.h>

/* The following are for MSM5100 on Gator
*/
#ifdef FEATURE_PM1000
#include "pm1000.h"
#endif /* FEATURE_PM1000 */
/* The following are for MSM6050 on Bambi
*/
#ifdef FEATURE_PMIC_LCDKBD_LED_DRIVER
#include "pm.h"
#endif /* FEATURE_PMIC_LCDKBD_LED_DRIVER */

#ifdef DISP_DEVICE_18BPP
#undef DISP_DEVICE_18BPP
#define DISP_DEVICE_16BPP
#endif

#define QCIF_WIDTH        176
#define QCIF_HEIGHT       220

static void *DISP_CMD_PORT;
static void *DISP_DATA_PORT;

#define DISP_CMD_DISON    0xaf
#define DISP_CMD_DISOFF   0xae
#define DISP_CMD_DISNOR   0xa6
#define DISP_CMD_DISINV   0xa7
#define DISP_CMD_DISCTL   0xca
#define DISP_CMD_GCP64    0xcb
#define DISP_CMD_GCP16    0xcc
#define DISP_CMD_GSSET    0xcd
#define DISP_GS_2       0x02
#define DISP_GS_16      0x01
#define DISP_GS_64      0x00
#define DISP_CMD_SLPIN    0x95
#define DISP_CMD_SLPOUT   0x94
#define DISP_CMD_SD_PSET  0x75
#define DISP_CMD_MD_PSET  0x76
#define DISP_CMD_SD_CSET  0x15
#define DISP_CMD_MD_CSET  0x16
#define DISP_CMD_DATCTL   0xbc
#define DISP_DATCTL_666 0x08
#define DISP_DATCTL_565 0x28
#define DISP_DATCTL_444 0x38
#define DISP_CMD_RAMWR    0x5c
#define DISP_CMD_RAMRD    0x5d
#define DISP_CMD_PTLIN    0xa8
#define DISP_CMD_PTLOUT   0xa9
#define DISP_CMD_ASCSET   0xaa
#define DISP_CMD_SCSTART  0xab
#define DISP_CMD_VOLCTL   0xc6
#define DISP_VOLCTL_TONE 0x80
#define DISP_CMD_NOp      0x25
#define DISP_CMD_OSSEL    0xd0
#define DISP_CMD_3500KSET 0xd1
#define DISP_CMD_3500KEND 0xd2
#define DISP_CMD_14MSET   0xd3
#define DISP_CMD_14MEND   0xd4

#define DISP_CMD_OUT(cmd) outpw(DISP_CMD_PORT, cmd);

#define DISP_DATA_OUT(data) outpw(DISP_DATA_PORT, data);

#define DISP_DATA_IN() inpw(DISP_DATA_PORT);

/* Epson device column number starts at 2
*/
#define DISP_SET_RECT(ulhc_row, lrhc_row, ulhc_col, lrhc_col) \
	  DISP_CMD_OUT(DISP_CMD_SD_PSET) \
	  DISP_DATA_OUT((ulhc_row) & 0xFF) \
	  DISP_DATA_OUT((ulhc_row) >> 8) \
	  DISP_DATA_OUT((lrhc_row) & 0xFF) \
	  DISP_DATA_OUT((lrhc_row) >> 8) \
	  DISP_CMD_OUT(DISP_CMD_SD_CSET) \
	  DISP_DATA_OUT(((ulhc_col)+2) & 0xFF) \
	  DISP_DATA_OUT(((ulhc_col)+2) >> 8) \
	  DISP_DATA_OUT(((lrhc_col)+2) & 0xFF) \
	  DISP_DATA_OUT(((lrhc_col)+2) >> 8)

#define DISP_MIN_CONTRAST      0
#define DISP_MAX_CONTRAST      127
#define DISP_DEFAULT_CONTRAST  80

#define DISP_MIN_BACKLIGHT     0
#define DISP_MAX_BACKLIGHT     15
#define DISP_DEFAULT_BACKLIGHT 2

#define WAIT_SEC(sec) mdelay((sec)/1000)

static word disp_area_start_row;
static word disp_area_end_row;
static byte disp_contrast = DISP_DEFAULT_CONTRAST;
static boolean disp_powered_up;
static boolean disp_initialized = FALSE;
/* For some reason the contrast set at init time is not good. Need to do
 * it again
 */
static boolean display_on = FALSE;
static void epsonQcif_disp_init(struct platform_device *pdev);
static void epsonQcif_disp_set_contrast(word contrast);
static void epsonQcif_disp_set_display_area(word start_row, word end_row);
static int epsonQcif_disp_off(struct platform_device *pdev);
static int epsonQcif_disp_on(struct platform_device *pdev);
static void epsonQcif_disp_set_rect(int x, int y, int xres, int yres);

volatile word databack;
static void epsonQcif_disp_init(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;

	int i;

	if (disp_initialized)
		return;

	mfd = platform_get_drvdata(pdev);

	DISP_CMD_PORT = mfd->cmd_port;
	DISP_DATA_PORT = mfd->data_port;

	/* Sleep in */
	DISP_CMD_OUT(DISP_CMD_SLPIN);

	/* Display off */
	DISP_CMD_OUT(DISP_CMD_DISOFF);

	/* Display normal */
	DISP_CMD_OUT(DISP_CMD_DISNOR);

	/* Set data mode */
	DISP_CMD_OUT(DISP_CMD_DATCTL);
	DISP_DATA_OUT(DISP_DATCTL_565);

	/* Set display timing */
	DISP_CMD_OUT(DISP_CMD_DISCTL);
	DISP_DATA_OUT(0x1c);	/* p1 */
	DISP_DATA_OUT(0x02);	/* p1 */
	DISP_DATA_OUT(0x82);	/* p2 */
	DISP_DATA_OUT(0x00);	/* p3 */
	DISP_DATA_OUT(0x00);	/* p4 */
	DISP_DATA_OUT(0xe0);	/* p5 */
	DISP_DATA_OUT(0x00);	/* p5 */
	DISP_DATA_OUT(0xdc);	/* p6 */
	DISP_DATA_OUT(0x00);	/* p6 */
	DISP_DATA_OUT(0x02);	/* p7 */
	DISP_DATA_OUT(0x00);	/* p8 */

	/* Set 64 gray scale level */
	DISP_CMD_OUT(DISP_CMD_GCP64);
	DISP_DATA_OUT(0x08);	/* p01 */
	DISP_DATA_OUT(0x00);
	DISP_DATA_OUT(0x2a);	/* p02 */
	DISP_DATA_OUT(0x00);
	DISP_DATA_OUT(0x4e);	/* p03 */
	DISP_DATA_OUT(0x00);
	DISP_DATA_OUT(0x6b);	/* p04 */
	DISP_DATA_OUT(0x00);
	DISP_DATA_OUT(0x88);	/* p05 */
	DISP_DATA_OUT(0x00);
	DISP_DATA_OUT(0xa3);	/* p06 */
	DISP_DATA_OUT(0x00);
	DISP_DATA_OUT(0xba);	/* p07 */
	DISP_DATA_OUT(0x00);
	DISP_DATA_OUT(0xd1);	/* p08 */
	DISP_DATA_OUT(0x00);
	DISP_DATA_OUT(0xe5);	/* p09 */
	DISP_DATA_OUT(0x00);
	DISP_DATA_OUT(0xf3);	/* p10 */
	DISP_DATA_OUT(0x00);
	DISP_DATA_OUT(0x03);	/* p11 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0x13);	/* p12 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0x22);	/* p13 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0x2f);	/* p14 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0x3b);	/* p15 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0x46);	/* p16 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0x51);	/* p17 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0x5b);	/* p18 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0x64);	/* p19 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0x6c);	/* p20 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0x74);	/* p21 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0x7c);	/* p22 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0x83);	/* p23 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0x8a);	/* p24 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0x91);	/* p25 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0x98);	/* p26 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0x9f);	/* p27 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0xa6);	/* p28 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0xac);	/* p29 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0xb2);	/* p30 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0xb7);	/* p31 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0xbc);	/* p32 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0xc1);	/* p33 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0xc6);	/* p34 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0xcb);	/* p35 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0xd0);	/* p36 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0xd4);	/* p37 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0xd8);	/* p38 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0xdc);	/* p39 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0xe0);	/* p40 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0xe4);	/* p41 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0xe8);	/* p42 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0xec);	/* p43 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0xf0);	/* p44 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0xf4);	/* p45 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0xf8);	/* p46 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0xfb);	/* p47 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0xfe);	/* p48 */
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0x01);	/* p49 */
	DISP_DATA_OUT(0x02);
	DISP_DATA_OUT(0x03);	/* p50 */
	DISP_DATA_OUT(0x02);
	DISP_DATA_OUT(0x05);	/* p51 */
	DISP_DATA_OUT(0x02);
	DISP_DATA_OUT(0x07);	/* p52 */
	DISP_DATA_OUT(0x02);
	DISP_DATA_OUT(0x09);	/* p53 */
	DISP_DATA_OUT(0x02);
	DISP_DATA_OUT(0x0b);	/* p54 */
	DISP_DATA_OUT(0x02);
	DISP_DATA_OUT(0x0d);	/* p55 */
	DISP_DATA_OUT(0x02);
	DISP_DATA_OUT(0x0f);	/* p56 */
	DISP_DATA_OUT(0x02);
	DISP_DATA_OUT(0x11);	/* p57 */
	DISP_DATA_OUT(0x02);
	DISP_DATA_OUT(0x13);	/* p58 */
	DISP_DATA_OUT(0x02);
	DISP_DATA_OUT(0x15);	/* p59 */
	DISP_DATA_OUT(0x02);
	DISP_DATA_OUT(0x17);	/* p60 */
	DISP_DATA_OUT(0x02);
	DISP_DATA_OUT(0x19);	/* p61 */
	DISP_DATA_OUT(0x02);
	DISP_DATA_OUT(0x1b);	/* p62 */
	DISP_DATA_OUT(0x02);
	DISP_DATA_OUT(0x1c);	/* p63 */
	DISP_DATA_OUT(0x02);

	/* Set 16 gray scale level */
	DISP_CMD_OUT(DISP_CMD_GCP16);
	DISP_DATA_OUT(0x1a);	/* p01 */
	DISP_DATA_OUT(0x32);	/* p02 */
	DISP_DATA_OUT(0x42);	/* p03 */
	DISP_DATA_OUT(0x4c);	/* p04 */
	DISP_DATA_OUT(0x58);	/* p05 */
	DISP_DATA_OUT(0x5f);	/* p06 */
	DISP_DATA_OUT(0x66);	/* p07 */
	DISP_DATA_OUT(0x6b);	/* p08 */
	DISP_DATA_OUT(0x70);	/* p09 */
	DISP_DATA_OUT(0x74);	/* p10 */
	DISP_DATA_OUT(0x78);	/* p11 */
	DISP_DATA_OUT(0x7b);	/* p12 */
	DISP_DATA_OUT(0x7e);	/* p13 */
	DISP_DATA_OUT(0x80);	/* p14 */
	DISP_DATA_OUT(0x82);	/* p15 */

	/* Set DSP column */
	DISP_CMD_OUT(DISP_CMD_MD_CSET);
	DISP_DATA_OUT(0xff);
	DISP_DATA_OUT(0x03);
	DISP_DATA_OUT(0xff);
	DISP_DATA_OUT(0x03);

	/* Set DSP page */
	DISP_CMD_OUT(DISP_CMD_MD_PSET);
	DISP_DATA_OUT(0xff);
	DISP_DATA_OUT(0x01);
	DISP_DATA_OUT(0xff);
	DISP_DATA_OUT(0x01);

	/* Set ARM column */
	DISP_CMD_OUT(DISP_CMD_SD_CSET);
	DISP_DATA_OUT(0x02);
	DISP_DATA_OUT(0x00);
	DISP_DATA_OUT((QCIF_WIDTH + 1) & 0xFF);
	DISP_DATA_OUT((QCIF_WIDTH + 1) >> 8);

	/* Set ARM page */
	DISP_CMD_OUT(DISP_CMD_SD_PSET);
	DISP_DATA_OUT(0x00);
	DISP_DATA_OUT(0x00);
	DISP_DATA_OUT((QCIF_HEIGHT - 1) & 0xFF);
	DISP_DATA_OUT((QCIF_HEIGHT - 1) >> 8);

	/* Set 64 gray scales */
	DISP_CMD_OUT(DISP_CMD_GSSET);
	DISP_DATA_OUT(DISP_GS_64);

	DISP_CMD_OUT(DISP_CMD_OSSEL);
	DISP_DATA_OUT(0);

	/* Sleep out */
	DISP_CMD_OUT(DISP_CMD_SLPOUT);

	WAIT_SEC(40000);

	/* Initialize power IC */
	DISP_CMD_OUT(DISP_CMD_VOLCTL);
	DISP_DATA_OUT(DISP_VOLCTL_TONE);

	WAIT_SEC(40000);

	/* Set electronic volume, d'xx */
	DISP_CMD_OUT(DISP_CMD_VOLCTL);
	DISP_DATA_OUT(DISP_DEFAULT_CONTRAST);	/* value from 0 to 127 */

	/* Initialize display data */
	DISP_SET_RECT(0, (QCIF_HEIGHT - 1), 0, (QCIF_WIDTH - 1));
	DISP_CMD_OUT(DISP_CMD_RAMWR);
	for (i = 0; i < QCIF_HEIGHT * QCIF_WIDTH; i++)
		DISP_DATA_OUT(0xffff);

	DISP_CMD_OUT(DISP_CMD_RAMRD);
	databack = DISP_DATA_IN();
	databack = DISP_DATA_IN();
	databack = DISP_DATA_IN();
	databack = DISP_DATA_IN();

	WAIT_SEC(80000);

	DISP_CMD_OUT(DISP_CMD_DISON);

	disp_area_start_row = 0;
	disp_area_end_row = QCIF_HEIGHT - 1;
	disp_powered_up = TRUE;
	disp_initialized = TRUE;
	epsonQcif_disp_set_display_area(0, QCIF_HEIGHT - 1);
	display_on = TRUE;
}

static void epsonQcif_disp_set_rect(int x, int y, int xres, int yres)
{
	if (!disp_initialized)
		return;

	DISP_SET_RECT(y, y + yres - 1, x, x + xres - 1);
	DISP_CMD_OUT(DISP_CMD_RAMWR);
}

static void epsonQcif_disp_set_display_area(word start_row, word end_row)
{
	if (!disp_initialized)
		return;

	if ((start_row == disp_area_start_row)
	    && (end_row == disp_area_end_row))
		return;
	disp_area_start_row = start_row;
	disp_area_end_row = end_row;

	/* Range checking
	 */
	if (end_row >= QCIF_HEIGHT)
		end_row = QCIF_HEIGHT - 1;
	if (start_row > end_row)
		start_row = end_row;

	/* When display is not the full screen, gray scale is set to
	 ** 2; otherwise it is set to 64.
	 */
	if ((start_row == 0) && (end_row == (QCIF_HEIGHT - 1))) {
		/* The whole screen */
		DISP_CMD_OUT(DISP_CMD_PTLOUT);
		WAIT_SEC(10000);
		DISP_CMD_OUT(DISP_CMD_DISOFF);
		WAIT_SEC(100000);
		DISP_CMD_OUT(DISP_CMD_GSSET);
		DISP_DATA_OUT(DISP_GS_64);
		WAIT_SEC(100000);
		DISP_CMD_OUT(DISP_CMD_DISON);
	} else {
		/* partial screen */
		DISP_CMD_OUT(DISP_CMD_PTLIN);
		DISP_DATA_OUT(start_row);
		DISP_DATA_OUT(start_row >> 8);
		DISP_DATA_OUT(end_row);
		DISP_DATA_OUT(end_row >> 8);
		DISP_CMD_OUT(DISP_CMD_GSSET);
		DISP_DATA_OUT(DISP_GS_2);
	}
}

static int epsonQcif_disp_off(struct platform_device *pdev)
{
	if (!disp_initialized)
		epsonQcif_disp_init(pdev);

	if (display_on) {
		DISP_CMD_OUT(DISP_CMD_DISOFF);
		DISP_CMD_OUT(DISP_CMD_SLPIN);
		display_on = FALSE;
	}

	return 0;
}

static int epsonQcif_disp_on(struct platform_device *pdev)
{
	if (!disp_initialized)
		epsonQcif_disp_init(pdev);

	if (!display_on) {
		DISP_CMD_OUT(DISP_CMD_SLPOUT);
		WAIT_SEC(40000);
		DISP_CMD_OUT(DISP_CMD_DISON);
		epsonQcif_disp_set_contrast(disp_contrast);
		display_on = TRUE;
	}

	return 0;
}

static void epsonQcif_disp_set_contrast(word contrast)
{
	if (!disp_initialized)
		return;

	/* Initialize power IC, d'24 */
	DISP_CMD_OUT(DISP_CMD_VOLCTL);
	DISP_DATA_OUT(DISP_VOLCTL_TONE);

	WAIT_SEC(40000);

	/* Set electronic volume, d'xx */
	DISP_CMD_OUT(DISP_CMD_VOLCTL);
	if (contrast > 127)
		contrast = 127;
	DISP_DATA_OUT(contrast);	/* value from 0 to 127 */
	disp_contrast = (byte) contrast;
}				/* End disp_set_contrast */

static void epsonQcif_disp_clear_screen_area(
	word start_row, word end_row, word start_column, word end_column) {
	int32 i;

	/* Clear the display screen */
	DISP_SET_RECT(start_row, end_row, start_column, end_column);
	DISP_CMD_OUT(DISP_CMD_RAMWR);
	i = (end_row - start_row + 1) * (end_column - start_column + 1);
	for (; i > 0; i--)
		DISP_DATA_OUT(0xffff);
}

static int __init epsonQcif_probe(struct platform_device *pdev)
{
	msm_fb_add_device(pdev);

	return 0;
}

static struct platform_driver this_driver = {
	.probe  = epsonQcif_probe,
	.driver = {
		.name   = "ebi2_epson_qcif",
	},
};

static struct msm_fb_panel_data epsonQcif_panel_data = {
	.on = epsonQcif_disp_on,
	.off = epsonQcif_disp_off,
	.set_rect = epsonQcif_disp_set_rect,
};

static struct platform_device this_device = {
	.name   = "ebi2_epson_qcif",
	.id	= 0,
	.dev	= {
		.platform_data = &epsonQcif_panel_data,
	}
};

static int __init epsonQcif_init(void)
{
	int ret;
	struct msm_panel_info *pinfo;

	ret = platform_driver_register(&this_driver);
	if (!ret) {
		pinfo = &epsonQcif_panel_data.panel_info;
		pinfo->xres = QCIF_WIDTH;
		pinfo->yres = QCIF_HEIGHT;
		pinfo->type = EBI2_PANEL;
		pinfo->pdest = DISPLAY_2;
		pinfo->wait_cycle = 0x808000;
		pinfo->bpp = 16;
		pinfo->fb_num = 2;
		pinfo->lcd.vsync_enable = FALSE;

		ret = platform_device_register(&this_device);
		if (ret)
			platform_driver_unregister(&this_driver);
	}

	return ret;
}

module_init(epsonQcif_init);
