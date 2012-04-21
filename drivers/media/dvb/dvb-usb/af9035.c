/*
 * Afatech AF9035 DVB USB driver
 *
 * Copyright (C) 2009 Antti Palosaari <crope@iki.fi>
 * Copyright (C) 2012 Antti Palosaari <crope@iki.fi>
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
 *    You should have received a copy of the GNU General Public License along
 *    with this program; if not, write to the Free Software Foundation, Inc.,
 *    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "af9035.h"
#include "af9033.h"
#include "tua9001.h"
#include "fc0011.h"
#include "mxl5007t.h"
#include "tda18218.h"

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);
static DEFINE_MUTEX(af9035_usb_mutex);
static struct config af9035_config;
static struct dvb_usb_device_properties af9035_properties[2];
static int af9035_properties_count = ARRAY_SIZE(af9035_properties);
static struct af9033_config af9035_af9033_config[] = {
	{
		.ts_mode = AF9033_TS_MODE_USB,
	}, {
		.ts_mode = AF9033_TS_MODE_SERIAL,
	}
};

static u16 af9035_checksum(const u8 *buf, size_t len)
{
	size_t i;
	u16 checksum = 0;

	for (i = 1; i < len; i++) {
		if (i % 2)
			checksum += buf[i] << 8;
		else
			checksum += buf[i];
	}
	checksum = ~checksum;

	return checksum;
}

static int af9035_ctrl_msg(struct usb_device *udev, struct usb_req *req)
{
#define BUF_LEN 64
#define REQ_HDR_LEN 4 /* send header size */
#define ACK_HDR_LEN 3 /* rece header size */
#define CHECKSUM_LEN 2
#define USB_TIMEOUT 2000

	int ret, msg_len, act_len;
	u8 buf[BUF_LEN];
	static u8 seq; /* packet sequence number */
	u16 checksum, tmp_checksum;

	/* buffer overflow check */
	if (req->wlen > (BUF_LEN - REQ_HDR_LEN - CHECKSUM_LEN) ||
		req->rlen > (BUF_LEN - ACK_HDR_LEN - CHECKSUM_LEN)) {
		pr_debug("%s: too much data wlen=%d rlen=%d\n", __func__,
				req->wlen, req->rlen);
		return -EINVAL;
	}

	if (mutex_lock_interruptible(&af9035_usb_mutex) < 0)
		return -EAGAIN;

	buf[0] = REQ_HDR_LEN + req->wlen + CHECKSUM_LEN - 1;
	buf[1] = req->mbox;
	buf[2] = req->cmd;
	buf[3] = seq++;
	if (req->wlen)
		memcpy(&buf[4], req->wbuf, req->wlen);

	/* calc and add checksum */
	checksum = af9035_checksum(buf, buf[0] - 1);
	buf[buf[0] - 1] = (checksum >> 8);
	buf[buf[0] - 0] = (checksum & 0xff);

	msg_len = REQ_HDR_LEN + req->wlen + CHECKSUM_LEN ;

	/* send req */
	ret = usb_bulk_msg(udev, usb_sndbulkpipe(udev, 0x02), buf, msg_len,
			&act_len, USB_TIMEOUT);
	if (ret < 0)
		err("bulk message failed=%d (%d/%d)", ret, msg_len, act_len);
	else
		if (act_len != msg_len)
			ret = -EIO; /* all data is not send */
	if (ret < 0)
		goto err_mutex_unlock;

	/* no ack for those packets */
	if (req->cmd == CMD_FW_DL)
		goto exit_mutex_unlock;

	/* receive ack and data if read req */
	msg_len = ACK_HDR_LEN + req->rlen + CHECKSUM_LEN;
	ret = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, 0x81), buf, msg_len,
			&act_len, USB_TIMEOUT);
	if (ret < 0) {
		err("recv bulk message failed=%d", ret);
		ret = -EIO;
		goto err_mutex_unlock;
	}

	if (act_len != msg_len) {
		err("recv bulk message truncated (%d != %d)", act_len, msg_len);
		ret = -EIO;
		goto err_mutex_unlock;
	}

	/* verify checksum */
	checksum = af9035_checksum(buf, act_len - 2);
	tmp_checksum = (buf[act_len - 2] << 8) | buf[act_len - 1];
	if (tmp_checksum != checksum) {
		err("%s: command=%02x checksum mismatch (%04x != %04x)",
		    __func__, req->cmd, tmp_checksum, checksum);
		ret = -EIO;
		goto err_mutex_unlock;
	}

	/* check status */
	if (buf[2]) {
		pr_debug("%s: command=%02x failed fw error=%d\n", __func__,
				req->cmd, buf[2]);
		ret = -EIO;
		goto err_mutex_unlock;
	}

	/* read request, copy returned data to return buf */
	if (req->rlen)
		memcpy(req->rbuf, &buf[ACK_HDR_LEN], req->rlen);

err_mutex_unlock:
exit_mutex_unlock:
	mutex_unlock(&af9035_usb_mutex);

	return ret;
}

/* write multiple registers */
static int af9035_wr_regs(struct dvb_usb_device *d, u32 reg, u8 *val, int len)
{
	u8 wbuf[6 + len];
	u8 mbox = (reg >> 16) & 0xff;
	struct usb_req req = { CMD_MEM_WR, mbox, sizeof(wbuf), wbuf, 0, NULL };

	wbuf[0] = len;
	wbuf[1] = 2;
	wbuf[2] = 0;
	wbuf[3] = 0;
	wbuf[4] = (reg >> 8) & 0xff;
	wbuf[5] = (reg >> 0) & 0xff;
	memcpy(&wbuf[6], val, len);

	return af9035_ctrl_msg(d->udev, &req);
}

/* read multiple registers */
static int af9035_rd_regs(struct dvb_usb_device *d, u32 reg, u8 *val, int len)
{
	u8 wbuf[] = { len, 2, 0, 0, (reg >> 8) & 0xff, reg & 0xff };
	u8 mbox = (reg >> 16) & 0xff;
	struct usb_req req = { CMD_MEM_RD, mbox, sizeof(wbuf), wbuf, len, val };

	return af9035_ctrl_msg(d->udev, &req);
}

/* write single register */
static int af9035_wr_reg(struct dvb_usb_device *d, u32 reg, u8 val)
{
	return af9035_wr_regs(d, reg, &val, 1);
}

/* read single register */
static int af9035_rd_reg(struct dvb_usb_device *d, u32 reg, u8 *val)
{
	return af9035_rd_regs(d, reg, val, 1);
}

/* write single register with mask */
static int af9035_wr_reg_mask(struct dvb_usb_device *d, u32 reg, u8 val,
		u8 mask)
{
	int ret;
	u8 tmp;

	/* no need for read if whole reg is written */
	if (mask != 0xff) {
		ret = af9035_rd_regs(d, reg, &tmp, 1);
		if (ret)
			return ret;

		val &= mask;
		tmp &= ~mask;
		val |= tmp;
	}

	return af9035_wr_regs(d, reg, &val, 1);
}

static int af9035_i2c_master_xfer(struct i2c_adapter *adap,
		struct i2c_msg msg[], int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	int ret;

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	/*
	 * I2C sub header is 5 bytes long. Meaning of those bytes are:
	 * 0: data len
	 * 1: I2C addr << 1
	 * 2: reg addr len
	 *    byte 3 and 4 can be used as reg addr
	 * 3: reg addr MSB
	 *    used when reg addr len is set to 2
	 * 4: reg addr LSB
	 *    used when reg addr len is set to 1 or 2
	 *
	 * For the simplify we do not use register addr at all.
	 * NOTE: As a firmware knows tuner type there is very small possibility
	 * there could be some tuner I2C hacks done by firmware and this may
	 * lead problems if firmware expects those bytes are used.
	 */
	if (num == 2 && !(msg[0].flags & I2C_M_RD) &&
			(msg[1].flags & I2C_M_RD)) {
		if (msg[0].len > 40 || msg[1].len > 40) {
			/* TODO: correct limits > 40 */
			ret = -EOPNOTSUPP;
		} else if (msg[0].addr == af9035_af9033_config[0].i2c_addr) {
			/* integrated demod */
			u32 reg = msg[0].buf[0] << 16 | msg[0].buf[1] << 8 |
					msg[0].buf[2];
			ret = af9035_rd_regs(d, reg, &msg[1].buf[0],
					msg[1].len);
		} else {
			/* I2C */
			u8 buf[5 + msg[0].len];
			struct usb_req req = { CMD_I2C_RD, 0, sizeof(buf),
					buf, msg[1].len, msg[1].buf };
			buf[0] = msg[1].len;
			buf[1] = msg[0].addr << 1;
			buf[2] = 0x00; /* reg addr len */
			buf[3] = 0x00; /* reg addr MSB */
			buf[4] = 0x00; /* reg addr LSB */
			memcpy(&buf[5], msg[0].buf, msg[0].len);
			ret = af9035_ctrl_msg(d->udev, &req);
		}
	} else if (num == 1 && !(msg[0].flags & I2C_M_RD)) {
		if (msg[0].len > 40) {
			/* TODO: correct limits > 40 */
			ret = -EOPNOTSUPP;
		} else if (msg[0].addr == af9035_af9033_config[0].i2c_addr) {
			/* integrated demod */
			u32 reg = msg[0].buf[0] << 16 | msg[0].buf[1] << 8 |
					msg[0].buf[2];
			ret = af9035_wr_regs(d, reg, &msg[0].buf[3],
					msg[0].len - 3);
		} else {
			/* I2C */
			u8 buf[5 + msg[0].len];
			struct usb_req req = { CMD_I2C_WR, 0, sizeof(buf), buf,
					0, NULL };
			buf[0] = msg[0].len;
			buf[1] = msg[0].addr << 1;
			buf[2] = 0x00; /* reg addr len */
			buf[3] = 0x00; /* reg addr MSB */
			buf[4] = 0x00; /* reg addr LSB */
			memcpy(&buf[5], msg[0].buf, msg[0].len);
			ret = af9035_ctrl_msg(d->udev, &req);
		}
	} else {
		/*
		 * We support only two kind of I2C transactions:
		 * 1) 1 x read + 1 x write
		 * 2) 1 x write
		 */
		ret = -EOPNOTSUPP;
	}

	mutex_unlock(&d->i2c_mutex);

	if (ret < 0)
		return ret;
	else
		return num;
}

static u32 af9035_i2c_functionality(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm af9035_i2c_algo = {
	.master_xfer = af9035_i2c_master_xfer,
	.functionality = af9035_i2c_functionality,
};

#define AF9035_POLL 250
static int af9035_rc_query(struct dvb_usb_device *d)
{
	unsigned int key;
	unsigned char b[4];
	int ret;
	struct usb_req req = { CMD_IR_GET, 0, 0, NULL, 4, b };

	ret = af9035_ctrl_msg(d->udev, &req);
	if (ret < 0)
		goto err;

	if ((b[2] + b[3]) == 0xff) {
		if ((b[0] + b[1]) == 0xff) {
			/* NEC */
			key = b[0] << 8 | b[2];
		} else {
			/* ext. NEC */
			key = b[0] << 16 | b[1] << 8 | b[2];
		}
	} else {
		key = b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3];
	}

	rc_keydown(d->rc_dev, key, 0);

err:
	/* ignore errors */
	return 0;
}

static int af9035_init(struct dvb_usb_device *d)
{
	int ret, i;
	u16 frame_size = 87 * 188 / 4;
	u8  packet_size = 512 / 4;
	struct reg_val_mask tab[] = {
		{ 0x80f99d, 0x01, 0x01 },
		{ 0x80f9a4, 0x01, 0x01 },
		{ 0x00dd11, 0x00, 0x20 },
		{ 0x00dd11, 0x00, 0x40 },
		{ 0x00dd13, 0x00, 0x20 },
		{ 0x00dd13, 0x00, 0x40 },
		{ 0x00dd11, 0x20, 0x20 },
		{ 0x00dd88, (frame_size >> 0) & 0xff, 0xff},
		{ 0x00dd89, (frame_size >> 8) & 0xff, 0xff},
		{ 0x00dd0c, packet_size, 0xff},
		{ 0x00dd11, af9035_config.dual_mode << 6, 0x40 },
		{ 0x00dd8a, (frame_size >> 0) & 0xff, 0xff},
		{ 0x00dd8b, (frame_size >> 8) & 0xff, 0xff},
		{ 0x00dd0d, packet_size, 0xff },
		{ 0x80f9a3, 0x00, 0x01 },
		{ 0x80f9cd, 0x00, 0x01 },
		{ 0x80f99d, 0x00, 0x01 },
		{ 0x80f9a4, 0x00, 0x01 },
	};

	pr_debug("%s: USB speed=%d frame_size=%04x packet_size=%02x\n",
		__func__, d->udev->speed, frame_size, packet_size);

	/* init endpoints */
	for (i = 0; i < ARRAY_SIZE(tab); i++) {
		ret = af9035_wr_reg_mask(d, tab[i].reg, tab[i].val,
				tab[i].mask);
		if (ret < 0)
			goto err;
	}

	return 0;

err:
	pr_debug("%s: failed=%d\n", __func__, ret);

	return ret;
}

static int af9035_identify_state(struct usb_device *udev,
		struct dvb_usb_device_properties *props,
		struct dvb_usb_device_description **desc,
		int *cold)
{
	int ret;
	u8 wbuf[1] = { 1 };
	u8 rbuf[4];
	struct usb_req req = { CMD_FW_QUERYINFO, 0, sizeof(wbuf), wbuf,
			sizeof(rbuf), rbuf };

	ret = af9035_ctrl_msg(udev, &req);
	if (ret < 0)
		goto err;

	pr_debug("%s: reply=%02x %02x %02x %02x\n", __func__,
		rbuf[0], rbuf[1], rbuf[2], rbuf[3]);
	if (rbuf[0] || rbuf[1] || rbuf[2] || rbuf[3])
		*cold = 0;
	else
		*cold = 1;

	return 0;

err:
	pr_debug("%s: failed=%d\n", __func__, ret);

	return ret;
}

static int af9035_download_firmware(struct usb_device *udev,
		const struct firmware *fw)
{
	int ret, i, j, len;
	u8 wbuf[1];
	u8 rbuf[4];
	struct usb_req req = { 0, 0, 0, NULL, 0, NULL };
	struct usb_req req_fw_dl = { CMD_FW_DL, 0, 0, wbuf, 0, NULL };
	struct usb_req req_fw_ver = { CMD_FW_QUERYINFO, 0, 1, wbuf, 4, rbuf } ;
	u8 hdr_core;
	u16 hdr_addr, hdr_data_len, hdr_checksum;
	#define MAX_DATA 58
	#define HDR_SIZE 7

	/*
	 * Thanks to Daniel Glöckner <daniel-gl@gmx.net> about that info!
	 *
	 * byte 0: MCS 51 core
	 *  There are two inside the AF9035 (1=Link and 2=OFDM) with separate
	 *  address spaces
	 * byte 1-2: Big endian destination address
	 * byte 3-4: Big endian number of data bytes following the header
	 * byte 5-6: Big endian header checksum, apparently ignored by the chip
	 *  Calculated as ~(h[0]*256+h[1]+h[2]*256+h[3]+h[4]*256)
	 */

	for (i = fw->size; i > HDR_SIZE;) {
		hdr_core = fw->data[fw->size - i + 0];
		hdr_addr = fw->data[fw->size - i + 1] << 8;
		hdr_addr |= fw->data[fw->size - i + 2] << 0;
		hdr_data_len = fw->data[fw->size - i + 3] << 8;
		hdr_data_len |= fw->data[fw->size - i + 4] << 0;
		hdr_checksum = fw->data[fw->size - i + 5] << 8;
		hdr_checksum |= fw->data[fw->size - i + 6] << 0;

		pr_debug("%s: core=%d addr=%04x data_len=%d checksum=%04x\n",
				__func__, hdr_core, hdr_addr, hdr_data_len,
				hdr_checksum);

		if (((hdr_core != 1) && (hdr_core != 2)) ||
				(hdr_data_len > i)) {
			pr_debug("%s: bad firmware\n", __func__);
			break;
		}

		/* download begin packet */
		req.cmd = CMD_FW_DL_BEGIN;
		ret = af9035_ctrl_msg(udev, &req);
		if (ret < 0)
			goto err;

		/* download firmware packet(s) */
		for (j = HDR_SIZE + hdr_data_len; j > 0; j -= MAX_DATA) {
			len = j;
			if (len > MAX_DATA)
				len = MAX_DATA;
			req_fw_dl.wlen = len;
			req_fw_dl.wbuf = (u8 *) &fw->data[fw->size - i +
					HDR_SIZE + hdr_data_len - j];
			ret = af9035_ctrl_msg(udev, &req_fw_dl);
			if (ret < 0)
				goto err;
		}

		/* download end packet */
		req.cmd = CMD_FW_DL_END;
		ret = af9035_ctrl_msg(udev, &req);
		if (ret < 0)
			goto err;

		i -= hdr_data_len + HDR_SIZE;

		pr_debug("%s: data uploaded=%zu\n", __func__, fw->size - i);
	}

	/* firmware loaded, request boot */
	req.cmd = CMD_FW_BOOT;
	ret = af9035_ctrl_msg(udev, &req);
	if (ret < 0)
		goto err;

	/* ensure firmware starts */
	wbuf[0] = 1;
	ret = af9035_ctrl_msg(udev, &req_fw_ver);
	if (ret < 0)
		goto err;

	if (!(rbuf[0] || rbuf[1] || rbuf[2] || rbuf[3])) {
		info("firmware did not run");
		ret = -ENODEV;
		goto err;
	}

	info("firmware version=%d.%d.%d.%d", rbuf[0], rbuf[1], rbuf[2],
			rbuf[3]);

	return 0;

err:
	pr_debug("%s: failed=%d\n", __func__, ret);

	return ret;
}

static int af9035_download_firmware_it9135(struct usb_device *udev,
		const struct firmware *fw)
{
	int ret, i, i_prev;
	u8 wbuf[1];
	u8 rbuf[4];
	struct usb_req req = { 0, 0, 0, NULL, 0, NULL };
	struct usb_req req_fw_dl = { CMD_FW_SCATTER_WR, 0, 0, NULL, 0, NULL };
	struct usb_req req_fw_ver = { CMD_FW_QUERYINFO, 0, 1, wbuf, 4, rbuf } ;
	#define HDR_SIZE 7

	/*
	 * There seems to be following firmware header. Meaning of bytes 0-3
	 * is unknown.
	 *
	 * 0: 3
	 * 1: 0, 1
	 * 2: 0
	 * 3: 1, 2, 3
	 * 4: addr MSB
	 * 5: addr LSB
	 * 6: count of data bytes ?
	 */

	for (i = HDR_SIZE, i_prev = 0; i <= fw->size; i++) {
		if (i == fw->size ||
				(fw->data[i + 0] == 0x03 &&
				(fw->data[i + 1] == 0x00 ||
				fw->data[i + 1] == 0x01) &&
				fw->data[i + 2] == 0x00)) {
			req_fw_dl.wlen = i - i_prev;
			req_fw_dl.wbuf = (u8 *) &fw->data[i_prev];
			i_prev = i;
			ret = af9035_ctrl_msg(udev, &req_fw_dl);
			if (ret < 0)
				goto err;

			pr_debug("%s: data uploaded=%d\n", __func__, i);
		}
	}

	/* firmware loaded, request boot */
	req.cmd = CMD_FW_BOOT;
	ret = af9035_ctrl_msg(udev, &req);
	if (ret < 0)
		goto err;

	/* ensure firmware starts */
	wbuf[0] = 1;
	ret = af9035_ctrl_msg(udev, &req_fw_ver);
	if (ret < 0)
		goto err;

	if (!(rbuf[0] || rbuf[1] || rbuf[2] || rbuf[3])) {
		info("firmware did not run");
		ret = -ENODEV;
		goto err;
	}

	info("firmware version=%d.%d.%d.%d", rbuf[0], rbuf[1], rbuf[2],
			rbuf[3]);

	return 0;

err:
	pr_debug("%s: failed=%d\n", __func__, ret);

	return ret;
}

/* abuse that callback as there is no better one for reading eeprom */
static int af9035_read_mac_address(struct dvb_usb_device *d, u8 mac[6])
{
	int ret, i, eeprom_shift = 0;
	u8 tmp;
	u16 tmp16;

	/* check if there is dual tuners */
	ret = af9035_rd_reg(d, EEPROM_DUAL_MODE, &tmp);
	if (ret < 0)
		goto err;

	af9035_config.dual_mode = tmp;
	pr_debug("%s: dual mode=%d\n", __func__, af9035_config.dual_mode);

	for (i = 0; i < af9035_properties[0].num_adapters; i++) {
		/* tuner */
		ret = af9035_rd_reg(d, EEPROM_1_TUNER_ID + eeprom_shift, &tmp);
		if (ret < 0)
			goto err;

		af9035_af9033_config[i].tuner = tmp;
		pr_debug("%s: [%d]tuner=%02x\n", __func__, i, tmp);

		switch (tmp) {
		case AF9033_TUNER_TUA9001:
		case AF9033_TUNER_FC0011:
		case AF9033_TUNER_MXL5007T:
		case AF9033_TUNER_TDA18218:
			af9035_af9033_config[i].spec_inv = 1;
			break;
		default:
			af9035_config.hw_not_supported = true;
			warn("tuner ID=%02x not supported, please report!",
				tmp);
		};

		/* tuner IF frequency */
		ret = af9035_rd_reg(d, EEPROM_1_IFFREQ_L + eeprom_shift, &tmp);
		if (ret < 0)
			goto err;

		tmp16 = tmp;

		ret = af9035_rd_reg(d, EEPROM_1_IFFREQ_H + eeprom_shift, &tmp);
		if (ret < 0)
			goto err;

		tmp16 |= tmp << 8;

		pr_debug("%s: [%d]IF=%d\n", __func__, i, tmp16);

		eeprom_shift = 0x10; /* shift for the 2nd tuner params */
	}

	/* get demod clock */
	ret = af9035_rd_reg(d, 0x00d800, &tmp);
	if (ret < 0)
		goto err;

	tmp = (tmp >> 0) & 0x0f;

	for (i = 0; i < af9035_properties[0].num_adapters; i++)
		af9035_af9033_config[i].clock = clock_lut[tmp];

	ret = af9035_rd_reg(d, EEPROM_IR_MODE, &tmp);
	if (ret < 0)
		goto err;
	pr_debug("%s: ir_mode=%02x\n", __func__, tmp);

	/* don't activate rc if in HID mode or if not available */
	if (tmp == 5) {
		ret = af9035_rd_reg(d, EEPROM_IR_TYPE, &tmp);
		if (ret < 0)
			goto err;
		pr_debug("%s: ir_type=%02x\n", __func__, tmp);

		switch (tmp) {
		case 0: /* NEC */
		default:
			d->props.rc.core.protocol = RC_TYPE_NEC;
			d->props.rc.core.allowed_protos = RC_TYPE_NEC;
			break;
		case 1: /* RC6 */
			d->props.rc.core.protocol = RC_TYPE_RC6;
			d->props.rc.core.allowed_protos = RC_TYPE_RC6;
			break;
		}
		d->props.rc.core.rc_query = af9035_rc_query;
	}

	return 0;

err:
	pr_debug("%s: failed=%d\n", __func__, ret);

	return ret;
}

/* abuse that callback as there is no better one for reading eeprom */
static int af9035_read_mac_address_it9135(struct dvb_usb_device *d, u8 mac[6])
{
	int ret, i;
	u8 tmp;

	af9035_config.dual_mode = 0;

	/* get demod clock */
	ret = af9035_rd_reg(d, 0x00d800, &tmp);
	if (ret < 0)
		goto err;

	tmp = (tmp >> 0) & 0x0f;

	for (i = 0; i < af9035_properties[0].num_adapters; i++)
		af9035_af9033_config[i].clock = clock_lut_it9135[tmp];

	return 0;

err:
	pr_debug("%s: failed=%d\n", __func__, ret);

	return ret;
}

static int af9035_fc0011_tuner_callback(struct dvb_usb_device *d,
		int cmd, int arg)
{
	int ret;

	switch (cmd) {
	case FC0011_FE_CALLBACK_POWER:
		/* Tuner enable */
		ret = af9035_wr_reg_mask(d, 0xd8eb, 1, 1);
		if (ret < 0)
			goto err;

		ret = af9035_wr_reg_mask(d, 0xd8ec, 1, 1);
		if (ret < 0)
			goto err;

		ret = af9035_wr_reg_mask(d, 0xd8ed, 1, 1);
		if (ret < 0)
			goto err;

		/* LED */
		ret = af9035_wr_reg_mask(d, 0xd8d0, 1, 1);
		if (ret < 0)
			goto err;

		ret = af9035_wr_reg_mask(d, 0xd8d1, 1, 1);
		if (ret < 0)
			goto err;

		usleep_range(10000, 50000);
		break;
	case FC0011_FE_CALLBACK_RESET:
		ret = af9035_wr_reg(d, 0xd8e9, 1);
		if (ret < 0)
			goto err;

		ret = af9035_wr_reg(d, 0xd8e8, 1);
		if (ret < 0)
			goto err;

		ret = af9035_wr_reg(d, 0xd8e7, 1);
		if (ret < 0)
			goto err;

		usleep_range(10000, 20000);

		ret = af9035_wr_reg(d, 0xd8e7, 0);
		if (ret < 0)
			goto err;

		usleep_range(10000, 20000);
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	return 0;

err:
	pr_debug("%s: failed=%d\n", __func__, ret);

	return ret;
}

static int af9035_tuner_callback(struct dvb_usb_device *d, int cmd, int arg)
{
	switch (af9035_af9033_config[0].tuner) {
	case AF9033_TUNER_FC0011:
		return af9035_fc0011_tuner_callback(d, cmd, arg);
	default:
		break;
	}

	return -ENODEV;
}

static int af9035_frontend_callback(void *adapter_priv, int component,
				    int cmd, int arg)
{
	struct i2c_adapter *adap = adapter_priv;
	struct dvb_usb_device *d = i2c_get_adapdata(adap);

	switch (component) {
	case DVB_FRONTEND_COMPONENT_TUNER:
		return af9035_tuner_callback(d, cmd, arg);
	default:
		break;
	}

	return -EINVAL;
}

static int af9035_frontend_attach(struct dvb_usb_adapter *adap)
{
	int ret;

	if (af9035_config.hw_not_supported) {
		ret = -ENODEV;
		goto err;
	}

	if (adap->id == 0) {
		ret = af9035_wr_reg(adap->dev, 0x00417f,
				af9035_af9033_config[1].i2c_addr);
		if (ret < 0)
			goto err;

		ret = af9035_wr_reg(adap->dev, 0x00d81a,
				af9035_config.dual_mode);
		if (ret < 0)
			goto err;
	}

	/* attach demodulator */
	adap->fe_adap[0].fe = dvb_attach(af9033_attach,
			&af9035_af9033_config[adap->id], &adap->dev->i2c_adap);
	if (adap->fe_adap[0].fe == NULL) {
		ret = -ENODEV;
		goto err;
	}

	/* disable I2C-gate */
	adap->fe_adap[0].fe->ops.i2c_gate_ctrl = NULL;
	adap->fe_adap[0].fe->callback = af9035_frontend_callback;

	return 0;

err:
	pr_debug("%s: failed=%d\n", __func__, ret);

	return ret;
}

static struct tua9001_config af9035_tua9001_config = {
	.i2c_addr = 0x60,
};

static const struct fc0011_config af9035_fc0011_config = {
	.i2c_address = 0x60,
};

static struct mxl5007t_config af9035_mxl5007t_config = {
	.xtal_freq_hz = MxL_XTAL_24_MHZ,
	.if_freq_hz = MxL_IF_4_57_MHZ,
	.invert_if = 0,
	.loop_thru_enable = 0,
	.clk_out_enable = 0,
	.clk_out_amp = MxL_CLKOUT_AMP_0_94V,
};

static struct tda18218_config af9035_tda18218_config = {
	.i2c_address = 0x60,
	.i2c_wr_max = 21,
};

static int af9035_tuner_attach(struct dvb_usb_adapter *adap)
{
	int ret;
	struct dvb_frontend *fe;

	switch (af9035_af9033_config[adap->id].tuner) {
	case AF9033_TUNER_TUA9001:
		/* AF9035 gpiot3 = TUA9001 RESETN
		   AF9035 gpiot2 = TUA9001 RXEN */

		/* configure gpiot2 and gpiot2 as output */
		ret = af9035_wr_reg_mask(adap->dev, 0x00d8ec, 0x01, 0x01);
		if (ret < 0)
			goto err;

		ret = af9035_wr_reg_mask(adap->dev, 0x00d8ed, 0x01, 0x01);
		if (ret < 0)
			goto err;

		ret = af9035_wr_reg_mask(adap->dev, 0x00d8e8, 0x01, 0x01);
		if (ret < 0)
			goto err;

		ret = af9035_wr_reg_mask(adap->dev, 0x00d8e9, 0x01, 0x01);
		if (ret < 0)
			goto err;

		/* reset tuner */
		ret = af9035_wr_reg_mask(adap->dev, 0x00d8e7, 0x00, 0x01);
		if (ret < 0)
			goto err;

		usleep_range(2000, 20000);

		ret = af9035_wr_reg_mask(adap->dev, 0x00d8e7, 0x01, 0x01);
		if (ret < 0)
			goto err;

		/* activate tuner RX */
		/* TODO: use callback for TUA9001 RXEN */
		ret = af9035_wr_reg_mask(adap->dev, 0x00d8eb, 0x01, 0x01);
		if (ret < 0)
			goto err;

		/* attach tuner */
		fe = dvb_attach(tua9001_attach, adap->fe_adap[0].fe,
				&adap->dev->i2c_adap, &af9035_tua9001_config);
		break;
	case AF9033_TUNER_FC0011:
		fe = dvb_attach(fc0011_attach, adap->fe_adap[0].fe,
				&adap->dev->i2c_adap, &af9035_fc0011_config);
		break;
	case AF9033_TUNER_MXL5007T:
		ret = af9035_wr_reg(adap->dev, 0x00d8e0, 1);
		if (ret < 0)
			goto err;
		ret = af9035_wr_reg(adap->dev, 0x00d8e1, 1);
		if (ret < 0)
			goto err;
		ret = af9035_wr_reg(adap->dev, 0x00d8df, 0);
		if (ret < 0)
			goto err;

		msleep(30);

		ret = af9035_wr_reg(adap->dev, 0x00d8df, 1);
		if (ret < 0)
			goto err;

		msleep(300);

		ret = af9035_wr_reg(adap->dev, 0x00d8c0, 1);
		if (ret < 0)
			goto err;
		ret = af9035_wr_reg(adap->dev, 0x00d8c1, 1);
		if (ret < 0)
			goto err;
		ret = af9035_wr_reg(adap->dev, 0x00d8bf, 0);
		if (ret < 0)
			goto err;
		ret = af9035_wr_reg(adap->dev, 0x00d8b4, 1);
		if (ret < 0)
			goto err;
		ret = af9035_wr_reg(adap->dev, 0x00d8b5, 1);
		if (ret < 0)
			goto err;
		ret = af9035_wr_reg(adap->dev, 0x00d8b3, 1);
		if (ret < 0)
			goto err;

		/* attach tuner */
		fe = dvb_attach(mxl5007t_attach, adap->fe_adap[0].fe,
				&adap->dev->i2c_adap, 0x60, &af9035_mxl5007t_config);
		break;
	case AF9033_TUNER_TDA18218:
		/* attach tuner */
		fe = dvb_attach(tda18218_attach, adap->fe_adap[0].fe,
				&adap->dev->i2c_adap, &af9035_tda18218_config);
		break;
	default:
		fe = NULL;
	}

	if (fe == NULL) {
		ret = -ENODEV;
		goto err;
	}

	return 0;

err:
	pr_debug("%s: failed=%d\n", __func__, ret);

	return ret;
}

enum af9035_id_entry {
	AF9035_15A4_9035,
	AF9035_15A4_1001,
	AF9035_0CCD_0093,
	AF9035_07CA_A835,
	AF9035_07CA_B835,
	AF9035_07CA_1867,
	AF9035_07CA_A867,
	AF9035_07CA_0825,
};

static struct usb_device_id af9035_id[] = {
	[AF9035_15A4_9035] = {
		USB_DEVICE(USB_VID_AFATECH, USB_PID_AFATECH_AF9035)},
	[AF9035_15A4_1001] = {
		USB_DEVICE(USB_VID_AFATECH, USB_PID_AFATECH_AF9035_2)},
	[AF9035_0CCD_0093] = {
		USB_DEVICE(USB_VID_TERRATEC, USB_PID_TERRATEC_CINERGY_T_STICK)},
	[AF9035_07CA_A835] = {
		USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_A835)},
	[AF9035_07CA_B835] = {
		USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_B835)},
	[AF9035_07CA_1867] = {
		USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_1867)},
	[AF9035_07CA_A867] = {
		USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_A867)},
	[AF9035_07CA_0825] = {
		USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_TWINSTAR)},
	{},
};

MODULE_DEVICE_TABLE(usb, af9035_id);

static struct dvb_usb_device_properties af9035_properties[] = {
	{
		.caps = DVB_USB_IS_AN_I2C_ADAPTER,

		.usb_ctrl = DEVICE_SPECIFIC,
		.download_firmware = af9035_download_firmware,
		.firmware = "dvb-usb-af9035-02.fw",
		.no_reconnect = 1,

		.num_adapters = 1,
		.adapter = {
			{
				.num_frontends = 1,
				.fe = {
					{
						.frontend_attach = af9035_frontend_attach,
						.tuner_attach = af9035_tuner_attach,
						.stream = {
							.type = USB_BULK,
							.count = 6,
							.endpoint = 0x84,
							.u = {
								.bulk = {
									.buffersize = (87 * 188),
								}
							}
						}
					}
				}
			}
		},

		.identify_state = af9035_identify_state,
		.read_mac_address = af9035_read_mac_address,

		.i2c_algo = &af9035_i2c_algo,

		.rc.core = {
			.protocol       = RC_TYPE_UNKNOWN,
			.module_name    = "af9035",
			.rc_query       = NULL,
			.rc_interval    = AF9035_POLL,
			.allowed_protos = RC_TYPE_UNKNOWN,
			.rc_codes       = RC_MAP_EMPTY,
		},
		.num_device_descs = 5,
		.devices = {
			{
				.name = "Afatech AF9035 reference design",
				.cold_ids = {
					&af9035_id[AF9035_15A4_9035],
					&af9035_id[AF9035_15A4_1001],
				},
			}, {
				.name = "TerraTec Cinergy T Stick",
				.cold_ids = {
					&af9035_id[AF9035_0CCD_0093],
				},
			}, {
				.name = "AVerMedia AVerTV Volar HD/PRO (A835)",
				.cold_ids = {
					&af9035_id[AF9035_07CA_A835],
					&af9035_id[AF9035_07CA_B835],
				},
			}, {
				.name = "AVerMedia HD Volar (A867)",
				.cold_ids = {
					&af9035_id[AF9035_07CA_1867],
					&af9035_id[AF9035_07CA_A867],
				},
			}, {
				.name = "AVerMedia Twinstar (A825)",
				.cold_ids = {
					&af9035_id[AF9035_07CA_0825],
				},
			},
		}
	},
	{
		.caps = DVB_USB_IS_AN_I2C_ADAPTER,

		.usb_ctrl = DEVICE_SPECIFIC,
		.download_firmware = af9035_download_firmware_it9135,
		.firmware = "dvb-usb-it9135-01.fw",
		.no_reconnect = 1,

		.num_adapters = 1,
		.adapter = {
			{
				.num_frontends = 1,
				.fe = {
					{
						.frontend_attach = af9035_frontend_attach,
						.tuner_attach = af9035_tuner_attach,
						.stream = {
							.type = USB_BULK,
							.count = 6,
							.endpoint = 0x84,
							.u = {
								.bulk = {
									.buffersize = (87 * 188),
								}
							}
						}
					}
				}
			}
		},

		.identify_state = af9035_identify_state,
		.read_mac_address = af9035_read_mac_address_it9135,

		.i2c_algo = &af9035_i2c_algo,

		.num_device_descs = 0, /* disabled as no support for IT9135 */
		.devices = {
			{
				.name = "ITE Tech. IT9135 reference design",
			},
		}
	},
};

static int af9035_usb_probe(struct usb_interface *intf,
			    const struct usb_device_id *id)
{
	int ret, i;
	struct dvb_usb_device *d = NULL;
	struct usb_device *udev;
	bool found;

	pr_debug("%s: interface=%d\n", __func__,
			intf->cur_altsetting->desc.bInterfaceNumber);

	/* interface 0 is used by DVB-T receiver and
	   interface 1 is for remote controller (HID) */
	if (intf->cur_altsetting->desc.bInterfaceNumber != 0)
		return 0;

	/* Dynamic USB ID support. Replaces first device ID with current one. */
	udev = interface_to_usbdev(intf);

	for (i = 0, found = false; i < ARRAY_SIZE(af9035_id) - 1; i++) {
		if (af9035_id[i].idVendor ==
				le16_to_cpu(udev->descriptor.idVendor) &&
				af9035_id[i].idProduct ==
				le16_to_cpu(udev->descriptor.idProduct)) {
			found = true;
			break;
		}
	}

	if (!found) {
		pr_debug("%s: using dynamic ID %04x:%04x\n", __func__,
				le16_to_cpu(udev->descriptor.idVendor),
				le16_to_cpu(udev->descriptor.idProduct));
		af9035_properties[0].devices[0].cold_ids[0]->idVendor =
				le16_to_cpu(udev->descriptor.idVendor);
		af9035_properties[0].devices[0].cold_ids[0]->idProduct =
				le16_to_cpu(udev->descriptor.idProduct);
	}


	for (i = 0; i < af9035_properties_count; i++) {
		ret = dvb_usb_device_init(intf, &af9035_properties[i],
				THIS_MODULE, &d, adapter_nr);

		if (ret == -ENODEV)
			continue;
		else
			break;
	}

	if (ret < 0)
		goto err;

	if (d) {
		ret = af9035_init(d);
		if (ret < 0)
			goto err;
	}

	return 0;

err:
	pr_debug("%s: failed=%d\n", __func__, ret);

	return ret;
}

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver af9035_usb_driver = {
	.name = "dvb_usb_af9035",
	.probe = af9035_usb_probe,
	.disconnect = dvb_usb_device_exit,
	.id_table = af9035_id,
};

module_usb_driver(af9035_usb_driver);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Afatech AF9035 driver");
MODULE_LICENSE("GPL");
