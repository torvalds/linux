/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Fujitsu B-series Lifebook PS/2 TouchScreen driver
 *
 * Copyright (c) 2005 Vojtech Pavlik
 */

#ifndef _LIFEBOOK_H
#define _LIFEBOOK_H

#ifdef CONFIG_MOUSE_PS2_LIFEBOOK
void lifebook_module_init(void);
int lifebook_detect(struct psmouse *psmouse, bool set_properties);
int lifebook_init(struct psmouse *psmouse);
#else
static inline void lifebook_module_init(void)
{
}
static inline int lifebook_detect(struct psmouse *psmouse, bool set_properties)
{
	return -ENOSYS;
}
static inline int lifebook_init(struct psmouse *psmouse)
{
	return -ENOSYS;
}
#endif

#endif
