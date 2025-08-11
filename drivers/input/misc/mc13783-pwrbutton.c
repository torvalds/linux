/*
 * Copyright (C) 2011 Philippe Rétornaz
 *
 * Based on twl4030-pwrbutton driver by:
 *     Peter De Schrijver <peter.de-schrijver@nokia.com>
 *     Felipe Balbi <felipe.balbi@nokia.com>
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
 * Foundation, Inc., 51 Franklin Street, Suite 500, Boston, MA 02110-1335  USA
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/mfd/mc13783.h>
#include <linux/sched.h>
#include <linux/slab.h>

struct mc13783_pwrb {
	struct input_dev *pwr;
	struct mc13xxx *mc13783;
#define MC13783_PWRB_B1_POL_INVERT	(1 << 0)
#define MC13783_PWRB_B2_POL_INVERT	(1 << 1)
#define MC13783_PWRB_B3_POL_INVERT	(1 << 2)
	int flags;
	unsigned short keymap[3];
};

#define MC13783_REG_INTERRUPT_SENSE_1		5
#define MC13783_IRQSENSE1_ONOFD1S		(1 << 3)
#define MC13783_IRQSENSE1_ONOFD2S		(1 << 4)
#define MC13783_IRQSENSE1_ONOFD3S		(1 << 5)

#define MC13783_REG_POWER_CONTROL_2		15
#define MC13783_POWER_CONTROL_2_ON1BDBNC	4
#define MC13783_POWER_CONTROL_2_ON2BDBNC	6
#define MC13783_POWER_CONTROL_2_ON3BDBNC	8
#define MC13783_POWER_CONTROL_2_ON1BRSTEN	(1 << 1)
#define MC13783_POWER_CONTROL_2_ON2BRSTEN	(1 << 2)
#define MC13783_POWER_CONTROL_2_ON3BRSTEN	(1 << 3)

static irqreturn_t button_irq(int irq, void *_priv)
{
	struct mc13783_pwrb *priv = _priv;
	int val;

	mc13xxx_reg_read(priv->mc13783, MC13783_REG_INTERRUPT_SENSE_1, &val);

	switch (irq) {
	case MC13783_IRQ_ONOFD1:
		val = val & MC13783_IRQSENSE1_ONOFD1S ? 1 : 0;
		if (priv->flags & MC13783_PWRB_B1_POL_INVERT)
			val ^= 1;
		input_report_key(priv->pwr, priv->keymap[0], val);
		break;

	case MC13783_IRQ_ONOFD2:
		val = val & MC13783_IRQSENSE1_ONOFD2S ? 1 : 0;
		if (priv->flags & MC13783_PWRB_B2_POL_INVERT)
			val ^= 1;
		input_report_key(priv->pwr, priv->keymap[1], val);
		break;

	case MC13783_IRQ_ONOFD3:
		val = val & MC13783_IRQSENSE1_ONOFD3S ? 1 : 0;
		if (priv->flags & MC13783_PWRB_B3_POL_INVERT)
			val ^= 1;
		input_report_key(priv->pwr, priv->keymap[2], val);
		break;
	}

	input_sync(priv->pwr);

	return IRQ_HANDLED;
}

static int mc13783_pwrbutton_probe(struct platform_device *pdev)
{
	const struct mc13xxx_buttons_platform_data *pdata;
	struct mc13xxx *mc13783 = dev_get_drvdata(pdev->dev.parent);
	struct input_dev *pwr;
	struct mc13783_pwrb *priv;
	int err = 0;
	int reg = 0;

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata) {
		dev_err(&pdev->dev, "missing platform data\n");
		return -ENODEV;
	}

	pwr = input_allocate_device();
	if (!pwr) {
		dev_dbg(&pdev->dev, "Can't allocate power button\n");
		return -ENOMEM;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		err = -ENOMEM;
		dev_dbg(&pdev->dev, "Can't allocate power button\n");
		goto free_input_dev;
	}

	reg |= (pdata->b1on_flags & 0x3) << MC13783_POWER_CONTROL_2_ON1BDBNC;
	reg |= (pdata->b2on_flags & 0x3) << MC13783_POWER_CONTROL_2_ON2BDBNC;
	reg |= (pdata->b3on_flags & 0x3) << MC13783_POWER_CONTROL_2_ON3BDBNC;

	priv->pwr = pwr;
	priv->mc13783 = mc13783;

	mc13xxx_lock(mc13783);

	if (pdata->b1on_flags & MC13783_BUTTON_ENABLE) {
		priv->keymap[0] = pdata->b1on_key;
		if (pdata->b1on_key != KEY_RESERVED)
			__set_bit(pdata->b1on_key, pwr->keybit);

		if (pdata->b1on_flags & MC13783_BUTTON_POL_INVERT)
			priv->flags |= MC13783_PWRB_B1_POL_INVERT;

		if (pdata->b1on_flags & MC13783_BUTTON_RESET_EN)
			reg |= MC13783_POWER_CONTROL_2_ON1BRSTEN;

		err = mc13xxx_irq_request(mc13783, MC13783_IRQ_ONOFD1,
					  button_irq, "b1on", priv);
		if (err) {
			dev_dbg(&pdev->dev, "Can't request irq\n");
			goto free_priv;
		}
	}

	if (pdata->b2on_flags & MC13783_BUTTON_ENABLE) {
		priv->keymap[1] = pdata->b2on_key;
		if (pdata->b2on_key != KEY_RESERVED)
			__set_bit(pdata->b2on_key, pwr->keybit);

		if (pdata->b2on_flags & MC13783_BUTTON_POL_INVERT)
			priv->flags |= MC13783_PWRB_B2_POL_INVERT;

		if (pdata->b2on_flags & MC13783_BUTTON_RESET_EN)
			reg |= MC13783_POWER_CONTROL_2_ON2BRSTEN;

		err = mc13xxx_irq_request(mc13783, MC13783_IRQ_ONOFD2,
					  button_irq, "b2on", priv);
		if (err) {
			dev_dbg(&pdev->dev, "Can't request irq\n");
			goto free_irq_b1;
		}
	}

	if (pdata->b3on_flags & MC13783_BUTTON_ENABLE) {
		priv->keymap[2] = pdata->b3on_key;
		if (pdata->b3on_key != KEY_RESERVED)
			__set_bit(pdata->b3on_key, pwr->keybit);

		if (pdata->b3on_flags & MC13783_BUTTON_POL_INVERT)
			priv->flags |= MC13783_PWRB_B3_POL_INVERT;

		if (pdata->b3on_flags & MC13783_BUTTON_RESET_EN)
			reg |= MC13783_POWER_CONTROL_2_ON3BRSTEN;

		err = mc13xxx_irq_request(mc13783, MC13783_IRQ_ONOFD3,
					  button_irq, "b3on", priv);
		if (err) {
			dev_dbg(&pdev->dev, "Can't request irq: %d\n", err);
			goto free_irq_b2;
		}
	}

	mc13xxx_reg_rmw(mc13783, MC13783_REG_POWER_CONTROL_2, 0x3FE, reg);

	mc13xxx_unlock(mc13783);

	pwr->name = "mc13783_pwrbutton";
	pwr->phys = "mc13783_pwrbutton/input0";
	pwr->dev.parent = &pdev->dev;

	pwr->keycode = priv->keymap;
	pwr->keycodemax = ARRAY_SIZE(priv->keymap);
	pwr->keycodesize = sizeof(priv->keymap[0]);
	__set_bit(EV_KEY, pwr->evbit);

	err = input_register_device(pwr);
	if (err) {
		dev_dbg(&pdev->dev, "Can't register power button: %d\n", err);
		goto free_irq;
	}

	platform_set_drvdata(pdev, priv);

	return 0;

free_irq:
	mc13xxx_lock(mc13783);

	if (pdata->b3on_flags & MC13783_BUTTON_ENABLE)
		mc13xxx_irq_free(mc13783, MC13783_IRQ_ONOFD3, priv);

free_irq_b2:
	if (pdata->b2on_flags & MC13783_BUTTON_ENABLE)
		mc13xxx_irq_free(mc13783, MC13783_IRQ_ONOFD2, priv);

free_irq_b1:
	if (pdata->b1on_flags & MC13783_BUTTON_ENABLE)
		mc13xxx_irq_free(mc13783, MC13783_IRQ_ONOFD1, priv);

free_priv:
	mc13xxx_unlock(mc13783);
	kfree(priv);

free_input_dev:
	input_free_device(pwr);

	return err;
}

static void mc13783_pwrbutton_remove(struct platform_device *pdev)
{
	struct mc13783_pwrb *priv = platform_get_drvdata(pdev);
	const struct mc13xxx_buttons_platform_data *pdata;

	pdata = dev_get_platdata(&pdev->dev);

	mc13xxx_lock(priv->mc13783);

	if (pdata->b3on_flags & MC13783_BUTTON_ENABLE)
		mc13xxx_irq_free(priv->mc13783, MC13783_IRQ_ONOFD3, priv);
	if (pdata->b2on_flags & MC13783_BUTTON_ENABLE)
		mc13xxx_irq_free(priv->mc13783, MC13783_IRQ_ONOFD2, priv);
	if (pdata->b1on_flags & MC13783_BUTTON_ENABLE)
		mc13xxx_irq_free(priv->mc13783, MC13783_IRQ_ONOFD1, priv);

	mc13xxx_unlock(priv->mc13783);

	input_unregister_device(priv->pwr);
	kfree(priv);
}

static struct platform_driver mc13783_pwrbutton_driver = {
	.probe		= mc13783_pwrbutton_probe,
	.remove		= mc13783_pwrbutton_remove,
	.driver		= {
		.name	= "mc13783-pwrbutton",
	},
};

module_platform_driver(mc13783_pwrbutton_driver);

MODULE_ALIAS("platform:mc13783-pwrbutton");
MODULE_DESCRIPTION("MC13783 Power Button");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Philippe Retornaz");
