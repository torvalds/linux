// SPDX-License-Identifier: GPL-2.0
/*
 * Firmware Layer for UFS APIs
 *
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 */

#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/module.h>

/* Register Node IDs */
#define PM_REGNODE_PMC_IOU_SLCR		0x30000002 /* PMC IOU SLCR */
#define PM_REGNODE_EFUSE_CACHE		0x30000003 /* EFUSE Cache */

/* Register Offsets for PMC IOU SLCR */
#define SRAM_CSR_OFFSET			0x104C /* SRAM Control and Status */
#define TXRX_CFGRDY_OFFSET		0x1054 /* M-PHY TX-RX Config ready */

/* Masks for SRAM Control and Status Register */
#define SRAM_CSR_INIT_DONE_MASK		BIT(0) /* SRAM initialization done */
#define SRAM_CSR_EXT_LD_DONE_MASK	BIT(1) /* SRAM External load done */
#define SRAM_CSR_BYPASS_MASK		BIT(2) /* Bypass SRAM interface */

/* Mask to check M-PHY TX-RX configuration readiness */
#define TX_RX_CFG_RDY_MASK		GENMASK(3, 0)

/* Register Offsets for EFUSE Cache */
#define UFS_CAL_1_OFFSET		0xBE8 /* UFS Calibration Value */

/**
 * zynqmp_pm_is_mphy_tx_rx_config_ready - check M-PHY TX-RX config readiness
 * @is_ready:	Store output status (true/false)
 *
 * Return:	Returns 0 on success or error value on failure.
 */
int zynqmp_pm_is_mphy_tx_rx_config_ready(bool *is_ready)
{
	u32 regval;
	int ret;

	if (!is_ready)
		return -EINVAL;

	ret = zynqmp_pm_sec_read_reg(PM_REGNODE_PMC_IOU_SLCR, TXRX_CFGRDY_OFFSET, &regval);
	if (ret)
		return ret;

	regval &= TX_RX_CFG_RDY_MASK;
	if (regval)
		*is_ready = true;
	else
		*is_ready = false;

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_is_mphy_tx_rx_config_ready);

/**
 * zynqmp_pm_is_sram_init_done - check SRAM initialization
 * @is_done:	Store output status (true/false)
 *
 * Return:	Returns 0 on success or error value on failure.
 */
int zynqmp_pm_is_sram_init_done(bool *is_done)
{
	u32 regval;
	int ret;

	if (!is_done)
		return -EINVAL;

	ret = zynqmp_pm_sec_read_reg(PM_REGNODE_PMC_IOU_SLCR, SRAM_CSR_OFFSET, &regval);
	if (ret)
		return ret;

	regval &= SRAM_CSR_INIT_DONE_MASK;
	if (regval)
		*is_done = true;
	else
		*is_done = false;

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_is_sram_init_done);

/**
 * zynqmp_pm_set_sram_bypass - Set SRAM bypass Control
 *
 * Return:	Returns 0 on success or error value on failure.
 */
int zynqmp_pm_set_sram_bypass(void)
{
	u32 sram_csr;
	int ret;

	ret = zynqmp_pm_sec_read_reg(PM_REGNODE_PMC_IOU_SLCR, SRAM_CSR_OFFSET, &sram_csr);
	if (ret)
		return ret;

	sram_csr &= ~SRAM_CSR_EXT_LD_DONE_MASK;
	sram_csr |= SRAM_CSR_BYPASS_MASK;

	return zynqmp_pm_sec_mask_write_reg(PM_REGNODE_PMC_IOU_SLCR, SRAM_CSR_OFFSET,
					    GENMASK(2, 1), sram_csr);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_set_sram_bypass);

/**
 * zynqmp_pm_get_ufs_calibration_values - Read UFS calibration values
 * @val:	Store the calibration value
 *
 * Return:	Returns 0 on success or error value on failure.
 */
int zynqmp_pm_get_ufs_calibration_values(u32 *val)
{
	return zynqmp_pm_sec_read_reg(PM_REGNODE_EFUSE_CACHE, UFS_CAL_1_OFFSET, val);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_get_ufs_calibration_values);
