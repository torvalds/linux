/*
 *
 * TWL6030 MADC module driver-This driver only implements the ADC read
 * functions
 *
 * Copyright (C) 2011 Samsung Telecommunications of America
 *
 * Based on twl4030-madc.c
 * Copyright (C) 2008 Nokia Corporation
 * Mikko Ylinen <mikko.k.ylinen@nokia.com>
 *
 * Amit Kucheria <amit.kucheria@canonical.com>
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

#include <linux/init.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/i2c/twl.h>
#include <linux/i2c/twl6030-madc.h>
#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>
#include <linux/bitops.h>
#include <linux/jiffies.h>
#include <linux/types.h>
#include <linux/gfp.h>
#include <linux/err.h>
#include <linux/wakelock.h>

#define GPADCS		(1 << 1)
#define GPADCR		(1 << 0)
#define REG_TOGGLE1	0x90

#define DRIVER_NAME	(twl6030_madc_driver.driver.name)
static struct platform_driver twl6030_madc_driver;

/*
 * struct twl6030_madc_data - a container for madc info
 * @dev - pointer to device structure for madc
 * @lock - mutex protecting this data structure
 */
struct twl6030_madc_data {
	struct device *dev;
	struct mutex lock;
	struct dentry		*file;
	struct wake_lock wakelock;
};

static struct twl6030_madc_data *twl6030_madc;
static u8 gpadc_ctrl_reg;

static inline int twl6030_madc_start_conversion(struct twl6030_madc_data *madc)
{
	int ret;

	ret = twl_i2c_write_u8(TWL6030_MODULE_ID1, GPADCS, REG_TOGGLE1);
	if (ret) {
		dev_err(madc->dev, "unable to write register 0x%X\n",
			REG_TOGGLE1);
		return ret;
	}

	udelay(100);
	ret = twl_i2c_write_u8(TWL_MODULE_MADC, TWL6030_MADC_SP1,
				TWL6030_MADC_CTRL_P1);
	if (ret) {
		dev_err(madc->dev, "unable to write register 0x%X\n",
			TWL6030_MADC_CTRL_P1);
		return ret;
	}
	return 0;
}

/*
 * Function that waits for conversion to be ready
 * @madc - pointer to twl4030_madc_data struct
 * @timeout_ms - timeout value in milliseconds
 * @status_reg - ctrl register
 * returns 0 if succeeds else a negative error value
 */
static int twl6030_madc_wait_conversion_ready(struct twl6030_madc_data *madc,
					      unsigned int timeout_ms,
					      u8 status_reg)
{
	unsigned long timeout;
	unsigned long delta;
	u8 reg;
	int ret;

	delta = msecs_to_jiffies(timeout_ms);

	if (delta < 2)
		delta = 2;

	wake_lock(&madc->wakelock);
	timeout = jiffies + delta;
	do {
		ret = twl_i2c_read_u8(TWL6030_MODULE_MADC, &reg, status_reg);
		if (ret) {
			dev_err(madc->dev,
				"unable to read status register 0x%X\n",
				status_reg);
			goto unlock;
		}
		if (!(reg & TWL6030_MADC_BUSY) && (reg & TWL6030_MADC_EOCP1)) {
			ret = 0;
			goto unlock;
		}

		if (time_after(jiffies, timeout))
			break;

		usleep_range(500, 2000);
	} while (1);

	dev_err(madc->dev, "conversion timeout, ctrl_px=0x%08x\n", reg);
	ret = -EAGAIN;

unlock:
	wake_unlock(&madc->wakelock);
	return ret;
}

/*
 * Function to read a particular channel value.
 * @madc - pointer to struct twl6030_madc_data
 * @reg - lsb of ADC Channel
 * If the i2c read fails it returns an error else returns 0.
 */
static int twl6030_madc_channel_raw_read(struct twl6030_madc_data *madc,
					u8 reg)
{
	u8 msb, lsb;
	int ret;

	mutex_lock(&madc->lock);
	ret = twl6030_madc_start_conversion(twl6030_madc);
	if (ret)
		goto unlock;

	ret = twl6030_madc_wait_conversion_ready(twl6030_madc, 5,
						TWL6030_MADC_CTRL_P1);
	if (ret)
		goto unlock;

	/*
	 * For each ADC channel, we have MSB and LSB register
	 * pair. MSB address is always LSB address+1. reg parameter is
	 * the address of LSB register
	 */
	ret = twl_i2c_read_u8(TWL6030_MODULE_MADC, &msb, reg + 1);
	if (ret) {
		dev_err(madc->dev, "unable to read MSB register 0x%X\n",
			reg + 1);
		goto unlock;
	}
	ret = twl_i2c_read_u8(TWL6030_MODULE_MADC, &lsb, reg);
	if (ret) {
		dev_err(madc->dev, "unable to read LSB register 0x%X\n", reg);
		goto unlock;
	}
	ret = (int)((msb << 8) | lsb);
unlock:
	/* Disable GPADC for power savings. */
	twl_i2c_write_u8(TWL6030_MODULE_ID1, GPADCR, REG_TOGGLE1);
	mutex_unlock(&madc->lock);
	return ret;
}

/*
 * Return channel value
 * Or < 0 on failure.
 */
int twl6030_get_madc_conversion(int channel_no)
{
	u8 reg = TWL6030_MADC_GPCH0_LSB + (2 * channel_no);
	if (!twl6030_madc) {
		pr_err("%s: No ADC device\n", __func__);
		return -EINVAL;
	}
	if (channel_no >= TWL6030_MADC_MAX_CHANNELS) {
		dev_err(twl6030_madc->dev,
			"%s: Channel number (%d) exceeds max (%d)\n",
			__func__, channel_no, TWL6030_MADC_MAX_CHANNELS);
		return -EINVAL;
	}

	return twl6030_madc_channel_raw_read(twl6030_madc, reg);
}
EXPORT_SYMBOL_GPL(twl6030_get_madc_conversion);

#ifdef CONFIG_DEBUG_FS

static int debug_twl6030_madc_show(struct seq_file *s, void *_)
{
	int i, result;
	for (i = 0; i < TWL6030_MADC_MAX_CHANNELS; i++) {
		result = twl6030_get_madc_conversion(i);
		seq_printf(s, "channel %3d returns result %d\n",
			i, result);
	}
	return 0;
}

static int debug_twl6030_madc_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_twl6030_madc_show, inode->i_private);
}

static const struct file_operations debug_fops = {
	.open		= debug_twl6030_madc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

#define DEBUG_FOPS	(&debug_fops)

#else
#define DEBUG_FOPS	NULL
#endif

/*
 * Initialize MADC
 */
static int __devinit twl6030_madc_probe(struct platform_device *pdev)
{
	struct twl6030_madc_data *madc;
	struct twl4030_madc_platform_data *pdata = pdev->dev.platform_data;

	if (!pdata) {
		dev_err(&pdev->dev, "platform_data not available\n");
		return -EINVAL;
	}
	madc = kzalloc(sizeof(*madc), GFP_KERNEL);
	if (!madc)
		return -ENOMEM;

	platform_set_drvdata(pdev, madc);
	madc->dev = &pdev->dev;
	mutex_init(&madc->lock);
	madc->file = debugfs_create_file(DRIVER_NAME, S_IRUGO, NULL,
					madc, DEBUG_FOPS);
	wake_lock_init(&madc->wakelock, WAKE_LOCK_SUSPEND, "twl6030 adc");
	twl6030_madc = madc;
	return 0;
}

static int __devexit twl6030_madc_remove(struct platform_device *pdev)
{
	struct twl6030_madc_data *madc = platform_get_drvdata(pdev);

	wake_lock_destroy(&madc->wakelock);
	mutex_destroy(&madc->lock);
	free_irq(platform_get_irq(pdev, 0), madc);
	platform_set_drvdata(pdev, NULL);
	twl6030_madc = NULL;
	debugfs_remove(madc->file);
	kfree(madc);

	return 0;
}

static int twl6030_madc_suspend(struct device *pdev)
{
	int ret;
	u8 reg_val;

	ret = twl_i2c_read_u8(TWL_MODULE_MADC, &reg_val, TWL6030_MADC_CTRL);
	if (!ret) {
		reg_val &= ~(TWL6030_MADC_TEMP1_EN);
		ret = twl_i2c_write_u8(TWL_MODULE_MADC, reg_val,
					TWL6030_MADC_CTRL);
	}

	if (ret) {
		dev_err(twl6030_madc->dev, "unable to disable madc temp1!\n");
		gpadc_ctrl_reg = TWL6030_MADC_TEMP1_EN;
	} else
		gpadc_ctrl_reg = reg_val;

	return 0;
};

static int twl6030_madc_resume(struct device *pdev)
{
	int ret;

	if (!(gpadc_ctrl_reg & TWL6030_MADC_TEMP1_EN)) {
		gpadc_ctrl_reg |= TWL6030_MADC_TEMP1_EN;
		ret = twl_i2c_write_u8(TWL_MODULE_MADC, gpadc_ctrl_reg,
					TWL6030_MADC_CTRL);
		if (ret)
			dev_err(twl6030_madc->dev,
				"unable to enable madc temp1!\n");
	}

	return 0;
};

static const struct dev_pm_ops twl6030_madc_pm_ops = {
	.suspend = twl6030_madc_suspend,
	.resume = twl6030_madc_resume,
};

static struct platform_driver twl6030_madc_driver = {
	.probe = twl6030_madc_probe,
	.remove = __exit_p(twl6030_madc_remove),
	.driver = {
		   .name = "twl6030_madc",
		   .owner = THIS_MODULE,
		   .pm = &twl6030_madc_pm_ops,
		   },
};

static int __init twl6030_madc_init(void)
{
	return platform_driver_register(&twl6030_madc_driver);
}

module_init(twl6030_madc_init);

static void __exit twl6030_madc_exit(void)
{
	platform_driver_unregister(&twl6030_madc_driver);
}

module_exit(twl6030_madc_exit);

MODULE_DESCRIPTION("TWL6030 ADC driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("J Keerthy");
MODULE_ALIAS("platform:twl6030_madc");
