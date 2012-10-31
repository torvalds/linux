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

#include "disp_display.h"

__s32 BSP_disp_hwc_enable(__u32 sel, __bool enable)
{
	DE_BE_HWC_Enable(sel, enable);

	return DIS_SUCCESS;
}

__s32 BSP_disp_hwc_set_pos(__u32 sel, __disp_pos_t *pos)
{
	DE_BE_HWC_Set_Pos(sel, pos);

	return DIS_SUCCESS;
}

__s32 BSP_disp_hwc_get_pos(__u32 sel, __disp_pos_t *pos)
{
	DE_BE_HWC_Get_Pos(sel, pos);

	return DIS_SUCCESS;
}

__s32 BSP_disp_hwc_set_framebuffer(__u32 sel, __disp_hwc_pattern_t *patmem)
{
	de_hwc_src_t hsrc;

	if (patmem == NULL)
		return DIS_PARA_FAILED;

	hsrc.mode = patmem->pat_mode;
	hsrc.paddr = patmem->addr;
	DE_BE_HWC_Set_Src(sel, &hsrc);

	return DIS_SUCCESS;
}

__s32 BSP_disp_hwc_set_palette(__u32 sel, void *palette, __u32 offset,
			       __u32 palette_size)
{
	if ((palette == NULL) || ((offset + palette_size) > 1024)) {
		DE_WRN("para invalid in BSP_disp_hwc_set_palette\n");
		return DIS_PARA_FAILED;
	}
	DE_BE_HWC_Set_Palette(sel, (__u32) palette, offset, palette_size);

	return DIS_SUCCESS;
}
