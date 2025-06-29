// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel PPS signal Generator Driver
 *
 * Copyright (C) 2024 Intel Corporation
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/hrtimer.h>
#include <linux/io-64-nonatomic-hi-lo.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pps_gen_kernel.h>
#include <linux/timekeeping.h>
#include <linux/types.h>

#include <asm/cpu_device_id.h>

#define TIOCTL			0x00
#define TIOCOMPV		0x10
#define TIOEC			0x30

/* Control Register */
#define TIOCTL_EN			BIT(0)
#define TIOCTL_DIR			BIT(1)
#define TIOCTL_EP			GENMASK(3, 2)
#define TIOCTL_EP_RISING_EDGE		FIELD_PREP(TIOCTL_EP, 0)
#define TIOCTL_EP_FALLING_EDGE		FIELD_PREP(TIOCTL_EP, 1)
#define TIOCTL_EP_TOGGLE_EDGE		FIELD_PREP(TIOCTL_EP, 2)

/* Safety time to set hrtimer early */
#define SAFE_TIME_NS			(10 * NSEC_PER_MSEC)

#define MAGIC_CONST			(NSEC_PER_SEC - SAFE_TIME_NS)
#define ART_HW_DELAY_CYCLES		2

struct pps_tio {
	struct pps_gen_source_info gen_info;
	struct pps_gen_device *pps_gen;
	struct hrtimer timer;
	void __iomem *base;
	u32 prev_count;
	spinlock_t lock;
	struct device *dev;
};

static inline u32 pps_tio_read(u32 offset, struct pps_tio *tio)
{
	return readl(tio->base + offset);
}

static inline void pps_ctl_write(u32 value, struct pps_tio *tio)
{
	writel(value, tio->base + TIOCTL);
}

/*
 * For COMPV register, It's safer to write
 * higher 32-bit followed by lower 32-bit
 */
static inline void pps_compv_write(u64 value, struct pps_tio *tio)
{
	hi_lo_writeq(value, tio->base + TIOCOMPV);
}

static inline ktime_t first_event(struct pps_tio *tio)
{
	return ktime_set(ktime_get_real_seconds() + 1, MAGIC_CONST);
}

static u32 pps_tio_disable(struct pps_tio *tio)
{
	u32 ctrl;

	ctrl = pps_tio_read(TIOCTL, tio);
	pps_compv_write(0, tio);

	ctrl &= ~TIOCTL_EN;
	pps_ctl_write(ctrl, tio);
	tio->pps_gen->enabled = false;
	tio->prev_count = 0;
	return ctrl;
}

static void pps_tio_enable(struct pps_tio *tio)
{
	u32 ctrl;

	ctrl = pps_tio_read(TIOCTL, tio);
	ctrl |= TIOCTL_EN;
	pps_ctl_write(ctrl, tio);
	tio->pps_gen->enabled = true;
}

static void pps_tio_direction_output(struct pps_tio *tio)
{
	u32 ctrl;

	ctrl = pps_tio_disable(tio);

	/*
	 * We enable the device, be sure that the
	 * 'compare' value is invalid
	 */
	pps_compv_write(0, tio);

	ctrl &= ~(TIOCTL_DIR | TIOCTL_EP);
	ctrl |= TIOCTL_EP_TOGGLE_EDGE;
	pps_ctl_write(ctrl, tio);
	pps_tio_enable(tio);
}

static bool pps_generate_next_pulse(ktime_t expires, struct pps_tio *tio)
{
	u64 art;

	if (!ktime_real_to_base_clock(expires, CSID_X86_ART, &art)) {
		pps_tio_disable(tio);
		return false;
	}

	pps_compv_write(art - ART_HW_DELAY_CYCLES, tio);
	return true;
}

static enum hrtimer_restart hrtimer_callback(struct hrtimer *timer)
{
	ktime_t expires, now;
	u32 event_count;
	struct pps_tio *tio = container_of(timer, struct pps_tio, timer);

	guard(spinlock)(&tio->lock);

	/*
	 * Check if any event is missed.
	 * If an event is missed, TIO will be disabled.
	 */
	event_count = pps_tio_read(TIOEC, tio);
	if (tio->prev_count && tio->prev_count == event_count)
		goto err;
	tio->prev_count = event_count;

	expires = hrtimer_get_expires(timer);

	now = ktime_get_real();
	if (now - expires >= SAFE_TIME_NS)
		goto err;

	tio->pps_gen->enabled = pps_generate_next_pulse(expires + SAFE_TIME_NS, tio);
	if (!tio->pps_gen->enabled)
		return HRTIMER_NORESTART;

	hrtimer_forward(timer, now, NSEC_PER_SEC / 2);
	return HRTIMER_RESTART;

err:
	dev_err(tio->dev, "Event missed, Disabling Timed I/O");
	pps_tio_disable(tio);
	pps_gen_event(tio->pps_gen, PPS_GEN_EVENT_MISSEDPULSE, NULL);
	return HRTIMER_NORESTART;
}

static int pps_tio_gen_enable(struct pps_gen_device *pps_gen, bool enable)
{
	struct pps_tio *tio = container_of(pps_gen->info, struct pps_tio, gen_info);

	if (!timekeeping_clocksource_has_base(CSID_X86_ART)) {
		dev_err_once(tio->dev, "PPS cannot be used as clock is not related to ART");
		return -ENODEV;
	}

	guard(spinlock_irqsave)(&tio->lock);
	if (enable && !pps_gen->enabled) {
		pps_tio_direction_output(tio);
		hrtimer_start(&tio->timer, first_event(tio), HRTIMER_MODE_ABS);
	} else if (!enable && pps_gen->enabled) {
		hrtimer_cancel(&tio->timer);
		pps_tio_disable(tio);
	}

	return 0;
}

static int pps_tio_get_time(struct pps_gen_device *pps_gen,
			    struct timespec64 *time)
{
	struct system_time_snapshot snap;

	ktime_get_snapshot(&snap);
	*time = ktime_to_timespec64(snap.real);

	return 0;
}

static int pps_gen_tio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pps_tio *tio;

	if (!(cpu_feature_enabled(X86_FEATURE_TSC_KNOWN_FREQ) &&
	      cpu_feature_enabled(X86_FEATURE_ART))) {
		dev_warn(dev, "TSC/ART is not enabled");
		return -ENODEV;
	}

	tio = devm_kzalloc(dev, sizeof(*tio), GFP_KERNEL);
	if (!tio)
		return -ENOMEM;

	tio->gen_info.use_system_clock = true;
	tio->gen_info.enable = pps_tio_gen_enable;
	tio->gen_info.get_time = pps_tio_get_time;
	tio->gen_info.owner = THIS_MODULE;

	tio->pps_gen = pps_gen_register_source(&tio->gen_info);
	if (IS_ERR(tio->pps_gen))
		return PTR_ERR(tio->pps_gen);

	tio->dev = dev;
	tio->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(tio->base))
		return PTR_ERR(tio->base);

	pps_tio_disable(tio);
	hrtimer_setup(&tio->timer, hrtimer_callback, CLOCK_REALTIME,
		      HRTIMER_MODE_ABS);
	spin_lock_init(&tio->lock);
	platform_set_drvdata(pdev, tio);

	return 0;
}

static void pps_gen_tio_remove(struct platform_device *pdev)
{
	struct pps_tio *tio = platform_get_drvdata(pdev);

	hrtimer_cancel(&tio->timer);
	pps_tio_disable(tio);
	pps_gen_unregister_source(tio->pps_gen);
}

static const struct acpi_device_id intel_pmc_tio_acpi_match[] = {
	{ "INTC1021" },
	{ "INTC1022" },
	{ "INTC1023" },
	{ "INTC1024" },
	{}
};
MODULE_DEVICE_TABLE(acpi, intel_pmc_tio_acpi_match);

static struct platform_driver pps_gen_tio_driver = {
	.probe          = pps_gen_tio_probe,
	.remove         = pps_gen_tio_remove,
	.driver         = {
		.name                   = "intel-pps-gen-tio",
		.acpi_match_table       = intel_pmc_tio_acpi_match,
	},
};
module_platform_driver(pps_gen_tio_driver);

MODULE_AUTHOR("Christopher Hall <christopher.s.hall@intel.com>");
MODULE_AUTHOR("Lakshmi Sowjanya D <lakshmi.sowjanya.d@intel.com>");
MODULE_AUTHOR("Pandith N <pandith.n@intel.com>");
MODULE_AUTHOR("Thejesh Reddy T R <thejesh.reddy.t.r@intel.com>");
MODULE_AUTHOR("Subramanian Mohan <subramanian.mohan@intel.com>");
MODULE_DESCRIPTION("Intel PMC Time-Aware IO Generator Driver");
MODULE_LICENSE("GPL");
