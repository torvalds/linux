/* SPDX-License-Identifier: GPL-2.0 */
#ifndef DDK750_MODE_H__
#define DDK750_MODE_H__

#include "ddk750_chip.h"

enum spolarity {
	POS = 0, /* positive */
	NEG, /* negative */
};

struct mode_parameter {
	/* Horizontal timing. */
	unsigned long horizontal_total;
	unsigned long horizontal_display_end;
	unsigned long horizontal_sync_start;
	unsigned long horizontal_sync_width;
	enum spolarity horizontal_sync_polarity;

	/* Vertical timing. */
	unsigned long vertical_total;
	unsigned long vertical_display_end;
	unsigned long vertical_sync_start;
	unsigned long vertical_sync_height;
	enum spolarity vertical_sync_polarity;

	/* Refresh timing. */
	unsigned long pixel_clock;
	unsigned long horizontal_frequency;
	unsigned long vertical_frequency;

	/* Clock Phase. This clock phase only applies to Panel. */
	enum spolarity clock_phase_polarity;
};

int ddk750_setModeTiming(struct mode_parameter *parm, clock_type_t clock);
#endif
