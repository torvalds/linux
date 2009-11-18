/*
 * zfcp device driver
 *
 * Interface to the FSF support functions.
 *
 * Copyright IBM Corporation 2002, 2009
 */

#ifndef FSF_H
#define FSF_H

#include <linux/pfn.h>
#include <linux/scatterlist.h>

#define FSF_QTCB_CURRENT_VERSION		0x00000001

/* FSF commands */
#define	FSF_QTCB_FCP_CMND			0x00000001
#define	FSF_QTCB_ABORT_FCP_CMND			0x00000002
#define	FSF_QTCB_OPEN_PORT_WITH_DID		0x00000005
#define	FSF_QTCB_OPEN_LUN			0x00000006
#define	FSF_QTCB_CLOSE_LUN			0x00000007
#define	FSF_QTCB_CLOSE_PORT			0x00000008
#define	FSF_QTCB_CLOSE_PHYSICAL_PORT		0x00000009
#define	FSF_QTCB_SEND_ELS			0x0000000B
#define	FSF_QTCB_SEND_GENERIC			0x0000000C
#define	FSF_QTCB_EXCHANGE_CONFIG_DATA		0x0000000D
#define	FSF_QTCB_EXCHANGE_PORT_DATA		0x0000000E
#define FSF_QTCB_DOWNLOAD_CONTROL_FILE		0x00000012
#define FSF_QTCB_UPLOAD_CONTROL_FILE		0x00000013

/* FSF QTCB types */
#define FSF_IO_COMMAND				0x00000001
#define FSF_SUPPORT_COMMAND			0x00000002
#define FSF_CONFIG_COMMAND			0x00000003
#define FSF_PORT_COMMAND			0x00000004

/* FSF control file upload/download operations' subtype and options */
#define FSF_CFDC_OPERATION_SUBTYPE		0x00020001
#define FSF_CFDC_OPTION_NORMAL_MODE		0x00000000
#define FSF_CFDC_OPTION_FORCE			0x00000001
#define FSF_CFDC_OPTION_FULL_ACCESS		0x00000002
#define FSF_CFDC_OPTION_RESTRICTED_ACCESS	0x00000004

/* FSF protocol states */
#define FSF_PROT_GOOD				0x00000001
#define FSF_PROT_QTCB_VERSION_ERROR		0x00000010
#define FSF_PROT_SEQ_NUMB_ERROR			0x00000020
#define FSF_PROT_UNSUPP_QTCB_TYPE		0x00000040
#define FSF_PROT_HOST_CONNECTION_INITIALIZING	0x00000080
#define FSF_PROT_FSF_STATUS_PRESENTED		0x00000100
#define FSF_PROT_DUPLICATE_REQUEST_ID		0x00000200
#define FSF_PROT_LINK_DOWN                      0x00000400
#define FSF_PROT_REEST_QUEUE                    0x00000800
#define FSF_PROT_ERROR_STATE			0x01000000

/* FSF states */
#define FSF_GOOD				0x00000000
#define FSF_PORT_ALREADY_OPEN			0x00000001
#define FSF_LUN_ALREADY_OPEN			0x00000002
#define FSF_PORT_HANDLE_NOT_VALID		0x00000003
#define FSF_LUN_HANDLE_NOT_VALID		0x00000004
#define FSF_HANDLE_MISMATCH			0x00000005
#define FSF_SERVICE_CLASS_NOT_SUPPORTED		0x00000006
#define FSF_FCPLUN_NOT_VALID			0x00000009
#define FSF_ACCESS_DENIED			0x00000010
#define FSF_LUN_SHARING_VIOLATION               0x00000012
#define FSF_FCP_COMMAND_DOES_NOT_EXIST		0x00000022
#define FSF_DIRECTION_INDICATOR_NOT_VALID	0x00000030
#define FSF_CMND_LENGTH_NOT_VALID		0x00000033
#define FSF_MAXIMUM_NUMBER_OF_PORTS_EXCEEDED	0x00000040
#define FSF_MAXIMUM_NUMBER_OF_LUNS_EXCEEDED	0x00000041
#define FSF_ELS_COMMAND_REJECTED		0x00000050
#define FSF_GENERIC_COMMAND_REJECTED		0x00000051
#define FSF_PORT_BOXED				0x00000059
#define FSF_LUN_BOXED				0x0000005A
#define FSF_EXCHANGE_CONFIG_DATA_INCOMPLETE	0x0000005B
#define FSF_PAYLOAD_SIZE_MISMATCH		0x00000060
#define FSF_REQUEST_SIZE_TOO_LARGE		0x00000061
#define FSF_RESPONSE_SIZE_TOO_LARGE		0x00000062
#define FSF_SBAL_MISMATCH			0x00000063
#define FSF_ADAPTER_STATUS_AVAILABLE		0x000000AD
#define FSF_UNKNOWN_COMMAND			0x000000E2
#define FSF_UNKNOWN_OP_SUBTYPE                  0x000000E3
#define FSF_INVALID_COMMAND_OPTION              0x000000E5
/* #define FSF_ERROR                             0x000000FF  */

#define FSF_PROT_STATUS_QUAL_SIZE		16
#define FSF_STATUS_QUALIFIER_SIZE		16

/* FSF status qualifier, recommendations */
#define FSF_SQ_NO_RECOM				0x00
#define FSF_SQ_FCP_RSP_AVAILABLE		0x01
#define FSF_SQ_RETRY_IF_POSSIBLE		0x02
#define FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED	0x03
#define FSF_SQ_INVOKE_LINK_TEST_PROCEDURE	0x04
#define FSF_SQ_COMMAND_ABORTED			0x06
#define FSF_SQ_NO_RETRY_POSSIBLE		0x07

/* FSF status qualifier (most significant 4 bytes), local link down */
#define FSF_PSQ_LINK_NO_LIGHT			0x00000004
#define FSF_PSQ_LINK_WRAP_PLUG			0x00000008
#define FSF_PSQ_LINK_NO_FCP			0x00000010
#define FSF_PSQ_LINK_FIRMWARE_UPDATE		0x00000020
#define FSF_PSQ_LINK_INVALID_WWPN		0x00000100
#define FSF_PSQ_LINK_NO_NPIV_SUPPORT		0x00000200
#define FSF_PSQ_LINK_NO_FCP_RESOURCES		0x00000400
#define FSF_PSQ_LINK_NO_FABRIC_RESOURCES	0x00000800
#define FSF_PSQ_LINK_FABRIC_LOGIN_UNABLE	0x00001000
#define FSF_PSQ_LINK_WWPN_ASSIGNMENT_CORRUPTED	0x00002000
#define FSF_PSQ_LINK_MODE_TABLE_CURRUPTED	0x00004000
#define FSF_PSQ_LINK_NO_WWPN_ASSIGNMENT		0x00008000

/* payload size in status read buffer */
#define FSF_STATUS_READ_PAYLOAD_SIZE		4032

/* number of status read buffers that should be sent by ULP */
#define FSF_STATUS_READS_RECOM			16

/* status types in status read buffer */
#define FSF_STATUS_READ_PORT_CLOSED		0x00000001
#define FSF_STATUS_READ_INCOMING_ELS		0x00000002
#define FSF_STATUS_READ_SENSE_DATA_AVAIL        0x00000003
#define FSF_STATUS_READ_BIT_ERROR_THRESHOLD	0x00000004
#define FSF_STATUS_READ_LINK_DOWN		0x00000005
#define FSF_STATUS_READ_LINK_UP          	0x00000006
#define FSF_STATUS_READ_NOTIFICATION_LOST	0x00000009
#define FSF_STATUS_READ_CFDC_UPDATED		0x0000000A
#define FSF_STATUS_READ_FEATURE_UPDATE_ALERT	0x0000000C

/* status subtypes for link down */
#define FSF_STATUS_READ_SUB_NO_PHYSICAL_LINK	0x00000000
#define FSF_STATUS_READ_SUB_FDISC_FAILED	0x00000001
#define FSF_STATUS_READ_SUB_FIRMWARE_UPDATE	0x00000002

/* status subtypes for unsolicited status notification lost */
#define FSF_STATUS_READ_SUB_INCOMING_ELS	0x00000001
#define FSF_STATUS_READ_SUB_ACT_UPDATED		0x00000020

/* topologie that is detected by the adapter */
#define FSF_TOPO_P2P				0x00000001
#define FSF_TOPO_FABRIC				0x00000002
#define FSF_TOPO_AL				0x00000003

/* data direction for FCP commands */
#define FSF_DATADIR_WRITE			0x00000001
#define FSF_DATADIR_READ			0x00000002
#define FSF_DATADIR_CMND			0x00000004

/* fc service class */
#define FSF_CLASS_3				0x00000003

/* SBAL chaining */
#define FSF_MAX_SBALS_PER_REQ			36

/* logging space behind QTCB */
#define FSF_QTCB_LOG_SIZE			1024

/* channel features */
#define FSF_FEATURE_CFDC			0x00000002
#define FSF_FEATURE_LUN_SHARING			0x00000004
#define FSF_FEATURE_NOTIFICATION_LOST		0x00000008
#define FSF_FEATURE_HBAAPI_MANAGEMENT           0x00000010
#define FSF_FEATURE_ELS_CT_CHAINED_SBALS	0x00000020
#define FSF_FEATURE_UPDATE_ALERT		0x00000100
#define FSF_FEATURE_MEASUREMENT_DATA		0x00000200

/* host connection features */
#define FSF_FEATURE_NPIV_MODE			0x00000001

/* option */
#define FSF_OPEN_LUN_SUPPRESS_BOXING		0x00000001

/* open LUN access flags*/
#define FSF_UNIT_ACCESS_EXCLUSIVE		0x02000000
#define FSF_UNIT_ACCESS_OUTBOUND_TRANSFER	0x10000000

/* FSF interface for CFDC */
#define ZFCP_CFDC_MAX_SIZE		127 * 1024
#define ZFCP_CFDC_PAGES 		PFN_UP(ZFCP_CFDC_MAX_SIZE)

struct zfcp_fsf_cfdc {
	struct scatterlist sg[ZFCP_CFDC_PAGES];
	u32 command;
	u32 option;
};

struct fsf_queue_designator {
	u8  cssid;
	u8  chpid;
	u8  hla;
	u8  ua;
	u32 res1;
} __attribute__ ((packed));

struct fsf_bit_error_payload {
	u32 res1;
	u32 link_failure_error_count;
	u32 loss_of_sync_error_count;
	u32 loss_of_signal_error_count;
	u32 primitive_sequence_error_count;
	u32 invalid_transmission_word_error_count;
	u32 crc_error_count;
	u32 primitive_sequence_event_timeout_count;
	u32 elastic_buffer_overrun_error_count;
	u32 fcal_arbitration_timeout_count;
	u32 advertised_receive_b2b_credit;
	u32 current_receive_b2b_credit;
	u32 advertised_transmit_b2b_credit;
	u32 current_transmit_b2b_credit;
} __attribute__ ((packed));

struct fsf_link_down_info {
	u32 error_code;
	u32 res1;
	u8 res2[2];
	u8 primary_status;
	u8 ioerr_code;
	u8 action_code;
	u8 reason_code;
	u8 explanation_code;
	u8 vendor_specific_code;
} __attribute__ ((packed));

struct fsf_status_read_buffer {
	u32 status_type;
	u32 status_subtype;
	u32 length;
	u32 res1;
	struct fsf_queue_designator queue_designator;
	u32 d_id;
	u32 class;
	u64 fcp_lun;
	u8  res3[24];
	union {
		u8  data[FSF_STATUS_READ_PAYLOAD_SIZE];
		u32 word[FSF_STATUS_READ_PAYLOAD_SIZE/sizeof(u32)];
		struct fsf_link_down_info link_down_info;
		struct fsf_bit_error_payload bit_error;
	} payload;
} __attribute__ ((packed));

struct fsf_qual_version_error {
	u32 fsf_version;
	u32 res1[3];
} __attribute__ ((packed));

struct fsf_qual_sequence_error {
	u32 exp_req_seq_no;
	u32 res1[3];
} __attribute__ ((packed));

struct fsf_qual_latency_info {
	u32 channel_lat;
	u32 fabric_lat;
	u8 res1[8];
} __attribute__ ((packed));

union fsf_prot_status_qual {
	u32 word[FSF_PROT_STATUS_QUAL_SIZE / sizeof(u32)];
	u64 doubleword[FSF_PROT_STATUS_QUAL_SIZE / sizeof(u64)];
	struct fsf_qual_version_error   version_error;
	struct fsf_qual_sequence_error  sequence_error;
	struct fsf_link_down_info link_down_info;
	struct fsf_qual_latency_info latency_info;
} __attribute__ ((packed));

struct fsf_qtcb_prefix {
	u64 req_id;
	u32 qtcb_version;
	u32 ulp_info;
	u32 qtcb_type;
	u32 req_seq_no;
	u32 prot_status;
	union fsf_prot_status_qual prot_status_qual;
	u8  res1[20];
} __attribute__ ((packed));

struct fsf_statistics_info {
	u64 input_req;
	u64 output_req;
	u64 control_req;
	u64 input_mb;
	u64 output_mb;
	u64 seconds_act;
} __attribute__ ((packed));

union fsf_status_qual {
	u8  byte[FSF_STATUS_QUALIFIER_SIZE];
	u16 halfword[FSF_STATUS_QUALIFIER_SIZE / sizeof (u16)];
	u32 word[FSF_STATUS_QUALIFIER_SIZE / sizeof (u32)];
	u64 doubleword[FSF_STATUS_QUALIFIER_SIZE / sizeof(u64)];
	struct fsf_queue_designator fsf_queue_designator;
	struct fsf_link_down_info link_down_info;
} __attribute__ ((packed));

struct fsf_qtcb_header {
	u64 req_handle;
	u32 fsf_command;
	u32 res1;
	u32 port_handle;
	u32 lun_handle;
	u32 res2;
	u32 fsf_status;
	union fsf_status_qual fsf_status_qual;
	u8  res3[28];
	u16 log_start;
	u16 log_length;
	u8  res4[16];
} __attribute__ ((packed));

struct fsf_nport_serv_param {
	u8  common_serv_param[16];
	u64 wwpn;
	u64 wwnn;
	u8  class1_serv_param[16];
	u8  class2_serv_param[16];
	u8  class3_serv_param[16];
	u8  class4_serv_param[16];
	u8  vendor_version_level[16];
} __attribute__ ((packed));

#define FSF_PLOGI_MIN_LEN	112
struct fsf_plogi {
	u32    code;
	struct fsf_nport_serv_param serv_param;
} __attribute__ ((packed));

#define FSF_FCP_CMND_SIZE	288
#define FSF_FCP_RSP_SIZE	128

struct fsf_qtcb_bottom_io {
	u32 data_direction;
	u32 service_class;
	u8  res1[8];
	u32 fcp_cmnd_length;
	u8  res2[12];
	u8  fcp_cmnd[FSF_FCP_CMND_SIZE];
	u8  fcp_rsp[FSF_FCP_RSP_SIZE];
	u8  res3[64];
} __attribute__ ((packed));

struct fsf_qtcb_bottom_support {
	u32 operation_subtype;
	u8  res1[12];
	u32 d_id;
	u32 option;
	u64 fcp_lun;
	u64 res2;
	u64 req_handle;
	u32 service_class;
	u8  res3[3];
	u8  timeout;
        u32 lun_access_info;
        u8  res4[180];
	u32 els1_length;
	u32 els2_length;
	u32 req_buf_length;
	u32 resp_buf_length;
	u8  els[256];
} __attribute__ ((packed));

struct fsf_qtcb_bottom_config {
	u32 lic_version;
	u32 feature_selection;
	u32 high_qtcb_version;
	u32 low_qtcb_version;
	u32 max_qtcb_size;
	u32 max_data_transfer_size;
	u32 adapter_features;
	u32 connection_features;
	u32 fc_topology;
	u32 fc_link_speed;
	u32 adapter_type;
	u32 peer_d_id;
	u8 res1[2];
	u16 timer_interval;
	u8 res2[8];
	u32 s_id;
	struct fsf_nport_serv_param nport_serv_param;
	u8 reserved_nport_serv_param[16];
	u8 res3[8];
	u32 adapter_ports;
	u32 hardware_version;
	u8 serial_number[32];
	struct fsf_nport_serv_param plogi_payload;
	struct fsf_statistics_info stat_info;
	u8 res4[112];
} __attribute__ ((packed));

struct fsf_qtcb_bottom_port {
	u64 wwpn;
	u32 fc_port_id;
	u32 port_type;
	u32 port_state;
	u32 class_of_service;	/* should be 0x00000006 for class 2 and 3 */
	u8 supported_fc4_types[32]; /* should be 0x00000100 for scsi fcp */
	u8 active_fc4_types[32];
	u32 supported_speed;	/* 0x0001 for 1 GBit/s or 0x0002 for 2 GBit/s */
	u32 maximum_frame_size;	/* fixed value of 2112 */
	u64 seconds_since_last_reset;
	u64 tx_frames;
	u64 tx_words;
	u64 rx_frames;
	u64 rx_words;
	u64 lip;		/* 0 */
	u64 nos;		/* currently 0 */
	u64 error_frames;	/* currently 0 */
	u64 dumped_frames;	/* currently 0 */
	u64 link_failure;
	u64 loss_of_sync;
	u64 loss_of_signal;
	u64 psp_error_counts;
	u64 invalid_tx_words;
	u64 invalid_crcs;
	u64 input_requests;
	u64 output_requests;
	u64 control_requests;
	u64 input_mb;		/* where 1 MByte == 1.000.000 Bytes */
	u64 output_mb;		/* where 1 MByte == 1.000.000 Bytes */
	u8 cp_util;
	u8 cb_util;
	u8 a_util;
	u8 res2[253];
} __attribute__ ((packed));

union fsf_qtcb_bottom {
	struct fsf_qtcb_bottom_io      io;
	struct fsf_qtcb_bottom_support support;
	struct fsf_qtcb_bottom_config  config;
	struct fsf_qtcb_bottom_port port;
};

struct fsf_qtcb {
	struct fsf_qtcb_prefix prefix;
	struct fsf_qtcb_header header;
	union  fsf_qtcb_bottom bottom;
	u8 log[FSF_QTCB_LOG_SIZE];
} __attribute__ ((packed));

struct zfcp_blk_drv_data {
#define ZFCP_BLK_DRV_DATA_MAGIC			0x1
	u32 magic;
#define ZFCP_BLK_LAT_VALID			0x1
#define ZFCP_BLK_REQ_ERROR			0x2
	u16 flags;
	u8 inb_usage;
	u8 outb_usage;
	u64 channel_lat;
	u64 fabric_lat;
} __attribute__ ((packed));

#endif				/* FSF_H */
