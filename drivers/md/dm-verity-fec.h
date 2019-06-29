/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2015 Google, Inc.
 *
 * Author: Sami Tolvanen <samitolvanen@google.com>
 */

#ifndef DM_VERITY_FEC_H
#define DM_VERITY_FEC_H

#include "dm-verity.h"
#include <linux/rslib.h>

/* Reed-Solomon(M, N) parameters */
#define DM_VERITY_FEC_RSM		255
#define DM_VERITY_FEC_MAX_RSN		253
#define DM_VERITY_FEC_MIN_RSN		231	/* ~10% space overhead */

/* buffers for deinterleaving and decoding */
#define DM_VERITY_FEC_BUF_PREALLOC	1	/* buffers to preallocate */
#define DM_VERITY_FEC_BUF_RS_BITS	4	/* 1 << RS blocks per buffer */
/* we need buffers for at most 1 << block size RS blocks */
#define DM_VERITY_FEC_BUF_MAX \
	(1 << (PAGE_SHIFT - DM_VERITY_FEC_BUF_RS_BITS))

/* maximum recursion level for verity_fec_decode */
#define DM_VERITY_FEC_MAX_RECURSION	4

#define DM_VERITY_OPT_FEC_DEV		"use_fec_from_device"
#define DM_VERITY_OPT_FEC_BLOCKS	"fec_blocks"
#define DM_VERITY_OPT_FEC_START		"fec_start"
#define DM_VERITY_OPT_FEC_ROOTS		"fec_roots"

/* configuration */
struct dm_verity_fec {
	struct dm_dev *dev;	/* parity data device */
	struct dm_bufio_client *data_bufio;	/* for data dev access */
	struct dm_bufio_client *bufio;		/* for parity data access */
	sector_t start;		/* parity data start in blocks */
	sector_t blocks;	/* number of blocks covered */
	sector_t rounds;	/* number of interleaving rounds */
	sector_t hash_blocks;	/* blocks covered after v->hash_start */
	unsigned char roots;	/* number of parity bytes, M-N of RS(M, N) */
	unsigned char rsn;	/* N of RS(M, N) */
	mempool_t rs_pool;	/* mempool for fio->rs */
	mempool_t prealloc_pool;	/* mempool for preallocated buffers */
	mempool_t extra_pool;	/* mempool for extra buffers */
	mempool_t output_pool;	/* mempool for output */
	struct kmem_cache *cache;	/* cache for buffers */
};

/* per-bio data */
struct dm_verity_fec_io {
	struct rs_control *rs;	/* Reed-Solomon state */
	int erasures[DM_VERITY_FEC_MAX_RSN];	/* erasures for decode_rs8 */
	u8 *bufs[DM_VERITY_FEC_BUF_MAX];	/* bufs for deinterleaving */
	unsigned nbufs;		/* number of buffers allocated */
	u8 *output;		/* buffer for corrected output */
	size_t output_pos;
	unsigned level;		/* recursion level */
};

#ifdef CONFIG_DM_VERITY_FEC

/* each feature parameter requires a value */
#define DM_VERITY_OPTS_FEC	8

extern bool verity_fec_is_enabled(struct dm_verity *v);

extern int verity_fec_decode(struct dm_verity *v, struct dm_verity_io *io,
			     enum verity_block_type type, sector_t block,
			     u8 *dest, struct bvec_iter *iter);

extern unsigned verity_fec_status_table(struct dm_verity *v, unsigned sz,
					char *result, unsigned maxlen);

extern void verity_fec_finish_io(struct dm_verity_io *io);
extern void verity_fec_init_io(struct dm_verity_io *io);

extern bool verity_is_fec_opt_arg(const char *arg_name);
extern int verity_fec_parse_opt_args(struct dm_arg_set *as,
				     struct dm_verity *v, unsigned *argc,
				     const char *arg_name);

extern void verity_fec_dtr(struct dm_verity *v);

extern int verity_fec_ctr_alloc(struct dm_verity *v);
extern int verity_fec_ctr(struct dm_verity *v);

#else /* !CONFIG_DM_VERITY_FEC */

#define DM_VERITY_OPTS_FEC	0

static inline bool verity_fec_is_enabled(struct dm_verity *v)
{
	return false;
}

static inline int verity_fec_decode(struct dm_verity *v,
				    struct dm_verity_io *io,
				    enum verity_block_type type,
				    sector_t block, u8 *dest,
				    struct bvec_iter *iter)
{
	return -EOPNOTSUPP;
}

static inline unsigned verity_fec_status_table(struct dm_verity *v,
					       unsigned sz, char *result,
					       unsigned maxlen)
{
	return sz;
}

static inline void verity_fec_finish_io(struct dm_verity_io *io)
{
}

static inline void verity_fec_init_io(struct dm_verity_io *io)
{
}

static inline bool verity_is_fec_opt_arg(const char *arg_name)
{
	return false;
}

static inline int verity_fec_parse_opt_args(struct dm_arg_set *as,
					    struct dm_verity *v,
					    unsigned *argc,
					    const char *arg_name)
{
	return -EINVAL;
}

static inline void verity_fec_dtr(struct dm_verity *v)
{
}

static inline int verity_fec_ctr_alloc(struct dm_verity *v)
{
	return 0;
}

static inline int verity_fec_ctr(struct dm_verity *v)
{
	return 0;
}

#endif /* CONFIG_DM_VERITY_FEC */

#endif /* DM_VERITY_FEC_H */
