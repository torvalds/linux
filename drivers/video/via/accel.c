/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTIES OR REPRESENTATIONS; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.See the GNU General Public License
 * for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include "global.h"

void viafb_init_accel(void)
{
	viaparinfo->fbmem_free -= CURSOR_SIZE;
	viaparinfo->cursor_start = viaparinfo->fbmem_free;
	viaparinfo->fbmem_used += CURSOR_SIZE;

	/* Reverse 8*1024 memory space for cursor image */
	viaparinfo->fbmem_free -= (CURSOR_SIZE + VQ_SIZE);
	viaparinfo->VQ_start = viaparinfo->fbmem_free;
	viaparinfo->VQ_end = viaparinfo->VQ_start + VQ_SIZE - 1;
	viaparinfo->fbmem_used += (CURSOR_SIZE + VQ_SIZE); }

void viafb_init_2d_engine(void)
{
	u32 dwVQStartAddr, dwVQEndAddr;
	u32 dwVQLen, dwVQStartL, dwVQEndL, dwVQStartEndH;

	/* init 2D engine regs to reset 2D engine */
	writel(0x0, viaparinfo->io_virt + VIA_REG_GEMODE);
	writel(0x0, viaparinfo->io_virt + VIA_REG_SRCPOS);
	writel(0x0, viaparinfo->io_virt + VIA_REG_DSTPOS);
	writel(0x0, viaparinfo->io_virt + VIA_REG_DIMENSION);
	writel(0x0, viaparinfo->io_virt + VIA_REG_PATADDR);
	writel(0x0, viaparinfo->io_virt + VIA_REG_FGCOLOR);
	writel(0x0, viaparinfo->io_virt + VIA_REG_BGCOLOR);
	writel(0x0, viaparinfo->io_virt + VIA_REG_CLIPTL);
	writel(0x0, viaparinfo->io_virt + VIA_REG_CLIPBR);
	writel(0x0, viaparinfo->io_virt + VIA_REG_OFFSET);
	writel(0x0, viaparinfo->io_virt + VIA_REG_KEYCONTROL);
	writel(0x0, viaparinfo->io_virt + VIA_REG_SRCBASE);
	writel(0x0, viaparinfo->io_virt + VIA_REG_DSTBASE);
	writel(0x0, viaparinfo->io_virt + VIA_REG_PITCH);
	writel(0x0, viaparinfo->io_virt + VIA_REG_MONOPAT1);

	/* Init AGP and VQ regs */
	switch (viaparinfo->chip_info->gfx_chip_name) {
	case UNICHROME_K8M890:
	case UNICHROME_P4M900:
		writel(0x00100000, viaparinfo->io_virt + VIA_REG_CR_TRANSET);
		writel(0x680A0000, viaparinfo->io_virt + VIA_REG_CR_TRANSPACE);
		writel(0x02000000, viaparinfo->io_virt + VIA_REG_CR_TRANSPACE);
		break;

	default:
		writel(0x00100000, viaparinfo->io_virt + VIA_REG_TRANSET);
		writel(0x00000000, viaparinfo->io_virt + VIA_REG_TRANSPACE);
		writel(0x00333004, viaparinfo->io_virt + VIA_REG_TRANSPACE);
		writel(0x60000000, viaparinfo->io_virt + VIA_REG_TRANSPACE);
		writel(0x61000000, viaparinfo->io_virt + VIA_REG_TRANSPACE);
		writel(0x62000000, viaparinfo->io_virt + VIA_REG_TRANSPACE);
		writel(0x63000000, viaparinfo->io_virt + VIA_REG_TRANSPACE);
		writel(0x64000000, viaparinfo->io_virt + VIA_REG_TRANSPACE);
		writel(0x7D000000, viaparinfo->io_virt + VIA_REG_TRANSPACE);

		writel(0xFE020000, viaparinfo->io_virt + VIA_REG_TRANSET);
		writel(0x00000000, viaparinfo->io_virt + VIA_REG_TRANSPACE);
		break;
	}
	if (viaparinfo->VQ_start != 0) {
		/* Enable VQ */
		dwVQStartAddr = viaparinfo->VQ_start;
		dwVQEndAddr = viaparinfo->VQ_end;

		dwVQStartL = 0x50000000 | (dwVQStartAddr & 0xFFFFFF);
		dwVQEndL = 0x51000000 | (dwVQEndAddr & 0xFFFFFF);
		dwVQStartEndH = 0x52000000 |
			((dwVQStartAddr & 0xFF000000) >> 24) |
			((dwVQEndAddr & 0xFF000000) >> 16);
		dwVQLen = 0x53000000 | (VQ_SIZE >> 3);
		switch (viaparinfo->chip_info->gfx_chip_name) {
		case UNICHROME_K8M890:
		case UNICHROME_P4M900:
			dwVQStartL |= 0x20000000;
			dwVQEndL |= 0x20000000;
			dwVQStartEndH |= 0x20000000;
			dwVQLen |= 0x20000000;
			break;
		default:
			break;
		}

		switch (viaparinfo->chip_info->gfx_chip_name) {
		case UNICHROME_K8M890:
		case UNICHROME_P4M900:
			writel(0x00100000,
				viaparinfo->io_virt + VIA_REG_CR_TRANSET);
			writel(dwVQStartEndH,
				viaparinfo->io_virt + VIA_REG_CR_TRANSPACE);
			writel(dwVQStartL,
				viaparinfo->io_virt + VIA_REG_CR_TRANSPACE);
			writel(dwVQEndL,
				viaparinfo->io_virt + VIA_REG_CR_TRANSPACE);
			writel(dwVQLen,
				viaparinfo->io_virt + VIA_REG_CR_TRANSPACE);
			writel(0x74301001,
				viaparinfo->io_virt + VIA_REG_CR_TRANSPACE);
			writel(0x00000000,
				viaparinfo->io_virt + VIA_REG_CR_TRANSPACE);
			break;
		default:
			writel(0x00FE0000,
				viaparinfo->io_virt + VIA_REG_TRANSET);
			writel(0x080003FE,
				viaparinfo->io_virt + VIA_REG_TRANSPACE);
			writel(0x0A00027C,
				viaparinfo->io_virt + VIA_REG_TRANSPACE);
			writel(0x0B000260,
				viaparinfo->io_virt + VIA_REG_TRANSPACE);
			writel(0x0C000274,
				viaparinfo->io_virt + VIA_REG_TRANSPACE);
			writel(0x0D000264,
				viaparinfo->io_virt + VIA_REG_TRANSPACE);
			writel(0x0E000000,
				viaparinfo->io_virt + VIA_REG_TRANSPACE);
			writel(0x0F000020,
				viaparinfo->io_virt + VIA_REG_TRANSPACE);
			writel(0x1000027E,
				viaparinfo->io_virt + VIA_REG_TRANSPACE);
			writel(0x110002FE,
				viaparinfo->io_virt + VIA_REG_TRANSPACE);
			writel(0x200F0060,
				viaparinfo->io_virt + VIA_REG_TRANSPACE);

			writel(0x00000006,
				viaparinfo->io_virt + VIA_REG_TRANSPACE);
			writel(0x40008C0F,
				viaparinfo->io_virt + VIA_REG_TRANSPACE);
			writel(0x44000000,
				viaparinfo->io_virt + VIA_REG_TRANSPACE);
			writel(0x45080C04,
				viaparinfo->io_virt + VIA_REG_TRANSPACE);
			writel(0x46800408,
				viaparinfo->io_virt + VIA_REG_TRANSPACE);

			writel(dwVQStartEndH,
				viaparinfo->io_virt + VIA_REG_TRANSPACE);
			writel(dwVQStartL,
				viaparinfo->io_virt + VIA_REG_TRANSPACE);
			writel(dwVQEndL,
				viaparinfo->io_virt + VIA_REG_TRANSPACE);
			writel(dwVQLen,
				viaparinfo->io_virt + VIA_REG_TRANSPACE);
			break;
		}
	} else {
		/* Disable VQ */
		switch (viaparinfo->chip_info->gfx_chip_name) {
		case UNICHROME_K8M890:
		case UNICHROME_P4M900:
			writel(0x00100000,
				viaparinfo->io_virt + VIA_REG_CR_TRANSET);
			writel(0x74301000,
				viaparinfo->io_virt + VIA_REG_CR_TRANSPACE);
			break;
		default:
			writel(0x00FE0000,
				viaparinfo->io_virt + VIA_REG_TRANSET);
			writel(0x00000004,
				viaparinfo->io_virt + VIA_REG_TRANSPACE);
			writel(0x40008C0F,
				viaparinfo->io_virt + VIA_REG_TRANSPACE);
			writel(0x44000000,
				viaparinfo->io_virt + VIA_REG_TRANSPACE);
			writel(0x45080C04,
				viaparinfo->io_virt + VIA_REG_TRANSPACE);
			writel(0x46800408,
				viaparinfo->io_virt + VIA_REG_TRANSPACE);
			break;
		}
	}

	viafb_set_2d_color_depth(viaparinfo->bpp);

	writel(0x0, viaparinfo->io_virt + VIA_REG_SRCBASE);
	writel(0x0, viaparinfo->io_virt + VIA_REG_DSTBASE);

	writel(VIA_PITCH_ENABLE |
		   (((viaparinfo->hres *
		      viaparinfo->bpp >> 3) >> 3) | (((viaparinfo->hres *
						   viaparinfo->
						   bpp >> 3) >> 3) << 16)),
					viaparinfo->io_virt + VIA_REG_PITCH);
}

void viafb_set_2d_color_depth(int bpp)
{
	u32 dwGEMode;

	dwGEMode = readl(viaparinfo->io_virt + 0x04) & 0xFFFFFCFF;

	switch (bpp) {
	case 16:
		dwGEMode |= VIA_GEM_16bpp;
		break;
	case 32:
		dwGEMode |= VIA_GEM_32bpp;
		break;
	default:
		dwGEMode |= VIA_GEM_8bpp;
		break;
	}

	/* Set BPP and Pitch */
	writel(dwGEMode, viaparinfo->io_virt + VIA_REG_GEMODE);
}

void viafb_hw_cursor_init(void)
{
	/* Set Cursor Image Base Address */
	writel(viaparinfo->cursor_start,
		viaparinfo->io_virt + VIA_REG_CURSOR_MODE);
	writel(0x0, viaparinfo->io_virt + VIA_REG_CURSOR_POS);
	writel(0x0, viaparinfo->io_virt + VIA_REG_CURSOR_ORG);
	writel(0x0, viaparinfo->io_virt + VIA_REG_CURSOR_BG);
	writel(0x0, viaparinfo->io_virt + VIA_REG_CURSOR_FG);
}

void viafb_show_hw_cursor(struct fb_info *info, int Status)
{
	u32 temp;
	u32 iga_path = ((struct viafb_par *)(info->par))->iga_path;

	temp = readl(viaparinfo->io_virt + VIA_REG_CURSOR_MODE);
	switch (Status) {
	case HW_Cursor_ON:
		temp |= 0x1;
		break;
	case HW_Cursor_OFF:
		temp &= 0xFFFFFFFE;
		break;
	}
	switch (iga_path) {
	case IGA2:
		temp |= 0x80000000;
		break;
	case IGA1:
	default:
		temp &= 0x7FFFFFFF;
	}
	writel(temp, viaparinfo->io_virt + VIA_REG_CURSOR_MODE);
}

int viafb_wait_engine_idle(void)
{
	int loop = 0;

	while (!(readl(viaparinfo->io_virt + VIA_REG_STATUS) &
			VIA_VR_QUEUE_BUSY) && (loop++ < MAXLOOP))
		cpu_relax();

	while ((readl(viaparinfo->io_virt + VIA_REG_STATUS) &
		    (VIA_CMD_RGTR_BUSY | VIA_2D_ENG_BUSY | VIA_3D_ENG_BUSY)) &&
		    (loop++ < MAXLOOP))
		cpu_relax();

	return loop >= MAXLOOP;
}
