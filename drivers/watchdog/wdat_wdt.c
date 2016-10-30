/*
 * ACPI Hardware Watchdog (WDAT) driver.
 *
 * Copyright (C) 2016, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/watchdog.h>

#define MAX_WDAT_ACTIONS ACPI_WDAT_ACTION_RESERVED

/**
 * struct wdat_instruction - Single ACPI WDAT instruction
 * @entry: Copy of the ACPI table instruction
 * @reg: Register the instruction is accessing
 * @node: Next instruction in action sequence
 */
struct wdat_instruction {
	struct acpi_wdat_entry entry;
	void __iomem *reg;
	struct list_head node;
};

/**
 * struct wdat_wdt - ACPI WDAT watchdog device
 * @pdev: Parent platform device
 * @wdd: Watchdog core device
 * @period: How long is one watchdog period in ms
 * @stopped_in_sleep: Is this watchdog stopped by the firmware in S1-S5
 * @stopped: Was the watchdog stopped by the driver in suspend
 * @actions: An array of instruction lists indexed by an action number from
 *           the WDAT table. There can be %NULL entries for not implemented
 *           actions.
 */
struct wdat_wdt {
	struct platform_device *pdev;
	struct watchdog_device wdd;
	unsigned int period;
	bool stopped_in_sleep;
	bool stopped;
	struct list_head *instructions[MAX_WDAT_ACTIONS];
};

#define to_wdat_wdt(wdd) container_of(wdd, struct wdat_wdt, wdd)

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static int wdat_wdt_read(struct wdat_wdt *wdat,
	 const struct wdat_instruction *instr, u32 *value)
{
	const struct acpi_generic_address *gas = &instr->entry.register_region;

	switch (gas->access_width) {
	case 1:
		*value = ioread8(instr->reg);
		break;
	case 2:
		*value = ioread16(instr->reg);
		break;
	case 3:
		*value = ioread32(instr->reg);
		break;
	default:
		return -EINVAL;
	}

	dev_dbg(&wdat->pdev->dev, "Read %#x from 0x%08llx\n", *value,
		gas->address);

	return 0;
}

static int wdat_wdt_write(struct wdat_wdt *wdat,
	const struct wdat_instruction *instr, u32 value)
{
	const struct acpi_generic_address *gas = &instr->entry.register_region;

	switch (gas->access_width) {
	case 1:
		iowrite8((u8)value, instr->reg);
		break;
	case 2:
		iowrite16((u16)value, instr->reg);
		break;
	case 3:
		iowrite32(value, instr->reg);
		break;
	default:
		return -EINVAL;
	}

	dev_dbg(&wdat->pdev->dev, "Wrote %#x to 0x%08llx\n", value,
		gas->address);

	return 0;
}

static int wdat_wdt_run_action(struct wdat_wdt *wdat, unsigned int action,
			       u32 param, u32 *retval)
{
	struct wdat_instruction *instr;

	if (action >= ARRAY_SIZE(wdat->instructions))
		return -EINVAL;

	if (!wdat->instructions[action])
		return -EOPNOTSUPP;

	dev_dbg(&wdat->pdev->dev, "Running action %#x\n", action);

	/* Run each instruction sequentially */
	list_for_each_entry(instr, wdat->instructions[action], node) {
		const struct acpi_wdat_entry *entry = &instr->entry;
		const struct acpi_generic_address *gas;
		u32 flags, value, mask, x, y;
		bool preserve;
		int ret;

		gas = &entry->register_region;

		preserve = entry->instruction & ACPI_WDAT_PRESERVE_REGISTER;
		flags = entry->instruction & ~ACPI_WDAT_PRESERVE_REGISTER;
		value = entry->value;
		mask = entry->mask;

		switch (flags) {
		case ACPI_WDAT_READ_VALUE:
			ret = wdat_wdt_read(wdat, instr, &x);
			if (ret)
				return ret;
			x >>= gas->bit_offset;
			x &= mask;
			if (retval)
				*retval = x == value;
			break;

		case ACPI_WDAT_READ_COUNTDOWN:
			ret = wdat_wdt_read(wdat, instr, &x);
			if (ret)
				return ret;
			x >>= gas->bit_offset;
			x &= mask;
			if (retval)
				*retval = x;
			break;

		case ACPI_WDAT_WRITE_VALUE:
			x = value & mask;
			x <<= gas->bit_offset;
			if (preserve) {
				ret = wdat_wdt_read(wdat, instr, &y);
				if (ret)
					return ret;
				y = y & ~(mask << gas->bit_offset);
				x |= y;
			}
			ret = wdat_wdt_write(wdat, instr, x);
			if (ret)
				return ret;
			break;

		case ACPI_WDAT_WRITE_COUNTDOWN:
			x = param;
			x &= mask;
			x <<= gas->bit_offset;
			if (preserve) {
				ret = wdat_wdt_read(wdat, instr, &y);
				if (ret)
					return ret;
				y = y & ~(mask << gas->bit_offset);
				x |= y;
			}
			ret = wdat_wdt_write(wdat, instr, x);
			if (ret)
				return ret;
			break;

		default:
			dev_err(&wdat->pdev->dev, "Unknown instruction: %u\n",
				flags);
			return -EINVAL;
		}
	}

	return 0;
}

static int wdat_wdt_enable_reboot(struct wdat_wdt *wdat)
{
	int ret;

	/*
	 * WDAT specification says that the watchdog is required to reboot
	 * the system when it fires. However, it also states that it is
	 * recommeded to make it configurable through hardware register. We
	 * enable reboot now if it is configrable, just in case.
	 */
	ret = wdat_wdt_run_action(wdat, ACPI_WDAT_SET_REBOOT, 0, NULL);
	if (ret && ret != -EOPNOTSUPP) {
		dev_err(&wdat->pdev->dev,
			"Failed to enable reboot when watchdog triggers\n");
		return ret;
	}

	return 0;
}

static void wdat_wdt_boot_status(struct wdat_wdt *wdat)
{
	u32 boot_status = 0;
	int ret;

	ret = wdat_wdt_run_action(wdat, ACPI_WDAT_GET_STATUS, 0, &boot_status);
	if (ret && ret != -EOPNOTSUPP) {
		dev_err(&wdat->pdev->dev, "Failed to read boot status\n");
		return;
	}

	if (boot_status)
		wdat->wdd.bootstatus = WDIOF_CARDRESET;

	/* Clear the boot status in case BIOS did not do it */
	ret = wdat_wdt_run_action(wdat, ACPI_WDAT_SET_STATUS, 0, NULL);
	if (ret && ret != -EOPNOTSUPP)
		dev_err(&wdat->pdev->dev, "Failed to clear boot status\n");
}

static void wdat_wdt_set_running(struct wdat_wdt *wdat)
{
	u32 running = 0;
	int ret;

	ret = wdat_wdt_run_action(wdat, ACPI_WDAT_GET_RUNNING_STATE, 0,
				  &running);
	if (ret && ret != -EOPNOTSUPP)
		dev_err(&wdat->pdev->dev, "Failed to read running state\n");

	if (running)
		set_bit(WDOG_HW_RUNNING, &wdat->wdd.status);
}

static int wdat_wdt_start(struct watchdog_device *wdd)
{
	return wdat_wdt_run_action(to_wdat_wdt(wdd),
				   ACPI_WDAT_SET_RUNNING_STATE, 0, NULL);
}

static int wdat_wdt_stop(struct watchdog_device *wdd)
{
	return wdat_wdt_run_action(to_wdat_wdt(wdd),
				   ACPI_WDAT_SET_STOPPED_STATE, 0, NULL);
}

static int wdat_wdt_ping(struct watchdog_device *wdd)
{
	return wdat_wdt_run_action(to_wdat_wdt(wdd), ACPI_WDAT_RESET, 0, NULL);
}

static int wdat_wdt_set_timeout(struct watchdog_device *wdd,
				unsigned int timeout)
{
	struct wdat_wdt *wdat = to_wdat_wdt(wdd);
	unsigned int periods;
	int ret;

	periods = timeout * 1000 / wdat->period;
	ret = wdat_wdt_run_action(wdat, ACPI_WDAT_SET_COUNTDOWN, periods, NULL);
	if (!ret)
		wdd->timeout = timeout;
	return ret;
}

static unsigned int wdat_wdt_get_timeleft(struct watchdog_device *wdd)
{
	struct wdat_wdt *wdat = to_wdat_wdt(wdd);
	u32 periods = 0;

	wdat_wdt_run_action(wdat, ACPI_WDAT_GET_COUNTDOWN, 0, &periods);
	return periods * wdat->period / 1000;
}

static const struct watchdog_info wdat_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.firmware_version = 0,
	.identity = "wdat_wdt",
};

static const struct watchdog_ops wdat_wdt_ops = {
	.owner = THIS_MODULE,
	.start = wdat_wdt_start,
	.stop = wdat_wdt_stop,
	.ping = wdat_wdt_ping,
	.set_timeout = wdat_wdt_set_timeout,
	.get_timeleft = wdat_wdt_get_timeleft,
};

static int wdat_wdt_probe(struct platform_device *pdev)
{
	const struct acpi_wdat_entry *entries;
	const struct acpi_table_wdat *tbl;
	struct wdat_wdt *wdat;
	struct resource *res;
	void __iomem **regs;
	acpi_status status;
	int i, ret;

	status = acpi_get_table(ACPI_SIG_WDAT, 0,
				(struct acpi_table_header **)&tbl);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	wdat = devm_kzalloc(&pdev->dev, sizeof(*wdat), GFP_KERNEL);
	if (!wdat)
		return -ENOMEM;

	regs = devm_kcalloc(&pdev->dev, pdev->num_resources, sizeof(*regs),
			    GFP_KERNEL);
	if (!regs)
		return -ENOMEM;

	/* WDAT specification wants to have >= 1ms period */
	if (tbl->timer_period < 1)
		return -EINVAL;
	if (tbl->min_count > tbl->max_count)
		return -EINVAL;

	wdat->period = tbl->timer_period;
	wdat->wdd.min_hw_heartbeat_ms = wdat->period * tbl->min_count;
	wdat->wdd.max_hw_heartbeat_ms = wdat->period * tbl->max_count;
	wdat->stopped_in_sleep = tbl->flags & ACPI_WDAT_STOPPED;
	wdat->wdd.info = &wdat_wdt_info;
	wdat->wdd.ops = &wdat_wdt_ops;
	wdat->pdev = pdev;

	/* Request and map all resources */
	for (i = 0; i < pdev->num_resources; i++) {
		void __iomem *reg;

		res = &pdev->resource[i];
		if (resource_type(res) == IORESOURCE_MEM) {
			reg = devm_ioremap_resource(&pdev->dev, res);
			if (IS_ERR(reg))
				return PTR_ERR(reg);
		} else if (resource_type(res) == IORESOURCE_IO) {
			reg = devm_ioport_map(&pdev->dev, res->start, 1);
			if (!reg)
				return -ENOMEM;
		} else {
			dev_err(&pdev->dev, "Unsupported resource\n");
			return -EINVAL;
		}

		regs[i] = reg;
	}

	entries = (struct acpi_wdat_entry *)(tbl + 1);
	for (i = 0; i < tbl->entries; i++) {
		const struct acpi_generic_address *gas;
		struct wdat_instruction *instr;
		struct list_head *instructions;
		unsigned int action;
		struct resource r;
		int j;

		action = entries[i].action;
		if (action >= MAX_WDAT_ACTIONS) {
			dev_dbg(&pdev->dev, "Skipping unknown action: %u\n",
				action);
			continue;
		}

		instr = devm_kzalloc(&pdev->dev, sizeof(*instr), GFP_KERNEL);
		if (!instr)
			return -ENOMEM;

		INIT_LIST_HEAD(&instr->node);
		instr->entry = entries[i];

		gas = &entries[i].register_region;

		memset(&r, 0, sizeof(r));
		r.start = gas->address;
		r.end = r.start + gas->access_width;
		if (gas->space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY) {
			r.flags = IORESOURCE_MEM;
		} else if (gas->space_id == ACPI_ADR_SPACE_SYSTEM_IO) {
			r.flags = IORESOURCE_IO;
		} else {
			dev_dbg(&pdev->dev, "Unsupported address space: %d\n",
				gas->space_id);
			continue;
		}

		/* Find the matching resource */
		for (j = 0; j < pdev->num_resources; j++) {
			res = &pdev->resource[j];
			if (resource_contains(res, &r)) {
				instr->reg = regs[j] + r.start - res->start;
				break;
			}
		}

		if (!instr->reg) {
			dev_err(&pdev->dev, "I/O resource not found\n");
			return -EINVAL;
		}

		instructions = wdat->instructions[action];
		if (!instructions) {
			instructions = devm_kzalloc(&pdev->dev,
					sizeof(*instructions), GFP_KERNEL);
			if (!instructions)
				return -ENOMEM;

			INIT_LIST_HEAD(instructions);
			wdat->instructions[action] = instructions;
		}

		list_add_tail(&instr->node, instructions);
	}

	wdat_wdt_boot_status(wdat);
	wdat_wdt_set_running(wdat);

	ret = wdat_wdt_enable_reboot(wdat);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, wdat);

	watchdog_set_nowayout(&wdat->wdd, nowayout);
	return devm_watchdog_register_device(&pdev->dev, &wdat->wdd);
}

#ifdef CONFIG_PM_SLEEP
static int wdat_wdt_suspend_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct wdat_wdt *wdat = platform_get_drvdata(pdev);
	int ret;

	if (!watchdog_active(&wdat->wdd))
		return 0;

	/*
	 * We need to stop the watchdog if firmare is not doing it or if we
	 * are going suspend to idle (where firmware is not involved). If
	 * firmware is stopping the watchdog we kick it here one more time
	 * to give it some time.
	 */
	wdat->stopped = false;
	if (acpi_target_system_state() == ACPI_STATE_S0 ||
	    !wdat->stopped_in_sleep) {
		ret = wdat_wdt_stop(&wdat->wdd);
		if (!ret)
			wdat->stopped = true;
	} else {
		ret = wdat_wdt_ping(&wdat->wdd);
	}

	return ret;
}

static int wdat_wdt_resume_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct wdat_wdt *wdat = platform_get_drvdata(pdev);
	int ret;

	if (!watchdog_active(&wdat->wdd))
		return 0;

	if (!wdat->stopped) {
		/*
		 * Looks like the boot firmware reinitializes the watchdog
		 * before it hands off to the OS on resume from sleep so we
		 * stop and reprogram the watchdog here.
		 */
		ret = wdat_wdt_stop(&wdat->wdd);
		if (ret)
			return ret;

		ret = wdat_wdt_set_timeout(&wdat->wdd, wdat->wdd.timeout);
		if (ret)
			return ret;

		ret = wdat_wdt_enable_reboot(wdat);
		if (ret)
			return ret;

		ret = wdat_wdt_ping(&wdat->wdd);
		if (ret)
			return ret;
	}

	return wdat_wdt_start(&wdat->wdd);
}
#endif

static const struct dev_pm_ops wdat_wdt_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(wdat_wdt_suspend_noirq,
				      wdat_wdt_resume_noirq)
};

static struct platform_driver wdat_wdt_driver = {
	.probe = wdat_wdt_probe,
	.driver = {
		.name = "wdat_wdt",
		.pm = &wdat_wdt_pm_ops,
	},
};

module_platform_driver(wdat_wdt_driver);

MODULE_AUTHOR("Mika Westerberg <mika.westerberg@linux.intel.com>");
MODULE_DESCRIPTION("ACPI Hardware Watchdog (WDAT) driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:wdat_wdt");
