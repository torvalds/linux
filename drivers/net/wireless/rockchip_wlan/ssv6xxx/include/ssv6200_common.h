/*
 * Copyright (c) 2015 South Silicon Valley Microelectronics Inc.
 * Copyright (c) 2015 iComm Corporation
 *
 * This program is free software: you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation, either version 3 of the License, or 
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _SSV6200_COMMON_H_
#define _SSV6200_COMMON_H_ 
#define FW_VERSION_REG ADR_TX_SEG
#define M_ENG_CPU 0x00
#define M_ENG_HWHCI 0x01
#define M_ENG_EMPTY 0x02
#define M_ENG_ENCRYPT 0x03
#define M_ENG_MACRX 0x04
#define M_ENG_MIC 0x05
#define M_ENG_TX_EDCA0 0x06
#define M_ENG_TX_EDCA1 0x07
#define M_ENG_TX_EDCA2 0x08
#define M_ENG_TX_EDCA3 0x09
#define M_ENG_TX_MNG 0x0A
#define M_ENG_ENCRYPT_SEC 0x0B
#define M_ENG_MIC_SEC 0x0C
#define M_ENG_RESERVED_1 0x0D
#define M_ENG_RESERVED_2 0x0E
#define M_ENG_TRASH_CAN 0x0F
#define M_ENG_MAX (M_ENG_TRASH_CAN+1)
#define M_CPU_HWENG 0x00
#define M_CPU_TXL34CS 0x01
#define M_CPU_RXL34CS 0x02
#define M_CPU_DEFRAG 0x03
#define M_CPU_EDCATX 0x04
#define M_CPU_RXDATA 0x05
#define M_CPU_RXMGMT 0x06
#define M_CPU_RXCTRL 0x07
#define M_CPU_FRAG 0x08
#define M_CPU_TXTPUT 0x09
#ifndef ID_TRAP_SW_TXTPUT
#define ID_TRAP_SW_TXTPUT 50
#endif
#define M0_TXREQ 0
#define M1_TXREQ 1
#define M2_TXREQ 2
#define M0_RXEVENT 3
#define M2_RXEVENT 4
#define HOST_CMD 5
#define HOST_EVENT 6
#define TEST_CMD 7
#define SSV6XXX_RX_DESC_LEN \
        (sizeof(struct ssv6200_rx_desc) + \
         sizeof(struct ssv6200_rxphy_info))
#define SSV6XXX_TX_DESC_LEN \
        (sizeof(struct ssv6200_tx_desc) + 0)
#define TXPB_OFFSET 80
#define RXPB_OFFSET 80
#define SSV6200_TX_PKT_RSVD_SETTING 0x3
#define SSV6200_TX_PKT_RSVD SSV6200_TX_PKT_RSVD_SETTING*16
#define SSV6200_ALLOC_RSVD TXPB_OFFSET+SSV6200_TX_PKT_RSVD
#define SSV62XX_TX_MAX_RATES 3

enum ssv6xxx_sr_bhvr {
    SUSPEND_RESUME_0,
    SUSPEND_RESUME_1,
    SUSPEND_RESUME_MAX
};

enum ssv6xxx_reboot_bhvr {
		SSV_SYS_REBOOT = 1,
		SSV_SYS_HALF,
		SSV_SYS_POWER_OFF
};

struct fw_rc_retry_params {
    u32 count:4;
    u32 drate:6;
    u32 crate:6;
    u32 rts_cts_nav:16;
    u32 frame_consume_time:10;
    u32 dl_length:12;
    u32 RSVD:10;
} __attribute__((packed));
struct ssv6200_tx_desc
{
    u32 len:16;
    u32 c_type:3;
    u32 f80211:1;
    u32 qos:1;
    u32 ht:1;
    u32 use_4addr:1;
    u32 RSVD_0:3;
    u32 bc_que:1;
    u32 security:1;
    u32 more_data:1;
    u32 stype_b5b4:2;
    u32 extra_info:1;
    u32 fCmd;
    u32 hdr_offset:8;
    u32 frag:1;
    u32 unicast:1;
    u32 hdr_len:6;
    u32 tx_report:1;
    u32 tx_burst:1;
    u32 ack_policy:2;
    u32 aggregation:1;
    u32 RSVD_1:3;
    u32 do_rts_cts:2;
    u32 reason:6;
    u32 payload_offset:8;
    u32 RSVD_4:7;
    u32 RSVD_2:1;
    u32 fCmdIdx:3;
    u32 wsid:4;
    u32 txq_idx:3;
    u32 TxF_ID:6;
    u32 rts_cts_nav:16;
    u32 frame_consume_time:10;
    u32 crate_idx:6;
    u32 drate_idx:6;
    u32 dl_length:12;
    u32 RSVD_3:14;
    u32 RESERVED[8];
    struct fw_rc_retry_params rc_params[SSV62XX_TX_MAX_RATES];
};
struct ssv6200_rx_desc
{
    u32 len:16;
    u32 c_type:3;
    u32 f80211:1;
    u32 qos:1;
    u32 ht:1;
    u32 use_4addr:1;
    u32 l3cs_err:1;
    u32 l4cs_err:1;
    u32 align2:1;
    u32 RSVD_0:2;
    u32 psm:1;
    u32 stype_b5b4:2;
    u32 extra_info:1;
    u32 edca0_used:4;
    u32 edca1_used:5;
    u32 edca2_used:5;
    u32 edca3_used:5;
    u32 mng_used:4;
    u32 tx_page_used:9;
    u32 hdr_offset:8;
    u32 frag:1;
    u32 unicast:1;
    u32 hdr_len:6;
    u32 RxResult:8;
    u32 wildcard_bssid:1;
    u32 RSVD_1:1;
    u32 reason:6;
    u32 payload_offset:8;
    u32 tx_id_used:8;
    u32 fCmdIdx:3;
    u32 wsid:4;
    u32 RSVD_3:3;
    u32 rate_idx:6;
};
struct ssv6200_rxphy_info {
    u32 len:16;
    u32 rsvd0:16;
    u32 mode:3;
    u32 ch_bw:3;
    u32 preamble:1;
    u32 ht_short_gi:1;
    u32 rate:7;
    u32 rsvd1:1;
    u32 smoothing:1;
    u32 no_sounding:1;
    u32 aggregate:1;
    u32 stbc:2;
    u32 fec:1;
    u32 n_ess:2;
    u32 rsvd2:8;
    u32 l_length:12;
    u32 l_rate:3;
    u32 rsvd3:17;
    u32 rsvd4;
    u32 rpci:8;
    u32 snr:8;
    u32 service:16;
};
struct ssv6200_rxphy_info_padding {
u32 rpci:8;
u32 snr:8;
u32 RSVD:16;
};
struct ssv6200_txphy_info {
    u32 rsvd[7];
};
#ifdef CONFIG_P2P_NOA
struct ssv6xxx_p2p_noa_param {
    u32 duration;
    u32 interval;
    u32 start_time;
    u32 enable:8;
    u32 count:8;
    u8 addr[6];
    u8 vif_id;
}__attribute__((packed));
#endif
typedef struct cfg_host_cmd {
    u32 len:16;
    u32 c_type:3;
    u32 RSVD0:5;
    u32 h_cmd:8;
    u32 cmd_seq_no;
    union {
    u32 dummy;
    u8 dat8[0];
    u16 dat16[0];
    u32 dat32[0];
    };
} HDR_HostCmd;
#define HOST_CMD_HDR_LEN ((size_t)(((HDR_HostCmd *)100)->dat8)-100U)
struct sdio_rxtput_cfg {
    u32 size_per_frame;
 u32 total_frames;
};
typedef enum{
    SSV6XXX_HOST_CMD_START = 0 ,
    SSV6XXX_HOST_CMD_LOG ,
    SSV6XXX_HOST_CMD_PS ,
    SSV6XXX_HOST_CMD_INIT_CALI ,
    SSV6XXX_HOST_CMD_RX_TPUT ,
    SSV6XXX_HOST_CMD_TX_TPUT ,
    SSV6XXX_HOST_CMD_WATCHDOG_START,
    SSV6XXX_HOST_CMD_WATCHDOG_STOP,
#ifdef FW_WSID_WATCH_LIST
    SSV6XXX_HOST_CMD_WSID_OP ,
#endif
#ifdef CONFIG_P2P_NOA
    SSV6XXX_HOST_CMD_SET_NOA ,
#endif
    SSV6XXX_HOST_SOC_CMD_MAXID ,
}ssv6xxx_host_cmd_id;
#define SSV_NUM_HW_STA 2
typedef struct cfg_host_event {
    u32 len:16;
    u32 c_type:3;
    u32 RSVD0:5;
    u32 h_event:8;
    u32 evt_seq_no;
    u8 dat[0];
} HDR_HostEvent;
typedef enum{
#ifdef USE_CMD_RESP
    SOC_EVT_CMD_RESP ,
    SOC_EVT_SCAN_RESULT ,
    SOC_EVT_DEAUTH ,
#else
    SOC_EVT_GET_REG_RESP ,
#endif
    SOC_EVT_NO_BA ,
    SOC_EVT_RC_MPDU_REPORT ,
    SOC_EVT_RC_AMPDU_REPORT ,
    SOC_EVT_LOG ,
#ifdef CONFIG_P2P_NOA
    SOC_EVT_NOA ,
#endif
    SOC_EVT_USER_END ,
    SOC_EVT_SDIO_TEST_COMMAND ,
    SOC_EVT_RESET_HOST ,
    SOC_EVT_SDIO_TXTPUT_RESULT ,
    SOC_EVT_WATCHDOG_TRIGGER ,
    SOC_EVT_TXLOOPBK_RESULT ,
    SOC_EVT_MAXID ,
} ssv6xxx_soc_event;
#ifdef CONFIG_P2P_NOA
typedef enum{
    SSV6XXX_NOA_START = 0 ,
    SSV6XXX_NOA_STOP ,
}ssv6xxx_host_noa_event;
struct ssv62xx_noa_evt {
    u8 evt_id;
    u8 vif;
} __attribute__((packed));
#endif
typedef enum{
    SSV6XXX_RC_COUNTER_CLEAR = 1 ,
    SSV6XXX_RC_REPORT ,
}ssv6xxx_host_rate_control_event;
#define MAX_AGGR_NUM (24)
struct ssv62xx_tx_rate {
    s8 data_rate;
    u8 count;
} __attribute__((packed));
struct ampdu_ba_notify_data {
    u8 wsid;
    struct ssv62xx_tx_rate tried_rates[SSV62XX_TX_MAX_RATES];
    u16 seq_no[MAX_AGGR_NUM];
} __attribute__((packed));
struct firmware_rate_control_report_data{
    u8 wsid;
    struct ssv62xx_tx_rate rates[SSV62XX_TX_MAX_RATES];
    u16 ampdu_len;
    u16 ampdu_ack_len;
    int ack_signal;
} __attribute__((packed));
#define RC_RETRY_PARAM_OFFSET ((sizeof(struct fw_rc_retry_params))*SSV62XX_TX_MAX_RATES)
#define SSV_RC_RATE_MAX 39
#ifdef FW_WSID_WATCH_LIST
enum SSV6XXX_WSID_OPS
{
    SSV6XXX_WSID_OPS_ADD,
    SSV6XXX_WSID_OPS_DEL,
    SSV6XXX_WSID_OPS_RESETALL,
    SSV6XXX_WSID_OPS_ENABLE_CAPS,
    SSV6XXX_WSID_OPS_DISABLE_CAPS,
    SSV6XXX_WSID_OPS_HWWSID_PAIRWISE_SET_TYPE,
    SSV6XXX_WSID_OPS_HWWSID_GROUP_SET_TYPE,
    SSV6XXX_WSID_OPS_MAX
};
enum SSV6XXX_WSID_SEC
{
    SSV6XXX_WSID_SEC_NONE = 0,
    SSV6XXX_WSID_SEC_PAIRWISE = 1<<0,
    SSV6XXX_WSID_SEC_GROUP = 1<<1,
};
enum SSV6XXX_WSID_SEC_TYPE
{
    SSV6XXX_WSID_SEC_SW,
    SSV6XXX_WSID_SEC_HW,
    SSV6XXX_WSID_SEC_TYPE_MAX
};
enum SSV6XXX_RETURN_STATE
{
    SSV6XXX_STATE_OK,
    SSV6XXX_STATE_NG,
    SSV6XXX_STATE_MAX
};
struct ssv6xxx_wsid_params
{
    u8 cmd;
    u8 wsid_idx;
    u8 target_wsid[6];
    u8 hw_security;
};
#endif
struct ssv6xxx_iqk_cfg {
    u32 cfg_xtal:8;
    u32 cfg_pa:8;
    u32 cfg_pabias_ctrl:8;
    u32 cfg_pacascode_ctrl:8;
    u32 cfg_tssi_trgt:8;
    u32 cfg_tssi_div:8;
    u32 cfg_def_tx_scale_11b:8;
    u32 cfg_def_tx_scale_11b_p0d5:8;
    u32 cfg_def_tx_scale_11g:8;
    u32 cfg_def_tx_scale_11g_p0d5:8;
    u32 cmd_sel;
    union {
        u32 fx_sel;
        u32 argv;
    };
    u32 phy_tbl_size;
    u32 rf_tbl_size;
};
#define PHY_SETTING_SIZE sizeof(phy_setting)
#ifdef CONFIG_SSV_CABRIO_E
struct ssv6xxx_ch_cfg {
 u32 reg_addr;
 u32 ch1_12_value;
 u32 ch13_14_value;
};
#define IQK_CFG_LEN (sizeof(struct ssv6xxx_iqk_cfg))
#define RF_SETTING_SIZE (sizeof(asic_rf_setting))
#endif
#define MAX_PHY_SETTING_TABLE_SIZE 1920
#define MAX_RF_SETTING_TABLE_SIZE 512
typedef enum {
    SSV6XXX_VOLT_DCDC_CONVERT = 0,
    SSV6XXX_VOLT_LDO_CONVERT,
} ssv6xxx_cfg_volt;
typedef enum {
    SSV6XXX_VOLT_33V = 0,
    SSV6XXX_VOLT_42V,
} ssv6xxx_cfg_volt_value;
typedef enum {
    SSV6XXX_IQK_CFG_XTAL_26M = 0,
    SSV6XXX_IQK_CFG_XTAL_40M,
    SSV6XXX_IQK_CFG_XTAL_24M,
    SSV6XXX_IQK_CFG_XTAL_MAX,
} ssv6xxx_iqk_cfg_xtal;
typedef enum {
    SSV6XXX_IQK_CFG_PA_DEF = 0,
    SSV6XXX_IQK_CFG_PA_LI_MPB,
    SSV6XXX_IQK_CFG_PA_LI_EVB,
    SSV6XXX_IQK_CFG_PA_HP,
} ssv6xxx_iqk_cfg_pa;
typedef enum {
    SSV6XXX_IQK_CMD_INIT_CALI = 0,
    SSV6XXX_IQK_CMD_RTBL_LOAD,
    SSV6XXX_IQK_CMD_RTBL_LOAD_DEF,
    SSV6XXX_IQK_CMD_RTBL_RESET,
    SSV6XXX_IQK_CMD_RTBL_SET,
    SSV6XXX_IQK_CMD_RTBL_EXPORT,
    SSV6XXX_IQK_CMD_TK_EVM,
    SSV6XXX_IQK_CMD_TK_TONE,
    SSV6XXX_IQK_CMD_TK_CHCH,
} ssv6xxx_iqk_cmd_sel;
#define SSV6XXX_IQK_TEMPERATURE 0x00000004
#define SSV6XXX_IQK_RXDC 0x00000008
#define SSV6XXX_IQK_RXRC 0x00000010
#define SSV6XXX_IQK_TXDC 0x00000020
#define SSV6XXX_IQK_TXIQ 0x00000040
#define SSV6XXX_IQK_RXIQ 0x00000080
#define SSV6XXX_IQK_TSSI 0x00000100
#define SSV6XXX_IQK_PAPD 0x00000200
typedef struct ssv_cabrio_reg_st {
    u32 address;
    u32 data;
} ssv_cabrio_reg;
typedef enum __PBuf_Type_E {
    NOTYPE_BUF = 0,
    TX_BUF = 1,
    RX_BUF = 2
} PBuf_Type_E;
struct SKB_info_st
{
    struct ieee80211_sta *sta;
    u16 mpdu_retry_counter;
    unsigned long aggr_timestamp;
    u16 ampdu_tx_status;
    u16 ampdu_tx_final_retry_count;
    u16 lowest_rate;
    struct fw_rc_retry_params rates[SSV62XX_TX_MAX_RATES];
#ifdef CONFIG_DEBUG_SKB_TIMESTAMP
 ktime_t timestamp;
#endif
#ifdef MULTI_THREAD_ENCRYPT
    volatile u8 crypt_st;
#endif
};
typedef struct SKB_info_st SKB_info;
typedef struct SKB_info_st *p_SKB_info;
#define SSV_SKB_info_size (sizeof(struct SKB_info_st))
#ifdef MULTI_THREAD_ENCRYPT
enum ssv_pkt_crypt_status
{
    PKT_CRYPT_ST_DEC_PRE,
    PKT_CRYPT_ST_ENC_PRE,
    PKT_CRYPT_ST_DEC_DONE,
    PKT_CRYPT_ST_ENC_DONE,
    PKT_CRYPT_ST_FAIL,
    PKT_CRYPT_ST_NOT_SUPPORT
};
#endif
#ifdef CONFIG_DEBUG_SKB_TIMESTAMP
#define SKB_DURATION_TIMEOUT_MS 100
enum ssv_debug_skb_timestamp
{
 SKB_DURATION_STAGE_TX_ENQ,
 SKB_DURATION_STAGE_TO_SDIO,
 SKB_DURATION_STAGE_IN_HWQ,
 SKB_DURATION_STAGE_END
};
#endif
#define SSV6051Q_P1 0x00000000
#define SSV6051Q_P2 0x70000000
#define SSV6051Z 0x71000000
#define SSV6051Q 0x73000000
#define SSV6051P 0x75000000
#ifdef CONFIG_SSV_CABRIO_E
struct ssv6xxx_tx_loopback {
    u32 reg;
    u32 val;
    u32 restore_val;
    u8 restore;
    u8 delay_ms;
};
#endif
#endif
