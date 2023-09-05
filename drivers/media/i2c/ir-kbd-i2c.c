// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * keyboard input driver for i2c IR remote controls
 *
 * Copyright (c) 2000-2003 Gerd Knorr <kraxel@bytesex.org>
 * modified for PixelView (BT878P+W/FM) by
 *      Michal Kochanowicz <mkochano@pld.org.pl>
 *      Christoph Bartelmus <lirc@bartelmus.de>
 * modified for KNC ONE TV Station/Anubis Typhoon TView Tuner by
 *      Ulrich Mueller <ulrich.mueller42@web.de>
 * modified for em2820 based USB TV tuners by
 *      Markus Rechberger <mrechberger@gmail.com>
 * modified for DViCO Fusion HDTV 5 RT GOLD by
 *      Chaogui Zhang <czhang1974@gmail.com>
 * modified for MSI TV@nywhere Plus by
 *      Henry Wong <henry@stuffedcow.net>
 *      Mark Schultz <n9xmj@yahoo.com>
 *      Brian Rogers <brian_rogers@comcast.net>
 * modified for AVerMedia Cardbus by
 *      Oldrich Jedlicka <oldium.pro@seznam.cz>
 * Zilog Transmitter portions/ideas were derived from GPLv2+ sources:
 *  - drivers/char/pctv_zilogir.[ch] from Hauppauge Broadway product
 *	Copyright 2011 Hauppauge Computer works
 *  - drivers/staging/media/lirc/lirc_zilog.c
 *	Copyright (c) 2000 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *	Michal Kochanowicz <mkochano@pld.org.pl>
 *	Christoph Bartelmus <lirc@bartelmus.de>
 *	Ulrich Mueller <ulrich.mueller42@web.de>
 *	Stefan Jahn <stefan@lkcc.org>
 *	Jerome Brock <jbrock@users.sourceforge.net>
 *	Thomas Reitmayr (treitmayr@yahoo.com)
 *	Mark Weaver <mark@npsl.co.uk>
 *	Jarod Wilson <jarod@redhat.com>
 *	Copyright (C) 2011 Andy Walls <awalls@md.metrocast.net>
 */

#include <asm/unaligned.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>

#include <media/rc-core.h>
#include <media/i2c/ir-kbd-i2c.h>

#define FLAG_TX		1
#define FLAG_HDPVR	2

static bool enable_hdpvr;
module_param(enable_hdpvr, bool, 0644);

static int get_key_haup_common(struct IR_i2c *ir, enum rc_proto *protocol,
			       u32 *scancode, u8 *ptoggle, int size)
{
	unsigned char buf[6];
	int start, range, toggle, dev, code, ircode, vendor;

	/* poll IR chip */
	if (size != i2c_master_recv(ir->c, buf, size))
		return -EIO;

	if (buf[0] & 0x80) {
		int offset = (size == 6) ? 3 : 0;

		/* split rc5 data block ... */
		start  = (buf[offset] >> 7) &    1;
		range  = (buf[offset] >> 6) &    1;
		toggle = (buf[offset] >> 5) &    1;
		dev    =  buf[offset]       & 0x1f;
		code   = (buf[offset+1] >> 2) & 0x3f;

		/* rc5 has two start bits
		 * the first bit must be one
		 * the second bit defines the command range:
		 * 1 = 0-63, 0 = 64 - 127
		 */
		if (!start)
			/* no key pressed */
			return 0;

		/* filter out invalid key presses */
		ircode = (start << 12) | (toggle << 11) | (dev << 6) | code;
		if ((ircode & 0x1fff) == 0x1fff)
			return 0;

		if (!range)
			code += 64;

		dev_dbg(&ir->rc->dev,
			"ir hauppauge (rc5): s%d r%d t%d dev=%d code=%d\n",
			start, range, toggle, dev, code);

		*protocol = RC_PROTO_RC5;
		*scancode = RC_SCANCODE_RC5(dev, code);
		*ptoggle = toggle;

		return 1;
	} else if (size == 6 && (buf[0] & 0x40)) {
		code = buf[4];
		dev = buf[3];
		vendor = get_unaligned_be16(buf + 1);

		if (vendor == 0x800f) {
			*ptoggle = (dev & 0x80) != 0;
			*protocol = RC_PROTO_RC6_MCE;
			dev &= 0x7f;
			dev_dbg(&ir->rc->dev,
				"ir hauppauge (rc6-mce): t%d vendor=%d dev=%d code=%d\n",
				*ptoggle, vendor, dev, code);
		} else {
			*ptoggle = 0;
			*protocol = RC_PROTO_RC6_6A_32;
			dev_dbg(&ir->rc->dev,
				"ir hauppauge (rc6-6a-32): vendor=%d dev=%d code=%d\n",
				vendor, dev, code);
		}

		*scancode = RC_SCANCODE_RC6_6A(vendor, dev, code);

		return 1;
	}

	return 0;
}

static int get_key_haup(struct IR_i2c *ir, enum rc_proto *protocol,
			u32 *scancode, u8 *toggle)
{
	return get_key_haup_common(ir, protocol, scancode, toggle, 3);
}

static int get_key_haup_xvr(struct IR_i2c *ir, enum rc_proto *protocol,
			    u32 *scancode, u8 *toggle)
{
	int ret;
	unsigned char buf[1] = { 0 };

	/*
	 * This is the same apparent "are you ready?" poll command observed
	 * watching Windows driver traffic and implemented in lirc_zilog. With
	 * this added, we get far saner remote behavior with z8 chips on usb
	 * connected devices, even with the default polling interval of 100ms.
	 */
	ret = i2c_master_send(ir->c, buf, 1);
	if (ret != 1)
		return (ret < 0) ? ret : -EINVAL;

	return get_key_haup_common(ir, protocol, scancode, toggle, 6);
}

static int get_key_pixelview(struct IR_i2c *ir, enum rc_proto *protocol,
			     u32 *scancode, u8 *toggle)
{
	int rc;
	unsigned char b;

	/* poll IR chip */
	rc = i2c_master_recv(ir->c, &b, 1);
	if (rc != 1) {
		dev_dbg(&ir->rc->dev, "read error\n");
		if (rc < 0)
			return rc;
		return -EIO;
	}

	*protocol = RC_PROTO_OTHER;
	*scancode = b;
	*toggle = 0;
	return 1;
}

static int get_key_fusionhdtv(struct IR_i2c *ir, enum rc_proto *protocol,
			      u32 *scancode, u8 *toggle)
{
	int rc;
	unsigned char buf[4];

	/* poll IR chip */
	rc = i2c_master_recv(ir->c, buf, 4);
	if (rc != 4) {
		dev_dbg(&ir->rc->dev, "read error\n");
		if (rc < 0)
			return rc;
		return -EIO;
	}

	if (buf[0] != 0 || buf[1] != 0 || buf[2] != 0 || buf[3] != 0)
		dev_dbg(&ir->rc->dev, "%s: %*ph\n", __func__, 4, buf);

	/* no key pressed or signal from other ir remote */
	if(buf[0] != 0x1 ||  buf[1] != 0xfe)
		return 0;

	*protocol = RC_PROTO_UNKNOWN;
	*scancode = buf[2];
	*toggle = 0;
	return 1;
}

static int get_key_knc1(struct IR_i2c *ir, enum rc_proto *protocol,
			u32 *scancode, u8 *toggle)
{
	int rc;
	unsigned char b;

	/* poll IR chip */
	rc = i2c_master_recv(ir->c, &b, 1);
	if (rc != 1) {
		dev_dbg(&ir->rc->dev, "read error\n");
		if (rc < 0)
			return rc;
		return -EIO;
	}

	/* it seems that 0xFE indicates that a button is still hold
	   down, while 0xff indicates that no button is hold
	   down. 0xfe sequences are sometimes interrupted by 0xFF */

	dev_dbg(&ir->rc->dev, "key %02x\n", b);

	if (b == 0xff)
		return 0;

	if (b == 0xfe)
		/* keep old data */
		return 1;

	*protocol = RC_PROTO_UNKNOWN;
	*scancode = b;
	*toggle = 0;
	return 1;
}

static int get_key_geniatech(struct IR_i2c *ir, enum rc_proto *protocol,
			     u32 *scancode, u8 *toggle)
{
	int i, rc;
	unsigned char b;

	/* poll IR chip */
	for (i = 0; i < 4; i++) {
		rc = i2c_master_recv(ir->c, &b, 1);
		if (rc == 1)
			break;
		msleep(20);
	}
	if (rc != 1) {
		dev_dbg(&ir->rc->dev, "read error\n");
		if (rc < 0)
			return rc;
		return -EIO;
	}

	/* don't repeat the key */
	if (ir->old == b)
		return 0;
	ir->old = b;

	/* decode to RC5 */
	b &= 0x7f;
	b = (b - 1) / 2;

	dev_dbg(&ir->rc->dev, "key %02x\n", b);

	*protocol = RC_PROTO_RC5;
	*scancode = b;
	*toggle = ir->old >> 7;
	return 1;
}

static int get_key_avermedia_cardbus(struct IR_i2c *ir, enum rc_proto *protocol,
				     u32 *scancode, u8 *toggle)
{
	unsigned char subaddr, key, keygroup;
	struct i2c_msg msg[] = { { .addr = ir->c->addr, .flags = 0,
				   .buf = &subaddr, .len = 1},
				 { .addr = ir->c->addr, .flags = I2C_M_RD,
				  .buf = &key, .len = 1} };
	subaddr = 0x0d;
	if (2 != i2c_transfer(ir->c->adapter, msg, 2)) {
		dev_dbg(&ir->rc->dev, "read error\n");
		return -EIO;
	}

	if (key == 0xff)
		return 0;

	subaddr = 0x0b;
	msg[1].buf = &keygroup;
	if (2 != i2c_transfer(ir->c->adapter, msg, 2)) {
		dev_dbg(&ir->rc->dev, "read error\n");
		return -EIO;
	}

	if (keygroup == 0xff)
		return 0;

	dev_dbg(&ir->rc->dev, "read key 0x%02x/0x%02x\n", key, keygroup);
	if (keygroup < 2 || keygroup > 4) {
		dev_warn(&ir->rc->dev, "warning: invalid key group 0x%02x for key 0x%02x\n",
			 keygroup, key);
	}
	key |= (keygroup & 1) << 6;

	*protocol = RC_PROTO_UNKNOWN;
	*scancode = key;
	if (ir->c->addr == 0x41) /* AVerMedia EM78P153 */
		*scancode |= keygroup << 8;
	*toggle = 0;
	return 1;
}

/* ----------------------------------------------------------------------- */

static int ir_key_poll(struct IR_i2c *ir)
{
	enum rc_proto protocol;
	u32 scancode;
	u8 toggle;
	int rc;

	dev_dbg(&ir->rc->dev, "%s\n", __func__);
	rc = ir->get_key(ir, &protocol, &scancode, &toggle);
	if (rc < 0) {
		dev_warn(&ir->rc->dev, "error %d\n", rc);
		return rc;
	}

	if (rc) {
		dev_dbg(&ir->rc->dev, "%s: proto = 0x%04x, scancode = 0x%08x\n",
			__func__, protocol, scancode);
		rc_keydown(ir->rc, protocol, scancode, toggle);
	}
	return 0;
}

static void ir_work(struct work_struct *work)
{
	int rc;
	struct IR_i2c *ir = container_of(work, struct IR_i2c, work.work);

	/*
	 * If the transmit code is holding the lock, skip polling for
	 * IR, we'll get it to it next time round
	 */
	if (mutex_trylock(&ir->lock)) {
		rc = ir_key_poll(ir);
		mutex_unlock(&ir->lock);
		if (rc == -ENODEV) {
			rc_unregister_device(ir->rc);
			ir->rc = NULL;
			return;
		}
	}

	schedule_delayed_work(&ir->work, msecs_to_jiffies(ir->polling_interval));
}

static int ir_open(struct rc_dev *dev)
{
	struct IR_i2c *ir = dev->priv;

	schedule_delayed_work(&ir->work, 0);

	return 0;
}

static void ir_close(struct rc_dev *dev)
{
	struct IR_i2c *ir = dev->priv;

	cancel_delayed_work_sync(&ir->work);
}

/* Zilog Transmit Interface */
#define XTAL_FREQ		18432000

#define ZILOG_SEND		0x80
#define ZILOG_UIR_END		0x40
#define ZILOG_INIT_END		0x20
#define ZILOG_LIR_END		0x10

#define ZILOG_STATUS_OK		0x80
#define ZILOG_STATUS_TX		0x40
#define ZILOG_STATUS_SET	0x20

/*
 * As you can see here, very few different lengths of pulse and space
 * can be encoded. This means that the hardware does not work well with
 * recorded IR. It's best to work with generated IR, like from ir-ctl or
 * the in-kernel encoders.
 */
struct code_block {
	u8	length;
	u16	pulse[7];	/* not aligned */
	u8	carrier_pulse;
	u8	carrier_space;
	u16	space[8];	/* not aligned */
	u8	codes[61];
	u8	csum[2];
} __packed;

static int send_data_block(struct IR_i2c *ir, int cmd,
			   struct code_block *code_block)
{
	int i, j, ret;
	u8 buf[5], *p;

	p = &code_block->length;
	for (i = 0; p < code_block->csum; i++)
		code_block->csum[i & 1] ^= *p++;

	p = &code_block->length;

	for (i = 0; i < sizeof(*code_block);) {
		int tosend = sizeof(*code_block) - i;

		if (tosend > 4)
			tosend = 4;
		buf[0] = i + 1;
		for (j = 0; j < tosend; ++j)
			buf[1 + j] = p[i + j];
		dev_dbg(&ir->rc->dev, "%*ph", tosend + 1, buf);
		ret = i2c_master_send(ir->tx_c, buf, tosend + 1);
		if (ret != tosend + 1) {
			dev_dbg(&ir->rc->dev,
				"i2c_master_send failed with %d\n", ret);
			return ret < 0 ? ret : -EIO;
		}
		i += tosend;
	}

	buf[0] = 0;
	buf[1] = cmd;
	ret = i2c_master_send(ir->tx_c, buf, 2);
	if (ret != 2) {
		dev_err(&ir->rc->dev, "i2c_master_send failed with %d\n", ret);
		return ret < 0 ? ret : -EIO;
	}

	usleep_range(2000, 5000);

	ret = i2c_master_send(ir->tx_c, buf, 1);
	if (ret != 1) {
		dev_err(&ir->rc->dev, "i2c_master_send failed with %d\n", ret);
		return ret < 0 ? ret : -EIO;
	}

	return 0;
}

static int zilog_init(struct IR_i2c *ir)
{
	struct code_block code_block = { .length = sizeof(code_block) };
	u8 buf[4];
	int ret;

	put_unaligned_be16(0x1000, &code_block.pulse[3]);

	ret = send_data_block(ir, ZILOG_INIT_END, &code_block);
	if (ret)
		return ret;

	ret = i2c_master_recv(ir->tx_c, buf, 4);
	if (ret != 4) {
		dev_err(&ir->c->dev, "failed to retrieve firmware version: %d\n",
			ret);
		return ret < 0 ? ret : -EIO;
	}

	dev_info(&ir->c->dev, "Zilog/Hauppauge IR blaster firmware version %d.%d.%d\n",
		 buf[1], buf[2], buf[3]);

	return 0;
}

/*
 * If the last slot for pulse is the same as the current slot for pulse,
 * then use slot no 7.
 */
static void copy_codes(u8 *dst, u8 *src, unsigned int count)
{
	u8 c, last = 0xff;

	while (count--) {
		c = *src++;
		if ((c & 0xf0) == last) {
			*dst++ = 0x70 | (c & 0xf);
		} else {
			*dst++ = c;
			last = c & 0xf0;
		}
	}
}

/*
 * When looking for repeats, we don't care about the trailing space. This
 * is set to the shortest possible anyway.
 */
static int cmp_no_trail(u8 *a, u8 *b, unsigned int count)
{
	while (--count) {
		if (*a++ != *b++)
			return 1;
	}

	return (*a & 0xf0) - (*b & 0xf0);
}

static int find_slot(u16 *array, unsigned int size, u16 val)
{
	int i;

	for (i = 0; i < size; i++) {
		if (get_unaligned_be16(&array[i]) == val) {
			return i;
		} else if (!array[i]) {
			put_unaligned_be16(val, &array[i]);
			return i;
		}
	}

	return -1;
}

static int zilog_ir_format(struct rc_dev *rcdev, unsigned int *txbuf,
			   unsigned int count, struct code_block *code_block)
{
	struct IR_i2c *ir = rcdev->priv;
	int rep, i, l, p = 0, s, c = 0;
	bool repeating;
	u8 codes[174];

	code_block->carrier_pulse = DIV_ROUND_CLOSEST(
			ir->duty_cycle * XTAL_FREQ / 1000, ir->carrier);
	code_block->carrier_space = DIV_ROUND_CLOSEST(
			(100 - ir->duty_cycle) * XTAL_FREQ / 1000, ir->carrier);

	for (i = 0; i < count; i++) {
		if (c >= ARRAY_SIZE(codes) - 1) {
			dev_warn(&rcdev->dev, "IR too long, cannot transmit\n");
			return -EINVAL;
		}

		/*
		 * Lengths more than 142220us cannot be encoded; also
		 * this checks for multiply overflow
		 */
		if (txbuf[i] > 142220)
			return -EINVAL;

		l = DIV_ROUND_CLOSEST((XTAL_FREQ / 1000) * txbuf[i], 40000);

		if (i & 1) {
			s = find_slot(code_block->space,
				      ARRAY_SIZE(code_block->space), l);
			if (s == -1) {
				dev_warn(&rcdev->dev, "Too many different lengths spaces, cannot transmit");
				return -EINVAL;
			}

			/* We have a pulse and space */
			codes[c++] = (p << 4) | s;
		} else {
			p = find_slot(code_block->pulse,
				      ARRAY_SIZE(code_block->pulse), l);
			if (p == -1) {
				dev_warn(&rcdev->dev, "Too many different lengths pulses, cannot transmit");
				return -EINVAL;
			}
		}
	}

	/* We have to encode the trailing pulse. Find the shortest space */
	s = 0;
	for (i = 1; i < ARRAY_SIZE(code_block->space); i++) {
		u16 d = get_unaligned_be16(&code_block->space[i]);

		if (get_unaligned_be16(&code_block->space[s]) > d)
			s = i;
	}

	codes[c++] = (p << 4) | s;

	dev_dbg(&rcdev->dev, "generated %d codes\n", c);

	/*
	 * Are the last N codes (so pulse + space) repeating 3 times?
	 * if so we can shorten the codes list and use code 0xc0 to repeat
	 * them.
	 */
	repeating = false;

	for (rep = c / 3; rep >= 1; rep--) {
		if (!memcmp(&codes[c - rep * 3], &codes[c - rep * 2], rep) &&
		    !cmp_no_trail(&codes[c - rep], &codes[c - rep * 2], rep)) {
			repeating = true;
			break;
		}
	}

	if (repeating) {
		/* first copy any leading non-repeating */
		int leading = c - rep * 3;

		if (leading >= ARRAY_SIZE(code_block->codes) - 3 - rep) {
			dev_warn(&rcdev->dev, "IR too long, cannot transmit\n");
			return -EINVAL;
		}

		dev_dbg(&rcdev->dev, "found trailing %d repeat\n", rep);
		copy_codes(code_block->codes, codes, leading);
		code_block->codes[leading] = 0x82;
		copy_codes(code_block->codes + leading + 1, codes + leading,
			   rep);
		c = leading + 1 + rep;
		code_block->codes[c++] = 0xc0;
	} else {
		if (c >= ARRAY_SIZE(code_block->codes) - 3) {
			dev_warn(&rcdev->dev, "IR too long, cannot transmit\n");
			return -EINVAL;
		}

		dev_dbg(&rcdev->dev, "found no trailing repeat\n");
		code_block->codes[0] = 0x82;
		copy_codes(code_block->codes + 1, codes, c);
		c++;
		code_block->codes[c++] = 0xc4;
	}

	while (c < ARRAY_SIZE(code_block->codes))
		code_block->codes[c++] = 0x83;

	return 0;
}

static int zilog_tx(struct rc_dev *rcdev, unsigned int *txbuf,
		    unsigned int count)
{
	struct IR_i2c *ir = rcdev->priv;
	struct code_block code_block = { .length = sizeof(code_block) };
	u8 buf[2];
	int ret, i;

	ret = zilog_ir_format(rcdev, txbuf, count, &code_block);
	if (ret)
		return ret;

	ret = mutex_lock_interruptible(&ir->lock);
	if (ret)
		return ret;

	ret = send_data_block(ir, ZILOG_UIR_END, &code_block);
	if (ret)
		goto out_unlock;

	ret = i2c_master_recv(ir->tx_c, buf, 1);
	if (ret != 1) {
		dev_err(&ir->rc->dev, "i2c_master_recv failed with %d\n", ret);
		goto out_unlock;
	}

	dev_dbg(&ir->rc->dev, "code set status: %02x\n", buf[0]);

	if (buf[0] != (ZILOG_STATUS_OK | ZILOG_STATUS_SET)) {
		dev_err(&ir->rc->dev, "unexpected IR TX response %02x\n",
			buf[0]);
		ret = -EIO;
		goto out_unlock;
	}

	buf[0] = 0x00;
	buf[1] = ZILOG_SEND;

	ret = i2c_master_send(ir->tx_c, buf, 2);
	if (ret != 2) {
		dev_err(&ir->rc->dev, "i2c_master_send failed with %d\n", ret);
		if (ret >= 0)
			ret = -EIO;
		goto out_unlock;
	}

	dev_dbg(&ir->rc->dev, "send command sent\n");

	/*
	 * This bit NAKs until the device is ready, so we retry it
	 * sleeping a bit each time.  This seems to be what the windows
	 * driver does, approximately.
	 * Try for up to 1s.
	 */
	for (i = 0; i < 20; ++i) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(50));
		ret = i2c_master_send(ir->tx_c, buf, 1);
		if (ret == 1)
			break;
		dev_dbg(&ir->rc->dev,
			"NAK expected: i2c_master_send failed with %d (try %d)\n",
			ret, i + 1);
	}

	if (ret != 1) {
		dev_err(&ir->rc->dev,
			"IR TX chip never got ready: last i2c_master_send failed with %d\n",
			ret);
		if (ret >= 0)
			ret = -EIO;
		goto out_unlock;
	}

	ret = i2c_master_recv(ir->tx_c, buf, 1);
	if (ret != 1) {
		dev_err(&ir->rc->dev, "i2c_master_recv failed with %d\n", ret);
		ret = -EIO;
		goto out_unlock;
	} else if (buf[0] != ZILOG_STATUS_OK) {
		dev_err(&ir->rc->dev, "unexpected IR TX response #2: %02x\n",
			buf[0]);
		ret = -EIO;
		goto out_unlock;
	}
	dev_dbg(&ir->rc->dev, "transmit complete\n");

	/* Oh good, it worked */
	ret = count;
out_unlock:
	mutex_unlock(&ir->lock);

	return ret;
}

static int zilog_tx_carrier(struct rc_dev *dev, u32 carrier)
{
	struct IR_i2c *ir = dev->priv;

	if (carrier > 500000 || carrier < 20000)
		return -EINVAL;

	ir->carrier = carrier;

	return 0;
}

static int zilog_tx_duty_cycle(struct rc_dev *dev, u32 duty_cycle)
{
	struct IR_i2c *ir = dev->priv;

	ir->duty_cycle = duty_cycle;

	return 0;
}

static int ir_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	char *ir_codes = NULL;
	const char *name = NULL;
	u64 rc_proto = RC_PROTO_BIT_UNKNOWN;
	struct IR_i2c *ir;
	struct rc_dev *rc = NULL;
	struct i2c_adapter *adap = client->adapter;
	unsigned short addr = client->addr;
	bool probe_tx = (id->driver_data & FLAG_TX) != 0;
	int err;

	if ((id->driver_data & FLAG_HDPVR) && !enable_hdpvr) {
		dev_err(&client->dev, "IR for HDPVR is known to cause problems during recording, use enable_hdpvr modparam to enable\n");
		return -ENODEV;
	}

	ir = devm_kzalloc(&client->dev, sizeof(*ir), GFP_KERNEL);
	if (!ir)
		return -ENOMEM;

	ir->c = client;
	ir->polling_interval = DEFAULT_POLLING_INTERVAL;
	i2c_set_clientdata(client, ir);

	switch(addr) {
	case 0x64:
		name        = "Pixelview";
		ir->get_key = get_key_pixelview;
		rc_proto    = RC_PROTO_BIT_OTHER;
		ir_codes    = RC_MAP_EMPTY;
		break;
	case 0x18:
	case 0x1f:
	case 0x1a:
		name        = "Hauppauge";
		ir->get_key = get_key_haup;
		rc_proto    = RC_PROTO_BIT_RC5;
		ir_codes    = RC_MAP_HAUPPAUGE;
		break;
	case 0x30:
		name        = "KNC One";
		ir->get_key = get_key_knc1;
		rc_proto    = RC_PROTO_BIT_OTHER;
		ir_codes    = RC_MAP_EMPTY;
		break;
	case 0x33:
		name        = "Geniatech";
		ir->get_key = get_key_geniatech;
		rc_proto    = RC_PROTO_BIT_RC5;
		ir_codes    = RC_MAP_TOTAL_MEDIA_IN_HAND_02;
		ir->old     = 0xfc;
		break;
	case 0x6b:
		name        = "FusionHDTV";
		ir->get_key = get_key_fusionhdtv;
		rc_proto    = RC_PROTO_BIT_UNKNOWN;
		ir_codes    = RC_MAP_FUSIONHDTV_MCE;
		break;
	case 0x40:
		name        = "AVerMedia Cardbus remote";
		ir->get_key = get_key_avermedia_cardbus;
		rc_proto    = RC_PROTO_BIT_OTHER;
		ir_codes    = RC_MAP_AVERMEDIA_CARDBUS;
		break;
	case 0x41:
		name        = "AVerMedia EM78P153";
		ir->get_key = get_key_avermedia_cardbus;
		rc_proto    = RC_PROTO_BIT_OTHER;
		/* RM-KV remote, seems to be same as RM-K6 */
		ir_codes    = RC_MAP_AVERMEDIA_M733A_RM_K6;
		break;
	case 0x71:
		name        = "Hauppauge/Zilog Z8";
		ir->get_key = get_key_haup_xvr;
		rc_proto    = RC_PROTO_BIT_RC5 | RC_PROTO_BIT_RC6_MCE |
							RC_PROTO_BIT_RC6_6A_32;
		ir_codes    = RC_MAP_HAUPPAUGE;
		ir->polling_interval = 125;
		probe_tx = true;
		break;
	}

	/* Let the caller override settings */
	if (client->dev.platform_data) {
		const struct IR_i2c_init_data *init_data =
						client->dev.platform_data;

		ir_codes = init_data->ir_codes;
		rc = init_data->rc_dev;

		name = init_data->name;
		if (init_data->type)
			rc_proto = init_data->type;

		if (init_data->polling_interval)
			ir->polling_interval = init_data->polling_interval;

		switch (init_data->internal_get_key_func) {
		case IR_KBD_GET_KEY_CUSTOM:
			/* The bridge driver provided us its own function */
			ir->get_key = init_data->get_key;
			break;
		case IR_KBD_GET_KEY_PIXELVIEW:
			ir->get_key = get_key_pixelview;
			break;
		case IR_KBD_GET_KEY_HAUP:
			ir->get_key = get_key_haup;
			break;
		case IR_KBD_GET_KEY_KNC1:
			ir->get_key = get_key_knc1;
			break;
		case IR_KBD_GET_KEY_GENIATECH:
			ir->get_key = get_key_geniatech;
			break;
		case IR_KBD_GET_KEY_FUSIONHDTV:
			ir->get_key = get_key_fusionhdtv;
			break;
		case IR_KBD_GET_KEY_HAUP_XVR:
			ir->get_key = get_key_haup_xvr;
			break;
		case IR_KBD_GET_KEY_AVERMEDIA_CARDBUS:
			ir->get_key = get_key_avermedia_cardbus;
			break;
		}
	}

	if (!rc) {
		/*
		 * If platform_data doesn't specify rc_dev, initialize it
		 * internally
		 */
		rc = rc_allocate_device(RC_DRIVER_SCANCODE);
		if (!rc)
			return -ENOMEM;
	}
	ir->rc = rc;

	/* Make sure we are all setup before going on */
	if (!name || !ir->get_key || !rc_proto || !ir_codes) {
		dev_warn(&client->dev, "Unsupported device at address 0x%02x\n",
			 addr);
		err = -ENODEV;
		goto err_out_free;
	}

	ir->ir_codes = ir_codes;

	snprintf(ir->phys, sizeof(ir->phys), "%s/%s", dev_name(&adap->dev),
		 dev_name(&client->dev));

	/*
	 * Initialize input_dev fields
	 * It doesn't make sense to allow overriding them via platform_data
	 */
	rc->input_id.bustype = BUS_I2C;
	rc->input_phys       = ir->phys;
	rc->device_name	     = name;
	rc->dev.parent       = &client->dev;
	rc->priv             = ir;
	rc->open             = ir_open;
	rc->close            = ir_close;

	/*
	 * Initialize the other fields of rc_dev
	 */
	rc->map_name       = ir->ir_codes;
	rc->allowed_protocols = rc_proto;
	if (!rc->driver_name)
		rc->driver_name = KBUILD_MODNAME;

	mutex_init(&ir->lock);

	INIT_DELAYED_WORK(&ir->work, ir_work);

	if (probe_tx) {
		ir->tx_c = i2c_new_dummy_device(client->adapter, 0x70);
		if (IS_ERR(ir->tx_c)) {
			dev_err(&client->dev, "failed to setup tx i2c address");
			err = PTR_ERR(ir->tx_c);
			goto err_out_free;
		} else if (!zilog_init(ir)) {
			ir->carrier = 38000;
			ir->duty_cycle = 40;
			rc->tx_ir = zilog_tx;
			rc->s_tx_carrier = zilog_tx_carrier;
			rc->s_tx_duty_cycle = zilog_tx_duty_cycle;
		}
	}

	err = rc_register_device(rc);
	if (err)
		goto err_out_free;

	return 0;

 err_out_free:
	if (!IS_ERR(ir->tx_c))
		i2c_unregister_device(ir->tx_c);

	/* Only frees rc if it were allocated internally */
	rc_free_device(rc);
	return err;
}

static void ir_remove(struct i2c_client *client)
{
	struct IR_i2c *ir = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&ir->work);

	i2c_unregister_device(ir->tx_c);

	rc_unregister_device(ir->rc);
}

static const struct i2c_device_id ir_kbd_id[] = {
	/* Generic entry for any IR receiver */
	{ "ir_video", 0 },
	/* IR device specific entries should be added here */
	{ "ir_z8f0811_haup", FLAG_TX },
	{ "ir_z8f0811_hdpvr", FLAG_TX | FLAG_HDPVR },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ir_kbd_id);

static struct i2c_driver ir_kbd_driver = {
	.driver = {
		.name   = "ir-kbd-i2c",
	},
	.probe          = ir_probe,
	.remove         = ir_remove,
	.id_table       = ir_kbd_id,
};

module_i2c_driver(ir_kbd_driver);

/* ----------------------------------------------------------------------- */

MODULE_AUTHOR("Gerd Knorr, Michal Kochanowicz, Christoph Bartelmus, Ulrich Mueller");
MODULE_DESCRIPTION("input driver for i2c IR remote controls");
MODULE_LICENSE("GPL");
