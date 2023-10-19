// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.

 */
#include <linux/via-core.h>
#include "global.h"

/*
 * Figure out an appropriate bytes-per-pixel setting.
 */
static int viafb_set_bpp(void __iomem *engine, u8 bpp)
{
	u32 gemode;

	/* Preserve the reserved bits */
	/* Lowest 2 bits to zero gives us no rotation */
	gemode = readl(engine + VIA_REG_GEMODE) & 0xfffffcfc;
	switch (bpp) {
	case 8:
		gemode |= VIA_GEM_8bpp;
		break;
	case 16:
		gemode |= VIA_GEM_16bpp;
		break;
	case 32:
		gemode |= VIA_GEM_32bpp;
		break;
	default:
		printk(KERN_WARNING "viafb_set_bpp: Unsupported bpp %d\n", bpp);
		return -EINVAL;
	}
	writel(gemode, engine + VIA_REG_GEMODE);
	return 0;
}


static int hw_bitblt_1(void __iomem *engine, u8 op, u32 width, u32 height,
	u8 dst_bpp, u32 dst_addr, u32 dst_pitch, u32 dst_x, u32 dst_y,
	u32 *src_mem, u32 src_addr, u32 src_pitch, u32 src_x, u32 src_y,
	u32 fg_color, u32 bg_color, u8 fill_rop)
{
	u32 ge_cmd = 0, tmp, i;
	int ret;

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

	ret = viafb_set_bpp(engine, dst_bpp);
	if (ret)
		return ret;

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
	tmp = VIA_PITCH_ENABLE | (tmp >> 3) | (dst_pitch << (16 - 3));
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
	int ret;

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

	ret = viafb_set_bpp(engine, dst_bpp);
	if (ret)
		return ret;

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

	if (op == VIA_BITBLT_FILL) {
		writel(fg_color, engine + 0x58);
	} else if (op == VIA_BITBLT_MONO) {
		writel(fg_color, engine + 0x4C);
		writel(bg_color, engine + 0x50);
	}

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

int viafb_setup_engine(struct fb_info *info)
{
	struct viafb_par *viapar = info->par;
	void __iomem *engine;
	u32 chip_name = viapar->shared->chip_info.gfx_chip_name;

	engine = viapar->shared->vdev->engine_mmio;
	if (!engine) {
		printk(KERN_WARNING "viafb_init_accel: ioremap failed, "
			"hardware acceleration disabled\n");
		return -ENOMEM;
	}

	switch (chip_name) {
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
		viapar->shared->hw_bitblt = hw_bitblt_1;
		break;
	case UNICHROME_VX800:
	case UNICHROME_VX855:
	case UNICHROME_VX900:
		viapar->shared->hw_bitblt = hw_bitblt_2;
		break;
	default:
		viapar->shared->hw_bitblt = NULL;
	}

	viapar->fbmem_free -= CURSOR_SIZE;
	viapar->shared->cursor_vram_addr = viapar->fbmem_free;
	viapar->fbmem_used += CURSOR_SIZE;

	viapar->fbmem_free -= VQ_SIZE;
	viapar->shared->vq_vram_addr = viapar->fbmem_free;
	viapar->fbmem_used += VQ_SIZE;

#if IS_ENABLED(CONFIG_VIDEO_VIA_CAMERA)
	/*
	 * Set aside a chunk of framebuffer memory for the camera
	 * driver.  Someday this driver probably needs a proper allocator
	 * for fbmem; for now, we just have to do this before the
	 * framebuffer initializes itself.
	 *
	 * As for the size: the engine can handle three frames,
	 * 16 bits deep, up to VGA resolution.
	 */
	viapar->shared->vdev->camera_fbmem_size = 3*VGA_HEIGHT*VGA_WIDTH*2;
	viapar->fbmem_free -= viapar->shared->vdev->camera_fbmem_size;
	viapar->fbmem_used += viapar->shared->vdev->camera_fbmem_size;
	viapar->shared->vdev->camera_fbmem_offset = viapar->fbmem_free;
#endif

	viafb_reset_engine(viapar);
	return 0;
}

void viafb_reset_engine(struct viafb_par *viapar)
{
	void __iomem *engine = viapar->shared->vdev->engine_mmio;
	int highest_reg, i;
	u32 vq_start_addr, vq_end_addr, vq_start_low, vq_end_low, vq_high,
		vq_len, chip_name = viapar->shared->chip_info.gfx_chip_name;

	/* Initialize registers to reset the 2D engine */
	switch (viapar->shared->chip_info.twod_engine) {
	case VIA_2D_ENG_M1:
		highest_reg = 0x5c;
		break;
	default:
		highest_reg = 0x40;
		break;
	}
	for (i = 0; i <= highest_reg; i += 4)
		writel(0x0, engine + i);

	/* Init AGP and VQ regs */
	switch (chip_name) {
	case UNICHROME_K8M890:
	case UNICHROME_P4M900:
	case UNICHROME_VX800:
	case UNICHROME_VX855:
	case UNICHROME_VX900:
		writel(0x00100000, engine + VIA_REG_CR_TRANSET);
		writel(0x680A0000, engine + VIA_REG_CR_TRANSPACE);
		writel(0x02000000, engine + VIA_REG_CR_TRANSPACE);
		break;

	default:
		writel(0x00100000, engine + VIA_REG_TRANSET);
		writel(0x00000000, engine + VIA_REG_TRANSPACE);
		writel(0x00333004, engine + VIA_REG_TRANSPACE);
		writel(0x60000000, engine + VIA_REG_TRANSPACE);
		writel(0x61000000, engine + VIA_REG_TRANSPACE);
		writel(0x62000000, engine + VIA_REG_TRANSPACE);
		writel(0x63000000, engine + VIA_REG_TRANSPACE);
		writel(0x64000000, engine + VIA_REG_TRANSPACE);
		writel(0x7D000000, engine + VIA_REG_TRANSPACE);

		writel(0xFE020000, engine + VIA_REG_TRANSET);
		writel(0x00000000, engine + VIA_REG_TRANSPACE);
		break;
	}

	/* Enable VQ */
	vq_start_addr = viapar->shared->vq_vram_addr;
	vq_end_addr = viapar->shared->vq_vram_addr + VQ_SIZE - 1;

	vq_start_low = 0x50000000 | (vq_start_addr & 0xFFFFFF);
	vq_end_low = 0x51000000 | (vq_end_addr & 0xFFFFFF);
	vq_high = 0x52000000 | ((vq_start_addr & 0xFF000000) >> 24) |
		((vq_end_addr & 0xFF000000) >> 16);
	vq_len = 0x53000000 | (VQ_SIZE >> 3);

	switch (chip_name) {
	case UNICHROME_K8M890:
	case UNICHROME_P4M900:
	case UNICHROME_VX800:
	case UNICHROME_VX855:
	case UNICHROME_VX900:
		vq_start_low |= 0x20000000;
		vq_end_low |= 0x20000000;
		vq_high |= 0x20000000;
		vq_len |= 0x20000000;

		writel(0x00100000, engine + VIA_REG_CR_TRANSET);
		writel(vq_high, engine + VIA_REG_CR_TRANSPACE);
		writel(vq_start_low, engine + VIA_REG_CR_TRANSPACE);
		writel(vq_end_low, engine + VIA_REG_CR_TRANSPACE);
		writel(vq_len, engine + VIA_REG_CR_TRANSPACE);
		writel(0x74301001, engine + VIA_REG_CR_TRANSPACE);
		writel(0x00000000, engine + VIA_REG_CR_TRANSPACE);
		break;
	default:
		writel(0x00FE0000, engine + VIA_REG_TRANSET);
		writel(0x080003FE, engine + VIA_REG_TRANSPACE);
		writel(0x0A00027C, engine + VIA_REG_TRANSPACE);
		writel(0x0B000260, engine + VIA_REG_TRANSPACE);
		writel(0x0C000274, engine + VIA_REG_TRANSPACE);
		writel(0x0D000264, engine + VIA_REG_TRANSPACE);
		writel(0x0E000000, engine + VIA_REG_TRANSPACE);
		writel(0x0F000020, engine + VIA_REG_TRANSPACE);
		writel(0x1000027E, engine + VIA_REG_TRANSPACE);
		writel(0x110002FE, engine + VIA_REG_TRANSPACE);
		writel(0x200F0060, engine + VIA_REG_TRANSPACE);

		writel(0x00000006, engine + VIA_REG_TRANSPACE);
		writel(0x40008C0F, engine + VIA_REG_TRANSPACE);
		writel(0x44000000, engine + VIA_REG_TRANSPACE);
		writel(0x45080C04, engine + VIA_REG_TRANSPACE);
		writel(0x46800408, engine + VIA_REG_TRANSPACE);

		writel(vq_high, engine + VIA_REG_TRANSPACE);
		writel(vq_start_low, engine + VIA_REG_TRANSPACE);
		writel(vq_end_low, engine + VIA_REG_TRANSPACE);
		writel(vq_len, engine + VIA_REG_TRANSPACE);
		break;
	}

	/* Set Cursor Image Base Address */
	writel(viapar->shared->cursor_vram_addr, engine + VIA_REG_CURSOR_MODE);
	writel(0x0, engine + VIA_REG_CURSOR_POS);
	writel(0x0, engine + VIA_REG_CURSOR_ORG);
	writel(0x0, engine + VIA_REG_CURSOR_BG);
	writel(0x0, engine + VIA_REG_CURSOR_FG);
	return;
}

void viafb_show_hw_cursor(struct fb_info *info, int Status)
{
	struct viafb_par *viapar = info->par;
	u32 temp, iga_path = viapar->iga_path;

	temp = readl(viapar->shared->vdev->engine_mmio + VIA_REG_CURSOR_MODE);
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
	writel(temp, viapar->shared->vdev->engine_mmio + VIA_REG_CURSOR_MODE);
}

void viafb_wait_engine_idle(struct fb_info *info)
{
	struct viafb_par *viapar = info->par;
	int loop = 0;
	u32 mask;
	void __iomem *engine = viapar->shared->vdev->engine_mmio;

	switch (viapar->shared->chip_info.twod_engine) {
	case VIA_2D_ENG_H5:
	case VIA_2D_ENG_M1:
		mask = VIA_CMD_RGTR_BUSY_M1 | VIA_2D_ENG_BUSY_M1 |
			      VIA_3D_ENG_BUSY_M1;
		break;
	default:
		while (!(readl(engine + VIA_REG_STATUS) &
				VIA_VR_QUEUE_BUSY) && (loop < MAXLOOP)) {
			loop++;
			cpu_relax();
		}
		mask = VIA_CMD_RGTR_BUSY | VIA_2D_ENG_BUSY | VIA_3D_ENG_BUSY;
		break;
	}

	while ((readl(engine + VIA_REG_STATUS) & mask) && (loop < MAXLOOP)) {
		loop++;
		cpu_relax();
	}

	if (loop >= MAXLOOP)
		printk(KERN_ERR "viafb_wait_engine_idle: not syncing\n");
}
