// SPDX-License-Identifier: GPL-2.0-only
//
// VCPU stall detector.
//  Copyright (C) Google, 2022

#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/nmi.h>
#include <linux/of.h>
#include <linux/param.h>
#include <linux/percpu.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define VCPU_STALL_REG_STATUS		(0x00)
#define VCPU_STALL_REG_LOAD_CNT		(0x04)
#define VCPU_STALL_REG_CURRENT_CNT	(0x08)
#define VCPU_STALL_REG_CLOCK_FREQ_HZ	(0x0C)
#define VCPU_STALL_REG_LEN		(0x10)

#define VCPU_STALL_DEFAULT_CLOCK_HZ	(10)
#define VCPU_STALL_MAX_CLOCK_HZ		(100)
#define VCPU_STALL_DEFAULT_TIMEOUT_SEC	(8)
#define VCPU_STALL_MAX_TIMEOUT_SEC	(600)

struct vcpu_stall_detect_config {
	u32 clock_freq_hz;
	u32 stall_timeout_sec;
	int ppi_irq;

	void __iomem *membase;
	struct platform_device *dev;
	enum cpuhp_state hp_online;
};

struct vcpu_stall_priv {
	struct hrtimer vcpu_hrtimer;
	bool is_initialized;
};

/* The vcpu stall configuration structure which applies to all the CPUs */
static struct vcpu_stall_detect_config vcpu_stall_config;

#define vcpu_stall_reg_write(vcpu, reg, value)				\
	writel_relaxed((value),						\
		       (void __iomem *)(vcpu_stall_config.membase +	\
		       (vcpu) * VCPU_STALL_REG_LEN + (reg)))


static struct vcpu_stall_priv __percpu *vcpu_stall_detectors;

static enum hrtimer_restart
vcpu_stall_detect_timer_fn(struct hrtimer *hrtimer)
{
	u32 ticks, ping_timeout_ms;

	/* Reload the stall detector counter register every
	 * `ping_timeout_ms` to prevent the virtual device
	 * from decrementing it to 0. The virtual device decrements this
	 * register at 'clock_freq_hz' frequency.
	 */
	ticks = vcpu_stall_config.clock_freq_hz *
		vcpu_stall_config.stall_timeout_sec;
	vcpu_stall_reg_write(smp_processor_id(),
			     VCPU_STALL_REG_LOAD_CNT, ticks);

	ping_timeout_ms = vcpu_stall_config.stall_timeout_sec *
			  MSEC_PER_SEC / 2;
	hrtimer_forward_now(hrtimer,
			    ms_to_ktime(ping_timeout_ms));

	return HRTIMER_RESTART;
}

static irqreturn_t vcpu_stall_detector_irq(int irq, void *dev)
{
	panic("vCPU stall detector");
	return IRQ_HANDLED;
}

static int start_stall_detector_cpu(unsigned int cpu)
{
	u32 ticks, ping_timeout_ms;
	struct vcpu_stall_priv *vcpu_stall_detector =
		this_cpu_ptr(vcpu_stall_detectors);
	struct hrtimer *vcpu_hrtimer = &vcpu_stall_detector->vcpu_hrtimer;

	vcpu_stall_reg_write(cpu, VCPU_STALL_REG_CLOCK_FREQ_HZ,
			     vcpu_stall_config.clock_freq_hz);

	/* Compute the number of ticks required for the stall detector
	 * counter register based on the internal clock frequency and the
	 * timeout value given from the device tree.
	 */
	ticks = vcpu_stall_config.clock_freq_hz *
		vcpu_stall_config.stall_timeout_sec;
	vcpu_stall_reg_write(cpu, VCPU_STALL_REG_LOAD_CNT, ticks);

	/* Enable the internal clock and start the stall detector */
	vcpu_stall_reg_write(cpu, VCPU_STALL_REG_STATUS, 1);

	/* Pet the stall detector at half of its expiration timeout
	 * to prevent spurious resets.
	 */
	ping_timeout_ms = vcpu_stall_config.stall_timeout_sec *
			  MSEC_PER_SEC / 2;

	hrtimer_init(vcpu_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	vcpu_hrtimer->function = vcpu_stall_detect_timer_fn;
	vcpu_stall_detector->is_initialized = true;

	hrtimer_start(vcpu_hrtimer, ms_to_ktime(ping_timeout_ms),
		      HRTIMER_MODE_REL_PINNED);

	return 0;
}

static int stop_stall_detector_cpu(unsigned int cpu)
{
	struct vcpu_stall_priv *vcpu_stall_detector =
		per_cpu_ptr(vcpu_stall_detectors, cpu);

	if (!vcpu_stall_detector->is_initialized)
		return 0;

	/* Disable the stall detector for the current CPU */
	hrtimer_cancel(&vcpu_stall_detector->vcpu_hrtimer);
	vcpu_stall_reg_write(cpu, VCPU_STALL_REG_STATUS, 0);
	vcpu_stall_detector->is_initialized = false;

	return 0;
}

static int vcpu_stall_detect_probe(struct platform_device *pdev)
{
	int ret, irq;
	struct resource *r;
	void __iomem *membase;
	u32 clock_freq_hz = VCPU_STALL_DEFAULT_CLOCK_HZ;
	u32 stall_timeout_sec = VCPU_STALL_DEFAULT_TIMEOUT_SEC;
	struct device_node *np = pdev->dev.of_node;

	vcpu_stall_detectors = devm_alloc_percpu(&pdev->dev,
						 typeof(struct vcpu_stall_priv));
	if (!vcpu_stall_detectors)
		return -ENOMEM;

	membase = devm_platform_get_and_ioremap_resource(pdev, 0, &r);
	if (IS_ERR(membase)) {
		dev_err(&pdev->dev, "Failed to get memory resource\n");
		return PTR_ERR(membase);
	}

	if (!of_property_read_u32(np, "clock-frequency", &clock_freq_hz)) {
		if (!(clock_freq_hz > 0 &&
		      clock_freq_hz < VCPU_STALL_MAX_CLOCK_HZ)) {
			dev_warn(&pdev->dev, "clk out of range\n");
			clock_freq_hz = VCPU_STALL_DEFAULT_CLOCK_HZ;
		}
	}

	if (!of_property_read_u32(np, "timeout-sec", &stall_timeout_sec)) {
		if (!(stall_timeout_sec > 0 &&
		      stall_timeout_sec < VCPU_STALL_MAX_TIMEOUT_SEC)) {
			dev_warn(&pdev->dev, "stall timeout out of range\n");
			stall_timeout_sec = VCPU_STALL_DEFAULT_TIMEOUT_SEC;
		}
	}

	vcpu_stall_config = (struct vcpu_stall_detect_config) {
		.membase		= membase,
		.clock_freq_hz		= clock_freq_hz,
		.stall_timeout_sec	= stall_timeout_sec,
		.ppi_irq		= -1,
	};

	irq = platform_get_irq_optional(pdev, 0);
	if (irq > 0) {
		ret = request_percpu_irq(irq,
					 vcpu_stall_detector_irq,
					 "vcpu_stall_detector",
					 vcpu_stall_detectors);
		if (ret)
			goto err;

		vcpu_stall_config.ppi_irq = irq;
	}

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
				"virt/vcpu_stall_detector:online",
				start_stall_detector_cpu,
				stop_stall_detector_cpu);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to install cpu hotplug");
		goto err;
	}

	vcpu_stall_config.hp_online = ret;
	return 0;
err:
	if (vcpu_stall_config.ppi_irq > 0)
		free_percpu_irq(vcpu_stall_config.ppi_irq,
				vcpu_stall_detectors);
	return ret;
}

static void vcpu_stall_detect_remove(struct platform_device *pdev)
{
	int cpu;

	cpuhp_remove_state(vcpu_stall_config.hp_online);

	if (vcpu_stall_config.ppi_irq > 0)
		free_percpu_irq(vcpu_stall_config.ppi_irq,
				vcpu_stall_detectors);

	for_each_possible_cpu(cpu)
		stop_stall_detector_cpu(cpu);
}

static const struct of_device_id vcpu_stall_detect_of_match[] = {
	{ .compatible = "qemu,vcpu-stall-detector", },
	{}
};

MODULE_DEVICE_TABLE(of, vcpu_stall_detect_of_match);

static struct platform_driver vcpu_stall_detect_driver = {
	.probe  = vcpu_stall_detect_probe,
	.remove_new = vcpu_stall_detect_remove,
	.driver = {
		.name           = KBUILD_MODNAME,
		.of_match_table = vcpu_stall_detect_of_match,
	},
};

module_platform_driver(vcpu_stall_detect_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sebastian Ene <sebastianene@google.com>");
MODULE_DESCRIPTION("VCPU stall detector");
