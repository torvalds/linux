/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2019-2021 Texas Instruments Incorporated - https://www.ti.com */
#ifndef __NET_TI_PRUSS_FDB_TBL_H
#define __NET_TI_PRUSS_FDB_TBL_H

#include <linux/kernel.h>
#include <linux/debugfs.h>
#include "icssm_prueth.h"

/* 4 bytes */
struct fdb_index_tbl_entry {
	/* Bucket Table index of first Bucket with this MAC address */
	u16 bucket_idx;
	u16 bucket_entries; /* Number of entries in this bucket */
};

/* 4 * 256 = 1024 = 0x200 bytes */
struct fdb_index_array {
	struct fdb_index_tbl_entry index_tbl_entry[FDB_INDEX_TBL_MAX_ENTRIES];
};

/* 10 bytes */
struct fdb_mac_tbl_entry {
	u8  mac[ETH_ALEN];
	u16 age;
	u8  port; /* 0 based: 0=port1, 1=port2 */
	union {
		struct {
			u8  is_static:1;
			u8  active:1;
		};
		u8 flags;
	};
};

/* 10 * 256 = 2560 = 0xa00 bytes */
struct fdb_mac_tbl_array {
	struct fdb_mac_tbl_entry mac_tbl_entry[FDB_MAC_TBL_MAX_ENTRIES];
};

/* 1 byte */
struct fdb_stp_config {
	u8  state; /* per-port STP state (defined in FW header) */
};

/* 1 byte */
struct fdb_flood_config {
	u8 host_flood_enable:1;
	u8 port1_flood_enable:1;
	u8 port2_flood_enable:1;
};

/* 2 byte */
struct fdb_arbitration {
	u8  host_lock;
	u8  pru_locks;
};

struct fdb_tbl {
	/* fdb index table */
	struct fdb_index_array __iomem *index_a;
	/* fdb MAC table */
	struct fdb_mac_tbl_array __iomem *mac_tbl_a;
	/* port 1 stp config */
	struct fdb_stp_config __iomem *port1_stp_cfg;
	/* port 2 stp config */
	struct fdb_stp_config __iomem *port2_stp_cfg;
	/* per-port flood enable */
	struct fdb_flood_config __iomem *flood_enable_flags;
	/* fdb locking mechanism */
	struct fdb_arbitration __iomem *locks;
	/* total number of entries in hash table */
	u16 total_entries;
};

#endif
