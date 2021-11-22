/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 * Copyright(c) 2019-2020  Realtek Corporation
 */
#ifndef __SER_H__
#define __SER_H__

#include "core.h"

int rtw89_ser_init(struct rtw89_dev *rtwdev);
int rtw89_ser_deinit(struct rtw89_dev *rtwdev);
int rtw89_ser_notify(struct rtw89_dev *rtwdev, u32 err);
void rtw89_ser_recfg_done(struct rtw89_dev *rtwdev);

#endif /* __SER_H__*/

