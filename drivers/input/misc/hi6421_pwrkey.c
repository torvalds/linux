/*
 * hi6421_pwrkey.c - Hisilicon Hi6421 PMIC ONKEY driver
 *
 * Copyright (C) 2013 Hisilicon Ltd.
 * Copyright (C) 2013 Linaro Ltd.
 * Author: Haojian Zhuang <haojian.zhuang@linaro.org>
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
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/mfd/hi6421-pmic.h>

struct hi6421_onkey_info {
	struct input_dev	*idev;
	int			irq[4];
};

static irqreturn_t hi6421_onkey_handler(int irq, void *data)
{
	struct hi6421_onkey_info *info = (struct hi6421_onkey_info *)data;

	/* only handle power down & power up event at here */
	if (irq == info->irq[0]) {
		input_report_key(info->idev, KEY_POWER, 1);
		input_sync(info->idev);
	} else if (irq == info->irq[1]) {
		input_report_key(info->idev, KEY_POWER, 0);
		input_sync(info->idev);
	}
	return IRQ_HANDLED;
}

static int hi6421_onkey_probe(struct platform_device *pdev)
{
	struct hi6421_onkey_info *info;
	struct device *dev = &pdev->dev;
	int ret;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->idev = input_allocate_device();
	if (!info->idev) {
		dev_err(&pdev->dev, "Failed to allocate input device\n");
		return -ENOMEM;
	}
	info->idev->name = "hi6421_on";
	info->idev->phys = "hi6421_on/input0";
	info->idev->dev.parent = &pdev->dev;
	info->idev->evbit[0] = BIT_MASK(EV_KEY);
	__set_bit(KEY_POWER, info->idev->keybit);

	info->irq[0] = platform_get_irq_byname(pdev, "down");
	if (info->irq[0] < 0) {
		ret = -ENOENT;
		goto err_irq0;
	}
	ret = request_irq(info->irq[0], hi6421_onkey_handler,
			  IRQF_DISABLED, "down", info);
	if (ret < 0)
		goto err_irq0;
	info->irq[1] = platform_get_irq_byname(pdev, "up");
	if (info->irq[1] < 0) {
		ret = -ENOENT;
		goto err_irq1;
	}
	ret = request_irq(info->irq[1], hi6421_onkey_handler,
			  IRQF_DISABLED, "up", info);
	if (ret < 0)
		goto err_irq1;
	info->irq[2] = platform_get_irq_byname(pdev, "hold 1s");
	if (info->irq[2] < 0) {
		ret = -ENOENT;
		goto err_irq2;
	}
	ret = request_irq(info->irq[2], hi6421_onkey_handler,
			  IRQF_DISABLED, "hold 1s", info);
	if (ret < 0)
		goto err_irq2;
	info->irq[3] = platform_get_irq_byname(pdev, "hold 10s");
	if (info->irq[3] < 0) {
		ret = -ENOENT;
		goto err_irq3;
	}
	ret = request_irq(info->irq[3], hi6421_onkey_handler,
			  IRQF_DISABLED, "hold 10s", info);
	if (ret < 0)
		goto err_irq3;

	ret = input_register_device(info->idev);
	if (ret) {
		dev_err(&pdev->dev, "Can't register input device: %d\n", ret);
		goto err_reg;
	}

	platform_set_drvdata(pdev, info);
	return ret;
err_reg:
	free_irq(info->irq[3], info);
err_irq3:
	free_irq(info->irq[2], info);
err_irq2:
	free_irq(info->irq[1], info);
err_irq1:
	free_irq(info->irq[0], info);
err_irq0:
	input_free_device(info->idev);
	return ret;
}

static int hi6421_onkey_remove(struct platform_device *pdev)
{
	return 0;
}

static struct of_device_id hi6421_onkey_of_match[] = {
	{ .compatible = "hisilicon,hi6421-onkey", },
	{ },
};
MODULE_DEVICE_TABLE(of, hi6421_onkey_of_match);

static struct platform_driver hi6421_onkey_driver = {
	.probe		= hi6421_onkey_probe,
	.remove		= hi6421_onkey_remove,
	.driver		= {
		.owner		= THIS_MODULE,
		.name		= "hi6421-onkey",
		.of_match_table	= hi6421_onkey_of_match,
	},
};
module_platform_driver(hi6421_onkey_driver);

MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@linaro.org");
MODULE_DESCRIPTION("Hi6421 PMIC Power key driver");
MODULE_LICENSE("GPL v2");
