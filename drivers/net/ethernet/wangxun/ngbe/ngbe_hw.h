/* SPDX-License-Identifier: GPL-2.0 */
/*
 * WangXun Gigabit PCI Express Linux driver
 * Copyright (c) 2019 - 2022 Beijing WangXun Technology Co., Ltd.
 */

#ifndef _NGBE_HW_H_
#define _NGBE_HW_H_

int ngbe_eeprom_chksum_hostif(struct ngbe_hw *hw);
int ngbe_reset_hw(struct ngbe_hw *hw);
#endif /* _NGBE_HW_H_ */
