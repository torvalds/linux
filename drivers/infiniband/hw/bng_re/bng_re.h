/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (c) 2025 Broadcom.

#ifndef __BNG_RE_H__
#define __BNG_RE_H__

#include "bng_res.h"

#define BNG_RE_ADEV_NAME		"bng_en"

#define BNG_RE_DESC	"Broadcom 800G RoCE Driver"

#define	rdev_to_dev(rdev)	((rdev) ? (&(rdev)->ibdev.dev) : NULL)

#define BNG_RE_MIN_MSIX		2
#define BNG_RE_MAX_MSIX		BNGE_MAX_ROCE_MSIX

#define BNG_RE_CREQ_NQ_IDX	0

#define BNGE_INVALID_STATS_CTX_ID	-1
/* NQ specific structures  */
struct bng_re_nq_db {
	struct bng_re_reg_desc	reg;
	struct bng_re_db_info	dbinfo;
};

struct bng_re_nq {
	struct pci_dev			*pdev;
	struct bng_re_res		*res;
	char				*name;
	struct bng_re_hwq		hwq;
	struct bng_re_nq_db		nq_db;
	u16				ring_id;
	int				msix_vec;
	cpumask_t			mask;
	struct tasklet_struct		nq_tasklet;
	bool				requested;
	int				budget;
	u32				load;

	struct workqueue_struct		*cqn_wq;
};

struct bng_re_nq_record {
	struct bnge_msix_info	msix_entries[BNG_RE_MAX_MSIX];
	struct bng_re_nq	nq[BNG_RE_MAX_MSIX];
	int			num_msix;
	/* serialize NQ access */
	struct mutex		load_lock;
};

struct bng_re_en_dev_info {
	struct bng_re_dev *rdev;
	struct bnge_auxr_dev *auxr_dev;
};

struct bng_re_ring_attr {
	dma_addr_t	*dma_arr;
	int		pages;
	int		type;
	u32		depth;
	u32		lrid; /* Logical ring id */
	u8		mode;
};

struct bng_re_dev {
	struct ib_device		ibdev;
	unsigned long			flags;
#define BNG_RE_FLAG_NETDEV_REGISTERED		0
#define BNG_RE_FLAG_RCFW_CHANNEL_EN		1
	struct net_device		*netdev;
	struct auxiliary_device         *adev;
	struct bnge_auxr_dev		*aux_dev;
	struct bng_re_chip_ctx		*chip_ctx;
	int				fn_id;
	struct bng_re_res		bng_res;
	struct bng_re_rcfw		rcfw;
	struct bng_re_nq_record		*nqr;
	/* Device Resources */
	struct bng_re_dev_attr		*dev_attr;
	struct dentry			*dbg_root;
	struct bng_re_stats		stats_ctx;
};

#endif
