// SPDX-License-Identifier: GPL-2.0
#include <linux/syscore_ops.h>
#include <linux/suspend.h>
#include <linux/cpu.h>

#include <asm/msr.h>

#define UMWAIT_C02_ENABLE	0

#define UMWAIT_CTRL_VAL(maxtime, c02_disable)				\
	(((maxtime) & MSR_IA32_UMWAIT_CONTROL_TIME_MASK) |		\
	((c02_disable) & MSR_IA32_UMWAIT_CONTROL_C02_DISABLE))

/*
 * Cache IA32_UMWAIT_CONTROL MSR. This is a systemwide control. By default,
 * umwait max time is 100000 in TSC-quanta and C0.2 is enabled
 */
static u32 umwait_control_cached = UMWAIT_CTRL_VAL(100000, UMWAIT_C02_ENABLE);

/* Set IA32_UMWAIT_CONTROL MSR on this CPU to the current global setting. */
static int umwait_cpu_online(unsigned int cpu)
{
	wrmsr(MSR_IA32_UMWAIT_CONTROL, umwait_control_cached, 0);
	return 0;
}

/*
 * On resume, restore IA32_UMWAIT_CONTROL MSR on the boot processor which
 * is the only active CPU at this time. The MSR is set up on the APs via the
 * CPU hotplug callback.
 *
 * This function is invoked on resume from suspend and hibernation. On
 * resume from suspend the restore should be not required, but we neither
 * trust the firmware nor does it matter if the same value is written
 * again.
 */
static void umwait_syscore_resume(void)
{
	wrmsr(MSR_IA32_UMWAIT_CONTROL, umwait_control_cached, 0);
}

static struct syscore_ops umwait_syscore_ops = {
	.resume	= umwait_syscore_resume,
};

static int __init umwait_init(void)
{
	int ret;

	if (!boot_cpu_has(X86_FEATURE_WAITPKG))
		return -ENODEV;

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "umwait:online",
				umwait_cpu_online, NULL);
	if (ret < 0)
		return ret;

	register_syscore_ops(&umwait_syscore_ops);

	return 0;
}
device_initcall(umwait_init);
