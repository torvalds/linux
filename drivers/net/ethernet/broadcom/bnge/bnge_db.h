/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025 Broadcom */

#ifndef _BNGE_DB_H_
#define _BNGE_DB_H_

/* 64-bit doorbell */
#define DBR_EPOCH_SFT					24
#define DBR_TOGGLE_SFT					25
#define DBR_XID_SFT					32
#define DBR_PATH_L2					(0x1ULL << 56)
#define DBR_VALID					(0x1ULL << 58)
#define DBR_TYPE_SQ					(0x0ULL << 60)
#define DBR_TYPE_SRQ					(0x2ULL << 60)
#define DBR_TYPE_CQ					(0x4ULL << 60)
#define DBR_TYPE_CQ_ARMALL				(0x6ULL << 60)
#define DBR_TYPE_NQ					(0xaULL << 60)
#define DBR_TYPE_NQ_ARM					(0xbULL << 60)
#define DBR_TYPE_NQ_MASK				(0xeULL << 60)

struct bnge_db_info {
	void __iomem		*doorbell;
	u64			db_key64;
	u32			db_ring_mask;
	u32			db_epoch_mask;
	u8			db_epoch_shift;
};

#define DB_EPOCH(db, idx)	(((idx) & (db)->db_epoch_mask) <<	\
				 ((db)->db_epoch_shift))
#define DB_RING_IDX(db, idx)	(((idx) & (db)->db_ring_mask) |		\
				 DB_EPOCH(db, idx))

#endif /* _BNGE_DB_H_ */
