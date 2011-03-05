/*
 * Copyright (C) 2009 by Jan Weitzel Phytec Messtechnik GmbH,
 *			<armlinux@phytec.de>
 * Copyright (C) 2010 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#ifndef __MACH_MXS_IOMUX_H__
#define __MACH_MXS_IOMUX_H__

/*
 * IOMUX/PAD Bit field definitions
 *
 * PAD_BANK:		 0..2	(3)
 * PAD_PIN:		 3..7	(5)
 * PAD_MUXSEL:		 8..9	(2)
 * PAD_MA:		10..11	(2)
 * PAD_MA_VALID:	12	(1)
 * PAD_VOL:		13	(1)
 * PAD_VOL_VALID:	14	(1)
 * PAD_PULL:		15	(1)
 * PAD_PULL_VALID:	16	(1)
 * RESERVED:		17..31	(15)
 */
typedef u32 iomux_cfg_t;

#define MXS_PAD_BANK_SHIFT	0
#define MXS_PAD_BANK_MASK	((iomux_cfg_t)0x7 << MXS_PAD_BANK_SHIFT)
#define MXS_PAD_PIN_SHIFT	3
#define MXS_PAD_PIN_MASK	((iomux_cfg_t)0x1f << MXS_PAD_PIN_SHIFT)
#define MXS_PAD_MUXSEL_SHIFT	8
#define MXS_PAD_MUXSEL_MASK	((iomux_cfg_t)0x3 << MXS_PAD_MUXSEL_SHIFT)
#define MXS_PAD_MA_SHIFT	10
#define MXS_PAD_MA_MASK		((iomux_cfg_t)0x3 << MXS_PAD_MA_SHIFT)
#define MXS_PAD_MA_VALID_SHIFT	12
#define MXS_PAD_MA_VALID_MASK	((iomux_cfg_t)0x1 << MXS_PAD_MA_VALID_SHIFT)
#define MXS_PAD_VOL_SHIFT	13
#define MXS_PAD_VOL_MASK	((iomux_cfg_t)0x1 << MXS_PAD_VOL_SHIFT)
#define MXS_PAD_VOL_VALID_SHIFT	14
#define MXS_PAD_VOL_VALID_MASK	((iomux_cfg_t)0x1 << MXS_PAD_VOL_VALID_SHIFT)
#define MXS_PAD_PULL_SHIFT	15
#define MXS_PAD_PULL_MASK	((iomux_cfg_t)0x1 << MXS_PAD_PULL_SHIFT)
#define MXS_PAD_PULL_VALID_SHIFT 16
#define MXS_PAD_PULL_VALID_MASK	((iomux_cfg_t)0x1 << MXS_PAD_PULL_VALID_SHIFT)

#define PAD_MUXSEL_0		0
#define PAD_MUXSEL_1		1
#define PAD_MUXSEL_2		2
#define PAD_MUXSEL_GPIO		3

#define PAD_4MA			0
#define PAD_8MA			1
#define PAD_12MA		2
#define PAD_16MA		3

#define PAD_1V8			0
#define PAD_3V3			1

#define PAD_NOPULL		0
#define PAD_PULLUP		1

#define MXS_PAD_4MA	((PAD_4MA << MXS_PAD_MA_SHIFT) | \
					MXS_PAD_MA_VALID_MASK)
#define MXS_PAD_8MA	((PAD_8MA << MXS_PAD_MA_SHIFT) | \
					MXS_PAD_MA_VALID_MASK)
#define MXS_PAD_12MA	((PAD_12MA << MXS_PAD_MA_SHIFT) | \
					MXS_PAD_MA_VALID_MASK)
#define MXS_PAD_16MA	((PAD_16MA << MXS_PAD_MA_SHIFT) | \
					MXS_PAD_MA_VALID_MASK)

#define MXS_PAD_1V8	((PAD_1V8 << MXS_PAD_VOL_SHIFT) | \
					MXS_PAD_VOL_VALID_MASK)
#define MXS_PAD_3V3	((PAD_3V3 << MXS_PAD_VOL_SHIFT) | \
					MXS_PAD_VOL_VALID_MASK)

#define MXS_PAD_NOPULL	((PAD_NOPULL << MXS_PAD_PULL_SHIFT) | \
					MXS_PAD_PULL_VALID_MASK)
#define MXS_PAD_PULLUP	((PAD_PULLUP << MXS_PAD_PULL_SHIFT) | \
					MXS_PAD_PULL_VALID_MASK)

/* generic pad control used in most cases */
#define MXS_PAD_CTRL	(MXS_PAD_4MA | MXS_PAD_3V3 | MXS_PAD_NOPULL)

#define MXS_IOMUX_PAD(_bank, _pin, _muxsel, _ma, _vol, _pull)		\
		(((iomux_cfg_t)(_bank) << MXS_PAD_BANK_SHIFT) |		\
		((iomux_cfg_t)(_pin) << MXS_PAD_PIN_SHIFT) |		\
		((iomux_cfg_t)(_muxsel) << MXS_PAD_MUXSEL_SHIFT) |	\
		((iomux_cfg_t)(_ma) << MXS_PAD_MA_SHIFT) |		\
		((iomux_cfg_t)(_vol) << MXS_PAD_VOL_SHIFT) |		\
		((iomux_cfg_t)(_pull) << MXS_PAD_PULL_SHIFT))

/*
 * A pad becomes naked, when none of mA, vol or pull
 * validity bits is set.
 */
#define MXS_IOMUX_PAD_NAKED(_bank, _pin, _muxsel) \
		MXS_IOMUX_PAD(_bank, _pin, _muxsel, 0, 0, 0)

static inline unsigned int PAD_BANK(iomux_cfg_t pad)
{
	return (pad & MXS_PAD_BANK_MASK) >> MXS_PAD_BANK_SHIFT;
}

static inline unsigned int PAD_PIN(iomux_cfg_t pad)
{
	return (pad & MXS_PAD_PIN_MASK) >> MXS_PAD_PIN_SHIFT;
}

static inline unsigned int PAD_MUXSEL(iomux_cfg_t pad)
{
	return (pad & MXS_PAD_MUXSEL_MASK) >> MXS_PAD_MUXSEL_SHIFT;
}

static inline unsigned int PAD_MA(iomux_cfg_t pad)
{
	return (pad & MXS_PAD_MA_MASK) >> MXS_PAD_MA_SHIFT;
}

static inline unsigned int PAD_MA_VALID(iomux_cfg_t pad)
{
	return (pad & MXS_PAD_MA_VALID_MASK) >> MXS_PAD_MA_VALID_SHIFT;
}

static inline unsigned int PAD_VOL(iomux_cfg_t pad)
{
	return (pad & MXS_PAD_VOL_MASK) >> MXS_PAD_VOL_SHIFT;
}

static inline unsigned int PAD_VOL_VALID(iomux_cfg_t pad)
{
	return (pad & MXS_PAD_VOL_VALID_MASK) >> MXS_PAD_VOL_VALID_SHIFT;
}

static inline unsigned int PAD_PULL(iomux_cfg_t pad)
{
	return (pad & MXS_PAD_PULL_MASK) >> MXS_PAD_PULL_SHIFT;
}

static inline unsigned int PAD_PULL_VALID(iomux_cfg_t pad)
{
	return (pad & MXS_PAD_PULL_VALID_MASK) >> MXS_PAD_PULL_VALID_SHIFT;
}

/*
 * configures a single pad in the iomuxer
 */
int mxs_iomux_setup_pad(iomux_cfg_t pad);

/*
 * configures multiple pads
 * convenient way to call the above function with tables
 */
int mxs_iomux_setup_multiple_pads(const iomux_cfg_t *pad_list, unsigned count);

#endif /* __MACH_MXS_IOMUX_H__*/
