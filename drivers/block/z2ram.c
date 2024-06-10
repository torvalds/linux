/*
** z2ram - Amiga pseudo-driver to access 16bit-RAM in ZorroII space
**         as a block device, to be used as a RAM disk or swap space
** 
** Copyright (C) 1994 by Ingo Wilken (Ingo.Wilken@informatik.uni-oldenburg.de)
**
** ++Geert: support for zorro_unused_z2ram, better range checking
** ++roman: translate accesses via an array
** ++Milan: support for ChipRAM usage
** ++yambo: converted to 2.0 kernel
** ++yambo: modularized and support added for 3 minor devices including:
**          MAJOR  MINOR  DESCRIPTION
**          -----  -----  ----------------------------------------------
**          37     0       Use Zorro II and Chip ram
**          37     1       Use only Zorro II ram
**          37     2       Use only Chip ram
**          37     4-7     Use memory list entry 1-4 (first is 0)
** ++jskov: support for 1-4th memory list entry.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#define DEVICE_NAME "Z2RAM"

#include <linux/major.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/blk-mq.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/pgtable.h>

#include <asm/setup.h>
#include <asm/amigahw.h>

#include <linux/zorro.h>

#define Z2MINOR_COMBINED      (0)
#define Z2MINOR_Z2ONLY        (1)
#define Z2MINOR_CHIPONLY      (2)
#define Z2MINOR_MEMLIST1      (4)
#define Z2MINOR_MEMLIST2      (5)
#define Z2MINOR_MEMLIST3      (6)
#define Z2MINOR_MEMLIST4      (7)
#define Z2MINOR_COUNT         (8)	/* Move this down when adding a new minor */

#define Z2RAM_CHUNK1024       ( Z2RAM_CHUNKSIZE >> 10 )

static DEFINE_MUTEX(z2ram_mutex);
static u_long *z2ram_map = NULL;
static u_long z2ram_size = 0;
static int z2_count = 0;
static int chip_count = 0;
static int list_count = 0;
static int current_device = -1;

static DEFINE_SPINLOCK(z2ram_lock);

static struct gendisk *z2ram_gendisk[Z2MINOR_COUNT];

static blk_status_t z2_queue_rq(struct blk_mq_hw_ctx *hctx,
				const struct blk_mq_queue_data *bd)
{
	struct request *req = bd->rq;
	unsigned long start = blk_rq_pos(req) << 9;
	unsigned long len = blk_rq_cur_bytes(req);

	blk_mq_start_request(req);

	if (start + len > z2ram_size) {
		pr_err(DEVICE_NAME ": bad access: block=%llu, "
		       "count=%u\n",
		       (unsigned long long)blk_rq_pos(req),
		       blk_rq_cur_sectors(req));
		return BLK_STS_IOERR;
	}

	spin_lock_irq(&z2ram_lock);

	while (len) {
		unsigned long addr = start & Z2RAM_CHUNKMASK;
		unsigned long size = Z2RAM_CHUNKSIZE - addr;
		void *buffer = bio_data(req->bio);

		if (len < size)
			size = len;
		addr += z2ram_map[start >> Z2RAM_CHUNKSHIFT];
		if (rq_data_dir(req) == READ)
			memcpy(buffer, (char *)addr, size);
		else
			memcpy((char *)addr, buffer, size);
		start += size;
		len -= size;
	}

	spin_unlock_irq(&z2ram_lock);
	blk_mq_end_request(req, BLK_STS_OK);
	return BLK_STS_OK;
}

static void get_z2ram(void)
{
	int i;

	for (i = 0; i < Z2RAM_SIZE / Z2RAM_CHUNKSIZE; i++) {
		if (test_bit(i, zorro_unused_z2ram)) {
			z2_count++;
			z2ram_map[z2ram_size++] =
			    (unsigned long)ZTWO_VADDR(Z2RAM_START) +
			    (i << Z2RAM_CHUNKSHIFT);
			clear_bit(i, zorro_unused_z2ram);
		}
	}

	return;
}

static void get_chipram(void)
{

	while (amiga_chip_avail() > (Z2RAM_CHUNKSIZE * 4)) {
		chip_count++;
		z2ram_map[z2ram_size] =
		    (u_long) amiga_chip_alloc(Z2RAM_CHUNKSIZE, "z2ram");

		if (z2ram_map[z2ram_size] == 0) {
			break;
		}

		z2ram_size++;
	}

	return;
}

static int z2_open(struct gendisk *disk, blk_mode_t mode)
{
	int device = disk->first_minor;
	int max_z2_map = (Z2RAM_SIZE / Z2RAM_CHUNKSIZE) * sizeof(z2ram_map[0]);
	int max_chip_map = (amiga_chip_size / Z2RAM_CHUNKSIZE) *
	    sizeof(z2ram_map[0]);
	int rc = -ENOMEM;

	mutex_lock(&z2ram_mutex);
	if (current_device != -1 && current_device != device) {
		rc = -EBUSY;
		goto err_out;
	}

	if (current_device == -1) {
		z2_count = 0;
		chip_count = 0;
		list_count = 0;
		z2ram_size = 0;

		/* Use a specific list entry. */
		if (device >= Z2MINOR_MEMLIST1 && device <= Z2MINOR_MEMLIST4) {
			int index = device - Z2MINOR_MEMLIST1 + 1;
			unsigned long size, paddr, vaddr;

			if (index >= m68k_realnum_memory) {
				printk(KERN_ERR DEVICE_NAME
				       ": no such entry in z2ram_map\n");
				goto err_out;
			}

			paddr = m68k_memory[index].addr;
			size = m68k_memory[index].size & ~(Z2RAM_CHUNKSIZE - 1);

#ifdef __powerpc__
			/* FIXME: ioremap doesn't build correct memory tables. */
			{
				vfree(vmalloc(size));
			}

			vaddr = (unsigned long)ioremap_wt(paddr, size);

#else
			vaddr =
			    (unsigned long)z_remap_nocache_nonser(paddr, size);
#endif
			z2ram_map =
			    kmalloc_array(size / Z2RAM_CHUNKSIZE,
					  sizeof(z2ram_map[0]), GFP_KERNEL);
			if (z2ram_map == NULL) {
				printk(KERN_ERR DEVICE_NAME
				       ": cannot get mem for z2ram_map\n");
				goto err_out;
			}

			while (size) {
				z2ram_map[z2ram_size++] = vaddr;
				size -= Z2RAM_CHUNKSIZE;
				vaddr += Z2RAM_CHUNKSIZE;
				list_count++;
			}

			if (z2ram_size != 0)
				printk(KERN_INFO DEVICE_NAME
				       ": using %iK List Entry %d Memory\n",
				       list_count * Z2RAM_CHUNK1024, index);
		} else
			switch (device) {
			case Z2MINOR_COMBINED:

				z2ram_map =
				    kmalloc(max_z2_map + max_chip_map,
					    GFP_KERNEL);
				if (z2ram_map == NULL) {
					printk(KERN_ERR DEVICE_NAME
					       ": cannot get mem for z2ram_map\n");
					goto err_out;
				}

				get_z2ram();
				get_chipram();

				if (z2ram_size != 0)
					printk(KERN_INFO DEVICE_NAME
					       ": using %iK Zorro II RAM and %iK Chip RAM (Total %dK)\n",
					       z2_count * Z2RAM_CHUNK1024,
					       chip_count * Z2RAM_CHUNK1024,
					       (z2_count +
						chip_count) * Z2RAM_CHUNK1024);

				break;

			case Z2MINOR_Z2ONLY:
				z2ram_map = kmalloc(max_z2_map, GFP_KERNEL);
				if (!z2ram_map)
					goto err_out;

				get_z2ram();

				if (z2ram_size != 0)
					printk(KERN_INFO DEVICE_NAME
					       ": using %iK of Zorro II RAM\n",
					       z2_count * Z2RAM_CHUNK1024);

				break;

			case Z2MINOR_CHIPONLY:
				z2ram_map = kmalloc(max_chip_map, GFP_KERNEL);
				if (!z2ram_map)
					goto err_out;

				get_chipram();

				if (z2ram_size != 0)
					printk(KERN_INFO DEVICE_NAME
					       ": using %iK Chip RAM\n",
					       chip_count * Z2RAM_CHUNK1024);

				break;

			default:
				rc = -ENODEV;
				goto err_out;

				break;
			}

		if (z2ram_size == 0) {
			printk(KERN_NOTICE DEVICE_NAME
			       ": no unused ZII/Chip RAM found\n");
			goto err_out_kfree;
		}

		current_device = device;
		z2ram_size <<= Z2RAM_CHUNKSHIFT;
		set_capacity(z2ram_gendisk[device], z2ram_size >> 9);
	}

	mutex_unlock(&z2ram_mutex);
	return 0;

err_out_kfree:
	kfree(z2ram_map);
err_out:
	mutex_unlock(&z2ram_mutex);
	return rc;
}

static void z2_release(struct gendisk *disk)
{
	mutex_lock(&z2ram_mutex);
	if (current_device == -1) {
		mutex_unlock(&z2ram_mutex);
		return;
	}
	mutex_unlock(&z2ram_mutex);
	/*
	 * FIXME: unmap memory
	 */
}

static const struct block_device_operations z2_fops = {
	.owner = THIS_MODULE,
	.open = z2_open,
	.release = z2_release,
};

static struct blk_mq_tag_set tag_set;

static const struct blk_mq_ops z2_mq_ops = {
	.queue_rq = z2_queue_rq,
};

static int z2ram_register_disk(int minor)
{
	struct gendisk *disk;
	int err;

	disk = blk_mq_alloc_disk(&tag_set, NULL, NULL);
	if (IS_ERR(disk))
		return PTR_ERR(disk);

	disk->major = Z2RAM_MAJOR;
	disk->first_minor = minor;
	disk->minors = 1;
	disk->flags |= GENHD_FL_NO_PART;
	disk->fops = &z2_fops;
	if (minor)
		sprintf(disk->disk_name, "z2ram%d", minor);
	else
		sprintf(disk->disk_name, "z2ram");

	z2ram_gendisk[minor] = disk;
	err = add_disk(disk);
	if (err)
		put_disk(disk);
	return err;
}

static int __init z2_init(void)
{
	int ret, i;

	if (!MACH_IS_AMIGA)
		return -ENODEV;

	if (register_blkdev(Z2RAM_MAJOR, DEVICE_NAME))
		return -EBUSY;

	tag_set.ops = &z2_mq_ops;
	tag_set.nr_hw_queues = 1;
	tag_set.nr_maps = 1;
	tag_set.queue_depth = 16;
	tag_set.numa_node = NUMA_NO_NODE;
	tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
	ret = blk_mq_alloc_tag_set(&tag_set);
	if (ret)
		goto out_unregister_blkdev;

	for (i = 0; i < Z2MINOR_COUNT; i++) {
		ret = z2ram_register_disk(i);
		if (ret && i == 0)
			goto out_free_tagset;
	}

	return 0;

out_free_tagset:
	blk_mq_free_tag_set(&tag_set);
out_unregister_blkdev:
	unregister_blkdev(Z2RAM_MAJOR, DEVICE_NAME);
	return ret;
}

static void __exit z2_exit(void)
{
	int i, j;

	unregister_blkdev(Z2RAM_MAJOR, DEVICE_NAME);

	for (i = 0; i < Z2MINOR_COUNT; i++) {
		del_gendisk(z2ram_gendisk[i]);
		put_disk(z2ram_gendisk[i]);
	}
	blk_mq_free_tag_set(&tag_set);

	if (current_device != -1) {
		i = 0;

		for (j = 0; j < z2_count; j++) {
			set_bit(i++, zorro_unused_z2ram);
		}

		for (j = 0; j < chip_count; j++) {
			if (z2ram_map[i]) {
				amiga_chip_free((void *)z2ram_map[i++]);
			}
		}

		if (z2ram_map != NULL) {
			kfree(z2ram_map);
		}
	}

	return;
}

module_init(z2_init);
module_exit(z2_exit);
MODULE_LICENSE("GPL");
