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

static const u32 ifhz_coeffs[][19] = {
	{	// 3.0 MHz
		0x00000002, 0x00080012, 0x001e0024, 0x001bfff8,
		0xffb4ff50, 0xfed8fe68, 0xfe24fe34, 0xfebaffc7,
		0x014d031f, 0x04f0065d, 0x07010688, 0x04c901d6,
		0xfe00f9d3, 0xf600f342, 0xf235f337, 0xf64efb22,
		0x0105070f, 0x0c460fce, 0x110d0000,
	}, {	// 3.1 MHz
		0x00000001, 0x00070012, 0x00220032, 0x00370026,
		0xfff0ff91, 0xff0efe7c, 0xfe01fdcc, 0xfe0afedb,
		0x00440224, 0x0434060c, 0x0738074e, 0x06090361,
		0xff99fb39, 0xf6fef3b6, 0xf21af2a5, 0xf573fa33,
		0x0034067d, 0x0bfb0fb9, 0x110d0000,
	}, {	// 3.2 MHz
		0x00000000, 0x0004000e, 0x00200038, 0x004c004f,
		0x002fffdf, 0xff5cfeb6, 0xfe0dfd92, 0xfd7ffe03,
		0xff36010a, 0x03410575, 0x072607d2, 0x071804d5,
		0x0134fcb7, 0xf81ff451, 0xf223f22e, 0xf4a7f94b,
		0xff6405e8, 0x0bae0fa4, 0x110d0000,
	}, {	// 3.3 MHz
		0x0000ffff, 0x00000008, 0x001a0036, 0x0056006d,
		0x00670030, 0xffbdff10, 0xfe46fd8d, 0xfd25fd4f,
		0xfe35ffe0, 0x0224049f, 0x06c9080e, 0x07ef0627,
		0x02c9fe45, 0xf961f513, 0xf250f1d2, 0xf3ecf869,
		0xfe930552, 0x0b5f0f8f, 0x110d0000,
	}, {	// 3.4 MHz
		0xfffffffe, 0xfffd0001, 0x000f002c, 0x0054007d,
		0x0093007c, 0x0024ff82, 0xfea6fdbb, 0xfd03fcca,
		0xfd51feb9, 0x00eb0392, 0x06270802, 0x08880750,
		0x044dffdb, 0xfabdf5f8, 0xf2a0f193, 0xf342f78f,
		0xfdc404b9, 0x0b0e0f78, 0x110d0000,
	}, {	// 3.5 MHz
		0xfffffffd, 0xfffafff9, 0x0002001b, 0x0046007d,
		0x00ad00ba, 0x00870000, 0xff26fe1a, 0xfd1bfc7e,
		0xfc99fda4, 0xffa5025c, 0x054507ad, 0x08dd0847,
		0x05b80172, 0xfc2ef6ff, 0xf313f170, 0xf2abf6bd,
		0xfcf6041f, 0x0abc0f61, 0x110d0000,
	}, {	// 3.6 MHz
		0xfffffffd, 0xfff8fff3, 0xfff50006, 0x002f006c,
		0x00b200e3, 0x00dc007e, 0xffb9fea0, 0xfd6bfc71,
		0xfc17fcb1, 0xfe65010b, 0x042d0713, 0x08ec0906,
		0x07020302, 0xfdaff823, 0xf3a7f16a, 0xf228f5f5,
		0xfc2a0384, 0x0a670f4a, 0x110d0000,
	}, {	// 3.7 MHz
		0x0000fffd, 0xfff7ffef, 0xffe9fff1, 0x0010004d,
		0x00a100f2, 0x011a00f0, 0x0053ff44, 0xfdedfca2,
		0xfbd3fbef, 0xfd39ffae, 0x02ea0638, 0x08b50987,
		0x08230483, 0xff39f960, 0xf45bf180, 0xf1b8f537,
		0xfb6102e7, 0x0a110f32, 0x110d0000,
	}, {	// 3.8 MHz
		0x0000fffe, 0xfff9ffee, 0xffe1ffdd, 0xfff00024,
		0x007c00e5, 0x013a014a, 0x00e6fff8, 0xfe98fd0f,
		0xfbd3fb67, 0xfc32fe54, 0x01880525, 0x083909c7,
		0x091505ee, 0x00c7fab3, 0xf52df1b4, 0xf15df484,
		0xfa9b0249, 0x09ba0f19, 0x110d0000,
	}, {	// 3.9 MHz
		0x00000000, 0xfffbfff0, 0xffdeffcf, 0xffd1fff6,
		0x004800be, 0x01390184, 0x016300ac, 0xff5efdb1,
		0xfc17fb23, 0xfb5cfd0d, 0x001703e4, 0x077b09c4,
		0x09d2073c, 0x0251fc18, 0xf61cf203, 0xf118f3dc,
		0xf9d801aa, 0x09600eff, 0x110d0000,
	}, {	// 4.0 MHz
		0x00000001, 0xfffefff4, 0xffe1ffc8, 0xffbaffca,
		0x000b0082, 0x01170198, 0x01c10152, 0x0030fe7b,
		0xfc99fb24, 0xfac3fbe9, 0xfea5027f, 0x0683097f,
		0x0a560867, 0x03d2fd89, 0xf723f26f, 0xf0e8f341,
		0xf919010a, 0x09060ee5, 0x110d0000,
	}, {	// 4.1 MHz
		0x00010002, 0x0002fffb, 0xffe8ffca, 0xffacffa4,
		0xffcd0036, 0x00d70184, 0x01f601dc, 0x00ffff60,
		0xfd51fb6d, 0xfa6efaf5, 0xfd410103, 0x055708f9,
		0x0a9e0969, 0x0543ff02, 0xf842f2f5, 0xf0cef2b2,
		0xf85e006b, 0x08aa0ecb, 0x110d0000,
	}, {	// 4.2 MHz
		0x00010003, 0x00050003, 0xfff3ffd3, 0xffaaff8b,
		0xff95ffe5, 0x0080014a, 0x01fe023f, 0x01ba0050,
		0xfe35fbf8, 0xfa62fa3b, 0xfbf9ff7e, 0x04010836,
		0x0aa90a3d, 0x069f007f, 0xf975f395, 0xf0cbf231,
		0xf7a9ffcb, 0x084c0eaf, 0x110d0000,
	}, {	// 4.3 MHz
		0x00010003, 0x0008000a, 0x0000ffe4, 0xffb4ff81,
		0xff6aff96, 0x001c00f0, 0x01d70271, 0x0254013b,
		0xff36fcbd, 0xfa9ff9c5, 0xfadbfdfe, 0x028c073b,
		0x0a750adf, 0x07e101fa, 0xfab8f44e, 0xf0ddf1be,
		0xf6f9ff2b, 0x07ed0e94, 0x110d0000,
	}, {	// 4.4 MHz
		0x00000003, 0x0009000f, 0x000efff8, 0xffc9ff87,
		0xff52ff54, 0xffb5007e, 0x01860270, 0x02c00210,
		0x0044fdb2, 0xfb22f997, 0xf9f2fc90, 0x0102060f,
		0x0a050b4c, 0x0902036e, 0xfc0af51e, 0xf106f15a,
		0xf64efe8b, 0x078d0e77, 0x110d0000,
	}, {	// 4.5 MHz
		0x00000002, 0x00080012, 0x0019000e, 0xffe5ff9e,
		0xff4fff25, 0xff560000, 0x0112023b, 0x02f702c0,
		0x014dfec8, 0xfbe5f9b3, 0xf947fb41, 0xff7004b9,
		0x095a0b81, 0x0a0004d8, 0xfd65f603, 0xf144f104,
		0xf5aafdec, 0x072b0e5a, 0x110d0000,
	}, {	// 4.6 MHz
		0x00000001, 0x00060012, 0x00200022, 0x0005ffc1,
		0xff61ff10, 0xff09ff82, 0x008601d7, 0x02f50340,
		0x0241fff0, 0xfcddfa19, 0xf8e2fa1e, 0xfde30343,
		0x08790b7f, 0x0ad50631, 0xfec7f6fc, 0xf198f0bd,
		0xf50dfd4e, 0x06c90e3d, 0x110d0000,
	}, {	// 4.7 MHz
		0x0000ffff, 0x0003000f, 0x00220030, 0x0025ffed,
		0xff87ff15, 0xfed6ff10, 0xffed014c, 0x02b90386,
		0x03110119, 0xfdfefac4, 0xf8c6f92f, 0xfc6701b7,
		0x07670b44, 0x0b7e0776, 0x002df807, 0xf200f086,
		0xf477fcb1, 0x06650e1e, 0x110d0000,
	}, {	// 4.8 MHz
		0xfffffffe, 0xffff0009, 0x001e0038, 0x003f001b,
		0xffbcff36, 0xfec2feb6, 0xff5600a5, 0x0248038d,
		0x03b00232, 0xff39fbab, 0xf8f4f87f, 0xfb060020,
		0x062a0ad2, 0x0bf908a3, 0x0192f922, 0xf27df05e,
		0xf3e8fc14, 0x06000e00, 0x110d0000,
	}, {	// 4.9 MHz
		0xfffffffd, 0xfffc0002, 0x00160037, 0x00510046,
		0xfff9ff6d, 0xfed0fe7c, 0xfecefff0, 0x01aa0356,
		0x0413032b, 0x007ffcc5, 0xf96cf812, 0xf9cefe87,
		0x04c90a2c, 0x0c4309b4, 0x02f3fa4a, 0xf30ef046,
		0xf361fb7a, 0x059b0de0, 0x110d0000,
	}, {	// 5.0 MHz
		0xfffffffd, 0xfff9fffa, 0x000a002d, 0x00570067,
		0x0037ffb5, 0xfefffe68, 0xfe62ff3d, 0x00ec02e3,
		0x043503f6, 0x01befe05, 0xfa27f7ee, 0xf8c6fcf8,
		0x034c0954, 0x0c5c0aa4, 0x044cfb7e, 0xf3b1f03f,
		0xf2e2fae1, 0x05340dc0, 0x110d0000,
	}, {	// 5.1 MHz
		0x0000fffd, 0xfff8fff4, 0xfffd001e, 0x0051007b,
		0x006e0006, 0xff48fe7c, 0xfe1bfe9a, 0x001d023e,
		0x04130488, 0x02e6ff5b, 0xfb1ef812, 0xf7f7fb7f,
		0x01bc084e, 0x0c430b72, 0x059afcba, 0xf467f046,
		0xf26cfa4a, 0x04cd0da0, 0x110d0000,
	}, {	// 5.2 MHz
		0x0000fffe, 0xfff8ffef, 0xfff00009, 0x003f007f,
		0x00980056, 0xffa5feb6, 0xfe00fe15, 0xff4b0170,
		0x03b004d7, 0x03e800b9, 0xfc48f87f, 0xf768fa23,
		0x0022071f, 0x0bf90c1b, 0x06dafdfd, 0xf52df05e,
		0xf1fef9b5, 0x04640d7f, 0x110d0000,
	}, {	// 5.3 MHz
		0x0000ffff, 0xfff9ffee, 0xffe6fff3, 0x00250072,
		0x00af009c, 0x000cff10, 0xfe13fdb8, 0xfe870089,
		0x031104e1, 0x04b8020f, 0xfd98f92f, 0xf71df8f0,
		0xfe8805ce, 0x0b7e0c9c, 0x0808ff44, 0xf603f086,
		0xf19af922, 0x03fb0d5e, 0x110d0000,
	}, {	// 5.4 MHz
		0x00000001, 0xfffcffef, 0xffe0ffe0, 0x00050056,
		0x00b000d1, 0x0071ff82, 0xfe53fd8c, 0xfddfff99,
		0x024104a3, 0x054a034d, 0xff01fa1e, 0xf717f7ed,
		0xfcf50461, 0x0ad50cf4, 0x0921008d, 0xf6e7f0bd,
		0xf13ff891, 0x03920d3b, 0x110d0000,
	}, {	// 5.5 MHz
		0x00010002, 0xfffffff3, 0xffdeffd1, 0xffe5002f,
		0x009c00ed, 0x00cb0000, 0xfebafd94, 0xfd61feb0,
		0x014d0422, 0x05970464, 0x0074fb41, 0xf759f721,
		0xfb7502de, 0x0a000d21, 0x0a2201d4, 0xf7d9f104,
		0xf0edf804, 0x03280d19, 0x110d0000,
	}, {	// 5.6 MHz
		0x00010003, 0x0003fffa, 0xffe3ffc9, 0xffc90002,
		0x007500ef, 0x010e007e, 0xff3dfdcf, 0xfd16fddd,
		0x00440365, 0x059b0548, 0x01e3fc90, 0xf7dff691,
		0xfa0f014d, 0x09020d23, 0x0b0a0318, 0xf8d7f15a,
		0xf0a5f779, 0x02bd0cf6, 0x110d0000,
	}, {	// 5.7 MHz
		0x00010003, 0x00060001, 0xffecffc9, 0xffb4ffd4,
		0x004000d5, 0x013600f0, 0xffd3fe39, 0xfd04fd31,
		0xff360277, 0x055605ef, 0x033efdfe, 0xf8a5f642,
		0xf8cbffb6, 0x07e10cfb, 0x0bd50456, 0xf9dff1be,
		0xf067f6f2, 0x02520cd2, 0x110d0000,
	}, {	// 5.8 MHz
		0x00000003, 0x00080009, 0xfff8ffd2, 0xffaaffac,
		0x000200a3, 0x013c014a, 0x006dfec9, 0xfd2bfcb7,
		0xfe350165, 0x04cb0651, 0x0477ff7e, 0xf9a5f635,
		0xf7b1fe20, 0x069f0ca8, 0x0c81058b, 0xfaf0f231,
		0xf033f66d, 0x01e60cae, 0x110d0000,
	}, {	// 5.9 MHz
		0x00000002, 0x0009000e, 0x0005ffe1, 0xffacff90,
		0xffc5005f, 0x01210184, 0x00fcff72, 0xfd8afc77,
		0xfd51003f, 0x04020669, 0x05830103, 0xfad7f66b,
		0xf6c8fc93, 0x05430c2b, 0x0d0d06b5, 0xfc08f2b2,
		0xf00af5ec, 0x017b0c89, 0x110d0000,
	}, {	// 6.0 MHz
		0x00000001, 0x00070012, 0x0012fff5, 0xffbaff82,
		0xff8e000f, 0x00e80198, 0x01750028, 0xfe18fc75,
		0xfc99ff15, 0x03050636, 0x0656027f, 0xfc32f6e2,
		0xf614fb17, 0x03d20b87, 0x0d7707d2, 0xfd26f341,
		0xefeaf56f, 0x010f0c64, 0x110d0000,
	}, {	// 6.1 MHz
		0xffff0000, 0x00050012, 0x001c000b, 0xffd1ff84,
		0xff66ffbe, 0x00960184, 0x01cd00da, 0xfeccfcb2,
		0xfc17fdf9, 0x01e005bc, 0x06e703e4, 0xfdabf798,
		0xf599f9b3, 0x02510abd, 0x0dbf08df, 0xfe48f3dc,
		0xefd5f4f6, 0x00a20c3e, 0x110d0000,
	}, {	// 6.2 MHz
		0xfffffffe, 0x0002000f, 0x0021001f, 0xfff0ff97,
		0xff50ff74, 0x0034014a, 0x01fa0179, 0xff97fd2a,
		0xfbd3fcfa, 0x00a304fe, 0x07310525, 0xff37f886,
		0xf55cf86e, 0x00c709d0, 0x0de209db, 0xff6df484,
		0xefcbf481, 0x00360c18, 0x110d0000,
	}, {	// 6.3 MHz
		0xfffffffd, 0xfffe000a, 0x0021002f, 0x0010ffb8,
		0xff50ff3b, 0xffcc00f0, 0x01fa01fa, 0x0069fdd4,
		0xfbd3fc26, 0xff5d0407, 0x07310638, 0x00c9f9a8,
		0xf55cf74e, 0xff3908c3, 0x0de20ac3, 0x0093f537,
		0xefcbf410, 0xffca0bf2, 0x110d0000,
	}, {	// 6.4 MHz
		0xfffffffd, 0xfffb0003, 0x001c0037, 0x002fffe2,
		0xff66ff17, 0xff6a007e, 0x01cd0251, 0x0134fea5,
		0xfc17fb8b, 0xfe2002e0, 0x06e70713, 0x0255faf5,
		0xf599f658, 0xfdaf0799, 0x0dbf0b96, 0x01b8f5f5,
		0xefd5f3a3, 0xff5e0bca, 0x110d0000,
	}, {	// 6.5 MHz
		0x0000fffd, 0xfff9fffb, 0x00120037, 0x00460010,
		0xff8eff0f, 0xff180000, 0x01750276, 0x01e8ff8d,
		0xfc99fb31, 0xfcfb0198, 0x065607ad, 0x03cefc64,
		0xf614f592, 0xfc2e0656, 0x0d770c52, 0x02daf6bd,
		0xefeaf33b, 0xfef10ba3, 0x110d0000,
	}, {	// 6.6 MHz
		0x0000fffe, 0xfff7fff5, 0x0005002f, 0x0054003c,
		0xffc5ff22, 0xfedfff82, 0x00fc0267, 0x0276007e,
		0xfd51fb1c, 0xfbfe003e, 0x05830802, 0x0529fdec,
		0xf6c8f4fe, 0xfabd04ff, 0x0d0d0cf6, 0x03f8f78f,
		0xf00af2d7, 0xfe850b7b, 0x110d0000,
	}, {	// 6.7 MHz
		0x0000ffff, 0xfff8fff0, 0xfff80020, 0x00560060,
		0x0002ff4e, 0xfec4ff10, 0x006d0225, 0x02d50166,
		0xfe35fb4e, 0xfb35fee1, 0x0477080e, 0x065bff82,
		0xf7b1f4a0, 0xf9610397, 0x0c810d80, 0x0510f869,
		0xf033f278, 0xfe1a0b52, 0x110d0000,
	}, {	// 6.8 MHz
		0x00010000, 0xfffaffee, 0xffec000c, 0x004c0078,
		0x0040ff8e, 0xfecafeb6, 0xffd301b6, 0x02fc0235,
		0xff36fbc5, 0xfaaafd90, 0x033e07d2, 0x075b011b,
		0xf8cbf47a, 0xf81f0224, 0x0bd50def, 0x0621f94b,
		0xf067f21e, 0xfdae0b29, 0x110d0000,
	}, {	// 6.9 MHz
		0x00010001, 0xfffdffef, 0xffe3fff6, 0x0037007f,
		0x0075ffdc, 0xfef2fe7c, 0xff3d0122, 0x02ea02dd,
		0x0044fc79, 0xfa65fc5d, 0x01e3074e, 0x082102ad,
		0xfa0ff48c, 0xf6fe00a9, 0x0b0a0e43, 0x0729fa33,
		0xf0a5f1c9, 0xfd430b00, 0x110d0000,
	}, {	// 7.0 MHz
		0x00010002, 0x0001fff3, 0xffdeffe2, 0x001b0076,
		0x009c002d, 0xff35fe68, 0xfeba0076, 0x029f0352,
		0x014dfd60, 0xfa69fb53, 0x00740688, 0x08a7042d,
		0xfb75f4d6, 0xf600ff2d, 0x0a220e7a, 0x0827fb22,
		0xf0edf17a, 0xfcd80ad6, 0x110d0000,
	}, {	// 7.1 MHz
		0x00000003, 0x0004fff9, 0xffe0ffd2, 0xfffb005e,
		0x00b0007a, 0xff8ffe7c, 0xfe53ffc1, 0x0221038c,
		0x0241fe6e, 0xfab6fa80, 0xff010587, 0x08e90590,
		0xfcf5f556, 0xf52bfdb3, 0x09210e95, 0x0919fc15,
		0xf13ff12f, 0xfc6e0aab, 0x110d0000,
	}, {	// 7.2 MHz
		0x00000003, 0x00070000, 0xffe6ffc9, 0xffdb0039,
		0x00af00b8, 0xfff4feb6, 0xfe13ff10, 0x01790388,
		0x0311ff92, 0xfb48f9ed, 0xfd980453, 0x08e306cd,
		0xfe88f60a, 0xf482fc40, 0x08080e93, 0x09fdfd0c,
		0xf19af0ea, 0xfc050a81, 0x110d0000,
	}, {	// 7.3 MHz
		0x00000002, 0x00080008, 0xfff0ffc9, 0xffc1000d,
		0x009800e2, 0x005bff10, 0xfe00fe74, 0x00b50345,
		0x03b000bc, 0xfc18f9a1, 0xfc4802f9, 0x089807dc,
		0x0022f6f0, 0xf407fada, 0x06da0e74, 0x0ad3fe06,
		0xf1fef0ab, 0xfb9c0a55, 0x110d0000,
	}, {	// 7.4 MHz
		0x00000001, 0x0008000e, 0xfffdffd0, 0xffafffdf,
		0x006e00f2, 0x00b8ff82, 0xfe1bfdf8, 0xffe302c8,
		0x041301dc, 0xfd1af99e, 0xfb1e0183, 0x080908b5,
		0x01bcf801, 0xf3bdf985, 0x059a0e38, 0x0b99ff03,
		0xf26cf071, 0xfb330a2a, 0x110d0000,
	}, {	// 7.5 MHz
		0xffff0000, 0x00070011, 0x000affdf, 0xffa9ffb5,
		0x003700e6, 0x01010000, 0xfe62fda8, 0xff140219,
		0x043502e1, 0xfe42f9e6, 0xfa270000, 0x073a0953,
		0x034cf939, 0xf3a4f845, 0x044c0de1, 0x0c4f0000,
		0xf2e2f03c, 0xfacc09fe, 0x110d0000,
	}, {	// 7.6 MHz
		0xffffffff, 0x00040012, 0x0016fff3, 0xffafff95,
		0xfff900c0, 0x0130007e, 0xfecefd89, 0xfe560146,
		0x041303bc, 0xff81fa76, 0xf96cfe7d, 0x063209b1,
		0x04c9fa93, 0xf3bdf71e, 0x02f30d6e, 0x0cf200fd,
		0xf361f00e, 0xfa6509d1, 0x110d0000,
	}, {	// 7.7 MHz
		0xfffffffe, 0x00010010, 0x001e0008, 0xffc1ff84,
		0xffbc0084, 0x013e00f0, 0xff56fd9f, 0xfdb8005c,
		0x03b00460, 0x00c7fb45, 0xf8f4fd07, 0x04fa09ce,
		0x062afc07, 0xf407f614, 0x01920ce0, 0x0d8301fa,
		0xf3e8efe5, 0xfa0009a4, 0x110d0000,
	}, {	// 7.8 MHz
		0x0000fffd, 0xfffd000b, 0x0022001d, 0xffdbff82,
		0xff870039, 0x012a014a, 0xffedfde7, 0xfd47ff6b,
		0x031104c6, 0x0202fc4c, 0xf8c6fbad, 0x039909a7,
		0x0767fd8e, 0xf482f52b, 0x002d0c39, 0x0e0002f4,
		0xf477efc2, 0xf99b0977, 0x110d0000,
	}, {	// 7.9 MHz
		0x0000fffd, 0xfffa0004, 0x0020002d, 0xfffbff91,
		0xff61ffe8, 0x00f70184, 0x0086fe5c, 0xfd0bfe85,
		0x024104e5, 0x0323fd7d, 0xf8e2fa79, 0x021d093f,
		0x0879ff22, 0xf52bf465, 0xfec70b79, 0x0e6803eb,
		0xf50defa5, 0xf937094a, 0x110d0000,
	}, {	// 8.0 MHz
		0x0000fffe, 0xfff8fffd, 0x00190036, 0x001bffaf,
		0xff4fff99, 0x00aa0198, 0x0112fef3, 0xfd09fdb9,
		0x014d04be, 0x041bfecc, 0xf947f978, 0x00900897,
		0x095a00b9, 0xf600f3c5, 0xfd650aa3, 0x0ebc04de,
		0xf5aaef8e, 0xf8d5091c, 0x110d0000,
	}, {	// 8.1 MHz
		0x0000ffff, 0xfff7fff6, 0x000e0038, 0x0037ffd7,
		0xff52ff56, 0x004b0184, 0x0186ffa1, 0xfd40fd16,
		0x00440452, 0x04de0029, 0xf9f2f8b2, 0xfefe07b5,
		0x0a05024d, 0xf6fef34d, 0xfc0a09b8, 0x0efa05cd,
		0xf64eef7d, 0xf87308ed, 0x110d0000,
	}, {	// 8.2 MHz
		0x00010000, 0xfff8fff0, 0x00000031, 0x004c0005,
		0xff6aff27, 0xffe4014a, 0x01d70057, 0xfdacfca6,
		0xff3603a7, 0x05610184, 0xfadbf82e, 0xfd74069f,
		0x0a7503d6, 0xf81ff2ff, 0xfab808b9, 0x0f2306b5,
		0xf6f9ef72, 0xf81308bf, 0x110d0000,
	}, {	// 8.3 MHz
		0x00010001, 0xfffbffee, 0xfff30022, 0x00560032,
		0xff95ff10, 0xff8000f0, 0x01fe0106, 0xfe46fc71,
		0xfe3502c7, 0x059e02ce, 0xfbf9f7f2, 0xfbff055b,
		0x0aa9054c, 0xf961f2db, 0xf97507aa, 0x0f350797,
		0xf7a9ef6d, 0xf7b40890, 0x110d0000,
	}, {	// 8.4 MHz
		0x00010002, 0xfffeffee, 0xffe8000f, 0x00540058,
		0xffcdff14, 0xff29007e, 0x01f6019e, 0xff01fc7c,
		0xfd5101bf, 0x059203f6, 0xfd41f7fe, 0xfaa903f3,
		0x0a9e06a9, 0xfabdf2e2, 0xf842068b, 0x0f320871,
		0xf85eef6e, 0xf7560860, 0x110d0000,
	}, {	// 8.5 MHz
		0x00000003, 0x0002fff2, 0xffe1fff9, 0x00460073,
		0x000bff34, 0xfee90000, 0x01c10215, 0xffd0fcc5,
		0xfc99009d, 0x053d04f1, 0xfea5f853, 0xf97d0270,
		0x0a5607e4, 0xfc2ef314, 0xf723055f, 0x0f180943,
		0xf919ef75, 0xf6fa0830, 0x110d0000,
	}, {	// 8.6 MHz
		0x00000003, 0x0005fff8, 0xffdeffe4, 0x002f007f,
		0x0048ff6b, 0xfec7ff82, 0x0163025f, 0x00a2fd47,
		0xfc17ff73, 0x04a405b2, 0x0017f8ed, 0xf88500dc,
		0x09d208f9, 0xfdaff370, 0xf61c0429, 0x0ee80a0b,
		0xf9d8ef82, 0xf6a00800, 0x110d0000,
	}, {	// 8.7 MHz
		0x00000003, 0x0007ffff, 0xffe1ffd4, 0x0010007a,
		0x007cffb2, 0xfec6ff10, 0x00e60277, 0x0168fdf9,
		0xfbd3fe50, 0x03ce0631, 0x0188f9c8, 0xf7c7ff43,
		0x091509e3, 0xff39f3f6, 0xf52d02ea, 0x0ea30ac9,
		0xfa9bef95, 0xf64607d0, 0x110d0000,
	}, {	// 8.8 MHz
		0x00000002, 0x00090007, 0xffe9ffca, 0xfff00065,
		0x00a10003, 0xfee6feb6, 0x0053025b, 0x0213fed0,
		0xfbd3fd46, 0x02c70668, 0x02eafadb, 0xf74bfdae,
		0x08230a9c, 0x00c7f4a3, 0xf45b01a6, 0x0e480b7c,
		0xfb61efae, 0xf5ef079f, 0x110d0000,
	}, {	// 8.9 MHz
		0xffff0000, 0x0008000d, 0xfff5ffc8, 0xffd10043,
		0x00b20053, 0xff24fe7c, 0xffb9020c, 0x0295ffbb,
		0xfc17fc64, 0x019b0654, 0x042dfc1c, 0xf714fc2a,
		0x07020b21, 0x0251f575, 0xf3a7005e, 0x0dd80c24,
		0xfc2aefcd, 0xf599076e, 0x110d0000,
	}, {	// 9.0 MHz
		0xffffffff, 0x00060011, 0x0002ffcf, 0xffba0018,
		0x00ad009a, 0xff79fe68, 0xff260192, 0x02e500ab,
		0xfc99fbb6, 0x005b05f7, 0x0545fd81, 0xf723fabf,
		0x05b80b70, 0x03d2f669, 0xf313ff15, 0x0d550cbf,
		0xfcf6eff2, 0xf544073d, 0x110d0000,
	}, {	// 9.1 MHz
		0xfffffffe, 0x00030012, 0x000fffdd, 0xffacffea,
		0x009300cf, 0xffdcfe7c, 0xfea600f7, 0x02fd0190,
		0xfd51fb46, 0xff150554, 0x0627fefd, 0xf778f978,
		0x044d0b87, 0x0543f77d, 0xf2a0fdcf, 0x0cbe0d4e,
		0xfdc4f01d, 0xf4f2070b, 0x110d0000,
	}, {	// 9.2 MHz
		0x0000fffd, 0x00000010, 0x001afff0, 0xffaaffbf,
		0x006700ed, 0x0043feb6, 0xfe460047, 0x02db0258,
		0xfe35fb1b, 0xfddc0473, 0x06c90082, 0xf811f85e,
		0x02c90b66, 0x069ff8ad, 0xf250fc8d, 0x0c140dcf,
		0xfe93f04d, 0xf4a106d9, 0x110d0000,
	}, {	// 9.3 MHz
		0x0000fffd, 0xfffc000c, 0x00200006, 0xffb4ff9c,
		0x002f00ef, 0x00a4ff10, 0xfe0dff92, 0x028102f7,
		0xff36fb37, 0xfcbf035e, 0x07260202, 0xf8e8f778,
		0x01340b0d, 0x07e1f9f4, 0xf223fb51, 0x0b590e42,
		0xff64f083, 0xf45206a7, 0x110d0000,
	}, {	// 9.4 MHz
		0x0000fffd, 0xfff90005, 0x0022001a, 0xffc9ff86,
		0xfff000d7, 0x00f2ff82, 0xfe01fee5, 0x01f60362,
		0x0044fb99, 0xfbcc0222, 0x07380370, 0xf9f7f6cc,
		0xff990a7e, 0x0902fb50, 0xf21afa1f, 0x0a8d0ea6,
		0x0034f0bf, 0xf4050675, 0x110d0000,
	}, {	// 9.5 MHz
		0x0000fffe, 0xfff8fffe, 0x001e002b, 0xffe5ff81,
		0xffb400a5, 0x01280000, 0xfe24fe50, 0x01460390,
		0x014dfc3a, 0xfb1000ce, 0x070104bf, 0xfb37f65f,
		0xfe0009bc, 0x0a00fcbb, 0xf235f8f8, 0x09b20efc,
		0x0105f101, 0xf3ba0642, 0x110d0000,
	}, {	// 9.6 MHz
		0x0001ffff, 0xfff8fff7, 0x00150036, 0x0005ff8c,
		0xff810061, 0x013d007e, 0xfe71fddf, 0x007c0380,
		0x0241fd13, 0xfa94ff70, 0x068005e2, 0xfc9bf633,
		0xfc7308ca, 0x0ad5fe30, 0xf274f7e0, 0x08c90f43,
		0x01d4f147, 0xf371060f, 0x110d0000,
	}, {	// 9.7 MHz
		0x00010001, 0xfff9fff1, 0x00090038, 0x0025ffa7,
		0xff5e0012, 0x013200f0, 0xfee3fd9b, 0xffaa0331,
		0x0311fe15, 0xfa60fe18, 0x05bd06d1, 0xfe1bf64a,
		0xfafa07ae, 0x0b7effab, 0xf2d5f6d7, 0x07d30f7a,
		0x02a3f194, 0xf32905dc, 0x110d0000,
	}, {	// 9.8 MHz
		0x00010002, 0xfffcffee, 0xfffb0032, 0x003fffcd,
		0xff4effc1, 0x0106014a, 0xff6efd8a, 0xfedd02aa,
		0x03b0ff34, 0xfa74fcd7, 0x04bf0781, 0xffaaf6a3,
		0xf99e066b, 0x0bf90128, 0xf359f5e1, 0x06d20fa2,
		0x0370f1e5, 0xf2e405a8, 0x110d0000,
	}, {	// 9.9 MHz
		0x00000003, 0xffffffee, 0xffef0024, 0x0051fffa,
		0xff54ff77, 0x00be0184, 0x0006fdad, 0xfe2701f3,
		0x0413005e, 0xfad1fbba, 0x039007ee, 0x013bf73d,
		0xf868050a, 0x0c4302a1, 0xf3fdf4fe, 0x05c70fba,
		0x043bf23c, 0xf2a10575, 0x110d0000,
	}, {	// 10.0 MHz
		0x00000003, 0x0003fff1, 0xffe50011, 0x00570027,
		0xff70ff3c, 0x00620198, 0x009efe01, 0xfd95011a,
		0x04350183, 0xfb71fad0, 0x023c0812, 0x02c3f811,
		0xf75e0390, 0x0c5c0411, 0xf4c1f432, 0x04b30fc1,
		0x0503f297, 0xf2610541, 0x110d0000,
	}, {	// 10.1 MHz
		0x00000003, 0x0006fff7, 0xffdffffc, 0x00510050,
		0xff9dff18, 0xfffc0184, 0x0128fe80, 0xfd32002e,
		0x04130292, 0xfc4dfa21, 0x00d107ee, 0x0435f91c,
		0xf6850205, 0x0c430573, 0xf5a1f37d, 0x03990fba,
		0x05c7f2f8, 0xf222050d, 0x110d0000,
	}, {	// 10.2 MHz
		0x00000002, 0x0008fffe, 0xffdfffe7, 0x003f006e,
		0xffd6ff0f, 0xff96014a, 0x0197ff1f, 0xfd05ff3e,
		0x03b0037c, 0xfd59f9b7, 0xff5d0781, 0x0585fa56,
		0xf5e4006f, 0x0bf906c4, 0xf69df2e0, 0x02790fa2,
		0x0688f35d, 0xf1e604d8, 0x110d0000,
	}, {	// 10.3 MHz
		0xffff0001, 0x00090005, 0xffe4ffd6, 0x0025007e,
		0x0014ff20, 0xff3c00f0, 0x01e1ffd0, 0xfd12fe5c,
		0x03110433, 0xfe88f996, 0xfdf106d1, 0x06aafbb7,
		0xf57efed8, 0x0b7e07ff, 0xf7b0f25e, 0x01560f7a,
		0x0745f3c7, 0xf1ac04a4, 0x110d0000,
	}, {	// 10.4 MHz
		0xffffffff, 0x0008000c, 0xffedffcb, 0x0005007d,
		0x0050ff4c, 0xfef6007e, 0x01ff0086, 0xfd58fd97,
		0x024104ad, 0xffcaf9c0, 0xfc9905e2, 0x079afd35,
		0xf555fd46, 0x0ad50920, 0xf8d9f1f6, 0x00310f43,
		0x07fdf435, 0xf174046f, 0x110d0000,
	}, {	// 10.5 MHz
		0xfffffffe, 0x00050011, 0xfffaffc8, 0xffe5006b,
		0x0082ff8c, 0xfecc0000, 0x01f00130, 0xfdd2fcfc,
		0x014d04e3, 0x010efa32, 0xfb6404bf, 0x084efec5,
		0xf569fbc2, 0x0a000a23, 0xfa15f1ab, 0xff0b0efc,
		0x08b0f4a7, 0xf13f043a, 0x110d0000,
	}, {	// 10.6 MHz
		0x0000fffd, 0x00020012, 0x0007ffcd, 0xffc9004c,
		0x00a4ffd9, 0xfec3ff82, 0x01b401c1, 0xfe76fc97,
		0x004404d2, 0x0245fae8, 0xfa5f0370, 0x08c1005f,
		0xf5bcfa52, 0x09020b04, 0xfb60f17b, 0xfde70ea6,
		0x095df51e, 0xf10c0405, 0x110d0000,
	}, {	// 10.7 MHz
		0x0000fffd, 0xffff0011, 0x0014ffdb, 0xffb40023,
		0x00b2002a, 0xfedbff10, 0x0150022d, 0xff38fc6f,
		0xff36047b, 0x035efbda, 0xf9940202, 0x08ee01f5,
		0xf649f8fe, 0x07e10bc2, 0xfcb6f169, 0xfcc60e42,
		0x0a04f599, 0xf0db03d0, 0x110d0000,
	}, {	// 10.8 MHz
		0x0000fffd, 0xfffb000d, 0x001dffed, 0xffaafff5,
		0x00aa0077, 0xff13feb6, 0x00ce026b, 0x000afc85,
		0xfe3503e3, 0x044cfcfb, 0xf90c0082, 0x08d5037f,
		0xf710f7cc, 0x069f0c59, 0xfe16f173, 0xfbaa0dcf,
		0x0aa5f617, 0xf0ad039b, 0x110d0000,
	}, {	// 10.9 MHz
		0x0000fffe, 0xfff90006, 0x00210003, 0xffacffc8,
		0x008e00b6, 0xff63fe7c, 0x003a0275, 0x00dafcda,
		0xfd510313, 0x0501fe40, 0xf8cbfefd, 0x087604f0,
		0xf80af6c2, 0x05430cc8, 0xff7af19a, 0xfa940d4e,
		0x0b3ff699, 0xf0810365, 0x110d0000,
	}, {	// 11.0 MHz
		0x0001ffff, 0xfff8ffff, 0x00210018, 0xffbaffa3,
		0x006000e1, 0xffc4fe68, 0xffa0024b, 0x019afd66,
		0xfc990216, 0x0575ff99, 0xf8d4fd81, 0x07d40640,
		0xf932f5e6, 0x03d20d0d, 0x00dff1de, 0xf9860cbf,
		0x0bd1f71e, 0xf058032f, 0x110d0000,
	}, {	// 11.1 MHz
		0x00010000, 0xfff8fff8, 0x001b0029, 0xffd1ff8a,
		0x002600f2, 0x002cfe7c, 0xff0f01f0, 0x023bfe20,
		0xfc1700fa, 0x05a200f7, 0xf927fc1c, 0x06f40765,
		0xfa82f53b, 0x02510d27, 0x0243f23d, 0xf8810c24,
		0x0c5cf7a7, 0xf03102fa, 0x110d0000,
	}, {	// 11.2 MHz
		0x00010002, 0xfffafff2, 0x00110035, 0xfff0ff81,
		0xffe700e7, 0x008ffeb6, 0xfe94016d, 0x02b0fefb,
		0xfbd3ffd1, 0x05850249, 0xf9c1fadb, 0x05de0858,
		0xfbf2f4c4, 0x00c70d17, 0x03a0f2b8, 0xf7870b7c,
		0x0cdff833, 0xf00d02c4, 0x110d0000,
	}, {	// 11.3 MHz
		0x00000003, 0xfffdffee, 0x00040038, 0x0010ff88,
		0xffac00c2, 0x00e2ff10, 0xfe3900cb, 0x02f1ffe9,
		0xfbd3feaa, 0x05210381, 0xfa9cf9c8, 0x04990912,
		0xfd7af484, 0xff390cdb, 0x04f4f34d, 0xf69a0ac9,
		0x0d5af8c1, 0xefec028e, 0x110d0000,
	}, {	// 11.4 MHz
		0x00000003, 0x0000ffee, 0xfff60033, 0x002fff9f,
		0xff7b0087, 0x011eff82, 0xfe080018, 0x02f900d8,
		0xfc17fd96, 0x04790490, 0xfbadf8ed, 0x032f098e,
		0xff10f47d, 0xfdaf0c75, 0x063cf3fc, 0xf5ba0a0b,
		0x0dccf952, 0xefcd0258, 0x110d0000,
	}, {	// 11.5 MHz
		0x00000003, 0x0004fff1, 0xffea0026, 0x0046ffc3,
		0xff5a003c, 0x013b0000, 0xfe04ff63, 0x02c801b8,
		0xfc99fca6, 0x0397056a, 0xfcecf853, 0x01ad09c9,
		0x00acf4ad, 0xfc2e0be7, 0x0773f4c2, 0xf4e90943,
		0x0e35f9e6, 0xefb10221, 0x110d0000,
	}, {	// 11.6 MHz
		0x00000002, 0x0007fff6, 0xffe20014, 0x0054ffee,
		0xff4effeb, 0x0137007e, 0xfe2efebb, 0x0260027a,
		0xfd51fbe6, 0x02870605, 0xfe4af7fe, 0x001d09c1,
		0x0243f515, 0xfabd0b32, 0x0897f59e, 0xf4280871,
		0x0e95fa7c, 0xef9701eb, 0x110d0000,
	}, {	// 11.7 MHz
		0xffff0001, 0x0008fffd, 0xffdeffff, 0x0056001d,
		0xff57ff9c, 0x011300f0, 0xfe82fe2e, 0x01ca0310,
		0xfe35fb62, 0x0155065a, 0xffbaf7f2, 0xfe8c0977,
		0x03cef5b2, 0xf9610a58, 0x09a5f68f, 0xf3790797,
		0x0eebfb14, 0xef8001b5, 0x110d0000,
	}, {	// 11.8 MHz
		0xffff0000, 0x00080004, 0xffe0ffe9, 0x004c0047,
		0xff75ff58, 0x00d1014a, 0xfef9fdc8, 0x0111036f,
		0xff36fb21, 0x00120665, 0x012df82e, 0xfd0708ec,
		0x0542f682, 0xf81f095c, 0x0a9af792, 0xf2db06b5,
		0x0f38fbad, 0xef6c017e, 0x110d0000,
	}, {	// 11.9 MHz
		0xffffffff, 0x0007000b, 0xffe7ffd8, 0x00370068,
		0xffa4ff28, 0x00790184, 0xff87fd91, 0x00430392,
		0x0044fb26, 0xfece0626, 0x0294f8b2, 0xfb990825,
		0x0698f77f, 0xf6fe0842, 0x0b73f8a7, 0xf25105cd,
		0x0f7bfc48, 0xef5a0148, 0x110d0000,
	}, {	// 12.0 MHz
		0x0000fffe, 0x00050010, 0xfff2ffcc, 0x001b007b,
		0xffdfff10, 0x00140198, 0x0020fd8e, 0xff710375,
		0x014dfb73, 0xfd9a059f, 0x03e0f978, 0xfa4e0726,
		0x07c8f8a7, 0xf600070c, 0x0c2ff9c9, 0xf1db04de,
		0x0fb4fce5, 0xef4b0111, 0x110d0000,
	}, {	// 12.1 MHz
		0x0000fffd, 0x00010012, 0xffffffc8, 0xfffb007e,
		0x001dff14, 0xffad0184, 0x00b7fdbe, 0xfea9031b,
		0x0241fc01, 0xfc8504d6, 0x0504fa79, 0xf93005f6,
		0x08caf9f2, 0xf52b05c0, 0x0ccbfaf9, 0xf17903eb,
		0x0fe3fd83, 0xef3f00db, 0x110d0000,
	}, {	// 12.2 MHz
		0x0000fffd, 0xfffe0011, 0x000cffcc, 0xffdb0071,
		0x0058ff32, 0xff4f014a, 0x013cfe1f, 0xfdfb028a,
		0x0311fcc9, 0xfb9d03d6, 0x05f4fbad, 0xf848049d,
		0x0999fb5b, 0xf4820461, 0x0d46fc32, 0xf12d02f4,
		0x1007fe21, 0xef3600a4, 0x110d0000,
	}, {	// 12.3 MHz
		0x0000fffe, 0xfffa000e, 0x0017ffd9, 0xffc10055,
		0x0088ff68, 0xff0400f0, 0x01a6fea7, 0xfd7501cc,
		0x03b0fdc0, 0xfaef02a8, 0x06a7fd07, 0xf79d0326,
		0x0a31fcda, 0xf40702f3, 0x0d9ffd72, 0xf0f601fa,
		0x1021fec0, 0xef2f006d, 0x110d0000,
	}, {	// 12.4 MHz
		0x0001ffff, 0xfff80007, 0x001fffeb, 0xffaf002d,
		0x00a8ffb0, 0xfed3007e, 0x01e9ff4c, 0xfd2000ee,
		0x0413fed8, 0xfa82015c, 0x0715fe7d, 0xf7340198,
		0x0a8dfe69, 0xf3bd017c, 0x0dd5feb8, 0xf0d500fd,
		0x1031ff60, 0xef2b0037, 0x110d0000,
	}, {	// 12.5 MHz
		0x00010000, 0xfff70000, 0x00220000, 0xffa90000,
		0x00b30000, 0xfec20000, 0x02000000, 0xfd030000,
		0x04350000, 0xfa5e0000, 0x073b0000, 0xf7110000,
		0x0aac0000, 0xf3a40000, 0x0de70000, 0xf0c90000,
		0x10360000, 0xef290000, 0x110d0000,
	}, {	// 12.6 MHz
		0x00010001, 0xfff8fff9, 0x001f0015, 0xffafffd3,
		0x00a80050, 0xfed3ff82, 0x01e900b4, 0xfd20ff12,
		0x04130128, 0xfa82fea4, 0x07150183, 0xf734fe68,
		0x0a8d0197, 0xf3bdfe84, 0x0dd50148, 0xf0d5ff03,
		0x103100a0, 0xef2bffc9, 0x110d0000,
	}, {	// 12.7 MHz
		0x00000002, 0xfffafff2, 0x00170027, 0xffc1ffab,
		0x00880098, 0xff04ff10, 0x01a60159, 0xfd75fe34,
		0x03b00240, 0xfaeffd58, 0x06a702f9, 0xf79dfcda,
		0x0a310326, 0xf407fd0d, 0x0d9f028e, 0xf0f6fe06,
		0x10210140, 0xef2fff93, 0x110d0000,
	}, {	// 12.8 MHz
		0x00000003, 0xfffeffef, 0x000c0034, 0xffdbff8f,
		0x005800ce, 0xff4ffeb6, 0x013c01e1, 0xfdfbfd76,
		0x03110337, 0xfb9dfc2a, 0x05f40453, 0xf848fb63,
		0x099904a5, 0xf482fb9f, 0x0d4603ce, 0xf12dfd0c,
		0x100701df, 0xef36ff5c, 0x110d0000,
	}, {	// 12.9 MHz
		0x00000003, 0x0001ffee, 0xffff0038, 0xfffbff82,
		0x001d00ec, 0xffadfe7c, 0x00b70242, 0xfea9fce5,
		0x024103ff, 0xfc85fb2a, 0x05040587, 0xf930fa0a,
		0x08ca060e, 0xf52bfa40, 0x0ccb0507, 0xf179fc15,
		0x0fe3027d, 0xef3fff25, 0x110d0000,
	}, {	// 13.0 MHz
		0x00000002, 0x0005fff0, 0xfff20034, 0x001bff85,
		0xffdf00f0, 0x0014fe68, 0x00200272, 0xff71fc8b,
		0x014d048d, 0xfd9afa61, 0x03e00688, 0xfa4ef8da,
		0x07c80759, 0xf600f8f4, 0x0c2f0637, 0xf1dbfb22,
		0x0fb4031b, 0xef4bfeef, 0x110d0000,
	}, {	// 13.1 MHz
		0xffff0001, 0x0007fff5, 0xffe70028, 0x0037ff98,
		0xffa400d8, 0x0079fe7c, 0xff87026f, 0x0043fc6e,
		0x004404da, 0xfecef9da, 0x0294074e, 0xfb99f7db,
		0x06980881, 0xf6fef7be, 0x0b730759, 0xf251fa33,
		0x0f7b03b8, 0xef5afeb8, 0x110d0000,
	}, {	// 13.2 MHz
		0xffff0000, 0x0008fffc, 0xffe00017, 0x004cffb9,
		0xff7500a8, 0x00d1feb6, 0xfef90238, 0x0111fc91,
		0xff3604df, 0x0012f99b, 0x012d07d2, 0xfd07f714,
		0x0542097e, 0xf81ff6a4, 0x0a9a086e, 0xf2dbf94b,
		0x0f380453, 0xef6cfe82, 0x110d0000,
	}, {	// 13.3 MHz
		0xffffffff, 0x00080003, 0xffde0001, 0x0056ffe3,
		0xff570064, 0x0113ff10, 0xfe8201d2, 0x01cafcf0,
		0xfe35049e, 0x0155f9a6, 0xffba080e, 0xfe8cf689,
		0x03ce0a4e, 0xf961f5a8, 0x09a50971, 0xf379f869,
		0x0eeb04ec, 0xef80fe4b, 0x110d0000,
	}, {	// 13.4 MHz
		0x0000fffe, 0x0007000a, 0xffe2ffec, 0x00540012,
		0xff4e0015, 0x0137ff82, 0xfe2e0145, 0x0260fd86,
		0xfd51041a, 0x0287f9fb, 0xfe4a0802, 0x001df63f,
		0x02430aeb, 0xfabdf4ce, 0x08970a62, 0xf428f78f,
		0x0e950584, 0xef97fe15, 0x110d0000,
	}, {	// 13.5 MHz
		0x0000fffd, 0x0004000f, 0xffeaffda, 0x0046003d,
		0xff5affc4, 0x013b0000, 0xfe04009d, 0x02c8fe48,
		0xfc99035a, 0x0397fa96, 0xfcec07ad, 0x01adf637,
		0x00ac0b53, 0xfc2ef419, 0x07730b3e, 0xf4e9f6bd,
		0x0e35061a, 0xefb1fddf, 0x110d0000,
	}, {	// 13.6 MHz
		0x0000fffd, 0x00000012, 0xfff6ffcd, 0x002f0061,
		0xff7bff79, 0x011e007e, 0xfe08ffe8, 0x02f9ff28,
		0xfc17026a, 0x0479fb70, 0xfbad0713, 0x032ff672,
		0xff100b83, 0xfdaff38b, 0x063c0c04, 0xf5baf5f5,
		0x0dcc06ae, 0xefcdfda8, 0x110d0000,
	}, {	// 13.7 MHz
		0x0000fffd, 0xfffd0012, 0x0004ffc8, 0x00100078,
		0xffacff3e, 0x00e200f0, 0xfe39ff35, 0x02f10017,
		0xfbd30156, 0x0521fc7f, 0xfa9c0638, 0x0499f6ee,
		0xfd7a0b7c, 0xff39f325, 0x04f40cb3, 0xf69af537,
		0x0d5a073f, 0xefecfd72, 0x110d0000,
	}, {	// 13.8 MHz
		0x0001fffe, 0xfffa000e, 0x0011ffcb, 0xfff0007f,
		0xffe7ff19, 0x008f014a, 0xfe94fe93, 0x02b00105,
		0xfbd3002f, 0x0585fdb7, 0xf9c10525, 0x05def7a8,
		0xfbf20b3c, 0x00c7f2e9, 0x03a00d48, 0xf787f484,
		0x0cdf07cd, 0xf00dfd3c, 0x110d0000,
	}, {	// 13.9 MHz
		0x00010000, 0xfff80008, 0x001bffd7, 0xffd10076,
		0x0026ff0e, 0x002c0184, 0xff0ffe10, 0x023b01e0,
		0xfc17ff06, 0x05a2ff09, 0xf92703e4, 0x06f4f89b,
		0xfa820ac5, 0x0251f2d9, 0x02430dc3, 0xf881f3dc,
		0x0c5c0859, 0xf031fd06, 0x110d0000,
	}, {	// 14.0 MHz
		0x00010001, 0xfff80001, 0x0021ffe8, 0xffba005d,
		0x0060ff1f, 0xffc40198, 0xffa0fdb5, 0x019a029a,
		0xfc99fdea, 0x05750067, 0xf8d4027f, 0x07d4f9c0,
		0xf9320a1a, 0x03d2f2f3, 0x00df0e22, 0xf986f341,
		0x0bd108e2, 0xf058fcd1, 0x110d0000,
	}, {	// 14.1 MHz
		0x00000002, 0xfff9fffa, 0x0021fffd, 0xffac0038,
		0x008eff4a, 0xff630184, 0x003afd8b, 0x00da0326,
		0xfd51fced, 0x050101c0, 0xf8cb0103, 0x0876fb10,
		0xf80a093e, 0x0543f338, 0xff7a0e66, 0xfa94f2b2,
		0x0b3f0967, 0xf081fc9b, 0x110d0000,
	}, {	// 14.2 MHz
		0x00000003, 0xfffbfff3, 0x001d0013, 0xffaa000b,
		0x00aaff89, 0xff13014a, 0x00cefd95, 0x000a037b,
		0xfe35fc1d, 0x044c0305, 0xf90cff7e, 0x08d5fc81,
		0xf7100834, 0x069ff3a7, 0xfe160e8d, 0xfbaaf231,
		0x0aa509e9, 0xf0adfc65, 0x110d0000,
	}, {	// 14.3 MHz
		0x00000003, 0xffffffef, 0x00140025, 0xffb4ffdd,
		0x00b2ffd6, 0xfedb00f0, 0x0150fdd3, 0xff380391,
		0xff36fb85, 0x035e0426, 0xf994fdfe, 0x08eefe0b,
		0xf6490702, 0x07e1f43e, 0xfcb60e97, 0xfcc6f1be,
		0x0a040a67, 0xf0dbfc30, 0x110d0000,
	}, {	// 14.4 MHz
		0x00000003, 0x0002ffee, 0x00070033, 0xffc9ffb4,
		0x00a40027, 0xfec3007e, 0x01b4fe3f, 0xfe760369,
		0x0044fb2e, 0x02450518, 0xfa5ffc90, 0x08c1ffa1,
		0xf5bc05ae, 0x0902f4fc, 0xfb600e85, 0xfde7f15a,
		0x095d0ae2, 0xf10cfbfb, 0x110d0000,
	}, {	// 14.5 MHz
		0xffff0002, 0x0005ffef, 0xfffa0038, 0xffe5ff95,
		0x00820074, 0xfecc0000, 0x01f0fed0, 0xfdd20304,
		0x014dfb1d, 0x010e05ce, 0xfb64fb41, 0x084e013b,
		0xf569043e, 0x0a00f5dd, 0xfa150e55, 0xff0bf104,
		0x08b00b59, 0xf13ffbc6, 0x110d0000,
	}, {	// 14.6 MHz
		0xffff0001, 0x0008fff4, 0xffed0035, 0x0005ff83,
		0x005000b4, 0xfef6ff82, 0x01ffff7a, 0xfd580269,
		0x0241fb53, 0xffca0640, 0xfc99fa1e, 0x079a02cb,
		0xf55502ba, 0x0ad5f6e0, 0xf8d90e0a, 0x0031f0bd,
		0x07fd0bcb, 0xf174fb91, 0x110d0000,
	}, {	// 14.7 MHz
		0xffffffff, 0x0009fffb, 0xffe4002a, 0x0025ff82,
		0x001400e0, 0xff3cff10, 0x01e10030, 0xfd1201a4,
		0x0311fbcd, 0xfe88066a, 0xfdf1f92f, 0x06aa0449,
		0xf57e0128, 0x0b7ef801, 0xf7b00da2, 0x0156f086,
		0x07450c39, 0xf1acfb5c, 0x110d0000,
	}, {	// 14.8 MHz
		0x0000fffe, 0x00080002, 0xffdf0019, 0x003fff92,
		0xffd600f1, 0xff96feb6, 0x019700e1, 0xfd0500c2,
		0x03b0fc84, 0xfd590649, 0xff5df87f, 0x058505aa,
		0xf5e4ff91, 0x0bf9f93c, 0xf69d0d20, 0x0279f05e,
		0x06880ca3, 0xf1e6fb28, 0x110d0000,
	}, {	// 14.9 MHz
		0x0000fffd, 0x00060009, 0xffdf0004, 0x0051ffb0,
		0xff9d00e8, 0xfffcfe7c, 0x01280180, 0xfd32ffd2,
		0x0413fd6e, 0xfc4d05df, 0x00d1f812, 0x043506e4,
		0xf685fdfb, 0x0c43fa8d, 0xf5a10c83, 0x0399f046,
		0x05c70d08, 0xf222faf3, 0x110d0000,
	}, {	// 15.0 MHz
		0x0000fffd, 0x0003000f, 0xffe5ffef, 0x0057ffd9,
		0xff7000c4, 0x0062fe68, 0x009e01ff, 0xfd95fee6,
		0x0435fe7d, 0xfb710530, 0x023cf7ee, 0x02c307ef,
		0xf75efc70, 0x0c5cfbef, 0xf4c10bce, 0x04b3f03f,
		0x05030d69, 0xf261fabf, 0x110d0000,
	}, {	// 15.1 MHz
		0x0000fffd, 0xffff0012, 0xffefffdc, 0x00510006,
		0xff540089, 0x00befe7c, 0x00060253, 0xfe27fe0d,
		0x0413ffa2, 0xfad10446, 0x0390f812, 0x013b08c3,
		0xf868faf6, 0x0c43fd5f, 0xf3fd0b02, 0x05c7f046,
		0x043b0dc4, 0xf2a1fa8b, 0x110d0000,
	}, {	// 15.2 MHz
		0x0001fffe, 0xfffc0012, 0xfffbffce, 0x003f0033,
		0xff4e003f, 0x0106feb6, 0xff6e0276, 0xfeddfd56,
		0x03b000cc, 0xfa740329, 0x04bff87f, 0xffaa095d,
		0xf99ef995, 0x0bf9fed8, 0xf3590a1f, 0x06d2f05e,
		0x03700e1b, 0xf2e4fa58, 0x110d0000,
	}, {	// 15.3 MHz
		0x0001ffff, 0xfff9000f, 0x0009ffc8, 0x00250059,
		0xff5effee, 0x0132ff10, 0xfee30265, 0xffaafccf,
		0x031101eb, 0xfa6001e8, 0x05bdf92f, 0xfe1b09b6,
		0xfafaf852, 0x0b7e0055, 0xf2d50929, 0x07d3f086,
		0x02a30e6c, 0xf329fa24, 0x110d0000,
	}, {	// 15.4 MHz
		0x00010001, 0xfff80009, 0x0015ffca, 0x00050074,
		0xff81ff9f, 0x013dff82, 0xfe710221, 0x007cfc80,
		0x024102ed, 0xfa940090, 0x0680fa1e, 0xfc9b09cd,
		0xfc73f736, 0x0ad501d0, 0xf2740820, 0x08c9f0bd,
		0x01d40eb9, 0xf371f9f1, 0x110d0000,
	}, {	// 15.5 MHz
		0x00000002, 0xfff80002, 0x001effd5, 0xffe5007f,
		0xffb4ff5b, 0x01280000, 0xfe2401b0, 0x0146fc70,
		0x014d03c6, 0xfb10ff32, 0x0701fb41, 0xfb3709a1,
		0xfe00f644, 0x0a000345, 0xf2350708, 0x09b2f104,
		0x01050eff, 0xf3baf9be, 0x110d0000,
	}, {	// 15.6 MHz
		0x00000003, 0xfff9fffb, 0x0022ffe6, 0xffc9007a,
		0xfff0ff29, 0x00f2007e, 0xfe01011b, 0x01f6fc9e,
		0x00440467, 0xfbccfdde, 0x0738fc90, 0xf9f70934,
		0xff99f582, 0x090204b0, 0xf21a05e1, 0x0a8df15a,
		0x00340f41, 0xf405f98b, 0x110d0000,
	}, {	// 15.7 MHz
		0x00000003, 0xfffcfff4, 0x0020fffa, 0xffb40064,
		0x002fff11, 0x00a400f0, 0xfe0d006e, 0x0281fd09,
		0xff3604c9, 0xfcbffca2, 0x0726fdfe, 0xf8e80888,
		0x0134f4f3, 0x07e1060c, 0xf22304af, 0x0b59f1be,
		0xff640f7d, 0xf452f959, 0x110d0000,
	}, {	// 15.8 MHz
		0x00000003, 0x0000fff0, 0x001a0010, 0xffaa0041,
		0x0067ff13, 0x0043014a, 0xfe46ffb9, 0x02dbfda8,
		0xfe3504e5, 0xfddcfb8d, 0x06c9ff7e, 0xf81107a2,
		0x02c9f49a, 0x069f0753, 0xf2500373, 0x0c14f231,
		0xfe930fb3, 0xf4a1f927, 0x110d0000,
	}, {	// 15.9 MHz
		0xffff0002, 0x0003ffee, 0x000f0023, 0xffac0016,
		0x0093ff31, 0xffdc0184, 0xfea6ff09, 0x02fdfe70,
		0xfd5104ba, 0xff15faac, 0x06270103, 0xf7780688,
		0x044df479, 0x05430883, 0xf2a00231, 0x0cbef2b2,
		0xfdc40fe3, 0xf4f2f8f5, 0x110d0000,
	}, {	// 16.0 MHz
		0xffff0001, 0x0006ffef, 0x00020031, 0xffbaffe8,
		0x00adff66, 0xff790198, 0xff26fe6e, 0x02e5ff55,
		0xfc99044a, 0x005bfa09, 0x0545027f, 0xf7230541,
		0x05b8f490, 0x03d20997, 0xf31300eb, 0x0d55f341,
		0xfcf6100e, 0xf544f8c3, 0x110d0000,
	}
};

static void cx23885_dif_setup(struct i2c_client *client, u32 ifHz)
{
	u64 pll_freq;
	u32 pll_freq_word;
	const u32 *coeffs;

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

	coeffs = ifhz_coeffs[(ifHz - 3000000) / 100000];
	cx25840_write4(client, DIF_BPF_COEFF01, coeffs[0]);
	cx25840_write4(client, DIF_BPF_COEFF23, coeffs[1]);
	cx25840_write4(client, DIF_BPF_COEFF45, coeffs[2]);
	cx25840_write4(client, DIF_BPF_COEFF67, coeffs[3]);
	cx25840_write4(client, DIF_BPF_COEFF89, coeffs[4]);
	cx25840_write4(client, DIF_BPF_COEFF1011, coeffs[5]);
	cx25840_write4(client, DIF_BPF_COEFF1213, coeffs[6]);
	cx25840_write4(client, DIF_BPF_COEFF1415, coeffs[7]);
	cx25840_write4(client, DIF_BPF_COEFF1617, coeffs[8]);
	cx25840_write4(client, DIF_BPF_COEFF1819, coeffs[9]);
	cx25840_write4(client, DIF_BPF_COEFF2021, coeffs[10]);
	cx25840_write4(client, DIF_BPF_COEFF2223, coeffs[11]);
	cx25840_write4(client, DIF_BPF_COEFF2425, coeffs[12]);
	cx25840_write4(client, DIF_BPF_COEFF2627, coeffs[13]);
	cx25840_write4(client, DIF_BPF_COEFF2829, coeffs[14]);
	cx25840_write4(client, DIF_BPF_COEFF3031, coeffs[15]);
	cx25840_write4(client, DIF_BPF_COEFF3233, coeffs[16]);
	cx25840_write4(client, DIF_BPF_COEFF3435, coeffs[17]);
	cx25840_write4(client, DIF_BPF_COEFF36, coeffs[18]);
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

static int cx25840_probe(struct i2c_client *client)
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

static void cx25840_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct cx25840_state *state = to_state(sd);

	cx25840_ir_remove(sd);
	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&state->hdl);
}

static const struct i2c_device_id cx25840_id[] = {
	{ "cx25840" },
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
