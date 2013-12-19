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

static efi_status_t setup_efi_pci(struct boot_params *params)
{
	efi_pci_io_protocol *pci;
	efi_status_t status;
	void **pci_handle;
	efi_guid_t pci_proto = EFI_PCI_IO_PROTOCOL_GUID;
	unsigned long nr_pci, size = 0;
	int i;
	struct setup_data *data;

	data = (struct setup_data *)(unsigned long)params->hdr.setup_data;

	while (data && data->next)
		data = (struct setup_data *)(unsigned long)data->next;

	status = efi_call_phys5(sys_table->boottime->locate_handle,
				EFI_LOCATE_BY_PROTOCOL, &pci_proto,
				NULL, &size, pci_handle);

	if (status == EFI_BUFFER_TOO_SMALL) {
		status = efi_call_phys3(sys_table->boottime->allocate_pool,
					EFI_LOADER_DATA, size, &pci_handle);

		if (status != EFI_SUCCESS)
			return status;

		status = efi_call_phys5(sys_table->boottime->locate_handle,
					EFI_LOCATE_BY_PROTOCOL, &pci_proto,
					NULL, &size, pci_handle);
	}

	if (status != EFI_SUCCESS)
		goto free_handle;

	nr_pci = size / sizeof(void *);
	for (i = 0; i < nr_pci; i++) {
		void *h = pci_handle[i];
		uint64_t attributes;
		struct pci_setup_rom *rom;

		status = efi_call_phys3(sys_table->boottime->handle_protocol,
					h, &pci_proto, &pci);

		if (status != EFI_SUCCESS)
			continue;

		if (!pci)
			continue;

#ifdef CONFIG_X86_64
		status = efi_call_phys4(pci->attributes, pci,
					EfiPciIoAttributeOperationGet, 0,
					&attributes);
#else
		status = efi_call_phys5(pci->attributes, pci,
					EfiPciIoAttributeOperationGet, 0, 0,
					&attributes);
#endif
		if (status != EFI_SUCCESS)
			continue;

		if (!pci->romimage || !pci->romsize)
			continue;

		size = pci->romsize + sizeof(*rom);

		status = efi_call_phys3(sys_table->boottime->allocate_pool,
				EFI_LOADER_DATA, size, &rom);

		if (status != EFI_SUCCESS)
			continue;

		rom->data.type = SETUP_PCI;
		rom->data.len = size - sizeof(struct setup_data);
		rom->data.next = 0;
		rom->pcilen = pci->romsize;

		status = efi_call_phys5(pci->pci.read, pci,
					EfiPciIoWidthUint16, PCI_VENDOR_ID,
					1, &(rom->vendor));

		if (status != EFI_SUCCESS)
			goto free_struct;

		status = efi_call_phys5(pci->pci.read, pci,
					EfiPciIoWidthUint16, PCI_DEVICE_ID,
					1, &(rom->devid));

		if (status != EFI_SUCCESS)
			goto free_struct;

		status = efi_call_phys5(pci->get_location, pci,
					&(rom->segment), &(rom->bus),
					&(rom->device), &(rom->function));

		if (status != EFI_SUCCESS)
			goto free_struct;

		memcpy(rom->romdata, pci->romimage, pci->romsize);

		if (data)
			data->next = (unsigned long)rom;
		else
			params->hdr.setup_data = (unsigned long)rom;

		data = (struct setup_data *)rom;

		continue;
	free_struct:
		efi_call_phys1(sys_table->boottime->free_pool, rom);
	}

free_handle:
	efi_call_phys1(sys_table->boottime->free_pool, pci_handle);
	return status;
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
			first_gop = gop;
			if (conout_found)
				break;
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

	si->lfb_size = si->lfb_linelength * si->lfb_height;

	si->capabilities |= VIDEO_CAPABILITY_SKIP_QUIRKS;

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
	efi_guid_t proto = LOADED_IMAGE_PROTOCOL_GUID;
	int options_size = 0;
	efi_status_t status;
	char *cmdline_ptr;
	u16 *s2;
	u8 *s1;
	int i;
	unsigned long ramdisk_addr;
	unsigned long ramdisk_size;

	sys_table = _table;

	/* Check if we were booted by the EFI firmware */
	if (sys_table->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE)
		return NULL;

	status = efi_call_phys3(sys_table->boottime->handle_protocol,
				handle, &proto, (void *)&image);
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

	hdr->code32_start = (__u64)(unsigned long)image->image_base;

	hdr->type_of_loader = 0x21;

	/* Convert unicode cmdline to ascii */
	cmdline_ptr = efi_convert_cmdline_to_ascii(sys_table, image,
						   &options_size);
	if (!cmdline_ptr)
		goto fail;
	hdr->cmd_line_ptr = (unsigned long)cmdline_ptr;

	hdr->ramdisk_image = 0;
	hdr->ramdisk_size = 0;

	/* Clear APM BIOS info */
	memset(bi, 0, sizeof(*bi));

	memset(sdt, 0, sizeof(*sdt));

	status = handle_cmdline_files(sys_table, image,
				      (char *)(unsigned long)hdr->cmd_line_ptr,
				      "initrd=", hdr->initrd_addr_max,
				      &ramdisk_addr, &ramdisk_size);
	if (status != EFI_SUCCESS)
		goto fail2;
	hdr->ramdisk_image = ramdisk_addr;
	hdr->ramdisk_size = ramdisk_size;

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
		efi_call_phys1(sys_table->boottime->free_pool, *e820ext);
		*e820ext = NULL;
		*e820ext_size = 0;
	}

	status = efi_call_phys3(sys_table->boottime->allocate_pool,
				EFI_LOADER_DATA, size, e820ext);

	if (status == EFI_SUCCESS)
		*e820ext_size = size;

	return status;
}

static efi_status_t exit_boot(struct boot_params *boot_params,
			      void *handle)
{
	struct efi_info *efi = &boot_params->efi_info;
	unsigned long map_sz, key, desc_size;
	efi_memory_desc_t *mem_map;
	struct setup_data *e820ext;
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

		efi_call_phys1(sys_table->boottime->free_pool, mem_map);
		goto get_map; /* Allocated memory, get map again */
	}

	memcpy(&efi->efi_loader_signature, EFI_LOADER_SIGNATURE, sizeof(__u32));
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
	status = efi_call_phys2(sys_table->boottime->exit_boot_services,
				handle, key);
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
		efi_call_phys1(sys_table->boottime->free_pool, mem_map);
		goto get_map;
	}

	/* Historic? */
	boot_params->alt_mem_k = 32 * 1024;

	status = setup_e820(boot_params, e820ext, e820ext_size);
	if (status != EFI_SUCCESS)
		return status;

	return EFI_SUCCESS;

free_mem_map:
	efi_call_phys1(sys_table->boottime->free_pool, mem_map);
	return status;
}


/*
 * On success we return a pointer to a boot_params structure, and NULL
 * on failure.
 */
struct boot_params *efi_main(void *handle, efi_system_table_t *_table,
			     struct boot_params *boot_params)
{
	struct desc_ptr *gdt;
	efi_loaded_image_t *image;
	struct setup_header *hdr = &boot_params->hdr;
	efi_status_t status;
	struct desc_struct *desc;

	sys_table = _table;

	/* Check if we were booted by the EFI firmware */
	if (sys_table->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE)
		goto fail;

	setup_graphics(boot_params);

	setup_efi_pci(boot_params);

	status = efi_call_phys3(sys_table->boottime->allocate_pool,
				EFI_LOADER_DATA, sizeof(*gdt),
				(void **)&gdt);
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

	asm volatile("cli");
	asm volatile ("lgdt %0" : : "m" (*gdt));

	return boot_params;
fail:
	return NULL;
}
