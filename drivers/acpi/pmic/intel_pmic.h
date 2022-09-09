/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __INTEL_PMIC_H
#define __INTEL_PMIC_H

#include <acpi/acpi_lpat.h>

struct pmic_table {
	int address;	/* operation region address */
	int reg;	/* corresponding thermal register */
	int bit;	/* control bit for power */
};

struct intel_pmic_opregion_data {
	int (*get_power)(struct regmap *r, int reg, int bit, u64 *value);
	int (*update_power)(struct regmap *r, int reg, int bit, bool on);
	int (*get_raw_temp)(struct regmap *r, int reg);
	int (*update_aux)(struct regmap *r, int reg, int raw_temp);
	int (*get_policy)(struct regmap *r, int reg, int bit, u64 *value);
	int (*update_policy)(struct regmap *r, int reg, int bit, int enable);
	int (*exec_mipi_pmic_seq_element)(struct regmap *r, u16 i2c_address,
					  u32 reg_address, u32 value, u32 mask);
	int (*lpat_raw_to_temp)(struct acpi_lpat_conversion_table *lpat_table,
				int raw);
	struct pmic_table *power_table;
	int power_table_count;
	struct pmic_table *thermal_table;
	int thermal_table_count;
	/* For generic exec_mipi_pmic_seq_element handling */
	int pmic_i2c_address;
};

int intel_pmic_install_opregion_handler(struct device *dev, acpi_handle handle,
					struct regmap *regmap,
					const struct intel_pmic_opregion_data *d);

#endif
