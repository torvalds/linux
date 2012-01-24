/*
 * Custom OID/ioctl definitions for
 * Broadcom 802.11abg Networking Device Driver
 *
 * Definitions subject to change without notice.
 *
 * Copyright (C) 1999-2011, Broadcom Corporation
 * 
 *         Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: wlioctl.h 302303 2011-12-10 07:51:07Z $
 */


#ifndef _wlioctl_h_
#define _wlioctl_h_

#include <typedefs.h>
#include <proto/ethernet.h>
#include <proto/bcmeth.h>
#include <proto/bcmevent.h>
#include <proto/802.11.h>
#include <bcmwifi.h>

#include <bcmcdc.h>

#ifndef INTF_NAME_SIZ
#define INTF_NAME_SIZ   16
#endif


typedef struct remote_ioctl {
	cdc_ioctl_t     msg;
	uint        data_len;
	char            intf_name[INTF_NAME_SIZ];
} rem_ioctl_t;
#define REMOTE_SIZE sizeof(rem_ioctl_t)

#define ACTION_FRAME_SIZE 1040

typedef struct wl_action_frame {
	struct ether_addr   da;
	uint16          len;
	uint32          packetId;
	uint8           data[ACTION_FRAME_SIZE];
} wl_action_frame_t;

#define WL_WIFI_ACTION_FRAME_SIZE sizeof(struct wl_action_frame)

typedef struct ssid_info
{
	uint8		ssid_len;	
	uint8		ssid[32];	
} ssid_info_t;

typedef struct wl_af_params {
	uint32          channel;
	int32           dwell_time;
	struct ether_addr   BSSID;
	wl_action_frame_t   action_frame;
} wl_af_params_t;

#define WL_WIFI_AF_PARAMS_SIZE sizeof(struct wl_af_params)


#define BWL_DEFAULT_PACKING
#include <packed_section_start.h>





#define LEGACY2_WL_BSS_INFO_VERSION 108     


typedef struct wl_bss_info_108 {
	uint32      version;        
	uint32      length;         
	struct ether_addr BSSID;
	uint16      beacon_period;      
	uint16      capability;     
	uint8       SSID_len;
	uint8       SSID[32];
	struct {
		uint    count;          
		uint8   rates[16];      
	} rateset;              
	chanspec_t  chanspec;       
	uint16      atim_window;        
	uint8       dtim_period;        
	int16       RSSI;           
	int8        phy_noise;      

	uint8       n_cap;          
	uint32      nbss_cap;       
	uint8       ctl_ch;         
	uint32      reserved32[1];      
	uint8       flags;          
	uint8       reserved[3];        
	uint8       basic_mcs[MCSSET_LEN];  

	uint16      ie_offset;      
	uint32      ie_length;      
	
	
} wl_bss_info_108_t;

#define WL_BSS_INFO_VERSION 109     


typedef struct wl_bss_info {
	uint32      version;        
	uint32      length;         
	struct ether_addr BSSID;
	uint16      beacon_period;      
	uint16      capability;     
	uint8       SSID_len;
	uint8       SSID[32];
	struct {
		uint    count;          
		uint8   rates[16];      
	} rateset;              
	chanspec_t  chanspec;       
	uint16      atim_window;        
	uint8       dtim_period;        
	int16       RSSI;           
	int8        phy_noise;      

	uint8       n_cap;          
	uint32      nbss_cap;       
	uint8       ctl_ch;         
	uint32      reserved32[1];      
	uint8       flags;          
	uint8       reserved[3];        
	uint8       basic_mcs[MCSSET_LEN];  

	uint16      ie_offset;      
	uint32      ie_length;      
	int16       SNR;            
	
	
} wl_bss_info_t;

typedef struct wl_bsscfg {
	uint32  wsec;
	uint32  WPA_auth;
	uint32  wsec_index;
	uint32  associated;
	uint32  BSS;
	uint32  phytest_on;
	struct ether_addr   prev_BSSID;
	struct ether_addr   BSSID;
} wl_bsscfg_t;

typedef struct wl_bss_config {
	uint32  atim_window;
	uint32  beacon_period;
	uint32  chanspec;
} wl_bss_config_t;


typedef struct wlc_ssid {
	uint32      SSID_len;
	uchar       SSID[32];
} wlc_ssid_t;


#define WL_BSSTYPE_INFRA 1
#define WL_BSSTYPE_INDEP 0
#define WL_BSSTYPE_ANY   2


#define WL_SCANFLAGS_PASSIVE 0x01       
#define WL_SCANFLAGS_RESERVED 0x02      
#define WL_SCANFLAGS_PROHIBITED 0x04    

#define WL_SCAN_PARAMS_SSID_MAX 	10

typedef struct wl_scan_params {
	wlc_ssid_t ssid;        
	struct ether_addr bssid;    
	int8 bss_type;          
	uint8 scan_type;        
	int32 nprobes;          
	int32 active_time;      
	int32 passive_time;     
	int32 home_time;        
	int32 channel_num;      
	uint16 channel_list[1];     
} wl_scan_params_t;


#define WL_SCAN_PARAMS_FIXED_SIZE 64


#define WL_SCAN_PARAMS_COUNT_MASK 0x0000ffff
#define WL_SCAN_PARAMS_NSSID_SHIFT 16

#define WL_SCAN_ACTION_START      1
#define WL_SCAN_ACTION_CONTINUE   2
#define WL_SCAN_ACTION_ABORT      3

#define ISCAN_REQ_VERSION 1


typedef struct wl_iscan_params {
	uint32 version;
	uint16 action;
	uint16 scan_duration;
	wl_scan_params_t params;
} wl_iscan_params_t;


#define WL_ISCAN_PARAMS_FIXED_SIZE (OFFSETOF(wl_iscan_params_t, params) + sizeof(wlc_ssid_t))

typedef struct wl_scan_results {
	uint32 buflen;
	uint32 version;
	uint32 count;
	wl_bss_info_t bss_info[1];
} wl_scan_results_t;


#define WL_SCAN_RESULTS_FIXED_SIZE (sizeof(wl_scan_results_t) - sizeof(wl_bss_info_t))


#define WL_SCAN_RESULTS_SUCCESS 0
#define WL_SCAN_RESULTS_PARTIAL 1
#define WL_SCAN_RESULTS_PENDING 2
#define WL_SCAN_RESULTS_ABORTED 3
#define WL_SCAN_RESULTS_NO_MEM  4


#define DNGL_RXCTXT_SIZE	45

#if defined(SIMPLE_ISCAN)
#define ISCAN_RETRY_CNT   5
#define ISCAN_STATE_IDLE   0
#define ISCAN_STATE_SCANING 1
#define ISCAN_STATE_PENDING 2


#define WLC_IW_ISCAN_MAXLEN   2048
typedef struct iscan_buf {
	struct iscan_buf * next;
	char   iscan_buf[WLC_IW_ISCAN_MAXLEN];
} iscan_buf_t;
#endif 

#define ESCAN_REQ_VERSION 1

typedef struct wl_escan_params {
	uint32 version;
	uint16 action;
	uint16 sync_id;
	wl_scan_params_t params;
} wl_escan_params_t;

#define WL_ESCAN_PARAMS_FIXED_SIZE (OFFSETOF(wl_escan_params_t, params) + sizeof(wlc_ssid_t))

typedef struct wl_escan_result {
	uint32 buflen;
	uint32 version;
	uint16 sync_id;
	uint16 bss_count;
	wl_bss_info_t bss_info[1];
} wl_escan_result_t;

#define WL_ESCAN_RESULTS_FIXED_SIZE (sizeof(wl_escan_result_t) - sizeof(wl_bss_info_t))


typedef struct wl_iscan_results {
	uint32 status;
	wl_scan_results_t results;
} wl_iscan_results_t;


#define WL_ISCAN_RESULTS_FIXED_SIZE \
	(WL_SCAN_RESULTS_FIXED_SIZE + OFFSETOF(wl_iscan_results_t, results))

typedef struct wl_probe_params {
	wlc_ssid_t ssid;
	struct ether_addr bssid;
	struct ether_addr mac;
} wl_probe_params_t;

#define WL_NUMRATES     16  
typedef struct wl_rateset {
	uint32  count;          
	uint8   rates[WL_NUMRATES]; 
} wl_rateset_t;

typedef struct wl_rateset_args {
	uint32  count;          
	uint8   rates[WL_NUMRATES]; 
	uint8   mcs[MCSSET_LEN];        
} wl_rateset_args_t;


typedef struct wl_uint32_list {
	
	uint32 count;
	
	uint32 element[1];
} wl_uint32_list_t;


typedef struct wl_assoc_params {
	struct ether_addr bssid;    
	uint16 bssid_cnt;       
	int32 chanspec_num;     
	chanspec_t chanspec_list[1];    
} wl_assoc_params_t;
#define WL_ASSOC_PARAMS_FIXED_SIZE  (sizeof(wl_assoc_params_t) - sizeof(chanspec_t))


typedef wl_assoc_params_t wl_reassoc_params_t;
#define WL_REASSOC_PARAMS_FIXED_SIZE    WL_ASSOC_PARAMS_FIXED_SIZE


typedef wl_assoc_params_t wl_join_assoc_params_t;
#define WL_JOIN_ASSOC_PARAMS_FIXED_SIZE        WL_ASSOC_PARAMS_FIXED_SIZE


typedef struct wl_join_params {
	wlc_ssid_t ssid;
	wl_assoc_params_t params;   
} wl_join_params_t;
#define WL_JOIN_PARAMS_FIXED_SIZE   (sizeof(wl_join_params_t) - sizeof(chanspec_t))


typedef struct wl_join_scan_params {
	uint8 scan_type;                
	int32 nprobes;                  
	int32 active_time;              
	int32 passive_time;             
	int32 home_time;                
} wl_join_scan_params_t;


typedef struct wl_extjoin_params {
	wlc_ssid_t ssid;                
	wl_join_scan_params_t scan;
	wl_join_assoc_params_t assoc;   
} wl_extjoin_params_t;
#define WL_EXTJOIN_PARAMS_FIXED_SIZE    (sizeof(wl_extjoin_params_t) - sizeof(chanspec_t))

typedef struct {
	uint32 num;
	chanspec_t list[1];
} chanspec_list_t;


#define NRATE_MCS_INUSE 0x00000080  
#define NRATE_RATE_MASK 0x0000007f  
#define NRATE_STF_MASK  0x0000ff00  
#define NRATE_STF_SHIFT 8       
#define NRATE_OVERRIDE  0x80000000  
#define NRATE_OVERRIDE_MCS_ONLY 0x40000000 
#define NRATE_SGI_MASK  0x00800000      
#define NRATE_SGI_SHIFT 23              
#define NRATE_LDPC_CODING 0x00400000    
#define NRATE_LDPC_SHIFT 22             
#define NRATE_BCMC_OVERRIDE 0x00200000    
#define NRATE_BCMC_SHIFT 21             

#define NRATE_STF_SISO  0       
#define NRATE_STF_CDD   1       
#define NRATE_STF_STBC  2       
#define NRATE_STF_SDM   3       

#define ANTENNA_NUM_1   1       
#define ANTENNA_NUM_2   2
#define ANTENNA_NUM_3   3
#define ANTENNA_NUM_4   4

#define ANT_SELCFG_AUTO     0x80    
#define ANT_SELCFG_MASK     0x33    
#define ANT_SELCFG_MAX      4   
#define ANT_SELCFG_TX_UNICAST   0   
#define ANT_SELCFG_RX_UNICAST   1   
#define ANT_SELCFG_TX_DEF   2   
#define ANT_SELCFG_RX_DEF   3   

#define MAX_STREAMS_SUPPORTED   4   

typedef struct {
	uint8 ant_config[ANT_SELCFG_MAX];   
	uint8 num_antcfg;   
} wlc_antselcfg_t;

#define HIGHEST_SINGLE_STREAM_MCS   7 

#define MAX_CCA_CHANNELS 38 
#define MAX_CCA_SECS     60 

#define IBSS_MED        15  
#define IBSS_HI         25  
#define OBSS_MED        12
#define OBSS_HI         25
#define INTERFER_MED    5
#define INTERFER_HI     10

#define  CCA_FLAG_2G_ONLY       0x01    
#define  CCA_FLAG_5G_ONLY       0x02    
#define  CCA_FLAG_IGNORE_DURATION   0x04    
#define  CCA_FLAGS_PREFER_1_6_11    0x10
#define  CCA_FLAG_IGNORE_INTERFER   0x20 

#define CCA_ERRNO_BAND      1   
#define CCA_ERRNO_DURATION  2   
#define CCA_ERRNO_PREF_CHAN 3   
#define CCA_ERRNO_INTERFER  4   
#define CCA_ERRNO_TOO_FEW   5   

typedef struct {
	uint32 duration;    
	uint32 congest_ibss;    
				
	uint32 congest_obss;    
	uint32 interference;    
	uint32 timestamp;   
} cca_congest_t;

typedef struct {
	chanspec_t chanspec;    
	uint8 num_secs;     
	cca_congest_t  secs[1]; 
} cca_congest_channel_req_t;

#define WLC_CNTRY_BUF_SZ    4       

typedef struct wl_country {
	char country_abbrev[WLC_CNTRY_BUF_SZ];  
	int32 rev;              
	char ccode[WLC_CNTRY_BUF_SZ];       
} wl_country_t;

typedef struct wl_channels_in_country {
	uint32 buflen;
	uint32 band;
	char country_abbrev[WLC_CNTRY_BUF_SZ];
	uint32 count;
	uint32 channel[1];
} wl_channels_in_country_t;

typedef struct wl_country_list {
	uint32 buflen;
	uint32 band_set;
	uint32 band;
	uint32 count;
	char country_abbrev[1];
} wl_country_list_t;

#define WL_NUM_RPI_BINS     8
#define WL_RM_TYPE_BASIC    1
#define WL_RM_TYPE_CCA      2
#define WL_RM_TYPE_RPI      3

#define WL_RM_FLAG_PARALLEL (1<<0)

#define WL_RM_FLAG_LATE     (1<<1)
#define WL_RM_FLAG_INCAPABLE    (1<<2)
#define WL_RM_FLAG_REFUSED  (1<<3)

typedef struct wl_rm_req_elt {
	int8    type;
	int8    flags;
	chanspec_t  chanspec;
	uint32  token;      
	uint32  tsf_h;      
	uint32  tsf_l;      
	uint32  dur;        
} wl_rm_req_elt_t;

typedef struct wl_rm_req {
	uint32  token;      
	uint32  count;      
	void    *cb;        
	void    *cb_arg;    
	wl_rm_req_elt_t req[1]; 
} wl_rm_req_t;
#define WL_RM_REQ_FIXED_LEN OFFSETOF(wl_rm_req_t, req)

typedef struct wl_rm_rep_elt {
	int8    type;
	int8    flags;
	chanspec_t  chanspec;
	uint32  token;      
	uint32  tsf_h;      
	uint32  tsf_l;      
	uint32  dur;        
	uint32  len;        
	uint8   data[1];    
} wl_rm_rep_elt_t;
#define WL_RM_REP_ELT_FIXED_LEN 24  

#define WL_RPI_REP_BIN_NUM 8
typedef struct wl_rm_rpi_rep {
	uint8   rpi[WL_RPI_REP_BIN_NUM];
	int8    rpi_max[WL_RPI_REP_BIN_NUM];
} wl_rm_rpi_rep_t;

typedef struct wl_rm_rep {
	uint32  token;      
	uint32  len;        
	wl_rm_rep_elt_t rep[1]; 
} wl_rm_rep_t;
#define WL_RM_REP_FIXED_LEN 8


typedef enum sup_auth_status {
	
	WLC_SUP_DISCONNECTED = 0,
	WLC_SUP_CONNECTING,
	WLC_SUP_IDREQUIRED,
	WLC_SUP_AUTHENTICATING,
	WLC_SUP_AUTHENTICATED,
	WLC_SUP_KEYXCHANGE,
	WLC_SUP_KEYED,
	WLC_SUP_TIMEOUT,
	WLC_SUP_LAST_BASIC_STATE,

	
	
	WLC_SUP_KEYXCHANGE_WAIT_M1 = WLC_SUP_AUTHENTICATED,
	
	WLC_SUP_KEYXCHANGE_PREP_M2 = WLC_SUP_KEYXCHANGE,
	
	WLC_SUP_KEYXCHANGE_WAIT_M3 = WLC_SUP_LAST_BASIC_STATE,
	WLC_SUP_KEYXCHANGE_PREP_M4, 
	WLC_SUP_KEYXCHANGE_WAIT_G1, 
	WLC_SUP_KEYXCHANGE_PREP_G2  
} sup_auth_status_t;


#define CRYPTO_ALGO_OFF         0
#define CRYPTO_ALGO_WEP1        1
#define CRYPTO_ALGO_TKIP        2
#define CRYPTO_ALGO_WEP128      3
#define CRYPTO_ALGO_AES_CCM     4
#define CRYPTO_ALGO_AES_OCB_MSDU    5
#define CRYPTO_ALGO_AES_OCB_MPDU    6
#define CRYPTO_ALGO_NALG        7
#define CRYPTO_ALGO_PMK			12	

#define WSEC_GEN_MIC_ERROR  0x0001
#define WSEC_GEN_REPLAY     0x0002
#define WSEC_GEN_ICV_ERROR  0x0004

#define WL_SOFT_KEY (1 << 0)    
#define WL_PRIMARY_KEY  (1 << 1)    
#define WL_KF_RES_4 (1 << 4)    
#define WL_KF_RES_5 (1 << 5)    
#define WL_IBSS_PEER_GROUP_KEY  (1 << 6)    

typedef struct wl_wsec_key {
	uint32      index;      
	uint32      len;        
	uint8       data[DOT11_MAX_KEY_SIZE];   
	uint32      pad_1[18];
	uint32      algo;       
	uint32      flags;      
	uint32      pad_2[2];
	int     pad_3;
	int     iv_initialized; 
	int     pad_4;
	
	struct {
		uint32  hi;     
		uint16  lo;     
	} rxiv;
	uint32      pad_5[2];
	struct ether_addr ea;       
} wl_wsec_key_t;

#define WSEC_MIN_PSK_LEN    8
#define WSEC_MAX_PSK_LEN    64


#define WSEC_PASSPHRASE     (1<<0)


typedef struct {
	ushort  key_len;        
	ushort  flags;          
	uint8   key[WSEC_MAX_PSK_LEN];  
} wsec_pmk_t;


#define WEP_ENABLED     0x0001
#define TKIP_ENABLED        0x0002
#define AES_ENABLED     0x0004
#define WSEC_SWFLAG     0x0008
#define SES_OW_ENABLED      0x0040  


#define WPA_AUTH_DISABLED   0x0000  
#define WPA_AUTH_NONE       0x0001  
#define WPA_AUTH_UNSPECIFIED    0x0002  
#define WPA_AUTH_PSK        0x0004  
 
#define WPA2_AUTH_UNSPECIFIED   0x0040  
#define WPA2_AUTH_PSK       0x0080  
#define BRCM_AUTH_PSK           0x0100  
#define BRCM_AUTH_DPT       0x0200  
#define WPA2_AUTH_MFP           0x1000  
#define WPA2_AUTH_TPK		0x2000 	
#define WPA2_AUTH_FT		0x4000 	


#define MAXPMKID        16

typedef struct _pmkid {
	struct ether_addr   BSSID;
	uint8           PMKID[WPA2_PMKID_LEN];
} pmkid_t;

typedef struct _pmkid_list {
	uint32  npmkid;
	pmkid_t pmkid[1];
} pmkid_list_t;

typedef struct _pmkid_cand {
	struct ether_addr   BSSID;
	uint8           preauth;
} pmkid_cand_t;

typedef struct _pmkid_cand_list {
	uint32  npmkid_cand;
	pmkid_cand_t    pmkid_cand[1];
} pmkid_cand_list_t;

typedef struct wl_assoc_info {
	uint32      req_len;
	uint32      resp_len;
	uint32      flags;
	struct dot11_assoc_req req;
	struct ether_addr reassoc_bssid;	
	struct dot11_assoc_resp resp;
} wl_assoc_info_t;


#define WLC_ASSOC_REQ_IS_REASSOC 0x01	


typedef struct {
	uint16          ver;        
	uint16          len;        
	uint16          cap;        
	uint32          flags;      
	uint32          idle;       
	struct ether_addr   ea;     
	wl_rateset_t        rateset;    
	uint32          in;     
	uint32          listen_interval_inms; 
	uint32          tx_pkts;    
	uint32          tx_failures;    
	uint32          rx_ucast_pkts;  
	uint32          rx_mcast_pkts;  
	uint32          tx_rate;    
	uint32          rx_rate;    
	uint32          rx_decrypt_succeeds;    
	uint32          rx_decrypt_failures;    
} sta_info_t;

#define WL_OLD_STAINFO_SIZE OFFSETOF(sta_info_t, tx_pkts)

#define WL_STA_VER      3


#define WL_STA_BRCM     0x1     
#define WL_STA_WME      0x2     
#define WL_STA_ABCAP        0x4
#define WL_STA_AUTHE        0x8     
#define WL_STA_ASSOC        0x10        
#define WL_STA_AUTHO        0x20        
#define WL_STA_WDS      0x40        
#define WL_STA_WDS_LINKUP   0x80        
#define WL_STA_PS       0x100       
#define WL_STA_APSD_BE      0x200       
#define WL_STA_APSD_BK      0x400       
#define WL_STA_APSD_VI      0x800       
#define WL_STA_APSD_VO      0x1000      
#define WL_STA_N_CAP        0x2000      
#define WL_STA_SCBSTATS     0x4000      

#define WL_WDS_LINKUP       WL_STA_WDS_LINKUP   


#define WLC_TXFILTER_OVERRIDE_DISABLED  0
#define WLC_TXFILTER_OVERRIDE_ENABLED   1


typedef struct {
	uint32  val;
	struct ether_addr ea;
} scb_val_t;


typedef struct {
	uint32 code;
	scb_val_t ioctl_args;
} authops_t;


typedef struct channel_info {
	int hw_channel;
	int target_channel;
	int scan_channel;
} channel_info_t;


struct maclist {
	uint count;         
	struct ether_addr ea[1];    
};


typedef struct get_pktcnt {
	uint rx_good_pkt;
	uint rx_bad_pkt;
	uint tx_good_pkt;
	uint tx_bad_pkt;
	uint rx_ocast_good_pkt; 
} get_pktcnt_t;

#define WL_IOCTL_ACTION_GET				0x0
#define WL_IOCTL_ACTION_SET				0x1
#define WL_IOCTL_ACTION_OVL_IDX_MASK	0x1e
#define WL_IOCTL_ACTION_OVL_RSV			0x20
#define WL_IOCTL_ACTION_OVL				0x40
#define WL_IOCTL_ACTION_MASK			0x7e
#define WL_IOCTL_ACTION_OVL_SHIFT		1


typedef struct wl_ioctl {
	uint cmd;   
	void *buf;  
	uint len;   
	uint8 set;		
	uint used;  
	uint needed;    
} wl_ioctl_t;


#define ioctl_subtype   set     
#define ioctl_pid   used        
#define ioctl_status    needed      


typedef struct wlc_rev_info {
	uint        vendorid;   
	uint        deviceid;   
	uint        radiorev;   
	uint        chiprev;    
	uint        corerev;    
	uint        boardid;    
	uint        boardvendor;    
	uint        boardrev;   
	uint        driverrev;  
	uint        ucoderev;   
	uint        bus;        
	uint        chipnum;    
	uint        phytype;    
	uint        phyrev;     
	uint        anarev;     
	uint        chippkg;    
} wlc_rev_info_t;

#define WL_REV_INFO_LEGACY_LENGTH   48

#define WL_BRAND_MAX 10
typedef struct wl_instance_info {
	uint instance;
	char brand[WL_BRAND_MAX];
} wl_instance_info_t;


typedef struct wl_txfifo_sz {
	uint16  magic;
	uint16  fifo;
	uint16  size;
} wl_txfifo_sz_t;

#define WL_TXFIFO_SZ_MAGIC  0xa5a5



#define WLC_IOV_NAME_LEN 30
typedef struct wlc_iov_trx_s {
	uint8 module;
	uint8 type;
	char name[WLC_IOV_NAME_LEN];
} wlc_iov_trx_t;


#define WLC_IOCTL_MAGIC     0x14e46c77


#define WLC_IOCTL_VERSION   1

#define WLC_IOCTL_MAXLEN        8192    
#define WLC_IOCTL_SMLEN         256 
#define WLC_IOCTL_MEDLEN        1536    
#ifdef WLC_HIGH_ONLY
#define WLC_SAMPLECOLLECT_MAXLEN    1024    
#else
#define WLC_SAMPLECOLLECT_MAXLEN    10240   
#endif 


#define WLC_GET_MAGIC               0
#define WLC_GET_VERSION             1
#define WLC_UP                  2
#define WLC_DOWN                3
#define WLC_GET_LOOP                4
#define WLC_SET_LOOP                5
#define WLC_DUMP                6
#define WLC_GET_MSGLEVEL            7
#define WLC_SET_MSGLEVEL            8
#define WLC_GET_PROMISC             9
#define WLC_SET_PROMISC             10
#define WLC_OVERLAY_IOCTL           11
#define WLC_GET_RATE                12
 
#define WLC_GET_INSTANCE            14
 
 
 
 
#define WLC_GET_INFRA               19
#define WLC_SET_INFRA               20
#define WLC_GET_AUTH                21
#define WLC_SET_AUTH                22
#define WLC_GET_BSSID               23
#define WLC_SET_BSSID               24
#define WLC_GET_SSID                25
#define WLC_SET_SSID                26
#define WLC_RESTART             27
#define WLC_TERMINATED             28
 
#define WLC_GET_CHANNEL             29
#define WLC_SET_CHANNEL             30
#define WLC_GET_SRL             31
#define WLC_SET_SRL             32
#define WLC_GET_LRL             33
#define WLC_SET_LRL             34
#define WLC_GET_PLCPHDR             35
#define WLC_SET_PLCPHDR             36
#define WLC_GET_RADIO               37
#define WLC_SET_RADIO               38
#define WLC_GET_PHYTYPE             39
#define WLC_DUMP_RATE               40
#define WLC_SET_RATE_PARAMS         41
#define WLC_GET_FIXRATE             42
#define WLC_SET_FIXRATE             43
 
 
#define WLC_GET_KEY             44
#define WLC_SET_KEY             45
#define WLC_GET_REGULATORY          46
#define WLC_SET_REGULATORY          47
#define WLC_GET_PASSIVE_SCAN            48
#define WLC_SET_PASSIVE_SCAN            49
#define WLC_SCAN                50
#define WLC_SCAN_RESULTS            51
#define WLC_DISASSOC                52
#define WLC_REASSOC             53
#define WLC_GET_ROAM_TRIGGER            54
#define WLC_SET_ROAM_TRIGGER            55
#define WLC_GET_ROAM_DELTA          56
#define WLC_SET_ROAM_DELTA          57
#define WLC_GET_ROAM_SCAN_PERIOD        58
#define WLC_SET_ROAM_SCAN_PERIOD        59
#define WLC_EVM                 60  
#define WLC_GET_TXANT               61
#define WLC_SET_TXANT               62
#define WLC_GET_ANTDIV              63
#define WLC_SET_ANTDIV              64
 
 
#define WLC_GET_CLOSED              67
#define WLC_SET_CLOSED              68
#define WLC_GET_MACLIST             69
#define WLC_SET_MACLIST             70
#define WLC_GET_RATESET             71
#define WLC_SET_RATESET             72
 
#define WLC_LONGTRAIN               74
#define WLC_GET_BCNPRD              75
#define WLC_SET_BCNPRD              76
#define WLC_GET_DTIMPRD             77
#define WLC_SET_DTIMPRD             78
#define WLC_GET_SROM                79
#define WLC_SET_SROM                80
#define WLC_GET_WEP_RESTRICT            81
#define WLC_SET_WEP_RESTRICT            82
#define WLC_GET_COUNTRY             83
#define WLC_SET_COUNTRY             84
#define WLC_GET_PM              85
#define WLC_SET_PM              86
#define WLC_GET_WAKE                87
#define WLC_SET_WAKE                88
 
#define WLC_GET_FORCELINK           90  
#define WLC_SET_FORCELINK           91  
#define WLC_FREQ_ACCURACY           92  
#define WLC_CARRIER_SUPPRESS            93  
#define WLC_GET_PHYREG              94
#define WLC_SET_PHYREG              95
#define WLC_GET_RADIOREG            96
#define WLC_SET_RADIOREG            97
#define WLC_GET_REVINFO             98
#define WLC_GET_UCANTDIV            99
#define WLC_SET_UCANTDIV            100
#define WLC_R_REG               101
#define WLC_W_REG               102

 
#define WLC_GET_MACMODE             105
#define WLC_SET_MACMODE             106
#define WLC_GET_MONITOR             107
#define WLC_SET_MONITOR             108
#define WLC_GET_GMODE               109
#define WLC_SET_GMODE               110
#define WLC_GET_LEGACY_ERP          111
#define WLC_SET_LEGACY_ERP          112
#define WLC_GET_RX_ANT              113
#define WLC_GET_CURR_RATESET            114 
#define WLC_GET_SCANSUPPRESS            115
#define WLC_SET_SCANSUPPRESS            116
#define WLC_GET_AP              117
#define WLC_SET_AP              118
#define WLC_GET_EAP_RESTRICT            119
#define WLC_SET_EAP_RESTRICT            120
#define WLC_SCB_AUTHORIZE           121
#define WLC_SCB_DEAUTHORIZE         122
#define WLC_GET_WDSLIST             123
#define WLC_SET_WDSLIST             124
#define WLC_GET_ATIM                125
#define WLC_SET_ATIM                126
#define WLC_GET_RSSI                127
#define WLC_GET_PHYANTDIV           128
#define WLC_SET_PHYANTDIV           129
#define WLC_AP_RX_ONLY              130
#define WLC_GET_TX_PATH_PWR         131
#define WLC_SET_TX_PATH_PWR         132
#define WLC_GET_WSEC                133
#define WLC_SET_WSEC                134
#define WLC_GET_PHY_NOISE           135
#define WLC_GET_BSS_INFO            136
#define WLC_GET_PKTCNTS             137
#define WLC_GET_LAZYWDS             138
#define WLC_SET_LAZYWDS             139
#define WLC_GET_BANDLIST            140
#define WLC_GET_BAND                141
#define WLC_SET_BAND                142
#define WLC_SCB_DEAUTHENTICATE          143
#define WLC_GET_SHORTSLOT           144
#define WLC_GET_SHORTSLOT_OVERRIDE      145
#define WLC_SET_SHORTSLOT_OVERRIDE      146
#define WLC_GET_SHORTSLOT_RESTRICT      147
#define WLC_SET_SHORTSLOT_RESTRICT      148
#define WLC_GET_GMODE_PROTECTION        149
#define WLC_GET_GMODE_PROTECTION_OVERRIDE   150
#define WLC_SET_GMODE_PROTECTION_OVERRIDE   151
#define WLC_UPGRADE             152
 
 
#define WLC_GET_IGNORE_BCNS         155
#define WLC_SET_IGNORE_BCNS         156
#define WLC_GET_SCB_TIMEOUT         157
#define WLC_SET_SCB_TIMEOUT         158
#define WLC_GET_ASSOCLIST           159
#define WLC_GET_CLK             160
#define WLC_SET_CLK             161
#define WLC_GET_UP              162
#define WLC_OUT                 163
#define WLC_GET_WPA_AUTH            164
#define WLC_SET_WPA_AUTH            165
#define WLC_GET_UCFLAGS             166
#define WLC_SET_UCFLAGS             167
#define WLC_GET_PWRIDX              168
#define WLC_SET_PWRIDX              169
#define WLC_GET_TSSI                170
#define WLC_GET_SUP_RATESET_OVERRIDE        171
#define WLC_SET_SUP_RATESET_OVERRIDE        172
 
 
 
 
 
#define WLC_GET_PROTECTION_CONTROL      178
#define WLC_SET_PROTECTION_CONTROL      179
#define WLC_GET_PHYLIST             180
#define WLC_ENCRYPT_STRENGTH            181 
#define WLC_DECRYPT_STATUS          182 
#define WLC_GET_KEY_SEQ             183
#define WLC_GET_SCAN_CHANNEL_TIME       184
#define WLC_SET_SCAN_CHANNEL_TIME       185
#define WLC_GET_SCAN_UNASSOC_TIME       186
#define WLC_SET_SCAN_UNASSOC_TIME       187
#define WLC_GET_SCAN_HOME_TIME          188
#define WLC_SET_SCAN_HOME_TIME          189
#define WLC_GET_SCAN_NPROBES            190
#define WLC_SET_SCAN_NPROBES            191
#define WLC_GET_PRB_RESP_TIMEOUT        192
#define WLC_SET_PRB_RESP_TIMEOUT        193
#define WLC_GET_ATTEN               194
#define WLC_SET_ATTEN               195
#define WLC_GET_SHMEM               196 
#define WLC_SET_SHMEM               197 
 
 
#define WLC_SET_WSEC_TEST           200
#define WLC_SCB_DEAUTHENTICATE_FOR_REASON   201
#define WLC_TKIP_COUNTERMEASURES        202
#define WLC_GET_PIOMODE             203
#define WLC_SET_PIOMODE             204
#define WLC_SET_ASSOC_PREFER            205
#define WLC_GET_ASSOC_PREFER            206
#define WLC_SET_ROAM_PREFER         207
#define WLC_GET_ROAM_PREFER         208
#define WLC_SET_LED             209
#define WLC_GET_LED             210
#define WLC_GET_INTERFERENCE_MODE       211
#define WLC_SET_INTERFERENCE_MODE       212
#define WLC_GET_CHANNEL_QA          213
#define WLC_START_CHANNEL_QA            214
#define WLC_GET_CHANNEL_SEL         215
#define WLC_START_CHANNEL_SEL           216
#define WLC_GET_VALID_CHANNELS          217
#define WLC_GET_FAKEFRAG            218
#define WLC_SET_FAKEFRAG            219
#define WLC_GET_PWROUT_PERCENTAGE       220
#define WLC_SET_PWROUT_PERCENTAGE       221
#define WLC_SET_BAD_FRAME_PREEMPT       222
#define WLC_GET_BAD_FRAME_PREEMPT       223
#define WLC_SET_LEAP_LIST           224
#define WLC_GET_LEAP_LIST           225
#define WLC_GET_CWMIN               226
#define WLC_SET_CWMIN               227
#define WLC_GET_CWMAX               228
#define WLC_SET_CWMAX               229
#define WLC_GET_WET             230
#define WLC_SET_WET             231
#define WLC_GET_PUB             232
 
 
#define WLC_GET_KEY_PRIMARY         235
#define WLC_SET_KEY_PRIMARY         236
 
#define WLC_GET_ACI_ARGS            238
#define WLC_SET_ACI_ARGS            239
#define WLC_UNSET_CALLBACK          240
#define WLC_SET_CALLBACK            241
#define WLC_GET_RADAR               242
#define WLC_SET_RADAR               243
#define WLC_SET_SPECT_MANAGMENT         244
#define WLC_GET_SPECT_MANAGMENT         245
#define WLC_WDS_GET_REMOTE_HWADDR       246 
#define WLC_WDS_GET_WPA_SUP         247
#define WLC_SET_CS_SCAN_TIMER           248
#define WLC_GET_CS_SCAN_TIMER           249
#define WLC_MEASURE_REQUEST         250
#define WLC_INIT                251
#define WLC_SEND_QUIET              252
#define WLC_KEEPALIVE           253
#define WLC_SEND_PWR_CONSTRAINT         254
#define WLC_UPGRADE_STATUS          255
#define WLC_CURRENT_PWR             256
#define WLC_GET_SCAN_PASSIVE_TIME       257
#define WLC_SET_SCAN_PASSIVE_TIME       258
#define WLC_LEGACY_LINK_BEHAVIOR        259
#define WLC_GET_CHANNELS_IN_COUNTRY     260
#define WLC_GET_COUNTRY_LIST            261
#define WLC_GET_VAR             262 
#define WLC_SET_VAR             263 
#define WLC_NVRAM_GET               264 
#define WLC_NVRAM_SET               265
#define WLC_NVRAM_DUMP              266
#define WLC_REBOOT              267
#define WLC_SET_WSEC_PMK            268
#define WLC_GET_AUTH_MODE           269
#define WLC_SET_AUTH_MODE           270
#define WLC_GET_WAKEENTRY           271
#define WLC_SET_WAKEENTRY           272
#define WLC_NDCONFIG_ITEM           273 
#define WLC_NVOTPW              274
#define WLC_OTPW                275
#define WLC_IOV_BLOCK_GET           276
#define WLC_IOV_MODULES_GET         277
#define WLC_SOFT_RESET              278
#define WLC_GET_ALLOW_MODE          279
#define WLC_SET_ALLOW_MODE          280
#define WLC_GET_DESIRED_BSSID           281
#define WLC_SET_DESIRED_BSSID           282
#define WLC_DISASSOC_MYAP           283
#define WLC_GET_NBANDS              284 
#define WLC_GET_BANDSTATES          285 
#define WLC_GET_WLC_BSS_INFO            286 
#define WLC_GET_ASSOC_INFO          287 
#define WLC_GET_OID_PHY             288 
#define WLC_SET_OID_PHY             289 
#define WLC_SET_ASSOC_TIME          290 
#define WLC_GET_DESIRED_SSID            291 
#define WLC_GET_CHANSPEC            292 
#define WLC_GET_ASSOC_STATE         293 
#define WLC_SET_PHY_STATE           294 
#define WLC_GET_SCAN_PENDING            295 
#define WLC_GET_SCANREQ_PENDING         296 
#define WLC_GET_PREV_ROAM_REASON        297 
#define WLC_SET_PREV_ROAM_REASON        298 
#define WLC_GET_BANDSTATES_PI           299 
#define WLC_GET_PHY_STATE           300 
#define WLC_GET_BSS_WPA_RSN         301 
#define WLC_GET_BSS_WPA2_RSN            302 
#define WLC_GET_BSS_BCN_TS          303 
#define WLC_GET_INT_DISASSOC            304 
#define WLC_SET_NUM_PEERS           305     
#define WLC_GET_NUM_BSS             306 
#define WLC_NPHY_SAMPLE_COLLECT         307 
#define WLC_UM_PRIV             308 
#define WLC_GET_CMD             309
  
#define WLC_SET_INTERFERENCE_OVERRIDE_MODE  311 
#define WLC_GET_INTERFERENCE_OVERRIDE_MODE  312 
#define WLC_GET_WAI_RESTRICT            313 
#define WLC_SET_WAI_RESTRICT            314 
#define WLC_SET_WAI_REKEY           315 
#define WLC_SET_PEAKRATE            316 
#define WLC_GET_PEAKRATE            317
#define WLC_LAST                318

#ifndef EPICTRL_COOKIE
#define EPICTRL_COOKIE      0xABADCEDE
#endif


#define CMN_IOCTL_OFF 0x180




#define WL_OID_BASE     0xFFE41420


#define OID_WL_GETINSTANCE  (WL_OID_BASE + WLC_GET_INSTANCE)
#define OID_WL_GET_FORCELINK    (WL_OID_BASE + WLC_GET_FORCELINK)
#define OID_WL_SET_FORCELINK    (WL_OID_BASE + WLC_SET_FORCELINK)
#define OID_WL_ENCRYPT_STRENGTH (WL_OID_BASE + WLC_ENCRYPT_STRENGTH)
#define OID_WL_DECRYPT_STATUS   (WL_OID_BASE + WLC_DECRYPT_STATUS)
#define OID_LEGACY_LINK_BEHAVIOR (WL_OID_BASE + WLC_LEGACY_LINK_BEHAVIOR)
#define OID_WL_NDCONFIG_ITEM    (WL_OID_BASE + WLC_NDCONFIG_ITEM)


#define OID_STA_CHANSPEC    (WL_OID_BASE + WLC_GET_CHANSPEC)
#define OID_STA_NBANDS      (WL_OID_BASE + WLC_GET_NBANDS)
#define OID_STA_GET_PHY     (WL_OID_BASE + WLC_GET_OID_PHY)
#define OID_STA_SET_PHY     (WL_OID_BASE + WLC_SET_OID_PHY)
#define OID_STA_ASSOC_TIME  (WL_OID_BASE + WLC_SET_ASSOC_TIME)
#define OID_STA_DESIRED_SSID    (WL_OID_BASE + WLC_GET_DESIRED_SSID)
#define OID_STA_SET_PHY_STATE   (WL_OID_BASE + WLC_SET_PHY_STATE)
#define OID_STA_SCAN_PENDING    (WL_OID_BASE + WLC_GET_SCAN_PENDING)
#define OID_STA_SCANREQ_PENDING (WL_OID_BASE + WLC_GET_SCANREQ_PENDING)
#define OID_STA_GET_ROAM_REASON (WL_OID_BASE + WLC_GET_PREV_ROAM_REASON)
#define OID_STA_SET_ROAM_REASON (WL_OID_BASE + WLC_SET_PREV_ROAM_REASON)
#define OID_STA_GET_PHY_STATE   (WL_OID_BASE + WLC_GET_PHY_STATE)
#define OID_STA_INT_DISASSOC    (WL_OID_BASE + WLC_GET_INT_DISASSOC)
#define OID_STA_SET_NUM_PEERS   (WL_OID_BASE + WLC_SET_NUM_PEERS)
#define OID_STA_GET_NUM_BSS (WL_OID_BASE + WLC_GET_NUM_BSS)

#define WL_DECRYPT_STATUS_SUCCESS   1
#define WL_DECRYPT_STATUS_FAILURE   2
#define WL_DECRYPT_STATUS_UNKNOWN   3


#define WLC_UPGRADE_SUCCESS         0
#define WLC_UPGRADE_PENDING         1

#ifdef CONFIG_USBRNDIS_RETAIL

typedef struct {
	char *name;
	void *param;
} ndconfig_item_t;
#endif



#define WL_AUTH_OPEN_SYSTEM     0   
#define WL_AUTH_SHARED_KEY      1   
#define WL_AUTH_OPEN_SHARED		3	


#define WL_RADIO_SW_DISABLE     (1<<0)
#define WL_RADIO_HW_DISABLE     (1<<1)
#define WL_RADIO_MPC_DISABLE        (1<<2)
#define WL_RADIO_COUNTRY_DISABLE    (1<<3)  

#define WL_SPURAVOID_OFF    0
#define WL_SPURAVOID_ON1    1
#define WL_SPURAVOID_ON2    2


#define WL_TXPWR_OVERRIDE   (1U<<31)
#define WL_TXPWR_NEG   (1U<<30)

#define WL_PHY_PAVARS_LEN   6   

#define WL_PHY_PAVARS2_NUM	3	
#define WL_PHY_PAVAR_VER	1	
typedef struct wl_pavars2 {
	uint16 ver;		
	uint16 len;		
	uint16 inuse;		
	uint16 phy_type;	
	uint16 bandrange;
	uint16 chain;
	uint16 inpa[WL_PHY_PAVARS2_NUM];	
} wl_pavars2_t;

typedef struct wl_po {
	uint16  phy_type;   
	uint16  band;
	uint16  cckpo;
	uint32  ofdmpo;
	uint16  mcspo[8];
} wl_po_t;


#define WLC_TXPWR_MAX       (127)   


#define WL_DIAG_INTERRUPT           1   
#define WL_DIAG_LOOPBACK            2   
#define WL_DIAG_MEMORY              3   
#define WL_DIAG_LED             4   
#define WL_DIAG_REG             5   
#define WL_DIAG_SROM                6   
#define WL_DIAG_DMA             7   

#define WL_DIAGERR_SUCCESS          0
#define WL_DIAGERR_FAIL_TO_RUN          1   
#define WL_DIAGERR_NOT_SUPPORTED        2   
#define WL_DIAGERR_INTERRUPT_FAIL       3   
#define WL_DIAGERR_LOOPBACK_FAIL        4   
#define WL_DIAGERR_SROM_FAIL            5   
#define WL_DIAGERR_SROM_BADCRC          6   
#define WL_DIAGERR_REG_FAIL         7   
#define WL_DIAGERR_MEMORY_FAIL          8   
#define WL_DIAGERR_NOMEM            9   
#define WL_DIAGERR_DMA_FAIL         10  

#define WL_DIAGERR_MEMORY_TIMEOUT       11  
#define WL_DIAGERR_MEMORY_BADPATTERN        12  


#define WLC_BAND_AUTO       0   
#define WLC_BAND_5G     1   
#define WLC_BAND_2G     2   
#define WLC_BAND_ALL        3   


#define WL_CHAN_FREQ_RANGE_2G      0
#define WL_CHAN_FREQ_RANGE_5GL     1
#define WL_CHAN_FREQ_RANGE_5GM     2
#define WL_CHAN_FREQ_RANGE_5GH     3

#define WL_CHAN_FREQ_RANGE_5GLL_5BAND    4
#define WL_CHAN_FREQ_RANGE_5GLH_5BAND    5
#define WL_CHAN_FREQ_RANGE_5GML_5BAND    6
#define WL_CHAN_FREQ_RANGE_5GMH_5BAND    7
#define WL_CHAN_FREQ_RANGE_5GH_5BAND     8

#define WL_CHAN_FREQ_RANGE_5G_BAND0     1
#define WL_CHAN_FREQ_RANGE_5G_BAND1     2
#define WL_CHAN_FREQ_RANGE_5G_BAND2     3
#define WL_CHAN_FREQ_RANGE_5G_BAND3     4


#define WLC_PHY_TYPE_A      0
#define WLC_PHY_TYPE_B      1
#define WLC_PHY_TYPE_G      2
#define WLC_PHY_TYPE_N      4
#define WLC_PHY_TYPE_LP     5
#define WLC_PHY_TYPE_SSN    6
#define WLC_PHY_TYPE_HT     7
#define WLC_PHY_TYPE_LCN    8
#define WLC_PHY_TYPE_NULL   0xf


#define WLC_MACMODE_DISABLED    0   
#define WLC_MACMODE_DENY    1   
#define WLC_MACMODE_ALLOW   2   


#define GMODE_LEGACY_B      0
#define GMODE_AUTO      1
#define GMODE_ONLY      2
#define GMODE_B_DEFERRED    3
#define GMODE_PERFORMANCE   4
#define GMODE_LRS       5
#define GMODE_MAX       6


#define WLC_PLCP_AUTO   -1
#define WLC_PLCP_SHORT  0
#define WLC_PLCP_LONG   1


#define WLC_PROTECTION_AUTO     -1
#define WLC_PROTECTION_OFF      0
#define WLC_PROTECTION_ON       1
#define WLC_PROTECTION_MMHDR_ONLY   2
#define WLC_PROTECTION_CTS_ONLY     3


#define WLC_PROTECTION_CTL_OFF      0
#define WLC_PROTECTION_CTL_LOCAL    1
#define WLC_PROTECTION_CTL_OVERLAP  2


#define WLC_N_PROTECTION_OFF        0
#define WLC_N_PROTECTION_OPTIONAL   1
#define WLC_N_PROTECTION_20IN40     2
#define WLC_N_PROTECTION_MIXEDMODE  3


#define WLC_N_PREAMBLE_MIXEDMODE    0
#define WLC_N_PREAMBLE_GF       1
#define WLC_N_PREAMBLE_GF_BRCM          2


#define WLC_N_BW_20ALL          0
#define WLC_N_BW_40ALL          1
#define WLC_N_BW_20IN2G_40IN5G      2


#define WLC_N_TXRX_CHAIN0       0
#define WLC_N_TXRX_CHAIN1       1


#define WLC_N_SGI_20            0x01
#define WLC_N_SGI_40            0x02


#define PM_OFF  0
#define PM_MAX  1
#define PM_FAST 2

#define LISTEN_INTERVAL			10

#define INTERFERE_OVRRIDE_OFF   -1  
#define INTERFERE_NONE  0   
#define NON_WLAN    1   
#define WLAN_MANUAL 2   
#define WLAN_AUTO   3   
#define WLAN_AUTO_W_NOISE   4   
#define AUTO_ACTIVE (1 << 7) 

typedef struct wl_aci_args {
	int enter_aci_thresh; 
	int exit_aci_thresh; 
	int usec_spin; 
	int glitch_delay; 
	uint16 nphy_adcpwr_enter_thresh;    
	uint16 nphy_adcpwr_exit_thresh; 
	uint16 nphy_repeat_ctr;     
	uint16 nphy_num_samples;    
	uint16 nphy_undetect_window_sz; 
	uint16 nphy_b_energy_lo_aci;    
	uint16 nphy_b_energy_md_aci;    
	uint16 nphy_b_energy_hi_aci;    
	uint16 nphy_noise_noassoc_glitch_th_up; 
	uint16 nphy_noise_noassoc_glitch_th_dn;
	uint16 nphy_noise_assoc_glitch_th_up;
	uint16 nphy_noise_assoc_glitch_th_dn;
	uint16 nphy_noise_assoc_aci_glitch_th_up;
	uint16 nphy_noise_assoc_aci_glitch_th_dn;
	uint16 nphy_noise_assoc_enter_th;
	uint16 nphy_noise_noassoc_enter_th;
	uint16 nphy_noise_assoc_rx_glitch_badplcp_enter_th;
	uint16 nphy_noise_noassoc_crsidx_incr;
	uint16 nphy_noise_assoc_crsidx_incr;
	uint16 nphy_noise_crsidx_decr;
} wl_aci_args_t;

#define TRIGGER_NOW             0
#define TRIGGER_CRS             0x01
#define TRIGGER_CRSDEASSERT         0x02
#define TRIGGER_GOODFCS             0x04
#define TRIGGER_BADFCS              0x08
#define TRIGGER_BADPLCP             0x10
#define TRIGGER_CRSGLITCH           0x20
#define WL_ACI_ARGS_LEGACY_LENGTH   16  
#define WL_SAMPLECOLLECT_T_VERSION  1   
typedef struct wl_samplecollect_args {
	
	uint8 coll_us;
	int cores;
	
	uint16 version;     
	uint16 length;      
	uint8 trigger;
	uint16 timeout;
	uint16 mode;
	uint32 pre_dur;
	uint32 post_dur;
	uint8 gpio_sel;
	bool downsamp;
	bool be_deaf;
	bool agc;       
	bool filter;        
} wl_samplecollect_args_t;

#define WL_SAMPLEDATA_HEADER_TYPE   1
#define WL_SAMPLEDATA_HEADER_SIZE   80  
#define WL_SAMPLEDATA_TYPE      2
#define WL_SAMPLEDATA_SEQ       0xff    
#define WL_SAMPLEDATA_MORE_DATA     0x100   
#define WL_SAMPLEDATA_T_VERSION     1   

#define WL_SAMPLEDATA_T_VERSION_SPEC_AN 2

typedef struct wl_sampledata {
	uint16 version; 
	uint16 size;    
	uint16 tag; 
	uint16 length;  
	uint32 flag;    
} wl_sampledata_t;



#define WL_ERROR_VAL        0x00000001
#define WL_TRACE_VAL        0x00000002
#define WL_PRHDRS_VAL       0x00000004
#define WL_PRPKT_VAL        0x00000008
#define WL_INFORM_VAL       0x00000010
#define WL_TMP_VAL      0x00000020
#define WL_OID_VAL      0x00000040
#define WL_RATE_VAL     0x00000080
#define WL_ASSOC_VAL        0x00000100
#define WL_PRUSR_VAL        0x00000200
#define WL_PS_VAL       0x00000400
#define WL_TXPWR_VAL        0x00000800  
#define WL_PORT_VAL     0x00001000
#define WL_DUAL_VAL     0x00002000
#define WL_WSEC_VAL     0x00004000
#define WL_WSEC_DUMP_VAL    0x00008000
#define WL_LOG_VAL      0x00010000
#define WL_NRSSI_VAL        0x00020000  
#define WL_LOFT_VAL     0x00040000  
#define WL_REGULATORY_VAL   0x00080000
#define WL_PHYCAL_VAL       0x00100000  
#define WL_RADAR_VAL        0x00200000  
#define WL_MPC_VAL      0x00400000
#define WL_APSTA_VAL        0x00800000
#define WL_DFS_VAL      0x01000000
#define WL_BA_VAL       0x02000000
#define WL_ACI_VAL      0x04000000
#define WL_MBSS_VAL     0x04000000
#define WL_CAC_VAL      0x08000000
#define WL_AMSDU_VAL        0x10000000
#define WL_AMPDU_VAL        0x20000000
#define WL_FFPLD_VAL        0x40000000


#define WL_DPT_VAL      0x00000001
#define WL_SCAN_VAL     0x00000002
#define WL_WOWL_VAL     0x00000004
#define WL_COEX_VAL     0x00000008
#define WL_RTDC_VAL     0x00000010
#define WL_PROTO_VAL        0x00000020
#define WL_BTA_VAL      0x00000040
#define WL_CHANINT_VAL      0x00000080
#define WL_THERMAL_VAL      0x00000100  
#define WL_P2P_VAL      0x00000200
#define WL_TXRX_VAL		0x00000400
#define WL_MCHAN_VAL            0x00000800
#define WL_TDLS_VAL		0x00001000


#define WL_LED_NUMGPIO      16  


#define WL_LED_OFF      0       
#define WL_LED_ON       1       
#define WL_LED_ACTIVITY     2       
#define WL_LED_RADIO        3       
#define WL_LED_ARADIO       4       
#define WL_LED_BRADIO       5       
#define WL_LED_BGMODE       6       
#define WL_LED_WI1      7
#define WL_LED_WI2      8
#define WL_LED_WI3      9
#define WL_LED_ASSOC        10      
#define WL_LED_INACTIVE     11      
#define WL_LED_ASSOCACT     12      
#define WL_LED_WI4      13
#define WL_LED_WI5      14
#define WL_LED_BLINKSLOW    15      
#define WL_LED_BLINKMED     16      
#define WL_LED_BLINKFAST    17      
#define WL_LED_BLINKCUSTOM  18      
#define WL_LED_BLINKPERIODIC    19      
#define WL_LED_ASSOC_WITH_SEC   20      
						
#define WL_LED_START_OFF    21      
#define WL_LED_NUMBEHAVIOR  22


#define WL_LED_BEH_MASK     0x7f        
#define WL_LED_AL_MASK      0x80        


#define WL_NUMCHANNELS      64
#define WL_NUMCHANSPECS     100


#define WL_WDS_WPA_ROLE_AUTH    0   
#define WL_WDS_WPA_ROLE_SUP 1   
#define WL_WDS_WPA_ROLE_AUTO    255 


#define WL_EVENTING_MASK_LEN    16




#define WL_JOIN_PREF_RSSI   1   
#define WL_JOIN_PREF_WPA    2   
#define WL_JOIN_PREF_BAND   3   
#define WL_JOIN_PREF_RSSI_DELTA 4   
#define WL_JOIN_PREF_TRANS_PREF	5	


#define WLJP_BAND_ASSOC_PREF    255 


#define WL_WPA_ACP_MCS_ANY  "\x00\x00\x00\x00"

struct tsinfo_arg {
	uint8 octets[3];
};

#define NFIFO           6   

#define WL_CNT_T_VERSION    6   

typedef struct {
	uint16  version;    
	uint16  length;     

	
	uint32  txframe;    
	uint32  txbyte;     
	uint32  txretrans;  
	uint32  txerror;    
	uint32  txctl;      
	uint32  txprshort;  
	uint32  txserr;     
	uint32  txnobuf;    
	uint32  txnoassoc;  
	uint32  txrunt;     
	uint32  txchit;     
	uint32  txcmiss;    

	
	uint32  txuflo;     
	uint32  txphyerr;   
	uint32  txphycrs;

	
	uint32  rxframe;    
	uint32  rxbyte;     
	uint32  rxerror;    
	uint32  rxctl;      
	uint32  rxnobuf;    
	uint32  rxnondata;  
	uint32  rxbadds;    
	uint32  rxbadcm;    
	uint32  rxfragerr;  
	uint32  rxrunt;     
	uint32  rxgiant;    
	uint32  rxnoscb;    
	uint32  rxbadproto; 
	uint32  rxbadsrcmac;    
	uint32  rxbadda;    
	uint32  rxfilter;   

	
	uint32  rxoflo;     
	uint32  rxuflo[NFIFO];  

	uint32  d11cnt_txrts_off;   
	uint32  d11cnt_rxcrc_off;   
	uint32  d11cnt_txnocts_off; 

	
	uint32  dmade;      
	uint32  dmada;      
	uint32  dmape;      
	uint32  reset;      
	uint32  tbtt;       
	uint32  txdmawar;
	uint32  pkt_callback_reg_fail;  

	
	uint32  txallfrm;   
	uint32  txrtsfrm;   
	uint32  txctsfrm;   
	uint32  txackfrm;   
	uint32  txdnlfrm;   
	uint32  txbcnfrm;   
	uint32  txfunfl[8]; 
	uint32  txtplunfl;  
	uint32  txphyerror; 
	uint32  rxfrmtoolong;   
	uint32  rxfrmtooshrt;   
	uint32  rxinvmachdr;    
	uint32  rxbadfcs;   
	uint32  rxbadplcp;  
	uint32  rxcrsglitch;    
	uint32  rxstrt;     
	uint32  rxdfrmucastmbss; 
	uint32  rxmfrmucastmbss; 
	uint32  rxcfrmucast;    
	uint32  rxrtsucast; 
	uint32  rxctsucast; 
	uint32  rxackucast; 
	uint32  rxdfrmocast;    
	uint32  rxmfrmocast;    
	uint32  rxcfrmocast;    
	uint32  rxrtsocast; 
	uint32  rxctsocast; 
	uint32  rxdfrmmcast;    
	uint32  rxmfrmmcast;    
	uint32  rxcfrmmcast;    
	uint32  rxbeaconmbss;   
	uint32  rxdfrmucastobss; 
	uint32  rxbeaconobss;   
	uint32  rxrsptmout; 
	uint32  bcntxcancl; 
	uint32  rxf0ovfl;   
	uint32  rxf1ovfl;   
	uint32  rxf2ovfl;   
	uint32  txsfovfl;   
	uint32  pmqovfl;    
	uint32  rxcgprqfrm; 
	uint32  rxcgprsqovfl;   
	uint32  txcgprsfail;    
	uint32  txcgprssuc; 
	uint32  prs_timeout;    
	uint32  rxnack;
	uint32  frmscons;
	uint32  txnack;
	uint32  txglitch_nack;  
	uint32  txburst;    

	
	uint32  txfrag;     
	uint32  txmulti;    
	uint32  txfail;     
	uint32  txretry;    
	uint32  txretrie;   
	uint32  rxdup;      
	uint32  txrts;      
	uint32  txnocts;    
	uint32  txnoack;    
	uint32  rxfrag;     
	uint32  rxmulti;    
	uint32  rxcrc;      
	uint32  txfrmsnt;   
	uint32  rxundec;    

	
	uint32  tkipmicfaill;   
	uint32  tkipcntrmsr;    
	uint32  tkipreplay; 
	uint32  ccmpfmterr; 
	uint32  ccmpreplay; 
	uint32  ccmpundec;  
	uint32  fourwayfail;    
	uint32  wepundec;   
	uint32  wepicverr;  
	uint32  decsuccess; 
	uint32  tkipicverr; 
	uint32  wepexcluded;    

	uint32  rxundec_mcst;   

	
	uint32  tkipmicfaill_mcst;  
	uint32  tkipcntrmsr_mcst;   
	uint32  tkipreplay_mcst;    
	uint32  ccmpfmterr_mcst;    
	uint32  ccmpreplay_mcst;    
	uint32  ccmpundec_mcst; 
	uint32  fourwayfail_mcst;   
	uint32  wepundec_mcst;  
	uint32  wepicverr_mcst; 
	uint32  decsuccess_mcst;    
	uint32  tkipicverr_mcst;    
	uint32  wepexcluded_mcst;   

	uint32  txchanrej;  
	uint32  txexptime;  
	uint32  psmwds;     
	uint32  phywatchdog;    

	
	uint32  prq_entries_handled;    
	uint32  prq_undirected_entries; 
	uint32  prq_bad_entries;    
	uint32  atim_suppress_count;    
	uint32  bcn_template_not_ready; 
	uint32  bcn_template_not_ready_done; 
	uint32  late_tbtt_dpc;  

	
	uint32  rx1mbps;    
	uint32  rx2mbps;    
	uint32  rx5mbps5;   
	uint32  rx6mbps;    
	uint32  rx9mbps;    
	uint32  rx11mbps;   
	uint32  rx12mbps;   
	uint32  rx18mbps;   
	uint32  rx24mbps;   
	uint32  rx36mbps;   
	uint32  rx48mbps;   
	uint32  rx54mbps;   
	uint32  rx108mbps;  
	uint32  rx162mbps;  
	uint32  rx216mbps;  
	uint32  rx270mbps;  
	uint32  rx324mbps;  
	uint32  rx378mbps;  
	uint32  rx432mbps;  
	uint32  rx486mbps;  
	uint32  rx540mbps;  

	
	uint32  pktengrxducast; 
	uint32  pktengrxdmcast; 

	uint32  rfdisable;  
	uint32  bphy_rxcrsglitch;   

	uint32  txmpdu_sgi; 
	uint32  rxmpdu_sgi; 
	uint32  txmpdu_stbc;    
	uint32  rxmpdu_stbc;    
} wl_cnt_t;


#define WL_WME_CNT_VERSION  1   

typedef struct {
	uint32 packets;
	uint32 bytes;
} wl_traffic_stats_t;

typedef struct {
	uint16  version;    
	uint16  length;     

	wl_traffic_stats_t tx[AC_COUNT];    
	wl_traffic_stats_t tx_failed[AC_COUNT]; 
	wl_traffic_stats_t rx[AC_COUNT];    
	wl_traffic_stats_t rx_failed[AC_COUNT]; 

	wl_traffic_stats_t forward[AC_COUNT];   

	wl_traffic_stats_t tx_expired[AC_COUNT];    

} wl_wme_cnt_t;

struct wl_msglevel2 {
	uint32 low;
	uint32 high;
};

typedef struct wl_mkeep_alive_pkt {
	uint16	version; 
	uint16	length; 
	uint32	period_msec;
	uint16	len_bytes;
	uint8	keep_alive_id; 
	uint8	data[1];
} wl_mkeep_alive_pkt_t;

#define WL_MKEEP_ALIVE_VERSION		1
#define WL_MKEEP_ALIVE_FIXED_LEN	OFFSETOF(wl_mkeep_alive_pkt_t, data)
#define WL_MKEEP_ALIVE_PRECISION	500



#define WLC_ROAM_TRIGGER_DEFAULT    0 
#define WLC_ROAM_TRIGGER_BANDWIDTH  1 
#define WLC_ROAM_TRIGGER_DISTANCE   2 
#define WLC_ROAM_TRIGGER_AUTO       3 
#define WLC_ROAM_TRIGGER_MAX_VALUE  3 


#define WPA_AUTH_PFN_ANY	0xffffffff	

enum {
	PFN_LIST_ORDER,
	PFN_RSSI
};

enum {
	DISABLE,
	ENABLE
};

enum {
	OFF_ADAPT,
	SMART_ADAPT,
	STRICT_ADAPT,
	SLOW_ADAPT
};

#define SORT_CRITERIA_BIT		0
#define AUTO_NET_SWITCH_BIT		1
#define ENABLE_BKGRD_SCAN_BIT	2
#define IMMEDIATE_SCAN_BIT		3
#define	AUTO_CONNECT_BIT		4
#define	ENABLE_BD_SCAN_BIT		5
#define ENABLE_ADAPTSCAN_BIT	6
#define IMMEDIATE_EVENT_BIT		8

#define SORT_CRITERIA_MASK		0x0001
#define AUTO_NET_SWITCH_MASK	0x0002
#define ENABLE_BKGRD_SCAN_MASK	0x0004
#define IMMEDIATE_SCAN_MASK		0x0008
#define	AUTO_CONNECT_MASK		0x0010
#define ENABLE_BD_SCAN_MASK		0x0020
#define ENABLE_ADAPTSCAN_MASK	0x00c0
#define IMMEDIATE_EVENT_MASK	0x0100

#define PFN_VERSION				2
#define PFN_SCANRESULT_VERSION	1
#define MAX_PFN_LIST_COUNT	16

#define PFN_COMPLETE			1
#define PFN_INCOMPLETE			0

#define DEFAULT_BESTN			2
#define DEFAULT_MSCAN			0
#define DEFAULT_REPEAT			10
#define DEFAULT_EXP				2


typedef struct wl_pfn_subnet_info {
	struct ether_addr BSSID;
	uint8	channel; 
	uint8	SSID_len;
	uint8	SSID[32];
} wl_pfn_subnet_info_t;

typedef struct wl_pfn_net_info {
	wl_pfn_subnet_info_t pfnsubnet;
	int16	RSSI; 
	uint16	timestamp; 
} wl_pfn_net_info_t;

typedef struct wl_pfn_scanresults {
	uint32 version;
	uint32 status;
	uint32 count;
	wl_pfn_net_info_t netinfo[1];
} wl_pfn_scanresults_t;


typedef struct wl_pfn_param {
	int32 version;			
	int32 scan_freq;		
	int32 lost_network_timeout;	
	int16 flags;			
	int16 rssi_margin;		
	uint8 bestn; 
	uint8 mscan; 
	uint8 repeat; 
	uint8 exp; 
	int32 slow_freq; 
} wl_pfn_param_t;

typedef struct wl_pfn_bssid {
	struct ether_addr 	macaddr;
	
	uint16				flags;
} wl_pfn_bssid_t;
#define WL_PFN_SUPPRESSFOUND_MASK	0x08
#define WL_PFN_SUPPRESSLOST_MASK	0x10

typedef struct wl_pfn_cfg {
	uint32				reporttype;
	int32				channel_num;
	uint16				channel_list[WL_NUMCHANNELS];
} wl_pfn_cfg_t;
#define WL_PFN_REPORT_ALLNET 	0
#define WL_PFN_REPORT_SSIDNET 	1
#define WL_PFN_REPORT_BSSIDNET 	2

typedef struct wl_pfn {
	wlc_ssid_t		ssid;			
	int32			flags;			
	int32			infra;			
	int32			auth;			
	int32			wpa_auth;		
	int32			wsec;			
} wl_pfn_t;
#define WL_PFN_HIDDEN_BIT		2
#define PNO_SCAN_MAX_FW		508*1000	
#define PNO_SCAN_MAX_FW_SEC	PNO_SCAN_MAX_FW/1000 
#define PNO_SCAN_MIN_FW_SEC	10			
#define WL_PFN_HIDDEN_MASK		0x4


#define TOE_TX_CSUM_OL      0x00000001
#define TOE_RX_CSUM_OL      0x00000002


#define TOE_ERRTEST_TX_CSUM 0x00000001
#define TOE_ERRTEST_RX_CSUM 0x00000002
#define TOE_ERRTEST_RX_CSUM2    0x00000004

struct toe_ol_stats_t {
	
	uint32 tx_summed;

	
	uint32 tx_iph_fill;
	uint32 tx_tcp_fill;
	uint32 tx_udp_fill;
	uint32 tx_icmp_fill;

	
	uint32 rx_iph_good;
	uint32 rx_iph_bad;
	uint32 rx_tcp_good;
	uint32 rx_tcp_bad;
	uint32 rx_udp_good;
	uint32 rx_udp_bad;
	uint32 rx_icmp_good;
	uint32 rx_icmp_bad;

	
	uint32 tx_tcp_errinj;
	uint32 tx_udp_errinj;
	uint32 tx_icmp_errinj;

	
	uint32 rx_tcp_errinj;
	uint32 rx_udp_errinj;
	uint32 rx_icmp_errinj;
};


#define ARP_OL_AGENT        0x00000001
#define ARP_OL_SNOOP        0x00000002
#define ARP_OL_HOST_AUTO_REPLY  0x00000004
#define ARP_OL_PEER_AUTO_REPLY  0x00000008


#define ARP_ERRTEST_REPLY_PEER  0x1
#define ARP_ERRTEST_REPLY_HOST  0x2

#define ARP_MULTIHOMING_MAX 8   


struct arp_ol_stats_t {
	uint32  host_ip_entries;    
	uint32  host_ip_overflow;   

	uint32  arp_table_entries;  
	uint32  arp_table_overflow; 

	uint32  host_request;       
	uint32  host_reply;     
	uint32  host_service;       

	uint32  peer_request;       
	uint32  peer_request_drop;  
	uint32  peer_reply;     
	uint32  peer_reply_drop;    
	uint32  peer_service;       
};




typedef struct wl_keep_alive_pkt {
	uint32  period_msec;    
	uint16  len_bytes;  
	uint8   data[1];    
} wl_keep_alive_pkt_t;

#define WL_KEEP_ALIVE_FIXED_LEN     OFFSETOF(wl_keep_alive_pkt_t, data)




#define MAX_WAKE_PACKET_BYTES 128


typedef struct pm_wake_packet {
	uint32	status;		
	uint32	pattern_id;	
	uint32	original_packet_size;
	uint32	saved_packet_size;
	uchar	packet[MAX_WAKE_PACKET_BYTES];
} pm_wake_packet_t;



#define PKT_FILTER_MODE_FORWARD_ON_MATCH      		1

#define PKT_FILTER_MODE_DISABLE					2

#define PKT_FILTER_MODE_PKT_CACHE_ON_MATCH    		4

#define PKT_FILTER_MODE_PKT_FORWARD_OFF_DEFAULT 8


typedef enum wl_pkt_filter_type {
	WL_PKT_FILTER_TYPE_PATTERN_MATCH,	
	WL_PKT_FILTER_TYPE_MAGIC_PATTERN_MATCH	
} wl_pkt_filter_type_t;

#define WL_PKT_FILTER_TYPE wl_pkt_filter_type_t


typedef struct wl_pkt_filter_pattern {
	uint32  offset;     
	uint32  size_bytes; 
	uint8   mask_and_pattern[1]; 
} wl_pkt_filter_pattern_t;


typedef struct wl_pkt_filter {
	uint32  id;     
	uint32  type;       
	uint32  negate_match;   
	union {         
		wl_pkt_filter_pattern_t pattern;    
	} u;
} wl_pkt_filter_t;

#define WL_PKT_FILTER_FIXED_LEN       OFFSETOF(wl_pkt_filter_t, u)
#define WL_PKT_FILTER_PATTERN_FIXED_LEN   OFFSETOF(wl_pkt_filter_pattern_t, mask_and_pattern)


typedef struct wl_pkt_filter_enable {
	uint32  id;     
	uint32  enable;     
} wl_pkt_filter_enable_t;


typedef struct wl_pkt_filter_list {
	uint32  num;        
	wl_pkt_filter_t filter[1];  
} wl_pkt_filter_list_t;

#define WL_PKT_FILTER_LIST_FIXED_LEN      OFFSETOF(wl_pkt_filter_list_t, filter)


typedef struct wl_pkt_filter_stats {
	uint32  num_pkts_matched;   
	uint32  num_pkts_forwarded; 
	uint32  num_pkts_discarded; 
} wl_pkt_filter_stats_t;


typedef struct wl_seq_cmd_ioctl {
	uint32 cmd;     
	uint32 len;     
} wl_seq_cmd_ioctl_t;

#define WL_SEQ_CMD_ALIGN_BYTES  4


#define WL_SEQ_CMDS_GET_IOCTL_FILTER(cmd) \
	(((cmd) == WLC_GET_MAGIC) || \
	 ((cmd) == WLC_GET_VERSION) || \
	 ((cmd) == WLC_GET_AP) || \
	 ((cmd) == WLC_GET_INSTANCE))



#define WL_PKTENG_PER_TX_START          0x01
#define WL_PKTENG_PER_TX_STOP           0x02
#define WL_PKTENG_PER_RX_START          0x04
#define WL_PKTENG_PER_RX_WITH_ACK_START     0x05
#define WL_PKTENG_PER_TX_WITH_ACK_START     0x06
#define WL_PKTENG_PER_RX_STOP           0x08
#define WL_PKTENG_PER_MASK          0xff

#define WL_PKTENG_SYNCHRONOUS           0x100   

typedef struct wl_pkteng {
	uint32 flags;
	uint32 delay;           
	uint32 nframes;         
	uint32 length;          
	uint8  seqno;           
	struct ether_addr dest;     
	struct ether_addr src;      
} wl_pkteng_t;

#define NUM_80211b_RATES    4
#define NUM_80211ag_RATES   8
#define NUM_80211n_RATES    32
#define NUM_80211_RATES     (NUM_80211b_RATES+NUM_80211ag_RATES+NUM_80211n_RATES)
typedef struct wl_pkteng_stats {
	uint32 lostfrmcnt;      
	int32 rssi;         
	int32 snr;          
	uint16 rxpktcnt[NUM_80211_RATES+1];
} wl_pkteng_stats_t;


#define WL_WOWL_MAGIC   (1 << 0)    
#define WL_WOWL_NET (1 << 1)    
#define WL_WOWL_DIS (1 << 2)    
#define WL_WOWL_RETR    (1 << 3)    
#define WL_WOWL_BCN (1 << 4)    
#define WL_WOWL_TST (1 << 5)    
#define WL_WOWL_M1      (1 << 6)        
#define WL_WOWL_EAPID   (1 << 7)        
#define WL_WOWL_KEYROT  (1 << 14)       
#define WL_WOWL_BCAST   (1 << 15)   

#define MAGIC_PKT_MINLEN 102    

typedef struct {
	uint masksize;      
	uint offset;        
	uint patternoffset; 
	uint patternsize;   
	ulong id;       
	
	
} wl_wowl_pattern_t;

typedef struct {
	uint            count;
	wl_wowl_pattern_t   pattern[1];
} wl_wowl_pattern_list_t;

typedef struct {
	uint8   pci_wakeind;    
	uint16  ucode_wakeind;  
} wl_wowl_wakeind_t;


typedef struct wl_txrate_class {
	uint8       init_rate;
	uint8       min_rate;
	uint8       max_rate;
} wl_txrate_class_t;




#define WLC_OBSS_SCAN_PASSIVE_DWELL_DEFAULT     20  
#define WLC_OBSS_SCAN_PASSIVE_DWELL_MIN         5   
#define WLC_OBSS_SCAN_PASSIVE_DWELL_MAX         1000    
#define WLC_OBSS_SCAN_ACTIVE_DWELL_DEFAULT      10  
#define WLC_OBSS_SCAN_ACTIVE_DWELL_MIN          10  
#define WLC_OBSS_SCAN_ACTIVE_DWELL_MAX          1000    
#define WLC_OBSS_SCAN_WIDTHSCAN_INTERVAL_DEFAULT    300 
#define WLC_OBSS_SCAN_WIDTHSCAN_INTERVAL_MIN        10  
#define WLC_OBSS_SCAN_WIDTHSCAN_INTERVAL_MAX        900 
#define WLC_OBSS_SCAN_CHANWIDTH_TRANSITION_DLY_DEFAULT  5
#define WLC_OBSS_SCAN_CHANWIDTH_TRANSITION_DLY_MIN  5
#define WLC_OBSS_SCAN_CHANWIDTH_TRANSITION_DLY_MAX  100
#define WLC_OBSS_SCAN_PASSIVE_TOTAL_PER_CHANNEL_DEFAULT 200 
#define WLC_OBSS_SCAN_PASSIVE_TOTAL_PER_CHANNEL_MIN 200 
#define WLC_OBSS_SCAN_PASSIVE_TOTAL_PER_CHANNEL_MAX 10000   
#define WLC_OBSS_SCAN_ACTIVE_TOTAL_PER_CHANNEL_DEFAULT  20  
#define WLC_OBSS_SCAN_ACTIVE_TOTAL_PER_CHANNEL_MIN  20  
#define WLC_OBSS_SCAN_ACTIVE_TOTAL_PER_CHANNEL_MAX  10000   
#define WLC_OBSS_SCAN_ACTIVITY_THRESHOLD_DEFAULT    25  
#define WLC_OBSS_SCAN_ACTIVITY_THRESHOLD_MIN        0   
#define WLC_OBSS_SCAN_ACTIVITY_THRESHOLD_MAX        100 


typedef struct wl_obss_scan_arg {
	int16   passive_dwell;
	int16   active_dwell;
	int16   bss_widthscan_interval;
	int16   passive_total;
	int16   active_total;
	int16   chanwidth_transition_delay;
	int16   activity_threshold;
} wl_obss_scan_arg_t;

#define WL_OBSS_SCAN_PARAM_LEN  sizeof(wl_obss_scan_arg_t)
#define WL_MIN_NUM_OBSS_SCAN_ARG 7  

#define WL_COEX_INFO_MASK       0x07
#define WL_COEX_INFO_REQ        0x01
#define WL_COEX_40MHZ_INTOLERANT    0x02
#define WL_COEX_WIDTH20         0x04

#define WLC_RSSI_INVALID     0  

#define MAX_RSSI_LEVELS 8


typedef struct wl_rssi_event {
	uint32 rate_limit_msec;     
	uint8 num_rssi_levels;      
	int8 rssi_levels[MAX_RSSI_LEVELS];  
} wl_rssi_event_t;

typedef struct wl_action_obss_coex_req {
	uint8 info;
	uint8 num;
	uint8 ch_list[1];
} wl_action_obss_coex_req_t;


#define EXTLOG_CUR_VER      0x0100

#define MAX_ARGSTR_LEN      18 


#define LOG_MODULE_COMMON   0x0001
#define LOG_MODULE_ASSOC    0x0002
#define LOG_MODULE_EVENT    0x0004
#define LOG_MODULE_MAX      3           


#define WL_LOG_LEVEL_DISABLE    0
#define WL_LOG_LEVEL_ERR    1
#define WL_LOG_LEVEL_WARN   2
#define WL_LOG_LEVEL_INFO   3
#define WL_LOG_LEVEL_MAX    WL_LOG_LEVEL_INFO   


#define LOG_FLAG_EVENT      1


#define LOG_ARGTYPE_NULL    0
#define LOG_ARGTYPE_STR     1   
#define LOG_ARGTYPE_INT     2   
#define LOG_ARGTYPE_INT_STR 3   
#define LOG_ARGTYPE_STR_INT 4   

typedef struct wlc_extlog_cfg {
	int max_number;
	uint16 module;  
	uint8 level;
	uint8 flag;
	uint16 version;
} wlc_extlog_cfg_t;

typedef struct log_record {
	uint32 time;
	uint16 module;
	uint16 id;
	uint8 level;
	uint8 sub_unit;
	uint8 seq_num;
	int32 arg;
	char str[MAX_ARGSTR_LEN];
} log_record_t;

typedef struct wlc_extlog_req {
	uint32 from_last;
	uint32 num;
} wlc_extlog_req_t;

typedef struct wlc_extlog_results {
	uint16 version;
	uint16 record_len;
	uint32 num;
	log_record_t logs[1];
} wlc_extlog_results_t;

typedef struct log_idstr {
	uint16  id;
	uint16  flag;
	uint8   arg_type;
	const char  *fmt_str;
} log_idstr_t;

#define FMTSTRF_USER        1


typedef enum {
	FMTSTR_DRIVER_UP_ID = 0,
	FMTSTR_DRIVER_DOWN_ID = 1,
	FMTSTR_SUSPEND_MAC_FAIL_ID = 2,
	FMTSTR_NO_PROGRESS_ID = 3,
	FMTSTR_RFDISABLE_ID = 4,
	FMTSTR_REG_PRINT_ID = 5,
	FMTSTR_EXPTIME_ID = 6,
	FMTSTR_JOIN_START_ID = 7,
	FMTSTR_JOIN_COMPLETE_ID = 8,
	FMTSTR_NO_NETWORKS_ID = 9,
	FMTSTR_SECURITY_MISMATCH_ID = 10,
	FMTSTR_RATE_MISMATCH_ID = 11,
	FMTSTR_AP_PRUNED_ID = 12,
	FMTSTR_KEY_INSERTED_ID = 13,
	FMTSTR_DEAUTH_ID = 14,
	FMTSTR_DISASSOC_ID = 15,
	FMTSTR_LINK_UP_ID = 16,
	FMTSTR_LINK_DOWN_ID = 17,
	FMTSTR_RADIO_HW_OFF_ID = 18,
	FMTSTR_RADIO_HW_ON_ID = 19,
	FMTSTR_EVENT_DESC_ID = 20,
	FMTSTR_PNP_SET_POWER_ID = 21,
	FMTSTR_RADIO_SW_OFF_ID = 22,
	FMTSTR_RADIO_SW_ON_ID = 23,
	FMTSTR_PWD_MISMATCH_ID = 24,
	FMTSTR_FATAL_ERROR_ID = 25,
	FMTSTR_AUTH_FAIL_ID = 26,
	FMTSTR_ASSOC_FAIL_ID = 27,
	FMTSTR_IBSS_FAIL_ID = 28,
	FMTSTR_EXTAP_FAIL_ID = 29,
	FMTSTR_MAX_ID
} log_fmtstr_id_t;

#ifdef DONGLEOVERLAYS
typedef struct {
	uint32 flags_idx;	
	uint32 offset;		
	uint32 len;			
	
} wl_ioctl_overlay_t;

#define OVERLAY_IDX_MASK		0x000000ff
#define OVERLAY_IDX_SHIFT		0
#define OVERLAY_FLAGS_MASK		0xffffff00
#define OVERLAY_FLAGS_SHIFT		8

#define OVERLAY_FLAG_POSTLOAD	0x100

#define OVERLAY_FLAG_DEFER_DL	0x200

#define OVERLAY_FLAG_PRESLEEP	0x400

#define OVERLAY_DOWNLOAD_CHUNKSIZE	1024
#endif 


#include <packed_section_end.h>


#include <packed_section_start.h>

#define VNDR_IE_CMD_LEN     4   


#define VNDR_IE_BEACON_FLAG 0x1
#define VNDR_IE_PRBRSP_FLAG 0x2
#define VNDR_IE_ASSOCRSP_FLAG   0x4
#define VNDR_IE_AUTHRSP_FLAG    0x8
#define VNDR_IE_PRBREQ_FLAG 0x10
#define VNDR_IE_ASSOCREQ_FLAG   0x20
#define VNDR_IE_CUSTOM_FLAG 0x100 

#define VNDR_IE_INFO_HDR_LEN    (sizeof(uint32))

typedef BWL_PRE_PACKED_STRUCT struct {
	uint32 pktflag;         
	vndr_ie_t vndr_ie_data;     
} BWL_POST_PACKED_STRUCT vndr_ie_info_t;

typedef BWL_PRE_PACKED_STRUCT struct {
	int iecount;            
	vndr_ie_info_t vndr_ie_list[1]; 
} BWL_POST_PACKED_STRUCT vndr_ie_buf_t;

typedef BWL_PRE_PACKED_STRUCT struct {
	char cmd[VNDR_IE_CMD_LEN];  
	vndr_ie_buf_t vndr_ie_buffer;   
} BWL_POST_PACKED_STRUCT vndr_ie_setbuf_t;



typedef BWL_PRE_PACKED_STRUCT struct sta_prbreq_wps_ie_hdr {
	struct ether_addr staAddr;
	uint16 ieLen;
} BWL_POST_PACKED_STRUCT sta_prbreq_wps_ie_hdr_t;

typedef BWL_PRE_PACKED_STRUCT struct sta_prbreq_wps_ie_data {
	sta_prbreq_wps_ie_hdr_t hdr;
	uint8 ieData[1];
} BWL_POST_PACKED_STRUCT sta_prbreq_wps_ie_data_t;

typedef BWL_PRE_PACKED_STRUCT struct sta_prbreq_wps_ie_list {
	uint32 totLen;
	uint8 ieDataList[1];
} BWL_POST_PACKED_STRUCT sta_prbreq_wps_ie_list_t;


#ifdef WLMEDIA_TXFAILEVENT
typedef BWL_PRE_PACKED_STRUCT struct {
	char   dest[ETHER_ADDR_LEN]; 
	uint8  prio;            
	uint8  flags;           
	uint32 tsf_l;           
	uint32 tsf_h;           
	uint16 rates;           
	uint16 txstatus;        
} BWL_POST_PACKED_STRUCT txfailinfo_t;
#endif 

#include <packed_section_end.h>


#define ASSERTLOG_CUR_VER   0x0100
#define MAX_ASSRTSTR_LEN    64

typedef struct assert_record {
	uint32 time;
	uint8 seq_num;
	char str[MAX_ASSRTSTR_LEN];
} assert_record_t;

typedef struct assertlog_results {
	uint16 version;
	uint16 record_len;
	uint32 num;
	assert_record_t logs[1];
} assertlog_results_t;

#define LOGRRC_FIX_LEN  8
#define IOBUF_ALLOWED_NUM_OF_LOGREC(type, len) ((len - LOGRRC_FIX_LEN)/sizeof(type))





#define CHANIM_DISABLE  0   
#define CHANIM_DETECT   1   
#define CHANIM_ACT  2   
#define CHANIM_MODE_MAX 2


#define APCS_IOCTL      1
#define APCS_CHANIM     2
#define APCS_CSTIMER    3
#define APCS_BTA        4


#define CHANIM_ACS_RECORD           10


typedef struct {
	bool valid;
	uint8 trigger;
	chanspec_t selected_chspc;
	uint32 glitch_cnt;
	uint8 ccastats;
	uint timestamp;
} chanim_acs_record_t;

typedef struct {
	chanim_acs_record_t acs_record[CHANIM_ACS_RECORD];
	uint8 count;
	uint timestamp;
} wl_acs_record_t;



#define SMFS_VERSION 1

typedef struct wl_smfs_elem {
	uint32 count;
	uint16 code;  
} wl_smfs_elem_t;

typedef struct wl_smf_stats {
	uint32 version;
	uint16 length;  
	uint8 type;
	uint8 codetype;
	uint32 ignored_cnt;
	uint32 malformed_cnt;
	uint32 count_total; 
	wl_smfs_elem_t elem[1];
} wl_smf_stats_t;

#define WL_SMFSTATS_FIXED_LEN OFFSETOF(wl_smf_stats_t, elem);

enum {
	SMFS_CODETYPE_SC,
	SMFS_CODETYPE_RC
};


#define SMFS_CODE_MALFORMED 0xFFFE
#define SMFS_CODE_IGNORED   0xFFFD

typedef enum smfs_type {
	SMFS_TYPE_AUTH,
	SMFS_TYPE_ASSOC,
	SMFS_TYPE_REASSOC,
	SMFS_TYPE_DISASSOC_TX,
	SMFS_TYPE_DISASSOC_RX,
	SMFS_TYPE_DEAUTH_TX,
	SMFS_TYPE_DEAUTH_RX,
	SMFS_TYPE_MAX
} smfs_type_t;

#ifdef PHYMON

#define PHYMON_VERSION 1

typedef struct wl_phycal_core_state {
	
	int16 tx_iqlocal_a;
	int16 tx_iqlocal_b;
	int8 tx_iqlocal_ci;
	int8 tx_iqlocal_cq;
	int8 tx_iqlocal_di;
	int8 tx_iqlocal_dq;
	int8 tx_iqlocal_ei;
	int8 tx_iqlocal_eq;
	int8 tx_iqlocal_fi;
	int8 tx_iqlocal_fq;

	
	int16 rx_iqcal_a;
	int16 rx_iqcal_b;

	uint8 tx_iqlocal_pwridx; 
	uint32 papd_epsilon_table[64]; 
	int16 papd_epsilon_offset; 
	uint8 curr_tx_pwrindex; 
	int8 idle_tssi; 
	int8 est_tx_pwr; 
	int8 est_rx_pwr; 
	uint16 rx_gaininfo; 
	uint16 init_gaincode; 
	int8 estirr_tx;
	int8 estirr_rx;

} wl_phycal_core_state_t;

typedef struct wl_phycal_state {
	int version;
	int8 num_phy_cores; 
	int8 curr_temperature; 
	chanspec_t chspec; 
	bool aci_state; 
	uint16 crsminpower; 
	uint16 crsminpowerl; 
	uint16 crsminpoweru; 
	wl_phycal_core_state_t phycal_core[1];
} wl_phycal_state_t;

#define WL_PHYCAL_STAT_FIXED_LEN OFFSETOF(wl_phycal_state_t, phycal_core)
#endif 

#ifdef WLDSTA
typedef struct wl_dsta_if {
	struct ether_addr addr;
} wl_dsta_if_t;
#endif

#ifdef WLP2P

typedef struct wl_p2p_disc_st {
	uint8 state;    
	chanspec_t chspec;  
	uint16 dwell;   
} wl_p2p_disc_st_t;


#define WL_P2P_DISC_ST_SCAN 0
#define WL_P2P_DISC_ST_LISTEN   1
#define WL_P2P_DISC_ST_SEARCH   2


typedef struct wl_p2p_scan {
	uint8 type;     
	uint8 reserved[3];
	
} wl_p2p_scan_t;


typedef struct wl_p2p_if {
	struct ether_addr addr;
	uint8 type; 
	chanspec_t chspec;  
} wl_p2p_if_t;


#define WL_P2P_IF_CLIENT    0
#define WL_P2P_IF_GO        1
#define WL_P2P_IF_DYNBCN_GO     2
#define WL_P2P_IF_DEV       3


typedef struct wl_p2p_ifq {
	uint bsscfgidx;
	char ifname[BCM_MSG_IFNAME_MAX];
} wl_p2p_ifq_t;


typedef struct wl_p2p_ops {
	uint8 ops;  
	uint8 ctw;  
} wl_p2p_ops_t;


typedef struct wl_p2p_sched_desc {
	uint32 start;
	uint32 interval;
	uint32 duration;
	uint32 count;   
} wl_p2p_sched_desc_t;


#define WL_P2P_SCHED_RSVD   0
#define WL_P2P_SCHED_REPEAT 255 

typedef struct wl_p2p_sched {
	uint8 type; 
	uint8 action;   
	uint8 option;   
	wl_p2p_sched_desc_t desc[1];
} wl_p2p_sched_t;
#define WL_P2P_SCHED_FIXED_LEN      3


#define WL_P2P_SCHED_TYPE_ABS       0   
#define WL_P2P_SCHED_TYPE_REQ_ABS   1   


#define WL_P2P_SCHED_ACTION_NONE    0   
#define WL_P2P_SCHED_ACTION_DOZE    1   

#define WL_P2P_SCHED_ACTION_GOOFF   2   

#define WL_P2P_SCHED_ACTION_RESET   255 


#define WL_P2P_SCHED_OPTION_NORMAL  0   
#define WL_P2P_SCHED_OPTION_BCNPCT  1   

#define WL_P2P_SCHED_OPTION_TSFOFS  2   


#define WL_P2P_FEAT_GO_CSA      (1 << 0)        
#define WL_P2P_FEAT_GO_NOLEGACY (1 << 1)        
#define WL_P2P_FEAT_RESTRICT_DEV_RESP (1 << 2)  
#endif 


#define BCM_ACTION_RFAWARE      0x77
#define BCM_ACTION_RFAWARE_DCS  0x01



#define WL_11N_2x2          1
#define WL_11N_3x3          3
#define WL_11N_4x4          4


#define WLFEATURE_DISABLE_11N       0x00000001
#define WLFEATURE_DISABLE_11N_STBC_TX   0x00000002
#define WLFEATURE_DISABLE_11N_STBC_RX   0x00000004
#define WLFEATURE_DISABLE_11N_SGI_TX    0x00000008
#define WLFEATURE_DISABLE_11N_SGI_RX    0x00000010
#define WLFEATURE_DISABLE_11N_AMPDU_TX  0x00000020
#define WLFEATURE_DISABLE_11N_AMPDU_RX  0x00000040
#define WLFEATURE_DISABLE_11N_GF    0x00000080


#define LQ_IDX_LAST             3
#define MCS_INDEX_SIZE          33

#define LQ_IDX_MIN              0
#define LQ_IDX_MAX              1
#define LQ_IDX_AVG              2
#define LQ_IDX_SUM              2
#define LQ_IDX_LAST             3
#define LQ_STOP_MONITOR         0
#define LQ_START_MONITOR        1

#define LINKQUAL_V1     0x01

struct  wl_lq {
	int32 enable;
	int32 rssi[LQ_IDX_LAST];    
	int32 rssicnt;
	int32 snr[LQ_IDX_LAST];     
	uint32 nsamples;            
	uint8 isvalid;              
	uint8 version;
}; 

typedef struct wl_lq wl_lq_t;
typedef struct wl_lq  wl_lq_stats_t;

typedef struct {
	struct  ether_addr ea;  
	uint8   ac_cat; 
	uint8   num_pkts;   
} wl_mac_ratehisto_cmd_t;   


typedef struct {
	uint32  rate[WLC_MAXRATE + 1];  
	uint32  mcs_index[MCS_INDEX_SIZE];  
	uint32  tsf_timer[2][2];    
} wl_mac_ratehisto_res_t;   

#ifdef PROP_TXSTATUS


#define WLFC_FLAGS_RSSI_SIGNALS                         1


#define WLFC_FLAGS_XONXOFF_SIGNALS                      2


#define WLFC_FLAGS_CREDIT_STATUS_SIGNALS        4

#define WLFC_FLAGS_HOST_PROPTXSTATUS_ACTIVE     8
#define WLFC_FLAGS_PSQ_GENERATIONFSM_ENABLE     16
#define WLFC_FLAGS_PSQ_ZERO_BUFFER_ENABLE	32
#endif 

#define BTA_STATE_LOG_SZ    64


enum {
	HCIReset = 1,
	HCIReadLocalAMPInfo,
	HCIReadLocalAMPASSOC,
	HCIWriteRemoteAMPASSOC,
	HCICreatePhysicalLink,
	HCIAcceptPhysicalLinkRequest,
	HCIDisconnectPhysicalLink,
	HCICreateLogicalLink,
	HCIAcceptLogicalLink,
	HCIDisconnectLogicalLink,
	HCILogicalLinkCancel,
	HCIAmpStateChange,
	HCIWriteLogicalLinkAcceptTimeout
};

typedef struct flush_txfifo {
	uint32 txfifobmp;
	uint32 hwtxfifoflush;
	struct ether_addr ea;
} flush_txfifo_t;

#define CHANNEL_5G_LOW_START    36      
#define CHANNEL_5G_MID_START    52      
#define CHANNEL_5G_HIGH_START   100     
#define CHANNEL_5G_UPPER_START  149     

enum {
	SPATIAL_MODE_2G_IDX = 0,
	SPATIAL_MODE_5G_LOW_IDX,
	SPATIAL_MODE_5G_MID_IDX,
	SPATIAL_MODE_5G_HIGH_IDX,
	SPATIAL_MODE_5G_UPPER_IDX,
	SPATIAL_MODE_MAX_IDX
};

#endif 
