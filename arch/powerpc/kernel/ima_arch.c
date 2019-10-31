// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 IBM Corporation
 * Author: Nayna Jain
 */

#include <linux/ima.h>
#include <asm/secure_boot.h>

bool arch_ima_get_secureboot(void)
{
	return is_ppc_secureboot_enabled();
}

/*
 * The "secure_rules" are enabled only on "secureboot" enabled systems.
 * These rules verify the file signatures against known good values.
 * The "appraise_type=imasig|modsig" option allows the known good signature
 * to be stored as an xattr or as an appended signature.
 *
 * To avoid duplicate signature verification as much as possible, the IMA
 * policy rule for module appraisal is added only if CONFIG_MODULE_SIG_FORCE
 * is not enabled.
 */
static const char *const secure_rules[] = {
	"appraise func=KEXEC_KERNEL_CHECK appraise_type=imasig|modsig",
#ifndef CONFIG_MODULE_SIG_FORCE
	"appraise func=MODULE_CHECK appraise_type=imasig|modsig",
#endif
	NULL
};

/*
 * Returns the relevant IMA arch-specific policies based on the system secure
 * boot state.
 */
const char *const *arch_get_ima_policy(void)
{
	if (is_ppc_secureboot_enabled())
		return secure_rules;

	return NULL;
}
