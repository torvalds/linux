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
	unsigned long bi_flags;
	struct bvec_iter bi_iter;
};

static inline void dm_bio_record(struct dm_bio_details *bd, struct bio *bio)
{
	bd->bi_disk = bio->bi_disk;
	bd->bi_partno = bio->bi_partno;
	bd->bi_flags = bio->bi_flags;
	bd->bi_iter = bio->bi_iter;
}

static inline void dm_bio_restore(struct dm_bio_details *bd, struct bio *bio)
{
	bio->bi_disk = bd->bi_disk;
	bio->bi_partno = bd->bi_partno;
	bio->bi_flags = bd->bi_flags;
	bio->bi_iter = bd->bi_iter;
}

#endif
