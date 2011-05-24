//-
// Copyright (c) 2009-2010 Atheros Communications Inc.
// All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//
//


#ifndef __A_HCI_H__
#define __A_HCI_H__

#define HCI_CMD_OGF_MASK            0x3F
#define HCI_CMD_OGF_SHIFT           10
#define HCI_CMD_GET_OGF(opcode)     ((opcode >> HCI_CMD_OGF_SHIFT) & HCI_CMD_OGF_MASK)

#define HCI_CMD_OCF_MASK            0x3FF
#define HCI_CMD_OCF_SHIFT           0 
#define HCI_CMD_GET_OCF(opcode)     (((opcode) >> HCI_CMD_OCF_SHIFT) & HCI_CMD_OCF_MASK)

#define HCI_FORM_OPCODE(ocf, ogf)    ((ocf & HCI_CMD_OCF_MASK) << HCI_CMD_OCF_SHIFT | \
                                          (ogf & HCI_CMD_OGF_MASK) << HCI_CMD_OGF_SHIFT)


/*======== HCI Opcode groups ===============*/
#define OGF_NOP                         0x00
#define OGF_LINK_CONTROL                0x01
#define OGF_LINK_POLICY                 0x03
#define OGF_INFO_PARAMS                 0x04
#define OGF_STATUS                      0x05
#define OGF_TESTING                     0x06
#define OGF_BLUETOOTH                   0x3E
#define OGF_VENDOR_DEBUG                0x3F



#define OCF_NOP                         0x00


/*===== Link Control Commands Opcode===================*/
#define OCF_HCI_Create_Physical_Link                0x35
#define OCF_HCI_Accept_Physical_Link_Req            0x36
#define OCF_HCI_Disconnect_Physical_Link            0x37
#define OCF_HCI_Create_Logical_Link                 0x38
#define OCF_HCI_Accept_Logical_Link                 0x39
#define OCF_HCI_Disconnect_Logical_Link             0x3A
#define OCF_HCI_Logical_Link_Cancel                 0x3B
#define OCF_HCI_Flow_Spec_Modify                    0x3C



/*===== Link Policy Commands Opcode====================*/
#define OCF_HCI_Set_Event_Mask                      0x01
#define OCF_HCI_Reset                               0x03
#define OCF_HCI_Read_Conn_Accept_Timeout            0x15
#define OCF_HCI_Write_Conn_Accept_Timeout           0x16
#define OCF_HCI_Read_Link_Supervision_Timeout       0x36
#define OCF_HCI_Write_Link_Supervision_Timeout      0x37
#define OCF_HCI_Enhanced_Flush                      0x5F
#define OCF_HCI_Read_Logical_Link_Accept_Timeout    0x61
#define OCF_HCI_Write_Logical_Link_Accept_Timeout   0x62
#define OCF_HCI_Set_Event_Mask_Page_2               0x63
#define OCF_HCI_Read_Location_Data                  0x64
#define OCF_HCI_Write_Location_Data                 0x65
#define OCF_HCI_Read_Flow_Control_Mode              0x66
#define OCF_HCI_Write_Flow_Control_Mode             0x67
#define OCF_HCI_Read_BE_Flush_Timeout               0x69
#define OCF_HCI_Write_BE_Flush_Timeout              0x6A
#define OCF_HCI_Short_Range_Mode                    0x6B


/*======== Info Commands Opcode========================*/
#define OCF_HCI_Read_Local_Ver_Info                 0x01
#define OCF_HCI_Read_Local_Supported_Cmds           0x02
#define OCF_HCI_Read_Data_Block_Size                0x0A
/*======== Status Commands Opcode======================*/
#define OCF_HCI_Read_Failed_Contact_Counter         0x01
#define OCF_HCI_Reset_Failed_Contact_Counter        0x02
#define OCF_HCI_Read_Link_Quality                   0x03
#define OCF_HCI_Read_RSSI                           0x05
#define OCF_HCI_Read_Local_AMP_Info                 0x09    
#define OCF_HCI_Read_Local_AMP_ASSOC                0x0A
#define OCF_HCI_Write_Remote_AMP_ASSOC              0x0B


/*======= AMP_ASSOC Specific TLV tags =================*/
#define AMP_ASSOC_MAC_ADDRESS_INFO_TYPE             0x1
#define AMP_ASSOC_PREF_CHAN_LIST                    0x2
#define AMP_ASSOC_CONNECTED_CHAN                    0x3
#define AMP_ASSOC_PAL_CAPABILITIES                  0x4
#define AMP_ASSOC_PAL_VERSION                       0x5


/*========= PAL Events =================================*/
#define PAL_COMMAND_COMPLETE_EVENT                  0x0E
#define PAL_COMMAND_STATUS_EVENT                    0x0F
#define PAL_HARDWARE_ERROR_EVENT                    0x10
#define PAL_FLUSH_OCCURRED_EVENT                    0x11
#define PAL_LOOPBACK_EVENT                          0x19
#define PAL_BUFFER_OVERFLOW_EVENT                   0x1A
#define PAL_QOS_VIOLATION_EVENT                     0x1E
#define PAL_ENHANCED_FLUSH_COMPLT_EVENT             0x39
#define PAL_PHYSICAL_LINK_COMPL_EVENT               0x40
#define PAL_CHANNEL_SELECT_EVENT                    0x41
#define PAL_DISCONNECT_PHYSICAL_LINK_EVENT          0x42
#define PAL_PHY_LINK_EARLY_LOSS_WARNING_EVENT       0x43
#define PAL_PHY_LINK_RECOVERY_EVENT                 0x44
#define PAL_LOGICAL_LINK_COMPL_EVENT                0x45
#define PAL_DISCONNECT_LOGICAL_LINK_COMPL_EVENT     0x46
#define PAL_FLOW_SPEC_MODIFY_COMPL_EVENT            0x47
#define PAL_NUM_COMPL_DATA_BLOCK_EVENT              0x48
#define PAL_SHORT_RANGE_MODE_CHANGE_COMPL_EVENT     0x4C
#define PAL_AMP_STATUS_CHANGE_EVENT                 0x4D
/*======== End of PAL events definition =================*/


/*======== Timeouts (not part of HCI cmd, but input to PAL engine) =========*/
#define Timer_Conn_Accept_TO                        0x01
#define Timer_Link_Supervision_TO                   0x02

#define NUM_HCI_COMMAND_PKTS                0x1


/*====== NOP Cmd ============================*/
#define HCI_CMD_NOP                     HCI_FORM_OPCODE(OCF_NOP, OGF_NOP)


/*===== Link Control Commands================*/
#define HCI_Create_Physical_Link        HCI_FORM_OPCODE(OCF_HCI_Create_Physical_Link, OGF_LINK_CONTROL)
#define HCI_Accept_Physical_Link_Req    HCI_FORM_OPCODE(OCF_HCI_Accept_Physical_Link_Req, OGF_LINK_CONTROL)
#define HCI_Disconnect_Physical_Link    HCI_FORM_OPCODE(OCF_HCI_Disconnect_Physical_Link, OGF_LINK_CONTROL)
#define HCI_Create_Logical_Link         HCI_FORM_OPCODE(OCF_HCI_Create_Logical_Link, OGF_LINK_CONTROL)
#define HCI_Accept_Logical_Link         HCI_FORM_OPCODE(OCF_HCI_Accept_Logical_Link, OGF_LINK_CONTROL)
#define HCI_Disconnect_Logical_Link     HCI_FORM_OPCODE(OCF_HCI_Disconnect_Logical_Link, OGF_LINK_CONTROL)
#define HCI_Logical_Link_Cancel         HCI_FORM_OPCODE(OCF_HCI_Logical_Link_Cancel, OGF_LINK_CONTROL)
#define HCI_Flow_Spec_Modify            HCI_FORM_OPCODE(OCF_HCI_Flow_Spec_Modify, OGF_LINK_CONTROL)


/*===== Link Policy Commands ================*/
#define HCI_Set_Event_Mask              HCI_FORM_OPCODE(OCF_HCI_Set_Event_Mask, OGF_LINK_POLICY)
#define HCI_Reset                       HCI_FORM_OPCODE(OCF_HCI_Reset, OGF_LINK_POLICY)
#define HCI_Enhanced_Flush              HCI_FORM_OPCODE(OCF_HCI_Enhanced_Flush, OGF_LINK_POLICY)
#define HCI_Read_Conn_Accept_Timeout    HCI_FORM_OPCODE(OCF_HCI_Read_Conn_Accept_Timeout, OGF_LINK_POLICY)
#define HCI_Write_Conn_Accept_Timeout   HCI_FORM_OPCODE(OCF_HCI_Write_Conn_Accept_Timeout, OGF_LINK_POLICY)
#define HCI_Read_Logical_Link_Accept_Timeout    HCI_FORM_OPCODE(OCF_HCI_Read_Logical_Link_Accept_Timeout, OGF_LINK_POLICY)
#define HCI_Write_Logical_Link_Accept_Timeout   HCI_FORM_OPCODE(OCF_HCI_Write_Logical_Link_Accept_Timeout, OGF_LINK_POLICY)
#define HCI_Read_Link_Supervision_Timeout       HCI_FORM_OPCODE(OCF_HCI_Read_Link_Supervision_Timeout, OGF_LINK_POLICY)
#define HCI_Write_Link_Supervision_Timeout      HCI_FORM_OPCODE(OCF_HCI_Write_Link_Supervision_Timeout, OGF_LINK_POLICY)
#define HCI_Read_Location_Data          HCI_FORM_OPCODE(OCF_HCI_Read_Location_Data, OGF_LINK_POLICY)
#define HCI_Write_Location_Data         HCI_FORM_OPCODE(OCF_HCI_Write_Location_Data, OGF_LINK_POLICY)
#define HCI_Set_Event_Mask_Page_2       HCI_FORM_OPCODE(OCF_HCI_Set_Event_Mask_Page_2, OGF_LINK_POLICY)
#define HCI_Read_Flow_Control_Mode      HCI_FORM_OPCODE(OCF_HCI_Read_Flow_Control_Mode, OGF_LINK_POLICY)
#define HCI_Write_Flow_Control_Mode     HCI_FORM_OPCODE(OCF_HCI_Write_Flow_Control_Mode, OGF_LINK_POLICY)
#define HCI_Write_BE_Flush_Timeout      HCI_FORM_OPCODE(OCF_HCI_Write_BE_Flush_Timeout, OGF_LINK_POLICY)
#define HCI_Read_BE_Flush_Timeout       HCI_FORM_OPCODE(OCF_HCI_Read_BE_Flush_Timeout, OGF_LINK_POLICY)
#define HCI_Short_Range_Mode            HCI_FORM_OPCODE(OCF_HCI_Short_Range_Mode, OGF_LINK_POLICY)            


/*===== Info Commands =====================*/
#define HCI_Read_Local_Ver_Info         HCI_FORM_OPCODE(OCF_HCI_Read_Local_Ver_Info,  OGF_INFO_PARAMS)
#define HCI_Read_Local_Supported_Cmds   HCI_FORM_OPCODE(OCF_HCI_Read_Local_Supported_Cmds, OGF_INFO_PARAMS)
#define HCI_Read_Data_Block_Size        HCI_FORM_OPCODE(OCF_HCI_Read_Data_Block_Size, OGF_INFO_PARAMS)

/*===== Status Commands =====================*/
#define HCI_Read_Link_Quality           HCI_FORM_OPCODE(OCF_HCI_Read_Link_Quality, OGF_STATUS)
#define HCI_Read_RSSI                   HCI_FORM_OPCODE(OCF_HCI_Read_RSSI, OGF_STATUS)
#define HCI_Read_Local_AMP_Info         HCI_FORM_OPCODE(OCF_HCI_Read_Local_AMP_Info, OGF_STATUS)
#define HCI_Read_Local_AMP_ASSOC        HCI_FORM_OPCODE(OCF_HCI_Read_Local_AMP_ASSOC, OGF_STATUS)
#define HCI_Write_Remote_AMP_ASSOC      HCI_FORM_OPCODE(OCF_HCI_Write_Remote_AMP_ASSOC, OGF_STATUS)

/*====== End of cmd definitions =============*/



/*===== Timeouts(private - can't come from HCI)=================*/
#define Conn_Accept_TO                  HCI_FORM_OPCODE(Timer_Conn_Accept_TO, OGF_VENDOR_DEBUG)
#define Link_Supervision_TO             HCI_FORM_OPCODE(Timer_Link_Supervision_TO, OGF_VENDOR_DEBUG)

/*----- PAL Constants (Sec 6 of Doc)------------------------*/
#define Max80211_PAL_PDU_Size      1492
#define Max80211_AMP_ASSOC_Len      672
#define MinGUserPrio                4
#define MaxGUserPrio                7
#define BEUserPrio0                 0
#define BEUserPrio1                 3
#define Max80211BeaconPeriod        2000    /* in millisec */
#define ShortRangeModePowerMax      4       /* dBm */

/*------ PAL Protocol Identifiers (Sec5.1) ------------------*/
typedef enum {
    ACL_DATA = 0x01,
    ACTIVITY_REPORT,
    SECURED_FRAMES,
    LINK_SUPERVISION_REQ,
    LINK_SUPERVISION_RESP,
}PAL_PROTOCOL_IDENTIFIERS;

#define HCI_CMD_HDR_SZ          3
#define HCI_EVENT_HDR_SIZE      2
#define MAX_EVT_PKT_SZ          255
#define AMP_ASSOC_MAX_FRAG_SZ   248
#define AMP_MAX_GUARANTEED_BW   20000

#define DEFAULT_CONN_ACCPT_TO   5000
#define DEFAULT_LL_ACCPT_TO     5000
#define DEFAULT_LSTO            10000

#define PACKET_BASED_FLOW_CONTROL_MODE      0x00
#define DATA_BLK_BASED_FLOW_CONTROL_MODE    0x01

#define SERVICE_TYPE_BEST_EFFORT    0x01
#define SERVICE_TYPE_GUARANTEED     0x02

#define MAC_ADDR_LEN            6
#define LINK_KEY_LEN            32

typedef enum  {
    ACL_DATA_PB_1ST_NON_AUTOMATICALLY_FLUSHABLE = 0x00,
    ACL_DATA_PB_CONTINUING_FRAGMENT = 0x01,
    ACL_DATA_PB_1ST_AUTOMATICALLY_FLUSHABLE = 0x02,
    ACL_DATA_PB_COMPLETE_PDU = 0x03,
} ACL_DATA_PB_FLAGS;
#define ACL_DATA_PB_FLAGS_SHIFT     12

typedef enum {
    ACL_DATA_BC_POINT_TO_POINT = 0x00,
} ACL_DATA_BC_FLAGS;
#define ACL_DATA_BC_FLAGS_SHIFT     14

/* Command pkt */
typedef struct  hci_cmd_pkt_t {
    u16 opcode;
    u8 param_length;
    u8 params[255];
} POSTPACK HCI_CMD_PKT;

#define ACL_DATA_HDR_SIZE   4   /* hdl_and flags + data_len */
/* Data pkt */
typedef struct  hci_acl_data_pkt_t {
    u16 hdl_and_flags;
    u16 data_len;
    u8 data[Max80211_PAL_PDU_Size];
} POSTPACK HCI_ACL_DATA_PKT;

/* Event pkt */
typedef struct  hci_event_pkt_t {
    u8 event_code;
    u8 param_len;
    u8 params[256];
} POSTPACK HCI_EVENT_PKT;


/*============== HCI Command definitions ======================= */
typedef struct hci_cmd_phy_link_t {
    u16 opcode;
    u8 param_length;
    u8 phy_link_hdl;
    u8 link_key_len;
    u8 link_key_type;
    u8 link_key[LINK_KEY_LEN];
} POSTPACK HCI_CMD_PHY_LINK;

typedef struct  hci_cmd_write_rem_amp_assoc_t {
    u16 opcode;
    u8 param_length;
    u8 phy_link_hdl;
    u16 len_so_far;
    u16 amp_assoc_remaining_len;
    u8 amp_assoc_frag[AMP_ASSOC_MAX_FRAG_SZ];
} POSTPACK HCI_CMD_WRITE_REM_AMP_ASSOC;


typedef struct  hci_cmd_opcode_hdl_t {
    u16 opcode;
    u8 param_length;
    u16 hdl;
} POSTPACK HCI_CMD_READ_LINK_QUAL,
           HCI_CMD_FLUSH,
           HCI_CMD_READ_LINK_SUPERVISION_TIMEOUT;

typedef struct  hci_cmd_read_local_amp_assoc_t {
    u16 opcode;
    u8 param_length;
    u8 phy_link_hdl;
    u16 len_so_far;
    u16 max_rem_amp_assoc_len;
} POSTPACK HCI_CMD_READ_LOCAL_AMP_ASSOC;


typedef struct hci_cmd_set_event_mask_t {
    u16 opcode;
    u8 param_length;
    u64 mask;
}POSTPACK HCI_CMD_SET_EVT_MASK, HCI_CMD_SET_EVT_MASK_PG_2;


typedef struct  hci_cmd_enhanced_flush_t{
    u16 opcode;
    u8 param_length;
    u16 hdl;
    u8 type;
} POSTPACK HCI_CMD_ENHANCED_FLUSH;


typedef struct  hci_cmd_write_timeout_t {
    u16 opcode;
    u8 param_length;
    u16 timeout;
} POSTPACK  HCI_CMD_WRITE_TIMEOUT;

typedef struct  hci_cmd_write_link_supervision_timeout_t {
    u16 opcode;
    u8 param_length;
    u16 hdl;
    u16 timeout;
} POSTPACK HCI_CMD_WRITE_LINK_SUPERVISION_TIMEOUT;

typedef struct  hci_cmd_write_flow_control_t {
    u16 opcode;
    u8 param_length;
    u8 mode;
} POSTPACK  HCI_CMD_WRITE_FLOW_CONTROL;

typedef struct  location_data_cfg_t {
    u8 reg_domain_aware;
    u8 reg_domain[3];
    u8 reg_options;
} POSTPACK LOCATION_DATA_CFG;

typedef struct  hci_cmd_write_location_data_t {
    u16 opcode;
    u8 param_length;
    LOCATION_DATA_CFG   cfg;
} POSTPACK  HCI_CMD_WRITE_LOCATION_DATA;


typedef struct  flow_spec_t {
    u8 id;
    u8 service_type;
    u16 max_sdu;
    u32 sdu_inter_arrival_time;
    u32 access_latency;
    u32 flush_timeout;
} POSTPACK FLOW_SPEC;


typedef struct  hci_cmd_create_logical_link_t {
    u16 opcode;
    u8 param_length;
    u8 phy_link_hdl;
    FLOW_SPEC   tx_flow_spec;
    FLOW_SPEC   rx_flow_spec;
} POSTPACK HCI_CMD_CREATE_LOGICAL_LINK;

typedef struct  hci_cmd_flow_spec_modify_t {
    u16 opcode;
    u8 param_length;
    u16 hdl;
    FLOW_SPEC   tx_flow_spec;
    FLOW_SPEC   rx_flow_spec;
} POSTPACK HCI_CMD_FLOW_SPEC_MODIFY;

typedef struct hci_cmd_logical_link_cancel_t {
    u16 opcode;
    u8 param_length;
    u8 phy_link_hdl;
    u8 tx_flow_spec_id;
} POSTPACK HCI_CMD_LOGICAL_LINK_CANCEL;

typedef struct  hci_cmd_disconnect_logical_link_t {
    u16 opcode;
    u8 param_length;
    u16 logical_link_hdl;
} POSTPACK HCI_CMD_DISCONNECT_LOGICAL_LINK;

typedef struct  hci_cmd_disconnect_phy_link_t {
    u16 opcode;
    u8 param_length;
    u8 phy_link_hdl;
} POSTPACK HCI_CMD_DISCONNECT_PHY_LINK;

typedef struct  hci_cmd_srm_t {
    u16 opcode;
    u8 param_length;
    u8 phy_link_hdl;
    u8 mode;
} POSTPACK HCI_CMD_SHORT_RANGE_MODE;
/*============== HCI Command definitions end ======================= */



/*============== HCI Event definitions ============================= */

/* Command complete event */
typedef struct  hci_event_cmd_complete_t {
    u8 event_code;
    u8 param_len;
    u8 num_hci_cmd_pkts;
    u16 opcode;
    u8 params[255];
} POSTPACK HCI_EVENT_CMD_COMPLETE;


/* Command status event */
typedef struct  hci_event_cmd_status_t {
    u8 event_code;
    u8 param_len;
    u8 status;
    u8 num_hci_cmd_pkts;
    u16 opcode;
} POSTPACK HCI_EVENT_CMD_STATUS;

/* Hardware Error event */
typedef struct  hci_event_hw_err_t {
    u8 event_code;
    u8 param_len;
    u8 hw_err_code;
} POSTPACK HCI_EVENT_HW_ERR;

/* Flush occurred event */
/* Qos Violation event */
typedef struct  hci_event_handle_t {
    u8 event_code;
    u8 param_len;
    u16 handle;
} POSTPACK HCI_EVENT_FLUSH_OCCRD,
           HCI_EVENT_QOS_VIOLATION;

/* Loopback command event */
typedef struct hci_loopback_cmd_t {
    u8 event_code;
    u8 param_len;
    u8 params[252];
} POSTPACK HCI_EVENT_LOOPBACK_CMD;

/* Data buffer overflow event */
typedef struct  hci_data_buf_overflow_t {
    u8 event_code;
    u8 param_len;
    u8 link_type;
} POSTPACK  HCI_EVENT_DATA_BUF_OVERFLOW;

/* Enhanced Flush complete event */
typedef struct hci_enhanced_flush_complt_t{
    u8 event_code;
    u8 param_len;
    u16 hdl;
} POSTPACK  HCI_EVENT_ENHANCED_FLUSH_COMPLT;

/* Channel select event */
typedef struct  hci_event_chan_select_t {
    u8 event_code;
    u8 param_len;
    u8 phy_link_hdl;
} POSTPACK HCI_EVENT_CHAN_SELECT;

/* Physical Link Complete event */
typedef struct  hci_event_phy_link_complete_event_t {
    u8 event_code;
    u8 param_len;
    u8 status;
    u8 phy_link_hdl;
} POSTPACK HCI_EVENT_PHY_LINK_COMPLETE;

/* Logical Link complete event */
typedef struct hci_event_logical_link_complete_event_t {
    u8 event_code;
    u8 param_len;
    u8 status;
    u16 logical_link_hdl;
    u8 phy_hdl;
    u8 tx_flow_id;
} POSTPACK HCI_EVENT_LOGICAL_LINK_COMPLETE_EVENT;

/* Disconnect Logical Link complete event */
typedef struct hci_event_disconnect_logical_link_event_t {
    u8 event_code;
    u8 param_len;
    u8 status;
    u16 logical_link_hdl;
    u8 reason;
} POSTPACK HCI_EVENT_DISCONNECT_LOGICAL_LINK_EVENT;

/* Disconnect Physical Link complete event */
typedef struct hci_event_disconnect_phy_link_complete_t {
    u8 event_code;
    u8 param_len;
    u8 status;
    u8 phy_link_hdl;
    u8 reason;
} POSTPACK HCI_EVENT_DISCONNECT_PHY_LINK_COMPLETE;

typedef struct hci_event_physical_link_loss_early_warning_t{
    u8 event_code;
    u8 param_len;
    u8 phy_hdl;
    u8 reason;
} POSTPACK HCI_EVENT_PHY_LINK_LOSS_EARLY_WARNING;

typedef struct hci_event_physical_link_recovery_t{
    u8 event_code;
    u8 param_len;
    u8 phy_hdl;
} POSTPACK HCI_EVENT_PHY_LINK_RECOVERY;


/* Flow spec modify complete event */
/* Flush event */
typedef struct hci_event_status_handle_t {
    u8 event_code;
    u8 param_len;
    u8 status;
    u16 handle;
} POSTPACK HCI_EVENT_FLOW_SPEC_MODIFY,
           HCI_EVENT_FLUSH;


/* Num of completed data blocks event */
typedef struct hci_event_num_of_compl_data_blks_t {
    u8 event_code;
    u8 param_len;
    u16 num_data_blks;
    u8 num_handles;
    u8 params[255];
} POSTPACK HCI_EVENT_NUM_COMPL_DATA_BLKS;

/* Short range mode change complete event */
typedef struct  hci_srm_cmpl_t {
    u8 event_code;
    u8 param_len;
    u8 status;
    u8 phy_link;
    u8 state;
} POSTPACK HCI_EVENT_SRM_COMPL;

typedef struct hci_event_amp_status_change_t{
    u8 event_code;
    u8 param_len;
    u8 status;
    u8 amp_status;
} POSTPACK HCI_EVENT_AMP_STATUS_CHANGE;

/*============== Event definitions end =========================== */


typedef struct  local_amp_info_resp_t {
    u8 status;
    u8 amp_status;
    u32 total_bw;           /* kbps */
    u32 max_guranteed_bw;   /* kbps */
    u32 min_latency;
    u32 max_pdu_size;
    u8 amp_type;
    u16 pal_capabilities;
    u16 amp_assoc_len;
    u32 max_flush_timeout;  /* in ms */
    u32 be_flush_timeout;   /* in ms */
} POSTPACK  LOCAL_AMP_INFO;

typedef struct  amp_assoc_cmd_resp_t{
    u8 status;
    u8 phy_hdl;
    u16 amp_assoc_len;
    u8 amp_assoc_frag[AMP_ASSOC_MAX_FRAG_SZ];
}POSTPACK AMP_ASSOC_CMD_RESP;


enum PAL_HCI_CMD_STATUS {
    PAL_HCI_CMD_PROCESSED,
    PAL_HCI_CMD_IGNORED
}; 


/*============= HCI Error Codes =======================*/
#define HCI_SUCCESS                             0x00
#define HCI_ERR_UNKNOW_CMD                      0x01
#define HCI_ERR_UNKNOWN_CONN_ID                 0x02
#define HCI_ERR_HW_FAILURE                      0x03
#define HCI_ERR_PAGE_TIMEOUT                    0x04
#define HCI_ERR_AUTH_FAILURE                    0x05
#define HCI_ERR_KEY_MISSING                     0x06
#define HCI_ERR_MEM_CAP_EXECED                  0x07
#define HCI_ERR_CON_TIMEOUT                     0x08
#define HCI_ERR_CON_LIMIT_EXECED                0x09
#define	HCI_ERR_ACL_CONN_ALRDY_EXISTS	        0x0B
#define	HCI_ERR_COMMAND_DISALLOWED		        0x0C
#define HCI_ERR_CONN_REJ_BY_LIMIT_RES           0x0D
#define HCI_ERR_CONN_REJ_BY_SEC                 0x0E
#define HCI_ERR_CONN_REJ_BY_BAD_ADDR            0x0F
#define HCI_ERR_CONN_ACCPT_TIMEOUT              0x10
#define HCI_ERR_UNSUPPORT_FEATURE               0x11
#define HCI_ERR_INVALID_HCI_CMD_PARAMS          0x12
#define HCI_ERR_REMOTE_USER_TERMINATE_CONN      0x13
#define HCI_ERR_CON_TERM_BY_HOST                0x16
#define HCI_ERR_UNSPECIFIED_ERROR               0x1F
#define HCI_ERR_ENCRYPTION_MODE_NOT_SUPPORT     0x25
#define HCI_ERR_REQUESTED_QOS_NOT_SUPPORT       0x27
#define HCI_ERR_QOS_UNACCEPTABLE_PARM           0x2C
#define HCI_ERR_QOS_REJECTED                    0x2D
#define HCI_ERR_CONN_REJ_NO_SUITABLE_CHAN       0x39

/*============= HCI Error Codes End =======================*/


/* Following are event return parameters.. part of HCI events 
 */
typedef struct  timeout_read_t {
    u8 status;
    u16 timeout;
}POSTPACK TIMEOUT_INFO;

typedef struct  link_supervision_timeout_read_t {
    u8 status;
    u16 hdl;
    u16 timeout;
}POSTPACK LINK_SUPERVISION_TIMEOUT_INFO;

typedef struct  status_hdl_t {
    u8 status;
    u16 hdl;
}POSTPACK INFO_STATUS_HDL;

typedef struct write_remote_amp_assoc_t{
    u8 status;
    u8 hdl;
}POSTPACK WRITE_REMOTE_AMP_ASSOC_INFO;

typedef struct  read_loc_info_t {
    u8 status;
    LOCATION_DATA_CFG   loc;
}POSTPACK READ_LOC_INFO;

typedef struct  read_flow_ctrl_mode_t {
    u8 status;
    u8 mode;
}POSTPACK READ_FLWCTRL_INFO;

typedef struct  read_data_blk_size_t {
    u8 status;
    u16 max_acl_data_pkt_len;
    u16 data_block_len;
    u16 total_num_data_blks;
}POSTPACK READ_DATA_BLK_SIZE_INFO;

/* Read Link quality info */
typedef struct link_qual_t {
    u8 status;
    u16 hdl;
    u8 link_qual;
} POSTPACK READ_LINK_QUAL_INFO,
            READ_RSSI_INFO;

typedef struct ll_cancel_resp_t {
    u8 status;
    u8 phy_link_hdl;
    u8 tx_flow_spec_id;
} POSTPACK LL_CANCEL_RESP;

typedef struct read_local_ver_info_t {
    u8 status;
    u8 hci_version;
    u16 hci_revision;
    u8 pal_version;
    u16 manf_name;
    u16 pal_sub_ver;
} POSTPACK READ_LOCAL_VER_INFO;


#endif  /* __A_HCI_H__ */
