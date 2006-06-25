/* 
 * This file is part of the zfcp device driver for
 * FCP adapters for IBM System z9 and zSeries.
 *
 * (C) Copyright IBM Corp. 2002, 2006
 * 
 * This program is free software; you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation; either version 2, or (at your option) 
 * any later version. 
 * 
 * This program is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
 * GNU General Public License for more details. 
 * 
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 */ 


#ifndef ZFCP_DEF_H
#define ZFCP_DEF_H

/*************************** INCLUDES *****************************************/

#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/miscdevice.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <scsi/scsi.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>
#include "zfcp_fsf.h"
#include <asm/ccwdev.h>
#include <asm/qdio.h>
#include <asm/debug.h>
#include <asm/ebcdic.h>
#include <linux/mempool.h>
#include <linux/syscalls.h>
#include <linux/ioctl.h>


/********************* GENERAL DEFINES *********************************/

/* zfcp version number, it consists of major, minor, and patch-level number */
#define ZFCP_VERSION		"4.7.0"

/**
 * zfcp_sg_to_address - determine kernel address from struct scatterlist
 * @list: struct scatterlist
 * Return: kernel address
 */
static inline void *
zfcp_sg_to_address(struct scatterlist *list)
{
	return (void *) (page_address(list->page) + list->offset);
}

/**
 * zfcp_address_to_sg - set up struct scatterlist from kernel address
 * @address: kernel address
 * @list: struct scatterlist
 */
static inline void
zfcp_address_to_sg(void *address, struct scatterlist *list)
{
	list->page = virt_to_page(address);
	list->offset = ((unsigned long) address) & (PAGE_SIZE - 1);
}

#define REQUEST_LIST_SIZE 128

/********************* SCSI SPECIFIC DEFINES *********************************/
#define ZFCP_SCSI_ER_TIMEOUT                    (100*HZ)

/********************* CIO/QDIO SPECIFIC DEFINES *****************************/

/* Adapter Identification Parameters */
#define ZFCP_CONTROL_UNIT_TYPE  0x1731
#define ZFCP_CONTROL_UNIT_MODEL 0x03
#define ZFCP_DEVICE_TYPE        0x1732
#define ZFCP_DEVICE_MODEL       0x03
#define ZFCP_DEVICE_MODEL_PRIV	0x04
 
/* allow as many chained SBALs as are supported by hardware */
#define ZFCP_MAX_SBALS_PER_REQ		FSF_MAX_SBALS_PER_REQ
#define ZFCP_MAX_SBALS_PER_CT_REQ	FSF_MAX_SBALS_PER_REQ
#define ZFCP_MAX_SBALS_PER_ELS_REQ	FSF_MAX_SBALS_PER_ELS_REQ

/* DMQ bug workaround: don't use last SBALE */
#define ZFCP_MAX_SBALES_PER_SBAL	(QDIO_MAX_ELEMENTS_PER_BUFFER - 1)

/* index of last SBALE (with respect to DMQ bug workaround) */
#define ZFCP_LAST_SBALE_PER_SBAL	(ZFCP_MAX_SBALES_PER_SBAL - 1)

/* max. number of (data buffer) SBALEs in largest SBAL chain */
#define ZFCP_MAX_SBALES_PER_REQ		\
	(ZFCP_MAX_SBALS_PER_REQ * ZFCP_MAX_SBALES_PER_SBAL - 2)
        /* request ID + QTCB in SBALE 0 + 1 of first SBAL in chain */

/* FIXME(tune): free space should be one max. SBAL chain plus what? */
#define ZFCP_QDIO_PCI_INTERVAL		(QDIO_MAX_BUFFERS_PER_Q \
                                         - (ZFCP_MAX_SBALS_PER_REQ + 4))

#define ZFCP_SBAL_TIMEOUT               (5*HZ)

#define ZFCP_TYPE2_RECOVERY_TIME        (8*HZ)

/* queue polling (values in microseconds) */
#define ZFCP_MAX_INPUT_THRESHOLD 	5000	/* FIXME: tune */
#define ZFCP_MAX_OUTPUT_THRESHOLD 	1000	/* FIXME: tune */
#define ZFCP_MIN_INPUT_THRESHOLD 	1	/* ignored by QDIO layer */
#define ZFCP_MIN_OUTPUT_THRESHOLD 	1	/* ignored by QDIO layer */

#define QDIO_SCSI_QFMT			1	/* 1 for FSF */

/********************* FSF SPECIFIC DEFINES *********************************/

#define ZFCP_ULP_INFO_VERSION                   26
#define ZFCP_QTCB_VERSION	FSF_QTCB_CURRENT_VERSION
/* ATTENTION: value must not be used by hardware */
#define FSF_QTCB_UNSOLICITED_STATUS		0x6305
#define ZFCP_STATUS_READ_FAILED_THRESHOLD	3
#define ZFCP_STATUS_READS_RECOM		        FSF_STATUS_READS_RECOM

/* Do 1st retry in 1 second, then double the timeout for each following retry */
#define ZFCP_EXCHANGE_CONFIG_DATA_FIRST_SLEEP	100
#define ZFCP_EXCHANGE_CONFIG_DATA_RETRIES	7

/* timeout value for "default timer" for fsf requests */
#define ZFCP_FSF_REQUEST_TIMEOUT (60*HZ);

/*************** FIBRE CHANNEL PROTOCOL SPECIFIC DEFINES ********************/

typedef unsigned long long wwn_t;
typedef unsigned long long fcp_lun_t;
/* data length field may be at variable position in FCP-2 FCP_CMND IU */
typedef unsigned int       fcp_dl_t;

#define ZFCP_FC_SERVICE_CLASS_DEFAULT	FSF_CLASS_3

/* timeout for name-server lookup (in seconds) */
#define ZFCP_NS_GID_PN_TIMEOUT		10

/* largest SCSI command we can process */
/* FCP-2 (FCP_CMND IU) allows up to (255-3+16) */
#define ZFCP_MAX_SCSI_CMND_LENGTH	255
/* maximum number of commands in LUN queue (tagged queueing) */
#define ZFCP_CMND_PER_LUN               32

/* task attribute values in FCP-2 FCP_CMND IU */
#define SIMPLE_Q	0
#define HEAD_OF_Q	1
#define ORDERED_Q	2
#define ACA_Q		4
#define UNTAGGED	5

/* task management flags in FCP-2 FCP_CMND IU */
#define FCP_CLEAR_ACA		0x40
#define FCP_TARGET_RESET	0x20
#define FCP_LOGICAL_UNIT_RESET	0x10
#define FCP_CLEAR_TASK_SET	0x04
#define FCP_ABORT_TASK_SET	0x02

#define FCP_CDB_LENGTH		16

#define ZFCP_DID_MASK           0x00FFFFFF

/* FCP(-2) FCP_CMND IU */
struct fcp_cmnd_iu {
	fcp_lun_t fcp_lun;	   /* FCP logical unit number */
	u8  crn;	           /* command reference number */
	u8  reserved0:5;	   /* reserved */
	u8  task_attribute:3;	   /* task attribute */
	u8  task_management_flags; /* task management flags */
	u8  add_fcp_cdb_length:6;  /* additional FCP_CDB length */
	u8  rddata:1;              /* read data */
	u8  wddata:1;              /* write data */
	u8  fcp_cdb[FCP_CDB_LENGTH];
} __attribute__((packed));

/* FCP(-2) FCP_RSP IU */
struct fcp_rsp_iu {
	u8  reserved0[10];
	union {
		struct {
			u8 reserved1:3;
			u8 fcp_conf_req:1;
			u8 fcp_resid_under:1;
			u8 fcp_resid_over:1;
			u8 fcp_sns_len_valid:1;
			u8 fcp_rsp_len_valid:1;
		} bits;
		u8 value;
	} validity;
	u8  scsi_status;
	u32 fcp_resid;
	u32 fcp_sns_len;
	u32 fcp_rsp_len;
} __attribute__((packed));


#define RSP_CODE_GOOD		 0
#define RSP_CODE_LENGTH_MISMATCH 1
#define RSP_CODE_FIELD_INVALID	 2
#define RSP_CODE_RO_MISMATCH	 3
#define RSP_CODE_TASKMAN_UNSUPP	 4
#define RSP_CODE_TASKMAN_FAILED	 5

/* see fc-fs */
#define LS_RSCN  0x61040000
#define LS_LOGO  0x05000000
#define LS_PLOGI 0x03000000

struct fcp_rscn_head {
        u8  command;
        u8  page_length; /* always 0x04 */
        u16 payload_len;
} __attribute__((packed));

struct fcp_rscn_element {
        u8  reserved:2;
        u8  event_qual:4;
        u8  addr_format:2;
        u32 nport_did:24;
} __attribute__((packed));

#define ZFCP_PORT_ADDRESS   0x0
#define ZFCP_AREA_ADDRESS   0x1
#define ZFCP_DOMAIN_ADDRESS 0x2
#define ZFCP_FABRIC_ADDRESS 0x3

#define ZFCP_PORTS_RANGE_PORT   0xFFFFFF
#define ZFCP_PORTS_RANGE_AREA   0xFFFF00
#define ZFCP_PORTS_RANGE_DOMAIN 0xFF0000
#define ZFCP_PORTS_RANGE_FABRIC 0x000000

#define ZFCP_NO_PORTS_PER_AREA    0x100
#define ZFCP_NO_PORTS_PER_DOMAIN  0x10000
#define ZFCP_NO_PORTS_PER_FABRIC  0x1000000

/* see fc-ph */
struct fcp_logo {
        u32 command;
        u32 nport_did;
        wwn_t nport_wwpn;
} __attribute__((packed));

/*
 * DBF stuff
 */
#define ZFCP_DBF_TAG_SIZE      4

struct zfcp_dbf_dump {
	u8 tag[ZFCP_DBF_TAG_SIZE];
	u32 total_size;		/* size of total dump data */
	u32 offset;		/* how much data has being already dumped */
	u32 size;		/* how much data comes with this record */
	u8 data[];		/* dump data */
} __attribute__ ((packed));

/* FIXME: to be inflated when reworking the erp dbf */
struct zfcp_erp_dbf_record {
	u8 dummy[16];
} __attribute__ ((packed));

struct zfcp_hba_dbf_record_response {
	u32 fsf_command;
	u64 fsf_reqid;
	u32 fsf_seqno;
	u64 fsf_issued;
	u32 fsf_prot_status;
	u32 fsf_status;
	u8 fsf_prot_status_qual[FSF_PROT_STATUS_QUAL_SIZE];
	u8 fsf_status_qual[FSF_STATUS_QUALIFIER_SIZE];
	u32 fsf_req_status;
	u8 sbal_first;
	u8 sbal_curr;
	u8 sbal_last;
	u8 pool;
	u64 erp_action;
	union {
		struct {
			u64 scsi_cmnd;
			u64 scsi_serial;
		} send_fcp;
		struct {
			u64 wwpn;
			u32 d_id;
			u32 port_handle;
		} port;
		struct {
			u64 wwpn;
			u64 fcp_lun;
			u32 port_handle;
			u32 lun_handle;
		} unit;
		struct {
			u32 d_id;
			u8 ls_code;
		} send_els;
	} data;
} __attribute__ ((packed));

struct zfcp_hba_dbf_record_status {
	u8 failed;
	u32 status_type;
	u32 status_subtype;
	struct fsf_queue_designator
	 queue_designator;
	u32 payload_size;
#define ZFCP_DBF_UNSOL_PAYLOAD				80
#define ZFCP_DBF_UNSOL_PAYLOAD_SENSE_DATA_AVAIL		32
#define ZFCP_DBF_UNSOL_PAYLOAD_BIT_ERROR_THRESHOLD	56
#define ZFCP_DBF_UNSOL_PAYLOAD_FEATURE_UPDATE_ALERT	2 * sizeof(u32)
	u8 payload[ZFCP_DBF_UNSOL_PAYLOAD];
} __attribute__ ((packed));

struct zfcp_hba_dbf_record_qdio {
	u32 status;
	u32 qdio_error;
	u32 siga_error;
	u8 sbal_index;
	u8 sbal_count;
} __attribute__ ((packed));

struct zfcp_hba_dbf_record {
	u8 tag[ZFCP_DBF_TAG_SIZE];
	u8 tag2[ZFCP_DBF_TAG_SIZE];
	union {
		struct zfcp_hba_dbf_record_response response;
		struct zfcp_hba_dbf_record_status status;
		struct zfcp_hba_dbf_record_qdio qdio;
	} type;
} __attribute__ ((packed));

struct zfcp_san_dbf_record_ct {
	union {
		struct {
			u16 cmd_req_code;
			u8 revision;
			u8 gs_type;
			u8 gs_subtype;
			u8 options;
			u16 max_res_size;
		} request;
		struct {
			u16 cmd_rsp_code;
			u8 revision;
			u8 reason_code;
			u8 reason_code_expl;
			u8 vendor_unique;
		} response;
	} type;
	u32 payload_size;
#define ZFCP_DBF_CT_PAYLOAD	24
	u8 payload[ZFCP_DBF_CT_PAYLOAD];
} __attribute__ ((packed));

struct zfcp_san_dbf_record_els {
	u8 ls_code;
	u32 payload_size;
#define ZFCP_DBF_ELS_PAYLOAD	32
#define ZFCP_DBF_ELS_MAX_PAYLOAD 1024
	u8 payload[ZFCP_DBF_ELS_PAYLOAD];
} __attribute__ ((packed));

struct zfcp_san_dbf_record {
	u8 tag[ZFCP_DBF_TAG_SIZE];
	u64 fsf_reqid;
	u32 fsf_seqno;
	u32 s_id;
	u32 d_id;
	union {
		struct zfcp_san_dbf_record_ct ct;
		struct zfcp_san_dbf_record_els els;
	} type;
} __attribute__ ((packed));

struct zfcp_scsi_dbf_record {
	u8 tag[ZFCP_DBF_TAG_SIZE];
	u8 tag2[ZFCP_DBF_TAG_SIZE];
	u32 scsi_id;
	u32 scsi_lun;
	u32 scsi_result;
	u64 scsi_cmnd;
	u64 scsi_serial;
#define ZFCP_DBF_SCSI_OPCODE	16
	u8 scsi_opcode[ZFCP_DBF_SCSI_OPCODE];
	u8 scsi_retries;
	u8 scsi_allowed;
	u64 fsf_reqid;
	u32 fsf_seqno;
	u64 fsf_issued;
	union {
		u64 old_fsf_reqid;
		struct {
			u8 rsp_validity;
			u8 rsp_scsi_status;
			u32 rsp_resid;
			u8 rsp_code;
#define ZFCP_DBF_SCSI_FCP_SNS_INFO	16
#define ZFCP_DBF_SCSI_MAX_FCP_SNS_INFO	256
			u32 sns_info_len;
			u8 sns_info[ZFCP_DBF_SCSI_FCP_SNS_INFO];
		} fcp;
	} type;
} __attribute__ ((packed));

/*
 * FC-FS stuff
 */
#define R_A_TOV				10 /* seconds */
#define ZFCP_ELS_TIMEOUT		(2 * R_A_TOV)

#define ZFCP_LS_RLS			0x0f
#define ZFCP_LS_ADISC			0x52
#define ZFCP_LS_RPS			0x56
#define ZFCP_LS_RSCN			0x61
#define ZFCP_LS_RNID			0x78

struct zfcp_ls_rjt_par {
	u8 action;
 	u8 reason_code;
 	u8 reason_expl;
 	u8 vendor_unique;
} __attribute__ ((packed));

struct zfcp_ls_adisc {
	u8		code;
	u8		field[3];
	u32		hard_nport_id;
	u64		wwpn;
	u64		wwnn;
	u32		nport_id;
} __attribute__ ((packed));

struct zfcp_ls_adisc_acc {
	u8		code;
	u8		field[3];
	u32		hard_nport_id;
	u64		wwpn;
	u64		wwnn;
	u32		nport_id;
} __attribute__ ((packed));

struct zfcp_rc_entry {
	u8 code;
	const char *description;
};

/*
 * FC-GS-2 stuff
 */
#define ZFCP_CT_REVISION		0x01
#define ZFCP_CT_DIRECTORY_SERVICE	0xFC
#define ZFCP_CT_NAME_SERVER		0x02
#define ZFCP_CT_SYNCHRONOUS		0x00
#define ZFCP_CT_GID_PN			0x0121
#define ZFCP_CT_MAX_SIZE		0x1020
#define ZFCP_CT_ACCEPT			0x8002
#define ZFCP_CT_REJECT			0x8001

/*
 * FC-GS-4 stuff
 */
#define ZFCP_CT_TIMEOUT			(3 * R_A_TOV)

/******************** LOGGING MACROS AND DEFINES *****************************/

/*
 * Logging may be applied on certain kinds of driver operations
 * independently. Additionally, different log-levels are supported for
 * each of these areas.
 */

#define ZFCP_NAME               "zfcp"

/* independent log areas */
#define ZFCP_LOG_AREA_OTHER	0
#define ZFCP_LOG_AREA_SCSI	1
#define ZFCP_LOG_AREA_FSF	2
#define ZFCP_LOG_AREA_CONFIG	3
#define ZFCP_LOG_AREA_CIO	4
#define ZFCP_LOG_AREA_QDIO	5
#define ZFCP_LOG_AREA_ERP	6
#define ZFCP_LOG_AREA_FC	7

/* log level values*/
#define ZFCP_LOG_LEVEL_NORMAL	0
#define ZFCP_LOG_LEVEL_INFO	1
#define ZFCP_LOG_LEVEL_DEBUG	2
#define ZFCP_LOG_LEVEL_TRACE	3

/*
 * this allows removal of logging code by the preprocessor
 * (the most detailed log level still to be compiled in is specified, 
 * higher log levels are removed)
 */
#define ZFCP_LOG_LEVEL_LIMIT	ZFCP_LOG_LEVEL_TRACE

/* get "loglevel" nibble assignment */
#define ZFCP_GET_LOG_VALUE(zfcp_lognibble) \
	       ((atomic_read(&zfcp_data.loglevel) >> (zfcp_lognibble<<2)) & 0xF)

/* set "loglevel" nibble */
#define ZFCP_SET_LOG_NIBBLE(value, zfcp_lognibble) \
	       (value << (zfcp_lognibble << 2))

/* all log-level defaults are combined to generate initial log-level */
#define ZFCP_LOG_LEVEL_DEFAULTS \
	(ZFCP_SET_LOG_NIBBLE(ZFCP_LOG_LEVEL_NORMAL, ZFCP_LOG_AREA_OTHER) | \
	 ZFCP_SET_LOG_NIBBLE(ZFCP_LOG_LEVEL_NORMAL, ZFCP_LOG_AREA_SCSI) | \
	 ZFCP_SET_LOG_NIBBLE(ZFCP_LOG_LEVEL_NORMAL, ZFCP_LOG_AREA_FSF) | \
	 ZFCP_SET_LOG_NIBBLE(ZFCP_LOG_LEVEL_NORMAL, ZFCP_LOG_AREA_CONFIG) | \
	 ZFCP_SET_LOG_NIBBLE(ZFCP_LOG_LEVEL_NORMAL, ZFCP_LOG_AREA_CIO) | \
	 ZFCP_SET_LOG_NIBBLE(ZFCP_LOG_LEVEL_NORMAL, ZFCP_LOG_AREA_QDIO) | \
	 ZFCP_SET_LOG_NIBBLE(ZFCP_LOG_LEVEL_NORMAL, ZFCP_LOG_AREA_ERP) | \
	 ZFCP_SET_LOG_NIBBLE(ZFCP_LOG_LEVEL_NORMAL, ZFCP_LOG_AREA_FC))

/* check whether we have the right level for logging */
#define ZFCP_LOG_CHECK(level) \
	((ZFCP_GET_LOG_VALUE(ZFCP_LOG_AREA)) >= level)

/* logging routine for zfcp */
#define _ZFCP_LOG(fmt, args...) \
	printk(KERN_ERR ZFCP_NAME": %s(%d): " fmt, __FUNCTION__, \
	       __LINE__ , ##args)

#define ZFCP_LOG(level, fmt, args...) \
do { \
	if (ZFCP_LOG_CHECK(level)) \
		_ZFCP_LOG(fmt, ##args); \
} while (0)
	
#if ZFCP_LOG_LEVEL_LIMIT < ZFCP_LOG_LEVEL_NORMAL
# define ZFCP_LOG_NORMAL(fmt, args...)
#else
# define ZFCP_LOG_NORMAL(fmt, args...) \
do { \
	if (ZFCP_LOG_CHECK(ZFCP_LOG_LEVEL_NORMAL)) \
		printk(KERN_ERR ZFCP_NAME": " fmt, ##args); \
} while (0)
#endif

#if ZFCP_LOG_LEVEL_LIMIT < ZFCP_LOG_LEVEL_INFO
# define ZFCP_LOG_INFO(fmt, args...)
#else
# define ZFCP_LOG_INFO(fmt, args...) \
do { \
	if (ZFCP_LOG_CHECK(ZFCP_LOG_LEVEL_INFO)) \
		printk(KERN_ERR ZFCP_NAME": " fmt, ##args); \
} while (0)
#endif

#if ZFCP_LOG_LEVEL_LIMIT < ZFCP_LOG_LEVEL_DEBUG
# define ZFCP_LOG_DEBUG(fmt, args...)
#else
# define ZFCP_LOG_DEBUG(fmt, args...) \
	ZFCP_LOG(ZFCP_LOG_LEVEL_DEBUG, fmt , ##args)
#endif

#if ZFCP_LOG_LEVEL_LIMIT < ZFCP_LOG_LEVEL_TRACE
# define ZFCP_LOG_TRACE(fmt, args...)
#else
# define ZFCP_LOG_TRACE(fmt, args...) \
	ZFCP_LOG(ZFCP_LOG_LEVEL_TRACE, fmt , ##args)
#endif

/*************** ADAPTER/PORT/UNIT AND FSF_REQ STATUS FLAGS ******************/

/* 
 * Note, the leftmost status byte is common among adapter, port 
 * and unit
 */
#define ZFCP_COMMON_FLAGS			0xfff00000

/* common status bits */
#define ZFCP_STATUS_COMMON_REMOVE		0x80000000
#define ZFCP_STATUS_COMMON_RUNNING		0x40000000
#define ZFCP_STATUS_COMMON_ERP_FAILED		0x20000000
#define ZFCP_STATUS_COMMON_UNBLOCKED		0x10000000
#define ZFCP_STATUS_COMMON_OPENING              0x08000000
#define ZFCP_STATUS_COMMON_OPEN                 0x04000000
#define ZFCP_STATUS_COMMON_CLOSING              0x02000000
#define ZFCP_STATUS_COMMON_ERP_INUSE		0x01000000
#define ZFCP_STATUS_COMMON_ACCESS_DENIED	0x00800000
#define ZFCP_STATUS_COMMON_ACCESS_BOXED		0x00400000

/* adapter status */
#define ZFCP_STATUS_ADAPTER_QDIOUP		0x00000002
#define ZFCP_STATUS_ADAPTER_REGISTERED		0x00000004
#define ZFCP_STATUS_ADAPTER_XCONFIG_OK		0x00000008
#define ZFCP_STATUS_ADAPTER_HOST_CON_INIT	0x00000010
#define ZFCP_STATUS_ADAPTER_ERP_THREAD_UP	0x00000020
#define ZFCP_STATUS_ADAPTER_ERP_THREAD_KILL	0x00000080
#define ZFCP_STATUS_ADAPTER_ERP_PENDING		0x00000100
#define ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED	0x00000200
#define ZFCP_STATUS_ADAPTER_XPORT_OK		0x00000800

/* FC-PH/FC-GS well-known address identifiers for generic services */
#define ZFCP_DID_MANAGEMENT_SERVICE		0xFFFFFA
#define ZFCP_DID_TIME_SERVICE			0xFFFFFB
#define ZFCP_DID_DIRECTORY_SERVICE		0xFFFFFC
#define ZFCP_DID_ALIAS_SERVICE			0xFFFFF8
#define ZFCP_DID_KEY_DISTRIBUTION_SERVICE	0xFFFFF7

/* remote port status */
#define ZFCP_STATUS_PORT_PHYS_OPEN		0x00000001
#define ZFCP_STATUS_PORT_DID_DID		0x00000002
#define ZFCP_STATUS_PORT_PHYS_CLOSING		0x00000004
#define ZFCP_STATUS_PORT_NO_WWPN		0x00000008
#define ZFCP_STATUS_PORT_NO_SCSI_ID		0x00000010
#define ZFCP_STATUS_PORT_INVALID_WWPN		0x00000020

/* for ports with well known addresses */
#define ZFCP_STATUS_PORT_WKA \
		(ZFCP_STATUS_PORT_NO_WWPN | \
		 ZFCP_STATUS_PORT_NO_SCSI_ID)

/* logical unit status */
#define ZFCP_STATUS_UNIT_NOTSUPPUNITRESET	0x00000001
#define ZFCP_STATUS_UNIT_TEMPORARY		0x00000002
#define ZFCP_STATUS_UNIT_SHARED			0x00000004
#define ZFCP_STATUS_UNIT_READONLY		0x00000008
#define ZFCP_STATUS_UNIT_REGISTERED		0x00000010

/* FSF request status (this does not have a common part) */
#define ZFCP_STATUS_FSFREQ_NOT_INIT		0x00000000
#define ZFCP_STATUS_FSFREQ_POOL  		0x00000001
#define ZFCP_STATUS_FSFREQ_TASK_MANAGEMENT	0x00000002
#define ZFCP_STATUS_FSFREQ_COMPLETED		0x00000004
#define ZFCP_STATUS_FSFREQ_ERROR		0x00000008
#define ZFCP_STATUS_FSFREQ_CLEANUP		0x00000010
#define ZFCP_STATUS_FSFREQ_ABORTING		0x00000020
#define ZFCP_STATUS_FSFREQ_ABORTSUCCEEDED	0x00000040
#define ZFCP_STATUS_FSFREQ_ABORTNOTNEEDED       0x00000080
#define ZFCP_STATUS_FSFREQ_ABORTED              0x00000100
#define ZFCP_STATUS_FSFREQ_TMFUNCFAILED         0x00000200
#define ZFCP_STATUS_FSFREQ_TMFUNCNOTSUPP        0x00000400
#define ZFCP_STATUS_FSFREQ_RETRY                0x00000800
#define ZFCP_STATUS_FSFREQ_DISMISSED            0x00001000

/*********************** ERROR RECOVERY PROCEDURE DEFINES ********************/

#define ZFCP_MAX_ERPS                   3

#define ZFCP_ERP_FSFREQ_TIMEOUT		(30 * HZ)
#define ZFCP_ERP_MEMWAIT_TIMEOUT	HZ

#define ZFCP_STATUS_ERP_TIMEDOUT	0x10000000
#define ZFCP_STATUS_ERP_CLOSE_ONLY	0x01000000
#define ZFCP_STATUS_ERP_DISMISSING	0x00100000
#define ZFCP_STATUS_ERP_DISMISSED	0x00200000
#define ZFCP_STATUS_ERP_LOWMEM		0x00400000

#define ZFCP_ERP_STEP_UNINITIALIZED	0x00000000
#define ZFCP_ERP_STEP_FSF_XCONFIG	0x00000001
#define ZFCP_ERP_STEP_PHYS_PORT_CLOSING	0x00000010
#define ZFCP_ERP_STEP_PORT_CLOSING	0x00000100
#define ZFCP_ERP_STEP_NAMESERVER_OPEN	0x00000200
#define ZFCP_ERP_STEP_NAMESERVER_LOOKUP	0x00000400
#define ZFCP_ERP_STEP_PORT_OPENING	0x00000800
#define ZFCP_ERP_STEP_UNIT_CLOSING	0x00001000
#define ZFCP_ERP_STEP_UNIT_OPENING	0x00002000

/* Ordered by escalation level (necessary for proper erp-code operation) */
#define ZFCP_ERP_ACTION_REOPEN_ADAPTER		0x4
#define ZFCP_ERP_ACTION_REOPEN_PORT_FORCED	0x3
#define ZFCP_ERP_ACTION_REOPEN_PORT		0x2
#define ZFCP_ERP_ACTION_REOPEN_UNIT		0x1

#define ZFCP_ERP_ACTION_RUNNING			0x1
#define ZFCP_ERP_ACTION_READY			0x2

#define ZFCP_ERP_SUCCEEDED	0x0
#define ZFCP_ERP_FAILED		0x1
#define ZFCP_ERP_CONTINUES	0x2
#define ZFCP_ERP_EXIT		0x3
#define ZFCP_ERP_DISMISSED	0x4
#define ZFCP_ERP_NOMEM		0x5


/******************** CFDC SPECIFIC STUFF *****************************/

/* Firewall data channel sense data record */
struct zfcp_cfdc_sense_data {
	u32 signature;           /* Request signature */
	u32 devno;               /* FCP adapter device number */
	u32 command;             /* Command code */
	u32 fsf_status;          /* FSF request status and status qualifier */
	u8  fsf_status_qual[FSF_STATUS_QUALIFIER_SIZE];
	u8  payloads[256];       /* Access conflicts list */
	u8  control_file[0];     /* Access control table */
};

#define ZFCP_CFDC_SIGNATURE			0xCFDCACDF

#define ZFCP_CFDC_CMND_DOWNLOAD_NORMAL		0x00010001
#define ZFCP_CFDC_CMND_DOWNLOAD_FORCE		0x00010101
#define ZFCP_CFDC_CMND_FULL_ACCESS		0x00000201
#define ZFCP_CFDC_CMND_RESTRICTED_ACCESS	0x00000401
#define ZFCP_CFDC_CMND_UPLOAD			0x00010002

#define ZFCP_CFDC_DOWNLOAD			0x00000001
#define ZFCP_CFDC_UPLOAD			0x00000002
#define ZFCP_CFDC_WITH_CONTROL_FILE		0x00010000

#define ZFCP_CFDC_DEV_NAME			"zfcp_cfdc"
#define ZFCP_CFDC_DEV_MAJOR			MISC_MAJOR
#define ZFCP_CFDC_DEV_MINOR			MISC_DYNAMIC_MINOR

#define ZFCP_CFDC_MAX_CONTROL_FILE_SIZE		127 * 1024

/************************* STRUCTURE DEFINITIONS *****************************/

struct zfcp_fsf_req;

/* holds various memory pools of an adapter */
struct zfcp_adapter_mempool {
	mempool_t *fsf_req_erp;
	mempool_t *fsf_req_scsi;
	mempool_t *fsf_req_abort;
	mempool_t *fsf_req_status_read;
	mempool_t *data_status_read;
	mempool_t *data_gid_pn;
};

/*
 * header for CT_IU
 */
struct ct_hdr {
	u8 revision;		// 0x01
	u8 in_id[3];		// 0x00
	u8 gs_type;		// 0xFC	Directory Service
	u8 gs_subtype;		// 0x02	Name Server
	u8 options;		// 0x00 single bidirectional exchange
	u8 reserved0;
	u16 cmd_rsp_code;	// 0x0121 GID_PN, or 0x0100 GA_NXT
	u16 max_res_size;	// <= (4096 - 16) / 4
	u8 reserved1;
	u8 reason_code;
	u8 reason_code_expl;
	u8 vendor_unique;
} __attribute__ ((packed));

/* nameserver request CT_IU -- for requests where
 * a port name is required */
struct ct_iu_gid_pn_req {
	struct ct_hdr header;
	wwn_t wwpn;
} __attribute__ ((packed));

/* FS_ACC IU and data unit for GID_PN nameserver request */
struct ct_iu_gid_pn_resp {
	struct ct_hdr header;
	u32 d_id;
} __attribute__ ((packed));

typedef void (*zfcp_send_ct_handler_t)(unsigned long);

/**
 * struct zfcp_send_ct - used to pass parameters to function zfcp_fsf_send_ct
 * @port: port where the request is sent to
 * @req: scatter-gather list for request
 * @resp: scatter-gather list for response
 * @req_count: number of elements in request scatter-gather list
 * @resp_count: number of elements in response scatter-gather list
 * @handler: handler function (called for response to the request)
 * @handler_data: data passed to handler function
 * @pool: pointer to memory pool for ct request structure
 * @timeout: FSF timeout for this request
 * @timer: timer (e.g. for request initiated by erp)
 * @completion: completion for synchronization purposes
 * @status: used to pass error status to calling function
 */
struct zfcp_send_ct {
	struct zfcp_port *port;
	struct scatterlist *req;
	struct scatterlist *resp;
	unsigned int req_count;
	unsigned int resp_count;
	zfcp_send_ct_handler_t handler;
	unsigned long handler_data;
	mempool_t *pool;
	int timeout;
	struct timer_list *timer;
	struct completion *completion;
	int status;
};

/* used for name server requests in error recovery */
struct zfcp_gid_pn_data {
	struct zfcp_send_ct ct;
	struct scatterlist req;
	struct scatterlist resp;
	struct ct_iu_gid_pn_req ct_iu_req;
	struct ct_iu_gid_pn_resp ct_iu_resp;
        struct zfcp_port *port;
};

typedef void (*zfcp_send_els_handler_t)(unsigned long);

/**
 * struct zfcp_send_els - used to pass parameters to function zfcp_fsf_send_els
 * @adapter: adapter where request is sent from
 * @port: port where ELS is destinated (port reference count has to be increased)
 * @d_id: destiniation id of port where request is sent to
 * @req: scatter-gather list for request
 * @resp: scatter-gather list for response
 * @req_count: number of elements in request scatter-gather list
 * @resp_count: number of elements in response scatter-gather list
 * @handler: handler function (called for response to the request)
 * @handler_data: data passed to handler function
 * @timer: timer (e.g. for request initiated by erp)
 * @completion: completion for synchronization purposes
 * @ls_code: hex code of ELS command
 * @status: used to pass error status to calling function
 */
struct zfcp_send_els {
	struct zfcp_adapter *adapter;
	struct zfcp_port *port;
	u32 d_id;
	struct scatterlist *req;
	struct scatterlist *resp;
	unsigned int req_count;
	unsigned int resp_count;
	zfcp_send_els_handler_t handler;
	unsigned long handler_data;
	struct timer_list *timer;
	struct completion *completion;
	int ls_code;
	int status;
};

struct zfcp_qdio_queue {
	struct qdio_buffer *buffer[QDIO_MAX_BUFFERS_PER_Q]; /* SBALs */
	u8		   free_index;	      /* index of next free bfr
						 in queue (free_count>0) */
	atomic_t           free_count;	      /* number of free buffers
						 in queue */
	rwlock_t	   queue_lock;	      /* lock for operations on queue */
        int                distance_from_int; /* SBALs used since PCI indication
						 was last set */
};

struct zfcp_erp_action {
	struct list_head list;
	int action;	              /* requested action code */
	struct zfcp_adapter *adapter; /* device which should be recovered */
	struct zfcp_port *port;
	struct zfcp_unit *unit;
	volatile u32 status;	      /* recovery status */
	u32 step;	              /* active step of this erp action */
	struct zfcp_fsf_req *fsf_req; /* fsf request currently pending
					 for this action */
	struct timer_list timer;
};


struct zfcp_adapter {
	struct list_head	list;              /* list of adapters */
	atomic_t                refcount;          /* reference count */
	wait_queue_head_t	remove_wq;         /* can be used to wait for
						      refcount drop to zero */
	wwn_t			peer_wwnn;	   /* P2P peer WWNN */
	wwn_t			peer_wwpn;	   /* P2P peer WWPN */
	u32			peer_d_id;	   /* P2P peer D_ID */
	struct ccw_device       *ccw_device;	   /* S/390 ccw device */
	u32			hydra_version;	   /* Hydra version */
	u32			fsf_lic_version;
	u32			adapter_features;  /* FCP channel features */
	u32			connection_features; /* host connection features */
        u32			hardware_version;  /* of FCP channel */
	struct Scsi_Host	*scsi_host;	   /* Pointer to mid-layer */
	struct list_head	port_list_head;	   /* remote port list */
	struct list_head        port_remove_lh;    /* head of ports to be
						      removed */
	u32			ports;	           /* number of remote ports */
        struct timer_list       scsi_er_timer;     /* SCSI err recovery watch */
	struct list_head	fsf_req_list_head; /* head of FSF req list */
	spinlock_t		fsf_req_list_lock; /* lock for ops on list of
						      FSF requests */
        atomic_t       		fsf_reqs_active;   /* # active FSF reqs */
	struct zfcp_qdio_queue	request_queue;	   /* request queue */
	u32			fsf_req_seq_no;	   /* FSF cmnd seq number */
	wait_queue_head_t	request_wq;	   /* can be used to wait for
						      more avaliable SBALs */
	struct zfcp_qdio_queue	response_queue;	   /* response queue */
	rwlock_t		abort_lock;        /* Protects against SCSI
						      stack abort/command
						      completion races */
	u16			status_read_failed; /* # failed status reads */
	atomic_t		status;	           /* status of this adapter */
	struct list_head	erp_ready_head;	   /* error recovery for this
						      adapter/devices */
	struct list_head	erp_running_head;
	rwlock_t		erp_lock;
	struct semaphore	erp_ready_sem;
	wait_queue_head_t	erp_thread_wqh;
	wait_queue_head_t	erp_done_wqh;
	struct zfcp_erp_action	erp_action;	   /* pending error recovery */
        atomic_t                erp_counter;
	u32			erp_total_count;   /* total nr of enqueued erp
						      actions */
	u32			erp_low_mem_count; /* nr of erp actions waiting
						      for memory */
	struct zfcp_port	*nameserver_port;  /* adapter's nameserver */
	debug_info_t		*erp_dbf;
	debug_info_t		*hba_dbf;
	debug_info_t		*san_dbf;          /* debug feature areas */
	debug_info_t		*scsi_dbf;
	spinlock_t		erp_dbf_lock;
	spinlock_t		hba_dbf_lock;
	spinlock_t		san_dbf_lock;
	spinlock_t		scsi_dbf_lock;
	struct zfcp_erp_dbf_record	erp_dbf_buf;
	struct zfcp_hba_dbf_record	hba_dbf_buf;
	struct zfcp_san_dbf_record	san_dbf_buf;
	struct zfcp_scsi_dbf_record	scsi_dbf_buf;
	struct zfcp_adapter_mempool	pool;      /* Adapter memory pools */
	struct qdio_initialize  qdio_init_data;    /* for qdio_establish */
	struct device           generic_services;  /* directory for WKA ports */
	struct fc_host_statistics *fc_stats;
	struct fsf_qtcb_bottom_port *stats_reset_data;
	unsigned long		stats_reset;
};

/*
 * the struct device sysfs_device must be at the beginning of this structure.
 * pointer to struct device is used to free port structure in release function
 * of the device. don't change!
 */
struct zfcp_port {
	struct device          sysfs_device;   /* sysfs device */
	struct fc_rport        *rport;         /* rport of fc transport class */
	struct list_head       list;	       /* list of remote ports */
	atomic_t               refcount;       /* reference count */
	wait_queue_head_t      remove_wq;      /* can be used to wait for
						  refcount drop to zero */
	struct zfcp_adapter    *adapter;       /* adapter used to access port */
	struct list_head       unit_list_head; /* head of logical unit list */
	struct list_head       unit_remove_lh; /* head of luns to be removed
						  list */
	u32		       units;	       /* # of logical units in list */
	atomic_t	       status;	       /* status of this remote port */
	wwn_t		       wwnn;	       /* WWNN if known */
	wwn_t		       wwpn;	       /* WWPN */
	u32		       d_id;	       /* D_ID */
	u32		       handle;	       /* handle assigned by FSF */
	struct zfcp_erp_action erp_action;     /* pending error recovery */
        atomic_t               erp_counter;
	u32                    maxframe_size;
	u32                    supported_classes;
};

/* the struct device sysfs_device must be at the beginning of this structure.
 * pointer to struct device is used to free unit structure in release function
 * of the device. don't change!
 */
struct zfcp_unit {
	struct device          sysfs_device;   /* sysfs device */
	struct list_head       list;	       /* list of logical units */
	atomic_t               refcount;       /* reference count */
	wait_queue_head_t      remove_wq;      /* can be used to wait for
						  refcount drop to zero */
	struct zfcp_port       *port;	       /* remote port of unit */
	atomic_t	       status;	       /* status of this logical unit */
	unsigned int	       scsi_lun;       /* own SCSI LUN */
	fcp_lun_t	       fcp_lun;	       /* own FCP_LUN */
	u32		       handle;	       /* handle assigned by FSF */
        struct scsi_device     *device;        /* scsi device struct pointer */
	struct zfcp_erp_action erp_action;     /* pending error recovery */
        atomic_t               erp_counter;
};

/* FSF request */
struct zfcp_fsf_req {
	struct list_head       list;	       /* list of FSF requests */
	struct zfcp_adapter    *adapter;       /* adapter request belongs to */
	u8		       sbal_number;    /* nr of SBALs free for use */
	u8		       sbal_first;     /* first SBAL for this request */
	u8		       sbal_last;      /* last possible SBAL for
						  this reuest */
	u8		       sbal_curr;      /* current SBAL during creation
						  of request */
	u8		       sbale_curr;     /* current SBALE during creation
						  of request */
	wait_queue_head_t      completion_wq;  /* can be used by a routine
						  to wait for completion */
	volatile u32	       status;	       /* status of this request */
	u32		       fsf_command;    /* FSF Command copy */
	struct fsf_qtcb	       *qtcb;	       /* address of associated QTCB */
	u32		       seq_no;         /* Sequence number of request */
        unsigned long          data;           /* private data of request */ 
	struct zfcp_erp_action *erp_action;    /* used if this request is
						  issued on behalf of erp */
	mempool_t	       *pool;	       /* used if request was alloacted
						  from emergency pool */
	unsigned long long     issued;         /* request sent time (STCK) */
	struct zfcp_unit       *unit;
};

typedef void zfcp_fsf_req_handler_t(struct zfcp_fsf_req*);

/* driver data */
struct zfcp_data {
	struct scsi_host_template scsi_host_template;
        atomic_t                status;             /* Module status flags */
	struct list_head	adapter_list_head;  /* head of adapter list */
	struct list_head	adapter_remove_lh;  /* head of adapters to be
						       removed */
	u32			adapters;	    /* # of adapters in list */
	rwlock_t                config_lock;        /* serialises changes
						       to adapter/port/unit
						       lists */
	struct semaphore        config_sema;        /* serialises configuration
						       changes */
	atomic_t		loglevel;            /* current loglevel */
	char                    init_busid[BUS_ID_SIZE];
	wwn_t                   init_wwpn;
	fcp_lun_t               init_fcp_lun;
	char 			*driver_version;
};

/**
 * struct zfcp_sg_list - struct describing a scatter-gather list
 * @sg: pointer to array of (struct scatterlist)
 * @count: number of elements in scatter-gather list
 */
struct zfcp_sg_list {
	struct scatterlist *sg;
	unsigned int count;
};

/* number of elements for various memory pools */
#define ZFCP_POOL_FSF_REQ_ERP_NR	1
#define ZFCP_POOL_FSF_REQ_SCSI_NR	1
#define ZFCP_POOL_FSF_REQ_ABORT_NR	1
#define ZFCP_POOL_STATUS_READ_NR	ZFCP_STATUS_READS_RECOM
#define ZFCP_POOL_DATA_GID_PN_NR	1

/* struct used by memory pools for fsf_requests */
struct zfcp_fsf_req_pool_element {
	struct zfcp_fsf_req fsf_req;
	struct fsf_qtcb qtcb;
};

/********************** ZFCP SPECIFIC DEFINES ********************************/

#define ZFCP_REQ_AUTO_CLEANUP	0x00000002
#define ZFCP_WAIT_FOR_SBAL	0x00000004
#define ZFCP_REQ_NO_QTCB	0x00000008

#define ZFCP_SET                0x00000100
#define ZFCP_CLEAR              0x00000200

#ifndef atomic_test_mask
#define atomic_test_mask(mask, target) \
           ((atomic_read(target) & mask) == mask)
#endif

extern void _zfcp_hex_dump(char *, int);
#define ZFCP_HEX_DUMP(level, addr, count) \
		if (ZFCP_LOG_CHECK(level)) { \
			_zfcp_hex_dump(addr, count); \
		}

#define zfcp_get_busid_by_adapter(adapter) (adapter->ccw_device->dev.bus_id)
#define zfcp_get_busid_by_port(port) (zfcp_get_busid_by_adapter(port->adapter))
#define zfcp_get_busid_by_unit(unit) (zfcp_get_busid_by_port(unit->port))

/*
 *  functions needed for reference/usage counting
 */

static inline void
zfcp_unit_get(struct zfcp_unit *unit)
{
	atomic_inc(&unit->refcount);
}

static inline void
zfcp_unit_put(struct zfcp_unit *unit)
{
	if (atomic_dec_return(&unit->refcount) == 0)
		wake_up(&unit->remove_wq);
}

static inline void
zfcp_unit_wait(struct zfcp_unit *unit)
{
	wait_event(unit->remove_wq, atomic_read(&unit->refcount) == 0);
}

static inline void
zfcp_port_get(struct zfcp_port *port)
{
	atomic_inc(&port->refcount);
}

static inline void
zfcp_port_put(struct zfcp_port *port)
{
	if (atomic_dec_return(&port->refcount) == 0)
		wake_up(&port->remove_wq);
}

static inline void
zfcp_port_wait(struct zfcp_port *port)
{
	wait_event(port->remove_wq, atomic_read(&port->refcount) == 0);
}

static inline void
zfcp_adapter_get(struct zfcp_adapter *adapter)
{
	atomic_inc(&adapter->refcount);
}

static inline void
zfcp_adapter_put(struct zfcp_adapter *adapter)
{
	if (atomic_dec_return(&adapter->refcount) == 0)
		wake_up(&adapter->remove_wq);
}

static inline void
zfcp_adapter_wait(struct zfcp_adapter *adapter)
{
	wait_event(adapter->remove_wq, atomic_read(&adapter->refcount) == 0);
}

#endif /* ZFCP_DEF_H */
