/*
 * wm831x-on.c - WM831X ON pin driver
 *
 * Copyright (C) 2009 Wolfson Microelectronics plc
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
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/mfd/wm831x/core.h>

struct wm831x_on {
	struct input_dev *dev;
	struct delayed_work work;
	struct wm831x *wm831x;
};

/*
 * The chip gives us an interrupt when the ON pin is asserted but we
 * then need to poll to see when the pin is deasserted.
 */
static void wm831x_poll_on(struct work_struct *work)
{
	struct wm831x_on *wm831x_on = container_of(work, struct wm831x_on,
						   work.work);
	struct wm831x *wm831x = wm831x_on->wm831x;
	int poll, ret;

	ret = wm831x_reg_read(wm831x, WM831X_ON_PIN_CONTROL);
	if (ret >= 0) {
		poll = !(ret & WM831X_ON_PIN_STS);

		input_report_key(wm831x_on->dev, KEY_POWER, poll);
		input_sync(wm831x_on->dev);
	} else {
		dev_err(wm831x->dev, "Failed to read ON status: %d\n", ret);
		poll = 1;
	}

	if (poll)
		schedule_delayed_work(&wm831x_on->work, 100);
}

static irqreturn_t wm831x_on_irq(int irq, void *data)
{
	struct wm831x_on *wm831x_on = data;

	schedule_delayed_work(&wm831x_on->work, 0);

	return IRQ_HANDLED;
}

static int wm831x_on_probe(struct platform_device *pdev)
{
	struct wm831x *wm831x = dev_get_drvdata(pdev->dev.parent);
	struct wm831x_on *wm831x_on;
	int irq = wm831x_irq(wm831x, platform_get_irq(pdev, 0));
	int ret;

	wm831x_on = devm_kzalloc(&pdev->dev, sizeof(struct wm831x_on),
				 GFP_KERNEL);
	if (!wm831x_on) {
		dev_err(&pdev->dev, "Can't allocate data\n");
		return -ENOMEM;
	}

	wm831x_on->wm831x = wm831x;
	INIT_DELAYED_WORK(&wm831x_on->work, wm831x_poll_on);

	wm831x_on->dev = devm_input_allocate_device(&pdev->dev);
	if (!wm831x_on->dev) {
		dev_err(&pdev->dev, "Can't allocate input dev\n");
		ret = -ENOMEM;
		goto err;
	}

	wm831x_on->dev->evbit[0] = BIT_MASK(EV_KEY);
	wm831x_on->dev->keybit[BIT_WORD(KEY_POWER)] = BIT_MASK(KEY_POWER);
	wm831x_on->dev->name = "wm831x_on";
	wm831x_on->dev->phys = "wm831x_on/input0";
	wm831x_on->dev->dev.parent = &pdev->dev;

	ret = request_threaded_irq(irq, NULL, wm831x_on_irq,
				   IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				   "wm831x_on",
				   wm831x_on);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to request IRQ: %d\n", ret);
		goto err_input_dev;
	}
	ret = input_register_device(wm831x_on->dev);
	if (ret) {
		dev_dbg(&pdev->dev, "Can't register input device: %d\n", ret);
		goto err_irq;
	}

	platform_set_drvdata(pdev, wm831x_on);

	return 0;

err_irq:
	free_irq(irq, wm831x_on);
err_input_dev:
err:
	return ret;
}

static void wm831x_on_remove(struct platform_device *pdev)
{
	struct wm831x_on *wm831x_on = platform_get_drvdata(pdev);
	int irq = platform_get_irq(pdev, 0);

	free_irq(irq, wm831x_on);
	cancel_delayed_work_sync(&wm831x_on->work);
}

static struct platform_driver wm831x_on_driver = {
	.probe		= wm831x_on_probe,
	.remove		= wm831x_on_remove,
	.driver		= {
		.name	= "wm831x-on",
	},
};
module_platform_driver(wm831x_on_driver);

MODULE_ALIAS("platform:wm831x-on");
MODULE_DESCRIPTION("WM831x ON pin");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");

