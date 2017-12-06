/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __COMMON_H__
#define __COMMON_H__

#define MAX_FPS_OPTIONS_SUPPORTED	3
#define I2C_MSG_LENGTH		0x2
#define E2PROM_2ADDR 0x80000000
#define E2PROM_ADDR_MASK 0x7fffffff

/* Defines for register writes and register array processing */
#define IMX_BYTE_MAX	32
#define IMX_SHORT_MAX	16
#define I2C_RETRY_COUNT		5
#define IMX_TOK_MASK	0xfff0

enum imx_tok_type {
	IMX_8BIT  = 0x0001,
	IMX_16BIT = 0x0002,
	IMX_TOK_TERM   = 0xf000,	/* terminating token for reg list */
	IMX_TOK_DELAY  = 0xfe00	/* delay token for reg list */
};

/**
 * struct imx_reg - MI sensor  register format
 * @type: type of the register
 * @reg: 16-bit offset to register
 * @val: 8/16/32-bit register value
 *
 * Define a structure for sensor register initialization values
 */
struct imx_reg {
	enum imx_tok_type type;
	u16 sreg;
	u32 val;	/* @set value for read/mod/write, @mask */
};

struct imx_fps_setting {
	int fps;
	unsigned short pixels_per_line;
	unsigned short lines_per_frame;
	int mipi_freq;			/* MIPI lane frequency in kHz */
	const struct imx_reg *regs; /* regs that the fps setting needs */
};

struct imx_resolution {
	const struct imx_fps_setting fps_options[MAX_FPS_OPTIONS_SUPPORTED];
	u8 *desc;
	const struct imx_reg *regs;
	int res;
	int width;
	int height;
	int fps;
	unsigned short pixels_per_line;
	unsigned short lines_per_frame;
	int mipi_freq;			/* MIPI lane frequency in kHz */
	unsigned short skip_frames;
	u8 bin_factor_x;
	u8 bin_factor_y;
	bool used;
};

#define GROUPED_PARAMETER_HOLD_ENABLE  {IMX_8BIT, 0x0104, 0x1}
#define GROUPED_PARAMETER_HOLD_DISABLE  {IMX_8BIT, 0x0104, 0x0}

int imx_write_reg(struct i2c_client *client, u16 data_length, u16 reg, u16 val);
#endif
