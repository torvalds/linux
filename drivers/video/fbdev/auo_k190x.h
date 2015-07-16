/*
 * Private common definitions for AUO-K190X framebuffer drivers
 *
 * Copyright (C) 2012 Heiko Stuebner <heiko@sntech.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * I80 interface specific defines
 */

#define AUOK190X_I80_CS			0x01
#define AUOK190X_I80_DC			0x02
#define AUOK190X_I80_WR			0x03
#define AUOK190X_I80_OE			0x04

/*
 * AUOK190x commands, common to both controllers
 */

#define AUOK190X_CMD_INIT		0x0000
#define AUOK190X_CMD_STANDBY		0x0001
#define AUOK190X_CMD_WAKEUP		0x0002
#define AUOK190X_CMD_TCON_RESET		0x0003
#define AUOK190X_CMD_DATA_STOP		0x1002
#define AUOK190X_CMD_LUT_START		0x1003
#define AUOK190X_CMD_DISP_REFRESH	0x1004
#define AUOK190X_CMD_DISP_RESET		0x1005
#define AUOK190X_CMD_PRE_DISPLAY_START	0x100D
#define AUOK190X_CMD_PRE_DISPLAY_STOP	0x100F
#define AUOK190X_CMD_FLASH_W		0x2000
#define AUOK190X_CMD_FLASH_E		0x2001
#define AUOK190X_CMD_FLASH_STS		0x2002
#define AUOK190X_CMD_FRAMERATE		0x3000
#define AUOK190X_CMD_READ_VERSION	0x4000
#define AUOK190X_CMD_READ_STATUS	0x4001
#define AUOK190X_CMD_READ_LUT		0x4003
#define AUOK190X_CMD_DRIVERTIMING	0x5000
#define AUOK190X_CMD_LBALANCE		0x5001
#define AUOK190X_CMD_AGINGMODE		0x6000
#define AUOK190X_CMD_AGINGEXIT		0x6001

/*
 * Common settings for AUOK190X_CMD_INIT
 */

#define AUOK190X_INIT_DATA_FILTER	(0 << 12)
#define AUOK190X_INIT_DATA_BYPASS	(1 << 12)
#define AUOK190X_INIT_INVERSE_WHITE	(0 << 9)
#define AUOK190X_INIT_INVERSE_BLACK	(1 << 9)
#define AUOK190X_INIT_SCAN_DOWN		(0 << 1)
#define AUOK190X_INIT_SCAN_UP		(1 << 1)
#define AUOK190X_INIT_SHIFT_LEFT	(0 << 0)
#define AUOK190X_INIT_SHIFT_RIGHT	(1 << 0)

/* Common bits to pixels
 *   Mode	15-12	11-8	7-4	3-0
 *   format0	4	3	2	1
 *   format1	3	4	1	2
 */

#define AUOK190X_INIT_FORMAT0		0
#define AUOK190X_INIT_FORMAT1		(1 << 6)

/*
 * settings for AUOK190X_CMD_RESET
 */

#define AUOK190X_RESET_TCON		(0 << 0)
#define AUOK190X_RESET_NORMAL		(1 << 0)
#define AUOK190X_RESET_PON		(1 << 1)

/*
 * AUOK190X_CMD_VERSION
 */

#define AUOK190X_VERSION_TEMP_MASK		(0x1ff)
#define AUOK190X_VERSION_EPD_MASK		(0xff)
#define AUOK190X_VERSION_SIZE_INT(_val)		((_val & 0xfc00) >> 10)
#define AUOK190X_VERSION_SIZE_FLOAT(_val)	((_val & 0x3c0) >> 6)
#define AUOK190X_VERSION_MODEL(_val)		(_val & 0x3f)
#define AUOK190X_VERSION_LUT(_val)		(_val & 0xff)
#define AUOK190X_VERSION_TCON(_val)		((_val & 0xff00) >> 8)

/*
 * update modes for CMD_PARTIALDISP on K1900 and CMD_DDMA on K1901
 */

#define AUOK190X_UPDATE_MODE(_res)		((_res & 0x7) << 12)
#define AUOK190X_UPDATE_NONFLASH		(1 << 15)

/*
 * track panel specific parameters for common init
 */

struct auok190x_init_data {
	char *id;
	struct auok190x_board *board;

	void (*update_partial)(struct auok190xfb_par *par, u16 y1, u16 y2);
	void (*update_all)(struct auok190xfb_par *par);
	bool (*need_refresh)(struct auok190xfb_par *par);
	void (*init)(struct auok190xfb_par *par);
};


extern void auok190x_send_command_nowait(struct auok190xfb_par *par, u16 data);
extern int auok190x_send_command(struct auok190xfb_par *par, u16 data);
extern void auok190x_send_cmdargs_nowait(struct auok190xfb_par *par, u16 cmd,
					 int argc, u16 *argv);
extern int auok190x_send_cmdargs(struct auok190xfb_par *par, u16 cmd,
				  int argc, u16 *argv);
extern void auok190x_send_cmdargs_pixels_nowait(struct auok190xfb_par *par,
						u16 cmd, int argc, u16 *argv,
						int size, u16 *data);
extern int auok190x_send_cmdargs_pixels(struct auok190xfb_par *par, u16 cmd,
					int argc, u16 *argv, int size,
					u16 *data);
extern int auok190x_read_cmdargs(struct auok190xfb_par *par, u16 cmd,
				  int argc, u16 *argv);

extern int auok190x_common_probe(struct platform_device *pdev,
				 struct auok190x_init_data *init);
extern int auok190x_common_remove(struct platform_device *pdev);

extern const struct dev_pm_ops auok190x_pm;
