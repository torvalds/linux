// SPDX-License-Identifier: GPL-2.0-only
/*
 * QTI hardware key manager driver.
 *
 * Copyright (c) 2022, 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/crypto.h>
#include <linux/bitops.h>
#include <linux/iommu.h>

#include <linux/hwkm.h>
#include <linux/tme_hwkm_master.h>
#include "hwkmregs.h"
#include "hwkm_serialize.h"
#include "crypto-qti-ice-regs.h"

#define ASYNC_CMD_HANDLING false

// Maximum number of times to poll
#define MAX_RETRIES 1000

int retries;
#define WAIT_UNTIL(cond)			\
for (retries = 0; !(cond) && (retries < MAX_RETRIES); retries++)

#define ICE_SLAVE_TPKEY_VAL		0x18C

#define qti_hwkm_readl(hwkm_base, reg, dest)				\
	(readl_relaxed(hwkm_base + (reg)))
#define qti_hwkm_writel(hwkm_base, val, reg, dest)			\
	(writel_relaxed((val), hwkm_base + (reg)))
#define qti_hwkm_setb(hwkm_base, reg, nr, dest) {			\
	u32 val = qti_hwkm_readl(hwkm_base, reg, dest);		\
	val |= (0x1 << nr);					\
	qti_hwkm_writel(hwkm_base, val, reg, dest);			\
}
#define qti_hwkm_clearb(hwkm_base, reg, nr, dest) {			\
	u32 val = qti_hwkm_readl(hwkm_base, reg, dest);		\
	val &= ~(0x1 << nr);					\
	qti_hwkm_writel(hwkm_base, val, reg, dest);			\
}

static inline bool qti_hwkm_testb(void __iomem *ice_hwkm_mmio, u32 reg, u8 nr,
				  enum hwkm_destination dest)
{
	u32 val = qti_hwkm_readl(ice_hwkm_mmio, reg, dest);

	val = (val >> nr) & 0x1;
	if (val == 0)
		return false;
	return true;
}

unsigned int qti_hwkm_get_reg_data(void __iomem *ice_hwkm_mmio,
						 u32 reg, u32 offset, u32 mask,
						 enum hwkm_destination dest)
{
	u32 val = 0;

	val = qti_hwkm_readl(ice_hwkm_mmio, reg, dest);
	return ((val & mask) >> offset);
}
EXPORT_SYMBOL_GPL(qti_hwkm_get_reg_data);

static void print_err_info(struct tme_ext_err_info *err)
{
	pr_err("printing tme hwkm error response\n");
	pr_err("tme_err_status = %d\n", err->tme_err_status);
	pr_err("seq_err_status = %d\n", err->seq_err_status);
	pr_err("seq_kp_err_status0 = %d\n", err->seq_kp_err_status0);
	pr_err("seq_kp_err_status1 = %d\n", err->seq_kp_err_status1);
}

static int qti_handle_set_tpkey(const struct hwkm_cmd *cmd_in,
				struct hwkm_rsp *rsp_in)
{
	int status = 0;
	int retries = 0;
	struct tme_ext_err_info errinfo = {0};

	if (cmd_in->dest != KM_MASTER) {
		pr_err("Invalid dest %d, only master supported\n",
						cmd_in->dest);
		return -EINVAL;
	}

	status = tme_hwkm_master_broadcast_transportkey(&errinfo);
	if (status) {
		if ((status == -ENODEV) || (status == -EAGAIN)) {
			while (((status == -ENODEV) || (status == -EAGAIN)) &&
					(retries < MAX_RETRIES)) {
				usleep_range(8000, 12000);
				status =
				tme_hwkm_master_broadcast_transportkey(
							&errinfo);
				if (status == 0)
					goto ret;
				retries++;
			}
		}
		pr_err("Err in tme hwkm tpkey call, sts = %d\n", status);
		print_err_info(&errinfo);
	}

ret:
	return status;
}

int qti_hwkm_handle_cmd(struct hwkm_cmd *cmd, struct hwkm_rsp *rsp)
{
	switch (cmd->op) {
	case SET_TPKEY:
		return qti_handle_set_tpkey(cmd, rsp);
	case KEY_UNWRAP_IMPORT:
	case KEY_SLOT_CLEAR:
	case KEY_SLOT_RDWR:
	case SYSTEM_KDF:
	case NIST_KEYGEN:
	case KEY_WRAP_EXPORT:
	case QFPROM_KEY_RDWR: // cmd for HW initialization cmd only
	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(qti_hwkm_handle_cmd);

static void qti_hwkm_configure_slot_access(const struct ice_mmio_data *mmio_data)
{
	qti_hwkm_writel(mmio_data->ice_hwkm_mmio, 0xffffffff,
		QTI_HWKM_ICE_RG_BANK0_AC_BANKN_BBAC_0, ICE_SLAVE);
	qti_hwkm_writel(mmio_data->ice_hwkm_mmio, 0xffffffff,
		QTI_HWKM_ICE_RG_BANK0_AC_BANKN_BBAC_1, ICE_SLAVE);
	qti_hwkm_writel(mmio_data->ice_hwkm_mmio, 0xffffffff,
		QTI_HWKM_ICE_RG_BANK0_AC_BANKN_BBAC_2, ICE_SLAVE);
	qti_hwkm_writel(mmio_data->ice_hwkm_mmio, 0xffffffff,
		QTI_HWKM_ICE_RG_BANK0_AC_BANKN_BBAC_3, ICE_SLAVE);
	qti_hwkm_writel(mmio_data->ice_hwkm_mmio, 0xffffffff,
		QTI_HWKM_ICE_RG_BANK0_AC_BANKN_BBAC_4, ICE_SLAVE);
}

static int qti_hwkm_check_bist_status(const struct ice_mmio_data *mmio_data)
{
	if (!qti_hwkm_testb(mmio_data->ice_hwkm_mmio, QTI_HWKM_ICE_RG_TZ_KM_STATUS,
		BIST_DONE, ICE_SLAVE)) {
		pr_err("%s: Error with BIST_DONE\n", __func__);
		return -EINVAL;
	}

	if (!qti_hwkm_testb(mmio_data->ice_hwkm_mmio, QTI_HWKM_ICE_RG_TZ_KM_STATUS,
		CRYPTO_LIB_BIST_DONE, ICE_SLAVE)) {
		pr_err("%s: Error with CRYPTO_LIB_BIST_DONE\n", __func__);
		return -EINVAL;
	}

	if (!qti_hwkm_testb(mmio_data->ice_hwkm_mmio, QTI_HWKM_ICE_RG_TZ_KM_STATUS,
		BOOT_CMD_LIST1_DONE, ICE_SLAVE)) {
		pr_err("%s: Error with BOOT_CMD_LIST1_DONE\n", __func__);
		return -EINVAL;
	}

	if (!qti_hwkm_testb(mmio_data->ice_hwkm_mmio, QTI_HWKM_ICE_RG_TZ_KM_STATUS,
		BOOT_CMD_LIST0_DONE, ICE_SLAVE)) {
		pr_err("%s: Error with BOOT_CMD_LIST0_DONE\n", __func__);
		return -EINVAL;
	}

	if (!qti_hwkm_testb(mmio_data->ice_hwkm_mmio, QTI_HWKM_ICE_RG_TZ_KM_STATUS,
		KT_CLEAR_DONE, ICE_SLAVE)) {
		pr_err("%s: KT_CLEAR_DONE\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int qti_hwkm_ice_init_sequence(const struct ice_mmio_data *mmio_data)
{
	int ret = 0;
	u32 val = 0;

	//Put ICE in standard mode
	val = ice_readl(mmio_data->ice_base_mmio, ICE_REGS_CONTROL);
	val = val & 0xFFFFFFFE;
	ice_writel(mmio_data->ice_base_mmio, val, ICE_REGS_CONTROL);
	/* Write memory barrier */
	wmb();

	pr_debug("%s: ICE_REGS_CONTROL = 0x%x\n", __func__,
			ice_readl(mmio_data->ice_base_mmio, ICE_REGS_CONTROL));

	ret = qti_hwkm_check_bist_status(mmio_data);
	if (ret) {
		pr_err("%s: Error in BIST initialization %d\n", __func__, ret);
		return ret;
	}

	// Disable CRC checks
	qti_hwkm_clearb(mmio_data->ice_hwkm_mmio, QTI_HWKM_ICE_RG_TZ_KM_CTL,
				CRC_CHECK_EN, ICE_SLAVE);
	/* Write memory barrier */
	wmb();

	// Configure key slots to be accessed by HLOS
	qti_hwkm_configure_slot_access(mmio_data);
	/* Write memory barrier */
	wmb();

	// Clear RSP_FIFO_FULL bit
	qti_hwkm_setb(mmio_data->ice_hwkm_mmio,
			QTI_HWKM_ICE_RG_BANK0_BANKN_IRQ_STATUS,
			RSP_FIFO_FULL, ICE_SLAVE);
	/* Write memory barrier */
	wmb();

	return ret;
}

static void qti_hwkm_enable_slave_receive_mode(
					const struct ice_mmio_data *mmio_data)
{
	qti_hwkm_clearb(mmio_data->ice_hwkm_mmio,
			QTI_HWKM_ICE_RG_TZ_TPKEY_RECEIVE_CTL, TPKEY_EN, ICE_SLAVE);
	/* Write memory barrier */
	wmb();
	qti_hwkm_writel(mmio_data->ice_hwkm_mmio, ICE_SLAVE_TPKEY_VAL,
			QTI_HWKM_ICE_RG_TZ_TPKEY_RECEIVE_CTL, ICE_SLAVE);
	/* Write memory barrier */
	wmb();
}

static void qti_hwkm_disable_slave_receive_mode(
					const struct ice_mmio_data *mmio_data)
{
	qti_hwkm_clearb(mmio_data->ice_hwkm_mmio,
			QTI_HWKM_ICE_RG_TZ_TPKEY_RECEIVE_CTL, TPKEY_EN, ICE_SLAVE);
	/* Write memory barrier */
	wmb();
}

static void qti_hwkm_check_tpkey_status(const struct ice_mmio_data *mmio_data)
{
	int val = 0;

	val = qti_hwkm_readl(mmio_data->ice_hwkm_mmio,
			QTI_HWKM_ICE_RG_TZ_TPKEY_RECEIVE_STATUS, ICE_SLAVE);

	pr_debug("%s: Tpkey receive status 0x%x\n", __func__, val);
}

static int qti_hwkm_set_tpkey(const struct ice_mmio_data *mmio_data)
{
	int err = 0;
	struct hwkm_cmd cmd_settpkey = {0};
	struct hwkm_rsp rsp_settpkey = {0};

	qti_hwkm_enable_slave_receive_mode(mmio_data);

	cmd_settpkey.op = SET_TPKEY;
	cmd_settpkey.dest = KM_MASTER;
	err = qti_hwkm_handle_cmd(&cmd_settpkey, &rsp_settpkey);
	if (err) {
		pr_err("%s: Error with Set TP key in master %d\n", __func__,
							err);
		return -EINVAL;
	}

	qti_hwkm_check_tpkey_status(mmio_data);
	qti_hwkm_disable_slave_receive_mode(mmio_data);

	return 0;
}

int qti_hwkm_init(const struct ice_mmio_data *mmio_data)
{
	int ret = 0;

	pr_debug("%s %d: HWKM init starts\n", __func__, __LINE__);
	if (!mmio_data->ice_hwkm_mmio || !mmio_data->ice_base_mmio) {
		pr_err("%s: HWKM ICE slave mmio invalid\n", __func__);
		return -EINVAL;
	}

	ret = qti_hwkm_ice_init_sequence(mmio_data);
	if (ret) {
		pr_err("%s: Error in ICE init sequence %d\n", __func__, ret);
		return ret;
	}

	ret = qti_hwkm_set_tpkey(mmio_data);
	if (ret) {
		pr_err("%s: Error setting ICE to receive %d\n", __func__, ret);
		return ret;
	}
	/* Write memory barrier */
	wmb();

	pr_debug("%s %d: HWKM init ends\n", __func__, __LINE__);
	return ret;
}
EXPORT_SYMBOL(qti_hwkm_init);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("QTI Hardware Key Manager library");
