/*
 * HND Run Time Environment debug info area
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: hnd_debug.h 678890 2017-01-11 11:48:36Z $
 */

#ifndef	_HND_DEBUG_H
#define	_HND_DEBUG_H

/* Magic number at a magic location to find HND_DEBUG pointers */
#define HND_DEBUG_PTR_PTR_MAGIC 0x50504244  	/* DBPP */

/* Magic number at a magic location to find RAM size */
#define HND_RAMSIZE_PTR_MAGIC	0x534d4152	/* RAMS */

#ifndef _LANGUAGE_ASSEMBLY

/* Includes only when building dongle code */


/* We use explicit sizes here since this gets included from different
 * systems.  The sizes must be the size of the creating system
 * (currently 32 bit ARM) since this is gleaned from  dump.
 */

#ifdef FWID
extern uint32 gFWID;
#endif

/* Define pointers for use on other systems */
#define _HD_EVLOG_P	uint32
#define _HD_CONS_P	uint32
#define _HD_TRAP_P	uint32

/* This struct is placed at a well-defined location, and contains a pointer to hnd_debug. */
typedef struct hnd_debug_ptr {
	uint32	magic;

	/* RAM address of 'hnd_debug'. For legacy versions of this struct, it is a 0-indexed
	 * offset instead.
	 */
	uint32	hnd_debug_addr;

	/* Base address of RAM. This field does not exist for legacy versions of this struct.  */
	uint32	ram_base_addr;

} hnd_debug_ptr_t;

/* This struct is placed at a well-defined location. */
typedef struct hnd_ramsize_ptr {
	uint32	magic;			/* 'RAMS' */

	/* RAM size information. */
	uint32	ram_size;
} hnd_ramsize_ptr_t;

#define  HND_DEBUG_EPIVERS_MAX_STR_LEN	32
#define  HND_DEBUG_BUILD_SIGNATURE_FWID_LEN	17
#define  HND_DEBUG_BUILD_SIGNATURE_VER_LEN	22
typedef struct hnd_debug {
	uint32	magic;
#define HND_DEBUG_MAGIC 0x47424544	/* 'DEBG' */

	uint32	version;		/* Debug struct version */
#define HND_DEBUG_VERSION 1

	uint32	fwid;			/* 4 bytes of fw info */
	char	epivers[HND_DEBUG_EPIVERS_MAX_STR_LEN];

	_HD_TRAP_P trap_ptr;		/* trap_t data struct */
	_HD_CONS_P console;		/* Console  */

	uint32	ram_base;
	uint32	ram_size;

	uint32	rom_base;
	uint32	rom_size;

	_HD_EVLOG_P event_log_top;

	/* To populated fields below,
	 * INCLUDE_BUILD_SIGNATURE_IN_SOCRAM needs to be enabled
	 */
	char fwid_signature[HND_DEBUG_BUILD_SIGNATURE_FWID_LEN]; /* fwid=<FWID> */
	char ver_signature[HND_DEBUG_BUILD_SIGNATURE_VER_LEN]; /* ver=abc.abc.abc.abc */

} hnd_debug_t;

/*
 * timeval_t and prstatus_t are copies of the Linux structures.
 * Included here because we need the definitions for the target processor
 * (32 bits) and not the definition on the host this is running on
 * (which could be 64 bits).
 */

typedef struct             {    /* Time value with microsecond resolution    */
	uint32 tv_sec;	/* Seconds                                   */
	uint32 tv_usec;	/* Microseconds                              */
} timeval_t;


/* Linux/ARM 32 prstatus for notes section */
typedef struct prstatus {
	  int32 si_signo; 	/* Signal number */
	  int32 si_code; 	/* Extra code */
	  int32 si_errno; 	/* Errno */
	  uint16 pr_cursig; 	/* Current signal.  */
	  uint16 unused;
	  uint32 pr_sigpend;	/* Set of pending signals.  */
	  uint32 pr_sighold;	/* Set of held signals.  */
	  uint32 pr_pid;
	  uint32 pr_ppid;
	  uint32 pr_pgrp;
	  uint32 pr_sid;
	  timeval_t pr_utime;	/* User time.  */
	  timeval_t pr_stime;	/* System time.  */
	  timeval_t pr_cutime;	/* Cumulative user time.  */
	  timeval_t pr_cstime;	/* Cumulative system time.  */
	  uint32 uregs[18];
	  int32 pr_fpvalid;	/* True if math copro being used.  */
} prstatus_t;

/* for mkcore and other utilities use */
#define DUMP_INFO_PTR_PTR_0   0x74
#define DUMP_INFO_PTR_PTR_1   0x78
#define DUMP_INFO_PTR_PTR_2   0xf0
#define DUMP_INFO_PTR_PTR_3   0xf8
#define DUMP_INFO_PTR_PTR_4   0x874
#define DUMP_INFO_PTR_PTR_5   0x878
#define DUMP_INFO_PTR_PTR_END 0xffffffff
#define DUMP_INFO_PTR_PTR_LIST	DUMP_INFO_PTR_PTR_0, \
		DUMP_INFO_PTR_PTR_1,					\
		DUMP_INFO_PTR_PTR_2,					\
		DUMP_INFO_PTR_PTR_3,					\
		DUMP_INFO_PTR_PTR_4,					\
		DUMP_INFO_PTR_PTR_5,					\
		DUMP_INFO_PTR_PTR_END

/* for DHD driver to get dongle ram size info. */
#define RAMSIZE_PTR_PTR_0	0x6c
#define RAMSIZE_PTR_PTR_END	0xffffffff
#define RAMSIZE_PTR_PTR_LIST	RAMSIZE_PTR_PTR_0, \
				RAMSIZE_PTR_PTR_END

typedef struct hnd_ext_trap_hdr {
	uint8 version;    /* Extended trap version info */
	uint8 reserved;   /* currently unused */
	uint16 len;       /* Length of data excluding this header */
	uint8 data[];     /* TLV data */
} hnd_ext_trap_hdr_t;

#define TAG_TRAP_SIGNATURE       1  /* Processor register dumps */
#define TAG_TRAP_STACK           2  /* Processor stack dump (possible code locations) */
#define TAG_TRAP_MEMORY          3  /* Memory subsystem dump */
#define TAG_TRAP_DEEPSLEEP       4  /* Deep sleep health check failures */
#define TAG_TRAP_PSM_WD          5  /* PSM watchdog information */
#define TAG_TRAP_PHY             6  /* Phy related issues */
#define TAG_TRAP_BUS             7  /* Bus level issues */
#define TAG_TRAP_MAC             8  /* Mac level issues */
#define TAG_TRAP_BACKPLANE       9  /* Backplane related errors */

typedef struct hnd_ext_trap_bp_err
{
	uint32 error;
	uint32 coreid;
	uint32 baseaddr;
	uint32 ioctrl;
	uint32 iostatus;
	uint32 resetctrl;
	uint32 resetstatus;
	uint32 errlogctrl;
	uint32 errlogdone;
	uint32 errlogstatus;
	uint32 errlogaddrlo;
	uint32 errlogaddrhi;
	uint32 errlogid;
	uint32 errloguser;
	uint32 errlogflags;
} hnd_ext_trap_bp_err_t;

#endif /* !LANGUAGE_ASSEMBLY */

#endif /* _HND_DEBUG_H */
