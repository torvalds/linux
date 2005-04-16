/*
 *  PID Force feedback support for hid devices.
 *
 *  Copyright (c) 2002 Rodrigo Damazio.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so by
 * e-mail - mail your message to <rdamazio@lsi.usp.br>
 */

#define FF_EFFECTS_MAX 64

#define FF_PID_FLAGS_USED	1	/* If the effect exists */
#define FF_PID_FLAGS_UPDATING	2	/* If the effect is being updated */
#define FF_PID_FLAGS_PLAYING	3	/* If the effect is currently being played */

#define FF_PID_FALSE	0
#define FF_PID_TRUE	1

struct hid_pid_effect {
	unsigned long flags;
	pid_t owner;
	unsigned int device_id;	/* The device-assigned ID */
	struct ff_effect effect;
};

struct hid_ff_pid {
	struct hid_device *hid;
	unsigned long gain;

	struct urb *urbffout;
	struct usb_ctrlrequest ffcr;
	spinlock_t lock;

	unsigned char ctrl_buffer[8];

	struct hid_pid_effect effects[FF_EFFECTS_MAX];
};

/*
 * Constants from the PID usage table (still far from complete)
 */

#define FF_PID_USAGE_BLOCK_LOAD 	0x89UL
#define FF_PID_USAGE_BLOCK_FREE		0x90UL
#define FF_PID_USAGE_NEW_EFFECT 	0xABUL
#define FF_PID_USAGE_POOL_REPORT	0x7FUL
