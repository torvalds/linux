/* SPDX-License-Identifier: GPL-2.0 */
/*
 * WangXun Gigabit PCI Express Linux driver
 * Copyright (c) 2019 - 2022 Beijing WangXun Technology Co., Ltd.
 */

#ifndef _NGBE_MDIO_H_
#define _NGBE_MDIO_H_

int ngbe_phy_connect(struct wx *wx);
int ngbe_mdio_init(struct wx *wx);
#endif /* _NGBE_MDIO_H_ */
