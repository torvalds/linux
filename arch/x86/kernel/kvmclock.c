/*  KVM paravirtual clock driver. A clocksource implementation
    Copyright (C) 2008 Glauber de Oliveira Costa, Red Hat Inc.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <linux/clocksource.h>
#include <linux/kvm_para.h>
#include <asm/pvclock.h>
#include <asm/msr.h>
#include <asm/apic.h>
#include <linux/percpu.h>
#include <linux/hardirq.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/mm.h>

#include <asm/mem_encrypt.h>
#include <asm/x86_init.h>
#include <asm/reboot.h>
#include <asm/kvmclock.h>

static int kvmclock __initdata = 1;
static int msr_kvm_system_time __ro_after_init = MSR_KVM_SYSTEM_TIME;
static int msr_kvm_wall_clock __ro_after_init = MSR_KVM_WALL_CLOCK;
static u64 kvm_sched_clock_offset __ro_after_init;

static int __init parse_no_kvmclock(char *arg)
{
	kvmclock = 0;
	return 0;
}
early_param("no-kvmclock", parse_no_kvmclock);

/* Aligned to page sizes to match whats mapped via vsyscalls to userspace */
#define HV_CLOCK_SIZE	(sizeof(struct pvclock_vsyscall_time_info) * NR_CPUS)

static u8 hv_clock_mem[PAGE_ALIGN(HV_CLOCK_SIZE)] __aligned(PAGE_SIZE);

/* The hypervisor will put information about time periodically here */
static struct pvclock_vsyscall_time_info *hv_clock __ro_after_init;
static struct pvclock_wall_clock wall_clock;

/*
 * The wallclock is the time of day when we booted. Since then, some time may
 * have elapsed since the hypervisor wrote the data. So we try to account for
 * that with system time
 */
static void kvm_get_wallclock(struct timespec64 *now)
{
	struct pvclock_vcpu_time_info *vcpu_time;
	int cpu;

	wrmsrl(msr_kvm_wall_clock, slow_virt_to_phys(&wall_clock));

	cpu = get_cpu();

	vcpu_time = &hv_clock[cpu].pvti;
	pvclock_read_wallclock(&wall_clock, vcpu_time, now);

	put_cpu();
}

static int kvm_set_wallclock(const struct timespec64 *now)
{
	return -ENODEV;
}

static u64 kvm_clock_read(void)
{
	struct pvclock_vcpu_time_info *src;
	u64 ret;
	int cpu;

	preempt_disable_notrace();
	cpu = smp_processor_id();
	src = &hv_clock[cpu].pvti;
	ret = pvclock_clocksource_read(src);
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
	if (!stable) {
		pv_time_ops.sched_clock = kvm_clock_read;
		clear_sched_clock_stable();
		return;
	}

	kvm_sched_clock_offset = kvm_clock_read();
	pv_time_ops.sched_clock = kvm_sched_clock_read;

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
	return pvclock_tsc_khz(&hv_clock[0].pvti);
}

static void kvm_get_preset_lpj(void)
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
	struct pvclock_vcpu_time_info *src;
	bool ret = false;

	if (!hv_clock)
		return ret;

	src = &hv_clock[smp_processor_id()].pvti;
	if ((src->flags & PVCLOCK_GUEST_STOPPED) != 0) {
		src->flags &= ~PVCLOCK_GUEST_STOPPED;
		pvclock_touch_watchdogs();
		ret = true;
	}
	return ret;
}

struct clocksource kvm_clock = {
	.name	= "kvm-clock",
	.read	= kvm_clock_get_cycles,
	.rating	= 400,
	.mask	= CLOCKSOURCE_MASK(64),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};
EXPORT_SYMBOL_GPL(kvm_clock);

static void kvm_register_clock(char *txt)
{
	struct pvclock_vcpu_time_info *src;
	int cpu = smp_processor_id();
	u64 pa;

	if (!hv_clock)
		return;

	src = &hv_clock[cpu].pvti;
	pa = slow_virt_to_phys(src) | 0x01ULL;
	wrmsrl(msr_kvm_system_time, pa);
	pr_info("kvm-clock: cpu %d, msr %llx, %s", cpu, pa, txt);
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

	pr_info("kvm-clock: Using msrs %x and %x",
		msr_kvm_system_time, msr_kvm_wall_clock);

	hv_clock = (struct pvclock_vsyscall_time_info *)hv_clock_mem;
	kvm_register_clock("primary cpu clock");
	pvclock_set_pvti_cpu0_va(hv_clock);

	if (kvm_para_has_feature(KVM_FEATURE_CLOCKSOURCE_STABLE_BIT))
		pvclock_set_flags(PVCLOCK_TSC_STABLE_BIT);

	flags = pvclock_read_flags(&hv_clock[0].pvti);
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
	clocksource_register_hz(&kvm_clock, NSEC_PER_SEC);
	pv_info.name = "KVM";
}

int __init kvm_setup_vsyscall_timeinfo(void)
{
#ifdef CONFIG_X86_64
	u8 flags;

	if (!hv_clock)
		return 0;

	flags = pvclock_read_flags(&hv_clock[0].pvti);
	if (!(flags & PVCLOCK_TSC_STABLE_BIT))
		return 1;

	kvm_clock.archdata.vclock_mode = VCLOCK_PVCLOCK;
#endif
	return 0;
}
