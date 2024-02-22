// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/iio/adc/qcom-vadc-common.h>
#include <linux/iio/iio.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <dt-bindings/iio/qcom,spmi-vadc.h>

#define ADC5_USR_REVISION1			0x0
#define ADC5_USR_STATUS1			0x8
#define ADC5_USR_STATUS1_CONV_FAULT		BIT(7)
#define ADC5_USR_STATUS1_REQ_STS		BIT(1)
#define ADC5_USR_STATUS1_EOC			BIT(0)
#define ADC5_USR_STATUS1_REQ_STS_EOC_MASK	0x3

#define ADC5_USR_STATUS2			0x9
#define ADC5_USR_STATUS2_CONV_SEQ_MASK		0x70
#define ADC5_USR_STATUS2_CONV_SEQ_MASK_SHIFT	0x5

#define ADC5_USR_IBAT_MEAS			0xf
#define ADC5_USR_IBAT_MEAS_SUPPORTED		BIT(0)

#define ADC5_USR_DIG_PARAM			0x42
#define ADC5_USR_DIG_PARAM_CAL_VAL		BIT(6)
#define ADC5_USR_DIG_PARAM_CAL_VAL_SHIFT	6
#define ADC5_USR_DIG_PARAM_CAL_SEL		0x30
#define ADC5_USR_DIG_PARAM_CAL_SEL_SHIFT	4
#define ADC5_USR_DIG_PARAM_DEC_RATIO_SEL	0xc
#define ADC5_USR_DIG_PARAM_DEC_RATIO_SEL_SHIFT	2

#define ADC5_USR_FAST_AVG_CTL			0x43
#define ADC5_USR_FAST_AVG_CTL_EN		BIT(7)
#define ADC5_USR_FAST_AVG_CTL_SAMPLES_MASK	0x7

#define ADC5_USR_CH_SEL_CTL			0x44

#define ADC5_USR_DELAY_CTL			0x45
#define ADC5_USR_HW_SETTLE_DELAY_MASK		0xf

#define ADC5_USR_EN_CTL1			0x46
#define ADC5_USR_EN_CTL1_ADC_EN			BIT(7)

#define ADC5_USR_CONV_REQ			0x47
#define ADC5_USR_CONV_REQ_REQ			BIT(7)

#define ADC5_USR_DATA0				0x50

#define ADC5_USR_DATA1				0x51

#define ADC5_USR_IBAT_DATA0			0x52

#define ADC5_USR_IBAT_DATA1			0x53

#define ADC_CHANNEL_OFFSET			0x8
#define ADC_CHANNEL_MASK			GENMASK(7, 0)

/*
 * Conversion time varies based on the decimation, clock rate, fast average
 * samples and measurements queued across different VADC peripherals.
 * Set the timeout to a max of 100ms.
 */
#define ADC5_POLL_DELAY_MIN_US			10000
#define ADC5_POLL_DELAY_MAX_US			10001
#define ADC5_CONV_TIME_RETRY_POLL		40
#define ADC5_CONV_TIME_RETRY			30
#define ADC5_CONV_TIMEOUT			msecs_to_jiffies(100)

/* Digital version >= 5.3 supports hw_settle_2 */
#define ADC5_HW_SETTLE_DIFF_MINOR		3
#define ADC5_HW_SETTLE_DIFF_MAJOR		5

/* ADC_PBS for ADC device shared between multiple subsystems */
#define ADC5_PBS_EN_CTL				0x46
#define ADC5_ENABLE				BIT(7)

/* For PMIC7 */
#define ADC_APP_SID				0x40
#define ADC_APP_SID_MASK			GENMASK(3, 0)
#define ADC7_CONV_TIMEOUT_MS			501

/* For ADC_PBS on PMIC7 with SW calibration */
#define ADC7_SW_CALIB_PBS_CALREF_FLAG		0x57
#define ADC7_SW_CALIB_PBS_CALREF_RDY		BIT(7)

#define ADC7_SW_CALIB_PBS_GND_REF_D0		0x58
#define ADC7_SW_CALIB_PBS_GND_REF_D1		0x59
#define ADC7_SW_CALIB_PBS_VREF_VADC_DELTA_D0	0x5a
#define ADC7_SW_CALIB_PBS_VREF_VADC_DELTA_D1	0x5b
#define ADC7_SW_CALIB_PBS_VREF_MBG_DELTA_D0	0x5c
#define ADC7_SW_CALIB_PBS_VREF_MBG_DELTA_D1	0x5d

/* For ADC_CMN on PMIC7 with SW calibration  */
#define ADC7_SW_CALIB_CMN_PBUS_WRITE_SYNC_CTL	0x4e
#define ADC7_PBUS_WRITE_SYNC_SW_CLK_REQ		BIT(2)
#define ADC7_PBUS_WRITE_SYNC_SW_CLK_REQ_MODE	BIT(1)
#define ADC7_PBUS_WRITE_SYNC_BYPASS		BIT(0)

enum adc5_cal_method {
	ADC5_NO_CAL = 0,
	ADC5_RATIOMETRIC_CAL,
	ADC5_ABSOLUTE_CAL
};

enum adc5_cal_val {
	ADC5_TIMER_CAL = 0,
	ADC5_NEW_CAL
};

/**
 * struct adc5_channel_prop - ADC channel property.
 * @channel: channel number, refer to the channel list.
 * @cal_method: calibration method.
 * @cal_val: calibration value
 * @decimation: sampling rate supported for the channel.
 * @sid: slave id of PMIC owning the channel, for PMIC7.
 * @prescale: channel scaling performed on the input signal.
 * @hw_settle_time: the time between AMUX being configured and the
 *	start of conversion.
 * @avg_samples: ability to provide single result from the ADC
 *	that is an average of multiple measurements.
 * @scale_fn_type: Represents the scaling function to convert voltage
 *	physical units desired by the client for the channel.
 * @datasheet_name: Channel name used in device tree.
 */
struct adc5_channel_prop {
	unsigned int		channel;
	enum adc5_cal_method	cal_method;
	enum adc5_cal_val	cal_val;
	unsigned int		decimation;
	unsigned int		sid;
	unsigned int		prescale;
	unsigned int		hw_settle_time;
	unsigned int		avg_samples;
	enum vadc_scale_fn_type	scale_fn_type;
	const char		*datasheet_name;
};

/**
 * struct adc5_chip - ADC private structure.
 * @regmap: SPMI ADC5 peripheral register map field.
 * @dev: SPMI ADC5 device.
 * @base: base address for the ADC peripheral.
 * @cmn_base: base address for the ADC_CMN peripheral, needed
 *	for SW calibrated ADC.
 * @nchannels: number of ADC channels.
 * @chan_props: array of ADC channel properties.
 * @iio_chans: array of IIO channels specification.
 * @poll_eoc: use polling instead of interrupt.
 * @complete: ADC result notification after interrupt is received.
 * @lock: ADC lock for access to the peripheral.
 * @data: software configuration data.
 */
struct adc5_chip {
	struct regmap		*regmap;
	struct device		*dev;
	u16			base;
	u16			cmn_base;
	unsigned int		nchannels;
	struct adc5_channel_prop	*chan_props;
	struct iio_chan_spec	*iio_chans;
	/* Below three properties are for shared_adc_peripheral */
	int			adc5_retry_acquire_adc_count;
	int			adc5_retry_after_read_count;
	u8			adc_config[5];

	bool			poll_eoc;
	struct completion	complete;
	struct mutex		lock;
	const struct adc5_data	*data;
	int			irq_eoc;
};

static int adc5_read(struct adc5_chip *adc, u16 offset, u8 *data, int len)
{
	int ret;

	ret = regmap_bulk_read(adc->regmap, adc->base + offset, data, len);
	if (ret)
		pr_err("adc read to register %#x of length:%d failed, ret=%d\n",
			offset, len, ret);

	return ret;
}

static int adc5_write(struct adc5_chip *adc, u16 offset, u8 *data, int len)
{
	int ret;

	ret = regmap_bulk_write(adc->regmap, adc->base + offset, data, len);
	if (ret)
		pr_err("adc write to register %#x of length:%d failed, ret=%d\n",
			offset, len, ret);

	return ret;
}

static int adc5_masked_write(struct adc5_chip *adc, u16 offset, u8 mask, u8 val)
{
	int ret;

	ret = regmap_update_bits(adc->regmap, adc->base + offset, mask, val);
	if (ret)
		pr_err("adc masked write to register %#x with mask:0x%x failed, ret=%d\n",
			offset, mask, ret);

	return ret;
}

static int adc5_cmn_write(struct adc5_chip *adc, u16 offset, u8 *data, int len)
{
	int ret;

	ret = regmap_bulk_write(adc->regmap, adc->cmn_base + offset, data, len);
	if (ret)
		pr_err("adc_cmn write to register %#x of length:%d failed, ret=%d\n",
			offset, len, ret);

	return ret;
}

static int adc5_read_voltage_data(struct adc5_chip *adc, u16 *data)
{
	int ret;
	u8 rslt_lsb, rslt_msb;

	ret = adc5_read(adc, ADC5_USR_DATA0, &rslt_lsb, 1);
	if (ret)
		return ret;

	ret = adc5_read(adc, ADC5_USR_DATA1, &rslt_msb, 1);
	if (ret)
		return ret;

	*data = (rslt_msb << 8) | rslt_lsb;

	if (*data == ADC5_USR_DATA_CHECK) {
		dev_err(adc->dev, "Invalid data:0x%x\n", *data);
		return -EINVAL;
	}

	dev_dbg(adc->dev, "voltage raw code:0x%x\n", *data);

	return 0;
}

static int adc5_poll_wait_eoc(struct adc5_chip *adc, bool poll_only)
{
	unsigned int count, retry = ADC5_CONV_TIME_RETRY;
	u8 status1;
	int ret;

	if (poll_only)
		retry = ADC5_CONV_TIME_RETRY_POLL;
	else
		retry = ADC5_CONV_TIME_RETRY;

	for (count = 0; count < retry; count++) {
		ret = adc5_read(adc, ADC5_USR_STATUS1, &status1,
							sizeof(status1));
		if (ret)
			return ret;

		status1 &= ADC5_USR_STATUS1_REQ_STS_EOC_MASK;
		if (status1 == ADC5_USR_STATUS1_EOC)
			return 0;

		usleep_range(ADC5_POLL_DELAY_MIN_US, ADC5_POLL_DELAY_MAX_US);
	}

	return -ETIMEDOUT;
}

static void adc5_update_dig_param(struct adc5_chip *adc,
			struct adc5_channel_prop *prop, u8 *data)
{
	/* Update calibration value */
	*data &= ~ADC5_USR_DIG_PARAM_CAL_VAL;
	*data |= (prop->cal_val << ADC5_USR_DIG_PARAM_CAL_VAL_SHIFT);

	/* Update calibration select */
	*data &= ~ADC5_USR_DIG_PARAM_CAL_SEL;
	*data |= (prop->cal_method << ADC5_USR_DIG_PARAM_CAL_SEL_SHIFT);

	/* Update decimation ratio select */
	*data &= ~ADC5_USR_DIG_PARAM_DEC_RATIO_SEL;
	*data |= (prop->decimation << ADC5_USR_DIG_PARAM_DEC_RATIO_SEL_SHIFT);
}

#define ADC5_WAIT_FOR_PERIPHERAL_MIN_US		3000
#define ADC5_WAIT_FOR_PERIPHERAL_MAX_US		4000

static int adc5_shared_try_enable(struct adc5_chip *adc)
{
	int ret = 0;
	u8 rslt;

	ret = adc5_read(adc, ADC5_PBS_EN_CTL, &rslt, 1);
	if (ret) {
		pr_err("Failed to read ADC5_PBS_EN_CTL register = %d\n", ret);
		return ret;
	}

	if (rslt & ADC5_ENABLE) {
		/*
		 * If ADC peripheral is used by other subsystems,
		 * wait for 3-4ms before retrying.
		 */
		usleep_range(ADC5_WAIT_FOR_PERIPHERAL_MIN_US,
				ADC5_WAIT_FOR_PERIPHERAL_MAX_US);
		pr_debug("ADC peripheral is being used by other subsystem\n");
		return -EBUSY;
	}

	rslt = ADC5_ENABLE;
	ret = adc5_write(adc, ADC5_PBS_EN_CTL, &rslt, sizeof(rslt));
	if (ret)
		pr_err("Failed to write to ADC5_PBS_EN_CTL register = %d\n", ret);

	return ret;
}

static int adc5_configure(struct adc5_chip *adc,
			struct adc5_channel_prop *prop)
{
	int ret;
	u8 buf[6];

	/* Read registers 0x42 through 0x46 */
	ret = adc5_read(adc, ADC5_USR_DIG_PARAM, buf, sizeof(buf));
	if (ret)
		return ret;

	/* Digital param selection */
	adc5_update_dig_param(adc, prop, &buf[0]);

	/* Update fast average sample value */
	buf[1] &= (u8) ~ADC5_USR_FAST_AVG_CTL_SAMPLES_MASK;
	buf[1] |= prop->avg_samples;

	/* Select ADC channel */
	buf[2] = prop->channel;

	/* Select HW settle delay for channel */
	buf[3] &= (u8) ~ADC5_USR_HW_SETTLE_DELAY_MASK;
	buf[3] |= prop->hw_settle_time;

	/* Select ADC enable */
	buf[4] |= ADC5_USR_EN_CTL1_ADC_EN;

	/* Select CONV request */
	buf[5] |= ADC5_USR_CONV_REQ_REQ;

	if (!adc->poll_eoc)
		reinit_completion(&adc->complete);

	if (!strcmp(adc->data->name, "pm-adc5-shared"))
		memcpy(adc->adc_config, buf, sizeof(adc->adc_config));

	return adc5_write(adc, ADC5_USR_DIG_PARAM, buf, sizeof(buf));
}

static int adc7_configure(struct adc5_chip *adc,
			struct adc5_channel_prop *prop)
{
	int ret;
	u8 conv_req = 0, buf[4];

	ret = adc5_masked_write(adc, ADC_APP_SID, ADC_APP_SID_MASK, prop->sid);
	if (ret)
		return ret;

	ret = adc5_read(adc, ADC5_USR_DIG_PARAM, buf, sizeof(buf));
	if (ret)
		return ret;

	/* Digital param selection */
	adc5_update_dig_param(adc, prop, &buf[0]);

	/* Update fast average sample value */
	buf[1] &= ~ADC5_USR_FAST_AVG_CTL_SAMPLES_MASK;
	buf[1] |= prop->avg_samples;

	/* Select ADC channel */
	buf[2] = prop->channel;

	/* Select HW settle delay for channel */
	buf[3] &= ~ADC5_USR_HW_SETTLE_DELAY_MASK;
	buf[3] |= prop->hw_settle_time;

	/* Select CONV request */
	conv_req = ADC5_USR_CONV_REQ_REQ;

	if (!adc->poll_eoc)
		reinit_completion(&adc->complete);

	ret = adc5_write(adc, ADC5_USR_DIG_PARAM, buf, sizeof(buf));
	if (ret)
		return ret;

	return adc5_write(adc, ADC5_USR_CONV_REQ, &conv_req, 1);
}

static int adc7_sw_calib_configure(struct adc5_chip *adc,
			struct adc5_channel_prop *prop)
{
	int ret;
	u8 buf[5], val = 0;

	/* Read registers 0x42 through 0x46 */
	ret = adc5_read(adc, ADC5_USR_DIG_PARAM, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	/* Digital param selection */
	adc5_update_dig_param(adc, prop, &buf[0]);

	/* Update fast average sample value */
	buf[1] &= (u8) ~ADC5_USR_FAST_AVG_CTL_SAMPLES_MASK;
	buf[1] |= prop->avg_samples | ADC5_USR_FAST_AVG_CTL_EN;

	/* Select ADC channel */
	buf[2] = prop->channel;

	/* Select HW settle delay for channel */
	buf[3] &= (u8) ~ADC5_USR_HW_SETTLE_DELAY_MASK;
	buf[3] |= prop->hw_settle_time;

	/* Select ADC enable */
	buf[4] |= ADC5_USR_EN_CTL1_ADC_EN;

	if (!adc->poll_eoc)
		reinit_completion(&adc->complete);

	ret = adc5_write(adc, ADC5_USR_DIG_PARAM, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	val = ADC7_PBUS_WRITE_SYNC_SW_CLK_REQ | ADC7_PBUS_WRITE_SYNC_SW_CLK_REQ_MODE;

	ret = adc5_cmn_write(adc, ADC7_SW_CALIB_CMN_PBUS_WRITE_SYNC_CTL, &val, 1);
	if (ret < 0)
		return ret;

	/* Select CONV request */
	val = ADC5_USR_CONV_REQ_REQ;
	return adc5_write(adc, ADC5_USR_CONV_REQ, &val, 1);
}

#define MAX_RETRY_COUNT		3
#define MAX_COUNTER		5

static int adc5_shared_do_conversion(struct adc5_chip *adc,
			struct adc5_channel_prop *prop,
			struct iio_chan_spec const *chan,
			u16 *data_volt, u16 *data_cur)
{
	int ret, i;
	u8 buf[MAX_COUNTER], rslt;

	mutex_lock(&adc->lock);

	while (adc->adc5_retry_after_read_count < MAX_RETRY_COUNT) {
		while (adc->adc5_retry_acquire_adc_count < MAX_RETRY_COUNT) {
			dev_dbg(adc->dev, "Attempts to acquire ADC: %d\n",
					adc->adc5_retry_acquire_adc_count);
			ret = adc5_shared_try_enable(adc);
			if (ret) {
				if (ret != -EBUSY)
					goto unlock;
				else
					adc->adc5_retry_acquire_adc_count++;
			} else {
				adc->adc5_retry_acquire_adc_count = 0;
				break;
			}
		}

		if (adc->adc5_retry_acquire_adc_count == MAX_RETRY_COUNT ||
				adc->adc5_retry_after_read_count == MAX_RETRY_COUNT) {
			ret = -EBUSY;
			goto unlock;
		}

		ret = adc5_configure(adc, prop);
		if (ret) {
			dev_err(adc->dev, "ADC configure failed with %d\n", ret);
			goto unlock;
		}

		ret = wait_for_completion_timeout(&adc->complete,
						ADC5_CONV_TIMEOUT);
		if (!ret) {
			dev_dbg(adc->dev, "Did not get completion timeout\n");
			ret = adc5_poll_wait_eoc(adc, false);
			if (ret) {
				dev_err(adc->dev, "EOC bit not set\n");
				goto unlock;
			}
		}

		ret = adc5_read(adc, ADC5_USR_DIG_PARAM, buf, sizeof(buf));
		if (ret)
			goto unlock;

		if (!(buf[4] & ADC5_ENABLE)) {
			dev_err(adc->dev, "Concurrency with other subsystems, try again\n");
			adc->adc5_retry_after_read_count++;
			continue;
		}

		for (i = 0; i < MAX_COUNTER; i++) {
			if (!(adc->adc_config[i] == buf[i])) {
				dev_err(adc->dev, "adc-config registers mismatch, try again\n");
				adc->adc5_retry_after_read_count++;
				break;
			}
		}

		if (i < MAX_COUNTER)
			continue;

		ret = adc5_read_voltage_data(adc, data_volt);
		if (*data_volt != ADC5_USR_DATA_CHECK)
			break;

		adc->adc5_retry_after_read_count++;
	}

unlock:
	adc->adc5_retry_after_read_count = 0;
	adc->adc5_retry_acquire_adc_count = 0;
	rslt = 0;
	ret = adc5_write(adc, ADC5_PBS_EN_CTL, &rslt, sizeof(rslt));
	if (ret < 0)
		dev_err(adc->dev, "Failed to disable shared adc peripheral\n");

	mutex_unlock(&adc->lock);

	return ret;
}

static int adc5_do_conversion(struct adc5_chip *adc,
			struct adc5_channel_prop *prop,
			struct iio_chan_spec const *chan,
			u16 *data_volt, u16 *data_cur)
{
	int ret;

	mutex_lock(&adc->lock);

	ret = adc5_configure(adc, prop);
	if (ret) {
		dev_err(adc->dev, "ADC configure failed with %d\n", ret);
		goto unlock;
	}

	if (adc->poll_eoc) {
		ret = adc5_poll_wait_eoc(adc, true);
		if (ret) {
			dev_err(adc->dev, "EOC bit not set\n");
			goto unlock;
		}
	} else {
		ret = wait_for_completion_timeout(&adc->complete,
							ADC5_CONV_TIMEOUT);
		if (!ret) {
			dev_dbg(adc->dev, "Did not get completion timeout.\n");
			ret = adc5_poll_wait_eoc(adc, false);
			if (ret) {
				dev_err(adc->dev, "EOC bit not set\n");
				goto unlock;
			}
		}
	}

	ret = adc5_read_voltage_data(adc, data_volt);
unlock:
	mutex_unlock(&adc->lock);

	return ret;
}

static int adc7_do_conversion(struct adc5_chip *adc,
			struct adc5_channel_prop *prop,
			struct iio_chan_spec const *chan,
			u16 *data_volt, u16 *data_cur)
{
	int ret;
	u8 status;
	unsigned long rc;
	unsigned int time_pending_ms;

	mutex_lock(&adc->lock);

	ret = adc7_configure(adc, prop);
	if (ret) {
		dev_err(adc->dev, "ADC configure failed with %d\n", ret);
		goto unlock;
	}

	/* No support for polling mode at present */
	rc = wait_for_completion_timeout(&adc->complete,
					msecs_to_jiffies(ADC7_CONV_TIMEOUT_MS));
	if (!rc) {
		dev_err(adc->dev, "Reading ADC channel %s timed out\n",
			prop->datasheet_name);
		ret = -ETIMEDOUT;
		goto unlock;
	}

	/*
	 * As per the hardware documentation, EOC should happen within 15 ms
	 * in a good case where there could be multiple conversion requests
	 * going through PMIC HW arbiter for reading ADC channels. However, if
	 * for some reason, one of the conversion request fails and times out,
	 * worst possible delay can be 500 ms. Hence print a warning when we
	 * see EOC completion happened more than 15 ms.
	 */
	time_pending_ms = jiffies_to_msecs(rc);
	if (time_pending_ms < ADC7_CONV_TIMEOUT_MS &&
	    (ADC7_CONV_TIMEOUT_MS - time_pending_ms) > 15)
		dev_warn(adc->dev, "ADC channel %s EOC took %u ms\n",
			prop->datasheet_name,
			ADC7_CONV_TIMEOUT_MS - time_pending_ms);

	ret = adc5_read(adc, ADC5_USR_STATUS1, &status, 1);
	if (ret)
		goto unlock;

	if (status & ADC5_USR_STATUS1_CONV_FAULT) {
		dev_err(adc->dev, "ADC channel %s unexpected conversion fault\n",
			prop->datasheet_name);
		ret = -EIO;
		goto unlock;
	}

	if (!(status & ADC5_USR_STATUS1_EOC)) {
		dev_err(adc->dev, "ADC channel %s EOC bit not set, status=%#x\n",
			prop->datasheet_name, status);
		ret = -EIO;
		goto unlock;
	}

	ret = adc5_read_voltage_data(adc, data_volt);

unlock:
	mutex_unlock(&adc->lock);

	return ret;
}

#define ADC7_SW_CALIB_CONV_TIMEOUT_MS			150
static int adc7_sw_calib_do_conversion(struct adc5_chip *adc,
			struct adc5_channel_prop *prop, u16 *adc_code_volt)
{
	int ret;
	unsigned long rc;
	u8 status = 0, val;

	mutex_lock(&adc->lock);

	ret = adc7_sw_calib_configure(adc, prop);
	if (ret) {
		pr_err("ADC configure failed with %d\n", ret);
		goto unlock;
	}

	/* No support for polling mode at present*/
	rc = wait_for_completion_timeout(&adc->complete,
					msecs_to_jiffies(ADC7_SW_CALIB_CONV_TIMEOUT_MS));
	if (!rc) {
		pr_err("Reading ADC channel %s timed out\n",
			prop->datasheet_name);
		ret = -ETIMEDOUT;
		goto unlock;
	}

	ret = adc5_read(adc, ADC5_USR_STATUS1, &status, 1);
	if (ret < 0)
		goto unlock;

	if (!(status & ADC5_USR_STATUS1_EOC)) {
		pr_err("ADC channel %s EOC bit not set, status=%#x\n",
			prop->datasheet_name, status);
		ret = -EIO;
		goto unlock;
	}

	ret = adc5_read_voltage_data(adc, adc_code_volt);
	if (ret < 0)
		goto unlock;

	val = 0;
	ret = adc5_write(adc, ADC5_USR_EN_CTL1, &val, 1);
	if (ret < 0)
		goto unlock;

	ret = adc5_cmn_write(adc, ADC7_SW_CALIB_CMN_PBUS_WRITE_SYNC_CTL, &val, 1);
unlock:
	mutex_unlock(&adc->lock);

	return ret;
}

typedef int (*adc_do_conversion)(struct adc5_chip *adc,
			struct adc5_channel_prop *prop,
			struct iio_chan_spec const *chan,
			u16 *data_volt, u16 *data_cur);

static irqreturn_t adc5_isr(int irq, void *dev_id)
{
	struct adc5_chip *adc = dev_id;

	complete(&adc->complete);

	return IRQ_HANDLED;
}

static struct adc5_channel_prop *adc7_get_channel(struct adc5_chip *adc,
						  unsigned int num)
{
	unsigned int i;

	for (i = 0; i < adc->nchannels; i++) {
		if (adc->chan_props[i].channel == num)
			return &adc->chan_props[i];
	}

	pr_err("Invalid channel %02x\n", num);

	return NULL;
}

static int adc5_fwnode_xlate(struct iio_dev *indio_dev,
			     const struct fwnode_reference_args *iiospec)
{
	struct adc5_chip *adc = iio_priv(indio_dev);
	int i;

	for (i = 0; i < adc->nchannels; i++)
		if (adc->chan_props[i].channel == iiospec->args[0])
			return i;

	return -EINVAL;
}

static int adc7_fwnode_xlate(struct iio_dev *indio_dev,
			     const struct fwnode_reference_args *iiospec)
{
	struct adc5_chip *adc = iio_priv(indio_dev);
	int i, v_channel;

	for (i = 0; i < adc->nchannels; i++) {
		v_channel = (adc->chan_props[i].sid << ADC_CHANNEL_OFFSET) |
			adc->chan_props[i].channel;
		if (v_channel == iiospec->args[0])
			return i;
	}

	return -EINVAL;
}

static int adc_read_raw_common(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan, int *val, int *val2,
			 long mask, adc_do_conversion do_conv)
{
	struct adc5_chip *adc = iio_priv(indio_dev);
	struct adc5_channel_prop *prop;
	u16 adc_code_volt, adc_code_cur;
	int ret;

	prop = &adc->chan_props[chan->address];

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		ret = do_conv(adc, prop, chan,
					&adc_code_volt, &adc_code_cur);
		if (ret)
			return ret;

		ret = qcom_adc5_hw_scale(prop->scale_fn_type,
			prop->prescale,
			adc->data,
			adc_code_volt, val);
		if (ret)
			return ret;

		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int adc5_read_raw(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan, int *val, int *val2,
			 long mask)
{
	return adc_read_raw_common(indio_dev, chan, val, val2,
				mask, adc5_do_conversion);
}

static int adc7_read_raw(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan, int *val, int *val2,
			 long mask)
{
	return adc_read_raw_common(indio_dev, chan, val, val2,
				mask, adc7_do_conversion);
}

static int adc5_shared_read_raw(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan, int *val, int *val2,
			 long mask)
{
	return adc_read_raw_common(indio_dev, chan, val, val2,
				mask, adc5_shared_do_conversion);
}

static int adc7_calib(struct adc5_chip *adc)
{
	int ret = 0;
	u16 gnd, vref_1p25, vref_vdd;
	u8 buf[2];
	struct adc5_channel_prop *gnd_prop, *vref_1p25_prop, *vref_vdd_prop;

	/* These channels are mandatory, they are used as reference points */
	gnd_prop = adc7_get_channel(adc, ADC7_REF_GND);
	if (!gnd_prop) {
		dev_err(adc->dev, "GND channel not defined for SW calibration\n");
		return -ENODEV;
	}

	vref_1p25_prop = adc7_get_channel(adc, ADC7_1P25VREF);
	if (!vref_1p25_prop) {
		dev_err(adc->dev, "1.25VREF channel not defined for SW calibration\n");
		return -ENODEV;
	}

	vref_vdd_prop = adc7_get_channel(adc, ADC7_VREF_VADC);
	if (!vref_vdd_prop) {
		dev_err(adc->dev, "VDD channel not defined for SW calibration\n");
		return -ENODEV;
	}

	ret = adc7_sw_calib_do_conversion(adc, gnd_prop, &gnd);
	if (ret) {
		dev_err(adc->dev, "Failed to read GND channel, ret = %d\n", ret);
		return ret;
	}

	ret = adc7_sw_calib_do_conversion(adc, vref_1p25_prop, &vref_1p25);
	if (ret) {
		dev_err(adc->dev, "Failed to read 1.25VREF channel, ret = %d\n", ret);
		return ret;
	}

	ret = adc7_sw_calib_do_conversion(adc, vref_vdd_prop, &vref_vdd);
	if (ret) {
		dev_err(adc->dev, "Failed to read VDD channel, ret = %d\n", ret);
		return ret;
	}

	buf[0] = gnd & 0xff;
	buf[1] = gnd >> 8;
	ret = adc5_write(adc, ADC7_SW_CALIB_PBS_GND_REF_D0, buf, sizeof(buf));
	if (ret)
		return ret;

	vref_vdd -= gnd;
	buf[0] = vref_vdd & 0xff;
	buf[1] = vref_vdd >> 8;
	ret = adc5_write(adc, ADC7_SW_CALIB_PBS_VREF_VADC_DELTA_D0, buf, sizeof(buf));
	if (ret)
		return ret;

	vref_1p25 -= gnd;
	buf[0] = vref_1p25 & 0xff;
	buf[1] = vref_1p25 >> 8;
	ret = adc5_write(adc, ADC7_SW_CALIB_PBS_VREF_MBG_DELTA_D0, buf, sizeof(buf));

	if (!ret)
		dev_dbg(adc->dev, "SW calibration done, gnd:0x%x vref_vdd:0x%x vref_1p25:0x%x\n",
			gnd, vref_vdd, vref_1p25);

	return ret;
}

static int adc7_sw_calib_conv(struct adc5_chip *adc, struct adc5_channel_prop *prop, int *val)
{
	int ret = 0;
	u16 adc_code_volt;

	ret = adc7_calib(adc);
	if (ret)
		return ret;

	ret = adc7_sw_calib_do_conversion(adc, prop, &adc_code_volt);
	if (ret)
		return ret;

	return qcom_adc5_hw_scale(prop->scale_fn_type,
		prop->prescale,
		adc->data,
		adc_code_volt, val);
}

static int adc7_sw_calib_read_raw(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan, int *val, int *val2,
			 long mask)
{
	struct adc5_chip *adc = iio_priv(indio_dev);
	struct adc5_channel_prop *prop;
	int ret;

	prop = &adc->chan_props[chan->address];

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		ret = adc7_sw_calib_conv(adc, prop, val);
		if (ret)
			return ret;

		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct iio_info adc5_info = {
	.read_raw = adc5_read_raw,
	.fwnode_xlate = adc5_fwnode_xlate,
};

static const struct iio_info adc5_shared_info = {
	.read_raw = adc5_shared_read_raw,
	.fwnode_xlate = adc5_fwnode_xlate,
};

static const struct iio_info adc7_info = {
	.read_raw = adc7_read_raw,
	.fwnode_xlate = adc7_fwnode_xlate,
};

static const struct iio_info adc7_sw_calib_info = {
	.read_raw = adc7_sw_calib_read_raw,
	.fwnode_xlate = adc5_fwnode_xlate,
};

struct adc5_channels {
	const char *datasheet_name;
	unsigned int prescale_index;
	enum iio_chan_type type;
	long info_mask;
	enum vadc_scale_fn_type scale_fn_type;
};

/* In these definitions, _pre refers to an index into adc5_prescale_ratios. */
#define ADC5_CHAN(_dname, _type, _mask, _pre, _scale)			\
	{								\
		.datasheet_name = _dname,				\
		.prescale_index = _pre,					\
		.type = _type,						\
		.info_mask = _mask,					\
		.scale_fn_type = _scale,				\
	},								\

#define ADC5_CHAN_TEMP(_dname, _pre, _scale)				\
	ADC5_CHAN(_dname, IIO_TEMP,					\
		BIT(IIO_CHAN_INFO_PROCESSED),				\
		_pre, _scale)						\

#define ADC5_CHAN_VOLT(_dname, _pre, _scale)				\
	ADC5_CHAN(_dname, IIO_VOLTAGE,					\
		  BIT(IIO_CHAN_INFO_PROCESSED),				\
		  _pre, _scale)						\

#define ADC5_CHAN_CUR(_dname, _pre, _scale)				\
	ADC5_CHAN(_dname, IIO_CURRENT,					\
		  BIT(IIO_CHAN_INFO_PROCESSED),				\
		  _pre, _scale)						\

static const struct adc5_channels adc5_chans_pmic[ADC5_MAX_CHANNEL] = {
	[ADC5_REF_GND]		= ADC5_CHAN_VOLT("ref_gnd", 0,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_1P25VREF]		= ADC5_CHAN_VOLT("vref_1p25", 0,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_VPH_PWR]		= ADC5_CHAN_VOLT("vph_pwr", 1,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_VBAT_SNS]		= ADC5_CHAN_VOLT("vbat_sns", 1,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_VCOIN]		= ADC5_CHAN_VOLT("vcoin", 1,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_DIE_TEMP]		= ADC5_CHAN_TEMP("die_temp", 0,
					SCALE_HW_CALIB_PMIC_THERM)
	[ADC5_USB_IN_I]		= ADC5_CHAN_VOLT("usb_in_i_uv", 0,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_USB_IN_V_16]	= ADC5_CHAN_VOLT("usb_in_v_div_16", 8,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_CHG_TEMP]		= ADC5_CHAN_TEMP("chg_temp", 0,
					SCALE_HW_CALIB_PM5_CHG_TEMP)
	/* Charger prescales SBUx and MID_CHG to fit within 1.8V upper unit */
	[ADC5_SBUx]		= ADC5_CHAN_VOLT("chg_sbux", 1,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_MID_CHG_DIV6]	= ADC5_CHAN_VOLT("chg_mid_chg", 3,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_XO_THERM_100K_PU]	= ADC5_CHAN_TEMP("xo_therm", 0,
					SCALE_HW_CALIB_XOTHERM)
	[ADC5_BAT_THERM_100K_PU]	= ADC5_CHAN_TEMP("bat_therm_100k_pu", 0,
					SCALE_HW_CALIB_BATT_THERM_100K)
	[ADC5_BAT_THERM_30K_PU]	= ADC5_CHAN_TEMP("bat_therm_30k_pu", 0,
					SCALE_HW_CALIB_BATT_THERM_30K)
	[ADC5_BAT_THERM_400K_PU]	= ADC5_CHAN_TEMP("bat_therm_400k_pu", 0,
					SCALE_HW_CALIB_BATT_THERM_400K)
	[ADC5_BAT_ID_100K_PU]	= ADC5_CHAN_TEMP("bat_id", 0,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_AMUX_THM1_100K_PU] = ADC5_CHAN_TEMP("amux_thm1_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
	[ADC5_AMUX_THM2_100K_PU] = ADC5_CHAN_TEMP("amux_thm2_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
	[ADC5_AMUX_THM3_100K_PU] = ADC5_CHAN_TEMP("amux_thm3_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
	[ADC5_AMUX_THM4_100K_PU] = ADC5_CHAN_TEMP("amux_thm4_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
	[ADC5_AMUX_THM2]	= ADC5_CHAN_TEMP("amux_thm2", 0,
					SCALE_HW_CALIB_PM5_SMB_TEMP)
	[ADC5_PARALLEL_ISENSE]	= ADC5_CHAN_VOLT("parallel_isense", 0,
					SCALE_HW_CALIB_PM5_CUR)
	[ADC5_GPIO1_100K_PU]	= ADC5_CHAN_TEMP("gpio1_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
	[ADC5_GPIO2_100K_PU]	= ADC5_CHAN_TEMP("gpio2_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
	[ADC5_GPIO3_100K_PU]	= ADC5_CHAN_TEMP("gpio3_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
	[ADC5_GPIO4_100K_PU]	= ADC5_CHAN_TEMP("gpio4_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
};

static const struct adc5_channels adc7_chans_pmic[ADC5_MAX_CHANNEL] = {
	[ADC7_REF_GND]		= ADC5_CHAN_VOLT("ref_gnd", 0,
					SCALE_HW_CALIB_DEFAULT)
	[ADC7_1P25VREF]		= ADC5_CHAN_VOLT("vref_1p25", 0,
					SCALE_HW_CALIB_DEFAULT)
	[ADC7_VREF_VADC]	= ADC5_CHAN_VOLT("vref_vadc", 0,
					SCALE_HW_CALIB_DEFAULT)
	[ADC7_VPH_PWR]		= ADC5_CHAN_VOLT("vph_pwr", 1,
					SCALE_HW_CALIB_DEFAULT)
	[ADC7_VBAT_SNS]		= ADC5_CHAN_VOLT("vbat_sns", 3,
					SCALE_HW_CALIB_DEFAULT)
	[ADC7_USB_IN_V_16]	= ADC5_CHAN_VOLT("usb_in_v_div_16", 8,
					SCALE_HW_CALIB_DEFAULT)
	[ADC7_AMUX_THM3]	= ADC5_CHAN_TEMP("smb_temp", 9,
					SCALE_HW_CALIB_PM7_SMB_TEMP)
	[ADC7_CHG_TEMP]		= ADC5_CHAN_TEMP("chg_temp", 0,
					SCALE_HW_CALIB_PM7_CHG_TEMP)
	[ADC7_IIN_FB]		= ADC5_CHAN_CUR("iin_fb", 10,
					SCALE_HW_CALIB_CUR)
	[ADC7_IIN_SMB]		= ADC5_CHAN_CUR("iin_smb", 12,
					SCALE_HW_CALIB_CUR)
	[ADC7_ICHG_SMB]		= ADC5_CHAN_CUR("ichg_smb", 13,
					SCALE_HW_CALIB_CUR)
	[ADC7_ICHG_FB]		= ADC5_CHAN_CUR("ichg_fb", 14,
					SCALE_HW_CALIB_CUR_RAW)
	[ADC7_DIE_TEMP]		= ADC5_CHAN_TEMP("die_temp", 0,
					SCALE_HW_CALIB_PMIC_THERM_PM7)
	[ADC7_AMUX_THM1_100K_PU] = ADC5_CHAN_TEMP("amux_thm1_pu2", 0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC7_AMUX_THM2_100K_PU] = ADC5_CHAN_TEMP("amux_thm2_pu2", 0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC7_AMUX_THM3_100K_PU] = ADC5_CHAN_TEMP("amux_thm3_pu2", 0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC7_AMUX_THM4_100K_PU] = ADC5_CHAN_TEMP("amux_thm4_pu2", 0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC7_AMUX_THM5_100K_PU] = ADC5_CHAN_TEMP("amux_thm5_pu2", 0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC7_AMUX_THM6_100K_PU] = ADC5_CHAN_TEMP("amux_thm6_pu2", 0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC7_GPIO1_100K_PU]	= ADC5_CHAN_TEMP("gpio1_pu2", 0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC7_GPIO2_100K_PU]	= ADC5_CHAN_TEMP("gpio2_pu2", 0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC7_GPIO3_100K_PU]	= ADC5_CHAN_TEMP("gpio3_pu2", 0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC7_GPIO4_100K_PU]	= ADC5_CHAN_TEMP("gpio4_pu2", 0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
};

static const struct adc5_channels adc5_chans_rev2[ADC5_MAX_CHANNEL] = {
	[ADC5_REF_GND]		= ADC5_CHAN_VOLT("ref_gnd", 0,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_1P25VREF]		= ADC5_CHAN_VOLT("vref_1p25", 0,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_VREF_VADC]	= ADC5_CHAN_VOLT("vref_vadc", 0,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_VPH_PWR]		= ADC5_CHAN_VOLT("vph_pwr", 1,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_VBAT_SNS]		= ADC5_CHAN_VOLT("vbat_sns", 1,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_VCOIN]		= ADC5_CHAN_VOLT("vcoin", 1,
					SCALE_HW_CALIB_DEFAULT)
	[ADC5_DIE_TEMP]		= ADC5_CHAN_TEMP("die_temp", 0,
					SCALE_HW_CALIB_PMIC_THERM)
	[ADC5_AMUX_THM1_100K_PU] = ADC5_CHAN_TEMP("amux_thm1_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
	[ADC5_AMUX_THM2_100K_PU] = ADC5_CHAN_TEMP("amux_thm2_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
	[ADC5_AMUX_THM3_100K_PU] = ADC5_CHAN_TEMP("amux_thm3_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
	[ADC5_AMUX_THM4_100K_PU] = ADC5_CHAN_TEMP("amux_thm4_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
	[ADC5_AMUX_THM5_100K_PU] = ADC5_CHAN_TEMP("amux_thm5_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
	[ADC5_XO_THERM_100K_PU]	= ADC5_CHAN_TEMP("xo_therm_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
	[ADC5_GPIO2_100K_PU]	= ADC5_CHAN_TEMP("gpio2_100k_pu", 0,
					SCALE_HW_CALIB_THERM_100K_PULLUP)
};

/* Below channels are similar to ADC5_GEN3 */
static const struct adc5_channels adc5_shared_chans_pmic[ADC5_MAX_CHANNEL] = {
	[ADC5_GEN3_OFFSET_REF]		= ADC5_CHAN_VOLT("ref_gnd", 0,
						SCALE_HW_CALIB_DEFAULT)
	[ADC5_GEN3_1P25VREF]		= ADC5_CHAN_VOLT("vref_1p25", 0,
						SCALE_HW_CALIB_DEFAULT)
	[ADC5_GEN3_VPH_PWR]		= ADC5_CHAN_VOLT("vph_pwr", 1,
						SCALE_HW_CALIB_DEFAULT)
	[ADC5_GEN3_VBAT_SNS_QBG]	= ADC5_CHAN_VOLT("vbat_sns", 1,
						SCALE_HW_CALIB_DEFAULT)
	[ADC5_GEN3_AMUX3_THM]		= ADC5_CHAN_TEMP("smb_temp", 9,
						SCALE_HW_CALIB_PM7_SMB_TEMP)
	[ADC5_GEN3_CHG_TEMP]		= ADC5_CHAN_TEMP("chg_temp", 0,
						SCALE_HW_CALIB_PM7_CHG_TEMP)
	[ADC5_GEN3_USB_SNS_V_16]	= ADC5_CHAN_TEMP("usb_sns_v_div_16", 8,
						SCALE_HW_CALIB_DEFAULT)
	[ADC5_GEN3_VIN_DIV16_MUX]	= ADC5_CHAN_TEMP("vin_div_16", 8,
						SCALE_HW_CALIB_DEFAULT)
	[ADC5_GEN3_IIN_FB]		= ADC5_CHAN_CUR("iin_fb", 10,
						SCALE_HW_CALIB_CUR)
	[ADC5_GEN3_ICHG_SMB]		= ADC5_CHAN_CUR("ichg_smb", 13,
						SCALE_HW_CALIB_CUR)
	[ADC5_GEN3_IIN_SMB]		= ADC5_CHAN_CUR("iin_smb", 12,
						SCALE_HW_CALIB_CUR)
	[ADC5_GEN3_ICHG_FB]		= ADC5_CHAN_CUR("ichg_fb", 16,
						SCALE_HW_CALIB_CUR_RAW)
	[ADC5_GEN3_DIE_TEMP]		= ADC5_CHAN_TEMP("die_temp", 0,
						SCALE_HW_CALIB_PMIC_THERM_PM7)
	[ADC5_GEN3_TEMP_ALARM_LITE]	= ADC5_CHAN_TEMP("die_temp_lite", 0,
						SCALE_HW_CALIB_PMIC_THERM_PM7)
	[ADC5_GEN3_AMUX1_THM_100K_PU]	= ADC5_CHAN_TEMP("amux_thm1_pu2", 0,
						SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC5_GEN3_AMUX2_THM_100K_PU]	= ADC5_CHAN_TEMP("amux_thm2_pu2", 0,
						SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC5_GEN3_AMUX3_THM_100K_PU]	= ADC5_CHAN_TEMP("amux_thm3_pu2", 0,
						SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC5_GEN3_AMUX4_THM_100K_PU]	= ADC5_CHAN_TEMP("amux_thm4_pu2", 0,
						SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC5_GEN3_AMUX5_THM_100K_PU]	= ADC5_CHAN_TEMP("amux_thm5_pu2", 0,
						SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC5_GEN3_AMUX6_THM_100K_PU]	= ADC5_CHAN_TEMP("amux_thm6_pu2", 0,
						SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC5_GEN3_AMUX1_GPIO_100K_PU]	= ADC5_CHAN_TEMP("amux1_gpio_pu2", 0,
						SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC5_GEN3_AMUX2_GPIO_100K_PU]	= ADC5_CHAN_TEMP("amux2_gpio_pu2", 0,
						SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC5_GEN3_AMUX3_GPIO_100K_PU]	= ADC5_CHAN_TEMP("amux3_gpio_pu2", 0,
						SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC5_GEN3_AMUX4_GPIO_100K_PU]	= ADC5_CHAN_TEMP("amux4_gpio_pu2", 0,
						SCALE_HW_CALIB_THERM_100K_PU_PM7)
};

static int adc5_get_fw_channel_data(struct adc5_chip *adc,
				    struct adc5_channel_prop *prop,
				    struct fwnode_handle *fwnode,
				    const struct adc5_data *data)
{
	const char *channel_name;
	char *name;
	u32 chan, value, varr[2];
	u32 sid = 0;
	int ret;
	struct device *dev = adc->dev;

	name = devm_kasprintf(dev, GFP_KERNEL, "%pfwP", fwnode);
	if (!name)
		return -ENOMEM;

	/* Cut the address part */
	name[strchrnul(name, '@') - name] = '\0';

	ret = fwnode_property_read_u32(fwnode, "reg", &chan);
	if (ret) {
		dev_err(dev, "invalid channel number %s\n", name);
		return ret;
	}

	/* Value read from "reg" is virtual channel number */

	/* virtual channel number = sid << 8 | channel number */

	if (adc->data->info == &adc7_info) {
		sid = chan >> ADC_CHANNEL_OFFSET;
		chan = chan & ADC_CHANNEL_MASK;
	}

	if (chan > ADC5_PARALLEL_ISENSE_VBAT_IDATA ||
	    !data->adc_chans[chan].datasheet_name) {
		dev_err(dev, "%s invalid channel number %d\n", name, chan);
		return -EINVAL;
	}

	/* the channel has DT description */
	prop->channel = chan;
	prop->sid = sid;

	ret = fwnode_property_read_string(fwnode, "label", &channel_name);
	if (ret)
		channel_name = name;

	prop->datasheet_name = channel_name;

	ret = fwnode_property_read_u32(fwnode, "qcom,decimation", &value);
	if (!ret) {
		ret = qcom_adc5_decimation_from_dt(value, data->decimation);
		if (ret < 0) {
			dev_err(dev, "%02x invalid decimation %d\n",
				chan, value);
			return ret;
		}
		prop->decimation = ret;
	} else {
		prop->decimation = ADC5_DECIMATION_DEFAULT;
	}

	ret = fwnode_property_read_u32_array(fwnode, "qcom,pre-scaling", varr, 2);
	if (!ret) {
		ret = qcom_adc5_prescaling_from_dt(varr[0], varr[1]);
		if (ret < 0) {
			dev_err(dev, "%02x invalid pre-scaling <%d %d>\n",
				chan, varr[0], varr[1]);
			return ret;
		}
		prop->prescale = ret;
	} else {
		prop->prescale =
			adc->data->adc_chans[prop->channel].prescale_index;
	}

	ret = fwnode_property_read_u32(fwnode, "qcom,hw-settle-time", &value);
	if (!ret) {
		u8 dig_version[2];

		ret = adc5_read(adc, ADC5_USR_REVISION1, dig_version,
							sizeof(dig_version));
		if (ret) {
			dev_err(dev, "Invalid dig version read %d\n", ret);
			return ret;
		}

		dev_dbg(dev, "dig_ver:minor:%d, major:%d\n", dig_version[0],
						dig_version[1]);
		/* Digital controller >= 5.3 have hw_settle_2 option */
		if ((dig_version[0] >= ADC5_HW_SETTLE_DIFF_MINOR &&
			dig_version[1] >= ADC5_HW_SETTLE_DIFF_MAJOR) ||
			adc->data->info == &adc7_info ||
			adc->data->info == &adc7_sw_calib_info)
			ret = qcom_adc5_hw_settle_time_from_dt(value, data->hw_settle_2);
		else
			ret = qcom_adc5_hw_settle_time_from_dt(value, data->hw_settle_1);

		if (ret < 0) {
			dev_err(dev, "%02x invalid hw-settle-time %d us\n",
				chan, value);
			return ret;
		}
		prop->hw_settle_time = ret;
	} else {
		prop->hw_settle_time = VADC_DEF_HW_SETTLE_TIME;
	}

	ret = fwnode_property_read_u32(fwnode, "qcom,avg-samples", &value);
	if (!ret) {
		ret = qcom_adc5_avg_samples_from_dt(value);
		if (ret < 0) {
			dev_err(dev, "%02x invalid avg-samples %d\n",
				chan, value);
			return ret;
		}
		prop->avg_samples = ret;
	} else {
		prop->avg_samples = VADC_DEF_AVG_SAMPLES;
	}

	prop->scale_fn_type = -EINVAL;
	ret = fwnode_property_read_u32(fwnode, "qcom,scale-fn-type", &value);
	if (!ret && value < SCALE_HW_CALIB_INVALID)
		prop->scale_fn_type = value;

	if (fwnode_property_read_bool(fwnode, "qcom,ratiometric"))
		prop->cal_method = ADC5_RATIOMETRIC_CAL;
	else if (fwnode_property_read_bool(fwnode, "qcom,no-cal"))
		prop->cal_method = ADC5_NO_CAL;
	else
		prop->cal_method = ADC5_ABSOLUTE_CAL;

	/*
	 * Default to using timer calibration. Using a fresh calibration value
	 * for every conversion will increase the overall time for a request.
	 */
	prop->cal_val = ADC5_TIMER_CAL;

	dev_dbg(dev, "%02x name %s\n", chan, name);

	return 0;
}

static const struct adc5_data adc5_data_pmic = {
	.name = "pm-adc5",
	.full_scale_code_volt = 0x70e4,
	.full_scale_code_cur = 0x2710,
	.adc_chans = adc5_chans_pmic,
	.info = &adc5_info,
	.decimation = (unsigned int [ADC5_DECIMATION_SAMPLES_MAX])
				{250, 420, 840},
	.hw_settle_1 = (unsigned int [VADC_HW_SETTLE_SAMPLES_MAX])
				{15, 100, 200, 300, 400, 500, 600, 700,
				800, 900, 1, 2, 4, 6, 8, 10},
	.hw_settle_2 = (unsigned int [VADC_HW_SETTLE_SAMPLES_MAX])
				{15, 100, 200, 300, 400, 500, 600, 700,
				1, 2, 4, 8, 16, 32, 64, 128},
};

static const struct adc5_data adc5_data_pmic5_shared = {
	.name = "pm-adc5-shared",
	.full_scale_code_volt = 0x70e4,
	.full_scale_code_cur = 0x2ee0,
	.adc_chans = adc5_shared_chans_pmic,
	.info = &adc5_shared_info,
	.decimation = (unsigned int [ADC5_DECIMATION_SAMPLES_MAX])
				{85, 340, 1360},
	.hw_settle_1 = (unsigned int [VADC_HW_SETTLE_SAMPLES_MAX])
				{15, 100, 200, 300, 400, 500, 600, 700,
				1000, 2000, 4000, 8000, 16000, 32000,
				64000, 128000},
};

static const struct adc5_data adc7_data_pmic = {
	.name = "pm-adc7",
	.full_scale_code_volt = 0x70e4,
	.adc_chans = adc7_chans_pmic,
	.info = &adc7_info,
	.decimation = (unsigned int [ADC5_DECIMATION_SAMPLES_MAX])
				{85, 340, 1360},
	.hw_settle_2 = (unsigned int [VADC_HW_SETTLE_SAMPLES_MAX])
				{15, 100, 200, 300, 400, 500, 600, 700,
				1000, 2000, 4000, 8000, 16000, 32000,
				64000, 128000},
};

static const struct adc5_data adc7_sw_calib_data_pmic = {
	.name = "pm-adc7",
	.full_scale_code_volt = 0x70e4,
	.adc_chans = adc7_chans_pmic,
	.info = &adc7_sw_calib_info,
	.decimation = (unsigned int [ADC5_DECIMATION_SAMPLES_MAX])
				{85, 340, 1360},
	.hw_settle_2 = (unsigned int [VADC_HW_SETTLE_SAMPLES_MAX])
				{15, 100, 200, 300, 400, 500, 600, 700,
				1000, 2000, 4000, 8000, 16000, 32000,
				64000, 128000},
};

static const struct adc5_data adc5_data_pmic5_lite = {
	.name = "pm-adc5-lite",
	.full_scale_code_volt = 0x70e4,
	/* On PMI632, IBAT LSB = 5A/32767 */
	.full_scale_code_cur = 5000,
	.adc_chans = adc5_chans_pmic,
	.decimation = (unsigned int []) {250, 420, 840},
	.hw_settle_1 = (unsigned int []) {15, 100, 200, 300, 400, 500, 600, 700,
					800, 900, 1, 2, 4, 6, 8, 10},
};

static const struct adc5_data adc5_data_pmic_rev2 = {
	.name = "pm-adc4-rev2",
	.full_scale_code_volt = 0x4000,
	.full_scale_code_cur = 0x1800,
	.adc_chans = adc5_chans_rev2,
	.info = &adc5_info,
	.decimation = (unsigned int [ADC5_DECIMATION_SAMPLES_MAX])
				{256, 512, 1024},
	.hw_settle_1 = (unsigned int [VADC_HW_SETTLE_SAMPLES_MAX])
				{0, 100, 200, 300, 400, 500, 600, 700,
				800, 900, 1, 2, 4, 6, 8, 10},
	.hw_settle_2 = (unsigned int [VADC_HW_SETTLE_SAMPLES_MAX])
				{15, 100, 200, 300, 400, 500, 600, 700,
				1, 2, 4, 8, 16, 32, 64, 128},
};

static const struct of_device_id adc5_match_table[] = {
	{
		.compatible = "qcom,spmi-adc5",
		.data = &adc5_data_pmic,
	},
	{
		.compatible = "qcom,spmi-adc7",
		.data = &adc7_data_pmic,
	},
	{
		.compatible = "qcom,spmi-adc7-sw-calib",
		.data = &adc7_sw_calib_data_pmic,
	},
	{
		.compatible = "qcom,spmi-adc-rev2",
		.data = &adc5_data_pmic_rev2,
	},
	{
		.compatible = "qcom,spmi-adc5-lite",
		.data = &adc5_data_pmic5_lite,
	},
	{
		.compatible = "qcom,spmi-adc5-shared",
		.data = &adc5_data_pmic5_shared,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, adc5_match_table);

static int adc5_get_fw_data(struct adc5_chip *adc)
{
	const struct adc5_channels *adc_chan;
	struct iio_chan_spec *iio_chan;
	struct adc5_channel_prop prop, *chan_props;
	struct fwnode_handle *child;
	unsigned int index = 0;
	int ret;

	adc->nchannels = device_get_child_node_count(adc->dev);
	if (!adc->nchannels)
		return -EINVAL;

	adc->iio_chans = devm_kcalloc(adc->dev, adc->nchannels,
				       sizeof(*adc->iio_chans), GFP_KERNEL);
	if (!adc->iio_chans)
		return -ENOMEM;

	adc->chan_props = devm_kcalloc(adc->dev, adc->nchannels,
					sizeof(*adc->chan_props), GFP_KERNEL);
	if (!adc->chan_props)
		return -ENOMEM;

	chan_props = adc->chan_props;
	iio_chan = adc->iio_chans;
	adc->data = device_get_match_data(adc->dev);
	if (!adc->data)
		adc->data = &adc5_data_pmic;

	device_for_each_child_node(adc->dev, child) {
		ret = adc5_get_fw_channel_data(adc, &prop, child, adc->data);
		if (ret) {
			fwnode_handle_put(child);
			return ret;
		}

		if (prop.scale_fn_type == -EINVAL)
			prop.scale_fn_type =
				adc->data->adc_chans[prop.channel].scale_fn_type;

		*chan_props = prop;
		adc_chan = &adc->data->adc_chans[prop.channel];

		iio_chan->channel = prop.channel;
		iio_chan->datasheet_name = prop.datasheet_name;
		iio_chan->extend_name = prop.datasheet_name;
		iio_chan->info_mask_separate = adc_chan->info_mask;
		iio_chan->type = adc_chan->type;
		iio_chan->address = index;
		iio_chan++;
		chan_props++;
		index++;
	}

	return 0;
}

static int adc5_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iio_dev *indio_dev;
	struct adc5_chip *adc;
	struct regmap *regmap;
	const char *irq_name;
	const __be32 *prop_addr;
	int ret;
	u32 reg;
	u8 val;

	regmap = dev_get_regmap(dev->parent, NULL);
	if (!regmap)
		return -ENODEV;

	ret = device_property_read_u32(dev, "reg", &reg);
	if (ret < 0)
		return ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->regmap = regmap;
	adc->dev = dev;

	prop_addr = of_get_address(dev->of_node, 0, NULL, NULL);
	if (!prop_addr) {
		pr_err("invalid IO resource\n");
		return -EINVAL;
	}
	adc->base = be32_to_cpu(*prop_addr);

	prop_addr = of_get_address(dev->of_node, 1, NULL, NULL);
	if (!prop_addr)
		pr_debug("invalid cmn resource\n");
	else
		adc->cmn_base = be32_to_cpu(*prop_addr);

	if (of_device_is_compatible(dev->of_node, "qcom,spmi-adc7-sw-calib")) {
		if (!adc->cmn_base) {
			pr_err("ADC_CMN undefined\n");
			return -ENODEV;
		}
	}

	init_completion(&adc->complete);
	mutex_init(&adc->lock);

	ret = adc5_get_fw_data(adc);
	if (ret) {
		dev_err(dev, "adc get dt data failed\n");
		return ret;
	}

	adc->irq_eoc = platform_get_irq(pdev, 0);
	if (adc->irq_eoc < 0) {
		if (adc->irq_eoc == -EPROBE_DEFER || adc->irq_eoc == -EINVAL)
			return adc->irq_eoc;
		adc->poll_eoc = true;
	} else {
		irq_name = "pm-adc5";
		if (adc->data->name)
			irq_name = adc->data->name;

		ret = devm_request_irq(dev, adc->irq_eoc, adc5_isr, 0,
				       irq_name, adc);
		if (ret)
			return ret;
	}

	platform_set_drvdata(pdev, adc);
	if (of_device_is_compatible(dev->of_node, "qcom,spmi-adc7-sw-calib")) {
		ret = adc7_calib(adc);
		if (ret)
			return ret;

		val = ADC7_SW_CALIB_PBS_CALREF_RDY;
		ret = adc5_write(adc, ADC7_SW_CALIB_PBS_CALREF_FLAG, &val, 1);
		if (ret < 0)
			return ret;
	}

	indio_dev->name = pdev->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = adc->data->info;
	indio_dev->channels = adc->iio_chans;
	indio_dev->num_channels = adc->nchannels;

	return devm_iio_device_register(dev, indio_dev);
}

static int adc_restore(struct device *dev)
{
	int ret = 0;
	struct adc5_chip *adc = dev_get_drvdata(dev);

	if (adc->irq_eoc > 0)
		ret = devm_request_irq(dev, adc->irq_eoc, adc5_isr, 0,
				       "pm-adc5", adc);

	return ret;
}

static int adc_freeze(struct device *dev)
{
	struct adc5_chip *adc = dev_get_drvdata(dev);

	if (adc->irq_eoc > 0)
		devm_free_irq(dev, adc->irq_eoc, adc);

	return 0;
}

static const struct dev_pm_ops adc_pm_ops = {
	.freeze = adc_freeze,
	.thaw = adc_restore,
	.restore = adc_restore,
};

static struct platform_driver adc5_driver = {
	.driver = {
		.name = "qcom-spmi-adc5",
		.of_match_table = adc5_match_table,
		.pm = &adc_pm_ops,
	},
	.probe = adc5_probe,
};
module_platform_driver(adc5_driver);

MODULE_ALIAS("platform:qcom-spmi-adc5");
MODULE_DESCRIPTION("Qualcomm Technologies Inc. PMIC5 ADC driver");
MODULE_LICENSE("GPL v2");
