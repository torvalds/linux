/* DVB USB compliant linux driver for IT9137
 *
 * Copyright (C) 2011 Malcolm Priestley (tvboxspy@gmail.com)
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

#include "dvb-usb.h"
#include "it913x-fe.h"

/* debug */
static int dvb_usb_it913x_debug;
#define l_dprintk(var, level, args...) do { \
	if ((var >= level)) \
		printk(KERN_DEBUG DVB_USB_LOG_PREFIX ": " args); \
} while (0)

#define deb_info(level, args...) l_dprintk(dvb_usb_it913x_debug, level, args)
#define debug_data_snipet(level, name, p) \
	 deb_info(level, name" (%02x%02x%02x%02x%02x%02x%02x%02x)", \
		*p, *(p+1), *(p+2), *(p+3), *(p+4), \
			*(p+5), *(p+6), *(p+7));


module_param_named(debug, dvb_usb_it913x_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info (or-able))."
			DVB_USB_DEBUG_STATUS);

static int pid_filter;
module_param_named(pid, pid_filter, int, 0644);
MODULE_PARM_DESC(pid, "set default 0=on 1=off");

static int dvb_usb_it913x_firmware;
module_param_named(firmware, dvb_usb_it913x_firmware, int, 0644);
MODULE_PARM_DESC(firmware, "set firmware 0=auto 1=IT9137 2=IT9135V1");


int cmd_counter;

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

struct it913x_state {
	u8 id;
	struct ite_config it913x_config;
	u8 pid_filter_onoff;
};

struct ite_config it913x_config;

#define IT913X_RETRY	10
#define IT913X_SND_TIMEOUT	100
#define IT913X_RCV_TIMEOUT	200

static int it913x_bulk_write(struct usb_device *dev,
				u8 *snd, int len, u8 pipe)
{
	int ret, actual_l, i;

	for (i = 0; i < IT913X_RETRY; i++) {
		ret = usb_bulk_msg(dev, usb_sndbulkpipe(dev, pipe),
				snd, len , &actual_l, IT913X_SND_TIMEOUT);
		if (ret == 0 || ret != -EBUSY || ret != -ETIMEDOUT)
			break;
	}

	if (len != actual_l && ret == 0)
		ret = -EAGAIN;

	return ret;
}

static int it913x_bulk_read(struct usb_device *dev,
				u8 *rev, int len, u8 pipe)
{
	int ret, actual_l, i;

	for (i = 0; i < IT913X_RETRY; i++) {
		ret = usb_bulk_msg(dev, usb_rcvbulkpipe(dev, pipe),
				 rev, len , &actual_l, IT913X_RCV_TIMEOUT);
		if (ret == 0 || ret != -EBUSY || ret != -ETIMEDOUT)
			break;
	}

	if (len != actual_l && ret == 0)
		ret = -EAGAIN;

	return ret;
}

static u16 check_sum(u8 *p, u8 len)
{
	u16 sum = 0;
	u8 i = 1;
	while (i < len)
		sum += (i++ & 1) ? (*p++) << 8 : *p++;
	return ~sum;
}

static int it913x_usb_talk(struct usb_device *udev, u8 mode, u8 pro,
			u8 cmd, u32 reg, u8 addr, u8 *data, u8 len)
{
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
	buff[buf_size++] = cmd_counter;

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

	ret = it913x_bulk_write(udev, buff, buf_size , 0x02);
	if (ret < 0)
		goto error;

	ret = it913x_bulk_read(udev, buff, (mode & 1) ?
			5 : len + 5 , 0x01);
	if (ret < 0)
		goto error;

	rlen = (mode & 0x1) ? 0x1 : len;

	if (mode & 1)
		ret = buff[2];
	else
		memcpy(data, &buff[3], rlen);

	cmd_counter++;

error:	kfree(buff);

	return ret;
}

static int it913x_io(struct usb_device *udev, u8 mode, u8 pro,
			u8 cmd, u32 reg, u8 addr, u8 *data, u8 len)
{
	int ret, i;

	for (i = 0; i < IT913X_RETRY; i++) {
		ret = it913x_usb_talk(udev, mode, pro,
			cmd, reg, addr, data, len);
		if (ret != -EAGAIN)
			break;
	}

	return ret;
}

static int it913x_wr_reg(struct usb_device *udev, u8 pro, u32 reg , u8 data)
{
	int ret;
	u8 b[1];
	b[0] = data;
	ret = it913x_io(udev, WRITE_LONG, pro,
			CMD_DEMOD_WRITE, reg, 0, b, sizeof(b));

	return ret;
}

static int it913x_read_reg(struct usb_device *udev, u32 reg)
{
	int ret;
	u8 data[1];

	ret = it913x_io(udev, READ_LONG, DEV_0,
			CMD_DEMOD_READ, reg, 0, &data[0], 1);

	return (ret < 0) ? ret : data[0];
}

static u32 it913x_query(struct usb_device *udev, u8 pro)
{
	int ret;
	u8 data[4];
	ret = it913x_io(udev, READ_LONG, pro, CMD_DEMOD_READ,
		0x1222, 0, &data[0], 3);

	it913x_config.chip_ver = data[0];
	it913x_config.chip_type = (u16)(data[2] << 8) + data[1];

	info("Chip Version=%02x Chip Type=%04x", it913x_config.chip_ver,
		it913x_config.chip_type);

	ret |= it913x_io(udev, READ_SHORT, pro,
			CMD_QUERYINFO, 0, 0x1, &data[0], 4);

	it913x_config.firmware = (data[0] << 24) + (data[1] << 16) +
			(data[2] << 8) + data[3];

	return (ret < 0) ? 0 : it913x_config.firmware;
}

static int it913x_pid_filter_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	struct it913x_state *st = adap->dev->priv;
	struct usb_device *udev = adap->dev->udev;
	int ret;
	u8 pro = (adap->id == 0) ? DEV_0_DMOD : DEV_1_DMOD;

	mutex_lock(&adap->dev->i2c_mutex);

	deb_info(1, "PID_C  (%02x)", onoff);

	ret = it913x_wr_reg(udev, pro, PID_EN, st->pid_filter_onoff);

	mutex_unlock(&adap->dev->i2c_mutex);
	return ret;
}

static int it913x_pid_filter(struct dvb_usb_adapter *adap,
		int index, u16 pid, int onoff)
{
	struct it913x_state *st = adap->dev->priv;
	struct usb_device *udev = adap->dev->udev;
	int ret;
	u8 pro = (adap->id == 0) ? DEV_0_DMOD : DEV_1_DMOD;

	mutex_lock(&adap->dev->i2c_mutex);

	deb_info(1, "PID_F  (%02x)", onoff);

	ret = it913x_wr_reg(udev, pro, PID_LSB, (u8)(pid & 0xff));

	ret |= it913x_wr_reg(udev, pro, PID_MSB, (u8)(pid >> 8));

	ret |= it913x_wr_reg(udev, pro, PID_INX_EN, (u8)onoff);

	ret |= it913x_wr_reg(udev, pro, PID_INX, (u8)(index & 0x1f));

	if (udev->speed == USB_SPEED_HIGH && pid == 0x2000) {
			ret |= it913x_wr_reg(udev, pro, PID_EN, !onoff);
			st->pid_filter_onoff = !onoff;
	} else
		st->pid_filter_onoff =
			adap->fe_adap[adap->active_fe].pid_filtering;

	mutex_unlock(&adap->dev->i2c_mutex);
	return 0;
}


static int it913x_return_status(struct usb_device *udev)
{
	u32 firm = 0;

	firm = it913x_query(udev, DEV_0);
	if (firm > 0)
		info("Firmware Version %d", firm);

	return (firm > 0) ? firm : 0;
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

	debug_data_snipet(1, "Message out", msg[0].buf);
	deb_info(2, "num of messages %d address %02x", num, msg[0].addr);

	pro = (msg[0].addr & 0x2) ?  DEV_0_DMOD : 0x0;
	pro |= (msg[0].addr & 0x20) ? DEV_1 : DEV_0;
	memcpy(data, msg[0].buf, msg[0].len);
	reg = (data[0] << 24) + (data[1] << 16) +
			(data[2] << 8) + data[3];
	if (num == 2) {
		ret = it913x_io(d->udev, READ_LONG, pro,
			CMD_DEMOD_READ, reg, 0, data, msg[1].len);
		memcpy(msg[1].buf, data, msg[1].len);
	} else
		ret = it913x_io(d->udev, WRITE_LONG, pro, CMD_DEMOD_WRITE,
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
#define IT913X_POLL 250
static int it913x_rc_query(struct dvb_usb_device *d)
{
	u8 ibuf[4];
	int ret;
	u32 key;
	/* Avoid conflict with frontends*/
	mutex_lock(&d->i2c_mutex);

	ret = it913x_io(d->udev, READ_LONG, PRO_LINK, CMD_IR_GET,
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

/* Firmware sets raw */
const char fw_it9135_v1[] = "dvb-usb-it9135-01.fw";
const char fw_it9135_v2[] = "dvb-usb-it9135-02.fw";
const char fw_it9137[] = "dvb-usb-it9137-01.fw";

static int ite_firmware_select(struct usb_device *udev,
	struct dvb_usb_device_properties *props)
{
	int sw;
	/* auto switch */
	if (le16_to_cpu(udev->descriptor.idVendor) == USB_VID_KWORLD_2)
		sw = IT9137_FW;
	else if (it913x_config.chip_ver == 1)
		sw = IT9135_V1_FW;
	else
		sw = IT9135_V2_FW;

	/* force switch */
	if (dvb_usb_it913x_firmware != IT9135_AUTO)
		sw = dvb_usb_it913x_firmware;

	switch (sw) {
	case IT9135_V1_FW:
		it913x_config.firmware_ver = 1;
		it913x_config.adc_x2 = 1;
		it913x_config.read_slevel = false;
		props->firmware = fw_it9135_v1;
		break;
	case IT9135_V2_FW:
		it913x_config.firmware_ver = 1;
		it913x_config.adc_x2 = 1;
		it913x_config.read_slevel = false;
		props->firmware = fw_it9135_v2;
		switch (it913x_config.tuner_id_0) {
		case IT9135_61:
		case IT9135_62:
			break;
		default:
			info("Unknown tuner ID applying default 0x60");
		case IT9135_60:
			it913x_config.tuner_id_0 = IT9135_60;
		}
		break;
	case IT9137_FW:
	default:
		it913x_config.firmware_ver = 0;
		it913x_config.adc_x2 = 0;
		it913x_config.read_slevel = true;
		props->firmware = fw_it9137;
	}

	return 0;
}

static void it913x_select_remote(struct usb_device *udev,
	struct dvb_usb_device_properties *props)
{
	switch (le16_to_cpu(udev->descriptor.idProduct)) {
	case USB_PID_ITETECH_IT9135_9005:
		props->rc.core.rc_codes = RC_MAP_IT913X_V2;
		return;
	default:
		props->rc.core.rc_codes = RC_MAP_IT913X_V1;
	}
	return;
}

#define TS_MPEG_PKT_SIZE	188
#define EP_LOW			21
#define TS_BUFFER_SIZE_PID	(EP_LOW*TS_MPEG_PKT_SIZE)
#define EP_HIGH			348
#define TS_BUFFER_SIZE_MAX	(EP_HIGH*TS_MPEG_PKT_SIZE)

static int it913x_select_config(struct usb_device *udev,
	struct dvb_usb_device_properties *props)
{
	int ret = 0, reg;
	bool proprietary_ir = false;

	if (it913x_config.chip_ver == 0x02
			&& it913x_config.chip_type == 0x9135)
		reg = it913x_read_reg(udev, 0x461d);
	else
		reg = it913x_read_reg(udev, 0x461b);

	if (reg < 0)
		return reg;

	if (reg == 0) {
		it913x_config.dual_mode = 0;
		it913x_config.tuner_id_0 = IT9135_38;
		proprietary_ir = true;
	} else {
		/* TS mode */
		reg =  it913x_read_reg(udev, 0x49c5);
		if (reg < 0)
			return reg;
		it913x_config.dual_mode = reg;

		/* IR mode type */
		reg = it913x_read_reg(udev, 0x49ac);
		if (reg < 0)
			return reg;
		if (reg == 5) {
			info("Remote propriety (raw) mode");
			proprietary_ir = true;
		} else if (reg == 1) {
			info("Remote HID mode NOT SUPPORTED");
			proprietary_ir = false;
			props->rc.core.rc_codes = NULL;
		} else
			props->rc.core.rc_codes = NULL;

		/* Tuner_id */
		reg = it913x_read_reg(udev, 0x49d0);
		if (reg < 0)
			return reg;
		it913x_config.tuner_id_0 = reg;
	}

	if (proprietary_ir)
		it913x_select_remote(udev, props);

	if (udev->speed != USB_SPEED_HIGH) {
		props->adapter[0].fe[0].pid_filter_count = 5;
		info("USB 1 low speed mode - connect to USB 2 port");
		if (pid_filter > 0)
			pid_filter = 0;
		if (it913x_config.dual_mode) {
			it913x_config.dual_mode = 0;
			info("Dual mode not supported in USB 1");
		}
	} else /* For replugging */
		if(props->adapter[0].fe[0].pid_filter_count == 5)
			props->adapter[0].fe[0].pid_filter_count = 31;

	/* Select Stream Buffer Size and pid filter option*/
	if (pid_filter) {
		props->adapter[0].fe[0].stream.u.bulk.buffersize =
			TS_BUFFER_SIZE_MAX;
		props->adapter[0].fe[0].caps &=
			~DVB_USB_ADAP_NEED_PID_FILTERING;
	} else
		props->adapter[0].fe[0].stream.u.bulk.buffersize =
			TS_BUFFER_SIZE_PID;

	if (it913x_config.dual_mode) {
		props->adapter[1].fe[0].stream.u.bulk.buffersize =
			props->adapter[0].fe[0].stream.u.bulk.buffersize;
		props->num_adapters = 2;
		if (pid_filter)
			props->adapter[1].fe[0].caps =
				props->adapter[0].fe[0].caps;
	} else
		props->num_adapters = 1;

	info("Dual mode=%x Tuner Type=%x", it913x_config.dual_mode,
		it913x_config.tuner_id_0);

	ret = ite_firmware_select(udev, props);

	return ret;
}

static int it913x_identify_state(struct usb_device *udev,
		struct dvb_usb_device_properties *props,
		struct dvb_usb_device_description **desc,
		int *cold)
{
	int ret = 0, firm_no;
	u8 reg;

	firm_no = it913x_return_status(udev);

	/* Read and select config */
	ret = it913x_select_config(udev, props);
	if (ret < 0)
		return ret;

	if (firm_no > 0) {
		*cold = 0;
		return 0;
	}

	if (it913x_config.dual_mode) {
		it913x_config.tuner_id_1 = it913x_read_reg(udev, 0x49e0);
		ret = it913x_wr_reg(udev, DEV_0, GPIOH1_EN, 0x1);
		ret |= it913x_wr_reg(udev, DEV_0, GPIOH1_ON, 0x1);
		ret |= it913x_wr_reg(udev, DEV_0, GPIOH1_O, 0x1);
		msleep(50);
		ret |= it913x_wr_reg(udev, DEV_0, GPIOH1_O, 0x0);
		msleep(50);
		reg = it913x_read_reg(udev, GPIOH1_O);
		if (reg == 0) {
			ret |= it913x_wr_reg(udev, DEV_0,  GPIOH1_O, 0x1);
			ret |= it913x_return_status(udev);
			if (ret != 0)
				ret = it913x_wr_reg(udev, DEV_0,
					GPIOH1_O, 0x0);
		}
	}

	reg = it913x_read_reg(udev, IO_MUX_POWER_CLK);

	if (it913x_config.dual_mode) {
		ret |= it913x_wr_reg(udev, DEV_0, 0x4bfb, CHIP2_I2C_ADDR);
		if (it913x_config.firmware_ver == 1)
			ret |= it913x_wr_reg(udev, DEV_0,  0xcfff, 0x1);
		else
			ret |= it913x_wr_reg(udev, DEV_0,  CLK_O_EN, 0x1);
	} else {
		ret |= it913x_wr_reg(udev, DEV_0, 0x4bfb, 0x0);
		if (it913x_config.firmware_ver == 1)
			ret |= it913x_wr_reg(udev, DEV_0,  0xcfff, 0x0);
		else
			ret |= it913x_wr_reg(udev, DEV_0,  CLK_O_EN, 0x0);
	}

	*cold = 1;

	return (ret < 0) ? -ENODEV : 0;
}

static int it913x_streaming_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	struct it913x_state *st = adap->dev->priv;
	int ret = 0;
	u8 pro = (adap->id == 0) ? DEV_0_DMOD : DEV_1_DMOD;

	deb_info(1, "STM  (%02x)", onoff);

	if (!onoff) {
		mutex_lock(&adap->dev->i2c_mutex);

		ret = it913x_wr_reg(adap->dev->udev, pro, PID_RST, 0x1);

		mutex_unlock(&adap->dev->i2c_mutex);
		st->pid_filter_onoff =
			adap->fe_adap[adap->active_fe].pid_filtering;

	}

	return ret;
}

static int it913x_download_firmware(struct usb_device *udev,
					const struct firmware *fw)
{
	int ret = 0, i = 0, pos = 0;
	u8 packet_size, min_pkt;
	u8 *fw_data;

	ret = it913x_wr_reg(udev, DEV_0,  I2C_CLK, I2C_CLK_100);

	info("FRM Starting Firmware Download");

	/* Multi firmware loader */
	/* This uses scatter write firmware headers */
	/* The firmware must start with 03 XX 00 */
	/* and be the extact firmware length */

	if (it913x_config.chip_ver == 2)
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
				if (packet_size > 0)
					ret |= it913x_io(udev, WRITE_DATA,
						DEV_0, CMD_SCATTER_WRITE, 0,
						0, fw_data, packet_size);
				udelay(1000);
			}
		}
		i++;
	}

	ret |= it913x_io(udev, WRITE_CMD, DEV_0, CMD_BOOT, 0, 0, NULL, 0);

	msleep(100);

	if (ret < 0)
		info("FRM Firmware Download Failed (%04x)" , ret);
	else
		info("FRM Firmware Download Completed - Resetting Device");

	ret |= it913x_return_status(udev);

	msleep(30);

	ret |= it913x_wr_reg(udev, DEV_0,  I2C_CLK, I2C_CLK_400);

	/* Tuner function */
	if (it913x_config.dual_mode)
		ret |= it913x_wr_reg(udev, DEV_0_DMOD , 0xec4c, 0xa0);
	else
		ret |= it913x_wr_reg(udev, DEV_0_DMOD , 0xec4c, 0x68);

	if ((it913x_config.chip_ver == 1) &&
		(it913x_config.chip_type == 0x9135)) {
		ret |= it913x_wr_reg(udev, DEV_0,  PADODPU, 0x0);
		ret |= it913x_wr_reg(udev, DEV_0,  AGC_O_D, 0x0);
		if (it913x_config.dual_mode) {
			ret |= it913x_wr_reg(udev, DEV_1,  PADODPU, 0x0);
			ret |= it913x_wr_reg(udev, DEV_1,  AGC_O_D, 0x0);
		}
	}

	return (ret < 0) ? -ENODEV : 0;
}

static int it913x_name(struct dvb_usb_adapter *adap)
{
	const char *desc = adap->dev->desc->name;
	char *fe_name[] = {"_1", "_2", "_3", "_4"};
	char *name = adap->fe_adap[0].fe->ops.info.name;

	strlcpy(name, desc, 128);
	strlcat(name, fe_name[adap->id], 128);

	return 0;
}

static int it913x_frontend_attach(struct dvb_usb_adapter *adap)
{
	struct usb_device *udev = adap->dev->udev;
	struct it913x_state *st = adap->dev->priv;
	int ret = 0;
	u8 adap_addr = I2C_BASE_ADDR + (adap->id << 5);
	u16 ep_size = adap->props.fe[0].stream.u.bulk.buffersize / 4;
	u8 pkt_size = 0x80;

	if (adap->dev->udev->speed != USB_SPEED_HIGH)
		pkt_size = 0x10;

	it913x_config.adf = it913x_read_reg(udev, IO_MUX_POWER_CLK);

	if (adap->id == 0)
		memcpy(&st->it913x_config, &it913x_config,
			sizeof(struct ite_config));

	adap->fe_adap[0].fe = dvb_attach(it913x_fe_attach,
		&adap->dev->i2c_adap, adap_addr, &st->it913x_config);

	if (adap->id == 0 && adap->fe_adap[0].fe) {
		ret = it913x_wr_reg(udev, DEV_0_DMOD, MP2_SW_RST, 0x1);
		ret = it913x_wr_reg(udev, DEV_0_DMOD, MP2IF2_SW_RST, 0x1);
		ret = it913x_wr_reg(udev, DEV_0, EP0_TX_EN, 0x0f);
		ret = it913x_wr_reg(udev, DEV_0, EP0_TX_NAK, 0x1b);
		ret = it913x_wr_reg(udev, DEV_0, EP0_TX_EN, 0x2f);
		ret = it913x_wr_reg(udev, DEV_0, EP4_TX_LEN_LSB,
					ep_size & 0xff);
		ret = it913x_wr_reg(udev, DEV_0, EP4_TX_LEN_MSB, ep_size >> 8);
		ret = it913x_wr_reg(udev, DEV_0, EP4_MAX_PKT, pkt_size);
	} else if (adap->id == 1 && adap->fe_adap[0].fe) {
		ret = it913x_wr_reg(udev, DEV_0, EP0_TX_EN, 0x6f);
		ret = it913x_wr_reg(udev, DEV_0, EP5_TX_LEN_LSB,
					ep_size & 0xff);
		ret = it913x_wr_reg(udev, DEV_0, EP5_TX_LEN_MSB, ep_size >> 8);
		ret = it913x_wr_reg(udev, DEV_0, EP5_MAX_PKT, pkt_size);
		ret = it913x_wr_reg(udev, DEV_0_DMOD, MP2IF2_EN, 0x1);
		ret = it913x_wr_reg(udev, DEV_1_DMOD, MP2IF_SERIAL, 0x1);
		ret = it913x_wr_reg(udev, DEV_1, TOP_HOSTB_SER_MODE, 0x1);
		ret = it913x_wr_reg(udev, DEV_0_DMOD, TSIS_ENABLE, 0x1);
		ret = it913x_wr_reg(udev, DEV_0_DMOD, MP2_SW_RST, 0x0);
		ret = it913x_wr_reg(udev, DEV_0_DMOD, MP2IF2_SW_RST, 0x0);
		ret = it913x_wr_reg(udev, DEV_0_DMOD, MP2IF2_HALF_PSB, 0x0);
		ret = it913x_wr_reg(udev, DEV_0_DMOD, MP2IF_STOP_EN, 0x1);
		ret = it913x_wr_reg(udev, DEV_1_DMOD, MPEG_FULL_SPEED, 0x0);
		ret = it913x_wr_reg(udev, DEV_1_DMOD, MP2IF_STOP_EN, 0x0);
	} else
		return -ENODEV;

	ret = it913x_name(adap);

	return ret;
}

/* DVB USB Driver */
static struct dvb_usb_device_properties it913x_properties;

static int it913x_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	cmd_counter = 0;
	if (0 == dvb_usb_device_init(intf, &it913x_properties,
				     THIS_MODULE, NULL, adapter_nr)) {
		info("DEV registering device driver");
		return 0;
	}

	info("DEV it913x Error");
	return -ENODEV;

}

static struct usb_device_id it913x_table[] = {
	{ USB_DEVICE(USB_VID_KWORLD_2, USB_PID_KWORLD_UB499_2T_T09) },
	{ USB_DEVICE(USB_VID_ITETECH, USB_PID_ITETECH_IT9135) },
	{ USB_DEVICE(USB_VID_KWORLD_2, USB_PID_SVEON_STV22_IT9137) },
	{ USB_DEVICE(USB_VID_ITETECH, USB_PID_ITETECH_IT9135_9005) },
	{ USB_DEVICE(USB_VID_ITETECH, USB_PID_ITETECH_IT9135_9006) },
	{}		/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, it913x_table);

static struct dvb_usb_device_properties it913x_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,
	.usb_ctrl = DEVICE_SPECIFIC,
	.download_firmware = it913x_download_firmware,
	.firmware = "dvb-usb-it9137-01.fw",
	.no_reconnect = 1,
	.size_of_priv = sizeof(struct it913x_state),
	.num_adapters = 2,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.caps = DVB_USB_ADAP_HAS_PID_FILTER|
				DVB_USB_ADAP_NEED_PID_FILTERING|
				DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
			.streaming_ctrl   = it913x_streaming_ctrl,
			.pid_filter_count = 31,
			.pid_filter = it913x_pid_filter,
			.pid_filter_ctrl  = it913x_pid_filter_ctrl,
			.frontend_attach  = it913x_frontend_attach,
			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_BULK,
				.count = 10,
				.endpoint = 0x04,
				.u = {/* Keep Low if PID filter on */
					.bulk = {
					.buffersize =
						TS_BUFFER_SIZE_PID,
					}
				}
			}
		}},
		},
			{
		.num_frontends = 1,
		.fe = {{
			.caps = DVB_USB_ADAP_HAS_PID_FILTER|
				DVB_USB_ADAP_NEED_PID_FILTERING|
				DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
			.streaming_ctrl   = it913x_streaming_ctrl,
			.pid_filter_count = 31,
			.pid_filter = it913x_pid_filter,
			.pid_filter_ctrl  = it913x_pid_filter_ctrl,
			.frontend_attach  = it913x_frontend_attach,
			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_BULK,
				.count = 5,
				.endpoint = 0x05,
				.u = {
					.bulk = {
						.buffersize =
							TS_BUFFER_SIZE_PID,
					}
				}
			}
		}},
		}
	},
	.identify_state   = it913x_identify_state,
	.rc.core = {
		.protocol	= RC_TYPE_NEC,
		.module_name	= "it913x",
		.rc_query	= it913x_rc_query,
		.rc_interval	= IT913X_POLL,
		.allowed_protos	= RC_TYPE_NEC,
		.rc_codes	= RC_MAP_IT913X_V1,
	},
	.i2c_algo         = &it913x_i2c_algo,
	.num_device_descs = 5,
	.devices = {
		{   "Kworld UB499-2T T09(IT9137)",
			{ &it913x_table[0], NULL },
			},
		{   "ITE 9135 Generic",
			{ &it913x_table[1], NULL },
			},
		{   "Sveon STV22 Dual DVB-T HDTV(IT9137)",
			{ &it913x_table[2], NULL },
			},
		{   "ITE 9135(9005) Generic",
			{ &it913x_table[3], NULL },
			},
		{   "ITE 9135(9006) Generic",
			{ &it913x_table[4], NULL },
			},
	}
};

static struct usb_driver it913x_driver = {
	.name		= "it913x",
	.probe		= it913x_probe,
	.disconnect	= dvb_usb_device_exit,
	.id_table	= it913x_table,
};

module_usb_driver(it913x_driver);

MODULE_AUTHOR("Malcolm Priestley <tvboxspy@gmail.com>");
MODULE_DESCRIPTION("it913x USB 2 Driver");
MODULE_VERSION("1.27");
MODULE_LICENSE("GPL");
