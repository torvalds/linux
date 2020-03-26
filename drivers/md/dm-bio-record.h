/*
 * Copyright (C) 2004-2005 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 */

#ifndef DM_BIO_RECORD_H
#define DM_BIO_RECORD_H

#include <linux/bio.h>

/*
 * There are lots of mutable fields in the bio struct that get
 * changed by the lower levels of the block layer.  Some targets,
 * such as multipath, may wish to resubmit a bio on error.  The
 * functions in this file help the target record and restore the
 * original bio state.
 */

struct dm_bio_details {
	struct gendisk *bi_disk;
	u8 bi_partno;
	int __bi_remaining;
	unsigned long bi_flags;
	struct bvec_iter bi_iter;
	bio_end_io_t *bi_end_io;
#if defined(CONFIG_BLK_DEV_INTEGRITY)
	struct bio_integrity_payload *bi_integrity;
#endif
};

static inline void dm_bio_record(struct dm_bio_details *bd, struct bio *bio)
{
	bd->bi_disk = bio->bi_disk;
	bd->bi_partno = bio->bi_partno;
	bd->bi_flags = bio->bi_flags;
	bd->bi_iter = bio->bi_iter;
	bd->__bi_remaining = atomic_read(&bio->__bi_remaining);
	bd->bi_end_io = bio->bi_end_io;
#if defined(CONFIG_BLK_DEV_INTEGRITY)
	bd->bi_integrity = bio_integrity(bio);
#endif
}

static inline void dm_bio_restore(struct dm_bio_details *bd, struct bio *bio)
{
	bio->bi_disk = bd->bi_disk;
	bio->bi_partno = bd->bi_partno;
	bio->bi_flags = bd->bi_flags;
	bio->bi_iter = bd->bi_iter;
	atomic_set(&bio->__bi_remaining, bd->__bi_remaining);
	bio->bi_end_io = bd->bi_end_io;
#if defined(CONFIG_BLK_DEV_INTEGRITY)
	bio->bi_integrity = bd->bi_integrity;
#endif
}

#endif
