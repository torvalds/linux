/*
 * DVB USB compliant linux driver for ITE IT9135 and IT9137
 *
 * Copyright (C) 2011 Malcolm Priestley (tvboxspy@gmail.com)
 * IT9135 (C) ITE Tech Inc.
 * IT9137 (C) ITE Tech Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 * see Documentation/dvb/README.dvb-usb for more information
 * see Documentation/dvb/it9137.txt for firmware information
 *
 */
#define DVB_USB_LOG_PREFIX "it913x"

#include <linux/usb.h>
#include <linux/usb/input.h>
#include <media/rc-core.h>

#include "dvb_usb.h"
#include "it913x-fe.h"

/* debug */
static int dvb_usb_it913x_debug;
#define it_debug(var, level, args...) \
	do { if ((var & level)) pr_debug(DVB_USB_LOG_PREFIX": " args); \
} while (0)
#define deb_info(level, args...) it_debug(dvb_usb_it913x_debug, level, args)
#define info(args...) pr_info(DVB_USB_LOG_PREFIX": " args)

module_param_named(debug, dvb_usb_it913x_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info (or-able)).");

static int dvb_usb_it913x_firmware;
module_param_named(firmware, dvb_usb_it913x_firmware, int, 0644);
MODULE_PARM_DESC(firmware, "set firmware 0=auto "\
	"1=IT9137 2=IT9135 V1 3=IT9135 V2");
#define FW_IT9137 "dvb-usb-it9137-01.fw"
#define FW_IT9135_V1 "dvb-usb-it9135-01.fw"
#define FW_IT9135_V2 "dvb-usb-it9135-02.fw"

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

struct it913x_state {
	struct ite_config it913x_config;
	u8 pid_filter_onoff;
	bool proprietary_ir;
	int cmd_counter;
};

static u16 check_sum(u8 *p, u8 len)
{
	u16 sum = 0;
	u8 i = 1;
	while (i < len)
		sum += (i++ & 1) ? (*p++) << 8 : *p++;
	return ~sum;
}

static int it913x_io(struct dvb_usb_device *d, u8 mode, u8 pro,
			u8 cmd, u32 reg, u8 addr, u8 *data, u8 len)
{
	struct it913x_state *st = d->priv;
	int ret = 0, i, buf_size = 1;
	u8 *buff;
	u8 rlen;
	u16 chk_sum;

	buff = kzalloc(256, GFP_KERNEL);
	if (!buff) {
		info("USB Buffer Failed");
		return -ENOMEM;
	}

	buff[buf_size++] = pro;
	buff[buf_size++] = cmd;
	buff[buf_size++] = st->cmd_counter;

	switch (mode) {
	case READ_LONG:
	case WRITE_LONG:
		buff[buf_size++] = len;
		buff[buf_size++] = 2;
		buff[buf_size++] = (reg >> 24);
		buff[buf_size++] = (reg >> 16) & 0xff;
		buff[buf_size++] = (reg >> 8) & 0xff;
		buff[buf_size++] = reg & 0xff;
	break;
	case READ_SHORT:
		buff[buf_size++] = addr;
		break;
	case WRITE_SHORT:
		buff[buf_size++] = len;
		buff[buf_size++] = addr;
		buff[buf_size++] = (reg >> 8) & 0xff;
		buff[buf_size++] = reg & 0xff;
	break;
	case READ_DATA:
	case WRITE_DATA:
		break;
	case WRITE_CMD:
		mode = 7;
		break;
	default:
		kfree(buff);
		return -EINVAL;
	}

	if (mode & 1) {
		for (i = 0; i < len ; i++)
			buff[buf_size++] = data[i];
	}
	chk_sum = check_sum(&buff[1], buf_size);

	buff[buf_size++] = chk_sum >> 8;
	buff[0] = buf_size;
	buff[buf_size++] = (chk_sum & 0xff);

	ret = dvb_usbv2_generic_rw(d, buff, buf_size, buff, (mode & 1) ?
			5 : len + 5);
	if (ret < 0)
		goto error;

	rlen = (mode & 0x1) ? 0x1 : len;

	if (mode & 1)
		ret = buff[2];
	else
		memcpy(data, &buff[3], rlen);

	st->cmd_counter++;

error:	kfree(buff);

	return ret;
}

static int it913x_wr_reg(struct dvb_usb_device *d, u8 pro, u32 reg , u8 data)
{
	int ret;
	u8 b[1];
	b[0] = data;
	ret = it913x_io(d, WRITE_LONG, pro,
			CMD_DEMOD_WRITE, reg, 0, b, sizeof(b));

	return ret;
}

static int it913x_read_reg(struct dvb_usb_device *d, u32 reg)
{
	int ret;
	u8 data[1];

	ret = it913x_io(d, READ_LONG, DEV_0,
			CMD_DEMOD_READ, reg, 0, &data[0], sizeof(data));

	return (ret < 0) ? ret : data[0];
}

static int it913x_query(struct dvb_usb_device *d, u8 pro)
{
	struct it913x_state *st = d->priv;
	int ret, i;
	u8 data[4];
	u8 ver;

	for (i = 0; i < 5; i++) {
		ret = it913x_io(d, READ_LONG, pro, CMD_DEMOD_READ,
			0x1222, 0, &data[0], 3);
		ver = data[0];
		if (ver > 0 && ver < 3)
			break;
		msleep(100);
	}

	if (ver < 1 || ver > 2) {
		info("Failed to identify chip version applying 1");
		st->it913x_config.chip_ver = 0x1;
		st->it913x_config.chip_type = 0x9135;
		return 0;
	}

	st->it913x_config.chip_ver = ver;
	st->it913x_config.chip_type = (u16)(data[2] << 8) + data[1];

	info("Chip Version=%02x Chip Type=%04x", st->it913x_config.chip_ver,
		st->it913x_config.chip_type);

	ret = it913x_io(d, READ_SHORT, pro,
			CMD_QUERYINFO, 0, 0x1, &data[0], 4);

	st->it913x_config.firmware = (data[0] << 24) | (data[1] << 16) |
			(data[2] << 8) | data[3];

	return ret;
}

static int it913x_pid_filter_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	struct dvb_usb_device *d = adap_to_d(adap);
	struct it913x_state *st = adap_to_priv(adap);
	int ret;
	u8 pro = (adap->id == 0) ? DEV_0_DMOD : DEV_1_DMOD;

	mutex_lock(&d->i2c_mutex);

	deb_info(1, "PID_C  (%02x)", onoff);

	st->pid_filter_onoff = adap->pid_filtering;
	ret = it913x_wr_reg(d, pro, PID_EN, st->pid_filter_onoff);

	mutex_unlock(&d->i2c_mutex);
	return ret;
}

static int it913x_pid_filter(struct dvb_usb_adapter *adap,
		int index, u16 pid, int onoff)
{
	struct dvb_usb_device *d = adap_to_d(adap);
	struct it913x_state *st = adap_to_priv(adap);
	int ret;
	u8 pro = (adap->id == 0) ? DEV_0_DMOD : DEV_1_DMOD;

	mutex_lock(&d->i2c_mutex);

	deb_info(1, "PID_F  (%02x)", onoff);

	ret = it913x_wr_reg(d, pro, PID_LSB, (u8)(pid & 0xff));

	ret |= it913x_wr_reg(d, pro, PID_MSB, (u8)(pid >> 8));

	ret |= it913x_wr_reg(d, pro, PID_INX_EN, (u8)onoff);

	ret |= it913x_wr_reg(d, pro, PID_INX, (u8)(index & 0x1f));

	if (d->udev->speed == USB_SPEED_HIGH && pid == 0x2000) {
			ret |= it913x_wr_reg(d , pro, PID_EN, !onoff);
			st->pid_filter_onoff = !onoff;
	} else
		st->pid_filter_onoff =
			adap->pid_filtering;

	mutex_unlock(&d->i2c_mutex);
	return 0;
}


static int it913x_return_status(struct dvb_usb_device *d)
{
	struct it913x_state *st = d->priv;
	int ret = it913x_query(d, DEV_0);
	if (st->it913x_config.firmware > 0)
		info("Firmware Version %d", st->it913x_config.firmware);

	return ret;
}

static int it913x_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msg[],
				 int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	static u8 data[256];
	int ret;
	u32 reg;
	u8 pro;

	mutex_lock(&d->i2c_mutex);

	deb_info(2, "num of messages %d address %02x", num, msg[0].addr);

	pro = (msg[0].addr & 0x2) ?  DEV_0_DMOD : 0x0;
	pro |= (msg[0].addr & 0x20) ? DEV_1 : DEV_0;
	memcpy(data, msg[0].buf, msg[0].len);
	reg = (data[0] << 24) + (data[1] << 16) +
			(data[2] << 8) + data[3];
	if (num == 2) {
		ret = it913x_io(d, READ_LONG, pro,
			CMD_DEMOD_READ, reg, 0, data, msg[1].len);
		memcpy(msg[1].buf, data, msg[1].len);
	} else
		ret = it913x_io(d, WRITE_LONG, pro, CMD_DEMOD_WRITE,
			reg, 0, &data[4], msg[0].len - 4);

	mutex_unlock(&d->i2c_mutex);

	return ret;
}

static u32 it913x_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm it913x_i2c_algo = {
	.master_xfer   = it913x_i2c_xfer,
	.functionality = it913x_i2c_func,
};

/* Callbacks for DVB USB */
#if IS_ENABLED(CONFIG_RC_CORE)
static int it913x_rc_query(struct dvb_usb_device *d)
{
	u8 ibuf[4];
	int ret;
	u32 key;
	/* Avoid conflict with frontends*/
	mutex_lock(&d->i2c_mutex);

	ret = it913x_io(d, READ_LONG, PRO_LINK, CMD_IR_GET,
		0, 0, &ibuf[0], sizeof(ibuf));

	if ((ibuf[2] + ibuf[3]) == 0xff) {
		key = ibuf[2];
		key += ibuf[0] << 16;
		key += ibuf[1] << 8;
		deb_info(1, "NEC Extended Key =%08x", key);
		if (d->rc_dev != NULL)
			rc_keydown(d->rc_dev, key, 0);
	}

	mutex_unlock(&d->i2c_mutex);

	return ret;
}

static int it913x_get_rc_config(struct dvb_usb_device *d, struct dvb_usb_rc *rc)
{
	struct it913x_state *st = d->priv;

	if (st->proprietary_ir == false) {
		rc->map_name = NULL;
		return 0;
	}

	rc->allowed_protos = RC_BIT_NEC;
	rc->query = it913x_rc_query;
	rc->interval = 250;

	return 0;
}
#else
	#define it913x_get_rc_config NULL
#endif

/* Firmware sets raw */
static const char fw_it9135_v1[] = FW_IT9135_V1;
static const char fw_it9135_v2[] = FW_IT9135_V2;
static const char fw_it9137[] = FW_IT9137;

static void ite_get_firmware_name(struct dvb_usb_device *d,
	const char **name)
{
	struct it913x_state *st = d->priv;
	int sw;
	/* auto switch */
	if (le16_to_cpu(d->udev->descriptor.idVendor) == USB_VID_KWORLD_2)
		sw = IT9137_FW;
	else if (st->it913x_config.chip_ver == 1)
		sw = IT9135_V1_FW;
	else
		sw = IT9135_V2_FW;

	/* force switch */
	if (dvb_usb_it913x_firmware != IT9135_AUTO)
		sw = dvb_usb_it913x_firmware;

	switch (sw) {
	case IT9135_V1_FW:
		st->it913x_config.firmware_ver = 1;
		st->it913x_config.adc_x2 = 1;
		st->it913x_config.read_slevel = false;
		*name = fw_it9135_v1;
		break;
	case IT9135_V2_FW:
		st->it913x_config.firmware_ver = 1;
		st->it913x_config.adc_x2 = 1;
		st->it913x_config.read_slevel = false;
		*name = fw_it9135_v2;
		switch (st->it913x_config.tuner_id_0) {
		case IT9135_61:
		case IT9135_62:
			break;
		default:
			info("Unknown tuner ID applying default 0x60");
		case IT9135_60:
			st->it913x_config.tuner_id_0 = IT9135_60;
		}
		break;
	case IT9137_FW:
	default:
		st->it913x_config.firmware_ver = 0;
		st->it913x_config.adc_x2 = 0;
		st->it913x_config.read_slevel = true;
		*name = fw_it9137;
	}

	return;
}

#define TS_MPEG_PKT_SIZE	188
#define EP_LOW			21
#define TS_BUFFER_SIZE_PID	(EP_LOW*TS_MPEG_PKT_SIZE)
#define EP_HIGH			348
#define TS_BUFFER_SIZE_MAX	(EP_HIGH*TS_MPEG_PKT_SIZE)

static int it913x_get_stream_config(struct dvb_frontend *fe, u8 *ts_type,
		struct usb_data_stream_properties *stream)
{
	struct dvb_usb_adapter *adap = fe_to_adap(fe);
	if (adap->pid_filtering)
		stream->u.bulk.buffersize = TS_BUFFER_SIZE_PID;
	else
		stream->u.bulk.buffersize = TS_BUFFER_SIZE_MAX;

	return 0;
}

static int it913x_select_config(struct dvb_usb_device *d)
{
	struct it913x_state *st = d->priv;
	int ret, reg;

	ret = it913x_return_status(d);
	if (ret < 0)
		return ret;

	if (st->it913x_config.chip_ver == 0x02
			&& st->it913x_config.chip_type == 0x9135)
		reg = it913x_read_reg(d, 0x461d);
	else
		reg = it913x_read_reg(d, 0x461b);

	if (reg < 0)
		return reg;

	if (reg == 0) {
		st->it913x_config.dual_mode = 0;
		st->it913x_config.tuner_id_0 = IT9135_38;
		st->proprietary_ir = true;
	} else {
		/* TS mode */
		reg =  it913x_read_reg(d, 0x49c5);
		if (reg < 0)
			return reg;
		st->it913x_config.dual_mode = reg;

		/* IR mode type */
		reg = it913x_read_reg(d, 0x49ac);
		if (reg < 0)
			return reg;
		if (reg == 5) {
			info("Remote propriety (raw) mode");
			st->proprietary_ir = true;
		} else if (reg == 1) {
			info("Remote HID mode NOT SUPPORTED");
			st->proprietary_ir = false;
		}

		/* Tuner_id */
		reg = it913x_read_reg(d, 0x49d0);
		if (reg < 0)
			return reg;
		st->it913x_config.tuner_id_0 = reg;
	}

	info("Dual mode=%x Tuner Type=%x", st->it913x_config.dual_mode,
		st->it913x_config.tuner_id_0);

	return ret;
}

static int it913x_streaming_ctrl(struct dvb_frontend *fe, int onoff)
{
	struct dvb_usb_adapter *adap = fe_to_adap(fe);
	struct dvb_usb_device *d = adap_to_d(adap);
	struct it913x_state *st = fe_to_priv(fe);
	int ret = 0;
	u8 pro = (adap->id == 0) ? DEV_0_DMOD : DEV_1_DMOD;

	deb_info(1, "STM  (%02x)", onoff);

	if (!onoff) {
		mutex_lock(&d->i2c_mutex);

		ret = it913x_wr_reg(d, pro, PID_RST, 0x1);

		mutex_unlock(&d->i2c_mutex);
		st->pid_filter_onoff =
			adap->pid_filtering;

	}

	return ret;
}

static int it913x_identify_state(struct dvb_usb_device *d, const char **name)
{
	struct it913x_state *st = d->priv;
	int ret;
	u8 reg;

	/* Read and select config */
	ret = it913x_select_config(d);
	if (ret < 0)
		return ret;

	ite_get_firmware_name(d, name);

	if (st->it913x_config.firmware > 0)
		return WARM;

	if (st->it913x_config.dual_mode) {
		st->it913x_config.tuner_id_1 = it913x_read_reg(d, 0x49e0);
		ret = it913x_wr_reg(d, DEV_0, GPIOH1_EN, 0x1);
		ret |= it913x_wr_reg(d, DEV_0, GPIOH1_ON, 0x1);
		ret |= it913x_wr_reg(d, DEV_0, GPIOH1_O, 0x1);
		msleep(50);
		ret |= it913x_wr_reg(d, DEV_0, GPIOH1_O, 0x0);
		msleep(50);
		reg = it913x_read_reg(d, GPIOH1_O);
		if (reg == 0) {
			ret |= it913x_wr_reg(d, DEV_0,  GPIOH1_O, 0x1);
			ret |= it913x_return_status(d);
			if (ret != 0)
				ret = it913x_wr_reg(d, DEV_0,
					GPIOH1_O, 0x0);
		}
	}

	reg = it913x_read_reg(d, IO_MUX_POWER_CLK);

	if (st->it913x_config.dual_mode) {
		ret |= it913x_wr_reg(d, DEV_0, 0x4bfb, CHIP2_I2C_ADDR);
		if (st->it913x_config.firmware_ver == 1)
			ret |= it913x_wr_reg(d, DEV_0,  0xcfff, 0x1);
		else
			ret |= it913x_wr_reg(d, DEV_0,  CLK_O_EN, 0x1);
	} else {
		ret |= it913x_wr_reg(d, DEV_0, 0x4bfb, 0x0);
		if (st->it913x_config.firmware_ver == 1)
			ret |= it913x_wr_reg(d, DEV_0,  0xcfff, 0x0);
		else
			ret |= it913x_wr_reg(d, DEV_0,  CLK_O_EN, 0x0);
	}

	ret |= it913x_wr_reg(d, DEV_0,  I2C_CLK, I2C_CLK_100);

	return (ret < 0) ? ret : COLD;
}

static int it913x_download_firmware(struct dvb_usb_device *d,
					const struct firmware *fw)
{
	struct it913x_state *st = d->priv;
	int ret = 0, i = 0, pos = 0;
	u8 packet_size, min_pkt;
	u8 *fw_data;

	ret = it913x_wr_reg(d, DEV_0,  I2C_CLK, I2C_CLK_100);

	info("FRM Starting Firmware Download");

	/* Multi firmware loader */
	/* This uses scatter write firmware headers */
	/* The firmware must start with 03 XX 00 */
	/* and be the extact firmware length */

	if (st->it913x_config.chip_ver == 2)
		min_pkt = 0x11;
	else
		min_pkt = 0x19;

	while (i <= fw->size) {
		if (((fw->data[i] == 0x3) && (fw->data[i + 2] == 0x0))
			|| (i == fw->size)) {
			packet_size = i - pos;
			if ((packet_size > min_pkt) || (i == fw->size)) {
				fw_data = (u8 *)(fw->data + pos);
				pos += packet_size;
				if (packet_size > 0) {
					ret = it913x_io(d, WRITE_DATA,
						DEV_0, CMD_SCATTER_WRITE, 0,
						0, fw_data, packet_size);
					if (ret < 0)
						break;
				}
				udelay(1000);
			}
		}
		i++;
	}

	if (ret < 0)
		info("FRM Firmware Download Failed (%d)" , ret);
	else
		info("FRM Firmware Download Completed - Resetting Device");

	msleep(30);

	ret = it913x_io(d, WRITE_CMD, DEV_0, CMD_BOOT, 0, 0, NULL, 0);
	if (ret < 0)
		info("FRM Device not responding to reboot");

	ret = it913x_return_status(d);
	if (st->it913x_config.firmware == 0) {
		info("FRM Failed to reboot device");
		return -ENODEV;
	}

	msleep(30);

	ret = it913x_wr_reg(d, DEV_0,  I2C_CLK, I2C_CLK_400);

	msleep(30);

	/* Tuner function */
	if (st->it913x_config.dual_mode)
		ret |= it913x_wr_reg(d, DEV_0_DMOD , 0xec4c, 0xa0);
	else
		ret |= it913x_wr_reg(d, DEV_0_DMOD , 0xec4c, 0x68);

	if ((st->it913x_config.chip_ver == 1) &&
		(st->it913x_config.chip_type == 0x9135)) {
		ret |= it913x_wr_reg(d, DEV_0,  PADODPU, 0x0);
		ret |= it913x_wr_reg(d, DEV_0,  AGC_O_D, 0x0);
		if (st->it913x_config.dual_mode) {
			ret |= it913x_wr_reg(d, DEV_1,  PADODPU, 0x0);
			ret |= it913x_wr_reg(d, DEV_1,  AGC_O_D, 0x0);
		}
	}

	return (ret < 0) ? -ENODEV : 0;
}

static int it913x_name(struct dvb_usb_adapter *adap)
{
	struct dvb_usb_device *d = adap_to_d(adap);
	const char *desc = d->name;
	char *fe_name[] = {"_1", "_2", "_3", "_4"};
	char *name = adap->fe[0]->ops.info.name;

	strlcpy(name, desc, 128);
	strlcat(name, fe_name[adap->id], 128);

	return 0;
}

static int it913x_frontend_attach(struct dvb_usb_adapter *adap)
{
	struct dvb_usb_device *d = adap_to_d(adap);
	struct it913x_state *st = d->priv;
	int ret = 0;
	u8 adap_addr = I2C_BASE_ADDR + (adap->id << 5);
	u16 ep_size = (adap->pid_filtering) ? TS_BUFFER_SIZE_PID / 4 :
		TS_BUFFER_SIZE_MAX / 4;
	u8 pkt_size = 0x80;

	if (d->udev->speed != USB_SPEED_HIGH)
		pkt_size = 0x10;

	st->it913x_config.adf = it913x_read_reg(d, IO_MUX_POWER_CLK);

	adap->fe[0] = dvb_attach(it913x_fe_attach,
		&d->i2c_adap, adap_addr, &st->it913x_config);

	if (adap->id == 0 && adap->fe[0]) {
		it913x_wr_reg(d, DEV_0_DMOD, MP2_SW_RST, 0x1);
		it913x_wr_reg(d, DEV_0_DMOD, MP2IF2_SW_RST, 0x1);
		it913x_wr_reg(d, DEV_0, EP0_TX_EN, 0x0f);
		it913x_wr_reg(d, DEV_0, EP0_TX_NAK, 0x1b);
		if (st->proprietary_ir == false) /* Enable endpoint 3 */
			it913x_wr_reg(d, DEV_0, EP0_TX_EN, 0x3f);
		else
			it913x_wr_reg(d, DEV_0, EP0_TX_EN, 0x2f);
		it913x_wr_reg(d, DEV_0, EP4_TX_LEN_LSB,
					ep_size & 0xff);
		it913x_wr_reg(d, DEV_0, EP4_TX_LEN_MSB, ep_size >> 8);
		ret = it913x_wr_reg(d, DEV_0, EP4_MAX_PKT, pkt_size);
	} else if (adap->id == 1 && adap->fe[0]) {
		if (st->proprietary_ir == false)
			it913x_wr_reg(d, DEV_0, EP0_TX_EN, 0x7f);
		else
			it913x_wr_reg(d, DEV_0, EP0_TX_EN, 0x6f);
		it913x_wr_reg(d, DEV_0, EP5_TX_LEN_LSB,
					ep_size & 0xff);
		it913x_wr_reg(d, DEV_0, EP5_TX_LEN_MSB, ep_size >> 8);
		it913x_wr_reg(d, DEV_0, EP5_MAX_PKT, pkt_size);
		it913x_wr_reg(d, DEV_0_DMOD, MP2IF2_EN, 0x1);
		it913x_wr_reg(d, DEV_1_DMOD, MP2IF_SERIAL, 0x1);
		it913x_wr_reg(d, DEV_1, TOP_HOSTB_SER_MODE, 0x1);
		it913x_wr_reg(d, DEV_0_DMOD, TSIS_ENABLE, 0x1);
		it913x_wr_reg(d, DEV_0_DMOD, MP2_SW_RST, 0x0);
		it913x_wr_reg(d, DEV_0_DMOD, MP2IF2_SW_RST, 0x0);
		it913x_wr_reg(d, DEV_0_DMOD, MP2IF2_HALF_PSB, 0x0);
		it913x_wr_reg(d, DEV_0_DMOD, MP2IF_STOP_EN, 0x1);
		it913x_wr_reg(d, DEV_1_DMOD, MPEG_FULL_SPEED, 0x0);
		ret = it913x_wr_reg(d, DEV_1_DMOD, MP2IF_STOP_EN, 0x0);
	} else
		return -ENODEV;

	ret |= it913x_name(adap);

	return ret;
}

/* DVB USB Driver */
static int it913x_get_adapter_count(struct dvb_usb_device *d)
{
	struct it913x_state *st = d->priv;
	if (st->it913x_config.dual_mode)
		return 2;
	return 1;
}

static struct dvb_usb_device_properties it913x_properties = {
	.driver_name = KBUILD_MODNAME,
	.owner = THIS_MODULE,
	.bInterfaceNumber = 0,
	.generic_bulk_ctrl_endpoint = 0x02,
	.generic_bulk_ctrl_endpoint_response = 0x81,

	.adapter_nr = adapter_nr,
	.size_of_priv = sizeof(struct it913x_state),

	.identify_state = it913x_identify_state,
	.i2c_algo = &it913x_i2c_algo,

	.download_firmware = it913x_download_firmware,

	.frontend_attach  = it913x_frontend_attach,
	.get_rc_config = it913x_get_rc_config,
	.get_stream_config = it913x_get_stream_config,
	.get_adapter_count = it913x_get_adapter_count,
	.streaming_ctrl   = it913x_streaming_ctrl,


	.adapter = {
		{
			.caps = DVB_USB_ADAP_HAS_PID_FILTER|
				DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
			.pid_filter_count = 32,
			.pid_filter = it913x_pid_filter,
			.pid_filter_ctrl  = it913x_pid_filter_ctrl,
			.stream =
			DVB_USB_STREAM_BULK(0x84, 10, TS_BUFFER_SIZE_MAX),
		},
		{
			.caps = DVB_USB_ADAP_HAS_PID_FILTER|
				DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
			.pid_filter_count = 32,
			.pid_filter = it913x_pid_filter,
			.pid_filter_ctrl  = it913x_pid_filter_ctrl,
			.stream =
			DVB_USB_STREAM_BULK(0x85, 10, TS_BUFFER_SIZE_MAX),
		}
	}
};

static const struct usb_device_id it913x_id_table[] = {
	{ DVB_USB_DEVICE(USB_VID_KWORLD_2, USB_PID_KWORLD_UB499_2T_T09,
		&it913x_properties, "Kworld UB499-2T T09(IT9137)",
			RC_MAP_IT913X_V1) },
	{ DVB_USB_DEVICE(USB_VID_ITETECH, USB_PID_ITETECH_IT9135,
		&it913x_properties, "ITE 9135 Generic",
			RC_MAP_IT913X_V1) },
	{ DVB_USB_DEVICE(USB_VID_KWORLD_2, USB_PID_SVEON_STV22_IT9137,
		&it913x_properties, "Sveon STV22 Dual DVB-T HDTV(IT9137)",
			RC_MAP_IT913X_V1) },
	{ DVB_USB_DEVICE(USB_VID_ITETECH, USB_PID_ITETECH_IT9135_9005,
		&it913x_properties, "ITE 9135(9005) Generic",
			RC_MAP_IT913X_V2) },
	{ DVB_USB_DEVICE(USB_VID_ITETECH, USB_PID_ITETECH_IT9135_9006,
		&it913x_properties, "ITE 9135(9006) Generic",
			RC_MAP_IT913X_V1) },
	{ DVB_USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_A835B_1835,
		&it913x_properties, "Avermedia A835B(1835)",
			RC_MAP_IT913X_V2) },
	{ DVB_USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_A835B_2835,
		&it913x_properties, "Avermedia A835B(2835)",
			RC_MAP_IT913X_V2) },
	{ DVB_USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_A835B_3835,
		&it913x_properties, "Avermedia A835B(3835)",
			RC_MAP_IT913X_V2) },
	{ DVB_USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_A835B_4835,
		&it913x_properties, "Avermedia A835B(4835)",
			RC_MAP_IT913X_V2) },
	{ DVB_USB_DEVICE(USB_VID_KWORLD_2, USB_PID_CTVDIGDUAL_V2,
		&it913x_properties, "Digital Dual TV Receiver CTVDIGDUAL_V2",
			RC_MAP_IT913X_V1) },
	{}		/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, it913x_id_table);

static struct usb_driver it913x_driver = {
	.name		= KBUILD_MODNAME,
	.probe		= dvb_usbv2_probe,
	.disconnect	= dvb_usbv2_disconnect,
	.suspend	= dvb_usbv2_suspend,
	.resume		= dvb_usbv2_resume,
	.id_table	= it913x_id_table,
};

module_usb_driver(it913x_driver);

MODULE_AUTHOR("Malcolm Priestley <tvboxspy@gmail.com>");
MODULE_DESCRIPTION("it913x USB 2 Driver");
MODULE_VERSION("1.33");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(FW_IT9135_V1);
MODULE_FIRMWARE(FW_IT9135_V2);
MODULE_FIRMWARE(FW_IT9137);

