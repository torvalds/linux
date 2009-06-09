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
#include "af9013.h"
#include "mt2060.h"
#include "qt1010.h"
#include "tda18271.h"
#include "mxl5005s.h"
#include "mc44s803.h"

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
	int act_len, ret;
	u8 buf[64];
	u8 write = 1;
	u8 msg_len = 8;
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
	case BOOT:
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
		    ((req->addr & 0xae00) == 0xae00))
			buf[0] = WRITE_VIRTUAL_MEMORY;
	case WRITE_VIRTUAL_MEMORY:
	case COPY_FIRMWARE:
	case DOWNLOAD_FIRMWARE:
		break;
	default:
		err("unknown command:%d", req->cmd);
		ret = -1;
		goto error_unlock;
	}

	/* write requested */
	if (write) {
		memcpy(&buf[8], req->data, req->data_len);
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

	/* receive ack and data if read req */
	msg_len = 1 + 1 + req->data_len;  /* seq + status + data len */
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
		memcpy(req->data, &buf[2], req->data_len);

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

static int af9015_read_reg(struct dvb_usb_device *d, u16 addr, u8 *val)
{
	struct req_t req = {READ_MEMORY, AF9015_I2C_DEMOD, addr, 0, 0, 1, val};
	return af9015_ctrl_msg(d, &req);
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
	u8 mbox, addr_len;
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
			mbox = 0;
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

#define TS_PACKET_SIZE            188

#define TS_USB20_PACKET_COUNT     348
#define TS_USB20_FRAME_SIZE       (TS_PACKET_SIZE*TS_USB20_PACKET_COUNT)

#define TS_USB11_PACKET_COUNT      21
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

	ret = af9015_read_reg_i2c(d, 0x3a, 0x98be, &val);
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

/* dump eeprom */
static int af9015_eeprom_dump(struct dvb_usb_device *d)
{
	char buf[4+3*16+1], buf2[4];
	u8 reg, val;

	for (reg = 0; ; reg++) {
		if (reg % 16 == 0) {
			if (reg)
				deb_info("%s\n", buf);
			sprintf(buf, "%02x: ", reg);
		}
		if (af9015_read_reg_i2c(d, AF9015_I2C_EEPROM, reg, &val) == 0)
			sprintf(buf2, "%02x ", val);
		else
			strcpy(buf2, "-- ");
		strcat(buf, buf2);
		if (reg == 0xff)
			break;
	}
	deb_info("%s\n", buf);
	return 0;
}

static int af9015_download_ir_table(struct dvb_usb_device *d)
{
	int i, packets = 0, ret;
	u16 addr = 0x9a56; /* ir-table start address */
	struct req_t req = {WRITE_MEMORY, 0, 0, 0, 0, 1, NULL};
	u8 *data = NULL;
	deb_info("%s:\n", __func__);

	data = af9015_config.ir_table;
	packets = af9015_config.ir_table_size;

	/* no remote */
	if (!packets)
		goto exit;

	/* load remote ir-table */
	for (i = 0; i < packets; i++) {
		req.addr = addr + i;
		req.data = &data[i];
		ret = af9015_ctrl_msg(d, &req);
		if (ret) {
			err("ir-table download failed at packet %d with " \
				"code %d", i, ret);
			return ret;
		}
	}

exit:
	return 0;
}

static int af9015_init(struct dvb_usb_device *d)
{
	int ret;
	deb_info("%s:\n", __func__);

	ret = af9015_init_endpoint(d);
	if (ret)
		goto error;

	ret = af9015_download_ir_table(d);
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

static int af9015_read_config(struct usb_device *udev)
{
	int ret;
	u8 val, i, offset = 0;
	struct req_t req = {READ_I2C, AF9015_I2C_EEPROM, 0, 0, 1, 1, &val};
	char manufacturer[10];

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
	deb_info("%s: IR mode:%d\n", __func__, val);
	for (i = 0; i < af9015_properties_count; i++) {
		if (val == AF9015_IR_MODE_DISABLED || val == 0x04) {
			af9015_properties[i].rc_key_map = NULL;
			af9015_properties[i].rc_key_map_size  = 0;
		} else if (dvb_usb_af9015_remote) {
			/* load remote defined as module param */
			switch (dvb_usb_af9015_remote) {
			case AF9015_REMOTE_A_LINK_DTU_M:
				af9015_properties[i].rc_key_map =
				  af9015_rc_keys_a_link;
				af9015_properties[i].rc_key_map_size =
				  ARRAY_SIZE(af9015_rc_keys_a_link);
				af9015_config.ir_table = af9015_ir_table_a_link;
				af9015_config.ir_table_size =
				  ARRAY_SIZE(af9015_ir_table_a_link);
				break;
			case AF9015_REMOTE_MSI_DIGIVOX_MINI_II_V3:
				af9015_properties[i].rc_key_map =
				  af9015_rc_keys_msi;
				af9015_properties[i].rc_key_map_size =
				  ARRAY_SIZE(af9015_rc_keys_msi);
				af9015_config.ir_table = af9015_ir_table_msi;
				af9015_config.ir_table_size =
				  ARRAY_SIZE(af9015_ir_table_msi);
				break;
			case AF9015_REMOTE_MYGICTV_U718:
				af9015_properties[i].rc_key_map =
				  af9015_rc_keys_mygictv;
				af9015_properties[i].rc_key_map_size =
				  ARRAY_SIZE(af9015_rc_keys_mygictv);
				af9015_config.ir_table =
				  af9015_ir_table_mygictv;
				af9015_config.ir_table_size =
				  ARRAY_SIZE(af9015_ir_table_mygictv);
				break;
			case AF9015_REMOTE_DIGITTRADE_DVB_T:
				af9015_properties[i].rc_key_map =
				  af9015_rc_keys_digittrade;
				af9015_properties[i].rc_key_map_size =
				  ARRAY_SIZE(af9015_rc_keys_digittrade);
				af9015_config.ir_table =
				  af9015_ir_table_digittrade;
				af9015_config.ir_table_size =
				  ARRAY_SIZE(af9015_ir_table_digittrade);
				break;
			case AF9015_REMOTE_AVERMEDIA_KS:
				af9015_properties[i].rc_key_map =
				  af9015_rc_keys_avermedia;
				af9015_properties[i].rc_key_map_size =
				  ARRAY_SIZE(af9015_rc_keys_avermedia);
				af9015_config.ir_table =
				  af9015_ir_table_avermedia_ks;
				af9015_config.ir_table_size =
				  ARRAY_SIZE(af9015_ir_table_avermedia_ks);
				break;
			}
		} else {
			switch (le16_to_cpu(udev->descriptor.idVendor)) {
			case USB_VID_LEADTEK:
				af9015_properties[i].rc_key_map =
				  af9015_rc_keys_leadtek;
				af9015_properties[i].rc_key_map_size =
				  ARRAY_SIZE(af9015_rc_keys_leadtek);
				af9015_config.ir_table =
				  af9015_ir_table_leadtek;
				af9015_config.ir_table_size =
				  ARRAY_SIZE(af9015_ir_table_leadtek);
				break;
			case USB_VID_VISIONPLUS:
				af9015_properties[i].rc_key_map =
				  af9015_rc_keys_twinhan;
				af9015_properties[i].rc_key_map_size =
				  ARRAY_SIZE(af9015_rc_keys_twinhan);
				af9015_config.ir_table =
				  af9015_ir_table_twinhan;
				af9015_config.ir_table_size =
				  ARRAY_SIZE(af9015_ir_table_twinhan);
				break;
			case USB_VID_KWORLD_2:
				/* TODO: use correct rc keys */
				af9015_properties[i].rc_key_map =
				  af9015_rc_keys_twinhan;
				af9015_properties[i].rc_key_map_size =
				  ARRAY_SIZE(af9015_rc_keys_twinhan);
				af9015_config.ir_table = af9015_ir_table_kworld;
				af9015_config.ir_table_size =
				  ARRAY_SIZE(af9015_ir_table_kworld);
				break;
			/* Check USB manufacturer and product strings and try
			   to determine correct remote in case of chip vendor
			   reference IDs are used. */
			case USB_VID_AFATECH:
				memset(manufacturer, 0, sizeof(manufacturer));
				usb_string(udev, udev->descriptor.iManufacturer,
					manufacturer, sizeof(manufacturer));
				if (!strcmp("Geniatech", manufacturer)) {
					/* iManufacturer 1 Geniatech
					   iProduct      2 AF9015 */
					af9015_properties[i].rc_key_map =
					  af9015_rc_keys_mygictv;
					af9015_properties[i].rc_key_map_size =
					  ARRAY_SIZE(af9015_rc_keys_mygictv);
					af9015_config.ir_table =
					  af9015_ir_table_mygictv;
					af9015_config.ir_table_size =
					  ARRAY_SIZE(af9015_ir_table_mygictv);
				} else if (!strcmp("MSI", manufacturer)) {
					/* iManufacturer 1 MSI
					   iProduct      2 MSI K-VOX */
					af9015_properties[i].rc_key_map =
					  af9015_rc_keys_msi;
					af9015_properties[i].rc_key_map_size =
					  ARRAY_SIZE(af9015_rc_keys_msi);
					af9015_config.ir_table =
					  af9015_ir_table_msi;
					af9015_config.ir_table_size =
					  ARRAY_SIZE(af9015_ir_table_msi);
				} else if (udev->descriptor.idProduct ==
					cpu_to_le16(USB_PID_TREKSTOR_DVBT)) {
					af9015_properties[i].rc_key_map =
					  af9015_rc_keys_trekstor;
					af9015_properties[i].rc_key_map_size =
					  ARRAY_SIZE(af9015_rc_keys_trekstor);
					af9015_config.ir_table =
					  af9015_ir_table_trekstor;
					af9015_config.ir_table_size =
					  ARRAY_SIZE(af9015_ir_table_trekstor);
				}
				break;
			case USB_VID_AVERMEDIA:
				af9015_properties[i].rc_key_map =
				  af9015_rc_keys_avermedia;
				af9015_properties[i].rc_key_map_size =
				  ARRAY_SIZE(af9015_rc_keys_avermedia);
				af9015_config.ir_table =
				  af9015_ir_table_avermedia;
				af9015_config.ir_table_size =
				  ARRAY_SIZE(af9015_ir_table_avermedia);
				break;
			}
		}
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
				= TS_USB11_MAX_PACKET_SIZE;
			/* disable 2nd adapter because we don't have
			   PID-filters */
			af9015_config.dual_mode = 0;
		} else {
			af9015_properties[i].adapter[0].stream.u.bulk.buffersize
				= TS_USB20_MAX_PACKET_SIZE;
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
			af9015_af9013_config[i].rf_spec_inv = 1;
			break;
		case AF9013_TUNER_MXL5003D:
		case AF9013_TUNER_MXL5005D:
		case AF9013_TUNER_MXL5005R:
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
	   content :-( Override some wrong values here. */
	if (le16_to_cpu(udev->descriptor.idVendor) == USB_VID_AVERMEDIA &&
	    le16_to_cpu(udev->descriptor.idProduct) == USB_PID_AVERMEDIA_A850) {
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

static int af9015_rc_query(struct dvb_usb_device *d, u32 *event, int *state)
{
	u8 buf[8];
	struct req_t req = {GET_IR_CODE, 0, 0, 0, 0, sizeof(buf), buf};
	struct dvb_usb_rc_key *keymap = d->props.rc_key_map;
	int i, ret;

	memset(buf, 0, sizeof(buf));

	ret = af9015_ctrl_msg(d, &req);
	if (ret)
		return ret;

	*event = 0;
	*state = REMOTE_NO_KEY_PRESSED;

	for (i = 0; i < d->props.rc_key_map_size; i++) {
		if (!buf[1] && keymap[i].custom == buf[0] &&
		    keymap[i].data == buf[2]) {
			*event = keymap[i].event;
			*state = REMOTE_KEY_PRESSED;
			break;
		}
	}
	if (!buf[1])
		deb_rc("%s: %02x %02x %02x %02x %02x %02x %02x %02x\n",
			__func__, buf[0], buf[1], buf[2], buf[3], buf[4],
			buf[5], buf[6], buf[7]);

	return 0;
}

/* init 2nd I2C adapter */
static int af9015_i2c_init(struct dvb_usb_device *d)
{
	int ret;
	struct af9015_state *state = d->priv;
	deb_info("%s:\n", __func__);

	strncpy(state->i2c_adap.name, d->desc->name,
		sizeof(state->i2c_adap.name));
#ifdef I2C_ADAP_CLASS_TV_DIGITAL
	state->i2c_adap.class = I2C_ADAP_CLASS_TV_DIGITAL,
#else
	state->i2c_adap.class = I2C_CLASS_TV_DIGITAL,
#endif
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

		/* dump eeprom (debug) */
		ret = af9015_eeprom_dump(adap->dev);
		if (ret)
			return ret;
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
	.small_i2c = 1,
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

static int af9015_tuner_attach(struct dvb_usb_adapter *adap)
{
	struct af9015_state *state = adap->dev->priv;
	struct i2c_adapter *i2c_adap;
	int ret;
	deb_info("%s: \n", __func__);

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
	{0},
};
MODULE_DEVICE_TABLE(usb, af9015_usb_table);

static struct dvb_usb_device_properties af9015_properties[] = {
	{
		.caps = DVB_USB_IS_AN_I2C_ADAPTER,

		.usb_ctrl = DEVICE_SPECIFIC,
		.download_firmware = af9015_download_firmware,
		.firmware = "dvb-usb-af9015.fw",
		.no_reconnect = 1,

		.size_of_priv = sizeof(struct af9015_state), \

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
						TS_USB20_MAX_PACKET_SIZE,
						}
					}
				},
			}
		},

		.identify_state = af9015_identify_state,

		.rc_query         = af9015_rc_query,
		.rc_interval      = 150,

		.i2c_algo = &af9015_i2c_algo,

		.num_device_descs = 9, /* max 9 */
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
				.cold_ids = {&af9015_usb_table[4], NULL},
				.warm_ids = {NULL},
			},
			{
				.name = "DigitalNow TinyTwin DVB-T Receiver",
				.cold_ids = {&af9015_usb_table[5], NULL},
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
		}
	}, {
		.caps = DVB_USB_IS_AN_I2C_ADAPTER,

		.usb_ctrl = DEVICE_SPECIFIC,
		.download_firmware = af9015_download_firmware,
		.firmware = "dvb-usb-af9015.fw",
		.no_reconnect = 1,

		.size_of_priv = sizeof(struct af9015_state), \

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
						TS_USB20_MAX_PACKET_SIZE,
						}
					}
				},
			}
		},

		.identify_state = af9015_identify_state,

		.rc_query         = af9015_rc_query,
		.rc_interval      = 150,

		.i2c_algo = &af9015_i2c_algo,

		.num_device_descs = 9, /* max 9 */
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
					     &af9015_usb_table[18], NULL},
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

		.size_of_priv = sizeof(struct af9015_state), \

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
						TS_USB20_MAX_PACKET_SIZE,
						}
					}
				},
			}
		},

		.identify_state = af9015_identify_state,

		.rc_query         = af9015_rc_query,
		.rc_interval      = 150,

		.i2c_algo = &af9015_i2c_algo,

		.num_device_descs = 4, /* max 9 */
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
	deb_info("%s: \n", __func__);

	/* remove 2nd I2C adapter */
	if (d->state & DVB_USB_STATE_I2C)
		i2c_del_adapter(&state->i2c_adap);
}

static void af9015_usb_device_exit(struct usb_interface *intf)
{
	struct dvb_usb_device *d = usb_get_intfdata(intf);
	deb_info("%s: \n", __func__);

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
