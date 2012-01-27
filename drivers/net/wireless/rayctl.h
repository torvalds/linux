#ifndef RAYLINK_H

typedef unsigned char UCHAR;

/****** IEEE 802.11 constants ************************************************/
#define ADDRLEN           6
/* Frame control 1 bit fields */
#define PROTOCOL_VER      0x00
#define DATA_TYPE         0x08
#define ASSOC_REQ_TYPE    0x00
#define ASSOC_RESP_TYPE   0x10
#define REASSOC_REQ_TYPE  0x20
#define REASSOC_RESP_TYPE 0x30
#define NULL_MSG_TYPE     0x48
#define BEACON_TYPE       0x80
#define DISASSOC_TYPE     0xA0
#define PSPOLL_TYPE       0xA4
#define AUTHENTIC_TYPE    0xB0
#define DEAUTHENTIC_TYPE  0xC0
/* Frame control 2 bit fields */
#define FC2_TO_DS         0x01
#define FC2_FROM_DS       0x02
#define FC2_MORE_FRAG     0x04
#define FC2_RETRY         0x08
#define FC2_PSM           0x10
#define FC2_MORE_DATA     0x20
#define FC2_WEP           0x40
#define FC2_ORDER         0x80
/*****************************************************************************/
/* 802.11 element ID's and lengths */
#define C_BP_CAPABILITY_ESS             0x01
#define C_BP_CAPABILITY_IBSS            0x02
#define C_BP_CAPABILITY_CF_POLLABLE     0x04
#define C_BP_CAPABILITY_CF_POLL_REQUEST 0x08
#define C_BP_CAPABILITY_PRIVACY         0x10

#define C_ESSID_ELEMENT_ID               0
#define C_ESSID_ELEMENT_MAX_LENGTH       32

#define C_SUPPORTED_RATES_ELEMENT_ID     1
#define C_SUPPORTED_RATES_ELEMENT_LENGTH 2

#define C_FH_PARAM_SET_ELEMENT_ID        2
#define C_FH_PARAM_SET_ELEMENT_LNGTH     5

#define C_CF_PARAM_SET_ELEMENT_ID        4
#define C_CF_PARAM_SET_ELEMENT_LNGTH     6

#define C_TIM_ELEMENT_ID                 5
#define C_TIM_BITMAP_LENGTH            251
#define C_TIM_BMCAST_BIT              0x01

#define C_IBSS_ELEMENT_ID                6
#define C_IBSS_ELEMENT_LENGTH            2

#define C_JAPAN_CALL_SIGN_ELEMENT_ID    51
#define C_JAPAN_CALL_SIGN_ELEMENT_LNGTH 12

#define C_DISASSOC_REASON_CODE_LEN       2
#define C_DISASSOC_REASON_CODE_DEFAULT   8

#define C_CRC_LEN                        4
#define C_NUM_SUPPORTED_RATES            8 
/****** IEEE 802.11 mac header for type data packets *************************/
struct mac_header {
  UCHAR frame_ctl_1;                          
  UCHAR frame_ctl_2;
  UCHAR duration_lsb;
  UCHAR duration_msb;
  UCHAR addr_1[ADDRLEN];
  UCHAR addr_2[ADDRLEN];
  UCHAR addr_3[ADDRLEN];
  UCHAR seq_frag_num[2];
/*  UCHAR addr_4[ADDRLEN]; *//* only present for AP to AP (TO DS and FROM DS */
};
/****** IEEE 802.11 frame element structures *********************************/
struct essid_element
{
  UCHAR id;
  UCHAR length;
  UCHAR text[C_ESSID_ELEMENT_MAX_LENGTH];
};
struct rates_element
{
  UCHAR id;
  UCHAR length;
  UCHAR value[8];
};
struct freq_hop_element
{
  UCHAR id;
  UCHAR length;
  UCHAR dwell_time[2];
  UCHAR hop_set;
  UCHAR hop_pattern;
  UCHAR hop_index;
};
struct tim_element
{
  UCHAR id;
  UCHAR length;
  UCHAR dtim_count;
  UCHAR dtim_period;    
  UCHAR bitmap_control;
  UCHAR tim[C_TIM_BITMAP_LENGTH];
};
struct ibss_element
{
  UCHAR id;
  UCHAR length;
  UCHAR atim_window[2];
};
struct japan_call_sign_element
{
  UCHAR id;
  UCHAR length;
  UCHAR call_sign[12];
};
/****** Beacon message structures ********************************************/
/* .elements is a large lump of max size because elements are variable size  */
struct infra_beacon
{
    UCHAR timestamp[8];
    UCHAR beacon_intvl[2];
    UCHAR capability[2];
    UCHAR elements[sizeof(struct essid_element) 
                  + sizeof(struct rates_element)
                  + sizeof(struct freq_hop_element) 
                  + sizeof(struct japan_call_sign_element)
                  + sizeof(struct tim_element)];
};
struct adhoc_beacon
{
    UCHAR timestamp[8];
    UCHAR beacon_intvl[2];
    UCHAR capability[2];
    UCHAR elements[sizeof(struct essid_element) 
                  + sizeof(struct rates_element)
                  + sizeof(struct freq_hop_element) 
                  + sizeof(struct japan_call_sign_element)
                  + sizeof(struct ibss_element)];
};
/*****************************************************************************/
/*****************************************************************************/
/* #define C_MAC_HDR_2_WEP 0x40 */
/* TX/RX CCS constants */
#define TX_HEADER_LENGTH 0x1C
#define RX_MAC_HEADER_LENGTH 0x18
#define TX_AUTHENTICATE_LENGTH (TX_HEADER_LENGTH + 6)
#define TX_AUTHENTICATE_LENGTH_MSB (TX_AUTHENTICATE_LENGTH >> 8)
#define TX_AUTHENTICATE_LENGTH_LSB (TX_AUTHENTICATE_LENGTH & 0xff)
#define TX_DEAUTHENTICATE_LENGTH (TX_HEADER_LENGTH + 2)
#define TX_DEAUTHENTICATE_LENGTH_MSB (TX_AUTHENTICATE_LENGTH >> 8)
#define TX_DEAUTHENTICATE_LENGTH_LSB (TX_AUTHENTICATE_LENGTH & 0xff)
#define FCS_LEN           4

#define ADHOC                 0
#define INFRA                 1

#define TYPE_STA              0
#define TYPE_AP               1

#define PASSIVE_SCAN          1
#define ACTIVE_SCAN           1

#define PSM_CAM               0

/* Country codes */
#define USA                   1
#define EUROPE                2
#define JAPAN                 3
#define KOREA                 4
#define SPAIN                 5
#define FRANCE                6
#define ISRAEL                7
#define AUSTRALIA             8
#define JAPAN_TEST            9

/* Hop pattern lengths */
#define USA_HOP_MOD          79 
#define EUROPE_HOP_MOD       79 
#define JAPAN_HOP_MOD        23
#define KOREA_HOP_MOD        23
#define SPAIN_HOP_MOD        27
#define FRANCE_HOP_MOD       35
#define ISRAEL_HOP_MOD       35
#define AUSTRALIA_HOP_MOD    47
#define JAPAN_TEST_HOP_MOD   23

#define ESSID_SIZE           32
/**********************************************************************/
/* CIS Register Constants */
#define CIS_OFFSET             0x0f00
/* Configuration Option Register (0x0F00) */
#define COR_OFFSET             0x00
#define COR_SOFT_RESET         0x80
#define COR_LEVEL_IRQ          0x40
#define COR_CONFIG_NUM         0x01
#define COR_DEFAULT            (COR_LEVEL_IRQ | COR_CONFIG_NUM)

/* Card Configuration and Status Register (0x0F01) */
#define CCSR_OFFSET            0x01
#define CCSR_HOST_INTR_PENDING 0x01
#define CCSR_POWER_DOWN        0x04

/* HCS Interrupt Register (0x0F05) */
#define HCS_INTR_OFFSET        0x05
/* #define HCS_INTR_OFFSET        0x0A */
#define HCS_INTR_CLEAR         0x00

/* ECF Interrupt Register (0x0F06) */
#define ECF_INTR_OFFSET        0x06
/* #define ECF_INTR_OFFSET        0x0C */
#define ECF_INTR_SET           0x01

/* Authorization Register 0 (0x0F08) */
#define AUTH_0_ON              0x57

/* Authorization Register 1 (0x0F09) */
#define AUTH_1_ON              0x82

/* Program Mode Register (0x0F0A) */
#define PC2PM                  0x02
#define PC2CAL                 0x10
#define PC2MLSE                0x20

/* PC Test Mode Register (0x0F0B) */
#define PC_TEST_MODE           0x08

/* Frequency Control Word (0x0F10) */
/* Range 0x02 - 0xA6 */

/* Test Mode Control 1-4 (0x0F14 - 0x0F17) */

/**********************************************************************/

/* Shared RAM Area */
#define SCB_BASE               0x0000
#define STATUS_BASE            0x0100
#define HOST_TO_ECF_BASE       0x0200
#define ECF_TO_HOST_BASE       0x0300
#define CCS_BASE               0x0400
#define RCS_BASE               0x0800
#define INFRA_TIM_BASE         0x0C00
#define SSID_LIST_BASE         0x0D00
#define TX_BUF_BASE            0x1000
#define RX_BUF_BASE            0x8000

#define NUMBER_OF_CCS    64
#define NUMBER_OF_RCS    64
/*#define NUMBER_OF_TX_CCS 14 */
#define NUMBER_OF_TX_CCS 14

#define TX_BUF_SIZE      (2048 - sizeof(struct tx_msg))
#define RX_BUFF_END      0x3FFF
/* Values for buffer_status */
#define CCS_BUFFER_FREE       0
#define CCS_BUFFER_BUSY       1
#define CCS_COMMAND_COMPLETE  2
#define CCS_COMMAND_FAILED    3

/* Values for cmd */
#define CCS_DOWNLOAD_STARTUP_PARAMS    1
#define CCS_UPDATE_PARAMS              2
#define CCS_REPORT_PARAMS              3
#define CCS_UPDATE_MULTICAST_LIST      4
#define CCS_UPDATE_POWER_SAVINGS_MODE  5
#define CCS_START_NETWORK              6
#define CCS_JOIN_NETWORK               7
#define CCS_START_ASSOCIATION          8
#define CCS_TX_REQUEST                 9
#define CCS_TEST_MEMORY              0xa
#define CCS_SHUTDOWN                 0xb
#define CCS_DUMP_MEMORY              0xc
#define CCS_START_TIMER              0xe
#define CCS_LAST_CMD                 CCS_START_TIMER

/* Values for link field */
#define CCS_END_LIST                 0xff

/* values for buffer_status field */
#define RCS_BUFFER_FREE       0
#define RCS_BUFFER_BUSY       1
#define RCS_COMPLETE          2
#define RCS_FAILED            3
#define RCS_BUFFER_RELEASE    0xFF

/* values for interrupt_id field */
#define PROCESS_RX_PACKET           0x80 /* */
#define REJOIN_NET_COMPLETE         0x81 /* RCS ID: Rejoin Net Complete */
#define ROAMING_INITIATED           0x82 /* RCS ID: Roaming Initiated   */
#define JAPAN_CALL_SIGN_RXD         0x83 /* RCS ID: New Japan Call Sign */

/*****************************************************************************/
/* Memory types for dump memory command */
#define C_MEM_PROG  0
#define C_MEM_XDATA 1
#define C_MEM_SFR   2
#define C_MEM_IDATA 3

/*** Return values for hw_xmit **********/
#define XMIT_OK        (0)
#define XMIT_MSG_BAD   (-1)
#define XMIT_NO_CCS    (-2)
#define XMIT_NO_INTR   (-3)
#define XMIT_NEED_AUTH (-4)

/*** Values for card status */
#define CARD_INSERTED       (0)

#define CARD_AWAITING_PARAM (1)
#define CARD_INIT_ERROR     (11)

#define CARD_DL_PARAM       (2)
#define CARD_DL_PARAM_ERROR (12)

#define CARD_DOING_ACQ      (3)

#define CARD_ACQ_COMPLETE   (4)
#define CARD_ACQ_FAILED     (14)

#define CARD_AUTH_COMPLETE  (5)
#define CARD_AUTH_REFUSED   (15)

#define CARD_ASSOC_COMPLETE (6)
#define CARD_ASSOC_FAILED   (16)

/*** Values for authentication_state ***********************************/
#define UNAUTHENTICATED     (0)
#define AWAITING_RESPONSE   (1)
#define AUTHENTICATED       (2)
#define NEED_TO_AUTH        (3)

/*** Values for authentication type ************************************/
#define OPEN_AUTH_REQUEST   (1)
#define OPEN_AUTH_RESPONSE  (2)
#define BROADCAST_DEAUTH    (0xc0)
/*** Values for timer functions ****************************************/
#define TODO_NOTHING              (0)
#define TODO_VERIFY_DL_START      (-1)
#define TODO_START_NET            (-2)
#define TODO_JOIN_NET             (-3)
#define TODO_AUTHENTICATE_TIMEOUT (-4)
#define TODO_SEND_CCS             (-5)
/***********************************************************************/
/* Parameter passing structure for update/report parameter CCS's */
struct object_id {
    void          *object_addr;
    unsigned char object_length;
};

#define OBJID_network_type            0
#define OBJID_acting_as_ap_status     1
#define OBJID_current_ess_id          2
#define OBJID_scanning_mode           3
#define OBJID_power_mgt_state         4
#define OBJID_mac_address             5
#define OBJID_frag_threshold          6
#define OBJID_hop_time                7
#define OBJID_beacon_period           8
#define OBJID_dtim_period             9
#define OBJID_retry_max              10
#define OBJID_ack_timeout            11
#define OBJID_sifs                   12
#define OBJID_difs                   13
#define OBJID_pifs                   14
#define OBJID_rts_threshold          15
#define OBJID_scan_dwell_time        16
#define OBJID_max_scan_dwell_time    17
#define OBJID_assoc_resp_timeout     18
#define OBJID_adhoc_scan_cycle_max   19
#define OBJID_infra_scan_cycle_max   20
#define OBJID_infra_super_cycle_max  21
#define OBJID_promiscuous_mode       22
#define OBJID_unique_word            23
#define OBJID_slot_time              24
#define OBJID_roaming_low_snr        25
#define OBJID_low_snr_count_thresh   26
#define OBJID_infra_missed_bcn       27
#define OBJID_adhoc_missed_bcn       28
#define OBJID_curr_country_code      29
#define OBJID_hop_pattern            30
#define OBJID_reserved               31
#define OBJID_cw_max_msb             32
#define OBJID_cw_min_msb             33
#define OBJID_noise_filter_gain      34
#define OBJID_noise_limit_offset     35
#define OBJID_det_rssi_thresh_offset 36
#define OBJID_med_busy_thresh_offset 37
#define OBJID_det_sync_thresh        38
#define OBJID_test_mode              39
#define OBJID_test_min_chan_num      40
#define OBJID_test_max_chan_num      41
#define OBJID_allow_bcast_ID_prbrsp  42
#define OBJID_privacy_must_start     43
#define OBJID_privacy_can_join       44
#define OBJID_basic_rate_set         45

/**** Configuration/Status/Control Area ***************************/
/*    System Control Block (SCB) Area
 *    Located at Shared RAM offset 0
 */
struct scb {
    UCHAR ccs_index;
    UCHAR rcs_index;
};

/****** Status area at Shared RAM offset 0x0100 ******************************/
struct status {
    UCHAR mrx_overflow_for_host;         /* 0=ECF may write, 1=host may write*/
    UCHAR mrx_checksum_error_for_host;   /* 0=ECF may write, 1=host may write*/
    UCHAR rx_hec_error_for_host;         /* 0=ECF may write, 1=host may write*/
    UCHAR reserved1;
    short mrx_overflow;                  /* ECF increments on rx overflow    */
    short mrx_checksum_error;            /* ECF increments on rx CRC error   */
    short rx_hec_error;                  /* ECF incs on mac header CRC error */
    UCHAR rxnoise;                       /* Average RSL measurement          */
};

/****** Host-to-ECF Data Area at Shared RAM offset 0x200 *********************/
struct host_to_ecf_area {
    
};

/****** ECF-to-Host Data Area at Shared RAM offset 0x0300 ********************/
struct startup_res_518 {
    UCHAR startup_word;
    UCHAR station_addr[ADDRLEN];
    UCHAR calc_prog_chksum;
    UCHAR calc_cis_chksum;
    UCHAR ecf_spare[7];
    UCHAR japan_call_sign[12];
};

struct startup_res_6 {
    UCHAR startup_word;
    UCHAR station_addr[ADDRLEN];
    UCHAR reserved;
    UCHAR supp_rates[8];
    UCHAR japan_call_sign[12];
    UCHAR calc_prog_chksum;
    UCHAR calc_cis_chksum;
    UCHAR firmware_version[3];
    UCHAR asic_version;
    UCHAR tib_length;
};

struct start_join_net_params {
    UCHAR net_type;
    UCHAR ssid[ESSID_SIZE];
    UCHAR reserved;
    UCHAR privacy_can_join;
};

/****** Command Control Structure area at Shared ram offset 0x0400 ***********/
/* Structures for command specific parameters (ccs.var) */
struct update_param_cmd {
    UCHAR object_id;
    UCHAR number_objects;
    UCHAR failure_cause;
};
struct report_param_cmd {
    UCHAR object_id;
    UCHAR number_objects;
    UCHAR failure_cause;
    UCHAR length;
};
struct start_network_cmd {
    UCHAR update_param;
    UCHAR bssid[ADDRLEN];
    UCHAR net_initiated;
    UCHAR net_default_tx_rate;
    UCHAR encryption;
};
struct join_network_cmd {
    UCHAR update_param;
    UCHAR bssid[ADDRLEN];
    UCHAR net_initiated;
    UCHAR net_default_tx_rate;
    UCHAR encryption;
};
struct tx_requested_cmd {
 
    UCHAR tx_data_ptr[2];
    UCHAR tx_data_length[2];
    UCHAR host_reserved[2];
    UCHAR reserved[3];
    UCHAR tx_rate;
    UCHAR pow_sav_mode;
    UCHAR retries;
    UCHAR antenna;
};
struct tx_requested_cmd_4 {
 
    UCHAR tx_data_ptr[2];
    UCHAR tx_data_length[2];
    UCHAR dest_addr[ADDRLEN];
    UCHAR pow_sav_mode;
    UCHAR retries;
    UCHAR station_id;
};
struct memory_dump_cmd {
    UCHAR memory_type;
    UCHAR memory_ptr[2];
    UCHAR length;
};
struct update_association_cmd {
    UCHAR status;
    UCHAR aid[2];
};
struct start_timer_cmd {
    UCHAR duration[2];
};

struct ccs {
    UCHAR buffer_status;                 /* 0 = buffer free, 1 = buffer busy */
                                         /* 2 = command complete, 3 = failed */
    UCHAR cmd;                           /* command to ECF                   */
    UCHAR link;                          /* link to next CCS, FF=end of list */
    /* command specific parameters      */
    union {
        char reserved[13];
        struct update_param_cmd update_param;
        struct report_param_cmd report_param;
        UCHAR nummulticast;
        UCHAR mode;
        struct start_network_cmd start_network;
        struct join_network_cmd join_network;
        struct tx_requested_cmd tx_request;
        struct memory_dump_cmd memory_dump;
        struct update_association_cmd update_assoc;
        struct start_timer_cmd start_timer;
    } var;
};

/*****************************************************************************/
/* Transmit buffer structures */
struct tib_structure {
    UCHAR ccs_index;
    UCHAR psm;
    UCHAR pass_fail;
    UCHAR retry_count;
    UCHAR max_retries;
    UCHAR frags_remaining;
    UCHAR no_rb;
    UCHAR rts_reqd;
    UCHAR csma_tx_cntrl_2;
    UCHAR sifs_tx_cntrl_2;
    UCHAR tx_dma_addr_1[2];
    UCHAR tx_dma_addr_2[2];
    UCHAR var_dur_2mhz[2];
    UCHAR var_dur_1mhz[2];
    UCHAR max_dur_2mhz[2];
    UCHAR max_dur_1mhz[2];
    UCHAR hdr_len;
    UCHAR max_frag_len[2];
    UCHAR var_len[2];
    UCHAR phy_hdr_4;
    UCHAR mac_hdr_1;
    UCHAR mac_hdr_2;
    UCHAR sid[2];
};

struct phy_header {
    UCHAR sfd[2];
    UCHAR hdr_3;
    UCHAR hdr_4;
};
struct ray_rx_msg {
    struct mac_header mac;
    UCHAR  var[0];
};

struct tx_msg {
    struct tib_structure tib;
    struct phy_header phy;
    struct mac_header mac;
    UCHAR  var[1];
};

/****** ECF Receive Control Structure (RCS) Area at Shared RAM offset 0x0800  */
/* Structures for command specific parameters (rcs.var) */
struct rx_packet_cmd {
    UCHAR rx_data_ptr[2];
    UCHAR rx_data_length[2];
    UCHAR rx_sig_lev;
    UCHAR next_frag_rcs_index;
    UCHAR totalpacketlength[2];
};
struct rejoin_net_cmplt_cmd {
    UCHAR reserved;
    UCHAR bssid[ADDRLEN];
};
struct japan_call_sign_rxd {
    UCHAR rxd_call_sign[8];
    UCHAR reserved[5];
};

struct rcs {
    UCHAR buffer_status;
    UCHAR interrupt_id;
    UCHAR link_field;
    /* command specific parameters      */
    union {
        UCHAR reserved[13]; 
        struct rx_packet_cmd rx_packet;
        struct rejoin_net_cmplt_cmd rejoin_net_complete;
        struct japan_call_sign_rxd japan_call_sign;
    } var;
};

/****** Startup parameter structures for both versions of firmware ***********/
struct b4_startup_params {
    UCHAR a_network_type;                /* C_ADHOC, C_INFRA                 */
    UCHAR a_acting_as_ap_status;         /* C_TYPE_STA, C_TYPE_AP            */
    UCHAR a_current_ess_id[ESSID_SIZE];  /* Null terminated unless 32 long   */
    UCHAR a_scanning_mode;               /* passive 0, active 1              */
    UCHAR a_power_mgt_state;             /* CAM 0,                           */
    UCHAR a_mac_addr[ADDRLEN];           /*                                  */
    UCHAR a_frag_threshold[2];           /* 512                              */
    UCHAR a_hop_time[2];                 /* 16k * 2**n, n=0-4 in Kus         */
    UCHAR a_beacon_period[2];            /* n * a_hop_time  in Kus           */
    UCHAR a_dtim_period;                 /* in beacons                       */
    UCHAR a_retry_max;                   /*                                  */
    UCHAR a_ack_timeout;                 /*                                  */
    UCHAR a_sifs;                        /*                                  */
    UCHAR a_difs;                        /*                                  */
    UCHAR a_pifs;                        /*                                  */
    UCHAR a_rts_threshold[2];            /*                                  */
    UCHAR a_scan_dwell_time[2];          /*                                  */
    UCHAR a_max_scan_dwell_time[2];      /*                                  */
    UCHAR a_assoc_resp_timeout_thresh;   /*                                  */
    UCHAR a_adhoc_scan_cycle_max;        /*                                  */
    UCHAR a_infra_scan_cycle_max;        /*                                  */
    UCHAR a_infra_super_scan_cycle_max;  /*                                  */
    UCHAR a_promiscuous_mode;            /*                                  */
    UCHAR a_unique_word[2];              /*                                  */
    UCHAR a_slot_time;                   /*                                  */
    UCHAR a_roaming_low_snr_thresh;      /*                                  */
    UCHAR a_low_snr_count_thresh;        /*                                  */
    UCHAR a_infra_missed_bcn_thresh;     /*                                  */
    UCHAR a_adhoc_missed_bcn_thresh;     /*                                  */
    UCHAR a_curr_country_code;           /* C_USA                            */
    UCHAR a_hop_pattern;                 /*                                  */
    UCHAR a_hop_pattern_length;          /*                                  */
/* b4 - b5 differences start here */
    UCHAR a_cw_max;                      /*                                  */
    UCHAR a_cw_min;                      /*                                  */
    UCHAR a_noise_filter_gain;           /*                                  */
    UCHAR a_noise_limit_offset;          /*                                  */
    UCHAR a_det_rssi_thresh_offset;      /*                                  */
    UCHAR a_med_busy_thresh_offset;      /*                                  */
    UCHAR a_det_sync_thresh;             /*                                  */
    UCHAR a_test_mode;                   /*                                  */
    UCHAR a_test_min_chan_num;           /*                                  */
    UCHAR a_test_max_chan_num;           /*                                  */
    UCHAR a_rx_tx_delay;                 /*                                  */
    UCHAR a_current_bss_id[ADDRLEN];     /*                                  */
    UCHAR a_hop_set;                     /*                                  */
};
struct b5_startup_params {
    UCHAR a_network_type;                /* C_ADHOC, C_INFRA                 */
    UCHAR a_acting_as_ap_status;         /* C_TYPE_STA, C_TYPE_AP            */
    UCHAR a_current_ess_id[ESSID_SIZE];  /* Null terminated unless 32 long   */
    UCHAR a_scanning_mode;               /* passive 0, active 1              */
    UCHAR a_power_mgt_state;             /* CAM 0,                           */
    UCHAR a_mac_addr[ADDRLEN];           /*                                  */
    UCHAR a_frag_threshold[2];           /* 512                              */
    UCHAR a_hop_time[2];                 /* 16k * 2**n, n=0-4 in Kus         */
    UCHAR a_beacon_period[2];            /* n * a_hop_time  in Kus           */
    UCHAR a_dtim_period;                 /* in beacons                       */
    UCHAR a_retry_max;                   /* 4                                */
    UCHAR a_ack_timeout;                 /*                                  */
    UCHAR a_sifs;                        /*                                  */
    UCHAR a_difs;                        /*                                  */
    UCHAR a_pifs;                        /*                                  */
    UCHAR a_rts_threshold[2];            /*                                  */
    UCHAR a_scan_dwell_time[2];          /*                                  */
    UCHAR a_max_scan_dwell_time[2];      /*                                  */
    UCHAR a_assoc_resp_timeout_thresh;   /*                                  */
    UCHAR a_adhoc_scan_cycle_max;        /*                                  */
    UCHAR a_infra_scan_cycle_max;        /*                                  */
    UCHAR a_infra_super_scan_cycle_max;  /*                                  */
    UCHAR a_promiscuous_mode;            /*                                  */
    UCHAR a_unique_word[2];              /*                                  */
    UCHAR a_slot_time;                   /*                                  */
    UCHAR a_roaming_low_snr_thresh;      /*                                  */
    UCHAR a_low_snr_count_thresh;        /*                                  */
    UCHAR a_infra_missed_bcn_thresh;     /*                                  */
    UCHAR a_adhoc_missed_bcn_thresh;     /*                                  */
    UCHAR a_curr_country_code;           /* C_USA                            */
    UCHAR a_hop_pattern;                 /*                                  */
    UCHAR a_hop_pattern_length;          /*                                  */
/* b4 - b5 differences start here */
    UCHAR a_cw_max[2];                   /*                                  */
    UCHAR a_cw_min[2];                   /*                                  */
    UCHAR a_noise_filter_gain;           /*                                  */
    UCHAR a_noise_limit_offset;          /*                                  */
    UCHAR a_det_rssi_thresh_offset;      /*                                  */
    UCHAR a_med_busy_thresh_offset;      /*                                  */
    UCHAR a_det_sync_thresh;             /*                                  */
    UCHAR a_test_mode;                   /*                                  */
    UCHAR a_test_min_chan_num;           /*                                  */
    UCHAR a_test_max_chan_num;           /*                                  */
    UCHAR a_allow_bcast_SSID_probe_rsp;
    UCHAR a_privacy_must_start;
    UCHAR a_privacy_can_join;
    UCHAR a_basic_rate_set[8];
};

/*****************************************************************************/
#define RAY_IOCG_PARMS (SIOCDEVPRIVATE)
#define RAY_IOCS_PARMS (SIOCDEVPRIVATE + 1)
#define RAY_DO_CMD     (SIOCDEVPRIVATE + 2)

/****** ethernet <-> 802.11 translation **************************************/
typedef struct snaphdr_t
{
  UCHAR   dsap;
  UCHAR   ssap;
  UCHAR   ctrl;
  UCHAR   org[3];
  UCHAR   ethertype[2];
} snaphdr_t;

#define BRIDGE_ENCAP  0xf80000
#define RFC1042_ENCAP 0
#define SNAP_ID       0x0003aaaa
#define RAY_IPX_TYPE  0x8137
#define APPLEARP_TYPE 0x80f3
/*****************************************************************************/
#endif /* #ifndef RAYLINK_H */
