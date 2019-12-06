/* -----------------------------------------------------------------------
 *
 *   Copyright 2011 Intel Corporation; author Matt Fleming
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

#include <linux/efi.h>
#include <linux/screen_info.h>
#include <asm/efi.h>
#include <asm/setup.h>

static void find_bits(unsigned long mask, u8 *pos, u8 *size)
{
	u8 first, len;

	first = 0;
	len = 0;

	if (mask) {
		while (!(mask & 0x1)) {
			mask = mask >> 1;
			first++;
		}

		while (mask & 0x1) {
			mask = mask >> 1;
			len++;
		}
	}

	*pos = first;
	*size = len;
}

static void
setup_pixel_info(struct screen_info *si, u32 pixels_per_scan_line,
		 struct efi_pixel_bitmask pixel_info, int pixel_format)
{
	if (pixel_format == PIXEL_RGB_RESERVED_8BIT_PER_COLOR) {
		si->lfb_depth = 32;
		si->lfb_linelength = pixels_per_scan_line * 4;
		si->red_size = 8;
		si->red_pos = 0;
		si->green_size = 8;
		si->green_pos = 8;
		si->blue_size = 8;
		si->blue_pos = 16;
		si->rsvd_size = 8;
		si->rsvd_pos = 24;
	} else if (pixel_format == PIXEL_BGR_RESERVED_8BIT_PER_COLOR) {
		si->lfb_depth = 32;
		si->lfb_linelength = pixels_per_scan_line * 4;
		si->red_size = 8;
		si->red_pos = 16;
		si->green_size = 8;
		si->green_pos = 8;
		si->blue_size = 8;
		si->blue_pos = 0;
		si->rsvd_size = 8;
		si->rsvd_pos = 24;
	} else if (pixel_format == PIXEL_BIT_MASK) {
		find_bits(pixel_info.red_mask, &si->red_pos, &si->red_size);
		find_bits(pixel_info.green_mask, &si->green_pos,
			  &si->green_size);
		find_bits(pixel_info.blue_mask, &si->blue_pos, &si->blue_size);
		find_bits(pixel_info.reserved_mask, &si->rsvd_pos,
			  &si->rsvd_size);
		si->lfb_depth = si->red_size + si->green_size +
			si->blue_size + si->rsvd_size;
		si->lfb_linelength = (pixels_per_scan_line * si->lfb_depth) / 8;
	} else {
		si->lfb_depth = 4;
		si->lfb_linelength = si->lfb_width / 2;
		si->red_size = 0;
		si->red_pos = 0;
		si->green_size = 0;
		si->green_pos = 0;
		si->blue_size = 0;
		si->blue_pos = 0;
		si->rsvd_size = 0;
		si->rsvd_pos = 0;
	}
}

static efi_status_t
setup_gop32(efi_system_table_t *sys_table_arg, struct screen_info *si,
            efi_guid_t *proto, unsigned long size, void **gop_handle)
{
	struct efi_graphics_output_protocol_32 *gop32, *first_gop;
	unsigned long nr_gops;
	u16 width, height;
	u32 pixels_per_scan_line;
	u32 ext_lfb_base;
	u64 fb_base;
	struct efi_pixel_bitmask pixel_info;
	int pixel_format;
	efi_status_t status;
	u32 *handles = (u32 *)(unsigned long)gop_handle;
	int i;

	first_gop = NULL;
	gop32 = NULL;

	nr_gops = size / sizeof(u32);
	for (i = 0; i < nr_gops; i++) {
		struct efi_graphics_output_protocol_mode_32 *mode;
		struct efi_graphics_output_mode_info *info = NULL;
		efi_guid_t conout_proto = EFI_CONSOLE_OUT_DEVICE_GUID;
		bool conout_found = false;
		void *dummy = NULL;
		efi_handle_t h = (efi_handle_t)(unsigned long)handles[i];
		u64 current_fb_base;

		status = efi_call_early(handle_protocol, h,
					proto, (void **)&gop32);
		if (status != EFI_SUCCESS)
			continue;

		status = efi_call_early(handle_protocol, h,
					&conout_proto, &dummy);
		if (status == EFI_SUCCESS)
			conout_found = true;

		mode = (void *)(unsigned long)gop32->mode;
		info = (void *)(unsigned long)mode->info;
		current_fb_base = mode->frame_buffer_base;

		if ((!first_gop || conout_found) &&
		    info->pixel_format != PIXEL_BLT_ONLY) {
			/*
			 * Systems that use the UEFI Console Splitter may
			 * provide multiple GOP devices, not all of which are
			 * backed by real hardware. The workaround is to search
			 * for a GOP implementing the ConOut protocol, and if
			 * one isn't found, to just fall back to the first GOP.
			 */
			width = info->horizontal_resolution;
			height = info->vertical_resolution;
			pixel_format = info->pixel_format;
			pixel_info = info->pixel_information;
			pixels_per_scan_line = info->pixels_per_scan_line;
			fb_base = current_fb_base;

			/*
			 * Once we've found a GOP supporting ConOut,
			 * don't bother looking any further.
			 */
			first_gop = gop32;
			if (conout_found)
				break;
		}
	}

	/* Did we find any GOPs? */
	if (!first_gop)
		return EFI_NOT_FOUND;

	/* EFI framebuffer */
	si->orig_video_isVGA = VIDEO_TYPE_EFI;

	si->lfb_width = width;
	si->lfb_height = height;
	si->lfb_base = fb_base;

	ext_lfb_base = (u64)(unsigned long)fb_base >> 32;
	if (ext_lfb_base) {
		si->capabilities |= VIDEO_CAPABILITY_64BIT_BASE;
		si->ext_lfb_base = ext_lfb_base;
	}

	si->pages = 1;

	setup_pixel_info(si, pixels_per_scan_line, pixel_info, pixel_format);

	si->lfb_size = si->lfb_linelength * si->lfb_height;

	si->capabilities |= VIDEO_CAPABILITY_SKIP_QUIRKS;

	return EFI_SUCCESS;
}

static efi_status_t
setup_gop64(efi_system_table_t *sys_table_arg, struct screen_info *si,
	    efi_guid_t *proto, unsigned long size, void **gop_handle)
{
	struct efi_graphics_output_protocol_64 *gop64, *first_gop;
	unsigned long nr_gops;
	u16 width, height;
	u32 pixels_per_scan_line;
	u32 ext_lfb_base;
	u64 fb_base;
	struct efi_pixel_bitmask pixel_info;
	int pixel_format;
	efi_status_t status;
	u64 *handles = (u64 *)(unsigned long)gop_handle;
	int i;

	first_gop = NULL;
	gop64 = NULL;

	nr_gops = size / sizeof(u64);
	for (i = 0; i < nr_gops; i++) {
		struct efi_graphics_output_protocol_mode_64 *mode;
		struct efi_graphics_output_mode_info *info = NULL;
		efi_guid_t conout_proto = EFI_CONSOLE_OUT_DEVICE_GUID;
		bool conout_found = false;
		void *dummy = NULL;
		efi_handle_t h = (efi_handle_t)(unsigned long)handles[i];
		u64 current_fb_base;

		status = efi_call_early(handle_protocol, h,
					proto, (void **)&gop64);
		if (status != EFI_SUCCESS)
			continue;

		status = efi_call_early(handle_protocol, h,
					&conout_proto, &dummy);
		if (status == EFI_SUCCESS)
			conout_found = true;

		mode = (void *)(unsigned long)gop64->mode;
		info = (void *)(unsigned long)mode->info;
		current_fb_base = mode->frame_buffer_base;

		if ((!first_gop || conout_found) &&
		    info->pixel_format != PIXEL_BLT_ONLY) {
			/*
			 * Systems that use the UEFI Console Splitter may
			 * provide multiple GOP devices, not all of which are
			 * backed by real hardware. The workaround is to search
			 * for a GOP implementing the ConOut protocol, and if
			 * one isn't found, to just fall back to the first GOP.
			 */
			width = info->horizontal_resolution;
			height = info->vertical_resolution;
			pixel_format = info->pixel_format;
			pixel_info = info->pixel_information;
			pixels_per_scan_line = info->pixels_per_scan_line;
			fb_base = current_fb_base;

			/*
			 * Once we've found a GOP supporting ConOut,
			 * don't bother looking any further.
			 */
			first_gop = gop64;
			if (conout_found)
				break;
		}
	}

	/* Did we find any GOPs? */
	if (!first_gop)
		return EFI_NOT_FOUND;

	/* EFI framebuffer */
	si->orig_video_isVGA = VIDEO_TYPE_EFI;

	si->lfb_width = width;
	si->lfb_height = height;
	si->lfb_base = fb_base;

	ext_lfb_base = (u64)(unsigned long)fb_base >> 32;
	if (ext_lfb_base) {
		si->capabilities |= VIDEO_CAPABILITY_64BIT_BASE;
		si->ext_lfb_base = ext_lfb_base;
	}

	si->pages = 1;

	setup_pixel_info(si, pixels_per_scan_line, pixel_info, pixel_format);

	si->lfb_size = si->lfb_linelength * si->lfb_height;

	si->capabilities |= VIDEO_CAPABILITY_SKIP_QUIRKS;

	return EFI_SUCCESS;
}

/*
 * See if we have Graphics Output Protocol
 */
efi_status_t efi_setup_gop(efi_system_table_t *sys_table_arg,
			   struct screen_info *si, efi_guid_t *proto,
			   unsigned long size)
{
	efi_status_t status;
	void **gop_handle = NULL;

	status = efi_call_early(allocate_pool, EFI_LOADER_DATA,
				size, (void **)&gop_handle);
	if (status != EFI_SUCCESS)
		return status;

	status = efi_call_early(locate_handle,
				EFI_LOCATE_BY_PROTOCOL,
				proto, NULL, &size, gop_handle);
	if (status != EFI_SUCCESS)
		goto free_handle;

	if (efi_is_64bit()) {
		status = setup_gop64(sys_table_arg, si, proto, size,
				     gop_handle);
	} else {
		status = setup_gop32(sys_table_arg, si, proto, size,
				     gop_handle);
	}

free_handle:
	efi_call_early(free_pool, gop_handle);
	return status;
}
