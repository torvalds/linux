// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2016 Broadcom
 */

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <linux/iio/iio.h>

/* Below Register's are common to IPROC ADC and Touchscreen IP */
#define IPROC_REGCTL1			0x00
#define IPROC_REGCTL2			0x04
#define IPROC_INTERRUPT_THRES		0x08
#define IPROC_INTERRUPT_MASK		0x0c
#define IPROC_INTERRUPT_STATUS		0x10
#define IPROC_ANALOG_CONTROL		0x1c
#define IPROC_CONTROLLER_STATUS		0x14
#define IPROC_AUX_DATA			0x20
#define IPROC_SOFT_BYPASS_CONTROL	0x38
#define IPROC_SOFT_BYPASS_DATA		0x3C

/* IPROC ADC Channel register offsets */
#define IPROC_ADC_CHANNEL_REGCTL1		0x800
#define IPROC_ADC_CHANNEL_REGCTL2		0x804
#define IPROC_ADC_CHANNEL_STATUS		0x808
#define IPROC_ADC_CHANNEL_INTERRUPT_STATUS	0x80c
#define IPROC_ADC_CHANNEL_INTERRUPT_MASK	0x810
#define IPROC_ADC_CHANNEL_DATA			0x814
#define IPROC_ADC_CHANNEL_OFFSET		0x20

/* Bit definitions for IPROC_REGCTL2 */
#define IPROC_ADC_AUXIN_SCAN_ENA	BIT(0)
#define IPROC_ADC_PWR_LDO		BIT(5)
#define IPROC_ADC_PWR_ADC		BIT(4)
#define IPROC_ADC_PWR_BG		BIT(3)
#define IPROC_ADC_CONTROLLER_EN		BIT(17)

/* Bit definitions for IPROC_INTERRUPT_MASK and IPROC_INTERRUPT_STATUS */
#define IPROC_ADC_AUXDATA_RDY_INTR	BIT(3)
#define IPROC_ADC_INTR			9
#define IPROC_ADC_INTR_MASK		(0xFF << IPROC_ADC_INTR)

/* Bit definitions for IPROC_ANALOG_CONTROL */
#define IPROC_ADC_CHANNEL_SEL		11
#define IPROC_ADC_CHANNEL_SEL_MASK	(0x7 << IPROC_ADC_CHANNEL_SEL)

/* Bit definitions for IPROC_ADC_CHANNEL_REGCTL1 */
#define IPROC_ADC_CHANNEL_ROUNDS	0x2
#define IPROC_ADC_CHANNEL_ROUNDS_MASK	(0x3F << IPROC_ADC_CHANNEL_ROUNDS)
#define IPROC_ADC_CHANNEL_MODE		0x1
#define IPROC_ADC_CHANNEL_MODE_MASK	(0x1 << IPROC_ADC_CHANNEL_MODE)
#define IPROC_ADC_CHANNEL_MODE_TDM	0x1
#define IPROC_ADC_CHANNEL_MODE_SNAPSHOT 0x0
#define IPROC_ADC_CHANNEL_ENABLE	0x0
#define IPROC_ADC_CHANNEL_ENABLE_MASK	0x1

/* Bit definitions for IPROC_ADC_CHANNEL_REGCTL2 */
#define IPROC_ADC_CHANNEL_WATERMARK	0x0
#define IPROC_ADC_CHANNEL_WATERMARK_MASK \
		(0x3F << IPROC_ADC_CHANNEL_WATERMARK)

#define IPROC_ADC_WATER_MARK_LEVEL	0x1

/* Bit definitions for IPROC_ADC_CHANNEL_STATUS */
#define IPROC_ADC_CHANNEL_DATA_LOST		0x0
#define IPROC_ADC_CHANNEL_DATA_LOST_MASK	\
		(0x0 << IPROC_ADC_CHANNEL_DATA_LOST)
#define IPROC_ADC_CHANNEL_VALID_ENTERIES	0x1
#define IPROC_ADC_CHANNEL_VALID_ENTERIES_MASK	\
		(0xFF << IPROC_ADC_CHANNEL_VALID_ENTERIES)
#define IPROC_ADC_CHANNEL_TOTAL_ENTERIES	0x9
#define IPROC_ADC_CHANNEL_TOTAL_ENTERIES_MASK	\
		(0xFF << IPROC_ADC_CHANNEL_TOTAL_ENTERIES)

/* Bit definitions for IPROC_ADC_CHANNEL_INTERRUPT_MASK */
#define IPROC_ADC_CHANNEL_WTRMRK_INTR			0x0
#define IPROC_ADC_CHANNEL_WTRMRK_INTR_MASK		\
		(0x1 << IPROC_ADC_CHANNEL_WTRMRK_INTR)
#define IPROC_ADC_CHANNEL_FULL_INTR			0x1
#define IPROC_ADC_CHANNEL_FULL_INTR_MASK		\
		(0x1 << IPROC_ADC_IPROC_ADC_CHANNEL_FULL_INTR)
#define IPROC_ADC_CHANNEL_EMPTY_INTR			0x2
#define IPROC_ADC_CHANNEL_EMPTY_INTR_MASK		\
		(0x1 << IPROC_ADC_CHANNEL_EMPTY_INTR)

#define IPROC_ADC_WATER_MARK_INTR_ENABLE		0x1

/* Number of time to retry a set of the interrupt mask reg */
#define IPROC_ADC_INTMASK_RETRY_ATTEMPTS		10

#define IPROC_ADC_READ_TIMEOUT        (HZ*2)

#define iproc_adc_dbg_reg(dev, priv, reg) \
do { \
	u32 val; \
	regmap_read(priv->regmap, reg, &val); \
	dev_dbg(dev, "%20s= 0x%08x\n", #reg, val); \
} while (0)

struct iproc_adc_priv {
	struct regmap *regmap;
	struct clk *adc_clk;
	struct mutex mutex;
	int  irqno;
	int chan_val;
	int chan_id;
	struct completion completion;
};

static void iproc_adc_reg_dump(struct iio_dev *indio_dev)
{
	struct device *dev = &indio_dev->dev;
	struct iproc_adc_priv *adc_priv = iio_priv(indio_dev);

	iproc_adc_dbg_reg(dev, adc_priv, IPROC_REGCTL1);
	iproc_adc_dbg_reg(dev, adc_priv, IPROC_REGCTL2);
	iproc_adc_dbg_reg(dev, adc_priv, IPROC_INTERRUPT_THRES);
	iproc_adc_dbg_reg(dev, adc_priv, IPROC_INTERRUPT_MASK);
	iproc_adc_dbg_reg(dev, adc_priv, IPROC_INTERRUPT_STATUS);
	iproc_adc_dbg_reg(dev, adc_priv, IPROC_CONTROLLER_STATUS);
	iproc_adc_dbg_reg(dev, adc_priv, IPROC_ANALOG_CONTROL);
	iproc_adc_dbg_reg(dev, adc_priv, IPROC_AUX_DATA);
	iproc_adc_dbg_reg(dev, adc_priv, IPROC_SOFT_BYPASS_CONTROL);
	iproc_adc_dbg_reg(dev, adc_priv, IPROC_SOFT_BYPASS_DATA);
}

static irqreturn_t iproc_adc_interrupt_thread(int irq, void *data)
{
	u32 channel_intr_status;
	u32 intr_status;
	u32 intr_mask;
	struct iio_dev *indio_dev = data;
	struct iproc_adc_priv *adc_priv = iio_priv(indio_dev);

	/*
	 * This interrupt is shared with the touchscreen driver.
	 * Make sure this interrupt is intended for us.
	 * Handle only ADC channel specific interrupts.
	 */
	regmap_read(adc_priv->regmap, IPROC_INTERRUPT_STATUS, &intr_status);
	regmap_read(adc_priv->regmap, IPROC_INTERRUPT_MASK, &intr_mask);
	intr_status = intr_status & intr_mask;
	channel_intr_status = (intr_status & IPROC_ADC_INTR_MASK) >>
				IPROC_ADC_INTR;
	if (channel_intr_status)
		return IRQ_WAKE_THREAD;

	return IRQ_NONE;
}

static irqreturn_t iproc_adc_interrupt_handler(int irq, void *data)
{
	irqreturn_t retval = IRQ_NONE;
	struct iproc_adc_priv *adc_priv;
	struct iio_dev *indio_dev = data;
	unsigned int valid_entries;
	u32 intr_status;
	u32 intr_channels;
	u32 channel_status;
	u32 ch_intr_status;

	adc_priv = iio_priv(indio_dev);

	regmap_read(adc_priv->regmap, IPROC_INTERRUPT_STATUS, &intr_status);
	dev_dbg(&indio_dev->dev, "iproc_adc_interrupt_handler(),INTRPT_STS:%x\n",
			intr_status);

	intr_channels = (intr_status & IPROC_ADC_INTR_MASK) >> IPROC_ADC_INTR;
	if (intr_channels) {
		regmap_read(adc_priv->regmap,
			    IPROC_ADC_CHANNEL_INTERRUPT_STATUS +
			    IPROC_ADC_CHANNEL_OFFSET * adc_priv->chan_id,
			    &ch_intr_status);

		if (ch_intr_status & IPROC_ADC_CHANNEL_WTRMRK_INTR_MASK) {
			regmap_read(adc_priv->regmap,
					IPROC_ADC_CHANNEL_STATUS +
					IPROC_ADC_CHANNEL_OFFSET *
					adc_priv->chan_id,
					&channel_status);

			valid_entries = ((channel_status &
				IPROC_ADC_CHANNEL_VALID_ENTERIES_MASK) >>
				IPROC_ADC_CHANNEL_VALID_ENTERIES);
			if (valid_entries >= 1) {
				regmap_read(adc_priv->regmap,
					IPROC_ADC_CHANNEL_DATA +
					IPROC_ADC_CHANNEL_OFFSET *
					adc_priv->chan_id,
					&adc_priv->chan_val);
				complete(&adc_priv->completion);
			} else {
				dev_err(&indio_dev->dev,
					"No data rcvd on channel %d\n",
					adc_priv->chan_id);
			}
			regmap_write(adc_priv->regmap,
					IPROC_ADC_CHANNEL_INTERRUPT_MASK +
					IPROC_ADC_CHANNEL_OFFSET *
					adc_priv->chan_id,
					(ch_intr_status &
					~(IPROC_ADC_CHANNEL_WTRMRK_INTR_MASK)));
		}
		regmap_write(adc_priv->regmap,
				IPROC_ADC_CHANNEL_INTERRUPT_STATUS +
				IPROC_ADC_CHANNEL_OFFSET * adc_priv->chan_id,
				ch_intr_status);
		regmap_write(adc_priv->regmap, IPROC_INTERRUPT_STATUS,
				intr_channels);
		retval = IRQ_HANDLED;
	}

	return retval;
}

static int iproc_adc_do_read(struct iio_dev *indio_dev,
			   int channel,
			   u16 *p_adc_data)
{
	int read_len = 0;
	u32 val;
	u32 mask;
	u32 val_check;
	int failed_cnt = 0;
	struct iproc_adc_priv *adc_priv = iio_priv(indio_dev);

	mutex_lock(&adc_priv->mutex);

	/*
	 * After a read is complete the ADC interrupts will be disabled so
	 * we can assume this section of code is safe from interrupts.
	 */
	adc_priv->chan_val = -1;
	adc_priv->chan_id = channel;

	reinit_completion(&adc_priv->completion);
	/* Clear any pending interrupt */
	regmap_update_bits(adc_priv->regmap, IPROC_INTERRUPT_STATUS,
			IPROC_ADC_INTR_MASK | IPROC_ADC_AUXDATA_RDY_INTR,
			((0x0 << channel) << IPROC_ADC_INTR) |
			IPROC_ADC_AUXDATA_RDY_INTR);

	/* Configure channel for snapshot mode and enable  */
	val = (BIT(IPROC_ADC_CHANNEL_ROUNDS) |
		(IPROC_ADC_CHANNEL_MODE_SNAPSHOT << IPROC_ADC_CHANNEL_MODE) |
		(0x1 << IPROC_ADC_CHANNEL_ENABLE));

	mask = IPROC_ADC_CHANNEL_ROUNDS_MASK | IPROC_ADC_CHANNEL_MODE_MASK |
		IPROC_ADC_CHANNEL_ENABLE_MASK;
	regmap_update_bits(adc_priv->regmap, (IPROC_ADC_CHANNEL_REGCTL1 +
				IPROC_ADC_CHANNEL_OFFSET * channel),
				mask, val);

	/* Set the Watermark for a channel */
	regmap_update_bits(adc_priv->regmap, (IPROC_ADC_CHANNEL_REGCTL2 +
					IPROC_ADC_CHANNEL_OFFSET * channel),
					IPROC_ADC_CHANNEL_WATERMARK_MASK,
					0x1);

	/* Enable water mark interrupt */
	regmap_update_bits(adc_priv->regmap, (IPROC_ADC_CHANNEL_INTERRUPT_MASK +
					IPROC_ADC_CHANNEL_OFFSET *
					channel),
					IPROC_ADC_CHANNEL_WTRMRK_INTR_MASK,
					IPROC_ADC_WATER_MARK_INTR_ENABLE);
	regmap_read(adc_priv->regmap, IPROC_INTERRUPT_MASK, &val);

	/* Enable ADC interrupt for a channel */
	val |= (BIT(channel) << IPROC_ADC_INTR);
	regmap_write(adc_priv->regmap, IPROC_INTERRUPT_MASK, val);

	/*
	 * There seems to be a very rare issue where writing to this register
	 * does not take effect.  To work around the issue we will try multiple
	 * writes.  In total we will spend about 10*10 = 100 us attempting this.
	 * Testing has shown that this may loop a few time, but we have never
	 * hit the full count.
	 */
	regmap_read(adc_priv->regmap, IPROC_INTERRUPT_MASK, &val_check);
	while (val_check != val) {
		failed_cnt++;

		if (failed_cnt > IPROC_ADC_INTMASK_RETRY_ATTEMPTS)
			break;

		udelay(10);
		regmap_update_bits(adc_priv->regmap, IPROC_INTERRUPT_MASK,
				IPROC_ADC_INTR_MASK,
				((0x1 << channel) <<
				IPROC_ADC_INTR));

		regmap_read(adc_priv->regmap, IPROC_INTERRUPT_MASK, &val_check);
	}

	if (failed_cnt) {
		dev_dbg(&indio_dev->dev,
			"IntMask failed (%d times)", failed_cnt);
		if (failed_cnt > IPROC_ADC_INTMASK_RETRY_ATTEMPTS) {
			dev_err(&indio_dev->dev,
				"IntMask set failed. Read will likely fail.");
			read_len = -EIO;
			goto adc_err;
		}
	}
	regmap_read(adc_priv->regmap, IPROC_INTERRUPT_MASK, &val_check);

	if (wait_for_completion_timeout(&adc_priv->completion,
		IPROC_ADC_READ_TIMEOUT) > 0) {

		/* Only the lower 16 bits are relevant */
		*p_adc_data = adc_priv->chan_val & 0xFFFF;
		read_len = sizeof(*p_adc_data);

	} else {
		/*
		 * We never got the interrupt, something went wrong.
		 * Perhaps the interrupt may still be coming, we do not want
		 * that now.  Lets disable the ADC interrupt, and clear the
		 * status to put it back in to normal state.
		 */
		read_len = -ETIMEDOUT;
		goto adc_err;
	}
	mutex_unlock(&adc_priv->mutex);

	return read_len;

adc_err:
	regmap_update_bits(adc_priv->regmap, IPROC_INTERRUPT_MASK,
			   IPROC_ADC_INTR_MASK,
			   ((0x0 << channel) << IPROC_ADC_INTR));

	regmap_update_bits(adc_priv->regmap, IPROC_INTERRUPT_STATUS,
			   IPROC_ADC_INTR_MASK,
			   ((0x0 << channel) << IPROC_ADC_INTR));

	dev_err(&indio_dev->dev, "Timed out waiting for ADC data!\n");
	iproc_adc_reg_dump(indio_dev);
	mutex_unlock(&adc_priv->mutex);

	return read_len;
}

static int iproc_adc_enable(struct iio_dev *indio_dev)
{
	u32 val;
	u32 channel_id;
	struct iproc_adc_priv *adc_priv = iio_priv(indio_dev);
	int ret;

	/* Set i_amux = 3b'000, select channel 0 */
	ret = regmap_clear_bits(adc_priv->regmap, IPROC_ANALOG_CONTROL,
				IPROC_ADC_CHANNEL_SEL_MASK);
	if (ret) {
		dev_err(&indio_dev->dev,
			"failed to write IPROC_ANALOG_CONTROL %d\n", ret);
		return ret;
	}
	adc_priv->chan_val = -1;

	/*
	 * PWR up LDO, ADC, and Band Gap (0 to enable)
	 * Also enable ADC controller (set high)
	 */
	ret = regmap_read(adc_priv->regmap, IPROC_REGCTL2, &val);
	if (ret) {
		dev_err(&indio_dev->dev,
			"failed to read IPROC_REGCTL2 %d\n", ret);
		return ret;
	}

	val &= ~(IPROC_ADC_PWR_LDO | IPROC_ADC_PWR_ADC | IPROC_ADC_PWR_BG);

	ret = regmap_write(adc_priv->regmap, IPROC_REGCTL2, val);
	if (ret) {
		dev_err(&indio_dev->dev,
			"failed to write IPROC_REGCTL2 %d\n", ret);
		return ret;
	}

	ret = regmap_read(adc_priv->regmap, IPROC_REGCTL2, &val);
	if (ret) {
		dev_err(&indio_dev->dev,
			"failed to read IPROC_REGCTL2 %d\n", ret);
		return ret;
	}

	val |= IPROC_ADC_CONTROLLER_EN;
	ret = regmap_write(adc_priv->regmap, IPROC_REGCTL2, val);
	if (ret) {
		dev_err(&indio_dev->dev,
			"failed to write IPROC_REGCTL2 %d\n", ret);
		return ret;
	}

	for (channel_id = 0; channel_id < indio_dev->num_channels;
		channel_id++) {
		ret = regmap_write(adc_priv->regmap,
				IPROC_ADC_CHANNEL_INTERRUPT_MASK +
				IPROC_ADC_CHANNEL_OFFSET * channel_id, 0);
		if (ret) {
			dev_err(&indio_dev->dev,
			    "failed to write ADC_CHANNEL_INTERRUPT_MASK %d\n",
			    ret);
			return ret;
		}

		ret = regmap_write(adc_priv->regmap,
				IPROC_ADC_CHANNEL_INTERRUPT_STATUS +
				IPROC_ADC_CHANNEL_OFFSET * channel_id, 0);
		if (ret) {
			dev_err(&indio_dev->dev,
			    "failed to write ADC_CHANNEL_INTERRUPT_STATUS %d\n",
			    ret);
			return ret;
		}
	}

	return 0;
}

static void iproc_adc_disable(struct iio_dev *indio_dev)
{
	u32 val;
	int ret;
	struct iproc_adc_priv *adc_priv = iio_priv(indio_dev);

	ret = regmap_read(adc_priv->regmap, IPROC_REGCTL2, &val);
	if (ret) {
		dev_err(&indio_dev->dev,
			"failed to read IPROC_REGCTL2 %d\n", ret);
		return;
	}

	val &= ~IPROC_ADC_CONTROLLER_EN;
	ret = regmap_write(adc_priv->regmap, IPROC_REGCTL2, val);
	if (ret) {
		dev_err(&indio_dev->dev,
			"failed to write IPROC_REGCTL2 %d\n", ret);
		return;
	}
}

static int iproc_adc_read_raw(struct iio_dev *indio_dev,
			  struct iio_chan_spec const *chan,
			  int *val,
			  int *val2,
			  long mask)
{
	u16 adc_data;
	int err;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		err =  iproc_adc_do_read(indio_dev, chan->channel, &adc_data);
		if (err < 0)
			return err;
		*val = adc_data;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_VOLTAGE:
			*val = 1800;
			*val2 = 10;
			return IIO_VAL_FRACTIONAL_LOG2;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static const struct iio_info iproc_adc_iio_info = {
	.read_raw = &iproc_adc_read_raw,
};

#define IPROC_ADC_CHANNEL(_index, _id) {                \
	.type = IIO_VOLTAGE,                            \
	.indexed = 1,                                   \
	.channel = _index,                              \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),   \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
	.datasheet_name = _id,                          \
}

static const struct iio_chan_spec iproc_adc_iio_channels[] = {
	IPROC_ADC_CHANNEL(0, "adc0"),
	IPROC_ADC_CHANNEL(1, "adc1"),
	IPROC_ADC_CHANNEL(2, "adc2"),
	IPROC_ADC_CHANNEL(3, "adc3"),
	IPROC_ADC_CHANNEL(4, "adc4"),
	IPROC_ADC_CHANNEL(5, "adc5"),
	IPROC_ADC_CHANNEL(6, "adc6"),
	IPROC_ADC_CHANNEL(7, "adc7"),
};

static int iproc_adc_probe(struct platform_device *pdev)
{
	struct iproc_adc_priv *adc_priv;
	struct iio_dev *indio_dev = NULL;
	int ret;

	indio_dev = devm_iio_device_alloc(&pdev->dev,
					sizeof(*adc_priv));
	if (!indio_dev) {
		dev_err(&pdev->dev, "failed to allocate iio device\n");
		return -ENOMEM;
	}

	adc_priv = iio_priv(indio_dev);
	platform_set_drvdata(pdev, indio_dev);

	mutex_init(&adc_priv->mutex);

	init_completion(&adc_priv->completion);

	adc_priv->regmap = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
			   "adc-syscon");
	if (IS_ERR(adc_priv->regmap)) {
		dev_err(&pdev->dev, "failed to get handle for tsc syscon\n");
		ret = PTR_ERR(adc_priv->regmap);
		return ret;
	}

	adc_priv->adc_clk = devm_clk_get(&pdev->dev, "tsc_clk");
	if (IS_ERR(adc_priv->adc_clk)) {
		dev_err(&pdev->dev,
			"failed getting clock tsc_clk\n");
		ret = PTR_ERR(adc_priv->adc_clk);
		return ret;
	}

	adc_priv->irqno = platform_get_irq(pdev, 0);
	if (adc_priv->irqno < 0)
		return adc_priv->irqno;

	ret = regmap_clear_bits(adc_priv->regmap, IPROC_REGCTL2,
				IPROC_ADC_AUXIN_SCAN_ENA);
	if (ret) {
		dev_err(&pdev->dev, "failed to write IPROC_REGCTL2 %d\n", ret);
		return ret;
	}

	ret = devm_request_threaded_irq(&pdev->dev, adc_priv->irqno,
				iproc_adc_interrupt_handler,
				iproc_adc_interrupt_thread,
				IRQF_SHARED, "iproc-adc", indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "request_irq error %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(adc_priv->adc_clk);
	if (ret) {
		dev_err(&pdev->dev,
			"clk_prepare_enable failed %d\n", ret);
		return ret;
	}

	ret = iproc_adc_enable(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable adc %d\n", ret);
		goto err_adc_enable;
	}

	indio_dev->name = "iproc-static-adc";
	indio_dev->info = &iproc_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = iproc_adc_iio_channels;
	indio_dev->num_channels = ARRAY_SIZE(iproc_adc_iio_channels);

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "iio_device_register failed:err %d\n", ret);
		goto err_clk;
	}

	return 0;

err_clk:
	iproc_adc_disable(indio_dev);
err_adc_enable:
	clk_disable_unprepare(adc_priv->adc_clk);

	return ret;
}

static void iproc_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct iproc_adc_priv *adc_priv = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	iproc_adc_disable(indio_dev);
	clk_disable_unprepare(adc_priv->adc_clk);
}

static const struct of_device_id iproc_adc_of_match[] = {
	{.compatible = "brcm,iproc-static-adc", },
	{ }
};
MODULE_DEVICE_TABLE(of, iproc_adc_of_match);

static struct platform_driver iproc_adc_driver = {
	.probe  = iproc_adc_probe,
	.remove_new = iproc_adc_remove,
	.driver	= {
		.name	= "iproc-static-adc",
		.of_match_table = iproc_adc_of_match,
	},
};
module_platform_driver(iproc_adc_driver);

MODULE_DESCRIPTION("Broadcom iProc ADC controller driver");
MODULE_AUTHOR("Raveendra Padasalagi <raveendra.padasalagi@broadcom.com>");
MODULE_LICENSE("GPL v2");
