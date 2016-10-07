/*
 * Linux driver for Technisat DVB-S/S2 USB 2.0 device
 *
 * Copyright (C) 2010 Patrick Boettcher,
 *                    Kernel Labs Inc. PO Box 745, St James, NY 11780
 *
 * Development was sponsored by Technisat Digital UK Limited, whose
 * registered office is Witan Gate House 500 - 600 Witan Gate West,
 * Milton Keynes, MK9 1SH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * THIS PROGRAM IS PROVIDED "AS IS" AND BOTH THE COPYRIGHT HOLDER AND
 * TECHNISAT DIGITAL UK LTD DISCLAIM ALL WARRANTIES WITH REGARD TO
 * THIS PROGRAM INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY OR
 * FITNESS FOR A PARTICULAR PURPOSE.  NEITHER THE COPYRIGHT HOLDER
 * NOR TECHNISAT DIGITAL UK LIMITED SHALL BE LIABLE FOR ANY SPECIAL,
 * DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR
 * IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS PROGRAM. See the
 * GNU General Public License for more details.
 */

#define DVB_USB_LOG_PREFIX "technisat-usb2"
#include "dvb-usb.h"

#include "stv6110x.h"
#include "stv090x.h"

/* module parameters */
DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug,
		"set debugging level (bit-mask: 1=info,2=eeprom,4=i2c,8=rc)." \
		DVB_USB_DEBUG_STATUS);

/* disables all LED control command and
 * also does not start the signal polling thread */
static int disable_led_control;
module_param(disable_led_control, int, 0444);
MODULE_PARM_DESC(disable_led_control,
		"disable LED control of the device "
		"(default: 0 - LED control is active).");

/* device private data */
struct technisat_usb2_state {
	struct dvb_usb_device *dev;
	struct delayed_work green_led_work;
	u8 power_state;

	u16 last_scan_code;

	u8 buf[64];
};

/* debug print helpers */
#define deb_info(args...)    dprintk(debug, 0x01, args)
#define deb_eeprom(args...)  dprintk(debug, 0x02, args)
#define deb_i2c(args...)     dprintk(debug, 0x04, args)
#define deb_rc(args...)      dprintk(debug, 0x08, args)

/* vendor requests */
#define SET_IFCLK_TO_EXTERNAL_TSCLK_VENDOR_REQUEST 0xB3
#define SET_FRONT_END_RESET_VENDOR_REQUEST         0xB4
#define GET_VERSION_INFO_VENDOR_REQUEST            0xB5
#define SET_GREEN_LED_VENDOR_REQUEST               0xB6
#define SET_RED_LED_VENDOR_REQUEST                 0xB7
#define GET_IR_DATA_VENDOR_REQUEST                 0xB8
#define SET_LED_TIMER_DIVIDER_VENDOR_REQUEST       0xB9
#define SET_USB_REENUMERATION                      0xBA

/* i2c-access methods */
#define I2C_SPEED_100KHZ_BIT 0x40

#define I2C_STATUS_NAK 7
#define I2C_STATUS_OK 8

static int technisat_usb2_i2c_access(struct usb_device *udev,
		u8 device_addr, u8 *tx, u8 txlen, u8 *rx, u8 rxlen)
{
	u8 *b;
	int ret, actual_length;

	b = kmalloc(64, GFP_KERNEL);
	if (!b)
		return -ENOMEM;

	deb_i2c("i2c-access: %02x, tx: ", device_addr);
	debug_dump(tx, txlen, deb_i2c);
	deb_i2c(" ");

	if (txlen > 62) {
		err("i2c TX buffer can't exceed 62 bytes (dev 0x%02x)",
				device_addr);
		txlen = 62;
	}
	if (rxlen > 62) {
		err("i2c RX buffer can't exceed 62 bytes (dev 0x%02x)",
				device_addr);
		rxlen = 62;
	}

	b[0] = I2C_SPEED_100KHZ_BIT;
	b[1] = device_addr << 1;

	if (rx != NULL) {
		b[0] |= rxlen;
		b[1] |= 1;
	}

	memcpy(&b[2], tx, txlen);
	ret = usb_bulk_msg(udev,
			usb_sndbulkpipe(udev, 0x01),
			b, 2 + txlen,
			NULL, 1000);

	if (ret < 0) {
		err("i2c-error: out failed %02x = %d", device_addr, ret);
		goto err;
	}

	ret = usb_bulk_msg(udev,
			usb_rcvbulkpipe(udev, 0x01),
			b, 64, &actual_length, 1000);
	if (ret < 0) {
		err("i2c-error: in failed %02x = %d", device_addr, ret);
		goto err;
	}

	if (b[0] != I2C_STATUS_OK) {
		err("i2c-error: %02x = %d", device_addr, b[0]);
		/* handle tuner-i2c-nak */
		if (!(b[0] == I2C_STATUS_NAK &&
				device_addr == 0x60
				/* && device_is_technisat_usb2 */))
			goto err;
	}

	deb_i2c("status: %d, ", b[0]);

	if (rx != NULL) {
		memcpy(rx, &b[2], rxlen);

		deb_i2c("rx (%d): ", rxlen);
		debug_dump(rx, rxlen, deb_i2c);
	}

	deb_i2c("\n");

err:
	kfree(b);
	return ret;
}

static int technisat_usb2_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msg,
				int num)
{
	int ret = 0, i;
	struct dvb_usb_device *d = i2c_get_adapdata(adap);

	/* Ensure nobody else hits the i2c bus while we're sending our
	   sequence of messages, (such as the remote control thread) */
	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	for (i = 0; i < num; i++) {
		if (i+1 < num && msg[i+1].flags & I2C_M_RD) {
			ret = technisat_usb2_i2c_access(d->udev, msg[i+1].addr,
						msg[i].buf, msg[i].len,
						msg[i+1].buf, msg[i+1].len);
			if (ret != 0)
				break;
			i++;
		} else {
			ret = technisat_usb2_i2c_access(d->udev, msg[i].addr,
						msg[i].buf, msg[i].len,
						NULL, 0);
			if (ret != 0)
				break;
		}
	}

	if (ret == 0)
		ret = i;

	mutex_unlock(&d->i2c_mutex);

	return ret;
}

static u32 technisat_usb2_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm technisat_usb2_i2c_algo = {
	.master_xfer   = technisat_usb2_i2c_xfer,
	.functionality = technisat_usb2_i2c_func,
};

#if 0
static void technisat_usb2_frontend_reset(struct usb_device *udev)
{
	usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			SET_FRONT_END_RESET_VENDOR_REQUEST,
			USB_TYPE_VENDOR | USB_DIR_OUT,
			10, 0,
			NULL, 0, 500);
}
#endif

/* LED control */
enum technisat_usb2_led_state {
	TECH_LED_OFF,
	TECH_LED_BLINK,
	TECH_LED_ON,
	TECH_LED_UNDEFINED
};

static int technisat_usb2_set_led(struct dvb_usb_device *d, int red,
				  enum technisat_usb2_led_state st)
{
	struct technisat_usb2_state *state = d->priv;
	u8 *led = state->buf;
	int ret;

	led[0] = red ? SET_RED_LED_VENDOR_REQUEST : SET_GREEN_LED_VENDOR_REQUEST;

	if (disable_led_control && st != TECH_LED_OFF)
		return 0;

	switch (st) {
	case TECH_LED_ON:
		led[1] = 0x82;
		break;
	case TECH_LED_BLINK:
		led[1] = 0x82;
		if (red) {
			led[2] = 0x02;
			led[3] = 10;
			led[4] = 10;
		} else {
			led[2] = 0xff;
			led[3] = 50;
			led[4] = 50;
		}
		led[5] = 1;
		break;

	default:
	case TECH_LED_OFF:
		led[1] = 0x80;
		break;
	}

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	ret = usb_control_msg(d->udev, usb_sndctrlpipe(d->udev, 0),
		red ? SET_RED_LED_VENDOR_REQUEST : SET_GREEN_LED_VENDOR_REQUEST,
		USB_TYPE_VENDOR | USB_DIR_OUT,
		0, 0,
		led, 8, 500);

	mutex_unlock(&d->i2c_mutex);
	return ret;
}

static int technisat_usb2_set_led_timer(struct dvb_usb_device *d, u8 red, u8 green)
{
	struct technisat_usb2_state *state = d->priv;
	u8 *b = state->buf;
	int ret;

	b[0] = 0;

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	ret = usb_control_msg(d->udev, usb_sndctrlpipe(d->udev, 0),
		SET_LED_TIMER_DIVIDER_VENDOR_REQUEST,
		USB_TYPE_VENDOR | USB_DIR_OUT,
		(red << 8) | green, 0,
		b, 1, 500);

	mutex_unlock(&d->i2c_mutex);

	return ret;
}

static void technisat_usb2_green_led_control(struct work_struct *work)
{
	struct technisat_usb2_state *state =
		container_of(work, struct technisat_usb2_state, green_led_work.work);
	struct dvb_frontend *fe = state->dev->adapter[0].fe_adap[0].fe;

	if (state->power_state == 0)
		goto schedule;

	if (fe != NULL) {
		enum fe_status status;

		if (fe->ops.read_status(fe, &status) != 0)
			goto schedule;

		if (status & FE_HAS_LOCK) {
			u32 ber;

			if (fe->ops.read_ber(fe, &ber) != 0)
				goto schedule;

			if (ber > 1000)
				technisat_usb2_set_led(state->dev, 0, TECH_LED_BLINK);
			else
				technisat_usb2_set_led(state->dev, 0, TECH_LED_ON);
		} else
			technisat_usb2_set_led(state->dev, 0, TECH_LED_OFF);
	}

schedule:
	schedule_delayed_work(&state->green_led_work,
			msecs_to_jiffies(500));
}

/* method to find out whether the firmware has to be downloaded or not */
static int technisat_usb2_identify_state(struct usb_device *udev,
		struct dvb_usb_device_properties *props,
		struct dvb_usb_device_description **desc, int *cold)
{
	int ret;
	u8 *version;

	version = kmalloc(3, GFP_KERNEL);
	if (!version)
		return -ENOMEM;

	/* first select the interface */
	if (usb_set_interface(udev, 0, 1) != 0)
		err("could not set alternate setting to 0");
	else
		info("set alternate setting");

	*cold = 0; /* by default do not download a firmware - just in case something is wrong */

	ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
		GET_VERSION_INFO_VENDOR_REQUEST,
		USB_TYPE_VENDOR | USB_DIR_IN,
		0, 0,
		version, 3, 500);

	if (ret < 0)
		*cold = 1;
	else {
		info("firmware version: %d.%d", version[1], version[2]);
		*cold = 0;
	}

	kfree(version);

	return 0;
}

/* power control */
static int technisat_usb2_power_ctrl(struct dvb_usb_device *d, int level)
{
	struct technisat_usb2_state *state = d->priv;

	state->power_state = level;

	if (disable_led_control)
		return 0;

	/* green led is turned off in any case - will be turned on when tuning */
	technisat_usb2_set_led(d, 0, TECH_LED_OFF);
	/* red led is turned on all the time */
	technisat_usb2_set_led(d, 1, TECH_LED_ON);
	return 0;
}

/* mac address reading - from the eeprom */
#if 0
static void technisat_usb2_eeprom_dump(struct dvb_usb_device *d)
{
	u8 reg;
	u8 b[16];
	int i, j;

	/* full EEPROM dump */
	for (j = 0; j < 256 * 4; j += 16) {
		reg = j;
		if (technisat_usb2_i2c_access(d->udev, 0x50 + j / 256, &reg, 1, b, 16) != 0)
			break;

		deb_eeprom("EEPROM: %01x%02x: ", j / 256, reg);
		for (i = 0; i < 16; i++)
			deb_eeprom("%02x ", b[i]);
		deb_eeprom("\n");
	}
}
#endif

static u8 technisat_usb2_calc_lrc(const u8 *b, u16 length)
{
	u8 lrc = 0;
	while (--length)
		lrc ^= *b++;
	return lrc;
}

static int technisat_usb2_eeprom_lrc_read(struct dvb_usb_device *d,
	u16 offset, u8 *b, u16 length, u8 tries)
{
	u8 bo = offset & 0xff;
	struct i2c_msg msg[] = {
		{
			.addr = 0x50 | ((offset >> 8) & 0x3),
			.buf = &bo,
			.len = 1
		}, {
			.addr = 0x50 | ((offset >> 8) & 0x3),
			.flags	= I2C_M_RD,
			.buf = b,
			.len = length
		}
	};

	while (tries--) {
		int status;

		if (i2c_transfer(&d->i2c_adap, msg, 2) != 2)
			break;

		status =
			technisat_usb2_calc_lrc(b, length - 1) == b[length - 1];

		if (status)
			return 0;
	}

	return -EREMOTEIO;
}

#define EEPROM_MAC_START 0x3f8
#define EEPROM_MAC_TOTAL 8
static int technisat_usb2_read_mac_address(struct dvb_usb_device *d,
		u8 mac[])
{
	u8 buf[EEPROM_MAC_TOTAL];

	if (technisat_usb2_eeprom_lrc_read(d, EEPROM_MAC_START,
				buf, EEPROM_MAC_TOTAL, 4) != 0)
		return -ENODEV;

	memcpy(mac, buf, 6);
	return 0;
}

static struct stv090x_config technisat_usb2_stv090x_config;

/* frontend attach */
static int technisat_usb2_set_voltage(struct dvb_frontend *fe,
				      enum fe_sec_voltage voltage)
{
	int i;
	u8 gpio[3] = { 0 }; /* 0 = 2, 1 = 3, 2 = 4 */

	gpio[2] = 1; /* high - voltage ? */

	switch (voltage) {
	case SEC_VOLTAGE_13:
		gpio[0] = 1;
		break;
	case SEC_VOLTAGE_18:
		gpio[0] = 1;
		gpio[1] = 1;
		break;
	default:
	case SEC_VOLTAGE_OFF:
		break;
	}

	for (i = 0; i < 3; i++)
		if (technisat_usb2_stv090x_config.set_gpio(fe, i+2, 0,
							   gpio[i], 0) != 0)
			return -EREMOTEIO;
	return 0;
}

static struct stv090x_config technisat_usb2_stv090x_config = {
	.device         = STV0903,
	.demod_mode     = STV090x_SINGLE,
	.clk_mode       = STV090x_CLK_EXT,

	.xtal           = 8000000,
	.address        = 0x68,

	.ts1_mode       = STV090x_TSMODE_DVBCI,
	.ts1_clk        = 13400000,
	.ts1_tei        = 1,

	.repeater_level = STV090x_RPTLEVEL_64,

	.tuner_bbgain   = 6,
};

static struct stv6110x_config technisat_usb2_stv6110x_config = {
	.addr           = 0x60,
	.refclk         = 16000000,
	.clk_div        = 2,
};

static int technisat_usb2_frontend_attach(struct dvb_usb_adapter *a)
{
	struct usb_device *udev = a->dev->udev;
	int ret;

	a->fe_adap[0].fe = dvb_attach(stv090x_attach, &technisat_usb2_stv090x_config,
			&a->dev->i2c_adap, STV090x_DEMODULATOR_0);

	if (a->fe_adap[0].fe) {
		const struct stv6110x_devctl *ctl;

		ctl = dvb_attach(stv6110x_attach,
				a->fe_adap[0].fe,
				&technisat_usb2_stv6110x_config,
				&a->dev->i2c_adap);

		if (ctl) {
			technisat_usb2_stv090x_config.tuner_init          = ctl->tuner_init;
			technisat_usb2_stv090x_config.tuner_sleep         = ctl->tuner_sleep;
			technisat_usb2_stv090x_config.tuner_set_mode      = ctl->tuner_set_mode;
			technisat_usb2_stv090x_config.tuner_set_frequency = ctl->tuner_set_frequency;
			technisat_usb2_stv090x_config.tuner_get_frequency = ctl->tuner_get_frequency;
			technisat_usb2_stv090x_config.tuner_set_bandwidth = ctl->tuner_set_bandwidth;
			technisat_usb2_stv090x_config.tuner_get_bandwidth = ctl->tuner_get_bandwidth;
			technisat_usb2_stv090x_config.tuner_set_bbgain    = ctl->tuner_set_bbgain;
			technisat_usb2_stv090x_config.tuner_get_bbgain    = ctl->tuner_get_bbgain;
			technisat_usb2_stv090x_config.tuner_set_refclk    = ctl->tuner_set_refclk;
			technisat_usb2_stv090x_config.tuner_get_status    = ctl->tuner_get_status;

			/* call the init function once to initialize
			   tuner's clock output divider and demod's
			   master clock */
			if (a->fe_adap[0].fe->ops.init)
				a->fe_adap[0].fe->ops.init(a->fe_adap[0].fe);

			if (mutex_lock_interruptible(&a->dev->i2c_mutex) < 0)
				return -EAGAIN;

			ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
					SET_IFCLK_TO_EXTERNAL_TSCLK_VENDOR_REQUEST,
					USB_TYPE_VENDOR | USB_DIR_OUT,
					0, 0,
					NULL, 0, 500);
			mutex_unlock(&a->dev->i2c_mutex);

			if (ret != 0)
				err("could not set IF_CLK to external");

			a->fe_adap[0].fe->ops.set_voltage = technisat_usb2_set_voltage;

			/* if everything was successful assign a nice name to the frontend */
			strlcpy(a->fe_adap[0].fe->ops.info.name, a->dev->desc->name,
					sizeof(a->fe_adap[0].fe->ops.info.name));
		} else {
			dvb_frontend_detach(a->fe_adap[0].fe);
			a->fe_adap[0].fe = NULL;
		}
	}

	technisat_usb2_set_led_timer(a->dev, 1, 1);

	return a->fe_adap[0].fe == NULL ? -ENODEV : 0;
}

/* Remote control */

/* the device is giving providing raw IR-signals to the host mapping
 * it only to one remote control is just the default implementation
 */
#define NOMINAL_IR_BIT_TRANSITION_TIME_US 889
#define NOMINAL_IR_BIT_TIME_US (2 * NOMINAL_IR_BIT_TRANSITION_TIME_US)

#define FIRMWARE_CLOCK_TICK 83333
#define FIRMWARE_CLOCK_DIVISOR 256

#define IR_PERCENT_TOLERANCE 15

#define NOMINAL_IR_BIT_TRANSITION_TICKS ((NOMINAL_IR_BIT_TRANSITION_TIME_US * 1000 * 1000) / FIRMWARE_CLOCK_TICK)
#define NOMINAL_IR_BIT_TRANSITION_TICK_COUNT (NOMINAL_IR_BIT_TRANSITION_TICKS / FIRMWARE_CLOCK_DIVISOR)

#define NOMINAL_IR_BIT_TIME_TICKS ((NOMINAL_IR_BIT_TIME_US * 1000 * 1000) / FIRMWARE_CLOCK_TICK)
#define NOMINAL_IR_BIT_TIME_TICK_COUNT (NOMINAL_IR_BIT_TIME_TICKS / FIRMWARE_CLOCK_DIVISOR)

#define MINIMUM_IR_BIT_TRANSITION_TICK_COUNT (NOMINAL_IR_BIT_TRANSITION_TICK_COUNT - ((NOMINAL_IR_BIT_TRANSITION_TICK_COUNT * IR_PERCENT_TOLERANCE) / 100))
#define MAXIMUM_IR_BIT_TRANSITION_TICK_COUNT (NOMINAL_IR_BIT_TRANSITION_TICK_COUNT + ((NOMINAL_IR_BIT_TRANSITION_TICK_COUNT * IR_PERCENT_TOLERANCE) / 100))

#define MINIMUM_IR_BIT_TIME_TICK_COUNT (NOMINAL_IR_BIT_TIME_TICK_COUNT - ((NOMINAL_IR_BIT_TIME_TICK_COUNT * IR_PERCENT_TOLERANCE) / 100))
#define MAXIMUM_IR_BIT_TIME_TICK_COUNT (NOMINAL_IR_BIT_TIME_TICK_COUNT + ((NOMINAL_IR_BIT_TIME_TICK_COUNT * IR_PERCENT_TOLERANCE) / 100))

static int technisat_usb2_get_ir(struct dvb_usb_device *d)
{
	struct technisat_usb2_state *state = d->priv;
	u8 *buf = state->buf;
	u8 *b;
	int ret;
	struct ir_raw_event ev;

	buf[0] = GET_IR_DATA_VENDOR_REQUEST;
	buf[1] = 0x08;
	buf[2] = 0x8f;
	buf[3] = MINIMUM_IR_BIT_TRANSITION_TICK_COUNT;
	buf[4] = MAXIMUM_IR_BIT_TIME_TICK_COUNT;

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;
	ret = usb_control_msg(d->udev, usb_sndctrlpipe(d->udev, 0),
			GET_IR_DATA_VENDOR_REQUEST,
			USB_TYPE_VENDOR | USB_DIR_OUT,
			0, 0,
			buf, 5, 500);
	if (ret < 0)
		goto unlock;

	buf[1] = 0;
	buf[2] = 0;
	ret = usb_control_msg(d->udev, usb_rcvctrlpipe(d->udev, 0),
			GET_IR_DATA_VENDOR_REQUEST,
			USB_TYPE_VENDOR | USB_DIR_IN,
			0x8080, 0,
			buf, 62, 500);

unlock:
	mutex_unlock(&d->i2c_mutex);

	if (ret < 0)
		return ret;

	if (ret == 1)
		return 0; /* no key pressed */

	/* decoding */
	b = buf+1;

#if 0
	deb_rc("RC: %d ", ret);
	debug_dump(b, ret, deb_rc);
#endif

	ev.pulse = 0;
	while (1) {
		ev.pulse = !ev.pulse;
		ev.duration = (*b * FIRMWARE_CLOCK_DIVISOR * FIRMWARE_CLOCK_TICK) / 1000;
		ir_raw_event_store(d->rc_dev, &ev);

		b++;
		if (*b == 0xff) {
			ev.pulse = 0;
			ev.duration = 888888*2;
			ir_raw_event_store(d->rc_dev, &ev);
			break;
		}
	}

	ir_raw_event_handle(d->rc_dev);

	return 1;
}

static int technisat_usb2_rc_query(struct dvb_usb_device *d)
{
	int ret = technisat_usb2_get_ir(d);

	if (ret < 0)
		return ret;

	if (ret == 0)
		return 0;

	if (!disable_led_control)
		technisat_usb2_set_led(d, 1, TECH_LED_BLINK);

	return 0;
}

/* DVB-USB and USB stuff follows */
static struct usb_device_id technisat_usb2_id_table[] = {
	{ USB_DEVICE(USB_VID_TECHNISAT, USB_PID_TECHNISAT_USB2_DVB_S2) },
	{ 0 }		/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, technisat_usb2_id_table);

/* device description */
static struct dvb_usb_device_properties technisat_usb2_devices = {
	.caps              = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl          = CYPRESS_FX2,

	.identify_state    = technisat_usb2_identify_state,
	.firmware          = "dvb-usb-SkyStar_USB_HD_FW_v17_63.HEX.fw",

	.size_of_priv      = sizeof(struct technisat_usb2_state),

	.i2c_algo          = &technisat_usb2_i2c_algo,

	.power_ctrl        = technisat_usb2_power_ctrl,
	.read_mac_address  = technisat_usb2_read_mac_address,

	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.frontend_attach  = technisat_usb2_frontend_attach,

			.stream = {
				.type = USB_ISOC,
				.count = 4,
				.endpoint = 0x2,
				.u = {
					.isoc = {
						.framesperurb = 32,
						.framesize = 2048,
						.interval = 1,
					}
				}
			},
		}},
			.size_of_priv = 0,
		},
	},

	.num_device_descs = 1,
	.devices = {
		{   "Technisat SkyStar USB HD (DVB-S/S2)",
			{ &technisat_usb2_id_table[0], NULL },
			{ NULL },
		},
	},

	.rc.core = {
		.rc_interval = 100,
		.rc_codes    = RC_MAP_TECHNISAT_USB2,
		.module_name = "technisat-usb2",
		.rc_query    = technisat_usb2_rc_query,
		.allowed_protos = RC_BIT_ALL,
		.driver_type    = RC_DRIVER_IR_RAW,
	}
};

static int technisat_usb2_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	struct dvb_usb_device *dev;

	if (dvb_usb_device_init(intf, &technisat_usb2_devices, THIS_MODULE,
				&dev, adapter_nr) != 0)
		return -ENODEV;

	if (dev) {
		struct technisat_usb2_state *state = dev->priv;
		state->dev = dev;

		if (!disable_led_control) {
			INIT_DELAYED_WORK(&state->green_led_work,
					technisat_usb2_green_led_control);
			schedule_delayed_work(&state->green_led_work,
					msecs_to_jiffies(500));
		}
	}

	return 0;
}

static void technisat_usb2_disconnect(struct usb_interface *intf)
{
	struct dvb_usb_device *dev = usb_get_intfdata(intf);

	/* work and stuff was only created when the device is is hot-state */
	if (dev != NULL) {
		struct technisat_usb2_state *state = dev->priv;
		if (state != NULL)
			cancel_delayed_work_sync(&state->green_led_work);
	}

	dvb_usb_device_exit(intf);
}

static struct usb_driver technisat_usb2_driver = {
	.name       = "dvb_usb_technisat_usb2",
	.probe      = technisat_usb2_probe,
	.disconnect = technisat_usb2_disconnect,
	.id_table   = technisat_usb2_id_table,
};

module_usb_driver(technisat_usb2_driver);

MODULE_AUTHOR("Patrick Boettcher <pboettcher@kernellabs.com>");
MODULE_DESCRIPTION("Driver for Technisat DVB-S/S2 USB 2.0 device");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
