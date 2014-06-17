
/*
 *
 Copyright (c) Eicon Networks, 2002.
 *
 This source file is supplied for the use with
 Eicon Networks range of DIVA Server Adapters.
 *
 Eicon File Revision :    2.1
 *
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.
 *
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY
 implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 See the GNU General Public License for more details.
 *
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/*#define DEBUG */











#define IMPLEMENT_DTMF 1
#define IMPLEMENT_LINE_INTERCONNECT2 1
#define IMPLEMENT_ECHO_CANCELLER 1
#define IMPLEMENT_RTP 1
#define IMPLEMENT_T38 1
#define IMPLEMENT_FAX_SUB_SEP_PWD 1
#define IMPLEMENT_V18 1
#define IMPLEMENT_DTMF_TONE 1
#define IMPLEMENT_PIAFS 1
#define IMPLEMENT_FAX_PAPER_FORMATS 1
#define IMPLEMENT_VOWN 1
#define IMPLEMENT_CAPIDTMF 1
#define IMPLEMENT_FAX_NONSTANDARD 1
#define VSWITCH_SUPPORT 1


#define IMPLEMENT_LINE_INTERCONNECT 0
#define IMPLEMENT_MARKED_OK_AFTER_FC 1

#include "capidtmf.h"

/*------------------------------------------------------------------*/
/* Common API internal definitions                                  */
/*------------------------------------------------------------------*/

#define MAX_APPL 240
#define MAX_NCCI           127

#define MSG_IN_QUEUE_SIZE  ((4096 + 3) & 0xfffc)  /* must be multiple of 4 */


#define MSG_IN_OVERHEAD    sizeof(APPL   *)

#define MAX_NL_CHANNEL     255
#define MAX_DATA_B3        8
#define MAX_DATA_ACK       MAX_DATA_B3
#define MAX_MULTI_IE       6
#define MAX_MSG_SIZE       256
#define MAX_MSG_PARMS      10
#define MAX_CPN_MASK_SIZE  16
#define MAX_MSN_CONFIG     10
#define EXT_CONTROLLER     0x80
#define CODEC              0x01
#define CODEC_PERMANENT    0x02
#define ADV_VOICE          0x03
#define MAX_CIP_TYPES      5  /* kind of CIP types for group optimization */
#define C_IND_MASK_DWORDS  ((MAX_APPL + 32) >> 5)


#define FAX_CONNECT_INFO_BUFFER_SIZE  256
#define NCPI_BUFFER_SIZE              256

#define MAX_CHANNELS_PER_PLCI         8
#define MAX_INTERNAL_COMMAND_LEVELS   4
#define INTERNAL_REQ_BUFFER_SIZE      272

#define INTERNAL_IND_BUFFER_SIZE      768

#define DTMF_PARAMETER_BUFFER_SIZE    12
#define ADV_VOICE_COEF_BUFFER_SIZE    50

#define LI_PLCI_B_QUEUE_ENTRIES       256



typedef struct _APPL APPL;
typedef struct _PLCI PLCI;
typedef struct _NCCI NCCI;
typedef struct _DIVA_CAPI_ADAPTER DIVA_CAPI_ADAPTER;
typedef struct _DATA_B3_DESC DATA_B3_DESC;
typedef struct _DATA_ACK_DESC DATA_ACK_DESC;
typedef struct manufacturer_profile_s MANUFACTURER_PROFILE;
typedef struct fax_ncpi_s FAX_NCPI;
typedef struct api_parse_s API_PARSE;
typedef struct api_save_s API_SAVE;
typedef struct msn_config_s MSN_CONFIG;
typedef struct msn_config_max_s MSN_CONFIG_MAX;
typedef struct msn_ld_s MSN_LD;

struct manufacturer_profile_s {
	dword private_options;
	dword rtp_primary_payloads;
	dword rtp_additional_payloads;
};

struct fax_ncpi_s {
	word options;
	word format;
};

struct msn_config_s {
	byte msn[MAX_CPN_MASK_SIZE];
};

struct msn_config_max_s {
	MSN_CONFIG    msn_conf[MAX_MSN_CONFIG];
};

struct msn_ld_s {
	dword low;
	dword high;
};

struct api_parse_s {
	word          length;
	byte *info;
};

struct api_save_s {
	API_PARSE     parms[MAX_MSG_PARMS + 1];
	byte          info[MAX_MSG_SIZE];
};

struct _DATA_B3_DESC {
	word          Handle;
	word          Number;
	word          Flags;
	word          Length;
	void *P;
};

struct _DATA_ACK_DESC {
	word          Handle;
	word          Number;
};

typedef void (*t_std_internal_command)(dword Id, PLCI *plci, byte Rc);

/************************************************************************/
/* Don't forget to adapt dos.asm after changing the _APPL structure!!!! */
struct _APPL {
	word          Id;
	word          NullCREnable;
	word          CDEnable;
	dword         S_Handle;






	LIST_ENTRY    s_function;
	dword         s_context;
	word          s_count;
	APPL *s_next;
	byte *xbuffer_used;
	void **xbuffer_internal;
	void **xbuffer_ptr;






	byte *queue;
	word          queue_size;
	word          queue_free;
	word          queue_read;
	word          queue_write;
	word          queue_signal;
	byte          msg_lost;
	byte          appl_flags;
	word          Number;

	word          MaxBuffer;
	byte          MaxNCCI;
	byte          MaxNCCIData;
	word          MaxDataLength;
	word          NCCIDataFlowCtrlTimer;
	byte *ReceiveBuffer;
	word *DataNCCI;
	word *DataFlags;
};


struct _PLCI {
	ENTITY        Sig;
	ENTITY        NL;
	word          RNum;
	word          RFlags;
	BUFFERS       RData[2];
	BUFFERS       XData[1];
	BUFFERS       NData[2];

	DIVA_CAPI_ADAPTER   *adapter;
	APPL      *appl;
	PLCI      *relatedPTYPLCI;
	byte          Id;
	byte          State;
	byte          sig_req;
	byte          nl_req;
	byte          SuppState;
	byte          channels;
	byte          tel;
	byte          B1_resource;
	byte          B2_prot;
	byte          B3_prot;

	word          command;
	word          m_command;
	word          internal_command;
	word          number;
	word          req_in_start;
	word          req_in;
	word          req_out;
	word          msg_in_write_pos;
	word          msg_in_read_pos;
	word          msg_in_wrap_pos;

	void *data_sent_ptr;
	byte          data_sent;
	byte          send_disc;
	byte          sig_global_req;
	byte          sig_remove_id;
	byte          nl_global_req;
	byte          nl_remove_id;
	byte          b_channel;
	byte          adv_nl;
	byte          manufacturer;
	byte          call_dir;
	byte          hook_state;
	byte          spoofed_msg;
	byte          ptyState;
	byte          cr_enquiry;
	word          hangup_flow_ctrl_timer;

	word          ncci_ring_list;
	byte          inc_dis_ncci_table[MAX_CHANNELS_PER_PLCI];
	t_std_internal_command internal_command_queue[MAX_INTERNAL_COMMAND_LEVELS];
	dword         c_ind_mask_table[C_IND_MASK_DWORDS];
	dword         group_optimization_mask_table[C_IND_MASK_DWORDS];
	byte          RBuffer[200];
	dword         msg_in_queue[MSG_IN_QUEUE_SIZE/sizeof(dword)];
	API_SAVE      saved_msg;
	API_SAVE      B_protocol;
	byte          fax_connect_info_length;
	byte          fax_connect_info_buffer[FAX_CONNECT_INFO_BUFFER_SIZE];
	byte          fax_edata_ack_length;
	word          nsf_control_bits;
	byte          ncpi_state;
	byte          ncpi_buffer[NCPI_BUFFER_SIZE];

	byte          internal_req_buffer[INTERNAL_REQ_BUFFER_SIZE];
	byte          internal_ind_buffer[INTERNAL_IND_BUFFER_SIZE + 3];
	dword         requested_options_conn;
	dword         requested_options;
	word          B1_facilities;
	API_SAVE   *adjust_b_parms_msg;
	word          adjust_b_facilities;
	word          adjust_b_command;
	word          adjust_b_ncci;
	word          adjust_b_mode;
	word          adjust_b_state;
	byte          adjust_b_restore;

	byte          dtmf_rec_active;
	word          dtmf_rec_pulse_ms;
	word          dtmf_rec_pause_ms;
	byte          dtmf_send_requests;
	word          dtmf_send_pulse_ms;
	word          dtmf_send_pause_ms;
	word          dtmf_cmd;
	word          dtmf_msg_number_queue[8];
	byte          dtmf_parameter_length;
	byte          dtmf_parameter_buffer[DTMF_PARAMETER_BUFFER_SIZE];


	t_capidtmf_state capidtmf_state;


	byte          li_bchannel_id;    /* BRI: 1..2, PRI: 1..32 */
	byte          li_channel_bits;
	byte          li_notify_update;
	word          li_cmd;
	word          li_write_command;
	word          li_write_channel;
	word          li_plci_b_write_pos;
	word          li_plci_b_read_pos;
	word          li_plci_b_req_pos;
	dword         li_plci_b_queue[LI_PLCI_B_QUEUE_ENTRIES];


	word          ec_cmd;
	word          ec_idi_options;
	word          ec_tail_length;


	byte          tone_last_indication_code;

	byte          vswitchstate;
	byte          vsprot;
	byte          vsprotdialect;
	byte          notifiedcall; /* Flag if it is a spoofed call */

	int           rx_dma_descriptor;
	dword         rx_dma_magic;
};


struct _NCCI {
	byte          data_out;
	byte          data_pending;
	byte          data_ack_out;
	byte          data_ack_pending;
	DATA_B3_DESC  DBuffer[MAX_DATA_B3];
	DATA_ACK_DESC DataAck[MAX_DATA_ACK];
};


struct _DIVA_CAPI_ADAPTER {
	IDI_CALL      request;
	byte          Id;
	byte          max_plci;
	byte          max_listen;
	byte          listen_active;
	PLCI      *plci;
	byte          ch_ncci[MAX_NL_CHANNEL + 1];
	byte          ncci_ch[MAX_NCCI + 1];
	byte          ncci_plci[MAX_NCCI + 1];
	byte          ncci_state[MAX_NCCI + 1];
	byte          ncci_next[MAX_NCCI + 1];
	NCCI          ncci[MAX_NCCI + 1];

	byte          ch_flow_control[MAX_NL_CHANNEL + 1];  /* Used by XON protocol */
	byte          ch_flow_control_pending;
	byte          ch_flow_plci[MAX_NL_CHANNEL + 1];
	int           last_flow_control_ch;

	dword         Info_Mask[MAX_APPL];
	dword         CIP_Mask[MAX_APPL];

	dword         Notification_Mask[MAX_APPL];
	PLCI      *codec_listen[MAX_APPL];
	dword         requested_options_table[MAX_APPL];
	API_PROFILE   profile;
	MANUFACTURER_PROFILE man_profile;
	dword         manufacturer_features;

	byte          AdvCodecFLAG;
	PLCI      *AdvCodecPLCI;
	PLCI      *AdvSignalPLCI;
	APPL      *AdvSignalAppl;
	byte          TelOAD[23];
	byte          TelOSA[23];
	byte          scom_appl_disable;
	PLCI      *automatic_lawPLCI;
	byte          automatic_law;
	byte          u_law;

	byte          adv_voice_coef_length;
	byte          adv_voice_coef_buffer[ADV_VOICE_COEF_BUFFER_SIZE];

	byte          li_pri;
	byte          li_channels;
	word          li_base;

	byte adapter_disabled;
	byte group_optimization_enabled; /* use application groups if enabled */
	dword sdram_bar;
	byte flag_dynamic_l1_down; /* for hunt groups:down layer 1 if no appl present*/
	byte FlowControlIdTable[256];
	byte FlowControlSkipTable[256];
	void *os_card; /* pointer to associated OS dependent adapter structure */
};


/*------------------------------------------------------------------*/
/* Application flags                                                */
/*------------------------------------------------------------------*/

#define APPL_FLAG_OLD_LI_SPEC           0x01
#define APPL_FLAG_PRIV_EC_SPEC          0x02


/*------------------------------------------------------------------*/
/* API parameter definitions                                        */
/*------------------------------------------------------------------*/

#define X75_TTX         1       /* x.75 for ttx                     */
#define TRF             2       /* transparent with hdlc framing    */
#define TRF_IN          3       /* transparent with hdlc fr. inc.   */
#define SDLC            4       /* sdlc, sna layer-2                */
#define X75_BTX         5       /* x.75 for btx                     */
#define LAPD            6       /* lapd (Q.921)                     */
#define X25_L2          7       /* x.25 layer-2                     */
#define V120_L2         8       /* V.120 layer-2 protocol           */
#define V42_IN          9       /* V.42 layer-2 protocol, incoming */
#define V42            10       /* V.42 layer-2 protocol            */
#define MDM_ATP        11       /* AT Parser built in the L2        */
#define X75_V42BIS     12       /* ISO7776 (X.75 SLP) modified to support V.42 bis compression */
#define RTPL2_IN       13       /* RTP layer-2 protocol, incoming  */
#define RTPL2          14       /* RTP layer-2 protocol             */
#define V120_V42BIS    15       /* V.120 layer-2 protocol supporting V.42 bis compression */

#define T70NL           1
#define X25PLP          2
#define T70NLX          3
#define TRANSPARENT_NL  4
#define ISO8208         5
#define T30             6


/*------------------------------------------------------------------*/
/* FAX interface to IDI                                             */
/*------------------------------------------------------------------*/

#define CAPI_MAX_HEAD_LINE_SPACE        89
#define CAPI_MAX_DATE_TIME_LENGTH       18

#define T30_MAX_STATION_ID_LENGTH       20
#define T30_MAX_SUBADDRESS_LENGTH       20
#define T30_MAX_PASSWORD_LENGTH         20

typedef struct t30_info_s T30_INFO;
struct t30_info_s {
	byte          code;
	byte          rate_div_2400;
	byte          resolution;
	byte          data_format;
	byte          pages_low;
	byte          pages_high;
	byte          operating_mode;
	byte          control_bits_low;
	byte          control_bits_high;
	byte          feature_bits_low;
	byte          feature_bits_high;
	byte          recording_properties;
	byte          universal_6;
	byte          universal_7;
	byte          station_id_len;
	byte          head_line_len;
	byte          station_id[T30_MAX_STATION_ID_LENGTH];
/* byte          head_line[];      */
/* byte          sub_sep_length;   */
/* byte          sub_sep_field[];  */
/* byte          pwd_length;       */
/* byte          pwd_field[];      */
/* byte          nsf_info_length;   */
/* byte          nsf_info_field[];  */
};


#define T30_RESOLUTION_R8_0385          0x00
#define T30_RESOLUTION_R8_0770_OR_200   0x01
#define T30_RESOLUTION_R8_1540          0x02
#define T30_RESOLUTION_R16_1540_OR_400  0x04
#define T30_RESOLUTION_R4_0385_OR_100   0x08
#define T30_RESOLUTION_300_300          0x10
#define T30_RESOLUTION_INCH_BASED       0x40
#define T30_RESOLUTION_METRIC_BASED     0x80

#define T30_RECORDING_WIDTH_ISO_A4      0
#define T30_RECORDING_WIDTH_ISO_B4      1
#define T30_RECORDING_WIDTH_ISO_A3      2
#define T30_RECORDING_WIDTH_COUNT       3

#define T30_RECORDING_LENGTH_ISO_A4     0
#define T30_RECORDING_LENGTH_ISO_B4     1
#define T30_RECORDING_LENGTH_UNLIMITED  2
#define T30_RECORDING_LENGTH_COUNT      3

#define T30_MIN_SCANLINE_TIME_00_00_00  0
#define T30_MIN_SCANLINE_TIME_05_05_05  1
#define T30_MIN_SCANLINE_TIME_10_05_05  2
#define T30_MIN_SCANLINE_TIME_10_10_10  3
#define T30_MIN_SCANLINE_TIME_20_10_10  4
#define T30_MIN_SCANLINE_TIME_20_20_20  5
#define T30_MIN_SCANLINE_TIME_40_20_20  6
#define T30_MIN_SCANLINE_TIME_40_40_40  7
#define T30_MIN_SCANLINE_TIME_RES_8     8
#define T30_MIN_SCANLINE_TIME_RES_9     9
#define T30_MIN_SCANLINE_TIME_RES_10    10
#define T30_MIN_SCANLINE_TIME_10_10_05  11
#define T30_MIN_SCANLINE_TIME_20_10_05  12
#define T30_MIN_SCANLINE_TIME_20_20_10  13
#define T30_MIN_SCANLINE_TIME_40_20_10  14
#define T30_MIN_SCANLINE_TIME_40_40_20  15
#define T30_MIN_SCANLINE_TIME_COUNT     16

#define T30_DATA_FORMAT_SFF             0
#define T30_DATA_FORMAT_ASCII           1
#define T30_DATA_FORMAT_NATIVE          2
#define T30_DATA_FORMAT_COUNT           3


#define T30_OPERATING_MODE_STANDARD     0
#define T30_OPERATING_MODE_CLASS2       1
#define T30_OPERATING_MODE_CLASS1       2
#define T30_OPERATING_MODE_CAPI         3
#define T30_OPERATING_MODE_CAPI_NEG     4
#define T30_OPERATING_MODE_COUNT        5

/* EDATA transmit messages */
#define EDATA_T30_DIS         0x01
#define EDATA_T30_FTT         0x02
#define EDATA_T30_MCF         0x03
#define EDATA_T30_PARAMETERS  0x04

/* EDATA receive messages */
#define EDATA_T30_DCS         0x81
#define EDATA_T30_TRAIN_OK    0x82
#define EDATA_T30_EOP         0x83
#define EDATA_T30_MPS         0x84
#define EDATA_T30_EOM         0x85
#define EDATA_T30_DTC         0x86
#define EDATA_T30_PAGE_END    0x87   /* Indicates end of page data. Reserved, but not implemented ! */
#define EDATA_T30_EOP_CAPI    0x88


#define T30_SUCCESS                        0
#define T30_ERR_NO_DIS_RECEIVED            1
#define T30_ERR_TIMEOUT_NO_RESPONSE        2
#define T30_ERR_RETRY_NO_RESPONSE          3
#define T30_ERR_TOO_MANY_REPEATS           4
#define T30_ERR_UNEXPECTED_MESSAGE         5
#define T30_ERR_UNEXPECTED_DCN             6
#define T30_ERR_DTC_UNSUPPORTED            7
#define T30_ERR_ALL_RATES_FAILED           8
#define T30_ERR_TOO_MANY_TRAINS            9
#define T30_ERR_RECEIVE_CORRUPTED          10
#define T30_ERR_UNEXPECTED_DISC            11
#define T30_ERR_APPLICATION_DISC           12
#define T30_ERR_INCOMPATIBLE_DIS           13
#define T30_ERR_INCOMPATIBLE_DCS           14
#define T30_ERR_TIMEOUT_NO_COMMAND         15
#define T30_ERR_RETRY_NO_COMMAND           16
#define T30_ERR_TIMEOUT_COMMAND_TOO_LONG   17
#define T30_ERR_TIMEOUT_RESPONSE_TOO_LONG  18
#define T30_ERR_NOT_IDENTIFIED             19
#define T30_ERR_SUPERVISORY_TIMEOUT        20
#define T30_ERR_TOO_LONG_SCAN_LINE         21
/* #define T30_ERR_RETRY_NO_PAGE_AFTER_MPS    22 */
#define T30_ERR_RETRY_NO_PAGE_RECEIVED     23
#define T30_ERR_RETRY_NO_DCS_AFTER_FTT     24
#define T30_ERR_RETRY_NO_DCS_AFTER_EOM     25
#define T30_ERR_RETRY_NO_DCS_AFTER_MPS     26
#define T30_ERR_RETRY_NO_DCN_AFTER_MCF     27
#define T30_ERR_RETRY_NO_DCN_AFTER_RTN     28
#define T30_ERR_RETRY_NO_CFR               29
#define T30_ERR_RETRY_NO_MCF_AFTER_EOP     30
#define T30_ERR_RETRY_NO_MCF_AFTER_EOM     31
#define T30_ERR_RETRY_NO_MCF_AFTER_MPS     32
#define T30_ERR_SUB_SEP_UNSUPPORTED        33
#define T30_ERR_PWD_UNSUPPORTED            34
#define T30_ERR_SUB_SEP_PWD_UNSUPPORTED    35
#define T30_ERR_INVALID_COMMAND_FRAME      36
#define T30_ERR_UNSUPPORTED_PAGE_CODING    37
#define T30_ERR_INVALID_PAGE_CODING        38
#define T30_ERR_INCOMPATIBLE_PAGE_CONFIG   39
#define T30_ERR_TIMEOUT_FROM_APPLICATION   40
#define T30_ERR_V34FAX_NO_REACTION_ON_MARK 41
#define T30_ERR_V34FAX_TRAINING_TIMEOUT    42
#define T30_ERR_V34FAX_UNEXPECTED_V21      43
#define T30_ERR_V34FAX_PRIMARY_CTS_ON      44
#define T30_ERR_V34FAX_TURNAROUND_POLLING  45
#define T30_ERR_V34FAX_V8_INCOMPATIBILITY  46


#define T30_CONTROL_BIT_DISABLE_FINE       0x0001
#define T30_CONTROL_BIT_ENABLE_ECM         0x0002
#define T30_CONTROL_BIT_ECM_64_BYTES       0x0004
#define T30_CONTROL_BIT_ENABLE_2D_CODING   0x0008
#define T30_CONTROL_BIT_ENABLE_T6_CODING   0x0010
#define T30_CONTROL_BIT_ENABLE_UNCOMPR     0x0020
#define T30_CONTROL_BIT_ACCEPT_POLLING     0x0040
#define T30_CONTROL_BIT_REQUEST_POLLING    0x0080
#define T30_CONTROL_BIT_MORE_DOCUMENTS     0x0100
#define T30_CONTROL_BIT_ACCEPT_SUBADDRESS  0x0200
#define T30_CONTROL_BIT_ACCEPT_SEL_POLLING 0x0400
#define T30_CONTROL_BIT_ACCEPT_PASSWORD    0x0800
#define T30_CONTROL_BIT_ENABLE_V34FAX      0x1000
#define T30_CONTROL_BIT_EARLY_CONNECT      0x2000

#define T30_CONTROL_BIT_ALL_FEATURES  (T30_CONTROL_BIT_ENABLE_ECM | T30_CONTROL_BIT_ENABLE_2D_CODING |   T30_CONTROL_BIT_ENABLE_T6_CODING | T30_CONTROL_BIT_ENABLE_UNCOMPR |   T30_CONTROL_BIT_ENABLE_V34FAX)

#define T30_FEATURE_BIT_FINE               0x0001
#define T30_FEATURE_BIT_ECM                0x0002
#define T30_FEATURE_BIT_ECM_64_BYTES       0x0004
#define T30_FEATURE_BIT_2D_CODING          0x0008
#define T30_FEATURE_BIT_T6_CODING          0x0010
#define T30_FEATURE_BIT_UNCOMPR_ENABLED    0x0020
#define T30_FEATURE_BIT_POLLING            0x0040
#define T30_FEATURE_BIT_MORE_DOCUMENTS     0x0100
#define T30_FEATURE_BIT_V34FAX             0x1000


#define T30_NSF_CONTROL_BIT_ENABLE_NSF     0x0001
#define T30_NSF_CONTROL_BIT_RAW_INFO       0x0002
#define T30_NSF_CONTROL_BIT_NEGOTIATE_IND  0x0004
#define T30_NSF_CONTROL_BIT_NEGOTIATE_RESP 0x0008

#define T30_NSF_ELEMENT_NSF_FIF            0x00
#define T30_NSF_ELEMENT_NSC_FIF            0x01
#define T30_NSF_ELEMENT_NSS_FIF            0x02
#define T30_NSF_ELEMENT_COMPANY_NAME       0x03


/*------------------------------------------------------------------*/
/* Analog modem definitions                                         */
/*------------------------------------------------------------------*/

typedef struct async_s ASYNC_FORMAT;
struct async_s {
	unsigned pe:1;
	unsigned parity:2;
	unsigned spare:2;
	unsigned stp:1;
	unsigned ch_len:2;   /* 3th octett in CAI */
};


/*------------------------------------------------------------------*/
/* PLCI/NCCI states                                                 */
/*------------------------------------------------------------------*/

#define IDLE                    0
#define OUTG_CON_PENDING        1
#define INC_CON_PENDING         2
#define INC_CON_ALERT           3
#define INC_CON_ACCEPT          4
#define INC_ACT_PENDING         5
#define LISTENING               6
#define CONNECTED               7
#define OUTG_DIS_PENDING        8
#define INC_DIS_PENDING         9
#define LOCAL_CONNECT           10
#define INC_RES_PENDING         11
#define OUTG_RES_PENDING        12
#define SUSPENDING              13
#define ADVANCED_VOICE_SIG      14
#define ADVANCED_VOICE_NOSIG    15
#define RESUMING                16
#define INC_CON_CONNECTED_ALERT 17
#define OUTG_REJ_PENDING        18


/*------------------------------------------------------------------*/
/* auxiliary states for supplementary services                     */
/*------------------------------------------------------------------*/

#define IDLE                0
#define HOLD_REQUEST        1
#define HOLD_INDICATE       2
#define CALL_HELD           3
#define RETRIEVE_REQUEST    4
#define RETRIEVE_INDICATION 5

/*------------------------------------------------------------------*/
/* Capi IE + Msg types                                              */
/*------------------------------------------------------------------*/
#define ESC_CAUSE        0x800 | CAU        /* Escape cause element */
#define ESC_MSGTYPE      0x800 | MSGTYPEIE  /* Escape message type  */
#define ESC_CHI          0x800 | CHI        /* Escape channel id    */
#define ESC_LAW          0x800 | BC         /* Escape law info      */
#define ESC_CR           0x800 | CRIE       /* Escape CallReference */
#define ESC_PROFILE      0x800 | PROFILEIE  /* Escape profile       */
#define ESC_SSEXT        0x800 | SSEXTIE    /* Escape Supplem. Serv.*/
#define ESC_VSWITCH      0x800 | VSWITCHIE  /* Escape VSwitch       */
#define CST              0x14               /* Call State i.e.      */
#define PI               0x1E               /* Progress Indicator   */
#define NI               0x27               /* Notification Ind     */
#define CONN_NR          0x4C               /* Connected Number     */
#define CONG_RNR         0xBF               /* Congestion RNR       */
#define CONG_RR          0xB0               /* Congestion RR        */
#define RESERVED         0xFF               /* Res. for future use  */
#define ON_BOARD_CODEC   0x02               /* external controller  */
#define HANDSET          0x04               /* Codec+Handset(Pro11) */
#define HOOK_SUPPORT     0x01               /* activate Hook signal */
#define SCR              0x7a               /* unscreened number    */

#define HOOK_OFF_REQ     0x9001             /* internal conn req    */
#define HOOK_ON_REQ      0x9002             /* internal disc req    */
#define SUSPEND_REQ      0x9003             /* internal susp req    */
#define RESUME_REQ       0x9004             /* internal resume req  */
#define USELAW_REQ       0x9005             /* internal law    req  */
#define LISTEN_SIG_ASSIGN_PEND  0x9006
#define PERM_LIST_REQ    0x900a             /* permanent conn DCE   */
#define C_HOLD_REQ       0x9011
#define C_RETRIEVE_REQ   0x9012
#define C_NCR_FAC_REQ    0x9013
#define PERM_COD_ASSIGN  0x9014
#define PERM_COD_CALL    0x9015
#define PERM_COD_HOOK    0x9016
#define PERM_COD_CONN_PEND 0x9017           /* wait for connect_con */
#define PTY_REQ_PEND     0x9018
#define CD_REQ_PEND      0x9019
#define CF_START_PEND    0x901a
#define CF_STOP_PEND     0x901b
#define ECT_REQ_PEND     0x901c
#define GETSERV_REQ_PEND 0x901d
#define BLOCK_PLCI       0x901e
#define INTERR_NUMBERS_REQ_PEND         0x901f
#define INTERR_DIVERSION_REQ_PEND       0x9020
#define MWI_ACTIVATE_REQ_PEND           0x9021
#define MWI_DEACTIVATE_REQ_PEND         0x9022
#define SSEXT_REQ_COMMAND               0x9023
#define SSEXT_NC_REQ_COMMAND            0x9024
#define START_L1_SIG_ASSIGN_PEND        0x9025
#define REM_L1_SIG_ASSIGN_PEND          0x9026
#define CONF_BEGIN_REQ_PEND             0x9027
#define CONF_ADD_REQ_PEND               0x9028
#define CONF_SPLIT_REQ_PEND             0x9029
#define CONF_DROP_REQ_PEND              0x902a
#define CONF_ISOLATE_REQ_PEND           0x902b
#define CONF_REATTACH_REQ_PEND          0x902c
#define VSWITCH_REQ_PEND                0x902d
#define GET_MWI_STATE                   0x902e
#define CCBS_REQUEST_REQ_PEND           0x902f
#define CCBS_DEACTIVATE_REQ_PEND        0x9030
#define CCBS_INTERROGATE_REQ_PEND       0x9031

#define NO_INTERNAL_COMMAND             0
#define DTMF_COMMAND_1                  1
#define DTMF_COMMAND_2                  2
#define DTMF_COMMAND_3                  3
#define MIXER_COMMAND_1                 4
#define MIXER_COMMAND_2                 5
#define MIXER_COMMAND_3                 6
#define ADV_VOICE_COMMAND_CONNECT_1     7
#define ADV_VOICE_COMMAND_CONNECT_2     8
#define ADV_VOICE_COMMAND_CONNECT_3     9
#define ADV_VOICE_COMMAND_DISCONNECT_1  10
#define ADV_VOICE_COMMAND_DISCONNECT_2  11
#define ADV_VOICE_COMMAND_DISCONNECT_3  12
#define ADJUST_B_RESTORE_1              13
#define ADJUST_B_RESTORE_2              14
#define RESET_B3_COMMAND_1              15
#define SELECT_B_COMMAND_1              16
#define FAX_CONNECT_INFO_COMMAND_1      17
#define FAX_CONNECT_INFO_COMMAND_2      18
#define FAX_ADJUST_B23_COMMAND_1        19
#define FAX_ADJUST_B23_COMMAND_2        20
#define EC_COMMAND_1                    21
#define EC_COMMAND_2                    22
#define EC_COMMAND_3                    23
#define RTP_CONNECT_B3_REQ_COMMAND_1    24
#define RTP_CONNECT_B3_REQ_COMMAND_2    25
#define RTP_CONNECT_B3_REQ_COMMAND_3    26
#define RTP_CONNECT_B3_RES_COMMAND_1    27
#define RTP_CONNECT_B3_RES_COMMAND_2    28
#define RTP_CONNECT_B3_RES_COMMAND_3    29
#define HOLD_SAVE_COMMAND_1             30
#define RETRIEVE_RESTORE_COMMAND_1      31
#define FAX_DISCONNECT_COMMAND_1        32
#define FAX_DISCONNECT_COMMAND_2        33
#define FAX_DISCONNECT_COMMAND_3        34
#define FAX_EDATA_ACK_COMMAND_1         35
#define FAX_EDATA_ACK_COMMAND_2         36
#define FAX_CONNECT_ACK_COMMAND_1       37
#define FAX_CONNECT_ACK_COMMAND_2       38
#define STD_INTERNAL_COMMAND_COUNT      39

#define UID              0x2d               /* User Id for Mgmt      */

#define CALL_DIR_OUT             0x01       /* call direction of initial call */
#define CALL_DIR_IN              0x02
#define CALL_DIR_ORIGINATE       0x04       /* DTE/DCE direction according to */
#define CALL_DIR_ANSWER          0x08       /*   state of B-Channel Operation */
#define CALL_DIR_FORCE_OUTG_NL   0x10       /* for RESET_B3 reconnect, after DISC_B3... */

#define AWAITING_MANUF_CON 0x80             /* command spoofing flags */
#define SPOOFING_REQUIRED  0xff
#define AWAITING_SELECT_B  0xef

/*------------------------------------------------------------------*/
/* B_CTRL / DSP_CTRL                                                */
/*------------------------------------------------------------------*/

#define DSP_CTRL_OLD_SET_MIXER_COEFFICIENTS     0x01
#define DSP_CTRL_SET_BCHANNEL_PASSIVATION_BRI   0x02
#define DSP_CTRL_SET_DTMF_PARAMETERS            0x03

#define MANUFACTURER_FEATURE_SLAVE_CODEC          0x00000001L
#define MANUFACTURER_FEATURE_FAX_MORE_DOCUMENTS   0x00000002L
#define MANUFACTURER_FEATURE_HARDDTMF             0x00000004L
#define MANUFACTURER_FEATURE_SOFTDTMF_SEND        0x00000008L
#define MANUFACTURER_FEATURE_DTMF_PARAMETERS      0x00000010L
#define MANUFACTURER_FEATURE_SOFTDTMF_RECEIVE     0x00000020L
#define MANUFACTURER_FEATURE_FAX_SUB_SEP_PWD      0x00000040L
#define MANUFACTURER_FEATURE_V18                  0x00000080L
#define MANUFACTURER_FEATURE_MIXER_CH_CH          0x00000100L
#define MANUFACTURER_FEATURE_MIXER_CH_PC          0x00000200L
#define MANUFACTURER_FEATURE_MIXER_PC_CH          0x00000400L
#define MANUFACTURER_FEATURE_MIXER_PC_PC          0x00000800L
#define MANUFACTURER_FEATURE_ECHO_CANCELLER       0x00001000L
#define MANUFACTURER_FEATURE_RTP                  0x00002000L
#define MANUFACTURER_FEATURE_T38                  0x00004000L
#define MANUFACTURER_FEATURE_TRANSP_DELIVERY_CONF 0x00008000L
#define MANUFACTURER_FEATURE_XONOFF_FLOW_CONTROL  0x00010000L
#define MANUFACTURER_FEATURE_OOB_CHANNEL          0x00020000L
#define MANUFACTURER_FEATURE_IN_BAND_CHANNEL      0x00040000L
#define MANUFACTURER_FEATURE_IN_BAND_FEATURE      0x00080000L
#define MANUFACTURER_FEATURE_PIAFS                0x00100000L
#define MANUFACTURER_FEATURE_DTMF_TONE            0x00200000L
#define MANUFACTURER_FEATURE_FAX_PAPER_FORMATS    0x00400000L
#define MANUFACTURER_FEATURE_OK_FC_LABEL          0x00800000L
#define MANUFACTURER_FEATURE_VOWN                 0x01000000L
#define MANUFACTURER_FEATURE_XCONNECT             0x02000000L
#define MANUFACTURER_FEATURE_DMACONNECT           0x04000000L
#define MANUFACTURER_FEATURE_AUDIO_TAP            0x08000000L
#define MANUFACTURER_FEATURE_FAX_NONSTANDARD      0x10000000L

/*------------------------------------------------------------------*/
/* DTMF interface to IDI                                            */
/*------------------------------------------------------------------*/


#define DTMF_DIGIT_TONE_LOW_GROUP_697_HZ        0x00
#define DTMF_DIGIT_TONE_LOW_GROUP_770_HZ        0x01
#define DTMF_DIGIT_TONE_LOW_GROUP_852_HZ        0x02
#define DTMF_DIGIT_TONE_LOW_GROUP_941_HZ        0x03
#define DTMF_DIGIT_TONE_LOW_GROUP_MASK          0x03
#define DTMF_DIGIT_TONE_HIGH_GROUP_1209_HZ      0x00
#define DTMF_DIGIT_TONE_HIGH_GROUP_1336_HZ      0x04
#define DTMF_DIGIT_TONE_HIGH_GROUP_1477_HZ      0x08
#define DTMF_DIGIT_TONE_HIGH_GROUP_1633_HZ      0x0c
#define DTMF_DIGIT_TONE_HIGH_GROUP_MASK         0x0c
#define DTMF_DIGIT_TONE_CODE_0                  0x07
#define DTMF_DIGIT_TONE_CODE_1                  0x00
#define DTMF_DIGIT_TONE_CODE_2                  0x04
#define DTMF_DIGIT_TONE_CODE_3                  0x08
#define DTMF_DIGIT_TONE_CODE_4                  0x01
#define DTMF_DIGIT_TONE_CODE_5                  0x05
#define DTMF_DIGIT_TONE_CODE_6                  0x09
#define DTMF_DIGIT_TONE_CODE_7                  0x02
#define DTMF_DIGIT_TONE_CODE_8                  0x06
#define DTMF_DIGIT_TONE_CODE_9                  0x0a
#define DTMF_DIGIT_TONE_CODE_STAR               0x03
#define DTMF_DIGIT_TONE_CODE_HASHMARK           0x0b
#define DTMF_DIGIT_TONE_CODE_A                  0x0c
#define DTMF_DIGIT_TONE_CODE_B                  0x0d
#define DTMF_DIGIT_TONE_CODE_C                  0x0e
#define DTMF_DIGIT_TONE_CODE_D                  0x0f

#define DTMF_UDATA_REQUEST_SEND_DIGITS            16
#define DTMF_UDATA_REQUEST_ENABLE_RECEIVER        17
#define DTMF_UDATA_REQUEST_DISABLE_RECEIVER       18
#define DTMF_UDATA_INDICATION_DIGITS_SENT         16
#define DTMF_UDATA_INDICATION_DIGITS_RECEIVED     17
#define DTMF_UDATA_INDICATION_MODEM_CALLING_TONE  18
#define DTMF_UDATA_INDICATION_FAX_CALLING_TONE    19
#define DTMF_UDATA_INDICATION_ANSWER_TONE         20

#define UDATA_REQUEST_MIXER_TAP_DATA        27
#define UDATA_INDICATION_MIXER_TAP_DATA     27

#define DTMF_LISTEN_ACTIVE_FLAG        0x01
#define DTMF_SEND_DIGIT_FLAG           0x01


/*------------------------------------------------------------------*/
/* Mixer interface to IDI                                           */
/*------------------------------------------------------------------*/


#define LI2_FLAG_PCCONNECT_A_B 0x40000000
#define LI2_FLAG_PCCONNECT_B_A 0x80000000

#define MIXER_BCHANNELS_BRI    2
#define MIXER_IC_CHANNELS_BRI  MIXER_BCHANNELS_BRI
#define MIXER_IC_CHANNEL_BASE  MIXER_BCHANNELS_BRI
#define MIXER_CHANNELS_BRI     (MIXER_BCHANNELS_BRI + MIXER_IC_CHANNELS_BRI)
#define MIXER_CHANNELS_PRI     32

typedef struct li_config_s LI_CONFIG;

struct xconnect_card_address_s {
	dword low;
	dword high;
};

struct xconnect_transfer_address_s {
	struct xconnect_card_address_s card_address;
	dword offset;
};

struct li_config_s {
	DIVA_CAPI_ADAPTER   *adapter;
	PLCI   *plci;
	struct xconnect_transfer_address_s send_b;
	struct xconnect_transfer_address_s send_pc;
	byte   *flag_table;  /* dword aligned and sized */
	byte   *coef_table;  /* dword aligned and sized */
	byte channel;
	byte curchnl;
	byte chflags;
};

extern LI_CONFIG   *li_config_table;
extern word li_total_channels;

#define LI_CHANNEL_INVOLVED        0x01
#define LI_CHANNEL_ACTIVE          0x02
#define LI_CHANNEL_TX_DATA         0x04
#define LI_CHANNEL_RX_DATA         0x08
#define LI_CHANNEL_CONFERENCE      0x10
#define LI_CHANNEL_ADDRESSES_SET   0x80

#define LI_CHFLAG_MONITOR          0x01
#define LI_CHFLAG_MIX              0x02
#define LI_CHFLAG_LOOP             0x04

#define LI_FLAG_INTERCONNECT       0x01
#define LI_FLAG_MONITOR            0x02
#define LI_FLAG_MIX                0x04
#define LI_FLAG_PCCONNECT          0x08
#define LI_FLAG_CONFERENCE         0x10
#define LI_FLAG_ANNOUNCEMENT       0x20

#define LI_COEF_CH_CH              0x01
#define LI_COEF_CH_PC              0x02
#define LI_COEF_PC_CH              0x04
#define LI_COEF_PC_PC              0x08
#define LI_COEF_CH_CH_SET          0x10
#define LI_COEF_CH_PC_SET          0x20
#define LI_COEF_PC_CH_SET          0x40
#define LI_COEF_PC_PC_SET          0x80

#define LI_REQ_SILENT_UPDATE       0xffff

#define LI_PLCI_B_LAST_FLAG        ((dword) 0x80000000L)
#define LI_PLCI_B_DISC_FLAG        ((dword) 0x40000000L)
#define LI_PLCI_B_SKIP_FLAG        ((dword) 0x20000000L)
#define LI_PLCI_B_FLAG_MASK        ((dword) 0xe0000000L)

#define UDATA_REQUEST_SET_MIXER_COEFS_BRI       24
#define UDATA_REQUEST_SET_MIXER_COEFS_PRI_SYNC  25
#define UDATA_REQUEST_SET_MIXER_COEFS_PRI_ASYN  26
#define UDATA_INDICATION_MIXER_COEFS_SET        24

#define MIXER_FEATURE_ENABLE_TX_DATA        0x0001
#define MIXER_FEATURE_ENABLE_RX_DATA        0x0002

#define MIXER_COEF_LINE_CHANNEL_MASK        0x1f
#define MIXER_COEF_LINE_FROM_PC_FLAG        0x20
#define MIXER_COEF_LINE_TO_PC_FLAG          0x40
#define MIXER_COEF_LINE_ROW_FLAG            0x80

#define UDATA_REQUEST_XCONNECT_FROM         28
#define UDATA_INDICATION_XCONNECT_FROM      28
#define UDATA_REQUEST_XCONNECT_TO           29
#define UDATA_INDICATION_XCONNECT_TO        29

#define XCONNECT_CHANNEL_PORT_B             0x0000
#define XCONNECT_CHANNEL_PORT_PC            0x8000
#define XCONNECT_CHANNEL_PORT_MASK          0x8000
#define XCONNECT_CHANNEL_NUMBER_MASK        0x7fff
#define XCONNECT_CHANNEL_PORT_COUNT         2

#define XCONNECT_SUCCESS           0x0000
#define XCONNECT_ERROR             0x0001


/*------------------------------------------------------------------*/
/* Echo canceller interface to IDI                                  */
/*------------------------------------------------------------------*/


#define PRIVATE_ECHO_CANCELLER         0

#define PRIV_SELECTOR_ECHO_CANCELLER   255

#define EC_ENABLE_OPERATION            1
#define EC_DISABLE_OPERATION           2
#define EC_FREEZE_COEFFICIENTS         3
#define EC_RESUME_COEFFICIENT_UPDATE   4
#define EC_RESET_COEFFICIENTS          5

#define EC_DISABLE_NON_LINEAR_PROCESSING     0x0001
#define EC_DO_NOT_REQUIRE_REVERSALS          0x0002
#define EC_DETECT_DISABLE_TONE               0x0004

#define EC_SUCCESS                           0
#define EC_UNSUPPORTED_OPERATION             1

#define EC_BYPASS_DUE_TO_CONTINUOUS_2100HZ   1
#define EC_BYPASS_DUE_TO_REVERSED_2100HZ     2
#define EC_BYPASS_RELEASED                   3

#define DSP_CTRL_SET_LEC_PARAMETERS          0x05

#define LEC_ENABLE_ECHO_CANCELLER            0x0001
#define LEC_ENABLE_2100HZ_DETECTOR           0x0002
#define LEC_REQUIRE_2100HZ_REVERSALS         0x0004
#define LEC_MANUAL_DISABLE                   0x0008
#define LEC_ENABLE_NONLINEAR_PROCESSING      0x0010
#define LEC_FREEZE_COEFFICIENTS              0x0020
#define LEC_RESET_COEFFICIENTS               0x8000

#define LEC_MAX_SUPPORTED_TAIL_LENGTH        32

#define LEC_UDATA_INDICATION_DISABLE_DETECT  9

#define LEC_DISABLE_TYPE_CONTIGNUOUS_2100HZ  0x00
#define LEC_DISABLE_TYPE_REVERSED_2100HZ     0x01
#define LEC_DISABLE_RELEASED                 0x02


/*------------------------------------------------------------------*/
/* RTP interface to IDI                                             */
/*------------------------------------------------------------------*/


#define B1_RTP                  31
#define B2_RTP                  31
#define B3_RTP                  31

#define PRIVATE_RTP                    1

#define RTP_PRIM_PAYLOAD_PCMU_8000     0
#define RTP_PRIM_PAYLOAD_1016_8000     1
#define RTP_PRIM_PAYLOAD_G726_32_8000  2
#define RTP_PRIM_PAYLOAD_GSM_8000      3
#define RTP_PRIM_PAYLOAD_G723_8000     4
#define RTP_PRIM_PAYLOAD_DVI4_8000     5
#define RTP_PRIM_PAYLOAD_DVI4_16000    6
#define RTP_PRIM_PAYLOAD_LPC_8000      7
#define RTP_PRIM_PAYLOAD_PCMA_8000     8
#define RTP_PRIM_PAYLOAD_G722_16000    9
#define RTP_PRIM_PAYLOAD_QCELP_8000    12
#define RTP_PRIM_PAYLOAD_G728_8000     14
#define RTP_PRIM_PAYLOAD_G729_8000     18
#define RTP_PRIM_PAYLOAD_GSM_HR_8000   30
#define RTP_PRIM_PAYLOAD_GSM_EFR_8000  31

#define RTP_ADD_PAYLOAD_BASE           32
#define RTP_ADD_PAYLOAD_RED            32
#define RTP_ADD_PAYLOAD_CN_8000        33
#define RTP_ADD_PAYLOAD_DTMF           34

#define RTP_SUCCESS                         0
#define RTP_ERR_SSRC_OR_PAYLOAD_CHANGE      1

#define UDATA_REQUEST_RTP_RECONFIGURE       64
#define UDATA_INDICATION_RTP_CHANGE         65
#define BUDATA_REQUEST_QUERY_RTCP_REPORT    1
#define BUDATA_INDICATION_RTCP_REPORT       1

#define RTP_CONNECT_OPTION_DISC_ON_SSRC_CHANGE    0x00000001L
#define RTP_CONNECT_OPTION_DISC_ON_PT_CHANGE      0x00000002L
#define RTP_CONNECT_OPTION_DISC_ON_UNKNOWN_PT     0x00000004L
#define RTP_CONNECT_OPTION_NO_SILENCE_TRANSMIT    0x00010000L

#define RTP_PAYLOAD_OPTION_VOICE_ACTIVITY_DETECT  0x0001
#define RTP_PAYLOAD_OPTION_DISABLE_POST_FILTER    0x0002
#define RTP_PAYLOAD_OPTION_G723_LOW_CODING_RATE   0x0100

#define RTP_PACKET_FILTER_IGNORE_UNKNOWN_SSRC     0x00000001L

#define RTP_CHANGE_FLAG_SSRC_CHANGE               0x00000001L
#define RTP_CHANGE_FLAG_PAYLOAD_TYPE_CHANGE       0x00000002L
#define RTP_CHANGE_FLAG_UNKNOWN_PAYLOAD_TYPE      0x00000004L


/*------------------------------------------------------------------*/
/* T.38 interface to IDI                                            */
/*------------------------------------------------------------------*/


#define B1_T38                  30
#define B2_T38                  30
#define B3_T38                  30

#define PRIVATE_T38                    2


/*------------------------------------------------------------------*/
/* PIAFS interface to IDI                                            */
/*------------------------------------------------------------------*/


#define B1_PIAFS                29
#define B2_PIAFS                29

#define PRIVATE_PIAFS           29

/*
  B2 configuration for PIAFS:
  +---------------------+------+-----------------------------------------+
  | PIAFS Protocol      | byte | Bit 1 - Protocol Speed                  |
  | Speed configuration |      |         0 - 32K                         |
  |                     |      |         1 - 64K (default)               |
  |                     |      | Bit 2 - Variable Protocol Speed         |
  |                     |      |         0 - Speed is fix                |
  |                     |      |         1 - Speed is variable (default) |
  +---------------------+------+-----------------------------------------+
  | Direction           | word | Enable compression/decompression for    |
  |                     |      | 0: All direction                        |
  |                     |      | 1: disable outgoing data                |
  |                     |      | 2: disable incoming data               |
  |                     |      | 3: disable both direction (default)     |
  +---------------------+------+-----------------------------------------+
  | Number of code      | word | Parameter P1 of V.42bis in accordance   |
  | words               |      | with V.42bis                            |
  +---------------------+------+-----------------------------------------+
  | Maximum String      | word | Parameter P2 of V.42bis in accordance   |
  | Length              |      | with V.42bis                            |
  +---------------------+------+-----------------------------------------+
  | control (UDATA)     | byte | enable PIAFS control communication      |
  | abilities           |      |                                         |
  +---------------------+------+-----------------------------------------+
*/
#define PIAFS_UDATA_ABILITIES  0x80

/*------------------------------------------------------------------*/
/* FAX SUB/SEP/PWD extension                                        */
/*------------------------------------------------------------------*/


#define PRIVATE_FAX_SUB_SEP_PWD        3



/*------------------------------------------------------------------*/
/* V.18 extension                                                   */
/*------------------------------------------------------------------*/


#define PRIVATE_V18                    4



/*------------------------------------------------------------------*/
/* DTMF TONE extension                                              */
/*------------------------------------------------------------------*/


#define DTMF_GET_SUPPORTED_DETECT_CODES  0xf8
#define DTMF_GET_SUPPORTED_SEND_CODES    0xf9
#define DTMF_LISTEN_TONE_START           0xfa
#define DTMF_LISTEN_TONE_STOP            0xfb
#define DTMF_SEND_TONE                   0xfc
#define DTMF_LISTEN_MF_START             0xfd
#define DTMF_LISTEN_MF_STOP              0xfe
#define DTMF_SEND_MF                     0xff

#define DTMF_MF_DIGIT_TONE_CODE_1               0x10
#define DTMF_MF_DIGIT_TONE_CODE_2               0x11
#define DTMF_MF_DIGIT_TONE_CODE_3               0x12
#define DTMF_MF_DIGIT_TONE_CODE_4               0x13
#define DTMF_MF_DIGIT_TONE_CODE_5               0x14
#define DTMF_MF_DIGIT_TONE_CODE_6               0x15
#define DTMF_MF_DIGIT_TONE_CODE_7               0x16
#define DTMF_MF_DIGIT_TONE_CODE_8               0x17
#define DTMF_MF_DIGIT_TONE_CODE_9               0x18
#define DTMF_MF_DIGIT_TONE_CODE_0               0x19
#define DTMF_MF_DIGIT_TONE_CODE_K1              0x1a
#define DTMF_MF_DIGIT_TONE_CODE_K2              0x1b
#define DTMF_MF_DIGIT_TONE_CODE_KP              0x1c
#define DTMF_MF_DIGIT_TONE_CODE_S1              0x1d
#define DTMF_MF_DIGIT_TONE_CODE_ST              0x1e

#define DTMF_DIGIT_CODE_COUNT                   16
#define DTMF_MF_DIGIT_CODE_BASE                 DSP_DTMF_DIGIT_CODE_COUNT
#define DTMF_MF_DIGIT_CODE_COUNT                15
#define DTMF_TOTAL_DIGIT_CODE_COUNT             (DSP_MF_DIGIT_CODE_BASE + DSP_MF_DIGIT_CODE_COUNT)

#define DTMF_TONE_DIGIT_BASE                    0x80

#define DTMF_SIGNAL_NO_TONE                     (DTMF_TONE_DIGIT_BASE + 0)
#define DTMF_SIGNAL_UNIDENTIFIED_TONE           (DTMF_TONE_DIGIT_BASE + 1)

#define DTMF_SIGNAL_DIAL_TONE                   (DTMF_TONE_DIGIT_BASE + 2)
#define DTMF_SIGNAL_PABX_INTERNAL_DIAL_TONE     (DTMF_TONE_DIGIT_BASE + 3)
#define DTMF_SIGNAL_SPECIAL_DIAL_TONE           (DTMF_TONE_DIGIT_BASE + 4)   /* stutter dial tone */
#define DTMF_SIGNAL_SECOND_DIAL_TONE            (DTMF_TONE_DIGIT_BASE + 5)
#define DTMF_SIGNAL_RINGING_TONE                (DTMF_TONE_DIGIT_BASE + 6)
#define DTMF_SIGNAL_SPECIAL_RINGING_TONE        (DTMF_TONE_DIGIT_BASE + 7)
#define DTMF_SIGNAL_BUSY_TONE                   (DTMF_TONE_DIGIT_BASE + 8)
#define DTMF_SIGNAL_CONGESTION_TONE             (DTMF_TONE_DIGIT_BASE + 9)   /* reorder tone */
#define DTMF_SIGNAL_SPECIAL_INFORMATION_TONE    (DTMF_TONE_DIGIT_BASE + 10)
#define DTMF_SIGNAL_COMFORT_TONE                (DTMF_TONE_DIGIT_BASE + 11)
#define DTMF_SIGNAL_HOLD_TONE                   (DTMF_TONE_DIGIT_BASE + 12)
#define DTMF_SIGNAL_RECORD_TONE                 (DTMF_TONE_DIGIT_BASE + 13)
#define DTMF_SIGNAL_CALLER_WAITING_TONE         (DTMF_TONE_DIGIT_BASE + 14)
#define DTMF_SIGNAL_CALL_WAITING_TONE           (DTMF_TONE_DIGIT_BASE + 15)
#define DTMF_SIGNAL_PAY_TONE                    (DTMF_TONE_DIGIT_BASE + 16)
#define DTMF_SIGNAL_POSITIVE_INDICATION_TONE    (DTMF_TONE_DIGIT_BASE + 17)
#define DTMF_SIGNAL_NEGATIVE_INDICATION_TONE    (DTMF_TONE_DIGIT_BASE + 18)
#define DTMF_SIGNAL_WARNING_TONE                (DTMF_TONE_DIGIT_BASE + 19)
#define DTMF_SIGNAL_INTRUSION_TONE              (DTMF_TONE_DIGIT_BASE + 20)
#define DTMF_SIGNAL_CALLING_CARD_SERVICE_TONE   (DTMF_TONE_DIGIT_BASE + 21)
#define DTMF_SIGNAL_PAYPHONE_RECOGNITION_TONE   (DTMF_TONE_DIGIT_BASE + 22)
#define DTMF_SIGNAL_CPE_ALERTING_SIGNAL         (DTMF_TONE_DIGIT_BASE + 23)
#define DTMF_SIGNAL_OFF_HOOK_WARNING_TONE       (DTMF_TONE_DIGIT_BASE + 24)

#define DTMF_SIGNAL_INTERCEPT_TONE              (DTMF_TONE_DIGIT_BASE + 63)

#define DTMF_SIGNAL_MODEM_CALLING_TONE          (DTMF_TONE_DIGIT_BASE + 64)
#define DTMF_SIGNAL_FAX_CALLING_TONE            (DTMF_TONE_DIGIT_BASE + 65)
#define DTMF_SIGNAL_ANSWER_TONE                 (DTMF_TONE_DIGIT_BASE + 66)
#define DTMF_SIGNAL_REVERSED_ANSWER_TONE        (DTMF_TONE_DIGIT_BASE + 67)
#define DTMF_SIGNAL_ANSAM_TONE                  (DTMF_TONE_DIGIT_BASE + 68)
#define DTMF_SIGNAL_REVERSED_ANSAM_TONE         (DTMF_TONE_DIGIT_BASE + 69)
#define DTMF_SIGNAL_BELL103_ANSWER_TONE         (DTMF_TONE_DIGIT_BASE + 70)
#define DTMF_SIGNAL_FAX_FLAGS                   (DTMF_TONE_DIGIT_BASE + 71)
#define DTMF_SIGNAL_G2_FAX_GROUP_ID             (DTMF_TONE_DIGIT_BASE + 72)
#define DTMF_SIGNAL_HUMAN_SPEECH                (DTMF_TONE_DIGIT_BASE + 73)
#define DTMF_SIGNAL_ANSWERING_MACHINE_390       (DTMF_TONE_DIGIT_BASE + 74)

#define DTMF_MF_LISTEN_ACTIVE_FLAG     0x02
#define DTMF_SEND_MF_FLAG              0x02
#define DTMF_TONE_LISTEN_ACTIVE_FLAG   0x04
#define DTMF_SEND_TONE_FLAG            0x04

#define PRIVATE_DTMF_TONE              5


/*------------------------------------------------------------------*/
/* FAX paper format extension                                       */
/*------------------------------------------------------------------*/


#define PRIVATE_FAX_PAPER_FORMATS      6



/*------------------------------------------------------------------*/
/* V.OWN extension                                                  */
/*------------------------------------------------------------------*/


#define PRIVATE_VOWN                   7



/*------------------------------------------------------------------*/
/* FAX non-standard facilities extension                            */
/*------------------------------------------------------------------*/


#define PRIVATE_FAX_NONSTANDARD        8



/*------------------------------------------------------------------*/
/* Advanced voice                                                   */
/*------------------------------------------------------------------*/

#define ADV_VOICE_WRITE_ACTIVATION    0
#define ADV_VOICE_WRITE_DEACTIVATION  1
#define ADV_VOICE_WRITE_UPDATE        2

#define ADV_VOICE_OLD_COEF_COUNT    6
#define ADV_VOICE_NEW_COEF_BASE     (ADV_VOICE_OLD_COEF_COUNT * sizeof(word))

/*------------------------------------------------------------------*/
/* B1 resource switching                                            */
/*------------------------------------------------------------------*/

#define B1_FACILITY_LOCAL  0x01
#define B1_FACILITY_MIXER  0x02
#define B1_FACILITY_DTMFX  0x04
#define B1_FACILITY_DTMFR  0x08
#define B1_FACILITY_VOICE  0x10
#define B1_FACILITY_EC     0x20

#define ADJUST_B_MODE_SAVE          0x0001
#define ADJUST_B_MODE_REMOVE_L23    0x0002
#define ADJUST_B_MODE_SWITCH_L1     0x0004
#define ADJUST_B_MODE_NO_RESOURCE   0x0008
#define ADJUST_B_MODE_ASSIGN_L23    0x0010
#define ADJUST_B_MODE_USER_CONNECT  0x0020
#define ADJUST_B_MODE_CONNECT       0x0040
#define ADJUST_B_MODE_RESTORE       0x0080

#define ADJUST_B_START                     0
#define ADJUST_B_SAVE_MIXER_1              1
#define ADJUST_B_SAVE_DTMF_1               2
#define ADJUST_B_REMOVE_L23_1              3
#define ADJUST_B_REMOVE_L23_2              4
#define ADJUST_B_SAVE_EC_1                 5
#define ADJUST_B_SAVE_DTMF_PARAMETER_1     6
#define ADJUST_B_SAVE_VOICE_1              7
#define ADJUST_B_SWITCH_L1_1               8
#define ADJUST_B_SWITCH_L1_2               9
#define ADJUST_B_RESTORE_VOICE_1           10
#define ADJUST_B_RESTORE_VOICE_2           11
#define ADJUST_B_RESTORE_DTMF_PARAMETER_1  12
#define ADJUST_B_RESTORE_DTMF_PARAMETER_2  13
#define ADJUST_B_RESTORE_EC_1              14
#define ADJUST_B_RESTORE_EC_2              15
#define ADJUST_B_ASSIGN_L23_1              16
#define ADJUST_B_ASSIGN_L23_2              17
#define ADJUST_B_CONNECT_1                 18
#define ADJUST_B_CONNECT_2                 19
#define ADJUST_B_CONNECT_3                 20
#define ADJUST_B_CONNECT_4                 21
#define ADJUST_B_RESTORE_DTMF_1            22
#define ADJUST_B_RESTORE_DTMF_2            23
#define ADJUST_B_RESTORE_MIXER_1           24
#define ADJUST_B_RESTORE_MIXER_2           25
#define ADJUST_B_RESTORE_MIXER_3           26
#define ADJUST_B_RESTORE_MIXER_4           27
#define ADJUST_B_RESTORE_MIXER_5           28
#define ADJUST_B_RESTORE_MIXER_6           29
#define ADJUST_B_RESTORE_MIXER_7           30
#define ADJUST_B_END                       31

/*------------------------------------------------------------------*/
/* XON Protocol def's                                               */
/*------------------------------------------------------------------*/
#define N_CH_XOFF               0x01
#define N_XON_SENT              0x02
#define N_XON_REQ               0x04
#define N_XON_CONNECT_IND       0x08
#define N_RX_FLOW_CONTROL_MASK  0x3f
#define N_OK_FC_PENDING         0x80
#define N_TX_FLOW_CONTROL_MASK  0xc0

/*------------------------------------------------------------------*/
/* NCPI state                                                       */
/*------------------------------------------------------------------*/
#define NCPI_VALID_CONNECT_B3_IND  0x01
#define NCPI_VALID_CONNECT_B3_ACT  0x02
#define NCPI_VALID_DISC_B3_IND     0x04
#define NCPI_CONNECT_B3_ACT_SENT   0x08
#define NCPI_NEGOTIATE_B3_SENT     0x10
#define NCPI_MDM_CTS_ON_RECEIVED   0x40
#define NCPI_MDM_DCD_ON_RECEIVED   0x80

/*------------------------------------------------------------------*/
