// SPDX-License-Identifier: GPL-2.0
#define BOOT_CTYPE_H
#include "misc.h"
#include "error.h"
#include "../string.h"

#include <linux/efi.h>
#include <asm/efi.h>

/*
 * Max length of 64-bit hex address string is 19, prefix "0x" + 16 hex
 * digits, and '\0' for termination.
 */
#define MAX_ADDR_LEN 19

static acpi_physical_address get_acpi_rsdp(void)
{
	acpi_physical_address addr = 0;

#ifdef CONFIG_KEXEC
	char val[MAX_ADDR_LEN] = { };
	int ret;

	ret = cmdline_find_option("acpi_rsdp", val, MAX_ADDR_LEN);
	if (ret < 0)
		return 0;

	if (kstrtoull(val, 16, &addr))
		return 0;
#endif
	return addr;
}

/* Search EFI system tables for RSDP. */
static acpi_physical_address efi_get_rsdp_addr(void)
{
	acpi_physical_address rsdp_addr = 0;

#ifdef CONFIG_EFI
	efi_system_table_t *systab;
	struct efi_info *ei;
	bool efi_64;
	int size, i;
	char *sig;

	ei = &boot_params->efi_info;
	sig = (char *)&ei->efi_loader_signature;

	if (!strncmp(sig, EFI64_LOADER_SIGNATURE, 4)) {
		efi_64 = true;
	} else if (!strncmp(sig, EFI32_LOADER_SIGNATURE, 4)) {
		efi_64 = false;
	} else {
		debug_putstr("Wrong EFI loader signature.\n");
		return 0;
	}

	/* Get systab from boot params. */
#ifdef CONFIG_X86_64
	systab = (efi_system_table_t *)(ei->efi_systab | ((__u64)ei->efi_systab_hi<<32));
#else
	if (ei->efi_systab_hi || ei->efi_memmap_hi) {
		debug_putstr("Error getting RSDP address: EFI system table located above 4GB.\n");
		return 0;
	}
	systab = (efi_system_table_t *)ei->efi_systab;
#endif
	if (!systab)
		error("EFI system table not found.");

	/*
	 * Get EFI tables from systab.
	 */
	size = efi_64 ? sizeof(efi_config_table_64_t) :
			sizeof(efi_config_table_32_t);

	for (i = 0; i < systab->nr_tables; i++) {
		acpi_physical_address table;
		void *config_tables;
		efi_guid_t guid;

		config_tables = (void *)(systab->tables + size * i);
		if (efi_64) {
			efi_config_table_64_t *tmp_table;

			tmp_table = config_tables;
			guid = tmp_table->guid;
			table = tmp_table->table;

			if (!IS_ENABLED(CONFIG_X86_64) && table >> 32) {
				debug_putstr("Error getting RSDP address: EFI config table located above 4GB.\n");
				return 0;
			}
		} else {
			efi_config_table_32_t *tmp_table;

			tmp_table = config_tables;
			guid = tmp_table->guid;
			table = tmp_table->table;
		}

		if (!(efi_guidcmp(guid, ACPI_TABLE_GUID)))
			rsdp_addr = table;
		else if (!(efi_guidcmp(guid, ACPI_20_TABLE_GUID)))
			return table;
	}
#endif
	return rsdp_addr;
}

static u8 compute_checksum(u8 *buffer, u32 length)
{
	u8 *end = buffer + length;
	u8 sum = 0;

	while (buffer < end)
		sum += *(buffer++);

	return sum;
}

/* Search a block of memory for the RSDP signature. */
static u8 *scan_mem_for_rsdp(u8 *start, u32 length)
{
	struct acpi_table_rsdp *rsdp;
	u8 *address, *end;

	end = start + length;

	/* Search from given start address for the requested length */
	for (address = start; address < end; address += ACPI_RSDP_SCAN_STEP) {
		/*
		 * Both RSDP signature and checksum must be correct.
		 * Note: Sometimes there exists more than one RSDP in memory;
		 * the valid RSDP has a valid checksum, all others have an
		 * invalid checksum.
		 */
		rsdp = (struct acpi_table_rsdp *)address;

		/* BAD Signature */
		if (!ACPI_VALIDATE_RSDP_SIG(rsdp->signature))
			continue;

		/* Check the standard checksum */
		if (compute_checksum((u8 *)rsdp, ACPI_RSDP_CHECKSUM_LENGTH))
			continue;

		/* Check extended checksum if table version >= 2 */
		if ((rsdp->revision >= 2) &&
		    (compute_checksum((u8 *)rsdp, ACPI_RSDP_XCHECKSUM_LENGTH)))
			continue;

		/* Signature and checksum valid, we have found a real RSDP */
		return address;
	}
	return NULL;
}

/* Search RSDP address in EBDA. */
static acpi_physical_address bios_get_rsdp_addr(void)
{
	unsigned long address;
	u8 *rsdp;

	/* Get the location of the Extended BIOS Data Area (EBDA) */
	address = *(u16 *)ACPI_EBDA_PTR_LOCATION;
	address <<= 4;

	/*
	 * Search EBDA paragraphs (EBDA is required to be a minimum of
	 * 1K length)
	 */
	if (address > 0x400) {
		rsdp = scan_mem_for_rsdp((u8 *)address, ACPI_EBDA_WINDOW_SIZE);
		if (rsdp)
			return (acpi_physical_address)(unsigned long)rsdp;
	}

	/* Search upper memory: 16-byte boundaries in E0000h-FFFFFh */
	rsdp = scan_mem_for_rsdp((u8 *) ACPI_HI_RSDP_WINDOW_BASE,
					ACPI_HI_RSDP_WINDOW_SIZE);
	if (rsdp)
		return (acpi_physical_address)(unsigned long)rsdp;

	return 0;
}

/* Return RSDP address on success, otherwise 0. */
acpi_physical_address get_rsdp_addr(void)
{
	acpi_physical_address pa;

	pa = get_acpi_rsdp();

	if (!pa)
		pa = boot_params->acpi_rsdp_addr;

	if (!pa)
		pa = efi_get_rsdp_addr();

	if (!pa)
		pa = bios_get_rsdp_addr();

	return pa;
}
