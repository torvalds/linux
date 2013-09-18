/*
 *	X86 CPU microcode early update for Linux
 *
 *	Copyright (C) 2012 Fenghua Yu <fenghua.yu@intel.com>
 *			   H Peter Anvin" <hpa@zytor.com>
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
#include <asm/microcode_intel.h>
#include <asm/microcode_amd.h>
#include <asm/processor.h>

#define QCHAR(a, b, c, d) ((a) + ((b) << 8) + ((c) << 16) + ((d) << 24))
#define CPUID_INTEL1 QCHAR('G', 'e', 'n', 'u')
#define CPUID_INTEL2 QCHAR('i', 'n', 'e', 'I')
#define CPUID_INTEL3 QCHAR('n', 't', 'e', 'l')
#define CPUID_AMD1 QCHAR('A', 'u', 't', 'h')
#define CPUID_AMD2 QCHAR('e', 'n', 't', 'i')
#define CPUID_AMD3 QCHAR('c', 'A', 'M', 'D')

#define CPUID_IS(a, b, c, ebx, ecx, edx)	\
		(!((ebx ^ (a))|(edx ^ (b))|(ecx ^ (c))))

/*
 * In early loading microcode phase on BSP, boot_cpu_data is not set up yet.
 * x86_vendor() gets vendor id for BSP.
 *
 * In 32 bit AP case, accessing boot_cpu_data needs linear address. To simplify
 * coding, we still use x86_vendor() to get vendor id for AP.
 *
 * x86_vendor() gets vendor information directly through cpuid.
 */
static int x86_vendor(void)
{
	u32 eax = 0x00000000;
	u32 ebx, ecx = 0, edx;

	native_cpuid(&eax, &ebx, &ecx, &edx);

	if (CPUID_IS(CPUID_INTEL1, CPUID_INTEL2, CPUID_INTEL3, ebx, ecx, edx))
		return X86_VENDOR_INTEL;

	if (CPUID_IS(CPUID_AMD1, CPUID_AMD2, CPUID_AMD3, ebx, ecx, edx))
		return X86_VENDOR_AMD;

	return X86_VENDOR_UNKNOWN;
}

static int x86_family(void)
{
	u32 eax = 0x00000001;
	u32 ebx, ecx = 0, edx;
	int x86;

	native_cpuid(&eax, &ebx, &ecx, &edx);

	x86 = (eax >> 8) & 0xf;
	if (x86 == 15)
		x86 += (eax >> 20) & 0xff;

	return x86;
}

void __init load_ucode_bsp(void)
{
	int vendor, x86;

	if (!have_cpuid_p())
		return;

	vendor = x86_vendor();
	x86 = x86_family();

	switch (vendor) {
	case X86_VENDOR_INTEL:
		if (x86 >= 6)
			load_ucode_intel_bsp();
		break;
	case X86_VENDOR_AMD:
		if (x86 >= 0x10)
			load_ucode_amd_bsp();
		break;
	default:
		break;
	}
}

void load_ucode_ap(void)
{
	int vendor, x86;

	if (!have_cpuid_p())
		return;

	vendor = x86_vendor();
	x86 = x86_family();

	switch (vendor) {
	case X86_VENDOR_INTEL:
		if (x86 >= 6)
			load_ucode_intel_ap();
		break;
	case X86_VENDOR_AMD:
		if (x86 >= 0x10)
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
