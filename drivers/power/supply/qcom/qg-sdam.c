// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"QG-K: %s: " fmt, __func__

#include <linux/device.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include "qg-sdam.h"
#include "qg-reg.h"

static struct qg_sdam *the_chip;

struct qg_sdam_info {
	char		*name;
	u32		offset;
	u32		length;
};

static struct qg_sdam_info sdam_info[] = {
	[SDAM_VALID] = {
		.name	= "VALID",
		.offset = QG_SDAM_VALID_OFFSET,
		.length = 1,
	},
	[SDAM_SOC] = {
		.name	= "SOC",
		.offset = QG_SDAM_SOC_OFFSET,
		.length = 1,
	},
	[SDAM_TEMP] = {
		.name	= "BATT_TEMP",
		.offset = QG_SDAM_TEMP_OFFSET,
		.length = 2,
	},
	[SDAM_RBAT_MOHM] = {
		.name	= "RBAT_MOHM",
		.offset = QG_SDAM_RBAT_OFFSET,
		.length = 2,
	},
	[SDAM_OCV_UV] = {
		.name	= "OCV_UV",
		.offset = QG_SDAM_OCV_OFFSET,
		.length = 4,
	},
	[SDAM_IBAT_UA] = {
		.name	= "IBAT_UA",
		.offset = QG_SDAM_IBAT_OFFSET,
		.length = 4,
	},
	[SDAM_TIME_SEC] = {
		.name	= "TIME_SEC",
		.offset = QG_SDAM_TIME_OFFSET,
		.length = 4,
	},
	[SDAM_PON_OCV_UV] = {
		.name	= "SDAM_PON_OCV",
		.offset = QG_SDAM_PON_OCV_OFFSET,
		.length = 2,
	},
	[SDAM_ESR_CHARGE_DELTA] = {
		.name	= "SDAM_ESR_CHARGE_DELTA",
		.offset = QG_SDAM_ESR_CHARGE_DELTA_OFFSET,
		.length = 4,
	},
	[SDAM_ESR_DISCHARGE_DELTA] = {
		.name	= "SDAM_ESR_DISCHARGE_DELTA",
		.offset = QG_SDAM_ESR_DISCHARGE_DELTA_OFFSET,
		.length = 4,
	},
	[SDAM_ESR_CHARGE_SF] = {
		.name	= "SDAM_ESR_CHARGE_SF_OFFSET",
		.offset = QG_SDAM_ESR_CHARGE_SF_OFFSET,
		.length = 2,
	},
	[SDAM_ESR_DISCHARGE_SF] = {
		.name	= "SDAM_ESR_DISCHARGE_SF_OFFSET",
		.offset = QG_SDAM_ESR_DISCHARGE_SF_OFFSET,
		.length = 2,
	},
	[SDAM_BATT_AGE_LEVEL] = {
		.name   = "SDAM_BATT_AGE_LEVEL_OFFSET",
		.offset = QG_SDAM_BATT_AGE_LEVEL_OFFSET,
		.length = 1,
	},
	[SDAM_MAGIC] = {
		.name	= "SDAM_MAGIC_OFFSET",
		.offset = QG_SDAM_MAGIC_OFFSET,
		.length = 4,
	},
	[SDAM_FLASH_OCV] = {
		.name	= "SDAM_FLASH_OCV_OFFSET",
		.offset = QG_SDAM_FLASH_OCV_OFFSET,
		.length = 1,
	},
};

int qg_sdam_write(u8 param, u32 data)
{
	int rc;
	struct qg_sdam *chip = the_chip;
	u32 offset;
	size_t length;

	if (!chip) {
		pr_err("Invalid sdam-chip pointer\n");
		return -EINVAL;
	}

	if (param >= SDAM_MAX) {
		pr_err("Invalid SDAM param %d\n", param);
		return -EINVAL;
	}

	offset = chip->sdam_base + sdam_info[param].offset;
	length = sdam_info[param].length;
	rc = regmap_bulk_write(chip->regmap, offset, (u8 *)&data, length);
	if (rc < 0)
		pr_err("Failed to write offset=%0x4 param=%d value=%d\n",
					offset, param, data);
	else
		pr_debug("QG SDAM write param=%s value=%d\n",
					sdam_info[param].name, data);

	return rc;
}

int qg_sdam_read(u8 param, u32 *data)
{
	int rc;
	struct qg_sdam *chip = the_chip;
	u32 offset;
	size_t length;

	if (!chip) {
		pr_err("Invalid sdam-chip pointer\n");
		return -EINVAL;
	}

	if (param >= SDAM_MAX) {
		pr_err("Invalid SDAM param %d\n", param);
		return -EINVAL;
	}

	*data = 0;
	offset = chip->sdam_base + sdam_info[param].offset;
	length = sdam_info[param].length;
	rc = regmap_raw_read(chip->regmap, offset, (u8 *)data, length);
	if (rc < 0)
		pr_err("Failed to read offset=%0x4 param=%d\n",
					offset, param);
	else
		pr_debug("QG SDAM read param=%s value=%d\n",
				sdam_info[param].name, *data);

	return rc;
}

int qg_sdam_multibyte_write(u32 offset, u8 *data, u32 length)
{
	int rc, i;
	struct qg_sdam *chip = the_chip;

	if (!chip) {
		pr_err("Invalid sdam-chip pointer\n");
		return -EINVAL;
	}

	offset = chip->sdam_base + offset;
	rc = regmap_bulk_write(chip->regmap, offset, data, (size_t)length);
	if (rc < 0) {
		pr_err("Failed to write offset=%0x4 value=%d\n",
					offset, *data);
	} else {
		for (i = 0; i < length; i++)
			pr_debug("QG SDAM write offset=%0x4 value=%d\n",
					offset++, data[i]);
	}

	return rc;
}

int qg_sdam_multibyte_read(u32 offset, u8 *data, u32 length)
{
	int rc, i;
	struct qg_sdam *chip = the_chip;

	if (!chip) {
		pr_err("Invalid sdam-chip pointer\n");
		return -EINVAL;
	}

	offset = chip->sdam_base + offset;
	rc = regmap_raw_read(chip->regmap, offset, (u8 *)data, (size_t)length);
	if (rc < 0) {
		pr_err("Failed to read offset=%0x4\n", offset);
	} else {
		for (i = 0; i < length; i++)
			pr_debug("QG SDAM read offset=%0x4 value=%d\n",
					offset++, data[i]);
	}

	return rc;
}

int qg_sdam_read_all(u32 *sdam_data)
{
	int i, rc;
	struct qg_sdam *chip = the_chip;

	if (!chip) {
		pr_err("Invalid sdam-chip pointer\n");
		return -EINVAL;
	}

	for (i = 0; i < SDAM_MAX; i++) {
		rc = qg_sdam_read(i, &sdam_data[i]);
		if (rc < 0) {
			pr_err("Failed to read SDAM param=%s rc=%d\n",
					sdam_info[i].name, rc);
			return rc;
		}
	}

	return 0;
}

int qg_sdam_write_all(u32 *sdam_data)
{
	int i, rc;
	struct qg_sdam *chip = the_chip;

	if (!chip) {
		pr_err("Invalid sdam-chip pointer\n");
		return -EINVAL;
	}

	for (i = 0; i < SDAM_MAX; i++) {
		rc = qg_sdam_write(i, sdam_data[i]);
		if (rc < 0) {
			pr_err("Failed to write SDAM param=%s rc=%d\n",
					sdam_info[i].name, rc);
			return rc;
		}
	}

	return 0;
}

int qg_sdam_clear(void)
{
	int i, rc = 0;
	struct qg_sdam *chip = the_chip;
	u8 data = 0;

	if (!chip) {
		pr_err("Invalid sdam-chip pointer\n");
		return -EINVAL;
	}

	for (i = SDAM_MIN_OFFSET; i <= SDAM_MAX_OFFSET; i++)
		rc |= qg_sdam_multibyte_write(i, &data, 1);

	return rc;
}

int qg_sdam_init(struct device *dev)
{
	int rc;
	u32 base = 0, type = 0;
	struct qg_sdam *chip;
	struct device_node *child, *node = dev->of_node;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return 0;

	chip->regmap = dev_get_regmap(dev->parent, NULL);
	if (!chip->regmap) {
		pr_err("Parent regmap is unavailable\n");
		return -ENXIO;
	}

	/* get the SDAM base address */
	for_each_available_child_of_node(node, child) {
		rc = of_property_read_u32(child, "reg", &base);
		if (rc < 0) {
			pr_err("Failed to read base address rc=%d\n", rc);
			return rc;
		}

		rc = regmap_read(chip->regmap, base + PERPH_TYPE_REG, &type);
		if (rc < 0) {
			pr_err("Failed to read type rc=%d\n", rc);
			return rc;
		}

		switch (type) {
		case SDAM_TYPE:
			chip->sdam_base = base;
			break;
		default:
			break;
		}
	}
	if (!chip->sdam_base) {
		pr_err("QG SDAM node not defined\n");
		return -EINVAL;
	}

	the_chip = chip;

	return 0;
}
