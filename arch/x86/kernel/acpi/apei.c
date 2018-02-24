/*
 * Arch-specific APEI-related functions.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <acpi/apei.h>

#include <asm/mce.h>
#include <asm/tlbflush.h>

int arch_apei_enable_cmcff(struct acpi_hest_header *hest_hdr, void *data)
{
#ifdef CONFIG_X86_MCE
	int i;
	struct acpi_hest_ia_corrected *cmc;
	struct acpi_hest_ia_error_bank *mc_bank;

	cmc = (struct acpi_hest_ia_corrected *)hest_hdr;
	if (!cmc->enabled)
		return 0;

	/*
	 * We expect HEST to provide a list of MC banks that report errors
	 * in firmware first mode. Otherwise, return non-zero value to
	 * indicate that we are done parsing HEST.
	 */
	if (!(cmc->flags & ACPI_HEST_FIRMWARE_FIRST) ||
	    !cmc->num_hardware_banks)
		return 1;

	pr_info("HEST: Enabling Firmware First mode for corrected errors.\n");

	mc_bank = (struct acpi_hest_ia_error_bank *)(cmc + 1);
	for (i = 0; i < cmc->num_hardware_banks; i++, mc_bank++)
		mce_disable_bank(mc_bank->bank_number);
#endif
	return 1;
}

void arch_apei_report_mem_error(int sev, struct cper_sec_mem_err *mem_err)
{
#ifdef CONFIG_X86_MCE
	apei_mce_report_mem_error(sev, mem_err);
#endif
}

void arch_apei_flush_tlb_one(unsigned long addr)
{
	__flush_tlb_one_kernel(addr);
}
