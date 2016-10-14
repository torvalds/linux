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
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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
	int ret, wlen, rlen;
	u8 write = 1;

	mutex_lock(&d->usb_mutex);

	state->buf[0] = req->cmd;
	state->buf[1] = state->seq++;
	state->buf[2] = req->i2c_addr;
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
		dev_err(&d->udev->dev, "%s: unknown command=%d\n",
				KBUILD_MODNAME, req->cmd);
		ret = -EIO;
		goto error;
	}

	/* buffer overflow check */
	if ((write && (req->data_len > BUF_LEN - REQ_HDR_LEN)) ||
			(!write && (req->data_len > BUF_LEN - ACK_HDR_LEN))) {
		dev_err(&d->udev->dev, "%s: too much data; cmd=%d len=%d\n",
				KBUILD_MODNAME, req->cmd, req->data_len);
		ret = -EINVAL;
		goto error;
	}

	/* write receives seq + status = 2 bytes
	   read receives seq + status + data = 2 + N bytes */
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

	ret = dvb_usbv2_generic_rw_locked(d,
			state->buf, wlen, state->buf, rlen);
	if (ret)
		goto error;

	/* check status */
	if (rlen && state->buf[1]) {
		dev_err(&d->udev->dev, "%s: command failed=%d\n",
				KBUILD_MODNAME, state->buf[1]);
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

static int af9015_write_regs(struct dvb_usb_device *d, u16 addr, u8 *val,
	u8 len)
{
	struct req_t req = {WRITE_MEMORY, AF9015_I2C_DEMOD, addr, 0, 0, len,
		val};
	return af9015_ctrl_msg(d, &req);
}

static int af9015_read_regs(struct dvb_usb_device *d, u16 addr, u8 *val, u8 len)
{
	struct req_t req = {READ_MEMORY, AF9015_I2C_DEMOD, addr, 0, 0, len,
		val};
	return af9015_ctrl_msg(d, &req);
}

static int af9015_write_reg(struct dvb_usb_device *d, u16 addr, u8 val)
{
	return af9015_write_regs(d, addr, &val, 1);
}

static int af9015_read_reg(struct dvb_usb_device *d, u16 addr, u8 *val)
{
	return af9015_read_regs(d, addr, val, 1);
}

static int af9015_write_reg_i2c(struct dvb_usb_device *d, u8 addr, u16 reg,
	u8 val)
{
	struct af9015_state *state = d_to_priv(d);
	struct req_t req = {WRITE_I2C, addr, reg, 1, 1, 1, &val};

	if (addr == state->af9013_config[0].i2c_addr ||
	    addr == state->af9013_config[1].i2c_addr)
		req.addr_len = 3;

	return af9015_ctrl_msg(d, &req);
}

static int af9015_read_reg_i2c(struct dvb_usb_device *d, u8 addr, u16 reg,
	u8 *val)
{
	struct af9015_state *state = d_to_priv(d);
	struct req_t req = {READ_I2C, addr, reg, 0, 1, 1, val};

	if (addr == state->af9013_config[0].i2c_addr ||
	    addr == state->af9013_config[1].i2c_addr)
		req.addr_len = 3;

	return af9015_ctrl_msg(d, &req);
}

static int af9015_do_reg_bit(struct dvb_usb_device *d, u16 addr, u8 bit, u8 op)
{
	int ret;
	u8 val, mask = 0x01;

	ret = af9015_read_reg(d, addr, &val);
	if (ret)
		return ret;

	mask <<= bit;
	if (op) {
		/* set bit */
		val |= mask;
	} else {
		/* clear bit */
		mask ^= 0xff;
		val &= mask;
	}

	return af9015_write_reg(d, addr, val);
}

static int af9015_set_reg_bit(struct dvb_usb_device *d, u16 addr, u8 bit)
{
	return af9015_do_reg_bit(d, addr, bit, 1);
}

static int af9015_clear_reg_bit(struct dvb_usb_device *d, u16 addr, u8 bit)
{
	return af9015_do_reg_bit(d, addr, bit, 0);
}

static int af9015_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msg[],
	int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	struct af9015_state *state = d_to_priv(d);
	int ret = 0, i = 0;
	u16 addr;
	u8 uninitialized_var(mbox), addr_len;
	struct req_t req;

/*
The bus lock is needed because there is two tuners both using same I2C-address.
Due to that the only way to select correct tuner is use demodulator I2C-gate.

................................................
. AF9015 includes integrated AF9013 demodulator.
. ____________                   ____________  .                ____________
.|     uC     |                 |   demod    | .               |    tuner   |
.|------------|                 |------------| .               |------------|
.|   AF9015   |                 |  AF9013/5  | .               |   MXL5003  |
.|            |--+----I2C-------|-----/ -----|-.-----I2C-------|            |
.|            |  |              | addr 0x38  | .               |  addr 0xc6 |
.|____________|  |              |____________| .               |____________|
.................|..............................
		 |               ____________                   ____________
		 |              |   demod    |                 |    tuner   |
		 |              |------------|                 |------------|
		 |              |   AF9013   |                 |   MXL5003  |
		 +----I2C-------|-----/ -----|-------I2C-------|            |
				| addr 0x3a  |                 |  addr 0xc6 |
				|____________|                 |____________|
*/
	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	while (i < num) {
		if (msg[i].addr == state->af9013_config[0].i2c_addr ||
		    msg[i].addr == state->af9013_config[1].i2c_addr) {
			addr = msg[i].buf[0] << 8;
			addr += msg[i].buf[1];
			mbox = msg[i].buf[2];
			addr_len = 3;
		} else {
			addr = msg[i].buf[0];
			addr_len = 1;
			/* mbox is don't care in that case */
		}

		if (num > i + 1 && (msg[i+1].flags & I2C_M_RD)) {
			if (msg[i].len > 3 || msg[i+1].len > 61) {
				ret = -EOPNOTSUPP;
				goto error;
			}
			if (msg[i].addr == state->af9013_config[0].i2c_addr)
				req.cmd = READ_MEMORY;
			else
				req.cmd = READ_I2C;
			req.i2c_addr = msg[i].addr;
			req.addr = addr;
			req.mbox = mbox;
			req.addr_len = addr_len;
			req.data_len = msg[i+1].len;
			req.data = &msg[i+1].buf[0];
			ret = af9015_ctrl_msg(d, &req);
			i += 2;
		} else if (msg[i].flags & I2C_M_RD) {
			if (msg[i].len > 61) {
				ret = -EOPNOTSUPP;
				goto error;
			}
			if (msg[i].addr == state->af9013_config[0].i2c_addr) {
				ret = -EINVAL;
				goto error;
			}
			req.cmd = READ_I2C;
			req.i2c_addr = msg[i].addr;
			req.addr = addr;
			req.mbox = mbox;
			req.addr_len = addr_len;
			req.data_len = msg[i].len;
			req.data = &msg[i].buf[0];
			ret = af9015_ctrl_msg(d, &req);
			i += 1;
		} else {
			if (msg[i].len > 21) {
				ret = -EOPNOTSUPP;
				goto error;
			}
			if (msg[i].addr == state->af9013_config[0].i2c_addr)
				req.cmd = WRITE_MEMORY;
			else
				req.cmd = WRITE_I2C;
			req.i2c_addr = msg[i].addr;
			req.addr = addr;
			req.mbox = mbox;
			req.addr_len = addr_len;
			req.data_len = msg[i].len-addr_len;
			req.data = &msg[i].buf[addr_len];
			ret = af9015_ctrl_msg(d, &req);
			i += 1;
		}
		if (ret)
			goto error;

	}
	ret = i;

error:
	mutex_unlock(&d->i2c_mutex);

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
	int ret;
	u8 reply;
	struct req_t req = {GET_CONFIG, 0, 0, 0, 0, 1, &reply};

	ret = af9015_ctrl_msg(d, &req);
	if (ret)
		return ret;

	dev_dbg(&d->udev->dev, "%s: reply=%02x\n", __func__, reply);

	if (reply == 0x02)
		ret = WARM;
	else
		ret = COLD;

	return ret;
}

static int af9015_download_firmware(struct dvb_usb_device *d,
	const struct firmware *fw)
{
	struct af9015_state *state = d_to_priv(d);
	int i, len, remaining, ret;
	struct req_t req = {DOWNLOAD_FIRMWARE, 0, 0, 0, 0, 0, NULL};
	u16 checksum = 0;
	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	/* calc checksum */
	for (i = 0; i < fw->size; i++)
		checksum += fw->data[i];

	state->firmware_size = fw->size;
	state->firmware_checksum = checksum;

	#define FW_ADDR 0x5100 /* firmware start address */
	#define LEN_MAX 55 /* max packet size */
	for (remaining = fw->size; remaining > 0; remaining -= LEN_MAX) {
		len = remaining;
		if (len > LEN_MAX)
			len = LEN_MAX;

		req.data_len = len;
		req.data = (u8 *) &fw->data[fw->size - remaining];
		req.addr = FW_ADDR + fw->size - remaining;

		ret = af9015_ctrl_msg(d, &req);
		if (ret) {
			dev_err(&d->udev->dev,
					"%s: firmware download failed=%d\n",
					KBUILD_MODNAME, ret);
			goto error;
		}
	}

	/* firmware loaded, request boot */
	req.cmd = BOOT;
	req.data_len = 0;
	ret = af9015_ctrl_msg(d, &req);
	if (ret) {
		dev_err(&d->udev->dev, "%s: firmware boot failed=%d\n",
				KBUILD_MODNAME, ret);
		goto error;
	}

error:
	return ret;
}

#define AF9015_EEPROM_SIZE 256
/* 2^31 + 2^29 - 2^25 + 2^22 - 2^19 - 2^16 + 1 */
#define GOLDEN_RATIO_PRIME_32 0x9e370001UL

/* hash (and dump) eeprom */
static int af9015_eeprom_hash(struct dvb_usb_device *d)
{
	struct af9015_state *state = d_to_priv(d);
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
		dev_dbg(&d->udev->dev, "%s: %*ph\n", __func__, 16, buf + i);

	dev_dbg(&d->udev->dev, "%s: eeprom sum=%.8x\n",
			__func__, state->eeprom_sum);
	return 0;
err:
	dev_err(&d->udev->dev, "%s: eeprom failed=%d\n", KBUILD_MODNAME, ret);
	return ret;
}

static int af9015_read_config(struct dvb_usb_device *d)
{
	struct af9015_state *state = d_to_priv(d);
	int ret;
	u8 val, i, offset = 0;
	struct req_t req = {READ_I2C, AF9015_I2C_EEPROM, 0, 0, 1, 1, &val};

	dev_dbg(&d->udev->dev, "%s:\n", __func__);

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
	dev_dbg(&d->udev->dev, "%s: IR mode=%d\n", __func__, val);

	/* TS mode - one or two receivers */
	req.addr = AF9015_EEPROM_TS_MODE;
	ret = af9015_ctrl_msg(d, &req);
	if (ret)
		goto error;

	state->dual_mode = val;
	dev_dbg(&d->udev->dev, "%s: TS mode=%d\n", __func__, state->dual_mode);

	/* disable 2nd adapter because we don't have PID-filters */
	if (d->udev->speed == USB_SPEED_FULL)
		state->dual_mode = 0;

	if (state->dual_mode) {
		/* read 2nd demodulator I2C address */
		req.addr = AF9015_EEPROM_DEMOD2_I2C;
		ret = af9015_ctrl_msg(d, &req);
		if (ret)
			goto error;

		state->af9013_config[1].i2c_addr = val;
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
			state->af9013_config[i].clock = 28800000;
			break;
		case 1:
			state->af9013_config[i].clock = 20480000;
			break;
		case 2:
			state->af9013_config[i].clock = 28000000;
			break;
		case 3:
			state->af9013_config[i].clock = 25000000;
			break;
		}
		dev_dbg(&d->udev->dev, "%s: [%d] xtal=%d set clock=%d\n",
				__func__, i, val,
				state->af9013_config[i].clock);

		/* IF frequency */
		req.addr = AF9015_EEPROM_IF1H + offset;
		ret = af9015_ctrl_msg(d, &req);
		if (ret)
			goto error;

		state->af9013_config[i].if_frequency = val << 8;

		req.addr = AF9015_EEPROM_IF1L + offset;
		ret = af9015_ctrl_msg(d, &req);
		if (ret)
			goto error;

		state->af9013_config[i].if_frequency += val;
		state->af9013_config[i].if_frequency *= 1000;
		dev_dbg(&d->udev->dev, "%s: [%d] IF frequency=%d\n", __func__,
				i, state->af9013_config[i].if_frequency);

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
		dev_dbg(&d->udev->dev, "%s: [%d] MT2060 IF1=%d\n", __func__, i,
				state->mt2060_if1[i]);

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
			state->af9013_config[i].spec_inv = 1;
			break;
		case AF9013_TUNER_MXL5003D:
		case AF9013_TUNER_MXL5005D:
		case AF9013_TUNER_MXL5005R:
		case AF9013_TUNER_MXL5007T:
			state->af9013_config[i].spec_inv = 0;
			break;
		case AF9013_TUNER_MC44S803:
			state->af9013_config[i].gpio[1] = AF9013_GPIO_LO;
			state->af9013_config[i].spec_inv = 1;
			break;
		default:
			dev_err(&d->udev->dev, "%s: tuner id=%d not " \
					"supported, please report!\n",
					KBUILD_MODNAME, val);
			return -ENODEV;
		}

		state->af9013_config[i].tuner = val;
		dev_dbg(&d->udev->dev, "%s: [%d] tuner id=%d\n",
				__func__, i, val);
	}

error:
	if (ret)
		dev_err(&d->udev->dev, "%s: eeprom read failed=%d\n",
				KBUILD_MODNAME, ret);

	/* AverMedia AVerTV Volar Black HD (A850) device have bad EEPROM
	   content :-( Override some wrong values here. Ditto for the
	   AVerTV Red HD+ (A850T) device. */
	if (le16_to_cpu(d->udev->descriptor.idVendor) == USB_VID_AVERMEDIA &&
		((le16_to_cpu(d->udev->descriptor.idProduct) ==
			USB_PID_AVERMEDIA_A850) ||
		(le16_to_cpu(d->udev->descriptor.idProduct) ==
			USB_PID_AVERMEDIA_A850T))) {
		dev_dbg(&d->udev->dev,
				"%s: AverMedia A850: overriding config\n",
				__func__);
		/* disable dual mode */
		state->dual_mode = 0;

		/* set correct IF */
		state->af9013_config[0].if_frequency = 4570000;
	}

	return ret;
}

static int af9015_get_stream_config(struct dvb_frontend *fe, u8 *ts_type,
		struct usb_data_stream_properties *stream)
{
	struct dvb_usb_device *d = fe_to_d(fe);
	dev_dbg(&d->udev->dev, "%s: adap=%d\n", __func__, fe_to_adap(fe)->id);

	if (d->udev->speed == USB_SPEED_FULL)
		stream->u.bulk.buffersize = TS_USB11_FRAME_SIZE;

	return 0;
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
	int ret;
	u8 fw_params[4];
	u8 val, i;
	struct req_t req = {COPY_FIRMWARE, 0, 0x5100, 0, 0, sizeof(fw_params),
		fw_params };
	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	fw_params[0] = state->firmware_size >> 8;
	fw_params[1] = state->firmware_size & 0xff;
	fw_params[2] = state->firmware_checksum >> 8;
	fw_params[3] = state->firmware_checksum & 0xff;

	/* wait 2nd demodulator ready */
	msleep(100);

	ret = af9015_read_reg_i2c(d, state->af9013_config[1].i2c_addr,
			0x98be, &val);
	if (ret)
		goto error;
	else
		dev_dbg(&d->udev->dev, "%s: firmware status=%02x\n",
				__func__, val);

	if (val == 0x0c) /* fw is running, no need for download */
		goto exit;

	/* set I2C master clock to fast (to speed up firmware copy) */
	ret = af9015_write_reg(d, 0xd416, 0x04); /* 0x04 * 400ns */
	if (ret)
		goto error;

	msleep(50);

	/* copy firmware */
	ret = af9015_ctrl_msg(d, &req);
	if (ret)
		dev_err(&d->udev->dev, "%s: firmware copy cmd failed=%d\n",
				KBUILD_MODNAME, ret);

	dev_dbg(&d->udev->dev, "%s: firmware copy done\n", __func__);

	/* set I2C master clock back to normal */
	ret = af9015_write_reg(d, 0xd416, 0x14); /* 0x14 * 400ns */
	if (ret)
		goto error;

	/* request boot firmware */
	ret = af9015_write_reg_i2c(d, state->af9013_config[1].i2c_addr,
			0xe205, 1);
	dev_dbg(&d->udev->dev, "%s: firmware boot cmd status=%d\n",
			__func__, ret);
	if (ret)
		goto error;

	for (i = 0; i < 15; i++) {
		msleep(100);

		/* check firmware status */
		ret = af9015_read_reg_i2c(d, state->af9013_config[1].i2c_addr,
				0x98be, &val);
		dev_dbg(&d->udev->dev, "%s: firmware status cmd status=%d " \
				"firmware status=%02x\n", __func__, ret, val);
		if (ret)
			goto error;

		if (val == 0x0c || val == 0x04) /* success or fail */
			break;
	}

	if (val == 0x04) {
		dev_err(&d->udev->dev, "%s: firmware did not run\n",
				KBUILD_MODNAME);
		ret = -ETIMEDOUT;
	} else if (val != 0x0c) {
		dev_err(&d->udev->dev, "%s: firmware boot timeout\n",
				KBUILD_MODNAME);
		ret = -ETIMEDOUT;
	}

error:
exit:
	return ret;
}

static int af9015_af9013_frontend_attach(struct dvb_usb_adapter *adap)
{
	int ret;
	struct af9015_state *state = adap_to_priv(adap);

	if (adap->id == 0) {
		state->af9013_config[0].ts_mode = AF9013_TS_USB;
		memcpy(state->af9013_config[0].api_version, "\x0\x1\x9\x0", 4);
		state->af9013_config[0].gpio[0] = AF9013_GPIO_HI;
		state->af9013_config[0].gpio[3] = AF9013_GPIO_TUNER_ON;
	} else if (adap->id == 1) {
		state->af9013_config[1].ts_mode = AF9013_TS_SERIAL;
		memcpy(state->af9013_config[1].api_version, "\x0\x1\x9\x0", 4);
		state->af9013_config[1].gpio[0] = AF9013_GPIO_TUNER_ON;
		state->af9013_config[1].gpio[1] = AF9013_GPIO_LO;

		/* copy firmware to 2nd demodulator */
		if (state->dual_mode) {
			ret = af9015_copy_firmware(adap_to_d(adap));
			if (ret) {
				dev_err(&adap_to_d(adap)->udev->dev,
						"%s: firmware copy to 2nd " \
						"frontend failed, will " \
						"disable it\n", KBUILD_MODNAME);
				state->dual_mode = 0;
				return -ENODEV;
			}
		} else {
			return -ENODEV;
		}
	}

	/* attach demodulator */
	adap->fe[0] = dvb_attach(af9013_attach,
		&state->af9013_config[adap->id], &adap_to_d(adap)->i2c_adap);

	/*
	 * AF9015 firmware does not like if it gets interrupted by I2C adapter
	 * request on some critical phases. During normal operation I2C adapter
	 * is used only 2nd demodulator and tuner on dual tuner devices.
	 * Override demodulator callbacks and use mutex for limit access to
	 * those "critical" paths to keep AF9015 happy.
	 */
	if (adap->fe[0]) {
		state->set_frontend[adap->id] =
			adap->fe[0]->ops.set_frontend;
		adap->fe[0]->ops.set_frontend =
			af9015_af9013_set_frontend;

		state->read_status[adap->id] =
			adap->fe[0]->ops.read_status;
		adap->fe[0]->ops.read_status =
			af9015_af9013_read_status;

		state->init[adap->id] = adap->fe[0]->ops.init;
		adap->fe[0]->ops.init = af9015_af9013_init;

		state->sleep[adap->id] = adap->fe[0]->ops.sleep;
		adap->fe[0]->ops.sleep = af9015_af9013_sleep;
	}

	return adap->fe[0] == NULL ? -ENODEV : 0;
}

static struct mt2060_config af9015_mt2060_config = {
	.i2c_address = 0xc0,
	.clock_out = 0,
};

static struct qt1010_config af9015_qt1010_config = {
	.i2c_address = 0xc4,
};

static struct tda18271_config af9015_tda18271_config = {
	.gate = TDA18271_GATE_DIGITAL,
	.small_i2c = TDA18271_16_BYTE_CHUNK_INIT,
};

static struct mxl5005s_config af9015_mxl5003_config = {
	.i2c_address     = 0xc6,
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
	.i2c_address     = 0xc6,
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
	.i2c_address = 0xc0,
	.dig_out = 1,
};

static struct tda18218_config af9015_tda18218_config = {
	.i2c_address = 0xc0,
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
	int ret;
	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	switch (state->af9013_config[adap->id].tuner) {
	case AF9013_TUNER_MT2060:
	case AF9013_TUNER_MT2060_2:
		ret = dvb_attach(mt2060_attach, adap->fe[0],
			&adap_to_d(adap)->i2c_adap, &af9015_mt2060_config,
			state->mt2060_if1[adap->id])
			== NULL ? -ENODEV : 0;
		break;
	case AF9013_TUNER_QT1010:
	case AF9013_TUNER_QT1010A:
		ret = dvb_attach(qt1010_attach, adap->fe[0],
			&adap_to_d(adap)->i2c_adap,
			&af9015_qt1010_config) == NULL ? -ENODEV : 0;
		break;
	case AF9013_TUNER_TDA18271:
		ret = dvb_attach(tda18271_attach, adap->fe[0], 0xc0,
			&adap_to_d(adap)->i2c_adap,
			&af9015_tda18271_config) == NULL ? -ENODEV : 0;
		break;
	case AF9013_TUNER_TDA18218:
		ret = dvb_attach(tda18218_attach, adap->fe[0],
			&adap_to_d(adap)->i2c_adap,
			&af9015_tda18218_config) == NULL ? -ENODEV : 0;
		break;
	case AF9013_TUNER_MXL5003D:
		ret = dvb_attach(mxl5005s_attach, adap->fe[0],
			&adap_to_d(adap)->i2c_adap,
			&af9015_mxl5003_config) == NULL ? -ENODEV : 0;
		break;
	case AF9013_TUNER_MXL5005D:
	case AF9013_TUNER_MXL5005R:
		ret = dvb_attach(mxl5005s_attach, adap->fe[0],
			&adap_to_d(adap)->i2c_adap,
			&af9015_mxl5005_config) == NULL ? -ENODEV : 0;
		break;
	case AF9013_TUNER_ENV77H11D5:
		ret = dvb_attach(dvb_pll_attach, adap->fe[0], 0xc0,
			&adap_to_d(adap)->i2c_adap,
			DVB_PLL_TDA665X) == NULL ? -ENODEV : 0;
		break;
	case AF9013_TUNER_MC44S803:
		ret = dvb_attach(mc44s803_attach, adap->fe[0],
			&adap_to_d(adap)->i2c_adap,
			&af9015_mc44s803_config) == NULL ? -ENODEV : 0;
		break;
	case AF9013_TUNER_MXL5007T:
		ret = dvb_attach(mxl5007t_attach, adap->fe[0],
			&adap_to_d(adap)->i2c_adap,
			0xc0, &af9015_mxl5007t_config) == NULL ? -ENODEV : 0;
		break;
	case AF9013_TUNER_UNKNOWN:
	default:
		dev_err(&d->udev->dev, "%s: unknown tuner id=%d\n",
				KBUILD_MODNAME,
				state->af9013_config[adap->id].tuner);
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
	struct dvb_usb_device *d = adap_to_d(adap);
	int ret;
	dev_dbg(&d->udev->dev, "%s: onoff=%d\n", __func__, onoff);

	if (onoff)
		ret = af9015_set_reg_bit(d, 0xd503, 0);
	else
		ret = af9015_clear_reg_bit(d, 0xd503, 0);

	return ret;
}

static int af9015_pid_filter(struct dvb_usb_adapter *adap, int index, u16 pid,
	int onoff)
{
	struct dvb_usb_device *d = adap_to_d(adap);
	int ret;
	u8 idx;
	dev_dbg(&d->udev->dev, "%s: index=%d pid=%04x onoff=%d\n",
			__func__, index, pid, onoff);

	ret = af9015_write_reg(d, 0xd505, (pid & 0xff));
	if (ret)
		goto error;

	ret = af9015_write_reg(d, 0xd506, (pid >> 8));
	if (ret)
		goto error;

	idx = ((index & 0x1f) | (1 << 5));
	ret = af9015_write_reg(d, 0xd504, idx);

error:
	return ret;
}

static int af9015_init_endpoint(struct dvb_usb_device *d)
{
	struct af9015_state *state = d_to_priv(d);
	int ret;
	u16 frame_size;
	u8  packet_size;
	dev_dbg(&d->udev->dev, "%s: USB speed=%d\n", __func__, d->udev->speed);

	if (d->udev->speed == USB_SPEED_FULL) {
		frame_size = TS_USB11_FRAME_SIZE/4;
		packet_size = TS_USB11_MAX_PACKET_SIZE/4;
	} else {
		frame_size = TS_USB20_FRAME_SIZE/4;
		packet_size = TS_USB20_MAX_PACKET_SIZE/4;
	}

	ret = af9015_set_reg_bit(d, 0xd507, 2); /* assert EP4 reset */
	if (ret)
		goto error;
	ret = af9015_set_reg_bit(d, 0xd50b, 1); /* assert EP5 reset */
	if (ret)
		goto error;
	ret = af9015_clear_reg_bit(d, 0xdd11, 5); /* disable EP4 */
	if (ret)
		goto error;
	ret = af9015_clear_reg_bit(d, 0xdd11, 6); /* disable EP5 */
	if (ret)
		goto error;
	ret = af9015_set_reg_bit(d, 0xdd11, 5); /* enable EP4 */
	if (ret)
		goto error;
	if (state->dual_mode) {
		ret = af9015_set_reg_bit(d, 0xdd11, 6); /* enable EP5 */
		if (ret)
			goto error;
	}
	ret = af9015_clear_reg_bit(d, 0xdd13, 5); /* disable EP4 NAK */
	if (ret)
		goto error;
	if (state->dual_mode) {
		ret = af9015_clear_reg_bit(d, 0xdd13, 6); /* disable EP5 NAK */
		if (ret)
			goto error;
	}
	/* EP4 xfer length */
	ret = af9015_write_reg(d, 0xdd88, frame_size & 0xff);
	if (ret)
		goto error;
	ret = af9015_write_reg(d, 0xdd89, frame_size >> 8);
	if (ret)
		goto error;
	/* EP5 xfer length */
	ret = af9015_write_reg(d, 0xdd8a, frame_size & 0xff);
	if (ret)
		goto error;
	ret = af9015_write_reg(d, 0xdd8b, frame_size >> 8);
	if (ret)
		goto error;
	ret = af9015_write_reg(d, 0xdd0c, packet_size); /* EP4 packet size */
	if (ret)
		goto error;
	ret = af9015_write_reg(d, 0xdd0d, packet_size); /* EP5 packet size */
	if (ret)
		goto error;
	ret = af9015_clear_reg_bit(d, 0xd507, 2); /* negate EP4 reset */
	if (ret)
		goto error;
	if (state->dual_mode) {
		ret = af9015_clear_reg_bit(d, 0xd50b, 1); /* negate EP5 reset */
		if (ret)
			goto error;
	}

	/* enable / disable mp2if2 */
	if (state->dual_mode)
		ret = af9015_set_reg_bit(d, 0xd50b, 0);
	else
		ret = af9015_clear_reg_bit(d, 0xd50b, 0);

error:
	if (ret)
		dev_err(&d->udev->dev, "%s: endpoint init failed=%d\n",
				KBUILD_MODNAME, ret);

	return ret;
}

static int af9015_init(struct dvb_usb_device *d)
{
	struct af9015_state *state = d_to_priv(d);
	int ret;
	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	mutex_init(&state->fe_mutex);

	/* init RC canary */
	ret = af9015_write_reg(d, 0x98e9, 0xff);
	if (ret)
		goto error;

	ret = af9015_init_endpoint(d);
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
	int ret;
	u8 buf[17];

	/* read registers needed to detect remote controller code */
	ret = af9015_read_regs(d, 0x98d9, buf, sizeof(buf));
	if (ret)
		goto error;

	/* If any of these are non-zero, assume invalid data */
	if (buf[1] || buf[2] || buf[3]) {
		dev_dbg(&d->udev->dev, "%s: invalid data\n", __func__);
		return ret;
	}

	/* Check for repeat of previous code */
	if ((state->rc_repeat != buf[6] || buf[0]) &&
			!memcmp(&buf[12], state->rc_last, 4)) {
		dev_dbg(&d->udev->dev, "%s: key repeated\n", __func__);
		rc_repeat(d->rc_dev);
		state->rc_repeat = buf[6];
		return ret;
	}

	/* Only process key if canary killed */
	if (buf[16] != 0xff && buf[0] != 0x01) {
		dev_dbg(&d->udev->dev, "%s: key pressed %*ph\n",
				__func__, 4, buf + 12);

		/* Reset the canary */
		ret = af9015_write_reg(d, 0x98e9, 0xff);
		if (ret)
			goto error;

		/* Remember this key */
		memcpy(state->rc_last, &buf[12], 4);
		if (buf[14] == (u8) ~buf[15]) {
			if (buf[12] == (u8) ~buf[13]) {
				/* NEC */
				state->rc_keycode = RC_SCANCODE_NEC(buf[12],
								    buf[14]);
			} else {
				/* NEC extended*/
				state->rc_keycode = RC_SCANCODE_NECX(buf[12] << 8 |
								     buf[13],
								     buf[14]);
			}
		} else {
			/* 32 bit NEC */
			state->rc_keycode = RC_SCANCODE_NEC32(buf[12] << 24 |
							      buf[13] << 16 |
							      buf[14] << 8  |
							      buf[15]);
		}
		rc_keydown(d->rc_dev, RC_TYPE_NEC, state->rc_keycode, 0);
	} else {
		dev_dbg(&d->udev->dev, "%s: no key press\n", __func__);
		/* Invalidate last keypress */
		/* Not really needed, but helps with debug */
		state->rc_last[2] = state->rc_last[3];
	}

	state->rc_repeat = buf[6];
	state->rc_failed = false;

error:
	if (ret) {
		dev_warn(&d->udev->dev, "%s: rc query failed=%d\n",
				KBUILD_MODNAME, ret);

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
		/* Check USB manufacturer and product strings and try
		   to determine correct remote in case of chip vendor
		   reference IDs are used.
		   DO NOT ADD ANYTHING NEW HERE. Use hashes instead. */
		char manufacturer[10];
		memset(manufacturer, 0, sizeof(manufacturer));
		usb_string(d->udev, d->udev->descriptor.iManufacturer,
			manufacturer, sizeof(manufacturer));
		if (!strcmp("MSI", manufacturer)) {
			/* iManufacturer 1 MSI
			   iProduct      2 MSI K-VOX */
			rc->map_name = af9015_rc_setup_match(
					AF9015_REMOTE_MSI_DIGIVOX_MINI_II_V3,
					af9015_rc_setup_modparam);
		}
	}

	/* load empty to enable rc */
	if (!rc->map_name)
		rc->map_name = RC_MAP_EMPTY;

	rc->allowed_protos = RC_BIT_NEC;
	rc->query = af9015_rc_query;
	rc->interval = 500;

	return 0;
}
#else
	#define af9015_get_rc_config NULL
#endif

static int af9015_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	char manufacturer[sizeof("ITE Technologies, Inc.")];

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
			dev_dbg(&udev->dev, "%s: rejecting device\n", __func__);
			return -ENODEV;
		}
	}

	return dvb_usbv2_probe(intf, id);
}

/* interface 0 is used by DVB-T receiver and
   interface 1 is for remote controller (HID) */
static struct dvb_usb_device_properties af9015_props = {
	.driver_name = KBUILD_MODNAME,
	.owner = THIS_MODULE,
	.adapter_nr = adapter_nr,
	.size_of_priv = sizeof(struct af9015_state),

	.generic_bulk_ctrl_endpoint = 0x02,
	.generic_bulk_ctrl_endpoint_response = 0x81,

	.identify_state = af9015_identify_state,
	.firmware = AF9015_FIRMWARE,
	.download_firmware = af9015_download_firmware,

	.i2c_algo = &af9015_i2c_algo,
	.read_config = af9015_read_config,
	.frontend_attach = af9015_af9013_frontend_attach,
	.tuner_attach = af9015_tuner_attach,
	.init = af9015_init,
	.get_rc_config = af9015_get_rc_config,
	.get_stream_config = af9015_get_stream_config,

	.get_adapter_count = af9015_get_adapter_count,
	.adapter = {
		{
			.caps = DVB_USB_ADAP_HAS_PID_FILTER |
				DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
			.pid_filter_count = 32,
			.pid_filter = af9015_pid_filter,
			.pid_filter_ctrl = af9015_pid_filter_ctrl,

			.stream = DVB_USB_STREAM_BULK(0x84, 8, TS_USB20_FRAME_SIZE),
		}, {
			.stream = DVB_USB_STREAM_BULK(0x85, 8, TS_USB20_FRAME_SIZE),
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
		&af9015_props, "KWorld Digial MC-810", NULL) },
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
	.probe = af9015_probe,
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
