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

/*
 * The Focaltech PS/2 touchpad protocol is unknown. This drivers deals with
 * detection only, to avoid further detection attempts confusing the touchpad
 * this way it at least works in PS/2 mouse compatibility mode.
 */

#include <linux/device.h>
#include <linux/libps2.h>
#include "psmouse.h"

static const char * const focaltech_pnp_ids[] = {
	"FLT0101",
	"FLT0102",
	"FLT0103",
	NULL
};

int focaltech_detect(struct psmouse *psmouse, bool set_properties)
{
	if (!psmouse_matches_pnp_id(psmouse, focaltech_pnp_ids))
		return -ENODEV;

	if (set_properties) {
		psmouse->vendor = "FocalTech";
		psmouse->name = "FocalTech Touchpad in mouse emulation mode";
	}

	return 0;
}

int focaltech_init(struct psmouse *psmouse)
{
	ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_RESET_DIS);
	psmouse_reset(psmouse);

	return 0;
}
