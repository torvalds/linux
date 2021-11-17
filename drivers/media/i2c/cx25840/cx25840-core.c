// SPDX-License-Identifier: GPL-2.0-or-later
/* cx25840 - Conexant CX25840 audio/video decoder driver
 *
 * Copyright (C) 2004 Ulf Eklund
 *
 * Based on the saa7115 driver and on the first version of Chris Kennedy's
 * cx25840 driver.
 *
 * Changes by Tyler Trafford <tatrafford@comcast.net>
 *    - cleanup/rewrite for V4L2 API (2005)
 *
 * VBI support by Hans Verkuil <hverkuil@xs4all.nl>.
 *
 * NTSC sliced VBI support by Christopher Neufeld <television@cneufeld.ca>
 * with additional fixes by Hans Verkuil <hverkuil@xs4all.nl>.
 *
 * CX23885 support by Steven Toth <stoth@linuxtv.org>.
 *
 * CX2388[578] IRQ handling, IO Pin mux configuration and other small fixes are
 * Copyright (C) 2010 Andy Walls <awalls@md.metrocast.net>
 *
 * CX23888 DIF support for the HVR1850
 * Copyright (C) 2011 Steven Toth <stoth@kernellabs.com>
 *
 * CX2584x pin to pad mapping and output format configuration support are
 * Copyright (C) 2011 Maciej S. Szmigiero <mail@maciej.szmigiero.name>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/math64.h>
#include <media/v4l2-common.h>
#include <media/drv-intf/cx25840.h>

#include "cx25840-core.h"

MODULE_DESCRIPTION("Conexant CX25840 audio/video decoder driver");
MODULE_AUTHOR("Ulf Eklund, Chris Kennedy, Hans Verkuil, Tyler Trafford");
MODULE_LICENSE("GPL");

#define CX25840_VID_INT_STAT_REG 0x410
#define CX25840_VID_INT_STAT_BITS 0x0000ffff
#define CX25840_VID_INT_MASK_BITS 0xffff0000
#define CX25840_VID_INT_MASK_SHFT 16
#define CX25840_VID_INT_MASK_REG 0x412

#define CX23885_AUD_MC_INT_MASK_REG 0x80c
#define CX23885_AUD_MC_INT_STAT_BITS 0xffff0000
#define CX23885_AUD_MC_INT_CTRL_BITS 0x0000ffff
#define CX23885_AUD_MC_INT_STAT_SHFT 16

#define CX25840_AUD_INT_CTRL_REG 0x812
#define CX25840_AUD_INT_STAT_REG 0x813

#define CX23885_PIN_CTRL_IRQ_REG 0x123
#define CX23885_PIN_CTRL_IRQ_IR_STAT  0x40
#define CX23885_PIN_CTRL_IRQ_AUD_STAT 0x20
#define CX23885_PIN_CTRL_IRQ_VID_STAT 0x10

#define CX25840_IR_STATS_REG	0x210
#define CX25840_IR_IRQEN_REG	0x214

static int cx25840_debug;

module_param_named(debug, cx25840_debug, int, 0644);

MODULE_PARM_DESC(debug, "Debugging messages [0=Off (default) 1=On]");

/* ----------------------------------------------------------------------- */
static void cx23888_std_setup(struct i2c_client *client);

int cx25840_write(struct i2c_client *client, u16 addr, u8 value)
{
	u8 buffer[3];

	buffer[0] = addr >> 8;
	buffer[1] = addr & 0xff;
	buffer[2] = value;
	return i2c_master_send(client, buffer, 3);
}

int cx25840_write4(struct i2c_client *client, u16 addr, u32 value)
{
	u8 buffer[6];

	buffer[0] = addr >> 8;
	buffer[1] = addr & 0xff;
	buffer[2] = value & 0xff;
	buffer[3] = (value >> 8) & 0xff;
	buffer[4] = (value >> 16) & 0xff;
	buffer[5] = value >> 24;
	return i2c_master_send(client, buffer, 6);
}

u8 cx25840_read(struct i2c_client *client, u16 addr)
{
	struct i2c_msg msgs[2];
	u8 tx_buf[2], rx_buf[1];

	/* Write register address */
	tx_buf[0] = addr >> 8;
	tx_buf[1] = addr & 0xff;
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = (char *)tx_buf;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = 1;
	msgs[1].buf = (char *)rx_buf;

	if (i2c_transfer(client->adapter, msgs, 2) < 2)
		return 0;

	return rx_buf[0];
}

u32 cx25840_read4(struct i2c_client *client, u16 addr)
{
	struct i2c_msg msgs[2];
	u8 tx_buf[2], rx_buf[4];

	/* Write register address */
	tx_buf[0] = addr >> 8;
	tx_buf[1] = addr & 0xff;
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = (char *)tx_buf;

	/* Read data from registers */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = 4;
	msgs[1].buf = (char *)rx_buf;

	if (i2c_transfer(client->adapter, msgs, 2) < 2)
		return 0;

	return (rx_buf[3] << 24) | (rx_buf[2] << 16) | (rx_buf[1] << 8) |
		rx_buf[0];
}

int cx25840_and_or(struct i2c_client *client, u16 addr, unsigned int and_mask,
		   u8 or_value)
{
	return cx25840_write(client, addr,
			     (cx25840_read(client, addr) & and_mask) |
			     or_value);
}

int cx25840_and_or4(struct i2c_client *client, u16 addr, u32 and_mask,
		    u32 or_value)
{
	return cx25840_write4(client, addr,
			      (cx25840_read4(client, addr) & and_mask) |
			      or_value);
}

/* ----------------------------------------------------------------------- */

static int set_input(struct i2c_client *client,
		     enum cx25840_video_input vid_input,
		     enum cx25840_audio_input aud_input);

/* ----------------------------------------------------------------------- */

static int cx23885_s_io_pin_config(struct v4l2_subdev *sd, size_t n,
				   struct v4l2_subdev_io_pin_config *p)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int i;
	u32 pin_ctrl;
	u8 gpio_oe, gpio_data, strength;

	pin_ctrl = cx25840_read4(client, 0x120);
	gpio_oe = cx25840_read(client, 0x160);
	gpio_data = cx25840_read(client, 0x164);

	for (i = 0; i < n; i++) {
		strength = p[i].strength;
		if (strength > CX25840_PIN_DRIVE_FAST)
			strength = CX25840_PIN_DRIVE_FAST;

		switch (p[i].pin) {
		case CX23885_PIN_IRQ_N_GPIO16:
			if (p[i].function != CX23885_PAD_IRQ_N) {
				/* GPIO16 */
				pin_ctrl &= ~(0x1 << 25);
			} else {
				/* IRQ_N */
				if (p[i].flags &
					(BIT(V4L2_SUBDEV_IO_PIN_DISABLE) |
					 BIT(V4L2_SUBDEV_IO_PIN_INPUT))) {
					pin_ctrl &= ~(0x1 << 25);
				} else {
					pin_ctrl |= (0x1 << 25);
				}
				if (p[i].flags &
					BIT(V4L2_SUBDEV_IO_PIN_ACTIVE_LOW)) {
					pin_ctrl &= ~(0x1 << 24);
				} else {
					pin_ctrl |= (0x1 << 24);
				}
			}
			break;
		case CX23885_PIN_IR_RX_GPIO19:
			if (p[i].function != CX23885_PAD_GPIO19) {
				/* IR_RX */
				gpio_oe |= (0x1 << 0);
				pin_ctrl &= ~(0x3 << 18);
				pin_ctrl |= (strength << 18);
			} else {
				/* GPIO19 */
				gpio_oe &= ~(0x1 << 0);
				if (p[i].flags & BIT(V4L2_SUBDEV_IO_PIN_SET_VALUE)) {
					gpio_data &= ~(0x1 << 0);
					gpio_data |= ((p[i].value & 0x1) << 0);
				}
				pin_ctrl &= ~(0x3 << 12);
				pin_ctrl |= (strength << 12);
			}
			break;
		case CX23885_PIN_IR_TX_GPIO20:
			if (p[i].function != CX23885_PAD_GPIO20) {
				/* IR_TX */
				gpio_oe |= (0x1 << 1);
				if (p[i].flags & BIT(V4L2_SUBDEV_IO_PIN_DISABLE))
					pin_ctrl &= ~(0x1 << 10);
				else
					pin_ctrl |= (0x1 << 10);
				pin_ctrl &= ~(0x3 << 18);
				pin_ctrl |= (strength << 18);
			} else {
				/* GPIO20 */
				gpio_oe &= ~(0x1 << 1);
				if (p[i].flags & BIT(V4L2_SUBDEV_IO_PIN_SET_VALUE)) {
					gpio_data &= ~(0x1 << 1);
					gpio_data |= ((p[i].value & 0x1) << 1);
				}
				pin_ctrl &= ~(0x3 << 12);
				pin_ctrl |= (strength << 12);
			}
			break;
		case CX23885_PIN_I2S_SDAT_GPIO21:
			if (p[i].function != CX23885_PAD_GPIO21) {
				/* I2S_SDAT */
				/* TODO: Input or Output config */
				gpio_oe |= (0x1 << 2);
				pin_ctrl &= ~(0x3 << 22);
				pin_ctrl |= (strength << 22);
			} else {
				/* GPIO21 */
				gpio_oe &= ~(0x1 << 2);
				if (p[i].flags & BIT(V4L2_SUBDEV_IO_PIN_SET_VALUE)) {
					gpio_data &= ~(0x1 << 2);
					gpio_data |= ((p[i].value & 0x1) << 2);
				}
				pin_ctrl &= ~(0x3 << 12);
				pin_ctrl |= (strength << 12);
			}
			break;
		case CX23885_PIN_I2S_WCLK_GPIO22:
			if (p[i].function != CX23885_PAD_GPIO22) {
				/* I2S_WCLK */
				/* TODO: Input or Output config */
				gpio_oe |= (0x1 << 3);
				pin_ctrl &= ~(0x3 << 22);
				pin_ctrl |= (strength << 22);
			} else {
				/* GPIO22 */
				gpio_oe &= ~(0x1 << 3);
				if (p[i].flags & BIT(V4L2_SUBDEV_IO_PIN_SET_VALUE)) {
					gpio_data &= ~(0x1 << 3);
					gpio_data |= ((p[i].value & 0x1) << 3);
				}
				pin_ctrl &= ~(0x3 << 12);
				pin_ctrl |= (strength << 12);
			}
			break;
		case CX23885_PIN_I2S_BCLK_GPIO23:
			if (p[i].function != CX23885_PAD_GPIO23) {
				/* I2S_BCLK */
				/* TODO: Input or Output config */
				gpio_oe |= (0x1 << 4);
				pin_ctrl &= ~(0x3 << 22);
				pin_ctrl |= (strength << 22);
			} else {
				/* GPIO23 */
				gpio_oe &= ~(0x1 << 4);
				if (p[i].flags & BIT(V4L2_SUBDEV_IO_PIN_SET_VALUE)) {
					gpio_data &= ~(0x1 << 4);
					gpio_data |= ((p[i].value & 0x1) << 4);
				}
				pin_ctrl &= ~(0x3 << 12);
				pin_ctrl |= (strength << 12);
			}
			break;
		}
	}

	cx25840_write(client, 0x164, gpio_data);
	cx25840_write(client, 0x160, gpio_oe);
	cx25840_write4(client, 0x120, pin_ctrl);
	return 0;
}

static u8 cx25840_function_to_pad(struct i2c_client *client, u8 function)
{
	if (function > CX25840_PAD_VRESET) {
		v4l_err(client, "invalid function %u, assuming default\n",
			(unsigned int)function);
		return 0;
	}

	return function;
}

static void cx25840_set_invert(u8 *pinctrl3, u8 *voutctrl4, u8 function,
			       u8 pin, bool invert)
{
	switch (function) {
	case CX25840_PAD_IRQ_N:
		if (invert)
			*pinctrl3 &= ~2;
		else
			*pinctrl3 |= 2;
		break;

	case CX25840_PAD_ACTIVE:
		if (invert)
			*voutctrl4 |= BIT(2);
		else
			*voutctrl4 &= ~BIT(2);
		break;

	case CX25840_PAD_VACTIVE:
		if (invert)
			*voutctrl4 |= BIT(5);
		else
			*voutctrl4 &= ~BIT(5);
		break;

	case CX25840_PAD_CBFLAG:
		if (invert)
			*voutctrl4 |= BIT(4);
		else
			*voutctrl4 &= ~BIT(4);
		break;

	case CX25840_PAD_VRESET:
		if (invert)
			*voutctrl4 |= BIT(0);
		else
			*voutctrl4 &= ~BIT(0);
		break;
	}

	if (function != CX25840_PAD_DEFAULT)
		return;

	switch (pin) {
	case CX25840_PIN_DVALID_PRGM0:
		if (invert)
			*voutctrl4 |= BIT(6);
		else
			*voutctrl4 &= ~BIT(6);
		break;

	case CX25840_PIN_HRESET_PRGM2:
		if (invert)
			*voutctrl4 |= BIT(1);
		else
			*voutctrl4 &= ~BIT(1);
		break;
	}
}

static int cx25840_s_io_pin_config(struct v4l2_subdev *sd, size_t n,
				   struct v4l2_subdev_io_pin_config *p)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	unsigned int i;
	u8 pinctrl[6], pinconf[10], voutctrl4;

	for (i = 0; i < 6; i++)
		pinctrl[i] = cx25840_read(client, 0x114 + i);

	for (i = 0; i < 10; i++)
		pinconf[i] = cx25840_read(client, 0x11c + i);

	voutctrl4 = cx25840_read(client, 0x407);

	for (i = 0; i < n; i++) {
		u8 strength = p[i].strength;

		if (strength != CX25840_PIN_DRIVE_SLOW &&
		    strength != CX25840_PIN_DRIVE_MEDIUM &&
		    strength != CX25840_PIN_DRIVE_FAST) {
			v4l_err(client,
				"invalid drive speed for pin %u (%u), assuming fast\n",
				(unsigned int)p[i].pin,
				(unsigned int)strength);

			strength = CX25840_PIN_DRIVE_FAST;
		}

		switch (p[i].pin) {
		case CX25840_PIN_DVALID_PRGM0:
			if (p[i].flags & BIT(V4L2_SUBDEV_IO_PIN_DISABLE))
				pinctrl[0] &= ~BIT(6);
			else
				pinctrl[0] |= BIT(6);

			pinconf[3] &= 0xf0;
			pinconf[3] |= cx25840_function_to_pad(client,
							      p[i].function);

			cx25840_set_invert(&pinctrl[3], &voutctrl4,
					   p[i].function,
					   CX25840_PIN_DVALID_PRGM0,
					   p[i].flags &
					   BIT(V4L2_SUBDEV_IO_PIN_ACTIVE_LOW));

			pinctrl[4] &= ~(3 << 2); /* CX25840_PIN_DRIVE_MEDIUM */
			switch (strength) {
			case CX25840_PIN_DRIVE_SLOW:
				pinctrl[4] |= 1 << 2;
				break;

			case CX25840_PIN_DRIVE_FAST:
				pinctrl[4] |= 2 << 2;
				break;
			}

			break;

		case CX25840_PIN_HRESET_PRGM2:
			if (p[i].flags & BIT(V4L2_SUBDEV_IO_PIN_DISABLE))
				pinctrl[1] &= ~BIT(0);
			else
				pinctrl[1] |= BIT(0);

			pinconf[4] &= 0xf0;
			pinconf[4] |= cx25840_function_to_pad(client,
							      p[i].function);

			cx25840_set_invert(&pinctrl[3], &voutctrl4,
					   p[i].function,
					   CX25840_PIN_HRESET_PRGM2,
					   p[i].flags &
					   BIT(V4L2_SUBDEV_IO_PIN_ACTIVE_LOW));

			pinctrl[4] &= ~(3 << 2); /* CX25840_PIN_DRIVE_MEDIUM */
			switch (strength) {
			case CX25840_PIN_DRIVE_SLOW:
				pinctrl[4] |= 1 << 2;
				break;

			case CX25840_PIN_DRIVE_FAST:
				pinctrl[4] |= 2 << 2;
				break;
			}

			break;

		case CX25840_PIN_PLL_CLK_PRGM7:
			if (p[i].flags & BIT(V4L2_SUBDEV_IO_PIN_DISABLE))
				pinctrl[2] &= ~BIT(2);
			else
				pinctrl[2] |= BIT(2);

			switch (p[i].function) {
			case CX25840_PAD_XTI_X5_DLL:
				pinconf[6] = 0;
				break;

			case CX25840_PAD_AUX_PLL:
				pinconf[6] = 1;
				break;

			case CX25840_PAD_VID_PLL:
				pinconf[6] = 5;
				break;

			case CX25840_PAD_XTI:
				pinconf[6] = 2;
				break;

			default:
				pinconf[6] = 3;
				pinconf[6] |=
					cx25840_function_to_pad(client,
								p[i].function)
					<< 4;
			}

			break;

		default:
			v4l_err(client, "invalid or unsupported pin %u\n",
				(unsigned int)p[i].pin);
			break;
		}
	}

	cx25840_write(client, 0x407, voutctrl4);

	for (i = 0; i < 6; i++)
		cx25840_write(client, 0x114 + i, pinctrl[i]);

	for (i = 0; i < 10; i++)
		cx25840_write(client, 0x11c + i, pinconf[i]);

	return 0;
}

static int common_s_io_pin_config(struct v4l2_subdev *sd, size_t n,
				  struct v4l2_subdev_io_pin_config *pincfg)
{
	struct cx25840_state *state = to_state(sd);

	if (is_cx2388x(state))
		return cx23885_s_io_pin_config(sd, n, pincfg);
	else if (is_cx2584x(state))
		return cx25840_s_io_pin_config(sd, n, pincfg);
	return 0;
}

/* ----------------------------------------------------------------------- */

static void init_dll1(struct i2c_client *client)
{
	/*
	 * This is the Hauppauge sequence used to
	 * initialize the Delay Lock Loop 1 (ADC DLL).
	 */
	cx25840_write(client, 0x159, 0x23);
	cx25840_write(client, 0x15a, 0x87);
	cx25840_write(client, 0x15b, 0x06);
	udelay(10);
	cx25840_write(client, 0x159, 0xe1);
	udelay(10);
	cx25840_write(client, 0x15a, 0x86);
	cx25840_write(client, 0x159, 0xe0);
	cx25840_write(client, 0x159, 0xe1);
	cx25840_write(client, 0x15b, 0x10);
}

static void init_dll2(struct i2c_client *client)
{
	/*
	 * This is the Hauppauge sequence used to
	 * initialize the Delay Lock Loop 2 (ADC DLL).
	 */
	cx25840_write(client, 0x15d, 0xe3);
	cx25840_write(client, 0x15e, 0x86);
	cx25840_write(client, 0x15f, 0x06);
	udelay(10);
	cx25840_write(client, 0x15d, 0xe1);
	cx25840_write(client, 0x15d, 0xe0);
	cx25840_write(client, 0x15d, 0xe1);
}

static void cx25836_initialize(struct i2c_client *client)
{
	/*
	 *reset configuration is described on page 3-77
	 * of the CX25836 datasheet
	 */

	/* 2. */
	cx25840_and_or(client, 0x000, ~0x01, 0x01);
	cx25840_and_or(client, 0x000, ~0x01, 0x00);
	/* 3a. */
	cx25840_and_or(client, 0x15a, ~0x70, 0x00);
	/* 3b. */
	cx25840_and_or(client, 0x15b, ~0x1e, 0x06);
	/* 3c. */
	cx25840_and_or(client, 0x159, ~0x02, 0x02);
	/* 3d. */
	udelay(10);
	/* 3e. */
	cx25840_and_or(client, 0x159, ~0x02, 0x00);
	/* 3f. */
	cx25840_and_or(client, 0x159, ~0xc0, 0xc0);
	/* 3g. */
	cx25840_and_or(client, 0x159, ~0x01, 0x00);
	cx25840_and_or(client, 0x159, ~0x01, 0x01);
	/* 3h. */
	cx25840_and_or(client, 0x15b, ~0x1e, 0x10);
}

static void cx25840_work_handler(struct work_struct *work)
{
	struct cx25840_state *state = container_of(work, struct cx25840_state, fw_work);

	cx25840_loadfw(state->c);
	wake_up(&state->fw_wait);
}

#define CX25840_VCONFIG_SET_BIT(state, opt_msk, voc, idx, bit, oneval)	\
	do {								\
		if ((state)->vid_config & (opt_msk)) {			\
			if (((state)->vid_config & (opt_msk)) ==	\
			    (oneval))					\
				(voc)[idx] |= BIT(bit);		\
			else						\
				(voc)[idx] &= ~BIT(bit);		\
		}							\
	} while (0)

/* apply current vconfig to hardware regs */
static void cx25840_vconfig_apply(struct i2c_client *client)
{
	struct cx25840_state *state = to_state(i2c_get_clientdata(client));
	u8 voutctrl[3];
	unsigned int i;

	for (i = 0; i < 3; i++)
		voutctrl[i] = cx25840_read(client, 0x404 + i);

	if (state->vid_config & CX25840_VCONFIG_FMT_MASK)
		voutctrl[0] &= ~3;
	switch (state->vid_config & CX25840_VCONFIG_FMT_MASK) {
	case CX25840_VCONFIG_FMT_BT656:
		voutctrl[0] |= 1;
		break;

	case CX25840_VCONFIG_FMT_VIP11:
		voutctrl[0] |= 2;
		break;

	case CX25840_VCONFIG_FMT_VIP2:
		voutctrl[0] |= 3;
		break;

	case CX25840_VCONFIG_FMT_BT601:
		/* zero */
	default:
		break;
	}

	CX25840_VCONFIG_SET_BIT(state, CX25840_VCONFIG_RES_MASK, voutctrl,
				0, 2, CX25840_VCONFIG_RES_10BIT);
	CX25840_VCONFIG_SET_BIT(state, CX25840_VCONFIG_VBIRAW_MASK, voutctrl,
				0, 3, CX25840_VCONFIG_VBIRAW_ENABLED);
	CX25840_VCONFIG_SET_BIT(state, CX25840_VCONFIG_ANCDATA_MASK, voutctrl,
				0, 4, CX25840_VCONFIG_ANCDATA_ENABLED);
	CX25840_VCONFIG_SET_BIT(state, CX25840_VCONFIG_TASKBIT_MASK, voutctrl,
				0, 5, CX25840_VCONFIG_TASKBIT_ONE);
	CX25840_VCONFIG_SET_BIT(state, CX25840_VCONFIG_ACTIVE_MASK, voutctrl,
				1, 2, CX25840_VCONFIG_ACTIVE_HORIZONTAL);
	CX25840_VCONFIG_SET_BIT(state, CX25840_VCONFIG_VALID_MASK, voutctrl,
				1, 3, CX25840_VCONFIG_VALID_ANDACTIVE);
	CX25840_VCONFIG_SET_BIT(state, CX25840_VCONFIG_HRESETW_MASK, voutctrl,
				1, 4, CX25840_VCONFIG_HRESETW_PIXCLK);

	if (state->vid_config & CX25840_VCONFIG_CLKGATE_MASK)
		voutctrl[1] &= ~(3 << 6);
	switch (state->vid_config & CX25840_VCONFIG_CLKGATE_MASK) {
	case CX25840_VCONFIG_CLKGATE_VALID:
		voutctrl[1] |= 2;
		break;

	case CX25840_VCONFIG_CLKGATE_VALIDACTIVE:
		voutctrl[1] |= 3;
		break;

	case CX25840_VCONFIG_CLKGATE_NONE:
		/* zero */
	default:
		break;
	}

	CX25840_VCONFIG_SET_BIT(state, CX25840_VCONFIG_DCMODE_MASK, voutctrl,
				2, 0, CX25840_VCONFIG_DCMODE_BYTES);
	CX25840_VCONFIG_SET_BIT(state, CX25840_VCONFIG_IDID0S_MASK, voutctrl,
				2, 1, CX25840_VCONFIG_IDID0S_LINECNT);
	CX25840_VCONFIG_SET_BIT(state, CX25840_VCONFIG_VIPCLAMP_MASK, voutctrl,
				2, 4, CX25840_VCONFIG_VIPCLAMP_ENABLED);

	for (i = 0; i < 3; i++)
		cx25840_write(client, 0x404 + i, voutctrl[i]);
}

static void cx25840_initialize(struct i2c_client *client)
{
	DEFINE_WAIT(wait);
	struct cx25840_state *state = to_state(i2c_get_clientdata(client));
	struct workqueue_struct *q;

	/* datasheet startup in numbered steps, refer to page 3-77 */
	/* 2. */
	cx25840_and_or(client, 0x803, ~0x10, 0x00);
	/*
	 * The default of this register should be 4, but I get 0 instead.
	 * Set this register to 4 manually.
	 */
	cx25840_write(client, 0x000, 0x04);
	/* 3. */
	init_dll1(client);
	init_dll2(client);
	cx25840_write(client, 0x136, 0x0a);
	/* 4. */
	cx25840_write(client, 0x13c, 0x01);
	cx25840_write(client, 0x13c, 0x00);
	/* 5. */
	/*
	 * Do the firmware load in a work handler to prevent.
	 * Otherwise the kernel is blocked waiting for the
	 * bit-banging i2c interface to finish uploading the
	 * firmware.
	 */
	INIT_WORK(&state->fw_work, cx25840_work_handler);
	init_waitqueue_head(&state->fw_wait);
	q = create_singlethread_workqueue("cx25840_fw");
	if (q) {
		prepare_to_wait(&state->fw_wait, &wait, TASK_UNINTERRUPTIBLE);
		queue_work(q, &state->fw_work);
		schedule();
		finish_wait(&state->fw_wait, &wait);
		destroy_workqueue(q);
	}

	/* 6. */
	cx25840_write(client, 0x115, 0x8c);
	cx25840_write(client, 0x116, 0x07);
	cx25840_write(client, 0x118, 0x02);
	/* 7. */
	cx25840_write(client, 0x4a5, 0x80);
	cx25840_write(client, 0x4a5, 0x00);
	cx25840_write(client, 0x402, 0x00);
	/* 8. */
	cx25840_and_or(client, 0x401, ~0x18, 0);
	cx25840_and_or(client, 0x4a2, ~0x10, 0x10);
	/* steps 8c and 8d are done in change_input() */
	/* 10. */
	cx25840_write(client, 0x8d3, 0x1f);
	cx25840_write(client, 0x8e3, 0x03);

	cx25840_std_setup(client);

	/* trial and error says these are needed to get audio */
	cx25840_write(client, 0x914, 0xa0);
	cx25840_write(client, 0x918, 0xa0);
	cx25840_write(client, 0x919, 0x01);

	/* stereo preferred */
	cx25840_write(client, 0x809, 0x04);
	/* AC97 shift */
	cx25840_write(client, 0x8cf, 0x0f);

	/* (re)set input */
	set_input(client, state->vid_input, state->aud_input);

	if (state->generic_mode)
		cx25840_vconfig_apply(client);

	/* start microcontroller */
	cx25840_and_or(client, 0x803, ~0x10, 0x10);
}

static void cx23885_initialize(struct i2c_client *client)
{
	DEFINE_WAIT(wait);
	struct cx25840_state *state = to_state(i2c_get_clientdata(client));
	u32 clk_freq = 0;
	struct workqueue_struct *q;

	/* cx23885 sets hostdata to clk_freq pointer */
	if (v4l2_get_subdev_hostdata(&state->sd))
		clk_freq = *((u32 *)v4l2_get_subdev_hostdata(&state->sd));

	/*
	 * Come out of digital power down
	 * The CX23888, at least, needs this, otherwise registers aside from
	 * 0x0-0x2 can't be read or written.
	 */
	cx25840_write(client, 0x000, 0);

	/* Internal Reset */
	cx25840_and_or(client, 0x102, ~0x01, 0x01);
	cx25840_and_or(client, 0x102, ~0x01, 0x00);

	/* Stop microcontroller */
	cx25840_and_or(client, 0x803, ~0x10, 0x00);

	/* DIF in reset? */
	cx25840_write(client, 0x398, 0);

	/*
	 * Trust the default xtal, no division
	 * '885: 28.636363... MHz
	 * '887: 25.000000 MHz
	 * '888: 50.000000 MHz
	 */
	cx25840_write(client, 0x2, 0x76);

	/* Power up all the PLL's and DLL */
	cx25840_write(client, 0x1, 0x40);

	/* Sys PLL */
	switch (state->id) {
	case CX23888_AV:
		/*
		 * 50.0 MHz * (0xb + 0xe8ba26/0x2000000)/4 = 5 * 28.636363 MHz
		 * 572.73 MHz before post divide
		 */
		if (clk_freq == 25000000) {
			/* 888/ImpactVCBe or 25Mhz xtal */
			; /* nothing to do */
		} else {
			/* HVR1850 or 50MHz xtal */
			cx25840_write(client, 0x2, 0x71);
		}
		cx25840_write4(client, 0x11c, 0x01d1744c);
		cx25840_write4(client, 0x118, 0x00000416);
		cx25840_write4(client, 0x404, 0x0010253e);
		cx25840_write4(client, 0x42c, 0x42600000);
		cx25840_write4(client, 0x44c, 0x161f1000);
		break;
	case CX23887_AV:
		/*
		 * 25.0 MHz * (0x16 + 0x1d1744c/0x2000000)/4 = 5 * 28.636363 MHz
		 * 572.73 MHz before post divide
		 */
		cx25840_write4(client, 0x11c, 0x01d1744c);
		cx25840_write4(client, 0x118, 0x00000416);
		break;
	case CX23885_AV:
	default:
		/*
		 * 28.636363 MHz * (0x14 + 0x0/0x2000000)/4 = 5 * 28.636363 MHz
		 * 572.73 MHz before post divide
		 */
		cx25840_write4(client, 0x11c, 0x00000000);
		cx25840_write4(client, 0x118, 0x00000414);
		break;
	}

	/* Disable DIF bypass */
	cx25840_write4(client, 0x33c, 0x00000001);

	/* DIF Src phase inc */
	cx25840_write4(client, 0x340, 0x0df7df83);

	/*
	 * Vid PLL
	 * Setup for a BT.656 pixel clock of 13.5 Mpixels/second
	 *
	 * 28.636363 MHz * (0xf + 0x02be2c9/0x2000000)/4 = 8 * 13.5 MHz
	 * 432.0 MHz before post divide
	 */

	/* HVR1850 */
	switch (state->id) {
	case CX23888_AV:
		if (clk_freq == 25000000) {
			/* 888/ImpactVCBe or 25MHz xtal */
			cx25840_write4(client, 0x10c, 0x01b6db7b);
			cx25840_write4(client, 0x108, 0x00000512);
		} else {
			/* 888/HVR1250 or 50MHz xtal */
			cx25840_write4(client, 0x10c, 0x13333333);
			cx25840_write4(client, 0x108, 0x00000515);
		}
		break;
	default:
		cx25840_write4(client, 0x10c, 0x002be2c9);
		cx25840_write4(client, 0x108, 0x0000040f);
	}

	/* Luma */
	cx25840_write4(client, 0x414, 0x00107d12);

	/* Chroma */
	if (is_cx23888(state))
		cx25840_write4(client, 0x418, 0x1d008282);
	else
		cx25840_write4(client, 0x420, 0x3d008282);

	/*
	 * Aux PLL
	 * Initial setup for audio sample clock:
	 * 48 ksps, 16 bits/sample, x160 multiplier = 122.88 MHz
	 * Initial I2S output/master clock(?):
	 * 48 ksps, 16 bits/sample, x16 multiplier = 12.288 MHz
	 */
	switch (state->id) {
	case CX23888_AV:
		/*
		 * 50.0 MHz * (0x7 + 0x0bedfa4/0x2000000)/3 = 122.88 MHz
		 * 368.64 MHz before post divide
		 * 122.88 MHz / 0xa = 12.288 MHz
		 */
		/* HVR1850 or 50MHz xtal or 25MHz xtal */
		cx25840_write4(client, 0x114, 0x017dbf48);
		cx25840_write4(client, 0x110, 0x000a030e);
		break;
	case CX23887_AV:
		/*
		 * 25.0 MHz * (0xe + 0x17dbf48/0x2000000)/3 = 122.88 MHz
		 * 368.64 MHz before post divide
		 * 122.88 MHz / 0xa = 12.288 MHz
		 */
		cx25840_write4(client, 0x114, 0x017dbf48);
		cx25840_write4(client, 0x110, 0x000a030e);
		break;
	case CX23885_AV:
	default:
		/*
		 * 28.636363 MHz * (0xc + 0x1bf0c9e/0x2000000)/3 = 122.88 MHz
		 * 368.64 MHz before post divide
		 * 122.88 MHz / 0xa = 12.288 MHz
		 */
		cx25840_write4(client, 0x114, 0x01bf0c9e);
		cx25840_write4(client, 0x110, 0x000a030c);
		break;
	}

	/* ADC2 input select */
	cx25840_write(client, 0x102, 0x10);

	/* VIN1 & VIN5 */
	cx25840_write(client, 0x103, 0x11);

	/* Enable format auto detect */
	cx25840_write(client, 0x400, 0);
	/* Fast subchroma lock */
	/* White crush, Chroma AGC & Chroma Killer enabled */
	cx25840_write(client, 0x401, 0xe8);

	/* Select AFE clock pad output source */
	cx25840_write(client, 0x144, 0x05);

	/* Drive GPIO2 direction and values for HVR1700
	 * where an onboard mux selects the output of demodulator
	 * vs the 417. Failure to set this results in no DTV.
	 * It's safe to set this across all Hauppauge boards
	 * currently, regardless of the board type.
	 */
	cx25840_write(client, 0x160, 0x1d);
	cx25840_write(client, 0x164, 0x00);

	/*
	 * Do the firmware load in a work handler to prevent.
	 * Otherwise the kernel is blocked waiting for the
	 * bit-banging i2c interface to finish uploading the
	 * firmware.
	 */
	INIT_WORK(&state->fw_work, cx25840_work_handler);
	init_waitqueue_head(&state->fw_wait);
	q = create_singlethread_workqueue("cx25840_fw");
	if (q) {
		prepare_to_wait(&state->fw_wait, &wait, TASK_UNINTERRUPTIBLE);
		queue_work(q, &state->fw_work);
		schedule();
		finish_wait(&state->fw_wait, &wait);
		destroy_workqueue(q);
	}

	/*
	 * Call the cx23888 specific std setup func, we no longer rely on
	 * the generic cx24840 func.
	 */
	if (is_cx23888(state))
		cx23888_std_setup(client);
	else
		cx25840_std_setup(client);

	/* (re)set input */
	set_input(client, state->vid_input, state->aud_input);

	/* start microcontroller */
	cx25840_and_or(client, 0x803, ~0x10, 0x10);

	/* Disable and clear video interrupts - we don't use them */
	cx25840_write4(client, CX25840_VID_INT_STAT_REG, 0xffffffff);

	/* Disable and clear audio interrupts - we don't use them */
	cx25840_write(client, CX25840_AUD_INT_CTRL_REG, 0xff);
	cx25840_write(client, CX25840_AUD_INT_STAT_REG, 0xff);

	/* CC raw enable */

	/*
	 *  - VIP 1.1 control codes - 10bit, blue field enable.
	 *  - enable raw data during vertical blanking.
	 *  - enable ancillary Data insertion for 656 or VIP.
	 */
	cx25840_write4(client, 0x404, 0x0010253e);

	/* CC on  - VBI_LINE_CTRL3, FLD_VBI_MD_LINE12 */
	cx25840_write(client, state->vbi_regs_offset + 0x42f, 0x66);

	/* HVR-1250 / HVR1850 DIF related */
	/* Power everything up */
	cx25840_write4(client, 0x130, 0x0);

	/* SRC_COMB_CFG */
	if (is_cx23888(state))
		cx25840_write4(client, 0x454, 0x6628021F);
	else
		cx25840_write4(client, 0x478, 0x6628021F);

	/* AFE_CLK_OUT_CTRL - Select the clock output source as output */
	cx25840_write4(client, 0x144, 0x5);

	/* I2C_OUT_CTL - I2S output configuration as
	 * Master, Sony, Left justified, left sample on WS=1
	 */
	cx25840_write4(client, 0x918, 0x1a0);

	/* AFE_DIAG_CTRL1 */
	cx25840_write4(client, 0x134, 0x000a1800);

	/* AFE_DIAG_CTRL3 - Inverted Polarity for Audio and Video */
	cx25840_write4(client, 0x13c, 0x00310000);
}

/* ----------------------------------------------------------------------- */

static void cx231xx_initialize(struct i2c_client *client)
{
	DEFINE_WAIT(wait);
	struct cx25840_state *state = to_state(i2c_get_clientdata(client));
	struct workqueue_struct *q;

	/* Internal Reset */
	cx25840_and_or(client, 0x102, ~0x01, 0x01);
	cx25840_and_or(client, 0x102, ~0x01, 0x00);

	/* Stop microcontroller */
	cx25840_and_or(client, 0x803, ~0x10, 0x00);

	/* DIF in reset? */
	cx25840_write(client, 0x398, 0);

	/* Trust the default xtal, no division */
	/* This changes for the cx23888 products */
	cx25840_write(client, 0x2, 0x76);

	/* Bring down the regulator for AUX clk */
	cx25840_write(client, 0x1, 0x40);

	/* Disable DIF bypass */
	cx25840_write4(client, 0x33c, 0x00000001);

	/* DIF Src phase inc */
	cx25840_write4(client, 0x340, 0x0df7df83);

	/* Luma */
	cx25840_write4(client, 0x414, 0x00107d12);

	/* Chroma */
	cx25840_write4(client, 0x420, 0x3d008282);

	/* ADC2 input select */
	cx25840_write(client, 0x102, 0x10);

	/* VIN1 & VIN5 */
	cx25840_write(client, 0x103, 0x11);

	/* Enable format auto detect */
	cx25840_write(client, 0x400, 0);
	/* Fast subchroma lock */
	/* White crush, Chroma AGC & Chroma Killer enabled */
	cx25840_write(client, 0x401, 0xe8);

	/*
	 * Do the firmware load in a work handler to prevent.
	 * Otherwise the kernel is blocked waiting for the
	 * bit-banging i2c interface to finish uploading the
	 * firmware.
	 */
	INIT_WORK(&state->fw_work, cx25840_work_handler);
	init_waitqueue_head(&state->fw_wait);
	q = create_singlethread_workqueue("cx25840_fw");
	if (q) {
		prepare_to_wait(&state->fw_wait, &wait, TASK_UNINTERRUPTIBLE);
		queue_work(q, &state->fw_work);
		schedule();
		finish_wait(&state->fw_wait, &wait);
		destroy_workqueue(q);
	}

	cx25840_std_setup(client);

	/* (re)set input */
	set_input(client, state->vid_input, state->aud_input);

	/* start microcontroller */
	cx25840_and_or(client, 0x803, ~0x10, 0x10);

	/* CC raw enable */
	cx25840_write(client, 0x404, 0x0b);

	/* CC on */
	cx25840_write(client, 0x42f, 0x66);
	cx25840_write4(client, 0x474, 0x1e1e601a);
}

/* ----------------------------------------------------------------------- */

void cx25840_std_setup(struct i2c_client *client)
{
	struct cx25840_state *state = to_state(i2c_get_clientdata(client));
	v4l2_std_id std = state->std;
	int hblank, hactive, burst, vblank, vactive, sc;
	int vblank656, src_decimation;
	int luma_lpf, uv_lpf, comb;
	u32 pll_int, pll_frac, pll_post;

	/* datasheet startup, step 8d */
	if (std & ~V4L2_STD_NTSC)
		cx25840_write(client, 0x49f, 0x11);
	else
		cx25840_write(client, 0x49f, 0x14);

	/* generic mode uses the values that the chip autoconfig would set */
	if (std & V4L2_STD_625_50) {
		hblank = 132;
		hactive = 720;
		burst = 93;
		if (state->generic_mode) {
			vblank = 34;
			vactive = 576;
			vblank656 = 38;
		} else {
			vblank = 36;
			vactive = 580;
			vblank656 = 40;
		}
		src_decimation = 0x21f;
		luma_lpf = 2;

		if (std & V4L2_STD_SECAM) {
			uv_lpf = 0;
			comb = 0;
			sc = 0x0a425f;
		} else if (std == V4L2_STD_PAL_Nc) {
			if (state->generic_mode) {
				burst = 95;
				luma_lpf = 1;
			}
			uv_lpf = 1;
			comb = 0x20;
			sc = 556453;
		} else {
			uv_lpf = 1;
			comb = 0x20;
			sc = 688739;
		}
	} else {
		hactive = 720;
		hblank = 122;
		vactive = 487;
		luma_lpf = 1;
		uv_lpf = 1;
		if (state->generic_mode) {
			vblank = 20;
			vblank656 = 24;
		}

		src_decimation = 0x21f;
		if (std == V4L2_STD_PAL_60) {
			if (!state->generic_mode) {
				vblank = 26;
				vblank656 = 26;
				burst = 0x5b;
			} else {
				burst = 0x59;
			}
			luma_lpf = 2;
			comb = 0x20;
			sc = 688739;
		} else if (std == V4L2_STD_PAL_M) {
			vblank = 20;
			vblank656 = 24;
			burst = 0x61;
			comb = 0x20;
			sc = 555452;
		} else {
			if (!state->generic_mode) {
				vblank = 26;
				vblank656 = 26;
			}
			burst = 0x5b;
			comb = 0x66;
			sc = 556063;
		}
	}

	/* DEBUG: Displays configured PLL frequency */
	if (!is_cx231xx(state)) {
		pll_int = cx25840_read(client, 0x108);
		pll_frac = cx25840_read4(client, 0x10c) & 0x1ffffff;
		pll_post = cx25840_read(client, 0x109);
		v4l_dbg(1, cx25840_debug, client,
			"PLL regs = int: %u, frac: %u, post: %u\n",
			pll_int, pll_frac, pll_post);

		if (pll_post) {
			int fin, fsc;
			int pll = (28636363L * ((((u64)pll_int) << 25L) + pll_frac)) >> 25L;

			pll /= pll_post;
			v4l_dbg(1, cx25840_debug, client,
				"PLL = %d.%06d MHz\n",
				pll / 1000000, pll % 1000000);
			v4l_dbg(1, cx25840_debug, client,
				"PLL/8 = %d.%06d MHz\n",
				pll / 8000000, (pll / 8) % 1000000);

			fin = ((u64)src_decimation * pll) >> 12;
			v4l_dbg(1, cx25840_debug, client,
				"ADC Sampling freq = %d.%06d MHz\n",
				fin / 1000000, fin % 1000000);

			fsc = (((u64)sc) * pll) >> 24L;
			v4l_dbg(1, cx25840_debug, client,
				"Chroma sub-carrier freq = %d.%06d MHz\n",
				fsc / 1000000, fsc % 1000000);

			v4l_dbg(1, cx25840_debug, client,
				"hblank %i, hactive %i, vblank %i, vactive %i, vblank656 %i, src_dec %i, burst 0x%02x, luma_lpf %i, uv_lpf %i, comb 0x%02x, sc 0x%06x\n",
				hblank, hactive, vblank, vactive, vblank656,
				src_decimation, burst, luma_lpf, uv_lpf,
				comb, sc);
		}
	}

	/* Sets horizontal blanking delay and active lines */
	cx25840_write(client, 0x470, hblank);
	cx25840_write(client, 0x471,
		      (((hblank >> 8) & 0x3) | (hactive << 4)) & 0xff);
	cx25840_write(client, 0x472, hactive >> 4);

	/* Sets burst gate delay */
	cx25840_write(client, 0x473, burst);

	/* Sets vertical blanking delay and active duration */
	cx25840_write(client, 0x474, vblank);
	cx25840_write(client, 0x475,
		      (((vblank >> 8) & 0x3) | (vactive << 4)) & 0xff);
	cx25840_write(client, 0x476, vactive >> 4);
	cx25840_write(client, 0x477, vblank656);

	/* Sets src decimation rate */
	cx25840_write(client, 0x478, src_decimation & 0xff);
	cx25840_write(client, 0x479, (src_decimation >> 8) & 0xff);

	/* Sets Luma and UV Low pass filters */
	cx25840_write(client, 0x47a, luma_lpf << 6 | ((uv_lpf << 4) & 0x30));

	/* Enables comb filters */
	cx25840_write(client, 0x47b, comb);

	/* Sets SC Step*/
	cx25840_write(client, 0x47c, sc);
	cx25840_write(client, 0x47d, (sc >> 8) & 0xff);
	cx25840_write(client, 0x47e, (sc >> 16) & 0xff);

	/* Sets VBI parameters */
	if (std & V4L2_STD_625_50) {
		cx25840_write(client, 0x47f, 0x01);
		state->vbi_line_offset = 5;
	} else {
		cx25840_write(client, 0x47f, 0x00);
		state->vbi_line_offset = 8;
	}
}

/* ----------------------------------------------------------------------- */

static void input_change(struct i2c_client *client)
{
	struct cx25840_state *state = to_state(i2c_get_clientdata(client));
	v4l2_std_id std = state->std;

	/* Follow step 8c and 8d of section 3.16 in the cx25840 datasheet */
	if (std & V4L2_STD_SECAM) {
		cx25840_write(client, 0x402, 0);
	} else {
		cx25840_write(client, 0x402, 0x04);
		cx25840_write(client, 0x49f,
			      (std & V4L2_STD_NTSC) ? 0x14 : 0x11);
	}
	cx25840_and_or(client, 0x401, ~0x60, 0);
	cx25840_and_or(client, 0x401, ~0x60, 0x60);

	/* Don't write into audio registers on cx2583x chips */
	if (is_cx2583x(state))
		return;

	cx25840_and_or(client, 0x810, ~0x01, 1);

	if (state->radio) {
		cx25840_write(client, 0x808, 0xf9);
		cx25840_write(client, 0x80b, 0x00);
	} else if (std & V4L2_STD_525_60) {
		/*
		 * Certain Hauppauge PVR150 models have a hardware bug
		 * that causes audio to drop out. For these models the
		 * audio standard must be set explicitly.
		 * To be precise: it affects cards with tuner models
		 * 85, 99 and 112 (model numbers from tveeprom).
		 */
		int hw_fix = state->pvr150_workaround;

		if (std == V4L2_STD_NTSC_M_JP) {
			/* Japan uses EIAJ audio standard */
			cx25840_write(client, 0x808, hw_fix ? 0x2f : 0xf7);
		} else if (std == V4L2_STD_NTSC_M_KR) {
			/* South Korea uses A2 audio standard */
			cx25840_write(client, 0x808, hw_fix ? 0x3f : 0xf8);
		} else {
			/* Others use the BTSC audio standard */
			cx25840_write(client, 0x808, hw_fix ? 0x1f : 0xf6);
		}
		cx25840_write(client, 0x80b, 0x00);
	} else if (std & V4L2_STD_PAL) {
		/* Autodetect audio standard and audio system */
		cx25840_write(client, 0x808, 0xff);
		/*
		 * Since system PAL-L is pretty much non-existent and
		 * not used by any public broadcast network, force
		 * 6.5 MHz carrier to be interpreted as System DK,
		 * this avoids DK audio detection instability
		 */
		cx25840_write(client, 0x80b, 0x00);
	} else if (std & V4L2_STD_SECAM) {
		/* Autodetect audio standard and audio system */
		cx25840_write(client, 0x808, 0xff);
		/*
		 * If only one of SECAM-DK / SECAM-L is required, then force
		 * 6.5MHz carrier, else autodetect it
		 */
		if ((std & V4L2_STD_SECAM_DK) &&
		    !(std & (V4L2_STD_SECAM_L | V4L2_STD_SECAM_LC))) {
			/* 6.5 MHz carrier to be interpreted as System DK */
			cx25840_write(client, 0x80b, 0x00);
		} else if (!(std & V4L2_STD_SECAM_DK) &&
			   (std & (V4L2_STD_SECAM_L | V4L2_STD_SECAM_LC))) {
			/* 6.5 MHz carrier to be interpreted as System L */
			cx25840_write(client, 0x80b, 0x08);
		} else {
			/* 6.5 MHz carrier to be autodetected */
			cx25840_write(client, 0x80b, 0x10);
		}
	}

	cx25840_and_or(client, 0x810, ~0x01, 0);
}

static int set_input(struct i2c_client *client,
		     enum cx25840_video_input vid_input,
		     enum cx25840_audio_input aud_input)
{
	struct cx25840_state *state = to_state(i2c_get_clientdata(client));
	u8 is_composite = (vid_input >= CX25840_COMPOSITE1 &&
			   vid_input <= CX25840_COMPOSITE8);
	u8 is_component = (vid_input & CX25840_COMPONENT_ON) ==
			CX25840_COMPONENT_ON;
	u8 is_dif = (vid_input & CX25840_DIF_ON) ==
			CX25840_DIF_ON;
	u8 is_svideo = (vid_input & CX25840_SVIDEO_ON) ==
			CX25840_SVIDEO_ON;
	int luma = vid_input & 0xf0;
	int chroma = vid_input & 0xf00;
	u8 reg;
	u32 val;

	v4l_dbg(1, cx25840_debug, client,
		"decoder set video input %d, audio input %d\n",
		vid_input, aud_input);

	if (vid_input >= CX25840_VIN1_CH1) {
		v4l_dbg(1, cx25840_debug, client, "vid_input 0x%x\n",
			vid_input);
		reg = vid_input & 0xff;
		is_composite = !is_component &&
			       ((vid_input & CX25840_SVIDEO_ON) != CX25840_SVIDEO_ON);

		v4l_dbg(1, cx25840_debug, client, "mux cfg 0x%x comp=%d\n",
			reg, is_composite);
	} else if (is_composite) {
		reg = 0xf0 + (vid_input - CX25840_COMPOSITE1);
	} else {
		if ((vid_input & ~0xff0) ||
		    luma < CX25840_SVIDEO_LUMA1 ||
		    luma > CX25840_SVIDEO_LUMA8 ||
		    chroma < CX25840_SVIDEO_CHROMA4 ||
		    chroma > CX25840_SVIDEO_CHROMA8) {
			v4l_err(client, "0x%04x is not a valid video input!\n",
				vid_input);
			return -EINVAL;
		}
		reg = 0xf0 + ((luma - CX25840_SVIDEO_LUMA1) >> 4);
		if (chroma >= CX25840_SVIDEO_CHROMA7) {
			reg &= 0x3f;
			reg |= (chroma - CX25840_SVIDEO_CHROMA7) >> 2;
		} else {
			reg &= 0xcf;
			reg |= (chroma - CX25840_SVIDEO_CHROMA4) >> 4;
		}
	}

	/* The caller has previously prepared the correct routing
	 * configuration in reg (for the cx23885) so we have no
	 * need to attempt to flip bits for earlier av decoders.
	 */
	if (!is_cx2388x(state) && !is_cx231xx(state)) {
		switch (aud_input) {
		case CX25840_AUDIO_SERIAL:
			/* do nothing, use serial audio input */
			break;
		case CX25840_AUDIO4:
			reg &= ~0x30;
			break;
		case CX25840_AUDIO5:
			reg &= ~0x30;
			reg |= 0x10;
			break;
		case CX25840_AUDIO6:
			reg &= ~0x30;
			reg |= 0x20;
			break;
		case CX25840_AUDIO7:
			reg &= ~0xc0;
			break;
		case CX25840_AUDIO8:
			reg &= ~0xc0;
			reg |= 0x40;
			break;
		default:
			v4l_err(client, "0x%04x is not a valid audio input!\n",
				aud_input);
			return -EINVAL;
		}
	}

	cx25840_write(client, 0x103, reg);

	/* Set INPUT_MODE to Composite, S-Video or Component */
	if (is_component)
		cx25840_and_or(client, 0x401, ~0x6, 0x6);
	else
		cx25840_and_or(client, 0x401, ~0x6, is_composite ? 0 : 0x02);

	if (is_cx2388x(state)) {
		/* Enable or disable the DIF for tuner use */
		if (is_dif) {
			cx25840_and_or(client, 0x102, ~0x80, 0x80);

			/* Set of defaults for NTSC and PAL */
			cx25840_write4(client, 0x31c, 0xc2262600);
			cx25840_write4(client, 0x320, 0xc2262600);

			/* 18271 IF - Nobody else yet uses a different
			 * tuner with the DIF, so these are reasonable
			 * assumptions (HVR1250 and HVR1850 specific).
			 */
			cx25840_write4(client, 0x318, 0xda262600);
			cx25840_write4(client, 0x33c, 0x2a24c800);
			cx25840_write4(client, 0x104, 0x0704dd00);
		} else {
			cx25840_write4(client, 0x300, 0x015c28f5);

			cx25840_and_or(client, 0x102, ~0x80, 0);
			cx25840_write4(client, 0x340, 0xdf7df83);
			cx25840_write4(client, 0x104, 0x0704dd80);
			cx25840_write4(client, 0x314, 0x22400600);
			cx25840_write4(client, 0x318, 0x40002600);
			cx25840_write4(client, 0x324, 0x40002600);
			cx25840_write4(client, 0x32c, 0x0250e620);
			cx25840_write4(client, 0x39c, 0x01FF0B00);

			cx25840_write4(client, 0x410, 0xffff0dbf);
			cx25840_write4(client, 0x414, 0x00137d03);

			if (is_cx23888(state)) {
				/* 888 MISC_TIM_CTRL */
				cx25840_write4(client, 0x42c, 0x42600000);
				/* 888 FIELD_COUNT */
				cx25840_write4(client, 0x430, 0x0000039b);
				/* 888 VSCALE_CTRL */
				cx25840_write4(client, 0x438, 0x00000000);
				/* 888 DFE_CTRL1 */
				cx25840_write4(client, 0x440, 0xF8E3E824);
				/* 888 DFE_CTRL2 */
				cx25840_write4(client, 0x444, 0x401040dc);
				/* 888 DFE_CTRL3 */
				cx25840_write4(client, 0x448, 0xcd3f02a0);
				/* 888 PLL_CTRL */
				cx25840_write4(client, 0x44c, 0x161f1000);
				/* 888 HTL_CTRL */
				cx25840_write4(client, 0x450, 0x00000802);
			}
			cx25840_write4(client, 0x91c, 0x01000000);
			cx25840_write4(client, 0x8e0, 0x03063870);
			cx25840_write4(client, 0x8d4, 0x7FFF0024);
			cx25840_write4(client, 0x8d0, 0x00063073);

			cx25840_write4(client, 0x8c8, 0x00010000);
			cx25840_write4(client, 0x8cc, 0x00080023);

			/* DIF BYPASS */
			cx25840_write4(client, 0x33c, 0x2a04c800);
		}

		/* Reset the DIF */
		cx25840_write4(client, 0x398, 0);
	}

	if (!is_cx2388x(state) && !is_cx231xx(state)) {
		/* Set CH_SEL_ADC2 to 1 if input comes from CH3 */
		cx25840_and_or(client, 0x102, ~0x2, (reg & 0x80) == 0 ? 2 : 0);
		/* Set DUAL_MODE_ADC2 to 1 if input comes from both CH2&CH3 */
		if ((reg & 0xc0) != 0xc0 && (reg & 0x30) != 0x30)
			cx25840_and_or(client, 0x102, ~0x4, 4);
		else
			cx25840_and_or(client, 0x102, ~0x4, 0);
	} else {
		/* Set DUAL_MODE_ADC2 to 1 if component*/
		cx25840_and_or(client, 0x102, ~0x4, is_component ? 0x4 : 0x0);
		if (is_composite) {
			/* ADC2 input select channel 2 */
			cx25840_and_or(client, 0x102, ~0x2, 0);
		} else if (!is_component) {
			/* S-Video */
			if (chroma >= CX25840_SVIDEO_CHROMA7) {
				/* ADC2 input select channel 3 */
				cx25840_and_or(client, 0x102, ~0x2, 2);
			} else {
				/* ADC2 input select channel 2 */
				cx25840_and_or(client, 0x102, ~0x2, 0);
			}
		}

		/* cx23885 / SVIDEO */
		if (is_cx2388x(state) && is_svideo) {
#define AFE_CTRL  (0x104)
#define MODE_CTRL (0x400)
			cx25840_and_or(client, 0x102, ~0x2, 0x2);

			val = cx25840_read4(client, MODE_CTRL);
			val &= 0xFFFFF9FF;

			/* YC */
			val |= 0x00000200;
			val &= ~0x2000;
			cx25840_write4(client, MODE_CTRL, val);

			val = cx25840_read4(client, AFE_CTRL);

			/* Chroma in select */
			val |= 0x00001000;
			val &= 0xfffffe7f;
			/* Clear VGA_SEL_CH2 and VGA_SEL_CH3 (bits 7 and 8).
			 * This sets them to use video rather than audio.
			 * Only one of the two will be in use.
			 */
			cx25840_write4(client, AFE_CTRL, val);
		} else {
			cx25840_and_or(client, 0x102, ~0x2, 0);
		}
	}

	state->vid_input = vid_input;
	state->aud_input = aud_input;
	cx25840_audio_set_path(client);
	input_change(client);

	if (is_cx2388x(state)) {
		/* Audio channel 1 src : Parallel 1 */
		cx25840_write(client, 0x124, 0x03);

		/* Select AFE clock pad output source */
		cx25840_write(client, 0x144, 0x05);

		/* I2S_IN_CTL: I2S_IN_SONY_MODE, LEFT SAMPLE on WS=1 */
		cx25840_write(client, 0x914, 0xa0);

		/* I2S_OUT_CTL:
		 * I2S_IN_SONY_MODE, LEFT SAMPLE on WS=1
		 * I2S_OUT_MASTER_MODE = Master
		 */
		cx25840_write(client, 0x918, 0xa0);
		cx25840_write(client, 0x919, 0x01);
	} else if (is_cx231xx(state)) {
		/* Audio channel 1 src : Parallel 1 */
		cx25840_write(client, 0x124, 0x03);

		/* I2S_IN_CTL: I2S_IN_SONY_MODE, LEFT SAMPLE on WS=1 */
		cx25840_write(client, 0x914, 0xa0);

		/* I2S_OUT_CTL:
		 * I2S_IN_SONY_MODE, LEFT SAMPLE on WS=1
		 * I2S_OUT_MASTER_MODE = Master
		 */
		cx25840_write(client, 0x918, 0xa0);
		cx25840_write(client, 0x919, 0x01);
	}

	if (is_cx2388x(state) &&
	    ((aud_input == CX25840_AUDIO7) || (aud_input == CX25840_AUDIO6))) {
		/* Configure audio from LR1 or LR2 input */
		cx25840_write4(client, 0x910, 0);
		cx25840_write4(client, 0x8d0, 0x63073);
	} else if (is_cx2388x(state) && (aud_input == CX25840_AUDIO8)) {
		/* Configure audio from tuner/sif input */
		cx25840_write4(client, 0x910, 0x12b000c9);
		cx25840_write4(client, 0x8d0, 0x1f063870);
	}

	if (is_cx23888(state)) {
		/*
		 * HVR1850
		 *
		 * AUD_IO_CTRL - I2S Input, Parallel1
		 *  - Channel 1 src - Parallel1 (Merlin out)
		 *  - Channel 2 src - Parallel2 (Merlin out)
		 *  - Channel 3 src - Parallel3 (Merlin AC97 out)
		 *  - I2S source and dir - Merlin, output
		 */
		cx25840_write4(client, 0x124, 0x100);

		if (!is_dif) {
			/*
			 * Stop microcontroller if we don't need it
			 * to avoid audio popping on svideo/composite use.
			 */
			cx25840_and_or(client, 0x803, ~0x10, 0x00);
		}
	}

	return 0;
}

/* ----------------------------------------------------------------------- */

static int set_v4lstd(struct i2c_client *client)
{
	struct cx25840_state *state = to_state(i2c_get_clientdata(client));
	u8 fmt = 0;	/* zero is autodetect */
	u8 pal_m = 0;

	/* First tests should be against specific std */
	if (state->std == V4L2_STD_NTSC_M_JP) {
		fmt = 0x2;
	} else if (state->std == V4L2_STD_NTSC_443) {
		fmt = 0x3;
	} else if (state->std == V4L2_STD_PAL_M) {
		pal_m = 1;
		fmt = 0x5;
	} else if (state->std == V4L2_STD_PAL_N) {
		fmt = 0x6;
	} else if (state->std == V4L2_STD_PAL_Nc) {
		fmt = 0x7;
	} else if (state->std == V4L2_STD_PAL_60) {
		fmt = 0x8;
	} else {
		/* Then, test against generic ones */
		if (state->std & V4L2_STD_NTSC)
			fmt = 0x1;
		else if (state->std & V4L2_STD_PAL)
			fmt = 0x4;
		else if (state->std & V4L2_STD_SECAM)
			fmt = 0xc;
	}

	v4l_dbg(1, cx25840_debug, client,
		"changing video std to fmt %i\n", fmt);

	/*
	 * Follow step 9 of section 3.16 in the cx25840 datasheet.
	 * Without this PAL may display a vertical ghosting effect.
	 * This happens for example with the Yuan MPC622.
	 */
	if (fmt >= 4 && fmt < 8) {
		/* Set format to NTSC-M */
		cx25840_and_or(client, 0x400, ~0xf, 1);
		/* Turn off LCOMB */
		cx25840_and_or(client, 0x47b, ~6, 0);
	}
	cx25840_and_or(client, 0x400, ~0xf, fmt);
	cx25840_and_or(client, 0x403, ~0x3, pal_m);
	if (is_cx23888(state))
		cx23888_std_setup(client);
	else
		cx25840_std_setup(client);
	if (!is_cx2583x(state))
		input_change(client);
	return 0;
}

/* ----------------------------------------------------------------------- */

static int cx25840_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct cx25840_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		cx25840_write(client, 0x414, ctrl->val - 128);
		break;

	case V4L2_CID_CONTRAST:
		cx25840_write(client, 0x415, ctrl->val << 1);
		break;

	case V4L2_CID_SATURATION:
		if (is_cx23888(state)) {
			cx25840_write(client, 0x418, ctrl->val << 1);
			cx25840_write(client, 0x419, ctrl->val << 1);
		} else {
			cx25840_write(client, 0x420, ctrl->val << 1);
			cx25840_write(client, 0x421, ctrl->val << 1);
		}
		break;

	case V4L2_CID_HUE:
		if (is_cx23888(state))
			cx25840_write(client, 0x41a, ctrl->val);
		else
			cx25840_write(client, 0x422, ctrl->val);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/* ----------------------------------------------------------------------- */

static int cx25840_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *sd_state,
			   struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt = &format->format;
	struct cx25840_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u32 hsc, vsc, v_src, h_src, v_add;
	int filter;
	int is_50hz = !(state->std & V4L2_STD_525_60);

	if (format->pad || fmt->code != MEDIA_BUS_FMT_FIXED)
		return -EINVAL;

	fmt->field = V4L2_FIELD_INTERLACED;
	fmt->colorspace = V4L2_COLORSPACE_SMPTE170M;

	if (is_cx23888(state)) {
		v_src = (cx25840_read(client, 0x42a) & 0x3f) << 4;
		v_src |= (cx25840_read(client, 0x429) & 0xf0) >> 4;
	} else {
		v_src = (cx25840_read(client, 0x476) & 0x3f) << 4;
		v_src |= (cx25840_read(client, 0x475) & 0xf0) >> 4;
	}

	if (is_cx23888(state)) {
		h_src = (cx25840_read(client, 0x426) & 0x3f) << 4;
		h_src |= (cx25840_read(client, 0x425) & 0xf0) >> 4;
	} else {
		h_src = (cx25840_read(client, 0x472) & 0x3f) << 4;
		h_src |= (cx25840_read(client, 0x471) & 0xf0) >> 4;
	}

	if (!state->generic_mode) {
		v_add = is_50hz ? 4 : 7;

		/*
		 * cx23888 in 525-line mode is programmed for 486 active lines
		 * while other chips use 487 active lines.
		 *
		 * See reg 0x428 bits [21:12] in cx23888_std_setup() vs
		 * vactive in cx25840_std_setup().
		 */
		if (is_cx23888(state) && !is_50hz)
			v_add--;
	} else {
		v_add = 0;
	}

	if (h_src == 0 ||
	    v_src <= v_add) {
		v4l_err(client,
			"chip reported picture size (%u x %u) is far too small\n",
			(unsigned int)h_src, (unsigned int)v_src);
		/*
		 * that's the best we can do since the output picture
		 * size is completely unknown in this case
		 */
		return -EINVAL;
	}

	fmt->width = clamp(fmt->width, (h_src + 15) / 16, h_src);

	if (v_add * 8 >= v_src)
		fmt->height = clamp(fmt->height, (u32)1, v_src - v_add);
	else
		fmt->height = clamp(fmt->height, (v_src - v_add * 8 + 7) / 8,
				    v_src - v_add);

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	hsc = (h_src * (1 << 20)) / fmt->width - (1 << 20);
	vsc = (1 << 16) - (v_src * (1 << 9) / (fmt->height + v_add) - (1 << 9));
	vsc &= 0x1fff;

	if (fmt->width >= 385)
		filter = 0;
	else if (fmt->width > 192)
		filter = 1;
	else if (fmt->width > 96)
		filter = 2;
	else
		filter = 3;

	v4l_dbg(1, cx25840_debug, client,
		"decoder set size %u x %u with scale %x x %x\n",
		(unsigned int)fmt->width, (unsigned int)fmt->height,
		(unsigned int)hsc, (unsigned int)vsc);

	/* HSCALE=hsc */
	if (is_cx23888(state)) {
		cx25840_write4(client, 0x434, hsc | (1 << 24));
		/* VSCALE=vsc VS_INTRLACE=1 VFILT=filter */
		cx25840_write4(client, 0x438, vsc | (1 << 19) | (filter << 16));
	} else {
		cx25840_write(client, 0x418, hsc & 0xff);
		cx25840_write(client, 0x419, (hsc >> 8) & 0xff);
		cx25840_write(client, 0x41a, hsc >> 16);
		/* VSCALE=vsc */
		cx25840_write(client, 0x41c, vsc & 0xff);
		cx25840_write(client, 0x41d, vsc >> 8);
		/* VS_INTRLACE=1 VFILT=filter */
		cx25840_write(client, 0x41e, 0x8 | filter);
	}
	return 0;
}

/* ----------------------------------------------------------------------- */

static void log_video_status(struct i2c_client *client)
{
	static const char *const fmt_strs[] = {
		"0x0",
		"NTSC-M", "NTSC-J", "NTSC-4.43",
		"PAL-BDGHI", "PAL-M", "PAL-N", "PAL-Nc", "PAL-60",
		"0x9", "0xA", "0xB",
		"SECAM",
		"0xD", "0xE", "0xF"
	};

	struct cx25840_state *state = to_state(i2c_get_clientdata(client));
	u8 vidfmt_sel = cx25840_read(client, 0x400) & 0xf;
	u8 gen_stat1 = cx25840_read(client, 0x40d);
	u8 gen_stat2 = cx25840_read(client, 0x40e);
	int vid_input = state->vid_input;

	v4l_info(client, "Video signal:              %spresent\n",
		 (gen_stat2 & 0x20) ? "" : "not ");
	v4l_info(client, "Detected format:           %s\n",
		 fmt_strs[gen_stat1 & 0xf]);

	v4l_info(client, "Specified standard:        %s\n",
		 vidfmt_sel ? fmt_strs[vidfmt_sel] : "automatic detection");

	if (vid_input >= CX25840_COMPOSITE1 &&
	    vid_input <= CX25840_COMPOSITE8) {
		v4l_info(client, "Specified video input:     Composite %d\n",
			 vid_input - CX25840_COMPOSITE1 + 1);
	} else {
		v4l_info(client,
			 "Specified video input:     S-Video (Luma In%d, Chroma In%d)\n",
			 (vid_input & 0xf0) >> 4, (vid_input & 0xf00) >> 8);
	}

	v4l_info(client, "Specified audioclock freq: %d Hz\n",
		 state->audclk_freq);
}

/* ----------------------------------------------------------------------- */

static void log_audio_status(struct i2c_client *client)
{
	struct cx25840_state *state = to_state(i2c_get_clientdata(client));
	u8 download_ctl = cx25840_read(client, 0x803);
	u8 mod_det_stat0 = cx25840_read(client, 0x804);
	u8 mod_det_stat1 = cx25840_read(client, 0x805);
	u8 audio_config = cx25840_read(client, 0x808);
	u8 pref_mode = cx25840_read(client, 0x809);
	u8 afc0 = cx25840_read(client, 0x80b);
	u8 mute_ctl = cx25840_read(client, 0x8d3);
	int aud_input = state->aud_input;
	char *p;

	switch (mod_det_stat0) {
	case 0x00:
		p = "mono";
		break;
	case 0x01:
		p = "stereo";
		break;
	case 0x02:
		p = "dual";
		break;
	case 0x04:
		p = "tri";
		break;
	case 0x10:
		p = "mono with SAP";
		break;
	case 0x11:
		p = "stereo with SAP";
		break;
	case 0x12:
		p = "dual with SAP";
		break;
	case 0x14:
		p = "tri with SAP";
		break;
	case 0xfe:
		p = "forced mode";
		break;
	default:
		p = "not defined";
	}
	v4l_info(client, "Detected audio mode:       %s\n", p);

	switch (mod_det_stat1) {
	case 0x00:
		p = "not defined";
		break;
	case 0x01:
		p = "EIAJ";
		break;
	case 0x02:
		p = "A2-M";
		break;
	case 0x03:
		p = "A2-BG";
		break;
	case 0x04:
		p = "A2-DK1";
		break;
	case 0x05:
		p = "A2-DK2";
		break;
	case 0x06:
		p = "A2-DK3";
		break;
	case 0x07:
		p = "A1 (6.0 MHz FM Mono)";
		break;
	case 0x08:
		p = "AM-L";
		break;
	case 0x09:
		p = "NICAM-BG";
		break;
	case 0x0a:
		p = "NICAM-DK";
		break;
	case 0x0b:
		p = "NICAM-I";
		break;
	case 0x0c:
		p = "NICAM-L";
		break;
	case 0x0d:
		p = "BTSC/EIAJ/A2-M Mono (4.5 MHz FMMono)";
		break;
	case 0x0e:
		p = "IF FM Radio";
		break;
	case 0x0f:
		p = "BTSC";
		break;
	case 0x10:
		p = "high-deviation FM";
		break;
	case 0x11:
		p = "very high-deviation FM";
		break;
	case 0xfd:
		p = "unknown audio standard";
		break;
	case 0xfe:
		p = "forced audio standard";
		break;
	case 0xff:
		p = "no detected audio standard";
		break;
	default:
		p = "not defined";
	}
	v4l_info(client, "Detected audio standard:   %s\n", p);
	v4l_info(client, "Audio microcontroller:     %s\n",
		 (download_ctl & 0x10) ?
		 ((mute_ctl & 0x2) ? "detecting" : "running") : "stopped");

	switch (audio_config >> 4) {
	case 0x00:
		p = "undefined";
		break;
	case 0x01:
		p = "BTSC";
		break;
	case 0x02:
		p = "EIAJ";
		break;
	case 0x03:
		p = "A2-M";
		break;
	case 0x04:
		p = "A2-BG";
		break;
	case 0x05:
		p = "A2-DK1";
		break;
	case 0x06:
		p = "A2-DK2";
		break;
	case 0x07:
		p = "A2-DK3";
		break;
	case 0x08:
		p = "A1 (6.0 MHz FM Mono)";
		break;
	case 0x09:
		p = "AM-L";
		break;
	case 0x0a:
		p = "NICAM-BG";
		break;
	case 0x0b:
		p = "NICAM-DK";
		break;
	case 0x0c:
		p = "NICAM-I";
		break;
	case 0x0d:
		p = "NICAM-L";
		break;
	case 0x0e:
		p = "FM radio";
		break;
	case 0x0f:
		p = "automatic detection";
		break;
	default:
		p = "undefined";
	}
	v4l_info(client, "Configured audio standard: %s\n", p);

	if ((audio_config >> 4) < 0xF) {
		switch (audio_config & 0xF) {
		case 0x00:
			p = "MONO1 (LANGUAGE A/Mono L+R channel for BTSC, EIAJ, A2)";
			break;
		case 0x01:
			p = "MONO2 (LANGUAGE B)";
			break;
		case 0x02:
			p = "MONO3 (STEREO forced MONO)";
			break;
		case 0x03:
			p = "MONO4 (NICAM ANALOG-Language C/Analog Fallback)";
			break;
		case 0x04:
			p = "STEREO";
			break;
		case 0x05:
			p = "DUAL1 (AB)";
			break;
		case 0x06:
			p = "DUAL2 (AC) (FM)";
			break;
		case 0x07:
			p = "DUAL3 (BC) (FM)";
			break;
		case 0x08:
			p = "DUAL4 (AC) (AM)";
			break;
		case 0x09:
			p = "DUAL5 (BC) (AM)";
			break;
		case 0x0a:
			p = "SAP";
			break;
		default:
			p = "undefined";
		}
		v4l_info(client, "Configured audio mode:     %s\n", p);
	} else {
		switch (audio_config & 0xF) {
		case 0x00:
			p = "BG";
			break;
		case 0x01:
			p = "DK1";
			break;
		case 0x02:
			p = "DK2";
			break;
		case 0x03:
			p = "DK3";
			break;
		case 0x04:
			p = "I";
			break;
		case 0x05:
			p = "L";
			break;
		case 0x06:
			p = "BTSC";
			break;
		case 0x07:
			p = "EIAJ";
			break;
		case 0x08:
			p = "A2-M";
			break;
		case 0x09:
			p = "FM Radio";
			break;
		case 0x0f:
			p = "automatic standard and mode detection";
			break;
		default:
			p = "undefined";
		}
		v4l_info(client, "Configured audio system:   %s\n", p);
	}

	if (aud_input) {
		v4l_info(client, "Specified audio input:     Tuner (In%d)\n",
			 aud_input);
	} else {
		v4l_info(client, "Specified audio input:     External\n");
	}

	switch (pref_mode & 0xf) {
	case 0:
		p = "mono/language A";
		break;
	case 1:
		p = "language B";
		break;
	case 2:
		p = "language C";
		break;
	case 3:
		p = "analog fallback";
		break;
	case 4:
		p = "stereo";
		break;
	case 5:
		p = "language AC";
		break;
	case 6:
		p = "language BC";
		break;
	case 7:
		p = "language AB";
		break;
	default:
		p = "undefined";
	}
	v4l_info(client, "Preferred audio mode:      %s\n", p);

	if ((audio_config & 0xf) == 0xf) {
		switch ((afc0 >> 3) & 0x3) {
		case 0:
			p = "system DK";
			break;
		case 1:
			p = "system L";
			break;
		case 2:
			p = "autodetect";
			break;
		default:
			p = "undefined";
		}
		v4l_info(client, "Selected 65 MHz format:    %s\n", p);

		switch (afc0 & 0x7) {
		case 0:
			p = "chroma";
			break;
		case 1:
			p = "BTSC";
			break;
		case 2:
			p = "EIAJ";
			break;
		case 3:
			p = "A2-M";
			break;
		case 4:
			p = "autodetect";
			break;
		default:
			p = "undefined";
		}
		v4l_info(client, "Selected 45 MHz format:    %s\n", p);
	}
}

#define CX25840_VCONFIG_OPTION(state, cfg_in, opt_msk)			\
	do {								\
		if ((cfg_in) & (opt_msk)) {				\
			(state)->vid_config &= ~(opt_msk);		\
			(state)->vid_config |= (cfg_in) & (opt_msk);	\
		}							\
	} while (0)

/* apply incoming options to the current vconfig */
static void cx25840_vconfig_add(struct cx25840_state *state, u32 cfg_in)
{
	CX25840_VCONFIG_OPTION(state, cfg_in, CX25840_VCONFIG_FMT_MASK);
	CX25840_VCONFIG_OPTION(state, cfg_in, CX25840_VCONFIG_RES_MASK);
	CX25840_VCONFIG_OPTION(state, cfg_in, CX25840_VCONFIG_VBIRAW_MASK);
	CX25840_VCONFIG_OPTION(state, cfg_in, CX25840_VCONFIG_ANCDATA_MASK);
	CX25840_VCONFIG_OPTION(state, cfg_in, CX25840_VCONFIG_TASKBIT_MASK);
	CX25840_VCONFIG_OPTION(state, cfg_in, CX25840_VCONFIG_ACTIVE_MASK);
	CX25840_VCONFIG_OPTION(state, cfg_in, CX25840_VCONFIG_VALID_MASK);
	CX25840_VCONFIG_OPTION(state, cfg_in, CX25840_VCONFIG_HRESETW_MASK);
	CX25840_VCONFIG_OPTION(state, cfg_in, CX25840_VCONFIG_CLKGATE_MASK);
	CX25840_VCONFIG_OPTION(state, cfg_in, CX25840_VCONFIG_DCMODE_MASK);
	CX25840_VCONFIG_OPTION(state, cfg_in, CX25840_VCONFIG_IDID0S_MASK);
	CX25840_VCONFIG_OPTION(state, cfg_in, CX25840_VCONFIG_VIPCLAMP_MASK);
}

/* ----------------------------------------------------------------------- */

/*
 * Initializes the device in the generic mode.
 * For cx2584x chips also adds additional video output settings provided
 * in @val parameter (CX25840_VCONFIG_*).
 *
 * The generic mode disables some of the ivtv-related hacks in this driver.
 * For cx2584x chips it also enables setting video output configuration while
 * setting it according to datasheet defaults by default.
 */
static int cx25840_init(struct v4l2_subdev *sd, u32 val)
{
	struct cx25840_state *state = to_state(sd);

	state->generic_mode = true;

	if (is_cx2584x(state)) {
		/* set datasheet video output defaults */
		state->vid_config = CX25840_VCONFIG_FMT_BT656 |
				    CX25840_VCONFIG_RES_8BIT |
				    CX25840_VCONFIG_VBIRAW_DISABLED |
				    CX25840_VCONFIG_ANCDATA_ENABLED |
				    CX25840_VCONFIG_TASKBIT_ONE |
				    CX25840_VCONFIG_ACTIVE_HORIZONTAL |
				    CX25840_VCONFIG_VALID_NORMAL |
				    CX25840_VCONFIG_HRESETW_NORMAL |
				    CX25840_VCONFIG_CLKGATE_NONE |
				    CX25840_VCONFIG_DCMODE_DWORDS |
				    CX25840_VCONFIG_IDID0S_NORMAL |
				    CX25840_VCONFIG_VIPCLAMP_DISABLED;

		/* add additional settings */
		cx25840_vconfig_add(state, val);
	} else {
		/* TODO: generic mode needs to be developed for other chips */
		WARN_ON(1);
	}

	return 0;
}

static int cx25840_reset(struct v4l2_subdev *sd, u32 val)
{
	struct cx25840_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (is_cx2583x(state))
		cx25836_initialize(client);
	else if (is_cx2388x(state))
		cx23885_initialize(client);
	else if (is_cx231xx(state))
		cx231xx_initialize(client);
	else
		cx25840_initialize(client);

	state->is_initialized = 1;

	return 0;
}

/*
 * This load_fw operation must be called to load the driver's firmware.
 * This will load the firmware on the first invocation (further ones are NOP).
 * Without this the audio standard detection will fail and you will
 * only get mono.
 * Alternatively, you can call the reset operation instead of this one.
 *
 * Since loading the firmware is often problematic when the driver is
 * compiled into the kernel I recommend postponing calling this function
 * until the first open of the video device. Another reason for
 * postponing it is that loading this firmware takes a long time (seconds)
 * due to the slow i2c bus speed. So it will speed up the boot process if
 * you can avoid loading the fw as long as the video device isn't used.
 */
static int cx25840_load_fw(struct v4l2_subdev *sd)
{
	struct cx25840_state *state = to_state(sd);

	if (!state->is_initialized) {
		/* initialize and load firmware */
		cx25840_reset(sd, 0);
	}
	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int cx25840_g_register(struct v4l2_subdev *sd,
			      struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	reg->size = 1;
	reg->val = cx25840_read(client, reg->reg & 0x0fff);
	return 0;
}

static int cx25840_s_register(struct v4l2_subdev *sd,
			      const struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	cx25840_write(client, reg->reg & 0x0fff, reg->val & 0xff);
	return 0;
}
#endif

static int cx25840_s_audio_stream(struct v4l2_subdev *sd, int enable)
{
	struct cx25840_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u8 v;

	if (is_cx2583x(state) || is_cx2388x(state) || is_cx231xx(state))
		return 0;

	v4l_dbg(1, cx25840_debug, client, "%s audio output\n",
		enable ? "enable" : "disable");

	if (enable) {
		v = cx25840_read(client, 0x115) | 0x80;
		cx25840_write(client, 0x115, v);
		v = cx25840_read(client, 0x116) | 0x03;
		cx25840_write(client, 0x116, v);
	} else {
		v = cx25840_read(client, 0x115) & ~(0x80);
		cx25840_write(client, 0x115, v);
		v = cx25840_read(client, 0x116) & ~(0x03);
		cx25840_write(client, 0x116, v);
	}
	return 0;
}

static int cx25840_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct cx25840_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u8 v;

	v4l_dbg(1, cx25840_debug, client, "%s video output\n",
		enable ? "enable" : "disable");

	/*
	 * It's not clear what should be done for these devices.
	 * The original code used the same addresses as for the cx25840, but
	 * those addresses do something else entirely on the cx2388x and
	 * cx231xx. Since it never did anything in the first place, just do
	 * nothing.
	 */
	if (is_cx2388x(state) || is_cx231xx(state))
		return 0;

	if (enable) {
		v = cx25840_read(client, 0x115) | 0x0c;
		cx25840_write(client, 0x115, v);
		v = cx25840_read(client, 0x116) | 0x04;
		cx25840_write(client, 0x116, v);
	} else {
		v = cx25840_read(client, 0x115) & ~(0x0c);
		cx25840_write(client, 0x115, v);
		v = cx25840_read(client, 0x116) & ~(0x04);
		cx25840_write(client, 0x116, v);
	}
	return 0;
}

/* Query the current detected video format */
static int cx25840_querystd(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	static const v4l2_std_id stds[] = {
		/* 0000 */ V4L2_STD_UNKNOWN,

		/* 0001 */ V4L2_STD_NTSC_M,
		/* 0010 */ V4L2_STD_NTSC_M_JP,
		/* 0011 */ V4L2_STD_NTSC_443,
		/* 0100 */ V4L2_STD_PAL,
		/* 0101 */ V4L2_STD_PAL_M,
		/* 0110 */ V4L2_STD_PAL_N,
		/* 0111 */ V4L2_STD_PAL_Nc,
		/* 1000 */ V4L2_STD_PAL_60,

		/* 1001 */ V4L2_STD_UNKNOWN,
		/* 1010 */ V4L2_STD_UNKNOWN,
		/* 1011 */ V4L2_STD_UNKNOWN,
		/* 1100 */ V4L2_STD_SECAM,
		/* 1101 */ V4L2_STD_UNKNOWN,
		/* 1110 */ V4L2_STD_UNKNOWN,
		/* 1111 */ V4L2_STD_UNKNOWN
	};

	u32 fmt = (cx25840_read4(client, 0x40c) >> 8) & 0xf;
	*std = stds[fmt];

	v4l_dbg(1, cx25840_debug, client,
		"querystd fmt = %x, v4l2_std_id = 0x%x\n",
		fmt, (unsigned int)stds[fmt]);

	return 0;
}

static int cx25840_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	/*
	 * A limited function that checks for signal status and returns
	 * the state.
	 */

	/* Check for status of Horizontal lock (SRC lock isn't reliable) */
	if ((cx25840_read4(client, 0x40c) & 0x00010000) == 0)
		*status |= V4L2_IN_ST_NO_SIGNAL;

	return 0;
}

static int cx25840_g_std(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	struct cx25840_state *state = to_state(sd);

	*std = state->std;

	return 0;
}

static int cx25840_s_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct cx25840_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (state->radio == 0 && state->std == std)
		return 0;
	state->radio = 0;
	state->std = std;
	return set_v4lstd(client);
}

static int cx25840_s_radio(struct v4l2_subdev *sd)
{
	struct cx25840_state *state = to_state(sd);

	state->radio = 1;
	return 0;
}

static int cx25840_s_video_routing(struct v4l2_subdev *sd,
				   u32 input, u32 output, u32 config)
{
	struct cx25840_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (is_cx23888(state))
		cx23888_std_setup(client);

	if (is_cx2584x(state) && state->generic_mode && config) {
		cx25840_vconfig_add(state, config);
		cx25840_vconfig_apply(client);
	}

	return set_input(client, input, state->aud_input);
}

static int cx25840_s_audio_routing(struct v4l2_subdev *sd,
				   u32 input, u32 output, u32 config)
{
	struct cx25840_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (is_cx23888(state))
		cx23888_std_setup(client);
	return set_input(client, state->vid_input, input);
}

static int cx25840_s_frequency(struct v4l2_subdev *sd,
			       const struct v4l2_frequency *freq)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	input_change(client);
	return 0;
}

static int cx25840_g_tuner(struct v4l2_subdev *sd, struct v4l2_tuner *vt)
{
	struct cx25840_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u8 vpres = cx25840_read(client, 0x40e) & 0x20;
	u8 mode;
	int val = 0;

	if (state->radio)
		return 0;

	vt->signal = vpres ? 0xffff : 0x0;
	if (is_cx2583x(state))
		return 0;

	vt->capability |= V4L2_TUNER_CAP_STEREO | V4L2_TUNER_CAP_LANG1 |
			  V4L2_TUNER_CAP_LANG2 | V4L2_TUNER_CAP_SAP;

	mode = cx25840_read(client, 0x804);

	/* get rxsubchans and audmode */
	if ((mode & 0xf) == 1)
		val |= V4L2_TUNER_SUB_STEREO;
	else
		val |= V4L2_TUNER_SUB_MONO;

	if (mode == 2 || mode == 4)
		val = V4L2_TUNER_SUB_LANG1 | V4L2_TUNER_SUB_LANG2;

	if (mode & 0x10)
		val |= V4L2_TUNER_SUB_SAP;

	vt->rxsubchans = val;
	vt->audmode = state->audmode;
	return 0;
}

static int cx25840_s_tuner(struct v4l2_subdev *sd, const struct v4l2_tuner *vt)
{
	struct cx25840_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (state->radio || is_cx2583x(state))
		return 0;

	switch (vt->audmode) {
	case V4L2_TUNER_MODE_MONO:
		/*
		 * mono      -> mono
		 * stereo    -> mono
		 * bilingual -> lang1
		 */
		cx25840_and_or(client, 0x809, ~0xf, 0x00);
		break;
	case V4L2_TUNER_MODE_STEREO:
	case V4L2_TUNER_MODE_LANG1:
		/*
		 * mono      -> mono
		 * stereo    -> stereo
		 * bilingual -> lang1
		 */
		cx25840_and_or(client, 0x809, ~0xf, 0x04);
		break;
	case V4L2_TUNER_MODE_LANG1_LANG2:
		/*
		 * mono      -> mono
		 * stereo    -> stereo
		 * bilingual -> lang1/lang2
		 */
		cx25840_and_or(client, 0x809, ~0xf, 0x07);
		break;
	case V4L2_TUNER_MODE_LANG2:
		/*
		 * mono      -> mono
		 * stereo    -> stereo
		 * bilingual -> lang2
		 */
		cx25840_and_or(client, 0x809, ~0xf, 0x01);
		break;
	default:
		return -EINVAL;
	}
	state->audmode = vt->audmode;
	return 0;
}

static int cx25840_log_status(struct v4l2_subdev *sd)
{
	struct cx25840_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	log_video_status(client);
	if (!is_cx2583x(state))
		log_audio_status(client);
	cx25840_ir_log_status(sd);
	v4l2_ctrl_handler_log_status(&state->hdl, sd->name);
	return 0;
}

static int cx23885_irq_handler(struct v4l2_subdev *sd, u32 status,
			       bool *handled)
{
	struct cx25840_state *state = to_state(sd);
	struct i2c_client *c = v4l2_get_subdevdata(sd);
	u8 irq_stat, aud_stat, aud_en, ir_stat, ir_en;
	u32 vid_stat, aud_mc_stat;
	bool block_handled;
	int ret = 0;

	irq_stat = cx25840_read(c, CX23885_PIN_CTRL_IRQ_REG);
	v4l_dbg(2, cx25840_debug, c, "AV Core IRQ status (entry): %s %s %s\n",
		irq_stat & CX23885_PIN_CTRL_IRQ_IR_STAT ? "ir" : "  ",
		irq_stat & CX23885_PIN_CTRL_IRQ_AUD_STAT ? "aud" : "   ",
		irq_stat & CX23885_PIN_CTRL_IRQ_VID_STAT ? "vid" : "   ");

	if ((is_cx23885(state) || is_cx23887(state))) {
		ir_stat = cx25840_read(c, CX25840_IR_STATS_REG);
		ir_en = cx25840_read(c, CX25840_IR_IRQEN_REG);
		v4l_dbg(2, cx25840_debug, c,
			"AV Core ir IRQ status: %#04x disables: %#04x\n",
			ir_stat, ir_en);
		if (irq_stat & CX23885_PIN_CTRL_IRQ_IR_STAT) {
			block_handled = false;
			ret = cx25840_ir_irq_handler(sd,
						     status, &block_handled);
			if (block_handled)
				*handled = true;
		}
	}

	aud_stat = cx25840_read(c, CX25840_AUD_INT_STAT_REG);
	aud_en = cx25840_read(c, CX25840_AUD_INT_CTRL_REG);
	v4l_dbg(2, cx25840_debug, c,
		"AV Core audio IRQ status: %#04x disables: %#04x\n",
		aud_stat, aud_en);
	aud_mc_stat = cx25840_read4(c, CX23885_AUD_MC_INT_MASK_REG);
	v4l_dbg(2, cx25840_debug, c,
		"AV Core audio MC IRQ status: %#06x enables: %#06x\n",
		aud_mc_stat >> CX23885_AUD_MC_INT_STAT_SHFT,
		aud_mc_stat & CX23885_AUD_MC_INT_CTRL_BITS);
	if (irq_stat & CX23885_PIN_CTRL_IRQ_AUD_STAT) {
		if (aud_stat) {
			cx25840_write(c, CX25840_AUD_INT_STAT_REG, aud_stat);
			*handled = true;
		}
	}

	vid_stat = cx25840_read4(c, CX25840_VID_INT_STAT_REG);
	v4l_dbg(2, cx25840_debug, c,
		"AV Core video IRQ status: %#06x disables: %#06x\n",
		vid_stat & CX25840_VID_INT_STAT_BITS,
		vid_stat >> CX25840_VID_INT_MASK_SHFT);
	if (irq_stat & CX23885_PIN_CTRL_IRQ_VID_STAT) {
		if (vid_stat & CX25840_VID_INT_STAT_BITS) {
			cx25840_write4(c, CX25840_VID_INT_STAT_REG, vid_stat);
			*handled = true;
		}
	}

	irq_stat = cx25840_read(c, CX23885_PIN_CTRL_IRQ_REG);
	v4l_dbg(2, cx25840_debug, c, "AV Core IRQ status (exit): %s %s %s\n",
		irq_stat & CX23885_PIN_CTRL_IRQ_IR_STAT ? "ir" : "  ",
		irq_stat & CX23885_PIN_CTRL_IRQ_AUD_STAT ? "aud" : "   ",
		irq_stat & CX23885_PIN_CTRL_IRQ_VID_STAT ? "vid" : "   ");

	return ret;
}

static int cx25840_irq_handler(struct v4l2_subdev *sd, u32 status,
			       bool *handled)
{
	struct cx25840_state *state = to_state(sd);

	*handled = false;

	/* Only support the CX2388[578] AV Core for now */
	if (is_cx2388x(state))
		return cx23885_irq_handler(sd, status, handled);

	return -ENODEV;
}

/* ----------------------------------------------------------------------- */

#define DIF_PLL_FREQ_WORD	(0x300)
#define DIF_BPF_COEFF01		(0x348)
#define DIF_BPF_COEFF23		(0x34c)
#define DIF_BPF_COEFF45		(0x350)
#define DIF_BPF_COEFF67		(0x354)
#define DIF_BPF_COEFF89		(0x358)
#define DIF_BPF_COEFF1011	(0x35c)
#define DIF_BPF_COEFF1213	(0x360)
#define DIF_BPF_COEFF1415	(0x364)
#define DIF_BPF_COEFF1617	(0x368)
#define DIF_BPF_COEFF1819	(0x36c)
#define DIF_BPF_COEFF2021	(0x370)
#define DIF_BPF_COEFF2223	(0x374)
#define DIF_BPF_COEFF2425	(0x378)
#define DIF_BPF_COEFF2627	(0x37c)
#define DIF_BPF_COEFF2829	(0x380)
#define DIF_BPF_COEFF3031	(0x384)
#define DIF_BPF_COEFF3233	(0x388)
#define DIF_BPF_COEFF3435	(0x38c)
#define DIF_BPF_COEFF36		(0x390)

static void cx23885_dif_setup(struct i2c_client *client, u32 ifHz)
{
	u64 pll_freq;
	u32 pll_freq_word;

	v4l_dbg(1, cx25840_debug, client, "%s(%d)\n", __func__, ifHz);

	/* Assuming TV */
	/* Calculate the PLL frequency word based on the adjusted ifHz */
	pll_freq = div_u64((u64)ifHz * 268435456, 50000000);
	pll_freq_word = (u32)pll_freq;

	cx25840_write4(client, DIF_PLL_FREQ_WORD,  pll_freq_word);

	/* Round down to the nearest 100KHz */
	ifHz = (ifHz / 100000) * 100000;

	if (ifHz < 3000000)
		ifHz = 3000000;

	if (ifHz > 16000000)
		ifHz = 16000000;

	v4l_dbg(1, cx25840_debug, client, "%s(%d) again\n", __func__, ifHz);

	switch (ifHz) {
	case 3000000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000002);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00080012);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x001e0024);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x001bfff8);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xffb4ff50);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xfed8fe68);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe24fe34);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfebaffc7);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x014d031f);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x04f0065d);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x07010688);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x04c901d6);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xfe00f9d3);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf600f342);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf235f337);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf64efb22);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0105070f);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x0c460fce);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 3100000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000001);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00070012);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x00220032);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x00370026);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xfff0ff91);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xff0efe7c);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe01fdcc);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfe0afedb);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x00440224);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x0434060c);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x0738074e);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x06090361);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xff99fb39);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf6fef3b6);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf21af2a5);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf573fa33);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0034067d);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x0bfb0fb9);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 3200000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000000);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0004000e);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x00200038);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x004c004f);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x002fffdf);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xff5cfeb6);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe0dfd92);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd7ffe03);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xff36010a);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x03410575);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x072607d2);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x071804d5);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x0134fcb7);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf81ff451);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf223f22e);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf4a7f94b);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xff6405e8);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x0bae0fa4);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 3300000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000ffff);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00000008);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x001a0036);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0056006d);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x00670030);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xffbdff10);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe46fd8d);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd25fd4f);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfe35ffe0);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x0224049f);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x06c9080e);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x07ef0627);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x02c9fe45);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf961f513);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf250f1d2);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf3ecf869);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xfe930552);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x0b5f0f8f);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 3400000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xfffffffe);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffd0001);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x000f002c);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0054007d);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x0093007c);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x0024ff82);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfea6fdbb);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd03fcca);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfd51feb9);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x00eb0392);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x06270802);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x08880750);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x044dffdb);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xfabdf5f8);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf2a0f193);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf342f78f);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xfdc404b9);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x0b0e0f78);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 3500000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xfffffffd);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffafff9);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x0002001b);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0046007d);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x00ad00ba);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x00870000);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xff26fe1a);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd1bfc7e);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfc99fda4);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xffa5025c);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x054507ad);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x08dd0847);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x05b80172);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xfc2ef6ff);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf313f170);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf2abf6bd);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xfcf6041f);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x0abc0f61);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 3600000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xfffffffd);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff8fff3);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xfff50006);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x002f006c);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x00b200e3);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x00dc007e);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xffb9fea0);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd6bfc71);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfc17fcb1);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfe65010b);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x042d0713);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x08ec0906);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x07020302);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xfdaff823);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf3a7f16a);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf228f5f5);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xfc2a0384);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x0a670f4a);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 3700000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffd);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff7ffef);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffe9fff1);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0010004d);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x00a100f2);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x011a00f0);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x0053ff44);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfdedfca2);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfbd3fbef);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfd39ffae);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x02ea0638);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x08b50987);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x08230483);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xff39f960);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf45bf180);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf1b8f537);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xfb6102e7);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x0a110f32);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 3800000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffe);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff9ffee);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffe1ffdd);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xfff00024);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x007c00e5);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x013a014a);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x00e6fff8);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfe98fd0f);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfbd3fb67);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfc32fe54);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x01880525);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x083909c7);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x091505ee);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x00c7fab3);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf52df1b4);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf15df484);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xfa9b0249);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x09ba0f19);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 3900000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000000);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffbfff0);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffdeffcf);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffd1fff6);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x004800be);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x01390184);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x016300ac);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xff5efdb1);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfc17fb23);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfb5cfd0d);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x001703e4);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x077b09c4);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x09d2073c);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0251fc18);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf61cf203);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf118f3dc);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf9d801aa);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x09600eff);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 4000000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000001);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffefff4);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffe1ffc8);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffbaffca);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x000b0082);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x01170198);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01c10152);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x0030fe7b);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfc99fb24);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfac3fbe9);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfea5027f);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x0683097f);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x0a560867);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x03d2fd89);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf723f26f);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf0e8f341);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf919010a);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x09060ee5);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 4100000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00010002);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0002fffb);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffe8ffca);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffacffa4);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xffcd0036);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x00d70184);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01f601dc);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x00ffff60);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfd51fb6d);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfa6efaf5);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfd410103);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x055708f9);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x0a9e0969);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0543ff02);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf842f2f5);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf0cef2b2);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf85e006b);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x08aa0ecb);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 4200000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00010003);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00050003);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xfff3ffd3);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffaaff8b);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff95ffe5);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x0080014a);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01fe023f);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x01ba0050);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfe35fbf8);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfa62fa3b);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfbf9ff7e);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x04010836);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x0aa90a3d);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x069f007f);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf975f395);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf0cbf231);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf7a9ffcb);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x084c0eaf);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 4300000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00010003);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0008000a);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x0000ffe4);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffb4ff81);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff6aff96);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x001c00f0);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01d70271);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x0254013b);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xff36fcbd);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfa9ff9c5);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfadbfdfe);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x028c073b);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x0a750adf);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x07e101fa);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xfab8f44e);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf0ddf1be);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf6f9ff2b);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x07ed0e94);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 4400000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000003);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0009000f);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x000efff8);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffc9ff87);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff52ff54);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xffb5007e);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01860270);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x02c00210);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x0044fdb2);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfb22f997);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xf9f2fc90);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x0102060f);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x0a050b4c);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0902036e);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xfc0af51e);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf106f15a);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf64efe8b);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x078d0e77);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 4500000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000002);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00080012);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x0019000e);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffe5ff9e);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff4fff25);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xff560000);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x0112023b);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x02f702c0);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x014dfec8);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfbe5f9b3);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xf947fb41);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xff7004b9);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x095a0b81);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0a0004d8);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xfd65f603);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf144f104);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf5aafdec);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x072b0e5a);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 4600000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000001);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00060012);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x00200022);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0005ffc1);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff61ff10);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xff09ff82);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x008601d7);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x02f50340);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x0241fff0);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfcddfa19);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xf8e2fa1e);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xfde30343);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x08790b7f);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0ad50631);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xfec7f6fc);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf198f0bd);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf50dfd4e);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x06c90e3d);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 4700000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000ffff);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0003000f);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x00220030);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0025ffed);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff87ff15);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xfed6ff10);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xffed014c);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x02b90386);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x03110119);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfdfefac4);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xf8c6f92f);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xfc6701b7);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x07670b44);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0b7e0776);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x002df807);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf200f086);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf477fcb1);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x06650e1e);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 4800000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xfffffffe);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xffff0009);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x001e0038);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x003f001b);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xffbcff36);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xfec2feb6);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xff5600a5);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x0248038d);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x03b00232);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xff39fbab);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xf8f4f87f);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xfb060020);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x062a0ad2);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0bf908a3);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0192f922);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf27df05e);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf3e8fc14);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x06000e00);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 4900000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xfffffffd);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffc0002);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x00160037);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x00510046);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xfff9ff6d);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xfed0fe7c);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfecefff0);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x01aa0356);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x0413032b);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x007ffcc5);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xf96cf812);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf9cefe87);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x04c90a2c);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0c4309b4);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x02f3fa4a);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf30ef046);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf361fb7a);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x059b0de0);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 5000000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xfffffffd);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff9fffa);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x000a002d);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x00570067);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x0037ffb5);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xfefffe68);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe62ff3d);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x00ec02e3);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x043503f6);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x01befe05);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfa27f7ee);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf8c6fcf8);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x034c0954);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0c5c0aa4);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x044cfb7e);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf3b1f03f);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf2e2fae1);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x05340dc0);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 5100000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffd);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff8fff4);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xfffd001e);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0051007b);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x006e0006);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xff48fe7c);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe1bfe9a);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x001d023e);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x04130488);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x02e6ff5b);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfb1ef812);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf7f7fb7f);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x01bc084e);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0c430b72);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x059afcba);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf467f046);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf26cfa4a);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x04cd0da0);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 5200000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffe);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff8ffef);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xfff00009);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x003f007f);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x00980056);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xffa5feb6);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe00fe15);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xff4b0170);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x03b004d7);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x03e800b9);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfc48f87f);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf768fa23);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x0022071f);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0bf90c1b);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x06dafdfd);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf52df05e);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf1fef9b5);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x04640d7f);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 5300000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000ffff);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff9ffee);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffe6fff3);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x00250072);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x00af009c);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x000cff10);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe13fdb8);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfe870089);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x031104e1);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x04b8020f);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfd98f92f);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf71df8f0);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xfe8805ce);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0b7e0c9c);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0808ff44);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf603f086);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf19af922);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x03fb0d5e);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 5400000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000001);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffcffef);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffe0ffe0);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x00050056);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x00b000d1);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x0071ff82);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe53fd8c);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfddfff99);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x024104a3);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x054a034d);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xff01fa1e);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf717f7ed);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xfcf50461);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0ad50cf4);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0921008d);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf6e7f0bd);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf13ff891);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x03920d3b);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 5500000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00010002);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffffff3);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffdeffd1);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffe5002f);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x009c00ed);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x00cb0000);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfebafd94);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd61feb0);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x014d0422);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x05970464);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x0074fb41);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf759f721);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xfb7502de);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0a000d21);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0a2201d4);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf7d9f104);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf0edf804);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x03280d19);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 5600000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00010003);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0003fffa);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffe3ffc9);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffc90002);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x007500ef);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x010e007e);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xff3dfdcf);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd16fddd);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x00440365);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x059b0548);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x01e3fc90);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf7dff691);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xfa0f014d);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x09020d23);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0b0a0318);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf8d7f15a);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf0a5f779);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x02bd0cf6);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 5700000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00010003);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00060001);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffecffc9);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffb4ffd4);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x004000d5);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x013600f0);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xffd3fe39);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd04fd31);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xff360277);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x055605ef);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x033efdfe);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf8a5f642);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf8cbffb6);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x07e10cfb);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0bd50456);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf9dff1be);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf067f6f2);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x02520cd2);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 5800000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000003);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00080009);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xfff8ffd2);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffaaffac);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x000200a3);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x013c014a);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x006dfec9);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd2bfcb7);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfe350165);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x04cb0651);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x0477ff7e);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf9a5f635);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf7b1fe20);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x069f0ca8);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0c81058b);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xfaf0f231);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf033f66d);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x01e60cae);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 5900000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000002);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0009000e);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x0005ffe1);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffacff90);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xffc5005f);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x01210184);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x00fcff72);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd8afc77);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfd51003f);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x04020669);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x05830103);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xfad7f66b);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf6c8fc93);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x05430c2b);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0d0d06b5);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xfc08f2b2);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf00af5ec);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x017b0c89);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 6000000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000001);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00070012);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x0012fff5);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffbaff82);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff8e000f);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x00e80198);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01750028);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfe18fc75);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfc99ff15);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x03050636);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x0656027f);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xfc32f6e2);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf614fb17);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x03d20b87);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0d7707d2);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xfd26f341);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xefeaf56f);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x010f0c64);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 6100000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xffff0000);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00050012);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x001c000b);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffd1ff84);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff66ffbe);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x00960184);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01cd00da);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfeccfcb2);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfc17fdf9);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x01e005bc);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x06e703e4);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xfdabf798);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf599f9b3);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x02510abd);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0dbf08df);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xfe48f3dc);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xefd5f4f6);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x00a20c3e);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 6200000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xfffffffe);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0002000f);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x0021001f);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xfff0ff97);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff50ff74);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x0034014a);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01fa0179);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xff97fd2a);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfbd3fcfa);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x00a304fe);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x07310525);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xff37f886);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf55cf86e);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x00c709d0);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0de209db);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xff6df484);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xefcbf481);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0x00360c18);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 6300000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xfffffffd);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffe000a);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x0021002f);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0010ffb8);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff50ff3b);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xffcc00f0);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01fa01fa);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x0069fdd4);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfbd3fc26);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xff5d0407);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x07310638);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x00c9f9a8);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf55cf74e);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xff3908c3);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0de20ac3);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0093f537);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xefcbf410);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xffca0bf2);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 6400000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xfffffffd);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffb0003);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x001c0037);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x002fffe2);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff66ff17);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xff6a007e);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01cd0251);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x0134fea5);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfc17fb8b);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfe2002e0);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x06e70713);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x0255faf5);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf599f658);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xfdaf0799);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0dbf0b96);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x01b8f5f5);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xefd5f3a3);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xff5e0bca);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 6500000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffd);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff9fffb);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x00120037);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x00460010);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff8eff0f);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xff180000);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01750276);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x01e8ff8d);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfc99fb31);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfcfb0198);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x065607ad);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x03cefc64);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf614f592);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xfc2e0656);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0d770c52);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x02daf6bd);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xefeaf33b);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xfef10ba3);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 6600000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffe);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff7fff5);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x0005002f);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0054003c);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xffc5ff22);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xfedfff82);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x00fc0267);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x0276007e);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfd51fb1c);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfbfe003e);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x05830802);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x0529fdec);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf6c8f4fe);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xfabd04ff);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0d0d0cf6);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x03f8f78f);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf00af2d7);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xfe850b7b);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 6700000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000ffff);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff8fff0);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xfff80020);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x00560060);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x0002ff4e);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xfec4ff10);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x006d0225);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x02d50166);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfe35fb4e);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfb35fee1);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x0477080e);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x065bff82);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf7b1f4a0);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf9610397);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0c810d80);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0510f869);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf033f278);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xfe1a0b52);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 6800000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00010000);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffaffee);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffec000c);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x004c0078);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x0040ff8e);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xfecafeb6);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xffd301b6);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x02fc0235);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xff36fbc5);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfaaafd90);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x033e07d2);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x075b011b);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf8cbf47a);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf81f0224);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0bd50def);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0621f94b);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf067f21e);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xfdae0b29);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 6900000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00010001);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffdffef);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffe3fff6);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0037007f);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x0075ffdc);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xfef2fe7c);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xff3d0122);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x02ea02dd);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x0044fc79);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfa65fc5d);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x01e3074e);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x082102ad);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xfa0ff48c);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf6fe00a9);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0b0a0e43);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0729fa33);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf0a5f1c9);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xfd430b00);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 7000000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00010002);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0001fff3);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffdeffe2);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x001b0076);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x009c002d);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xff35fe68);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfeba0076);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x029f0352);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x014dfd60);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfa69fb53);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x00740688);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x08a7042d);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xfb75f4d6);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf600ff2d);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0a220e7a);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0827fb22);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf0edf17a);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xfcd80ad6);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 7100000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000003);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0004fff9);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffe0ffd2);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xfffb005e);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x00b0007a);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xff8ffe7c);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe53ffc1);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x0221038c);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x0241fe6e);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfab6fa80);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xff010587);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x08e90590);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xfcf5f556);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf52bfdb3);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x09210e95);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0919fc15);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf13ff12f);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xfc6e0aab);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 7200000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000003);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00070000);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffe6ffc9);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffdb0039);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x00af00b8);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xfff4feb6);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe13ff10);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x01790388);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x0311ff92);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfb48f9ed);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfd980453);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x08e306cd);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xfe88f60a);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf482fc40);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x08080e93);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x09fdfd0c);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf19af0ea);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xfc050a81);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 7300000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000002);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00080008);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xfff0ffc9);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffc1000d);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x009800e2);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x005bff10);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe00fe74);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x00b50345);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x03b000bc);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfc18f9a1);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfc4802f9);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x089807dc);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x0022f6f0);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf407fada);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x06da0e74);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0ad3fe06);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf1fef0ab);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xfb9c0a55);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 7400000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000001);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0008000e);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xfffdffd0);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffafffdf);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x006e00f2);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x00b8ff82);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe1bfdf8);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xffe302c8);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x041301dc);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfd1af99e);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfb1e0183);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x080908b5);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x01bcf801);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf3bdf985);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x059a0e38);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0b99ff03);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf26cf071);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xfb330a2a);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 7500000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xffff0000);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00070011);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x000affdf);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffa9ffb5);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x003700e6);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x01010000);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe62fda8);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xff140219);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x043502e1);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfe42f9e6);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfa270000);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x073a0953);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x034cf939);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf3a4f845);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x044c0de1);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0c4f0000);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf2e2f03c);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xfacc09fe);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 7600000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xffffffff);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00040012);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x0016fff3);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffafff95);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xfff900c0);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x0130007e);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfecefd89);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfe560146);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x041303bc);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xff81fa76);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xf96cfe7d);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x063209b1);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x04c9fa93);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf3bdf71e);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x02f30d6e);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0cf200fd);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf361f00e);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xfa6509d1);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 7700000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xfffffffe);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00010010);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x001e0008);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffc1ff84);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xffbc0084);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x013e00f0);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xff56fd9f);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfdb8005c);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x03b00460);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x00c7fb45);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xf8f4fd07);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x04fa09ce);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x062afc07);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf407f614);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x01920ce0);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0d8301fa);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf3e8efe5);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xfa0009a4);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 7800000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffd);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffd000b);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x0022001d);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffdbff82);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff870039);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x012a014a);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xffedfde7);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd47ff6b);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x031104c6);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x0202fc4c);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xf8c6fbad);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x039909a7);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x0767fd8e);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf482f52b);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x002d0c39);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0e0002f4);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf477efc2);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf99b0977);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 7900000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffd);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffa0004);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x0020002d);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xfffbff91);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff61ffe8);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x00f70184);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x0086fe5c);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd0bfe85);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x024104e5);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x0323fd7d);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xf8e2fa79);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x021d093f);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x0879ff22);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf52bf465);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xfec70b79);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0e6803eb);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf50defa5);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf937094a);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 8000000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffe);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff8fffd);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x00190036);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x001bffaf);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff4fff99);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x00aa0198);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x0112fef3);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd09fdb9);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x014d04be);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x041bfecc);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xf947f978);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x00900897);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x095a00b9);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf600f3c5);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xfd650aa3);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0ebc04de);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf5aaef8e);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf8d5091c);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 8100000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000ffff);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff7fff6);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x000e0038);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0037ffd7);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff52ff56);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x004b0184);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x0186ffa1);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd40fd16);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x00440452);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x04de0029);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xf9f2f8b2);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xfefe07b5);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x0a05024d);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf6fef34d);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xfc0a09b8);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0efa05cd);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf64eef7d);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf87308ed);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 8200000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00010000);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff8fff0);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x00000031);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x004c0005);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff6aff27);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xffe4014a);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01d70057);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfdacfca6);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xff3603a7);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x05610184);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfadbf82e);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xfd74069f);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x0a7503d6);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf81ff2ff);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xfab808b9);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0f2306b5);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf6f9ef72);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf81308bf);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 8300000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00010001);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffbffee);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xfff30022);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x00560032);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff95ff10);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xff8000f0);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01fe0106);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfe46fc71);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfe3502c7);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x059e02ce);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfbf9f7f2);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xfbff055b);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x0aa9054c);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf961f2db);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf97507aa);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0f350797);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf7a9ef6d);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf7b40890);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 8400000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00010002);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffeffee);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffe8000f);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x00540058);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xffcdff14);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xff29007e);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01f6019e);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xff01fc7c);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfd5101bf);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x059203f6);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfd41f7fe);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xfaa903f3);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x0a9e06a9);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xfabdf2e2);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf842068b);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0f320871);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf85eef6e);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf7560860);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 8500000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000003);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0002fff2);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffe1fff9);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x00460073);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x000bff34);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xfee90000);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01c10215);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xffd0fcc5);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfc99009d);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x053d04f1);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfea5f853);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf97d0270);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x0a5607e4);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xfc2ef314);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf723055f);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0f180943);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf919ef75);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf6fa0830);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 8600000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000003);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0005fff8);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffdeffe4);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x002f007f);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x0048ff6b);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xfec7ff82);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x0163025f);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x00a2fd47);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfc17ff73);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x04a405b2);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x0017f8ed);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf88500dc);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x09d208f9);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xfdaff370);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf61c0429);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0ee80a0b);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xf9d8ef82);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf6a00800);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 8700000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000003);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0007ffff);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffe1ffd4);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0010007a);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x007cffb2);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xfec6ff10);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x00e60277);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x0168fdf9);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfbd3fe50);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x03ce0631);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x0188f9c8);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf7c7ff43);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x091509e3);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xff39f3f6);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf52d02ea);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0ea30ac9);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xfa9bef95);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf64607d0);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 8800000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000002);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00090007);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffe9ffca);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xfff00065);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x00a10003);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xfee6feb6);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x0053025b);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x0213fed0);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfbd3fd46);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x02c70668);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x02eafadb);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf74bfdae);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x08230a9c);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x00c7f4a3);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf45b01a6);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0e480b7c);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xfb61efae);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf5ef079f);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 8900000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xffff0000);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0008000d);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xfff5ffc8);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffd10043);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x00b20053);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xff24fe7c);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xffb9020c);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x0295ffbb);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfc17fc64);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x019b0654);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x042dfc1c);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf714fc2a);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x07020b21);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0251f575);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf3a7005e);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0dd80c24);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xfc2aefcd);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf599076e);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 9000000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xffffffff);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00060011);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x0002ffcf);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffba0018);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x00ad009a);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xff79fe68);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xff260192);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x02e500ab);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfc99fbb6);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x005b05f7);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x0545fd81);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf723fabf);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x05b80b70);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x03d2f669);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf313ff15);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0d550cbf);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xfcf6eff2);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf544073d);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 9100000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xfffffffe);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00030012);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x000fffdd);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffacffea);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x009300cf);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xffdcfe7c);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfea600f7);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x02fd0190);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfd51fb46);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xff150554);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x0627fefd);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf778f978);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x044d0b87);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0543f77d);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf2a0fdcf);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0cbe0d4e);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xfdc4f01d);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf4f2070b);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 9200000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffd);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00000010);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x001afff0);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffaaffbf);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x006700ed);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x0043feb6);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe460047);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x02db0258);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfe35fb1b);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfddc0473);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x06c90082);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf811f85e);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x02c90b66);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x069ff8ad);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf250fc8d);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0c140dcf);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xfe93f04d);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf4a106d9);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 9300000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffd);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffc000c);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x00200006);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffb4ff9c);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x002f00ef);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x00a4ff10);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe0dff92);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x028102f7);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xff36fb37);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfcbf035e);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x07260202);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf8e8f778);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x01340b0d);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x07e1f9f4);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf223fb51);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0b590e42);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xff64f083);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf45206a7);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 9400000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffd);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff90005);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x0022001a);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffc9ff86);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xfff000d7);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x00f2ff82);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe01fee5);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x01f60362);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x0044fb99);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfbcc0222);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x07380370);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf9f7f6cc);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xff990a7e);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0902fb50);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf21afa1f);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0a8d0ea6);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0034f0bf);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf4050675);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 9500000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffe);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff8fffe);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x001e002b);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffe5ff81);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xffb400a5);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x01280000);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe24fe50);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x01460390);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x014dfc3a);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfb1000ce);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x070104bf);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xfb37f65f);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xfe0009bc);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0a00fcbb);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf235f8f8);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x09b20efc);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0105f101);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf3ba0642);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 9600000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0001ffff);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff8fff7);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x00150036);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0005ff8c);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff810061);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x013d007e);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe71fddf);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x007c0380);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x0241fd13);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfa94ff70);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x068005e2);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xfc9bf633);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xfc7308ca);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0ad5fe30);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf274f7e0);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x08c90f43);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x01d4f147);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf371060f);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 9700000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00010001);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff9fff1);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x00090038);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0025ffa7);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff5e0012);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x013200f0);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfee3fd9b);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xffaa0331);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x0311fe15);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfa60fe18);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x05bd06d1);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xfe1bf64a);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xfafa07ae);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0b7effab);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf2d5f6d7);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x07d30f7a);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x02a3f194);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf32905dc);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 9800000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00010002);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffcffee);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xfffb0032);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x003fffcd);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff4effc1);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x0106014a);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xff6efd8a);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfedd02aa);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x03b0ff34);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfa74fcd7);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x04bf0781);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xffaaf6a3);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf99e066b);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0bf90128);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf359f5e1);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x06d20fa2);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0370f1e5);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf2e405a8);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 9900000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000003);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xffffffee);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffef0024);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0051fffa);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff54ff77);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x00be0184);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x0006fdad);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfe2701f3);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x0413005e);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfad1fbba);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x039007ee);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x013bf73d);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf868050a);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0c4302a1);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf3fdf4fe);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x05c70fba);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x043bf23c);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf2a10575);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 10000000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000003);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0003fff1);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffe50011);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x00570027);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff70ff3c);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x00620198);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x009efe01);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd95011a);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x04350183);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfb71fad0);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x023c0812);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x02c3f811);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf75e0390);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0c5c0411);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf4c1f432);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x04b30fc1);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0503f297);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf2610541);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 10100000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000003);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0006fff7);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffdffffc);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x00510050);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff9dff18);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xfffc0184);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x0128fe80);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd32002e);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x04130292);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfc4dfa21);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x00d107ee);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x0435f91c);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf6850205);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0c430573);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf5a1f37d);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x03990fba);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x05c7f2f8);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf222050d);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 10200000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000002);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0008fffe);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffdfffe7);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x003f006e);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xffd6ff0f);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xff96014a);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x0197ff1f);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd05ff3e);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x03b0037c);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfd59f9b7);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xff5d0781);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x0585fa56);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf5e4006f);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0bf906c4);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf69df2e0);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x02790fa2);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0688f35d);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf1e604d8);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 10300000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xffff0001);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00090005);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffe4ffd6);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0025007e);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x0014ff20);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xff3c00f0);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01e1ffd0);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd12fe5c);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x03110433);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfe88f996);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfdf106d1);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x06aafbb7);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf57efed8);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0b7e07ff);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf7b0f25e);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x01560f7a);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0745f3c7);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf1ac04a4);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 10400000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xffffffff);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0008000c);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffedffcb);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0005007d);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x0050ff4c);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xfef6007e);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01ff0086);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd58fd97);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x024104ad);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xffcaf9c0);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfc9905e2);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x079afd35);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf555fd46);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0ad50920);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf8d9f1f6);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x00310f43);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x07fdf435);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf174046f);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 10500000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xfffffffe);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00050011);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xfffaffc8);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffe5006b);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x0082ff8c);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xfecc0000);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01f00130);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfdd2fcfc);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x014d04e3);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x010efa32);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfb6404bf);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x084efec5);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf569fbc2);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0a000a23);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xfa15f1ab);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xff0b0efc);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x08b0f4a7);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf13f043a);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 10600000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffd);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00020012);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x0007ffcd);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffc9004c);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x00a4ffd9);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xfec3ff82);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01b401c1);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfe76fc97);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x004404d2);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x0245fae8);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfa5f0370);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x08c1005f);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf5bcfa52);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x09020b04);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xfb60f17b);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xfde70ea6);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x095df51e);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf10c0405);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 10700000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffd);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xffff0011);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x0014ffdb);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffb40023);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x00b2002a);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xfedbff10);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x0150022d);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xff38fc6f);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xff36047b);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x035efbda);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xf9940202);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x08ee01f5);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf649f8fe);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x07e10bc2);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xfcb6f169);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xfcc60e42);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0a04f599);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf0db03d0);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 10800000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffd);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffb000d);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x001dffed);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffaafff5);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x00aa0077);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xff13feb6);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x00ce026b);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x000afc85);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfe3503e3);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x044cfcfb);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xf90c0082);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x08d5037f);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf710f7cc);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x069f0c59);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xfe16f173);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xfbaa0dcf);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0aa5f617);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf0ad039b);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 10900000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffe);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff90006);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x00210003);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffacffc8);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x008e00b6);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xff63fe7c);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x003a0275);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x00dafcda);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfd510313);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x0501fe40);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xf8cbfefd);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x087604f0);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf80af6c2);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x05430cc8);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xff7af19a);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xfa940d4e);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0b3ff699);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf0810365);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 11000000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0001ffff);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff8ffff);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x00210018);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffbaffa3);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x006000e1);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xffc4fe68);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xffa0024b);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x019afd66);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfc990216);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x0575ff99);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xf8d4fd81);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x07d40640);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf932f5e6);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x03d20d0d);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x00dff1de);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf9860cbf);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0bd1f71e);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf058032f);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 11100000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00010000);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff8fff8);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x001b0029);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffd1ff8a);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x002600f2);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x002cfe7c);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xff0f01f0);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x023bfe20);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfc1700fa);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x05a200f7);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xf927fc1c);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x06f40765);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xfa82f53b);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x02510d27);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0243f23d);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf8810c24);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0c5cf7a7);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf03102fa);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 11200000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00010002);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffafff2);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x00110035);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xfff0ff81);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xffe700e7);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x008ffeb6);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe94016d);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x02b0fefb);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfbd3ffd1);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x05850249);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xf9c1fadb);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x05de0858);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xfbf2f4c4);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x00c70d17);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x03a0f2b8);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf7870b7c);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0cdff833);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf00d02c4);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 11300000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000003);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffdffee);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x00040038);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0010ff88);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xffac00c2);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x00e2ff10);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe3900cb);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x02f1ffe9);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfbd3feaa);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x05210381);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfa9cf9c8);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x04990912);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xfd7af484);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xff390cdb);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x04f4f34d);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf69a0ac9);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0d5af8c1);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xefec028e);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 11400000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000003);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0000ffee);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xfff60033);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x002fff9f);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff7b0087);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x011eff82);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe080018);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x02f900d8);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfc17fd96);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x04790490);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfbadf8ed);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x032f098e);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xff10f47d);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xfdaf0c75);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x063cf3fc);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf5ba0a0b);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0dccf952);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xefcd0258);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 11500000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000003);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0004fff1);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffea0026);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0046ffc3);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff5a003c);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x013b0000);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe04ff63);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x02c801b8);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfc99fca6);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x0397056a);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfcecf853);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x01ad09c9);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x00acf4ad);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xfc2e0be7);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0773f4c2);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf4e90943);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0e35f9e6);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xefb10221);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 11600000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000002);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0007fff6);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffe20014);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0054ffee);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff4effeb);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x0137007e);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe2efebb);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x0260027a);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfd51fbe6);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x02870605);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfe4af7fe);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x001d09c1);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x0243f515);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xfabd0b32);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0897f59e);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf4280871);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0e95fa7c);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xef9701eb);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 11700000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xffff0001);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0008fffd);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffdeffff);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0056001d);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff57ff9c);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x011300f0);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe82fe2e);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x01ca0310);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfe35fb62);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x0155065a);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xffbaf7f2);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xfe8c0977);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x03cef5b2);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf9610a58);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x09a5f68f);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf3790797);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0eebfb14);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xef8001b5);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 11800000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xffff0000);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00080004);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffe0ffe9);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x004c0047);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff75ff58);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x00d1014a);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfef9fdc8);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x0111036f);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xff36fb21);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x00120665);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x012df82e);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xfd0708ec);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x0542f682);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf81f095c);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0a9af792);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf2db06b5);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0f38fbad);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xef6c017e);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 11900000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xffffffff);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0007000b);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffe7ffd8);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x00370068);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xffa4ff28);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x00790184);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xff87fd91);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x00430392);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x0044fb26);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfece0626);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x0294f8b2);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xfb990825);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x0698f77f);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf6fe0842);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0b73f8a7);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf25105cd);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0f7bfc48);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xef5a0148);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 12000000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffe);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00050010);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xfff2ffcc);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x001b007b);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xffdfff10);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x00140198);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x0020fd8e);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xff710375);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x014dfb73);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfd9a059f);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x03e0f978);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xfa4e0726);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x07c8f8a7);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf600070c);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0c2ff9c9);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf1db04de);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0fb4fce5);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xef4b0111);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 12100000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffd);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00010012);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffffffc8);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xfffb007e);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x001dff14);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xffad0184);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x00b7fdbe);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfea9031b);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x0241fc01);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfc8504d6);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x0504fa79);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf93005f6);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x08caf9f2);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf52b05c0);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0ccbfaf9);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf17903eb);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0fe3fd83);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xef3f00db);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 12200000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffd);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffe0011);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x000cffcc);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffdb0071);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x0058ff32);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xff4f014a);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x013cfe1f);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfdfb028a);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x0311fcc9);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfb9d03d6);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x05f4fbad);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf848049d);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x0999fb5b);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf4820461);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0d46fc32);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf12d02f4);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x1007fe21);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xef3600a4);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 12300000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffe);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffa000e);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x0017ffd9);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffc10055);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x0088ff68);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xff0400f0);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01a6fea7);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd7501cc);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x03b0fdc0);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfaef02a8);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x06a7fd07);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf79d0326);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x0a31fcda);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf40702f3);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0d9ffd72);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf0f601fa);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x1021fec0);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xef2f006d);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 12400000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0001ffff);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff80007);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x001fffeb);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffaf002d);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x00a8ffb0);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xfed3007e);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01e9ff4c);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd2000ee);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x0413fed8);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfa82015c);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x0715fe7d);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf7340198);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x0a8dfe69);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf3bd017c);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0dd5feb8);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf0d500fd);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x1031ff60);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xef2b0037);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 12500000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00010000);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff70000);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x00220000);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffa90000);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x00b30000);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xfec20000);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x02000000);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd030000);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x04350000);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfa5e0000);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x073b0000);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf7110000);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x0aac0000);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf3a40000);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0de70000);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf0c90000);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x10360000);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xef290000);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 12600000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00010001);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff8fff9);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x001f0015);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffafffd3);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x00a80050);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xfed3ff82);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01e900b4);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd20ff12);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x04130128);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfa82fea4);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x07150183);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf734fe68);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x0a8d0197);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf3bdfe84);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0dd50148);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf0d5ff03);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x103100a0);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xef2bffc9);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 12700000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000002);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffafff2);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x00170027);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffc1ffab);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x00880098);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xff04ff10);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01a60159);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd75fe34);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x03b00240);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfaeffd58);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x06a702f9);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf79dfcda);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x0a310326);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf407fd0d);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0d9f028e);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf0f6fe06);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x10210140);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xef2fff93);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 12800000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000003);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffeffef);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x000c0034);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffdbff8f);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x005800ce);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xff4ffeb6);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x013c01e1);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfdfbfd76);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x03110337);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfb9dfc2a);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x05f40453);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf848fb63);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x099904a5);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf482fb9f);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0d4603ce);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf12dfd0c);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x100701df);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xef36ff5c);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 12900000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000003);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0001ffee);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffff0038);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xfffbff82);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x001d00ec);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xffadfe7c);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x00b70242);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfea9fce5);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x024103ff);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfc85fb2a);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x05040587);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf930fa0a);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x08ca060e);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf52bfa40);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0ccb0507);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf179fc15);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0fe3027d);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xef3fff25);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 13000000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000002);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0005fff0);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xfff20034);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x001bff85);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xffdf00f0);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x0014fe68);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x00200272);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xff71fc8b);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x014d048d);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfd9afa61);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x03e00688);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xfa4ef8da);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x07c80759);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf600f8f4);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0c2f0637);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf1dbfb22);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0fb4031b);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xef4bfeef);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 13100000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xffff0001);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0007fff5);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffe70028);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0037ff98);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xffa400d8);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x0079fe7c);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xff87026f);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x0043fc6e);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x004404da);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfecef9da);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x0294074e);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xfb99f7db);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x06980881);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf6fef7be);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0b730759);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf251fa33);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0f7b03b8);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xef5afeb8);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 13200000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xffff0000);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0008fffc);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffe00017);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x004cffb9);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff7500a8);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x00d1feb6);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfef90238);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x0111fc91);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xff3604df);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x0012f99b);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x012d07d2);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xfd07f714);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x0542097e);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf81ff6a4);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x0a9a086e);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf2dbf94b);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0f380453);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xef6cfe82);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 13300000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xffffffff);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00080003);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffde0001);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0056ffe3);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff570064);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x0113ff10);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe8201d2);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x01cafcf0);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfe35049e);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x0155f9a6);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xffba080e);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xfe8cf689);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x03ce0a4e);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xf961f5a8);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x09a50971);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf379f869);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0eeb04ec);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xef80fe4b);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 13400000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffe);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0007000a);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffe2ffec);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x00540012);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff4e0015);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x0137ff82);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe2e0145);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x0260fd86);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfd51041a);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x0287f9fb);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfe4a0802);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x001df63f);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x02430aeb);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xfabdf4ce);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x08970a62);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf428f78f);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0e950584);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xef97fe15);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 13500000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffd);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0004000f);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffeaffda);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0046003d);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff5affc4);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x013b0000);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe04009d);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x02c8fe48);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfc99035a);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x0397fa96);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfcec07ad);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x01adf637);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x00ac0b53);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xfc2ef419);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x07730b3e);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf4e9f6bd);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0e35061a);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xefb1fddf);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 13600000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffd);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00000012);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xfff6ffcd);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x002f0061);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff7bff79);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x011e007e);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe08ffe8);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x02f9ff28);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfc17026a);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x0479fb70);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfbad0713);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x032ff672);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xff100b83);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xfdaff38b);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x063c0c04);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf5baf5f5);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0dcc06ae);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xefcdfda8);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 13700000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffd);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffd0012);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x0004ffc8);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x00100078);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xffacff3e);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x00e200f0);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe39ff35);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x02f10017);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfbd30156);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x0521fc7f);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfa9c0638);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x0499f6ee);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xfd7a0b7c);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0xff39f325);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x04f40cb3);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf69af537);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0d5a073f);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xefecfd72);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 13800000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0001fffe);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffa000e);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x0011ffcb);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xfff0007f);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xffe7ff19);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x008f014a);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe94fe93);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x02b00105);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfbd3002f);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x0585fdb7);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xf9c10525);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x05def7a8);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xfbf20b3c);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x00c7f2e9);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x03a00d48);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf787f484);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0cdf07cd);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf00dfd3c);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 13900000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00010000);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff80008);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x001bffd7);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffd10076);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x0026ff0e);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x002c0184);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xff0ffe10);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x023b01e0);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfc17ff06);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x05a2ff09);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xf92703e4);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x06f4f89b);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xfa820ac5);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0251f2d9);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x02430dc3);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf881f3dc);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0c5c0859);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf031fd06);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 14000000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00010001);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff80001);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x0021ffe8);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffba005d);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x0060ff1f);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xffc40198);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xffa0fdb5);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x019a029a);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfc99fdea);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x05750067);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xf8d4027f);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x07d4f9c0);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf9320a1a);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x03d2f2f3);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0x00df0e22);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xf986f341);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0bd108e2);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf058fcd1);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 14100000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000002);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff9fffa);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x0021fffd);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffac0038);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x008eff4a);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xff630184);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x003afd8b);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x00da0326);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfd51fced);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x050101c0);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xf8cb0103);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x0876fb10);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf80a093e);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0543f338);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xff7a0e66);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xfa94f2b2);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0b3f0967);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf081fc9b);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 14200000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000003);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffbfff3);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x001d0013);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffaa000b);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x00aaff89);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xff13014a);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x00cefd95);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x000a037b);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfe35fc1d);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x044c0305);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xf90cff7e);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x08d5fc81);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf7100834);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x069ff3a7);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xfe160e8d);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xfbaaf231);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0aa509e9);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf0adfc65);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 14300000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000003);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xffffffef);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x00140025);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffb4ffdd);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x00b2ffd6);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xfedb00f0);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x0150fdd3);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xff380391);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xff36fb85);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x035e0426);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xf994fdfe);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x08eefe0b);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf6490702);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x07e1f43e);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xfcb60e97);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xfcc6f1be);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x0a040a67);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf0dbfc30);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 14400000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000003);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0002ffee);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x00070033);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffc9ffb4);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x00a40027);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xfec3007e);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01b4fe3f);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfe760369);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x0044fb2e);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x02450518);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfa5ffc90);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x08c1ffa1);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf5bc05ae);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0902f4fc);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xfb600e85);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xfde7f15a);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x095d0ae2);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf10cfbfb);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 14500000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xffff0002);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0005ffef);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xfffa0038);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffe5ff95);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x00820074);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xfecc0000);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01f0fed0);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfdd20304);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x014dfb1d);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x010e05ce);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfb64fb41);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x084e013b);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf569043e);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0a00f5dd);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xfa150e55);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0xff0bf104);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x08b00b59);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf13ffbc6);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 14600000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xffff0001);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0008fff4);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffed0035);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0005ff83);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x005000b4);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xfef6ff82);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01ffff7a);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd580269);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x0241fb53);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xffca0640);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfc99fa1e);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x079a02cb);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf55502ba);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0ad5f6e0);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf8d90e0a);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0031f0bd);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x07fd0bcb);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf174fb91);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 14700000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xffffffff);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0009fffb);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffe4002a);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0025ff82);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x001400e0);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xff3cff10);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01e10030);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd1201a4);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x0311fbcd);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfe88066a);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xfdf1f92f);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x06aa0449);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf57e0128);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0b7ef801);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf7b00da2);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0156f086);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x07450c39);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf1acfb5c);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 14800000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffe);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00080002);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffdf0019);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x003fff92);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xffd600f1);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xff96feb6);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x019700e1);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd0500c2);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x03b0fc84);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfd590649);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0xff5df87f);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x058505aa);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf5e4ff91);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0bf9f93c);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf69d0d20);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0279f05e);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x06880ca3);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf1e6fb28);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 14900000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffd);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x00060009);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffdf0004);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0051ffb0);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff9d00e8);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xfffcfe7c);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x01280180);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd32ffd2);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x0413fd6e);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfc4d05df);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x00d1f812);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x043506e4);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf685fdfb);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0c43fa8d);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf5a10c83);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0399f046);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x05c70d08);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf222faf3);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 15000000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffd);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0003000f);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffe5ffef);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x0057ffd9);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff7000c4);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x0062fe68);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x009e01ff);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfd95fee6);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x0435fe7d);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfb710530);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x023cf7ee);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x02c307ef);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf75efc70);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0c5cfbef);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf4c10bce);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x04b3f03f);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x05030d69);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf261fabf);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 15100000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0000fffd);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xffff0012);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xffefffdc);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x00510006);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff540089);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x00befe7c);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0x00060253);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfe27fe0d);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x0413ffa2);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfad10446);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x0390f812);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0x013b08c3);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf868faf6);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0c43fd5f);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf3fd0b02);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x05c7f046);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x043b0dc4);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf2a1fa8b);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 15200000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0001fffe);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffc0012);
		cx25840_write4(client, DIF_BPF_COEFF45, 0xfffbffce);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x003f0033);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff4e003f);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x0106feb6);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xff6e0276);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xfeddfd56);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x03b000cc);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfa740329);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x04bff87f);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xffaa095d);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xf99ef995);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0bf9fed8);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf3590a1f);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x06d2f05e);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x03700e1b);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf2e4fa58);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 15300000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x0001ffff);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff9000f);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x0009ffc8);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x00250059);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff5effee);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x0132ff10);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfee30265);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0xffaafccf);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x031101eb);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfa6001e8);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x05bdf92f);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xfe1b09b6);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xfafaf852);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0b7e0055);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf2d50929);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x07d3f086);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x02a30e6c);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf329fa24);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 15400000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00010001);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff80009);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x0015ffca);
		cx25840_write4(client, DIF_BPF_COEFF67, 0x00050074);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xff81ff9f);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x013dff82);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe710221);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x007cfc80);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x024102ed);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfa940090);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x0680fa1e);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xfc9b09cd);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xfc73f736);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0ad501d0);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf2740820);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x08c9f0bd);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x01d40eb9);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf371f9f1);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 15500000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000002);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff80002);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x001effd5);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffe5007f);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xffb4ff5b);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x01280000);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe2401b0);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x0146fc70);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x014d03c6);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfb10ff32);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x0701fb41);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xfb3709a1);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xfe00f644);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x0a000345);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf2350708);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x09b2f104);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x01050eff);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf3baf9be);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 15600000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000003);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfff9fffb);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x0022ffe6);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffc9007a);
		cx25840_write4(client, DIF_BPF_COEFF89, 0xfff0ff29);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x00f2007e);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe01011b);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x01f6fc9e);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0x00440467);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfbccfdde);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x0738fc90);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf9f70934);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0xff99f582);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x090204b0);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf21a05e1);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0a8df15a);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0x00340f41);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf405f98b);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 15700000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000003);
		cx25840_write4(client, DIF_BPF_COEFF23, 0xfffcfff4);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x0020fffa);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffb40064);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x002fff11);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x00a400f0);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe0d006e);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x0281fd09);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xff3604c9);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfcbffca2);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x0726fdfe);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf8e80888);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x0134f4f3);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x07e1060c);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf22304af);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0b59f1be);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xff640f7d);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf452f959);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 15800000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0x00000003);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0000fff0);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x001a0010);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffaa0041);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x0067ff13);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0x0043014a);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfe46ffb9);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x02dbfda8);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfe3504e5);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xfddcfb8d);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x06c9ff7e);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf81107a2);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x02c9f49a);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x069f0753);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf2500373);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0c14f231);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xfe930fb3);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf4a1f927);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 15900000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xffff0002);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0003ffee);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x000f0023);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffac0016);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x0093ff31);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xffdc0184);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xfea6ff09);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x02fdfe70);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfd5104ba);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0xff15faac);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x06270103);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf7780688);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x044df479);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x05430883);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf2a00231);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0cbef2b2);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xfdc40fe3);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf4f2f8f5);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;

	case 16000000:
		cx25840_write4(client, DIF_BPF_COEFF01, 0xffff0001);
		cx25840_write4(client, DIF_BPF_COEFF23, 0x0006ffef);
		cx25840_write4(client, DIF_BPF_COEFF45, 0x00020031);
		cx25840_write4(client, DIF_BPF_COEFF67, 0xffbaffe8);
		cx25840_write4(client, DIF_BPF_COEFF89, 0x00adff66);
		cx25840_write4(client, DIF_BPF_COEFF1011, 0xff790198);
		cx25840_write4(client, DIF_BPF_COEFF1213, 0xff26fe6e);
		cx25840_write4(client, DIF_BPF_COEFF1415, 0x02e5ff55);
		cx25840_write4(client, DIF_BPF_COEFF1617, 0xfc99044a);
		cx25840_write4(client, DIF_BPF_COEFF1819, 0x005bfa09);
		cx25840_write4(client, DIF_BPF_COEFF2021, 0x0545027f);
		cx25840_write4(client, DIF_BPF_COEFF2223, 0xf7230541);
		cx25840_write4(client, DIF_BPF_COEFF2425, 0x05b8f490);
		cx25840_write4(client, DIF_BPF_COEFF2627, 0x03d20997);
		cx25840_write4(client, DIF_BPF_COEFF2829, 0xf31300eb);
		cx25840_write4(client, DIF_BPF_COEFF3031, 0x0d55f341);
		cx25840_write4(client, DIF_BPF_COEFF3233, 0xfcf6100e);
		cx25840_write4(client, DIF_BPF_COEFF3435, 0xf544f8c3);
		cx25840_write4(client, DIF_BPF_COEFF36, 0x110d0000);
		break;
	}
}

static void cx23888_std_setup(struct i2c_client *client)
{
	struct cx25840_state *state = to_state(i2c_get_clientdata(client));
	v4l2_std_id std = state->std;
	u32 ifHz;

	cx25840_write4(client, 0x478, 0x6628021F);
	cx25840_write4(client, 0x400, 0x0);
	cx25840_write4(client, 0x4b4, 0x20524030);
	cx25840_write4(client, 0x47c, 0x010a8263);

	if (std & V4L2_STD_525_60) {
		v4l_dbg(1, cx25840_debug, client, "%s() Selecting NTSC",
			__func__);

		/* Horiz / vert timing */
		cx25840_write4(client, 0x428, 0x1e1e601a);
		cx25840_write4(client, 0x424, 0x5b2d007a);

		/* DIF NTSC */
		cx25840_write4(client, 0x304, 0x6503bc0c);
		cx25840_write4(client, 0x308, 0xbd038c85);
		cx25840_write4(client, 0x30c, 0x1db4640a);
		cx25840_write4(client, 0x310, 0x00008800);
		cx25840_write4(client, 0x314, 0x44400400);
		cx25840_write4(client, 0x32c, 0x0c800800);
		cx25840_write4(client, 0x330, 0x27000100);
		cx25840_write4(client, 0x334, 0x1f296e1f);
		cx25840_write4(client, 0x338, 0x009f50c1);
		cx25840_write4(client, 0x340, 0x1befbf06);
		cx25840_write4(client, 0x344, 0x000035e8);

		/* DIF I/F */
		ifHz = 5400000;

	} else {
		v4l_dbg(1, cx25840_debug, client, "%s() Selecting PAL-BG",
			__func__);

		/* Horiz / vert timing */
		cx25840_write4(client, 0x428, 0x28244024);
		cx25840_write4(client, 0x424, 0x5d2d0084);

		/* DIF */
		cx25840_write4(client, 0x304, 0x6503bc0c);
		cx25840_write4(client, 0x308, 0xbd038c85);
		cx25840_write4(client, 0x30c, 0x1db4640a);
		cx25840_write4(client, 0x310, 0x00008800);
		cx25840_write4(client, 0x314, 0x44400600);
		cx25840_write4(client, 0x32c, 0x0c800800);
		cx25840_write4(client, 0x330, 0x27000100);
		cx25840_write4(client, 0x334, 0x213530ec);
		cx25840_write4(client, 0x338, 0x00a65ba8);
		cx25840_write4(client, 0x340, 0x1befbf06);
		cx25840_write4(client, 0x344, 0x000035e8);

		/* DIF I/F */
		ifHz = 6000000;
	}

	cx23885_dif_setup(client, ifHz);

	/* Explicitly ensure the inputs are reconfigured after
	 * a standard change.
	 */
	set_input(client, state->vid_input, state->aud_input);
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_ctrl_ops cx25840_ctrl_ops = {
	.s_ctrl = cx25840_s_ctrl,
};

static const struct v4l2_subdev_core_ops cx25840_core_ops = {
	.log_status = cx25840_log_status,
	.reset = cx25840_reset,
	/* calling the (optional) init op will turn on the generic mode */
	.init = cx25840_init,
	.load_fw = cx25840_load_fw,
	.s_io_pin_config = common_s_io_pin_config,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = cx25840_g_register,
	.s_register = cx25840_s_register,
#endif
	.interrupt_service_routine = cx25840_irq_handler,
};

static const struct v4l2_subdev_tuner_ops cx25840_tuner_ops = {
	.s_frequency = cx25840_s_frequency,
	.s_radio = cx25840_s_radio,
	.g_tuner = cx25840_g_tuner,
	.s_tuner = cx25840_s_tuner,
};

static const struct v4l2_subdev_audio_ops cx25840_audio_ops = {
	.s_clock_freq = cx25840_s_clock_freq,
	.s_routing = cx25840_s_audio_routing,
	.s_stream = cx25840_s_audio_stream,
};

static const struct v4l2_subdev_video_ops cx25840_video_ops = {
	.g_std = cx25840_g_std,
	.s_std = cx25840_s_std,
	.querystd = cx25840_querystd,
	.s_routing = cx25840_s_video_routing,
	.s_stream = cx25840_s_stream,
	.g_input_status = cx25840_g_input_status,
};

static const struct v4l2_subdev_vbi_ops cx25840_vbi_ops = {
	.decode_vbi_line = cx25840_decode_vbi_line,
	.s_raw_fmt = cx25840_s_raw_fmt,
	.s_sliced_fmt = cx25840_s_sliced_fmt,
	.g_sliced_fmt = cx25840_g_sliced_fmt,
};

static const struct v4l2_subdev_pad_ops cx25840_pad_ops = {
	.set_fmt = cx25840_set_fmt,
};

static const struct v4l2_subdev_ops cx25840_ops = {
	.core = &cx25840_core_ops,
	.tuner = &cx25840_tuner_ops,
	.audio = &cx25840_audio_ops,
	.video = &cx25840_video_ops,
	.vbi = &cx25840_vbi_ops,
	.pad = &cx25840_pad_ops,
	.ir = &cx25840_ir_ops,
};

/* ----------------------------------------------------------------------- */

static u32 get_cx2388x_ident(struct i2c_client *client)
{
	u32 ret;

	/* Come out of digital power down */
	cx25840_write(client, 0x000, 0);

	/*
	 * Detecting whether the part is cx23885/7/8 is more
	 * difficult than it needs to be. No ID register. Instead we
	 * probe certain registers indicated in the datasheets to look
	 * for specific defaults that differ between the silicon designs.
	 */

	/* It's either 885/7 if the IR Tx Clk Divider register exists */
	if (cx25840_read4(client, 0x204) & 0xffff) {
		/*
		 * CX23885 returns bogus repetitive byte values for the DIF,
		 * which doesn't exist for it. (Ex. 8a8a8a8a or 31313131)
		 */
		ret = cx25840_read4(client, 0x300);
		if (((ret & 0xffff0000) >> 16) == (ret & 0xffff)) {
			/* No DIF */
			ret = CX23885_AV;
		} else {
			/*
			 * CX23887 has a broken DIF, but the registers
			 * appear valid (but unused), good enough to detect.
			 */
			ret = CX23887_AV;
		}
	} else if (cx25840_read4(client, 0x300) & 0x0fffffff) {
		/* DIF PLL Freq Word reg exists; chip must be a CX23888 */
		ret = CX23888_AV;
	} else {
		v4l_err(client, "Unable to detect h/w, assuming cx23887\n");
		ret = CX23887_AV;
	}

	/* Back into digital power down */
	cx25840_write(client, 0x000, 2);
	return ret;
}

static int cx25840_probe(struct i2c_client *client,
			 const struct i2c_device_id *did)
{
	struct cx25840_state *state;
	struct v4l2_subdev *sd;
	int default_volume;
	u32 id;
	u16 device_id;
#if defined(CONFIG_MEDIA_CONTROLLER)
	int ret;
#endif

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	v4l_dbg(1, cx25840_debug, client,
		"detecting cx25840 client on address 0x%x\n",
		client->addr << 1);

	device_id = cx25840_read(client, 0x101) << 8;
	device_id |= cx25840_read(client, 0x100);
	v4l_dbg(1, cx25840_debug, client, "device_id = 0x%04x\n", device_id);

	/*
	 * The high byte of the device ID should be
	 * 0x83 for the cx2583x and 0x84 for the cx2584x
	 */
	if ((device_id & 0xff00) == 0x8300) {
		id = CX25836 + ((device_id >> 4) & 0xf) - 6;
	} else if ((device_id & 0xff00) == 0x8400) {
		id = CX25840 + ((device_id >> 4) & 0xf);
	} else if (device_id == 0x0000) {
		id = get_cx2388x_ident(client);
	} else if ((device_id & 0xfff0) == 0x5A30) {
		/* The CX23100 (0x5A3C = 23100) doesn't have an A/V decoder */
		id = CX2310X_AV;
	} else if ((device_id & 0xff) == (device_id >> 8)) {
		v4l_err(client,
			"likely a confused/unresponsive cx2388[578] A/V decoder found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);
		v4l_err(client,
			"A method to reset it from the cx25840 driver software is not known at this time\n");
		return -ENODEV;
	} else {
		v4l_dbg(1, cx25840_debug, client, "cx25840 not found\n");
		return -ENODEV;
	}

	state = devm_kzalloc(&client->dev, sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	sd = &state->sd;
	v4l2_i2c_subdev_init(sd, client, &cx25840_ops);
#if defined(CONFIG_MEDIA_CONTROLLER)
	/*
	 * TODO: add media controller support for analog video inputs like
	 * composite, svideo, etc.
	 * A real input pad for this analog demod would be like:
	 *                 ___________
	 * TUNER --------> |         |
	 *		   |         |
	 * SVIDEO .......> | cx25840 |
	 *		   |         |
	 * COMPOSITE1 ...> |_________|
	 *
	 * However, at least for now, there's no much gain on modelling
	 * those extra inputs. So, let's add it only when needed.
	 */
	state->pads[CX25840_PAD_INPUT].flags = MEDIA_PAD_FL_SINK;
	state->pads[CX25840_PAD_INPUT].sig_type = PAD_SIGNAL_ANALOG;
	state->pads[CX25840_PAD_VID_OUT].flags = MEDIA_PAD_FL_SOURCE;
	state->pads[CX25840_PAD_VID_OUT].sig_type = PAD_SIGNAL_DV;
	sd->entity.function = MEDIA_ENT_F_ATV_DECODER;

	ret = media_entity_pads_init(&sd->entity, ARRAY_SIZE(state->pads),
				     state->pads);
	if (ret < 0) {
		v4l_info(client, "failed to initialize media entity!\n");
		return ret;
	}
#endif

	switch (id) {
	case CX23885_AV:
		v4l_info(client, "cx23885 A/V decoder found @ 0x%x (%s)\n",
			 client->addr << 1, client->adapter->name);
		break;
	case CX23887_AV:
		v4l_info(client, "cx23887 A/V decoder found @ 0x%x (%s)\n",
			 client->addr << 1, client->adapter->name);
		break;
	case CX23888_AV:
		v4l_info(client, "cx23888 A/V decoder found @ 0x%x (%s)\n",
			 client->addr << 1, client->adapter->name);
		break;
	case CX2310X_AV:
		v4l_info(client, "cx%d A/V decoder found @ 0x%x (%s)\n",
			 device_id, client->addr << 1, client->adapter->name);
		break;
	case CX25840:
	case CX25841:
	case CX25842:
	case CX25843:
		/*
		 * Note: revision '(device_id & 0x0f) == 2' was never built.
		 * The marking skips from 0x1 == 22 to 0x3 == 23.
		 */
		v4l_info(client, "cx25%3x-2%x found @ 0x%x (%s)\n",
			 (device_id & 0xfff0) >> 4,
			 (device_id & 0x0f) < 3 ? (device_id & 0x0f) + 1
						: (device_id & 0x0f),
			 client->addr << 1, client->adapter->name);
		break;
	case CX25836:
	case CX25837:
	default:
		v4l_info(client, "cx25%3x-%x found @ 0x%x (%s)\n",
			 (device_id & 0xfff0) >> 4, device_id & 0x0f,
			 client->addr << 1, client->adapter->name);
		break;
	}

	state->c = client;
	state->vid_input = CX25840_COMPOSITE7;
	state->aud_input = CX25840_AUDIO8;
	state->audclk_freq = 48000;
	state->audmode = V4L2_TUNER_MODE_LANG1;
	state->vbi_line_offset = 8;
	state->id = id;
	state->rev = device_id;
	state->vbi_regs_offset = id == CX23888_AV ? 0x500 - 0x424 : 0;
	state->std = V4L2_STD_NTSC_M;
	v4l2_ctrl_handler_init(&state->hdl, 9);
	v4l2_ctrl_new_std(&state->hdl, &cx25840_ctrl_ops,
			  V4L2_CID_BRIGHTNESS, 0, 255, 1, 128);
	v4l2_ctrl_new_std(&state->hdl, &cx25840_ctrl_ops,
			  V4L2_CID_CONTRAST, 0, 127, 1, 64);
	v4l2_ctrl_new_std(&state->hdl, &cx25840_ctrl_ops,
			  V4L2_CID_SATURATION, 0, 127, 1, 64);
	v4l2_ctrl_new_std(&state->hdl, &cx25840_ctrl_ops,
			  V4L2_CID_HUE, -128, 127, 1, 0);
	if (!is_cx2583x(state)) {
		default_volume = cx25840_read(client, 0x8d4);
		/*
		 * Enforce the legacy PVR-350/MSP3400 to PVR-150/CX25843 volume
		 * scale mapping limits to avoid -ERANGE errors when
		 * initializing the volume control
		 */
		if (default_volume > 228) {
			/* Bottom out at -96 dB, v4l2 vol range 0x2e00-0x2fff */
			default_volume = 228;
			cx25840_write(client, 0x8d4, 228);
		} else if (default_volume < 20) {
			/* Top out at + 8 dB, v4l2 vol range 0xfe00-0xffff */
			default_volume = 20;
			cx25840_write(client, 0x8d4, 20);
		}
		default_volume = (((228 - default_volume) >> 1) + 23) << 9;

		state->volume = v4l2_ctrl_new_std(&state->hdl,
						  &cx25840_audio_ctrl_ops,
						  V4L2_CID_AUDIO_VOLUME,
						  0, 65535, 65535 / 100,
						  default_volume);
		state->mute = v4l2_ctrl_new_std(&state->hdl,
						&cx25840_audio_ctrl_ops,
						V4L2_CID_AUDIO_MUTE,
						0, 1, 1, 0);
		v4l2_ctrl_new_std(&state->hdl, &cx25840_audio_ctrl_ops,
				  V4L2_CID_AUDIO_BALANCE,
				  0, 65535, 65535 / 100, 32768);
		v4l2_ctrl_new_std(&state->hdl, &cx25840_audio_ctrl_ops,
				  V4L2_CID_AUDIO_BASS,
				  0, 65535, 65535 / 100, 32768);
		v4l2_ctrl_new_std(&state->hdl, &cx25840_audio_ctrl_ops,
				  V4L2_CID_AUDIO_TREBLE,
				  0, 65535, 65535 / 100, 32768);
	}
	sd->ctrl_handler = &state->hdl;
	if (state->hdl.error) {
		int err = state->hdl.error;

		v4l2_ctrl_handler_free(&state->hdl);
		return err;
	}
	if (!is_cx2583x(state))
		v4l2_ctrl_cluster(2, &state->volume);
	v4l2_ctrl_handler_setup(&state->hdl);

	if (client->dev.platform_data) {
		struct cx25840_platform_data *pdata = client->dev.platform_data;

		state->pvr150_workaround = pdata->pvr150_workaround;
	}

	cx25840_ir_probe(sd);
	return 0;
}

static int cx25840_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct cx25840_state *state = to_state(sd);

	cx25840_ir_remove(sd);
	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&state->hdl);
	return 0;
}

static const struct i2c_device_id cx25840_id[] = {
	{ "cx25840", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cx25840_id);

static struct i2c_driver cx25840_driver = {
	.driver = {
		.name	= "cx25840",
	},
	.probe		= cx25840_probe,
	.remove		= cx25840_remove,
	.id_table	= cx25840_id,
};

module_i2c_driver(cx25840_driver);
