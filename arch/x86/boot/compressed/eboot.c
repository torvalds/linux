/* -----------------------------------------------------------------------
 *
 *   Copyright 2011 Intel Corporation; author Matt Fleming
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

#include <linux/efi.h>
#include <asm/efi.h>
#include <asm/setup.h>
#include <asm/desc.h>

#include "eboot.h"

static efi_system_table_t *sys_table;

static void efi_printk(char *str)
{
	char *s8;

	for (s8 = str; *s8; s8++) {
		struct efi_simple_text_output_protocol *out;
		efi_char16_t ch[2] = { 0 };

		ch[0] = *s8;
		out = (struct efi_simple_text_output_protocol *)sys_table->con_out;

		if (*s8 == '\n') {
			efi_char16_t nl[2] = { '\r', 0 };
			efi_call_phys2(out->output_string, out, nl);
		}

		efi_call_phys2(out->output_string, out, ch);
	}
}

static efi_status_t __get_map(efi_memory_desc_t **map, unsigned long *map_size,
			      unsigned long *desc_size)
{
	efi_memory_desc_t *m = NULL;
	efi_status_t status;
	unsigned long key;
	u32 desc_version;

	*map_size = sizeof(*m) * 32;
again:
	/*
	 * Add an additional efi_memory_desc_t because we're doing an
	 * allocation which may be in a new descriptor region.
	 */
	*map_size += sizeof(*m);
	status = efi_call_phys3(sys_table->boottime->allocate_pool,
				EFI_LOADER_DATA, *map_size, (void **)&m);
	if (status != EFI_SUCCESS)
		goto fail;

	status = efi_call_phys5(sys_table->boottime->get_memory_map, map_size,
				m, &key, desc_size, &desc_version);
	if (status == EFI_BUFFER_TOO_SMALL) {
		efi_call_phys1(sys_table->boottime->free_pool, m);
		goto again;
	}

	if (status != EFI_SUCCESS)
		efi_call_phys1(sys_table->boottime->free_pool, m);

fail:
	*map = m;
	return status;
}

/*
 * Allocate at the highest possible address that is not above 'max'.
 */
static efi_status_t high_alloc(unsigned long size, unsigned long align,
			      unsigned long *addr, unsigned long max)
{
	unsigned long map_size, desc_size;
	efi_memory_desc_t *map;
	efi_status_t status;
	unsigned long nr_pages;
	u64 max_addr = 0;
	int i;

	status = __get_map(&map, &map_size, &desc_size);
	if (status != EFI_SUCCESS)
		goto fail;

	nr_pages = round_up(size, EFI_PAGE_SIZE) / EFI_PAGE_SIZE;
again:
	for (i = 0; i < map_size / desc_size; i++) {
		efi_memory_desc_t *desc;
		unsigned long m = (unsigned long)map;
		u64 start, end;

		desc = (efi_memory_desc_t *)(m + (i * desc_size));
		if (desc->type != EFI_CONVENTIONAL_MEMORY)
			continue;

		if (desc->num_pages < nr_pages)
			continue;

		start = desc->phys_addr;
		end = start + desc->num_pages * (1UL << EFI_PAGE_SHIFT);

		if ((start + size) > end || (start + size) > max)
			continue;

		if (end - size > max)
			end = max;

		if (round_down(end - size, align) < start)
			continue;

		start = round_down(end - size, align);

		/*
		 * Don't allocate at 0x0. It will confuse code that
		 * checks pointers against NULL.
		 */
		if (start == 0x0)
			continue;

		if (start > max_addr)
			max_addr = start;
	}

	if (!max_addr)
		status = EFI_NOT_FOUND;
	else {
		status = efi_call_phys4(sys_table->boottime->allocate_pages,
					EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA,
					nr_pages, &max_addr);
		if (status != EFI_SUCCESS) {
			max = max_addr;
			max_addr = 0;
			goto again;
		}

		*addr = max_addr;
	}

free_pool:
	efi_call_phys1(sys_table->boottime->free_pool, map);

fail:
	return status;
}

/*
 * Allocate at the lowest possible address.
 */
static efi_status_t low_alloc(unsigned long size, unsigned long align,
			      unsigned long *addr)
{
	unsigned long map_size, desc_size;
	efi_memory_desc_t *map;
	efi_status_t status;
	unsigned long nr_pages;
	int i;

	status = __get_map(&map, &map_size, &desc_size);
	if (status != EFI_SUCCESS)
		goto fail;

	nr_pages = round_up(size, EFI_PAGE_SIZE) / EFI_PAGE_SIZE;
	for (i = 0; i < map_size / desc_size; i++) {
		efi_memory_desc_t *desc;
		unsigned long m = (unsigned long)map;
		u64 start, end;

		desc = (efi_memory_desc_t *)(m + (i * desc_size));

		if (desc->type != EFI_CONVENTIONAL_MEMORY)
			continue;

		if (desc->num_pages < nr_pages)
			continue;

		start = desc->phys_addr;
		end = start + desc->num_pages * (1UL << EFI_PAGE_SHIFT);

		/*
		 * Don't allocate at 0x0. It will confuse code that
		 * checks pointers against NULL. Skip the first 8
		 * bytes so we start at a nice even number.
		 */
		if (start == 0x0)
			start += 8;

		start = round_up(start, align);
		if ((start + size) > end)
			continue;

		status = efi_call_phys4(sys_table->boottime->allocate_pages,
					EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA,
					nr_pages, &start);
		if (status == EFI_SUCCESS) {
			*addr = start;
			break;
		}
	}

	if (i == map_size / desc_size)
		status = EFI_NOT_FOUND;

free_pool:
	efi_call_phys1(sys_table->boottime->free_pool, map);
fail:
	return status;
}

static void low_free(unsigned long size, unsigned long addr)
{
	unsigned long nr_pages;

	nr_pages = round_up(size, EFI_PAGE_SIZE) / EFI_PAGE_SIZE;
	efi_call_phys2(sys_table->boottime->free_pages, addr, size);
}

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

/*
 * See if we have Graphics Output Protocol
 */
static efi_status_t setup_gop(struct screen_info *si, efi_guid_t *proto,
			      unsigned long size)
{
	struct efi_graphics_output_protocol *gop, *first_gop;
	struct efi_pixel_bitmask pixel_info;
	unsigned long nr_gops;
	efi_status_t status;
	void **gop_handle;
	u16 width, height;
	u32 fb_base, fb_size;
	u32 pixels_per_scan_line;
	int pixel_format;
	int i;

	status = efi_call_phys3(sys_table->boottime->allocate_pool,
				EFI_LOADER_DATA, size, &gop_handle);
	if (status != EFI_SUCCESS)
		return status;

	status = efi_call_phys5(sys_table->boottime->locate_handle,
				EFI_LOCATE_BY_PROTOCOL, proto,
				NULL, &size, gop_handle);
	if (status != EFI_SUCCESS)
		goto free_handle;

	first_gop = NULL;

	nr_gops = size / sizeof(void *);
	for (i = 0; i < nr_gops; i++) {
		struct efi_graphics_output_mode_info *info;
		efi_guid_t conout_proto = EFI_CONSOLE_OUT_DEVICE_GUID;
		bool conout_found = false;
		void *dummy;
		void *h = gop_handle[i];

		status = efi_call_phys3(sys_table->boottime->handle_protocol,
					h, proto, &gop);
		if (status != EFI_SUCCESS)
			continue;

		status = efi_call_phys3(sys_table->boottime->handle_protocol,
					h, &conout_proto, &dummy);

		if (status == EFI_SUCCESS)
			conout_found = true;

		status = efi_call_phys4(gop->query_mode, gop,
					gop->mode->mode, &size, &info);
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
			fb_base = gop->mode->frame_buffer_base;
			fb_size = gop->mode->frame_buffer_size;
			pixel_format = info->pixel_format;
			pixel_info = info->pixel_information;
			pixels_per_scan_line = info->pixels_per_scan_line;

			/*
			 * Once we've found a GOP supporting ConOut,
			 * don't bother looking any further.
			 */
			if (conout_found)
				break;

			first_gop = gop;
		}
	}

	/* Did we find any GOPs? */
	if (!first_gop)
		goto free_handle;

	/* EFI framebuffer */
	si->orig_video_isVGA = VIDEO_TYPE_EFI;

	si->lfb_width = width;
	si->lfb_height = height;
	si->lfb_base = fb_base;
	si->lfb_size = fb_size;
	si->pages = 1;

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

free_handle:
	efi_call_phys1(sys_table->boottime->free_pool, gop_handle);
	return status;
}

/*
 * See if we have Universal Graphics Adapter (UGA) protocol
 */
static efi_status_t setup_uga(struct screen_info *si, efi_guid_t *uga_proto,
			      unsigned long size)
{
	struct efi_uga_draw_protocol *uga, *first_uga;
	unsigned long nr_ugas;
	efi_status_t status;
	u32 width, height;
	void **uga_handle = NULL;
	int i;

	status = efi_call_phys3(sys_table->boottime->allocate_pool,
				EFI_LOADER_DATA, size, &uga_handle);
	if (status != EFI_SUCCESS)
		return status;

	status = efi_call_phys5(sys_table->boottime->locate_handle,
				EFI_LOCATE_BY_PROTOCOL, uga_proto,
				NULL, &size, uga_handle);
	if (status != EFI_SUCCESS)
		goto free_handle;

	first_uga = NULL;

	nr_ugas = size / sizeof(void *);
	for (i = 0; i < nr_ugas; i++) {
		efi_guid_t pciio_proto = EFI_PCI_IO_PROTOCOL_GUID;
		void *handle = uga_handle[i];
		u32 w, h, depth, refresh;
		void *pciio;

		status = efi_call_phys3(sys_table->boottime->handle_protocol,
					handle, uga_proto, &uga);
		if (status != EFI_SUCCESS)
			continue;

		efi_call_phys3(sys_table->boottime->handle_protocol,
			       handle, &pciio_proto, &pciio);

		status = efi_call_phys5(uga->get_mode, uga, &w, &h,
					&depth, &refresh);
		if (status == EFI_SUCCESS && (!first_uga || pciio)) {
			width = w;
			height = h;

			/*
			 * Once we've found a UGA supporting PCIIO,
			 * don't bother looking any further.
			 */
			if (pciio)
				break;

			first_uga = uga;
		}
	}

	if (!first_uga)
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
	efi_call_phys1(sys_table->boottime->free_pool, uga_handle);
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
	status = efi_call_phys5(sys_table->boottime->locate_handle,
				EFI_LOCATE_BY_PROTOCOL, &graphics_proto,
				NULL, &size, gop_handle);
	if (status == EFI_BUFFER_TOO_SMALL)
		status = setup_gop(si, &graphics_proto, size);

	if (status != EFI_SUCCESS) {
		size = 0;
		status = efi_call_phys5(sys_table->boottime->locate_handle,
					EFI_LOCATE_BY_PROTOCOL, &uga_proto,
					NULL, &size, uga_handle);
		if (status == EFI_BUFFER_TOO_SMALL)
			setup_uga(si, &uga_proto, size);
	}
}

struct initrd {
	efi_file_handle_t *handle;
	u64 size;
};

/*
 * Check the cmdline for a LILO-style initrd= arguments.
 *
 * We only support loading an initrd from the same filesystem as the
 * kernel image.
 */
static efi_status_t handle_ramdisks(efi_loaded_image_t *image,
				    struct setup_header *hdr)
{
	struct initrd *initrds;
	unsigned long initrd_addr;
	efi_guid_t fs_proto = EFI_FILE_SYSTEM_GUID;
	u64 initrd_total;
	efi_file_io_interface_t *io;
	efi_file_handle_t *fh;
	efi_status_t status;
	int nr_initrds;
	char *str;
	int i, j, k;

	initrd_addr = 0;
	initrd_total = 0;

	str = (char *)(unsigned long)hdr->cmd_line_ptr;

	j = 0;			/* See close_handles */

	if (!str || !*str)
		return EFI_SUCCESS;

	for (nr_initrds = 0; *str; nr_initrds++) {
		str = strstr(str, "initrd=");
		if (!str)
			break;

		str += 7;

		/* Skip any leading slashes */
		while (*str == '/' || *str == '\\')
			str++;

		while (*str && *str != ' ' && *str != '\n')
			str++;
	}

	if (!nr_initrds)
		return EFI_SUCCESS;

	status = efi_call_phys3(sys_table->boottime->allocate_pool,
				EFI_LOADER_DATA,
				nr_initrds * sizeof(*initrds),
				&initrds);
	if (status != EFI_SUCCESS) {
		efi_printk("Failed to alloc mem for initrds\n");
		goto fail;
	}

	str = (char *)(unsigned long)hdr->cmd_line_ptr;
	for (i = 0; i < nr_initrds; i++) {
		struct initrd *initrd;
		efi_file_handle_t *h;
		efi_file_info_t *info;
		efi_char16_t filename_16[256];
		unsigned long info_sz;
		efi_guid_t info_guid = EFI_FILE_INFO_ID;
		efi_char16_t *p;
		u64 file_sz;

		str = strstr(str, "initrd=");
		if (!str)
			break;

		str += 7;

		initrd = &initrds[i];
		p = filename_16;

		/* Skip any leading slashes */
		while (*str == '/' || *str == '\\')
			str++;

		while (*str && *str != ' ' && *str != '\n') {
			if ((u8 *)p >= (u8 *)filename_16 + sizeof(filename_16))
				break;

			*p++ = *str++;
		}

		*p = '\0';

		/* Only open the volume once. */
		if (!i) {
			efi_boot_services_t *boottime;

			boottime = sys_table->boottime;

			status = efi_call_phys3(boottime->handle_protocol,
					image->device_handle, &fs_proto, &io);
			if (status != EFI_SUCCESS) {
				efi_printk("Failed to handle fs_proto\n");
				goto free_initrds;
			}

			status = efi_call_phys2(io->open_volume, io, &fh);
			if (status != EFI_SUCCESS) {
				efi_printk("Failed to open volume\n");
				goto free_initrds;
			}
		}

		status = efi_call_phys5(fh->open, fh, &h, filename_16,
					EFI_FILE_MODE_READ, (u64)0);
		if (status != EFI_SUCCESS) {
			efi_printk("Failed to open initrd file\n");
			goto close_handles;
		}

		initrd->handle = h;

		info_sz = 0;
		status = efi_call_phys4(h->get_info, h, &info_guid,
					&info_sz, NULL);
		if (status != EFI_BUFFER_TOO_SMALL) {
			efi_printk("Failed to get initrd info size\n");
			goto close_handles;
		}

grow:
		status = efi_call_phys3(sys_table->boottime->allocate_pool,
					EFI_LOADER_DATA, info_sz, &info);
		if (status != EFI_SUCCESS) {
			efi_printk("Failed to alloc mem for initrd info\n");
			goto close_handles;
		}

		status = efi_call_phys4(h->get_info, h, &info_guid,
					&info_sz, info);
		if (status == EFI_BUFFER_TOO_SMALL) {
			efi_call_phys1(sys_table->boottime->free_pool, info);
			goto grow;
		}

		file_sz = info->file_size;
		efi_call_phys1(sys_table->boottime->free_pool, info);

		if (status != EFI_SUCCESS) {
			efi_printk("Failed to get initrd info\n");
			goto close_handles;
		}

		initrd->size = file_sz;
		initrd_total += file_sz;
	}

	if (initrd_total) {
		unsigned long addr;

		/*
		 * Multiple initrd's need to be at consecutive
		 * addresses in memory, so allocate enough memory for
		 * all the initrd's.
		 */
		status = high_alloc(initrd_total, 0x1000,
				   &initrd_addr, hdr->initrd_addr_max);
		if (status != EFI_SUCCESS) {
			efi_printk("Failed to alloc highmem for initrds\n");
			goto close_handles;
		}

		/* We've run out of free low memory. */
		if (initrd_addr > hdr->initrd_addr_max) {
			efi_printk("We've run out of free low memory\n");
			status = EFI_INVALID_PARAMETER;
			goto free_initrd_total;
		}

		addr = initrd_addr;
		for (j = 0; j < nr_initrds; j++) {
			u64 size;

			size = initrds[j].size;
			while (size) {
				u64 chunksize;
				if (size > EFI_READ_CHUNK_SIZE)
					chunksize = EFI_READ_CHUNK_SIZE;
				else
					chunksize = size;
				status = efi_call_phys3(fh->read,
							initrds[j].handle,
							&chunksize, addr);
				if (status != EFI_SUCCESS) {
					efi_printk("Failed to read initrd\n");
					goto free_initrd_total;
				}
				addr += chunksize;
				size -= chunksize;
			}

			efi_call_phys1(fh->close, initrds[j].handle);
		}

	}

	efi_call_phys1(sys_table->boottime->free_pool, initrds);

	hdr->ramdisk_image = initrd_addr;
	hdr->ramdisk_size = initrd_total;

	return status;

free_initrd_total:
	low_free(initrd_total, initrd_addr);

close_handles:
	for (k = j; k < i; k++)
		efi_call_phys1(fh->close, initrds[k].handle);
free_initrds:
	efi_call_phys1(sys_table->boottime->free_pool, initrds);
fail:
	hdr->ramdisk_image = 0;
	hdr->ramdisk_size = 0;

	return status;
}

/*
 * Because the x86 boot code expects to be passed a boot_params we
 * need to create one ourselves (usually the bootloader would create
 * one for us).
 */
struct boot_params *make_boot_params(void *handle, efi_system_table_t *_table)
{
	struct boot_params *boot_params;
	struct sys_desc_table *sdt;
	struct apm_bios_info *bi;
	struct setup_header *hdr;
	struct efi_info *efi;
	efi_loaded_image_t *image;
	void *options;
	u32 load_options_size;
	efi_guid_t proto = LOADED_IMAGE_PROTOCOL_GUID;
	int options_size = 0;
	efi_status_t status;
	unsigned long cmdline;
	u16 *s2;
	u8 *s1;
	int i;

	sys_table = _table;

	/* Check if we were booted by the EFI firmware */
	if (sys_table->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE)
		return NULL;

	status = efi_call_phys3(sys_table->boottime->handle_protocol,
				handle, &proto, (void *)&image);
	if (status != EFI_SUCCESS) {
		efi_printk("Failed to get handle for LOADED_IMAGE_PROTOCOL\n");
		return NULL;
	}

	status = low_alloc(0x4000, 1, (unsigned long *)&boot_params);
	if (status != EFI_SUCCESS) {
		efi_printk("Failed to alloc lowmem for boot params\n");
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

	hdr->code32_start = (__u64)(unsigned long)image->image_base;

	hdr->type_of_loader = 0x21;

	/* Convert unicode cmdline to ascii */
	options = image->load_options;
	load_options_size = image->load_options_size / 2; /* ASCII */
	cmdline = 0;
	s2 = (u16 *)options;

	if (s2) {
		while (*s2 && *s2 != '\n' && options_size < load_options_size) {
			s2++;
			options_size++;
		}

		if (options_size) {
			if (options_size > hdr->cmdline_size)
				options_size = hdr->cmdline_size;

			options_size++;	/* NUL termination */

			status = low_alloc(options_size, 1, &cmdline);
			if (status != EFI_SUCCESS) {
				efi_printk("Failed to alloc mem for cmdline\n");
				goto fail;
			}

			s1 = (u8 *)(unsigned long)cmdline;
			s2 = (u16 *)options;

			for (i = 0; i < options_size - 1; i++)
				*s1++ = *s2++;

			*s1 = '\0';
		}
	}

	hdr->cmd_line_ptr = cmdline;

	hdr->ramdisk_image = 0;
	hdr->ramdisk_size = 0;

	/* Clear APM BIOS info */
	memset(bi, 0, sizeof(*bi));

	memset(sdt, 0, sizeof(*sdt));

	status = handle_ramdisks(image, hdr);
	if (status != EFI_SUCCESS)
		goto fail2;

	return boot_params;
fail2:
	if (options_size)
		low_free(options_size, hdr->cmd_line_ptr);
fail:
	low_free(0x4000, (unsigned long)boot_params);
	return NULL;
}

static efi_status_t exit_boot(struct boot_params *boot_params,
			      void *handle)
{
	struct efi_info *efi = &boot_params->efi_info;
	struct e820entry *e820_map = &boot_params->e820_map[0];
	struct e820entry *prev = NULL;
	unsigned long size, key, desc_size, _size;
	efi_memory_desc_t *mem_map;
	efi_status_t status;
	__u32 desc_version;
	u8 nr_entries;
	int i;

	size = sizeof(*mem_map) * 32;

again:
	size += sizeof(*mem_map);
	_size = size;
	status = low_alloc(size, 1, (unsigned long *)&mem_map);
	if (status != EFI_SUCCESS)
		return status;

	status = efi_call_phys5(sys_table->boottime->get_memory_map, &size,
				mem_map, &key, &desc_size, &desc_version);
	if (status == EFI_BUFFER_TOO_SMALL) {
		low_free(_size, (unsigned long)mem_map);
		goto again;
	}

	if (status != EFI_SUCCESS)
		goto free_mem_map;

	memcpy(&efi->efi_loader_signature, EFI_LOADER_SIGNATURE, sizeof(__u32));
	efi->efi_systab = (unsigned long)sys_table;
	efi->efi_memdesc_size = desc_size;
	efi->efi_memdesc_version = desc_version;
	efi->efi_memmap = (unsigned long)mem_map;
	efi->efi_memmap_size = size;

#ifdef CONFIG_X86_64
	efi->efi_systab_hi = (unsigned long)sys_table >> 32;
	efi->efi_memmap_hi = (unsigned long)mem_map >> 32;
#endif

	/* Might as well exit boot services now */
	status = efi_call_phys2(sys_table->boottime->exit_boot_services,
				handle, key);
	if (status != EFI_SUCCESS)
		goto free_mem_map;

	/* Historic? */
	boot_params->alt_mem_k = 32 * 1024;

	/*
	 * Convert the EFI memory map to E820.
	 */
	nr_entries = 0;
	for (i = 0; i < size / desc_size; i++) {
		efi_memory_desc_t *d;
		unsigned int e820_type = 0;
		unsigned long m = (unsigned long)mem_map;

		d = (efi_memory_desc_t *)(m + (i * desc_size));
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
		    (prev->addr + prev->size) == d->phys_addr)
			prev->size += d->num_pages << 12;
		else {
			e820_map->addr = d->phys_addr;
			e820_map->size = d->num_pages << 12;
			e820_map->type = e820_type;
			prev = e820_map++;
			nr_entries++;
		}
	}

	boot_params->e820_entries = nr_entries;

	return EFI_SUCCESS;

free_mem_map:
	low_free(_size, (unsigned long)mem_map);
	return status;
}

static efi_status_t relocate_kernel(struct setup_header *hdr)
{
	unsigned long start, nr_pages;
	efi_status_t status;

	/*
	 * The EFI firmware loader could have placed the kernel image
	 * anywhere in memory, but the kernel has various restrictions
	 * on the max physical address it can run at. Attempt to move
	 * the kernel to boot_params.pref_address, or as low as
	 * possible.
	 */
	start = hdr->pref_address;
	nr_pages = round_up(hdr->init_size, EFI_PAGE_SIZE) / EFI_PAGE_SIZE;

	status = efi_call_phys4(sys_table->boottime->allocate_pages,
				EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA,
				nr_pages, &start);
	if (status != EFI_SUCCESS) {
		status = low_alloc(hdr->init_size, hdr->kernel_alignment,
				   &start);
		if (status != EFI_SUCCESS)
			efi_printk("Failed to alloc mem for kernel\n");
	}

	if (status == EFI_SUCCESS)
		memcpy((void *)start, (void *)(unsigned long)hdr->code32_start,
		       hdr->init_size);

	hdr->pref_address = hdr->code32_start;
	hdr->code32_start = (__u32)start;

	return status;
}

/*
 * On success we return a pointer to a boot_params structure, and NULL
 * on failure.
 */
struct boot_params *efi_main(void *handle, efi_system_table_t *_table,
			     struct boot_params *boot_params)
{
	struct desc_ptr *gdt, *idt;
	efi_loaded_image_t *image;
	struct setup_header *hdr = &boot_params->hdr;
	efi_status_t status;
	struct desc_struct *desc;

	sys_table = _table;

	/* Check if we were booted by the EFI firmware */
	if (sys_table->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE)
		goto fail;

	setup_graphics(boot_params);

	status = efi_call_phys3(sys_table->boottime->allocate_pool,
				EFI_LOADER_DATA, sizeof(*gdt),
				(void **)&gdt);
	if (status != EFI_SUCCESS) {
		efi_printk("Failed to alloc mem for gdt structure\n");
		goto fail;
	}

	gdt->size = 0x800;
	status = low_alloc(gdt->size, 8, (unsigned long *)&gdt->address);
	if (status != EFI_SUCCESS) {
		efi_printk("Failed to alloc mem for gdt\n");
		goto fail;
	}

	status = efi_call_phys3(sys_table->boottime->allocate_pool,
				EFI_LOADER_DATA, sizeof(*idt),
				(void **)&idt);
	if (status != EFI_SUCCESS) {
		efi_printk("Failed to alloc mem for idt structure\n");
		goto fail;
	}

	idt->size = 0;
	idt->address = 0;

	/*
	 * If the kernel isn't already loaded at the preferred load
	 * address, relocate it.
	 */
	if (hdr->pref_address != hdr->code32_start) {
		status = relocate_kernel(hdr);

		if (status != EFI_SUCCESS)
			goto fail;
	}

	status = exit_boot(boot_params, handle);
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

	asm volatile ("lidt %0" : : "m" (*idt));
	asm volatile ("lgdt %0" : : "m" (*gdt));

	asm volatile("cli");

	return boot_params;
fail:
	return NULL;
}
