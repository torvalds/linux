// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/auxiliary_bus.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/container_of.h>
#include <linux/device/devres.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/iio/adc/qcom-adc5-gen3-common.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/thermal.h>
#include <linux/types.h>
#include <linux/unaligned.h>

#include "../thermal_hwmon.h"

#define ADC_TM5_GEN3_CONFIG_REGS 12

struct device;
struct adc_tm5_gen3_chip;

/**
 * struct adc_tm5_gen3_channel_props - ADC_TM channel structure
 * @common_props: structure with common ADC channel properties.
 * @chip: ADC TM device.
 * @tzd: pointer to thermal device corresponding to TM channel.
 * @sdam_index: SDAM on which this TM channel lies.
 * @timer: time period of recurring TM measurement.
 * @tm_chan_index: TM channel number used.
 * @high_thr_en: TM high threshold crossing detection enabled.
 * @low_thr_en: TM low threshold crossing detection enabled.
 */
struct adc_tm5_gen3_channel_props {
	struct adc5_channel_common_prop common_props;
	struct adc_tm5_gen3_chip *chip;
	struct thermal_zone_device *tzd;
	unsigned int sdam_index;
	unsigned int timer;
	unsigned int tm_chan_index;
	bool high_thr_en;
	bool low_thr_en;
};

/**
 * struct adc_tm5_gen3_chip - ADC Thermal Monitoring device structure
 * @dev_data: Top-level ADC device data.
 * @chan_props: Array of ADC_TM channel structures.
 * @dev: SPMI ADC5 Gen3 device.
 * @nchannels: number of TM channels allocated
 */
struct adc_tm5_gen3_chip {
	struct adc5_device_data *dev_data;
	struct adc_tm5_gen3_channel_props *chan_props;
	struct device *dev;
	unsigned int nchannels;
};

DEFINE_GUARD(adc5_gen3, struct adc_tm5_gen3_chip *,
	     adc5_gen3_mutex_lock(_T->dev), adc5_gen3_mutex_unlock(_T->dev))

static int get_sdam_from_irq(struct adc_tm5_gen3_chip *adc_tm5, int irq)
{
	for (int i = 0; i < adc_tm5->dev_data->num_sdams; i++) {
		if (adc_tm5->dev_data->base[i].irq == irq)
			return i;
	}
	return -ENOENT;
}

static irqreturn_t adctm5_gen3_isr(int irq, void *dev_id)
{
	struct adc_tm5_gen3_chip *adc_tm5 = dev_id;
	int ret, sdam_num;
	u8 tm_status[2];
	u8 status, val;

	sdam_num = get_sdam_from_irq(adc_tm5, irq);
	if (sdam_num < 0)
		return IRQ_NONE;

	ret = adc5_gen3_read(adc_tm5->dev_data, sdam_num, ADC5_GEN3_STATUS1,
			     &status, sizeof(status));
	if (ret)
		return IRQ_NONE;

	if (status & ADC5_GEN3_STATUS1_CONV_FAULT) {
		val = ADC5_GEN3_CONV_ERR_CLR_REQ;
		adc5_gen3_status_clear(adc_tm5->dev_data, sdam_num,
				       ADC5_GEN3_CONV_ERR_CLR, &val, 1);
		return IRQ_HANDLED;
	}

	ret = adc5_gen3_read(adc_tm5->dev_data, sdam_num, ADC5_GEN3_TM_HIGH_STS,
			     tm_status, sizeof(tm_status));
	if (ret)
		return IRQ_NONE;

	if (tm_status[0] || tm_status[1])
		return IRQ_WAKE_THREAD;

	return IRQ_NONE;
}

static irqreturn_t adctm5_gen3_isr_thread(int irq, void *dev_id)
{
	struct adc_tm5_gen3_chip *adc_tm5 = dev_id;
	u8 tm_status[2];
	int sdam_index;

	sdam_index = get_sdam_from_irq(adc_tm5, irq);
	if (sdam_index < 0)
		return IRQ_NONE;

	scoped_guard(adc5_gen3, adc_tm5) {
		int ret;

		ret = adc5_gen3_read(adc_tm5->dev_data, sdam_index, ADC5_GEN3_TM_HIGH_STS,
				     tm_status, sizeof(tm_status));
		if (ret)
			return IRQ_NONE;

		ret = adc5_gen3_status_clear(adc_tm5->dev_data, sdam_index,
					     ADC5_GEN3_TM_HIGH_STS_CLR, tm_status,
					     sizeof(tm_status));
		if (ret)
			return IRQ_NONE;
	}

	for (int i = 0; i < adc_tm5->nchannels; i++) {
		struct adc_tm5_gen3_channel_props *chan_prop = &adc_tm5->chan_props[i];
		int offset = chan_prop->tm_chan_index;
		bool upper_set, lower_set;

		if (chan_prop->sdam_index != sdam_index)
			continue;

		upper_set = ((tm_status[0] & BIT(offset)) && chan_prop->high_thr_en);
		lower_set = ((tm_status[1] & BIT(offset)) && chan_prop->low_thr_en);

		if (!(upper_set || lower_set))
			continue;

		thermal_zone_device_update(chan_prop->tzd, THERMAL_TRIP_VIOLATED);
	}

	return IRQ_HANDLED;
}

static int adc_tm5_gen3_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct adc_tm5_gen3_channel_props *prop = thermal_zone_device_priv(tz);
	struct adc_tm5_gen3_chip *adc_tm5;

	if (!prop || !prop->chip)
		return -EINVAL;

	adc_tm5 = prop->chip;

	return adc5_gen3_get_scaled_reading(adc_tm5->dev, &prop->common_props, temp);
}

static int adc_tm5_gen3_disable_channel(struct adc_tm5_gen3_channel_props *prop)
{
	struct adc_tm5_gen3_chip *adc_tm5 = prop->chip;
	int ret;
	u8 val;

	prop->high_thr_en = false;
	prop->low_thr_en = false;

	ret = adc5_gen3_poll_wait_hs(adc_tm5->dev_data, prop->sdam_index);
	if (ret)
		return ret;

	val = BIT(prop->tm_chan_index);
	ret = adc5_gen3_write(adc_tm5->dev_data, prop->sdam_index,
			      ADC5_GEN3_TM_HIGH_STS_CLR, &val, sizeof(val));
	if (ret)
		return ret;

	ret = adc5_gen3_write(adc_tm5->dev_data, prop->sdam_index,
			      ADC5_GEN3_TM_LOW_STS_CLR, &val, sizeof(val));
	if (ret)
		return ret;

	val = MEAS_INT_DISABLE;
	ret = adc5_gen3_write(adc_tm5->dev_data, prop->sdam_index,
			      ADC5_GEN3_TIMER_SEL, &val, sizeof(val));
	if (ret)
		return ret;

	/* To indicate there is an actual conversion request */
	val = ADC5_GEN3_CHAN_CONV_REQ | prop->tm_chan_index;
	ret = adc5_gen3_write(adc_tm5->dev_data, prop->sdam_index,
			      ADC5_GEN3_PERPH_CH, &val, sizeof(val));
	if (ret)
		return ret;

	val = ADC5_GEN3_CONV_REQ_REQ;
	return adc5_gen3_write(adc_tm5->dev_data, prop->sdam_index,
			       ADC5_GEN3_CONV_REQ, &val, sizeof(val));
}

static int adc_tm5_gen3_configure(struct adc_tm5_gen3_channel_props *prop,
				  int low_temp, int high_temp)
{
	struct adc_tm5_gen3_chip *adc_tm5 = prop->chip;
	u8 buf[ADC_TM5_GEN3_CONFIG_REGS];
	u8 conv_req;
	u16 adc_code;
	int ret;

	ret = adc5_gen3_poll_wait_hs(adc_tm5->dev_data, prop->sdam_index);
	if (ret < 0)
		return ret;

	ret = adc5_gen3_read(adc_tm5->dev_data, prop->sdam_index,
			     ADC5_GEN3_SID, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	/* Write SID */
	buf[0] = FIELD_PREP(ADC5_GEN3_SID_MASK, prop->common_props.sid);

	/* Select TM channel and indicate there is an actual conversion request */
	buf[1] = ADC5_GEN3_CHAN_CONV_REQ | prop->tm_chan_index;

	buf[2] = prop->timer;

	/* Digital param selection */
	adc5_gen3_update_dig_param(&prop->common_props, &buf[3]);

	/* Update fast average sample value */
	buf[4] = FIELD_PREP(ADC5_GEN3_FAST_AVG_CTL_SAMPLES_MASK,
			    prop->common_props.avg_samples) | ADC5_GEN3_FAST_AVG_CTL_EN;

	/* Select ADC channel */
	buf[5] = prop->common_props.channel;

	/* Select HW settle delay for channel */
	buf[6] = FIELD_PREP(ADC5_GEN3_HW_SETTLE_DELAY_MASK,
			    prop->common_props.hw_settle_time_us);

	buf[7] = 0;

	/* High temperature corresponds to low voltage threshold */
	prop->low_thr_en = (high_temp != INT_MAX);
	if (prop->low_thr_en) {
		adc_code = qcom_adc_tm5_gen2_temp_res_scale(high_temp);
		put_unaligned_le16(adc_code, &buf[8]);
		buf[7] |= ADC5_GEN3_LOW_THR_INT_EN;
	}

	/* Low temperature corresponds to high voltage threshold */
	prop->high_thr_en = (low_temp != -INT_MAX);
	if (prop->high_thr_en) {
		adc_code = qcom_adc_tm5_gen2_temp_res_scale(low_temp);
		put_unaligned_le16(adc_code, &buf[10]);
		buf[7] |= ADC5_GEN3_HIGH_THR_INT_EN;
	}

	ret = adc5_gen3_write(adc_tm5->dev_data, prop->sdam_index, ADC5_GEN3_SID,
			      buf, sizeof(buf));
	if (ret < 0)
		return ret;

	conv_req = ADC5_GEN3_CONV_REQ_REQ;
	return adc5_gen3_write(adc_tm5->dev_data, prop->sdam_index,
			       ADC5_GEN3_CONV_REQ, &conv_req, sizeof(conv_req));
}

static int adc_tm5_gen3_set_trip_temp(struct thermal_zone_device *tz,
				      int low_temp, int high_temp)
{
	struct adc_tm5_gen3_channel_props *prop = thermal_zone_device_priv(tz);
	struct adc_tm5_gen3_chip *adc_tm5;

	if (!prop || !prop->chip)
		return -EINVAL;

	adc_tm5 = prop->chip;

	dev_dbg(adc_tm5->dev, "channel:%s, low_temp(mdegC):%d, high_temp(mdegC):%d\n",
		prop->common_props.label, low_temp, high_temp);

	guard(adc5_gen3)(adc_tm5);

	return adc_tm5_gen3_configure(prop, low_temp, high_temp);
}

static const struct thermal_zone_device_ops adc_tm_ops = {
	.get_temp = adc_tm5_gen3_get_temp,
	.set_trips = adc_tm5_gen3_set_trip_temp,
};

static int adc_tm5_register_tzd(struct adc_tm5_gen3_chip *adc_tm5)
{
	struct thermal_zone_device *tzd;
	unsigned int channel;
	int ret;

	for (int i = 0; i < adc_tm5->nchannels; i++) {
		channel = ADC5_GEN3_V_CHAN(adc_tm5->chan_props[i].common_props);
		tzd = devm_thermal_of_zone_register(adc_tm5->dev, channel,
						    &adc_tm5->chan_props[i],
						    &adc_tm_ops);
		if (IS_ERR(tzd)) {
			if (PTR_ERR(tzd) == -ENODEV) {
				dev_dbg(adc_tm5->dev,
					"thermal sensor on channel %d is not used\n",
					channel);
				continue;
			}
			return PTR_ERR(tzd);
		}
		adc_tm5->chan_props[i].tzd = tzd;
		ret = devm_thermal_add_hwmon_sysfs(adc_tm5->dev, tzd);
		if (ret)
			return ret;
	}

	return 0;
}

static void adc5_gen3_disable(void *data)
{
	struct adc_tm5_gen3_chip *adc_tm5 = data;

	guard(adc5_gen3)(adc_tm5);

	/* Disable all available TM channels */
	for (int i = 0; i < adc_tm5->nchannels; i++)
		adc_tm5_gen3_disable_channel(&adc_tm5->chan_props[i]);
}

static int adc_tm5_probe(struct auxiliary_device *aux_dev,
			 const struct auxiliary_device_id *id)
{
	struct adc_tm5_gen3_chip *adc_tm5;
	struct tm5_aux_dev_wrapper *aux_dev_wrapper;
	struct device *dev = &aux_dev->dev;
	int ret;

	adc_tm5 = devm_kzalloc(dev, sizeof(*adc_tm5), GFP_KERNEL);
	if (!adc_tm5)
		return -ENOMEM;

	aux_dev_wrapper = container_of(aux_dev, struct tm5_aux_dev_wrapper, aux_dev);

	adc_tm5->dev = dev;
	adc_tm5->dev_data = aux_dev_wrapper->dev_data;
	adc_tm5->nchannels = aux_dev_wrapper->n_tm_channels;
	adc_tm5->chan_props = devm_kcalloc(dev, aux_dev_wrapper->n_tm_channels,
					   sizeof(*adc_tm5->chan_props), GFP_KERNEL);
	if (!adc_tm5->chan_props)
		return -ENOMEM;

	for (int i = 0; i < adc_tm5->nchannels; i++) {
		/*
		 * Since the first channel of the first SDAM is reserved for
		 * immediate ADC conversions, TM channel count must start from
		 * the channel just after it. The variable tm_count is used to
		 * calculate SDAM and TM channel index on that SDAM correctly
		 * for each TM channel.
		 */
		int tm_count = i + 1;

		adc_tm5->chan_props[i].common_props = aux_dev_wrapper->tm_props[i];
		adc_tm5->chan_props[i].timer = MEAS_INT_1S;
		adc_tm5->chan_props[i].sdam_index = tm_count / 8;
		adc_tm5->chan_props[i].tm_chan_index = tm_count % 8;
		adc_tm5->chan_props[i].chip = adc_tm5;
	}

	/*
	 * ADC_TM channels are enabled in the loop in adc_tm5_register_tzd() as
	 * part of the set_trips calls during thermal zone registration. This
	 * action is to disable them all in case of probe failure.
	 */
	ret = devm_add_action(dev, adc5_gen3_disable, adc_tm5);
	if (ret)
		return ret;

	ret = adc_tm5_register_tzd(adc_tm5);
	if (ret)
		return ret;

	for (int i = 0; i < adc_tm5->dev_data->num_sdams; i++) {
		u32 irq_flags = IRQF_ONESHOT;

		/*
		 * First SDAM's interrupt is shared between main ADC driver and
		 * auxiliary TM driver, so its flags must include IRQF_SHARED.
		 * This is not needed for other SDAMs as they will be used only
		 * for TM functionality.
		 */
		if (i == 0)
			irq_flags |= IRQF_SHARED;

		ret = devm_request_threaded_irq(dev,
						adc_tm5->dev_data->base[i].irq,
						adctm5_gen3_isr,
						adctm5_gen3_isr_thread,
						irq_flags,
						adc_tm5->dev_data->base[i].irq_name,
						adc_tm5);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static const struct auxiliary_device_id adctm5_auxiliary_id_table[] = {
	{ .name = "qcom_spmi_adc5_gen3.adc5_tm_gen3" },
	{ }
};
MODULE_DEVICE_TABLE(auxiliary, adctm5_auxiliary_id_table);

static struct auxiliary_driver adctm5gen3_auxiliary_driver = {
	.id_table = adctm5_auxiliary_id_table,
	.probe = adc_tm5_probe,
};
module_auxiliary_driver(adctm5gen3_auxiliary_driver);

MODULE_DESCRIPTION("SPMI PMIC Thermal Monitor ADC driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("QCOM_SPMI_ADC5_GEN3");
