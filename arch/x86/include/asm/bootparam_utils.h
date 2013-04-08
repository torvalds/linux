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
	/* 
	 * IMPORTANT NOTE TO BOOTLOADER AUTHORS: do not simply clear
	 * this field.  The purpose of this field is to guarantee
	 * compliance with the x86 boot spec located in
	 * Documentation/x86/boot.txt .  That spec says that the
	 * *whole* structure should be cleared, after which only the
	 * portion defined by struct setup_header (boot_params->hdr)
	 * should be copied in.
	 *
	 * If you're having an issue because the sentinel is set, you
	 * need to change the whole structure to be cleared, not this
	 * (or any other) individual field, or you will soon have
	 * problems again.
	 */
	if (boot_params->sentinel) {
		/* fields in boot_params are left uninitialized, clear them */
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
