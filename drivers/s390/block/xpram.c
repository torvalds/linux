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
#include <linux/devfs_fs_kernel.h>
#include <asm/uaccess.h>

#define XPRAM_NAME	"xpram"
#define XPRAM_DEVS	1	/* one partition */
#define XPRAM_MAX_DEVS	32	/* maximal number of devices (partitions) */

#define PRINT_DEBUG(x...)	printk(KERN_DEBUG XPRAM_NAME " debug:" x)
#define PRINT_INFO(x...)	printk(KERN_INFO XPRAM_NAME " info:" x)
#define PRINT_WARN(x...)	printk(KERN_WARNING XPRAM_NAME " warning:" x)
#define PRINT_ERR(x...)		printk(KERN_ERR XPRAM_NAME " error:" x)


static struct sysdev_class xpram_sysclass = {
	set_kset_name("xpram"),
};

static struct sys_device xpram_sys_device = {
	.id	= 0,
	.cls	= &xpram_sysclass,
}; 

typedef struct {
	unsigned int	size;		/* size of xpram segment in pages */
	unsigned int	offset;		/* start page of xpram segment */
} xpram_device_t;

static xpram_device_t xpram_devices[XPRAM_MAX_DEVS];
static unsigned int xpram_sizes[XPRAM_MAX_DEVS];
static struct gendisk *xpram_disks[XPRAM_MAX_DEVS];
static unsigned int xpram_pages;
static int xpram_devs;

/*
 * Parameter parsing functions.
 */
static int devs = XPRAM_DEVS;
static unsigned int sizes[XPRAM_MAX_DEVS];

module_param(devs, int, 0);
module_param_array(sizes, int, NULL, 0);

MODULE_PARM_DESC(devs, "number of devices (\"partitions\"), " \
		 "the default is " __MODULE_STRING(XPRAM_DEVS) "\n");
MODULE_PARM_DESC(sizes, "list of device (partition) sizes " \
		 "the defaults are 0s \n" \
		 "All devices with size 0 equally partition the "
		 "remaining space on the expanded strorage not "
		 "claimed by explicit sizes\n");
MODULE_LICENSE("GPL");

#ifndef MODULE
/*
 * Parses the kernel parameters given in the kernel parameter line.
 * The expected format is
 *           <number_of_partitions>[","<partition_size>]*
 * where
 *           devices is a positive integer that initializes xpram_devs
 *           each size is a non-negative integer possibly followed by a
 *           magnitude (k,K,m,M,g,G), the list of sizes initialises
 *           xpram_sizes
 *
 * Arguments
 *           str: substring of kernel parameter line that contains xprams
 *                kernel parameters.
 *
 * Result    0 on success, -EINVAL else -- only for Version > 2.3
 *
 * Side effects
 *           the global variabls devs is set to the value of
 *           <number_of_partitions> and sizes[i] is set to the i-th
 *           partition size (if provided). A parsing error of a value
 *           results in this value being set to -EINVAL.
 */
static int __init xpram_setup (char *str)
{
	char *cp;
	int i;

	devs = simple_strtoul(str, &cp, 10);
	if (cp <= str || devs > XPRAM_MAX_DEVS)
		return 0;
	for (i = 0; (i < devs) && (*cp++ == ','); i++) {
		sizes[i] = simple_strtoul(cp, &cp, 10);
		if (*cp == 'g' || *cp == 'G') {
			sizes[i] <<= 20;
			cp++;
		} else if (*cp == 'm' || *cp == 'M') {
			sizes[i] <<= 10;
			cp++;
		} else if (*cp == 'k' || *cp == 'K')
			cp++;
		while (isspace(*cp)) cp++;
	}
	if (*cp == ',' && i >= devs)
		PRINT_WARN("partition sizes list has too many entries.\n");
	else if (*cp != 0)
		PRINT_WARN("ignored '%s' at end of parameter string.\n", cp);
	return 1;
}

__setup("xpram_parts=", xpram_setup);
#endif

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
	int cc;

	__asm__ __volatile__ (
		"   lhi   %0,2\n"  /* return unused cc 2 if pgin traps */
		"   .insn rre,0xb22e0000,%1,%2\n"  /* pgin %1,%2 */
                "0: ipm   %0\n"
		"   srl   %0,28\n"
		"1:\n"
#ifndef CONFIG_64BIT
		".section __ex_table,\"a\"\n"
		"   .align 4\n"
		"   .long  0b,1b\n"
		".previous"
#else
                ".section __ex_table,\"a\"\n"
                "   .align 8\n"
                "   .quad 0b,1b\n"
                ".previous"
#endif
		: "=&d" (cc) 
		: "a" (__pa(page_addr)), "a" (xpage_index) 
		: "cc" );
	if (cc == 3)
		return -ENXIO;
	if (cc == 2) {
		PRINT_ERR("expanded storage lost!\n");
		return -ENXIO;
	}
	if (cc == 1) {
		PRINT_ERR("page in failed for page index %u.\n",
			  xpage_index);
		return -EIO;
	}
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
	int cc;

	__asm__ __volatile__ (
		"   lhi   %0,2\n"  /* return unused cc 2 if pgout traps */
		"   .insn rre,0xb22f0000,%1,%2\n"  /* pgout %1,%2 */
                "0: ipm   %0\n"
		"   srl   %0,28\n"
		"1:\n"
#ifndef CONFIG_64BIT
		".section __ex_table,\"a\"\n"
		"   .align 4\n"
		"   .long  0b,1b\n"
		".previous"
#else
                ".section __ex_table,\"a\"\n"
                "   .align 8\n"
                "   .quad 0b,1b\n"
                ".previous"
#endif
		: "=&d" (cc) 
		: "a" (__pa(page_addr)), "a" (xpage_index) 
		: "cc" );
	if (cc == 3)
		return -ENXIO;
	if (cc == 2) {
		PRINT_ERR("expanded storage lost!\n");
		return -ENXIO;
	}
	if (cc == 1) {
		PRINT_ERR("page out failed for page index %u.\n",
			  xpage_index);
		return -EIO;
	}
	return 0;
}

/*
 * Check if xpram is available.
 */
static int __init xpram_present(void)
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
static unsigned long __init xpram_highest_page_index(void)
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
static int xpram_make_request(request_queue_t *q, struct bio *bio)
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
	bytes = bio->bi_size;
	bio->bi_size = 0;
	bio->bi_end_io(bio, bytes, 0);
	return 0;
fail:
	bio_io_error(bio, bio->bi_size);
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

static struct block_device_operations xpram_devops =
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
	int mem_auto_no;
	int i;

	/* Check number of devices. */
	if (devs <= 0 || devs > XPRAM_MAX_DEVS) {
		PRINT_ERR("invalid number %d of devices\n",devs);
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
		xpram_sizes[i] = (sizes[i] + 3) & -4UL;
		if (xpram_sizes[i])
			mem_needed += xpram_sizes[i];
		else
			mem_auto_no++;
	}
	
	PRINT_INFO("  number of devices (partitions): %d \n", xpram_devs);
	for (i = 0; i < xpram_devs; i++) {
		if (xpram_sizes[i])
			PRINT_INFO("  size of partition %d: %u kB\n",
				   i, xpram_sizes[i]);
		else
			PRINT_INFO("  size of partition %d to be set "
				   "automatically\n",i);
	}
	PRINT_DEBUG("  memory needed (for sized partitions): %lu kB\n",
		    mem_needed);
	PRINT_DEBUG("  partitions to be sized automatically: %d\n",
		    mem_auto_no);

	if (mem_needed > pages * 4) {
		PRINT_ERR("Not enough expanded memory available\n");
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
		PRINT_INFO("  automatically determined "
			   "partition size: %lu kB\n", mem_auto);
		for (i = 0; i < xpram_devs; i++)
			if (xpram_sizes[i] == 0)
				xpram_sizes[i] = mem_auto;
	}
	return 0;
}

static struct request_queue *xpram_queue;

static int __init xpram_setup_blkdev(void)
{
	unsigned long offset;
	int i, rc = -ENOMEM;

	for (i = 0; i < xpram_devs; i++) {
		struct gendisk *disk = alloc_disk(1);
		if (!disk)
			goto out;
		xpram_disks[i] = disk;
	}

	/*
	 * Register xpram major.
	 */
	rc = register_blkdev(XPRAM_MAJOR, XPRAM_NAME);
	if (rc < 0)
		goto out;

	devfs_mk_dir("slram");

	/*
	 * Assign the other needed values: make request function, sizes and
	 * hardsect size. All the minor devices feature the same value.
	 */
	xpram_queue = blk_alloc_queue(GFP_KERNEL);
	if (!xpram_queue) {
		rc = -ENOMEM;
		goto out_unreg;
	}
	blk_queue_make_request(xpram_queue, xpram_make_request);
	blk_queue_hardsect_size(xpram_queue, 4096);

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
		disk->queue = xpram_queue;
		sprintf(disk->disk_name, "slram%d", i);
		sprintf(disk->devfs_name, "slram/%d", i);
		set_capacity(disk, xpram_sizes[i] << 1);
		add_disk(disk);
	}

	return 0;
out_unreg:
	devfs_remove("slram");
	unregister_blkdev(XPRAM_MAJOR, XPRAM_NAME);
out:
	while (i--)
		put_disk(xpram_disks[i]);
	return rc;
}

/*
 * Finally, the init/exit functions.
 */
static void __exit xpram_exit(void)
{
	int i;
	for (i = 0; i < xpram_devs; i++) {
		del_gendisk(xpram_disks[i]);
		put_disk(xpram_disks[i]);
	}
	unregister_blkdev(XPRAM_MAJOR, XPRAM_NAME);
	devfs_remove("slram");
	blk_cleanup_queue(xpram_queue);
	sysdev_unregister(&xpram_sys_device);
	sysdev_class_unregister(&xpram_sysclass);
}

static int __init xpram_init(void)
{
	int rc;

	/* Find out size of expanded memory. */
	if (xpram_present() != 0) {
		PRINT_WARN("No expanded memory available\n");
		return -ENODEV;
	}
	xpram_pages = xpram_highest_page_index();
	PRINT_INFO("  %u pages expanded memory found (%lu KB).\n",
		   xpram_pages, (unsigned long) xpram_pages*4);
	rc = xpram_setup_sizes(xpram_pages);
	if (rc)
		return rc;
	rc = sysdev_class_register(&xpram_sysclass);
	if (rc)
		return rc;

	rc = sysdev_register(&xpram_sys_device);
	if (rc) {
		sysdev_class_unregister(&xpram_sysclass);
		return rc;
	}
	rc = xpram_setup_blkdev();
	if (rc)
		sysdev_unregister(&xpram_sys_device);
	return rc;
}

module_init(xpram_init);
module_exit(xpram_exit);
