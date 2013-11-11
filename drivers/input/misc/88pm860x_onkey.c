/*
 * 88pm860x_onkey.c - Marvell 88PM860x ONKEY driver
 *
 * Copyright (C) 2009-2010 Marvell International Ltd.
 *      Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/mfd/88pm860x.h>
#include <linux/slab.h>

#define PM8607_WAKEUP		0x0b

#define LONG_ONKEY_EN		(1 << 1)
#define ONKEY_STATUS		(1 << 0)

struct pm860x_onkey_info {
	struct input_dev	*idev;
	struct pm860x_chip	*chip;
	struct i2c_client	*i2c;
	struct device		*dev;
	int			irq;
};

/* 88PM860x gives us an interrupt when ONKEY is held */
static irqreturn_t pm860x_onkey_handler(int irq, void *data)
{
	struct pm860x_onkey_info *info = data;
	int ret;

	ret = pm860x_reg_read(info->i2c, PM8607_STATUS_2);
	ret &= ONKEY_STATUS;
	input_report_key(info->idev, KEY_POWER, ret);
	input_sync(info->idev);

	/* Enable 8-second long onkey detection */
	pm860x_set_bits(info->i2c, PM8607_WAKEUP, 3, LONG_ONKEY_EN);
	return IRQ_HANDLED;
}

static int pm860x_onkey_probe(struct platform_device *pdev)
{
	struct pm860x_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct pm860x_onkey_info *info;
	int irq, ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "No IRQ resource!\n");
		return -EINVAL;
	}

	info = kzalloc(sizeof(struct pm860x_onkey_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->chip = chip;
	info->i2c = (chip->id == CHIP_PM8607) ? chip->client : chip->companion;
	info->dev = &pdev->dev;
	info->irq = irq;

	info->idev = input_allocate_device();
	if (!info->idev) {
		dev_err(chip->dev, "Failed to allocate input dev\n");
		ret = -ENOMEM;
		goto out;
	}

	info->idev->name = "88pm860x_on";
	info->idev->phys = "88pm860x_on/input0";
	info->idev->id.bustype = BUS_I2C;
	info->idev->dev.parent = &pdev->dev;
	info->idev->evbit[0] = BIT_MASK(EV_KEY);
	info->idev->keybit[BIT_WORD(KEY_POWER)] = BIT_MASK(KEY_POWER);

	ret = input_register_device(info->idev);
	if (ret) {
		dev_err(chip->dev, "Can't register input device: %d\n", ret);
		goto out_reg;
	}

	ret = request_threaded_irq(info->irq, NULL, pm860x_onkey_handler,
				   IRQF_ONESHOT, "onkey", info);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to request IRQ: #%d: %d\n",
			info->irq, ret);
		goto out_irq;
	}

	platform_set_drvdata(pdev, info);
	device_init_wakeup(&pdev->dev, 1);

	return 0;

out_irq:
	input_unregister_device(info->idev);
	kfree(info);
	return ret;

out_reg:
	input_free_device(info->idev);
out:
	kfree(info);
	return ret;
}

static int pm860x_onkey_remove(struct platform_device *pdev)
{
	struct pm860x_onkey_info *info = platform_get_drvdata(pdev);

	free_irq(info->irq, info);
	input_unregister_device(info->idev);
	kfree(info);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int pm860x_onkey_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pm860x_chip *chip = dev_get_drvdata(pdev->dev.parent);

	if (device_may_wakeup(dev))
		chip->wakeup_flag |= 1 << PM8607_IRQ_ONKEY;
	return 0;
}
static int pm860x_onkey_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pm860x_chip *chip = dev_get_drvdata(pdev->dev.parent);

	if (device_may_wakeup(dev))
		chip->wakeup_flag &= ~(1 << PM8607_IRQ_ONKEY);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(pm860x_onkey_pm_ops, pm860x_onkey_suspend, pm860x_onkey_resume);

static struct platform_driver pm860x_onkey_driver = {
	.driver		= {
		.name	= "88pm860x-onkey",
		.owner	= THIS_MODULE,
		.pm	= &pm860x_onkey_pm_ops,
	},
	.probe		= pm860x_onkey_probe,
	.remove		= pm860x_onkey_remove,
};
module_platform_driver(pm860x_onkey_driver);

MODULE_DESCRIPTION("Marvell 88PM860x ONKEY driver");
MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@marvell.com>");
MODULE_LICENSE("GPL");
