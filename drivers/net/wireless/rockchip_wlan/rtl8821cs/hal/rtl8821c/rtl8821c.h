/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#ifndef _RTL8821C_H_
#define _RTL8821C_H_

#include <drv_types.h>		/* PADAPTER */
#include <rtw_rf.h>		/* CHANNEL_WIDTH */
#include <rtw_xmit.h>		/* struct pkt_attrib, struct xmit_frame */
#include <rtw_recv.h>		/* struct recv_frame */
#include <hal_intf.h>		/* HAL_DEF_VARIABLE */
#include <rtl8821c_dm.h>

#define DRIVER_EARLY_INT_TIME_8821C	0x05
#define BCN_DMA_ATIME_INT_TIME_8821C	0x02

/* rtl8821c_halinit.c */
u32 rtl8821c_power_on(PADAPTER);
void rtl8821c_power_off(PADAPTER);
u8 rtl8821c_mac_init(PADAPTER);
u8 rtl8821c_mac_verify(PADAPTER);
void rtl8821c_hal_init_misc(PADAPTER padapter);
u32 rtl8821c_hal_init(PADAPTER);
u32 rtl8821c_hal_deinit(PADAPTER);
void rtl8821c_init_default_value(PADAPTER);
u8 rtl8821c_phy_init(PADAPTER adapter);
u8 rtl8821c_init_phy_parameter_mac(PADAPTER adapter);

/* rtl8821c_mac.c */
#ifdef CONFIG_XMIT_ACK
u8 rtl8821c_set_mgnt_xmit_ack(_adapter *adapter);
#endif
u8 rtl8821c_rx_ba_ssn_appended(PADAPTER);
u8 rtl8821c_rx_fcs_append_switch(PADAPTER, u8 enable);
u8 rtl8821c_rx_fcs_appended(PADAPTER);
u8 rtl8821c_rx_tsf_addr_filter_config(_adapter *adapter, u8 config);
s32 rtl8821c_fw_dl(PADAPTER, u8 wowlan);
s32 rtl8821c_fw_mem_dl(PADAPTER adapter, enum fw_mem mem);

#define BIT_PRETXERR_HANDLE_IMR	BIT(31)
#define BIT_PRETXERR_HANDLE_ISR	BIT(31)

#ifdef CONFIG_AMPDU_PRETX_CD
#define BIT_PRETXERR			BIT(7)

void rtl8821c_pretx_cd_config(_adapter *adapter);
#endif
/* rtl8821c_ops.c */
u8 rtl8821c_read_efuse(PADAPTER);
void rtl8821c_run_thread(PADAPTER);
void rtl8821c_cancel_thread(PADAPTER);
u8 rtl8821c_sethwreg(PADAPTER, u8 variable, u8 *pval);
void rtl8821c_gethwreg(PADAPTER, u8 variable, u8 *pval);
u8 rtl8821c_sethaldefvar(PADAPTER, HAL_DEF_VARIABLE, void *pval);
u8 rtl8821c_gethaldefvar(PADAPTER, HAL_DEF_VARIABLE, void *pval);
void rtl8821c_set_hal_ops(PADAPTER);

/* tx */
void rtl8821c_init_xmit_priv(_adapter *adapter);
void rtl8821c_fill_txdesc_sectype(struct pkt_attrib *, u8 *ptxdesc);
void rtl8821c_fill_txdesc_vcs(PADAPTER, struct pkt_attrib *, u8 *ptxdesc);
void rtl8821c_fill_txdesc_phy(PADAPTER, struct pkt_attrib *, u8 *ptxdesc);
u8 rtl8821c_bw_mapping(PADAPTER, struct pkt_attrib *);
u8 rtl8821c_sc_mapping(PADAPTER, struct pkt_attrib *);
void rtl8821c_cal_txdesc_chksum(PADAPTER, u8 *ptxdesc);
void rtl8821c_update_txdesc(struct xmit_frame *, u8 *pbuf);
void rtl8821c_dbg_dump_tx_desc(PADAPTER, int frame_tag, u8 *ptxdesc);
void fill_txdesc_force_bmc_camid(struct pkt_attrib *pattrib, u8 *ptxdesc);
void fill_txdesc_bmc_tx_rate(struct pkt_attrib *pattrib, u8 *ptxdesc);

/* rx */
void rtl8821c_rxdesc2attribute(struct rx_pkt_attrib *a, u8 *desc);
void rtl8821c_query_rx_desc(union recv_frame *, u8 *pdesc);

/* rtl8821c_cmd.c */
s32 rtl8821c_fillh2ccmd(PADAPTER, u8 id, u32 buf_len, u8 *pbuf);
void rtl8821c_set_FwPwrMode_cmd(PADAPTER, u8 psmode);
void rtl8821c_set_FwPwrModeInIPS_cmd(PADAPTER adapter, u8 cmd_param);
void c2h_handler_rtl8821c(_adapter *adapter, u8 *pbuf, u16 length);
void c2h_pre_handler_rtl8821c(_adapter *adapter, u8 *pbuf, s32 length);
#ifdef CONFIG_BT_COEXIST
void rtl8821c_download_BTCoex_AP_mode_rsvd_page(PADAPTER);
#endif /* CONFIG_BT_COEXIST */

/* rtl8821c_phy.c */
u32 rtl8821c_read_bb_reg(PADAPTER, u32 addr, u32 mask);
void rtl8821c_write_bb_reg(PADAPTER, u32 addr, u32 mask, u32 val);
u32 rtl8821c_read_rf_reg(PADAPTER adapter, enum rf_path path, u32 addr, u32 mask);
void rtl8821c_write_rf_reg(PADAPTER adapter, enum rf_path path, u32 addr, u32 mask, u32 val);
void rtl8821c_set_channel_bw(PADAPTER adapter, u8 center_ch, enum channel_width, u8 offset40, u8 offset80);
void rtl8821c_set_tx_power_level(PADAPTER, u8 channel);
void rtl8821c_set_tx_power_index(PADAPTER adapter, u32 powerindex, enum rf_path rfpath, u8 rate);
void rtl8821c_notch_filter_switch(PADAPTER, bool enable);
#ifdef CONFIG_BEAMFORMING
void rtl8821c_phy_bf_init(PADAPTER);
void rtl8821c_phy_bf_enter(PADAPTER, struct sta_info*);
void rtl8821c_phy_bf_leave(PADAPTER, u8 *addr);
void rtl8821c_phy_bf_set_gid_table(PADAPTER, struct beamformer_entry*);
#endif /* CONFIG_BEAMFORMING */
#endif /* _RTL8821C_H_ */
