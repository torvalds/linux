/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SN Platform GRU Driver
 *
 *              GRU HANDLE DEFINITION
 *
 *  Copyright (c) 2008 Silicon Graphics, Inc.  All Rights Reserved.
 */

#ifndef __GRUHANDLES_H__
#define __GRUHANDLES_H__
#include "gru_instructions.h"

/*
 * Manifest constants for GRU Memory Map
 */
#define GRU_GSEG0_BASE		0
#define GRU_MCS_BASE		(64 * 1024 * 1024)
#define GRU_SIZE		(128UL * 1024 * 1024)

/* Handle & resource counts */
#define GRU_NUM_CB		128
#define GRU_NUM_DSR_BYTES	(32 * 1024)
#define GRU_NUM_TFM		16
#define GRU_NUM_TGH		24
#define GRU_NUM_CBE		128
#define GRU_NUM_TFH		128
#define GRU_NUM_CCH		16

/* Maximum resource counts that can be reserved by user programs */
#define GRU_NUM_USER_CBR	GRU_NUM_CBE
#define GRU_NUM_USER_DSR_BYTES	GRU_NUM_DSR_BYTES

/* Bytes per handle & handle stride. Code assumes all cb, tfh, cbe handles
 * are the same */
#define GRU_HANDLE_BYTES	64
#define GRU_HANDLE_STRIDE	256

/* Base addresses of handles */
#define GRU_TFM_BASE		(GRU_MCS_BASE + 0x00000)
#define GRU_TGH_BASE		(GRU_MCS_BASE + 0x08000)
#define GRU_CBE_BASE		(GRU_MCS_BASE + 0x10000)
#define GRU_TFH_BASE		(GRU_MCS_BASE + 0x18000)
#define GRU_CCH_BASE		(GRU_MCS_BASE + 0x20000)

/* User gseg constants */
#define GRU_GSEG_STRIDE		(4 * 1024 * 1024)
#define GSEG_BASE(a)		((a) & ~(GRU_GSEG_PAGESIZE - 1))

/* Data segment constants */
#define GRU_DSR_AU_BYTES	1024
#define GRU_DSR_CL		(GRU_NUM_DSR_BYTES / GRU_CACHE_LINE_BYTES)
#define GRU_DSR_AU_CL		(GRU_DSR_AU_BYTES / GRU_CACHE_LINE_BYTES)
#define GRU_DSR_AU		(GRU_NUM_DSR_BYTES / GRU_DSR_AU_BYTES)

/* Control block constants */
#define GRU_CBR_AU_SIZE		2
#define GRU_CBR_AU		(GRU_NUM_CBE / GRU_CBR_AU_SIZE)

/* Convert resource counts to the number of AU */
#define GRU_DS_BYTES_TO_AU(n)	DIV_ROUND_UP(n, GRU_DSR_AU_BYTES)
#define GRU_CB_COUNT_TO_AU(n)	DIV_ROUND_UP(n, GRU_CBR_AU_SIZE)

/* UV limits */
#define GRU_CHIPLETS_PER_HUB	2
#define GRU_HUBS_PER_BLADE	1
#define GRU_CHIPLETS_PER_BLADE	(GRU_HUBS_PER_BLADE * GRU_CHIPLETS_PER_HUB)

/* User GRU Gseg offsets */
#define GRU_CB_BASE		0
#define GRU_CB_LIMIT		(GRU_CB_BASE + GRU_HANDLE_STRIDE * GRU_NUM_CBE)
#define GRU_DS_BASE		0x20000
#define GRU_DS_LIMIT		(GRU_DS_BASE + GRU_NUM_DSR_BYTES)

/* Convert a GRU physical address to the chiplet offset */
#define GSEGPOFF(h) 		((h) & (GRU_SIZE - 1))

/* Convert an arbitrary handle address to the beginning of the GRU segment */
#define GRUBASE(h)		((void *)((unsigned long)(h) & ~(GRU_SIZE - 1)))

/* Test a valid handle address to determine the type */
#define TYPE_IS(hn, h)		((h) >= GRU_##hn##_BASE && (h) <	\
		GRU_##hn##_BASE + GRU_NUM_##hn * GRU_HANDLE_STRIDE &&   \
		(((h) & (GRU_HANDLE_STRIDE - 1)) == 0))


/* General addressing macros. */
static inline void *get_gseg_base_address(void *base, int ctxnum)
{
	return (void *)(base + GRU_GSEG0_BASE + GRU_GSEG_STRIDE * ctxnum);
}

static inline void *get_gseg_base_address_cb(void *base, int ctxnum, int line)
{
	return (void *)(get_gseg_base_address(base, ctxnum) +
			GRU_CB_BASE + GRU_HANDLE_STRIDE * line);
}

static inline void *get_gseg_base_address_ds(void *base, int ctxnum, int line)
{
	return (void *)(get_gseg_base_address(base, ctxnum) + GRU_DS_BASE +
			GRU_CACHE_LINE_BYTES * line);
}

static inline struct gru_tlb_fault_map *get_tfm(void *base, int ctxnum)
{
	return (struct gru_tlb_fault_map *)(base + GRU_TFM_BASE +
					ctxnum * GRU_HANDLE_STRIDE);
}

static inline struct gru_tlb_global_handle *get_tgh(void *base, int ctxnum)
{
	return (struct gru_tlb_global_handle *)(base + GRU_TGH_BASE +
					ctxnum * GRU_HANDLE_STRIDE);
}

static inline struct gru_control_block_extended *get_cbe(void *base, int ctxnum)
{
	return (struct gru_control_block_extended *)(base + GRU_CBE_BASE +
					ctxnum * GRU_HANDLE_STRIDE);
}

static inline struct gru_tlb_fault_handle *get_tfh(void *base, int ctxnum)
{
	return (struct gru_tlb_fault_handle *)(base + GRU_TFH_BASE +
					ctxnum * GRU_HANDLE_STRIDE);
}

static inline struct gru_context_configuration_handle *get_cch(void *base,
					int ctxnum)
{
	return (struct gru_context_configuration_handle *)(base +
				GRU_CCH_BASE + ctxnum * GRU_HANDLE_STRIDE);
}

static inline unsigned long get_cb_number(void *cb)
{
	return (((unsigned long)cb - GRU_CB_BASE) % GRU_GSEG_PAGESIZE) /
					GRU_HANDLE_STRIDE;
}

/* byte offset to a specific GRU chiplet. (p=pnode, c=chiplet (0 or 1)*/
static inline unsigned long gru_chiplet_paddr(unsigned long paddr, int pnode,
							int chiplet)
{
	return paddr + GRU_SIZE * (2 * pnode  + chiplet);
}

static inline void *gru_chiplet_vaddr(void *vaddr, int pnode, int chiplet)
{
	return vaddr + GRU_SIZE * (2 * pnode  + chiplet);
}

static inline struct gru_control_block_extended *gru_tfh_to_cbe(
					struct gru_tlb_fault_handle *tfh)
{
	unsigned long cbe;

	cbe = (unsigned long)tfh - GRU_TFH_BASE + GRU_CBE_BASE;
	return (struct gru_control_block_extended*)cbe;
}




/*
 * Global TLB Fault Map
 * 	Bitmap of outstanding TLB misses needing interrupt/polling service.
 *
 */
struct gru_tlb_fault_map {
	unsigned long fault_bits[BITS_TO_LONGS(GRU_NUM_CBE)];
	unsigned long fill0[2];
	unsigned long done_bits[BITS_TO_LONGS(GRU_NUM_CBE)];
	unsigned long fill1[2];
};

/*
 * TGH - TLB Global Handle
 * 	Used for TLB flushing.
 *
 */
struct gru_tlb_global_handle {
	unsigned int cmd:1;		/* DW 0 */
	unsigned int delresp:1;
	unsigned int opc:1;
	unsigned int fill1:5;

	unsigned int fill2:8;

	unsigned int status:2;
	unsigned long fill3:2;
	unsigned int state:3;
	unsigned long fill4:1;

	unsigned int cause:3;
	unsigned long fill5:37;

	unsigned long vaddr:64;		/* DW 1 */

	unsigned int asid:24;		/* DW 2 */
	unsigned int fill6:8;

	unsigned int pagesize:5;
	unsigned int fill7:11;

	unsigned int global:1;
	unsigned int fill8:15;

	unsigned long vaddrmask:39;	/* DW 3 */
	unsigned int fill9:9;
	unsigned int n:10;
	unsigned int fill10:6;

	unsigned int ctxbitmap:16;	/* DW4 */
	unsigned long fill11[3];
};

enum gru_tgh_cmd {
	TGHCMD_START
};

enum gru_tgh_opc {
	TGHOP_TLBNOP,
	TGHOP_TLBINV
};

enum gru_tgh_status {
	TGHSTATUS_IDLE,
	TGHSTATUS_EXCEPTION,
	TGHSTATUS_ACTIVE
};

enum gru_tgh_state {
	TGHSTATE_IDLE,
	TGHSTATE_PE_INVAL,
	TGHSTATE_INTERRUPT_INVAL,
	TGHSTATE_WAITDONE,
	TGHSTATE_RESTART_CTX,
};

enum gru_tgh_cause {
	TGHCAUSE_RR_ECC,
	TGHCAUSE_TLB_ECC,
	TGHCAUSE_LRU_ECC,
	TGHCAUSE_PS_ECC,
	TGHCAUSE_MUL_ERR,
	TGHCAUSE_DATA_ERR,
	TGHCAUSE_SW_FORCE
};


/*
 * TFH - TLB Global Handle
 * 	Used for TLB dropins into the GRU TLB.
 *
 */
struct gru_tlb_fault_handle {
	unsigned int cmd:1;		/* DW 0 - low 32*/
	unsigned int delresp:1;
	unsigned int fill0:2;
	unsigned int opc:3;
	unsigned int fill1:9;

	unsigned int status:2;
	unsigned int fill2:2;
	unsigned int state:3;
	unsigned int fill3:1;

	unsigned int cause:6;
	unsigned int cb_int:1;
	unsigned int fill4:1;

	unsigned int indexway:12;	/* DW 0 - high 32 */
	unsigned int fill5:4;

	unsigned int ctxnum:4;
	unsigned int fill6:12;

	unsigned long missvaddr:64;	/* DW 1 */

	unsigned int missasid:24;	/* DW 2 */
	unsigned int fill7:8;
	unsigned int fillasid:24;
	unsigned int dirty:1;
	unsigned int gaa:2;
	unsigned long fill8:5;

	unsigned long pfn:41;		/* DW 3 */
	unsigned int fill9:7;
	unsigned int pagesize:5;
	unsigned int fill10:11;

	unsigned long fillvaddr:64;	/* DW 4 */

	unsigned long fill11[3];
};

enum gru_tfh_opc {
	TFHOP_NOOP,
	TFHOP_RESTART,
	TFHOP_WRITE_ONLY,
	TFHOP_WRITE_RESTART,
	TFHOP_EXCEPTION,
	TFHOP_USER_POLLING_MODE = 7,
};

enum tfh_status {
	TFHSTATUS_IDLE,
	TFHSTATUS_EXCEPTION,
	TFHSTATUS_ACTIVE,
};

enum tfh_state {
	TFHSTATE_INACTIVE,
	TFHSTATE_IDLE,
	TFHSTATE_MISS_UPM,
	TFHSTATE_MISS_FMM,
	TFHSTATE_HW_ERR,
	TFHSTATE_WRITE_TLB,
	TFHSTATE_RESTART_CBR,
};

/* TFH cause bits */
enum tfh_cause {
	TFHCAUSE_NONE,
	TFHCAUSE_TLB_MISS,
	TFHCAUSE_TLB_MOD,
	TFHCAUSE_HW_ERROR_RR,
	TFHCAUSE_HW_ERROR_MAIN_ARRAY,
	TFHCAUSE_HW_ERROR_VALID,
	TFHCAUSE_HW_ERROR_PAGESIZE,
	TFHCAUSE_INSTRUCTION_EXCEPTION,
	TFHCAUSE_UNCORRECTIBLE_ERROR,
};

/* GAA values */
#define GAA_RAM				0x0
#define GAA_NCRAM			0x2
#define GAA_MMIO			0x1
#define GAA_REGISTER			0x3

/* GRU paddr shift for pfn. (NOTE: shift is NOT by actual pagesize) */
#define GRU_PADDR_SHIFT			12

/*
 * Context Configuration handle
 * 	Used to allocate resources to a GSEG context.
 *
 */
struct gru_context_configuration_handle {
	unsigned int cmd:1;			/* DW0 */
	unsigned int delresp:1;
	unsigned int opc:3;
	unsigned int unmap_enable:1;
	unsigned int req_slice_set_enable:1;
	unsigned int req_slice:2;
	unsigned int cb_int_enable:1;
	unsigned int tlb_int_enable:1;
	unsigned int tfm_fault_bit_enable:1;
	unsigned int tlb_int_select:4;

	unsigned int status:2;
	unsigned int state:2;
	unsigned int reserved2:4;

	unsigned int cause:4;
	unsigned int tfm_done_bit_enable:1;
	unsigned int unused:3;

	unsigned int dsr_allocation_map;

	unsigned long cbr_allocation_map;	/* DW1 */

	unsigned int asid[8];			/* DW 2 - 5 */
	unsigned short sizeavail[8];		/* DW 6 - 7 */
} __attribute__ ((packed));

enum gru_cch_opc {
	CCHOP_START = 1,
	CCHOP_ALLOCATE,
	CCHOP_INTERRUPT,
	CCHOP_DEALLOCATE,
	CCHOP_INTERRUPT_SYNC,
};

enum gru_cch_status {
	CCHSTATUS_IDLE,
	CCHSTATUS_EXCEPTION,
	CCHSTATUS_ACTIVE,
};

enum gru_cch_state {
	CCHSTATE_INACTIVE,
	CCHSTATE_MAPPED,
	CCHSTATE_ACTIVE,
	CCHSTATE_INTERRUPTED,
};

/* CCH Exception cause */
enum gru_cch_cause {
	CCHCAUSE_REGION_REGISTER_WRITE_ERROR = 1,
	CCHCAUSE_ILLEGAL_OPCODE = 2,
	CCHCAUSE_INVALID_START_REQUEST = 3,
	CCHCAUSE_INVALID_ALLOCATION_REQUEST = 4,
	CCHCAUSE_INVALID_DEALLOCATION_REQUEST = 5,
	CCHCAUSE_INVALID_INTERRUPT_REQUEST = 6,
	CCHCAUSE_CCH_BUSY = 7,
	CCHCAUSE_NO_CBRS_TO_ALLOCATE = 8,
	CCHCAUSE_BAD_TFM_CONFIG = 9,
	CCHCAUSE_CBR_RESOURCES_OVERSUBSCRIPED = 10,
	CCHCAUSE_DSR_RESOURCES_OVERSUBSCRIPED = 11,
	CCHCAUSE_CBR_DEALLOCATION_ERROR = 12,
};
/*
 * CBE - Control Block Extended
 * 	Maintains internal GRU state for active CBs.
 *
 */
struct gru_control_block_extended {
	unsigned int reserved0:1;	/* DW 0  - low */
	unsigned int imacpy:3;
	unsigned int reserved1:4;
	unsigned int xtypecpy:3;
	unsigned int iaa0cpy:2;
	unsigned int iaa1cpy:2;
	unsigned int reserved2:1;
	unsigned int opccpy:8;
	unsigned int exopccpy:8;

	unsigned int idef2cpy:22;	/* DW 0  - high */
	unsigned int reserved3:10;

	unsigned int idef4cpy:22;	/* DW 1 */
	unsigned int reserved4:10;
	unsigned int idef4upd:22;
	unsigned int reserved5:10;

	unsigned long idef1upd:64;	/* DW 2 */

	unsigned long idef5cpy:64;	/* DW 3 */

	unsigned long idef6cpy:64;	/* DW 4 */

	unsigned long idef3upd:64;	/* DW 5 */

	unsigned long idef5upd:64;	/* DW 6 */

	unsigned int idef2upd:22;	/* DW 7 */
	unsigned int reserved6:10;

	unsigned int ecause:20;
	unsigned int cbrstate:4;
	unsigned int cbrexecstatus:8;
};

/* CBE fields for active BCOPY instructions */
#define cbe_baddr0	idef1upd
#define cbe_baddr1	idef3upd
#define cbe_src_cl	idef6cpy
#define cbe_nelemcur	idef5upd

enum gru_cbr_state {
	CBRSTATE_INACTIVE,
	CBRSTATE_IDLE,
	CBRSTATE_PE_CHECK,
	CBRSTATE_QUEUED,
	CBRSTATE_WAIT_RESPONSE,
	CBRSTATE_INTERRUPTED,
	CBRSTATE_INTERRUPTED_MISS_FMM,
	CBRSTATE_BUSY_INTERRUPT_MISS_FMM,
	CBRSTATE_INTERRUPTED_MISS_UPM,
	CBRSTATE_BUSY_INTERRUPTED_MISS_UPM,
	CBRSTATE_REQUEST_ISSUE,
	CBRSTATE_BUSY_INTERRUPT,
};

/* CBE cbrexecstatus bits  - defined in gru_instructions.h*/
/* CBE ecause bits  - defined in gru_instructions.h */

/*
 * Convert a processor pagesize into the strange encoded pagesize used by the
 * GRU. Processor pagesize is encoded as log of bytes per page. (or PAGE_SHIFT)
 * 	pagesize	log pagesize	grupagesize
 * 	  4k			12	0
 * 	 16k 			14	1
 * 	 64k			16	2
 * 	256k			18	3
 * 	  1m			20	4
 * 	  2m			21	5
 * 	  4m			22	6
 * 	 16m			24	7
 * 	 64m			26	8
 * 	...
 */
#define GRU_PAGESIZE(sh)	((((sh) > 20 ? (sh) + 2 : (sh)) >> 1) - 6)
#define GRU_SIZEAVAIL(sh)	(1UL << GRU_PAGESIZE(sh))

/* minimum TLB purge count to ensure a full purge */
#define GRUMAXINVAL		1024UL

int cch_allocate(struct gru_context_configuration_handle *cch);
int cch_start(struct gru_context_configuration_handle *cch);
int cch_interrupt(struct gru_context_configuration_handle *cch);
int cch_deallocate(struct gru_context_configuration_handle *cch);
int cch_interrupt_sync(struct gru_context_configuration_handle *cch);
int tgh_invalidate(struct gru_tlb_global_handle *tgh, unsigned long vaddr,
	unsigned long vaddrmask, int asid, int pagesize, int global, int n,
	unsigned short ctxbitmap);
int tfh_write_only(struct gru_tlb_fault_handle *tfh, unsigned long paddr,
	int gaa, unsigned long vaddr, int asid, int dirty, int pagesize);
void tfh_write_restart(struct gru_tlb_fault_handle *tfh, unsigned long paddr,
	int gaa, unsigned long vaddr, int asid, int dirty, int pagesize);
void tfh_user_polling_mode(struct gru_tlb_fault_handle *tfh);
void tfh_exception(struct gru_tlb_fault_handle *tfh);

#endif /* __GRUHANDLES_H__ */
