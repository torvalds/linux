/*
 * valkyriefb.h: Constants of all sorts for valkyriefb
 *
 *  Created 8 August 1998 by 
 *  Martin Costabel <costabel@wanadoo.fr> and Kevin Schoedel
 *
 * Vmode-switching changes and vmode 15/17 modifications created 29 August
 * 1998 by Barry K. Nathan <barryn@pobox.com>.
 * 
 * vmode 10 changed by Steven Borley <sjb@salix.demon.co.uk>, 14 mai 2000
 *
 * Ported to 68k Macintosh by David Huggins-Daines <dhd@debian.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Based directly on:
 *
 *  controlfb.h: Constants of all sorts for controlfb
 *  Copyright (C) 1998 Daniel Jacobowitz <dan@debian.org>
 *
 *  pmc-valkyrie.h: Console support for PowerMac "control" display adaptor.
 *  Copyright (C) 1997 Paul Mackerras.
 *
 *  pmc-valkyrie.c: Console support for PowerMac "control" display adaptor.
 *  Copyright (C) 1997 Paul Mackerras.
 *
 * and indirectly from:
 *
 *  pmc-control.h: Console support for PowerMac "control" display adaptor.
 *  Copyright (C) 1997 Paul Mackerras.
 *
 *  pmc-control.c: Console support for PowerMac "control" display adaptor.
 *  Copyright (C) 1996 Paul Mackerras.
 *
 *  platinumfb.c: Console support for PowerMac "platinum" display adaptor.
 *  Copyright (C) 1998 Jon Howell
 */

#ifdef CONFIG_MAC
/* Valkyrie registers are word-aligned on m68k */
#define VALKYRIE_REG_PADSIZE	3
#else
#define VALKYRIE_REG_PADSIZE	7
#endif

/*
 * Structure of the registers for the Valkyrie colormap registers.
 */
struct cmap_regs {
	unsigned char addr;
	char pad1[VALKYRIE_REG_PADSIZE];
	unsigned char lut;
};

/*
 * Structure of the registers for the "valkyrie" display adaptor.
 */

struct vpreg {			/* padded register */
	unsigned char r;
	char pad[VALKYRIE_REG_PADSIZE];
};


struct valkyrie_regs {
	struct vpreg mode;
	struct vpreg depth;
	struct vpreg status;
	struct vpreg reg3;
	struct vpreg intr;
	struct vpreg reg5;
	struct vpreg intr_enb;
	struct vpreg msense;
};

/*
 * Register initialization tables for the valkyrie display.
 *
 * Dot clock rate is
 * 3.9064MHz * 2**clock_params[2] * clock_params[1] / clock_params[0].
 */
struct valkyrie_regvals {
	unsigned char mode;
	unsigned char clock_params[3];
	int	pitch[2];		/* bytes/line, indexed by color_mode */
	int	hres;
	int	vres;
};

#ifndef CONFIG_MAC
/* Register values for 1024x768, 75Hz mode (17) */
/* I'm not sure which mode this is (16 or 17), so I'm defining it as 17,
 * since the equivalent mode in controlfb (which I adapted this from) is
 * also 17. Just because MacOS can't do this on Valkyrie doesn't mean we
 * can't! :)
 *
 * I was going to use 12, 31, 3, which I found by myself, but instead I'm
 * using 11, 28, 3 like controlfb, for consistency's sake.
 */

static struct valkyrie_regvals valkyrie_reg_init_17 = {
    15, 
    { 11, 28, 3 },  /* pixel clock = 79.55MHz for V=74.50Hz */
    { 1024, 0 },
	1024, 768
};

/* Register values for 1024x768, 72Hz mode (15) */
/* This used to be 12, 30, 3 for pixel clock = 78.12MHz for V=72.12Hz, but
 * that didn't match MacOS in the same video mode on this chip, and it also
 * caused the 15" Apple Studio Display to not work in this mode. While this
 * mode still doesn't match MacOS exactly (as far as I can tell), it's a lot
 * closer now, and it works with the Apple Studio Display.
 *
 * Yes, even though MacOS calls it "72Hz", in reality it's about 70Hz.
 */
static struct valkyrie_regvals valkyrie_reg_init_15 = {
    15,
    { 12, 29, 3 },  /* pixel clock = 75.52MHz for V=69.71Hz? */
		    /* I interpolated the V=69.71 from the vmode 14 and old 15
		     * numbers. Is this result correct?
		     */
    { 1024, 0 },
	1024, 768
};

/* Register values for 1024x768, 60Hz mode (14) */
static struct valkyrie_regvals valkyrie_reg_init_14 = {
    14,
    { 15, 31, 3 },  /* pixel clock = 64.58MHz for V=59.62Hz */
    { 1024, 0 },
	1024, 768
};

/* Register values for 800x600, 72Hz mode (11) */
static struct valkyrie_regvals valkyrie_reg_init_11 = {
    13,
    { 17, 27, 3 },  /* pixel clock = 49.63MHz for V=71.66Hz */
    { 800, 0 },
	800, 600
};
#endif /* CONFIG_MAC */

/* Register values for 832x624, 75Hz mode (13) */
static struct valkyrie_regvals valkyrie_reg_init_13 = {
    9,
    { 23, 42, 3 },  /* pixel clock = 57.07MHz for V=74.27Hz */
    { 832, 0 },
	832, 624
};

/* Register values for 800x600, 60Hz mode (10) */
static struct valkyrie_regvals valkyrie_reg_init_10 = {
    12,
    { 25, 32, 3 },  /* pixel clock = 40.0015MHz,
                     used to be 20,53,2, pixel clock 41.41MHz for V=59.78Hz */
    { 800, 1600 },
	800, 600
};

/* Register values for 640x480, 67Hz mode (6) */
static struct valkyrie_regvals valkyrie_reg_init_6 = {
    6,
    { 14, 27, 2 },  /* pixel clock = 30.13MHz for V=66.43Hz */
    { 640, 1280 },
	640, 480
};

/* Register values for 640x480, 60Hz mode (5) */
static struct valkyrie_regvals valkyrie_reg_init_5 = {
    11,
    { 23, 37, 2 },  /* pixel clock = 25.14MHz for V=59.85Hz */
    { 640, 1280 },
	640, 480
};

static struct valkyrie_regvals *valkyrie_reg_init[VMODE_MAX] = {
	NULL,
	NULL,
	NULL,
	NULL,
	&valkyrie_reg_init_5,
	&valkyrie_reg_init_6,
	NULL,
	NULL,
	NULL,
	&valkyrie_reg_init_10,
#ifdef CONFIG_MAC
	NULL,
	NULL,
	&valkyrie_reg_init_13,
	NULL,
	NULL,
	NULL,
	NULL,
#else
	&valkyrie_reg_init_11,
	NULL,
	&valkyrie_reg_init_13,
	&valkyrie_reg_init_14,
	&valkyrie_reg_init_15,
	NULL,
	&valkyrie_reg_init_17,
#endif
	NULL,
	NULL,
	NULL
};
