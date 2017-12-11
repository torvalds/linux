/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __INTEL_PMIC_H
#define __INTEL_PMIC_H

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
	struct pmic_table *power_table;
	int power_table_count;
	struct pmic_table *thermal_table;
	int thermal_table_count;
};

int intel_pmic_install_opregion_handler(struct device *dev, acpi_handle handle, struct regmap *regmap, struct intel_pmic_opregion_data *d);

#endif
