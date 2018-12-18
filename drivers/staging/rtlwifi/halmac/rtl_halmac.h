/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef _RTL_HALMAC_H_
#define _RTL_HALMAC_H_

#include "halmac_api.h"

#define rtlpriv_to_halmac(priv)                                                \
	((struct halmac_adapter *)((priv)->halmac.internal))

/* for H2C cmd */
#define MAX_H2C_BOX_NUMS 4
#define MESSAGE_BOX_SIZE 4
#define EX_MESSAGE_BOX_SIZE 4

/* HALMAC API for Driver(HAL) */
int rtl_halmac_init_adapter(struct rtl_priv *rtlpriv);
int rtl_halmac_deinit_adapter(struct rtl_priv *rtlpriv);
int rtl_halmac_poweron(struct rtl_priv *rtlpriv);
int rtl_halmac_poweroff(struct rtl_priv *rtlpriv);
int rtl_halmac_init_hal(struct rtl_priv *rtlpriv);
int rtl_halmac_init_hal_fw(struct rtl_priv *rtlpriv, u8 *fw, u32 fwsize);
int rtl_halmac_init_hal_fw_file(struct rtl_priv *rtlpriv, u8 *fwpath);
int rtl_halmac_deinit_hal(struct rtl_priv *rtlpriv);
int rtl_halmac_self_verify(struct rtl_priv *rtlpriv);
int rtl_halmac_dlfw(struct rtl_priv *rtlpriv, u8 *fw, u32 fwsize);
int rtl_halmac_dlfw_from_file(struct rtl_priv *rtlpriv, u8 *fwpath);
int rtl_halmac_phy_power_switch(struct rtl_priv *rtlpriv, u8 enable);
int rtl_halmac_send_h2c(struct rtl_priv *rtlpriv, u8 *h2c);
int rtl_halmac_c2h_handle(struct rtl_priv *rtlpriv, u8 *c2h, u32 size);

int rtl_halmac_get_physical_efuse_size(struct rtl_priv *rtlpriv, u32 *size);
int rtl_halmac_read_physical_efuse_map(struct rtl_priv *rtlpriv, u8 *map,
				       u32 size);
int rtl_halmac_read_physical_efuse(struct rtl_priv *rtlpriv, u32 offset,
				   u32 cnt, u8 *data);
int rtl_halmac_write_physical_efuse(struct rtl_priv *rtlpriv, u32 offset,
				    u32 cnt, u8 *data);
int rtl_halmac_get_logical_efuse_size(struct rtl_priv *rtlpriv, u32 *size);
int rtl_halmac_read_logical_efuse_map(struct rtl_priv *rtlpriv, u8 *map,
				      u32 size);
int rtl_halmac_write_logical_efuse_map(struct rtl_priv *rtlpriv, u8 *map,
				       u32 size, u8 *maskmap, u32 masksize);
int rtl_halmac_read_logical_efuse(struct rtl_priv *rtlpriv, u32 offset, u32 cnt,
				  u8 *data);
int rtl_halmac_write_logical_efuse(struct rtl_priv *rtlpriv, u32 offset,
				   u32 cnt, u8 *data);

int rtl_halmac_config_rx_info(struct rtl_priv *rtlpriv, enum halmac_drv_info);
int rtl_halmac_set_mac_address(struct rtl_priv *rtlpriv, u8 hwport, u8 *addr);
int rtl_halmac_set_bssid(struct rtl_priv *d, u8 hwport, u8 *addr);

int rtl_halmac_set_bandwidth(struct rtl_priv *rtlpriv, u8 channel,
			     u8 pri_ch_idx, u8 bw);
int rtl_halmac_rx_agg_switch(struct rtl_priv *rtlpriv, bool enable);
int rtl_halmac_get_hw_value(struct rtl_priv *d, enum halmac_hw_id hw_id,
			    void *pvalue);
int rtl_halmac_dump_fifo(struct rtl_priv *rtlpriv,
			 enum hal_fifo_sel halmac_fifo_sel);

int rtl_halmac_get_wow_reason(struct rtl_priv *rtlpriv, u8 *reason);
int rtl_halmac_get_drv_info_sz(struct rtl_priv *d, u8 *sz);

int rtl_halmac_get_rsvd_drv_pg_bndy(struct rtl_priv *dvobj, u16 *drv_pg);
int rtl_halmac_download_rsvd_page(struct rtl_priv *dvobj, u8 pg_offset,
				  u8 *pbuf, u32 size);

int rtl_halmac_chk_txdesc(struct rtl_priv *rtlpriv, u8 *txdesc, u32 size);

struct rtl_halmac_ops *rtl_halmac_get_ops_pointer(void);

#endif /* _RTL_HALMAC_H_ */
