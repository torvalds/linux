// SPDX-License-Identifier: GPL-2.0
/*
 * RTC driver for the interal RTC block in the Amlogic Meson6, Meson8,
 * Meson8b and Meson8m2 SoCs.
 *
 * The RTC is split in to two parts, the AHB front end and a simple serial
 * connection to the actual registers. This driver manages both parts.
 *
 * Copyright (c) 2018 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
 * Copyright (c) 2015 Ben Dooks <ben.dooks@codethink.co.uk> for Codethink Ltd
 * Based on origin by Carlo Caione <carlo@endlessm.com>
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/rtc.h>

/* registers accessed from cpu bus */
#define RTC_ADDR0				0x00
	#define RTC_ADDR0_LINE_SCLK		BIT(0)
	#define RTC_ADDR0_LINE_SEN		BIT(1)
	#define RTC_ADDR0_LINE_SDI		BIT(2)
	#define RTC_ADDR0_START_SER		BIT(17)
	#define RTC_ADDR0_WAIT_SER		BIT(22)
	#define RTC_ADDR0_DATA			GENMASK(31, 24)

#define RTC_ADDR1				0x04
	#define RTC_ADDR1_SDO			BIT(0)
	#define RTC_ADDR1_S_READY		BIT(1)

#define RTC_ADDR2				0x08
#define RTC_ADDR3				0x0c

#define RTC_REG4				0x10
	#define RTC_REG4_STATIC_VALUE		GENMASK(7, 0)

/* rtc registers accessed via rtc-serial interface */
#define RTC_COUNTER		(0)
#define RTC_SEC_ADJ		(2)
#define RTC_REGMEM_0		(4)
#define RTC_REGMEM_1		(5)
#define RTC_REGMEM_2		(6)
#define RTC_REGMEM_3		(7)

#define RTC_ADDR_BITS		(3)	/* number of address bits to send */
#define RTC_DATA_BITS		(32)	/* number of data bits to tx/rx */

#define MESON_STATIC_BIAS_CUR	(0x5 << 1)
#define MESON_STATIC_VOLTAGE	(0x3 << 11)
#define MESON_STATIC_DEFAULT    (MESON_STATIC_BIAS_CUR | MESON_STATIC_VOLTAGE)

struct meson_rtc {
	struct rtc_device	*rtc;		/* rtc device we created */
	struct device		*dev;		/* device we bound from */
	struct reset_control	*reset;		/* reset source */
	struct regulator	*vdd;		/* voltage input */
	struct regmap		*peripheral;	/* peripheral registers */
	struct regmap		*serial;	/* serial registers */
};

static const struct regmap_config meson_rtc_peripheral_regmap_config = {
	.name		= "peripheral-registers",
	.reg_bits	= 8,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= RTC_REG4,
	.fast_io	= true,
};

/* RTC front-end serialiser controls */

static void meson_rtc_sclk_pulse(struct meson_rtc *rtc)
{
	udelay(5);
	regmap_update_bits(rtc->peripheral, RTC_ADDR0, RTC_ADDR0_LINE_SCLK, 0);
	udelay(5);
	regmap_update_bits(rtc->peripheral, RTC_ADDR0, RTC_ADDR0_LINE_SCLK,
			   RTC_ADDR0_LINE_SCLK);
}

static void meson_rtc_send_bit(struct meson_rtc *rtc, unsigned int bit)
{
	regmap_update_bits(rtc->peripheral, RTC_ADDR0, RTC_ADDR0_LINE_SDI,
			   bit ? RTC_ADDR0_LINE_SDI : 0);
	meson_rtc_sclk_pulse(rtc);
}

static void meson_rtc_send_bits(struct meson_rtc *rtc, u32 data,
				unsigned int nr)
{
	u32 bit = 1 << (nr - 1);

	while (bit) {
		meson_rtc_send_bit(rtc, data & bit);
		bit >>= 1;
	}
}

static void meson_rtc_set_dir(struct meson_rtc *rtc, u32 mode)
{
	regmap_update_bits(rtc->peripheral, RTC_ADDR0, RTC_ADDR0_LINE_SEN, 0);
	regmap_update_bits(rtc->peripheral, RTC_ADDR0, RTC_ADDR0_LINE_SDI, 0);
	meson_rtc_send_bit(rtc, mode);
	regmap_update_bits(rtc->peripheral, RTC_ADDR0, RTC_ADDR0_LINE_SDI, 0);
}

static u32 meson_rtc_get_data(struct meson_rtc *rtc)
{
	u32 tmp, val = 0;
	int bit;

	for (bit = 0; bit < RTC_DATA_BITS; bit++) {
		meson_rtc_sclk_pulse(rtc);
		val <<= 1;

		regmap_read(rtc->peripheral, RTC_ADDR1, &tmp);
		val |= tmp & RTC_ADDR1_SDO;
	}

	return val;
}

static int meson_rtc_get_bus(struct meson_rtc *rtc)
{
	int ret, retries = 3;
	u32 val;

	/* prepare bus for transfers, set all lines low */
	val = RTC_ADDR0_LINE_SDI | RTC_ADDR0_LINE_SEN | RTC_ADDR0_LINE_SCLK;
	regmap_update_bits(rtc->peripheral, RTC_ADDR0, val, 0);

	for (retries = 0; retries < 3; retries++) {
		/* wait for the bus to be ready */
		if (!regmap_read_poll_timeout(rtc->peripheral, RTC_ADDR1, val,
					      val & RTC_ADDR1_S_READY, 10,
					      10000))
			return 0;

		dev_warn(rtc->dev, "failed to get bus, resetting RTC\n");

		ret = reset_control_reset(rtc->reset);
		if (ret)
			return ret;
	}

	dev_err(rtc->dev, "bus is not ready\n");
	return -ETIMEDOUT;
}

static int meson_rtc_serial_bus_reg_read(void *context, unsigned int reg,
					 unsigned int *data)
{
	struct meson_rtc *rtc = context;
	int ret;

	ret = meson_rtc_get_bus(rtc);
	if (ret)
		return ret;

	regmap_update_bits(rtc->peripheral, RTC_ADDR0, RTC_ADDR0_LINE_SEN,
			   RTC_ADDR0_LINE_SEN);
	meson_rtc_send_bits(rtc, reg, RTC_ADDR_BITS);
	meson_rtc_set_dir(rtc, 0);
	*data = meson_rtc_get_data(rtc);

	return 0;
}

static int meson_rtc_serial_bus_reg_write(void *context, unsigned int reg,
					  unsigned int data)
{
	struct meson_rtc *rtc = context;
	int ret;

	ret = meson_rtc_get_bus(rtc);
	if (ret)
		return ret;

	regmap_update_bits(rtc->peripheral, RTC_ADDR0, RTC_ADDR0_LINE_SEN,
			   RTC_ADDR0_LINE_SEN);
	meson_rtc_send_bits(rtc, data, RTC_DATA_BITS);
	meson_rtc_send_bits(rtc, reg, RTC_ADDR_BITS);
	meson_rtc_set_dir(rtc, 1);

	return 0;
}

static const struct regmap_bus meson_rtc_serial_bus = {
	.reg_read	= meson_rtc_serial_bus_reg_read,
	.reg_write	= meson_rtc_serial_bus_reg_write,
};

static const struct regmap_config meson_rtc_serial_regmap_config = {
	.name		= "serial-registers",
	.reg_bits	= 4,
	.reg_stride	= 1,
	.val_bits	= 32,
	.max_register	= RTC_REGMEM_3,
	.fast_io	= false,
};

static int meson_rtc_write_static(struct meson_rtc *rtc, u32 data)
{
	u32 tmp;

	regmap_write(rtc->peripheral, RTC_REG4,
		     FIELD_PREP(RTC_REG4_STATIC_VALUE, (data >> 8)));

	/* write the static value and start the auto serializer */
	tmp = FIELD_PREP(RTC_ADDR0_DATA, (data & 0xff)) | RTC_ADDR0_START_SER;
	regmap_update_bits(rtc->peripheral, RTC_ADDR0,
			   RTC_ADDR0_DATA | RTC_ADDR0_START_SER, tmp);

	/* wait for the auto serializer to complete */
	return regmap_read_poll_timeout(rtc->peripheral, RTC_REG4, tmp,
					!(tmp & RTC_ADDR0_WAIT_SER), 10,
					10000);
}

/* RTC interface layer functions */

static int meson_rtc_gettime(struct device *dev, struct rtc_time *tm)
{
	struct meson_rtc *rtc = dev_get_drvdata(dev);
	u32 time;
	int ret;

	ret = regmap_read(rtc->serial, RTC_COUNTER, &time);
	if (!ret)
		rtc_time64_to_tm(time, tm);

	return ret;
}

static int meson_rtc_settime(struct device *dev, struct rtc_time *tm)
{
	struct meson_rtc *rtc = dev_get_drvdata(dev);

	return regmap_write(rtc->serial, RTC_COUNTER, rtc_tm_to_time64(tm));
}

static const struct rtc_class_ops meson_rtc_ops = {
	.read_time	= meson_rtc_gettime,
	.set_time	= meson_rtc_settime,
};

/* NVMEM interface layer functions */

static int meson_rtc_regmem_read(void *context, unsigned int offset,
				 void *buf, size_t bytes)
{
	struct meson_rtc *rtc = context;
	unsigned int read_offset, read_size;

	read_offset = RTC_REGMEM_0 + (offset / 4);
	read_size = bytes / 4;

	return regmap_bulk_read(rtc->serial, read_offset, buf, read_size);
}

static int meson_rtc_regmem_write(void *context, unsigned int offset,
				  void *buf, size_t bytes)
{
	struct meson_rtc *rtc = context;
	unsigned int write_offset, write_size;

	write_offset = RTC_REGMEM_0 + (offset / 4);
	write_size = bytes / 4;

	return regmap_bulk_write(rtc->serial, write_offset, buf, write_size);
}

static int meson_rtc_probe(struct platform_device *pdev)
{
	struct nvmem_config meson_rtc_nvmem_config = {
		.name = "meson-rtc-regmem",
		.type = NVMEM_TYPE_BATTERY_BACKED,
		.word_size = 4,
		.stride = 4,
		.size = 4 * 4,
		.reg_read = meson_rtc_regmem_read,
		.reg_write = meson_rtc_regmem_write,
	};
	struct device *dev = &pdev->dev;
	struct meson_rtc *rtc;
	struct resource *res;
	void __iomem *base;
	int ret;
	u32 tm;

	rtc = devm_kzalloc(dev, sizeof(struct meson_rtc), GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	rtc->rtc = devm_rtc_allocate_device(dev);
	if (IS_ERR(rtc->rtc))
		return PTR_ERR(rtc->rtc);

	platform_set_drvdata(pdev, rtc);

	rtc->dev = dev;

	rtc->rtc->ops = &meson_rtc_ops;
	rtc->rtc->range_max = U32_MAX;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	rtc->peripheral = devm_regmap_init_mmio(dev, base,
					&meson_rtc_peripheral_regmap_config);
	if (IS_ERR(rtc->peripheral)) {
		dev_err(dev, "failed to create peripheral regmap\n");
		return PTR_ERR(rtc->peripheral);
	}

	rtc->reset = devm_reset_control_get(dev, NULL);
	if (IS_ERR(rtc->reset)) {
		dev_err(dev, "missing reset line\n");
		return PTR_ERR(rtc->reset);
	}

	rtc->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(rtc->vdd)) {
		dev_err(dev, "failed to get the vdd-supply\n");
		return PTR_ERR(rtc->vdd);
	}

	ret = regulator_enable(rtc->vdd);
	if (ret) {
		dev_err(dev, "failed to enable vdd-supply\n");
		return ret;
	}

	ret = meson_rtc_write_static(rtc, MESON_STATIC_DEFAULT);
	if (ret) {
		dev_err(dev, "failed to set static values\n");
		goto out_disable_vdd;
	}

	rtc->serial = devm_regmap_init(dev, &meson_rtc_serial_bus, rtc,
				       &meson_rtc_serial_regmap_config);
	if (IS_ERR(rtc->serial)) {
		dev_err(dev, "failed to create serial regmap\n");
		ret = PTR_ERR(rtc->serial);
		goto out_disable_vdd;
	}

	/*
	 * check if we can read RTC counter, if not then the RTC is probably
	 * not functional. If it isn't probably best to not bind.
	 */
	ret = regmap_read(rtc->serial, RTC_COUNTER, &tm);
	if (ret) {
		dev_err(dev, "cannot read RTC counter, RTC not functional\n");
		goto out_disable_vdd;
	}

	meson_rtc_nvmem_config.priv = rtc;
	ret = rtc_nvmem_register(rtc->rtc, &meson_rtc_nvmem_config);
	if (ret)
		goto out_disable_vdd;

	ret = rtc_register_device(rtc->rtc);
	if (ret)
		goto out_disable_vdd;

	return 0;

out_disable_vdd:
	regulator_disable(rtc->vdd);
	return ret;
}

static const struct of_device_id meson_rtc_dt_match[] = {
	{ .compatible = "amlogic,meson6-rtc", },
	{ .compatible = "amlogic,meson8-rtc", },
	{ .compatible = "amlogic,meson8b-rtc", },
	{ .compatible = "amlogic,meson8m2-rtc", },
	{ },
};
MODULE_DEVICE_TABLE(of, meson_rtc_dt_match);

static struct platform_driver meson_rtc_driver = {
	.probe		= meson_rtc_probe,
	.driver		= {
		.name		= "meson-rtc",
		.of_match_table	= of_match_ptr(meson_rtc_dt_match),
	},
};
module_platform_driver(meson_rtc_driver);

MODULE_DESCRIPTION("Amlogic Meson RTC Driver");
MODULE_AUTHOR("Ben Dooks <ben.doosk@codethink.co.uk>");
MODULE_AUTHOR("Martin Blumenstingl <martin.blumenstingl@googlemail.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:meson-rtc");
