/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _PIXART_PS2_H
#define _PIXART_PS2_H

#include "psmouse.h"

#define PIXART_PAD_WIDTH		1023
#define PIXART_PAD_HEIGHT		579
#define PIXART_MAX_FINGERS		4

#define PIXART_CMD_REPORT_FORMAT	0x01d8
#define PIXART_CMD_SWITCH_PROTO		0x00de

#define PIXART_MODE_REL			0
#define PIXART_MODE_ABS			1

#define PIXART_TYPE_CLICKPAD		0
#define PIXART_TYPE_TOUCHPAD		1

#define CONTACT_CNT_MASK		GENMASK(6, 4)

#define SLOT_ID_MASK			GENMASK(2, 0)
#define ABS_Y_MASK			GENMASK(5, 4)
#define ABS_X_MASK			GENMASK(7, 6)

struct pixart_data {
	u8 mode;
	u8 type;
	int x_max;
	int y_max;
};

int pixart_detect(struct psmouse *psmouse, bool set_properties);
int pixart_init(struct psmouse *psmouse);

#endif  /* _PIXART_PS2_H */
