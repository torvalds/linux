/*
 * CXL Flash Device Driver
 *
 * Written by: Manoj N. Kumar <manoj@linux.vnet.ibm.com>, IBM Corporation
 *             Matthew R. Ochs <mrochs@linux.vnet.ibm.com>, IBM Corporation
 *
 * Copyright (C) 2015 IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _CXLFLASH_VLUN_H
#define _CXLFLASH_VLUN_H

/* RHT - Resource Handle Table */
#define MC_RHT_NMASK      16	/* in bits */
#define MC_CHUNK_SHIFT    MC_RHT_NMASK	/* shift to go from LBA to chunk# */

#define HIBIT             (BITS_PER_LONG - 1)

#define MAX_AUN_CLONE_CNT 0xFF

/*
 * LXT - LBA Translation Table
 *
 * +-------+-------+-------+-------+-------+-------+-------+---+---+
 * | RLBA_BASE                                     |LUN_IDX| P |SEL|
 * +-------+-------+-------+-------+-------+-------+-------+---+---+
 *
 * The LXT Entry contains the physical LBA where the chunk starts (RLBA_BASE).
 * AFU ORes the low order bits from the virtual LBA (offset into the chunk)
 * with RLBA_BASE. The result is the physical LBA to be sent to storage.
 * The LXT Entry also contains an index to a LUN TBL and a bitmask of which
 * outgoing (FC) * ports can be selected. The port select bit-mask is ANDed
 * with a global port select bit-mask maintained by the driver.
 * In addition, it has permission bits that are ANDed with the
 * RHT permissions to arrive at the final permissions for the chunk.
 *
 * LXT tables are allocated dynamically in groups. This is done to avoid
 * a malloc/free overhead each time the LXT has to grow or shrink.
 *
 * Based on the current lxt_cnt (used), it is always possible to know
 * how many are allocated (used+free). The number of allocated entries is
 * not stored anywhere.
 *
 * The LXT table is re-allocated whenever it needs to cross into another group.
*/
#define LXT_GROUP_SIZE          8
#define LXT_NUM_GROUPS(lxt_cnt) (((lxt_cnt) + 7)/8)	/* alloc'ed groups */
#define LXT_LUNIDX_SHIFT  8	/* LXT entry, shift for LUN index */
#define LXT_PERM_SHIFT    4	/* LXT entry, shift for permission bits */

struct ba_lun_info {
	u64 *lun_alloc_map;
	u32 lun_bmap_size;
	u32 total_aus;
	u64 free_aun_cnt;

	/* indices to be used for elevator lookup of free map */
	u32 free_low_idx;
	u32 free_curr_idx;
	u32 free_high_idx;

	u8 *aun_clone_map;
};

struct ba_lun {
	u64 lun_id;
	u64 wwpn;
	size_t lsize;		/* LUN size in number of LBAs             */
	size_t lba_size;	/* LBA size in number of bytes            */
	size_t au_size;		/* Allocation Unit size in number of LBAs */
	struct ba_lun_info *ba_lun_handle;
};

/* Block Allocator */
struct blka {
	struct ba_lun ba_lun;
	u64 nchunk;		/* number of chunks */
	struct mutex mutex;
};

#endif /* ifndef _CXLFLASH_SUPERPIPE_H */
