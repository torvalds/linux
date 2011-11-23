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
#include <linux/usb.h>
#include <sound/core.h>
#include <sound/rawmidi.h>

#include "audio.h"
#include "driver.h"
#include "midi.h"
#include "pod.h"
#include "usbdefs.h"

#define line6_rawmidi_substream_midi(substream) \
	((struct snd_line6_midi *)((substream)->rmidi->private_data))

static int send_midi_async(struct usb_line6 *line6, unsigned char *data,
			   int length);

/*
	Pass data received via USB to MIDI.
*/
void line6_midi_receive(struct usb_line6 *line6, unsigned char *data,
			int length)
{
	if (line6->line6midi->substream_receive)
		snd_rawmidi_receive(line6->line6midi->substream_receive,
				    data, length);
}

/*
	Read data from MIDI buffer and transmit them via USB.
*/
static void line6_midi_transmit(struct snd_rawmidi_substream *substream)
{
	struct usb_line6 *line6 =
	    line6_rawmidi_substream_midi(substream)->line6;
	struct snd_line6_midi *line6midi = line6->line6midi;
	struct MidiBuffer *mb = &line6midi->midibuf_out;
	unsigned long flags;
	unsigned char chunk[line6->max_packet_size];
	int req, done;

	spin_lock_irqsave(&line6->line6midi->midi_transmit_lock, flags);

	for (;;) {
		req = min(line6_midibuf_bytes_free(mb), line6->max_packet_size);
		done = snd_rawmidi_transmit_peek(substream, chunk, req);

		if (done == 0)
			break;

#ifdef CONFIG_LINE6_USB_DUMP_MIDI
		line6_write_hexdump(line6, 's', chunk, done);
#endif
		line6_midibuf_write(mb, chunk, done);
		snd_rawmidi_transmit_ack(substream, done);
	}

	for (;;) {
		done = line6_midibuf_read(mb, chunk, line6->max_packet_size);

		if (done == 0)
			break;

		if (line6_midibuf_skip_message
		    (mb, line6midi->midi_mask_transmit))
			continue;

		send_midi_async(line6, chunk, done);
	}

	spin_unlock_irqrestore(&line6->line6midi->midi_transmit_lock, flags);
}

/*
	Notification of completion of MIDI transmission.
*/
static void midi_sent(struct urb *urb)
{
	unsigned long flags;
	int status;
	int num;
	struct usb_line6 *line6 = (struct usb_line6 *)urb->context;

	status = urb->status;
	kfree(urb->transfer_buffer);
	usb_free_urb(urb);

	if (status == -ESHUTDOWN)
		return;

	spin_lock_irqsave(&line6->line6midi->send_urb_lock, flags);
	num = --line6->line6midi->num_active_send_urbs;

	if (num == 0) {
		line6_midi_transmit(line6->line6midi->substream_transmit);
		num = line6->line6midi->num_active_send_urbs;
	}

	if (num == 0)
		wake_up(&line6->line6midi->send_wait);

	spin_unlock_irqrestore(&line6->line6midi->send_urb_lock, flags);
}

/*
	Send an asynchronous MIDI message.
	Assumes that line6->line6midi->send_urb_lock is held
	(i.e., this function is serialized).
*/
static int send_midi_async(struct usb_line6 *line6, unsigned char *data,
			   int length)
{
	struct urb *urb;
	int retval;
	unsigned char *transfer_buffer;

	urb = usb_alloc_urb(0, GFP_ATOMIC);

	if (urb == NULL) {
		dev_err(line6->ifcdev, "Out of memory\n");
		return -ENOMEM;
	}
#ifdef CONFIG_LINE6_USB_DUMP_CTRL
	line6_write_hexdump(line6, 'S', data, length);
#endif

	transfer_buffer = kmalloc(length, GFP_ATOMIC);

	if (transfer_buffer == NULL) {
		usb_free_urb(urb);
		dev_err(line6->ifcdev, "Out of memory\n");
		return -ENOMEM;
	}

	memcpy(transfer_buffer, data, length);
	usb_fill_int_urb(urb, line6->usbdev,
			 usb_sndbulkpipe(line6->usbdev,
					 line6->ep_control_write),
			 transfer_buffer, length, midi_sent, line6,
			 line6->interval);
	urb->actual_length = 0;
	retval = usb_submit_urb(urb, GFP_ATOMIC);

	if (retval < 0) {
		dev_err(line6->ifcdev, "usb_submit_urb failed\n");
		usb_free_urb(urb);
		return -EINVAL;
	}

	++line6->line6midi->num_active_send_urbs;

	switch (line6->usbdev->descriptor.idProduct) {
	case LINE6_DEVID_BASSPODXT:
	case LINE6_DEVID_BASSPODXTLIVE:
	case LINE6_DEVID_BASSPODXTPRO:
	case LINE6_DEVID_PODXT:
	case LINE6_DEVID_PODXTLIVE:
	case LINE6_DEVID_PODXTPRO:
	case LINE6_DEVID_POCKETPOD:
		line6_pod_midi_postprocess((struct usb_line6_pod *)line6, data,
					   length);
		break;

	case LINE6_DEVID_VARIAX:
	case LINE6_DEVID_PODHD300:
		break;

	default:
		MISSING_CASE;
	}

	return 0;
}

static int line6_midi_output_open(struct snd_rawmidi_substream *substream)
{
	return 0;
}

static int line6_midi_output_close(struct snd_rawmidi_substream *substream)
{
	return 0;
}

static void line6_midi_output_trigger(struct snd_rawmidi_substream *substream,
				      int up)
{
	unsigned long flags;
	struct usb_line6 *line6 =
	    line6_rawmidi_substream_midi(substream)->line6;

	line6->line6midi->substream_transmit = substream;
	spin_lock_irqsave(&line6->line6midi->send_urb_lock, flags);

	if (line6->line6midi->num_active_send_urbs == 0)
		line6_midi_transmit(substream);

	spin_unlock_irqrestore(&line6->line6midi->send_urb_lock, flags);
}

static void line6_midi_output_drain(struct snd_rawmidi_substream *substream)
{
	struct usb_line6 *line6 =
	    line6_rawmidi_substream_midi(substream)->line6;
	struct snd_line6_midi *midi = line6->line6midi;
	wait_event_interruptible(midi->send_wait,
				 midi->num_active_send_urbs == 0);
}

static int line6_midi_input_open(struct snd_rawmidi_substream *substream)
{
	return 0;
}

static int line6_midi_input_close(struct snd_rawmidi_substream *substream)
{
	return 0;
}

static void line6_midi_input_trigger(struct snd_rawmidi_substream *substream,
				     int up)
{
	struct usb_line6 *line6 =
	    line6_rawmidi_substream_midi(substream)->line6;

	if (up)
		line6->line6midi->substream_receive = substream;
	else
		line6->line6midi->substream_receive = 0;
}

static struct snd_rawmidi_ops line6_midi_output_ops = {
	.open = line6_midi_output_open,
	.close = line6_midi_output_close,
	.trigger = line6_midi_output_trigger,
	.drain = line6_midi_output_drain,
};

static struct snd_rawmidi_ops line6_midi_input_ops = {
	.open = line6_midi_input_open,
	.close = line6_midi_input_close,
	.trigger = line6_midi_input_trigger,
};

/*
	Cleanup the Line6 MIDI device.
*/
static void line6_cleanup_midi(struct snd_rawmidi *rmidi)
{
}

/* Create a MIDI device */
static int snd_line6_new_midi(struct snd_line6_midi *line6midi)
{
	struct snd_rawmidi *rmidi;
	int err;

	err = snd_rawmidi_new(line6midi->line6->card, "Line6 MIDI", 0, 1, 1,
			      &rmidi);
	if (err < 0)
		return err;

	rmidi->private_data = line6midi;
	rmidi->private_free = line6_cleanup_midi;
	strcpy(rmidi->id, line6midi->line6->properties->id);
	strcpy(rmidi->name, line6midi->line6->properties->name);

	rmidi->info_flags =
	    SNDRV_RAWMIDI_INFO_OUTPUT |
	    SNDRV_RAWMIDI_INFO_INPUT | SNDRV_RAWMIDI_INFO_DUPLEX;

	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
			    &line6_midi_output_ops);
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT,
			    &line6_midi_input_ops);
	return 0;
}

/*
	"read" request on "midi_mask_transmit" special file.
*/
static ssize_t midi_get_midi_mask_transmit(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6 *line6 = usb_get_intfdata(interface);
	return sprintf(buf, "%d\n", line6->line6midi->midi_mask_transmit);
}

/*
	"write" request on "midi_mask" special file.
*/
static ssize_t midi_set_midi_mask_transmit(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6 *line6 = usb_get_intfdata(interface);
	unsigned long value;
	int ret;

	ret = strict_strtoul(buf, 10, &value);
	if (ret)
		return ret;

	line6->line6midi->midi_mask_transmit = value;
	return count;
}

/*
	"read" request on "midi_mask_receive" special file.
*/
static ssize_t midi_get_midi_mask_receive(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6 *line6 = usb_get_intfdata(interface);
	return sprintf(buf, "%d\n", line6->line6midi->midi_mask_receive);
}

/*
	"write" request on "midi_mask" special file.
*/
static ssize_t midi_set_midi_mask_receive(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6 *line6 = usb_get_intfdata(interface);
	unsigned long value;
	int ret;

	ret = strict_strtoul(buf, 10, &value);
	if (ret)
		return ret;

	line6->line6midi->midi_mask_receive = value;
	return count;
}

static DEVICE_ATTR(midi_mask_transmit, S_IWUSR | S_IRUGO,
		   midi_get_midi_mask_transmit, midi_set_midi_mask_transmit);
static DEVICE_ATTR(midi_mask_receive, S_IWUSR | S_IRUGO,
		   midi_get_midi_mask_receive, midi_set_midi_mask_receive);

/* MIDI device destructor */
static int snd_line6_midi_free(struct snd_device *device)
{
	struct snd_line6_midi *line6midi = device->device_data;
	device_remove_file(line6midi->line6->ifcdev,
			   &dev_attr_midi_mask_transmit);
	device_remove_file(line6midi->line6->ifcdev,
			   &dev_attr_midi_mask_receive);
	line6_midibuf_destroy(&line6midi->midibuf_in);
	line6_midibuf_destroy(&line6midi->midibuf_out);
	return 0;
}

/*
	Initialize the Line6 MIDI subsystem.
*/
int line6_init_midi(struct usb_line6 *line6)
{
	static struct snd_device_ops midi_ops = {
		.dev_free = snd_line6_midi_free,
	};

	int err;
	struct snd_line6_midi *line6midi;

	if (!(line6->properties->capabilities & LINE6_BIT_CONTROL)) {
		/* skip MIDI initialization and report success */
		return 0;
	}

	line6midi = kzalloc(sizeof(struct snd_line6_midi), GFP_KERNEL);

	if (line6midi == NULL)
		return -ENOMEM;

	err = line6_midibuf_init(&line6midi->midibuf_in, MIDI_BUFFER_SIZE, 0);
	if (err < 0) {
		kfree(line6midi);
		return err;
	}

	err = line6_midibuf_init(&line6midi->midibuf_out, MIDI_BUFFER_SIZE, 1);
	if (err < 0) {
		kfree(line6midi->midibuf_in.buf);
		kfree(line6midi);
		return err;
	}

	line6midi->line6 = line6;
	line6midi->midi_mask_transmit = 1;
	line6midi->midi_mask_receive = 4;
	line6->line6midi = line6midi;

	err = snd_device_new(line6->card, SNDRV_DEV_RAWMIDI, line6midi,
			     &midi_ops);
	if (err < 0)
		return err;

	snd_card_set_dev(line6->card, line6->ifcdev);

	err = snd_line6_new_midi(line6midi);
	if (err < 0)
		return err;

	err = device_create_file(line6->ifcdev, &dev_attr_midi_mask_transmit);
	if (err < 0)
		return err;

	err = device_create_file(line6->ifcdev, &dev_attr_midi_mask_receive);
	if (err < 0)
		return err;

	init_waitqueue_head(&line6midi->send_wait);
	spin_lock_init(&line6midi->send_urb_lock);
	spin_lock_init(&line6midi->midi_transmit_lock);
	return 0;
}
