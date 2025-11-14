/* SPDX-License-Identifier: GPL-2.0+ */
// Copyright (c) 2016-2017 Hisilicon Limited.

#ifndef __HCLGE_MDIO_H
#define __HCLGE_MDIO_H

#include "hnae3.h"

struct hclge_dev;

int hclge_mac_mdio_config(struct hclge_dev *hdev);
int hclge_mac_connect_phy(struct hnae3_handle *handle);
void hclge_mac_disconnect_phy(struct hnae3_handle *handle);
void hclge_mac_start_phy(struct hclge_dev *hdev);
void hclge_mac_stop_phy(struct hclge_dev *hdev);
int hclge_read_phy_reg(struct hclge_dev *hdev, u16 reg_addr, u16 *val);
int hclge_write_phy_reg(struct hclge_dev *hdev, u16 reg_addr, u16 val);

#endif
