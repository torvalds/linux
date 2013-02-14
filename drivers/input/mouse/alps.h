/*
 * ALPS touchpad PS/2 mouse driver
 *
 * Copyright (c) 2003 Peter Osterlund <petero2@telia.com>
 * Copyright (c) 2005 Vojtech Pavlik <vojtech@suse.cz>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#ifndef _ALPS_H
#define _ALPS_H

#define ALPS_PROTO_V1	0
#define ALPS_PROTO_V2	1
#define ALPS_PROTO_V3	2
#define ALPS_PROTO_V4	3

/**
 * struct alps_model_info - touchpad ID table
 * @signature: E7 response string to match.
 * @command_mode_resp: For V3/V4 touchpads, the final byte of the EC response
 *   (aka command mode response) identifies the firmware minor version.  This
 *   can be used to distinguish different hardware models which are not
 *   uniquely identifiable through their E7 responses.
 * @proto_version: Indicates V1/V2/V3/...
 * @byte0: Helps figure out whether a position report packet matches the
 *   known format for this model.  The first byte of the report, ANDed with
 *   mask0, should match byte0.
 * @mask0: The mask used to check the first byte of the report.
 * @flags: Additional device capabilities (passthrough port, trackstick, etc.).
 *
 * Many (but not all) ALPS touchpads can be identified by looking at the
 * values returned in the "E7 report" and/or the "EC report."  This table
 * lists a number of such touchpads.
 */
struct alps_model_info {
	unsigned char signature[3];
	unsigned char command_mode_resp;
	unsigned char proto_version;
	unsigned char byte0, mask0;
	unsigned char flags;
};

/**
 * struct alps_nibble_commands - encodings for register accesses
 * @command: PS/2 command used for the nibble
 * @data: Data supplied as an argument to the PS/2 command, if applicable
 *
 * The ALPS protocol uses magic sequences to transmit binary data to the
 * touchpad, as it is generally not OK to send arbitrary bytes out the
 * PS/2 port.  Each of the sequences in this table sends one nibble of the
 * register address or (write) data.  Different versions of the ALPS protocol
 * use slightly different encodings.
 */
struct alps_nibble_commands {
	int command;
	unsigned char data;
};

/**
 * struct alps_data - private data structure for the ALPS driver
 * @dev2: "Relative" device used to report trackstick or mouse activity.
 * @phys: Physical path for the relative device.
 * @i: Information on the detected touchpad model.
 * @nibble_commands: Command mapping used for touchpad register accesses.
 * @addr_command: Command used to tell the touchpad that a register address
 *   follows.
 * @prev_fin: Finger bit from previous packet.
 * @multi_packet: Multi-packet data in progress.
 * @multi_data: Saved multi-packet data.
 * @x1: First X coordinate from last MT report.
 * @x2: Second X coordinate from last MT report.
 * @y1: First Y coordinate from last MT report.
 * @y2: Second Y coordinate from last MT report.
 * @fingers: Number of fingers from last MT report.
 * @quirks: Bitmap of ALPS_QUIRK_*.
 * @timer: Timer for flushing out the final report packet in the stream.
 */
struct alps_data {
	struct input_dev *dev2;
	char phys[32];
	const struct alps_model_info *i;
	const struct alps_nibble_commands *nibble_commands;
	int addr_command;
	int prev_fin;
	int multi_packet;
	unsigned char multi_data[6];
	int x1, x2, y1, y2;
	int fingers;
	u8 quirks;
	struct timer_list timer;
};

#define ALPS_QUIRK_TRACKSTICK_BUTTONS	1 /* trakcstick buttons in trackstick packet */

#ifdef CONFIG_MOUSE_PS2_ALPS
int alps_detect(struct psmouse *psmouse, bool set_properties);
int alps_init(struct psmouse *psmouse);
#else
inline int alps_detect(struct psmouse *psmouse, bool set_properties)
{
	return -ENOSYS;
}
inline int alps_init(struct psmouse *psmouse)
{
	return -ENOSYS;
}
#endif /* CONFIG_MOUSE_PS2_ALPS */

#endif
