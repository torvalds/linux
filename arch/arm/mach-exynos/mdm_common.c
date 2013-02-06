/* Copyright (c) 2011-2012, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/ioctl.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/debugfs.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/clk.h>
#ifndef CONFIG_ARCH_EXYNOS
#include <linux/mfd/pmic8058.h>
#endif
#include <asm/mach-types.h>
#include <asm/uaccess.h>
#include <mach/mdm2.h>
#include <mach/restart.h>
#include <mach/subsystem_restart.h>
#include <linux/msm_charm.h>
#ifndef CONFIG_ARCH_EXYNOS
#include "msm_watchdog.h"
#endif
#include "mdm_private.h"

#ifdef CONFIG_MDM_HSIC_PM
#include <linux/mdm_hsic_pm.h>
static const char rmnet_pm_dev[] = "mdm_hsic_pm0";
#endif

#ifdef CONFIG_ARCH_EXYNOS
#include <linux/interrupt.h>
#include <plat/gpio-cfg.h>
#include <mach/gpio.h>
#endif

#ifdef CONFIG_SIM_DETECT
#include <linux/poll.h>
#endif

#define MDM_MODEM_TIMEOUT	6000
#define MDM_MODEM_DELTA	100
#define MDM_BOOT_TIMEOUT	60000L
#define MDM_RDUMP_TIMEOUT	120000L
#define MDM2AP_STATUS_TIMEOUT_MS 300000L
/* declare as module param controled by cmdline parameter
 * this value makes force ramdump even mdm gives silent reset
 * usage : add cmdline ' mdm_common.force_dump=1 '
 */
static int force_dump;
module_param(force_dump, int, S_IRUGO | S_IWUSR);

static int mdm_debug_on;
static struct workqueue_struct *mdm_queue;
static struct workqueue_struct *mdm_sfr_queue;
static unsigned int dump_timeout_ms;

#define EXTERNAL_MODEM "external_modem"

static struct mdm_modem_drv *mdm_drv;

static unsigned char *mdm_read_err_report(void);
static void mdm_disable_irqs(void);

DECLARE_COMPLETION(mdm_needs_reload);
DECLARE_COMPLETION(mdm_boot);
DECLARE_COMPLETION(mdm_ram_dumps);

static int first_boot = 1;

#define RD_BUF_SIZE			100
#define SFR_MAX_RETRIES		10
#define SFR_RETRY_INTERVAL	1000

#ifndef CONFIG_ARCH_EXYNOS
static irqreturn_t mdm_vddmin_change(int irq, void *dev_id)
{
	int value = gpio_get_value(
		mdm_drv->pdata->vddmin_resource->mdm2ap_vddmin_gpio);

	if (value == 0)
		pr_info("External Modem entered Vddmin\n");
	else
		pr_info("External Modem exited Vddmin\n");

	return IRQ_HANDLED;
}
#endif

static void mdm_setup_vddmin_gpios(void)
{
#ifdef CONFIG_ARCH_EXYNOS
	return;
#else
	struct msm_rpm_iv_pair req;
	struct mdm_vddmin_resource *vddmin_res;
	int irq, ret;

	/* This resource may not be supported by some platforms. */
	vddmin_res = mdm_drv->pdata->vddmin_resource;
	if (!vddmin_res)
		return;

	req.id = vddmin_res->rpm_id;
	req.value = ((uint32_t)vddmin_res->ap2mdm_vddmin_gpio & 0x0000FFFF)
							<< 16;
	req.value |= ((uint32_t)vddmin_res->modes & 0x000000FF) << 8;
	req.value |= (uint32_t)vddmin_res->drive_strength & 0x000000FF;

	msm_rpm_set(MSM_RPM_CTX_SET_0, &req, 1);

	/* Monitor low power gpio from mdm */
	irq = MSM_GPIO_TO_INT(vddmin_res->mdm2ap_vddmin_gpio);
	if (irq < 0) {
		pr_err("%s: could not get LPM POWER IRQ resource.\n",
			__func__);
		goto error_end;
	}

	ret = request_threaded_irq(irq, NULL, mdm_vddmin_change,
		IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
		"mdm lpm", NULL);

	if (ret < 0)
		pr_err("%s: MDM LPM IRQ#%d request failed with error=%d",
			__func__, irq, ret);
error_end:
	return;
#endif
}

static void mdm_restart_reason_fn(struct work_struct *work)
{
#ifndef CONFIG_ARCH_EXYNOS
	int ret, ntries = 0;
	char sfr_buf[RD_BUF_SIZE];
	do {
		msleep(SFR_RETRY_INTERVAL);
		ret = sysmon_get_reason(SYSMON_SS_EXT_MODEM,
					sfr_buf, sizeof(sfr_buf));
		if (ret) {
			/*
			 * The sysmon device may not have been probed as yet
			 * after the restart.
			 */
			pr_err("%s: Error retrieving mdm restart reason, ret = %d, "
					"%d/%d tries\n", __func__, ret,
					ntries + 1,	SFR_MAX_RETRIES);
		} else {
			pr_err("mdm restart reason: %s\n", sfr_buf);
			break;
		}
	} while (++ntries < SFR_MAX_RETRIES);
#endif
}

static DECLARE_WORK(sfr_reason_work, mdm_restart_reason_fn);

void mdm_set_chip_configuration(bool dload)
{
	if (mdm_drv) {
		pr_info("mdm9x15 boot protocol = %s\n",
						dload ? "DLOAD" : "SAHARA");
		mdm_drv->proto_is_dload = dload;
	}
}

static void mdm2ap_status_check(struct work_struct *work)
{
	/*
	 * If the mdm modem did not pull the MDM2AP_STATUS gpio
	 * high then call subsystem_restart.
	 */
	if (gpio_get_value(mdm_drv->mdm2ap_status_gpio) == 0) {
		pr_err("%s: MDM2AP_STATUS gpio did not go high\n",
			   __func__);
		mdm_drv->mdm_ready = 0;
		notify_modem_fatal();
		subsystem_restart(EXTERNAL_MODEM);
	}
}

static DECLARE_DELAYED_WORK(mdm2ap_status_check_work, mdm2ap_status_check);

long mdm_modem_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg)
{
	int status, ret = 0;

	if (_IOC_TYPE(cmd) != CHARM_CODE) {
		pr_err("%s: invalid ioctl code\n", __func__);
		return -EINVAL;
	}

	pr_debug("%s: Entering ioctl cmd = %d\n", __func__, _IOC_NR(cmd));
	switch (cmd) {
	case WAKE_CHARM:
		pr_info("%s: Powering on mdm\n", __func__);
#ifdef CONFIG_MDM_HSIC_PM
		request_boot_lock_set(rmnet_pm_dev);
#endif
		mdm_drv->ops->power_on_mdm_cb(mdm_drv);
		break;
	case CHECK_FOR_BOOT:
		if (gpio_get_value(mdm_drv->mdm2ap_status_gpio) == 0)
			put_user(1, (unsigned long __user *) arg);
		else
			put_user(0, (unsigned long __user *) arg);
		break;
	case NORMAL_BOOT_DONE:
		pr_info("%s: check if mdm is booted up\n", __func__);
		get_user(status, (unsigned long __user *) arg);
		if (status) {
			pr_debug("%s: normal boot failed\n", __func__);
			mdm_drv->mdm_boot_status = -EIO;
		} else {
			pr_info("%s: normal boot done\n", __func__);
			mdm_drv->mdm_boot_status = 0;
		}
		mdm_drv->mdm_ready = 1;

		if (mdm_drv->ops->normal_boot_done_cb != NULL)
			mdm_drv->ops->normal_boot_done_cb(mdm_drv);

		if (!first_boot)
			complete(&mdm_boot);
		else
			first_boot = 0;

		/* If bootup succeeded, start a timer to check that the
		 * mdm2ap_status gpio goes high.
		 */
		if (!status && gpio_get_value(mdm_drv->mdm2ap_status_gpio) == 0)
			schedule_delayed_work(&mdm2ap_status_check_work,
				msecs_to_jiffies(MDM2AP_STATUS_TIMEOUT_MS));
		break;
	case RAM_DUMP_DONE:
		pr_info("%s: mdm done collecting RAM dumps\n", __func__);
		get_user(status, (unsigned long __user *) arg);
		if (status)
			mdm_drv->mdm_ram_dump_status = -EIO;
		else {
			pr_info("%s: ramdump collection completed\n", __func__);
			mdm_drv->mdm_ram_dump_status = 0;
			panic("CP Crash %s", mdm_read_err_report());
		}
		complete(&mdm_ram_dumps);
		break;

	case WAIT_FOR_ERROR:
		pr_debug("%s: wait for mdm error\n", __func__);
		#if 0
		ret = wait_for_completion_interruptible(&mdm_error);
		INIT_COMPLETION(mdm_error);
		#endif
		break;

	case WAIT_FOR_RESTART:
		pr_info("%s: wait for mdm to need images reloaded\n",
				__func__);
		ret = wait_for_completion_interruptible(&mdm_needs_reload);
		if (!ret)
			put_user(mdm_drv->boot_type,
					 (unsigned long __user *) arg);
		INIT_COMPLETION(mdm_needs_reload);
		break;

	case SILENT_RESET_CONTROL:
		pr_info("%s: mdm doing silent reset\n", __func__);
		mdm_drv->mdm_ram_dump_status = 0;
		complete(&mdm_ram_dumps);
		break;

	case AUTOPM_LOCK:
		get_user(status, (unsigned long __user *) arg);
		pr_info("%s: mdm autopm request[%s]\n", __func__,
						status ? "lock" : "release");
		request_autopm_lock(status);
		break;

	case GET_BOOT_PROTOCOL:
		pr_info("%s: mdm get boot protocol %d\n", __func__,
						mdm_drv->proto_is_dload);
		return mdm_drv->proto_is_dload;

	case GET_FORCE_RAMDUMP:
		pr_info("%s: mdm get dump mode = %d\n", __func__, force_dump);
		mdm_force_fatal();
		break;

#ifdef CONFIG_SIM_DETECT
	case GET_SIM_DETECT:
		pr_info("%s: mdm get sim detect = %d\n", __func__,
						mdm_drv->sim_state);
		return mdm_drv->sim_state;
#endif
	default:
		pr_err("%s: invalid ioctl cmd = %d\n", __func__, _IOC_NR(cmd));
		ret = -EINVAL;
		break;
	}

	return ret;
}

static void mdm_fatal_fn(struct work_struct *work)
{
	pr_info("%s: Reseting the mdm due to an errfatal\n", __func__);
	notify_modem_fatal();
	subsystem_restart(EXTERNAL_MODEM);
}

static DECLARE_WORK(mdm_fatal_work, mdm_fatal_fn);

static void mdm_status_fn(struct work_struct *work)
{
	int value = gpio_get_value(mdm_drv->mdm2ap_status_gpio);

	pr_debug("%s: status:%d\n", __func__, value);
	if (mdm_drv->mdm_ready && mdm_drv->ops->status_cb)
		mdm_drv->ops->status_cb(mdm_drv, value);
#ifdef CONFIG_MDM_HSIC_PM
	if (value) {
		request_boot_lock_release(rmnet_pm_dev);
		request_active_lock_set(rmnet_pm_dev);
	}
#endif
}

static DECLARE_WORK(mdm_status_work, mdm_status_fn);

/* temporary implemented, it should be removed at mass production */
/* simply declare this function as extern at test point, and call it */
void mdm_force_fatal(void)
{
	pr_info("%s: Reseting the mdm due to AP request\n", __func__);

	force_dump = 1;

	if (in_irq())
		queue_work(mdm_queue, &mdm_fatal_work);
	else {
		notify_modem_fatal();
		subsystem_restart(EXTERNAL_MODEM);
	}
}
EXPORT_SYMBOL(mdm_force_fatal);

static void mdm_disable_irqs(void)
{
	disable_irq_nosync(mdm_drv->mdm_errfatal_irq);
	disable_irq_nosync(mdm_drv->mdm_status_irq);
}

static irqreturn_t mdm_errfatal(int irq, void *dev_id)
{
	pr_debug("%s: mdm got errfatal interrupt\n", __func__);
	if (mdm_drv->mdm_ready &&
		(gpio_get_value(mdm_drv->mdm2ap_status_gpio) == 1)) {
		pr_info("%s: Reseting the mdm due to an errfatal\n", __func__);
		mdm_drv->mdm_ready = 0;
		/* subsystem_restart(EXTERNAL_MODEM); */
		queue_work(mdm_queue, &mdm_fatal_work);
	}
	return IRQ_HANDLED;
}

#ifdef CONFIG_SIM_DETECT
/*
 * SIM state gpio shows level low when SIM inserted
 *
 * HIGH : detach
 * LOW : attach
 */
void get_sim_state_at_boot(void)
{
	if (mdm_drv) {
		mdm_drv->sim_state = !gpio_get_value(mdm_drv->sim_detect_gpio);
		mdm_drv->sim_changed = 0;
		pr_info("%s: sim state = %s\n", __func__,
				mdm_drv->sim_state == 1 ? "Attach" : "Detach");
	}
}

static void sim_status_check(struct work_struct *work)
{
	int cur_sim_state;

	if (!mdm_drv->mdm_ready)
		return;

	cur_sim_state = !gpio_get_value(mdm_drv->sim_detect_gpio);
	if (cur_sim_state != mdm_drv->sim_state) {
		mdm_drv->sim_state = cur_sim_state;
		mdm_drv->sim_changed = 1;
		pr_info("sim state = %s\n",
			mdm_drv->sim_state == 1 ? "Attach" : "Detach");
		wake_up_interruptible(&mdm_drv->wq);
	} else
		mdm_drv->sim_changed = 0;
}

static DECLARE_DELAYED_WORK(sim_status_check_work, sim_status_check);

#define SIM_DEBOUNCE_TIME_MS	1000
static irqreturn_t sim_detect_irq_handler(int irq, void *dev_id)
{
	if (mdm_drv->mdm_ready) {
		pr_info("%s: sim gpio level = %d\n", __func__,
				gpio_get_value(mdm_drv->sim_detect_gpio));

		schedule_delayed_work(&sim_status_check_work,
					msecs_to_jiffies(SIM_DEBOUNCE_TIME_MS));
	}

	return IRQ_HANDLED;
}
#endif

static unsigned char *mdm_read_err_report(void)
{
	/* Read CP error report from mdm_err.log in tombstones */
	static unsigned char buf[1000] = { 0, };
	struct file *filp;
	mm_segment_t old_fs = get_fs();
	set_fs(KERNEL_DS);
	do {
		filp = filp_open("/tombstones/mdm/mdm_err.log", \
			O_RDWR, S_IRUSR|S_IWUSR);
		if (IS_ERR(filp)) {
			set_fs(old_fs);
			return (unsigned char *) buf;
		}
		vfs_read(filp, buf, 1000, &filp->f_pos);
		filp_close(filp, NULL);
		set_fs(old_fs);
	} while (0);
	return (unsigned char *) buf;
}

#ifdef CONFIG_SIM_DETECT
static unsigned int mdm_modem_poll(struct file *file, poll_table *wait)
{
	int mask = 0;
	poll_wait(file, &mdm_drv->wq, wait);
	if (mdm_drv->sim_changed == 1) {
		mdm_drv->sim_changed = 0;
		mask = POLLHUP;
	}

	return mask;
}
#endif

static int mdm_modem_open(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations mdm_modem_fops = {
	.owner		= THIS_MODULE,
	.open		= mdm_modem_open,
	.unlocked_ioctl	= mdm_modem_ioctl,
#ifdef CONFIG_SIM_DETECT
	.poll = mdm_modem_poll,
#endif
};


static struct miscdevice mdm_modem_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "mdm",
	.fops	= &mdm_modem_fops
};

static int mdm_panic_prep(struct notifier_block *this,
				unsigned long event, void *ptr)
{
	int i;

	pr_debug("%s: setting AP2MDM_ERRFATAL high for a non graceful reset\n",
			 __func__);
	mdm_disable_irqs();
	gpio_set_value(mdm_drv->ap2mdm_errfatal_gpio, 1);

	for (i = MDM_MODEM_TIMEOUT; i > 0; i -= MDM_MODEM_DELTA) {
		/* pet_watchdog(); */
		mdelay(MDM_MODEM_DELTA);
		if (gpio_get_value(mdm_drv->mdm2ap_status_gpio) == 0)
			break;
	}
	if (i <= 0) {
		pr_err("%s: MDM2AP_STATUS never went low\n", __func__);
		/* Reset the modem so that it will go into download mode. */
		if (mdm_drv && mdm_drv->ops->reset_mdm_cb)
			mdm_drv->ops->reset_mdm_cb(mdm_drv);
	}
	return NOTIFY_DONE;
}

static struct notifier_block mdm_panic_blk = {
	.notifier_call  = mdm_panic_prep,
	.priority = 1,
};

static int mdm_reboot_notifier(struct notifier_block *this,
				unsigned long event, void *ptr)
{
	int soft_reset_direction =
		mdm_drv->pdata->soft_reset_inverted ? 1 : 0;
	mdm_drv->mdm_ready = 0;
	mdm_disable_irqs();
	notify_modem_fatal();
	gpio_direction_output(mdm_drv->ap2mdm_soft_reset_gpio,
							soft_reset_direction);
	if (mdm_drv->ap2mdm_pmic_pwr_en_gpio > 0)
		gpio_direction_output(mdm_drv->ap2mdm_pmic_pwr_en_gpio, 0);

	/* give modem PMIC debounce time, spec is ~3.5 but 1s will be enough */
	msleep(1500);

	return NOTIFY_DONE;
}

static struct notifier_block mdm_down_block = {
	.notifier_call  = mdm_reboot_notifier,
	.priority = 1,
};

static irqreturn_t mdm_status_change(int irq, void *dev_id)
{
	int value = gpio_get_value(mdm_drv->mdm2ap_status_gpio);

	pr_debug("%s: mdm sent status change interrupt\n", __func__);
	if (value == 0 && mdm_drv->mdm_ready == 1) {
		pr_info("%s: unexpected reset external modem\n", __func__);
		mdm_drv->mdm_unexpected_reset_occurred = 1;
		mdm_drv->mdm_ready = 0;
		notify_modem_fatal();
		subsystem_restart(EXTERNAL_MODEM);
	} else if (value == 1) {
		cancel_delayed_work(&mdm2ap_status_check_work);
		pr_info("%s: status = 1: mdm is now ready\n", __func__);
		queue_work(mdm_queue, &mdm_status_work);
	}
	return IRQ_HANDLED;
}

static irqreturn_t mdm_pblrdy_change(int irq, void *dev_id)
{
	pr_info("%s: pbl ready:%d\n", __func__,
			gpio_get_value(mdm_drv->mdm2ap_pblrdy));

	return IRQ_HANDLED;
}

static int mdm_subsys_shutdown(const struct subsys_data *crashed_subsys)
{
	pr_info("%s\n", __func__);

	gpio_direction_output(mdm_drv->ap2mdm_errfatal_gpio, 1);
	if (mdm_drv->pdata->ramdump_delay_ms > 0) {
		/* Wait for the external modem to complete
		 * its preparation for ramdumps.
		 */
		msleep(mdm_drv->pdata->ramdump_delay_ms);
	}

	/* close silent log */
	silent_log_panic_handler();

	#if 0
	if (!mdm_drv->mdm_unexpected_reset_occurred)
		mdm_drv->ops->reset_mdm_cb(mdm_drv);
	else
		mdm_drv->mdm_unexpected_reset_occurred = 0;

	#endif
	return 0;
}

static int mdm_subsys_powerup(const struct subsys_data *crashed_subsys)
{
	pr_info("%s\n", __func__);
	gpio_direction_output(mdm_drv->ap2mdm_errfatal_gpio, 0);
	gpio_direction_output(mdm_drv->ap2mdm_status_gpio, 0);
	mdm_drv->ops->power_on_mdm_cb(mdm_drv);
	mdm_drv->boot_type = CHARM_NORMAL_BOOT;
	complete(&mdm_needs_reload);
	if (!wait_for_completion_timeout(&mdm_boot,
			msecs_to_jiffies(MDM_BOOT_TIMEOUT))) {
		mdm_drv->mdm_boot_status = -ETIMEDOUT;
		pr_info("%s: mdm modem restart timed out.\n", __func__);
	} else {
		pr_info("%s: mdm modem has been restarted\n", __func__);

		/* Log the reason for the restart */
		if (mdm_drv->pdata->sfr_query)
			queue_work(mdm_sfr_queue, &sfr_reason_work);
	}
	INIT_COMPLETION(mdm_boot);
	return mdm_drv->mdm_boot_status;
}

static int mdm_subsys_ramdumps(int want_dumps,
				const struct subsys_data *crashed_subsys)
{
	pr_info("%s(dump = %d)\n", __func__, want_dumps);
	mdm_drv->mdm_ram_dump_status = 0;
	if (want_dumps) {
		mdm_drv->boot_type = CHARM_RAM_DUMPS;
		complete(&mdm_needs_reload);
		pr_info("%s: waiting ramdump ...\n", __func__);
		if (!wait_for_completion_timeout(&mdm_ram_dumps,
				msecs_to_jiffies(dump_timeout_ms))) {
			mdm_drv->mdm_ram_dump_status = -ETIMEDOUT;
			pr_info("%s: mdm modem ramdumps timed out.\n",
					__func__);
		} else {
			pr_info("%s: mdm modem ramdumps completed.\n",
					__func__);
			mdm_drv->mdm_ram_dump_status = 0;
		}
		INIT_COMPLETION(mdm_ram_dumps);
		if (!mdm_drv->pdata->no_powerdown_after_ramdumps)
			mdm_drv->ops->power_down_mdm_cb(mdm_drv);
	}
	return mdm_drv->mdm_ram_dump_status;
}

static struct subsys_data mdm_subsystem = {
	.shutdown = mdm_subsys_shutdown,
	.ramdump = mdm_subsys_ramdumps,
	.powerup = mdm_subsys_powerup,
	.name = EXTERNAL_MODEM,
};

static int mdm_debug_on_set(void *data, u64 val)
{
	mdm_debug_on = val;
	if (mdm_drv->ops->debug_state_changed_cb)
		mdm_drv->ops->debug_state_changed_cb(mdm_debug_on);
	return 0;
}

static int mdm_debug_on_get(void *data, u64 *val)
{
	*val = mdm_debug_on;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(mdm_debug_on_fops,
			mdm_debug_on_get,
			mdm_debug_on_set, "%llu\n");

#ifndef CONFIG_ARCH_EXYNOS
static int mdm_debugfs_init(void)
{
	struct dentry *dent;

	dent = debugfs_create_dir("mdm_dbg", 0);
	if (IS_ERR(dent))
		return PTR_ERR(dent);

	debugfs_create_file("debug_on", 0644, dent, NULL,
			&mdm_debug_on_fops);
	return 0;
}
#endif


static void mdm_modem_initialize_data(struct platform_device  *pdev,
				struct mdm_ops *mdm_ops)
{
	struct resource *pres;

	/* MDM2AP_ERRFATAL */
	pres = platform_get_resource_byname(pdev, IORESOURCE_IO,
							"MDM2AP_ERRFATAL");
	if (pres)
		mdm_drv->mdm2ap_errfatal_gpio = pres->start;

	/* AP2MDM_ERRFATAL */
	pres = platform_get_resource_byname(pdev, IORESOURCE_IO,
							"AP2MDM_ERRFATAL");
	if (pres)
		mdm_drv->ap2mdm_errfatal_gpio = pres->start;

	/* MDM2AP_STATUS */
	pres = platform_get_resource_byname(pdev, IORESOURCE_IO,
							"MDM2AP_STATUS");
	if (pres)
		mdm_drv->mdm2ap_status_gpio = pres->start;

	/* AP2MDM_STATUS */
	pres = platform_get_resource_byname(pdev, IORESOURCE_IO,
							"AP2MDM_STATUS");
	if (pres)
		mdm_drv->ap2mdm_status_gpio = pres->start;

	/* MDM2AP_WAKEUP */
	pres = platform_get_resource_byname(pdev, IORESOURCE_IO,
							"MDM2AP_WAKEUP");
	if (pres)
		mdm_drv->mdm2ap_wakeup_gpio = pres->start;

	/* AP2MDM_WAKEUP */
	pres = platform_get_resource_byname(pdev, IORESOURCE_IO,
							"AP2MDM_WAKEUP");
	if (pres)
		mdm_drv->ap2mdm_wakeup_gpio = pres->start;

	/* AP2MDM_SOFT_RESET */
	pres = platform_get_resource_byname(pdev, IORESOURCE_IO,
							"AP2MDM_SOFT_RESET");
	if (pres)
		mdm_drv->ap2mdm_soft_reset_gpio = pres->start;

	/* AP2MDM_KPDPWR_N */
	pres = platform_get_resource_byname(pdev, IORESOURCE_IO,
							"AP2MDM_KPDPWR_N");
	if (pres)
		mdm_drv->ap2mdm_kpdpwr_n_gpio = pres->start;

	/* AP2MDM_PMIC_PWR_EN */
	pres = platform_get_resource_byname(pdev, IORESOURCE_IO,
							"AP2MDM_PMIC_PWR_EN");
	if (pres)
		mdm_drv->ap2mdm_pmic_pwr_en_gpio = pres->start;

	/* MDM2AP_PBLRDY */
	pres = platform_get_resource_byname(pdev, IORESOURCE_IO,
							"MDM2AP_PBLRDY");
	if (pres)
		mdm_drv->mdm2ap_pblrdy = pres->start;
#ifdef CONFIG_SIM_DETECT
	/* SIM_DETECT */
	pres = platform_get_resource_byname(pdev, IORESOURCE_IO,
							"SIM_DETECT");
	if (pres)
		mdm_drv->sim_detect_gpio = pres->start;
	else
		pr_err("%s: fail to get resource\n", __func__);
#endif

	mdm_drv->boot_type                  = CHARM_NORMAL_BOOT;

	mdm_drv->ops      = mdm_ops;
	mdm_drv->pdata    = pdev->dev.platform_data;
	dump_timeout_ms = mdm_drv->pdata->ramdump_timeout_ms > 0 ?
		mdm_drv->pdata->ramdump_timeout_ms : MDM_RDUMP_TIMEOUT;
}

int mdm_common_create(struct platform_device  *pdev,
					  struct mdm_ops *p_mdm_cb)
{
	int ret = -1, irq;
	pr_err("%s\n", __func__);

	mdm_drv = kzalloc(sizeof(struct mdm_modem_drv), GFP_KERNEL);
	if (mdm_drv == NULL) {
		pr_err("%s: kzalloc fail.\n", __func__);
		goto alloc_err;
	}

	mdm_modem_initialize_data(pdev, p_mdm_cb);
	if (mdm_drv->ops->debug_state_changed_cb)
		mdm_drv->ops->debug_state_changed_cb(mdm_debug_on);

	gpio_request(mdm_drv->ap2mdm_status_gpio, "AP2MDM_STATUS");
	gpio_request(mdm_drv->ap2mdm_errfatal_gpio, "AP2MDM_ERRFATAL");
	if (mdm_drv->ap2mdm_kpdpwr_n_gpio > 0)
		gpio_request(mdm_drv->ap2mdm_kpdpwr_n_gpio, "AP2MDM_KPDPWR_N");
	gpio_request(mdm_drv->mdm2ap_status_gpio, "MDM2AP_STATUS");
	gpio_request(mdm_drv->mdm2ap_errfatal_gpio, "MDM2AP_ERRFATAL");
#ifdef CONFIG_SIM_DETECT
	gpio_request(mdm_drv->sim_detect_gpio, "SIM_DETECT");
#endif
	if (mdm_drv->mdm2ap_pblrdy > 0)
		gpio_request(mdm_drv->mdm2ap_pblrdy, "MDM2AP_PBLRDY");

	if (mdm_drv->ap2mdm_pmic_pwr_en_gpio > 0) {
		gpio_request(mdm_drv->ap2mdm_pmic_pwr_en_gpio,
					 "AP2MDM_PMIC_PWR_EN");
		gpio_set_value(mdm_drv->ap2mdm_pmic_pwr_en_gpio, 0);
#ifdef CONFIG_ARCH_EXYNOS
		s3c_gpio_cfgpin(mdm_drv->ap2mdm_pmic_pwr_en_gpio,
							S3C_GPIO_OUTPUT);
		s3c_gpio_setpull(mdm_drv->ap2mdm_pmic_pwr_en_gpio,
							S3C_GPIO_PULL_UP);
		s5p_gpio_set_drvstr(mdm_drv->ap2mdm_pmic_pwr_en_gpio,
							S5P_GPIO_DRVSTR_LV4);
#endif
	}
	if (mdm_drv->ap2mdm_soft_reset_gpio > 0) {
		gpio_request(mdm_drv->ap2mdm_soft_reset_gpio,
					 "AP2MDM_SOFT_RESET");
		gpio_set_value(mdm_drv->ap2mdm_soft_reset_gpio, 0);
#ifdef CONFIG_ARCH_EXYNOS
		s3c_gpio_cfgpin(mdm_drv->ap2mdm_soft_reset_gpio,
							S3C_GPIO_OUTPUT);
		s3c_gpio_setpull(mdm_drv->ap2mdm_soft_reset_gpio,
							S3C_GPIO_PULL_UP);
#endif
	}
	if (mdm_drv->ap2mdm_wakeup_gpio > 0) {
		gpio_request(mdm_drv->ap2mdm_wakeup_gpio, "AP2MDM_WAKEUP");
		gpio_set_value(mdm_drv->ap2mdm_wakeup_gpio, 0);
#ifdef CONFIG_ARCH_EXYNOS
		s3c_gpio_cfgpin(mdm_drv->ap2mdm_wakeup_gpio, S3C_GPIO_OUTPUT);
		s3c_gpio_setpull(mdm_drv->ap2mdm_wakeup_gpio,
							S3C_GPIO_PULL_NONE);
#endif
	}

	gpio_direction_output(mdm_drv->ap2mdm_errfatal_gpio, 0);
#ifdef CONFIG_ARCH_EXYNOS
	s3c_gpio_cfgpin(mdm_drv->ap2mdm_errfatal_gpio, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(mdm_drv->ap2mdm_errfatal_gpio, S3C_GPIO_PULL_NONE);
#endif

	if (mdm_drv->ap2mdm_wakeup_gpio > 0)
		gpio_direction_output(mdm_drv->ap2mdm_wakeup_gpio, 0);

	gpio_direction_input(mdm_drv->mdm2ap_status_gpio);
	gpio_direction_input(mdm_drv->mdm2ap_errfatal_gpio);
#ifdef CONFIG_SIM_DETECT
	gpio_direction_input(mdm_drv->sim_detect_gpio);
	init_waitqueue_head(&mdm_drv->wq);
#endif

	mdm_queue = create_singlethread_workqueue("mdm_queue");
	if (!mdm_queue) {
		pr_err("%s: could not create workqueue. All mdm "
				"functionality will be disabled\n",
			__func__);
		ret = -ENOMEM;
		goto fatal_err;
	}

#ifndef CONFIG_ARCH_EXYNOS
	mdm_sfr_queue = alloc_workqueue("mdm_sfr_queue", 0, 0);
	if (!mdm_sfr_queue) {
		pr_err("%s: could not create workqueue mdm_sfr_queue."
			" All mdm functionality will be disabled\n",
			__func__);
		ret = -ENOMEM;
		destroy_workqueue(mdm_queue);
		goto fatal_err;
	}
#endif

	atomic_notifier_chain_register(&panic_notifier_list, &mdm_panic_blk);
	register_reboot_notifier(&mdm_down_block);

#ifndef CONFIG_ARCH_EXYNOS
	mdm_debugfs_init();
#endif

	/* Register subsystem handlers */
	ssr_register_subsystem(&mdm_subsystem);

	/* ERR_FATAL irq. */
#ifdef CONFIG_ARCH_EXYNOS
	irq = gpio_to_irq(mdm_drv->mdm2ap_errfatal_gpio);
	s3c_gpio_cfgpin(mdm_drv->mdm2ap_errfatal_gpio, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(mdm_drv->mdm2ap_errfatal_gpio, S3C_GPIO_PULL_NONE);
#else
	irq = MSM_GPIO_TO_INT(mdm_drv->mdm2ap_errfatal_gpio);
#endif
	if (irq < 0) {
		pr_err("%s: could not get MDM2AP_ERRFATAL IRQ resource. "
			"error=%d No IRQ will be generated on errfatal.",
			__func__, irq);
		goto errfatal_err;
	}
	ret = request_irq(irq, mdm_errfatal,
		IRQF_TRIGGER_RISING , "mdm errfatal", NULL);

	if (ret < 0) {
		pr_err("%s: MDM2AP_ERRFATAL IRQ#%d request failed with error=%d"
			". No IRQ will be generated on errfatal.",
			__func__, irq, ret);
		goto errfatal_err;
	}
	mdm_drv->mdm_errfatal_irq = irq;
	enable_irq_wake(irq);

errfatal_err:
	/* status irq */
#ifdef CONFIG_ARCH_EXYNOS
	s3c_gpio_cfgpin(mdm_drv->mdm2ap_status_gpio, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(mdm_drv->mdm2ap_status_gpio, S3C_GPIO_PULL_NONE);
	irq = gpio_to_irq(mdm_drv->mdm2ap_status_gpio);
#else
	irq = MSM_GPIO_TO_INT(mdm_drv->mdm2ap_status_gpio);
#endif
	if (irq < 0) {
		pr_err("%s: could not get MDM2AP_STATUS IRQ resource. "
			"error=%d No IRQ will be generated on status change.",
			__func__, irq);
		goto status_err;
	}

	ret = request_threaded_irq(irq, NULL, mdm_status_change,
		IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_SHARED,
		"mdm status", mdm_drv);

	if (ret < 0) {
		pr_err("%s: MDM2AP_STATUS IRQ#%d request failed with error=%d"
			". No IRQ will be generated on status change.",
			__func__, irq, ret);
		goto status_err;
	}
	mdm_drv->mdm_status_irq = irq;
	enable_irq_wake(irq);

status_err:
#ifdef CONFIG_SIM_DETECT
	/* sim detect irq */
#ifdef CONFIG_ARCH_EXYNOS
	s3c_gpio_cfgpin(mdm_drv->sim_detect_gpio, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(mdm_drv->sim_detect_gpio, S3C_GPIO_PULL_NONE);
	irq = gpio_to_irq(mdm_drv->sim_detect_gpio);
#else
	irq = MSM_GPIO_TO_INT(mdm_drv->sim_detect_gpio);
#endif
	if (irq < 0) {
		pr_err("%s: could not get SIM DETECT IRQ resource. "
			"error=%d No IRQ will be generated on status change.",
			__func__, irq);
		goto simdetect_err;
	}

	ret = request_threaded_irq(irq, NULL, sim_detect_irq_handler,
		IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_SHARED,
		"sim detect", mdm_drv);

	if (ret < 0) {
		pr_err("%s: SIM_DETECT IRQ#%d request failed with error=%d"
			". No IRQ will be generated on status change.",
			__func__, irq, ret);
		goto simdetect_err;
	}

	mdm_drv->mdm_status_irq = irq;
	enable_irq_wake(irq);
simdetect_err:
#endif

	if (mdm_drv->mdm2ap_pblrdy > 0) {
#ifdef CONFIG_ARCH_EXYNOS
		s3c_gpio_cfgpin(mdm_drv->mdm2ap_pblrdy, S3C_GPIO_SFN(0xf));
		s3c_gpio_setpull(mdm_drv->mdm2ap_pblrdy, S3C_GPIO_PULL_NONE);
		irq = gpio_to_irq(mdm_drv->mdm2ap_pblrdy);
#else
		irq = MSM_GPIO_TO_INT(mdm_drv->mdm2ap_pblrdy);
#endif
		if (irq < 0) {
			pr_err("%s: could not get MDM2AP_PBLRDY IRQ resource",
				__func__);
			goto pblrdy_err;
		}

		ret = request_threaded_irq(irq, NULL, mdm_pblrdy_change,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |
			IRQF_SHARED,
			"mdm pbl ready", mdm_drv);

		if (ret < 0) {
			pr_err("%s: MDM2AP_PBL IRQ#%d request failed error=%d",
				__func__, irq, ret);
			goto pblrdy_err;
		}
	}

pblrdy_err:
	/*
	 * If AP2MDM_PMIC_PWR_EN gpio is used, pull it high. It remains
	 * high until the whole phone is shut down.
	 */
	if (mdm_drv->ap2mdm_pmic_pwr_en_gpio > 0)
		gpio_direction_output(mdm_drv->ap2mdm_pmic_pwr_en_gpio, 1);
	/* Register VDDmin gpios with RPM */
	mdm_setup_vddmin_gpios();

	/* Perform early powerup of the external modem in order to
	 * allow tabla devices to be found.
	 */
	if (mdm_drv->pdata->early_power_on)
		mdm_drv->ops->power_on_mdm_cb(mdm_drv);

	pr_info("%s: Registering mdm modem\n", __func__);
	return misc_register(&mdm_modem_misc);

fatal_err:
	gpio_free(mdm_drv->ap2mdm_status_gpio);
	gpio_free(mdm_drv->ap2mdm_errfatal_gpio);
	if (mdm_drv->ap2mdm_kpdpwr_n_gpio > 0)
		gpio_free(mdm_drv->ap2mdm_kpdpwr_n_gpio);
	if (mdm_drv->ap2mdm_pmic_pwr_en_gpio > 0)
		gpio_free(mdm_drv->ap2mdm_pmic_pwr_en_gpio);
	gpio_free(mdm_drv->mdm2ap_status_gpio);
	gpio_free(mdm_drv->mdm2ap_errfatal_gpio);
#ifdef CONFIG_MACH_SIM_DETECT
	gpio_free(mdm_drv->sim_detect_gpio);
#endif
	if (mdm_drv->ap2mdm_soft_reset_gpio > 0)
		gpio_free(mdm_drv->ap2mdm_soft_reset_gpio);

	if (mdm_drv->ap2mdm_wakeup_gpio > 0)
		gpio_free(mdm_drv->ap2mdm_wakeup_gpio);

	kfree(mdm_drv);
	ret = -ENODEV;

alloc_err:
	return ret;
}

int mdm_common_modem_remove(struct platform_device *pdev)
{
	int ret;

	gpio_free(mdm_drv->ap2mdm_status_gpio);
	gpio_free(mdm_drv->ap2mdm_errfatal_gpio);
	if (mdm_drv->ap2mdm_kpdpwr_n_gpio > 0)
		gpio_free(mdm_drv->ap2mdm_kpdpwr_n_gpio);
	if (mdm_drv->ap2mdm_pmic_pwr_en_gpio > 0)
		gpio_free(mdm_drv->ap2mdm_pmic_pwr_en_gpio);
	gpio_free(mdm_drv->mdm2ap_status_gpio);
	gpio_free(mdm_drv->mdm2ap_errfatal_gpio);
#ifdef CONFIG_SIM_DETECT
	gpio_free(mdm_drv->sim_detect_gpio);
#endif
	if (mdm_drv->ap2mdm_soft_reset_gpio > 0)
		gpio_free(mdm_drv->ap2mdm_soft_reset_gpio);

	if (mdm_drv->ap2mdm_wakeup_gpio > 0)
		gpio_free(mdm_drv->ap2mdm_wakeup_gpio);

	kfree(mdm_drv);

	ret = misc_deregister(&mdm_modem_misc);
	return ret;
}

void mdm_common_modem_shutdown(struct platform_device *pdev)
{
	mdm_disable_irqs();

	mdm_drv->ops->power_down_mdm_cb(mdm_drv);
	if (mdm_drv->ap2mdm_pmic_pwr_en_gpio > 0)
		gpio_direction_output(mdm_drv->ap2mdm_pmic_pwr_en_gpio, 0);
}
