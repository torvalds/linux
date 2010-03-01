/* Linux driver for devices based on the DiBcom DiB0700 USB bridge
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the Free
 *	Software Foundation, version 2.
 *
 *  Copyright (C) 2005-6 DiBcom, SA
 */
#include "dib0700.h"

/* debug */
int dvb_usb_dib0700_debug;
module_param_named(debug,dvb_usb_dib0700_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info,2=fw,4=fwdata,8=data (or-able))." DVB_USB_DEBUG_STATUS);

int dvb_usb_dib0700_ir_proto = 1;
module_param(dvb_usb_dib0700_ir_proto, int, 0644);
MODULE_PARM_DESC(dvb_usb_dib0700_ir_proto, "set ir protocol (0=NEC, 1=RC5 (default), 2=RC6).");

static int nb_packet_buffer_size = 21;
module_param(nb_packet_buffer_size, int, 0644);
MODULE_PARM_DESC(nb_packet_buffer_size,
	"Set the dib0700 driver data buffer size. This parameter "
	"corresponds to the number of TS packets. The actual size of "
	"the data buffer corresponds to this parameter "
	"multiplied by 188 (default: 21)");

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);


int dib0700_get_version(struct dvb_usb_device *d, u32 *hwversion,
			u32 *romversion, u32 *ramversion, u32 *fwtype)
{
	u8 b[16];
	int ret = usb_control_msg(d->udev, usb_rcvctrlpipe(d->udev, 0),
				  REQUEST_GET_VERSION,
				  USB_TYPE_VENDOR | USB_DIR_IN, 0, 0,
				  b, sizeof(b), USB_CTRL_GET_TIMEOUT);
	if (hwversion != NULL)
		*hwversion  = (b[0] << 24)  | (b[1] << 16)  | (b[2] << 8)  | b[3];
	if (romversion != NULL)
		*romversion = (b[4] << 24)  | (b[5] << 16)  | (b[6] << 8)  | b[7];
	if (ramversion != NULL)
		*ramversion = (b[8] << 24)  | (b[9] << 16)  | (b[10] << 8) | b[11];
	if (fwtype != NULL)
		*fwtype     = (b[12] << 24) | (b[13] << 16) | (b[14] << 8) | b[15];
	return ret;
}

/* expecting rx buffer: request data[0] data[1] ... data[2] */
static int dib0700_ctrl_wr(struct dvb_usb_device *d, u8 *tx, u8 txlen)
{
	int status;

	deb_data(">>> ");
	debug_dump(tx,txlen,deb_data);

	status = usb_control_msg(d->udev, usb_sndctrlpipe(d->udev,0),
		tx[0], USB_TYPE_VENDOR | USB_DIR_OUT, 0, 0, tx, txlen,
		USB_CTRL_GET_TIMEOUT);

	if (status != txlen)
		deb_data("ep 0 write error (status = %d, len: %d)\n",status,txlen);

	return status < 0 ? status : 0;
}

/* expecting tx buffer: request data[0] ... data[n] (n <= 4) */
int dib0700_ctrl_rd(struct dvb_usb_device *d, u8 *tx, u8 txlen, u8 *rx, u8 rxlen)
{
	u16 index, value;
	int status;

	if (txlen < 2) {
		err("tx buffer length is smaller than 2. Makes no sense.");
		return -EINVAL;
	}
	if (txlen > 4) {
		err("tx buffer length is larger than 4. Not supported.");
		return -EINVAL;
	}

	deb_data(">>> ");
	debug_dump(tx,txlen,deb_data);

	value = ((txlen - 2) << 8) | tx[1];
	index = 0;
	if (txlen > 2)
		index |= (tx[2] << 8);
	if (txlen > 3)
		index |= tx[3];

	status = usb_control_msg(d->udev, usb_rcvctrlpipe(d->udev,0), tx[0],
			USB_TYPE_VENDOR | USB_DIR_IN, value, index, rx, rxlen,
			USB_CTRL_GET_TIMEOUT);

	if (status < 0)
		deb_info("ep 0 read error (status = %d)\n",status);

	deb_data("<<< ");
	debug_dump(rx,rxlen,deb_data);

	return status; /* length in case of success */
}

int dib0700_set_gpio(struct dvb_usb_device *d, enum dib07x0_gpios gpio, u8 gpio_dir, u8 gpio_val)
{
	u8 buf[3] = { REQUEST_SET_GPIO, gpio, ((gpio_dir & 0x01) << 7) | ((gpio_val & 0x01) << 6) };
	return dib0700_ctrl_wr(d,buf,3);
}

static int dib0700_set_usb_xfer_len(struct dvb_usb_device *d, u16 nb_ts_packets)
{
    struct dib0700_state *st = d->priv;
    u8 b[3];
    int ret;

    if (st->fw_version >= 0x10201) {
	b[0] = REQUEST_SET_USB_XFER_LEN;
	b[1] = (nb_ts_packets >> 8)&0xff;
	b[2] = nb_ts_packets & 0xff;

	deb_info("set the USB xfer len to %i Ts packet\n", nb_ts_packets);

	ret = dib0700_ctrl_wr(d, b, 3);
    } else {
	deb_info("this firmware does not allow to change the USB xfer len\n");
	ret = -EIO;
    }
    return ret;
}

/*
 * I2C master xfer function (supported in 1.20 firmware)
 */
static int dib0700_i2c_xfer_new(struct i2c_adapter *adap, struct i2c_msg *msg,
				int num)
{
	/* The new i2c firmware messages are more reliable and in particular
	   properly support i2c read calls not preceded by a write */

	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	uint8_t bus_mode = 1;  /* 0=eeprom bus, 1=frontend bus */
	uint8_t gen_mode = 0; /* 0=master i2c, 1=gpio i2c */
	uint8_t en_start = 0;
	uint8_t en_stop = 0;
	uint8_t buf[255]; /* TBV: malloc ? */
	int result, i;

	/* Ensure nobody else hits the i2c bus while we're sending our
	   sequence of messages, (such as the remote control thread) */
	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	for (i = 0; i < num; i++) {
		if (i == 0) {
			/* First message in the transaction */
			en_start = 1;
		} else if (!(msg[i].flags & I2C_M_NOSTART)) {
			/* Device supports repeated-start */
			en_start = 1;
		} else {
			/* Not the first packet and device doesn't support
			   repeated start */
			en_start = 0;
		}
		if (i == (num - 1)) {
			/* Last message in the transaction */
			en_stop = 1;
		}

		if (msg[i].flags & I2C_M_RD) {
			/* Read request */
			u16 index, value;
			uint8_t i2c_dest;

			i2c_dest = (msg[i].addr << 1);
			value = ((en_start << 7) | (en_stop << 6) |
				 (msg[i].len & 0x3F)) << 8 | i2c_dest;
			/* I2C ctrl + FE bus; */
			index = ((gen_mode<<6)&0xC0) | ((bus_mode<<4)&0x30);

			result = usb_control_msg(d->udev,
						 usb_rcvctrlpipe(d->udev, 0),
						 REQUEST_NEW_I2C_READ,
						 USB_TYPE_VENDOR | USB_DIR_IN,
						 value, index, msg[i].buf,
						 msg[i].len,
						 USB_CTRL_GET_TIMEOUT);
			if (result < 0) {
				err("i2c read error (status = %d)\n", result);
				break;
			}

			deb_data("<<< ");
			debug_dump(msg[i].buf, msg[i].len, deb_data);

		} else {
			/* Write request */
			buf[0] = REQUEST_NEW_I2C_WRITE;
			buf[1] = (msg[i].addr << 1);
			buf[2] = (en_start << 7) | (en_stop << 6) |
				(msg[i].len & 0x3F);
			/* I2C ctrl + FE bus; */
			buf[3] = ((gen_mode<<6)&0xC0) | ((bus_mode<<4)&0x30);
			/* The Actual i2c payload */
			memcpy(&buf[4], msg[i].buf, msg[i].len);

			deb_data(">>> ");
			debug_dump(buf, msg[i].len + 4, deb_data);

			result = usb_control_msg(d->udev,
						 usb_sndctrlpipe(d->udev, 0),
						 REQUEST_NEW_I2C_WRITE,
						 USB_TYPE_VENDOR | USB_DIR_OUT,
						 0, 0, buf, msg[i].len + 4,
						 USB_CTRL_GET_TIMEOUT);
			if (result < 0) {
				err("i2c write error (status = %d)\n", result);
				break;
			}
		}
	}
	mutex_unlock(&d->i2c_mutex);
	return i;
}

/*
 * I2C master xfer function (pre-1.20 firmware)
 */
static int dib0700_i2c_xfer_legacy(struct i2c_adapter *adap,
				   struct i2c_msg *msg, int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	int i,len;
	u8 buf[255];

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	for (i = 0; i < num; i++) {
		/* fill in the address */
		buf[1] = (msg[i].addr << 1);
		/* fill the buffer */
		memcpy(&buf[2], msg[i].buf, msg[i].len);

		/* write/read request */
		if (i+1 < num && (msg[i+1].flags & I2C_M_RD)) {
			buf[0] = REQUEST_I2C_READ;
			buf[1] |= 1;

			/* special thing in the current firmware: when length is zero the read-failed */
			if ((len = dib0700_ctrl_rd(d, buf, msg[i].len + 2, msg[i+1].buf, msg[i+1].len)) <= 0) {
				deb_info("I2C read failed on address 0x%02x\n",
					 msg[i].addr);
				break;
			}

			msg[i+1].len = len;

			i++;
		} else {
			buf[0] = REQUEST_I2C_WRITE;
			if (dib0700_ctrl_wr(d, buf, msg[i].len + 2) < 0)
				break;
		}
	}

	mutex_unlock(&d->i2c_mutex);
	return i;
}

static int dib0700_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msg,
			    int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	struct dib0700_state *st = d->priv;

	if (st->fw_use_new_i2c_api == 1) {
		/* User running at least fw 1.20 */
		return dib0700_i2c_xfer_new(adap, msg, num);
	} else {
		/* Use legacy calls */
		return dib0700_i2c_xfer_legacy(adap, msg, num);
	}
}

static u32 dib0700_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

struct i2c_algorithm dib0700_i2c_algo = {
	.master_xfer   = dib0700_i2c_xfer,
	.functionality = dib0700_i2c_func,
};

int dib0700_identify_state(struct usb_device *udev, struct dvb_usb_device_properties *props,
			struct dvb_usb_device_description **desc, int *cold)
{
	u8 b[16];
	s16 ret = usb_control_msg(udev, usb_rcvctrlpipe(udev,0),
		REQUEST_GET_VERSION, USB_TYPE_VENDOR | USB_DIR_IN, 0, 0, b, 16, USB_CTRL_GET_TIMEOUT);

	deb_info("FW GET_VERSION length: %d\n",ret);

	*cold = ret <= 0;

	deb_info("cold: %d\n", *cold);
	return 0;
}

static int dib0700_set_clock(struct dvb_usb_device *d, u8 en_pll,
	u8 pll_src, u8 pll_range, u8 clock_gpio3, u16 pll_prediv,
	u16 pll_loopdiv, u16 free_div, u16 dsuScaler)
{
	u8 b[10];
	b[0] = REQUEST_SET_CLOCK;
	b[1] = (en_pll << 7) | (pll_src << 6) | (pll_range << 5) | (clock_gpio3 << 4);
	b[2] = (pll_prediv >> 8)  & 0xff; // MSB
	b[3] =  pll_prediv        & 0xff; // LSB
	b[4] = (pll_loopdiv >> 8) & 0xff; // MSB
	b[5] =  pll_loopdiv       & 0xff; // LSB
	b[6] = (free_div >> 8)    & 0xff; // MSB
	b[7] =  free_div          & 0xff; // LSB
	b[8] = (dsuScaler >> 8)   & 0xff; // MSB
	b[9] =  dsuScaler         & 0xff; // LSB

	return dib0700_ctrl_wr(d, b, 10);
}

int dib0700_ctrl_clock(struct dvb_usb_device *d, u32 clk_MHz, u8 clock_out_gp3)
{
	switch (clk_MHz) {
		case 72: dib0700_set_clock(d, 1, 0, 1, clock_out_gp3, 2, 24, 0, 0x4c); break;
		default: return -EINVAL;
	}
	return 0;
}

static int dib0700_jumpram(struct usb_device *udev, u32 address)
{
	int ret, actlen;
	u8 buf[8] = { REQUEST_JUMPRAM, 0, 0, 0,
		(address >> 24) & 0xff,
		(address >> 16) & 0xff,
		(address >> 8)  & 0xff,
		 address        & 0xff };

	if ((ret = usb_bulk_msg(udev, usb_sndbulkpipe(udev, 0x01),buf,8,&actlen,1000)) < 0) {
		deb_fw("jumpram to 0x%x failed\n",address);
		return ret;
	}
	if (actlen != 8) {
		deb_fw("jumpram to 0x%x failed\n",address);
		return -EIO;
	}
	return 0;
}

int dib0700_download_firmware(struct usb_device *udev, const struct firmware *fw)
{
	struct hexline hx;
	int pos = 0, ret, act_len, i, adap_num;
	u8 b[16];
	u32 fw_version;

	u8 buf[260];

	while ((ret = dvb_usb_get_hexline(fw, &hx, &pos)) > 0) {
		deb_fwdata("writing to address 0x%08x (buffer: 0x%02x %02x)\n",hx.addr, hx.len, hx.chk);

		buf[0] = hx.len;
		buf[1] = (hx.addr >> 8) & 0xff;
		buf[2] =  hx.addr       & 0xff;
		buf[3] = hx.type;
		memcpy(&buf[4],hx.data,hx.len);
		buf[4+hx.len] = hx.chk;

		ret = usb_bulk_msg(udev,
			usb_sndbulkpipe(udev, 0x01),
			buf,
			hx.len + 5,
			&act_len,
			1000);

		if (ret < 0) {
			err("firmware download failed at %d with %d",pos,ret);
			return ret;
		}
	}

	if (ret == 0) {
		/* start the firmware */
		if ((ret = dib0700_jumpram(udev, 0x70000000)) == 0) {
			info("firmware started successfully.");
			msleep(500);
		}
	} else
		ret = -EIO;

	/* the number of ts packet has to be at least 1 */
	if (nb_packet_buffer_size < 1)
		nb_packet_buffer_size = 1;

	/* get the fimware version */
	usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
				  REQUEST_GET_VERSION,
				  USB_TYPE_VENDOR | USB_DIR_IN, 0, 0,
				  b, sizeof(b), USB_CTRL_GET_TIMEOUT);
	fw_version = (b[8] << 24)  | (b[9] << 16)  | (b[10] << 8) | b[11];

	/* set the buffer size - DVB-USB is allocating URB buffers
	 * only after the firwmare download was successful */
	for (i = 0; i < dib0700_device_count; i++) {
		for (adap_num = 0; adap_num < dib0700_devices[i].num_adapters;
				adap_num++) {
			if (fw_version >= 0x10201)
				dib0700_devices[i].adapter[adap_num].stream.u.bulk.buffersize = 188*nb_packet_buffer_size;
			else {
				/* for fw version older than 1.20.1,
				 * the buffersize has to be n times 512 */
				dib0700_devices[i].adapter[adap_num].stream.u.bulk.buffersize = ((188*nb_packet_buffer_size+188/2)/512)*512;
				if (dib0700_devices[i].adapter[adap_num].stream.u.bulk.buffersize < 512)
					dib0700_devices[i].adapter[adap_num].stream.u.bulk.buffersize = 512;
			}
		}
	}

	return ret;
}

int dib0700_streaming_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	struct dib0700_state *st = adap->dev->priv;
	u8 b[4];
	int ret;

	if ((onoff != 0) && (st->fw_version >= 0x10201)) {
		/* for firmware later than 1.20.1,
		 * the USB xfer length can be set  */
		ret = dib0700_set_usb_xfer_len(adap->dev,
			st->nb_packet_buffer_size);
		if (ret < 0) {
			deb_info("can not set the USB xfer len\n");
			return ret;
		}
	}

	b[0] = REQUEST_ENABLE_VIDEO;
	b[1] = (onoff << 4) | 0x00; /* this bit gives a kind of command, rather than enabling something or not */

	if (st->disable_streaming_master_mode == 1)
		b[2] = 0x00;
	else
		b[2] = (0x01 << 4); /* Master mode */

	b[3] = 0x00;

	deb_info("modifying (%d) streaming state for %d\n", onoff, adap->id);

	if (onoff)
		st->channel_state |=   1 << adap->id;
	else
		st->channel_state &= ~(1 << adap->id);

	b[2] |= st->channel_state;

	deb_info("data for streaming: %x %x\n",b[1],b[2]);

	return dib0700_ctrl_wr(adap->dev, b, 4);
}

int dib0700_rc_setup(struct dvb_usb_device *d)
{
	u8 rc_setup[3] = {REQUEST_SET_RC, dvb_usb_dib0700_ir_proto, 0};
	int i = dib0700_ctrl_wr(d, rc_setup, 3);
	if (i<0) {
		err("ir protocol setup failed");
		return -1;
	}
	return 0;
}

static int dib0700_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	int i;
	struct dvb_usb_device *dev;

	for (i = 0; i < dib0700_device_count; i++)
		if (dvb_usb_device_init(intf, &dib0700_devices[i], THIS_MODULE,
		    &dev, adapter_nr) == 0) {
			struct dib0700_state *st = dev->priv;
			u32 hwversion, romversion, fw_version, fwtype;

			dib0700_get_version(dev, &hwversion, &romversion,
				&fw_version, &fwtype);

			deb_info("Firmware version: %x, %d, 0x%x, %d\n",
				hwversion, romversion, fw_version, fwtype);

			st->fw_version = fw_version;
			st->nb_packet_buffer_size = (u32)nb_packet_buffer_size;

			dib0700_rc_setup(dev);

			return 0;
		}

	return -ENODEV;
}

static struct usb_driver dib0700_driver = {
	.name       = "dvb_usb_dib0700",
	.probe      = dib0700_probe,
	.disconnect = dvb_usb_device_exit,
	.id_table   = dib0700_usb_id_table,
};

/* module stuff */
static int __init dib0700_module_init(void)
{
	int result;
	info("loaded with support for %d different device-types", dib0700_device_count);
	if ((result = usb_register(&dib0700_driver))) {
		err("usb_register failed. Error number %d",result);
		return result;
	}

	return 0;
}

static void __exit dib0700_module_exit(void)
{
	/* deregister this driver from the USB subsystem */
	usb_deregister(&dib0700_driver);
}

module_init (dib0700_module_init);
module_exit (dib0700_module_exit);

MODULE_AUTHOR("Patrick Boettcher <pboettcher@dibcom.fr>");
MODULE_DESCRIPTION("Driver for devices based on DiBcom DiB0700 - USB bridge");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
