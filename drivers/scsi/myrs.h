/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Linux Driver for Mylex DAC960/AcceleRAID/eXtremeRAID PCI RAID Controllers
 *
 * This driver supports the newer, SCSI-based firmware interface only.
 *
 * Copyright 2018 Hannes Reinecke, SUSE Linux GmbH <hare@suse.com>
 *
 * Based on the original DAC960 driver, which has
 * Copyright 1998-2001 by Leonard N. Zubkoff <lnz@dandelion.com>
 * Portions Copyright 2002 by Mylex (An IBM Business Unit)
 */

#ifndef _MYRS_H
#define _MYRS_H

#define MYRS_MAILBOX_TIMEOUT 1000000

#define MYRS_DCMD_TAG 1
#define MYRS_MCMD_TAG 2

#define MYRS_LINE_BUFFER_SIZE 128

#define MYRS_PRIMARY_MONITOR_INTERVAL (10 * HZ)
#define MYRS_SECONDARY_MONITOR_INTERVAL (60 * HZ)

/* Maximum number of Scatter/Gather Segments supported */
#define MYRS_SG_LIMIT		128

/*
 * Number of Command and Status Mailboxes used by the
 * DAC960 V2 Firmware Memory Mailbox Interface.
 */
#define MYRS_MAX_CMD_MBOX		512
#define MYRS_MAX_STAT_MBOX		512

#define MYRS_DCDB_SIZE			16
#define MYRS_SENSE_SIZE			14

/*
 * DAC960 V2 Firmware Command Opcodes.
 */
enum myrs_cmd_opcode {
	MYRS_CMD_OP_MEMCOPY		= 0x01,
	MYRS_CMD_OP_SCSI_10_PASSTHRU	= 0x02,
	MYRS_CMD_OP_SCSI_255_PASSTHRU	= 0x03,
	MYRS_CMD_OP_SCSI_10		= 0x04,
	MYRS_CMD_OP_SCSI_256		= 0x05,
	MYRS_CMD_OP_IOCTL		= 0x20,
} __packed;

/*
 * DAC960 V2 Firmware IOCTL Opcodes.
 */
enum myrs_ioctl_opcode {
	MYRS_IOCTL_GET_CTLR_INFO	= 0x01,
	MYRS_IOCTL_GET_LDEV_INFO_VALID	= 0x03,
	MYRS_IOCTL_GET_PDEV_INFO_VALID	= 0x05,
	MYRS_IOCTL_GET_HEALTH_STATUS	= 0x11,
	MYRS_IOCTL_GET_EVENT		= 0x15,
	MYRS_IOCTL_START_DISCOVERY	= 0x81,
	MYRS_IOCTL_SET_DEVICE_STATE	= 0x82,
	MYRS_IOCTL_INIT_PDEV_START	= 0x84,
	MYRS_IOCTL_INIT_PDEV_STOP	= 0x85,
	MYRS_IOCTL_INIT_LDEV_START	= 0x86,
	MYRS_IOCTL_INIT_LDEV_STOP	= 0x87,
	MYRS_IOCTL_RBLD_DEVICE_START	= 0x88,
	MYRS_IOCTL_RBLD_DEVICE_STOP	= 0x89,
	MYRS_IOCTL_MAKE_CONSISTENT_START = 0x8A,
	MYRS_IOCTL_MAKE_CONSISTENT_STOP = 0x8B,
	MYRS_IOCTL_CC_START		= 0x8C,
	MYRS_IOCTL_CC_STOP		= 0x8D,
	MYRS_IOCTL_SET_MEM_MBOX		= 0x8E,
	MYRS_IOCTL_RESET_DEVICE		= 0x90,
	MYRS_IOCTL_FLUSH_DEVICE_DATA	= 0x91,
	MYRS_IOCTL_PAUSE_DEVICE		= 0x92,
	MYRS_IOCTL_UNPAUS_EDEVICE	= 0x93,
	MYRS_IOCTL_LOCATE_DEVICE	= 0x94,
	MYRS_IOCTL_CREATE_CONFIGURATION = 0xC0,
	MYRS_IOCTL_DELETE_LDEV		= 0xC1,
	MYRS_IOCTL_REPLACE_INTERNALDEVICE = 0xC2,
	MYRS_IOCTL_RENAME_LDEV		= 0xC3,
	MYRS_IOCTL_ADD_CONFIGURATION	= 0xC4,
	MYRS_IOCTL_XLATE_PDEV_TO_LDEV	= 0xC5,
	MYRS_IOCTL_CLEAR_CONFIGURATION	= 0xCA,
} __packed;

/*
 * DAC960 V2 Firmware Command Status Codes.
 */
#define MYRS_STATUS_SUCCESS			0x00
#define MYRS_STATUS_FAILED			0x02
#define MYRS_STATUS_DEVICE_BUSY			0x08
#define MYRS_STATUS_DEVICE_NON_RESPONSIVE	0x0E
#define MYRS_STATUS_DEVICE_NON_RESPONSIVE2	0x0F
#define MYRS_STATUS_RESERVATION_CONFLICT	0x18

/*
 * DAC960 V2 Firmware Memory Type structure.
 */
struct myrs_mem_type {
	enum {
		MYRS_MEMTYPE_RESERVED	= 0x00,
		MYRS_MEMTYPE_DRAM	= 0x01,
		MYRS_MEMTYPE_EDRAM	= 0x02,
		MYRS_MEMTYPE_EDO	= 0x03,
		MYRS_MEMTYPE_SDRAM	= 0x04,
		MYRS_MEMTYPE_LAST	= 0x1F,
	} __packed mem_type:5;	/* Byte 0 Bits 0-4 */
	unsigned rsvd:1;			/* Byte 0 Bit 5 */
	unsigned mem_parity:1;			/* Byte 0 Bit 6 */
	unsigned mem_ecc:1;			/* Byte 0 Bit 7 */
};

/*
 * DAC960 V2 Firmware Processor Type structure.
 */
enum myrs_cpu_type {
	MYRS_CPUTYPE_i960CA	= 0x01,
	MYRS_CPUTYPE_i960RD	= 0x02,
	MYRS_CPUTYPE_i960RN	= 0x03,
	MYRS_CPUTYPE_i960RP	= 0x04,
	MYRS_CPUTYPE_NorthBay	= 0x05,
	MYRS_CPUTYPE_StrongArm	= 0x06,
	MYRS_CPUTYPE_i960RM	= 0x07,
} __packed;

/*
 * DAC960 V2 Firmware Get Controller Info reply structure.
 */
struct myrs_ctlr_info {
	unsigned char rsvd1;				/* Byte 0 */
	enum {
		MYRS_SCSI_BUS	= 0x00,
		MYRS_Fibre_BUS	= 0x01,
		MYRS_PCI_BUS	= 0x03
	} __packed bus;	/* Byte 1 */
	enum {
		MYRS_CTLR_DAC960E	= 0x01,
		MYRS_CTLR_DAC960M	= 0x08,
		MYRS_CTLR_DAC960PD	= 0x10,
		MYRS_CTLR_DAC960PL	= 0x11,
		MYRS_CTLR_DAC960PU	= 0x12,
		MYRS_CTLR_DAC960PE	= 0x13,
		MYRS_CTLR_DAC960PG	= 0x14,
		MYRS_CTLR_DAC960PJ	= 0x15,
		MYRS_CTLR_DAC960PTL0	= 0x16,
		MYRS_CTLR_DAC960PR	= 0x17,
		MYRS_CTLR_DAC960PRL	= 0x18,
		MYRS_CTLR_DAC960PT	= 0x19,
		MYRS_CTLR_DAC1164P	= 0x1A,
		MYRS_CTLR_DAC960PTL1	= 0x1B,
		MYRS_CTLR_EXR2000P	= 0x1C,
		MYRS_CTLR_EXR3000P	= 0x1D,
		MYRS_CTLR_ACCELERAID352 = 0x1E,
		MYRS_CTLR_ACCELERAID170 = 0x1F,
		MYRS_CTLR_ACCELERAID160 = 0x20,
		MYRS_CTLR_DAC960S	= 0x60,
		MYRS_CTLR_DAC960SU	= 0x61,
		MYRS_CTLR_DAC960SX	= 0x62,
		MYRS_CTLR_DAC960SF	= 0x63,
		MYRS_CTLR_DAC960SS	= 0x64,
		MYRS_CTLR_DAC960FL	= 0x65,
		MYRS_CTLR_DAC960LL	= 0x66,
		MYRS_CTLR_DAC960FF	= 0x67,
		MYRS_CTLR_DAC960HP	= 0x68,
		MYRS_CTLR_RAIDBRICK	= 0x69,
		MYRS_CTLR_METEOR_FL	= 0x6A,
		MYRS_CTLR_METEOR_FF	= 0x6B
	} __packed ctlr_type;	/* Byte 2 */
	unsigned char rsvd2;			/* Byte 3 */
	unsigned short bus_speed_mhz;		/* Bytes 4-5 */
	unsigned char bus_width;		/* Byte 6 */
	unsigned char flash_code;		/* Byte 7 */
	unsigned char ports_present;		/* Byte 8 */
	unsigned char rsvd3[7];			/* Bytes 9-15 */
	unsigned char bus_name[16];		/* Bytes 16-31 */
	unsigned char ctlr_name[16];		/* Bytes 32-47 */
	unsigned char rsvd4[16];		/* Bytes 48-63 */
	/* Firmware Release Information */
	unsigned char fw_major_version;		/* Byte 64 */
	unsigned char fw_minor_version;		/* Byte 65 */
	unsigned char fw_turn_number;		/* Byte 66 */
	unsigned char fw_build_number;		/* Byte 67 */
	unsigned char fw_release_day;		/* Byte 68 */
	unsigned char fw_release_month;		/* Byte 69 */
	unsigned char fw_release_year_hi;	/* Byte 70 */
	unsigned char fw_release_year_lo;	/* Byte 71 */
	/* Hardware Release Information */
	unsigned char hw_rev;			/* Byte 72 */
	unsigned char rsvd5[3];			/* Bytes 73-75 */
	unsigned char hw_release_day;		/* Byte 76 */
	unsigned char hw_release_month;		/* Byte 77 */
	unsigned char hw_release_year_hi;	/* Byte 78 */
	unsigned char hw_release_year_lo;	/* Byte 79 */
	/* Hardware Manufacturing Information */
	unsigned char manuf_batch_num;		/* Byte 80 */
	unsigned char rsvd6;			/* Byte 81 */
	unsigned char manuf_plant_num;		/* Byte 82 */
	unsigned char rsvd7;			/* Byte 83 */
	unsigned char hw_manuf_day;		/* Byte 84 */
	unsigned char hw_manuf_month;		/* Byte 85 */
	unsigned char hw_manuf_year_hi;		/* Byte 86 */
	unsigned char hw_manuf_year_lo;		/* Byte 87 */
	unsigned char max_pd_per_xld;		/* Byte 88 */
	unsigned char max_ild_per_xld;		/* Byte 89 */
	unsigned short nvram_size_kb;		/* Bytes 90-91 */
	unsigned char max_xld;			/* Byte 92 */
	unsigned char rsvd8[3];			/* Bytes 93-95 */
	/* Unique Information per Controller */
	unsigned char serial_number[16];	/* Bytes 96-111 */
	unsigned char rsvd9[16];		/* Bytes 112-127 */
	/* Vendor Information */
	unsigned char rsvd10[3];		/* Bytes 128-130 */
	unsigned char oem_code;			/* Byte 131 */
	unsigned char vendor[16];		/* Bytes 132-147 */
	/* Other Physical/Controller/Operation Information */
	unsigned char bbu_present:1;		/* Byte 148 Bit 0 */
	unsigned char cluster_mode:1;		/* Byte 148 Bit 1 */
	unsigned char rsvd11:6;			/* Byte 148 Bits 2-7 */
	unsigned char rsvd12[3];		/* Bytes 149-151 */
	/* Physical Device Scan Information */
	unsigned char pscan_active:1;		/* Byte 152 Bit 0 */
	unsigned char rsvd13:7;			/* Byte 152 Bits 1-7 */
	unsigned char pscan_chan;		/* Byte 153 */
	unsigned char pscan_target;		/* Byte 154 */
	unsigned char pscan_lun;		/* Byte 155 */
	/* Maximum Command Data Transfer Sizes */
	unsigned short max_transfer_size;	/* Bytes 156-157 */
	unsigned short max_sge;			/* Bytes 158-159 */
	/* Logical/Physical Device Counts */
	unsigned short ldev_present;		/* Bytes 160-161 */
	unsigned short ldev_critical;		/* Bytes 162-163 */
	unsigned short ldev_offline;		/* Bytes 164-165 */
	unsigned short pdev_present;		/* Bytes 166-167 */
	unsigned short pdisk_present;		/* Bytes 168-169 */
	unsigned short pdisk_critical;		/* Bytes 170-171 */
	unsigned short pdisk_offline;		/* Bytes 172-173 */
	unsigned short max_tcq;			/* Bytes 174-175 */
	/* Channel and Target ID Information */
	unsigned char physchan_present;		/* Byte 176 */
	unsigned char virtchan_present;		/* Byte 177 */
	unsigned char physchan_max;		/* Byte 178 */
	unsigned char virtchan_max;		/* Byte 179 */
	unsigned char max_targets[16];		/* Bytes 180-195 */
	unsigned char rsvd14[12];		/* Bytes 196-207 */
	/* Memory/Cache Information */
	unsigned short mem_size_mb;		/* Bytes 208-209 */
	unsigned short cache_size_mb;		/* Bytes 210-211 */
	unsigned int valid_cache_bytes;		/* Bytes 212-215 */
	unsigned int dirty_cache_bytes;		/* Bytes 216-219 */
	unsigned short mem_speed_mhz;		/* Bytes 220-221 */
	unsigned char mem_data_width;		/* Byte 222 */
	struct myrs_mem_type mem_type;		/* Byte 223 */
	unsigned char cache_mem_type_name[16];	/* Bytes 224-239 */
	/* Execution Memory Information */
	unsigned short exec_mem_size_mb;	/* Bytes 240-241 */
	unsigned short exec_l2_cache_size_mb;	/* Bytes 242-243 */
	unsigned char rsvd15[8];		/* Bytes 244-251 */
	unsigned short exec_mem_speed_mhz;	/* Bytes 252-253 */
	unsigned char exec_mem_data_width;	/* Byte 254 */
	struct myrs_mem_type exec_mem_type;	/* Byte 255 */
	unsigned char exec_mem_type_name[16];	/* Bytes 256-271 */
	/* CPU Type Information */
	struct {				/* Bytes 272-335 */
		unsigned short cpu_speed_mhz;
		enum myrs_cpu_type cpu_type;
		unsigned char cpu_count;
		unsigned char rsvd16[12];
		unsigned char cpu_name[16];
	} __packed cpu[2];
	/* Debugging/Profiling/Command Time Tracing Information */
	unsigned short cur_prof_page_num;	/* Bytes 336-337 */
	unsigned short num_prof_waiters;	/* Bytes 338-339 */
	unsigned short cur_trace_page_num;	/* Bytes 340-341 */
	unsigned short num_trace_waiters;	/* Bytes 342-343 */
	unsigned char rsvd18[8];		/* Bytes 344-351 */
	/* Error Counters on Physical Devices */
	unsigned short pdev_bus_resets;		/* Bytes 352-353 */
	unsigned short pdev_parity_errors;	/* Bytes 355-355 */
	unsigned short pdev_soft_errors;	/* Bytes 356-357 */
	unsigned short pdev_cmds_failed;	/* Bytes 358-359 */
	unsigned short pdev_misc_errors;	/* Bytes 360-361 */
	unsigned short pdev_cmd_timeouts;	/* Bytes 362-363 */
	unsigned short pdev_sel_timeouts;	/* Bytes 364-365 */
	unsigned short pdev_retries_done;	/* Bytes 366-367 */
	unsigned short pdev_aborts_done;	/* Bytes 368-369 */
	unsigned short pdev_host_aborts_done;	/* Bytes 370-371 */
	unsigned short pdev_predicted_failures;	/* Bytes 372-373 */
	unsigned short pdev_host_cmds_failed;	/* Bytes 374-375 */
	unsigned short pdev_hard_errors;	/* Bytes 376-377 */
	unsigned char rsvd19[6];		/* Bytes 378-383 */
	/* Error Counters on Logical Devices */
	unsigned short ldev_soft_errors;	/* Bytes 384-385 */
	unsigned short ldev_cmds_failed;	/* Bytes 386-387 */
	unsigned short ldev_host_aborts_done;	/* Bytes 388-389 */
	unsigned char rsvd20[2];		/* Bytes 390-391 */
	/* Error Counters on Controller */
	unsigned short ctlr_mem_errors;		/* Bytes 392-393 */
	unsigned short ctlr_host_aborts_done;	/* Bytes 394-395 */
	unsigned char rsvd21[4];		/* Bytes 396-399 */
	/* Long Duration Activity Information */
	unsigned short bg_init_active;		/* Bytes 400-401 */
	unsigned short ldev_init_active;	/* Bytes 402-403 */
	unsigned short pdev_init_active;	/* Bytes 404-405 */
	unsigned short cc_active;		/* Bytes 406-407 */
	unsigned short rbld_active;		/* Bytes 408-409 */
	unsigned short exp_active;		/* Bytes 410-411 */
	unsigned short patrol_active;		/* Bytes 412-413 */
	unsigned char rsvd22[2];		/* Bytes 414-415 */
	/* Flash ROM Information */
	unsigned char flash_type;		/* Byte 416 */
	unsigned char rsvd23;			/* Byte 417 */
	unsigned short flash_size_MB;		/* Bytes 418-419 */
	unsigned int flash_limit;		/* Bytes 420-423 */
	unsigned int flash_count;		/* Bytes 424-427 */
	unsigned char rsvd24[4];		/* Bytes 428-431 */
	unsigned char flash_type_name[16];	/* Bytes 432-447 */
	/* Firmware Run Time Information */
	unsigned char rbld_rate;		/* Byte 448 */
	unsigned char bg_init_rate;		/* Byte 449 */
	unsigned char fg_init_rate;		/* Byte 450 */
	unsigned char cc_rate;			/* Byte 451 */
	unsigned char rsvd25[4];		/* Bytes 452-455 */
	unsigned int max_dp;			/* Bytes 456-459 */
	unsigned int free_dp;			/* Bytes 460-463 */
	unsigned int max_iop;			/* Bytes 464-467 */
	unsigned int free_iop;			/* Bytes 468-471 */
	unsigned short max_combined_len;	/* Bytes 472-473 */
	unsigned short num_cfg_groups;		/* Bytes 474-475 */
	unsigned installation_abort_status:1;	/* Byte 476 Bit 0 */
	unsigned maint_mode_status:1;		/* Byte 476 Bit 1 */
	unsigned rsvd26:6;			/* Byte 476 Bits 2-7 */
	unsigned char rsvd27[6];		/* Bytes 477-511 */
	unsigned char rsvd28[512];		/* Bytes 512-1023 */
};

/*
 * DAC960 V2 Firmware Device State type.
 */
enum myrs_devstate {
	MYRS_DEVICE_UNCONFIGURED	= 0x00,
	MYRS_DEVICE_ONLINE		= 0x01,
	MYRS_DEVICE_REBUILD		= 0x03,
	MYRS_DEVICE_MISSING		= 0x04,
	MYRS_DEVICE_SUSPECTED_CRITICAL	= 0x05,
	MYRS_DEVICE_OFFLINE		= 0x08,
	MYRS_DEVICE_CRITICAL		= 0x09,
	MYRS_DEVICE_SUSPECTED_DEAD	= 0x0C,
	MYRS_DEVICE_COMMANDED_OFFLINE	= 0x10,
	MYRS_DEVICE_STANDBY		= 0x21,
	MYRS_DEVICE_INVALID_STATE	= 0xFF,
} __packed;

/*
 * DAC960 V2 RAID Levels
 */
enum myrs_raid_level {
	MYRS_RAID_LEVEL0	= 0x0,     /* RAID 0 */
	MYRS_RAID_LEVEL1	= 0x1,     /* RAID 1 */
	MYRS_RAID_LEVEL3	= 0x3,     /* RAID 3 right asymmetric parity */
	MYRS_RAID_LEVEL5	= 0x5,     /* RAID 5 right asymmetric parity */
	MYRS_RAID_LEVEL6	= 0x6,     /* RAID 6 (Mylex RAID 6) */
	MYRS_RAID_JBOD		= 0x7,     /* RAID 7 (JBOD) */
	MYRS_RAID_NEWSPAN	= 0x8,     /* New Mylex SPAN */
	MYRS_RAID_LEVEL3F	= 0x9,     /* RAID 3 fixed parity */
	MYRS_RAID_LEVEL3L	= 0xb,     /* RAID 3 left symmetric parity */
	MYRS_RAID_SPAN		= 0xc,     /* current spanning implementation */
	MYRS_RAID_LEVEL5L	= 0xd,     /* RAID 5 left symmetric parity */
	MYRS_RAID_LEVELE	= 0xe,     /* RAID E (concatenation) */
	MYRS_RAID_PHYSICAL	= 0xf,     /* physical device */
} __packed;

enum myrs_stripe_size {
	MYRS_STRIPE_SIZE_0	= 0x0,	/* no stripe (RAID 1, RAID 7, etc) */
	MYRS_STRIPE_SIZE_512B	= 0x1,
	MYRS_STRIPE_SIZE_1K	= 0x2,
	MYRS_STRIPE_SIZE_2K	= 0x3,
	MYRS_STRIPE_SIZE_4K	= 0x4,
	MYRS_STRIPE_SIZE_8K	= 0x5,
	MYRS_STRIPE_SIZE_16K	= 0x6,
	MYRS_STRIPE_SIZE_32K	= 0x7,
	MYRS_STRIPE_SIZE_64K	= 0x8,
	MYRS_STRIPE_SIZE_128K	= 0x9,
	MYRS_STRIPE_SIZE_256K	= 0xa,
	MYRS_STRIPE_SIZE_512K	= 0xb,
	MYRS_STRIPE_SIZE_1M	= 0xc,
} __packed;

enum myrs_cacheline_size {
	MYRS_CACHELINE_ZERO	= 0x0,	/* caching cannot be enabled */
	MYRS_CACHELINE_512B	= 0x1,
	MYRS_CACHELINE_1K	= 0x2,
	MYRS_CACHELINE_2K	= 0x3,
	MYRS_CACHELINE_4K	= 0x4,
	MYRS_CACHELINE_8K	= 0x5,
	MYRS_CACHELINE_16K	= 0x6,
	MYRS_CACHELINE_32K	= 0x7,
	MYRS_CACHELINE_64K	= 0x8,
} __packed;

/*
 * DAC960 V2 Firmware Get Logical Device Info reply structure.
 */
struct myrs_ldev_info {
	unsigned char ctlr;			/* Byte 0 */
	unsigned char channel;			/* Byte 1 */
	unsigned char target;			/* Byte 2 */
	unsigned char lun;			/* Byte 3 */
	enum myrs_devstate dev_state;		/* Byte 4 */
	unsigned char raid_level;		/* Byte 5 */
	enum myrs_stripe_size stripe_size;	/* Byte 6 */
	enum myrs_cacheline_size cacheline_size; /* Byte 7 */
	struct {
		enum {
			MYRS_READCACHE_DISABLED		= 0x0,
			MYRS_READCACHE_ENABLED		= 0x1,
			MYRS_READAHEAD_ENABLED		= 0x2,
			MYRS_INTELLIGENT_READAHEAD_ENABLED = 0x3,
			MYRS_READCACHE_LAST		= 0x7,
		} __packed rce:3; /* Byte 8 Bits 0-2 */
		enum {
			MYRS_WRITECACHE_DISABLED	= 0x0,
			MYRS_LOGICALDEVICE_RO		= 0x1,
			MYRS_WRITECACHE_ENABLED		= 0x2,
			MYRS_INTELLIGENT_WRITECACHE_ENABLED = 0x3,
			MYRS_WRITECACHE_LAST		= 0x7,
		} __packed wce:3; /* Byte 8 Bits 3-5 */
		unsigned rsvd1:1;		/* Byte 8 Bit 6 */
		unsigned ldev_init_done:1;	/* Byte 8 Bit 7 */
	} ldev_control;				/* Byte 8 */
	/* Logical Device Operations Status */
	unsigned char cc_active:1;		/* Byte 9 Bit 0 */
	unsigned char rbld_active:1;		/* Byte 9 Bit 1 */
	unsigned char bg_init_active:1;		/* Byte 9 Bit 2 */
	unsigned char fg_init_active:1;		/* Byte 9 Bit 3 */
	unsigned char migration_active:1;	/* Byte 9 Bit 4 */
	unsigned char patrol_active:1;		/* Byte 9 Bit 5 */
	unsigned char rsvd2:2;			/* Byte 9 Bits 6-7 */
	unsigned char raid5_writeupdate;	/* Byte 10 */
	unsigned char raid5_algo;		/* Byte 11 */
	unsigned short ldev_num;		/* Bytes 12-13 */
	/* BIOS Info */
	unsigned char bios_disabled:1;		/* Byte 14 Bit 0 */
	unsigned char cdrom_boot:1;		/* Byte 14 Bit 1 */
	unsigned char drv_coercion:1;		/* Byte 14 Bit 2 */
	unsigned char write_same_disabled:1;	/* Byte 14 Bit 3 */
	unsigned char hba_mode:1;		/* Byte 14 Bit 4 */
	enum {
		MYRS_GEOMETRY_128_32	= 0x0,
		MYRS_GEOMETRY_255_63	= 0x1,
		MYRS_GEOMETRY_RSVD1	= 0x2,
		MYRS_GEOMETRY_RSVD2	= 0x3
	} __packed drv_geom:2;	/* Byte 14 Bits 5-6 */
	unsigned char super_ra_enabled:1;	/* Byte 14 Bit 7 */
	unsigned char rsvd3;			/* Byte 15 */
	/* Error Counters */
	unsigned short soft_errs;		/* Bytes 16-17 */
	unsigned short cmds_failed;		/* Bytes 18-19 */
	unsigned short cmds_aborted;		/* Bytes 20-21 */
	unsigned short deferred_write_errs;	/* Bytes 22-23 */
	unsigned int rsvd4;			/* Bytes 24-27 */
	unsigned int rsvd5;			/* Bytes 28-31 */
	/* Device Size Information */
	unsigned short rsvd6;			/* Bytes 32-33 */
	unsigned short devsize_bytes;		/* Bytes 34-35 */
	unsigned int orig_devsize;		/* Bytes 36-39 */
	unsigned int cfg_devsize;		/* Bytes 40-43 */
	unsigned int rsvd7;			/* Bytes 44-47 */
	unsigned char ldev_name[32];		/* Bytes 48-79 */
	unsigned char inquiry[36];		/* Bytes 80-115 */
	unsigned char rsvd8[12];		/* Bytes 116-127 */
	u64 last_read_lba;			/* Bytes 128-135 */
	u64 last_write_lba;			/* Bytes 136-143 */
	u64 cc_lba;				/* Bytes 144-151 */
	u64 rbld_lba;				/* Bytes 152-159 */
	u64 bg_init_lba;			/* Bytes 160-167 */
	u64 fg_init_lba;			/* Bytes 168-175 */
	u64 migration_lba;			/* Bytes 176-183 */
	u64 patrol_lba;				/* Bytes 184-191 */
	unsigned char rsvd9[64];		/* Bytes 192-255 */
};

/*
 * DAC960 V2 Firmware Get Physical Device Info reply structure.
 */
struct myrs_pdev_info {
	unsigned char rsvd1;			/* Byte 0 */
	unsigned char channel;			/* Byte 1 */
	unsigned char target;			/* Byte 2 */
	unsigned char lun;			/* Byte 3 */
	/* Configuration Status Bits */
	unsigned char pdev_fault_tolerant:1;	/* Byte 4 Bit 0 */
	unsigned char pdev_connected:1;		/* Byte 4 Bit 1 */
	unsigned char pdev_local_to_ctlr:1;	/* Byte 4 Bit 2 */
	unsigned char rsvd2:5;			/* Byte 4 Bits 3-7 */
	/* Multiple Host/Controller Status Bits */
	unsigned char remote_host_dead:1;	/* Byte 5 Bit 0 */
	unsigned char remove_ctlr_dead:1;	/* Byte 5 Bit 1 */
	unsigned char rsvd3:6;			/* Byte 5 Bits 2-7 */
	enum myrs_devstate dev_state;		/* Byte 6 */
	unsigned char nego_data_width;		/* Byte 7 */
	unsigned short nego_sync_rate;		/* Bytes 8-9 */
	/* Multiported Physical Device Information */
	unsigned char num_ports;		/* Byte 10 */
	unsigned char drv_access_bitmap;	/* Byte 11 */
	unsigned int rsvd4;			/* Bytes 12-15 */
	unsigned char ip_address[16];		/* Bytes 16-31 */
	unsigned short max_tags;		/* Bytes 32-33 */
	/* Physical Device Operations Status */
	unsigned char cc_in_progress:1;		/* Byte 34 Bit 0 */
	unsigned char rbld_in_progress:1;	/* Byte 34 Bit 1 */
	unsigned char makecc_in_progress:1;	/* Byte 34 Bit 2 */
	unsigned char pdevinit_in_progress:1;	/* Byte 34 Bit 3 */
	unsigned char migration_in_progress:1;	/* Byte 34 Bit 4 */
	unsigned char patrol_in_progress:1;	/* Byte 34 Bit 5 */
	unsigned char rsvd5:2;			/* Byte 34 Bits 6-7 */
	unsigned char long_op_status;		/* Byte 35 */
	unsigned char parity_errs;		/* Byte 36 */
	unsigned char soft_errs;		/* Byte 37 */
	unsigned char hard_errs;		/* Byte 38 */
	unsigned char misc_errs;		/* Byte 39 */
	unsigned char cmd_timeouts;		/* Byte 40 */
	unsigned char retries;			/* Byte 41 */
	unsigned char aborts;			/* Byte 42 */
	unsigned char pred_failures;		/* Byte 43 */
	unsigned int rsvd6;			/* Bytes 44-47 */
	unsigned short rsvd7;			/* Bytes 48-49 */
	unsigned short devsize_bytes;		/* Bytes 50-51 */
	unsigned int orig_devsize;		/* Bytes 52-55 */
	unsigned int cfg_devsize;		/* Bytes 56-59 */
	unsigned int rsvd8;			/* Bytes 60-63 */
	unsigned char pdev_name[16];		/* Bytes 64-79 */
	unsigned char rsvd9[16];		/* Bytes 80-95 */
	unsigned char rsvd10[32];		/* Bytes 96-127 */
	unsigned char inquiry[36];		/* Bytes 128-163 */
	unsigned char rsvd11[20];		/* Bytes 164-183 */
	unsigned char rsvd12[8];		/* Bytes 184-191 */
	u64 last_read_lba;			/* Bytes 192-199 */
	u64 last_write_lba;			/* Bytes 200-207 */
	u64 cc_lba;				/* Bytes 208-215 */
	u64 rbld_lba;				/* Bytes 216-223 */
	u64 makecc_lba;				/* Bytes 224-231 */
	u64 devinit_lba;			/* Bytes 232-239 */
	u64 migration_lba;			/* Bytes 240-247 */
	u64 patrol_lba;				/* Bytes 248-255 */
	unsigned char rsvd13[256];		/* Bytes 256-511 */
};

/*
 * DAC960 V2 Firmware Health Status Buffer structure.
 */
struct myrs_fwstat {
	unsigned int uptime_usecs;		/* Bytes 0-3 */
	unsigned int uptime_msecs;		/* Bytes 4-7 */
	unsigned int seconds;			/* Bytes 8-11 */
	unsigned char rsvd1[4];			/* Bytes 12-15 */
	unsigned int epoch;			/* Bytes 16-19 */
	unsigned char rsvd2[4];			/* Bytes 20-23 */
	unsigned int dbg_msgbuf_idx;		/* Bytes 24-27 */
	unsigned int coded_msgbuf_idx;		/* Bytes 28-31 */
	unsigned int cur_timetrace_page;	/* Bytes 32-35 */
	unsigned int cur_prof_page;		/* Bytes 36-39 */
	unsigned int next_evseq;		/* Bytes 40-43 */
	unsigned char rsvd3[4];			/* Bytes 44-47 */
	unsigned char rsvd4[16];		/* Bytes 48-63 */
	unsigned char rsvd5[64];		/* Bytes 64-127 */
};

/*
 * DAC960 V2 Firmware Get Event reply structure.
 */
struct myrs_event {
	unsigned int ev_seq;			/* Bytes 0-3 */
	unsigned int ev_time;			/* Bytes 4-7 */
	unsigned int ev_code;			/* Bytes 8-11 */
	unsigned char rsvd1;			/* Byte 12 */
	unsigned char channel;			/* Byte 13 */
	unsigned char target;			/* Byte 14 */
	unsigned char lun;			/* Byte 15 */
	unsigned int rsvd2;			/* Bytes 16-19 */
	unsigned int ev_parm;			/* Bytes 20-23 */
	unsigned char sense_data[40];		/* Bytes 24-63 */
};

/*
 * DAC960 V2 Firmware Command Control Bits structure.
 */
struct myrs_cmd_ctrl {
	unsigned char fua:1;			/* Byte 0 Bit 0 */
	unsigned char disable_pgout:1;		/* Byte 0 Bit 1 */
	unsigned char rsvd1:1;			/* Byte 0 Bit 2 */
	unsigned char add_sge_mem:1;		/* Byte 0 Bit 3 */
	unsigned char dma_ctrl_to_host:1;	/* Byte 0 Bit 4 */
	unsigned char rsvd2:1;			/* Byte 0 Bit 5 */
	unsigned char no_autosense:1;		/* Byte 0 Bit 6 */
	unsigned char disc_prohibited:1;	/* Byte 0 Bit 7 */
};

/*
 * DAC960 V2 Firmware Command Timeout structure.
 */
struct myrs_cmd_tmo {
	unsigned char tmo_val:6;			/* Byte 0 Bits 0-5 */
	enum {
		MYRS_TMO_SCALE_SECONDS	= 0,
		MYRS_TMO_SCALE_MINUTES	= 1,
		MYRS_TMO_SCALE_HOURS	= 2,
		MYRS_TMO_SCALE_RESERVED = 3
	} __packed tmo_scale:2;		/* Byte 0 Bits 6-7 */
};

/*
 * DAC960 V2 Firmware Physical Device structure.
 */
struct myrs_pdev {
	unsigned char lun;			/* Byte 0 */
	unsigned char target;			/* Byte 1 */
	unsigned char channel:3;		/* Byte 2 Bits 0-2 */
	unsigned char ctlr:5;			/* Byte 2 Bits 3-7 */
} __packed;

/*
 * DAC960 V2 Firmware Logical Device structure.
 */
struct myrs_ldev {
	unsigned short ldev_num;		/* Bytes 0-1 */
	unsigned char rsvd:3;			/* Byte 2 Bits 0-2 */
	unsigned char ctlr:5;			/* Byte 2 Bits 3-7 */
} __packed;

/*
 * DAC960 V2 Firmware Operation Device type.
 */
enum myrs_opdev {
	MYRS_PHYSICAL_DEVICE	= 0x00,
	MYRS_RAID_DEVICE	= 0x01,
	MYRS_PHYSICAL_CHANNEL	= 0x02,
	MYRS_RAID_CHANNEL	= 0x03,
	MYRS_PHYSICAL_CONTROLLER = 0x04,
	MYRS_RAID_CONTROLLER	= 0x05,
	MYRS_CONFIGURATION_GROUP = 0x10,
	MYRS_ENCLOSURE		= 0x11,
} __packed;

/*
 * DAC960 V2 Firmware Translate Physical To Logical Device structure.
 */
struct myrs_devmap {
	unsigned short ldev_num;		/* Bytes 0-1 */
	unsigned short rsvd;			/* Bytes 2-3 */
	unsigned char prev_boot_ctlr;		/* Byte 4 */
	unsigned char prev_boot_channel;	/* Byte 5 */
	unsigned char prev_boot_target;		/* Byte 6 */
	unsigned char prev_boot_lun;		/* Byte 7 */
};

/*
 * DAC960 V2 Firmware Scatter/Gather List Entry structure.
 */
struct myrs_sge {
	u64 sge_addr;			/* Bytes 0-7 */
	u64 sge_count;			/* Bytes 8-15 */
};

/*
 * DAC960 V2 Firmware Data Transfer Memory Address structure.
 */
union myrs_sgl {
	struct myrs_sge sge[2]; /* Bytes 0-31 */
	struct {
		unsigned short sge0_len;	/* Bytes 0-1 */
		unsigned short sge1_len;	/* Bytes 2-3 */
		unsigned short sge2_len;	/* Bytes 4-5 */
		unsigned short rsvd;		/* Bytes 6-7 */
		u64 sge0_addr;			/* Bytes 8-15 */
		u64 sge1_addr;			/* Bytes 16-23 */
		u64 sge2_addr;			/* Bytes 24-31 */
	} ext;
};

/*
 * 64 Byte DAC960 V2 Firmware Command Mailbox structure.
 */
union myrs_cmd_mbox {
	unsigned int words[16];				/* Words 0-15 */
	struct {
		unsigned short id;			/* Bytes 0-1 */
		enum myrs_cmd_opcode opcode;		/* Byte 2 */
		struct myrs_cmd_ctrl control;		/* Byte 3 */
		u32 dma_size:24;			/* Bytes 4-6 */
		unsigned char dma_num;			/* Byte 7 */
		u64 sense_addr;				/* Bytes 8-15 */
		unsigned int rsvd1:24;			/* Bytes 16-18 */
		struct myrs_cmd_tmo tmo;		/* Byte 19 */
		unsigned char sense_len;		/* Byte 20 */
		enum myrs_ioctl_opcode ioctl_opcode;	/* Byte 21 */
		unsigned char rsvd2[10];		/* Bytes 22-31 */
		union myrs_sgl dma_addr;		/* Bytes 32-63 */
	} common;
	struct {
		unsigned short id;			/* Bytes 0-1 */
		enum myrs_cmd_opcode opcode;		/* Byte 2 */
		struct myrs_cmd_ctrl control;		/* Byte 3 */
		u32 dma_size;				/* Bytes 4-7 */
		u64 sense_addr;				/* Bytes 8-15 */
		struct myrs_pdev pdev;			/* Bytes 16-18 */
		struct myrs_cmd_tmo tmo;		/* Byte 19 */
		unsigned char sense_len;		/* Byte 20 */
		unsigned char cdb_len;			/* Byte 21 */
		unsigned char cdb[10];			/* Bytes 22-31 */
		union myrs_sgl dma_addr;		/* Bytes 32-63 */
	} SCSI_10;
	struct {
		unsigned short id;			/* Bytes 0-1 */
		enum myrs_cmd_opcode opcode;		/* Byte 2 */
		struct myrs_cmd_ctrl control;		/* Byte 3 */
		u32 dma_size;				/* Bytes 4-7 */
		u64 sense_addr;				/* Bytes 8-15 */
		struct myrs_pdev pdev;			/* Bytes 16-18 */
		struct myrs_cmd_tmo tmo;		/* Byte 19 */
		unsigned char sense_len;		/* Byte 20 */
		unsigned char cdb_len;			/* Byte 21 */
		unsigned short rsvd;			/* Bytes 22-23 */
		u64 cdb_addr;				/* Bytes 24-31 */
		union myrs_sgl dma_addr;		/* Bytes 32-63 */
	} SCSI_255;
	struct {
		unsigned short id;			/* Bytes 0-1 */
		enum myrs_cmd_opcode opcode;		/* Byte 2 */
		struct myrs_cmd_ctrl control;		/* Byte 3 */
		u32 dma_size:24;			/* Bytes 4-6 */
		unsigned char dma_num;			/* Byte 7 */
		u64 sense_addr;				/* Bytes 8-15 */
		unsigned short rsvd1;			/* Bytes 16-17 */
		unsigned char ctlr_num;			/* Byte 18 */
		struct myrs_cmd_tmo tmo;		/* Byte 19 */
		unsigned char sense_len;		/* Byte 20 */
		enum myrs_ioctl_opcode ioctl_opcode;	/* Byte 21 */
		unsigned char rsvd2[10];		/* Bytes 22-31 */
		union myrs_sgl dma_addr;		/* Bytes 32-63 */
	} ctlr_info;
	struct {
		unsigned short id;			/* Bytes 0-1 */
		enum myrs_cmd_opcode opcode;		/* Byte 2 */
		struct myrs_cmd_ctrl control;		/* Byte 3 */
		u32 dma_size:24;			/* Bytes 4-6 */
		unsigned char dma_num;			/* Byte 7 */
		u64 sense_addr;				/* Bytes 8-15 */
		struct myrs_ldev ldev;			/* Bytes 16-18 */
		struct myrs_cmd_tmo tmo;		/* Byte 19 */
		unsigned char sense_len;		/* Byte 20 */
		enum myrs_ioctl_opcode ioctl_opcode;	/* Byte 21 */
		unsigned char rsvd[10];			/* Bytes 22-31 */
		union myrs_sgl dma_addr;		/* Bytes 32-63 */
	} ldev_info;
	struct {
		unsigned short id;			/* Bytes 0-1 */
		enum myrs_cmd_opcode opcode;		/* Byte 2 */
		struct myrs_cmd_ctrl control;		/* Byte 3 */
		u32 dma_size:24;			/* Bytes 4-6 */
		unsigned char dma_num;			/* Byte 7 */
		u64 sense_addr;				/* Bytes 8-15 */
		struct myrs_pdev pdev;			/* Bytes 16-18 */
		struct myrs_cmd_tmo tmo;		/* Byte 19 */
		unsigned char sense_len;		/* Byte 20 */
		enum myrs_ioctl_opcode ioctl_opcode;	/* Byte 21 */
		unsigned char rsvd[10];			/* Bytes 22-31 */
		union myrs_sgl dma_addr;		/* Bytes 32-63 */
	} pdev_info;
	struct {
		unsigned short id;			/* Bytes 0-1 */
		enum myrs_cmd_opcode opcode;		/* Byte 2 */
		struct myrs_cmd_ctrl control;		/* Byte 3 */
		u32 dma_size:24;			/* Bytes 4-6 */
		unsigned char dma_num;			/* Byte 7 */
		u64 sense_addr;				/* Bytes 8-15 */
		unsigned short evnum_upper;		/* Bytes 16-17 */
		unsigned char ctlr_num;			/* Byte 18 */
		struct myrs_cmd_tmo tmo;		/* Byte 19 */
		unsigned char sense_len;		/* Byte 20 */
		enum myrs_ioctl_opcode ioctl_opcode;	/* Byte 21 */
		unsigned short evnum_lower;		/* Bytes 22-23 */
		unsigned char rsvd[8];			/* Bytes 24-31 */
		union myrs_sgl dma_addr;		/* Bytes 32-63 */
	} get_event;
	struct {
		unsigned short id;			/* Bytes 0-1 */
		enum myrs_cmd_opcode opcode;		/* Byte 2 */
		struct myrs_cmd_ctrl control;		/* Byte 3 */
		u32 dma_size:24;			/* Bytes 4-6 */
		unsigned char dma_num;			/* Byte 7 */
		u64 sense_addr;				/* Bytes 8-15 */
		union {
			struct myrs_ldev ldev;		/* Bytes 16-18 */
			struct myrs_pdev pdev;		/* Bytes 16-18 */
		};
		struct myrs_cmd_tmo tmo;		/* Byte 19 */
		unsigned char sense_len;		/* Byte 20 */
		enum myrs_ioctl_opcode ioctl_opcode;	/* Byte 21 */
		enum myrs_devstate state;		/* Byte 22 */
		unsigned char rsvd[9];			/* Bytes 23-31 */
		union myrs_sgl dma_addr;		/* Bytes 32-63 */
	} set_devstate;
	struct {
		unsigned short id;			/* Bytes 0-1 */
		enum myrs_cmd_opcode opcode;		/* Byte 2 */
		struct myrs_cmd_ctrl control;		/* Byte 3 */
		u32 dma_size:24;			/* Bytes 4-6 */
		unsigned char dma_num;			/* Byte 7 */
		u64 sense_addr;				/* Bytes 8-15 */
		struct myrs_ldev ldev;			/* Bytes 16-18 */
		struct myrs_cmd_tmo tmo;		/* Byte 19 */
		unsigned char sense_len;		/* Byte 20 */
		enum myrs_ioctl_opcode ioctl_opcode;	/* Byte 21 */
		unsigned char restore_consistency:1;	/* Byte 22 Bit 0 */
		unsigned char initialized_area_only:1;	/* Byte 22 Bit 1 */
		unsigned char rsvd1:6;			/* Byte 22 Bits 2-7 */
		unsigned char rsvd2[9];			/* Bytes 23-31 */
		union myrs_sgl dma_addr;		/* Bytes 32-63 */
	} cc;
	struct {
		unsigned short id;			/* Bytes 0-1 */
		enum myrs_cmd_opcode opcode;		/* Byte 2 */
		struct myrs_cmd_ctrl control;		/* Byte 3 */
		unsigned char first_cmd_mbox_size_kb;	/* Byte 4 */
		unsigned char first_stat_mbox_size_kb;	/* Byte 5 */
		unsigned char second_cmd_mbox_size_kb;	/* Byte 6 */
		unsigned char second_stat_mbox_size_kb;	/* Byte 7 */
		u64 sense_addr;				/* Bytes 8-15 */
		unsigned int rsvd1:24;			/* Bytes 16-18 */
		struct myrs_cmd_tmo tmo;		/* Byte 19 */
		unsigned char sense_len;		/* Byte 20 */
		enum myrs_ioctl_opcode ioctl_opcode;	/* Byte 21 */
		unsigned char fwstat_buf_size_kb;	/* Byte 22 */
		unsigned char rsvd2;			/* Byte 23 */
		u64 fwstat_buf_addr;			/* Bytes 24-31 */
		u64 first_cmd_mbox_addr;		/* Bytes 32-39 */
		u64 first_stat_mbox_addr;		/* Bytes 40-47 */
		u64 second_cmd_mbox_addr;		/* Bytes 48-55 */
		u64 second_stat_mbox_addr;		/* Bytes 56-63 */
	} set_mbox;
	struct {
		unsigned short id;			/* Bytes 0-1 */
		enum myrs_cmd_opcode opcode;		/* Byte 2 */
		struct myrs_cmd_ctrl control;		/* Byte 3 */
		u32 dma_size:24;			/* Bytes 4-6 */
		unsigned char dma_num;			/* Byte 7 */
		u64 sense_addr;				/* Bytes 8-15 */
		struct myrs_pdev pdev;			/* Bytes 16-18 */
		struct myrs_cmd_tmo tmo;		/* Byte 19 */
		unsigned char sense_len;		/* Byte 20 */
		enum myrs_ioctl_opcode ioctl_opcode;	/* Byte 21 */
		enum myrs_opdev opdev;			/* Byte 22 */
		unsigned char rsvd[9];			/* Bytes 23-31 */
		union myrs_sgl dma_addr;		/* Bytes 32-63 */
	} dev_op;
};

/*
 * DAC960 V2 Firmware Controller Status Mailbox structure.
 */
struct myrs_stat_mbox {
	unsigned short id;		/* Bytes 0-1 */
	unsigned char status;		/* Byte 2 */
	unsigned char sense_len;	/* Byte 3 */
	int residual;			/* Bytes 4-7 */
};

struct myrs_cmdblk {
	union myrs_cmd_mbox mbox;
	unsigned char status;
	unsigned char sense_len;
	int residual;
	struct completion *complete;
	struct myrs_sge *sgl;
	dma_addr_t sgl_addr;
	unsigned char *dcdb;
	dma_addr_t dcdb_dma;
	unsigned char *sense;
	dma_addr_t sense_addr;
};

/*
 * DAC960 Driver Controller structure.
 */
struct myrs_hba {
	void __iomem *io_base;
	void __iomem *mmio_base;
	phys_addr_t io_addr;
	phys_addr_t pci_addr;
	unsigned int irq;

	unsigned char model_name[28];
	unsigned char fw_version[12];

	struct Scsi_Host *host;
	struct pci_dev *pdev;

	unsigned int epoch;
	unsigned int next_evseq;
	/* Monitor flags */
	bool needs_update;
	bool disable_enc_msg;

	struct workqueue_struct *work_q;
	char work_q_name[20];
	struct delayed_work monitor_work;
	unsigned long primary_monitor_time;
	unsigned long secondary_monitor_time;

	spinlock_t queue_lock;

	struct dma_pool *sg_pool;
	struct dma_pool *sense_pool;
	struct dma_pool *dcdb_pool;

	void (*write_cmd_mbox)(union myrs_cmd_mbox *next_mbox,
			       union myrs_cmd_mbox *cmd_mbox);
	void (*get_cmd_mbox)(void __iomem *base);
	void (*disable_intr)(void __iomem *base);
	void (*reset)(void __iomem *base);

	dma_addr_t cmd_mbox_addr;
	size_t cmd_mbox_size;
	union myrs_cmd_mbox *first_cmd_mbox;
	union myrs_cmd_mbox *last_cmd_mbox;
	union myrs_cmd_mbox *next_cmd_mbox;
	union myrs_cmd_mbox *prev_cmd_mbox1;
	union myrs_cmd_mbox *prev_cmd_mbox2;

	dma_addr_t stat_mbox_addr;
	size_t stat_mbox_size;
	struct myrs_stat_mbox *first_stat_mbox;
	struct myrs_stat_mbox *last_stat_mbox;
	struct myrs_stat_mbox *next_stat_mbox;

	struct myrs_cmdblk dcmd_blk;
	struct myrs_cmdblk mcmd_blk;
	struct mutex dcmd_mutex;

	struct myrs_fwstat *fwstat_buf;
	dma_addr_t fwstat_addr;

	struct myrs_ctlr_info *ctlr_info;
	struct mutex cinfo_mutex;

	struct myrs_event *event_buf;
};

typedef unsigned char (*enable_mbox_t)(void __iomem *base, dma_addr_t addr);
typedef int (*myrs_hwinit_t)(struct pci_dev *pdev,
			     struct myrs_hba *c, void __iomem *base);

struct myrs_privdata {
	myrs_hwinit_t		hw_init;
	irq_handler_t		irq_handler;
	unsigned int		mmio_size;
};

/*
 * DAC960 GEM Series Controller Interface Register Offsets.
 */

#define DAC960_GEM_mmio_size	0x600

enum DAC960_GEM_reg_offset {
	DAC960_GEM_IDB_READ_OFFSET	= 0x214,
	DAC960_GEM_IDB_CLEAR_OFFSET	= 0x218,
	DAC960_GEM_ODB_READ_OFFSET	= 0x224,
	DAC960_GEM_ODB_CLEAR_OFFSET	= 0x228,
	DAC960_GEM_IRQSTS_OFFSET	= 0x208,
	DAC960_GEM_IRQMASK_READ_OFFSET	= 0x22C,
	DAC960_GEM_IRQMASK_CLEAR_OFFSET	= 0x230,
	DAC960_GEM_CMDMBX_OFFSET	= 0x510,
	DAC960_GEM_CMDSTS_OFFSET	= 0x518,
	DAC960_GEM_ERRSTS_READ_OFFSET	= 0x224,
	DAC960_GEM_ERRSTS_CLEAR_OFFSET	= 0x228,
};

/*
 * DAC960 GEM Series Inbound Door Bell Register.
 */
#define DAC960_GEM_IDB_HWMBOX_NEW_CMD	0x01
#define DAC960_GEM_IDB_HWMBOX_ACK_STS	0x02
#define DAC960_GEM_IDB_GEN_IRQ		0x04
#define DAC960_GEM_IDB_CTRL_RESET	0x08
#define DAC960_GEM_IDB_MMBOX_NEW_CMD	0x10

#define DAC960_GEM_IDB_HWMBOX_FULL	0x01
#define DAC960_GEM_IDB_INIT_IN_PROGRESS	0x02

/*
 * DAC960 GEM Series Outbound Door Bell Register.
 */
#define DAC960_GEM_ODB_HWMBOX_ACK_IRQ	0x01
#define DAC960_GEM_ODB_MMBOX_ACK_IRQ	0x02
#define DAC960_GEM_ODB_HWMBOX_STS_AVAIL 0x01
#define DAC960_GEM_ODB_MMBOX_STS_AVAIL	0x02

/*
 * DAC960 GEM Series Interrupt Mask Register.
 */
#define DAC960_GEM_IRQMASK_HWMBOX_IRQ	0x01
#define DAC960_GEM_IRQMASK_MMBOX_IRQ	0x02

/*
 * DAC960 GEM Series Error Status Register.
 */
#define DAC960_GEM_ERRSTS_PENDING	0x20

/*
 * dma_addr_writeql is provided to write dma_addr_t types
 * to a 64-bit pci address space register.  The controller
 * will accept having the register written as two 32-bit
 * values.
 *
 * In HIGHMEM kernels, dma_addr_t is a 64-bit value.
 * without HIGHMEM,  dma_addr_t is a 32-bit value.
 *
 * The compiler should always fix up the assignment
 * to u.wq appropriately, depending upon the size of
 * dma_addr_t.
 */
static inline
void dma_addr_writeql(dma_addr_t addr, void __iomem *write_address)
{
	union {
		u64 wq;
		uint wl[2];
	} u;

	u.wq = addr;

	writel(u.wl[0], write_address);
	writel(u.wl[1], write_address + 4);
}

/*
 * DAC960 BA Series Controller Interface Register Offsets.
 */

#define DAC960_BA_mmio_size		0x80

enum DAC960_BA_reg_offset {
	DAC960_BA_IRQSTS_OFFSET	= 0x30,
	DAC960_BA_IRQMASK_OFFSET = 0x34,
	DAC960_BA_CMDMBX_OFFSET = 0x50,
	DAC960_BA_CMDSTS_OFFSET = 0x58,
	DAC960_BA_IDB_OFFSET	= 0x60,
	DAC960_BA_ODB_OFFSET	= 0x61,
	DAC960_BA_ERRSTS_OFFSET = 0x63,
};

/*
 * DAC960 BA Series Inbound Door Bell Register.
 */
#define DAC960_BA_IDB_HWMBOX_NEW_CMD	0x01
#define DAC960_BA_IDB_HWMBOX_ACK_STS	0x02
#define DAC960_BA_IDB_GEN_IRQ		0x04
#define DAC960_BA_IDB_CTRL_RESET	0x08
#define DAC960_BA_IDB_MMBOX_NEW_CMD	0x10

#define DAC960_BA_IDB_HWMBOX_EMPTY	0x01
#define DAC960_BA_IDB_INIT_DONE		0x02

/*
 * DAC960 BA Series Outbound Door Bell Register.
 */
#define DAC960_BA_ODB_HWMBOX_ACK_IRQ	0x01
#define DAC960_BA_ODB_MMBOX_ACK_IRQ	0x02

#define DAC960_BA_ODB_HWMBOX_STS_AVAIL	0x01
#define DAC960_BA_ODB_MMBOX_STS_AVAIL	0x02

/*
 * DAC960 BA Series Interrupt Mask Register.
 */
#define DAC960_BA_IRQMASK_DISABLE_IRQ	0x04
#define DAC960_BA_IRQMASK_DISABLEW_I2O	0x08

/*
 * DAC960 BA Series Error Status Register.
 */
#define DAC960_BA_ERRSTS_PENDING	0x04

/*
 * DAC960 LP Series Controller Interface Register Offsets.
 */

#define DAC960_LP_mmio_size		0x80

enum DAC960_LP_reg_offset {
	DAC960_LP_CMDMBX_OFFSET = 0x10,
	DAC960_LP_CMDSTS_OFFSET = 0x18,
	DAC960_LP_IDB_OFFSET	= 0x20,
	DAC960_LP_ODB_OFFSET	= 0x2C,
	DAC960_LP_ERRSTS_OFFSET = 0x2E,
	DAC960_LP_IRQSTS_OFFSET	= 0x30,
	DAC960_LP_IRQMASK_OFFSET = 0x34,
};

/*
 * DAC960 LP Series Inbound Door Bell Register.
 */
#define DAC960_LP_IDB_HWMBOX_NEW_CMD	0x01
#define DAC960_LP_IDB_HWMBOX_ACK_STS	0x02
#define DAC960_LP_IDB_GEN_IRQ		0x04
#define DAC960_LP_IDB_CTRL_RESET	0x08
#define DAC960_LP_IDB_MMBOX_NEW_CMD	0x10

#define DAC960_LP_IDB_HWMBOX_FULL	0x01
#define DAC960_LP_IDB_INIT_IN_PROGRESS	0x02

/*
 * DAC960 LP Series Outbound Door Bell Register.
 */
#define DAC960_LP_ODB_HWMBOX_ACK_IRQ	0x01
#define DAC960_LP_ODB_MMBOX_ACK_IRQ	0x02

#define DAC960_LP_ODB_HWMBOX_STS_AVAIL	0x01
#define DAC960_LP_ODB_MMBOX_STS_AVAIL	0x02

/*
 * DAC960 LP Series Interrupt Mask Register.
 */
#define DAC960_LP_IRQMASK_DISABLE_IRQ	0x04

/*
 * DAC960 LP Series Error Status Register.
 */
#define DAC960_LP_ERRSTS_PENDING	0x04

#endif /* _MYRS_H */
