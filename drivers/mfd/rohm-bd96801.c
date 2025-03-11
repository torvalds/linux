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
#include <linux/mfd/rohm-generic.h>

static const struct resource regulator_errb_irqs[] = {
	DEFINE_RES_IRQ_NAMED(BD96801_OTP_ERR_STAT, "bd96801-otp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_DBIST_ERR_STAT, "bd96801-dbist-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_EEP_ERR_STAT, "bd96801-eep-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_ABIST_ERR_STAT, "bd96801-abist-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_PRSTB_ERR_STAT, "bd96801-prstb-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_DRMOS1_ERR_STAT, "bd96801-drmoserr1"),
	DEFINE_RES_IRQ_NAMED(BD96801_DRMOS2_ERR_STAT, "bd96801-drmoserr2"),
	DEFINE_RES_IRQ_NAMED(BD96801_SLAVE_ERR_STAT, "bd96801-slave-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_VREF_ERR_STAT, "bd96801-vref-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_TSD_ERR_STAT, "bd96801-tsd"),
	DEFINE_RES_IRQ_NAMED(BD96801_UVLO_ERR_STAT, "bd96801-uvlo-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_OVLO_ERR_STAT, "bd96801-ovlo-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_OSC_ERR_STAT, "bd96801-osc-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_PON_ERR_STAT, "bd96801-pon-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_POFF_ERR_STAT, "bd96801-poff-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_CMD_SHDN_ERR_STAT, "bd96801-cmd-shdn-err"),

	DEFINE_RES_IRQ_NAMED(BD96801_INT_PRSTB_WDT_ERR, "bd96801-prstb-wdt-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_INT_CHIP_IF_ERR, "bd96801-chip-if-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_INT_SHDN_ERR_STAT, "bd96801-int-shdn-err"),

	DEFINE_RES_IRQ_NAMED(BD96801_BUCK1_PVIN_ERR_STAT, "bd96801-buck1-pvin-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK1_OVP_ERR_STAT, "bd96801-buck1-ovp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK1_UVP_ERR_STAT, "bd96801-buck1-uvp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK1_SHDN_ERR_STAT, "bd96801-buck1-shdn-err"),

	DEFINE_RES_IRQ_NAMED(BD96801_BUCK2_PVIN_ERR_STAT, "bd96801-buck2-pvin-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK2_OVP_ERR_STAT, "bd96801-buck2-ovp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK2_UVP_ERR_STAT, "bd96801-buck2-uvp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK2_SHDN_ERR_STAT, "bd96801-buck2-shdn-err"),

	DEFINE_RES_IRQ_NAMED(BD96801_BUCK3_PVIN_ERR_STAT, "bd96801-buck3-pvin-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK3_OVP_ERR_STAT, "bd96801-buck3-ovp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK3_UVP_ERR_STAT, "bd96801-buck3-uvp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK3_SHDN_ERR_STAT, "bd96801-buck3-shdn-err"),

	DEFINE_RES_IRQ_NAMED(BD96801_BUCK4_PVIN_ERR_STAT, "bd96801-buck4-pvin-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK4_OVP_ERR_STAT, "bd96801-buck4-ovp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK4_UVP_ERR_STAT, "bd96801-buck4-uvp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK4_SHDN_ERR_STAT, "bd96801-buck4-shdn-err"),

	DEFINE_RES_IRQ_NAMED(BD96801_LDO5_PVIN_ERR_STAT, "bd96801-ldo5-pvin-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO5_OVP_ERR_STAT, "bd96801-ldo5-ovp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO5_UVP_ERR_STAT, "bd96801-ldo5-uvp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO5_SHDN_ERR_STAT, "bd96801-ldo5-shdn-err"),

	DEFINE_RES_IRQ_NAMED(BD96801_LDO6_PVIN_ERR_STAT, "bd96801-ldo6-pvin-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO6_OVP_ERR_STAT, "bd96801-ldo6-ovp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO6_UVP_ERR_STAT, "bd96801-ldo6-uvp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO6_SHDN_ERR_STAT, "bd96801-ldo6-shdn-err"),

	DEFINE_RES_IRQ_NAMED(BD96801_LDO7_PVIN_ERR_STAT, "bd96801-ldo7-pvin-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO7_OVP_ERR_STAT, "bd96801-ldo7-ovp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO7_UVP_ERR_STAT, "bd96801-ldo7-uvp-err"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO7_SHDN_ERR_STAT, "bd96801-ldo7-shdn-err"),
};

static const struct resource regulator_intb_irqs[] = {
	DEFINE_RES_IRQ_NAMED(BD96801_TW_STAT, "bd96801-core-thermal"),

	DEFINE_RES_IRQ_NAMED(BD96801_BUCK1_OCPH_STAT, "bd96801-buck1-overcurr-h"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK1_OCPL_STAT, "bd96801-buck1-overcurr-l"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK1_OCPN_STAT, "bd96801-buck1-overcurr-n"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK1_OVD_STAT, "bd96801-buck1-overvolt"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK1_UVD_STAT, "bd96801-buck1-undervolt"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK1_TW_CH_STAT, "bd96801-buck1-thermal"),

	DEFINE_RES_IRQ_NAMED(BD96801_BUCK2_OCPH_STAT, "bd96801-buck2-overcurr-h"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK2_OCPL_STAT, "bd96801-buck2-overcurr-l"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK2_OCPN_STAT, "bd96801-buck2-overcurr-n"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK2_OVD_STAT, "bd96801-buck2-overvolt"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK2_UVD_STAT, "bd96801-buck2-undervolt"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK2_TW_CH_STAT, "bd96801-buck2-thermal"),

	DEFINE_RES_IRQ_NAMED(BD96801_BUCK3_OCPH_STAT, "bd96801-buck3-overcurr-h"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK3_OCPL_STAT, "bd96801-buck3-overcurr-l"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK3_OCPN_STAT, "bd96801-buck3-overcurr-n"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK3_OVD_STAT, "bd96801-buck3-overvolt"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK3_UVD_STAT, "bd96801-buck3-undervolt"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK3_TW_CH_STAT, "bd96801-buck3-thermal"),

	DEFINE_RES_IRQ_NAMED(BD96801_BUCK4_OCPH_STAT, "bd96801-buck4-overcurr-h"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK4_OCPL_STAT, "bd96801-buck4-overcurr-l"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK4_OCPN_STAT, "bd96801-buck4-overcurr-n"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK4_OVD_STAT, "bd96801-buck4-overvolt"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK4_UVD_STAT, "bd96801-buck4-undervolt"),
	DEFINE_RES_IRQ_NAMED(BD96801_BUCK4_TW_CH_STAT, "bd96801-buck4-thermal"),

	DEFINE_RES_IRQ_NAMED(BD96801_LDO5_OCPH_STAT, "bd96801-ldo5-overcurr"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO5_OVD_STAT, "bd96801-ldo5-overvolt"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO5_UVD_STAT, "bd96801-ldo5-undervolt"),

	DEFINE_RES_IRQ_NAMED(BD96801_LDO6_OCPH_STAT, "bd96801-ldo6-overcurr"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO6_OVD_STAT, "bd96801-ldo6-overvolt"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO6_UVD_STAT, "bd96801-ldo6-undervolt"),

	DEFINE_RES_IRQ_NAMED(BD96801_LDO7_OCPH_STAT, "bd96801-ldo7-overcurr"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO7_OVD_STAT, "bd96801-ldo7-overvolt"),
	DEFINE_RES_IRQ_NAMED(BD96801_LDO7_UVD_STAT, "bd96801-ldo7-undervolt"),
};

enum {
	WDG_CELL = 0,
	REGULATOR_CELL,
};

static struct mfd_cell bd96801_cells[] = {
	[WDG_CELL] = { .name = "bd96801-wdt", },
	[REGULATOR_CELL] = { .name = "bd96801-regulator", },
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

static const struct regmap_access_table volatile_regs = {
	.yes_ranges = bd96801_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(bd96801_volatile_ranges),
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

static const struct regmap_irq_sub_irq_map errb_sub_irq_offsets[] = {
	REGMAP_IRQ_MAIN_REG_OFFSET(bit0_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit1_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit2_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit3_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit4_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit5_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit6_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit7_offsets),
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
	.sub_reg_offsets = &errb_sub_irq_offsets[0],
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

static const struct regmap_config bd96801_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &volatile_regs,
	.cache_type = REGCACHE_MAPLE,
};

static int bd96801_i2c_probe(struct i2c_client *i2c)
{
	struct regmap_irq_chip_data *intb_irq_data, *errb_irq_data;
	struct irq_domain *intb_domain, *errb_domain;
	const struct fwnode_handle *fwnode;
	struct resource *regulator_res;
	struct resource wdg_irq;
	struct regmap *regmap;
	int intb_irq, errb_irq, num_intb, num_errb = 0;
	int num_regu_irqs, wdg_irq_no;
	int i, ret;

	fwnode = dev_fwnode(&i2c->dev);
	if (!fwnode)
		return dev_err_probe(&i2c->dev, -EINVAL, "Failed to find fwnode\n");

	intb_irq = fwnode_irq_get_byname(fwnode, "intb");
	if (intb_irq < 0)
		return dev_err_probe(&i2c->dev, intb_irq, "INTB IRQ not configured\n");

	num_intb =  ARRAY_SIZE(regulator_intb_irqs);

	/* ERRB may be omitted if processor is powered by the PMIC */
	errb_irq = fwnode_irq_get_byname(fwnode, "errb");
	if (errb_irq < 0)
		errb_irq = 0;

	if (errb_irq)
		num_errb = ARRAY_SIZE(regulator_errb_irqs);

	num_regu_irqs = num_intb + num_errb;

	regulator_res = devm_kcalloc(&i2c->dev, num_regu_irqs,
				     sizeof(*regulator_res), GFP_KERNEL);
	if (!regulator_res)
		return -ENOMEM;

	regmap = devm_regmap_init_i2c(i2c, &bd96801_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(&i2c->dev, PTR_ERR(regmap),
				    "Regmap initialization failed\n");

	ret = regmap_write(regmap, BD96801_LOCK_REG, BD96801_UNLOCK);
	if (ret)
		return dev_err_probe(&i2c->dev, ret, "Failed to unlock PMIC\n");

	ret = devm_regmap_add_irq_chip(&i2c->dev, regmap, intb_irq,
				       IRQF_ONESHOT, 0, &bd96801_irq_chip_intb,
				       &intb_irq_data);
	if (ret)
		return dev_err_probe(&i2c->dev, ret, "Failed to add INTB IRQ chip\n");

	intb_domain = regmap_irq_get_domain(intb_irq_data);

	/*
	 * MFD core code is built to handle only one IRQ domain. BD96801
	 * has two domains so we do IRQ mapping here and provide the
	 * already mapped IRQ numbers to sub-devices.
	 */
	for (i = 0; i < num_intb; i++) {
		struct resource *res = &regulator_res[i];

		*res = regulator_intb_irqs[i];
		res->start = res->end = irq_create_mapping(intb_domain,
							    res->start);
	}

	wdg_irq_no = irq_create_mapping(intb_domain, BD96801_WDT_ERR_STAT);
	wdg_irq = DEFINE_RES_IRQ_NAMED(wdg_irq_no, "bd96801-wdg");
	bd96801_cells[WDG_CELL].resources = &wdg_irq;
	bd96801_cells[WDG_CELL].num_resources = 1;

	if (!num_errb)
		goto skip_errb;

	ret = devm_regmap_add_irq_chip(&i2c->dev, regmap, errb_irq, IRQF_ONESHOT,
				       0, &bd96801_irq_chip_errb, &errb_irq_data);
	if (ret)
		return dev_err_probe(&i2c->dev, ret,
				     "Failed to add ERRB IRQ chip\n");

	errb_domain = regmap_irq_get_domain(errb_irq_data);

	for (i = 0; i < num_errb; i++) {
		struct resource *res = &regulator_res[num_intb + i];

		*res = regulator_errb_irqs[i];
		res->start = res->end = irq_create_mapping(errb_domain, res->start);
	}

skip_errb:
	bd96801_cells[REGULATOR_CELL].resources = regulator_res;
	bd96801_cells[REGULATOR_CELL].num_resources = num_regu_irqs;

	ret = devm_mfd_add_devices(&i2c->dev, PLATFORM_DEVID_AUTO, bd96801_cells,
				   ARRAY_SIZE(bd96801_cells), NULL, 0, NULL);
	if (ret)
		dev_err_probe(&i2c->dev, ret, "Failed to create subdevices\n");

	return ret;
}

static const struct of_device_id bd96801_of_match[] = {
	{ .compatible = "rohm,bd96801",	},
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
MODULE_DESCRIPTION("ROHM BD96801 Power Management IC driver");
MODULE_LICENSE("GPL");
