/*
 * Copyright (c) 2016-2017 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __HCLGE_MDIO_H
#define __HCLGE_MDIO_H

int hclge_mac_mdio_config(struct hclge_dev *hdev);
int hclge_mac_start_phy(struct hclge_dev *hdev);
void hclge_mac_stop_phy(struct hclge_dev *hdev);

#endif
