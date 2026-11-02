/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Qualcomm Bluetooth USB transport-specific support
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __BTUSB_QCOM_H
#define __BTUSB_QCOM_H

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/skbuff.h>
#include <linux/usb.h>

#include <net/bluetooth/hci_core.h>

#define QPERI_COMMAND_PKT	0x31
#define QPERI_ACLDATA_PKT	0x32
#define QPERI_EVENT_PKT		0x34

struct btusb_qcom {
	__u16                 idVendor;
	__u16                 idProduct;
	struct usb_device    *udev;
	struct usb_interface *intf;
	struct gpio_desc     *reset_gpio;

	/* serializes sending BT and vendor frames to the transport */
	struct mutex          tx_mutex;

	void (*prepare_reset)(struct hci_dev *hdev);
	int (*recv_bt_frame)(struct hci_dev *hdev, struct sk_buff *skb);
	int (*send_bt_frame)(struct hci_dev *hdev, struct sk_buff *skb);
	int (*send_vendor_frame)(struct hci_dev *hdev, struct sk_buff *skb);
};

#if IS_ENABLED(CONFIG_BT_HCIBTUSB_QCOM)

int btusb_qcom_hdev_priv_size(void);
struct btusb_qcom *btusb_qcom_xport_data(struct hci_dev *hdev);

int btusb_qcom_setup(struct hci_dev *hdev);
int btusb_qcom_disconnect(struct hci_dev *hdev);
int btusb_qcom_send_frame(struct hci_dev *hdev, struct sk_buff *skb);

struct sk_buff *btusb_qcom_recv_intr(struct hci_dev *hdev, struct sk_buff *skb,
				     void *buffer, int count, int *err);

struct sk_buff *btusb_qcom_recv_bulk(struct hci_dev *hdev, struct sk_buff *skb,
				     void *buffer, int count, int *err);

#else

static inline int btusb_qcom_hdev_priv_size(void)
{
	return 0;
}

static inline struct btusb_qcom *btusb_qcom_xport_data(struct hci_dev *hdev)
{
	return NULL;
}

static inline int btusb_qcom_setup(struct hci_dev *hdev)
{
	return -EOPNOTSUPP;
}

static inline int btusb_qcom_disconnect(struct hci_dev *hdev)
{
	return -EOPNOTSUPP;
}

static inline int btusb_qcom_send_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
	return -EOPNOTSUPP;
}

static inline struct sk_buff *btusb_qcom_recv_intr(struct hci_dev *hdev,
						   struct sk_buff *skb,
						   void *buffer, int count,
						   int *err)
{
	*err = -EOPNOTSUPP;
	return NULL;
}

static inline struct sk_buff *btusb_qcom_recv_bulk(struct hci_dev *hdev,
						   struct sk_buff *skb,
						   void *buffer, int count,
						   int *err)
{
	*err = -EOPNOTSUPP;
	return NULL;
}

#endif
#endif
