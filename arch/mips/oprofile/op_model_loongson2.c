/*
 * Loongson2 performance counter driver for oprofile
 *
 * Copyright (C) 2009 Lemote Inc.
 * Author: Yanhua <yanh@lemote.com>
 * Author: Wu Zhangjin <wuzhangjin@gmail.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */
#include <linux/init.h>
#include <linux/oprofile.h>
#include <linux/interrupt.h>

#include <loongson.h>			/* LOONGSON2_PERFCNT_IRQ */
#include "op_impl.h"

/*
 * a patch should be sent to oprofile with the loongson-specific support.
 * otherwise, the oprofile tool will not recognize this and complain about
 * "cpu_type 'unset' is not valid".
 */
#define LOONGSON2_CPU_TYPE	"mips/loongson2"

#define LOONGSON2_COUNTER1_EVENT(event)	((event & 0x0f) << 5)
#define LOONGSON2_COUNTER2_EVENT(event)	((event & 0x0f) << 9)

#define LOONGSON2_PERFCNT_EXL			(1UL	<<  0)
#define LOONGSON2_PERFCNT_KERNEL		(1UL    <<  1)
#define LOONGSON2_PERFCNT_SUPERVISOR	(1UL    <<  2)
#define LOONGSON2_PERFCNT_USER			(1UL    <<  3)
#define LOONGSON2_PERFCNT_INT_EN		(1UL    <<  4)
#define LOONGSON2_PERFCNT_OVERFLOW		(1ULL   << 31)

/* Loongson2 performance counter register */
#define read_c0_perfctrl() __read_64bit_c0_register($24, 0)
#define write_c0_perfctrl(val) __write_64bit_c0_register($24, 0, val)
#define read_c0_perfcnt() __read_64bit_c0_register($25, 0)
#define write_c0_perfcnt(val) __write_64bit_c0_register($25, 0, val)

static struct loongson2_register_config {
	unsigned int ctrl;
	unsigned long long reset_counter1;
	unsigned long long reset_counter2;
	int cnt1_enabled, cnt2_enabled;
} reg;

static char *oprofid = "LoongsonPerf";
static irqreturn_t loongson2_perfcount_handler(int irq, void *dev_id);
/* Compute all of the registers in preparation for enabling profiling.  */

static void loongson2_reg_setup(struct op_counter_config *cfg)
{
	unsigned int ctrl = 0;

	reg.reset_counter1 = 0;
	reg.reset_counter2 = 0;
	/* Compute the performance counter ctrl word.  */
	/* For now count kernel and user mode */
	if (cfg[0].enabled) {
		ctrl |= LOONGSON2_COUNTER1_EVENT(cfg[0].event);
		reg.reset_counter1 = 0x80000000ULL - cfg[0].count;
	}

	if (cfg[1].enabled) {
		ctrl |= LOONGSON2_COUNTER2_EVENT(cfg[1].event);
		reg.reset_counter2 = (0x80000000ULL - cfg[1].count);
	}

	if (cfg[0].enabled || cfg[1].enabled) {
		ctrl |= LOONGSON2_PERFCNT_EXL | LOONGSON2_PERFCNT_INT_EN;
		if (cfg[0].kernel || cfg[1].kernel)
			ctrl |= LOONGSON2_PERFCNT_KERNEL;
		if (cfg[0].user || cfg[1].user)
			ctrl |= LOONGSON2_PERFCNT_USER;
	}

	reg.ctrl = ctrl;

	reg.cnt1_enabled = cfg[0].enabled;
	reg.cnt2_enabled = cfg[1].enabled;

}

/* Program all of the registers in preparation for enabling profiling.  */

static void loongson2_cpu_setup(void *args)
{
	uint64_t perfcount;

	perfcount = (reg.reset_counter2 << 32) | reg.reset_counter1;
	write_c0_perfcnt(perfcount);
}

static void loongson2_cpu_start(void *args)
{
	/* Start all counters on current CPU */
	if (reg.cnt1_enabled || reg.cnt2_enabled)
		write_c0_perfctrl(reg.ctrl);
}

static void loongson2_cpu_stop(void *args)
{
	/* Stop all counters on current CPU */
	write_c0_perfctrl(0);
	memset(&reg, 0, sizeof(reg));
}

static irqreturn_t loongson2_perfcount_handler(int irq, void *dev_id)
{
	uint64_t counter, counter1, counter2;
	struct pt_regs *regs = get_irq_regs();
	int enabled;

	/*
	 * LOONGSON2 defines two 32-bit performance counters.
	 * To avoid a race updating the registers we need to stop the counters
	 * while we're messing with
	 * them ...
	 */

	/* Check whether the irq belongs to me */
	enabled = read_c0_perfcnt() & LOONGSON2_PERFCNT_INT_EN;
	if (!enabled)
		return IRQ_NONE;
	enabled = reg.cnt1_enabled | reg.cnt2_enabled;
	if (!enabled)
		return IRQ_NONE;

	counter = read_c0_perfcnt();
	counter1 = counter & 0xffffffff;
	counter2 = counter >> 32;

	if (counter1 & LOONGSON2_PERFCNT_OVERFLOW) {
		if (reg.cnt1_enabled)
			oprofile_add_sample(regs, 0);
		counter1 = reg.reset_counter1;
	}
	if (counter2 & LOONGSON2_PERFCNT_OVERFLOW) {
		if (reg.cnt2_enabled)
			oprofile_add_sample(regs, 1);
		counter2 = reg.reset_counter2;
	}

	write_c0_perfcnt((counter2 << 32) | counter1);

	return IRQ_HANDLED;
}

static int __init loongson2_init(void)
{
	return request_irq(LOONGSON2_PERFCNT_IRQ, loongson2_perfcount_handler,
			   IRQF_SHARED, "Perfcounter", oprofid);
}

static void loongson2_exit(void)
{
	write_c0_perfctrl(0);
	free_irq(LOONGSON2_PERFCNT_IRQ, oprofid);
}

struct op_mips_model op_model_loongson2_ops = {
	.reg_setup = loongson2_reg_setup,
	.cpu_setup = loongson2_cpu_setup,
	.init = loongson2_init,
	.exit = loongson2_exit,
	.cpu_start = loongson2_cpu_start,
	.cpu_stop = loongson2_cpu_stop,
	.cpu_type = LOONGSON2_CPU_TYPE,
	.num_counters = 2
};
