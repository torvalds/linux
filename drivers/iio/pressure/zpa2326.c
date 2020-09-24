/*
 * Murata ZPA2326 pressure and temperature sensor IIO driver
 *
 * Copyright (c) 2016 Parrot S.A.
 *
 * Author: Gregor Boirie <gregor.boirie@parrot.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

/**
 * DOC: ZPA2326 theory of operations
 *
 * This driver supports %INDIO_DIRECT_MODE and %INDIO_BUFFER_TRIGGERED IIO
 * modes.
 * A internal hardware trigger is also implemented to dispatch registered IIO
 * trigger consumers upon "sample ready" interrupts.
 *
 * ZPA2326 hardware supports 2 sampling mode: one shot and continuous.
 *
 * A complete one shot sampling cycle gets device out of low power mode,
 * performs pressure and temperature measurements, then automatically switches
 * back to low power mode. It is meant for on demand sampling with optimal power
 * saving at the cost of lower sampling rate and higher software overhead.
 * This is a natural candidate for IIO read_raw hook implementation
 * (%INDIO_DIRECT_MODE). It is also used for triggered buffering support to
 * ensure explicit synchronization with external trigger events
 * (%INDIO_BUFFER_TRIGGERED).
 *
 * The continuous mode works according to a periodic hardware measurement
 * process continuously pushing samples into an internal hardware FIFO (for
 * pressure samples only). Measurement cycle completion may be signaled by a
 * "sample ready" interrupt.
 * Typical software sequence of operations :
 * - get device out of low power mode,
 * - setup hardware sampling period,
 * - at end of period, upon data ready interrupt: pop pressure samples out of
 *   hardware FIFO and fetch temperature sample
 * - when no longer needed, stop sampling process by putting device into
 *   low power mode.
 * This mode is used to implement %INDIO_BUFFER_TRIGGERED mode if device tree
 * declares a valid interrupt line. In this case, the internal hardware trigger
 * drives acquisition.
 *
 * Note that hardware sampling frequency is taken into account only when
 * internal hardware trigger is attached as the highest sampling rate seems to
 * be the most energy efficient.
 *
 * TODO:
 *   preset pressure threshold crossing / IIO events ;
 *   differential pressure sampling ;
 *   hardware samples averaging.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include "zpa2326.h"

/* 200 ms should be enough for the longest conversion time in one-shot mode. */
#define ZPA2326_CONVERSION_JIFFIES (HZ / 5)

/* There should be a 1 ms delay (Tpup) after getting out of reset. */
#define ZPA2326_TPUP_USEC_MIN      (1000)
#define ZPA2326_TPUP_USEC_MAX      (2000)

/**
 * struct zpa2326_frequency - Hardware sampling frequency descriptor
 * @hz : Frequency in Hertz.
 * @odr: Output Data Rate word as expected by %ZPA2326_CTRL_REG3_REG.
 */
struct zpa2326_frequency {
	int hz;
	u16 odr;
};

/*
 * Keep these in strict ascending order: last array entry is expected to
 * correspond to the highest sampling frequency.
 */
static const struct zpa2326_frequency zpa2326_sampling_frequencies[] = {
	{ .hz = 1,  .odr = 1 << ZPA2326_CTRL_REG3_ODR_SHIFT },
	{ .hz = 5,  .odr = 5 << ZPA2326_CTRL_REG3_ODR_SHIFT },
	{ .hz = 11, .odr = 6 << ZPA2326_CTRL_REG3_ODR_SHIFT },
	{ .hz = 23, .odr = 7 << ZPA2326_CTRL_REG3_ODR_SHIFT },
};

/* Return the highest hardware sampling frequency available. */
static const struct zpa2326_frequency *zpa2326_highest_frequency(void)
{
	return &zpa2326_sampling_frequencies[
		ARRAY_SIZE(zpa2326_sampling_frequencies) - 1];
}

/**
 * struct zpa_private - Per-device internal private state
 * @timestamp:  Buffered samples ready datum.
 * @regmap:     Underlying I2C / SPI bus adapter used to abstract slave register
 *              accesses.
 * @result:     Allows sampling logic to get completion status of operations
 *              that interrupt handlers perform asynchronously.
 * @data_ready: Interrupt handler uses this to wake user context up at sampling
 *              operation completion.
 * @trigger:    Optional hardware / interrupt driven trigger used to notify
 *              external devices a new sample is ready.
 * @waken:      Flag indicating whether or not device has just been powered on.
 * @irq:        Optional interrupt line: negative or zero if not declared into
 *              DT, in which case sampling logic keeps polling status register
 *              to detect completion.
 * @frequency:  Current hardware sampling frequency.
 * @vref:       Power / voltage reference.
 * @vdd:        Power supply.
 */
struct zpa2326_private {
	s64                             timestamp;
	struct regmap                  *regmap;
	int                             result;
	struct completion               data_ready;
	struct iio_trigger             *trigger;
	bool                            waken;
	int                             irq;
	const struct zpa2326_frequency *frequency;
	struct regulator               *vref;
	struct regulator               *vdd;
};

#define zpa2326_err(idev, fmt, ...)					\
	dev_err(idev->dev.parent, fmt "\n", ##__VA_ARGS__)

#define zpa2326_warn(idev, fmt, ...)					\
	dev_warn(idev->dev.parent, fmt "\n", ##__VA_ARGS__)

#define zpa2326_dbg(idev, fmt, ...)					\
	dev_dbg(idev->dev.parent, fmt "\n", ##__VA_ARGS__)

bool zpa2326_isreg_writeable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ZPA2326_REF_P_XL_REG:
	case ZPA2326_REF_P_L_REG:
	case ZPA2326_REF_P_H_REG:
	case ZPA2326_RES_CONF_REG:
	case ZPA2326_CTRL_REG0_REG:
	case ZPA2326_CTRL_REG1_REG:
	case ZPA2326_CTRL_REG2_REG:
	case ZPA2326_CTRL_REG3_REG:
	case ZPA2326_THS_P_LOW_REG:
	case ZPA2326_THS_P_HIGH_REG:
		return true;

	default:
		return false;
	}
}
EXPORT_SYMBOL_GPL(zpa2326_isreg_writeable);

bool zpa2326_isreg_readable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ZPA2326_REF_P_XL_REG:
	case ZPA2326_REF_P_L_REG:
	case ZPA2326_REF_P_H_REG:
	case ZPA2326_DEVICE_ID_REG:
	case ZPA2326_RES_CONF_REG:
	case ZPA2326_CTRL_REG0_REG:
	case ZPA2326_CTRL_REG1_REG:
	case ZPA2326_CTRL_REG2_REG:
	case ZPA2326_CTRL_REG3_REG:
	case ZPA2326_INT_SOURCE_REG:
	case ZPA2326_THS_P_LOW_REG:
	case ZPA2326_THS_P_HIGH_REG:
	case ZPA2326_STATUS_REG:
	case ZPA2326_PRESS_OUT_XL_REG:
	case ZPA2326_PRESS_OUT_L_REG:
	case ZPA2326_PRESS_OUT_H_REG:
	case ZPA2326_TEMP_OUT_L_REG:
	case ZPA2326_TEMP_OUT_H_REG:
		return true;

	default:
		return false;
	}
}
EXPORT_SYMBOL_GPL(zpa2326_isreg_readable);

bool zpa2326_isreg_precious(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ZPA2326_INT_SOURCE_REG:
	case ZPA2326_PRESS_OUT_H_REG:
		return true;

	default:
		return false;
	}
}
EXPORT_SYMBOL_GPL(zpa2326_isreg_precious);

/**
 * zpa2326_enable_device() - Enable device, i.e. get out of low power mode.
 * @indio_dev: The IIO device associated with the hardware to enable.
 *
 * Required to access complete register space and to perform any sampling
 * or control operations.
 *
 * Return: Zero when successful, a negative error code otherwise.
 */
static int zpa2326_enable_device(const struct iio_dev *indio_dev)
{
	int err;

	err = regmap_write(((struct zpa2326_private *)
			    iio_priv(indio_dev))->regmap,
			    ZPA2326_CTRL_REG0_REG, ZPA2326_CTRL_REG0_ENABLE);
	if (err) {
		zpa2326_err(indio_dev, "failed to enable device (%d)", err);
		return err;
	}

	zpa2326_dbg(indio_dev, "enabled");

	return 0;
}

/**
 * zpa2326_sleep() - Disable device, i.e. switch to low power mode.
 * @indio_dev: The IIO device associated with the hardware to disable.
 *
 * Only %ZPA2326_DEVICE_ID_REG and %ZPA2326_CTRL_REG0_REG registers may be
 * accessed once device is in the disabled state.
 *
 * Return: Zero when successful, a negative error code otherwise.
 */
static int zpa2326_sleep(const struct iio_dev *indio_dev)
{
	int err;

	err = regmap_write(((struct zpa2326_private *)
			    iio_priv(indio_dev))->regmap,
			    ZPA2326_CTRL_REG0_REG, 0);
	if (err) {
		zpa2326_err(indio_dev, "failed to sleep (%d)", err);
		return err;
	}

	zpa2326_dbg(indio_dev, "sleeping");

	return 0;
}

/**
 * zpa2326_reset_device() - Reset device to default hardware state.
 * @indio_dev: The IIO device associated with the hardware to reset.
 *
 * Disable sampling and empty hardware FIFO.
 * Device must be enabled before reset, i.e. not in low power mode.
 *
 * Return: Zero when successful, a negative error code otherwise.
 */
static int zpa2326_reset_device(const struct iio_dev *indio_dev)
{
	int err;

	err = regmap_write(((struct zpa2326_private *)
			    iio_priv(indio_dev))->regmap,
			    ZPA2326_CTRL_REG2_REG, ZPA2326_CTRL_REG2_SWRESET);
	if (err) {
		zpa2326_err(indio_dev, "failed to reset device (%d)", err);
		return err;
	}

	usleep_range(ZPA2326_TPUP_USEC_MIN, ZPA2326_TPUP_USEC_MAX);

	zpa2326_dbg(indio_dev, "reset");

	return 0;
}

/**
 * zpa2326_start_oneshot() - Start a single sampling cycle, i.e. in one shot
 *                           mode.
 * @indio_dev: The IIO device associated with the sampling hardware.
 *
 * Device must have been previously enabled and configured for one shot mode.
 * Device will be switched back to low power mode at end of cycle.
 *
 * Return: Zero when successful, a negative error code otherwise.
 */
static int zpa2326_start_oneshot(const struct iio_dev *indio_dev)
{
	int err;

	err = regmap_write(((struct zpa2326_private *)
			    iio_priv(indio_dev))->regmap,
			    ZPA2326_CTRL_REG0_REG,
			    ZPA2326_CTRL_REG0_ENABLE |
			    ZPA2326_CTRL_REG0_ONE_SHOT);
	if (err) {
		zpa2326_err(indio_dev, "failed to start one shot cycle (%d)",
			    err);
		return err;
	}

	zpa2326_dbg(indio_dev, "one shot cycle started");

	return 0;
}

/**
 * zpa2326_power_on() - Power on device to allow subsequent configuration.
 * @indio_dev: The IIO device associated with the sampling hardware.
 * @private:   Internal private state related to @indio_dev.
 *
 * Sampling will be disabled, preventing strange things from happening in our
 * back. Hardware FIFO content will be cleared.
 * When successful, device will be left in the enabled state to allow further
 * configuration.
 *
 * Return: Zero when successful, a negative error code otherwise.
 */
static int zpa2326_power_on(const struct iio_dev         *indio_dev,
			    const struct zpa2326_private *private)
{
	int err;

	err = regulator_enable(private->vref);
	if (err)
		return err;

	err = regulator_enable(private->vdd);
	if (err)
		goto vref;

	zpa2326_dbg(indio_dev, "powered on");

	err = zpa2326_enable_device(indio_dev);
	if (err)
		goto vdd;

	err = zpa2326_reset_device(indio_dev);
	if (err)
		goto sleep;

	return 0;

sleep:
	zpa2326_sleep(indio_dev);
vdd:
	regulator_disable(private->vdd);
vref:
	regulator_disable(private->vref);

	zpa2326_dbg(indio_dev, "powered off");

	return err;
}

/**
 * zpa2326_power_off() - Power off device, i.e. disable attached power
 *                       regulators.
 * @indio_dev: The IIO device associated with the sampling hardware.
 * @private:   Internal private state related to @indio_dev.
 *
 * Return: Zero when successful, a negative error code otherwise.
 */
static void zpa2326_power_off(const struct iio_dev         *indio_dev,
			      const struct zpa2326_private *private)
{
	regulator_disable(private->vdd);
	regulator_disable(private->vref);

	zpa2326_dbg(indio_dev, "powered off");
}

/**
 * zpa2326_config_oneshot() - Setup device for one shot / on demand mode.
 * @indio_dev: The IIO device associated with the sampling hardware.
 * @irq:       Optional interrupt line the hardware uses to notify new data
 *             samples are ready. Negative or zero values indicate no interrupts
 *             are available, meaning polling is required.
 *
 * Output Data Rate is configured for the highest possible rate so that
 * conversion time and power consumption are reduced to a minimum.
 * Note that hardware internal averaging machinery (not implemented in this
 * driver) is not applicable in this mode.
 *
 * Device must have been previously enabled before calling
 * zpa2326_config_oneshot().
 *
 * Return: Zero when successful, a negative error code otherwise.
 */
static int zpa2326_config_oneshot(const struct iio_dev *indio_dev,
				  int                   irq)
{
	struct regmap                  *regs = ((struct zpa2326_private *)
						iio_priv(indio_dev))->regmap;
	const struct zpa2326_frequency *freq = zpa2326_highest_frequency();
	int                             err;

	/* Setup highest available Output Data Rate for one shot mode. */
	err = regmap_write(regs, ZPA2326_CTRL_REG3_REG, freq->odr);
	if (err)
		return err;

	if (irq > 0) {
		/* Request interrupt when new sample is available. */
		err = regmap_write(regs, ZPA2326_CTRL_REG1_REG,
				   (u8)~ZPA2326_CTRL_REG1_MASK_DATA_READY);

		if (err) {
			dev_err(indio_dev->dev.parent,
				"failed to setup one shot mode (%d)", err);
			return err;
		}
	}

	zpa2326_dbg(indio_dev, "one shot mode setup @%dHz", freq->hz);

	return 0;
}

/**
 * zpa2326_clear_fifo() - Clear remaining entries in hardware FIFO.
 * @indio_dev: The IIO device associated with the sampling hardware.
 * @min_count: Number of samples present within hardware FIFO.
 *
 * @min_count argument is a hint corresponding to the known minimum number of
 * samples currently living in the FIFO. This allows to reduce the number of bus
 * accesses by skipping status register read operation as long as we know for
 * sure there are still entries left.
 *
 * Return: Zero when successful, a negative error code otherwise.
 */
static int zpa2326_clear_fifo(const struct iio_dev *indio_dev,
			      unsigned int          min_count)
{
	struct regmap *regs = ((struct zpa2326_private *)
			       iio_priv(indio_dev))->regmap;
	int            err;
	unsigned int   val;

	if (!min_count) {
		/*
		 * No hint: read status register to determine whether FIFO is
		 * empty or not.
		 */
		err = regmap_read(regs, ZPA2326_STATUS_REG, &val);

		if (err < 0)
			goto err;

		if (val & ZPA2326_STATUS_FIFO_E)
			/* Fifo is empty: nothing to trash. */
			return 0;
	}

	/* Clear FIFO. */
	do {
		/*
		 * A single fetch from pressure MSB register is enough to pop
		 * values out of FIFO.
		 */
		err = regmap_read(regs, ZPA2326_PRESS_OUT_H_REG, &val);
		if (err < 0)
			goto err;

		if (min_count) {
			/*
			 * We know for sure there are at least min_count entries
			 * left in FIFO. Skip status register read.
			 */
			min_count--;
			continue;
		}

		err = regmap_read(regs, ZPA2326_STATUS_REG, &val);
		if (err < 0)
			goto err;

	} while (!(val & ZPA2326_STATUS_FIFO_E));

	zpa2326_dbg(indio_dev, "FIFO cleared");

	return 0;

err:
	zpa2326_err(indio_dev, "failed to clear FIFO (%d)", err);

	return err;
}

/**
 * zpa2326_dequeue_pressure() - Retrieve the most recent pressure sample from
 *                              hardware FIFO.
 * @indio_dev: The IIO device associated with the sampling hardware.
 * @pressure:  Sampled pressure output.
 *
 * Note that ZPA2326 hardware FIFO stores pressure samples only.
 *
 * Return: Zero when successful, a negative error code otherwise.
 */
static int zpa2326_dequeue_pressure(const struct iio_dev *indio_dev,
				    u32                  *pressure)
{
	struct regmap *regs = ((struct zpa2326_private *)
			       iio_priv(indio_dev))->regmap;
	unsigned int   val;
	int            err;
	int            cleared = -1;

	err = regmap_read(regs, ZPA2326_STATUS_REG, &val);
	if (err < 0)
		return err;

	*pressure = 0;

	if (val & ZPA2326_STATUS_P_OR) {
		/*
		 * Fifo overrun : first sample dequeued from FIFO is the
		 * newest.
		 */
		zpa2326_warn(indio_dev, "FIFO overflow");

		err = regmap_bulk_read(regs, ZPA2326_PRESS_OUT_XL_REG, pressure,
				       3);
		if (err)
			return err;

#define ZPA2326_FIFO_DEPTH (16U)
		/* Hardware FIFO may hold no more than 16 pressure samples. */
		return zpa2326_clear_fifo(indio_dev, ZPA2326_FIFO_DEPTH - 1);
	}

	/*
	 * Fifo has not overflown : retrieve newest sample. We need to pop
	 * values out until FIFO is empty : last fetched pressure is the newest.
	 * In nominal cases, we should find a single queued sample only.
	 */
	do {
		err = regmap_bulk_read(regs, ZPA2326_PRESS_OUT_XL_REG, pressure,
				       3);
		if (err)
			return err;

		err = regmap_read(regs, ZPA2326_STATUS_REG, &val);
		if (err < 0)
			return err;

		cleared++;
	} while (!(val & ZPA2326_STATUS_FIFO_E));

	if (cleared)
		/*
		 * Samples were pushed by hardware during previous rounds but we
		 * didn't consume them fast enough: inform user.
		 */
		zpa2326_dbg(indio_dev, "cleared %d FIFO entries", cleared);

	return 0;
}

/**
 * zpa2326_fill_sample_buffer() - Enqueue new channel samples to IIO buffer.
 * @indio_dev: The IIO device associated with the sampling hardware.
 * @private:   Internal private state related to @indio_dev.
 *
 * Return: Zero when successful, a negative error code otherwise.
 */
static int zpa2326_fill_sample_buffer(struct iio_dev               *indio_dev,
				      const struct zpa2326_private *private)
{
	struct {
		u32 pressure;
		u16 temperature;
		u64 timestamp;
	}   sample;
	int err;

	if (test_bit(0, indio_dev->active_scan_mask)) {
		/* Get current pressure from hardware FIFO. */
		err = zpa2326_dequeue_pressure(indio_dev, &sample.pressure);
		if (err) {
			zpa2326_warn(indio_dev, "failed to fetch pressure (%d)",
				     err);
			return err;
		}
	}

	if (test_bit(1, indio_dev->active_scan_mask)) {
		/* Get current temperature. */
		err = regmap_bulk_read(private->regmap, ZPA2326_TEMP_OUT_L_REG,
				       &sample.temperature, 2);
		if (err) {
			zpa2326_warn(indio_dev,
				     "failed to fetch temperature (%d)", err);
			return err;
		}
	}

	/*
	 * Now push samples using timestamp stored either :
	 *   - by hardware interrupt handler if interrupt is available: see
	 *     zpa2326_handle_irq(),
	 *   - or oneshot completion polling machinery : see
	 *     zpa2326_trigger_handler().
	 */
	zpa2326_dbg(indio_dev, "filling raw samples buffer");

	iio_push_to_buffers_with_timestamp(indio_dev, &sample,
					   private->timestamp);

	return 0;
}

#ifdef CONFIG_PM
static int zpa2326_runtime_suspend(struct device *parent)
{
	const struct iio_dev *indio_dev = dev_get_drvdata(parent);

	if (pm_runtime_autosuspend_expiration(parent))
		/* Userspace changed autosuspend delay. */
		return -EAGAIN;

	zpa2326_power_off(indio_dev, iio_priv(indio_dev));

	return 0;
}

static int zpa2326_runtime_resume(struct device *parent)
{
	const struct iio_dev *indio_dev = dev_get_drvdata(parent);

	return zpa2326_power_on(indio_dev, iio_priv(indio_dev));
}

const struct dev_pm_ops zpa2326_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(zpa2326_runtime_suspend, zpa2326_runtime_resume,
			   NULL)
};
EXPORT_SYMBOL_GPL(zpa2326_pm_ops);

/**
 * zpa2326_resume() - Request the PM layer to power supply the device.
 * @indio_dev: The IIO device associated with the sampling hardware.
 *
 * Return:
 *  < 0 - a negative error code meaning failure ;
 *    0 - success, device has just been powered up ;
 *    1 - success, device was already powered.
 */
static int zpa2326_resume(const struct iio_dev *indio_dev)
{
	int err;

	err = pm_runtime_get_sync(indio_dev->dev.parent);
	if (err < 0) {
		pm_runtime_put(indio_dev->dev.parent);
		return err;
	}

	if (err > 0) {
		/*
		 * Device was already power supplied: get it out of low power
		 * mode and inform caller.
		 */
		zpa2326_enable_device(indio_dev);
		return 1;
	}

	/* Inform caller device has just been brought back to life. */
	return 0;
}

/**
 * zpa2326_suspend() - Schedule a power down using autosuspend feature of PM
 *                     layer.
 * @indio_dev: The IIO device associated with the sampling hardware.
 *
 * Device is switched to low power mode at first to save power even when
 * attached regulator is a "dummy" one.
 */
static void zpa2326_suspend(struct iio_dev *indio_dev)
{
	struct device *parent = indio_dev->dev.parent;

	zpa2326_sleep(indio_dev);

	pm_runtime_mark_last_busy(parent);
	pm_runtime_put_autosuspend(parent);
}

static void zpa2326_init_runtime(struct device *parent)
{
	pm_runtime_get_noresume(parent);
	pm_runtime_set_active(parent);
	pm_runtime_enable(parent);
	pm_runtime_set_autosuspend_delay(parent, 1000);
	pm_runtime_use_autosuspend(parent);
	pm_runtime_mark_last_busy(parent);
	pm_runtime_put_autosuspend(parent);
}

static void zpa2326_fini_runtime(struct device *parent)
{
	pm_runtime_disable(parent);
	pm_runtime_set_suspended(parent);
}
#else /* !CONFIG_PM */
static int zpa2326_resume(const struct iio_dev *indio_dev)
{
	zpa2326_enable_device(indio_dev);

	return 0;
}

static void zpa2326_suspend(struct iio_dev *indio_dev)
{
	zpa2326_sleep(indio_dev);
}

#define zpa2326_init_runtime(_parent)
#define zpa2326_fini_runtime(_parent)
#endif /* !CONFIG_PM */

/**
 * zpa2326_handle_irq() - Process hardware interrupts.
 * @irq:  Interrupt line the hardware uses to notify new data has arrived.
 * @data: The IIO device associated with the sampling hardware.
 *
 * Timestamp buffered samples as soon as possible then schedule threaded bottom
 * half.
 *
 * Return: Always successful.
 */
static irqreturn_t zpa2326_handle_irq(int irq, void *data)
{
	struct iio_dev *indio_dev = data;

	if (iio_buffer_enabled(indio_dev)) {
		/* Timestamping needed for buffered sampling only. */
		((struct zpa2326_private *)
		 iio_priv(indio_dev))->timestamp = iio_get_time_ns(indio_dev);
	}

	return IRQ_WAKE_THREAD;
}

/**
 * zpa2326_handle_threaded_irq() - Interrupt bottom-half handler.
 * @irq:  Interrupt line the hardware uses to notify new data has arrived.
 * @data: The IIO device associated with the sampling hardware.
 *
 * Mainly ensures interrupt is caused by a real "new sample available"
 * condition. This relies upon the ability to perform blocking / sleeping bus
 * accesses to slave's registers. This is why zpa2326_handle_threaded_irq() is
 * called from within a thread, i.e. not called from hard interrupt context.
 *
 * When device is using its own internal hardware trigger in continuous sampling
 * mode, data are available into hardware FIFO once interrupt has occurred. All
 * we have to do is to dispatch the trigger, which in turn will fetch data and
 * fill IIO buffer.
 *
 * When not using its own internal hardware trigger, the device has been
 * configured in one-shot mode either by an external trigger or the IIO read_raw
 * hook. This means one of the latter is currently waiting for sampling
 * completion, in which case we must simply wake it up.
 *
 * See zpa2326_trigger_handler().
 *
 * Return:
 *   %IRQ_NONE - no consistent interrupt happened ;
 *   %IRQ_HANDLED - there was new samples available.
 */
static irqreturn_t zpa2326_handle_threaded_irq(int irq, void *data)
{
	struct iio_dev         *indio_dev = data;
	struct zpa2326_private *priv = iio_priv(indio_dev);
	unsigned int            val;
	bool                    cont;
	irqreturn_t             ret = IRQ_NONE;

	/*
	 * Are we using our own internal trigger in triggered buffer mode, i.e.,
	 * currently working in continuous sampling mode ?
	 */
	cont = (iio_buffer_enabled(indio_dev) &&
		iio_trigger_using_own(indio_dev));

	/*
	 * Device works according to a level interrupt scheme: reading interrupt
	 * status de-asserts interrupt line.
	 */
	priv->result = regmap_read(priv->regmap, ZPA2326_INT_SOURCE_REG, &val);
	if (priv->result < 0) {
		if (cont)
			return IRQ_NONE;

		goto complete;
	}

	/* Data ready is the only interrupt source we requested. */
	if (!(val & ZPA2326_INT_SOURCE_DATA_READY)) {
		/*
		 * Interrupt happened but no new sample available: likely caused
		 * by spurious interrupts, in which case, returning IRQ_NONE
		 * allows to benefit from the generic spurious interrupts
		 * handling.
		 */
		zpa2326_warn(indio_dev, "unexpected interrupt status %02x",
			     val);

		if (cont)
			return IRQ_NONE;

		priv->result = -ENODATA;
		goto complete;
	}

	/* New sample available: dispatch internal trigger consumers. */
	iio_trigger_poll_chained(priv->trigger);

	if (cont)
		/*
		 * Internal hardware trigger has been scheduled above : it will
		 * fetch data on its own.
		 */
		return IRQ_HANDLED;

	ret = IRQ_HANDLED;

complete:
	/*
	 * Wake up direct or externaly triggered buffer mode waiters: see
	 * zpa2326_sample_oneshot() and zpa2326_trigger_handler().
	 */
	complete(&priv->data_ready);

	return ret;
}

/**
 * zpa2326_wait_oneshot_completion() - Wait for oneshot data ready interrupt.
 * @indio_dev: The IIO device associated with the sampling hardware.
 * @private:   Internal private state related to @indio_dev.
 *
 * Return: Zero when successful, a negative error code otherwise.
 */
static int zpa2326_wait_oneshot_completion(const struct iio_dev   *indio_dev,
					   struct zpa2326_private *private)
{
	unsigned int val;
	long     timeout;

	zpa2326_dbg(indio_dev, "waiting for one shot completion interrupt");

	timeout = wait_for_completion_interruptible_timeout(
		&private->data_ready, ZPA2326_CONVERSION_JIFFIES);
	if (timeout > 0)
		/*
		 * Interrupt handler completed before timeout: return operation
		 * status.
		 */
		return private->result;

	/* Clear all interrupts just to be sure. */
	regmap_read(private->regmap, ZPA2326_INT_SOURCE_REG, &val);

	if (!timeout) {
		/* Timed out. */
		zpa2326_warn(indio_dev, "no one shot interrupt occurred (%ld)",
			     timeout);
		return -ETIME;
	}

	zpa2326_warn(indio_dev, "wait for one shot interrupt cancelled");
	return -ERESTARTSYS;
}

static int zpa2326_init_managed_irq(struct device          *parent,
				    struct iio_dev         *indio_dev,
				    struct zpa2326_private *private,
				    int                     irq)
{
	int err;

	private->irq = irq;

	if (irq <= 0) {
		/*
		 * Platform declared no interrupt line: device will be polled
		 * for data availability.
		 */
		dev_info(parent, "no interrupt found, running in polling mode");
		return 0;
	}

	init_completion(&private->data_ready);

	/* Request handler to be scheduled into threaded interrupt context. */
	err = devm_request_threaded_irq(parent, irq, zpa2326_handle_irq,
					zpa2326_handle_threaded_irq,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					dev_name(parent), indio_dev);
	if (err) {
		dev_err(parent, "failed to request interrupt %d (%d)", irq,
			err);
		return err;
	}

	dev_info(parent, "using interrupt %d", irq);

	return 0;
}

/**
 * zpa2326_poll_oneshot_completion() - Actively poll for one shot data ready.
 * @indio_dev: The IIO device associated with the sampling hardware.
 *
 * Loop over registers content to detect end of sampling cycle. Used when DT
 * declared no valid interrupt lines.
 *
 * Return: Zero when successful, a negative error code otherwise.
 */
static int zpa2326_poll_oneshot_completion(const struct iio_dev *indio_dev)
{
	unsigned long  tmout = jiffies + ZPA2326_CONVERSION_JIFFIES;
	struct regmap *regs = ((struct zpa2326_private *)
			       iio_priv(indio_dev))->regmap;
	unsigned int   val;
	int            err;

	zpa2326_dbg(indio_dev, "polling for one shot completion");

	/*
	 * At least, 100 ms is needed for the device to complete its one-shot
	 * cycle.
	 */
	if (msleep_interruptible(100))
		return -ERESTARTSYS;

	/* Poll for conversion completion in hardware. */
	while (true) {
		err = regmap_read(regs, ZPA2326_CTRL_REG0_REG, &val);
		if (err < 0)
			goto err;

		if (!(val & ZPA2326_CTRL_REG0_ONE_SHOT))
			/* One-shot bit self clears at conversion end. */
			break;

		if (time_after(jiffies, tmout)) {
			/* Prevent from waiting forever : let's time out. */
			err = -ETIME;
			goto err;
		}

		usleep_range(10000, 20000);
	}

	/*
	 * In oneshot mode, pressure sample availability guarantees that
	 * temperature conversion has also completed : just check pressure
	 * status bit to keep things simple.
	 */
	err = regmap_read(regs, ZPA2326_STATUS_REG, &val);
	if (err < 0)
		goto err;

	if (!(val & ZPA2326_STATUS_P_DA)) {
		/* No sample available. */
		err = -ENODATA;
		goto err;
	}

	return 0;

err:
	zpa2326_warn(indio_dev, "failed to poll one shot completion (%d)", err);

	return err;
}

/**
 * zpa2326_fetch_raw_sample() - Retrieve a raw sample and convert it to CPU
 *                              endianness.
 * @indio_dev: The IIO device associated with the sampling hardware.
 * @type:      Type of measurement / channel to fetch from.
 * @value:     Sample output.
 *
 * Return: Zero when successful, a negative error code otherwise.
 */
static int zpa2326_fetch_raw_sample(const struct iio_dev *indio_dev,
				    enum iio_chan_type    type,
				    int                  *value)
{
	struct regmap *regs = ((struct zpa2326_private *)
			       iio_priv(indio_dev))->regmap;
	int            err;

	switch (type) {
	case IIO_PRESSURE:
		zpa2326_dbg(indio_dev, "fetching raw pressure sample");

		err = regmap_bulk_read(regs, ZPA2326_PRESS_OUT_XL_REG, value,
				       3);
		if (err) {
			zpa2326_warn(indio_dev, "failed to fetch pressure (%d)",
				     err);
			return err;
		}

		/* Pressure is a 24 bits wide little-endian unsigned int. */
		*value = (((u8 *)value)[2] << 16) | (((u8 *)value)[1] << 8) |
			 ((u8 *)value)[0];

		return IIO_VAL_INT;

	case IIO_TEMP:
		zpa2326_dbg(indio_dev, "fetching raw temperature sample");

		err = regmap_bulk_read(regs, ZPA2326_TEMP_OUT_L_REG, value, 2);
		if (err) {
			zpa2326_warn(indio_dev,
				     "failed to fetch temperature (%d)", err);
			return err;
		}

		/* Temperature is a 16 bits wide little-endian signed int. */
		*value = (int)le16_to_cpup((__le16 *)value);

		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

/**
 * zpa2326_sample_oneshot() - Perform a complete one shot sampling cycle.
 * @indio_dev: The IIO device associated with the sampling hardware.
 * @type:      Type of measurement / channel to fetch from.
 * @value:     Sample output.
 *
 * Return: Zero when successful, a negative error code otherwise.
 */
static int zpa2326_sample_oneshot(struct iio_dev     *indio_dev,
				  enum iio_chan_type  type,
				  int                *value)
{
	int                     ret;
	struct zpa2326_private *priv;

	ret = iio_device_claim_direct_mode(indio_dev);
	if (ret)
		return ret;

	ret = zpa2326_resume(indio_dev);
	if (ret < 0)
		goto release;

	priv = iio_priv(indio_dev);

	if (ret > 0) {
		/*
		 * We were already power supplied. Just clear hardware FIFO to
		 * get rid of samples acquired during previous rounds (if any).
		 * Sampling operation always generates both temperature and
		 * pressure samples. The latter are always enqueued into
		 * hardware FIFO. This may lead to situations were pressure
		 * samples still sit into FIFO when previous cycle(s) fetched
		 * temperature data only.
		 * Hence, we need to clear hardware FIFO content to prevent from
		 * getting outdated values at the end of current cycle.
		 */
		if (type == IIO_PRESSURE) {
			ret = zpa2326_clear_fifo(indio_dev, 0);
			if (ret)
				goto suspend;
		}
	} else {
		/*
		 * We have just been power supplied, i.e. device is in default
		 * "out of reset" state, meaning we need to reconfigure it
		 * entirely.
		 */
		ret = zpa2326_config_oneshot(indio_dev, priv->irq);
		if (ret)
			goto suspend;
	}

	/* Start a sampling cycle in oneshot mode. */
	ret = zpa2326_start_oneshot(indio_dev);
	if (ret)
		goto suspend;

	/* Wait for sampling cycle to complete. */
	if (priv->irq > 0)
		ret = zpa2326_wait_oneshot_completion(indio_dev, priv);
	else
		ret = zpa2326_poll_oneshot_completion(indio_dev);

	if (ret)
		goto suspend;

	/* Retrieve raw sample value and convert it to CPU endianness. */
	ret = zpa2326_fetch_raw_sample(indio_dev, type, value);

suspend:
	zpa2326_suspend(indio_dev);
release:
	iio_device_release_direct_mode(indio_dev);

	return ret;
}

/**
 * zpa2326_trigger_handler() - Perform an IIO buffered sampling round in one
 *                             shot mode.
 * @irq:  The software interrupt assigned to @data
 * @data: The IIO poll function dispatched by external trigger our device is
 *        attached to.
 *
 * Bottom-half handler called by the IIO trigger to which our device is
 * currently attached. Allows us to synchronize this device buffered sampling
 * either with external events (such as timer expiration, external device sample
 * ready, etc...) or with its own interrupt (internal hardware trigger).
 *
 * When using an external trigger, basically run the same sequence of operations
 * as for zpa2326_sample_oneshot() with the following hereafter. Hardware FIFO
 * is not cleared since already done at buffering enable time and samples
 * dequeueing always retrieves the most recent value.
 *
 * Otherwise, when internal hardware trigger has dispatched us, just fetch data
 * from hardware FIFO.
 *
 * Fetched data will pushed unprocessed to IIO buffer since samples conversion
 * is delegated to userspace in buffered mode (endianness, etc...).
 *
 * Return:
 *   %IRQ_NONE - no consistent interrupt happened ;
 *   %IRQ_HANDLED - there was new samples available.
 */
static irqreturn_t zpa2326_trigger_handler(int irq, void *data)
{
	struct iio_dev         *indio_dev = ((struct iio_poll_func *)
					     data)->indio_dev;
	struct zpa2326_private *priv = iio_priv(indio_dev);
	bool                    cont;

	/*
	 * We have been dispatched, meaning we are in triggered buffer mode.
	 * Using our own internal trigger implies we are currently in continuous
	 * hardware sampling mode.
	 */
	cont = iio_trigger_using_own(indio_dev);

	if (!cont) {
		/* On demand sampling : start a one shot cycle. */
		if (zpa2326_start_oneshot(indio_dev))
			goto out;

		/* Wait for sampling cycle to complete. */
		if (priv->irq <= 0) {
			/* No interrupt available: poll for completion. */
			if (zpa2326_poll_oneshot_completion(indio_dev))
				goto out;

			/* Only timestamp sample once it is ready. */
			priv->timestamp = iio_get_time_ns(indio_dev);
		} else {
			/* Interrupt handlers will timestamp for us. */
			if (zpa2326_wait_oneshot_completion(indio_dev, priv))
				goto out;
		}
	}

	/* Enqueue to IIO buffer / userspace. */
	zpa2326_fill_sample_buffer(indio_dev, priv);

out:
	if (!cont)
		/* Don't switch to low power if sampling continuously. */
		zpa2326_sleep(indio_dev);

	/* Inform attached trigger we are done. */
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

/**
 * zpa2326_preenable_buffer() - Prepare device for configuring triggered
 *                              sampling
 * modes.
 * @indio_dev: The IIO device associated with the sampling hardware.
 *
 * Basically power up device.
 * Called with IIO device's lock held.
 *
 * Return: Zero when successful, a negative error code otherwise.
 */
static int zpa2326_preenable_buffer(struct iio_dev *indio_dev)
{
	int ret = zpa2326_resume(indio_dev);

	if (ret < 0)
		return ret;

	/* Tell zpa2326_postenable_buffer() if we have just been powered on. */
	((struct zpa2326_private *)
	 iio_priv(indio_dev))->waken = iio_priv(indio_dev);

	return 0;
}

/**
 * zpa2326_postenable_buffer() - Configure device for triggered sampling.
 * @indio_dev: The IIO device associated with the sampling hardware.
 *
 * Basically setup one-shot mode if plugging external trigger.
 * Otherwise, let internal trigger configure continuous sampling :
 * see zpa2326_set_trigger_state().
 *
 * If an error is returned, IIO layer will call our postdisable hook for us,
 * i.e. no need to explicitly power device off here.
 * Called with IIO device's lock held.
 *
 * Called with IIO device's lock held.
 *
 * Return: Zero when successful, a negative error code otherwise.
 */
static int zpa2326_postenable_buffer(struct iio_dev *indio_dev)
{
	const struct zpa2326_private *priv = iio_priv(indio_dev);
	int                           err;

	if (!priv->waken) {
		/*
		 * We were already power supplied. Just clear hardware FIFO to
		 * get rid of samples acquired during previous rounds (if any).
		 */
		err = zpa2326_clear_fifo(indio_dev, 0);
		if (err)
			goto err;
	}

	if (!iio_trigger_using_own(indio_dev) && priv->waken) {
		/*
		 * We are using an external trigger and we have just been
		 * powered up: reconfigure one-shot mode.
		 */
		err = zpa2326_config_oneshot(indio_dev, priv->irq);
		if (err)
			goto err;
	}

	/* Plug our own trigger event handler. */
	err = iio_triggered_buffer_postenable(indio_dev);
	if (err)
		goto err;

	return 0;

err:
	zpa2326_err(indio_dev, "failed to enable buffering (%d)", err);

	return err;
}

static int zpa2326_postdisable_buffer(struct iio_dev *indio_dev)
{
	zpa2326_suspend(indio_dev);

	return 0;
}

static const struct iio_buffer_setup_ops zpa2326_buffer_setup_ops = {
	.preenable   = zpa2326_preenable_buffer,
	.postenable  = zpa2326_postenable_buffer,
	.predisable  = iio_triggered_buffer_predisable,
	.postdisable = zpa2326_postdisable_buffer
};

/**
 * zpa2326_set_trigger_state() - Start / stop continuous sampling.
 * @trig:  The trigger being attached to IIO device associated with the sampling
 *         hardware.
 * @state: Tell whether to start (true) or stop (false)
 *
 * Basically enable / disable hardware continuous sampling mode.
 *
 * Called with IIO device's lock held at postenable() or predisable() time.
 *
 * Return: Zero when successful, a negative error code otherwise.
 */
static int zpa2326_set_trigger_state(struct iio_trigger *trig, bool state)
{
	const struct iio_dev         *indio_dev = dev_get_drvdata(
							trig->dev.parent);
	const struct zpa2326_private *priv = iio_priv(indio_dev);
	int                           err;

	if (!state) {
		/*
		 * Switch trigger off : in case of failure, interrupt is left
		 * disabled in order to prevent handler from accessing released
		 * resources.
		 */
		unsigned int val;

		/*
		 * As device is working in continuous mode, handlers may be
		 * accessing resources we are currently freeing...
		 * Prevent this by disabling interrupt handlers and ensure
		 * the device will generate no more interrupts unless explicitly
		 * required to, i.e. by restoring back to default one shot mode.
		 */
		disable_irq(priv->irq);

		/*
		 * Disable continuous sampling mode to restore settings for
		 * one shot / direct sampling operations.
		 */
		err = regmap_write(priv->regmap, ZPA2326_CTRL_REG3_REG,
				   zpa2326_highest_frequency()->odr);
		if (err)
			return err;

		/*
		 * Now that device won't generate interrupts on its own,
		 * acknowledge any currently active interrupts (may happen on
		 * rare occasions while stopping continuous mode).
		 */
		err = regmap_read(priv->regmap, ZPA2326_INT_SOURCE_REG, &val);
		if (err < 0)
			return err;

		/*
		 * Re-enable interrupts only if we can guarantee the device will
		 * generate no more interrupts to prevent handlers from
		 * accessing released resources.
		 */
		enable_irq(priv->irq);

		zpa2326_dbg(indio_dev, "continuous mode stopped");
	} else {
		/*
		 * Switch trigger on : start continuous sampling at required
		 * frequency.
		 */

		if (priv->waken) {
			/* Enable interrupt if getting out of reset. */
			err = regmap_write(priv->regmap, ZPA2326_CTRL_REG1_REG,
					   (u8)
					   ~ZPA2326_CTRL_REG1_MASK_DATA_READY);
			if (err)
				return err;
		}

		/* Enable continuous sampling at specified frequency. */
		err = regmap_write(priv->regmap, ZPA2326_CTRL_REG3_REG,
				   ZPA2326_CTRL_REG3_ENABLE_MEAS |
				   priv->frequency->odr);
		if (err)
			return err;

		zpa2326_dbg(indio_dev, "continuous mode setup @%dHz",
			    priv->frequency->hz);
	}

	return 0;
}

static const struct iio_trigger_ops zpa2326_trigger_ops = {
	.set_trigger_state = zpa2326_set_trigger_state,
};

/**
 * zpa2326_init_trigger() - Create an interrupt driven / hardware trigger
 *                          allowing to notify external devices a new sample is
 *                          ready.
 * @parent:    Hardware sampling device @indio_dev is a child of.
 * @indio_dev: The IIO device associated with the sampling hardware.
 * @private:   Internal private state related to @indio_dev.
 * @irq:       Optional interrupt line the hardware uses to notify new data
 *             samples are ready. Negative or zero values indicate no interrupts
 *             are available, meaning polling is required.
 *
 * Only relevant when DT declares a valid interrupt line.
 *
 * Return: Zero when successful, a negative error code otherwise.
 */
static int zpa2326_init_managed_trigger(struct device          *parent,
					struct iio_dev         *indio_dev,
					struct zpa2326_private *private,
					int                     irq)
{
	struct iio_trigger *trigger;
	int                 ret;

	if (irq <= 0)
		return 0;

	trigger = devm_iio_trigger_alloc(parent, "%s-dev%d",
					 indio_dev->name, indio_dev->id);
	if (!trigger)
		return -ENOMEM;

	/* Basic setup. */
	trigger->dev.parent = parent;
	trigger->ops = &zpa2326_trigger_ops;

	private->trigger = trigger;

	/* Register to triggers space. */
	ret = devm_iio_trigger_register(parent, trigger);
	if (ret)
		dev_err(parent, "failed to register hardware trigger (%d)",
			ret);

	return ret;
}

static int zpa2326_get_frequency(const struct iio_dev *indio_dev)
{
	return ((struct zpa2326_private *)iio_priv(indio_dev))->frequency->hz;
}

static int zpa2326_set_frequency(struct iio_dev *indio_dev, int hz)
{
	struct zpa2326_private *priv = iio_priv(indio_dev);
	int                     freq;
	int                     err;

	/* Check if requested frequency is supported. */
	for (freq = 0; freq < ARRAY_SIZE(zpa2326_sampling_frequencies); freq++)
		if (zpa2326_sampling_frequencies[freq].hz == hz)
			break;
	if (freq == ARRAY_SIZE(zpa2326_sampling_frequencies))
		return -EINVAL;

	/* Don't allow changing frequency if buffered sampling is ongoing. */
	err = iio_device_claim_direct_mode(indio_dev);
	if (err)
		return err;

	priv->frequency = &zpa2326_sampling_frequencies[freq];

	iio_device_release_direct_mode(indio_dev);

	return 0;
}

/* Expose supported hardware sampling frequencies (Hz) through sysfs. */
static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("1 5 11 23");

static struct attribute *zpa2326_attributes[] = {
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL
};

static const struct attribute_group zpa2326_attribute_group = {
	.attrs = zpa2326_attributes,
};

static int zpa2326_read_raw(struct iio_dev             *indio_dev,
			    struct iio_chan_spec const *chan,
			    int                        *val,
			    int                        *val2,
			    long                        mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return zpa2326_sample_oneshot(indio_dev, chan->type, val);

	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_PRESSURE:
			/*
			 * Pressure resolution is 1/64 Pascal. Scale to kPascal
			 * as required by IIO ABI.
			 */
			*val = 1;
			*val2 = 64000;
			return IIO_VAL_FRACTIONAL;

		case IIO_TEMP:
			/*
			 * Temperature follows the equation:
			 *     Temp[degC] = Tempcode * 0.00649 - 176.83
			 * where:
			 *     Tempcode is composed the raw sampled 16 bits.
			 *
			 * Hence, to produce a temperature in milli-degrees
			 * Celsius according to IIO ABI, we need to apply the
			 * following equation to raw samples:
			 *     Temp[milli degC] = (Tempcode + Offset) * Scale
			 * where:
			 *     Offset = -176.83 / 0.00649
			 *     Scale = 0.00649 * 1000
			 */
			*val = 6;
			*val2 = 490000;
			return IIO_VAL_INT_PLUS_MICRO;

		default:
			return -EINVAL;
		}

	case IIO_CHAN_INFO_OFFSET:
		switch (chan->type) {
		case IIO_TEMP:
			*val = -17683000;
			*val2 = 649;
			return IIO_VAL_FRACTIONAL;

		default:
			return -EINVAL;
		}

	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = zpa2326_get_frequency(indio_dev);
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int zpa2326_write_raw(struct iio_dev             *indio_dev,
			     const struct iio_chan_spec *chan,
			     int                         val,
			     int                         val2,
			     long                        mask)
{
	if ((mask != IIO_CHAN_INFO_SAMP_FREQ) || val2)
		return -EINVAL;

	return zpa2326_set_frequency(indio_dev, val);
}

static const struct iio_chan_spec zpa2326_channels[] = {
	[0] = {
		.type                    = IIO_PRESSURE,
		.scan_index              = 0,
		.scan_type               = {
			.sign                   = 'u',
			.realbits               = 24,
			.storagebits            = 32,
			.endianness             = IIO_LE,
		},
		.info_mask_separate      = BIT(IIO_CHAN_INFO_RAW) |
					   BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
	},
	[1] = {
		.type                    = IIO_TEMP,
		.scan_index              = 1,
		.scan_type               = {
			.sign                   = 's',
			.realbits               = 16,
			.storagebits            = 16,
			.endianness             = IIO_LE,
		},
		.info_mask_separate      = BIT(IIO_CHAN_INFO_RAW) |
					   BIT(IIO_CHAN_INFO_SCALE) |
					   BIT(IIO_CHAN_INFO_OFFSET),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
	},
	[2] = IIO_CHAN_SOFT_TIMESTAMP(2),
};

static const struct iio_info zpa2326_info = {
	.attrs         = &zpa2326_attribute_group,
	.read_raw      = zpa2326_read_raw,
	.write_raw     = zpa2326_write_raw,
};

static struct iio_dev *zpa2326_create_managed_iiodev(struct device *device,
						     const char    *name,
						     struct regmap *regmap)
{
	struct iio_dev *indio_dev;

	/* Allocate space to hold IIO device internal state. */
	indio_dev = devm_iio_device_alloc(device,
					  sizeof(struct zpa2326_private));
	if (!indio_dev)
		return NULL;

	/* Setup for userspace synchronous on demand sampling. */
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->dev.parent = device;
	indio_dev->channels = zpa2326_channels;
	indio_dev->num_channels = ARRAY_SIZE(zpa2326_channels);
	indio_dev->name = name;
	indio_dev->info = &zpa2326_info;

	return indio_dev;
}

int zpa2326_probe(struct device *parent,
		  const char    *name,
		  int            irq,
		  unsigned int   hwid,
		  struct regmap *regmap)
{
	struct iio_dev         *indio_dev;
	struct zpa2326_private *priv;
	int                     err;
	unsigned int            id;

	indio_dev = zpa2326_create_managed_iiodev(parent, name, regmap);
	if (!indio_dev)
		return -ENOMEM;

	priv = iio_priv(indio_dev);

	priv->vref = devm_regulator_get(parent, "vref");
	if (IS_ERR(priv->vref))
		return PTR_ERR(priv->vref);

	priv->vdd = devm_regulator_get(parent, "vdd");
	if (IS_ERR(priv->vdd))
		return PTR_ERR(priv->vdd);

	/* Set default hardware sampling frequency to highest rate supported. */
	priv->frequency = zpa2326_highest_frequency();

	/*
	 * Plug device's underlying bus abstraction : this MUST be set before
	 * registering interrupt handlers since an interrupt might happen if
	 * power up sequence is not properly applied.
	 */
	priv->regmap = regmap;

	err = devm_iio_triggered_buffer_setup(parent, indio_dev, NULL,
					      zpa2326_trigger_handler,
					      &zpa2326_buffer_setup_ops);
	if (err)
		return err;

	err = zpa2326_init_managed_trigger(parent, indio_dev, priv, irq);
	if (err)
		return err;

	err = zpa2326_init_managed_irq(parent, indio_dev, priv, irq);
	if (err)
		return err;

	/* Power up to check device ID and perform initial hardware setup. */
	err = zpa2326_power_on(indio_dev, priv);
	if (err)
		return err;

	/* Read id register to check we are talking to the right slave. */
	err = regmap_read(regmap, ZPA2326_DEVICE_ID_REG, &id);
	if (err)
		goto sleep;

	if (id != hwid) {
		dev_err(parent, "found device with unexpected id %02x", id);
		err = -ENODEV;
		goto sleep;
	}

	err = zpa2326_config_oneshot(indio_dev, irq);
	if (err)
		goto sleep;

	/* Setup done : go sleeping. Device will be awaken upon user request. */
	err = zpa2326_sleep(indio_dev);
	if (err)
		goto poweroff;

	dev_set_drvdata(parent, indio_dev);

	zpa2326_init_runtime(parent);

	err = iio_device_register(indio_dev);
	if (err) {
		zpa2326_fini_runtime(parent);
		goto poweroff;
	}

	return 0;

sleep:
	/* Put to sleep just in case power regulators are "dummy" ones. */
	zpa2326_sleep(indio_dev);
poweroff:
	zpa2326_power_off(indio_dev, priv);

	return err;
}
EXPORT_SYMBOL_GPL(zpa2326_probe);

void zpa2326_remove(const struct device *parent)
{
	struct iio_dev *indio_dev = dev_get_drvdata(parent);

	iio_device_unregister(indio_dev);
	zpa2326_fini_runtime(indio_dev->dev.parent);
	zpa2326_sleep(indio_dev);
	zpa2326_power_off(indio_dev, iio_priv(indio_dev));
}
EXPORT_SYMBOL_GPL(zpa2326_remove);

MODULE_AUTHOR("Gregor Boirie <gregor.boirie@parrot.com>");
MODULE_DESCRIPTION("Core driver for Murata ZPA2326 pressure sensor");
MODULE_LICENSE("GPL v2");
