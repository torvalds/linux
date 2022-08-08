// SPDX-License-Identifier: GPL-2.0-only
/*
 * Helper functions for secure register writes on Qualcomm PMICs
 */

#include <linux/export.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>

#define SEC_ACCESS_ADDR		0x00d0
#define SEC_ACCESS_VALUE	0x00a5
#define PERPH_BASE_MASK		0xff00

static DEFINE_SPINLOCK(sec_access_lock);

/**
 * @brief qcom_pmic_sec_write() - Write to a secure register
 *
 * @param regmap Pointer to regmap
 * @param addr   Address of register to write into
 * @param val    Buffer to write bytes from
 * @param len    Length of register in bytes
 * @return 0 on success, -errno on failure
 *
 * @details: Some blocks have registers that need to be unlocked first before
 * being written to. This function unlocks secure registers in the peripheral
 * block of a given register then writes a given value to the register.
 */
int qcom_pmic_sec_write(struct regmap *regmap, u16 addr, u8 *val, int len)
{
	unsigned long flags;
	unsigned int perph_base;
	int ret;

	spin_lock_irqsave(&sec_access_lock, flags);

	/* Get peripheral base of given register */
	perph_base = (addr & PERPH_BASE_MASK);

	ret = regmap_write(regmap, perph_base + SEC_ACCESS_ADDR,
			   SEC_ACCESS_VALUE);
	if (ret)
		goto out;

	ret = regmap_bulk_write(regmap, addr, val, len);
out:
	spin_unlock_irqrestore(&sec_access_lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(qcom_pmic_sec_write);

/**
 * @brief qcom_pmic_sec_masked_write() - Masked write to secure registers
 *
 * @param regmap Pointer to regmap
 * @param addr   Address of register to write into
 * @param mask   Mask to apply on value
 * @param val    value to be written
 * @return 0 on success, -errno on failure
 *
 * @details: Masked version of smbchg_sec_write().
 */
int qcom_pmic_sec_masked_write(struct regmap *regmap, u16 addr, u8 mask, u8 val)
{
	int reg;
	int ret;

	ret = regmap_read(regmap, addr, &reg);
	if (ret)
		return ret;

	reg &= ~mask;
	reg |= val & mask;

	return qcom_pmic_sec_write(regmap, addr, (u8 *)&reg, 1);
}
EXPORT_SYMBOL_GPL(qcom_pmic_sec_masked_write);

MODULE_AUTHOR("Yassine Oudjana <y.oudjana@protonmail.com>");
MODULE_DESCRIPTION("Qualcomm PMIC secure write helpers");
MODULE_LICENSE("GPL");
