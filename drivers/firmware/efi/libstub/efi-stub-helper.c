// SPDX-License-Identifier: GPL-2.0
/*
 * Helper functions used by the EFI stub on multiple
 * architectures. This should be #included by the EFI stub
 * implementation files.
 *
 * Copyright 2011 Intel Corporation; author Matt Fleming
 */

#include <linux/efi.h>
#include <asm/efi.h>

#include "efistub.h"

static bool __efistub_global efi_nochunk;
static bool __efistub_global efi_nokaslr;
static bool __efistub_global efi_noinitrd;
static bool __efistub_global efi_quiet;
static bool __efistub_global efi_novamap;
static bool __efistub_global efi_nosoftreserve;
static bool __efistub_global efi_disable_pci_dma =
					IS_ENABLED(CONFIG_EFI_DISABLE_PCI_DMA);

bool __pure nochunk(void)
{
	return efi_nochunk;
}
bool __pure nokaslr(void)
{
	return efi_nokaslr;
}
bool __pure noinitrd(void)
{
	return efi_noinitrd;
}
bool __pure is_quiet(void)
{
	return efi_quiet;
}
bool __pure novamap(void)
{
	return efi_novamap;
}
bool __pure __efi_soft_reserve_enabled(void)
{
	return !efi_nosoftreserve;
}

void efi_printk(char *str)
{
	char *s8;

	for (s8 = str; *s8; s8++) {
		efi_char16_t ch[2] = { 0 };

		ch[0] = *s8;
		if (*s8 == '\n') {
			efi_char16_t nl[2] = { '\r', 0 };
			efi_char16_printk(nl);
		}

		efi_char16_printk(ch);
	}
}

/*
 * Parse the ASCII string 'cmdline' for EFI options, denoted by the efi=
 * option, e.g. efi=nochunk.
 *
 * It should be noted that efi= is parsed in two very different
 * environments, first in the early boot environment of the EFI boot
 * stub, and subsequently during the kernel boot.
 */
efi_status_t efi_parse_options(char const *cmdline)
{
	size_t len = strlen(cmdline) + 1;
	efi_status_t status;
	char *str, *buf;

	status = efi_bs_call(allocate_pool, EFI_LOADER_DATA, len, (void **)&buf);
	if (status != EFI_SUCCESS)
		return status;

	str = skip_spaces(memcpy(buf, cmdline, len));

	while (*str) {
		char *param, *val;

		str = next_arg(str, &param, &val);

		if (!strcmp(param, "nokaslr")) {
			efi_nokaslr = true;
		} else if (!strcmp(param, "quiet")) {
			efi_quiet = true;
		} else if (!strcmp(param, "noinitrd")) {
			efi_noinitrd = true;
		} else if (!strcmp(param, "efi") && val) {
			efi_nochunk = parse_option_str(val, "nochunk");
			efi_novamap = parse_option_str(val, "novamap");

			efi_nosoftreserve = IS_ENABLED(CONFIG_EFI_SOFT_RESERVE) &&
					    parse_option_str(val, "nosoftreserve");

			if (parse_option_str(val, "disable_early_pci_dma"))
				efi_disable_pci_dma = true;
			if (parse_option_str(val, "no_disable_early_pci_dma"))
				efi_disable_pci_dma = false;
		}
	}
	efi_bs_call(free_pool, buf);
	return EFI_SUCCESS;
}

/*
 * Get the number of UTF-8 bytes corresponding to an UTF-16 character.
 * This overestimates for surrogates, but that is okay.
 */
static int efi_utf8_bytes(u16 c)
{
	return 1 + (c >= 0x80) + (c >= 0x800);
}

/*
 * Convert an UTF-16 string, not necessarily null terminated, to UTF-8.
 */
static u8 *efi_utf16_to_utf8(u8 *dst, const u16 *src, int n)
{
	unsigned int c;

	while (n--) {
		c = *src++;
		if (n && c >= 0xd800 && c <= 0xdbff &&
		    *src >= 0xdc00 && *src <= 0xdfff) {
			c = 0x10000 + ((c & 0x3ff) << 10) + (*src & 0x3ff);
			src++;
			n--;
		}
		if (c >= 0xd800 && c <= 0xdfff)
			c = 0xfffd; /* Unmatched surrogate */
		if (c < 0x80) {
			*dst++ = c;
			continue;
		}
		if (c < 0x800) {
			*dst++ = 0xc0 + (c >> 6);
			goto t1;
		}
		if (c < 0x10000) {
			*dst++ = 0xe0 + (c >> 12);
			goto t2;
		}
		*dst++ = 0xf0 + (c >> 18);
		*dst++ = 0x80 + ((c >> 12) & 0x3f);
	t2:
		*dst++ = 0x80 + ((c >> 6) & 0x3f);
	t1:
		*dst++ = 0x80 + (c & 0x3f);
	}

	return dst;
}

/*
 * Convert the unicode UEFI command line to ASCII to pass to kernel.
 * Size of memory allocated return in *cmd_line_len.
 * Returns NULL on error.
 */
char *efi_convert_cmdline(efi_loaded_image_t *image,
			  int *cmd_line_len, unsigned long max_addr)
{
	const u16 *s2;
	u8 *s1 = NULL;
	unsigned long cmdline_addr = 0;
	int load_options_chars = efi_table_attr(image, load_options_size) / 2;
	const u16 *options = efi_table_attr(image, load_options);
	int options_bytes = 0;  /* UTF-8 bytes */
	int options_chars = 0;  /* UTF-16 chars */
	efi_status_t status;
	u16 zero = 0;

	if (options) {
		s2 = options;
		while (*s2 && *s2 != '\n'
		       && options_chars < load_options_chars) {
			options_bytes += efi_utf8_bytes(*s2++);
			options_chars++;
		}
	}

	if (!options_chars) {
		/* No command line options, so return empty string*/
		options = &zero;
	}

	options_bytes++;	/* NUL termination */

	status = efi_allocate_pages(options_bytes, &cmdline_addr, max_addr);
	if (status != EFI_SUCCESS)
		return NULL;

	s1 = (u8 *)cmdline_addr;
	s2 = (const u16 *)options;

	s1 = efi_utf16_to_utf8(s1, s2, options_chars);
	*s1 = '\0';

	*cmd_line_len = options_bytes;
	return (char *)cmdline_addr;
}

/*
 * Handle calling ExitBootServices according to the requirements set out by the
 * spec.  Obtains the current memory map, and returns that info after calling
 * ExitBootServices.  The client must specify a function to perform any
 * processing of the memory map data prior to ExitBootServices.  A client
 * specific structure may be passed to the function via priv.  The client
 * function may be called multiple times.
 */
efi_status_t efi_exit_boot_services(void *handle,
				    struct efi_boot_memmap *map,
				    void *priv,
				    efi_exit_boot_map_processing priv_func)
{
	efi_status_t status;

	status = efi_get_memory_map(map);

	if (status != EFI_SUCCESS)
		goto fail;

	status = priv_func(map, priv);
	if (status != EFI_SUCCESS)
		goto free_map;

	if (efi_disable_pci_dma)
		efi_pci_disable_bridge_busmaster();

	status = efi_bs_call(exit_boot_services, handle, *map->key_ptr);

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
		*map->map_size = *map->buff_size;
		status = efi_bs_call(get_memory_map,
				     map->map_size,
				     *map->map,
				     map->key_ptr,
				     map->desc_size,
				     map->desc_ver);

		/* exit_boot_services() was called, thus cannot free */
		if (status != EFI_SUCCESS)
			goto fail;

		status = priv_func(map, priv);
		/* exit_boot_services() was called, thus cannot free */
		if (status != EFI_SUCCESS)
			goto fail;

		status = efi_bs_call(exit_boot_services, handle, *map->key_ptr);
	}

	/* exit_boot_services() was called, thus cannot free */
	if (status != EFI_SUCCESS)
		goto fail;

	return EFI_SUCCESS;

free_map:
	efi_bs_call(free_pool, *map->map);
fail:
	return status;
}

void *get_efi_config_table(efi_guid_t guid)
{
	unsigned long tables = efi_table_attr(efi_system_table(), tables);
	int nr_tables = efi_table_attr(efi_system_table(), nr_tables);
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

void efi_char16_printk(efi_char16_t *str)
{
	efi_call_proto(efi_table_attr(efi_system_table(), con_out),
		       output_string, str);
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
 * efi_load_initrd_dev_path - load the initrd from the Linux initrd device path
 * @load_addr:	pointer to store the address where the initrd was loaded
 * @load_size:	pointer to store the size of the loaded initrd
 * @max:	upper limit for the initrd memory allocation
 * @return:	%EFI_SUCCESS if the initrd was loaded successfully, in which
 *		case @load_addr and @load_size are assigned accordingly
 *		%EFI_NOT_FOUND if no LoadFile2 protocol exists on the initrd
 *		device path
 *		%EFI_INVALID_PARAMETER if load_addr == NULL or load_size == NULL
 *		%EFI_OUT_OF_RESOURCES if memory allocation failed
 *		%EFI_LOAD_ERROR in all other cases
 */
efi_status_t efi_load_initrd_dev_path(unsigned long *load_addr,
				      unsigned long *load_size,
				      unsigned long max)
{
	efi_guid_t lf2_proto_guid = EFI_LOAD_FILE2_PROTOCOL_GUID;
	efi_device_path_protocol_t *dp;
	efi_load_file2_protocol_t *lf2;
	unsigned long initrd_addr;
	unsigned long initrd_size;
	efi_handle_t handle;
	efi_status_t status;

	if (!load_addr || !load_size)
		return EFI_INVALID_PARAMETER;

	dp = (efi_device_path_protocol_t *)&initrd_dev_path;
	status = efi_bs_call(locate_device_path, &lf2_proto_guid, &dp, &handle);
	if (status != EFI_SUCCESS)
		return status;

	status = efi_bs_call(handle_protocol, handle, &lf2_proto_guid,
			     (void **)&lf2);
	if (status != EFI_SUCCESS)
		return status;

	status = efi_call_proto(lf2, load_file, dp, false, &initrd_size, NULL);
	if (status != EFI_BUFFER_TOO_SMALL)
		return EFI_LOAD_ERROR;

	status = efi_allocate_pages(initrd_size, &initrd_addr, max);
	if (status != EFI_SUCCESS)
		return status;

	status = efi_call_proto(lf2, load_file, dp, false, &initrd_size,
				(void *)initrd_addr);
	if (status != EFI_SUCCESS) {
		efi_free(initrd_size, initrd_addr);
		return EFI_LOAD_ERROR;
	}

	*load_addr = initrd_addr;
	*load_size = initrd_size;
	return EFI_SUCCESS;
}
