/*
 * Copyright (C) 2000, 2001, 2002 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/*  *********************************************************************
    *
    *  Broadcom Common Firmware Environment (CFE)
    *
    *  Device function prototypes		File: cfe_api_int.h
    *
    *  This header defines all internal types and macros for the
    *  library.  This is stuff that's not exported to an app
    *  using the library.
    *
    *  Authors:  Mitch Lichtenberg, Chris Demetriou
    *
    ********************************************************************* */

#ifndef CFE_API_INT_H
#define CFE_API_INT_H

/*  *********************************************************************
    *  Constants
    ********************************************************************* */

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

/*  *********************************************************************
    *  Structures
    ********************************************************************* */

typedef uint64_t cfe_xuint_t;
typedef int64_t cfe_xint_t;
typedef int64_t cfe_xptr_t;

typedef struct xiocb_buffer_s {
	cfe_xuint_t buf_offset;		/* offset on device (bytes) */
	cfe_xptr_t  buf_ptr;		/* pointer to a buffer */
	cfe_xuint_t buf_length;		/* length of this buffer */
	cfe_xuint_t buf_retlen;		/* returned length (for read ops) */
	cfe_xuint_t buf_ioctlcmd;	/* IOCTL command (used only for IOCTLs) */
} xiocb_buffer_t;

#define buf_devflags buf_ioctlcmd	/* returned device info flags */

typedef struct xiocb_inpstat_s {
	cfe_xuint_t inp_status;		/* 1 means input available */
} xiocb_inpstat_t;

typedef struct xiocb_envbuf_s {
	cfe_xint_t enum_idx;		/* 0-based enumeration index */
	cfe_xptr_t name_ptr;		/* name string buffer */
	cfe_xint_t name_length;		/* size of name buffer */
	cfe_xptr_t val_ptr;		/* value string buffer */
	cfe_xint_t val_length;		/* size of value string buffer */
} xiocb_envbuf_t;

typedef struct xiocb_cpuctl_s {
	cfe_xuint_t cpu_number;		/* cpu number to control */
	cfe_xuint_t cpu_command;	/* command to issue to CPU */
	cfe_xuint_t start_addr;		/* CPU start address */
	cfe_xuint_t gp_val;		/* starting GP value */
	cfe_xuint_t sp_val;		/* starting SP value */
	cfe_xuint_t a1_val;		/* starting A1 value */
} xiocb_cpuctl_t;

typedef struct xiocb_time_s {
	cfe_xint_t ticks;		/* current time in ticks */
} xiocb_time_t;

typedef struct xiocb_exitstat_s {
	cfe_xint_t status;
} xiocb_exitstat_t;

typedef struct xiocb_meminfo_s {
	cfe_xint_t mi_idx;		/* 0-based enumeration index */
	cfe_xint_t mi_type;		/* type of memory block */
	cfe_xuint_t mi_addr;		/* physical start address */
	cfe_xuint_t mi_size;		/* block size */
} xiocb_meminfo_t;

typedef struct xiocb_fwinfo_s {
	cfe_xint_t fwi_version;		/* major, minor, eco version */
	cfe_xint_t fwi_totalmem;	/* total installed mem */
	cfe_xint_t fwi_flags;		/* various flags */
	cfe_xint_t fwi_boardid;		/* board ID */
	cfe_xint_t fwi_bootarea_va;	/* VA of boot area */
	cfe_xint_t fwi_bootarea_pa;	/* PA of boot area */
	cfe_xint_t fwi_bootarea_size;	/* size of boot area */
	cfe_xint_t fwi_reserved1;
	cfe_xint_t fwi_reserved2;
	cfe_xint_t fwi_reserved3;
} xiocb_fwinfo_t;

typedef struct cfe_xiocb_s {
	cfe_xuint_t xiocb_fcode;	/* IOCB function code */
	cfe_xint_t xiocb_status;	/* return status */
	cfe_xint_t xiocb_handle;	/* file/device handle */
	cfe_xuint_t xiocb_flags;	/* flags for this IOCB */
	cfe_xuint_t xiocb_psize;	/* size of parameter list */
	union {
		xiocb_buffer_t xiocb_buffer;	/* buffer parameters */
		xiocb_inpstat_t xiocb_inpstat;	/* input status parameters */
		xiocb_envbuf_t xiocb_envbuf;	/* environment function parameters */
		xiocb_cpuctl_t xiocb_cpuctl;	/* CPU control parameters */
		xiocb_time_t xiocb_time;	/* timer parameters */
		xiocb_meminfo_t xiocb_meminfo;	/* memory arena info parameters */
		xiocb_fwinfo_t xiocb_fwinfo;	/* firmware information */
		xiocb_exitstat_t xiocb_exitstat;	/* Exit Status */
	} plist;
} cfe_xiocb_t;

#endif				/* CFE_API_INT_H */
