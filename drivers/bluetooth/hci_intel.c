/*
 *
 *  Bluetooth HCI UART driver for Intel devices
 *
 *  Copyright (C) 2015  Intel Corporation
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/firmware.h>
#include <linux/wait.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "hci_uart.h"
#include "btintel.h"

#define STATE_BOOTLOADER	0
#define STATE_DOWNLOADING	1
#define STATE_FIRMWARE_LOADED	2
#define STATE_FIRMWARE_FAILED	3
#define STATE_BOOTING		4

struct intel_data {
	struct sk_buff *rx_skb;
	struct sk_buff_head txq;
	unsigned long flags;
};

static int intel_open(struct hci_uart *hu)
{
	struct intel_data *intel;

	BT_DBG("hu %p", hu);

	intel = kzalloc(sizeof(*intel), GFP_KERNEL);
	if (!intel)
		return -ENOMEM;

	skb_queue_head_init(&intel->txq);

	hu->priv = intel;
	return 0;
}

static int intel_close(struct hci_uart *hu)
{
	struct intel_data *intel = hu->priv;

	BT_DBG("hu %p", hu);

	skb_queue_purge(&intel->txq);
	kfree_skb(intel->rx_skb);
	kfree(intel);

	hu->priv = NULL;
	return 0;
}

static int intel_flush(struct hci_uart *hu)
{
	struct intel_data *intel = hu->priv;

	BT_DBG("hu %p", hu);

	skb_queue_purge(&intel->txq);

	return 0;
}

static int inject_cmd_complete(struct hci_dev *hdev, __u16 opcode)
{
	struct sk_buff *skb;
	struct hci_event_hdr *hdr;
	struct hci_ev_cmd_complete *evt;

	skb = bt_skb_alloc(sizeof(*hdr) + sizeof(*evt) + 1, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	hdr = (struct hci_event_hdr *)skb_put(skb, sizeof(*hdr));
	hdr->evt = HCI_EV_CMD_COMPLETE;
	hdr->plen = sizeof(*evt) + 1;

	evt = (struct hci_ev_cmd_complete *)skb_put(skb, sizeof(*evt));
	evt->ncmd = 0x01;
	evt->opcode = cpu_to_le16(opcode);

	*skb_put(skb, 1) = 0x00;

	bt_cb(skb)->pkt_type = HCI_EVENT_PKT;

	return hci_recv_frame(hdev, skb);
}

static int intel_setup(struct hci_uart *hu)
{
	static const u8 reset_param[] = { 0x00, 0x01, 0x00, 0x01,
					  0x00, 0x08, 0x04, 0x00 };
	struct intel_data *intel = hu->priv;
	struct hci_dev *hdev = hu->hdev;
	struct sk_buff *skb;
	struct intel_version *ver;
	struct intel_boot_params *params;
	const struct firmware *fw;
	const u8 *fw_ptr;
	char fwname[64];
	u32 frag_len;
	ktime_t calltime, delta, rettime;
	unsigned long long duration;
	int err;

	BT_DBG("%s", hdev->name);

	hu->hdev->set_bdaddr = btintel_set_bdaddr;

	calltime = ktime_get();

	set_bit(STATE_BOOTLOADER, &intel->flags);

	/* Read the Intel version information to determine if the device
	 * is in bootloader mode or if it already has operational firmware
	 * loaded.
	 */
	skb = __hci_cmd_sync(hdev, 0xfc05, 0, NULL, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		BT_ERR("%s: Reading Intel version information failed (%ld)",
		       hdev->name, PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	if (skb->len != sizeof(*ver)) {
		BT_ERR("%s: Intel version event size mismatch", hdev->name);
		kfree_skb(skb);
		return -EILSEQ;
	}

	ver = (struct intel_version *)skb->data;
	if (ver->status) {
		BT_ERR("%s: Intel version command failure (%02x)",
		       hdev->name, ver->status);
		err = -bt_to_errno(ver->status);
		kfree_skb(skb);
		return err;
	}

	/* The hardware platform number has a fixed value of 0x37 and
	 * for now only accept this single value.
	 */
	if (ver->hw_platform != 0x37) {
		BT_ERR("%s: Unsupported Intel hardware platform (%u)",
		       hdev->name, ver->hw_platform);
		kfree_skb(skb);
		return -EINVAL;
	}

	/* At the moment only the hardware variant iBT 3.0 (LnP/SfP) is
	 * supported by this firmware loading method. This check has been
	 * put in place to ensure correct forward compatibility options
	 * when newer hardware variants come along.
	 */
	if (ver->hw_variant != 0x0b) {
		BT_ERR("%s: Unsupported Intel hardware variant (%u)",
		       hdev->name, ver->hw_variant);
		kfree_skb(skb);
		return -EINVAL;
	}

	btintel_version_info(hdev, ver);

	/* The firmware variant determines if the device is in bootloader
	 * mode or is running operational firmware. The value 0x06 identifies
	 * the bootloader and the value 0x23 identifies the operational
	 * firmware.
	 *
	 * When the operational firmware is already present, then only
	 * the check for valid Bluetooth device address is needed. This
	 * determines if the device will be added as configured or
	 * unconfigured controller.
	 *
	 * It is not possible to use the Secure Boot Parameters in this
	 * case since that command is only available in bootloader mode.
	 */
	if (ver->fw_variant == 0x23) {
		kfree_skb(skb);
		clear_bit(STATE_BOOTLOADER, &intel->flags);
		btintel_check_bdaddr(hdev);
		return 0;
	}

	/* If the device is not in bootloader mode, then the only possible
	 * choice is to return an error and abort the device initialization.
	 */
	if (ver->fw_variant != 0x06) {
		BT_ERR("%s: Unsupported Intel firmware variant (%u)",
		       hdev->name, ver->fw_variant);
		kfree_skb(skb);
		return -ENODEV;
	}

	kfree_skb(skb);

	/* Read the secure boot parameters to identify the operating
	 * details of the bootloader.
	 */
	skb = __hci_cmd_sync(hdev, 0xfc0d, 0, NULL, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		BT_ERR("%s: Reading Intel boot parameters failed (%ld)",
		       hdev->name, PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	if (skb->len != sizeof(*params)) {
		BT_ERR("%s: Intel boot parameters size mismatch", hdev->name);
		kfree_skb(skb);
		return -EILSEQ;
	}

	params = (struct intel_boot_params *)skb->data;
	if (params->status) {
		BT_ERR("%s: Intel boot parameters command failure (%02x)",
		       hdev->name, params->status);
		err = -bt_to_errno(params->status);
		kfree_skb(skb);
		return err;
	}

	BT_INFO("%s: Device revision is %u", hdev->name,
		le16_to_cpu(params->dev_revid));

	BT_INFO("%s: Secure boot is %s", hdev->name,
		params->secure_boot ? "enabled" : "disabled");

	BT_INFO("%s: Minimum firmware build %u week %u %u", hdev->name,
		params->min_fw_build_nn, params->min_fw_build_cw,
		2000 + params->min_fw_build_yy);

	/* It is required that every single firmware fragment is acknowledged
	 * with a command complete event. If the boot parameters indicate
	 * that this bootloader does not send them, then abort the setup.
	 */
	if (params->limited_cce != 0x00) {
		BT_ERR("%s: Unsupported Intel firmware loading method (%u)",
		       hdev->name, params->limited_cce);
		kfree_skb(skb);
		return -EINVAL;
	}

	/* If the OTP has no valid Bluetooth device address, then there will
	 * also be no valid address for the operational firmware.
	 */
	if (!bacmp(&params->otp_bdaddr, BDADDR_ANY)) {
		BT_INFO("%s: No device address configured", hdev->name);
		set_bit(HCI_QUIRK_INVALID_BDADDR, &hdev->quirks);
	}

	/* With this Intel bootloader only the hardware variant and device
	 * revision information are used to select the right firmware.
	 *
	 * Currently this bootloader support is limited to hardware variant
	 * iBT 3.0 (LnP/SfP) which is identified by the value 11 (0x0b).
	 */
	snprintf(fwname, sizeof(fwname), "intel/ibt-11-%u.sfi",
		 le16_to_cpu(params->dev_revid));

	err = request_firmware(&fw, fwname, &hdev->dev);
	if (err < 0) {
		BT_ERR("%s: Failed to load Intel firmware file (%d)",
		       hdev->name, err);
		kfree_skb(skb);
		return err;
	}

	BT_INFO("%s: Found device firmware: %s", hdev->name, fwname);

	kfree_skb(skb);

	if (fw->size < 644) {
		BT_ERR("%s: Invalid size of firmware file (%zu)",
		       hdev->name, fw->size);
		err = -EBADF;
		goto done;
	}

	set_bit(STATE_DOWNLOADING, &intel->flags);

	/* Start the firmware download transaction with the Init fragment
	 * represented by the 128 bytes of CSS header.
	 */
	err = btintel_secure_send(hdev, 0x00, 128, fw->data);
	if (err < 0) {
		BT_ERR("%s: Failed to send firmware header (%d)",
		       hdev->name, err);
		goto done;
	}

	/* Send the 256 bytes of public key information from the firmware
	 * as the PKey fragment.
	 */
	err = btintel_secure_send(hdev, 0x03, 256, fw->data + 128);
	if (err < 0) {
		BT_ERR("%s: Failed to send firmware public key (%d)",
		       hdev->name, err);
		goto done;
	}

	/* Send the 256 bytes of signature information from the firmware
	 * as the Sign fragment.
	 */
	err = btintel_secure_send(hdev, 0x02, 256, fw->data + 388);
	if (err < 0) {
		BT_ERR("%s: Failed to send firmware signature (%d)",
		       hdev->name, err);
		goto done;
	}

	fw_ptr = fw->data + 644;
	frag_len = 0;

	while (fw_ptr - fw->data < fw->size) {
		struct hci_command_hdr *cmd = (void *)(fw_ptr + frag_len);

		frag_len += sizeof(*cmd) + cmd->plen;

		BT_DBG("%s: patching %td/%zu", hdev->name,
		       (fw_ptr - fw->data), fw->size);

		/* The parameter length of the secure send command requires
		 * a 4 byte alignment. It happens so that the firmware file
		 * contains proper Intel_NOP commands to align the fragments
		 * as needed.
		 *
		 * Send set of commands with 4 byte alignment from the
		 * firmware data buffer as a single Data fragement.
		 */
		if (frag_len % 4)
			continue;

		/* Send each command from the firmware data buffer as
		 * a single Data fragment.
		 */
		err = btintel_secure_send(hdev, 0x01, frag_len, fw_ptr);
		if (err < 0) {
			BT_ERR("%s: Failed to send firmware data (%d)",
			       hdev->name, err);
			goto done;
		}

		fw_ptr += frag_len;
		frag_len = 0;
	}

	set_bit(STATE_FIRMWARE_LOADED, &intel->flags);

	BT_INFO("%s: Waiting for firmware download to complete", hdev->name);

	/* Before switching the device into operational mode and with that
	 * booting the loaded firmware, wait for the bootloader notification
	 * that all fragments have been successfully received.
	 *
	 * When the event processing receives the notification, then the
	 * STATE_DOWNLOADING flag will be cleared.
	 *
	 * The firmware loading should not take longer than 5 seconds
	 * and thus just timeout if that happens and fail the setup
	 * of this device.
	 */
	err = wait_on_bit_timeout(&intel->flags, STATE_DOWNLOADING,
				  TASK_INTERRUPTIBLE,
				  msecs_to_jiffies(5000));
	if (err == 1) {
		BT_ERR("%s: Firmware loading interrupted", hdev->name);
		err = -EINTR;
		goto done;
	}

	if (err) {
		BT_ERR("%s: Firmware loading timeout", hdev->name);
		err = -ETIMEDOUT;
		goto done;
	}

	if (test_bit(STATE_FIRMWARE_FAILED, &intel->flags)) {
		BT_ERR("%s: Firmware loading failed", hdev->name);
		err = -ENOEXEC;
		goto done;
	}

	rettime = ktime_get();
	delta = ktime_sub(rettime, calltime);
	duration = (unsigned long long) ktime_to_ns(delta) >> 10;

	BT_INFO("%s: Firmware loaded in %llu usecs", hdev->name, duration);

done:
	release_firmware(fw);

	if (err < 0)
		return err;

	calltime = ktime_get();

	set_bit(STATE_BOOTING, &intel->flags);

	skb = __hci_cmd_sync(hdev, 0xfc01, sizeof(reset_param), reset_param,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	kfree_skb(skb);

	/* The bootloader will not indicate when the device is ready. This
	 * is done by the operational firmware sending bootup notification.
	 *
	 * Booting into operational firmware should not take longer than
	 * 1 second. However if that happens, then just fail the setup
	 * since something went wrong.
	 */
	BT_INFO("%s: Waiting for device to boot", hdev->name);

	err = wait_on_bit_timeout(&intel->flags, STATE_BOOTING,
				  TASK_INTERRUPTIBLE,
				  msecs_to_jiffies(1000));

	if (err == 1) {
		BT_ERR("%s: Device boot interrupted", hdev->name);
		return -EINTR;
	}

	if (err) {
		BT_ERR("%s: Device boot timeout", hdev->name);
		return -ETIMEDOUT;
	}

	rettime = ktime_get();
	delta = ktime_sub(rettime, calltime);
	duration = (unsigned long long) ktime_to_ns(delta) >> 10;

	BT_INFO("%s: Device booted in %llu usecs", hdev->name, duration);

	clear_bit(STATE_BOOTLOADER, &intel->flags);

	return 0;
}

static int intel_recv_event(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_uart *hu = hci_get_drvdata(hdev);
	struct intel_data *intel = hu->priv;
	struct hci_event_hdr *hdr;

	if (!test_bit(STATE_BOOTLOADER, &intel->flags))
		goto recv;

	hdr = (void *)skb->data;

	/* When the firmware loading completes the device sends
	 * out a vendor specific event indicating the result of
	 * the firmware loading.
	 */
	if (skb->len == 7 && hdr->evt == 0xff && hdr->plen == 0x05 &&
	    skb->data[2] == 0x06) {
		if (skb->data[3] != 0x00)
			set_bit(STATE_FIRMWARE_FAILED, &intel->flags);

		if (test_and_clear_bit(STATE_DOWNLOADING, &intel->flags) &&
		    test_bit(STATE_FIRMWARE_LOADED, &intel->flags)) {
			smp_mb__after_atomic();
			wake_up_bit(&intel->flags, STATE_DOWNLOADING);
		}

	/* When switching to the operational firmware the device
	 * sends a vendor specific event indicating that the bootup
	 * completed.
	 */
	} else if (skb->len == 9 && hdr->evt == 0xff && hdr->plen == 0x07 &&
		   skb->data[2] == 0x02) {
		if (test_and_clear_bit(STATE_BOOTING, &intel->flags)) {
			smp_mb__after_atomic();
			wake_up_bit(&intel->flags, STATE_BOOTING);
		}
	}
recv:
	return hci_recv_frame(hdev, skb);
}

static const struct h4_recv_pkt intel_recv_pkts[] = {
	{ H4_RECV_ACL,   .recv = hci_recv_frame },
	{ H4_RECV_SCO,   .recv = hci_recv_frame },
	{ H4_RECV_EVENT, .recv = intel_recv_event },
};

static int intel_recv(struct hci_uart *hu, const void *data, int count)
{
	struct intel_data *intel = hu->priv;

	if (!test_bit(HCI_UART_REGISTERED, &hu->flags))
		return -EUNATCH;

	intel->rx_skb = h4_recv_buf(hu->hdev, intel->rx_skb, data, count,
				    intel_recv_pkts,
				    ARRAY_SIZE(intel_recv_pkts));
	if (IS_ERR(intel->rx_skb)) {
		int err = PTR_ERR(intel->rx_skb);
		BT_ERR("%s: Frame reassembly failed (%d)", hu->hdev->name, err);
		intel->rx_skb = NULL;
		return err;
	}

	return count;
}

static int intel_enqueue(struct hci_uart *hu, struct sk_buff *skb)
{
	struct intel_data *intel = hu->priv;

	BT_DBG("hu %p skb %p", hu, skb);

	skb_queue_tail(&intel->txq, skb);

	return 0;
}

static struct sk_buff *intel_dequeue(struct hci_uart *hu)
{
	struct intel_data *intel = hu->priv;
	struct sk_buff *skb;

	skb = skb_dequeue(&intel->txq);
	if (!skb)
		return skb;

	if (test_bit(STATE_BOOTLOADER, &intel->flags) &&
	    (bt_cb(skb)->pkt_type == HCI_COMMAND_PKT)) {
		struct hci_command_hdr *cmd = (void *)skb->data;
		__u16 opcode = le16_to_cpu(cmd->opcode);

		/* When the 0xfc01 command is issued to boot into
		 * the operational firmware, it will actually not
		 * send a command complete event. To keep the flow
		 * control working inject that event here.
		 */
		if (opcode == 0xfc01)
			inject_cmd_complete(hu->hdev, opcode);
	}

	/* Prepend skb with frame type */
	memcpy(skb_push(skb, 1), &bt_cb(skb)->pkt_type, 1);

	return skb;
}

static const struct hci_uart_proto intel_proto = {
	.id		= HCI_UART_INTEL,
	.name		= "Intel",
	.init_speed	= 115200,
	.open		= intel_open,
	.close		= intel_close,
	.flush		= intel_flush,
	.setup		= intel_setup,
	.recv		= intel_recv,
	.enqueue	= intel_enqueue,
	.dequeue	= intel_dequeue,
};

int __init intel_init(void)
{
	return hci_uart_register_proto(&intel_proto);
}

int __exit intel_deinit(void)
{
	return hci_uart_unregister_proto(&intel_proto);
}
