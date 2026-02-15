/*
 * TWL4030 Power Button Input Driver
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Written by Peter De Schrijver <peter.de-schrijver@nokia.com>
 * Several fixes by Felipe Balbi <felipe.balbi@nokia.com>
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

#include <linux/bits.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/platform_device.h>
#include <linux/mfd/twl.h>

#define PWR_PWRON_IRQ BIT(0)

struct twl_pwrbutton_chipdata {
	u8 status_reg;
	bool need_manual_irq;
};

static const struct twl_pwrbutton_chipdata twl4030_chipdata = {
	.status_reg = 0xf,
	.need_manual_irq = false,
};

static const struct twl_pwrbutton_chipdata twl6030_chipdata = {
	.status_reg = 0x2,
	.need_manual_irq = true,
};

static irqreturn_t powerbutton_irq(int irq, void *_pwr)
{
	struct input_dev *pwr = _pwr;
	const struct twl_pwrbutton_chipdata *pdata = dev_get_drvdata(pwr->dev.parent);
	int err;
	u8 value;

	err = twl_i2c_read_u8(TWL_MODULE_PM_MASTER, &value, pdata->status_reg);
	if (!err)  {
		pm_wakeup_event(pwr->dev.parent, 0);
		input_report_key(pwr, KEY_POWER, value & PWR_PWRON_IRQ);
		input_sync(pwr);
	} else {
		dev_err(pwr->dev.parent, "twl4030: i2c error %d while reading"
			" TWL4030 PM_MASTER STS_HW_CONDITIONS register\n", err);
	}

	return IRQ_HANDLED;
}

static int twl4030_pwrbutton_probe(struct platform_device *pdev)
{
	const struct twl_pwrbutton_chipdata *pdata;
	struct input_dev *pwr;
	int irq = platform_get_irq(pdev, 0);
	int err;

	pdata = device_get_match_data(&pdev->dev);
	if (!pdata)
		return -EINVAL;

	platform_set_drvdata(pdev, (void *)pdata);

	pwr = devm_input_allocate_device(&pdev->dev);
	if (!pwr) {
		dev_err(&pdev->dev, "Can't allocate power button\n");
		return -ENOMEM;
	}

	input_set_capability(pwr, EV_KEY, KEY_POWER);
	pwr->name = "twl4030_pwrbutton";
	pwr->phys = "twl4030_pwrbutton/input0";
	pwr->dev.parent = &pdev->dev;

	err = devm_request_threaded_irq(&pdev->dev, irq, NULL, powerbutton_irq,
			IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING |
			IRQF_ONESHOT,
			"twl4030_pwrbutton", pwr);
	if (err < 0) {
		dev_err(&pdev->dev, "Can't get IRQ for pwrbutton: %d\n", err);
		return err;
	}

	err = input_register_device(pwr);
	if (err) {
		dev_err(&pdev->dev, "Can't register power button: %d\n", err);
		return err;
	}

	if (pdata->need_manual_irq) {
		err = twl6030_interrupt_unmask(0x01, REG_INT_MSK_LINE_A);
		if (err)
			return err;

		err = twl6030_interrupt_unmask(0x01, REG_INT_MSK_STS_A);
		if (err)
			return err;
	}

	device_init_wakeup(&pdev->dev, true);

	return 0;
}

static void twl4030_pwrbutton_remove(struct platform_device *pdev)
{
	const struct twl_pwrbutton_chipdata *pdata = platform_get_drvdata(pdev);

	if (pdata->need_manual_irq) {
		twl6030_interrupt_mask(0x01, REG_INT_MSK_LINE_A);
		twl6030_interrupt_mask(0x01, REG_INT_MSK_STS_A);
	}
}

static const struct of_device_id twl4030_pwrbutton_dt_match_table[] = {
	{
		.compatible = "ti,twl4030-pwrbutton",
		.data = &twl4030_chipdata,
	},
	{
		.compatible = "ti,twl6030-pwrbutton",
		.data = &twl6030_chipdata,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, twl4030_pwrbutton_dt_match_table);

static struct platform_driver twl4030_pwrbutton_driver = {
	.probe		= twl4030_pwrbutton_probe,
	.remove		= twl4030_pwrbutton_remove,
	.driver		= {
		.name	= "twl4030_pwrbutton",
		.of_match_table = twl4030_pwrbutton_dt_match_table,
	},
};
module_platform_driver(twl4030_pwrbutton_driver);

MODULE_ALIAS("platform:twl4030_pwrbutton");
MODULE_DESCRIPTION("Triton2 Power Button");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Peter De Schrijver <peter.de-schrijver@nokia.com>");
MODULE_AUTHOR("Felipe Balbi <felipe.balbi@nokia.com>");

