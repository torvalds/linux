// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RDMA Network Block Driver
 *
 * Copyright (c) 2014 - 2018 ProfitBricks GmbH. All rights reserved.
 * Copyright (c) 2018 - 2019 1&1 IONOS Cloud GmbH. All rights reserved.
 * Copyright (c) 2019 - 2020 1&1 IONOS SE. All rights reserved.
 */
#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME " L" __stringify(__LINE__) ": " fmt

#include "rnbd-srv-dev.h"
#include "rnbd-log.h"

struct rnbd_dev *rnbd_dev_open(const char *path, fmode_t flags,
			       struct bio_set *bs)
{
	struct rnbd_dev *dev;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	dev->blk_open_flags = flags;
	dev->bdev = blkdev_get_by_path(path, flags, THIS_MODULE);
	ret = PTR_ERR_OR_ZERO(dev->bdev);
	if (ret)
		goto err;

	dev->blk_open_flags = flags;
	bdevname(dev->bdev, dev->name);
	dev->ibd_bio_set = bs;

	return dev;

err:
	kfree(dev);
	return ERR_PTR(ret);
}

void rnbd_dev_close(struct rnbd_dev *dev)
{
	blkdev_put(dev->bdev, dev->blk_open_flags);
	kfree(dev);
}

void rnbd_dev_bi_end_io(struct bio *bio)
{
	struct rnbd_dev_blk_io *io = bio->bi_private;

	rnbd_endio(io->priv, blk_status_to_errno(bio->bi_status));
	bio_put(bio);
}

/**
 *	rnbd_bio_map_kern	-	map kernel address into bio
 *	@data: pointer to buffer to map
 *	@bs: bio_set to use.
 *	@len: length in bytes
 *	@gfp_mask: allocation flags for bio allocation
 *
 *	Map the kernel address into a bio suitable for io to a block
 *	device. Returns an error pointer in case of error.
 */
struct bio *rnbd_bio_map_kern(void *data, struct bio_set *bs,
			      unsigned int len, gfp_t gfp_mask)
{
	unsigned long kaddr = (unsigned long)data;
	unsigned long end = (kaddr + len + PAGE_SIZE - 1) >> PAGE_SHIFT;
	unsigned long start = kaddr >> PAGE_SHIFT;
	const int nr_pages = end - start;
	int offset, i;
	struct bio *bio;

	bio = bio_alloc_bioset(gfp_mask, nr_pages, bs);
	if (!bio)
		return ERR_PTR(-ENOMEM);

	offset = offset_in_page(kaddr);
	for (i = 0; i < nr_pages; i++) {
		unsigned int bytes = PAGE_SIZE - offset;

		if (len <= 0)
			break;

		if (bytes > len)
			bytes = len;

		if (bio_add_page(bio, virt_to_page(data), bytes,
				    offset) < bytes) {
			/* we don't support partial mappings */
			bio_put(bio);
			return ERR_PTR(-EINVAL);
		}

		data += bytes;
		len -= bytes;
		offset = 0;
	}

	return bio;
}
