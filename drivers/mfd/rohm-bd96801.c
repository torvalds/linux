// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2024 ROHM Semiconductors
 *
 * ROHM BD96801 PMIC driver
 *
 * This version of the "BD86801 scalable PMIC"'s driver supports only very
 * basic set of the PMIC features.
 * Most notably, there is no support for the configurations which should
 * be done when the PMIC is in STBY mode.
 *
 * Being able to reliably do the configurations like changing the
 * regulator safety limits (like limits for the over/under -voltages, over
 * current, thermal protection) would require the configuring driver to be
 * synchronized with entity causing the PMIC state transitions. Eg, one
 * should be able to ensure the PMIC is in STBY state when the
 * configurations are applied to the hardware. How and when the PMIC state
 * transitions are to be done is likely to be very system specific, as will
 * be the need to configure these safety limits. Hence it's not simple to
 * come up with a generic solution.
 *
 * Users who require the STBY state configurations can  have a look at the
 * original RFC:
 * https://lore.kernel.org/all/cover.1712920132.git.mazziesaccount@gmail.com/
 * which implements some of the safety limit configurations - but leaves the
 * state change handling and synchronization to be implemented.
 *
 * It would be great to hear (and receive a patch!) if you implement the
 * STBY configuration support or a proper fix in your downstream driver ;)
 */

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include <linux/mfd/rohm-bd96801.h>
#include <linux/mfd/rohm-bd96802.h>
#include <linux/mfd/rohm-generic.h>

struct bd968xx {
	const struct resource *errb_irqs;
	const struct resource *intb_irqs;
	int num_errb_irqs;
	int num_intb_irqs;
	const struct regmap_irq_chip *errb_irq_chip;
	const struct regmap_irq_chip *intb_irq_chip;
	const struct regmap_config *regmap_config;
	struct mfd_cell *cells;
	int num_cells;
	int unlock_reg;
	int unlock_val;
};

static const struct resource bd96801_reg_errb_irqs[] = {
	DEFINE_RES_IRQ_NAMED(BD96801_OTP_ERR_STAT, "otp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_DBIST_ERR_STAT, "dbist-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_EEP_ERR_STAT, "eep-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_ABIST_ERR_STAT, "abist-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_PRSTB_ERR_STAT, "prstb-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_DRMOS1_ERR_STAT, "drmoserr1"),
	DEFINE_RES_IRQ_NAMED(BD96801_DRMOS2_ERR_STAT, "drmoserr2"),
	DEFINE_RES_IRQ_NAMED(BD96801_SLAVE_ERR_STAT, "slave-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_VREF_ERR_STAT, "vref-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_TSD_ERR_STAT, "tsd"),
	DEFINE_RES_IRQ_NAMED(BD96801_UVLO_ERR_STAT, "uvlo-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_OVLO_ERR_STAT, "ovlo-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_OSC_ERR_STAT, "osc-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_PON_ERR_STAT, "pon-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_POFF_ERR_STAT, "poff-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_CMD_SHDN_ERR_STAT, "cmd-shdn-err"),

	DEFINE_RES_IRQ_NAMED(BD96801_INT_PRSTB_WDT_ERR, "bd96801-prstb-wdt-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_INT_CHIP_IF_ERR, "bd96801-chip-if-err"),

	DEFINE_RES_IRQ_NAMED(BD96801_INT_SHDN_ERR_STAT, "int-shdn-err"),

	DEFINE_RES_IRQ_NAMED(BD96801_BUCK1_PVIN_ERR_STAT, "buck1-pvin-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK1_OVP_ERR_STAT, "buck1-ovp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK1_UVP_ERR_STAT, "buck1-uvp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK1_SHDN_ERR_STAT, "buck1-shdn-err"),

	DEFINE_RES_IRQ_NAMED(BD96801_BUCK2_PVIN_ERR_STAT, "buck2-pvin-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK2_OVP_ERR_STAT, "buck2-ovp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK2_UVP_ERR_STAT, "buck2-uvp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK2_SHDN_ERR_STAT, "buck2-shdn-err"),

	DEFINE_RES_IRQ_NAMED(BD96801_BUCK3_PVIN_ERR_STAT, "buck3-pvin-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK3_OVP_ERR_STAT, "buck3-ovp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK3_UVP_ERR_STAT, "buck3-uvp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK3_SHDN_ERR_STAT, "buck3-shdn-err"),

	DEFINE_RES_IRQ_NAMED(BD96801_BUCK4_PVIN_ERR_STAT, "buck4-pvin-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK4_OVP_ERR_STAT, "buck4-ovp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK4_UVP_ERR_STAT, "buck4-uvp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK4_SHDN_ERR_STAT, "buck4-shdn-err"),

	DEFINE_RES_IRQ_NAMED(BD96801_LDO5_PVIN_ERR_STAT, "ldo5-pvin-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO5_OVP_ERR_STAT, "ldo5-ovp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO5_UVP_ERR_STAT, "ldo5-uvp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO5_SHDN_ERR_STAT, "ldo5-shdn-err"),

	DEFINE_RES_IRQ_NAMED(BD96801_LDO6_PVIN_ERR_STAT, "ldo6-pvin-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO6_OVP_ERR_STAT, "ldo6-ovp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO6_UVP_ERR_STAT, "ldo6-uvp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO6_SHDN_ERR_STAT, "ldo6-shdn-err"),

	DEFINE_RES_IRQ_NAMED(BD96801_LDO7_PVIN_ERR_STAT, "ldo7-pvin-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO7_OVP_ERR_STAT, "ldo7-ovp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO7_UVP_ERR_STAT, "ldo7-uvp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO7_SHDN_ERR_STAT, "ldo7-shdn-err"),
};

static const struct resource bd96802_reg_errb_irqs[] = {
	DEFINE_RES_IRQ_NAMED(BD96802_OTP_ERR_STAT, "otp-err"),
	DEFINE_RES_IRQ_NAMED(BD96802_DBIST_ERR_STAT, "dbist-err"),
	DEFINE_RES_IRQ_NAMED(BD96802_EEP_ERR_STAT, "eep-err"),
	DEFINE_RES_IRQ_NAMED(BD96802_ABIST_ERR_STAT, "abist-err"),
	DEFINE_RES_IRQ_NAMED(BD96802_PRSTB_ERR_STAT, "prstb-err"),
	DEFINE_RES_IRQ_NAMED(BD96802_DRMOS1_ERR_STAT, "drmoserr1"),
	DEFINE_RES_IRQ_NAMED(BD96802_DRMOS1_ERR_STAT, "drmoserr2"),
	DEFINE_RES_IRQ_NAMED(BD96802_SLAVE_ERR_STAT, "slave-err"),
	DEFINE_RES_IRQ_NAMED(BD96802_VREF_ERR_STAT, "vref-err"),
	DEFINE_RES_IRQ_NAMED(BD96802_TSD_ERR_STAT, "tsd"),
	DEFINE_RES_IRQ_NAMED(BD96802_UVLO_ERR_STAT, "uvlo-err"),
	DEFINE_RES_IRQ_NAMED(BD96802_OVLO_ERR_STAT, "ovlo-err"),
	DEFINE_RES_IRQ_NAMED(BD96802_OSC_ERR_STAT, "osc-err"),
	DEFINE_RES_IRQ_NAMED(BD96802_PON_ERR_STAT, "pon-err"),
	DEFINE_RES_IRQ_NAMED(BD96802_POFF_ERR_STAT, "poff-err"),
	DEFINE_RES_IRQ_NAMED(BD96802_CMD_SHDN_ERR_STAT, "cmd-shdn-err"),
	DEFINE_RES_IRQ_NAMED(BD96802_INT_SHDN_ERR_STAT, "int-shdn-err"),

	DEFINE_RES_IRQ_NAMED(BD96802_BUCK1_PVIN_ERR_STAT, "buck1-pvin-err"),
	DEFINE_RES_IRQ_NAMED(BD96802_BUCK1_OVP_ERR_STAT, "buck1-ovp-err"),
	DEFINE_RES_IRQ_NAMED(BD96802_BUCK1_UVP_ERR_STAT, "buck1-uvp-err"),
	DEFINE_RES_IRQ_NAMED(BD96802_BUCK1_SHDN_ERR_STAT, "buck1-shdn-err"),

	DEFINE_RES_IRQ_NAMED(BD96802_BUCK2_PVIN_ERR_STAT, "buck2-pvin-err"),
	DEFINE_RES_IRQ_NAMED(BD96802_BUCK2_OVP_ERR_STAT, "buck2-ovp-err"),
	DEFINE_RES_IRQ_NAMED(BD96802_BUCK2_UVP_ERR_STAT, "buck2-uvp-err"),
	DEFINE_RES_IRQ_NAMED(BD96802_BUCK2_SHDN_ERR_STAT, "buck2-shdn-err"),
};

static const struct resource bd96801_reg_intb_irqs[] = {
	DEFINE_RES_IRQ_NAMED(BD96801_TW_STAT, "core-thermal"),

	DEFINE_RES_IRQ_NAMED(BD96801_BUCK1_OCPH_STAT, "buck1-overcurr-h"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK1_OCPL_STAT, "buck1-overcurr-l"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK1_OCPN_STAT, "buck1-overcurr-n"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK1_OVD_STAT, "buck1-overvolt"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK1_UVD_STAT, "buck1-undervolt"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK1_TW_CH_STAT, "buck1-thermal"),

	DEFINE_RES_IRQ_NAMED(BD96801_BUCK2_OCPH_STAT, "buck2-overcurr-h"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK2_OCPL_STAT, "buck2-overcurr-l"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK2_OCPN_STAT, "buck2-overcurr-n"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK2_OVD_STAT, "buck2-overvolt"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK2_UVD_STAT, "buck2-undervolt"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK2_TW_CH_STAT, "buck2-thermal"),

	DEFINE_RES_IRQ_NAMED(BD96801_BUCK3_OCPH_STAT, "buck3-overcurr-h"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK3_OCPL_STAT, "buck3-overcurr-l"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK3_OCPN_STAT, "buck3-overcurr-n"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK3_OVD_STAT, "buck3-overvolt"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK3_UVD_STAT, "buck3-undervolt"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK3_TW_CH_STAT, "buck3-thermal"),

	DEFINE_RES_IRQ_NAMED(BD96801_BUCK4_OCPH_STAT, "buck4-overcurr-h"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK4_OCPL_STAT, "buck4-overcurr-l"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK4_OCPN_STAT, "buck4-overcurr-n"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK4_OVD_STAT, "buck4-overvolt"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK4_UVD_STAT, "buck4-undervolt"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK4_TW_CH_STAT, "buck4-thermal"),

	DEFINE_RES_IRQ_NAMED(BD96801_LDO5_OCPH_STAT, "ldo5-overcurr"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO5_OVD_STAT, "ldo5-overvolt"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO5_UVD_STAT, "ldo5-undervolt"),

	DEFINE_RES_IRQ_NAMED(BD96801_LDO6_OCPH_STAT, "ldo6-overcurr"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO6_OVD_STAT, "ldo6-overvolt"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO6_UVD_STAT, "ldo6-undervolt"),

	DEFINE_RES_IRQ_NAMED(BD96801_LDO7_OCPH_STAT, "ldo7-overcurr"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO7_OVD_STAT, "ldo7-overvolt"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO7_UVD_STAT, "ldo7-undervolt"),
};

static const struct resource bd96802_reg_intb_irqs[] = {
	DEFINE_RES_IRQ_NAMED(BD96802_TW_STAT, "core-thermal"),

	DEFINE_RES_IRQ_NAMED(BD96802_BUCK1_OCPH_STAT, "buck1-overcurr-h"),
	DEFINE_RES_IRQ_NAMED(BD96802_BUCK1_OCPL_STAT, "buck1-overcurr-l"),
	DEFINE_RES_IRQ_NAMED(BD96802_BUCK1_OCPN_STAT, "buck1-overcurr-n"),
	DEFINE_RES_IRQ_NAMED(BD96802_BUCK1_OVD_STAT, "buck1-overvolt"),
	DEFINE_RES_IRQ_NAMED(BD96802_BUCK1_UVD_STAT, "buck1-undervolt"),
	DEFINE_RES_IRQ_NAMED(BD96802_BUCK1_TW_CH_STAT, "buck1-thermal"),

	DEFINE_RES_IRQ_NAMED(BD96802_BUCK2_OCPH_STAT, "buck2-overcurr-h"),
	DEFINE_RES_IRQ_NAMED(BD96802_BUCK2_OCPL_STAT, "buck2-overcurr-l"),
	DEFINE_RES_IRQ_NAMED(BD96802_BUCK2_OCPN_STAT, "buck2-overcurr-n"),
	DEFINE_RES_IRQ_NAMED(BD96802_BUCK2_OVD_STAT, "buck2-overvolt"),
	DEFINE_RES_IRQ_NAMED(BD96802_BUCK2_UVD_STAT, "buck2-undervolt"),
	DEFINE_RES_IRQ_NAMED(BD96802_BUCK2_TW_CH_STAT, "buck2-thermal"),
};

enum {
	WDG_CELL = 0,
	REGULATOR_CELL,
};

static struct mfd_cell bd96801_cells[] = {
	[WDG_CELL] = { .name = "bd96801-wdt", },
	[REGULATOR_CELL] = { .name = "bd96801-regulator", },
};

static struct mfd_cell bd96802_cells[] = {
	[WDG_CELL] = { .name = "bd96801-wdt", },
	[REGULATOR_CELL] = { .name = "bd96802-regulator", },
};
static struct mfd_cell bd96805_cells[] = {
	[WDG_CELL] = { .name = "bd96801-wdt", },
	[REGULATOR_CELL] = { .name = "bd96805-regulator", },
};

static struct mfd_cell bd96806_cells[] = {
	[WDG_CELL] = { .name = "bd96806-wdt", },
	[REGULATOR_CELL] = { .name = "bd96806-regulator", },
};

static const struct regmap_range bd96801_volatile_ranges[] = {
	/* Status registers */
	regmap_reg_range(BD96801_REG_WD_FEED, BD96801_REG_WD_FAILCOUNT),
	regmap_reg_range(BD96801_REG_WD_ASK, BD96801_REG_WD_ASK),
	regmap_reg_range(BD96801_REG_WD_STATUS, BD96801_REG_WD_STATUS),
	regmap_reg_range(BD96801_REG_PMIC_STATE, BD96801_REG_INT_LDO7_INTB),
	/* Registers which do not update value unless PMIC is in STBY */
	regmap_reg_range(BD96801_REG_SSCG_CTRL, BD96801_REG_SHD_INTB),
	regmap_reg_range(BD96801_REG_BUCK_OVP, BD96801_REG_BOOT_OVERTIME),
	/*
	 * LDO control registers have single bit (LDO MODE) which does not
	 * change when we write it unless PMIC is in STBY. It's safer to not
	 * cache it.
	 */
	regmap_reg_range(BD96801_LDO5_VOL_LVL_REG, BD96801_LDO7_VOL_LVL_REG),
};

static const struct regmap_range bd96802_volatile_ranges[] = {
	/* Status regs */
	regmap_reg_range(BD96801_REG_WD_FEED, BD96801_REG_WD_FAILCOUNT),
	regmap_reg_range(BD96801_REG_WD_ASK, BD96801_REG_WD_ASK),
	regmap_reg_range(BD96801_REG_WD_STATUS, BD96801_REG_WD_STATUS),
	regmap_reg_range(BD96801_REG_PMIC_STATE, BD96801_REG_INT_BUCK2_ERRB),
	regmap_reg_range(BD96801_REG_INT_SYS_INTB, BD96801_REG_INT_BUCK2_INTB),
	/* Registers which do not update value unless PMIC is in STBY */
	regmap_reg_range(BD96801_REG_SSCG_CTRL, BD96801_REG_SHD_INTB),
	regmap_reg_range(BD96801_REG_BUCK_OVP, BD96801_REG_BOOT_OVERTIME),
};

static const struct regmap_access_table bd96801_volatile_regs = {
	.yes_ranges = bd96801_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(bd96801_volatile_ranges),
};

static const struct regmap_access_table bd96802_volatile_regs = {
	.yes_ranges = bd96802_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(bd96802_volatile_ranges),
};

/*
 * For ERRB we need main register bit mapping as bit(0) indicates active IRQ
 * in one of the first 3 sub IRQ registers, For INTB we can use default 1 to 1
 * mapping.
 */
static unsigned int bit0_offsets[] = {0, 1, 2};	/* System stat, 3 registers */
static unsigned int bit1_offsets[] = {3};	/* Buck 1 stat */
static unsigned int bit2_offsets[] = {4};	/* Buck 2 stat */
static unsigned int bit3_offsets[] = {5};	/* Buck 3 stat */
static unsigned int bit4_offsets[] = {6};	/* Buck 4 stat */
static unsigned int bit5_offsets[] = {7};	/* LDO 5 stat */
static unsigned int bit6_offsets[] = {8};	/* LDO 6 stat */
static unsigned int bit7_offsets[] = {9};	/* LDO 7 stat */

static const struct regmap_irq_sub_irq_map bd96801_errb_sub_irq_offsets[] = {
	REGMAP_IRQ_MAIN_REG_OFFSET(bit0_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit1_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit2_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit3_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit4_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit5_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit6_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit7_offsets),
};

static const struct regmap_irq_sub_irq_map bd96802_errb_sub_irq_offsets[] = {
	REGMAP_IRQ_MAIN_REG_OFFSET(bit0_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit1_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit2_offsets),
};

static const struct regmap_irq bd96801_errb_irqs[] = {
	/* Reg 0x52 Fatal ERRB1 */
	REGMAP_IRQ_REG(BD96801_OTP_ERR_STAT, 0, BD96801_OTP_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_DBIST_ERR_STAT, 0, BD96801_DBIST_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_EEP_ERR_STAT, 0, BD96801_EEP_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_ABIST_ERR_STAT, 0, BD96801_ABIST_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_PRSTB_ERR_STAT, 0, BD96801_PRSTB_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_DRMOS1_ERR_STAT, 0, BD96801_DRMOS1_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_DRMOS2_ERR_STAT, 0, BD96801_DRMOS2_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_SLAVE_ERR_STAT, 0, BD96801_SLAVE_ERR_MASK),
	/* 0x53 Fatal ERRB2 */
	REGMAP_IRQ_REG(BD96801_VREF_ERR_STAT, 1, BD96801_VREF_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_TSD_ERR_STAT, 1, BD96801_TSD_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_UVLO_ERR_STAT, 1, BD96801_UVLO_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_OVLO_ERR_STAT, 1, BD96801_OVLO_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_OSC_ERR_STAT, 1, BD96801_OSC_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_PON_ERR_STAT, 1, BD96801_PON_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_POFF_ERR_STAT, 1, BD96801_POFF_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_CMD_SHDN_ERR_STAT, 1, BD96801_CMD_SHDN_ERR_MASK),
	/* 0x54 Fatal INTB shadowed to ERRB */
	REGMAP_IRQ_REG(BD96801_INT_PRSTB_WDT_ERR, 2, BD96801_INT_PRSTB_WDT_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_INT_CHIP_IF_ERR, 2, BD96801_INT_CHIP_IF_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_INT_SHDN_ERR_STAT, 2, BD96801_INT_SHDN_ERR_MASK),
	/* Reg 0x55 BUCK1 ERR IRQs */
	REGMAP_IRQ_REG(BD96801_BUCK1_PVIN_ERR_STAT, 3, BD96801_OUT_PVIN_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK1_OVP_ERR_STAT, 3, BD96801_OUT_OVP_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK1_UVP_ERR_STAT, 3, BD96801_OUT_UVP_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK1_SHDN_ERR_STAT, 3, BD96801_OUT_SHDN_ERR_MASK),
	/* Reg 0x56 BUCK2 ERR IRQs */
	REGMAP_IRQ_REG(BD96801_BUCK2_PVIN_ERR_STAT, 4, BD96801_OUT_PVIN_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK2_OVP_ERR_STAT, 4, BD96801_OUT_OVP_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK2_UVP_ERR_STAT, 4, BD96801_OUT_UVP_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK2_SHDN_ERR_STAT, 4, BD96801_OUT_SHDN_ERR_MASK),
	/* Reg 0x57 BUCK3 ERR IRQs */
	REGMAP_IRQ_REG(BD96801_BUCK3_PVIN_ERR_STAT, 5, BD96801_OUT_PVIN_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK3_OVP_ERR_STAT, 5, BD96801_OUT_OVP_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK3_UVP_ERR_STAT, 5, BD96801_OUT_UVP_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK3_SHDN_ERR_STAT, 5, BD96801_OUT_SHDN_ERR_MASK),
	/* Reg 0x58 BUCK4 ERR IRQs */
	REGMAP_IRQ_REG(BD96801_BUCK4_PVIN_ERR_STAT, 6, BD96801_OUT_PVIN_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK4_OVP_ERR_STAT, 6, BD96801_OUT_OVP_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK4_UVP_ERR_STAT, 6, BD96801_OUT_UVP_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK4_SHDN_ERR_STAT, 6, BD96801_OUT_SHDN_ERR_MASK),
	/* Reg 0x59 LDO5 ERR IRQs */
	REGMAP_IRQ_REG(BD96801_LDO5_PVIN_ERR_STAT, 7, BD96801_OUT_PVIN_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_LDO5_OVP_ERR_STAT, 7, BD96801_OUT_OVP_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_LDO5_UVP_ERR_STAT, 7, BD96801_OUT_UVP_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_LDO5_SHDN_ERR_STAT, 7, BD96801_OUT_SHDN_ERR_MASK),
	/* Reg 0x5a LDO6 ERR IRQs */
	REGMAP_IRQ_REG(BD96801_LDO6_PVIN_ERR_STAT, 8, BD96801_OUT_PVIN_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_LDO6_OVP_ERR_STAT, 8, BD96801_OUT_OVP_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_LDO6_UVP_ERR_STAT, 8, BD96801_OUT_UVP_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_LDO6_SHDN_ERR_STAT, 8, BD96801_OUT_SHDN_ERR_MASK),
	/* Reg 0x5b LDO7 ERR IRQs */
	REGMAP_IRQ_REG(BD96801_LDO7_PVIN_ERR_STAT, 9, BD96801_OUT_PVIN_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_LDO7_OVP_ERR_STAT, 9, BD96801_OUT_OVP_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_LDO7_UVP_ERR_STAT, 9, BD96801_OUT_UVP_ERR_MASK),
	REGMAP_IRQ_REG(BD96801_LDO7_SHDN_ERR_STAT, 9, BD96801_OUT_SHDN_ERR_MASK),
};

static const struct regmap_irq bd96802_errb_irqs[] = {
	/* Reg 0x52 Fatal ERRB1 */
	REGMAP_IRQ_REG(BD96802_OTP_ERR_STAT, 0, BD96801_OTP_ERR_MASK),
	REGMAP_IRQ_REG(BD96802_DBIST_ERR_STAT, 0, BD96801_DBIST_ERR_MASK),
	REGMAP_IRQ_REG(BD96802_EEP_ERR_STAT, 0, BD96801_EEP_ERR_MASK),
	REGMAP_IRQ_REG(BD96802_ABIST_ERR_STAT, 0, BD96801_ABIST_ERR_MASK),
	REGMAP_IRQ_REG(BD96802_PRSTB_ERR_STAT, 0, BD96801_PRSTB_ERR_MASK),
	REGMAP_IRQ_REG(BD96802_DRMOS1_ERR_STAT, 0, BD96801_DRMOS1_ERR_MASK),
	REGMAP_IRQ_REG(BD96802_DRMOS2_ERR_STAT, 0, BD96801_DRMOS2_ERR_MASK),
	REGMAP_IRQ_REG(BD96802_SLAVE_ERR_STAT, 0, BD96801_SLAVE_ERR_MASK),
	/* 0x53 Fatal ERRB2 */
	REGMAP_IRQ_REG(BD96802_VREF_ERR_STAT, 1, BD96801_VREF_ERR_MASK),
	REGMAP_IRQ_REG(BD96802_TSD_ERR_STAT, 1, BD96801_TSD_ERR_MASK),
	REGMAP_IRQ_REG(BD96802_UVLO_ERR_STAT, 1, BD96801_UVLO_ERR_MASK),
	REGMAP_IRQ_REG(BD96802_OVLO_ERR_STAT, 1, BD96801_OVLO_ERR_MASK),
	REGMAP_IRQ_REG(BD96802_OSC_ERR_STAT, 1, BD96801_OSC_ERR_MASK),
	REGMAP_IRQ_REG(BD96802_PON_ERR_STAT, 1, BD96801_PON_ERR_MASK),
	REGMAP_IRQ_REG(BD96802_POFF_ERR_STAT, 1, BD96801_POFF_ERR_MASK),
	REGMAP_IRQ_REG(BD96802_CMD_SHDN_ERR_STAT, 1, BD96801_CMD_SHDN_ERR_MASK),
	/* 0x54 Fatal INTB shadowed to ERRB */
	REGMAP_IRQ_REG(BD96802_INT_SHDN_ERR_STAT, 2, BD96801_INT_SHDN_ERR_MASK),
	/* Reg 0x55 BUCK1 ERR IRQs */
	REGMAP_IRQ_REG(BD96802_BUCK1_PVIN_ERR_STAT, 3, BD96801_OUT_PVIN_ERR_MASK),
	REGMAP_IRQ_REG(BD96802_BUCK1_OVP_ERR_STAT, 3, BD96801_OUT_OVP_ERR_MASK),
	REGMAP_IRQ_REG(BD96802_BUCK1_UVP_ERR_STAT, 3, BD96801_OUT_UVP_ERR_MASK),
	REGMAP_IRQ_REG(BD96802_BUCK1_SHDN_ERR_STAT, 3, BD96801_OUT_SHDN_ERR_MASK),
	/* Reg 0x56 BUCK2 ERR IRQs */
	REGMAP_IRQ_REG(BD96802_BUCK2_PVIN_ERR_STAT, 4, BD96801_OUT_PVIN_ERR_MASK),
	REGMAP_IRQ_REG(BD96802_BUCK2_OVP_ERR_STAT, 4, BD96801_OUT_OVP_ERR_MASK),
	REGMAP_IRQ_REG(BD96802_BUCK2_UVP_ERR_STAT, 4, BD96801_OUT_UVP_ERR_MASK),
	REGMAP_IRQ_REG(BD96802_BUCK2_SHDN_ERR_STAT, 4, BD96801_OUT_SHDN_ERR_MASK),
};

static const struct regmap_irq bd96801_intb_irqs[] = {
	/* STATUS SYSTEM INTB */
	REGMAP_IRQ_REG(BD96801_TW_STAT, 0, BD96801_TW_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_WDT_ERR_STAT, 0, BD96801_WDT_ERR_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_I2C_ERR_STAT, 0, BD96801_I2C_ERR_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_CHIP_IF_ERR_STAT, 0, BD96801_CHIP_IF_ERR_STAT_MASK),
	/* STATUS BUCK1 INTB */
	REGMAP_IRQ_REG(BD96801_BUCK1_OCPH_STAT, 1, BD96801_BUCK_OCPH_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK1_OCPL_STAT, 1, BD96801_BUCK_OCPL_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK1_OCPN_STAT, 1, BD96801_BUCK_OCPN_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK1_OVD_STAT, 1, BD96801_BUCK_OVD_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK1_UVD_STAT, 1, BD96801_BUCK_UVD_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK1_TW_CH_STAT, 1, BD96801_BUCK_TW_CH_STAT_MASK),
	/* BUCK 2 INTB */
	REGMAP_IRQ_REG(BD96801_BUCK2_OCPH_STAT, 2, BD96801_BUCK_OCPH_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK2_OCPL_STAT, 2, BD96801_BUCK_OCPL_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK2_OCPN_STAT, 2, BD96801_BUCK_OCPN_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK2_OVD_STAT, 2, BD96801_BUCK_OVD_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK2_UVD_STAT, 2, BD96801_BUCK_UVD_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK2_TW_CH_STAT, 2, BD96801_BUCK_TW_CH_STAT_MASK),
	/* BUCK 3 INTB */
	REGMAP_IRQ_REG(BD96801_BUCK3_OCPH_STAT, 3, BD96801_BUCK_OCPH_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK3_OCPL_STAT, 3, BD96801_BUCK_OCPL_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK3_OCPN_STAT, 3, BD96801_BUCK_OCPN_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK3_OVD_STAT, 3, BD96801_BUCK_OVD_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK3_UVD_STAT, 3, BD96801_BUCK_UVD_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK3_TW_CH_STAT, 3, BD96801_BUCK_TW_CH_STAT_MASK),
	/* BUCK 4 INTB */
	REGMAP_IRQ_REG(BD96801_BUCK4_OCPH_STAT, 4, BD96801_BUCK_OCPH_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK4_OCPL_STAT, 4, BD96801_BUCK_OCPL_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK4_OCPN_STAT, 4, BD96801_BUCK_OCPN_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK4_OVD_STAT, 4, BD96801_BUCK_OVD_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK4_UVD_STAT, 4, BD96801_BUCK_UVD_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_BUCK4_TW_CH_STAT, 4, BD96801_BUCK_TW_CH_STAT_MASK),
	/* LDO5 INTB */
	REGMAP_IRQ_REG(BD96801_LDO5_OCPH_STAT, 5, BD96801_LDO_OCPH_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_LDO5_OVD_STAT, 5, BD96801_LDO_OVD_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_LDO5_UVD_STAT, 5, BD96801_LDO_UVD_STAT_MASK),
	/* LDO6 INTB */
	REGMAP_IRQ_REG(BD96801_LDO6_OCPH_STAT, 6, BD96801_LDO_OCPH_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_LDO6_OVD_STAT, 6, BD96801_LDO_OVD_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_LDO6_UVD_STAT, 6, BD96801_LDO_UVD_STAT_MASK),
	/* LDO7 INTB */
	REGMAP_IRQ_REG(BD96801_LDO7_OCPH_STAT, 7, BD96801_LDO_OCPH_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_LDO7_OVD_STAT, 7, BD96801_LDO_OVD_STAT_MASK),
	REGMAP_IRQ_REG(BD96801_LDO7_UVD_STAT, 7, BD96801_LDO_UVD_STAT_MASK),
};

static const struct regmap_irq bd96802_intb_irqs[] = {
	/* STATUS SYSTEM INTB */
	REGMAP_IRQ_REG(BD96802_TW_STAT, 0, BD96801_TW_STAT_MASK),
	REGMAP_IRQ_REG(BD96802_WDT_ERR_STAT, 0, BD96801_WDT_ERR_STAT_MASK),
	REGMAP_IRQ_REG(BD96802_I2C_ERR_STAT, 0, BD96801_I2C_ERR_STAT_MASK),
	REGMAP_IRQ_REG(BD96802_CHIP_IF_ERR_STAT, 0, BD96801_CHIP_IF_ERR_STAT_MASK),
	/* STATUS BUCK1 INTB */
	REGMAP_IRQ_REG(BD96802_BUCK1_OCPH_STAT, 1, BD96801_BUCK_OCPH_STAT_MASK),
	REGMAP_IRQ_REG(BD96802_BUCK1_OCPL_STAT, 1, BD96801_BUCK_OCPL_STAT_MASK),
	REGMAP_IRQ_REG(BD96802_BUCK1_OCPN_STAT, 1, BD96801_BUCK_OCPN_STAT_MASK),
	REGMAP_IRQ_REG(BD96802_BUCK1_OVD_STAT, 1, BD96801_BUCK_OVD_STAT_MASK),
	REGMAP_IRQ_REG(BD96802_BUCK1_UVD_STAT, 1, BD96801_BUCK_UVD_STAT_MASK),
	REGMAP_IRQ_REG(BD96802_BUCK1_TW_CH_STAT, 1, BD96801_BUCK_TW_CH_STAT_MASK),
	/* BUCK 2 INTB */
	REGMAP_IRQ_REG(BD96802_BUCK2_OCPH_STAT, 2, BD96801_BUCK_OCPH_STAT_MASK),
	REGMAP_IRQ_REG(BD96802_BUCK2_OCPL_STAT, 2, BD96801_BUCK_OCPL_STAT_MASK),
	REGMAP_IRQ_REG(BD96802_BUCK2_OCPN_STAT, 2, BD96801_BUCK_OCPN_STAT_MASK),
	REGMAP_IRQ_REG(BD96802_BUCK2_OVD_STAT, 2, BD96801_BUCK_OVD_STAT_MASK),
	REGMAP_IRQ_REG(BD96802_BUCK2_UVD_STAT, 2, BD96801_BUCK_UVD_STAT_MASK),
	REGMAP_IRQ_REG(BD96802_BUCK2_TW_CH_STAT, 2, BD96801_BUCK_TW_CH_STAT_MASK),
};

/*
 * The IRQ stuff is a bit hairy. The BD96801 / BD96802 provide two physical
 * IRQ lines called INTB and ERRB. They share the same main status register.
 *
 * For ERRB, mapping from main status to sub-status is such that the
 * 'global' faults are mapped to first 3 sub-status registers - and indicated
 * by the first bit[0] in main status reg.
 *
 * Rest of the status registers are for indicating stuff for individual
 * regulators, 1 sub register / regulator and 1 main status register bit /
 * regulator, starting from bit[1].
 *
 * Eg, regulator specific stuff has 1 to 1 mapping from main-status to sub
 * registers but 'global' ERRB IRQs require mapping from main status bit[0] to
 * 3 status registers.
 *
 * Furthermore, the BD96801 has 7 regulators where the BD96802 has only 2.
 *
 * INTB has only 1 sub status register for 'global' events and then own sub
 * status register for each of the regulators. So, for INTB we have direct
 * 1 to 1 mapping - BD96801 just having 5 register and 5 main status bits
 * more than the BD96802.
 *
 * Sharing the main status bits could be a problem if we had both INTB and
 * ERRB IRQs asserted but for different sub-status offsets. This might lead
 * IRQ controller code to go read a sub status register which indicates no
 * active IRQs. I assume this occurring repeteadly might lead the IRQ to be
 * disabled by core as a result of repeteadly returned IRQ_NONEs.
 *
 * I don't consider this as a fatal problem for now because:
 *	a) Having ERRB asserted leads to PMIC fault state which will kill
 *	   the SoC powered by the PMIC. (So, relevant only for potential
 *	   case of not powering the processor with this PMIC).
 *	b) Having ERRB set without having respective INTB is unlikely
 *	   (haven't actually verified this).
 *
 * So, let's proceed with main status enabled for both INTB and ERRB. We can
 * later disable main-status usage on systems where this ever proves to be
 * a problem.
 */

static const struct regmap_irq_chip bd96801_irq_chip_errb = {
	.name = "bd96801-irq-errb",
	.domain_suffix = "errb",
	.main_status = BD96801_REG_INT_MAIN,
	.num_main_regs = 1,
	.irqs = &bd96801_errb_irqs[0],
	.num_irqs = ARRAY_SIZE(bd96801_errb_irqs),
	.status_base = BD96801_REG_INT_SYS_ERRB1,
	.mask_base = BD96801_REG_MASK_SYS_ERRB,
	.ack_base = BD96801_REG_INT_SYS_ERRB1,
	.init_ack_masked = true,
	.num_regs = 10,
	.irq_reg_stride = 1,
	.sub_reg_offsets = &bd96801_errb_sub_irq_offsets[0],
};

static const struct regmap_irq_chip bd96802_irq_chip_errb = {
	.name = "bd96802-irq-errb",
	.domain_suffix = "errb",
	.main_status = BD96801_REG_INT_MAIN,
	.num_main_regs = 1,
	.irqs = &bd96802_errb_irqs[0],
	.num_irqs = ARRAY_SIZE(bd96802_errb_irqs),
	.status_base = BD96801_REG_INT_SYS_ERRB1,
	.mask_base = BD96801_REG_MASK_SYS_ERRB,
	.ack_base = BD96801_REG_INT_SYS_ERRB1,
	.init_ack_masked = true,
	.num_regs = 5,
	.irq_reg_stride = 1,
	.sub_reg_offsets = &bd96802_errb_sub_irq_offsets[0],
};

static const struct regmap_irq_chip bd96801_irq_chip_intb = {
	.name = "bd96801-irq-intb",
	.domain_suffix = "intb",
	.main_status = BD96801_REG_INT_MAIN,
	.num_main_regs = 1,
	.irqs = &bd96801_intb_irqs[0],
	.num_irqs = ARRAY_SIZE(bd96801_intb_irqs),
	.status_base = BD96801_REG_INT_SYS_INTB,
	.mask_base = BD96801_REG_MASK_SYS_INTB,
	.ack_base = BD96801_REG_INT_SYS_INTB,
	.init_ack_masked = true,
	.num_regs = 8,
	.irq_reg_stride = 1,
};

static const struct regmap_irq_chip bd96802_irq_chip_intb = {
	.name = "bd96802-irq-intb",
	.domain_suffix = "intb",
	.main_status = BD96801_REG_INT_MAIN,
	.num_main_regs = 1,
	.irqs = &bd96802_intb_irqs[0],
	.num_irqs = ARRAY_SIZE(bd96802_intb_irqs),
	.status_base = BD96801_REG_INT_SYS_INTB,
	.mask_base = BD96801_REG_MASK_SYS_INTB,
	.ack_base = BD96801_REG_INT_SYS_INTB,
	.init_ack_masked = true,
	.num_regs = 3,
	.irq_reg_stride = 1,
};

static const struct regmap_config bd96801_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &bd96801_volatile_regs,
	.cache_type = REGCACHE_MAPLE,
};

static const struct regmap_config bd96802_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &bd96802_volatile_regs,
	.cache_type = REGCACHE_MAPLE,
};

static const struct bd968xx bd96801_data = {
	.errb_irqs = bd96801_reg_errb_irqs,
	.intb_irqs = bd96801_reg_intb_irqs,
	.num_errb_irqs = ARRAY_SIZE(bd96801_reg_errb_irqs),
	.num_intb_irqs = ARRAY_SIZE(bd96801_reg_intb_irqs),
	.errb_irq_chip = &bd96801_irq_chip_errb,
	.intb_irq_chip = &bd96801_irq_chip_intb,
	.regmap_config = &bd96801_regmap_config,
	.cells = bd96801_cells,
	.num_cells = ARRAY_SIZE(bd96801_cells),
	.unlock_reg = BD96801_LOCK_REG,
	.unlock_val = BD96801_UNLOCK,
};

static const struct bd968xx bd96802_data = {
	.errb_irqs = bd96802_reg_errb_irqs,
	.intb_irqs = bd96802_reg_intb_irqs,
	.num_errb_irqs = ARRAY_SIZE(bd96802_reg_errb_irqs),
	.num_intb_irqs = ARRAY_SIZE(bd96802_reg_intb_irqs),
	.errb_irq_chip = &bd96802_irq_chip_errb,
	.intb_irq_chip = &bd96802_irq_chip_intb,
	.regmap_config = &bd96802_regmap_config,
	.cells = bd96802_cells,
	.num_cells = ARRAY_SIZE(bd96802_cells),
	.unlock_reg = BD96801_LOCK_REG,
	.unlock_val = BD96801_UNLOCK,
};

static const struct bd968xx bd96805_data = {
	.errb_irqs = bd96801_reg_errb_irqs,
	.intb_irqs = bd96801_reg_intb_irqs,
	.num_errb_irqs = ARRAY_SIZE(bd96801_reg_errb_irqs),
	.num_intb_irqs = ARRAY_SIZE(bd96801_reg_intb_irqs),
	.errb_irq_chip = &bd96801_irq_chip_errb,
	.intb_irq_chip = &bd96801_irq_chip_intb,
	.regmap_config = &bd96801_regmap_config,
	.cells = bd96805_cells,
	.num_cells = ARRAY_SIZE(bd96805_cells),
	.unlock_reg = BD96801_LOCK_REG,
	.unlock_val = BD96801_UNLOCK,
};

static struct bd968xx bd96806_data = {
	.errb_irqs = bd96802_reg_errb_irqs,
	.intb_irqs = bd96802_reg_intb_irqs,
	.num_errb_irqs = ARRAY_SIZE(bd96802_reg_errb_irqs),
	.num_intb_irqs = ARRAY_SIZE(bd96802_reg_intb_irqs),
	.errb_irq_chip = &bd96802_irq_chip_errb,
	.intb_irq_chip = &bd96802_irq_chip_intb,
	.regmap_config = &bd96802_regmap_config,
	.cells = bd96806_cells,
	.num_cells = ARRAY_SIZE(bd96806_cells),
	.unlock_reg = BD96801_LOCK_REG,
	.unlock_val = BD96801_UNLOCK,
};

static int bd96801_i2c_probe(struct i2c_client *i2c)
{
	struct regmap_irq_chip_data *intb_irq_data, *errb_irq_data;
	struct irq_domain *intb_domain, *errb_domain;
	const struct bd968xx *ddata;
	const struct fwnode_handle *fwnode;
	struct resource *regulator_res;
	struct resource wdg_irq;
	struct regmap *regmap;
	int intb_irq, errb_irq, num_errb = 0;
	int num_regu_irqs, wdg_irq_no;
	unsigned int chip_type;
	int i, ret;

	chip_type = (unsigned int)(uintptr_t)device_get_match_data(&i2c->dev);
	switch (chip_type) {
	case ROHM_CHIP_TYPE_BD96801:
		ddata = &bd96801_data;
		break;
	case ROHM_CHIP_TYPE_BD96802:
		ddata = &bd96802_data;
		break;
	case ROHM_CHIP_TYPE_BD96805:
		ddata = &bd96805_data;
		break;
	case ROHM_CHIP_TYPE_BD96806:
		ddata = &bd96806_data;
		break;
	default:
		dev_err(&i2c->dev, "Unknown IC\n");
		return -EINVAL;
	}

	fwnode = dev_fwnode(&i2c->dev);
	if (!fwnode)
		return dev_err_probe(&i2c->dev, -EINVAL, "Failed to find fwnode\n");

	intb_irq = fwnode_irq_get_byname(fwnode, "intb");
	if (intb_irq < 0)
		return dev_err_probe(&i2c->dev, intb_irq, "INTB IRQ not configured\n");

	/* ERRB may be omitted if processor is powered by the PMIC */
	errb_irq = fwnode_irq_get_byname(fwnode, "errb");
	if (errb_irq == -EPROBE_DEFER)
		return errb_irq;

	if (errb_irq > 0)
		num_errb = ddata->num_errb_irqs;

	num_regu_irqs = ddata->num_intb_irqs + num_errb;

	regulator_res = devm_kcalloc(&i2c->dev, num_regu_irqs,
				     sizeof(*regulator_res), GFP_KERNEL);
	if (!regulator_res)
		return -ENOMEM;

	regmap = devm_regmap_init_i2c(i2c, ddata->regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(&i2c->dev, PTR_ERR(regmap),
				    "Regmap initialization failed\n");

	ret = regmap_write(regmap, ddata->unlock_reg, ddata->unlock_val);
	if (ret)
		return dev_err_probe(&i2c->dev, ret, "Failed to unlock PMIC\n");

	ret = devm_regmap_add_irq_chip(&i2c->dev, regmap, intb_irq,
				       IRQF_ONESHOT, 0, ddata->intb_irq_chip,
				       &intb_irq_data);
	if (ret)
		return dev_err_probe(&i2c->dev, ret, "Failed to add INTB IRQ chip\n");

	intb_domain = regmap_irq_get_domain(intb_irq_data);

	/*
	 * MFD core code is built to handle only one IRQ domain. BD96801
	 * has two domains so we do IRQ mapping here and provide the
	 * already mapped IRQ numbers to sub-devices.
	 */
	for (i = 0; i < ddata->num_intb_irqs; i++) {
		struct resource *res = &regulator_res[i];

		*res = ddata->intb_irqs[i];
		res->start = res->end = irq_create_mapping(intb_domain,
							    res->start);
	}

	wdg_irq_no = irq_create_mapping(intb_domain, BD96801_WDT_ERR_STAT);
	wdg_irq = DEFINE_RES_IRQ_NAMED(wdg_irq_no, "bd96801-wdg");

	ddata->cells[WDG_CELL].resources = &wdg_irq;
	ddata->cells[WDG_CELL].num_resources = 1;

	if (!num_errb)
		goto skip_errb;

	ret = devm_regmap_add_irq_chip(&i2c->dev, regmap, errb_irq, IRQF_ONESHOT,
				       0, ddata->errb_irq_chip, &errb_irq_data);
	if (ret)
		return dev_err_probe(&i2c->dev, ret,
				     "Failed to add ERRB IRQ chip\n");

	errb_domain = regmap_irq_get_domain(errb_irq_data);

	for (i = 0; i < num_errb; i++) {
		struct resource *res = &regulator_res[ddata->num_intb_irqs + i];

		*res = ddata->errb_irqs[i];
		res->start = res->end = irq_create_mapping(errb_domain, res->start);
	}

skip_errb:
	ddata->cells[REGULATOR_CELL].resources = regulator_res;
	ddata->cells[REGULATOR_CELL].num_resources = num_regu_irqs;
	ret = devm_mfd_add_devices(&i2c->dev, PLATFORM_DEVID_AUTO, ddata->cells,
				   ddata->num_cells, NULL, 0, NULL);
	if (ret)
		dev_err_probe(&i2c->dev, ret, "Failed to create subdevices\n");

	return ret;
}

static const struct of_device_id bd96801_of_match[] = {
	{ .compatible = "rohm,bd96801", .data = (void *)ROHM_CHIP_TYPE_BD96801 },
	{ .compatible = "rohm,bd96802", .data = (void *)ROHM_CHIP_TYPE_BD96802 },
	{ .compatible = "rohm,bd96805", .data = (void *)ROHM_CHIP_TYPE_BD96805 },
	{ .compatible = "rohm,bd96806", .data = (void *)ROHM_CHIP_TYPE_BD96806 },
	{ }
};
MODULE_DEVICE_TABLE(of, bd96801_of_match);

static struct i2c_driver bd96801_i2c_driver = {
	.driver = {
		.name = "rohm-bd96801",
		.of_match_table = bd96801_of_match,
	},
	.probe = bd96801_i2c_probe,
};

static int __init bd96801_i2c_init(void)
{
	return i2c_add_driver(&bd96801_i2c_driver);
}

/* Initialise early so consumer devices can complete system boot */
subsys_initcall(bd96801_i2c_init);

static void __exit bd96801_i2c_exit(void)
{
	i2c_del_driver(&bd96801_i2c_driver);
}
module_exit(bd96801_i2c_exit);

MODULE_AUTHOR("Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>");
MODULE_DESCRIPTION("ROHM BD9680X Power Management IC driver");
MODULE_LICENSE("GPL");
