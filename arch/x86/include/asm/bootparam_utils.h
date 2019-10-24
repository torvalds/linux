/* SPDX-License-Identifier: GPL-2.0 */
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

#define sizeof_mbr(type, member) ({ sizeof(((type *)0)->member); })

#define BOOT_PARAM_PRESERVE(struct_member)				\
	{								\
		.start = offsetof(struct boot_params, struct_member),	\
		.len   = sizeof_mbr(struct boot_params, struct_member),	\
	}

struct boot_params_to_save {
	unsigned int start;
	unsigned int len;
};

static void sanitize_boot_params(struct boot_params *boot_params)
{
	/* 
	 * IMPORTANT NOTE TO BOOTLOADER AUTHORS: do not simply clear
	 * this field.  The purpose of this field is to guarantee
	 * compliance with the x86 boot spec located in
	 * Documentation/x86/boot.rst .  That spec says that the
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
		static struct boot_params scratch;
		char *bp_base = (char *)boot_params;
		char *save_base = (char *)&scratch;
		int i;

		const struct boot_params_to_save to_save[] = {
			BOOT_PARAM_PRESERVE(screen_info),
			BOOT_PARAM_PRESERVE(apm_bios_info),
			BOOT_PARAM_PRESERVE(tboot_addr),
			BOOT_PARAM_PRESERVE(ist_info),
			BOOT_PARAM_PRESERVE(hd0_info),
			BOOT_PARAM_PRESERVE(hd1_info),
			BOOT_PARAM_PRESERVE(sys_desc_table),
			BOOT_PARAM_PRESERVE(olpc_ofw_header),
			BOOT_PARAM_PRESERVE(efi_info),
			BOOT_PARAM_PRESERVE(alt_mem_k),
			BOOT_PARAM_PRESERVE(scratch),
			BOOT_PARAM_PRESERVE(e820_entries),
			BOOT_PARAM_PRESERVE(eddbuf_entries),
			BOOT_PARAM_PRESERVE(edd_mbr_sig_buf_entries),
			BOOT_PARAM_PRESERVE(edd_mbr_sig_buffer),
			BOOT_PARAM_PRESERVE(secure_boot),
			BOOT_PARAM_PRESERVE(hdr),
			BOOT_PARAM_PRESERVE(e820_table),
			BOOT_PARAM_PRESERVE(eddbuf),
		};

		memset(&scratch, 0, sizeof(scratch));

		for (i = 0; i < ARRAY_SIZE(to_save); i++) {
			memcpy(save_base + to_save[i].start,
			       bp_base + to_save[i].start, to_save[i].len);
		}

		memcpy(boot_params, save_base, sizeof(*boot_params));
	}
}

#endif /* _ASM_X86_BOOTPARAM_UTILS_H */
