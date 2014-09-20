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

#include "midibuf.h"

static int midibuf_message_length(unsigned char code)
{
	int message_length;

	if (code < 0x80)
		message_length = -1;
	else if (code < 0xf0) {
		static const int length[] = { 3, 3, 3, 3, 2, 2, 3 };

		message_length = length[(code >> 4) - 8];
	} else {
		/*
		   Note that according to the MIDI specification 0xf2 is
		   the "Song Position Pointer", but this is used by Line6
		   to send sysex messages to the host.
		 */
		static const int length[] = { -1, 2, -1, 2, -1, -1, 1, 1, 1, 1,
			1, 1, 1, -1, 1, 1
		};
		message_length = length[code & 0x0f];
	}

	return message_length;
}

static int midibuf_is_empty(struct midi_buffer *this)
{
	return (this->pos_read == this->pos_write) && !this->full;
}

static int midibuf_is_full(struct midi_buffer *this)
{
	return this->full;
}

void line6_midibuf_reset(struct midi_buffer *this)
{
	this->pos_read = this->pos_write = this->full = 0;
	this->command_prev = -1;
}

int line6_midibuf_init(struct midi_buffer *this, int size, int split)
{
	this->buf = kmalloc(size, GFP_KERNEL);

	if (this->buf == NULL)
		return -ENOMEM;

	this->size = size;
	this->split = split;
	line6_midibuf_reset(this);
	return 0;
}

void line6_midibuf_status(struct midi_buffer *this)
{
	pr_debug("midibuf size=%d split=%d pos_read=%d pos_write=%d full=%d command_prev=%02x\n",
		 this->size, this->split, this->pos_read, this->pos_write,
		 this->full, this->command_prev);
}

int line6_midibuf_bytes_free(struct midi_buffer *this)
{
	return
	    midibuf_is_full(this) ?
	    0 :
	    (this->pos_read - this->pos_write + this->size - 1) % this->size +
	    1;
}

int line6_midibuf_bytes_used(struct midi_buffer *this)
{
	return
	    midibuf_is_empty(this) ?
	    0 :
	    (this->pos_write - this->pos_read + this->size - 1) % this->size +
	    1;
}

int line6_midibuf_write(struct midi_buffer *this, unsigned char *data,
			int length)
{
	int bytes_free;
	int length1, length2;
	int skip_active_sense = 0;

	if (midibuf_is_full(this) || (length <= 0))
		return 0;

	/* skip trailing active sense */
	if (data[length - 1] == 0xfe) {
		--length;
		skip_active_sense = 1;
	}

	bytes_free = line6_midibuf_bytes_free(this);

	if (length > bytes_free)
		length = bytes_free;

	if (length > 0) {
		length1 = this->size - this->pos_write;

		if (length < length1) {
			/* no buffer wraparound */
			memcpy(this->buf + this->pos_write, data, length);
			this->pos_write += length;
		} else {
			/* buffer wraparound */
			length2 = length - length1;
			memcpy(this->buf + this->pos_write, data, length1);
			memcpy(this->buf, data + length1, length2);
			this->pos_write = length2;
		}

		if (this->pos_write == this->pos_read)
			this->full = 1;
	}

	return length + skip_active_sense;
}

int line6_midibuf_read(struct midi_buffer *this, unsigned char *data,
		       int length)
{
	int bytes_used;
	int length1, length2;
	int command;
	int midi_length;
	int repeat = 0;
	int i;

	/* we need to be able to store at least a 3 byte MIDI message */
	if (length < 3)
		return -EINVAL;

	if (midibuf_is_empty(this))
		return 0;

	bytes_used = line6_midibuf_bytes_used(this);

	if (length > bytes_used)
		length = bytes_used;

	length1 = this->size - this->pos_read;

	/* check MIDI command length */
	command = this->buf[this->pos_read];

	if (command & 0x80) {
		midi_length = midibuf_message_length(command);
		this->command_prev = command;
	} else {
		if (this->command_prev > 0) {
			int midi_length_prev =
			    midibuf_message_length(this->command_prev);

			if (midi_length_prev > 0) {
				midi_length = midi_length_prev - 1;
				repeat = 1;
			} else
				midi_length = -1;
		} else
			midi_length = -1;
	}

	if (midi_length < 0) {
		/* search for end of message */
		if (length < length1) {
			/* no buffer wraparound */
			for (i = 1; i < length; ++i)
				if (this->buf[this->pos_read + i] & 0x80)
					break;

			midi_length = i;
		} else {
			/* buffer wraparound */
			length2 = length - length1;

			for (i = 1; i < length1; ++i)
				if (this->buf[this->pos_read + i] & 0x80)
					break;

			if (i < length1)
				midi_length = i;
			else {
				for (i = 0; i < length2; ++i)
					if (this->buf[i] & 0x80)
						break;

				midi_length = length1 + i;
			}
		}

		if (midi_length == length)
			midi_length = -1;	/* end of message not found */
	}

	if (midi_length < 0) {
		if (!this->split)
			return 0;	/* command is not yet complete */
	} else {
		if (length < midi_length)
			return 0;	/* command is not yet complete */

		length = midi_length;
	}

	if (length < length1) {
		/* no buffer wraparound */
		memcpy(data + repeat, this->buf + this->pos_read, length);
		this->pos_read += length;
	} else {
		/* buffer wraparound */
		length2 = length - length1;
		memcpy(data + repeat, this->buf + this->pos_read, length1);
		memcpy(data + repeat + length1, this->buf, length2);
		this->pos_read = length2;
	}

	if (repeat)
		data[0] = this->command_prev;

	this->full = 0;
	return length + repeat;
}

int line6_midibuf_ignore(struct midi_buffer *this, int length)
{
	int bytes_used = line6_midibuf_bytes_used(this);

	if (length > bytes_used)
		length = bytes_used;

	this->pos_read = (this->pos_read + length) % this->size;
	this->full = 0;
	return length;
}

int line6_midibuf_skip_message(struct midi_buffer *this, unsigned short mask)
{
	int cmd = this->command_prev;

	if ((cmd >= 0x80) && (cmd < 0xf0))
		if ((mask & (1 << (cmd & 0x0f))) == 0)
			return 1;

	return 0;
}

void line6_midibuf_destroy(struct midi_buffer *this)
{
	kfree(this->buf);
	this->buf = NULL;
}
