/*
 * block2mtd.c - create an mtd from a block device
 *
 * Copyright (C) 2001,2002	Simon Evans <spse@secret.org.uk>
 * Copyright (C) 2004-2006	Joern Engel <joern@wh.fh-wedel.de>
 *
 * Licence: GPL
 */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/pagemap.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/mtd/mtd.h>
#include <linux/buffer_head.h>
#include <linux/mutex.h>
#include <linux/mount.h>
#include <linux/slab.h>

#define ERROR(fmt, args...) printk(KERN_ERR "block2mtd: " fmt "\n" , ## args)
#define INFO(fmt, args...) printk(KERN_INFO "block2mtd: " fmt "\n" , ## args)


/* Info for the block device */
struct block2mtd_dev {
	struct list_head list;
	struct block_device *blkdev;
	struct mtd_info mtd;
	struct mutex write_mutex;
};


/* Static info about the MTD, used in cleanup_module */
static LIST_HEAD(blkmtd_device_list);


static struct page *page_read(struct address_space *mapping, int index)
{
	return read_mapping_page(mapping, index, NULL);
}

/* erase a specified part of the device */
static int _block2mtd_erase(struct block2mtd_dev *dev, loff_t to, size_t len)
{
	struct address_space *mapping = dev->blkdev->bd_inode->i_mapping;
	struct page *page;
	int index = to >> PAGE_SHIFT;	// page index
	int pages = len >> PAGE_SHIFT;
	u_long *p;
	u_long *max;

	while (pages) {
		page = page_read(mapping, index);
		if (!page)
			return -ENOMEM;
		if (IS_ERR(page))
			return PTR_ERR(page);

		max = page_address(page) + PAGE_SIZE;
		for (p=page_address(page); p<max; p++)
			if (*p != -1UL) {
				lock_page(page);
				memset(page_address(page), 0xff, PAGE_SIZE);
				set_page_dirty(page);
				unlock_page(page);
				break;
			}

		page_cache_release(page);
		pages--;
		index++;
	}
	return 0;
}
static int block2mtd_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct block2mtd_dev *dev = mtd->priv;
	size_t from = instr->addr;
	size_t len = instr->len;
	int err;

	instr->state = MTD_ERASING;
	mutex_lock(&dev->write_mutex);
	err = _block2mtd_erase(dev, from, len);
	mutex_unlock(&dev->write_mutex);
	if (err) {
		ERROR("erase failed err = %d", err);
		instr->state = MTD_ERASE_FAILED;
	} else
		instr->state = MTD_ERASE_DONE;

	mtd_erase_callback(instr);
	return err;
}


static int block2mtd_read(struct mtd_info *mtd, loff_t from, size_t len,
		size_t *retlen, u_char *buf)
{
	struct block2mtd_dev *dev = mtd->priv;
	struct page *page;
	int index = from >> PAGE_SHIFT;
	int offset = from & (PAGE_SIZE-1);
	int cpylen;

	if (from > mtd->size)
		return -EINVAL;
	if (from + len > mtd->size)
		len = mtd->size - from;

	if (retlen)
		*retlen = 0;

	while (len) {
		if ((offset + len) > PAGE_SIZE)
			cpylen = PAGE_SIZE - offset;	// multiple pages
		else
			cpylen = len;	// this page
		len = len - cpylen;

		page = page_read(dev->blkdev->bd_inode->i_mapping, index);
		if (!page)
			return -ENOMEM;
		if (IS_ERR(page))
			return PTR_ERR(page);

		memcpy(buf, page_address(page) + offset, cpylen);
		page_cache_release(page);

		if (retlen)
			*retlen += cpylen;
		buf += cpylen;
		offset = 0;
		index++;
	}
	return 0;
}


/* write data to the underlying device */
static int _block2mtd_write(struct block2mtd_dev *dev, const u_char *buf,
		loff_t to, size_t len, size_t *retlen)
{
	struct page *page;
	struct address_space *mapping = dev->blkdev->bd_inode->i_mapping;
	int index = to >> PAGE_SHIFT;	// page index
	int offset = to & ~PAGE_MASK;	// page offset
	int cpylen;

	if (retlen)
		*retlen = 0;
	while (len) {
		if ((offset+len) > PAGE_SIZE)
			cpylen = PAGE_SIZE - offset;	// multiple pages
		else
			cpylen = len;			// this page
		len = len - cpylen;

		page = page_read(mapping, index);
		if (!page)
			return -ENOMEM;
		if (IS_ERR(page))
			return PTR_ERR(page);

		if (memcmp(page_address(page)+offset, buf, cpylen)) {
			lock_page(page);
			memcpy(page_address(page) + offset, buf, cpylen);
			set_page_dirty(page);
			unlock_page(page);
		}
		page_cache_release(page);

		if (retlen)
			*retlen += cpylen;

		buf += cpylen;
		offset = 0;
		index++;
	}
	return 0;
}


static int block2mtd_write(struct mtd_info *mtd, loff_t to, size_t len,
		size_t *retlen, const u_char *buf)
{
	struct block2mtd_dev *dev = mtd->priv;
	int err;

	if (!len)
		return 0;
	if (to >= mtd->size)
		return -ENOSPC;
	if (to + len > mtd->size)
		len = mtd->size - to;

	mutex_lock(&dev->write_mutex);
	err = _block2mtd_write(dev, buf, to, len, retlen);
	mutex_unlock(&dev->write_mutex);
	if (err > 0)
		err = 0;
	return err;
}


/* sync the device - wait until the write queue is empty */
static void block2mtd_sync(struct mtd_info *mtd)
{
	struct block2mtd_dev *dev = mtd->priv;
	sync_blockdev(dev->blkdev);
	return;
}


static void block2mtd_free_device(struct block2mtd_dev *dev)
{
	if (!dev)
		return;

	kfree(dev->mtd.name);

	if (dev->blkdev) {
		invalidate_mapping_pages(dev->blkdev->bd_inode->i_mapping,
					0, -1);
		blkdev_put(dev->blkdev, FMODE_READ|FMODE_WRITE|FMODE_EXCL);
	}

	kfree(dev);
}


/* FIXME: ensure that mtd->size % erase_size == 0 */
static struct block2mtd_dev *add_device(char *devname, int erase_size)
{
	const fmode_t mode = FMODE_READ | FMODE_WRITE | FMODE_EXCL;
	struct block_device *bdev;
	struct block2mtd_dev *dev;
	char *name;

	if (!devname)
		return NULL;

	dev = kzalloc(sizeof(struct block2mtd_dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	/* Get a handle on the device */
	bdev = blkdev_get_by_path(devname, mode, dev);
#ifndef MODULE
	if (IS_ERR(bdev)) {

		/* We might not have rootfs mounted at this point. Try
		   to resolve the device name by other means. */

		dev_t devt = name_to_dev_t(devname);
		if (devt)
			bdev = blkdev_get_by_dev(devt, mode, dev);
	}
#endif

	if (IS_ERR(bdev)) {
		ERROR("error: cannot open device %s", devname);
		goto devinit_err;
	}
	dev->blkdev = bdev;

	if (MAJOR(bdev->bd_dev) == MTD_BLOCK_MAJOR) {
		ERROR("attempting to use an MTD device as a block device");
		goto devinit_err;
	}

	mutex_init(&dev->write_mutex);

	/* Setup the MTD structure */
	/* make the name contain the block device in */
	name = kasprintf(GFP_KERNEL, "block2mtd: %s", devname);
	if (!name)
		goto devinit_err;

	dev->mtd.name = name;

	dev->mtd.size = dev->blkdev->bd_inode->i_size & PAGE_MASK;
	dev->mtd.erasesize = erase_size;
	dev->mtd.writesize = 1;
	dev->mtd.type = MTD_RAM;
	dev->mtd.flags = MTD_CAP_RAM;
	dev->mtd.erase = block2mtd_erase;
	dev->mtd.write = block2mtd_write;
	dev->mtd.writev = default_mtd_writev;
	dev->mtd.sync = block2mtd_sync;
	dev->mtd.read = block2mtd_read;
	dev->mtd.priv = dev;
	dev->mtd.owner = THIS_MODULE;

	if (add_mtd_device(&dev->mtd)) {
		/* Device didnt get added, so free the entry */
		goto devinit_err;
	}
	list_add(&dev->list, &blkmtd_device_list);
	INFO("mtd%d: [%s] erase_size = %dKiB [%d]", dev->mtd.index,
			dev->mtd.name + strlen("block2mtd: "),
			dev->mtd.erasesize >> 10, dev->mtd.erasesize);
	return dev;

devinit_err:
	block2mtd_free_device(dev);
	return NULL;
}


/* This function works similar to reguler strtoul.  In addition, it
 * allows some suffixes for a more human-readable number format:
 * ki, Ki, kiB, KiB	- multiply result with 1024
 * Mi, MiB		- multiply result with 1024^2
 * Gi, GiB		- multiply result with 1024^3
 */
static int ustrtoul(const char *cp, char **endp, unsigned int base)
{
	unsigned long result = simple_strtoul(cp, endp, base);
	switch (**endp) {
	case 'G' :
		result *= 1024;
	case 'M':
		result *= 1024;
	case 'K':
	case 'k':
		result *= 1024;
	/* By dwmw2 editorial decree, "ki", "Mi" or "Gi" are to be used. */
		if ((*endp)[1] == 'i') {
			if ((*endp)[2] == 'B')
				(*endp) += 3;
			else
				(*endp) += 2;
		}
	}
	return result;
}


static int parse_num(size_t *num, const char *token)
{
	char *endp;
	size_t n;

	n = (size_t) ustrtoul(token, &endp, 0);
	if (*endp)
		return -EINVAL;

	*num = n;
	return 0;
}


static inline void kill_final_newline(char *str)
{
	char *newline = strrchr(str, '\n');
	if (newline && !newline[1])
		*newline = 0;
}


#define parse_err(fmt, args...) do {	\
	ERROR(fmt, ## args);		\
	return 0;			\
} while (0)

#ifndef MODULE
static int block2mtd_init_called = 0;
static char block2mtd_paramline[80 + 12]; /* 80 for device, 12 for erase size */
#endif


static int block2mtd_setup2(const char *val)
{
	char buf[80 + 12]; /* 80 for device, 12 for erase size */
	char *str = buf;
	char *token[2];
	char *name;
	size_t erase_size = PAGE_SIZE;
	int i, ret;

	if (strnlen(val, sizeof(buf)) >= sizeof(buf))
		parse_err("parameter too long");

	strcpy(str, val);
	kill_final_newline(str);

	for (i = 0; i < 2; i++)
		token[i] = strsep(&str, ",");

	if (str)
		parse_err("too many arguments");

	if (!token[0])
		parse_err("no argument");

	name = token[0];
	if (strlen(name) + 1 > 80)
		parse_err("device name too long");

	if (token[1]) {
		ret = parse_num(&erase_size, token[1]);
		if (ret) {
			parse_err("illegal erase size");
		}
	}

	add_device(name, erase_size);

	return 0;
}


static int block2mtd_setup(const char *val, struct kernel_param *kp)
{
#ifdef MODULE
	return block2mtd_setup2(val);
#else
	/* If more parameters are later passed in via
	   /sys/module/block2mtd/parameters/block2mtd
	   and block2mtd_init() has already been called,
	   we can parse the argument now. */

	if (block2mtd_init_called)
		return block2mtd_setup2(val);

	/* During early boot stage, we only save the parameters
	   here. We must parse them later: if the param passed
	   from kernel boot command line, block2mtd_setup() is
	   called so early that it is not possible to resolve
	   the device (even kmalloc() fails). Deter that work to
	   block2mtd_setup2(). */

	strlcpy(block2mtd_paramline, val, sizeof(block2mtd_paramline));

	return 0;
#endif
}


module_param_call(block2mtd, block2mtd_setup, NULL, NULL, 0200);
MODULE_PARM_DESC(block2mtd, "Device to use. \"block2mtd=<dev>[,<erasesize>]\"");

static int __init block2mtd_init(void)
{
	int ret = 0;

#ifndef MODULE
	if (strlen(block2mtd_paramline))
		ret = block2mtd_setup2(block2mtd_paramline);
	block2mtd_init_called = 1;
#endif

	return ret;
}


static void __devexit block2mtd_exit(void)
{
	struct list_head *pos, *next;

	/* Remove the MTD devices */
	list_for_each_safe(pos, next, &blkmtd_device_list) {
		struct block2mtd_dev *dev = list_entry(pos, typeof(*dev), list);
		block2mtd_sync(&dev->mtd);
		del_mtd_device(&dev->mtd);
		INFO("mtd%d: [%s] removed", dev->mtd.index,
				dev->mtd.name + strlen("block2mtd: "));
		list_del(&dev->list);
		block2mtd_free_device(dev);
	}
}


module_init(block2mtd_init);
module_exit(block2mtd_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joern Engel <joern@lazybastard.org>");
MODULE_DESCRIPTION("Emulate an MTD using a block device");
