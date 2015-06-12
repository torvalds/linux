/*
 * Driver for Virtual PS/2 Mouse on VMware and QEMU hypervisors.
 *
 * Copyright (C) 2014, VMware, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#ifndef _VMMOUSE_H
#define _VMMOUSE_H

#ifdef CONFIG_MOUSE_PS2_VMMOUSE
#define VMMOUSE_PSNAME  "VirtualPS/2"

int vmmouse_detect(struct psmouse *psmouse, bool set_properties);
int vmmouse_init(struct psmouse *psmouse);
#else
static inline int vmmouse_detect(struct psmouse *psmouse, bool set_properties)
{
	return -ENOSYS;
}
static inline int vmmouse_init(struct psmouse *psmouse)
{
	return -ENOSYS;
}
#endif

#endif
