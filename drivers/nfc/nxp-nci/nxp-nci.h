/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2014  NXP Semiconductors  All rights reserved.
 *
 * Authors: Clément Perrochaud <clement.perrochaud@nxp.com>
 *
 * Derived from PN544 device driver:
 * Copyright (C) 2012  Intel Corporation. All rights reserved.
*/

#ifndef __LOCAL_NXP_NCI_H_
#define __LOCAL_NXP_NCI_H_

#include <linux/completion.h>
#include <linux/firmware.h>
#include <linux/nfc.h>
#include <linux/platform_data/nxp-nci.h>

#include <net/nfc/nci_core.h>

#define NXP_NCI_FW_HDR_LEN	2
#define NXP_NCI_FW_CRC_LEN	2

#define NXP_NCI_FW_FRAME_LEN_MASK	0x03FF

enum nxp_nci_mode {
	NXP_NCI_MODE_COLD,
	NXP_NCI_MODE_NCI,
	NXP_NCI_MODE_FW
};

struct nxp_nci_phy_ops {
	int (*set_mode)(void *id, enum nxp_nci_mode mode);
	int (*write)(void *id, struct sk_buff *skb);
};

struct nxp_nci_fw_info {
	char name[NFC_FIRMWARE_NAME_MAXSIZE + 1];
	const struct firmware *fw;

	size_t size;
	size_t written;

	const u8 *data;
	size_t frame_size;

	struct work_struct work;
	struct completion cmd_completion;

	int cmd_result;
};

struct nxp_nci_info {
	struct nci_dev *ndev;
	void *phy_id;
	struct device *pdev;

	enum nxp_nci_mode mode;

	const struct nxp_nci_phy_ops *phy_ops;
	unsigned int max_payload;

	struct mutex info_lock;

	struct nxp_nci_fw_info fw_info;
};

int nxp_nci_fw_download(struct nci_dev *ndev, const char *firmware_name);
void nxp_nci_fw_work(struct work_struct *work);
void nxp_nci_fw_recv_frame(struct nci_dev *ndev, struct sk_buff *skb);
void nxp_nci_fw_work_complete(struct nxp_nci_info *info, int result);

int nxp_nci_probe(void *phy_id, struct device *pdev,
		  const struct nxp_nci_phy_ops *phy_ops,
		  unsigned int max_payload,
		  struct nci_dev **ndev);
void nxp_nci_remove(struct nci_dev *ndev);

#endif /* __LOCAL_NXP_NCI_H_ */
