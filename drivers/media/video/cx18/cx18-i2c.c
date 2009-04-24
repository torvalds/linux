/*
 *  cx18 I2C functions
 *
 *  Derived from ivtv-i2c.c
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *  Copyright (C) 2008  Andy Walls <awalls@radix.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA
 */

#include "cx18-driver.h"
#include "cx18-io.h"
#include "cx18-cards.h"
#include "cx18-gpio.h"
#include "cx18-i2c.h"
#include "cx18-irq.h"

#define CX18_REG_I2C_1_WR   0xf15000
#define CX18_REG_I2C_1_RD   0xf15008
#define CX18_REG_I2C_2_WR   0xf25100
#define CX18_REG_I2C_2_RD   0xf25108

#define SETSCL_BIT      0x0001
#define SETSDL_BIT      0x0002
#define GETSCL_BIT      0x0004
#define GETSDL_BIT      0x0008

#define CX18_CS5345_I2C_ADDR		0x4c

/* This array should match the CX18_HW_ defines */
static const u8 hw_addrs[] = {
	0,			/* CX18_HW_TUNER */
	0,			/* CX18_HW_TVEEPROM */
	CX18_CS5345_I2C_ADDR,	/* CX18_HW_CS5345 */
	0,			/* CX18_HW_DVB */
	0,			/* CX18_HW_418_AV */
	0,			/* CX18_HW_GPIO_MUX */
	0,			/* CX18_HW_GPIO_RESET_CTRL */
};

/* This array should match the CX18_HW_ defines */
/* This might well become a card-specific array */
static const u8 hw_bus[] = {
	1,	/* CX18_HW_TUNER */
	0,	/* CX18_HW_TVEEPROM */
	0,	/* CX18_HW_CS5345 */
	0,	/* CX18_HW_DVB */
	0,	/* CX18_HW_418_AV */
	0,	/* CX18_HW_GPIO_MUX */
	0,	/* CX18_HW_GPIO_RESET_CTRL */
};

/* This array should match the CX18_HW_ defines */
static const char * const hw_modules[] = {
	"tuner",	/* CX18_HW_TUNER */
	NULL,		/* CX18_HW_TVEEPROM */
	"cs5345",	/* CX18_HW_CS5345 */
	NULL,		/* CX18_HW_DVB */
	NULL,		/* CX18_HW_418_AV */
	NULL,		/* CX18_HW_GPIO_MUX */
	NULL,		/* CX18_HW_GPIO_RESET_CTRL */
};

/* This array should match the CX18_HW_ defines */
static const char * const hw_devicenames[] = {
	"tuner",
	"tveeprom",
	"cs5345",
	"cx23418_DTV",
	"cx23418_AV",
	"gpio_mux",
	"gpio_reset_ctrl",
};

int cx18_i2c_register(struct cx18 *cx, unsigned idx)
{
	struct v4l2_subdev *sd;
	int bus = hw_bus[idx];
	struct i2c_adapter *adap = &cx->i2c_adap[bus];
	const char *mod = hw_modules[idx];
	const char *type = hw_devicenames[idx];
	u32 hw = 1 << idx;

	if (idx >= ARRAY_SIZE(hw_addrs))
		return -1;

	if (hw == CX18_HW_TUNER) {
		/* special tuner group handling */
		sd = v4l2_i2c_new_probed_subdev(&cx->v4l2_dev,
				adap, mod, type, cx->card_i2c->radio);
		if (sd != NULL)
			sd->grp_id = hw;
		sd = v4l2_i2c_new_probed_subdev(&cx->v4l2_dev,
				adap, mod, type, cx->card_i2c->demod);
		if (sd != NULL)
			sd->grp_id = hw;
		sd = v4l2_i2c_new_probed_subdev(&cx->v4l2_dev,
				adap, mod, type, cx->card_i2c->tv);
		if (sd != NULL)
			sd->grp_id = hw;
		return sd != NULL ? 0 : -1;
	}

	/* Is it not an I2C device or one we do not wish to register? */
	if (!hw_addrs[idx])
		return -1;

	/* It's an I2C device other than an analog tuner */
	sd = v4l2_i2c_new_subdev(&cx->v4l2_dev, adap, mod, type, hw_addrs[idx]);
	if (sd != NULL)
		sd->grp_id = hw;
	return sd != NULL ? 0 : -1;
}

/* Find the first member of the subdev group id in hw */
struct v4l2_subdev *cx18_find_hw(struct cx18 *cx, u32 hw)
{
	struct v4l2_subdev *result = NULL;
	struct v4l2_subdev *sd;

	spin_lock(&cx->v4l2_dev.lock);
	v4l2_device_for_each_subdev(sd, &cx->v4l2_dev) {
		if (sd->grp_id == hw) {
			result = sd;
			break;
		}
	}
	spin_unlock(&cx->v4l2_dev.lock);
	return result;
}

static void cx18_setscl(void *data, int state)
{
	struct cx18 *cx = ((struct cx18_i2c_algo_callback_data *)data)->cx;
	int bus_index = ((struct cx18_i2c_algo_callback_data *)data)->bus_index;
	u32 addr = bus_index ? CX18_REG_I2C_2_WR : CX18_REG_I2C_1_WR;
	u32 r = cx18_read_reg(cx, addr);

	if (state)
		cx18_write_reg(cx, r | SETSCL_BIT, addr);
	else
		cx18_write_reg(cx, r & ~SETSCL_BIT, addr);
}

static void cx18_setsda(void *data, int state)
{
	struct cx18 *cx = ((struct cx18_i2c_algo_callback_data *)data)->cx;
	int bus_index = ((struct cx18_i2c_algo_callback_data *)data)->bus_index;
	u32 addr = bus_index ? CX18_REG_I2C_2_WR : CX18_REG_I2C_1_WR;
	u32 r = cx18_read_reg(cx, addr);

	if (state)
		cx18_write_reg(cx, r | SETSDL_BIT, addr);
	else
		cx18_write_reg(cx, r & ~SETSDL_BIT, addr);
}

static int cx18_getscl(void *data)
{
	struct cx18 *cx = ((struct cx18_i2c_algo_callback_data *)data)->cx;
	int bus_index = ((struct cx18_i2c_algo_callback_data *)data)->bus_index;
	u32 addr = bus_index ? CX18_REG_I2C_2_RD : CX18_REG_I2C_1_RD;

	return cx18_read_reg(cx, addr) & GETSCL_BIT;
}

static int cx18_getsda(void *data)
{
	struct cx18 *cx = ((struct cx18_i2c_algo_callback_data *)data)->cx;
	int bus_index = ((struct cx18_i2c_algo_callback_data *)data)->bus_index;
	u32 addr = bus_index ? CX18_REG_I2C_2_RD : CX18_REG_I2C_1_RD;

	return cx18_read_reg(cx, addr) & GETSDL_BIT;
}

/* template for i2c-bit-algo */
static struct i2c_adapter cx18_i2c_adap_template = {
	.name = "cx18 i2c driver",
	.id = I2C_HW_B_CX2341X,
	.algo = NULL,                   /* set by i2c-algo-bit */
	.algo_data = NULL,              /* filled from template */
	.owner = THIS_MODULE,
};

#define CX18_SCL_PERIOD (10) /* usecs. 10 usec is period for a 100 KHz clock */
#define CX18_ALGO_BIT_TIMEOUT (2) /* seconds */

static struct i2c_algo_bit_data cx18_i2c_algo_template = {
	.setsda		= cx18_setsda,
	.setscl		= cx18_setscl,
	.getsda		= cx18_getsda,
	.getscl		= cx18_getscl,
	.udelay		= CX18_SCL_PERIOD/2,       /* 1/2 clock period in usec*/
	.timeout	= CX18_ALGO_BIT_TIMEOUT*HZ /* jiffies */
};

/* init + register i2c algo-bit adapter */
int init_cx18_i2c(struct cx18 *cx)
{
	int i;
	CX18_DEBUG_I2C("i2c init\n");

	for (i = 0; i < 2; i++) {
		/* Setup algorithm for adapter */
		memcpy(&cx->i2c_algo[i], &cx18_i2c_algo_template,
			sizeof(struct i2c_algo_bit_data));
		cx->i2c_algo_cb_data[i].cx = cx;
		cx->i2c_algo_cb_data[i].bus_index = i;
		cx->i2c_algo[i].data = &cx->i2c_algo_cb_data[i];

		/* Setup adapter */
		memcpy(&cx->i2c_adap[i], &cx18_i2c_adap_template,
			sizeof(struct i2c_adapter));
		cx->i2c_adap[i].algo_data = &cx->i2c_algo[i];
		sprintf(cx->i2c_adap[i].name + strlen(cx->i2c_adap[i].name),
				" #%d-%d", cx->instance, i);
		i2c_set_adapdata(&cx->i2c_adap[i], &cx->v4l2_dev);
		cx->i2c_adap[i].dev.parent = &cx->pci_dev->dev;
	}

	if (cx18_read_reg(cx, CX18_REG_I2C_2_WR) != 0x0003c02f) {
		/* Reset/Unreset I2C hardware block */
		/* Clock select 220MHz */
		cx18_write_reg_expect(cx, 0x10000000, 0xc71004,
					  0x00000000, 0x10001000);
		/* Clock Enable */
		cx18_write_reg_expect(cx, 0x10001000, 0xc71024,
					  0x00001000, 0x10001000);
	}
	/* courtesy of Steven Toth <stoth@hauppauge.com> */
	cx18_write_reg_expect(cx, 0x00c00000, 0xc7001c, 0x00000000, 0x00c000c0);
	mdelay(10);
	cx18_write_reg_expect(cx, 0x00c000c0, 0xc7001c, 0x000000c0, 0x00c000c0);
	mdelay(10);
	cx18_write_reg_expect(cx, 0x00c00000, 0xc7001c, 0x00000000, 0x00c000c0);
	mdelay(10);

	/* Set to edge-triggered intrs. */
	cx18_write_reg(cx, 0x00c00000, 0xc730c8);
	/* Clear any stale intrs */
	cx18_write_reg_expect(cx, HW2_I2C1_INT|HW2_I2C2_INT, HW2_INT_CLR_STATUS,
		       ~(HW2_I2C1_INT|HW2_I2C2_INT), HW2_I2C1_INT|HW2_I2C2_INT);

	/* Hw I2C1 Clock Freq ~100kHz */
	cx18_write_reg(cx, 0x00021c0f & ~4, CX18_REG_I2C_1_WR);
	cx18_setscl(&cx->i2c_algo_cb_data[0], 1);
	cx18_setsda(&cx->i2c_algo_cb_data[0], 1);

	/* Hw I2C2 Clock Freq ~100kHz */
	cx18_write_reg(cx, 0x00021c0f & ~4, CX18_REG_I2C_2_WR);
	cx18_setscl(&cx->i2c_algo_cb_data[1], 1);
	cx18_setsda(&cx->i2c_algo_cb_data[1], 1);

	cx18_call_hw(cx, CX18_HW_GPIO_RESET_CTRL,
		     core, reset, (u32) CX18_GPIO_RESET_I2C);

	return i2c_bit_add_bus(&cx->i2c_adap[0]) ||
		i2c_bit_add_bus(&cx->i2c_adap[1]);
}

void exit_cx18_i2c(struct cx18 *cx)
{
	int i;
	CX18_DEBUG_I2C("i2c exit\n");
	cx18_write_reg(cx, cx18_read_reg(cx, CX18_REG_I2C_1_WR) | 4,
							CX18_REG_I2C_1_WR);
	cx18_write_reg(cx, cx18_read_reg(cx, CX18_REG_I2C_2_WR) | 4,
							CX18_REG_I2C_2_WR);

	for (i = 0; i < 2; i++) {
		i2c_del_adapter(&cx->i2c_adap[i]);
	}
}

/*
   Hauppauge HVR1600 should have:
   32 cx24227
   98 unknown
   a0 eeprom
   c2 tuner
   e? zilog ir
   */
