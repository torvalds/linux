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

/*
	PCM interface to POD series devices.
*/

#ifndef PCM_H
#define PCM_H


#include <sound/pcm.h>

#include "driver.h"
#include "usbdefs.h"


/* number of URBs */
#define LINE6_ISO_BUFFERS	8

/* number of USB frames per URB */
#define LINE6_ISO_PACKETS	2

/* in a "full speed" device (such as the PODxt Pro) this means 1ms */
#define LINE6_ISO_INTERVAL	1

/* this should be queried dynamically from the USB interface! */
#define LINE6_ISO_PACKET_SIZE_MAX	252


/*
	Extract the messaging device from the substream instance
*/
#define s2m(s)	(((struct snd_line6_pcm *) \
		   snd_pcm_substream_chip(s))->line6->ifcdev)


enum {
	BIT_RUNNING_PLAYBACK,
	BIT_RUNNING_CAPTURE,
	BIT_PAUSE_PLAYBACK,
	BIT_PREPARED
};

struct line6_pcm_properties {
	struct snd_pcm_hardware snd_line6_playback_hw, snd_line6_capture_hw;
	struct snd_pcm_hw_constraint_ratdens snd_line6_rates;
	int bytes_per_frame;
};

struct snd_line6_pcm {
	/**
		 Pointer back to the Line6 driver data structure.
	*/
	struct usb_line6 *line6;

	/**
		 Properties.
	*/
	struct line6_pcm_properties *properties;

	/**
		 ALSA pcm stream
	*/
	struct snd_pcm *pcm;

	/**
		 URBs for audio playback.
	*/
	struct urb *urb_audio_out[LINE6_ISO_BUFFERS];

	/**
		 URBs for audio capture.
	*/
	struct urb *urb_audio_in[LINE6_ISO_BUFFERS];

	/**
		 Temporary buffer to hold data when playback buffer wraps.
	*/
	unsigned char *wrap_out;

	/**
		 Temporary buffer for capture.
		 Since the packet size is not known in advance, this buffer is
		 large enough to store maximum size packets.
	*/
	unsigned char *buffer_in;

	/**
		 Free frame position in the playback buffer.
	*/
	snd_pcm_uframes_t pos_out;

	/**
		 Count processed bytes for playback.
		 This is modulo period size (to determine when a period is
		 finished).
	*/
	unsigned bytes_out;

	/**
		 Counter to create desired playback sample rate.
	*/
	unsigned count_out;

	/**
		 Playback period size in bytes
	*/
	unsigned period_out;

	/**
		 Processed frame position in the playback buffer.
		 The contents of the output ring buffer have been consumed by
		 the USB subsystem (i.e., sent to the USB device) up to this
		 position.
	*/
	snd_pcm_uframes_t pos_out_done;

	/**
		 Count processed bytes for capture.
		 This is modulo period size (to determine when a period is
		 finished).
	*/
	unsigned bytes_in;

	/**
		 Counter to create desired capture sample rate.
	*/
	unsigned count_in;

	/**
		 Capture period size in bytes
	*/
	unsigned period_in;

	/**
		 Processed frame position in the capture buffer.
		 The contents of the output ring buffer have been consumed by
		 the USB subsystem (i.e., sent to the USB device) up to this
		 position.
	*/
	snd_pcm_uframes_t pos_in_done;

	/**
		 Bit mask of active playback URBs.
	*/
	unsigned long active_urb_out;

	/**
		 Maximum size of USB packet.
	*/
	int max_packet_size;

	/**
		 USB endpoint for listening to audio data.
	*/
	int ep_audio_read;

	/**
		 USB endpoint for writing audio data.
	*/
	int ep_audio_write;

	/**
		 Bit mask of active capture URBs.
	*/
	unsigned long active_urb_in;

	/**
		 Bit mask of playback URBs currently being unlinked.
	*/
	unsigned long unlink_urb_out;

	/**
		 Bit mask of capture URBs currently being unlinked.
	*/
	unsigned long unlink_urb_in;

	/**
		 Spin lock to protect updates of the playback buffer positions (not
		 contents!)
	*/
	spinlock_t lock_audio_out;

	/**
		 Spin lock to protect updates of the capture buffer positions (not
		 contents!)
	*/
	spinlock_t lock_audio_in;

	/**
		 Spin lock to protect trigger.
	*/
	spinlock_t lock_trigger;

	/**
		 PCM playback volume (left and right).
	*/
	int volume[2];

	/**
		 Several status bits (see BIT_*).
	*/
	unsigned long flags;
};


extern int line6_init_pcm(struct usb_line6 *line6,
			  struct line6_pcm_properties *properties);
extern int snd_line6_trigger(struct snd_pcm_substream *substream, int cmd);
extern int snd_line6_prepare(struct snd_pcm_substream *substream);


#endif
