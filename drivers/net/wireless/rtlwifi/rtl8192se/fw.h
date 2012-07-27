/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef __REALTEK_FIRMWARE92S_H__
#define __REALTEK_FIRMWARE92S_H__

#define RTL8190_MAX_FIRMWARE_CODE_SIZE		64000
#define RTL8190_MAX_RAW_FIRMWARE_CODE_SIZE	90000
#define RTL8190_CPU_START_OFFSET		0x80
/* Firmware Local buffer size. 64k */
#define	MAX_FIRMWARE_CODE_SIZE			0xFF00

#define	RT_8192S_FIRMWARE_HDR_SIZE		80
#define RT_8192S_FIRMWARE_HDR_EXCLUDE_PRI_SIZE	32

/* support till 64 bit bus width OS */
#define MAX_DEV_ADDR_SIZE			8
#define MAX_FIRMWARE_INFORMATION_SIZE		32
#define MAX_802_11_HEADER_LENGTH		(40 + \
						MAX_FIRMWARE_INFORMATION_SIZE)
#define ENCRYPTION_MAX_OVERHEAD			128
#define MAX_FRAGMENT_COUNT			8
#define MAX_TRANSMIT_BUFFER_SIZE		(1600 + \
						(MAX_802_11_HEADER_LENGTH + \
						ENCRYPTION_MAX_OVERHEAD) *\
						MAX_FRAGMENT_COUNT)

#define H2C_TX_CMD_HDR_LEN			8

/* The following DM control code are for Reg0x364, */
#define	FW_DIG_ENABLE_CTL			BIT(0)
#define	FW_HIGH_PWR_ENABLE_CTL			BIT(1)
#define	FW_SS_CTL				BIT(2)
#define	FW_RA_INIT_CTL				BIT(3)
#define	FW_RA_BG_CTL				BIT(4)
#define	FW_RA_N_CTL				BIT(5)
#define	FW_PWR_TRK_CTL				BIT(6)
#define	FW_IQK_CTL				BIT(7)
#define	FW_FA_CTL				BIT(8)
#define	FW_DRIVER_CTRL_DM_CTL			BIT(9)
#define	FW_PAPE_CTL_BY_SW_HW			BIT(10)
#define	FW_DISABLE_ALL_DM			0
#define	FW_PWR_TRK_PARAM_CLR			0x0000ffff
#define	FW_RA_PARAM_CLR				0xffff0000

enum desc_packet_type {
	DESC_PACKET_TYPE_INIT = 0,
	DESC_PACKET_TYPE_NORMAL = 1,
};

/* 8-bytes alignment required */
struct fw_priv {
	/* --- long word 0 ---- */
	/* 0x12: CE product, 0x92: IT product */
	u8 signature_0;
	/* 0x87: CE product, 0x81: IT product */
	u8 signature_1;
	/* 0x81: PCI-AP, 01:PCIe, 02: 92S-U,
	 * 0x82: USB-AP, 0x12: 72S-U, 03:SDIO */
	u8 hci_sel;
	/* the same value as reigster value  */
	u8 chip_version;
	/* customer  ID low byte */
	u8 customer_id_0;
	/* customer  ID high byte */
	u8 customer_id_1;
	/* 0x11:  1T1R, 0x12: 1T2R,
	 * 0x92: 1T2R turbo, 0x22: 2T2R */
	u8 rf_config;
	/* 4: 4EP, 6: 6EP, 11: 11EP */
	u8 usb_ep_num;

	/* --- long word 1 ---- */
	/* regulatory class bit map 0 */
	u8 regulatory_class_0;
	/* regulatory class bit map 1 */
	u8 regulatory_class_1;
	/* regulatory class bit map 2 */
	u8 regulatory_class_2;
	/* regulatory class bit map 3 */
	u8 regulatory_class_3;
	/* 0:SWSI, 1:HWSI, 2:HWPI */
	u8 rfintfs;
	u8 def_nettype;
	u8 rsvd010;
	u8 rsvd011;

	/* --- long word 2 ---- */
	/* 0x00: normal, 0x03: MACLBK, 0x01: PHYLBK */
	u8 lbk_mode;
	/* 1: for MP use, 0: for normal
	 * driver (to be discussed) */
	u8 mp_mode;
	u8 rsvd020;
	u8 rsvd021;
	u8 rsvd022;
	u8 rsvd023;
	u8 rsvd024;
	u8 rsvd025;

	/* --- long word 3 ---- */
	/* QoS enable */
	u8 qos_en;
	/* 40MHz BW enable */
	/* 4181 convert AMSDU to AMPDU, 0: disable */
	u8 bw_40mhz_en;
	u8 amsdu2ampdu_en;
	/* 11n AMPDU enable */
	u8 ampdu_en;
	/* FW offloads, 0: driver handles */
	u8 rate_control_offload;
	/* FW offloads, 0: driver handles */
	u8 aggregation_offload;
	u8 rsvd030;
	u8 rsvd031;

	/* --- long word 4 ---- */
	/* 1. FW offloads, 0: driver handles */
	u8 beacon_offload;
	/* 2. FW offloads, 0: driver handles */
	u8 mlme_offload;
	/* 3. FW offloads, 0: driver handles */
	u8 hwpc_offload;
	/* 4. FW offloads, 0: driver handles */
	u8 tcp_checksum_offload;
	/* 5. FW offloads, 0: driver handles */
	u8 tcp_offload;
	/* 6. FW offloads, 0: driver handles */
	u8 ps_control_offload;
	/* 7. FW offloads, 0: driver handles */
	u8 wwlan_offload;
	u8 rsvd040;

	/* --- long word 5 ---- */
	/* tcp tx packet length low byte */
	u8 tcp_tx_frame_len_L;
	/* tcp tx packet length high byte */
	u8 tcp_tx_frame_len_H;
	/* tcp rx packet length low byte */
	u8 tcp_rx_frame_len_L;
	/* tcp rx packet length high byte */
	u8 tcp_rx_frame_len_H;
	u8 rsvd050;
	u8 rsvd051;
	u8 rsvd052;
	u8 rsvd053;
};

/* 8-byte alinment required */
struct fw_hdr {

	/* --- LONG WORD 0 ---- */
	u16 signature;
	/* 0x8000 ~ 0x8FFF for FPGA version,
	 * 0x0000 ~ 0x7FFF for ASIC version, */
	u16 version;
	/* define the size of boot loader */
	u32 dmem_size;


	/* --- LONG WORD 1 ---- */
	/* define the size of FW in IMEM */
	u32 img_imem_size;
	/* define the size of FW in SRAM */
	u32 img_sram_size;

	/* --- LONG WORD 2 ---- */
	/* define the size of DMEM variable */
	u32 fw_priv_size;
	u32 rsvd0;

	/* --- LONG WORD 3 ---- */
	u32 rsvd1;
	u32 rsvd2;

	struct fw_priv fwpriv;

} ;

enum fw_status {
	FW_STATUS_INIT = 0,
	FW_STATUS_LOAD_IMEM = 1,
	FW_STATUS_LOAD_EMEM = 2,
	FW_STATUS_LOAD_DMEM = 3,
	FW_STATUS_READY = 4,
};

struct rt_firmware {
	struct fw_hdr *pfwheader;
	enum fw_status fwstatus;
	u16 firmwareversion;
	u8 fw_imem[RTL8190_MAX_FIRMWARE_CODE_SIZE];
	u8 fw_emem[RTL8190_MAX_FIRMWARE_CODE_SIZE];
	u32 fw_imem_len;
	u32 fw_emem_len;
	u8 sz_fw_tmpbuffer[RTL8190_MAX_RAW_FIRMWARE_CODE_SIZE];
	u32 sz_fw_tmpbufferlen;
	u16 cmdpacket_fragthresold;
};

struct h2c_set_pwrmode_parm {
	u8 mode;
	u8 flag_low_traffic_en;
	u8 flag_lpnav_en;
	u8 flag_rf_low_snr_en;
	/* 1: dps, 0: 32k */
	u8 flag_dps_en;
	u8 bcn_rx_en;
	u8 bcn_pass_cnt;
	/* beacon TO (ms). ¡§=0¡¨ no limit. */
	u8 bcn_to;
	u16	bcn_itv;
	/* only for VOIP mode. */
	u8 app_itv;
	u8 awake_bcn_itvl;
	u8 smart_ps;
	/* unit: 100 ms */
	u8 bcn_pass_period;
};

struct h2c_joinbss_rpt_parm {
	u8 opmode;
	u8 ps_qos_info;
	u8 bssid[6];
	u16 bcnitv;
	u16 aid;
} ;

struct h2c_wpa_ptk {
	/* EAPOL-Key Key Confirmation Key (KCK) */
	u8 kck[16];
	/* EAPOL-Key Key Encryption Key (KEK) */
	u8 kek[16];
	/* Temporal Key 1 (TK1) */
	u8 tk1[16];
	union {
		/* Temporal Key 2 (TK2) */
		u8 tk2[16];
		struct {
			u8 tx_mic_key[8];
			u8 rx_mic_key[8];
		} athu;
	} u;
};

struct h2c_wpa_two_way_parm {
	/* algorithm TKIP or AES */
	u8 pairwise_en_alg;
	u8 group_en_alg;
	struct h2c_wpa_ptk wpa_ptk_value;
} ;

enum h2c_cmd {
	FW_H2C_SETPWRMODE = 0,
	FW_H2C_JOINBSSRPT = 1,
	FW_H2C_WOWLAN_UPDATE_GTK = 2,
	FW_H2C_WOWLAN_UPDATE_IV = 3,
	FW_H2C_WOWLAN_OFFLOAD = 4,
};

enum fw_h2c_cmd {
	H2C_READ_MACREG_CMD,				/*0*/
	H2C_WRITE_MACREG_CMD,
	H2C_READBB_CMD,
	H2C_WRITEBB_CMD,
	H2C_READRF_CMD,
	H2C_WRITERF_CMD,				/*5*/
	H2C_READ_EEPROM_CMD,
	H2C_WRITE_EEPROM_CMD,
	H2C_READ_EFUSE_CMD,
	H2C_WRITE_EFUSE_CMD,
	H2C_READ_CAM_CMD,				/*10*/
	H2C_WRITE_CAM_CMD,
	H2C_SETBCNITV_CMD,
	H2C_SETMBIDCFG_CMD,
	H2C_JOINBSS_CMD,
	H2C_DISCONNECT_CMD,				/*15*/
	H2C_CREATEBSS_CMD,
	H2C_SETOPMode_CMD,
	H2C_SITESURVEY_CMD,
	H2C_SETAUTH_CMD,
	H2C_SETKEY_CMD,					/*20*/
	H2C_SETSTAKEY_CMD,
	H2C_SETASSOCSTA_CMD,
	H2C_DELASSOCSTA_CMD,
	H2C_SETSTAPWRSTATE_CMD,
	H2C_SETBASICRATE_CMD,				/*25*/
	H2C_GETBASICRATE_CMD,
	H2C_SETDATARATE_CMD,
	H2C_GETDATARATE_CMD,
	H2C_SETPHYINFO_CMD,
	H2C_GETPHYINFO_CMD,				/*30*/
	H2C_SETPHY_CMD,
	H2C_GETPHY_CMD,
	H2C_READRSSI_CMD,
	H2C_READGAIN_CMD,
	H2C_SETATIM_CMD,				/*35*/
	H2C_SETPWRMODE_CMD,
	H2C_JOINBSSRPT_CMD,
	H2C_SETRATABLE_CMD,
	H2C_GETRATABLE_CMD,
	H2C_GETCCXREPORT_CMD,				/*40*/
	H2C_GETDTMREPORT_CMD,
	H2C_GETTXRATESTATICS_CMD,
	H2C_SETUSBSUSPEND_CMD,
	H2C_SETH2CLBK_CMD,
	H2C_TMP1,					/*45*/
	H2C_WOWLAN_UPDATE_GTK_CMD,
	H2C_WOWLAN_FW_OFFLOAD,
	H2C_TMP2,
	H2C_TMP3,
	H2C_WOWLAN_UPDATE_IV_CMD,			/*50*/
	H2C_TMP4,
	MAX_H2CCMD					/*52*/
};

/* The following macros are used for FW
 * CMD map and parameter updated. */
#define FW_CMD_IO_CLR(rtlpriv, _Bit)				\
	do {							\
		udelay(1000);					\
		rtlpriv->rtlhal.fwcmd_iomap &= (~_Bit);		\
	} while (0)

#define FW_CMD_IO_UPDATE(rtlpriv, _val)				\
	rtlpriv->rtlhal.fwcmd_iomap = _val;

#define FW_CMD_IO_SET(rtlpriv, _val)				\
	do {							\
		rtl_write_word(rtlpriv, LBUS_MON_ADDR, (u16)_val);	\
		FW_CMD_IO_UPDATE(rtlpriv, _val);		\
	} while (0)

#define FW_CMD_PARA_SET(rtlpriv, _val)				\
	do {							\
		rtl_write_dword(rtlpriv, LBUS_ADDR_MASK, _val);	\
		rtlpriv->rtlhal.fwcmd_ioparam = _val;		\
	} while (0)

#define FW_CMD_IO_QUERY(rtlpriv)				\
	(u16)(rtlpriv->rtlhal.fwcmd_iomap)
#define FW_CMD_IO_PARA_QUERY(rtlpriv)				\
	((u32)(rtlpriv->rtlhal.fwcmd_ioparam))

int rtl92s_download_fw(struct ieee80211_hw *hw);
void rtl92s_set_fw_pwrmode_cmd(struct ieee80211_hw *hw, u8 mode);
void rtl92s_set_fw_joinbss_report_cmd(struct ieee80211_hw *hw,
				      u8 mstatus, u8 ps_qosinfo);

#endif

