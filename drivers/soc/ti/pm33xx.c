// SPDX-License-Identifier: GPL-2.0
/*
 * AM33XX Power Management Routines
 *
 * Copyright (C) 2012-2018 Texas Instruments Incorporated - http://www.ti.com/
 *	Vaibhav Bedia, Dave Gerlach
 */

#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/err.h>
#include <linux/genalloc.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/platform_data/pm33xx.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/rtc/rtc-omap.h>
#include <linux/sizes.h>
#include <linux/sram.h>
#include <linux/suspend.h>
#include <linux/ti-emif-sram.h>
#include <linux/wkup_m3_ipc.h>

#include <asm/proc-fns.h>
#include <asm/suspend.h>
#include <asm/system_misc.h>

#define AMX3_PM_SRAM_SYMBOL_OFFSET(sym) ((unsigned long)(sym) - \
					 (unsigned long)pm_sram->do_wfi)

#define RTC_SCRATCH_RESUME_REG	0
#define RTC_SCRATCH_MAGIC_REG	1
#define RTC_REG_BOOT_MAGIC	0x8cd0 /* RTC */
#define GIC_INT_SET_PENDING_BASE 0x200
#define AM43XX_GIC_DIST_BASE	0x48241000

static u32 rtc_magic_val;

static int (*am33xx_do_wfi_sram)(unsigned long unused);
static phys_addr_t am33xx_do_wfi_sram_phys;

static struct gen_pool *sram_pool, *sram_pool_data;
static unsigned long ocmcram_location, ocmcram_location_data;

static struct rtc_device *omap_rtc;
static void __iomem *gic_dist_base;

static struct am33xx_pm_platform_data *pm_ops;
static struct am33xx_pm_sram_addr *pm_sram;

static struct device *pm33xx_dev;
static struct wkup_m3_ipc *m3_ipc;

#ifdef CONFIG_SUSPEND
static int rtc_only_idle;
static int retrigger_irq;
static unsigned long suspend_wfi_flags;

static struct wkup_m3_wakeup_src wakeup_src = {.irq_nr = 0,
	.src = "Unknown",
};

static struct wkup_m3_wakeup_src rtc_alarm_wakeup = {
	.irq_nr = 108, .src = "RTC Alarm",
};

static struct wkup_m3_wakeup_src rtc_ext_wakeup = {
	.irq_nr = 0, .src = "Ext wakeup",
};
#endif

static u32 sram_suspend_address(unsigned long addr)
{
	return ((unsigned long)am33xx_do_wfi_sram +
		AMX3_PM_SRAM_SYMBOL_OFFSET(addr));
}

static int am33xx_push_sram_idle(void)
{
	struct am33xx_pm_ro_sram_data ro_sram_data;
	int ret;
	u32 table_addr, ro_data_addr;
	void *copy_addr;

	ro_sram_data.amx3_pm_sram_data_virt = ocmcram_location_data;
	ro_sram_data.amx3_pm_sram_data_phys =
		gen_pool_virt_to_phys(sram_pool_data, ocmcram_location_data);
	ro_sram_data.rtc_base_virt = pm_ops->get_rtc_base_addr();

	/* Save physical address to calculate resume offset during pm init */
	am33xx_do_wfi_sram_phys = gen_pool_virt_to_phys(sram_pool,
							ocmcram_location);

	am33xx_do_wfi_sram = sram_exec_copy(sram_pool, (void *)ocmcram_location,
					    pm_sram->do_wfi,
					    *pm_sram->do_wfi_sz);
	if (!am33xx_do_wfi_sram) {
		dev_err(pm33xx_dev,
			"PM: %s: am33xx_do_wfi copy to sram failed\n",
			__func__);
		return -ENODEV;
	}

	table_addr =
		sram_suspend_address((unsigned long)pm_sram->emif_sram_table);
	ret = ti_emif_copy_pm_function_table(sram_pool, (void *)table_addr);
	if (ret) {
		dev_dbg(pm33xx_dev,
			"PM: %s: EMIF function copy failed\n", __func__);
		return -EPROBE_DEFER;
	}

	ro_data_addr =
		sram_suspend_address((unsigned long)pm_sram->ro_sram_data);
	copy_addr = sram_exec_copy(sram_pool, (void *)ro_data_addr,
				   &ro_sram_data,
				   sizeof(ro_sram_data));
	if (!copy_addr) {
		dev_err(pm33xx_dev,
			"PM: %s: ro_sram_data copy to sram failed\n",
			__func__);
		return -ENODEV;
	}

	return 0;
}

static int am33xx_do_sram_idle(u32 wfi_flags)
{
	int ret = 0;

	if (!m3_ipc || !pm_ops)
		return 0;

	if (wfi_flags & WFI_FLAG_WAKE_M3)
		ret = m3_ipc->ops->prepare_low_power(m3_ipc, WKUP_M3_IDLE);

	return pm_ops->cpu_suspend(am33xx_do_wfi_sram, wfi_flags);
}

static int __init am43xx_map_gic(void)
{
	gic_dist_base = ioremap(AM43XX_GIC_DIST_BASE, SZ_4K);

	if (!gic_dist_base)
		return -ENOMEM;

	return 0;
}

#ifdef CONFIG_SUSPEND
static struct wkup_m3_wakeup_src rtc_wake_src(void)
{
	u32 i;

	i = __raw_readl(pm_ops->get_rtc_base_addr() + 0x44) & 0x40;

	if (i) {
		retrigger_irq = rtc_alarm_wakeup.irq_nr;
		return rtc_alarm_wakeup;
	}

	retrigger_irq = rtc_ext_wakeup.irq_nr;

	return rtc_ext_wakeup;
}

static int am33xx_rtc_only_idle(unsigned long wfi_flags)
{
	omap_rtc_power_off_program(&omap_rtc->dev);
	am33xx_do_wfi_sram(wfi_flags);
	return 0;
}

static int am33xx_pm_suspend(suspend_state_t suspend_state)
{
	int i, ret = 0;

	if (suspend_state == PM_SUSPEND_MEM &&
	    pm_ops->check_off_mode_enable()) {
		pm_ops->prepare_rtc_suspend();
		pm_ops->save_context();
		suspend_wfi_flags |= WFI_FLAG_RTC_ONLY;
		clk_save_context();
		ret = pm_ops->soc_suspend(suspend_state, am33xx_rtc_only_idle,
					  suspend_wfi_flags);

		suspend_wfi_flags &= ~WFI_FLAG_RTC_ONLY;
		dev_info(pm33xx_dev, "Entering RTC Only mode with DDR in self-refresh\n");

		if (!ret) {
			clk_restore_context();
			pm_ops->restore_context();
			m3_ipc->ops->set_rtc_only(m3_ipc);
			am33xx_push_sram_idle();
		}
	} else {
		ret = pm_ops->soc_suspend(suspend_state, am33xx_do_wfi_sram,
					  suspend_wfi_flags);
	}

	if (ret) {
		dev_err(pm33xx_dev, "PM: Kernel suspend failure\n");
	} else {
		i = m3_ipc->ops->request_pm_status(m3_ipc);

		switch (i) {
		case 0:
			dev_info(pm33xx_dev,
				 "PM: Successfully put all powerdomains to target state\n");
			break;
		case 1:
			dev_err(pm33xx_dev,
				"PM: Could not transition all powerdomains to target state\n");
			ret = -1;
			break;
		default:
			dev_err(pm33xx_dev,
				"PM: CM3 returned unknown result = %d\n", i);
			ret = -1;
		}

		/* print the wakeup reason */
		if (rtc_only_idle) {
			wakeup_src = rtc_wake_src();
			pr_info("PM: Wakeup source %s\n", wakeup_src.src);
		} else {
			pr_info("PM: Wakeup source %s\n",
				m3_ipc->ops->request_wake_src(m3_ipc));
		}
	}

	if (suspend_state == PM_SUSPEND_MEM && pm_ops->check_off_mode_enable())
		pm_ops->prepare_rtc_resume();

	return ret;
}

static int am33xx_pm_enter(suspend_state_t suspend_state)
{
	int ret = 0;

	switch (suspend_state) {
	case PM_SUSPEND_MEM:
	case PM_SUSPEND_STANDBY:
		ret = am33xx_pm_suspend(suspend_state);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int am33xx_pm_begin(suspend_state_t state)
{
	int ret = -EINVAL;
	struct nvmem_device *nvmem;

	if (state == PM_SUSPEND_MEM && pm_ops->check_off_mode_enable()) {
		nvmem = devm_nvmem_device_get(&omap_rtc->dev,
					      "omap_rtc_scratch0");
		if (!IS_ERR(nvmem))
			nvmem_device_write(nvmem, RTC_SCRATCH_MAGIC_REG * 4, 4,
					   (void *)&rtc_magic_val);
		rtc_only_idle = 1;
	} else {
		rtc_only_idle = 0;
	}

	pm_ops->begin_suspend();

	switch (state) {
	case PM_SUSPEND_MEM:
		ret = m3_ipc->ops->prepare_low_power(m3_ipc, WKUP_M3_DEEPSLEEP);
		break;
	case PM_SUSPEND_STANDBY:
		ret = m3_ipc->ops->prepare_low_power(m3_ipc, WKUP_M3_STANDBY);
		break;
	}

	return ret;
}

static void am33xx_pm_end(void)
{
	u32 val = 0;
	struct nvmem_device *nvmem;

	nvmem = devm_nvmem_device_get(&omap_rtc->dev, "omap_rtc_scratch0");
	if (IS_ERR(nvmem))
		return;

	m3_ipc->ops->finish_low_power(m3_ipc);
	if (rtc_only_idle) {
		if (retrigger_irq) {
			/*
			 * 32 bits of Interrupt Set-Pending correspond to 32
			 * 32 interrupts. Compute the bit offset of the
			 * Interrupt and set that particular bit
			 * Compute the register offset by dividing interrupt
			 * number by 32 and mutiplying by 4
			 */
			writel_relaxed(1 << (retrigger_irq & 31),
				       gic_dist_base + GIC_INT_SET_PENDING_BASE
				       + retrigger_irq / 32 * 4);
		}

		nvmem_device_write(nvmem, RTC_SCRATCH_MAGIC_REG * 4, 4,
				   (void *)&val);
	}

	rtc_only_idle = 0;

	pm_ops->finish_suspend();
}

static int am33xx_pm_valid(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_STANDBY:
	case PM_SUSPEND_MEM:
		return 1;
	default:
		return 0;
	}
}

static const struct platform_suspend_ops am33xx_pm_ops = {
	.begin		= am33xx_pm_begin,
	.end		= am33xx_pm_end,
	.enter		= am33xx_pm_enter,
	.valid		= am33xx_pm_valid,
};
#endif /* CONFIG_SUSPEND */

static void am33xx_pm_set_ipc_ops(void)
{
	u32 resume_address;
	int temp;

	temp = ti_emif_get_mem_type();
	if (temp < 0) {
		dev_err(pm33xx_dev, "PM: Cannot determine memory type, no PM available\n");
		return;
	}
	m3_ipc->ops->set_mem_type(m3_ipc, temp);

	/* Physical resume address to be used by ROM code */
	resume_address = am33xx_do_wfi_sram_phys +
			 *pm_sram->resume_offset + 0x4;

	m3_ipc->ops->set_resume_address(m3_ipc, (void *)resume_address);
}

static void am33xx_pm_free_sram(void)
{
	gen_pool_free(sram_pool, ocmcram_location, *pm_sram->do_wfi_sz);
	gen_pool_free(sram_pool_data, ocmcram_location_data,
		      sizeof(struct am33xx_pm_ro_sram_data));
}

/*
 * Push the minimal suspend-resume code to SRAM
 */
static int am33xx_pm_alloc_sram(void)
{
	struct device_node *np;
	int ret = 0;

	np = of_find_compatible_node(NULL, NULL, "ti,omap3-mpu");
	if (!np) {
		np = of_find_compatible_node(NULL, NULL, "ti,omap4-mpu");
		if (!np) {
			dev_err(pm33xx_dev, "PM: %s: Unable to find device node for mpu\n",
				__func__);
			return -ENODEV;
		}
	}

	sram_pool = of_gen_pool_get(np, "pm-sram", 0);
	if (!sram_pool) {
		dev_err(pm33xx_dev, "PM: %s: Unable to get sram pool for ocmcram\n",
			__func__);
		ret = -ENODEV;
		goto mpu_put_node;
	}

	sram_pool_data = of_gen_pool_get(np, "pm-sram", 1);
	if (!sram_pool_data) {
		dev_err(pm33xx_dev, "PM: %s: Unable to get sram data pool for ocmcram\n",
			__func__);
		ret = -ENODEV;
		goto mpu_put_node;
	}

	ocmcram_location = gen_pool_alloc(sram_pool, *pm_sram->do_wfi_sz);
	if (!ocmcram_location) {
		dev_err(pm33xx_dev, "PM: %s: Unable to allocate memory from ocmcram\n",
			__func__);
		ret = -ENOMEM;
		goto mpu_put_node;
	}

	ocmcram_location_data = gen_pool_alloc(sram_pool_data,
					       sizeof(struct emif_regs_amx3));
	if (!ocmcram_location_data) {
		dev_err(pm33xx_dev, "PM: Unable to allocate memory from ocmcram\n");
		gen_pool_free(sram_pool, ocmcram_location, *pm_sram->do_wfi_sz);
		ret = -ENOMEM;
	}

mpu_put_node:
	of_node_put(np);
	return ret;
}

static int am33xx_pm_rtc_setup(void)
{
	struct device_node *np;
	unsigned long val = 0;
	struct nvmem_device *nvmem;

	np = of_find_node_by_name(NULL, "rtc");

	if (of_device_is_available(np)) {
		omap_rtc = rtc_class_open("rtc0");
		if (!omap_rtc) {
			pr_warn("PM: rtc0 not available");
			return -EPROBE_DEFER;
		}

		nvmem = devm_nvmem_device_get(&omap_rtc->dev,
					      "omap_rtc_scratch0");
		if (!IS_ERR(nvmem)) {
			nvmem_device_read(nvmem, RTC_SCRATCH_MAGIC_REG * 4,
					  4, (void *)&rtc_magic_val);
			if ((rtc_magic_val & 0xffff) != RTC_REG_BOOT_MAGIC)
				pr_warn("PM: bootloader does not support rtc-only!\n");

			nvmem_device_write(nvmem, RTC_SCRATCH_MAGIC_REG * 4,
					   4, (void *)&val);
			val = pm_sram->resume_address;
			nvmem_device_write(nvmem, RTC_SCRATCH_RESUME_REG * 4,
					   4, (void *)&val);
		}
	} else {
		pr_warn("PM: no-rtc available, rtc-only mode disabled.\n");
	}

	return 0;
}

static int am33xx_pm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;

	if (!of_machine_is_compatible("ti,am33xx") &&
	    !of_machine_is_compatible("ti,am43"))
		return -ENODEV;

	pm_ops = dev->platform_data;
	if (!pm_ops) {
		dev_err(dev, "PM: Cannot get core PM ops!\n");
		return -ENODEV;
	}

	ret = am43xx_map_gic();
	if (ret) {
		pr_err("PM: Could not ioremap GIC base\n");
		return ret;
	}

	pm_sram = pm_ops->get_sram_addrs();
	if (!pm_sram) {
		dev_err(dev, "PM: Cannot get PM asm function addresses!!\n");
		return -ENODEV;
	}

	m3_ipc = wkup_m3_ipc_get();
	if (!m3_ipc) {
		pr_err("PM: Cannot get wkup_m3_ipc handle\n");
		return -EPROBE_DEFER;
	}

	pm33xx_dev = dev;

	ret = am33xx_pm_alloc_sram();
	if (ret)
		return ret;

	ret = am33xx_pm_rtc_setup();
	if (ret)
		goto err_free_sram;

	ret = am33xx_push_sram_idle();
	if (ret)
		goto err_free_sram;

	am33xx_pm_set_ipc_ops();

#ifdef CONFIG_SUSPEND
	suspend_set_ops(&am33xx_pm_ops);

	/*
	 * For a system suspend we must flush the caches, we want
	 * the DDR in self-refresh, we want to save the context
	 * of the EMIF, and we want the wkup_m3 to handle low-power
	 * transition.
	 */
	suspend_wfi_flags |= WFI_FLAG_FLUSH_CACHE;
	suspend_wfi_flags |= WFI_FLAG_SELF_REFRESH;
	suspend_wfi_flags |= WFI_FLAG_SAVE_EMIF;
	suspend_wfi_flags |= WFI_FLAG_WAKE_M3;
#endif /* CONFIG_SUSPEND */

	ret = pm_ops->init(am33xx_do_sram_idle);
	if (ret) {
		dev_err(dev, "Unable to call core pm init!\n");
		ret = -ENODEV;
		goto err_put_wkup_m3_ipc;
	}

	return 0;

err_put_wkup_m3_ipc:
	wkup_m3_ipc_put(m3_ipc);
err_free_sram:
	am33xx_pm_free_sram();
	pm33xx_dev = NULL;
	return ret;
}

static int am33xx_pm_remove(struct platform_device *pdev)
{
	if (pm_ops->deinit)
		pm_ops->deinit();
	suspend_set_ops(NULL);
	wkup_m3_ipc_put(m3_ipc);
	am33xx_pm_free_sram();
	return 0;
}

static struct platform_driver am33xx_pm_driver = {
	.driver = {
		.name   = "pm33xx",
	},
	.probe = am33xx_pm_probe,
	.remove = am33xx_pm_remove,
};
module_platform_driver(am33xx_pm_driver);

MODULE_ALIAS("platform:pm33xx");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("am33xx power management driver");
