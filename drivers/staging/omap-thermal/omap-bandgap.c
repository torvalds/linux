/*
 * OMAP4 Bandgap temperature sensor driver
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
#include <linux/mutex.h>
#include <linux/reboot.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/io.h>

#include "omap-bandgap.h"

static u32 omap_bandgap_readl(struct omap_bandgap *bg_ptr, u32 reg)
{
	return readl(bg_ptr->base + reg);
}

static void omap_bandgap_writel(struct omap_bandgap *bg_ptr, u32 val, u32 reg)
{
	writel(val, bg_ptr->base + reg);
}

static int omap_bandgap_power(struct omap_bandgap *bg_ptr, bool on)
{
	struct temp_sensor_registers *tsr;
	int i;
	u32 ctrl;

	if (!OMAP_BANDGAP_HAS(bg_ptr, POWER_SWITCH))
		return 0;

	for (i = 0; i < bg_ptr->conf->sensor_count; i++) {
		tsr = bg_ptr->conf->sensors[i].registers;
		ctrl = omap_bandgap_readl(bg_ptr, tsr->temp_sensor_ctrl);
		ctrl &= ~tsr->bgap_tempsoff_mask;
		/* active on 0 */
		ctrl |= !on << __ffs(tsr->bgap_tempsoff_mask);

		/* write BGAP_TEMPSOFF should be reset to 0 */
		omap_bandgap_writel(bg_ptr, ctrl, tsr->temp_sensor_ctrl);
	}

	return 0;
}

static u32 omap_bandgap_read_temp(struct omap_bandgap *bg_ptr, int id)
{
	struct temp_sensor_registers *tsr;
	u32 temp, ctrl, reg;

	tsr = bg_ptr->conf->sensors[id].registers;
	reg = tsr->temp_sensor_ctrl;

	if (OMAP_BANDGAP_HAS(bg_ptr, FREEZE_BIT)) {
		ctrl = omap_bandgap_readl(bg_ptr, tsr->bgap_mask_ctrl);
		ctrl |= tsr->mask_freeze_mask;
		omap_bandgap_writel(bg_ptr, ctrl, tsr->bgap_mask_ctrl);
		/*
		 * In case we cannot read from cur_dtemp / dtemp_0,
		 * then we read from the last valid temp read
		 */
		reg = tsr->ctrl_dtemp_1;
	}

	/* read temperature */
	temp = omap_bandgap_readl(bg_ptr, reg);
	temp &= tsr->bgap_dtemp_mask;

	if (OMAP_BANDGAP_HAS(bg_ptr, FREEZE_BIT)) {
		ctrl = omap_bandgap_readl(bg_ptr, tsr->bgap_mask_ctrl);
		ctrl &= ~tsr->mask_freeze_mask;
		omap_bandgap_writel(bg_ptr, ctrl, tsr->bgap_mask_ctrl);
	}

	return temp;
}

/* This is the Talert handler. Call it only if HAS(TALERT) is set */
static irqreturn_t talert_irq_handler(int irq, void *data)
{
	struct omap_bandgap *bg_ptr = data;
	struct temp_sensor_registers *tsr;
	u32 t_hot = 0, t_cold = 0, ctrl;
	int i;

	bg_ptr = data;
	/* Read the status of t_hot */
	for (i = 0; i < bg_ptr->conf->sensor_count; i++) {
		tsr = bg_ptr->conf->sensors[i].registers;
		t_hot = omap_bandgap_readl(bg_ptr, tsr->bgap_status);
		t_hot &= tsr->status_hot_mask;

		/* Read the status of t_cold */
		t_cold = omap_bandgap_readl(bg_ptr, tsr->bgap_status);
		t_cold &= tsr->status_cold_mask;

		if (!t_cold && !t_hot)
			continue;

		ctrl = omap_bandgap_readl(bg_ptr, tsr->bgap_mask_ctrl);
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

		omap_bandgap_writel(bg_ptr, ctrl, tsr->bgap_mask_ctrl);

		dev_dbg(bg_ptr->dev,
			"%s: IRQ from %s sensor: hotevent %d coldevent %d\n",
			__func__, bg_ptr->conf->sensors[i].domain,
			t_hot, t_cold);

		/* report temperature to whom may concern */
		if (bg_ptr->conf->report_temperature)
			bg_ptr->conf->report_temperature(bg_ptr, i);
	}

	return IRQ_HANDLED;
}

/* This is the Tshut handler. Call it only if HAS(TSHUT) is set */
static irqreturn_t omap_bandgap_tshut_irq_handler(int irq, void *data)
{
	pr_emerg("%s: TSHUT temperature reached. Needs shut down...\n",
		 __func__);

	orderly_poweroff(true);

	return IRQ_HANDLED;
}

static
int adc_to_temp_conversion(struct omap_bandgap *bg_ptr, int id, int adc_val,
			   int *t)
{
	struct temp_sensor_data *ts_data = bg_ptr->conf->sensors[id].ts_data;

	/* look up for temperature in the table and return the temperature */
	if (adc_val < ts_data->adc_start_val || adc_val > ts_data->adc_end_val)
		return -ERANGE;

	*t = bg_ptr->conf->conv_table[adc_val - ts_data->adc_start_val];

	return 0;
}

static int temp_to_adc_conversion(long temp, struct omap_bandgap *bg_ptr, int i,
				  int *adc)
{
	struct temp_sensor_data *ts_data = bg_ptr->conf->sensors[i].ts_data;
	const int *conv_table = bg_ptr->conf->conv_table;
	int high, low, mid;

	low = 0;
	high = ts_data->adc_end_val - ts_data->adc_start_val;
	mid = (high + low) / 2;

	if (temp < conv_table[low] || temp > conv_table[high])
		return -EINVAL;

	while (low < high) {
		if (temp < conv_table[mid])
			high = mid - 1;
		else
			low = mid + 1;
		mid = (low + high) / 2;
	}

	*adc = ts_data->adc_start_val + low;

	return 0;
}

/* Talert masks. Call it only if HAS(TALERT) is set */
static int temp_sensor_unmask_interrupts(struct omap_bandgap *bg_ptr, int id,
					 u32 t_hot, u32 t_cold)
{
	struct temp_sensor_registers *tsr;
	u32 temp, reg_val;

	/* Read the current on die temperature */
	temp = omap_bandgap_read_temp(bg_ptr, id);

	tsr = bg_ptr->conf->sensors[id].registers;
	reg_val = omap_bandgap_readl(bg_ptr, tsr->bgap_mask_ctrl);

	if (temp < t_hot)
		reg_val |= tsr->mask_hot_mask;
	else
		reg_val &= ~tsr->mask_hot_mask;

	if (t_cold < temp)
		reg_val |= tsr->mask_cold_mask;
	else
		reg_val &= ~tsr->mask_cold_mask;
	omap_bandgap_writel(bg_ptr, reg_val, tsr->bgap_mask_ctrl);

	return 0;
}

static
int add_hyst(int adc_val, int hyst_val, struct omap_bandgap *bg_ptr, int i,
	     u32 *sum)
{
	int temp, ret;

	ret = adc_to_temp_conversion(bg_ptr, i, adc_val, &temp);
	if (ret < 0)
		return ret;

	temp += hyst_val;

	return temp_to_adc_conversion(temp, bg_ptr, i, sum);
}

/* Talert Thot threshold. Call it only if HAS(TALERT) is set */
static
int temp_sensor_configure_thot(struct omap_bandgap *bg_ptr, int id, int t_hot)
{
	struct temp_sensor_data *ts_data = bg_ptr->conf->sensors[id].ts_data;
	struct temp_sensor_registers *tsr;
	u32 thresh_val, reg_val;
	int cold, err = 0;

	tsr = bg_ptr->conf->sensors[id].registers;

	/* obtain the T cold value */
	thresh_val = omap_bandgap_readl(bg_ptr, tsr->bgap_threshold);
	cold = (thresh_val & tsr->threshold_tcold_mask) >>
	    __ffs(tsr->threshold_tcold_mask);
	if (t_hot <= cold) {
		/* change the t_cold to t_hot - 5000 millidegrees */
		err |= add_hyst(t_hot, -ts_data->hyst_val, bg_ptr, id, &cold);
		/* write the new t_cold value */
		reg_val = thresh_val & (~tsr->threshold_tcold_mask);
		reg_val |= cold << __ffs(tsr->threshold_tcold_mask);
		omap_bandgap_writel(bg_ptr, reg_val, tsr->bgap_threshold);
		thresh_val = reg_val;
	}

	/* write the new t_hot value */
	reg_val = thresh_val & ~tsr->threshold_thot_mask;
	reg_val |= (t_hot << __ffs(tsr->threshold_thot_mask));
	omap_bandgap_writel(bg_ptr, reg_val, tsr->bgap_threshold);
	if (err) {
		dev_err(bg_ptr->dev, "failed to reprogram thot threshold\n");
		return -EIO;
	}

	return temp_sensor_unmask_interrupts(bg_ptr, id, t_hot, cold);
}

/* Talert Thot and Tcold thresholds. Call it only if HAS(TALERT) is set */
static
int temp_sensor_init_talert_thresholds(struct omap_bandgap *bg_ptr, int id,
				       int t_hot, int t_cold)
{
	struct temp_sensor_registers *tsr;
	u32 reg_val, thresh_val;

	tsr = bg_ptr->conf->sensors[id].registers;
	thresh_val = omap_bandgap_readl(bg_ptr, tsr->bgap_threshold);

	/* write the new t_cold value */
	reg_val = thresh_val & ~tsr->threshold_tcold_mask;
	reg_val |= (t_cold << __ffs(tsr->threshold_tcold_mask));
	omap_bandgap_writel(bg_ptr, reg_val, tsr->bgap_threshold);

	thresh_val = omap_bandgap_readl(bg_ptr, tsr->bgap_threshold);

	/* write the new t_hot value */
	reg_val = thresh_val & ~tsr->threshold_thot_mask;
	reg_val |= (t_hot << __ffs(tsr->threshold_thot_mask));
	omap_bandgap_writel(bg_ptr, reg_val, tsr->bgap_threshold);

	reg_val = omap_bandgap_readl(bg_ptr, tsr->bgap_mask_ctrl);
	reg_val |= tsr->mask_hot_mask;
	reg_val |= tsr->mask_cold_mask;
	omap_bandgap_writel(bg_ptr, reg_val, tsr->bgap_mask_ctrl);

	return 0;
}

/* Talert Tcold threshold. Call it only if HAS(TALERT) is set */
static
int temp_sensor_configure_tcold(struct omap_bandgap *bg_ptr, int id,
				int t_cold)
{
	struct temp_sensor_data *ts_data = bg_ptr->conf->sensors[id].ts_data;
	struct temp_sensor_registers *tsr;
	u32 thresh_val, reg_val;
	int hot, err = 0;

	tsr = bg_ptr->conf->sensors[id].registers;
	/* obtain the T cold value */
	thresh_val = omap_bandgap_readl(bg_ptr, tsr->bgap_threshold);
	hot = (thresh_val & tsr->threshold_thot_mask) >>
	    __ffs(tsr->threshold_thot_mask);

	if (t_cold >= hot) {
		/* change the t_hot to t_cold + 5000 millidegrees */
		err |= add_hyst(t_cold, ts_data->hyst_val, bg_ptr, id, &hot);
		/* write the new t_hot value */
		reg_val = thresh_val & (~tsr->threshold_thot_mask);
		reg_val |= hot << __ffs(tsr->threshold_thot_mask);
		omap_bandgap_writel(bg_ptr, reg_val, tsr->bgap_threshold);
		thresh_val = reg_val;
	}

	/* write the new t_cold value */
	reg_val = thresh_val & ~tsr->threshold_tcold_mask;
	reg_val |= (t_cold << __ffs(tsr->threshold_tcold_mask));
	omap_bandgap_writel(bg_ptr, reg_val, tsr->bgap_threshold);
	if (err) {
		dev_err(bg_ptr->dev, "failed to reprogram tcold threshold\n");
		return -EIO;
	}

	return temp_sensor_unmask_interrupts(bg_ptr, id, hot, t_cold);
}

/* This is Tshut Thot config. Call it only if HAS(TSHUT_CONFIG) is set */
static int temp_sensor_configure_tshut_hot(struct omap_bandgap *bg_ptr,
					   int id, int tshut_hot)
{
	struct temp_sensor_registers *tsr;
	u32 reg_val;

	tsr = bg_ptr->conf->sensors[id].registers;
	reg_val = omap_bandgap_readl(bg_ptr, tsr->tshut_threshold);
	reg_val &= ~tsr->tshut_hot_mask;
	reg_val |= tshut_hot << __ffs(tsr->tshut_hot_mask);
	omap_bandgap_writel(bg_ptr, reg_val, tsr->tshut_threshold);

	return 0;
}

/* This is Tshut Tcold config. Call it only if HAS(TSHUT_CONFIG) is set */
static int temp_sensor_configure_tshut_cold(struct omap_bandgap *bg_ptr,
					    int id, int tshut_cold)
{
	struct temp_sensor_registers *tsr;
	u32 reg_val;

	tsr = bg_ptr->conf->sensors[id].registers;
	reg_val = omap_bandgap_readl(bg_ptr, tsr->tshut_threshold);
	reg_val &= ~tsr->tshut_cold_mask;
	reg_val |= tshut_cold << __ffs(tsr->tshut_cold_mask);
	omap_bandgap_writel(bg_ptr, reg_val, tsr->tshut_threshold);

	return 0;
}

/* This is counter config. Call it only if HAS(COUNTER) is set */
static int configure_temp_sensor_counter(struct omap_bandgap *bg_ptr, int id,
					 u32 counter)
{
	struct temp_sensor_registers *tsr;
	u32 val;

	tsr = bg_ptr->conf->sensors[id].registers;
	val = omap_bandgap_readl(bg_ptr, tsr->bgap_counter);
	val &= ~tsr->counter_mask;
	val |= counter << __ffs(tsr->counter_mask);
	omap_bandgap_writel(bg_ptr, val, tsr->bgap_counter);

	return 0;
}

#define bandgap_is_valid(b)						\
			(!IS_ERR_OR_NULL(b))
#define bandgap_is_valid_sensor_id(b, i)				\
			((i) >= 0 && (i) < (b)->conf->sensor_count)
static inline int omap_bandgap_validate(struct omap_bandgap *bg_ptr, int id)
{
	if (!bandgap_is_valid(bg_ptr)) {
		pr_err("%s: invalid bandgap pointer\n", __func__);
		return -EINVAL;
	}

	if (!bandgap_is_valid_sensor_id(bg_ptr, id)) {
		dev_err(bg_ptr->dev, "%s: sensor id out of range (%d)\n",
			__func__, id);
		return -ERANGE;
	}

	return 0;
}

/* Exposed APIs */
/**
 * omap_bandgap_read_thot() - reads sensor current thot
 * @bg_ptr - pointer to bandgap instance
 * @id - sensor id
 * @thot - resulting current thot value
 *
 * returns 0 on success or the proper error code
 */
int omap_bandgap_read_thot(struct omap_bandgap *bg_ptr, int id,
			      int *thot)
{
	struct temp_sensor_registers *tsr;
	u32 temp;
	int ret;

	ret = omap_bandgap_validate(bg_ptr, id);
	if (ret)
		return ret;

	if (!OMAP_BANDGAP_HAS(bg_ptr, TALERT))
		return -ENOTSUPP;

	tsr = bg_ptr->conf->sensors[id].registers;
	temp = omap_bandgap_readl(bg_ptr, tsr->bgap_threshold);
	temp = (temp & tsr->threshold_thot_mask) >>
		__ffs(tsr->threshold_thot_mask);
	ret |= adc_to_temp_conversion(bg_ptr, id, temp, &temp);
	if (ret) {
		dev_err(bg_ptr->dev, "failed to read thot\n");
		return -EIO;
	}

	*thot = temp;

	return 0;
}

/**
 * omap_bandgap_write_thot() - sets sensor current thot
 * @bg_ptr - pointer to bandgap instance
 * @id - sensor id
 * @val - desired thot value
 *
 * returns 0 on success or the proper error code
 */
int omap_bandgap_write_thot(struct omap_bandgap *bg_ptr, int id, int val)
{
	struct temp_sensor_data *ts_data;
	struct temp_sensor_registers *tsr;
	u32 t_hot;
	int ret;

	ret = omap_bandgap_validate(bg_ptr, id);
	if (ret)
		return ret;

	if (!OMAP_BANDGAP_HAS(bg_ptr, TALERT))
		return -ENOTSUPP;

	ts_data = bg_ptr->conf->sensors[id].ts_data;
	tsr = bg_ptr->conf->sensors[id].registers;

	if (val < ts_data->min_temp + ts_data->hyst_val)
		return -EINVAL;
	ret = temp_to_adc_conversion(val, bg_ptr, id, &t_hot);
	if (ret < 0)
		return ret;

	mutex_lock(&bg_ptr->bg_mutex);
	temp_sensor_configure_thot(bg_ptr, id, t_hot);
	mutex_unlock(&bg_ptr->bg_mutex);

	return 0;
}

/**
 * omap_bandgap_read_tcold() - reads sensor current tcold
 * @bg_ptr - pointer to bandgap instance
 * @id - sensor id
 * @tcold - resulting current tcold value
 *
 * returns 0 on success or the proper error code
 */
int omap_bandgap_read_tcold(struct omap_bandgap *bg_ptr, int id,
			       int *tcold)
{
	struct temp_sensor_registers *tsr;
	u32 temp;
	int ret;

	ret = omap_bandgap_validate(bg_ptr, id);
	if (ret)
		return ret;

	if (!OMAP_BANDGAP_HAS(bg_ptr, TALERT))
		return -ENOTSUPP;

	tsr = bg_ptr->conf->sensors[id].registers;
	temp = omap_bandgap_readl(bg_ptr, tsr->bgap_threshold);
	temp = (temp & tsr->threshold_tcold_mask)
	    >> __ffs(tsr->threshold_tcold_mask);
	ret |= adc_to_temp_conversion(bg_ptr, id, temp, &temp);
	if (ret)
		return -EIO;

	*tcold = temp;

	return 0;
}

/**
 * omap_bandgap_write_tcold() - sets the sensor tcold
 * @bg_ptr - pointer to bandgap instance
 * @id - sensor id
 * @val - desired tcold value
 *
 * returns 0 on success or the proper error code
 */
int omap_bandgap_write_tcold(struct omap_bandgap *bg_ptr, int id, int val)
{
	struct temp_sensor_data *ts_data;
	struct temp_sensor_registers *tsr;
	u32 t_cold;
	int ret;

	ret = omap_bandgap_validate(bg_ptr, id);
	if (ret)
		return ret;

	if (!OMAP_BANDGAP_HAS(bg_ptr, TALERT))
		return -ENOTSUPP;

	ts_data = bg_ptr->conf->sensors[id].ts_data;
	tsr = bg_ptr->conf->sensors[id].registers;
	if (val > ts_data->max_temp + ts_data->hyst_val)
		return -EINVAL;

	ret = temp_to_adc_conversion(val, bg_ptr, id, &t_cold);
	if (ret < 0)
		return ret;

	mutex_lock(&bg_ptr->bg_mutex);
	temp_sensor_configure_tcold(bg_ptr, id, t_cold);
	mutex_unlock(&bg_ptr->bg_mutex);

	return 0;
}

/**
 * omap_bandgap_read_update_interval() - read the sensor update interval
 * @bg_ptr - pointer to bandgap instance
 * @id - sensor id
 * @interval - resulting update interval in miliseconds
 *
 * returns 0 on success or the proper error code
 */
int omap_bandgap_read_update_interval(struct omap_bandgap *bg_ptr, int id,
					 int *interval)
{
	struct temp_sensor_registers *tsr;
	u32 time;
	int ret;

	ret = omap_bandgap_validate(bg_ptr, id);
	if (ret)
		return ret;

	if (!OMAP_BANDGAP_HAS(bg_ptr, COUNTER))
		return -ENOTSUPP;

	tsr = bg_ptr->conf->sensors[id].registers;
	time = omap_bandgap_readl(bg_ptr, tsr->bgap_counter);
	time = (time & tsr->counter_mask) >> __ffs(tsr->counter_mask);
	time = time * 1000 / bg_ptr->clk_rate;

	*interval = time;

	return 0;
}

/**
 * omap_bandgap_write_update_interval() - set the update interval
 * @bg_ptr - pointer to bandgap instance
 * @id - sensor id
 * @interval - desired update interval in miliseconds
 *
 * returns 0 on success or the proper error code
 */
int omap_bandgap_write_update_interval(struct omap_bandgap *bg_ptr,
					  int id, u32 interval)
{
	int ret = omap_bandgap_validate(bg_ptr, id);
	if (ret)
		return ret;

	if (!OMAP_BANDGAP_HAS(bg_ptr, COUNTER))
		return -ENOTSUPP;

	interval = interval * bg_ptr->clk_rate / 1000;
	mutex_lock(&bg_ptr->bg_mutex);
	configure_temp_sensor_counter(bg_ptr, id, interval);
	mutex_unlock(&bg_ptr->bg_mutex);

	return 0;
}

/**
 * omap_bandgap_read_temperature() - report current temperature
 * @bg_ptr - pointer to bandgap instance
 * @id - sensor id
 * @temperature - resulting temperature
 *
 * returns 0 on success or the proper error code
 */
int omap_bandgap_read_temperature(struct omap_bandgap *bg_ptr, int id,
				     int *temperature)
{
	struct temp_sensor_registers *tsr;
	u32 temp;
	int ret;

	ret = omap_bandgap_validate(bg_ptr, id);
	if (ret)
		return ret;

	tsr = bg_ptr->conf->sensors[id].registers;
	mutex_lock(&bg_ptr->bg_mutex);
	temp = omap_bandgap_read_temp(bg_ptr, id);
	mutex_unlock(&bg_ptr->bg_mutex);

	ret |= adc_to_temp_conversion(bg_ptr, id, temp, &temp);
	if (ret)
		return -EIO;

	*temperature = temp;

	return 0;
}

/**
 * omap_bandgap_set_sensor_data() - helper function to store thermal
 * framework related data.
 * @bg_ptr - pointer to bandgap instance
 * @id - sensor id
 * @data - thermal framework related data to be stored
 *
 * returns 0 on success or the proper error code
 */
int omap_bandgap_set_sensor_data(struct omap_bandgap *bg_ptr, int id,
				void *data)
{
	int ret = omap_bandgap_validate(bg_ptr, id);
	if (ret)
		return ret;

	bg_ptr->conf->sensors[id].data = data;

	return 0;
}

/**
 * omap_bandgap_get_sensor_data() - helper function to get thermal
 * framework related data.
 * @bg_ptr - pointer to bandgap instance
 * @id - sensor id
 *
 * returns data stored by set function with sensor id on success or NULL
 */
void *omap_bandgap_get_sensor_data(struct omap_bandgap *bg_ptr, int id)
{
	int ret = omap_bandgap_validate(bg_ptr, id);
	if (ret)
		return ERR_PTR(ret);

	return bg_ptr->conf->sensors[id].data;
}

static int
omap_bandgap_force_single_read(struct omap_bandgap *bg_ptr, int id)
{
	struct temp_sensor_registers *tsr;
	u32 temp = 0, counter = 1000;

	tsr = bg_ptr->conf->sensors[id].registers;
	/* Select single conversion mode */
	if (OMAP_BANDGAP_HAS(bg_ptr, MODE_CONFIG)) {
		temp = omap_bandgap_readl(bg_ptr, tsr->bgap_mode_ctrl);
		temp &= ~(1 << __ffs(tsr->mode_ctrl_mask));
		omap_bandgap_writel(bg_ptr, temp, tsr->bgap_mode_ctrl);
	}

	/* Start of Conversion = 1 */
	temp = omap_bandgap_readl(bg_ptr, tsr->temp_sensor_ctrl);
	temp |= 1 << __ffs(tsr->bgap_soc_mask);
	omap_bandgap_writel(bg_ptr, temp, tsr->temp_sensor_ctrl);
	/* Wait until DTEMP is updated */
	temp = omap_bandgap_read_temp(bg_ptr, id);

	while ((temp == 0) && --counter)
		temp = omap_bandgap_read_temp(bg_ptr, id);

	/* Start of Conversion = 0 */
	temp = omap_bandgap_readl(bg_ptr, tsr->temp_sensor_ctrl);
	temp &= ~(1 << __ffs(tsr->bgap_soc_mask));
	omap_bandgap_writel(bg_ptr, temp, tsr->temp_sensor_ctrl);

	return 0;
}

/**
 * enable_continuous_mode() - One time enabling of continuous conversion mode
 * @bg_ptr - pointer to scm instance
 *
 * Call this function only if HAS(MODE_CONFIG) is set
 */
static int enable_continuous_mode(struct omap_bandgap *bg_ptr)
{
	struct temp_sensor_registers *tsr;
	int i;
	u32 val;

	for (i = 0; i < bg_ptr->conf->sensor_count; i++) {
		/* Perform a single read just before enabling continuous */
		omap_bandgap_force_single_read(bg_ptr, i);
		tsr = bg_ptr->conf->sensors[i].registers;
		val = omap_bandgap_readl(bg_ptr, tsr->bgap_mode_ctrl);
		val |= 1 << __ffs(tsr->mode_ctrl_mask);
		omap_bandgap_writel(bg_ptr, val, tsr->bgap_mode_ctrl);
	}

	return 0;
}

static int omap_bandgap_tshut_init(struct omap_bandgap *bg_ptr,
				      struct platform_device *pdev)
{
	int gpio_nr = bg_ptr->tshut_gpio;
	int status;

	/* Request for gpio_86 line */
	status = gpio_request(gpio_nr, "tshut");
	if (status < 0) {
		dev_err(bg_ptr->dev,
			"Could not request for TSHUT GPIO:%i\n", 86);
		return status;
	}
	status = gpio_direction_input(gpio_nr);
	if (status) {
		dev_err(bg_ptr->dev,
			"Cannot set input TSHUT GPIO %d\n", gpio_nr);
		return status;
	}

	status = request_irq(gpio_to_irq(gpio_nr),
			     omap_bandgap_tshut_irq_handler,
			     IRQF_TRIGGER_RISING, "tshut",
			     NULL);
	if (status) {
		gpio_free(gpio_nr);
		dev_err(bg_ptr->dev, "request irq failed for TSHUT");
	}

	return 0;
}

/* Initialization of Talert. Call it only if HAS(TALERT) is set */
static int omap_bandgap_talert_init(struct omap_bandgap *bg_ptr,
				       struct platform_device *pdev)
{
	int ret;

	bg_ptr->irq = platform_get_irq(pdev, 0);
	if (bg_ptr->irq < 0) {
		dev_err(&pdev->dev, "get_irq failed\n");
		return bg_ptr->irq;
	}
	ret = request_threaded_irq(bg_ptr->irq, NULL,
				   talert_irq_handler,
				   IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
				   "talert", bg_ptr);
	if (ret) {
		dev_err(&pdev->dev, "Request threaded irq failed.\n");
		return ret;
	}

	return 0;
}

static const struct of_device_id of_omap_bandgap_match[];
static struct omap_bandgap *omap_bandgap_build(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	const struct of_device_id *of_id;
	struct omap_bandgap *bg_ptr;
	struct resource *res;
	u32 prop;
	int i;

	/* just for the sake */
	if (!node) {
		dev_err(&pdev->dev, "no platform information available\n");
		return ERR_PTR(-EINVAL);
	}

	bg_ptr = devm_kzalloc(&pdev->dev, sizeof(struct omap_bandgap),
				    GFP_KERNEL);
	if (!bg_ptr) {
		dev_err(&pdev->dev, "Unable to allocate mem for driver ref\n");
		return ERR_PTR(-ENOMEM);
	}

	of_id = of_match_device(of_omap_bandgap_match, &pdev->dev);
	if (of_id)
		bg_ptr->conf = of_id->data;

	i = 0;
	do {
		void __iomem *chunk;

		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res)
			break;
		chunk = devm_ioremap_resource(&pdev->dev, res);
		if (i == 0)
			bg_ptr->base = chunk;
		if (IS_ERR(chunk))
			return ERR_CAST(chunk);
		
		i++;
	} while (res);

	if (OMAP_BANDGAP_HAS(bg_ptr, TSHUT)) {
		if (of_property_read_u32(node, "ti,tshut-gpio", &prop) < 0) {
			dev_err(&pdev->dev, "missing tshut gpio in device tree\n");
			return ERR_PTR(-EINVAL);
		}
		bg_ptr->tshut_gpio = prop;
		if (!gpio_is_valid(bg_ptr->tshut_gpio)) {
			dev_err(&pdev->dev, "invalid gpio for tshut (%d)\n",
				bg_ptr->tshut_gpio);
			return ERR_PTR(-EINVAL);
		}
	}

	return bg_ptr;
}

static
int omap_bandgap_probe(struct platform_device *pdev)
{
	struct omap_bandgap *bg_ptr;
	int clk_rate, ret = 0, i;

	bg_ptr = omap_bandgap_build(pdev);
	if (IS_ERR_OR_NULL(bg_ptr)) {
		dev_err(&pdev->dev, "failed to fetch platform data\n");
		return PTR_ERR(bg_ptr);
	}
	bg_ptr->dev = &pdev->dev;

	if (OMAP_BANDGAP_HAS(bg_ptr, TSHUT)) {
		ret = omap_bandgap_tshut_init(bg_ptr, pdev);
		if (ret) {
			dev_err(&pdev->dev,
				"failed to initialize system tshut IRQ\n");
			return ret;
		}
	}

	bg_ptr->fclock = clk_get(NULL, bg_ptr->conf->fclock_name);
	ret = IS_ERR_OR_NULL(bg_ptr->fclock);
	if (ret) {
		dev_err(&pdev->dev, "failed to request fclock reference\n");
		goto free_irqs;
	}

	bg_ptr->div_clk = clk_get(NULL,  bg_ptr->conf->div_ck_name);
	ret = IS_ERR_OR_NULL(bg_ptr->div_clk);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to request div_ts_ck clock ref\n");
		goto free_irqs;
	}

	for (i = 0; i < bg_ptr->conf->sensor_count; i++) {
		struct temp_sensor_registers *tsr;
		u32 val;

		tsr = bg_ptr->conf->sensors[i].registers;
		/*
		 * check if the efuse has a non-zero value if not
		 * it is an untrimmed sample and the temperatures
		 * may not be accurate
		 */
		val = omap_bandgap_readl(bg_ptr, tsr->bgap_efuse);
		if (ret || !val)
			dev_info(&pdev->dev,
				 "Non-trimmed BGAP, Temp not accurate\n");
	}

	clk_rate = clk_round_rate(bg_ptr->div_clk,
				  bg_ptr->conf->sensors[0].ts_data->max_freq);
	if (clk_rate < bg_ptr->conf->sensors[0].ts_data->min_freq ||
	    clk_rate == 0xffffffff) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "wrong clock rate (%d)\n", clk_rate);
		goto put_clks;
	}

	ret = clk_set_rate(bg_ptr->div_clk, clk_rate);
	if (ret)
		dev_err(&pdev->dev, "Cannot re-set clock rate. Continuing\n");

	bg_ptr->clk_rate = clk_rate;
	if (OMAP_BANDGAP_HAS(bg_ptr, CLK_CTRL))
		clk_enable(bg_ptr->fclock);


	mutex_init(&bg_ptr->bg_mutex);
	bg_ptr->dev = &pdev->dev;
	platform_set_drvdata(pdev, bg_ptr);

	omap_bandgap_power(bg_ptr, true);

	/* Set default counter to 1 for now */
	if (OMAP_BANDGAP_HAS(bg_ptr, COUNTER))
		for (i = 0; i < bg_ptr->conf->sensor_count; i++)
			configure_temp_sensor_counter(bg_ptr, i, 1);

	for (i = 0; i < bg_ptr->conf->sensor_count; i++) {
		struct temp_sensor_data *ts_data;

		ts_data = bg_ptr->conf->sensors[i].ts_data;

		if (OMAP_BANDGAP_HAS(bg_ptr, TALERT))
			temp_sensor_init_talert_thresholds(bg_ptr, i,
							   ts_data->t_hot,
							   ts_data->t_cold);
		if (OMAP_BANDGAP_HAS(bg_ptr, TSHUT_CONFIG)) {
			temp_sensor_configure_tshut_hot(bg_ptr, i,
							ts_data->tshut_hot);
			temp_sensor_configure_tshut_cold(bg_ptr, i,
							 ts_data->tshut_cold);
		}
	}

	if (OMAP_BANDGAP_HAS(bg_ptr, MODE_CONFIG))
		enable_continuous_mode(bg_ptr);

	/* Set .250 seconds time as default counter */
	if (OMAP_BANDGAP_HAS(bg_ptr, COUNTER))
		for (i = 0; i < bg_ptr->conf->sensor_count; i++)
			configure_temp_sensor_counter(bg_ptr, i,
						      bg_ptr->clk_rate / 4);

	/* Every thing is good? Then expose the sensors */
	for (i = 0; i < bg_ptr->conf->sensor_count; i++) {
		char *domain;

		if (bg_ptr->conf->sensors[i].register_cooling)
			bg_ptr->conf->sensors[i].register_cooling(bg_ptr, i);

		domain = bg_ptr->conf->sensors[i].domain;
		if (bg_ptr->conf->expose_sensor)
			bg_ptr->conf->expose_sensor(bg_ptr, i, domain);
	}

	/*
	 * Enable the Interrupts once everything is set. Otherwise irq handler
	 * might be called as soon as it is enabled where as rest of framework
	 * is still getting initialised.
	 */
	if (OMAP_BANDGAP_HAS(bg_ptr, TALERT)) {
		ret = omap_bandgap_talert_init(bg_ptr, pdev);
		if (ret) {
			dev_err(&pdev->dev, "failed to initialize Talert IRQ\n");
			i = bg_ptr->conf->sensor_count;
			goto disable_clk;
		}
	}

	return 0;

disable_clk:
	if (OMAP_BANDGAP_HAS(bg_ptr, CLK_CTRL))
		clk_disable(bg_ptr->fclock);
put_clks:
	clk_put(bg_ptr->fclock);
	clk_put(bg_ptr->div_clk);
free_irqs:
	if (OMAP_BANDGAP_HAS(bg_ptr, TSHUT)) {
		free_irq(gpio_to_irq(bg_ptr->tshut_gpio), NULL);
		gpio_free(bg_ptr->tshut_gpio);
	}

	return ret;
}

static
int omap_bandgap_remove(struct platform_device *pdev)
{
	struct omap_bandgap *bg_ptr = platform_get_drvdata(pdev);
	int i;

	/* First thing is to remove sensor interfaces */
	for (i = 0; i < bg_ptr->conf->sensor_count; i++) {
		if (bg_ptr->conf->sensors[i].register_cooling)
			bg_ptr->conf->sensors[i].unregister_cooling(bg_ptr, i);

		if (bg_ptr->conf->remove_sensor)
			bg_ptr->conf->remove_sensor(bg_ptr, i);
	}

	omap_bandgap_power(bg_ptr, false);

	if (OMAP_BANDGAP_HAS(bg_ptr, CLK_CTRL))
		clk_disable(bg_ptr->fclock);
	clk_put(bg_ptr->fclock);
	clk_put(bg_ptr->div_clk);

	if (OMAP_BANDGAP_HAS(bg_ptr, TALERT))
		free_irq(bg_ptr->irq, bg_ptr);

	if (OMAP_BANDGAP_HAS(bg_ptr, TSHUT)) {
		free_irq(gpio_to_irq(bg_ptr->tshut_gpio), NULL);
		gpio_free(bg_ptr->tshut_gpio);
	}

	return 0;
}

#ifdef CONFIG_PM
static int omap_bandgap_save_ctxt(struct omap_bandgap *bg_ptr)
{
	int i;

	for (i = 0; i < bg_ptr->conf->sensor_count; i++) {
		struct temp_sensor_registers *tsr;
		struct temp_sensor_regval *rval;

		rval = &bg_ptr->conf->sensors[i].regval;
		tsr = bg_ptr->conf->sensors[i].registers;

		if (OMAP_BANDGAP_HAS(bg_ptr, MODE_CONFIG))
			rval->bg_mode_ctrl = omap_bandgap_readl(bg_ptr,
							tsr->bgap_mode_ctrl);
		if (OMAP_BANDGAP_HAS(bg_ptr, COUNTER))
			rval->bg_counter = omap_bandgap_readl(bg_ptr,
							tsr->bgap_counter);
		if (OMAP_BANDGAP_HAS(bg_ptr, TALERT)) {
			rval->bg_threshold = omap_bandgap_readl(bg_ptr,
							tsr->bgap_threshold);
			rval->bg_ctrl = omap_bandgap_readl(bg_ptr,
						   tsr->bgap_mask_ctrl);
		}

		if (OMAP_BANDGAP_HAS(bg_ptr, TSHUT_CONFIG))
			rval->tshut_threshold = omap_bandgap_readl(bg_ptr,
						   tsr->tshut_threshold);
	}

	return 0;
}

static int omap_bandgap_restore_ctxt(struct omap_bandgap *bg_ptr)
{
	int i;

	for (i = 0; i < bg_ptr->conf->sensor_count; i++) {
		struct temp_sensor_registers *tsr;
		struct temp_sensor_regval *rval;
		u32 val = 0;

		rval = &bg_ptr->conf->sensors[i].regval;
		tsr = bg_ptr->conf->sensors[i].registers;

		if (OMAP_BANDGAP_HAS(bg_ptr, COUNTER))
			val = omap_bandgap_readl(bg_ptr, tsr->bgap_counter);

		if (OMAP_BANDGAP_HAS(bg_ptr, TSHUT_CONFIG))
			omap_bandgap_writel(bg_ptr,
				rval->tshut_threshold,
					   tsr->tshut_threshold);
		/* Force immediate temperature measurement and update
		 * of the DTEMP field
		 */
		omap_bandgap_force_single_read(bg_ptr, i);

		if (OMAP_BANDGAP_HAS(bg_ptr, COUNTER))
			omap_bandgap_writel(bg_ptr, rval->bg_counter,
						   tsr->bgap_counter);
		if (OMAP_BANDGAP_HAS(bg_ptr, MODE_CONFIG))
			omap_bandgap_writel(bg_ptr, rval->bg_mode_ctrl,
						   tsr->bgap_mode_ctrl);
		if (OMAP_BANDGAP_HAS(bg_ptr, TALERT)) {
			omap_bandgap_writel(bg_ptr,
						   rval->bg_threshold,
						   tsr->bgap_threshold);
			omap_bandgap_writel(bg_ptr, rval->bg_ctrl,
						   tsr->bgap_mask_ctrl);
		}
	}

	return 0;
}

static int omap_bandgap_suspend(struct device *dev)
{
	struct omap_bandgap *bg_ptr = dev_get_drvdata(dev);
	int err;

	err = omap_bandgap_save_ctxt(bg_ptr);
	omap_bandgap_power(bg_ptr, false);

	if (OMAP_BANDGAP_HAS(bg_ptr, CLK_CTRL))
		clk_disable(bg_ptr->fclock);

	return err;
}

static int omap_bandgap_resume(struct device *dev)
{
	struct omap_bandgap *bg_ptr = dev_get_drvdata(dev);

	if (OMAP_BANDGAP_HAS(bg_ptr, CLK_CTRL))
		clk_enable(bg_ptr->fclock);

	omap_bandgap_power(bg_ptr, true);

	return omap_bandgap_restore_ctxt(bg_ptr);
}
static const struct dev_pm_ops omap_bandgap_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(omap_bandgap_suspend,
				omap_bandgap_resume)
};

#define DEV_PM_OPS	(&omap_bandgap_dev_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif

static const struct of_device_id of_omap_bandgap_match[] = {
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
	/* Sentinel */
	{ },
};
MODULE_DEVICE_TABLE(of, of_omap_bandgap_match);

static struct platform_driver omap_bandgap_sensor_driver = {
	.probe = omap_bandgap_probe,
	.remove = omap_bandgap_remove,
	.driver = {
			.name = "omap-bandgap",
			.pm = DEV_PM_OPS,
			.of_match_table	= of_omap_bandgap_match,
	},
};

module_platform_driver(omap_bandgap_sensor_driver);

MODULE_DESCRIPTION("OMAP4+ bandgap temperature sensor driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:omap-bandgap");
MODULE_AUTHOR("Texas Instrument Inc.");
