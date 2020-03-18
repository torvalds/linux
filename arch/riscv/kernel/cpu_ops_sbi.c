// SPDX-License-Identifier: GPL-2.0-only
/*
 * HSM extension and cpu_ops implementation.
 *
 * Copyright (c) 2020 Western Digital Corporation or its affiliates.
 */

#include <linux/init.h>
#include <linux/mm.h>
#include <asm/cpu_ops.h>
#include <asm/sbi.h>
#include <asm/smp.h>

extern char secondary_start_sbi[];
const struct cpu_operations cpu_ops_sbi;

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
	int rc;
	unsigned long boot_addr = __pa_symbol(secondary_start_sbi);
	int hartid = cpuid_to_hartid_map(cpuid);

	cpu_update_secondary_bootdata(cpuid, tidle);
	rc = sbi_hsm_hart_start(hartid, boot_addr, 0);

	return rc;
}

static int sbi_cpu_prepare(unsigned int cpuid)
{
	if (!cpu_ops_sbi.cpu_start) {
		pr_err("cpu start method not defined for CPU [%d]\n", cpuid);
		return -ENODEV;
	}
	return 0;
}

const struct cpu_operations cpu_ops_sbi = {
	.name		= "sbi",
	.cpu_prepare	= sbi_cpu_prepare,
	.cpu_start	= sbi_cpu_start,
};
