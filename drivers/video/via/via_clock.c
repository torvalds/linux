/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.
 * Copyright 2011 Florian Tobias Schandinat <FlorianSchandinat@gmx.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTIES OR REPRESENTATIONS; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
/*
 * clock and PLL management functions
 */

#include <linux/kernel.h>
#include <linux/via-core.h>
#include "via_clock.h"
#include "global.h"
#include "debug.h"

const char *via_slap = "Please slap VIA Technologies to motivate them "
	"releasing full documentation for your platform!\n";

static inline u32 cle266_encode_pll(struct via_pll_config pll)
{
	return (pll.multiplier << 8)
		| (pll.rshift << 6)
		| pll.divisor;
}

static inline u32 k800_encode_pll(struct via_pll_config pll)
{
	return ((pll.divisor - 2) << 16)
		| (pll.rshift << 10)
		| (pll.multiplier - 2);
}

static inline u32 vx855_encode_pll(struct via_pll_config pll)
{
	return (pll.divisor << 16)
		| (pll.rshift << 10)
		| pll.multiplier;
}

static inline void cle266_set_primary_pll_encoded(u32 data)
{
	via_write_reg_mask(VIASR, 0x40, 0x02, 0x02); /* enable reset */
	via_write_reg(VIASR, 0x46, data & 0xFF);
	via_write_reg(VIASR, 0x47, (data >> 8) & 0xFF);
	via_write_reg_mask(VIASR, 0x40, 0x00, 0x02); /* disable reset */
}

static inline void k800_set_primary_pll_encoded(u32 data)
{
	via_write_reg_mask(VIASR, 0x40, 0x02, 0x02); /* enable reset */
	via_write_reg(VIASR, 0x44, data & 0xFF);
	via_write_reg(VIASR, 0x45, (data >> 8) & 0xFF);
	via_write_reg(VIASR, 0x46, (data >> 16) & 0xFF);
	via_write_reg_mask(VIASR, 0x40, 0x00, 0x02); /* disable reset */
}

static inline void cle266_set_secondary_pll_encoded(u32 data)
{
	via_write_reg_mask(VIASR, 0x40, 0x04, 0x04); /* enable reset */
	via_write_reg(VIASR, 0x44, data & 0xFF);
	via_write_reg(VIASR, 0x45, (data >> 8) & 0xFF);
	via_write_reg_mask(VIASR, 0x40, 0x00, 0x04); /* disable reset */
}

static inline void k800_set_secondary_pll_encoded(u32 data)
{
	via_write_reg_mask(VIASR, 0x40, 0x04, 0x04); /* enable reset */
	via_write_reg(VIASR, 0x4A, data & 0xFF);
	via_write_reg(VIASR, 0x4B, (data >> 8) & 0xFF);
	via_write_reg(VIASR, 0x4C, (data >> 16) & 0xFF);
	via_write_reg_mask(VIASR, 0x40, 0x00, 0x04); /* disable reset */
}

static inline void set_engine_pll_encoded(u32 data)
{
	via_write_reg_mask(VIASR, 0x40, 0x01, 0x01); /* enable reset */
	via_write_reg(VIASR, 0x47, data & 0xFF);
	via_write_reg(VIASR, 0x48, (data >> 8) & 0xFF);
	via_write_reg(VIASR, 0x49, (data >> 16) & 0xFF);
	via_write_reg_mask(VIASR, 0x40, 0x00, 0x01); /* disable reset */
}

static void cle266_set_primary_pll(struct via_pll_config config)
{
	cle266_set_primary_pll_encoded(cle266_encode_pll(config));
}

static void k800_set_primary_pll(struct via_pll_config config)
{
	k800_set_primary_pll_encoded(k800_encode_pll(config));
}

static void vx855_set_primary_pll(struct via_pll_config config)
{
	k800_set_primary_pll_encoded(vx855_encode_pll(config));
}

static void cle266_set_secondary_pll(struct via_pll_config config)
{
	cle266_set_secondary_pll_encoded(cle266_encode_pll(config));
}

static void k800_set_secondary_pll(struct via_pll_config config)
{
	k800_set_secondary_pll_encoded(k800_encode_pll(config));
}

static void vx855_set_secondary_pll(struct via_pll_config config)
{
	k800_set_secondary_pll_encoded(vx855_encode_pll(config));
}

static void k800_set_engine_pll(struct via_pll_config config)
{
	set_engine_pll_encoded(k800_encode_pll(config));
}

static void vx855_set_engine_pll(struct via_pll_config config)
{
	set_engine_pll_encoded(vx855_encode_pll(config));
}

static void set_primary_pll_state(u8 state)
{
	u8 value;

	switch (state) {
	case VIA_STATE_ON:
		value = 0x20;
		break;
	case VIA_STATE_OFF:
		value = 0x00;
		break;
	default:
		return;
	}

	via_write_reg_mask(VIASR, 0x2D, value, 0x30);
}

static void set_secondary_pll_state(u8 state)
{
	u8 value;

	switch (state) {
	case VIA_STATE_ON:
		value = 0x08;
		break;
	case VIA_STATE_OFF:
		value = 0x00;
		break;
	default:
		return;
	}

	via_write_reg_mask(VIASR, 0x2D, value, 0x0C);
}

static void set_engine_pll_state(u8 state)
{
	u8 value;

	switch (state) {
	case VIA_STATE_ON:
		value = 0x02;
		break;
	case VIA_STATE_OFF:
		value = 0x00;
		break;
	default:
		return;
	}

	via_write_reg_mask(VIASR, 0x2D, value, 0x03);
}

static void set_primary_clock_state(u8 state)
{
	u8 value;

	switch (state) {
	case VIA_STATE_ON:
		value = 0x20;
		break;
	case VIA_STATE_OFF:
		value = 0x00;
		break;
	default:
		return;
	}

	via_write_reg_mask(VIASR, 0x1B, value, 0x30);
}

static void set_secondary_clock_state(u8 state)
{
	u8 value;

	switch (state) {
	case VIA_STATE_ON:
		value = 0x80;
		break;
	case VIA_STATE_OFF:
		value = 0x00;
		break;
	default:
		return;
	}

	via_write_reg_mask(VIASR, 0x1B, value, 0xC0);
}

static inline u8 set_clock_source_common(enum via_clksrc source, bool use_pll)
{
	u8 data = 0;

	switch (source) {
	case VIA_CLKSRC_X1:
		data = 0x00;
		break;
	case VIA_CLKSRC_TVX1:
		data = 0x02;
		break;
	case VIA_CLKSRC_TVPLL:
		data = 0x04; /* 0x06 should be the same */
		break;
	case VIA_CLKSRC_DVP1TVCLKR:
		data = 0x0A;
		break;
	case VIA_CLKSRC_CAP0:
		data = 0xC;
		break;
	case VIA_CLKSRC_CAP1:
		data = 0x0E;
		break;
	}

	if (!use_pll)
		data |= 1;

	return data;
}

static void set_primary_clock_source(enum via_clksrc source, bool use_pll)
{
	u8 data = set_clock_source_common(source, use_pll) << 4;
	via_write_reg_mask(VIACR, 0x6C, data, 0xF0);
}

static void set_secondary_clock_source(enum via_clksrc source, bool use_pll)
{
	u8 data = set_clock_source_common(source, use_pll);
	via_write_reg_mask(VIACR, 0x6C, data, 0x0F);
}

static void dummy_set_clock_state(u8 state)
{
	printk(KERN_INFO "Using undocumented set clock state.\n%s", via_slap);
}

static void dummy_set_clock_source(enum via_clksrc source, bool use_pll)
{
	printk(KERN_INFO "Using undocumented set clock source.\n%s", via_slap);
}

static void dummy_set_pll_state(u8 state)
{
	printk(KERN_INFO "Using undocumented set PLL state.\n%s", via_slap);
}

static void dummy_set_pll(struct via_pll_config config)
{
	printk(KERN_INFO "Using undocumented set PLL.\n%s", via_slap);
}

void via_clock_init(struct via_clock *clock, int gfx_chip)
{
	switch (gfx_chip) {
	case UNICHROME_CLE266:
	case UNICHROME_K400:
		clock->set_primary_clock_state = dummy_set_clock_state;
		clock->set_primary_clock_source = dummy_set_clock_source;
		clock->set_primary_pll_state = dummy_set_pll_state;
		clock->set_primary_pll = cle266_set_primary_pll;

		clock->set_secondary_clock_state = dummy_set_clock_state;
		clock->set_secondary_clock_source = dummy_set_clock_source;
		clock->set_secondary_pll_state = dummy_set_pll_state;
		clock->set_secondary_pll = cle266_set_secondary_pll;

		clock->set_engine_pll_state = dummy_set_pll_state;
		clock->set_engine_pll = dummy_set_pll;
		break;
	case UNICHROME_K800:
	case UNICHROME_PM800:
	case UNICHROME_CN700:
	case UNICHROME_CX700:
	case UNICHROME_CN750:
	case UNICHROME_K8M890:
	case UNICHROME_P4M890:
	case UNICHROME_P4M900:
	case UNICHROME_VX800:
		clock->set_primary_clock_state = set_primary_clock_state;
		clock->set_primary_clock_source = set_primary_clock_source;
		clock->set_primary_pll_state = set_primary_pll_state;
		clock->set_primary_pll = k800_set_primary_pll;

		clock->set_secondary_clock_state = set_secondary_clock_state;
		clock->set_secondary_clock_source = set_secondary_clock_source;
		clock->set_secondary_pll_state = set_secondary_pll_state;
		clock->set_secondary_pll = k800_set_secondary_pll;

		clock->set_engine_pll_state = set_engine_pll_state;
		clock->set_engine_pll = k800_set_engine_pll;
		break;
	case UNICHROME_VX855:
	case UNICHROME_VX900:
		clock->set_primary_clock_state = set_primary_clock_state;
		clock->set_primary_clock_source = set_primary_clock_source;
		clock->set_primary_pll_state = set_primary_pll_state;
		clock->set_primary_pll = vx855_set_primary_pll;

		clock->set_secondary_clock_state = set_secondary_clock_state;
		clock->set_secondary_clock_source = set_secondary_clock_source;
		clock->set_secondary_pll_state = set_secondary_pll_state;
		clock->set_secondary_pll = vx855_set_secondary_pll;

		clock->set_engine_pll_state = set_engine_pll_state;
		clock->set_engine_pll = vx855_set_engine_pll;
		break;

	}
}
