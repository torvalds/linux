// SPDX-License-Identifier: GPL-2.0
/* -----------------------------------------------------------------------
 *
 *   Copyright 2011 Intel Corporation; author Matt Fleming
 *
 * ----------------------------------------------------------------------- */

#include <linux/bitops.h>
#include <linux/ctype.h>
#include <linux/efi.h>
#include <linux/screen_info.h>
#include <linux/string.h>
#include <asm/efi.h>
#include <asm/setup.h>

#include "efistub.h"

enum efi_cmdline_option {
	EFI_CMDLINE_NONE,
	EFI_CMDLINE_MODE_NUM,
	EFI_CMDLINE_RES,
	EFI_CMDLINE_AUTO,
	EFI_CMDLINE_LIST
};

static struct {
	enum efi_cmdline_option option;
	union {
		u32 mode;
		struct {
			u32 width, height;
			int format;
			u8 depth;
		} res;
	};
} cmdline = { .option = EFI_CMDLINE_NONE };

static bool parse_modenum(char *option, char **next)
{
	u32 m;

	if (!strstarts(option, "mode="))
		return false;
	option += strlen("mode=");
	m = simple_strtoull(option, &option, 0);
	if (*option && *option++ != ',')
		return false;
	cmdline.option = EFI_CMDLINE_MODE_NUM;
	cmdline.mode   = m;

	*next = option;
	return true;
}

static bool parse_res(char *option, char **next)
{
	u32 w, h, d = 0;
	int pf = -1;

	if (!isdigit(*option))
		return false;
	w = simple_strtoull(option, &option, 10);
	if (*option++ != 'x' || !isdigit(*option))
		return false;
	h = simple_strtoull(option, &option, 10);
	if (*option == '-') {
		option++;
		if (strstarts(option, "rgb")) {
			option += strlen("rgb");
			pf = PIXEL_RGB_RESERVED_8BIT_PER_COLOR;
		} else if (strstarts(option, "bgr")) {
			option += strlen("bgr");
			pf = PIXEL_BGR_RESERVED_8BIT_PER_COLOR;
		} else if (isdigit(*option))
			d = simple_strtoull(option, &option, 10);
		else
			return false;
	}
	if (*option && *option++ != ',')
		return false;
	cmdline.option     = EFI_CMDLINE_RES;
	cmdline.res.width  = w;
	cmdline.res.height = h;
	cmdline.res.format = pf;
	cmdline.res.depth  = d;

	*next = option;
	return true;
}

static bool parse_auto(char *option, char **next)
{
	if (!strstarts(option, "auto"))
		return false;
	option += strlen("auto");
	if (*option && *option++ != ',')
		return false;
	cmdline.option = EFI_CMDLINE_AUTO;

	*next = option;
	return true;
}

static bool parse_list(char *option, char **next)
{
	if (!strstarts(option, "list"))
		return false;
	option += strlen("list");
	if (*option && *option++ != ',')
		return false;
	cmdline.option = EFI_CMDLINE_LIST;

	*next = option;
	return true;
}

void efi_parse_option_graphics(char *option)
{
	while (*option) {
		if (parse_modenum(option, &option))
			continue;
		if (parse_res(option, &option))
			continue;
		if (parse_auto(option, &option))
			continue;
		if (parse_list(option, &option))
			continue;

		while (*option && *option++ != ',')
			;
	}
}

static u32 choose_mode_modenum(efi_graphics_output_protocol_t *gop)
{
	efi_status_t status;

	efi_graphics_output_protocol_mode_t *mode;
	efi_graphics_output_mode_info_t *info;
	unsigned long info_size;

	u32 max_mode, cur_mode;
	int pf;

	mode = efi_table_attr(gop, mode);

	cur_mode = efi_table_attr(mode, mode);
	if (cmdline.mode == cur_mode)
		return cur_mode;

	max_mode = efi_table_attr(mode, max_mode);
	if (cmdline.mode >= max_mode) {
		efi_err("Requested mode is invalid\n");
		return cur_mode;
	}

	status = efi_call_proto(gop, query_mode, cmdline.mode,
				&info_size, &info);
	if (status != EFI_SUCCESS) {
		efi_err("Couldn't get mode information\n");
		return cur_mode;
	}

	pf = info->pixel_format;

	efi_bs_call(free_pool, info);

	if (pf == PIXEL_BLT_ONLY || pf >= PIXEL_FORMAT_MAX) {
		efi_err("Invalid PixelFormat\n");
		return cur_mode;
	}

	return cmdline.mode;
}

static u8 pixel_bpp(int pixel_format, efi_pixel_bitmask_t pixel_info)
{
	if (pixel_format == PIXEL_BIT_MASK) {
		u32 mask = pixel_info.red_mask | pixel_info.green_mask |
			   pixel_info.blue_mask | pixel_info.reserved_mask;
		if (!mask)
			return 0;
		return __fls(mask) - __ffs(mask) + 1;
	} else
		return 32;
}

static u32 choose_mode_res(efi_graphics_output_protocol_t *gop)
{
	efi_status_t status;

	efi_graphics_output_protocol_mode_t *mode;
	efi_graphics_output_mode_info_t *info;
	unsigned long info_size;

	u32 max_mode, cur_mode;
	int pf;
	efi_pixel_bitmask_t pi;
	u32 m, w, h;

	mode = efi_table_attr(gop, mode);

	cur_mode = efi_table_attr(mode, mode);
	info = efi_table_attr(mode, info);
	pf = info->pixel_format;
	pi = info->pixel_information;
	w  = info->horizontal_resolution;
	h  = info->vertical_resolution;

	if (w == cmdline.res.width && h == cmdline.res.height &&
	    (cmdline.res.format < 0 || cmdline.res.format == pf) &&
	    (!cmdline.res.depth || cmdline.res.depth == pixel_bpp(pf, pi)))
		return cur_mode;

	max_mode = efi_table_attr(mode, max_mode);

	for (m = 0; m < max_mode; m++) {
		if (m == cur_mode)
			continue;

		status = efi_call_proto(gop, query_mode, m,
					&info_size, &info);
		if (status != EFI_SUCCESS)
			continue;

		pf = info->pixel_format;
		pi = info->pixel_information;
		w  = info->horizontal_resolution;
		h  = info->vertical_resolution;

		efi_bs_call(free_pool, info);

		if (pf == PIXEL_BLT_ONLY || pf >= PIXEL_FORMAT_MAX)
			continue;
		if (w == cmdline.res.width && h == cmdline.res.height &&
		    (cmdline.res.format < 0 || cmdline.res.format == pf) &&
		    (!cmdline.res.depth || cmdline.res.depth == pixel_bpp(pf, pi)))
			return m;
	}

	efi_err("Couldn't find requested mode\n");

	return cur_mode;
}

static u32 choose_mode_auto(efi_graphics_output_protocol_t *gop)
{
	efi_status_t status;

	efi_graphics_output_protocol_mode_t *mode;
	efi_graphics_output_mode_info_t *info;
	unsigned long info_size;

	u32 max_mode, cur_mode, best_mode, area;
	u8 depth;
	int pf;
	efi_pixel_bitmask_t pi;
	u32 m, w, h, a;
	u8 d;

	mode = efi_table_attr(gop, mode);

	cur_mode = efi_table_attr(mode, mode);
	max_mode = efi_table_attr(mode, max_mode);

	info = efi_table_attr(mode, info);

	pf = info->pixel_format;
	pi = info->pixel_information;
	w  = info->horizontal_resolution;
	h  = info->vertical_resolution;

	best_mode = cur_mode;
	area = w * h;
	depth = pixel_bpp(pf, pi);

	for (m = 0; m < max_mode; m++) {
		if (m == cur_mode)
			continue;

		status = efi_call_proto(gop, query_mode, m,
					&info_size, &info);
		if (status != EFI_SUCCESS)
			continue;

		pf = info->pixel_format;
		pi = info->pixel_information;
		w  = info->horizontal_resolution;
		h  = info->vertical_resolution;

		efi_bs_call(free_pool, info);

		if (pf == PIXEL_BLT_ONLY || pf >= PIXEL_FORMAT_MAX)
			continue;
		a = w * h;
		if (a < area)
			continue;
		d = pixel_bpp(pf, pi);
		if (a > area || d > depth) {
			best_mode = m;
			area = a;
			depth = d;
		}
	}

	return best_mode;
}

static u32 choose_mode_list(efi_graphics_output_protocol_t *gop)
{
	efi_status_t status;

	efi_graphics_output_protocol_mode_t *mode;
	efi_graphics_output_mode_info_t *info;
	unsigned long info_size;

	u32 max_mode, cur_mode;
	int pf;
	efi_pixel_bitmask_t pi;
	u32 m, w, h;
	u8 d;
	const char *dstr;
	bool valid;
	efi_input_key_t key;

	mode = efi_table_attr(gop, mode);

	cur_mode = efi_table_attr(mode, mode);
	max_mode = efi_table_attr(mode, max_mode);

	efi_printk("Available graphics modes are 0-%u\n", max_mode-1);
	efi_puts("  * = current mode\n"
		 "  - = unusable mode\n");
	for (m = 0; m < max_mode; m++) {
		status = efi_call_proto(gop, query_mode, m,
					&info_size, &info);
		if (status != EFI_SUCCESS)
			continue;

		pf = info->pixel_format;
		pi = info->pixel_information;
		w  = info->horizontal_resolution;
		h  = info->vertical_resolution;

		efi_bs_call(free_pool, info);

		valid = !(pf == PIXEL_BLT_ONLY || pf >= PIXEL_FORMAT_MAX);
		d = 0;
		switch (pf) {
		case PIXEL_RGB_RESERVED_8BIT_PER_COLOR:
			dstr = "rgb";
			break;
		case PIXEL_BGR_RESERVED_8BIT_PER_COLOR:
			dstr = "bgr";
			break;
		case PIXEL_BIT_MASK:
			dstr = "";
			d = pixel_bpp(pf, pi);
			break;
		case PIXEL_BLT_ONLY:
			dstr = "blt";
			break;
		default:
			dstr = "xxx";
			break;
		}

		efi_printk("Mode %3u %c%c: Resolution %ux%u-%s%.0hhu\n",
			   m,
			   m == cur_mode ? '*' : ' ',
			   !valid ? '-' : ' ',
			   w, h, dstr, d);
	}

	efi_puts("\nPress any key to continue (or wait 10 seconds)\n");
	status = efi_wait_for_key(10 * EFI_USEC_PER_SEC, &key);
	if (status != EFI_SUCCESS && status != EFI_TIMEOUT) {
		efi_err("Unable to read key, continuing in 10 seconds\n");
		efi_bs_call(stall, 10 * EFI_USEC_PER_SEC);
	}

	return cur_mode;
}

static void set_mode(efi_graphics_output_protocol_t *gop)
{
	efi_graphics_output_protocol_mode_t *mode;
	u32 cur_mode, new_mode;

	switch (cmdline.option) {
	case EFI_CMDLINE_MODE_NUM:
		new_mode = choose_mode_modenum(gop);
		break;
	case EFI_CMDLINE_RES:
		new_mode = choose_mode_res(gop);
		break;
	case EFI_CMDLINE_AUTO:
		new_mode = choose_mode_auto(gop);
		break;
	case EFI_CMDLINE_LIST:
		new_mode = choose_mode_list(gop);
		break;
	default:
		return;
	}

	mode = efi_table_attr(gop, mode);
	cur_mode = efi_table_attr(mode, mode);

	if (new_mode == cur_mode)
		return;

	if (efi_call_proto(gop, set_mode, new_mode) != EFI_SUCCESS)
		efi_err("Failed to set requested mode\n");
}

static void find_bits(u32 mask, u8 *pos, u8 *size)
{
	if (!mask) {
		*pos = *size = 0;
		return;
	}

	/* UEFI spec guarantees that the set bits are contiguous */
	*pos  = __ffs(mask);
	*size = __fls(mask) - *pos + 1;
}

static void
setup_pixel_info(struct screen_info *si, u32 pixels_per_scan_line,
		 efi_pixel_bitmask_t pixel_info, int pixel_format)
{
	if (pixel_format == PIXEL_BIT_MASK) {
		find_bits(pixel_info.red_mask,
			  &si->red_pos, &si->red_size);
		find_bits(pixel_info.green_mask,
			  &si->green_pos, &si->green_size);
		find_bits(pixel_info.blue_mask,
			  &si->blue_pos, &si->blue_size);
		find_bits(pixel_info.reserved_mask,
			  &si->rsvd_pos, &si->rsvd_size);
		si->lfb_depth = si->red_size + si->green_size +
			si->blue_size + si->rsvd_size;
		si->lfb_linelength = (pixels_per_scan_line * si->lfb_depth) / 8;
	} else {
		if (pixel_format == PIXEL_RGB_RESERVED_8BIT_PER_COLOR) {
			si->red_pos   = 0;
			si->blue_pos  = 16;
		} else /* PIXEL_BGR_RESERVED_8BIT_PER_COLOR */ {
			si->blue_pos  = 0;
			si->red_pos   = 16;
		}

		si->green_pos = 8;
		si->rsvd_pos  = 24;
		si->red_size = si->green_size =
			si->blue_size = si->rsvd_size = 8;

		si->lfb_depth = 32;
		si->lfb_linelength = pixels_per_scan_line * 4;
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
		if (info->pixel_format == PIXEL_BLT_ONLY ||
		    info->pixel_format >= PIXEL_FORMAT_MAX)
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

	gop = find_gop(proto, size, handles);

	/* Did we find any GOPs? */
	if (!gop)
		return EFI_NOT_FOUND;

	/* Change mode if requested */
	set_mode(gop);

	/* EFI framebuffer */
	mode = efi_table_attr(gop, mode);
	info = efi_table_attr(mode, info);

	si->orig_video_isVGA = VIDEO_TYPE_EFI;

	si->lfb_width  = info->horizontal_resolution;
	si->lfb_height = info->vertical_resolution;

	efi_set_u64_split(efi_table_attr(mode, frame_buffer_base),
			  &si->lfb_base, &si->ext_lfb_base);
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
