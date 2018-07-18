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
#include <asm/e820/types.h>
#include <asm/setup.h>
#include <asm/desc.h>

#include "../string.h"
#include "eboot.h"

static efi_system_table_t *sys_table;

static struct efi_config *efi_early;

__pure const struct efi_config *__efi_early(void)
{
	return efi_early;
}

#define BOOT_SERVICES(bits)						\
static void setup_boot_services##bits(struct efi_config *c)		\
{									\
	efi_system_table_##bits##_t *table;				\
									\
	table = (typeof(table))sys_table;				\
									\
	c->runtime_services = table->runtime;				\
	c->boot_services = table->boottime;				\
	c->text_output = table->con_out;				\
}
BOOT_SERVICES(32);
BOOT_SERVICES(64);

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
	efi_call_proto(efi_simple_text_output_protocol, output_string,
		       efi_early->text_output, str);
}

static efi_status_t
__setup_efi_pci(efi_pci_io_protocol_t *pci, struct pci_setup_rom **__rom)
{
	struct pci_setup_rom *rom = NULL;
	efi_status_t status;
	unsigned long size;
	uint64_t romsize;
	void *romimage;

	/*
	 * Some firmware images contain EFI function pointers at the place where
	 * the romimage and romsize fields are supposed to be. Typically the EFI
	 * code is mapped at high addresses, translating to an unrealistically
	 * large romsize. The UEFI spec limits the size of option ROMs to 16
	 * MiB so we reject any ROMs over 16 MiB in size to catch this.
	 */
	romimage = (void *)(unsigned long)efi_table_attr(efi_pci_io_protocol,
							 romimage, pci);
	romsize = efi_table_attr(efi_pci_io_protocol, romsize, pci);
	if (!romimage || !romsize || romsize > SZ_16M)
		return EFI_INVALID_PARAMETER;

	size = romsize + sizeof(*rom);

	status = efi_call_early(allocate_pool, EFI_LOADER_DATA, size, &rom);
	if (status != EFI_SUCCESS) {
		efi_printk(sys_table, "Failed to alloc mem for rom\n");
		return status;
	}

	memset(rom, 0, sizeof(*rom));

	rom->data.type = SETUP_PCI;
	rom->data.len = size - sizeof(struct setup_data);
	rom->data.next = 0;
	rom->pcilen = pci->romsize;
	*__rom = rom;

	status = efi_call_proto(efi_pci_io_protocol, pci.read, pci,
				EfiPciIoWidthUint16, PCI_VENDOR_ID, 1,
				&rom->vendor);

	if (status != EFI_SUCCESS) {
		efi_printk(sys_table, "Failed to read rom->vendor\n");
		goto free_struct;
	}

	status = efi_call_proto(efi_pci_io_protocol, pci.read, pci,
				EfiPciIoWidthUint16, PCI_DEVICE_ID, 1,
				&rom->devid);

	if (status != EFI_SUCCESS) {
		efi_printk(sys_table, "Failed to read rom->devid\n");
		goto free_struct;
	}

	status = efi_call_proto(efi_pci_io_protocol, get_location, pci,
				&rom->segment, &rom->bus, &rom->device,
				&rom->function);

	if (status != EFI_SUCCESS)
		goto free_struct;

	memcpy(rom->romdata, romimage, romsize);
	return status;

free_struct:
	efi_call_early(free_pool, rom);
	return status;
}

static void
setup_efi_pci32(struct boot_params *params, void **pci_handle,
		unsigned long size)
{
	efi_pci_io_protocol_t *pci = NULL;
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

		status = __setup_efi_pci(pci, &rom);
		if (status != EFI_SUCCESS)
			continue;

		if (data)
			data->next = (unsigned long)rom;
		else
			params->hdr.setup_data = (unsigned long)rom;

		data = (struct setup_data *)rom;

	}
}

static void
setup_efi_pci64(struct boot_params *params, void **pci_handle,
		unsigned long size)
{
	efi_pci_io_protocol_t *pci = NULL;
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

		status = __setup_efi_pci(pci, &rom);
		if (status != EFI_SUCCESS)
			continue;

		if (data)
			data->next = (unsigned long)rom;
		else
			params->hdr.setup_data = (unsigned long)rom;

		data = (struct setup_data *)rom;

	}
}

/*
 * There's no way to return an informative status from this function,
 * because any analysis (and printing of error messages) needs to be
 * done directly at the EFI function call-site.
 *
 * For example, EFI_INVALID_PARAMETER could indicate a bug or maybe we
 * just didn't find any PCI devices, but there's no way to tell outside
 * the context of the call.
 */
static void setup_efi_pci(struct boot_params *params)
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

		if (status != EFI_SUCCESS) {
			efi_printk(sys_table, "Failed to alloc mem for pci_handle\n");
			return;
		}

		status = efi_call_early(locate_handle,
					EFI_LOCATE_BY_PROTOCOL, &pci_proto,
					NULL, &size, pci_handle);
	}

	if (status != EFI_SUCCESS)
		goto free_handle;

	if (efi_early->is64)
		setup_efi_pci64(params, pci_handle, size);
	else
		setup_efi_pci32(params, pci_handle, size);

free_handle:
	efi_call_early(free_pool, pci_handle);
}

static void retrieve_apple_device_properties(struct boot_params *boot_params)
{
	efi_guid_t guid = APPLE_PROPERTIES_PROTOCOL_GUID;
	struct setup_data *data, *new;
	efi_status_t status;
	u32 size = 0;
	void *p;

	status = efi_call_early(locate_protocol, &guid, NULL, &p);
	if (status != EFI_SUCCESS)
		return;

	if (efi_table_attr(apple_properties_protocol, version, p) != 0x10000) {
		efi_printk(sys_table, "Unsupported properties proto version\n");
		return;
	}

	efi_call_proto(apple_properties_protocol, get_all, p, NULL, &size);
	if (!size)
		return;

	do {
		status = efi_call_early(allocate_pool, EFI_LOADER_DATA,
					size + sizeof(struct setup_data), &new);
		if (status != EFI_SUCCESS) {
			efi_printk(sys_table,
					"Failed to alloc mem for properties\n");
			return;
		}

		status = efi_call_proto(apple_properties_protocol, get_all, p,
					new->data, &size);

		if (status == EFI_BUFFER_TOO_SMALL)
			efi_call_early(free_pool, new);
	} while (status == EFI_BUFFER_TOO_SMALL);

	new->type = SETUP_APPLE_PROPERTIES;
	new->len  = size;
	new->next = 0;

	data = (struct setup_data *)(unsigned long)boot_params->hdr.setup_data;
	if (!data)
		boot_params->hdr.setup_data = (unsigned long)new;
	else {
		while (data->next)
			data = (struct setup_data *)(unsigned long)data->next;
		data->next = (unsigned long)new;
	}
}

static const efi_char16_t apple[] = L"Apple";

static void setup_quirks(struct boot_params *boot_params)
{
	efi_char16_t *fw_vendor = (efi_char16_t *)(unsigned long)
		efi_table_attr(efi_system_table, fw_vendor, sys_table);

	if (!memcmp(fw_vendor, apple, sizeof(apple))) {
		if (IS_ENABLED(CONFIG_APPLE_PROPERTIES))
			retrieve_apple_device_properties(boot_params);
	}
}

static efi_status_t
setup_uga32(void **uga_handle, unsigned long size, u32 *width, u32 *height)
{
	struct efi_uga_draw_protocol *uga = NULL, *first_uga;
	efi_guid_t uga_proto = EFI_UGA_PROTOCOL_GUID;
	unsigned long nr_ugas;
	u32 *handles = (u32 *)uga_handle;
	efi_status_t status = EFI_INVALID_PARAMETER;
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
	u64 *handles = (u64 *)uga_handle;
	efi_status_t status = EFI_INVALID_PARAMETER;
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
		status = efi_setup_gop(NULL, si, &graphics_proto, size);

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
	struct apm_bios_info *bi;
	struct setup_header *hdr;
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
	bi = &boot_params->apm_bios_info;

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
	/* Fill in upper bits of command line address, NOP on 32 bit  */
	boot_params->ext_cmd_line_ptr = (u64)(unsigned long)cmdline_ptr >> 32;

	hdr->ramdisk_image = 0;
	hdr->ramdisk_size = 0;

	/* Clear APM BIOS info */
	memset(bi, 0, sizeof(*bi));

	status = efi_parse_options(cmdline_ptr);
	if (status != EFI_SUCCESS)
		goto fail2;

	status = handle_cmdline_files(sys_table, image,
				      (char *)(unsigned long)hdr->cmd_line_ptr,
				      "initrd=", hdr->initrd_addr_max,
				      &ramdisk_addr, &ramdisk_size);

	if (status != EFI_SUCCESS &&
	    hdr->xloadflags & XLF_CAN_BE_LOADED_ABOVE_4G) {
		efi_printk(sys_table, "Trying to load files to higher address\n");
		status = handle_cmdline_files(sys_table, image,
				      (char *)(unsigned long)hdr->cmd_line_ptr,
				      "initrd=", -1UL,
				      &ramdisk_addr, &ramdisk_size);
	}

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
	e820ext->len = nr_entries * sizeof(struct boot_e820_entry);
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
	struct boot_e820_entry *entry = params->e820_table;
	struct efi_info *efi = &params->efi_info;
	struct boot_e820_entry *prev = NULL;
	u32 nr_entries;
	u32 nr_desc;
	int i;

	nr_entries = 0;
	nr_desc = efi->efi_memmap_size / efi->efi_memdesc_size;

	for (i = 0; i < nr_desc; i++) {
		efi_memory_desc_t *d;
		unsigned int e820_type = 0;
		unsigned long m = efi->efi_memmap;

#ifdef CONFIG_X86_64
		m |= (u64)efi->efi_memmap_hi << 32;
#endif

		d = efi_early_memdesc_ptr(m, efi->efi_memdesc_size, i);
		switch (d->type) {
		case EFI_RESERVED_TYPE:
		case EFI_RUNTIME_SERVICES_CODE:
		case EFI_RUNTIME_SERVICES_DATA:
		case EFI_MEMORY_MAPPED_IO:
		case EFI_MEMORY_MAPPED_IO_PORT_SPACE:
		case EFI_PAL_CODE:
			e820_type = E820_TYPE_RESERVED;
			break;

		case EFI_UNUSABLE_MEMORY:
			e820_type = E820_TYPE_UNUSABLE;
			break;

		case EFI_ACPI_RECLAIM_MEMORY:
			e820_type = E820_TYPE_ACPI;
			break;

		case EFI_LOADER_CODE:
		case EFI_LOADER_DATA:
		case EFI_BOOT_SERVICES_CODE:
		case EFI_BOOT_SERVICES_DATA:
		case EFI_CONVENTIONAL_MEMORY:
			e820_type = E820_TYPE_RAM;
			break;

		case EFI_ACPI_MEMORY_NVS:
			e820_type = E820_TYPE_NVS;
			break;

		case EFI_PERSISTENT_MEMORY:
			e820_type = E820_TYPE_PMEM;
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

		if (nr_entries == ARRAY_SIZE(params->e820_table)) {
			u32 need = (nr_desc - i) * sizeof(struct e820_entry) +
				   sizeof(struct setup_data);

			if (!e820ext || e820ext_size < need)
				return EFI_BUFFER_TOO_SMALL;

			/* boot_params map full, switch to e820 extended */
			entry = (struct boot_e820_entry *)e820ext->data;
		}

		entry->addr = d->phys_addr;
		entry->size = d->num_pages << PAGE_SHIFT;
		entry->type = e820_type;
		prev = entry++;
		nr_entries++;
	}

	if (nr_entries > ARRAY_SIZE(params->e820_table)) {
		u32 nr_e820ext = nr_entries - ARRAY_SIZE(params->e820_table);

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
		sizeof(struct e820_entry) * nr_desc;

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

struct exit_boot_struct {
	struct boot_params *boot_params;
	struct efi_info *efi;
	struct setup_data *e820ext;
	__u32 e820ext_size;
	bool is64;
};

static efi_status_t exit_boot_func(efi_system_table_t *sys_table_arg,
				   struct efi_boot_memmap *map,
				   void *priv)
{
	static bool first = true;
	const char *signature;
	__u32 nr_desc;
	efi_status_t status;
	struct exit_boot_struct *p = priv;

	if (first) {
		nr_desc = *map->buff_size / *map->desc_size;
		if (nr_desc > ARRAY_SIZE(p->boot_params->e820_table)) {
			u32 nr_e820ext = nr_desc -
					ARRAY_SIZE(p->boot_params->e820_table);

			status = alloc_e820ext(nr_e820ext, &p->e820ext,
					       &p->e820ext_size);
			if (status != EFI_SUCCESS)
				return status;
		}
		first = false;
	}

	signature = p->is64 ? EFI64_LOADER_SIGNATURE : EFI32_LOADER_SIGNATURE;
	memcpy(&p->efi->efi_loader_signature, signature, sizeof(__u32));

	p->efi->efi_systab = (unsigned long)sys_table_arg;
	p->efi->efi_memdesc_size = *map->desc_size;
	p->efi->efi_memdesc_version = *map->desc_ver;
	p->efi->efi_memmap = (unsigned long)*map->map;
	p->efi->efi_memmap_size = *map->map_size;

#ifdef CONFIG_X86_64
	p->efi->efi_systab_hi = (unsigned long)sys_table_arg >> 32;
	p->efi->efi_memmap_hi = (unsigned long)*map->map >> 32;
#endif

	return EFI_SUCCESS;
}

static efi_status_t exit_boot(struct boot_params *boot_params,
			      void *handle, bool is64)
{
	unsigned long map_sz, key, desc_size, buff_size;
	efi_memory_desc_t *mem_map;
	struct setup_data *e820ext;
	__u32 e820ext_size;
	efi_status_t status;
	__u32 desc_version;
	struct efi_boot_memmap map;
	struct exit_boot_struct priv;

	map.map =		&mem_map;
	map.map_size =		&map_sz;
	map.desc_size =		&desc_size;
	map.desc_ver =		&desc_version;
	map.key_ptr =		&key;
	map.buff_size =		&buff_size;
	priv.boot_params =	boot_params;
	priv.efi =		&boot_params->efi_info;
	priv.e820ext =		NULL;
	priv.e820ext_size =	0;
	priv.is64 =		is64;

	/* Might as well exit boot services now */
	status = efi_exit_boot_services(sys_table, handle, &map, &priv,
					exit_boot_func);
	if (status != EFI_SUCCESS)
		return status;

	e820ext = priv.e820ext;
	e820ext_size = priv.e820ext_size;
	/* Historic? */
	boot_params->alt_mem_k = 32 * 1024;

	status = setup_e820(boot_params, e820ext, e820ext_size);
	if (status != EFI_SUCCESS)
		return status;

	return EFI_SUCCESS;
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

	/*
	 * If the boot loader gave us a value for secure_boot then we use that,
	 * otherwise we ask the BIOS.
	 */
	if (boot_params->secure_boot == efi_secureboot_mode_unset)
		boot_params->secure_boot = efi_get_secureboot(sys_table);

	/* Ask the firmware to clear memory on unclean shutdown */
	efi_enable_reset_attack_mitigation(sys_table);
	efi_retrieve_tpm2_eventlog(sys_table);

	setup_graphics(boot_params);

	setup_efi_pci(boot_params);

	setup_quirks(boot_params);

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
		if (status != EFI_SUCCESS) {
			efi_printk(sys_table, "efi_relocate_kernel() failed!\n");
			goto fail;
		}

		hdr->pref_address = hdr->code32_start;
		hdr->code32_start = bzimage_addr;
	}

	status = exit_boot(boot_params, handle, is64);
	if (status != EFI_SUCCESS) {
		efi_printk(sys_table, "exit_boot() failed!\n");
		goto fail;
	}

	memset((char *)gdt->address, 0x0, gdt->size);
	desc = (struct desc_struct *)gdt->address;

	/* The first GDT is a dummy. */
	desc++;

	if (IS_ENABLED(CONFIG_X86_64)) {
		/* __KERNEL32_CS */
		desc->limit0 = 0xffff;
		desc->base0 = 0x0000;
		desc->base1 = 0x0000;
		desc->type = SEG_TYPE_CODE | SEG_TYPE_EXEC_READ;
		desc->s = DESC_TYPE_CODE_DATA;
		desc->dpl = 0;
		desc->p = 1;
		desc->limit1 = 0xf;
		desc->avl = 0;
		desc->l = 0;
		desc->d = SEG_OP_SIZE_32BIT;
		desc->g = SEG_GRANULARITY_4KB;
		desc->base2 = 0x00;
		desc++;
	} else {
		/* Second entry is unused on 32-bit */
		desc++;
	}

	/* __KERNEL_CS */
	desc->limit0 = 0xffff;
	desc->base0 = 0x0000;
	desc->base1 = 0x0000;
	desc->type = SEG_TYPE_CODE | SEG_TYPE_EXEC_READ;
	desc->s = DESC_TYPE_CODE_DATA;
	desc->dpl = 0;
	desc->p = 1;
	desc->limit1 = 0xf;
	desc->avl = 0;
	if (IS_ENABLED(CONFIG_X86_64)) {
		desc->l = 1;
		desc->d = 0;
	} else {
		desc->l = 0;
		desc->d = SEG_OP_SIZE_32BIT;
	}
	desc->g = SEG_GRANULARITY_4KB;
	desc->base2 = 0x00;
	desc++;

	/* __KERNEL_DS */
	desc->limit0 = 0xffff;
	desc->base0 = 0x0000;
	desc->base1 = 0x0000;
	desc->type = SEG_TYPE_DATA | SEG_TYPE_READ_WRITE;
	desc->s = DESC_TYPE_CODE_DATA;
	desc->dpl = 0;
	desc->p = 1;
	desc->limit1 = 0xf;
	desc->avl = 0;
	desc->l = 0;
	desc->d = SEG_OP_SIZE_32BIT;
	desc->g = SEG_GRANULARITY_4KB;
	desc->base2 = 0x00;
	desc++;

	if (IS_ENABLED(CONFIG_X86_64)) {
		/* Task segment value */
		desc->limit0 = 0x0000;
		desc->base0 = 0x0000;
		desc->base1 = 0x0000;
		desc->type = SEG_TYPE_TSS;
		desc->s = 0;
		desc->dpl = 0;
		desc->p = 1;
		desc->limit1 = 0x0;
		desc->avl = 0;
		desc->l = 0;
		desc->d = 0;
		desc->g = SEG_GRANULARITY_4KB;
		desc->base2 = 0x00;
		desc++;
	}

	asm volatile("cli");
	asm volatile ("lgdt %0" : : "m" (*gdt));

	return boot_params;
fail:
	efi_printk(sys_table, "efi_main() failed!\n");
	return NULL;
}
