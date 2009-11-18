/*
 * Xpram.c -- the S/390 expanded memory RAM-disk
 *           
 * significant parts of this code are based on
 * the sbull device driver presented in
 * A. Rubini: Linux Device Drivers
 *
 * Author of XPRAM specific coding: Reinhard Buendgen
 *                                  buendgen@de.ibm.com
 * Rewrite for 2.5: Martin Schwidefsky <schwidefsky@de.ibm.com>
 *
 * External interfaces:
 *   Interfaces to linux kernel
 *        xpram_setup: read kernel parameters
 *   Device specific file operations
 *        xpram_iotcl
 *        xpram_open
 *
 * "ad-hoc" partitioning:
 *    the expanded memory can be partitioned among several devices 
 *    (with different minors). The partitioning set up can be
 *    set by kernel or module parameters (int devs & int sizes[])
 *
 * Potential future improvements:
 *   generic hard disk support to replace ad-hoc partitioning
 */

#define KMSG_COMPONENT "xpram"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/ctype.h>  /* isdigit, isxdigit */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/blkpg.h>
#include <linux/hdreg.h>  /* HDIO_GETGEO */
#include <linux/sysdev.h>
#include <linux/bio.h>
#include <linux/suspend.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>

#define XPRAM_NAME	"xpram"
#define XPRAM_DEVS	1	/* one partition */
#define XPRAM_MAX_DEVS	32	/* maximal number of devices (partitions) */

typedef struct {
	unsigned int	size;		/* size of xpram segment in pages */
	unsigned int	offset;		/* start page of xpram segment */
} xpram_device_t;

static xpram_device_t xpram_devices[XPRAM_MAX_DEVS];
static unsigned int xpram_sizes[XPRAM_MAX_DEVS];
static struct gendisk *xpram_disks[XPRAM_MAX_DEVS];
static struct request_queue *xpram_queues[XPRAM_MAX_DEVS];
static unsigned int xpram_pages;
static int xpram_devs;

/*
 * Parameter parsing functions.
 */
static int __initdata devs = XPRAM_DEVS;
static char __initdata *sizes[XPRAM_MAX_DEVS];

module_param(devs, int, 0);
module_param_array(sizes, charp, NULL, 0);

MODULE_PARM_DESC(devs, "number of devices (\"partitions\"), " \
		 "the default is " __MODULE_STRING(XPRAM_DEVS) "\n");
MODULE_PARM_DESC(sizes, "list of device (partition) sizes " \
		 "the defaults are 0s \n" \
		 "All devices with size 0 equally partition the "
		 "remaining space on the expanded strorage not "
		 "claimed by explicit sizes\n");
MODULE_LICENSE("GPL");

/*
 * Copy expanded memory page (4kB) into main memory                  
 * Arguments                                                         
 *           page_addr:    address of target page                    
 *           xpage_index:  index of expandeded memory page           
 * Return value                                                      
 *           0:            if operation succeeds
 *           -EIO:         if pgin failed
 *           -ENXIO:       if xpram has vanished
 */
static int xpram_page_in (unsigned long page_addr, unsigned int xpage_index)
{
	int cc = 2;	/* return unused cc 2 if pgin traps */

	asm volatile(
		"	.insn	rre,0xb22e0000,%1,%2\n"  /* pgin %1,%2 */
		"0:	ipm	%0\n"
		"	srl	%0,28\n"
		"1:\n"
		EX_TABLE(0b,1b)
		: "+d" (cc) : "a" (__pa(page_addr)), "d" (xpage_index) : "cc");
	if (cc == 3)
		return -ENXIO;
	if (cc == 2)
		return -ENXIO;
	if (cc == 1)
		return -EIO;
	return 0;
}

/*
 * Copy a 4kB page of main memory to an expanded memory page          
 * Arguments                                                          
 *           page_addr:    address of source page                     
 *           xpage_index:  index of expandeded memory page            
 * Return value                                                       
 *           0:            if operation succeeds
 *           -EIO:         if pgout failed
 *           -ENXIO:       if xpram has vanished
 */
static long xpram_page_out (unsigned long page_addr, unsigned int xpage_index)
{
	int cc = 2;	/* return unused cc 2 if pgin traps */

	asm volatile(
		"	.insn	rre,0xb22f0000,%1,%2\n"  /* pgout %1,%2 */
		"0:	ipm	%0\n"
		"	srl	%0,28\n"
		"1:\n"
		EX_TABLE(0b,1b)
		: "+d" (cc) : "a" (__pa(page_addr)), "d" (xpage_index) : "cc");
	if (cc == 3)
		return -ENXIO;
	if (cc == 2)
		return -ENXIO;
	if (cc == 1)
		return -EIO;
	return 0;
}

/*
 * Check if xpram is available.
 */
static int xpram_present(void)
{
	unsigned long mem_page;
	int rc;

	mem_page = (unsigned long) __get_free_page(GFP_KERNEL);
	if (!mem_page)
		return -ENOMEM;
	rc = xpram_page_in(mem_page, 0);
	free_page(mem_page);
	return rc ? -ENXIO : 0;
}

/*
 * Return index of the last available xpram page.
 */
static unsigned long xpram_highest_page_index(void)
{
	unsigned int page_index, add_bit;
	unsigned long mem_page;

	mem_page = (unsigned long) __get_free_page(GFP_KERNEL);
	if (!mem_page)
		return 0;

	page_index = 0;
	add_bit = 1ULL << (sizeof(unsigned int)*8 - 1);
	while (add_bit > 0) {
		if (xpram_page_in(mem_page, page_index | add_bit) == 0)
			page_index |= add_bit;
		add_bit >>= 1;
	}

	free_page (mem_page);

	return page_index;
}

/*
 * Block device make request function.
 */
static int xpram_make_request(struct request_queue *q, struct bio *bio)
{
	xpram_device_t *xdev = bio->bi_bdev->bd_disk->private_data;
	struct bio_vec *bvec;
	unsigned int index;
	unsigned long page_addr;
	unsigned long bytes;
	int i;

	if ((bio->bi_sector & 7) != 0 || (bio->bi_size & 4095) != 0)
		/* Request is not page-aligned. */
		goto fail;
	if ((bio->bi_size >> 12) > xdev->size)
		/* Request size is no page-aligned. */
		goto fail;
	if ((bio->bi_sector >> 3) > 0xffffffffU - xdev->offset)
		goto fail;
	index = (bio->bi_sector >> 3) + xdev->offset;
	bio_for_each_segment(bvec, bio, i) {
		page_addr = (unsigned long)
			kmap(bvec->bv_page) + bvec->bv_offset;
		bytes = bvec->bv_len;
		if ((page_addr & 4095) != 0 || (bytes & 4095) != 0)
			/* More paranoia. */
			goto fail;
		while (bytes > 0) {
			if (bio_data_dir(bio) == READ) {
				if (xpram_page_in(page_addr, index) != 0)
					goto fail;
			} else {
				if (xpram_page_out(page_addr, index) != 0)
					goto fail;
			}
			page_addr += 4096;
			bytes -= 4096;
			index++;
		}
	}
	set_bit(BIO_UPTODATE, &bio->bi_flags);
	bio_endio(bio, 0);
	return 0;
fail:
	bio_io_error(bio);
	return 0;
}

static int xpram_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	unsigned long size;

	/*
	 * get geometry: we have to fake one...  trim the size to a
	 * multiple of 64 (32k): tell we have 16 sectors, 4 heads,
	 * whatever cylinders. Tell also that data starts at sector. 4.
	 */
	size = (xpram_pages * 8) & ~0x3f;
	geo->cylinders = size >> 6;
	geo->heads = 4;
	geo->sectors = 16;
	geo->start = 4;
	return 0;
}

static const struct block_device_operations xpram_devops =
{
	.owner	= THIS_MODULE,
	.getgeo	= xpram_getgeo,
};

/*
 * Setup xpram_sizes array.
 */
static int __init xpram_setup_sizes(unsigned long pages)
{
	unsigned long mem_needed;
	unsigned long mem_auto;
	unsigned long long size;
	int mem_auto_no;
	int i;

	/* Check number of devices. */
	if (devs <= 0 || devs > XPRAM_MAX_DEVS) {
		pr_err("%d is not a valid number of XPRAM devices\n",devs);
		return -EINVAL;
	}
	xpram_devs = devs;

	/*
	 * Copy sizes array to xpram_sizes and align partition
	 * sizes to page boundary.
	 */
	mem_needed = 0;
	mem_auto_no = 0;
	for (i = 0; i < xpram_devs; i++) {
		if (sizes[i]) {
			size = simple_strtoull(sizes[i], &sizes[i], 0);
			switch (sizes[i][0]) {
			case 'g':
			case 'G':
				size <<= 20;
				break;
			case 'm':
			case 'M':
				size <<= 10;
			}
			xpram_sizes[i] = (size + 3) & -4UL;
		}
		if (xpram_sizes[i])
			mem_needed += xpram_sizes[i];
		else
			mem_auto_no++;
	}
	
	pr_info("  number of devices (partitions): %d \n", xpram_devs);
	for (i = 0; i < xpram_devs; i++) {
		if (xpram_sizes[i])
			pr_info("  size of partition %d: %u kB\n",
				i, xpram_sizes[i]);
		else
			pr_info("  size of partition %d to be set "
				"automatically\n",i);
	}
	pr_info("  memory needed (for sized partitions): %lu kB\n",
		mem_needed);
	pr_info("  partitions to be sized automatically: %d\n",
		mem_auto_no);

	if (mem_needed > pages * 4) {
		pr_err("Not enough expanded memory available\n");
		return -EINVAL;
	}

	/*
	 * partitioning:
	 * xpram_sizes[i] != 0; partition i has size xpram_sizes[i] kB
	 * else:             ; all partitions with zero xpram_sizes[i]
	 *                     partition equally the remaining space
	 */
	if (mem_auto_no) {
		mem_auto = ((pages - mem_needed / 4) / mem_auto_no) * 4;
		pr_info("  automatically determined "
			"partition size: %lu kB\n", mem_auto);
		for (i = 0; i < xpram_devs; i++)
			if (xpram_sizes[i] == 0)
				xpram_sizes[i] = mem_auto;
	}
	return 0;
}

static int __init xpram_setup_blkdev(void)
{
	unsigned long offset;
	int i, rc = -ENOMEM;

	for (i = 0; i < xpram_devs; i++) {
		xpram_disks[i] = alloc_disk(1);
		if (!xpram_disks[i])
			goto out;
		xpram_queues[i] = blk_alloc_queue(GFP_KERNEL);
		if (!xpram_queues[i]) {
			put_disk(xpram_disks[i]);
			goto out;
		}
		blk_queue_make_request(xpram_queues[i], xpram_make_request);
		blk_queue_logical_block_size(xpram_queues[i], 4096);
	}

	/*
	 * Register xpram major.
	 */
	rc = register_blkdev(XPRAM_MAJOR, XPRAM_NAME);
	if (rc < 0)
		goto out;

	/*
	 * Setup device structures.
	 */
	offset = 0;
	for (i = 0; i < xpram_devs; i++) {
		struct gendisk *disk = xpram_disks[i];

		xpram_devices[i].size = xpram_sizes[i] / 4;
		xpram_devices[i].offset = offset;
		offset += xpram_devices[i].size;
		disk->major = XPRAM_MAJOR;
		disk->first_minor = i;
		disk->fops = &xpram_devops;
		disk->private_data = &xpram_devices[i];
		disk->queue = xpram_queues[i];
		sprintf(disk->disk_name, "slram%d", i);
		set_capacity(disk, xpram_sizes[i] << 1);
		add_disk(disk);
	}

	return 0;
out:
	while (i--) {
		blk_cleanup_queue(xpram_queues[i]);
		put_disk(xpram_disks[i]);
	}
	return rc;
}

/*
 * Resume failed: Print error message and call panic.
 */
static void xpram_resume_error(const char *message)
{
	pr_err("Resuming the system failed: %s\n", message);
	panic("xpram resume error\n");
}

/*
 * Check if xpram setup changed between suspend and resume.
 */
static int xpram_restore(struct device *dev)
{
	if (!xpram_pages)
		return 0;
	if (xpram_present() != 0)
		xpram_resume_error("xpram disappeared");
	if (xpram_pages != xpram_highest_page_index() + 1)
		xpram_resume_error("Size of xpram changed");
	return 0;
}

static struct dev_pm_ops xpram_pm_ops = {
	.restore	= xpram_restore,
};

static struct platform_driver xpram_pdrv = {
	.driver = {
		.name	= XPRAM_NAME,
		.owner	= THIS_MODULE,
		.pm	= &xpram_pm_ops,
	},
};

static struct platform_device *xpram_pdev;

/*
 * Finally, the init/exit functions.
 */
static void __exit xpram_exit(void)
{
	int i;
	for (i = 0; i < xpram_devs; i++) {
		del_gendisk(xpram_disks[i]);
		blk_cleanup_queue(xpram_queues[i]);
		put_disk(xpram_disks[i]);
	}
	unregister_blkdev(XPRAM_MAJOR, XPRAM_NAME);
	platform_device_unregister(xpram_pdev);
	platform_driver_unregister(&xpram_pdrv);
}

static int __init xpram_init(void)
{
	int rc;

	/* Find out size of expanded memory. */
	if (xpram_present() != 0) {
		pr_err("No expanded memory available\n");
		return -ENODEV;
	}
	xpram_pages = xpram_highest_page_index() + 1;
	pr_info("  %u pages expanded memory found (%lu KB).\n",
		xpram_pages, (unsigned long) xpram_pages*4);
	rc = xpram_setup_sizes(xpram_pages);
	if (rc)
		return rc;
	rc = platform_driver_register(&xpram_pdrv);
	if (rc)
		return rc;
	xpram_pdev = platform_device_register_simple(XPRAM_NAME, -1, NULL, 0);
	if (IS_ERR(xpram_pdev)) {
		rc = PTR_ERR(xpram_pdev);
		goto fail_platform_driver_unregister;
	}
	rc = xpram_setup_blkdev();
	if (rc)
		goto fail_platform_device_unregister;
	return 0;

fail_platform_device_unregister:
	platform_device_unregister(xpram_pdev);
fail_platform_driver_unregister:
	platform_driver_unregister(&xpram_pdrv);
	return rc;
}

module_init(xpram_init);
module_exit(xpram_exit);
