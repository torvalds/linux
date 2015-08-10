/*
 * Copyright (C) 2011-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <asm/cacheflush.h>
#include <asm/fncpy.h>
#include <asm/io.h>
#include <asm/mach/map.h>
#include <asm/mach-types.h>
#include <asm/tlb.h>
#include <linux/busfreq-imx.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/reboot.h>
#include <linux/regulator/consumer.h>
#include <linux/sched.h>
#include <linux/suspend.h>
#include "clk.h"
#include "hardware.h"
#include "common.h"

#define LPAPM_CLK		24000000
#define LOW_AUDIO_CLK		50000000
#define HIGH_AUDIO_CLK		100000000

unsigned int ddr_med_rate;
unsigned int ddr_normal_rate;
unsigned long ddr_freq_change_total_size;
unsigned long ddr_freq_change_iram_base;
unsigned long ddr_freq_change_iram_phys;

static int low_bus_freq_mode;
static int audio_bus_freq_mode;
static int ultra_low_bus_freq_mode;
static int high_bus_freq_mode;
static int med_bus_freq_mode;
static int bus_freq_scaling_initialized;
static struct device *busfreq_dev;
static int busfreq_suspended;
static int bus_freq_scaling_is_active;
static int high_bus_count, med_bus_count, audio_bus_count, low_bus_count;
static unsigned int ddr_low_rate;
static int cur_bus_freq_mode;

extern unsigned long iram_tlb_phys_addr;
extern int unsigned long iram_tlb_base_addr;

extern int init_ddrc_ddr_settings(struct platform_device *dev);
extern int update_ddr_freq_imx_smp(int ddr_rate);
extern int update_lpddr2_freq(int ddr_rate);

DEFINE_MUTEX(bus_freq_mutex);

static struct clk *osc_clk;
static struct clk *ahb_clk;
static struct clk *axi_sel_clk;
static struct clk *dram_root;
static struct clk *dram_alt_sel;
static struct clk *dram_alt_root;
static struct clk *pfd0_392m;
static struct clk *pfd2_270m;
static struct clk *pfd1_332m;
static struct clk *pll_dram;
static struct clk *ahb_sel_clk;
static struct clk *axi_clk;

static struct delayed_work low_bus_freq_handler;
static struct delayed_work bus_freq_daemon;

static RAW_NOTIFIER_HEAD(busfreq_notifier_chain);

static int busfreq_notify(enum busfreq_event event)
{
	int ret;

	ret = raw_notifier_call_chain(&busfreq_notifier_chain, event, NULL);

	return notifier_to_errno(ret);
}

int register_busfreq_notifier(struct notifier_block *nb)
{
	return raw_notifier_chain_register(&busfreq_notifier_chain, nb);
}
EXPORT_SYMBOL(register_busfreq_notifier);

int unregister_busfreq_notifier(struct notifier_block *nb)
{
	return raw_notifier_chain_unregister(&busfreq_notifier_chain, nb);
}
EXPORT_SYMBOL(unregister_busfreq_notifier);

static void enter_lpm_imx7d(void)
{
	if (audio_bus_count) {
		clk_prepare_enable(pfd0_392m);
		update_ddr_freq_imx_smp(HIGH_AUDIO_CLK);

		clk_set_parent(dram_alt_sel, pfd0_392m);
		clk_set_parent(dram_root, dram_alt_root);
		if (high_bus_freq_mode) {
			clk_set_parent(axi_sel_clk, osc_clk);
			clk_set_parent(ahb_sel_clk, osc_clk);
			clk_set_rate(ahb_clk, LPAPM_CLK);
		}
		clk_disable_unprepare(pfd0_392m);
		audio_bus_freq_mode = 1;
		low_bus_freq_mode = 0;
		cur_bus_freq_mode = BUS_FREQ_AUDIO;
	} else {
		update_ddr_freq_imx_smp(LPAPM_CLK);

		clk_set_parent(dram_alt_sel, osc_clk);
		clk_set_parent(dram_root, dram_alt_root);
		if (high_bus_freq_mode) {
			clk_set_parent(axi_sel_clk, osc_clk);
			clk_set_parent(ahb_sel_clk, osc_clk);
			clk_set_rate(ahb_clk, LPAPM_CLK);
		}
		low_bus_freq_mode = 1;
		audio_bus_freq_mode = 0;
		cur_bus_freq_mode = BUS_FREQ_LOW;
	}
}

static void exit_lpm_imx7d(void)
{
	clk_set_parent(axi_sel_clk, pfd1_332m);
	clk_set_rate(ahb_clk, LPAPM_CLK / 2);
	clk_set_parent(ahb_sel_clk, pfd2_270m);

	update_ddr_freq_imx_smp(ddr_normal_rate);

	clk_set_parent(dram_root, pll_dram);
}

static void reduce_bus_freq(void)
{
	if (audio_bus_count && (low_bus_freq_mode || ultra_low_bus_freq_mode))
		busfreq_notify(LOW_BUSFREQ_EXIT);
	else if (!audio_bus_count)
		busfreq_notify(LOW_BUSFREQ_ENTER);

	if (cpu_is_imx7d())
		enter_lpm_imx7d();

	med_bus_freq_mode = 0;
	high_bus_freq_mode = 0;

	if (audio_bus_freq_mode)
		dev_dbg(busfreq_dev,
			"Bus freq set to audio mode. Count: high %d, med %d, audio %d\n",
			high_bus_count, med_bus_count, audio_bus_count);
	if (low_bus_freq_mode)
		dev_dbg(busfreq_dev,
			"Bus freq set to low mode. Count: high %d, med %d, audio %d\n",
			high_bus_count, med_bus_count, audio_bus_count);
}

static void reduce_bus_freq_handler(struct work_struct *work)
{
	mutex_lock(&bus_freq_mutex);

	reduce_bus_freq();

	mutex_unlock(&bus_freq_mutex);
}

/*
 * Set the DDR, AHB to 24MHz.
 * This mode will be activated only when none of the modules that
 * need a higher DDR or AHB frequency are active.
 */
int set_low_bus_freq(void)
{
	if (busfreq_suspended)
		return 0;

	if (!bus_freq_scaling_initialized || !bus_freq_scaling_is_active)
		return 0;

	/*
	 * Check to see if we need to got from
	 * low bus freq mode to audio bus freq mode.
	 * If so, the change needs to be done immediately.
	 */
	if (audio_bus_count && (low_bus_freq_mode || ultra_low_bus_freq_mode))
		reduce_bus_freq();
	else
		/*
		 * Don't lower the frequency immediately. Instead
		 * scheduled a delayed work and drop the freq if
		 * the conditions still remain the same.
		 */
		schedule_delayed_work(&low_bus_freq_handler,
					usecs_to_jiffies(3000000));
	return 0;
}

/*
 * Set the DDR to either 528MHz or 400MHz for iMX6qd
 * or 400MHz for iMX6dl.
 */
static int set_high_bus_freq(int high_bus_freq)
{
	if (bus_freq_scaling_initialized && bus_freq_scaling_is_active)
		cancel_delayed_work_sync(&low_bus_freq_handler);

	if (busfreq_suspended)
		return 0;

	if (!bus_freq_scaling_initialized || !bus_freq_scaling_is_active)
		return 0;

	if (high_bus_freq_mode)
		return 0;

	/* medium bus freq is only supported for MX6DQ */
	if (med_bus_freq_mode && !high_bus_freq)
		return 0;

	if (low_bus_freq_mode || ultra_low_bus_freq_mode)
		busfreq_notify(LOW_BUSFREQ_EXIT);

	if (cpu_is_imx7d())
		exit_lpm_imx7d();

	high_bus_freq_mode = 1;
	med_bus_freq_mode = 0;
	low_bus_freq_mode = 0;
	audio_bus_freq_mode = 0;
	cur_bus_freq_mode = BUS_FREQ_HIGH;

	if (high_bus_freq_mode)
		dev_dbg(busfreq_dev,
			"Bus freq set to high mode. Count: high %d, med %d, audio %d\n",
			high_bus_count, med_bus_count, audio_bus_count);
	if (med_bus_freq_mode)
		dev_dbg(busfreq_dev,
			"Bus freq set to med mode. Count: high %d, med %d, audio %d\n",
			high_bus_count, med_bus_count, audio_bus_count);

	return 0;
}

void request_bus_freq(enum bus_freq_mode mode)
{
	mutex_lock(&bus_freq_mutex);

	if (mode == BUS_FREQ_ULTRA_LOW) {
		dev_dbg(busfreq_dev, "This mode cannot be requested!\n");
		mutex_unlock(&bus_freq_mutex);
		return;
	}

	if (mode == BUS_FREQ_HIGH)
		high_bus_count++;
	else if (mode == BUS_FREQ_MED)
		med_bus_count++;
	else if (mode == BUS_FREQ_AUDIO)
		audio_bus_count++;
	else if (mode == BUS_FREQ_LOW)
		low_bus_count++;

	if (busfreq_suspended || !bus_freq_scaling_initialized ||
		!bus_freq_scaling_is_active) {
		mutex_unlock(&bus_freq_mutex);
		return;
	}
	cancel_delayed_work_sync(&low_bus_freq_handler);

	if ((mode == BUS_FREQ_HIGH) && (!high_bus_freq_mode)) {
		set_high_bus_freq(1);
		mutex_unlock(&bus_freq_mutex);
		return;
	}

	if ((mode == BUS_FREQ_MED) && (!high_bus_freq_mode) &&
		(!med_bus_freq_mode)) {
		set_high_bus_freq(0);
		mutex_unlock(&bus_freq_mutex);
		return;
	}
	if ((mode == BUS_FREQ_AUDIO) && (!high_bus_freq_mode) &&
		(!med_bus_freq_mode) && (!audio_bus_freq_mode)) {
		set_low_bus_freq();
		mutex_unlock(&bus_freq_mutex);
		return;
	}
	mutex_unlock(&bus_freq_mutex);
}
EXPORT_SYMBOL(request_bus_freq);

void release_bus_freq(enum bus_freq_mode mode)
{
	mutex_lock(&bus_freq_mutex);

	if (mode == BUS_FREQ_ULTRA_LOW) {
		dev_dbg(busfreq_dev,
			"This mode cannot be released!\n");
		mutex_unlock(&bus_freq_mutex);
		return;
	}

	if (mode == BUS_FREQ_HIGH) {
		if (high_bus_count == 0) {
			dev_err(busfreq_dev, "high bus count mismatch!\n");
			dump_stack();
			mutex_unlock(&bus_freq_mutex);
			return;
		}
		high_bus_count--;
	} else if (mode == BUS_FREQ_MED) {
		if (med_bus_count == 0) {
			dev_err(busfreq_dev, "med bus count mismatch!\n");
			dump_stack();
			mutex_unlock(&bus_freq_mutex);
			return;
		}
		med_bus_count--;
	} else if (mode == BUS_FREQ_AUDIO) {
		if (audio_bus_count == 0) {
			dev_err(busfreq_dev, "audio bus count mismatch!\n");
			dump_stack();
			mutex_unlock(&bus_freq_mutex);
			return;
		}
		audio_bus_count--;
	} else if (mode == BUS_FREQ_LOW) {
		if (low_bus_count == 0) {
			dev_err(busfreq_dev, "low bus count mismatch!\n");
			dump_stack();
			mutex_unlock(&bus_freq_mutex);
			return;
		}
		low_bus_count--;
	}

	if (busfreq_suspended || !bus_freq_scaling_initialized ||
		!bus_freq_scaling_is_active) {
		mutex_unlock(&bus_freq_mutex);
		return;
	}

	if ((!audio_bus_freq_mode) && (high_bus_count == 0) &&
		(med_bus_count == 0) && (audio_bus_count != 0)) {
		set_low_bus_freq();
		mutex_unlock(&bus_freq_mutex);
		return;
	}
	if ((!low_bus_freq_mode) && (high_bus_count == 0) &&
		(med_bus_count == 0) && (audio_bus_count == 0) &&
		(low_bus_count != 0)) {
		set_low_bus_freq();
		mutex_unlock(&bus_freq_mutex);
		return;
	}
	if ((!ultra_low_bus_freq_mode) && (high_bus_count == 0) &&
		(med_bus_count == 0) && (audio_bus_count == 0) &&
		(low_bus_count == 0)) {
		set_low_bus_freq();
		mutex_unlock(&bus_freq_mutex);
		return;
	}

	mutex_unlock(&bus_freq_mutex);
}
EXPORT_SYMBOL(release_bus_freq);

int get_bus_freq_mode(void)
{
	return cur_bus_freq_mode;
}
EXPORT_SYMBOL(get_bus_freq_mode);

static struct map_desc ddr_iram_io_desc __initdata = {
	/* .virtual and .pfn are run-time assigned */
	.length		= SZ_1M,
	.type		= MT_MEMORY_RWX_NONCACHED,
};

const static char *ddr_freq_iram_match[] __initconst = {
	"fsl,ddr-lpm-sram",
	NULL
};

static int __init imx_dt_find_ddr_sram(unsigned long node,
		const char *uname, int depth, void *data)
{
	unsigned long ddr_iram_addr;
	const __be32 *prop;

	if (of_flat_dt_match(node, ddr_freq_iram_match)) {
		unsigned int len;

		prop = of_get_flat_dt_prop(node, "reg", &len);
		if (prop == NULL || len != (sizeof(unsigned long) * 2))
			return -EINVAL;
		ddr_iram_addr = be32_to_cpu(prop[0]);
		ddr_freq_change_total_size = be32_to_cpu(prop[1]);
		ddr_freq_change_iram_phys = ddr_iram_addr;

		/* Make sure ddr_freq_change_iram_phys is 8 byte aligned. */
		if ((uintptr_t)(ddr_freq_change_iram_phys) & (FNCPY_ALIGN - 1))
			ddr_freq_change_iram_phys += FNCPY_ALIGN -
				((uintptr_t)ddr_freq_change_iram_phys %
				(FNCPY_ALIGN));
	}
	return 0;
}

void __init imx_busfreq_map_io(void)
{
	/*
	 * Get the address of IRAM to be used by the ddr frequency
	 * change code from the device tree.
	 */
	WARN_ON(of_scan_flat_dt(imx_dt_find_ddr_sram, NULL));
	if (ddr_freq_change_iram_phys) {
		ddr_freq_change_iram_base = IMX_IO_P2V(
			ddr_freq_change_iram_phys);
		if ((iram_tlb_phys_addr & 0xFFF00000) !=
			(ddr_freq_change_iram_phys & 0xFFF00000)) {
			/* We need to create a 1M page table entry. */
			ddr_iram_io_desc.virtual = IMX_IO_P2V(
				ddr_freq_change_iram_phys & 0xFFF00000);
			ddr_iram_io_desc.pfn = __phys_to_pfn(
				ddr_freq_change_iram_phys & 0xFFF00000);
			iotable_init(&ddr_iram_io_desc, 1);
		}
		memset((void *)ddr_freq_change_iram_base, 0,
			ddr_freq_change_total_size);
	}
}

static void bus_freq_daemon_handler(struct work_struct *work)
{
	mutex_lock(&bus_freq_mutex);
	if ((!low_bus_freq_mode) && (!ultra_low_bus_freq_mode)
		&& (high_bus_count == 0) &&
		(med_bus_count == 0) && (audio_bus_count == 0))
		set_low_bus_freq();
	mutex_unlock(&bus_freq_mutex);
}

static ssize_t bus_freq_scaling_enable_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	if (bus_freq_scaling_is_active)
		return sprintf(buf, "Bus frequency scaling is enabled\n");
	else
		return sprintf(buf, "Bus frequency scaling is disabled\n");
}

static ssize_t bus_freq_scaling_enable_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size)
{
	if (strncmp(buf, "1", 1) == 0) {
		bus_freq_scaling_is_active = 1;
		set_high_bus_freq(1);
		/*
		 * We set bus freq to highest at the beginning,
		 * so we use this daemon thread to make sure system
		 * can enter low bus mode if
		 * there is no high bus request pending
		 */
		schedule_delayed_work(&bus_freq_daemon,
			usecs_to_jiffies(5000000));
	} else if (strncmp(buf, "0", 1) == 0) {
		if (bus_freq_scaling_is_active)
			set_high_bus_freq(1);
		bus_freq_scaling_is_active = 0;
	}
	return size;
}

static int bus_freq_pm_notify(struct notifier_block *nb, unsigned long event,
	void *dummy)
{
	mutex_lock(&bus_freq_mutex);

	if (event == PM_SUSPEND_PREPARE) {
		high_bus_count++;
		set_high_bus_freq(1);
		busfreq_suspended = 1;
	} else if (event == PM_POST_SUSPEND) {
		busfreq_suspended = 0;
		high_bus_count--;
		schedule_delayed_work(&bus_freq_daemon,
			usecs_to_jiffies(5000000));
	}

	mutex_unlock(&bus_freq_mutex);

	return NOTIFY_OK;
}

static int busfreq_reboot_notifier_event(struct notifier_block *this,
						 unsigned long event, void *ptr)
{
	/* System is rebooting. Set the system into high_bus_freq_mode. */
	request_bus_freq(BUS_FREQ_HIGH);

	return 0;
}

static struct notifier_block imx_bus_freq_pm_notifier = {
	.notifier_call = bus_freq_pm_notify,
};

static struct notifier_block imx_busfreq_reboot_notifier = {
	.notifier_call = busfreq_reboot_notifier_event,
};


static DEVICE_ATTR(enable, 0644, bus_freq_scaling_enable_show,
			bus_freq_scaling_enable_store);

/*!
 * This is the probe routine for the bus frequency driver.
 *
 * @param   pdev   The platform device structure
 *
 * @return         The function returns 0 on success
 *
 */

static int busfreq_probe(struct platform_device *pdev)
{
	u32 err;

	busfreq_dev = &pdev->dev;

	/* Return if no IRAM space is allocated for ddr freq change code. */
	if (!ddr_freq_change_iram_base)
		return -ENOMEM;

	if (cpu_is_imx7d()) {
		osc_clk = devm_clk_get(&pdev->dev, "osc");
		axi_sel_clk = devm_clk_get(&pdev->dev, "axi_sel");
		ahb_sel_clk = devm_clk_get(&pdev->dev, "ahb_sel");
		pfd0_392m = devm_clk_get(&pdev->dev, "pfd0_392m");
		dram_root = devm_clk_get(&pdev->dev, "dram_root");
		dram_alt_sel = devm_clk_get(&pdev->dev, "dram_alt_sel");
		pll_dram = devm_clk_get(&pdev->dev, "pll_dram");
		dram_alt_root = devm_clk_get(&pdev->dev, "dram_alt_root");
		pfd1_332m = devm_clk_get(&pdev->dev, "pfd1_332m");
		pfd2_270m = devm_clk_get(&pdev->dev, "pfd2_270m");
		ahb_clk = devm_clk_get(&pdev->dev, "ahb");
		axi_clk = devm_clk_get(&pdev->dev, "axi");
		if (IS_ERR(osc_clk) || IS_ERR(axi_sel_clk) || IS_ERR(ahb_clk)
			|| IS_ERR(pfd0_392m) || IS_ERR(dram_root)
			|| IS_ERR(dram_alt_sel) || IS_ERR(pll_dram)
			|| IS_ERR(dram_alt_root) || IS_ERR(pfd1_332m)
			|| IS_ERR(ahb_clk) || IS_ERR(axi_clk)
			|| IS_ERR(pfd2_270m)) {
			dev_err(busfreq_dev,
				"%s: failed to get busfreq clk\n", __func__);
			return -EINVAL;
		}
	}

	err = sysfs_create_file(&busfreq_dev->kobj, &dev_attr_enable.attr);
	if (err) {
		dev_err(busfreq_dev,
		       "Unable to register sysdev entry for BUSFREQ");
		return err;
	}

	if (of_property_read_u32(pdev->dev.of_node, "fsl,max_ddr_freq",
			&ddr_normal_rate)) {
		dev_err(busfreq_dev, "max_ddr_freq entry missing\n");
		return -EINVAL;
	}

	high_bus_freq_mode = 1;
	med_bus_freq_mode = 0;
	low_bus_freq_mode = 0;
	audio_bus_freq_mode = 0;
	ultra_low_bus_freq_mode = 0;
	cur_bus_freq_mode = BUS_FREQ_HIGH;

	bus_freq_scaling_is_active = 1;
	bus_freq_scaling_initialized = 1;

	ddr_low_rate = LPAPM_CLK;

	INIT_DELAYED_WORK(&low_bus_freq_handler, reduce_bus_freq_handler);
	INIT_DELAYED_WORK(&bus_freq_daemon, bus_freq_daemon_handler);
	register_pm_notifier(&imx_bus_freq_pm_notifier);
	register_reboot_notifier(&imx_busfreq_reboot_notifier);

	/* enter low bus mode if no high speed device enabled */
	schedule_delayed_work(&bus_freq_daemon,
		msecs_to_jiffies(10000));

	/*
	 * Need to make sure to an entry for the ddr freq change code
	 * address in the IRAM page table.
	 * This is only required if the DDR freq code and suspend/idle
	 * code are in different OCRAM spaces.
	 */
	if ((iram_tlb_phys_addr & 0xFFF00000) !=
		(ddr_freq_change_iram_phys & 0xFFF00000)) {
		unsigned long i;

		/*
		 * Make sure the ddr_iram virtual address has a mapping
		 * in the IRAM page table.
		 */
		i = ((IMX_IO_P2V(ddr_freq_change_iram_phys) >> 20) << 2) / 4;
		*((unsigned long *)iram_tlb_base_addr + i) =
			(ddr_freq_change_iram_phys  & 0xFFF00000) |
			TT_ATTRIB_NON_CACHEABLE_1M;
	}

	if (cpu_is_imx7d())
		err = init_ddrc_ddr_settings(pdev);

	if (err) {
		dev_err(busfreq_dev, "Busfreq init of ddr controller failed\n");
		return err;
	}
	return 0;
}

static const struct of_device_id imx_busfreq_ids[] = {
	{ .compatible = "fsl,imx_busfreq", },
	{ /* sentinel */ }
};

static struct platform_driver busfreq_driver = {
	.driver = {
		.name = "imx_busfreq",
		.owner  = THIS_MODULE,
		.of_match_table = imx_busfreq_ids,
		},
	.probe = busfreq_probe,
};

/*!
 * Initialise the busfreq_driver.
 *
 * @return  The function always returns 0.
 */

static int __init busfreq_init(void)
{
#ifndef CONFIG_MX6_VPU_352M
	if (platform_driver_register(&busfreq_driver) != 0)
		return -ENODEV;

	printk(KERN_INFO "Bus freq driver module loaded\n");
#endif
	return 0;
}

static void __exit busfreq_cleanup(void)
{
	sysfs_remove_file(&busfreq_dev->kobj, &dev_attr_enable.attr);

	/* Unregister the device structure */
	platform_driver_unregister(&busfreq_driver);
	bus_freq_scaling_initialized = 0;
}

module_init(busfreq_init);
module_exit(busfreq_cleanup);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("BusFreq driver");
MODULE_LICENSE("GPL");
