// SPDX-License-Identifier: ISC
/* Copyright (C) 2021 MediaTek Inc.
 *
 */
#include <linux/module.h>
#include <linux/firmware.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btmtk.h"

#define VERSION "0.1"

/* It is for mt79xx download rom patch*/
#define MTK_FW_ROM_PATCH_HEADER_SIZE	32
#define MTK_FW_ROM_PATCH_GD_SIZE	64
#define MTK_FW_ROM_PATCH_SEC_MAP_SIZE	64
#define MTK_SEC_MAP_COMMON_SIZE	12
#define MTK_SEC_MAP_NEED_SEND_SIZE	52

struct btmtk_patch_header {
	u8 datetime[16];
	u8 platform[4];
	__le16 hwver;
	__le16 swver;
	__le32 magicnum;
} __packed;

struct btmtk_global_desc {
	__le32 patch_ver;
	__le32 sub_sys;
	__le32 feature_opt;
	__le32 section_num;
} __packed;

struct btmtk_section_map {
	__le32 sectype;
	__le32 secoffset;
	__le32 secsize;
	union {
		__le32 u4SecSpec[13];
		struct {
			__le32 dlAddr;
			__le32 dlsize;
			__le32 seckeyidx;
			__le32 alignlen;
			__le32 sectype;
			__le32 dlmodecrctype;
			__le32 crc;
			__le32 reserved[6];
		} bin_info_spec;
	};
} __packed;

static void btmtk_coredump(struct hci_dev *hdev)
{
	int err;

	err = __hci_cmd_send(hdev, 0xfd5b, 0, NULL);
	if (err < 0)
		bt_dev_err(hdev, "Coredump failed (%d)", err);
}

static void btmtk_coredump_hdr(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btmediatek_data *data = hci_get_priv(hdev);
	char buf[80];

	snprintf(buf, sizeof(buf), "Controller Name: 0x%X\n",
		 data->dev_id);
	skb_put_data(skb, buf, strlen(buf));

	snprintf(buf, sizeof(buf), "Firmware Version: 0x%X\n",
		 data->cd_info.fw_version);
	skb_put_data(skb, buf, strlen(buf));

	snprintf(buf, sizeof(buf), "Driver: %s\n",
		 data->cd_info.driver_name);
	skb_put_data(skb, buf, strlen(buf));

	snprintf(buf, sizeof(buf), "Vendor: MediaTek\n");
	skb_put_data(skb, buf, strlen(buf));
}

static void btmtk_coredump_notify(struct hci_dev *hdev, int state)
{
	struct btmediatek_data *data = hci_get_priv(hdev);

	switch (state) {
	case HCI_DEVCOREDUMP_IDLE:
		data->cd_info.state = HCI_DEVCOREDUMP_IDLE;
		break;
	case HCI_DEVCOREDUMP_ACTIVE:
		data->cd_info.state = HCI_DEVCOREDUMP_ACTIVE;
		break;
	case HCI_DEVCOREDUMP_TIMEOUT:
	case HCI_DEVCOREDUMP_ABORT:
	case HCI_DEVCOREDUMP_DONE:
		data->cd_info.state = HCI_DEVCOREDUMP_IDLE;
		btmtk_reset_sync(hdev);
		break;
	}
}

int btmtk_setup_firmware_79xx(struct hci_dev *hdev, const char *fwname,
			      wmt_cmd_sync_func_t wmt_cmd_sync)
{
	struct btmtk_hci_wmt_params wmt_params;
	struct btmtk_patch_header *hdr;
	struct btmtk_global_desc *globaldesc = NULL;
	struct btmtk_section_map *sectionmap;
	const struct firmware *fw;
	const u8 *fw_ptr;
	const u8 *fw_bin_ptr;
	int err, dlen, i, status;
	u8 flag, first_block, retry;
	u32 section_num, dl_size, section_offset;
	u8 cmd[64];

	err = request_firmware(&fw, fwname, &hdev->dev);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to load firmware file (%d)", err);
		return err;
	}

	fw_ptr = fw->data;
	fw_bin_ptr = fw_ptr;
	hdr = (struct btmtk_patch_header *)fw_ptr;
	globaldesc = (struct btmtk_global_desc *)(fw_ptr + MTK_FW_ROM_PATCH_HEADER_SIZE);
	section_num = le32_to_cpu(globaldesc->section_num);

	bt_dev_info(hdev, "HW/SW Version: 0x%04x%04x, Build Time: %s",
		    le16_to_cpu(hdr->hwver), le16_to_cpu(hdr->swver), hdr->datetime);

	for (i = 0; i < section_num; i++) {
		first_block = 1;
		fw_ptr = fw_bin_ptr;
		sectionmap = (struct btmtk_section_map *)(fw_ptr + MTK_FW_ROM_PATCH_HEADER_SIZE +
			      MTK_FW_ROM_PATCH_GD_SIZE + MTK_FW_ROM_PATCH_SEC_MAP_SIZE * i);

		section_offset = le32_to_cpu(sectionmap->secoffset);
		dl_size = le32_to_cpu(sectionmap->bin_info_spec.dlsize);

		if (dl_size > 0) {
			retry = 20;
			while (retry > 0) {
				cmd[0] = 0; /* 0 means legacy dl mode. */
				memcpy(cmd + 1,
				       fw_ptr + MTK_FW_ROM_PATCH_HEADER_SIZE +
				       MTK_FW_ROM_PATCH_GD_SIZE +
				       MTK_FW_ROM_PATCH_SEC_MAP_SIZE * i +
				       MTK_SEC_MAP_COMMON_SIZE,
				       MTK_SEC_MAP_NEED_SEND_SIZE + 1);

				wmt_params.op = BTMTK_WMT_PATCH_DWNLD;
				wmt_params.status = &status;
				wmt_params.flag = 0;
				wmt_params.dlen = MTK_SEC_MAP_NEED_SEND_SIZE + 1;
				wmt_params.data = &cmd;

				err = wmt_cmd_sync(hdev, &wmt_params);
				if (err < 0) {
					bt_dev_err(hdev, "Failed to send wmt patch dwnld (%d)",
						   err);
					goto err_release_fw;
				}

				if (status == BTMTK_WMT_PATCH_UNDONE) {
					break;
				} else if (status == BTMTK_WMT_PATCH_PROGRESS) {
					msleep(100);
					retry--;
				} else if (status == BTMTK_WMT_PATCH_DONE) {
					goto next_section;
				} else {
					bt_dev_err(hdev, "Failed wmt patch dwnld status (%d)",
						   status);
					err = -EIO;
					goto err_release_fw;
				}
			}

			fw_ptr += section_offset;
			wmt_params.op = BTMTK_WMT_PATCH_DWNLD;
			wmt_params.status = NULL;

			while (dl_size > 0) {
				dlen = min_t(int, 250, dl_size);
				if (first_block == 1) {
					flag = 1;
					first_block = 0;
				} else if (dl_size - dlen <= 0) {
					flag = 3;
				} else {
					flag = 2;
				}

				wmt_params.flag = flag;
				wmt_params.dlen = dlen;
				wmt_params.data = fw_ptr;

				err = wmt_cmd_sync(hdev, &wmt_params);
				if (err < 0) {
					bt_dev_err(hdev, "Failed to send wmt patch dwnld (%d)",
						   err);
					goto err_release_fw;
				}

				dl_size -= dlen;
				fw_ptr += dlen;
			}
		}
next_section:
		continue;
	}
	/* Wait a few moments for firmware activation done */
	usleep_range(100000, 120000);

err_release_fw:
	release_firmware(fw);

	return err;
}
EXPORT_SYMBOL_GPL(btmtk_setup_firmware_79xx);

int btmtk_setup_firmware(struct hci_dev *hdev, const char *fwname,
			 wmt_cmd_sync_func_t wmt_cmd_sync)
{
	struct btmtk_hci_wmt_params wmt_params;
	const struct firmware *fw;
	const u8 *fw_ptr;
	size_t fw_size;
	int err, dlen;
	u8 flag, param;

	err = request_firmware(&fw, fwname, &hdev->dev);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to load firmware file (%d)", err);
		return err;
	}

	/* Power on data RAM the firmware relies on. */
	param = 1;
	wmt_params.op = BTMTK_WMT_FUNC_CTRL;
	wmt_params.flag = 3;
	wmt_params.dlen = sizeof(param);
	wmt_params.data = &param;
	wmt_params.status = NULL;

	err = wmt_cmd_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to power on data RAM (%d)", err);
		goto err_release_fw;
	}

	fw_ptr = fw->data;
	fw_size = fw->size;

	/* The size of patch header is 30 bytes, should be skip */
	if (fw_size < 30) {
		err = -EINVAL;
		goto err_release_fw;
	}

	fw_size -= 30;
	fw_ptr += 30;
	flag = 1;

	wmt_params.op = BTMTK_WMT_PATCH_DWNLD;
	wmt_params.status = NULL;

	while (fw_size > 0) {
		dlen = min_t(int, 250, fw_size);

		/* Tell device the position in sequence */
		if (fw_size - dlen <= 0)
			flag = 3;
		else if (fw_size < fw->size - 30)
			flag = 2;

		wmt_params.flag = flag;
		wmt_params.dlen = dlen;
		wmt_params.data = fw_ptr;

		err = wmt_cmd_sync(hdev, &wmt_params);
		if (err < 0) {
			bt_dev_err(hdev, "Failed to send wmt patch dwnld (%d)",
				   err);
			goto err_release_fw;
		}

		fw_size -= dlen;
		fw_ptr += dlen;
	}

	wmt_params.op = BTMTK_WMT_RST;
	wmt_params.flag = 4;
	wmt_params.dlen = 0;
	wmt_params.data = NULL;
	wmt_params.status = NULL;

	/* Activate funciton the firmware providing to */
	err = wmt_cmd_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to send wmt rst (%d)", err);
		goto err_release_fw;
	}

	/* Wait a few moments for firmware activation done */
	usleep_range(10000, 12000);

err_release_fw:
	release_firmware(fw);

	return err;
}
EXPORT_SYMBOL_GPL(btmtk_setup_firmware);

int btmtk_set_bdaddr(struct hci_dev *hdev, const bdaddr_t *bdaddr)
{
	struct sk_buff *skb;
	long ret;

	skb = __hci_cmd_sync(hdev, 0xfc1a, 6, bdaddr, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		ret = PTR_ERR(skb);
		bt_dev_err(hdev, "changing Mediatek device address failed (%ld)",
			   ret);
		return ret;
	}
	kfree_skb(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(btmtk_set_bdaddr);

void btmtk_reset_sync(struct hci_dev *hdev)
{
	struct btmediatek_data *reset_work = hci_get_priv(hdev);
	int err;

	hci_dev_lock(hdev);

	err = hci_cmd_sync_queue(hdev, reset_work->reset_sync, NULL, NULL);
	if (err)
		bt_dev_err(hdev, "failed to reset (%d)", err);

	hci_dev_unlock(hdev);
}
EXPORT_SYMBOL_GPL(btmtk_reset_sync);

int btmtk_register_coredump(struct hci_dev *hdev, const char *name,
			    u32 fw_version)
{
	struct btmediatek_data *data = hci_get_priv(hdev);

	if (!IS_ENABLED(CONFIG_DEV_COREDUMP))
		return -EOPNOTSUPP;

	data->cd_info.fw_version = fw_version;
	data->cd_info.state = HCI_DEVCOREDUMP_IDLE;
	data->cd_info.driver_name = name;

	return hci_devcd_register(hdev, btmtk_coredump, btmtk_coredump_hdr,
				  btmtk_coredump_notify);
}
EXPORT_SYMBOL_GPL(btmtk_register_coredump);

int btmtk_process_coredump(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btmediatek_data *data = hci_get_priv(hdev);
	int err;

	if (!IS_ENABLED(CONFIG_DEV_COREDUMP)) {
		kfree_skb(skb);
		return 0;
	}

	switch (data->cd_info.state) {
	case HCI_DEVCOREDUMP_IDLE:
		err = hci_devcd_init(hdev, MTK_COREDUMP_SIZE);
		if (err < 0) {
			kfree_skb(skb);
			break;
		}
		data->cd_info.cnt = 0;

		/* It is supposed coredump can be done within 5 seconds */
		schedule_delayed_work(&hdev->dump.dump_timeout,
				      msecs_to_jiffies(5000));
		fallthrough;
	case HCI_DEVCOREDUMP_ACTIVE:
	default:
		err = hci_devcd_append(hdev, skb);
		if (err < 0)
			break;
		data->cd_info.cnt++;

		/* Mediatek coredump data would be more than MTK_COREDUMP_NUM */
		if (data->cd_info.cnt > MTK_COREDUMP_NUM &&
		    skb->len > MTK_COREDUMP_END_LEN)
			if (!memcmp((char *)&skb->data[skb->len - MTK_COREDUMP_END_LEN],
				    MTK_COREDUMP_END, MTK_COREDUMP_END_LEN - 1)) {
				bt_dev_info(hdev, "Mediatek coredump end");
				hci_devcd_complete(hdev);
			}

		break;
	}

	return err;
}
EXPORT_SYMBOL_GPL(btmtk_process_coredump);

MODULE_AUTHOR("Sean Wang <sean.wang@mediatek.com>");
MODULE_AUTHOR("Mark Chen <mark-yw.chen@mediatek.com>");
MODULE_DESCRIPTION("Bluetooth support for MediaTek devices ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(FIRMWARE_MT7622);
MODULE_FIRMWARE(FIRMWARE_MT7663);
MODULE_FIRMWARE(FIRMWARE_MT7668);
MODULE_FIRMWARE(FIRMWARE_MT7922);
MODULE_FIRMWARE(FIRMWARE_MT7961);
MODULE_FIRMWARE(FIRMWARE_MT7925);
