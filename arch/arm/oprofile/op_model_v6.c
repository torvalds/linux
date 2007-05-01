/**
 * @file op_model_v6.c
 * ARM11 Performance Monitor Driver
 *
 * Based on op_model_xscale.c
 *
 * @remark Copyright 2000-2004 Deepak Saxena <dsaxena@mvista.com>
 * @remark Copyright 2000-2004 MontaVista Software Inc
 * @remark Copyright 2004 Dave Jiang <dave.jiang@intel.com>
 * @remark Copyright 2004 Intel Corporation
 * @remark Copyright 2004 Zwane Mwaikambo <zwane@arm.linux.org.uk>
 * @remark Copyright 2004 OProfile Authors
 *
 * @remark Read the file COPYING
 *
 * @author Tony Lindgren <tony@atomide.com>
 */

/* #define DEBUG */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/oprofile.h>
#include <linux/interrupt.h>
#include <asm/irq.h>
#include <asm/system.h>

#include "op_counter.h"
#include "op_arm_model.h"
#include "op_model_arm11_core.h"

static int irqs[] = {
#ifdef CONFIG_ARCH_OMAP2
	3,
#endif
};

static void armv6_pmu_stop(void)
{
	arm11_stop_pmu();
	arm11_release_interrupts(irqs, ARRAY_SIZE(irqs));
}

static int armv6_pmu_start(void)
{
	int ret;

	ret = arm11_request_interrupts(irqs, ARRAY_SIZE(irqs));
	if (ret >= 0)
		ret = arm11_start_pmu();

	return ret;
}

static int armv6_detect_pmu(void)
{
	return 0;
}

struct op_arm_model_spec op_armv6_spec = {
	.init		= armv6_detect_pmu,
	.num_counters	= 3,
	.setup_ctrs	= arm11_setup_pmu,
	.start		= armv6_pmu_start,
	.stop		= armv6_pmu_stop,
	.name		= "arm/armv6",
};
