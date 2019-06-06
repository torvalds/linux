/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#ifndef __RTW_MAC_H__
#define __RTW_MAC_H__

#define RTW_HW_PORT_NUM		5
#define cut_version_to_mask(cut) (0x1 << ((cut) + 1))
#define SDIO_LOCAL_OFFSET	0x10250000
#define DDMA_POLLING_COUNT	1000
#define C2H_PKT_BUF		256
#define PHY_STATUS_SIZE		4
#define ILLEGAL_KEY_GROUP	0xFAAAAA00

/* HW memory address */
#define OCPBASE_TXBUF_88XX		0x18780000
#define OCPBASE_DMEM_88XX		0x00200000
#define OCPBASE_EMEM_88XX		0x00100000

#define RSVD_PG_DRV_NUM			16
#define RSVD_PG_H2C_EXTRAINFO_NUM	24
#define RSVD_PG_H2C_STATICINFO_NUM	8
#define RSVD_PG_H2CQ_NUM		8
#define RSVD_PG_CPU_INSTRUCTION_NUM	0
#define RSVD_PG_FW_TXBUF_NUM		4

void rtw_set_channel_mac(struct rtw_dev *rtwdev, u8 channel, u8 bw,
			 u8 primary_ch_idx);
int rtw_mac_power_on(struct rtw_dev *rtwdev);
void rtw_mac_power_off(struct rtw_dev *rtwdev);
int rtw_download_firmware(struct rtw_dev *rtwdev, struct rtw_fw_state *fw);
int rtw_mac_init(struct rtw_dev *rtwdev);

#endif
