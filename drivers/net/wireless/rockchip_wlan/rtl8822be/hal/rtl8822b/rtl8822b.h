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
#ifndef _RTL8822B_H_
#define _RTL8822B_H_

#include <drv_types.h>		/* PADAPTER */
#include <rtw_rf.h>		/* CHANNEL_WIDTH */
#include <rtw_xmit.h>		/* struct pkt_attrib, struct xmit_frame */
#include <rtw_recv.h>		/* struct recv_frame */
#include <hal_intf.h>		/* HAL_DEF_VARIABLE */

#define DRIVER_EARLY_INT_TIME_8822B	0x05
#define BCN_DMA_ATIME_INT_TIME_8822B	0x02

struct hw_port_reg {
	u32 net_type;	/*reg_offset*/
	u8 net_type_shift;
	u32 macaddr;	/*reg_offset*/
	u32 bssid;	/*reg_offset*/
	u32 bcn_ctl;			/*reg_offset*/
	u32 tsf_rst;			/*reg_offset*/
	u8 tsf_rst_bit;
	u32 bcn_space;		/*reg_offset*/
	u8 bcn_space_shift;
	u16 bcn_space_mask;
};

/* rtl8822b_halinit.c */
void rtl8822b_init_hal_spec(PADAPTER);
u32 rtl8822b_power_on(PADAPTER);
void rtl8822b_power_off(PADAPTER);
u8 rtl8822b_hal_init(PADAPTER);
u8 rtl8822b_mac_verify(PADAPTER);
void rtl8822b_init_misc(PADAPTER padapter);
u32 rtl8822b_init(PADAPTER);
u32 rtl8822b_deinit(PADAPTER);
void rtl8822b_init_default_value(PADAPTER);

/* rtl8822b_mac.c */
u8 rtl8822b_rcr_config(PADAPTER, u32 rcr);
u8 rtl8822b_rcr_get(PADAPTER, u32 *rcr);
u8 rtl8822b_rcr_check(PADAPTER, u32 check_bit);
u8 rtl8822b_rcr_add(PADAPTER, u32 add);
u8 rtl8822b_rcr_clear(PADAPTER, u32 clear);
u8 rtl8822b_rx_ba_ssn_appended(PADAPTER);
u8 rtl8822b_rx_fcs_append_switch(PADAPTER, u8 enable);
u8 rtl8822b_rx_fcs_appended(PADAPTER);
u8 rtl8822b_rx_tsf_addr_filter_config(PADAPTER, u8 config);
s32 rtl8822b_fw_dl(PADAPTER, u8 wowlan);

/* rtl8822b_ops.c */
u8 rtl8822b_read_efuse(PADAPTER);
void rtl8822b_run_thread(PADAPTER);
void rtl8822b_cancel_thread(PADAPTER);
void rtl8822b_sethwreg(PADAPTER, u8 variable, u8 *pval);
void rtl8822b_gethwreg(PADAPTER, u8 variable, u8 *pval);
void rtl8822b_sethwregwithbuf(PADAPTER, u8 variable, u8 *pbuf, int len);
u8 rtl8822b_sethaldefvar(PADAPTER, HAL_DEF_VARIABLE, void *pval);
u8 rtl8822b_gethaldefvar(PADAPTER, HAL_DEF_VARIABLE, void *pval);
void rtl8822b_set_hal_ops(PADAPTER);
void rtl8822b_resume_tx_beacon(PADAPTER);
void rtl8822b_stop_tx_beacon(PADAPTER);

/* tx */
void rtl8822b_fill_txdesc_sectype(struct pkt_attrib *, u8 *ptxdesc);
void rtl8822b_fill_txdesc_vcs(PADAPTER, struct pkt_attrib *, u8 *ptxdesc);
void rtl8822b_fill_txdesc_phy(PADAPTER, struct pkt_attrib *, u8 *ptxdesc);
void rtl8822b_fill_txdesc_force_bmc_camid(struct pkt_attrib *, u8 *ptxdesc);
u8 rtl8822b_bw_mapping(PADAPTER, struct pkt_attrib *);
u8 rtl8822b_sc_mapping(PADAPTER, struct pkt_attrib *);
void rtl8822b_cal_txdesc_chksum(PADAPTER, u8 *ptxdesc);
void rtl8822b_update_txdesc(struct xmit_frame *, u8 *pbuf);
void rtl8822b_dbg_dump_tx_desc(PADAPTER, int frame_tag, u8 *ptxdesc);

/* rx */
void rtl8822b_rxdesc2attribute(struct rx_pkt_attrib *a, u8 *desc);
void rtl8822b_query_rx_desc(union recv_frame *, u8 *pdesc);

/* rtl8822b_cmd.c */
s32 rtl8822b_fillh2ccmd(PADAPTER, u8 id, u32 buf_len, u8 *pbuf);
void rtl8822b_set_FwMediaStatusRpt_cmd(PADAPTER, u8 mstatus, u8 macid);
void rtl8822b_set_FwMacIdConfig_cmd(PADAPTER , u64 bitmap, u8 *arg);
void rtl8822b_set_FwRssiSetting_cmd(PADAPTER, u8 *param);
void rtl8822b_set_FwPwrMode_cmd(PADAPTER, u8 psmode);
void rtl8822b_req_txrpt_cmd(PADAPTER, u8 macid);
#ifdef CONFIG_P2P
void rtl8822b_set_p2p_ps_offload_cmd(PADAPTER, u8 p2p_ps_state);
#endif
void rtl8822b_fw_update_beacon_cmd(PADAPTER);
void rtl8822b_c2h_handler(PADAPTER, u8 *pbuf, u16 length);
void rtl8822b_c2h_handler_no_io(PADAPTER, u8 *pbuf, u16 length);

#ifdef CONFIG_BT_COEXIST
void rtl8822b_download_BTCoex_AP_mode_rsvd_page(PADAPTER);
#endif /* CONFIG_BT_COEXIST */

/* rtl8822b_phy.c */
u8 rtl8822b_phy_init_mac_register(PADAPTER);
u8 rtl8822b_phy_init(PADAPTER);
void rtl8822b_phy_init_dm_priv(PADAPTER);
void rtl8822b_phy_deinit_dm_priv(PADAPTER);
void rtl8822b_phy_init_haldm(PADAPTER);
void rtl8822b_phy_haldm_watchdog(PADAPTER);
void rtl8822b_phy_haldm_in_lps(PADAPTER);
void rtl8822b_phy_haldm_watchdog_in_lps(PADAPTER);
u32 rtl8822b_read_bb_reg(PADAPTER, u32 addr, u32 mask);
void rtl8822b_write_bb_reg(PADAPTER, u32 addr, u32 mask, u32 val);
u32 rtl8822b_read_rf_reg(PADAPTER, u8 path, u32 addr, u32 mask);
void rtl8822b_write_rf_reg(PADAPTER, u8 path, u32 addr, u32 mask, u32 val);
void rtl8822b_set_channel_bw(PADAPTER, u8 center_ch, CHANNEL_WIDTH, u8 offset40, u8 offset80);
void rtl8822b_set_channel(PADAPTER, u8 center_ch);
void rtl8822b_set_bw_mode(PADAPTER, CHANNEL_WIDTH, u8 offset);
void rtl8822b_set_tx_power_level(PADAPTER, u8 channel);
void rtl8822b_get_tx_power_level(PADAPTER, s32 *power);
void rtl8822b_set_tx_power_index(PADAPTER, u32 powerindex, u8 rfpath, u8 rate);
u8 rtl8822b_get_tx_power_index(PADAPTER, u8 rfpath, u8 rate, u8 bandwidth, u8 channel);
void rtl8822b_notch_filter_switch(PADAPTER, bool enable);
#ifdef CONFIG_BEAMFORMING
void rtl8822b_phy_init_beamforming(PADAPTER);
void rtl8822b_phy_sounding_enter(PADAPTER, struct sta_info*);
void rtl8822b_phy_sounding_leave(PADAPTER, u8 *addr);
void rtl8822b_phy_sounding_set_gid_table(PADAPTER, struct beamformer_entry*);
#endif /* CONFIG_BEAMFORMING */
s32 c2h_id_filter_ccx_8822b(u8 *buf);

#endif /* _RTL8822B_H_ */
