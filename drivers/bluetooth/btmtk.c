// SPDX-License-Identifier: ISC
/* Copyright (C) 2021 MediaTek Inc.
 *
 */
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/usb.h>
#include <linux/iopoll.h>
#include <asm/unaligned.h>

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

/* It is for mt79xx iso data transmission setting */
#define MTK_ISO_THRESHOLD	264

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
	struct btmtk_data *data = hci_get_priv(hdev);
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
	struct btmtk_data *data = hci_get_priv(hdev);

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

void btmtk_fw_get_filename(char *buf, size_t size, u32 dev_id, u32 fw_ver,
			   u32 fw_flavor)
{
	if (dev_id == 0x7925)
		snprintf(buf, size,
			 "mediatek/mt%04x/BT_RAM_CODE_MT%04x_1_%x_hdr.bin",
			 dev_id & 0xffff, dev_id & 0xffff, (fw_ver & 0xff) + 1);
	else if (dev_id == 0x7961 && fw_flavor)
		snprintf(buf, size,
			 "mediatek/BT_RAM_CODE_MT%04x_1a_%x_hdr.bin",
			 dev_id & 0xffff, (fw_ver & 0xff) + 1);
	else
		snprintf(buf, size,
			 "mediatek/BT_RAM_CODE_MT%04x_1_%x_hdr.bin",
			 dev_id & 0xffff, (fw_ver & 0xff) + 1);
}
EXPORT_SYMBOL_GPL(btmtk_fw_get_filename);

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
	struct btmtk_data *reset_work = hci_get_priv(hdev);
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
	struct btmtk_data *data = hci_get_priv(hdev);

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
	struct btmtk_data *data = hci_get_priv(hdev);
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

#if IS_ENABLED(CONFIG_BT_HCIBTUSB_MTK)
static void btmtk_usb_wmt_recv(struct urb *urb)
{
	struct hci_dev *hdev = urb->context;
	struct btmtk_data *data = hci_get_priv(hdev);
	struct sk_buff *skb;
	int err;

	if (urb->status == 0 && urb->actual_length > 0) {
		hdev->stat.byte_rx += urb->actual_length;

		/* WMT event shouldn't be fragmented and the size should be
		 * less than HCI_WMT_MAX_EVENT_SIZE.
		 */
		skb = bt_skb_alloc(HCI_WMT_MAX_EVENT_SIZE, GFP_ATOMIC);
		if (!skb) {
			hdev->stat.err_rx++;
			kfree(urb->setup_packet);
			return;
		}

		hci_skb_pkt_type(skb) = HCI_EVENT_PKT;
		skb_put_data(skb, urb->transfer_buffer, urb->actual_length);

		/* When someone waits for the WMT event, the skb is being cloned
		 * and being processed the events from there then.
		 */
		if (test_bit(BTMTK_TX_WAIT_VND_EVT, &data->flags)) {
			data->evt_skb = skb_clone(skb, GFP_ATOMIC);
			if (!data->evt_skb) {
				kfree_skb(skb);
				kfree(urb->setup_packet);
				return;
			}
		}

		err = hci_recv_frame(hdev, skb);
		if (err < 0) {
			kfree_skb(data->evt_skb);
			data->evt_skb = NULL;
			kfree(urb->setup_packet);
			return;
		}

		if (test_and_clear_bit(BTMTK_TX_WAIT_VND_EVT,
				       &data->flags)) {
			/* Barrier to sync with other CPUs */
			smp_mb__after_atomic();
			wake_up_bit(&data->flags,
				    BTMTK_TX_WAIT_VND_EVT);
		}
		kfree(urb->setup_packet);
		return;
	} else if (urb->status == -ENOENT) {
		/* Avoid suspend failed when usb_kill_urb */
		return;
	}

	usb_mark_last_busy(data->udev);

	/* The URB complete handler is still called with urb->actual_length = 0
	 * when the event is not available, so we should keep re-submitting
	 * URB until WMT event returns, Also, It's necessary to wait some time
	 * between the two consecutive control URBs to relax the target device
	 * to generate the event. Otherwise, the WMT event cannot return from
	 * the device successfully.
	 */
	udelay(500);

	usb_anchor_urb(urb, data->ctrl_anchor);
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		kfree(urb->setup_packet);
		/* -EPERM: urb is being killed;
		 * -ENODEV: device got disconnected
		 */
		if (err != -EPERM && err != -ENODEV)
			bt_dev_err(hdev, "urb %p failed to resubmit (%d)",
				   urb, -err);
		usb_unanchor_urb(urb);
	}
}

static int btmtk_usb_submit_wmt_recv_urb(struct hci_dev *hdev)
{
	struct btmtk_data *data = hci_get_priv(hdev);
	struct usb_ctrlrequest *dr;
	unsigned char *buf;
	int err, size = 64;
	unsigned int pipe;
	struct urb *urb;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return -ENOMEM;

	dr = kmalloc(sizeof(*dr), GFP_KERNEL);
	if (!dr) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	dr->bRequestType = USB_TYPE_VENDOR | USB_DIR_IN;
	dr->bRequest     = 1;
	dr->wIndex       = cpu_to_le16(0);
	dr->wValue       = cpu_to_le16(48);
	dr->wLength      = cpu_to_le16(size);

	buf = kmalloc(size, GFP_KERNEL);
	if (!buf) {
		kfree(dr);
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvctrlpipe(data->udev, 0);

	usb_fill_control_urb(urb, data->udev, pipe, (void *)dr,
			     buf, size, btmtk_usb_wmt_recv, hdev);

	urb->transfer_flags |= URB_FREE_BUFFER;

	usb_anchor_urb(urb, data->ctrl_anchor);
	err = usb_submit_urb(urb, GFP_KERNEL);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			bt_dev_err(hdev, "urb %p submission failed (%d)",
				   urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static int btmtk_usb_hci_wmt_sync(struct hci_dev *hdev,
				  struct btmtk_hci_wmt_params *wmt_params)
{
	struct btmtk_data *data = hci_get_priv(hdev);
	struct btmtk_hci_wmt_evt_funcc *wmt_evt_funcc;
	u32 hlen, status = BTMTK_WMT_INVALID;
	struct btmtk_hci_wmt_evt *wmt_evt;
	struct btmtk_hci_wmt_cmd *wc;
	struct btmtk_wmt_hdr *hdr;
	int err;

	/* Send the WMT command and wait until the WMT event returns */
	hlen = sizeof(*hdr) + wmt_params->dlen;
	if (hlen > 255)
		return -EINVAL;

	wc = kzalloc(hlen, GFP_KERNEL);
	if (!wc)
		return -ENOMEM;

	hdr = &wc->hdr;
	hdr->dir = 1;
	hdr->op = wmt_params->op;
	hdr->dlen = cpu_to_le16(wmt_params->dlen + 1);
	hdr->flag = wmt_params->flag;
	memcpy(wc->data, wmt_params->data, wmt_params->dlen);

	set_bit(BTMTK_TX_WAIT_VND_EVT, &data->flags);

	/* WMT cmd/event doesn't follow up the generic HCI cmd/event handling,
	 * it needs constantly polling control pipe until the host received the
	 * WMT event, thus, we should require to specifically acquire PM counter
	 * on the USB to prevent the interface from entering auto suspended
	 * while WMT cmd/event in progress.
	 */
	err = usb_autopm_get_interface(data->intf);
	if (err < 0)
		goto err_free_wc;

	err = __hci_cmd_send(hdev, 0xfc6f, hlen, wc);

	if (err < 0) {
		clear_bit(BTMTK_TX_WAIT_VND_EVT, &data->flags);
		usb_autopm_put_interface(data->intf);
		goto err_free_wc;
	}

	/* Submit control IN URB on demand to process the WMT event */
	err = btmtk_usb_submit_wmt_recv_urb(hdev);

	usb_autopm_put_interface(data->intf);

	if (err < 0)
		goto err_free_wc;

	/* The vendor specific WMT commands are all answered by a vendor
	 * specific event and will have the Command Status or Command
	 * Complete as with usual HCI command flow control.
	 *
	 * After sending the command, wait for BTUSB_TX_WAIT_VND_EVT
	 * state to be cleared. The driver specific event receive routine
	 * will clear that state and with that indicate completion of the
	 * WMT command.
	 */
	err = wait_on_bit_timeout(&data->flags, BTMTK_TX_WAIT_VND_EVT,
				  TASK_INTERRUPTIBLE, HCI_INIT_TIMEOUT);
	if (err == -EINTR) {
		bt_dev_err(hdev, "Execution of wmt command interrupted");
		clear_bit(BTMTK_TX_WAIT_VND_EVT, &data->flags);
		goto err_free_wc;
	}

	if (err) {
		bt_dev_err(hdev, "Execution of wmt command timed out");
		clear_bit(BTMTK_TX_WAIT_VND_EVT, &data->flags);
		err = -ETIMEDOUT;
		goto err_free_wc;
	}

	if (data->evt_skb == NULL)
		goto err_free_wc;

	/* Parse and handle the return WMT event */
	wmt_evt = (struct btmtk_hci_wmt_evt *)data->evt_skb->data;
	if (wmt_evt->whdr.op != hdr->op) {
		bt_dev_err(hdev, "Wrong op received %d expected %d",
			   wmt_evt->whdr.op, hdr->op);
		err = -EIO;
		goto err_free_skb;
	}

	switch (wmt_evt->whdr.op) {
	case BTMTK_WMT_SEMAPHORE:
		if (wmt_evt->whdr.flag == 2)
			status = BTMTK_WMT_PATCH_UNDONE;
		else
			status = BTMTK_WMT_PATCH_DONE;
		break;
	case BTMTK_WMT_FUNC_CTRL:
		wmt_evt_funcc = (struct btmtk_hci_wmt_evt_funcc *)wmt_evt;
		if (be16_to_cpu(wmt_evt_funcc->status) == 0x404)
			status = BTMTK_WMT_ON_DONE;
		else if (be16_to_cpu(wmt_evt_funcc->status) == 0x420)
			status = BTMTK_WMT_ON_PROGRESS;
		else
			status = BTMTK_WMT_ON_UNDONE;
		break;
	case BTMTK_WMT_PATCH_DWNLD:
		if (wmt_evt->whdr.flag == 2)
			status = BTMTK_WMT_PATCH_DONE;
		else if (wmt_evt->whdr.flag == 1)
			status = BTMTK_WMT_PATCH_PROGRESS;
		else
			status = BTMTK_WMT_PATCH_UNDONE;
		break;
	}

	if (wmt_params->status)
		*wmt_params->status = status;

err_free_skb:
	kfree_skb(data->evt_skb);
	data->evt_skb = NULL;
err_free_wc:
	kfree(wc);
	return err;
}

static int btmtk_usb_func_query(struct hci_dev *hdev)
{
	struct btmtk_hci_wmt_params wmt_params;
	int status, err;
	u8 param = 0;

	/* Query whether the function is enabled */
	wmt_params.op = BTMTK_WMT_FUNC_CTRL;
	wmt_params.flag = 4;
	wmt_params.dlen = sizeof(param);
	wmt_params.data = &param;
	wmt_params.status = &status;

	err = btmtk_usb_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to query function status (%d)", err);
		return err;
	}

	return status;
}

static int btmtk_usb_uhw_reg_write(struct hci_dev *hdev, u32 reg, u32 val)
{
	struct btmtk_data *data = hci_get_priv(hdev);
	int pipe, err;
	void *buf;

	buf = kzalloc(4, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	put_unaligned_le32(val, buf);

	pipe = usb_sndctrlpipe(data->udev, 0);
	err = usb_control_msg(data->udev, pipe, 0x02,
			      0x5E,
			      reg >> 16, reg & 0xffff,
			      buf, 4, USB_CTRL_SET_TIMEOUT);
	if (err < 0)
		bt_dev_err(hdev, "Failed to write uhw reg(%d)", err);

	kfree(buf);

	return err;
}

static int btmtk_usb_uhw_reg_read(struct hci_dev *hdev, u32 reg, u32 *val)
{
	struct btmtk_data *data = hci_get_priv(hdev);
	int pipe, err;
	void *buf;

	buf = kzalloc(4, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	pipe = usb_rcvctrlpipe(data->udev, 0);
	err = usb_control_msg(data->udev, pipe, 0x01,
			      0xDE,
			      reg >> 16, reg & 0xffff,
			      buf, 4, USB_CTRL_GET_TIMEOUT);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to read uhw reg(%d)", err);
		goto err_free_buf;
	}

	*val = get_unaligned_le32(buf);
	bt_dev_dbg(hdev, "reg=%x, value=0x%08x", reg, *val);

err_free_buf:
	kfree(buf);

	return err;
}

static int btmtk_usb_reg_read(struct hci_dev *hdev, u32 reg, u32 *val)
{
	struct btmtk_data *data = hci_get_priv(hdev);
	int pipe, err, size = sizeof(u32);
	void *buf;

	buf = kzalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	pipe = usb_rcvctrlpipe(data->udev, 0);
	err = usb_control_msg(data->udev, pipe, 0x63,
			      USB_TYPE_VENDOR | USB_DIR_IN,
			      reg >> 16, reg & 0xffff,
			      buf, size, USB_CTRL_GET_TIMEOUT);
	if (err < 0)
		goto err_free_buf;

	*val = get_unaligned_le32(buf);

err_free_buf:
	kfree(buf);

	return err;
}

static int btmtk_usb_id_get(struct hci_dev *hdev, u32 reg, u32 *id)
{
	return btmtk_usb_reg_read(hdev, reg, id);
}

static u32 btmtk_usb_reset_done(struct hci_dev *hdev)
{
	u32 val = 0;

	btmtk_usb_uhw_reg_read(hdev, MTK_BT_MISC, &val);

	return val & MTK_BT_RST_DONE;
}

int btmtk_usb_subsys_reset(struct hci_dev *hdev, u32 dev_id)
{
	u32 val;
	int err;

	if (dev_id == 0x7922) {
		err = btmtk_usb_uhw_reg_read(hdev, MTK_BT_SUBSYS_RST, &val);
		if (err < 0)
			return err;
		val |= 0x00002020;
		err = btmtk_usb_uhw_reg_write(hdev, MTK_BT_SUBSYS_RST, val);
		if (err < 0)
			return err;
		err = btmtk_usb_uhw_reg_write(hdev, MTK_EP_RST_OPT, 0x00010001);
		if (err < 0)
			return err;
		err = btmtk_usb_uhw_reg_read(hdev, MTK_BT_SUBSYS_RST, &val);
		if (err < 0)
			return err;
		val |= BIT(0);
		err = btmtk_usb_uhw_reg_write(hdev, MTK_BT_SUBSYS_RST, val);
		if (err < 0)
			return err;
		msleep(100);
	} else if (dev_id == 0x7925) {
		err = btmtk_usb_uhw_reg_read(hdev, MTK_BT_RESET_REG_CONNV3, &val);
		if (err < 0)
			return err;
		val |= (1 << 5);
		err = btmtk_usb_uhw_reg_write(hdev, MTK_BT_RESET_REG_CONNV3, val);
		if (err < 0)
			return err;
		err = btmtk_usb_uhw_reg_read(hdev, MTK_BT_RESET_REG_CONNV3, &val);
		if (err < 0)
			return err;
		val &= 0xFFFF00FF;
		val |= (1 << 13);
		err = btmtk_usb_uhw_reg_write(hdev, MTK_BT_RESET_REG_CONNV3, val);
		if (err < 0)
			return err;
		err = btmtk_usb_uhw_reg_write(hdev, MTK_EP_RST_OPT, 0x00010001);
		if (err < 0)
			return err;
		err = btmtk_usb_uhw_reg_read(hdev, MTK_BT_RESET_REG_CONNV3, &val);
		if (err < 0)
			return err;
		val |= (1 << 0);
		err = btmtk_usb_uhw_reg_write(hdev, MTK_BT_RESET_REG_CONNV3, val);
		if (err < 0)
			return err;
		err = btmtk_usb_uhw_reg_write(hdev, MTK_UDMA_INT_STA_BT, 0x000000FF);
		if (err < 0)
			return err;
		err = btmtk_usb_uhw_reg_read(hdev, MTK_UDMA_INT_STA_BT, &val);
		if (err < 0)
			return err;
		err = btmtk_usb_uhw_reg_write(hdev, MTK_UDMA_INT_STA_BT1, 0x000000FF);
		if (err < 0)
			return err;
		err = btmtk_usb_uhw_reg_read(hdev, MTK_UDMA_INT_STA_BT1, &val);
		if (err < 0)
			return err;
		msleep(100);
	} else {
		/* It's Device EndPoint Reset Option Register */
		bt_dev_dbg(hdev, "Initiating reset mechanism via uhw");
		err = btmtk_usb_uhw_reg_write(hdev, MTK_EP_RST_OPT, MTK_EP_RST_IN_OUT_OPT);
		if (err < 0)
			return err;
		err = btmtk_usb_uhw_reg_read(hdev, MTK_BT_WDT_STATUS, &val);
		if (err < 0)
			return err;
		/* Reset the bluetooth chip via USB interface. */
		err = btmtk_usb_uhw_reg_write(hdev, MTK_BT_SUBSYS_RST, 1);
		if (err < 0)
			return err;
		err = btmtk_usb_uhw_reg_write(hdev, MTK_UDMA_INT_STA_BT, 0x000000FF);
		if (err < 0)
			return err;
		err = btmtk_usb_uhw_reg_read(hdev, MTK_UDMA_INT_STA_BT, &val);
		if (err < 0)
			return err;
		err = btmtk_usb_uhw_reg_write(hdev, MTK_UDMA_INT_STA_BT1, 0x000000FF);
		if (err < 0)
			return err;
		err = btmtk_usb_uhw_reg_read(hdev, MTK_UDMA_INT_STA_BT1, &val);
		if (err < 0)
			return err;
		/* MT7921 need to delay 20ms between toggle reset bit */
		msleep(20);
		err = btmtk_usb_uhw_reg_write(hdev, MTK_BT_SUBSYS_RST, 0);
		if (err < 0)
			return err;
		err = btmtk_usb_uhw_reg_read(hdev, MTK_BT_SUBSYS_RST, &val);
		if (err < 0)
			return err;
	}

	err = readx_poll_timeout(btmtk_usb_reset_done, hdev, val,
				 val & MTK_BT_RST_DONE, 20000, 1000000);
	if (err < 0)
		bt_dev_err(hdev, "Reset timeout");

	if (dev_id == 0x7922) {
		err = btmtk_usb_uhw_reg_write(hdev, MTK_UDMA_INT_STA_BT, 0x000000FF);
		if (err < 0)
			return err;
	}

	err = btmtk_usb_id_get(hdev, 0x70010200, &val);
	if (err < 0 || !val)
		bt_dev_err(hdev, "Can't get device id, subsys reset fail.");

	return err;
}
EXPORT_SYMBOL_GPL(btmtk_usb_subsys_reset);

int btmtk_usb_recv_acl(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btmtk_data *data = hci_get_priv(hdev);
	u16 handle = le16_to_cpu(hci_acl_hdr(skb)->handle);

	switch (handle) {
	case 0xfc6f:		/* Firmware dump from device */
		/* When the firmware hangs, the device can no longer
		 * suspend and thus disable auto-suspend.
		 */
		usb_disable_autosuspend(data->udev);

		/* We need to forward the diagnostic packet to userspace daemon
		 * for backward compatibility, so we have to clone the packet
		 * extraly for the in-kernel coredump support.
		 */
		if (IS_ENABLED(CONFIG_DEV_COREDUMP)) {
			struct sk_buff *skb_cd = skb_clone(skb, GFP_ATOMIC);

			if (skb_cd)
				btmtk_process_coredump(hdev, skb_cd);
		}

		fallthrough;
	case 0x05ff:		/* Firmware debug logging 1 */
	case 0x05fe:		/* Firmware debug logging 2 */
		return hci_recv_diag(hdev, skb);
	}

	return hci_recv_frame(hdev, skb);
}
EXPORT_SYMBOL_GPL(btmtk_usb_recv_acl);

static int btmtk_isopkt_pad(struct hci_dev *hdev, struct sk_buff *skb)
{
	if (skb->len > MTK_ISO_THRESHOLD)
		return -EINVAL;

	if (skb_pad(skb, MTK_ISO_THRESHOLD - skb->len))
		return -ENOMEM;

	__skb_put(skb, MTK_ISO_THRESHOLD - skb->len);

	return 0;
}

static int __set_mtk_intr_interface(struct hci_dev *hdev)
{
	struct btmtk_data *btmtk_data = hci_get_priv(hdev);
	struct usb_interface *intf = btmtk_data->isopkt_intf;
	int i, err;

	if (!btmtk_data->isopkt_intf)
		return -ENODEV;

	err = usb_set_interface(btmtk_data->udev, MTK_ISO_IFNUM, 1);
	if (err < 0) {
		bt_dev_err(hdev, "setting interface failed (%d)", -err);
		return err;
	}

	btmtk_data->isopkt_tx_ep = NULL;
	btmtk_data->isopkt_rx_ep = NULL;

	for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
		struct usb_endpoint_descriptor *ep_desc;

		ep_desc = &intf->cur_altsetting->endpoint[i].desc;

		if (!btmtk_data->isopkt_tx_ep &&
		    usb_endpoint_is_int_out(ep_desc)) {
			btmtk_data->isopkt_tx_ep = ep_desc;
			continue;
		}

		if (!btmtk_data->isopkt_rx_ep &&
		    usb_endpoint_is_int_in(ep_desc)) {
			btmtk_data->isopkt_rx_ep = ep_desc;
			continue;
		}
	}

	if (!btmtk_data->isopkt_tx_ep ||
	    !btmtk_data->isopkt_rx_ep) {
		bt_dev_err(hdev, "invalid interrupt descriptors");
		return -ENODEV;
	}

	return 0;
}

struct urb *alloc_mtk_intr_urb(struct hci_dev *hdev, struct sk_buff *skb,
			       usb_complete_t tx_complete)
{
	struct btmtk_data *btmtk_data = hci_get_priv(hdev);
	struct urb *urb;
	unsigned int pipe;

	if (!btmtk_data->isopkt_tx_ep)
		return ERR_PTR(-ENODEV);

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return ERR_PTR(-ENOMEM);

	if (btmtk_isopkt_pad(hdev, skb))
		return ERR_PTR(-EINVAL);

	pipe = usb_sndintpipe(btmtk_data->udev,
			      btmtk_data->isopkt_tx_ep->bEndpointAddress);

	usb_fill_int_urb(urb, btmtk_data->udev, pipe,
			 skb->data, skb->len, tx_complete,
			 skb, btmtk_data->isopkt_tx_ep->bInterval);

	skb->dev = (void *)hdev;

	return urb;
}
EXPORT_SYMBOL_GPL(alloc_mtk_intr_urb);

static int btmtk_recv_isopkt(struct hci_dev *hdev, void *buffer, int count)
{
	struct btmtk_data *btmtk_data = hci_get_priv(hdev);
	struct sk_buff *skb;
	unsigned long flags;
	int err = 0;

	spin_lock_irqsave(&btmtk_data->isorxlock, flags);
	skb = btmtk_data->isopkt_skb;

	while (count) {
		int len;

		if (!skb) {
			skb = bt_skb_alloc(HCI_MAX_ISO_SIZE, GFP_ATOMIC);
			if (!skb) {
				err = -ENOMEM;
				break;
			}

			hci_skb_pkt_type(skb) = HCI_ISODATA_PKT;
			hci_skb_expect(skb) = HCI_ISO_HDR_SIZE;
		}

		len = min_t(uint, hci_skb_expect(skb), count);
		skb_put_data(skb, buffer, len);

		count -= len;
		buffer += len;
		hci_skb_expect(skb) -= len;

		if (skb->len == HCI_ISO_HDR_SIZE) {
			__le16 dlen = ((struct hci_iso_hdr *)skb->data)->dlen;

			/* Complete ISO header */
			hci_skb_expect(skb) = __le16_to_cpu(dlen);

			if (skb_tailroom(skb) < hci_skb_expect(skb)) {
				kfree_skb(skb);
				skb = NULL;

				err = -EILSEQ;
				break;
			}
		}

		if (!hci_skb_expect(skb)) {
			/* Complete frame */
			hci_recv_frame(hdev, skb);
			skb = NULL;
		}
	}

	btmtk_data->isopkt_skb = skb;
	spin_unlock_irqrestore(&btmtk_data->isorxlock, flags);

	return err;
}

static void btmtk_intr_complete(struct urb *urb)
{
	struct hci_dev *hdev = urb->context;
	struct btmtk_data *btmtk_data = hci_get_priv(hdev);
	int err;

	BT_DBG("%s urb %p status %d count %d", hdev->name, urb, urb->status,
	       urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return;

	if (hdev->suspended)
		return;

	if (urb->status == 0) {
		hdev->stat.byte_rx += urb->actual_length;

		if (btmtk_recv_isopkt(hdev, urb->transfer_buffer,
				      urb->actual_length) < 0) {
			bt_dev_err(hdev, "corrupted iso packet");
			hdev->stat.err_rx++;
		}
	} else if (urb->status == -ENOENT) {
		/* Avoid suspend failed when usb_kill_urb */
		return;
	}

	usb_mark_last_busy(btmtk_data->udev);
	usb_anchor_urb(urb, &btmtk_data->isopkt_anchor);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		/* -EPERM: urb is being killed;
		 * -ENODEV: device got disconnected
		 */
		if (err != -EPERM && err != -ENODEV)
			bt_dev_err(hdev, "urb %p failed to resubmit (%d)",
				   urb, -err);
		if (err != -EPERM)
			hci_cmd_sync_cancel(hdev, -err);
		usb_unanchor_urb(urb);
	}
}

static int btmtk_submit_intr_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
	struct btmtk_data *btmtk_data = hci_get_priv(hdev);
	unsigned char *buf;
	unsigned int pipe;
	struct urb *urb;
	int err, size;

	BT_DBG("%s", hdev->name);

	if (!btmtk_data->isopkt_rx_ep)
		return -ENODEV;

	urb = usb_alloc_urb(0, mem_flags);
	if (!urb)
		return -ENOMEM;
	size = le16_to_cpu(btmtk_data->isopkt_rx_ep->wMaxPacketSize);

	buf = kmalloc(size, mem_flags);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvintpipe(btmtk_data->udev,
			      btmtk_data->isopkt_rx_ep->bEndpointAddress);

	usb_fill_int_urb(urb, btmtk_data->udev, pipe, buf, size,
			 btmtk_intr_complete, hdev,
			 btmtk_data->isopkt_rx_ep->bInterval);

	urb->transfer_flags |= URB_FREE_BUFFER;

	usb_mark_last_busy(btmtk_data->udev);
	usb_anchor_urb(urb, &btmtk_data->isopkt_anchor);

	err = usb_submit_urb(urb, mem_flags);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			bt_dev_err(hdev, "urb %p submission failed (%d)",
				   urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static int btmtk_usb_isointf_init(struct hci_dev *hdev)
{
	struct btmtk_data *btmtk_data = hci_get_priv(hdev);
	u8 iso_param[2] = { 0x08, 0x01 };
	struct sk_buff *skb;
	int err;

	init_usb_anchor(&btmtk_data->isopkt_anchor);
	spin_lock_init(&btmtk_data->isorxlock);

	__set_mtk_intr_interface(hdev);

	err = btmtk_submit_intr_urb(hdev, GFP_KERNEL);
	if (err < 0) {
		usb_kill_anchored_urbs(&btmtk_data->isopkt_anchor);
		bt_dev_err(hdev, "ISO intf not support (%d)", err);
		return err;
	}

	skb = __hci_cmd_sync(hdev, 0xfd98, sizeof(iso_param), iso_param,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Failed to apply iso setting (%ld)", PTR_ERR(skb));
		return PTR_ERR(skb);
	}
	kfree_skb(skb);

	return 0;
}

int btmtk_usb_resume(struct hci_dev *hdev)
{
	/* This function describes the specific additional steps taken by MediaTek
	 * when Bluetooth usb driver's resume function is called.
	 */
	struct btmtk_data *btmtk_data = hci_get_priv(hdev);

	/* Resubmit urb for iso data transmission */
	if (test_bit(BTMTK_ISOPKT_RUNNING, &btmtk_data->flags)) {
		if (btmtk_submit_intr_urb(hdev, GFP_NOIO) < 0)
			clear_bit(BTMTK_ISOPKT_RUNNING, &btmtk_data->flags);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(btmtk_usb_resume);

int btmtk_usb_suspend(struct hci_dev *hdev)
{
	/* This function describes the specific additional steps taken by MediaTek
	 * when Bluetooth usb driver's suspend function is called.
	 */
	struct btmtk_data *btmtk_data = hci_get_priv(hdev);

	/* Stop urb anchor for iso data transmission */
	if (test_bit(BTMTK_ISOPKT_RUNNING, &btmtk_data->flags))
		usb_kill_anchored_urbs(&btmtk_data->isopkt_anchor);

	return 0;
}
EXPORT_SYMBOL_GPL(btmtk_usb_suspend);

int btmtk_usb_setup(struct hci_dev *hdev)
{
	struct btmtk_data *btmtk_data = hci_get_priv(hdev);
	struct btmtk_hci_wmt_params wmt_params;
	ktime_t calltime, delta, rettime;
	struct btmtk_tci_sleep tci_sleep;
	unsigned long long duration;
	struct sk_buff *skb;
	const char *fwname;
	int err, status;
	u32 dev_id = 0;
	char fw_bin_name[64];
	u32 fw_version = 0, fw_flavor = 0;
	u8 param;

	calltime = ktime_get();

	err = btmtk_usb_id_get(hdev, 0x80000008, &dev_id);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to get device id (%d)", err);
		return err;
	}

	if (!dev_id || dev_id != 0x7663) {
		err = btmtk_usb_id_get(hdev, 0x70010200, &dev_id);
		if (err < 0) {
			bt_dev_err(hdev, "Failed to get device id (%d)", err);
			return err;
		}
		err = btmtk_usb_id_get(hdev, 0x80021004, &fw_version);
		if (err < 0) {
			bt_dev_err(hdev, "Failed to get fw version (%d)", err);
			return err;
		}
		err = btmtk_usb_id_get(hdev, 0x70010020, &fw_flavor);
		if (err < 0) {
			bt_dev_err(hdev, "Failed to get fw flavor (%d)", err);
			return err;
		}
		fw_flavor = (fw_flavor & 0x00000080) >> 7;
	}

	btmtk_data->dev_id = dev_id;

	err = btmtk_register_coredump(hdev, btmtk_data->drv_name, fw_version);
	if (err < 0)
		bt_dev_err(hdev, "Failed to register coredump (%d)", err);

	switch (dev_id) {
	case 0x7663:
		fwname = FIRMWARE_MT7663;
		break;
	case 0x7668:
		fwname = FIRMWARE_MT7668;
		break;
	case 0x7922:
	case 0x7961:
	case 0x7925:
		/* Reset the device to ensure it's in the initial state before
		 * downloading the firmware to ensure.
		 */

		if (!test_bit(BTMTK_FIRMWARE_LOADED, &btmtk_data->flags))
			btmtk_usb_subsys_reset(hdev, dev_id);

		btmtk_fw_get_filename(fw_bin_name, sizeof(fw_bin_name), dev_id,
				      fw_version, fw_flavor);

		err = btmtk_setup_firmware_79xx(hdev, fw_bin_name,
						btmtk_usb_hci_wmt_sync);
		if (err < 0) {
			bt_dev_err(hdev, "Failed to set up firmware (%d)", err);
			clear_bit(BTMTK_FIRMWARE_LOADED, &btmtk_data->flags);
			return err;
		}

		set_bit(BTMTK_FIRMWARE_LOADED, &btmtk_data->flags);

		/* It's Device EndPoint Reset Option Register */
		err = btmtk_usb_uhw_reg_write(hdev, MTK_EP_RST_OPT,
					      MTK_EP_RST_IN_OUT_OPT);
		if (err < 0)
			return err;

		/* Enable Bluetooth protocol */
		param = 1;
		wmt_params.op = BTMTK_WMT_FUNC_CTRL;
		wmt_params.flag = 0;
		wmt_params.dlen = sizeof(param);
		wmt_params.data = &param;
		wmt_params.status = NULL;

		err = btmtk_usb_hci_wmt_sync(hdev, &wmt_params);
		if (err < 0) {
			bt_dev_err(hdev, "Failed to send wmt func ctrl (%d)", err);
			return err;
		}

		hci_set_msft_opcode(hdev, 0xFD30);
		hci_set_aosp_capable(hdev);

		/* Set up ISO interface after protocol enabled */
		if (test_bit(BTMTK_ISOPKT_OVER_INTR, &btmtk_data->flags)) {
			if (!btmtk_usb_isointf_init(hdev))
				set_bit(BTMTK_ISOPKT_RUNNING, &btmtk_data->flags);
		}

		goto done;
	default:
		bt_dev_err(hdev, "Unsupported hardware variant (%08x)",
			   dev_id);
		return -ENODEV;
	}

	/* Query whether the firmware is already download */
	wmt_params.op = BTMTK_WMT_SEMAPHORE;
	wmt_params.flag = 1;
	wmt_params.dlen = 0;
	wmt_params.data = NULL;
	wmt_params.status = &status;

	err = btmtk_usb_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to query firmware status (%d)", err);
		return err;
	}

	if (status == BTMTK_WMT_PATCH_DONE) {
		bt_dev_info(hdev, "firmware already downloaded");
		goto ignore_setup_fw;
	}

	/* Setup a firmware which the device definitely requires */
	err = btmtk_setup_firmware(hdev, fwname,
				   btmtk_usb_hci_wmt_sync);
	if (err < 0)
		return err;

ignore_setup_fw:
	err = readx_poll_timeout(btmtk_usb_func_query, hdev, status,
				 status < 0 || status != BTMTK_WMT_ON_PROGRESS,
				 2000, 5000000);
	/* -ETIMEDOUT happens */
	if (err < 0)
		return err;

	/* The other errors happen in btmtk_usb_func_query */
	if (status < 0)
		return status;

	if (status == BTMTK_WMT_ON_DONE) {
		bt_dev_info(hdev, "function already on");
		goto ignore_func_on;
	}

	/* Enable Bluetooth protocol */
	param = 1;
	wmt_params.op = BTMTK_WMT_FUNC_CTRL;
	wmt_params.flag = 0;
	wmt_params.dlen = sizeof(param);
	wmt_params.data = &param;
	wmt_params.status = NULL;

	err = btmtk_usb_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to send wmt func ctrl (%d)", err);
		return err;
	}

ignore_func_on:
	/* Apply the low power environment setup */
	tci_sleep.mode = 0x5;
	tci_sleep.duration = cpu_to_le16(0x640);
	tci_sleep.host_duration = cpu_to_le16(0x640);
	tci_sleep.host_wakeup_pin = 0;
	tci_sleep.time_compensation = 0;

	skb = __hci_cmd_sync(hdev, 0xfc7a, sizeof(tci_sleep), &tci_sleep,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "Failed to apply low power setting (%d)", err);
		return err;
	}
	kfree_skb(skb);

done:
	rettime = ktime_get();
	delta = ktime_sub(rettime, calltime);
	duration = (unsigned long long)ktime_to_ns(delta) >> 10;

	bt_dev_info(hdev, "Device setup in %llu usecs", duration);

	return 0;
}
EXPORT_SYMBOL_GPL(btmtk_usb_setup);

int btmtk_usb_shutdown(struct hci_dev *hdev)
{
	struct btmtk_hci_wmt_params wmt_params;
	u8 param = 0;
	int err;

	/* Disable the device */
	wmt_params.op = BTMTK_WMT_FUNC_CTRL;
	wmt_params.flag = 0;
	wmt_params.dlen = sizeof(param);
	wmt_params.data = &param;
	wmt_params.status = NULL;

	err = btmtk_usb_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to send wmt func ctrl (%d)", err);
		return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(btmtk_usb_shutdown);
#endif

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
