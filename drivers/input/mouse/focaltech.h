/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Focaltech TouchPad PS/2 mouse driver
 *
 * Copyright (c) 2014 Red Hat Inc.
 * Copyright (c) 2014 Mathias Gottschlag <mgottschlag@gmail.com>
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
