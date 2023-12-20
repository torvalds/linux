// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Ventana Micro Systems Inc.
 */

#define pr_fmt(fmt) "riscv-pv: " fmt

#include <linux/cpuhotplug.h>
#include <linux/init.h>
#include <linux/jump_label.h>
#include <linux/printk.h>
#include <linux/static_call.h>
#include <linux/types.h>

#include <asm/paravirt.h>

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

static bool __init has_pv_steal_clock(void)
{
	return false;
}

static int pv_time_cpu_online(unsigned int cpu)
{
	return 0;
}

static int pv_time_cpu_down_prepare(unsigned int cpu)
{
	return 0;
}

static u64 pv_time_steal_clock(int cpu)
{
	return 0;
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
