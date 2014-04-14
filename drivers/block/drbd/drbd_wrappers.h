#ifndef _DRBD_WRAPPERS_H
#define _DRBD_WRAPPERS_H

#include <linux/ctype.h>
#include <linux/mm.h>
#include "drbd_int.h"

/* see get_sb_bdev and bd_claim */
extern char *drbd_sec_holder;

/* sets the number of 512 byte sectors of our virtual device */
static inline void drbd_set_my_capacity(struct drbd_device *device,
					sector_t size)
{
	/* set_capacity(device->this_bdev->bd_disk, size); */
	set_capacity(device->vdisk, size);
	device->this_bdev->bd_inode->i_size = (loff_t)size << 9;
}

#define drbd_bio_uptodate(bio) bio_flagged(bio, BIO_UPTODATE)

/* bi_end_io handlers */
extern void drbd_md_io_complete(struct bio *bio, int error);
extern void drbd_peer_request_endio(struct bio *bio, int error);
extern void drbd_request_endio(struct bio *bio, int error);

/*
 * used to submit our private bio
 */
static inline void drbd_generic_make_request(struct drbd_device *device,
					     int fault_type, struct bio *bio)
{
	__release(local);
	if (!bio->bi_bdev) {
		printk(KERN_ERR "drbd%d: drbd_generic_make_request: "
				"bio->bi_bdev == NULL\n",
		       device_to_minor(device));
		dump_stack();
		bio_endio(bio, -ENODEV);
		return;
	}

	if (drbd_insert_fault(device, fault_type))
		bio_endio(bio, -EIO);
	else
		generic_make_request(bio);
}

#ifndef __CHECKER__
# undef __cond_lock
# define __cond_lock(x,c) (c)
#endif

#endif
