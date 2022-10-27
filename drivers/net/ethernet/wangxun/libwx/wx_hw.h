/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2015 - 2022 Beijing WangXun Technology Co., Ltd. */

#ifndef _WX_HW_H_
#define _WX_HW_H_

int wx_check_flash_load(struct wx_hw *hw, u32 check_bit);
int wx_stop_adapter(struct wx_hw *wxhw);
void wx_reset_misc(struct wx_hw *wxhw);
int wx_sw_init(struct wx_hw *wxhw);

#endif /* _WX_HW_H_ */
