/*
 * Largely written by Julian Elischer (julian@tfs.com)
 * for TRW Financial Systems.
 *
 * TRW Financial Systems, in accordance with their agreement with Carnegie
 * Mellon University, makes this software available to CMU to distribute
 * or use in any manner that they see fit as long as this message is kept with
 * the software. For this reason TFS also grants any other persons or
 * organisations permission to use or modify this software.
 *
 * TFS supplies this software to be publicly redistributed
 * on the understanding that TFS is not responsible for the correct
 * functioning of this software in any circumstances.
 *
 * Ported to run under 386BSD by Julian Elischer (julian@tfs.com) Sept 1992
 *
 * $FreeBSD: src/sys/cam/scsi/scsi_all.h,v 1.21 2002/10/08 17:12:44 ken Exp $
 *
 * Copyright (c) 2003 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id$
 */

#ifndef	_AICLIB_H
#define _AICLIB_H

/*
 * Linux Interrupt Support.
 */
#ifndef IRQ_RETVAL
typedef void irqreturn_t;
#define	IRQ_RETVAL(x)
#endif

/*
 * SCSI command format
 */

/*
 * Define dome bits that are in ALL (or a lot of) scsi commands
 */
#define SCSI_CTL_LINK		0x01
#define SCSI_CTL_FLAG		0x02
#define SCSI_CTL_VENDOR		0xC0
#define	SCSI_CMD_LUN		0xA0	/* these two should not be needed */
#define	SCSI_CMD_LUN_SHIFT	5	/* LUN in the cmd is no longer SCSI */

#define SCSI_MAX_CDBLEN		16	/* 
					 * 16 byte commands are in the 
					 * SCSI-3 spec 
					 */
/* 6byte CDBs special case 0 length to be 256 */
#define SCSI_CDB6_LEN(len)	((len) == 0 ? 256 : len)

/*
 * This type defines actions to be taken when a particular sense code is
 * received.  Right now, these flags are only defined to take up 16 bits,
 * but can be expanded in the future if necessary.
 */
typedef enum {
	SS_NOP		= 0x000000, /* Do nothing */
	SS_RETRY	= 0x010000, /* Retry the command */
	SS_FAIL		= 0x020000, /* Bail out */
	SS_START	= 0x030000, /* Send a Start Unit command to the device,
				     * then retry the original command.
				     */
	SS_TUR		= 0x040000, /* Send a Test Unit Ready command to the
				     * device, then retry the original command.
				     */
	SS_REQSENSE	= 0x050000, /* Send a RequestSense command to the
				     * device, then retry the original command.
				     */
	SS_INQ_REFRESH	= 0x060000,
	SS_MASK		= 0xff0000
} aic_sense_action;

typedef enum {
	SSQ_NONE		= 0x0000,
	SSQ_DECREMENT_COUNT	= 0x0100,  /* Decrement the retry count */
	SSQ_MANY		= 0x0200,  /* send lots of recovery commands */
	SSQ_RANGE		= 0x0400,  /*
					    * This table entry represents the
					    * end of a range of ASCQs that
					    * have identical error actions
					    * and text.
					    */
	SSQ_PRINT_SENSE		= 0x0800,
	SSQ_DELAY		= 0x1000,  /* Delay before retry. */
	SSQ_DELAY_RANDOM	= 0x2000,  /* Randomized delay before retry. */
	SSQ_FALLBACK		= 0x4000,  /* Do a speed fallback to recover */
	SSQ_MASK		= 0xff00
} aic_sense_action_qualifier;

/* Mask for error status values */
#define SS_ERRMASK	0xff

/* The default, retyable, error action */
#define SS_RDEF		SS_RETRY|SSQ_DECREMENT_COUNT|SSQ_PRINT_SENSE|EIO

/* The retyable, error action, with table specified error code */
#define SS_RET		SS_RETRY|SSQ_DECREMENT_COUNT|SSQ_PRINT_SENSE

/* Fatal error action, with table specified error code */
#define SS_FATAL	SS_FAIL|SSQ_PRINT_SENSE

struct scsi_generic
{
	uint8_t opcode;
	uint8_t bytes[11];
};

struct scsi_request_sense
{
	uint8_t opcode;
	uint8_t byte2;
	uint8_t unused[2];
	uint8_t length;
	uint8_t control;
};

struct scsi_test_unit_ready
{
	uint8_t opcode;
	uint8_t byte2;
	uint8_t unused[3];
	uint8_t control;
};

struct scsi_send_diag
{
	uint8_t opcode;
	uint8_t byte2;
#define	SSD_UOL		0x01
#define	SSD_DOL		0x02
#define	SSD_SELFTEST	0x04
#define	SSD_PF		0x10
	uint8_t unused[1];
	uint8_t paramlen[2];
	uint8_t control;
};

struct scsi_sense
{
	uint8_t opcode;
	uint8_t byte2;
	uint8_t unused[2];
	uint8_t length;
	uint8_t control;
};

struct scsi_inquiry
{
	uint8_t opcode;
	uint8_t byte2;
#define	SI_EVPD 0x01
	uint8_t page_code;
	uint8_t reserved;
	uint8_t length;
	uint8_t control;
};

struct scsi_mode_sense_6
{
	uint8_t opcode;
	uint8_t byte2;
#define	SMS_DBD				0x08
	uint8_t page;
#define	SMS_PAGE_CODE 			0x3F
#define SMS_VENDOR_SPECIFIC_PAGE	0x00
#define SMS_DISCONNECT_RECONNECT_PAGE	0x02
#define SMS_PERIPHERAL_DEVICE_PAGE	0x09
#define SMS_CONTROL_MODE_PAGE		0x0A
#define SMS_ALL_PAGES_PAGE		0x3F
#define	SMS_PAGE_CTRL_MASK		0xC0
#define	SMS_PAGE_CTRL_CURRENT 		0x00
#define	SMS_PAGE_CTRL_CHANGEABLE 	0x40
#define	SMS_PAGE_CTRL_DEFAULT 		0x80
#define	SMS_PAGE_CTRL_SAVED 		0xC0
	uint8_t unused;
	uint8_t length;
	uint8_t control;
};

struct scsi_mode_sense_10
{
	uint8_t opcode;
	uint8_t byte2;		/* same bits as small version */
	uint8_t page; 		/* same bits as small version */
	uint8_t unused[4];
	uint8_t length[2];
	uint8_t control;
};

struct scsi_mode_select_6
{
	uint8_t opcode;
	uint8_t byte2;
#define	SMS_SP	0x01
#define	SMS_PF	0x10
	uint8_t unused[2];
	uint8_t length;
	uint8_t control;
};

struct scsi_mode_select_10
{
	uint8_t opcode;
	uint8_t byte2;		/* same bits as small version */
	uint8_t unused[5];
	uint8_t length[2];
	uint8_t control;
};

/*
 * When sending a mode select to a tape drive, the medium type must be 0.
 */
struct scsi_mode_hdr_6
{
	uint8_t datalen;
	uint8_t medium_type;
	uint8_t dev_specific;
	uint8_t block_descr_len;
};

struct scsi_mode_hdr_10
{
	uint8_t datalen[2];
	uint8_t medium_type;
	uint8_t dev_specific;
	uint8_t reserved[2];
	uint8_t block_descr_len[2];
};

struct scsi_mode_block_descr
{
	uint8_t density_code;
	uint8_t num_blocks[3];
	uint8_t reserved;
	uint8_t block_len[3];
};

struct scsi_log_sense
{
	uint8_t opcode;
	uint8_t byte2;
#define	SLS_SP				0x01
#define	SLS_PPC				0x02
	uint8_t page;
#define	SLS_PAGE_CODE 			0x3F
#define	SLS_ALL_PAGES_PAGE		0x00
#define	SLS_OVERRUN_PAGE		0x01
#define	SLS_ERROR_WRITE_PAGE		0x02
#define	SLS_ERROR_READ_PAGE		0x03
#define	SLS_ERROR_READREVERSE_PAGE	0x04
#define	SLS_ERROR_VERIFY_PAGE		0x05
#define	SLS_ERROR_NONMEDIUM_PAGE	0x06
#define	SLS_ERROR_LASTN_PAGE		0x07
#define	SLS_PAGE_CTRL_MASK		0xC0
#define	SLS_PAGE_CTRL_THRESHOLD		0x00
#define	SLS_PAGE_CTRL_CUMULATIVE	0x40
#define	SLS_PAGE_CTRL_THRESH_DEFAULT	0x80
#define	SLS_PAGE_CTRL_CUMUL_DEFAULT	0xC0
	uint8_t reserved[2];
	uint8_t paramptr[2];
	uint8_t length[2];
	uint8_t control;
};

struct scsi_log_select
{
	uint8_t opcode;
	uint8_t byte2;
/*	SLS_SP				0x01 */
#define	SLS_PCR				0x02
	uint8_t page;
/*	SLS_PAGE_CTRL_MASK		0xC0 */
/*	SLS_PAGE_CTRL_THRESHOLD		0x00 */
/*	SLS_PAGE_CTRL_CUMULATIVE	0x40 */
/*	SLS_PAGE_CTRL_THRESH_DEFAULT	0x80 */
/*	SLS_PAGE_CTRL_CUMUL_DEFAULT	0xC0 */
	uint8_t reserved[4];
	uint8_t length[2];
	uint8_t control;
};

struct scsi_log_header
{
	uint8_t page;
	uint8_t reserved;
	uint8_t datalen[2];
};

struct scsi_log_param_header {
	uint8_t param_code[2];
	uint8_t param_control;
#define	SLP_LP				0x01
#define	SLP_LBIN			0x02
#define	SLP_TMC_MASK			0x0C
#define	SLP_TMC_ALWAYS			0x00
#define	SLP_TMC_EQUAL			0x04
#define	SLP_TMC_NOTEQUAL		0x08
#define	SLP_TMC_GREATER			0x0C
#define	SLP_ETC				0x10
#define	SLP_TSD				0x20
#define	SLP_DS				0x40
#define	SLP_DU				0x80
	uint8_t param_len;
};

struct scsi_control_page {
	uint8_t page_code;
	uint8_t page_length;
	uint8_t rlec;
#define SCB_RLEC			0x01	/*Report Log Exception Cond*/
	uint8_t queue_flags;
#define SCP_QUEUE_ALG_MASK		0xF0
#define SCP_QUEUE_ALG_RESTRICTED	0x00
#define SCP_QUEUE_ALG_UNRESTRICTED	0x10
#define SCP_QUEUE_ERR			0x02	/*Queued I/O aborted for CACs*/
#define SCP_QUEUE_DQUE			0x01	/*Queued I/O disabled*/
	uint8_t eca_and_aen;
#define SCP_EECA			0x80	/*Enable Extended CA*/
#define SCP_RAENP			0x04	/*Ready AEN Permission*/
#define SCP_UAAENP			0x02	/*UA AEN Permission*/
#define SCP_EAENP			0x01	/*Error AEN Permission*/
	uint8_t reserved;
	uint8_t aen_holdoff_period[2];
};

struct scsi_reserve
{
	uint8_t opcode;
	uint8_t byte2;
	uint8_t unused[2];
	uint8_t length;
	uint8_t control;
};

struct scsi_release
{
	uint8_t opcode;
	uint8_t byte2;
	uint8_t unused[2];
	uint8_t length;
	uint8_t control;
};

struct scsi_prevent
{
	uint8_t opcode;
	uint8_t byte2;
	uint8_t unused[2];
	uint8_t how;
	uint8_t control;
};
#define	PR_PREVENT 0x01
#define PR_ALLOW   0x00

struct scsi_sync_cache
{
	uint8_t opcode;
	uint8_t byte2;
	uint8_t begin_lba[4];
	uint8_t reserved;
	uint8_t lb_count[2];
	uint8_t control;	
};


struct scsi_changedef
{
	uint8_t opcode;
	uint8_t byte2;
	uint8_t unused1;
	uint8_t how;
	uint8_t unused[4];
	uint8_t datalen;
	uint8_t control;
};

struct scsi_read_buffer
{
	uint8_t opcode;
	uint8_t byte2;
#define	RWB_MODE		0x07
#define	RWB_MODE_HDR_DATA	0x00
#define	RWB_MODE_DATA		0x02
#define	RWB_MODE_DOWNLOAD	0x04
#define	RWB_MODE_DOWNLOAD_SAVE	0x05
        uint8_t buffer_id;
        uint8_t offset[3];
        uint8_t length[3];
        uint8_t control;
};

struct scsi_write_buffer
{
	uint8_t opcode;
	uint8_t byte2;
	uint8_t buffer_id;
	uint8_t offset[3];
	uint8_t length[3];
	uint8_t control;
};

struct scsi_rw_6
{
	uint8_t opcode;
	uint8_t addr[3];
/* only 5 bits are valid in the MSB address byte */
#define	SRW_TOPADDR	0x1F
	uint8_t length;
	uint8_t control;
};

struct scsi_rw_10
{
	uint8_t opcode;
#define	SRW10_RELADDR	0x01
#define SRW10_FUA	0x08
#define	SRW10_DPO	0x10
	uint8_t byte2;
	uint8_t addr[4];
	uint8_t reserved;
	uint8_t length[2];
	uint8_t control;
};

struct scsi_rw_12
{
	uint8_t opcode;
#define	SRW12_RELADDR	0x01
#define SRW12_FUA	0x08
#define	SRW12_DPO	0x10
	uint8_t byte2;
	uint8_t addr[4];
	uint8_t length[4];
	uint8_t reserved;
	uint8_t control;
};

struct scsi_start_stop_unit
{
	uint8_t opcode;
	uint8_t byte2;
#define	SSS_IMMED		0x01
	uint8_t reserved[2];
	uint8_t how;
#define	SSS_START		0x01
#define	SSS_LOEJ		0x02
	uint8_t control;
};

#define SC_SCSI_1 0x01
#define SC_SCSI_2 0x03

/*
 * Opcodes
 */

#define	TEST_UNIT_READY		0x00
#define REQUEST_SENSE		0x03
#define	READ_6			0x08
#define WRITE_6			0x0a
#define INQUIRY			0x12
#define MODE_SELECT_6		0x15
#define MODE_SENSE_6		0x1a
#define START_STOP_UNIT		0x1b
#define START_STOP		0x1b
#define RESERVE      		0x16
#define RELEASE      		0x17
#define	RECEIVE_DIAGNOSTIC	0x1c
#define	SEND_DIAGNOSTIC		0x1d
#define PREVENT_ALLOW		0x1e
#define	READ_CAPACITY		0x25
#define	READ_10			0x28
#define WRITE_10		0x2a
#define POSITION_TO_ELEMENT	0x2b
#define	SYNCHRONIZE_CACHE	0x35
#define	WRITE_BUFFER            0x3b
#define	READ_BUFFER             0x3c
#define	CHANGE_DEFINITION	0x40
#define	LOG_SELECT		0x4c
#define	LOG_SENSE		0x4d
#ifdef XXXCAM
#define	MODE_SENSE_10		0x5A
#endif
#define	MODE_SELECT_10		0x55
#define MOVE_MEDIUM     	0xa5
#define READ_12			0xa8
#define WRITE_12		0xaa
#define READ_ELEMENT_STATUS	0xb8


/*
 * Device Types
 */
#define T_DIRECT	0x00
#define T_SEQUENTIAL	0x01
#define T_PRINTER	0x02
#define T_PROCESSOR	0x03
#define T_WORM		0x04
#define T_CDROM		0x05
#define T_SCANNER 	0x06
#define T_OPTICAL 	0x07
#define T_CHANGER	0x08
#define T_COMM		0x09
#define T_ASC0		0x0a
#define T_ASC1		0x0b
#define	T_STORARRAY	0x0c
#define	T_ENCLOSURE	0x0d
#define	T_RBC		0x0e
#define	T_OCRW		0x0f
#define T_NODEVICE	0x1F
#define	T_ANY		0xFF	/* Used in Quirk table matches */

#define T_REMOV		1
#define	T_FIXED		0

/*
 * This length is the initial inquiry length used by the probe code, as    
 * well as the legnth necessary for aic_print_inquiry() to function 
 * correctly.  If either use requires a different length in the future, 
 * the two values should be de-coupled.
 */
#define	SHORT_INQUIRY_LENGTH	36

struct scsi_inquiry_data
{
	uint8_t device;
#define	SID_TYPE(inq_data) ((inq_data)->device & 0x1f)
#define	SID_QUAL(inq_data) (((inq_data)->device & 0xE0) >> 5)
#define	SID_QUAL_LU_CONNECTED	0x00	/*
					 * The specified peripheral device
					 * type is currently connected to
					 * logical unit.  If the target cannot
					 * determine whether or not a physical
					 * device is currently connected, it
					 * shall also use this peripheral
					 * qualifier when returning the INQUIRY
					 * data.  This peripheral qualifier
					 * does not mean that the device is
					 * ready for access by the initiator.
					 */
#define	SID_QUAL_LU_OFFLINE	0x01	/*
					 * The target is capable of supporting
					 * the specified peripheral device type
					 * on this logical unit; however, the
					 * physical device is not currently
					 * connected to this logical unit.
					 */
#define SID_QUAL_RSVD		0x02
#define	SID_QUAL_BAD_LU		0x03	/*
					 * The target is not capable of
					 * supporting a physical device on
					 * this logical unit. For this
					 * peripheral qualifier the peripheral
					 * device type shall be set to 1Fh to
					 * provide compatibility with previous
					 * versions of SCSI. All other
					 * peripheral device type values are
					 * reserved for this peripheral
					 * qualifier.
					 */
#define	SID_QUAL_IS_VENDOR_UNIQUE(inq_data) ((SID_QUAL(inq_data) & 0x08) != 0)
	uint8_t dev_qual2;
#define	SID_QUAL2	0x7F
#define	SID_IS_REMOVABLE(inq_data) (((inq_data)->dev_qual2 & 0x80) != 0)
	uint8_t version;
#define SID_ANSI_REV(inq_data) ((inq_data)->version & 0x07)
#define		SCSI_REV_0		0
#define		SCSI_REV_CCS		1
#define		SCSI_REV_2		2
#define		SCSI_REV_SPC		3
#define		SCSI_REV_SPC2		4

#define SID_ECMA	0x38
#define SID_ISO		0xC0
	uint8_t response_format;
#define SID_AENC	0x80
#define SID_TrmIOP	0x40
	uint8_t additional_length;
	uint8_t reserved[2];
	uint8_t flags;
#define	SID_SftRe	0x01
#define	SID_CmdQue	0x02
#define	SID_Linked	0x08
#define	SID_Sync	0x10
#define	SID_WBus16	0x20
#define	SID_WBus32	0x40
#define	SID_RelAdr	0x80
#define SID_VENDOR_SIZE   8
	char	 vendor[SID_VENDOR_SIZE];
#define SID_PRODUCT_SIZE  16
	char	 product[SID_PRODUCT_SIZE];
#define SID_REVISION_SIZE 4
	char	 revision[SID_REVISION_SIZE];
	/*
	 * The following fields were taken from SCSI Primary Commands - 2
	 * (SPC-2) Revision 14, Dated 11 November 1999
	 */
#define	SID_VENDOR_SPECIFIC_0_SIZE	20
	uint8_t vendor_specific0[SID_VENDOR_SPECIFIC_0_SIZE];
	/*
	 * An extension of SCSI Parallel Specific Values
	 */
#define	SID_SPI_IUS		0x01
#define	SID_SPI_QAS		0x02
#define	SID_SPI_CLOCK_ST	0x00
#define	SID_SPI_CLOCK_DT	0x04
#define	SID_SPI_CLOCK_DT_ST	0x0C
#define	SID_SPI_MASK		0x0F
	uint8_t spi3data;
	uint8_t reserved2;
	/*
	 * Version Descriptors, stored 2 byte values.
	 */
	uint8_t version1[2];
	uint8_t version2[2];
	uint8_t version3[2];
	uint8_t version4[2];
	uint8_t version5[2];
	uint8_t version6[2];
	uint8_t version7[2];
	uint8_t version8[2];

	uint8_t reserved3[22];

#define	SID_VENDOR_SPECIFIC_1_SIZE	160
	uint8_t vendor_specific1[SID_VENDOR_SPECIFIC_1_SIZE];
};

struct scsi_vpd_unit_serial_number
{
	uint8_t device;
	uint8_t page_code;
#define SVPD_UNIT_SERIAL_NUMBER	0x80
	uint8_t reserved;
	uint8_t length; /* serial number length */
#define SVPD_SERIAL_NUM_SIZE 251
	uint8_t serial_num[SVPD_SERIAL_NUM_SIZE];
};

struct scsi_read_capacity
{
	uint8_t opcode;
	uint8_t byte2;
	uint8_t addr[4];
	uint8_t unused[3];
	uint8_t control;
};

struct scsi_read_capacity_data
{
	uint8_t addr[4];
	uint8_t length[4];
};

struct scsi_report_luns
{
	uint8_t opcode;
	uint8_t byte2;
	uint8_t unused[3];
	uint8_t addr[4];
	uint8_t control;
};

struct scsi_report_luns_data {
	uint8_t length[4];	/* length of LUN inventory, in bytes */
	uint8_t reserved[4];	/* unused */
	/*
	 * LUN inventory- we only support the type zero form for now.
	 */
	struct {
		uint8_t lundata[8];
	} luns[1];
};
#define	RPL_LUNDATA_ATYP_MASK	0xc0	/* MBZ for type 0 lun */
#define	RPL_LUNDATA_T0LUN	1	/* @ lundata[1] */


struct scsi_sense_data
{
	uint8_t error_code;
#define	SSD_ERRCODE			0x7F
#define		SSD_CURRENT_ERROR	0x70
#define		SSD_DEFERRED_ERROR	0x71
#define	SSD_ERRCODE_VALID	0x80	
	uint8_t segment;
	uint8_t flags;
#define	SSD_KEY				0x0F
#define		SSD_KEY_NO_SENSE	0x00
#define		SSD_KEY_RECOVERED_ERROR	0x01
#define		SSD_KEY_NOT_READY	0x02
#define		SSD_KEY_MEDIUM_ERROR	0x03
#define		SSD_KEY_HARDWARE_ERROR	0x04
#define		SSD_KEY_ILLEGAL_REQUEST	0x05
#define		SSD_KEY_UNIT_ATTENTION	0x06
#define		SSD_KEY_DATA_PROTECT	0x07
#define		SSD_KEY_BLANK_CHECK	0x08
#define		SSD_KEY_Vendor_Specific	0x09
#define		SSD_KEY_COPY_ABORTED	0x0a
#define		SSD_KEY_ABORTED_COMMAND	0x0b		
#define		SSD_KEY_EQUAL		0x0c
#define		SSD_KEY_VOLUME_OVERFLOW	0x0d
#define		SSD_KEY_MISCOMPARE	0x0e
#define		SSD_KEY_RESERVED	0x0f			
#define	SSD_ILI		0x20
#define	SSD_EOM		0x40
#define	SSD_FILEMARK	0x80
	uint8_t info[4];
	uint8_t extra_len;
	uint8_t cmd_spec_info[4];
	uint8_t add_sense_code;
	uint8_t add_sense_code_qual;
	uint8_t fru;
	uint8_t sense_key_spec[3];
#define	SSD_SCS_VALID		0x80
#define SSD_FIELDPTR_CMD	0x40
#define SSD_BITPTR_VALID	0x08
#define SSD_BITPTR_VALUE	0x07
#define SSD_MIN_SIZE 18
	uint8_t extra_bytes[14];
#define SSD_FULL_SIZE sizeof(struct scsi_sense_data)
};

struct scsi_mode_header_6
{
	uint8_t data_length;	/* Sense data length */
	uint8_t medium_type;
	uint8_t dev_spec;
	uint8_t blk_desc_len;
};

struct scsi_mode_header_10
{
	uint8_t data_length[2];/* Sense data length */
	uint8_t medium_type;
	uint8_t dev_spec;
	uint8_t unused[2];
	uint8_t blk_desc_len[2];
};

struct scsi_mode_page_header
{
	uint8_t page_code;
	uint8_t page_length;
};

struct scsi_mode_blk_desc
{
	uint8_t density;
	uint8_t nblocks[3];
	uint8_t reserved;
	uint8_t blklen[3];
};

#define	SCSI_DEFAULT_DENSITY	0x00	/* use 'default' density */
#define	SCSI_SAME_DENSITY	0x7f	/* use 'same' density- >= SCSI-2 only */


/*
 * Status Byte
 */
#define	SCSI_STATUS_OK			0x00
#define	SCSI_STATUS_CHECK_COND		0x02
#define	SCSI_STATUS_COND_MET		0x04
#define	SCSI_STATUS_BUSY		0x08
#define SCSI_STATUS_INTERMED		0x10
#define SCSI_STATUS_INTERMED_COND_MET	0x14
#define SCSI_STATUS_RESERV_CONFLICT	0x18
#define SCSI_STATUS_CMD_TERMINATED	0x22	/* Obsolete in SAM-2 */
#define SCSI_STATUS_QUEUE_FULL		0x28
#define SCSI_STATUS_ACA_ACTIVE		0x30
#define SCSI_STATUS_TASK_ABORTED	0x40

struct scsi_inquiry_pattern {
	uint8_t   type;
	uint8_t   media_type;
#define	SIP_MEDIA_REMOVABLE	0x01
#define	SIP_MEDIA_FIXED		0x02
	const char *vendor;
	const char *product;
	const char *revision;
}; 

struct scsi_static_inquiry_pattern {
	uint8_t   type;
	uint8_t   media_type;
	char       vendor[SID_VENDOR_SIZE+1];
	char       product[SID_PRODUCT_SIZE+1];
	char       revision[SID_REVISION_SIZE+1];
};

struct scsi_sense_quirk_entry {
	struct scsi_inquiry_pattern	inq_pat;
	int				num_sense_keys;
	int				num_ascs;
	struct sense_key_table_entry	*sense_key_info;
	struct asc_table_entry		*asc_info;
};

struct sense_key_table_entry {
	uint8_t    sense_key;
	uint32_t   action;
	const char *desc;
};

struct asc_table_entry {
	uint8_t    asc;
	uint8_t    ascq;
	uint32_t   action;
	const char *desc;
};

struct op_table_entry {
	uint8_t    opcode;
	uint16_t   opmask;
	const char  *desc;
};

struct scsi_op_quirk_entry {
	struct scsi_inquiry_pattern	inq_pat;
	int				num_ops;
	struct op_table_entry		*op_table;
};

typedef enum {
	SSS_FLAG_NONE		= 0x00,
	SSS_FLAG_PRINT_COMMAND	= 0x01
} scsi_sense_string_flags;

extern const char *scsi_sense_key_text[];

/************************* Large Disk Handling ********************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
static __inline int aic_sector_div(u_long capacity, int heads, int sectors);

static __inline int
aic_sector_div(u_long capacity, int heads, int sectors)
{
	return (capacity / (heads * sectors));
}
#else
static __inline int aic_sector_div(sector_t capacity, int heads, int sectors);

static __inline int
aic_sector_div(sector_t capacity, int heads, int sectors)
{
	/* ugly, ugly sector_div calling convention.. */
	sector_div(capacity, (heads * sectors));
	return (int)capacity;
}
#endif

/**************************** Module Library Hack *****************************/
/*
 * What we'd like to do is have a single "scsi library" module that both the
 * aic7xxx and aic79xx drivers could load and depend on.  A cursory examination
 * of implementing module dependencies in Linux (handling the install and
 * initrd cases) does not look promissing.  For now, we just duplicate this
 * code in both drivers using a simple symbol renaming scheme that hides this
 * hack from the drivers.
 */
#define AIC_LIB_ENTRY_CONCAT(x, prefix)	prefix ## x
#define	AIC_LIB_ENTRY_EXPAND(x, prefix) AIC_LIB_ENTRY_CONCAT(x, prefix)
#define AIC_LIB_ENTRY(x)		AIC_LIB_ENTRY_EXPAND(x, AIC_LIB_PREFIX)

#define	aic_sense_desc			AIC_LIB_ENTRY(_sense_desc)
#define	aic_sense_error_action		AIC_LIB_ENTRY(_sense_error_action)
#define	aic_error_action		AIC_LIB_ENTRY(_error_action)
#define	aic_op_desc			AIC_LIB_ENTRY(_op_desc)
#define	aic_cdb_string			AIC_LIB_ENTRY(_cdb_string)
#define aic_print_inquiry		AIC_LIB_ENTRY(_print_inquiry)
#define aic_calc_syncsrate		AIC_LIB_ENTRY(_calc_syncrate)
#define	aic_calc_syncparam		AIC_LIB_ENTRY(_calc_syncparam)
#define	aic_calc_speed			AIC_LIB_ENTRY(_calc_speed)
#define	aic_inquiry_match		AIC_LIB_ENTRY(_inquiry_match)
#define	aic_static_inquiry_match	AIC_LIB_ENTRY(_static_inquiry_match)
#define	aic_parse_brace_option		AIC_LIB_ENTRY(_parse_brace_option)

/******************************************************************************/

void			aic_sense_desc(int /*sense_key*/, int /*asc*/,
				       int /*ascq*/, struct scsi_inquiry_data*,
				       const char** /*sense_key_desc*/,
				       const char** /*asc_desc*/);
aic_sense_action	aic_sense_error_action(struct scsi_sense_data*,
					       struct scsi_inquiry_data*,
					       uint32_t /*sense_flags*/);
uint32_t		aic_error_action(struct scsi_cmnd *,
					 struct scsi_inquiry_data *,
					 cam_status, u_int);

#define	SF_RETRY_UA	0x01
#define SF_NO_PRINT	0x02
#define SF_QUIET_IR	0x04	/* Be quiet about Illegal Request reponses */
#define SF_PRINT_ALWAYS	0x08


const char *	aic_op_desc(uint16_t /*opcode*/, struct scsi_inquiry_data*);
char *		aic_cdb_string(uint8_t* /*cdb_ptr*/, char* /*cdb_string*/,
			       size_t /*len*/);
void		aic_print_inquiry(struct scsi_inquiry_data*);

u_int		aic_calc_syncsrate(u_int /*period_factor*/);
u_int		aic_calc_syncparam(u_int /*period*/);
u_int		aic_calc_speed(u_int width, u_int period, u_int offset,
			       u_int min_rate);
	
int		aic_inquiry_match(caddr_t /*inqbuffer*/,
				  caddr_t /*table_entry*/);
int		aic_static_inquiry_match(caddr_t /*inqbuffer*/,
					 caddr_t /*table_entry*/);

typedef void aic_option_callback_t(u_long, int, int, int32_t);
char *		aic_parse_brace_option(char *opt_name, char *opt_arg,
				       char *end, int depth,
				       aic_option_callback_t *, u_long);

static __inline void	 scsi_extract_sense(struct scsi_sense_data *sense,
					    int *error_code, int *sense_key,
					    int *asc, int *ascq);
static __inline void	 scsi_ulto2b(uint32_t val, uint8_t *bytes);
static __inline void	 scsi_ulto3b(uint32_t val, uint8_t *bytes);
static __inline void	 scsi_ulto4b(uint32_t val, uint8_t *bytes);
static __inline uint32_t scsi_2btoul(uint8_t *bytes);
static __inline uint32_t scsi_3btoul(uint8_t *bytes);
static __inline int32_t	 scsi_3btol(uint8_t *bytes);
static __inline uint32_t scsi_4btoul(uint8_t *bytes);

static __inline void scsi_extract_sense(struct scsi_sense_data *sense,
				       int *error_code, int *sense_key,
				       int *asc, int *ascq)
{
	*error_code = sense->error_code & SSD_ERRCODE;
	*sense_key = sense->flags & SSD_KEY;
	*asc = (sense->extra_len >= 5) ? sense->add_sense_code : 0;
	*ascq = (sense->extra_len >= 6) ? sense->add_sense_code_qual : 0;
}

static __inline void
scsi_ulto2b(uint32_t val, uint8_t *bytes)
{

	bytes[0] = (val >> 8) & 0xff;
	bytes[1] = val & 0xff;
}

static __inline void
scsi_ulto3b(uint32_t val, uint8_t *bytes)
{

	bytes[0] = (val >> 16) & 0xff;
	bytes[1] = (val >> 8) & 0xff;
	bytes[2] = val & 0xff;
}

static __inline void
scsi_ulto4b(uint32_t val, uint8_t *bytes)
{

	bytes[0] = (val >> 24) & 0xff;
	bytes[1] = (val >> 16) & 0xff;
	bytes[2] = (val >> 8) & 0xff;
	bytes[3] = val & 0xff;
}

static __inline uint32_t
scsi_2btoul(uint8_t *bytes)
{
	uint32_t rv;

	rv = (bytes[0] << 8) |
	     bytes[1];
	return (rv);
}

static __inline uint32_t
scsi_3btoul(uint8_t *bytes)
{
	uint32_t rv;

	rv = (bytes[0] << 16) |
	     (bytes[1] << 8) |
	     bytes[2];
	return (rv);
}

static __inline int32_t 
scsi_3btol(uint8_t *bytes)
{
	uint32_t rc = scsi_3btoul(bytes);
 
	if (rc & 0x00800000)
		rc |= 0xff000000;

	return (int32_t) rc;
}

static __inline uint32_t
scsi_4btoul(uint8_t *bytes)
{
	uint32_t rv;

	rv = (bytes[0] << 24) |
	     (bytes[1] << 16) |
	     (bytes[2] << 8) |
	     bytes[3];
	return (rv);
}

/* Macros for generating the elements of the PCI ID tables. */

#define GETID(v, s) (unsigned)(((v) >> (s)) & 0xFFFF ?: PCI_ANY_ID)

#define ID_C(x, c)						\
{								\
	GETID(x,32), GETID(x,48), GETID(x,0), GETID(x,16),	\
	(c) << 8, 0xFFFF00, 0					\
}

#define ID2C(x)                          \
	ID_C(x, PCI_CLASS_STORAGE_SCSI), \
	ID_C(x, PCI_CLASS_STORAGE_RAID)

#define IDIROC(x)  ((x) | ~ID_ALL_IROC_MASK)

/* Generate IDs for all 16 possibilites.
 * The argument has already masked out
 * the 4 least significant bits of the device id.
 * (e.g., mask: ID_9005_GENERIC_MASK).
 */
#define ID16(x)                          \
	ID(x),                           \
	ID((x) | 0x0001000000000000ull), \
	ID((x) | 0x0002000000000000ull), \
	ID((x) | 0x0003000000000000ull), \
	ID((x) | 0x0004000000000000ull), \
	ID((x) | 0x0005000000000000ull), \
	ID((x) | 0x0006000000000000ull), \
	ID((x) | 0x0007000000000000ull), \
	ID((x) | 0x0008000000000000ull), \
	ID((x) | 0x0009000000000000ull), \
	ID((x) | 0x000A000000000000ull), \
	ID((x) | 0x000B000000000000ull), \
	ID((x) | 0x000C000000000000ull), \
	ID((x) | 0x000D000000000000ull), \
	ID((x) | 0x000E000000000000ull), \
	ID((x) | 0x000F000000000000ull)

#endif /*_AICLIB_H */
