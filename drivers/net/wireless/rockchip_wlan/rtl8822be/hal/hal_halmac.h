/******************************************************************************
 *
 * Copyright(c) 2015 - 2016 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef _HAL_HALMAC_H_
#define _HAL_HALMAC_H_

#include <drv_types.h>		/* adapter_to_dvobj(), struct intf_hdl and etc. */
#include "halmac/halmac_api.h"	/* PHALMAC_ADAPTER and etc. */

/* HALMAC Definition for Driver */
#define RTW_HALMAC_H2C_MAX_SIZE		HALMAC_H2C_CMD_ORIGINAL_SIZE_88XX
#define RTW_HALMAC_BA_SSN_RPT_SIZE	4

#define dvobj_set_halmac(d, mac)	((d)->halmac = (mac))
#define dvobj_to_halmac(d)		((PHALMAC_ADAPTER)((d)->halmac))
#define adapter_to_halmac(p)		dvobj_to_halmac(adapter_to_dvobj(p))

/* for H2C cmd */
#define MAX_H2C_BOX_NUMS 4
#define MESSAGE_BOX_SIZE 4
#define EX_MESSAGE_BOX_SIZE 4

typedef enum _RTW_HALMAC_MODE {
	RTW_HALMAC_MODE_NORMAL,
	RTW_HALMAC_MODE_WIFI_TEST,
} RTW_HALMAC_MODE;

extern HALMAC_PLATFORM_API rtw_halmac_platform_api;

/* HALMAC API for Driver(HAL) */
u8 rtw_halmac_read8(struct intf_hdl *, u32 addr);
u16 rtw_halmac_read16(struct intf_hdl *, u32 addr);
u32 rtw_halmac_read32(struct intf_hdl *, u32 addr);
int rtw_halmac_write8(struct intf_hdl *, u32 addr, u8 value);
int rtw_halmac_write16(struct intf_hdl *, u32 addr, u16 value);
int rtw_halmac_write32(struct intf_hdl *, u32 addr, u32 value);

int rtw_halmac_init_adapter(struct dvobj_priv *, PHALMAC_PLATFORM_API);
int rtw_halmac_deinit_adapter(struct dvobj_priv *);
int rtw_halmac_poweron(struct dvobj_priv *);
int rtw_halmac_poweroff(struct dvobj_priv *);
int rtw_halmac_init_hal(struct dvobj_priv *);
int rtw_halmac_init_hal_fw(struct dvobj_priv *, u8 *fw, u32 fwsize);
int rtw_halmac_init_hal_fw_file(struct dvobj_priv *, u8 *fwpath);
int rtw_halmac_deinit_hal(struct dvobj_priv *);
int rtw_halmac_self_verify(struct dvobj_priv *);
int rtw_halmac_dlfw(struct dvobj_priv *, u8 *fw, u32 fwsize);
int rtw_halmac_dlfw_from_file(struct dvobj_priv *, u8 *fwpath);
int rtw_halmac_phy_power_switch(struct dvobj_priv *, u8 enable);
int rtw_halmac_send_h2c(struct dvobj_priv *, u8 *h2c);
int rtw_halmac_c2h_handle(struct dvobj_priv *, u8 *c2h, u32 size);
int rtw_halmac_get_available_efuse_size(struct dvobj_priv *d, u32 *size);

int rtw_halmac_get_physical_efuse_size(struct dvobj_priv *, u32 *size);
int rtw_halmac_read_physical_efuse_map(struct dvobj_priv *, u8 *map, u32 size);
int rtw_halmac_read_physical_efuse(struct dvobj_priv *, u32 offset, u32 cnt, u8 *data);
int rtw_halmac_write_physical_efuse(struct dvobj_priv *, u32 offset, u32 cnt, u8 *data);
int rtw_halmac_get_logical_efuse_size(struct dvobj_priv *, u32 *size);
int rtw_halmac_read_logical_efuse_map(struct dvobj_priv *, u8 *map, u32 size);
int rtw_halmac_write_logical_efuse_map(struct dvobj_priv *, u8 *map, u32 size, u8 *maskmap, u32 masksize);
int rtw_halmac_read_logical_efuse(struct dvobj_priv *, u32 offset, u32 cnt, u8 *data);
int rtw_halmac_write_logical_efuse(struct dvobj_priv *, u32 offset, u32 cnt, u8 *data);

int rtw_halmac_write_bt_physical_efuse(struct dvobj_priv *, u32 offset, u32 cnt, u8 *data);
int rtw_halmac_read_bt_physical_efuse_map(struct dvobj_priv *, u8 *map, u32 size);

int rtw_halmac_config_rx_info(struct dvobj_priv *, HALMAC_DRV_INFO);
int rtw_halmac_set_mac_address(struct dvobj_priv *, enum _hw_port, u8 *addr);
int rtw_halmac_set_bssid(struct dvobj_priv *d, enum _hw_port hwport, u8 *addr);

int rtw_halmac_set_bandwidth(struct dvobj_priv *, u8 channel, u8 pri_ch_idx, u8 bw);
int rtw_halmac_rx_agg_switch(struct dvobj_priv *, u8 enable);
int rtw_halmac_get_hw_value(struct dvobj_priv *d, HALMAC_HW_ID hw_id, VOID *pvalue);

int rtw_halmac_get_wow_reason(struct dvobj_priv *, u8 *reason);
int rtw_halmac_get_drv_info_sz(struct dvobj_priv *d, u8 *sz);

int rtw_halmac_get_rsvd_drv_pg_bndy(struct dvobj_priv *dvobj, u16 *drv_pg);
int rtw_halmac_download_rsvd_page(struct dvobj_priv *dvobj, u16 page_idx, u8 *pbuf, u8 length);

#ifdef CONFIG_SDIO_HCI
int rtw_halmac_query_tx_page_num(struct dvobj_priv *);
int rtw_halmac_get_tx_queue_page_num(struct dvobj_priv *, u8 queue, u32 *page);
u32 rtw_halmac_sdio_get_tx_addr(struct dvobj_priv *, u8 *desc, u32 size);
int rtw_halmac_sdio_tx_allowed(struct dvobj_priv *, u8 *buf, u32 size);
u32 rtw_halmac_sdio_get_rx_addr(struct dvobj_priv *, u8 *seq);
#endif /* CONFIG_SDIO_HCI */

#ifdef CONFIG_USB_HCI
u8 rtw_halmac_usb_get_bulkout_id(struct dvobj_priv *, u8 *buf, u32 size);
u8 rtw_halmac_switch_usb_mode(struct dvobj_priv *d, enum RTW_USB_SPEED usb_mode);
#endif /* CONFIG_USB_HCI */

#endif /* _HAL_HALMAC_H_ */
