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
#include <linux/init.h>
#include <linux/export.h>
#include <linux/clocksource.h>
#include <asm/div64.h>
#include <asm/x86_init.h>
#include <asm/hypervisor.h>
#include <asm/timer.h>
#include <asm/apic.h>

#undef pr_fmt
#define pr_fmt(fmt)	"vmware: " fmt

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

static unsigned long vmware_tsc_khz __ro_after_init;

static inline int __vmware_platform(void)
{
	uint32_t eax, ebx, ecx, edx;
	VMWARE_PORT(GETVERSION, eax, ebx, ecx, edx);
	return eax != (uint32_t)-1 && ebx == VMWARE_HYPERVISOR_MAGIC;
}

static unsigned long vmware_get_tsc_khz(void)
{
	return vmware_tsc_khz;
}

#ifdef CONFIG_PARAVIRT
static struct cyc2ns_data vmware_cyc2ns __ro_after_init;
static int vmw_sched_clock __initdata = 1;

static __init int setup_vmw_sched_clock(char *s)
{
	vmw_sched_clock = 0;
	return 0;
}
early_param("no-vmw-sched-clock", setup_vmw_sched_clock);

static unsigned long long notrace vmware_sched_clock(void)
{
	unsigned long long ns;

	ns = mul_u64_u32_shr(rdtsc(), vmware_cyc2ns.cyc2ns_mul,
			     vmware_cyc2ns.cyc2ns_shift);
	ns -= vmware_cyc2ns.cyc2ns_offset;
	return ns;
}

static void __init vmware_sched_clock_setup(void)
{
	struct cyc2ns_data *d = &vmware_cyc2ns;
	unsigned long long tsc_now = rdtsc();

	clocks_calc_mult_shift(&d->cyc2ns_mul, &d->cyc2ns_shift,
			       vmware_tsc_khz, NSEC_PER_MSEC, 0);
	d->cyc2ns_offset = mul_u64_u32_shr(tsc_now, d->cyc2ns_mul,
					   d->cyc2ns_shift);

	pv_time_ops.sched_clock = vmware_sched_clock;
	pr_info("using sched offset of %llu ns\n", d->cyc2ns_offset);
}

static void __init vmware_paravirt_ops_setup(void)
{
	pv_info.name = "VMware hypervisor";
	pv_cpu_ops.io_delay = paravirt_nop;

	if (vmware_tsc_khz && vmw_sched_clock)
		vmware_sched_clock_setup();
}
#else
#define vmware_paravirt_ops_setup() do {} while (0)
#endif

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
static void __init vmware_set_capabilities(void)
{
	setup_force_cpu_cap(X86_FEATURE_CONSTANT_TSC);
	setup_force_cpu_cap(X86_FEATURE_TSC_RELIABLE);
}

static void __init vmware_platform_setup(void)
{
	uint32_t eax, ebx, ecx, edx;
	uint64_t lpj, tsc_khz;

	VMWARE_PORT(GETHZ, eax, ebx, ecx, edx);

	if (ebx != UINT_MAX) {
		lpj = tsc_khz = eax | (((uint64_t)ebx) << 32);
		do_div(tsc_khz, 1000);
		WARN_ON(tsc_khz >> 32);
		pr_info("TSC freq read from hypervisor : %lu.%03lu MHz\n",
			(unsigned long) tsc_khz / 1000,
			(unsigned long) tsc_khz % 1000);

		if (!preset_lpj) {
			do_div(lpj, HZ);
			preset_lpj = lpj;
		}

		vmware_tsc_khz = tsc_khz;
		x86_platform.calibrate_tsc = vmware_get_tsc_khz;
		x86_platform.calibrate_cpu = vmware_get_tsc_khz;

#ifdef CONFIG_X86_LOCAL_APIC
		/* Skip lapic calibration since we know the bus frequency. */
		lapic_timer_frequency = ecx / HZ;
		pr_info("Host bus clock speed read from hypervisor : %u Hz\n",
			ecx);
#endif
	} else {
		pr_warn("Failed to get TSC freq from the hypervisor\n");
	}

	vmware_paravirt_ops_setup();

#ifdef CONFIG_X86_IO_APIC
	no_timer_check = 1;
#endif

	vmware_set_capabilities();
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

/* Checks if hypervisor supports x2apic without VT-D interrupt remapping. */
static bool __init vmware_legacy_x2apic_available(void)
{
	uint32_t eax, ebx, ecx, edx;
	VMWARE_PORT(GETVCPU_INFO, eax, ebx, ecx, edx);
	return (eax & (1 << VMWARE_PORT_CMD_VCPU_RESERVED)) == 0 &&
	       (eax & (1 << VMWARE_PORT_CMD_LEGACY_X2APIC)) != 0;
}

const __initconst struct hypervisor_x86 x86_hyper_vmware = {
	.name			= "VMware",
	.detect			= vmware_platform,
	.type			= X86_HYPER_VMWARE,
	.init.init_platform	= vmware_platform_setup,
	.init.x2apic_available	= vmware_legacy_x2apic_available,
};
