// SPDX-License-Identifier: GPL-2.0
/*
 * AMD Promontory 21 xHCI Hwmon Implementation
 * (only temperature monitoring is supported)
 *
 * This can be effectively used as the alternative chipset temperature monitor.
 *
 * Copyright (C) 2026 Jihong Min <hurryman2212@gmail.com>
 */

#include <linux/auxiliary_bus.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/hwmon.h>
#include <linux/io.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_data/usb-xhci-prom21.h>
#include <linux/pm_runtime.h>

#define PROM21_XHCI_INDEX_OFFSET	0x3000
#define PROM21_XHCI_DATA_OFFSET		0x3008
#define PROM21_XHCI_TEMP_SELECTOR	0x0001e520

struct prom21_xhci {
	struct pci_dev *pdev;
	struct device *hwmon_dev;
	void __iomem *regs;
};

static int prom21_xhci_pm_get(struct prom21_xhci *hwmon)
{
	struct device *dev = &hwmon->pdev->dev;
	int ret;

	/*
	 * PROM21 temperature register access does not return a valid value while
	 * the parent xHCI PCI function is suspended. Do not wake the device from
	 * a hwmon read. On success, hold a usage reference without changing the
	 * runtime PM state; if runtime PM is disabled, allow the read unless the
	 * device is still marked suspended.
	 */
	ret = pm_runtime_get_if_active(dev);
	if (ret > 0)
		return 0;

	if (ret == -EINVAL) {
		if (pm_runtime_status_suspended(dev))
			return -ENODATA;

		pm_runtime_get_noresume(dev);
		return 0;
	}

	if (!ret)
		return -ENODATA;

	return ret;
}

/*
 * This is not a pure MMIO read. The PROM21 vendor data register is selected
 * by temporarily writing PROM21_XHCI_TEMP_SELECTOR to the vendor index
 * register.
 * The hwmon core already serializes this driver's callbacks, so this driver
 * does not need an additional private lock. That does not synchronize with
 * firmware, SMM, ACPI, or other possible users. Keep the sequence short and
 * restore the previous index before returning.
 */
static int prom21_xhci_read_temp_raw_restore_index(struct prom21_xhci *hwmon,
						   u8 *raw)
{
	struct device *dev = &hwmon->pdev->dev;
	u32 index;
	u8 data;
	int ret;

	ret = prom21_xhci_pm_get(hwmon);
	if (ret)
		return ret;

	index = readl(hwmon->regs + PROM21_XHCI_INDEX_OFFSET);
	/* Select the PROM21 temperature register through the vendor index. */
	writel(PROM21_XHCI_TEMP_SELECTOR,
	       hwmon->regs + PROM21_XHCI_INDEX_OFFSET);
	/* Use a 32-bit read for PCI MMIO register access. */
	data = readl(hwmon->regs + PROM21_XHCI_DATA_OFFSET) & 0xff;
	/* Restore the previous vendor index register value. */
	writel(index, hwmon->regs + PROM21_XHCI_INDEX_OFFSET);
	readl(hwmon->regs + PROM21_XHCI_INDEX_OFFSET);

	/*
	 * Drop the usage reference taken by prom21_xhci_pm_get(). This is
	 * enough because the read path never resumes the device; use the normal
	 * put path so the PM core can re-evaluate idle state after the read.
	 * Otherwise, a racing xHCI autosuspend attempt can see a nonzero
	 * runtime PM usage count and skip autosuspend, and a later
	 * pm_runtime_put_noidle(), which does not check for an idle device,
	 * would leave the device active.
	 */
	pm_runtime_put(dev);

	if (!data)
		return -ENODATA;

	*raw = data;
	return 0;
}

static long prom21_xhci_raw_to_millicelsius(u8 raw)
{
	/*
	 * No public AMD reference is available for this value.
	 * The scale was derived from observed PROM21 xHCI temperature readings:
	 *  temp[C] = raw * 0.9066 - 78.624
	 */
	return DIV_ROUND_CLOSEST(raw * 9066, 10) - 78624;
}

static umode_t prom21_xhci_is_visible(const void *drvdata,
				      enum hwmon_sensor_types type, u32 attr,
				      int channel)
{
	if (type != hwmon_temp)
		return 0;

	switch (attr) {
	case hwmon_temp_input:
		return 0444;
	default:
		return 0;
	}
}

static int prom21_xhci_read(struct device *dev, enum hwmon_sensor_types type,
			    u32 attr, int channel, long *val)
{
	struct prom21_xhci *hwmon = dev_get_drvdata(dev);
	u8 raw;
	int ret;

	if (type != hwmon_temp || attr != hwmon_temp_input)
		return -EOPNOTSUPP;

	ret = prom21_xhci_read_temp_raw_restore_index(hwmon, &raw);
	if (ret)
		return ret;

	*val = prom21_xhci_raw_to_millicelsius(raw);
	return 0;
}

static const struct hwmon_ops prom21_xhci_ops = {
	.is_visible = prom21_xhci_is_visible,
	.read = prom21_xhci_read,
};

static const struct hwmon_channel_info *const prom21_xhci_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT),
	NULL,
};

static const struct hwmon_chip_info prom21_xhci_chip_info = {
	.ops = &prom21_xhci_ops,
	.info = prom21_xhci_info,
};

static int prom21_xhci_probe(struct auxiliary_device *auxdev,
			     const struct auxiliary_device_id *id)
{
	struct device *dev = &auxdev->dev;
	const struct prom21_xhci_pdata *pdata = dev_get_platdata(dev);
	struct prom21_xhci *hwmon;

	if (!pdata)
		return dev_err_probe(dev, -ENODEV,
				     "platform data unavailable\n");

	if (!pdata->regs ||
	    pdata->rsrc_len < PROM21_XHCI_DATA_OFFSET + sizeof(u32))
		return dev_err_probe(dev, -ENODEV, "invalid MMIO resource\n");

	hwmon = devm_kzalloc(dev, sizeof(*hwmon), GFP_KERNEL);
	if (!hwmon)
		return -ENOMEM;

	hwmon->pdev = pdata->pdev;
	hwmon->regs = pdata->regs;
	auxiliary_set_drvdata(auxdev, hwmon);

	/*
	 * Parent the hwmon device to the PCI function because the temperature
	 * value is read from that function's MMIO BAR, and systems may contain
	 * multiple PROM21 xHCI functions. This lets userspace identify the PCI
	 * endpoint for each reading. The auxiliary driver still owns the hwmon
	 * lifetime and unregisters it before HCD teardown.
	 */
	hwmon->hwmon_dev =
		hwmon_device_register_with_info(&pdata->pdev->dev, "prom21_xhci",
						hwmon, &prom21_xhci_chip_info,
						NULL);
	if (IS_ERR(hwmon->hwmon_dev))
		return PTR_ERR(hwmon->hwmon_dev);

	return 0;
}

static void prom21_xhci_remove(struct auxiliary_device *auxdev)
{
	struct prom21_xhci *hwmon = auxiliary_get_drvdata(auxdev);

	/*
	 * The PROM21 PCI glue destroys the auxiliary device before HCD teardown.
	 * Unregister the hwmon device here so sysfs removes the attributes,
	 * stops new reads, and drains active hwmon callbacks before the xHCI
	 * MMIO mapping is released.
	 */
	hwmon_device_unregister(hwmon->hwmon_dev);
}

static const struct auxiliary_device_id prom21_xhci_id_table[] = {
	{ .name = "xhci_pci_prom21.hwmon" },
	{}
};
MODULE_DEVICE_TABLE(auxiliary, prom21_xhci_id_table);

static struct auxiliary_driver prom21_xhci_driver = {
	.name = "prom21-xhci",
	.probe = prom21_xhci_probe,
	.remove = prom21_xhci_remove,
	.id_table = prom21_xhci_id_table,
};
module_auxiliary_driver(prom21_xhci_driver);

MODULE_AUTHOR("Jihong Min <hurryman2212@gmail.com>");
MODULE_DESCRIPTION("AMD Promontory 21 xHCI temperature sensor driver");
MODULE_LICENSE("GPL");
