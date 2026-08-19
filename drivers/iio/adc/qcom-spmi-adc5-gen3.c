// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/auxiliary_bus.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/completion.h>
#include <linux/container_of.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/device/devres.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/iio/adc/qcom-adc5-gen3-common.h>
#include <linux/iio/iio.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/types.h>
#include <linux/unaligned.h>

#define ADC5_GEN3_VADC_SDAM			0x0

struct adc5_chip;

/**
 * struct adc5_channel_prop - ADC channel structure
 * @common_props: structure with ADC channel properties (common to TM usage).
 * @adc_tm: indicates TM type if the channel is used for TM measurements.
 * @chip: pointer to top-level ADC device structure.
 */
struct adc5_channel_prop {
	struct adc5_channel_common_prop common_props;
	int adc_tm;
	struct adc5_chip *chip;
};

/**
 * struct adc5_chip - ADC private structure.
 * @dev: SPMI ADC5 Gen3 device.
 * @dev_data: Top-level ADC device data.
 * @nchannels: number of ADC channels.
 * @chan_props: array of ADC channel properties.
 * @iio_chans: array of IIO channels specification.
 * @complete: ADC result notification after interrupt is received.
 * @lock: ADC lock for access to the peripheral, to prevent concurrent
 *	requests from multiple clients.
 * @data: software configuration data.
 * @n_tm_channels: number of ADC channels used for TM measurements.
 * @handler: TM callback to be called for threshold violation interrupt
 *	on first SDAM.
 * @tm_aux: pointer to auxiliary TM device.
 */
struct adc5_chip {
	struct device *dev;
	struct adc5_device_data dev_data;
	unsigned int nchannels;
	struct adc5_channel_prop *chan_props;
	struct iio_chan_spec *iio_chans;
	struct completion complete;
	struct mutex lock;
	const struct adc5_data *data;
	unsigned int n_tm_channels;
	void (*handler)(struct auxiliary_device *tm_aux);
	struct auxiliary_device *tm_aux;
};

int adc5_gen3_read(struct adc5_device_data *adc, unsigned int sdam_index,
		   u16 offset, u8 *data, int len)
{
	return regmap_bulk_read(adc->regmap,
				adc->base[sdam_index].base_addr + offset,
				data, len);
}
EXPORT_SYMBOL_NS_GPL(adc5_gen3_read, "QCOM_SPMI_ADC5_GEN3");

int adc5_gen3_write(struct adc5_device_data *adc, unsigned int sdam_index,
		    u16 offset, u8 *data, int len)
{
	return regmap_bulk_write(adc->regmap,
				 adc->base[sdam_index].base_addr + offset,
				 data, len);
}
EXPORT_SYMBOL_NS_GPL(adc5_gen3_write, "QCOM_SPMI_ADC5_GEN3");

static int adc5_gen3_read_voltage_data(struct adc5_chip *adc, u16 *data)
{
	u8 rslt[2];
	int ret;

	ret = adc5_gen3_read(&adc->dev_data, ADC5_GEN3_VADC_SDAM,
			     ADC5_GEN3_CH_DATA0(0), rslt, sizeof(rslt));
	if (ret)
		return ret;

	*data = get_unaligned_le16(rslt);

	if (*data == ADC5_USR_DATA_CHECK) {
		dev_err(adc->dev, "Invalid data:%#x\n", *data);
		return -EINVAL;
	}

	dev_dbg(adc->dev, "voltage raw code:%#x\n", *data);

	return 0;
}

void adc5_gen3_update_dig_param(struct adc5_channel_common_prop *prop, u8 *data)
{
	/* Update calibration select and decimation ratio select */
	*data &= ~(ADC5_GEN3_DIG_PARAM_CAL_SEL_MASK | ADC5_GEN3_DIG_PARAM_DEC_RATIO_SEL_MASK);
	*data |= FIELD_PREP(ADC5_GEN3_DIG_PARAM_CAL_SEL_MASK, prop->cal_method);
	*data |= FIELD_PREP(ADC5_GEN3_DIG_PARAM_DEC_RATIO_SEL_MASK, prop->decimation);
}
EXPORT_SYMBOL_NS_GPL(adc5_gen3_update_dig_param, "QCOM_SPMI_ADC5_GEN3");

#define ADC5_GEN3_READ_CONFIG_REGS 7

static int adc5_gen3_configure(struct adc5_chip *adc,
			       struct adc5_channel_common_prop *prop)
{
	u8 buf[ADC5_GEN3_READ_CONFIG_REGS];
	u8 conv_req = 0;
	int ret;

	ret = adc5_gen3_read(&adc->dev_data, ADC5_GEN3_VADC_SDAM, ADC5_GEN3_SID,
			     buf, sizeof(buf));
	if (ret)
		return ret;

	/* Write SID */
	buf[0] = FIELD_PREP(ADC5_GEN3_SID_MASK, prop->sid);

	/*
	 * Use channel 0 by default for immediate conversion and to indicate
	 * there is an actual conversion request
	 */
	buf[1] = ADC5_GEN3_CHAN_CONV_REQ | 0;

	buf[2] = ADC5_GEN3_TIME_IMMEDIATE;

	/* Digital param selection */
	adc5_gen3_update_dig_param(prop, &buf[3]);

	/* Update fast average sample value */
	buf[4] = FIELD_PREP(ADC5_GEN3_FAST_AVG_CTL_SAMPLES_MASK,
			    prop->avg_samples) | ADC5_GEN3_FAST_AVG_CTL_EN;

	/* Select ADC channel */
	buf[5] = prop->channel;

	/* Select HW settle delay for channel */
	buf[6] = FIELD_PREP(ADC5_GEN3_HW_SETTLE_DELAY_MASK,
			    prop->hw_settle_time_us);

	reinit_completion(&adc->complete);

	ret = adc5_gen3_write(&adc->dev_data, ADC5_GEN3_VADC_SDAM, ADC5_GEN3_SID,
			      buf, sizeof(buf));
	if (ret)
		return ret;

	conv_req = ADC5_GEN3_CONV_REQ_REQ;
	return adc5_gen3_write(&adc->dev_data, ADC5_GEN3_VADC_SDAM,
			       ADC5_GEN3_CONV_REQ, &conv_req, sizeof(conv_req));
}

/*
 * Worst case delay from PBS in readying handshake bit  can be up to 15ms, when
 * PBS is busy running other simultaneous transactions, while in the best case,
 * it is already ready at this point. Assigning polling delay and retry count
 * accordingly.
 */

#define ADC5_GEN3_HS_DELAY_US			100
#define ADC5_GEN3_HS_RETRY_COUNT		150

int adc5_gen3_poll_wait_hs(struct adc5_device_data *adc,
			   unsigned int sdam_index)
{
	u8 conv_req = ADC5_GEN3_CONV_REQ_REQ;
	int ret, count;
	u8 status = 0;

	for (count = 0; count < ADC5_GEN3_HS_RETRY_COUNT; count++) {
		ret = adc5_gen3_read(adc, sdam_index, ADC5_GEN3_HS, &status, sizeof(status));
		if (ret)
			return ret;

		if (status == ADC5_GEN3_HS_READY) {
			ret = adc5_gen3_read(adc, sdam_index, ADC5_GEN3_CONV_REQ,
					     &conv_req, sizeof(conv_req));
			if (ret)
				return ret;

			if (!conv_req)
				return 0;
		}

		fsleep(ADC5_GEN3_HS_DELAY_US);
	}

	pr_err("Setting HS ready bit timed out, sdam_index:%d, status:%#x\n",
	       sdam_index, status);
	return -ETIMEDOUT;
}
EXPORT_SYMBOL_NS_GPL(adc5_gen3_poll_wait_hs, "QCOM_SPMI_ADC5_GEN3");

int adc5_gen3_status_clear(struct adc5_device_data *adc,
			   int sdam_index, u16 offset, u8 *val, int len)
{
	u8 value;
	int ret;

	ret = adc5_gen3_write(adc, sdam_index, offset, val, len);
	if (ret)
		return ret;

	/* To indicate conversion request is only to clear a status */
	value = 0;
	ret = adc5_gen3_write(adc, sdam_index, ADC5_GEN3_PERPH_CH, &value,
			      sizeof(value));
	if (ret)
		return ret;

	value = ADC5_GEN3_CONV_REQ_REQ;
	return adc5_gen3_write(adc, sdam_index, ADC5_GEN3_CONV_REQ, &value,
			      sizeof(value));
}
EXPORT_SYMBOL_NS_GPL(adc5_gen3_status_clear, "QCOM_SPMI_ADC5_GEN3");

/*
 * Worst case delay from PBS for conversion time can be up to 500ms, when PBS
 * has timed out twice, once for the initial attempt and once for a retry of
 * the same transaction.
 */

#define ADC5_GEN3_CONV_TIMEOUT_MS	501

static int adc5_gen3_do_conversion(struct adc5_chip *adc,
				   struct adc5_channel_common_prop *prop,
				   u16 *data_volt)
{
	unsigned long rc;
	int ret;
	u8 val;

	guard(mutex)(&adc->lock);
	ret = adc5_gen3_poll_wait_hs(&adc->dev_data, ADC5_GEN3_VADC_SDAM);
	if (ret)
		return ret;

	ret = adc5_gen3_configure(adc, prop);
	if (ret) {
		dev_err(adc->dev, "ADC configure failed with %d\n", ret);
		return ret;
	}

	/* No support for polling mode at present */
	rc = wait_for_completion_timeout(&adc->complete,
					 msecs_to_jiffies(ADC5_GEN3_CONV_TIMEOUT_MS));
	if (!rc) {
		dev_err(adc->dev, "Reading ADC channel %s timed out\n",
			prop->label);
		return -ETIMEDOUT;
	}

	ret = adc5_gen3_read_voltage_data(adc, data_volt);
	if (ret)
		return ret;

	val = BIT(0);
	return adc5_gen3_status_clear(&adc->dev_data, ADC5_GEN3_VADC_SDAM,
				      ADC5_GEN3_EOC_CLR, &val, 1);
}

static irqreturn_t adc5_gen3_isr(int irq, void *dev_id)
{
	struct adc5_chip *adc = dev_id;
	struct device *dev = adc->dev;
	struct auxiliary_device *adev;
	u8 status, eoc_status, val;
	u8 tm_status[2];
	int ret;

	ret = adc5_gen3_read(&adc->dev_data, ADC5_GEN3_VADC_SDAM,
			     ADC5_GEN3_STATUS1, &status, sizeof(status));
	if (ret) {
		dev_err(dev, "adc read status1 failed with %d\n", ret);
		return IRQ_HANDLED;
	}

	ret = adc5_gen3_read(&adc->dev_data, ADC5_GEN3_VADC_SDAM,
			     ADC5_GEN3_EOC_STS, &eoc_status, sizeof(eoc_status));
	if (ret) {
		dev_err(dev, "adc read eoc status failed with %d\n", ret);
		return IRQ_HANDLED;
	}

	if (status & ADC5_GEN3_STATUS1_CONV_FAULT) {
		dev_err_ratelimited(dev,
				    "Unexpected conversion fault, status:%#x, eoc_status:%#x\n",
				    status, eoc_status);
		val = ADC5_GEN3_CONV_ERR_CLR_REQ;
		adc5_gen3_status_clear(&adc->dev_data, ADC5_GEN3_VADC_SDAM,
				       ADC5_GEN3_CONV_ERR_CLR, &val, 1);
		return IRQ_HANDLED;
	}

	/* CHAN0 is the preconfigured channel for immediate conversion */
	if (eoc_status & ADC5_GEN3_EOC_CHAN_0)
		complete(&adc->complete);

	ret = adc5_gen3_read(&adc->dev_data, ADC5_GEN3_VADC_SDAM,
			     ADC5_GEN3_TM_HIGH_STS, tm_status, sizeof(tm_status));
	if (ret) {
		dev_err(dev, "adc read TM status failed with %d\n", ret);
		return IRQ_HANDLED;
	}

	dev_dbg(dev, "Interrupt status:%#x, EOC status:%#x, high:%#x, low:%#x\n",
		status, eoc_status, tm_status[0], tm_status[1]);

	if (tm_status[0] || tm_status[1]) {
		adev = adc->tm_aux;
		if (!adev || !adev->dev.driver) {
			dev_err(dev, "adc_tm auxiliary device not initialized\n");
			return IRQ_HANDLED;
		}

		adc->handler(adev);
	}

	return IRQ_HANDLED;
}

static int adc5_gen3_fwnode_xlate(struct iio_dev *indio_dev,
				  const struct fwnode_reference_args *iiospec)
{
	struct adc5_chip *adc = iio_priv(indio_dev);
	int i, v_channel;

	for (i = 0; i < adc->nchannels; i++) {
		v_channel = ADC5_GEN3_V_CHAN(adc->chan_props[i].common_props);
		if (v_channel == iiospec->args[0])
			return i;
	}

	return -ENOENT;
}

static int adc5_gen3_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan, int *val,
			      int *val2, long mask)
{
	struct adc5_chip *adc = iio_priv(indio_dev);
	struct adc5_channel_common_prop *prop;
	u16 adc_code_volt;
	int ret;

	prop = &adc->chan_props[chan->address].common_props;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		ret = adc5_gen3_do_conversion(adc, prop, &adc_code_volt);
		if (ret)
			return ret;

		ret = qcom_adc5_hw_scale(prop->scale_fn_type, prop->prescale,
					 adc->data, adc_code_volt, val);
		if (ret)
			return ret;

		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int adc5_gen3_read_label(struct iio_dev *indio_dev,
				const struct iio_chan_spec *chan, char *label)
{
	struct adc5_chip *adc = iio_priv(indio_dev);
	struct adc5_channel_prop *prop;

	prop = &adc->chan_props[chan->address];
	return sprintf(label, "%s\n", prop->common_props.label);
}

static const struct iio_info adc5_gen3_info = {
	.read_raw = adc5_gen3_read_raw,
	.read_label = adc5_gen3_read_label,
	.fwnode_xlate = adc5_gen3_fwnode_xlate,
};

struct adc5_channels {
	unsigned int prescale_index;
	enum iio_chan_type type;
	long info_mask;
	enum vadc_scale_fn_type scale_fn_type;
};

/* In these definitions, _pre refers to an index into adc5_prescale_ratios. */
#define ADC5_CHAN(_type, _mask, _pre, _scale)	\
	{						\
		.prescale_index = _pre,			\
		.type = _type,				\
		.info_mask = _mask,			\
		.scale_fn_type = _scale,		\
	},						\

#define ADC5_CHAN_TEMP(_pre, _scale)		\
	ADC5_CHAN(IIO_TEMP, BIT(IIO_CHAN_INFO_PROCESSED), _pre, _scale)	\

#define ADC5_CHAN_VOLT(_pre, _scale)		\
	ADC5_CHAN(IIO_VOLTAGE, BIT(IIO_CHAN_INFO_PROCESSED), _pre, _scale)	\

#define ADC5_CHAN_CUR(_pre, _scale)		\
	ADC5_CHAN(IIO_CURRENT, BIT(IIO_CHAN_INFO_PROCESSED), _pre, _scale)	\

static const struct adc5_channels adc5_gen3_chans_pmic[ADC5_MAX_CHANNEL] = {
	[ADC5_GEN3_REF_GND]		= ADC5_CHAN_VOLT(0, SCALE_HW_CALIB_DEFAULT)
	[ADC5_GEN3_1P25VREF]		= ADC5_CHAN_VOLT(0, SCALE_HW_CALIB_DEFAULT)
	[ADC5_GEN3_VPH_PWR]		= ADC5_CHAN_VOLT(1, SCALE_HW_CALIB_DEFAULT)
	[ADC5_GEN3_VBAT_SNS_QBG]	= ADC5_CHAN_VOLT(1, SCALE_HW_CALIB_DEFAULT)
	[ADC5_GEN3_USB_SNS_V_16]	= ADC5_CHAN_TEMP(8, SCALE_HW_CALIB_DEFAULT)
	[ADC5_GEN3_VIN_DIV16_MUX]	= ADC5_CHAN_TEMP(8, SCALE_HW_CALIB_DEFAULT)
	[ADC5_GEN3_DIE_TEMP]		= ADC5_CHAN_TEMP(0,
						SCALE_HW_CALIB_PMIC_THERM_PM7)
	[ADC5_GEN3_AMUX1_THM_100K_PU]	= ADC5_CHAN_TEMP(0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC5_GEN3_AMUX2_THM_100K_PU]	= ADC5_CHAN_TEMP(0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC5_GEN3_AMUX3_THM_100K_PU]	= ADC5_CHAN_TEMP(0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC5_GEN3_AMUX4_THM_100K_PU]	= ADC5_CHAN_TEMP(0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC5_GEN3_AMUX5_THM_100K_PU]	= ADC5_CHAN_TEMP(0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC5_GEN3_AMUX6_THM_100K_PU]	= ADC5_CHAN_TEMP(0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC5_GEN3_AMUX1_GPIO_100K_PU]	= ADC5_CHAN_TEMP(0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC5_GEN3_AMUX2_GPIO_100K_PU]	= ADC5_CHAN_TEMP(0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC5_GEN3_AMUX3_GPIO_100K_PU]	= ADC5_CHAN_TEMP(0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC5_GEN3_AMUX4_GPIO_100K_PU]	= ADC5_CHAN_TEMP(0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
};

static int adc5_gen3_get_fw_channel_data(struct adc5_chip *adc,
					 struct adc5_channel_prop *prop,
					 struct fwnode_handle *fwnode)
{
	const char *name = fwnode_get_name(fwnode);
	const struct adc5_data *data = adc->data;
	struct device *dev = adc->dev;
	const char *channel_name;
	u32 chan, value, sid;
	u32 varr[2];
	int ret;

	ret = fwnode_property_read_u32(fwnode, "reg", &chan);
	if (ret < 0)
		return dev_err_probe(dev, ret, "invalid channel number %s\n",
				     name);

	/*
	 * Value read from "reg" is virtual channel number
	 * virtual channel number = sid << 8 | channel number
	 */
	sid = FIELD_GET(ADC5_GEN3_VIRTUAL_SID_MASK, chan);
	chan = FIELD_GET(ADC5_GEN3_CHANNEL_MASK, chan);

	if (chan >= ADC5_MAX_CHANNEL)
		return dev_err_probe(dev, -EINVAL,
				     "%s invalid channel number %d\n",
				     name, chan);

	prop->common_props.channel = chan;
	prop->common_props.sid = sid;

	if (!adc->data->adc_chans[chan].info_mask)
		return dev_err_probe(dev, -EINVAL, "Channel %#x not supported\n", chan);

	channel_name = name;
	fwnode_property_read_string(fwnode, "label", &channel_name);
	prop->common_props.label = channel_name;

	value = data->decimation[ADC5_DECIMATION_DEFAULT];
	fwnode_property_read_u32(fwnode, "qcom,decimation", &value);
	ret = qcom_adc5_decimation_from_dt(value, data->decimation);
	if (ret < 0)
		return dev_err_probe(dev, ret, "%#x invalid decimation %d\n",
				     chan, value);
	prop->common_props.decimation = ret;

	prop->common_props.prescale = adc->data->adc_chans[chan].prescale_index;
	ret = fwnode_property_read_u32_array(fwnode, "qcom,pre-scaling", varr, 2);
	if (!ret) {
		ret = qcom_adc5_prescaling_from_dt(varr[0], varr[1]);
		if (ret < 0)
			return dev_err_probe(dev, ret,
					     "%#x invalid pre-scaling <%d %d>\n",
					     chan, varr[0], varr[1]);
		prop->common_props.prescale = ret;
	}

	value = data->hw_settle_1[VADC_DEF_HW_SETTLE_TIME];
	fwnode_property_read_u32(fwnode, "qcom,hw-settle-time", &value);
	ret = qcom_adc5_hw_settle_time_from_dt(value, data->hw_settle_1);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "%#x invalid hw-settle-time %d us\n",
				     chan, value);
	prop->common_props.hw_settle_time_us = ret;

	value = BIT(VADC_DEF_AVG_SAMPLES);
	fwnode_property_read_u32(fwnode, "qcom,avg-samples", &value);
	ret = qcom_adc5_avg_samples_from_dt(value);
	if (ret < 0)
		return dev_err_probe(dev, ret, "%#x invalid avg-samples %d\n",
				     chan, value);
	prop->common_props.avg_samples = ret;

	if (fwnode_property_read_bool(fwnode, "qcom,ratiometric"))
		prop->common_props.cal_method = ADC5_RATIOMETRIC_CAL;
	else
		prop->common_props.cal_method = ADC5_ABSOLUTE_CAL;

	prop->adc_tm = fwnode_property_read_bool(fwnode, "qcom,adc-tm");
	if (prop->adc_tm) {
		adc->n_tm_channels++;
		if (adc->n_tm_channels > (adc->dev_data.num_sdams * 8 - 1))
			return dev_err_probe(dev, -EINVAL,
					     "Number of TM nodes %u greater than channels supported:%u\n",
					     adc->n_tm_channels,
					     adc->dev_data.num_sdams * 8 - 1);
	}

	return 0;
}

static const struct adc5_data adc5_gen3_data_pmic = {
	.full_scale_code_volt = 0x70e4,
	.adc_chans = adc5_gen3_chans_pmic,
	.info = &adc5_gen3_info,
	.decimation = (unsigned int [ADC5_DECIMATION_SAMPLES_MAX])
			   { 85, 340, 1360 },
	.hw_settle_1 = (unsigned int [VADC_HW_SETTLE_SAMPLES_MAX])
			   { 15, 100, 200, 300,
			     400, 500, 600, 700,
			     1000, 2000, 4000, 8000,
			     16000, 32000, 64000, 128000 },
};

static const struct of_device_id adc5_match_table[] = {
	{
		.compatible = "qcom,spmi-adc5-gen3",
		.data = &adc5_gen3_data_pmic,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, adc5_match_table);

static int adc5_get_fw_data(struct adc5_chip *adc)
{
	const struct adc5_channels *adc_chan;
	struct adc5_channel_prop *chan_props;
	struct iio_chan_spec *iio_chan;
	struct device *dev = adc->dev;
	unsigned int index = 0;
	int ret;

	adc->nchannels = device_get_child_node_count(dev);
	if (!adc->nchannels)
		return dev_err_probe(dev, -EINVAL, "No ADC channels found\n");

	adc->iio_chans = devm_kcalloc(dev, adc->nchannels,
				      sizeof(*adc->iio_chans), GFP_KERNEL);
	if (!adc->iio_chans)
		return -ENOMEM;

	adc->chan_props = devm_kcalloc(dev, adc->nchannels,
				       sizeof(*adc->chan_props), GFP_KERNEL);
	if (!adc->chan_props)
		return -ENOMEM;

	chan_props = adc->chan_props;
	adc->n_tm_channels = 0;
	iio_chan = adc->iio_chans;
	adc->data = device_get_match_data(dev);

	device_for_each_child_node_scoped(dev, child) {
		ret = adc5_gen3_get_fw_channel_data(adc, chan_props, child);
		if (ret)
			return ret;

		chan_props->chip = adc;
		adc_chan = &adc->data->adc_chans[chan_props->common_props.channel];
		chan_props->common_props.scale_fn_type = adc_chan->scale_fn_type;

		iio_chan->channel = ADC5_GEN3_V_CHAN(chan_props->common_props);
		iio_chan->info_mask_separate = adc_chan->info_mask;
		iio_chan->type = adc_chan->type;
		iio_chan->address = index;
		iio_chan->indexed = 1;
		iio_chan++;
		chan_props++;
		index++;
	}

	return 0;
}

static void adc5_gen3_uninit_aux(void *data)
{
	auxiliary_device_uninit(data);
}

static void adc5_gen3_delete_aux(void *data)
{
	auxiliary_device_delete(data);
}

static void adc5_gen3_aux_device_release(struct device *dev) {}

static int adc5_gen3_add_aux_tm_device(struct adc5_chip *adc)
{
	struct tm5_aux_dev_wrapper *aux_device;
	int i, ret, i_tm = 0;

	aux_device = devm_kzalloc(adc->dev, sizeof(*aux_device), GFP_KERNEL);
	if (!aux_device)
		return -ENOMEM;

	aux_device->aux_dev.name = "adc5_tm_gen3";
	aux_device->aux_dev.dev.parent = adc->dev;
	aux_device->aux_dev.dev.release = adc5_gen3_aux_device_release;

	aux_device->tm_props = devm_kcalloc(adc->dev, adc->n_tm_channels,
					    sizeof(*aux_device->tm_props),
					    GFP_KERNEL);
	if (!aux_device->tm_props)
		return -ENOMEM;

	aux_device->dev_data = &adc->dev_data;

	for (i = 0; i < adc->nchannels; i++) {
		if (!adc->chan_props[i].adc_tm)
			continue;
		aux_device->tm_props[i_tm] = adc->chan_props[i].common_props;
		i_tm++;
	}

	device_set_of_node_from_dev(&aux_device->aux_dev.dev, adc->dev);

	aux_device->n_tm_channels = adc->n_tm_channels;

	ret = auxiliary_device_init(&aux_device->aux_dev);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(adc->dev, adc5_gen3_uninit_aux,
				       &aux_device->aux_dev);
	if (ret)
		return ret;

	ret = auxiliary_device_add(&aux_device->aux_dev);
	if (ret)
		return ret;
	ret = devm_add_action_or_reset(adc->dev, adc5_gen3_delete_aux,
				       &aux_device->aux_dev);
	if (ret)
		return ret;

	adc->tm_aux = &aux_device->aux_dev;

	return 0;
}

void adc5_gen3_mutex_lock(struct device *dev)
	__acquires(&adc->lock)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev->parent);
	struct adc5_chip *adc = iio_priv(indio_dev);

	mutex_lock(&adc->lock);
}
EXPORT_SYMBOL_NS_GPL(adc5_gen3_mutex_lock, "QCOM_SPMI_ADC5_GEN3");

void adc5_gen3_mutex_unlock(struct device *dev)
	__releases(&adc->lock)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev->parent);
	struct adc5_chip *adc = iio_priv(indio_dev);

	mutex_unlock(&adc->lock);
}
EXPORT_SYMBOL_NS_GPL(adc5_gen3_mutex_unlock, "QCOM_SPMI_ADC5_GEN3");

int adc5_gen3_get_scaled_reading(struct device *dev,
				 struct adc5_channel_common_prop *common_props,
				 int *val)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev->parent);
	struct adc5_chip *adc = iio_priv(indio_dev);
	u16 adc_code_volt;
	int ret;

	ret = adc5_gen3_do_conversion(adc, common_props, &adc_code_volt);
	if (ret)
		return ret;

	return qcom_adc5_hw_scale(common_props->scale_fn_type,
				  common_props->prescale,
				  adc->data, adc_code_volt, val);
}
EXPORT_SYMBOL_NS_GPL(adc5_gen3_get_scaled_reading, "QCOM_SPMI_ADC5_GEN3");

int adc5_gen3_therm_code_to_temp(struct device *dev,
				 struct adc5_channel_common_prop *common_props,
				 u16 code, int *val)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev->parent);
	struct adc5_chip *adc = iio_priv(indio_dev);

	return qcom_adc5_hw_scale(common_props->scale_fn_type,
				  common_props->prescale,
				  adc->data, code, val);
}
EXPORT_SYMBOL_NS_GPL(adc5_gen3_therm_code_to_temp, "QCOM_SPMI_ADC5_GEN3");

void adc5_gen3_register_tm_event_notifier(struct device *dev,
					  void (*handler)(struct auxiliary_device *))
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev->parent);
	struct adc5_chip *adc = iio_priv(indio_dev);

	adc->handler = handler;
}
EXPORT_SYMBOL_NS_GPL(adc5_gen3_register_tm_event_notifier, "QCOM_SPMI_ADC5_GEN3");

static int adc5_gen3_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iio_dev *indio_dev;
	struct adc5_chip *adc;
	struct regmap *regmap;
	int ret, i;
	u32 *reg;

	regmap = dev_get_regmap(dev->parent, NULL);
	if (!regmap)
		return -ENODEV;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->dev_data.regmap = regmap;
	adc->dev = dev;

	ret = device_property_count_u32(dev, "reg");
	if (ret < 0)
		return ret;

	adc->dev_data.num_sdams = ret;

	reg = devm_kcalloc(dev, adc->dev_data.num_sdams, sizeof(u32),
			   GFP_KERNEL);
	if (!reg)
		return -ENOMEM;

	ret = device_property_read_u32_array(dev, "reg", reg,
					     adc->dev_data.num_sdams);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to read reg property\n");

	adc->dev_data.base = devm_kcalloc(dev, adc->dev_data.num_sdams,
					  sizeof(*adc->dev_data.base),
					  GFP_KERNEL);
	if (!adc->dev_data.base)
		return -ENOMEM;

	platform_set_drvdata(pdev, indio_dev);
	init_completion(&adc->complete);
	ret = devm_mutex_init(dev, &adc->lock);
	if (ret)
		return ret;

	for (i = 0; i < adc->dev_data.num_sdams; i++) {
		adc->dev_data.base[i].base_addr = reg[i];

		ret = platform_get_irq(pdev, i);
		if (ret < 0)
			return dev_err_probe(dev, ret,
					     "Getting IRQ %d failed\n", i);

		adc->dev_data.base[i].irq = ret;

		adc->dev_data.base[i].irq_name = devm_kasprintf(dev, GFP_KERNEL,
								"sdam%d", i);
		if (!adc->dev_data.base[i].irq_name)
			return -ENOMEM;
	}

	ret = devm_request_irq(dev, adc->dev_data.base[ADC5_GEN3_VADC_SDAM].irq,
			       adc5_gen3_isr, 0,
			       adc->dev_data.base[ADC5_GEN3_VADC_SDAM].irq_name,
			       adc);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to request SDAM%d irq\n",
				     ADC5_GEN3_VADC_SDAM);

	ret = adc5_get_fw_data(adc);
	if (ret)
		return ret;

	if (adc->n_tm_channels > 0) {
		ret = adc5_gen3_add_aux_tm_device(adc);
		if (ret)
			dev_err_probe(dev, ret,
				      "Failed to add auxiliary TM device\n");
	}

	indio_dev->name = "spmi-adc5-gen3";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &adc5_gen3_info;
	indio_dev->channels = adc->iio_chans;
	indio_dev->num_channels = adc->nchannels;

	return devm_iio_device_register(dev, indio_dev);
}

static struct platform_driver adc5_gen3_driver = {
	.driver = {
		.name = "qcom-spmi-adc5-gen3",
		.of_match_table = adc5_match_table,
	},
	.probe = adc5_gen3_probe,
};
module_platform_driver(adc5_gen3_driver);

MODULE_DESCRIPTION("Qualcomm Technologies Inc. PMIC5 Gen3 ADC driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("QCOM_SPMI_ADC5_GEN3");
