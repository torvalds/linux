/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Linux Driver for Mylex DAC960/AcceleRAID/eXtremeRAID PCI RAID Controllers
 *
 * Copyright 2017 Hannes Reinecke, SUSE Linux GmbH <hare@suse.com>
 *
 * Based on the original DAC960 driver,
 * Copyright 1998-2001 by Leonard N. Zubkoff <lnz@dandelion.com>
 * Portions Copyright 2002 by Mylex (An IBM Business Unit)
 *
 */

#ifndef MYRB_H
#define MYRB_H

#define MYRB_MAX_LDEVS			32
#define MYRB_MAX_CHANNELS		3
#define MYRB_MAX_TARGETS		16
#define MYRB_MAX_PHYSICAL_DEVICES	45
#define MYRB_SCATTER_GATHER_LIMIT	32
#define MYRB_CMD_MBOX_COUNT		256
#define MYRB_STAT_MBOX_COUNT		1024

#define MYRB_BLKSIZE_BITS		9
#define MYRB_MAILBOX_TIMEOUT		1000000

#define MYRB_DCMD_TAG			1
#define MYRB_MCMD_TAG			2

#define MYRB_PRIMARY_MONITOR_INTERVAL (10 * HZ)
#define MYRB_SECONDARY_MONITOR_INTERVAL (60 * HZ)

/*
 * DAC960 V1 Firmware Command Opcodes.
 */
enum myrb_cmd_opcode {
	/* I/O Commands */
	MYRB_CMD_READ_EXTENDED =	0x33,
	MYRB_CMD_WRITE_EXTENDED =	0x34,
	MYRB_CMD_READAHEAD_EXTENDED =	0x35,
	MYRB_CMD_READ_EXTENDED_SG =	0xB3,
	MYRB_CMD_WRITE_EXTENDED_SG =	0xB4,
	MYRB_CMD_READ =			0x36,
	MYRB_CMD_READ_SG =		0xB6,
	MYRB_CMD_WRITE =		0x37,
	MYRB_CMD_WRITE_SG =		0xB7,
	MYRB_CMD_DCDB =			0x04,
	MYRB_CMD_DCDB_SG =		0x84,
	MYRB_CMD_FLUSH =		0x0A,
	/* Controller Status Related Commands */
	MYRB_CMD_ENQUIRY =		0x53,
	MYRB_CMD_ENQUIRY2 =		0x1C,
	MYRB_CMD_GET_LDRV_ELEMENT =	0x55,
	MYRB_CMD_GET_LDEV_INFO =	0x19,
	MYRB_CMD_IOPORTREAD =		0x39,
	MYRB_CMD_IOPORTWRITE =		0x3A,
	MYRB_CMD_GET_SD_STATS =		0x3E,
	MYRB_CMD_GET_PD_STATS =		0x3F,
	MYRB_CMD_EVENT_LOG_OPERATION =	0x72,
	/* Device Related Commands */
	MYRB_CMD_START_DEVICE =		0x10,
	MYRB_CMD_GET_DEVICE_STATE =	0x50,
	MYRB_CMD_STOP_CHANNEL =		0x13,
	MYRB_CMD_START_CHANNEL =	0x12,
	MYRB_CMD_RESET_CHANNEL =	0x1A,
	/* Commands Associated with Data Consistency and Errors */
	MYRB_CMD_REBUILD =		0x09,
	MYRB_CMD_REBUILD_ASYNC =	0x16,
	MYRB_CMD_CHECK_CONSISTENCY =	0x0F,
	MYRB_CMD_CHECK_CONSISTENCY_ASYNC = 0x1E,
	MYRB_CMD_REBUILD_STAT =		0x0C,
	MYRB_CMD_GET_REBUILD_PROGRESS =	0x27,
	MYRB_CMD_REBUILD_CONTROL =	0x1F,
	MYRB_CMD_READ_BADBLOCK_TABLE =	0x0B,
	MYRB_CMD_READ_BADDATA_TABLE =	0x25,
	MYRB_CMD_CLEAR_BADDATA_TABLE =	0x26,
	MYRB_CMD_GET_ERROR_TABLE =	0x17,
	MYRB_CMD_ADD_CAPACITY_ASYNC =	0x2A,
	MYRB_CMD_BGI_CONTROL =		0x2B,
	/* Configuration Related Commands */
	MYRB_CMD_READ_CONFIG2 =		0x3D,
	MYRB_CMD_WRITE_CONFIG2 =	0x3C,
	MYRB_CMD_READ_CONFIG_ONDISK =	0x4A,
	MYRB_CMD_WRITE_CONFIG_ONDISK =	0x4B,
	MYRB_CMD_READ_CONFIG =		0x4E,
	MYRB_CMD_READ_BACKUP_CONFIG =	0x4D,
	MYRB_CMD_WRITE_CONFIG =		0x4F,
	MYRB_CMD_ADD_CONFIG =		0x4C,
	MYRB_CMD_READ_CONFIG_LABEL =	0x48,
	MYRB_CMD_WRITE_CONFIG_LABEL =	0x49,
	/* Firmware Upgrade Related Commands */
	MYRB_CMD_LOAD_IMAGE =		0x20,
	MYRB_CMD_STORE_IMAGE =		0x21,
	MYRB_CMD_PROGRAM_IMAGE =	0x22,
	/* Diagnostic Commands */
	MYRB_CMD_SET_DIAGNOSTIC_MODE =	0x31,
	MYRB_CMD_RUN_DIAGNOSTIC =	0x32,
	/* Subsystem Service Commands */
	MYRB_CMD_GET_SUBSYS_DATA =	0x70,
	MYRB_CMD_SET_SUBSYS_PARAM =	0x71,
	/* Version 2.xx Firmware Commands */
	MYRB_CMD_ENQUIRY_OLD =		0x05,
	MYRB_CMD_GET_DEVICE_STATE_OLD =	0x14,
	MYRB_CMD_READ_OLD =		0x02,
	MYRB_CMD_WRITE_OLD =		0x03,
	MYRB_CMD_READ_SG_OLD =		0x82,
	MYRB_CMD_WRITE_SG_OLD =		0x83
} __packed;

/*
 * DAC960 V1 Firmware Command Status Codes.
 */
#define MYRB_STATUS_SUCCESS			0x0000	/* Common */
#define MYRB_STATUS_CHECK_CONDITION		0x0002	/* Common */
#define MYRB_STATUS_NO_DEVICE			0x0102	/* Common */
#define MYRB_STATUS_INVALID_ADDRESS		0x0105	/* Common */
#define MYRB_STATUS_INVALID_PARAM		0x0105	/* Common */
#define MYRB_STATUS_IRRECOVERABLE_DATA_ERROR	0x0001	/* I/O */
#define MYRB_STATUS_LDRV_NONEXISTENT_OR_OFFLINE 0x0002	/* I/O */
#define MYRB_STATUS_ACCESS_BEYOND_END_OF_LDRV	0x0105	/* I/O */
#define MYRB_STATUS_BAD_DATA			0x010C	/* I/O */
#define MYRB_STATUS_DEVICE_BUSY			0x0008	/* DCDB */
#define MYRB_STATUS_DEVICE_NONRESPONSIVE	0x000E	/* DCDB */
#define MYRB_STATUS_COMMAND_TERMINATED		0x000F	/* DCDB */
#define MYRB_STATUS_START_DEVICE_FAILED		0x0002	/* Device */
#define MYRB_STATUS_INVALID_CHANNEL_OR_TARGET	0x0105	/* Device */
#define MYRB_STATUS_CHANNEL_BUSY		0x0106	/* Device */
#define MYRB_STATUS_OUT_OF_MEMORY		0x0107	/* Device */
#define MYRB_STATUS_CHANNEL_NOT_STOPPED		0x0002	/* Device */
#define MYRB_STATUS_ATTEMPT_TO_RBLD_ONLINE_DRIVE 0x0002	/* Consistency */
#define MYRB_STATUS_RBLD_BADBLOCKS		0x0003	/* Consistency */
#define MYRB_STATUS_RBLD_NEW_DISK_FAILED	0x0004	/* Consistency */
#define MYRB_STATUS_RBLD_OR_CHECK_INPROGRESS	0x0106	/* Consistency */
#define MYRB_STATUS_DEPENDENT_DISK_DEAD		0x0002	/* Consistency */
#define MYRB_STATUS_INCONSISTENT_BLOCKS		0x0003	/* Consistency */
#define MYRB_STATUS_INVALID_OR_NONREDUNDANT_LDRV 0x0105 /* Consistency */
#define MYRB_STATUS_NO_RBLD_OR_CHECK_INPROGRESS	0x0105	/* Consistency */
#define MYRB_STATUS_RBLD_IN_PROGRESS_DATA_VALID	0x0000	/* Consistency */
#define MYRB_STATUS_RBLD_FAILED_LDEV_FAILURE	0x0002	/* Consistency */
#define MYRB_STATUS_RBLD_FAILED_BADBLOCKS	0x0003	/* Consistency */
#define MYRB_STATUS_RBLD_FAILED_NEW_DRIVE_FAILED 0x0004	/* Consistency */
#define MYRB_STATUS_RBLD_SUCCESS		0x0100	/* Consistency */
#define MYRB_STATUS_RBLD_SUCCESS_TERMINATED	0x0107	/* Consistency */
#define MYRB_STATUS_RBLD_NOT_CHECKED		0x0108	/* Consistency */
#define MYRB_STATUS_BGI_SUCCESS			0x0100	/* Consistency */
#define MYRB_STATUS_BGI_ABORTED			0x0005	/* Consistency */
#define MYRB_STATUS_NO_BGI_INPROGRESS		0x0105	/* Consistency */
#define MYRB_STATUS_ADD_CAPACITY_INPROGRESS	0x0004	/* Consistency */
#define MYRB_STATUS_ADD_CAPACITY_FAILED_OR_SUSPENDED 0x00F4 /* Consistency */
#define MYRB_STATUS_CONFIG2_CSUM_ERROR		0x0002	/* Configuration */
#define MYRB_STATUS_CONFIGURATION_SUSPENDED	0x0106	/* Configuration */
#define MYRB_STATUS_FAILED_TO_CONFIGURE_NVRAM	0x0105	/* Configuration */
#define MYRB_STATUS_CONFIGURATION_NOT_SAVED	0x0106	/* Configuration */
#define MYRB_STATUS_SUBSYS_NOTINSTALLED		0x0001	/* Subsystem */
#define MYRB_STATUS_SUBSYS_FAILED		0x0002	/* Subsystem */
#define MYRB_STATUS_SUBSYS_BUSY			0x0106	/* Subsystem */
#define MYRB_STATUS_SUBSYS_TIMEOUT		0x0108	/* Subsystem */

/*
 * DAC960 V1 Firmware Enquiry Command reply structure.
 */
struct myrb_enquiry {
	unsigned char ldev_count;			/* Byte 0 */
	unsigned int rsvd1:24;				/* Bytes 1-3 */
	unsigned int ldev_sizes[32];			/* Bytes 4-131 */
	unsigned short flash_age;			/* Bytes 132-133 */
	struct {
		unsigned char deferred:1;		/* Byte 134 Bit 0 */
		unsigned char low_bat:1;		/* Byte 134 Bit 1 */
		unsigned char rsvd2:6;			/* Byte 134 Bits 2-7 */
	} status;
	unsigned char rsvd3:8;				/* Byte 135 */
	unsigned char fw_minor_version;			/* Byte 136 */
	unsigned char fw_major_version;			/* Byte 137 */
	enum {
		MYRB_NO_STDBY_RBLD_OR_CHECK_IN_PROGRESS =	0x00,
		MYRB_STDBY_RBLD_IN_PROGRESS =			0x01,
		MYRB_BG_RBLD_IN_PROGRESS =			0x02,
		MYRB_BG_CHECK_IN_PROGRESS =			0x03,
		MYRB_STDBY_RBLD_COMPLETED_WITH_ERROR =		0xFF,
		MYRB_BG_RBLD_OR_CHECK_FAILED_DRIVE_FAILED =	0xF0,
		MYRB_BG_RBLD_OR_CHECK_FAILED_LDEV_FAILED =	0xF1,
		MYRB_BG_RBLD_OR_CHECK_FAILED_OTHER =		0xF2,
		MYRB_BG_RBLD_OR_CHECK_SUCCESS_TERMINATED =	0xF3
	} __packed rbld;		/* Byte 138 */
	unsigned char max_tcq;				/* Byte 139 */
	unsigned char ldev_offline;			/* Byte 140 */
	unsigned char rsvd4:8;				/* Byte 141 */
	unsigned short ev_seq;				/* Bytes 142-143 */
	unsigned char ldev_critical;			/* Byte 144 */
	unsigned int rsvd5:24;				/* Bytes 145-147 */
	unsigned char pdev_dead;			/* Byte 148 */
	unsigned char rsvd6:8;				/* Byte 149 */
	unsigned char rbld_count;			/* Byte 150 */
	struct {
		unsigned char rsvd7:3;			/* Byte 151 Bits 0-2 */
		unsigned char bbu_present:1;		/* Byte 151 Bit 3 */
		unsigned char rsvd8:4;			/* Byte 151 Bits 4-7 */
	} misc;
	struct {
		unsigned char target;
		unsigned char channel;
	} dead_drives[21];				/* Bytes 152-194 */
	unsigned char rsvd9[62];			/* Bytes 195-255 */
} __packed;

/*
 * DAC960 V1 Firmware Enquiry2 Command reply structure.
 */
struct myrb_enquiry2 {
	struct {
		enum {
			DAC960_V1_P_PD_PU =			0x01,
			DAC960_V1_PL =				0x02,
			DAC960_V1_PG =				0x10,
			DAC960_V1_PJ =				0x11,
			DAC960_V1_PR =				0x12,
			DAC960_V1_PT =				0x13,
			DAC960_V1_PTL0 =			0x14,
			DAC960_V1_PRL =				0x15,
			DAC960_V1_PTL1 =			0x16,
			DAC960_V1_1164P =			0x20
		} __packed sub_model;		/* Byte 0 */
		unsigned char actual_channels;			/* Byte 1 */
		enum {
			MYRB_5_CHANNEL_BOARD =		0x01,
			MYRB_3_CHANNEL_BOARD =		0x02,
			MYRB_2_CHANNEL_BOARD =		0x03,
			MYRB_3_CHANNEL_ASIC_DAC =	0x04
		} __packed model;		/* Byte 2 */
		enum {
			MYRB_EISA_CONTROLLER =		0x01,
			MYRB_MCA_CONTROLLER =		0x02,
			MYRB_PCI_CONTROLLER =		0x03,
			MYRB_SCSI_TO_SCSI =		0x08
		} __packed controller;	/* Byte 3 */
	} hw;						/* Bytes 0-3 */
	/* MajorVersion.MinorVersion-FirmwareType-TurnID */
	struct {
		unsigned char major_version;		/* Byte 4 */
		unsigned char minor_version;		/* Byte 5 */
		unsigned char turn_id;			/* Byte 6 */
		char firmware_type;			/* Byte 7 */
	} fw;						/* Bytes 4-7 */
	unsigned int rsvd1;				/* Byte 8-11 */
	unsigned char cfg_chan;				/* Byte 12 */
	unsigned char cur_chan;				/* Byte 13 */
	unsigned char max_targets;			/* Byte 14 */
	unsigned char max_tcq;				/* Byte 15 */
	unsigned char max_ldev;				/* Byte 16 */
	unsigned char max_arms;				/* Byte 17 */
	unsigned char max_spans;			/* Byte 18 */
	unsigned char rsvd2;				/* Byte 19 */
	unsigned int rsvd3;				/* Bytes 20-23 */
	unsigned int mem_size;				/* Bytes 24-27 */
	unsigned int cache_size;			/* Bytes 28-31 */
	unsigned int flash_size;			/* Bytes 32-35 */
	unsigned int nvram_size;			/* Bytes 36-39 */
	struct {
		enum {
			MYRB_RAM_TYPE_DRAM =		0x0,
			MYRB_RAM_TYPE_EDO =			0x1,
			MYRB_RAM_TYPE_SDRAM =		0x2,
			MYRB_RAM_TYPE_Last =		0x7
		} __packed ram:3;	/* Byte 40 Bits 0-2 */
		enum {
			MYRB_ERR_CORR_None =	0x0,
			MYRB_ERR_CORR_Parity =	0x1,
			MYRB_ERR_CORR_ECC =		0x2,
			MYRB_ERR_CORR_Last =	0x7
		} __packed ec:3;	/* Byte 40 Bits 3-5 */
		unsigned char fast_page:1;		/* Byte 40 Bit 6 */
		unsigned char low_power:1;		/* Byte 40 Bit 7 */
		unsigned char rsvd4;			/* Bytes 41 */
	} mem_type;
	unsigned short clock_speed;			/* Bytes 42-43 */
	unsigned short mem_speed;			/* Bytes 44-45 */
	unsigned short hw_speed;			/* Bytes 46-47 */
	unsigned char rsvd5[12];			/* Bytes 48-59 */
	unsigned short max_cmds;			/* Bytes 60-61 */
	unsigned short max_sge;				/* Bytes 62-63 */
	unsigned short max_drv_cmds;			/* Bytes 64-65 */
	unsigned short max_io_desc;			/* Bytes 66-67 */
	unsigned short max_sectors;			/* Bytes 68-69 */
	unsigned char latency;				/* Byte 70 */
	unsigned char rsvd6;				/* Byte 71 */
	unsigned char scsi_tmo;				/* Byte 72 */
	unsigned char rsvd7;				/* Byte 73 */
	unsigned short min_freelines;			/* Bytes 74-75 */
	unsigned char rsvd8[8];				/* Bytes 76-83 */
	unsigned char rbld_rate_const;			/* Byte 84 */
	unsigned char rsvd9[11];			/* Byte 85-95 */
	unsigned short pdrv_block_size;			/* Bytes 96-97 */
	unsigned short ldev_block_size;			/* Bytes 98-99 */
	unsigned short max_blocks_per_cmd;		/* Bytes 100-101 */
	unsigned short block_factor;			/* Bytes 102-103 */
	unsigned short cacheline_size;			/* Bytes 104-105 */
	struct {
		enum {
			MYRB_WIDTH_NARROW_8BIT =		0x0,
			MYRB_WIDTH_WIDE_16BIT =			0x1,
			MYRB_WIDTH_WIDE_32BIT =			0x2
		} __packed bus_width:2;	/* Byte 106 Bits 0-1 */
		enum {
			MYRB_SCSI_SPEED_FAST =			0x0,
			MYRB_SCSI_SPEED_ULTRA =			0x1,
			MYRB_SCSI_SPEED_ULTRA2 =		0x2
		} __packed bus_speed:2;	/* Byte 106 Bits 2-3 */
		unsigned char differential:1;		/* Byte 106 Bit 4 */
		unsigned char rsvd10:3;			/* Byte 106 Bits 5-7 */
	} scsi_cap;
	unsigned char rsvd11[5];			/* Byte 107-111 */
	unsigned short fw_build;			/* Bytes 112-113 */
	enum {
		MYRB_FAULT_AEMI =				0x01,
		MYRB_FAULT_OEM1 =				0x02,
		MYRB_FAULT_OEM2 =				0x04,
		MYRB_FAULT_OEM3 =				0x08,
		MYRB_FAULT_CONNER =				0x10,
		MYRB_FAULT_SAFTE =				0x20
	} __packed fault_mgmt;		/* Byte 114 */
	unsigned char rsvd12;				/* Byte 115 */
	struct {
		unsigned int clustering:1;		/* Byte 116 Bit 0 */
		unsigned int online_RAID_expansion:1;	/* Byte 116 Bit 1 */
		unsigned int readahead:1;		/* Byte 116 Bit 2 */
		unsigned int bgi:1;			/* Byte 116 Bit 3 */
		unsigned int rsvd13:28;			/* Bytes 116-119 */
	} fw_features;
	unsigned char rsvd14[8];			/* Bytes 120-127 */
} __packed;

/*
 * DAC960 V1 Firmware Logical Drive State type.
 */
enum myrb_devstate {
	MYRB_DEVICE_DEAD =		0x00,
	MYRB_DEVICE_WO =		0x02,
	MYRB_DEVICE_ONLINE =		0x03,
	MYRB_DEVICE_CRITICAL =		0x04,
	MYRB_DEVICE_STANDBY =		0x10,
	MYRB_DEVICE_OFFLINE =		0xFF
} __packed;

/*
 * DAC960 V1 RAID Levels
 */
enum myrb_raidlevel {
	MYRB_RAID_LEVEL0 =		0x0,     /* RAID 0 */
	MYRB_RAID_LEVEL1 =		0x1,     /* RAID 1 */
	MYRB_RAID_LEVEL3 =		0x3,     /* RAID 3 */
	MYRB_RAID_LEVEL5 =		0x5,     /* RAID 5 */
	MYRB_RAID_LEVEL6 =		0x6,     /* RAID 6 */
	MYRB_RAID_JBOD =		0x7,     /* RAID 7 (JBOD) */
} __packed;

/*
 * DAC960 V1 Firmware Logical Drive Information structure.
 */
struct myrb_ldev_info {
	unsigned int size;				/* Bytes 0-3 */
	enum myrb_devstate state;			/* Byte 4 */
	unsigned int raid_level:7;			/* Byte 5 Bits 0-6 */
	unsigned int wb_enabled:1;			/* Byte 5 Bit 7 */
	unsigned int rsvd:16;				/* Bytes 6-7 */
};

/*
 * DAC960 V1 Firmware Perform Event Log Operation Types.
 */
#define DAC960_V1_GetEventLogEntry		0x00

/*
 * DAC960 V1 Firmware Get Event Log Entry Command reply structure.
 */
struct myrb_log_entry {
	unsigned char msg_type;			/* Byte 0 */
	unsigned char msg_len;			/* Byte 1 */
	unsigned char target:5;			/* Byte 2 Bits 0-4 */
	unsigned char channel:3;		/* Byte 2 Bits 5-7 */
	unsigned char lun:6;			/* Byte 3 Bits 0-5 */
	unsigned char rsvd1:2;			/* Byte 3 Bits 6-7 */
	unsigned short seq_num;			/* Bytes 4-5 */
	unsigned char sense[26];		/* Bytes 6-31 */
};

/*
 * DAC960 V1 Firmware Get Device State Command reply structure.
 * The structure is padded by 2 bytes for compatibility with Version 2.xx
 * Firmware.
 */
struct myrb_pdev_state {
	unsigned int present:1;			/* Byte 0 Bit 0 */
	unsigned int :7;				/* Byte 0 Bits 1-7 */
	enum {
		MYRB_TYPE_OTHER =			0x0,
		MYRB_TYPE_DISK =			0x1,
		MYRB_TYPE_TAPE =			0x2,
		MYRB_TYPE_CDROM_OR_WORM =		0x3
	} __packed devtype:2;		/* Byte 1 Bits 0-1 */
	unsigned int rsvd1:1;				/* Byte 1 Bit 2 */
	unsigned int fast20:1;				/* Byte 1 Bit 3 */
	unsigned int sync:1;				/* Byte 1 Bit 4 */
	unsigned int fast:1;				/* Byte 1 Bit 5 */
	unsigned int wide:1;				/* Byte 1 Bit 6 */
	unsigned int tcq_supported:1;			/* Byte 1 Bit 7 */
	enum myrb_devstate state;			/* Byte 2 */
	unsigned int rsvd2:8;				/* Byte 3 */
	unsigned int sync_multiplier;			/* Byte 4 */
	unsigned int sync_offset:5;			/* Byte 5 Bits 0-4 */
	unsigned int rsvd3:3;				/* Byte 5 Bits 5-7 */
	unsigned int size;				/* Bytes 6-9 */
	unsigned int rsvd4:16;			/* Bytes 10-11 */
} __packed;

/*
 * DAC960 V1 Firmware Get Rebuild Progress Command reply structure.
 */
struct myrb_rbld_progress {
	unsigned int ldev_num;				/* Bytes 0-3 */
	unsigned int ldev_size;				/* Bytes 4-7 */
	unsigned int blocks_left;			/* Bytes 8-11 */
};

/*
 * DAC960 V1 Firmware Background Initialization Status Command reply structure.
 */
struct myrb_bgi_status {
	unsigned int ldev_size;				/* Bytes 0-3 */
	unsigned int blocks_done;			/* Bytes 4-7 */
	unsigned char rsvd1[12];			/* Bytes 8-19 */
	unsigned int ldev_num;				/* Bytes 20-23 */
	unsigned char raid_level;			/* Byte 24 */
	enum {
		MYRB_BGI_INVALID =	0x00,
		MYRB_BGI_STARTED =	0x02,
		MYRB_BGI_INPROGRESS =	0x04,
		MYRB_BGI_SUSPENDED =	0x05,
		MYRB_BGI_CANCELLED =	0x06
	} __packed status;		/* Byte 25 */
	unsigned char rsvd2[6];				/* Bytes 26-31 */
};

/*
 * DAC960 V1 Firmware Error Table Entry structure.
 */
struct myrb_error_entry {
	unsigned char parity_err;			/* Byte 0 */
	unsigned char soft_err;				/* Byte 1 */
	unsigned char hard_err;				/* Byte 2 */
	unsigned char misc_err;				/* Byte 3 */
};

/*
 * DAC960 V1 Firmware Read Config2 Command reply structure.
 */
struct myrb_config2 {
	unsigned rsvd1:1;				/* Byte 0 Bit 0 */
	unsigned active_negation:1;			/* Byte 0 Bit 1 */
	unsigned rsvd2:5;				/* Byte 0 Bits 2-6 */
	unsigned no_rescan_on_reset_during_scan:1;	/* Byte 0 Bit 7 */
	unsigned StorageWorks_support:1;		/* Byte 1 Bit 0 */
	unsigned HewlettPackard_support:1;		/* Byte 1 Bit 1 */
	unsigned no_disconnect_on_first_command:1;	/* Byte 1 Bit 2 */
	unsigned rsvd3:2;				/* Byte 1 Bits 3-4 */
	unsigned AEMI_ARM:1;				/* Byte 1 Bit 5 */
	unsigned AEMI_OFM:1;				/* Byte 1 Bit 6 */
	unsigned rsvd4:1;				/* Byte 1 Bit 7 */
	enum {
		MYRB_OEMID_MYLEX =		0x00,
		MYRB_OEMID_IBM =		0x08,
		MYRB_OEMID_HP =			0x0A,
		MYRB_OEMID_DEC =		0x0C,
		MYRB_OEMID_SIEMENS =		0x10,
		MYRB_OEMID_INTEL =		0x12
	} __packed OEMID;		/* Byte 2 */
	unsigned char oem_model_number;			/* Byte 3 */
	unsigned char physical_sector;			/* Byte 4 */
	unsigned char logical_sector;			/* Byte 5 */
	unsigned char block_factor;			/* Byte 6 */
	unsigned readahead_enabled:1;			/* Byte 7 Bit 0 */
	unsigned low_BIOS_delay:1;			/* Byte 7 Bit 1 */
	unsigned rsvd5:2;				/* Byte 7 Bits 2-3 */
	unsigned restrict_reassign_to_one_sector:1;	/* Byte 7 Bit 4 */
	unsigned rsvd6:1;				/* Byte 7 Bit 5 */
	unsigned FUA_during_write_recovery:1;		/* Byte 7 Bit 6 */
	unsigned enable_LeftSymmetricRAID5Algorithm:1;	/* Byte 7 Bit 7 */
	unsigned char default_rebuild_rate;		/* Byte 8 */
	unsigned char rsvd7;				/* Byte 9 */
	unsigned char blocks_per_cacheline;		/* Byte 10 */
	unsigned char blocks_per_stripe;		/* Byte 11 */
	struct {
		enum {
			MYRB_SPEED_ASYNC =		0x0,
			MYRB_SPEED_SYNC_8MHz =		0x1,
			MYRB_SPEED_SYNC_5MHz =		0x2,
			MYRB_SPEED_SYNC_10_OR_20MHz =	0x3
		} __packed speed:2;	/* Byte 11 Bits 0-1 */
		unsigned force_8bit:1;			/* Byte 11 Bit 2 */
		unsigned disable_fast20:1;		/* Byte 11 Bit 3 */
		unsigned rsvd8:3;			/* Byte 11 Bits 4-6 */
		unsigned enable_tcq:1;			/* Byte 11 Bit 7 */
	} __packed channelparam[6];	/* Bytes 12-17 */
	unsigned char SCSIInitiatorID;			/* Byte 18 */
	unsigned char rsvd9;				/* Byte 19 */
	enum {
		MYRB_STARTUP_CONTROLLER_SPINUP =	0x00,
		MYRB_STARTUP_POWERON_SPINUP =		0x01
	} __packed startup;		/* Byte 20 */
	unsigned char simultaneous_device_spinup_count;	/* Byte 21 */
	unsigned char seconds_delay_between_spinups;	/* Byte 22 */
	unsigned char rsvd10[29];			/* Bytes 23-51 */
	unsigned BIOS_disabled:1;			/* Byte 52 Bit 0 */
	unsigned CDROM_boot_enabled:1;			/* Byte 52 Bit 1 */
	unsigned rsvd11:3;				/* Byte 52 Bits 2-4 */
	enum {
		MYRB_GEOM_128_32 =		0x0,
		MYRB_GEOM_255_63 =		0x1,
		MYRB_GEOM_RESERVED1 =		0x2,
		MYRB_GEOM_RESERVED2 =		0x3
	} __packed drive_geometry:2;	/* Byte 52 Bits 5-6 */
	unsigned rsvd12:1;				/* Byte 52 Bit 7 */
	unsigned char rsvd13[9];			/* Bytes 53-61 */
	unsigned short csum;				/* Bytes 62-63 */
};

/*
 * DAC960 V1 Firmware DCDB request structure.
 */
struct myrb_dcdb {
	unsigned target:4;				 /* Byte 0 Bits 0-3 */
	unsigned channel:4;				 /* Byte 0 Bits 4-7 */
	enum {
		MYRB_DCDB_XFER_NONE =		0,
		MYRB_DCDB_XFER_DEVICE_TO_SYSTEM = 1,
		MYRB_DCDB_XFER_SYSTEM_TO_DEVICE = 2,
		MYRB_DCDB_XFER_ILLEGAL =	3
	} __packed data_xfer:2;				/* Byte 1 Bits 0-1 */
	unsigned early_status:1;			/* Byte 1 Bit 2 */
	unsigned rsvd1:1;				/* Byte 1 Bit 3 */
	enum {
		MYRB_DCDB_TMO_24_HRS =	0,
		MYRB_DCDB_TMO_10_SECS =	1,
		MYRB_DCDB_TMO_60_SECS =	2,
		MYRB_DCDB_TMO_10_MINS =	3
	} __packed timeout:2;				/* Byte 1 Bits 4-5 */
	unsigned no_autosense:1;			/* Byte 1 Bit 6 */
	unsigned allow_disconnect:1;			/* Byte 1 Bit 7 */
	unsigned short xfer_len_lo;			/* Bytes 2-3 */
	u32 dma_addr;					/* Bytes 4-7 */
	unsigned char cdb_len:4;			/* Byte 8 Bits 0-3 */
	unsigned char xfer_len_hi4:4;			/* Byte 8 Bits 4-7 */
	unsigned char sense_len;			/* Byte 9 */
	unsigned char cdb[12];				/* Bytes 10-21 */
	unsigned char sense[64];			/* Bytes 22-85 */
	unsigned char status;				/* Byte 86 */
	unsigned char rsvd2;				/* Byte 87 */
};

/*
 * DAC960 V1 Firmware Scatter/Gather List Type 1 32 Bit Address
 *32 Bit Byte Count structure.
 */
struct myrb_sge {
	u32 sge_addr;		/* Bytes 0-3 */
	u32 sge_count;		/* Bytes 4-7 */
};

/*
 * 13 Byte DAC960 V1 Firmware Command Mailbox structure.
 * Bytes 13-15 are not used.  The structure is padded to 16 bytes for
 * efficient access.
 */
union myrb_cmd_mbox {
	unsigned int words[4];				/* Words 0-3 */
	unsigned char bytes[16];			/* Bytes 0-15 */
	struct {
		enum myrb_cmd_opcode opcode;		/* Byte 0 */
		unsigned char id;			/* Byte 1 */
		unsigned char rsvd[14];			/* Bytes 2-15 */
	} __packed common;
	struct {
		enum myrb_cmd_opcode opcode;		/* Byte 0 */
		unsigned char id;			/* Byte 1 */
		unsigned char rsvd1[6];			/* Bytes 2-7 */
		u32 addr;				/* Bytes 8-11 */
		unsigned char rsvd2[4];			/* Bytes 12-15 */
	} __packed type3;
	struct {
		enum myrb_cmd_opcode opcode;		/* Byte 0 */
		unsigned char id;			/* Byte 1 */
		unsigned char optype;			/* Byte 2 */
		unsigned char rsvd1[5];			/* Bytes 3-7 */
		u32 addr;				/* Bytes 8-11 */
		unsigned char rsvd2[4];			/* Bytes 12-15 */
	} __packed type3B;
	struct {
		enum myrb_cmd_opcode opcode;		/* Byte 0 */
		unsigned char id;			/* Byte 1 */
		unsigned char rsvd1[5];			/* Bytes 2-6 */
		unsigned char ldev_num:6;		/* Byte 7 Bits 0-6 */
		unsigned char auto_restore:1;		/* Byte 7 Bit 7 */
		unsigned char rsvd2[8];			/* Bytes 8-15 */
	} __packed type3C;
	struct {
		enum myrb_cmd_opcode opcode;		/* Byte 0 */
		unsigned char id;			/* Byte 1 */
		unsigned char channel;			/* Byte 2 */
		unsigned char target;			/* Byte 3 */
		enum myrb_devstate state;		/* Byte 4 */
		unsigned char rsvd1[3];			/* Bytes 5-7 */
		u32 addr;				/* Bytes 8-11 */
		unsigned char rsvd2[4];			/* Bytes 12-15 */
	} __packed type3D;
	struct {
		enum myrb_cmd_opcode opcode;		/* Byte 0 */
		unsigned char id;			/* Byte 1 */
		unsigned char optype;			/* Byte 2 */
		unsigned char opqual;			/* Byte 3 */
		unsigned short ev_seq;			/* Bytes 4-5 */
		unsigned char rsvd1[2];			/* Bytes 6-7 */
		u32 addr;				/* Bytes 8-11 */
		unsigned char rsvd2[4];			/* Bytes 12-15 */
	} __packed type3E;
	struct {
		enum myrb_cmd_opcode opcode;		/* Byte 0 */
		unsigned char id;			/* Byte 1 */
		unsigned char rsvd1[2];			/* Bytes 2-3 */
		unsigned char rbld_rate;		/* Byte 4 */
		unsigned char rsvd2[3];			/* Bytes 5-7 */
		u32 addr;				/* Bytes 8-11 */
		unsigned char rsvd3[4];			/* Bytes 12-15 */
	} __packed type3R;
	struct {
		enum myrb_cmd_opcode opcode;		/* Byte 0 */
		unsigned char id;			/* Byte 1 */
		unsigned short xfer_len;		/* Bytes 2-3 */
		unsigned int lba;			/* Bytes 4-7 */
		u32 addr;				/* Bytes 8-11 */
		unsigned char ldev_num;			/* Byte 12 */
		unsigned char rsvd[3];			/* Bytes 13-15 */
	} __packed type4;
	struct {
		enum myrb_cmd_opcode opcode;		/* Byte 0 */
		unsigned char id;			/* Byte 1 */
		struct {
			unsigned short xfer_len:11;	/* Bytes 2-3 */
			unsigned char ldev_num:5;	/* Byte 3 Bits 3-7 */
		} __packed ld;
		unsigned int lba;			/* Bytes 4-7 */
		u32 addr;				/* Bytes 8-11 */
		unsigned char sg_count:6;		/* Byte 12 Bits 0-5 */
		enum {
			MYRB_SGL_ADDR32_COUNT32 = 0x0,
			MYRB_SGL_ADDR32_COUNT16 = 0x1,
			MYRB_SGL_COUNT32_ADDR32 = 0x2,
			MYRB_SGL_COUNT16_ADDR32 = 0x3
		} __packed sg_type:2;	/* Byte 12 Bits 6-7 */
		unsigned char rsvd[3];			/* Bytes 13-15 */
	} __packed type5;
	struct {
		enum myrb_cmd_opcode opcode;		/* Byte 0 */
		unsigned char id;			/* Byte 1 */
		unsigned char opcode2;			/* Byte 2 */
		unsigned char rsvd1:8;			/* Byte 3 */
		u32 cmd_mbox_addr;			/* Bytes 4-7 */
		u32 stat_mbox_addr;			/* Bytes 8-11 */
		unsigned char rsvd2[4];			/* Bytes 12-15 */
	} __packed typeX;
};

/*
 * DAC960 V1 Firmware Controller Status Mailbox structure.
 */
struct myrb_stat_mbox {
	unsigned char id;		/* Byte 0 */
	unsigned char rsvd:7;		/* Byte 1 Bits 0-6 */
	unsigned char valid:1;			/* Byte 1 Bit 7 */
	unsigned short status;		/* Bytes 2-3 */
};

struct myrb_cmdblk {
	union myrb_cmd_mbox mbox;
	unsigned short status;
	struct completion *completion;
	struct myrb_dcdb *dcdb;
	dma_addr_t dcdb_addr;
	struct myrb_sge *sgl;
	dma_addr_t sgl_addr;
};

struct myrb_hba {
	unsigned int ldev_block_size;
	unsigned char ldev_geom_heads;
	unsigned char ldev_geom_sectors;
	unsigned char bus_width;
	unsigned short stripe_size;
	unsigned short segment_size;
	unsigned short new_ev_seq;
	unsigned short old_ev_seq;
	bool dual_mode_interface;
	bool bgi_status_supported;
	bool safte_enabled;
	bool need_ldev_info;
	bool need_err_info;
	bool need_rbld;
	bool need_cc_status;
	bool need_bgi_status;
	bool rbld_first;

	struct pci_dev *pdev;
	struct Scsi_Host *host;

	struct workqueue_struct *work_q;
	struct delayed_work monitor_work;
	unsigned long primary_monitor_time;
	unsigned long secondary_monitor_time;

	struct dma_pool *sg_pool;
	struct dma_pool *dcdb_pool;

	spinlock_t queue_lock;

	void (*qcmd)(struct myrb_hba *cs, struct myrb_cmdblk *cmd_blk);
	void (*write_cmd_mbox)(union myrb_cmd_mbox *next_mbox,
			       union myrb_cmd_mbox *cmd_mbox);
	void (*get_cmd_mbox)(void __iomem *base);
	void (*disable_intr)(void __iomem *base);
	void (*reset)(void __iomem *base);

	unsigned int ctlr_num;
	unsigned char model_name[20];
	unsigned char fw_version[12];

	unsigned int irq;
	phys_addr_t io_addr;
	phys_addr_t pci_addr;
	void __iomem *io_base;
	void __iomem *mmio_base;

	size_t cmd_mbox_size;
	dma_addr_t cmd_mbox_addr;
	union myrb_cmd_mbox *first_cmd_mbox;
	union myrb_cmd_mbox *last_cmd_mbox;
	union myrb_cmd_mbox *next_cmd_mbox;
	union myrb_cmd_mbox *prev_cmd_mbox1;
	union myrb_cmd_mbox *prev_cmd_mbox2;

	size_t stat_mbox_size;
	dma_addr_t stat_mbox_addr;
	struct myrb_stat_mbox *first_stat_mbox;
	struct myrb_stat_mbox *last_stat_mbox;
	struct myrb_stat_mbox *next_stat_mbox;

	struct myrb_cmdblk dcmd_blk;
	struct myrb_cmdblk mcmd_blk;
	struct mutex dcmd_mutex;

	struct myrb_enquiry *enquiry;
	dma_addr_t enquiry_addr;

	struct myrb_error_entry *err_table;
	dma_addr_t err_table_addr;

	unsigned short last_rbld_status;

	struct myrb_ldev_info *ldev_info_buf;
	dma_addr_t ldev_info_addr;

	struct myrb_bgi_status bgi_status;

	struct mutex dma_mutex;
};

/*
 * DAC960 LA Series Controller Interface Register Offsets.
 */
#define DAC960_LA_mmio_size		0x80

enum DAC960_LA_reg_offset {
	DAC960_LA_IRQMASK_OFFSET	= 0x34,
	DAC960_LA_CMDOP_OFFSET		= 0x50,
	DAC960_LA_CMDID_OFFSET		= 0x51,
	DAC960_LA_MBOX2_OFFSET		= 0x52,
	DAC960_LA_MBOX3_OFFSET		= 0x53,
	DAC960_LA_MBOX4_OFFSET		= 0x54,
	DAC960_LA_MBOX5_OFFSET		= 0x55,
	DAC960_LA_MBOX6_OFFSET		= 0x56,
	DAC960_LA_MBOX7_OFFSET		= 0x57,
	DAC960_LA_MBOX8_OFFSET		= 0x58,
	DAC960_LA_MBOX9_OFFSET		= 0x59,
	DAC960_LA_MBOX10_OFFSET		= 0x5A,
	DAC960_LA_MBOX11_OFFSET		= 0x5B,
	DAC960_LA_MBOX12_OFFSET		= 0x5C,
	DAC960_LA_STSID_OFFSET		= 0x5D,
	DAC960_LA_STS_OFFSET		= 0x5E,
	DAC960_LA_IDB_OFFSET		= 0x60,
	DAC960_LA_ODB_OFFSET		= 0x61,
	DAC960_LA_ERRSTS_OFFSET		= 0x63,
};

/*
 * DAC960 LA Series Inbound Door Bell Register.
 */
#define DAC960_LA_IDB_HWMBOX_NEW_CMD 0x01
#define DAC960_LA_IDB_HWMBOX_ACK_STS 0x02
#define DAC960_LA_IDB_GEN_IRQ 0x04
#define DAC960_LA_IDB_CTRL_RESET 0x08
#define DAC960_LA_IDB_MMBOX_NEW_CMD 0x10

#define DAC960_LA_IDB_HWMBOX_EMPTY 0x01
#define DAC960_LA_IDB_INIT_DONE 0x02

/*
 * DAC960 LA Series Outbound Door Bell Register.
 */
#define DAC960_LA_ODB_HWMBOX_ACK_IRQ 0x01
#define DAC960_LA_ODB_MMBOX_ACK_IRQ 0x02
#define DAC960_LA_ODB_HWMBOX_STS_AVAIL 0x01
#define DAC960_LA_ODB_MMBOX_STS_AVAIL 0x02

/*
 * DAC960 LA Series Interrupt Mask Register.
 */
#define DAC960_LA_IRQMASK_DISABLE_IRQ 0x04

/*
 * DAC960 LA Series Error Status Register.
 */
#define DAC960_LA_ERRSTS_PENDING 0x02

/*
 * DAC960 PG Series Controller Interface Register Offsets.
 */
#define DAC960_PG_mmio_size		0x2000

enum DAC960_PG_reg_offset {
	DAC960_PG_IDB_OFFSET		= 0x0020,
	DAC960_PG_ODB_OFFSET		= 0x002C,
	DAC960_PG_IRQMASK_OFFSET	= 0x0034,
	DAC960_PG_CMDOP_OFFSET		= 0x1000,
	DAC960_PG_CMDID_OFFSET		= 0x1001,
	DAC960_PG_MBOX2_OFFSET		= 0x1002,
	DAC960_PG_MBOX3_OFFSET		= 0x1003,
	DAC960_PG_MBOX4_OFFSET		= 0x1004,
	DAC960_PG_MBOX5_OFFSET		= 0x1005,
	DAC960_PG_MBOX6_OFFSET		= 0x1006,
	DAC960_PG_MBOX7_OFFSET		= 0x1007,
	DAC960_PG_MBOX8_OFFSET		= 0x1008,
	DAC960_PG_MBOX9_OFFSET		= 0x1009,
	DAC960_PG_MBOX10_OFFSET		= 0x100A,
	DAC960_PG_MBOX11_OFFSET		= 0x100B,
	DAC960_PG_MBOX12_OFFSET		= 0x100C,
	DAC960_PG_STSID_OFFSET		= 0x1018,
	DAC960_PG_STS_OFFSET		= 0x101A,
	DAC960_PG_ERRSTS_OFFSET		= 0x103F,
};

/*
 * DAC960 PG Series Inbound Door Bell Register.
 */
#define DAC960_PG_IDB_HWMBOX_NEW_CMD 0x01
#define DAC960_PG_IDB_HWMBOX_ACK_STS 0x02
#define DAC960_PG_IDB_GEN_IRQ 0x04
#define DAC960_PG_IDB_CTRL_RESET 0x08
#define DAC960_PG_IDB_MMBOX_NEW_CMD 0x10

#define DAC960_PG_IDB_HWMBOX_FULL 0x01
#define DAC960_PG_IDB_INIT_IN_PROGRESS 0x02

/*
 * DAC960 PG Series Outbound Door Bell Register.
 */
#define DAC960_PG_ODB_HWMBOX_ACK_IRQ 0x01
#define DAC960_PG_ODB_MMBOX_ACK_IRQ 0x02
#define DAC960_PG_ODB_HWMBOX_STS_AVAIL 0x01
#define DAC960_PG_ODB_MMBOX_STS_AVAIL 0x02

/*
 * DAC960 PG Series Interrupt Mask Register.
 */
#define DAC960_PG_IRQMASK_MSI_MASK1 0x03
#define DAC960_PG_IRQMASK_DISABLE_IRQ 0x04
#define DAC960_PG_IRQMASK_MSI_MASK2 0xF8

/*
 * DAC960 PG Series Error Status Register.
 */
#define DAC960_PG_ERRSTS_PENDING 0x04

/*
 * DAC960 PD Series Controller Interface Register Offsets.
 */
#define DAC960_PD_mmio_size		0x80

enum DAC960_PD_reg_offset {
	DAC960_PD_CMDOP_OFFSET		= 0x00,
	DAC960_PD_CMDID_OFFSET		= 0x01,
	DAC960_PD_MBOX2_OFFSET		= 0x02,
	DAC960_PD_MBOX3_OFFSET		= 0x03,
	DAC960_PD_MBOX4_OFFSET		= 0x04,
	DAC960_PD_MBOX5_OFFSET		= 0x05,
	DAC960_PD_MBOX6_OFFSET		= 0x06,
	DAC960_PD_MBOX7_OFFSET		= 0x07,
	DAC960_PD_MBOX8_OFFSET		= 0x08,
	DAC960_PD_MBOX9_OFFSET		= 0x09,
	DAC960_PD_MBOX10_OFFSET		= 0x0A,
	DAC960_PD_MBOX11_OFFSET		= 0x0B,
	DAC960_PD_MBOX12_OFFSET		= 0x0C,
	DAC960_PD_STSID_OFFSET		= 0x0D,
	DAC960_PD_STS_OFFSET		= 0x0E,
	DAC960_PD_ERRSTS_OFFSET		= 0x3F,
	DAC960_PD_IDB_OFFSET		= 0x40,
	DAC960_PD_ODB_OFFSET		= 0x41,
	DAC960_PD_IRQEN_OFFSET		= 0x43,
};

/*
 * DAC960 PD Series Inbound Door Bell Register.
 */
#define DAC960_PD_IDB_HWMBOX_NEW_CMD 0x01
#define DAC960_PD_IDB_HWMBOX_ACK_STS 0x02
#define DAC960_PD_IDB_GEN_IRQ 0x04
#define DAC960_PD_IDB_CTRL_RESET 0x08

#define DAC960_PD_IDB_HWMBOX_FULL 0x01
#define DAC960_PD_IDB_INIT_IN_PROGRESS 0x02

/*
 * DAC960 PD Series Outbound Door Bell Register.
 */
#define DAC960_PD_ODB_HWMBOX_ACK_IRQ 0x01
#define DAC960_PD_ODB_HWMBOX_STS_AVAIL 0x01

/*
 * DAC960 PD Series Interrupt Enable Register.
 */
#define DAC960_PD_IRQMASK_ENABLE_IRQ 0x01

/*
 * DAC960 PD Series Error Status Register.
 */
#define DAC960_PD_ERRSTS_PENDING 0x04

typedef int (*myrb_hw_init_t)(struct pci_dev *pdev,
			      struct myrb_hba *cb, void __iomem *base);
typedef unsigned short (*mbox_mmio_init_t)(struct pci_dev *pdev,
					   void __iomem *base,
					   union myrb_cmd_mbox *mbox);

struct myrb_privdata {
	myrb_hw_init_t		hw_init;
	irq_handler_t		irq_handler;
	unsigned int		mmio_size;
};

#endif /* MYRB_H */
