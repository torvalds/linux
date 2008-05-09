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
#include <asm/arch_hooks.h>
#include <asm/msr.h>
#include <asm/apic.h>
#include <linux/percpu.h>
#include <asm/reboot.h>

#define KVM_SCALE 22

static int kvmclock = 1;

static int parse_no_kvmclock(char *arg)
{
	kvmclock = 0;
	return 0;
}
early_param("no-kvmclock", parse_no_kvmclock);

/* The hypervisor will put information about time periodically here */
static DEFINE_PER_CPU_SHARED_ALIGNED(struct kvm_vcpu_time_info, hv_clock);
#define get_clock(cpu, field) per_cpu(hv_clock, cpu).field

static inline u64 kvm_get_delta(u64 last_tsc)
{
	int cpu = smp_processor_id();
	u64 delta = native_read_tsc() - last_tsc;
	return (delta * get_clock(cpu, tsc_to_system_mul)) >> KVM_SCALE;
}

static struct kvm_wall_clock wall_clock;
static cycle_t kvm_clock_read(void);
/*
 * The wallclock is the time of day when we booted. Since then, some time may
 * have elapsed since the hypervisor wrote the data. So we try to account for
 * that with system time
 */
unsigned long kvm_get_wallclock(void)
{
	u32 wc_sec, wc_nsec;
	u64 delta;
	struct timespec ts;
	int version, nsec;
	int low, high;

	low = (int)__pa(&wall_clock);
	high = ((u64)__pa(&wall_clock) >> 32);

	delta = kvm_clock_read();

	native_write_msr(MSR_KVM_WALL_CLOCK, low, high);
	do {
		version = wall_clock.wc_version;
		rmb();
		wc_sec = wall_clock.wc_sec;
		wc_nsec = wall_clock.wc_nsec;
		rmb();
	} while ((wall_clock.wc_version != version) || (version & 1));

	delta = kvm_clock_read() - delta;
	delta += wc_nsec;
	nsec = do_div(delta, NSEC_PER_SEC);
	set_normalized_timespec(&ts, wc_sec + delta, nsec);
	/*
	 * Of all mechanisms of time adjustment I've tested, this one
	 * was the champion!
	 */
	return ts.tv_sec + 1;
}

int kvm_set_wallclock(unsigned long now)
{
	return 0;
}

/*
 * This is our read_clock function. The host puts an tsc timestamp each time
 * it updates a new time. Without the tsc adjustment, we can have a situation
 * in which a vcpu starts to run earlier (smaller system_time), but probes
 * time later (compared to another vcpu), leading to backwards time
 */
static cycle_t kvm_clock_read(void)
{
	u64 last_tsc, now;
	int cpu;

	preempt_disable();
	cpu = smp_processor_id();

	last_tsc = get_clock(cpu, tsc_timestamp);
	now = get_clock(cpu, system_time);

	now += kvm_get_delta(last_tsc);
	preempt_enable();

	return now;
}
static struct clocksource kvm_clock = {
	.name = "kvm-clock",
	.read = kvm_clock_read,
	.rating = 400,
	.mask = CLOCKSOURCE_MASK(64),
	.mult = 1 << KVM_SCALE,
	.shift = KVM_SCALE,
	.flags = CLOCK_SOURCE_IS_CONTINUOUS,
};

static int kvm_register_clock(void)
{
	int cpu = smp_processor_id();
	int low, high;
	low = (int)__pa(&per_cpu(hv_clock, cpu)) | 1;
	high = ((u64)__pa(&per_cpu(hv_clock, cpu)) >> 32);

	return native_write_msr_safe(MSR_KVM_SYSTEM_TIME, low, high);
}

#ifdef CONFIG_X86_LOCAL_APIC
static void kvm_setup_secondary_clock(void)
{
	/*
	 * Now that the first cpu already had this clocksource initialized,
	 * we shouldn't fail.
	 */
	WARN_ON(kvm_register_clock());
	/* ok, done with our trickery, call native */
	setup_secondary_APIC_clock();
}
#endif

/*
 * After the clock is registered, the host will keep writing to the
 * registered memory location. If the guest happens to shutdown, this memory
 * won't be valid. In cases like kexec, in which you install a new kernel, this
 * means a random memory location will be kept being written. So before any
 * kind of shutdown from our side, we unregister the clock by writting anything
 * that does not have the 'enable' bit set in the msr
 */
#ifdef CONFIG_KEXEC
static void kvm_crash_shutdown(struct pt_regs *regs)
{
	native_write_msr_safe(MSR_KVM_SYSTEM_TIME, 0, 0);
	native_machine_crash_shutdown(regs);
}
#endif

static void kvm_shutdown(void)
{
	native_write_msr_safe(MSR_KVM_SYSTEM_TIME, 0, 0);
	native_machine_shutdown();
}

void __init kvmclock_init(void)
{
	if (!kvm_para_available())
		return;

	if (kvmclock && kvm_para_has_feature(KVM_FEATURE_CLOCKSOURCE)) {
		if (kvm_register_clock())
			return;
		pv_time_ops.get_wallclock = kvm_get_wallclock;
		pv_time_ops.set_wallclock = kvm_set_wallclock;
		pv_time_ops.sched_clock = kvm_clock_read;
#ifdef CONFIG_X86_LOCAL_APIC
		pv_apic_ops.setup_secondary_clock = kvm_setup_secondary_clock;
#endif
		machine_ops.shutdown  = kvm_shutdown;
#ifdef CONFIG_KEXEC
		machine_ops.crash_shutdown  = kvm_crash_shutdown;
#endif
		clocksource_register(&kvm_clock);
	}
}
