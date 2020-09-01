/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 IBM Corporation
 */
#include <linux/efi.h>
#include <linux/module.h>
#include <linux/ima.h>

extern struct boot_params boot_params;

static enum efi_secureboot_mode get_sb_mode(void)
{
	efi_guid_t efi_variable_guid = EFI_GLOBAL_VARIABLE_GUID;
	efi_status_t status;
	unsigned long size;
	u8 secboot, setupmode;

	size = sizeof(secboot);

	if (!efi_rt_services_supported(EFI_RT_SUPPORTED_GET_VARIABLE)) {
		pr_info("ima: secureboot mode unknown, no efi\n");
		return efi_secureboot_mode_unknown;
	}

	/* Get variable contents into buffer */
	status = efi.get_variable(L"SecureBoot", &efi_variable_guid,
				  NULL, &size, &secboot);
	if (status == EFI_NOT_FOUND) {
		pr_info("ima: secureboot mode disabled\n");
		return efi_secureboot_mode_disabled;
	}

	if (status != EFI_SUCCESS) {
		pr_info("ima: secureboot mode unknown\n");
		return efi_secureboot_mode_unknown;
	}

	size = sizeof(setupmode);
	status = efi.get_variable(L"SetupMode", &efi_variable_guid,
				  NULL, &size, &setupmode);

	if (status != EFI_SUCCESS)	/* ignore unknown SetupMode */
		setupmode = 0;

	if (secboot == 0 || setupmode == 1) {
		pr_info("ima: secureboot mode disabled\n");
		return efi_secureboot_mode_disabled;
	}

	pr_info("ima: secureboot mode enabled\n");
	return efi_secureboot_mode_enabled;
}

bool arch_ima_get_secureboot(void)
{
	static enum efi_secureboot_mode sb_mode;
	static bool initialized;

	if (!initialized && efi_enabled(EFI_BOOT)) {
		sb_mode = boot_params.secure_boot;

		if (sb_mode == efi_secureboot_mode_unset)
			sb_mode = get_sb_mode();
		initialized = true;
	}

	if (sb_mode == efi_secureboot_mode_enabled)
		return true;
	else
		return false;
}

/* secureboot arch rules */
static const char * const sb_arch_rules[] = {
#if !IS_ENABLED(CONFIG_KEXEC_SIG)
	"appraise func=KEXEC_KERNEL_CHECK appraise_type=imasig",
#endif /* CONFIG_KEXEC_SIG */
	"measure func=KEXEC_KERNEL_CHECK",
#if !IS_ENABLED(CONFIG_MODULE_SIG)
	"appraise func=MODULE_CHECK appraise_type=imasig",
#endif
	"measure func=MODULE_CHECK",
	NULL
};

const char * const *arch_get_ima_policy(void)
{
	if (IS_ENABLED(CONFIG_IMA_ARCH_POLICY) && arch_ima_get_secureboot()) {
		if (IS_ENABLED(CONFIG_MODULE_SIG))
			set_module_sig_enforced();
		return sb_arch_rules;
	}
	return NULL;
}
