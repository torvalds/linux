/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * controlfb_hw.h: Constants of all sorts for controlfb
 *
 * Copyright (C) 1998 Daniel Jacobowitz <dan@debian.org>
 *
 * Based on an awful lot of code, including:
 *
 * control.c: Console support for PowerMac "control" display adaptor.
 * Copyright (C) 1996 Paul Mackerras.
 *
 * The so far unpublished platinumfb.c
 * Copyright (C) 1998 Jon Howell
 */

/*
 * Structure of the registers for the RADACAL colormap device.
 */
struct cmap_regs {
	unsigned char addr;	/* index for both cmap and misc registers */
	char pad1[15];
	unsigned char crsr;	/* cursor palette */
	char pad2[15];
	unsigned char dat;	/* RADACAL misc register data */
	char pad3[15];
	unsigned char lut;	/* cmap data */
	char pad4[15];
};

/*
 * Structure of the registers for the "control" display adaptor.
 */
#define PAD(x)	char x[12]

struct preg {			/* padded register */
	unsigned r;
	char pad[12];
};

struct control_regs {
	struct preg vcount;	/* vertical counter */
	/* Vertical parameters are in units of 1/2 scan line */
	struct preg vswin;	/* between vsblank and vssync */
	struct preg vsblank;	/* vert start blank */
	struct preg veblank;	/* vert end blank (display start) */
	struct preg vewin;	/* between vesync and veblank */
	struct preg vesync;	/* vert end sync */
	struct preg vssync;	/* vert start sync */
	struct preg vperiod;	/* vert period */
	struct preg piped;	/* pipe delay hardware cursor */
	/* Horizontal params are in units of 2 pixels */
	struct preg hperiod;	/* horiz period - 2 */
	struct preg hsblank;	/* horiz start blank */
	struct preg heblank;	/* horiz end blank */
	struct preg hesync;	/* horiz end sync */
	struct preg hssync;	/* horiz start sync */
	struct preg heq;	/* half horiz sync len */
	struct preg hlfln;	/* half horiz period */
	struct preg hserr;	/* horiz period - horiz sync len */
	struct preg cnttst;
	struct preg ctrl;	/* display control */
	struct preg start_addr;	/* start address: 5 lsbs zero */
	struct preg pitch;	/* addrs diff between scan lines */
	struct preg mon_sense;	/* monitor sense bits */
	struct preg vram_attr;	/* enable vram banks */
	struct preg mode;
	struct preg rfrcnt;	/* refresh count */
	struct preg intr_ena;	/* interrupt enable */
	struct preg intr_stat;	/* interrupt status */
	struct preg res[5];
};

struct control_regints {
	/* Vertical parameters are in units of 1/2 scan line */
	unsigned vswin;	/* between vsblank and vssync */
	unsigned vsblank;	/* vert start blank */
	unsigned veblank;	/* vert end blank (display start) */
	unsigned vewin;	/* between vesync and veblank */
	unsigned vesync;	/* vert end sync */
	unsigned vssync;	/* vert start sync */
	unsigned vperiod;	/* vert period */
	unsigned piped;		/* pipe delay hardware cursor */
	/* Horizontal params are in units of 2 pixels */
	/* Except, apparently, for hres > 1024 (or == 1280?) */
	unsigned hperiod;	/* horiz period - 2 */
	unsigned hsblank;	/* horiz start blank */
	unsigned heblank;	/* horiz end blank */
	unsigned hesync;	/* horiz end sync */
	unsigned hssync;	/* horiz start sync */
	unsigned heq;		/* half horiz sync len */
	unsigned hlfln;		/* half horiz period */
	unsigned hserr;		/* horiz period - horiz sync len */
};
	
/*
 * Dot clock rate is
 * 3.9064MHz * 2**clock_params[2] * clock_params[1] / clock_params[0].
 */
struct control_regvals {
	unsigned regs[16];		/* for vswin .. hserr */
	unsigned char mode;
	unsigned char radacal_ctrl;
	unsigned char clock_params[3];
};

#define CTRLFB_OFF 16	/* position of pixel 0 in frame buffer */


/*
 * Best cmode supported by control
 */
struct max_cmodes {
	int m[2];	/* 0: 2MB vram, 1: 4MB vram */
};

/*
 * Video modes supported by macmodes.c
 */
static struct max_cmodes control_mac_modes[] = {
	{{-1,-1}},	/* 512x384, 60Hz interlaced (NTSC) */
	{{-1,-1}},	/* 512x384, 60Hz */
	{{-1,-1}},	/* 640x480, 50Hz interlaced (PAL) */
	{{-1,-1}},	/* 640x480, 60Hz interlaced (NTSC) */
	{{ 2, 2}},	/* 640x480, 60Hz (VGA) */
	{{ 2, 2}},	/* 640x480, 67Hz */
	{{-1,-1}},	/* 640x870, 75Hz (portrait) */
	{{-1,-1}},	/* 768x576, 50Hz (PAL full frame) */
	{{ 2, 2}},	/* 800x600, 56Hz */
	{{ 2, 2}},	/* 800x600, 60Hz */
	{{ 2, 2}},	/* 800x600, 72Hz */
	{{ 2, 2}},	/* 800x600, 75Hz */
	{{ 1, 2}},	/* 832x624, 75Hz */
	{{ 1, 2}},	/* 1024x768, 60Hz */
	{{ 1, 2}},	/* 1024x768, 70Hz (or 72Hz?) */
	{{ 1, 2}},	/* 1024x768, 75Hz (VESA) */
	{{ 1, 2}},	/* 1024x768, 75Hz */
	{{ 1, 2}},	/* 1152x870, 75Hz */
	{{ 0, 1}},	/* 1280x960, 75Hz */
	{{ 0, 1}},	/* 1280x1024, 75Hz */
	{{ 1, 2}},	/* 1152x768, 60Hz */
	{{ 0, 1}},	/* 1600x1024, 60Hz */
};

