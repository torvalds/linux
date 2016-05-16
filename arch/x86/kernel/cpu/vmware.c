/*
 * VMware Detection code.
 *
 * Copyright (C) 2008, VMware, Inc.
 * Author : Alok N Kataria <akataria@vmware.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <linux/dmi.h>
#include <linux/module.h>
#include <asm/div64.h>
#include <asm/x86_init.h>
#include <asm/hypervisor.h>

#define CPUID_VMWARE_INFO_LEAF	0x40000000
#define VMWARE_HYPERVISOR_MAGIC	0x564D5868
#define VMWARE_HYPERVISOR_PORT	0x5658

#define VMWARE_PORT_CMD_GETVERSION	10
#define VMWARE_PORT_CMD_GETHZ		45
#define VMWARE_PORT_CMD_GETVCPU_INFO	68
#define VMWARE_PORT_CMD_LEGACY_X2APIC	3
#define VMWARE_PORT_CMD_VCPU_RESERVED	31

#define VMWARE_PORT(cmd, eax, ebx, ecx, edx)				\
	__asm__("inl (%%dx)" :						\
			"=a"(eax), "=c"(ecx), "=d"(edx), "=b"(ebx) :	\
			"0"(VMWARE_HYPERVISOR_MAGIC),			\
			"1"(VMWARE_PORT_CMD_##cmd),			\
			"2"(VMWARE_HYPERVISOR_PORT), "3"(UINT_MAX) :	\
			"memory");

static inline int __vmware_platform(void)
{
	uint32_t eax, ebx, ecx, edx;
	VMWARE_PORT(GETVERSION, eax, ebx, ecx, edx);
	return eax != (uint32_t)-1 && ebx == VMWARE_HYPERVISOR_MAGIC;
}

static unsigned long vmware_get_tsc_khz(void)
{
	uint64_t tsc_hz, lpj;
	uint32_t eax, ebx, ecx, edx;

	VMWARE_PORT(GETHZ, eax, ebx, ecx, edx);

	tsc_hz = eax | (((uint64_t)ebx) << 32);
	do_div(tsc_hz, 1000);
	BUG_ON(tsc_hz >> 32);
	pr_info("TSC freq read from hypervisor : %lu.%03lu MHz\n",
			 (unsigned long) tsc_hz / 1000,
			 (unsigned long) tsc_hz % 1000);

	if (!preset_lpj) {
		lpj = ((u64)tsc_hz * 1000);
		do_div(lpj, HZ);
		preset_lpj = lpj;
	}

	return tsc_hz;
}

static void __init vmware_platform_setup(void)
{
	uint32_t eax, ebx, ecx, edx;

	VMWARE_PORT(GETHZ, eax, ebx, ecx, edx);

	if (ebx != UINT_MAX)
		x86_platform.calibrate_tsc = vmware_get_tsc_khz;
	else
		pr_warn("Failed to get TSC freq from the hypervisor\n");
}

/*
 * While checking the dmi string information, just checking the product
 * serial key should be enough, as this will always have a VMware
 * specific string when running under VMware hypervisor.
 */
static uint32_t __init vmware_platform(void)
{
	if (boot_cpu_has(X86_FEATURE_HYPERVISOR)) {
		unsigned int eax;
		unsigned int hyper_vendor_id[3];

		cpuid(CPUID_VMWARE_INFO_LEAF, &eax, &hyper_vendor_id[0],
		      &hyper_vendor_id[1], &hyper_vendor_id[2]);
		if (!memcmp(hyper_vendor_id, "VMwareVMware", 12))
			return CPUID_VMWARE_INFO_LEAF;
	} else if (dmi_available && dmi_name_in_serial("VMware") &&
		   __vmware_platform())
		return 1;

	return 0;
}

/*
 * VMware hypervisor takes care of exporting a reliable TSC to the guest.
 * Still, due to timing difference when running on virtual cpus, the TSC can
 * be marked as unstable in some cases. For example, the TSC sync check at
 * bootup can fail due to a marginal offset between vcpus' TSCs (though the
 * TSCs do not drift from each other).  Also, the ACPI PM timer clocksource
 * is not suitable as a watchdog when running on a hypervisor because the
 * kernel may miss a wrap of the counter if the vcpu is descheduled for a
 * long time. To skip these checks at runtime we set these capability bits,
 * so that the kernel could just trust the hypervisor with providing a
 * reliable virtual TSC that is suitable for timekeeping.
 */
static void vmware_set_cpu_features(struct cpuinfo_x86 *c)
{
	set_cpu_cap(c, X86_FEATURE_CONSTANT_TSC);
	set_cpu_cap(c, X86_FEATURE_TSC_RELIABLE);
}

/* Checks if hypervisor supports x2apic without VT-D interrupt remapping. */
static bool __init vmware_legacy_x2apic_available(void)
{
	uint32_t eax, ebx, ecx, edx;
	VMWARE_PORT(GETVCPU_INFO, eax, ebx, ecx, edx);
	return (eax & (1 << VMWARE_PORT_CMD_VCPU_RESERVED)) == 0 &&
	       (eax & (1 << VMWARE_PORT_CMD_LEGACY_X2APIC)) != 0;
}

const __refconst struct hypervisor_x86 x86_hyper_vmware = {
	.name			= "VMware",
	.detect			= vmware_platform,
	.set_cpu_features	= vmware_set_cpu_features,
	.init_platform		= vmware_platform_setup,
	.x2apic_available	= vmware_legacy_x2apic_available,
};
EXPORT_SYMBOL(x86_hyper_vmware);
