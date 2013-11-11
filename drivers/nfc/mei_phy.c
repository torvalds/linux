/*
 * MEI Library for mei bus nfc device access
 *
 * Copyright (C) 2013  Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/nfc.h>

#include "mei_phy.h"

struct mei_nfc_hdr {
	u8 cmd;
	u8 status;
	u16 req_id;
	u32 reserved;
	u16 data_size;
} __attribute__((packed));

#define MEI_NFC_MAX_READ (MEI_NFC_HEADER_SIZE + MEI_NFC_MAX_HCI_PAYLOAD)

#define MEI_DUMP_SKB_IN(info, skb)				\
do {								\
	pr_debug("%s:\n", info);				\
	print_hex_dump_debug("mei in : ", DUMP_PREFIX_OFFSET,	\
			16, 1, (skb)->data, (skb)->len, false);	\
} while (0)

#define MEI_DUMP_SKB_OUT(info, skb)				\
do {								\
	pr_debug("%s:\n", info);				\
	print_hex_dump_debug("mei out: ", DUMP_PREFIX_OFFSET,	\
		       16, 1, (skb)->data, (skb)->len, false);	\
} while (0)

int nfc_mei_phy_enable(void *phy_id)
{
	int r;
	struct nfc_mei_phy *phy = phy_id;

	pr_info("%s\n", __func__);

	if (phy->powered == 1)
		return 0;

	r = mei_cl_enable_device(phy->device);
	if (r < 0) {
                pr_err("MEI_PHY: Could not enable device\n");
                return r;
	}

	r = mei_cl_register_event_cb(phy->device, nfc_mei_event_cb, phy);
	if (r) {
		pr_err("MEY_PHY: Event cb registration failed\n");
		mei_cl_disable_device(phy->device);
		phy->powered = 0;

		return r;
	}

	phy->powered = 1;

	return 0;
}
EXPORT_SYMBOL_GPL(nfc_mei_phy_enable);

void nfc_mei_phy_disable(void *phy_id)
{
	struct nfc_mei_phy *phy = phy_id;

	pr_info("%s\n", __func__);

	mei_cl_disable_device(phy->device);

	phy->powered = 0;
}
EXPORT_SYMBOL_GPL(nfc_mei_phy_disable);

/*
 * Writing a frame must not return the number of written bytes.
 * It must return either zero for success, or <0 for error.
 * In addition, it must not alter the skb
 */
static int nfc_mei_phy_write(void *phy_id, struct sk_buff *skb)
{
	struct nfc_mei_phy *phy = phy_id;
	int r;

	MEI_DUMP_SKB_OUT("mei frame sent", skb);

	r = mei_cl_send(phy->device, skb->data, skb->len);
	if (r > 0)
		r = 0;

	return r;
}

void nfc_mei_event_cb(struct mei_cl_device *device, u32 events, void *context)
{
	struct nfc_mei_phy *phy = context;

	if (phy->hard_fault != 0)
		return;

	if (events & BIT(MEI_CL_EVENT_RX)) {
		struct sk_buff *skb;
		int reply_size;

		skb = alloc_skb(MEI_NFC_MAX_READ, GFP_KERNEL);
		if (!skb)
			return;

		reply_size = mei_cl_recv(device, skb->data, MEI_NFC_MAX_READ);
		if (reply_size < MEI_NFC_HEADER_SIZE) {
			kfree(skb);
			return;
		}

		skb_put(skb, reply_size);
		skb_pull(skb, MEI_NFC_HEADER_SIZE);

		MEI_DUMP_SKB_IN("mei frame read", skb);

		nfc_hci_recv_frame(phy->hdev, skb);
	}
}
EXPORT_SYMBOL_GPL(nfc_mei_event_cb);

struct nfc_phy_ops mei_phy_ops = {
	.write = nfc_mei_phy_write,
	.enable = nfc_mei_phy_enable,
	.disable = nfc_mei_phy_disable,
};
EXPORT_SYMBOL_GPL(mei_phy_ops);

struct nfc_mei_phy *nfc_mei_phy_alloc(struct mei_cl_device *device)
{
	struct nfc_mei_phy *phy;

	phy = kzalloc(sizeof(struct nfc_mei_phy), GFP_KERNEL);
	if (!phy)
		return NULL;

	phy->device = device;
	mei_cl_set_drvdata(device, phy);

	return phy;
}
EXPORT_SYMBOL_GPL(nfc_mei_phy_alloc);

void nfc_mei_phy_free(struct nfc_mei_phy *phy)
{
	kfree(phy);
}
EXPORT_SYMBOL_GPL(nfc_mei_phy_free);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("mei bus NFC device interface");
