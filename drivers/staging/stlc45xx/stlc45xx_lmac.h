/************************************************************************
*  This is the LMAC API interface header file for STLC4560.        	*
*  Copyright (C) 2007 Conexant Systems, Inc.                            *
*  This program is free software; you can redistribute it and/or        *
*  modify it under the terms of the GNU General Public License          *
*  as published by the Free Software Foundation; either version 2	*
*  of the License, or (at your option) any later version.               *
*                                                                       *
*  This program is distributed in the hope that it will be useful,      *
*  but WITHOUT ANY WARRANTY; without even the implied warranty of	*
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the        *
*  GNU General Public License for more details.                         *
*                                                                       *
*  You should have received a copy of the GNU General Public License    *
*  along with this program.  If not, see <http://www.gnu.org/licenses/>.*
*************************************************************************/

#ifndef __lmac_h__
#define __lmac_h__

#define LM_TOP_VARIANT      0x0506
#define LM_BOTTOM_VARIANT   0x0506

/*
 * LMAC - UMAC interface definition:
 */

#define LM_FLAG_CONTROL     0x8000
#define LM_FLAG_ALIGN       0x4000

#define LM_CTRL_OPSET       0x0001

#define LM_OUT_PROMISC      0x0001
#define LM_OUT_TIMESTAMP    0x0002
#define LM_OUT_SEQNR        0x0004
#define LM_OUT_BURST        0x0010
#define LM_OUT_NOCANCEL     0x0020
#define LM_OUT_CLEARTIM     0x0040
#define LM_OUT_HITCHHIKE    0x0080
#define LM_OUT_COMPRESS     0x0100
#define LM_OUT_CONCAT       0x0200
#define LM_OUT_PCS_ACCEPT   0x0400
#define LM_OUT_WAITEOSP     0x0800


#define LM_ALOFT_SP         0x10
#define LM_ALOFT_CTS        0x20
#define LM_ALOFT_RTS        0x40
#define LM_ALOFT_MASK       0x1f
#define LM_ALOFT_RATE       0x0f

#define LM_IN_FCS_GOOD      0x0001
#define LM_IN_MATCH_MAC     0x0002
#define LM_IN_MCBC          0x0004
#define LM_IN_BEACON        0x0008
#define LM_IN_MATCH_BSS     0x0010
#define LM_IN_BCAST_BSS     0x0020
#define LM_IN_DATA          0x0040
#define LM_IN_TRUNCATED     0x0080

#define LM_IN_TRANSPARENT   0x0200

#define LM_QUEUE_BEACON     0
#define LM_QUEUE_SCAN       1
#define LM_QUEUE_MGT        2
#define LM_QUEUE_MCBC       3
#define LM_QUEUE_DATA       4
#define LM_QUEUE_DATA0      4
#define LM_QUEUE_DATA1      5
#define LM_QUEUE_DATA2      6
#define LM_QUEUE_DATA3      7

#define LM_SETUP_INFRA          0x0001
#define LM_SETUP_IBSS           0x0002
#define LM_SETUP_TRANSPARENT    0x0008
#define LM_SETUP_PROMISCUOUS    0x0010
#define LM_SETUP_HIBERNATE      0x0020
#define LM_SETUP_NOACK          0x0040
#define LM_SETUP_RX_DISABLED    0x0080

#define LM_ANTENNA_0            0
#define LM_ANTENNA_1            1
#define LM_ANTENNA_DIVERSITY    2

#define LM_TX_FAILED            0x0001
#define LM_TX_PSM               0x0002
#define LM_TX_PSM_CANCELLED	0x0004

#define LM_SCAN_EXIT            0x0001
#define LM_SCAN_TRAP            0x0002
#define LM_SCAN_ACTIVE          0x0004
#define LM_SCAN_FILTER          0x0008

#define LM_PSM                  0x0001
#define LM_PSM_DTIM             0x0002
#define LM_PSM_MCBC             0x0004
#define LM_PSM_CHECKSUM         0x0008
#define LM_PSM_SKIP_MORE_DATA   0x0010
#define LM_PSM_BEACON_TIMEOUT   0x0020
#define LM_PSM_HFOSLEEP         0x0040
#define LM_PSM_AUTOSWITCH_SLEEP 0x0080
#define	LM_PSM_LPIT		0x0100
#define LM_PSM_BF_UCAST_SKIP    0x0200
#define LM_PSM_BF_MCAST_SKIP    0x0400

/* hfosleep */
#define LM_PSM_SLEEP_OPTION_MASK (LM_PSM_AUTOSWITCH_SLEEP | LM_PSM_HFOSLEEP)
#define LM_PSM_SLEEP_OPTION_SHIFT       6
/* hfosleepend */
#define LM_PSM_BF_OPTION_MASK (LM_PSM_BF_MCAST_SKIP | LM_PSM_BF_UCAST_SKIP)
#define LM_PSM_BF_OPTION_SHIFT  9


#define LM_PRIVACC_WEP          0x01
#define LM_PRIVACC_TKIP         0x02
#define LM_PRIVACC_MICHAEL      0x04
#define LM_PRIVACC_CCX_KP       0x08
#define LM_PRIVACC_CCX_MIC      0x10
#define LM_PRIVACC_AES_CCMP     0x20

/* size of s_lm_descr in words */
#define LM_DESCR_SIZE_WORDS     11

#ifndef __ASSEMBLER__

enum {
    LM_MODE_CLIENT = 0,
    LM_MODE_AP
};

struct s_lm_descr {
    uint16_t modes;
    uint16_t flags;
    uint32_t buffer_start;
    uint32_t buffer_end;
    uint8_t header;
    uint8_t trailer;
    uint8_t tx_queues;
    uint8_t tx_depth;
    uint8_t privacy;
    uint8_t rx_keycache;
    uint8_t tim_size;
    uint8_t pad1;
    uint8_t rates[16];
    uint32_t link;
	uint16_t mtu;
};


struct s_lm_control {
    uint16_t flags;
    uint16_t length;
    uint32_t handle;
    uint16_t oid;
    uint16_t pad;
    /* uint8_t data[]; */
};

enum {
    LM_PRIV_NONE = 0,
    LM_PRIV_WEP,
    LM_PRIV_TKIP,
    LM_PRIV_TKIPMICHAEL,
    LM_PRIV_CCX_WEPMIC,
    LM_PRIV_CCX_KPMIC,
    LM_PRIV_CCX_KP,
    LM_PRIV_AES_CCMP
};

enum {
    LM_DECRYPT_NONE,
    LM_DECRYPT_OK,
    LM_DECRYPT_NOKEY,
    LM_DECRYPT_NOMICHAEL,
    LM_DECRYPT_NOCKIPMIC,
    LM_DECRYPT_FAIL_WEP,
    LM_DECRYPT_FAIL_TKIP,
    LM_DECRYPT_FAIL_MICHAEL,
    LM_DECRYPT_FAIL_CKIPKP,
    LM_DECRYPT_FAIL_CKIPMIC,
    LM_DECRYPT_FAIL_AESCCMP
};

struct s_lm_data_out {
    uint16_t flags;
    uint16_t length;
    uint32_t handle;
    uint16_t aid;
    uint8_t rts_retries;
    uint8_t retries;
    uint8_t aloft[8];
    uint8_t aloft_ctrl;
    uint8_t crypt_offset;
    uint8_t keytype;
    uint8_t keylen;
    uint8_t key[16];
    uint8_t queue;
    uint8_t backlog;
    uint16_t durations[4];
    uint8_t antenna;
    uint8_t cts;
    int16_t power;
    uint8_t pad[2];
    /*uint8_t data[];*/
};

#define LM_RCPI_INVALID         (0xff)

struct s_lm_data_in {
    uint16_t flags;
    uint16_t length;
    uint16_t frequency;
    uint8_t antenna;
    uint8_t rate;
    uint8_t rcpi;
    uint8_t sq;
    uint8_t decrypt;
    uint8_t rssi_raw;
    uint32_t clock[2];
    /*uint8_t data[];*/
};

union u_lm_data {
    struct s_lm_data_out out;
    struct s_lm_data_in in;
};

enum {
    LM_OID_SETUP	= 0,
    LM_OID_SCAN		= 1,
    LM_OID_TRAP		= 2,
    LM_OID_EDCF		= 3,
    LM_OID_KEYCACHE	= 4,
    LM_OID_PSM		= 6,
    LM_OID_TXCANCEL	= 7,
    LM_OID_TX		= 8,
    LM_OID_BURST	= 9,
    LM_OID_STATS	= 10,
    LM_OID_LED		= 13,
    LM_OID_TIMER	= 15,
    LM_OID_NAV		= 20,
    LM_OID_PCS		= 22,
    LM_OID_BT_BALANCER  = 28,
    LM_OID_GROUP_ADDRESS_TABLE	= 30,
    LM_OID_ARPTABLE     = 31,
    LM_OID_BT_OPTIONS = 35
};

enum {
    LM_FRONTEND_UNKNOWN = 0,
    LM_FRONTEND_DUETTE3,
    LM_FRONTEND_DUETTE2,
    LM_FRONTEND_FRISBEE,
    LM_FRONTEND_CROSSBOW,
    LM_FRONTEND_LONGBOW
};


#define INVALID_LPF_BANDWIDTH   0xffff
#define INVALID_OSC_START_DELAY 0xffff

struct s_lmo_setup {
    uint16_t flags;
    uint8_t  macaddr[6];
    uint8_t  bssid[6];
    uint8_t  antenna;
    uint8_t  rx_align;
    uint32_t rx_buffer;
    uint16_t rx_mtu;
    uint16_t frontend;
    uint16_t timeout;
    uint16_t truncate;
    uint32_t bratemask;
    uint8_t  sbss_offset;
    uint8_t  mcast_window;
    uint8_t  rx_rssi_threshold;
    uint8_t  rx_ed_threshold;
    uint32_t ref_clock;
    uint16_t lpf_bandwidth;
    uint16_t osc_start_delay;
};


struct s_lmo_scan {
    uint16_t flags;
    uint16_t dwell;
    uint8_t channel[292];
    uint32_t bratemask;
    uint8_t  aloft[8];
    uint8_t  rssical[8];
};


enum {
    LM_TRAP_SCAN = 0,
    LM_TRAP_TIMER,
    LM_TRAP_BEACON_TX,
    LM_TRAP_FAA_RADIO_ON,
    LM_TRAP_FAA_RADIO_OFF,
    LM_TRAP_RADAR,
    LM_TRAP_NO_BEACON,
    LM_TRAP_TBTT,
    LM_TRAP_SCO_ENTER,
    LM_TRAP_SCO_EXIT
};

struct s_lmo_trap {
    uint16_t event;
    uint16_t frequency;
};

struct s_lmo_timer {
    uint32_t interval;
};

struct s_lmo_nav {
    uint32_t period;
};


struct s_lmo_edcf_queue;

struct s_lmo_edcf {
    uint8_t  flags;
    uint8_t  slottime;
    uint8_t  sifs;
    uint8_t  eofpad;
    struct s_lmo_edcf_queue {
	    uint8_t  aifs;
	    uint8_t  pad0;
	    uint16_t cwmin;
	    uint16_t cwmax;
	    uint16_t txop;
    } queues[8];
    uint8_t  mapping[4];
    uint16_t maxburst;
    uint16_t round_trip_delay;
};

struct s_lmo_keycache {
    uint8_t entry;
    uint8_t keyid;
    uint8_t address[6];
    uint8_t pad[2];
    uint8_t keytype;
    uint8_t keylen;
    uint8_t key[24];
};


struct s_lm_interval;

struct s_lmo_psm {
    uint16_t    flags;
    uint16_t    aid;
    struct s_lm_interval {
	    uint16_t interval;
	    uint16_t periods;
    } intervals[4];
    /* uint16_t    pad; */
    uint8_t     beacon_rcpi_skip_max;
    uint8_t     rcpi_delta_threshold;
    uint8_t     nr;
    uint8_t     exclude[1];
};

#define MC_FILTER_ADDRESS_NUM   4

struct s_lmo_group_address_table {
    uint16_t    filter_enable;
    uint16_t    num_address;
    uint8_t     macaddr_list[MC_FILTER_ADDRESS_NUM][6];
};

struct s_lmo_txcancel {
    uint32_t address[1];
};


struct s_lmo_tx {
    uint8_t  flags;
    uint8_t  retries;
    uint8_t  rcpi;
    uint8_t  sq;
    uint16_t seqctrl;
    uint8_t  antenna;
    uint8_t  pad;
};

struct s_lmo_burst {
    uint8_t  flags;
    uint8_t  queue;
    uint8_t  backlog;
    uint8_t  pad;
    uint16_t durations[32];
};

struct s_lmo_stats {
    uint32_t valid;
    uint32_t fcs;
    uint32_t abort;
    uint32_t phyabort;
    uint32_t rts_success;
    uint32_t rts_fail;
    uint32_t timestamp;
    uint32_t time_tx;
    uint32_t noisefloor;
    uint32_t sample_noise[8];
    uint32_t sample_cca;
    uint32_t sample_tx;
};


struct s_lmo_led {
    uint16_t flags;
    uint16_t mask[2];
    uint16_t delay/*[2]*/;
};


struct s_lmo_bt_balancer {
    uint16_t prio_thresh;
    uint16_t acl_thresh;
};


struct s_lmo_arp_table {
    uint16_t    filter_enable;
    uint32_t    ipaddr;
};

#endif /* __ASSEMBLER__ */

#endif /* __lmac_h__ */
