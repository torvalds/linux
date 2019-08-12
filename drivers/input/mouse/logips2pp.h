/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Logitech PS/2++ mouse driver header
 *
 * Copyright (c) 2003 Vojtech Pavlik <vojtech@suse.cz>
 */

#ifndef _LOGIPS2PP_H
#define _LOGIPS2PP_H

#ifdef CONFIG_MOUSE_PS2_LOGIPS2PP
int ps2pp_detect(struct psmouse *psmouse, bool set_properties);
#else
static inline int ps2pp_detect(struct psmouse *psmouse, bool set_properties)
{
	return -ENOSYS;
}
#endif /* CONFIG_MOUSE_PS2_LOGIPS2PP */

#endif
