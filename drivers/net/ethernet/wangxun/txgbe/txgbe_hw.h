/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2015 - 2022 Beijing WangXun Technology Co., Ltd. */

#ifndef _TXGBE_HW_H_
#define _TXGBE_HW_H_

int txgbe_read_pba_string(struct txgbe_hw *hw, u8 *pba_num, u32 pba_num_size);
int txgbe_validate_eeprom_checksum(struct txgbe_hw *hw, u16 *checksum_val);
int txgbe_reset_hw(struct txgbe_hw *hw);

#endif /* _TXGBE_HW_H_ */
