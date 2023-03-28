/* SPDX-License-Identifier: GPL-2.0 */
/*
 * WangXun Gigabit PCI Express Linux driver
 * Copyright (c) 2019 - 2022 Beijing WangXun Technology Co., Ltd.
 */

#ifndef _NGBE_HW_H_
#define _NGBE_HW_H_

int ngbe_eeprom_chksum_hostif(struct wx *wx);
void ngbe_sfp_modules_txrx_powerctl(struct wx *wx, bool swi);
int ngbe_reset_hw(struct wx *wx);
#endif /* _NGBE_HW_H_ */
