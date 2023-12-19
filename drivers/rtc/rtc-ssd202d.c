// SPDX-License-Identifier: GPL-2.0-only
/*
 * Real time clocks driver for MStar/SigmaStar SSD202D SoCs.
 *
 * (C) 2021 Daniel Palmer
 * (C) 2023 Romain Perier
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/regmap.h>
#include <linux/pm.h>

#define REG_CTRL	0x0
#define REG_CTRL1	0x4
#define REG_ISO_CTRL	0xc
#define REG_WRDATA_L	0x10
#define REG_WRDATA_H	0x14
#define REG_ISOACK	0x20
#define REG_RDDATA_L	0x24
#define REG_RDDATA_H	0x28
#define REG_RDCNT_L	0x30
#define REG_RDCNT_H	0x34
#define REG_CNT_TRIG	0x38
#define REG_PWRCTRL	0x3c
#define REG_RTC_TEST	0x54

#define CNT_RD_TRIG_BIT BIT(0)
#define CNT_RD_BIT BIT(0)
#define BASE_WR_BIT BIT(1)
#define BASE_RD_BIT BIT(2)
#define CNT_RST_BIT BIT(3)
#define ISO_CTRL_ACK_MASK BIT(3)
#define ISO_CTRL_ACK_SHIFT 3
#define SW0_WR_BIT BIT(5)
#define SW1_WR_BIT BIT(6)
#define SW0_RD_BIT BIT(7)
#define SW1_RD_BIT BIT(8)

#define ISO_CTRL_MASK GENMASK(2, 0)

struct ssd202d_rtc {
	struct rtc_device *rtc_dev;
	void __iomem *base;
};

static u8 read_iso_en(void __iomem *base)
{
	return readb(base + REG_RTC_TEST) & 0x1;
}

static u8 read_iso_ctrl_ack(void __iomem *base)
{
	return (readb(base + REG_ISOACK) & ISO_CTRL_ACK_MASK) >> ISO_CTRL_ACK_SHIFT;
}

static int ssd202d_rtc_isoctrl(struct ssd202d_rtc *priv)
{
	static const unsigned int sequence[] = { 0x0, 0x1, 0x3, 0x7, 0x5, 0x1, 0x0 };
	unsigned int val;
	struct device *dev = &priv->rtc_dev->dev;
	int i, ret;

	/*
	 * This gates iso_en by writing a special sequence of bytes to iso_ctrl
	 * and ensuring that it has been correctly applied by reading iso_ctrl_ack
	 */
	for (i = 0; i < ARRAY_SIZE(sequence); i++) {
		writeb(sequence[i] & ISO_CTRL_MASK, priv->base +  REG_ISO_CTRL);

		ret = read_poll_timeout(read_iso_ctrl_ack, val, val == (i % 2), 100,
					20 * 100, true, priv->base);
		if (ret) {
			dev_dbg(dev, "Timeout waiting for ack byte %i (%x) of sequence\n", i,
				sequence[i]);
			return ret;
		}
	}

	/*
	 * At this point iso_en should be raised for 1ms
	 */
	ret = read_poll_timeout(read_iso_en, val, val, 100, 22 * 100, true, priv->base);
	if (ret)
		dev_dbg(dev, "Timeout waiting for iso_en\n");
	mdelay(2);
	return 0;
}

static void ssd202d_rtc_read_reg(struct ssd202d_rtc *priv, unsigned int reg,
				 unsigned int field, unsigned int *base)
{
	unsigned int l, h;
	u16 val;

	/* Ask for the content of an RTC value into RDDATA by gating iso_en,
	 * then iso_en is gated and the content of RDDATA can be read
	 */
	val = readw(priv->base + reg);
	writew(val | field, priv->base + reg);
	ssd202d_rtc_isoctrl(priv);
	writew(val & ~field, priv->base + reg);

	l = readw(priv->base + REG_RDDATA_L);
	h = readw(priv->base + REG_RDDATA_H);

	*base = (h << 16) | l;
}

static void ssd202d_rtc_write_reg(struct ssd202d_rtc *priv, unsigned int reg,
				  unsigned int field, u32 base)
{
	u16 val;

	/* Set the content of an RTC value from WRDATA by gating iso_en */
	val = readw(priv->base + reg);
	writew(val | field, priv->base + reg);
	writew(base, priv->base + REG_WRDATA_L);
	writew(base >> 16, priv->base + REG_WRDATA_H);
	ssd202d_rtc_isoctrl(priv);
	writew(val & ~field, priv->base + reg);
}

static int ssd202d_rtc_read_counter(struct ssd202d_rtc *priv, unsigned int *counter)
{
	unsigned int l, h;
	u16 val;

	val = readw(priv->base + REG_CTRL1);
	writew(val | CNT_RD_BIT, priv->base + REG_CTRL1);
	ssd202d_rtc_isoctrl(priv);
	writew(val & ~CNT_RD_BIT, priv->base + REG_CTRL1);

	val = readw(priv->base + REG_CTRL1);
	writew(val | CNT_RD_TRIG_BIT, priv->base + REG_CNT_TRIG);
	writew(val & ~CNT_RD_TRIG_BIT, priv->base + REG_CNT_TRIG);

	l = readw(priv->base + REG_RDCNT_L);
	h = readw(priv->base + REG_RDCNT_H);

	*counter = (h << 16) | l;

	return 0;
}

static int ssd202d_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct ssd202d_rtc *priv = dev_get_drvdata(dev);
	unsigned int sw0, base, counter;
	u32 seconds;
	int ret;

	/* Check that RTC is enabled by SW */
	ssd202d_rtc_read_reg(priv, REG_CTRL, SW0_RD_BIT, &sw0);
	if (sw0 != 1)
		return -EINVAL;

	/* Get RTC base value from RDDATA */
	ssd202d_rtc_read_reg(priv, REG_CTRL, BASE_RD_BIT, &base);
	/* Get RTC counter value from RDDATA */
	ret = ssd202d_rtc_read_counter(priv, &counter);
	if (ret)
		return ret;

	seconds = base + counter;

	rtc_time64_to_tm(seconds, tm);

	return 0;
}

static int ssd202d_rtc_reset_counter(struct ssd202d_rtc *priv)
{
	u16 val;

	val = readw(priv->base + REG_CTRL);
	writew(val | CNT_RST_BIT, priv->base + REG_CTRL);
	ssd202d_rtc_isoctrl(priv);
	writew(val & ~CNT_RST_BIT, priv->base + REG_CTRL);
	ssd202d_rtc_isoctrl(priv);

	return 0;
}

static int ssd202d_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct ssd202d_rtc *priv = dev_get_drvdata(dev);
	unsigned long seconds = rtc_tm_to_time64(tm);

	ssd202d_rtc_write_reg(priv, REG_CTRL, BASE_WR_BIT, seconds);
	ssd202d_rtc_reset_counter(priv);
	ssd202d_rtc_write_reg(priv, REG_CTRL, SW0_WR_BIT, 1);

	return 0;
}

static const struct rtc_class_ops ssd202d_rtc_ops = {
	.read_time = ssd202d_rtc_read_time,
	.set_time = ssd202d_rtc_set_time,
};

static int ssd202d_rtc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ssd202d_rtc *priv;

	priv = devm_kzalloc(&pdev->dev, sizeof(struct ssd202d_rtc), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->rtc_dev = devm_rtc_allocate_device(dev);
	if (IS_ERR(priv->rtc_dev))
		return PTR_ERR(priv->rtc_dev);

	priv->rtc_dev->ops = &ssd202d_rtc_ops;
	priv->rtc_dev->range_max = U32_MAX;

	platform_set_drvdata(pdev, priv);

	return devm_rtc_register_device(priv->rtc_dev);
}

static const struct of_device_id ssd202d_rtc_of_match_table[] = {
	{ .compatible = "mstar,ssd202d-rtc" },
	{ }
};
MODULE_DEVICE_TABLE(of, ssd202d_rtc_of_match_table);

static struct platform_driver ssd202d_rtc_driver = {
	.probe = ssd202d_rtc_probe,
	.driver = {
		.name = "ssd202d-rtc",
		.of_match_table = ssd202d_rtc_of_match_table,
	},
};
module_platform_driver(ssd202d_rtc_driver);

MODULE_AUTHOR("Daniel Palmer <daniel@thingy.jp>");
MODULE_AUTHOR("Romain Perier <romain.perier@gmail.com>");
MODULE_DESCRIPTION("MStar SSD202D RTC Driver");
MODULE_LICENSE("GPL");
