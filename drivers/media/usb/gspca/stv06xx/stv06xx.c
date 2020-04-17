// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2001 Jean-Fredric Clere, Nikolas Zimmermann, Georg Acher
 *		      Mark Cave-Ayland, Carlo E Prelz, Dick Streefland
 * Copyright (c) 2002, 2003 Tuukka Toivonen
 * Copyright (c) 2008 Erik Andrén
 *
 * P/N 861037:      Sensor HDCS1000        ASIC STV0600
 * P/N 861050-0010: Sensor HDCS1000        ASIC STV0600
 * P/N 861050-0020: Sensor Photobit PB100  ASIC STV0600-1 - QuickCam Express
 * P/N 861055:      Sensor ST VV6410       ASIC STV0610   - LEGO cam
 * P/N 861075-0040: Sensor HDCS1000        ASIC
 * P/N 961179-0700: Sensor ST VV6410       ASIC STV0602   - Dexxa WebCam USB
 * P/N 861040-0000: Sensor ST VV6410       ASIC STV0610   - QuickCam Web
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/input.h>
#include "stv06xx_sensor.h"

MODULE_AUTHOR("Erik Andrén");
MODULE_DESCRIPTION("STV06XX USB Camera Driver");
MODULE_LICENSE("GPL");

static bool dump_bridge;
static bool dump_sensor;

int stv06xx_write_bridge(struct sd *sd, u16 address, u16 i2c_data)
{
	int err;
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	struct usb_device *udev = sd->gspca_dev.dev;
	__u8 *buf = sd->gspca_dev.usb_buf;

	u8 len = (i2c_data > 0xff) ? 2 : 1;

	buf[0] = i2c_data & 0xff;
	buf[1] = (i2c_data >> 8) & 0xff;

	err = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			      0x04, 0x40, address, 0, buf, len,
			      STV06XX_URB_MSG_TIMEOUT);

	gspca_dbg(gspca_dev, D_CONF, "Written 0x%x to address 0x%x, status: %d\n",
		  i2c_data, address, err);

	return (err < 0) ? err : 0;
}

int stv06xx_read_bridge(struct sd *sd, u16 address, u8 *i2c_data)
{
	int err;
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	struct usb_device *udev = sd->gspca_dev.dev;
	__u8 *buf = sd->gspca_dev.usb_buf;

	err = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
			      0x04, 0xc0, address, 0, buf, 1,
			      STV06XX_URB_MSG_TIMEOUT);

	*i2c_data = buf[0];

	gspca_dbg(gspca_dev, D_CONF, "Reading 0x%x from address 0x%x, status %d\n",
		  *i2c_data, address, err);

	return (err < 0) ? err : 0;
}

/* Wraps the normal write sensor bytes / words functions for writing a
   single value */
int stv06xx_write_sensor(struct sd *sd, u8 address, u16 value)
{
	if (sd->sensor->i2c_len == 2) {
		u16 data[2] = { address, value };
		return stv06xx_write_sensor_words(sd, data, 1);
	} else {
		u8 data[2] = { address, value };
		return stv06xx_write_sensor_bytes(sd, data, 1);
	}
}

static int stv06xx_write_sensor_finish(struct sd *sd)
{
	int err = 0;

	if (sd->bridge == BRIDGE_STV610) {
		struct usb_device *udev = sd->gspca_dev.dev;
		__u8 *buf = sd->gspca_dev.usb_buf;

		buf[0] = 0;
		err = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				      0x04, 0x40, 0x1704, 0, buf, 1,
				      STV06XX_URB_MSG_TIMEOUT);
	}

	return (err < 0) ? err : 0;
}

int stv06xx_write_sensor_bytes(struct sd *sd, const u8 *data, u8 len)
{
	int err, i, j;
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	struct usb_device *udev = sd->gspca_dev.dev;
	__u8 *buf = sd->gspca_dev.usb_buf;

	gspca_dbg(gspca_dev, D_CONF, "I2C: Command buffer contains %d entries\n",
		  len);
	for (i = 0; i < len;) {
		/* Build the command buffer */
		memset(buf, 0, I2C_BUFFER_LENGTH);
		for (j = 0; j < I2C_MAX_BYTES && i < len; j++, i++) {
			buf[j] = data[2*i];
			buf[0x10 + j] = data[2*i+1];
			gspca_dbg(gspca_dev, D_CONF, "I2C: Writing 0x%02x to reg 0x%02x\n",
				  data[2*i+1], data[2*i]);
		}
		buf[0x20] = sd->sensor->i2c_addr;
		buf[0x21] = j - 1; /* Number of commands to send - 1 */
		buf[0x22] = I2C_WRITE_CMD;
		err = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				      0x04, 0x40, 0x0400, 0, buf,
				      I2C_BUFFER_LENGTH,
				      STV06XX_URB_MSG_TIMEOUT);
		if (err < 0)
			return err;
	}
	return stv06xx_write_sensor_finish(sd);
}

int stv06xx_write_sensor_words(struct sd *sd, const u16 *data, u8 len)
{
	int err, i, j;
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	struct usb_device *udev = sd->gspca_dev.dev;
	__u8 *buf = sd->gspca_dev.usb_buf;

	gspca_dbg(gspca_dev, D_CONF, "I2C: Command buffer contains %d entries\n",
		  len);

	for (i = 0; i < len;) {
		/* Build the command buffer */
		memset(buf, 0, I2C_BUFFER_LENGTH);
		for (j = 0; j < I2C_MAX_WORDS && i < len; j++, i++) {
			buf[j] = data[2*i];
			buf[0x10 + j * 2] = data[2*i+1];
			buf[0x10 + j * 2 + 1] = data[2*i+1] >> 8;
			gspca_dbg(gspca_dev, D_CONF, "I2C: Writing 0x%04x to reg 0x%02x\n",
				  data[2*i+1], data[2*i]);
		}
		buf[0x20] = sd->sensor->i2c_addr;
		buf[0x21] = j - 1; /* Number of commands to send - 1 */
		buf[0x22] = I2C_WRITE_CMD;
		err = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				0x04, 0x40, 0x0400, 0, buf,
				I2C_BUFFER_LENGTH,
				STV06XX_URB_MSG_TIMEOUT);
		if (err < 0)
			return err;
	}
	return stv06xx_write_sensor_finish(sd);
}

int stv06xx_read_sensor(struct sd *sd, const u8 address, u16 *value)
{
	int err;
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	struct usb_device *udev = sd->gspca_dev.dev;
	__u8 *buf = sd->gspca_dev.usb_buf;

	err = stv06xx_write_bridge(sd, STV_I2C_FLUSH, sd->sensor->i2c_flush);
	if (err < 0)
		return err;

	/* Clear mem */
	memset(buf, 0, I2C_BUFFER_LENGTH);

	buf[0] = address;
	buf[0x20] = sd->sensor->i2c_addr;
	buf[0x21] = 0;

	/* Read I2C register */
	buf[0x22] = I2C_READ_CMD;

	err = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			      0x04, 0x40, 0x1400, 0, buf, I2C_BUFFER_LENGTH,
			      STV06XX_URB_MSG_TIMEOUT);
	if (err < 0) {
		pr_err("I2C: Read error writing address: %d\n", err);
		return err;
	}

	err = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
			      0x04, 0xc0, 0x1410, 0, buf, sd->sensor->i2c_len,
			      STV06XX_URB_MSG_TIMEOUT);
	if (sd->sensor->i2c_len == 2)
		*value = buf[0] | (buf[1] << 8);
	else
		*value = buf[0];

	gspca_dbg(gspca_dev, D_CONF, "I2C: Read 0x%x from address 0x%x, status: %d\n",
		  *value, address, err);

	return (err < 0) ? err : 0;
}

/* Dumps all bridge registers */
static void stv06xx_dump_bridge(struct sd *sd)
{
	int i;
	u8 data, buf;

	pr_info("Dumping all stv06xx bridge registers\n");
	for (i = 0x1400; i < 0x160f; i++) {
		stv06xx_read_bridge(sd, i, &data);

		pr_info("Read 0x%x from address 0x%x\n", data, i);
	}

	pr_info("Testing stv06xx bridge registers for writability\n");
	for (i = 0x1400; i < 0x160f; i++) {
		stv06xx_read_bridge(sd, i, &data);
		buf = data;

		stv06xx_write_bridge(sd, i, 0xff);
		stv06xx_read_bridge(sd, i, &data);
		if (data == 0xff)
			pr_info("Register 0x%x is read/write\n", i);
		else if (data != buf)
			pr_info("Register 0x%x is read/write, but only partially\n",
				i);
		else
			pr_info("Register 0x%x is read-only\n", i);

		stv06xx_write_bridge(sd, i, buf);
	}
}

/* this function is called at probe and resume time */
static int stv06xx_init(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int err;

	gspca_dbg(gspca_dev, D_PROBE, "Initializing camera\n");

	/* Let the usb init settle for a bit
	   before performing the initialization */
	msleep(250);

	err = sd->sensor->init(sd);

	if (dump_sensor && sd->sensor->dump)
		sd->sensor->dump(sd);

	return (err < 0) ? err : 0;
}

/* this function is called at probe time */
static int stv06xx_init_controls(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	gspca_dbg(gspca_dev, D_PROBE, "Initializing controls\n");

	gspca_dev->vdev.ctrl_handler = &gspca_dev->ctrl_handler;
	return sd->sensor->init_controls(sd);
}

/* Start the camera */
static int stv06xx_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct usb_host_interface *alt;
	struct usb_interface *intf;
	int err, packet_size;

	intf = usb_ifnum_to_if(sd->gspca_dev.dev, sd->gspca_dev.iface);
	alt = usb_altnum_to_altsetting(intf, sd->gspca_dev.alt);
	if (!alt) {
		gspca_err(gspca_dev, "Couldn't get altsetting\n");
		return -EIO;
	}

	if (alt->desc.bNumEndpoints < 1)
		return -ENODEV;

	packet_size = le16_to_cpu(alt->endpoint[0].desc.wMaxPacketSize);
	err = stv06xx_write_bridge(sd, STV_ISO_SIZE_L, packet_size);
	if (err < 0)
		return err;

	/* Prepare the sensor for start */
	err = sd->sensor->start(sd);
	if (err < 0)
		goto out;

	/* Start isochronous streaming */
	err = stv06xx_write_bridge(sd, STV_ISO_ENABLE, 1);

out:
	if (err < 0)
		gspca_dbg(gspca_dev, D_STREAM, "Starting stream failed\n");
	else
		gspca_dbg(gspca_dev, D_STREAM, "Started streaming\n");

	return (err < 0) ? err : 0;
}

static int stv06xx_isoc_init(struct gspca_dev *gspca_dev)
{
	struct usb_interface_cache *intfc;
	struct usb_host_interface *alt;
	struct sd *sd = (struct sd *) gspca_dev;

	intfc = gspca_dev->dev->actconfig->intf_cache[0];

	if (intfc->num_altsetting < 2)
		return -ENODEV;

	alt = &intfc->altsetting[1];

	if (alt->desc.bNumEndpoints < 1)
		return -ENODEV;

	/* Start isoc bandwidth "negotiation" at max isoc bandwidth */
	alt->endpoint[0].desc.wMaxPacketSize =
		cpu_to_le16(sd->sensor->max_packet_size[gspca_dev->curr_mode]);

	return 0;
}

static int stv06xx_isoc_nego(struct gspca_dev *gspca_dev)
{
	int ret, packet_size, min_packet_size;
	struct usb_host_interface *alt;
	struct sd *sd = (struct sd *) gspca_dev;

	/*
	 * Existence of altsetting and endpoint was verified in
	 * stv06xx_isoc_init()
	 */
	alt = &gspca_dev->dev->actconfig->intf_cache[0]->altsetting[1];
	packet_size = le16_to_cpu(alt->endpoint[0].desc.wMaxPacketSize);
	min_packet_size = sd->sensor->min_packet_size[gspca_dev->curr_mode];
	if (packet_size <= min_packet_size)
		return -EIO;

	packet_size -= 100;
	if (packet_size < min_packet_size)
		packet_size = min_packet_size;
	alt->endpoint[0].desc.wMaxPacketSize = cpu_to_le16(packet_size);

	ret = usb_set_interface(gspca_dev->dev, gspca_dev->iface, 1);
	if (ret < 0)
		gspca_err(gspca_dev, "set alt 1 err %d\n", ret);

	return ret;
}

static void stv06xx_stopN(struct gspca_dev *gspca_dev)
{
	int err;
	struct sd *sd = (struct sd *) gspca_dev;

	/* stop ISO-streaming */
	err = stv06xx_write_bridge(sd, STV_ISO_ENABLE, 0);
	if (err < 0)
		goto out;

	err = sd->sensor->stop(sd);

out:
	if (err < 0)
		gspca_dbg(gspca_dev, D_STREAM, "Failed to stop stream\n");
	else
		gspca_dbg(gspca_dev, D_STREAM, "Stopped streaming\n");
}

/*
 * Analyse an USB packet of the data stream and store it appropriately.
 * Each packet contains an integral number of chunks. Each chunk has
 * 2-bytes identification, followed by 2-bytes that describe the chunk
 * length. Known/guessed chunk identifications are:
 * 8001/8005/C001/C005 - Begin new frame
 * 8002/8006/C002/C006 - End frame
 * 0200/4200           - Contains actual image data, bayer or compressed
 * 0005                - 11 bytes of unknown data
 * 0100                - 2 bytes of unknown data
 * The 0005 and 0100 chunks seem to appear only in compressed stream.
 */
static void stv06xx_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data,			/* isoc packet */
			int len)			/* iso packet length */
{
	struct sd *sd = (struct sd *) gspca_dev;

	gspca_dbg(gspca_dev, D_PACK, "Packet of length %d arrived\n", len);

	/* A packet may contain several frames
	   loop until the whole packet is reached */
	while (len) {
		int id, chunk_len;

		if (len < 4) {
			gspca_dbg(gspca_dev, D_PACK, "Packet is smaller than 4 bytes\n");
			return;
		}

		/* Capture the id */
		id = (data[0] << 8) | data[1];

		/* Capture the chunk length */
		chunk_len = (data[2] << 8) | data[3];
		gspca_dbg(gspca_dev, D_PACK, "Chunk id: %x, length: %d\n",
			  id, chunk_len);

		data += 4;
		len -= 4;

		if (len < chunk_len) {
			gspca_err(gspca_dev, "URB packet length is smaller than the specified chunk length\n");
			gspca_dev->last_packet_type = DISCARD_PACKET;
			return;
		}

		/* First byte seem to be 02=data 2nd byte is unknown??? */
		if (sd->bridge == BRIDGE_ST6422 && (id & 0xff00) == 0x0200)
			goto frame_data;

		switch (id) {
		case 0x0200:
		case 0x4200:
frame_data:
			gspca_dbg(gspca_dev, D_PACK, "Frame data packet detected\n");

			if (sd->to_skip) {
				int skip = (sd->to_skip < chunk_len) ?
					    sd->to_skip : chunk_len;
				data += skip;
				len -= skip;
				chunk_len -= skip;
				sd->to_skip -= skip;
			}

			gspca_frame_add(gspca_dev, INTER_PACKET,
					data, chunk_len);
			break;

		case 0x8001:
		case 0x8005:
		case 0xc001:
		case 0xc005:
			gspca_dbg(gspca_dev, D_PACK, "Starting new frame\n");

			/* Create a new frame, chunk length should be zero */
			gspca_frame_add(gspca_dev, FIRST_PACKET,
					NULL, 0);

			if (sd->bridge == BRIDGE_ST6422)
				sd->to_skip = gspca_dev->pixfmt.width * 4;

			if (chunk_len)
				gspca_err(gspca_dev, "Chunk length is non-zero on a SOF\n");
			break;

		case 0x8002:
		case 0x8006:
		case 0xc002:
			gspca_dbg(gspca_dev, D_PACK, "End of frame detected\n");

			/* Complete the last frame (if any) */
			gspca_frame_add(gspca_dev, LAST_PACKET,
					NULL, 0);

			if (chunk_len)
				gspca_err(gspca_dev, "Chunk length is non-zero on a EOF\n");
			break;

		case 0x0005:
			gspca_dbg(gspca_dev, D_PACK, "Chunk 0x005 detected\n");
			/* Unknown chunk with 11 bytes of data,
			   occurs just before end of each frame
			   in compressed mode */
			break;

		case 0x0100:
			gspca_dbg(gspca_dev, D_PACK, "Chunk 0x0100 detected\n");
			/* Unknown chunk with 2 bytes of data,
			   occurs 2-3 times per USB interrupt */
			break;
		case 0x42ff:
			gspca_dbg(gspca_dev, D_PACK, "Chunk 0x42ff detected\n");
			/* Special chunk seen sometimes on the ST6422 */
			break;
		default:
			gspca_dbg(gspca_dev, D_PACK, "Unknown chunk 0x%04x detected\n",
				  id);
			/* Unknown chunk */
		}
		data    += chunk_len;
		len     -= chunk_len;
	}
}

#if IS_ENABLED(CONFIG_INPUT)
static int sd_int_pkt_scan(struct gspca_dev *gspca_dev,
			u8 *data,		/* interrupt packet data */
			int len)		/* interrupt packet length */
{
	int ret = -EINVAL;

	if (len == 1 && (data[0] == 0x80 || data[0] == 0x10)) {
		input_report_key(gspca_dev->input_dev, KEY_CAMERA, 1);
		input_sync(gspca_dev->input_dev);
		ret = 0;
	}

	if (len == 1 && (data[0] == 0x88 || data[0] == 0x11)) {
		input_report_key(gspca_dev->input_dev, KEY_CAMERA, 0);
		input_sync(gspca_dev->input_dev);
		ret = 0;
	}

	return ret;
}
#endif

static int stv06xx_config(struct gspca_dev *gspca_dev,
			  const struct usb_device_id *id);

/* sub-driver description */
static const struct sd_desc sd_desc = {
	.name = MODULE_NAME,
	.config = stv06xx_config,
	.init = stv06xx_init,
	.init_controls = stv06xx_init_controls,
	.start = stv06xx_start,
	.stopN = stv06xx_stopN,
	.pkt_scan = stv06xx_pkt_scan,
	.isoc_init = stv06xx_isoc_init,
	.isoc_nego = stv06xx_isoc_nego,
#if IS_ENABLED(CONFIG_INPUT)
	.int_pkt_scan = sd_int_pkt_scan,
#endif
};

/* This function is called at probe time */
static int stv06xx_config(struct gspca_dev *gspca_dev,
			  const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;

	gspca_dbg(gspca_dev, D_PROBE, "Configuring camera\n");

	sd->bridge = id->driver_info;
	gspca_dev->sd_desc = &sd_desc;

	if (dump_bridge)
		stv06xx_dump_bridge(sd);

	sd->sensor = &stv06xx_sensor_st6422;
	if (!sd->sensor->probe(sd))
		return 0;

	sd->sensor = &stv06xx_sensor_vv6410;
	if (!sd->sensor->probe(sd))
		return 0;

	sd->sensor = &stv06xx_sensor_hdcs1x00;
	if (!sd->sensor->probe(sd))
		return 0;

	sd->sensor = &stv06xx_sensor_hdcs1020;
	if (!sd->sensor->probe(sd))
		return 0;

	sd->sensor = &stv06xx_sensor_pb0100;
	if (!sd->sensor->probe(sd))
		return 0;

	sd->sensor = NULL;
	return -ENODEV;
}



/* -- module initialisation -- */
static const struct usb_device_id device_table[] = {
	{USB_DEVICE(0x046d, 0x0840), .driver_info = BRIDGE_STV600 },	/* QuickCam Express */
	{USB_DEVICE(0x046d, 0x0850), .driver_info = BRIDGE_STV610 },	/* LEGO cam / QuickCam Web */
	{USB_DEVICE(0x046d, 0x0870), .driver_info = BRIDGE_STV602 },	/* Dexxa WebCam USB */
	{USB_DEVICE(0x046D, 0x08F0), .driver_info = BRIDGE_ST6422 },	/* QuickCam Messenger */
	{USB_DEVICE(0x046D, 0x08F5), .driver_info = BRIDGE_ST6422 },	/* QuickCam Communicate */
	{USB_DEVICE(0x046D, 0x08F6), .driver_info = BRIDGE_ST6422 },	/* QuickCam Messenger (new) */
	{}
};
MODULE_DEVICE_TABLE(usb, device_table);

/* -- device connect -- */
static int sd_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	return gspca_dev_probe(intf, id, &sd_desc, sizeof(struct sd),
			       THIS_MODULE);
}

static void sd_disconnect(struct usb_interface *intf)
{
	struct gspca_dev *gspca_dev = usb_get_intfdata(intf);
	struct sd *sd = (struct sd *) gspca_dev;
	void *priv = sd->sensor_priv;
	gspca_dbg(gspca_dev, D_PROBE, "Disconnecting the stv06xx device\n");

	sd->sensor = NULL;
	gspca_disconnect(intf);
	kfree(priv);
}

static struct usb_driver sd_driver = {
	.name = MODULE_NAME,
	.id_table = device_table,
	.probe = sd_probe,
	.disconnect = sd_disconnect,
#ifdef CONFIG_PM
	.suspend = gspca_suspend,
	.resume = gspca_resume,
	.reset_resume = gspca_resume,
#endif
};

module_usb_driver(sd_driver);

module_param(dump_bridge, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(dump_bridge, "Dumps all usb bridge registers at startup");

module_param(dump_sensor, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(dump_sensor, "Dumps all sensor registers at startup");
