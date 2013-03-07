#ifndef _ASM_X86_BOOTPARAM_UTILS_H
#define _ASM_X86_BOOTPARAM_UTILS_H

#include <asm/bootparam.h>

/*
 * This file is included from multiple environments.  Do not
 * add completing #includes to make it standalone.
 */

/*
 * Deal with bootloaders which fail to initialize unknown fields in
 * boot_params to zero.  The list fields in this list are taken from
 * analysis of kexec-tools; if other broken bootloaders initialize a
 * different set of fields we will need to figure out how to disambiguate.
 *
 * Note: efi_info is commonly left uninitialized, but that field has a
 * private magic, so it is better to leave it unchanged.
 */
static void sanitize_boot_params(struct boot_params *boot_params)
{
	if (boot_params->sentinel) {
		/*fields in boot_params are not valid, clear them */
		memset(&boot_params->olpc_ofw_header, 0,
		       (char *)&boot_params->efi_info -
			(char *)&boot_params->olpc_ofw_header);
		memset(&boot_params->kbd_status, 0,
		       (char *)&boot_params->hdr -
		       (char *)&boot_params->kbd_status);
		memset(&boot_params->_pad7[0], 0,
		       (char *)&boot_params->edd_mbr_sig_buffer[0] -
			(char *)&boot_params->_pad7[0]);
		memset(&boot_params->_pad8[0], 0,
		       (char *)&boot_params->eddbuf[0] -
			(char *)&boot_params->_pad8[0]);
		memset(&boot_params->_pad9[0], 0, sizeof(boot_params->_pad9));
	}
}

#endif /* _ASM_X86_BOOTPARAM_UTILS_H */
