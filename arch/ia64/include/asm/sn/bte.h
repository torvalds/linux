/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2000-2007 Silicon Graphics, Inc.  All Rights Reserved.
 */


#ifndef _ASM_IA64_SN_BTE_H
#define _ASM_IA64_SN_BTE_H

#include <linux/timer.h>
#include <linux/spinlock.h>
#include <linux/cache.h>
#include <asm/sn/pda.h>
#include <asm/sn/types.h>
#include <asm/sn/shub_mmr.h>

#define IBCT_NOTIFY             (0x1UL << 4)
#define IBCT_ZFIL_MODE          (0x1UL << 0)

/* #define BTE_DEBUG */
/* #define BTE_DEBUG_VERBOSE */

#ifdef BTE_DEBUG
#  define BTE_PRINTK(x) printk x	/* Terse */
#  ifdef BTE_DEBUG_VERBOSE
#    define BTE_PRINTKV(x) printk x	/* Verbose */
#  else
#    define BTE_PRINTKV(x)
#  endif /* BTE_DEBUG_VERBOSE */
#else
#  define BTE_PRINTK(x)
#  define BTE_PRINTKV(x)
#endif	/* BTE_DEBUG */


/* BTE status register only supports 16 bits for length field */
#define BTE_LEN_BITS (16)
#define BTE_LEN_MASK ((1 << BTE_LEN_BITS) - 1)
#define BTE_MAX_XFER (BTE_LEN_MASK << L1_CACHE_SHIFT)


/* Define hardware */
#define BTES_PER_NODE (is_shub2() ? 4 : 2)
#define MAX_BTES_PER_NODE 4

#define BTE2OFF_CTRL	0
#define BTE2OFF_SRC	(SH2_BT_ENG_SRC_ADDR_0 - SH2_BT_ENG_CSR_0)
#define BTE2OFF_DEST	(SH2_BT_ENG_DEST_ADDR_0 - SH2_BT_ENG_CSR_0)
#define BTE2OFF_NOTIFY	(SH2_BT_ENG_NOTIF_ADDR_0 - SH2_BT_ENG_CSR_0)

#define BTE_BASE_ADDR(interface) 				\
    (is_shub2() ? (interface == 0) ? SH2_BT_ENG_CSR_0 :		\
		  (interface == 1) ? SH2_BT_ENG_CSR_1 :		\
		  (interface == 2) ? SH2_BT_ENG_CSR_2 :		\
		  		     SH2_BT_ENG_CSR_3 		\
		: (interface == 0) ? IIO_IBLS0 : IIO_IBLS1)

#define BTE_SOURCE_ADDR(base)					\
    (is_shub2() ? base + (BTE2OFF_SRC/8) 			\
		: base + (BTEOFF_SRC/8))

#define BTE_DEST_ADDR(base)					\
    (is_shub2() ? base + (BTE2OFF_DEST/8) 			\
		: base + (BTEOFF_DEST/8))

#define BTE_CTRL_ADDR(base)					\
    (is_shub2() ? base + (BTE2OFF_CTRL/8) 			\
		: base + (BTEOFF_CTRL/8))

#define BTE_NOTIF_ADDR(base)					\
    (is_shub2() ? base + (BTE2OFF_NOTIFY/8) 			\
		: base + (BTEOFF_NOTIFY/8))

/* Define hardware modes */
#define BTE_NOTIFY IBCT_NOTIFY
#define BTE_NORMAL BTE_NOTIFY
#define BTE_ZERO_FILL (BTE_NOTIFY | IBCT_ZFIL_MODE)
/* Use a reserved bit to let the caller specify a wait for any BTE */
#define BTE_WACQUIRE 0x4000
/* Use the BTE on the node with the destination memory */
#define BTE_USE_DEST (BTE_WACQUIRE << 1)
/* Use any available BTE interface on any node for the transfer */
#define BTE_USE_ANY (BTE_USE_DEST << 1)
/* macro to force the IBCT0 value valid */
#define BTE_VALID_MODE(x) ((x) & (IBCT_NOTIFY | IBCT_ZFIL_MODE))

#define BTE_ACTIVE		(IBLS_BUSY | IBLS_ERROR)
#define BTE_WORD_AVAILABLE	(IBLS_BUSY << 1)
#define BTE_WORD_BUSY		(~BTE_WORD_AVAILABLE)

/*
 * Some macros to simplify reading.
 * Start with macros to locate the BTE control registers.
 */
#define BTE_LNSTAT_LOAD(_bte)						\
			HUB_L(_bte->bte_base_addr)
#define BTE_LNSTAT_STORE(_bte, _x)					\
			HUB_S(_bte->bte_base_addr, (_x))
#define BTE_SRC_STORE(_bte, _x)						\
({									\
		u64 __addr = ((_x) & ~AS_MASK);				\
		if (is_shub2()) 					\
			__addr = SH2_TIO_PHYS_TO_DMA(__addr);		\
		HUB_S(_bte->bte_source_addr, __addr);			\
})
#define BTE_DEST_STORE(_bte, _x)					\
({									\
		u64 __addr = ((_x) & ~AS_MASK);				\
		if (is_shub2()) 					\
			__addr = SH2_TIO_PHYS_TO_DMA(__addr);		\
		HUB_S(_bte->bte_destination_addr, __addr);		\
})
#define BTE_CTRL_STORE(_bte, _x)					\
			HUB_S(_bte->bte_control_addr, (_x))
#define BTE_NOTIF_STORE(_bte, _x)					\
({									\
		u64 __addr = ia64_tpa((_x) & ~AS_MASK);			\
		if (is_shub2()) 					\
			__addr = SH2_TIO_PHYS_TO_DMA(__addr);		\
		HUB_S(_bte->bte_notify_addr, __addr);			\
})

#define BTE_START_TRANSFER(_bte, _len, _mode)				\
	is_shub2() ? BTE_CTRL_STORE(_bte, IBLS_BUSY | (_mode << 24) | _len) \
		: BTE_LNSTAT_STORE(_bte, _len);				\
		  BTE_CTRL_STORE(_bte, _mode)

/* Possible results from bte_copy and bte_unaligned_copy */
/* The following error codes map into the BTE hardware codes
 * IIO_ICRB_ECODE_* (in shubio.h). The hardware uses
 * an error code of 0 (IIO_ICRB_ECODE_DERR), but we want zero
 * to mean BTE_SUCCESS, so add one (BTEFAIL_OFFSET) to the error
 * codes to give the following error codes.
 */
#define BTEFAIL_OFFSET	1

typedef enum {
	BTE_SUCCESS,		/* 0 is success */
	BTEFAIL_DIR,		/* Directory error due to IIO access*/
	BTEFAIL_POISON,		/* poison error on IO access (write to poison page) */
	BTEFAIL_WERR,		/* Write error (ie WINV to a Read only line) */
	BTEFAIL_ACCESS,		/* access error (protection violation) */
	BTEFAIL_PWERR,		/* Partial Write Error */
	BTEFAIL_PRERR,		/* Partial Read Error */
	BTEFAIL_TOUT,		/* CRB Time out */
	BTEFAIL_XTERR,		/* Incoming xtalk pkt had error bit */
	BTEFAIL_NOTAVAIL,	/* BTE not available */
} bte_result_t;

#define BTEFAIL_SH2_RESP_SHORT	0x1	/* bit 000001 */
#define BTEFAIL_SH2_RESP_LONG	0x2	/* bit 000010 */
#define BTEFAIL_SH2_RESP_DSP	0x4	/* bit 000100 */
#define BTEFAIL_SH2_RESP_ACCESS	0x8	/* bit 001000 */
#define BTEFAIL_SH2_CRB_TO	0x10	/* bit 010000 */
#define BTEFAIL_SH2_NACK_LIMIT	0x20	/* bit 100000 */
#define BTEFAIL_SH2_ALL		0x3F	/* bit 111111 */

#define	BTE_ERR_BITS	0x3FUL
#define	BTE_ERR_SHIFT	36
#define BTE_ERR_MASK	(BTE_ERR_BITS << BTE_ERR_SHIFT)

#define BTE_ERROR_RETRY(value)						\
	(is_shub2() ? (value != BTEFAIL_SH2_CRB_TO)			\
		: (value != BTEFAIL_TOUT))

/*
 * On shub1 BTE_ERR_MASK will always be false, so no need for is_shub2()
 */
#define BTE_SHUB2_ERROR(_status)					\
	((_status & BTE_ERR_MASK) 					\
	   ? (((_status >> BTE_ERR_SHIFT) & BTE_ERR_BITS) | IBLS_ERROR) \
	   : _status)

#define BTE_GET_ERROR_STATUS(_status)					\
	(BTE_SHUB2_ERROR(_status) & ~IBLS_ERROR)

#define BTE_VALID_SH2_ERROR(value)					\
	((value >= BTEFAIL_SH2_RESP_SHORT) && (value <= BTEFAIL_SH2_ALL))

/*
 * Structure defining a bte.  An instance of this
 * structure is created in the nodepda for each
 * bte on that node (as defined by BTES_PER_NODE)
 * This structure contains everything necessary
 * to work with a BTE.
 */
struct bteinfo_s {
	volatile u64 notify ____cacheline_aligned;
	u64 *bte_base_addr ____cacheline_aligned;
	u64 *bte_source_addr;
	u64 *bte_destination_addr;
	u64 *bte_control_addr;
	u64 *bte_notify_addr;
	spinlock_t spinlock;
	cnodeid_t bte_cnode;	/* cnode                            */
	int bte_error_count;	/* Number of errors encountered     */
	int bte_num;		/* 0 --> BTE0, 1 --> BTE1           */
	int cleanup_active;	/* Interface is locked for cleanup  */
	volatile bte_result_t bh_error;	/* error while processing   */
	volatile u64 *most_rcnt_na;
	struct bteinfo_s *btes_to_try[MAX_BTES_PER_NODE];
};


/*
 * Function prototypes (functions defined in bte.c, used elsewhere)
 */
extern bte_result_t bte_copy(u64, u64, u64, u64, void *);
extern bte_result_t bte_unaligned_copy(u64, u64, u64, u64);
extern void bte_error_handler(unsigned long);

#define bte_zero(dest, len, mode, notification) \
	bte_copy(0, dest, len, ((mode) | BTE_ZERO_FILL), notification)

/*
 * The following is the prefered way of calling bte_unaligned_copy
 * If the copy is fully cache line aligned, then bte_copy is
 * used instead.  Since bte_copy is inlined, this saves a call
 * stack.  NOTE: bte_copy is called synchronously and does block
 * until the transfer is complete.  In order to get the asynch
 * version of bte_copy, you must perform this check yourself.
 */
#define BTE_UNALIGNED_COPY(src, dest, len, mode)			\
	(((len & (L1_CACHE_BYTES - 1)) ||				\
	  (src & (L1_CACHE_BYTES - 1)) ||				\
	  (dest & (L1_CACHE_BYTES - 1))) ?				\
	 bte_unaligned_copy(src, dest, len, mode) :			\
	 bte_copy(src, dest, len, mode, NULL))


#endif	/* _ASM_IA64_SN_BTE_H */
