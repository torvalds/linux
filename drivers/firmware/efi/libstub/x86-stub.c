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
#include <asm/kaslr.h>
#include <asm/sev.h>

#include "efistub.h"
#include "x86-stub.h"

extern char _bss[], _ebss[];

const efi_system_table_t *efi_system_table;
const efi_dxe_services_table_t *efi_dxe_table;
static efi_loaded_image_t *image = NULL;
static efi_memory_attribute_protocol_t *memattr;

typedef union sev_memory_acceptance_protocol sev_memory_acceptance_protocol_t;
union sev_memory_acceptance_protocol {
	struct {
		efi_status_t (__efiapi * allow_unaccepted_memory)(
			sev_memory_acceptance_protocol_t *);
	};
	struct {
		u32 allow_unaccepted_memory;
	} mixed_mode;
};

static efi_status_t
preserve_pci_rom_image(efi_pci_io_protocol_t *pci, struct pci_setup_rom **__rom)
{
	struct pci_setup_rom *rom __free(efi_pool) = NULL;
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
	rom->pcilen	= romsize;

	status = efi_call_proto(pci, pci.read, EfiPciIoWidthUint16,
				PCI_VENDOR_ID, 1, &rom->vendor);

	if (status != EFI_SUCCESS) {
		efi_err("Failed to read rom->vendor\n");
		return status;
	}

	status = efi_call_proto(pci, pci.read, EfiPciIoWidthUint16,
				PCI_DEVICE_ID, 1, &rom->devid);

	if (status != EFI_SUCCESS) {
		efi_err("Failed to read rom->devid\n");
		return status;
	}

	status = efi_call_proto(pci, get_location, &rom->segment, &rom->bus,
				&rom->device, &rom->function);

	if (status != EFI_SUCCESS)
		return status;

	memcpy(rom->romdata, romimage, romsize);
	*__rom = no_free_ptr(rom);
	return EFI_SUCCESS;
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
	efi_handle_t *pci_handle __free(efi_pool) = NULL;
	efi_guid_t pci_proto = EFI_PCI_IO_PROTOCOL_GUID;
	struct setup_data *data;
	unsigned long num;
	efi_handle_t h;

	status = efi_bs_call(locate_handle_buffer, EFI_LOCATE_BY_PROTOCOL,
			     &pci_proto, NULL, &num, &pci_handle);
	if (status != EFI_SUCCESS)
		return;

	data = (struct setup_data *)(unsigned long)params->hdr.setup_data;

	while (data && data->next)
		data = (struct setup_data *)(unsigned long)data->next;

	for_each_efi_handle(h, pci_handle, num) {
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

static bool apple_match_product_name(void)
{
	static const char type1_product_matches[][15] = {
		"MacBookPro11,3",
		"MacBookPro11,5",
		"MacBookPro13,3",
		"MacBookPro14,3",
		"MacBookPro15,1",
		"MacBookPro15,3",
		"MacBookPro16,1",
		"MacBookPro16,4",
	};
	const struct efi_smbios_type1_record *record;
	const u8 *product;

	record = (struct efi_smbios_type1_record *)efi_get_smbios_record(1);
	if (!record)
		return false;

	product = efi_get_smbios_string(record, product_name);
	if (!product)
		return false;

	for (int i = 0; i < ARRAY_SIZE(type1_product_matches); i++) {
		if (!strcmp(product, type1_product_matches[i]))
			return true;
	}

	return false;
}

static void apple_set_os(void)
{
	struct {
		unsigned long version;
		efi_status_t (__efiapi *set_os_version)(const char *);
		efi_status_t (__efiapi *set_os_vendor)(const char *);
	} *set_os;
	efi_status_t status;

	if (!efi_is_64bit() || !apple_match_product_name())
		return;

	status = efi_bs_call(locate_protocol, &APPLE_SET_OS_PROTOCOL_GUID, NULL,
			     (void **)&set_os);
	if (status != EFI_SUCCESS)
		return;

	if (set_os->version >= 2) {
		status = set_os->set_os_vendor("Apple Inc.");
		if (status != EFI_SUCCESS)
			efi_err("Failed to set OS vendor via apple_set_os\n");
	}

	if (set_os->version > 0) {
		/* The version being set doesn't seem to matter */
		status = set_os->set_os_version("Mac OS X 10.9");
		if (status != EFI_SUCCESS)
			efi_err("Failed to set OS version via apple_set_os\n");
	}
}

efi_status_t efi_adjust_memory_range_protection(unsigned long start,
						unsigned long size)
{
	efi_status_t status;
	efi_gcd_memory_space_desc_t desc;
	unsigned long end, next;
	unsigned long rounded_start, rounded_end;
	unsigned long unprotect_start, unprotect_size;

	rounded_start = rounddown(start, EFI_PAGE_SIZE);
	rounded_end = roundup(start + size, EFI_PAGE_SIZE);

	if (memattr != NULL) {
		status = efi_call_proto(memattr, set_memory_attributes,
					rounded_start,
					rounded_end - rounded_start,
					EFI_MEMORY_RO);
		if (status != EFI_SUCCESS) {
			efi_warn("Failed to set EFI_MEMORY_RO attribute\n");
			return status;
		}

		status = efi_call_proto(memattr, clear_memory_attributes,
					rounded_start,
					rounded_end - rounded_start,
					EFI_MEMORY_XP);
		if (status != EFI_SUCCESS)
			efi_warn("Failed to clear EFI_MEMORY_XP attribute\n");
		return status;
	}

	if (efi_dxe_table == NULL)
		return EFI_SUCCESS;

	/*
	 * Don't modify memory region attributes, if they are
	 * already suitable, to lower the possibility to
	 * encounter firmware bugs.
	 */

	for (end = start + size; start < end; start = next) {

		status = efi_dxe_call(get_memory_space_descriptor, start, &desc);

		if (status != EFI_SUCCESS)
			break;

		next = desc.base_address + desc.length;

		/*
		 * Only system memory and more reliable memory are suitable for
		 * trampoline/kernel image placement. So only those memory types
		 * may need to have attributes modified.
		 */

		if ((desc.gcd_memory_type != EfiGcdMemoryTypeSystemMemory &&
		     desc.gcd_memory_type != EfiGcdMemoryTypeMoreReliable) ||
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
			break;
		}
	}
	return EFI_SUCCESS;
}

static void setup_unaccepted_memory(void)
{
	efi_guid_t mem_acceptance_proto = OVMF_SEV_MEMORY_ACCEPTANCE_PROTOCOL_GUID;
	sev_memory_acceptance_protocol_t *proto;
	efi_status_t status;

	if (!IS_ENABLED(CONFIG_UNACCEPTED_MEMORY))
		return;

	/*
	 * Enable unaccepted memory before calling exit boot services in order
	 * for the UEFI to not accept all memory on EBS.
	 */
	status = efi_bs_call(locate_protocol, &mem_acceptance_proto, NULL,
			     (void **)&proto);
	if (status != EFI_SUCCESS)
		return;

	status = efi_call_proto(proto, allow_unaccepted_memory);
	if (status != EFI_SUCCESS)
		efi_err("Memory acceptance protocol failed\n");
}

static efi_char16_t *efistub_fw_vendor(void)
{
	unsigned long vendor = efi_table_attr(efi_system_table, fw_vendor);

	return (efi_char16_t *)vendor;
}

static const efi_char16_t apple[] = L"Apple";

static void setup_quirks(struct boot_params *boot_params)
{
	if (!memcmp(efistub_fw_vendor(), apple, sizeof(apple))) {
		if (IS_ENABLED(CONFIG_APPLE_PROPERTIES))
			retrieve_apple_device_properties(boot_params);

		apple_set_os();
	}
}

static void setup_graphics(struct boot_params *boot_params)
{
	struct screen_info *si = memset(&boot_params->screen_info, 0, sizeof(*si));

	efi_setup_gop(si);
}

static void __noreturn efi_exit(efi_handle_t handle, efi_status_t status)
{
	efi_bs_call(exit, handle, status, 0, NULL);
	for(;;)
		asm("hlt");
}

/*
 * Because the x86 boot code expects to be passed a boot_params we
 * need to create one ourselves (usually the bootloader would create
 * one for us).
 */
static efi_status_t efi_allocate_bootparams(efi_handle_t handle,
					    struct boot_params **bp)
{
	efi_guid_t proto = LOADED_IMAGE_PROTOCOL_GUID;
	struct boot_params *boot_params;
	struct setup_header *hdr;
	efi_status_t status;
	unsigned long alloc;
	char *cmdline_ptr;

	status = efi_bs_call(handle_protocol, handle, &proto, (void **)&image);
	if (status != EFI_SUCCESS) {
		efi_err("Failed to get handle for LOADED_IMAGE_PROTOCOL\n");
		return status;
	}

	status = efi_allocate_pages(PARAM_SIZE, &alloc, ULONG_MAX);
	if (status != EFI_SUCCESS)
		return status;

	boot_params = memset((void *)alloc, 0x0, PARAM_SIZE);
	hdr	    = &boot_params->hdr;

	/* Assign the setup_header fields that the kernel actually cares about */
	hdr->root_flags	= 1;
	hdr->vid_mode	= 0xffff;

	hdr->type_of_loader = 0x21;
	hdr->initrd_addr_max = INT_MAX;

	/* Convert unicode cmdline to ascii */
	cmdline_ptr = efi_convert_cmdline(image);
	if (!cmdline_ptr) {
		efi_free(PARAM_SIZE, alloc);
		return EFI_OUT_OF_RESOURCES;
	}

	efi_set_u64_split((unsigned long)cmdline_ptr, &hdr->cmd_line_ptr,
			  &boot_params->ext_cmd_line_ptr);

	*bp = boot_params;
	return EFI_SUCCESS;
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

		d = efi_memdesc_ptr(m, efi->efi_memdesc_size, i);
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

		case EFI_UNACCEPTED_MEMORY:
			if (!IS_ENABLED(CONFIG_UNACCEPTED_MEMORY))
				continue;
			e820_type = E820_TYPE_RAM;
			process_unaccepted_memory(d->phys_addr,
						  d->phys_addr + PAGE_SIZE * d->num_pages);
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
	struct efi_boot_memmap *map __free(efi_pool) = NULL;
	efi_status_t status;
	__u32 nr_desc;

	status = efi_get_memory_map(&map, false);
	if (status != EFI_SUCCESS)
		return status;

	nr_desc = map->map_size / map->desc_size;
	if (nr_desc > ARRAY_SIZE(params->e820_table) - EFI_MMAP_NR_SLACK_SLOTS) {
		u32 nr_e820ext = nr_desc - ARRAY_SIZE(params->e820_table) +
				 EFI_MMAP_NR_SLACK_SLOTS;

		status = alloc_e820ext(nr_e820ext, e820ext, e820ext_size);
		if (status != EFI_SUCCESS)
			return status;
	}

	if (IS_ENABLED(CONFIG_UNACCEPTED_MEMORY))
		return allocate_unaccepted_bitmap(nr_desc, map);

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
	p->efi->efi_memdesc_size	= map->desc_size;
	p->efi->efi_memdesc_version	= map->desc_ver;
	efi_set_u64_split((unsigned long)map->map,
			  &p->efi->efi_memmap, &p->efi->efi_memmap_hi);
	p->efi->efi_memmap_size		= map->map_size;

	return EFI_SUCCESS;
}

static efi_status_t exit_boot(struct boot_params *boot_params, void *handle)
{
	struct setup_data *e820ext = NULL;
	__u32 e820ext_size = 0;
	efi_status_t status;
	struct exit_boot_struct priv;

	priv.boot_params	= boot_params;
	priv.efi		= &boot_params->efi_info;

	status = allocate_e820(boot_params, &e820ext, &e820ext_size);
	if (status != EFI_SUCCESS)
		return status;

	/* Might as well exit boot services now */
	status = efi_exit_boot_services(handle, &priv, exit_boot_func);
	if (status != EFI_SUCCESS)
		return status;

	/* Historic? */
	boot_params->alt_mem_k	= 32 * 1024;

	status = setup_e820(boot_params, e820ext, e820ext_size);
	if (status != EFI_SUCCESS)
		return status;

	return EFI_SUCCESS;
}

static bool have_unsupported_snp_features(void)
{
	u64 unsupported;

	unsupported = snp_get_unsupported_features(sev_get_status());
	if (unsupported) {
		efi_err("Unsupported SEV-SNP features detected: 0x%llx\n",
			unsupported);
		return true;
	}
	return false;
}

static void efi_get_seed(void *seed, int size)
{
	efi_get_random_bytes(size, seed);

	/*
	 * This only updates seed[0] when running on 32-bit, but in that case,
	 * seed[1] is not used anyway, as there is no virtual KASLR on 32-bit.
	 */
	*(unsigned long *)seed ^= kaslr_get_random_long("EFI");
}

static void error(char *str)
{
	efi_warn("Decompression failed: %s\n", str);
}

static const char *cmdline_memmap_override;

static efi_status_t parse_options(const char *cmdline)
{
	static const char opts[][14] = {
		"mem=", "memmap=", "hugepages="
	};

	for (int i = 0; i < ARRAY_SIZE(opts); i++) {
		const char *p = strstr(cmdline, opts[i]);

		if (p == cmdline || (p > cmdline && isspace(p[-1]))) {
			cmdline_memmap_override = opts[i];
			break;
		}
	}

	return efi_parse_options(cmdline);
}

static efi_status_t efi_decompress_kernel(unsigned long *kernel_entry,
					  struct boot_params *boot_params)
{
	unsigned long virt_addr = LOAD_PHYSICAL_ADDR;
	unsigned long addr, alloc_size, entry;
	efi_status_t status;
	u32 seed[2] = {};

	boot_params_ptr	= boot_params;

	/* determine the required size of the allocation */
	alloc_size = ALIGN(max_t(unsigned long, output_len, kernel_total_size),
			   MIN_KERNEL_ALIGN);

	if (IS_ENABLED(CONFIG_RANDOMIZE_BASE) && !efi_nokaslr) {
		u64 range = KERNEL_IMAGE_SIZE - LOAD_PHYSICAL_ADDR - kernel_total_size;
		static const efi_char16_t ami[] = L"American Megatrends";

		efi_get_seed(seed, sizeof(seed));

		virt_addr += (range * seed[1]) >> 32;
		virt_addr &= ~(CONFIG_PHYSICAL_ALIGN - 1);

		/*
		 * Older Dell systems with AMI UEFI firmware v2.0 may hang
		 * while decompressing the kernel if physical address
		 * randomization is enabled.
		 *
		 * https://bugzilla.kernel.org/show_bug.cgi?id=218173
		 */
		if (efi_system_table->hdr.revision <= EFI_2_00_SYSTEM_TABLE_REVISION &&
		    !memcmp(efistub_fw_vendor(), ami, sizeof(ami))) {
			efi_debug("AMI firmware v2.0 or older detected - disabling physical KASLR\n");
			seed[0] = 0;
		} else if (cmdline_memmap_override) {
			efi_info("%s detected on the kernel command line - disabling physical KASLR\n",
				 cmdline_memmap_override);
			seed[0] = 0;
		}

		boot_params->hdr.loadflags |= KASLR_FLAG;
	}

	status = efi_random_alloc(alloc_size, CONFIG_PHYSICAL_ALIGN, &addr,
				  seed[0], EFI_LOADER_CODE,
				  LOAD_PHYSICAL_ADDR,
				  EFI_X86_KERNEL_ALLOC_LIMIT);
	if (status != EFI_SUCCESS)
		return status;

	entry = decompress_kernel((void *)addr, virt_addr, error);
	if (entry == ULONG_MAX) {
		efi_free(alloc_size, addr);
		return EFI_LOAD_ERROR;
	}

	*kernel_entry = addr + entry;

	return efi_adjust_memory_range_protection(addr, kernel_text_size) ?:
	       efi_adjust_memory_range_protection(addr + kernel_inittext_offset,
						  kernel_inittext_size);
}

static void __noreturn enter_kernel(unsigned long kernel_addr,
				    struct boot_params *boot_params)
{
	/* enter decompressed kernel with boot_params pointer in RSI/ESI */
	asm("jmp *%0"::"r"(kernel_addr), "S"(boot_params));

	unreachable();
}

/*
 * On success, this routine will jump to the relocated image directly and never
 * return.  On failure, it will exit to the firmware via efi_exit() instead of
 * returning.
 */
void __noreturn efi_stub_entry(efi_handle_t handle,
			       efi_system_table_t *sys_table_arg,
			       struct boot_params *boot_params)

{
	efi_guid_t guid = EFI_MEMORY_ATTRIBUTE_PROTOCOL_GUID;
	const struct linux_efi_initrd *initrd = NULL;
	unsigned long kernel_entry;
	struct setup_header *hdr;
	efi_status_t status;

	efi_system_table = sys_table_arg;
	/* Check if we were booted by the EFI firmware */
	if (efi_system_table->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE)
		efi_exit(handle, EFI_INVALID_PARAMETER);

	if (!IS_ENABLED(CONFIG_EFI_HANDOVER_PROTOCOL) || !boot_params) {
		status = efi_allocate_bootparams(handle, &boot_params);
		if (status != EFI_SUCCESS)
			efi_exit(handle, status);
	}

	hdr = &boot_params->hdr;

	if (have_unsupported_snp_features())
		efi_exit(handle, EFI_UNSUPPORTED);

	if (IS_ENABLED(CONFIG_EFI_DXE_MEM_ATTRIBUTES)) {
		efi_dxe_table = get_efi_config_table(EFI_DXE_SERVICES_TABLE_GUID);
		if (efi_dxe_table &&
		    efi_dxe_table->hdr.signature != EFI_DXE_SERVICES_TABLE_SIGNATURE) {
			efi_warn("Ignoring DXE services table: invalid signature\n");
			efi_dxe_table = NULL;
		}
	}

	/* grab the memory attributes protocol if it exists */
	efi_bs_call(locate_protocol, &guid, NULL, (void **)&memattr);

	status = efi_setup_5level_paging();
	if (status != EFI_SUCCESS) {
		efi_err("efi_setup_5level_paging() failed!\n");
		goto fail;
	}

#ifdef CONFIG_CMDLINE_BOOL
	status = parse_options(CONFIG_CMDLINE);
	if (status != EFI_SUCCESS) {
		efi_err("Failed to parse options\n");
		goto fail;
	}
#endif
	if (!IS_ENABLED(CONFIG_CMDLINE_OVERRIDE)) {
		unsigned long cmdline_paddr = ((u64)hdr->cmd_line_ptr |
					       ((u64)boot_params->ext_cmd_line_ptr << 32));
		status = parse_options((char *)cmdline_paddr);
		if (status != EFI_SUCCESS) {
			efi_err("Failed to parse options\n");
			goto fail;
		}
	}

	if (efi_mem_encrypt > 0)
		hdr->xloadflags |= XLF_MEM_ENCRYPTION;

	status = efi_decompress_kernel(&kernel_entry, boot_params);
	if (status != EFI_SUCCESS) {
		efi_err("Failed to decompress kernel\n");
		goto fail;
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
	status = efi_load_initrd(image, hdr->initrd_addr_max, ULONG_MAX,
				 &initrd);
	if (status != EFI_SUCCESS)
		goto fail;
	if (initrd && initrd->size > 0) {
		efi_set_u64_split(initrd->base, &hdr->ramdisk_image,
				  &boot_params->ext_ramdisk_image);
		efi_set_u64_split(initrd->size, &hdr->ramdisk_size,
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

	efi_retrieve_eventlog();

	setup_graphics(boot_params);

	setup_efi_pci(boot_params);

	setup_quirks(boot_params);

	setup_unaccepted_memory();

	status = exit_boot(boot_params, handle);
	if (status != EFI_SUCCESS) {
		efi_err("exit_boot() failed!\n");
		goto fail;
	}

	/*
	 * Call the SEV init code while still running with the firmware's
	 * GDT/IDT, so #VC exceptions will be handled by EFI.
	 */
	sev_enable(boot_params);

	efi_5level_switch();

	enter_kernel(kernel_entry, boot_params);
fail:
	efi_err("efi_stub_entry() failed!\n");

	efi_exit(handle, status);
}

efi_status_t __efiapi efi_pe_entry(efi_handle_t handle,
				   efi_system_table_t *sys_table_arg)
{
	efi_stub_entry(handle, sys_table_arg, NULL);
}

#ifdef CONFIG_EFI_HANDOVER_PROTOCOL
void efi_handover_entry(efi_handle_t handle, efi_system_table_t *sys_table_arg,
			struct boot_params *boot_params)
{
	memset(_bss, 0, _ebss - _bss);
	efi_stub_entry(handle, sys_table_arg, boot_params);
}

#ifndef CONFIG_EFI_MIXED
extern __alias(efi_handover_entry)
void efi32_stub_entry(efi_handle_t handle, efi_system_table_t *sys_table_arg,
		      struct boot_params *boot_params);

extern __alias(efi_handover_entry)
void efi64_stub_entry(efi_handle_t handle, efi_system_table_t *sys_table_arg,
		      struct boot_params *boot_params);
#endif
#endif
