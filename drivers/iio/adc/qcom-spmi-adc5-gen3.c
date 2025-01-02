// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/thermal.h>
#include <linux/thermal_minidump.h>
#include <linux/slab.h>
#include <linux/iio/iio.h>
#include <linux/iio/adc/qcom-vadc-common.h>

#include <dt-bindings/iio/qcom,spmi-vadc.h>

static LIST_HEAD(adc_tm_device_list);

#define ADC5_GEN3_HS				0x45
#define ADC5_GEN3_HS_BUSY			BIT(7)
#define ADC5_GEN3_HS_READY			BIT(0)

#define ADC5_GEN3_STATUS1			0x46
#define ADC5_GEN3_STATUS1_CONV_FAULT		BIT(7)
#define ADC5_GEN3_STATUS1_THR_CROSS		BIT(6)
#define ADC5_GEN3_STATUS1_EOC			BIT(0)

#define ADC5_GEN3_TM_EN_STS			0x47

#define ADC5_GEN3_TM_HIGH_STS			0x48

#define ADC5_GEN3_TM_LOW_STS			0x49

#define ADC5_GEN3_EOC_STS			0x4a
#define ADC5_GEN3_EOC_CHAN_0			BIT(0)

#define ADC5_GEN3_EOC_CLR			0x4b

#define ADC5_GEN3_TM_HIGH_STS_CLR		0x4c

#define ADC5_GEN3_TM_LOW_STS_CLR		0x4d

#define ADC5_GEN3_CONV_ERR_CLR			0x4e
#define ADC5_GEN3_CONV_ERR_CLR_REQ		BIT(0)

#define ADC5_GEN3_SID				0x4f
#define ADC5_GEN3_SID_MASK			GENMASK(3, 0)

#define ADC5_GEN3_PERPH_CH			0x50
#define ADC5_GEN3_CHAN_CONV_REQ			BIT(7)

#define ADC5_GEN3_TIMER_SEL			0x51
#define ADC5_GEN3_TIME_IMMEDIATE		0x1

#define ADC5_GEN3_DIG_PARAM			0x52
#define ADC5_GEN3_DIG_PARAM_CAL_SEL_MASK	GENMASK(5, 4)
#define ADC5_GEN3_DIG_PARAM_CAL_SEL_SHIFT	4
#define ADC5_GEN3_DIG_PARAM_DEC_RATIO_SEL_MASK	GENMASK(3, 2)
#define ADC5_GEN3_DIG_PARAM_DEC_RATIO_SEL_SHIFT	2

#define ADC5_GEN3_FAST_AVG			0x53
#define ADC5_GEN3_FAST_AVG_CTL_EN		BIT(7)
#define ADC5_GEN3_FAST_AVG_CTL_SAMPLES_MASK	GENMASK(2, 0)

#define ADC5_GEN3_ADC_CH_SEL_CTL		0x54

#define ADC5_GEN3_DELAY_CTL			0x55
#define ADC5_GEN3_HW_SETTLE_DELAY_MASK		0xf

#define ADC5_GEN3_CH_EN				0x56

#define ADC5_GEN3_LOW_THR0			0x57

#define ADC5_GEN3_LOW_THR1			0x58

#define ADC5_GEN3_HIGH_THR0			0x59

#define ADC5_GEN3_HIGH_THR1			0x5a

#define ADC5_GEN3_CH0_DATA0			0x5c
#define ADC5_GEN3_CH0_DATA1			0x5d
#define ADC5_GEN3_CH1_DATA0			0x5e
#define ADC5_GEN3_CH1_DATA1			0x5f
#define ADC5_GEN3_CH2_DATA0			0x60
#define ADC5_GEN3_CH2_DATA1			0x61
#define ADC5_GEN3_CH3_DATA0			0x62
#define ADC5_GEN3_CH3_DATA1			0x63
#define ADC5_GEN3_CH4_DATA0			0x64
#define ADC5_GEN3_CH4_DATA1			0x65
#define ADC5_GEN3_CH5_DATA0			0x66
#define ADC5_GEN3_CH5_DATA1			0x67
#define ADC5_GEN3_CH6_DATA0			0x68
#define ADC5_GEN3_CH6_DATA1			0x69
#define ADC5_GEN3_CH7_DATA0			0x6a
#define ADC5_GEN3_CH7_DATA1			0x6b

#define ADC5_GEN3_CONV_REQ			0xe5
#define ADC5_GEN3_CONV_REQ_REQ			BIT(0)

#define ADC5_GEN3_SID_OFFSET			0x8
#define ADC5_GEN3_CHANNEL_MASK			0xff
#define V_CHAN(x)				(((x).sid << ADC5_GEN3_SID_OFFSET) | (x).channel)

#define ADC_TM5_GEN3_LOWER_MASK(n)		((n) & GENMASK(7, 0))
#define ADC_TM5_GEN3_UPPER_MASK(n)		(((n) & GENMASK(15, 8)) >> 8)

enum adc5_cal_method {
	ADC5_NO_CAL = 0,
	ADC5_RATIOMETRIC_CAL,
	ADC5_ABSOLUTE_CAL
};

enum adc_time_select {
	MEAS_INT_DISABLE = 0,
	MEAS_INT_IMMEDIATE,
	MEAS_INT_50MS,
	MEAS_INT_100MS,
	MEAS_INT_1S,
	MEAS_INT_NONE,
};

enum adc_tm_type {
	ADC_TM_NONE = 0,
	ADC_TM,
	ADC_TM_IIO,
	ADC_TM_NON_THERMAL,
	ADC_TM_INVALID,
};

static struct adc_tm_reverse_scale_fn adc_tm_rscale_fn[] = {
	[SCALE_R_ABSOLUTE] = {adc_tm_absolute_rthr_gen3},
};

struct adc5_base_data {
	u16			base_addr;
	const char		*irq_name;
	int			irq;
};

/**
 * struct adc5_channel_prop - ADC channel property.
 * @channel: channel number, refer to the channel list.
 * @cal_method: calibration method.
 * @decimation: sampling rate supported for the channel.
 * @sid: slave id of PMIC owning the channel, for PMIC5 Gen2 and above.
 * @prescale: channel scaling performed on the input signal.
 * @hw_settle_time: the time between AMUX being configured and the
 *	start of conversion.
 * @avg_samples: ability to provide single result from the ADC
 *	that is an average of multiple measurements.
 * @sdam_index: Index for which SDAM this channel is on.
 * @scale_fn_type: Represents the scaling function to convert voltage
 *	physical units desired by the client for the channel.
 * @datasheet_name: Channel name used in device tree.
 * @chip: pointer to top-level ADC device structure.
 * @adc_tm: indicates TM type if the channel is used for TM measurements.
 * @tm_chan_index: TM channel number used (ranging from 1-7).
 * @timer: time period of recurring TM measurement.
 * @tzd: pointer to thermal device corresponding to TM channel.
 * @high_thr_en: TM high threshold crossing detection enabled.
 * @low_thr_en: TM low threshold crossing detection enabled.
 * @high_thr_triggered: indicates if high TM threshold has been triggered.
 * @low_thr_triggered: indicates if low TM threshold has been triggered.
 * @high_thr_voltage: upper threshold voltage for TM.
 * @low_thr_voltage: lower threshold voltage for TM.
 * @last_temp: last temperature that caused threshold violation,
 *	or a thermal TM channel.
 * @last_temp_set: indicates if last_temp is stored.
 * @req_wq: workqueue holding queued notification tasks for a non-thermal
 *	TM channel.
 * @work: scheduled work for handling non-thermal TM client notification.
 * @thr_list: list of client thresholds configured for non-thermal TM channel.
 * @adc_rscale_fn: reverse scaling function to convert voltage to raw code
 *	for non-thermal TM channels.
 */
struct adc5_channel_prop {
	unsigned int			channel;
	enum adc5_cal_method		cal_method;
	unsigned int			decimation;
	unsigned int			sid;
	unsigned int			prescale;
	unsigned int			hw_settle_time;
	unsigned int			avg_samples;
	unsigned int			sdam_index;

	enum vadc_scale_fn_type		scale_fn_type;
	const char			*datasheet_name;

	struct adc5_chip		*chip;
	/* TM properties */
	int				adc_tm;
	unsigned int			tm_chan_index;
	unsigned int			timer;
	struct thermal_zone_device	*tzd;
	int				high_thr_en;
	int				low_thr_en;
	bool				high_thr_triggered;
	bool				low_thr_triggered;
	int64_t				high_thr_voltage;
	int64_t				low_thr_voltage;
	int				last_temp;
	bool				last_temp_set;
	struct workqueue_struct		*req_wq;
	struct work_struct		work;
	struct list_head		thr_list;
	enum adc_tm_rscale_fn_type	adc_rscale_fn;
};

/**
 * struct adc5_chip - ADC private structure.
 * @regmap: SPMI ADC5 peripheral register map field.
 * @dev: SPMI ADC5 device.
 * @base: pointer to array of ADC peripheral base and interrupt.
 * @debug_base: base address for the reserved ADC peripheral,
 *	to dump for debug purposes alone.
 * @num_sdams: number of SDAMs being used.
 * @nchannels: number of ADC channels.
 * @chan_props: array of ADC channel properties.
 * @iio_chans: array of IIO channels specification.
 * @complete: ADC result notification after interrupt is received.
 * @lock: ADC lock for access to the peripheral.
 * @data: software configuration data.
 * @n_tm_channels: number of ADC channels used for TM measurements.
 * @list: list item, used to add this device to gloal list of ADC_TM devices.
 * @device_list: pointer to list of ADC_TM devices.
 * @tm_handler_work: scheduled work for handling TM threshold violation.
 */
struct adc5_chip {
	struct regmap			*regmap;
	struct device			*dev;
	struct adc5_base_data		*base;
	u16				debug_base;
	unsigned int			num_sdams;
	unsigned int			nchannels;
	struct adc5_channel_prop	*chan_props;
	struct iio_chan_spec		*iio_chans;
	struct completion		complete;
	struct mutex			lock;
	const struct adc5_data		*data;
	/* TM properties */
	unsigned int			n_tm_channels;
	struct list_head		list;
	struct list_head		*device_list;
	struct work_struct		tm_handler_work;
	struct minidump_data		*adc_md;
};

static int adc5_read(struct adc5_chip *adc, unsigned int sdam_index, u16 offset, u8 *data, int len)
{
	int ret;

	ret = regmap_bulk_read(adc->regmap, adc->base[sdam_index].base_addr + offset, data, len);
	if (ret < 0)
		pr_err("adc read to register 0x%x of length:%d failed, ret=%d\n", offset, len, ret);

	return ret;
}

static int adc5_write(struct adc5_chip *adc, unsigned int sdam_index, u16 offset, u8 *data, int len)
{
	int ret;

	ret = regmap_bulk_write(adc->regmap, adc->base[sdam_index].base_addr + offset, data, len);
	if (ret < 0)
		pr_err("adc write to register 0x%x of length:%d failed, ret=%d\n", offset, len,
			ret);

	return ret;
}

static int adc5_hw_settle_time_from_dt(u32 value,
					const unsigned int *hw_settle)
{
	unsigned int i;

	for (i = 0; i < VADC_HW_SETTLE_SAMPLES_MAX; i++) {
		if (value == hw_settle[i])
			return i;
	}

	return -ENOENT;
}

static int adc5_avg_samples_from_dt(u32 value)
{
	if (!is_power_of_2(value) || value > ADC5_AVG_SAMPLES_MAX)
		return -EINVAL;

	return __ffs(value);
}

static int adc5_decimation_from_dt(u32 value,
					const unsigned int *decimation)
{
	unsigned int i;

	for (i = 0; i < ADC5_DECIMATION_SAMPLES_MAX; i++) {
		if (value == decimation[i])
			return i;
	}

	return -ENOENT;
}

#if IS_ENABLED(CONFIG_QCOM_SPMI_ADC5_GEN3_DEBUG_LOGGING)
#define NUM_BYTES	8
#define REG_COUNT	32

static void adc5_gen3_dump_register(struct regmap *regmap, unsigned int offset)
{
	int i, rc;
	u8 buf[NUM_BYTES];

	for (i = 0; i < REG_COUNT; i++) {
		rc = regmap_bulk_read(regmap, offset, buf, sizeof(buf));
		if (rc < 0) {
			pr_err("debug register dump failed with rc=%d\n", rc);
			return;
		}
		pr_err("%#04x: %*ph\n", offset, sizeof(buf), buf);
		offset += NUM_BYTES;
	}
	pr_err("\n");
}

static void adc5_gen3_dump_regs_debug(struct adc5_chip *adc)
{
	u32 i;

	for (i = 0; i < adc->num_sdams; i++) {
		pr_err("ADC SDAM%d DUMP\n", i);
		adc5_gen3_dump_register(adc->regmap, adc->base[i].base_addr);
	}

	if (adc->debug_base) {
		pr_err("ADC DEBUG BASE DUMP\n");
		adc5_gen3_dump_register(adc->regmap, adc->debug_base);
	}

	BUG_ON(1);
}
#else
static inline void adc5_gen3_dump_regs_debug(struct adc5_chip *adc)
{}
#endif

static int adc5_gen3_read_voltage_data(struct adc5_chip *adc, u16 *data,
				u8 sdam_index)
{
	int ret;
	u8 rslt[2];

	ret = adc5_read(adc, sdam_index, ADC5_GEN3_CH0_DATA0, rslt, 2);
	if (ret < 0)
		return ret;

	*data = (rslt[1] << 8) | rslt[0];

	if (*data == ADC5_USR_DATA_CHECK) {
		pr_err("Invalid data:%#x\n", *data);
		return -EINVAL;
	}

	pr_debug("voltage raw code:0x%x\n", *data);

	return 0;
}

static void adc5_gen3_update_dig_param(struct adc5_chip *adc,
			struct adc5_channel_prop *prop, u8 *data)
{
	/* Update calibration select */
	*data &= ~ADC5_GEN3_DIG_PARAM_CAL_SEL_MASK;
	*data |= (prop->cal_method << ADC5_GEN3_DIG_PARAM_CAL_SEL_SHIFT);

	/* Update decimation ratio select */
	*data &= ~ADC5_GEN3_DIG_PARAM_DEC_RATIO_SEL_MASK;
	*data |= (prop->decimation << ADC5_GEN3_DIG_PARAM_DEC_RATIO_SEL_SHIFT);
}

static int adc5_gen3_configure(struct adc5_chip *adc,
			struct adc5_channel_prop *prop)
{
	int ret;
	u8 conv_req = 0, buf[7];
	u8 sdam_index = prop->sdam_index;

	/* Reserve channel 0 of first SDAM for immediate conversions */
	if (prop->adc_tm)
		sdam_index = 0;

	ret = adc5_read(adc, sdam_index, ADC5_GEN3_SID, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	/* Write SID */
	buf[0] = prop->sid & ADC5_GEN3_SID_MASK;

	/*
	 * Use channel 0 by default for immediate conversion and
	 * to indicate there is an actual conversion request
	 */
	buf[1] = ADC5_GEN3_CHAN_CONV_REQ | 0;

	buf[2] = ADC5_GEN3_TIME_IMMEDIATE;

	/* Digital param selection */
	adc5_gen3_update_dig_param(adc, prop, &buf[3]);

	/* Update fast average sample value */
	buf[4] &= (u8) ~ADC5_GEN3_FAST_AVG_CTL_SAMPLES_MASK;
	buf[4] |= prop->avg_samples | ADC5_GEN3_FAST_AVG_CTL_EN;

	/* Select ADC channel */
	buf[5] = prop->channel;

	/* Select HW settle delay for channel */
	buf[6] &= (u8) ~ADC5_GEN3_HW_SETTLE_DELAY_MASK;
	buf[6] |= prop->hw_settle_time;

	reinit_completion(&adc->complete);

	ret = adc5_write(adc, sdam_index, ADC5_GEN3_SID, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	conv_req = ADC5_GEN3_CONV_REQ_REQ;
	ret = adc5_write(adc, sdam_index, ADC5_GEN3_CONV_REQ, &conv_req, 1);

	return ret;
}

#define ADC5_GEN3_HS_DELAY_MIN_US		100
#define ADC5_GEN3_HS_DELAY_MAX_US		110
#define ADC5_GEN3_HS_RETRY_COUNT		150

static int adc5_gen3_poll_wait_hs(struct adc5_chip *adc,
				unsigned int sdam_index)
{
	int ret, count;
	u8 status = 0, conv_req = ADC5_GEN3_CONV_REQ_REQ;

	for (count = 0; count < ADC5_GEN3_HS_RETRY_COUNT; count++) {
		ret = adc5_read(adc, sdam_index, ADC5_GEN3_HS, &status, 1);
		if (ret < 0)
			return ret;

		if (status == ADC5_GEN3_HS_READY) {
			ret = adc5_read(adc, sdam_index, ADC5_GEN3_CONV_REQ,
					&conv_req, 1);
			if (ret < 0)
				return ret;

			if (!conv_req)
				break;
		}

		usleep_range(ADC5_GEN3_HS_DELAY_MIN_US,
			ADC5_GEN3_HS_DELAY_MAX_US);
	}

	if (count == ADC5_GEN3_HS_RETRY_COUNT) {
		pr_err("Setting HS ready bit timed out, status:%#x\n", status);
		return -ETIMEDOUT;
	}

	return 0;
}

#define ADC5_GEN3_CONV_TIMEOUT_MS	50
#define ADC5_GEN3_POLL_ATTEMPTS		10

static int adc5_gen3_do_conversion(struct adc5_chip *adc,
			struct adc5_channel_prop *prop,
			u16 *data_volt)
{
	int ret, i;
	bool poll_eoc = false;
	unsigned long rc;
	unsigned int time_pending_ms;
	u8 val, eoc_status, sdam_index = prop->sdam_index;

	/* Reserve channel 0 of first SDAM for immediate conversions */
	if (prop->adc_tm)
		sdam_index = 0;

	mutex_lock(&adc->lock);
	ret = adc5_gen3_poll_wait_hs(adc, 0);
	if (ret < 0)
		goto unlock;

	ret = adc5_gen3_configure(adc, prop);
	if (ret < 0) {
		pr_err("ADC configure failed with %d\n", ret);
		goto unlock;
	}

	for (i = 0; i < ADC5_GEN3_POLL_ATTEMPTS; i++) {
		/* Trying both polling and waiting for interrupt */
		rc = wait_for_completion_timeout(&adc->complete,
						msecs_to_jiffies(ADC5_GEN3_CONV_TIMEOUT_MS));
		if (rc) {
			pr_debug("Got EOC interrupt after %d polling attempts\n", i);
			break;
		}

		/* CHAN0 is the preconfigured channel for immediate conversion */
		ret = adc5_read(adc, 0, ADC5_GEN3_EOC_STS, &eoc_status, 1);
		if (ret < 0) {
			pr_err("adc read eoc status failed with %d\n", ret);
			goto unlock;
		}

		if (eoc_status & ADC5_GEN3_EOC_CHAN_0) {
			poll_eoc = true;
			break;
		}
	}

	if (!rc && !poll_eoc) {
		pr_err("Reading ADC channel %s timed out\n",
			prop->datasheet_name);
		adc5_gen3_dump_regs_debug(adc);
		ret = -ETIMEDOUT;
		goto unlock;
	}

	time_pending_ms = jiffies_to_msecs(rc);
	pr_debug("ADC channel %s EOC took %u ms\n", prop->datasheet_name,
		(i + 1) * ADC5_GEN3_CONV_TIMEOUT_MS - time_pending_ms);

	ret = adc5_gen3_read_voltage_data(adc, data_volt, sdam_index);
	if (ret < 0)
		goto unlock;

	val = BIT(0);
	ret = adc5_write(adc, sdam_index, ADC5_GEN3_EOC_CLR, &val, 1);
	if (ret < 0)
		goto unlock;

	/* To indicate conversion request is only to clear a status */
	val = 0;
	ret = adc5_write(adc, sdam_index, ADC5_GEN3_PERPH_CH, &val, 1);
	if (ret < 0)
		goto unlock;

	val = ADC5_GEN3_CONV_REQ_REQ;
	ret = adc5_write(adc, sdam_index, ADC5_GEN3_CONV_REQ, &val, 1);

unlock:
	mutex_unlock(&adc->lock);

	return ret;
}

static int get_sdam_from_irq(struct adc5_chip *adc, int irq)
{
	int i;

	for (i = 0; i < adc->num_sdams; i++) {
		if (adc->base[i].irq == irq)
			return i;
	}
	return -ENOENT;
}

static irqreturn_t adc5_gen3_isr(int irq, void *dev_id)
{
	struct adc5_chip *adc = dev_id;
	u8 status, tm_status[2], eoc_status, val;
	int ret, sdam_num;

	sdam_num = get_sdam_from_irq(adc, irq);
	if (sdam_num < 0) {
		pr_err("adc irq %d not associated with an sdam\n", irq);
		goto handler_end;
	}

	ret = adc5_read(adc, sdam_num, ADC5_GEN3_EOC_STS, &eoc_status, 1);
	if (ret < 0) {
		pr_err("adc read eoc status failed with %d\n", ret);
		goto handler_end;
	}

	/* CHAN0 is the preconfigured channel for immediate conversion */
	if (eoc_status & ADC5_GEN3_EOC_CHAN_0)
		complete(&adc->complete);

	ret = adc5_read(adc, sdam_num, ADC5_GEN3_TM_HIGH_STS, tm_status, 2);
	if (ret < 0) {
		pr_err("adc read TM status failed with %d\n", ret);
		goto handler_end;
	}

	if (tm_status[0] || tm_status[1])
		schedule_work(&adc->tm_handler_work);

	ret = adc5_read(adc, sdam_num, ADC5_GEN3_STATUS1, &status, 1);
	if (ret < 0) {
		pr_err("adc read status1 failed with %d\n", ret);
		goto handler_end;
	}

	pr_debug("Interrupt status:%#x, EOC status:%#x, high:%#x, low:%#x\n",
			status, eoc_status, tm_status[0], tm_status[1]);

	if (status & ADC5_GEN3_STATUS1_CONV_FAULT) {
		pr_err_ratelimited("Unexpected conversion fault, status:%#x, eoc_status:%#x\n",
					status, eoc_status);
		adc5_gen3_dump_regs_debug(adc);

		val = ADC5_GEN3_CONV_ERR_CLR_REQ;
		ret = adc5_write(adc, sdam_num, ADC5_GEN3_CONV_ERR_CLR, &val, 1);
		if (ret < 0)
			goto handler_end;

		/* To indicate conversion request is only to clear a status */
		val = 0;
		ret = adc5_write(adc, sdam_num, ADC5_GEN3_PERPH_CH, &val, 1);
		if (ret < 0)
			goto handler_end;

		val = ADC5_GEN3_CONV_REQ_REQ;
		ret = adc5_write(adc, sdam_num, ADC5_GEN3_CONV_REQ, &val, 1);
		if (ret < 0)
			goto handler_end;
	}

	return IRQ_HANDLED;

handler_end:
	return IRQ_NONE;
}

static void tm_handler_work(struct work_struct *work)
{
	struct adc5_channel_prop *chan_prop;
	u8 tm_status[2] = {0};
	u8 buf[16] = {0};
	u8 val;
	int ret, i, sdam_index = -1;
	struct adc5_chip *adc = container_of(work, struct adc5_chip,
						tm_handler_work);

	for (i = 0; i < adc->nchannels; i++) {
		bool upper_set = false, lower_set = false;
		u8 data_low = 0, data_high = 0;
		u16 code = 0;
		int temp, offset;

		chan_prop = &adc->chan_props[i];
		offset = chan_prop->tm_chan_index;

		if (chan_prop->adc_tm != ADC_TM && chan_prop->adc_tm != ADC_TM_NON_THERMAL)
			continue;

		mutex_lock(&adc->lock);
		if (chan_prop->sdam_index != sdam_index) {
			sdam_index = chan_prop->sdam_index;
			ret = adc5_read(adc, sdam_index, ADC5_GEN3_TM_HIGH_STS, tm_status, 2);
			if (ret < 0) {
				pr_err("adc read TM status failed with %d\n", ret);
				goto work_unlock;
			}

			ret = adc5_write(adc, sdam_index, ADC5_GEN3_TM_HIGH_STS_CLR, tm_status, 2);
			if (ret < 0) {
				pr_err("adc write TM status failed with %d\n", ret);
				goto work_unlock;
			}

			/* To indicate conversion request is only to clear a status */
			val = 0;
			ret = adc5_write(adc, sdam_index, ADC5_GEN3_PERPH_CH, &val, 1);
			if (ret < 0) {
				pr_err("adc write status clear conv_req failed with %d\n", ret);
				goto work_unlock;
			}

			val = ADC5_GEN3_CONV_REQ_REQ;
			ret = adc5_write(adc, sdam_index, ADC5_GEN3_CONV_REQ, &val, 1);
			if (ret < 0) {
				pr_err("adc write conv_req failed with %d\n", ret);
				goto work_unlock;
			}

			ret = adc5_read(adc, sdam_index, ADC5_GEN3_CH0_DATA0, buf, sizeof(buf));
			if (ret < 0) {
				pr_err("adc read data failed with %d\n", ret);
				goto work_unlock;
			}
		}

		if ((tm_status[0] & BIT(offset)) && (chan_prop->high_thr_en))
			upper_set = true;

		if ((tm_status[1] & BIT(offset)) && (chan_prop->low_thr_en))
			lower_set = true;

		mutex_unlock(&adc->lock);

		if (!(upper_set || lower_set))
			continue;

		data_low = buf[2 * offset];
		data_high = buf[2 * offset + 1];
		code = ((data_high << 8) | data_low);
		pr_debug("ADC_TM threshold code:0x%x\n", code);

		if (chan_prop->adc_tm == ADC_TM_NON_THERMAL) {
			if (lower_set) {
				mutex_lock(&adc->lock);
				chan_prop->low_thr_en = 0;
				chan_prop->low_thr_triggered = true;
				mutex_unlock(&adc->lock);
				queue_work(chan_prop->req_wq,
						&chan_prop->work);
			}

			if (upper_set) {
				mutex_lock(&adc->lock);
				chan_prop->high_thr_en = 0;
				chan_prop->high_thr_triggered = true;
				mutex_unlock(&adc->lock);
				queue_work(chan_prop->req_wq,
						&chan_prop->work);
			}
		} else {
			ret = qcom_adc5_hw_scale(chan_prop->scale_fn_type,
				chan_prop->prescale, adc->data, code, &temp);

			if (ret < 0) {
				pr_err("Invalid temperature reading, ret=%d, code=0x%x\n",
						ret, code);
				continue;
			}
			pr_debug("notifying thermal, temp:%d\n", temp);
			chan_prop->last_temp = temp;
			chan_prop->last_temp_set = true;
			thermal_zone_device_update(chan_prop->tzd, THERMAL_TRIP_VIOLATED);
		}
	}

	return;

work_unlock:
	mutex_unlock(&adc->lock);
}

static int adc5_gen3_fwnode_xlate(struct iio_dev *indio_dev,
				const struct fwnode_reference_args *iiospec)
{
	struct adc5_chip *adc = iio_priv(indio_dev);
	int i, v_channel;

	for (i = 0; i < adc->nchannels; i++) {
		v_channel = V_CHAN(adc->chan_props[i]);
		if (v_channel == iiospec->args[0])
			return i;
	}

	return -ENOENT;
}

static int adc5_gen3_read_raw(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan, int *val, int *val2,
			 long mask)
{
	struct adc5_chip *adc = iio_priv(indio_dev);
	struct adc5_channel_prop *prop;
	u16 adc_code_volt;
	int ret;

	prop = &adc->chan_props[chan->address];

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		ret = adc5_gen3_do_conversion(adc, prop,
					&adc_code_volt);
		if (ret < 0)
			return ret;

		ret = qcom_adc5_hw_scale(prop->scale_fn_type,
			prop->prescale, adc->data,
			adc_code_volt, val);
		if (ret < 0)
			return ret;

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_RAW:
		ret = adc5_gen3_do_conversion(adc, prop,
					&adc_code_volt);
		if (ret < 0)
			return ret;

		*val = (int)adc_code_volt;
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}

	return 0;
}

static const struct iio_info adc5_gen3_info = {
	.read_raw = adc5_gen3_read_raw,
	.fwnode_xlate = adc5_gen3_fwnode_xlate,
};

/* Used by thermal clients to read ADC channel temperature */
int adc_tm_gen3_get_temp(struct thermal_zone_device *tz, int *temp)
{
	int ret;
	struct adc5_channel_prop *prop = tz->devdata;
	struct adc5_chip *adc;
	u16 adc_code_volt;

	if (!prop || !prop->chip)
		return -EINVAL;

	adc = prop->chip;

	if (prop->last_temp_set) {
		pr_debug("last_temp: %d\n", prop->last_temp);
		prop->last_temp_set = false;
		*temp = prop->last_temp;
		return 0;
	}

	ret = adc5_gen3_do_conversion(adc, prop, &adc_code_volt);
	if (ret < 0)
		return ret;

	ret = qcom_adc5_hw_scale(prop->scale_fn_type,
		prop->prescale, adc->data,
		adc_code_volt, temp);

	/* Save temperature data to minidump */
	if (prop->chip->adc_md && prop->tzd)
		thermal_minidump_update_data(prop->chip->adc_md,
			prop->tzd->type, temp);

	return ret;
}

static int adc_tm5_gen3_configure(struct adc5_channel_prop *prop)
{
	int ret;
	u8 conv_req = 0, buf[12];
	u32 mask = 0;
	struct adc5_chip *adc = prop->chip;

	ret = adc5_gen3_poll_wait_hs(adc, prop->sdam_index);
	if (ret < 0)
		return ret;

	ret = adc5_read(adc, prop->sdam_index, ADC5_GEN3_SID, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	/* Write SID */
	buf[0] = prop->sid & ADC5_GEN3_SID_MASK;

	/*
	 * Select TM channel and indicate there is an actual
	 * conversion request
	 */
	buf[1] = ADC5_GEN3_CHAN_CONV_REQ | prop->tm_chan_index;

	buf[2] = prop->timer;

	/* Digital param selection */
	adc5_gen3_update_dig_param(adc, prop, &buf[3]);

	/* Update fast average sample value */
	buf[4] &= (u8) ~ADC5_GEN3_FAST_AVG_CTL_SAMPLES_MASK;
	buf[4] |= prop->avg_samples | ADC5_GEN3_FAST_AVG_CTL_EN;

	/* Select ADC channel */
	buf[5] = prop->channel;

	/* Select HW settle delay for channel */
	buf[6] &= (u8) ~ADC5_GEN3_HW_SETTLE_DELAY_MASK;
	buf[6] |= prop->hw_settle_time;

	buf[7] = prop->high_thr_en << 1 | prop->low_thr_en;

	mask = lower_32_bits(prop->low_thr_voltage);
	buf[8] = ADC_TM5_GEN3_LOWER_MASK(mask);
	buf[9] = ADC_TM5_GEN3_UPPER_MASK(mask);

	mask = lower_32_bits(prop->high_thr_voltage);
	buf[10] = ADC_TM5_GEN3_LOWER_MASK(mask);
	buf[11] = ADC_TM5_GEN3_UPPER_MASK(mask);

	ret = adc5_write(adc, prop->sdam_index, ADC5_GEN3_SID, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	conv_req = ADC5_GEN3_CONV_REQ_REQ;
	return adc5_write(adc, prop->sdam_index, ADC5_GEN3_CONV_REQ, &conv_req, 1);
}

static int adc_tm5_gen3_set_trip_temp(struct thermal_zone_device *tz,
					int low_temp, int high_temp)
{
	struct adc5_channel_prop *prop = tz->devdata;
	struct adc5_chip *adc;
	struct adc_tm_config tm_config;
	int ret;

	if (!prop)
		return -EINVAL;

	pr_debug("channel:%s :low_temp(mdegC):%d, high_temp(mdegC):%d\n",
		prop->datasheet_name, low_temp, high_temp);

	adc = prop->chip;
	tm_config.high_thr_temp = tm_config.low_thr_temp = 0;
	if (high_temp != INT_MAX)
		tm_config.high_thr_temp = high_temp;
	if (low_temp != INT_MIN)
		tm_config.low_thr_temp = low_temp;

	if ((high_temp == INT_MAX) && (low_temp == INT_MIN)) {
		pr_err("No trips to set\n");
		return -EINVAL;
	}

	pr_debug("requested a low temp- %d and high temp- %d\n",
			tm_config.low_thr_temp, tm_config.high_thr_temp);

	adc_tm_scale_therm_voltage_100k_gen3(&tm_config);

	/*
	 * Thresholds are forward scaled to confirm their
	 * temperatures will actually cause a violation, before
	 * being written.
	 */
	pr_debug("high_thr:0x%llx, low_thr:0x%llx\n",
		tm_config.high_thr_voltage, tm_config.low_thr_voltage);

	mutex_lock(&adc->lock);

	if (high_temp != INT_MAX) {
		prop->low_thr_voltage = tm_config.low_thr_voltage;
		prop->low_thr_en = 1;
	} else {
		prop->low_thr_en = 0;
	}

	if (low_temp > -INT_MAX) {
		prop->high_thr_voltage = tm_config.high_thr_voltage;
		prop->high_thr_en = 1;
	} else {
		prop->high_thr_en = 0;
	}

	ret = adc_tm5_gen3_configure(prop);
	if (ret < 0)
		pr_err("Error during adc-tm configure:%d\n", ret);

	mutex_unlock(&adc->lock);

	return ret;
}

#define MAX_PROP_NAME_LEN		32
struct adc5_chip *get_adc_tm_gen3(struct device *dev, const char *name)
{
	struct platform_device *pdev;
	struct adc5_chip *chip;
	struct device_node *node = NULL;
	char prop_name[MAX_PROP_NAME_LEN];

	scnprintf(prop_name, MAX_PROP_NAME_LEN, "qcom,%s-adc_tm", name);

	node = of_parse_phandle(dev->of_node, prop_name, 0);
	if (node == NULL)
		return ERR_PTR(-ENODEV);

	list_for_each_entry(chip, &adc_tm_device_list, list) {
		pdev = to_platform_device(chip->dev);
		if (pdev->dev.of_node == node) {
			of_node_put(node);
			return chip;
		}
	}
	of_node_put(node);
	return ERR_PTR(-EPROBE_DEFER);
}
EXPORT_SYMBOL(get_adc_tm_gen3);

static int32_t adc_tm_add_to_list(struct adc5_chip *chip,
				uint32_t dt_index,
				struct adc_tm_param *param)
{
	struct adc_tm_client_info *client_info = NULL;
	bool client_info_exists = false;

	list_for_each_entry(client_info,
			&chip->chan_props[dt_index].thr_list, list) {
		if (client_info->param->id == param->id) {
			client_info->low_thr_requested = param->low_thr;
			client_info->high_thr_requested = param->high_thr;
			client_info->state_request = param->state_request;
			client_info->notify_low_thr = false;
			client_info->notify_high_thr = false;
			client_info_exists = true;
		}
	}

	if (!client_info_exists) {
		client_info = devm_kzalloc(chip->dev,
			sizeof(struct adc_tm_client_info), GFP_KERNEL);
		if (!client_info)
			return -ENOMEM;

		client_info->param->id = (uintptr_t) client_info;
		client_info->low_thr_requested = param->low_thr;
		client_info->high_thr_requested = param->high_thr;
		client_info->state_request = param->state_request;

		list_add_tail(&client_info->list,
					&chip->chan_props[dt_index].thr_list);
	}
	return 0;
}

static int32_t adc_tm_gen3_manage_thresholds(struct adc5_channel_prop *prop)
{
	int high_thr = INT_MAX, low_thr = INT_MIN;
	struct adc_tm_client_info *client_info = NULL;
	struct list_head *thr_list;
	uint32_t scale_type = 0;
	struct adc_tm_config tm_config;

	prop->high_thr_en = 0;
	prop->low_thr_en = 0;

	/*
	 * Reset the high_thr_set and low_thr_set of all
	 * clients since the thresholds will be recomputed.
	 */
	list_for_each(thr_list, &prop->thr_list) {
		client_info = list_entry(thr_list,
					struct adc_tm_client_info, list);
		client_info->high_thr_set = false;
		client_info->low_thr_set = false;
	}

	/* Find the min of high_thr and max of low_thr */
	list_for_each(thr_list, &prop->thr_list) {
		client_info = list_entry(thr_list,
					struct adc_tm_client_info, list);

		if ((client_info->state_request == ADC_TM_HIGH_THR_ENABLE) ||
			(client_info->state_request ==
				ADC_TM_HIGH_LOW_THR_ENABLE))
			if (client_info->high_thr_requested < high_thr)
				high_thr = client_info->high_thr_requested;

		if ((client_info->state_request == ADC_TM_LOW_THR_ENABLE) ||
			(client_info->state_request ==
				ADC_TM_HIGH_LOW_THR_ENABLE))
			if (client_info->low_thr_requested > low_thr)
				low_thr = client_info->low_thr_requested;

		pr_debug("threshold compared is high:%d and low:%d\n",
				client_info->high_thr_requested,
				client_info->low_thr_requested);
		pr_debug("current threshold is high:%d and low:%d\n",
							high_thr, low_thr);
	}

	/* Check which of the high_thr and low_thr got set */
	list_for_each(thr_list, &prop->thr_list) {
		client_info = list_entry(thr_list,
					struct adc_tm_client_info, list);

		if ((client_info->state_request == ADC_TM_HIGH_THR_ENABLE) ||
			(client_info->state_request ==
				ADC_TM_HIGH_LOW_THR_ENABLE))
			if (high_thr == client_info->high_thr_requested) {
				prop->high_thr_en = 1;
				client_info->high_thr_set = true;
			}

		if ((client_info->state_request == ADC_TM_LOW_THR_ENABLE) ||
			(client_info->state_request ==
				ADC_TM_HIGH_LOW_THR_ENABLE))
			if (low_thr == client_info->low_thr_requested) {
				prop->low_thr_en = 1;
				client_info->low_thr_set = true;
			}
	}

	tm_config.high_thr_voltage = (int64_t)high_thr;
	tm_config.low_thr_voltage = (int64_t)low_thr;

	scale_type = prop->adc_rscale_fn;
	if (scale_type >= SCALE_RSCALE_NONE)
		return -EBADF;

	adc_tm_rscale_fn[scale_type].chan(&tm_config);

	prop->low_thr_voltage = tm_config.low_thr_voltage;
	prop->high_thr_voltage = tm_config.high_thr_voltage;

	pr_debug("threshold written is high:%d and low:%d\n",
							high_thr, low_thr);

	return 0;
}

/* Used to notify non-thermal clients of threshold crossing */
void notify_adc_tm_fn(struct work_struct *work)
{
	struct adc5_channel_prop *prop = container_of(work,
		struct adc5_channel_prop, work);
	struct adc_tm_client_info *client_info = NULL;
	struct adc5_chip *chip;
	struct list_head *thr_list;
	int ret;

	chip = prop->chip;

	mutex_lock(&chip->lock);
	if (prop->low_thr_triggered) {
		/* adjust thr, calling manage_thr */
		list_for_each(thr_list, &prop->thr_list) {
			client_info = list_entry(thr_list,
					struct adc_tm_client_info, list);
			if (client_info->low_thr_set) {
				client_info->low_thr_set = false;
				client_info->notify_low_thr = true;
				if (client_info->state_request ==
						ADC_TM_HIGH_LOW_THR_ENABLE)
					client_info->state_request =
							ADC_TM_HIGH_THR_ENABLE;
				else
					client_info->state_request =
							ADC_TM_LOW_THR_DISABLE;
			}
		}
		prop->low_thr_triggered = false;
	}

	if (prop->high_thr_triggered) {
		/* adjust thr, calling manage_thr */
		list_for_each(thr_list, &prop->thr_list) {
			client_info = list_entry(thr_list,
					struct adc_tm_client_info, list);
			if (client_info->high_thr_set) {
				client_info->high_thr_set = false;
				client_info->notify_high_thr = true;
				if (client_info->state_request ==
						ADC_TM_HIGH_LOW_THR_ENABLE)
					client_info->state_request =
							ADC_TM_LOW_THR_ENABLE;
				else
					client_info->state_request =
							ADC_TM_HIGH_THR_DISABLE;
			}
		}
		prop->high_thr_triggered = false;
	}
	ret = adc_tm_gen3_manage_thresholds(prop);
	if (ret < 0)
		pr_err("Error in reverse scaling:%d\n", ret);

	ret = adc_tm5_gen3_configure(prop);
	if (ret < 0)
		pr_err("Error during adc-tm configure:%d\n", ret);

	mutex_unlock(&chip->lock);

	list_for_each_entry(client_info, &prop->thr_list, list) {
		if (client_info->notify_low_thr &&
				client_info->param->threshold_notification != NULL) {
			pr_debug("notify kernel with low state for channel 0x%x\n",
					prop->channel);
			client_info->param->threshold_notification(
				ADC_TM_LOW_STATE,
				client_info->param->btm_ctx);
			client_info->notify_low_thr = false;
		}

		if (client_info->notify_high_thr &&
				client_info->param->threshold_notification != NULL) {
			pr_debug("notify kernel with high state for channel 0x%x\n",
					prop->channel);
			client_info->param->threshold_notification(
				ADC_TM_HIGH_STATE,
				client_info->param->btm_ctx);
			client_info->notify_high_thr = false;
		}
	}
}

/* Used by non-thermal clients to configure an ADC_TM channel */
int32_t adc_tm_channel_measure_gen3(struct adc5_chip *chip,
					struct adc_tm_param *param)

{
	int ret, i;
	uint32_t v_channel, dt_index = 0;
	bool chan_found = false;

	if (param == NULL)
		return -EINVAL;

	if (param->threshold_notification == NULL) {
		pr_debug("No notification for high/low temp\n");
		return -EINVAL;
	}

	for (i = 0; i < chip->nchannels; i++) {
		v_channel = V_CHAN(chip->chan_props[i]);
		if (v_channel == param->channel) {
			dt_index = i;
			chan_found = true;
			break;
		}
	}

	if (!chan_found)  {
		pr_err("not a valid ADC_TM channel\n");
		return -EINVAL;
	}

	mutex_lock(&chip->lock);

	/* add channel client to channel list */
	adc_tm_add_to_list(chip, dt_index, param);

	/* set right thresholds for the sensor */
	ret = adc_tm_gen3_manage_thresholds(&chip->chan_props[dt_index]);
	if (ret < 0)
		pr_err("Error in reverse scaling:%d\n", ret);

	/* configure channel */
	ret = adc_tm5_gen3_configure(&chip->chan_props[dt_index]);
	if (ret < 0) {
		pr_err("Error during adc-tm configure:%d\n", ret);
		goto fail_unlock;
	}

fail_unlock:
	mutex_unlock(&chip->lock);
	return ret;
}
EXPORT_SYMBOL(adc_tm_channel_measure_gen3);

/* Used by non-thermal clients to release an ADC_TM channel */
int32_t adc_tm_disable_chan_meas_gen3(struct adc5_chip *chip,
					struct adc_tm_param *param)
{
	int ret = 0, i;
	uint32_t dt_index = 0, v_channel;
	struct adc_tm_client_info *client_info = NULL;

	if (param == NULL)
		return -EINVAL;

	for (i = 0; i < chip->nchannels; i++) {
		v_channel = V_CHAN(chip->chan_props[i]);
		if (v_channel == param->channel) {
			dt_index = i;
			break;
		}
	}

	if (i == chip->nchannels)  {
		pr_err("not a valid ADC_TM channel\n");
		return -EINVAL;
	}

	mutex_lock(&chip->lock);
	list_for_each_entry(client_info,
			&chip->chan_props[i].thr_list, list) {
		if (client_info->param->id == param->id) {
			client_info->state_request =
				ADC_TM_HIGH_LOW_THR_DISABLE;
			ret = adc_tm_gen3_manage_thresholds(&chip->chan_props[i]);
			if (ret < 0) {
				pr_err("Error in reverse scaling:%d\n",
						ret);
				goto fail;
			}
			ret = adc_tm5_gen3_configure(&chip->chan_props[i]);
			if (ret < 0) {
				pr_err("Error during adc-tm configure:%d\n",
						ret);
				goto fail;
			}
		}
	}

fail:
	mutex_unlock(&chip->lock);
	return ret;
}
EXPORT_SYMBOL(adc_tm_disable_chan_meas_gen3);

static struct thermal_zone_device_ops adc_tm_ops = {
	.get_temp = adc_tm_gen3_get_temp,
	.set_trips = adc_tm5_gen3_set_trip_temp,
};

static struct thermal_zone_device_ops adc_tm_ops_iio = {
	.get_temp = adc_tm_gen3_get_temp,
};

static int adc_tm_register_tzd(struct adc5_chip *adc)
{
	unsigned int i, channel;
	struct thermal_zone_device *tzd;

	for (i = 0; i < adc->nchannels; i++) {
		channel = V_CHAN(adc->chan_props[i]);

		switch (adc->chan_props[i].adc_tm) {
		case ADC_TM_NONE:
			continue;
		case ADC_TM:
			tzd = devm_thermal_of_zone_register(
				adc->dev, channel,
				&adc->chan_props[i], &adc_tm_ops);
			break;
		case ADC_TM_IIO:
			tzd = devm_thermal_of_zone_register(
				adc->dev, channel,
				&adc->chan_props[i], &adc_tm_ops_iio);
			break;
		case ADC_TM_NON_THERMAL:
			tzd = NULL;
			break;
		default:
			pr_err("Invalid ADC_TM type:%d for dt_ch:%d\n",
					adc->chan_props[i].adc_tm, adc->chan_props[i].channel);
			return -EINVAL;
		}

		if (IS_ERR(tzd)) {
			pr_err("Error registering TZ zone:%ld for dt_ch:%d\n",
				PTR_ERR(tzd), adc->chan_props[i].channel);
			return PTR_ERR(tzd);
		}
		adc->chan_props[i].tzd = tzd;
	}

	return 0;
}

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
	[ADC5_GEN3_AMUX4_THM]		= ADC5_CHAN_VOLT("pot_res_r", 0,
						SCALE_HW_CALIB_DEFAULT)
	[ADC5_GEN3_AMUX5_THM]		= ADC5_CHAN_VOLT("pot_res", 0,
						SCALE_HW_CALIB_DEFAULT)
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

static int adc5_get_dt_channel_data(struct adc5_chip *adc,
				    struct adc5_channel_prop *prop,
				    struct device_node *node,
				    const struct adc5_data *data)
{
	const char *name = node->name, *channel_name;
	u32 chan, value, varr[2];
	u32 sid = 0;
	int ret, val;
	struct device *dev = adc->dev;

	ret = of_property_read_u32(node, "reg", &chan);
	if (ret < 0) {
		dev_err(dev, "invalid channel number %s\n", name);
		return ret;
	}

	/*
	 * Value read from "reg" is virtual channel number
	 * virtual channel number = (sid << 8 | channel number).
	 */
	sid = (chan >> ADC5_GEN3_SID_OFFSET);
	chan = (chan & ADC5_GEN3_CHANNEL_MASK);

	if (chan > ADC5_OFFSET_EXT2 ||
	    !data->adc_chans[chan].datasheet_name) {
		dev_err(dev, "%s invalid channel number %d\n", name, chan);
		return -EINVAL;
	}

	/* the channel has DT description */
	prop->channel = chan;
	prop->sid = sid;

	channel_name = of_get_property(node,
				"label", NULL) ? : node->name;
	if (!channel_name) {
		pr_err("Invalid channel name\n");
		return -EINVAL;
	}
	prop->datasheet_name = channel_name;

	ret = of_property_read_u32(node, "qcom,decimation", &value);
	if (!ret) {
		ret = adc5_decimation_from_dt(value, data->decimation);
		if (ret < 0) {
			dev_err(dev, "%02x invalid decimation %d\n",
				chan, value);
			return ret;
		}
		prop->decimation = ret;
	} else {
		prop->decimation = ADC5_DECIMATION_DEFAULT;
	}

	ret = of_property_read_u32_array(node, "qcom,pre-scaling", varr, 2);
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

	ret = of_property_read_u32(node, "qcom,hw-settle-time", &value);
	if (!ret) {
		ret = adc5_hw_settle_time_from_dt(value,
						data->hw_settle_1);
		if (ret < 0) {
			dev_err(dev, "%02x invalid hw-settle-time %d us\n",
				chan, value);
			return ret;
		}
		prop->hw_settle_time = ret;
	} else {
		prop->hw_settle_time = VADC_DEF_HW_SETTLE_TIME;
	}

	ret = of_property_read_u32(node, "qcom,avg-samples", &value);
	if (!ret) {
		ret = adc5_avg_samples_from_dt(value);
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
	ret = of_property_read_u32(node, "qcom,scale-fn-type", &value);
	if (!ret && value < SCALE_HW_CALIB_INVALID)
		prop->scale_fn_type = value;

	if (of_property_read_bool(node, "qcom,ratiometric"))
		prop->cal_method = ADC5_RATIOMETRIC_CAL;
	else
		prop->cal_method = ADC5_ABSOLUTE_CAL;

	prop->timer = MEAS_INT_IMMEDIATE;
	prop->adc_tm = ADC_TM_NONE;

	ret = of_property_read_u32(node, "qcom,adc-tm-type", &value);
	if (!ret && value < ADC_TM_INVALID)
		prop->adc_tm = value;

	if (prop->adc_tm == ADC_TM_NON_THERMAL) {
		ret = of_property_read_u32(node, "qcom,rscale-type",
							&prop->adc_rscale_fn);
		if (ret < 0)
			prop->adc_rscale_fn = SCALE_RSCALE_NONE;
	}

	if (prop->adc_tm && prop->adc_tm != ADC_TM_IIO) {
		adc->n_tm_channels++;
		if (adc->n_tm_channels > ((adc->num_sdams * 8) - 1)) {
			pr_err("Number of TM nodes %u greater than channels supported:%u\n",
						adc->n_tm_channels, (adc->num_sdams * 8) - 1);
			return -EINVAL;
		}

		val = adc->n_tm_channels / 8;
		prop->sdam_index = val;
		prop->tm_chan_index = adc->n_tm_channels - (8*val);

		prop->timer = MEAS_INT_1S;

		if (prop->adc_tm == ADC_TM_NON_THERMAL) {
			prop->req_wq = alloc_workqueue(
				"adc_tm_notify_wq", WQ_HIGHPRI, 0);
			if (!prop->req_wq) {
				pr_err("Requesting priority wq failed\n");
				return -ENOMEM;
			}
			INIT_WORK(&prop->work, notify_adc_tm_fn);
		}
		INIT_LIST_HEAD(&prop->thr_list);
	}

	dev_dbg(dev, "%02x name %s\n", chan, name);

	return 0;
}

static const struct adc5_data adc5_gen3_data_pmic = {
	.name = "pm-adc5-gen3",
	.full_scale_code_volt = 0x70e4,
	.full_scale_code_cur = 0x2ee0,
	.adc_chans = adc5_chans_pmic,
	.decimation = (unsigned int [ADC5_DECIMATION_SAMPLES_MAX])
				{85, 340, 1360},
	.hw_settle_1 = (unsigned int [VADC_HW_SETTLE_SAMPLES_MAX])
				{15, 100, 200, 300, 400, 500, 600, 700,
				1000, 2000, 4000, 8000, 16000, 32000,
				64000, 128000},
};

static const struct of_device_id adc5_match_table[] = {
	{
		.compatible = "qcom,spmi-adc5-gen3",
		.data = &adc5_gen3_data_pmic,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, adc5_match_table);

static int adc5_get_dt_data(struct adc5_chip *adc, struct device_node *node)
{
	const struct adc5_channels *adc_chan;
	struct iio_chan_spec *iio_chan;
	struct adc5_channel_prop *chan_props;
	struct device_node *child;
	unsigned int index = 0;
	const struct of_device_id *id;
	const struct adc5_data *data;
	int ret;

	adc->nchannels = of_get_available_child_count(node);
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
	adc->n_tm_channels = 0;
	iio_chan = adc->iio_chans;
	id = of_match_node(adc5_match_table, node);
	if (id)
		data = id->data;
	else
		data = &adc5_gen3_data_pmic;
	adc->data = data;

	for_each_available_child_of_node(node, child) {
		ret = adc5_get_dt_channel_data(adc, chan_props, child, data);
		if (ret < 0) {
			of_node_put(child);
			return ret;
		}

		chan_props->chip = adc;
		if (chan_props->scale_fn_type == -EINVAL)
			chan_props->scale_fn_type =
				data->adc_chans[chan_props->channel].scale_fn_type;
		adc_chan = &data->adc_chans[chan_props->channel];
		iio_chan->channel = chan_props->channel;
		iio_chan->datasheet_name = chan_props->datasheet_name;
		iio_chan->extend_name = chan_props->datasheet_name;
		iio_chan->info_mask_separate = adc_chan->info_mask;
		iio_chan->type = adc_chan->type;
		iio_chan->address = index;
		iio_chan++;
		chan_props++;
		index++;
	}

	return 0;
}

static int adc5_gen3_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct iio_dev *indio_dev;
	struct adc5_chip *adc;
	struct regmap *regmap;
	int ret, i;
	u32 reg;
	char buf[20];

	regmap = dev_get_regmap(dev->parent, NULL);
	if (!regmap)
		return -ENODEV;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->regmap = regmap;
	adc->dev = dev;

	ret = of_property_count_u32_elems(node, "reg");
	if (ret < 0)
		return ret;

	adc->num_sdams = ret;

	adc->base = devm_kcalloc(adc->dev, adc->num_sdams, sizeof(*adc->base), GFP_KERNEL);
	if (!adc->base)
		return -ENOMEM;

	for (i = 0; i < adc->num_sdams; i++) {
		ret = of_property_read_u32_index(node, "reg", i, &reg);
		if (ret < 0)
			return ret;

		adc->base[i].base_addr = reg;

		scnprintf(buf, sizeof(buf), "adc-sdam%d", i);
		ret = of_irq_get_byname(node, buf);
		if (ret < 0) {
			pr_err("Failed to get irq for ADC5 GEN3 SDAM%d, ret=%d\n", i, ret);
			return ret;
		}
		adc->base[i].irq = ret;

		adc->base[i].irq_name = devm_kstrdup(adc->dev, buf, GFP_KERNEL);
		if (!adc->base[i].irq_name)
			return -ENOMEM;
	}

	if (!of_property_read_u32(node, "qcom,debug-base", &reg))
		adc->debug_base = reg;

	platform_set_drvdata(pdev, adc);

	indio_dev->info = &adc5_gen3_info;

	init_completion(&adc->complete);
	mutex_init(&adc->lock);

	ret = adc5_get_dt_data(adc, node);
	if (ret < 0) {
		pr_err("adc get dt data failed\n");
		goto fail;
	}

	for (i = 0; i < adc->num_sdams; i++) {
		ret = request_irq(adc->base[i].irq, adc5_gen3_isr,
					0, adc->base[i].irq_name, adc);
		if (ret < 0)
			goto irq_fail;
	}

	ret = adc_tm_register_tzd(adc);
	if (ret < 0)
		goto irq_fail;

	adc->adc_md = thermal_minidump_register("adc5_gen3");

	if (adc->n_tm_channels)
		INIT_WORK(&adc->tm_handler_work, tm_handler_work);

	indio_dev->dev.parent = dev;
	indio_dev->dev.of_node = node;
	indio_dev->name = pdev->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = adc->iio_chans;
	indio_dev->num_channels = adc->nchannels;

	list_add_tail(&adc->list, &adc_tm_device_list);
	adc->device_list = &adc_tm_device_list;

	ret = devm_iio_device_register(dev, indio_dev);
	if (!ret)
		return 0;

irq_fail:
	for (i = 0; i < adc->num_sdams; i++)
		free_irq(adc->base[i].irq, adc);
fail:
	i = 0;
	while (i < adc->nchannels) {
		if (adc->chan_props[i].req_wq)
			destroy_workqueue(adc->chan_props[i].req_wq);
		i++;
	}
	return ret;
}

static int adc5_gen3_remove(struct platform_device *pdev)
{
	struct adc5_chip *adc = platform_get_drvdata(pdev);
	u8 data = 0;
	int i, sdam_index;

	if (adc->n_tm_channels)
		cancel_work_sync(&adc->tm_handler_work);

	for (i = 0; i < adc->num_sdams; i++)
		free_irq(adc->base[i].irq, adc);

	mutex_lock(&adc->lock);
	for (i = 0; i < adc->nchannels; i++) {
		if (adc->chan_props[i].req_wq)
			destroy_workqueue(adc->chan_props[i].req_wq);
		adc->chan_props[i].timer = MEAS_INT_DISABLE;
	}

	/* Disable all available channels */
	for (i = 0; i < adc->num_sdams * 8; i++) {
		sdam_index = i / 8;

		adc5_gen3_poll_wait_hs(adc, sdam_index);

		data = MEAS_INT_DISABLE;
		adc5_write(adc, sdam_index, ADC5_GEN3_TIMER_SEL, &data, 1);

		/* To indicate there is an actual conversion request */
		data = ADC5_GEN3_CHAN_CONV_REQ | (i - (sdam_index*8));
		adc5_write(adc, sdam_index, ADC5_GEN3_PERPH_CH, &data, 1);

		data = ADC5_GEN3_CONV_REQ_REQ;
		adc5_write(adc, sdam_index, ADC5_GEN3_CONV_REQ, &data, 1);
	}

	mutex_unlock(&adc->lock);

	mutex_destroy(&adc->lock);

	list_del(&adc->list);

	thermal_minidump_unregister(adc->adc_md);

	return 0;
}

static int adc5_gen3_freeze(struct device *dev)
{
	struct adc5_chip *adc = dev_get_drvdata(dev);
	int i = 0;

	mutex_lock(&adc->lock);

	for (i = 0; i < adc->num_sdams; i++)
		free_irq(adc->base[i].irq, adc);

	mutex_unlock(&adc->lock);

	return 0;
}

static int adc5_gen3_restore(struct device *dev)
{
	struct adc5_chip *adc = dev_get_drvdata(dev);
	int i = 0;
	int ret = 0;

	for (i = 0; i < adc->num_sdams; i++) {
		ret = request_irq(adc->base[i].irq, adc5_gen3_isr,
				0, adc->base[i].irq_name, adc);
		if (ret < 0)
			return ret;
	}

	return ret;
}

static const struct dev_pm_ops adc5_gen3_pm_ops = {
	.freeze = adc5_gen3_freeze,
	.restore = adc5_gen3_restore,
};

static struct platform_driver adc5_gen3_driver = {
	.driver = {
		.name = "qcom-spmi-adc5-gen3",
		.of_match_table = adc5_match_table,
		.pm = &adc5_gen3_pm_ops,
	},
	.probe = adc5_gen3_probe,
	.remove = adc5_gen3_remove,
};
module_platform_driver(adc5_gen3_driver);

MODULE_ALIAS("platform:qcom-spmi-adc5-gen3");
MODULE_DESCRIPTION("Qualcomm Technologies Inc. PMIC5 Gen3 ADC driver");
MODULE_LICENSE("GPL v2");
