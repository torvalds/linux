// SPDX-License-Identifier: GPL-2.0-or-later
/* DVB USB compliant Linux driver for the Afatech 9005
 * USB1.1 DVB-T receiver.
 *
 * Copyright (C) 2007 Luca Olivetti (luca@ventoso.org)
 *
 * Thanks to Afatech who kindly provided information.
 *
 * see Documentation/driver-api/media/drivers/dvb-usb.rst for more information
 */
#include "af9005.h"

/* debug */
int dvb_usb_af9005_debug;
module_param_named(debug, dvb_usb_af9005_debug, int, 0644);
MODULE_PARM_DESC(debug,
		 "set debugging level (1=info,xfer=2,rc=4,reg=8,i2c=16,fw=32 (or-able))."
		 DVB_USB_DEBUG_STATUS);
/* enable obnoxious led */
bool dvb_usb_af9005_led = true;
module_param_named(led, dvb_usb_af9005_led, bool, 0644);
MODULE_PARM_DESC(led, "enable led (default: 1).");

/* eeprom dump */
static int dvb_usb_af9005_dump_eeprom;
module_param_named(dump_eeprom, dvb_usb_af9005_dump_eeprom, int, 0);
MODULE_PARM_DESC(dump_eeprom, "dump contents of the eeprom.");

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

/* remote control decoder */
static int (*rc_decode) (struct dvb_usb_device *d, u8 *data, int len,
		u32 *event, int *state);
static void *rc_keys;
static int *rc_keys_size;

u8 regmask[8] = { 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff };

struct af9005_device_state {
	u8 sequence;
	int led_state;
	unsigned char data[256];
};

static int af9005_generic_read_write(struct dvb_usb_device *d, u16 reg,
			      int readwrite, int type, u8 * values, int len)
{
	struct af9005_device_state *st = d->priv;
	u8 command, seq;
	int i, ret;

	if (len < 1) {
		err("generic read/write, less than 1 byte. Makes no sense.");
		return -EINVAL;
	}
	if (len > 8) {
		err("generic read/write, more than 8 bytes. Not supported.");
		return -EINVAL;
	}

	mutex_lock(&d->data_mutex);
	st->data[0] = 14;		/* rest of buffer length low */
	st->data[1] = 0;		/* rest of buffer length high */

	st->data[2] = AF9005_REGISTER_RW;	/* register operation */
	st->data[3] = 12;		/* rest of buffer length */

	st->data[4] = seq = st->sequence++;	/* sequence number */

	st->data[5] = (u8) (reg >> 8);	/* register address */
	st->data[6] = (u8) (reg & 0xff);

	if (type == AF9005_OFDM_REG) {
		command = AF9005_CMD_OFDM_REG;
	} else {
		command = AF9005_CMD_TUNER;
	}

	if (len > 1)
		command |=
		    AF9005_CMD_BURST | AF9005_CMD_AUTOINC | (len - 1) << 3;
	command |= readwrite;
	if (readwrite == AF9005_CMD_WRITE)
		for (i = 0; i < len; i++)
			st->data[8 + i] = values[i];
	else if (type == AF9005_TUNER_REG)
		/* read command for tuner, the first byte contains the i2c address */
		st->data[8] = values[0];
	st->data[7] = command;

	ret = dvb_usb_generic_rw(d, st->data, 16, st->data, 17, 0);
	if (ret)
		goto ret;

	/* sanity check */
	if (st->data[2] != AF9005_REGISTER_RW_ACK) {
		err("generic read/write, wrong reply code.");
		ret = -EIO;
		goto ret;
	}
	if (st->data[3] != 0x0d) {
		err("generic read/write, wrong length in reply.");
		ret = -EIO;
		goto ret;
	}
	if (st->data[4] != seq) {
		err("generic read/write, wrong sequence in reply.");
		ret = -EIO;
		goto ret;
	}
	/*
	 * In thesis, both input and output buffers should have
	 * identical values for st->data[5] to st->data[8].
	 * However, windows driver doesn't check these fields, in fact
	 * sometimes the register in the reply is different that what
	 * has been sent
	 */
	if (st->data[16] != 0x01) {
		err("generic read/write wrong status code in reply.");
		ret = -EIO;
		goto ret;
	}

	if (readwrite == AF9005_CMD_READ)
		for (i = 0; i < len; i++)
			values[i] = st->data[8 + i];

ret:
	mutex_unlock(&d->data_mutex);
	return ret;

}

int af9005_read_ofdm_register(struct dvb_usb_device *d, u16 reg, u8 * value)
{
	int ret;
	deb_reg("read register %x ", reg);
	ret = af9005_generic_read_write(d, reg,
					AF9005_CMD_READ, AF9005_OFDM_REG,
					value, 1);
	if (ret)
		deb_reg("failed\n");
	else
		deb_reg("value %x\n", *value);
	return ret;
}

int af9005_read_ofdm_registers(struct dvb_usb_device *d, u16 reg,
			       u8 * values, int len)
{
	int ret;
	deb_reg("read %d registers %x ", len, reg);
	ret = af9005_generic_read_write(d, reg,
					AF9005_CMD_READ, AF9005_OFDM_REG,
					values, len);
	if (ret)
		deb_reg("failed\n");
	else
		debug_dump(values, len, deb_reg);
	return ret;
}

int af9005_write_ofdm_register(struct dvb_usb_device *d, u16 reg, u8 value)
{
	int ret;
	u8 temp = value;
	deb_reg("write register %x value %x ", reg, value);
	ret = af9005_generic_read_write(d, reg,
					AF9005_CMD_WRITE, AF9005_OFDM_REG,
					&temp, 1);
	if (ret)
		deb_reg("failed\n");
	else
		deb_reg("ok\n");
	return ret;
}

int af9005_write_ofdm_registers(struct dvb_usb_device *d, u16 reg,
				u8 * values, int len)
{
	int ret;
	deb_reg("write %d registers %x values ", len, reg);
	debug_dump(values, len, deb_reg);

	ret = af9005_generic_read_write(d, reg,
					AF9005_CMD_WRITE, AF9005_OFDM_REG,
					values, len);
	if (ret)
		deb_reg("failed\n");
	else
		deb_reg("ok\n");
	return ret;
}

int af9005_read_register_bits(struct dvb_usb_device *d, u16 reg, u8 pos,
			      u8 len, u8 * value)
{
	u8 temp;
	int ret;
	deb_reg("read bits %x %x %x", reg, pos, len);
	ret = af9005_read_ofdm_register(d, reg, &temp);
	if (ret) {
		deb_reg(" failed\n");
		return ret;
	}
	*value = (temp >> pos) & regmask[len - 1];
	deb_reg(" value %x\n", *value);
	return 0;

}

int af9005_write_register_bits(struct dvb_usb_device *d, u16 reg, u8 pos,
			       u8 len, u8 value)
{
	u8 temp, mask;
	int ret;
	deb_reg("write bits %x %x %x value %x\n", reg, pos, len, value);
	if (pos == 0 && len == 8)
		return af9005_write_ofdm_register(d, reg, value);
	ret = af9005_read_ofdm_register(d, reg, &temp);
	if (ret)
		return ret;
	mask = regmask[len - 1] << pos;
	temp = (temp & ~mask) | ((value << pos) & mask);
	return af9005_write_ofdm_register(d, reg, temp);

}

static int af9005_usb_read_tuner_registers(struct dvb_usb_device *d,
					   u16 reg, u8 * values, int len)
{
	return af9005_generic_read_write(d, reg,
					 AF9005_CMD_READ, AF9005_TUNER_REG,
					 values, len);
}

static int af9005_usb_write_tuner_registers(struct dvb_usb_device *d,
					    u16 reg, u8 * values, int len)
{
	return af9005_generic_read_write(d, reg,
					 AF9005_CMD_WRITE,
					 AF9005_TUNER_REG, values, len);
}

int af9005_write_tuner_registers(struct dvb_usb_device *d, u16 reg,
				 u8 * values, int len)
{
	/* don't let the name of this function mislead you: it's just used
	   as an interface from the firmware to the i2c bus. The actual
	   i2c addresses are contained in the data */
	int ret, i, done = 0, fail = 0;
	u8 temp;
	ret = af9005_usb_write_tuner_registers(d, reg, values, len);
	if (ret)
		return ret;
	if (reg != 0xffff) {
		/* check if write done (0xa40d bit 1) or fail (0xa40d bit 2) */
		for (i = 0; i < 200; i++) {
			ret =
			    af9005_read_ofdm_register(d,
						      xd_I2C_i2c_m_status_wdat_done,
						      &temp);
			if (ret)
				return ret;
			done = temp & (regmask[i2c_m_status_wdat_done_len - 1]
				       << i2c_m_status_wdat_done_pos);
			if (done)
				break;
			fail = temp & (regmask[i2c_m_status_wdat_fail_len - 1]
				       << i2c_m_status_wdat_fail_pos);
			if (fail)
				break;
			msleep(50);
		}
		if (i == 200)
			return -ETIMEDOUT;
		if (fail) {
			/* clear write fail bit */
			af9005_write_register_bits(d,
						   xd_I2C_i2c_m_status_wdat_fail,
						   i2c_m_status_wdat_fail_pos,
						   i2c_m_status_wdat_fail_len,
						   1);
			return -EIO;
		}
		/* clear write done bit */
		ret =
		    af9005_write_register_bits(d,
					       xd_I2C_i2c_m_status_wdat_fail,
					       i2c_m_status_wdat_done_pos,
					       i2c_m_status_wdat_done_len, 1);
		if (ret)
			return ret;
	}
	return 0;
}

int af9005_read_tuner_registers(struct dvb_usb_device *d, u16 reg, u8 addr,
				u8 * values, int len)
{
	/* don't let the name of this function mislead you: it's just used
	   as an interface from the firmware to the i2c bus. The actual
	   i2c addresses are contained in the data */
	int ret, i;
	u8 temp, buf[2];

	buf[0] = addr;		/* tuner i2c address */
	buf[1] = values[0];	/* tuner register */

	values[0] = addr + 0x01;	/* i2c read address */

	if (reg == APO_REG_I2C_RW_SILICON_TUNER) {
		/* write tuner i2c address to tuner, 0c00c0 undocumented, found by sniffing */
		ret = af9005_write_tuner_registers(d, 0x00c0, buf, 2);
		if (ret)
			return ret;
	}

	/* send read command to ofsm */
	ret = af9005_usb_read_tuner_registers(d, reg, values, 1);
	if (ret)
		return ret;

	/* check if read done */
	for (i = 0; i < 200; i++) {
		ret = af9005_read_ofdm_register(d, 0xa408, &temp);
		if (ret)
			return ret;
		if (temp & 0x01)
			break;
		msleep(50);
	}
	if (i == 200)
		return -ETIMEDOUT;

	/* clear read done bit (by writing 1) */
	ret = af9005_write_ofdm_register(d, xd_I2C_i2c_m_data8, 1);
	if (ret)
		return ret;

	/* get read data (available from 0xa400) */
	for (i = 0; i < len; i++) {
		ret = af9005_read_ofdm_register(d, 0xa400 + i, &temp);
		if (ret)
			return ret;
		values[i] = temp;
	}
	return 0;
}

static int af9005_i2c_write(struct dvb_usb_device *d, u8 i2caddr, u8 reg,
			    u8 * data, int len)
{
	int ret, i;
	u8 buf[3];
	deb_i2c("i2c_write i2caddr %x, reg %x, len %d data ", i2caddr,
		reg, len);
	debug_dump(data, len, deb_i2c);

	for (i = 0; i < len; i++) {
		buf[0] = i2caddr;
		buf[1] = reg + (u8) i;
		buf[2] = data[i];
		ret =
		    af9005_write_tuner_registers(d,
						 APO_REG_I2C_RW_SILICON_TUNER,
						 buf, 3);
		if (ret) {
			deb_i2c("i2c_write failed\n");
			return ret;
		}
	}
	deb_i2c("i2c_write ok\n");
	return 0;
}

static int af9005_i2c_read(struct dvb_usb_device *d, u8 i2caddr, u8 reg,
			   u8 * data, int len)
{
	int ret, i;
	u8 temp;
	deb_i2c("i2c_read i2caddr %x, reg %x, len %d\n ", i2caddr, reg, len);
	for (i = 0; i < len; i++) {
		temp = reg + i;
		ret =
		    af9005_read_tuner_registers(d,
						APO_REG_I2C_RW_SILICON_TUNER,
						i2caddr, &temp, 1);
		if (ret) {
			deb_i2c("i2c_read failed\n");
			return ret;
		}
		data[i] = temp;
	}
	deb_i2c("i2c data read: ");
	debug_dump(data, len, deb_i2c);
	return 0;
}

static int af9005_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msg[],
			   int num)
{
	/* only implements what the mt2060 module does, don't know how
	   to make it really generic */
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	int ret;
	u8 reg, addr;
	u8 *value;

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	if (num > 2)
		warn("more than 2 i2c messages at a time is not handled yet. TODO.");

	if (num == 2) {
		/* reads a single register */
		reg = *msg[0].buf;
		addr = msg[0].addr;
		value = msg[1].buf;
		ret = af9005_i2c_read(d, addr, reg, value, 1);
		if (ret == 0)
			ret = 2;
	} else {
		/* write one or more registers */
		reg = msg[0].buf[0];
		addr = msg[0].addr;
		value = &msg[0].buf[1];
		ret = af9005_i2c_write(d, addr, reg, value, msg[0].len - 1);
		if (ret == 0)
			ret = 1;
	}

	mutex_unlock(&d->i2c_mutex);
	return ret;
}

static u32 af9005_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm af9005_i2c_algo = {
	.master_xfer = af9005_i2c_xfer,
	.functionality = af9005_i2c_func,
};

int af9005_send_command(struct dvb_usb_device *d, u8 command, u8 * wbuf,
			int wlen, u8 * rbuf, int rlen)
{
	struct af9005_device_state *st = d->priv;

	int ret, i, packet_len;
	u8 seq;

	if (wlen < 0) {
		err("send command, wlen less than 0 bytes. Makes no sense.");
		return -EINVAL;
	}
	if (wlen > 54) {
		err("send command, wlen more than 54 bytes. Not supported.");
		return -EINVAL;
	}
	if (rlen > 54) {
		err("send command, rlen more than 54 bytes. Not supported.");
		return -EINVAL;
	}
	packet_len = wlen + 5;

	mutex_lock(&d->data_mutex);

	st->data[0] = (u8) (packet_len & 0xff);
	st->data[1] = (u8) ((packet_len & 0xff00) >> 8);

	st->data[2] = 0x26;		/* packet type */
	st->data[3] = wlen + 3;
	st->data[4] = seq = st->sequence++;
	st->data[5] = command;
	st->data[6] = wlen;
	for (i = 0; i < wlen; i++)
		st->data[7 + i] = wbuf[i];
	ret = dvb_usb_generic_rw(d, st->data, wlen + 7, st->data, rlen + 7, 0);
	if (st->data[2] != 0x27) {
		err("send command, wrong reply code.");
		ret = -EIO;
	} else if (st->data[4] != seq) {
		err("send command, wrong sequence in reply.");
		ret = -EIO;
	} else if (st->data[5] != 0x01) {
		err("send command, wrong status code in reply.");
		ret = -EIO;
	} else if (st->data[6] != rlen) {
		err("send command, invalid data length in reply.");
		ret = -EIO;
	}
	if (!ret) {
		for (i = 0; i < rlen; i++)
			rbuf[i] = st->data[i + 7];
	}

	mutex_unlock(&d->data_mutex);
	return ret;
}

int af9005_read_eeprom(struct dvb_usb_device *d, u8 address, u8 * values,
		       int len)
{
	struct af9005_device_state *st = d->priv;
	u8 seq;
	int ret, i;

	mutex_lock(&d->data_mutex);

	memset(st->data, 0, sizeof(st->data));

	st->data[0] = 14;		/* length of rest of packet low */
	st->data[1] = 0;		/* length of rest of packer high */

	st->data[2] = 0x2a;		/* read/write eeprom */

	st->data[3] = 12;		/* size */

	st->data[4] = seq = st->sequence++;

	st->data[5] = 0;		/* read */

	st->data[6] = len;
	st->data[7] = address;
	ret = dvb_usb_generic_rw(d, st->data, 16, st->data, 14, 0);
	if (st->data[2] != 0x2b) {
		err("Read eeprom, invalid reply code");
		ret = -EIO;
	} else if (st->data[3] != 10) {
		err("Read eeprom, invalid reply length");
		ret = -EIO;
	} else if (st->data[4] != seq) {
		err("Read eeprom, wrong sequence in reply ");
		ret = -EIO;
	} else if (st->data[5] != 1) {
		err("Read eeprom, wrong status in reply ");
		ret = -EIO;
	}

	if (!ret) {
		for (i = 0; i < len; i++)
			values[i] = st->data[6 + i];
	}
	mutex_unlock(&d->data_mutex);

	return ret;
}

static int af9005_boot_packet(struct usb_device *udev, int type, u8 *reply,
			      u8 *buf, int size)
{
	u16 checksum;
	int act_len = 0, i, ret;

	memset(buf, 0, size);
	buf[0] = (u8) (FW_BULKOUT_SIZE & 0xff);
	buf[1] = (u8) ((FW_BULKOUT_SIZE >> 8) & 0xff);
	switch (type) {
	case FW_CONFIG:
		buf[2] = 0x11;
		buf[3] = 0x04;
		buf[4] = 0x00;	/* sequence number, original driver doesn't increment it here */
		buf[5] = 0x03;
		checksum = buf[4] + buf[5];
		buf[6] = (u8) ((checksum >> 8) & 0xff);
		buf[7] = (u8) (checksum & 0xff);
		break;
	case FW_CONFIRM:
		buf[2] = 0x11;
		buf[3] = 0x04;
		buf[4] = 0x00;	/* sequence number, original driver doesn't increment it here */
		buf[5] = 0x01;
		checksum = buf[4] + buf[5];
		buf[6] = (u8) ((checksum >> 8) & 0xff);
		buf[7] = (u8) (checksum & 0xff);
		break;
	case FW_BOOT:
		buf[2] = 0x10;
		buf[3] = 0x08;
		buf[4] = 0x00;	/* sequence number, original driver doesn't increment it here */
		buf[5] = 0x97;
		buf[6] = 0xaa;
		buf[7] = 0x55;
		buf[8] = 0xa5;
		buf[9] = 0x5a;
		checksum = 0;
		for (i = 4; i <= 9; i++)
			checksum += buf[i];
		buf[10] = (u8) ((checksum >> 8) & 0xff);
		buf[11] = (u8) (checksum & 0xff);
		break;
	default:
		err("boot packet invalid boot packet type");
		return -EINVAL;
	}
	deb_fw(">>> ");
	debug_dump(buf, FW_BULKOUT_SIZE + 2, deb_fw);

	ret = usb_bulk_msg(udev,
			   usb_sndbulkpipe(udev, 0x02),
			   buf, FW_BULKOUT_SIZE + 2, &act_len, 2000);
	if (ret)
		err("boot packet bulk message failed: %d (%d/%d)", ret,
		    FW_BULKOUT_SIZE + 2, act_len);
	else
		ret = act_len != FW_BULKOUT_SIZE + 2 ? -1 : 0;
	if (ret)
		return ret;
	memset(buf, 0, 9);
	ret = usb_bulk_msg(udev,
			   usb_rcvbulkpipe(udev, 0x01), buf, 9, &act_len, 2000);
	if (ret) {
		err("boot packet recv bulk message failed: %d", ret);
		return ret;
	}
	deb_fw("<<< ");
	debug_dump(buf, act_len, deb_fw);
	checksum = 0;
	switch (type) {
	case FW_CONFIG:
		if (buf[2] != 0x11) {
			err("boot bad config header.");
			return -EIO;
		}
		if (buf[3] != 0x05) {
			err("boot bad config size.");
			return -EIO;
		}
		if (buf[4] != 0x00) {
			err("boot bad config sequence.");
			return -EIO;
		}
		if (buf[5] != 0x04) {
			err("boot bad config subtype.");
			return -EIO;
		}
		for (i = 4; i <= 6; i++)
			checksum += buf[i];
		if (buf[7] * 256 + buf[8] != checksum) {
			err("boot bad config checksum.");
			return -EIO;
		}
		*reply = buf[6];
		break;
	case FW_CONFIRM:
		if (buf[2] != 0x11) {
			err("boot bad confirm header.");
			return -EIO;
		}
		if (buf[3] != 0x05) {
			err("boot bad confirm size.");
			return -EIO;
		}
		if (buf[4] != 0x00) {
			err("boot bad confirm sequence.");
			return -EIO;
		}
		if (buf[5] != 0x02) {
			err("boot bad confirm subtype.");
			return -EIO;
		}
		for (i = 4; i <= 6; i++)
			checksum += buf[i];
		if (buf[7] * 256 + buf[8] != checksum) {
			err("boot bad confirm checksum.");
			return -EIO;
		}
		*reply = buf[6];
		break;
	case FW_BOOT:
		if (buf[2] != 0x10) {
			err("boot bad boot header.");
			return -EIO;
		}
		if (buf[3] != 0x05) {
			err("boot bad boot size.");
			return -EIO;
		}
		if (buf[4] != 0x00) {
			err("boot bad boot sequence.");
			return -EIO;
		}
		if (buf[5] != 0x01) {
			err("boot bad boot pattern 01.");
			return -EIO;
		}
		if (buf[6] != 0x10) {
			err("boot bad boot pattern 10.");
			return -EIO;
		}
		for (i = 4; i <= 6; i++)
			checksum += buf[i];
		if (buf[7] * 256 + buf[8] != checksum) {
			err("boot bad boot checksum.");
			return -EIO;
		}
		break;

	}

	return 0;
}

static int af9005_download_firmware(struct usb_device *udev, const struct firmware *fw)
{
	int i, packets, ret, act_len;

	u8 *buf;
	u8 reply;

	buf = kmalloc(FW_BULKOUT_SIZE + 2, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = af9005_boot_packet(udev, FW_CONFIG, &reply, buf,
				 FW_BULKOUT_SIZE + 2);
	if (ret)
		goto err;
	if (reply != 0x01) {
		err("before downloading firmware, FW_CONFIG expected 0x01, received 0x%x", reply);
		ret = -EIO;
		goto err;
	}
	packets = fw->size / FW_BULKOUT_SIZE;
	buf[0] = (u8) (FW_BULKOUT_SIZE & 0xff);
	buf[1] = (u8) ((FW_BULKOUT_SIZE >> 8) & 0xff);
	for (i = 0; i < packets; i++) {
		memcpy(&buf[2], fw->data + i * FW_BULKOUT_SIZE,
		       FW_BULKOUT_SIZE);
		deb_fw(">>> ");
		debug_dump(buf, FW_BULKOUT_SIZE + 2, deb_fw);
		ret = usb_bulk_msg(udev,
				   usb_sndbulkpipe(udev, 0x02),
				   buf, FW_BULKOUT_SIZE + 2, &act_len, 1000);
		if (ret) {
			err("firmware download failed at packet %d with code %d", i, ret);
			goto err;
		}
	}
	ret = af9005_boot_packet(udev, FW_CONFIRM, &reply,
				 buf, FW_BULKOUT_SIZE + 2);
	if (ret)
		goto err;
	if (reply != (u8) (packets & 0xff)) {
		err("after downloading firmware, FW_CONFIRM expected 0x%x, received 0x%x", packets & 0xff, reply);
		ret = -EIO;
		goto err;
	}
	ret = af9005_boot_packet(udev, FW_BOOT, &reply, buf,
				 FW_BULKOUT_SIZE + 2);
	if (ret)
		goto err;
	ret = af9005_boot_packet(udev, FW_CONFIG, &reply, buf,
				 FW_BULKOUT_SIZE + 2);
	if (ret)
		goto err;
	if (reply != 0x02) {
		err("after downloading firmware, FW_CONFIG expected 0x02, received 0x%x", reply);
		ret = -EIO;
		goto err;
	}

err:
	kfree(buf);
	return ret;

}

int af9005_led_control(struct dvb_usb_device *d, int onoff)
{
	struct af9005_device_state *st = d->priv;
	int temp, ret;

	if (onoff && dvb_usb_af9005_led)
		temp = 1;
	else
		temp = 0;
	if (st->led_state != temp) {
		ret =
		    af9005_write_register_bits(d, xd_p_reg_top_locken1,
					       reg_top_locken1_pos,
					       reg_top_locken1_len, temp);
		if (ret)
			return ret;
		ret =
		    af9005_write_register_bits(d, xd_p_reg_top_lock1,
					       reg_top_lock1_pos,
					       reg_top_lock1_len, temp);
		if (ret)
			return ret;
		st->led_state = temp;
	}
	return 0;
}

static int af9005_frontend_attach(struct dvb_usb_adapter *adap)
{
	u8 buf[8];
	int i;

	/* without these calls the first commands after downloading
	   the firmware fail. I put these calls here to simulate
	   what it is done in dvb-usb-init.c.
	 */
	struct usb_device *udev = adap->dev->udev;
	usb_clear_halt(udev, usb_sndbulkpipe(udev, 2));
	usb_clear_halt(udev, usb_rcvbulkpipe(udev, 1));
	if (dvb_usb_af9005_dump_eeprom) {
		printk("EEPROM DUMP\n");
		for (i = 0; i < 255; i += 8) {
			af9005_read_eeprom(adap->dev, i, buf, 8);
			debug_dump(buf, 8, printk);
		}
	}
	adap->fe_adap[0].fe = af9005_fe_attach(adap->dev);
	return 0;
}

static int af9005_rc_query(struct dvb_usb_device *d, u32 * event, int *state)
{
	struct af9005_device_state *st = d->priv;
	int ret, len;
	u8 seq;

	*state = REMOTE_NO_KEY_PRESSED;
	if (rc_decode == NULL) {
		/* it shouldn't never come here */
		return 0;
	}

	mutex_lock(&d->data_mutex);

	/* deb_info("rc_query\n"); */
	st->data[0] = 3;		/* rest of packet length low */
	st->data[1] = 0;		/* rest of packet length high */
	st->data[2] = 0x40;		/* read remote */
	st->data[3] = 1;		/* rest of packet length */
	st->data[4] = seq = st->sequence++;	/* sequence number */
	ret = dvb_usb_generic_rw(d, st->data, 5, st->data, 256, 0);
	if (ret) {
		err("rc query failed");
		goto ret;
	}
	if (st->data[2] != 0x41) {
		err("rc query bad header.");
		ret = -EIO;
		goto ret;
	} else if (st->data[4] != seq) {
		err("rc query bad sequence.");
		ret = -EIO;
		goto ret;
	}
	len = st->data[5];
	if (len > 246) {
		err("rc query invalid length");
		ret = -EIO;
		goto ret;
	}
	if (len > 0) {
		deb_rc("rc data (%d) ", len);
		debug_dump((st->data + 6), len, deb_rc);
		ret = rc_decode(d, &st->data[6], len, event, state);
		if (ret) {
			err("rc_decode failed");
			goto ret;
		} else {
			deb_rc("rc_decode state %x event %x\n", *state, *event);
			if (*state == REMOTE_KEY_REPEAT)
				*event = d->last_event;
		}
	}

ret:
	mutex_unlock(&d->data_mutex);
	return ret;
}

static int af9005_power_ctrl(struct dvb_usb_device *d, int onoff)
{

	return 0;
}

static int af9005_pid_filter_control(struct dvb_usb_adapter *adap, int onoff)
{
	int ret;
	deb_info("pid filter control  onoff %d\n", onoff);
	if (onoff) {
		ret =
		    af9005_write_ofdm_register(adap->dev, XD_MP2IF_DMX_CTRL, 1);
		if (ret)
			return ret;
		ret =
		    af9005_write_register_bits(adap->dev,
					       XD_MP2IF_DMX_CTRL, 1, 1, 1);
		if (ret)
			return ret;
		ret =
		    af9005_write_ofdm_register(adap->dev, XD_MP2IF_DMX_CTRL, 1);
	} else
		ret =
		    af9005_write_ofdm_register(adap->dev, XD_MP2IF_DMX_CTRL, 0);
	if (ret)
		return ret;
	deb_info("pid filter control ok\n");
	return 0;
}

static int af9005_pid_filter(struct dvb_usb_adapter *adap, int index,
			     u16 pid, int onoff)
{
	u8 cmd = index & 0x1f;
	int ret;
	deb_info("set pid filter, index %d, pid %x, onoff %d\n", index,
		 pid, onoff);
	if (onoff) {
		/* cannot use it as pid_filter_ctrl since it has to be done
		   before setting the first pid */
		if (adap->feedcount == 1) {
			deb_info("first pid set, enable pid table\n");
			ret = af9005_pid_filter_control(adap, onoff);
			if (ret)
				return ret;
		}
		ret =
		    af9005_write_ofdm_register(adap->dev,
					       XD_MP2IF_PID_DATA_L,
					       (u8) (pid & 0xff));
		if (ret)
			return ret;
		ret =
		    af9005_write_ofdm_register(adap->dev,
					       XD_MP2IF_PID_DATA_H,
					       (u8) (pid >> 8));
		if (ret)
			return ret;
		cmd |= 0x20 | 0x40;
	} else {
		if (adap->feedcount == 0) {
			deb_info("last pid unset, disable pid table\n");
			ret = af9005_pid_filter_control(adap, onoff);
			if (ret)
				return ret;
		}
	}
	ret = af9005_write_ofdm_register(adap->dev, XD_MP2IF_PID_IDX, cmd);
	if (ret)
		return ret;
	deb_info("set pid ok\n");
	return 0;
}

static int af9005_identify_state(struct usb_device *udev,
				 const struct dvb_usb_device_properties *props,
				 const struct dvb_usb_device_description **desc,
				 int *cold)
{
	int ret;
	u8 reply, *buf;

	buf = kmalloc(FW_BULKOUT_SIZE + 2, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = af9005_boot_packet(udev, FW_CONFIG, &reply,
				 buf, FW_BULKOUT_SIZE + 2);
	if (ret)
		goto err;
	deb_info("result of FW_CONFIG in identify state %d\n", reply);
	if (reply == 0x01)
		*cold = 1;
	else if (reply == 0x02)
		*cold = 0;
	else
		ret = -EIO;
	if (!ret)
		deb_info("Identify state cold = %d\n", *cold);

err:
	kfree(buf);
	return ret;
}

static struct dvb_usb_device_properties af9005_properties;

static int af9005_usb_probe(struct usb_interface *intf,
			    const struct usb_device_id *id)
{
	return dvb_usb_device_init(intf, &af9005_properties,
				  THIS_MODULE, NULL, adapter_nr);
}

enum af9005_usb_table_entry {
	AFATECH_AF9005,
	TERRATEC_AF9005,
	ANSONIC_AF9005,
};

static struct usb_device_id af9005_usb_table[] = {
	[AFATECH_AF9005] = {USB_DEVICE(USB_VID_AFATECH,
				USB_PID_AFATECH_AF9005)},
	[TERRATEC_AF9005] = {USB_DEVICE(USB_VID_TERRATEC,
				USB_PID_TERRATEC_CINERGY_T_USB_XE)},
	[ANSONIC_AF9005] = {USB_DEVICE(USB_VID_ANSONIC,
				USB_PID_ANSONIC_DVBT_USB)},
	{ }
};

MODULE_DEVICE_TABLE(usb, af9005_usb_table);

static struct dvb_usb_device_properties af9005_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl = DEVICE_SPECIFIC,
	.firmware = "af9005.fw",
	.download_firmware = af9005_download_firmware,
	.no_reconnect = 1,

	.size_of_priv = sizeof(struct af9005_device_state),

	.num_adapters = 1,
	.adapter = {
		    {
		    .num_frontends = 1,
		    .fe = {{
		     .caps =
		     DVB_USB_ADAP_HAS_PID_FILTER |
		     DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
		     .pid_filter_count = 32,
		     .pid_filter = af9005_pid_filter,
		     /* .pid_filter_ctrl = af9005_pid_filter_control, */
		     .frontend_attach = af9005_frontend_attach,
		     /* .tuner_attach     = af9005_tuner_attach, */
		     /* parameter for the MPEG2-data transfer */
		     .stream = {
				.type = USB_BULK,
				.count = 10,
				.endpoint = 0x04,
				.u = {
				      .bulk = {
					       .buffersize = 4096,	/* actual size seen is 3948 */
					       }
				      }
				},
		     }},
		     }
		    },
	.power_ctrl = af9005_power_ctrl,
	.identify_state = af9005_identify_state,

	.i2c_algo = &af9005_i2c_algo,

	.rc.legacy = {
		.rc_interval = 200,
		.rc_map_table = NULL,
		.rc_map_size = 0,
		.rc_query = af9005_rc_query,
	},

	.generic_bulk_ctrl_endpoint          = 2,
	.generic_bulk_ctrl_endpoint_response = 1,

	.num_device_descs = 3,
	.devices = {
		    {.name = "Afatech DVB-T USB1.1 stick",
		     .cold_ids = {&af9005_usb_table[AFATECH_AF9005], NULL},
		     .warm_ids = {NULL},
		     },
		    {.name = "TerraTec Cinergy T USB XE",
		     .cold_ids = {&af9005_usb_table[TERRATEC_AF9005], NULL},
		     .warm_ids = {NULL},
		     },
		    {.name = "Ansonic DVB-T USB1.1 stick",
		     .cold_ids = {&af9005_usb_table[ANSONIC_AF9005], NULL},
		     .warm_ids = {NULL},
		     },
		    {NULL},
		    }
};

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver af9005_usb_driver = {
	.name = "dvb_usb_af9005",
	.probe = af9005_usb_probe,
	.disconnect = dvb_usb_device_exit,
	.id_table = af9005_usb_table,
};

/* module stuff */
static int __init af9005_usb_module_init(void)
{
	int result;
	if ((result = usb_register(&af9005_usb_driver))) {
		err("usb_register failed. (%d)", result);
		return result;
	}
#if IS_MODULE(CONFIG_DVB_USB_AF9005) || defined(CONFIG_DVB_USB_AF9005_REMOTE)
	/* FIXME: convert to todays kernel IR infrastructure */
	rc_decode = symbol_request(af9005_rc_decode);
	rc_keys = symbol_request(rc_map_af9005_table);
	rc_keys_size = symbol_request(rc_map_af9005_table_size);
#endif
	if (rc_decode == NULL || rc_keys == NULL || rc_keys_size == NULL) {
		err("af9005_rc_decode function not found, disabling remote");
		af9005_properties.rc.legacy.rc_query = NULL;
	} else {
		af9005_properties.rc.legacy.rc_map_table = rc_keys;
		af9005_properties.rc.legacy.rc_map_size = *rc_keys_size;
	}

	return 0;
}

static void __exit af9005_usb_module_exit(void)
{
	/* release rc decode symbols */
	if (rc_decode != NULL)
		symbol_put(af9005_rc_decode);
	if (rc_keys != NULL)
		symbol_put(rc_map_af9005_table);
	if (rc_keys_size != NULL)
		symbol_put(rc_map_af9005_table_size);
	/* deregister this driver from the USB subsystem */
	usb_deregister(&af9005_usb_driver);
}

module_init(af9005_usb_module_init);
module_exit(af9005_usb_module_exit);

MODULE_AUTHOR("Luca Olivetti <luca@ventoso.org>");
MODULE_DESCRIPTION("Driver for Afatech 9005 DVB-T USB1.1 stick");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
