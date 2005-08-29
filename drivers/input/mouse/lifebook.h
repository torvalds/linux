/*
 * Fujitsu B-series Lifebook PS/2 TouchScreen driver
 *
 * Copyright (c) 2005 Vojtech Pavlik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#ifndef _LIFEBOOK_H
#define _LIFEBOOK_H

int lifebook_detect(struct psmouse *psmouse, int set_properties);
int lifebook_init(struct psmouse *psmouse);

#endif
