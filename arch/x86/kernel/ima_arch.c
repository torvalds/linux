/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 IBM Corporation
 */
#include <linux/efi.h>
#include <linux/ima.h>

extern struct boot_params boot_params;

bool arch_ima_get_secureboot(void)
{
	if (efi_enabled(EFI_BOOT) &&
		(boot_params.secure_boot == efi_secureboot_mode_enabled))
		return true;
	else
		return false;
}

/* secureboot arch rules */
static const char * const sb_arch_rules[] = {
#if !IS_ENABLED(CONFIG_KEXEC_VERIFY_SIG)
	"appraise func=KEXEC_KERNEL_CHECK appraise_type=imasig",
#endif /* CONFIG_KEXEC_VERIFY_SIG */
	"measure func=KEXEC_KERNEL_CHECK",
	NULL
};

const char * const *arch_get_ima_policy(void)
{
	if (IS_ENABLED(CONFIG_IMA_ARCH_POLICY) && arch_ima_get_secureboot())
		return sb_arch_rules;
	return NULL;
}
