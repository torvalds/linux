// SPDX-License-Identifier: GPL-2.0-only
/*
 * HSM extension and cpu_ops implementation.
 *
 * Copyright (c) 2020 Western Digital Corporation or its affiliates.
 */

#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched/task_stack.h>
#include <asm/cpu_ops.h>
#include <asm/cpu_ops_sbi.h>
#include <asm/sbi.h>
#include <asm/smp.h>

extern char secondary_start_sbi[];
const struct cpu_operations cpu_ops_sbi;

/*
 * Ordered booting via HSM brings one cpu at a time. However, cpu hotplug can
 * be invoked from multiple threads in parallel. Define a per cpu data
 * to handle that.
 */
static DEFINE_PER_CPU(struct sbi_hart_boot_data, boot_data);

static int sbi_hsm_hart_start(unsigned long hartid, unsigned long saddr,
			      unsigned long priv)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_HSM, SBI_EXT_HSM_HART_START,
			hartid, saddr, priv, 0, 0, 0);
	if (ret.error)
		return sbi_err_map_linux_errno(ret.error);
	else
		return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
static int sbi_hsm_hart_stop(void)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_HSM, SBI_EXT_HSM_HART_STOP, 0, 0, 0, 0, 0, 0);

	if (ret.error)
		return sbi_err_map_linux_errno(ret.error);
	else
		return 0;
}

static int sbi_hsm_hart_get_status(unsigned long hartid)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_HSM, SBI_EXT_HSM_HART_STATUS,
			hartid, 0, 0, 0, 0, 0);
	if (ret.error)
		return sbi_err_map_linux_errno(ret.error);
	else
		return ret.value;
}
#endif

static int sbi_cpu_start(unsigned int cpuid, struct task_struct *tidle)
{
	unsigned long boot_addr = __pa_symbol(secondary_start_sbi);
	int hartid = cpuid_to_hartid_map(cpuid);
	unsigned long hsm_data;
	struct sbi_hart_boot_data *bdata = &per_cpu(boot_data, cpuid);

	/* Make sure tidle is updated */
	smp_mb();
	bdata->task_ptr = tidle;
	bdata->stack_ptr = task_stack_page(tidle) + THREAD_SIZE;
	/* Make sure boot data is updated */
	smp_mb();
	hsm_data = __pa(bdata);
	return sbi_hsm_hart_start(hartid, boot_addr, hsm_data);
}

static int sbi_cpu_prepare(unsigned int cpuid)
{
	if (!cpu_ops_sbi.cpu_start) {
		pr_err("cpu start method not defined for CPU [%d]\n", cpuid);
		return -ENODEV;
	}
	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
static int sbi_cpu_disable(unsigned int cpuid)
{
	if (!cpu_ops_sbi.cpu_stop)
		return -EOPNOTSUPP;
	return 0;
}

static void sbi_cpu_stop(void)
{
	int ret;

	ret = sbi_hsm_hart_stop();
	pr_crit("Unable to stop the cpu %u (%d)\n", smp_processor_id(), ret);
}

static int sbi_cpu_is_stopped(unsigned int cpuid)
{
	int rc;
	int hartid = cpuid_to_hartid_map(cpuid);

	rc = sbi_hsm_hart_get_status(hartid);

	if (rc == SBI_HSM_STATE_STOPPED)
		return 0;
	return rc;
}
#endif

const struct cpu_operations cpu_ops_sbi = {
	.name		= "sbi",
	.cpu_prepare	= sbi_cpu_prepare,
	.cpu_start	= sbi_cpu_start,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable	= sbi_cpu_disable,
	.cpu_stop	= sbi_cpu_stop,
	.cpu_is_stopped	= sbi_cpu_is_stopped,
#endif
};
