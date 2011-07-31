/**
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
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/mfd/wm831x/core.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <linux/delay.h>

#if 0
#define DBG(x...)   printk(x)
#else
#define DBG(x...)
#endif 

struct wm831x_on {
	struct input_dev *dev;
	struct delayed_work work;
	struct wm831x *wm831x;
	int flag_resume;
	spinlock_t		flag_lock;
	struct wake_lock 	wm831x_on_wake;
};

struct wm831x_on *g_wm831x_on;

#ifndef CONFIG_KEYS_RK29
void rk28_send_wakeup_key(void)
{
        printk("%s\n", __FUNCTION__);
	if(!g_wm831x_on)
	{
		printk("%s:addr err!\n",__FUNCTION__);
		return;
	}
        input_report_key(g_wm831x_on->dev, KEY_POWER, 1);
        input_sync(g_wm831x_on->dev);
        input_report_key(g_wm831x_on->dev, KEY_POWER, 0);
        input_sync(g_wm831x_on->dev);
        printk("%s end\n", __FUNCTION__);
}
#endif

#if 1

static int wm831x_on_suspend_noirq(struct device *dev)
{
	DBG("%s\n",__FUNCTION__);
	
	if(!g_wm831x_on)
	{
		printk("%s:addr err!\n",__FUNCTION__);
		return -1;
	}

	spin_lock(&g_wm831x_on->flag_lock);
	g_wm831x_on->flag_resume = 0;
	spin_unlock(&g_wm831x_on->flag_lock);
	return 0;
}

static int wm831x_on_resume_noirq(struct device *dev)
{
	//int poll, ret;
	
	if(!g_wm831x_on)
	{
		printk("%s:addr err!\n",__FUNCTION__);
		return -1;
	}
	
	spin_lock(&g_wm831x_on->flag_lock);
	g_wm831x_on->flag_resume = 1;
	spin_unlock(&g_wm831x_on->flag_lock);
	
#if 0	
	ret = wm831x_reg_read(g_wm831x_on->wm831x, WM831X_ON_PIN_CONTROL);
	if (ret >= 0) {
		poll = !(ret & WM831X_ON_PIN_STS);	
		//poll = 1;
		input_report_key(g_wm831x_on->dev, KEY_POWER, poll);
		input_sync(g_wm831x_on->dev);
		DBG("%s:poll=%d,ret=0x%x\n",__FUNCTION__,poll,ret);
	} 
#endif
	DBG("%s\n",__FUNCTION__);
	return 0;
}

static struct dev_pm_ops wm831x_on_dev_pm_ops = {
	.suspend_noirq = wm831x_on_suspend_noirq,
	.resume_noirq = wm831x_on_resume_noirq,
};


static struct platform_driver wm831x_on_pm_driver = {
	.driver		= {
		.name	= "wm831x_on",
		.pm	= &wm831x_on_dev_pm_ops,
	},
};

static struct platform_device wm831x_on_pm_device = {
	.name		= "wm831x_on",
	.id		= -1,
	.dev = {
		.driver = &wm831x_on_pm_driver.driver,
	}
	
};

static inline void wm831x_on_pm_init(void)
{
	if (platform_driver_register(&wm831x_on_pm_driver) == 0)
		(void) platform_device_register(&wm831x_on_pm_device);
}


#endif	

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
		DBG("%s:poll=%d,ret=0x%x\n",__FUNCTION__,poll,ret);
	} else {
		dev_err(wm831x->dev, "Failed to read ON status: %d\n", ret);
		poll = 1;
	}

	if (poll)
		schedule_delayed_work(&wm831x_on->work, 2);
	else
		wake_unlock(&wm831x->handle_wake);
		//wake_unlock(&wm831x_on->wm831x_on_wake);
}

#if 0
static irqreturn_t wm831x_on_irq(int irq, void *data)
{
	struct wm831x_on *wm831x_on = data;
	wake_lock(&wm831x_on->wm831x_on_wake);
	schedule_delayed_work(&wm831x_on->work, 0);
	return IRQ_HANDLED;
}

#else

static irqreturn_t wm831x_on_irq(int irq, void *data)
{
	struct wm831x_on *wm831x_on = data;
	struct wm831x *wm831x = wm831x_on->wm831x;
	int poll, ret;
	
	//wake_lock(&wm831x_on->wm831x_on_wake);
		
	ret = wm831x_reg_read(wm831x, WM831X_ON_PIN_CONTROL);//it may be unpress if start to read register now
	if (ret >= 0) {
		if(wm831x_on->flag_resume)
		{
			poll = 1;
			spin_lock(&wm831x_on->flag_lock);
			wm831x_on->flag_resume = 0;
			spin_unlock(&wm831x_on->flag_lock);
		}
		else
		poll = !(ret & WM831X_ON_PIN_STS);
		input_report_key(wm831x_on->dev, KEY_POWER, poll);
		input_sync(wm831x_on->dev);
		DBG("%s:poll=%d,ret=0x%x\n",__FUNCTION__,poll,ret);
	} else {
		dev_err(wm831x->dev, "Failed to read ON status: %d\n", ret);
		poll = 1;
	}

	if (poll)
		schedule_delayed_work(&wm831x_on->work, 0);
	else
		wake_unlock(&wm831x->handle_wake);
		//wake_unlock(&wm831x_on->wm831x_on_wake);
			
	return IRQ_HANDLED;
}

#endif

static int __devinit wm831x_on_probe(struct platform_device *pdev)
{
	struct wm831x *wm831x = dev_get_drvdata(pdev->dev.parent);
	struct wm831x_on *wm831x_on = NULL;
	int irq = platform_get_irq(pdev, 0);
	int ret;
	printk("%s irq=%d\n", __FUNCTION__,irq);
	wm831x_on = kzalloc(sizeof(struct wm831x_on), GFP_KERNEL);
	if (!wm831x_on) {
		dev_err(&pdev->dev, "Can't allocate data\n");
		return -ENOMEM;
	}

	
	wm831x_on->wm831x = wm831x;
	INIT_DELAYED_WORK(&wm831x_on->work, wm831x_poll_on);
	wake_lock_init(&wm831x_on->wm831x_on_wake, WAKE_LOCK_SUSPEND, "wm831x_on_wake");
	
	wm831x_on->dev = input_allocate_device();
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
	g_wm831x_on = wm831x_on;

	wm831x_on_pm_init();

	ret = request_threaded_irq(irq, NULL, wm831x_on_irq,
				   IRQF_TRIGGER_RISING, "wm831x_on",
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
	input_free_device(wm831x_on->dev);
err:
	kfree(wm831x_on);
	return ret;
}

static int __devexit wm831x_on_remove(struct platform_device *pdev)
{
	struct wm831x_on *wm831x_on = platform_get_drvdata(pdev);
	int irq = platform_get_irq(pdev, 0);

	free_irq(irq, wm831x_on);
	cancel_delayed_work_sync(&wm831x_on->work);
	input_unregister_device(wm831x_on->dev);
	kfree(wm831x_on);

	return 0;
}

static struct platform_driver wm831x_on_driver = {
	.probe		= wm831x_on_probe,
	.remove		= __devexit_p(wm831x_on_remove),
	.driver		= {
		.name	= "wm831x-on",
		.owner	= THIS_MODULE,
	},
};

static int __init wm831x_on_init(void)
{
	return platform_driver_register(&wm831x_on_driver);
}
module_init(wm831x_on_init);

static void __exit wm831x_on_exit(void)
{
	platform_driver_unregister(&wm831x_on_driver);
}
module_exit(wm831x_on_exit);

MODULE_ALIAS("platform:wm831x-on");
MODULE_DESCRIPTION("WM831x ON pin");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");

