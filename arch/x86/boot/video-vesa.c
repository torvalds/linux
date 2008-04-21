/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * VESA text modes
 */

#include "boot.h"
#include "video.h"
#include "vesa.h"

/* VESA information */
static struct vesa_general_info vginfo;
static struct vesa_mode_info vminfo;

__videocard video_vesa;

#ifndef _WAKEUP
static void vesa_store_mode_params_graphics(void);
#else /* _WAKEUP */
static inline void vesa_store_mode_params_graphics(void) {}
#endif /* _WAKEUP */

static int vesa_probe(void)
{
#if defined(CONFIG_VIDEO_VESA) || defined(CONFIG_FIRMWARE_EDID)
	u16 ax, cx, di;
	u16 mode;
	addr_t mode_ptr;
	struct mode_info *mi;
	int nmodes = 0;

	video_vesa.modes = GET_HEAP(struct mode_info, 0);

	ax = 0x4f00;
	di = (size_t)&vginfo;
	asm(INT10
	    : "+a" (ax), "+D" (di), "=m" (vginfo)
	    : : "ebx", "ecx", "edx", "esi");

	if (ax != 0x004f ||
	    vginfo.signature != VESA_MAGIC ||
	    vginfo.version < 0x0102)
		return 0;	/* Not present */
#endif /* CONFIG_VIDEO_VESA || CONFIG_FIRMWARE_EDID */
#ifdef CONFIG_VIDEO_VESA
	set_fs(vginfo.video_mode_ptr.seg);
	mode_ptr = vginfo.video_mode_ptr.off;

	while ((mode = rdfs16(mode_ptr)) != 0xffff) {
		mode_ptr += 2;

		if (!heap_free(sizeof(struct mode_info)))
			break;	/* Heap full, can't save mode info */

		if (mode & ~0x1ff)
			continue;

		memset(&vminfo, 0, sizeof vminfo); /* Just in case... */

		ax = 0x4f01;
		cx = mode;
		di = (size_t)&vminfo;
		asm(INT10
		    : "+a" (ax), "+c" (cx), "+D" (di), "=m" (vminfo)
		    : : "ebx", "edx", "esi");

		if (ax != 0x004f)
			continue;

		if ((vminfo.mode_attr & 0x15) == 0x05) {
			/* Text Mode, TTY BIOS supported,
			   supported by hardware */
			mi = GET_HEAP(struct mode_info, 1);
			mi->mode  = mode + VIDEO_FIRST_VESA;
			mi->depth = 0; /* text */
			mi->x     = vminfo.h_res;
			mi->y     = vminfo.v_res;
			nmodes++;
		} else if ((vminfo.mode_attr & 0x99) == 0x99 &&
			   (vminfo.memory_layout == 4 ||
			    vminfo.memory_layout == 6) &&
			   vminfo.memory_planes == 1) {
#ifdef CONFIG_FB
			/* Graphics mode, color, linear frame buffer
			   supported.  Only register the mode if
			   if framebuffer is configured, however,
			   otherwise the user will be left without a screen.
			   We don't require CONFIG_FB_VESA, however, since
			   some of the other framebuffer drivers can use
			   this mode-setting, too. */
			mi = GET_HEAP(struct mode_info, 1);
			mi->mode = mode + VIDEO_FIRST_VESA;
			mi->depth = vminfo.bpp;
			mi->x = vminfo.h_res;
			mi->y = vminfo.v_res;
			nmodes++;
#endif
		}
	}

	return nmodes;
#else
	return 0;
#endif /* CONFIG_VIDEO_VESA */
}

static int vesa_set_mode(struct mode_info *mode)
{
	u16 ax, bx, cx, di;
	int is_graphic;
	u16 vesa_mode = mode->mode - VIDEO_FIRST_VESA;

	memset(&vminfo, 0, sizeof vminfo); /* Just in case... */

	ax = 0x4f01;
	cx = vesa_mode;
	di = (size_t)&vminfo;
	asm(INT10
	    : "+a" (ax), "+c" (cx), "+D" (di), "=m" (vminfo)
	    : : "ebx", "edx", "esi");

	if (ax != 0x004f)
		return -1;

	if ((vminfo.mode_attr & 0x15) == 0x05) {
		/* It's a supported text mode */
		is_graphic = 0;
	} else if ((vminfo.mode_attr & 0x99) == 0x99) {
		/* It's a graphics mode with linear frame buffer */
		is_graphic = 1;
		vesa_mode |= 0x4000; /* Request linear frame buffer */
	} else {
		return -1;	/* Invalid mode */
	}


	ax = 0x4f02;
	bx = vesa_mode;
	di = 0;
	asm volatile(INT10
		     : "+a" (ax), "+b" (bx), "+D" (di)
		     : : "ecx", "edx", "esi");

	if (ax != 0x004f)
		return -1;

	graphic_mode = is_graphic;
	if (!is_graphic) {
		/* Text mode */
		force_x = mode->x;
		force_y = mode->y;
		do_restore = 1;
	} else {
		/* Graphics mode */
		vesa_store_mode_params_graphics();
	}

	return 0;
}


#ifndef _WAKEUP

/* Switch DAC to 8-bit mode */
static void vesa_dac_set_8bits(void)
{
	u8 dac_size = 6;

	/* If possible, switch the DAC to 8-bit mode */
	if (vginfo.capabilities & 1) {
		u16 ax, bx;

		ax = 0x4f08;
		bx = 0x0800;
		asm volatile(INT10
			     : "+a" (ax), "+b" (bx)
			     : : "ecx", "edx", "esi", "edi");

		if (ax == 0x004f)
			dac_size = bx >> 8;
	}

	/* Set the color sizes to the DAC size, and offsets to 0 */
	boot_params.screen_info.red_size = dac_size;
	boot_params.screen_info.green_size = dac_size;
	boot_params.screen_info.blue_size = dac_size;
	boot_params.screen_info.rsvd_size = dac_size;

	boot_params.screen_info.red_pos = 0;
	boot_params.screen_info.green_pos = 0;
	boot_params.screen_info.blue_pos = 0;
	boot_params.screen_info.rsvd_pos = 0;
}

/* Save the VESA protected mode info */
static void vesa_store_pm_info(void)
{
	u16 ax, bx, di, es;

	ax = 0x4f0a;
	bx = di = 0;
	asm("pushw %%es; "INT10"; movw %%es,%0; popw %%es"
	    : "=d" (es), "+a" (ax), "+b" (bx), "+D" (di)
	    : : "ecx", "esi");

	if (ax != 0x004f)
		return;

	boot_params.screen_info.vesapm_seg = es;
	boot_params.screen_info.vesapm_off = di;
}

/*
 * Save video mode parameters for graphics mode
 */
static void vesa_store_mode_params_graphics(void)
{
	/* Tell the kernel we're in VESA graphics mode */
	boot_params.screen_info.orig_video_isVGA = 0x23;

	/* Mode parameters */
	boot_params.screen_info.vesa_attributes = vminfo.mode_attr;
	boot_params.screen_info.lfb_linelength = vminfo.logical_scan;
	boot_params.screen_info.lfb_width = vminfo.h_res;
	boot_params.screen_info.lfb_height = vminfo.v_res;
	boot_params.screen_info.lfb_depth = vminfo.bpp;
	boot_params.screen_info.pages = vminfo.image_planes;
	boot_params.screen_info.lfb_base = vminfo.lfb_ptr;
	memcpy(&boot_params.screen_info.red_size,
	       &vminfo.rmask, 8);

	/* General parameters */
	boot_params.screen_info.lfb_size = vginfo.total_memory;

	if (vminfo.bpp <= 8)
		vesa_dac_set_8bits();

	vesa_store_pm_info();
}

/*
 * Save EDID information for the kernel; this is invoked, separately,
 * after mode-setting.
 */
void vesa_store_edid(void)
{
#ifdef CONFIG_FIRMWARE_EDID
	u16 ax, bx, cx, dx, di;

	/* Apparently used as a nonsense token... */
	memset(&boot_params.edid_info, 0x13, sizeof boot_params.edid_info);

	if (vginfo.version < 0x0200)
		return;		/* EDID requires VBE 2.0+ */

	ax = 0x4f15;		/* VBE DDC */
	bx = 0x0000;		/* Report DDC capabilities */
	cx = 0;			/* Controller 0 */
	di = 0;			/* ES:DI must be 0 by spec */

	/* Note: The VBE DDC spec is different from the main VESA spec;
	   we genuinely have to assume all registers are destroyed here. */

	asm("pushw %%es; movw %2,%%es; "INT10"; popw %%es"
	    : "+a" (ax), "+b" (bx)
	    :  "c" (cx), "D" (di)
	    : "esi");

	if (ax != 0x004f)
		return;		/* No EDID */

	/* BH = time in seconds to transfer EDD information */
	/* BL = DDC level supported */

	ax = 0x4f15;		/* VBE DDC */
	bx = 0x0001;		/* Read EDID */
	cx = 0;			/* Controller 0 */
	dx = 0;			/* EDID block number */
	di =(size_t) &boot_params.edid_info; /* (ES:)Pointer to block */
	asm(INT10
	    : "+a" (ax), "+b" (bx), "+d" (dx), "=m" (boot_params.edid_info)
	    : "c" (cx), "D" (di)
	    : "esi");
#endif /* CONFIG_FIRMWARE_EDID */
}

#endif /* not _WAKEUP */

__videocard video_vesa =
{
	.card_name	= "VESA",
	.probe		= vesa_probe,
	.set_mode	= vesa_set_mode,
	.xmode_first	= VIDEO_FIRST_VESA,
	.xmode_n	= 0x200,
};
