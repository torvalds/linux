/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef __SCSI_H__
#define __SCSI_H__

#include <protocol/types.h>

#pragma pack(1)

/*
 * generic SCSI cdb definition
 */
#define SCSI_MAX_CDBLEN     16
struct scsi_cdb_s{
	u8         scsi_cdb[SCSI_MAX_CDBLEN];
};

/*
 * scsi lun serial number definition
 */
#define SCSI_LUN_SN_LEN     32
struct scsi_lun_sn_s{
	u8         lun_sn[SCSI_LUN_SN_LEN];
};

/*
 * SCSI Direct Access Commands
 */
enum {
	SCSI_OP_TEST_UNIT_READY		= 0x00,
	SCSI_OP_REQUEST_SENSE		= 0x03,
	SCSI_OP_FORMAT_UNIT		= 0x04,
	SCSI_OP_READ6			= 0x08,
	SCSI_OP_WRITE6			= 0x0A,
	SCSI_OP_WRITE_FILEMARKS		= 0x10,
	SCSI_OP_INQUIRY			= 0x12,
	SCSI_OP_MODE_SELECT6		= 0x15,
	SCSI_OP_RESERVE6		= 0x16,
	SCSI_OP_RELEASE6		= 0x17,
	SCSI_OP_MODE_SENSE6		= 0x1A,
	SCSI_OP_START_STOP_UNIT		= 0x1B,
	SCSI_OP_SEND_DIAGNOSTIC		= 0x1D,
	SCSI_OP_READ_CAPACITY		= 0x25,
	SCSI_OP_READ10			= 0x28,
	SCSI_OP_WRITE10			= 0x2A,
	SCSI_OP_VERIFY10		= 0x2F,
	SCSI_OP_READ_DEFECT_DATA	= 0x37,
	SCSI_OP_LOG_SELECT		= 0x4C,
	SCSI_OP_LOG_SENSE		= 0x4D,
	SCSI_OP_MODE_SELECT10		= 0x55,
	SCSI_OP_RESERVE10		= 0x56,
	SCSI_OP_RELEASE10		= 0x57,
	SCSI_OP_MODE_SENSE10		= 0x5A,
	SCSI_OP_PER_RESERVE_IN		= 0x5E,
	SCSI_OP_PER_RESERVE_OUR		= 0x5E,
	SCSI_OP_READ16			= 0x88,
	SCSI_OP_WRITE16			= 0x8A,
	SCSI_OP_VERIFY16		= 0x8F,
	SCSI_OP_READ_CAPACITY16		= 0x9E,
	SCSI_OP_REPORT_LUNS		= 0xA0,
	SCSI_OP_READ12			= 0xA8,
	SCSI_OP_WRITE12			= 0xAA,
	SCSI_OP_UNDEF			= 0xFF,
};

/*
 * SCSI START_STOP_UNIT command
 */
struct scsi_start_stop_unit_s{
	u8         opcode;
#ifdef __BIGENDIAN
	u8         lun:3;
	u8         reserved1:4;
	u8         immed:1;
#else
	u8         immed:1;
	u8         reserved1:4;
	u8         lun:3;
#endif
	u8         reserved2;
	u8         reserved3;
#ifdef __BIGENDIAN
	u8         power_conditions:4;
	u8         reserved4:2;
	u8         loEj:1;
	u8         start:1;
#else
	u8         start:1;
	u8         loEj:1;
	u8         reserved4:2;
	u8         power_conditions:4;
#endif
	u8         control;
};

/*
 * SCSI SEND_DIAGNOSTIC command
 */
struct scsi_send_diagnostic_s{
	u8         opcode;
#ifdef __BIGENDIAN
	u8         self_test_code:3;
	u8         pf:1;
	u8         reserved1:1;
	u8         self_test:1;
	u8         dev_offl:1;
	u8         unit_offl:1;
#else
	u8         unit_offl:1;
	u8         dev_offl:1;
	u8         self_test:1;
	u8         reserved1:1;
	u8         pf:1;
	u8         self_test_code:3;
#endif
	u8         reserved2;

	u8         param_list_length[2];	/* MSB first */
	u8         control;

};

/*
 * SCSI READ10/WRITE10 commands
 */
struct scsi_rw10_s{
	u8         opcode;
#ifdef __BIGENDIAN
	u8         lun:3;
	u8         dpo:1;	/* Disable Page Out */
	u8         fua:1;	/* Force Unit Access */
	u8         reserved1:2;
	u8         rel_adr:1;	/* relative address */
#else
	u8         rel_adr:1;
	u8         reserved1:2;
	u8         fua:1;
	u8         dpo:1;
	u8         lun:3;
#endif
	u8         lba0;	/* logical block address - MSB */
	u8         lba1;
	u8         lba2;
	u8         lba3;	/* LSB */
	u8         reserved3;
	u8         xfer_length0;	/* transfer length in blocks - MSB */
	u8         xfer_length1;	/* LSB */
	u8         control;
};

#define SCSI_CDB10_GET_LBA(cdb)                     \
    (((cdb)->lba0 << 24) | ((cdb)->lba1 << 16) |    \
     ((cdb)->lba2 << 8) | (cdb)->lba3)

#define SCSI_CDB10_SET_LBA(cdb, lba) {      \
    (cdb)->lba0 = lba >> 24;            \
    (cdb)->lba1 = (lba >> 16) & 0xFF;   \
    (cdb)->lba2 = (lba >> 8) & 0xFF;    \
    (cdb)->lba3 = lba & 0xFF;           \
}

#define SCSI_CDB10_GET_TL(cdb)  \
    ((cdb)->xfer_length0 << 8 | (cdb)->xfer_length1)
#define SCSI_CDB10_SET_TL(cdb, tl) {      \
    (cdb)->xfer_length0 = tl >> 8;       \
    (cdb)->xfer_length1 = tl & 0xFF;     \
}

/*
 * SCSI READ6/WRITE6 commands
 */
struct scsi_rw6_s{
	u8         opcode;
#ifdef __BIGENDIAN
	u8         lun:3;
	u8         lba0:5;		/* MSb */
#else
	u8         lba0:5;		/* MSb */
	u8         lun:3;
#endif
	u8         lba1;
	u8         lba2;		/* LSB */
	u8         xfer_length;
	u8         control;
};

#define SCSI_TAPE_CDB6_GET_TL(cdb)              \
    (((cdb)->tl0 << 16) | ((cdb)->tl1 << 8) | (cdb)->tl2)

#define SCSI_TAPE_CDB6_SET_TL(cdb, tl) {      \
    (cdb)->tl0 = tl >> 16;            \
    (cdb)->tl1 = (tl >> 8) & 0xFF;    \
    (cdb)->tl2 = tl & 0xFF;           \
}

/*
 * SCSI sequential (TAPE) wrtie command
 */
struct scsi_tape_wr_s{
	u8         opcode;
#ifdef __BIGENDIAN
	u8         rsvd:7;
	u8         fixed:1;	/* MSb */
#else
	u8         fixed:1;	/* MSb */
	u8         rsvd:7;
#endif
	u8         tl0;		/* Msb */
	u8         tl1;
	u8         tl2;		/* Lsb */

	u8         control;
};

#define SCSI_CDB6_GET_LBA(cdb)              \
    (((cdb)->lba0 << 16) | ((cdb)->lba1 << 8) | (cdb)->lba2)

#define SCSI_CDB6_SET_LBA(cdb, lba) {      \
    (cdb)->lba0 = lba >> 16;            \
    (cdb)->lba1 = (lba >> 8) & 0xFF;    \
    (cdb)->lba2 = lba & 0xFF;           \
}

#define SCSI_CDB6_GET_TL(cdb) ((cdb)->xfer_length)
#define SCSI_CDB6_SET_TL(cdb, tl) {      \
    (cdb)->xfer_length = tl;         \
}

/*
 * SCSI sense data format
 */
struct scsi_sense_s{
#ifdef __BIGENDIAN
	u8         valid:1;
	u8         rsp_code:7;
#else
	u8         rsp_code:7;
	u8         valid:1;
#endif
	u8         seg_num;
#ifdef __BIGENDIAN
	u8         file_mark:1;
	u8         eom:1;		/* end of media */
	u8         ili:1;		/* incorrect length indicator */
	u8         reserved:1;
	u8         sense_key:4;
#else
	u8         sense_key:4;
	u8         reserved:1;
	u8         ili:1;		/* incorrect length indicator */
	u8         eom:1;		/* end of media */
	u8         file_mark:1;
#endif
	u8         information[4];	/* device-type or command specific info
					 */
	u8         add_sense_length;
					/* additional sense length */
	u8         command_info[4];/* command specific information
						 */
	u8         asc;		/* additional sense code */
	u8         ascq;		/* additional sense code qualifier */
	u8         fru_code;	/* field replaceable unit code */
#ifdef __BIGENDIAN
	u8         sksv:1;		/* sense key specific valid */
	u8         c_d:1;		/* command/data bit */
	u8         res1:2;
	u8         bpv:1;		/* bit pointer valid */
	u8         bpointer:3;	/* bit pointer */
#else
	u8         bpointer:3;	/* bit pointer */
	u8         bpv:1;		/* bit pointer valid */
	u8         res1:2;
	u8         c_d:1;		/* command/data bit */
	u8         sksv:1;		/* sense key specific valid */
#endif
	u8         fpointer[2];	/* field pointer */
};

#define SCSI_SENSE_CUR_ERR          0x70
#define SCSI_SENSE_DEF_ERR          0x71

/*
 * SCSI sense key values
 */
#define SCSI_SK_NO_SENSE        0x0
#define SCSI_SK_REC_ERR         0x1	/* recovered error */
#define SCSI_SK_NOT_READY       0x2
#define SCSI_SK_MED_ERR         0x3	/* medium error */
#define SCSI_SK_HW_ERR          0x4	/* hardware error */
#define SCSI_SK_ILLEGAL_REQ     0x5
#define SCSI_SK_UNIT_ATT        0x6	/* unit attention */
#define SCSI_SK_DATA_PROTECT    0x7
#define SCSI_SK_BLANK_CHECK     0x8
#define SCSI_SK_VENDOR_SPEC     0x9
#define SCSI_SK_COPY_ABORTED    0xA
#define SCSI_SK_ABORTED_CMND    0xB
#define SCSI_SK_VOL_OVERFLOW    0xD
#define SCSI_SK_MISCOMPARE      0xE

/*
 * SCSI additional sense codes
 */
#define SCSI_ASC_NO_ADD_SENSE           0x00
#define SCSI_ASC_LUN_NOT_READY          0x04
#define SCSI_ASC_LUN_COMMUNICATION      0x08
#define SCSI_ASC_WRITE_ERROR            0x0C
#define SCSI_ASC_INVALID_CMND_CODE      0x20
#define SCSI_ASC_BAD_LBA                0x21
#define SCSI_ASC_INVALID_FIELD_IN_CDB   0x24
#define SCSI_ASC_LUN_NOT_SUPPORTED      0x25
#define SCSI_ASC_LUN_WRITE_PROTECT      0x27
#define SCSI_ASC_POWERON_BDR            0x29	/* power on reset, bus reset,
						 * bus device reset
						 */
#define SCSI_ASC_PARAMS_CHANGED         0x2A
#define SCSI_ASC_CMND_CLEARED_BY_A_I    0x2F
#define SCSI_ASC_SAVING_PARAM_NOTSUPP   0x39
#define SCSI_ASC_TOCC                   0x3F	/* target operating condtions
						 * changed
						 */
#define SCSI_ASC_PARITY_ERROR           0x47
#define SCSI_ASC_CMND_PHASE_ERROR       0x4A
#define SCSI_ASC_DATA_PHASE_ERROR       0x4B
#define SCSI_ASC_VENDOR_SPEC            0x7F

/*
 * SCSI additional sense code qualifiers
 */
#define SCSI_ASCQ_CAUSE_NOT_REPORT      0x00
#define SCSI_ASCQ_BECOMING_READY        0x01
#define SCSI_ASCQ_INIT_CMD_REQ          0x02
#define SCSI_ASCQ_FORMAT_IN_PROGRESS    0x04
#define SCSI_ASCQ_OPERATION_IN_PROGRESS 0x07
#define SCSI_ASCQ_SELF_TEST_IN_PROGRESS 0x09
#define SCSI_ASCQ_WR_UNEXP_UNSOL_DATA   0x0C
#define SCSI_ASCQ_WR_NOTENG_UNSOL_DATA  0x0D

#define SCSI_ASCQ_LBA_OUT_OF_RANGE      0x00
#define SCSI_ASCQ_INVALID_ELEMENT_ADDR  0x01

#define SCSI_ASCQ_LUN_WRITE_PROTECTED       0x00
#define SCSI_ASCQ_LUN_HW_WRITE_PROTECTED    0x01
#define SCSI_ASCQ_LUN_SW_WRITE_PROTECTED    0x02

#define SCSI_ASCQ_POR   0x01	/* power on reset */
#define SCSI_ASCQ_SBR   0x02	/* scsi bus reset */
#define SCSI_ASCQ_BDR   0x03	/* bus device reset */
#define SCSI_ASCQ_DIR   0x04	/* device internal reset */

#define SCSI_ASCQ_MODE_PARAMS_CHANGED       0x01
#define SCSI_ASCQ_LOG_PARAMS_CHANGED        0x02
#define SCSI_ASCQ_RESERVATIONS_PREEMPTED    0x03
#define SCSI_ASCQ_RESERVATIONS_RELEASED     0x04
#define SCSI_ASCQ_REGISTRATIONS_PREEMPTED   0x05

#define SCSI_ASCQ_MICROCODE_CHANGED 0x01
#define SCSI_ASCQ_CHANGED_OPER_COND 0x02
#define SCSI_ASCQ_INQ_CHANGED       0x03	/* inquiry data changed */
#define SCSI_ASCQ_DI_CHANGED        0x05	/* device id changed */
#define SCSI_ASCQ_RL_DATA_CHANGED   0x0E	/* report luns data changed */

#define SCSI_ASCQ_DP_CRC_ERR            0x01	/* data phase crc error */
#define SCSI_ASCQ_DP_SCSI_PARITY_ERR    0x02	/* data phase scsi parity error
						 */
#define SCSI_ASCQ_IU_CRC_ERR            0x03	/* information unit crc error */
#define SCSI_ASCQ_PROTO_SERV_CRC_ERR    0x05

#define SCSI_ASCQ_LUN_TIME_OUT          0x01

/* ------------------------------------------------------------
 * SCSI INQUIRY
 * ------------------------------------------------------------*/

struct scsi_inquiry_s{
	u8         opcode;
#ifdef __BIGENDIAN
	u8         lun:3;
	u8         reserved1:3;
	u8         cmd_dt:1;
	u8         evpd:1;
#else
	u8         evpd:1;
	u8         cmd_dt:1;
	u8         reserved1:3;
	u8         lun:3;
#endif
	u8         page_code;
	u8         reserved2;
	u8         alloc_length;
	u8         control;
};

struct scsi_inquiry_vendor_s{
	u8         vendor_id[8];
};

struct scsi_inquiry_prodid_s{
	u8         product_id[16];
};

struct scsi_inquiry_prodrev_s{
	u8         product_rev[4];
};

struct scsi_inquiry_data_s{
#ifdef __BIGENDIAN
	u8         peripheral_qual:3;	/* peripheral qualifier */
	u8         device_type:5;		/* peripheral device type */

	u8         rmb:1;			/* removable medium bit */
	u8         device_type_mod:7;	/* device type modifier */

	u8         version;

	u8         aenc:1;		/* async event notification capability
					 */
	u8         trm_iop:1;	/* terminate I/O process */
	u8         norm_aca:1;	/* normal ACA supported */
	u8         hi_support:1;	/* SCSI-3: supports REPORT LUNS */
	u8         rsp_data_format:4;

	u8         additional_len;
	u8         sccs:1;
	u8         reserved1:7;

	u8         reserved2:1;
	u8         enc_serv:1;	/* enclosure service component */
	u8         reserved3:1;
	u8         multi_port:1;	/* multi-port device */
	u8         m_chngr:1;	/* device in medium transport element */
	u8         ack_req_q:1;	/* SIP specific bit */
	u8         addr32:1;	/* SIP specific bit */
	u8         addr16:1;	/* SIP specific bit */

	u8         rel_adr:1;	/* relative address */
	u8         w_bus32:1;
	u8         w_bus16:1;
	u8         synchronous:1;
	u8         linked_commands:1;
	u8         trans_dis:1;
	u8         cmd_queue:1;	/* command queueing supported */
	u8         soft_reset:1;	/* soft reset alternative (VS) */
#else
	u8         device_type:5;	/* peripheral device type */
	u8         peripheral_qual:3;
					/* peripheral qualifier */

	u8         device_type_mod:7;
					/* device type modifier */
	u8         rmb:1;		/* removable medium bit */

	u8         version;

	u8         rsp_data_format:4;
	u8         hi_support:1;	/* SCSI-3: supports REPORT LUNS */
	u8         norm_aca:1;	/* normal ACA supported */
	u8         terminate_iop:1;/* terminate I/O process */
	u8         aenc:1;		/* async event notification capability
					 */

	u8         additional_len;
	u8         reserved1:7;
	u8         sccs:1;

	u8         addr16:1;	/* SIP specific bit */
	u8         addr32:1;	/* SIP specific bit */
	u8         ack_req_q:1;	/* SIP specific bit */
	u8         m_chngr:1;	/* device in medium transport element */
	u8         multi_port:1;	/* multi-port device */
	u8         reserved3:1;	/* TBD - Vendor Specific */
	u8         enc_serv:1;	/* enclosure service component */
	u8         reserved2:1;

	u8         soft_seset:1;	/* soft reset alternative (VS) */
	u8         cmd_queue:1;	/* command queueing supported */
	u8         trans_dis:1;
	u8         linked_commands:1;
	u8         synchronous:1;
	u8         w_bus16:1;
	u8         w_bus32:1;
	u8         rel_adr:1;	/* relative address */
#endif
	struct scsi_inquiry_vendor_s vendor_id;
	struct scsi_inquiry_prodid_s product_id;
	struct scsi_inquiry_prodrev_s product_rev;
	u8         vendor_specific[20];
	u8         reserved4[40];
};

/*
 * inquiry.peripheral_qual field values
 */
#define SCSI_DEVQUAL_DEFAULT        0
#define SCSI_DEVQUAL_NOT_CONNECTED  1
#define SCSI_DEVQUAL_NOT_SUPPORTED  3

/*
 * inquiry.device_type field values
 */
#define SCSI_DEVICE_DIRECT_ACCESS       0x00
#define SCSI_DEVICE_SEQ_ACCESS          0x01
#define SCSI_DEVICE_ARRAY_CONTROLLER    0x0C
#define SCSI_DEVICE_UNKNOWN             0x1F

/*
 * inquiry.version
 */
#define SCSI_VERSION_ANSI_X3131     2	/* ANSI X3.131 SCSI-2 */
#define SCSI_VERSION_SPC            3	/* SPC (SCSI-3), ANSI X3.301:1997 */
#define SCSI_VERSION_SPC_2          4	/* SPC-2 */

/*
 * response data format
 */
#define SCSI_RSP_DATA_FORMAT        2	/* SCSI-2 & SPC */

/*
 * SCSI inquiry page codes
 */
#define SCSI_INQ_PAGE_VPD_PAGES     0x00	/* supported vpd pages */
#define SCSI_INQ_PAGE_USN_PAGE      0x80	/* unit serial number page */
#define SCSI_INQ_PAGE_DEV_IDENT     0x83	/* device indentification page
						 */
#define SCSI_INQ_PAGES_MAX          3

/*
 * supported vital product data pages
 */
struct scsi_inq_page_vpd_pages_s{
#ifdef __BIGENDIAN
	u8         peripheral_qual:3;
	u8         device_type:5;
#else
	u8         device_type:5;
	u8         peripheral_qual:3;
#endif
	u8         page_code;
	u8         reserved;
	u8         page_length;
	u8         pages[SCSI_INQ_PAGES_MAX];
};

/*
 * Unit serial number page
 */
#define SCSI_INQ_USN_LEN 32

struct scsi_inq_usn_s{
	char            usn[SCSI_INQ_USN_LEN];
};

struct scsi_inq_page_usn_s{
#ifdef __BIGENDIAN
	u8         peripheral_qual:3;
	u8         device_type:5;
#else
	u8         device_type:5;
	u8         peripheral_qual:3;
#endif
	u8         page_code;
	u8         reserved1;
	u8         page_length;
	struct scsi_inq_usn_s  usn;
};

enum {
	SCSI_INQ_DIP_CODE_BINARY = 1,	/* identifier has binary value */
	SCSI_INQ_DIP_CODE_ASCII = 2,	/* identifier has ascii value */
};

enum {
	SCSI_INQ_DIP_ASSOC_LUN = 0,	/* id is associated with device */
	SCSI_INQ_DIP_ASSOC_PORT = 1,	/* id is associated with port that
					 * received the request
					 */
};

enum {
	SCSI_INQ_ID_TYPE_VENDOR = 1,
	SCSI_INQ_ID_TYPE_IEEE = 2,
	SCSI_INQ_ID_TYPE_FC_FS = 3,
	SCSI_INQ_ID_TYPE_OTHER = 4,
};

struct scsi_inq_dip_desc_s{
#ifdef __BIGENDIAN
	u8         res0:4;
	u8         code_set:4;
	u8         res1:2;
	u8         association:2;
	u8         id_type:4;
#else
	u8         code_set:4;
	u8         res0:4;
	u8         id_type:4;
	u8         association:2;
	u8         res1:2;
#endif
	u8         res2;
	u8         id_len;
	struct scsi_lun_sn_s   id;
};

/*
 * Device indentification page
 */
struct scsi_inq_page_dev_ident_s{
#ifdef __BIGENDIAN
	u8         peripheral_qual:3;
	u8         device_type:5;
#else
	u8         device_type:5;
	u8         peripheral_qual:3;
#endif
	u8         page_code;
	u8         reserved1;
	u8         page_length;
	struct scsi_inq_dip_desc_s desc;
};

/* ------------------------------------------------------------
 * READ CAPACITY
 * ------------------------------------------------------------
 */

struct scsi_read_capacity_s{
	u8         opcode;
#ifdef __BIGENDIAN
	u8         lun:3;
	u8         reserved1:4;
	u8         rel_adr:1;
#else
	u8         rel_adr:1;
	u8         reserved1:4;
	u8         lun:3;
#endif
	u8         lba0;	/* MSB */
	u8         lba1;
	u8         lba2;
	u8         lba3;	/* LSB */
	u8         reserved2;
	u8         reserved3;
#ifdef __BIGENDIAN
	u8         reserved4:7;
	u8         pmi:1;	/* partial medium indicator */
#else
	u8         pmi:1;	/* partial medium indicator */
	u8         reserved4:7;
#endif
	u8         control;
};

struct scsi_read_capacity_data_s{
	u32        max_lba;	/* maximum LBA available */
	u32        block_length;	/* in bytes */
};

struct scsi_read_capacity16_data_s{
	u64        lba;	/* maximum LBA available */
	u32        block_length;	/* in bytes */
#ifdef __BIGENDIAN
	u8         reserved1:4,
			p_type:3,
			prot_en:1;
	u8		reserved2:4,
			lb_pbe:4;	/* logical blocks per physical block
					 * exponent */
	u16	reserved3:2,
			lba_align:14;	/* lowest aligned logical block
					 * address */
#else
	u16	lba_align:14,	/* lowest aligned logical block
					 * address */
			reserved3:2;
	u8		lb_pbe:4,	/* logical blocks per physical block
					 * exponent */
			reserved2:4;
	u8		prot_en:1,
			p_type:3,
			reserved1:4;
#endif
	u64	reserved4;
	u64	reserved5;
};

/* ------------------------------------------------------------
 * REPORT LUNS command
 * ------------------------------------------------------------
 */

struct scsi_report_luns_s{
	u8         opcode;		/* A0h - REPORT LUNS opCode */
	u8         reserved1[5];
	u8         alloc_length[4];/* allocation length MSB first */
	u8         reserved2;
	u8         control;
};

#define SCSI_REPORT_LUN_ALLOC_LENGTH(rl)                		\
    ((rl->alloc_length[0] << 24) | (rl->alloc_length[1] << 16) | 	\
     (rl->alloc_length[2] << 8) | (rl->alloc_length[3]))

#define SCSI_REPORT_LUNS_SET_ALLOCLEN(rl, alloc_len) {      \
    (rl)->alloc_length[0] = (alloc_len) >> 24;      			\
    (rl)->alloc_length[1] = ((alloc_len) >> 16) & 0xFF; 		\
    (rl)->alloc_length[2] = ((alloc_len) >> 8) & 0xFF;  		\
    (rl)->alloc_length[3] = (alloc_len) & 0xFF;     			\
}

struct scsi_report_luns_data_s{
	u32        lun_list_length;	/* length of LUN list length */
	u32        reserved;
	lun_t           lun[1];			/* first LUN in lun list */
};

/* -------------------------------------------------------------
 * SCSI mode  parameters
 * -----------------------------------------------------------
 */
enum {
	SCSI_DA_MEDIUM_DEF = 0,	/* direct access default medium type */
	SCSI_DA_MEDIUM_SS = 1,	/* direct access single sided */
	SCSI_DA_MEDIUM_DS = 2,	/* direct access double sided */
};

/*
 * SCSI Mode Select(6) cdb
 */
struct scsi_mode_select6_s{
	u8         opcode;
#ifdef __BIGENDIAN
	u8         reserved1:3;
	u8         pf:1;		/* page format */
	u8         reserved2:3;
	u8         sp:1;		/* save pages if set to 1 */
#else
	u8         sp:1;	/* save pages if set to 1 */
	u8         reserved2:3;
	u8         pf:1;	/* page format */
	u8         reserved1:3;
#endif
	u8         reserved3[2];
	u8         alloc_len;
	u8         control;
};

/*
 * SCSI Mode Select(10) cdb
 */
struct scsi_mode_select10_s{
	u8         opcode;
#ifdef __BIGENDIAN
	u8         reserved1:3;
	u8         pf:1;	/* page format */
	u8         reserved2:3;
	u8         sp:1;	/* save pages if set to 1 */
#else
	u8         sp:1;	/* save pages if set to 1 */
	u8         reserved2:3;
	u8         pf:1;	/* page format */
	u8         reserved1:3;
#endif
	u8         reserved3[5];
	u8         alloc_len_msb;
	u8         alloc_len_lsb;
	u8         control;
};

/*
 * SCSI Mode Sense(6) cdb
 */
struct scsi_mode_sense6_s{
	u8         opcode;
#ifdef __BIGENDIAN
	u8         reserved1:4;
	u8         dbd:1;	/* disable block discriptors if set to 1 */
	u8         reserved2:3;

	u8         pc:2;	/* page control */
	u8         page_code:6;
#else
	u8         reserved2:3;
	u8         dbd:1;	/* disable block descriptors if set to 1 */
	u8         reserved1:4;

	u8         page_code:6;
	u8         pc:2;	/* page control */
#endif
	u8         reserved3;
	u8         alloc_len;
	u8         control;
};

/*
 * SCSI Mode Sense(10) cdb
 */
struct scsi_mode_sense10_s{
	u8         opcode;
#ifdef __BIGENDIAN
	u8         reserved1:3;
	u8         LLBAA:1;	/* long LBA accepted if set to 1 */
	u8         dbd:1;		/* disable block descriptors if set
					 * to 1
					 */
	u8         reserved2:3;

	u8         pc:2;		/* page control */
	u8         page_code:6;
#else
	u8         reserved2:3;
	u8         dbd:1;		/* disable block descriptors if set to
					 * 1
					 */
	u8         LLBAA:1;	/* long LBA accepted if set to 1 */
	u8         reserved1:3;

	u8         page_code:6;
	u8         pc:2;		/* page control */
#endif
	u8         reserved3[4];
	u8         alloc_len_msb;
	u8         alloc_len_lsb;
	u8         control;
};

#define SCSI_CDB10_GET_AL(cdb)  					\
    ((cdb)->alloc_len_msb << 8 | (cdb)->alloc_len_lsb)

#define SCSI_CDB10_SET_AL(cdb, al) {      \
    (cdb)->alloc_len_msb = al >> 8;       				\
    (cdb)->alloc_len_lsb = al & 0xFF;     				\
}

#define SCSI_CDB6_GET_AL(cdb) ((cdb)->alloc_len)

#define SCSI_CDB6_SET_AL(cdb, al) {      \
    (cdb)->alloc_len = al;         					\
}

/*
 * page control field values
 */
#define SCSI_PC_CURRENT_VALUES       0x0
#define SCSI_PC_CHANGEABLE_VALUES    0x1
#define SCSI_PC_DEFAULT_VALUES       0x2
#define SCSI_PC_SAVED_VALUES         0x3

/*
 * SCSI mode page codes
 */
#define SCSI_MP_VENDOR_SPEC     0x00
#define SCSI_MP_DISC_RECN       0x02	/* disconnect-reconnect page */
#define SCSI_MP_FORMAT_DEVICE   0x03
#define SCSI_MP_RDG             0x04	/* rigid disk geometry page */
#define SCSI_MP_FDP             0x05	/* flexible disk page */
#define SCSI_MP_CACHING         0x08	/* caching page */
#define SCSI_MP_CONTROL         0x0A	/* control mode page */
#define SCSI_MP_MED_TYPES_SUP   0x0B	/* medium types supported page */
#define SCSI_MP_INFO_EXCP_CNTL  0x1C	/* informational exception control */
#define SCSI_MP_ALL             0x3F	/* return all pages - mode sense only */

/*
 * mode parameter header
 */
struct scsi_mode_param_header6_s{
	u8         mode_datalen;
	u8         medium_type;

	/*
	 * device specific parameters expanded for direct access devices
	 */
#ifdef __BIGENDIAN
	u32        wp:1;		/* write protected */
	u32        reserved1:2;
	u32        dpofua:1;	/* disable page out + force unit access
					 */
	u32        reserved2:4;
#else
	u32        reserved2:4;
	u32        dpofua:1;	/* disable page out + force unit access
					 */
	u32        reserved1:2;
	u32        wp:1;		/* write protected */
#endif

	u8         block_desclen;
};

struct scsi_mode_param_header10_s{
	u32        mode_datalen:16;
	u32        medium_type:8;

	/*
	 * device specific parameters expanded for direct access devices
	 */
#ifdef __BIGENDIAN
	u32        wp:1;		/* write protected */
	u32        reserved1:2;
	u32        dpofua:1;	/* disable page out + force unit access
					 */
	u32        reserved2:4;
#else
	u32        reserved2:4;
	u32        dpofua:1;	/* disable page out + force unit access
					 */
	u32        reserved1:2;
	u32        wp:1;		/* write protected */
#endif

#ifdef __BIGENDIAN
	u32        reserved3:7;
	u32        longlba:1;
#else
	u32        longlba:1;
	u32        reserved3:7;
#endif
	u32        reserved4:8;
	u32        block_desclen:16;
};

/*
 * mode parameter block descriptor
 */
struct scsi_mode_param_desc_s{
	u32        nblks;
	u32        density_code:8;
	u32        block_length:24;
};

/*
 * Disconnect-reconnect mode page format
 */
struct scsi_mp_disc_recn_s{
#ifdef __BIGENDIAN
	u8         ps:1;
	u8         reserved1:1;
	u8         page_code:6;
#else
	u8         page_code:6;
	u8         reserved1:1;
	u8         ps:1;
#endif
	u8         page_len;
	u8         buf_full_ratio;
	u8         buf_empty_ratio;

	u8         bil_msb;	/* bus inactivity limit -MSB */
	u8         bil_lsb;	/* bus inactivity limit -LSB */

	u8         dtl_msb;	/* disconnect time limit - MSB */
	u8         dtl_lsb;	/* disconnect time limit - LSB */

	u8         ctl_msb;	/* connect time limit - MSB */
	u8         ctl_lsb;	/* connect time limit - LSB */

	u8         max_burst_len_msb;
	u8         max_burst_len_lsb;
#ifdef __BIGENDIAN
	u8         emdp:1;	/* enable modify data pointers */
	u8         fa:3;	/* fair arbitration */
	u8         dimm:1;	/* disconnect immediate */
	u8         dtdc:3;	/* data transfer disconnect control */
#else
	u8         dtdc:3;	/* data transfer disconnect control */
	u8         dimm:1;	/* disconnect immediate */
	u8         fa:3;	/* fair arbitration */
	u8         emdp:1;	/* enable modify data pointers */
#endif

	u8         reserved3;

	u8         first_burst_len_msb;
	u8         first_burst_len_lsb;
};

/*
 * SCSI format device mode page
 */
struct scsi_mp_format_device_s{
#ifdef __BIGENDIAN
	u32        ps:1;
	u32        reserved1:1;
	u32        page_code:6;
#else
	u32        page_code:6;
	u32        reserved1:1;
	u32        ps:1;
#endif
	u32        page_len:8;
	u32        tracks_per_zone:16;

	u32        a_sec_per_zone:16;
	u32        a_tracks_per_zone:16;

	u32        a_tracks_per_lun:16;	/* alternate tracks/lun-MSB */
	u32        sec_per_track:16;	/* sectors/track-MSB */

	u32        bytes_per_sector:16;
	u32        interleave:16;

	u32        tsf:16;			/* track skew factor-MSB */
	u32        csf:16;			/* cylinder skew factor-MSB */

#ifdef __BIGENDIAN
	u32        ssec:1;	/* soft sector formatting */
	u32        hsec:1;	/* hard sector formatting */
	u32        rmb:1;	/* removable media */
	u32        surf:1;	/* surface */
	u32        reserved2:4;
#else
	u32        reserved2:4;
	u32        surf:1;	/* surface */
	u32        rmb:1;	/* removable media */
	u32        hsec:1;	/* hard sector formatting */
	u32        ssec:1;	/* soft sector formatting */
#endif
	u32        reserved3:24;
};

/*
 * SCSI rigid disk device geometry page
 */
struct scsi_mp_rigid_device_geometry_s{
#ifdef __BIGENDIAN
	u32        ps:1;
	u32        reserved1:1;
	u32        page_code:6;
#else
	u32        page_code:6;
	u32        reserved1:1;
	u32        ps:1;
#endif
	u32        page_len:8;
	u32        num_cylinders0:8;
	u32        num_cylinders1:8;

	u32        num_cylinders2:8;
	u32        num_heads:8;
	u32        scwp0:8;
	u32        scwp1:8;

	u32        scwp2:8;
	u32        scrwc0:8;
	u32        scrwc1:8;
	u32        scrwc2:8;

	u32        dsr:16;
	u32        lscyl0:8;
	u32        lscyl1:8;

	u32        lscyl2:8;
#ifdef __BIGENDIAN
	u32        reserved2:6;
	u32        rpl:2;	/* rotational position locking */
#else
	u32        rpl:2;	/* rotational position locking */
	u32        reserved2:6;
#endif
	u32        rot_off:8;
	u32        reserved3:8;

	u32        med_rot_rate:16;
	u32        reserved4:16;
};

/*
 * SCSI caching mode page
 */
struct scsi_mp_caching_s{
#ifdef __BIGENDIAN
	u8         ps:1;
	u8         res1:1;
	u8         page_code:6;
#else
	u8         page_code:6;
	u8         res1:1;
	u8         ps:1;
#endif
	u8         page_len;
#ifdef __BIGENDIAN
	u8         ic:1;	/* initiator control */
	u8         abpf:1;	/* abort pre-fetch */
	u8         cap:1;	/* caching analysis permitted */
	u8         disc:1;	/* discontinuity */
	u8         size:1;	/* size enable */
	u8         wce:1;	/* write cache enable */
	u8         mf:1;	/* multiplication factor */
	u8         rcd:1;	/* read cache disable */

	u8         drrp:4;	/* demand read retention priority */
	u8         wrp:4;	/* write retention priority */
#else
	u8         rcd:1;	/* read cache disable */
	u8         mf:1;	/* multiplication factor */
	u8         wce:1;	/* write cache enable */
	u8         size:1;	/* size enable */
	u8         disc:1;	/* discontinuity */
	u8         cap:1;	/* caching analysis permitted */
	u8         abpf:1;	/* abort pre-fetch */
	u8         ic:1;	/* initiator control */

	u8         wrp:4;	/* write retention priority */
	u8         drrp:4;	/* demand read retention priority */
#endif
	u8         dptl[2];/* disable pre-fetch transfer length */
	u8         min_prefetch[2];
	u8         max_prefetch[2];
	u8         max_prefetch_limit[2];
#ifdef __BIGENDIAN
	u8         fsw:1;	/* force sequential write */
	u8         lbcss:1;/* logical block cache segment size */
	u8         dra:1;	/* disable read ahead */
	u8         vs:2;	/* vendor specific */
	u8         res2:3;
#else
	u8         res2:3;
	u8         vs:2;	/* vendor specific */
	u8         dra:1;	/* disable read ahead */
	u8         lbcss:1;/* logical block cache segment size */
	u8         fsw:1;	/* force sequential write */
#endif
	u8         num_cache_segs;

	u8         cache_seg_size[2];
	u8         res3;
	u8         non_cache_seg_size[3];
};

/*
 * SCSI control mode page
 */
struct scsi_mp_control_page_s{
#ifdef __BIGENDIAN
u8         ps:1;
u8         reserved1:1;
u8         page_code:6;
#else
u8         page_code:6;
u8         reserved1:1;
u8         ps:1;
#endif
	u8         page_len;
#ifdef __BIGENDIAN
	u8         tst:3;		/* task set type */
	u8         reserved3:3;
	u8         gltsd:1;	/* global logging target save disable */
	u8         rlec:1;		/* report log exception condition */

	u8         qalgo_mod:4;	/* queue alogorithm modifier */
	u8         reserved4:1;
	u8         qerr:2;		/* queue error management */
	u8         dque:1;		/* disable queuing */

	u8         reserved5:1;
	u8         rac:1;		/* report a check */
	u8         reserved6:2;
	u8         swp:1;		/* software write protect */
	u8         raerp:1;	/* ready AER permission */
	u8         uaaerp:1;	/* unit attenstion AER permission */
	u8         eaerp:1;	/* error AER permission */

	u8         reserved7:5;
	u8         autoload_mod:3;
#else
	u8         rlec:1;		/* report log exception condition */
	u8         gltsd:1;	/* global logging target save disable */
	u8         reserved3:3;
	u8         tst:3;		/* task set type */

	u8         dque:1;		/* disable queuing */
	u8         qerr:2;		/* queue error management */
	u8         reserved4:1;
	u8         qalgo_mod:4;	/* queue alogorithm modifier */

	u8         eaerp:1;	/* error AER permission */
	u8         uaaerp:1;	/* unit attenstion AER permission */
	u8         raerp:1;	/* ready AER permission */
	u8         swp:1;		/* software write protect */
	u8         reserved6:2;
	u8         rac:1;		/* report a check */
	u8         reserved5:1;

	u8         autoload_mod:3;
	u8         reserved7:5;
#endif
	u8         rahp_msb;	/* ready AER holdoff period - MSB */
	u8         rahp_lsb;	/* ready AER holdoff period - LSB */

	u8         busy_timeout_period_msb;
	u8         busy_timeout_period_lsb;

	u8         ext_selftest_compl_time_msb;
	u8         ext_selftest_compl_time_lsb;
};

/*
 * SCSI medium types supported mode page
 */
struct scsi_mp_medium_types_sup_s{
#ifdef __BIGENDIAN
	u8         ps:1;
	u8         reserved1:1;
	u8         page_code:6;
#else
	u8         page_code:6;
	u8         reserved1:1;
	u8         ps:1;
#endif
	u8         page_len;

	u8         reserved3[2];
	u8         med_type1_sup;	/* medium type one supported */
	u8         med_type2_sup;	/* medium type two supported */
	u8         med_type3_sup;	/* medium type three supported */
	u8         med_type4_sup;	/* medium type four supported */
};

/*
 * SCSI informational exception control mode page
 */
struct scsi_mp_info_excpt_cntl_s{
#ifdef __BIGENDIAN
	u8         ps:1;
	u8         reserved1:1;
	u8         page_code:6;
#else
	u8         page_code:6;
	u8         reserved1:1;
	u8         ps:1;
#endif
	u8         page_len;
#ifdef __BIGENDIAN
	u8         perf:1;		/* performance */
	u8         reserved3:1;
	u8         ebf:1;		/* enable background fucntion */
	u8         ewasc:1;	/* enable warning */
	u8         dexcpt:1;	/* disable exception control */
	u8         test:1;		/* enable test device failure
					 * notification
					 */
	u8         reserved4:1;
	u8         log_error:1;

	u8         reserved5:4;
	u8         mrie:4;		/* method of reporting info
					 * exceptions
					 */
#else
	u8         log_error:1;
	u8         reserved4:1;
	u8         test:1;		/* enable test device failure
					 * notification
					 */
	u8         dexcpt:1;	/* disable exception control */
	u8         ewasc:1;	/* enable warning */
	u8         ebf:1;		/* enable background fucntion */
	u8         reserved3:1;
	u8         perf:1;		/* performance */

	u8         mrie:4;		/* method of reporting info
					 * exceptions
					 */
	u8         reserved5:4;
#endif
	u8         interval_timer_msb;
	u8         interval_timer_lsb;

	u8         report_count_msb;
	u8         report_count_lsb;
};

/*
 * Methods of reporting informational exceptions
 */
#define SCSI_MP_IEC_NO_REPORT       0x0	/* no reporting of exceptions */
#define SCSI_MP_IEC_AER             0x1	/* async event reporting */
#define SCSI_MP_IEC_UNIT_ATTN       0x2	/* generate unit attenstion */
#define SCSI_MO_IEC_COND_REC_ERR    0x3	/* conditionally generate recovered
					 * error
					 */
#define SCSI_MP_IEC_UNCOND_REC_ERR  0x4	/* unconditionally generate recovered
					 * error
					 */
#define SCSI_MP_IEC_NO_SENSE        0x5	/* generate no sense */
#define SCSI_MP_IEC_ON_REQUEST      0x6	/* only report exceptions on request */

/*
 * SCSI flexible disk page
 */
struct scsi_mp_flexible_disk_s{
#ifdef __BIGENDIAN
	u8         ps:1;
	u8         reserved1:1;
	u8         page_code:6;
#else
	u8         page_code:6;
	u8         reserved1:1;
	u8         ps:1;
#endif
	u8         page_len;

	u8         transfer_rate_msb;
	u8         transfer_rate_lsb;

	u8         num_heads;
	u8         num_sectors;

	u8         bytes_per_sector_msb;
	u8         bytes_per_sector_lsb;

	u8         num_cylinders_msb;
	u8         num_cylinders_lsb;

	u8         sc_wpc_msb;	/* starting cylinder-write
					 * precompensation msb
					 */
	u8         sc_wpc_lsb;	/* starting cylinder-write
					 * precompensation lsb
					 */
	u8         sc_rwc_msb;	/* starting cylinder-reduced write
					 * current msb
					 */
	u8         sc_rwc_lsb;	/* starting cylinder-reduced write
					 * current lsb
					 */

	u8         dev_step_rate_msb;
	u8         dev_step_rate_lsb;

	u8         dev_step_pulse_width;

	u8         head_sd_msb;	/* head settle delay msb */
	u8         head_sd_lsb;	/* head settle delay lsb */

	u8         motor_on_delay;
	u8         motor_off_delay;
#ifdef __BIGENDIAN
	u8         trdy:1;		/* true ready bit */
	u8         ssn:1;		/* start sector number bit */
	u8         mo:1;		/* motor on bit */
	u8         reserved3:5;

	u8         reserved4:4;
	u8         spc:4;		/* step pulse per cylinder */
#else
	u8         reserved3:5;
	u8         mo:1;		/* motor on bit */
	u8         ssn:1;		/* start sector number bit */
	u8         trdy:1;		/* true ready bit */

	u8         spc:4;		/* step pulse per cylinder */
	u8         reserved4:4;
#endif
	u8         write_comp;
	u8         head_load_delay;
	u8         head_unload_delay;
#ifdef __BIGENDIAN
	u8         pin34:4;	/* pin34 usage */
	u8         pin2:4;		/* pin2 usage */

	u8         pin4:4;		/* pin4 usage */
	u8         pin1:4;		/* pin1 usage */
#else
	u8         pin2:4;		/* pin2 usage */
	u8         pin34:4;	/* pin34 usage */

	u8         pin1:4;		/* pin1 usage */
	u8         pin4:4;		/* pin4 usage */
#endif
	u8         med_rot_rate_msb;
	u8         med_rot_rate_lsb;

	u8         reserved5[2];
};

struct scsi_mode_page_format_data6_s{
	struct scsi_mode_param_header6_s mph;	/* mode page header */
	struct scsi_mode_param_desc_s desc;	/* block descriptor */
	struct scsi_mp_format_device_s format;	/* format device data */
};

struct scsi_mode_page_format_data10_s{
	struct scsi_mode_param_header10_s mph;	/* mode page header */
	struct scsi_mode_param_desc_s desc;	/* block descriptor */
	struct scsi_mp_format_device_s format;	/* format device data */
};

struct scsi_mode_page_rdg_data6_s{
	struct scsi_mode_param_header6_s mph;	/* mode page header */
	struct scsi_mode_param_desc_s desc;	/* block descriptor */
	struct scsi_mp_rigid_device_geometry_s rdg;
					/* rigid geometry data */
};

struct scsi_mode_page_rdg_data10_s{
	struct scsi_mode_param_header10_s mph;	/* mode page header */
	struct scsi_mode_param_desc_s desc;	/* block descriptor */
	struct scsi_mp_rigid_device_geometry_s rdg;
					/* rigid geometry data */
};

struct scsi_mode_page_cache6_s{
	struct scsi_mode_param_header6_s mph;	/* mode page header */
	struct scsi_mode_param_desc_s desc;	/* block descriptor */
	struct scsi_mp_caching_s cache;	/* cache page data */
};

struct scsi_mode_page_cache10_s{
	struct scsi_mode_param_header10_s mph;	/* mode page header */
	struct scsi_mode_param_desc_s desc;	/* block descriptor */
	struct scsi_mp_caching_s cache;	/* cache page data */
};

/* --------------------------------------------------------------
 * Format Unit command
 * ------------------------------------------------------------
 */

/*
 * Format Unit CDB
 */
struct scsi_format_unit_s{
	u8         opcode;
#ifdef __BIGENDIAN
	u8         res1:3;
	u8         fmtdata:1;	/* if set, data out phase has format
					 * data
					 */
	u8         cmplst:1;	/* if set, defect list is complete */
	u8         def_list:3;	/* format of defect descriptor is
					 * fmtdata =1
					 */
#else
	u8         def_list:3;	/* format of defect descriptor is
					 * fmtdata = 1
					 */
	u8         cmplst:1;	/* if set, defect list is complete */
	u8         fmtdata:1;	/* if set, data out phase has format
					 * data
					 */
	u8         res1:3;
#endif
	u8         interleave_msb;
	u8         interleave_lsb;
	u8         vendor_spec;
	u8         control;
};

/*
 * h
 */
struct scsi_reserve6_s{
	u8         opcode;
#ifdef __BIGENDIAN
	u8         reserved:3;
	u8         obsolete:4;
	u8         extent:1;
#else
	u8         extent:1;
	u8         obsolete:4;
	u8         reserved:3;
#endif
	u8         reservation_id;
	u16        param_list_len;
	u8         control;
};

/*
 * h
 */
struct scsi_release6_s{
	u8         opcode;
#ifdef __BIGENDIAN
	u8         reserved1:3;
	u8         obsolete:4;
	u8         extent:1;
#else
	u8         extent:1;
	u8         obsolete:4;
	u8         reserved1:3;
#endif
	u8         reservation_id;
	u16        reserved2;
	u8         control;
};

/*
 * h
 */
struct scsi_reserve10_s{
	u8         opcode;
#ifdef __BIGENDIAN
	u8         reserved1:3;
	u8         third_party:1;
	u8         reserved2:2;
	u8         long_id:1;
	u8         extent:1;
#else
	u8         extent:1;
	u8         long_id:1;
	u8         reserved2:2;
	u8         third_party:1;
	u8         reserved1:3;
#endif
	u8         reservation_id;
	u8         third_pty_dev_id;
	u8         reserved3;
	u8         reserved4;
	u8         reserved5;
	u16        param_list_len;
	u8         control;
};

struct scsi_release10_s{
	u8         opcode;
#ifdef __BIGENDIAN
	u8         reserved1:3;
	u8         third_party:1;
	u8         reserved2:2;
	u8         long_id:1;
	u8         extent:1;
#else
	u8         extent:1;
	u8         long_id:1;
	u8         reserved2:2;
	u8         third_party:1;
	u8         reserved1:3;
#endif
	u8         reservation_id;
	u8         third_pty_dev_id;
	u8         reserved3;
	u8         reserved4;
	u8         reserved5;
	u16        param_list_len;
	u8         control;
};

struct scsi_verify10_s{
	u8         opcode;
#ifdef __BIGENDIAN
	u8         lun:3;
	u8         dpo:1;
	u8         reserved:2;
	u8         bytchk:1;
	u8         reladdr:1;
#else
	u8         reladdr:1;
	u8         bytchk:1;
	u8         reserved:2;
	u8         dpo:1;
	u8         lun:3;
#endif
	u8         lba0;
	u8         lba1;
	u8         lba2;
	u8         lba3;
	u8         reserved1;
	u8         verification_len0;
	u8         verification_len1;
	u8         control_byte;
};

struct scsi_request_sense_s{
	u8         opcode;
#ifdef __BIGENDIAN
	u8         lun:3;
	u8         reserved:5;
#else
	u8         reserved:5;
	u8         lun:3;
#endif
	u8         reserved0;
	u8         reserved1;
	u8         alloc_len;
	u8         control_byte;
};

/* ------------------------------------------------------------
 * SCSI status byte values
 * ------------------------------------------------------------
 */
#define SCSI_STATUS_GOOD                   0x00
#define SCSI_STATUS_CHECK_CONDITION        0x02
#define SCSI_STATUS_CONDITION_MET          0x04
#define SCSI_STATUS_BUSY                   0x08
#define SCSI_STATUS_INTERMEDIATE           0x10
#define SCSI_STATUS_ICM                    0x14	/* intermediate condition met */
#define SCSI_STATUS_RESERVATION_CONFLICT   0x18
#define SCSI_STATUS_COMMAND_TERMINATED     0x22
#define SCSI_STATUS_QUEUE_FULL             0x28
#define SCSI_STATUS_ACA_ACTIVE             0x30

#define SCSI_MAX_ALLOC_LEN		0xFF	/* maximum allocarion length
						 * in CDBs
						 */

#define SCSI_OP_WRITE_VERIFY10      0x2E
#define SCSI_OP_WRITE_VERIFY12      0xAE
#define SCSI_OP_UNDEF               0xFF

/*
 * SCSI WRITE-VERIFY(10) command
 */
struct scsi_write_verify10_s{
	u8         opcode;
#ifdef __BIGENDIAN
	u8         reserved1:3;
	u8         dpo:1;		/* Disable Page Out */
	u8         reserved2:1;
	u8         ebp:1;		/* erse by-pass */
	u8         bytchk:1;	/* byte check */
	u8         rel_adr:1;	/* relative address */
#else
	u8         rel_adr:1;	/* relative address */
	u8         bytchk:1;	/* byte check */
	u8         ebp:1;		/* erse by-pass */
	u8         reserved2:1;
	u8         dpo:1;		/* Disable Page Out */
	u8         reserved1:3;
#endif
	u8         lba0;		/* logical block address - MSB */
	u8         lba1;
	u8         lba2;
	u8         lba3;		/* LSB */
	u8         reserved3;
	u8         xfer_length0;	/* transfer length in blocks - MSB */
	u8         xfer_length1;	/* LSB */
	u8         control;
};

#pragma pack()

#endif /* __SCSI_H__ */
