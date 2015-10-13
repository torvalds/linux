/*
 *	X86 CPU microcode early update for Linux
 *
 *	Copyright (C) 2012 Fenghua Yu <fenghua.yu@intel.com>
 *			   H Peter Anvin" <hpa@zytor.com>
 *		  (C) 2015 Borislav Petkov <bp@alien8.de>
 *
 *	This driver allows to early upgrade microcode on Intel processors
 *	belonging to IA-32 family - PentiumPro, Pentium II,
 *	Pentium III, Xeon, Pentium 4, etc.
 *
 *	Reference: Section 9.11 of Volume 3, IA-32 Intel Architecture
 *	Software Developer's Manual.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */
#include <linux/module.h>
#include <linux/firmware.h>
#include <asm/microcode.h>
#include <asm/microcode_intel.h>
#include <asm/microcode_amd.h>
#include <asm/processor.h>
#include <asm/cmdline.h>

static bool __init check_loader_disabled_bsp(void)
{
#ifdef CONFIG_X86_32
	const char *cmdline = (const char *)__pa_nodebug(boot_command_line);
	const char *opt	    = "dis_ucode_ldr";
	const char *option  = (const char *)__pa_nodebug(opt);
	bool *res = (bool *)__pa_nodebug(&dis_ucode_ldr);

#else /* CONFIG_X86_64 */
	const char *cmdline = boot_command_line;
	const char *option  = "dis_ucode_ldr";
	bool *res = &dis_ucode_ldr;
#endif

	if (cmdline_find_option_bool(cmdline, option))
		*res = true;

	return *res;
}

extern struct builtin_fw __start_builtin_fw[];
extern struct builtin_fw __end_builtin_fw[];

bool get_builtin_firmware(struct cpio_data *cd, const char *name)
{
#ifdef CONFIG_FW_LOADER
	struct builtin_fw *b_fw;

	for (b_fw = __start_builtin_fw; b_fw != __end_builtin_fw; b_fw++) {
		if (!strcmp(name, b_fw->name)) {
			cd->size = b_fw->size;
			cd->data = b_fw->data;
			return true;
		}
	}
#endif
	return false;
}

void __init load_ucode_bsp(void)
{
	int vendor;
	unsigned int family;

	if (check_loader_disabled_bsp())
		return;

	if (!have_cpuid_p())
		return;

	vendor = x86_vendor();
	family = x86_family();

	switch (vendor) {
	case X86_VENDOR_INTEL:
		if (family >= 6)
			load_ucode_intel_bsp();
		break;
	case X86_VENDOR_AMD:
		if (family >= 0x10)
			load_ucode_amd_bsp(family);
		break;
	default:
		break;
	}
}

static bool check_loader_disabled_ap(void)
{
#ifdef CONFIG_X86_32
	return *((bool *)__pa_nodebug(&dis_ucode_ldr));
#else
	return dis_ucode_ldr;
#endif
}

void load_ucode_ap(void)
{
	int vendor, family;

	if (check_loader_disabled_ap())
		return;

	if (!have_cpuid_p())
		return;

	vendor = x86_vendor();
	family = x86_family();

	switch (vendor) {
	case X86_VENDOR_INTEL:
		if (family >= 6)
			load_ucode_intel_ap();
		break;
	case X86_VENDOR_AMD:
		if (family >= 0x10)
			load_ucode_amd_ap();
		break;
	default:
		break;
	}
}

int __init save_microcode_in_initrd(void)
{
	struct cpuinfo_x86 *c = &boot_cpu_data;

	switch (c->x86_vendor) {
	case X86_VENDOR_INTEL:
		if (c->x86 >= 6)
			save_microcode_in_initrd_intel();
		break;
	case X86_VENDOR_AMD:
		if (c->x86 >= 0x10)
			save_microcode_in_initrd_amd();
		break;
	default:
		break;
	}

	return 0;
}

void reload_early_microcode(void)
{
	int vendor, family;

	vendor = x86_vendor();
	family = x86_family();

	switch (vendor) {
	case X86_VENDOR_INTEL:
		if (family >= 6)
			reload_ucode_intel();
		break;
	case X86_VENDOR_AMD:
		if (family >= 0x10)
			reload_ucode_amd();
		break;
	default:
		break;
	}
}
