/* -----------------------------------------------------------------------
 *
 *   Copyright 2011 Intel Corporation; author Matt Fleming
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

#include <linux/efi.h>
#include <linux/pci.h>
#include <asm/efi.h>
#include <asm/setup.h>
#include <asm/desc.h>

#undef memcpy			/* Use memcpy from misc.c */

#include "eboot.h"

static efi_system_table_t *sys_table;

struct efi_config *efi_early;

#define BOOT_SERVICES(bits)						\
static void setup_boot_services##bits(struct efi_config *c)		\
{									\
	efi_system_table_##bits##_t *table;				\
	efi_boot_services_##bits##_t *bt;				\
									\
	table = (typeof(table))sys_table;				\
									\
	c->text_output = table->con_out;				\
									\
	bt = (typeof(bt))(unsigned long)(table->boottime);		\
									\
	c->allocate_pool = bt->allocate_pool;				\
	c->allocate_pages = bt->allocate_pages;				\
	c->get_memory_map = bt->get_memory_map;				\
	c->free_pool = bt->free_pool;					\
	c->free_pages = bt->free_pages;					\
	c->locate_handle = bt->locate_handle;				\
	c->handle_protocol = bt->handle_protocol;			\
	c->exit_boot_services = bt->exit_boot_services;			\
}
BOOT_SERVICES(32);
BOOT_SERVICES(64);

void efi_char16_printk(efi_system_table_t *, efi_char16_t *);

static efi_status_t
__file_size32(void *__fh, efi_char16_t *filename_16,
	      void **handle, u64 *file_sz)
{
	efi_file_handle_32_t *h, *fh = __fh;
	efi_file_info_t *info;
	efi_status_t status;
	efi_guid_t info_guid = EFI_FILE_INFO_ID;
	u32 info_sz;

	status = efi_early->call((unsigned long)fh->open, fh, &h, filename_16,
				 EFI_FILE_MODE_READ, (u64)0);
	if (status != EFI_SUCCESS) {
		efi_printk(sys_table, "Failed to open file: ");
		efi_char16_printk(sys_table, filename_16);
		efi_printk(sys_table, "\n");
		return status;
	}

	*handle = h;

	info_sz = 0;
	status = efi_early->call((unsigned long)h->get_info, h, &info_guid,
				 &info_sz, NULL);
	if (status != EFI_BUFFER_TOO_SMALL) {
		efi_printk(sys_table, "Failed to get file info size\n");
		return status;
	}

grow:
	status = efi_call_early(allocate_pool, EFI_LOADER_DATA,
				info_sz, (void **)&info);
	if (status != EFI_SUCCESS) {
		efi_printk(sys_table, "Failed to alloc mem for file info\n");
		return status;
	}

	status = efi_early->call((unsigned long)h->get_info, h, &info_guid,
				 &info_sz, info);
	if (status == EFI_BUFFER_TOO_SMALL) {
		efi_call_early(free_pool, info);
		goto grow;
	}

	*file_sz = info->file_size;
	efi_call_early(free_pool, info);

	if (status != EFI_SUCCESS)
		efi_printk(sys_table, "Failed to get initrd info\n");

	return status;
}

static efi_status_t
__file_size64(void *__fh, efi_char16_t *filename_16,
	      void **handle, u64 *file_sz)
{
	efi_file_handle_64_t *h, *fh = __fh;
	efi_file_info_t *info;
	efi_status_t status;
	efi_guid_t info_guid = EFI_FILE_INFO_ID;
	u64 info_sz;

	status = efi_early->call((unsigned long)fh->open, fh, &h, filename_16,
				 EFI_FILE_MODE_READ, (u64)0);
	if (status != EFI_SUCCESS) {
		efi_printk(sys_table, "Failed to open file: ");
		efi_char16_printk(sys_table, filename_16);
		efi_printk(sys_table, "\n");
		return status;
	}

	*handle = h;

	info_sz = 0;
	status = efi_early->call((unsigned long)h->get_info, h, &info_guid,
				 &info_sz, NULL);
	if (status != EFI_BUFFER_TOO_SMALL) {
		efi_printk(sys_table, "Failed to get file info size\n");
		return status;
	}

grow:
	status = efi_call_early(allocate_pool, EFI_LOADER_DATA,
				info_sz, (void **)&info);
	if (status != EFI_SUCCESS) {
		efi_printk(sys_table, "Failed to alloc mem for file info\n");
		return status;
	}

	status = efi_early->call((unsigned long)h->get_info, h, &info_guid,
				 &info_sz, info);
	if (status == EFI_BUFFER_TOO_SMALL) {
		efi_call_early(free_pool, info);
		goto grow;
	}

	*file_sz = info->file_size;
	efi_call_early(free_pool, info);

	if (status != EFI_SUCCESS)
		efi_printk(sys_table, "Failed to get initrd info\n");

	return status;
}
efi_status_t
efi_file_size(efi_system_table_t *sys_table, void *__fh,
	      efi_char16_t *filename_16, void **handle, u64 *file_sz)
{
	if (efi_early->is64)
		return __file_size64(__fh, filename_16, handle, file_sz);

	return __file_size32(__fh, filename_16, handle, file_sz);
}

efi_status_t
efi_file_read(void *handle, unsigned long *size, void *addr)
{
	unsigned long func;

	if (efi_early->is64) {
		efi_file_handle_64_t *fh = handle;

		func = (unsigned long)fh->read;
		return efi_early->call(func, handle, size, addr);
	} else {
		efi_file_handle_32_t *fh = handle;

		func = (unsigned long)fh->read;
		return efi_early->call(func, handle, size, addr);
	}
}

efi_status_t efi_file_close(void *handle)
{
	if (efi_early->is64) {
		efi_file_handle_64_t *fh = handle;

		return efi_early->call((unsigned long)fh->close, handle);
	} else {
		efi_file_handle_32_t *fh = handle;

		return efi_early->call((unsigned long)fh->close, handle);
	}
}

static inline efi_status_t __open_volume32(void *__image, void **__fh)
{
	efi_file_io_interface_t *io;
	efi_loaded_image_32_t *image = __image;
	efi_file_handle_32_t *fh;
	efi_guid_t fs_proto = EFI_FILE_SYSTEM_GUID;
	efi_status_t status;
	void *handle = (void *)(unsigned long)image->device_handle;
	unsigned long func;

	status = efi_call_early(handle_protocol, handle,
				&fs_proto, (void **)&io);
	if (status != EFI_SUCCESS) {
		efi_printk(sys_table, "Failed to handle fs_proto\n");
		return status;
	}

	func = (unsigned long)io->open_volume;
	status = efi_early->call(func, io, &fh);
	if (status != EFI_SUCCESS)
		efi_printk(sys_table, "Failed to open volume\n");

	*__fh = fh;
	return status;
}

static inline efi_status_t __open_volume64(void *__image, void **__fh)
{
	efi_file_io_interface_t *io;
	efi_loaded_image_64_t *image = __image;
	efi_file_handle_64_t *fh;
	efi_guid_t fs_proto = EFI_FILE_SYSTEM_GUID;
	efi_status_t status;
	void *handle = (void *)(unsigned long)image->device_handle;
	unsigned long func;

	status = efi_call_early(handle_protocol, handle,
				&fs_proto, (void **)&io);
	if (status != EFI_SUCCESS) {
		efi_printk(sys_table, "Failed to handle fs_proto\n");
		return status;
	}

	func = (unsigned long)io->open_volume;
	status = efi_early->call(func, io, &fh);
	if (status != EFI_SUCCESS)
		efi_printk(sys_table, "Failed to open volume\n");

	*__fh = fh;
	return status;
}

efi_status_t
efi_open_volume(efi_system_table_t *sys_table, void *__image, void **__fh)
{
	if (efi_early->is64)
		return __open_volume64(__image, __fh);

	return __open_volume32(__image, __fh);
}

void efi_char16_printk(efi_system_table_t *table, efi_char16_t *str)
{
	unsigned long output_string;
	size_t offset;

	if (efi_early->is64) {
		struct efi_simple_text_output_protocol_64 *out;
		u64 *func;

		offset = offsetof(typeof(*out), output_string);
		output_string = efi_early->text_output + offset;
		func = (u64 *)output_string;

		efi_early->call(*func, efi_early->text_output, str);
	} else {
		struct efi_simple_text_output_protocol_32 *out;
		u32 *func;

		offset = offsetof(typeof(*out), output_string);
		output_string = efi_early->text_output + offset;
		func = (u32 *)output_string;

		efi_early->call(*func, efi_early->text_output, str);
	}
}

#include "../../../../drivers/firmware/efi/efi-stub-helper.c"

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

static efi_status_t
__setup_efi_pci32(efi_pci_io_protocol_32 *pci, struct pci_setup_rom **__rom)
{
	struct pci_setup_rom *rom = NULL;
	efi_status_t status;
	unsigned long size;
	uint64_t attributes;

	status = efi_early->call(pci->attributes, pci,
				 EfiPciIoAttributeOperationGet, 0, 0,
				 &attributes);
	if (status != EFI_SUCCESS)
		return status;

	if (!pci->romimage || !pci->romsize)
		return EFI_INVALID_PARAMETER;

	size = pci->romsize + sizeof(*rom);

	status = efi_call_early(allocate_pool, EFI_LOADER_DATA, size, &rom);
	if (status != EFI_SUCCESS)
		return status;

	memset(rom, 0, sizeof(*rom));

	rom->data.type = SETUP_PCI;
	rom->data.len = size - sizeof(struct setup_data);
	rom->data.next = 0;
	rom->pcilen = pci->romsize;
	*__rom = rom;

	status = efi_early->call(pci->pci.read, pci, EfiPciIoWidthUint16,
				 PCI_VENDOR_ID, 1, &(rom->vendor));

	if (status != EFI_SUCCESS)
		goto free_struct;

	status = efi_early->call(pci->pci.read, pci, EfiPciIoWidthUint16,
				 PCI_DEVICE_ID, 1, &(rom->devid));

	if (status != EFI_SUCCESS)
		goto free_struct;

	status = efi_early->call(pci->get_location, pci, &(rom->segment),
				 &(rom->bus), &(rom->device), &(rom->function));

	if (status != EFI_SUCCESS)
		goto free_struct;

	memcpy(rom->romdata, pci->romimage, pci->romsize);
	return status;

free_struct:
	efi_call_early(free_pool, rom);
	return status;
}

static efi_status_t
setup_efi_pci32(struct boot_params *params, void **pci_handle,
		unsigned long size)
{
	efi_pci_io_protocol_32 *pci = NULL;
	efi_guid_t pci_proto = EFI_PCI_IO_PROTOCOL_GUID;
	u32 *handles = (u32 *)(unsigned long)pci_handle;
	efi_status_t status;
	unsigned long nr_pci;
	struct setup_data *data;
	int i;

	data = (struct setup_data *)(unsigned long)params->hdr.setup_data;

	while (data && data->next)
		data = (struct setup_data *)(unsigned long)data->next;

	nr_pci = size / sizeof(u32);
	for (i = 0; i < nr_pci; i++) {
		struct pci_setup_rom *rom = NULL;
		u32 h = handles[i];

		status = efi_call_early(handle_protocol, h,
					&pci_proto, (void **)&pci);

		if (status != EFI_SUCCESS)
			continue;

		if (!pci)
			continue;

		status = __setup_efi_pci32(pci, &rom);
		if (status != EFI_SUCCESS)
			continue;

		if (data)
			data->next = (unsigned long)rom;
		else
			params->hdr.setup_data = (unsigned long)rom;

		data = (struct setup_data *)rom;

	}

	return status;
}

static efi_status_t
__setup_efi_pci64(efi_pci_io_protocol_64 *pci, struct pci_setup_rom **__rom)
{
	struct pci_setup_rom *rom;
	efi_status_t status;
	unsigned long size;
	uint64_t attributes;

	status = efi_early->call(pci->attributes, pci,
				 EfiPciIoAttributeOperationGet, 0,
				 &attributes);
	if (status != EFI_SUCCESS)
		return status;

	if (!pci->romimage || !pci->romsize)
		return EFI_INVALID_PARAMETER;

	size = pci->romsize + sizeof(*rom);

	status = efi_call_early(allocate_pool, EFI_LOADER_DATA, size, &rom);
	if (status != EFI_SUCCESS)
		return status;

	rom->data.type = SETUP_PCI;
	rom->data.len = size - sizeof(struct setup_data);
	rom->data.next = 0;
	rom->pcilen = pci->romsize;
	*__rom = rom;

	status = efi_early->call(pci->pci.read, pci, EfiPciIoWidthUint16,
				 PCI_VENDOR_ID, 1, &(rom->vendor));

	if (status != EFI_SUCCESS)
		goto free_struct;

	status = efi_early->call(pci->pci.read, pci, EfiPciIoWidthUint16,
				 PCI_DEVICE_ID, 1, &(rom->devid));

	if (status != EFI_SUCCESS)
		goto free_struct;

	status = efi_early->call(pci->get_location, pci, &(rom->segment),
				 &(rom->bus), &(rom->device), &(rom->function));

	if (status != EFI_SUCCESS)
		goto free_struct;

	memcpy(rom->romdata, pci->romimage, pci->romsize);
	return status;

free_struct:
	efi_call_early(free_pool, rom);
	return status;

}

static efi_status_t
setup_efi_pci64(struct boot_params *params, void **pci_handle,
		unsigned long size)
{
	efi_pci_io_protocol_64 *pci = NULL;
	efi_guid_t pci_proto = EFI_PCI_IO_PROTOCOL_GUID;
	u64 *handles = (u64 *)(unsigned long)pci_handle;
	efi_status_t status;
	unsigned long nr_pci;
	struct setup_data *data;
	int i;

	data = (struct setup_data *)(unsigned long)params->hdr.setup_data;

	while (data && data->next)
		data = (struct setup_data *)(unsigned long)data->next;

	nr_pci = size / sizeof(u64);
	for (i = 0; i < nr_pci; i++) {
		struct pci_setup_rom *rom = NULL;
		u64 h = handles[i];

		status = efi_call_early(handle_protocol, h,
					&pci_proto, (void **)&pci);

		if (status != EFI_SUCCESS)
			continue;

		if (!pci)
			continue;

		status = __setup_efi_pci64(pci, &rom);
		if (status != EFI_SUCCESS)
			continue;

		if (data)
			data->next = (unsigned long)rom;
		else
			params->hdr.setup_data = (unsigned long)rom;

		data = (struct setup_data *)rom;

	}

	return status;
}

static efi_status_t setup_efi_pci(struct boot_params *params)
{
	efi_status_t status;
	void **pci_handle = NULL;
	efi_guid_t pci_proto = EFI_PCI_IO_PROTOCOL_GUID;
	unsigned long size = 0;

	status = efi_call_early(locate_handle,
				EFI_LOCATE_BY_PROTOCOL,
				&pci_proto, NULL, &size, pci_handle);

	if (status == EFI_BUFFER_TOO_SMALL) {
		status = efi_call_early(allocate_pool,
					EFI_LOADER_DATA,
					size, (void **)&pci_handle);

		if (status != EFI_SUCCESS)
			return status;

		status = efi_call_early(locate_handle,
					EFI_LOCATE_BY_PROTOCOL, &pci_proto,
					NULL, &size, pci_handle);
	}

	if (status != EFI_SUCCESS)
		goto free_handle;

	if (efi_early->is64)
		status = setup_efi_pci64(params, pci_handle, size);
	else
		status = setup_efi_pci32(params, pci_handle, size);

free_handle:
	efi_call_early(free_pool, pci_handle);
	return status;
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
__gop_query32(struct efi_graphics_output_protocol_32 *gop32,
	      struct efi_graphics_output_mode_info **info,
	      unsigned long *size, u32 *fb_base)
{
	struct efi_graphics_output_protocol_mode_32 *mode;
	efi_status_t status;
	unsigned long m;

	m = gop32->mode;
	mode = (struct efi_graphics_output_protocol_mode_32 *)m;

	status = efi_early->call(gop32->query_mode, gop32,
				 mode->mode, size, info);
	if (status != EFI_SUCCESS)
		return status;

	*fb_base = mode->frame_buffer_base;
	return status;
}

static efi_status_t
setup_gop32(struct screen_info *si, efi_guid_t *proto,
	    unsigned long size, void **gop_handle)
{
	struct efi_graphics_output_protocol_32 *gop32, *first_gop;
	unsigned long nr_gops;
	u16 width, height;
	u32 pixels_per_scan_line;
	u32 fb_base;
	struct efi_pixel_bitmask pixel_info;
	int pixel_format;
	efi_status_t status;
	u32 *handles = (u32 *)(unsigned long)gop_handle;
	int i;

	first_gop = NULL;
	gop32 = NULL;

	nr_gops = size / sizeof(u32);
	for (i = 0; i < nr_gops; i++) {
		struct efi_graphics_output_mode_info *info = NULL;
		efi_guid_t conout_proto = EFI_CONSOLE_OUT_DEVICE_GUID;
		bool conout_found = false;
		void *dummy = NULL;
		u32 h = handles[i];

		status = efi_call_early(handle_protocol, h,
					proto, (void **)&gop32);
		if (status != EFI_SUCCESS)
			continue;

		status = efi_call_early(handle_protocol, h,
					&conout_proto, &dummy);
		if (status == EFI_SUCCESS)
			conout_found = true;

		status = __gop_query32(gop32, &info, &size, &fb_base);
		if (status == EFI_SUCCESS && (!first_gop || conout_found)) {
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
		goto out;

	/* EFI framebuffer */
	si->orig_video_isVGA = VIDEO_TYPE_EFI;

	si->lfb_width = width;
	si->lfb_height = height;
	si->lfb_base = fb_base;
	si->pages = 1;

	setup_pixel_info(si, pixels_per_scan_line, pixel_info, pixel_format);

	si->lfb_size = si->lfb_linelength * si->lfb_height;

	si->capabilities |= VIDEO_CAPABILITY_SKIP_QUIRKS;
out:
	return status;
}

static efi_status_t
__gop_query64(struct efi_graphics_output_protocol_64 *gop64,
	      struct efi_graphics_output_mode_info **info,
	      unsigned long *size, u32 *fb_base)
{
	struct efi_graphics_output_protocol_mode_64 *mode;
	efi_status_t status;
	unsigned long m;

	m = gop64->mode;
	mode = (struct efi_graphics_output_protocol_mode_64 *)m;

	status = efi_early->call(gop64->query_mode, gop64,
				 mode->mode, size, info);
	if (status != EFI_SUCCESS)
		return status;

	*fb_base = mode->frame_buffer_base;
	return status;
}

static efi_status_t
setup_gop64(struct screen_info *si, efi_guid_t *proto,
	    unsigned long size, void **gop_handle)
{
	struct efi_graphics_output_protocol_64 *gop64, *first_gop;
	unsigned long nr_gops;
	u16 width, height;
	u32 pixels_per_scan_line;
	u32 fb_base;
	struct efi_pixel_bitmask pixel_info;
	int pixel_format;
	efi_status_t status;
	u64 *handles = (u64 *)(unsigned long)gop_handle;
	int i;

	first_gop = NULL;
	gop64 = NULL;

	nr_gops = size / sizeof(u64);
	for (i = 0; i < nr_gops; i++) {
		struct efi_graphics_output_mode_info *info = NULL;
		efi_guid_t conout_proto = EFI_CONSOLE_OUT_DEVICE_GUID;
		bool conout_found = false;
		void *dummy = NULL;
		u64 h = handles[i];

		status = efi_call_early(handle_protocol, h,
					proto, (void **)&gop64);
		if (status != EFI_SUCCESS)
			continue;

		status = efi_call_early(handle_protocol, h,
					&conout_proto, &dummy);
		if (status == EFI_SUCCESS)
			conout_found = true;

		status = __gop_query64(gop64, &info, &size, &fb_base);
		if (status == EFI_SUCCESS && (!first_gop || conout_found)) {
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
		goto out;

	/* EFI framebuffer */
	si->orig_video_isVGA = VIDEO_TYPE_EFI;

	si->lfb_width = width;
	si->lfb_height = height;
	si->lfb_base = fb_base;
	si->pages = 1;

	setup_pixel_info(si, pixels_per_scan_line, pixel_info, pixel_format);

	si->lfb_size = si->lfb_linelength * si->lfb_height;

	si->capabilities |= VIDEO_CAPABILITY_SKIP_QUIRKS;
out:
	return status;
}

/*
 * See if we have Graphics Output Protocol
 */
static efi_status_t setup_gop(struct screen_info *si, efi_guid_t *proto,
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

	if (efi_early->is64)
		status = setup_gop64(si, proto, size, gop_handle);
	else
		status = setup_gop32(si, proto, size, gop_handle);

free_handle:
	efi_call_early(free_pool, gop_handle);
	return status;
}

static efi_status_t
setup_uga32(void **uga_handle, unsigned long size, u32 *width, u32 *height)
{
	struct efi_uga_draw_protocol *uga = NULL, *first_uga;
	efi_guid_t uga_proto = EFI_UGA_PROTOCOL_GUID;
	unsigned long nr_ugas;
	u32 *handles = (u32 *)uga_handle;;
	efi_status_t status;
	int i;

	first_uga = NULL;
	nr_ugas = size / sizeof(u32);
	for (i = 0; i < nr_ugas; i++) {
		efi_guid_t pciio_proto = EFI_PCI_IO_PROTOCOL_GUID;
		u32 w, h, depth, refresh;
		void *pciio;
		u32 handle = handles[i];

		status = efi_call_early(handle_protocol, handle,
					&uga_proto, (void **)&uga);
		if (status != EFI_SUCCESS)
			continue;

		efi_call_early(handle_protocol, handle, &pciio_proto, &pciio);

		status = efi_early->call((unsigned long)uga->get_mode, uga,
					 &w, &h, &depth, &refresh);
		if (status == EFI_SUCCESS && (!first_uga || pciio)) {
			*width = w;
			*height = h;

			/*
			 * Once we've found a UGA supporting PCIIO,
			 * don't bother looking any further.
			 */
			if (pciio)
				break;

			first_uga = uga;
		}
	}

	return status;
}

static efi_status_t
setup_uga64(void **uga_handle, unsigned long size, u32 *width, u32 *height)
{
	struct efi_uga_draw_protocol *uga = NULL, *first_uga;
	efi_guid_t uga_proto = EFI_UGA_PROTOCOL_GUID;
	unsigned long nr_ugas;
	u64 *handles = (u64 *)uga_handle;;
	efi_status_t status;
	int i;

	first_uga = NULL;
	nr_ugas = size / sizeof(u64);
	for (i = 0; i < nr_ugas; i++) {
		efi_guid_t pciio_proto = EFI_PCI_IO_PROTOCOL_GUID;
		u32 w, h, depth, refresh;
		void *pciio;
		u64 handle = handles[i];

		status = efi_call_early(handle_protocol, handle,
					&uga_proto, (void **)&uga);
		if (status != EFI_SUCCESS)
			continue;

		efi_call_early(handle_protocol, handle, &pciio_proto, &pciio);

		status = efi_early->call((unsigned long)uga->get_mode, uga,
					 &w, &h, &depth, &refresh);
		if (status == EFI_SUCCESS && (!first_uga || pciio)) {
			*width = w;
			*height = h;

			/*
			 * Once we've found a UGA supporting PCIIO,
			 * don't bother looking any further.
			 */
			if (pciio)
				break;

			first_uga = uga;
		}
	}

	return status;
}

/*
 * See if we have Universal Graphics Adapter (UGA) protocol
 */
static efi_status_t setup_uga(struct screen_info *si, efi_guid_t *uga_proto,
			      unsigned long size)
{
	efi_status_t status;
	u32 width, height;
	void **uga_handle = NULL;

	status = efi_call_early(allocate_pool, EFI_LOADER_DATA,
				size, (void **)&uga_handle);
	if (status != EFI_SUCCESS)
		return status;

	status = efi_call_early(locate_handle,
				EFI_LOCATE_BY_PROTOCOL,
				uga_proto, NULL, &size, uga_handle);
	if (status != EFI_SUCCESS)
		goto free_handle;

	height = 0;
	width = 0;

	if (efi_early->is64)
		status = setup_uga64(uga_handle, size, &width, &height);
	else
		status = setup_uga32(uga_handle, size, &width, &height);

	if (!width && !height)
		goto free_handle;

	/* EFI framebuffer */
	si->orig_video_isVGA = VIDEO_TYPE_EFI;

	si->lfb_depth = 32;
	si->lfb_width = width;
	si->lfb_height = height;

	si->red_size = 8;
	si->red_pos = 16;
	si->green_size = 8;
	si->green_pos = 8;
	si->blue_size = 8;
	si->blue_pos = 0;
	si->rsvd_size = 8;
	si->rsvd_pos = 24;

free_handle:
	efi_call_early(free_pool, uga_handle);
	return status;
}

void setup_graphics(struct boot_params *boot_params)
{
	efi_guid_t graphics_proto = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	struct screen_info *si;
	efi_guid_t uga_proto = EFI_UGA_PROTOCOL_GUID;
	efi_status_t status;
	unsigned long size;
	void **gop_handle = NULL;
	void **uga_handle = NULL;

	si = &boot_params->screen_info;
	memset(si, 0, sizeof(*si));

	size = 0;
	status = efi_call_early(locate_handle,
				EFI_LOCATE_BY_PROTOCOL,
				&graphics_proto, NULL, &size, gop_handle);
	if (status == EFI_BUFFER_TOO_SMALL)
		status = setup_gop(si, &graphics_proto, size);

	if (status != EFI_SUCCESS) {
		size = 0;
		status = efi_call_early(locate_handle,
					EFI_LOCATE_BY_PROTOCOL,
					&uga_proto, NULL, &size, uga_handle);
		if (status == EFI_BUFFER_TOO_SMALL)
			setup_uga(si, &uga_proto, size);
	}
}

/*
 * Because the x86 boot code expects to be passed a boot_params we
 * need to create one ourselves (usually the bootloader would create
 * one for us).
 *
 * The caller is responsible for filling out ->code32_start in the
 * returned boot_params.
 */
struct boot_params *make_boot_params(struct efi_config *c)
{
	struct boot_params *boot_params;
	struct sys_desc_table *sdt;
	struct apm_bios_info *bi;
	struct setup_header *hdr;
	struct efi_info *efi;
	efi_loaded_image_t *image;
	void *options, *handle;
	efi_guid_t proto = LOADED_IMAGE_PROTOCOL_GUID;
	int options_size = 0;
	efi_status_t status;
	char *cmdline_ptr;
	u16 *s2;
	u8 *s1;
	int i;
	unsigned long ramdisk_addr;
	unsigned long ramdisk_size;
	unsigned long initrd_addr_max;

	efi_early = c;
	sys_table = (efi_system_table_t *)(unsigned long)efi_early->table;
	handle = (void *)(unsigned long)efi_early->image_handle;

	/* Check if we were booted by the EFI firmware */
	if (sys_table->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE)
		return NULL;

	if (efi_early->is64)
		setup_boot_services64(efi_early);
	else
		setup_boot_services32(efi_early);

	status = efi_call_early(handle_protocol, handle,
				&proto, (void *)&image);
	if (status != EFI_SUCCESS) {
		efi_printk(sys_table, "Failed to get handle for LOADED_IMAGE_PROTOCOL\n");
		return NULL;
	}

	status = efi_low_alloc(sys_table, 0x4000, 1,
			       (unsigned long *)&boot_params);
	if (status != EFI_SUCCESS) {
		efi_printk(sys_table, "Failed to alloc lowmem for boot params\n");
		return NULL;
	}

	memset(boot_params, 0x0, 0x4000);

	hdr = &boot_params->hdr;
	efi = &boot_params->efi_info;
	bi = &boot_params->apm_bios_info;
	sdt = &boot_params->sys_desc_table;

	/* Copy the second sector to boot_params */
	memcpy(&hdr->jump, image->image_base + 512, 512);

	/*
	 * Fill out some of the header fields ourselves because the
	 * EFI firmware loader doesn't load the first sector.
	 */
	hdr->root_flags = 1;
	hdr->vid_mode = 0xffff;
	hdr->boot_flag = 0xAA55;

	hdr->type_of_loader = 0x21;

	/* Convert unicode cmdline to ascii */
	cmdline_ptr = efi_convert_cmdline(sys_table, image, &options_size);
	if (!cmdline_ptr)
		goto fail;
	hdr->cmd_line_ptr = (unsigned long)cmdline_ptr;

	hdr->ramdisk_image = 0;
	hdr->ramdisk_size = 0;

	/* Clear APM BIOS info */
	memset(bi, 0, sizeof(*bi));

	memset(sdt, 0, sizeof(*sdt));

	if (hdr->xloadflags & XLF_CAN_BE_LOADED_ABOVE_4G)
		initrd_addr_max = -1UL;
	else
		initrd_addr_max = hdr->initrd_addr_max;

	status = handle_cmdline_files(sys_table, image,
				      (char *)(unsigned long)hdr->cmd_line_ptr,
				      "initrd=", initrd_addr_max,
				      &ramdisk_addr, &ramdisk_size);
	if (status != EFI_SUCCESS)
		goto fail2;
	hdr->ramdisk_image = ramdisk_addr & 0xffffffff;
	hdr->ramdisk_size  = ramdisk_size & 0xffffffff;
	boot_params->ext_ramdisk_image = (u64)ramdisk_addr >> 32;
	boot_params->ext_ramdisk_size  = (u64)ramdisk_size >> 32;

	return boot_params;
fail2:
	efi_free(sys_table, options_size, hdr->cmd_line_ptr);
fail:
	efi_free(sys_table, 0x4000, (unsigned long)boot_params);
	return NULL;
}

static void add_e820ext(struct boot_params *params,
			struct setup_data *e820ext, u32 nr_entries)
{
	struct setup_data *data;
	efi_status_t status;
	unsigned long size;

	e820ext->type = SETUP_E820_EXT;
	e820ext->len = nr_entries * sizeof(struct e820entry);
	e820ext->next = 0;

	data = (struct setup_data *)(unsigned long)params->hdr.setup_data;

	while (data && data->next)
		data = (struct setup_data *)(unsigned long)data->next;

	if (data)
		data->next = (unsigned long)e820ext;
	else
		params->hdr.setup_data = (unsigned long)e820ext;
}

static efi_status_t setup_e820(struct boot_params *params,
			       struct setup_data *e820ext, u32 e820ext_size)
{
	struct e820entry *e820_map = &params->e820_map[0];
	struct efi_info *efi = &params->efi_info;
	struct e820entry *prev = NULL;
	u32 nr_entries;
	u32 nr_desc;
	int i;

	nr_entries = 0;
	nr_desc = efi->efi_memmap_size / efi->efi_memdesc_size;

	for (i = 0; i < nr_desc; i++) {
		efi_memory_desc_t *d;
		unsigned int e820_type = 0;
		unsigned long m = efi->efi_memmap;

		d = (efi_memory_desc_t *)(m + (i * efi->efi_memdesc_size));
		switch (d->type) {
		case EFI_RESERVED_TYPE:
		case EFI_RUNTIME_SERVICES_CODE:
		case EFI_RUNTIME_SERVICES_DATA:
		case EFI_MEMORY_MAPPED_IO:
		case EFI_MEMORY_MAPPED_IO_PORT_SPACE:
		case EFI_PAL_CODE:
			e820_type = E820_RESERVED;
			break;

		case EFI_UNUSABLE_MEMORY:
			e820_type = E820_UNUSABLE;
			break;

		case EFI_ACPI_RECLAIM_MEMORY:
			e820_type = E820_ACPI;
			break;

		case EFI_LOADER_CODE:
		case EFI_LOADER_DATA:
		case EFI_BOOT_SERVICES_CODE:
		case EFI_BOOT_SERVICES_DATA:
		case EFI_CONVENTIONAL_MEMORY:
			e820_type = E820_RAM;
			break;

		case EFI_ACPI_MEMORY_NVS:
			e820_type = E820_NVS;
			break;

		default:
			continue;
		}

		/* Merge adjacent mappings */
		if (prev && prev->type == e820_type &&
		    (prev->addr + prev->size) == d->phys_addr) {
			prev->size += d->num_pages << 12;
			continue;
		}

		if (nr_entries == ARRAY_SIZE(params->e820_map)) {
			u32 need = (nr_desc - i) * sizeof(struct e820entry) +
				   sizeof(struct setup_data);

			if (!e820ext || e820ext_size < need)
				return EFI_BUFFER_TOO_SMALL;

			/* boot_params map full, switch to e820 extended */
			e820_map = (struct e820entry *)e820ext->data;
		}

		e820_map->addr = d->phys_addr;
		e820_map->size = d->num_pages << PAGE_SHIFT;
		e820_map->type = e820_type;
		prev = e820_map++;
		nr_entries++;
	}

	if (nr_entries > ARRAY_SIZE(params->e820_map)) {
		u32 nr_e820ext = nr_entries - ARRAY_SIZE(params->e820_map);

		add_e820ext(params, e820ext, nr_e820ext);
		nr_entries -= nr_e820ext;
	}

	params->e820_entries = (u8)nr_entries;

	return EFI_SUCCESS;
}

static efi_status_t alloc_e820ext(u32 nr_desc, struct setup_data **e820ext,
				  u32 *e820ext_size)
{
	efi_status_t status;
	unsigned long size;

	size = sizeof(struct setup_data) +
		sizeof(struct e820entry) * nr_desc;

	if (*e820ext) {
		efi_call_early(free_pool, *e820ext);
		*e820ext = NULL;
		*e820ext_size = 0;
	}

	status = efi_call_early(allocate_pool, EFI_LOADER_DATA,
				size, (void **)e820ext);
	if (status == EFI_SUCCESS)
		*e820ext_size = size;

	return status;
}

static efi_status_t exit_boot(struct boot_params *boot_params,
			      void *handle, bool is64)
{
	struct efi_info *efi = &boot_params->efi_info;
	unsigned long map_sz, key, desc_size;
	efi_memory_desc_t *mem_map;
	struct setup_data *e820ext;
	const char *signature;
	__u32 e820ext_size;
	__u32 nr_desc, prev_nr_desc;
	efi_status_t status;
	__u32 desc_version;
	bool called_exit = false;
	u8 nr_entries;
	int i;

	nr_desc = 0;
	e820ext = NULL;
	e820ext_size = 0;

get_map:
	status = efi_get_memory_map(sys_table, &mem_map, &map_sz, &desc_size,
				    &desc_version, &key);

	if (status != EFI_SUCCESS)
		return status;

	prev_nr_desc = nr_desc;
	nr_desc = map_sz / desc_size;
	if (nr_desc > prev_nr_desc &&
	    nr_desc > ARRAY_SIZE(boot_params->e820_map)) {
		u32 nr_e820ext = nr_desc - ARRAY_SIZE(boot_params->e820_map);

		status = alloc_e820ext(nr_e820ext, &e820ext, &e820ext_size);
		if (status != EFI_SUCCESS)
			goto free_mem_map;

		efi_call_early(free_pool, mem_map);
		goto get_map; /* Allocated memory, get map again */
	}

	signature = is64 ? EFI64_LOADER_SIGNATURE : EFI32_LOADER_SIGNATURE;
	memcpy(&efi->efi_loader_signature, signature, sizeof(__u32));

	efi->efi_systab = (unsigned long)sys_table;
	efi->efi_memdesc_size = desc_size;
	efi->efi_memdesc_version = desc_version;
	efi->efi_memmap = (unsigned long)mem_map;
	efi->efi_memmap_size = map_sz;

#ifdef CONFIG_X86_64
	efi->efi_systab_hi = (unsigned long)sys_table >> 32;
	efi->efi_memmap_hi = (unsigned long)mem_map >> 32;
#endif

	/* Might as well exit boot services now */
	status = efi_call_early(exit_boot_services, handle, key);
	if (status != EFI_SUCCESS) {
		/*
		 * ExitBootServices() will fail if any of the event
		 * handlers change the memory map. In which case, we
		 * must be prepared to retry, but only once so that
		 * we're guaranteed to exit on repeated failures instead
		 * of spinning forever.
		 */
		if (called_exit)
			goto free_mem_map;

		called_exit = true;
		efi_call_early(free_pool, mem_map);
		goto get_map;
	}

	/* Historic? */
	boot_params->alt_mem_k = 32 * 1024;

	status = setup_e820(boot_params, e820ext, e820ext_size);
	if (status != EFI_SUCCESS)
		return status;

	return EFI_SUCCESS;

free_mem_map:
	efi_call_early(free_pool, mem_map);
	return status;
}

/*
 * On success we return a pointer to a boot_params structure, and NULL
 * on failure.
 */
struct boot_params *efi_main(struct efi_config *c,
			     struct boot_params *boot_params)
{
	struct desc_ptr *gdt = NULL;
	efi_loaded_image_t *image;
	struct setup_header *hdr = &boot_params->hdr;
	efi_status_t status;
	struct desc_struct *desc;
	void *handle;
	efi_system_table_t *_table;
	bool is64;

	efi_early = c;

	_table = (efi_system_table_t *)(unsigned long)efi_early->table;
	handle = (void *)(unsigned long)efi_early->image_handle;
	is64 = efi_early->is64;

	sys_table = _table;

	/* Check if we were booted by the EFI firmware */
	if (sys_table->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE)
		goto fail;

	if (is64)
		setup_boot_services64(efi_early);
	else
		setup_boot_services32(efi_early);

	setup_graphics(boot_params);

	setup_efi_pci(boot_params);

	status = efi_call_early(allocate_pool, EFI_LOADER_DATA,
				sizeof(*gdt), (void **)&gdt);
	if (status != EFI_SUCCESS) {
		efi_printk(sys_table, "Failed to alloc mem for gdt structure\n");
		goto fail;
	}

	gdt->size = 0x800;
	status = efi_low_alloc(sys_table, gdt->size, 8,
			   (unsigned long *)&gdt->address);
	if (status != EFI_SUCCESS) {
		efi_printk(sys_table, "Failed to alloc mem for gdt\n");
		goto fail;
	}

	/*
	 * If the kernel isn't already loaded at the preferred load
	 * address, relocate it.
	 */
	if (hdr->pref_address != hdr->code32_start) {
		unsigned long bzimage_addr = hdr->code32_start;
		status = efi_relocate_kernel(sys_table, &bzimage_addr,
					     hdr->init_size, hdr->init_size,
					     hdr->pref_address,
					     hdr->kernel_alignment);
		if (status != EFI_SUCCESS)
			goto fail;

		hdr->pref_address = hdr->code32_start;
		hdr->code32_start = bzimage_addr;
	}

	status = exit_boot(boot_params, handle, is64);
	if (status != EFI_SUCCESS)
		goto fail;

	memset((char *)gdt->address, 0x0, gdt->size);
	desc = (struct desc_struct *)gdt->address;

	/* The first GDT is a dummy and the second is unused. */
	desc += 2;

	desc->limit0 = 0xffff;
	desc->base0 = 0x0000;
	desc->base1 = 0x0000;
	desc->type = SEG_TYPE_CODE | SEG_TYPE_EXEC_READ;
	desc->s = DESC_TYPE_CODE_DATA;
	desc->dpl = 0;
	desc->p = 1;
	desc->limit = 0xf;
	desc->avl = 0;
	desc->l = 0;
	desc->d = SEG_OP_SIZE_32BIT;
	desc->g = SEG_GRANULARITY_4KB;
	desc->base2 = 0x00;

	desc++;
	desc->limit0 = 0xffff;
	desc->base0 = 0x0000;
	desc->base1 = 0x0000;
	desc->type = SEG_TYPE_DATA | SEG_TYPE_READ_WRITE;
	desc->s = DESC_TYPE_CODE_DATA;
	desc->dpl = 0;
	desc->p = 1;
	desc->limit = 0xf;
	desc->avl = 0;
	desc->l = 0;
	desc->d = SEG_OP_SIZE_32BIT;
	desc->g = SEG_GRANULARITY_4KB;
	desc->base2 = 0x00;

#ifdef CONFIG_X86_64
	/* Task segment value */
	desc++;
	desc->limit0 = 0x0000;
	desc->base0 = 0x0000;
	desc->base1 = 0x0000;
	desc->type = SEG_TYPE_TSS;
	desc->s = 0;
	desc->dpl = 0;
	desc->p = 1;
	desc->limit = 0x0;
	desc->avl = 0;
	desc->l = 0;
	desc->d = 0;
	desc->g = SEG_GRANULARITY_4KB;
	desc->base2 = 0x00;
#endif /* CONFIG_X86_64 */

	asm volatile("cli");
	asm volatile ("lgdt %0" : : "m" (*gdt));

	return boot_params;
fail:
	return NULL;
}
