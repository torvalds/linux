#ifndef _DRBD_WRAPPERS_H
#define _DRBD_WRAPPERS_H

#include <linux/ctype.h>
#include <linux/mm.h>

/* see get_sb_bdev and bd_claim */
extern char *drbd_sec_holder;

/* sets the number of 512 byte sectors of our virtual device */
static inline void drbd_set_my_capacity(struct drbd_conf *mdev,
					sector_t size)
{
	/* set_capacity(mdev->this_bdev->bd_disk, size); */
	set_capacity(mdev->vdisk, size);
	mdev->this_bdev->bd_inode->i_size = (loff_t)size << 9;
}

#define drbd_bio_uptodate(bio) bio_flagged(bio, BIO_UPTODATE)

/* bi_end_io handlers */
extern void drbd_md_io_complete(struct bio *bio, int error);
extern void drbd_endio_sec(struct bio *bio, int error);
extern void drbd_endio_pri(struct bio *bio, int error);

/*
 * used to submit our private bio
 */
static inline void drbd_generic_make_request(struct drbd_conf *mdev,
					     int fault_type, struct bio *bio)
{
	__release(local);
	if (!bio->bi_bdev) {
		printk(KERN_ERR "drbd%d: drbd_generic_make_request: "
				"bio->bi_bdev == NULL\n",
		       mdev_to_minor(mdev));
		dump_stack();
		bio_endio(bio, -ENODEV);
		return;
	}

	if (drbd_insert_fault(mdev, fault_type))
		bio_endio(bio, -EIO);
	else
		generic_make_request(bio);
}

static inline int drbd_crypto_is_hash(struct crypto_tfm *tfm)
{
        return (crypto_tfm_alg_type(tfm) & CRYPTO_ALG_TYPE_HASH_MASK)
                == CRYPTO_ALG_TYPE_HASH;
}

#ifndef __CHECKER__
# undef __cond_lock
# define __cond_lock(x,c) (c)
#endif

#endif
