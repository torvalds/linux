/*
 * Line6 Linux USB driver - 0.8.0
 *
 * Copyright (C) 2004-2009 Markus Grabner (grabner@icg.tugraz.at)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#include "driver.h"

#include <linux/slab.h>

#include "audio.h"
#include "control.h"
#include "variax.h"


#define VARIAX_SYSEX_CODE 7
#define VARIAX_SYSEX_PARAM 0x3b
#define VARIAX_SYSEX_ACTIVATE 0x2a
#define VARIAX_MODEL_HEADER_LENGTH 7
#define VARIAX_MODEL_MESSAGE_LENGTH 199
#define VARIAX_OFFSET_ACTIVATE 7


static const char variax_activate[] = {
	0xf0, 0x00, 0x01, 0x0c, 0x07, 0x00, 0x2a, 0x01,
	0xf7
};
static const char variax_request_bank[] = {
	0xf0, 0x00, 0x01, 0x0c, 0x07, 0x00, 0x6d, 0xf7
};
static const char variax_request_model1[] = {
	0xf0, 0x00, 0x01, 0x0c, 0x07, 0x00, 0x3c, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x05, 0x03,
	0x00, 0x00, 0x00, 0xf7
};
static const char variax_request_model2[] = {
	0xf0, 0x00, 0x01, 0x0c, 0x07, 0x00, 0x3c, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x07, 0x03,
	0x00, 0x00, 0x00, 0xf7
};


/*
	Decode data transmitted by workbench.
*/
static void variax_decode(const unsigned char *raw_data, unsigned char *data,
			  int raw_size)
{
	for (; raw_size > 0; raw_size -= 6) {
		data[2] = raw_data[0] | (raw_data[1] << 4);
		data[1] = raw_data[2] | (raw_data[3] << 4);
		data[0] = raw_data[4] | (raw_data[5] << 4);
		raw_data += 6;
		data += 3;
	}
}

static void variax_activate_timeout(unsigned long arg)
{
	struct usb_line6_variax *variax = (struct usb_line6_variax *)arg;
	variax->buffer_activate[VARIAX_OFFSET_ACTIVATE] = 1;
	line6_send_raw_message_async(&variax->line6, variax->buffer_activate,
				     sizeof(variax_activate));
}

/*
	Send an asynchronous activation request after a given interval.
*/
static void variax_activate_delayed(struct usb_line6_variax *variax,
				    int seconds)
{
	variax->activate_timer.expires = jiffies + seconds * HZ;
	variax->activate_timer.function = variax_activate_timeout;
	variax->activate_timer.data = (unsigned long)variax;
	add_timer(&variax->activate_timer);
}

static void variax_startup_timeout(unsigned long arg)
{
	struct usb_line6_variax *variax = (struct usb_line6_variax *)arg;

	if (variax->dumpreq.ok)
		return;

	line6_dump_request_async(&variax->dumpreq, &variax->line6, 0);
	line6_startup_delayed(&variax->dumpreq, 1, variax_startup_timeout,
			      variax);
}

/*
	Process a completely received message.
*/
void variax_process_message(struct usb_line6_variax *variax)
{
	const unsigned char *buf = variax->line6.buffer_message;

	switch (buf[0]) {
	case LINE6_PARAM_CHANGE | LINE6_CHANNEL_HOST:
		switch (buf[1]) {
		case VARIAXMIDI_volume:
			variax->volume = buf[2];
			break;

		case VARIAXMIDI_tone:
			variax->tone = buf[2];
		}

		break;

	case LINE6_PROGRAM_CHANGE | LINE6_CHANNEL_DEVICE:
	case LINE6_PROGRAM_CHANGE | LINE6_CHANNEL_HOST:
		variax->model = buf[1];
		line6_dump_request_async(&variax->dumpreq, &variax->line6, 0);
		break;

	case LINE6_RESET:
		dev_info(variax->line6.ifcdev, "VARIAX reset\n");
		variax_activate_delayed(variax, VARIAX_ACTIVATE_DELAY);
		break;

	case LINE6_SYSEX_BEGIN:
		if (memcmp(buf + 1, variax_request_model1 + 1,
			   VARIAX_MODEL_HEADER_LENGTH - 1) == 0) {
			if (variax->line6.message_length ==
			    VARIAX_MODEL_MESSAGE_LENGTH) {
				switch (variax->dumpreq.in_progress) {
				case VARIAX_DUMP_PASS1:
					variax_decode(buf + VARIAX_MODEL_HEADER_LENGTH, (unsigned char *)&variax->model_data,
												(sizeof(variax->model_data.name) + sizeof(variax->model_data.control) / 2) * 2);
					line6_dump_request_async(&variax->dumpreq, &variax->line6, 1);
					line6_dump_started(&variax->dumpreq, VARIAX_DUMP_PASS2);
					break;

				case VARIAX_DUMP_PASS2:
					/* model name is transmitted twice, so skip it here: */
					variax_decode(buf + VARIAX_MODEL_HEADER_LENGTH,
						      (unsigned char *)&variax->model_data.control + sizeof(variax->model_data.control) / 2,
						      sizeof(variax->model_data.control) / 2 * 2);
					variax->dumpreq.ok = 1;
					line6_dump_request_async(&variax->dumpreq, &variax->line6, 2);
					line6_dump_started(&variax->dumpreq, VARIAX_DUMP_PASS3);
				}
			} else {
				DEBUG_MESSAGES(dev_err(variax->line6.ifcdev, "illegal length %d of model data\n", variax->line6.message_length));
				line6_dump_finished(&variax->dumpreq);
			}
		} else if (memcmp(buf + 1, variax_request_bank + 1,
				sizeof(variax_request_bank) - 2) == 0) {
			memcpy(variax->bank,
			       buf + sizeof(variax_request_bank) - 1,
			       sizeof(variax->bank));
			variax->dumpreq.ok = 1;
			line6_dump_finished(&variax->dumpreq);
		}

		break;

	case LINE6_SYSEX_END:
		break;

	default:
		DEBUG_MESSAGES(dev_err(variax->line6.ifcdev, "Variax: unknown message %02X\n", buf[0]));
	}
}

/*
	"read" request on "volume" special file.
*/
static ssize_t variax_get_volume(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct usb_line6_variax *variax = usb_get_intfdata(to_usb_interface(dev));
	return sprintf(buf, "%d\n", variax->volume);
}

/*
	"write" request on "volume" special file.
*/
static ssize_t variax_set_volume(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct usb_line6_variax *variax = usb_get_intfdata(to_usb_interface(dev));
	unsigned long value;
	int ret;

	ret = strict_strtoul(buf, 10, &value);
	if (ret)
		return ret;

	if (line6_transmit_parameter(&variax->line6, VARIAXMIDI_volume,
				     value) == 0)
		variax->volume = value;

	return count;
}

/*
	"read" request on "model" special file.
*/
static ssize_t variax_get_model(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct usb_line6_variax *variax = usb_get_intfdata(to_usb_interface(dev));
	return sprintf(buf, "%d\n", variax->model);
}

/*
	"write" request on "model" special file.
*/
static ssize_t variax_set_model(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct usb_line6_variax *variax = usb_get_intfdata(to_usb_interface(dev));
	unsigned long value;
	int ret;

	ret = strict_strtoul(buf, 10, &value);
	if (ret)
		return ret;

	if (line6_send_program(&variax->line6, value) == 0)
		variax->model = value;

	return count;
}

/*
	"read" request on "active" special file.
*/
static ssize_t variax_get_active(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct usb_line6_variax *variax = usb_get_intfdata(to_usb_interface(dev));
	return sprintf(buf, "%d\n", variax->buffer_activate[VARIAX_OFFSET_ACTIVATE]);
}

/*
	"write" request on "active" special file.
*/
static ssize_t variax_set_active(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct usb_line6_variax *variax = usb_get_intfdata(to_usb_interface(dev));
	unsigned long value;
	int ret;

	ret = strict_strtoul(buf, 10, &value);
	if (ret)
		return ret;

	variax->buffer_activate[VARIAX_OFFSET_ACTIVATE] = value ? 1 : 0;
	line6_send_raw_message_async(&variax->line6, variax->buffer_activate,
				     sizeof(variax_activate));
	return count;
}

/*
	"read" request on "tone" special file.
*/
static ssize_t variax_get_tone(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct usb_line6_variax *variax = usb_get_intfdata(to_usb_interface(dev));
	return sprintf(buf, "%d\n", variax->tone);
}

/*
	"write" request on "tone" special file.
*/
static ssize_t variax_set_tone(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct usb_line6_variax *variax = usb_get_intfdata(to_usb_interface(dev));
	unsigned long value;
	int ret;

	ret = strict_strtoul(buf, 10, &value);
	if (ret)
		return ret;

	if (line6_transmit_parameter(&variax->line6, VARIAXMIDI_tone,
				     value) == 0)
		variax->tone = value;

	return count;
}

static ssize_t get_string(char *buf, const char *data, int length)
{
	int i;
	memcpy(buf, data, length);

	for (i = length; i--;) {
		char c = buf[i];

		if ((c != 0) && (c != ' '))
			break;
	}

	buf[i + 1] = '\n';
	return i + 2;
}

/*
	"read" request on "name" special file.
*/
static ssize_t variax_get_name(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct usb_line6_variax *variax = usb_get_intfdata(to_usb_interface(dev));
	line6_wait_dump(&variax->dumpreq, 0);
	return get_string(buf, variax->model_data.name,
			  sizeof(variax->model_data.name));
}

/*
	"read" request on "bank" special file.
*/
static ssize_t variax_get_bank(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct usb_line6_variax *variax = usb_get_intfdata(to_usb_interface(dev));
	line6_wait_dump(&variax->dumpreq, 0);
	return get_string(buf, variax->bank, sizeof(variax->bank));
}

/*
	"read" request on "dump" special file.
*/
static ssize_t variax_get_dump(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct usb_line6_variax *variax = usb_get_intfdata(to_usb_interface(dev));
	int retval;
	retval = line6_wait_dump(&variax->dumpreq, 0);
	if (retval < 0)
		return retval;
	memcpy(buf, &variax->model_data.control,
	       sizeof(variax->model_data.control));
	return sizeof(variax->model_data.control);
}

#if CREATE_RAW_FILE

/*
	"write" request on "raw" special file.
*/
static ssize_t variax_set_raw2(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct usb_line6_variax *variax = usb_get_intfdata(to_usb_interface(dev));
	int size;
	int i;
	char *sysex;

	count -= count % 3;
	size = count * 2;
	sysex = variax_alloc_sysex_buffer(variax, VARIAX_SYSEX_PARAM, size);

	if (!sysex)
		return 0;

	for (i = 0; i < count; i += 3) {
		const unsigned char *p1 = buf + i;
		char *p2 = sysex + SYSEX_DATA_OFS + i * 2;
		p2[0] = p1[2] & 0x0f;
		p2[1] = p1[2] >> 4;
		p2[2] = p1[1] & 0x0f;
		p2[3] = p1[1] >> 4;
		p2[4] = p1[0] & 0x0f;
		p2[5] = p1[0] >> 4;
	}

	line6_send_sysex_message(&variax->line6, sysex, size);
	kfree(sysex);
	return count;
}

#endif

/* Variax workbench special files: */
static DEVICE_ATTR(model, S_IWUGO | S_IRUGO, variax_get_model, variax_set_model);
static DEVICE_ATTR(volume, S_IWUGO | S_IRUGO, variax_get_volume, variax_set_volume);
static DEVICE_ATTR(tone, S_IWUGO | S_IRUGO, variax_get_tone, variax_set_tone);
static DEVICE_ATTR(name, S_IRUGO, variax_get_name, line6_nop_write);
static DEVICE_ATTR(bank, S_IRUGO, variax_get_bank, line6_nop_write);
static DEVICE_ATTR(dump, S_IRUGO, variax_get_dump, line6_nop_write);
static DEVICE_ATTR(active, S_IWUGO | S_IRUGO, variax_get_active, variax_set_active);

#if CREATE_RAW_FILE
static DEVICE_ATTR(raw, S_IWUGO, line6_nop_read, line6_set_raw);
static DEVICE_ATTR(raw2, S_IWUGO, line6_nop_read, variax_set_raw2);
#endif


/*
	Variax destructor.
*/
static void variax_destruct(struct usb_interface *interface)
{
	struct usb_line6_variax *variax = usb_get_intfdata(interface);
	struct usb_line6 *line6;

	if (variax == NULL)
		return;
	line6 = &variax->line6;
	if (line6 == NULL)
		return;
	line6_cleanup_audio(line6);

	/* free dump request data: */
	line6_dumpreq_destructbuf(&variax->dumpreq, 2);
	line6_dumpreq_destructbuf(&variax->dumpreq, 1);
	line6_dumpreq_destruct(&variax->dumpreq);

	kfree(variax->buffer_activate);
	del_timer_sync(&variax->activate_timer);
}

/*
	Create sysfs entries.
*/
static int variax_create_files2(struct device *dev)
{
	int err;
	CHECK_RETURN(device_create_file(dev, &dev_attr_model));
	CHECK_RETURN(device_create_file(dev, &dev_attr_volume));
	CHECK_RETURN(device_create_file(dev, &dev_attr_tone));
	CHECK_RETURN(device_create_file(dev, &dev_attr_name));
	CHECK_RETURN(device_create_file(dev, &dev_attr_bank));
	CHECK_RETURN(device_create_file(dev, &dev_attr_dump));
	CHECK_RETURN(device_create_file(dev, &dev_attr_active));
#if CREATE_RAW_FILE
	CHECK_RETURN(device_create_file(dev, &dev_attr_raw));
	CHECK_RETURN(device_create_file(dev, &dev_attr_raw2));
#endif
	return 0;
}

/*
	 Init workbench device.
*/
int variax_init(struct usb_interface *interface,
		struct usb_line6_variax *variax)
{
	int err;

	if ((interface == NULL) || (variax == NULL))
		return -ENODEV;

	/* initialize USB buffers: */
	err = line6_dumpreq_init(&variax->dumpreq, variax_request_model1,
				 sizeof(variax_request_model1));

	if (err < 0) {
		dev_err(&interface->dev, "Out of memory\n");
		variax_destruct(interface);
		return err;
	}

	err = line6_dumpreq_initbuf(&variax->dumpreq, variax_request_model2,
				    sizeof(variax_request_model2), 1);

	if (err < 0) {
		dev_err(&interface->dev, "Out of memory\n");
		variax_destruct(interface);
		return err;
	}

	err = line6_dumpreq_initbuf(&variax->dumpreq, variax_request_bank,
				    sizeof(variax_request_bank), 2);

	if (err < 0) {
		dev_err(&interface->dev, "Out of memory\n");
		variax_destruct(interface);
		return err;
	}

	variax->buffer_activate = kmemdup(variax_activate,
					  sizeof(variax_activate), GFP_KERNEL);

	if (variax->buffer_activate == NULL) {
		dev_err(&interface->dev, "Out of memory\n");
		variax_destruct(interface);
		return -ENOMEM;
	}

	init_timer(&variax->activate_timer);

	/* create sysfs entries: */
	err = variax_create_files(0, 0, &interface->dev);
	if (err < 0) {
		variax_destruct(interface);
		return err;
	}

	err = variax_create_files2(&interface->dev);
	if (err < 0) {
		variax_destruct(interface);
		return err;
	}

	/* initialize audio system: */
	err = line6_init_audio(&variax->line6);
	if (err < 0) {
		variax_destruct(interface);
		return err;
	}

	/* initialize MIDI subsystem: */
	err = line6_init_midi(&variax->line6);
	if (err < 0) {
		variax_destruct(interface);
		return err;
	}

	/* register audio system: */
	err = line6_register_audio(&variax->line6);
	if (err < 0) {
		variax_destruct(interface);
		return err;
	}

	variax_activate_delayed(variax, VARIAX_ACTIVATE_DELAY);
	line6_startup_delayed(&variax->dumpreq, VARIAX_STARTUP_DELAY,
			      variax_startup_timeout, variax);
	return 0;
}

/*
	Workbench device disconnected.
*/
void variax_disconnect(struct usb_interface *interface)
{
	struct device *dev;

	if (interface == NULL)
		return;
	dev = &interface->dev;

	if (dev != NULL) {
		/* remove sysfs entries: */
		variax_remove_files(0, 0, dev);
		device_remove_file(dev, &dev_attr_model);
		device_remove_file(dev, &dev_attr_volume);
		device_remove_file(dev, &dev_attr_tone);
		device_remove_file(dev, &dev_attr_name);
		device_remove_file(dev, &dev_attr_bank);
		device_remove_file(dev, &dev_attr_dump);
		device_remove_file(dev, &dev_attr_active);
#if CREATE_RAW_FILE
		device_remove_file(dev, &dev_attr_raw);
		device_remove_file(dev, &dev_attr_raw2);
#endif
	}

	variax_destruct(interface);
}
