// SPDX-License-Identifier: GPL-2.0
#define BOOT_CTYPE_H
#include "misc.h"
#include "error.h"
#include "../string.h"

#ifdef CONFIG_ACPI

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
#endif /* CONFIG_ACPI */
