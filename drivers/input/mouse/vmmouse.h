/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for Virtual PS/2 Mouse on VMware and QEMU hypervisors.
 *
 * Copyright (C) 2014, VMware, Inc. All Rights Reserved.
 */

#ifndef _VMMOUSE_H
#define _VMMOUSE_H

#define VMMOUSE_PSNAME  "VirtualPS/2"

int vmmouse_detect(struct psmouse *psmouse, bool set_properties);
int vmmouse_init(struct psmouse *psmouse);

#endif
