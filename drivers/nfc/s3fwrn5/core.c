/*
 * NCI based driver for Samsung S3FWRN5 NFC chip
 *
 * Copyright (C) 2015 Samsung Electrnoics
 * Robert Baldyga <r.baldyga@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
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

static int s3fwrn5_firmware_update(struct s3fwrn5_info *info)
{
	bool need_update;
	int ret;

	s3fwrn5_fw_init(&info->fw_info, "sec_s3fwrn5_firmware.bin");

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
	if (ret < 0)
		kfree_skb(skb);

	mutex_unlock(&info->mutex);
	return ret;
}

static int s3fwrn5_nci_post_setup(struct nci_dev *ndev)
{
	struct s3fwrn5_info *info = nci_get_drvdata(ndev);
	int ret;

	ret = s3fwrn5_firmware_update(info);
	if (ret < 0)
		goto out;

	/* NCI core reset */

	s3fwrn5_set_mode(info, S3FWRN5_MODE_NCI);
	s3fwrn5_set_wake(info, true);

	ret = nci_core_reset(info->ndev);
	if (ret < 0)
		goto out;

	ret = nci_core_init(info->ndev);

out:
	return ret;
}

static struct nci_ops s3fwrn5_nci_ops = {
	.open = s3fwrn5_nci_open,
	.close = s3fwrn5_nci_close,
	.send = s3fwrn5_nci_send,
	.post_setup = s3fwrn5_nci_post_setup,
};

int s3fwrn5_probe(struct nci_dev **ndev, void *phy_id, struct device *pdev,
	const struct s3fwrn5_phy_ops *phy_ops, unsigned int max_payload)
{
	struct s3fwrn5_info *info;
	int ret;

	info = devm_kzalloc(pdev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->phy_id = phy_id;
	info->pdev = pdev;
	info->phy_ops = phy_ops;
	info->max_payload = max_payload;
	mutex_init(&info->mutex);

	s3fwrn5_set_mode(info, S3FWRN5_MODE_COLD);

	s3fwrn5_nci_get_prop_ops(&s3fwrn5_nci_ops.prop_ops,
		&s3fwrn5_nci_ops.n_prop_ops);

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
		return -ENODEV;
	}
}
EXPORT_SYMBOL(s3fwrn5_recv_frame);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Samsung S3FWRN5 NFC driver");
MODULE_AUTHOR("Robert Baldyga <r.baldyga@samsung.com>");
