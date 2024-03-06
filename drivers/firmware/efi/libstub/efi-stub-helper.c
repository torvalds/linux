// SPDX-License-Identifier: GPL-2.0
/*
 * Helper functions used by the EFI stub on multiple
 * architectures. This should be #included by the EFI stub
 * implementation files.
 *
 * Copyright 2011 Intel Corporation; author Matt Fleming
 */

#include <linux/stdarg.h>

#include <linux/efi.h>
#include <linux/kernel.h>
#include <asm/efi.h>
#include <asm/setup.h>

#include "efistub.h"

bool efi_nochunk;
bool efi_nokaslr = !IS_ENABLED(CONFIG_RANDOMIZE_BASE);
bool efi_novamap;

static bool efi_noinitrd;
static bool efi_nosoftreserve;
static bool efi_disable_pci_dma = IS_ENABLED(CONFIG_EFI_DISABLE_PCI_DMA);

bool __pure __efi_soft_reserve_enabled(void)
{
	return !efi_nosoftreserve;
}

/**
 * efi_parse_options() - Parse EFI command line options
 * @cmdline:	kernel command line
 *
 * Parse the ASCII string @cmdline for EFI options, denoted by the efi=
 * option, e.g. efi=nochunk.
 *
 * It should be noted that efi= is parsed in two very different
 * environments, first in the early boot environment of the EFI boot
 * stub, and subsequently during the kernel boot.
 *
 * Return:	status code
 */
efi_status_t efi_parse_options(char const *cmdline)
{
	size_t len;
	efi_status_t status;
	char *str, *buf;

	if (!cmdline)
		return EFI_SUCCESS;

	len = strnlen(cmdline, COMMAND_LINE_SIZE - 1) + 1;
	status = efi_bs_call(allocate_pool, EFI_LOADER_DATA, len, (void **)&buf);
	if (status != EFI_SUCCESS)
		return status;

	memcpy(buf, cmdline, len - 1);
	buf[len - 1] = '\0';
	str = skip_spaces(buf);

	while (*str) {
		char *param, *val;

		str = next_arg(str, &param, &val);
		if (!val && !strcmp(param, "--"))
			break;

		if (!strcmp(param, "nokaslr")) {
			efi_nokaslr = true;
		} else if (!strcmp(param, "quiet")) {
			efi_loglevel = CONSOLE_LOGLEVEL_QUIET;
		} else if (!strcmp(param, "noinitrd")) {
			efi_noinitrd = true;
		} else if (IS_ENABLED(CONFIG_X86_64) && !strcmp(param, "no5lvl")) {
			efi_no5lvl = true;
		} else if (!strcmp(param, "efi") && val) {
			efi_nochunk = parse_option_str(val, "nochunk");
			efi_novamap |= parse_option_str(val, "novamap");

			efi_nosoftreserve = IS_ENABLED(CONFIG_EFI_SOFT_RESERVE) &&
					    parse_option_str(val, "nosoftreserve");

			if (parse_option_str(val, "disable_early_pci_dma"))
				efi_disable_pci_dma = true;
			if (parse_option_str(val, "no_disable_early_pci_dma"))
				efi_disable_pci_dma = false;
			if (parse_option_str(val, "debug"))
				efi_loglevel = CONSOLE_LOGLEVEL_DEBUG;
		} else if (!strcmp(param, "video") &&
			   val && strstarts(val, "efifb:")) {
			efi_parse_option_graphics(val + strlen("efifb:"));
		}
	}
	efi_bs_call(free_pool, buf);
	return EFI_SUCCESS;
}

/*
 * The EFI_LOAD_OPTION descriptor has the following layout:
 *	u32 Attributes;
 *	u16 FilePathListLength;
 *	u16 Description[];
 *	efi_device_path_protocol_t FilePathList[];
 *	u8 OptionalData[];
 *
 * This function validates and unpacks the variable-size data fields.
 */
static
bool efi_load_option_unpack(efi_load_option_unpacked_t *dest,
			    const efi_load_option_t *src, size_t size)
{
	const void *pos;
	u16 c;
	efi_device_path_protocol_t header;
	const efi_char16_t *description;
	const efi_device_path_protocol_t *file_path_list;

	if (size < offsetof(efi_load_option_t, variable_data))
		return false;
	pos = src->variable_data;
	size -= offsetof(efi_load_option_t, variable_data);

	if ((src->attributes & ~EFI_LOAD_OPTION_MASK) != 0)
		return false;

	/* Scan description. */
	description = pos;
	do {
		if (size < sizeof(c))
			return false;
		c = *(const u16 *)pos;
		pos += sizeof(c);
		size -= sizeof(c);
	} while (c != L'\0');

	/* Scan file_path_list. */
	file_path_list = pos;
	do {
		if (size < sizeof(header))
			return false;
		header = *(const efi_device_path_protocol_t *)pos;
		if (header.length < sizeof(header))
			return false;
		if (size < header.length)
			return false;
		pos += header.length;
		size -= header.length;
	} while ((header.type != EFI_DEV_END_PATH && header.type != EFI_DEV_END_PATH2) ||
		 (header.sub_type != EFI_DEV_END_ENTIRE));
	if (pos != (const void *)file_path_list + src->file_path_list_length)
		return false;

	dest->attributes = src->attributes;
	dest->file_path_list_length = src->file_path_list_length;
	dest->description = description;
	dest->file_path_list = file_path_list;
	dest->optional_data_size = size;
	dest->optional_data = size ? pos : NULL;

	return true;
}

/*
 * At least some versions of Dell firmware pass the entire contents of the
 * Boot#### variable, i.e. the EFI_LOAD_OPTION descriptor, rather than just the
 * OptionalData field.
 *
 * Detect this case and extract OptionalData.
 */
void efi_apply_loadoptions_quirk(const void **load_options, u32 *load_options_size)
{
	const efi_load_option_t *load_option = *load_options;
	efi_load_option_unpacked_t load_option_unpacked;

	if (!IS_ENABLED(CONFIG_X86))
		return;
	if (!load_option)
		return;
	if (*load_options_size < sizeof(*load_option))
		return;
	if ((load_option->attributes & ~EFI_LOAD_OPTION_BOOT_MASK) != 0)
		return;

	if (!efi_load_option_unpack(&load_option_unpacked, load_option, *load_options_size))
		return;

	efi_warn_once(FW_BUG "LoadOptions is an EFI_LOAD_OPTION descriptor\n");
	efi_warn_once(FW_BUG "Using OptionalData as a workaround\n");

	*load_options = load_option_unpacked.optional_data;
	*load_options_size = load_option_unpacked.optional_data_size;
}

enum efistub_event {
	EFISTUB_EVT_INITRD,
	EFISTUB_EVT_LOAD_OPTIONS,
	EFISTUB_EVT_COUNT,
};

#define STR_WITH_SIZE(s)	sizeof(s), s

static const struct {
	u32		pcr_index;
	u32		event_id;
	u32		event_data_len;
	u8		event_data[52];
} events[] = {
	[EFISTUB_EVT_INITRD] = {
		9,
		INITRD_EVENT_TAG_ID,
		STR_WITH_SIZE("Linux initrd")
	},
	[EFISTUB_EVT_LOAD_OPTIONS] = {
		9,
		LOAD_OPTIONS_EVENT_TAG_ID,
		STR_WITH_SIZE("LOADED_IMAGE::LoadOptions")
	},
};

static efi_status_t efi_measure_tagged_event(unsigned long load_addr,
					     unsigned long load_size,
					     enum efistub_event event)
{
	efi_guid_t tcg2_guid = EFI_TCG2_PROTOCOL_GUID;
	efi_tcg2_protocol_t *tcg2 = NULL;
	efi_status_t status;

	efi_bs_call(locate_protocol, &tcg2_guid, NULL, (void **)&tcg2);
	if (tcg2) {
		struct efi_measured_event {
			efi_tcg2_event_t	event_data;
			efi_tcg2_tagged_event_t tagged_event;
			u8			tagged_event_data[];
		} *evt;
		int size = sizeof(*evt) + events[event].event_data_len;

		status = efi_bs_call(allocate_pool, EFI_LOADER_DATA, size,
				     (void **)&evt);
		if (status != EFI_SUCCESS)
			goto fail;

		evt->event_data = (struct efi_tcg2_event){
			.event_size			= size,
			.event_header.header_size	= sizeof(evt->event_data.event_header),
			.event_header.header_version	= EFI_TCG2_EVENT_HEADER_VERSION,
			.event_header.pcr_index		= events[event].pcr_index,
			.event_header.event_type	= EV_EVENT_TAG,
		};

		evt->tagged_event = (struct efi_tcg2_tagged_event){
			.tagged_event_id		= events[event].event_id,
			.tagged_event_data_size		= events[event].event_data_len,
		};

		memcpy(evt->tagged_event_data, events[event].event_data,
		       events[event].event_data_len);

		status = efi_call_proto(tcg2, hash_log_extend_event, 0,
					load_addr, load_size, &evt->event_data);
		efi_bs_call(free_pool, evt);

		if (status != EFI_SUCCESS)
			goto fail;
		return EFI_SUCCESS;
	}

	return EFI_UNSUPPORTED;
fail:
	efi_warn("Failed to measure data for event %d: 0x%lx\n", event, status);
	return status;
}

/*
 * Convert the unicode UEFI command line to ASCII to pass to kernel.
 * Size of memory allocated return in *cmd_line_len.
 * Returns NULL on error.
 */
char *efi_convert_cmdline(efi_loaded_image_t *image, int *cmd_line_len)
{
	const efi_char16_t *options = efi_table_attr(image, load_options);
	u32 options_size = efi_table_attr(image, load_options_size);
	int options_bytes = 0, safe_options_bytes = 0;  /* UTF-8 bytes */
	unsigned long cmdline_addr = 0;
	const efi_char16_t *s2;
	bool in_quote = false;
	efi_status_t status;
	u32 options_chars;

	if (options_size > 0)
		efi_measure_tagged_event((unsigned long)options, options_size,
					 EFISTUB_EVT_LOAD_OPTIONS);

	efi_apply_loadoptions_quirk((const void **)&options, &options_size);
	options_chars = options_size / sizeof(efi_char16_t);

	if (options) {
		s2 = options;
		while (options_bytes < COMMAND_LINE_SIZE && options_chars--) {
			efi_char16_t c = *s2++;

			if (c < 0x80) {
				if (c == L'\0' || c == L'\n')
					break;
				if (c == L'"')
					in_quote = !in_quote;
				else if (!in_quote && isspace((char)c))
					safe_options_bytes = options_bytes;

				options_bytes++;
				continue;
			}

			/*
			 * Get the number of UTF-8 bytes corresponding to a
			 * UTF-16 character.
			 * The first part handles everything in the BMP.
			 */
			options_bytes += 2 + (c >= 0x800);
			/*
			 * Add one more byte for valid surrogate pairs. Invalid
			 * surrogates will be replaced with 0xfffd and take up
			 * only 3 bytes.
			 */
			if ((c & 0xfc00) == 0xd800) {
				/*
				 * If the very last word is a high surrogate,
				 * we must ignore it since we can't access the
				 * low surrogate.
				 */
				if (!options_chars) {
					options_bytes -= 3;
				} else if ((*s2 & 0xfc00) == 0xdc00) {
					options_bytes++;
					options_chars--;
					s2++;
				}
			}
		}
		if (options_bytes >= COMMAND_LINE_SIZE) {
			options_bytes = safe_options_bytes;
			efi_err("Command line is too long: truncated to %d bytes\n",
				options_bytes);
		}
	}

	options_bytes++;	/* NUL termination */

	status = efi_bs_call(allocate_pool, EFI_LOADER_DATA, options_bytes,
			     (void **)&cmdline_addr);
	if (status != EFI_SUCCESS)
		return NULL;

	snprintf((char *)cmdline_addr, options_bytes, "%.*ls",
		 options_bytes - 1, options);

	*cmd_line_len = options_bytes;
	return (char *)cmdline_addr;
}

/**
 * efi_exit_boot_services() - Exit boot services
 * @handle:	handle of the exiting image
 * @priv:	argument to be passed to @priv_func
 * @priv_func:	function to process the memory map before exiting boot services
 *
 * Handle calling ExitBootServices according to the requirements set out by the
 * spec.  Obtains the current memory map, and returns that info after calling
 * ExitBootServices.  The client must specify a function to perform any
 * processing of the memory map data prior to ExitBootServices.  A client
 * specific structure may be passed to the function via priv.  The client
 * function may be called multiple times.
 *
 * Return:	status code
 */
efi_status_t efi_exit_boot_services(void *handle, void *priv,
				    efi_exit_boot_map_processing priv_func)
{
	struct efi_boot_memmap *map;
	efi_status_t status;

	if (efi_disable_pci_dma)
		efi_pci_disable_bridge_busmaster();

	status = efi_get_memory_map(&map, true);
	if (status != EFI_SUCCESS)
		return status;

	status = priv_func(map, priv);
	if (status != EFI_SUCCESS) {
		efi_bs_call(free_pool, map);
		return status;
	}

	status = efi_bs_call(exit_boot_services, handle, map->map_key);

	if (status == EFI_INVALID_PARAMETER) {
		/*
		 * The memory map changed between efi_get_memory_map() and
		 * exit_boot_services().  Per the UEFI Spec v2.6, Section 6.4:
		 * EFI_BOOT_SERVICES.ExitBootServices we need to get the
		 * updated map, and try again.  The spec implies one retry
		 * should be sufficent, which is confirmed against the EDK2
		 * implementation.  Per the spec, we can only invoke
		 * get_memory_map() and exit_boot_services() - we cannot alloc
		 * so efi_get_memory_map() cannot be used, and we must reuse
		 * the buffer.  For all practical purposes, the headroom in the
		 * buffer should account for any changes in the map so the call
		 * to get_memory_map() is expected to succeed here.
		 */
		map->map_size = map->buff_size;
		status = efi_bs_call(get_memory_map,
				     &map->map_size,
				     &map->map,
				     &map->map_key,
				     &map->desc_size,
				     &map->desc_ver);

		/* exit_boot_services() was called, thus cannot free */
		if (status != EFI_SUCCESS)
			return status;

		status = priv_func(map, priv);
		/* exit_boot_services() was called, thus cannot free */
		if (status != EFI_SUCCESS)
			return status;

		status = efi_bs_call(exit_boot_services, handle, map->map_key);
	}

	return status;
}

/**
 * get_efi_config_table() - retrieve UEFI configuration table
 * @guid:	GUID of the configuration table to be retrieved
 * Return:	pointer to the configuration table or NULL
 */
void *get_efi_config_table(efi_guid_t guid)
{
	unsigned long tables = efi_table_attr(efi_system_table, tables);
	int nr_tables = efi_table_attr(efi_system_table, nr_tables);
	int i;

	for (i = 0; i < nr_tables; i++) {
		efi_config_table_t *t = (void *)tables;

		if (efi_guidcmp(t->guid, guid) == 0)
			return efi_table_attr(t, table);

		tables += efi_is_native() ? sizeof(efi_config_table_t)
					  : sizeof(efi_config_table_32_t);
	}
	return NULL;
}

/*
 * The LINUX_EFI_INITRD_MEDIA_GUID vendor media device path below provides a way
 * for the firmware or bootloader to expose the initrd data directly to the stub
 * via the trivial LoadFile2 protocol, which is defined in the UEFI spec, and is
 * very easy to implement. It is a simple Linux initrd specific conduit between
 * kernel and firmware, allowing us to put the EFI stub (being part of the
 * kernel) in charge of where and when to load the initrd, while leaving it up
 * to the firmware to decide whether it needs to expose its filesystem hierarchy
 * via EFI protocols.
 */
static const struct {
	struct efi_vendor_dev_path	vendor;
	struct efi_generic_dev_path	end;
} __packed initrd_dev_path = {
	{
		{
			EFI_DEV_MEDIA,
			EFI_DEV_MEDIA_VENDOR,
			sizeof(struct efi_vendor_dev_path),
		},
		LINUX_EFI_INITRD_MEDIA_GUID
	}, {
		EFI_DEV_END_PATH,
		EFI_DEV_END_ENTIRE,
		sizeof(struct efi_generic_dev_path)
	}
};

/**
 * efi_load_initrd_dev_path() - load the initrd from the Linux initrd device path
 * @initrd:	pointer of struct to store the address where the initrd was loaded
 *		and the size of the loaded initrd
 * @max:	upper limit for the initrd memory allocation
 *
 * Return:
 * * %EFI_SUCCESS if the initrd was loaded successfully, in which
 *   case @load_addr and @load_size are assigned accordingly
 * * %EFI_NOT_FOUND if no LoadFile2 protocol exists on the initrd device path
 * * %EFI_OUT_OF_RESOURCES if memory allocation failed
 * * %EFI_LOAD_ERROR in all other cases
 */
static
efi_status_t efi_load_initrd_dev_path(struct linux_efi_initrd *initrd,
				      unsigned long max)
{
	efi_guid_t lf2_proto_guid = EFI_LOAD_FILE2_PROTOCOL_GUID;
	efi_device_path_protocol_t *dp;
	efi_load_file2_protocol_t *lf2;
	efi_handle_t handle;
	efi_status_t status;

	dp = (efi_device_path_protocol_t *)&initrd_dev_path;
	status = efi_bs_call(locate_device_path, &lf2_proto_guid, &dp, &handle);
	if (status != EFI_SUCCESS)
		return status;

	status = efi_bs_call(handle_protocol, handle, &lf2_proto_guid,
			     (void **)&lf2);
	if (status != EFI_SUCCESS)
		return status;

	initrd->size = 0;
	status = efi_call_proto(lf2, load_file, dp, false, &initrd->size, NULL);
	if (status != EFI_BUFFER_TOO_SMALL)
		return EFI_LOAD_ERROR;

	status = efi_allocate_pages(initrd->size, &initrd->base, max);
	if (status != EFI_SUCCESS)
		return status;

	status = efi_call_proto(lf2, load_file, dp, false, &initrd->size,
				(void *)initrd->base);
	if (status != EFI_SUCCESS) {
		efi_free(initrd->size, initrd->base);
		return EFI_LOAD_ERROR;
	}
	return EFI_SUCCESS;
}

static
efi_status_t efi_load_initrd_cmdline(efi_loaded_image_t *image,
				     struct linux_efi_initrd *initrd,
				     unsigned long soft_limit,
				     unsigned long hard_limit)
{
	if (image == NULL)
		return EFI_UNSUPPORTED;

	return handle_cmdline_files(image, L"initrd=", sizeof(L"initrd=") - 2,
				    soft_limit, hard_limit,
				    &initrd->base, &initrd->size);
}

/**
 * efi_load_initrd() - Load initial RAM disk
 * @image:	EFI loaded image protocol
 * @soft_limit:	preferred address for loading the initrd
 * @hard_limit:	upper limit address for loading the initrd
 *
 * Return:	status code
 */
efi_status_t efi_load_initrd(efi_loaded_image_t *image,
			     unsigned long soft_limit,
			     unsigned long hard_limit,
			     const struct linux_efi_initrd **out)
{
	efi_guid_t tbl_guid = LINUX_EFI_INITRD_MEDIA_GUID;
	efi_status_t status = EFI_SUCCESS;
	struct linux_efi_initrd initrd, *tbl;

	if (!IS_ENABLED(CONFIG_BLK_DEV_INITRD) || efi_noinitrd)
		return EFI_SUCCESS;

	status = efi_load_initrd_dev_path(&initrd, hard_limit);
	if (status == EFI_SUCCESS) {
		efi_info("Loaded initrd from LINUX_EFI_INITRD_MEDIA_GUID device path\n");
		if (initrd.size > 0 &&
		    efi_measure_tagged_event(initrd.base, initrd.size,
					     EFISTUB_EVT_INITRD) == EFI_SUCCESS)
			efi_info("Measured initrd data into PCR 9\n");
	} else if (status == EFI_NOT_FOUND) {
		status = efi_load_initrd_cmdline(image, &initrd, soft_limit,
						 hard_limit);
		/* command line loader disabled or no initrd= passed? */
		if (status == EFI_UNSUPPORTED || status == EFI_NOT_READY)
			return EFI_SUCCESS;
		if (status == EFI_SUCCESS)
			efi_info("Loaded initrd from command line option\n");
	}
	if (status != EFI_SUCCESS)
		goto failed;

	status = efi_bs_call(allocate_pool, EFI_LOADER_DATA, sizeof(initrd),
			     (void **)&tbl);
	if (status != EFI_SUCCESS)
		goto free_initrd;

	*tbl = initrd;
	status = efi_bs_call(install_configuration_table, &tbl_guid, tbl);
	if (status != EFI_SUCCESS)
		goto free_tbl;

	if (out)
		*out = tbl;
	return EFI_SUCCESS;

free_tbl:
	efi_bs_call(free_pool, tbl);
free_initrd:
	efi_free(initrd.size, initrd.base);
failed:
	efi_err("Failed to load initrd: 0x%lx\n", status);
	return status;
}

/**
 * efi_wait_for_key() - Wait for key stroke
 * @usec:	number of microseconds to wait for key stroke
 * @key:	key entered
 *
 * Wait for up to @usec microseconds for a key stroke.
 *
 * Return:	status code, EFI_SUCCESS if key received
 */
efi_status_t efi_wait_for_key(unsigned long usec, efi_input_key_t *key)
{
	efi_event_t events[2], timer;
	unsigned long index;
	efi_simple_text_input_protocol_t *con_in;
	efi_status_t status;

	con_in = efi_table_attr(efi_system_table, con_in);
	if (!con_in)
		return EFI_UNSUPPORTED;
	efi_set_event_at(events, 0, efi_table_attr(con_in, wait_for_key));

	status = efi_bs_call(create_event, EFI_EVT_TIMER, 0, NULL, NULL, &timer);
	if (status != EFI_SUCCESS)
		return status;

	status = efi_bs_call(set_timer, timer, EfiTimerRelative,
			     EFI_100NSEC_PER_USEC * usec);
	if (status != EFI_SUCCESS)
		return status;
	efi_set_event_at(events, 1, timer);

	status = efi_bs_call(wait_for_event, 2, events, &index);
	if (status == EFI_SUCCESS) {
		if (index == 0)
			status = efi_call_proto(con_in, read_keystroke, key);
		else
			status = EFI_TIMEOUT;
	}

	efi_bs_call(close_event, timer);

	return status;
}

/**
 * efi_remap_image - Remap a loaded image with the appropriate permissions
 *                   for code and data
 *
 * @image_base:	the base of the image in memory
 * @alloc_size:	the size of the area in memory occupied by the image
 * @code_size:	the size of the leading part of the image containing code
 * 		and read-only data
 *
 * efi_remap_image() uses the EFI memory attribute protocol to remap the code
 * region of the loaded image read-only/executable, and the remainder
 * read-write/non-executable. The code region is assumed to start at the base
 * of the image, and will therefore cover the PE/COFF header as well.
 */
void efi_remap_image(unsigned long image_base, unsigned alloc_size,
		     unsigned long code_size)
{
	efi_guid_t guid = EFI_MEMORY_ATTRIBUTE_PROTOCOL_GUID;
	efi_memory_attribute_protocol_t *memattr;
	efi_status_t status;
	u64 attr;

	/*
	 * If the firmware implements the EFI_MEMORY_ATTRIBUTE_PROTOCOL, let's
	 * invoke it to remap the text/rodata region of the decompressed image
	 * as read-only and the data/bss region as non-executable.
	 */
	status = efi_bs_call(locate_protocol, &guid, NULL, (void **)&memattr);
	if (status != EFI_SUCCESS)
		return;

	// Get the current attributes for the entire region
	status = memattr->get_memory_attributes(memattr, image_base,
						alloc_size, &attr);
	if (status != EFI_SUCCESS) {
		efi_warn("Failed to retrieve memory attributes for image region: 0x%lx\n",
			 status);
		return;
	}

	// Mark the code region as read-only
	status = memattr->set_memory_attributes(memattr, image_base, code_size,
						EFI_MEMORY_RO);
	if (status != EFI_SUCCESS) {
		efi_warn("Failed to remap code region read-only\n");
		return;
	}

	// If the entire region was already mapped as non-exec, clear the
	// attribute from the code region. Otherwise, set it on the data
	// region.
	if (attr & EFI_MEMORY_XP) {
		status = memattr->clear_memory_attributes(memattr, image_base,
							  code_size,
							  EFI_MEMORY_XP);
		if (status != EFI_SUCCESS)
			efi_warn("Failed to remap code region executable\n");
	} else {
		status = memattr->set_memory_attributes(memattr,
							image_base + code_size,
							alloc_size - code_size,
							EFI_MEMORY_XP);
		if (status != EFI_SUCCESS)
			efi_warn("Failed to remap data region non-executable\n");
	}
}
