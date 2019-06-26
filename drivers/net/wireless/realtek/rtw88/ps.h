/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#ifndef __RTW_PS_H_
#define __RTW_PS_H_

#define RTW_LPS_THRESHOLD	2

int rtw_enter_ips(struct rtw_dev *rtwdev);
int rtw_leave_ips(struct rtw_dev *rtwdev);

void rtw_lps_work(struct work_struct *work);
void rtw_enter_lps_irqsafe(struct rtw_dev *rtwdev, struct rtw_vif *rtwvif);
void rtw_leave_lps_irqsafe(struct rtw_dev *rtwdev, struct rtw_vif *rtwvif);
void rtw_enter_lps(struct rtw_dev *rtwdev, struct rtw_vif *rtwvif);
void rtw_leave_lps(struct rtw_dev *rtwdev, struct rtw_vif *rtwvif);
bool rtw_in_lps(struct rtw_dev *rtwdev);

#endif
