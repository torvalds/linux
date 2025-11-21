// SPDX-License-Identifier: GPL-2.0
/*
 * Helper functions for MMC regulators.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/log2.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>

#include <linux/mmc/host.h>

#include "core.h"
#include "host.h"

#ifdef CONFIG_REGULATOR

/**
 * mmc_ocrbitnum_to_vdd - Convert a OCR bit number to its voltage
 * @vdd_bit:	OCR bit number
 * @min_uV:	minimum voltage value (mV)
 * @max_uV:	maximum voltage value (mV)
 *
 * This function returns the voltage range according to the provided OCR
 * bit number. If conversion is not possible a negative errno value returned.
 */
static int mmc_ocrbitnum_to_vdd(int vdd_bit, int *min_uV, int *max_uV)
{
	int		tmp;

	if (!vdd_bit)
		return -EINVAL;

	/*
	 * REVISIT mmc_vddrange_to_ocrmask() may have set some
	 * bits this regulator doesn't quite support ... don't
	 * be too picky, most cards and regulators are OK with
	 * a 0.1V range goof (it's a small error percentage).
	 */
	tmp = vdd_bit - ilog2(MMC_VDD_165_195);
	if (tmp == 0) {
		*min_uV = 1650 * 1000;
		*max_uV = 1950 * 1000;
	} else {
		*min_uV = 1900 * 1000 + tmp * 100 * 1000;
		*max_uV = *min_uV + 100 * 1000;
	}

	return 0;
}

/**
 * mmc_regulator_get_ocrmask - return mask of supported voltages
 * @supply: regulator to use
 *
 * This returns either a negative errno, or a mask of voltages that
 * can be provided to MMC/SD/SDIO devices using the specified voltage
 * regulator.  This would normally be called before registering the
 * MMC host adapter.
 */
static int mmc_regulator_get_ocrmask(struct regulator *supply)
{
	int			result = 0;
	int			count;
	int			i;
	int			vdd_uV;
	int			vdd_mV;

	count = regulator_count_voltages(supply);
	if (count < 0)
		return count;

	for (i = 0; i < count; i++) {
		vdd_uV = regulator_list_voltage(supply, i);
		if (vdd_uV <= 0)
			continue;

		vdd_mV = vdd_uV / 1000;
		result |= mmc_vddrange_to_ocrmask(vdd_mV, vdd_mV);
	}

	if (!result) {
		vdd_uV = regulator_get_voltage(supply);
		if (vdd_uV <= 0)
			return vdd_uV;

		vdd_mV = vdd_uV / 1000;
		result = mmc_vddrange_to_ocrmask(vdd_mV, vdd_mV);
	}

	return result;
}

/**
 * mmc_regulator_set_ocr - set regulator to match host->ios voltage
 * @mmc: the host to regulate
 * @supply: regulator to use
 * @vdd_bit: zero for power off, else a bit number (host->ios.vdd)
 *
 * Returns zero on success, else negative errno.
 *
 * MMC host drivers may use this to enable or disable a regulator using
 * a particular supply voltage.  This would normally be called from the
 * set_ios() method.
 */
int mmc_regulator_set_ocr(struct mmc_host *mmc,
			struct regulator *supply,
			unsigned short vdd_bit)
{
	int			result = 0;
	int			min_uV, max_uV;

	if (IS_ERR(supply))
		return 0;

	if (vdd_bit) {
		mmc_ocrbitnum_to_vdd(vdd_bit, &min_uV, &max_uV);

		result = regulator_set_voltage(supply, min_uV, max_uV);
		if (result == 0 && !mmc->regulator_enabled) {
			result = regulator_enable(supply);
			if (!result)
				mmc->regulator_enabled = true;
		}
	} else if (mmc->regulator_enabled) {
		result = regulator_disable(supply);
		if (result == 0)
			mmc->regulator_enabled = false;
	}

	if (result)
		dev_err(mmc_dev(mmc),
			"could not set regulator OCR (%d)\n", result);
	return result;
}
EXPORT_SYMBOL_GPL(mmc_regulator_set_ocr);

static int mmc_regulator_set_voltage_if_supported(struct regulator *regulator,
						  int min_uV, int target_uV,
						  int max_uV)
{
	int current_uV;

	/*
	 * Check if supported first to avoid errors since we may try several
	 * signal levels during power up and don't want to show errors.
	 */
	if (!regulator_is_supported_voltage(regulator, min_uV, max_uV))
		return -EINVAL;

	/*
	 * The voltage is already set, no need to switch.
	 * Return 1 to indicate that no switch happened.
	 */
	current_uV = regulator_get_voltage(regulator);
	if (current_uV == target_uV)
		return 1;

	return regulator_set_voltage_triplet(regulator, min_uV, target_uV,
					     max_uV);
}

/**
 * mmc_regulator_set_vqmmc - Set VQMMC as per the ios
 * @mmc: the host to regulate
 * @ios: io bus settings
 *
 * For 3.3V signaling, we try to match VQMMC to VMMC as closely as possible.
 * That will match the behavior of old boards where VQMMC and VMMC were supplied
 * by the same supply.  The Bus Operating conditions for 3.3V signaling in the
 * SD card spec also define VQMMC in terms of VMMC.
 * If this is not possible we'll try the full 2.7-3.6V of the spec.
 *
 * For 1.2V and 1.8V signaling we'll try to get as close as possible to the
 * requested voltage.  This is definitely a good idea for UHS where there's a
 * separate regulator on the card that's trying to make 1.8V and it's best if
 * we match.
 *
 * This function is expected to be used by a controller's
 * start_signal_voltage_switch() function.
 */
int mmc_regulator_set_vqmmc(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct device *dev = mmc_dev(mmc);
	int ret, volt, min_uV, max_uV;

	/* If no vqmmc supply then we can't change the voltage */
	if (IS_ERR(mmc->supply.vqmmc))
		return -EINVAL;

	switch (ios->signal_voltage) {
	case MMC_SIGNAL_VOLTAGE_120:
		return mmc_regulator_set_voltage_if_supported(mmc->supply.vqmmc,
						1100000, 1200000, 1300000);
	case MMC_SIGNAL_VOLTAGE_180:
		return mmc_regulator_set_voltage_if_supported(mmc->supply.vqmmc,
						1700000, 1800000, 1950000);
	case MMC_SIGNAL_VOLTAGE_330:
		ret = mmc_ocrbitnum_to_vdd(mmc->ios.vdd, &volt, &max_uV);
		if (ret < 0)
			return ret;

		dev_dbg(dev, "%s: found vmmc voltage range of %d-%duV\n",
			__func__, volt, max_uV);

		min_uV = max(volt - 300000, 2700000);
		max_uV = min(max_uV + 200000, 3600000);

		/*
		 * Due to a limitation in the current implementation of
		 * regulator_set_voltage_triplet() which is taking the lowest
		 * voltage possible if below the target, search for a suitable
		 * voltage in two steps and try to stay close to vmmc
		 * with a 0.3V tolerance at first.
		 */
		ret = mmc_regulator_set_voltage_if_supported(mmc->supply.vqmmc,
							min_uV, volt, max_uV);
		if (ret >= 0)
			return ret;

		return mmc_regulator_set_voltage_if_supported(mmc->supply.vqmmc,
						2700000, volt, 3600000);
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL_GPL(mmc_regulator_set_vqmmc);

/**
 * mmc_regulator_set_vqmmc2 - Set vqmmc2 as per the ios->vqmmc2_voltage
 * @mmc: The mmc host to regulate
 * @ios: The io bus settings
 *
 * Sets a new voltage level for the vqmmc2 regulator, which may correspond to
 * the vdd2 regulator for an SD UHS-II interface. This function is expected to
 * be called by mmc host drivers.
 *
 * Returns a negative error code on failure, zero if the voltage level was
 * changed successfully or a positive value if the level didn't need to change.
 */
int mmc_regulator_set_vqmmc2(struct mmc_host *mmc, struct mmc_ios *ios)
{
	if (IS_ERR(mmc->supply.vqmmc2))
		return -EINVAL;

	switch (ios->vqmmc2_voltage) {
	case MMC_VQMMC2_VOLTAGE_180:
		return mmc_regulator_set_voltage_if_supported(
			mmc->supply.vqmmc2, 1700000, 1800000, 1950000);
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL_GPL(mmc_regulator_set_vqmmc2);

#else

static inline int mmc_regulator_get_ocrmask(struct regulator *supply)
{
	return 0;
}

#endif /* CONFIG_REGULATOR */

/* To be called from a high-priority workqueue */
void mmc_undervoltage_workfn(struct work_struct *work)
{
	struct mmc_supply *supply;
	struct mmc_host *host;

	supply = container_of(work, struct mmc_supply, uv_work);
	host = container_of(supply, struct mmc_host, supply);

	mmc_handle_undervoltage(host);
}

static int mmc_handle_regulator_event(struct notifier_block *nb,
				      unsigned long event, void *data)
{
	struct mmc_supply *supply = container_of(nb, struct mmc_supply,
						 vmmc_nb);
	struct mmc_host *host = container_of(supply, struct mmc_host, supply);
	unsigned long flags;

	switch (event) {
	case REGULATOR_EVENT_UNDER_VOLTAGE:
		spin_lock_irqsave(&host->lock, flags);
		if (host->undervoltage) {
			spin_unlock_irqrestore(&host->lock, flags);
			return NOTIFY_OK;
		}

		host->undervoltage = true;
		spin_unlock_irqrestore(&host->lock, flags);

		queue_work(system_highpri_wq, &host->supply.uv_work);
		break;
	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}

/**
 * mmc_regulator_register_undervoltage_notifier - Register for undervoltage
 *						  events
 * @host: MMC host
 *
 * To be called by a bus driver when a card supporting graceful shutdown
 * is attached.
 */
void mmc_regulator_register_undervoltage_notifier(struct mmc_host *host)
{
	int ret;

	if (IS_ERR_OR_NULL(host->supply.vmmc))
		return;

	host->supply.vmmc_nb.notifier_call = mmc_handle_regulator_event;
	ret = regulator_register_notifier(host->supply.vmmc,
					  &host->supply.vmmc_nb);
	if (ret)
		dev_warn(mmc_dev(host), "Failed to register vmmc notifier: %d\n", ret);
}

/**
 * mmc_regulator_unregister_undervoltage_notifier - Unregister undervoltage
 *						    notifier
 * @host: MMC host
 */
void mmc_regulator_unregister_undervoltage_notifier(struct mmc_host *host)
{
	if (IS_ERR_OR_NULL(host->supply.vmmc))
		return;

	regulator_unregister_notifier(host->supply.vmmc, &host->supply.vmmc_nb);
	cancel_work_sync(&host->supply.uv_work);
}

/**
 * mmc_regulator_get_supply - try to get VMMC and VQMMC regulators for a host
 * @mmc: the host to regulate
 *
 * Returns 0 or errno. errno should be handled, it is either a critical error
 * or -EPROBE_DEFER. 0 means no critical error but it does not mean all
 * regulators have been found because they all are optional. If you require
 * certain regulators, you need to check separately in your driver if they got
 * populated after calling this function.
 */
int mmc_regulator_get_supply(struct mmc_host *mmc)
{
	struct device *dev = mmc_dev(mmc);
	int ret;

	mmc->supply.vmmc = devm_regulator_get_optional(dev, "vmmc");
	mmc->supply.vqmmc = devm_regulator_get_optional(dev, "vqmmc");
	mmc->supply.vqmmc2 = devm_regulator_get_optional(dev, "vqmmc2");

	if (IS_ERR(mmc->supply.vmmc)) {
		if (PTR_ERR(mmc->supply.vmmc) == -EPROBE_DEFER)
			return dev_err_probe(dev, -EPROBE_DEFER,
					     "vmmc regulator not available\n");

		dev_dbg(dev, "No vmmc regulator found\n");
	} else {
		ret = mmc_regulator_get_ocrmask(mmc->supply.vmmc);
		if (ret > 0)
			mmc->ocr_avail = ret;
		else
			dev_warn(dev, "Failed getting OCR mask: %d\n", ret);
	}

	if (IS_ERR(mmc->supply.vqmmc)) {
		if (PTR_ERR(mmc->supply.vqmmc) == -EPROBE_DEFER)
			return dev_err_probe(dev, -EPROBE_DEFER,
					     "vqmmc regulator not available\n");

		dev_dbg(dev, "No vqmmc regulator found\n");
	}

	if (IS_ERR(mmc->supply.vqmmc2)) {
		if (PTR_ERR(mmc->supply.vqmmc2) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_dbg(dev, "No vqmmc2 regulator found\n");
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mmc_regulator_get_supply);

/**
 * mmc_regulator_enable_vqmmc - enable VQMMC regulator for a host
 * @mmc: the host to regulate
 *
 * Returns 0 or errno. Enables the regulator for vqmmc.
 * Keeps track of the enable status for ensuring that calls to
 * regulator_enable/disable are balanced.
 */
int mmc_regulator_enable_vqmmc(struct mmc_host *mmc)
{
	int ret = 0;

	if (!IS_ERR(mmc->supply.vqmmc) && !mmc->vqmmc_enabled) {
		ret = regulator_enable(mmc->supply.vqmmc);
		if (ret < 0)
			dev_err(mmc_dev(mmc), "enabling vqmmc regulator failed\n");
		else
			mmc->vqmmc_enabled = true;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(mmc_regulator_enable_vqmmc);

/**
 * mmc_regulator_disable_vqmmc - disable VQMMC regulator for a host
 * @mmc: the host to regulate
 *
 * Returns 0 or errno. Disables the regulator for vqmmc.
 * Keeps track of the enable status for ensuring that calls to
 * regulator_enable/disable are balanced.
 */
void mmc_regulator_disable_vqmmc(struct mmc_host *mmc)
{
	if (!IS_ERR(mmc->supply.vqmmc) && mmc->vqmmc_enabled) {
		regulator_disable(mmc->supply.vqmmc);
		mmc->vqmmc_enabled = false;
	}
}
EXPORT_SYMBOL_GPL(mmc_regulator_disable_vqmmc);
