/*
 * DVB USB Linux driver for Afatech AF9015 DVB-T USB2.0 receiver
 *
 * Copyright (C) 2007 Antti Palosaari <crope@iki.fi>
 *
 * Thanks to Afatech who kindly provided information.
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 */

#include "af9015.h"

static int dvb_usb_af9015_remote;
module_param_named(remote, dvb_usb_af9015_remote, int, 0644);
MODULE_PARM_DESC(remote, "select remote");
DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static int af9015_ctrl_msg(struct dvb_usb_device *d, struct req_t *req)
{
#define REQ_HDR_LEN 8 /* send header size */
#define ACK_HDR_LEN 2 /* rece header size */
	struct af9015_state *state = d_to_priv(d);
	struct usb_interface *intf = d->intf;
	int ret, wlen, rlen;
	u8 write = 1;

	mutex_lock(&d->usb_mutex);

	state->buf[0] = req->cmd;
	state->buf[1] = state->seq++;
	state->buf[2] = req->i2c_addr << 1;
	state->buf[3] = req->addr >> 8;
	state->buf[4] = req->addr & 0xff;
	state->buf[5] = req->mbox;
	state->buf[6] = req->addr_len;
	state->buf[7] = req->data_len;

	switch (req->cmd) {
	case GET_CONFIG:
	case READ_MEMORY:
	case RECONNECT_USB:
		write = 0;
		break;
	case READ_I2C:
		write = 0;
		state->buf[2] |= 0x01; /* set I2C direction */
		/* fall through */
	case WRITE_I2C:
		state->buf[0] = READ_WRITE_I2C;
		break;
	case WRITE_MEMORY:
		if (((req->addr & 0xff00) == 0xff00) ||
		    ((req->addr & 0xff00) == 0xae00))
			state->buf[0] = WRITE_VIRTUAL_MEMORY;
	case WRITE_VIRTUAL_MEMORY:
	case COPY_FIRMWARE:
	case DOWNLOAD_FIRMWARE:
	case BOOT:
		break;
	default:
		dev_err(&intf->dev, "unknown cmd %d\n", req->cmd);
		ret = -EIO;
		goto error;
	}

	/* Buffer overflow check */
	if ((write && (req->data_len > BUF_LEN - REQ_HDR_LEN)) ||
	    (!write && (req->data_len > BUF_LEN - ACK_HDR_LEN))) {
		dev_err(&intf->dev, "too much data, cmd %u, len %u\n",
			req->cmd, req->data_len);
		ret = -EINVAL;
		goto error;
	}

	/*
	 * Write receives seq + status = 2 bytes
	 * Read receives seq + status + data = 2 + N bytes
	 */
	wlen = REQ_HDR_LEN;
	rlen = ACK_HDR_LEN;
	if (write) {
		wlen += req->data_len;
		memcpy(&state->buf[REQ_HDR_LEN], req->data, req->data_len);
	} else {
		rlen += req->data_len;
	}

	/* no ack for these packets */
	if (req->cmd == DOWNLOAD_FIRMWARE || req->cmd == RECONNECT_USB)
		rlen = 0;

	ret = dvb_usbv2_generic_rw_locked(d, state->buf, wlen,
					  state->buf, rlen);
	if (ret)
		goto error;

	/* check status */
	if (rlen && state->buf[1]) {
		dev_err(&intf->dev, "cmd failed %u\n", state->buf[1]);
		ret = -EIO;
		goto error;
	}

	/* read request, copy returned data to return buf */
	if (!write)
		memcpy(req->data, &state->buf[ACK_HDR_LEN], req->data_len);
error:
	mutex_unlock(&d->usb_mutex);

	return ret;
}

static int af9015_write_reg_i2c(struct dvb_usb_device *d, u8 addr, u16 reg,
				u8 val)
{
	struct af9015_state *state = d_to_priv(d);
	struct req_t req = {WRITE_I2C, addr, reg, 1, 1, 1, &val};

	if (addr == state->af9013_i2c_addr[0] ||
	    addr == state->af9013_i2c_addr[1])
		req.addr_len = 3;

	return af9015_ctrl_msg(d, &req);
}

static int af9015_read_reg_i2c(struct dvb_usb_device *d, u8 addr, u16 reg,
			       u8 *val)
{
	struct af9015_state *state = d_to_priv(d);
	struct req_t req = {READ_I2C, addr, reg, 0, 1, 1, val};

	if (addr == state->af9013_i2c_addr[0] ||
	    addr == state->af9013_i2c_addr[1])
		req.addr_len = 3;

	return af9015_ctrl_msg(d, &req);
}

static int af9015_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msg[],
			   int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	struct af9015_state *state = d_to_priv(d);
	struct usb_interface *intf = d->intf;
	int ret;
	u16 addr;
	u8 mbox, addr_len;
	struct req_t req;

	/*
	 * I2C multiplexing:
	 * There could be two tuners, both using same I2C address. Demodulator
	 * I2C-gate is only possibility to select correct tuner.
	 *
	 * ...........................................
	 * . AF9015 integrates AF9013 demodulator    .
	 * . ____________               ____________ .             ____________
	 * .|   USB IF   |             |   demod    |.            |   tuner    |
	 * .|------------|             |------------|.            |------------|
	 * .|   AF9015   |             |   AF9013   |.            |   MXL5003  |
	 * .|            |--+--I2C-----|-----/ -----|.----I2C-----|            |
	 * .|            |  |          | addr 0x1c  |.            |  addr 0x63 |
	 * .|____________|  |          |____________|.            |____________|
	 * .................|.........................
	 *                  |           ____________               ____________
	 *                  |          |   demod    |             |   tuner    |
	 *                  |          |------------|             |------------|
	 *                  |          |   AF9013   |             |   MXL5003  |
	 *                  +--I2C-----|-----/ -----|-----I2C-----|            |
	 *                             | addr 0x1d  |             |  addr 0x63 |
	 *                             |____________|             |____________|
	 */

	if (msg[0].len == 0 || msg[0].flags & I2C_M_RD) {
		addr = 0x0000;
		mbox = 0;
		addr_len = 0;
	} else if (msg[0].len == 1) {
		addr = msg[0].buf[0];
		mbox = 0;
		addr_len = 1;
	} else if (msg[0].len == 2) {
		addr = msg[0].buf[0] << 8 | msg[0].buf[1] << 0;
		mbox = 0;
		addr_len = 2;
	} else {
		addr = msg[0].buf[0] << 8 | msg[0].buf[1] << 0;
		mbox = msg[0].buf[2];
		addr_len = 3;
	}

	if (num == 1 && !(msg[0].flags & I2C_M_RD)) {
		/* i2c write */
		if (msg[0].len > 21) {
			ret = -EOPNOTSUPP;
			goto err;
		}
		if (msg[0].addr == state->af9013_i2c_addr[0])
			req.cmd = WRITE_MEMORY;
		else
			req.cmd = WRITE_I2C;
		req.i2c_addr = msg[0].addr;
		req.addr = addr;
		req.mbox = mbox;
		req.addr_len = addr_len;
		req.data_len = msg[0].len - addr_len;
		req.data = &msg[0].buf[addr_len];
		ret = af9015_ctrl_msg(d, &req);
	} else if (num == 2 && !(msg[0].flags & I2C_M_RD) &&
		   (msg[1].flags & I2C_M_RD)) {
		/* i2c write + read */
		if (msg[0].len > 3 || msg[1].len > 61) {
			ret = -EOPNOTSUPP;
			goto err;
		}
		if (msg[0].addr == state->af9013_i2c_addr[0])
			req.cmd = READ_MEMORY;
		else
			req.cmd = READ_I2C;
		req.i2c_addr = msg[0].addr;
		req.addr = addr;
		req.mbox = mbox;
		req.addr_len = addr_len;
		req.data_len = msg[1].len;
		req.data = &msg[1].buf[0];
		ret = af9015_ctrl_msg(d, &req);
	} else if (num == 1 && (msg[0].flags & I2C_M_RD)) {
		/* i2c read */
		if (msg[0].len > 61) {
			ret = -EOPNOTSUPP;
			goto err;
		}
		if (msg[0].addr == state->af9013_i2c_addr[0]) {
			ret = -EINVAL;
			goto err;
		}
		req.cmd = READ_I2C;
		req.i2c_addr = msg[0].addr;
		req.addr = addr;
		req.mbox = mbox;
		req.addr_len = addr_len;
		req.data_len = msg[0].len;
		req.data = &msg[0].buf[0];
		ret = af9015_ctrl_msg(d, &req);
	} else {
		ret = -EOPNOTSUPP;
		dev_dbg(&intf->dev, "unknown msg, num %u\n", num);
	}
	if (ret)
		goto err;

	return num;
err:
	dev_dbg(&intf->dev, "failed %d\n", ret);
	return ret;
}

static u32 af9015_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm af9015_i2c_algo = {
	.master_xfer = af9015_i2c_xfer,
	.functionality = af9015_i2c_func,
};

static int af9015_identify_state(struct dvb_usb_device *d, const char **name)
{
	struct usb_interface *intf = d->intf;
	int ret;
	u8 reply;
	struct req_t req = {GET_CONFIG, 0, 0, 0, 0, 1, &reply};

	ret = af9015_ctrl_msg(d, &req);
	if (ret)
		return ret;

	dev_dbg(&intf->dev, "reply %02x\n", reply);

	if (reply == 0x02)
		ret = WARM;
	else
		ret = COLD;

	return ret;
}

static int af9015_download_firmware(struct dvb_usb_device *d,
				    const struct firmware *firmware)
{
	struct af9015_state *state = d_to_priv(d);
	struct usb_interface *intf = d->intf;
	int ret, i, rem;
	struct req_t req = {DOWNLOAD_FIRMWARE, 0, 0, 0, 0, 0, NULL};
	u16 checksum;

	dev_dbg(&intf->dev, "\n");

	/* Calc checksum, we need it when copy firmware to slave demod */
	for (i = 0, checksum = 0; i < firmware->size; i++)
		checksum += firmware->data[i];

	state->firmware_size = firmware->size;
	state->firmware_checksum = checksum;

	#define LEN_MAX (BUF_LEN - REQ_HDR_LEN) /* Max payload size */
	for (rem = firmware->size; rem > 0; rem -= LEN_MAX) {
		req.data_len = min(LEN_MAX, rem);
		req.data = (u8 *)&firmware->data[firmware->size - rem];
		req.addr = 0x5100 + firmware->size - rem;
		ret = af9015_ctrl_msg(d, &req);
		if (ret) {
			dev_err(&intf->dev, "firmware download failed %d\n",
				ret);
			goto err;
		}
	}

	req.cmd = BOOT;
	req.data_len = 0;
	ret = af9015_ctrl_msg(d, &req);
	if (ret) {
		dev_err(&intf->dev, "firmware boot failed %d\n", ret);
		goto err;
	}

	return 0;
err:
	dev_dbg(&intf->dev, "failed %d\n", ret);
	return ret;
}

#define AF9015_EEPROM_SIZE 256
/* 2^31 + 2^29 - 2^25 + 2^22 - 2^19 - 2^16 + 1 */
#define GOLDEN_RATIO_PRIME_32 0x9e370001UL

/* hash (and dump) eeprom */
static int af9015_eeprom_hash(struct dvb_usb_device *d)
{
	struct af9015_state *state = d_to_priv(d);
	struct usb_interface *intf = d->intf;
	int ret, i;
	u8 buf[AF9015_EEPROM_SIZE];
	struct req_t req = {READ_I2C, AF9015_I2C_EEPROM, 0, 0, 1, 1, NULL};

	/* read eeprom */
	for (i = 0; i < AF9015_EEPROM_SIZE; i++) {
		req.addr = i;
		req.data = &buf[i];
		ret = af9015_ctrl_msg(d, &req);
		if (ret < 0)
			goto err;
	}

	/* calculate checksum */
	for (i = 0; i < AF9015_EEPROM_SIZE / sizeof(u32); i++) {
		state->eeprom_sum *= GOLDEN_RATIO_PRIME_32;
		state->eeprom_sum += le32_to_cpu(((__le32 *)buf)[i]);
	}

	for (i = 0; i < AF9015_EEPROM_SIZE; i += 16)
		dev_dbg(&intf->dev, "%*ph\n", 16, buf + i);

	dev_dbg(&intf->dev, "eeprom sum %.8x\n", state->eeprom_sum);
	return 0;
err:
	dev_dbg(&intf->dev, "failed %d\n", ret);
	return ret;
}

static int af9015_read_config(struct dvb_usb_device *d)
{
	struct af9015_state *state = d_to_priv(d);
	struct usb_interface *intf = d->intf;
	int ret;
	u8 val, i, offset = 0;
	struct req_t req = {READ_I2C, AF9015_I2C_EEPROM, 0, 0, 1, 1, &val};

	dev_dbg(&intf->dev, "\n");

	/* IR remote controller */
	req.addr = AF9015_EEPROM_IR_MODE;
	/* first message will timeout often due to possible hw bug */
	for (i = 0; i < 4; i++) {
		ret = af9015_ctrl_msg(d, &req);
		if (!ret)
			break;
	}
	if (ret)
		goto error;

	ret = af9015_eeprom_hash(d);
	if (ret)
		goto error;

	state->ir_mode = val;
	dev_dbg(&intf->dev, "ir mode %02x\n", val);

	/* TS mode - one or two receivers */
	req.addr = AF9015_EEPROM_TS_MODE;
	ret = af9015_ctrl_msg(d, &req);
	if (ret)
		goto error;

	state->dual_mode = val;
	dev_dbg(&intf->dev, "ts mode %02x\n", state->dual_mode);

	state->af9013_i2c_addr[0] = AF9015_I2C_DEMOD;

	if (state->dual_mode) {
		/* read 2nd demodulator I2C address */
		req.addr = AF9015_EEPROM_DEMOD2_I2C;
		ret = af9015_ctrl_msg(d, &req);
		if (ret)
			goto error;

		state->af9013_i2c_addr[1] = val >> 1;
	}

	for (i = 0; i < state->dual_mode + 1; i++) {
		if (i == 1)
			offset = AF9015_EEPROM_OFFSET;
		/* xtal */
		req.addr = AF9015_EEPROM_XTAL_TYPE1 + offset;
		ret = af9015_ctrl_msg(d, &req);
		if (ret)
			goto error;
		switch (val) {
		case 0:
			state->af9013_pdata[i].clk = 28800000;
			break;
		case 1:
			state->af9013_pdata[i].clk = 20480000;
			break;
		case 2:
			state->af9013_pdata[i].clk = 28000000;
			break;
		case 3:
			state->af9013_pdata[i].clk = 25000000;
			break;
		}
		dev_dbg(&intf->dev, "[%d] xtal %02x, clk %u\n",
			i, val, state->af9013_pdata[i].clk);

		/* IF frequency */
		req.addr = AF9015_EEPROM_IF1H + offset;
		ret = af9015_ctrl_msg(d, &req);
		if (ret)
			goto error;

		state->af9013_pdata[i].if_frequency = val << 8;

		req.addr = AF9015_EEPROM_IF1L + offset;
		ret = af9015_ctrl_msg(d, &req);
		if (ret)
			goto error;

		state->af9013_pdata[i].if_frequency += val;
		state->af9013_pdata[i].if_frequency *= 1000;
		dev_dbg(&intf->dev, "[%d] if frequency %u\n",
			i, state->af9013_pdata[i].if_frequency);

		/* MT2060 IF1 */
		req.addr = AF9015_EEPROM_MT2060_IF1H  + offset;
		ret = af9015_ctrl_msg(d, &req);
		if (ret)
			goto error;
		state->mt2060_if1[i] = val << 8;
		req.addr = AF9015_EEPROM_MT2060_IF1L + offset;
		ret = af9015_ctrl_msg(d, &req);
		if (ret)
			goto error;
		state->mt2060_if1[i] += val;
		dev_dbg(&intf->dev, "[%d] MT2060 IF1 %u\n",
			i, state->mt2060_if1[i]);

		/* tuner */
		req.addr =  AF9015_EEPROM_TUNER_ID1 + offset;
		ret = af9015_ctrl_msg(d, &req);
		if (ret)
			goto error;
		switch (val) {
		case AF9013_TUNER_ENV77H11D5:
		case AF9013_TUNER_MT2060:
		case AF9013_TUNER_QT1010:
		case AF9013_TUNER_UNKNOWN:
		case AF9013_TUNER_MT2060_2:
		case AF9013_TUNER_TDA18271:
		case AF9013_TUNER_QT1010A:
		case AF9013_TUNER_TDA18218:
			state->af9013_pdata[i].spec_inv = 1;
			break;
		case AF9013_TUNER_MXL5003D:
		case AF9013_TUNER_MXL5005D:
		case AF9013_TUNER_MXL5005R:
		case AF9013_TUNER_MXL5007T:
			state->af9013_pdata[i].spec_inv = 0;
			break;
		case AF9013_TUNER_MC44S803:
			state->af9013_pdata[i].gpio[1] = AF9013_GPIO_LO;
			state->af9013_pdata[i].spec_inv = 1;
			break;
		default:
			dev_err(&intf->dev,
				"tuner id %02x not supported, please report!\n",
				val);
			return -ENODEV;
		}

		state->af9013_pdata[i].tuner = val;
		dev_dbg(&intf->dev, "[%d] tuner id %02x\n", i, val);
	}

error:
	if (ret)
		dev_err(&intf->dev, "eeprom read failed %d\n", ret);

	/*
	 * AverMedia AVerTV Volar Black HD (A850) device have bad EEPROM
	 * content :-( Override some wrong values here. Ditto for the
	 * AVerTV Red HD+ (A850T) device.
	 */
	if (le16_to_cpu(d->udev->descriptor.idVendor) == USB_VID_AVERMEDIA &&
	    ((le16_to_cpu(d->udev->descriptor.idProduct) == USB_PID_AVERMEDIA_A850) ||
	    (le16_to_cpu(d->udev->descriptor.idProduct) == USB_PID_AVERMEDIA_A850T))) {
		dev_dbg(&intf->dev, "AverMedia A850: overriding config\n");
		/* disable dual mode */
		state->dual_mode = 0;

		/* set correct IF */
		state->af9013_pdata[0].if_frequency = 4570000;
	}

	return ret;
}

static int af9015_get_stream_config(struct dvb_frontend *fe, u8 *ts_type,
				    struct usb_data_stream_properties *stream)
{
	struct dvb_usb_device *d = fe_to_d(fe);
	struct usb_interface *intf = d->intf;

	dev_dbg(&intf->dev, "adap %u\n", fe_to_adap(fe)->id);

	if (d->udev->speed == USB_SPEED_FULL)
		stream->u.bulk.buffersize = 5 * 188;

	return 0;
}

static int af9015_streaming_ctrl(struct dvb_frontend *fe, int onoff)
{
	struct dvb_usb_device *d = fe_to_d(fe);
	struct af9015_state *state = d_to_priv(d);
	struct usb_interface *intf = d->intf;
	int ret;
	unsigned int utmp1, utmp2, reg1, reg2;
	u8 buf[2];
	const unsigned int adap_id = fe_to_adap(fe)->id;

	dev_dbg(&intf->dev, "adap id %d, onoff %d\n", adap_id, onoff);

	if (!state->usb_ts_if_configured[adap_id]) {
		dev_dbg(&intf->dev, "set usb and ts interface\n");

		/* USB IF stream settings */
		utmp1 = (d->udev->speed == USB_SPEED_FULL ? 5 : 87) * 188 / 4;
		utmp2 = (d->udev->speed == USB_SPEED_FULL ? 64 : 512) / 4;

		buf[0] = (utmp1 >> 0) & 0xff;
		buf[1] = (utmp1 >> 8) & 0xff;
		if (adap_id == 0) {
			/* 1st USB IF (EP4) stream settings */
			reg1 = 0xdd88;
			reg2 = 0xdd0c;
		} else {
			/* 2nd USB IF (EP5) stream settings */
			reg1 = 0xdd8a;
			reg2 = 0xdd0d;
		}
		ret = regmap_bulk_write(state->regmap, reg1, buf, 2);
		if (ret)
			goto err;
		ret = regmap_write(state->regmap, reg2, utmp2);
		if (ret)
			goto err;

		/* TS IF settings */
		if (state->dual_mode) {
			utmp1 = 0x01;
			utmp2 = 0x10;
		} else {
			utmp1 = 0x00;
			utmp2 = 0x00;
		}
		ret = regmap_update_bits(state->regmap, 0xd50b, 0x01, utmp1);
		if (ret)
			goto err;
		ret = regmap_update_bits(state->regmap, 0xd520, 0x10, utmp2);
		if (ret)
			goto err;

		state->usb_ts_if_configured[adap_id] = true;
	}

	if (adap_id == 0 && onoff) {
		/* Adapter 0 stream on. EP4: clear NAK, enable, clear reset */
		ret = regmap_update_bits(state->regmap, 0xdd13, 0x20, 0x00);
		if (ret)
			goto err;
		ret = regmap_update_bits(state->regmap, 0xdd11, 0x20, 0x20);
		if (ret)
			goto err;
		ret = regmap_update_bits(state->regmap, 0xd507, 0x04, 0x00);
		if (ret)
			goto err;
	} else if (adap_id == 1 && onoff) {
		/* Adapter 1 stream on. EP5: clear NAK, enable, clear reset */
		ret = regmap_update_bits(state->regmap, 0xdd13, 0x40, 0x00);
		if (ret)
			goto err;
		ret = regmap_update_bits(state->regmap, 0xdd11, 0x40, 0x40);
		if (ret)
			goto err;
		ret = regmap_update_bits(state->regmap, 0xd50b, 0x02, 0x00);
		if (ret)
			goto err;
	} else if (adap_id == 0 && !onoff) {
		/* Adapter 0 stream off. EP4: set reset, disable, set NAK */
		ret = regmap_update_bits(state->regmap, 0xd507, 0x04, 0x04);
		if (ret)
			goto err;
		ret = regmap_update_bits(state->regmap, 0xdd11, 0x20, 0x00);
		if (ret)
			goto err;
		ret = regmap_update_bits(state->regmap, 0xdd13, 0x20, 0x20);
		if (ret)
			goto err;
	} else if (adap_id == 1 && !onoff) {
		/* Adapter 1 stream off. EP5: set reset, disable, set NAK */
		ret = regmap_update_bits(state->regmap, 0xd50b, 0x02, 0x02);
		if (ret)
			goto err;
		ret = regmap_update_bits(state->regmap, 0xdd11, 0x40, 0x00);
		if (ret)
			goto err;
		ret = regmap_update_bits(state->regmap, 0xdd13, 0x40, 0x40);
		if (ret)
			goto err;
	}

	return 0;
err:
	dev_dbg(&intf->dev, "failed %d\n", ret);
	return ret;
}

static int af9015_get_adapter_count(struct dvb_usb_device *d)
{
	struct af9015_state *state = d_to_priv(d);

	return state->dual_mode + 1;
}

/* override demod callbacks for resource locking */
static int af9015_af9013_set_frontend(struct dvb_frontend *fe)
{
	int ret;
	struct af9015_state *state = fe_to_priv(fe);

	if (mutex_lock_interruptible(&state->fe_mutex))
		return -EAGAIN;

	ret = state->set_frontend[fe_to_adap(fe)->id](fe);

	mutex_unlock(&state->fe_mutex);

	return ret;
}

/* override demod callbacks for resource locking */
static int af9015_af9013_read_status(struct dvb_frontend *fe,
				     enum fe_status *status)
{
	int ret;
	struct af9015_state *state = fe_to_priv(fe);

	if (mutex_lock_interruptible(&state->fe_mutex))
		return -EAGAIN;

	ret = state->read_status[fe_to_adap(fe)->id](fe, status);

	mutex_unlock(&state->fe_mutex);

	return ret;
}

/* override demod callbacks for resource locking */
static int af9015_af9013_init(struct dvb_frontend *fe)
{
	int ret;
	struct af9015_state *state = fe_to_priv(fe);

	if (mutex_lock_interruptible(&state->fe_mutex))
		return -EAGAIN;

	ret = state->init[fe_to_adap(fe)->id](fe);

	mutex_unlock(&state->fe_mutex);

	return ret;
}

/* override demod callbacks for resource locking */
static int af9015_af9013_sleep(struct dvb_frontend *fe)
{
	int ret;
	struct af9015_state *state = fe_to_priv(fe);

	if (mutex_lock_interruptible(&state->fe_mutex))
		return -EAGAIN;

	ret = state->sleep[fe_to_adap(fe)->id](fe);

	mutex_unlock(&state->fe_mutex);

	return ret;
}

/* override tuner callbacks for resource locking */
static int af9015_tuner_init(struct dvb_frontend *fe)
{
	int ret;
	struct af9015_state *state = fe_to_priv(fe);

	if (mutex_lock_interruptible(&state->fe_mutex))
		return -EAGAIN;

	ret = state->tuner_init[fe_to_adap(fe)->id](fe);

	mutex_unlock(&state->fe_mutex);

	return ret;
}

/* override tuner callbacks for resource locking */
static int af9015_tuner_sleep(struct dvb_frontend *fe)
{
	int ret;
	struct af9015_state *state = fe_to_priv(fe);

	if (mutex_lock_interruptible(&state->fe_mutex))
		return -EAGAIN;

	ret = state->tuner_sleep[fe_to_adap(fe)->id](fe);

	mutex_unlock(&state->fe_mutex);

	return ret;
}

static int af9015_copy_firmware(struct dvb_usb_device *d)
{
	struct af9015_state *state = d_to_priv(d);
	struct usb_interface *intf = d->intf;
	int ret;
	unsigned long timeout;
	u8 val, firmware_info[4];
	struct req_t req = {COPY_FIRMWARE, 0, 0x5100, 0, 0, 4, firmware_info};

	dev_dbg(&intf->dev, "\n");

	firmware_info[0] = (state->firmware_size >> 8) & 0xff;
	firmware_info[1] = (state->firmware_size >> 0) & 0xff;
	firmware_info[2] = (state->firmware_checksum >> 8) & 0xff;
	firmware_info[3] = (state->firmware_checksum >> 0) & 0xff;

	/* Check whether firmware is already running */
	ret = af9015_read_reg_i2c(d, state->af9013_i2c_addr[1], 0x98be, &val);
	if (ret)
		goto err;

	dev_dbg(&intf->dev, "firmware status %02x\n", val);

	if (val == 0x0c)
		return 0;

	/* Set i2c clock to 625kHz to speed up firmware copy */
	ret = regmap_write(state->regmap, 0xd416, 0x04);
	if (ret)
		goto err;

	/* Copy firmware from master demod to slave demod */
	ret = af9015_ctrl_msg(d, &req);
	if (ret) {
		dev_err(&intf->dev, "firmware copy cmd failed %d\n", ret);
		goto err;
	}

	/* Set i2c clock to 125kHz */
	ret = regmap_write(state->regmap, 0xd416, 0x14);
	if (ret)
		goto err;

	/* Boot firmware */
	ret = af9015_write_reg_i2c(d, state->af9013_i2c_addr[1], 0xe205, 0x01);
	if (ret)
		goto err;

	/* Poll firmware ready */
	for (val = 0x00, timeout = jiffies + msecs_to_jiffies(1000);
	     !time_after(jiffies, timeout) && val != 0x0c && val != 0x04;) {
		msleep(20);

		/* Check firmware status. 0c=OK, 04=fail */
		ret = af9015_read_reg_i2c(d, state->af9013_i2c_addr[1],
					  0x98be, &val);
		if (ret)
			goto err;

		dev_dbg(&intf->dev, "firmware status %02x\n", val);
	}

	dev_dbg(&intf->dev, "firmware boot took %u ms\n",
		jiffies_to_msecs(jiffies) - (jiffies_to_msecs(timeout) - 1000));

	if (val == 0x04) {
		ret = -ENODEV;
		dev_err(&intf->dev, "firmware did not run\n");
		goto err;
	} else if (val != 0x0c) {
		ret = -ETIMEDOUT;
		dev_err(&intf->dev, "firmware boot timeout\n");
		goto err;
	}

	return 0;
err:
	dev_dbg(&intf->dev, "failed %d\n", ret);
	return ret;
}

static int af9015_af9013_frontend_attach(struct dvb_usb_adapter *adap)
{
	struct af9015_state *state = adap_to_priv(adap);
	struct dvb_usb_device *d = adap_to_d(adap);
	struct usb_interface *intf = d->intf;
	struct i2c_client *client;
	int ret;

	dev_dbg(&intf->dev, "adap id %u\n", adap->id);

	if (adap->id == 0) {
		state->af9013_pdata[0].ts_mode = AF9013_TS_MODE_USB;
		memcpy(state->af9013_pdata[0].api_version, "\x0\x1\x9\x0", 4);
		state->af9013_pdata[0].gpio[0] = AF9013_GPIO_HI;
		state->af9013_pdata[0].gpio[3] = AF9013_GPIO_TUNER_ON;
	} else if (adap->id == 1) {
		state->af9013_pdata[1].ts_mode = AF9013_TS_MODE_SERIAL;
		state->af9013_pdata[1].ts_output_pin = 7;
		memcpy(state->af9013_pdata[1].api_version, "\x0\x1\x9\x0", 4);
		state->af9013_pdata[1].gpio[0] = AF9013_GPIO_TUNER_ON;
		state->af9013_pdata[1].gpio[1] = AF9013_GPIO_LO;

		/* copy firmware to 2nd demodulator */
		if (state->dual_mode) {
			/* Wait 2nd demodulator ready */
			msleep(100);

			ret = af9015_copy_firmware(adap_to_d(adap));
			if (ret) {
				dev_err(&intf->dev,
					"firmware copy to 2nd frontend failed, will disable it\n");
				state->dual_mode = 0;
				goto err;
			}
		} else {
			ret = -ENODEV;
			goto err;
		}
	}

	/* Add I2C demod */
	client = dvb_module_probe("af9013", NULL, &d->i2c_adap,
				  state->af9013_i2c_addr[adap->id],
				  &state->af9013_pdata[adap->id]);
	if (!client) {
		ret = -ENODEV;
		goto err;
	}
	adap->fe[0] = state->af9013_pdata[adap->id].get_dvb_frontend(client);
	state->demod_i2c_client[adap->id] = client;

	/*
	 * AF9015 firmware does not like if it gets interrupted by I2C adapter
	 * request on some critical phases. During normal operation I2C adapter
	 * is used only 2nd demodulator and tuner on dual tuner devices.
	 * Override demodulator callbacks and use mutex for limit access to
	 * those "critical" paths to keep AF9015 happy.
	 */
	if (adap->fe[0]) {
		state->set_frontend[adap->id] = adap->fe[0]->ops.set_frontend;
		adap->fe[0]->ops.set_frontend = af9015_af9013_set_frontend;
		state->read_status[adap->id] = adap->fe[0]->ops.read_status;
		adap->fe[0]->ops.read_status = af9015_af9013_read_status;
		state->init[adap->id] = adap->fe[0]->ops.init;
		adap->fe[0]->ops.init = af9015_af9013_init;
		state->sleep[adap->id] = adap->fe[0]->ops.sleep;
		adap->fe[0]->ops.sleep = af9015_af9013_sleep;
	}

	return 0;
err:
	dev_dbg(&intf->dev, "failed %d\n", ret);
	return ret;
}

static int af9015_frontend_detach(struct dvb_usb_adapter *adap)
{
	struct af9015_state *state = adap_to_priv(adap);
	struct dvb_usb_device *d = adap_to_d(adap);
	struct usb_interface *intf = d->intf;
	struct i2c_client *client;

	dev_dbg(&intf->dev, "adap id %u\n", adap->id);

	/* Remove I2C demod */
	client = state->demod_i2c_client[adap->id];
	dvb_module_release(client);

	return 0;
}

static struct mt2060_config af9015_mt2060_config = {
	.i2c_address = 0x60,
	.clock_out = 0,
};

static struct qt1010_config af9015_qt1010_config = {
	.i2c_address = 0x62,
};

static struct tda18271_config af9015_tda18271_config = {
	.gate = TDA18271_GATE_DIGITAL,
	.small_i2c = TDA18271_16_BYTE_CHUNK_INIT,
};

static struct mxl5005s_config af9015_mxl5003_config = {
	.i2c_address     = 0x63,
	.if_freq         = IF_FREQ_4570000HZ,
	.xtal_freq       = CRYSTAL_FREQ_16000000HZ,
	.agc_mode        = MXL_SINGLE_AGC,
	.tracking_filter = MXL_TF_DEFAULT,
	.rssi_enable     = MXL_RSSI_ENABLE,
	.cap_select      = MXL_CAP_SEL_ENABLE,
	.div_out         = MXL_DIV_OUT_4,
	.clock_out       = MXL_CLOCK_OUT_DISABLE,
	.output_load     = MXL5005S_IF_OUTPUT_LOAD_200_OHM,
	.top		 = MXL5005S_TOP_25P2,
	.mod_mode        = MXL_DIGITAL_MODE,
	.if_mode         = MXL_ZERO_IF,
	.AgcMasterByte   = 0x00,
};

static struct mxl5005s_config af9015_mxl5005_config = {
	.i2c_address     = 0x63,
	.if_freq         = IF_FREQ_4570000HZ,
	.xtal_freq       = CRYSTAL_FREQ_16000000HZ,
	.agc_mode        = MXL_SINGLE_AGC,
	.tracking_filter = MXL_TF_OFF,
	.rssi_enable     = MXL_RSSI_ENABLE,
	.cap_select      = MXL_CAP_SEL_ENABLE,
	.div_out         = MXL_DIV_OUT_4,
	.clock_out       = MXL_CLOCK_OUT_DISABLE,
	.output_load     = MXL5005S_IF_OUTPUT_LOAD_200_OHM,
	.top		 = MXL5005S_TOP_25P2,
	.mod_mode        = MXL_DIGITAL_MODE,
	.if_mode         = MXL_ZERO_IF,
	.AgcMasterByte   = 0x00,
};

static struct mc44s803_config af9015_mc44s803_config = {
	.i2c_address = 0x60,
	.dig_out = 1,
};

static struct tda18218_config af9015_tda18218_config = {
	.i2c_address = 0x60,
	.i2c_wr_max = 21, /* max wr bytes AF9015 I2C adap can handle at once */
};

static struct mxl5007t_config af9015_mxl5007t_config = {
	.xtal_freq_hz = MxL_XTAL_24_MHZ,
	.if_freq_hz = MxL_IF_4_57_MHZ,
};

static int af9015_tuner_attach(struct dvb_usb_adapter *adap)
{
	struct dvb_usb_device *d = adap_to_d(adap);
	struct af9015_state *state = d_to_priv(d);
	struct usb_interface *intf = d->intf;
	struct i2c_client *client;
	struct i2c_adapter *adapter;
	int ret;

	dev_dbg(&intf->dev, "adap id %u\n", adap->id);

	client = state->demod_i2c_client[adap->id];
	adapter = state->af9013_pdata[adap->id].get_i2c_adapter(client);

	switch (state->af9013_pdata[adap->id].tuner) {
	case AF9013_TUNER_MT2060:
	case AF9013_TUNER_MT2060_2:
		ret = dvb_attach(mt2060_attach, adap->fe[0], adapter,
				 &af9015_mt2060_config,
				 state->mt2060_if1[adap->id]) == NULL ? -ENODEV : 0;
		break;
	case AF9013_TUNER_QT1010:
	case AF9013_TUNER_QT1010A:
		ret = dvb_attach(qt1010_attach, adap->fe[0], adapter,
				 &af9015_qt1010_config) == NULL ? -ENODEV : 0;
		break;
	case AF9013_TUNER_TDA18271:
		ret = dvb_attach(tda18271_attach, adap->fe[0], 0x60, adapter,
				 &af9015_tda18271_config) == NULL ? -ENODEV : 0;
		break;
	case AF9013_TUNER_TDA18218:
		ret = dvb_attach(tda18218_attach, adap->fe[0], adapter,
				 &af9015_tda18218_config) == NULL ? -ENODEV : 0;
		break;
	case AF9013_TUNER_MXL5003D:
		ret = dvb_attach(mxl5005s_attach, adap->fe[0], adapter,
				 &af9015_mxl5003_config) == NULL ? -ENODEV : 0;
		break;
	case AF9013_TUNER_MXL5005D:
	case AF9013_TUNER_MXL5005R:
		ret = dvb_attach(mxl5005s_attach, adap->fe[0], adapter,
				 &af9015_mxl5005_config) == NULL ? -ENODEV : 0;
		break;
	case AF9013_TUNER_ENV77H11D5:
		ret = dvb_attach(dvb_pll_attach, adap->fe[0], 0x60, adapter,
				 DVB_PLL_TDA665X) == NULL ? -ENODEV : 0;
		break;
	case AF9013_TUNER_MC44S803:
		ret = dvb_attach(mc44s803_attach, adap->fe[0], adapter,
				 &af9015_mc44s803_config) == NULL ? -ENODEV : 0;
		break;
	case AF9013_TUNER_MXL5007T:
		ret = dvb_attach(mxl5007t_attach, adap->fe[0], adapter,
				 0x60, &af9015_mxl5007t_config) == NULL ? -ENODEV : 0;
		break;
	case AF9013_TUNER_UNKNOWN:
	default:
		dev_err(&intf->dev, "unknown tuner, tuner id %02x\n",
			state->af9013_pdata[adap->id].tuner);
		ret = -ENODEV;
	}

	if (adap->fe[0]->ops.tuner_ops.init) {
		state->tuner_init[adap->id] =
			adap->fe[0]->ops.tuner_ops.init;
		adap->fe[0]->ops.tuner_ops.init = af9015_tuner_init;
	}

	if (adap->fe[0]->ops.tuner_ops.sleep) {
		state->tuner_sleep[adap->id] =
			adap->fe[0]->ops.tuner_ops.sleep;
		adap->fe[0]->ops.tuner_ops.sleep = af9015_tuner_sleep;
	}

	return ret;
}

static int af9015_pid_filter_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	struct af9015_state *state = adap_to_priv(adap);
	struct af9013_platform_data *pdata = &state->af9013_pdata[adap->id];
	int ret;

	mutex_lock(&state->fe_mutex);
	ret = pdata->pid_filter_ctrl(adap->fe[0], onoff);
	mutex_unlock(&state->fe_mutex);

	return ret;
}

static int af9015_pid_filter(struct dvb_usb_adapter *adap, int index,
			     u16 pid, int onoff)
{
	struct af9015_state *state = adap_to_priv(adap);
	struct af9013_platform_data *pdata = &state->af9013_pdata[adap->id];
	int ret;

	mutex_lock(&state->fe_mutex);
	ret = pdata->pid_filter(adap->fe[0], index, pid, onoff);
	mutex_unlock(&state->fe_mutex);

	return ret;
}

static int af9015_init(struct dvb_usb_device *d)
{
	struct af9015_state *state = d_to_priv(d);
	struct usb_interface *intf = d->intf;
	int ret;

	dev_dbg(&intf->dev, "\n");

	mutex_init(&state->fe_mutex);

	/* init RC canary */
	ret = regmap_write(state->regmap, 0x98e9, 0xff);
	if (ret)
		goto error;

error:
	return ret;
}

#if IS_ENABLED(CONFIG_RC_CORE)
struct af9015_rc_setup {
	unsigned int id;
	char *rc_codes;
};

static char *af9015_rc_setup_match(unsigned int id,
				   const struct af9015_rc_setup *table)
{
	for (; table->rc_codes; table++)
		if (table->id == id)
			return table->rc_codes;
	return NULL;
}

static const struct af9015_rc_setup af9015_rc_setup_modparam[] = {
	{ AF9015_REMOTE_A_LINK_DTU_M, RC_MAP_ALINK_DTU_M },
	{ AF9015_REMOTE_MSI_DIGIVOX_MINI_II_V3, RC_MAP_MSI_DIGIVOX_II },
	{ AF9015_REMOTE_MYGICTV_U718, RC_MAP_TOTAL_MEDIA_IN_HAND },
	{ AF9015_REMOTE_DIGITTRADE_DVB_T, RC_MAP_DIGITTRADE },
	{ AF9015_REMOTE_AVERMEDIA_KS, RC_MAP_AVERMEDIA_RM_KS },
	{ }
};

static const struct af9015_rc_setup af9015_rc_setup_hashes[] = {
	{ 0xb8feb708, RC_MAP_MSI_DIGIVOX_II },
	{ 0xa3703d00, RC_MAP_ALINK_DTU_M },
	{ 0x9b7dc64e, RC_MAP_TOTAL_MEDIA_IN_HAND }, /* MYGICTV U718 */
	{ 0x5d49e3db, RC_MAP_DIGITTRADE }, /* LC-Power LC-USB-DVBT */
	{ }
};

static int af9015_rc_query(struct dvb_usb_device *d)
{
	struct af9015_state *state = d_to_priv(d);
	struct usb_interface *intf = d->intf;
	int ret;
	u8 buf[17];

	/* read registers needed to detect remote controller code */
	ret = regmap_bulk_read(state->regmap, 0x98d9, buf, sizeof(buf));
	if (ret)
		goto error;

	/* If any of these are non-zero, assume invalid data */
	if (buf[1] || buf[2] || buf[3]) {
		dev_dbg(&intf->dev, "invalid data\n");
		return ret;
	}

	/* Check for repeat of previous code */
	if ((state->rc_repeat != buf[6] || buf[0]) &&
	    !memcmp(&buf[12], state->rc_last, 4)) {
		dev_dbg(&intf->dev, "key repeated\n");
		rc_repeat(d->rc_dev);
		state->rc_repeat = buf[6];
		return ret;
	}

	/* Only process key if canary killed */
	if (buf[16] != 0xff && buf[0] != 0x01) {
		enum rc_proto proto;

		dev_dbg(&intf->dev, "key pressed %*ph\n", 4, buf + 12);

		/* Reset the canary */
		ret = regmap_write(state->regmap, 0x98e9, 0xff);
		if (ret)
			goto error;

		/* Remember this key */
		memcpy(state->rc_last, &buf[12], 4);
		if (buf[14] == (u8)~buf[15]) {
			if (buf[12] == (u8)~buf[13]) {
				/* NEC */
				state->rc_keycode = RC_SCANCODE_NEC(buf[12],
								    buf[14]);
				proto = RC_PROTO_NEC;
			} else {
				/* NEC extended*/
				state->rc_keycode = RC_SCANCODE_NECX(buf[12] << 8 |
								     buf[13],
								     buf[14]);
				proto = RC_PROTO_NECX;
			}
		} else {
			/* 32 bit NEC */
			state->rc_keycode = RC_SCANCODE_NEC32(buf[12] << 24 |
							      buf[13] << 16 |
							      buf[14] << 8  |
							      buf[15]);
			proto = RC_PROTO_NEC32;
		}
		rc_keydown(d->rc_dev, proto, state->rc_keycode, 0);
	} else {
		dev_dbg(&intf->dev, "no key press\n");
		/* Invalidate last keypress */
		/* Not really needed, but helps with debug */
		state->rc_last[2] = state->rc_last[3];
	}

	state->rc_repeat = buf[6];
	state->rc_failed = false;

error:
	if (ret) {
		dev_warn(&intf->dev, "rc query failed %d\n", ret);

		/* allow random errors as dvb-usb will stop polling on error */
		if (!state->rc_failed)
			ret = 0;

		state->rc_failed = true;
	}

	return ret;
}

static int af9015_get_rc_config(struct dvb_usb_device *d, struct dvb_usb_rc *rc)
{
	struct af9015_state *state = d_to_priv(d);
	u16 vid = le16_to_cpu(d->udev->descriptor.idVendor);

	if (state->ir_mode == AF9015_IR_MODE_DISABLED)
		return 0;

	/* try to load remote based module param */
	if (!rc->map_name)
		rc->map_name = af9015_rc_setup_match(dvb_usb_af9015_remote,
						     af9015_rc_setup_modparam);

	/* try to load remote based eeprom hash */
	if (!rc->map_name)
		rc->map_name = af9015_rc_setup_match(state->eeprom_sum,
						     af9015_rc_setup_hashes);

	/* try to load remote based USB iManufacturer string */
	if (!rc->map_name && vid == USB_VID_AFATECH) {
		/*
		 * Check USB manufacturer and product strings and try
		 * to determine correct remote in case of chip vendor
		 * reference IDs are used.
		 * DO NOT ADD ANYTHING NEW HERE. Use hashes instead.
		 */
		char manufacturer[10];

		memset(manufacturer, 0, sizeof(manufacturer));
		usb_string(d->udev, d->udev->descriptor.iManufacturer,
			   manufacturer, sizeof(manufacturer));
		if (!strcmp("MSI", manufacturer)) {
			/*
			 * iManufacturer 1 MSI
			 * iProduct      2 MSI K-VOX
			 */
			rc->map_name = af9015_rc_setup_match(AF9015_REMOTE_MSI_DIGIVOX_MINI_II_V3,
							     af9015_rc_setup_modparam);
		}
	}

	/* load empty to enable rc */
	if (!rc->map_name)
		rc->map_name = RC_MAP_EMPTY;

	rc->allowed_protos = RC_PROTO_BIT_NEC | RC_PROTO_BIT_NECX |
						RC_PROTO_BIT_NEC32;
	rc->query = af9015_rc_query;
	rc->interval = 500;

	return 0;
}
#else
	#define af9015_get_rc_config NULL
#endif

static int af9015_regmap_write(void *context, const void *data, size_t count)
{
	struct dvb_usb_device *d = context;
	struct usb_interface *intf = d->intf;
	int ret;
	u16 reg = ((u8 *)data)[0] << 8 | ((u8 *)data)[1] << 0;
	u8 *val = &((u8 *)data)[2];
	const unsigned int len = count - 2;
	struct req_t req = {WRITE_MEMORY, 0, reg, 0, 0, len, val};

	ret = af9015_ctrl_msg(d, &req);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&intf->dev, "failed %d\n", ret);
	return ret;
}

static int af9015_regmap_read(void *context, const void *reg_buf,
			      size_t reg_size, void *val_buf, size_t val_size)
{
	struct dvb_usb_device *d = context;
	struct usb_interface *intf = d->intf;
	int ret;
	u16 reg = ((u8 *)reg_buf)[0] << 8 | ((u8 *)reg_buf)[1] << 0;
	u8 *val = &((u8 *)val_buf)[0];
	const unsigned int len = val_size;
	struct req_t req = {READ_MEMORY, 0, reg, 0, 0, len, val};

	ret = af9015_ctrl_msg(d, &req);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&intf->dev, "failed %d\n", ret);
	return ret;
}

static int af9015_probe(struct dvb_usb_device *d)
{
	struct af9015_state *state = d_to_priv(d);
	struct usb_interface *intf = d->intf;
	struct usb_device *udev = interface_to_usbdev(intf);
	int ret;
	char manufacturer[sizeof("ITE Technologies, Inc.")];
	static const struct regmap_config regmap_config = {
		.reg_bits    =  16,
		.val_bits    =  8,
	};
	static const struct regmap_bus regmap_bus = {
		.read = af9015_regmap_read,
		.write = af9015_regmap_write,
	};

	dev_dbg(&intf->dev, "\n");

	memset(manufacturer, 0, sizeof(manufacturer));
	usb_string(udev, udev->descriptor.iManufacturer,
		   manufacturer, sizeof(manufacturer));
	/*
	 * There is two devices having same ID but different chipset. One uses
	 * AF9015 and the other IT9135 chipset. Only difference seen on lsusb
	 * is iManufacturer string.
	 *
	 * idVendor           0x0ccd TerraTec Electronic GmbH
	 * idProduct          0x0099
	 * bcdDevice            2.00
	 * iManufacturer           1 Afatech
	 * iProduct                2 DVB-T 2
	 *
	 * idVendor           0x0ccd TerraTec Electronic GmbH
	 * idProduct          0x0099
	 * bcdDevice            2.00
	 * iManufacturer           1 ITE Technologies, Inc.
	 * iProduct                2 DVB-T TV Stick
	 */
	if ((le16_to_cpu(udev->descriptor.idVendor) == USB_VID_TERRATEC) &&
	    (le16_to_cpu(udev->descriptor.idProduct) == 0x0099)) {
		if (!strcmp("ITE Technologies, Inc.", manufacturer)) {
			ret = -ENODEV;
			dev_dbg(&intf->dev, "rejecting device\n");
			goto err;
		}
	}

	state->regmap = regmap_init(&intf->dev, &regmap_bus, d, &regmap_config);
	if (IS_ERR(state->regmap)) {
		ret = PTR_ERR(state->regmap);
		goto err;
	}

	return 0;
err:
	dev_dbg(&intf->dev, "failed %d\n", ret);
	return ret;
}

static void af9015_disconnect(struct dvb_usb_device *d)
{
	struct af9015_state *state = d_to_priv(d);
	struct usb_interface *intf = d->intf;

	dev_dbg(&intf->dev, "\n");

	regmap_exit(state->regmap);
}

/*
 * Interface 0 is used by DVB-T receiver and
 * interface 1 is for remote controller (HID)
 */
static const struct dvb_usb_device_properties af9015_props = {
	.driver_name = KBUILD_MODNAME,
	.owner = THIS_MODULE,
	.adapter_nr = adapter_nr,
	.size_of_priv = sizeof(struct af9015_state),

	.generic_bulk_ctrl_endpoint = 0x02,
	.generic_bulk_ctrl_endpoint_response = 0x81,

	.probe = af9015_probe,
	.disconnect = af9015_disconnect,
	.identify_state = af9015_identify_state,
	.firmware = AF9015_FIRMWARE,
	.download_firmware = af9015_download_firmware,

	.i2c_algo = &af9015_i2c_algo,
	.read_config = af9015_read_config,
	.frontend_attach = af9015_af9013_frontend_attach,
	.frontend_detach = af9015_frontend_detach,
	.tuner_attach = af9015_tuner_attach,
	.init = af9015_init,
	.get_rc_config = af9015_get_rc_config,
	.get_stream_config = af9015_get_stream_config,
	.streaming_ctrl = af9015_streaming_ctrl,

	.get_adapter_count = af9015_get_adapter_count,
	.adapter = {
		{
			.caps = DVB_USB_ADAP_HAS_PID_FILTER |
				DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
			.pid_filter_count = 32,
			.pid_filter = af9015_pid_filter,
			.pid_filter_ctrl = af9015_pid_filter_ctrl,

			.stream = DVB_USB_STREAM_BULK(0x84, 6, 87 * 188),
		}, {
			.caps = DVB_USB_ADAP_HAS_PID_FILTER |
				DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
			.pid_filter_count = 32,
			.pid_filter = af9015_pid_filter,
			.pid_filter_ctrl = af9015_pid_filter_ctrl,

			.stream = DVB_USB_STREAM_BULK(0x85, 6, 87 * 188),
		},
	},
};

static const struct usb_device_id af9015_id_table[] = {
	{ DVB_USB_DEVICE(USB_VID_AFATECH, USB_PID_AFATECH_AF9015_9015,
		&af9015_props, "Afatech AF9015 reference design", NULL) },
	{ DVB_USB_DEVICE(USB_VID_AFATECH, USB_PID_AFATECH_AF9015_9016,
		&af9015_props, "Afatech AF9015 reference design", NULL) },
	{ DVB_USB_DEVICE(USB_VID_LEADTEK, USB_PID_WINFAST_DTV_DONGLE_GOLD,
		&af9015_props, "Leadtek WinFast DTV Dongle Gold", RC_MAP_LEADTEK_Y04G0051) },
	{ DVB_USB_DEVICE(USB_VID_PINNACLE, USB_PID_PINNACLE_PCTV71E,
		&af9015_props, "Pinnacle PCTV 71e", NULL) },
	{ DVB_USB_DEVICE(USB_VID_KWORLD_2, USB_PID_KWORLD_399U,
		&af9015_props, "KWorld PlusTV Dual DVB-T Stick (DVB-T 399U)", NULL) },
	{ DVB_USB_DEVICE(USB_VID_VISIONPLUS, USB_PID_TINYTWIN,
		&af9015_props, "DigitalNow TinyTwin", RC_MAP_AZUREWAVE_AD_TU700) },
	{ DVB_USB_DEVICE(USB_VID_VISIONPLUS, USB_PID_AZUREWAVE_AD_TU700,
		&af9015_props, "TwinHan AzureWave AD-TU700(704J)", RC_MAP_AZUREWAVE_AD_TU700) },
	{ DVB_USB_DEVICE(USB_VID_TERRATEC, USB_PID_TERRATEC_CINERGY_T_USB_XE_REV2,
		&af9015_props, "TerraTec Cinergy T USB XE", NULL) },
	{ DVB_USB_DEVICE(USB_VID_KWORLD_2, USB_PID_KWORLD_PC160_2T,
		&af9015_props, "KWorld PlusTV Dual DVB-T PCI (DVB-T PC160-2T)", NULL) },
	{ DVB_USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_VOLAR_X,
		&af9015_props, "AVerMedia AVerTV DVB-T Volar X", RC_MAP_AVERMEDIA_M135A) },
	{ DVB_USB_DEVICE(USB_VID_XTENSIONS, USB_PID_XTENSIONS_XD_380,
		&af9015_props, "Xtensions XD-380", NULL) },
	{ DVB_USB_DEVICE(USB_VID_MSI_2, USB_PID_MSI_DIGIVOX_DUO,
		&af9015_props, "MSI DIGIVOX Duo", RC_MAP_MSI_DIGIVOX_III) },
	{ DVB_USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_VOLAR_X_2,
		&af9015_props, "Fujitsu-Siemens Slim Mobile USB DVB-T", NULL) },
	{ DVB_USB_DEVICE(USB_VID_TELESTAR,  USB_PID_TELESTAR_STARSTICK_2,
		&af9015_props, "Telestar Starstick 2", NULL) },
	{ DVB_USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_A309,
		&af9015_props, "AVerMedia A309", NULL) },
	{ DVB_USB_DEVICE(USB_VID_MSI_2, USB_PID_MSI_DIGI_VOX_MINI_III,
		&af9015_props, "MSI Digi VOX mini III", RC_MAP_MSI_DIGIVOX_III) },
	{ DVB_USB_DEVICE(USB_VID_KWORLD_2, USB_PID_KWORLD_395U,
		&af9015_props, "KWorld USB DVB-T TV Stick II (VS-DVB-T 395U)", NULL) },
	{ DVB_USB_DEVICE(USB_VID_KWORLD_2, USB_PID_KWORLD_395U_2,
		&af9015_props, "KWorld USB DVB-T TV Stick II (VS-DVB-T 395U)", NULL) },
	{ DVB_USB_DEVICE(USB_VID_KWORLD_2, USB_PID_KWORLD_395U_3,
		&af9015_props, "KWorld USB DVB-T TV Stick II (VS-DVB-T 395U)", NULL) },
	{ DVB_USB_DEVICE(USB_VID_AFATECH, USB_PID_TREKSTOR_DVBT,
		&af9015_props, "TrekStor DVB-T USB Stick", RC_MAP_TREKSTOR) },
	{ DVB_USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_A850,
		&af9015_props, "AverMedia AVerTV Volar Black HD (A850)", NULL) },
	{ DVB_USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_A805,
		&af9015_props, "AverMedia AVerTV Volar GPS 805 (A805)", NULL) },
	{ DVB_USB_DEVICE(USB_VID_KWORLD_2, USB_PID_CONCEPTRONIC_CTVDIGRCU,
		&af9015_props, "Conceptronic USB2.0 DVB-T CTVDIGRCU V3.0", NULL) },
	{ DVB_USB_DEVICE(USB_VID_KWORLD_2, USB_PID_KWORLD_MC810,
		&af9015_props, "KWorld Digital MC-810", NULL) },
	{ DVB_USB_DEVICE(USB_VID_KYE, USB_PID_GENIUS_TVGO_DVB_T03,
		&af9015_props, "Genius TVGo DVB-T03", NULL) },
	{ DVB_USB_DEVICE(USB_VID_KWORLD_2, USB_PID_KWORLD_399U_2,
		&af9015_props, "KWorld PlusTV Dual DVB-T Stick (DVB-T 399U)", NULL) },
	{ DVB_USB_DEVICE(USB_VID_KWORLD_2, USB_PID_KWORLD_PC160_T,
		&af9015_props, "KWorld PlusTV DVB-T PCI Pro Card (DVB-T PC160-T)", NULL) },
	{ DVB_USB_DEVICE(USB_VID_KWORLD_2, USB_PID_SVEON_STV20,
		&af9015_props, "Sveon STV20 Tuner USB DVB-T HDTV", NULL) },
	{ DVB_USB_DEVICE(USB_VID_KWORLD_2, USB_PID_TINYTWIN_2,
		&af9015_props, "DigitalNow TinyTwin v2", RC_MAP_DIGITALNOW_TINYTWIN) },
	{ DVB_USB_DEVICE(USB_VID_LEADTEK, USB_PID_WINFAST_DTV2000DS,
		&af9015_props, "Leadtek WinFast DTV2000DS", RC_MAP_LEADTEK_Y04G0051) },
	{ DVB_USB_DEVICE(USB_VID_KWORLD_2, USB_PID_KWORLD_UB383_T,
		&af9015_props, "KWorld USB DVB-T Stick Mobile (UB383-T)", NULL) },
	{ DVB_USB_DEVICE(USB_VID_KWORLD_2, USB_PID_KWORLD_395U_4,
		&af9015_props, "KWorld USB DVB-T TV Stick II (VS-DVB-T 395U)", NULL) },
	{ DVB_USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_A815M,
		&af9015_props, "AverMedia AVerTV Volar M (A815Mac)", NULL) },
	{ DVB_USB_DEVICE(USB_VID_TERRATEC, USB_PID_TERRATEC_CINERGY_T_STICK_RC,
		&af9015_props, "TerraTec Cinergy T Stick RC", RC_MAP_TERRATEC_SLIM_2) },
	/* XXX: that same ID [0ccd:0099] is used by af9035 driver too */
	{ DVB_USB_DEVICE(USB_VID_TERRATEC, USB_PID_TERRATEC_CINERGY_T_STICK_DUAL_RC,
		&af9015_props, "TerraTec Cinergy T Stick Dual RC", RC_MAP_TERRATEC_SLIM) },
	{ DVB_USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_A850T,
		&af9015_props, "AverMedia AVerTV Red HD+ (A850T)", NULL) },
	{ DVB_USB_DEVICE(USB_VID_GTEK, USB_PID_TINYTWIN_3,
		&af9015_props, "DigitalNow TinyTwin v3", RC_MAP_DIGITALNOW_TINYTWIN) },
	{ DVB_USB_DEVICE(USB_VID_KWORLD_2, USB_PID_SVEON_STV22,
		&af9015_props, "Sveon STV22 Dual USB DVB-T Tuner HDTV", RC_MAP_MSI_DIGIVOX_III) },
	{ }
};
MODULE_DEVICE_TABLE(usb, af9015_id_table);

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver af9015_usb_driver = {
	.name = KBUILD_MODNAME,
	.id_table = af9015_id_table,
	.probe = dvb_usbv2_probe,
	.disconnect = dvb_usbv2_disconnect,
	.suspend = dvb_usbv2_suspend,
	.resume = dvb_usbv2_resume,
	.reset_resume = dvb_usbv2_reset_resume,
	.no_dynamic_id = 1,
	.soft_unbind = 1,
};

module_usb_driver(af9015_usb_driver);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Afatech AF9015 driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(AF9015_FIRMWARE);
