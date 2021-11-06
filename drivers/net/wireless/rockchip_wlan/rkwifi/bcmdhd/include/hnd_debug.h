/*
 * HND Run Time Environment debug info area
 *
 * Copyright (C) 2020, Broadcom.
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
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#ifndef	_HND_DEBUG_H
#define	_HND_DEBUG_H

/* Magic number at a magic location to find HND_DEBUG pointers */
#define HND_DEBUG_PTR_PTR_MAGIC 0x50504244u	/* DBPP */

#ifndef _LANGUAGE_ASSEMBLY

#include <typedefs.h>

/* Includes only when building dongle code */
#ifdef _RTE_
#include <event_log.h>
#include <hnd_trap.h>
#include <hnd_cons.h>
#endif

/* We use explicit sizes here since this gets included from different
 * systems.  The sizes must be the size of the creating system
 * (currently 32 bit ARM) since this is gleaned from  dump.
 */

#ifdef FWID
extern uint32 gFWID;
#endif

enum hnd_debug_reloc_entry_type {
	HND_DEBUG_RELOC_ENTRY_TYPE_ROM		= 0u,
	HND_DEBUG_RELOC_ENTRY_TYPE_RAM		= 1u,
	HND_DEBUG_RELOC_ENTRY_TYPE_MTH_STACK	= 2u, /* main thread stack */
};
typedef uint32 hnd_debug_reloc_entry_type_t;

typedef struct hnd_debug_reloc_entry {
	/* Identifies the type(hnd_debug_reloc_entry_type) of the data */
	hnd_debug_reloc_entry_type_t type;
	uint32 phys_addr;		/* Physical address */
	uint32 virt_addr;		/* Virtual address */
	uint32 size;			/* Specifies the size of the segment */
} hnd_debug_reloc_entry_t;

#ifdef _RTE_
/* Define pointers for normal ARM use */
#define _HD_EVLOG_P		event_log_top_t *
#define _HD_CONS_P		hnd_cons_t *
#define _HD_TRAP_P		trap_t *
#define _HD_DEBUG_RELOC_ENTRY_P	hnd_debug_reloc_entry_t *
#define _HD_DEBUG_RELOC_P	hnd_debug_reloc_t *

#else
/* Define pointers for use on other systems */
#define _HD_EVLOG_P		uint32
#define _HD_CONS_P		uint32
#define _HD_TRAP_P		uint32
#define _HD_DEBUG_RELOC_ENTRY_P	uint32
#define _HD_DEBUG_RELOC_P	uint32

#endif /* _RTE_ */

/* MMU relocation info in the debug area */
typedef struct hnd_debug_reloc {
	_HD_DEBUG_RELOC_ENTRY_P hnd_reloc_ptr;	/* contains the pointer to the MMU reloc table */
	uint32 hnd_reloc_ptr_size;		/* Specifies the size of the MMU reloc table */
} hnd_debug_reloc_t;

/* Number of MMU relocation entries supported in v2 */
#define RELOC_NUM_ENTRIES		4u

/* Total MMU relocation table size for v2 */
#define HND_DEBUG_RELOC_PTR_SIZE	(RELOC_NUM_ENTRIES * sizeof(hnd_debug_reloc_entry_t))

#define HND_DEBUG_VERSION_1	1u	/* Legacy, version 1 */
#define HND_DEBUG_VERSION_2	2u	/* Version 2 contains the MMU information
					 * used for stack virtualization, etc.
					 */

/* Legacy debug version for older branches. */
#define HND_DEBUG_VERSION	HND_DEBUG_VERSION_1

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
extern hnd_debug_ptr_t debug_info_ptr;

#define  HND_DEBUG_EPIVERS_MAX_STR_LEN		32u

/* chip id string is 8 bytes long with null terminator. Example 43452a3 */
#define  HND_DEBUG_BUILD_SIGNATURE_CHIPID_LEN	13u

#define  HND_DEBUG_BUILD_SIGNATURE_FWID_LEN	17u

/* ver=abc.abc.abc.abcdefgh size = 24bytes. 6 bytes extra for expansion */
#define  HND_DEBUG_BUILD_SIGNATURE_VER_LEN	30u

typedef struct hnd_debug {
	uint32	magic;
#define HND_DEBUG_MAGIC 0x47424544u	/* 'DEBG' */

#ifndef HND_DEBUG_USE_V2
	uint32	version;		/* Legacy, debug struct version */
#else
	/* Note: The original uint32 version is split into two fields:
	 * uint16 version and uint16 length to accomidate future expansion
	 * of the strucutre.
	 *
	 * The length field is not populated for the version 1 of the structure.
	 */
	uint16	version;		/* Debug struct version */
	uint16	length;			/* Size of the whole structure in bytes */
#endif /* HND_DEBUG_USE_V2 */

	uint32	fwid;			/* 4 bytes of fw info */
	char	epivers[HND_DEBUG_EPIVERS_MAX_STR_LEN];

	_HD_TRAP_P PHYS_ADDR_N(trap_ptr);	/* trap_t data struct physical address. */
	_HD_CONS_P PHYS_ADDR_N(console);	/* Console physical address. */

	uint32	ram_base;
	uint32	ram_size;

	uint32	rom_base;
	uint32	rom_size;

	_HD_EVLOG_P event_log_top;	/* EVENT_LOG address. */

	/* To populated fields below,
	 * INCLUDE_BUILD_SIGNATURE_IN_SOCRAM needs to be enabled
	 */
	char fwid_signature[HND_DEBUG_BUILD_SIGNATURE_FWID_LEN]; /* fwid=<FWID> */
	/* ver=abc.abc.abc.abcdefgh size = 24bytes. 6 bytes extra for expansion */
	char ver_signature[HND_DEBUG_BUILD_SIGNATURE_VER_LEN];
	char chipid_signature[HND_DEBUG_BUILD_SIGNATURE_CHIPID_LEN]; /* chip=12345a3 */

#ifdef HND_DEBUG_USE_V2
	/* Version 2 fields */
	/* Specifies the hnd debug MMU info */
	_HD_DEBUG_RELOC_P	hnd_debug_reloc_ptr;
#endif /* HND_DEBUG_USE_V2 */
} hnd_debug_t;

#ifdef HND_DEBUG_USE_V2
#define HND_DEBUG_V1_SIZE       (OFFSETOF(hnd_debug_t, chipid_signature) + \
				 sizeof(((hnd_debug_t *)0)->chipid_signature))

#define HND_DEBUG_V2_BASE_SIZE  (OFFSETOF(hnd_debug_t, hnd_debug_reloc_ptr) + \
				 sizeof(((hnd_debug_t *)0)->hnd_debug_reloc_ptr))
#endif /* HND_DEBUG_USE_V2 */

/* The following structure is used in populating build information */
typedef struct hnd_build_info {
	uint8 version; /* Same as HND_DEBUG_VERSION */
	uint8 rsvd[3]; /* Reserved fields for padding purposes */
	/* To populated fields below,
	 * INCLUDE_BUILD_SIGNATURE_IN_SOCRAM needs to be enabled
	 */
	uint32 fwid;
	uint32 ver[4];
	char chipid_signature[HND_DEBUG_BUILD_SIGNATURE_CHIPID_LEN]; /* chip=12345a3 */
} hnd_build_info_t;

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

extern bool hnd_debug_info_in_trap_context(void);

/* Get build information. */
extern int hnd_build_info_get(void *ctx, void *arg2, uint32 *buf, uint16 *len);

#endif /* !LANGUAGE_ASSEMBLY */

#endif /* _HND_DEBUG_H */
