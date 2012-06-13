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

struct alps_model_info {
        unsigned char signature[3];
	unsigned char command_mode_resp; /* v3/v4 only */
	unsigned char proto_version;
        unsigned char byte0, mask0;
        unsigned char flags;
};

struct alps_nibble_commands {
	int command;
	unsigned char data;
};

struct alps_data {
	struct input_dev *dev2;		/* Relative device */
	char phys[32];			/* Phys */
	const struct alps_model_info *i;/* Info */
	const struct alps_nibble_commands *nibble_commands;
	int addr_command;		/* Command to set register address */
	int prev_fin;			/* Finger bit from previous packet */
	int multi_packet;		/* Multi-packet data in progress */
	unsigned char multi_data[6];	/* Saved multi-packet data */
	int x1, x2, y1, y2;		/* Coordinates from last MT report */
	int fingers;			/* Number of fingers from MT report */
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
