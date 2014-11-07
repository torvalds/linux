/*
 * Focaltech TouchPad PS/2 mouse driver
 *
 * Copyright (c) 2014 Red Hat Inc.
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
int focaltech_init(struct psmouse *psmouse);

#endif
