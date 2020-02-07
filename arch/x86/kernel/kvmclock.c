// SPDX-License-Identifier: GPL-2.0-or-later
/*  KVM paravirtual clock driver. A clocksource implementation
    Copyright (C) 2008 Glauber de Oliveira Costa, Red Hat Inc.
*/

#include <linux/clocksource.h>
#include <linux/kvm_para.h>
#include <asm/pvclock.h>
#include <asm/msr.h>
#include <asm/apic.h>
#include <linux/percpu.h>
#include <linux/hardirq.h>
#include <linux/cpuhotplug.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/set_memory.h>

#include <asm/hypervisor.h>
#include <asm/mem_encrypt.h>
#include <asm/x86_init.h>
#include <asm/reboot.h>
#include <asm/kvmclock.h>

static int kvmclock __initdata = 1;
static int kvmclock_vsyscall __initdata = 1;
static int msr_kvm_system_time __ro_after_init = MSR_KVM_SYSTEM_TIME;
static int msr_kvm_wall_clock __ro_after_init = MSR_KVM_WALL_CLOCK;
static u64 kvm_sched_clock_offset __ro_after_init;

static int __init parse_no_kvmclock(char *arg)
{
	kvmclock = 0;
	return 0;
}
early_param("no-kvmclock", parse_no_kvmclock);

static int __init parse_no_kvmclock_vsyscall(char *arg)
{
	kvmclock_vsyscall = 0;
	return 0;
}
early_param("no-kvmclock-vsyscall", parse_no_kvmclock_vsyscall);

/* Aligned to page sizes to match whats mapped via vsyscalls to userspace */
#define HV_CLOCK_SIZE	(sizeof(struct pvclock_vsyscall_time_info) * NR_CPUS)
#define HVC_BOOT_ARRAY_SIZE \
	(PAGE_SIZE / sizeof(struct pvclock_vsyscall_time_info))

static struct pvclock_vsyscall_time_info
			hv_clock_boot[HVC_BOOT_ARRAY_SIZE] __bss_decrypted __aligned(PAGE_SIZE);
static struct pvclock_wall_clock wall_clock __bss_decrypted;
static DEFINE_PER_CPU(struct pvclock_vsyscall_time_info *, hv_clock_per_cpu);
static struct pvclock_vsyscall_time_info *hvclock_mem;

static inline struct pvclock_vcpu_time_info *this_cpu_pvti(void)
{
	return &this_cpu_read(hv_clock_per_cpu)->pvti;
}

static inline struct pvclock_vsyscall_time_info *this_cpu_hvclock(void)
{
	return this_cpu_read(hv_clock_per_cpu);
}

/*
 * The wallclock is the time of day when we booted. Since then, some time may
 * have elapsed since the hypervisor wrote the data. So we try to account for
 * that with system time
 */
static void kvm_get_wallclock(struct timespec64 *now)
{
	wrmsrl(msr_kvm_wall_clock, slow_virt_to_phys(&wall_clock));
	preempt_disable();
	pvclock_read_wallclock(&wall_clock, this_cpu_pvti(), now);
	preempt_enable();
}

static int kvm_set_wallclock(const struct timespec64 *now)
{
	return -ENODEV;
}

static u64 kvm_clock_read(void)
{
	u64 ret;

	preempt_disable_notrace();
	ret = pvclock_clocksource_read(this_cpu_pvti());
	preempt_enable_notrace();
	return ret;
}

static u64 kvm_clock_get_cycles(struct clocksource *cs)
{
	return kvm_clock_read();
}

static u64 kvm_sched_clock_read(void)
{
	return kvm_clock_read() - kvm_sched_clock_offset;
}

static inline void kvm_sched_clock_init(bool stable)
{
	if (!stable)
		clear_sched_clock_stable();
	kvm_sched_clock_offset = kvm_clock_read();
	pv_ops.time.sched_clock = kvm_sched_clock_read;

	pr_info("kvm-clock: using sched offset of %llu cycles",
		kvm_sched_clock_offset);

	BUILD_BUG_ON(sizeof(kvm_sched_clock_offset) >
		sizeof(((struct pvclock_vcpu_time_info *)NULL)->system_time));
}

/*
 * If we don't do that, there is the possibility that the guest
 * will calibrate under heavy load - thus, getting a lower lpj -
 * and execute the delays themselves without load. This is wrong,
 * because no delay loop can finish beforehand.
 * Any heuristics is subject to fail, because ultimately, a large
 * poll of guests can be running and trouble each other. So we preset
 * lpj here
 */
static unsigned long kvm_get_tsc_khz(void)
{
	setup_force_cpu_cap(X86_FEATURE_TSC_KNOWN_FREQ);
	return pvclock_tsc_khz(this_cpu_pvti());
}

static void __init kvm_get_preset_lpj(void)
{
	unsigned long khz;
	u64 lpj;

	khz = kvm_get_tsc_khz();

	lpj = ((u64)khz * 1000);
	do_div(lpj, HZ);
	preset_lpj = lpj;
}

bool kvm_check_and_clear_guest_paused(void)
{
	struct pvclock_vsyscall_time_info *src = this_cpu_hvclock();
	bool ret = false;

	if (!src)
		return ret;

	if ((src->pvti.flags & PVCLOCK_GUEST_STOPPED) != 0) {
		src->pvti.flags &= ~PVCLOCK_GUEST_STOPPED;
		pvclock_touch_watchdogs();
		ret = true;
	}
	return ret;
}

static int kvm_cs_enable(struct clocksource *cs)
{
	vclocks_set_used(VCLOCK_PVCLOCK);
	return 0;
}

struct clocksource kvm_clock = {
	.name	= "kvm-clock",
	.read	= kvm_clock_get_cycles,
	.rating	= 400,
	.mask	= CLOCKSOURCE_MASK(64),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
	.enable	= kvm_cs_enable,
};
EXPORT_SYMBOL_GPL(kvm_clock);

static void kvm_register_clock(char *txt)
{
	struct pvclock_vsyscall_time_info *src = this_cpu_hvclock();
	u64 pa;

	if (!src)
		return;

	pa = slow_virt_to_phys(&src->pvti) | 0x01ULL;
	wrmsrl(msr_kvm_system_time, pa);
	pr_info("kvm-clock: cpu %d, msr %llx, %s", smp_processor_id(), pa, txt);
}

static void kvm_save_sched_clock_state(void)
{
}

static void kvm_restore_sched_clock_state(void)
{
	kvm_register_clock("primary cpu clock, resume");
}

#ifdef CONFIG_X86_LOCAL_APIC
static void kvm_setup_secondary_clock(void)
{
	kvm_register_clock("secondary cpu clock");
}
#endif

/*
 * After the clock is registered, the host will keep writing to the
 * registered memory location. If the guest happens to shutdown, this memory
 * won't be valid. In cases like kexec, in which you install a new kernel, this
 * means a random memory location will be kept being written. So before any
 * kind of shutdown from our side, we unregister the clock by writing anything
 * that does not have the 'enable' bit set in the msr
 */
#ifdef CONFIG_KEXEC_CORE
static void kvm_crash_shutdown(struct pt_regs *regs)
{
	native_write_msr(msr_kvm_system_time, 0, 0);
	kvm_disable_steal_time();
	native_machine_crash_shutdown(regs);
}
#endif

static void kvm_shutdown(void)
{
	native_write_msr(msr_kvm_system_time, 0, 0);
	kvm_disable_steal_time();
	native_machine_shutdown();
}

static void __init kvmclock_init_mem(void)
{
	unsigned long ncpus;
	unsigned int order;
	struct page *p;
	int r;

	if (HVC_BOOT_ARRAY_SIZE >= num_possible_cpus())
		return;

	ncpus = num_possible_cpus() - HVC_BOOT_ARRAY_SIZE;
	order = get_order(ncpus * sizeof(*hvclock_mem));

	p = alloc_pages(GFP_KERNEL, order);
	if (!p) {
		pr_warn("%s: failed to alloc %d pages", __func__, (1U << order));
		return;
	}

	hvclock_mem = page_address(p);

	/*
	 * hvclock is shared between the guest and the hypervisor, must
	 * be mapped decrypted.
	 */
	if (sev_active()) {
		r = set_memory_decrypted((unsigned long) hvclock_mem,
					 1UL << order);
		if (r) {
			__free_pages(p, order);
			hvclock_mem = NULL;
			pr_warn("kvmclock: set_memory_decrypted() failed. Disabling\n");
			return;
		}
	}

	memset(hvclock_mem, 0, PAGE_SIZE << order);
}

static int __init kvm_setup_vsyscall_timeinfo(void)
{
#ifdef CONFIG_X86_64
	u8 flags;

	if (!per_cpu(hv_clock_per_cpu, 0) || !kvmclock_vsyscall)
		return 0;

	flags = pvclock_read_flags(&hv_clock_boot[0].pvti);
	if (!(flags & PVCLOCK_TSC_STABLE_BIT))
		return 0;

	kvm_clock.archdata.vclock_mode = VCLOCK_PVCLOCK;
#endif

	kvmclock_init_mem();

	return 0;
}
early_initcall(kvm_setup_vsyscall_timeinfo);

static int kvmclock_setup_percpu(unsigned int cpu)
{
	struct pvclock_vsyscall_time_info *p = per_cpu(hv_clock_per_cpu, cpu);

	/*
	 * The per cpu area setup replicates CPU0 data to all cpu
	 * pointers. So carefully check. CPU0 has been set up in init
	 * already.
	 */
	if (!cpu || (p && p != per_cpu(hv_clock_per_cpu, 0)))
		return 0;

	/* Use the static page for the first CPUs, allocate otherwise */
	if (cpu < HVC_BOOT_ARRAY_SIZE)
		p = &hv_clock_boot[cpu];
	else if (hvclock_mem)
		p = hvclock_mem + cpu - HVC_BOOT_ARRAY_SIZE;
	else
		return -ENOMEM;

	per_cpu(hv_clock_per_cpu, cpu) = p;
	return p ? 0 : -ENOMEM;
}

void __init kvmclock_init(void)
{
	u8 flags;

	if (!kvm_para_available() || !kvmclock)
		return;

	if (kvm_para_has_feature(KVM_FEATURE_CLOCKSOURCE2)) {
		msr_kvm_system_time = MSR_KVM_SYSTEM_TIME_NEW;
		msr_kvm_wall_clock = MSR_KVM_WALL_CLOCK_NEW;
	} else if (!kvm_para_has_feature(KVM_FEATURE_CLOCKSOURCE)) {
		return;
	}

	if (cpuhp_setup_state(CPUHP_BP_PREPARE_DYN, "kvmclock:setup_percpu",
			      kvmclock_setup_percpu, NULL) < 0) {
		return;
	}

	pr_info("kvm-clock: Using msrs %x and %x",
		msr_kvm_system_time, msr_kvm_wall_clock);

	this_cpu_write(hv_clock_per_cpu, &hv_clock_boot[0]);
	kvm_register_clock("primary cpu clock");
	pvclock_set_pvti_cpu0_va(hv_clock_boot);

	if (kvm_para_has_feature(KVM_FEATURE_CLOCKSOURCE_STABLE_BIT))
		pvclock_set_flags(PVCLOCK_TSC_STABLE_BIT);

	flags = pvclock_read_flags(&hv_clock_boot[0].pvti);
	kvm_sched_clock_init(flags & PVCLOCK_TSC_STABLE_BIT);

	x86_platform.calibrate_tsc = kvm_get_tsc_khz;
	x86_platform.calibrate_cpu = kvm_get_tsc_khz;
	x86_platform.get_wallclock = kvm_get_wallclock;
	x86_platform.set_wallclock = kvm_set_wallclock;
#ifdef CONFIG_X86_LOCAL_APIC
	x86_cpuinit.early_percpu_clock_init = kvm_setup_secondary_clock;
#endif
	x86_platform.save_sched_clock_state = kvm_save_sched_clock_state;
	x86_platform.restore_sched_clock_state = kvm_restore_sched_clock_state;
	machine_ops.shutdown  = kvm_shutdown;
#ifdef CONFIG_KEXEC_CORE
	machine_ops.crash_shutdown  = kvm_crash_shutdown;
#endif
	kvm_get_preset_lpj();

	/*
	 * X86_FEATURE_NONSTOP_TSC is TSC runs at constant rate
	 * with P/T states and does not stop in deep C-states.
	 *
	 * Invariant TSC exposed by host means kvmclock is not necessary:
	 * can use TSC as clocksource.
	 *
	 */
	if (boot_cpu_has(X86_FEATURE_CONSTANT_TSC) &&
	    boot_cpu_has(X86_FEATURE_NONSTOP_TSC) &&
	    !check_tsc_unstable())
		kvm_clock.rating = 299;

	clocksource_register_hz(&kvm_clock, NSEC_PER_SEC);
	pv_info.name = "KVM";
}
