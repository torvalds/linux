/* linux/arch/arm/mach-exynos/tmu_exynos.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *      http://www.samsung.com
 *
 * EXYNOS - Thermal Management support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/slab.h>

#include <linux/irq.h>

#include <mach/regs-tmu.h>
#include <mach/cpufreq.h>
#include <plat/s5p-tmu.h>

#define MUX_ADDR_VALUE 6

enum tmu_status_t {
	TMU_STATUS_INIT = 0,
	TMU_STATUS_NORMAL,
	TMU_STATUS_THROTTLED,
	TMU_STATUS_WARNING,
	TMU_STATUS_TRIPPED,
};

static struct workqueue_struct  *tmu_monitor_wq;

static int tmu_tripped_cb(void)
{
	struct power_supply *psy = power_supply_get_by_name("battery");
	union power_supply_propval value;

	if (!psy) {
		pr_err("%s:fail to get batter ps\n", __func__);
		return -ENODEV;
	}

	value.intval = TMU_STATUS_TRIPPED;

	return psy->set_property(psy, POWER_SUPPLY_PROP_TEMP_AMBIENT, &value);
}

static unsigned char get_cur_temp(struct tmu_info *info)
{
	unsigned char curr_temp;
	unsigned char temperature;

	/* After reading temperature code from register, compensating
	 * its value and calculating celsius temperatue,
	 * get current temperatue.
	 */
	curr_temp = __raw_readl(info->tmu_base + CURRENT_TEMP) & 0xff;

	/* compensate and calculate current temperature */
	temperature = curr_temp - info->te1 + TMU_DC_VALUE;
	if (temperature < 0) {
		/* temperature code range are between min 25 and 125 */
		pr_err("%s: Current temperature is unreasonable value\n", __func__);
	}

	return temperature;
}

#ifdef CONFIG_TMU_DEBUG
static void cur_temp_monitor(struct work_struct *work)
{
	unsigned char cur_temp;
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct tmu_info *info =
		 container_of(delayed_work, struct tmu_info, monitor);

	cur_temp = get_cur_temp(info);
	pr_info("current temp = %d\n", cur_temp);
	queue_delayed_work_on(0, tmu_monitor_wq, &info->monitor,
			usecs_to_jiffies(1000 * 1000));
}
#endif

static void tmu_monitor(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct tmu_info *info =
		container_of(delayed_work, struct tmu_info, polling);
	struct tmu_data *data = info->dev->platform_data;
	unsigned char cur_temp;

#ifdef CONFIG_TMU_DEBUG
	cancel_delayed_work(&info->monitor);
#endif
	cur_temp = get_cur_temp(info);
	pr_info("Current: %dc, FLAG=%d\n",
			cur_temp, info->tmu_state);

	switch (info->tmu_state) {
	case TMU_STATUS_NORMAL:
#ifdef CONFIG_TMU_DEBUG
		queue_delayed_work_on(0, tmu_monitor_wq, &info->monitor,
		usecs_to_jiffies(1000 * 1000));
#endif
		cancel_delayed_work(&info->polling);
		enable_irq(info->irq);
		break;
	case TMU_STATUS_THROTTLED:
		if (cur_temp >= data->ts.start_warning)
			info->tmu_state = TMU_STATUS_WARNING;
		else if (cur_temp > data->ts.stop_throttle &&
				cur_temp < data->ts.start_warning)
			exynos_cpufreq_upper_limit(DVFS_LOCK_ID_TMU,
				data->cpulimit.throttle_freq);
		else if (cur_temp <= data->ts.stop_throttle) {
			info->tmu_state = TMU_STATUS_NORMAL;
			exynos_cpufreq_upper_limit_free(DVFS_LOCK_ID_TMU);
		}
		queue_delayed_work_on(0, tmu_monitor_wq,
		&info->polling, usecs_to_jiffies(500 * 1000));
		break;
	case TMU_STATUS_WARNING:
		if (cur_temp >= data->ts.start_tripping)
			info->tmu_state = TMU_STATUS_TRIPPED;
		else if (cur_temp > data->ts.stop_warning && \
				cur_temp < data->ts.start_tripping)
			exynos_cpufreq_upper_limit(DVFS_LOCK_ID_TMU,
				data->cpulimit.warning_freq);
		else if (cur_temp <= data->ts.stop_warning) {
			info->tmu_state = TMU_STATUS_THROTTLED;
			exynos_cpufreq_upper_limit_free(DVFS_LOCK_ID_TMU);
		}
		queue_delayed_work_on(0, tmu_monitor_wq,
				&info->polling, usecs_to_jiffies(500 * 1000));
		break;
	case TMU_STATUS_TRIPPED:
		tmu_tripped_cb();
		queue_delayed_work_on(0, tmu_monitor_wq,
				&info->polling, usecs_to_jiffies(5000 * 1000));
	default:
	    break;
	}
	return;
}

static void s5p_pm_tmu_save(struct tmu_info *info)
{
	info->reg_save[0] = __raw_readl(info->tmu_base + TMU_CON);
	info->reg_save[1] = __raw_readl(info->tmu_base + SAMPLING_INTERNAL);
	info->reg_save[2] = __raw_readl(info->tmu_base + CNT_VALUE0);
	info->reg_save[3] = __raw_readl(info->tmu_base + CNT_VALUE1);
	info->reg_save[4] = __raw_readl(info->tmu_base + THD_TEMP_RISE);
	info->reg_save[5] = __raw_readl(info->tmu_base + THD_TEMP_FALL);
	info->reg_save[6] = __raw_readl(info->tmu_base + INTEN);
}

static void s5p_pm_tmu_restore(struct tmu_info *info)
{
	__raw_writel(info->reg_save[0], info->tmu_base + TMU_CON);
	__raw_writel(info->reg_save[1], info->tmu_base + SAMPLING_INTERNAL);
	__raw_writel(info->reg_save[2], info->tmu_base + CNT_VALUE0);
	__raw_writel(info->reg_save[3], info->tmu_base + CNT_VALUE1);
	__raw_writel(info->reg_save[4], info->tmu_base + THD_TEMP_RISE);
	__raw_writel(info->reg_save[5], info->tmu_base + THD_TEMP_FALL);
	__raw_writel(info->reg_save[6], info->tmu_base + INTEN);
}

static int tmu_start(struct tmu_info *info)
{
	struct tmu_data *data = info->dev->platform_data;
	unsigned int te_temp, con;
	unsigned int throttle_temp, waring_temp, trip_temp;
	unsigned int cooling_temp;
	unsigned int rising_value;
	unsigned int reg_info; /* debugging */

	/* must reload for using efuse value at EXYNOS4212 */
	__raw_writel(TRIMINFO_RELOAD, info->tmu_base + TRIMINFO_CON);

	/* get the compensation parameter */
	te_temp = __raw_readl(info->tmu_base + TRIMINFO);
	info->te1 = te_temp & TRIM_INFO_MASK;
	info->te2 = ((te_temp >> 8) & TRIM_INFO_MASK);

	if ((EFUSE_MIN_VALUE > info->te1) || (info->te1 > EFUSE_MAX_VALUE)
		||  (info->te2 != 0))
		info->te1 = data->efuse_value;

	/*Get RISING & FALLING Threshold value */
	throttle_temp = data->ts.start_throttle
			+ info->te1 - TMU_DC_VALUE;
	waring_temp = data->ts.start_warning
			+ info->te1 - TMU_DC_VALUE;
	trip_temp =  data->ts.start_tripping
			+ info->te1 - TMU_DC_VALUE;
	cooling_temp = 0;

	rising_value = (throttle_temp | (waring_temp<<8) | \
			(trip_temp<<16));

	/* Set interrupt level */
	__raw_writel(rising_value, info->tmu_base + THD_TEMP_RISE);
	__raw_writel(cooling_temp, info->tmu_base + THD_TEMP_FALL);

	/* Set frequecny level */
	exynos_cpufreq_get_level(800000, &data->cpulimit.throttle_freq);
	exynos_cpufreq_get_level(200000, &data->cpulimit.warning_freq);

	/* Need to initail regsiter setting after getting parameter info */
	/* [28:23] vref [11:8] slope - Tunning parameter */
	__raw_writel(data->slope, info->tmu_base + TMU_CON);

	pr_info("TMU initialization is successful!!");
	reg_info = __raw_readl(info->tmu_base + THD_TEMP_RISE);
	pr_info("RISING THRESHOLD = %x", reg_info);

	__raw_writel(INTCLEARALL, info->tmu_base + INTCLEAR);
		/* TMU core enable */
	con = __raw_readl(info->tmu_base + TMU_CON);
	con |= (MUX_ADDR_VALUE<<20 | CORE_EN);

	__raw_writel(con, info->tmu_base + TMU_CON);

	/*LEV0 LEV1 LEV2 interrupt enable */
	__raw_writel(INTEN_RISE0 | INTEN_RISE1 | INTEN_RISE2,	\
		     info->tmu_base + INTEN);
	return 0;
}

static int tmu_initialize(struct platform_device *pdev)
{
	struct tmu_info *info = platform_get_drvdata(pdev);
	unsigned int en;

	en = (__raw_readl(info->tmu_base + TMU_STATUS) & 0x1);

	if (!en) {
		dev_err(&pdev->dev, "failed to start tmu drvier\n");
		return -ENOENT;
	}

	return tmu_start(info);
}

static irqreturn_t tmu_irq(int irq, void *id)
{
	struct tmu_info *info = id;
	unsigned int status;

	disable_irq_nosync(irq);

	status = __raw_readl(info->tmu_base + INTSTAT);

	if (status & INTSTAT_RISE0) {
		pr_info("Throttling interrupt occured!!!!\n");
		__raw_writel(INTCLEAR_RISE0, info->tmu_base + INTCLEAR);
		info->tmu_state = TMU_STATUS_THROTTLED;
		queue_delayed_work_on(0, tmu_monitor_wq,
				&info->polling, usecs_to_jiffies(500 * 1000));
	} else if (status & INTSTAT_RISE1) {
		pr_info("Warning interrupt occured!!!!\n");
		__raw_writel(INTCLEAR_RISE1, info->tmu_base + INTCLEAR);
		info->tmu_state = TMU_STATUS_WARNING;
		queue_delayed_work_on(0, tmu_monitor_wq,
				&info->polling, usecs_to_jiffies(500 * 1000));
	} else if (status & INTSTAT_RISE2) {
		pr_info("Tripping interrupt occured!!!!\n");
		info->tmu_state = TMU_STATUS_TRIPPED;
		__raw_writel(INTCLEAR_RISE2, info->tmu_base + INTCLEAR);
		tmu_tripped_cb();
	} else {
		pr_err("%s: TMU interrupt error\n", __func__);
		return -ENODEV;
	}

	return IRQ_HANDLED;
}

static int __devinit tmu_probe(struct platform_device *pdev)
{
	struct tmu_info *info;
	struct resource *res;
	int	ret = 0;

	pr_debug("%s: probe=%p\n", __func__, pdev);

	info = kzalloc(sizeof(struct tmu_info), GFP_KERNEL);
	if (!info) {
		dev_err(&pdev->dev, "failed to alloc memory!\n");
		ret = -ENOMEM;
		goto err_nores;
	}
	platform_set_drvdata(pdev, info);

	info->dev = &pdev->dev;
	info->tmu_state = TMU_STATUS_INIT;

	info->irq = platform_get_irq(pdev, 0);
	if (info->irq < 0) {
		dev_err(&pdev->dev, "no irq for thermal\n");
		return -ENOENT;
	}

	ret = request_irq(info->irq, tmu_irq,
			IRQF_DISABLED,  "tmu interrupt", info);
	if (ret) {
		dev_err(&pdev->dev, "IRQ%d error %d\n", info->irq, ret);
		goto err_noirq;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "failed to get memory region resource\n");
		return -ENOENT;
	}

	info->ioarea = request_mem_region(res->start,
			res->end-res->start+1, pdev->name);
	if (!(info->ioarea)) {
		dev_err(&pdev->dev, "failed to reserve memory region\n");
		ret = -ENOENT;
		goto err_nores;
	}

	info->tmu_base = ioremap(res->start, (res->end - res->start) + 1);
	if (!(info->tmu_base)) {
		dev_err(&pdev->dev, "failed ioremap()\n");
		ret = -EINVAL;
		goto err_nomap;
	}

	tmu_monitor_wq = create_freezable_workqueue("tmu");
	if (!tmu_monitor_wq) {
		pr_info("Creation of tmu_monitor_wq failed\n");
		return -EFAULT;
	}

#ifdef CONFIG_TMU_DEBUG
	INIT_DELAYED_WORK_DEFERRABLE(&info->monitor, cur_temp_monitor);
	queue_delayed_work_on(0, tmu_monitor_wq, &info->monitor,
			usecs_to_jiffies(1000 * 1000));
#endif
	INIT_DELAYED_WORK_DEFERRABLE(&info->polling, tmu_monitor);

	ret = tmu_initialize(pdev);
	if (ret)
		goto err_noinit;

	return ret;

err_noinit:
	free_irq(info->irq, info);
err_noirq:
	iounmap(info->tmu_base);
err_nomap:
	release_resource(info->ioarea);
err_nores:
	return ret;
}

static int __devinit tmu_remove(struct platform_device *pdev)
{
	struct tmu_info *info = platform_get_drvdata(pdev);

	free_irq(info->irq, (void *)pdev);
	iounmap(info->tmu_base);

	pr_info("%s is removed\n", dev_name(&pdev->dev));
	return 0;
}

#ifdef CONFIG_PM
static int tmu_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct tmu_info *info = platform_get_drvdata(pdev);
	s5p_pm_tmu_save(info);

	return 0;
}

static int tmu_resume(struct platform_device *pdev)
{
	struct tmu_info *info = platform_get_drvdata(pdev);
	s5p_pm_tmu_restore(info);

	return 0;
}

#else
#define s5p_tmu_suspend	NULL
#define s5p_tmu_resume	NULL
#endif

static struct platform_driver tmu_driver = {
	.probe		= tmu_probe,
	.remove		= tmu_remove,
	.suspend	= tmu_suspend,
	.resume		= tmu_resume,
	.driver		= {
		.name	=	"s5p-tmu",
		.owner	=	THIS_MODULE,
		},
};

static int __init tmu_driver_init(void)
{
	return platform_driver_register(&tmu_driver);
}

late_initcall(tmu_driver_init);
