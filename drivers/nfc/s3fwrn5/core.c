// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NCI based driver for Samsung S3FWRN5 NFC chip
 *
 * Copyright (C) 2015 Samsung Electrnoics
 * Robert Baldyga <r.baldyga@samsung.com>
 */

#include <linux/module.h>
#include <net/nfc/nci_core.h>

#include "s3fwrn5.h"
#include "firmware.h"
#include "nci.h"

#define S3FWRN5_NFC_PROTOCOLS  (NFC_PROTO_JEWEL_MASK | \
				NFC_PROTO_MIFARE_MASK | \
				NFC_PROTO_FELICA_MASK | \
				NFC_PROTO_ISO14443_MASK | \
				NFC_PROTO_ISO14443_B_MASK | \
				NFC_PROTO_ISO15693_MASK)

static int s3fwrn5_firmware_init(struct s3fwrn5_info *info)
{
	struct s3fwrn5_fw_info *fw_info = &info->fw_info;
	int ret;

	s3fwrn5_fw_init(fw_info, "sec_s3fwrn5_firmware.bin");

	/* Get firmware data */
	ret = s3fwrn5_fw_request_firmware(fw_info);
	if (ret < 0)
		dev_err(&fw_info->ndev->nfc_dev->dev,
			"Failed to get fw file, ret=%02x\n", ret);
	return ret;
}

static int s3fwrn5_firmware_update(struct s3fwrn5_info *info)
{
	bool need_update;
	int ret;

	/* Update firmware */

	s3fwrn5_set_wake(info, false);
	s3fwrn5_set_mode(info, S3FWRN5_MODE_FW);

	ret = s3fwrn5_fw_setup(&info->fw_info);
	if (ret < 0)
		return ret;

	need_update = s3fwrn5_fw_check_version(&info->fw_info,
		info->ndev->manufact_specific_info);
	if (!need_update)
		goto out;

	dev_info(&info->ndev->nfc_dev->dev, "Detected new firmware version\n");

	ret = s3fwrn5_fw_download(&info->fw_info);
	if (ret < 0)
		goto out;

	/* Update RF configuration */

	s3fwrn5_set_mode(info, S3FWRN5_MODE_NCI);

	s3fwrn5_set_wake(info, true);
	ret = s3fwrn5_nci_rf_configure(info, "sec_s3fwrn5_rfreg.bin");
	s3fwrn5_set_wake(info, false);

out:
	s3fwrn5_set_mode(info, S3FWRN5_MODE_COLD);
	s3fwrn5_fw_cleanup(&info->fw_info);
	return ret;
}

static int s3fwrn5_nci_open(struct nci_dev *ndev)
{
	struct s3fwrn5_info *info = nci_get_drvdata(ndev);

	if (s3fwrn5_get_mode(info) != S3FWRN5_MODE_COLD)
		return  -EBUSY;

	s3fwrn5_set_mode(info, S3FWRN5_MODE_NCI);
	s3fwrn5_set_wake(info, true);

	return 0;
}

static int s3fwrn5_nci_close(struct nci_dev *ndev)
{
	struct s3fwrn5_info *info = nci_get_drvdata(ndev);

	s3fwrn5_set_wake(info, false);
	s3fwrn5_set_mode(info, S3FWRN5_MODE_COLD);

	return 0;
}

static int s3fwrn5_nci_send(struct nci_dev *ndev, struct sk_buff *skb)
{
	struct s3fwrn5_info *info = nci_get_drvdata(ndev);
	int ret;

	mutex_lock(&info->mutex);

	if (s3fwrn5_get_mode(info) != S3FWRN5_MODE_NCI) {
		mutex_unlock(&info->mutex);
		return -EINVAL;
	}

	ret = s3fwrn5_write(info, skb);
	if (ret < 0) {
		kfree_skb(skb);
		mutex_unlock(&info->mutex);
		return ret;
	}

	consume_skb(skb);
	mutex_unlock(&info->mutex);
	return 0;
}

static int s3fwrn5_nci_post_setup(struct nci_dev *ndev)
{
	struct s3fwrn5_info *info = nci_get_drvdata(ndev);
	int ret;

	if (s3fwrn5_firmware_init(info)) {
		//skip bootloader mode
		return 0;
	}

	ret = s3fwrn5_firmware_update(info);
	if (ret < 0)
		return ret;

	/* NCI core reset */

	s3fwrn5_set_mode(info, S3FWRN5_MODE_NCI);
	s3fwrn5_set_wake(info, true);

	ret = nci_core_reset(info->ndev);
	if (ret < 0)
		return ret;

	return nci_core_init(info->ndev);
}

static const struct nci_ops s3fwrn5_nci_ops = {
	.open = s3fwrn5_nci_open,
	.close = s3fwrn5_nci_close,
	.send = s3fwrn5_nci_send,
	.post_setup = s3fwrn5_nci_post_setup,
	.prop_ops = s3fwrn5_nci_prop_ops,
	.n_prop_ops = ARRAY_SIZE(s3fwrn5_nci_prop_ops),
};

int s3fwrn5_probe(struct nci_dev **ndev, void *phy_id, struct device *pdev,
	const struct s3fwrn5_phy_ops *phy_ops)
{
	struct s3fwrn5_info *info;
	int ret;

	info = devm_kzalloc(pdev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->phy_id = phy_id;
	info->pdev = pdev;
	info->phy_ops = phy_ops;
	mutex_init(&info->mutex);

	s3fwrn5_set_mode(info, S3FWRN5_MODE_COLD);

	info->ndev = nci_allocate_device(&s3fwrn5_nci_ops,
		S3FWRN5_NFC_PROTOCOLS, 0, 0);
	if (!info->ndev)
		return -ENOMEM;

	nci_set_parent_dev(info->ndev, pdev);
	nci_set_drvdata(info->ndev, info);

	ret = nci_register_device(info->ndev);
	if (ret < 0) {
		nci_free_device(info->ndev);
		return ret;
	}

	info->fw_info.ndev = info->ndev;

	*ndev = info->ndev;

	return ret;
}
EXPORT_SYMBOL(s3fwrn5_probe);

void s3fwrn5_remove(struct nci_dev *ndev)
{
	struct s3fwrn5_info *info = nci_get_drvdata(ndev);

	s3fwrn5_set_mode(info, S3FWRN5_MODE_COLD);

	nci_unregister_device(ndev);
	nci_free_device(ndev);
}
EXPORT_SYMBOL(s3fwrn5_remove);

int s3fwrn5_recv_frame(struct nci_dev *ndev, struct sk_buff *skb,
	enum s3fwrn5_mode mode)
{
	switch (mode) {
	case S3FWRN5_MODE_NCI:
		return nci_recv_frame(ndev, skb);
	case S3FWRN5_MODE_FW:
		return s3fwrn5_fw_recv_frame(ndev, skb);
	default:
		kfree_skb(skb);
		return -ENODEV;
	}
}
EXPORT_SYMBOL(s3fwrn5_recv_frame);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Samsung S3FWRN5 NFC driver");
MODULE_AUTHOR("Robert Baldyga <r.baldyga@samsung.com>");
