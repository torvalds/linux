/*
 * Line6 Linux USB driver - 0.8.0
 *
 * Copyright (C) 2004-2009 Markus Grabner (grabner@icg.tugraz.at)
 *                         Emil Myhrman (emil.myhrman@gmail.com)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#include "driver.h"

#include "audio.h"
#include "capture.h"
#include "playback.h"
#include "toneport.h"

static int toneport_send_cmd(struct usb_device *usbdev, int cmd1, int cmd2);

static struct snd_ratden toneport_ratden = {
	.num_min = 44100,
	.num_max = 44100,
	.num_step = 1,
	.den = 1
};

static struct line6_pcm_properties toneport_pcm_properties = {
	.snd_line6_playback_hw = {
				  .info = (SNDRV_PCM_INFO_MMAP |
					   SNDRV_PCM_INFO_INTERLEAVED |
					   SNDRV_PCM_INFO_BLOCK_TRANSFER |
					   SNDRV_PCM_INFO_MMAP_VALID |
					   SNDRV_PCM_INFO_PAUSE |
					   SNDRV_PCM_INFO_SYNC_START),
				  .formats = SNDRV_PCM_FMTBIT_S16_LE,
				  .rates = SNDRV_PCM_RATE_KNOT,
				  .rate_min = 44100,
				  .rate_max = 44100,
				  .channels_min = 2,
				  .channels_max = 2,
				  .buffer_bytes_max = 60000,
				  .period_bytes_min = 180 * 4,
				  .period_bytes_max = 8192,
				  .periods_min = 1,
				  .periods_max = 1024},
	.snd_line6_capture_hw = {
				 .info = (SNDRV_PCM_INFO_MMAP |
					  SNDRV_PCM_INFO_INTERLEAVED |
					  SNDRV_PCM_INFO_BLOCK_TRANSFER |
					  SNDRV_PCM_INFO_MMAP_VALID |
					  SNDRV_PCM_INFO_SYNC_START),
				 .formats = SNDRV_PCM_FMTBIT_S16_LE,
				 .rates = SNDRV_PCM_RATE_KNOT,
				 .rate_min = 44100,
				 .rate_max = 44100,
				 .channels_min = 2,
				 .channels_max = 2,
				 .buffer_bytes_max = 60000,
				 .period_bytes_min = 188 * 4,
				 .period_bytes_max = 8192,
				 .periods_min = 1,
				 .periods_max = 1024},
	.snd_line6_rates = {
			    .nrats = 1,
			    .rats = &toneport_ratden},
	.bytes_per_frame = 4
};

/*
	For the led on Guitarport.
	Brightness goes from 0x00 to 0x26. Set a value above this to have led
	blink.
	(void cmd_0x02(byte red, byte green)
*/
static int led_red = 0x00;
static int led_green = 0x26;

static void toneport_update_led(struct device *dev)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6_toneport *tp = usb_get_intfdata(interface);
	struct usb_line6 *line6;

	if (!tp)
		return;

	line6 = &tp->line6;
	if (line6)
		toneport_send_cmd(line6->usbdev, (led_red << 8) | 0x0002,
				  led_green);
}

static ssize_t toneport_set_led_red(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int retval;
	long value;

	retval = strict_strtol(buf, 10, &value);
	if (retval)
		return retval;

	led_red = value;
	toneport_update_led(dev);
	return count;
}

static ssize_t toneport_set_led_green(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	int retval;
	long value;

	retval = strict_strtol(buf, 10, &value);
	if (retval)
		return retval;

	led_green = value;
	toneport_update_led(dev);
	return count;
}

static DEVICE_ATTR(led_red, S_IWUGO | S_IRUGO, line6_nop_read,
		   toneport_set_led_red);
static DEVICE_ATTR(led_green, S_IWUGO | S_IRUGO, line6_nop_read,
		   toneport_set_led_green);

static int toneport_send_cmd(struct usb_device *usbdev, int cmd1, int cmd2)
{
	int ret;

	ret = usb_control_msg(usbdev, usb_sndctrlpipe(usbdev, 0), 0x67,
			      USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
			      cmd1, cmd2, NULL, 0, LINE6_TIMEOUT * HZ);

	if (ret < 0) {
		err("send failed (error %d)\n", ret);
		return ret;
	}

	return 0;
}

/*
	Toneport destructor.
*/
static void toneport_destruct(struct usb_interface *interface)
{
	struct usb_line6_toneport *toneport = usb_get_intfdata(interface);
	struct usb_line6 *line6;

	if (toneport == NULL)
		return;
	line6 = &toneport->line6;
	if (line6 == NULL)
		return;
	line6_cleanup_audio(line6);
}

/*
	 Init Toneport device.
*/
int toneport_init(struct usb_interface *interface,
		  struct usb_line6_toneport *toneport)
{
	int err, ticks;
	struct usb_line6 *line6 = &toneport->line6;
	struct usb_device *usbdev;

	if ((interface == NULL) || (toneport == NULL))
		return -ENODEV;

	/* initialize audio system: */
	err = line6_init_audio(line6);
	if (err < 0) {
		toneport_destruct(interface);
		return err;
	}

	/* initialize PCM subsystem: */
	err = line6_init_pcm(line6, &toneport_pcm_properties);
	if (err < 0) {
		toneport_destruct(interface);
		return err;
	}

	/* register audio system: */
	err = line6_register_audio(line6);
	if (err < 0) {
		toneport_destruct(interface);
		return err;
	}

	usbdev = line6->usbdev;
	line6_read_serial_number(line6, &toneport->serial_number);
	line6_read_data(line6, 0x80c2, &toneport->firmware_version, 1);

	/* sync time on device with host: */
	ticks = (int)get_seconds();
	line6_write_data(line6, 0x80c6, &ticks, 4);

	/*
	   seems to work without the first two...
	 */
	/* toneport_send_cmd(usbdev, 0x0201, 0x0002); */
	/* toneport_send_cmd(usbdev, 0x0801, 0x0000); */
	/* only one that works for me; on GP, TP might be different? */
	toneport_send_cmd(usbdev, 0x0301, 0x0000);

	if (usbdev->descriptor.idProduct != LINE6_DEVID_GUITARPORT) {
		CHECK_RETURN(device_create_file
			     (&interface->dev, &dev_attr_led_red));
		CHECK_RETURN(device_create_file
			     (&interface->dev, &dev_attr_led_green));
		toneport_update_led(&usbdev->dev);
	}

	return 0;
}

/*
	Toneport device disconnected.
*/
void toneport_disconnect(struct usb_interface *interface)
{
	struct usb_line6_toneport *toneport;

	if (interface == NULL)
		return;
	toneport = usb_get_intfdata(interface);

	if (toneport->line6.usbdev->descriptor.idProduct !=
	    LINE6_DEVID_GUITARPORT) {
		device_remove_file(&interface->dev, &dev_attr_led_red);
		device_remove_file(&interface->dev, &dev_attr_led_green);
	}

	if (toneport != NULL) {
		struct snd_line6_pcm *line6pcm = toneport->line6.line6pcm;

		if (line6pcm != NULL) {
			unlink_wait_clear_audio_out_urbs(line6pcm);
			unlink_wait_clear_audio_in_urbs(line6pcm);
		}
	}

	toneport_destruct(interface);
}
