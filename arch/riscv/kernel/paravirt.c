// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Ventana Micro Systems Inc.
 */

#define pr_fmt(fmt) "riscv-pv: " fmt

#include <linux/cpuhotplug.h>
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/jump_label.h>
#include <linux/kconfig.h>
#include <linux/kernel.h>
#include <linux/percpu-defs.h>
#include <linux/printk.h>
#include <linux/static_call.h>
#include <linux/types.h>

#include <asm/barrier.h>
#include <asm/page.h>
#include <asm/paravirt.h>
#include <asm/sbi.h>

struct static_key paravirt_steal_enabled;
struct static_key paravirt_steal_rq_enabled;

static u64 native_steal_clock(int cpu)
{
	return 0;
}

DEFINE_STATIC_CALL(pv_steal_clock, native_steal_clock);

static bool steal_acc = true;
static int __init parse_no_stealacc(char *arg)
{
	steal_acc = false;
	return 0;
}

early_param("no-steal-acc", parse_no_stealacc);

DEFINE_PER_CPU(struct sbi_sta_struct, steal_time) __aligned(64);

static bool __init has_pv_steal_clock(void)
{
	if (sbi_spec_version >= sbi_mk_version(2, 0) &&
	    sbi_probe_extension(SBI_EXT_STA) > 0) {
		pr_info("SBI STA extension detected\n");
		return true;
	}

	return false;
}

static int sbi_sta_steal_time_set_shmem(unsigned long lo, unsigned long hi,
					unsigned long flags)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_STA, SBI_EXT_STA_STEAL_TIME_SET_SHMEM,
			lo, hi, flags, 0, 0, 0);
	if (ret.error) {
		if (lo == SBI_STA_SHMEM_DISABLE && hi == SBI_STA_SHMEM_DISABLE)
			pr_warn("Failed to disable steal-time shmem");
		else
			pr_warn("Failed to set steal-time shmem");
		return sbi_err_map_linux_errno(ret.error);
	}

	return 0;
}

static int pv_time_cpu_online(unsigned int cpu)
{
	struct sbi_sta_struct *st = this_cpu_ptr(&steal_time);
	phys_addr_t pa = __pa(st);
	unsigned long lo = (unsigned long)pa;
	unsigned long hi = IS_ENABLED(CONFIG_32BIT) ? upper_32_bits((u64)pa) : 0;

	return sbi_sta_steal_time_set_shmem(lo, hi, 0);
}

static int pv_time_cpu_down_prepare(unsigned int cpu)
{
	return sbi_sta_steal_time_set_shmem(SBI_STA_SHMEM_DISABLE,
					    SBI_STA_SHMEM_DISABLE, 0);
}

static u64 pv_time_steal_clock(int cpu)
{
	struct sbi_sta_struct *st = per_cpu_ptr(&steal_time, cpu);
	u32 sequence;
	u64 steal;

	/*
	 * Check the sequence field before and after reading the steal
	 * field. Repeat the read if it is different or odd.
	 */
	do {
		sequence = READ_ONCE(st->sequence);
		virt_rmb();
		steal = READ_ONCE(st->steal);
		virt_rmb();
	} while ((le32_to_cpu(sequence) & 1) ||
		 sequence != READ_ONCE(st->sequence));

	return le64_to_cpu(steal);
}

int __init pv_time_init(void)
{
	int ret;

	if (!has_pv_steal_clock())
		return 0;

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
				"riscv/pv_time:online",
				pv_time_cpu_online,
				pv_time_cpu_down_prepare);
	if (ret < 0)
		return ret;

	static_call_update(pv_steal_clock, pv_time_steal_clock);

	static_key_slow_inc(&paravirt_steal_enabled);
	if (steal_acc)
		static_key_slow_inc(&paravirt_steal_rq_enabled);

	pr_info("Computing paravirt steal-time\n");

	return 0;
}
