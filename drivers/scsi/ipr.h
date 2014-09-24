/*
 * ipr.h -- driver for IBM Power Linux RAID adapters
 *
 * Written By: Brian King <brking@us.ibm.com>, IBM Corporation
 *
 * Copyright (C) 2003, 2004 IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Alan Cox <alan@lxorguk.ukuu.org.uk> - Removed several careless u32/dma_addr_t errors
 *				that broke 64bit platforms.
 */

#ifndef _IPR_H
#define _IPR_H

#include <asm/unaligned.h>
#include <linux/types.h>
#include <linux/completion.h>
#include <linux/libata.h>
#include <linux/list.h>
#include <linux/kref.h>
#include <linux/blk-iopoll.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>

/*
 * Literals
 */
#define IPR_DRIVER_VERSION "2.6.0"
#define IPR_DRIVER_DATE "(November 16, 2012)"

/*
 * IPR_MAX_CMD_PER_LUN: This defines the maximum number of outstanding
 *	ops per device for devices not running tagged command queuing.
 *	This can be adjusted at runtime through sysfs device attributes.
 */
#define IPR_MAX_CMD_PER_LUN				6
#define IPR_MAX_CMD_PER_ATA_LUN			1

/*
 * IPR_NUM_BASE_CMD_BLKS: This defines the maximum number of
 *	ops the mid-layer can send to the adapter.
 */
#define IPR_NUM_BASE_CMD_BLKS			(ioa_cfg->max_cmds)

#define PCI_DEVICE_ID_IBM_OBSIDIAN_E	0x0339

#define PCI_DEVICE_ID_IBM_CROC_FPGA_E2          0x033D
#define PCI_DEVICE_ID_IBM_CROCODILE             0x034A

#define IPR_SUBS_DEV_ID_2780	0x0264
#define IPR_SUBS_DEV_ID_5702	0x0266
#define IPR_SUBS_DEV_ID_5703	0x0278
#define IPR_SUBS_DEV_ID_572E	0x028D
#define IPR_SUBS_DEV_ID_573E	0x02D3
#define IPR_SUBS_DEV_ID_573D	0x02D4
#define IPR_SUBS_DEV_ID_571A	0x02C0
#define IPR_SUBS_DEV_ID_571B	0x02BE
#define IPR_SUBS_DEV_ID_571E	0x02BF
#define IPR_SUBS_DEV_ID_571F	0x02D5
#define IPR_SUBS_DEV_ID_572A	0x02C1
#define IPR_SUBS_DEV_ID_572B	0x02C2
#define IPR_SUBS_DEV_ID_572F	0x02C3
#define IPR_SUBS_DEV_ID_574E	0x030A
#define IPR_SUBS_DEV_ID_575B	0x030D
#define IPR_SUBS_DEV_ID_575C	0x0338
#define IPR_SUBS_DEV_ID_57B3	0x033A
#define IPR_SUBS_DEV_ID_57B7	0x0360
#define IPR_SUBS_DEV_ID_57B8	0x02C2

#define IPR_SUBS_DEV_ID_57B4    0x033B
#define IPR_SUBS_DEV_ID_57B2    0x035F
#define IPR_SUBS_DEV_ID_57C0    0x0352
#define IPR_SUBS_DEV_ID_57C3    0x0353
#define IPR_SUBS_DEV_ID_57C4    0x0354
#define IPR_SUBS_DEV_ID_57C6    0x0357
#define IPR_SUBS_DEV_ID_57CC    0x035C

#define IPR_SUBS_DEV_ID_57B5    0x033C
#define IPR_SUBS_DEV_ID_57CE    0x035E
#define IPR_SUBS_DEV_ID_57B1    0x0355

#define IPR_SUBS_DEV_ID_574D    0x0356
#define IPR_SUBS_DEV_ID_57C8    0x035D

#define IPR_SUBS_DEV_ID_57D5    0x03FB
#define IPR_SUBS_DEV_ID_57D6    0x03FC
#define IPR_SUBS_DEV_ID_57D7    0x03FF
#define IPR_SUBS_DEV_ID_57D8    0x03FE
#define IPR_SUBS_DEV_ID_57D9    0x046D
#define IPR_SUBS_DEV_ID_57DA    0x04CA
#define IPR_SUBS_DEV_ID_57EB    0x0474
#define IPR_SUBS_DEV_ID_57EC    0x0475
#define IPR_SUBS_DEV_ID_57ED    0x0499
#define IPR_SUBS_DEV_ID_57EE    0x049A
#define IPR_SUBS_DEV_ID_57EF    0x049B
#define IPR_SUBS_DEV_ID_57F0    0x049C
#define IPR_SUBS_DEV_ID_2CCA	0x04C7
#define IPR_SUBS_DEV_ID_2CD2	0x04C8
#define IPR_SUBS_DEV_ID_2CCD	0x04C9
#define IPR_NAME				"ipr"

/*
 * Return codes
 */
#define IPR_RC_JOB_CONTINUE		1
#define IPR_RC_JOB_RETURN		2

/*
 * IOASCs
 */
#define IPR_IOASC_NR_INIT_CMD_REQUIRED		0x02040200
#define IPR_IOASC_NR_IOA_RESET_REQUIRED		0x02048000
#define IPR_IOASC_SYNC_REQUIRED			0x023f0000
#define IPR_IOASC_MED_DO_NOT_REALLOC		0x03110C00
#define IPR_IOASC_HW_SEL_TIMEOUT			0x04050000
#define IPR_IOASC_HW_DEV_BUS_STATUS			0x04448500
#define	IPR_IOASC_IOASC_MASK			0xFFFFFF00
#define	IPR_IOASC_SCSI_STATUS_MASK		0x000000FF
#define IPR_IOASC_HW_CMD_FAILEd			0x046E0000
#define IPR_IOASC_IR_INVALID_REQ_TYPE_OR_PKT	0x05240000
#define IPR_IOASC_IR_RESOURCE_HANDLE		0x05250000
#define IPR_IOASC_IR_NO_CMDS_TO_2ND_IOA		0x05258100
#define IPR_IOASA_IR_DUAL_IOA_DISABLED		0x052C8000
#define IPR_IOASC_BUS_WAS_RESET			0x06290000
#define IPR_IOASC_BUS_WAS_RESET_BY_OTHER		0x06298000
#define IPR_IOASC_ABORTED_CMD_TERM_BY_HOST	0x0B5A0000

#define IPR_FIRST_DRIVER_IOASC			0x10000000
#define IPR_IOASC_IOA_WAS_RESET			0x10000001
#define IPR_IOASC_PCI_ACCESS_ERROR			0x10000002

/* Driver data flags */
#define IPR_USE_LONG_TRANSOP_TIMEOUT		0x00000001
#define IPR_USE_PCI_WARM_RESET			0x00000002

#define IPR_DEFAULT_MAX_ERROR_DUMP			984
#define IPR_NUM_LOG_HCAMS				2
#define IPR_NUM_CFG_CHG_HCAMS				2
#define IPR_NUM_HCAMS	(IPR_NUM_LOG_HCAMS + IPR_NUM_CFG_CHG_HCAMS)

#define IPR_MAX_SIS64_TARGETS_PER_BUS			1024
#define IPR_MAX_SIS64_LUNS_PER_TARGET			0xffffffff

#define IPR_MAX_NUM_TARGETS_PER_BUS			256
#define IPR_MAX_NUM_LUNS_PER_TARGET			256
#define IPR_MAX_NUM_VSET_LUNS_PER_TARGET	8
#define IPR_VSET_BUS					0xff
#define IPR_IOA_BUS						0xff
#define IPR_IOA_TARGET					0xff
#define IPR_IOA_LUN						0xff
#define IPR_MAX_NUM_BUSES				16
#define IPR_MAX_BUS_TO_SCAN				IPR_MAX_NUM_BUSES

#define IPR_NUM_RESET_RELOAD_RETRIES		3

/* We need resources for HCAMS, IOA reset, IOA bringdown, and ERP */
#define IPR_NUM_INTERNAL_CMD_BLKS	(IPR_NUM_HCAMS + \
                                     ((IPR_NUM_RESET_RELOAD_RETRIES + 1) * 2) + 4)

#define IPR_MAX_COMMANDS		100
#define IPR_NUM_CMD_BLKS		(IPR_NUM_BASE_CMD_BLKS + \
						IPR_NUM_INTERNAL_CMD_BLKS)

#define IPR_MAX_PHYSICAL_DEVS				192
#define IPR_DEFAULT_SIS64_DEVS				1024
#define IPR_MAX_SIS64_DEVS				4096

#define IPR_MAX_SGLIST					64
#define IPR_IOA_MAX_SECTORS				32767
#define IPR_VSET_MAX_SECTORS				512
#define IPR_MAX_CDB_LEN					16
#define IPR_MAX_HRRQ_RETRIES				3

#define IPR_DEFAULT_BUS_WIDTH				16
#define IPR_80MBs_SCSI_RATE		((80 * 10) / (IPR_DEFAULT_BUS_WIDTH / 8))
#define IPR_U160_SCSI_RATE	((160 * 10) / (IPR_DEFAULT_BUS_WIDTH / 8))
#define IPR_U320_SCSI_RATE	((320 * 10) / (IPR_DEFAULT_BUS_WIDTH / 8))
#define IPR_MAX_SCSI_RATE(width) ((320 * 10) / ((width) / 8))

#define IPR_IOA_RES_HANDLE				0xffffffff
#define IPR_INVALID_RES_HANDLE			0
#define IPR_IOA_RES_ADDR				0x00ffffff

/*
 * Adapter Commands
 */
#define IPR_QUERY_RSRC_STATE				0xC2
#define IPR_RESET_DEVICE				0xC3
#define	IPR_RESET_TYPE_SELECT				0x80
#define	IPR_LUN_RESET					0x40
#define	IPR_TARGET_RESET					0x20
#define	IPR_BUS_RESET					0x10
#define	IPR_ATA_PHY_RESET					0x80
#define IPR_ID_HOST_RR_Q				0xC4
#define IPR_QUERY_IOA_CONFIG				0xC5
#define IPR_CANCEL_ALL_REQUESTS			0xCE
#define IPR_HOST_CONTROLLED_ASYNC			0xCF
#define	IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE	0x01
#define	IPR_HCAM_CDB_OP_CODE_LOG_DATA		0x02
#define IPR_SET_SUPPORTED_DEVICES			0xFB
#define IPR_SET_ALL_SUPPORTED_DEVICES			0x80
#define IPR_IOA_SHUTDOWN				0xF7
#define	IPR_WR_BUF_DOWNLOAD_AND_SAVE			0x05

/*
 * Timeouts
 */
#define IPR_SHUTDOWN_TIMEOUT			(ipr_fastfail ? 60 * HZ : 10 * 60 * HZ)
#define IPR_VSET_RW_TIMEOUT			(ipr_fastfail ? 30 * HZ : 2 * 60 * HZ)
#define IPR_ABBREV_SHUTDOWN_TIMEOUT		(10 * HZ)
#define IPR_DUAL_IOA_ABBR_SHUTDOWN_TO	(2 * 60 * HZ)
#define IPR_DEVICE_RESET_TIMEOUT		(ipr_fastfail ? 10 * HZ : 30 * HZ)
#define IPR_CANCEL_ALL_TIMEOUT		(ipr_fastfail ? 10 * HZ : 30 * HZ)
#define IPR_ABORT_TASK_TIMEOUT		(ipr_fastfail ? 10 * HZ : 30 * HZ)
#define IPR_INTERNAL_TIMEOUT			(ipr_fastfail ? 10 * HZ : 30 * HZ)
#define IPR_WRITE_BUFFER_TIMEOUT		(30 * 60 * HZ)
#define IPR_SET_SUP_DEVICE_TIMEOUT		(2 * 60 * HZ)
#define IPR_REQUEST_SENSE_TIMEOUT		(10 * HZ)
#define IPR_OPERATIONAL_TIMEOUT		(5 * 60)
#define IPR_LONG_OPERATIONAL_TIMEOUT	(12 * 60)
#define IPR_WAIT_FOR_RESET_TIMEOUT		(2 * HZ)
#define IPR_CHECK_FOR_RESET_TIMEOUT		(HZ / 10)
#define IPR_WAIT_FOR_BIST_TIMEOUT		(2 * HZ)
#define IPR_PCI_ERROR_RECOVERY_TIMEOUT	(120 * HZ)
#define IPR_PCI_RESET_TIMEOUT			(HZ / 2)
#define IPR_SIS32_DUMP_TIMEOUT			(15 * HZ)
#define IPR_SIS64_DUMP_TIMEOUT			(40 * HZ)
#define IPR_DUMP_DELAY_SECONDS			4
#define IPR_DUMP_DELAY_TIMEOUT			(IPR_DUMP_DELAY_SECONDS * HZ)

/*
 * SCSI Literals
 */
#define IPR_VENDOR_ID_LEN			8
#define IPR_PROD_ID_LEN				16
#define IPR_SERIAL_NUM_LEN			8

/*
 * Hardware literals
 */
#define IPR_FMT2_MBX_ADDR_MASK				0x0fffffff
#define IPR_FMT2_MBX_BAR_SEL_MASK			0xf0000000
#define IPR_FMT2_MKR_BAR_SEL_SHIFT			28
#define IPR_GET_FMT2_BAR_SEL(mbx) \
(((mbx) & IPR_FMT2_MBX_BAR_SEL_MASK) >> IPR_FMT2_MKR_BAR_SEL_SHIFT)
#define IPR_SDT_FMT2_BAR0_SEL				0x0
#define IPR_SDT_FMT2_BAR1_SEL				0x1
#define IPR_SDT_FMT2_BAR2_SEL				0x2
#define IPR_SDT_FMT2_BAR3_SEL				0x3
#define IPR_SDT_FMT2_BAR4_SEL				0x4
#define IPR_SDT_FMT2_BAR5_SEL				0x5
#define IPR_SDT_FMT2_EXP_ROM_SEL			0x8
#define IPR_FMT2_SDT_READY_TO_USE			0xC4D4E3F2
#define IPR_FMT3_SDT_READY_TO_USE			0xC4D4E3F3
#define IPR_DOORBELL					0x82800000
#define IPR_RUNTIME_RESET				0x40000000

#define IPR_IPL_INIT_MIN_STAGE_TIME			5
#define IPR_IPL_INIT_DEFAULT_STAGE_TIME                 15
#define IPR_IPL_INIT_STAGE_UNKNOWN			0x0
#define IPR_IPL_INIT_STAGE_TRANSOP			0xB0000000
#define IPR_IPL_INIT_STAGE_MASK				0xff000000
#define IPR_IPL_INIT_STAGE_TIME_MASK			0x0000ffff
#define IPR_PCII_IPL_STAGE_CHANGE			(0x80000000 >> 0)

#define IPR_PCII_IOA_TRANS_TO_OPER			(0x80000000 >> 0)
#define IPR_PCII_IOARCB_XFER_FAILED			(0x80000000 >> 3)
#define IPR_PCII_IOA_UNIT_CHECKED			(0x80000000 >> 4)
#define IPR_PCII_NO_HOST_RRQ				(0x80000000 >> 5)
#define IPR_PCII_CRITICAL_OPERATION			(0x80000000 >> 6)
#define IPR_PCII_IO_DEBUG_ACKNOWLEDGE		(0x80000000 >> 7)
#define IPR_PCII_IOARRIN_LOST				(0x80000000 >> 27)
#define IPR_PCII_MMIO_ERROR				(0x80000000 >> 28)
#define IPR_PCII_PROC_ERR_STATE			(0x80000000 >> 29)
#define IPR_PCII_HRRQ_UPDATED				(0x80000000 >> 30)
#define IPR_PCII_CORE_ISSUED_RST_REQ		(0x80000000 >> 31)

#define IPR_PCII_ERROR_INTERRUPTS \
(IPR_PCII_IOARCB_XFER_FAILED | IPR_PCII_IOA_UNIT_CHECKED | \
IPR_PCII_NO_HOST_RRQ | IPR_PCII_IOARRIN_LOST | IPR_PCII_MMIO_ERROR)

#define IPR_PCII_OPER_INTERRUPTS \
(IPR_PCII_ERROR_INTERRUPTS | IPR_PCII_HRRQ_UPDATED | IPR_PCII_IOA_TRANS_TO_OPER)

#define IPR_UPROCI_RESET_ALERT			(0x80000000 >> 7)
#define IPR_UPROCI_IO_DEBUG_ALERT			(0x80000000 >> 9)
#define IPR_UPROCI_SIS64_START_BIST			(0x80000000 >> 23)

#define IPR_LDUMP_MAX_LONG_ACK_DELAY_IN_USEC		200000	/* 200 ms */
#define IPR_LDUMP_MAX_SHORT_ACK_DELAY_IN_USEC		200000	/* 200 ms */

/*
 * Dump literals
 */
#define IPR_FMT2_MAX_IOA_DUMP_SIZE			(4 * 1024 * 1024)
#define IPR_FMT3_MAX_IOA_DUMP_SIZE			(80 * 1024 * 1024)
#define IPR_FMT2_NUM_SDT_ENTRIES			511
#define IPR_FMT3_NUM_SDT_ENTRIES			0xFFF
#define IPR_FMT2_MAX_NUM_DUMP_PAGES	((IPR_FMT2_MAX_IOA_DUMP_SIZE / PAGE_SIZE) + 1)
#define IPR_FMT3_MAX_NUM_DUMP_PAGES	((IPR_FMT3_MAX_IOA_DUMP_SIZE / PAGE_SIZE) + 1)

/*
 * Misc literals
 */
#define IPR_NUM_IOADL_ENTRIES			IPR_MAX_SGLIST
#define IPR_MAX_MSIX_VECTORS		0x10
#define IPR_MAX_HRRQ_NUM		0x10
#define IPR_INIT_HRRQ			0x0

/*
 * Adapter interface types
 */

struct ipr_res_addr {
	u8 reserved;
	u8 bus;
	u8 target;
	u8 lun;
#define IPR_GET_PHYS_LOC(res_addr) \
	(((res_addr).bus << 16) | ((res_addr).target << 8) | (res_addr).lun)
}__attribute__((packed, aligned (4)));

struct ipr_std_inq_vpids {
	u8 vendor_id[IPR_VENDOR_ID_LEN];
	u8 product_id[IPR_PROD_ID_LEN];
}__attribute__((packed));

struct ipr_vpd {
	struct ipr_std_inq_vpids vpids;
	u8 sn[IPR_SERIAL_NUM_LEN];
}__attribute__((packed));

struct ipr_ext_vpd {
	struct ipr_vpd vpd;
	__be32 wwid[2];
}__attribute__((packed));

struct ipr_ext_vpd64 {
	struct ipr_vpd vpd;
	__be32 wwid[4];
}__attribute__((packed));

struct ipr_std_inq_data {
	u8 peri_qual_dev_type;
#define IPR_STD_INQ_PERI_QUAL(peri) ((peri) >> 5)
#define IPR_STD_INQ_PERI_DEV_TYPE(peri) ((peri) & 0x1F)

	u8 removeable_medium_rsvd;
#define IPR_STD_INQ_REMOVEABLE_MEDIUM 0x80

#define IPR_IS_DASD_DEVICE(std_inq) \
((IPR_STD_INQ_PERI_DEV_TYPE((std_inq).peri_qual_dev_type) == TYPE_DISK) && \
!(((std_inq).removeable_medium_rsvd) & IPR_STD_INQ_REMOVEABLE_MEDIUM))

#define IPR_IS_SES_DEVICE(std_inq) \
(IPR_STD_INQ_PERI_DEV_TYPE((std_inq).peri_qual_dev_type) == TYPE_ENCLOSURE)

	u8 version;
	u8 aen_naca_fmt;
	u8 additional_len;
	u8 sccs_rsvd;
	u8 bq_enc_multi;
	u8 sync_cmdq_flags;

	struct ipr_std_inq_vpids vpids;

	u8 ros_rsvd_ram_rsvd[4];

	u8 serial_num[IPR_SERIAL_NUM_LEN];
}__attribute__ ((packed));

#define IPR_RES_TYPE_AF_DASD		0x00
#define IPR_RES_TYPE_GENERIC_SCSI	0x01
#define IPR_RES_TYPE_VOLUME_SET		0x02
#define IPR_RES_TYPE_REMOTE_AF_DASD	0x03
#define IPR_RES_TYPE_GENERIC_ATA	0x04
#define IPR_RES_TYPE_ARRAY		0x05
#define IPR_RES_TYPE_IOAFP		0xff

struct ipr_config_table_entry {
	u8 proto;
#define IPR_PROTO_SATA			0x02
#define IPR_PROTO_SATA_ATAPI		0x03
#define IPR_PROTO_SAS_STP		0x06
#define IPR_PROTO_SAS_STP_ATAPI		0x07
	u8 array_id;
	u8 flags;
#define IPR_IS_IOA_RESOURCE		0x80
	u8 rsvd_subtype;

#define IPR_QUEUEING_MODEL(res)	((((res)->flags) & 0x70) >> 4)
#define IPR_QUEUE_FROZEN_MODEL		0
#define IPR_QUEUE_NACA_MODEL		1

	struct ipr_res_addr res_addr;
	__be32 res_handle;
	__be32 lun_wwn[2];
	struct ipr_std_inq_data std_inq_data;
}__attribute__ ((packed, aligned (4)));

struct ipr_config_table_entry64 {
	u8 res_type;
	u8 proto;
	u8 vset_num;
	u8 array_id;
	__be16 flags;
	__be16 res_flags;
#define IPR_QUEUEING_MODEL64(res) ((((res)->res_flags) & 0x7000) >> 12)
	__be32 res_handle;
	u8 dev_id_type;
	u8 reserved[3];
	__be64 dev_id;
	__be64 lun;
	__be64 lun_wwn[2];
#define IPR_MAX_RES_PATH_LENGTH		48
	__be64 res_path;
	struct ipr_std_inq_data std_inq_data;
	u8 reserved2[4];
	__be64 reserved3[2];
	u8 reserved4[8];
}__attribute__ ((packed, aligned (8)));

struct ipr_config_table_hdr {
	u8 num_entries;
	u8 flags;
#define IPR_UCODE_DOWNLOAD_REQ	0x10
	__be16 reserved;
}__attribute__((packed, aligned (4)));

struct ipr_config_table_hdr64 {
	__be16 num_entries;
	__be16 reserved;
	u8 flags;
	u8 reserved2[11];
}__attribute__((packed, aligned (4)));

struct ipr_config_table {
	struct ipr_config_table_hdr hdr;
	struct ipr_config_table_entry dev[0];
}__attribute__((packed, aligned (4)));

struct ipr_config_table64 {
	struct ipr_config_table_hdr64 hdr64;
	struct ipr_config_table_entry64 dev[0];
}__attribute__((packed, aligned (8)));

struct ipr_config_table_entry_wrapper {
	union {
		struct ipr_config_table_entry *cfgte;
		struct ipr_config_table_entry64 *cfgte64;
	} u;
};

struct ipr_hostrcb_cfg_ch_not {
	union {
		struct ipr_config_table_entry cfgte;
		struct ipr_config_table_entry64 cfgte64;
	} u;
	u8 reserved[936];
}__attribute__((packed, aligned (4)));

struct ipr_supported_device {
	__be16 data_length;
	u8 reserved;
	u8 num_records;
	struct ipr_std_inq_vpids vpids;
	u8 reserved2[16];
}__attribute__((packed, aligned (4)));

struct ipr_hrr_queue {
	struct ipr_ioa_cfg *ioa_cfg;
	__be32 *host_rrq;
	dma_addr_t host_rrq_dma;
#define IPR_HRRQ_REQ_RESP_HANDLE_MASK	0xfffffffc
#define IPR_HRRQ_RESP_BIT_SET		0x00000002
#define IPR_HRRQ_TOGGLE_BIT		0x00000001
#define IPR_HRRQ_REQ_RESP_HANDLE_SHIFT	2
#define IPR_ID_HRRQ_SELE_ENABLE		0x02
	volatile __be32 *hrrq_start;
	volatile __be32 *hrrq_end;
	volatile __be32 *hrrq_curr;

	struct list_head hrrq_free_q;
	struct list_head hrrq_pending_q;
	spinlock_t _lock;
	spinlock_t *lock;

	volatile u32 toggle_bit;
	u32 size;
	u32 min_cmd_id;
	u32 max_cmd_id;
	u8 allow_interrupts:1;
	u8 ioa_is_dead:1;
	u8 allow_cmds:1;
	u8 removing_ioa:1;

	struct blk_iopoll iopoll;
};

/* Command packet structure */
struct ipr_cmd_pkt {
	u8 reserved;		/* Reserved by IOA */
	u8 hrrq_id;
	u8 request_type;
#define IPR_RQTYPE_SCSICDB		0x00
#define IPR_RQTYPE_IOACMD		0x01
#define IPR_RQTYPE_HCAM			0x02
#define IPR_RQTYPE_ATA_PASSTHRU	0x04

	u8 reserved2;

	u8 flags_hi;
#define IPR_FLAGS_HI_WRITE_NOT_READ		0x80
#define IPR_FLAGS_HI_NO_ULEN_CHK		0x20
#define IPR_FLAGS_HI_SYNC_OVERRIDE		0x10
#define IPR_FLAGS_HI_SYNC_COMPLETE		0x08
#define IPR_FLAGS_HI_NO_LINK_DESC		0x04

	u8 flags_lo;
#define IPR_FLAGS_LO_ALIGNED_BFR		0x20
#define IPR_FLAGS_LO_DELAY_AFTER_RST		0x10
#define IPR_FLAGS_LO_UNTAGGED_TASK		0x00
#define IPR_FLAGS_LO_SIMPLE_TASK		0x02
#define IPR_FLAGS_LO_ORDERED_TASK		0x04
#define IPR_FLAGS_LO_HEAD_OF_Q_TASK		0x06
#define IPR_FLAGS_LO_ACA_TASK			0x08

	u8 cdb[16];
	__be16 timeout;
}__attribute__ ((packed, aligned(4)));

struct ipr_ioarcb_ata_regs {	/* 22 bytes */
	u8 flags;
#define IPR_ATA_FLAG_PACKET_CMD			0x80
#define IPR_ATA_FLAG_XFER_TYPE_DMA			0x40
#define IPR_ATA_FLAG_STATUS_ON_GOOD_COMPLETION	0x20
	u8 reserved[3];

	__be16 data;
	u8 feature;
	u8 nsect;
	u8 lbal;
	u8 lbam;
	u8 lbah;
	u8 device;
	u8 command;
	u8 reserved2[3];
	u8 hob_feature;
	u8 hob_nsect;
	u8 hob_lbal;
	u8 hob_lbam;
	u8 hob_lbah;
	u8 ctl;
}__attribute__ ((packed, aligned(2)));

struct ipr_ioadl_desc {
	__be32 flags_and_data_len;
#define IPR_IOADL_FLAGS_MASK		0xff000000
#define IPR_IOADL_GET_FLAGS(x) (be32_to_cpu(x) & IPR_IOADL_FLAGS_MASK)
#define IPR_IOADL_DATA_LEN_MASK		0x00ffffff
#define IPR_IOADL_GET_DATA_LEN(x) (be32_to_cpu(x) & IPR_IOADL_DATA_LEN_MASK)
#define IPR_IOADL_FLAGS_READ		0x48000000
#define IPR_IOADL_FLAGS_READ_LAST	0x49000000
#define IPR_IOADL_FLAGS_WRITE		0x68000000
#define IPR_IOADL_FLAGS_WRITE_LAST	0x69000000
#define IPR_IOADL_FLAGS_LAST		0x01000000

	__be32 address;
}__attribute__((packed, aligned (8)));

struct ipr_ioadl64_desc {
	__be32 flags;
	__be32 data_len;
	__be64 address;
}__attribute__((packed, aligned (16)));

struct ipr_ata64_ioadl {
	struct ipr_ioarcb_ata_regs regs;
	u16 reserved[5];
	struct ipr_ioadl64_desc ioadl64[IPR_NUM_IOADL_ENTRIES];
}__attribute__((packed, aligned (16)));

struct ipr_ioarcb_add_data {
	union {
		struct ipr_ioarcb_ata_regs regs;
		struct ipr_ioadl_desc ioadl[5];
		__be32 add_cmd_parms[10];
	} u;
}__attribute__ ((packed, aligned (4)));

struct ipr_ioarcb_sis64_add_addr_ecb {
	__be64 ioasa_host_pci_addr;
	__be64 data_ioadl_addr;
	__be64 reserved;
	__be32 ext_control_buf[4];
}__attribute__((packed, aligned (8)));

/* IOA Request Control Block    128 bytes  */
struct ipr_ioarcb {
	union {
		__be32 ioarcb_host_pci_addr;
		__be64 ioarcb_host_pci_addr64;
	} a;
	__be32 res_handle;
	__be32 host_response_handle;
	__be32 reserved1;
	__be32 reserved2;
	__be32 reserved3;

	__be32 data_transfer_length;
	__be32 read_data_transfer_length;
	__be32 write_ioadl_addr;
	__be32 ioadl_len;
	__be32 read_ioadl_addr;
	__be32 read_ioadl_len;

	__be32 ioasa_host_pci_addr;
	__be16 ioasa_len;
	__be16 reserved4;

	struct ipr_cmd_pkt cmd_pkt;

	__be16 add_cmd_parms_offset;
	__be16 add_cmd_parms_len;

	union {
		struct ipr_ioarcb_add_data add_data;
		struct ipr_ioarcb_sis64_add_addr_ecb sis64_addr_data;
	} u;

}__attribute__((packed, aligned (4)));

struct ipr_ioasa_vset {
	__be32 failing_lba_hi;
	__be32 failing_lba_lo;
	__be32 reserved;
}__attribute__((packed, aligned (4)));

struct ipr_ioasa_af_dasd {
	__be32 failing_lba;
	__be32 reserved[2];
}__attribute__((packed, aligned (4)));

struct ipr_ioasa_gpdd {
	u8 end_state;
	u8 bus_phase;
	__be16 reserved;
	__be32 ioa_data[2];
}__attribute__((packed, aligned (4)));

struct ipr_ioasa_gata {
	u8 error;
	u8 nsect;		/* Interrupt reason */
	u8 lbal;
	u8 lbam;
	u8 lbah;
	u8 device;
	u8 status;
	u8 alt_status;	/* ATA CTL */
	u8 hob_nsect;
	u8 hob_lbal;
	u8 hob_lbam;
	u8 hob_lbah;
}__attribute__((packed, aligned (4)));

struct ipr_auto_sense {
	__be16 auto_sense_len;
	__be16 ioa_data_len;
	__be32 data[SCSI_SENSE_BUFFERSIZE/sizeof(__be32)];
};

struct ipr_ioasa_hdr {
	__be32 ioasc;
#define IPR_IOASC_SENSE_KEY(ioasc) ((ioasc) >> 24)
#define IPR_IOASC_SENSE_CODE(ioasc) (((ioasc) & 0x00ff0000) >> 16)
#define IPR_IOASC_SENSE_QUAL(ioasc) (((ioasc) & 0x0000ff00) >> 8)
#define IPR_IOASC_SENSE_STATUS(ioasc) ((ioasc) & 0x000000ff)

	__be16 ret_stat_len;	/* Length of the returned IOASA */

	__be16 avail_stat_len;	/* Total Length of status available. */

	__be32 residual_data_len;	/* number of bytes in the host data */
	/* buffers that were not used by the IOARCB command. */

	__be32 ilid;
#define IPR_NO_ILID			0
#define IPR_DRIVER_ILID		0xffffffff

	__be32 fd_ioasc;

	__be32 fd_phys_locator;

	__be32 fd_res_handle;

	__be32 ioasc_specific;	/* status code specific field */
#define IPR_ADDITIONAL_STATUS_FMT		0x80000000
#define IPR_AUTOSENSE_VALID			0x40000000
#define IPR_ATA_DEVICE_WAS_RESET		0x20000000
#define IPR_IOASC_SPECIFIC_MASK		0x00ffffff
#define IPR_FIELD_POINTER_VALID		(0x80000000 >> 8)
#define IPR_FIELD_POINTER_MASK		0x0000ffff

}__attribute__((packed, aligned (4)));

struct ipr_ioasa {
	struct ipr_ioasa_hdr hdr;

	union {
		struct ipr_ioasa_vset vset;
		struct ipr_ioasa_af_dasd dasd;
		struct ipr_ioasa_gpdd gpdd;
		struct ipr_ioasa_gata gata;
	} u;

	struct ipr_auto_sense auto_sense;
}__attribute__((packed, aligned (4)));

struct ipr_ioasa64 {
	struct ipr_ioasa_hdr hdr;
	u8 fd_res_path[8];

	union {
		struct ipr_ioasa_vset vset;
		struct ipr_ioasa_af_dasd dasd;
		struct ipr_ioasa_gpdd gpdd;
		struct ipr_ioasa_gata gata;
	} u;

	struct ipr_auto_sense auto_sense;
}__attribute__((packed, aligned (4)));

struct ipr_mode_parm_hdr {
	u8 length;
	u8 medium_type;
	u8 device_spec_parms;
	u8 block_desc_len;
}__attribute__((packed));

struct ipr_mode_pages {
	struct ipr_mode_parm_hdr hdr;
	u8 data[255 - sizeof(struct ipr_mode_parm_hdr)];
}__attribute__((packed));

struct ipr_mode_page_hdr {
	u8 ps_page_code;
#define IPR_MODE_PAGE_PS	0x80
#define IPR_GET_MODE_PAGE_CODE(hdr) ((hdr)->ps_page_code & 0x3F)
	u8 page_length;
}__attribute__ ((packed));

struct ipr_dev_bus_entry {
	struct ipr_res_addr res_addr;
	u8 flags;
#define IPR_SCSI_ATTR_ENABLE_QAS			0x80
#define IPR_SCSI_ATTR_DISABLE_QAS			0x40
#define IPR_SCSI_ATTR_QAS_MASK				0xC0
#define IPR_SCSI_ATTR_ENABLE_TM				0x20
#define IPR_SCSI_ATTR_NO_TERM_PWR			0x10
#define IPR_SCSI_ATTR_TM_SUPPORTED			0x08
#define IPR_SCSI_ATTR_LVD_TO_SE_NOT_ALLOWED	0x04

	u8 scsi_id;
	u8 bus_width;
	u8 extended_reset_delay;
#define IPR_EXTENDED_RESET_DELAY	7

	__be32 max_xfer_rate;

	u8 spinup_delay;
	u8 reserved3;
	__be16 reserved4;
}__attribute__((packed, aligned (4)));

struct ipr_mode_page28 {
	struct ipr_mode_page_hdr hdr;
	u8 num_entries;
	u8 entry_length;
	struct ipr_dev_bus_entry bus[0];
}__attribute__((packed));

struct ipr_mode_page24 {
	struct ipr_mode_page_hdr hdr;
	u8 flags;
#define IPR_ENABLE_DUAL_IOA_AF 0x80
}__attribute__((packed));

struct ipr_ioa_vpd {
	struct ipr_std_inq_data std_inq_data;
	u8 ascii_part_num[12];
	u8 reserved[40];
	u8 ascii_plant_code[4];
}__attribute__((packed));

struct ipr_inquiry_page3 {
	u8 peri_qual_dev_type;
	u8 page_code;
	u8 reserved1;
	u8 page_length;
	u8 ascii_len;
	u8 reserved2[3];
	u8 load_id[4];
	u8 major_release;
	u8 card_type;
	u8 minor_release[2];
	u8 ptf_number[4];
	u8 patch_number[4];
}__attribute__((packed));

struct ipr_inquiry_cap {
	u8 peri_qual_dev_type;
	u8 page_code;
	u8 reserved1;
	u8 page_length;
	u8 ascii_len;
	u8 reserved2;
	u8 sis_version[2];
	u8 cap;
#define IPR_CAP_DUAL_IOA_RAID		0x80
	u8 reserved3[15];
}__attribute__((packed));

#define IPR_INQUIRY_PAGE0_ENTRIES 20
struct ipr_inquiry_page0 {
	u8 peri_qual_dev_type;
	u8 page_code;
	u8 reserved1;
	u8 len;
	u8 page[IPR_INQUIRY_PAGE0_ENTRIES];
}__attribute__((packed));

struct ipr_hostrcb_device_data_entry {
	struct ipr_vpd vpd;
	struct ipr_res_addr dev_res_addr;
	struct ipr_vpd new_vpd;
	struct ipr_vpd ioa_last_with_dev_vpd;
	struct ipr_vpd cfc_last_with_dev_vpd;
	__be32 ioa_data[5];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_device_data_entry_enhanced {
	struct ipr_ext_vpd vpd;
	u8 ccin[4];
	struct ipr_res_addr dev_res_addr;
	struct ipr_ext_vpd new_vpd;
	u8 new_ccin[4];
	struct ipr_ext_vpd ioa_last_with_dev_vpd;
	struct ipr_ext_vpd cfc_last_with_dev_vpd;
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb64_device_data_entry_enhanced {
	struct ipr_ext_vpd vpd;
	u8 ccin[4];
	u8 res_path[8];
	struct ipr_ext_vpd new_vpd;
	u8 new_ccin[4];
	struct ipr_ext_vpd ioa_last_with_dev_vpd;
	struct ipr_ext_vpd cfc_last_with_dev_vpd;
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_array_data_entry {
	struct ipr_vpd vpd;
	struct ipr_res_addr expected_dev_res_addr;
	struct ipr_res_addr dev_res_addr;
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb64_array_data_entry {
	struct ipr_ext_vpd vpd;
	u8 ccin[4];
	u8 expected_res_path[8];
	u8 res_path[8];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_array_data_entry_enhanced {
	struct ipr_ext_vpd vpd;
	u8 ccin[4];
	struct ipr_res_addr expected_dev_res_addr;
	struct ipr_res_addr dev_res_addr;
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_type_ff_error {
	__be32 ioa_data[758];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_type_01_error {
	__be32 seek_counter;
	__be32 read_counter;
	u8 sense_data[32];
	__be32 ioa_data[236];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_type_21_error {
	__be32 wwn[4];
	u8 res_path[8];
	u8 primary_problem_desc[32];
	u8 second_problem_desc[32];
	__be32 sense_data[8];
	__be32 cdb[4];
	__be32 residual_trans_length;
	__be32 length_of_error;
	__be32 ioa_data[236];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_type_02_error {
	struct ipr_vpd ioa_vpd;
	struct ipr_vpd cfc_vpd;
	struct ipr_vpd ioa_last_attached_to_cfc_vpd;
	struct ipr_vpd cfc_last_attached_to_ioa_vpd;
	__be32 ioa_data[3];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_type_12_error {
	struct ipr_ext_vpd ioa_vpd;
	struct ipr_ext_vpd cfc_vpd;
	struct ipr_ext_vpd ioa_last_attached_to_cfc_vpd;
	struct ipr_ext_vpd cfc_last_attached_to_ioa_vpd;
	__be32 ioa_data[3];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_type_03_error {
	struct ipr_vpd ioa_vpd;
	struct ipr_vpd cfc_vpd;
	__be32 errors_detected;
	__be32 errors_logged;
	u8 ioa_data[12];
	struct ipr_hostrcb_device_data_entry dev[3];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_type_13_error {
	struct ipr_ext_vpd ioa_vpd;
	struct ipr_ext_vpd cfc_vpd;
	__be32 errors_detected;
	__be32 errors_logged;
	struct ipr_hostrcb_device_data_entry_enhanced dev[3];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_type_23_error {
	struct ipr_ext_vpd ioa_vpd;
	struct ipr_ext_vpd cfc_vpd;
	__be32 errors_detected;
	__be32 errors_logged;
	struct ipr_hostrcb64_device_data_entry_enhanced dev[3];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_type_04_error {
	struct ipr_vpd ioa_vpd;
	struct ipr_vpd cfc_vpd;
	u8 ioa_data[12];
	struct ipr_hostrcb_array_data_entry array_member[10];
	__be32 exposed_mode_adn;
	__be32 array_id;
	struct ipr_vpd incomp_dev_vpd;
	__be32 ioa_data2;
	struct ipr_hostrcb_array_data_entry array_member2[8];
	struct ipr_res_addr last_func_vset_res_addr;
	u8 vset_serial_num[IPR_SERIAL_NUM_LEN];
	u8 protection_level[8];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_type_14_error {
	struct ipr_ext_vpd ioa_vpd;
	struct ipr_ext_vpd cfc_vpd;
	__be32 exposed_mode_adn;
	__be32 array_id;
	struct ipr_res_addr last_func_vset_res_addr;
	u8 vset_serial_num[IPR_SERIAL_NUM_LEN];
	u8 protection_level[8];
	__be32 num_entries;
	struct ipr_hostrcb_array_data_entry_enhanced array_member[18];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_type_24_error {
	struct ipr_ext_vpd ioa_vpd;
	struct ipr_ext_vpd cfc_vpd;
	u8 reserved[2];
	u8 exposed_mode_adn;
#define IPR_INVALID_ARRAY_DEV_NUM		0xff
	u8 array_id;
	u8 last_res_path[8];
	u8 protection_level[8];
	struct ipr_ext_vpd64 array_vpd;
	u8 description[16];
	u8 reserved2[3];
	u8 num_entries;
	struct ipr_hostrcb64_array_data_entry array_member[32];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_type_07_error {
	u8 failure_reason[64];
	struct ipr_vpd vpd;
	u32 data[222];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_type_17_error {
	u8 failure_reason[64];
	struct ipr_ext_vpd vpd;
	u32 data[476];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_config_element {
	u8 type_status;
#define IPR_PATH_CFG_TYPE_MASK	0xF0
#define IPR_PATH_CFG_NOT_EXIST	0x00
#define IPR_PATH_CFG_IOA_PORT		0x10
#define IPR_PATH_CFG_EXP_PORT		0x20
#define IPR_PATH_CFG_DEVICE_PORT	0x30
#define IPR_PATH_CFG_DEVICE_LUN	0x40

#define IPR_PATH_CFG_STATUS_MASK	0x0F
#define IPR_PATH_CFG_NO_PROB		0x00
#define IPR_PATH_CFG_DEGRADED		0x01
#define IPR_PATH_CFG_FAILED		0x02
#define IPR_PATH_CFG_SUSPECT		0x03
#define IPR_PATH_NOT_DETECTED		0x04
#define IPR_PATH_INCORRECT_CONN	0x05

	u8 cascaded_expander;
	u8 phy;
	u8 link_rate;
#define IPR_PHY_LINK_RATE_MASK	0x0F

	__be32 wwid[2];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb64_config_element {
	__be16 length;
	u8 descriptor_id;
#define IPR_DESCRIPTOR_MASK		0xC0
#define IPR_DESCRIPTOR_SIS64		0x00

	u8 reserved;
	u8 type_status;

	u8 reserved2[2];
	u8 link_rate;

	u8 res_path[8];
	__be32 wwid[2];
}__attribute__((packed, aligned (8)));

struct ipr_hostrcb_fabric_desc {
	__be16 length;
	u8 ioa_port;
	u8 cascaded_expander;
	u8 phy;
	u8 path_state;
#define IPR_PATH_ACTIVE_MASK		0xC0
#define IPR_PATH_NO_INFO		0x00
#define IPR_PATH_ACTIVE			0x40
#define IPR_PATH_NOT_ACTIVE		0x80

#define IPR_PATH_STATE_MASK		0x0F
#define IPR_PATH_STATE_NO_INFO	0x00
#define IPR_PATH_HEALTHY		0x01
#define IPR_PATH_DEGRADED		0x02
#define IPR_PATH_FAILED			0x03

	__be16 num_entries;
	struct ipr_hostrcb_config_element elem[1];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb64_fabric_desc {
	__be16 length;
	u8 descriptor_id;

	u8 reserved[2];
	u8 path_state;

	u8 reserved2[2];
	u8 res_path[8];
	u8 reserved3[6];
	__be16 num_entries;
	struct ipr_hostrcb64_config_element elem[1];
}__attribute__((packed, aligned (8)));

#define for_each_hrrq(hrrq, ioa_cfg) \
		for (hrrq = (ioa_cfg)->hrrq; \
			hrrq < ((ioa_cfg)->hrrq + (ioa_cfg)->hrrq_num); hrrq++)

#define for_each_fabric_cfg(fabric, cfg) \
		for (cfg = (fabric)->elem; \
			cfg < ((fabric)->elem + be16_to_cpu((fabric)->num_entries)); \
			cfg++)

struct ipr_hostrcb_type_20_error {
	u8 failure_reason[64];
	u8 reserved[3];
	u8 num_entries;
	struct ipr_hostrcb_fabric_desc desc[1];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_type_30_error {
	u8 failure_reason[64];
	u8 reserved[3];
	u8 num_entries;
	struct ipr_hostrcb64_fabric_desc desc[1];
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb_error {
	__be32 fd_ioasc;
	struct ipr_res_addr fd_res_addr;
	__be32 fd_res_handle;
	__be32 prc;
	union {
		struct ipr_hostrcb_type_ff_error type_ff_error;
		struct ipr_hostrcb_type_01_error type_01_error;
		struct ipr_hostrcb_type_02_error type_02_error;
		struct ipr_hostrcb_type_03_error type_03_error;
		struct ipr_hostrcb_type_04_error type_04_error;
		struct ipr_hostrcb_type_07_error type_07_error;
		struct ipr_hostrcb_type_12_error type_12_error;
		struct ipr_hostrcb_type_13_error type_13_error;
		struct ipr_hostrcb_type_14_error type_14_error;
		struct ipr_hostrcb_type_17_error type_17_error;
		struct ipr_hostrcb_type_20_error type_20_error;
	} u;
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb64_error {
	__be32 fd_ioasc;
	__be32 ioa_fw_level;
	__be32 fd_res_handle;
	__be32 prc;
	__be64 fd_dev_id;
	__be64 fd_lun;
	u8 fd_res_path[8];
	__be64 time_stamp;
	u8 reserved[16];
	union {
		struct ipr_hostrcb_type_ff_error type_ff_error;
		struct ipr_hostrcb_type_12_error type_12_error;
		struct ipr_hostrcb_type_17_error type_17_error;
		struct ipr_hostrcb_type_21_error type_21_error;
		struct ipr_hostrcb_type_23_error type_23_error;
		struct ipr_hostrcb_type_24_error type_24_error;
		struct ipr_hostrcb_type_30_error type_30_error;
	} u;
}__attribute__((packed, aligned (8)));

struct ipr_hostrcb_raw {
	__be32 data[sizeof(struct ipr_hostrcb_error)/sizeof(__be32)];
}__attribute__((packed, aligned (4)));

struct ipr_hcam {
	u8 op_code;
#define IPR_HOST_RCB_OP_CODE_CONFIG_CHANGE			0xE1
#define IPR_HOST_RCB_OP_CODE_LOG_DATA				0xE2

	u8 notify_type;
#define IPR_HOST_RCB_NOTIF_TYPE_EXISTING_CHANGED	0x00
#define IPR_HOST_RCB_NOTIF_TYPE_NEW_ENTRY			0x01
#define IPR_HOST_RCB_NOTIF_TYPE_REM_ENTRY			0x02
#define IPR_HOST_RCB_NOTIF_TYPE_ERROR_LOG_ENTRY		0x10
#define IPR_HOST_RCB_NOTIF_TYPE_INFORMATION_ENTRY	0x11

	u8 notifications_lost;
#define IPR_HOST_RCB_NO_NOTIFICATIONS_LOST			0
#define IPR_HOST_RCB_NOTIFICATIONS_LOST				0x80

	u8 flags;
#define IPR_HOSTRCB_INTERNAL_OPER	0x80
#define IPR_HOSTRCB_ERR_RESP_SENT	0x40

	u8 overlay_id;
#define IPR_HOST_RCB_OVERLAY_ID_1				0x01
#define IPR_HOST_RCB_OVERLAY_ID_2				0x02
#define IPR_HOST_RCB_OVERLAY_ID_3				0x03
#define IPR_HOST_RCB_OVERLAY_ID_4				0x04
#define IPR_HOST_RCB_OVERLAY_ID_6				0x06
#define IPR_HOST_RCB_OVERLAY_ID_7				0x07
#define IPR_HOST_RCB_OVERLAY_ID_12				0x12
#define IPR_HOST_RCB_OVERLAY_ID_13				0x13
#define IPR_HOST_RCB_OVERLAY_ID_14				0x14
#define IPR_HOST_RCB_OVERLAY_ID_16				0x16
#define IPR_HOST_RCB_OVERLAY_ID_17				0x17
#define IPR_HOST_RCB_OVERLAY_ID_20				0x20
#define IPR_HOST_RCB_OVERLAY_ID_21				0x21
#define IPR_HOST_RCB_OVERLAY_ID_23				0x23
#define IPR_HOST_RCB_OVERLAY_ID_24				0x24
#define IPR_HOST_RCB_OVERLAY_ID_26				0x26
#define IPR_HOST_RCB_OVERLAY_ID_30				0x30
#define IPR_HOST_RCB_OVERLAY_ID_DEFAULT				0xFF

	u8 reserved1[3];
	__be32 ilid;
	__be32 time_since_last_ioa_reset;
	__be32 reserved2;
	__be32 length;

	union {
		struct ipr_hostrcb_error error;
		struct ipr_hostrcb64_error error64;
		struct ipr_hostrcb_cfg_ch_not ccn;
		struct ipr_hostrcb_raw raw;
	} u;
}__attribute__((packed, aligned (4)));

struct ipr_hostrcb {
	struct ipr_hcam hcam;
	dma_addr_t hostrcb_dma;
	struct list_head queue;
	struct ipr_ioa_cfg *ioa_cfg;
	char rp_buffer[IPR_MAX_RES_PATH_LENGTH];
};

/* IPR smart dump table structures */
struct ipr_sdt_entry {
	__be32 start_token;
	__be32 end_token;
	u8 reserved[4];

	u8 flags;
#define IPR_SDT_ENDIAN		0x80
#define IPR_SDT_VALID_ENTRY	0x20

	u8 resv;
	__be16 priority;
}__attribute__((packed, aligned (4)));

struct ipr_sdt_header {
	__be32 state;
	__be32 num_entries;
	__be32 num_entries_used;
	__be32 dump_size;
}__attribute__((packed, aligned (4)));

struct ipr_sdt {
	struct ipr_sdt_header hdr;
	struct ipr_sdt_entry entry[IPR_FMT3_NUM_SDT_ENTRIES];
}__attribute__((packed, aligned (4)));

struct ipr_uc_sdt {
	struct ipr_sdt_header hdr;
	struct ipr_sdt_entry entry[1];
}__attribute__((packed, aligned (4)));

/*
 * Driver types
 */
struct ipr_bus_attributes {
	u8 bus;
	u8 qas_enabled;
	u8 bus_width;
	u8 reserved;
	u32 max_xfer_rate;
};

struct ipr_sata_port {
	struct ipr_ioa_cfg *ioa_cfg;
	struct ata_port *ap;
	struct ipr_resource_entry *res;
	struct ipr_ioasa_gata ioasa;
};

struct ipr_resource_entry {
	u8 needs_sync_complete:1;
	u8 in_erp:1;
	u8 add_to_ml:1;
	u8 del_from_ml:1;
	u8 resetting_device:1;
	u8 reset_occurred:1;

	u32 bus;		/* AKA channel */
	u32 target;		/* AKA id */
	u32 lun;
#define IPR_ARRAY_VIRTUAL_BUS			0x1
#define IPR_VSET_VIRTUAL_BUS			0x2
#define IPR_IOAFP_VIRTUAL_BUS			0x3

#define IPR_GET_RES_PHYS_LOC(res) \
	(((res)->bus << 24) | ((res)->target << 8) | (res)->lun)

	u8 ata_class;

	u8 flags;
	__be16 res_flags;

	u8 type;

	u8 qmodel;
	struct ipr_std_inq_data std_inq_data;

	__be32 res_handle;
	__be64 dev_id;
	__be64 lun_wwn;
	struct scsi_lun dev_lun;
	u8 res_path[8];

	struct ipr_ioa_cfg *ioa_cfg;
	struct scsi_device *sdev;
	struct ipr_sata_port *sata_port;
	struct list_head queue;
}; /* struct ipr_resource_entry */

struct ipr_resource_hdr {
	u16 num_entries;
	u16 reserved;
};

struct ipr_misc_cbs {
	struct ipr_ioa_vpd ioa_vpd;
	struct ipr_inquiry_page0 page0_data;
	struct ipr_inquiry_page3 page3_data;
	struct ipr_inquiry_cap cap;
	struct ipr_mode_pages mode_pages;
	struct ipr_supported_device supp_dev;
};

struct ipr_interrupt_offsets {
	unsigned long set_interrupt_mask_reg;
	unsigned long clr_interrupt_mask_reg;
	unsigned long clr_interrupt_mask_reg32;
	unsigned long sense_interrupt_mask_reg;
	unsigned long sense_interrupt_mask_reg32;
	unsigned long clr_interrupt_reg;
	unsigned long clr_interrupt_reg32;

	unsigned long sense_interrupt_reg;
	unsigned long sense_interrupt_reg32;
	unsigned long ioarrin_reg;
	unsigned long sense_uproc_interrupt_reg;
	unsigned long sense_uproc_interrupt_reg32;
	unsigned long set_uproc_interrupt_reg;
	unsigned long set_uproc_interrupt_reg32;
	unsigned long clr_uproc_interrupt_reg;
	unsigned long clr_uproc_interrupt_reg32;

	unsigned long init_feedback_reg;

	unsigned long dump_addr_reg;
	unsigned long dump_data_reg;

#define IPR_ENDIAN_SWAP_KEY		0x00080800
	unsigned long endian_swap_reg;
};

struct ipr_interrupts {
	void __iomem *set_interrupt_mask_reg;
	void __iomem *clr_interrupt_mask_reg;
	void __iomem *clr_interrupt_mask_reg32;
	void __iomem *sense_interrupt_mask_reg;
	void __iomem *sense_interrupt_mask_reg32;
	void __iomem *clr_interrupt_reg;
	void __iomem *clr_interrupt_reg32;

	void __iomem *sense_interrupt_reg;
	void __iomem *sense_interrupt_reg32;
	void __iomem *ioarrin_reg;
	void __iomem *sense_uproc_interrupt_reg;
	void __iomem *sense_uproc_interrupt_reg32;
	void __iomem *set_uproc_interrupt_reg;
	void __iomem *set_uproc_interrupt_reg32;
	void __iomem *clr_uproc_interrupt_reg;
	void __iomem *clr_uproc_interrupt_reg32;

	void __iomem *init_feedback_reg;

	void __iomem *dump_addr_reg;
	void __iomem *dump_data_reg;

	void __iomem *endian_swap_reg;
};

struct ipr_chip_cfg_t {
	u32 mailbox;
	u16 max_cmds;
	u8 cache_line_size;
	u8 clear_isr;
	u32 iopoll_weight;
	struct ipr_interrupt_offsets regs;
};

struct ipr_chip_t {
	u16 vendor;
	u16 device;
	u16 intr_type;
#define IPR_USE_LSI			0x00
#define IPR_USE_MSI			0x01
#define IPR_USE_MSIX			0x02
	u16 sis_type;
#define IPR_SIS32			0x00
#define IPR_SIS64			0x01
	u16 bist_method;
#define IPR_PCI_CFG			0x00
#define IPR_MMIO			0x01
	const struct ipr_chip_cfg_t *cfg;
};

enum ipr_shutdown_type {
	IPR_SHUTDOWN_NORMAL = 0x00,
	IPR_SHUTDOWN_PREPARE_FOR_NORMAL = 0x40,
	IPR_SHUTDOWN_ABBREV = 0x80,
	IPR_SHUTDOWN_NONE = 0x100
};

struct ipr_trace_entry {
	u32 time;

	u8 op_code;
	u8 ata_op_code;
	u8 type;
#define IPR_TRACE_START			0x00
#define IPR_TRACE_FINISH		0xff
	u8 cmd_index;

	__be32 res_handle;
	union {
		u32 ioasc;
		u32 add_data;
		u32 res_addr;
	} u;
};

struct ipr_sglist {
	u32 order;
	u32 num_sg;
	u32 num_dma_sg;
	u32 buffer_len;
	struct scatterlist scatterlist[1];
};

enum ipr_sdt_state {
	INACTIVE,
	WAIT_FOR_DUMP,
	GET_DUMP,
	READ_DUMP,
	ABORT_DUMP,
	DUMP_OBTAINED
};

/* Per-controller data */
struct ipr_ioa_cfg {
	char eye_catcher[8];
#define IPR_EYECATCHER			"iprcfg"

	struct list_head queue;

	u8 in_reset_reload:1;
	u8 in_ioa_bringdown:1;
	u8 ioa_unit_checked:1;
	u8 dump_taken:1;
	u8 allow_ml_add_del:1;
	u8 needs_hard_reset:1;
	u8 dual_raid:1;
	u8 needs_warm_reset:1;
	u8 msi_received:1;
	u8 sis64:1;
	u8 dump_timeout:1;
	u8 cfg_locked:1;
	u8 clear_isr:1;
	u8 probe_done:1;

	u8 revid;

	/*
	 * Bitmaps for SIS64 generated target values
	 */
	unsigned long target_ids[BITS_TO_LONGS(IPR_MAX_SIS64_DEVS)];
	unsigned long array_ids[BITS_TO_LONGS(IPR_MAX_SIS64_DEVS)];
	unsigned long vset_ids[BITS_TO_LONGS(IPR_MAX_SIS64_DEVS)];

	u16 type; /* CCIN of the card */

	u8 log_level;
#define IPR_MAX_LOG_LEVEL			4
#define IPR_DEFAULT_LOG_LEVEL		2

#define IPR_NUM_TRACE_INDEX_BITS	8
#define IPR_NUM_TRACE_ENTRIES		(1 << IPR_NUM_TRACE_INDEX_BITS)
#define IPR_TRACE_SIZE	(sizeof(struct ipr_trace_entry) * IPR_NUM_TRACE_ENTRIES)
	char trace_start[8];
#define IPR_TRACE_START_LABEL			"trace"
	struct ipr_trace_entry *trace;
	atomic_t trace_index;

	char cfg_table_start[8];
#define IPR_CFG_TBL_START		"cfg"
	union {
		struct ipr_config_table *cfg_table;
		struct ipr_config_table64 *cfg_table64;
	} u;
	dma_addr_t cfg_table_dma;
	u32 cfg_table_size;
	u32 max_devs_supported;

	char resource_table_label[8];
#define IPR_RES_TABLE_LABEL		"res_tbl"
	struct ipr_resource_entry *res_entries;
	struct list_head free_res_q;
	struct list_head used_res_q;

	char ipr_hcam_label[8];
#define IPR_HCAM_LABEL			"hcams"
	struct ipr_hostrcb *hostrcb[IPR_NUM_HCAMS];
	dma_addr_t hostrcb_dma[IPR_NUM_HCAMS];
	struct list_head hostrcb_free_q;
	struct list_head hostrcb_pending_q;

	struct ipr_hrr_queue hrrq[IPR_MAX_HRRQ_NUM];
	u32 hrrq_num;
	atomic_t  hrrq_index;
	u16 identify_hrrq_index;

	struct ipr_bus_attributes bus_attr[IPR_MAX_NUM_BUSES];

	unsigned int transop_timeout;
	const struct ipr_chip_cfg_t *chip_cfg;
	const struct ipr_chip_t *ipr_chip;

	void __iomem *hdw_dma_regs;	/* iomapped PCI memory space */
	unsigned long hdw_dma_regs_pci;	/* raw PCI memory space */
	void __iomem *ioa_mailbox;
	struct ipr_interrupts regs;

	u16 saved_pcix_cmd_reg;
	u16 reset_retries;

	u32 errors_logged;
	u32 doorbell;

	struct Scsi_Host *host;
	struct pci_dev *pdev;
	struct ipr_sglist *ucode_sglist;
	u8 saved_mode_page_len;

	struct work_struct work_q;

	wait_queue_head_t reset_wait_q;
	wait_queue_head_t msi_wait_q;
	wait_queue_head_t eeh_wait_q;

	struct ipr_dump *dump;
	enum ipr_sdt_state sdt_state;

	struct ipr_misc_cbs *vpd_cbs;
	dma_addr_t vpd_cbs_dma;

	struct pci_pool *ipr_cmd_pool;

	struct ipr_cmnd *reset_cmd;
	int (*reset) (struct ipr_cmnd *);

	struct ata_host ata_host;
	char ipr_cmd_label[8];
#define IPR_CMD_LABEL		"ipr_cmd"
	u32 max_cmds;
	struct ipr_cmnd **ipr_cmnd_list;
	dma_addr_t *ipr_cmnd_list_dma;

	u16 intr_flag;
	unsigned int nvectors;

	struct {
		unsigned short vec;
		char desc[22];
	} vectors_info[IPR_MAX_MSIX_VECTORS];

	u32 iopoll_weight;

}; /* struct ipr_ioa_cfg */

struct ipr_cmnd {
	struct ipr_ioarcb ioarcb;
	union {
		struct ipr_ioadl_desc ioadl[IPR_NUM_IOADL_ENTRIES];
		struct ipr_ioadl64_desc ioadl64[IPR_NUM_IOADL_ENTRIES];
		struct ipr_ata64_ioadl ata_ioadl;
	} i;
	union {
		struct ipr_ioasa ioasa;
		struct ipr_ioasa64 ioasa64;
	} s;
	struct list_head queue;
	struct scsi_cmnd *scsi_cmd;
	struct ata_queued_cmd *qc;
	struct completion completion;
	struct timer_list timer;
	void (*fast_done) (struct ipr_cmnd *);
	void (*done) (struct ipr_cmnd *);
	int (*job_step) (struct ipr_cmnd *);
	int (*job_step_failed) (struct ipr_cmnd *);
	u16 cmd_index;
	u8 sense_buffer[SCSI_SENSE_BUFFERSIZE];
	dma_addr_t sense_buffer_dma;
	unsigned short dma_use_sg;
	dma_addr_t dma_addr;
	struct ipr_cmnd *sibling;
	union {
		enum ipr_shutdown_type shutdown_type;
		struct ipr_hostrcb *hostrcb;
		unsigned long time_left;
		unsigned long scratch;
		struct ipr_resource_entry *res;
		struct scsi_device *sdev;
	} u;

	struct ipr_hrr_queue *hrrq;
	struct ipr_ioa_cfg *ioa_cfg;
};

struct ipr_ses_table_entry {
	char product_id[17];
	char compare_product_id_byte[17];
	u32 max_bus_speed_limit;	/* MB/sec limit for this backplane */
};

struct ipr_dump_header {
	u32 eye_catcher;
#define IPR_DUMP_EYE_CATCHER		0xC5D4E3F2
	u32 len;
	u32 num_entries;
	u32 first_entry_offset;
	u32 status;
#define IPR_DUMP_STATUS_SUCCESS			0
#define IPR_DUMP_STATUS_QUAL_SUCCESS		2
#define IPR_DUMP_STATUS_FAILED			0xffffffff
	u32 os;
#define IPR_DUMP_OS_LINUX	0x4C4E5558
	u32 driver_name;
#define IPR_DUMP_DRIVER_NAME	0x49505232
}__attribute__((packed, aligned (4)));

struct ipr_dump_entry_header {
	u32 eye_catcher;
#define IPR_DUMP_EYE_CATCHER		0xC5D4E3F2
	u32 len;
	u32 num_elems;
	u32 offset;
	u32 data_type;
#define IPR_DUMP_DATA_TYPE_ASCII	0x41534349
#define IPR_DUMP_DATA_TYPE_BINARY	0x42494E41
	u32 id;
#define IPR_DUMP_IOA_DUMP_ID		0x494F4131
#define IPR_DUMP_LOCATION_ID		0x4C4F4341
#define IPR_DUMP_TRACE_ID		0x54524143
#define IPR_DUMP_DRIVER_VERSION_ID	0x44525652
#define IPR_DUMP_DRIVER_TYPE_ID	0x54595045
#define IPR_DUMP_IOA_CTRL_BLK		0x494F4342
#define IPR_DUMP_PEND_OPS		0x414F5053
	u32 status;
}__attribute__((packed, aligned (4)));

struct ipr_dump_location_entry {
	struct ipr_dump_entry_header hdr;
	u8 location[20];
}__attribute__((packed));

struct ipr_dump_trace_entry {
	struct ipr_dump_entry_header hdr;
	u32 trace[IPR_TRACE_SIZE / sizeof(u32)];
}__attribute__((packed, aligned (4)));

struct ipr_dump_version_entry {
	struct ipr_dump_entry_header hdr;
	u8 version[sizeof(IPR_DRIVER_VERSION)];
};

struct ipr_dump_ioa_type_entry {
	struct ipr_dump_entry_header hdr;
	u32 type;
	u32 fw_version;
};

struct ipr_driver_dump {
	struct ipr_dump_header hdr;
	struct ipr_dump_version_entry version_entry;
	struct ipr_dump_location_entry location_entry;
	struct ipr_dump_ioa_type_entry ioa_type_entry;
	struct ipr_dump_trace_entry trace_entry;
}__attribute__((packed));

struct ipr_ioa_dump {
	struct ipr_dump_entry_header hdr;
	struct ipr_sdt sdt;
	__be32 **ioa_data;
	u32 reserved;
	u32 next_page_index;
	u32 page_offset;
	u32 format;
}__attribute__((packed, aligned (4)));

struct ipr_dump {
	struct kref kref;
	struct ipr_ioa_cfg *ioa_cfg;
	struct ipr_driver_dump driver_dump;
	struct ipr_ioa_dump ioa_dump;
};

struct ipr_error_table_t {
	u32 ioasc;
	int log_ioasa;
	int log_hcam;
	char *error;
};

struct ipr_software_inq_lid_info {
	__be32 load_id;
	__be32 timestamp[3];
}__attribute__((packed, aligned (4)));

struct ipr_ucode_image_header {
	__be32 header_length;
	__be32 lid_table_offset;
	u8 major_release;
	u8 card_type;
	u8 minor_release[2];
	u8 reserved[20];
	char eyecatcher[16];
	__be32 num_lids;
	struct ipr_software_inq_lid_info lid[1];
}__attribute__((packed, aligned (4)));

/*
 * Macros
 */
#define IPR_DBG_CMD(CMD) if (ipr_debug) { CMD; }

#ifdef CONFIG_SCSI_IPR_TRACE
#define ipr_create_trace_file(kobj, attr) sysfs_create_bin_file(kobj, attr)
#define ipr_remove_trace_file(kobj, attr) sysfs_remove_bin_file(kobj, attr)
#else
#define ipr_create_trace_file(kobj, attr) 0
#define ipr_remove_trace_file(kobj, attr) do { } while(0)
#endif

#ifdef CONFIG_SCSI_IPR_DUMP
#define ipr_create_dump_file(kobj, attr) sysfs_create_bin_file(kobj, attr)
#define ipr_remove_dump_file(kobj, attr) sysfs_remove_bin_file(kobj, attr)
#else
#define ipr_create_dump_file(kobj, attr) 0
#define ipr_remove_dump_file(kobj, attr) do { } while(0)
#endif

/*
 * Error logging macros
 */
#define ipr_err(...) printk(KERN_ERR IPR_NAME ": "__VA_ARGS__)
#define ipr_info(...) printk(KERN_INFO IPR_NAME ": "__VA_ARGS__)
#define ipr_dbg(...) IPR_DBG_CMD(printk(KERN_INFO IPR_NAME ": "__VA_ARGS__))

#define ipr_res_printk(level, ioa_cfg, bus, target, lun, fmt, ...) \
	printk(level IPR_NAME ": %d:%d:%d:%d: " fmt, (ioa_cfg)->host->host_no, \
		bus, target, lun, ##__VA_ARGS__)

#define ipr_res_err(ioa_cfg, res, fmt, ...) \
	ipr_res_printk(KERN_ERR, ioa_cfg, (res)->bus, (res)->target, (res)->lun, fmt, ##__VA_ARGS__)

#define ipr_ra_printk(level, ioa_cfg, ra, fmt, ...) \
	printk(level IPR_NAME ": %d:%d:%d:%d: " fmt, (ioa_cfg)->host->host_no, \
		(ra).bus, (ra).target, (ra).lun, ##__VA_ARGS__)

#define ipr_ra_err(ioa_cfg, ra, fmt, ...) \
	ipr_ra_printk(KERN_ERR, ioa_cfg, ra, fmt, ##__VA_ARGS__)

#define ipr_phys_res_err(ioa_cfg, res, fmt, ...)			\
{									\
	if ((res).bus >= IPR_MAX_NUM_BUSES) {				\
		ipr_err(fmt": unknown\n", ##__VA_ARGS__);		\
	} else {							\
		ipr_err(fmt": %d:%d:%d:%d\n",				\
			##__VA_ARGS__, (ioa_cfg)->host->host_no,	\
			(res).bus, (res).target, (res).lun);		\
	}								\
}

#define ipr_hcam_err(hostrcb, fmt, ...)					\
{									\
	if (ipr_is_device(hostrcb)) {					\
		if ((hostrcb)->ioa_cfg->sis64) {			\
			printk(KERN_ERR IPR_NAME ": %s: " fmt, 		\
				ipr_format_res_path(hostrcb->ioa_cfg,	\
					hostrcb->hcam.u.error64.fd_res_path, \
					hostrcb->rp_buffer,		\
					sizeof(hostrcb->rp_buffer)),	\
				__VA_ARGS__);				\
		} else {						\
			ipr_ra_err((hostrcb)->ioa_cfg,			\
				(hostrcb)->hcam.u.error.fd_res_addr,	\
				fmt, __VA_ARGS__);			\
		}							\
	} else {							\
		dev_err(&(hostrcb)->ioa_cfg->pdev->dev, fmt, __VA_ARGS__); \
	}								\
}

#define ipr_trace ipr_dbg("%s: %s: Line: %d\n",\
	__FILE__, __func__, __LINE__)

#define ENTER IPR_DBG_CMD(printk(KERN_INFO IPR_NAME": Entering %s\n", __func__))
#define LEAVE IPR_DBG_CMD(printk(KERN_INFO IPR_NAME": Leaving %s\n", __func__))

#define ipr_err_separator \
ipr_err("----------------------------------------------------------\n")


/*
 * Inlines
 */

/**
 * ipr_is_ioa_resource - Determine if a resource is the IOA
 * @res:	resource entry struct
 *
 * Return value:
 * 	1 if IOA / 0 if not IOA
 **/
static inline int ipr_is_ioa_resource(struct ipr_resource_entry *res)
{
	return res->type == IPR_RES_TYPE_IOAFP;
}

/**
 * ipr_is_af_dasd_device - Determine if a resource is an AF DASD
 * @res:	resource entry struct
 *
 * Return value:
 * 	1 if AF DASD / 0 if not AF DASD
 **/
static inline int ipr_is_af_dasd_device(struct ipr_resource_entry *res)
{
	return res->type == IPR_RES_TYPE_AF_DASD ||
		res->type == IPR_RES_TYPE_REMOTE_AF_DASD;
}

/**
 * ipr_is_vset_device - Determine if a resource is a VSET
 * @res:	resource entry struct
 *
 * Return value:
 * 	1 if VSET / 0 if not VSET
 **/
static inline int ipr_is_vset_device(struct ipr_resource_entry *res)
{
	return res->type == IPR_RES_TYPE_VOLUME_SET;
}

/**
 * ipr_is_gscsi - Determine if a resource is a generic scsi resource
 * @res:	resource entry struct
 *
 * Return value:
 * 	1 if GSCSI / 0 if not GSCSI
 **/
static inline int ipr_is_gscsi(struct ipr_resource_entry *res)
{
	return res->type == IPR_RES_TYPE_GENERIC_SCSI;
}

/**
 * ipr_is_scsi_disk - Determine if a resource is a SCSI disk
 * @res:	resource entry struct
 *
 * Return value:
 * 	1 if SCSI disk / 0 if not SCSI disk
 **/
static inline int ipr_is_scsi_disk(struct ipr_resource_entry *res)
{
	if (ipr_is_af_dasd_device(res) ||
	    (ipr_is_gscsi(res) && IPR_IS_DASD_DEVICE(res->std_inq_data)))
		return 1;
	else
		return 0;
}

/**
 * ipr_is_gata - Determine if a resource is a generic ATA resource
 * @res:	resource entry struct
 *
 * Return value:
 * 	1 if GATA / 0 if not GATA
 **/
static inline int ipr_is_gata(struct ipr_resource_entry *res)
{
	return res->type == IPR_RES_TYPE_GENERIC_ATA;
}

/**
 * ipr_is_naca_model - Determine if a resource is using NACA queueing model
 * @res:	resource entry struct
 *
 * Return value:
 * 	1 if NACA queueing model / 0 if not NACA queueing model
 **/
static inline int ipr_is_naca_model(struct ipr_resource_entry *res)
{
	if (ipr_is_gscsi(res) && res->qmodel == IPR_QUEUE_NACA_MODEL)
		return 1;
	return 0;
}

/**
 * ipr_is_device - Determine if the hostrcb structure is related to a device
 * @hostrcb:	host resource control blocks struct
 *
 * Return value:
 * 	1 if AF / 0 if not AF
 **/
static inline int ipr_is_device(struct ipr_hostrcb *hostrcb)
{
	struct ipr_res_addr *res_addr;
	u8 *res_path;

	if (hostrcb->ioa_cfg->sis64) {
		res_path = &hostrcb->hcam.u.error64.fd_res_path[0];
		if ((res_path[0] == 0x00 || res_path[0] == 0x80 ||
		    res_path[0] == 0x81) && res_path[2] != 0xFF)
			return 1;
	} else {
		res_addr = &hostrcb->hcam.u.error.fd_res_addr;

		if ((res_addr->bus < IPR_MAX_NUM_BUSES) &&
		    (res_addr->target < (IPR_MAX_NUM_TARGETS_PER_BUS - 1)))
			return 1;
	}
	return 0;
}

/**
 * ipr_sdt_is_fmt2 - Determine if a SDT address is in format 2
 * @sdt_word:	SDT address
 *
 * Return value:
 * 	1 if format 2 / 0 if not
 **/
static inline int ipr_sdt_is_fmt2(u32 sdt_word)
{
	u32 bar_sel = IPR_GET_FMT2_BAR_SEL(sdt_word);

	switch (bar_sel) {
	case IPR_SDT_FMT2_BAR0_SEL:
	case IPR_SDT_FMT2_BAR1_SEL:
	case IPR_SDT_FMT2_BAR2_SEL:
	case IPR_SDT_FMT2_BAR3_SEL:
	case IPR_SDT_FMT2_BAR4_SEL:
	case IPR_SDT_FMT2_BAR5_SEL:
	case IPR_SDT_FMT2_EXP_ROM_SEL:
		return 1;
	};

	return 0;
}

#ifndef writeq
static inline void writeq(u64 val, void __iomem *addr)
{
        writel(((u32) (val >> 32)), addr);
        writel(((u32) (val)), (addr + 4));
}
#endif

#endif /* _IPR_H */
