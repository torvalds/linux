/*
 * Line6 Linux USB driver - 0.9.1beta
 *
 * Copyright (C) 2004-2010 Markus Grabner (grabner@icg.tugraz.at)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#include <linux/slab.h>

#include "audio.h"
#include "control.h"
#include "driver.h"
#include "variax.h"

#define VARIAX_SYSEX_CODE 7
#define VARIAX_SYSEX_PARAM 0x3b
#define VARIAX_SYSEX_ACTIVATE 0x2a
#define VARIAX_MODEL_HEADER_LENGTH 7
#define VARIAX_MODEL_MESSAGE_LENGTH 199
#define VARIAX_OFFSET_ACTIVATE 7

/*
	This message is sent by the device during initialization and identifies
	the connected guitar model.
*/
static const char variax_init_model[] = {
	0xf0, 0x00, 0x01, 0x0c, 0x07, 0x00, 0x69, 0x02,
	0x00
};

/*
	This message is sent by the device during initialization and identifies
	the connected guitar version.
*/
static const char variax_init_version[] = {
	0xf0, 0x7e, 0x7f, 0x06, 0x02, 0x00, 0x01, 0x0c,
	0x07, 0x00, 0x00, 0x00
};

/*
	This message is the last one sent by the device during initialization.
*/
static const char variax_init_done[] = {
	0xf0, 0x00, 0x01, 0x0c, 0x07, 0x00, 0x6b
};

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

/* forward declarations: */
static int variax_create_files2(struct device *dev);
static void variax_startup2(unsigned long data);
static void variax_startup4(unsigned long data);
static void variax_startup5(unsigned long data);

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

static void variax_activate_async(struct usb_line6_variax *variax, int a)
{
	variax->buffer_activate[VARIAX_OFFSET_ACTIVATE] = a;
	line6_send_raw_message_async(&variax->line6, variax->buffer_activate,
				     sizeof(variax_activate));
}

/*
	Variax startup procedure.
	This is a sequence of functions with special requirements (e.g., must
	not run immediately after initialization, must not run in interrupt
	context). After the last one has finished, the device is ready to use.
*/

static void variax_startup1(struct usb_line6_variax *variax)
{
	CHECK_STARTUP_PROGRESS(variax->startup_progress, VARIAX_STARTUP_INIT);

	/* delay startup procedure: */
	line6_start_timer(&variax->startup_timer1, VARIAX_STARTUP_DELAY1,
			  variax_startup2, (unsigned long)variax);
}

static void variax_startup2(unsigned long data)
{
	struct usb_line6_variax *variax = (struct usb_line6_variax *)data;
	struct usb_line6 *line6 = &variax->line6;

	/* schedule another startup procedure until startup is complete: */
	if (variax->startup_progress >= VARIAX_STARTUP_LAST)
		return;

	variax->startup_progress = VARIAX_STARTUP_VERSIONREQ;
	line6_start_timer(&variax->startup_timer1, VARIAX_STARTUP_DELAY1,
			  variax_startup2, (unsigned long)variax);

	/* request firmware version: */
	line6_version_request_async(line6);
}

static void variax_startup3(struct usb_line6_variax *variax)
{
	CHECK_STARTUP_PROGRESS(variax->startup_progress, VARIAX_STARTUP_WAIT);

	/* delay startup procedure: */
	line6_start_timer(&variax->startup_timer2, VARIAX_STARTUP_DELAY3,
			  variax_startup4, (unsigned long)variax);
}

static void variax_startup4(unsigned long data)
{
	struct usb_line6_variax *variax = (struct usb_line6_variax *)data;
	CHECK_STARTUP_PROGRESS(variax->startup_progress,
			       VARIAX_STARTUP_ACTIVATE);

	/* activate device: */
	variax_activate_async(variax, 1);
	line6_start_timer(&variax->startup_timer2, VARIAX_STARTUP_DELAY4,
			  variax_startup5, (unsigned long)variax);
}

static void variax_startup5(unsigned long data)
{
	struct usb_line6_variax *variax = (struct usb_line6_variax *)data;
	CHECK_STARTUP_PROGRESS(variax->startup_progress,
			       VARIAX_STARTUP_DUMPREQ);

	/* current model dump: */
	line6_dump_request_async(&variax->dumpreq, &variax->line6, 0,
				 VARIAX_DUMP_PASS1);
	/* passes 2 and 3 are performed implicitly before entering
	 * variax_startup6.
	 */
}

static void variax_startup6(struct usb_line6_variax *variax)
{
	CHECK_STARTUP_PROGRESS(variax->startup_progress,
			       VARIAX_STARTUP_WORKQUEUE);

	/* schedule work for global work queue: */
	schedule_work(&variax->startup_work);
}

static void variax_startup7(struct work_struct *work)
{
	struct usb_line6_variax *variax =
	    container_of(work, struct usb_line6_variax, startup_work);
	struct usb_line6 *line6 = &variax->line6;

	CHECK_STARTUP_PROGRESS(variax->startup_progress, VARIAX_STARTUP_SETUP);

	/* ALSA audio interface: */
	line6_register_audio(&variax->line6);

	/* device files: */
	line6_variax_create_files(0, 0, line6->ifcdev);
	variax_create_files2(line6->ifcdev);
}

/*
	Process a completely received message.
*/
void line6_variax_process_message(struct usb_line6_variax *variax)
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
		line6_dump_request_async(&variax->dumpreq, &variax->line6, 0,
					 VARIAX_DUMP_PASS1);
		break;

	case LINE6_RESET:
		dev_info(variax->line6.ifcdev, "VARIAX reset\n");
		break;

	case LINE6_SYSEX_BEGIN:
		if (memcmp(buf + 1, variax_request_model1 + 1,
			   VARIAX_MODEL_HEADER_LENGTH - 1) == 0) {
			if (variax->line6.message_length ==
			    VARIAX_MODEL_MESSAGE_LENGTH) {
				switch (variax->dumpreq.in_progress) {
				case VARIAX_DUMP_PASS1:
					variax_decode(buf +
						      VARIAX_MODEL_HEADER_LENGTH,
						      (unsigned char *)
						      &variax->model_data,
						      (sizeof
						       (variax->model_data.
							name) +
						       sizeof(variax->
							      model_data.
							      control)
						       / 2) * 2);
					line6_dump_request_async
					    (&variax->dumpreq, &variax->line6,
					     1, VARIAX_DUMP_PASS2);
					break;

				case VARIAX_DUMP_PASS2:
					/* model name is transmitted twice, so skip it here: */
					variax_decode(buf +
						      VARIAX_MODEL_HEADER_LENGTH,
						      (unsigned char *)
						      &variax->
						      model_data.control +
						      sizeof(variax->model_data.
							     control)
						      / 2,
						      sizeof(variax->model_data.
							     control)
						      / 2 * 2);
					line6_dump_request_async
					    (&variax->dumpreq, &variax->line6,
					     2, VARIAX_DUMP_PASS3);
				}
			} else {
				DEBUG_MESSAGES(dev_err
					       (variax->line6.ifcdev,
						"illegal length %d of model data\n",
						variax->line6.message_length));
				line6_dump_finished(&variax->dumpreq);
			}
		} else if (memcmp(buf + 1, variax_request_bank + 1,
				  sizeof(variax_request_bank) - 2) == 0) {
			memcpy(variax->bank,
			       buf + sizeof(variax_request_bank) - 1,
			       sizeof(variax->bank));
			line6_dump_finished(&variax->dumpreq);
			variax_startup6(variax);
		} else if (memcmp(buf + 1, variax_init_model + 1,
				  sizeof(variax_init_model) - 1) == 0) {
			memcpy(variax->guitar,
			       buf + sizeof(variax_init_model),
			       sizeof(variax->guitar));
		} else if (memcmp(buf + 1, variax_init_version + 1,
				  sizeof(variax_init_version) - 1) == 0) {
			variax_startup3(variax);
		} else if (memcmp(buf + 1, variax_init_done + 1,
				  sizeof(variax_init_done) - 1) == 0) {
			/* notify of complete initialization: */
			variax_startup4((unsigned long)variax);
		}

		break;

	case LINE6_SYSEX_END:
		break;

	default:
		DEBUG_MESSAGES(dev_err
			       (variax->line6.ifcdev,
				"Variax: unknown message %02X\n", buf[0]));
	}
}

/*
	"read" request on "volume" special file.
*/
static ssize_t variax_get_volume(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct usb_line6_variax *variax =
	    usb_get_intfdata(to_usb_interface(dev));
	return sprintf(buf, "%d\n", variax->volume);
}

/*
	"write" request on "volume" special file.
*/
static ssize_t variax_set_volume(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct usb_line6_variax *variax =
	    usb_get_intfdata(to_usb_interface(dev));
	u8 value;
	int ret;

	ret = kstrtou8(buf, 10, &value);
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
	struct usb_line6_variax *variax =
	    usb_get_intfdata(to_usb_interface(dev));
	return sprintf(buf, "%d\n", variax->model);
}

/*
	"write" request on "model" special file.
*/
static ssize_t variax_set_model(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct usb_line6_variax *variax =
	    usb_get_intfdata(to_usb_interface(dev));
	u8 value;
	int ret;

	ret = kstrtou8(buf, 10, &value);
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
	struct usb_line6_variax *variax =
	    usb_get_intfdata(to_usb_interface(dev));
	return sprintf(buf, "%d\n",
		       variax->buffer_activate[VARIAX_OFFSET_ACTIVATE]);
}

/*
	"write" request on "active" special file.
*/
static ssize_t variax_set_active(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct usb_line6_variax *variax =
	    usb_get_intfdata(to_usb_interface(dev));
	u8 value;
	int ret;

	ret = kstrtou8(buf, 10, &value);
	if (ret)
		return ret;

	variax_activate_async(variax, value ? 1 : 0);
	return count;
}

/*
	"read" request on "tone" special file.
*/
static ssize_t variax_get_tone(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct usb_line6_variax *variax =
	    usb_get_intfdata(to_usb_interface(dev));
	return sprintf(buf, "%d\n", variax->tone);
}

/*
	"write" request on "tone" special file.
*/
static ssize_t variax_set_tone(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct usb_line6_variax *variax =
	    usb_get_intfdata(to_usb_interface(dev));
	u8 value;
	int ret;

	ret = kstrtou8(buf, 10, &value);
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
	struct usb_line6_variax *variax =
	    usb_get_intfdata(to_usb_interface(dev));
	line6_dump_wait_interruptible(&variax->dumpreq);
	return get_string(buf, variax->model_data.name,
			  sizeof(variax->model_data.name));
}

/*
	"read" request on "bank" special file.
*/
static ssize_t variax_get_bank(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct usb_line6_variax *variax =
	    usb_get_intfdata(to_usb_interface(dev));
	line6_dump_wait_interruptible(&variax->dumpreq);
	return get_string(buf, variax->bank, sizeof(variax->bank));
}

/*
	"read" request on "dump" special file.
*/
static ssize_t variax_get_dump(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct usb_line6_variax *variax =
	    usb_get_intfdata(to_usb_interface(dev));
	int retval;
	retval = line6_dump_wait_interruptible(&variax->dumpreq);
	if (retval < 0)
		return retval;
	memcpy(buf, &variax->model_data.control,
	       sizeof(variax->model_data.control));
	return sizeof(variax->model_data.control);
}

/*
	"read" request on "guitar" special file.
*/
static ssize_t variax_get_guitar(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct usb_line6_variax *variax =
	    usb_get_intfdata(to_usb_interface(dev));
	return sprintf(buf, "%s\n", variax->guitar);
}

#ifdef CONFIG_LINE6_USB_RAW

static char *variax_alloc_sysex_buffer(struct usb_line6_variax *variax,
				       int code, int size)
{
	return line6_alloc_sysex_buffer(&variax->line6, VARIAX_SYSEX_CODE, code,
					size);
}

/*
	"write" request on "raw" special file.
*/
static ssize_t variax_set_raw2(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct usb_line6_variax *variax =
	    usb_get_intfdata(to_usb_interface(dev));
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
static DEVICE_ATTR(model, S_IWUSR | S_IRUGO, variax_get_model,
		   variax_set_model);
static DEVICE_ATTR(volume, S_IWUSR | S_IRUGO, variax_get_volume,
		   variax_set_volume);
static DEVICE_ATTR(tone, S_IWUSR | S_IRUGO, variax_get_tone, variax_set_tone);
static DEVICE_ATTR(name, S_IRUGO, variax_get_name, line6_nop_write);
static DEVICE_ATTR(bank, S_IRUGO, variax_get_bank, line6_nop_write);
static DEVICE_ATTR(dump, S_IRUGO, variax_get_dump, line6_nop_write);
static DEVICE_ATTR(active, S_IWUSR | S_IRUGO, variax_get_active,
		   variax_set_active);
static DEVICE_ATTR(guitar, S_IRUGO, variax_get_guitar, line6_nop_write);

#ifdef CONFIG_LINE6_USB_RAW
static DEVICE_ATTR(raw, S_IWUSR, line6_nop_read, line6_set_raw);
static DEVICE_ATTR(raw2, S_IWUSR, line6_nop_read, variax_set_raw2);
#endif

/*
	Variax destructor.
*/
static void variax_destruct(struct usb_interface *interface)
{
	struct usb_line6_variax *variax = usb_get_intfdata(interface);

	if (variax == NULL)
		return;
	line6_cleanup_audio(&variax->line6);

	del_timer(&variax->startup_timer1);
	del_timer(&variax->startup_timer2);
	cancel_work_sync(&variax->startup_work);

	/* free dump request data: */
	line6_dumpreq_destructbuf(&variax->dumpreq, 2);
	line6_dumpreq_destructbuf(&variax->dumpreq, 1);
	line6_dumpreq_destruct(&variax->dumpreq);

	kfree(variax->buffer_activate);
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
	CHECK_RETURN(device_create_file(dev, &dev_attr_guitar));
#ifdef CONFIG_LINE6_USB_RAW
	CHECK_RETURN(device_create_file(dev, &dev_attr_raw));
	CHECK_RETURN(device_create_file(dev, &dev_attr_raw2));
#endif
	return 0;
}

/*
	 Try to init workbench device.
*/
static int variax_try_init(struct usb_interface *interface,
			   struct usb_line6_variax *variax)
{
	int err;

	init_timer(&variax->startup_timer1);
	init_timer(&variax->startup_timer2);
	INIT_WORK(&variax->startup_work, variax_startup7);

	if ((interface == NULL) || (variax == NULL))
		return -ENODEV;

	/* initialize USB buffers: */
	err = line6_dumpreq_init(&variax->dumpreq, variax_request_model1,
				 sizeof(variax_request_model1));

	if (err < 0) {
		dev_err(&interface->dev, "Out of memory\n");
		return err;
	}

	err = line6_dumpreq_initbuf(&variax->dumpreq, variax_request_model2,
				    sizeof(variax_request_model2), 1);

	if (err < 0) {
		dev_err(&interface->dev, "Out of memory\n");
		return err;
	}

	err = line6_dumpreq_initbuf(&variax->dumpreq, variax_request_bank,
				    sizeof(variax_request_bank), 2);

	if (err < 0) {
		dev_err(&interface->dev, "Out of memory\n");
		return err;
	}

	variax->buffer_activate = kmemdup(variax_activate,
					  sizeof(variax_activate), GFP_KERNEL);

	if (variax->buffer_activate == NULL) {
		dev_err(&interface->dev, "Out of memory\n");
		return -ENOMEM;
	}

	/* initialize audio system: */
	err = line6_init_audio(&variax->line6);
	if (err < 0)
		return err;

	/* initialize MIDI subsystem: */
	err = line6_init_midi(&variax->line6);
	if (err < 0)
		return err;

	/* initiate startup procedure: */
	variax_startup1(variax);
	return 0;
}

/*
	 Init workbench device (and clean up in case of failure).
*/
int line6_variax_init(struct usb_interface *interface,
		      struct usb_line6_variax *variax)
{
	int err = variax_try_init(interface, variax);

	if (err < 0)
		variax_destruct(interface);

	return err;
}

/*
	Workbench device disconnected.
*/
void line6_variax_disconnect(struct usb_interface *interface)
{
	struct device *dev;

	if (interface == NULL)
		return;
	dev = &interface->dev;

	if (dev != NULL) {
		/* remove sysfs entries: */
		line6_variax_remove_files(0, 0, dev);
		device_remove_file(dev, &dev_attr_model);
		device_remove_file(dev, &dev_attr_volume);
		device_remove_file(dev, &dev_attr_tone);
		device_remove_file(dev, &dev_attr_name);
		device_remove_file(dev, &dev_attr_bank);
		device_remove_file(dev, &dev_attr_dump);
		device_remove_file(dev, &dev_attr_active);
		device_remove_file(dev, &dev_attr_guitar);
#ifdef CONFIG_LINE6_USB_RAW
		device_remove_file(dev, &dev_attr_raw);
		device_remove_file(dev, &dev_attr_raw2);
#endif
	}

	variax_destruct(interface);
}
