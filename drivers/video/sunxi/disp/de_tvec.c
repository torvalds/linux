/*
 * Copyright (C) 2007-2012 Allwinner Technology Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include "ebios_lcdc_tve.h"
#include "de_tvec_i.h"

static __u32 tve_reg_base0;
static __u32 tve_reg_base1;

__s32 TVE_set_reg_base(__u32 sel, __u32 address)
{
	if (sel == 0)
		tve_reg_base0 = address;
	else if (sel == 1)
		tve_reg_base1 = address;

	return 0;
}

/*
 * init module
 */
__s32 TVE_init(__u32 sel)
{
	TVE_close(sel);

	TVE_dac_set_de_bounce(sel, 0, 0);
	TVE_dac_set_de_bounce(sel, 1, 0);
	TVE_dac_set_de_bounce(sel, 2, 0);
	TVE_dac_set_de_bounce(sel, 3, 0);
	TVE_dac_int_disable(sel, 0);
	TVE_dac_int_disable(sel, 1);
	TVE_dac_int_disable(sel, 2);
	TVE_dac_int_disable(sel, 3);
	TVE_dac_autocheck_enable(sel, 0);
	TVE_dac_autocheck_enable(sel, 1);
	TVE_dac_autocheck_enable(sel, 2);
	TVE_dac_autocheck_enable(sel, 3);
	TVE_csc_init(sel, 0);

	if (sel == 0) {
		TVE_dac_sel(0, 0, 0);
		TVE_dac_sel(0, 1, 1);
		TVE_dac_sel(0, 2, 2);
		TVE_dac_sel(0, 3, 3);
	}
	TVE_SET_BIT(sel, TVE_008, 0x3 << 16);
	TVE_WUINT32(sel, TVE_024, 0x18181818);

	return 0;
}

__s32 TVE_exit(__u32 sel)
{
	TVE_dac_int_disable(sel, 0);
	TVE_dac_int_disable(sel, 1);
	TVE_dac_int_disable(sel, 2);
	TVE_dac_int_disable(sel, 3);
	TVE_dac_autocheck_disable(sel, 0);
	TVE_dac_autocheck_disable(sel, 1);
	TVE_dac_autocheck_disable(sel, 2);
	TVE_dac_autocheck_disable(sel, 3);

	return 0;
}

/*
 * open module
 */
__s32 TVE_open(__u32 sel)
{
	TVE_SET_BIT(sel, TVE_000, 0x1 << 0);

	return 0;
}

__s32 TVE_close(__u32 sel)
{
	TVE_CLR_BIT(sel, TVE_000, 0x1 << 0);
	TVE_WUINT32(sel, TVE_024, 0x18181818);
	return 0;
}

/*
 * 15~13     12~10       9~7         6~4
 * DAC3      DAC2        DAC1        DAC0
 *
 * CVBS(0)
 *           CR(4)       CB(5)       Y(6)
 *                       Chroma(1)   Luma(2)
 */
__s32 TVE_set_tv_mode(__u32 sel, __u8 mode)
{
	switch (mode) {
	case DISP_TV_MOD_PAL_SVIDEO:
	case DISP_TV_MOD_PAL:
		TVE_WUINT32(sel, TVE_004, 0x07030001);
		TVE_WUINT32(sel, TVE_014, 0x008a0018);
		TVE_WUINT32(sel, TVE_01C, 0x00160271);
		TVE_WUINT32(sel, TVE_114, 0x0016447e);
		TVE_WUINT32(sel, TVE_124, 0x000005a0);
		TVE_WUINT32(sel, TVE_130, 0x800D000C);
		TVE_WUINT32(sel, TVE_13C, 0x00000000);
		TVE_WUINT32(sel, TVE_00C, 0x00000120);
		TVE_WUINT32(sel, TVE_020, 0x00fc00fc);
		TVE_WUINT32(sel, TVE_10C, 0x00002828);
		TVE_WUINT32(sel, TVE_128, 0x00000002);
		TVE_WUINT32(sel, TVE_118, 0x0000e0e0);
		TVE_WUINT32(sel, TVE_12C, 0x00000101);
		break;

	case DISP_TV_MOD_PAL_M:
	case DISP_TV_MOD_PAL_M_SVIDEO:
		TVE_WUINT32(sel, TVE_004, 0x07030000); /* ntsc */
		TVE_WUINT32(sel, TVE_014, 0x00760020);
		TVE_WUINT32(sel, TVE_01C, 0x0016020d);
		TVE_WUINT32(sel, TVE_114, 0x0016447e);
		TVE_WUINT32(sel, TVE_124, 0x000005a0);
		TVE_WUINT32(sel, TVE_130, 0x000e000c);
		TVE_WUINT32(sel, TVE_13C, 0x00000000);
		TVE_WUINT32(sel, TVE_00C, 0x00000120);
		TVE_WUINT32(sel, TVE_020, 0x00f0011a);
		TVE_WUINT32(sel, TVE_10C, 0x0000004f);
		TVE_WUINT32(sel, TVE_110, 0x00000000);
		TVE_WUINT32(sel, TVE_118, 0x0000a0a0);
		TVE_WUINT32(sel, TVE_11C, 0x001000f0);
		TVE_WUINT32(sel, TVE_010, 0x21e6efe3); /* add for pal-m */
		TVE_WUINT32(sel, TVE_100, 0x00000000); /* add for pal-m */
		TVE_WUINT32(sel, TVE_128, 0x00000002);
		TVE_WUINT32(sel, TVE_12C, 0x00000101);
		break;

	case DISP_TV_MOD_PAL_NC:
	case DISP_TV_MOD_PAL_NC_SVIDEO:
		TVE_WUINT32(sel, TVE_004, 0x07030001); /* PAL */
		TVE_WUINT32(sel, TVE_014, 0x008a0018);
		TVE_WUINT32(sel, TVE_01C, 0x00160271);
		TVE_WUINT32(sel, TVE_114, 0x0016447e);
		TVE_WUINT32(sel, TVE_124, 0x000005a0);
		TVE_WUINT32(sel, TVE_130, 0x800D000C);
		TVE_WUINT32(sel, TVE_13C, 0x00000000);
		TVE_WUINT32(sel, TVE_00C, 0x00000120);
		TVE_WUINT32(sel, TVE_020, 0x00fc00fc);
		TVE_WUINT32(sel, TVE_10C, 0x00002828);
		TVE_WUINT32(sel, TVE_010, 0x21F69446); /* add for PAL-NC */
		TVE_WUINT32(sel, TVE_128, 0x00000002);
		TVE_WUINT32(sel, TVE_118, 0x0000e0e0);
		TVE_WUINT32(sel, TVE_12C, 0x00000101);
		break;

	case DISP_TV_MOD_NTSC:
	case DISP_TV_MOD_NTSC_SVIDEO:
		TVE_WUINT32(sel, TVE_004, 0x07030000);
		TVE_WUINT32(sel, TVE_014, 0x00760020);
		TVE_WUINT32(sel, TVE_01C, 0x0016020d);
		TVE_WUINT32(sel, TVE_114, 0x0016447e);
		TVE_WUINT32(sel, TVE_124, 0x000005a0);
		TVE_WUINT32(sel, TVE_130, 0x000e000c);
		TVE_WUINT32(sel, TVE_13C, 0x00000000);
		TVE_WUINT32(sel, TVE_00C, 0x00000120);
		TVE_WUINT32(sel, TVE_020, 0x00f0011a);
		TVE_WUINT32(sel, TVE_10C, 0x0000004f);
		TVE_WUINT32(sel, TVE_110, 0x00000000);
		TVE_WUINT32(sel, TVE_118, 0x0000a0a0);
		TVE_WUINT32(sel, TVE_11C, 0x001000f0);
		TVE_WUINT32(sel, TVE_128, 0x00000002);
		TVE_WUINT32(sel, TVE_12C, 0x00000101);
		break;

	case DISP_TV_MOD_480I:
		TVE_WUINT32(sel, TVE_004, 0x07040000);
		TVE_WUINT32(sel, TVE_014, 0x00760020);
		TVE_WUINT32(sel, TVE_01C, 0x0016020d);
		TVE_WUINT32(sel, TVE_114, 0x0016447e);
		TVE_WUINT32(sel, TVE_124, 0x000005a0);
		TVE_WUINT32(sel, TVE_130, 0x000e000c);
		TVE_WUINT32(sel, TVE_13C, 0x00000000);
		TVE_WUINT32(sel, TVE_00C, 0x00000120);
		TVE_WUINT32(sel, TVE_020, 0x00fc00fc);
		TVE_WUINT32(sel, TVE_10C, 0x0000004f);
		TVE_WUINT32(sel, TVE_110, 0x00000000);
		TVE_WUINT32(sel, TVE_118, 0x0000a0a0);
		TVE_WUINT32(sel, TVE_11C, 0x001000fc);
		break;

	case DISP_TV_MOD_576I:
		TVE_WUINT32(sel, TVE_004, 0x07040001);
		TVE_WUINT32(sel, TVE_014, 0x008a0018);
		TVE_WUINT32(sel, TVE_01C, 0x00160271);
		TVE_WUINT32(sel, TVE_114, 0x0016447e);
		TVE_WUINT32(sel, TVE_124, 0x000005a0);
		TVE_WUINT32(sel, TVE_130, 0x800D000C);
		TVE_WUINT32(sel, TVE_13C, 0x00000000);
		TVE_WUINT32(sel, TVE_00C, 0x00000120);
		TVE_WUINT32(sel, TVE_020, 0x00fc00fc);
		TVE_WUINT32(sel, TVE_10C, 0x00002828);
		break;

	case DISP_TV_MOD_480P:
		TVE_WUINT32(sel, TVE_004, 0x07040002);
		TVE_WUINT32(sel, TVE_014, 0x00760020);
		TVE_WUINT32(sel, TVE_01C, 0x002c020d);
		TVE_WUINT32(sel, TVE_114, 0x0016447e);
		TVE_WUINT32(sel, TVE_124, 0x000005a0);
		TVE_WUINT32(sel, TVE_130, 0x000e000C);
		TVE_WUINT32(sel, TVE_13C, 0x00000000);
		TVE_WUINT32(sel, TVE_00C, 0x00000120);
		TVE_WUINT32(sel, TVE_020, 0x00fc00fc);
		break;

	case DISP_TV_MOD_576P:
		TVE_WUINT32(sel, TVE_004, 0x07040003);
		TVE_WUINT32(sel, TVE_014, 0x008a0018);
		TVE_WUINT32(sel, TVE_01C, 0x002c0271);
		TVE_WUINT32(sel, TVE_114, 0x0016447e);
		TVE_WUINT32(sel, TVE_124, 0x000005a0);
		TVE_WUINT32(sel, TVE_130, 0x800B000C);
		TVE_WUINT32(sel, TVE_13C, 0x00000000);
		TVE_WUINT32(sel, TVE_00C, 0x00000120);
		TVE_WUINT32(sel, TVE_020, 0x00fc00fc);
		break;

	case DISP_TV_MOD_720P_50HZ:
		TVE_WUINT32(sel, TVE_004, 0x0004000a);
		TVE_WUINT32(sel, TVE_014, 0x01040190);
		TVE_WUINT32(sel, TVE_018, 0x05000190);
		TVE_WUINT32(sel, TVE_01C, 0x001902ee);
		TVE_WUINT32(sel, TVE_114, 0xdc280228);
		TVE_WUINT32(sel, TVE_124, 0x00000500);
		TVE_WUINT32(sel, TVE_130, 0x000e000c);
		TVE_WUINT32(sel, TVE_13C, 0x07000000);
		TVE_WUINT32(sel, TVE_00C, 0x01be0124);
		TVE_WUINT32(sel, TVE_128, 0x00000000);
		TVE_WUINT32(sel, TVE_020, 0x00fc00fc);
		TVE_WUINT32(sel, TVE_118, 0x0000a0a0);
		break;

	case DISP_TV_MOD_720P_60HZ:
		TVE_WUINT32(sel, TVE_004, 0x0004000a);
		TVE_WUINT32(sel, TVE_014, 0x01040046);
		TVE_WUINT32(sel, TVE_018, 0x05000046);
		TVE_WUINT32(sel, TVE_01C, 0x001902ee);
		TVE_WUINT32(sel, TVE_114, 0xdc280228);
		TVE_WUINT32(sel, TVE_124, 0x00000500);
		TVE_WUINT32(sel, TVE_130, 0x000c0008);
		TVE_WUINT32(sel, TVE_13C, 0x07000000);
		TVE_WUINT32(sel, TVE_00C, 0x01be0124);
		TVE_WUINT32(sel, TVE_128, 0x00000000);
		TVE_WUINT32(sel, TVE_020, 0x00fc00fc);
		TVE_WUINT32(sel, TVE_118, 0x0000a0a0);
		break;

	case DISP_TV_MOD_1080I_50HZ:
		TVE_WUINT32(sel, TVE_004, 0x0004000c);
		TVE_WUINT32(sel, TVE_014, 0x00c001e4);
		TVE_WUINT32(sel, TVE_018, 0x03700108);
		TVE_WUINT32(sel, TVE_01C, 0x00140465);
		TVE_WUINT32(sel, TVE_114, 0x582c442c);
		TVE_WUINT32(sel, TVE_124, 0x00000780);
		TVE_WUINT32(sel, TVE_130, 0x000e0008);
		TVE_WUINT32(sel, TVE_13C, 0x07000000);
		TVE_WUINT32(sel, TVE_00C, 0x01be0124);
		TVE_WUINT32(sel, TVE_128, 0x00000000);
		TVE_WUINT32(sel, TVE_020, 0x00fc00fc);
		TVE_WUINT32(sel, TVE_118, 0x0000a0a0);
		TVE_WUINT32(sel, TVE_104, 0x00000000);
		break;

	case DISP_TV_MOD_1080I_60HZ:
		TVE_WUINT32(sel, TVE_004, 0x0004000c);
		TVE_WUINT32(sel, TVE_014, 0x00c0002c);
		TVE_WUINT32(sel, TVE_018, 0x0370002c);
		TVE_WUINT32(sel, TVE_01C, 0x00140465);
		TVE_WUINT32(sel, TVE_114, 0x582c442c);
		TVE_WUINT32(sel, TVE_124, 0x00000780);
		TVE_WUINT32(sel, TVE_130, 0x000e0008);
		TVE_WUINT32(sel, TVE_13C, 0x07000000);
		TVE_WUINT32(sel, TVE_00C, 0x01be0124);
		TVE_WUINT32(sel, TVE_128, 0x00000000);
		TVE_WUINT32(sel, TVE_020, 0x00fc00fc);
		TVE_WUINT32(sel, TVE_118, 0x0000a0a0);
		TVE_WUINT32(sel, TVE_104, 0x00000000);
		break;

	case DISP_TV_MOD_1080P_50HZ:
		TVE_WUINT32(sel, TVE_004, 0x0004000e);
		TVE_WUINT32(sel, TVE_014, 0x00c001e4); /* 50hz */
		TVE_WUINT32(sel, TVE_018, 0x07bc01e4); /* 50hz */
		TVE_WUINT32(sel, TVE_01C, 0x00290465);
		TVE_WUINT32(sel, TVE_114, 0x582c022c);
		TVE_WUINT32(sel, TVE_124, 0x00000780);
		TVE_WUINT32(sel, TVE_130, 0x000e000c);
		TVE_WUINT32(sel, TVE_13C, 0x07000000);
		TVE_WUINT32(sel, TVE_00C, 0x01be0124);
		TVE_WUINT32(sel, TVE_128, 0x00000000);
		TVE_WUINT32(sel, TVE_020, 0x00fc00c0); /* ghost? */
		TVE_WUINT32(sel, TVE_118, 0x0000a0a0);
		break;

	case DISP_TV_MOD_1080P_60HZ:
		TVE_WUINT32(sel, TVE_004, 0x0004000e);
		TVE_WUINT32(sel, TVE_00C, 0x01be0124);
		TVE_WUINT32(sel, TVE_014, 0x00c0002c); /* 60hz */
		TVE_WUINT32(sel, TVE_018, 0x07bc002c); /* 60hz */
		TVE_WUINT32(sel, TVE_01C, 0x00290465);
		TVE_WUINT32(sel, TVE_020, 0x00fc00c0); /* ghost? */
		TVE_WUINT32(sel, TVE_114, 0x582c022c);
		TVE_WUINT32(sel, TVE_118, 0x0000a0a0);
		TVE_WUINT32(sel, TVE_124, 0x00000780);
		TVE_WUINT32(sel, TVE_128, 0x00000000);
		TVE_WUINT32(sel, TVE_130, 0x000e000c);
		TVE_WUINT32(sel, TVE_13C, 0x07000000);
		break;

	default:
		return 0;
	}
	TVE_CLR_BIT(sel, TVE_008, 0xfff << 4);
	TVE_SET_BIT(sel, TVE_008, 0x3 << 16);
	TVE_SET_BIT(sel, TVE_008, 0xf << 18);
	TVE_WUINT32(sel, TVE_024, 0x18181818);

	return 0;
}

__s32 TVE_set_vga_mode(__u32 sel)
{
	__u32 readval;

	TVE_WUINT32(sel, TVE_004, 0x20000000);
	TVE_WUINT32(sel, TVE_008, 0x403f1ac7);

	readval = TVE_RUINT32(sel, TVE_024);
	TVE_WUINT32(sel, TVE_024, readval & 0xff000000);

	TVE_INIT_BIT(0, TVE_000, 0xfff << 4, 0x321 << 4);
	return 0;
}

__u8 TVE_query_int(__u32 sel)
{
	__u8 sts = 0;
	__u32 readval;

	readval = TVE_RUINT32(sel, TVE_034);
	sts = readval & 0x0f;

	return sts;
}

__u8 TVE_clear_int(__u32 sel)
{
	__u32 sts = 0;
	__u32 readval;

	readval = TVE_RUINT32(sel, TVE_034);
	sts = readval & 0x0f;
	TVE_WUINT32(sel, TVE_034, sts);

	return 0;
}

/*
 * 0:unconnected
 * 1:connected
 * 3:short to ground
 */
__s32 TVE_get_dac_status(__u32 index)
{
	__u32 reg_000, map, sel, dac;
	__s32 status;

	reg_000 = TVE_RUINT32(0, TVE_000);
	map = (reg_000 >> (4 * (index + 1))) & 0xf;
	if (map >= 1 && map <= 4) {
		sel = 0;
		dac = map - 1;
	} else if (map >= 5 && map <= 8) {
		sel = 1;
		dac = map - 5;
	} else {
		return -1;
	}

	status = TVE_RUINT32(sel, TVE_038) >> (dac * 8);
	status &= 0x3;

	return status;
}

__u8 TVE_dac_int_enable(__u32 sel, __u8 index)
{
	__u32 readval;

	readval = TVE_RUINT32(sel, TVE_030);
	readval |= (1 << (16 + index));
	TVE_WUINT32(sel, TVE_030, readval);

	return 0;
}

__u8 TVE_dac_int_disable(__u32 sel, __u8 index)
{
	__u32 readval;

	readval = TVE_RUINT32(sel, TVE_030);
	readval &= (~(1 << (16 + index)));
	TVE_WUINT32(sel, TVE_030, readval);

	return 0;
}

__u8 TVE_dac_autocheck_enable(__u32 sel, __u8 index)
{
	__u32 readval;

	readval = TVE_RUINT32(sel, TVE_030);
	readval |= (1 << index);
	TVE_WUINT32(sel, TVE_030, readval);

	return 0;
}

__u8 TVE_dac_autocheck_disable(__u32 sel, __u8 index)
{
	__u32 readval;

	readval = TVE_RUINT32(sel, TVE_030);
	readval &= (~(1 << index));
	TVE_WUINT32(sel, TVE_030, readval);

	return 0;
}

__u8 TVE_dac_enable(__u32 sel, __u8 index)
{
	__u32 readval;

	readval = TVE_RUINT32(sel, TVE_008);

	TVE_SET_BIT(sel, TVE_008, readval | (1 << index));

	return 0;
}

__u8 TVE_dac_disable(__u32 sel, __u8 index)
{
	__u32 readval;

	readval = TVE_RUINT32(sel, TVE_008);

	TVE_WUINT32(sel, TVE_008, readval & (~(1 << index)));

	return 0;
}

__s32 TVE_dac_set_source(__u32 sel, __u32 index, __u32 source)
{
	__u32 readval;

	readval = TVE_RUINT32(sel, TVE_008);

	if (index == 0)
		readval = (readval & 0xffffff8f) | ((source & 0x7) << 4);
	else if (index == 1)
		readval = (readval & 0xfffffc7f) | ((source & 0x7) << 7);
	else if (index == 2)
		readval = (readval & 0xffffe3ff) | ((source & 0x7) << 10);
	else if (index == 3)
		readval = (readval & 0xffff1fff) | ((source & 0x7) << 13);
	else
		return 0;

	TVE_WUINT32(sel, TVE_008, readval);

	return 0;
}

__s32 TVE_dac_get_source(__u32 sel, __u32 index)
{
	__u32 readval = 0;

	readval = TVE_RUINT32(sel, TVE_008);

	if (index == 0)
		readval = (readval >> 4) & 0x7;
	else if (index == 1)
		readval = (readval >> 7) & 0x7;
	else if (index == 2)
		readval = (readval >> 10) & 0x7;
	else if (index == 3)
		readval = (readval >> 13) & 0x7;

	return readval;
}

__u8 TVE_dac_set_de_bounce(__u32 sel, __u8 index, __u32 times)
{
	__u32 readval;

	readval = TVE_RUINT32(sel, TVE_03C);

	if (index == 0)
		readval = (readval & 0xfffffff0) | (times & 0xf);
	else if (index == 1)
		readval = (readval & 0xfffff0ff) | ((times & 0xf) << 8);
	else if (index == 2)
		readval = (readval & 0xfff0ffff) | ((times & 0xf) << 16);
	else if (index == 3)
		readval = (readval & 0xfff0ffff) | ((times & 0xf) << 20);
	else
		return 0;

	TVE_WUINT32(sel, TVE_03C, readval);

	return 0;
}

__u8 TVE_dac_get_de_bounce(__u32 sel, __u8 index)
{
	__u8 sts = 0;
	__u32 readval;

	readval = TVE_RUINT32(sel, TVE_03C);

	if (index == 0)
		sts = readval & 0xf;
	else if (index == 1)
		sts = (readval & 0xf00) >> 8;
	else if (index == 2)
		sts = (readval & 0xf0000) >> 16;
	else if (index == 3)
		sts = (readval & 0xf000000) >> 20;
	else
		return 0;

	return sts;
}

/*
 * dac: 0~3
 * index: 0~3
 */
__s32 TVE_dac_sel(__u32 sel, __u32 dac, __u32 index)
{
	__u32 readval;

	if (dac == 0) {
		readval = TVE_RUINT32(sel, TVE_000);
		readval &= (~(0xf << 4));
		readval |= ((sel * 4 + index + 1) << 4);
		TVE_WUINT32(sel, TVE_000, readval);

		if (sel == 1) {
			readval = TVE_RUINT32(0, TVE_000);
			readval &= (~(0xf << 4));
			readval |= ((sel * 4 + index + 1) << 4);
			TVE_WUINT32(0, TVE_000, readval);
		}
	} else if (dac == 1) {
		readval = TVE_RUINT32(sel, TVE_000);
		readval &= (~(0xf << 8));
		readval |= ((sel * 4 + index + 1) << 8);
		TVE_WUINT32(sel, TVE_000, readval);
		if (sel == 1) {
			readval = TVE_RUINT32(0, TVE_000);
			readval &= (~(0xf << 8));
			readval |= ((sel * 4 + index + 1) << 8);
			TVE_WUINT32(0, TVE_000, readval);
		}
	} else if (dac == 2) {
		readval = TVE_RUINT32(sel, TVE_000);
		readval &= (~(0xf << 12));
		readval |= ((sel * 4 + index + 1) << 12);
		TVE_WUINT32(sel, TVE_000, readval);
		if (sel == 1) {
			readval = TVE_RUINT32(0, TVE_000);
			readval &= (~(0xf << 12));
			readval |= ((sel * 4 + index + 1) << 12);
			TVE_WUINT32(0, TVE_000, readval);
		}
	} else if (dac == 3) {
		readval = TVE_RUINT32(sel, TVE_000);
		readval &= (~(0xf << 16));
		readval |= ((sel * 4 + index + 1) << 16);
		TVE_WUINT32(sel, TVE_000, readval);
		if (sel == 1) {
			readval = TVE_RUINT32(0, TVE_000);
			readval &= (~(0xf << 16));
			readval |= ((sel * 4 + index + 1) << 16);
			TVE_WUINT32(0, TVE_000, readval);
		}
	}
	return 0;
}

__u8 TVE_csc_init(__u32 sel, __u8 type)
{
	if (sel == 0) {
		TVE_WUINT32(sel, TVE_040, 0x08440832);
		TVE_WUINT32(sel, TVE_044, 0x3B6DACE1);
		TVE_WUINT32(sel, TVE_048, 0x0E1D13DC);
		TVE_WUINT32(sel, TVE_04C, 0x00108080);
		return 0;
	} else
		return 0;
}

#ifdef UNUSED
static __u8
TVE_csc_enable(__u32 sel)
{
	TVE_SET_BIT(sel, TVE_040, (__u32) (0x1 << 31));
	return 0;
}

static __u8
TVE_csc_disable(__u32 sel)
{
	TVE_CLR_BIT(sel, TVE_040, 0x1 << 31);
	return 0;
}
#endif
