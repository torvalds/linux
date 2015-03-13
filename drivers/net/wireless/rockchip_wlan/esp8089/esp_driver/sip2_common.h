/* Copyright (c) 2008 -2014 Espressif System.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 *   Common definitions of Serial Interconnctor Protocol
 *
 *   little endian
 */

#ifndef _SIP2_COMMON_H
#define _SIP2_COMMON_H

#ifdef __ets__
#include "utils.h"
#endif /*__ets__*/

/* max 16 types */
typedef enum {
        SIP_CTRL = 0,
        SIP_DATA,
        SIP_DATA_AMPDU
} SIP_TYPE;

typedef enum {
        SIP_TX_CTRL_BUF = 0,  /* from host */
        SIP_RX_CTRL_BUF,  /* to host */
        SIP_TX_DATA_BUF, /* from host */
        SIP_RX_DATA_BUF /* to host */
} SIP_BUF_TYPE;

enum sip_cmd_id {
        SIP_CMD_GET_VER = 0,
        SIP_CMD_WRITE_MEMORY,//1 ROM code
        SIP_CMD_READ_MEMORY,//2
        SIP_CMD_WRITE_REG,//3 ROM code
        SIP_CMD_READ_REG,     //4
        SIP_CMD_BOOTUP,//5 ROM code
        SIP_CMD_COPYBACK,//6
        SIP_CMD_INIT,          //7
        SIP_CMD_SCAN,//8
        SIP_CMD_SETKEY,//9
        SIP_CMD_CONFIG,//10
        SIP_CMD_BSS_INFO_UPDATE,//11
        SIP_CMD_LOOPBACK,//12  ROM code
        //do not add cmd before this line
        SIP_CMD_SET_WMM_PARAM,
        SIP_CMD_AMPDU_ACTION,
        SIP_CMD_HB_REQ, //15
        SIP_CMD_RESET_MAC, //16
        SIP_CMD_PRE_DOWN,  //17
        SIP_CMD_SLEEP,        /* for sleep testing */
        SIP_CMD_WAKEUP,       /* for sleep testing */
        SIP_CMD_DEBUG,          /* for general testing */
        SIP_CMD_GET_FW_VER,  /* get fw rev. */
        SIP_CMD_SETVIF,
        SIP_CMD_SETSTA,
        SIP_CMD_PS,
        SIP_CMD_ATE,
        SIP_CMD_SUSPEND,
	SIP_CMD_RECALC_CREDIT,
        SIP_CMD_MAX,
};

enum {
        SIP_EVT_TARGET_ON = 0,    //
        SIP_EVT_BOOTUP,//1 in ROM code
        SIP_EVT_COPYBACK,//2
        SIP_EVT_SCAN_RESULT,  //3
        SIP_EVT_TX_STATUS,//4
        SIP_EVT_CREDIT_RPT,     //5, in ROM code
        SIP_EVT_ERROR,//6
        SIP_EVT_LOOPBACK,//7, in ROM code
        SIP_EVT_SNPRINTF_TO_HOST, //8  in ROM code
        //do not add evt before this line
        SIP_EVT_HB_ACK,  //9
        SIP_EVT_RESET_MAC_ACK, //10
        SIP_EVT_WAKEUP,//11        /* for sleep testing */
        SIP_EVT_DEBUG,//12          /* for general testing */
        SIP_EVT_PRINT_TO_HOST, //13
        SIP_EVT_TRC_AMPDU, //14
        SIP_EVT_ROC,              //15
        SIP_EVT_RESETTING,
        SIP_EVT_ATE,
        SIP_EVT_EP,
        SIP_EVT_INIT_EP,
        SIP_EVT_SLEEP,
        SIP_EVT_TXIDLE,
        SIP_EVT_NOISEFLOOR,
	SIP_EVT_NULLFUNC_REPORT,
        SIP_EVT_MAX
};

#define SIP_IFIDX_MASK 0xf0
#define SIP_IFIDX_S 4
#define SIP_TYPE_MASK 0x0f
#define SIP_TYPE_S 0

#define SIP_HDR_GET_IFIDX(fc0) (((fc0) & SIP_IFIDX_MASK) >> SIP_IFIDX_S)
#define SIP_HDR_SET_IFIDX(fc0, ifidx) ( (fc0) = ((fc0) & ~SIP_IFIDX_MASK) | ((ifidx) << SIP_IFIDX_S & SIP_IFIDX_MASK) )
#define SIP_HDR_GET_TYPE(fc0) ((fc0) & SIP_TYPE_MASK )
/* assume type field is cleared */
#define SIP_HDR_SET_TYPE(fc0, type) ((fc0) = ((fc0) & ~ SIP_TYPE_MASK) | ((type) & SIP_TYPE_MASK))

/* sip 2.0, not hybrid header so far */
#define SIP_HDR_IS_CTRL(hdr) (SIP_HDR_GET_TYPE((hdr)->fc[0]) == SIP_CTRL)
#define SIP_HDR_IS_DATA(hdr) (SIP_HDR_GET_TYPE((hdr)->fc[0]) == SIP_DATA)
#define SIP_HDR_IS_AMPDU(hdr) (SIP_HDR_GET_TYPE((hdr)->fc[0]) == SIP_DATA_AMPDU)

/* fc[1] flags, only for data pkt. Ctrl pkts use fc[1] as eventID */
#define SIP_HDR_SET_FLAGS(hdr, flags) ((hdr)->fc[1] |= (flags))
#define SIP_HDR_F_MORE_PKT 0x1
#define SIP_HDR_F_NEED_CRDT_RPT 0x2
#define SIP_HDR_F_SYNC 0x4
#define SIP_HDR_F_SYNC_RESET 0x8
#define SIP_HDR_F_PM_TURNING_ON 0x10
#define SIP_HDR_F_PM_TURNING_OFF 0x20

#define SIP_HDR_NEED_CREDIT_UPDATE(hdr) ((hdr)->fc[1] & SIP_HDR_F_NEED_CRDT_RPT)
#define SIP_HDR_IS_MORE_PKT(hdr) ((hdr)->fc[1] & SIP_HDR_F_MORE_PKT)
#define SIP_HDR_IS_CRDT_RPT(hdr) ((hdr)->fc[1] & SIP_HDR_F_CRDT_RPT)
#define SIP_HDR_IS_SYNC(hdr) ((hdr)->fc[1] & SIP_HDR_F_SYNC)
#define SIP_HDR_IS_SYNC_RESET(hdr) ((hdr)->fc[1] & SIP_HDR_F_SYNC_RESET)
#define SIP_HDR_IS_SYNC_PKT(hdr) (SIP_HDR_IS_SYNC(hdr) | SIP_HDR_IS_SYNC_RESET(hdr))
#define SIP_HDR_SET_SYNC(hdr) SIP_HDR_SET_FLAGS((hdr), SIP_HDR_F_SYNC)
#define SIP_HDR_SET_SYNC_RESET(hdr) SIP_HDR_SET_FLAGS((hdr), SIP_HDR_F_SYNC_RESET)
#define SIP_HDR_SET_MORE_PKT(hdr) SIP_HDR_SET_FLAGS((hdr), SIP_HDR_F_MORE_PKT)
#define SIP_HDR_SET_PM_TURNING_ON(hdr) SIP_HDR_SET_FLAGS((hdr), SIP_HDR_F_PM_TURNING_ON)
#define SIP_HDR_IS_PM_TURNING_ON(hdr) ((hdr)->fc[1] & SIP_HDR_F_PM_TURNING_ON)
#define SIP_HDR_SET_PM_TURNING_OFF(hdr) SIP_HDR_SET_FLAGS((hdr), SIP_HDR_F_PM_TURNING_OFF)
#define SIP_HDR_IS_PM_TURNING_OFF(hdr) ((hdr)->fc[1] & SIP_HDR_F_PM_TURNING_OFF)

/*
 * fc[0]: first 4bit: ifidx; last 4bit: type
 * fc[1]: flags
 *
 *   Don't touch the header definitons
 */
struct sip_hdr_min {
        u8 fc[2];
        __le16 len;
} __packed;

/* not more than 4byte long */
struct sip_tx_data_info {
        u8 tid;
        u8 ac;
        u8 p2p:1,
           enc_flag:7;
        u8 hw_kid;
}  __packed;

/* NB: this structure should be not more than 4byte !! */
struct sip_tx_info {
        union {
                u32 cmdid;
                struct sip_tx_data_info dinfo;
        } u;
} __packed;

struct sip_hdr {
        u8 fc[2];  //fc[0]: type and ifidx ; fc[1] is eventID if the first ctrl pkt in the chain. data pkt still can use fc[1] to set flag
        __le16 len;
        union {
                volatile u32 recycled_credits; /* last 12bits is credits, first 20 bits is actual length of the first pkt in the chain */
                struct sip_tx_info tx_info;
        } u;
        u32 seq;
} __packed;

#define h_credits u.recycled_credits
#define c_evtid fc[1]
#define c_cmdid u.tx_info.u.cmdid
#define d_ac u.tx_info.u.dinfo.ac
#define d_tid  u.tx_info.u.dinfo.tid
#define d_p2p   u.tx_info.u.dinfo.p2p
#define d_enc_flag u.tx_info.u.dinfo.enc_flag
#define d_hw_kid   u.tx_info.u.dinfo.hw_kid

#define SIP_CREDITS_MASK  0xfff  /* last 12 bits */

#ifdef HOST_RC

#define RC_CNT_MASK 0xf

struct sip_rc_status {
        u32 rc_map;
        union {
                u32 rc_cnt1:4,
                    rc_cnt2:4,
                    rc_cnt3:4,
                    rc_cnt4:4,
                    rc_cnt5:4;

                u32 rc_cnt_store;
        };
};

/* copy from mac80211.h */
struct sip_tx_rc {
        struct ieee80211_tx_rate rates[IEEE80211_TX_MAX_RATES];
        s8 rts_cts_rate_idx;
};
#endif /* HOST_RC */

#define SIP_HDR_MIN_LEN 4
#define SIP_HDR_LEN		sizeof(struct sip_hdr)
#define SIP_CTRL_HDR_LEN 	SIP_HDR_LEN  /* same as sip_hdr in sip2 design */
#define SIP_BOOT_BUF_SIZE 256
#define SIP_CTRL_BUF_SZ 256 /* too much?? */
#define SIP_CTRL_BUF_N 6
#define SIP_CTRL_TXBUF_N 2
#define SIP_CTRL_RXBUF_N 4

/* WAR for mblk */
#define SIP_RX_ADDR_PREFIX_MASK 0xfc000000
#define SIP_RX_ADDR_SHIFT 6  /* [31:5],  shift 6 bits*/

struct sip_cmd_write_memory {
        u32 addr;
        u32 len;
} __packed;

struct sip_cmd_read_memory {
        u32 addr;
        u32 len;
} __packed;

struct sip_cmd_write_reg {
        u32 addr;
        u32 val;
} __packed;

struct sip_cmd_bootup {
        u32 boot_addr;
} __packed;

struct sip_cmd_loopback {
        u32 txlen;       //host to target packet len, 0 means no txpacket
        u32 rxlen;      //target to host packet len, 0 means no rxpacket
        u32 pack_id;   //sequence of packet
} __packed;

struct sip_evt_loopback {
        u32 txlen;       //host to target packet len, 0 means no txpacket
        u32 rxlen;      //target to host packet len, 0 means no rxpacket
        u32 pack_id;   //sequence of packet
} __packed;

struct sip_cmd_copyback {
        u32 addr;
        u32 len;
} __packed;

struct sip_cmd_scan {
//        u8  ssid[32];
        u8 ssid_len;
//        u8 hw_channel[14];
        u8 n_channels;
        u8 ie_len;
        u8 aborted;
} __packed; // ie[] append at the end


#ifndef ETH_ALEN
#define ETH_ALEN 6
#endif /* ETH_ALEN */

struct sip_cmd_setkey {
        u8 bssid_no;
        u8 addr[ETH_ALEN];
        u8 alg;
        u8 keyidx;
        u8 hw_key_idx;
        u8 flags;
        u8 keylen;
        u8 key[32];
} __packed;

struct sip_cmd_config {
        u16  center_freq;
        u16  duration;
} __packed;

struct sip_cmd_bss_info_update {
        u8  bssid[ETH_ALEN];
        u16 isassoc;
        u32 beacon_int;
        u8  bssid_no;
} __packed;

struct sip_evt_bootup {
        u16 tx_blksz;
        u8 mac_addr[ETH_ALEN];
        /* anything else ? */
} __packed;

struct sip_cmd_setvif {
        u8 index;
        u8 mac[ETH_ALEN];
        u8 set;
        u8 op_mode;
        u8 is_p2p;
} __packed;

enum esp_ieee80211_phytype{
        ESP_IEEE80211_T_CCK = 0,
        ESP_IEEE80211_T_OFDM = 1,
        ESP_IEEE80211_T_HT20_L = 2,
        ESP_IEEE80211_T_HT20_S = 3,
};

struct sip_cmd_setsta {
        u8 ifidx;
        u8 index;
        u8 set;
        u8 phymode;
        u8 mac[ETH_ALEN];
	u16 aid;
        u8 ampdu_factor;
        u8 ampdu_density;
	u16 resv;
} __packed;

struct sip_cmd_ps {
	u8 dtim_period;
	u8 max_sleep_period;
        u8 on;
        u8 resv;
} __packed;

struct sip_cmd_suspend {
	u8 suspend;
	u8 resv[3];
} __packed;

#define SIP_DUMP_RPBM_ERR	BIT(0)
#define SIP_RXABORT_FIXED	BIT(1)
#define SIP_SUPPORT_BGSCAN	BIT(2)
struct sip_evt_bootup2 {
        u16 tx_blksz;
        u8 mac_addr[ETH_ALEN];
        u16 rx_blksz;
        u8 credit_to_reserve;
        u8 options;
	s16 noise_floor;
	u8 mac_type;
	u8 resv[1];
        /* anything else ? */
} __packed;

typedef enum {
        TRC_TX_AMPDU_STOPPED = 1,
        TRC_TX_AMPDU_OPERATIONAL,
        TRC_TX_AMPDU_WAIT_STOP,
        TRC_TX_AMPDU_WAIT_OPERATIONAL,
        TRC_TX_AMPDU_START,
} trc_ampdu_state_t;

struct sip_evt_trc_ampdu {
        u8 state;
        u8 tid;
        u8 addr[ETH_ALEN];
} __packed;

struct sip_cmd_set_wmm_params {
        u8 aci;
        u8 aifs;
        u8 ecw_min;
        u8 ecw_max;
        u16 txop_us;
} __packed;

#define SIP_AMPDU_RX_START 0
#define SIP_AMPDU_RX_STOP 1
#define SIP_AMPDU_TX_OPERATIONAL 2
#define SIP_AMPDU_TX_STOP 3
struct sip_cmd_ampdu_action {
        u8 action;
        u8 index;
        u8 tid;
        u8 win_size;
        u16 ssn;
        u8 addr[ETH_ALEN];
} __packed;

#define SIP_TX_ST_OK 0
#define SIP_TX_ST_NOEB 1
#define SIP_TX_ST_ACKTO 2
#define SIP_TX_ST_ENCERR 3

//NB: sip_tx_status must be 4 bytes aligned
struct sip_tx_status {
        u32 sip_seq;
#ifdef HOST_RC
        struct sip_rc_status rcstatus;
#endif /* HOST_RC */
        u8 errno;  /* success or failure code */
        u8 rate_index;
        char ack_signal;
        u8 pad;
} __packed;

struct sip_evt_tx_report {
        u32 pkts;
        struct sip_tx_status status[0];
} __packed;

struct sip_evt_tx_mblk {
        u32 mblk_map;
} __packed;

struct sip_evt_scan_report {
        u16 scan_id;
        u16 aborted;
} __packed;

struct sip_evt_roc {
     u16    state;     //start:1, end :0
     u16    is_ok;
} __packed;

struct sip_evt_txidle {
	u32	last_seq;
} __packed;

struct sip_evt_noisefloor {
     s16 noise_floor;
     u16    pad;
} __packed;

struct sip_evt_nullfunc_report {
     u8    ifidx;
     u8    index;
     u8    status;
     u8    pad;
} __packed;

/*
 *  for mblk direct memory access, no need for sip_hdr. tx: first 2k for contrl msg,
 *  rest of 14k for data.  rx, same.
 */
#ifdef TEST_MODE

struct sip_cmd_sleep {
        u32  sleep_mode;
        u32  sleep_tm_ms;
        u32  wakeup_tm_ms;            //zero: after receive bcn, then sleep, nozero: delay nozero ms to sleep
        u32  sleep_times;                 //zero: always sleep, nozero: after nozero number sleep/wakeup, then end up sleep
} __packed;

struct sip_cmd_wakeup {
        u32     check_data;            //0:copy to event
} __packed;

struct sip_evt_wakeup {
        u32    check_data;
} __packed;

//general debug command
struct sip_cmd_debug {
        u32  cmd_type;
        u32  para_num;
        u32  para[10];
} __packed;

struct sip_evt_debug {
        u16    len;
        u32    results[12];
        u16    pad;
} __packed;

struct sip_cmd_ate {
        //u8  len;
	u8  cmdstr[0];
} __packed;



#endif  //ifdef TEST_MODE

#endif /* _SIP_COMMON_H_ */
