/*
 * dcssblk.c -- the S/390 block driver for dcss memory
 *
 * Authors: Carsten Otte, Stefan Weinhuber, Gerald Schaefer
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <asm/extmem.h>
#include <asm/io.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <asm/s390_rdev.h>

//#define DCSSBLK_DEBUG		/* Debug messages on/off */
#define DCSSBLK_NAME "dcssblk"
#define DCSSBLK_MINORS_PER_DISK 1
#define DCSSBLK_PARM_LEN 400

#ifdef DCSSBLK_DEBUG
#define PRINT_DEBUG(x...) printk(KERN_DEBUG DCSSBLK_NAME " debug: " x)
#else
#define PRINT_DEBUG(x...) do {} while (0)
#endif
#define PRINT_INFO(x...)  printk(KERN_INFO DCSSBLK_NAME " info: " x)
#define PRINT_WARN(x...)  printk(KERN_WARNING DCSSBLK_NAME " warning: " x)
#define PRINT_ERR(x...)	  printk(KERN_ERR DCSSBLK_NAME " error: " x)


static int dcssblk_open(struct inode *inode, struct file *filp);
static int dcssblk_release(struct inode *inode, struct file *filp);
static int dcssblk_make_request(struct request_queue *q, struct bio *bio);
static int dcssblk_direct_access(struct block_device *bdev, sector_t secnum,
				 void **kaddr, unsigned long *pfn);

static char dcssblk_segments[DCSSBLK_PARM_LEN] = "\0";

static int dcssblk_major;
static struct block_device_operations dcssblk_devops = {
	.owner   	= THIS_MODULE,
	.open    	= dcssblk_open,
	.release 	= dcssblk_release,
	.direct_access 	= dcssblk_direct_access,
};

static ssize_t dcssblk_add_store(struct device * dev, struct device_attribute *attr, const char * buf,
				  size_t count);
static ssize_t dcssblk_remove_store(struct device * dev, struct device_attribute *attr, const char * buf,
				  size_t count);
static ssize_t dcssblk_save_store(struct device * dev, struct device_attribute *attr, const char * buf,
				  size_t count);
static ssize_t dcssblk_save_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t dcssblk_shared_store(struct device * dev, struct device_attribute *attr, const char * buf,
				  size_t count);
static ssize_t dcssblk_shared_show(struct device *dev, struct device_attribute *attr, char *buf);

static DEVICE_ATTR(add, S_IWUSR, NULL, dcssblk_add_store);
static DEVICE_ATTR(remove, S_IWUSR, NULL, dcssblk_remove_store);
static DEVICE_ATTR(save, S_IWUSR | S_IRUGO, dcssblk_save_show,
		   dcssblk_save_store);
static DEVICE_ATTR(shared, S_IWUSR | S_IRUGO, dcssblk_shared_show,
		   dcssblk_shared_store);

static struct device *dcssblk_root_dev;

struct dcssblk_dev_info {
	struct list_head lh;
	struct device dev;
	char segment_name[BUS_ID_SIZE];
	atomic_t use_count;
	struct gendisk *gd;
	unsigned long start;
	unsigned long end;
	int segment_type;
	unsigned char save_pending;
	unsigned char is_shared;
	struct request_queue *dcssblk_queue;
};

static LIST_HEAD(dcssblk_devices);
static struct rw_semaphore dcssblk_devices_sem;

/*
 * release function for segment device.
 */
static void
dcssblk_release_segment(struct device *dev)
{
	PRINT_DEBUG("segment release fn called for %s\n", dev->bus_id);
	kfree(container_of(dev, struct dcssblk_dev_info, dev));
	module_put(THIS_MODULE);
}

/*
 * get a minor number. needs to be called with
 * down_write(&dcssblk_devices_sem) and the
 * device needs to be enqueued before the semaphore is
 * freed.
 */
static int
dcssblk_assign_free_minor(struct dcssblk_dev_info *dev_info)
{
	int minor, found;
	struct dcssblk_dev_info *entry;

	if (dev_info == NULL)
		return -EINVAL;
	for (minor = 0; minor < (1<<MINORBITS); minor++) {
		found = 0;
		// test if minor available
		list_for_each_entry(entry, &dcssblk_devices, lh)
			if (minor == entry->gd->first_minor)
				found++;
		if (!found) break; // got unused minor
	}
	if (found)
		return -EBUSY;
	dev_info->gd->first_minor = minor;
	return 0;
}

/*
 * get the struct dcssblk_dev_info from dcssblk_devices
 * for the given name.
 * down_read(&dcssblk_devices_sem) must be held.
 */
static struct dcssblk_dev_info *
dcssblk_get_device_by_name(char *name)
{
	struct dcssblk_dev_info *entry;

	list_for_each_entry(entry, &dcssblk_devices, lh) {
		if (!strcmp(name, entry->segment_name)) {
			return entry;
		}
	}
	return NULL;
}

static void dcssblk_unregister_callback(struct device *dev)
{
	device_unregister(dev);
	put_device(dev);
}

/*
 * device attribute for switching shared/nonshared (exclusive)
 * operation (show + store)
 */
static ssize_t
dcssblk_shared_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dcssblk_dev_info *dev_info;

	dev_info = container_of(dev, struct dcssblk_dev_info, dev);
	return sprintf(buf, dev_info->is_shared ? "1\n" : "0\n");
}

static ssize_t
dcssblk_shared_store(struct device *dev, struct device_attribute *attr, const char *inbuf, size_t count)
{
	struct dcssblk_dev_info *dev_info;
	int rc;

	if ((count > 1) && (inbuf[1] != '\n') && (inbuf[1] != '\0')) {
		PRINT_WARN("Invalid value, must be 0 or 1\n");
		return -EINVAL;
	}
	down_write(&dcssblk_devices_sem);
	dev_info = container_of(dev, struct dcssblk_dev_info, dev);
	if (atomic_read(&dev_info->use_count)) {
		PRINT_ERR("share: segment %s is busy!\n",
			  dev_info->segment_name);
		rc = -EBUSY;
		goto out;
	}
	if (inbuf[0] == '1') {
		// reload segment in shared mode
		rc = segment_modify_shared(dev_info->segment_name,
					   SEGMENT_SHARED);
		if (rc < 0) {
			BUG_ON(rc == -EINVAL);
			if (rc != -EAGAIN)
				goto removeseg;
		} else {
			dev_info->is_shared = 1;
			switch (dev_info->segment_type) {
				case SEG_TYPE_SR:
				case SEG_TYPE_ER:
				case SEG_TYPE_SC:
					set_disk_ro(dev_info->gd,1);
			}
		}
	} else if (inbuf[0] == '0') {
		// reload segment in exclusive mode
		if (dev_info->segment_type == SEG_TYPE_SC) {
			PRINT_ERR("Segment type SC (%s) cannot be loaded in "
				  "non-shared mode\n", dev_info->segment_name);
			rc = -EINVAL;
			goto out;
		}
		rc = segment_modify_shared(dev_info->segment_name,
					   SEGMENT_EXCLUSIVE);
		if (rc < 0) {
			BUG_ON(rc == -EINVAL);
			if (rc != -EAGAIN)
				goto removeseg;
		} else {
			dev_info->is_shared = 0;
			set_disk_ro(dev_info->gd, 0);
		}
	} else {
		PRINT_WARN("Invalid value, must be 0 or 1\n");
		rc = -EINVAL;
		goto out;
	}
	rc = count;
	goto out;

removeseg:
	PRINT_ERR("Could not reload segment %s, removing it now!\n",
			dev_info->segment_name);
	list_del(&dev_info->lh);

	del_gendisk(dev_info->gd);
	blk_cleanup_queue(dev_info->dcssblk_queue);
	dev_info->gd->queue = NULL;
	put_disk(dev_info->gd);
	rc = device_schedule_callback(dev, dcssblk_unregister_callback);
out:
	up_write(&dcssblk_devices_sem);
	return rc;
}

/*
 * device attribute for save operation on current copy
 * of the segment. If the segment is busy, saving will
 * become pending until it gets released, which can be
 * undone by storing a non-true value to this entry.
 * (show + store)
 */
static ssize_t
dcssblk_save_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dcssblk_dev_info *dev_info;

	dev_info = container_of(dev, struct dcssblk_dev_info, dev);
	return sprintf(buf, dev_info->save_pending ? "1\n" : "0\n");
}

static ssize_t
dcssblk_save_store(struct device *dev, struct device_attribute *attr, const char *inbuf, size_t count)
{
	struct dcssblk_dev_info *dev_info;

	if ((count > 1) && (inbuf[1] != '\n') && (inbuf[1] != '\0')) {
		PRINT_WARN("Invalid value, must be 0 or 1\n");
		return -EINVAL;
	}
	dev_info = container_of(dev, struct dcssblk_dev_info, dev);

	down_write(&dcssblk_devices_sem);
	if (inbuf[0] == '1') {
		if (atomic_read(&dev_info->use_count) == 0) {
			// device is idle => we save immediately
			PRINT_INFO("Saving segment %s\n",
				   dev_info->segment_name);
			segment_save(dev_info->segment_name);
		}  else {
			// device is busy => we save it when it becomes
			// idle in dcssblk_release
			PRINT_INFO("Segment %s is currently busy, it will "
				   "be saved when it becomes idle...\n",
				   dev_info->segment_name);
			dev_info->save_pending = 1;
		}
	} else if (inbuf[0] == '0') {
		if (dev_info->save_pending) {
			// device is busy & the user wants to undo his save
			// request
			dev_info->save_pending = 0;
			PRINT_INFO("Pending save for segment %s deactivated\n",
					dev_info->segment_name);
		}
	} else {
		up_write(&dcssblk_devices_sem);
		PRINT_WARN("Invalid value, must be 0 or 1\n");
		return -EINVAL;
	}
	up_write(&dcssblk_devices_sem);
	return count;
}

/*
 * device attribute for adding devices
 */
static ssize_t
dcssblk_add_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int rc, i;
	struct dcssblk_dev_info *dev_info;
	char *local_buf;
	unsigned long seg_byte_size;

	dev_info = NULL;
	if (dev != dcssblk_root_dev) {
		rc = -EINVAL;
		goto out_nobuf;
	}
	local_buf = kmalloc(count + 1, GFP_KERNEL);
	if (local_buf == NULL) {
		rc = -ENOMEM;
		goto out_nobuf;
	}
	/*
	 * parse input
	 */
	for (i = 0; ((buf[i] != '\0') && (buf[i] != '\n') && i < count); i++) {
		local_buf[i] = toupper(buf[i]);
	}
	local_buf[i] = '\0';
	if ((i == 0) || (i > 8)) {
		rc = -ENAMETOOLONG;
		goto out;
	}
	/*
	 * already loaded?
	 */
	down_read(&dcssblk_devices_sem);
	dev_info = dcssblk_get_device_by_name(local_buf);
	up_read(&dcssblk_devices_sem);
	if (dev_info != NULL) {
		PRINT_WARN("Segment %s already loaded!\n", local_buf);
		rc = -EEXIST;
		goto out;
	}
	/*
	 * get a struct dcssblk_dev_info
	 */
	dev_info = kzalloc(sizeof(struct dcssblk_dev_info), GFP_KERNEL);
	if (dev_info == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	strcpy(dev_info->segment_name, local_buf);
	strlcpy(dev_info->dev.bus_id, local_buf, BUS_ID_SIZE);
	dev_info->dev.release = dcssblk_release_segment;
	INIT_LIST_HEAD(&dev_info->lh);

	dev_info->gd = alloc_disk(DCSSBLK_MINORS_PER_DISK);
	if (dev_info->gd == NULL) {
		rc = -ENOMEM;
		goto free_dev_info;
	}
	dev_info->gd->major = dcssblk_major;
	dev_info->gd->fops = &dcssblk_devops;
	dev_info->dcssblk_queue = blk_alloc_queue(GFP_KERNEL);
	dev_info->gd->queue = dev_info->dcssblk_queue;
	dev_info->gd->private_data = dev_info;
	dev_info->gd->driverfs_dev = &dev_info->dev;
	blk_queue_make_request(dev_info->dcssblk_queue, dcssblk_make_request);
	blk_queue_hardsect_size(dev_info->dcssblk_queue, 4096);
	/*
	 * load the segment
	 */
	rc = segment_load(local_buf, SEGMENT_SHARED,
				&dev_info->start, &dev_info->end);
	if (rc < 0) {
		segment_warning(rc, dev_info->segment_name);
		goto dealloc_gendisk;
	}
	seg_byte_size = (dev_info->end - dev_info->start + 1);
	set_capacity(dev_info->gd, seg_byte_size >> 9); // size in sectors
	PRINT_INFO("Loaded segment %s, size = %lu Byte, "
		   "capacity = %lu (512 Byte) sectors\n", local_buf,
		   seg_byte_size, seg_byte_size >> 9);

	dev_info->segment_type = rc;
	dev_info->save_pending = 0;
	dev_info->is_shared = 1;
	dev_info->dev.parent = dcssblk_root_dev;

	/*
	 * get minor, add to list
	 */
	down_write(&dcssblk_devices_sem);
	rc = dcssblk_assign_free_minor(dev_info);
	if (rc) {
		up_write(&dcssblk_devices_sem);
		PRINT_ERR("No free minor number available! "
			  "Unloading segment...\n");
		goto unload_seg;
	}
	sprintf(dev_info->gd->disk_name, "dcssblk%d",
		dev_info->gd->first_minor);
	list_add_tail(&dev_info->lh, &dcssblk_devices);

	if (!try_module_get(THIS_MODULE)) {
		rc = -ENODEV;
		goto list_del;
	}
	/*
	 * register the device
	 */
	rc = device_register(&dev_info->dev);
	if (rc) {
		PRINT_ERR("Segment %s could not be registered RC=%d\n",
				local_buf, rc);
		module_put(THIS_MODULE);
		goto list_del;
	}
	get_device(&dev_info->dev);
	rc = device_create_file(&dev_info->dev, &dev_attr_shared);
	if (rc)
		goto unregister_dev;
	rc = device_create_file(&dev_info->dev, &dev_attr_save);
	if (rc)
		goto unregister_dev;

	add_disk(dev_info->gd);

	switch (dev_info->segment_type) {
		case SEG_TYPE_SR:
		case SEG_TYPE_ER:
		case SEG_TYPE_SC:
			set_disk_ro(dev_info->gd,1);
			break;
		default:
			set_disk_ro(dev_info->gd,0);
			break;
	}
	PRINT_DEBUG("Segment %s loaded successfully\n", local_buf);
	up_write(&dcssblk_devices_sem);
	rc = count;
	goto out;

unregister_dev:
	PRINT_ERR("device_create_file() failed!\n");
	list_del(&dev_info->lh);
	blk_cleanup_queue(dev_info->dcssblk_queue);
	dev_info->gd->queue = NULL;
	put_disk(dev_info->gd);
	device_unregister(&dev_info->dev);
	segment_unload(dev_info->segment_name);
	put_device(&dev_info->dev);
	up_write(&dcssblk_devices_sem);
	goto out;
list_del:
	list_del(&dev_info->lh);
	up_write(&dcssblk_devices_sem);
unload_seg:
	segment_unload(local_buf);
dealloc_gendisk:
	blk_cleanup_queue(dev_info->dcssblk_queue);
	dev_info->gd->queue = NULL;
	put_disk(dev_info->gd);
free_dev_info:
	kfree(dev_info);
out:
	kfree(local_buf);
out_nobuf:
	return rc;
}

/*
 * device attribute for removing devices
 */
static ssize_t
dcssblk_remove_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct dcssblk_dev_info *dev_info;
	int rc, i;
	char *local_buf;

	if (dev != dcssblk_root_dev) {
		return -EINVAL;
	}
	local_buf = kmalloc(count + 1, GFP_KERNEL);
	if (local_buf == NULL) {
		return -ENOMEM;
	}
	/*
	 * parse input
	 */
	for (i = 0; ((*(buf+i)!='\0') && (*(buf+i)!='\n') && i < count); i++) {
		local_buf[i] = toupper(buf[i]);
	}
	local_buf[i] = '\0';
	if ((i == 0) || (i > 8)) {
		rc = -ENAMETOOLONG;
		goto out_buf;
	}

	down_write(&dcssblk_devices_sem);
	dev_info = dcssblk_get_device_by_name(local_buf);
	if (dev_info == NULL) {
		up_write(&dcssblk_devices_sem);
		PRINT_WARN("Segment %s is not loaded!\n", local_buf);
		rc = -ENODEV;
		goto out_buf;
	}
	if (atomic_read(&dev_info->use_count) != 0) {
		up_write(&dcssblk_devices_sem);
		PRINT_WARN("Segment %s is in use!\n", local_buf);
		rc = -EBUSY;
		goto out_buf;
	}
	list_del(&dev_info->lh);

	del_gendisk(dev_info->gd);
	blk_cleanup_queue(dev_info->dcssblk_queue);
	dev_info->gd->queue = NULL;
	put_disk(dev_info->gd);
	device_unregister(&dev_info->dev);
	segment_unload(dev_info->segment_name);
	PRINT_DEBUG("Segment %s unloaded successfully\n",
			dev_info->segment_name);
	put_device(&dev_info->dev);
	up_write(&dcssblk_devices_sem);

	rc = count;
out_buf:
	kfree(local_buf);
	return rc;
}

static int
dcssblk_open(struct inode *inode, struct file *filp)
{
	struct dcssblk_dev_info *dev_info;
	int rc;

	dev_info = inode->i_bdev->bd_disk->private_data;
	if (NULL == dev_info) {
		rc = -ENODEV;
		goto out;
	}
	atomic_inc(&dev_info->use_count);
	inode->i_bdev->bd_block_size = 4096;
	rc = 0;
out:
	return rc;
}

static int
dcssblk_release(struct inode *inode, struct file *filp)
{
	struct dcssblk_dev_info *dev_info;
	int rc;

	dev_info = inode->i_bdev->bd_disk->private_data;
	if (NULL == dev_info) {
		rc = -ENODEV;
		goto out;
	}
	down_write(&dcssblk_devices_sem);
	if (atomic_dec_and_test(&dev_info->use_count)
	    && (dev_info->save_pending)) {
		PRINT_INFO("Segment %s became idle and is being saved now\n",
			    dev_info->segment_name);
		segment_save(dev_info->segment_name);
		dev_info->save_pending = 0;
	}
	up_write(&dcssblk_devices_sem);
	rc = 0;
out:
	return rc;
}

static int
dcssblk_make_request(struct request_queue *q, struct bio *bio)
{
	struct dcssblk_dev_info *dev_info;
	struct bio_vec *bvec;
	unsigned long index;
	unsigned long page_addr;
	unsigned long source_addr;
	unsigned long bytes_done;
	int i;

	bytes_done = 0;
	dev_info = bio->bi_bdev->bd_disk->private_data;
	if (dev_info == NULL)
		goto fail;
	if ((bio->bi_sector & 7) != 0 || (bio->bi_size & 4095) != 0)
		/* Request is not page-aligned. */
		goto fail;
	if (((bio->bi_size >> 9) + bio->bi_sector)
			> get_capacity(bio->bi_bdev->bd_disk)) {
		/* Request beyond end of DCSS segment. */
		goto fail;
	}
	/* verify data transfer direction */
	if (dev_info->is_shared) {
		switch (dev_info->segment_type) {
		case SEG_TYPE_SR:
		case SEG_TYPE_ER:
		case SEG_TYPE_SC:
			/* cannot write to these segments */
			if (bio_data_dir(bio) == WRITE) {
				PRINT_WARN("rejecting write to ro segment %s\n", dev_info->dev.bus_id);
				goto fail;
			}
		}
	}

	index = (bio->bi_sector >> 3);
	bio_for_each_segment(bvec, bio, i) {
		page_addr = (unsigned long)
			page_address(bvec->bv_page) + bvec->bv_offset;
		source_addr = dev_info->start + (index<<12) + bytes_done;
		if (unlikely((page_addr & 4095) != 0) || (bvec->bv_len & 4095) != 0)
			// More paranoia.
			goto fail;
		if (bio_data_dir(bio) == READ) {
			memcpy((void*)page_addr, (void*)source_addr,
				bvec->bv_len);
		} else {
			memcpy((void*)source_addr, (void*)page_addr,
				bvec->bv_len);
		}
		bytes_done += bvec->bv_len;
	}
	bio_endio(bio, 0);
	return 0;
fail:
	bio_io_error(bio);
	return 0;
}

static int
dcssblk_direct_access (struct block_device *bdev, sector_t secnum,
			void **kaddr, unsigned long *pfn)
{
	struct dcssblk_dev_info *dev_info;
	unsigned long pgoff;

	dev_info = bdev->bd_disk->private_data;
	if (!dev_info)
		return -ENODEV;
	if (secnum % (PAGE_SIZE/512))
		return -EINVAL;
	pgoff = secnum / (PAGE_SIZE / 512);
	if ((pgoff+1)*PAGE_SIZE-1 > dev_info->end - dev_info->start)
		return -ERANGE;
	*kaddr = (void *) (dev_info->start+pgoff*PAGE_SIZE);
	*pfn = virt_to_phys(*kaddr) >> PAGE_SHIFT;

	return 0;
}

static void
dcssblk_check_params(void)
{
	int rc, i, j, k;
	char buf[9];
	struct dcssblk_dev_info *dev_info;

	for (i = 0; (i < DCSSBLK_PARM_LEN) && (dcssblk_segments[i] != '\0');
	     i++) {
		for (j = i; (dcssblk_segments[j] != ',')  &&
			    (dcssblk_segments[j] != '\0') &&
			    (dcssblk_segments[j] != '(')  &&
			    (j - i) < 8; j++)
		{
			buf[j-i] = dcssblk_segments[j];
		}
		buf[j-i] = '\0';
		rc = dcssblk_add_store(dcssblk_root_dev, NULL, buf, j-i);
		if ((rc >= 0) && (dcssblk_segments[j] == '(')) {
			for (k = 0; buf[k] != '\0'; k++)
				buf[k] = toupper(buf[k]);
			if (!strncmp(&dcssblk_segments[j], "(local)", 7)) {
				down_read(&dcssblk_devices_sem);
				dev_info = dcssblk_get_device_by_name(buf);
				up_read(&dcssblk_devices_sem);
				if (dev_info)
					dcssblk_shared_store(&dev_info->dev,
							     NULL, "0\n", 2);
			}
		}
		while ((dcssblk_segments[j] != ',') &&
		       (dcssblk_segments[j] != '\0'))
		{
			j++;
		}
		if (dcssblk_segments[j] == '\0')
			break;
		i = j;
	}
}

/*
 * The init/exit functions.
 */
static void __exit
dcssblk_exit(void)
{
	PRINT_DEBUG("DCSSBLOCK EXIT...\n");
	s390_root_dev_unregister(dcssblk_root_dev);
	unregister_blkdev(dcssblk_major, DCSSBLK_NAME);
	PRINT_DEBUG("...finished!\n");
}

static int __init
dcssblk_init(void)
{
	int rc;

	PRINT_DEBUG("DCSSBLOCK INIT...\n");
	dcssblk_root_dev = s390_root_dev_register("dcssblk");
	if (IS_ERR(dcssblk_root_dev)) {
		PRINT_ERR("device_register() failed!\n");
		return PTR_ERR(dcssblk_root_dev);
	}
	rc = device_create_file(dcssblk_root_dev, &dev_attr_add);
	if (rc) {
		PRINT_ERR("device_create_file(add) failed!\n");
		s390_root_dev_unregister(dcssblk_root_dev);
		return rc;
	}
	rc = device_create_file(dcssblk_root_dev, &dev_attr_remove);
	if (rc) {
		PRINT_ERR("device_create_file(remove) failed!\n");
		s390_root_dev_unregister(dcssblk_root_dev);
		return rc;
	}
	rc = register_blkdev(0, DCSSBLK_NAME);
	if (rc < 0) {
		PRINT_ERR("Can't get dynamic major!\n");
		s390_root_dev_unregister(dcssblk_root_dev);
		return rc;
	}
	dcssblk_major = rc;
	init_rwsem(&dcssblk_devices_sem);

	dcssblk_check_params();

	PRINT_DEBUG("...finished!\n");
	return 0;
}

module_init(dcssblk_init);
module_exit(dcssblk_exit);

module_param_string(segments, dcssblk_segments, DCSSBLK_PARM_LEN, 0444);
MODULE_PARM_DESC(segments, "Name of DCSS segment(s) to be loaded, "
		 "comma-separated list, each name max. 8 chars.\n"
		 "Adding \"(local)\" to segment name equals echoing 0 to "
		 "/sys/devices/dcssblk/<segment name>/shared after loading "
		 "the segment - \n"
		 "e.g. segments=\"mydcss1,mydcss2,mydcss3(local)\"");

MODULE_LICENSE("GPL");
