/*
 * TI Bandgap temperature sensor driver
 *
 * Copyright (C) 2011-2012 Texas Instruments Incorporated - http://www.ti.com/
 * Author: J Keerthy <j-keerthy@ti.com>
 * Author: Moiz Sonasath <m-sonasath@ti.com>
 * Couple of fixes, DT and MFD adaptation:
 *   Eduardo Valentin <eduardo.valentin@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/module.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/reboot.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/io.h>

#include "ti-bandgap.h"

/***   Helper functions to access registers and their bitfields   ***/

/**
 * ti_bandgap_readl() - simple read helper function
 * @bgp: pointer to ti_bandgap structure
 * @reg: desired register (offset) to be read
 *
 * Helper function to read bandgap registers. It uses the io remapped area.
 * Return: the register value.
 */
static u32 ti_bandgap_readl(struct ti_bandgap *bgp, u32 reg)
{
	return readl(bgp->base + reg);
}

/**
 * ti_bandgap_writel() - simple write helper function
 * @bgp: pointer to ti_bandgap structure
 * @val: desired register value to be written
 * @reg: desired register (offset) to be written
 *
 * Helper function to write bandgap registers. It uses the io remapped area.
 */
static void ti_bandgap_writel(struct ti_bandgap *bgp, u32 val, u32 reg)
{
	writel(val, bgp->base + reg);
}

/**
 * DOC: macro to update bits.
 *
 * RMW_BITS() - used to read, modify and update bandgap bitfields.
 *            The value passed will be shifted.
 */
#define RMW_BITS(bgp, id, reg, mask, val)			\
do {								\
	struct temp_sensor_registers *t;			\
	u32 r;							\
								\
	t = bgp->conf->sensors[(id)].registers;		\
	r = ti_bandgap_readl(bgp, t->reg);			\
	r &= ~t->mask;						\
	r |= (val) << __ffs(t->mask);				\
	ti_bandgap_writel(bgp, r, t->reg);			\
} while (0)

/***   Basic helper functions   ***/

/**
 * ti_bandgap_power() - controls the power state of a bandgap device
 * @bgp: pointer to ti_bandgap structure
 * @on: desired power state (1 - on, 0 - off)
 *
 * Used to power on/off a bandgap device instance. Only used on those
 * that features tempsoff bit.
 *
 * Return: 0 on success, -ENOTSUPP if tempsoff is not supported.
 */
static int ti_bandgap_power(struct ti_bandgap *bgp, bool on)
{
	int i, ret = 0;

	if (!TI_BANDGAP_HAS(bgp, POWER_SWITCH)) {
		ret = -ENOTSUPP;
		goto exit;
	}

	for (i = 0; i < bgp->conf->sensor_count; i++)
		/* active on 0 */
		RMW_BITS(bgp, i, temp_sensor_ctrl, bgap_tempsoff_mask, !on);

exit:
	return ret;
}

/**
 * ti_bandgap_read_temp() - helper function to read sensor temperature
 * @bgp: pointer to ti_bandgap structure
 * @id: bandgap sensor id
 *
 * Function to concentrate the steps to read sensor temperature register.
 * This function is desired because, depending on bandgap device version,
 * it might be needed to freeze the bandgap state machine, before fetching
 * the register value.
 *
 * Return: temperature in ADC values.
 */
static u32 ti_bandgap_read_temp(struct ti_bandgap *bgp, int id)
{
	struct temp_sensor_registers *tsr;
	u32 temp, reg;

	tsr = bgp->conf->sensors[id].registers;
	reg = tsr->temp_sensor_ctrl;

	if (TI_BANDGAP_HAS(bgp, FREEZE_BIT)) {
		RMW_BITS(bgp, id, bgap_mask_ctrl, mask_freeze_mask, 1);
		/*
		 * In case we cannot read from cur_dtemp / dtemp_0,
		 * then we read from the last valid temp read
		 */
		reg = tsr->ctrl_dtemp_1;
	}

	/* read temperature */
	temp = ti_bandgap_readl(bgp, reg);
	temp &= tsr->bgap_dtemp_mask;

	if (TI_BANDGAP_HAS(bgp, FREEZE_BIT))
		RMW_BITS(bgp, id, bgap_mask_ctrl, mask_freeze_mask, 0);

	return temp;
}

/***   IRQ handlers   ***/

/**
 * ti_bandgap_talert_irq_handler() - handles Temperature alert IRQs
 * @irq: IRQ number
 * @data: private data (struct ti_bandgap *)
 *
 * This is the Talert handler. Use it only if bandgap device features
 * HAS(TALERT). This handler goes over all sensors and checks their
 * conditions and acts accordingly. In case there are events pending,
 * it will reset the event mask to wait for the opposite event (next event).
 * Every time there is a new event, it will be reported to thermal layer.
 *
 * Return: IRQ_HANDLED
 */
static irqreturn_t ti_bandgap_talert_irq_handler(int irq, void *data)
{
	struct ti_bandgap *bgp = data;
	struct temp_sensor_registers *tsr;
	u32 t_hot = 0, t_cold = 0, ctrl;
	int i;

	spin_lock(&bgp->lock);
	for (i = 0; i < bgp->conf->sensor_count; i++) {
		tsr = bgp->conf->sensors[i].registers;
		ctrl = ti_bandgap_readl(bgp, tsr->bgap_status);

		/* Read the status of t_hot */
		t_hot = ctrl & tsr->status_hot_mask;

		/* Read the status of t_cold */
		t_cold = ctrl & tsr->status_cold_mask;

		if (!t_cold && !t_hot)
			continue;

		ctrl = ti_bandgap_readl(bgp, tsr->bgap_mask_ctrl);
		/*
		 * One TALERT interrupt: Two sources
		 * If the interrupt is due to t_hot then mask t_hot and
		 * and unmask t_cold else mask t_cold and unmask t_hot
		 */
		if (t_hot) {
			ctrl &= ~tsr->mask_hot_mask;
			ctrl |= tsr->mask_cold_mask;
		} else if (t_cold) {
			ctrl &= ~tsr->mask_cold_mask;
			ctrl |= tsr->mask_hot_mask;
		}

		ti_bandgap_writel(bgp, ctrl, tsr->bgap_mask_ctrl);

		dev_dbg(bgp->dev,
			"%s: IRQ from %s sensor: hotevent %d coldevent %d\n",
			__func__, bgp->conf->sensors[i].domain,
			t_hot, t_cold);

		/* report temperature to whom may concern */
		if (bgp->conf->report_temperature)
			bgp->conf->report_temperature(bgp, i);
	}
	spin_unlock(&bgp->lock);

	return IRQ_HANDLED;
}

/**
 * ti_bandgap_tshut_irq_handler() - handles Temperature shutdown signal
 * @irq: IRQ number
 * @data: private data (unused)
 *
 * This is the Tshut handler. Use it only if bandgap device features
 * HAS(TSHUT). If any sensor fires the Tshut signal, we simply shutdown
 * the system.
 *
 * Return: IRQ_HANDLED
 */
static irqreturn_t ti_bandgap_tshut_irq_handler(int irq, void *data)
{
	pr_emerg("%s: TSHUT temperature reached. Needs shut down...\n",
		 __func__);

	orderly_poweroff(true);

	return IRQ_HANDLED;
}

/***   Helper functions which manipulate conversion ADC <-> mi Celsius   ***/

/**
 * ti_bandgap_adc_to_mcelsius() - converts an ADC value to mCelsius scale
 * @bgp: struct ti_bandgap pointer
 * @adc_val: value in ADC representation
 * @t: address where to write the resulting temperature in mCelsius
 *
 * Simple conversion from ADC representation to mCelsius. In case the ADC value
 * is out of the ADC conv table range, it returns -ERANGE, 0 on success.
 * The conversion table is indexed by the ADC values.
 *
 * Return: 0 if conversion was successful, else -ERANGE in case the @adc_val
 * argument is out of the ADC conv table range.
 */
static
int ti_bandgap_adc_to_mcelsius(struct ti_bandgap *bgp, int adc_val, int *t)
{
	const struct ti_bandgap_data *conf = bgp->conf;
	int ret = 0;

	/* look up for temperature in the table and return the temperature */
	if (adc_val < conf->adc_start_val || adc_val > conf->adc_end_val) {
		ret = -ERANGE;
		goto exit;
	}

	*t = bgp->conf->conv_table[adc_val - conf->adc_start_val];

exit:
	return ret;
}

/**
 * ti_bandgap_mcelsius_to_adc() - converts a mCelsius value to ADC scale
 * @bgp: struct ti_bandgap pointer
 * @temp: value in mCelsius
 * @adc: address where to write the resulting temperature in ADC representation
 *
 * Simple conversion from mCelsius to ADC values. In case the temp value
 * is out of the ADC conv table range, it returns -ERANGE, 0 on success.
 * The conversion table is indexed by the ADC values.
 *
 * Return: 0 if conversion was successful, else -ERANGE in case the @temp
 * argument is out of the ADC conv table range.
 */
static
int ti_bandgap_mcelsius_to_adc(struct ti_bandgap *bgp, long temp, int *adc)
{
	const struct ti_bandgap_data *conf = bgp->conf;
	const int *conv_table = bgp->conf->conv_table;
	int high, low, mid, ret = 0;

	low = 0;
	high = conf->adc_end_val - conf->adc_start_val;
	mid = (high + low) / 2;

	if (temp < conv_table[low] || temp > conv_table[high]) {
		ret = -ERANGE;
		goto exit;
	}

	while (low < high) {
		if (temp < conv_table[mid])
			high = mid - 1;
		else
			low = mid + 1;
		mid = (low + high) / 2;
	}

	*adc = conf->adc_start_val + low;

exit:
	return ret;
}

/**
 * ti_bandgap_add_hyst() - add hysteresis (in mCelsius) to an ADC value
 * @bgp: struct ti_bandgap pointer
 * @adc_val: temperature value in ADC representation
 * @hyst_val: hysteresis value in mCelsius
 * @sum: address where to write the resulting temperature (in ADC scale)
 *
 * Adds an hysteresis value (in mCelsius) to a ADC temperature value.
 *
 * Return: 0 on success, -ERANGE otherwise.
 */
static
int ti_bandgap_add_hyst(struct ti_bandgap *bgp, int adc_val, int hyst_val,
			u32 *sum)
{
	int temp, ret;

	/*
	 * Need to add in the mcelsius domain, so we have a temperature
	 * the conv_table range
	 */
	ret = ti_bandgap_adc_to_mcelsius(bgp, adc_val, &temp);
	if (ret < 0)
		goto exit;

	temp += hyst_val;

	ret = ti_bandgap_mcelsius_to_adc(bgp, temp, sum);

exit:
	return ret;
}

/***   Helper functions handling device Alert/Shutdown signals   ***/

/**
 * ti_bandgap_unmask_interrupts() - unmasks the events of thot & tcold
 * @bgp: struct ti_bandgap pointer
 * @id: bandgap sensor id
 * @t_hot: hot temperature value to trigger alert signal
 * @t_cold: cold temperature value to trigger alert signal
 *
 * Checks the requested t_hot and t_cold values and configures the IRQ event
 * masks accordingly. Call this function only if bandgap features HAS(TALERT).
 */
static void ti_bandgap_unmask_interrupts(struct ti_bandgap *bgp, int id,
					 u32 t_hot, u32 t_cold)
{
	struct temp_sensor_registers *tsr;
	u32 temp, reg_val;

	/* Read the current on die temperature */
	temp = ti_bandgap_read_temp(bgp, id);

	tsr = bgp->conf->sensors[id].registers;
	reg_val = ti_bandgap_readl(bgp, tsr->bgap_mask_ctrl);

	if (temp < t_hot)
		reg_val |= tsr->mask_hot_mask;
	else
		reg_val &= ~tsr->mask_hot_mask;

	if (t_cold < temp)
		reg_val |= tsr->mask_cold_mask;
	else
		reg_val &= ~tsr->mask_cold_mask;
	ti_bandgap_writel(bgp, reg_val, tsr->bgap_mask_ctrl);
}

/**
 * ti_bandgap_update_alert_threshold() - sequence to update thresholds
 * @bgp: struct ti_bandgap pointer
 * @id: bandgap sensor id
 * @val: value (ADC) of a new threshold
 * @hot: desired threshold to be updated. true if threshold hot, false if
 *       threshold cold
 *
 * It will program the required thresholds (hot and cold) for TALERT signal.
 * This function can be used to update t_hot or t_cold, depending on @hot value.
 * It checks the resulting t_hot and t_cold values, based on the new passed @val
 * and configures the thresholds so that t_hot is always greater than t_cold.
 * Call this function only if bandgap features HAS(TALERT).
 *
 * Return: 0 if no error, else corresponding error
 */
static int ti_bandgap_update_alert_threshold(struct ti_bandgap *bgp, int id,
					     int val, bool hot)
{
	struct temp_sensor_data *ts_data = bgp->conf->sensors[id].ts_data;
	struct temp_sensor_registers *tsr;
	u32 thresh_val, reg_val, t_hot, t_cold;
	int err = 0;

	tsr = bgp->conf->sensors[id].registers;

	/* obtain the current value */
	thresh_val = ti_bandgap_readl(bgp, tsr->bgap_threshold);
	t_cold = (thresh_val & tsr->threshold_tcold_mask) >>
		__ffs(tsr->threshold_tcold_mask);
	t_hot = (thresh_val & tsr->threshold_thot_mask) >>
		__ffs(tsr->threshold_thot_mask);
	if (hot)
		t_hot = val;
	else
		t_cold = val;

	if (t_cold > t_hot) {
		if (hot)
			err = ti_bandgap_add_hyst(bgp, t_hot,
						  -ts_data->hyst_val,
						  &t_cold);
		else
			err = ti_bandgap_add_hyst(bgp, t_cold,
						  ts_data->hyst_val,
						  &t_hot);
	}

	/* write the new threshold values */
	reg_val = thresh_val &
		  ~(tsr->threshold_thot_mask | tsr->threshold_tcold_mask);
	reg_val |= (t_hot << __ffs(tsr->threshold_thot_mask)) |
		   (t_cold << __ffs(tsr->threshold_tcold_mask));
	ti_bandgap_writel(bgp, reg_val, tsr->bgap_threshold);

	if (err) {
		dev_err(bgp->dev, "failed to reprogram thot threshold\n");
		err = -EIO;
		goto exit;
	}

	ti_bandgap_unmask_interrupts(bgp, id, t_hot, t_cold);
exit:
	return err;
}

/**
 * ti_bandgap_validate() - helper to check the sanity of a struct ti_bandgap
 * @bgp: struct ti_bandgap pointer
 * @id: bandgap sensor id
 *
 * Checks if the bandgap pointer is valid and if the sensor id is also
 * applicable.
 *
 * Return: 0 if no errors, -EINVAL for invalid @bgp pointer or -ERANGE if
 * @id cannot index @bgp sensors.
 */
static inline int ti_bandgap_validate(struct ti_bandgap *bgp, int id)
{
	int ret = 0;

	if (!bgp || IS_ERR(bgp)) {
		pr_err("%s: invalid bandgap pointer\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

	if ((id < 0) || (id >= bgp->conf->sensor_count)) {
		dev_err(bgp->dev, "%s: sensor id out of range (%d)\n",
			__func__, id);
		ret = -ERANGE;
	}

exit:
	return ret;
}

/**
 * _ti_bandgap_write_threshold() - helper to update TALERT t_cold or t_hot
 * @bgp: struct ti_bandgap pointer
 * @id: bandgap sensor id
 * @val: value (mCelsius) of a new threshold
 * @hot: desired threshold to be updated. true if threshold hot, false if
 *       threshold cold
 *
 * It will update the required thresholds (hot and cold) for TALERT signal.
 * This function can be used to update t_hot or t_cold, depending on @hot value.
 * Validates the mCelsius range and update the requested threshold.
 * Call this function only if bandgap features HAS(TALERT).
 *
 * Return: 0 if no error, else corresponding error value.
 */
static int _ti_bandgap_write_threshold(struct ti_bandgap *bgp, int id, int val,
				       bool hot)
{
	struct temp_sensor_data *ts_data;
	struct temp_sensor_registers *tsr;
	u32 adc_val;
	int ret;

	ret = ti_bandgap_validate(bgp, id);
	if (ret)
		goto exit;

	if (!TI_BANDGAP_HAS(bgp, TALERT)) {
		ret = -ENOTSUPP;
		goto exit;
	}

	ts_data = bgp->conf->sensors[id].ts_data;
	tsr = bgp->conf->sensors[id].registers;
	if (hot) {
		if (val < ts_data->min_temp + ts_data->hyst_val)
			ret = -EINVAL;
	} else {
		if (val > ts_data->max_temp + ts_data->hyst_val)
			ret = -EINVAL;
	}

	if (ret)
		goto exit;

	ret = ti_bandgap_mcelsius_to_adc(bgp, val, &adc_val);
	if (ret < 0)
		goto exit;

	spin_lock(&bgp->lock);
	ret = ti_bandgap_update_alert_threshold(bgp, id, adc_val, hot);
	spin_unlock(&bgp->lock);

exit:
	return ret;
}

/**
 * _ti_bandgap_read_threshold() - helper to read TALERT t_cold or t_hot
 * @bgp: struct ti_bandgap pointer
 * @id: bandgap sensor id
 * @val: value (mCelsius) of a threshold
 * @hot: desired threshold to be read. true if threshold hot, false if
 *       threshold cold
 *
 * It will fetch the required thresholds (hot and cold) for TALERT signal.
 * This function can be used to read t_hot or t_cold, depending on @hot value.
 * Call this function only if bandgap features HAS(TALERT).
 *
 * Return: 0 if no error, -ENOTSUPP if it has no TALERT support, or the
 * corresponding error value if some operation fails.
 */
static int _ti_bandgap_read_threshold(struct ti_bandgap *bgp, int id,
				      int *val, bool hot)
{
	struct temp_sensor_registers *tsr;
	u32 temp, mask;
	int ret = 0;

	ret = ti_bandgap_validate(bgp, id);
	if (ret)
		goto exit;

	if (!TI_BANDGAP_HAS(bgp, TALERT)) {
		ret = -ENOTSUPP;
		goto exit;
	}

	tsr = bgp->conf->sensors[id].registers;
	if (hot)
		mask = tsr->threshold_thot_mask;
	else
		mask = tsr->threshold_tcold_mask;

	temp = ti_bandgap_readl(bgp, tsr->bgap_threshold);
	temp = (temp & mask) >> __ffs(mask);
	ret |= ti_bandgap_adc_to_mcelsius(bgp, temp, &temp);
	if (ret) {
		dev_err(bgp->dev, "failed to read thot\n");
		ret = -EIO;
		goto exit;
	}

	*val = temp;

exit:
	return ret;
}

/***   Exposed APIs   ***/

/**
 * ti_bandgap_read_thot() - reads sensor current thot
 * @bgp: pointer to bandgap instance
 * @id: sensor id
 * @thot: resulting current thot value
 *
 * Return: 0 on success or the proper error code
 */
int ti_bandgap_read_thot(struct ti_bandgap *bgp, int id, int *thot)
{
	return _ti_bandgap_read_threshold(bgp, id, thot, true);
}

/**
 * ti_bandgap_write_thot() - sets sensor current thot
 * @bgp: pointer to bandgap instance
 * @id: sensor id
 * @val: desired thot value
 *
 * Return: 0 on success or the proper error code
 */
int ti_bandgap_write_thot(struct ti_bandgap *bgp, int id, int val)
{
	return _ti_bandgap_write_threshold(bgp, id, val, true);
}

/**
 * ti_bandgap_read_tcold() - reads sensor current tcold
 * @bgp: pointer to bandgap instance
 * @id: sensor id
 * @tcold: resulting current tcold value
 *
 * Return: 0 on success or the proper error code
 */
int ti_bandgap_read_tcold(struct ti_bandgap *bgp, int id, int *tcold)
{
	return _ti_bandgap_read_threshold(bgp, id, tcold, false);
}

/**
 * ti_bandgap_write_tcold() - sets the sensor tcold
 * @bgp: pointer to bandgap instance
 * @id: sensor id
 * @val: desired tcold value
 *
 * Return: 0 on success or the proper error code
 */
int ti_bandgap_write_tcold(struct ti_bandgap *bgp, int id, int val)
{
	return _ti_bandgap_write_threshold(bgp, id, val, false);
}

/**
 * ti_bandgap_read_counter() - read the sensor counter
 * @bgp: pointer to bandgap instance
 * @id: sensor id
 * @interval: resulting update interval in miliseconds
 */
static void ti_bandgap_read_counter(struct ti_bandgap *bgp, int id,
				    int *interval)
{
	struct temp_sensor_registers *tsr;
	int time;

	tsr = bgp->conf->sensors[id].registers;
	time = ti_bandgap_readl(bgp, tsr->bgap_counter);
	time = (time & tsr->counter_mask) >>
					__ffs(tsr->counter_mask);
	time = time * 1000 / bgp->clk_rate;
	*interval = time;
}

/**
 * ti_bandgap_read_counter_delay() - read the sensor counter delay
 * @bgp: pointer to bandgap instance
 * @id: sensor id
 * @interval: resulting update interval in miliseconds
 */
static void ti_bandgap_read_counter_delay(struct ti_bandgap *bgp, int id,
					  int *interval)
{
	struct temp_sensor_registers *tsr;
	int reg_val;

	tsr = bgp->conf->sensors[id].registers;

	reg_val = ti_bandgap_readl(bgp, tsr->bgap_mask_ctrl);
	reg_val = (reg_val & tsr->mask_counter_delay_mask) >>
				__ffs(tsr->mask_counter_delay_mask);
	switch (reg_val) {
	case 0:
		*interval = 0;
		break;
	case 1:
		*interval = 1;
		break;
	case 2:
		*interval = 10;
		break;
	case 3:
		*interval = 100;
		break;
	case 4:
		*interval = 250;
		break;
	case 5:
		*interval = 500;
		break;
	default:
		dev_warn(bgp->dev, "Wrong counter delay value read from register %X",
			 reg_val);
	}
}

/**
 * ti_bandgap_read_update_interval() - read the sensor update interval
 * @bgp: pointer to bandgap instance
 * @id: sensor id
 * @interval: resulting update interval in miliseconds
 *
 * Return: 0 on success or the proper error code
 */
int ti_bandgap_read_update_interval(struct ti_bandgap *bgp, int id,
				    int *interval)
{
	int ret = 0;

	ret = ti_bandgap_validate(bgp, id);
	if (ret)
		goto exit;

	if (!TI_BANDGAP_HAS(bgp, COUNTER) &&
	    !TI_BANDGAP_HAS(bgp, COUNTER_DELAY)) {
		ret = -ENOTSUPP;
		goto exit;
	}

	if (TI_BANDGAP_HAS(bgp, COUNTER)) {
		ti_bandgap_read_counter(bgp, id, interval);
		goto exit;
	}

	ti_bandgap_read_counter_delay(bgp, id, interval);
exit:
	return ret;
}

/**
 * ti_bandgap_write_counter_delay() - set the counter_delay
 * @bgp: pointer to bandgap instance
 * @id: sensor id
 * @interval: desired update interval in miliseconds
 *
 * Return: 0 on success or the proper error code
 */
static int ti_bandgap_write_counter_delay(struct ti_bandgap *bgp, int id,
					  u32 interval)
{
	int rval;

	switch (interval) {
	case 0: /* Immediate conversion */
		rval = 0x0;
		break;
	case 1: /* Conversion after ever 1ms */
		rval = 0x1;
		break;
	case 10: /* Conversion after ever 10ms */
		rval = 0x2;
		break;
	case 100: /* Conversion after ever 100ms */
		rval = 0x3;
		break;
	case 250: /* Conversion after ever 250ms */
		rval = 0x4;
		break;
	case 500: /* Conversion after ever 500ms */
		rval = 0x5;
		break;
	default:
		dev_warn(bgp->dev, "Delay %d ms is not supported\n", interval);
		return -EINVAL;
	}

	spin_lock(&bgp->lock);
	RMW_BITS(bgp, id, bgap_mask_ctrl, mask_counter_delay_mask, rval);
	spin_unlock(&bgp->lock);

	return 0;
}

/**
 * ti_bandgap_write_counter() - set the bandgap sensor counter
 * @bgp: pointer to bandgap instance
 * @id: sensor id
 * @interval: desired update interval in miliseconds
 */
static void ti_bandgap_write_counter(struct ti_bandgap *bgp, int id,
				     u32 interval)
{
	interval = interval * bgp->clk_rate / 1000;
	spin_lock(&bgp->lock);
	RMW_BITS(bgp, id, bgap_counter, counter_mask, interval);
	spin_unlock(&bgp->lock);
}

/**
 * ti_bandgap_write_update_interval() - set the update interval
 * @bgp: pointer to bandgap instance
 * @id: sensor id
 * @interval: desired update interval in miliseconds
 *
 * Return: 0 on success or the proper error code
 */
int ti_bandgap_write_update_interval(struct ti_bandgap *bgp,
				     int id, u32 interval)
{
	int ret = ti_bandgap_validate(bgp, id);
	if (ret)
		goto exit;

	if (!TI_BANDGAP_HAS(bgp, COUNTER) &&
	    !TI_BANDGAP_HAS(bgp, COUNTER_DELAY)) {
		ret = -ENOTSUPP;
		goto exit;
	}

	if (TI_BANDGAP_HAS(bgp, COUNTER)) {
		ti_bandgap_write_counter(bgp, id, interval);
		goto exit;
	}

	ret = ti_bandgap_write_counter_delay(bgp, id, interval);
exit:
	return ret;
}

/**
 * ti_bandgap_read_temperature() - report current temperature
 * @bgp: pointer to bandgap instance
 * @id: sensor id
 * @temperature: resulting temperature
 *
 * Return: 0 on success or the proper error code
 */
int ti_bandgap_read_temperature(struct ti_bandgap *bgp, int id,
				int *temperature)
{
	u32 temp;
	int ret;

	ret = ti_bandgap_validate(bgp, id);
	if (ret)
		return ret;

	spin_lock(&bgp->lock);
	temp = ti_bandgap_read_temp(bgp, id);
	spin_unlock(&bgp->lock);

	ret |= ti_bandgap_adc_to_mcelsius(bgp, temp, &temp);
	if (ret)
		return -EIO;

	*temperature = temp;

	return 0;
}

/**
 * ti_bandgap_set_sensor_data() - helper function to store thermal
 * framework related data.
 * @bgp: pointer to bandgap instance
 * @id: sensor id
 * @data: thermal framework related data to be stored
 *
 * Return: 0 on success or the proper error code
 */
int ti_bandgap_set_sensor_data(struct ti_bandgap *bgp, int id, void *data)
{
	int ret = ti_bandgap_validate(bgp, id);
	if (ret)
		return ret;

	bgp->regval[id].data = data;

	return 0;
}

/**
 * ti_bandgap_get_sensor_data() - helper function to get thermal
 * framework related data.
 * @bgp: pointer to bandgap instance
 * @id: sensor id
 *
 * Return: data stored by set function with sensor id on success or NULL
 */
void *ti_bandgap_get_sensor_data(struct ti_bandgap *bgp, int id)
{
	int ret = ti_bandgap_validate(bgp, id);
	if (ret)
		return ERR_PTR(ret);

	return bgp->regval[id].data;
}

/***   Helper functions used during device initialization   ***/

/**
 * ti_bandgap_force_single_read() - executes 1 single ADC conversion
 * @bgp: pointer to struct ti_bandgap
 * @id: sensor id which it is desired to read 1 temperature
 *
 * Used to initialize the conversion state machine and set it to a valid
 * state. Called during device initialization and context restore events.
 *
 * Return: 0
 */
static int
ti_bandgap_force_single_read(struct ti_bandgap *bgp, int id)
{
	u32 temp = 0, counter = 1000;

	/* Select single conversion mode */
	if (TI_BANDGAP_HAS(bgp, MODE_CONFIG))
		RMW_BITS(bgp, id, bgap_mode_ctrl, mode_ctrl_mask, 0);

	/* Start of Conversion = 1 */
	RMW_BITS(bgp, id, temp_sensor_ctrl, bgap_soc_mask, 1);
	/* Wait until DTEMP is updated */
	temp = ti_bandgap_read_temp(bgp, id);

	while ((temp == 0) && --counter)
		temp = ti_bandgap_read_temp(bgp, id);
	/* REVISIT: Check correct condition for end of conversion */

	/* Start of Conversion = 0 */
	RMW_BITS(bgp, id, temp_sensor_ctrl, bgap_soc_mask, 0);

	return 0;
}

/**
 * ti_bandgap_set_continous_mode() - One time enabling of continuous mode
 * @bgp: pointer to struct ti_bandgap
 *
 * Call this function only if HAS(MODE_CONFIG) is set. As this driver may
 * be used for junction temperature monitoring, it is desirable that the
 * sensors are operational all the time, so that alerts are generated
 * properly.
 *
 * Return: 0
 */
static int ti_bandgap_set_continuous_mode(struct ti_bandgap *bgp)
{
	int i;

	for (i = 0; i < bgp->conf->sensor_count; i++) {
		/* Perform a single read just before enabling continuous */
		ti_bandgap_force_single_read(bgp, i);
		RMW_BITS(bgp, i, bgap_mode_ctrl, mode_ctrl_mask, 1);
	}

	return 0;
}

/**
 * ti_bandgap_get_trend() - To fetch the temperature trend of a sensor
 * @bgp: pointer to struct ti_bandgap
 * @id: id of the individual sensor
 * @trend: Pointer to trend.
 *
 * This function needs to be called to fetch the temperature trend of a
 * Particular sensor. The function computes the difference in temperature
 * w.r.t time. For the bandgaps with built in history buffer the temperatures
 * are read from the buffer and for those without the Buffer -ENOTSUPP is
 * returned.
 *
 * Return: 0 if no error, else return corresponding error. If no
 *		error then the trend value is passed on to trend parameter
 */
int ti_bandgap_get_trend(struct ti_bandgap *bgp, int id, int *trend)
{
	struct temp_sensor_registers *tsr;
	u32 temp1, temp2, reg1, reg2;
	int t1, t2, interval, ret = 0;

	ret = ti_bandgap_validate(bgp, id);
	if (ret)
		goto exit;

	if (!TI_BANDGAP_HAS(bgp, HISTORY_BUFFER) ||
	    !TI_BANDGAP_HAS(bgp, FREEZE_BIT)) {
		ret = -ENOTSUPP;
		goto exit;
	}

	spin_lock(&bgp->lock);

	tsr = bgp->conf->sensors[id].registers;

	/* Freeze and read the last 2 valid readings */
	RMW_BITS(bgp, id, bgap_mask_ctrl, mask_freeze_mask, 1);
	reg1 = tsr->ctrl_dtemp_1;
	reg2 = tsr->ctrl_dtemp_2;

	/* read temperature from history buffer */
	temp1 = ti_bandgap_readl(bgp, reg1);
	temp1 &= tsr->bgap_dtemp_mask;

	temp2 = ti_bandgap_readl(bgp, reg2);
	temp2 &= tsr->bgap_dtemp_mask;

	/* Convert from adc values to mCelsius temperature */
	ret = ti_bandgap_adc_to_mcelsius(bgp, temp1, &t1);
	if (ret)
		goto unfreeze;

	ret = ti_bandgap_adc_to_mcelsius(bgp, temp2, &t2);
	if (ret)
		goto unfreeze;

	/* Fetch the update interval */
	ret = ti_bandgap_read_update_interval(bgp, id, &interval);
	if (ret)
		goto unfreeze;

	/* Set the interval to 1 ms if bandgap counter delay is not set */
	if (interval == 0)
		interval = 1;

	*trend = (t1 - t2) / interval;

	dev_dbg(bgp->dev, "The temperatures are t1 = %d and t2 = %d and trend =%d\n",
		t1, t2, *trend);

unfreeze:
	RMW_BITS(bgp, id, bgap_mask_ctrl, mask_freeze_mask, 0);
	spin_unlock(&bgp->lock);
exit:
	return ret;
}

/**
 * ti_bandgap_tshut_init() - setup and initialize tshut handling
 * @bgp: pointer to struct ti_bandgap
 * @pdev: pointer to device struct platform_device
 *
 * Call this function only in case the bandgap features HAS(TSHUT).
 * In this case, the driver needs to handle the TSHUT signal as an IRQ.
 * The IRQ is wired as a GPIO, and for this purpose, it is required
 * to specify which GPIO line is used. TSHUT IRQ is fired anytime
 * one of the bandgap sensors violates the TSHUT high/hot threshold.
 * And in that case, the system must go off.
 *
 * Return: 0 if no error, else error status
 */
static int ti_bandgap_tshut_init(struct ti_bandgap *bgp,
				 struct platform_device *pdev)
{
	int gpio_nr = bgp->tshut_gpio;
	int status;

	/* Request for gpio_86 line */
	status = gpio_request(gpio_nr, "tshut");
	if (status < 0) {
		dev_err(bgp->dev, "Could not request for TSHUT GPIO:%i\n", 86);
		return status;
	}
	status = gpio_direction_input(gpio_nr);
	if (status) {
		dev_err(bgp->dev, "Cannot set input TSHUT GPIO %d\n", gpio_nr);
		return status;
	}

	status = request_irq(gpio_to_irq(gpio_nr), ti_bandgap_tshut_irq_handler,
			     IRQF_TRIGGER_RISING, "tshut", NULL);
	if (status) {
		gpio_free(gpio_nr);
		dev_err(bgp->dev, "request irq failed for TSHUT");
	}

	return 0;
}

/**
 * ti_bandgap_alert_init() - setup and initialize talert handling
 * @bgp: pointer to struct ti_bandgap
 * @pdev: pointer to device struct platform_device
 *
 * Call this function only in case the bandgap features HAS(TALERT).
 * In this case, the driver needs to handle the TALERT signals as an IRQs.
 * TALERT is a normal IRQ and it is fired any time thresholds (hot or cold)
 * are violated. In these situation, the driver must reprogram the thresholds,
 * accordingly to specified policy.
 *
 * Return: 0 if no error, else return corresponding error.
 */
static int ti_bandgap_talert_init(struct ti_bandgap *bgp,
				  struct platform_device *pdev)
{
	int ret;

	bgp->irq = platform_get_irq(pdev, 0);
	if (bgp->irq < 0) {
		dev_err(&pdev->dev, "get_irq failed\n");
		return bgp->irq;
	}
	ret = request_threaded_irq(bgp->irq, NULL,
				   ti_bandgap_talert_irq_handler,
				   IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
				   "talert", bgp);
	if (ret) {
		dev_err(&pdev->dev, "Request threaded irq failed.\n");
		return ret;
	}

	return 0;
}

static const struct of_device_id of_ti_bandgap_match[];
/**
 * ti_bandgap_build() - parse DT and setup a struct ti_bandgap
 * @pdev: pointer to device struct platform_device
 *
 * Used to read the device tree properties accordingly to the bandgap
 * matching version. Based on bandgap version and its capabilities it
 * will build a struct ti_bandgap out of the required DT entries.
 *
 * Return: valid bandgap structure if successful, else returns ERR_PTR
 * return value must be verified with IS_ERR.
 */
static struct ti_bandgap *ti_bandgap_build(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	const struct of_device_id *of_id;
	struct ti_bandgap *bgp;
	struct resource *res;
	int i;

	/* just for the sake */
	if (!node) {
		dev_err(&pdev->dev, "no platform information available\n");
		return ERR_PTR(-EINVAL);
	}

	bgp = devm_kzalloc(&pdev->dev, sizeof(*bgp), GFP_KERNEL);
	if (!bgp) {
		dev_err(&pdev->dev, "Unable to allocate mem for driver ref\n");
		return ERR_PTR(-ENOMEM);
	}

	of_id = of_match_device(of_ti_bandgap_match, &pdev->dev);
	if (of_id)
		bgp->conf = of_id->data;

	/* register shadow for context save and restore */
	bgp->regval = devm_kzalloc(&pdev->dev, sizeof(*bgp->regval) *
				   bgp->conf->sensor_count, GFP_KERNEL);
	if (!bgp) {
		dev_err(&pdev->dev, "Unable to allocate mem for driver ref\n");
		return ERR_PTR(-ENOMEM);
	}

	i = 0;
	do {
		void __iomem *chunk;

		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res)
			break;
		chunk = devm_ioremap_resource(&pdev->dev, res);
		if (i == 0)
			bgp->base = chunk;
		if (IS_ERR(chunk))
			return ERR_CAST(chunk);

		i++;
	} while (res);

	if (TI_BANDGAP_HAS(bgp, TSHUT)) {
		bgp->tshut_gpio = of_get_gpio(node, 0);
		if (!gpio_is_valid(bgp->tshut_gpio)) {
			dev_err(&pdev->dev, "invalid gpio for tshut (%d)\n",
				bgp->tshut_gpio);
			return ERR_PTR(-EINVAL);
		}
	}

	return bgp;
}

/***   Device driver call backs   ***/

static
int ti_bandgap_probe(struct platform_device *pdev)
{
	struct ti_bandgap *bgp;
	int clk_rate, ret = 0, i;

	bgp = ti_bandgap_build(pdev);
	if (IS_ERR(bgp)) {
		dev_err(&pdev->dev, "failed to fetch platform data\n");
		return PTR_ERR(bgp);
	}
	bgp->dev = &pdev->dev;

	if (TI_BANDGAP_HAS(bgp, TSHUT)) {
		ret = ti_bandgap_tshut_init(bgp, pdev);
		if (ret) {
			dev_err(&pdev->dev,
				"failed to initialize system tshut IRQ\n");
			return ret;
		}
	}

	bgp->fclock = clk_get(NULL, bgp->conf->fclock_name);
	ret = IS_ERR(bgp->fclock);
	if (ret) {
		dev_err(&pdev->dev, "failed to request fclock reference\n");
		ret = PTR_ERR(bgp->fclock);
		goto free_irqs;
	}

	bgp->div_clk = clk_get(NULL,  bgp->conf->div_ck_name);
	ret = IS_ERR(bgp->div_clk);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to request div_ts_ck clock ref\n");
		ret = PTR_ERR(bgp->div_clk);
		goto free_irqs;
	}

	for (i = 0; i < bgp->conf->sensor_count; i++) {
		struct temp_sensor_registers *tsr;
		u32 val;

		tsr = bgp->conf->sensors[i].registers;
		/*
		 * check if the efuse has a non-zero value if not
		 * it is an untrimmed sample and the temperatures
		 * may not be accurate
		 */
		val = ti_bandgap_readl(bgp, tsr->bgap_efuse);
		if (ret || !val)
			dev_info(&pdev->dev,
				 "Non-trimmed BGAP, Temp not accurate\n");
	}

	clk_rate = clk_round_rate(bgp->div_clk,
				  bgp->conf->sensors[0].ts_data->max_freq);
	if (clk_rate < bgp->conf->sensors[0].ts_data->min_freq ||
	    clk_rate == 0xffffffff) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "wrong clock rate (%d)\n", clk_rate);
		goto put_clks;
	}

	ret = clk_set_rate(bgp->div_clk, clk_rate);
	if (ret)
		dev_err(&pdev->dev, "Cannot re-set clock rate. Continuing\n");

	bgp->clk_rate = clk_rate;
	if (TI_BANDGAP_HAS(bgp, CLK_CTRL))
		clk_prepare_enable(bgp->fclock);


	spin_lock_init(&bgp->lock);
	bgp->dev = &pdev->dev;
	platform_set_drvdata(pdev, bgp);

	ti_bandgap_power(bgp, true);

	/* Set default counter to 1 for now */
	if (TI_BANDGAP_HAS(bgp, COUNTER))
		for (i = 0; i < bgp->conf->sensor_count; i++)
			RMW_BITS(bgp, i, bgap_counter, counter_mask, 1);

	/* Set default thresholds for alert and shutdown */
	for (i = 0; i < bgp->conf->sensor_count; i++) {
		struct temp_sensor_data *ts_data;

		ts_data = bgp->conf->sensors[i].ts_data;

		if (TI_BANDGAP_HAS(bgp, TALERT)) {
			/* Set initial Talert thresholds */
			RMW_BITS(bgp, i, bgap_threshold,
				 threshold_tcold_mask, ts_data->t_cold);
			RMW_BITS(bgp, i, bgap_threshold,
				 threshold_thot_mask, ts_data->t_hot);
			/* Enable the alert events */
			RMW_BITS(bgp, i, bgap_mask_ctrl, mask_hot_mask, 1);
			RMW_BITS(bgp, i, bgap_mask_ctrl, mask_cold_mask, 1);
		}

		if (TI_BANDGAP_HAS(bgp, TSHUT_CONFIG)) {
			/* Set initial Tshut thresholds */
			RMW_BITS(bgp, i, tshut_threshold,
				 tshut_hot_mask, ts_data->tshut_hot);
			RMW_BITS(bgp, i, tshut_threshold,
				 tshut_cold_mask, ts_data->tshut_cold);
		}
	}

	if (TI_BANDGAP_HAS(bgp, MODE_CONFIG))
		ti_bandgap_set_continuous_mode(bgp);

	/* Set .250 seconds time as default counter */
	if (TI_BANDGAP_HAS(bgp, COUNTER))
		for (i = 0; i < bgp->conf->sensor_count; i++)
			RMW_BITS(bgp, i, bgap_counter, counter_mask,
				 bgp->clk_rate / 4);

	/* Every thing is good? Then expose the sensors */
	for (i = 0; i < bgp->conf->sensor_count; i++) {
		char *domain;

		if (bgp->conf->sensors[i].register_cooling) {
			ret = bgp->conf->sensors[i].register_cooling(bgp, i);
			if (ret)
				goto remove_sensors;
		}

		if (bgp->conf->expose_sensor) {
			domain = bgp->conf->sensors[i].domain;
			ret = bgp->conf->expose_sensor(bgp, i, domain);
			if (ret)
				goto remove_last_cooling;
		}
	}

	/*
	 * Enable the Interrupts once everything is set. Otherwise irq handler
	 * might be called as soon as it is enabled where as rest of framework
	 * is still getting initialised.
	 */
	if (TI_BANDGAP_HAS(bgp, TALERT)) {
		ret = ti_bandgap_talert_init(bgp, pdev);
		if (ret) {
			dev_err(&pdev->dev, "failed to initialize Talert IRQ\n");
			i = bgp->conf->sensor_count;
			goto disable_clk;
		}
	}

	return 0;

remove_last_cooling:
	if (bgp->conf->sensors[i].unregister_cooling)
		bgp->conf->sensors[i].unregister_cooling(bgp, i);
remove_sensors:
	for (i--; i >= 0; i--) {
		if (bgp->conf->sensors[i].unregister_cooling)
			bgp->conf->sensors[i].unregister_cooling(bgp, i);
		if (bgp->conf->remove_sensor)
			bgp->conf->remove_sensor(bgp, i);
	}
	ti_bandgap_power(bgp, false);
disable_clk:
	if (TI_BANDGAP_HAS(bgp, CLK_CTRL))
		clk_disable_unprepare(bgp->fclock);
put_clks:
	clk_put(bgp->fclock);
	clk_put(bgp->div_clk);
free_irqs:
	if (TI_BANDGAP_HAS(bgp, TSHUT)) {
		free_irq(gpio_to_irq(bgp->tshut_gpio), NULL);
		gpio_free(bgp->tshut_gpio);
	}

	return ret;
}

static
int ti_bandgap_remove(struct platform_device *pdev)
{
	struct ti_bandgap *bgp = platform_get_drvdata(pdev);
	int i;

	/* First thing is to remove sensor interfaces */
	for (i = 0; i < bgp->conf->sensor_count; i++) {
		if (bgp->conf->sensors[i].unregister_cooling)
			bgp->conf->sensors[i].unregister_cooling(bgp, i);

		if (bgp->conf->remove_sensor)
			bgp->conf->remove_sensor(bgp, i);
	}

	ti_bandgap_power(bgp, false);

	if (TI_BANDGAP_HAS(bgp, CLK_CTRL))
		clk_disable_unprepare(bgp->fclock);
	clk_put(bgp->fclock);
	clk_put(bgp->div_clk);

	if (TI_BANDGAP_HAS(bgp, TALERT))
		free_irq(bgp->irq, bgp);

	if (TI_BANDGAP_HAS(bgp, TSHUT)) {
		free_irq(gpio_to_irq(bgp->tshut_gpio), NULL);
		gpio_free(bgp->tshut_gpio);
	}

	return 0;
}

#ifdef CONFIG_PM
static int ti_bandgap_save_ctxt(struct ti_bandgap *bgp)
{
	int i;

	for (i = 0; i < bgp->conf->sensor_count; i++) {
		struct temp_sensor_registers *tsr;
		struct temp_sensor_regval *rval;

		rval = &bgp->regval[i];
		tsr = bgp->conf->sensors[i].registers;

		if (TI_BANDGAP_HAS(bgp, MODE_CONFIG))
			rval->bg_mode_ctrl = ti_bandgap_readl(bgp,
							tsr->bgap_mode_ctrl);
		if (TI_BANDGAP_HAS(bgp, COUNTER))
			rval->bg_counter = ti_bandgap_readl(bgp,
							tsr->bgap_counter);
		if (TI_BANDGAP_HAS(bgp, TALERT)) {
			rval->bg_threshold = ti_bandgap_readl(bgp,
							tsr->bgap_threshold);
			rval->bg_ctrl = ti_bandgap_readl(bgp,
						   tsr->bgap_mask_ctrl);
		}

		if (TI_BANDGAP_HAS(bgp, TSHUT_CONFIG))
			rval->tshut_threshold = ti_bandgap_readl(bgp,
						   tsr->tshut_threshold);
	}

	return 0;
}

static int ti_bandgap_restore_ctxt(struct ti_bandgap *bgp)
{
	int i;

	for (i = 0; i < bgp->conf->sensor_count; i++) {
		struct temp_sensor_registers *tsr;
		struct temp_sensor_regval *rval;
		u32 val = 0;

		rval = &bgp->regval[i];
		tsr = bgp->conf->sensors[i].registers;

		if (TI_BANDGAP_HAS(bgp, COUNTER))
			val = ti_bandgap_readl(bgp, tsr->bgap_counter);

		if (TI_BANDGAP_HAS(bgp, TSHUT_CONFIG))
			ti_bandgap_writel(bgp, rval->tshut_threshold,
					  tsr->tshut_threshold);
		/* Force immediate temperature measurement and update
		 * of the DTEMP field
		 */
		ti_bandgap_force_single_read(bgp, i);

		if (TI_BANDGAP_HAS(bgp, COUNTER))
			ti_bandgap_writel(bgp, rval->bg_counter,
					  tsr->bgap_counter);
		if (TI_BANDGAP_HAS(bgp, MODE_CONFIG))
			ti_bandgap_writel(bgp, rval->bg_mode_ctrl,
					  tsr->bgap_mode_ctrl);
		if (TI_BANDGAP_HAS(bgp, TALERT)) {
			ti_bandgap_writel(bgp, rval->bg_threshold,
					  tsr->bgap_threshold);
			ti_bandgap_writel(bgp, rval->bg_ctrl,
					  tsr->bgap_mask_ctrl);
		}
	}

	return 0;
}

static int ti_bandgap_suspend(struct device *dev)
{
	struct ti_bandgap *bgp = dev_get_drvdata(dev);
	int err;

	err = ti_bandgap_save_ctxt(bgp);
	ti_bandgap_power(bgp, false);

	if (TI_BANDGAP_HAS(bgp, CLK_CTRL))
		clk_disable_unprepare(bgp->fclock);

	return err;
}

static int ti_bandgap_resume(struct device *dev)
{
	struct ti_bandgap *bgp = dev_get_drvdata(dev);

	if (TI_BANDGAP_HAS(bgp, CLK_CTRL))
		clk_prepare_enable(bgp->fclock);

	ti_bandgap_power(bgp, true);

	return ti_bandgap_restore_ctxt(bgp);
}
static SIMPLE_DEV_PM_OPS(ti_bandgap_dev_pm_ops, ti_bandgap_suspend,
			 ti_bandgap_resume);

#define DEV_PM_OPS	(&ti_bandgap_dev_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif

static const struct of_device_id of_ti_bandgap_match[] = {
#ifdef CONFIG_OMAP4_THERMAL
	{
		.compatible = "ti,omap4430-bandgap",
		.data = (void *)&omap4430_data,
	},
	{
		.compatible = "ti,omap4460-bandgap",
		.data = (void *)&omap4460_data,
	},
	{
		.compatible = "ti,omap4470-bandgap",
		.data = (void *)&omap4470_data,
	},
#endif
#ifdef CONFIG_OMAP5_THERMAL
	{
		.compatible = "ti,omap5430-bandgap",
		.data = (void *)&omap5430_data,
	},
#endif
#ifdef CONFIG_DRA752_THERMAL
	{
		.compatible = "ti,dra752-bandgap",
		.data = (void *)&dra752_data,
	},
#endif
	/* Sentinel */
	{ },
};
MODULE_DEVICE_TABLE(of, of_ti_bandgap_match);

static struct platform_driver ti_bandgap_sensor_driver = {
	.probe = ti_bandgap_probe,
	.remove = ti_bandgap_remove,
	.driver = {
			.name = "ti-soc-thermal",
			.pm = DEV_PM_OPS,
			.of_match_table	= of_ti_bandgap_match,
	},
};

module_platform_driver(ti_bandgap_sensor_driver);

MODULE_DESCRIPTION("OMAP4+ bandgap temperature sensor driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:ti-soc-thermal");
MODULE_AUTHOR("Texas Instrument Inc.");
