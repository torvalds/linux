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

#include <linux/hash.h>
#include <linux/slab.h>

#include "af9015.h"
#include "af9013.h"
#include "mt2060.h"
#include "qt1010.h"
#include "tda18271.h"
#include "mxl5005s.h"
#include "mc44s803.h"
#include "tda18218.h"
#include "mxl5007t.h"

static int dvb_usb_af9015_debug;
module_param_named(debug, dvb_usb_af9015_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level" DVB_USB_DEBUG_STATUS);
static int dvb_usb_af9015_remote;
module_param_named(remote, dvb_usb_af9015_remote, int, 0644);
MODULE_PARM_DESC(remote, "select remote");
DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static DEFINE_MUTEX(af9015_usb_mutex);

static struct af9015_config af9015_config;
static struct dvb_usb_device_properties af9015_properties[3];
static int af9015_properties_count = ARRAY_SIZE(af9015_properties);

static struct af9013_config af9015_af9013_config[] = {
	{
		.demod_address = AF9015_I2C_DEMOD,
		.output_mode = AF9013_OUTPUT_MODE_USB,
		.api_version = { 0, 1, 9, 0 },
		.gpio[0] = AF9013_GPIO_HI,
		.gpio[3] = AF9013_GPIO_TUNER_ON,

	}, {
		.output_mode = AF9013_OUTPUT_MODE_SERIAL,
		.api_version = { 0, 1, 9, 0 },
		.gpio[0] = AF9013_GPIO_TUNER_ON,
		.gpio[1] = AF9013_GPIO_LO,
	}
};

static int af9015_rw_udev(struct usb_device *udev, struct req_t *req)
{
#define BUF_LEN 63
#define REQ_HDR_LEN 8 /* send header size */
#define ACK_HDR_LEN 2 /* rece header size */
	int act_len, ret;
	u8 buf[BUF_LEN];
	u8 write = 1;
	u8 msg_len = REQ_HDR_LEN;
	static u8 seq; /* packet sequence number */

	if (mutex_lock_interruptible(&af9015_usb_mutex) < 0)
		return -EAGAIN;

	buf[0] = req->cmd;
	buf[1] = seq++;
	buf[2] = req->i2c_addr;
	buf[3] = req->addr >> 8;
	buf[4] = req->addr & 0xff;
	buf[5] = req->mbox;
	buf[6] = req->addr_len;
	buf[7] = req->data_len;

	switch (req->cmd) {
	case GET_CONFIG:
	case READ_MEMORY:
	case RECONNECT_USB:
	case GET_IR_CODE:
		write = 0;
		break;
	case READ_I2C:
		write = 0;
		buf[2] |= 0x01; /* set I2C direction */
	case WRITE_I2C:
		buf[0] = READ_WRITE_I2C;
		break;
	case WRITE_MEMORY:
		if (((req->addr & 0xff00) == 0xff00) ||
		    ((req->addr & 0xff00) == 0xae00))
			buf[0] = WRITE_VIRTUAL_MEMORY;
	case WRITE_VIRTUAL_MEMORY:
	case COPY_FIRMWARE:
	case DOWNLOAD_FIRMWARE:
	case BOOT:
		break;
	default:
		err("unknown command:%d", req->cmd);
		ret = -1;
		goto error_unlock;
	}

	/* buffer overflow check */
	if ((write && (req->data_len > BUF_LEN - REQ_HDR_LEN)) ||
		(!write && (req->data_len > BUF_LEN - ACK_HDR_LEN))) {
		err("too much data; cmd:%d len:%d", req->cmd, req->data_len);
		ret = -EINVAL;
		goto error_unlock;
	}

	/* write requested */
	if (write) {
		memcpy(&buf[REQ_HDR_LEN], req->data, req->data_len);
		msg_len += req->data_len;
	}

	deb_xfer(">>> ");
	debug_dump(buf, msg_len, deb_xfer);

	/* send req */
	ret = usb_bulk_msg(udev, usb_sndbulkpipe(udev, 0x02), buf, msg_len,
		&act_len, AF9015_USB_TIMEOUT);
	if (ret)
		err("bulk message failed:%d (%d/%d)", ret, msg_len, act_len);
	else
		if (act_len != msg_len)
			ret = -1; /* all data is not send */
	if (ret)
		goto error_unlock;

	/* no ack for those packets */
	if (req->cmd == DOWNLOAD_FIRMWARE || req->cmd == RECONNECT_USB)
		goto exit_unlock;

	/* write receives seq + status = 2 bytes
	   read receives seq + status + data = 2 + N bytes */
	msg_len = ACK_HDR_LEN;
	if (!write)
		msg_len += req->data_len;

	ret = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, 0x81), buf, msg_len,
		&act_len, AF9015_USB_TIMEOUT);
	if (ret) {
		err("recv bulk message failed:%d", ret);
		ret = -1;
		goto error_unlock;
	}

	deb_xfer("<<< ");
	debug_dump(buf, act_len, deb_xfer);

	/* remote controller query status is 1 if remote code is not received */
	if (req->cmd == GET_IR_CODE && buf[1] == 1) {
		buf[1] = 0; /* clear command "error" status */
		memset(&buf[2], 0, req->data_len);
		buf[3] = 1; /* no remote code received mark */
	}

	/* check status */
	if (buf[1]) {
		err("command failed:%d", buf[1]);
		ret = -1;
		goto error_unlock;
	}

	/* read request, copy returned data to return buf */
	if (!write)
		memcpy(req->data, &buf[ACK_HDR_LEN], req->data_len);

error_unlock:
exit_unlock:
	mutex_unlock(&af9015_usb_mutex);

	return ret;
}

static int af9015_ctrl_msg(struct dvb_usb_device *d, struct req_t *req)
{
	return af9015_rw_udev(d->udev, req);
}

static int af9015_write_regs(struct dvb_usb_device *d, u16 addr, u8 *val,
	u8 len)
{
	struct req_t req = {WRITE_MEMORY, AF9015_I2C_DEMOD, addr, 0, 0, len,
		val};
	return af9015_ctrl_msg(d, &req);
}

static int af9015_write_reg(struct dvb_usb_device *d, u16 addr, u8 val)
{
	return af9015_write_regs(d, addr, &val, 1);
}

static int af9015_read_regs(struct dvb_usb_device *d, u16 addr, u8 *val, u8 len)
{
	struct req_t req = {READ_MEMORY, AF9015_I2C_DEMOD, addr, 0, 0, len,
		val};
	return af9015_ctrl_msg(d, &req);
}

static int af9015_read_reg(struct dvb_usb_device *d, u16 addr, u8 *val)
{
	return af9015_read_regs(d, addr, val, 1);
}

static int af9015_write_reg_i2c(struct dvb_usb_device *d, u8 addr, u16 reg,
	u8 val)
{
	struct req_t req = {WRITE_I2C, addr, reg, 1, 1, 1, &val};

	if (addr == af9015_af9013_config[0].demod_address ||
	    addr == af9015_af9013_config[1].demod_address)
		req.addr_len = 3;

	return af9015_ctrl_msg(d, &req);
}

static int af9015_read_reg_i2c(struct dvb_usb_device *d, u8 addr, u16 reg,
	u8 *val)
{
	struct req_t req = {READ_I2C, addr, reg, 0, 1, 1, val};

	if (addr == af9015_af9013_config[0].demod_address ||
	    addr == af9015_af9013_config[1].demod_address)
		req.addr_len = 3;

	return af9015_ctrl_msg(d, &req);
}

static int af9015_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msg[],
	int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	int ret = 0, i = 0;
	u16 addr;
	u8 uninitialized_var(mbox), addr_len;
	struct req_t req;

/* TODO: implement bus lock

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
		if (msg[i].addr == af9015_af9013_config[0].demod_address ||
		    msg[i].addr == af9015_af9013_config[1].demod_address) {
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
			if (msg[i].addr ==
				af9015_af9013_config[0].demod_address)
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
			ret = -EINVAL;
			if (msg[i].addr ==
				af9015_af9013_config[0].demod_address)
				goto error;
			else
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
			if (msg[i].addr ==
				af9015_af9013_config[0].demod_address)
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

static int af9015_init_endpoint(struct dvb_usb_device *d)
{
	int ret;
	u16 frame_size;
	u8  packet_size;
	deb_info("%s: USB speed:%d\n", __func__, d->udev->speed);

	/* Windows driver uses packet count 21 for USB1.1 and 348 for USB2.0.
	   We use smaller - about 1/4 from the original, 5 and 87. */
#define TS_PACKET_SIZE            188

#define TS_USB20_PACKET_COUNT      87
#define TS_USB20_FRAME_SIZE       (TS_PACKET_SIZE*TS_USB20_PACKET_COUNT)

#define TS_USB11_PACKET_COUNT       5
#define TS_USB11_FRAME_SIZE       (TS_PACKET_SIZE*TS_USB11_PACKET_COUNT)

#define TS_USB20_MAX_PACKET_SIZE  512
#define TS_USB11_MAX_PACKET_SIZE   64

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
	if (af9015_config.dual_mode) {
		ret = af9015_set_reg_bit(d, 0xdd11, 6); /* enable EP5 */
		if (ret)
			goto error;
	}
	ret = af9015_clear_reg_bit(d, 0xdd13, 5); /* disable EP4 NAK */
	if (ret)
		goto error;
	if (af9015_config.dual_mode) {
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
	if (af9015_config.dual_mode) {
		ret = af9015_clear_reg_bit(d, 0xd50b, 1); /* negate EP5 reset */
		if (ret)
			goto error;
	}

	/* enable / disable mp2if2 */
	if (af9015_config.dual_mode)
		ret = af9015_set_reg_bit(d, 0xd50b, 0);
	else
		ret = af9015_clear_reg_bit(d, 0xd50b, 0);
error:
	if (ret)
		err("endpoint init failed:%d", ret);
	return ret;
}

static int af9015_copy_firmware(struct dvb_usb_device *d)
{
	int ret;
	u8 fw_params[4];
	u8 val, i;
	struct req_t req = {COPY_FIRMWARE, 0, 0x5100, 0, 0, sizeof(fw_params),
		fw_params };
	deb_info("%s:\n", __func__);

	fw_params[0] = af9015_config.firmware_size >> 8;
	fw_params[1] = af9015_config.firmware_size & 0xff;
	fw_params[2] = af9015_config.firmware_checksum >> 8;
	fw_params[3] = af9015_config.firmware_checksum & 0xff;

	/* wait 2nd demodulator ready */
	msleep(100);

	ret = af9015_read_reg_i2c(d,
		af9015_af9013_config[1].demod_address, 0x98be, &val);
	if (ret)
		goto error;
	else
		deb_info("%s: firmware status:%02x\n", __func__, val);

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
		err("firmware copy cmd failed:%d", ret);
	deb_info("%s: firmware copy done\n", __func__);

	/* set I2C master clock back to normal */
	ret = af9015_write_reg(d, 0xd416, 0x14); /* 0x14 * 400ns */
	if (ret)
		goto error;

	/* request boot firmware */
	ret = af9015_write_reg_i2c(d, af9015_af9013_config[1].demod_address,
		0xe205, 1);
	deb_info("%s: firmware boot cmd status:%d\n", __func__, ret);
	if (ret)
		goto error;

	for (i = 0; i < 15; i++) {
		msleep(100);

		/* check firmware status */
		ret = af9015_read_reg_i2c(d,
			af9015_af9013_config[1].demod_address, 0x98be, &val);
		deb_info("%s: firmware status cmd status:%d fw status:%02x\n",
			__func__, ret, val);
		if (ret)
			goto error;

		if (val == 0x0c || val == 0x04) /* success or fail */
			break;
	}

	if (val == 0x04) {
		err("firmware did not run");
		ret = -1;
	} else if (val != 0x0c) {
		err("firmware boot timeout");
		ret = -1;
	}

error:
exit:
	return ret;
}

/* hash (and dump) eeprom */
static int af9015_eeprom_hash(struct usb_device *udev)
{
	static const unsigned int eeprom_size = 256;
	unsigned int reg;
	int ret;
	u8 val, *eeprom;
	struct req_t req = {READ_I2C, AF9015_I2C_EEPROM, 0, 0, 1, 1, &val};

	eeprom = kmalloc(eeprom_size, GFP_KERNEL);
	if (eeprom == NULL)
		return -ENOMEM;

	for (reg = 0; reg < eeprom_size; reg++) {
		req.addr = reg;
		ret = af9015_rw_udev(udev, &req);
		if (ret)
			goto free;
		eeprom[reg] = val;
	}

	if (dvb_usb_af9015_debug & 0x01)
		print_hex_dump_bytes("", DUMP_PREFIX_OFFSET, eeprom,
				eeprom_size);

	BUG_ON(eeprom_size % 4);

	af9015_config.eeprom_sum = 0;
	for (reg = 0; reg < eeprom_size / sizeof(u32); reg++) {
		af9015_config.eeprom_sum *= GOLDEN_RATIO_PRIME_32;
		af9015_config.eeprom_sum += le32_to_cpu(((u32 *)eeprom)[reg]);
	}

	deb_info("%s: eeprom sum=%.8x\n", __func__, af9015_config.eeprom_sum);

	ret = 0;
free:
	kfree(eeprom);
	return ret;
}

static int af9015_init(struct dvb_usb_device *d)
{
	int ret;
	deb_info("%s:\n", __func__);

	ret = af9015_init_endpoint(d);
	if (ret)
		goto error;

error:
	return ret;
}

static int af9015_pid_filter_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	int ret;
	deb_info("%s: onoff:%d\n", __func__, onoff);

	if (onoff)
		ret = af9015_set_reg_bit(adap->dev, 0xd503, 0);
	else
		ret = af9015_clear_reg_bit(adap->dev, 0xd503, 0);

	return ret;
}

static int af9015_pid_filter(struct dvb_usb_adapter *adap, int index, u16 pid,
	int onoff)
{
	int ret;
	u8 idx;

	deb_info("%s: set pid filter, index %d, pid %x, onoff %d\n",
		__func__, index, pid, onoff);

	ret = af9015_write_reg(adap->dev, 0xd505, (pid & 0xff));
	if (ret)
		goto error;

	ret = af9015_write_reg(adap->dev, 0xd506, (pid >> 8));
	if (ret)
		goto error;

	idx = ((index & 0x1f) | (1 << 5));
	ret = af9015_write_reg(adap->dev, 0xd504, idx);

error:
	return ret;
}

static int af9015_download_firmware(struct usb_device *udev,
	const struct firmware *fw)
{
	int i, len, packets, remainder, ret;
	struct req_t req = {DOWNLOAD_FIRMWARE, 0, 0, 0, 0, 0, NULL};
	u16 addr = 0x5100; /* firmware start address */
	u16 checksum = 0;

	deb_info("%s:\n", __func__);

	/* calc checksum */
	for (i = 0; i < fw->size; i++)
		checksum += fw->data[i];

	af9015_config.firmware_size = fw->size;
	af9015_config.firmware_checksum = checksum;

	#define FW_PACKET_MAX_DATA  55

	packets = fw->size / FW_PACKET_MAX_DATA;
	remainder = fw->size % FW_PACKET_MAX_DATA;
	len = FW_PACKET_MAX_DATA;
	for (i = 0; i <= packets; i++) {
		if (i == packets)  /* set size of the last packet */
			len = remainder;

		req.data_len = len;
		req.data = (u8 *)(fw->data + i * FW_PACKET_MAX_DATA);
		req.addr = addr;
		addr += FW_PACKET_MAX_DATA;

		ret = af9015_rw_udev(udev, &req);
		if (ret) {
			err("firmware download failed at packet %d with " \
				"code %d", i, ret);
			goto error;
		}
	}

	/* firmware loaded, request boot */
	req.cmd = BOOT;
	ret = af9015_rw_udev(udev, &req);
	if (ret) {
		err("firmware boot failed:%d", ret);
		goto error;
	}

error:
	return ret;
}

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
	{ }
};

static const struct af9015_rc_setup af9015_rc_setup_usbids[] = {
	{ (USB_VID_TERRATEC << 16) + USB_PID_TERRATEC_CINERGY_T_STICK_DUAL_RC,
		RC_MAP_TERRATEC_SLIM },
	{ (USB_VID_VISIONPLUS << 16) + USB_PID_AZUREWAVE_AD_TU700,
		RC_MAP_AZUREWAVE_AD_TU700 },
	{ (USB_VID_VISIONPLUS << 16) + USB_PID_TINYTWIN,
		RC_MAP_AZUREWAVE_AD_TU700 },
	{ (USB_VID_MSI_2 << 16) + USB_PID_MSI_DIGI_VOX_MINI_III,
		RC_MAP_MSI_DIGIVOX_III },
	{ (USB_VID_LEADTEK << 16) + USB_PID_WINFAST_DTV_DONGLE_GOLD,
		RC_MAP_LEADTEK_Y04G0051 },
	{ (USB_VID_AVERMEDIA << 16) + USB_PID_AVERMEDIA_VOLAR_X,
		RC_MAP_AVERMEDIA_M135A },
	{ (USB_VID_AFATECH << 16) + USB_PID_TREKSTOR_DVBT,
		RC_MAP_TREKSTOR },
	{ (USB_VID_KWORLD_2 << 16) + USB_PID_TINYTWIN_2,
		RC_MAP_DIGITALNOW_TINYTWIN },
	{ (USB_VID_GTEK << 16) + USB_PID_TINYTWIN_3,
		RC_MAP_DIGITALNOW_TINYTWIN },
	{ }
};

static void af9015_set_remote_config(struct usb_device *udev,
		struct dvb_usb_device_properties *props)
{
	u16 vid = le16_to_cpu(udev->descriptor.idVendor);
	u16 pid = le16_to_cpu(udev->descriptor.idProduct);

	/* try to load remote based module param */
	props->rc.core.rc_codes = af9015_rc_setup_match(
		dvb_usb_af9015_remote, af9015_rc_setup_modparam);

	/* try to load remote based eeprom hash */
	if (!props->rc.core.rc_codes)
		props->rc.core.rc_codes = af9015_rc_setup_match(
			af9015_config.eeprom_sum, af9015_rc_setup_hashes);

	/* try to load remote based USB ID */
	if (!props->rc.core.rc_codes)
		props->rc.core.rc_codes = af9015_rc_setup_match(
			(vid << 16) + pid, af9015_rc_setup_usbids);

	/* try to load remote based USB iManufacturer string */
	if (!props->rc.core.rc_codes && vid == USB_VID_AFATECH) {
		/* Check USB manufacturer and product strings and try
		   to determine correct remote in case of chip vendor
		   reference IDs are used.
		   DO NOT ADD ANYTHING NEW HERE. Use hashes instead. */
		char manufacturer[10];
		memset(manufacturer, 0, sizeof(manufacturer));
		usb_string(udev, udev->descriptor.iManufacturer,
			manufacturer, sizeof(manufacturer));
		if (!strcmp("MSI", manufacturer)) {
			/* iManufacturer 1 MSI
			   iProduct      2 MSI K-VOX */
			props->rc.core.rc_codes = af9015_rc_setup_match(
				AF9015_REMOTE_MSI_DIGIVOX_MINI_II_V3,
				af9015_rc_setup_modparam);
		}
	}

	/* finally load "empty" just for leaving IR receiver enabled */
	if (!props->rc.core.rc_codes)
		props->rc.core.rc_codes = RC_MAP_EMPTY;

	return;
}

static int af9015_read_config(struct usb_device *udev)
{
	int ret;
	u8 val, i, offset = 0;
	struct req_t req = {READ_I2C, AF9015_I2C_EEPROM, 0, 0, 1, 1, &val};

	/* IR remote controller */
	req.addr = AF9015_EEPROM_IR_MODE;
	/* first message will timeout often due to possible hw bug */
	for (i = 0; i < 4; i++) {
		ret = af9015_rw_udev(udev, &req);
		if (!ret)
			break;
	}
	if (ret)
		goto error;

	ret = af9015_eeprom_hash(udev);
	if (ret)
		goto error;

	deb_info("%s: IR mode:%d\n", __func__, val);
	for (i = 0; i < af9015_properties_count; i++) {
		if (val == AF9015_IR_MODE_DISABLED)
			af9015_properties[i].rc.core.rc_codes = NULL;
		else
			af9015_set_remote_config(udev, &af9015_properties[i]);
	}

	/* TS mode - one or two receivers */
	req.addr = AF9015_EEPROM_TS_MODE;
	ret = af9015_rw_udev(udev, &req);
	if (ret)
		goto error;
	af9015_config.dual_mode = val;
	deb_info("%s: TS mode:%d\n", __func__, af9015_config.dual_mode);

	/* Set adapter0 buffer size according to USB port speed, adapter1 buffer
	   size can be static because it is enabled only USB2.0 */
	for (i = 0; i < af9015_properties_count; i++) {
		/* USB1.1 set smaller buffersize and disable 2nd adapter */
		if (udev->speed == USB_SPEED_FULL) {
			af9015_properties[i].adapter[0].stream.u.bulk.buffersize
				= TS_USB11_FRAME_SIZE;
			/* disable 2nd adapter because we don't have
			   PID-filters */
			af9015_config.dual_mode = 0;
		} else {
			af9015_properties[i].adapter[0].stream.u.bulk.buffersize
				= TS_USB20_FRAME_SIZE;
		}
	}

	if (af9015_config.dual_mode) {
		/* read 2nd demodulator I2C address */
		req.addr = AF9015_EEPROM_DEMOD2_I2C;
		ret = af9015_rw_udev(udev, &req);
		if (ret)
			goto error;
		af9015_af9013_config[1].demod_address = val;

		/* enable 2nd adapter */
		for (i = 0; i < af9015_properties_count; i++)
			af9015_properties[i].num_adapters = 2;

	} else {
		 /* disable 2nd adapter */
		for (i = 0; i < af9015_properties_count; i++)
			af9015_properties[i].num_adapters = 1;
	}

	for (i = 0; i < af9015_properties[0].num_adapters; i++) {
		if (i == 1)
			offset = AF9015_EEPROM_OFFSET;
		/* xtal */
		req.addr = AF9015_EEPROM_XTAL_TYPE1 + offset;
		ret = af9015_rw_udev(udev, &req);
		if (ret)
			goto error;
		switch (val) {
		case 0:
			af9015_af9013_config[i].adc_clock = 28800;
			break;
		case 1:
			af9015_af9013_config[i].adc_clock = 20480;
			break;
		case 2:
			af9015_af9013_config[i].adc_clock = 28000;
			break;
		case 3:
			af9015_af9013_config[i].adc_clock = 25000;
			break;
		};
		deb_info("%s: [%d] xtal:%d set adc_clock:%d\n", __func__, i,
			val, af9015_af9013_config[i].adc_clock);

		/* tuner IF */
		req.addr = AF9015_EEPROM_IF1H + offset;
		ret = af9015_rw_udev(udev, &req);
		if (ret)
			goto error;
		af9015_af9013_config[i].tuner_if = val << 8;
		req.addr = AF9015_EEPROM_IF1L + offset;
		ret = af9015_rw_udev(udev, &req);
		if (ret)
			goto error;
		af9015_af9013_config[i].tuner_if += val;
		deb_info("%s: [%d] IF1:%d\n", __func__, i,
			af9015_af9013_config[0].tuner_if);

		/* MT2060 IF1 */
		req.addr = AF9015_EEPROM_MT2060_IF1H  + offset;
		ret = af9015_rw_udev(udev, &req);
		if (ret)
			goto error;
		af9015_config.mt2060_if1[i] = val << 8;
		req.addr = AF9015_EEPROM_MT2060_IF1L + offset;
		ret = af9015_rw_udev(udev, &req);
		if (ret)
			goto error;
		af9015_config.mt2060_if1[i] += val;
		deb_info("%s: [%d] MT2060 IF1:%d\n", __func__, i,
			af9015_config.mt2060_if1[i]);

		/* tuner */
		req.addr =  AF9015_EEPROM_TUNER_ID1 + offset;
		ret = af9015_rw_udev(udev, &req);
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
			af9015_af9013_config[i].rf_spec_inv = 1;
			break;
		case AF9013_TUNER_MXL5003D:
		case AF9013_TUNER_MXL5005D:
		case AF9013_TUNER_MXL5005R:
		case AF9013_TUNER_MXL5007T:
			af9015_af9013_config[i].rf_spec_inv = 0;
			break;
		case AF9013_TUNER_MC44S803:
			af9015_af9013_config[i].gpio[1] = AF9013_GPIO_LO;
			af9015_af9013_config[i].rf_spec_inv = 1;
			break;
		default:
			warn("tuner id:%d not supported, please report!", val);
			return -ENODEV;
		};

		af9015_af9013_config[i].tuner = val;
		deb_info("%s: [%d] tuner id:%d\n", __func__, i, val);
	}

error:
	if (ret)
		err("eeprom read failed:%d", ret);

	/* AverMedia AVerTV Volar Black HD (A850) device have bad EEPROM
	   content :-( Override some wrong values here. Ditto for the
	   AVerTV Red HD+ (A850T) device. */
	if (le16_to_cpu(udev->descriptor.idVendor) == USB_VID_AVERMEDIA &&
		((le16_to_cpu(udev->descriptor.idProduct) ==
			USB_PID_AVERMEDIA_A850) ||
		(le16_to_cpu(udev->descriptor.idProduct) ==
			USB_PID_AVERMEDIA_A850T))) {
		deb_info("%s: AverMedia A850: overriding config\n", __func__);
		/* disable dual mode */
		af9015_config.dual_mode = 0;
		 /* disable 2nd adapter */
		for (i = 0; i < af9015_properties_count; i++)
			af9015_properties[i].num_adapters = 1;

		/* set correct IF */
		af9015_af9013_config[0].tuner_if = 4570;
	}

	return ret;
}

static int af9015_identify_state(struct usb_device *udev,
				 struct dvb_usb_device_properties *props,
				 struct dvb_usb_device_description **desc,
				 int *cold)
{
	int ret;
	u8 reply;
	struct req_t req = {GET_CONFIG, 0, 0, 0, 0, 1, &reply};

	ret = af9015_rw_udev(udev, &req);
	if (ret)
		return ret;

	deb_info("%s: reply:%02x\n", __func__, reply);
	if (reply == 0x02)
		*cold = 0;
	else
		*cold = 1;

	return ret;
}

static int af9015_rc_query(struct dvb_usb_device *d)
{
	struct af9015_state *priv = d->priv;
	int ret;
	u8 buf[16];

	/* read registers needed to detect remote controller code */
	ret = af9015_read_regs(d, 0x98d9, buf, sizeof(buf));
	if (ret)
		goto error;

	if (buf[14] || buf[15]) {
		deb_rc("%s: key pressed %02x %02x %02x %02x\n", __func__,
			buf[12], buf[13], buf[14], buf[15]);

		/* clean IR code from mem */
		ret = af9015_write_regs(d, 0x98e5, "\x00\x00\x00\x00", 4);
		if (ret)
			goto error;

		if (buf[14] == (u8) ~buf[15]) {
			if (buf[12] == (u8) ~buf[13]) {
				/* NEC */
				priv->rc_keycode = buf[12] << 8 | buf[14];
			} else {
				/* NEC extended*/
				priv->rc_keycode = buf[12] << 16 |
					buf[13] << 8 | buf[14];
			}
			ir_keydown(d->rc_input_dev, priv->rc_keycode, 0);
		} else {
			priv->rc_keycode = 0; /* clear just for sure */
		}
	} else if (priv->rc_repeat != buf[6] || buf[0]) {
		deb_rc("%s: key repeated\n", __func__);
		ir_keydown(d->rc_input_dev, priv->rc_keycode, 0);
	} else {
		deb_rc("%s: no key press\n", __func__);
	}

	priv->rc_repeat = buf[6];

error:
	if (ret)
		err("%s: failed:%d", __func__, ret);

	return ret;
}

/* init 2nd I2C adapter */
static int af9015_i2c_init(struct dvb_usb_device *d)
{
	int ret;
	struct af9015_state *state = d->priv;
	deb_info("%s:\n", __func__);

	strncpy(state->i2c_adap.name, d->desc->name,
		sizeof(state->i2c_adap.name));
	state->i2c_adap.algo      = d->props.i2c_algo;
	state->i2c_adap.algo_data = NULL;
	state->i2c_adap.dev.parent = &d->udev->dev;

	i2c_set_adapdata(&state->i2c_adap, d);

	ret = i2c_add_adapter(&state->i2c_adap);
	if (ret < 0)
		err("could not add i2c adapter");

	return ret;
}

static int af9015_af9013_frontend_attach(struct dvb_usb_adapter *adap)
{
	int ret;
	struct af9015_state *state = adap->dev->priv;
	struct i2c_adapter *i2c_adap;

	if (adap->id == 0) {
		/* select I2C adapter */
		i2c_adap = &adap->dev->i2c_adap;

		deb_info("%s: init I2C\n", __func__);
		ret = af9015_i2c_init(adap->dev);
	} else {
		/* select I2C adapter */
		i2c_adap = &state->i2c_adap;

		/* copy firmware to 2nd demodulator */
		if (af9015_config.dual_mode) {
			ret = af9015_copy_firmware(adap->dev);
			if (ret) {
				err("firmware copy to 2nd frontend " \
					"failed, will disable it");
				af9015_config.dual_mode = 0;
				return -ENODEV;
			}
		} else {
			return -ENODEV;
		}
	}

	/* attach demodulator */
	adap->fe = dvb_attach(af9013_attach, &af9015_af9013_config[adap->id],
		i2c_adap);

	return adap->fe == NULL ? -ENODEV : 0;
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
	struct af9015_state *state = adap->dev->priv;
	struct i2c_adapter *i2c_adap;
	int ret;
	deb_info("%s:\n", __func__);

	/* select I2C adapter */
	if (adap->id == 0)
		i2c_adap = &adap->dev->i2c_adap;
	else
		i2c_adap = &state->i2c_adap;

	switch (af9015_af9013_config[adap->id].tuner) {
	case AF9013_TUNER_MT2060:
	case AF9013_TUNER_MT2060_2:
		ret = dvb_attach(mt2060_attach, adap->fe, i2c_adap,
			&af9015_mt2060_config,
			af9015_config.mt2060_if1[adap->id])
			== NULL ? -ENODEV : 0;
		break;
	case AF9013_TUNER_QT1010:
	case AF9013_TUNER_QT1010A:
		ret = dvb_attach(qt1010_attach, adap->fe, i2c_adap,
			&af9015_qt1010_config) == NULL ? -ENODEV : 0;
		break;
	case AF9013_TUNER_TDA18271:
		ret = dvb_attach(tda18271_attach, adap->fe, 0xc0, i2c_adap,
			&af9015_tda18271_config) == NULL ? -ENODEV : 0;
		break;
	case AF9013_TUNER_TDA18218:
		ret = dvb_attach(tda18218_attach, adap->fe, i2c_adap,
			&af9015_tda18218_config) == NULL ? -ENODEV : 0;
		break;
	case AF9013_TUNER_MXL5003D:
		ret = dvb_attach(mxl5005s_attach, adap->fe, i2c_adap,
			&af9015_mxl5003_config) == NULL ? -ENODEV : 0;
		break;
	case AF9013_TUNER_MXL5005D:
	case AF9013_TUNER_MXL5005R:
		ret = dvb_attach(mxl5005s_attach, adap->fe, i2c_adap,
			&af9015_mxl5005_config) == NULL ? -ENODEV : 0;
		break;
	case AF9013_TUNER_ENV77H11D5:
		ret = dvb_attach(dvb_pll_attach, adap->fe, 0xc0, i2c_adap,
			DVB_PLL_TDA665X) == NULL ? -ENODEV : 0;
		break;
	case AF9013_TUNER_MC44S803:
		ret = dvb_attach(mc44s803_attach, adap->fe, i2c_adap,
			&af9015_mc44s803_config) == NULL ? -ENODEV : 0;
		break;
	case AF9013_TUNER_MXL5007T:
		ret = dvb_attach(mxl5007t_attach, adap->fe, i2c_adap,
			0xc0, &af9015_mxl5007t_config) == NULL ? -ENODEV : 0;
		break;
	case AF9013_TUNER_UNKNOWN:
	default:
		ret = -ENODEV;
		err("Unknown tuner id:%d",
			af9015_af9013_config[adap->id].tuner);
	}
	return ret;
}

static struct usb_device_id af9015_usb_table[] = {
/*  0 */{USB_DEVICE(USB_VID_AFATECH,   USB_PID_AFATECH_AF9015_9015)},
	{USB_DEVICE(USB_VID_AFATECH,   USB_PID_AFATECH_AF9015_9016)},
	{USB_DEVICE(USB_VID_LEADTEK,   USB_PID_WINFAST_DTV_DONGLE_GOLD)},
	{USB_DEVICE(USB_VID_PINNACLE,  USB_PID_PINNACLE_PCTV71E)},
	{USB_DEVICE(USB_VID_KWORLD_2,  USB_PID_KWORLD_399U)},
/*  5 */{USB_DEVICE(USB_VID_VISIONPLUS,
		USB_PID_TINYTWIN)},
	{USB_DEVICE(USB_VID_VISIONPLUS,
		USB_PID_AZUREWAVE_AD_TU700)},
	{USB_DEVICE(USB_VID_TERRATEC,  USB_PID_TERRATEC_CINERGY_T_USB_XE_REV2)},
	{USB_DEVICE(USB_VID_KWORLD_2,  USB_PID_KWORLD_PC160_2T)},
	{USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_VOLAR_X)},
/* 10 */{USB_DEVICE(USB_VID_XTENSIONS, USB_PID_XTENSIONS_XD_380)},
	{USB_DEVICE(USB_VID_MSI_2,     USB_PID_MSI_DIGIVOX_DUO)},
	{USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_VOLAR_X_2)},
	{USB_DEVICE(USB_VID_TELESTAR,  USB_PID_TELESTAR_STARSTICK_2)},
	{USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_A309)},
/* 15 */{USB_DEVICE(USB_VID_MSI_2,     USB_PID_MSI_DIGI_VOX_MINI_III)},
	{USB_DEVICE(USB_VID_KWORLD_2,  USB_PID_KWORLD_395U)},
	{USB_DEVICE(USB_VID_KWORLD_2,  USB_PID_KWORLD_395U_2)},
	{USB_DEVICE(USB_VID_KWORLD_2,  USB_PID_KWORLD_395U_3)},
	{USB_DEVICE(USB_VID_AFATECH,   USB_PID_TREKSTOR_DVBT)},
/* 20 */{USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_A850)},
	{USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_A805)},
	{USB_DEVICE(USB_VID_KWORLD_2,  USB_PID_CONCEPTRONIC_CTVDIGRCU)},
	{USB_DEVICE(USB_VID_KWORLD_2,  USB_PID_KWORLD_MC810)},
	{USB_DEVICE(USB_VID_KYE,       USB_PID_GENIUS_TVGO_DVB_T03)},
/* 25 */{USB_DEVICE(USB_VID_KWORLD_2,  USB_PID_KWORLD_399U_2)},
	{USB_DEVICE(USB_VID_KWORLD_2,  USB_PID_KWORLD_PC160_T)},
	{USB_DEVICE(USB_VID_KWORLD_2,  USB_PID_SVEON_STV20)},
	{USB_DEVICE(USB_VID_KWORLD_2,  USB_PID_TINYTWIN_2)},
	{USB_DEVICE(USB_VID_LEADTEK,   USB_PID_WINFAST_DTV2000DS)},
/* 30 */{USB_DEVICE(USB_VID_KWORLD_2,  USB_PID_KWORLD_UB383_T)},
	{USB_DEVICE(USB_VID_KWORLD_2,  USB_PID_KWORLD_395U_4)},
	{USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_A815M)},
	{USB_DEVICE(USB_VID_TERRATEC,  USB_PID_TERRATEC_CINERGY_T_STICK_RC)},
	{USB_DEVICE(USB_VID_TERRATEC,
		USB_PID_TERRATEC_CINERGY_T_STICK_DUAL_RC)},
/* 35 */{USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_A850T)},
	{USB_DEVICE(USB_VID_GTEK,      USB_PID_TINYTWIN_3)},
	{0},
};
MODULE_DEVICE_TABLE(usb, af9015_usb_table);

#define AF9015_RC_INTERVAL 500
static struct dvb_usb_device_properties af9015_properties[] = {
	{
		.caps = DVB_USB_IS_AN_I2C_ADAPTER,

		.usb_ctrl = DEVICE_SPECIFIC,
		.download_firmware = af9015_download_firmware,
		.firmware = "dvb-usb-af9015.fw",
		.no_reconnect = 1,

		.size_of_priv = sizeof(struct af9015_state),

		.num_adapters = 2,
		.adapter = {
			{
				.caps = DVB_USB_ADAP_HAS_PID_FILTER |
				DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,

				.pid_filter_count = 32,
				.pid_filter       = af9015_pid_filter,
				.pid_filter_ctrl  = af9015_pid_filter_ctrl,

				.frontend_attach =
					af9015_af9013_frontend_attach,
				.tuner_attach    = af9015_tuner_attach,
				.stream = {
					.type = USB_BULK,
					.count = 6,
					.endpoint = 0x84,
				},
			},
			{
				.frontend_attach =
					af9015_af9013_frontend_attach,
				.tuner_attach    = af9015_tuner_attach,
				.stream = {
					.type = USB_BULK,
					.count = 6,
					.endpoint = 0x85,
					.u = {
						.bulk = {
							.buffersize =
						TS_USB20_FRAME_SIZE,
						}
					}
				},
			}
		},

		.identify_state = af9015_identify_state,

		.rc.core = {
			.protocol         = IR_TYPE_NEC,
			.module_name      = "af9015",
			.rc_query         = af9015_rc_query,
			.rc_interval      = AF9015_RC_INTERVAL,
			.rc_props = {
				.allowed_protos = IR_TYPE_NEC,
			},
		},

		.i2c_algo = &af9015_i2c_algo,

		.num_device_descs = 12, /* check max from dvb-usb.h */
		.devices = {
			{
				.name = "Afatech AF9015 DVB-T USB2.0 stick",
				.cold_ids = {&af9015_usb_table[0],
					     &af9015_usb_table[1], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "Leadtek WinFast DTV Dongle Gold",
				.cold_ids = {&af9015_usb_table[2], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "Pinnacle PCTV 71e",
				.cold_ids = {&af9015_usb_table[3], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "KWorld PlusTV Dual DVB-T Stick " \
					"(DVB-T 399U)",
				.cold_ids = {&af9015_usb_table[4],
					     &af9015_usb_table[25], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "DigitalNow TinyTwin DVB-T Receiver",
				.cold_ids = {&af9015_usb_table[5],
					     &af9015_usb_table[28],
					     &af9015_usb_table[36], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "TwinHan AzureWave AD-TU700(704J)",
				.cold_ids = {&af9015_usb_table[6], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "TerraTec Cinergy T USB XE",
				.cold_ids = {&af9015_usb_table[7], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "KWorld PlusTV Dual DVB-T PCI " \
					"(DVB-T PC160-2T)",
				.cold_ids = {&af9015_usb_table[8], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "AVerMedia AVerTV DVB-T Volar X",
				.cold_ids = {&af9015_usb_table[9], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "TerraTec Cinergy T Stick RC",
				.cold_ids = {&af9015_usb_table[33], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "TerraTec Cinergy T Stick Dual RC",
				.cold_ids = {&af9015_usb_table[34], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "AverMedia AVerTV Red HD+ (A850T)",
				.cold_ids = {&af9015_usb_table[35], NULL},
				.warm_ids = {NULL},
			},
		}
	}, {
		.caps = DVB_USB_IS_AN_I2C_ADAPTER,

		.usb_ctrl = DEVICE_SPECIFIC,
		.download_firmware = af9015_download_firmware,
		.firmware = "dvb-usb-af9015.fw",
		.no_reconnect = 1,

		.size_of_priv = sizeof(struct af9015_state),

		.num_adapters = 2,
		.adapter = {
			{
				.caps = DVB_USB_ADAP_HAS_PID_FILTER |
				DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,

				.pid_filter_count = 32,
				.pid_filter       = af9015_pid_filter,
				.pid_filter_ctrl  = af9015_pid_filter_ctrl,

				.frontend_attach =
					af9015_af9013_frontend_attach,
				.tuner_attach    = af9015_tuner_attach,
				.stream = {
					.type = USB_BULK,
					.count = 6,
					.endpoint = 0x84,
				},
			},
			{
				.frontend_attach =
					af9015_af9013_frontend_attach,
				.tuner_attach    = af9015_tuner_attach,
				.stream = {
					.type = USB_BULK,
					.count = 6,
					.endpoint = 0x85,
					.u = {
						.bulk = {
							.buffersize =
						TS_USB20_FRAME_SIZE,
						}
					}
				},
			}
		},

		.identify_state = af9015_identify_state,

		.rc.core = {
			.protocol         = IR_TYPE_NEC,
			.module_name      = "af9015",
			.rc_query         = af9015_rc_query,
			.rc_interval      = AF9015_RC_INTERVAL,
			.rc_props = {
				.allowed_protos = IR_TYPE_NEC,
			},
		},

		.i2c_algo = &af9015_i2c_algo,

		.num_device_descs = 9, /* check max from dvb-usb.h */
		.devices = {
			{
				.name = "Xtensions XD-380",
				.cold_ids = {&af9015_usb_table[10], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "MSI DIGIVOX Duo",
				.cold_ids = {&af9015_usb_table[11], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "Fujitsu-Siemens Slim Mobile USB DVB-T",
				.cold_ids = {&af9015_usb_table[12], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "Telestar Starstick 2",
				.cold_ids = {&af9015_usb_table[13], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "AVerMedia A309",
				.cold_ids = {&af9015_usb_table[14], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "MSI Digi VOX mini III",
				.cold_ids = {&af9015_usb_table[15], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "KWorld USB DVB-T TV Stick II " \
					"(VS-DVB-T 395U)",
				.cold_ids = {&af9015_usb_table[16],
					     &af9015_usb_table[17],
					     &af9015_usb_table[18],
					     &af9015_usb_table[31], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "TrekStor DVB-T USB Stick",
				.cold_ids = {&af9015_usb_table[19], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "AverMedia AVerTV Volar Black HD " \
					"(A850)",
				.cold_ids = {&af9015_usb_table[20], NULL},
				.warm_ids = {NULL},
			},
		}
	}, {
		.caps = DVB_USB_IS_AN_I2C_ADAPTER,

		.usb_ctrl = DEVICE_SPECIFIC,
		.download_firmware = af9015_download_firmware,
		.firmware = "dvb-usb-af9015.fw",
		.no_reconnect = 1,

		.size_of_priv = sizeof(struct af9015_state),

		.num_adapters = 2,
		.adapter = {
			{
				.caps = DVB_USB_ADAP_HAS_PID_FILTER |
				DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,

				.pid_filter_count = 32,
				.pid_filter       = af9015_pid_filter,
				.pid_filter_ctrl  = af9015_pid_filter_ctrl,

				.frontend_attach =
					af9015_af9013_frontend_attach,
				.tuner_attach    = af9015_tuner_attach,
				.stream = {
					.type = USB_BULK,
					.count = 6,
					.endpoint = 0x84,
				},
			},
			{
				.frontend_attach =
					af9015_af9013_frontend_attach,
				.tuner_attach    = af9015_tuner_attach,
				.stream = {
					.type = USB_BULK,
					.count = 6,
					.endpoint = 0x85,
					.u = {
						.bulk = {
							.buffersize =
						TS_USB20_FRAME_SIZE,
						}
					}
				},
			}
		},

		.identify_state = af9015_identify_state,

		.rc.core = {
			.protocol         = IR_TYPE_NEC,
			.module_name      = "af9015",
			.rc_query         = af9015_rc_query,
			.rc_interval      = AF9015_RC_INTERVAL,
			.rc_props = {
				.allowed_protos = IR_TYPE_NEC,
			},
		},

		.i2c_algo = &af9015_i2c_algo,

		.num_device_descs = 9, /* check max from dvb-usb.h */
		.devices = {
			{
				.name = "AverMedia AVerTV Volar GPS 805 (A805)",
				.cold_ids = {&af9015_usb_table[21], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "Conceptronic USB2.0 DVB-T CTVDIGRCU " \
					"V3.0",
				.cold_ids = {&af9015_usb_table[22], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "KWorld Digial MC-810",
				.cold_ids = {&af9015_usb_table[23], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "Genius TVGo DVB-T03",
				.cold_ids = {&af9015_usb_table[24], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "KWorld PlusTV DVB-T PCI Pro Card " \
					"(DVB-T PC160-T)",
				.cold_ids = {&af9015_usb_table[26], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "Sveon STV20 Tuner USB DVB-T HDTV",
				.cold_ids = {&af9015_usb_table[27], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "Leadtek WinFast DTV2000DS",
				.cold_ids = {&af9015_usb_table[29], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "KWorld USB DVB-T Stick Mobile " \
					"(UB383-T)",
				.cold_ids = {&af9015_usb_table[30], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "AverMedia AVerTV Volar M (A815Mac)",
				.cold_ids = {&af9015_usb_table[32], NULL},
				.warm_ids = {NULL},
			},
		}
	},
};

static int af9015_usb_probe(struct usb_interface *intf,
			    const struct usb_device_id *id)
{
	int ret = 0;
	struct dvb_usb_device *d = NULL;
	struct usb_device *udev = interface_to_usbdev(intf);
	u8 i;

	deb_info("%s: interface:%d\n", __func__,
		intf->cur_altsetting->desc.bInterfaceNumber);

	/* interface 0 is used by DVB-T receiver and
	   interface 1 is for remote controller (HID) */
	if (intf->cur_altsetting->desc.bInterfaceNumber == 0) {
		ret = af9015_read_config(udev);
		if (ret)
			return ret;

		for (i = 0; i < af9015_properties_count; i++) {
			ret = dvb_usb_device_init(intf, &af9015_properties[i],
				THIS_MODULE, &d, adapter_nr);
			if (!ret)
				break;
			if (ret != -ENODEV)
				return ret;
		}
		if (ret)
			return ret;

		if (d)
			ret = af9015_init(d);
	}

	return ret;
}

static void af9015_i2c_exit(struct dvb_usb_device *d)
{
	struct af9015_state *state = d->priv;
	deb_info("%s:\n", __func__);

	/* remove 2nd I2C adapter */
	if (d->state & DVB_USB_STATE_I2C)
		i2c_del_adapter(&state->i2c_adap);
}

static void af9015_usb_device_exit(struct usb_interface *intf)
{
	struct dvb_usb_device *d = usb_get_intfdata(intf);
	deb_info("%s:\n", __func__);

	/* remove 2nd I2C adapter */
	if (d != NULL && d->desc != NULL)
		af9015_i2c_exit(d);

	dvb_usb_device_exit(intf);
}

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver af9015_usb_driver = {
	.name = "dvb_usb_af9015",
	.probe = af9015_usb_probe,
	.disconnect = af9015_usb_device_exit,
	.id_table = af9015_usb_table,
};

/* module stuff */
static int __init af9015_usb_module_init(void)
{
	int ret;
	ret = usb_register(&af9015_usb_driver);
	if (ret)
		err("module init failed:%d", ret);

	return ret;
}

static void __exit af9015_usb_module_exit(void)
{
	/* deregister this driver from the USB subsystem */
	usb_deregister(&af9015_usb_driver);
}

module_init(af9015_usb_module_init);
module_exit(af9015_usb_module_exit);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Driver for Afatech AF9015 DVB-T");
MODULE_LICENSE("GPL");
