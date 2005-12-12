/*
 * $Id: blkmtd.c,v 1.27 2005/11/07 11:14:24 gleixner Exp $
 *
 * blkmtd.c - use a block device as a fake MTD
 *
 * Author: Simon Evans <spse@secret.org.uk>
 *
 * Copyright (C) 2001,2002 Simon Evans
 *
 * Licence: GPL
 *
 * How it works:
 *	The driver uses raw/io to read/write the device and the page
 *	cache to cache access. Writes update the page cache with the
 *	new data and mark it dirty and add the page into a BIO which
 *	is then written out.
 *
 *	It can be loaded Read-Only to prevent erases and writes to the
 *	medium.
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/pagemap.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/mtd/mtd.h>


#define err(format, arg...) printk(KERN_ERR "blkmtd: " format "\n" , ## arg)
#define info(format, arg...) printk(KERN_INFO "blkmtd: " format "\n" , ## arg)
#define warn(format, arg...) printk(KERN_WARNING "blkmtd: " format "\n" , ## arg)
#define crit(format, arg...) printk(KERN_CRIT "blkmtd: " format "\n" , ## arg)


/* Default erase size in K, always make it a multiple of PAGE_SIZE */
#define CONFIG_MTD_BLKDEV_ERASESIZE (128 << 10)	/* 128KiB */
#define VERSION "$Revision: 1.27 $"

/* Info for the block device */
struct blkmtd_dev {
	struct list_head list;
	struct block_device *blkdev;
	struct mtd_info mtd_info;
	struct semaphore wrbuf_mutex;
};


/* Static info about the MTD, used in cleanup_module */
static LIST_HEAD(blkmtd_device_list);


static void blkmtd_sync(struct mtd_info *mtd);

#define MAX_DEVICES 4

/* Module parameters passed by insmod/modprobe */
static char *device[MAX_DEVICES];    /* the block device to use */
static int erasesz[MAX_DEVICES];     /* optional default erase size */
static int ro[MAX_DEVICES];          /* optional read only flag */
static int sync;


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Simon Evans <spse@secret.org.uk>");
MODULE_DESCRIPTION("Emulate an MTD using a block device");
module_param_array(device, charp, NULL, 0);
MODULE_PARM_DESC(device, "block device to use");
module_param_array(erasesz, int, NULL, 0);
MODULE_PARM_DESC(erasesz, "optional erase size to use in KiB. eg 4=4KiB.");
module_param_array(ro, bool, NULL, 0);
MODULE_PARM_DESC(ro, "1=Read only, writes and erases cause errors");
module_param(sync, bool, 0);
MODULE_PARM_DESC(sync, "1=Synchronous writes");


/* completion handler for BIO reads */
static int bi_read_complete(struct bio *bio, unsigned int bytes_done, int error)
{
	if (bio->bi_size)
		return 1;

	complete((struct completion*)bio->bi_private);
	return 0;
}


/* completion handler for BIO writes */
static int bi_write_complete(struct bio *bio, unsigned int bytes_done, int error)
{
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct bio_vec *bvec = bio->bi_io_vec + bio->bi_vcnt - 1;

	if (bio->bi_size)
		return 1;

	if(!uptodate)
		err("bi_write_complete: not uptodate\n");

	do {
		struct page *page = bvec->bv_page;
		DEBUG(3, "Cleaning up page %ld\n", page->index);
		if (--bvec >= bio->bi_io_vec)
			prefetchw(&bvec->bv_page->flags);

		if (uptodate) {
			SetPageUptodate(page);
		} else {
			ClearPageUptodate(page);
			SetPageError(page);
		}
		clear_page_dirty(page);
		unlock_page(page);
		page_cache_release(page);
	} while (bvec >= bio->bi_io_vec);

	complete((struct completion*)bio->bi_private);
	return 0;
}


/* read one page from the block device */
static int blkmtd_readpage(struct blkmtd_dev *dev, struct page *page)
{
	struct bio *bio;
	struct completion event;
	int err = -ENOMEM;

	if(PageUptodate(page)) {
		DEBUG(2, "blkmtd: readpage page %ld is already upto date\n", page->index);
		unlock_page(page);
		return 0;
	}

	ClearPageUptodate(page);
	ClearPageError(page);

	bio = bio_alloc(GFP_KERNEL, 1);
	if(bio) {
		init_completion(&event);
		bio->bi_bdev = dev->blkdev;
		bio->bi_sector = page->index << (PAGE_SHIFT-9);
		bio->bi_private = &event;
		bio->bi_end_io = bi_read_complete;
		if(bio_add_page(bio, page, PAGE_SIZE, 0) == PAGE_SIZE) {
			submit_bio(READ_SYNC, bio);
			wait_for_completion(&event);
			err = test_bit(BIO_UPTODATE, &bio->bi_flags) ? 0 : -EIO;
			bio_put(bio);
		}
	}

	if(err)
		SetPageError(page);
	else
		SetPageUptodate(page);
	flush_dcache_page(page);
	unlock_page(page);
	return err;
}


/* write out the current BIO and wait for it to finish */
static int blkmtd_write_out(struct bio *bio)
{
	struct completion event;
	int err;

	if(!bio->bi_vcnt) {
		bio_put(bio);
		return 0;
	}

	init_completion(&event);
	bio->bi_private = &event;
	bio->bi_end_io = bi_write_complete;
	submit_bio(WRITE_SYNC, bio);
	wait_for_completion(&event);
	DEBUG(3, "submit_bio completed, bi_vcnt = %d\n", bio->bi_vcnt);
	err = test_bit(BIO_UPTODATE, &bio->bi_flags) ? 0 : -EIO;
	bio_put(bio);
	return err;
}


/**
 * blkmtd_add_page - add a page to the current BIO
 * @bio: bio to add to (NULL to alloc initial bio)
 * @blkdev: block device
 * @page: page to add
 * @pagecnt: pages left to add
 *
 * Adds a page to the current bio, allocating it if necessary. If it cannot be
 * added, the current bio is written out and a new one is allocated. Returns
 * the new bio to add or NULL on error
 */
static struct bio *blkmtd_add_page(struct bio *bio, struct block_device *blkdev,
				   struct page *page, int pagecnt)
{

 retry:
	if(!bio) {
		bio = bio_alloc(GFP_KERNEL, pagecnt);
		if(!bio)
			return NULL;
		bio->bi_sector = page->index << (PAGE_SHIFT-9);
		bio->bi_bdev = blkdev;
	}

	if(bio_add_page(bio, page, PAGE_SIZE, 0) != PAGE_SIZE) {
		blkmtd_write_out(bio);
		bio = NULL;
		goto retry;
	}
	return bio;
}


/**
 * write_pages - write block of data to device via the page cache
 * @dev: device to write to
 * @buf: data source or NULL if erase (output is set to 0xff)
 * @to: offset into output device
 * @len: amount to data to write
 * @retlen: amount of data written
 *
 * Grab pages from the page cache and fill them with the source data.
 * Non page aligned start and end result in a readin of the page and
 * part of the page being modified. Pages are added to the bio and then written
 * out.
 */
static int write_pages(struct blkmtd_dev *dev, const u_char *buf, loff_t to,
		    size_t len, size_t *retlen)
{
	int pagenr, offset;
	size_t start_len = 0, end_len;
	int pagecnt = 0;
	int err = 0;
	struct bio *bio = NULL;
	size_t thislen = 0;

	pagenr = to >> PAGE_SHIFT;
	offset = to & ~PAGE_MASK;

	DEBUG(2, "blkmtd: write_pages: buf = %p to = %ld len = %zd pagenr = %d offset = %d\n",
	      buf, (long)to, len, pagenr, offset);

	/* see if we have to do a partial write at the start */
	if(offset) {
		start_len = ((offset + len) > PAGE_SIZE) ? PAGE_SIZE - offset : len;
		len -= start_len;
	}

	/* calculate the length of the other two regions */
	end_len = len & ~PAGE_MASK;
	len -= end_len;

	if(start_len)
		pagecnt++;

	if(len)
		pagecnt += len >> PAGE_SHIFT;

	if(end_len)
		pagecnt++;

	down(&dev->wrbuf_mutex);

	DEBUG(3, "blkmtd: write: start_len = %zd len = %zd end_len = %zd pagecnt = %d\n",
	      start_len, len, end_len, pagecnt);

	if(start_len) {
		/* do partial start region */
		struct page *page;

		DEBUG(3, "blkmtd: write: doing partial start, page = %d len = %zd offset = %d\n",
		      pagenr, start_len, offset);

		BUG_ON(!buf);
		page = read_cache_page(dev->blkdev->bd_inode->i_mapping, pagenr, (filler_t *)blkmtd_readpage, dev);
		lock_page(page);
		if(PageDirty(page)) {
			err("to = %lld start_len = %zd len = %zd end_len = %zd pagenr = %d\n",
			    to, start_len, len, end_len, pagenr);
			BUG();
		}
		memcpy(page_address(page)+offset, buf, start_len);
		set_page_dirty(page);
		SetPageUptodate(page);
		buf += start_len;
		thislen = start_len;
		bio = blkmtd_add_page(bio, dev->blkdev, page, pagecnt);
		if(!bio) {
			err = -ENOMEM;
			err("bio_add_page failed\n");
			goto write_err;
		}
		pagecnt--;
		pagenr++;
	}

	/* Now do the main loop to a page aligned, n page sized output */
	if(len) {
		int pagesc = len >> PAGE_SHIFT;
		DEBUG(3, "blkmtd: write: whole pages start = %d, count = %d\n",
		      pagenr, pagesc);
		while(pagesc) {
			struct page *page;

			/* see if page is in the page cache */
			DEBUG(3, "blkmtd: write: grabbing page %d from page cache\n", pagenr);
			page = grab_cache_page(dev->blkdev->bd_inode->i_mapping, pagenr);
			if(PageDirty(page)) {
				BUG();
			}
			if(!page) {
				warn("write: cannot grab cache page %d", pagenr);
				err = -ENOMEM;
				goto write_err;
			}
			if(!buf) {
				memset(page_address(page), 0xff, PAGE_SIZE);
			} else {
				memcpy(page_address(page), buf, PAGE_SIZE);
				buf += PAGE_SIZE;
			}
			bio = blkmtd_add_page(bio, dev->blkdev, page, pagecnt);
			if(!bio) {
				err = -ENOMEM;
				err("bio_add_page failed\n");
				goto write_err;
			}
			pagenr++;
			pagecnt--;
			set_page_dirty(page);
			SetPageUptodate(page);
			pagesc--;
			thislen += PAGE_SIZE;
		}
	}

	if(end_len) {
		/* do the third region */
		struct page *page;
		DEBUG(3, "blkmtd: write: doing partial end, page = %d len = %zd\n",
		      pagenr, end_len);
		BUG_ON(!buf);
		page = read_cache_page(dev->blkdev->bd_inode->i_mapping, pagenr, (filler_t *)blkmtd_readpage, dev);
		lock_page(page);
		if(PageDirty(page)) {
			err("to = %lld start_len = %zd len = %zd end_len = %zd pagenr = %d\n",
			    to, start_len, len, end_len, pagenr);
			BUG();
		}
		memcpy(page_address(page), buf, end_len);
		set_page_dirty(page);
		SetPageUptodate(page);
		DEBUG(3, "blkmtd: write: writing out partial end\n");
		thislen += end_len;
		bio = blkmtd_add_page(bio, dev->blkdev, page, pagecnt);
		if(!bio) {
			err = -ENOMEM;
			err("bio_add_page failed\n");
			goto write_err;
		}
		pagenr++;
	}

	DEBUG(3, "blkmtd: write: got %d vectors to write\n", bio->bi_vcnt);
 write_err:
	if(bio)
		blkmtd_write_out(bio);

	DEBUG(2, "blkmtd: write: end, retlen = %zd, err = %d\n", *retlen, err);
	up(&dev->wrbuf_mutex);

	if(retlen)
		*retlen = thislen;
	return err;
}


/* erase a specified part of the device */
static int blkmtd_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct blkmtd_dev *dev = mtd->priv;
	struct mtd_erase_region_info *einfo = mtd->eraseregions;
	int numregions = mtd->numeraseregions;
	size_t from;
	u_long len;
	int err = -EIO;
	size_t retlen;

	instr->state = MTD_ERASING;
	from = instr->addr;
	len = instr->len;

	/* check erase region has valid start and length */
	DEBUG(2, "blkmtd: erase: dev = `%s' from = 0x%zx len = 0x%lx\n",
	      mtd->name+9, from, len);
	while(numregions) {
		DEBUG(3, "blkmtd: checking erase region = 0x%08X size = 0x%X num = 0x%x\n",
		      einfo->offset, einfo->erasesize, einfo->numblocks);
		if(from >= einfo->offset
		   && from < einfo->offset + (einfo->erasesize * einfo->numblocks)) {
			if(len == einfo->erasesize
			   && ( (from - einfo->offset) % einfo->erasesize == 0))
				break;
		}
		numregions--;
		einfo++;
	}

	if(!numregions) {
		/* Not a valid erase block */
		err("erase: invalid erase request 0x%lX @ 0x%08zX", len, from);
		instr->state = MTD_ERASE_FAILED;
		err = -EIO;
	}

	if(instr->state != MTD_ERASE_FAILED) {
		/* do the erase */
		DEBUG(3, "Doing erase from = %zd len = %ld\n", from, len);
		err = write_pages(dev, NULL, from, len, &retlen);
		if(err || retlen != len) {
			err("erase failed err = %d", err);
			instr->state = MTD_ERASE_FAILED;
		} else {
			instr->state = MTD_ERASE_DONE;
		}
	}

	DEBUG(3, "blkmtd: erase: checking callback\n");
	mtd_erase_callback(instr);
	DEBUG(2, "blkmtd: erase: finished (err = %d)\n", err);
	return err;
}


/* read a range of the data via the page cache */
static int blkmtd_read(struct mtd_info *mtd, loff_t from, size_t len,
		       size_t *retlen, u_char *buf)
{
	struct blkmtd_dev *dev = mtd->priv;
	int err = 0;
	int offset;
	int pagenr, pages;
	size_t thislen = 0;

	DEBUG(2, "blkmtd: read: dev = `%s' from = %lld len = %zd buf = %p\n",
	      mtd->name+9, from, len, buf);

	if(from > mtd->size)
		return -EINVAL;
	if(from + len > mtd->size)
		len = mtd->size - from;

	pagenr = from >> PAGE_SHIFT;
	offset = from - (pagenr << PAGE_SHIFT);

	pages = (offset+len+PAGE_SIZE-1) >> PAGE_SHIFT;
	DEBUG(3, "blkmtd: read: pagenr = %d offset = %d, pages = %d\n",
	      pagenr, offset, pages);

	while(pages) {
		struct page *page;
		int cpylen;

		DEBUG(3, "blkmtd: read: looking for page: %d\n", pagenr);
		page = read_cache_page(dev->blkdev->bd_inode->i_mapping, pagenr, (filler_t *)blkmtd_readpage, dev);
		if(IS_ERR(page)) {
			err = -EIO;
			goto readerr;
		}

		cpylen = (PAGE_SIZE > len) ? len : PAGE_SIZE;
		if(offset+cpylen > PAGE_SIZE)
			cpylen = PAGE_SIZE-offset;

		memcpy(buf + thislen, page_address(page) + offset, cpylen);
		offset = 0;
		len -= cpylen;
		thislen += cpylen;
		pagenr++;
		pages--;
		if(!PageDirty(page))
			page_cache_release(page);
	}

 readerr:
	if(retlen)
		*retlen = thislen;
	DEBUG(2, "blkmtd: end read: retlen = %zd, err = %d\n", thislen, err);
	return err;
}


/* write data to the underlying device */
static int blkmtd_write(struct mtd_info *mtd, loff_t to, size_t len,
			size_t *retlen, const u_char *buf)
{
	struct blkmtd_dev *dev = mtd->priv;
	int err;

	if(!len)
		return 0;

	DEBUG(2, "blkmtd: write: dev = `%s' to = %lld len = %zd buf = %p\n",
	      mtd->name+9, to, len, buf);

	if(to >= mtd->size) {
		return -ENOSPC;
	}

	if(to + len > mtd->size) {
		len = mtd->size - to;
	}

	err = write_pages(dev, buf, to, len, retlen);
	if(err > 0)
		err = 0;
	DEBUG(2, "blkmtd: write: end, err = %d\n", err);
	return err;
}


/* sync the device - wait until the write queue is empty */
static void blkmtd_sync(struct mtd_info *mtd)
{
	/* Currently all writes are synchronous */
}


static void free_device(struct blkmtd_dev *dev)
{
	DEBUG(2, "blkmtd: free_device() dev = %p\n", dev);
	if(dev) {
		kfree(dev->mtd_info.eraseregions);
		kfree(dev->mtd_info.name);
		if(dev->blkdev) {
			invalidate_inode_pages(dev->blkdev->bd_inode->i_mapping);
			close_bdev_excl(dev->blkdev);
		}
		kfree(dev);
	}
}


/* For a given size and initial erase size, calculate the number
 * and size of each erase region. Goes round the loop twice,
 * once to find out how many regions, then allocates space,
 * then round the loop again to fill it in.
 */
static struct mtd_erase_region_info *calc_erase_regions(
	size_t erase_size, size_t total_size, int *regions)
{
	struct mtd_erase_region_info *info = NULL;

	DEBUG(2, "calc_erase_regions, es = %zd size = %zd regions = %d\n",
	      erase_size, total_size, *regions);
	/* Make any user specified erasesize be a power of 2
	   and at least PAGE_SIZE */
	if(erase_size) {
		int es = erase_size;
		erase_size = 1;
		while(es != 1) {
			es >>= 1;
			erase_size <<= 1;
		}
		if(erase_size < PAGE_SIZE)
			erase_size = PAGE_SIZE;
	} else {
		erase_size = CONFIG_MTD_BLKDEV_ERASESIZE;
	}

	*regions = 0;

	do {
		int tot_size = total_size;
		int er_size = erase_size;
		int count = 0, offset = 0, regcnt = 0;

		while(tot_size) {
			count = tot_size / er_size;
			if(count) {
				tot_size = tot_size % er_size;
				if(info) {
					DEBUG(2, "adding to erase info off=%d er=%d cnt=%d\n",
					      offset, er_size, count);
					(info+regcnt)->offset = offset;
					(info+regcnt)->erasesize = er_size;
					(info+regcnt)->numblocks = count;
					(*regions)++;
				}
				regcnt++;
				offset += (count * er_size);
			}
			while(er_size > tot_size)
				er_size >>= 1;
		}
		if(info == NULL) {
			info = kmalloc(regcnt * sizeof(struct mtd_erase_region_info), GFP_KERNEL);
			if(!info)
				break;
		}
	} while(!(*regions));
	DEBUG(2, "calc_erase_regions done, es = %zd size = %zd regions = %d\n",
	      erase_size, total_size, *regions);
	return info;
}


extern dev_t __init name_to_dev_t(const char *line);

static struct blkmtd_dev *add_device(char *devname, int readonly, int erase_size)
{
	struct block_device *bdev;
	int mode;
	struct blkmtd_dev *dev;

	if(!devname)
		return NULL;

	/* Get a handle on the device */


#ifdef MODULE
	mode = (readonly) ? O_RDONLY : O_RDWR;
	bdev = open_bdev_excl(devname, mode, NULL);
#else
	mode = (readonly) ? FMODE_READ : FMODE_WRITE;
	bdev = open_by_devnum(name_to_dev_t(devname), mode);
#endif
	if(IS_ERR(bdev)) {
		err("error: cannot open device %s", devname);
		DEBUG(2, "blkmtd: opening bdev returned %ld\n", PTR_ERR(bdev));
		return NULL;
	}

	DEBUG(1, "blkmtd: found a block device major = %d, minor = %d\n",
	      MAJOR(bdev->bd_dev), MINOR(bdev->bd_dev));

	if(MAJOR(bdev->bd_dev) == MTD_BLOCK_MAJOR) {
		err("attempting to use an MTD device as a block device");
		blkdev_put(bdev);
		return NULL;
	}

	dev = kmalloc(sizeof(struct blkmtd_dev), GFP_KERNEL);
	if(dev == NULL) {
		blkdev_put(bdev);
		return NULL;
	}

	memset(dev, 0, sizeof(struct blkmtd_dev));
	dev->blkdev = bdev;
	if(!readonly) {
		init_MUTEX(&dev->wrbuf_mutex);
	}

	dev->mtd_info.size = dev->blkdev->bd_inode->i_size & PAGE_MASK;

	/* Setup the MTD structure */
	/* make the name contain the block device in */
	dev->mtd_info.name = kmalloc(sizeof("blkmtd: ") + strlen(devname), GFP_KERNEL);
	if(dev->mtd_info.name == NULL)
		goto devinit_err;

	sprintf(dev->mtd_info.name, "blkmtd: %s", devname);
	dev->mtd_info.eraseregions = calc_erase_regions(erase_size, dev->mtd_info.size,
							&dev->mtd_info.numeraseregions);
	if(dev->mtd_info.eraseregions == NULL)
		goto devinit_err;

	dev->mtd_info.erasesize = dev->mtd_info.eraseregions->erasesize;
	DEBUG(1, "blkmtd: init: found %d erase regions\n",
	      dev->mtd_info.numeraseregions);

	if(readonly) {
		dev->mtd_info.type = MTD_ROM;
		dev->mtd_info.flags = MTD_CAP_ROM;
	} else {
		dev->mtd_info.type = MTD_RAM;
		dev->mtd_info.flags = MTD_CAP_RAM;
		dev->mtd_info.erase = blkmtd_erase;
		dev->mtd_info.write = blkmtd_write;
		dev->mtd_info.writev = default_mtd_writev;
		dev->mtd_info.sync = blkmtd_sync;
	}
	dev->mtd_info.read = blkmtd_read;
	dev->mtd_info.readv = default_mtd_readv;
	dev->mtd_info.priv = dev;
	dev->mtd_info.owner = THIS_MODULE;

	list_add(&dev->list, &blkmtd_device_list);
	if (add_mtd_device(&dev->mtd_info)) {
		/* Device didnt get added, so free the entry */
		list_del(&dev->list);
		goto devinit_err;
	} else {
		info("mtd%d: [%s] erase_size = %dKiB %s",
		     dev->mtd_info.index, dev->mtd_info.name + strlen("blkmtd: "),
		     dev->mtd_info.erasesize >> 10,
		     readonly ? "(read-only)" : "");
	}

	return dev;

 devinit_err:
	free_device(dev);
	return NULL;
}


/* Cleanup and exit - sync the device and kill of the kernel thread */
static void __devexit cleanup_blkmtd(void)
{
	struct list_head *temp1, *temp2;

	/* Remove the MTD devices */
	list_for_each_safe(temp1, temp2, &blkmtd_device_list) {
		struct blkmtd_dev *dev = list_entry(temp1, struct blkmtd_dev,
						    list);
		blkmtd_sync(&dev->mtd_info);
		del_mtd_device(&dev->mtd_info);
		info("mtd%d: [%s] removed", dev->mtd_info.index,
		     dev->mtd_info.name + strlen("blkmtd: "));
		list_del(&dev->list);
		free_device(dev);
	}
}

#ifndef MODULE

/* Handle kernel boot params */


static int __init param_blkmtd_device(char *str)
{
	int i;

	for(i = 0; i < MAX_DEVICES; i++) {
		device[i] = str;
		DEBUG(2, "blkmtd: device setup: %d = %s\n", i, device[i]);
		strsep(&str, ",");
	}
	return 1;
}


static int __init param_blkmtd_erasesz(char *str)
{
	int i;
	for(i = 0; i < MAX_DEVICES; i++) {
		char *val = strsep(&str, ",");
		if(val)
			erasesz[i] = simple_strtoul(val, NULL, 0);
		DEBUG(2, "blkmtd: erasesz setup: %d = %d\n", i, erasesz[i]);
	}

	return 1;
}


static int __init param_blkmtd_ro(char *str)
{
	int i;
	for(i = 0; i < MAX_DEVICES; i++) {
		char *val = strsep(&str, ",");
		if(val)
			ro[i] = simple_strtoul(val, NULL, 0);
		DEBUG(2, "blkmtd: ro setup: %d = %d\n", i, ro[i]);
	}

	return 1;
}


static int __init param_blkmtd_sync(char *str)
{
	if(str[0] == '1')
		sync = 1;
	return 1;
}

__setup("blkmtd_device=", param_blkmtd_device);
__setup("blkmtd_erasesz=", param_blkmtd_erasesz);
__setup("blkmtd_ro=", param_blkmtd_ro);
__setup("blkmtd_sync=", param_blkmtd_sync);

#endif


/* Startup */
static int __init init_blkmtd(void)
{
	int i;

	info("version " VERSION);
	/* Check args - device[0] is the bare minimum*/
	if(!device[0]) {
		err("error: missing `device' name\n");
		return -EINVAL;
	}

	for(i = 0; i < MAX_DEVICES; i++)
		add_device(device[i], ro[i], erasesz[i] << 10);

	if(list_empty(&blkmtd_device_list))
		return -EINVAL;

	return 0;
}

module_init(init_blkmtd);
module_exit(cleanup_blkmtd);
