/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2000, 2001, 2002 Broadcom Corporation
 */
#ifndef CFE_API_INT_H
#define CFE_API_INT_H

/*
 * Constants.
 */
#define CFE_CMD_FW_GETINFO	0
#define CFE_CMD_FW_RESTART	1
#define CFE_CMD_FW_BOOT		2
#define CFE_CMD_FW_CPUCTL	3
#define CFE_CMD_FW_GETTIME      4
#define CFE_CMD_FW_MEMENUM	5
#define CFE_CMD_FW_FLUSHCACHE	6

#define CFE_CMD_DEV_GETHANDLE	9
#define CFE_CMD_DEV_ENUM	10
#define CFE_CMD_DEV_OPEN	11
#define CFE_CMD_DEV_INPSTAT	12
#define CFE_CMD_DEV_READ	13
#define CFE_CMD_DEV_WRITE	14
#define CFE_CMD_DEV_IOCTL	15
#define CFE_CMD_DEV_CLOSE	16
#define CFE_CMD_DEV_GETINFO	17

#define CFE_CMD_ENV_ENUM	20
#define CFE_CMD_ENV_GET		22
#define CFE_CMD_ENV_SET		23
#define CFE_CMD_ENV_DEL		24

#define CFE_CMD_MAX		32

#define CFE_CMD_VENDOR_USE	0x8000	/* codes above this are for customer use */

/*
 * Structures.
 */

/* eeek, signed "pointers" */
typedef s64 cfe_xptr_t;

struct xiocb_buffer {
	u64 buf_offset;		/* offset on device (bytes) */
	cfe_xptr_t  buf_ptr;		/* pointer to a buffer */
	u64 buf_length;		/* length of this buffer */
	u64 buf_retlen;		/* returned length (for read ops) */
	u64 buf_ioctlcmd;	/* IOCTL command (used only for IOCTLs) */
};

struct xiocb_inpstat {
	u64 inp_status;		/* 1 means input available */
};

struct xiocb_envbuf {
	s64 enum_idx;		/* 0-based enumeration index */
	cfe_xptr_t name_ptr;		/* name string buffer */
	s64 name_length;		/* size of name buffer */
	cfe_xptr_t val_ptr;		/* value string buffer */
	s64 val_length;		/* size of value string buffer */
};

struct xiocb_cpuctl {
	u64 cpu_number;		/* cpu number to control */
	u64 cpu_command;	/* command to issue to CPU */
	u64 start_addr;		/* CPU start address */
	u64 gp_val;		/* starting GP value */
	u64 sp_val;		/* starting SP value */
	u64 a1_val;		/* starting A1 value */
};

struct xiocb_time {
	s64 ticks;		/* current time in ticks */
};

struct xiocb_exitstat{
	s64 status;
};

struct xiocb_meminfo {
	s64 mi_idx;		/* 0-based enumeration index */
	s64 mi_type;		/* type of memory block */
	u64 mi_addr;		/* physical start address */
	u64 mi_size;		/* block size */
};

struct xiocb_fwinfo {
	s64 fwi_version;		/* major, minor, eco version */
	s64 fwi_totalmem;	/* total installed mem */
	s64 fwi_flags;		/* various flags */
	s64 fwi_boardid;		/* board ID */
	s64 fwi_bootarea_va;	/* VA of boot area */
	s64 fwi_bootarea_pa;	/* PA of boot area */
	s64 fwi_bootarea_size;	/* size of boot area */
	s64 fwi_reserved1;
	s64 fwi_reserved2;
	s64 fwi_reserved3;
};

struct cfe_xiocb {
	u64 xiocb_fcode;	/* IOCB function code */
	s64 xiocb_status;	/* return status */
	s64 xiocb_handle;	/* file/device handle */
	u64 xiocb_flags;	/* flags for this IOCB */
	u64 xiocb_psize;	/* size of parameter list */
	union {
		/* buffer parameters */
		struct xiocb_buffer xiocb_buffer;

		/* input status parameters */
		struct xiocb_inpstat xiocb_inpstat;

		/* environment function parameters */
		struct xiocb_envbuf xiocb_envbuf;

		/* CPU control parameters */
		struct xiocb_cpuctl xiocb_cpuctl;

		/* timer parameters */
		struct xiocb_time xiocb_time;

		/* memory arena info parameters */
		struct xiocb_meminfo xiocb_meminfo;

		/* firmware information */
		struct xiocb_fwinfo xiocb_fwinfo;

		/* Exit Status */
		struct xiocb_exitstat xiocb_exitstat;
	} plist;
};

#endif /* CFE_API_INT_H */
