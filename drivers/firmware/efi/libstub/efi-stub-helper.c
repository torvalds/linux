// SPDX-License-Identifier: GPL-2.0
/*
 * Helper functions used by the EFI stub on multiple
 * architectures. This should be #included by the EFI stub
 * implementation files.
 *
 * Copyright 2011 Intel Corporation; author Matt Fleming
 */

#include <stdarg.h>

#include <linux/ctype.h>
#include <linux/efi.h>
#include <linux/kernel.h>
#include <linux/printk.h> /* For CONSOLE_LOGLEVEL_* */
#include <asm/efi.h>
#include <asm/setup.h>

#include "efistub.h"

bool efi_nochunk;
bool efi_nokaslr;
bool efi_noinitrd;
int efi_loglevel = CONSOLE_LOGLEVEL_DEFAULT;
bool efi_novamap;

static bool efi_nosoftreserve;
static bool efi_disable_pci_dma = IS_ENABLED(CONFIG_EFI_DISABLE_PCI_DMA);

bool __pure __efi_soft_reserve_enabled(void)
{
	return !efi_nosoftreserve;
}

/**
 * efi_char16_puts() - Write a UCS-2 encoded string to the console
 * @str:	UCS-2 encoded string
 */
void efi_char16_puts(efi_char16_t *str)
{
	efi_call_proto(efi_table_attr(efi_system_table, con_out),
		       output_string, str);
}

static
u32 utf8_to_utf32(const u8 **s8)
{
	u32 c32;
	u8 c0, cx;
	size_t clen, i;

	c0 = cx = *(*s8)++;
	/*
	 * The position of the most-significant 0 bit gives us the length of
	 * a multi-octet encoding.
	 */
	for (clen = 0; cx & 0x80; ++clen)
		cx <<= 1;
	/*
	 * If the 0 bit is in position 8, this is a valid single-octet
	 * encoding. If the 0 bit is in position 7 or positions 1-3, the
	 * encoding is invalid.
	 * In either case, we just return the first octet.
	 */
	if (clen < 2 || clen > 4)
		return c0;
	/* Get the bits from the first octet. */
	c32 = cx >> clen--;
	for (i = 0; i < clen; ++i) {
		/* Trailing octets must have 10 in most significant bits. */
		cx = (*s8)[i] ^ 0x80;
		if (cx & 0xc0)
			return c0;
		c32 = (c32 << 6) | cx;
	}
	/*
	 * Check for validity:
	 * - The character must be in the Unicode range.
	 * - It must not be a surrogate.
	 * - It must be encoded using the correct number of octets.
	 */
	if (c32 > 0x10ffff ||
	    (c32 & 0xf800) == 0xd800 ||
	    clen != (c32 >= 0x80) + (c32 >= 0x800) + (c32 >= 0x10000))
		return c0;
	*s8 += clen;
	return c32;
}

/**
 * efi_puts() - Write a UTF-8 encoded string to the console
 * @str:	UTF-8 encoded string
 */
void efi_puts(const char *str)
{
	efi_char16_t buf[128];
	size_t pos = 0, lim = ARRAY_SIZE(buf);
	const u8 *s8 = (const u8 *)str;
	u32 c32;

	while (*s8) {
		if (*s8 == '\n')
			buf[pos++] = L'\r';
		c32 = utf8_to_utf32(&s8);
		if (c32 < 0x10000) {
			/* Characters in plane 0 use a single word. */
			buf[pos++] = c32;
		} else {
			/*
			 * Characters in other planes encode into a surrogate
			 * pair.
			 */
			buf[pos++] = (0xd800 - (0x10000 >> 10)) + (c32 >> 10);
			buf[pos++] = 0xdc00 + (c32 & 0x3ff);
		}
		if (*s8 == '\0' || pos >= lim - 2) {
			buf[pos] = L'\0';
			efi_char16_puts(buf);
			pos = 0;
		}
	}
}

/**
 * efi_printk() - Print a kernel message
 * @fmt:	format string
 *
 * The first letter of the format string is used to determine the logging level
 * of the message. If the level is less then the current EFI logging level, the
 * message is suppressed. The message will be truncated to 255 bytes.
 *
 * Return:	number of printed characters
 */
int efi_printk(const char *fmt, ...)
{
	char printf_buf[256];
	va_list args;
	int printed;
	int loglevel = printk_get_level(fmt);

	switch (loglevel) {
	case '0' ... '9':
		loglevel -= '0';
		break;
	default:
		/*
		 * Use loglevel -1 for cases where we just want to print to
		 * the screen.
		 */
		loglevel = -1;
		break;
	}

	if (loglevel >= efi_loglevel)
		return 0;

	if (loglevel >= 0)
		efi_puts("EFI stub: ");

	fmt = printk_skip_level(fmt);

	va_start(args, fmt);
	printed = vsnprintf(printf_buf, sizeof(printf_buf), fmt, args);
	va_end(args);

	efi_puts(printf_buf);
	if (printed >= sizeof(printf_buf)) {
		efi_puts("[Message truncated]\n");
		return -1;
	}

	return printed;
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
			efi_loglevel = CONSOLE_LOGLEVEL_QUIET;
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
 * Convert the unicode UEFI command line to ASCII to pass to kernel.
 * Size of memory allocated return in *cmd_line_len.
 * Returns NULL on error.
 */
char *efi_convert_cmdline(efi_loaded_image_t *image, int *cmd_line_len)
{
	const u16 *s2;
	unsigned long cmdline_addr = 0;
	int options_chars = efi_table_attr(image, load_options_size) / 2;
	const u16 *options = efi_table_attr(image, load_options);
	int options_bytes = 0, safe_options_bytes = 0;  /* UTF-8 bytes */
	bool in_quote = false;
	efi_status_t status;

	if (options) {
		s2 = options;
		while (options_bytes < COMMAND_LINE_SIZE && options_chars--) {
			u16 c = *s2++;

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
 * @map:	pointer to receive the memory map
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
 * @load_addr:	pointer to store the address where the initrd was loaded
 * @load_size:	pointer to store the size of the loaded initrd
 * @max:	upper limit for the initrd memory allocation
 *
 * Return:
 * * %EFI_SUCCESS if the initrd was loaded successfully, in which
 *   case @load_addr and @load_size are assigned accordingly
 * * %EFI_NOT_FOUND if no LoadFile2 protocol exists on the initrd device path
 * * %EFI_INVALID_PARAMETER if load_addr == NULL or load_size == NULL
 * * %EFI_OUT_OF_RESOURCES if memory allocation failed
 * * %EFI_LOAD_ERROR in all other cases
 */
static
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

static
efi_status_t efi_load_initrd_cmdline(efi_loaded_image_t *image,
				     unsigned long *load_addr,
				     unsigned long *load_size,
				     unsigned long soft_limit,
				     unsigned long hard_limit)
{
	if (!IS_ENABLED(CONFIG_EFI_GENERIC_STUB_INITRD_CMDLINE_LOADER) ||
	    (IS_ENABLED(CONFIG_X86) && (!efi_is_native() || image == NULL))) {
		*load_addr = *load_size = 0;
		return EFI_SUCCESS;
	}

	return handle_cmdline_files(image, L"initrd=", sizeof(L"initrd=") - 2,
				    soft_limit, hard_limit,
				    load_addr, load_size);
}

/**
 * efi_load_initrd() - Load initial RAM disk
 * @image:	EFI loaded image protocol
 * @load_addr:	pointer to loaded initrd
 * @load_size:	size of loaded initrd
 * @soft_limit:	preferred size of allocated memory for loading the initrd
 * @hard_limit:	minimum size of allocated memory
 *
 * Return:	status code
 */
efi_status_t efi_load_initrd(efi_loaded_image_t *image,
			     unsigned long *load_addr,
			     unsigned long *load_size,
			     unsigned long soft_limit,
			     unsigned long hard_limit)
{
	efi_status_t status;

	if (!load_addr || !load_size)
		return EFI_INVALID_PARAMETER;

	status = efi_load_initrd_dev_path(load_addr, load_size, hard_limit);
	if (status == EFI_SUCCESS) {
		efi_info("Loaded initrd from LINUX_EFI_INITRD_MEDIA_GUID device path\n");
	} else if (status == EFI_NOT_FOUND) {
		status = efi_load_initrd_cmdline(image, load_addr, load_size,
						 soft_limit, hard_limit);
		if (status == EFI_SUCCESS && *load_size > 0)
			efi_info("Loaded initrd from command line option\n");
	}

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
