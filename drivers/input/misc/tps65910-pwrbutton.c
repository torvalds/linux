/**
 * tps65910-pwrbutton.c - TPS65910 Power Button Input Driver
 *
 * Copyright (C) 2010 Mistral Solutions Pvt Ltd <www.mistralsolutions.com>
 *
 * Based on twl4030-pwrbutton.c
 *
 * Written by Srinath.R <srinath@mistralsolutions.com>
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/i2c/tps65910.h>

#define TPS65910_PWR_PWRON_IRQ (1 << 2)


static irqreturn_t powerbutton_irq(int irq, void *_pwr)
{
	struct input_dev *pwr = _pwr;
	int err;
	u8 value;

#ifdef CONFIG_LOCKDEP
	/* WORKAROUND for lockdep forcing IRQF_DISABLED on us, which
	 * we don't want and can't tolerate since this is a threaded
	 * IRQ and can sleep due to the i2c reads it has to issue.
	 * Although it might be friendlier not to borrow this thread
	 * context...
	 */
	local_irq_enable();
#endif
	err = tps65910_i2c_read_u8(TPS65910_I2C_ID0, &value,
				TPS65910_REG_INT_STS);
	if (!err  && (value & TPS65910_PWR_PWRON_IRQ))  {

		if (value & TPS65910_PWR_PWRON_IRQ) {

			input_report_key(pwr, KEY_POWER,
					TPS65910_PWR_PWRON_IRQ);
			input_sync(pwr);
			return IRQ_HANDLED;
		}
	} else {
		dev_err(pwr->dev.parent, "tps65910: i2c error %d while reading"
			" TPS65910_REG_INT_STS register\n", err);
	}
	return IRQ_HANDLED;
}

static int __devinit tps65910_pwrbutton_probe(struct platform_device *pdev)
{
	struct input_dev *pwr;
	int irq = platform_get_irq(pdev, 0);
	int err;

	pwr = input_allocate_device();
	if (!pwr) {
		dev_dbg(&pdev->dev, "Can't allocate power button\n");
		return -ENOMEM;
	}

	pwr->evbit[0] = BIT_MASK(EV_KEY);
	pwr->keybit[BIT_WORD(KEY_POWER)] = BIT_MASK(KEY_POWER);
	pwr->name = "tps65910_pwrbutton";
	pwr->phys = "tps65910_pwrbutton/input0";
	pwr->dev.parent = &pdev->dev;

	err = request_irq(irq, powerbutton_irq,
			(IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING |
			IRQF_SHARED), "tps65910_pwrbutton", pwr);
	if (err < 0) {
		dev_dbg(&pdev->dev, "Can't get IRQ for pwrbutton: %d\n", err);
		goto free_input_dev;
	}

	err = input_register_device(pwr);
	if (err) {
		dev_dbg(&pdev->dev, "Can't register power button: %d\n", err);
		goto free_irq;
	}

	platform_set_drvdata(pdev, pwr);

	return 0;

free_irq:
	free_irq(irq, NULL);
free_input_dev:
	input_free_device(pwr);
	return err;
}

static int __devexit tps65910_pwrbutton_remove(struct platform_device *pdev)
{
	struct input_dev *pwr = platform_get_drvdata(pdev);
	int irq = platform_get_irq(pdev, 0);

	free_irq(irq, pwr);
	input_unregister_device(pwr);

	return 0;
}

struct platform_driver tps65910_pwrbutton_driver = {
	.probe		= tps65910_pwrbutton_probe,
	.remove		= __devexit_p(tps65910_pwrbutton_remove),
	.driver		= {
		.name	= "tps65910_pwrbutton",
		.owner	= THIS_MODULE,
	},
};

static int __init tps65910_pwrbutton_init(void)
{
	return platform_driver_register(&tps65910_pwrbutton_driver);
}
module_init(tps65910_pwrbutton_init);

static void __exit tps65910_pwrbutton_exit(void)
{
	platform_driver_unregister(&tps65910_pwrbutton_driver);
}
module_exit(tps65910_pwrbutton_exit);

MODULE_ALIAS("platform:tps65910_pwrbutton");
MODULE_DESCRIPTION("TPS65910 Power Button");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Srinath R <srinath@mistralsolutions.com>");

