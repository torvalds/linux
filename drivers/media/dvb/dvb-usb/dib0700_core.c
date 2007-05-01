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
static int dib0700_ctrl_rd(struct dvb_usb_device *d, u8 *tx, u8 txlen, u8 *rx, u8 rxlen)
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

/*
 * I2C master xfer function
 */
static int dib0700_i2c_xfer(struct i2c_adapter *adap,struct i2c_msg *msg,int num)
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
				deb_info("I2C read failed on address %x\n", msg[i].addr);
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
	int pos = 0, ret, act_len;

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

	return ret;
}

int dib0700_streaming_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	struct dib0700_state *st = adap->dev->priv;
	u8 b[4];

	b[0] = REQUEST_ENABLE_VIDEO;
	b[1] = 0x00;
	b[2] = (0x01 << 4); /* Master mode */
	b[3] = 0x00;

	deb_info("modifying (%d) streaming state for %d\n", onoff, adap->id);

	if (onoff)
		st->channel_state |=   1 << adap->id;
	else
		st->channel_state &= ~(1 << adap->id);

	b[2] |= st->channel_state;

	if (st->channel_state) /* if at least one channel is active */
		b[1] = (0x01 << 4) | 0x00;

	deb_info("data for streaming: %x %x\n",b[1],b[2]);

	return dib0700_ctrl_wr(adap->dev, b, 4);
}

static int dib0700_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	int i;

	for (i = 0; i < dib0700_device_count; i++)
		if (dvb_usb_device_init(intf, &dib0700_devices[i], THIS_MODULE, NULL) == 0)
			return 0;

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
