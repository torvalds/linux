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
 * policy rule for module appraisal is added only if CONFIG_MODULE_SIG
 * is not enabled.
 */
static const char *const secure_rules[] = {
	"appraise func=KEXEC_KERNEL_CHECK appraise_type=imasig|modsig",
#ifndef CONFIG_MODULE_SIG
	"appraise func=MODULE_CHECK appraise_type=imasig|modsig",
#endif
	NULL
};

/*
 * The "trusted_rules" are enabled only on "trustedboot" enabled systems.
 * These rules add the kexec kernel image and kernel modules file hashes to
 * the IMA measurement list.
 */
static const char *const trusted_rules[] = {
	"measure func=KEXEC_KERNEL_CHECK",
	"measure func=MODULE_CHECK",
	NULL
};

/*
 * The "secure_and_trusted_rules" contains rules for both the secure boot and
 * trusted boot. The "template=ima-modsig" option includes the appended
 * signature, when available, in the IMA measurement list.
 */
static const char *const secure_and_trusted_rules[] = {
	"measure func=KEXEC_KERNEL_CHECK template=ima-modsig",
	"measure func=MODULE_CHECK template=ima-modsig",
	"appraise func=KEXEC_KERNEL_CHECK appraise_type=imasig|modsig",
#ifndef CONFIG_MODULE_SIG
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
	if (is_ppc_secureboot_enabled()) {
		if (IS_ENABLED(CONFIG_MODULE_SIG))
			set_module_sig_enforced();

		if (is_ppc_trustedboot_enabled())
			return secure_and_trusted_rules;
		else
			return secure_rules;
	} else if (is_ppc_trustedboot_enabled()) {
		return trusted_rules;
	}

	return NULL;
}
