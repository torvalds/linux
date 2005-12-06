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

int alps_detect(struct psmouse *psmouse, int set_properties);
int alps_init(struct psmouse *psmouse);

struct alps_model_info {
        unsigned char signature[3];
        unsigned char byte0, mask0;
        unsigned char flags;
};

struct alps_data {
	struct input_dev *dev2;		/* Relative device */
	char name[32];			/* Name */
	char phys[32];			/* Phys */
	struct alps_model_info *i; 	/* Info */
	int prev_fin;			/* Finger bit from previous packet */
};

#endif
