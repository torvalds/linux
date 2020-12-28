// SPDX-License-Identifier: GPL-2.0
/*
 * FPGA Manager Driver for FPGA Management Engine (FME)
 *
 * Copyright (C) 2017-2018 Intel Corporation, Inc.
 *
 * Authors:
 *   Kang Luwei <luwei.kang@intel.com>
 *   Xiao Guangrong <guangrong.xiao@linux.intel.com>
 *   Wu Hao <hao.wu@intel.com>
 *   Joseph Grecco <joe.grecco@intel.com>
 *   Enno Luebbers <enno.luebbers@intel.com>
 *   Tim Whisonant <tim.whisonant@intel.com>
 *   Ananda Ravuri <ananda.ravuri@intel.com>
 *   Christopher Rauer <christopher.rauer@intel.com>
 *   Henry Mitchel <henry.mitchel@intel.com>
 */

#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/iopoll.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/fpga/fpga-mgr.h>

#include "dfl-fme-pr.h"

/* FME Partial Reconfiguration Sub Feature Register Set */
#define FME_PR_DFH		0x0
#define FME_PR_CTRL		0x8
#define FME_PR_STS		0x10
#define FME_PR_DATA		0x18
#define FME_PR_ERR		0x20
#define FME_PR_INTFC_ID_L	0xA8
#define FME_PR_INTFC_ID_H	0xB0

/* FME PR Control Register Bitfield */
#define FME_PR_CTRL_PR_RST	BIT_ULL(0)  /* Reset PR engine */
#define FME_PR_CTRL_PR_RSTACK	BIT_ULL(4)  /* Ack for PR engine reset */
#define FME_PR_CTRL_PR_RGN_ID	GENMASK_ULL(9, 7)       /* PR Region ID */
#define FME_PR_CTRL_PR_START	BIT_ULL(12) /* Start to request PR service */
#define FME_PR_CTRL_PR_COMPLETE	BIT_ULL(13) /* PR data push completion */

/* FME PR Status Register Bitfield */
/* Number of available entries in HW queue inside the PR engine. */
#define FME_PR_STS_PR_CREDIT	GENMASK_ULL(8, 0)
#define FME_PR_STS_PR_STS	BIT_ULL(16) /* PR operation status */
#define FME_PR_STS_PR_STS_IDLE	0
#define FME_PR_STS_PR_CTRLR_STS	GENMASK_ULL(22, 20)     /* Controller status */
#define FME_PR_STS_PR_HOST_STS	GENMASK_ULL(27, 24)     /* PR host status */

/* FME PR Data Register Bitfield */
/* PR data from the raw-binary file. */
#define FME_PR_DATA_PR_DATA_RAW	GENMASK_ULL(32, 0)

/* FME PR Error Register */
/* PR Operation errors detected. */
#define FME_PR_ERR_OPERATION_ERR	BIT_ULL(0)
/* CRC error detected. */
#define FME_PR_ERR_CRC_ERR		BIT_ULL(1)
/* Incompatible PR bitstream detected. */
#define FME_PR_ERR_INCOMPATIBLE_BS	BIT_ULL(2)
/* PR data push protocol violated. */
#define FME_PR_ERR_PROTOCOL_ERR		BIT_ULL(3)
/* PR data fifo overflow error detected */
#define FME_PR_ERR_FIFO_OVERFLOW	BIT_ULL(4)

#define PR_WAIT_TIMEOUT   8000000
#define PR_HOST_STATUS_IDLE	0

struct fme_mgr_priv {
	void __iomem *ioaddr;
	u64 pr_error;
};

static u64 pr_error_to_mgr_status(u64 err)
{
	u64 status = 0;

	if (err & FME_PR_ERR_OPERATION_ERR)
		status |= FPGA_MGR_STATUS_OPERATION_ERR;
	if (err & FME_PR_ERR_CRC_ERR)
		status |= FPGA_MGR_STATUS_CRC_ERR;
	if (err & FME_PR_ERR_INCOMPATIBLE_BS)
		status |= FPGA_MGR_STATUS_INCOMPATIBLE_IMAGE_ERR;
	if (err & FME_PR_ERR_PROTOCOL_ERR)
		status |= FPGA_MGR_STATUS_IP_PROTOCOL_ERR;
	if (err & FME_PR_ERR_FIFO_OVERFLOW)
		status |= FPGA_MGR_STATUS_FIFO_OVERFLOW_ERR;

	return status;
}

static u64 fme_mgr_pr_error_handle(void __iomem *fme_pr)
{
	u64 pr_status, pr_error;

	pr_status = readq(fme_pr + FME_PR_STS);
	if (!(pr_status & FME_PR_STS_PR_STS))
		return 0;

	pr_error = readq(fme_pr + FME_PR_ERR);
	writeq(pr_error, fme_pr + FME_PR_ERR);

	return pr_error;
}

static int fme_mgr_write_init(struct fpga_manager *mgr,
			      struct fpga_image_info *info,
			      const char *buf, size_t count)
{
	struct device *dev = &mgr->dev;
	struct fme_mgr_priv *priv = mgr->priv;
	void __iomem *fme_pr = priv->ioaddr;
	u64 pr_ctrl, pr_status;

	if (!(info->flags & FPGA_MGR_PARTIAL_RECONFIG)) {
		dev_err(dev, "only supports partial reconfiguration.\n");
		return -EINVAL;
	}

	dev_dbg(dev, "resetting PR before initiated PR\n");

	pr_ctrl = readq(fme_pr + FME_PR_CTRL);
	pr_ctrl |= FME_PR_CTRL_PR_RST;
	writeq(pr_ctrl, fme_pr + FME_PR_CTRL);

	if (readq_poll_timeout(fme_pr + FME_PR_CTRL, pr_ctrl,
			       pr_ctrl & FME_PR_CTRL_PR_RSTACK, 1,
			       PR_WAIT_TIMEOUT)) {
		dev_err(dev, "PR Reset ACK timeout\n");
		return -ETIMEDOUT;
	}

	pr_ctrl = readq(fme_pr + FME_PR_CTRL);
	pr_ctrl &= ~FME_PR_CTRL_PR_RST;
	writeq(pr_ctrl, fme_pr + FME_PR_CTRL);

	dev_dbg(dev,
		"waiting for PR resource in HW to be initialized and ready\n");

	if (readq_poll_timeout(fme_pr + FME_PR_STS, pr_status,
			       (pr_status & FME_PR_STS_PR_STS) ==
			       FME_PR_STS_PR_STS_IDLE, 1, PR_WAIT_TIMEOUT)) {
		dev_err(dev, "PR Status timeout\n");
		priv->pr_error = fme_mgr_pr_error_handle(fme_pr);
		return -ETIMEDOUT;
	}

	dev_dbg(dev, "check and clear previous PR error\n");
	priv->pr_error = fme_mgr_pr_error_handle(fme_pr);
	if (priv->pr_error)
		dev_dbg(dev, "previous PR error detected %llx\n",
			(unsigned long long)priv->pr_error);

	dev_dbg(dev, "set PR port ID\n");

	pr_ctrl = readq(fme_pr + FME_PR_CTRL);
	pr_ctrl &= ~FME_PR_CTRL_PR_RGN_ID;
	pr_ctrl |= FIELD_PREP(FME_PR_CTRL_PR_RGN_ID, info->region_id);
	writeq(pr_ctrl, fme_pr + FME_PR_CTRL);

	return 0;
}

static int fme_mgr_write(struct fpga_manager *mgr,
			 const char *buf, size_t count)
{
	struct device *dev = &mgr->dev;
	struct fme_mgr_priv *priv = mgr->priv;
	void __iomem *fme_pr = priv->ioaddr;
	u64 pr_ctrl, pr_status, pr_data;
	int delay = 0, pr_credit, i = 0;

	dev_dbg(dev, "start request\n");

	pr_ctrl = readq(fme_pr + FME_PR_CTRL);
	pr_ctrl |= FME_PR_CTRL_PR_START;
	writeq(pr_ctrl, fme_pr + FME_PR_CTRL);

	dev_dbg(dev, "pushing data from bitstream to HW\n");

	/*
	 * driver can push data to PR hardware using PR_DATA register once HW
	 * has enough pr_credit (> 1), pr_credit reduces one for every 32bit
	 * pr data write to PR_DATA register. If pr_credit <= 1, driver needs
	 * to wait for enough pr_credit from hardware by polling.
	 */
	pr_status = readq(fme_pr + FME_PR_STS);
	pr_credit = FIELD_GET(FME_PR_STS_PR_CREDIT, pr_status);

	while (count > 0) {
		while (pr_credit <= 1) {
			if (delay++ > PR_WAIT_TIMEOUT) {
				dev_err(dev, "PR_CREDIT timeout\n");
				return -ETIMEDOUT;
			}
			udelay(1);

			pr_status = readq(fme_pr + FME_PR_STS);
			pr_credit = FIELD_GET(FME_PR_STS_PR_CREDIT, pr_status);
		}

		if (count < 4) {
			dev_err(dev, "Invalid PR bitstream size\n");
			return -EINVAL;
		}

		pr_data = 0;
		pr_data |= FIELD_PREP(FME_PR_DATA_PR_DATA_RAW,
				      *(((u32 *)buf) + i));
		writeq(pr_data, fme_pr + FME_PR_DATA);
		count -= 4;
		pr_credit--;
		i++;
	}

	return 0;
}

static int fme_mgr_write_complete(struct fpga_manager *mgr,
				  struct fpga_image_info *info)
{
	struct device *dev = &mgr->dev;
	struct fme_mgr_priv *priv = mgr->priv;
	void __iomem *fme_pr = priv->ioaddr;
	u64 pr_ctrl;

	pr_ctrl = readq(fme_pr + FME_PR_CTRL);
	pr_ctrl |= FME_PR_CTRL_PR_COMPLETE;
	writeq(pr_ctrl, fme_pr + FME_PR_CTRL);

	dev_dbg(dev, "green bitstream push complete\n");
	dev_dbg(dev, "waiting for HW to release PR resource\n");

	if (readq_poll_timeout(fme_pr + FME_PR_CTRL, pr_ctrl,
			       !(pr_ctrl & FME_PR_CTRL_PR_START), 1,
			       PR_WAIT_TIMEOUT)) {
		dev_err(dev, "PR Completion ACK timeout.\n");
		return -ETIMEDOUT;
	}

	dev_dbg(dev, "PR operation complete, checking status\n");
	priv->pr_error = fme_mgr_pr_error_handle(fme_pr);
	if (priv->pr_error) {
		dev_dbg(dev, "PR error detected %llx\n",
			(unsigned long long)priv->pr_error);
		return -EIO;
	}

	dev_dbg(dev, "PR done successfully\n");

	return 0;
}

static enum fpga_mgr_states fme_mgr_state(struct fpga_manager *mgr)
{
	return FPGA_MGR_STATE_UNKNOWN;
}

static u64 fme_mgr_status(struct fpga_manager *mgr)
{
	struct fme_mgr_priv *priv = mgr->priv;

	return pr_error_to_mgr_status(priv->pr_error);
}

static const struct fpga_manager_ops fme_mgr_ops = {
	.write_init = fme_mgr_write_init,
	.write = fme_mgr_write,
	.write_complete = fme_mgr_write_complete,
	.state = fme_mgr_state,
	.status = fme_mgr_status,
};

static void fme_mgr_get_compat_id(void __iomem *fme_pr,
				  struct fpga_compat_id *id)
{
	id->id_l = readq(fme_pr + FME_PR_INTFC_ID_L);
	id->id_h = readq(fme_pr + FME_PR_INTFC_ID_H);
}

static int fme_mgr_probe(struct platform_device *pdev)
{
	struct dfl_fme_mgr_pdata *pdata = dev_get_platdata(&pdev->dev);
	struct fpga_compat_id *compat_id;
	struct device *dev = &pdev->dev;
	struct fme_mgr_priv *priv;
	struct fpga_manager *mgr;
	struct resource *res;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	if (pdata->ioaddr)
		priv->ioaddr = pdata->ioaddr;

	if (!priv->ioaddr) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		priv->ioaddr = devm_ioremap_resource(dev, res);
		if (IS_ERR(priv->ioaddr))
			return PTR_ERR(priv->ioaddr);
	}

	compat_id = devm_kzalloc(dev, sizeof(*compat_id), GFP_KERNEL);
	if (!compat_id)
		return -ENOMEM;

	fme_mgr_get_compat_id(priv->ioaddr, compat_id);

	mgr = devm_fpga_mgr_create(dev, "DFL FME FPGA Manager",
				   &fme_mgr_ops, priv);
	if (!mgr)
		return -ENOMEM;

	mgr->compat_id = compat_id;

	return devm_fpga_mgr_register(dev, mgr);
}

static struct platform_driver fme_mgr_driver = {
	.driver	= {
		.name    = DFL_FPGA_FME_MGR,
	},
	.probe   = fme_mgr_probe,
};

module_platform_driver(fme_mgr_driver);

MODULE_DESCRIPTION("FPGA Manager for DFL FPGA Management Engine");
MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:dfl-fme-mgr");
