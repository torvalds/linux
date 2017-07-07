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
#include <linux/module.h>
#include <linux/wait.h>
#include <linux/tty.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/acpi.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "hci_uart.h"
#include "btintel.h"

#define STATE_BOOTLOADER	0
#define STATE_DOWNLOADING	1
#define STATE_FIRMWARE_LOADED	2
#define STATE_FIRMWARE_FAILED	3
#define STATE_BOOTING		4
#define STATE_LPM_ENABLED	5
#define STATE_TX_ACTIVE		6
#define STATE_SUSPENDED		7
#define STATE_LPM_TRANSACTION	8

#define HCI_LPM_WAKE_PKT 0xf0
#define HCI_LPM_PKT 0xf1
#define HCI_LPM_MAX_SIZE 10
#define HCI_LPM_HDR_SIZE HCI_EVENT_HDR_SIZE

#define LPM_OP_TX_NOTIFY 0x00
#define LPM_OP_SUSPEND_ACK 0x02
#define LPM_OP_RESUME_ACK 0x03

#define LPM_SUSPEND_DELAY_MS 1000

struct hci_lpm_pkt {
	__u8 opcode;
	__u8 dlen;
	__u8 data[0];
} __packed;

struct intel_device {
	struct list_head list;
	struct platform_device *pdev;
	struct gpio_desc *reset;
	struct hci_uart *hu;
	struct mutex hu_lock;
	int irq;
};

static LIST_HEAD(intel_device_list);
static DEFINE_MUTEX(intel_device_list_lock);

struct intel_data {
	struct sk_buff *rx_skb;
	struct sk_buff_head txq;
	struct work_struct busy_work;
	struct hci_uart *hu;
	unsigned long flags;
};

static u8 intel_convert_speed(unsigned int speed)
{
	switch (speed) {
	case 9600:
		return 0x00;
	case 19200:
		return 0x01;
	case 38400:
		return 0x02;
	case 57600:
		return 0x03;
	case 115200:
		return 0x04;
	case 230400:
		return 0x05;
	case 460800:
		return 0x06;
	case 921600:
		return 0x07;
	case 1843200:
		return 0x08;
	case 3250000:
		return 0x09;
	case 2000000:
		return 0x0a;
	case 3000000:
		return 0x0b;
	default:
		return 0xff;
	}
}

static int intel_wait_booting(struct hci_uart *hu)
{
	struct intel_data *intel = hu->priv;
	int err;

	err = wait_on_bit_timeout(&intel->flags, STATE_BOOTING,
				  TASK_INTERRUPTIBLE,
				  msecs_to_jiffies(1000));

	if (err == -EINTR) {
		bt_dev_err(hu->hdev, "Device boot interrupted");
		return -EINTR;
	}

	if (err) {
		bt_dev_err(hu->hdev, "Device boot timeout");
		return -ETIMEDOUT;
	}

	return err;
}

#ifdef CONFIG_PM
static int intel_wait_lpm_transaction(struct hci_uart *hu)
{
	struct intel_data *intel = hu->priv;
	int err;

	err = wait_on_bit_timeout(&intel->flags, STATE_LPM_TRANSACTION,
				  TASK_INTERRUPTIBLE,
				  msecs_to_jiffies(1000));

	if (err == -EINTR) {
		bt_dev_err(hu->hdev, "LPM transaction interrupted");
		return -EINTR;
	}

	if (err) {
		bt_dev_err(hu->hdev, "LPM transaction timeout");
		return -ETIMEDOUT;
	}

	return err;
}

static int intel_lpm_suspend(struct hci_uart *hu)
{
	static const u8 suspend[] = { 0x01, 0x01, 0x01 };
	struct intel_data *intel = hu->priv;
	struct sk_buff *skb;

	if (!test_bit(STATE_LPM_ENABLED, &intel->flags) ||
	    test_bit(STATE_SUSPENDED, &intel->flags))
		return 0;

	if (test_bit(STATE_TX_ACTIVE, &intel->flags))
		return -EAGAIN;

	bt_dev_dbg(hu->hdev, "Suspending");

	skb = bt_skb_alloc(sizeof(suspend), GFP_KERNEL);
	if (!skb) {
		bt_dev_err(hu->hdev, "Failed to alloc memory for LPM packet");
		return -ENOMEM;
	}

	skb_put_data(skb, suspend, sizeof(suspend));
	hci_skb_pkt_type(skb) = HCI_LPM_PKT;

	set_bit(STATE_LPM_TRANSACTION, &intel->flags);

	/* LPM flow is a priority, enqueue packet at list head */
	skb_queue_head(&intel->txq, skb);
	hci_uart_tx_wakeup(hu);

	intel_wait_lpm_transaction(hu);
	/* Even in case of failure, continue and test the suspended flag */

	clear_bit(STATE_LPM_TRANSACTION, &intel->flags);

	if (!test_bit(STATE_SUSPENDED, &intel->flags)) {
		bt_dev_err(hu->hdev, "Device suspend error");
		return -EINVAL;
	}

	bt_dev_dbg(hu->hdev, "Suspended");

	hci_uart_set_flow_control(hu, true);

	return 0;
}

static int intel_lpm_resume(struct hci_uart *hu)
{
	struct intel_data *intel = hu->priv;
	struct sk_buff *skb;

	if (!test_bit(STATE_LPM_ENABLED, &intel->flags) ||
	    !test_bit(STATE_SUSPENDED, &intel->flags))
		return 0;

	bt_dev_dbg(hu->hdev, "Resuming");

	hci_uart_set_flow_control(hu, false);

	skb = bt_skb_alloc(0, GFP_KERNEL);
	if (!skb) {
		bt_dev_err(hu->hdev, "Failed to alloc memory for LPM packet");
		return -ENOMEM;
	}

	hci_skb_pkt_type(skb) = HCI_LPM_WAKE_PKT;

	set_bit(STATE_LPM_TRANSACTION, &intel->flags);

	/* LPM flow is a priority, enqueue packet at list head */
	skb_queue_head(&intel->txq, skb);
	hci_uart_tx_wakeup(hu);

	intel_wait_lpm_transaction(hu);
	/* Even in case of failure, continue and test the suspended flag */

	clear_bit(STATE_LPM_TRANSACTION, &intel->flags);

	if (test_bit(STATE_SUSPENDED, &intel->flags)) {
		bt_dev_err(hu->hdev, "Device resume error");
		return -EINVAL;
	}

	bt_dev_dbg(hu->hdev, "Resumed");

	return 0;
}
#endif /* CONFIG_PM */

static int intel_lpm_host_wake(struct hci_uart *hu)
{
	static const u8 lpm_resume_ack[] = { LPM_OP_RESUME_ACK, 0x00 };
	struct intel_data *intel = hu->priv;
	struct sk_buff *skb;

	hci_uart_set_flow_control(hu, false);

	clear_bit(STATE_SUSPENDED, &intel->flags);

	skb = bt_skb_alloc(sizeof(lpm_resume_ack), GFP_KERNEL);
	if (!skb) {
		bt_dev_err(hu->hdev, "Failed to alloc memory for LPM packet");
		return -ENOMEM;
	}

	skb_put_data(skb, lpm_resume_ack, sizeof(lpm_resume_ack));
	hci_skb_pkt_type(skb) = HCI_LPM_PKT;

	/* LPM flow is a priority, enqueue packet at list head */
	skb_queue_head(&intel->txq, skb);
	hci_uart_tx_wakeup(hu);

	bt_dev_dbg(hu->hdev, "Resumed by controller");

	return 0;
}

static irqreturn_t intel_irq(int irq, void *dev_id)
{
	struct intel_device *idev = dev_id;

	dev_info(&idev->pdev->dev, "hci_intel irq\n");

	mutex_lock(&idev->hu_lock);
	if (idev->hu)
		intel_lpm_host_wake(idev->hu);
	mutex_unlock(&idev->hu_lock);

	/* Host/Controller are now LPM resumed, trigger a new delayed suspend */
	pm_runtime_get(&idev->pdev->dev);
	pm_runtime_mark_last_busy(&idev->pdev->dev);
	pm_runtime_put_autosuspend(&idev->pdev->dev);

	return IRQ_HANDLED;
}

static int intel_set_power(struct hci_uart *hu, bool powered)
{
	struct list_head *p;
	int err = -ENODEV;

	if (!hu->tty->dev)
		return err;

	mutex_lock(&intel_device_list_lock);

	list_for_each(p, &intel_device_list) {
		struct intel_device *idev = list_entry(p, struct intel_device,
						       list);

		/* tty device and pdev device should share the same parent
		 * which is the UART port.
		 */
		if (hu->tty->dev->parent != idev->pdev->dev.parent)
			continue;

		if (!idev->reset) {
			err = -ENOTSUPP;
			break;
		}

		BT_INFO("hu %p, Switching compatible pm device (%s) to %u",
			hu, dev_name(&idev->pdev->dev), powered);

		gpiod_set_value(idev->reset, powered);

		/* Provide to idev a hu reference which is used to run LPM
		 * transactions (lpm suspend/resume) from PM callbacks.
		 * hu needs to be protected against concurrent removing during
		 * these PM ops.
		 */
		mutex_lock(&idev->hu_lock);
		idev->hu = powered ? hu : NULL;
		mutex_unlock(&idev->hu_lock);

		if (idev->irq < 0)
			break;

		if (powered && device_can_wakeup(&idev->pdev->dev)) {
			err = devm_request_threaded_irq(&idev->pdev->dev,
							idev->irq, NULL,
							intel_irq,
							IRQF_ONESHOT,
							"bt-host-wake", idev);
			if (err) {
				BT_ERR("hu %p, unable to allocate irq-%d",
				       hu, idev->irq);
				break;
			}

			device_wakeup_enable(&idev->pdev->dev);

			pm_runtime_set_active(&idev->pdev->dev);
			pm_runtime_use_autosuspend(&idev->pdev->dev);
			pm_runtime_set_autosuspend_delay(&idev->pdev->dev,
							 LPM_SUSPEND_DELAY_MS);
			pm_runtime_enable(&idev->pdev->dev);
		} else if (!powered && device_may_wakeup(&idev->pdev->dev)) {
			devm_free_irq(&idev->pdev->dev, idev->irq, idev);
			device_wakeup_disable(&idev->pdev->dev);

			pm_runtime_disable(&idev->pdev->dev);
		}
	}

	mutex_unlock(&intel_device_list_lock);

	return err;
}

static void intel_busy_work(struct work_struct *work)
{
	struct list_head *p;
	struct intel_data *intel = container_of(work, struct intel_data,
						busy_work);

	if (!intel->hu->tty->dev)
		return;

	/* Link is busy, delay the suspend */
	mutex_lock(&intel_device_list_lock);
	list_for_each(p, &intel_device_list) {
		struct intel_device *idev = list_entry(p, struct intel_device,
						       list);

		if (intel->hu->tty->dev->parent == idev->pdev->dev.parent) {
			pm_runtime_get(&idev->pdev->dev);
			pm_runtime_mark_last_busy(&idev->pdev->dev);
			pm_runtime_put_autosuspend(&idev->pdev->dev);
			break;
		}
	}
	mutex_unlock(&intel_device_list_lock);
}

static int intel_open(struct hci_uart *hu)
{
	struct intel_data *intel;

	BT_DBG("hu %p", hu);

	intel = kzalloc(sizeof(*intel), GFP_KERNEL);
	if (!intel)
		return -ENOMEM;

	skb_queue_head_init(&intel->txq);
	INIT_WORK(&intel->busy_work, intel_busy_work);

	intel->hu = hu;

	hu->priv = intel;

	if (!intel_set_power(hu, true))
		set_bit(STATE_BOOTING, &intel->flags);

	return 0;
}

static int intel_close(struct hci_uart *hu)
{
	struct intel_data *intel = hu->priv;

	BT_DBG("hu %p", hu);

	cancel_work_sync(&intel->busy_work);

	intel_set_power(hu, false);

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

	hdr = skb_put(skb, sizeof(*hdr));
	hdr->evt = HCI_EV_CMD_COMPLETE;
	hdr->plen = sizeof(*evt) + 1;

	evt = skb_put(skb, sizeof(*evt));
	evt->ncmd = 0x01;
	evt->opcode = cpu_to_le16(opcode);

	skb_put_u8(skb, 0x00);

	hci_skb_pkt_type(skb) = HCI_EVENT_PKT;

	return hci_recv_frame(hdev, skb);
}

static int intel_set_baudrate(struct hci_uart *hu, unsigned int speed)
{
	struct intel_data *intel = hu->priv;
	struct hci_dev *hdev = hu->hdev;
	u8 speed_cmd[] = { 0x06, 0xfc, 0x01, 0x00 };
	struct sk_buff *skb;
	int err;

	/* This can be the first command sent to the chip, check
	 * that the controller is ready.
	 */
	err = intel_wait_booting(hu);

	clear_bit(STATE_BOOTING, &intel->flags);

	/* In case of timeout, try to continue anyway */
	if (err && err != -ETIMEDOUT)
		return err;

	bt_dev_info(hdev, "Change controller speed to %d", speed);

	speed_cmd[3] = intel_convert_speed(speed);
	if (speed_cmd[3] == 0xff) {
		bt_dev_err(hdev, "Unsupported speed");
		return -EINVAL;
	}

	/* Device will not accept speed change if Intel version has not been
	 * previously requested.
	 */
	skb = __hci_cmd_sync(hdev, 0xfc05, 0, NULL, HCI_CMD_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Reading Intel version information failed (%ld)",
			   PTR_ERR(skb));
		return PTR_ERR(skb);
	}
	kfree_skb(skb);

	skb = bt_skb_alloc(sizeof(speed_cmd), GFP_KERNEL);
	if (!skb) {
		bt_dev_err(hdev, "Failed to alloc memory for baudrate packet");
		return -ENOMEM;
	}

	skb_put_data(skb, speed_cmd, sizeof(speed_cmd));
	hci_skb_pkt_type(skb) = HCI_COMMAND_PKT;

	hci_uart_set_flow_control(hu, true);

	skb_queue_tail(&intel->txq, skb);
	hci_uart_tx_wakeup(hu);

	/* wait 100ms to change baudrate on controller side */
	msleep(100);

	hci_uart_set_baudrate(hu, speed);
	hci_uart_set_flow_control(hu, false);

	return 0;
}

static int intel_setup(struct hci_uart *hu)
{
	static const u8 reset_param[] = { 0x00, 0x01, 0x00, 0x01,
					  0x00, 0x08, 0x04, 0x00 };
	struct intel_data *intel = hu->priv;
	struct hci_dev *hdev = hu->hdev;
	struct sk_buff *skb;
	struct intel_version ver;
	struct intel_boot_params *params;
	struct list_head *p;
	const struct firmware *fw;
	const u8 *fw_ptr;
	char fwname[64];
	u32 frag_len;
	ktime_t calltime, delta, rettime;
	unsigned long long duration;
	unsigned int init_speed, oper_speed;
	int speed_change = 0;
	int err;

	bt_dev_dbg(hdev, "start intel_setup");

	hu->hdev->set_diag = btintel_set_diag;
	hu->hdev->set_bdaddr = btintel_set_bdaddr;

	calltime = ktime_get();

	if (hu->init_speed)
		init_speed = hu->init_speed;
	else
		init_speed = hu->proto->init_speed;

	if (hu->oper_speed)
		oper_speed = hu->oper_speed;
	else
		oper_speed = hu->proto->oper_speed;

	if (oper_speed && init_speed && oper_speed != init_speed)
		speed_change = 1;

	/* Check that the controller is ready */
	err = intel_wait_booting(hu);

	clear_bit(STATE_BOOTING, &intel->flags);

	/* In case of timeout, try to continue anyway */
	if (err && err != -ETIMEDOUT)
		return err;

	set_bit(STATE_BOOTLOADER, &intel->flags);

	/* Read the Intel version information to determine if the device
	 * is in bootloader mode or if it already has operational firmware
	 * loaded.
	 */
	 err = btintel_read_version(hdev, &ver);
	 if (err)
		return err;

	/* The hardware platform number has a fixed value of 0x37 and
	 * for now only accept this single value.
	 */
	if (ver.hw_platform != 0x37) {
		bt_dev_err(hdev, "Unsupported Intel hardware platform (%u)",
			   ver.hw_platform);
		return -EINVAL;
	}

        /* Check for supported iBT hardware variants of this firmware
         * loading method.
         *
         * This check has been put in place to ensure correct forward
         * compatibility options when newer hardware variants come along.
         */
	switch (ver.hw_variant) {
	case 0x0b:	/* LnP */
	case 0x0c:	/* WsP */
	case 0x12:	/* ThP */
		break;
	default:
		bt_dev_err(hdev, "Unsupported Intel hardware variant (%u)",
			   ver.hw_variant);
		return -EINVAL;
	}

	btintel_version_info(hdev, &ver);

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
	if (ver.fw_variant == 0x23) {
		clear_bit(STATE_BOOTLOADER, &intel->flags);
		btintel_check_bdaddr(hdev);
		return 0;
	}

	/* If the device is not in bootloader mode, then the only possible
	 * choice is to return an error and abort the device initialization.
	 */
	if (ver.fw_variant != 0x06) {
		bt_dev_err(hdev, "Unsupported Intel firmware variant (%u)",
			   ver.fw_variant);
		return -ENODEV;
	}

	/* Read the secure boot parameters to identify the operating
	 * details of the bootloader.
	 */
	skb = __hci_cmd_sync(hdev, 0xfc0d, 0, NULL, HCI_CMD_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Reading Intel boot parameters failed (%ld)",
			   PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	if (skb->len != sizeof(*params)) {
		bt_dev_err(hdev, "Intel boot parameters size mismatch");
		kfree_skb(skb);
		return -EILSEQ;
	}

	params = (struct intel_boot_params *)skb->data;
	if (params->status) {
		bt_dev_err(hdev, "Intel boot parameters command failure (%02x)",
			   params->status);
		err = -bt_to_errno(params->status);
		kfree_skb(skb);
		return err;
	}

	bt_dev_info(hdev, "Device revision is %u",
		    le16_to_cpu(params->dev_revid));

	bt_dev_info(hdev, "Secure boot is %s",
		    params->secure_boot ? "enabled" : "disabled");

	bt_dev_info(hdev, "Minimum firmware build %u week %u %u",
		params->min_fw_build_nn, params->min_fw_build_cw,
		2000 + params->min_fw_build_yy);

	/* It is required that every single firmware fragment is acknowledged
	 * with a command complete event. If the boot parameters indicate
	 * that this bootloader does not send them, then abort the setup.
	 */
	if (params->limited_cce != 0x00) {
		bt_dev_err(hdev, "Unsupported Intel firmware loading method (%u)",
			   params->limited_cce);
		kfree_skb(skb);
		return -EINVAL;
	}

	/* If the OTP has no valid Bluetooth device address, then there will
	 * also be no valid address for the operational firmware.
	 */
	if (!bacmp(&params->otp_bdaddr, BDADDR_ANY)) {
		bt_dev_info(hdev, "No device address configured");
		set_bit(HCI_QUIRK_INVALID_BDADDR, &hdev->quirks);
	}

	/* With this Intel bootloader only the hardware variant and device
	 * revision information are used to select the right firmware.
	 *
	 * The firmware filename is ibt-<hw_variant>-<dev_revid>.sfi.
	 *
	 * Currently the supported hardware variants are:
	 *   11 (0x0b) for iBT 3.0 (LnP/SfP)
	 */
	snprintf(fwname, sizeof(fwname), "intel/ibt-%u-%u.sfi",
		le16_to_cpu(ver.hw_variant),
		le16_to_cpu(params->dev_revid));

	err = request_firmware(&fw, fwname, &hdev->dev);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to load Intel firmware file (%d)",
			   err);
		kfree_skb(skb);
		return err;
	}

	bt_dev_info(hdev, "Found device firmware: %s", fwname);

	/* Save the DDC file name for later */
	snprintf(fwname, sizeof(fwname), "intel/ibt-%u-%u.ddc",
		le16_to_cpu(ver.hw_variant),
		le16_to_cpu(params->dev_revid));

	kfree_skb(skb);

	if (fw->size < 644) {
		bt_dev_err(hdev, "Invalid size of firmware file (%zu)",
			   fw->size);
		err = -EBADF;
		goto done;
	}

	set_bit(STATE_DOWNLOADING, &intel->flags);

	/* Start the firmware download transaction with the Init fragment
	 * represented by the 128 bytes of CSS header.
	 */
	err = btintel_secure_send(hdev, 0x00, 128, fw->data);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to send firmware header (%d)", err);
		goto done;
	}

	/* Send the 256 bytes of public key information from the firmware
	 * as the PKey fragment.
	 */
	err = btintel_secure_send(hdev, 0x03, 256, fw->data + 128);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to send firmware public key (%d)",
			   err);
		goto done;
	}

	/* Send the 256 bytes of signature information from the firmware
	 * as the Sign fragment.
	 */
	err = btintel_secure_send(hdev, 0x02, 256, fw->data + 388);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to send firmware signature (%d)",
			   err);
		goto done;
	}

	fw_ptr = fw->data + 644;
	frag_len = 0;

	while (fw_ptr - fw->data < fw->size) {
		struct hci_command_hdr *cmd = (void *)(fw_ptr + frag_len);

		frag_len += sizeof(*cmd) + cmd->plen;

		bt_dev_dbg(hdev, "Patching %td/%zu", (fw_ptr - fw->data),
			   fw->size);

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
			bt_dev_err(hdev, "Failed to send firmware data (%d)",
				   err);
			goto done;
		}

		fw_ptr += frag_len;
		frag_len = 0;
	}

	set_bit(STATE_FIRMWARE_LOADED, &intel->flags);

	bt_dev_info(hdev, "Waiting for firmware download to complete");

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
	if (err == -EINTR) {
		bt_dev_err(hdev, "Firmware loading interrupted");
		err = -EINTR;
		goto done;
	}

	if (err) {
		bt_dev_err(hdev, "Firmware loading timeout");
		err = -ETIMEDOUT;
		goto done;
	}

	if (test_bit(STATE_FIRMWARE_FAILED, &intel->flags)) {
		bt_dev_err(hdev, "Firmware loading failed");
		err = -ENOEXEC;
		goto done;
	}

	rettime = ktime_get();
	delta = ktime_sub(rettime, calltime);
	duration = (unsigned long long) ktime_to_ns(delta) >> 10;

	bt_dev_info(hdev, "Firmware loaded in %llu usecs", duration);

done:
	release_firmware(fw);

	if (err < 0)
		return err;

	/* We need to restore the default speed before Intel reset */
	if (speed_change) {
		err = intel_set_baudrate(hu, init_speed);
		if (err)
			return err;
	}

	calltime = ktime_get();

	set_bit(STATE_BOOTING, &intel->flags);

	skb = __hci_cmd_sync(hdev, 0xfc01, sizeof(reset_param), reset_param,
			     HCI_CMD_TIMEOUT);
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
	bt_dev_info(hdev, "Waiting for device to boot");

	err = intel_wait_booting(hu);
	if (err)
		return err;

	clear_bit(STATE_BOOTING, &intel->flags);

	rettime = ktime_get();
	delta = ktime_sub(rettime, calltime);
	duration = (unsigned long long) ktime_to_ns(delta) >> 10;

	bt_dev_info(hdev, "Device booted in %llu usecs", duration);

	/* Enable LPM if matching pdev with wakeup enabled, set TX active
	 * until further LPM TX notification.
	 */
	mutex_lock(&intel_device_list_lock);
	list_for_each(p, &intel_device_list) {
		struct intel_device *dev = list_entry(p, struct intel_device,
						      list);
		if (!hu->tty->dev)
			break;
		if (hu->tty->dev->parent == dev->pdev->dev.parent) {
			if (device_may_wakeup(&dev->pdev->dev)) {
				set_bit(STATE_LPM_ENABLED, &intel->flags);
				set_bit(STATE_TX_ACTIVE, &intel->flags);
			}
			break;
		}
	}
	mutex_unlock(&intel_device_list_lock);

	/* Ignore errors, device can work without DDC parameters */
	btintel_load_ddc_config(hdev, fwname);

	skb = __hci_cmd_sync(hdev, HCI_OP_RESET, 0, NULL, HCI_CMD_TIMEOUT);
	if (IS_ERR(skb))
		return PTR_ERR(skb);
	kfree_skb(skb);

	if (speed_change) {
		err = intel_set_baudrate(hu, oper_speed);
		if (err)
			return err;
	}

	bt_dev_info(hdev, "Setup complete");

	clear_bit(STATE_BOOTLOADER, &intel->flags);

	return 0;
}

static int intel_recv_event(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_uart *hu = hci_get_drvdata(hdev);
	struct intel_data *intel = hu->priv;
	struct hci_event_hdr *hdr;

	if (!test_bit(STATE_BOOTLOADER, &intel->flags) &&
	    !test_bit(STATE_BOOTING, &intel->flags))
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

static void intel_recv_lpm_notify(struct hci_dev *hdev, int value)
{
	struct hci_uart *hu = hci_get_drvdata(hdev);
	struct intel_data *intel = hu->priv;

	bt_dev_dbg(hdev, "TX idle notification (%d)", value);

	if (value) {
		set_bit(STATE_TX_ACTIVE, &intel->flags);
		schedule_work(&intel->busy_work);
	} else {
		clear_bit(STATE_TX_ACTIVE, &intel->flags);
	}
}

static int intel_recv_lpm(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_lpm_pkt *lpm = (void *)skb->data;
	struct hci_uart *hu = hci_get_drvdata(hdev);
	struct intel_data *intel = hu->priv;

	switch (lpm->opcode) {
	case LPM_OP_TX_NOTIFY:
		if (lpm->dlen < 1) {
			bt_dev_err(hu->hdev, "Invalid LPM notification packet");
			break;
		}
		intel_recv_lpm_notify(hdev, lpm->data[0]);
		break;
	case LPM_OP_SUSPEND_ACK:
		set_bit(STATE_SUSPENDED, &intel->flags);
		if (test_and_clear_bit(STATE_LPM_TRANSACTION, &intel->flags)) {
			smp_mb__after_atomic();
			wake_up_bit(&intel->flags, STATE_LPM_TRANSACTION);
		}
		break;
	case LPM_OP_RESUME_ACK:
		clear_bit(STATE_SUSPENDED, &intel->flags);
		if (test_and_clear_bit(STATE_LPM_TRANSACTION, &intel->flags)) {
			smp_mb__after_atomic();
			wake_up_bit(&intel->flags, STATE_LPM_TRANSACTION);
		}
		break;
	default:
		bt_dev_err(hdev, "Unknown LPM opcode (%02x)", lpm->opcode);
		break;
	}

	kfree_skb(skb);

	return 0;
}

#define INTEL_RECV_LPM \
	.type = HCI_LPM_PKT, \
	.hlen = HCI_LPM_HDR_SIZE, \
	.loff = 1, \
	.lsize = 1, \
	.maxlen = HCI_LPM_MAX_SIZE

static const struct h4_recv_pkt intel_recv_pkts[] = {
	{ H4_RECV_ACL,    .recv = hci_recv_frame   },
	{ H4_RECV_SCO,    .recv = hci_recv_frame   },
	{ H4_RECV_EVENT,  .recv = intel_recv_event },
	{ INTEL_RECV_LPM, .recv = intel_recv_lpm   },
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
		bt_dev_err(hu->hdev, "Frame reassembly failed (%d)", err);
		intel->rx_skb = NULL;
		return err;
	}

	return count;
}

static int intel_enqueue(struct hci_uart *hu, struct sk_buff *skb)
{
	struct intel_data *intel = hu->priv;
	struct list_head *p;

	BT_DBG("hu %p skb %p", hu, skb);

	if (!hu->tty->dev)
		goto out_enqueue;

	/* Be sure our controller is resumed and potential LPM transaction
	 * completed before enqueuing any packet.
	 */
	mutex_lock(&intel_device_list_lock);
	list_for_each(p, &intel_device_list) {
		struct intel_device *idev = list_entry(p, struct intel_device,
						       list);

		if (hu->tty->dev->parent == idev->pdev->dev.parent) {
			pm_runtime_get_sync(&idev->pdev->dev);
			pm_runtime_mark_last_busy(&idev->pdev->dev);
			pm_runtime_put_autosuspend(&idev->pdev->dev);
			break;
		}
	}
	mutex_unlock(&intel_device_list_lock);
out_enqueue:
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
	    (hci_skb_pkt_type(skb) == HCI_COMMAND_PKT)) {
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
	memcpy(skb_push(skb, 1), &hci_skb_pkt_type(skb), 1);

	return skb;
}

static const struct hci_uart_proto intel_proto = {
	.id		= HCI_UART_INTEL,
	.name		= "Intel",
	.manufacturer	= 2,
	.init_speed	= 115200,
	.oper_speed	= 3000000,
	.open		= intel_open,
	.close		= intel_close,
	.flush		= intel_flush,
	.setup		= intel_setup,
	.set_baudrate	= intel_set_baudrate,
	.recv		= intel_recv,
	.enqueue	= intel_enqueue,
	.dequeue	= intel_dequeue,
};

#ifdef CONFIG_ACPI
static const struct acpi_device_id intel_acpi_match[] = {
	{ "INT33E1", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, intel_acpi_match);
#endif

#ifdef CONFIG_PM
static int intel_suspend_device(struct device *dev)
{
	struct intel_device *idev = dev_get_drvdata(dev);

	mutex_lock(&idev->hu_lock);
	if (idev->hu)
		intel_lpm_suspend(idev->hu);
	mutex_unlock(&idev->hu_lock);

	return 0;
}

static int intel_resume_device(struct device *dev)
{
	struct intel_device *idev = dev_get_drvdata(dev);

	mutex_lock(&idev->hu_lock);
	if (idev->hu)
		intel_lpm_resume(idev->hu);
	mutex_unlock(&idev->hu_lock);

	return 0;
}
#endif

#ifdef CONFIG_PM_SLEEP
static int intel_suspend(struct device *dev)
{
	struct intel_device *idev = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(idev->irq);

	return intel_suspend_device(dev);
}

static int intel_resume(struct device *dev)
{
	struct intel_device *idev = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(idev->irq);

	return intel_resume_device(dev);
}
#endif

static const struct dev_pm_ops intel_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(intel_suspend, intel_resume)
	SET_RUNTIME_PM_OPS(intel_suspend_device, intel_resume_device, NULL)
};

static const struct acpi_gpio_params reset_gpios = { 0, 0, false };
static const struct acpi_gpio_params host_wake_gpios = { 1, 0, false };

static const struct acpi_gpio_mapping acpi_hci_intel_gpios[] = {
	{ "reset-gpios", &reset_gpios, 1 },
	{ "host-wake-gpios", &host_wake_gpios, 1 },
	{ },
};

static int intel_probe(struct platform_device *pdev)
{
	struct intel_device *idev;
	int ret;

	idev = devm_kzalloc(&pdev->dev, sizeof(*idev), GFP_KERNEL);
	if (!idev)
		return -ENOMEM;

	mutex_init(&idev->hu_lock);

	idev->pdev = pdev;

	ret = devm_acpi_dev_add_driver_gpios(&pdev->dev, acpi_hci_intel_gpios);
	if (ret)
		dev_dbg(&pdev->dev, "Unable to add GPIO mapping table\n");

	idev->reset = devm_gpiod_get(&pdev->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(idev->reset)) {
		dev_err(&pdev->dev, "Unable to retrieve gpio\n");
		return PTR_ERR(idev->reset);
	}

	idev->irq = platform_get_irq(pdev, 0);
	if (idev->irq < 0) {
		struct gpio_desc *host_wake;

		dev_err(&pdev->dev, "No IRQ, falling back to gpio-irq\n");

		host_wake = devm_gpiod_get(&pdev->dev, "host-wake", GPIOD_IN);
		if (IS_ERR(host_wake)) {
			dev_err(&pdev->dev, "Unable to retrieve IRQ\n");
			goto no_irq;
		}

		idev->irq = gpiod_to_irq(host_wake);
		if (idev->irq < 0) {
			dev_err(&pdev->dev, "No corresponding irq for gpio\n");
			goto no_irq;
		}
	}

	/* Only enable wake-up/irq when controller is powered */
	device_set_wakeup_capable(&pdev->dev, true);
	device_wakeup_disable(&pdev->dev);

no_irq:
	platform_set_drvdata(pdev, idev);

	/* Place this instance on the device list */
	mutex_lock(&intel_device_list_lock);
	list_add_tail(&idev->list, &intel_device_list);
	mutex_unlock(&intel_device_list_lock);

	dev_info(&pdev->dev, "registered, gpio(%d)/irq(%d).\n",
		 desc_to_gpio(idev->reset), idev->irq);

	return 0;
}

static int intel_remove(struct platform_device *pdev)
{
	struct intel_device *idev = platform_get_drvdata(pdev);

	device_wakeup_disable(&pdev->dev);

	mutex_lock(&intel_device_list_lock);
	list_del(&idev->list);
	mutex_unlock(&intel_device_list_lock);

	dev_info(&pdev->dev, "unregistered.\n");

	return 0;
}

static struct platform_driver intel_driver = {
	.probe = intel_probe,
	.remove = intel_remove,
	.driver = {
		.name = "hci_intel",
		.acpi_match_table = ACPI_PTR(intel_acpi_match),
		.pm = &intel_pm_ops,
	},
};

int __init intel_init(void)
{
	platform_driver_register(&intel_driver);

	return hci_uart_register_proto(&intel_proto);
}

int __exit intel_deinit(void)
{
	platform_driver_unregister(&intel_driver);

	return hci_uart_unregister_proto(&intel_proto);
}
