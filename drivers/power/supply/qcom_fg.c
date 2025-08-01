// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020, The Linux Foundation. All rights reserved. */

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/types.h>
#include <linux/workqueue.h>

/* SOC */
#define BATT_MONOTONIC_SOC		0x009

/* BATT */
#define PARAM_ADDR_BATT_TEMP		0x150
#define BATT_INFO_JEITA_COLD		0x162
#define BATT_INFO_JEITA_COOL		0x163
#define BATT_INFO_JEITA_WARM		0x164
#define BATT_INFO_JEITA_HOT		0x165
#define PARAM_ADDR_BATT_VOLTAGE		0x1a0
#define PARAM_ADDR_BATT_CURRENT		0x1a2

/* MEMIF */
#define MEM_INTF_STS			0x410
#define MEM_INTF_CFG			0x450
#define MEM_INTF_CTL			0x451
#define MEM_INTF_IMA_CFG		0x452
#define MEM_INTF_IMA_EXP_STS		0x455
#define MEM_INTF_IMA_HW_STS		0x456
#define MEM_INTF_IMA_ERR_STS		0x45f
#define MEM_INTF_IMA_BYTE_EN		0x460
#define MEM_INTF_ADDR_LSB		0x461
#define MEM_INTF_RD_DATA0		0x467
#define MEM_INTF_WR_DATA0		0x463
#define MEM_IF_DMA_STS			0x470
#define MEM_IF_DMA_CTL			0x471

/* SRAM addresses */
#define TEMP_THRESHOLD			0x454
#define BATT_TEMP			0x550
#define BATT_VOLTAGE_CURRENT		0x5cc

#define BATT_TEMP_LSB_MASK		GENMASK(7, 0)
#define BATT_TEMP_MSB_MASK		GENMASK(2, 0)

#define BATT_TEMP_JEITA_COLD		100
#define BATT_TEMP_JEITA_COOL		50
#define BATT_TEMP_JEITA_WARM		400
#define BATT_TEMP_JEITA_HOT		450

#define MEM_INTF_AVAIL			BIT(0)
#define MEM_INTF_CTL_BURST		BIT(7)
#define MEM_INTF_CTL_WR_EN		BIT(6)
#define RIF_MEM_ACCESS_REQ		BIT(7)

#define MEM_IF_TIMEOUT_MS		5000
#define SRAM_ACCESS_RELEASE_DELAY_MS	500

struct qcom_fg_chip;

struct qcom_fg_ops {
	int (*get_capacity)(struct qcom_fg_chip *chip, int *);
	int (*get_temperature)(struct qcom_fg_chip *chip, int *);
	int (*get_current)(struct qcom_fg_chip *chip, int *);
	int (*get_voltage)(struct qcom_fg_chip *chip, int *);
	int (*get_temp_threshold)(struct qcom_fg_chip *chip,
			enum power_supply_property psp, int *);
	int (*set_temp_threshold)(struct qcom_fg_chip *chip,
			enum power_supply_property psp, int);
};

struct qcom_fg_chip {
	struct device *dev;
	unsigned int base;
	struct regmap *regmap;
	const struct qcom_fg_ops *ops;
	struct notifier_block nb;

	struct power_supply *batt_psy;
	struct power_supply_battery_info *batt_info;
	struct power_supply *chg_psy;
	int status;
	struct delayed_work status_changed_work;

	struct completion sram_access_granted;
	struct completion sram_access_revoked;
	struct workqueue_struct *sram_wq;
	struct delayed_work sram_release_access_work;
	spinlock_t sram_request_lock;
	spinlock_t sram_rw_lock;
	int sram_requests;
};

/************************
 * IO FUNCTIONS
 * **********************/

/**
 * @brief qcom_fg_read() - Read multiple registers with regmap_bulk_read
 *
 * @param chip Pointer to chip
 * @param val Pointer to read values into
 * @param addr Address to read from
 * @param len Number of registers (bytes) to read
 * @return int 0 on success, negative errno on error
 */
static int qcom_fg_read(struct qcom_fg_chip *chip, u8 *val, u16 addr, int len)
{
	if (((chip->base + addr) & 0xff00) == 0)
		return -EINVAL;

	dev_vdbg(chip->dev, "%s: Reading 0x%x bytes from 0x%x", __func__, len, addr);

	return regmap_bulk_read(chip->regmap, chip->base + addr, val, len);
}

/**
 * @brief qcom_fg_write() - Write multiple registers with regmap_bulk_write
 *
 * @param chip Pointer to chip
 * @param val Pointer to write values from
 * @param addr Address to write to
 * @param len Number of registers (bytes) to write
 * @return int 0 on success, negative errno on error
 */
static int qcom_fg_write(struct qcom_fg_chip *chip, u8 *val, u16 addr, int len)
{
	bool sec_access = (addr & 0xff) > 0xd0;
	u8 sec_addr_val = 0xa5;
	int ret;

	if (((chip->base + addr) & 0xff00) == 0)
			return -EINVAL;

	dev_vdbg(chip->dev, "%s: Writing 0x%x to 0x%x", __func__, *val, addr);

	if (sec_access) {
		ret = regmap_bulk_write(chip->regmap,
				((chip->base + addr) & 0xff00) | 0xd0,
				&sec_addr_val, 1);
		if (ret)
			return ret;
	}

	return regmap_bulk_write(chip->regmap, chip->base + addr, val, len);
}

/**
 * @brief qcom_fg_masked_write() - like qcom_fg_write but applies
 * a mask first.
 *
 * @param chip Pointer to chip
 * @param val Pointer to write values from
 * @param addr Address to write to
 * @param len Number of registers (bytes) to write
 * @return int 0 on success, negative errno on error
 */
static int qcom_fg_masked_write(struct qcom_fg_chip *chip, u16 addr, u8 mask, u8 val)
{
	u8 reg;
	int ret;

	ret = qcom_fg_read(chip, &reg, addr, 1);
	if (ret)
		return ret;

	reg &= ~mask;
	reg |= val & mask;

	return qcom_fg_write(chip, &reg, addr, 1);
}

/************************
 * SRAM FUNCTIONS
 * **********************/

/**
 * @brief qcom_fg_sram_check_access() - Check if SRAM is accessible
 *
 * @param chip Pointer to chip
 * @return bool true if accessible, false otherwise
 */
static bool qcom_fg_sram_check_access(struct qcom_fg_chip *chip)
{
	u8 mem_if_status;
	int ret;

	ret = qcom_fg_read(chip, &mem_if_status,
		MEM_INTF_STS, 1);

	if (ret || !(mem_if_status & MEM_INTF_AVAIL))
		return false;

	ret = qcom_fg_read(chip, &mem_if_status,
		MEM_INTF_CFG, 1);

	if (ret)
		return false;

	return !!(mem_if_status & RIF_MEM_ACCESS_REQ);
}

/**
 * @brief qcom_fg_sram_request_access() - Request access to SRAM and wait for it
 * 
 * @param chip Pointer to chip
 * @return int 0 on success, negative errno on error
 */
static int qcom_fg_sram_request_access(struct qcom_fg_chip *chip)
{
	bool sram_accessible;
	int ret;

	spin_lock(&chip->sram_request_lock);

	sram_accessible = qcom_fg_sram_check_access(chip);

	dev_vdbg(chip->dev, "Requesting SRAM access, current state: %d, requests: %d\n",
		sram_accessible, chip->sram_requests);

	if (!sram_accessible && chip->sram_requests == 0) {
		ret = qcom_fg_masked_write(chip, MEM_INTF_CFG,
				RIF_MEM_ACCESS_REQ, RIF_MEM_ACCESS_REQ);
		if (ret) {
			dev_err(chip->dev,
				"Failed to set SRAM access request bit: %d\n", ret);

			spin_unlock(&chip->sram_request_lock);
			return ret;
		}
	}

	chip->sram_requests++;

	spin_unlock(&chip->sram_request_lock);

	/* Wait to get access to SRAM, and try again if interrupted */
	do {
		ret = wait_for_completion_interruptible_timeout(
			&chip->sram_access_granted,
			msecs_to_jiffies(MEM_IF_TIMEOUT_MS));
	} while(ret == -ERESTARTSYS);

	if (ret <= 0) {
		ret = -ETIMEDOUT;

		spin_lock(&chip->sram_request_lock);
		chip->sram_requests--;
		spin_unlock(&chip->sram_request_lock);
	} else {
		ret = 0;

		reinit_completion(&chip->sram_access_revoked);
	}

	return ret;
}

/**
 * @brief qcom_fg_sram_release_access() - Release access to SRAM
 *
 * @param chip Pointer to chip
 * @return int 0 on success, negative errno on error
 */
static void qcom_fg_sram_release_access(struct qcom_fg_chip *chip)
{
	spin_lock(&chip->sram_request_lock);

	chip->sram_requests--;

	if(WARN(chip->sram_requests < 0,
			"sram_requests=%d, cannot be negative! resetting to 0.\n",
			chip->sram_requests))
		chip->sram_requests = 0;

	if(chip->sram_requests == 0)
		/* Schedule access release */
		queue_delayed_work(chip->sram_wq, &chip->sram_release_access_work,
			msecs_to_jiffies(SRAM_ACCESS_RELEASE_DELAY_MS));

	spin_unlock(&chip->sram_request_lock);
}

static void qcom_fg_sram_release_access_worker(struct work_struct *work)
{
	struct qcom_fg_chip *chip;
	bool wait = false;
	int ret;

	chip = container_of(work, struct qcom_fg_chip, sram_release_access_work.work);

	spin_lock(&chip->sram_request_lock);

	/* Request access release if there are still no access requests */
	if(chip->sram_requests == 0) {
		qcom_fg_masked_write(chip, MEM_INTF_CFG, RIF_MEM_ACCESS_REQ, 0);
		wait = true;
	}

	spin_unlock(&chip->sram_request_lock);

	if(!wait)
		return;

	/* Wait for SRAM access to be released, and try again if interrupted */
	do {
		ret = wait_for_completion_interruptible_timeout(
			&chip->sram_access_revoked,
			msecs_to_jiffies(MEM_IF_TIMEOUT_MS));
	} while(ret == -ERESTARTSYS);

	reinit_completion(&chip->sram_access_granted);
}

/**
 * @brief qcom_fg_sram_config_access() - Configure access to SRAM
 *
 * @param chip Pointer to chip
 * @param write 0 for read access, 1 for write access
 * @param burst 1 to access mutliple addresses successively
 * @return int 0 on success, negative errno on error
 */
static int qcom_fg_sram_config_access(struct qcom_fg_chip *chip,
		bool write, bool burst)
{
	u8 intf_ctl;
	int ret;

	intf_ctl = (write ? MEM_INTF_CTL_WR_EN : 0)
			| (burst ? MEM_INTF_CTL_BURST : 0);

	ret = qcom_fg_write(chip, &intf_ctl,
			MEM_INTF_CTL, 1);
	if (ret) {
		dev_err(chip->dev, "Failed to configure SRAM access: %d\n", ret);
		return ret;
	}

	return 0;
}

/**
 * @brief qcom_fg_sram_read() - Read data from SRAM
 *
 * @param chip Pointer to chip
 * @param val Pointer to read values into
 * @param addr Address to read from
 * @param len Number of bytes to read
 * @return int 0 on success, negative errno on error
 */
static int qcom_fg_sram_read(struct qcom_fg_chip *chip,
		u8 *val, u16 addr, int len, int offset)
{
	u8 *rd_data = val;
	int ret = 0;

	ret = qcom_fg_sram_request_access(chip);
	if (ret) {
		dev_err(chip->dev, "Failed to request SRAM access: %d", ret);
		return ret;
	}

	spin_lock(&chip->sram_rw_lock);

	dev_vdbg(chip->dev,
		"Reading address 0x%x with offset %d of length %d from SRAM",
		addr, len, offset);

	ret = qcom_fg_sram_config_access(chip, 0, (len > 4));
	if (ret) {
		dev_err(chip->dev, "Failed to configure SRAM access: %d", ret);
		goto out;
	}

	while(len > 0) {
		/* Set SRAM address register */
		ret = qcom_fg_write(chip, (u8 *) &addr,
				MEM_INTF_ADDR_LSB, 2);
		if (ret) {
			dev_err(chip->dev, "Failed to set SRAM address: %d", ret);
			goto out;
		}

		ret = qcom_fg_read(chip, rd_data,
				MEM_INTF_RD_DATA0 + offset, len);

		addr += 4;

		if (ret)
			goto out;

		rd_data += 4 - offset;
		len -= 4 - offset;
		offset = 0;
	}
out:
	spin_unlock(&chip->sram_rw_lock);
	qcom_fg_sram_release_access(chip);

	return ret;
}

/**
 * @brief qcom_fg_sram_write() - Write data to SRAM
 *
 * @param chip Pointer to chip
 * @param val Pointer to write values from
 * @param addr Address to write to
 * @param len Number of bytes to write
 * @return int 0 on success, negative errno on error
 */
static int qcom_fg_sram_write(struct qcom_fg_chip *chip,
		u8 *val, u16 addr, int len, int offset)
{
	u8 *wr_data = val;
	int ret;

	ret = qcom_fg_sram_request_access(chip);
	if (ret) {
		dev_err(chip->dev, "Failed to request SRAM access: %d", ret);
		return ret;
	}

	spin_lock(&chip->sram_rw_lock);

	dev_vdbg(chip->dev,
		"Wrtiting address 0x%x with offset %d of length %d to SRAM",
		addr, len, offset);

	ret = qcom_fg_sram_config_access(chip, 1, (len > 4));
	if (ret) {
		dev_err(chip->dev, "Failed to configure SRAM access: %d", ret);
		goto out;
	}

	while(len > 0) {
		/* Set SRAM address register */
		ret = qcom_fg_write(chip, (u8 *) &addr,
				MEM_INTF_ADDR_LSB, 2);
		if (ret) {
			dev_err(chip->dev, "Failed to set SRAM address: %d", ret);
			goto out;
		}

		ret = qcom_fg_write(chip, wr_data,
				MEM_INTF_WR_DATA0 + offset, len);

		addr += 4;

		if (ret)
			goto out;

		wr_data += 4 - offset;
		len -= 4 - offset;
		offset = 0;
	}
out:
	spin_unlock(&chip->sram_rw_lock);
	qcom_fg_sram_release_access(chip);

	return ret;
}

/*************************
 * BATTERY STATUS
 * ***********************/

/**
 * @brief qcom_fg_get_capacity() - Get remaining capacity of battery
 *
 * @param chip Pointer to chip
 * @param val Pointer to store value at
 * @return int 0 on success, negative errno on error
 */
static int qcom_fg_get_capacity(struct qcom_fg_chip *chip, int *val)
{
	u8 cap[2];
	int ret;

	ret = qcom_fg_read(chip, cap, BATT_MONOTONIC_SOC, 2);
	if (ret) {
		dev_err(chip->dev, "Failed to read capacity: %d", ret);
		return ret;
	}

	if (cap[0] != cap[1]) {
		cap[0] = cap[0] < cap[1] ? cap[0] : cap[1];
	}

	*val = DIV_ROUND_CLOSEST((cap[0] - 1) * 98, 0xff - 2) + 1;

	return 0;
}

/**
 * @brief qcom_fg_get_temperature() - Get temperature of battery
 *
 * @param chip Pointer to chip
 * @param val Pointer to store value at
 * @return int 0 on success, negative errno on error
 */
static int qcom_fg_get_temperature(struct qcom_fg_chip *chip, int *val)
{
	int temp;
	u8 readval[2];
	int ret;

	ret = qcom_fg_sram_read(chip, readval, BATT_TEMP, 2, 2);
	if (ret) {
		dev_err(chip->dev, "Failed to read temperature: %d", ret);
		return ret;
	}

	temp = readval[1] << 8 | readval[0];
	*val = temp * 625 / 1000 - 2730;
	return 0;
}

/**
 * @brief qcom_fg_get_current() - Get current being drawn from battery
 *
 * @param chip Pointer to chip
 * @param val Pointer to store value at
 * @return int 0 on success, negative errno on error
 */
static int qcom_fg_get_current(struct qcom_fg_chip *chip, int *val)
{
	s16 temp;
	u8 readval[2];
	int ret;

	ret = qcom_fg_sram_read(chip, readval, BATT_VOLTAGE_CURRENT, 2, 3);
	if (ret) {
		dev_err(chip->dev, "Failed to read current: %d", ret);
		return ret;
	}

	temp = (s16)(readval[1] << 8 | readval[0]);
	*val = div_s64((s64)temp * 152587, 1000);

	return 0;
}

/**
 * @brief qcom_fg_get_voltage() - Get voltage of battery
 *
 * @param chip Pointer to chip
 * @param val Pointer to store value at
 * @return int 0 on success, negative errno on error
 */
static int qcom_fg_get_voltage(struct qcom_fg_chip *chip, int *val)
{
	int temp;
	u8 readval[2];
	int ret;

	ret = qcom_fg_sram_read(chip, readval, BATT_VOLTAGE_CURRENT, 2, 1);
	if (ret) {
		dev_err(chip->dev, "Failed to read voltage: %d", ret);
		return ret;
	}

	temp = readval[1] << 8 | readval[0];
	*val = div_u64((u64)temp * 152587, 1000);

	return 0;
}

/**
 * @brief qcom_fg_get_temp_threshold() - Get configured temperature thresholds
 *
 * @param chip Pointer to chip
 * @param psp Power supply property of temperature limit
 * @param val Pointer to store value at
 * @return int 0 on success, negative errno on error
 */
static int qcom_fg_get_temp_threshold(struct qcom_fg_chip *chip,
				enum power_supply_property psp, int *val)
{
	u8 temp;
	int offset;
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_TEMP_MIN:
		offset = 0;
		break;
	case POWER_SUPPLY_PROP_TEMP_MAX:
		offset = 1;
		break;
	case POWER_SUPPLY_PROP_TEMP_ALERT_MIN:
		offset = 2;
		break;
	case POWER_SUPPLY_PROP_TEMP_ALERT_MAX:
		offset = 3;
		break;
	default:
		return -EINVAL;
	}

	ret = qcom_fg_sram_read(chip, &temp, TEMP_THRESHOLD, 1, offset);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read JEITA property %d level: %d\n", psp, ret);
		return ret;
	}

	*val = (temp - 30) * 10;

	return 0;
}

/**
 * @brief qcom_fg_set_temp_threshold() - Configure temperature thresholds
 *
 * @param chip Pointer to chip
 * @param psp Power supply property of temperature limit
 * @param val Pointer to get value from
 * @return int 0 on success, negative errno on error
 */
static int qcom_fg_set_temp_threshold(struct qcom_fg_chip *chip,
				enum power_supply_property psp, int val)
{
	u8 temp;
	int offset;
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_TEMP_MIN:
		offset = 0;
		break;
	case POWER_SUPPLY_PROP_TEMP_MAX:
		offset = 1;
		break;
	case POWER_SUPPLY_PROP_TEMP_ALERT_MIN:
		offset = 2;
		break;
	case POWER_SUPPLY_PROP_TEMP_ALERT_MAX:
		offset = 3;
		break;
	default:
		return -EINVAL;
	}

	temp = val / 10 + 30;

	ret = qcom_fg_sram_write(chip, &temp, TEMP_THRESHOLD, 1, offset);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to write JEITA property %d level: %d\n", psp, ret);
		return ret;
	}

	return 0;
}

/*************************
 * BATTERY STATUS, GEN3
 * ***********************/

/**
 * @brief qcom_fg_gen3_get_temperature() - Get temperature of battery
 *
 * @param chip Pointer to chip
 * @param val Pointer to store value at
 * @return int 0 on success, negative errno on error
 */
static int qcom_fg_gen3_get_temperature(struct qcom_fg_chip *chip, int *val)
{
	int temp;
	u8 readval[2];
	int ret;

	ret = qcom_fg_read(chip, readval, PARAM_ADDR_BATT_TEMP, 2);
	if (ret) {
		dev_err(chip->dev, "Failed to read temperature: %d\n", ret);
		return ret;
	}

	temp = ((readval[1] & BATT_TEMP_MSB_MASK) << 8) |
		(readval[0] & BATT_TEMP_LSB_MASK);
	temp = DIV_ROUND_CLOSEST(temp * 10, 4);

	*val = temp -2730;
	return 0;
}

/**
 * @brief qcom_fg_gen3_get_current() - Get current being drawn from battery
 *
 * @param chip Pointer to chip
 * @param val Pointer to store value at
 * @return int 0 on success, negative errno on error
 */
static int qcom_fg_gen3_get_current(struct qcom_fg_chip *chip, int *val)
{
	s16 temp;
	u8 readval[2];
	int ret;

	ret = qcom_fg_read(chip, readval, PARAM_ADDR_BATT_CURRENT, 2);
	if (ret) {
		dev_err(chip->dev, "Failed to read current: %d\n", ret);
		return ret;
	}

	//handle rev 1 too
	temp = (s16)(readval[1] << 8 | readval[0]);
	*val = div_s64((s64)temp * 488281, 1000);

	return 0;
}

/**
 * @brief qcom_fg_gen3_get_voltage() - Get voltage of battery
 *
 * @param chip Pointer to chip
 * @param val Pointer to store value at
 * @return int 0 on success, negative errno on error
 */
static int qcom_fg_gen3_get_voltage(struct qcom_fg_chip *chip, int *val)
{
	int temp;
	u8 readval[2];
	int ret;

	ret = qcom_fg_read(chip, readval, PARAM_ADDR_BATT_VOLTAGE, 2);
	if (ret) {
		dev_err(chip->dev, "Failed to read voltage: %d\n", ret);
		return ret;
	}

	//handle rev 1 too
	temp = readval[1] << 8 | readval[0];
	*val = div_u64((u64)temp * 122070, 1000);
	return 0;
}

/**
 * @brief qcom_fg_gen3_get_temp_threshold() - Get configured temperature thresholds
 *
 * @param chip Pointer to chip
 * @param psp Power supply property of temperature limit
 * @param val Pointer to store value at
 * @return int 0 on success, negative errno on error
 */
static int qcom_fg_gen3_get_temp_threshold(struct qcom_fg_chip *chip,
				enum power_supply_property psp, int *val)
{
	u8 temp;
	u16 reg;
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_TEMP_MIN:
		reg = BATT_INFO_JEITA_COLD;
		break;
	case POWER_SUPPLY_PROP_TEMP_MAX:
		reg = BATT_INFO_JEITA_HOT;
		break;
	case POWER_SUPPLY_PROP_TEMP_ALERT_MIN:
		reg = BATT_INFO_JEITA_COOL;
		break;
	case POWER_SUPPLY_PROP_TEMP_ALERT_MAX:
		reg = BATT_INFO_JEITA_WARM;
		break;
	default:
		return -EINVAL;
	}

	ret = qcom_fg_read(chip, &temp, reg, 1);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read JEITA property %d level: %d\n", psp, ret);
		return ret;
	}

	/* Resolution is 0.5C. Base is -30C. */
	*val = (((5 * temp) / 10) - 30) * 10;
	return 0;
}

/************************
 * BATTERY POWER SUPPLY
 * **********************/

/* Pre-Gen3 fuel gauge. PMI8996 and older */
static const struct qcom_fg_ops ops_fg = {
	.get_capacity = qcom_fg_get_capacity,
	.get_temperature = qcom_fg_get_temperature,
	.get_current = qcom_fg_get_current,
	.get_voltage = qcom_fg_get_voltage,
	.get_temp_threshold = qcom_fg_get_temp_threshold,
	.set_temp_threshold = qcom_fg_set_temp_threshold,
};

/* Gen3 fuel gauge. PMI8998 and newer */
static const struct qcom_fg_ops ops_fg_gen3 = {
	.get_capacity = qcom_fg_get_capacity,
	.get_temperature = qcom_fg_gen3_get_temperature,
	.get_current = qcom_fg_gen3_get_current,
	.get_voltage = qcom_fg_gen3_get_voltage,
	.get_temp_threshold = qcom_fg_gen3_get_temp_threshold,
};

static enum power_supply_property qcom_fg_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TEMP_MIN,
	POWER_SUPPLY_PROP_TEMP_MAX,
	POWER_SUPPLY_PROP_TEMP_ALERT_MIN,
	POWER_SUPPLY_PROP_TEMP_ALERT_MAX,
};

static int qcom_fg_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct qcom_fg_chip *chip = power_supply_get_drvdata(psy);
	int temp, ret = 0;

	dev_dbg(chip->dev, "Getting property: %d", psp);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		/* Get status from charger if available */
		if (chip->chg_psy &&
		    chip->status != POWER_SUPPLY_STATUS_UNKNOWN) {
			val->intval = chip->status;
			break;
		} else {
			/*
			 * Fall back to capacity and current-based
			 * status checking
			 */
			ret = chip->ops->get_capacity(chip, &temp);
			if (ret) {
				val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
				break;
			}
			if (temp == 100) {
				val->intval = POWER_SUPPLY_STATUS_FULL;
				break;
			}

			ret = chip->ops->get_current(chip, &temp);
			if (ret) {
				val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
				break;
			}
			if (temp < 0)
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
			else if (temp > 0)
				val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
			else
				val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		}

		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = chip->ops->get_capacity(chip, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = chip->ops->get_current(chip, &val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = chip->ops->get_voltage(chip, &val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = chip->batt_info->voltage_min_design_uv;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = chip->batt_info->voltage_max_design_uv;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = chip->batt_info->charge_full_design_uah;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		ret = chip->ops->get_temperature(chip, &val->intval);
		break;
	case POWER_SUPPLY_PROP_TEMP_MIN:
	case POWER_SUPPLY_PROP_TEMP_MAX:
	case POWER_SUPPLY_PROP_TEMP_ALERT_MIN:
	case POWER_SUPPLY_PROP_TEMP_ALERT_MAX:
		ret = chip->ops->get_temp_threshold(chip, psp, &val->intval);
		break;
	default:
		dev_err(chip->dev, "invalid property: %d\n", psp);
		return -EINVAL;
	}

	return ret;
}

static const struct power_supply_desc batt_psy_desc = {
	.name = "qcom-battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = qcom_fg_props,
	.num_properties = ARRAY_SIZE(qcom_fg_props),
	.get_property = qcom_fg_get_property,
};

/********************
 * INIT FUNCTIONS
 * ******************/

static int qcom_fg_iacs_clear_sequence(struct qcom_fg_chip *chip)
{
	u8 temp;
	int ret;

	/* clear the error */
	ret = qcom_fg_masked_write(chip, MEM_INTF_IMA_CFG, BIT(2), BIT(2));
	if (ret) {
		dev_err(chip->dev, "Failed to write IMA_CFG: %d\n", ret);
		return ret;
	}

	temp = 0x4;
	ret = qcom_fg_write(chip, &temp, MEM_INTF_ADDR_LSB + 1, 1);
	if (ret) {
		dev_err(chip->dev, "Failed to write MEM_INTF_ADDR_MSB: %d\n", ret);
		return ret;
	}

	temp = 0x0;
	ret = qcom_fg_write(chip, &temp, MEM_INTF_WR_DATA0 + 3, 1);
	if (ret) {
		dev_err(chip->dev, "Failed to write WR_DATA3: %d\n", ret);
		return ret;
	}

	ret = qcom_fg_read(chip, &temp, MEM_INTF_RD_DATA0 + 3, 1);
	if (ret) {
		dev_err(chip->dev, "Failed to write RD_DATA3: %d\n", ret);
		return ret;
	}

	ret = qcom_fg_masked_write(chip, MEM_INTF_IMA_CFG, BIT(2), 0);
	if (ret) {
		dev_err(chip->dev, "Failed to write IMA_CFG: %d\n", ret);
		return ret;
	}

	return 0;
}

static int qcom_fg_clear_ima(struct qcom_fg_chip *chip,
		bool check_hw_sts)
{
	u8 err_sts, exp_sts, hw_sts;
	bool run_err_clr_seq = false;
	int ret;

	ret = qcom_fg_read(chip, &err_sts,
			MEM_INTF_IMA_ERR_STS, 1);
	if (ret) {
		dev_err(chip->dev, "Failed to read IMA_ERR_STS: %d\n", ret);
		return ret;
	}

	ret = qcom_fg_read(chip, &exp_sts,
			MEM_INTF_IMA_EXP_STS, 1);
	if (ret) {
		dev_err(chip->dev, "Failed to read IMA_EXP_STS: %d\n", ret);
		return ret;
	}

	if (check_hw_sts) {
		ret = qcom_fg_read(chip, &hw_sts,
				MEM_INTF_IMA_HW_STS, 1);
		if (ret) {
			dev_err(chip->dev, "Failed to read IMA_HW_STS: %d\n", ret);
			return ret;
		}
		/*
		 * Lower nibble should be equal to upper nibble before SRAM
		 * transactions begins from SW side.
		 */
		if ((hw_sts & 0x0f) != hw_sts >> 4) {
			dev_dbg(chip->dev, "IMA HW not in correct state, hw_sts=%x\n",
					hw_sts);
			run_err_clr_seq = true;
		}
	}

	if (exp_sts & (BIT(0) | BIT(1) | BIT(3) |
		BIT(4) | BIT(5) | BIT(6) |
		BIT(7))) {
		dev_dbg(chip->dev, "IMA exception bit set, exp_sts=%x\n", exp_sts);
		run_err_clr_seq = true;
	}

	if (run_err_clr_seq) {
		ret = qcom_fg_iacs_clear_sequence(chip);
		if (!ret)
			return -EAGAIN;
	}

	return 0;
}

static irqreturn_t qcom_fg_handle_soc_delta(int irq, void *data)
{
	struct qcom_fg_chip *chip = data;

	/* Signal change in state of charge */
	power_supply_changed(chip->batt_psy);
	dev_dbg(chip->dev, "SOC changed");

	return IRQ_HANDLED;
}

static irqreturn_t qcom_fg_handle_mem_avail(int irq, void *data)
{
	struct qcom_fg_chip *chip = data;

	if (qcom_fg_sram_check_access(chip)) {
		complete_all(&chip->sram_access_granted);
		dev_dbg(chip->dev, "SRAM access granted");
	} else {
		complete_all(&chip->sram_access_revoked);
		dev_dbg(chip->dev, "SRAM access revoked");
	}

	return IRQ_HANDLED;
}

static void qcom_fg_status_changed_worker(struct work_struct *work)
{
	struct qcom_fg_chip *chip = container_of(work, struct qcom_fg_chip,
						status_changed_work.work);

	power_supply_changed(chip->batt_psy);
}

static int qcom_fg_notifier_call(struct notifier_block *nb,
		unsigned long val, void *v)
{
	struct qcom_fg_chip *chip = container_of(nb, struct qcom_fg_chip, nb);
	struct power_supply *psy = v;
	union power_supply_propval propval;
	int ret;

	if (psy == chip->chg_psy) {
		ret = power_supply_get_property(psy,
				POWER_SUPPLY_PROP_STATUS, &propval);
		if (ret)
			chip->status = POWER_SUPPLY_STATUS_UNKNOWN;

		chip->status = propval.intval;

		power_supply_changed(chip->batt_psy);

		if (chip->status == POWER_SUPPLY_STATUS_UNKNOWN) {
			/*
			 * REVISIT: Find better solution or remove current-based
			 * status checking once checking is properly implemented
			 * in charger drivers

			 * Sometimes it take a while for current to stabilize,
			 * so signal property change again later to make sure
			 * current-based status is properly detected.
			 */
			cancel_delayed_work_sync(&chip->status_changed_work);
			schedule_delayed_work(&chip->status_changed_work,
						msecs_to_jiffies(1000));
		}
	}

	return NOTIFY_OK;
}

static int qcom_fg_probe(struct platform_device *pdev)
{
	struct power_supply_config supply_config = {};
	struct qcom_fg_chip *chip;
	const __be32 *prop_addr;
	int irq;
	u8 dma_status;
	bool error_present;
	int ret;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;
	chip->ops = of_device_get_match_data(&pdev->dev);

	chip->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!chip->regmap) {
		dev_err(chip->dev, "Failed to locate the regmap\n");
		return -ENODEV;
	}

	/* Get base address */
	prop_addr = of_get_address(pdev->dev.of_node, 0, NULL, NULL);
	if (!prop_addr) {
		dev_err(chip->dev, "Failed to read SOC base address from dt\n");
		return -EINVAL;
	}
	chip->base = be32_to_cpu(*prop_addr);

	/*
	 * Change the FG_MEM_INT interrupt to track IACS_READY
	 * condition instead of end-of-transaction. This makes sure
	 * that the next transaction starts only after the hw is ready.
	 * IACS_INTR_SRC_SLCT is BIT(3)
	 */
	ret = qcom_fg_masked_write(chip,
		MEM_INTF_IMA_CFG, BIT(3), BIT(3));
	if (ret) {
		dev_err(chip->dev,
			"Failed to configure interrupt sourete: %d\n", ret);
		return ret;
	}

	ret = qcom_fg_clear_ima(chip, true);
	if (ret && ret != -EAGAIN) {
		dev_err(chip->dev, "Failed to clear IMA exception: %d\n", ret);
		return ret;
	}

	/* Check and clear DMA errors */
	ret = qcom_fg_read(chip, &dma_status, MEM_IF_DMA_STS, 1);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read dma_status: %d\n", ret);
		return ret;
	}

	error_present = dma_status & (BIT(1) | BIT(2));
	ret = qcom_fg_masked_write(chip, MEM_IF_DMA_CTL, BIT(0),
			error_present ? BIT(0) : 0);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to write dma_ctl: %d\n", ret);
		return ret;
	}

	supply_config.drv_data = chip;
	supply_config.of_node = pdev->dev.of_node;

	chip->batt_psy = devm_power_supply_register(chip->dev,
			&batt_psy_desc, &supply_config);
	if (IS_ERR(chip->batt_psy)) {
		if (PTR_ERR(chip->batt_psy) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to register battery\n");
		return PTR_ERR(chip->batt_psy);
	}

	platform_set_drvdata(pdev, chip);

	ret = power_supply_get_battery_info(chip->batt_psy, &chip->batt_info);
	if (ret) {
		dev_err(&pdev->dev, "Failed to get battery info: %d\n", ret);
		return ret;
	}

	/* Initialize SRAM */
	if (of_device_is_compatible(pdev->dev.of_node, "qcom,pmi8994-fg")) {
		irq = of_irq_get_byname(pdev->dev.of_node, "mem-avail");
		if (irq < 0) {
			dev_err(&pdev->dev, "Failed to get irq mem-avail byname: %d\n",
				irq);
			return irq;
		}

		init_completion(&chip->sram_access_granted);
		init_completion(&chip->sram_access_revoked);

		chip->sram_wq = create_singlethread_workqueue("qcom_fg");
		INIT_DELAYED_WORK(&chip->sram_release_access_work,
			qcom_fg_sram_release_access_worker);

		ret = devm_request_threaded_irq(chip->dev, irq, NULL,
						qcom_fg_handle_mem_avail,
						IRQF_ONESHOT, "mem-avail", chip);
		if (ret < 0) {
			dev_err(&pdev->dev, "Failed to request mem-avail IRQ: %d\n", ret);
			return ret;
		}

		spin_lock_init(&chip->sram_request_lock);
		spin_lock_init(&chip->sram_rw_lock);
		chip->sram_requests = 0;
	}

	/* Set default temperature thresholds */
	if (chip->ops->set_temp_threshold) {
		ret = chip->ops->set_temp_threshold(chip,
						POWER_SUPPLY_PROP_TEMP_MIN,
						BATT_TEMP_JEITA_COLD);
		if (ret) {
			dev_err(chip->dev,
				"Failed to set cold threshold: %d\n", ret);
			return ret;
		}

		ret = chip->ops->set_temp_threshold(chip,
						POWER_SUPPLY_PROP_TEMP_MAX,
						BATT_TEMP_JEITA_WARM);
		if (ret) {
			dev_err(chip->dev,
				"Failed to set warm threshold: %d\n", ret);
			return ret;
		}

		ret = chip->ops->set_temp_threshold(chip,
						POWER_SUPPLY_PROP_TEMP_ALERT_MIN,
						BATT_TEMP_JEITA_COOL);
		if (ret) {
			dev_err(chip->dev,
				"Failed to set cool threshold: %d\n", ret);
			return ret;
		}

		ret = chip->ops->set_temp_threshold(chip,
						POWER_SUPPLY_PROP_TEMP_ALERT_MAX,
						BATT_TEMP_JEITA_HOT);
		if (ret) {
			dev_err(chip->dev,
				"Failed to set hot threshold: %d\n", ret);
			return ret;
		}
	}

	/* Get soc-delta IRQ */
	irq = of_irq_get_byname(pdev->dev.of_node, "soc-delta");
	if (irq < 0) {
		dev_err(&pdev->dev, "Failed to get irq soc-delta byname: %d\n",
			irq);
		return irq;
	}

	ret = devm_request_threaded_irq(chip->dev, irq, NULL,
					qcom_fg_handle_soc_delta,
					IRQF_ONESHOT, "soc-delta", chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request soc-delta IRQ: %d\n", ret);
		return ret;
	}

	/* Optional: Get charger power supply for status checking */
	chip->chg_psy = power_supply_get_by_phandle(chip->dev->of_node,
							"power-supplies");
	if (IS_ERR(chip->chg_psy)) {
		ret = PTR_ERR(chip->chg_psy);
		dev_warn(chip->dev, "Failed to get charger supply: %d\n", ret);
		chip->chg_psy = NULL;
	}

	if (chip->chg_psy) {
		INIT_DELAYED_WORK(&chip->status_changed_work,
			qcom_fg_status_changed_worker);

		chip->nb.notifier_call = qcom_fg_notifier_call;
		ret = power_supply_reg_notifier(&chip->nb);
		if (ret) {
			dev_err(chip->dev,
				"Failed to register notifier: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static void qcom_fg_remove(struct platform_device *pdev)
{
	struct qcom_fg_chip *chip = platform_get_drvdata(pdev);

	power_supply_put_battery_info(chip->chg_psy, chip->batt_info);

	if(chip->sram_wq)
		destroy_workqueue(chip->sram_wq);
}

static const struct of_device_id fg_match_id_table[] = {
	{ .compatible = "qcom,pmi8994-fg", .data = &ops_fg },
	{ .compatible = "qcom,pmi8998-fg", .data = &ops_fg_gen3 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, fg_match_id_table);

static struct platform_driver qcom_fg_driver = {
	.probe = qcom_fg_probe,
	.remove = qcom_fg_remove,
	.driver = {
		.name = "qcom-fg",
		.of_match_table = fg_match_id_table,
	},
};

module_platform_driver(qcom_fg_driver);

MODULE_AUTHOR("Caleb Connolly <caleb@connolly.tech>");
MODULE_AUTHOR("Joel Selvaraj <jo@jsfamily.in>");
MODULE_AUTHOR("Yassine Oudjana <y.oudjana@protonmail.com>");
MODULE_DESCRIPTION("Qualcomm PMIC Fuel Gauge Driver");
MODULE_LICENSE("GPL v2");
