// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/drivers/mmc/core/host.c
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *  Copyright (C) 2007-2008 Pierre Ossman
 *  Copyright (C) 2010 Linus Walleij
 *
 *  MMC host class device management
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/idr.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/pagemap.h>
#include <linux/pm_wakeup.h>
#include <linux/export.h>
#include <linux/leds.h>
#include <linux/slab.h>

#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/slot-gpio.h>

#include "core.h"
#include "crypto.h"
#include "host.h"
#include "slot-gpio.h"
#include "pwrseq.h"
#include "sdio_ops.h"

#define cls_dev_to_mmc_host(d)	container_of(d, struct mmc_host, class_dev)

static DEFINE_IDA(mmc_host_ida);

#ifdef CONFIG_PM_SLEEP
static int mmc_host_class_prepare(struct device *dev)
{
	struct mmc_host *host = cls_dev_to_mmc_host(dev);

	/*
	 * It's safe to access the bus_ops pointer, as both userspace and the
	 * workqueue for detecting cards are frozen at this point.
	 */
	if (!host->bus_ops)
		return 0;

	/* Validate conditions for system suspend. */
	if (host->bus_ops->pre_suspend)
		return host->bus_ops->pre_suspend(host);

	return 0;
}

static void mmc_host_class_complete(struct device *dev)
{
	struct mmc_host *host = cls_dev_to_mmc_host(dev);

	_mmc_detect_change(host, 0, false);
}

static const struct dev_pm_ops mmc_host_class_dev_pm_ops = {
	.prepare = mmc_host_class_prepare,
	.complete = mmc_host_class_complete,
};

#define MMC_HOST_CLASS_DEV_PM_OPS (&mmc_host_class_dev_pm_ops)
#else
#define MMC_HOST_CLASS_DEV_PM_OPS NULL
#endif

static void mmc_host_classdev_release(struct device *dev)
{
	struct mmc_host *host = cls_dev_to_mmc_host(dev);
	wakeup_source_unregister(host->ws);
	if (of_alias_get_id(host->parent->of_node, "mmc") < 0)
		ida_simple_remove(&mmc_host_ida, host->index);
	kfree(host);
}

static int mmc_host_classdev_shutdown(struct device *dev)
{
	struct mmc_host *host = cls_dev_to_mmc_host(dev);

	__mmc_stop_host(host);
	return 0;
}

static struct class mmc_host_class = {
	.name		= "mmc_host",
	.dev_release	= mmc_host_classdev_release,
	.shutdown_pre	= mmc_host_classdev_shutdown,
	.pm		= MMC_HOST_CLASS_DEV_PM_OPS,
};

int mmc_register_host_class(void)
{
	return class_register(&mmc_host_class);
}

void mmc_unregister_host_class(void)
{
	class_unregister(&mmc_host_class);
}

/**
 * mmc_retune_enable() - enter a transfer mode that requires retuning
 * @host: host which should retune now
 */
void mmc_retune_enable(struct mmc_host *host)
{
	host->can_retune = 1;
	if (host->retune_period)
		mod_timer(&host->retune_timer,
			  jiffies + host->retune_period * HZ);
}

/*
 * Pause re-tuning for a small set of operations.  The pause begins after the
 * next command and after first doing re-tuning.
 */
void mmc_retune_pause(struct mmc_host *host)
{
	if (!host->retune_paused) {
		host->retune_paused = 1;
		mmc_retune_needed(host);
		mmc_retune_hold(host);
	}
}
EXPORT_SYMBOL(mmc_retune_pause);

void mmc_retune_unpause(struct mmc_host *host)
{
	if (host->retune_paused) {
		host->retune_paused = 0;
		mmc_retune_release(host);
	}
}
EXPORT_SYMBOL(mmc_retune_unpause);

/**
 * mmc_retune_disable() - exit a transfer mode that requires retuning
 * @host: host which should not retune anymore
 *
 * It is not meant for temporarily preventing retuning!
 */
void mmc_retune_disable(struct mmc_host *host)
{
	mmc_retune_unpause(host);
	host->can_retune = 0;
	del_timer_sync(&host->retune_timer);
	mmc_retune_clear(host);
}

void mmc_retune_timer_stop(struct mmc_host *host)
{
	del_timer_sync(&host->retune_timer);
}
EXPORT_SYMBOL(mmc_retune_timer_stop);

void mmc_retune_hold(struct mmc_host *host)
{
	if (!host->hold_retune)
		host->retune_now = 1;
	host->hold_retune += 1;
}

void mmc_retune_release(struct mmc_host *host)
{
	if (host->hold_retune)
		host->hold_retune -= 1;
	else
		WARN_ON(1);
}
EXPORT_SYMBOL(mmc_retune_release);

int mmc_retune(struct mmc_host *host)
{
	bool return_to_hs400 = false;
	int err;

	if (host->retune_now)
		host->retune_now = 0;
	else
		return 0;

	if (!host->need_retune || host->doing_retune || !host->card)
		return 0;

	host->need_retune = 0;

	host->doing_retune = 1;

	if (host->ios.timing == MMC_TIMING_MMC_HS400) {
		err = mmc_hs400_to_hs200(host->card);
		if (err)
			goto out;

		return_to_hs400 = true;
	}

	err = mmc_execute_tuning(host->card);
	if (err)
		goto out;

	if (return_to_hs400)
		err = mmc_hs200_to_hs400(host->card);
out:
	host->doing_retune = 0;

	return err;
}

static void mmc_retune_timer(struct timer_list *t)
{
	struct mmc_host *host = from_timer(host, t, retune_timer);

	mmc_retune_needed(host);
}

static void mmc_of_parse_timing_phase(struct device *dev, const char *prop,
				      struct mmc_clk_phase *phase)
{
	int degrees[2] = {0};
	int rc;

	rc = device_property_read_u32_array(dev, prop, degrees, 2);
	phase->valid = !rc;
	if (phase->valid) {
		phase->in_deg = degrees[0];
		phase->out_deg = degrees[1];
	}
}

void
mmc_of_parse_clk_phase(struct mmc_host *host, struct mmc_clk_phase_map *map)
{
	struct device *dev = host->parent;

	mmc_of_parse_timing_phase(dev, "clk-phase-legacy",
				  &map->phase[MMC_TIMING_LEGACY]);
	mmc_of_parse_timing_phase(dev, "clk-phase-mmc-hs",
				  &map->phase[MMC_TIMING_MMC_HS]);
	mmc_of_parse_timing_phase(dev, "clk-phase-sd-hs",
				  &map->phase[MMC_TIMING_SD_HS]);
	mmc_of_parse_timing_phase(dev, "clk-phase-uhs-sdr12",
				  &map->phase[MMC_TIMING_UHS_SDR12]);
	mmc_of_parse_timing_phase(dev, "clk-phase-uhs-sdr25",
				  &map->phase[MMC_TIMING_UHS_SDR25]);
	mmc_of_parse_timing_phase(dev, "clk-phase-uhs-sdr50",
				  &map->phase[MMC_TIMING_UHS_SDR50]);
	mmc_of_parse_timing_phase(dev, "clk-phase-uhs-sdr104",
				  &map->phase[MMC_TIMING_UHS_SDR104]);
	mmc_of_parse_timing_phase(dev, "clk-phase-uhs-ddr50",
				  &map->phase[MMC_TIMING_UHS_DDR50]);
	mmc_of_parse_timing_phase(dev, "clk-phase-mmc-ddr52",
				  &map->phase[MMC_TIMING_MMC_DDR52]);
	mmc_of_parse_timing_phase(dev, "clk-phase-mmc-hs200",
				  &map->phase[MMC_TIMING_MMC_HS200]);
	mmc_of_parse_timing_phase(dev, "clk-phase-mmc-hs400",
				  &map->phase[MMC_TIMING_MMC_HS400]);
}
EXPORT_SYMBOL(mmc_of_parse_clk_phase);

/**
 * mmc_of_parse() - parse host's device properties
 * @host: host whose properties should be parsed.
 *
 * To keep the rest of the MMC subsystem unaware of whether DT has been
 * used to instantiate and configure this host instance or not, we
 * parse the properties and set respective generic mmc-host flags and
 * parameters.
 */
int mmc_of_parse(struct mmc_host *host)
{
	struct device *dev = host->parent;
	u32 bus_width, drv_type, cd_debounce_delay_ms;
	int ret;

	if (!dev || !dev_fwnode(dev))
		return 0;

	/* "bus-width" is translated to MMC_CAP_*_BIT_DATA flags */
	if (device_property_read_u32(dev, "bus-width", &bus_width) < 0) {
		dev_dbg(host->parent,
			"\"bus-width\" property is missing, assuming 1 bit.\n");
		bus_width = 1;
	}

	switch (bus_width) {
	case 8:
		host->caps |= MMC_CAP_8_BIT_DATA;
		fallthrough;	/* Hosts capable of 8-bit can also do 4 bits */
	case 4:
		host->caps |= MMC_CAP_4_BIT_DATA;
		break;
	case 1:
		break;
	default:
		dev_err(host->parent,
			"Invalid \"bus-width\" value %u!\n", bus_width);
		return -EINVAL;
	}

	/* f_max is obtained from the optional "max-frequency" property */
	device_property_read_u32(dev, "max-frequency", &host->f_max);

	/*
	 * Configure CD and WP pins. They are both by default active low to
	 * match the SDHCI spec. If GPIOs are provided for CD and / or WP, the
	 * mmc-gpio helpers are used to attach, configure and use them. If
	 * polarity inversion is specified in DT, one of MMC_CAP2_CD_ACTIVE_HIGH
	 * and MMC_CAP2_RO_ACTIVE_HIGH capability-2 flags is set. If the
	 * "broken-cd" property is provided, the MMC_CAP_NEEDS_POLL capability
	 * is set. If the "non-removable" property is found, the
	 * MMC_CAP_NONREMOVABLE capability is set and no card-detection
	 * configuration is performed.
	 */

	/* Parse Card Detection */

	if (device_property_read_bool(dev, "non-removable")) {
		host->caps |= MMC_CAP_NONREMOVABLE;
	} else {
		if (device_property_read_bool(dev, "cd-inverted"))
			host->caps2 |= MMC_CAP2_CD_ACTIVE_HIGH;

		if (device_property_read_u32(dev, "cd-debounce-delay-ms",
					     &cd_debounce_delay_ms))
			cd_debounce_delay_ms = 200;

		if (device_property_read_bool(dev, "broken-cd"))
			host->caps |= MMC_CAP_NEEDS_POLL;

		ret = mmc_gpiod_request_cd(host, "cd", 0, false,
					   cd_debounce_delay_ms * 1000);
		if (!ret)
			dev_info(host->parent, "Got CD GPIO\n");
		else if (ret != -ENOENT && ret != -ENOSYS)
			return ret;
	}

	/* Parse Write Protection */

	if (device_property_read_bool(dev, "wp-inverted"))
		host->caps2 |= MMC_CAP2_RO_ACTIVE_HIGH;

	ret = mmc_gpiod_request_ro(host, "wp", 0, 0);
	if (!ret)
		dev_info(host->parent, "Got WP GPIO\n");
	else if (ret != -ENOENT && ret != -ENOSYS)
		return ret;

	if (device_property_read_bool(dev, "disable-wp"))
		host->caps2 |= MMC_CAP2_NO_WRITE_PROTECT;

	if (device_property_read_bool(dev, "cap-sd-highspeed"))
		host->caps |= MMC_CAP_SD_HIGHSPEED;
	if (device_property_read_bool(dev, "cap-mmc-highspeed"))
		host->caps |= MMC_CAP_MMC_HIGHSPEED;
	if (device_property_read_bool(dev, "sd-uhs-sdr12"))
		host->caps |= MMC_CAP_UHS_SDR12;
	if (device_property_read_bool(dev, "sd-uhs-sdr25"))
		host->caps |= MMC_CAP_UHS_SDR25;
	if (device_property_read_bool(dev, "sd-uhs-sdr50"))
		host->caps |= MMC_CAP_UHS_SDR50;
	if (device_property_read_bool(dev, "sd-uhs-sdr104"))
		host->caps |= MMC_CAP_UHS_SDR104;
	if (device_property_read_bool(dev, "sd-uhs-ddr50"))
		host->caps |= MMC_CAP_UHS_DDR50;
	if (device_property_read_bool(dev, "cap-power-off-card"))
		host->caps |= MMC_CAP_POWER_OFF_CARD;
	if (device_property_read_bool(dev, "cap-mmc-hw-reset"))
		host->caps |= MMC_CAP_HW_RESET;
	if (device_property_read_bool(dev, "cap-sdio-irq"))
		host->caps |= MMC_CAP_SDIO_IRQ;
	if (device_property_read_bool(dev, "full-pwr-cycle"))
		host->caps2 |= MMC_CAP2_FULL_PWR_CYCLE;
	if (device_property_read_bool(dev, "full-pwr-cycle-in-suspend"))
		host->caps2 |= MMC_CAP2_FULL_PWR_CYCLE_IN_SUSPEND;
	if (device_property_read_bool(dev, "keep-power-in-suspend"))
		host->pm_caps |= MMC_PM_KEEP_POWER;
	if (device_property_read_bool(dev, "wakeup-source") ||
	    device_property_read_bool(dev, "enable-sdio-wakeup")) /* legacy */
		host->pm_caps |= MMC_PM_WAKE_SDIO_IRQ;
	if (device_property_read_bool(dev, "mmc-ddr-3_3v"))
		host->caps |= MMC_CAP_3_3V_DDR;
	if (device_property_read_bool(dev, "mmc-ddr-1_8v"))
		host->caps |= MMC_CAP_1_8V_DDR;
	if (device_property_read_bool(dev, "mmc-ddr-1_2v"))
		host->caps |= MMC_CAP_1_2V_DDR;
	if (device_property_read_bool(dev, "mmc-hs200-1_8v"))
		host->caps2 |= MMC_CAP2_HS200_1_8V_SDR;
	if (device_property_read_bool(dev, "mmc-hs200-1_2v"))
		host->caps2 |= MMC_CAP2_HS200_1_2V_SDR;
	if (device_property_read_bool(dev, "mmc-hs400-1_8v"))
		host->caps2 |= MMC_CAP2_HS400_1_8V | MMC_CAP2_HS200_1_8V_SDR;
	if (device_property_read_bool(dev, "mmc-hs400-1_2v"))
		host->caps2 |= MMC_CAP2_HS400_1_2V | MMC_CAP2_HS200_1_2V_SDR;
	if (device_property_read_bool(dev, "mmc-hs400-enhanced-strobe"))
		host->caps2 |= MMC_CAP2_HS400_ES;
	if (device_property_read_bool(dev, "no-sdio"))
		host->caps2 |= MMC_CAP2_NO_SDIO;
	if (device_property_read_bool(dev, "no-sd"))
		host->caps2 |= MMC_CAP2_NO_SD;
	if (device_property_read_bool(dev, "no-mmc"))
		host->caps2 |= MMC_CAP2_NO_MMC;
	if (device_property_read_bool(dev, "no-mmc-hs400"))
		host->caps2 &= ~(MMC_CAP2_HS400_1_8V | MMC_CAP2_HS400_1_2V |
				 MMC_CAP2_HS400_ES);

	/* Must be after "non-removable" check */
	if (device_property_read_u32(dev, "fixed-emmc-driver-type", &drv_type) == 0) {
		if (host->caps & MMC_CAP_NONREMOVABLE)
			host->fixed_drv_type = drv_type;
		else
			dev_err(host->parent,
				"can't use fixed driver type, media is removable\n");
	}

	host->dsr_req = !device_property_read_u32(dev, "dsr", &host->dsr);
	if (host->dsr_req && (host->dsr & ~0xffff)) {
		dev_err(host->parent,
			"device tree specified broken value for DSR: 0x%x, ignoring\n",
			host->dsr);
		host->dsr_req = 0;
	}

	device_property_read_u32(dev, "post-power-on-delay-ms",
				 &host->ios.power_delay_ms);

	return mmc_pwrseq_alloc(host);
}

EXPORT_SYMBOL(mmc_of_parse);

/**
 * mmc_of_parse_voltage - return mask of supported voltages
 * @host: host whose properties should be parsed.
 * @mask: mask of voltages available for MMC/SD/SDIO
 *
 * Parse the "voltage-ranges" property, returning zero if it is not
 * found, negative errno if the voltage-range specification is invalid,
 * or one if the voltage-range is specified and successfully parsed.
 */
int mmc_of_parse_voltage(struct mmc_host *host, u32 *mask)
{
	const char *prop = "voltage-ranges";
	struct device *dev = host->parent;
	u32 *voltage_ranges;
	int num_ranges, i;
	int ret;

	if (!device_property_present(dev, prop)) {
		dev_dbg(dev, "%s unspecified\n", prop);
		return 0;
	}

	ret = device_property_count_u32(dev, prop);
	if (ret < 0)
		return ret;

	num_ranges = ret / 2;
	if (!num_ranges) {
		dev_err(dev, "%s empty\n", prop);
		return -EINVAL;
	}

	voltage_ranges = kcalloc(2 * num_ranges, sizeof(*voltage_ranges), GFP_KERNEL);
	if (!voltage_ranges)
		return -ENOMEM;

	ret = device_property_read_u32_array(dev, prop, voltage_ranges, 2 * num_ranges);
	if (ret) {
		kfree(voltage_ranges);
		return ret;
	}

	for (i = 0; i < num_ranges; i++) {
		const int j = i * 2;
		u32 ocr_mask;

		ocr_mask = mmc_vddrange_to_ocrmask(voltage_ranges[j + 0],
						   voltage_ranges[j + 1]);
		if (!ocr_mask) {
			dev_err(dev, "range #%d in %s is invalid\n", i, prop);
			kfree(voltage_ranges);
			return -EINVAL;
		}
		*mask |= ocr_mask;
	}

	kfree(voltage_ranges);

	return 1;
}
EXPORT_SYMBOL(mmc_of_parse_voltage);

/**
 * mmc_first_nonreserved_index() - get the first index that is not reserved
 */
static int mmc_first_nonreserved_index(void)
{
	int max;

	max = of_alias_get_highest_id("mmc");
	if (max < 0)
		return 0;

	return max + 1;
}

/**
 *	mmc_alloc_host - initialise the per-host structure.
 *	@extra: sizeof private data structure
 *	@dev: pointer to host device model structure
 *
 *	Initialise the per-host structure.
 */
struct mmc_host *mmc_alloc_host(int extra, struct device *dev)
{
	int index;
	struct mmc_host *host;
	int alias_id, min_idx, max_idx;

	host = kzalloc(sizeof(struct mmc_host) + extra, GFP_KERNEL);
	if (!host)
		return NULL;

	/* scanning will be enabled when we're ready */
	host->rescan_disable = 1;

	alias_id = of_alias_get_id(dev->of_node, "mmc");
	if (alias_id >= 0) {
		index = alias_id;
	} else {
		min_idx = mmc_first_nonreserved_index();
		max_idx = 0;

		index = ida_simple_get(&mmc_host_ida, min_idx, max_idx, GFP_KERNEL);
		if (index < 0) {
			kfree(host);
			return NULL;
		}
	}

	host->index = index;

	dev_set_name(&host->class_dev, "mmc%d", host->index);
	host->ws = wakeup_source_register(NULL, dev_name(&host->class_dev));

	host->parent = dev;
	host->class_dev.parent = dev;
	host->class_dev.class = &mmc_host_class;
	device_initialize(&host->class_dev);
	device_enable_async_suspend(&host->class_dev);

	if (mmc_gpio_alloc(host)) {
		put_device(&host->class_dev);
		return NULL;
	}

	spin_lock_init(&host->lock);
	init_waitqueue_head(&host->wq);
	INIT_DELAYED_WORK(&host->detect, mmc_rescan);
	INIT_WORK(&host->sdio_irq_work, sdio_irq_work);
	timer_setup(&host->retune_timer, mmc_retune_timer, 0);

	/*
	 * By default, hosts do not support SGIO or large requests.
	 * They have to set these according to their abilities.
	 */
	host->max_segs = 1;
	host->max_seg_size = PAGE_SIZE;

	host->max_req_size = PAGE_SIZE;
	host->max_blk_size = 512;
	host->max_blk_count = PAGE_SIZE / 512;

	host->fixed_drv_type = -EINVAL;
	host->ios.power_delay_ms = 10;
	host->ios.power_mode = MMC_POWER_UNDEFINED;

	return host;
}

EXPORT_SYMBOL(mmc_alloc_host);

static int mmc_validate_host_caps(struct mmc_host *host)
{
	struct device *dev = host->parent;
	u32 caps = host->caps, caps2 = host->caps2;

	if (caps & MMC_CAP_SDIO_IRQ && !host->ops->enable_sdio_irq) {
		dev_warn(dev, "missing ->enable_sdio_irq() ops\n");
		return -EINVAL;
	}

	if (caps2 & (MMC_CAP2_HS400_ES | MMC_CAP2_HS400) &&
	    !(caps & MMC_CAP_8_BIT_DATA) && !(caps2 & MMC_CAP2_NO_MMC)) {
		dev_warn(dev, "drop HS400 support since no 8-bit bus\n");
		host->caps2 = caps2 & ~MMC_CAP2_HS400_ES & ~MMC_CAP2_HS400;
	}

	return 0;
}

/**
 *	mmc_add_host - initialise host hardware
 *	@host: mmc host
 *
 *	Register the host with the driver model. The host must be
 *	prepared to start servicing requests before this function
 *	completes.
 */
int mmc_add_host(struct mmc_host *host)
{
	int err;

	err = mmc_validate_host_caps(host);
	if (err)
		return err;

	err = device_add(&host->class_dev);
	if (err)
		return err;

	led_trigger_register_simple(dev_name(&host->class_dev), &host->led);

	mmc_add_host_debugfs(host);

	mmc_start_host(host);
	return 0;
}

EXPORT_SYMBOL(mmc_add_host);

/**
 *	mmc_remove_host - remove host hardware
 *	@host: mmc host
 *
 *	Unregister and remove all cards associated with this host,
 *	and power down the MMC bus. No new requests will be issued
 *	after this function has returned.
 */
void mmc_remove_host(struct mmc_host *host)
{
	mmc_stop_host(host);

	mmc_remove_host_debugfs(host);

	device_del(&host->class_dev);

	led_trigger_unregister_simple(host->led);
}

EXPORT_SYMBOL(mmc_remove_host);

/**
 *	mmc_free_host - free the host structure
 *	@host: mmc host
 *
 *	Free the host once all references to it have been dropped.
 */
void mmc_free_host(struct mmc_host *host)
{
	mmc_pwrseq_free(host);
	put_device(&host->class_dev);
}

EXPORT_SYMBOL(mmc_free_host);
