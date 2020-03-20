// SPDX-License-Identifier: GPL-2.0
/* -----------------------------------------------------------------------
 *
 *   Copyright 2011 Intel Corporation; author Matt Fleming
 *
 * ----------------------------------------------------------------------- */

#include <linux/efi.h>
#include <linux/screen_info.h>
#include <asm/efi.h>
#include <asm/setup.h>

#include "efistub.h"

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
		 efi_pixel_bitmask_t pixel_info, int pixel_format)
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

static efi_graphics_output_protocol_t *
find_gop(efi_guid_t *proto, unsigned long size, void **handles)
{
	efi_graphics_output_protocol_t *first_gop;
	efi_handle_t h;
	int i;

	first_gop = NULL;

	for_each_efi_handle(h, handles, size, i) {
		efi_status_t status;

		efi_graphics_output_protocol_t *gop;
		efi_graphics_output_protocol_mode_t *mode;
		efi_graphics_output_mode_info_t *info;

		efi_guid_t conout_proto = EFI_CONSOLE_OUT_DEVICE_GUID;
		void *dummy = NULL;

		status = efi_bs_call(handle_protocol, h, proto, (void **)&gop);
		if (status != EFI_SUCCESS)
			continue;

		mode = efi_table_attr(gop, mode);
		info = efi_table_attr(mode, info);
		if (info->pixel_format == PIXEL_BLT_ONLY)
			continue;

		/*
		 * Systems that use the UEFI Console Splitter may
		 * provide multiple GOP devices, not all of which are
		 * backed by real hardware. The workaround is to search
		 * for a GOP implementing the ConOut protocol, and if
		 * one isn't found, to just fall back to the first GOP.
		 *
		 * Once we've found a GOP supporting ConOut,
		 * don't bother looking any further.
		 */
		status = efi_bs_call(handle_protocol, h, &conout_proto, &dummy);
		if (status == EFI_SUCCESS)
			return gop;

		if (!first_gop)
			first_gop = gop;
	}

	return first_gop;
}

static efi_status_t setup_gop(struct screen_info *si, efi_guid_t *proto,
			      unsigned long size, void **handles)
{
	efi_graphics_output_protocol_t *gop;
	efi_graphics_output_protocol_mode_t *mode;
	efi_graphics_output_mode_info_t *info;
	efi_physical_addr_t fb_base;

	gop = find_gop(proto, size, handles);

	/* Did we find any GOPs? */
	if (!gop)
		return EFI_NOT_FOUND;

	/* EFI framebuffer */
	mode = efi_table_attr(gop, mode);
	info = efi_table_attr(mode, info);

	si->orig_video_isVGA = VIDEO_TYPE_EFI;

	si->lfb_width  = info->horizontal_resolution;
	si->lfb_height = info->vertical_resolution;

	fb_base		 = efi_table_attr(mode, frame_buffer_base);
	si->lfb_base	 = lower_32_bits(fb_base);
	si->ext_lfb_base = upper_32_bits(fb_base);
	if (si->ext_lfb_base)
		si->capabilities |= VIDEO_CAPABILITY_64BIT_BASE;

	si->pages = 1;

	setup_pixel_info(si, info->pixels_per_scan_line,
			     info->pixel_information, info->pixel_format);

	si->lfb_size = si->lfb_linelength * si->lfb_height;

	si->capabilities |= VIDEO_CAPABILITY_SKIP_QUIRKS;

	return EFI_SUCCESS;
}

/*
 * See if we have Graphics Output Protocol
 */
efi_status_t efi_setup_gop(struct screen_info *si, efi_guid_t *proto,
			   unsigned long size)
{
	efi_status_t status;
	void **gop_handle = NULL;

	status = efi_bs_call(allocate_pool, EFI_LOADER_DATA, size,
			     (void **)&gop_handle);
	if (status != EFI_SUCCESS)
		return status;

	status = efi_bs_call(locate_handle, EFI_LOCATE_BY_PROTOCOL, proto, NULL,
			     &size, gop_handle);
	if (status != EFI_SUCCESS)
		goto free_handle;

	status = setup_gop(si, proto, size, gop_handle);

free_handle:
	efi_bs_call(free_pool, gop_handle);
	return status;
}
