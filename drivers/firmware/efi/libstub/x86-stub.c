// SPDX-License-Identifier: GPL-2.0-only

/* -----------------------------------------------------------------------
 *
 *   Copyright 2011 Intel Corporation; author Matt Fleming
 *
 * ----------------------------------------------------------------------- */

#include <linux/efi.h>
#include <linux/pci.h>
#include <linux/stddef.h>

#include <asm/efi.h>
#include <asm/e820/types.h>
#include <asm/setup.h>
#include <asm/desc.h>
#include <asm/boot.h>

#include "efistub.h"

/* Maximum physical address for 64-bit kernel with 4-level paging */
#define MAXMEM_X86_64_4LEVEL (1ull << 46)

const efi_system_table_t *efi_system_table;
const efi_dxe_services_table_t *efi_dxe_table;
extern u32 image_offset;
static efi_loaded_image_t *image = NULL;

static efi_status_t
preserve_pci_rom_image(efi_pci_io_protocol_t *pci, struct pci_setup_rom **__rom)
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
	romimage = efi_table_attr(pci, romimage);
	romsize = efi_table_attr(pci, romsize);
	if (!romimage || !romsize || romsize > SZ_16M)
		return EFI_INVALID_PARAMETER;

	size = romsize + sizeof(*rom);

	status = efi_bs_call(allocate_pool, EFI_LOADER_DATA, size,
			     (void **)&rom);
	if (status != EFI_SUCCESS) {
		efi_err("Failed to allocate memory for 'rom'\n");
		return status;
	}

	memset(rom, 0, sizeof(*rom));

	rom->data.type	= SETUP_PCI;
	rom->data.len	= size - sizeof(struct setup_data);
	rom->data.next	= 0;
	rom->pcilen	= pci->romsize;
	*__rom = rom;

	status = efi_call_proto(pci, pci.read, EfiPciIoWidthUint16,
				PCI_VENDOR_ID, 1, &rom->vendor);

	if (status != EFI_SUCCESS) {
		efi_err("Failed to read rom->vendor\n");
		goto free_struct;
	}

	status = efi_call_proto(pci, pci.read, EfiPciIoWidthUint16,
				PCI_DEVICE_ID, 1, &rom->devid);

	if (status != EFI_SUCCESS) {
		efi_err("Failed to read rom->devid\n");
		goto free_struct;
	}

	status = efi_call_proto(pci, get_location, &rom->segment, &rom->bus,
				&rom->device, &rom->function);

	if (status != EFI_SUCCESS)
		goto free_struct;

	memcpy(rom->romdata, romimage, romsize);
	return status;

free_struct:
	efi_bs_call(free_pool, rom);
	return status;
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
	struct setup_data *data;
	efi_handle_t h;
	int i;

	status = efi_bs_call(locate_handle, EFI_LOCATE_BY_PROTOCOL,
			     &pci_proto, NULL, &size, pci_handle);

	if (status == EFI_BUFFER_TOO_SMALL) {
		status = efi_bs_call(allocate_pool, EFI_LOADER_DATA, size,
				     (void **)&pci_handle);

		if (status != EFI_SUCCESS) {
			efi_err("Failed to allocate memory for 'pci_handle'\n");
			return;
		}

		status = efi_bs_call(locate_handle, EFI_LOCATE_BY_PROTOCOL,
				     &pci_proto, NULL, &size, pci_handle);
	}

	if (status != EFI_SUCCESS)
		goto free_handle;

	data = (struct setup_data *)(unsigned long)params->hdr.setup_data;

	while (data && data->next)
		data = (struct setup_data *)(unsigned long)data->next;

	for_each_efi_handle(h, pci_handle, size, i) {
		efi_pci_io_protocol_t *pci = NULL;
		struct pci_setup_rom *rom;

		status = efi_bs_call(handle_protocol, h, &pci_proto,
				     (void **)&pci);
		if (status != EFI_SUCCESS || !pci)
			continue;

		status = preserve_pci_rom_image(pci, &rom);
		if (status != EFI_SUCCESS)
			continue;

		if (data)
			data->next = (unsigned long)rom;
		else
			params->hdr.setup_data = (unsigned long)rom;

		data = (struct setup_data *)rom;
	}

free_handle:
	efi_bs_call(free_pool, pci_handle);
}

static void retrieve_apple_device_properties(struct boot_params *boot_params)
{
	efi_guid_t guid = APPLE_PROPERTIES_PROTOCOL_GUID;
	struct setup_data *data, *new;
	efi_status_t status;
	u32 size = 0;
	apple_properties_protocol_t *p;

	status = efi_bs_call(locate_protocol, &guid, NULL, (void **)&p);
	if (status != EFI_SUCCESS)
		return;

	if (efi_table_attr(p, version) != 0x10000) {
		efi_err("Unsupported properties proto version\n");
		return;
	}

	efi_call_proto(p, get_all, NULL, &size);
	if (!size)
		return;

	do {
		status = efi_bs_call(allocate_pool, EFI_LOADER_DATA,
				     size + sizeof(struct setup_data),
				     (void **)&new);
		if (status != EFI_SUCCESS) {
			efi_err("Failed to allocate memory for 'properties'\n");
			return;
		}

		status = efi_call_proto(p, get_all, new->data, &size);

		if (status == EFI_BUFFER_TOO_SMALL)
			efi_bs_call(free_pool, new);
	} while (status == EFI_BUFFER_TOO_SMALL);

	new->type = SETUP_APPLE_PROPERTIES;
	new->len  = size;
	new->next = 0;

	data = (struct setup_data *)(unsigned long)boot_params->hdr.setup_data;
	if (!data) {
		boot_params->hdr.setup_data = (unsigned long)new;
	} else {
		while (data->next)
			data = (struct setup_data *)(unsigned long)data->next;
		data->next = (unsigned long)new;
	}
}

static void
adjust_memory_range_protection(unsigned long start, unsigned long size)
{
	efi_status_t status;
	efi_gcd_memory_space_desc_t desc;
	unsigned long end, next;
	unsigned long rounded_start, rounded_end;
	unsigned long unprotect_start, unprotect_size;

	if (efi_dxe_table == NULL)
		return;

	rounded_start = rounddown(start, EFI_PAGE_SIZE);
	rounded_end = roundup(start + size, EFI_PAGE_SIZE);

	/*
	 * Don't modify memory region attributes, they are
	 * already suitable, to lower the possibility to
	 * encounter firmware bugs.
	 */

	for (end = start + size; start < end; start = next) {

		status = efi_dxe_call(get_memory_space_descriptor, start, &desc);

		if (status != EFI_SUCCESS)
			return;

		next = desc.base_address + desc.length;

		/*
		 * Only system memory is suitable for trampoline/kernel image placement,
		 * so only this type of memory needs its attributes to be modified.
		 */

		if (desc.gcd_memory_type != EfiGcdMemoryTypeSystemMemory ||
		    (desc.attributes & (EFI_MEMORY_RO | EFI_MEMORY_XP)) == 0)
			continue;

		unprotect_start = max(rounded_start, (unsigned long)desc.base_address);
		unprotect_size = min(rounded_end, next) - unprotect_start;

		status = efi_dxe_call(set_memory_space_attributes,
				      unprotect_start, unprotect_size,
				      EFI_MEMORY_WB);

		if (status != EFI_SUCCESS) {
			efi_warn("Unable to unprotect memory range [%08lx,%08lx]: %lx\n",
				 unprotect_start,
				 unprotect_start + unprotect_size,
				 status);
		}
	}
}

/*
 * Trampoline takes 2 pages and can be loaded in first megabyte of memory
 * with its end placed between 128k and 640k where BIOS might start.
 * (see arch/x86/boot/compressed/pgtable_64.c)
 *
 * We cannot find exact trampoline placement since memory map
 * can be modified by UEFI, and it can alter the computed address.
 */

#define TRAMPOLINE_PLACEMENT_BASE ((128 - 8)*1024)
#define TRAMPOLINE_PLACEMENT_SIZE (640*1024 - (128 - 8)*1024)

void startup_32(struct boot_params *boot_params);

static void
setup_memory_protection(unsigned long image_base, unsigned long image_size)
{
	/*
	 * Allow execution of possible trampoline used
	 * for switching between 4- and 5-level page tables
	 * and relocated kernel image.
	 */

	adjust_memory_range_protection(TRAMPOLINE_PLACEMENT_BASE,
				       TRAMPOLINE_PLACEMENT_SIZE);

#ifdef CONFIG_64BIT
	if (image_base != (unsigned long)startup_32)
		adjust_memory_range_protection(image_base, image_size);
#else
	/*
	 * Clear protection flags on a whole range of possible
	 * addresses used for KASLR. We don't need to do that
	 * on x86_64, since KASLR/extraction is performed after
	 * dedicated identity page tables are built and we only
	 * need to remove possible protection on relocated image
	 * itself disregarding further relocations.
	 */
	adjust_memory_range_protection(LOAD_PHYSICAL_ADDR,
				       KERNEL_IMAGE_SIZE - LOAD_PHYSICAL_ADDR);
#endif
}

static const efi_char16_t apple[] = L"Apple";

static void setup_quirks(struct boot_params *boot_params,
			 unsigned long image_base,
			 unsigned long image_size)
{
	efi_char16_t *fw_vendor = (efi_char16_t *)(unsigned long)
		efi_table_attr(efi_system_table, fw_vendor);

	if (!memcmp(fw_vendor, apple, sizeof(apple))) {
		if (IS_ENABLED(CONFIG_APPLE_PROPERTIES))
			retrieve_apple_device_properties(boot_params);
	}

	if (IS_ENABLED(CONFIG_EFI_DXE_MEM_ATTRIBUTES))
		setup_memory_protection(image_base, image_size);
}

/*
 * See if we have Universal Graphics Adapter (UGA) protocol
 */
static efi_status_t
setup_uga(struct screen_info *si, efi_guid_t *uga_proto, unsigned long size)
{
	efi_status_t status;
	u32 width, height;
	void **uga_handle = NULL;
	efi_uga_draw_protocol_t *uga = NULL, *first_uga;
	efi_handle_t handle;
	int i;

	status = efi_bs_call(allocate_pool, EFI_LOADER_DATA, size,
			     (void **)&uga_handle);
	if (status != EFI_SUCCESS)
		return status;

	status = efi_bs_call(locate_handle, EFI_LOCATE_BY_PROTOCOL,
			     uga_proto, NULL, &size, uga_handle);
	if (status != EFI_SUCCESS)
		goto free_handle;

	height = 0;
	width = 0;

	first_uga = NULL;
	for_each_efi_handle(handle, uga_handle, size, i) {
		efi_guid_t pciio_proto = EFI_PCI_IO_PROTOCOL_GUID;
		u32 w, h, depth, refresh;
		void *pciio;

		status = efi_bs_call(handle_protocol, handle, uga_proto,
				     (void **)&uga);
		if (status != EFI_SUCCESS)
			continue;

		pciio = NULL;
		efi_bs_call(handle_protocol, handle, &pciio_proto, &pciio);

		status = efi_call_proto(uga, get_mode, &w, &h, &depth, &refresh);
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

	if (!width && !height)
		goto free_handle;

	/* EFI framebuffer */
	si->orig_video_isVGA	= VIDEO_TYPE_EFI;

	si->lfb_depth		= 32;
	si->lfb_width		= width;
	si->lfb_height		= height;

	si->red_size		= 8;
	si->red_pos		= 16;
	si->green_size		= 8;
	si->green_pos		= 8;
	si->blue_size		= 8;
	si->blue_pos		= 0;
	si->rsvd_size		= 8;
	si->rsvd_pos		= 24;

free_handle:
	efi_bs_call(free_pool, uga_handle);

	return status;
}

static void setup_graphics(struct boot_params *boot_params)
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
	status = efi_bs_call(locate_handle, EFI_LOCATE_BY_PROTOCOL,
			     &graphics_proto, NULL, &size, gop_handle);
	if (status == EFI_BUFFER_TOO_SMALL)
		status = efi_setup_gop(si, &graphics_proto, size);

	if (status != EFI_SUCCESS) {
		size = 0;
		status = efi_bs_call(locate_handle, EFI_LOCATE_BY_PROTOCOL,
				     &uga_proto, NULL, &size, uga_handle);
		if (status == EFI_BUFFER_TOO_SMALL)
			setup_uga(si, &uga_proto, size);
	}
}


static void __noreturn efi_exit(efi_handle_t handle, efi_status_t status)
{
	efi_bs_call(exit, handle, status, 0, NULL);
	for(;;)
		asm("hlt");
}

void __noreturn efi_stub_entry(efi_handle_t handle,
			       efi_system_table_t *sys_table_arg,
			       struct boot_params *boot_params);

/*
 * Because the x86 boot code expects to be passed a boot_params we
 * need to create one ourselves (usually the bootloader would create
 * one for us).
 */
efi_status_t __efiapi efi_pe_entry(efi_handle_t handle,
				   efi_system_table_t *sys_table_arg)
{
	struct boot_params *boot_params;
	struct setup_header *hdr;
	void *image_base;
	efi_guid_t proto = LOADED_IMAGE_PROTOCOL_GUID;
	int options_size = 0;
	efi_status_t status;
	char *cmdline_ptr;

	efi_system_table = sys_table_arg;

	/* Check if we were booted by the EFI firmware */
	if (efi_system_table->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE)
		efi_exit(handle, EFI_INVALID_PARAMETER);

	status = efi_bs_call(handle_protocol, handle, &proto, (void **)&image);
	if (status != EFI_SUCCESS) {
		efi_err("Failed to get handle for LOADED_IMAGE_PROTOCOL\n");
		efi_exit(handle, status);
	}

	image_base = efi_table_attr(image, image_base);
	image_offset = (void *)startup_32 - image_base;

	status = efi_allocate_pages(sizeof(struct boot_params),
				    (unsigned long *)&boot_params, ULONG_MAX);
	if (status != EFI_SUCCESS) {
		efi_err("Failed to allocate lowmem for boot params\n");
		efi_exit(handle, status);
	}

	memset(boot_params, 0x0, sizeof(struct boot_params));

	hdr = &boot_params->hdr;

	/* Copy the setup header from the second sector to boot_params */
	memcpy(&hdr->jump, image_base + 512,
	       sizeof(struct setup_header) - offsetof(struct setup_header, jump));

	/*
	 * Fill out some of the header fields ourselves because the
	 * EFI firmware loader doesn't load the first sector.
	 */
	hdr->root_flags	= 1;
	hdr->vid_mode	= 0xffff;
	hdr->boot_flag	= 0xAA55;

	hdr->type_of_loader = 0x21;

	/* Convert unicode cmdline to ascii */
	cmdline_ptr = efi_convert_cmdline(image, &options_size);
	if (!cmdline_ptr)
		goto fail;

	efi_set_u64_split((unsigned long)cmdline_ptr,
			  &hdr->cmd_line_ptr, &boot_params->ext_cmd_line_ptr);

	hdr->ramdisk_image = 0;
	hdr->ramdisk_size = 0;

	efi_stub_entry(handle, sys_table_arg, boot_params);
	/* not reached */

fail:
	efi_free(sizeof(struct boot_params), (unsigned long)boot_params);

	efi_exit(handle, status);
}

static void add_e820ext(struct boot_params *params,
			struct setup_data *e820ext, u32 nr_entries)
{
	struct setup_data *data;

	e820ext->type = SETUP_E820_EXT;
	e820ext->len  = nr_entries * sizeof(struct boot_e820_entry);
	e820ext->next = 0;

	data = (struct setup_data *)(unsigned long)params->hdr.setup_data;

	while (data && data->next)
		data = (struct setup_data *)(unsigned long)data->next;

	if (data)
		data->next = (unsigned long)e820ext;
	else
		params->hdr.setup_data = (unsigned long)e820ext;
}

static efi_status_t
setup_e820(struct boot_params *params, struct setup_data *e820ext, u32 e820ext_size)
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
			if (efi_soft_reserve_enabled() &&
			    (d->attribute & EFI_MEMORY_SP))
				e820_type = E820_TYPE_SOFT_RESERVED;
			else
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
		efi_bs_call(free_pool, *e820ext);
		*e820ext = NULL;
		*e820ext_size = 0;
	}

	status = efi_bs_call(allocate_pool, EFI_LOADER_DATA, size,
			     (void **)e820ext);
	if (status == EFI_SUCCESS)
		*e820ext_size = size;

	return status;
}

static efi_status_t allocate_e820(struct boot_params *params,
				  struct setup_data **e820ext,
				  u32 *e820ext_size)
{
	unsigned long map_size, desc_size, map_key;
	efi_status_t status;
	__u32 nr_desc, desc_version;

	/* Only need the size of the mem map and size of each mem descriptor */
	map_size = 0;
	status = efi_bs_call(get_memory_map, &map_size, NULL, &map_key,
			     &desc_size, &desc_version);
	if (status != EFI_BUFFER_TOO_SMALL)
		return (status != EFI_SUCCESS) ? status : EFI_UNSUPPORTED;

	nr_desc = map_size / desc_size + EFI_MMAP_NR_SLACK_SLOTS;

	if (nr_desc > ARRAY_SIZE(params->e820_table)) {
		u32 nr_e820ext = nr_desc - ARRAY_SIZE(params->e820_table);

		status = alloc_e820ext(nr_e820ext, e820ext, e820ext_size);
		if (status != EFI_SUCCESS)
			return status;
	}

	return EFI_SUCCESS;
}

struct exit_boot_struct {
	struct boot_params	*boot_params;
	struct efi_info		*efi;
};

static efi_status_t exit_boot_func(struct efi_boot_memmap *map,
				   void *priv)
{
	const char *signature;
	struct exit_boot_struct *p = priv;

	signature = efi_is_64bit() ? EFI64_LOADER_SIGNATURE
				   : EFI32_LOADER_SIGNATURE;
	memcpy(&p->efi->efi_loader_signature, signature, sizeof(__u32));

	efi_set_u64_split((unsigned long)efi_system_table,
			  &p->efi->efi_systab, &p->efi->efi_systab_hi);
	p->efi->efi_memdesc_size	= *map->desc_size;
	p->efi->efi_memdesc_version	= *map->desc_ver;
	efi_set_u64_split((unsigned long)*map->map,
			  &p->efi->efi_memmap, &p->efi->efi_memmap_hi);
	p->efi->efi_memmap_size		= *map->map_size;

	return EFI_SUCCESS;
}

static efi_status_t exit_boot(struct boot_params *boot_params, void *handle)
{
	unsigned long map_sz, key, desc_size, buff_size;
	efi_memory_desc_t *mem_map;
	struct setup_data *e820ext = NULL;
	__u32 e820ext_size = 0;
	efi_status_t status;
	__u32 desc_version;
	struct efi_boot_memmap map;
	struct exit_boot_struct priv;

	map.map			= &mem_map;
	map.map_size		= &map_sz;
	map.desc_size		= &desc_size;
	map.desc_ver		= &desc_version;
	map.key_ptr		= &key;
	map.buff_size		= &buff_size;
	priv.boot_params	= boot_params;
	priv.efi		= &boot_params->efi_info;

	status = allocate_e820(boot_params, &e820ext, &e820ext_size);
	if (status != EFI_SUCCESS)
		return status;

	/* Might as well exit boot services now */
	status = efi_exit_boot_services(handle, &map, &priv, exit_boot_func);
	if (status != EFI_SUCCESS)
		return status;

	/* Historic? */
	boot_params->alt_mem_k	= 32 * 1024;

	status = setup_e820(boot_params, e820ext, e820ext_size);
	if (status != EFI_SUCCESS)
		return status;

	return EFI_SUCCESS;
}

/*
 * On success, we return the address of startup_32, which has potentially been
 * relocated by efi_relocate_kernel.
 * On failure, we exit to the firmware via efi_exit instead of returning.
 */
unsigned long efi_main(efi_handle_t handle,
			     efi_system_table_t *sys_table_arg,
			     struct boot_params *boot_params)
{
	unsigned long bzimage_addr = (unsigned long)startup_32;
	unsigned long buffer_start, buffer_end;
	struct setup_header *hdr = &boot_params->hdr;
	unsigned long addr, size;
	efi_status_t status;

	efi_system_table = sys_table_arg;
	/* Check if we were booted by the EFI firmware */
	if (efi_system_table->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE)
		efi_exit(handle, EFI_INVALID_PARAMETER);

	efi_dxe_table = get_efi_config_table(EFI_DXE_SERVICES_TABLE_GUID);
	if (efi_dxe_table &&
	    efi_dxe_table->hdr.signature != EFI_DXE_SERVICES_TABLE_SIGNATURE) {
		efi_warn("Ignoring DXE services table: invalid signature\n");
		efi_dxe_table = NULL;
	}

	/*
	 * If the kernel isn't already loaded at a suitable address,
	 * relocate it.
	 *
	 * It must be loaded above LOAD_PHYSICAL_ADDR.
	 *
	 * The maximum address for 64-bit is 1 << 46 for 4-level paging. This
	 * is defined as the macro MAXMEM, but unfortunately that is not a
	 * compile-time constant if 5-level paging is configured, so we instead
	 * define our own macro for use here.
	 *
	 * For 32-bit, the maximum address is complicated to figure out, for
	 * now use KERNEL_IMAGE_SIZE, which will be 512MiB, the same as what
	 * KASLR uses.
	 *
	 * Also relocate it if image_offset is zero, i.e. the kernel wasn't
	 * loaded by LoadImage, but rather by a bootloader that called the
	 * handover entry. The reason we must always relocate in this case is
	 * to handle the case of systemd-boot booting a unified kernel image,
	 * which is a PE executable that contains the bzImage and an initrd as
	 * COFF sections. The initrd section is placed after the bzImage
	 * without ensuring that there are at least init_size bytes available
	 * for the bzImage, and thus the compressed kernel's startup code may
	 * overwrite the initrd unless it is moved out of the way.
	 */

	buffer_start = ALIGN(bzimage_addr - image_offset,
			     hdr->kernel_alignment);
	buffer_end = buffer_start + hdr->init_size;

	if ((buffer_start < LOAD_PHYSICAL_ADDR)				     ||
	    (IS_ENABLED(CONFIG_X86_32) && buffer_end > KERNEL_IMAGE_SIZE)    ||
	    (IS_ENABLED(CONFIG_X86_64) && buffer_end > MAXMEM_X86_64_4LEVEL) ||
	    (image_offset == 0)) {
		extern char _bss[];

		status = efi_relocate_kernel(&bzimage_addr,
					     (unsigned long)_bss - bzimage_addr,
					     hdr->init_size,
					     hdr->pref_address,
					     hdr->kernel_alignment,
					     LOAD_PHYSICAL_ADDR);
		if (status != EFI_SUCCESS) {
			efi_err("efi_relocate_kernel() failed!\n");
			goto fail;
		}
		/*
		 * Now that we've copied the kernel elsewhere, we no longer
		 * have a set up block before startup_32(), so reset image_offset
		 * to zero in case it was set earlier.
		 */
		image_offset = 0;
	}

#ifdef CONFIG_CMDLINE_BOOL
	status = efi_parse_options(CONFIG_CMDLINE);
	if (status != EFI_SUCCESS) {
		efi_err("Failed to parse options\n");
		goto fail;
	}
#endif
	if (!IS_ENABLED(CONFIG_CMDLINE_OVERRIDE)) {
		unsigned long cmdline_paddr = ((u64)hdr->cmd_line_ptr |
					       ((u64)boot_params->ext_cmd_line_ptr << 32));
		status = efi_parse_options((char *)cmdline_paddr);
		if (status != EFI_SUCCESS) {
			efi_err("Failed to parse options\n");
			goto fail;
		}
	}

	/*
	 * At this point, an initrd may already have been loaded by the
	 * bootloader and passed via bootparams. We permit an initrd loaded
	 * from the LINUX_EFI_INITRD_MEDIA_GUID device path to supersede it.
	 *
	 * If the device path is not present, any command-line initrd=
	 * arguments will be processed only if image is not NULL, which will be
	 * the case only if we were loaded via the PE entry point.
	 */
	status = efi_load_initrd(image, &addr, &size, hdr->initrd_addr_max,
				 ULONG_MAX);
	if (status != EFI_SUCCESS)
		goto fail;
	if (size > 0) {
		efi_set_u64_split(addr, &hdr->ramdisk_image,
				  &boot_params->ext_ramdisk_image);
		efi_set_u64_split(size, &hdr->ramdisk_size,
				  &boot_params->ext_ramdisk_size);
	}

	/*
	 * If the boot loader gave us a value for secure_boot then we use that,
	 * otherwise we ask the BIOS.
	 */
	if (boot_params->secure_boot == efi_secureboot_mode_unset)
		boot_params->secure_boot = efi_get_secureboot();

	/* Ask the firmware to clear memory on unclean shutdown */
	efi_enable_reset_attack_mitigation();

	efi_random_get_seed();

	efi_retrieve_tpm2_eventlog();

	setup_graphics(boot_params);

	setup_efi_pci(boot_params);

	setup_quirks(boot_params, bzimage_addr, buffer_end - buffer_start);

	status = exit_boot(boot_params, handle);
	if (status != EFI_SUCCESS) {
		efi_err("exit_boot() failed!\n");
		goto fail;
	}

	return bzimage_addr;
fail:
	efi_err("efi_main() failed!\n");

	efi_exit(handle, status);
}
