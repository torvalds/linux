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
#include <linux/cpu.h>
#include <linux/reboot.h>
#include <linux/static_call.h>
#include <asm/div64.h>
#include <asm/x86_init.h>
#include <asm/hypervisor.h>
#include <asm/timer.h>
#include <asm/apic.h>
#include <asm/vmware.h>
#include <asm/svm.h>

#undef pr_fmt
#define pr_fmt(fmt)	"vmware: " fmt

#define CPUID_VMWARE_INFO_LEAF               0x40000000
#define CPUID_VMWARE_FEATURES_LEAF           0x40000010

#define GETVCPU_INFO_LEGACY_X2APIC           BIT(3)
#define GETVCPU_INFO_VCPU_RESERVED           BIT(31)

#define STEALCLOCK_NOT_AVAILABLE (-1)
#define STEALCLOCK_DISABLED        0
#define STEALCLOCK_ENABLED         1

struct vmware_steal_time {
	union {
		u64 clock;	/* stolen time counter in units of vtsc */
		struct {
			/* only for little-endian */
			u32 clock_low;
			u32 clock_high;
		};
	};
	u64 reserved[7];
};

static unsigned long vmware_tsc_khz __ro_after_init;
static u8 vmware_hypercall_mode     __ro_after_init;

unsigned long vmware_hypercall_slow(unsigned long cmd,
				    unsigned long in1, unsigned long in3,
				    unsigned long in4, unsigned long in5,
				    u32 *out1, u32 *out2, u32 *out3,
				    u32 *out4, u32 *out5)
{
	unsigned long out0, rbx, rcx, rdx, rsi, rdi;

	switch (vmware_hypercall_mode) {
	case CPUID_VMWARE_FEATURES_ECX_VMCALL:
		asm_inline volatile ("vmcall"
				: "=a" (out0), "=b" (rbx), "=c" (rcx),
				"=d" (rdx), "=S" (rsi), "=D" (rdi)
				: "a" (VMWARE_HYPERVISOR_MAGIC),
				"b" (in1),
				"c" (cmd),
				"d" (in3),
				"S" (in4),
				"D" (in5)
				: "cc", "memory");
		break;
	case CPUID_VMWARE_FEATURES_ECX_VMMCALL:
		asm_inline volatile ("vmmcall"
				: "=a" (out0), "=b" (rbx), "=c" (rcx),
				"=d" (rdx), "=S" (rsi), "=D" (rdi)
				: "a" (VMWARE_HYPERVISOR_MAGIC),
				"b" (in1),
				"c" (cmd),
				"d" (in3),
				"S" (in4),
				"D" (in5)
				: "cc", "memory");
		break;
	default:
		asm_inline volatile ("movw %[port], %%dx; inl (%%dx), %%eax"
				: "=a" (out0), "=b" (rbx), "=c" (rcx),
				"=d" (rdx), "=S" (rsi), "=D" (rdi)
				: [port] "i" (VMWARE_HYPERVISOR_PORT),
				"a" (VMWARE_HYPERVISOR_MAGIC),
				"b" (in1),
				"c" (cmd),
				"d" (in3),
				"S" (in4),
				"D" (in5)
				: "cc", "memory");
		break;
	}

	if (out1)
		*out1 = rbx;
	if (out2)
		*out2 = rcx;
	if (out3)
		*out3 = rdx;
	if (out4)
		*out4 = rsi;
	if (out5)
		*out5 = rdi;

	return out0;
}

static inline int __vmware_platform(void)
{
	u32 eax, ebx, ecx;

	eax = vmware_hypercall3(VMWARE_CMD_GETVERSION, 0, &ebx, &ecx);
	return eax != UINT_MAX && ebx == VMWARE_HYPERVISOR_MAGIC;
}

static unsigned long vmware_get_tsc_khz(void)
{
	return vmware_tsc_khz;
}

#ifdef CONFIG_PARAVIRT
static struct cyc2ns_data vmware_cyc2ns __ro_after_init;
static bool vmw_sched_clock __initdata = true;
static DEFINE_PER_CPU_DECRYPTED(struct vmware_steal_time, vmw_steal_time) __aligned(64);
static bool has_steal_clock;
static bool steal_acc __initdata = true; /* steal time accounting */

static __init int setup_vmw_sched_clock(char *s)
{
	vmw_sched_clock = false;
	return 0;
}
early_param("no-vmw-sched-clock", setup_vmw_sched_clock);

static __init int parse_no_stealacc(char *arg)
{
	steal_acc = false;
	return 0;
}
early_param("no-steal-acc", parse_no_stealacc);

static noinstr u64 vmware_sched_clock(void)
{
	unsigned long long ns;

	ns = mul_u64_u32_shr(rdtsc(), vmware_cyc2ns.cyc2ns_mul,
			     vmware_cyc2ns.cyc2ns_shift);
	ns -= vmware_cyc2ns.cyc2ns_offset;
	return ns;
}

static void __init vmware_cyc2ns_setup(void)
{
	struct cyc2ns_data *d = &vmware_cyc2ns;
	unsigned long long tsc_now = rdtsc();

	clocks_calc_mult_shift(&d->cyc2ns_mul, &d->cyc2ns_shift,
			       vmware_tsc_khz, NSEC_PER_MSEC, 0);
	d->cyc2ns_offset = mul_u64_u32_shr(tsc_now, d->cyc2ns_mul,
					   d->cyc2ns_shift);

	pr_info("using clock offset of %llu ns\n", d->cyc2ns_offset);
}

static int vmware_cmd_stealclock(u32 addr_hi, u32 addr_lo)
{
	u32 info;

	return vmware_hypercall5(VMWARE_CMD_STEALCLOCK, 0, 0, addr_hi, addr_lo,
				 &info);
}

static bool stealclock_enable(phys_addr_t pa)
{
	return vmware_cmd_stealclock(upper_32_bits(pa),
				     lower_32_bits(pa)) == STEALCLOCK_ENABLED;
}

static int __stealclock_disable(void)
{
	return vmware_cmd_stealclock(0, 1);
}

static void stealclock_disable(void)
{
	__stealclock_disable();
}

static bool vmware_is_stealclock_available(void)
{
	return __stealclock_disable() != STEALCLOCK_NOT_AVAILABLE;
}

/**
 * vmware_steal_clock() - read the per-cpu steal clock
 * @cpu:            the cpu number whose steal clock we want to read
 *
 * The function reads the steal clock if we are on a 64-bit system, otherwise
 * reads it in parts, checking that the high part didn't change in the
 * meantime.
 *
 * Return:
 *      The steal clock reading in ns.
 */
static u64 vmware_steal_clock(int cpu)
{
	struct vmware_steal_time *steal = &per_cpu(vmw_steal_time, cpu);
	u64 clock;

	if (IS_ENABLED(CONFIG_64BIT))
		clock = READ_ONCE(steal->clock);
	else {
		u32 initial_high, low, high;

		do {
			initial_high = READ_ONCE(steal->clock_high);
			/* Do not reorder initial_high and high readings */
			virt_rmb();
			low = READ_ONCE(steal->clock_low);
			/* Keep low reading in between */
			virt_rmb();
			high = READ_ONCE(steal->clock_high);
		} while (initial_high != high);

		clock = ((u64)high << 32) | low;
	}

	return mul_u64_u32_shr(clock, vmware_cyc2ns.cyc2ns_mul,
			     vmware_cyc2ns.cyc2ns_shift);
}

static void vmware_register_steal_time(void)
{
	int cpu = smp_processor_id();
	struct vmware_steal_time *st = &per_cpu(vmw_steal_time, cpu);

	if (!has_steal_clock)
		return;

	if (!stealclock_enable(slow_virt_to_phys(st))) {
		has_steal_clock = false;
		return;
	}

	pr_info("vmware-stealtime: cpu %d, pa %llx\n",
		cpu, (unsigned long long) slow_virt_to_phys(st));
}

static void vmware_disable_steal_time(void)
{
	if (!has_steal_clock)
		return;

	stealclock_disable();
}

static void vmware_guest_cpu_init(void)
{
	if (has_steal_clock)
		vmware_register_steal_time();
}

static void vmware_pv_guest_cpu_reboot(void *unused)
{
	vmware_disable_steal_time();
}

static int vmware_pv_reboot_notify(struct notifier_block *nb,
				unsigned long code, void *unused)
{
	if (code == SYS_RESTART)
		on_each_cpu(vmware_pv_guest_cpu_reboot, NULL, 1);
	return NOTIFY_DONE;
}

static struct notifier_block vmware_pv_reboot_nb = {
	.notifier_call = vmware_pv_reboot_notify,
};

#ifdef CONFIG_SMP
static void __init vmware_smp_prepare_boot_cpu(void)
{
	vmware_guest_cpu_init();
	native_smp_prepare_boot_cpu();
}

static int vmware_cpu_online(unsigned int cpu)
{
	local_irq_disable();
	vmware_guest_cpu_init();
	local_irq_enable();
	return 0;
}

static int vmware_cpu_down_prepare(unsigned int cpu)
{
	local_irq_disable();
	vmware_disable_steal_time();
	local_irq_enable();
	return 0;
}
#endif

static __init int activate_jump_labels(void)
{
	if (has_steal_clock) {
		static_key_slow_inc(&paravirt_steal_enabled);
		if (steal_acc)
			static_key_slow_inc(&paravirt_steal_rq_enabled);
	}

	return 0;
}
arch_initcall(activate_jump_labels);

static void __init vmware_paravirt_ops_setup(void)
{
	pv_info.name = "VMware hypervisor";
	pv_ops.cpu.io_delay = paravirt_nop;

	if (vmware_tsc_khz == 0)
		return;

	vmware_cyc2ns_setup();

	if (vmw_sched_clock)
		paravirt_set_sched_clock(vmware_sched_clock);

	if (vmware_is_stealclock_available()) {
		has_steal_clock = true;
		static_call_update(pv_steal_clock, vmware_steal_clock);

		/* We use reboot notifier only to disable steal clock */
		register_reboot_notifier(&vmware_pv_reboot_nb);

#ifdef CONFIG_SMP
		smp_ops.smp_prepare_boot_cpu =
			vmware_smp_prepare_boot_cpu;
		if (cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
					      "x86/vmware:online",
					      vmware_cpu_online,
					      vmware_cpu_down_prepare) < 0)
			pr_err("vmware_guest: Failed to install cpu hotplug callbacks\n");
#else
		vmware_guest_cpu_init();
#endif
	}
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
	if (vmware_tsc_khz)
		setup_force_cpu_cap(X86_FEATURE_TSC_KNOWN_FREQ);
	if (vmware_hypercall_mode == CPUID_VMWARE_FEATURES_ECX_VMCALL)
		setup_force_cpu_cap(X86_FEATURE_VMCALL);
	else if (vmware_hypercall_mode == CPUID_VMWARE_FEATURES_ECX_VMMCALL)
		setup_force_cpu_cap(X86_FEATURE_VMW_VMMCALL);
}

static void __init vmware_platform_setup(void)
{
	u32 eax, ebx, ecx;
	u64 lpj, tsc_khz;

	eax = vmware_hypercall3(VMWARE_CMD_GETHZ, UINT_MAX, &ebx, &ecx);

	if (ebx != UINT_MAX) {
		lpj = tsc_khz = eax | (((u64)ebx) << 32);
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
		lapic_timer_period = ecx / HZ;
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

static u8 __init vmware_select_hypercall(void)
{
	int eax, ebx, ecx, edx;

	cpuid(CPUID_VMWARE_FEATURES_LEAF, &eax, &ebx, &ecx, &edx);
	return (ecx & (CPUID_VMWARE_FEATURES_ECX_VMMCALL |
		       CPUID_VMWARE_FEATURES_ECX_VMCALL));
}

/*
 * While checking the dmi string information, just checking the product
 * serial key should be enough, as this will always have a VMware
 * specific string when running under VMware hypervisor.
 * If !boot_cpu_has(X86_FEATURE_HYPERVISOR), vmware_hypercall_mode
 * intentionally defaults to 0.
 */
static u32 __init vmware_platform(void)
{
	if (boot_cpu_has(X86_FEATURE_HYPERVISOR)) {
		unsigned int eax;
		unsigned int hyper_vendor_id[3];

		cpuid(CPUID_VMWARE_INFO_LEAF, &eax, &hyper_vendor_id[0],
		      &hyper_vendor_id[1], &hyper_vendor_id[2]);
		if (!memcmp(hyper_vendor_id, "VMwareVMware", 12)) {
			if (eax >= CPUID_VMWARE_FEATURES_LEAF)
				vmware_hypercall_mode =
					vmware_select_hypercall();

			pr_info("hypercall mode: 0x%02x\n",
				(unsigned int) vmware_hypercall_mode);

			return CPUID_VMWARE_INFO_LEAF;
		}
	} else if (dmi_available && dmi_name_in_serial("VMware") &&
		   __vmware_platform())
		return 1;

	return 0;
}

/* Checks if hypervisor supports x2apic without VT-D interrupt remapping. */
static bool __init vmware_legacy_x2apic_available(void)
{
	u32 eax;

	eax = vmware_hypercall1(VMWARE_CMD_GETVCPU_INFO, 0);
	return !(eax & GETVCPU_INFO_VCPU_RESERVED) &&
		(eax & GETVCPU_INFO_LEGACY_X2APIC);
}

#ifdef CONFIG_INTEL_TDX_GUEST
/*
 * TDCALL[TDG.VP.VMCALL] uses %rax (arg0) and %rcx (arg2). Therefore,
 * we remap those registers to %r12 and %r13, respectively.
 */
unsigned long vmware_tdx_hypercall(unsigned long cmd,
				   unsigned long in1, unsigned long in3,
				   unsigned long in4, unsigned long in5,
				   u32 *out1, u32 *out2, u32 *out3,
				   u32 *out4, u32 *out5)
{
	struct tdx_module_args args = {};

	if (!hypervisor_is_type(X86_HYPER_VMWARE)) {
		pr_warn_once("Incorrect usage\n");
		return ULONG_MAX;
	}

	if (cmd & ~VMWARE_CMD_MASK) {
		pr_warn_once("Out of range command %lx\n", cmd);
		return ULONG_MAX;
	}

	args.rbx = in1;
	args.rdx = in3;
	args.rsi = in4;
	args.rdi = in5;
	args.r10 = VMWARE_TDX_VENDOR_LEAF;
	args.r11 = VMWARE_TDX_HCALL_FUNC;
	args.r12 = VMWARE_HYPERVISOR_MAGIC;
	args.r13 = cmd;
	/* CPL */
	args.r15 = 0;

	__tdx_hypercall(&args);

	if (out1)
		*out1 = args.rbx;
	if (out2)
		*out2 = args.r13;
	if (out3)
		*out3 = args.rdx;
	if (out4)
		*out4 = args.rsi;
	if (out5)
		*out5 = args.rdi;

	return args.r12;
}
EXPORT_SYMBOL_GPL(vmware_tdx_hypercall);
#endif

#ifdef CONFIG_AMD_MEM_ENCRYPT
static void vmware_sev_es_hcall_prepare(struct ghcb *ghcb,
					struct pt_regs *regs)
{
	/* Copy VMWARE specific Hypercall parameters to the GHCB */
	ghcb_set_rip(ghcb, regs->ip);
	ghcb_set_rbx(ghcb, regs->bx);
	ghcb_set_rcx(ghcb, regs->cx);
	ghcb_set_rdx(ghcb, regs->dx);
	ghcb_set_rsi(ghcb, regs->si);
	ghcb_set_rdi(ghcb, regs->di);
	ghcb_set_rbp(ghcb, regs->bp);
}

static bool vmware_sev_es_hcall_finish(struct ghcb *ghcb, struct pt_regs *regs)
{
	if (!(ghcb_rbx_is_valid(ghcb) &&
	      ghcb_rcx_is_valid(ghcb) &&
	      ghcb_rdx_is_valid(ghcb) &&
	      ghcb_rsi_is_valid(ghcb) &&
	      ghcb_rdi_is_valid(ghcb) &&
	      ghcb_rbp_is_valid(ghcb)))
		return false;

	regs->bx = ghcb_get_rbx(ghcb);
	regs->cx = ghcb_get_rcx(ghcb);
	regs->dx = ghcb_get_rdx(ghcb);
	regs->si = ghcb_get_rsi(ghcb);
	regs->di = ghcb_get_rdi(ghcb);
	regs->bp = ghcb_get_rbp(ghcb);

	return true;
}
#endif

const __initconst struct hypervisor_x86 x86_hyper_vmware = {
	.name				= "VMware",
	.detect				= vmware_platform,
	.type				= X86_HYPER_VMWARE,
	.init.init_platform		= vmware_platform_setup,
	.init.x2apic_available		= vmware_legacy_x2apic_available,
#ifdef CONFIG_AMD_MEM_ENCRYPT
	.runtime.sev_es_hcall_prepare	= vmware_sev_es_hcall_prepare,
	.runtime.sev_es_hcall_finish	= vmware_sev_es_hcall_finish,
#endif
};
