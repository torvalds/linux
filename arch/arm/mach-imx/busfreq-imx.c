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

static int ddr_type;
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

extern int init_mmdc_lpddr2_settings(struct platform_device *dev);
extern int init_mmdc_ddr3_settings_imx6_up(struct platform_device *dev);
extern int init_ddrc_ddr_settings(struct platform_device *dev);
extern int update_ddr_freq_imx_smp(int ddr_rate);
extern int update_ddr_freq_imx6_up(int ddr_rate);
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

static struct clk *m4_clk;
static struct clk *arm_clk;
static struct clk *pll3_clk;
static struct clk *step_clk;
static struct clk *mmdc_clk;
static struct clk *ocram_clk;
static struct clk *pll2_400_clk;
static struct clk *pll2_200_clk;
static struct clk *pll2_bus_clk;
static struct clk *periph_clk;
static struct clk *periph_pre_clk;
static struct clk *periph_clk2_clk;
static struct clk *periph_clk2_sel_clk;
static struct clk *periph2_clk;
static struct clk *periph2_pre_clk;
static struct clk *periph2_clk2_clk;
static struct clk *periph2_clk2_sel_clk;

static struct delayed_work low_bus_freq_handler;
static struct delayed_work bus_freq_daemon;

static RAW_NOTIFIER_HEAD(busfreq_notifier_chain);

static bool check_m4_sleep(void)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(500);

	while (imx_gpc_is_m4_sleeping() == 0)
		if (time_after(jiffies, timeout))
			return false;
	return  true;
}

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

/*
 * enter_lpm_imx6_up and exit_lpm_imx6_up is used by
 * i.MX6SX/i.MX6UL for entering and exiting lpm mode.
 */
static void enter_lpm_imx6_up(void)
{
	if (cpu_is_imx6sx() && imx_src_is_m4_enabled())
		if (!check_m4_sleep())
			pr_err("M4 is NOT in sleep!!!\n");

	/* set periph_clk2 to source from OSC for periph */
	imx_clk_set_parent(periph_clk2_sel_clk, osc_clk);
	imx_clk_set_parent(periph_clk, periph_clk2_clk);
	/* set ahb/ocram to 24MHz */
	imx_clk_set_rate(ahb_clk, LPAPM_CLK);
	imx_clk_set_rate(ocram_clk, LPAPM_CLK);

	if (audio_bus_count) {
		/* Need to ensure that PLL2_PFD_400M is kept ON. */
		clk_prepare_enable(pll2_400_clk);
		if (ddr_type == IMX_DDR_TYPE_DDR3)
			update_ddr_freq_imx6_up(LOW_AUDIO_CLK);
		else if (ddr_type == IMX_DDR_TYPE_LPDDR2)
			update_lpddr2_freq(HIGH_AUDIO_CLK);
		imx_clk_set_parent(periph2_clk2_sel_clk, pll3_clk);
		imx_clk_set_parent(periph2_pre_clk, pll2_400_clk);
		imx_clk_set_parent(periph2_clk, periph2_pre_clk);
		/*
		 * As periph2_clk's parent is not changed from
		 * high mode to audio mode, so clk framework
		 * will not update its children's freq, but we
		 * change the mmdc's podf in asm code, so here
		 * need to update mmdc rate to make sure clk
		 * tree is right, although it will not do any
		 * change to hardware.
		 */
		if (high_bus_freq_mode) {
			if (ddr_type == IMX_DDR_TYPE_DDR3)
				imx_clk_set_rate(mmdc_clk, LOW_AUDIO_CLK);
			else if (ddr_type == IMX_DDR_TYPE_LPDDR2)
				imx_clk_set_rate(mmdc_clk, HIGH_AUDIO_CLK);
		}
		audio_bus_freq_mode = 1;
		low_bus_freq_mode = 0;
		cur_bus_freq_mode = BUS_FREQ_AUDIO;
	} else {
		if (ddr_type == IMX_DDR_TYPE_DDR3)
			update_ddr_freq_imx6_up(LPAPM_CLK);
		else if (ddr_type == IMX_DDR_TYPE_LPDDR2)
			update_lpddr2_freq(LPAPM_CLK);
		imx_clk_set_parent(periph2_clk2_sel_clk, osc_clk);
		imx_clk_set_parent(periph2_clk, periph2_clk2_clk);

		if (audio_bus_freq_mode)
			clk_disable_unprepare(pll2_400_clk);
		low_bus_freq_mode = 1;
		audio_bus_freq_mode = 0;
		cur_bus_freq_mode = BUS_FREQ_LOW;
	}
}

static void exit_lpm_imx6_up(void)
{
	clk_prepare_enable(pll2_400_clk);

	/*
	 * lower ahb/ocram's freq first to avoid too high
	 * freq during parent switch from OSC to pll3.
	 */
	if (cpu_is_imx6ul())
		imx_clk_set_rate(ahb_clk, LPAPM_CLK / 4);
	else
		imx_clk_set_rate(ahb_clk, LPAPM_CLK / 3);

	imx_clk_set_rate(ocram_clk, LPAPM_CLK / 2);
	/* set periph_clk2 to pll3 */
	imx_clk_set_parent(periph_clk2_sel_clk, pll3_clk);
	/* set periph clk to from pll2_bus on i.MX6UL */
	if (cpu_is_imx6ul())
		imx_clk_set_parent(periph_pre_clk, pll2_bus_clk);
	/* set periph clk to from pll2_400 */
	else
		imx_clk_set_parent(periph_pre_clk, pll2_400_clk);
	imx_clk_set_parent(periph_clk, periph_pre_clk);

	if (ddr_type == IMX_DDR_TYPE_DDR3)
		update_ddr_freq_imx6_up(ddr_normal_rate);
	else if (ddr_type == IMX_DDR_TYPE_LPDDR2)
		update_lpddr2_freq(ddr_normal_rate);
	/* correct parent info after ddr freq change in asm code */
	imx_clk_set_parent(periph2_pre_clk, pll2_400_clk);
	imx_clk_set_parent(periph2_clk, periph2_pre_clk);
	imx_clk_set_parent(periph2_clk2_sel_clk, pll3_clk);

	/*
	 * As periph2_clk's parent is not changed from
	 * audio mode to high mode, so clk framework
	 * will not update its children's freq, but we
	 * change the mmdc's podf in asm code, so here
	 * need to update mmdc rate to make sure clk
	 * tree is right, although it will not do any
	 * change to hardware.
	 */
	if (audio_bus_freq_mode)
		imx_clk_set_rate(mmdc_clk, ddr_normal_rate);

	clk_disable_unprepare(pll2_400_clk);

	if (audio_bus_freq_mode)
		clk_disable_unprepare(pll2_400_clk);
}

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
	if (cpu_is_imx6())
		clk_prepare_enable(pll3_clk);

	if (audio_bus_count && (low_bus_freq_mode || ultra_low_bus_freq_mode))
		busfreq_notify(LOW_BUSFREQ_EXIT);
	else if (!audio_bus_count)
		busfreq_notify(LOW_BUSFREQ_ENTER);

	if (cpu_is_imx7d())
		enter_lpm_imx7d();
	else if (cpu_is_imx6sx() || cpu_is_imx6ul())
		enter_lpm_imx6_up();

	med_bus_freq_mode = 0;
	high_bus_freq_mode = 0;

	if (cpu_is_imx6())
		clk_disable_unprepare(pll3_clk);

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
	struct clk *periph_clk_parent;

	if (bus_freq_scaling_initialized && bus_freq_scaling_is_active)
		cancel_delayed_work_sync(&low_bus_freq_handler);

	if (busfreq_suspended)
		return 0;

	if (cpu_is_imx6q())
		periph_clk_parent = pll2_bus_clk;
	else
		periph_clk_parent = pll2_400_clk;

	if (!bus_freq_scaling_initialized || !bus_freq_scaling_is_active)
		return 0;

	if (high_bus_freq_mode)
		return 0;

	/* medium bus freq is only supported for MX6DQ */
	if (med_bus_freq_mode && !high_bus_freq)
		return 0;

	if (low_bus_freq_mode || ultra_low_bus_freq_mode)
		busfreq_notify(LOW_BUSFREQ_EXIT);

	if (cpu_is_imx6())
		clk_prepare_enable(pll3_clk);

	if (cpu_is_imx7d())
		exit_lpm_imx7d();
	else if (cpu_is_imx6sx() || cpu_is_imx6ul())
		exit_lpm_imx6_up();

	high_bus_freq_mode = 1;
	med_bus_freq_mode = 0;
	low_bus_freq_mode = 0;
	audio_bus_freq_mode = 0;
	cur_bus_freq_mode = BUS_FREQ_HIGH;

	if (cpu_is_imx6())
		clk_disable_unprepare(pll3_clk);

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

	if (cpu_is_imx6sx()) {
		m4_clk = devm_clk_get(&pdev->dev, "m4");
		arm_clk = devm_clk_get(&pdev->dev, "arm");
		osc_clk = devm_clk_get(&pdev->dev, "osc");
		ahb_clk = devm_clk_get(&pdev->dev, "ahb");
		pll3_clk = devm_clk_get(&pdev->dev, "pll3_usb_otg");
		step_clk = devm_clk_get(&pdev->dev, "step");
		mmdc_clk = devm_clk_get(&pdev->dev, "mmdc");
		ocram_clk = devm_clk_get(&pdev->dev, "ocram");
		pll2_400_clk = devm_clk_get(&pdev->dev, "pll2_pfd2_396m");
		pll2_200_clk = devm_clk_get(&pdev->dev, "pll2_198m");
		pll2_bus_clk = devm_clk_get(&pdev->dev, "pll2_bus");
		periph_clk = devm_clk_get(&pdev->dev, "periph");
		periph_pre_clk = devm_clk_get(&pdev->dev, "periph_pre");
		periph_clk2_clk = devm_clk_get(&pdev->dev, "periph_clk2");
		periph_clk2_sel_clk =
			devm_clk_get(&pdev->dev, "periph_clk2_sel");
		periph2_clk = devm_clk_get(&pdev->dev, "periph2");
		periph2_pre_clk = devm_clk_get(&pdev->dev, "periph2_pre");
		periph2_clk2_clk = devm_clk_get(&pdev->dev, "periph2_clk2");
		periph2_clk2_sel_clk =
			devm_clk_get(&pdev->dev, "periph2_clk2_sel");
		if (IS_ERR(m4_clk) || IS_ERR(arm_clk) || IS_ERR(osc_clk)
			|| IS_ERR(ahb_clk) || IS_ERR(pll3_clk)
			|| IS_ERR(step_clk) || IS_ERR(mmdc_clk)
			|| IS_ERR(ocram_clk) || IS_ERR(pll2_400_clk)
			|| IS_ERR(pll2_200_clk) || IS_ERR(pll2_bus_clk)
			|| IS_ERR(periph_clk) || IS_ERR(periph_pre_clk)
			|| IS_ERR(periph_clk2_clk)
			|| IS_ERR(periph_clk2_sel_clk)
			|| IS_ERR(periph2_clk) || IS_ERR(periph2_pre_clk)
			|| IS_ERR(periph2_clk2_clk)
			|| IS_ERR(periph2_clk2_sel_clk)) {
			dev_err(busfreq_dev,
				"%s: failed to get busfreq clk\n", __func__);
			return -EINVAL;
		}
	}

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
	else if (cpu_is_imx6sx()) {
		ddr_type = imx_mmdc_get_ddr_type();
		if (ddr_type == IMX_DDR_TYPE_DDR3)
			err = init_mmdc_ddr3_settings_imx6_up(pdev);
		else if (ddr_type == IMX_DDR_TYPE_LPDDR2)
			err = init_mmdc_lpddr2_settings(pdev);
		/* if M4 is enabled and rate > 24MHz, add high bus count */
		if (imx_src_is_m4_enabled() &&
			(clk_get_rate(m4_clk) > LPAPM_CLK))
				high_bus_count++;
	}

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
