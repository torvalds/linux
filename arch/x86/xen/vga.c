// SPDX-License-Identifier: GPL-2.0
#include <linux/screen_info.h>
#include <linux/init.h>

#include <asm/bootparam.h>
#include <asm/setup.h>

#include <xen/interface/xen.h>

#include "xen-ops.h"

void __init xen_init_vga(const struct dom0_vga_console_info *info, size_t size)
{
	struct screen_info *screen_info = &boot_params.screen_info;

	/* This is drawn from a dump from vgacon:startup in
	 * standard Linux. */
	screen_info->orig_video_mode = 3;
	screen_info->orig_video_isVGA = 1;
	screen_info->orig_video_lines = 25;
	screen_info->orig_video_cols = 80;
	screen_info->orig_video_ega_bx = 3;
	screen_info->orig_video_points = 16;
	screen_info->orig_y = screen_info->orig_video_lines - 1;

	switch (info->video_type) {
	case XEN_VGATYPE_TEXT_MODE_3:
		if (size < offsetof(struct dom0_vga_console_info, u.text_mode_3)
		    + sizeof(info->u.text_mode_3))
			break;
		screen_info->orig_video_lines = info->u.text_mode_3.rows;
		screen_info->orig_video_cols = info->u.text_mode_3.columns;
		screen_info->orig_x = info->u.text_mode_3.cursor_x;
		screen_info->orig_y = info->u.text_mode_3.cursor_y;
		screen_info->orig_video_points =
			info->u.text_mode_3.font_height;
		break;

	case XEN_VGATYPE_EFI_LFB:
	case XEN_VGATYPE_VESA_LFB:
		if (size < offsetof(struct dom0_vga_console_info,
				    u.vesa_lfb.gbl_caps))
			break;
		screen_info->orig_video_isVGA = VIDEO_TYPE_VLFB;
		screen_info->lfb_width = info->u.vesa_lfb.width;
		screen_info->lfb_height = info->u.vesa_lfb.height;
		screen_info->lfb_depth = info->u.vesa_lfb.bits_per_pixel;
		screen_info->lfb_base = info->u.vesa_lfb.lfb_base;
		screen_info->lfb_size = info->u.vesa_lfb.lfb_size;
		screen_info->lfb_linelength = info->u.vesa_lfb.bytes_per_line;
		screen_info->red_size = info->u.vesa_lfb.red_size;
		screen_info->red_pos = info->u.vesa_lfb.red_pos;
		screen_info->green_size = info->u.vesa_lfb.green_size;
		screen_info->green_pos = info->u.vesa_lfb.green_pos;
		screen_info->blue_size = info->u.vesa_lfb.blue_size;
		screen_info->blue_pos = info->u.vesa_lfb.blue_pos;
		screen_info->rsvd_size = info->u.vesa_lfb.rsvd_size;
		screen_info->rsvd_pos = info->u.vesa_lfb.rsvd_pos;

		if (info->video_type == XEN_VGATYPE_EFI_LFB) {
			screen_info->orig_video_isVGA = VIDEO_TYPE_EFI;
			break;
		}

		if (size >= offsetof(struct dom0_vga_console_info,
				     u.vesa_lfb.mode_attrs)
		    + sizeof(info->u.vesa_lfb.mode_attrs))
			screen_info->vesa_attributes = info->u.vesa_lfb.mode_attrs;

		if (size >= offsetof(struct dom0_vga_console_info,
				     u.vesa_lfb.ext_lfb_base)
		    + sizeof(info->u.vesa_lfb.ext_lfb_base)
		    && info->u.vesa_lfb.ext_lfb_base) {
			screen_info->ext_lfb_base = info->u.vesa_lfb.ext_lfb_base;
			screen_info->capabilities |= VIDEO_CAPABILITY_64BIT_BASE;
		}
		break;
	}
}
