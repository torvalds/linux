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

static int hw_bitblt_1(void __iomem *engine, u8 op, u32 width, u32 height,
	u8 dst_bpp, u32 dst_addr, u32 dst_pitch, u32 dst_x, u32 dst_y,
	u32 *src_mem, u32 src_addr, u32 src_pitch, u32 src_x, u32 src_y,
	u32 fg_color, u32 bg_color, u8 fill_rop)
{
	u32 ge_cmd = 0, tmp, i;

	if (!op || op > 3) {
		printk(KERN_WARNING "hw_bitblt_1: Invalid operation: %d\n", op);
		return -EINVAL;
	}

	if (op != VIA_BITBLT_FILL && !src_mem && src_addr == dst_addr) {
		if (src_x < dst_x) {
			ge_cmd |= 0x00008000;
			src_x += width - 1;
			dst_x += width - 1;
		}
		if (src_y < dst_y) {
			ge_cmd |= 0x00004000;
			src_y += height - 1;
			dst_y += height - 1;
		}
	}

	if (op == VIA_BITBLT_FILL) {
		switch (fill_rop) {
		case 0x00: /* blackness */
		case 0x5A: /* pattern inversion */
		case 0xF0: /* pattern copy */
		case 0xFF: /* whiteness */
			break;
		default:
			printk(KERN_WARNING "hw_bitblt_1: Invalid fill rop: "
				"%u\n", fill_rop);
			return -EINVAL;
		}
	}

	switch (dst_bpp) {
	case 8:
		tmp = 0x00000000;
		break;
	case 16:
		tmp = 0x00000100;
		break;
	case 32:
		tmp = 0x00000300;
		break;
	default:
		printk(KERN_WARNING "hw_bitblt_1: Unsupported bpp %d\n",
			dst_bpp);
		return -EINVAL;
	}
	writel(tmp, engine + 0x04);

	if (op != VIA_BITBLT_FILL) {
		if (src_x & (op == VIA_BITBLT_MONO ? 0xFFFF8000 : 0xFFFFF000)
			|| src_y & 0xFFFFF000) {
			printk(KERN_WARNING "hw_bitblt_1: Unsupported source "
				"x/y %d %d\n", src_x, src_y);
			return -EINVAL;
		}
		tmp = src_x | (src_y << 16);
		writel(tmp, engine + 0x08);
	}

	if (dst_x & 0xFFFFF000 || dst_y & 0xFFFFF000) {
		printk(KERN_WARNING "hw_bitblt_1: Unsupported destination x/y "
			"%d %d\n", dst_x, dst_y);
		return -EINVAL;
	}
	tmp = dst_x | (dst_y << 16);
	writel(tmp, engine + 0x0C);

	if ((width - 1) & 0xFFFFF000 || (height - 1) & 0xFFFFF000) {
		printk(KERN_WARNING "hw_bitblt_1: Unsupported width/height "
			"%d %d\n", width, height);
		return -EINVAL;
	}
	tmp = (width - 1) | ((height - 1) << 16);
	writel(tmp, engine + 0x10);

	if (op != VIA_BITBLT_COLOR)
		writel(fg_color, engine + 0x18);

	if (op == VIA_BITBLT_MONO)
		writel(bg_color, engine + 0x1C);

	if (op != VIA_BITBLT_FILL) {
		tmp = src_mem ? 0 : src_addr;
		if (dst_addr & 0xE0000007) {
			printk(KERN_WARNING "hw_bitblt_1: Unsupported source "
				"address %X\n", tmp);
			return -EINVAL;
		}
		tmp >>= 3;
		writel(tmp, engine + 0x30);
	}

	if (dst_addr & 0xE0000007) {
		printk(KERN_WARNING "hw_bitblt_1: Unsupported destination "
			"address %X\n", dst_addr);
		return -EINVAL;
	}
	tmp = dst_addr >> 3;
	writel(tmp, engine + 0x34);

	if (op == VIA_BITBLT_FILL)
		tmp = 0;
	else
		tmp = src_pitch;
	if (tmp & 0xFFFFC007 || dst_pitch & 0xFFFFC007) {
		printk(KERN_WARNING "hw_bitblt_1: Unsupported pitch %X %X\n",
			tmp, dst_pitch);
		return -EINVAL;
	}
	tmp = (tmp >> 3) | (dst_pitch << (16 - 3));
	writel(tmp, engine + 0x38);

	if (op == VIA_BITBLT_FILL)
		ge_cmd |= fill_rop << 24 | 0x00002000 | 0x00000001;
	else {
		ge_cmd |= 0xCC000000; /* ROP=SRCCOPY */
		if (src_mem)
			ge_cmd |= 0x00000040;
		if (op == VIA_BITBLT_MONO)
			ge_cmd |= 0x00000002 | 0x00000100 | 0x00020000;
		else
			ge_cmd |= 0x00000001;
	}
	writel(ge_cmd, engine);

	if (op == VIA_BITBLT_FILL || !src_mem)
		return 0;

	tmp = (width * height * (op == VIA_BITBLT_MONO ? 1 : (dst_bpp >> 3)) +
		3) >> 2;

	for (i = 0; i < tmp; i++)
		writel(src_mem[i], engine + VIA_MMIO_BLTBASE);

	return 0;
}

static int hw_bitblt_2(void __iomem *engine, u8 op, u32 width, u32 height,
	u8 dst_bpp, u32 dst_addr, u32 dst_pitch, u32 dst_x, u32 dst_y,
	u32 *src_mem, u32 src_addr, u32 src_pitch, u32 src_x, u32 src_y,
	u32 fg_color, u32 bg_color, u8 fill_rop)
{
	u32 ge_cmd = 0, tmp, i;

	if (!op || op > 3) {
		printk(KERN_WARNING "hw_bitblt_2: Invalid operation: %d\n", op);
		return -EINVAL;
	}

	if (op != VIA_BITBLT_FILL && !src_mem && src_addr == dst_addr) {
		if (src_x < dst_x) {
			ge_cmd |= 0x00008000;
			src_x += width - 1;
			dst_x += width - 1;
		}
		if (src_y < dst_y) {
			ge_cmd |= 0x00004000;
			src_y += height - 1;
			dst_y += height - 1;
		}
	}

	if (op == VIA_BITBLT_FILL) {
		switch (fill_rop) {
		case 0x00: /* blackness */
		case 0x5A: /* pattern inversion */
		case 0xF0: /* pattern copy */
		case 0xFF: /* whiteness */
			break;
		default:
			printk(KERN_WARNING "hw_bitblt_2: Invalid fill rop: "
				"%u\n", fill_rop);
			return -EINVAL;
		}
	}

	switch (dst_bpp) {
	case 8:
		tmp = 0x00000000;
		break;
	case 16:
		tmp = 0x00000100;
		break;
	case 32:
		tmp = 0x00000300;
		break;
	default:
		printk(KERN_WARNING "hw_bitblt_2: Unsupported bpp %d\n",
			dst_bpp);
		return -EINVAL;
	}
	writel(tmp, engine + 0x04);

	if (op == VIA_BITBLT_FILL)
		tmp = 0;
	else
		tmp = src_pitch;
	if (tmp & 0xFFFFC007 || dst_pitch & 0xFFFFC007) {
		printk(KERN_WARNING "hw_bitblt_2: Unsupported pitch %X %X\n",
			tmp, dst_pitch);
		return -EINVAL;
	}
	tmp = (tmp >> 3) | (dst_pitch << (16 - 3));
	writel(tmp, engine + 0x08);

	if ((width - 1) & 0xFFFFF000 || (height - 1) & 0xFFFFF000) {
		printk(KERN_WARNING "hw_bitblt_2: Unsupported width/height "
			"%d %d\n", width, height);
		return -EINVAL;
	}
	tmp = (width - 1) | ((height - 1) << 16);
	writel(tmp, engine + 0x0C);

	if (dst_x & 0xFFFFF000 || dst_y & 0xFFFFF000) {
		printk(KERN_WARNING "hw_bitblt_2: Unsupported destination x/y "
			"%d %d\n", dst_x, dst_y);
		return -EINVAL;
	}
	tmp = dst_x | (dst_y << 16);
	writel(tmp, engine + 0x10);

	if (dst_addr & 0xE0000007) {
		printk(KERN_WARNING "hw_bitblt_2: Unsupported destination "
			"address %X\n", dst_addr);
		return -EINVAL;
	}
	tmp = dst_addr >> 3;
	writel(tmp, engine + 0x14);

	if (op != VIA_BITBLT_FILL) {
		if (src_x & (op == VIA_BITBLT_MONO ? 0xFFFF8000 : 0xFFFFF000)
			|| src_y & 0xFFFFF000) {
			printk(KERN_WARNING "hw_bitblt_2: Unsupported source "
				"x/y %d %d\n", src_x, src_y);
			return -EINVAL;
		}
		tmp = src_x | (src_y << 16);
		writel(tmp, engine + 0x18);

		tmp = src_mem ? 0 : src_addr;
		if (dst_addr & 0xE0000007) {
			printk(KERN_WARNING "hw_bitblt_2: Unsupported source "
				"address %X\n", tmp);
			return -EINVAL;
		}
		tmp >>= 3;
		writel(tmp, engine + 0x1C);
	}

	if (op != VIA_BITBLT_COLOR)
		writel(fg_color, engine + 0x4C);

	if (op == VIA_BITBLT_MONO)
		writel(bg_color, engine + 0x50);

	if (op == VIA_BITBLT_FILL)
		ge_cmd |= fill_rop << 24 | 0x00002000 | 0x00000001;
	else {
		ge_cmd |= 0xCC000000; /* ROP=SRCCOPY */
		if (src_mem)
			ge_cmd |= 0x00000040;
		if (op == VIA_BITBLT_MONO)
			ge_cmd |= 0x00000002 | 0x00000100 | 0x00020000;
		else
			ge_cmd |= 0x00000001;
	}
	writel(ge_cmd, engine);

	if (op == VIA_BITBLT_FILL || !src_mem)
		return 0;

	tmp = (width * height * (op == VIA_BITBLT_MONO ? 1 : (dst_bpp >> 3)) +
		3) >> 2;

	for (i = 0; i < tmp; i++)
		writel(src_mem[i], engine + VIA_MMIO_BLTBASE);

	return 0;
}

void viafb_init_accel(struct viafb_shared *shared)
{
	switch (shared->chip_info.gfx_chip_name) {
	case UNICHROME_CLE266:
	case UNICHROME_K400:
	case UNICHROME_K800:
	case UNICHROME_PM800:
	case UNICHROME_CN700:
	case UNICHROME_CX700:
	case UNICHROME_CN750:
	case UNICHROME_K8M890:
	case UNICHROME_P4M890:
	case UNICHROME_P4M900:
		shared->hw_bitblt = hw_bitblt_1;
		break;
	case UNICHROME_VX800:
		shared->hw_bitblt = hw_bitblt_2;
		break;
	default:
		shared->hw_bitblt = NULL;
	}

	viaparinfo->fbmem_free -= CURSOR_SIZE;
	viaparinfo->cursor_start = viaparinfo->fbmem_free;
	viaparinfo->fbmem_used += CURSOR_SIZE;

	/* Reverse 8*1024 memory space for cursor image */
	viaparinfo->fbmem_free -= (CURSOR_SIZE + VQ_SIZE);
	viaparinfo->VQ_start = viaparinfo->fbmem_free;
	viaparinfo->VQ_end = viaparinfo->VQ_start + VQ_SIZE - 1;
	viaparinfo->fbmem_used += (CURSOR_SIZE + VQ_SIZE);
}

void viafb_init_2d_engine(void)
{
	u32 dwVQStartAddr, dwVQEndAddr;
	u32 dwVQLen, dwVQStartL, dwVQEndL, dwVQStartEndH;

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
			VIA_VR_QUEUE_BUSY) && (loop < MAXLOOP)) {
		loop++;
		cpu_relax();
	}

	while ((readl(viaparinfo->io_virt + VIA_REG_STATUS) &
		    (VIA_CMD_RGTR_BUSY | VIA_2D_ENG_BUSY | VIA_3D_ENG_BUSY)) &&
		    (loop < MAXLOOP)) {
		loop++;
		cpu_relax();
	}

	return loop >= MAXLOOP;
}
