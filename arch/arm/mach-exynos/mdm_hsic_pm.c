/* linux/arch/arm/mach-xxxx/mdm_hsic_pm.c
 * Copyright (C) 2010 Samsung Electronics. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/usb.h>
#include <linux/pm_runtime.h>
#include <plat/gpio-cfg.h>
#include <linux/mdm_hsic_pm.h>
#include <linux/suspend.h>
#include <linux/wakelock.h>
#include <mach/subsystem_restart.h>
#include <mach/sec_modem.h>
#include <linux/msm_charm.h>
#include "mdm_private.h"
#include <linux/wakelock.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/usb/ehci_def.h>

#ifdef CONFIG_CPU_FREQ_TETHERING
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <mach/mdm2.h>
#include <linux/usb/android_composite.h>
#endif

#define EXTERNAL_MODEM "external_modem"
#define EHCI_REG_DUMP
#define DEFAULT_RAW_WAKE_TIME (0*HZ)

BLOCKING_NOTIFIER_HEAD(mdm_reset_notifier_list);

/**
 * TODO:
 * pm notifier register
 *
 * think the way to use notifier to register or unregister device
 *
 * disconnect also can be notified
 *
 * block request under kernel power off seq.
 *
 * in suspend function if busy has set, return
 *
 */

/**
 * struct mdm_hsic_pm_data - hsic pm platform driver private data
 *	provide data and information for pm state transition
 *
 * @name:		name of this pm driver
 * @udev:		usb driver for resuming device from device request
 * @intf_cnt:		count of registered interface driver
 * @block_request:	block and ignore requested resume interrupt
 * @state_busy:		it is not determined to use, it can be replaced to
 *			rpm status check
 * @pm_notifier:	notifier block to control block_request variable
 *			block_reqeust set to true at PM_SUSPEND_PREPARE and
 *			release at PM_POST_RESUME
 *
 */
struct mdm_hsic_pm_data {
	struct list_head list;
	char name[32];

	struct usb_device *udev;
	int intf_cnt;

	/* control variables */
	struct notifier_block pm_notifier;
#ifdef CONFIG_CPU_FREQ_TETHERING
	struct notifier_block netdev_notifier;
	struct notifier_block usb_composite_notifier;
#endif

	bool block_request;
	bool state_busy;
	atomic_t pmlock_cnt;
	bool shutdown;

	/* gpio-s and irq */
	int gpio_host_ready;
	int gpio_device_ready;
	int gpio_host_wake;
	int irq;

	/* wakelock for L0 - L2 */
	struct wake_lock l2_wake;

	/* wakelock for boot */
	struct wake_lock boot_wake;

	/* wakelock for fast dormancy */
	struct wake_lock fd_wake;
	long fd_wake_time; /* wake time for raw packet in jiffies */

	/* workqueue, work for delayed autosuspend */
	struct workqueue_struct *wq;
	struct delayed_work auto_rpm_start_work;
	struct delayed_work auto_rpm_restart_work;
	struct delayed_work request_resume_work;
	struct delayed_work fast_dormancy_work;

	struct mdm_hsic_pm_platform_data *mdm_pdata;
};

/* indicate wakeup from lpa state */
bool lpa_handling;

/* indicate receive hallo_packet_rx */
int hello_packet_rx;

#ifdef EHCI_REG_DUMP
struct dump_ehci_regs {
	unsigned caps_hc_capbase;
	unsigned caps_hcs_params;
	unsigned caps_hcc_params;
	unsigned reserved0;
	struct ehci_regs regs;
	unsigned port_usb;  /*0x54*/
	unsigned port_hsic0;
	unsigned port_hsic1;
	unsigned reserved[12];
	unsigned insnreg00;	/*0x90*/
	unsigned insnreg01;
	unsigned insnreg02;
	unsigned insnreg03;
	unsigned insnreg04;
	unsigned insnreg05;
	unsigned insnreg06;
	unsigned insnreg07;
};

struct s5p_ehci_hcd_stub {
	struct device *dev;
	struct usb_hcd *hcd;
	struct clk *clk;
	int power_on;
};
/* for EHCI register dump */
struct dump_ehci_regs sec_debug_ehci_regs;

#define pr_hcd(s, r) printk(KERN_DEBUG "hcd reg(%s):\t 0x%08x\n", s, r)
static void print_ehci_regs(struct dump_ehci_regs *base)
{
	pr_hcd("HCCPBASE", base->caps_hc_capbase);
	pr_hcd("HCSPARAMS", base->caps_hcs_params);
	pr_hcd("HCCPARAMS", base->caps_hcc_params);
	pr_hcd("USBCMD", base->regs.command);
	pr_hcd("USBSTS", base->regs.status);
	pr_hcd("USBINTR", base->regs.intr_enable);
	pr_hcd("FRINDEX", base->regs.frame_index);
	pr_hcd("CTRLDSSEGMENT", base->regs.segment);
	pr_hcd("PERIODICLISTBASE", base->regs.frame_list);
	pr_hcd("ASYNCLISTADDR", base->regs.async_next);
	pr_hcd("CONFIGFLAG", base->regs.configured_flag);
	pr_hcd("PORT0 Status/Control", base->port_usb);
	pr_hcd("PORT1 Status/Control", base->port_hsic0);
	pr_hcd("PORT2 Status/Control", base->port_hsic1);
	pr_hcd("INSNREG00", base->insnreg00);
	pr_hcd("INSNREG01", base->insnreg01);
	pr_hcd("INSNREG02", base->insnreg02);
	pr_hcd("INSNREG03", base->insnreg03);
	pr_hcd("INSNREG04", base->insnreg04);
	pr_hcd("INSNREG05", base->insnreg05);
	pr_hcd("INSNREG06", base->insnreg06);
	pr_hcd("INSNREG07", base->insnreg07);
}

void debug_ehci_reg_dump(struct device *hdev)
{
	struct s5p_ehci_hcd_stub *s5p_ehci = dev_get_drvdata(hdev);
	struct usb_hcd *hcd = s5p_ehci->hcd;
	char *buf = (char *)&sec_debug_ehci_regs;
	pr_info("%s\n", __func__);
	pr_info("EHCI %s, %s\n", dev_driver_string(hdev), dev_name(hdev));

	print_ehci_regs(hcd->regs);

	memcpy(buf, hcd->regs, 0xB);
	memcpy(buf + 0x10, hcd->regs + 0x10, 0x1F);
	memcpy(buf + 0x50, hcd->regs + 0x50, 0xF);
	memcpy(buf + 0x90, hcd->regs + 0x90, 0x1F);
}
#else
#define debug_ehci_reg_dump (NULL)
#endif

/**
 * hsic pm device list for multiple modem support
 */
static LIST_HEAD(hsic_pm_dev_list);

static void print_pm_dev_info(struct mdm_hsic_pm_data *pm_data)
{
	pr_info("pm device\n\tname = %s\n"
		"\tudev = 0x%p\n"
		"\tintf_cnt = %d\n"
		"\tblock_request = %s\n",
		pm_data->name,
		pm_data->udev,
		pm_data->intf_cnt,
		pm_data->block_request ? "true" : "false");
}

static struct mdm_hsic_pm_data *get_pm_data_by_dev_name(const char *name)
{
	struct mdm_hsic_pm_data *pm_data;

	if (list_empty(&hsic_pm_dev_list)) {
		pr_err("%s:there's no dev on pm dev list\n", __func__);
		return NULL;
	};

	/* get device from list */
	list_for_each_entry(pm_data, &hsic_pm_dev_list, list) {
		if (!strncmp(pm_data->name, name, strlen(name)))
			return pm_data;
	}

	return NULL;
}

/* do not call in irq context */
int pm_dev_runtime_get_enabled(struct usb_device *udev)
{
	int spin = 50;

	while (spin--) {
		pr_debug("%s: rpm status: %d\n", __func__,
						udev->dev.power.runtime_status);
		if (udev->dev.power.runtime_status == RPM_ACTIVE ||
			udev->dev.power.runtime_status == RPM_SUSPENDED) {
			usb_mark_last_busy(udev);
			break;
		}
		msleep(20);
	}
	if (spin <= 0) {
		pr_err("%s: rpm status %d, return -EAGAIN\n", __func__,
						udev->dev.power.runtime_status);
		return -EAGAIN;
	}
	usb_mark_last_busy(udev);

	return 0;
}

/* do not call in irq context */
int pm_dev_wait_lpa_wake(void)
{
	int spin = 50;

	while (lpa_handling && spin--) {
		pr_debug("%s: lpa wake wait loop\n", __func__);
		msleep(20);
	}

	if (lpa_handling) {
		pr_err("%s: in lpa wakeup, return EAGAIN\n", __func__);
		return -EAGAIN;
	}

	return 0;
}

void notify_modem_fatal(void)
{
	struct mdm_hsic_pm_data *pm_data =
				get_pm_data_by_dev_name("mdm_hsic_pm0");

	pr_info("%s or shutdown\n", __func__);

	if (!pm_data || !pm_data->intf_cnt || !pm_data->udev)
		return;

	pm_data->shutdown = true;

	/* crash from sleep, ehci is in waking up, so do not control ehci */
	if (!pm_data->block_request) {
		struct device *dev, *hdev;
		hdev = pm_data->udev->bus->root_hub->dev.parent;
		dev = &pm_data->udev->dev;

		pm_runtime_get_noresume(dev);
		pm_runtime_dont_use_autosuspend(dev);
		/* if it's in going suspend, give settle time before wake up */
		msleep(100);
		wake_up_all(&dev->power.wait_queue);
		pm_runtime_resume(dev);
		pm_runtime_get_noresume(dev);

		blocking_notifier_call_chain(&mdm_reset_notifier_list, 0, 0);
	}
}

void request_autopm_lock(int status)
{
	struct mdm_hsic_pm_data *pm_data =
					get_pm_data_by_dev_name("mdm_hsic_pm0");
	int spin = 5;

	if (!pm_data || !pm_data->udev)
		return;

	pr_debug("%s: set runtime pm lock : %d\n", __func__, status);

	if (status) {
		if (!atomic_read(&pm_data->pmlock_cnt)) {
			atomic_inc(&pm_data->pmlock_cnt);
			pr_info("get lock\n");

			do {
				if (!pm_dev_runtime_get_enabled(pm_data->udev))
					break;
			} while (spin--);

			if (spin <= 0)
				mdm_force_fatal();

			pm_runtime_get(&pm_data->udev->dev);
			pm_runtime_forbid(&pm_data->udev->dev);
		} else
			atomic_inc(&pm_data->pmlock_cnt);
	} else {
		if (!atomic_read(&pm_data->pmlock_cnt))
			pr_info("unbalanced release\n");
		else if (atomic_dec_and_test(&pm_data->pmlock_cnt)) {
			pr_info("release lock\n");
			pm_runtime_allow(&pm_data->udev->dev);
			pm_runtime_put(&pm_data->udev->dev);
		}
		/* initailize hello_packet_rx */
		hello_packet_rx = 0;
	}
}

void request_active_lock_set(const char *name)
{
	struct mdm_hsic_pm_data *pm_data = get_pm_data_by_dev_name(name);
	pr_info("%s\n", __func__);
	if (pm_data)
		wake_lock(&pm_data->l2_wake);
}

void request_active_lock_release(const char *name)
{
	struct mdm_hsic_pm_data *pm_data = get_pm_data_by_dev_name(name);
	pr_info("%s\n", __func__);
	if (pm_data)
		wake_unlock(&pm_data->l2_wake);

}

void request_boot_lock_set(const char *name)
{
	struct mdm_hsic_pm_data *pm_data = get_pm_data_by_dev_name(name);
	pr_info("%s\n", __func__);
	if (pm_data)
		wake_lock(&pm_data->boot_wake);
}

void request_boot_lock_release(const char *name)
{
	struct mdm_hsic_pm_data *pm_data = get_pm_data_by_dev_name(name);
	pr_info("%s\n", __func__);
	if (pm_data)
		wake_unlock(&pm_data->boot_wake);
}

bool check_request_blocked(const char *name)
{
	struct mdm_hsic_pm_data *pm_data = get_pm_data_by_dev_name(name);
	if (!pm_data)
		return false;

	return pm_data->block_request;
}

void set_host_stat(const char *name, enum pwr_stat status)
{
	/* find pm device from list by name */
	struct mdm_hsic_pm_data *pm_data = get_pm_data_by_dev_name(name);

	if (!pm_data) {
		pr_err("%s:no pm device(%s) exist\n", __func__, name);
		return;
	}

	if (pm_data->gpio_host_ready) {
		pr_info("dev rdy val = %d\n",
				gpio_get_value(pm_data->gpio_device_ready));
		pr_info("%s:set host port power status to [%d]\n",
							__func__, status);

		/*10ms delay location moved*/
		if(status == POWER_OFF)
			mdelay(10);

		gpio_set_value(pm_data->gpio_host_ready, status);
	}
}

#define DEV_POWER_WAIT_SPIN	10
#define DEV_POWER_WAIT_MS	10
int wait_dev_pwr_stat(const char *name, enum pwr_stat status)
{
	int spin;
	/* find pm device from list by name */
	struct mdm_hsic_pm_data *pm_data = get_pm_data_by_dev_name(name);

	if (!pm_data) {
		pr_err("%s:no pm device(%s) exist\n", __func__, name);
		return -ENODEV;
	}

	pr_info("%s:[%s]...\n", __func__, status ? "PWR ON" : "PWR OFF");

	if (pm_data->gpio_device_ready) {
		for (spin = 0; spin < DEV_POWER_WAIT_SPIN ; spin++) {
			if (gpio_get_value(pm_data->gpio_device_ready) ==
								status)
				break;
			else
				mdelay(DEV_POWER_WAIT_MS);
		}
	}

	if (gpio_get_value(pm_data->gpio_device_ready) == status)
		pr_info(" done\n");
	else
		subsystem_restart(EXTERNAL_MODEM);
	return 0;
}

/**
 * check suspended state for L3 drive
 * if not, L3 blocked and stay at L2 / L0 state
 */
int check_udev_suspend_allowed(const char *name)
{
	struct mdm_hsic_pm_data *pm_data = get_pm_data_by_dev_name(name);
	struct device *dev;

	if (!pm_data) {
		pr_err("%s:no pm device(%s) exist\n", __func__, name);
		return -ENODEV;
	}
	if (!pm_data->intf_cnt || !pm_data->udev)
		return -ENODEV;

	dev = &pm_data->udev->dev;

	pr_info("%s:state_busy = %d, suspended = %d(rpmstat = %d:dpth:%d),"
		" suspended_child = %d\n", __func__, pm_data->state_busy,
		pm_runtime_suspended(dev), dev->power.runtime_status,
		dev->power.disable_depth, pm_children_suspended(dev));

	if (pm_data->state_busy)
		return -EBUSY;

	return pm_runtime_suspended(dev) && pm_children_suspended(dev);
}

int set_hsic_lpa_states(int states)
{
	/* if modem need to check survive, get status in variable */
	int val = 1;

	/* set state for LPA enter */
	if (val) {
		switch (states) {
		case STATE_HSIC_LPA_ENTER:
			/*
			 * need get some delay for MDM9x15 suspend
			 * if L3 drive goes out to modem in suspending
			 * modem goes to unstable PM state. now 10 ms is enough
			 */
			/*10ms delay location moved*/
			//mdelay(10);
			set_host_stat("mdm_hsic_pm0", POWER_OFF);
			wait_dev_pwr_stat("mdm_hsic_pm0", POWER_OFF);
			pr_info("set hsic lpa enter\n");
			break;
		case STATE_HSIC_LPA_WAKE:
			/* host control is done by ehci runtime resume code */
			#if 0
			set_host_stat("mdm_hsic_pm0", POWER_ON);
			wait_dev_pwr_stat("mdm_hsic_pm0", POWER_ON);
			#endif
			lpa_handling = true;
			pr_info("%s: set lpa handling to true\n", __func__);
			request_active_lock_set("mdm_hsic_pm0");
			pr_info("set hsic lpa wake\n");
			break;
		case STATE_HSIC_LPA_PHY_INIT:
			pr_info("set hsic lpa phy init\n");
			break;
		case STATE_HSIC_LPA_CHECK:
			if (lpcharge)
				return 0;
			else
				if (!get_pm_data_by_dev_name("mdm_hsic_pm0"))
					return 1;
				else
					return 0;
		default:
			pr_info("unknown lpa state\n");
			break;
		}
	}
	return 0;
}

#define PM_START_DELAY_MS 3000
int register_udev_to_pm_dev(const char *name, struct usb_device *udev)
{
	/* find pm device from list by name */
	struct mdm_hsic_pm_data *pm_data = get_pm_data_by_dev_name(name);

	if (!pm_data) {
		pr_err("%s:no pm device(%s) exist for udev(0x%p)\n",
					__func__, name, udev);
		return -ENODEV;
	}

	print_pm_dev_info(pm_data);

	if (!pm_data->intf_cnt) {
		pr_info("%s: registering new udev(0x%p) to %s\n", __func__,
							udev, pm_data->name);
		pm_data->udev = udev;
		atomic_set(&pm_data->pmlock_cnt, 0);
		usb_disable_autosuspend(udev);
#ifdef CONFIG_SIM_DETECT
		get_sim_state_at_boot();
#endif
	} else if (pm_data->udev && pm_data->udev != udev) {
		pr_err("%s:udev mismatching: pm_data->udev(0x%p), udev(0x%p)\n",
		__func__, pm_data->udev, udev);
		return -EINVAL;
	}

	pm_data->intf_cnt++;
	pr_info("%s:udev(0x%p) successfully registerd to %s, intf count = %d\n",
			__func__, udev, pm_data->name, pm_data->intf_cnt);

	queue_delayed_work(pm_data->wq, &pm_data->auto_rpm_start_work,
					msecs_to_jiffies(PM_START_DELAY_MS));
	return 0;
}

/* force fatal for debug when HSIC disconnect */
extern void mdm_force_fatal(void);

void unregister_udev_from_pm_dev(const char *name, struct usb_device *udev)
{
	/* find pm device from list by name */
	struct mdm_hsic_pm_data *pm_data = get_pm_data_by_dev_name(name);
	struct device *hdev;

	pr_info("%s\n", __func__);

	if (!pm_data) {
		pr_err("%s:no pm device(%s) exist for udev(0x%p)\n",
					__func__, name, udev);
		return;
	}

	if (!pm_data->shutdown) {
		hdev = udev->bus->root_hub->dev.parent;
		pm_runtime_forbid(hdev); /*ehci*/
		debug_ehci_reg_dump(hdev);
	}

	if (pm_data->udev && pm_data->udev != udev) {
		pr_err("%s:udev mismatching: pm_data->udev(0x%p), udev(0x%p)\n",
		__func__, pm_data->udev, udev);
		return;
	}

	pm_data->intf_cnt--;
	pr_info("%s:udev(0x%p) unregistered from %s, intf count = %d\n",
			__func__, udev, pm_data->name, pm_data->intf_cnt);

	if (!pm_data->intf_cnt) {
		pr_info("%s: all intf device unregistered from %s\n",
						__func__, pm_data->name);
		pm_data->udev = NULL;
		/* force fatal for debug when HSIC disconnect */
		if (!pm_data->shutdown) {
			mdm_force_fatal();
		}
	}
}

static void mdm_hsic_rpm_check(struct work_struct *work)
{
	struct mdm_hsic_pm_data *pm_data =
			container_of(work, struct mdm_hsic_pm_data,
					request_resume_work.work);
	struct device *dev;

	if (pm_data->shutdown)
		return;

	pr_info("%s\n", __func__);

	if (!pm_data->udev)
		return;

	if (lpa_handling) {
		pr_info("ignore resume req, lpa handling\n");
		return;
	}

	dev = &pm_data->udev->dev;

	if (pm_runtime_resume(dev) < 0)
		queue_delayed_work(pm_data->wq, &pm_data->request_resume_work,
							msecs_to_jiffies(20));

	if (pm_runtime_suspended(dev))
		queue_delayed_work(pm_data->wq, &pm_data->request_resume_work,
							msecs_to_jiffies(20));
};

static void mdm_hsic_rpm_start(struct work_struct *work)
{
	struct mdm_hsic_pm_data *pm_data =
			container_of(work, struct mdm_hsic_pm_data,
					auto_rpm_start_work.work);
	struct usb_device *udev = pm_data->udev;
	struct device *dev, *pdev, *hdev;

	pr_info("%s\n", __func__);

	if (!pm_data->intf_cnt || !pm_data->udev)
		return;

	dev = &pm_data->udev->dev;
	pdev = dev->parent;
	pm_runtime_set_autosuspend_delay(dev, 500);
	hdev = udev->bus->root_hub->dev.parent;
	pr_info("EHCI runtime %s, %s\n", dev_driver_string(hdev),
			dev_name(hdev));

	pm_runtime_allow(dev);
	pm_runtime_allow(hdev);/*ehci*/

	pm_data->shutdown = false;
}

static void mdm_hsic_rpm_restart(struct work_struct *work)
{
	struct mdm_hsic_pm_data *pm_data =
			container_of(work, struct mdm_hsic_pm_data,
					auto_rpm_restart_work.work);
	struct device *dev;

	pr_info("%s\n", __func__);

	if (!pm_data->intf_cnt || !pm_data->udev)
		return;

	dev = &pm_data->udev->dev;
	pm_runtime_set_autosuspend_delay(dev, 500);
}

static void fast_dormancy_func(struct work_struct *work)
{
	struct mdm_hsic_pm_data *pm_data =
			container_of(work, struct mdm_hsic_pm_data,
					fast_dormancy_work.work);
	pr_debug("%s\n", __func__);

	if (!pm_data || !pm_data->fd_wake_time)
		return;

	wake_lock_timeout(&pm_data->fd_wake, pm_data->fd_wake_time);
};

void fast_dormancy_wakelock(const char *name)
{
	struct mdm_hsic_pm_data *pm_data = get_pm_data_by_dev_name(name);

	if (!pm_data || !pm_data->fd_wake_time)
		return;

	queue_delayed_work(pm_data->wq, &pm_data->fast_dormancy_work, 0);
}

static ssize_t show_waketime(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mdm_hsic_pm_data *pm_data = platform_get_drvdata(pdev);
	char *p = buf;
	unsigned int msec;

	if (!pm_data)
		return 0;

	msec = jiffies_to_msecs(pm_data->fd_wake_time);
	p += sprintf(p, "%u\n", msec);

	return p - buf;
}

static ssize_t store_waketime(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mdm_hsic_pm_data *pm_data = platform_get_drvdata(pdev);
	unsigned long msec;
	int r;

	if (!pm_data)
		return count;

	r = strict_strtoul(buf, 10, &msec);
	if (r)
		return count;

	pm_data->fd_wake_time = msecs_to_jiffies(msec);

	return count;
}
static DEVICE_ATTR(waketime, 0660, show_waketime, store_waketime);

static ssize_t store_runtime(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mdm_hsic_pm_data *pm_data = platform_get_drvdata(pdev);
	int value;

	if (sscanf(buf, "%d", &value) != 1)
		return -EINVAL;

	if (!pm_data || !pm_data->intf_cnt || !pm_data->udev)
		return -ENXIO;

	if (value == 1) {
		pr_info("%s: request runtime resume\n", __func__);
		if (pm_request_resume(&pm_data->udev->dev) < 0)
			pr_err("%s: unable to add pm work for rpm\n", __func__);
	}

	return count;
}
static DEVICE_ATTR(runtime, 0664, NULL, store_runtime);

static struct attribute *mdm_hsic_attrs[] = {
	&dev_attr_waketime.attr,
	&dev_attr_runtime.attr,
	NULL
};

static struct attribute_group mdm_hsic_attrgroup = {
	.attrs = mdm_hsic_attrs,
};

static int mdm_reset_notify_main(struct notifier_block *this,
				unsigned long event, void *ptr) {
	pr_info("%s\n", __func__);

	return NOTIFY_DONE;
};

static struct notifier_block mdm_reset_main_block = {
	.notifier_call = mdm_reset_notify_main,
};

static int mdm_hsic_pm_notify_event(struct notifier_block *this,
				unsigned long event, void *ptr)
{
	struct mdm_hsic_pm_data *pm_data =
		container_of(this, struct mdm_hsic_pm_data, pm_notifier);

	switch (event) {
	case PM_SUSPEND_PREPARE:
		/* to catch blocked resume req */
		pm_data->state_busy = false;
		pm_data->block_request = true;
		pr_info("%s: block request\n", __func__);
		return NOTIFY_OK;
	case PM_POST_SUSPEND:
		pm_data->block_request = false;
		pr_info("%s: unblock request\n", __func__);

		if (pm_data->shutdown) {
			notify_modem_fatal();
			return NOTIFY_DONE;
		}
		/**
		 * cover L2 -> L3 broken and resume req blocked case :
		 * force resume request for the lost request
		 */
		/* pm_request_resume(&pm_data->udev->dev); */
		queue_delayed_work(pm_data->wq, &pm_data->request_resume_work,
							msecs_to_jiffies(20));
		/*pm_runtime_set_autosuspend_delay(&pm_data->udev->dev, 200);*/
		queue_delayed_work(pm_data->wq, &pm_data->auto_rpm_restart_work,
						msecs_to_jiffies(20));

		request_active_lock_set(pm_data->name);

		return NOTIFY_OK;
	}
	return NOTIFY_DONE;
}

#define HSIC_RESUME_TRIGGER_LEVEL	1
static irqreturn_t mdm_hsic_irq_handler(int irq, void *data)
{
	int irq_level;
	struct mdm_hsic_pm_data *pm_data = data;

	if (!pm_data || !pm_data->intf_cnt || !pm_data->udev)
		return IRQ_HANDLED;

	if (pm_data->shutdown)
		return IRQ_HANDLED;

	/**
	 * host wake up handler, takes both edge
	 * in rising, isr triggers L2 ->  L0 resume
	 */

	irq_level = gpio_get_value(pm_data->gpio_host_wake);
	pr_info("%s: detect %s edge\n", __func__,
					irq_level ? "Rising" : "Falling");

	if (irq_level != HSIC_RESUME_TRIGGER_LEVEL)
		return IRQ_HANDLED;

	if (pm_data->block_request) {
		pr_info("%s: request blocked by kernel suspending\n", __func__);
		pm_data->state_busy = true;
		/* for blocked request, set wakelock to return at dpm suspend */
		wake_lock(&pm_data->l2_wake);
		return IRQ_HANDLED;
	}
#if 0
	if (pm_request_resume(&pm_data->udev->dev) < 0)
		pr_err("%s: unable to add pm work for rpm\n", __func__);
	/* check runtime pm runs in Active state, after 100ms */
	queue_delayed_work(pm_data->wq, &pm_data->request_resume_work,
							msecs_to_jiffies(200));
#else
	queue_delayed_work(pm_data->wq, &pm_data->request_resume_work, 0);
#endif
	return IRQ_HANDLED;
}

static int mdm_hsic_pm_gpio_init(struct mdm_hsic_pm_data *pm_data,
						struct platform_device *pdev)
{
	int ret;
	struct resource *res;

	/* get gpio from platform data */

	/* host ready gpio */
	res = platform_get_resource_byname(pdev, IORESOURCE_IO,
					"AP2MDM_HSIC_ACTIVE");
	if (res)
		pm_data->gpio_host_ready = res->start;

	if (pm_data->gpio_host_ready) {
		ret = gpio_request(pm_data->gpio_host_ready, "host_rdy");
		if (ret < 0)
			return ret;
		gpio_direction_output(pm_data->gpio_host_ready, 1);
		s3c_gpio_cfgpin(pm_data->gpio_host_ready, S3C_GPIO_OUTPUT);
		s3c_gpio_setpull(pm_data->gpio_host_ready, S3C_GPIO_PULL_NONE);
		s5p_gpio_set_drvstr(pm_data->gpio_host_ready,
							S5P_GPIO_DRVSTR_LV4);
		gpio_set_value(pm_data->gpio_host_ready, 1);
	} else
		return -ENXIO;

	/* device ready gpio */
	res = platform_get_resource_byname(pdev, IORESOURCE_IO,
					"MDM2AP_DEVICE_PWR_ACTIVE");
	if (res)
		pm_data->gpio_device_ready = res->start;
	if (pm_data->gpio_device_ready) {
		ret = gpio_request(pm_data->gpio_device_ready, "device_rdy");
		if (ret < 0)
			return ret;
		gpio_direction_input(pm_data->gpio_device_ready);
		s3c_gpio_cfgpin(pm_data->gpio_device_ready, S3C_GPIO_INPUT);
	} else
		return -ENXIO;

	/* host wake gpio */
	res = platform_get_resource_byname(pdev, IORESOURCE_IO,
					"MDM2AP_RESUME_REQ");
	if (res)
		pm_data->gpio_host_wake = res->start;
	if (pm_data->gpio_host_wake) {
		ret = gpio_request(pm_data->gpio_host_wake, "host_wake");
		if (ret < 0)
			return ret;
		gpio_direction_input(pm_data->gpio_host_wake);
		s3c_gpio_cfgpin(pm_data->gpio_host_wake, S3C_GPIO_SFN(0xF));
	} else
		return -ENXIO;

	if (pm_data->gpio_host_wake)
		pm_data->irq = gpio_to_irq(pm_data->gpio_host_wake);

	if (!pm_data->irq) {
		pr_err("fail to get host wake irq\n");
		return -ENXIO;
	}

	return 0;
}

static void mdm_hsic_pm_gpio_free(struct mdm_hsic_pm_data *pm_data)
{
	if (pm_data->gpio_host_ready)
		gpio_free(pm_data->gpio_host_ready);

	if (pm_data->gpio_device_ready)
		gpio_free(pm_data->gpio_device_ready);

	if (pm_data->gpio_host_wake)
		gpio_free(pm_data->gpio_host_wake);
}

#ifdef CONFIG_CPU_FREQ_TETHERING
static int link_pm_netdev_event(struct notifier_block *this,
				unsigned long event, void *ptr)
{
	struct mdm_hsic_pm_data *pm_data =
		container_of(this, struct mdm_hsic_pm_data, netdev_notifier);
	struct mdm_hsic_pm_platform_data *mdm_pdata = pm_data->mdm_pdata;
	struct net_device *dev = ptr;

	if (!net_eq(dev_net(dev), &init_net))
		return NOTIFY_DONE;

	if (!strncmp(dev->name, "rndis", 5)) {
		switch (event) {
		case NETDEV_UP:
			pr_info("%s: %s UP\n", __func__, dev->name);
			if (mdm_pdata->freq_lock)
				mdm_pdata->freq_lock(mdm_pdata->dev);

			break;
		case NETDEV_DOWN:
			pr_info("%s: %s DOWN\n", __func__, dev->name);
			if (mdm_pdata->freq_unlock)
				mdm_pdata->freq_unlock(mdm_pdata->dev);
			break;
		}
	}
	return NOTIFY_DONE;
}

static int usb_composite_notifier_event(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	struct mdm_hsic_pm_data *pm_data =
		container_of(this, struct mdm_hsic_pm_data,
				usb_composite_notifier);
	struct mdm_hsic_pm_platform_data *mdm_pdata = pm_data->mdm_pdata;

	switch (event) {
	case 0:
		if (mdm_pdata->freq_unlock)
			mdm_pdata->freq_unlock(mdm_pdata->dev);
		pr_info("%s: USB detached\n", __func__);
		break;
	case 1:
		if (mdm_pdata->freq_lock)
			mdm_pdata->freq_lock(mdm_pdata->dev);
		pr_info("%s: USB attached\n", __func__);
		break;
	}
	pr_info("%s: usb configuration: %s\n", __func__, (char *)ptr);

	return NOTIFY_DONE;
}
#endif

static int mdm_hsic_pm_probe(struct platform_device *pdev)
{
	int ret;
	struct mdm_hsic_pm_data *pm_data;

	pr_info("%s for %s\n", __func__, pdev->name);

	pm_data = kzalloc(sizeof(struct mdm_hsic_pm_data), GFP_KERNEL);
	if (!pm_data) {
		pr_err("%s: fail to alloc pm_data\n", __func__);
		return -ENOMEM;
	}

	/* initial value */
	memcpy(pm_data->name, pdev->name, strlen(pdev->name));

	ret = mdm_hsic_pm_gpio_init(pm_data, pdev);
	if (ret < 0)
		goto err_gpio_init_fail;

	/* request irq for host wake interrupt */
	ret = request_irq(pm_data->irq, mdm_hsic_irq_handler,
		IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_DISABLED,
		pm_data->name, (void *)pm_data);
	if (ret < 0) {
		pr_err("%s: fail to request irq(%d)\n", __func__, ret);
		goto err_request_irq;
	}

	ret = enable_irq_wake(pm_data->irq);
	if (ret < 0) {
		pr_err("%s: fail to set wake irq(%d)\n", __func__, ret);
		goto err_set_wake_irq;
	}

	pm_data->wq = create_singlethread_workqueue("hsicrpmd");
	if (!pm_data->wq) {
		pr_err("%s: fail to create wq\n", __func__);
		goto err_create_wq;
	}

	if (sysfs_create_group(&pdev->dev.kobj, &mdm_hsic_attrgroup) < 0) {
		pr_err("%s: fail to create sysfs\n", __func__);
		goto err_create_sys_file;
	}

	pm_data->mdm_pdata =
		(struct mdm_hsic_pm_platform_data *)pdev->dev.platform_data;
	INIT_DELAYED_WORK(&pm_data->auto_rpm_start_work, mdm_hsic_rpm_start);
	INIT_DELAYED_WORK(&pm_data->auto_rpm_restart_work,
							mdm_hsic_rpm_restart);
	INIT_DELAYED_WORK(&pm_data->request_resume_work, mdm_hsic_rpm_check);
	INIT_DELAYED_WORK(&pm_data->fast_dormancy_work, fast_dormancy_func);
	/* register notifier call */
	pm_data->pm_notifier.notifier_call = mdm_hsic_pm_notify_event;
	register_pm_notifier(&pm_data->pm_notifier);
	blocking_notifier_chain_register(&mdm_reset_notifier_list,
							&mdm_reset_main_block);

#ifdef CONFIG_CPU_FREQ_TETHERING
	pm_data->netdev_notifier.notifier_call = link_pm_netdev_event;
	register_netdevice_notifier(&pm_data->netdev_notifier);

	pm_data->usb_composite_notifier.notifier_call =
		usb_composite_notifier_event;
	register_usb_composite_notifier(&pm_data->usb_composite_notifier);
#endif

	wake_lock_init(&pm_data->l2_wake, WAKE_LOCK_SUSPEND, pm_data->name);
	wake_lock_init(&pm_data->boot_wake, WAKE_LOCK_SUSPEND, "mdm_boot");
	wake_lock_init(&pm_data->fd_wake, WAKE_LOCK_SUSPEND, "fast_dormancy");
	pm_data->fd_wake_time = DEFAULT_RAW_WAKE_TIME;

	print_pm_dev_info(pm_data);
	list_add(&pm_data->list, &hsic_pm_dev_list);
	platform_set_drvdata(pdev, pm_data);
	pr_info("%s for %s has done\n", __func__, pdev->name);

	return 0;

err_create_sys_file:
	destroy_workqueue(pm_data->wq);
err_create_wq:
	disable_irq_wake(pm_data->irq);
err_set_wake_irq:
	free_irq(pm_data->irq, (void *)pm_data);
err_request_irq:
err_gpio_init_fail:
	mdm_hsic_pm_gpio_free(pm_data);
	kfree(pm_data);
	return -ENXIO;
}

static struct platform_driver mdm_pm_driver = {
	.probe = mdm_hsic_pm_probe,
	.driver = {
		.name = "mdm_hsic_pm0",
		.owner = THIS_MODULE,
	},
};

static int __init mdm_hsic_pm_init(void)
{
	/* in lpm mode, do not load modem driver */
	if (lpcharge)
		return 0;
	return platform_driver_register(&mdm_pm_driver);
}

static void __exit mdm_hsic_pm_exit(void)
{
	platform_driver_unregister(&mdm_pm_driver);
}

late_initcall(mdm_hsic_pm_init);
