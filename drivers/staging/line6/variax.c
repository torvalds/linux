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
#include "driver.h"
#include "variax.h"

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
static void variax_startup2(unsigned long data);
static void variax_startup4(unsigned long data);
static void variax_startup5(unsigned long data);

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

	CHECK_STARTUP_PROGRESS(variax->startup_progress, VARIAX_STARTUP_SETUP);

	/* ALSA audio interface: */
	line6_register_audio(&variax->line6);
}

/*
	Process a completely received message.
*/
void line6_variax_process_message(struct usb_line6_variax *variax)
{
	const unsigned char *buf = variax->line6.buffer_message;

	switch (buf[0]) {
	case LINE6_PARAM_CHANGE | LINE6_CHANNEL_HOST:
		break;

	case LINE6_PROGRAM_CHANGE | LINE6_CHANNEL_DEVICE:
	case LINE6_PROGRAM_CHANGE | LINE6_CHANNEL_HOST:
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
					line6_dump_request_async
					    (&variax->dumpreq, &variax->line6,
					     1, VARIAX_DUMP_PASS2);
					break;

				case VARIAX_DUMP_PASS2:
					line6_dump_request_async
					    (&variax->dumpreq, &variax->line6,
					     2, VARIAX_DUMP_PASS3);
				}
			} else {
				dev_dbg(variax->line6.ifcdev,
					"illegal length %d of model data\n",
					variax->line6.message_length);
				line6_dump_finished(&variax->dumpreq);
			}
		} else if (memcmp(buf + 1, variax_request_bank + 1,
				  sizeof(variax_request_bank) - 2) == 0) {
			line6_dump_finished(&variax->dumpreq);
			variax_startup6(variax);
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
		dev_dbg(variax->line6.ifcdev,
			"Variax: unknown message %02X\n", buf[0]);
	}
}

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
	if (interface == NULL)
		return;

	variax_destruct(interface);
}
