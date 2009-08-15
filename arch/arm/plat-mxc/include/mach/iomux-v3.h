/*
 * Copyright (C) 2009 by Jan Weitzel Phytec Messtechnik GmbH,
 *			<armlinux@phytec.de>
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

#ifndef __MACH_IOMUX_V3_H__
#define __MACH_IOMUX_V3_H__

/*
 *	build IOMUX_PAD structure
 *
 * This iomux scheme is based around pads, which are the physical balls
 * on the processor.
 *
 * - Each pad has a pad control register (IOMUXC_SW_PAD_CTRL_x) which controls
 *   things like driving strength and pullup/pulldown.
 * - Each pad can have but not necessarily does have an output routing register
 *   (IOMUXC_SW_MUX_CTL_PAD_x).
 * - Each pad can have but not necessarily does have an input routing register
 *   (IOMUXC_x_SELECT_INPUT)
 *
 * The three register sets do not have a fixed offset to each other,
 * hence we order this table by pad control registers (which all pads
 * have) and put the optional i/o routing registers into additional
 * fields.
 *
 * The naming convention for the pad modes is MX35_PAD_<padname>__<padmode>
 * If <padname> or <padmode> refers to a GPIO, it is named
 * GPIO_<unit>_<num>
 *
 */

struct pad_desc {
	unsigned mux_ctrl_ofs:12; /* IOMUXC_SW_MUX_CTL_PAD offset */
	unsigned mux_mode:8;
	unsigned pad_ctrl_ofs:12; /* IOMUXC_SW_PAD_CTRL offset */
#define	NO_PAD_CTRL	(1 << 16)
	unsigned pad_ctrl:17;
	unsigned select_input_ofs:12; /* IOMUXC_SELECT_INPUT offset */
	unsigned select_input:3;
};

#define IOMUX_PAD(_pad_ctrl_ofs, _mux_ctrl_ofs, _mux_mode, _select_input_ofs, \
		_select_input, _pad_ctrl)				\
		{							\
			.mux_ctrl_ofs     = _mux_ctrl_ofs,		\
			.mux_mode         = _mux_mode,			\
			.pad_ctrl_ofs     = _pad_ctrl_ofs,		\
			.pad_ctrl         = _pad_ctrl,			\
			.select_input_ofs = _select_input_ofs,		\
			.select_input     = _select_input,		\
		}

/*
 * Use to set PAD control
 */
#define PAD_CTL_DRIVE_VOLTAGE_3_3_V	0
#define PAD_CTL_DRIVE_VOLTAGE_1_8_V	1

#define PAD_CTL_NO_HYSTERESIS		0
#define PAD_CTL_HYSTERESIS		1

#define PAD_CTL_PULL_DISABLED		0x0
#define PAD_CTL_PULL_KEEPER		0xa
#define PAD_CTL_PULL_DOWN_100K		0xc
#define PAD_CTL_PULL_UP_47K		0xd
#define PAD_CTL_PULL_UP_100K		0xe
#define PAD_CTL_PULL_UP_22K		0xf

#define PAD_CTL_OUTPUT_CMOS		0
#define PAD_CTL_OUTPUT_OPEN_DRAIN	1

#define PAD_CTL_DRIVE_STRENGTH_NORM	0
#define PAD_CTL_DRIVE_STRENGTH_HIGH	1
#define PAD_CTL_DRIVE_STRENGTH_MAX	2

#define PAD_CTL_SLEW_RATE_SLOW		0
#define PAD_CTL_SLEW_RATE_FAST		1

/*
 * setups a single pad:
 * 	- reserves the pad so that it is not claimed by another driver
 * 	- setups the iomux according to the configuration
 */
int mxc_iomux_v3_setup_pad(struct pad_desc *pad);

/*
 * setups mutliple pads
 * convenient way to call the above function with tables
 */
int mxc_iomux_v3_setup_multiple_pads(struct pad_desc *pad_list, unsigned count);

/*
 * releases a single pad:
 * 	- make it available for a future use by another driver
 * 	- DOES NOT reconfigure the IOMUX in its reset state
 */
void mxc_iomux_v3_release_pad(struct pad_desc *pad);

/*
 * releases multiple pads
 * convenvient way to call the above function with tables
 */
void mxc_iomux_v3_release_multiple_pads(struct pad_desc *pad_list, int count);

#endif /* __MACH_IOMUX_V3_H__*/

