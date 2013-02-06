/*
 * arch/arm/mach-exynos/u1-panel.h
 */

#ifndef __C1_PANEL_H__
#define __C1_PANEL_H__

#define SLEEPMSEC		0x1000
#define ENDDEF			0x2000
#define	DEFMASK		0xFF00
#define COMMAND_ONLY		0xFE
#define DATA_ONLY		0xFF


static const unsigned short SEQ_USER_SETTING[] = {
	0xF0, 0x5A,

	DATA_ONLY, 0x5A,
	ENDDEF, 0x00
};

static const unsigned short SEQ_DISPCTL[] = {
	0xF2, 0x02,

	DATA_ONLY, 0x06,
	DATA_ONLY, 0x0A,
	DATA_ONLY, 0x10,
	DATA_ONLY, 0x10,
	ENDDEF, 0x00
};

static const unsigned short SEQ_GTCON[] = {
	0xF7, 0x09,

	ENDDEF, 0x00
};

static const unsigned short SEQ_PANEL_CONDITION[] = {
	0xF8, 0x05,
	DATA_ONLY, 0x5E,
	DATA_ONLY, 0x96,
	DATA_ONLY, 0x6B,
	DATA_ONLY, 0x7D,
	DATA_ONLY, 0x0D,
	DATA_ONLY, 0x3F,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x32,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x07,
	DATA_ONLY, 0x05,
	DATA_ONLY, 0x1F,
	DATA_ONLY, 0x1F,
	DATA_ONLY, 0x1F,
	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,
	ENDDEF, 0x00
};

static const unsigned short SEQ_SLPOUT[] = {
	0x11, COMMAND_ONLY,
	SLEEPMSEC, 120,
	ENDDEF, 0x00
};

static const unsigned short SEQ_SLPIN[] = {
	0x10, COMMAND_ONLY,
	ENDDEF, 0x00
};

static const unsigned short SEQ_DISPON[] = {
	0x29, COMMAND_ONLY,
	ENDDEF, 0x00
};

static const unsigned short SEQ_DISPOFF[] = {
	0x28, COMMAND_ONLY,
	ENDDEF, 0x00
};

static const unsigned short SEQ_ELVSS_ON[] = {
	0xB1, 0x0F,

	DATA_ONLY, 0x00,
	DATA_ONLY, 0x16,
	ENDDEF, 0x00
};


static const unsigned short SEQ_ACL_ON[] = {
	0xC0, 0x01,

	ENDDEF, 0x00
};

static const unsigned short SEQ_ACL_OFF[] = {
	0xC0, 0x00,

	ENDDEF, 0x00
};

static const unsigned short SEQ_ACL_40P[] = {
	0xC1, 0x4D,

	DATA_ONLY, 0x96,	DATA_ONLY, 0x1D,	DATA_ONLY, 0x00,	DATA_ONLY, 0x00,
	DATA_ONLY, 0x01,	DATA_ONLY, 0xDF,	DATA_ONLY, 0x00,	DATA_ONLY, 0x00,
	DATA_ONLY, 0x03,	DATA_ONLY, 0x1F,	DATA_ONLY, 0x00,	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,	DATA_ONLY, 0x00,	DATA_ONLY, 0x00,	DATA_ONLY, 0x01,
	DATA_ONLY, 0x06,	DATA_ONLY, 0x11,	DATA_ONLY, 0x1A,	DATA_ONLY, 0x20,
	DATA_ONLY, 0x25,	DATA_ONLY, 0x29,	DATA_ONLY, 0x2D,	DATA_ONLY, 0x30,
	DATA_ONLY, 0x33,	DATA_ONLY, 0x35,

	0xC0, 0x01,

	ENDDEF, 0x00
};


static const unsigned short SEQ_ACL_50P[] = {
	0xC1, 0x4D,

	DATA_ONLY, 0x96,	DATA_ONLY, 0x1D,	DATA_ONLY, 0x00,	DATA_ONLY, 0x00,
	DATA_ONLY, 0x01,	DATA_ONLY, 0xDF,	DATA_ONLY, 0x00,	DATA_ONLY, 0x00,
	DATA_ONLY, 0x03,	DATA_ONLY, 0x1F,	DATA_ONLY, 0x00,	DATA_ONLY, 0x00,
	DATA_ONLY, 0x00,	DATA_ONLY, 0x00,	DATA_ONLY, 0x00,	DATA_ONLY, 0x01,
	DATA_ONLY, 0x08,	DATA_ONLY, 0x16,	DATA_ONLY, 0x22,	DATA_ONLY, 0x2B,
	DATA_ONLY, 0x31,	DATA_ONLY, 0x37,	DATA_ONLY, 0x3B,	DATA_ONLY, 0x3F,
	DATA_ONLY, 0x43,	DATA_ONLY, 0x46,

	0xC0, 0x01,

	ENDDEF, 0x00
};

static const unsigned short *ACL_cutoff_set[] = {
	SEQ_ACL_OFF,
	SEQ_ACL_40P,
	SEQ_ACL_50P,
};

#endif
