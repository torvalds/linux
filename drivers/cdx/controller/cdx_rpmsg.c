// SPDX-License-Identifier: GPL-2.0
/*
 * Platform driver for CDX bus.
 *
 * Copyright (C) 2022-2023, Advanced Micro Devices, Inc.
 */

#include <linux/rpmsg.h>
#include <linux/remoteproc.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/cdx/cdx_bus.h>
#include <linux/module.h>

#include "../cdx.h"
#include "cdx_controller.h"
#include "mcdi_functions.h"
#include "mcdid.h"

static struct rpmsg_device_id cdx_rpmsg_id_table[] = {
	{ .name = "mcdi_ipc" },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, cdx_rpmsg_id_table);

int cdx_rpmsg_send(struct cdx_mcdi *cdx_mcdi,
		   const struct cdx_dword *hdr, size_t hdr_len,
		   const struct cdx_dword *sdu, size_t sdu_len)
{
	unsigned char *send_buf;
	int ret;

	send_buf = kzalloc(hdr_len + sdu_len, GFP_KERNEL);
	if (!send_buf)
		return -ENOMEM;

	memcpy(send_buf, hdr, hdr_len);
	memcpy(send_buf + hdr_len, sdu, sdu_len);

	ret = rpmsg_send(cdx_mcdi->ept, send_buf, hdr_len + sdu_len);
	kfree(send_buf);

	return ret;
}

static int cdx_attach_to_rproc(struct platform_device *pdev)
{
	struct device_node *r5_core_node;
	struct cdx_controller *cdx_c;
	struct cdx_mcdi *cdx_mcdi;
	struct device *dev;
	struct rproc *rp;
	int ret;

	dev = &pdev->dev;
	cdx_c = platform_get_drvdata(pdev);
	cdx_mcdi = cdx_c->priv;

	r5_core_node = of_parse_phandle(dev->of_node, "xlnx,rproc", 0);
	if (!r5_core_node) {
		dev_err(&pdev->dev, "xlnx,rproc: invalid phandle\n");
		return -EINVAL;
	}

	rp = rproc_get_by_phandle(r5_core_node->phandle);
	if (!rp) {
		ret = -EPROBE_DEFER;
		goto pdev_err;
	}

	/* Attach to remote processor */
	ret = rproc_boot(rp);
	if (ret) {
		dev_err(&pdev->dev, "Failed to attach to remote processor\n");
		rproc_put(rp);
		goto pdev_err;
	}

	cdx_mcdi->r5_rproc = rp;
pdev_err:
	of_node_put(r5_core_node);
	return ret;
}

static void cdx_detach_to_r5(struct platform_device *pdev)
{
	struct cdx_controller *cdx_c;
	struct cdx_mcdi *cdx_mcdi;

	cdx_c = platform_get_drvdata(pdev);
	cdx_mcdi = cdx_c->priv;

	rproc_detach(cdx_mcdi->r5_rproc);
	rproc_put(cdx_mcdi->r5_rproc);
}

static int cdx_rpmsg_cb(struct rpmsg_device *rpdev, void *data,
			int len, void *priv, u32 src)
{
	struct cdx_controller *cdx_c = dev_get_drvdata(&rpdev->dev);
	struct cdx_mcdi *cdx_mcdi = cdx_c->priv;

	if (len > MCDI_BUF_LEN)
		return -EINVAL;

	cdx_mcdi_process_cmd(cdx_mcdi, (struct cdx_dword *)data, len);

	return 0;
}

static void cdx_rpmsg_post_probe_work(struct work_struct *work)
{
	struct cdx_controller *cdx_c;
	struct cdx_mcdi *cdx_mcdi;

	cdx_mcdi = container_of(work, struct cdx_mcdi, work);
	cdx_c = dev_get_drvdata(&cdx_mcdi->rpdev->dev);
	cdx_rpmsg_post_probe(cdx_c);
}

static int cdx_rpmsg_probe(struct rpmsg_device *rpdev)
{
	struct rpmsg_channel_info chinfo = {0};
	struct cdx_controller *cdx_c;
	struct cdx_mcdi *cdx_mcdi;

	cdx_c = (struct cdx_controller *)cdx_rpmsg_id_table[0].driver_data;
	cdx_mcdi = cdx_c->priv;

	chinfo.src = RPMSG_ADDR_ANY;
	chinfo.dst = rpdev->dst;
	strscpy(chinfo.name, cdx_rpmsg_id_table[0].name, sizeof(chinfo.name));

	cdx_mcdi->ept = rpmsg_create_ept(rpdev, cdx_rpmsg_cb, NULL, chinfo);
	if (!cdx_mcdi->ept) {
		dev_err_probe(&rpdev->dev, -ENXIO,
			      "Failed to create ept for channel %s\n",
			      chinfo.name);
		return -EINVAL;
	}

	cdx_mcdi->rpdev = rpdev;
	dev_set_drvdata(&rpdev->dev, cdx_c);

	schedule_work(&cdx_mcdi->work);
	return 0;
}

static void cdx_rpmsg_remove(struct rpmsg_device *rpdev)
{
	struct cdx_controller *cdx_c = dev_get_drvdata(&rpdev->dev);
	struct cdx_mcdi *cdx_mcdi = cdx_c->priv;

	flush_work(&cdx_mcdi->work);
	cdx_rpmsg_pre_remove(cdx_c);

	rpmsg_destroy_ept(cdx_mcdi->ept);
	dev_set_drvdata(&rpdev->dev, NULL);
}

static struct rpmsg_driver cdx_rpmsg_driver = {
	.drv.name = KBUILD_MODNAME,
	.id_table = cdx_rpmsg_id_table,
	.probe = cdx_rpmsg_probe,
	.remove = cdx_rpmsg_remove,
	.callback = cdx_rpmsg_cb,
};

int cdx_setup_rpmsg(struct platform_device *pdev)
{
	struct cdx_controller *cdx_c;
	struct cdx_mcdi *cdx_mcdi;
	int ret;

	/* Attach to remote processor */
	ret = cdx_attach_to_rproc(pdev);
	if (ret)
		return ret;

	cdx_c = platform_get_drvdata(pdev);
	cdx_mcdi = cdx_c->priv;

	/* Register RPMsg driver */
	cdx_rpmsg_id_table[0].driver_data = (kernel_ulong_t)cdx_c;

	INIT_WORK(&cdx_mcdi->work, cdx_rpmsg_post_probe_work);
	ret = register_rpmsg_driver(&cdx_rpmsg_driver);
	if (ret) {
		dev_err(&pdev->dev,
			"Failed to register cdx RPMsg driver: %d\n", ret);
		cdx_detach_to_r5(pdev);
	}

	return ret;
}

void cdx_destroy_rpmsg(struct platform_device *pdev)
{
	unregister_rpmsg_driver(&cdx_rpmsg_driver);

	cdx_detach_to_r5(pdev);
}
