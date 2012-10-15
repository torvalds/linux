/*
 *  Asus OLED USB driver
 *
 *  Copyright (C) 2007,2008 Jakub Schmidtke (sjakub@gmail.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
 *
 *  This module is based on usbled and asus-laptop modules.
 *
 *
 *  Asus OLED support is based on asusoled program taken from
 *  <http://lapsus.berlios.de/asus_oled.html>.
 *
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/platform_device.h>
#include <linux/ctype.h>

#define ASUS_OLED_VERSION		"0.04-dev"
#define ASUS_OLED_NAME			"asus-oled"
#define ASUS_OLED_UNDERSCORE_NAME	"asus_oled"

#define ASUS_OLED_STATIC		's'
#define ASUS_OLED_ROLL			'r'
#define ASUS_OLED_FLASH			'f'

#define ASUS_OLED_MAX_WIDTH		1792
#define ASUS_OLED_DISP_HEIGHT		32
#define ASUS_OLED_PACKET_BUF_SIZE	256

#define USB_VENDOR_ID_ASUS      0x0b05
#define USB_DEVICE_ID_ASUS_LCM      0x1726
#define USB_DEVICE_ID_ASUS_LCM2     0x175b

MODULE_AUTHOR("Jakub Schmidtke, sjakub@gmail.com");
MODULE_DESCRIPTION("Asus OLED Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(ASUS_OLED_VERSION);

static struct class *oled_class;
static int oled_num;

static uint start_off;

module_param(start_off, uint, 0644);

MODULE_PARM_DESC(start_off,
		 "Set to 1 to switch off OLED display after it is attached");

enum oled_pack_mode {
	PACK_MODE_G1,
	PACK_MODE_G50,
	PACK_MODE_LAST
};

struct oled_dev_desc_str {
	uint16_t		idVendor;
	uint16_t		idProduct;
	/* width of display */
	uint16_t		devWidth;
	/* formula to be used while packing the picture */
	enum oled_pack_mode	packMode;
	const char		*devDesc;
};

/* table of devices that work with this driver */
static const struct usb_device_id id_table[] = {
	/* Asus G1/G2 (and variants)*/
	{ USB_DEVICE(USB_VENDOR_ID_ASUS, USB_DEVICE_ID_ASUS_LCM) },
	/* Asus G50V (and possibly others - G70? G71?)*/
	{ USB_DEVICE(USB_VENDOR_ID_ASUS, USB_DEVICE_ID_ASUS_LCM2) },
	{ },
};

/* parameters of specific devices */
static struct oled_dev_desc_str oled_dev_desc_table[] = {
	{ USB_VENDOR_ID_ASUS, USB_DEVICE_ID_ASUS_LCM, 128, PACK_MODE_G1,
		"G1/G2" },
	{ USB_VENDOR_ID_ASUS, USB_DEVICE_ID_ASUS_LCM2, 256, PACK_MODE_G50,
		"G50" },
	{ },
};

MODULE_DEVICE_TABLE(usb, id_table);

struct asus_oled_header {
	uint8_t		magic1;
	uint8_t		magic2;
	uint8_t		flags;
	uint8_t		value3;
	uint8_t		buffer1;
	uint8_t		buffer2;
	uint8_t		value6;
	uint8_t		value7;
	uint8_t		value8;
	uint8_t		padding2[7];
} __attribute((packed));

struct asus_oled_packet {
	struct asus_oled_header		header;
	uint8_t				bitmap[ASUS_OLED_PACKET_BUF_SIZE];
} __attribute((packed));

struct asus_oled_dev {
	struct usb_device       *udev;
	uint8_t			pic_mode;
	uint16_t		dev_width;
	enum oled_pack_mode	pack_mode;
	size_t			height;
	size_t			width;
	size_t			x_shift;
	size_t			y_shift;
	size_t			buf_offs;
	uint8_t			last_val;
	size_t			buf_size;
	char			*buf;
	uint8_t			enabled;
	uint8_t			enabled_post_resume;
	struct device		*dev;
};

static void setup_packet_header(struct asus_oled_packet *packet, char flags,
			 char value3, char buffer1, char buffer2, char value6,
			 char value7, char value8)
{
	memset(packet, 0, sizeof(struct asus_oled_header));
	packet->header.magic1 = 0x55;
	packet->header.magic2 = 0xaa;
	packet->header.flags = flags;
	packet->header.value3 = value3;
	packet->header.buffer1 = buffer1;
	packet->header.buffer2 = buffer2;
	packet->header.value6 = value6;
	packet->header.value7 = value7;
	packet->header.value8 = value8;
}

static void enable_oled(struct asus_oled_dev *odev, uint8_t enabl)
{
	int retval;
	int act_len;
	struct asus_oled_packet *packet;

	packet = kzalloc(sizeof(struct asus_oled_packet), GFP_KERNEL);

	if (!packet) {
		dev_err(&odev->udev->dev, "out of memory\n");
		return;
	}

	setup_packet_header(packet, 0x20, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00);

	if (enabl)
		packet->bitmap[0] = 0xaf;
	else
		packet->bitmap[0] = 0xae;

	retval = usb_bulk_msg(odev->udev,
		usb_sndbulkpipe(odev->udev, 2),
		packet,
		sizeof(struct asus_oled_header) + 1,
		&act_len,
		-1);

	if (retval)
		dev_dbg(&odev->udev->dev, "retval = %d\n", retval);

	odev->enabled = enabl;

	kfree(packet);
}

static ssize_t set_enabled(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct asus_oled_dev *odev = usb_get_intfdata(intf);
	unsigned long value;
	if (kstrtoul(buf, 10, &value))
		return -EINVAL;

	enable_oled(odev, value);

	return count;
}

static ssize_t class_set_enabled(struct device *device,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct asus_oled_dev *odev =
		(struct asus_oled_dev *) dev_get_drvdata(device);
	unsigned long value;

	if (kstrtoul(buf, 10, &value))
		return -EINVAL;

	enable_oled(odev, value);

	return count;
}

static ssize_t get_enabled(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct asus_oled_dev *odev = usb_get_intfdata(intf);

	return sprintf(buf, "%d\n", odev->enabled);
}

static ssize_t class_get_enabled(struct device *device,
				 struct device_attribute *attr, char *buf)
{
	struct asus_oled_dev *odev =
		(struct asus_oled_dev *) dev_get_drvdata(device);

	return sprintf(buf, "%d\n", odev->enabled);
}

static void send_packets(struct usb_device *udev,
			 struct asus_oled_packet *packet,
			 char *buf, uint8_t p_type, size_t p_num)
{
	size_t i;
	int act_len;

	for (i = 0; i < p_num; i++) {
		int retval;

		switch (p_type) {
		case ASUS_OLED_ROLL:
			setup_packet_header(packet, 0x40, 0x80, p_num,
					    i + 1, 0x00, 0x01, 0xff);
			break;
		case ASUS_OLED_STATIC:
			setup_packet_header(packet, 0x10 + i, 0x80, 0x01,
					    0x01, 0x00, 0x01, 0x00);
			break;
		case ASUS_OLED_FLASH:
			setup_packet_header(packet, 0x10 + i, 0x80, 0x01,
					    0x01, 0x00, 0x00, 0xff);
			break;
		}

		memcpy(packet->bitmap, buf + (ASUS_OLED_PACKET_BUF_SIZE*i),
		       ASUS_OLED_PACKET_BUF_SIZE);

		retval = usb_bulk_msg(udev, usb_sndctrlpipe(udev, 2),
				      packet, sizeof(struct asus_oled_packet),
				      &act_len, -1);

		if (retval)
			dev_dbg(&udev->dev, "retval = %d\n", retval);
	}
}

static void send_packet(struct usb_device *udev,
			struct asus_oled_packet *packet,
			size_t offset, size_t len, char *buf, uint8_t b1,
			uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b5,
			uint8_t b6) {
	int retval;
	int act_len;

	setup_packet_header(packet, b1, b2, b3, b4, b5, b6, 0x00);
	memcpy(packet->bitmap, buf + offset, len);

	retval = usb_bulk_msg(udev,
			      usb_sndctrlpipe(udev, 2),
			      packet,
			      sizeof(struct asus_oled_packet),
			      &act_len,
			      -1);

	if (retval)
		dev_dbg(&udev->dev, "retval = %d\n", retval);
}


static void send_packets_g50(struct usb_device *udev,
			     struct asus_oled_packet *packet, char *buf)
{
	send_packet(udev, packet,     0, 0x100, buf,
		    0x10, 0x00, 0x02, 0x01, 0x00, 0x01);
	send_packet(udev, packet, 0x100, 0x080, buf,
		    0x10, 0x00, 0x02, 0x02, 0x80, 0x00);

	send_packet(udev, packet, 0x180, 0x100, buf,
		    0x11, 0x00, 0x03, 0x01, 0x00, 0x01);
	send_packet(udev, packet, 0x280, 0x100, buf,
		    0x11, 0x00, 0x03, 0x02, 0x00, 0x01);
	send_packet(udev, packet, 0x380, 0x080, buf,
		    0x11, 0x00, 0x03, 0x03, 0x80, 0x00);
}


static void send_data(struct asus_oled_dev *odev)
{
	size_t packet_num = odev->buf_size / ASUS_OLED_PACKET_BUF_SIZE;
	struct asus_oled_packet *packet;

	packet = kzalloc(sizeof(struct asus_oled_packet), GFP_KERNEL);

	if (!packet) {
		dev_err(&odev->udev->dev, "out of memory\n");
		return;
	}

	if (odev->pack_mode == PACK_MODE_G1) {
		/* When sending roll-mode data the display updated only
		   first packet.  I have no idea why, but when static picture
		   is sent just before rolling picture everything works fine. */
		if (odev->pic_mode == ASUS_OLED_ROLL)
			send_packets(odev->udev, packet, odev->buf,
				     ASUS_OLED_STATIC, 2);

		/* Only ROLL mode can use more than 2 packets.*/
		if (odev->pic_mode != ASUS_OLED_ROLL && packet_num > 2)
			packet_num = 2;

		send_packets(odev->udev, packet, odev->buf,
			     odev->pic_mode, packet_num);
	} else if (odev->pack_mode == PACK_MODE_G50) {
		send_packets_g50(odev->udev, packet, odev->buf);
	}

	kfree(packet);
}

static int append_values(struct asus_oled_dev *odev, uint8_t val, size_t count)
{
	odev->last_val = val;

	if (val == 0) {
		odev->buf_offs += count;
		return 0;
	}

	while (count-- > 0) {
		size_t x = odev->buf_offs % odev->width;
		size_t y = odev->buf_offs / odev->width;
		size_t i;

		x += odev->x_shift;
		y += odev->y_shift;

		switch (odev->pack_mode) {
		case PACK_MODE_G1:
			/* i = (x/128)*640 + 127 - x + (y/8)*128;
			   This one for 128 is the same, but might be better
			   for different widths? */
			i = (x/odev->dev_width)*640 +
				odev->dev_width - 1 - x +
				(y/8)*odev->dev_width;
			break;

		case PACK_MODE_G50:
			i =  (odev->dev_width - 1 - x)/8 + y*odev->dev_width/8;
			break;

		default:
			i = 0;
			dev_err(odev->dev, "Unknown OLED Pack Mode: %d!\n",
			       odev->pack_mode);
			break;
		}

		if (i >= odev->buf_size) {
			dev_err(odev->dev, "Buffer overflow! Report a bug:"
			       "offs: %d >= %d i: %d (x: %d y: %d)\n",
			       (int) odev->buf_offs, (int) odev->buf_size,
			       (int) i, (int) x, (int) y);
			return -EIO;
		}

		switch (odev->pack_mode) {
		case PACK_MODE_G1:
			odev->buf[i] &= ~(1<<(y%8));
			break;

		case PACK_MODE_G50:
			odev->buf[i] &= ~(1<<(x%8));
			break;

		default:
			/* cannot get here; stops gcc complaining*/
			;
		}

		odev->buf_offs++;
	}

	return 0;
}

static ssize_t odev_set_picture(struct asus_oled_dev *odev,
				const char *buf, size_t count)
{
	size_t offs = 0, max_offs;

	if (count < 1)
		return 0;

	if (tolower(buf[0]) == 'b') {
		/* binary mode, set the entire memory*/

		size_t i;

		odev->buf_size = (odev->dev_width * ASUS_OLED_DISP_HEIGHT) / 8;

		kfree(odev->buf);
		odev->buf = kmalloc(odev->buf_size, GFP_KERNEL);
		if (odev->buf == NULL) {
			odev->buf_size = 0;
			dev_err(odev->dev, "Out of memory!\n");
			return -ENOMEM;
		}

		memset(odev->buf, 0xff, odev->buf_size);

		for (i = 1; i < count && i <= 32 * 32; i++) {
			odev->buf[i-1] = buf[i];
			odev->buf_offs = i-1;
		}

		odev->width = odev->dev_width / 8;
		odev->height = ASUS_OLED_DISP_HEIGHT;
		odev->x_shift = 0;
		odev->y_shift = 0;
		odev->last_val =  0;

		send_data(odev);

		return count;
	}

	if (buf[0] == '<') {
		size_t i;
		size_t w = 0, h = 0;
		size_t w_mem, h_mem;

		if (count < 10 || buf[2] != ':')
			goto error_header;


		switch (tolower(buf[1])) {
		case ASUS_OLED_STATIC:
		case ASUS_OLED_ROLL:
		case ASUS_OLED_FLASH:
			odev->pic_mode = buf[1];
			break;
		default:
			dev_err(odev->dev, "Wrong picture mode: '%c'.\n",
			       buf[1]);
			return -EIO;
			break;
		}

		for (i = 3; i < count; ++i) {
			if (buf[i] >= '0' && buf[i] <= '9') {
				w = 10*w + (buf[i] - '0');

				if (w > ASUS_OLED_MAX_WIDTH)
					goto error_width;
			} else if (tolower(buf[i]) == 'x') {
				break;
			} else {
				goto error_width;
			}
		}

		for (++i; i < count; ++i) {
			if (buf[i] >= '0' && buf[i] <= '9') {
				h = 10*h + (buf[i] - '0');

				if (h > ASUS_OLED_DISP_HEIGHT)
					goto error_height;
			} else if (tolower(buf[i]) == '>') {
				break;
			} else {
				goto error_height;
			}
		}

		if (w < 1 || w > ASUS_OLED_MAX_WIDTH)
			goto error_width;

		if (h < 1 || h > ASUS_OLED_DISP_HEIGHT)
			goto error_height;

		if (i >= count || buf[i] != '>')
			goto error_header;

		offs = i+1;

		if (w % (odev->dev_width) != 0)
			w_mem = (w/(odev->dev_width) + 1)*(odev->dev_width);
		else
			w_mem = w;

		if (h < ASUS_OLED_DISP_HEIGHT)
			h_mem = ASUS_OLED_DISP_HEIGHT;
		else
			h_mem = h;

		odev->buf_size = w_mem * h_mem / 8;

		kfree(odev->buf);
		odev->buf = kmalloc(odev->buf_size, GFP_KERNEL);

		if (odev->buf == NULL) {
			odev->buf_size = 0;
			dev_err(odev->dev, "Out of memory!\n");
			return -ENOMEM;
		}

		memset(odev->buf, 0xff, odev->buf_size);

		odev->buf_offs = 0;
		odev->width = w;
		odev->height = h;
		odev->x_shift = 0;
		odev->y_shift = 0;
		odev->last_val = 0;

		if (odev->pic_mode == ASUS_OLED_FLASH) {
			if (h < ASUS_OLED_DISP_HEIGHT/2)
				odev->y_shift = (ASUS_OLED_DISP_HEIGHT/2 - h)/2;
		} else {
			if (h < ASUS_OLED_DISP_HEIGHT)
				odev->y_shift = (ASUS_OLED_DISP_HEIGHT - h)/2;
		}

		if (w < (odev->dev_width))
			odev->x_shift = ((odev->dev_width) - w)/2;
	}

	max_offs = odev->width * odev->height;

	while (offs < count && odev->buf_offs < max_offs) {
		int ret = 0;

		if (buf[offs] == '1' || buf[offs] == '#') {
			ret = append_values(odev, 1, 1);
			if (ret < 0)
				return ret;
		} else if (buf[offs] == '0' || buf[offs] == ' ') {
			ret = append_values(odev, 0, 1);
			if (ret < 0)
				return ret;
		} else if (buf[offs] == '\n') {
			/* New line detected. Lets assume, that all characters
			   till the end of the line were equal to the last
			   character in this line.*/
			if (odev->buf_offs % odev->width != 0)
				ret = append_values(odev, odev->last_val,
						    odev->width -
						    (odev->buf_offs %
						     odev->width));
			if (ret < 0)
				return ret;
		}

		offs++;
	}

	if (odev->buf_offs >= max_offs)
		send_data(odev);

	return count;

error_width:
	dev_err(odev->dev, "Wrong picture width specified.\n");
	return -EIO;

error_height:
	dev_err(odev->dev, "Wrong picture height specified.\n");
	return -EIO;

error_header:
	dev_err(odev->dev, "Wrong picture header.\n");
	return -EIO;
}

static ssize_t set_picture(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);

	return odev_set_picture(usb_get_intfdata(intf), buf, count);
}

static ssize_t class_set_picture(struct device *device,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	return odev_set_picture((struct asus_oled_dev *)
				dev_get_drvdata(device), buf, count);
}

#define ASUS_OLED_DEVICE_ATTR(_file)		dev_attr_asus_oled_##_file

static DEVICE_ATTR(asus_oled_enabled, S_IWUSR | S_IRUGO,
		   get_enabled, set_enabled);
static DEVICE_ATTR(asus_oled_picture, S_IWUSR , NULL, set_picture);

static DEVICE_ATTR(enabled, S_IWUSR | S_IRUGO,
		   class_get_enabled, class_set_enabled);
static DEVICE_ATTR(picture, S_IWUSR, NULL, class_set_picture);

static int asus_oled_probe(struct usb_interface *interface,
			   const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct asus_oled_dev *odev = NULL;
	int retval = -ENOMEM;
	uint16_t dev_width = 0;
	enum oled_pack_mode pack_mode = PACK_MODE_LAST;
	const struct oled_dev_desc_str *dev_desc = oled_dev_desc_table;
	const char *desc = NULL;

	if (!id) {
		/* Even possible? Just to make sure...*/
		dev_err(&interface->dev, "No usb_device_id provided!\n");
		return -ENODEV;
	}

	for (; dev_desc->idVendor; dev_desc++) {
		if (dev_desc->idVendor == id->idVendor
		    && dev_desc->idProduct == id->idProduct) {
			dev_width = dev_desc->devWidth;
			desc = dev_desc->devDesc;
			pack_mode = dev_desc->packMode;
			break;
		}
	}

	if (!desc || dev_width < 1 || pack_mode == PACK_MODE_LAST) {
		dev_err(&interface->dev,
			"Missing or incomplete device description!\n");
		return -ENODEV;
	}

	odev = kzalloc(sizeof(struct asus_oled_dev), GFP_KERNEL);

	if (odev == NULL) {
		dev_err(&interface->dev, "Out of memory\n");
		return -ENOMEM;
	}

	odev->udev = usb_get_dev(udev);
	odev->pic_mode = ASUS_OLED_STATIC;
	odev->dev_width = dev_width;
	odev->pack_mode = pack_mode;
	odev->height = 0;
	odev->width = 0;
	odev->x_shift = 0;
	odev->y_shift = 0;
	odev->buf_offs = 0;
	odev->buf_size = 0;
	odev->last_val = 0;
	odev->buf = NULL;
	odev->enabled = 1;
	odev->dev = NULL;

	usb_set_intfdata(interface, odev);

	retval = device_create_file(&interface->dev,
				    &ASUS_OLED_DEVICE_ATTR(enabled));
	if (retval)
		goto err_files;

	retval = device_create_file(&interface->dev,
				    &ASUS_OLED_DEVICE_ATTR(picture));
	if (retval)
		goto err_files;

	odev->dev = device_create(oled_class, &interface->dev, MKDEV(0, 0),
				  NULL, "oled_%d", ++oled_num);

	if (IS_ERR(odev->dev)) {
		retval = PTR_ERR(odev->dev);
		goto err_files;
	}

	dev_set_drvdata(odev->dev, odev);

	retval = device_create_file(odev->dev, &dev_attr_enabled);
	if (retval)
		goto err_class_enabled;

	retval = device_create_file(odev->dev, &dev_attr_picture);
	if (retval)
		goto err_class_picture;

	dev_info(&interface->dev,
		 "Attached Asus OLED device: %s [width %u, pack_mode %d]\n",
		 desc, odev->dev_width, odev->pack_mode);

	if (start_off)
		enable_oled(odev, 0);

	return 0;

err_class_picture:
	device_remove_file(odev->dev, &dev_attr_picture);

err_class_enabled:
	device_remove_file(odev->dev, &dev_attr_enabled);
	device_unregister(odev->dev);

err_files:
	device_remove_file(&interface->dev, &ASUS_OLED_DEVICE_ATTR(enabled));
	device_remove_file(&interface->dev, &ASUS_OLED_DEVICE_ATTR(picture));

	usb_set_intfdata(interface, NULL);
	usb_put_dev(odev->udev);
	kfree(odev);

	return retval;
}

static void asus_oled_disconnect(struct usb_interface *interface)
{
	struct asus_oled_dev *odev;

	odev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	device_remove_file(odev->dev, &dev_attr_picture);
	device_remove_file(odev->dev, &dev_attr_enabled);
	device_unregister(odev->dev);

	device_remove_file(&interface->dev, &ASUS_OLED_DEVICE_ATTR(picture));
	device_remove_file(&interface->dev, &ASUS_OLED_DEVICE_ATTR(enabled));

	usb_put_dev(odev->udev);

	kfree(odev->buf);

	kfree(odev);

	dev_info(&interface->dev, "Disconnected Asus OLED device\n");
}

#ifdef CONFIG_PM
static int asus_oled_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct asus_oled_dev *odev;

	odev = usb_get_intfdata(intf);
	if (!odev)
		return -ENODEV;

	odev->enabled_post_resume = odev->enabled;
	enable_oled(odev, 0);

	return 0;
}

static int asus_oled_resume(struct usb_interface *intf)
{
	struct asus_oled_dev *odev;

	odev = usb_get_intfdata(intf);
	if (!odev)
		return -ENODEV;

	enable_oled(odev, odev->enabled_post_resume);

	return 0;
}
#else
#define asus_oled_suspend NULL
#define asus_oled_resume NULL
#endif

static struct usb_driver oled_driver = {
	.name =		ASUS_OLED_NAME,
	.probe =	asus_oled_probe,
	.disconnect =	asus_oled_disconnect,
	.id_table =	id_table,
	.suspend =	asus_oled_suspend,
	.resume =	asus_oled_resume,
};

static CLASS_ATTR_STRING(version, S_IRUGO,
			ASUS_OLED_UNDERSCORE_NAME " " ASUS_OLED_VERSION);

static int __init asus_oled_init(void)
{
	int retval = 0;
	oled_class = class_create(THIS_MODULE, ASUS_OLED_UNDERSCORE_NAME);

	if (IS_ERR(oled_class)) {
		pr_err("Error creating " ASUS_OLED_UNDERSCORE_NAME " class\n");
		return PTR_ERR(oled_class);
	}

	retval = class_create_file(oled_class, &class_attr_version.attr);
	if (retval) {
		pr_err("Error creating class version file\n");
		goto error;
	}

	retval = usb_register(&oled_driver);

	if (retval) {
		pr_err("usb_register failed. Error number %d\n", retval);
		goto error;
	}

	return retval;

error:
	class_destroy(oled_class);
	return retval;
}

static void __exit asus_oled_exit(void)
{
	usb_deregister(&oled_driver);
	class_remove_file(oled_class, &class_attr_version.attr);
	class_destroy(oled_class);
}

module_init(asus_oled_init);
module_exit(asus_oled_exit);

