// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Broadcom.

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/auxiliary_bus.h>

#include <rdma/ib_verbs.h>

#include "bng_res.h"
#include "bng_re.h"
#include "bnge.h"
#include "bnge_hwrm.h"
#include "bnge_auxr.h"

MODULE_AUTHOR("Siva Reddy Kallam <siva.kallam@broadcom.com>");
MODULE_DESCRIPTION(BNG_RE_DESC);
MODULE_LICENSE("Dual BSD/GPL");

static struct bng_re_dev *bng_re_dev_add(struct auxiliary_device *adev,
					 struct bnge_auxr_dev *aux_dev)
{
	struct bng_re_dev *rdev;

	/* Allocate bng_re_dev instance */
	rdev = ib_alloc_device(bng_re_dev, ibdev);
	if (!rdev) {
		pr_err("%s: bng_re_dev allocation failure!", KBUILD_MODNAME);
		return NULL;
	}

	/* Assign auxiliary device specific data */
	rdev->netdev = aux_dev->net;
	rdev->aux_dev = aux_dev;
	rdev->adev = adev;
	rdev->fn_id = rdev->aux_dev->pdev->devfn;

	return rdev;
}


static int bng_re_register_netdev(struct bng_re_dev *rdev)
{
	struct bnge_auxr_dev *aux_dev;

	aux_dev = rdev->aux_dev;
	return bnge_register_dev(aux_dev, rdev->adev);
}

static void bng_re_destroy_chip_ctx(struct bng_re_dev *rdev)
{
	struct bng_re_chip_ctx *chip_ctx;

	if (!rdev->chip_ctx)
		return;

	chip_ctx = rdev->chip_ctx;
	rdev->chip_ctx = NULL;
	kfree(chip_ctx);
}

static int bng_re_setup_chip_ctx(struct bng_re_dev *rdev)
{
	struct bng_re_chip_ctx *chip_ctx;
	struct bnge_auxr_dev *aux_dev;

	aux_dev = rdev->aux_dev;

	chip_ctx = kzalloc(sizeof(*chip_ctx), GFP_KERNEL);
	if (!chip_ctx)
		return -ENOMEM;
	chip_ctx->chip_num = aux_dev->chip_num;
	chip_ctx->hw_stats_size = aux_dev->hw_ring_stats_size;

	rdev->chip_ctx = chip_ctx;

	return 0;
}

static void bng_re_init_hwrm_hdr(struct input *hdr, u16 opcd)
{
	hdr->req_type = cpu_to_le16(opcd);
	hdr->cmpl_ring = cpu_to_le16(-1);
	hdr->target_id = cpu_to_le16(-1);
}

static void bng_re_fill_fw_msg(struct bnge_fw_msg *fw_msg, void *msg,
			       int msg_len, void *resp, int resp_max_len,
			       int timeout)
{
	fw_msg->msg = msg;
	fw_msg->msg_len = msg_len;
	fw_msg->resp = resp;
	fw_msg->resp_max_len = resp_max_len;
	fw_msg->timeout = timeout;
}

static void bng_re_query_hwrm_version(struct bng_re_dev *rdev)
{
	struct bnge_auxr_dev *aux_dev = rdev->aux_dev;
	struct hwrm_ver_get_output ver_get_resp = {};
	struct hwrm_ver_get_input ver_get_req = {};
	struct bng_re_chip_ctx *cctx;
	struct bnge_fw_msg fw_msg = {};
	int rc;

	bng_re_init_hwrm_hdr((void *)&ver_get_req, HWRM_VER_GET);
	ver_get_req.hwrm_intf_maj = HWRM_VERSION_MAJOR;
	ver_get_req.hwrm_intf_min = HWRM_VERSION_MINOR;
	ver_get_req.hwrm_intf_upd = HWRM_VERSION_UPDATE;
	bng_re_fill_fw_msg(&fw_msg, (void *)&ver_get_req, sizeof(ver_get_req),
			    (void *)&ver_get_resp, sizeof(ver_get_resp),
			    BNGE_DFLT_HWRM_CMD_TIMEOUT);
	rc = bnge_send_msg(aux_dev, &fw_msg);
	if (rc) {
		ibdev_err(&rdev->ibdev, "Failed to query HW version, rc = 0x%x",
			  rc);
		return;
	}

	cctx = rdev->chip_ctx;
	cctx->hwrm_intf_ver =
		(u64)le16_to_cpu(ver_get_resp.hwrm_intf_major) << 48 |
		(u64)le16_to_cpu(ver_get_resp.hwrm_intf_minor) << 32 |
		(u64)le16_to_cpu(ver_get_resp.hwrm_intf_build) << 16 |
		le16_to_cpu(ver_get_resp.hwrm_intf_patch);

	cctx->hwrm_cmd_max_timeout = le16_to_cpu(ver_get_resp.max_req_timeout);

	if (!cctx->hwrm_cmd_max_timeout)
		cctx->hwrm_cmd_max_timeout = BNG_ROCE_FW_MAX_TIMEOUT;
}

static int bng_re_dev_init(struct bng_re_dev *rdev)
{
	int rc;

	/* Registered a new RoCE device instance to netdev */
	rc = bng_re_register_netdev(rdev);
	if (rc) {
		ibdev_err(&rdev->ibdev,
				"Failed to register with netedev: %#x\n", rc);
		return -EINVAL;
	}

	set_bit(BNG_RE_FLAG_NETDEV_REGISTERED, &rdev->flags);

	if (rdev->aux_dev->auxr_info->msix_requested < BNG_RE_MIN_MSIX) {
		ibdev_err(&rdev->ibdev,
			  "RoCE requires minimum 2 MSI-X vectors, but only %d reserved\n",
			  rdev->aux_dev->auxr_info->msix_requested);
		bnge_unregister_dev(rdev->aux_dev);
		clear_bit(BNG_RE_FLAG_NETDEV_REGISTERED, &rdev->flags);
		return -EINVAL;
	}
	ibdev_dbg(&rdev->ibdev, "Got %d MSI-X vectors\n",
		  rdev->aux_dev->auxr_info->msix_requested);

	rc = bng_re_setup_chip_ctx(rdev);
	if (rc) {
		bnge_unregister_dev(rdev->aux_dev);
		clear_bit(BNG_RE_FLAG_NETDEV_REGISTERED, &rdev->flags);
		ibdev_err(&rdev->ibdev, "Failed to get chip context\n");
		return -EINVAL;
	}

	bng_re_query_hwrm_version(rdev);

	return 0;
}

static void bng_re_dev_uninit(struct bng_re_dev *rdev)
{
	bng_re_destroy_chip_ctx(rdev);
	if (test_and_clear_bit(BNG_RE_FLAG_NETDEV_REGISTERED, &rdev->flags))
		bnge_unregister_dev(rdev->aux_dev);
}

static int bng_re_add_device(struct auxiliary_device *adev)
{
	struct bnge_auxr_priv *auxr_priv =
		container_of(adev, struct bnge_auxr_priv, aux_dev);
	struct bng_re_en_dev_info *dev_info;
	struct bng_re_dev *rdev;
	int rc;

	dev_info = auxiliary_get_drvdata(adev);

	rdev = bng_re_dev_add(adev, auxr_priv->auxr_dev);
	if (!rdev) {
		rc = -ENOMEM;
		goto exit;
	}

	dev_info->rdev = rdev;

	rc = bng_re_dev_init(rdev);
	if (rc)
		goto re_dev_dealloc;

	return 0;

re_dev_dealloc:
	ib_dealloc_device(&rdev->ibdev);
exit:
	return rc;
}


static void bng_re_remove_device(struct bng_re_dev *rdev,
				 struct auxiliary_device *aux_dev)
{
	bng_re_dev_uninit(rdev);
	ib_dealloc_device(&rdev->ibdev);
}


static int bng_re_probe(struct auxiliary_device *adev,
			const struct auxiliary_device_id *id)
{
	struct bnge_auxr_priv *aux_priv =
		container_of(adev, struct bnge_auxr_priv, aux_dev);
	struct bng_re_en_dev_info *en_info;
	int rc;

	en_info = kzalloc(sizeof(*en_info), GFP_KERNEL);
	if (!en_info)
		return -ENOMEM;

	en_info->auxr_dev = aux_priv->auxr_dev;

	auxiliary_set_drvdata(adev, en_info);

	rc = bng_re_add_device(adev);
	if (rc)
		kfree(en_info);

	return rc;
}

static void bng_re_remove(struct auxiliary_device *adev)
{
	struct bng_re_en_dev_info *dev_info = auxiliary_get_drvdata(adev);
	struct bng_re_dev *rdev;

	rdev = dev_info->rdev;

	if (rdev)
		bng_re_remove_device(rdev, adev);
	kfree(dev_info);
}

static const struct auxiliary_device_id bng_re_id_table[] = {
	{ .name = BNG_RE_ADEV_NAME ".rdma", },
	{},
};

MODULE_DEVICE_TABLE(auxiliary, bng_re_id_table);

static struct auxiliary_driver bng_re_driver = {
	.name = "rdma",
	.probe = bng_re_probe,
	.remove = bng_re_remove,
	.id_table = bng_re_id_table,
};

static int __init bng_re_mod_init(void)
{
	int rc;


	rc = auxiliary_driver_register(&bng_re_driver);
	if (rc) {
		pr_err("%s: Failed to register auxiliary driver\n",
		       KBUILD_MODNAME);
	}
	return rc;
}

static void __exit bng_re_mod_exit(void)
{
	auxiliary_driver_unregister(&bng_re_driver);
}

module_init(bng_re_mod_init);
module_exit(bng_re_mod_exit);
