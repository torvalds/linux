/*
 * Line6 Linux USB driver - 0.9.1beta
 *
 * Copyright (C) 2004-2010 Markus Grabner (grabner@icg.tugraz.at)
 *                         Emil Myhrman (emil.myhrman@gmail.com)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#include <linux/wait.h>
#include <sound/control.h>

#include "audio.h"
#include "capture.h"
#include "driver.h"
#include "playback.h"
#include "toneport.h"

static int toneport_send_cmd(struct usb_device *usbdev, int cmd1, int cmd2);

#define TONEPORT_PCM_DELAY 1

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
#ifdef CONFIG_PM
					   SNDRV_PCM_INFO_RESUME |
#endif
					   SNDRV_PCM_INFO_SYNC_START),
				  .formats = SNDRV_PCM_FMTBIT_S16_LE,
				  .rates = SNDRV_PCM_RATE_KNOT,
				  .rate_min = 44100,
				  .rate_max = 44100,
				  .channels_min = 2,
				  .channels_max = 2,
				  .buffer_bytes_max = 60000,
				  .period_bytes_min = 64,
				  .period_bytes_max = 8192,
				  .periods_min = 1,
				  .periods_max = 1024},
	.snd_line6_capture_hw = {
				 .info = (SNDRV_PCM_INFO_MMAP |
					  SNDRV_PCM_INFO_INTERLEAVED |
					  SNDRV_PCM_INFO_BLOCK_TRANSFER |
					  SNDRV_PCM_INFO_MMAP_VALID |
#ifdef CONFIG_PM
					  SNDRV_PCM_INFO_RESUME |
#endif
					  SNDRV_PCM_INFO_SYNC_START),
				 .formats = SNDRV_PCM_FMTBIT_S16_LE,
				 .rates = SNDRV_PCM_RATE_KNOT,
				 .rate_min = 44100,
				 .rate_max = 44100,
				 .channels_min = 2,
				 .channels_max = 2,
				 .buffer_bytes_max = 60000,
				 .period_bytes_min = 64,
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

struct ToneportSourceInfo {
	const char *name;
	int code;
};

static const struct ToneportSourceInfo toneport_source_info[] = {
	{"Microphone", 0x0a01},
	{"Line", 0x0801},
	{"Instrument", 0x0b01},
	{"Inst & Mic", 0x0901}
};

static bool toneport_has_led(short product)
{
	return
	    (product == LINE6_DEVID_GUITARPORT) ||
	    (product == LINE6_DEVID_TONEPORT_GX);
	/* add your device here if you are missing support for the LEDs */
}

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

static DEVICE_ATTR(led_red, S_IWUSR | S_IRUGO, line6_nop_read,
		   toneport_set_led_red);
static DEVICE_ATTR(led_green, S_IWUSR | S_IRUGO, line6_nop_read,
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

/* monitor info callback */
static int snd_toneport_monitor_info(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 256;
	return 0;
}

/* monitor get callback */
static int snd_toneport_monitor_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_line6_pcm *line6pcm = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = line6pcm->volume_monitor;
	return 0;
}

/* monitor put callback */
static int snd_toneport_monitor_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_line6_pcm *line6pcm = snd_kcontrol_chip(kcontrol);

	if (ucontrol->value.integer.value[0] == line6pcm->volume_monitor)
		return 0;

	line6pcm->volume_monitor = ucontrol->value.integer.value[0];

	if (line6pcm->volume_monitor > 0)
		line6_pcm_start(line6pcm, MASK_PCM_MONITOR);
	else
		line6_pcm_stop(line6pcm, MASK_PCM_MONITOR);

	return 1;
}

/* source info callback */
static int snd_toneport_source_info(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_info *uinfo)
{
	const int size = ARRAY_SIZE(toneport_source_info);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = size;

	if (uinfo->value.enumerated.item >= size)
		uinfo->value.enumerated.item = size - 1;

	strcpy(uinfo->value.enumerated.name,
	       toneport_source_info[uinfo->value.enumerated.item].name);

	return 0;
}

/* source get callback */
static int snd_toneport_source_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_line6_pcm *line6pcm = snd_kcontrol_chip(kcontrol);
	struct usb_line6_toneport *toneport =
	    (struct usb_line6_toneport *)line6pcm->line6;
	ucontrol->value.enumerated.item[0] = toneport->source;
	return 0;
}

/* source put callback */
static int snd_toneport_source_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_line6_pcm *line6pcm = snd_kcontrol_chip(kcontrol);
	struct usb_line6_toneport *toneport =
	    (struct usb_line6_toneport *)line6pcm->line6;

	if (ucontrol->value.enumerated.item[0] == toneport->source)
		return 0;

	toneport->source = ucontrol->value.enumerated.item[0];
	toneport_send_cmd(toneport->line6.usbdev,
			  toneport_source_info[toneport->source].code, 0x0000);
	return 1;
}

static void toneport_start_pcm(unsigned long arg)
{
	struct usb_line6_toneport *toneport = (struct usb_line6_toneport *)arg;
	struct usb_line6 *line6 = &toneport->line6;
	line6_pcm_start(line6->line6pcm, MASK_PCM_MONITOR);
}

/* control definition */
static struct snd_kcontrol_new toneport_control_monitor = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Monitor Playback Volume",
	.index = 0,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = snd_toneport_monitor_info,
	.get = snd_toneport_monitor_get,
	.put = snd_toneport_monitor_put
};

/* source selector definition */
static struct snd_kcontrol_new toneport_control_source = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "PCM Capture Source",
	.index = 0,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = snd_toneport_source_info,
	.get = snd_toneport_source_get,
	.put = snd_toneport_source_put
};

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
	Setup Toneport device.
*/
static void toneport_setup(struct usb_line6_toneport *toneport)
{
	int ticks;
	struct usb_line6 *line6 = &toneport->line6;
	struct usb_device *usbdev = line6->usbdev;

	/* sync time on device with host: */
	ticks = (int)get_seconds();
	line6_write_data(line6, 0x80c6, &ticks, 4);

	/* enable device: */
	toneport_send_cmd(usbdev, 0x0301, 0x0000);

	/* initialize source select: */
	switch (usbdev->descriptor.idProduct) {
	case LINE6_DEVID_TONEPORT_UX1:
	case LINE6_DEVID_PODSTUDIO_UX1:
		toneport_send_cmd(usbdev,
				  toneport_source_info[toneport->source].code,
				  0x0000);
	}

	if (toneport_has_led(usbdev->descriptor.idProduct))
		toneport_update_led(&usbdev->dev);
}

/*
	 Try to init Toneport device.
*/
static int toneport_try_init(struct usb_interface *interface,
			     struct usb_line6_toneport *toneport)
{
	int err;
	struct usb_line6 *line6 = &toneport->line6;
	struct usb_device *usbdev = line6->usbdev;

	if ((interface == NULL) || (toneport == NULL))
		return -ENODEV;

	/* initialize audio system: */
	err = line6_init_audio(line6);
	if (err < 0)
		return err;

	/* initialize PCM subsystem: */
	err = line6_init_pcm(line6, &toneport_pcm_properties);
	if (err < 0)
		return err;

	/* register monitor control: */
	err = snd_ctl_add(line6->card,
			  snd_ctl_new1(&toneport_control_monitor,
				       line6->line6pcm));
	if (err < 0)
		return err;

	/* register source select control: */
	switch (usbdev->descriptor.idProduct) {
	case LINE6_DEVID_TONEPORT_UX1:
	case LINE6_DEVID_PODSTUDIO_UX1:
		err =
		    snd_ctl_add(line6->card,
				snd_ctl_new1(&toneport_control_source,
					     line6->line6pcm));
		if (err < 0)
			return err;
	}

	/* register audio system: */
	err = line6_register_audio(line6);
	if (err < 0)
		return err;

	line6_read_serial_number(line6, &toneport->serial_number);
	line6_read_data(line6, 0x80c2, &toneport->firmware_version, 1);

	if (toneport_has_led(usbdev->descriptor.idProduct)) {
		CHECK_RETURN(device_create_file
			     (&interface->dev, &dev_attr_led_red));
		CHECK_RETURN(device_create_file
			     (&interface->dev, &dev_attr_led_green));
	}

	toneport_setup(toneport);

	init_timer(&toneport->timer);
	toneport->timer.expires = jiffies + TONEPORT_PCM_DELAY * HZ;
	toneport->timer.function = toneport_start_pcm;
	toneport->timer.data = (unsigned long)toneport;
	add_timer(&toneport->timer);

	return 0;
}

/*
	 Init Toneport device (and clean up in case of failure).
*/
int line6_toneport_init(struct usb_interface *interface,
			struct usb_line6_toneport *toneport)
{
	int err = toneport_try_init(interface, toneport);

	if (err < 0)
		toneport_destruct(interface);

	return err;
}

/*
	Resume Toneport device after reset.
*/
void line6_toneport_reset_resume(struct usb_line6_toneport *toneport)
{
	toneport_setup(toneport);
}

/*
	Toneport device disconnected.
*/
void line6_toneport_disconnect(struct usb_interface *interface)
{
	struct usb_line6_toneport *toneport;

	if (interface == NULL)
		return;

	toneport = usb_get_intfdata(interface);
	del_timer_sync(&toneport->timer);

	if (toneport_has_led(toneport->line6.usbdev->descriptor.idProduct)) {
		device_remove_file(&interface->dev, &dev_attr_led_red);
		device_remove_file(&interface->dev, &dev_attr_led_green);
	}

	if (toneport != NULL) {
		struct snd_line6_pcm *line6pcm = toneport->line6.line6pcm;

		if (line6pcm != NULL) {
			line6_pcm_stop(line6pcm, MASK_PCM_MONITOR);
			line6_pcm_disconnect(line6pcm);
		}
	}

	toneport_destruct(interface);
}
