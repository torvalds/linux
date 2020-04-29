// SPDX-License-Identifier: GPL-2.0+
// Copyright IBM Corp 2019

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fsi-occ.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "common.h"

struct p9_sbe_occ {
	struct occ occ;
	struct device *sbe;
};

#define to_p9_sbe_occ(x)	container_of((x), struct p9_sbe_occ, occ)

static int p9_sbe_occ_send_cmd(struct occ *occ, u8 *cmd)
{
	struct occ_response *resp = &occ->resp;
	struct p9_sbe_occ *ctx = to_p9_sbe_occ(occ);
	size_t resp_len = sizeof(*resp);
	int rc;

	rc = fsi_occ_submit(ctx->sbe, cmd, 8, resp, &resp_len);
	if (rc < 0)
		return rc;

	switch (resp->return_status) {
	case OCC_RESP_CMD_IN_PRG:
		rc = -ETIMEDOUT;
		break;
	case OCC_RESP_SUCCESS:
		rc = 0;
		break;
	case OCC_RESP_CMD_INVAL:
	case OCC_RESP_CMD_LEN_INVAL:
	case OCC_RESP_DATA_INVAL:
	case OCC_RESP_CHKSUM_ERR:
		rc = -EINVAL;
		break;
	case OCC_RESP_INT_ERR:
	case OCC_RESP_BAD_STATE:
	case OCC_RESP_CRIT_EXCEPT:
	case OCC_RESP_CRIT_INIT:
	case OCC_RESP_CRIT_WATCHDOG:
	case OCC_RESP_CRIT_OCB:
	case OCC_RESP_CRIT_HW:
		rc = -EREMOTEIO;
		break;
	default:
		rc = -EPROTO;
	}

	return rc;
}

static int p9_sbe_occ_probe(struct platform_device *pdev)
{
	int rc;
	struct occ *occ;
	struct p9_sbe_occ *ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx),
					      GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->sbe = pdev->dev.parent;
	occ = &ctx->occ;
	occ->bus_dev = &pdev->dev;
	platform_set_drvdata(pdev, occ);

	occ->powr_sample_time_us = 500;
	occ->poll_cmd_data = 0x20;		/* P9 OCC poll data */
	occ->send_cmd = p9_sbe_occ_send_cmd;

	rc = occ_setup(occ, "p9_occ");
	if (rc == -ESHUTDOWN)
		rc = -ENODEV;	/* Host is shutdown, don't spew errors */

	return rc;
}

static int p9_sbe_occ_remove(struct platform_device *pdev)
{
	struct occ *occ = platform_get_drvdata(pdev);
	struct p9_sbe_occ *ctx = to_p9_sbe_occ(occ);

	ctx->sbe = NULL;
	occ_shutdown(occ);

	return 0;
}

static struct platform_driver p9_sbe_occ_driver = {
	.driver = {
		.name = "occ-hwmon",
	},
	.probe	= p9_sbe_occ_probe,
	.remove = p9_sbe_occ_remove,
};

module_platform_driver(p9_sbe_occ_driver);

MODULE_AUTHOR("Eddie James <eajames@linux.ibm.com>");
MODULE_DESCRIPTION("BMC P9 OCC hwmon driver");
MODULE_LICENSE("GPL");
