// SPDX-License-Identifier: GPL-2.0-only

#include "error.h"
#include "misc.h"
#include "tdx.h"
#include "sev.h"
#include <asm/shared/tdx.h>

/*
 * accept_memory() and process_unaccepted_memory() called from EFI stub which
 * runs before decompressor and its early_tdx_detect().
 *
 * Enumerate TDX directly from the early users.
 */
static bool early_is_tdx_guest(void)
{
	static bool once;
	static bool is_tdx;

	if (!IS_ENABLED(CONFIG_INTEL_TDX_GUEST))
		return false;

	if (!once) {
		u32 eax, sig[3];

		cpuid_count(TDX_CPUID_LEAF_ID, 0, &eax,
			    &sig[0], &sig[2],  &sig[1]);
		is_tdx = !memcmp(TDX_IDENT, sig, sizeof(sig));
		once = true;
	}

	return is_tdx;
}

void arch_accept_memory(phys_addr_t start, phys_addr_t end)
{
	/* Platform-specific memory-acceptance call goes here */
	if (early_is_tdx_guest()) {
		if (!tdx_accept_memory(start, end))
			panic("TDX: Failed to accept memory\n");
	} else if (sev_snp_enabled()) {
		snp_accept_memory(start, end);
	} else {
		error("Cannot accept memory: unknown platform\n");
	}
}

bool init_unaccepted_memory(void)
{
	guid_t guid = LINUX_EFI_UNACCEPTED_MEM_TABLE_GUID;
	struct efi_unaccepted_memory *table;
	unsigned long cfg_table_pa;
	unsigned int cfg_table_len;
	enum efi_type et;
	int ret;

	et = efi_get_type(boot_params_ptr);
	if (et == EFI_TYPE_NONE)
		return false;

	ret = efi_get_conf_table(boot_params_ptr, &cfg_table_pa, &cfg_table_len);
	if (ret) {
		warn("EFI config table not found.");
		return false;
	}

	table = (void *)efi_find_vendor_table(boot_params_ptr, cfg_table_pa,
					      cfg_table_len, guid);
	if (!table)
		return false;

	if (table->version != 1)
		error("Unknown version of unaccepted memory table\n");

	/*
	 * In many cases unaccepted_table is already set by EFI stub, but it
	 * has to be initialized again to cover cases when the table is not
	 * allocated by EFI stub or EFI stub copied the kernel image with
	 * efi_relocate_kernel() before the variable is set.
	 *
	 * It must be initialized before the first usage of accept_memory().
	 */
	unaccepted_table = table;

	return true;
}
