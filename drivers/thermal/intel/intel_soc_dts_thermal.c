// SPDX-License-Identifier: GPL-2.0-only
/*
 * intel_soc_dts_thermal.c
 * Copyright (c) 2014, Intel Corporation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include "intel_soc_dts_iosf.h"

#define CRITICAL_OFFSET_FROM_TJ_MAX	5000

static int crit_offset = CRITICAL_OFFSET_FROM_TJ_MAX;
module_param(crit_offset, int, 0644);
MODULE_PARM_DESC(crit_offset,
	"Critical Temperature offset from tj max in millidegree Celsius.");

/* IRQ 86 is a fixed APIC interrupt for BYT DTS Aux threshold notifications */
#define BYT_SOC_DTS_APIC_IRQ	86

static int soc_dts_thres_gsi;
static int soc_dts_thres_irq;
static struct intel_soc_dts_sensors *soc_dts;

static irqreturn_t soc_irq_thread_fn(int irq, void *dev_data)
{
	pr_debug("proc_thermal_interrupt\n");
	intel_soc_dts_iosf_interrupt_handler(soc_dts);

	return IRQ_HANDLED;
}

static const struct x86_cpu_id soc_thermal_ids[] = {
	X86_MATCH_VFM(INTEL_ATOM_SILVERMONT, BYT_SOC_DTS_APIC_IRQ),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, soc_thermal_ids);

static int __init intel_soc_thermal_init(void)
{
	int err = 0;
	const struct x86_cpu_id *match_cpu;

	match_cpu = x86_match_cpu(soc_thermal_ids);
	if (!match_cpu)
		return -ENODEV;

	/* Create a zone with 2 trips with marked as read only */
	soc_dts = intel_soc_dts_iosf_init(INTEL_SOC_DTS_INTERRUPT_APIC, true,
					  crit_offset);
	if (IS_ERR(soc_dts)) {
		err = PTR_ERR(soc_dts);
		return err;
	}

	soc_dts_thres_gsi = (int)match_cpu->driver_data;
	if (soc_dts_thres_gsi) {
		/*
		 * Note the flags here MUST match the firmware defaults, rather
		 * then the request_irq flags, otherwise we get an EBUSY error.
		 */
		soc_dts_thres_irq = acpi_register_gsi(NULL, soc_dts_thres_gsi,
						      ACPI_LEVEL_SENSITIVE,
						      ACPI_ACTIVE_LOW);
		if (soc_dts_thres_irq < 0) {
			pr_warn("intel_soc_dts: Could not get IRQ for GSI %d, err %d\n",
				soc_dts_thres_gsi, soc_dts_thres_irq);
			soc_dts_thres_irq = 0;
		}
	}

	if (soc_dts_thres_irq) {
		err = request_threaded_irq(soc_dts_thres_irq, NULL,
					   soc_irq_thread_fn,
					   IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					   "soc_dts", soc_dts);
		if (err) {
			/*
			 * Do not just error out because the user space thermal
			 * daemon such as DPTF may use polling instead of being
			 * interrupt driven.
			 */
			pr_warn("request_threaded_irq ret %d\n", err);
		}
	}

	return 0;
}

static void __exit intel_soc_thermal_exit(void)
{
	if (soc_dts_thres_irq) {
		free_irq(soc_dts_thres_irq, soc_dts);
		acpi_unregister_gsi(soc_dts_thres_gsi);
	}
	intel_soc_dts_iosf_exit(soc_dts);
}

module_init(intel_soc_thermal_init)
module_exit(intel_soc_thermal_exit)

MODULE_DESCRIPTION("Intel SoC DTS Thermal Driver");
MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_LICENSE("GPL v2");
