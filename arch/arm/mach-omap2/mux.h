/*
 * Copyright (C) 2009 Nokia
 * Copyright (C) 2009 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "mux34xx.h"

#define OMAP_MUX_TERMINATOR	0xffff

/* 34xx mux mode options for each pin. See TRM for options */
#define OMAP_MUX_MODE0      0
#define OMAP_MUX_MODE1      1
#define OMAP_MUX_MODE2      2
#define OMAP_MUX_MODE3      3
#define OMAP_MUX_MODE4      4
#define OMAP_MUX_MODE5      5
#define OMAP_MUX_MODE6      6
#define OMAP_MUX_MODE7      7

/* 24xx/34xx mux bit defines */
#define OMAP_PULL_ENA			(1 << 3)
#define OMAP_PULL_UP			(1 << 4)
#define OMAP_ALTELECTRICALSEL		(1 << 5)

/* 34xx specific mux bit defines */
#define OMAP_INPUT_EN			(1 << 8)
#define OMAP_OFF_EN			(1 << 9)
#define OMAP_OFFOUT_EN			(1 << 10)
#define OMAP_OFFOUT_VAL			(1 << 11)
#define OMAP_OFF_PULL_EN		(1 << 12)
#define OMAP_OFF_PULL_UP		(1 << 13)
#define OMAP_WAKEUP_EN			(1 << 14)

/* Active pin states */
#define OMAP_PIN_OUTPUT			0
#define OMAP_PIN_INPUT			OMAP_INPUT_EN
#define OMAP_PIN_INPUT_PULLUP		(OMAP_PULL_ENA | OMAP_INPUT_EN \
						| OMAP_PULL_UP)
#define OMAP_PIN_INPUT_PULLDOWN		(OMAP_PULL_ENA | OMAP_INPUT_EN)

/* Off mode states */
#define OMAP_PIN_OFF_NONE		0
#define OMAP_PIN_OFF_OUTPUT_HIGH	(OMAP_OFF_EN | OMAP_OFFOUT_EN \
						| OMAP_OFFOUT_VAL)
#define OMAP_PIN_OFF_OUTPUT_LOW		(OMAP_OFF_EN | OMAP_OFFOUT_EN)
#define OMAP_PIN_OFF_INPUT_PULLUP	(OMAP_OFF_EN | OMAP_OFF_PULL_EN \
						| OMAP_OFF_PULL_UP)
#define OMAP_PIN_OFF_INPUT_PULLDOWN	(OMAP_OFF_EN | OMAP_OFF_PULL_EN)
#define OMAP_PIN_OFF_WAKEUPENABLE	OMAP_WAKEUP_EN

#define OMAP_MODE_GPIO(x)	(((x) & OMAP_MUX_MODE7) == OMAP_MUX_MODE4)

/* Flags for omap_mux_init */
#define OMAP_PACKAGE_MASK		0xffff
#define OMAP_PACKAGE_CBP		4		/* 515-pin 0.40 0.50 */
#define OMAP_PACKAGE_CUS		3		/* 423-pin 0.65 */
#define OMAP_PACKAGE_CBB		2		/* 515-pin 0.40 0.50 */
#define OMAP_PACKAGE_CBC		1		/* 515-pin 0.50 0.65 */


#define OMAP_MUX_NR_MODES	8			/* Available modes */
#define OMAP_MUX_NR_SIDES	2			/* Bottom & top */

/**
 * struct omap_mux - data for omap mux register offset and it's value
 * @reg_offset:	mux register offset from the mux base
 * @gpio:	GPIO number
 * @muxnames:	available signal modes for a ball
 */
struct omap_mux {
	u16	reg_offset;
	u16	gpio;
#ifdef CONFIG_OMAP_MUX
	char	*muxnames[OMAP_MUX_NR_MODES];
#ifdef CONFIG_DEBUG_FS
	char	*balls[OMAP_MUX_NR_SIDES];
#endif
#endif
};

/**
 * struct omap_ball - data for balls on omap package
 * @reg_offset:	mux register offset from the mux base
 * @balls:	available balls on the package
 */
struct omap_ball {
	u16	reg_offset;
	char	*balls[OMAP_MUX_NR_SIDES];
};

/**
 * struct omap_board_mux - data for initializing mux registers
 * @reg_offset:	mux register offset from the mux base
 * @mux_value:	desired mux value to set
 */
struct omap_board_mux {
	u16	reg_offset;
	u16	value;
};

#if defined(CONFIG_OMAP_MUX) && defined(CONFIG_ARCH_OMAP3)

/**
 * omap_mux_init_gpio - initialize a signal based on the GPIO number
 * @gpio:		GPIO number
 * @val:		Options for the mux register value
 */
int omap_mux_init_gpio(int gpio, int val);

/**
 * omap_mux_init_signal - initialize a signal based on the signal name
 * @muxname:		Mux name in mode0_name.signal_name format
 * @val:		Options for the mux register value
 */
int omap_mux_init_signal(char *muxname, int val);

#else

static inline int omap_mux_init_gpio(int gpio, int val)
{
	return 0;
}
static inline int omap_mux_init_signal(char *muxname, int val)
{
	return 0;
}

#endif

/**
 * omap_mux_get_gpio() - get mux register value based on GPIO number
 * @gpio:		GPIO number
 *
 */
u16 omap_mux_get_gpio(int gpio);

/**
 * omap_mux_set_gpio() - set mux register value based on GPIO number
 * @val:		New mux register value
 * @gpio:		GPIO number
 *
 */
void omap_mux_set_gpio(u16 val, int gpio);

/**
 * omap_mux_read() - read mux register
 * @mux_offset:		Offset of the mux register
 *
 */
u16 omap_mux_read(u16 mux_offset);

/**
 * omap_mux_write() - write mux register
 * @val:		New mux register value
 * @mux_offset:		Offset of the mux register
 *
 * This should be only needed for dynamic remuxing of non-gpio signals.
 */
void omap_mux_write(u16 val, u16 mux_offset);

/**
 * omap_mux_write_array() - write an array of mux registers
 * @board_mux:		Array of mux registers terminated by MAP_MUX_TERMINATOR
 *
 * This should be only needed for dynamic remuxing of non-gpio signals.
 */
void omap_mux_write_array(struct omap_board_mux *board_mux);

/**
 * omap3_mux_init() - initialize mux system with board specific set
 * @board_mux:		Board specific mux table
 * @flags:		OMAP package type used for the board
 */
int omap3_mux_init(struct omap_board_mux *board_mux, int flags);

/**
 * omap_mux_init - private mux init function, do not call
 */
int omap_mux_init(u32 mux_pbase, u32 mux_size,
				struct omap_mux *superset,
				struct omap_mux *package_subset,
				struct omap_board_mux *board_mux,
				struct omap_ball *package_balls);
