/*
 *  Ricoh RP5C01 RTC Driver
 *
 *  Copyright 2009 Geert Uytterhoeven
 *
 *  Based on the A3000 TOD code in arch/m68k/amiga/config.c
 *  Copyright (C) 1993 Hamish Macdonald
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/slab.h>


enum {
	RP5C01_1_SECOND		= 0x0,	/* MODE 00 */
	RP5C01_10_SECOND	= 0x1,	/* MODE 00 */
	RP5C01_1_MINUTE		= 0x2,	/* MODE 00 and MODE 01 */
	RP5C01_10_MINUTE	= 0x3,	/* MODE 00 and MODE 01 */
	RP5C01_1_HOUR		= 0x4,	/* MODE 00 and MODE 01 */
	RP5C01_10_HOUR		= 0x5,	/* MODE 00 and MODE 01 */
	RP5C01_DAY_OF_WEEK	= 0x6,	/* MODE 00 and MODE 01 */
	RP5C01_1_DAY		= 0x7,	/* MODE 00 and MODE 01 */
	RP5C01_10_DAY		= 0x8,	/* MODE 00 and MODE 01 */
	RP5C01_1_MONTH		= 0x9,	/* MODE 00 */
	RP5C01_10_MONTH		= 0xa,	/* MODE 00 */
	RP5C01_1_YEAR		= 0xb,	/* MODE 00 */
	RP5C01_10_YEAR		= 0xc,	/* MODE 00 */

	RP5C01_12_24_SELECT	= 0xa,	/* MODE 01 */
	RP5C01_LEAP_YEAR	= 0xb,	/* MODE 01 */

	RP5C01_MODE		= 0xd,	/* all modes */
	RP5C01_TEST		= 0xe,	/* all modes */
	RP5C01_RESET		= 0xf,	/* all modes */
};

#define RP5C01_12_24_SELECT_12	(0 << 0)
#define RP5C01_12_24_SELECT_24	(1 << 0)

#define RP5C01_10_HOUR_AM	(0 << 1)
#define RP5C01_10_HOUR_PM	(1 << 1)

#define RP5C01_MODE_TIMER_EN	(1 << 3)	/* timer enable */
#define RP5C01_MODE_ALARM_EN	(1 << 2)	/* alarm enable */

#define RP5C01_MODE_MODE_MASK	(3 << 0)
#define RP5C01_MODE_MODE00	(0 << 0)	/* time */
#define RP5C01_MODE_MODE01	(1 << 0)	/* alarm, 12h/24h, leap year */
#define RP5C01_MODE_RAM_BLOCK10	(2 << 0)	/* RAM 4 bits x 13 */
#define RP5C01_MODE_RAM_BLOCK11	(3 << 0)	/* RAM 4 bits x 13 */

#define RP5C01_RESET_1HZ_PULSE	(1 << 3)
#define RP5C01_RESET_16HZ_PULSE	(1 << 2)
#define RP5C01_RESET_SECOND	(1 << 1)	/* reset divider stages for */
						/* seconds or smaller units */
#define RP5C01_RESET_ALARM	(1 << 0)	/* reset all alarm registers */


struct rp5c01_priv {
	u32 __iomem *regs;
	struct rtc_device *rtc;
	spinlock_t lock;	/* against concurrent RTC/NVRAM access */
	struct bin_attribute nvram_attr;
};

static inline unsigned int rp5c01_read(struct rp5c01_priv *priv,
				       unsigned int reg)
{
	return __raw_readl(&priv->regs[reg]) & 0xf;
}

static inline void rp5c01_write(struct rp5c01_priv *priv, unsigned int val,
				unsigned int reg)
{
	__raw_writel(val, &priv->regs[reg]);
}

static void rp5c01_lock(struct rp5c01_priv *priv)
{
	rp5c01_write(priv, RP5C01_MODE_MODE00, RP5C01_MODE);
}

static void rp5c01_unlock(struct rp5c01_priv *priv)
{
	rp5c01_write(priv, RP5C01_MODE_TIMER_EN | RP5C01_MODE_MODE01,
		     RP5C01_MODE);
}

static int rp5c01_read_time(struct device *dev, struct rtc_time *tm)
{
	struct rp5c01_priv *priv = dev_get_drvdata(dev);

	spin_lock_irq(&priv->lock);
	rp5c01_lock(priv);

	tm->tm_sec  = rp5c01_read(priv, RP5C01_10_SECOND) * 10 +
		      rp5c01_read(priv, RP5C01_1_SECOND);
	tm->tm_min  = rp5c01_read(priv, RP5C01_10_MINUTE) * 10 +
		      rp5c01_read(priv, RP5C01_1_MINUTE);
	tm->tm_hour = rp5c01_read(priv, RP5C01_10_HOUR) * 10 +
		      rp5c01_read(priv, RP5C01_1_HOUR);
	tm->tm_mday = rp5c01_read(priv, RP5C01_10_DAY) * 10 +
		      rp5c01_read(priv, RP5C01_1_DAY);
	tm->tm_wday = rp5c01_read(priv, RP5C01_DAY_OF_WEEK);
	tm->tm_mon  = rp5c01_read(priv, RP5C01_10_MONTH) * 10 +
		      rp5c01_read(priv, RP5C01_1_MONTH) - 1;
	tm->tm_year = rp5c01_read(priv, RP5C01_10_YEAR) * 10 +
		      rp5c01_read(priv, RP5C01_1_YEAR);
	if (tm->tm_year <= 69)
		tm->tm_year += 100;

	rp5c01_unlock(priv);
	spin_unlock_irq(&priv->lock);

	return rtc_valid_tm(tm);
}

static int rp5c01_set_time(struct device *dev, struct rtc_time *tm)
{
	struct rp5c01_priv *priv = dev_get_drvdata(dev);

	spin_lock_irq(&priv->lock);
	rp5c01_lock(priv);

	rp5c01_write(priv, tm->tm_sec / 10, RP5C01_10_SECOND);
	rp5c01_write(priv, tm->tm_sec % 10, RP5C01_1_SECOND);
	rp5c01_write(priv, tm->tm_min / 10, RP5C01_10_MINUTE);
	rp5c01_write(priv, tm->tm_min % 10, RP5C01_1_MINUTE);
	rp5c01_write(priv, tm->tm_hour / 10, RP5C01_10_HOUR);
	rp5c01_write(priv, tm->tm_hour % 10, RP5C01_1_HOUR);
	rp5c01_write(priv, tm->tm_mday / 10, RP5C01_10_DAY);
	rp5c01_write(priv, tm->tm_mday % 10, RP5C01_1_DAY);
	if (tm->tm_wday != -1)
		rp5c01_write(priv, tm->tm_wday, RP5C01_DAY_OF_WEEK);
	rp5c01_write(priv, (tm->tm_mon + 1) / 10, RP5C01_10_MONTH);
	rp5c01_write(priv, (tm->tm_mon + 1) % 10, RP5C01_1_MONTH);
	if (tm->tm_year >= 100)
		tm->tm_year -= 100;
	rp5c01_write(priv, tm->tm_year / 10, RP5C01_10_YEAR);
	rp5c01_write(priv, tm->tm_year % 10, RP5C01_1_YEAR);

	rp5c01_unlock(priv);
	spin_unlock_irq(&priv->lock);
	return 0;
}

static const struct rtc_class_ops rp5c01_rtc_ops = {
	.read_time	= rp5c01_read_time,
	.set_time	= rp5c01_set_time,
};


/*
 * The NVRAM is organized as 2 blocks of 13 nibbles of 4 bits.
 * We provide access to them like AmigaOS does: the high nibble of each 8-bit
 * byte is stored in BLOCK10, the low nibble in BLOCK11.
 */

static ssize_t rp5c01_nvram_read(struct file *filp, struct kobject *kobj,
				 struct bin_attribute *bin_attr,
				 char *buf, loff_t pos, size_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct rp5c01_priv *priv = dev_get_drvdata(dev);
	ssize_t count;

	spin_lock_irq(&priv->lock);

	for (count = 0; size > 0 && pos < RP5C01_MODE; count++, size--) {
		u8 data;

		rp5c01_write(priv,
			     RP5C01_MODE_TIMER_EN | RP5C01_MODE_RAM_BLOCK10,
			     RP5C01_MODE);
		data = rp5c01_read(priv, pos) << 4;
		rp5c01_write(priv,
			     RP5C01_MODE_TIMER_EN | RP5C01_MODE_RAM_BLOCK11,
			     RP5C01_MODE);
		data |= rp5c01_read(priv, pos++);
		rp5c01_write(priv, RP5C01_MODE_TIMER_EN | RP5C01_MODE_MODE01,
			     RP5C01_MODE);
		*buf++ = data;
	}

	spin_unlock_irq(&priv->lock);
	return count;
}

static ssize_t rp5c01_nvram_write(struct file *filp, struct kobject *kobj,
				  struct bin_attribute *bin_attr,
				  char *buf, loff_t pos, size_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct rp5c01_priv *priv = dev_get_drvdata(dev);
	ssize_t count;

	spin_lock_irq(&priv->lock);

	for (count = 0; size > 0 && pos < RP5C01_MODE; count++, size--) {
		u8 data = *buf++;

		rp5c01_write(priv,
			     RP5C01_MODE_TIMER_EN | RP5C01_MODE_RAM_BLOCK10,
			     RP5C01_MODE);
		rp5c01_write(priv, data >> 4, pos);
		rp5c01_write(priv,
			     RP5C01_MODE_TIMER_EN | RP5C01_MODE_RAM_BLOCK11,
			     RP5C01_MODE);
		rp5c01_write(priv, data & 0xf, pos++);
		rp5c01_write(priv, RP5C01_MODE_TIMER_EN | RP5C01_MODE_MODE01,
			     RP5C01_MODE);
	}

	spin_unlock_irq(&priv->lock);
	return count;
}

static int __init rp5c01_rtc_probe(struct platform_device *dev)
{
	struct resource *res;
	struct rp5c01_priv *priv;
	struct rtc_device *rtc;
	int error;

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	priv = devm_kzalloc(&dev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regs = devm_ioremap(&dev->dev, res->start, resource_size(res));
	if (!priv->regs)
		return -ENOMEM;

	sysfs_bin_attr_init(&priv->nvram_attr);
	priv->nvram_attr.attr.name = "nvram";
	priv->nvram_attr.attr.mode = S_IRUGO | S_IWUSR;
	priv->nvram_attr.read = rp5c01_nvram_read;
	priv->nvram_attr.write = rp5c01_nvram_write;
	priv->nvram_attr.size = RP5C01_MODE;

	spin_lock_init(&priv->lock);

	platform_set_drvdata(dev, priv);

	rtc = devm_rtc_device_register(&dev->dev, "rtc-rp5c01", &rp5c01_rtc_ops,
				  THIS_MODULE);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);
	priv->rtc = rtc;

	error = sysfs_create_bin_file(&dev->dev.kobj, &priv->nvram_attr);
	if (error)
		return error;

	return 0;
}

static int __exit rp5c01_rtc_remove(struct platform_device *dev)
{
	struct rp5c01_priv *priv = platform_get_drvdata(dev);

	sysfs_remove_bin_file(&dev->dev.kobj, &priv->nvram_attr);
	return 0;
}

static struct platform_driver rp5c01_rtc_driver = {
	.driver	= {
		.name	= "rtc-rp5c01",
	},
	.remove	= __exit_p(rp5c01_rtc_remove),
};

module_platform_driver_probe(rp5c01_rtc_driver, rp5c01_rtc_probe);

MODULE_AUTHOR("Geert Uytterhoeven <geert@linux-m68k.org>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Ricoh RP5C01 RTC driver");
MODULE_ALIAS("platform:rtc-rp5c01");
