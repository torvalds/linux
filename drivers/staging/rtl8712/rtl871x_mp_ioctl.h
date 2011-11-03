/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/
#ifndef _RTL871X_MP_IOCTL_H
#define _RTL871X_MP_IOCTL_H

#include "osdep_service.h"
#include "drv_types.h"
#include "mp_custom_oid.h"
#include "rtl871x_ioctl.h"
#include "rtl871x_ioctl_rtl.h"
#include "rtl8712_efuse.h"

#define TESTFWCMDNUMBER			1000000
#define TEST_H2CINT_WAIT_TIME		500
#define TEST_C2HINT_WAIT_TIME		500
#define HCI_TEST_SYSCFG_HWMASK		1
#define _BUSCLK_40M			(4 << 2)

struct CFG_DBG_MSG_STRUCT {
	u32 DebugLevel;
	u32 DebugComponent_H32;
	u32 DebugComponent_L32;
};

struct mp_rw_reg {
	uint offset;
	uint width;
	u32 value;
};

/* for OID_RT_PRO_READ16_EEPROM & OID_RT_PRO_WRITE16_EEPROM */
struct eeprom_rw_param {
	uint offset;
	u16 value;
};

struct EFUSE_ACCESS_STRUCT {
	u16	start_addr;
	u16	cnts;
	u8	data[0];
};

struct burst_rw_reg {
	uint offset;
	uint len;
	u8 Data[256];
};

struct usb_vendor_req {
	u8	bRequest;
	u16	wValue;
	u16	wIndex;
	u16	wLength;
	u8	u8Dir;/*0:OUT, 1:IN */
	u8	u8InData;
};

struct DR_VARIABLE_STRUCT {
	u8 offset;
	u32 variable;
};

int mp_start_joinbss(struct _adapter *padapter, struct ndis_802_11_ssid *pssid);

/* oid_rtl_seg_87_11_00 */
uint oid_rt_pro8711_join_bss_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_read_register_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_write_register_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_burst_read_register_hdl(struct oid_par_priv*
					       poid_par_priv);
uint oid_rt_pro_burst_write_register_hdl(struct oid_par_priv*
						poid_par_priv);
uint oid_rt_pro_write_txcmd_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_read16_eeprom_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_write16_eeprom_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_pro8711_wi_poll_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_pro8711_pkt_loss_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_rd_attrib_mem_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_wr_attrib_mem_hdl(struct oid_par_priv *poid_par_priv);
uint  oid_rt_pro_set_rf_intfs_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_poll_rx_status_hdl(struct oid_par_priv *poid_par_priv);
/* oid_rtl_seg_87_11_20 */
uint oid_rt_pro_cfg_debug_message_hdl(
				struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_set_data_rate_ex_hdl(
				struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_set_basic_rate_hdl(
				struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_set_power_tracking_hdl(
				struct oid_par_priv *poid_par_priv);
/* oid_rtl_seg_87_11_50 */
uint oid_rt_pro_qry_pwrstate_hdl(
				struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_set_pwrstate_hdl(
				struct oid_par_priv *poid_par_priv);
/* oid_rtl_seg_87_11_F0 */
uint oid_rt_pro_h2c_set_rate_table_hdl(
				struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_h2c_get_rate_table_hdl(
				struct oid_par_priv *poid_par_priv);
/* oid_rtl_seg_81_80_00 */
uint oid_rt_pro_set_data_rate_hdl(
				struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_start_test_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_stop_test_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_set_channel_direct_call_hdl(
				struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_set_antenna_bb_hdl(
				struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_set_tx_power_control_hdl(
				struct oid_par_priv *poid_par_priv);
/* oid_rtl_seg_81_80_20 */
uint oid_rt_pro_query_tx_packet_sent_hdl(
				struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_query_rx_packet_received_hdl(
				struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_query_rx_packet_crc32_error_hdl(
				struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_reset_tx_packet_sent_hdl(
				struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_reset_rx_packet_received_hdl(
				struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_set_modulation_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_set_continuous_tx_hdl(
				struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_set_single_carrier_tx_hdl(
				struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_set_carrier_suppression_tx_hdl(
				struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_set_single_tone_tx_hdl(
				struct oid_par_priv *poid_par_priv);
/* oid_rtl_seg_81_87 */
uint oid_rt_pro_write_bb_reg_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_read_bb_reg_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_write_rf_reg_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_read_rf_reg_hdl(struct oid_par_priv *poid_par_priv);
/* oid_rtl_seg_81_85 */
uint oid_rt_wireless_mode_hdl(struct oid_par_priv *poid_par_priv);
/* oid_rtl_seg_87_12_00 */
uint oid_rt_pro_encryption_ctrl_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_add_sta_info_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_dele_sta_info_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_query_dr_variable_hdl(
				struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_rx_packet_type_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_read_efuse_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_write_efuse_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_rw_efuse_pgpkt_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_get_efuse_current_size_hdl(
				struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_efuse_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_efuse_map_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_set_bandwidth_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_set_crystal_cap_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_set_rx_packet_type_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_get_efuse_max_size_hdl(struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_set_tx_agc_offset_hdl(
				struct oid_par_priv *poid_par_priv);
uint oid_rt_pro_set_pkt_test_mode_hdl(
				struct oid_par_priv *poid_par_priv);
uint oid_rt_get_thermal_meter_hdl(
				struct oid_par_priv *poid_par_priv);
uint oid_rt_reset_phy_rx_packet_count_hdl(
				struct oid_par_priv *poid_par_priv);
uint oid_rt_get_phy_rx_packet_received_hdl(
				struct oid_par_priv *poid_par_priv);
uint oid_rt_get_phy_rx_packet_crc32_error_hdl(
				struct oid_par_priv *poid_par_priv);
uint oid_rt_set_power_down_hdl(
				struct oid_par_priv *poid_par_priv);
uint oid_rt_get_power_mode_hdl(
				struct oid_par_priv *poid_par_priv);
#ifdef _RTL871X_MP_IOCTL_C_ /* CAUTION!!! */
/* This ifdef _MUST_ be left in!! */
static const struct oid_obj_priv oid_rtl_seg_81_80_00[] = {
	{1, &oid_null_function},	/*0x00	OID_RT_PRO_RESET_DUT */
	{1, &oid_rt_pro_set_data_rate_hdl},	/*0x01*/
	{1, &oid_rt_pro_start_test_hdl},/*0x02*/
	{1, &oid_rt_pro_stop_test_hdl},	/*0x03*/
	{1, &oid_null_function},	/*0x04	OID_RT_PRO_SET_PREAMBLE*/
	{1, &oid_null_function},	/*0x05	OID_RT_PRO_SET_SCRAMBLER*/
	{1, &oid_null_function},	/*0x06	OID_RT_PRO_SET_FILTER_BB*/
	{1, &oid_null_function},	/*0x07
					 * OID_RT_PRO_SET_MANUAL_DIVERS_BB*/
	{1, &oid_rt_pro_set_channel_direct_call_hdl},	/*0x08*/
	{1, &oid_null_function},	/*0x09
				* OID_RT_PRO_SET_SLEEP_MODE_DIRECT_CALL*/
	{1, &oid_null_function},	/*0x0A
				* OID_RT_PRO_SET_WAKE_MODE_DIRECT_CALL*/
	{1, &oid_rt_pro_set_continuous_tx_hdl},	/*0x0B
				* OID_RT_PRO_SET_TX_CONTINUOUS_DIRECT_CALL*/
	{1, &oid_rt_pro_set_single_carrier_tx_hdl}, /*0x0C
				* OID_RT_PRO_SET_SINGLE_CARRIER_TX_CONTINUOUS*/
	{1, &oid_null_function},	/*0x0D
				* OID_RT_PRO_SET_TX_ANTENNA_BB*/
	{1, &oid_rt_pro_set_antenna_bb_hdl},		/*0x0E*/
	{1, &oid_null_function},	/*0x0F	OID_RT_PRO_SET_CR_SCRAMBLER*/
	{1, &oid_null_function},	/*0x10	OID_RT_PRO_SET_CR_NEW_FILTER*/
	{1, &oid_rt_pro_set_tx_power_control_hdl}, /*0x11
				* OID_RT_PRO_SET_TX_POWER_CONTROL*/
	{1, &oid_null_function},	/*0x12	OID_RT_PRO_SET_CR_TX_CONFIG*/
	{1, &oid_null_function},	/*0x13
					 * OID_RT_PRO_GET_TX_POWER_CONTROL*/
	{1, &oid_null_function},	/*0x14
					 * OID_RT_PRO_GET_CR_SIGNAL_QUALITY*/
	{1, &oid_null_function},	/*0x15	OID_RT_PRO_SET_CR_SETPOINT*/
	{1, &oid_null_function},	/*0x16	OID_RT_PRO_SET_INTEGRATOR*/
	{1, &oid_null_function},	/*0x17	OID_RT_PRO_SET_SIGNAL_QUALITY*/
	{1, &oid_null_function},	/*0x18	OID_RT_PRO_GET_INTEGRATOR*/
	{1, &oid_null_function},	/*0x19	OID_RT_PRO_GET_SIGNAL_QUALITY*/
	{1, &oid_null_function},	/*0x1A	OID_RT_PRO_QUERY_EEPROM_TYPE*/
	{1, &oid_null_function},	/*0x1B	OID_RT_PRO_WRITE_MAC_ADDRESS*/
	{1, &oid_null_function},	/*0x1C	OID_RT_PRO_READ_MAC_ADDRESS*/
	{1, &oid_null_function},	/*0x1D	OID_RT_PRO_WRITE_CIS_DATA*/
	{1, &oid_null_function},	/*0x1E	OID_RT_PRO_READ_CIS_DATA*/
	{1, &oid_null_function}		/*0x1F	OID_RT_PRO_WRITE_POWER_CONTROL*/
};

static const struct oid_obj_priv oid_rtl_seg_81_80_20[] = {
	{1, &oid_null_function},	/*0x20	OID_RT_PRO_READ_POWER_CONTROL*/
	{1, &oid_null_function},	/*0x21	OID_RT_PRO_WRITE_EEPROM*/
	{1, &oid_null_function},	/*0x22	OID_RT_PRO_READ_EEPROM*/
	{1, &oid_rt_pro_reset_tx_packet_sent_hdl},	/*0x23*/
	{1, &oid_rt_pro_query_tx_packet_sent_hdl},	/*0x24*/
	{1, &oid_rt_pro_reset_rx_packet_received_hdl},	/*0x25*/
	{1, &oid_rt_pro_query_rx_packet_received_hdl},	/*0x26*/
	{1, &oid_rt_pro_query_rx_packet_crc32_error_hdl},/*0x27*/
	{1, &oid_null_function},	/*0x28
					 *OID_RT_PRO_QUERY_CURRENT_ADDRESS*/
	{1, &oid_null_function},	/*0x29
					 *OID_RT_PRO_QUERY_PERMANENT_ADDRESS*/
	{1, &oid_null_function},	/*0x2A
				 *OID_RT_PRO_SET_PHILIPS_RF_PARAMETERS*/
	{1, &oid_rt_pro_set_carrier_suppression_tx_hdl},/*0x2B
				 *OID_RT_PRO_SET_CARRIER_SUPPRESSION_TX*/
	{1, &oid_null_function},	/*0x2C	OID_RT_PRO_RECEIVE_PACKET*/
	{1, &oid_null_function},	/*0x2D	OID_RT_PRO_WRITE_EEPROM_BYTE*/
	{1, &oid_null_function},	/*0x2E	OID_RT_PRO_READ_EEPROM_BYTE*/
	{1, &oid_rt_pro_set_modulation_hdl}		/*0x2F*/
};

static const struct oid_obj_priv oid_rtl_seg_81_80_40[] = {
	{1, &oid_null_function},			/*0x40*/
	{1, &oid_null_function},			/*0x41*/
	{1, &oid_null_function},			/*0x42*/
	{1, &oid_rt_pro_set_single_tone_tx_hdl},	/*0x43*/
	{1, &oid_null_function},			/*0x44*/
	{1, &oid_null_function}				/*0x45*/
};

static const struct oid_obj_priv oid_rtl_seg_81_80_80[] = {
	{1, &oid_null_function},	/*0x80	OID_RT_DRIVER_OPTION*/
	{1, &oid_null_function},	/*0x81	OID_RT_RF_OFF*/
	{1, &oid_null_function}		/*0x82	OID_RT_AUTH_STATUS*/

};

static const struct oid_obj_priv oid_rtl_seg_81_85[] = {
	{1, &oid_rt_wireless_mode_hdl}	/*0x00	OID_RT_WIRELESS_MODE*/
};

#else /* _RTL871X_MP_IOCTL_C_ */
extern struct oid_obj_priv oid_rtl_seg_81_80_00[32];
extern struct oid_obj_priv oid_rtl_seg_81_80_20[16];
extern struct oid_obj_priv oid_rtl_seg_81_80_40[6];
extern struct oid_obj_priv oid_rtl_seg_81_80_80[3];
extern struct oid_obj_priv oid_rtl_seg_81_85[1];
extern struct oid_obj_priv oid_rtl_seg_81_87[5];
extern struct oid_obj_priv oid_rtl_seg_87_11_00[32];
extern struct oid_obj_priv oid_rtl_seg_87_11_20[5];
extern struct oid_obj_priv oid_rtl_seg_87_11_50[2];
extern struct oid_obj_priv oid_rtl_seg_87_11_80[1];
extern struct oid_obj_priv oid_rtl_seg_87_11_B0[1];
extern struct oid_obj_priv oid_rtl_seg_87_11_F0[16];
extern struct oid_obj_priv oid_rtl_seg_87_12_00[32];

#endif /* _RTL871X_MP_IOCTL_C_ */


enum MP_MODE {
	MP_START_MODE,
	MP_STOP_MODE,
	MP_ERR_MODE
};

struct rwreg_param {
	unsigned int offset;
	unsigned int width;
	unsigned int value;
};

struct bbreg_param {
	unsigned int offset;
	unsigned int phymask;
	unsigned int value;
};

struct txpower_param {
	unsigned int pwr_index;
};

struct datarate_param {
	unsigned int rate_index;
};

struct rfintfs_parm {
	unsigned int rfintfs;
};

struct mp_xmit_packet {
	unsigned int len;
};

struct psmode_param {
	unsigned int ps_mode;
	unsigned int smart_ps;
};

struct mp_ioctl_handler {
	unsigned int paramsize;
	unsigned int (*handler)(struct oid_par_priv *poid_par_priv);
	unsigned int oid;
};

struct mp_ioctl_param {
	unsigned int subcode;
	unsigned int len;
	unsigned char data[0];
};

#define GEN_MP_IOCTL_SUBCODE(code) _MP_IOCTL_ ## code ## _CMD_

enum RTL871X_MP_IOCTL_SUBCODE {
	GEN_MP_IOCTL_SUBCODE(MP_START),			/*0*/
	GEN_MP_IOCTL_SUBCODE(MP_STOP),			/*1*/
	GEN_MP_IOCTL_SUBCODE(READ_REG),			/*2*/
	GEN_MP_IOCTL_SUBCODE(WRITE_REG),
	GEN_MP_IOCTL_SUBCODE(SET_CHANNEL),		/*4*/
	GEN_MP_IOCTL_SUBCODE(SET_TXPOWER),		/*5*/
	GEN_MP_IOCTL_SUBCODE(SET_DATARATE),		/*6*/
	GEN_MP_IOCTL_SUBCODE(READ_BB_REG),		/*7*/
	GEN_MP_IOCTL_SUBCODE(WRITE_BB_REG),
	GEN_MP_IOCTL_SUBCODE(READ_RF_REG),		/*9*/
	GEN_MP_IOCTL_SUBCODE(WRITE_RF_REG),
	GEN_MP_IOCTL_SUBCODE(SET_RF_INTFS),
	GEN_MP_IOCTL_SUBCODE(IOCTL_XMIT_PACKET),	/*12*/
	GEN_MP_IOCTL_SUBCODE(PS_STATE),			/*13*/
	GEN_MP_IOCTL_SUBCODE(READ16_EEPROM),		/*14*/
	GEN_MP_IOCTL_SUBCODE(WRITE16_EEPROM),		/*15*/
	GEN_MP_IOCTL_SUBCODE(SET_PTM),			/*16*/
	GEN_MP_IOCTL_SUBCODE(READ_TSSI),		/*17*/
	GEN_MP_IOCTL_SUBCODE(CNTU_TX),			/*18*/
	GEN_MP_IOCTL_SUBCODE(SET_BANDWIDTH),		/*19*/
	GEN_MP_IOCTL_SUBCODE(SET_RX_PKT_TYPE),		/*20*/
	GEN_MP_IOCTL_SUBCODE(RESET_PHY_RX_PKT_CNT),	/*21*/
	GEN_MP_IOCTL_SUBCODE(GET_PHY_RX_PKT_RECV),	/*22*/
	GEN_MP_IOCTL_SUBCODE(GET_PHY_RX_PKT_ERROR),	/*23*/
	GEN_MP_IOCTL_SUBCODE(SET_POWER_DOWN),		/*24*/
	GEN_MP_IOCTL_SUBCODE(GET_THERMAL_METER),	/*25*/
	GEN_MP_IOCTL_SUBCODE(GET_POWER_MODE),		/*26*/
	GEN_MP_IOCTL_SUBCODE(EFUSE),			/*27*/
	GEN_MP_IOCTL_SUBCODE(EFUSE_MAP),		/*28*/
	GEN_MP_IOCTL_SUBCODE(GET_EFUSE_MAX_SIZE),	/*29*/
	GEN_MP_IOCTL_SUBCODE(GET_EFUSE_CURRENT_SIZE),	/*30*/
	GEN_MP_IOCTL_SUBCODE(SC_TX),			/*31*/
	GEN_MP_IOCTL_SUBCODE(CS_TX),			/*32*/
	GEN_MP_IOCTL_SUBCODE(ST_TX),			/*33*/
	GEN_MP_IOCTL_SUBCODE(SET_ANTENNA),		/*34*/
	MAX_MP_IOCTL_SUBCODE,
};

unsigned int mp_ioctl_xmit_packet_hdl(struct oid_par_priv *poid_par_priv);

#ifdef _RTL871X_MP_IOCTL_C_ /* CAUTION!!! */
/* This ifdef _MUST_ be left in!! */

static struct mp_ioctl_handler mp_ioctl_hdl[] = {
	{sizeof(u32), oid_rt_pro_start_test_hdl,
			     OID_RT_PRO_START_TEST},/*0*/
	{sizeof(u32), oid_rt_pro_stop_test_hdl,
			     OID_RT_PRO_STOP_TEST},/*1*/
	{sizeof(struct rwreg_param),
			     oid_rt_pro_read_register_hdl,
			     OID_RT_PRO_READ_REGISTER},/*2*/
	{sizeof(struct rwreg_param),
			     oid_rt_pro_write_register_hdl,
			     OID_RT_PRO_WRITE_REGISTER},
	{sizeof(u32),
			     oid_rt_pro_set_channel_direct_call_hdl,
			     OID_RT_PRO_SET_CHANNEL_DIRECT_CALL},
	{sizeof(struct txpower_param),
			     oid_rt_pro_set_tx_power_control_hdl,
			     OID_RT_PRO_SET_TX_POWER_CONTROL},
	{sizeof(u32),
			     oid_rt_pro_set_data_rate_hdl,
			     OID_RT_PRO_SET_DATA_RATE},
	{sizeof(struct bb_reg_param),
			     oid_rt_pro_read_bb_reg_hdl,
			     OID_RT_PRO_READ_BB_REG},/*7*/
	{sizeof(struct bb_reg_param),
			     oid_rt_pro_write_bb_reg_hdl,
			     OID_RT_PRO_WRITE_BB_REG},
	{sizeof(struct rwreg_param),
			     oid_rt_pro_read_rf_reg_hdl,
			     OID_RT_PRO_RF_READ_REGISTRY},/*9*/
	{sizeof(struct rwreg_param),
			     oid_rt_pro_write_rf_reg_hdl,
			     OID_RT_PRO_RF_WRITE_REGISTRY},
	{sizeof(struct rfintfs_parm), NULL, 0},
	{0, &mp_ioctl_xmit_packet_hdl, 0},/*12*/
	{sizeof(struct psmode_param), NULL, 0},/*13*/
	{sizeof(struct eeprom_rw_param), NULL, 0},/*14*/
	{sizeof(struct eeprom_rw_param), NULL, 0},/*15*/
	{sizeof(unsigned char), NULL, 0},/*16*/
	{sizeof(u32), NULL, 0},/*17*/
	{sizeof(u32), oid_rt_pro_set_continuous_tx_hdl,
			     OID_RT_PRO_SET_CONTINUOUS_TX},/*18*/
	{sizeof(u32), oid_rt_set_bandwidth_hdl,
			     OID_RT_SET_BANDWIDTH},/*19*/
	{sizeof(u32), oid_rt_set_rx_packet_type_hdl,
			     OID_RT_SET_RX_PACKET_TYPE},/*20*/
	{0, oid_rt_reset_phy_rx_packet_count_hdl,
			     OID_RT_RESET_PHY_RX_PACKET_COUNT},/*21*/
	{sizeof(u32), oid_rt_get_phy_rx_packet_received_hdl,
			     OID_RT_GET_PHY_RX_PACKET_RECEIVED},/*22*/
	{sizeof(u32), oid_rt_get_phy_rx_packet_crc32_error_hdl,
			     OID_RT_GET_PHY_RX_PACKET_CRC32_ERROR},/*23*/
	{sizeof(unsigned char), oid_rt_set_power_down_hdl,
			     OID_RT_SET_POWER_DOWN},/*24*/
	{sizeof(u32), oid_rt_get_thermal_meter_hdl,
			     OID_RT_PRO_GET_THERMAL_METER},/*25*/
	{sizeof(u32), oid_rt_get_power_mode_hdl,
			     OID_RT_GET_POWER_MODE},/*26*/
	{sizeof(struct EFUSE_ACCESS_STRUCT),
			     oid_rt_pro_efuse_hdl, OID_RT_PRO_EFUSE},/*27*/
	{EFUSE_MAP_MAX_SIZE, oid_rt_pro_efuse_map_hdl,
			     OID_RT_PRO_EFUSE_MAP},/*28*/
	{sizeof(u32), oid_rt_get_efuse_max_size_hdl,
			     OID_RT_GET_EFUSE_MAX_SIZE},/*29*/
	{sizeof(u32), oid_rt_get_efuse_current_size_hdl,
			     OID_RT_GET_EFUSE_CURRENT_SIZE},/*30*/
	{sizeof(u32), oid_rt_pro_set_single_carrier_tx_hdl,
			     OID_RT_PRO_SET_SINGLE_CARRIER_TX},/*31*/
	{sizeof(u32), oid_rt_pro_set_carrier_suppression_tx_hdl,
			     OID_RT_PRO_SET_CARRIER_SUPPRESSION_TX},/*32*/
	{sizeof(u32), oid_rt_pro_set_single_tone_tx_hdl,
			     OID_RT_PRO_SET_SINGLE_TONE_TX},/*33*/
	{sizeof(u32), oid_rt_pro_set_antenna_bb_hdl,
			     OID_RT_PRO_SET_ANTENNA_BB},/*34*/
};

#else /* _RTL871X_MP_IOCTL_C_ */
extern struct mp_ioctl_handler mp_ioctl_hdl[];
#endif /* _RTL871X_MP_IOCTL_C_ */

#endif

