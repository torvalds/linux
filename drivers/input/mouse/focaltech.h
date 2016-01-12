/*
 * Focaltech TouchPad PS/2 mouse driver
 *
 * Copyright (c) 2014 Red Hat Inc.
 * Copyright (c) 2014 Mathias Gottschlag <mgottschlag@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Red Hat authors:
 *
 * Hans de Goede <hdegoede@redhat.com>
 */

#ifndef _FOCALTECH_H
#define _FOCALTECH_H

int focaltech_detect(struct psmouse *psmouse, bool set_properties);

#ifdef CONFIG_MOUSE_PS2_FOCALTECH
int focaltech_init(struct psmouse *psmouse);
#else
static inline int focaltech_init(struct psmouse *psmouse)
{
	return -ENOSYS;
}
#endif

#endif
