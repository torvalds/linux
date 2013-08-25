/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
#ifndef _RTW_MP_IOCTL_H_
#define _RTW_MP_IOCTL_H_

#include <drv_types.h>
#include <mp_custom_oid.h>
#include <rtw_ioctl.h>
#include <rtw_ioctl_rtl.h>
#include <rtw_efuse.h>
#include <rtw_mp.h>

/*  */
struct cfg_dbg_msg_struct {
	u32 DebugLevel;
	u32 DebugComponent_H32;
	u32 DebugComponent_L32;
};

struct mp_rw_reg {
	u32 offset;
	u32 width;
	u32 value;
};

struct efuse_access_struct {
	u16	start_addr;
	u16	cnts;
	u8	data[0];
};

struct burst_rw_reg {
	u32 offset;
	u32 len;
	u8 Data[256];
};

struct usb_vendor_req {
	u8	bRequest;
	u16	wValue;
	u16	wIndex;
	u16	wLength;
	u8	u8Dir;/* 0:OUT, 1:IN */
	u8	u8InData;
};

struct dr_variable_struct {
	u8 offset;
	u32 variable;
};

#define _irqlevel_changed_(a, b)

/* rtl8188eu_oid_rtl_seg_81_80_00 */
int rtl8188eu_oid_rt_pro_set_data_rate_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_start_test_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_stop_test_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_set_channel_direct_call_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_set_antenna_bb_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_set_tx_power_control_hdl(struct oid_par_priv *poid_par_priv);

/* rtl8188eu_oid_rtl_seg_81_80_20 */
int rtl8188eu_oid_rt_pro_query_tx_packet_sent_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_query_rx_packet_received_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_query_rx_packet_crc32_error_hdl(struct oid_par_priv *par_priv);
int rtl8188eu_oid_rt_pro_reset_tx_packet_sent_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_reset_rx_packet_received_hdl(struct oid_par_priv *par_priv);
int rtl8188eu_oid_rt_pro_set_modulation_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_set_continuous_tx_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_set_single_carrier_tx_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_set_carrier_suppression_tx_hdl(struct oid_par_priv *par_priv);
int rtl8188eu_oid_rt_pro_set_single_tone_tx_hdl(struct oid_par_priv *poid_par_priv);

/* rtl8188eu_oid_rtl_seg_81_87 */
int rtl8188eu_oid_rt_pro_write_bb_reg_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_read_bb_reg_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_write_rf_reg_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_read_rf_reg_hdl(struct oid_par_priv *poid_par_priv);

/* rtl8188eu_oid_rtl_seg_81_85 */
int rtl8188eu_oid_rt_wireless_mode_hdl(struct oid_par_priv *poid_par_priv);

/*  rtl8188eu_oid_rtl_seg_87_11_00 */
int rtl8188eu_oid_rt_pro8711_join_bss_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_read_register_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_write_register_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_burst_read_register_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_burst_write_register_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_write_txcmd_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_read16_eeprom_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_write16_eeprom_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro8711_wi_poll_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro8711_pkt_loss_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_rd_attrib_mem_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_wr_attrib_mem_hdl (struct oid_par_priv *poid_par_priv);
int  rtl8188eu_oid_rt_pro_set_rf_intfs_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_poll_rx_status_hdl(struct oid_par_priv *poid_par_priv);
/*  rtl8188eu_oid_rtl_seg_87_11_20 */
int rtl8188eu_oid_rt_pro_cfg_debug_message_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_set_data_rate_ex_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_set_basic_rate_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_read_tssi_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_set_power_tracking_hdl(struct oid_par_priv *poid_par_priv);
/* rtl8188eu_oid_rtl_seg_87_11_50 */
int rtl8188eu_oid_rt_pro_qry_pwrstate_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_set_pwrstate_hdl(struct oid_par_priv *poid_par_priv);
/* rtl8188eu_oid_rtl_seg_87_11_F0 */
int rtl8188eu_oid_rt_pro_h2c_set_rate_table_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_h2c_get_rate_table_hdl(struct oid_par_priv *poid_par_priv);

/* rtl8188eu_oid_rtl_seg_87_12_00 */
int rtl8188eu_oid_rt_pro_encryption_ctrl_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_add_sta_info_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_dele_sta_info_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_query_dr_variable_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_rx_packet_type_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_read_efuse_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_write_efuse_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_rw_efuse_pgpkt_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_get_efuse_current_size_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_efuse_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_efuse_map_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_set_bandwidth_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_set_crystal_cap_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_set_rx_packet_type_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_get_efuse_max_size_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_set_tx_agc_offset_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_set_pkt_test_mode_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_get_thermal_meter_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_reset_phy_rx_packet_count_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_get_phy_rx_packet_received_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_get_phy_rx_packet_crc32_error_hdl(struct oid_par_priv *par_priv);
int rtl8188eu_oid_rt_set_power_down_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_get_power_mode_hdl(struct oid_par_priv *poid_par_priv);
int rtl8188eu_oid_rt_pro_trigger_gpio_hdl(struct oid_par_priv *poid_par_priv);

#ifdef _RTW_MP_IOCTL_C_

static const struct oid_obj_priv rtl8188eu_oid_rtl_seg_81_80_00[] = {
	{1, &oid_null_function},		/* 0x00	OID_RT_PRO_RESET_DUT */
	{1, &rtl8188eu_oid_rt_pro_set_data_rate_hdl},	/* 0x01 */
	{1, &rtl8188eu_oid_rt_pro_start_test_hdl},	/* 0x02 */
	{1, &rtl8188eu_oid_rt_pro_stop_test_hdl},		/* 0x03 */
	{1, &oid_null_function},	/* 0x04	OID_RT_PRO_SET_PREAMBLE */
	{1, &oid_null_function},	/* 0x05	OID_RT_PRO_SET_SCRAMBLER */
	{1, &oid_null_function},	/* 0x06	OID_RT_PRO_SET_FILTER_BB */
	{1, &oid_null_function},/* 0x07	OID_RT_PRO_SET_MANUAL_DIVERSITY_BB */
	{1, &rtl8188eu_oid_rt_pro_set_channel_direct_call_hdl},	/* 0x08 */
	{1, &oid_null_function},/* 0x09	OID_RT_PRO_SET_SLEEP_MODE_DIRECT_CALL */
	{1, &oid_null_function},/* 0x0A	OID_RT_PRO_SET_WAKE_MODE_DIRECT_CALL */
	{1, &rtl8188eu_oid_rt_pro_set_continuous_tx_hdl},	/* 0x0B	OID_RT_PRO_SET_TX_CONTINUOUS_DIRECT_CALL */
	{1, &rtl8188eu_oid_rt_pro_set_single_carrier_tx_hdl},/* 0x0C	OID_RT_PRO_SET_SINGLE_CARRIER_TX_CONTINUOUS */
	{1, &oid_null_function},	/* 0x0D	OID_RT_PRO_SET_TX_ANTENNA_BB */
	{1, &rtl8188eu_oid_rt_pro_set_antenna_bb_hdl},	/* 0x0E */
	{1, &oid_null_function},		/* 0x0F	OID_RT_PRO_SET_CR_SCRAMBLER */
	{1, &oid_null_function},		/* 0x10	OID_RT_PRO_SET_CR_NEW_FILTER */
	{1, &rtl8188eu_oid_rt_pro_set_tx_power_control_hdl},/* 0x11 OID_RT_PRO_SET_TX_POWER_CONTROL */
	{1, &oid_null_function},	/* 0x12	OID_RT_PRO_SET_CR_TX_CONFIG */
	{1, &oid_null_function},	/* 0x13	OID_RT_PRO_GET_TX_POWER_CONTROL */
	{1, &oid_null_function},	/* 0x14	OID_RT_PRO_GET_CR_SIGNAL_QUALITY */
	{1, &oid_null_function},	/* 0x15	OID_RT_PRO_SET_CR_SETPOINT */
	{1, &oid_null_function},	/* 0x16	OID_RT_PRO_SET_INTEGRATOR */
	{1, &oid_null_function},	/* 0x17	OID_RT_PRO_SET_SIGNAL_QUALITY */
	{1, &oid_null_function},	/* 0x18	OID_RT_PRO_GET_INTEGRATOR */
	{1, &oid_null_function},	/* 0x19	OID_RT_PRO_GET_SIGNAL_QUALITY */
	{1, &oid_null_function},	/* 0x1A	OID_RT_PRO_QUERY_EEPROM_TYPE */
	{1, &oid_null_function},	/* 0x1B	OID_RT_PRO_WRITE_MAC_ADDRESS */
	{1, &oid_null_function},	/* 0x1C	OID_RT_PRO_READ_MAC_ADDRESS */
	{1, &oid_null_function},	/* 0x1D	OID_RT_PRO_WRITE_CIS_DATA */
	{1, &oid_null_function},	/* 0x1E	OID_RT_PRO_READ_CIS_DATA */
	{1, &oid_null_function}		/* 0x1F	OID_RT_PRO_WRITE_POWER_CONTROL */
};

static const struct oid_obj_priv rtl8188eu_oid_rtl_seg_81_80_20[] = {
	{1, &oid_null_function},	/* 0x20	OID_RT_PRO_READ_POWER_CONTROL */
	{1, &oid_null_function},	/* 0x21	OID_RT_PRO_WRITE_EEPROM */
	{1, &oid_null_function},	/* 0x22	OID_RT_PRO_READ_EEPROM */
	{1, &rtl8188eu_oid_rt_pro_reset_tx_packet_sent_hdl},	/* 0x23 */
	{1, &rtl8188eu_oid_rt_pro_query_tx_packet_sent_hdl},	/* 0x24 */
	{1, &rtl8188eu_oid_rt_pro_reset_rx_packet_received_hdl},	/* 0x25 */
	{1, &rtl8188eu_oid_rt_pro_query_rx_packet_received_hdl},	/* 0x26 */
	{1, &rtl8188eu_oid_rt_pro_query_rx_packet_crc32_error_hdl},	/* 0x27 */
	{1, &oid_null_function},	/* 0x28	OID_RT_PRO_QUERY_CURRENT_ADDRESS */
	{1, &oid_null_function},	/* 0x29	OID_RT_PRO_QUERY_PERMANENT_ADDRESS */
	{1, &oid_null_function},	/* 0x2A	OID_RT_PRO_SET_PHILIPS_RF_PARAMETERS */
	{1, &rtl8188eu_oid_rt_pro_set_carrier_suppression_tx_hdl},/* 0x2B	OID_RT_PRO_SET_CARRIER_SUPPRESSION_TX */
	{1, &oid_null_function},	/* 0x2C	OID_RT_PRO_RECEIVE_PACKET */
	{1, &oid_null_function},	/* 0x2D	OID_RT_PRO_WRITE_EEPROM_BYTE */
	{1, &oid_null_function},	/* 0x2E	OID_RT_PRO_READ_EEPROM_BYTE */
	{1, &rtl8188eu_oid_rt_pro_set_modulation_hdl}		/* 0x2F */
};

static const struct oid_obj_priv rtl8188eu_oid_rtl_seg_81_80_40[] = {
	{1, &oid_null_function},			/* 0x40 */
	{1, &oid_null_function},			/* 0x41 */
	{1, &oid_null_function},			/* 0x42 */
	{1, &rtl8188eu_oid_rt_pro_set_single_tone_tx_hdl},	/* 0x43 */
	{1, &oid_null_function},			/* 0x44 */
	{1, &oid_null_function}				/* 0x45 */
};

static const struct oid_obj_priv rtl8188eu_oid_rtl_seg_81_80_80[] = {
	{1, &oid_null_function},		/* 0x80	OID_RT_DRIVER_OPTION */
	{1, &oid_null_function},		/* 0x81	OID_RT_RF_OFF */
	{1, &oid_null_function}			/* 0x82	OID_RT_AUTH_STATUS */
};

static const struct oid_obj_priv rtl8188eu_oid_rtl_seg_81_85[] = {
	{1, &rtl8188eu_oid_rt_wireless_mode_hdl}		/* 0x00	OID_RT_WIRELESS_MODE */
};

#endif /* _RTL871X_MP_IOCTL_C_ */

struct rwreg_param {
	u32 offset;
	u32 width;
	u32 value;
};

struct bbreg_param {
	u32 offset;
	u32 phymask;
	u32 value;
};

struct txpower_param {
	u32 pwr_index;
};

struct datarate_param {
	u32 rate_index;
};

struct rfintfs_parm {
	u32 rfintfs;
};

struct mp_xmit_parm {
	u8 enable;
	u32 count;
	u16 length;
	u8 payload_type;
	u8 da[ETH_ALEN];
};

struct mp_xmit_packet {
	u32 len;
	u32 mem[MAX_MP_XMITBUF_SZ >> 2];
};

struct psmode_param {
	u32 ps_mode;
	u32 smart_ps;
};

/* for OID_RT_PRO_READ16_EEPROM & OID_RT_PRO_WRITE16_EEPROM */
struct eeprom_rw_param {
	u32 offset;
	u16 value;
};

struct mp_ioctl_handler {
	u32 paramsize;
	s32 (*handler)(struct oid_par_priv* poid_par_priv);
	u32 oid;
};

struct mp_ioctl_param{
	u32 subcode;
	u32 len;
	u8 data[0];
};

#define GEN_MP_IOCTL_SUBCODE(code) _MP_IOCTL_ ## code ## _CMD_

enum RTL871X_MP_IOCTL_SUBCODE {
	GEN_MP_IOCTL_SUBCODE(MP_START),			/*0*/
	GEN_MP_IOCTL_SUBCODE(MP_STOP),
	GEN_MP_IOCTL_SUBCODE(READ_REG),
	GEN_MP_IOCTL_SUBCODE(WRITE_REG),
	GEN_MP_IOCTL_SUBCODE(READ_BB_REG),
	GEN_MP_IOCTL_SUBCODE(WRITE_BB_REG),		/*5*/
	GEN_MP_IOCTL_SUBCODE(READ_RF_REG),
	GEN_MP_IOCTL_SUBCODE(WRITE_RF_REG),
	GEN_MP_IOCTL_SUBCODE(SET_CHANNEL),
	GEN_MP_IOCTL_SUBCODE(SET_TXPOWER),
	GEN_MP_IOCTL_SUBCODE(SET_DATARATE),		/*10*/
	GEN_MP_IOCTL_SUBCODE(SET_BANDWIDTH),
	GEN_MP_IOCTL_SUBCODE(SET_ANTENNA),
	GEN_MP_IOCTL_SUBCODE(CNTU_TX),
	GEN_MP_IOCTL_SUBCODE(SC_TX),
	GEN_MP_IOCTL_SUBCODE(CS_TX),			/*15*/
	GEN_MP_IOCTL_SUBCODE(ST_TX),
	GEN_MP_IOCTL_SUBCODE(IOCTL_XMIT_PACKET),
	GEN_MP_IOCTL_SUBCODE(SET_RX_PKT_TYPE),
	GEN_MP_IOCTL_SUBCODE(RESET_PHY_RX_PKT_CNT),
	GEN_MP_IOCTL_SUBCODE(GET_PHY_RX_PKT_RECV),	/*20*/
	GEN_MP_IOCTL_SUBCODE(GET_PHY_RX_PKT_ERROR),
	GEN_MP_IOCTL_SUBCODE(READ16_EEPROM),
	GEN_MP_IOCTL_SUBCODE(WRITE16_EEPROM),
	GEN_MP_IOCTL_SUBCODE(EFUSE),
	GEN_MP_IOCTL_SUBCODE(EFUSE_MAP),		/*25*/
	GEN_MP_IOCTL_SUBCODE(GET_EFUSE_MAX_SIZE),
	GEN_MP_IOCTL_SUBCODE(GET_EFUSE_CURRENT_SIZE),
	GEN_MP_IOCTL_SUBCODE(GET_THERMAL_METER),
	GEN_MP_IOCTL_SUBCODE(SET_PTM),
	GEN_MP_IOCTL_SUBCODE(SET_POWER_DOWN),		/*30*/
	GEN_MP_IOCTL_SUBCODE(TRIGGER_GPIO),
	GEN_MP_IOCTL_SUBCODE(SET_DM_BT),		/*35*/
	GEN_MP_IOCTL_SUBCODE(DEL_BA),			/*36*/
	GEN_MP_IOCTL_SUBCODE(GET_WIFI_STATUS),	/*37*/
	MAX_MP_IOCTL_SUBCODE,
};

s32 rtl8188eu_mp_ioctl_xmit_packet_hdl(struct oid_par_priv *poid_par_priv);

#define GEN_HANDLER(sz, hdl, oid) {sz, hdl, oid},

#define EXT_MP_IOCTL_HANDLER(sz, subcode, oid)			\
	 {sz, rtl8188eu_mp_ioctl_##subcode##_hdl, oid},


#endif
