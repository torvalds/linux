// SPDX-License-Identifier: GPL-2.0-only
/*
 * pm8xxx RTC driver
 *
 * Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 * Copyright (c) 2023, Linaro Limited
 */
#include <linux/efi.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/init.h>
#include <linux/rtc.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_wakeirq.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/unaligned.h>

#include <asm/byteorder.h>

/* RTC_CTRL register bit fields */
#define PM8xxx_RTC_ENABLE		BIT(7)
#define PM8xxx_RTC_ALARM_CLEAR		BIT(0)
#define PM8xxx_RTC_ALARM_ENABLE		BIT(7)

#define NUM_8_BIT_RTC_REGS		0x4

/**
 * struct pm8xxx_rtc_regs - describe RTC registers per PMIC versions
 * @ctrl:		address of control register
 * @write:		base address of write registers
 * @read:		base address of read registers
 * @alarm_ctrl:		address of alarm control register
 * @alarm_ctrl2:	address of alarm control2 register
 * @alarm_rw:		base address of alarm read-write registers
 * @alarm_en:		alarm enable mask
 */
struct pm8xxx_rtc_regs {
	unsigned int ctrl;
	unsigned int write;
	unsigned int read;
	unsigned int alarm_ctrl;
	unsigned int alarm_ctrl2;
	unsigned int alarm_rw;
	unsigned int alarm_en;
};

struct qcom_uefi_rtc_info {
	__le32	offset_gps;
	u8	reserved[8];
} __packed;

/**
 * struct pm8xxx_rtc -  RTC driver internal structure
 * @rtc:		RTC device
 * @regmap:		regmap used to access registers
 * @allow_set_time:	whether the time can be set
 * @use_uefi:		use UEFI variable as fallback for offset
 * @alarm_irq:		alarm irq number
 * @regs:		register description
 * @dev:		device structure
 * @rtc_info:		qcom uefi rtc-info structure
 * @nvmem_cell:		nvmem cell for offset
 * @offset:		offset from epoch in seconds
 * @offset_dirty:	offset needs to be stored on shutdown
 */
struct pm8xxx_rtc {
	struct rtc_device *rtc;
	struct regmap *regmap;
	bool allow_set_time;
	bool use_uefi;
	int alarm_irq;
	const struct pm8xxx_rtc_regs *regs;
	struct device *dev;
	struct qcom_uefi_rtc_info rtc_info;
	struct nvmem_cell *nvmem_cell;
	u32 offset;
	bool offset_dirty;
};

#ifdef CONFIG_EFI

MODULE_IMPORT_NS("EFIVAR");

#define QCOM_UEFI_NAME	L"RTCInfo"
#define QCOM_UEFI_GUID	EFI_GUID(0x882f8c2b, 0x9646, 0x435f, \
				 0x8d, 0xe5, 0xf2, 0x08, 0xff, 0x80, 0xc1, 0xbd)
#define QCOM_UEFI_ATTRS	(EFI_VARIABLE_NON_VOLATILE | \
			 EFI_VARIABLE_BOOTSERVICE_ACCESS | \
			 EFI_VARIABLE_RUNTIME_ACCESS)

static int pm8xxx_rtc_read_uefi_offset(struct pm8xxx_rtc *rtc_dd)
{
	struct qcom_uefi_rtc_info *rtc_info = &rtc_dd->rtc_info;
	unsigned long size = sizeof(*rtc_info);
	struct device *dev = rtc_dd->dev;
	efi_status_t status;
	u32 offset_gps;
	int rc;

	rc = efivar_lock();
	if (rc)
		return rc;

	status = efivar_get_variable(QCOM_UEFI_NAME, &QCOM_UEFI_GUID, NULL,
				     &size, rtc_info);
	efivar_unlock();

	if (status != EFI_SUCCESS) {
		dev_dbg(dev, "failed to read UEFI offset: %lu\n", status);
		return efi_status_to_err(status);
	}

	if (size != sizeof(*rtc_info)) {
		dev_dbg(dev, "unexpected UEFI structure size %lu\n", size);
		return -EINVAL;
	}

	dev_dbg(dev, "uefi_rtc_info = %*ph\n", (int)size, rtc_info);

	/* Convert from GPS to Unix time offset */
	offset_gps = le32_to_cpu(rtc_info->offset_gps);
	rtc_dd->offset = offset_gps + (u32)RTC_TIMESTAMP_EPOCH_GPS;

	return 0;
}

static int pm8xxx_rtc_write_uefi_offset(struct pm8xxx_rtc *rtc_dd, u32 offset)
{
	struct qcom_uefi_rtc_info *rtc_info = &rtc_dd->rtc_info;
	unsigned long size = sizeof(*rtc_info);
	struct device *dev = rtc_dd->dev;
	efi_status_t status;
	u32 offset_gps;

	/* Convert from Unix to GPS time offset */
	offset_gps = offset - (u32)RTC_TIMESTAMP_EPOCH_GPS;

	rtc_info->offset_gps = cpu_to_le32(offset_gps);

	dev_dbg(dev, "efi_rtc_info = %*ph\n", (int)size, rtc_info);

	status = efivar_set_variable(QCOM_UEFI_NAME, &QCOM_UEFI_GUID,
				     QCOM_UEFI_ATTRS, size, rtc_info);
	if (status != EFI_SUCCESS) {
		dev_dbg(dev, "failed to write UEFI offset: %lx\n", status);
		return efi_status_to_err(status);
	}

	return 0;
}

#else	/* CONFIG_EFI */

static int pm8xxx_rtc_read_uefi_offset(struct pm8xxx_rtc *rtc_dd)
{
	return -ENODEV;
}

static int pm8xxx_rtc_write_uefi_offset(struct pm8xxx_rtc *rtc_dd, u32 offset)
{
	return -ENODEV;
}

#endif	/* CONFIG_EFI */

static int pm8xxx_rtc_read_nvmem_offset(struct pm8xxx_rtc *rtc_dd)
{
	size_t len;
	void *buf;
	int rc;

	buf = nvmem_cell_read(rtc_dd->nvmem_cell, &len);
	if (IS_ERR(buf)) {
		rc = PTR_ERR(buf);
		dev_dbg(rtc_dd->dev, "failed to read nvmem offset: %d\n", rc);
		return rc;
	}

	if (len != sizeof(u32)) {
		dev_dbg(rtc_dd->dev, "unexpected nvmem cell size %zu\n", len);
		kfree(buf);
		return -EINVAL;
	}

	rtc_dd->offset = get_unaligned_le32(buf);

	kfree(buf);

	return 0;
}

static int pm8xxx_rtc_write_nvmem_offset(struct pm8xxx_rtc *rtc_dd, u32 offset)
{
	u8 buf[sizeof(u32)];
	int rc;

	put_unaligned_le32(offset, buf);

	rc = nvmem_cell_write(rtc_dd->nvmem_cell, buf, sizeof(buf));
	if (rc < 0) {
		dev_dbg(rtc_dd->dev, "failed to write nvmem offset: %d\n", rc);
		return rc;
	}

	return 0;
}

static int pm8xxx_rtc_read_raw(struct pm8xxx_rtc *rtc_dd, u32 *secs)
{
	const struct pm8xxx_rtc_regs *regs = rtc_dd->regs;
	u8 value[NUM_8_BIT_RTC_REGS];
	unsigned int reg;
	int rc;

	rc = regmap_bulk_read(rtc_dd->regmap, regs->read, value, sizeof(value));
	if (rc)
		return rc;

	/*
	 * Read the LSB again and check if there has been a carry over.
	 * If there has, redo the read operation.
	 */
	rc = regmap_read(rtc_dd->regmap, regs->read, &reg);
	if (rc < 0)
		return rc;

	if (reg < value[0]) {
		rc = regmap_bulk_read(rtc_dd->regmap, regs->read, value,
				      sizeof(value));
		if (rc)
			return rc;
	}

	*secs = get_unaligned_le32(value);

	return 0;
}

static int pm8xxx_rtc_update_offset(struct pm8xxx_rtc *rtc_dd, u32 secs)
{
	u32 raw_secs;
	u32 offset;
	int rc;

	if (!rtc_dd->nvmem_cell && !rtc_dd->use_uefi)
		return -ENODEV;

	rc = pm8xxx_rtc_read_raw(rtc_dd, &raw_secs);
	if (rc)
		return rc;

	offset = secs - raw_secs;

	if (offset == rtc_dd->offset)
		return 0;

	/*
	 * Reduce flash wear by deferring updates due to clock drift until
	 * shutdown.
	 */
	if (abs_diff(offset, rtc_dd->offset) < 30) {
		rtc_dd->offset_dirty = true;
		goto out;
	}

	if (rtc_dd->nvmem_cell)
		rc = pm8xxx_rtc_write_nvmem_offset(rtc_dd, offset);
	else
		rc = pm8xxx_rtc_write_uefi_offset(rtc_dd, offset);

	if (rc)
		return rc;

	rtc_dd->offset_dirty = false;
out:
	rtc_dd->offset = offset;

	return 0;
}

/*
 * Steps to write the RTC registers.
 * 1. Disable alarm if enabled.
 * 2. Disable rtc if enabled.
 * 3. Write 0x00 to LSB.
 * 4. Write Byte[1], Byte[2], Byte[3] then Byte[0].
 * 5. Enable rtc if disabled in step 2.
 * 6. Enable alarm if disabled in step 1.
 */
static int __pm8xxx_rtc_set_time(struct pm8xxx_rtc *rtc_dd, u32 secs)
{
	const struct pm8xxx_rtc_regs *regs = rtc_dd->regs;
	u8 value[NUM_8_BIT_RTC_REGS];
	bool alarm_enabled;
	int rc;

	put_unaligned_le32(secs, value);

	rc = regmap_update_bits_check(rtc_dd->regmap, regs->alarm_ctrl,
				      regs->alarm_en, 0, &alarm_enabled);
	if (rc)
		return rc;

	/* Disable RTC */
	rc = regmap_update_bits(rtc_dd->regmap, regs->ctrl, PM8xxx_RTC_ENABLE, 0);
	if (rc)
		return rc;

	/* Write 0 to Byte[0] */
	rc = regmap_write(rtc_dd->regmap, regs->write, 0);
	if (rc)
		return rc;

	/* Write Byte[1], Byte[2], Byte[3] */
	rc = regmap_bulk_write(rtc_dd->regmap, regs->write + 1,
			       &value[1], sizeof(value) - 1);
	if (rc)
		return rc;

	/* Write Byte[0] */
	rc = regmap_write(rtc_dd->regmap, regs->write, value[0]);
	if (rc)
		return rc;

	/* Enable RTC */
	rc = regmap_update_bits(rtc_dd->regmap, regs->ctrl, PM8xxx_RTC_ENABLE,
				PM8xxx_RTC_ENABLE);
	if (rc)
		return rc;

	if (alarm_enabled) {
		rc = regmap_update_bits(rtc_dd->regmap, regs->alarm_ctrl,
					regs->alarm_en, regs->alarm_en);
		if (rc)
			return rc;
	}

	return 0;
}

static int pm8xxx_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct pm8xxx_rtc *rtc_dd = dev_get_drvdata(dev);
	u32 secs;
	int rc;

	secs = rtc_tm_to_time64(tm);

	if (rtc_dd->allow_set_time)
		rc = __pm8xxx_rtc_set_time(rtc_dd, secs);
	else
		rc = pm8xxx_rtc_update_offset(rtc_dd, secs);

	if (rc)
		return rc;

	dev_dbg(dev, "set time: %ptRd %ptRt (%u + %u)\n", tm, tm,
			secs - rtc_dd->offset, rtc_dd->offset);
	return 0;
}

static int pm8xxx_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct pm8xxx_rtc *rtc_dd = dev_get_drvdata(dev);
	u32 secs;
	int rc;

	rc = pm8xxx_rtc_read_raw(rtc_dd, &secs);
	if (rc)
		return rc;

	secs += rtc_dd->offset;
	rtc_time64_to_tm(secs, tm);

	dev_dbg(dev, "read time: %ptRd %ptRt (%u + %u)\n", tm, tm,
			secs - rtc_dd->offset, rtc_dd->offset);
	return 0;
}

static int pm8xxx_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct pm8xxx_rtc *rtc_dd = dev_get_drvdata(dev);
	const struct pm8xxx_rtc_regs *regs = rtc_dd->regs;
	u8 value[NUM_8_BIT_RTC_REGS];
	u32 secs;
	int rc;

	secs = rtc_tm_to_time64(&alarm->time);
	secs -= rtc_dd->offset;
	put_unaligned_le32(secs, value);

	rc = regmap_update_bits(rtc_dd->regmap, regs->alarm_ctrl,
				regs->alarm_en, 0);
	if (rc)
		return rc;

	rc = regmap_bulk_write(rtc_dd->regmap, regs->alarm_rw, value,
			       sizeof(value));
	if (rc)
		return rc;

	if (alarm->enabled) {
		rc = regmap_update_bits(rtc_dd->regmap, regs->alarm_ctrl,
					regs->alarm_en, regs->alarm_en);
		if (rc)
			return rc;
	}

	dev_dbg(dev, "set alarm: %ptRd %ptRt\n", &alarm->time, &alarm->time);

	return 0;
}

static int pm8xxx_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct pm8xxx_rtc *rtc_dd = dev_get_drvdata(dev);
	const struct pm8xxx_rtc_regs *regs = rtc_dd->regs;
	u8 value[NUM_8_BIT_RTC_REGS];
	unsigned int ctrl_reg;
	u32 secs;
	int rc;

	rc = regmap_bulk_read(rtc_dd->regmap, regs->alarm_rw, value,
			      sizeof(value));
	if (rc)
		return rc;

	secs = get_unaligned_le32(value);
	secs += rtc_dd->offset;
	rtc_time64_to_tm(secs, &alarm->time);

	rc = regmap_read(rtc_dd->regmap, regs->alarm_ctrl, &ctrl_reg);
	if (rc)
		return rc;

	alarm->enabled = !!(ctrl_reg & PM8xxx_RTC_ALARM_ENABLE);

	dev_dbg(dev, "read alarm: %ptRd %ptRt\n", &alarm->time, &alarm->time);

	return 0;
}

static int pm8xxx_rtc_alarm_irq_enable(struct device *dev, unsigned int enable)
{
	struct pm8xxx_rtc *rtc_dd = dev_get_drvdata(dev);
	const struct pm8xxx_rtc_regs *regs = rtc_dd->regs;
	u8 value[NUM_8_BIT_RTC_REGS] = {0};
	unsigned int val;
	int rc;

	if (enable)
		val = regs->alarm_en;
	else
		val = 0;

	rc = regmap_update_bits(rtc_dd->regmap, regs->alarm_ctrl,
				regs->alarm_en, val);
	if (rc)
		return rc;

	/* Clear alarm register */
	if (!enable) {
		rc = regmap_bulk_write(rtc_dd->regmap, regs->alarm_rw, value,
				       sizeof(value));
		if (rc)
			return rc;
	}

	return 0;
}

static const struct rtc_class_ops pm8xxx_rtc_ops = {
	.read_time	= pm8xxx_rtc_read_time,
	.set_time	= pm8xxx_rtc_set_time,
	.set_alarm	= pm8xxx_rtc_set_alarm,
	.read_alarm	= pm8xxx_rtc_read_alarm,
	.alarm_irq_enable = pm8xxx_rtc_alarm_irq_enable,
};

static irqreturn_t pm8xxx_alarm_trigger(int irq, void *dev_id)
{
	struct pm8xxx_rtc *rtc_dd = dev_id;
	const struct pm8xxx_rtc_regs *regs = rtc_dd->regs;
	int rc;

	rtc_update_irq(rtc_dd->rtc, 1, RTC_IRQF | RTC_AF);

	/* Disable alarm */
	rc = regmap_update_bits(rtc_dd->regmap, regs->alarm_ctrl,
				regs->alarm_en, 0);
	if (rc)
		return IRQ_NONE;

	/* Clear alarm status */
	rc = regmap_update_bits(rtc_dd->regmap, regs->alarm_ctrl2,
				PM8xxx_RTC_ALARM_CLEAR, 0);
	if (rc)
		return IRQ_NONE;

	return IRQ_HANDLED;
}

static int pm8xxx_rtc_enable(struct pm8xxx_rtc *rtc_dd)
{
	const struct pm8xxx_rtc_regs *regs = rtc_dd->regs;

	return regmap_update_bits(rtc_dd->regmap, regs->ctrl, PM8xxx_RTC_ENABLE,
				  PM8xxx_RTC_ENABLE);
}

static const struct pm8xxx_rtc_regs pm8921_regs = {
	.ctrl		= 0x11d,
	.write		= 0x11f,
	.read		= 0x123,
	.alarm_rw	= 0x127,
	.alarm_ctrl	= 0x11d,
	.alarm_ctrl2	= 0x11e,
	.alarm_en	= BIT(1),
};

static const struct pm8xxx_rtc_regs pm8058_regs = {
	.ctrl		= 0x1e8,
	.write		= 0x1ea,
	.read		= 0x1ee,
	.alarm_rw	= 0x1f2,
	.alarm_ctrl	= 0x1e8,
	.alarm_ctrl2	= 0x1e9,
	.alarm_en	= BIT(1),
};

static const struct pm8xxx_rtc_regs pm8941_regs = {
	.ctrl		= 0x6046,
	.write		= 0x6040,
	.read		= 0x6048,
	.alarm_rw	= 0x6140,
	.alarm_ctrl	= 0x6146,
	.alarm_ctrl2	= 0x6148,
	.alarm_en	= BIT(7),
};

static const struct pm8xxx_rtc_regs pmk8350_regs = {
	.ctrl		= 0x6146,
	.write		= 0x6140,
	.read		= 0x6148,
	.alarm_rw	= 0x6240,
	.alarm_ctrl	= 0x6246,
	.alarm_ctrl2	= 0x6248,
	.alarm_en	= BIT(7),
};

static const struct of_device_id pm8xxx_id_table[] = {
	{ .compatible = "qcom,pm8921-rtc", .data = &pm8921_regs },
	{ .compatible = "qcom,pm8058-rtc", .data = &pm8058_regs },
	{ .compatible = "qcom,pm8941-rtc", .data = &pm8941_regs },
	{ .compatible = "qcom,pmk8350-rtc", .data = &pmk8350_regs },
	{ },
};
MODULE_DEVICE_TABLE(of, pm8xxx_id_table);

static int pm8xxx_rtc_probe_offset(struct pm8xxx_rtc *rtc_dd)
{
	int rc;

	rtc_dd->nvmem_cell = devm_nvmem_cell_get(rtc_dd->dev, "offset");
	if (IS_ERR(rtc_dd->nvmem_cell)) {
		rc = PTR_ERR(rtc_dd->nvmem_cell);
		if (rc != -ENOENT)
			return rc;
		rtc_dd->nvmem_cell = NULL;
	} else {
		return pm8xxx_rtc_read_nvmem_offset(rtc_dd);
	}

	/* Use UEFI storage as fallback if available */
	if (efivar_is_available()) {
		rc = pm8xxx_rtc_read_uefi_offset(rtc_dd);
		if (rc == 0)
			rtc_dd->use_uefi = true;
	}

	return 0;
}

static int pm8xxx_rtc_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct pm8xxx_rtc *rtc_dd;
	int rc;

	match = of_match_node(pm8xxx_id_table, pdev->dev.of_node);
	if (!match)
		return -ENXIO;

	rtc_dd = devm_kzalloc(&pdev->dev, sizeof(*rtc_dd), GFP_KERNEL);
	if (rtc_dd == NULL)
		return -ENOMEM;

	rtc_dd->regs = match->data;
	rtc_dd->dev = &pdev->dev;

	rtc_dd->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!rtc_dd->regmap)
		return -ENXIO;

	if (!of_property_read_bool(pdev->dev.of_node, "qcom,no-alarm")) {
		rtc_dd->alarm_irq = platform_get_irq(pdev, 0);
		if (rtc_dd->alarm_irq < 0)
			return -ENXIO;
	}

	rtc_dd->allow_set_time = of_property_read_bool(pdev->dev.of_node,
						      "allow-set-time");
	if (!rtc_dd->allow_set_time) {
		rc = pm8xxx_rtc_probe_offset(rtc_dd);
		if (rc)
			return rc;
	}

	rc = pm8xxx_rtc_enable(rtc_dd);
	if (rc)
		return rc;

	platform_set_drvdata(pdev, rtc_dd);

	rtc_dd->rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtc_dd->rtc))
		return PTR_ERR(rtc_dd->rtc);

	rtc_dd->rtc->ops = &pm8xxx_rtc_ops;
	rtc_dd->rtc->range_max = U32_MAX;

	if (rtc_dd->alarm_irq) {
		rc = devm_request_any_context_irq(&pdev->dev, rtc_dd->alarm_irq,
						  pm8xxx_alarm_trigger,
						  IRQF_TRIGGER_RISING,
						  "pm8xxx_rtc_alarm", rtc_dd);
		if (rc < 0)
			return rc;

		rc = devm_pm_set_wake_irq(&pdev->dev, rtc_dd->alarm_irq);
		if (rc)
			return rc;

		devm_device_init_wakeup(&pdev->dev);
	} else {
		clear_bit(RTC_FEATURE_ALARM, rtc_dd->rtc->features);
	}

	return devm_rtc_register_device(rtc_dd->rtc);
}

static void pm8xxx_shutdown(struct platform_device *pdev)
{
	struct pm8xxx_rtc *rtc_dd = platform_get_drvdata(pdev);

	if (rtc_dd->offset_dirty) {
		if (rtc_dd->nvmem_cell)
			pm8xxx_rtc_write_nvmem_offset(rtc_dd, rtc_dd->offset);
		else
			pm8xxx_rtc_write_uefi_offset(rtc_dd, rtc_dd->offset);
	}
}

static struct platform_driver pm8xxx_rtc_driver = {
	.probe		= pm8xxx_rtc_probe,
	.shutdown	= pm8xxx_shutdown,
	.driver	= {
		.name		= "rtc-pm8xxx",
		.of_match_table	= pm8xxx_id_table,
	},
};

module_platform_driver(pm8xxx_rtc_driver);

MODULE_ALIAS("platform:rtc-pm8xxx");
MODULE_DESCRIPTION("PMIC8xxx RTC driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Anirudh Ghayal <aghayal@codeaurora.org>");
MODULE_AUTHOR("Johan Hovold <johan@kernel.org>");
