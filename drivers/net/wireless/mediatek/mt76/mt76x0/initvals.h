/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 * Copyright (C) 2018 Stanislaw Gruszka <stf_xl@wp.pl>
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 */

#ifndef __MT76X0U_INITVALS_H
#define __MT76X0U_INITVALS_H

#include "phy.h"

static const struct mt76x0_bbp_switch_item mt76x0_bbp_switch_tab[] = {
	{ RF_G_BAND | RF_BW_20 | RF_BW_40,		{ MT_BBP(AGC, 4),	0x1FEDA049 } },
	{ RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{ MT_BBP(AGC, 4),	0x1FECA054 } },

	{ RF_G_BAND | RF_BW_20 | RF_BW_40,		{ MT_BBP(AGC, 6),	0x00000045 } },
	{ RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{ MT_BBP(AGC, 6),	0x0000000A } },

	{ RF_G_BAND | RF_BW_20 | RF_BW_40,		{ MT_BBP(AGC, 8),	0x16344EF0 } },
	{ RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{ MT_BBP(AGC, 8),	0x122C54F2 } },

	{ RF_G_BAND | RF_BW_20,				{ MT_BBP(AGC, 12),	0x05052879 } },
	{ RF_G_BAND | RF_BW_40,				{ MT_BBP(AGC, 12),	0x050528F9 } },
	{ RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{ MT_BBP(AGC, 12),	0x050528F9 } },

	{ RF_G_BAND | RF_BW_20 | RF_BW_40,		{ MT_BBP(AGC, 13),	0x35050004 } },
	{ RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{ MT_BBP(AGC, 13),	0x2C3A0406 } },

	{ RF_G_BAND | RF_BW_20 | RF_BW_40,		{ MT_BBP(AGC, 14),	0x310F2E3C } },
	{ RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{ MT_BBP(AGC, 14),	0x310F2A3F } },

	{ RF_G_BAND | RF_BW_20 | RF_BW_40,		{ MT_BBP(AGC, 26),	0x007C2005 } },
	{ RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{ MT_BBP(AGC, 26),	0x007C2005 } },

	{ RF_G_BAND | RF_BW_20 | RF_BW_40,		{ MT_BBP(AGC, 27),	0x000000E1 } },
	{ RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{ MT_BBP(AGC, 27),	0x000000EC } },

	{ RF_G_BAND | RF_BW_20,				{ MT_BBP(AGC, 28),	0x00060806 } },
	{ RF_G_BAND | RF_BW_40,				{ MT_BBP(AGC, 28),	0x00050806 } },
	{ RF_A_BAND | RF_BW_40,				{ MT_BBP(AGC, 28),	0x00060801 } },
	{ RF_A_BAND | RF_BW_20 | RF_BW_80,		{ MT_BBP(AGC, 28),	0x00060806 } },

	{ RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{ MT_BBP(RXO, 28),	0x0000008A } },

	{ RF_G_BAND | RF_BW_20 | RF_BW_40,		{ MT_BBP(AGC, 31),	0x00000E23 } },
	{ RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{ MT_BBP(AGC, 31),	0x00000E13 } },

	{ RF_G_BAND | RF_BW_20 | RF_BW_40,		{ MT_BBP(AGC, 32),	0x00003218 } },
	{ RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{ MT_BBP(AGC, 32),	0x0000181C } },

	{ RF_G_BAND | RF_BW_20 | RF_BW_40,		{ MT_BBP(AGC, 33),	0x00003240 } },
	{ RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{ MT_BBP(AGC, 33),	0x00003218 } },

	{ RF_G_BAND | RF_BW_20,				{ MT_BBP(AGC, 35),	0x11111616 } },
	{ RF_G_BAND | RF_BW_40,				{ MT_BBP(AGC, 35),	0x11111516 } },
	{ RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{ MT_BBP(AGC, 35),	0x11111111 } },

	{ RF_G_BAND | RF_BW_20,				{ MT_BBP(AGC, 39),	0x2A2A3036 } },
	{ RF_G_BAND | RF_BW_40,				{ MT_BBP(AGC, 39),	0x2A2A2C36 } },
	{ RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{ MT_BBP(AGC, 39),	0x2A2A2A2A } },

	{ RF_G_BAND | RF_BW_20,				{ MT_BBP(AGC, 43),	0x27273438 } },
	{ RF_G_BAND | RF_BW_40,				{ MT_BBP(AGC, 43),	0x27272D38 } },
	{ RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{ MT_BBP(AGC, 43),	0x27271A1A } },

	{ RF_G_BAND | RF_BW_20 | RF_BW_40,		{ MT_BBP(AGC, 51),	0x17171C1C } },
	{ RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{ MT_BBP(AGC, 51),	0xFFFFFFFF } },

	{ RF_G_BAND | RF_BW_20,				{ MT_BBP(AGC, 53),	0x26262A2F } },
	{ RF_G_BAND | RF_BW_40,				{ MT_BBP(AGC, 53),	0x2626322F } },
	{ RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{ MT_BBP(AGC, 53),	0xFFFFFFFF } },

	{ RF_G_BAND | RF_BW_20 | RF_BW_40,		{ MT_BBP(AGC, 55),	0x40404040 } },
	{ RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{ MT_BBP(AGC, 55),	0xFFFFFFFF } },

	{ RF_G_BAND | RF_BW_20 | RF_BW_40,		{ MT_BBP(AGC, 58),	0x00001010 } },
	{ RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{ MT_BBP(AGC, 58),	0x00000000 } },

	{ RF_G_BAND | RF_BW_20 | RF_BW_40,		{ MT_BBP(RXFE, 0),	0x3D5000E0 } },
	{ RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{ MT_BBP(RXFE, 0),	0x895000E0 } },
};

#endif
