/*
    I2C functions
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>
    Copyright (C) 2005-2007  Hans Verkuil <hverkuil@xs4all.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
    This file includes an i2c implementation that was reverse engineered
    from the Hauppauge windows driver.  Older ivtv versions used i2c-algo-bit,
    which whilst fine under most circumstances, had trouble with the Zilog
    CPU on the PVR-150 which handles IR functions (occasional inability to
    communicate with the chip until it was reset) and also with the i2c
    bus being completely unreachable when multiple PVR cards were present.

    The implementation is very similar to i2c-algo-bit, but there are enough
    subtle differences that the two are hard to merge.  The general strategy
    employed by i2c-algo-bit is to use udelay() to implement the timing
    when putting out bits on the scl/sda lines.  The general strategy taken
    here is to poll the lines for state changes (see ivtv_waitscl and
    ivtv_waitsda).  In addition there are small delays at various locations
    which poll the SCL line 5 times (ivtv_scldelay).  I would guess that
    since this is memory mapped I/O that the length of those delays is tied
    to the PCI bus clock.  There is some extra code to do with recovery
    and retries.  Since it is not known what causes the actual i2c problems
    in the first place, the only goal if one was to attempt to use
    i2c-algo-bit would be to try to make it follow the same code path.
    This would be a lot of work, and I'm also not convinced that it would
    provide a generic benefit to i2c-algo-bit.  Therefore consider this
    an engineering solution -- not pretty, but it works.

    Some more general comments about what we are doing:

    The i2c bus is a 2 wire serial bus, with clock (SCL) and data (SDA)
    lines.  To communicate on the bus (as a master, we don't act as a slave),
    we first initiate a start condition (ivtv_start).  We then write the
    address of the device that we want to communicate with, along with a flag
    that indicates whether this is a read or a write.  The slave then issues
    an ACK signal (ivtv_ack), which tells us that it is ready for reading /
    writing.  We then proceed with reading or writing (ivtv_read/ivtv_write),
    and finally issue a stop condition (ivtv_stop) to make the bus available
    to other masters.

    There is an additional form of transaction where a write may be
    immediately followed by a read.  In this case, there is no intervening
    stop condition.  (Only the msp3400 chip uses this method of data transfer).
 */

#include "ivtv-driver.h"
#include "ivtv-cards.h"
#include "ivtv-gpio.h"
#include "ivtv-i2c.h"
#include <media/drv-intf/cx25840.h>

/* i2c implementation for cx23415/6 chip, ivtv project.
 * Author: Kevin Thayer (nufan_wfk at yahoo.com)
 */
/* i2c stuff */
#define IVTV_REG_I2C_SETSCL_OFFSET 0x7000
#define IVTV_REG_I2C_SETSDA_OFFSET 0x7004
#define IVTV_REG_I2C_GETSCL_OFFSET 0x7008
#define IVTV_REG_I2C_GETSDA_OFFSET 0x700c

#define IVTV_CS53L32A_I2C_ADDR		0x11
#define IVTV_M52790_I2C_ADDR		0x48
#define IVTV_CX25840_I2C_ADDR		0x44
#define IVTV_SAA7115_I2C_ADDR		0x21
#define IVTV_SAA7127_I2C_ADDR		0x44
#define IVTV_SAA717x_I2C_ADDR		0x21
#define IVTV_MSP3400_I2C_ADDR		0x40
#define IVTV_HAUPPAUGE_I2C_ADDR		0x50
#define IVTV_WM8739_I2C_ADDR		0x1a
#define IVTV_WM8775_I2C_ADDR		0x1b
#define IVTV_TEA5767_I2C_ADDR		0x60
#define IVTV_UPD64031A_I2C_ADDR		0x12
#define IVTV_UPD64083_I2C_ADDR		0x5c
#define IVTV_VP27SMPX_I2C_ADDR		0x5b
#define IVTV_M52790_I2C_ADDR		0x48
#define IVTV_AVERMEDIA_IR_RX_I2C_ADDR	0x40
#define IVTV_HAUP_EXT_IR_RX_I2C_ADDR	0x1a
#define IVTV_HAUP_INT_IR_RX_I2C_ADDR	0x18
#define IVTV_Z8F0811_IR_TX_I2C_ADDR	0x70
#define IVTV_Z8F0811_IR_RX_I2C_ADDR	0x71
#define IVTV_ADAPTEC_IR_ADDR		0x6b

/* This array should match the IVTV_HW_ defines */
static const u8 hw_addrs[] = {
	IVTV_CX25840_I2C_ADDR,
	IVTV_SAA7115_I2C_ADDR,
	IVTV_SAA7127_I2C_ADDR,
	IVTV_MSP3400_I2C_ADDR,
	0,
	IVTV_WM8775_I2C_ADDR,
	IVTV_CS53L32A_I2C_ADDR,
	0,
	IVTV_SAA7115_I2C_ADDR,
	IVTV_UPD64031A_I2C_ADDR,
	IVTV_UPD64083_I2C_ADDR,
	IVTV_SAA717x_I2C_ADDR,
	IVTV_WM8739_I2C_ADDR,
	IVTV_VP27SMPX_I2C_ADDR,
	IVTV_M52790_I2C_ADDR,
	0,				/* IVTV_HW_GPIO dummy driver ID */
	IVTV_AVERMEDIA_IR_RX_I2C_ADDR,	/* IVTV_HW_I2C_IR_RX_AVER */
	IVTV_HAUP_EXT_IR_RX_I2C_ADDR,	/* IVTV_HW_I2C_IR_RX_HAUP_EXT */
	IVTV_HAUP_INT_IR_RX_I2C_ADDR,	/* IVTV_HW_I2C_IR_RX_HAUP_INT */
	IVTV_Z8F0811_IR_RX_I2C_ADDR,	/* IVTV_HW_Z8F0811_IR_HAUP */
	IVTV_ADAPTEC_IR_ADDR,		/* IVTV_HW_I2C_IR_RX_ADAPTEC */
};

/* This array should match the IVTV_HW_ defines */
static const char * const hw_devicenames[] = {
	"cx25840",
	"saa7115",
	"saa7127_auto",	/* saa7127 or saa7129 */
	"msp3400",
	"tuner",
	"wm8775",
	"cs53l32a",
	"tveeprom",
	"saa7114",
	"upd64031a",
	"upd64083",
	"saa717x",
	"wm8739",
	"vp27smpx",
	"m52790",
	"gpio",
	"ir_video",		/* IVTV_HW_I2C_IR_RX_AVER */
	"ir_video",		/* IVTV_HW_I2C_IR_RX_HAUP_EXT */
	"ir_video",		/* IVTV_HW_I2C_IR_RX_HAUP_INT */
	"ir_z8f0811_haup",	/* IVTV_HW_Z8F0811_IR_HAUP */
	"ir_video",		/* IVTV_HW_I2C_IR_RX_ADAPTEC */
};

static int get_key_adaptec(struct IR_i2c *ir, enum rc_proto *protocol,
			   u32 *scancode, u8 *toggle)
{
	unsigned char keybuf[4];

	keybuf[0] = 0x00;
	i2c_master_send(ir->c, keybuf, 1);
	/* poll IR chip */
	if (i2c_master_recv(ir->c, keybuf, sizeof(keybuf)) != sizeof(keybuf)) {
		return 0;
	}

	/* key pressed ? */
	if (keybuf[2] == 0xff)
		return 0;

	/* remove repeat bit */
	keybuf[2] &= 0x7f;
	keybuf[3] |= 0x80;

	*protocol = RC_PROTO_UNKNOWN;
	*scancode = keybuf[3] | keybuf[2] << 8 | keybuf[1] << 16 |keybuf[0] << 24;
	*toggle = 0;
	return 1;
}

static int ivtv_i2c_new_ir(struct ivtv *itv, u32 hw, const char *type, u8 addr)
{
	struct i2c_board_info info;
	struct i2c_adapter *adap = &itv->i2c_adap;
	struct IR_i2c_init_data *init_data = &itv->ir_i2c_init_data;
	unsigned short addr_list[2] = { addr, I2C_CLIENT_END };

	/* Only allow one IR receiver to be registered per board */
	if (itv->hw_flags & IVTV_HW_IR_ANY)
		return -1;

	/* Our default information for ir-kbd-i2c.c to use */
	switch (hw) {
	case IVTV_HW_I2C_IR_RX_AVER:
		init_data->ir_codes = RC_MAP_AVERMEDIA_CARDBUS;
		init_data->internal_get_key_func =
					IR_KBD_GET_KEY_AVERMEDIA_CARDBUS;
		init_data->type = RC_PROTO_BIT_OTHER;
		init_data->name = "AVerMedia AVerTV card";
		break;
	case IVTV_HW_I2C_IR_RX_HAUP_EXT:
	case IVTV_HW_I2C_IR_RX_HAUP_INT:
		init_data->ir_codes = RC_MAP_HAUPPAUGE;
		init_data->internal_get_key_func = IR_KBD_GET_KEY_HAUP;
		init_data->type = RC_PROTO_BIT_RC5;
		init_data->name = itv->card_name;
		break;
	case IVTV_HW_Z8F0811_IR_HAUP:
		/* Default to grey remote */
		init_data->ir_codes = RC_MAP_HAUPPAUGE;
		init_data->internal_get_key_func = IR_KBD_GET_KEY_HAUP_XVR;
		init_data->type = RC_PROTO_BIT_RC5 | RC_PROTO_BIT_RC6_MCE |
							RC_PROTO_BIT_RC6_6A_32;
		init_data->name = itv->card_name;
		break;
	case IVTV_HW_I2C_IR_RX_ADAPTEC:
		init_data->get_key = get_key_adaptec;
		init_data->name = itv->card_name;
		/* FIXME: The protocol and RC_MAP needs to be corrected */
		init_data->ir_codes = RC_MAP_EMPTY;
		init_data->type = RC_PROTO_BIT_UNKNOWN;
		break;
	}

	memset(&info, 0, sizeof(struct i2c_board_info));
	info.platform_data = init_data;
	strlcpy(info.type, type, I2C_NAME_SIZE);

	return i2c_new_probed_device(adap, &info, addr_list, NULL) == NULL ?
	       -1 : 0;
}

/* Instantiate the IR receiver device using probing -- undesirable */
struct i2c_client *ivtv_i2c_new_ir_legacy(struct ivtv *itv)
{
	struct i2c_board_info info;
	/*
	 * The external IR receiver is at i2c address 0x34.
	 * The internal IR receiver is at i2c address 0x30.
	 *
	 * In theory, both can be fitted, and Hauppauge suggests an external
	 * overrides an internal.  That's why we probe 0x1a (~0x34) first. CB
	 *
	 * Some of these addresses we probe may collide with other i2c address
	 * allocations, so this function must be called after all other i2c
	 * devices we care about are registered.
	 */
	const unsigned short addr_list[] = {
		0x1a,	/* Hauppauge IR external - collides with WM8739 */
		0x18,	/* Hauppauge IR internal */
		I2C_CLIENT_END
	};

	memset(&info, 0, sizeof(struct i2c_board_info));
	strlcpy(info.type, "ir_video", I2C_NAME_SIZE);
	return i2c_new_probed_device(&itv->i2c_adap, &info, addr_list, NULL);
}

int ivtv_i2c_register(struct ivtv *itv, unsigned idx)
{
	struct v4l2_subdev *sd;
	struct i2c_adapter *adap = &itv->i2c_adap;
	const char *type = hw_devicenames[idx];
	u32 hw = 1 << idx;

	if (hw == IVTV_HW_TUNER) {
		/* special tuner handling */
		sd = v4l2_i2c_new_subdev(&itv->v4l2_dev, adap, type, 0,
				itv->card_i2c->radio);
		if (sd)
			sd->grp_id = 1 << idx;
		sd = v4l2_i2c_new_subdev(&itv->v4l2_dev, adap, type, 0,
				itv->card_i2c->demod);
		if (sd)
			sd->grp_id = 1 << idx;
		sd = v4l2_i2c_new_subdev(&itv->v4l2_dev, adap, type, 0,
				itv->card_i2c->tv);
		if (sd)
			sd->grp_id = 1 << idx;
		return sd ? 0 : -1;
	}

	if (hw & IVTV_HW_IR_ANY)
		return ivtv_i2c_new_ir(itv, hw, type, hw_addrs[idx]);

	/* Is it not an I2C device or one we do not wish to register? */
	if (!hw_addrs[idx])
		return -1;

	/* It's an I2C device other than an analog tuner or IR chip */
	if (hw == IVTV_HW_UPD64031A || hw == IVTV_HW_UPD6408X) {
		sd = v4l2_i2c_new_subdev(&itv->v4l2_dev,
				adap, type, 0, I2C_ADDRS(hw_addrs[idx]));
	} else if (hw == IVTV_HW_CX25840) {
		struct cx25840_platform_data pdata;
		struct i2c_board_info cx25840_info = {
			.type = "cx25840",
			.addr = hw_addrs[idx],
			.platform_data = &pdata,
		};

		pdata.pvr150_workaround = itv->pvr150_workaround;
		sd = v4l2_i2c_new_subdev_board(&itv->v4l2_dev, adap,
				&cx25840_info, NULL);
	} else {
		sd = v4l2_i2c_new_subdev(&itv->v4l2_dev,
				adap, type, hw_addrs[idx], NULL);
	}
	if (sd)
		sd->grp_id = 1 << idx;
	return sd ? 0 : -1;
}

struct v4l2_subdev *ivtv_find_hw(struct ivtv *itv, u32 hw)
{
	struct v4l2_subdev *result = NULL;
	struct v4l2_subdev *sd;

	spin_lock(&itv->v4l2_dev.lock);
	v4l2_device_for_each_subdev(sd, &itv->v4l2_dev) {
		if (sd->grp_id == hw) {
			result = sd;
			break;
		}
	}
	spin_unlock(&itv->v4l2_dev.lock);
	return result;
}

/* Set the serial clock line to the desired state */
static void ivtv_setscl(struct ivtv *itv, int state)
{
	/* write them out */
	/* write bits are inverted */
	write_reg(~state, IVTV_REG_I2C_SETSCL_OFFSET);
}

/* Set the serial data line to the desired state */
static void ivtv_setsda(struct ivtv *itv, int state)
{
	/* write them out */
	/* write bits are inverted */
	write_reg(~state & 1, IVTV_REG_I2C_SETSDA_OFFSET);
}

/* Read the serial clock line */
static int ivtv_getscl(struct ivtv *itv)
{
	return read_reg(IVTV_REG_I2C_GETSCL_OFFSET) & 1;
}

/* Read the serial data line */
static int ivtv_getsda(struct ivtv *itv)
{
	return read_reg(IVTV_REG_I2C_GETSDA_OFFSET) & 1;
}

/* Implement a short delay by polling the serial clock line */
static void ivtv_scldelay(struct ivtv *itv)
{
	int i;

	for (i = 0; i < 5; ++i)
		ivtv_getscl(itv);
}

/* Wait for the serial clock line to become set to a specific value */
static int ivtv_waitscl(struct ivtv *itv, int val)
{
	int i;

	ivtv_scldelay(itv);
	for (i = 0; i < 1000; ++i) {
		if (ivtv_getscl(itv) == val)
			return 1;
	}
	return 0;
}

/* Wait for the serial data line to become set to a specific value */
static int ivtv_waitsda(struct ivtv *itv, int val)
{
	int i;

	ivtv_scldelay(itv);
	for (i = 0; i < 1000; ++i) {
		if (ivtv_getsda(itv) == val)
			return 1;
	}
	return 0;
}

/* Wait for the slave to issue an ACK */
static int ivtv_ack(struct ivtv *itv)
{
	int ret = 0;

	if (ivtv_getscl(itv) == 1) {
		IVTV_DEBUG_HI_I2C("SCL was high starting an ack\n");
		ivtv_setscl(itv, 0);
		if (!ivtv_waitscl(itv, 0)) {
			IVTV_DEBUG_I2C("Could not set SCL low starting an ack\n");
			return -EREMOTEIO;
		}
	}
	ivtv_setsda(itv, 1);
	ivtv_scldelay(itv);
	ivtv_setscl(itv, 1);
	if (!ivtv_waitsda(itv, 0)) {
		IVTV_DEBUG_I2C("Slave did not ack\n");
		ret = -EREMOTEIO;
	}
	ivtv_setscl(itv, 0);
	if (!ivtv_waitscl(itv, 0)) {
		IVTV_DEBUG_I2C("Failed to set SCL low after ACK\n");
		ret = -EREMOTEIO;
	}
	return ret;
}

/* Write a single byte to the i2c bus and wait for the slave to ACK */
static int ivtv_sendbyte(struct ivtv *itv, unsigned char byte)
{
	int i, bit;

	IVTV_DEBUG_HI_I2C("write %x\n",byte);
	for (i = 0; i < 8; ++i, byte<<=1) {
		ivtv_setscl(itv, 0);
		if (!ivtv_waitscl(itv, 0)) {
			IVTV_DEBUG_I2C("Error setting SCL low\n");
			return -EREMOTEIO;
		}
		bit = (byte>>7)&1;
		ivtv_setsda(itv, bit);
		if (!ivtv_waitsda(itv, bit)) {
			IVTV_DEBUG_I2C("Error setting SDA\n");
			return -EREMOTEIO;
		}
		ivtv_setscl(itv, 1);
		if (!ivtv_waitscl(itv, 1)) {
			IVTV_DEBUG_I2C("Slave not ready for bit\n");
			return -EREMOTEIO;
		}
	}
	ivtv_setscl(itv, 0);
	if (!ivtv_waitscl(itv, 0)) {
		IVTV_DEBUG_I2C("Error setting SCL low\n");
		return -EREMOTEIO;
	}
	return ivtv_ack(itv);
}

/* Read a byte from the i2c bus and send a NACK if applicable (i.e. for the
   final byte) */
static int ivtv_readbyte(struct ivtv *itv, unsigned char *byte, int nack)
{
	int i;

	*byte = 0;

	ivtv_setsda(itv, 1);
	ivtv_scldelay(itv);
	for (i = 0; i < 8; ++i) {
		ivtv_setscl(itv, 0);
		ivtv_scldelay(itv);
		ivtv_setscl(itv, 1);
		if (!ivtv_waitscl(itv, 1)) {
			IVTV_DEBUG_I2C("Error setting SCL high\n");
			return -EREMOTEIO;
		}
		*byte = ((*byte)<<1)|ivtv_getsda(itv);
	}
	ivtv_setscl(itv, 0);
	ivtv_scldelay(itv);
	ivtv_setsda(itv, nack);
	ivtv_scldelay(itv);
	ivtv_setscl(itv, 1);
	ivtv_scldelay(itv);
	ivtv_setscl(itv, 0);
	ivtv_scldelay(itv);
	IVTV_DEBUG_HI_I2C("read %x\n",*byte);
	return 0;
}

/* Issue a start condition on the i2c bus to alert slaves to prepare for
   an address write */
static int ivtv_start(struct ivtv *itv)
{
	int sda;

	sda = ivtv_getsda(itv);
	if (sda != 1) {
		IVTV_DEBUG_HI_I2C("SDA was low at start\n");
		ivtv_setsda(itv, 1);
		if (!ivtv_waitsda(itv, 1)) {
			IVTV_DEBUG_I2C("SDA stuck low\n");
			return -EREMOTEIO;
		}
	}
	if (ivtv_getscl(itv) != 1) {
		ivtv_setscl(itv, 1);
		if (!ivtv_waitscl(itv, 1)) {
			IVTV_DEBUG_I2C("SCL stuck low at start\n");
			return -EREMOTEIO;
		}
	}
	ivtv_setsda(itv, 0);
	ivtv_scldelay(itv);
	return 0;
}

/* Issue a stop condition on the i2c bus to release it */
static int ivtv_stop(struct ivtv *itv)
{
	int i;

	if (ivtv_getscl(itv) != 0) {
		IVTV_DEBUG_HI_I2C("SCL not low when stopping\n");
		ivtv_setscl(itv, 0);
		if (!ivtv_waitscl(itv, 0)) {
			IVTV_DEBUG_I2C("SCL could not be set low\n");
		}
	}
	ivtv_setsda(itv, 0);
	ivtv_scldelay(itv);
	ivtv_setscl(itv, 1);
	if (!ivtv_waitscl(itv, 1)) {
		IVTV_DEBUG_I2C("SCL could not be set high\n");
		return -EREMOTEIO;
	}
	ivtv_scldelay(itv);
	ivtv_setsda(itv, 1);
	if (!ivtv_waitsda(itv, 1)) {
		IVTV_DEBUG_I2C("resetting I2C\n");
		for (i = 0; i < 16; ++i) {
			ivtv_setscl(itv, 0);
			ivtv_scldelay(itv);
			ivtv_setscl(itv, 1);
			ivtv_scldelay(itv);
			ivtv_setsda(itv, 1);
		}
		ivtv_waitsda(itv, 1);
		return -EREMOTEIO;
	}
	return 0;
}

/* Write a message to the given i2c slave.  do_stop may be 0 to prevent
   issuing the i2c stop condition (when following with a read) */
static int ivtv_write(struct ivtv *itv, unsigned char addr, unsigned char *data, u32 len, int do_stop)
{
	int retry, ret = -EREMOTEIO;
	u32 i;

	for (retry = 0; ret != 0 && retry < 8; ++retry) {
		ret = ivtv_start(itv);

		if (ret == 0) {
			ret = ivtv_sendbyte(itv, addr<<1);
			for (i = 0; ret == 0 && i < len; ++i)
				ret = ivtv_sendbyte(itv, data[i]);
		}
		if (ret != 0 || do_stop) {
			ivtv_stop(itv);
		}
	}
	if (ret)
		IVTV_DEBUG_I2C("i2c write to %x failed\n", addr);
	return ret;
}

/* Read data from the given i2c slave.  A stop condition is always issued. */
static int ivtv_read(struct ivtv *itv, unsigned char addr, unsigned char *data, u32 len)
{
	int retry, ret = -EREMOTEIO;
	u32 i;

	for (retry = 0; ret != 0 && retry < 8; ++retry) {
		ret = ivtv_start(itv);
		if (ret == 0)
			ret = ivtv_sendbyte(itv, (addr << 1) | 1);
		for (i = 0; ret == 0 && i < len; ++i) {
			ret = ivtv_readbyte(itv, &data[i], i == len - 1);
		}
		ivtv_stop(itv);
	}
	if (ret)
		IVTV_DEBUG_I2C("i2c read from %x failed\n", addr);
	return ret;
}

/* Kernel i2c transfer implementation.  Takes a number of messages to be read
   or written.  If a read follows a write, this will occur without an
   intervening stop condition */
static int ivtv_xfer(struct i2c_adapter *i2c_adap, struct i2c_msg *msgs, int num)
{
	struct v4l2_device *v4l2_dev = i2c_get_adapdata(i2c_adap);
	struct ivtv *itv = to_ivtv(v4l2_dev);
	int retval;
	int i;

	mutex_lock(&itv->i2c_bus_lock);
	for (i = retval = 0; retval == 0 && i < num; i++) {
		if (msgs[i].flags & I2C_M_RD)
			retval = ivtv_read(itv, msgs[i].addr, msgs[i].buf, msgs[i].len);
		else {
			/* if followed by a read, don't stop */
			int stop = !(i + 1 < num && msgs[i + 1].flags == I2C_M_RD);

			retval = ivtv_write(itv, msgs[i].addr, msgs[i].buf, msgs[i].len, stop);
		}
	}
	mutex_unlock(&itv->i2c_bus_lock);
	return retval ? retval : num;
}

/* Kernel i2c capabilities */
static u32 ivtv_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm ivtv_algo = {
	.master_xfer   = ivtv_xfer,
	.functionality = ivtv_functionality,
};

/* template for our-bit banger */
static const struct i2c_adapter ivtv_i2c_adap_hw_template = {
	.name = "ivtv i2c driver",
	.algo = &ivtv_algo,
	.algo_data = NULL,			/* filled from template */
	.owner = THIS_MODULE,
};

static void ivtv_setscl_old(void *data, int state)
{
	struct ivtv *itv = (struct ivtv *)data;

	if (state)
		itv->i2c_state |= 0x01;
	else
		itv->i2c_state &= ~0x01;

	/* write them out */
	/* write bits are inverted */
	write_reg(~itv->i2c_state, IVTV_REG_I2C_SETSCL_OFFSET);
}

static void ivtv_setsda_old(void *data, int state)
{
	struct ivtv *itv = (struct ivtv *)data;

	if (state)
		itv->i2c_state |= 0x01;
	else
		itv->i2c_state &= ~0x01;

	/* write them out */
	/* write bits are inverted */
	write_reg(~itv->i2c_state, IVTV_REG_I2C_SETSDA_OFFSET);
}

static int ivtv_getscl_old(void *data)
{
	struct ivtv *itv = (struct ivtv *)data;

	return read_reg(IVTV_REG_I2C_GETSCL_OFFSET) & 1;
}

static int ivtv_getsda_old(void *data)
{
	struct ivtv *itv = (struct ivtv *)data;

	return read_reg(IVTV_REG_I2C_GETSDA_OFFSET) & 1;
}

/* template for i2c-bit-algo */
static const struct i2c_adapter ivtv_i2c_adap_template = {
	.name = "ivtv i2c driver",
	.algo = NULL,                   /* set by i2c-algo-bit */
	.algo_data = NULL,              /* filled from template */
	.owner = THIS_MODULE,
};

#define IVTV_ALGO_BIT_TIMEOUT	(2)	/* seconds */

static const struct i2c_algo_bit_data ivtv_i2c_algo_template = {
	.setsda		= ivtv_setsda_old,
	.setscl		= ivtv_setscl_old,
	.getsda		= ivtv_getsda_old,
	.getscl		= ivtv_getscl_old,
	.udelay		= IVTV_DEFAULT_I2C_CLOCK_PERIOD / 2,  /* microseconds */
	.timeout	= IVTV_ALGO_BIT_TIMEOUT * HZ,         /* jiffies */
};

static const struct i2c_client ivtv_i2c_client_template = {
	.name = "ivtv internal",
};

/* init + register i2c adapter */
int init_ivtv_i2c(struct ivtv *itv)
{
	int retval;

	IVTV_DEBUG_I2C("i2c init\n");

	/* Sanity checks for the I2C hardware arrays. They must be the
	 * same size.
	 */
	if (ARRAY_SIZE(hw_devicenames) != ARRAY_SIZE(hw_addrs)) {
		IVTV_ERR("Mismatched I2C hardware arrays\n");
		return -ENODEV;
	}
	if (itv->options.newi2c > 0) {
		itv->i2c_adap = ivtv_i2c_adap_hw_template;
	} else {
		itv->i2c_adap = ivtv_i2c_adap_template;
		itv->i2c_algo = ivtv_i2c_algo_template;
	}
	itv->i2c_algo.udelay = itv->options.i2c_clock_period / 2;
	itv->i2c_algo.data = itv;
	itv->i2c_adap.algo_data = &itv->i2c_algo;

	sprintf(itv->i2c_adap.name + strlen(itv->i2c_adap.name), " #%d",
		itv->instance);
	i2c_set_adapdata(&itv->i2c_adap, &itv->v4l2_dev);

	itv->i2c_client = ivtv_i2c_client_template;
	itv->i2c_client.adapter = &itv->i2c_adap;
	itv->i2c_adap.dev.parent = &itv->pdev->dev;

	IVTV_DEBUG_I2C("setting scl and sda to 1\n");
	ivtv_setscl(itv, 1);
	ivtv_setsda(itv, 1);

	if (itv->options.newi2c > 0)
		retval = i2c_add_adapter(&itv->i2c_adap);
	else
		retval = i2c_bit_add_bus(&itv->i2c_adap);

	return retval;
}

void exit_ivtv_i2c(struct ivtv *itv)
{
	IVTV_DEBUG_I2C("i2c exit\n");

	i2c_del_adapter(&itv->i2c_adap);
}
