// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 HiSilicon Limited. */

#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/uacce.h>
#include "zip.h"

/* memory */
#define DAE_MEM_START_OFFSET		0x331040
#define DAE_MEM_DONE_OFFSET		0x331044
#define DAE_MEM_START_MASK		0x1
#define DAE_MEM_DONE_MASK		0x1
#define DAE_REG_RD_INTVRL_US		10
#define DAE_REG_RD_TMOUT_US		USEC_PER_SEC

#define DAE_ALG_NAME			"hashagg"
#define DAE_V5_ALG_NAME			"hashagg\nudma\nhashjoin\ngather"

/* error */
#define DAE_AXI_CFG_OFFSET		0x331000
#define DAE_AXI_SHUTDOWN_MASK		(BIT(0) | BIT(5))
#define DAE_ERR_SOURCE_OFFSET		0x331C84
#define DAE_ERR_STATUS_OFFSET		0x331C88
#define DAE_ERR_CE_OFFSET		0x331CA0
#define DAE_ERR_CE_MASK			BIT(3)
#define DAE_ERR_NFE_OFFSET		0x331CA4
#define DAE_ERR_NFE_MASK		0x17
#define DAE_ERR_FE_OFFSET		0x331CA8
#define DAE_ERR_FE_MASK			0
#define DAE_ECC_MBIT_MASK		BIT(2)
#define DAE_ECC_INFO_OFFSET		0x33400C
#define DAE_ERR_SHUTDOWN_OFFSET		0x331CAC
#define DAE_ERR_SHUTDOWN_MASK		0x17
#define DAE_ERR_ENABLE_OFFSET		0x331C80
#define DAE_ERR_ENABLE_MASK		(DAE_ERR_FE_MASK | DAE_ERR_NFE_MASK | DAE_ERR_CE_MASK)
#define DAE_AM_CTRL_GLOBAL_OFFSET	0x330000
#define DAE_AM_RETURN_OFFSET		0x330150
#define DAE_AM_RETURN_MASK		0x3
#define DAE_AXI_CFG_OFFSET		0x331000
#define DAE_AXI_SHUTDOWN_EN_MASK	(BIT(0) | BIT(5))

struct hisi_dae_hw_error {
	u32 int_msk;
	const char *msg;
};

static const struct hisi_dae_hw_error dae_hw_error[] = {
	{ .int_msk = BIT(0), .msg = "dae_axi_bus_err" },
	{ .int_msk = BIT(1), .msg = "dae_axi_poison_err" },
	{ .int_msk = BIT(2), .msg = "dae_ecc_2bit_err" },
	{ .int_msk = BIT(3), .msg = "dae_ecc_1bit_err" },
	{ .int_msk = BIT(4), .msg = "dae_fsm_hbeat_err" },
};

static inline bool dae_is_support(struct hisi_qm *qm)
{
	if (test_bit(QM_SUPPORT_DAE, &qm->caps))
		return true;

	return false;
}

int hisi_dae_set_user_domain(struct hisi_qm *qm)
{
	u32 val;
	int ret;

	if (!dae_is_support(qm))
		return 0;

	val = readl(qm->io_base + DAE_MEM_START_OFFSET);
	val |= DAE_MEM_START_MASK;
	writel(val, qm->io_base + DAE_MEM_START_OFFSET);
	ret = readl_relaxed_poll_timeout(qm->io_base + DAE_MEM_DONE_OFFSET, val,
					 val & DAE_MEM_DONE_MASK,
					 DAE_REG_RD_INTVRL_US, DAE_REG_RD_TMOUT_US);
	if (ret)
		pci_err(qm->pdev, "failed to init dae memory!\n");

	return ret;
}

int hisi_dae_set_alg(struct hisi_qm *qm)
{
	const char *alg_name;
	size_t len;

	if (!dae_is_support(qm))
		return 0;

	if (!qm->uacce)
		return 0;

	if (qm->ver >= QM_HW_V5)
		alg_name = DAE_V5_ALG_NAME;
	else
		alg_name = DAE_ALG_NAME;

	len = strlen(qm->uacce->algs);
	/* A line break may be required */
	if (len + strlen(alg_name) + 1 >= QM_DEV_ALG_MAX_LEN) {
		pci_err(qm->pdev, "algorithm name is too long!\n");
		return -EINVAL;
	}

	if (len)
		strcat((char *)qm->uacce->algs, "\n");

	strcat((char *)qm->uacce->algs, alg_name);

	return 0;
}

static void hisi_dae_master_ooo_ctrl(struct hisi_qm *qm, bool enable)
{
	u32 axi_val, err_val;

	axi_val = readl(qm->io_base + DAE_AXI_CFG_OFFSET);
	if (enable) {
		axi_val |= DAE_AXI_SHUTDOWN_MASK;
		err_val = DAE_ERR_SHUTDOWN_MASK;
	} else {
		axi_val &= ~DAE_AXI_SHUTDOWN_MASK;
		err_val = 0;
	}

	writel(axi_val, qm->io_base + DAE_AXI_CFG_OFFSET);
	writel(err_val, qm->io_base + DAE_ERR_SHUTDOWN_OFFSET);
}

void hisi_dae_hw_error_enable(struct hisi_qm *qm)
{
	if (!dae_is_support(qm))
		return;

	/* clear dae hw error source if having */
	writel(DAE_ERR_ENABLE_MASK, qm->io_base + DAE_ERR_SOURCE_OFFSET);

	/* configure error type */
	writel(DAE_ERR_CE_MASK, qm->io_base + DAE_ERR_CE_OFFSET);
	writel(DAE_ERR_NFE_MASK, qm->io_base + DAE_ERR_NFE_OFFSET);
	writel(DAE_ERR_FE_MASK, qm->io_base + DAE_ERR_FE_OFFSET);

	hisi_dae_master_ooo_ctrl(qm, true);

	/* enable dae hw error interrupts */
	writel(DAE_ERR_ENABLE_MASK, qm->io_base + DAE_ERR_ENABLE_OFFSET);
}

void hisi_dae_hw_error_disable(struct hisi_qm *qm)
{
	if (!dae_is_support(qm))
		return;

	writel(0, qm->io_base + DAE_ERR_ENABLE_OFFSET);
	hisi_dae_master_ooo_ctrl(qm, false);
}

static u32 hisi_dae_get_hw_err_status(struct hisi_qm *qm)
{
	return readl(qm->io_base + DAE_ERR_STATUS_OFFSET);
}

static void hisi_dae_clear_hw_err_status(struct hisi_qm *qm, u32 err_sts)
{
	if (!dae_is_support(qm))
		return;

	writel(err_sts, qm->io_base + DAE_ERR_SOURCE_OFFSET);
}

static void hisi_dae_disable_error_report(struct hisi_qm *qm, u32 err_type)
{
	writel(DAE_ERR_NFE_MASK & (~err_type), qm->io_base + DAE_ERR_NFE_OFFSET);
}

static void hisi_dae_enable_error_report(struct hisi_qm *qm)
{
	writel(DAE_ERR_CE_MASK, qm->io_base + DAE_ERR_CE_OFFSET);
	writel(DAE_ERR_NFE_MASK, qm->io_base + DAE_ERR_NFE_OFFSET);
}

static void hisi_dae_log_hw_error(struct hisi_qm *qm, u32 err_type)
{
	const struct hisi_dae_hw_error *err = dae_hw_error;
	struct device *dev = &qm->pdev->dev;
	u32 ecc_info;
	size_t i;

	for (i = 0; i < ARRAY_SIZE(dae_hw_error); i++) {
		err = &dae_hw_error[i];
		if (!(err->int_msk & err_type))
			continue;

		dev_err(dev, "%s [error status=0x%x] found\n",
			err->msg, err->int_msk);

		if (err->int_msk & DAE_ECC_MBIT_MASK) {
			ecc_info = readl(qm->io_base + DAE_ECC_INFO_OFFSET);
			dev_err(dev, "dae multi ecc sram info 0x%x\n", ecc_info);
		}
	}
}

enum acc_err_result hisi_dae_get_err_result(struct hisi_qm *qm)
{
	u32 err_status;

	if (!dae_is_support(qm))
		return ACC_ERR_NONE;

	err_status = hisi_dae_get_hw_err_status(qm);
	if (!err_status)
		return ACC_ERR_NONE;

	hisi_dae_log_hw_error(qm, err_status);

	if (err_status & DAE_ERR_NFE_MASK) {
		/* Disable the same error reporting until device is recovered. */
		hisi_dae_disable_error_report(qm, err_status);
		return ACC_ERR_NEED_RESET;
	}
	hisi_dae_clear_hw_err_status(qm, err_status);
	/* Avoid firmware disable error report, re-enable. */
	hisi_dae_enable_error_report(qm);

	return ACC_ERR_RECOVERED;
}

bool hisi_dae_dev_is_abnormal(struct hisi_qm *qm)
{
	u32 err_status;

	if (!dae_is_support(qm))
		return false;

	err_status = hisi_dae_get_hw_err_status(qm);
	if (err_status & DAE_ERR_NFE_MASK)
		return true;

	return false;
}

int hisi_dae_close_axi_master_ooo(struct hisi_qm *qm)
{
	u32 val;
	int ret;

	if (!dae_is_support(qm))
		return 0;

	val = readl(qm->io_base + DAE_AM_CTRL_GLOBAL_OFFSET);
	val |= BIT(0);
	writel(val, qm->io_base + DAE_AM_CTRL_GLOBAL_OFFSET);

	ret = readl_relaxed_poll_timeout(qm->io_base + DAE_AM_RETURN_OFFSET,
					 val, (val == DAE_AM_RETURN_MASK),
					 DAE_REG_RD_INTVRL_US, DAE_REG_RD_TMOUT_US);
	if (ret)
		dev_err(&qm->pdev->dev, "failed to close dae axi ooo!\n");

	return ret;
}

void hisi_dae_open_axi_master_ooo(struct hisi_qm *qm)
{
	u32 val;

	if (!dae_is_support(qm))
		return;

	val = readl(qm->io_base + DAE_AXI_CFG_OFFSET);

	writel(val & ~DAE_AXI_SHUTDOWN_EN_MASK, qm->io_base + DAE_AXI_CFG_OFFSET);
	writel(val | DAE_AXI_SHUTDOWN_EN_MASK, qm->io_base + DAE_AXI_CFG_OFFSET);
}
