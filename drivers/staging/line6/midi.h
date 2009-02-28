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

#ifndef MIDI_H
#define MIDI_H


#include <sound/rawmidi.h>

#include "midibuf.h"


#define MIDI_BUFFER_SIZE 1024


struct snd_line6_midi {
	/**
		 Pointer back to the Line6 driver data structure.
	*/
	struct usb_line6 *line6;

	/**
		 MIDI substream for receiving (or NULL if not active).
	*/
	struct snd_rawmidi_substream *substream_receive;

	/**
		 MIDI substream for transmitting (or NULL if not active).
	*/
	struct snd_rawmidi_substream *substream_transmit;

	/**
		 Number of currently active MIDI send URBs.
	*/
	int num_active_send_urbs;

	/**
		 Spin lock to protect updates of send_urb.
	*/
	spinlock_t send_urb_lock;

	/**
		 Spin lock to protect MIDI buffer handling.
	*/
	spinlock_t midi_transmit_lock;

	/**
		 Wait queue for MIDI transmission.
	*/
	wait_queue_head_t send_wait;

	/**
		 Bit mask for output MIDI channels.
	*/
	int midi_mask_transmit;

	/**
		 Bit mask for input MIDI channels.
	*/
	int midi_mask_receive;

	/**
		 Buffer for incoming MIDI stream.
	*/
	struct MidiBuffer midibuf_in;

	/**
		 Buffer for outgoing MIDI stream.
	*/
	struct MidiBuffer midibuf_out;
};


extern int line6_init_midi(struct usb_line6 *line6);
extern void line6_midi_receive(struct usb_line6 *line6, unsigned char *data,
			       int length);


#endif
