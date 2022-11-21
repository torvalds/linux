// SPDX-License-Identifier: GPL-2.0
/*
 * scsicam.c - SCSI CAM support functions, use for HDIO_GETGEO, etc.
 *
 * Copyright 1993, 1994 Drew Eckhardt
 *      Visionary Computing 
 *      (Unix and Linux consulting and custom programming)
 *      drew@Colorado.EDU
 *      +1 (303) 786-7975
 *
 * For more information, please consult the SCSI-CAM draft.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/pagemap.h>
#include <linux/msdos_partition.h>
#include <asm/unaligned.h>

#include <scsi/scsicam.h>

/**
 * scsi_bios_ptable - Read PC partition table out of first sector of device.
 * @dev: from this device
 *
 * Description: Reads the first sector from the device and returns %0x42 bytes
 *              starting at offset %0x1be.
 * Returns: partition table in kmalloc(GFP_KERNEL) memory, or NULL on error.
 */
unsigned char *scsi_bios_ptable(struct block_device *dev)
{
	struct address_space *mapping = bdev_whole(dev)->bd_inode->i_mapping;
	unsigned char *res = NULL;
	struct page *page;

	page = read_mapping_page(mapping, 0, NULL);
	if (IS_ERR(page))
		return NULL;

	if (!PageError(page))
		res = kmemdup(page_address(page) + 0x1be, 66, GFP_KERNEL);
	put_page(page);
	return res;
}
EXPORT_SYMBOL(scsi_bios_ptable);

/**
 * scsi_partsize - Parse cylinders/heads/sectors from PC partition table
 * @bdev: block device to parse
 * @capacity: size of the disk in sectors
 * @geom: output in form of [hds, cylinders, sectors]
 *
 * Determine the BIOS mapping/geometry used to create the partition
 * table, storing the results in @geom.
 *
 * Returns: %false on failure, %true on success.
 */
bool scsi_partsize(struct block_device *bdev, sector_t capacity, int geom[3])
{
	int cyl, ext_cyl, end_head, end_cyl, end_sector;
	unsigned int logical_end, physical_end, ext_physical_end;
	struct msdos_partition *p, *largest = NULL;
	void *buf;
	int ret = false;

	buf = scsi_bios_ptable(bdev);
	if (!buf)
		return false;

	if (*(unsigned short *) (buf + 64) == 0xAA55) {
		int largest_cyl = -1, i;

		for (i = 0, p = buf; i < 4; i++, p++) {
			if (!p->sys_ind)
				continue;
#ifdef DEBUG
			printk("scsicam_bios_param : partition %d has system \n",
			       i);
#endif
			cyl = p->cyl + ((p->sector & 0xc0) << 2);
			if (cyl > largest_cyl) {
				largest_cyl = cyl;
				largest = p;
			}
		}
	}
	if (largest) {
		end_cyl = largest->end_cyl + ((largest->end_sector & 0xc0) << 2);
		end_head = largest->end_head;
		end_sector = largest->end_sector & 0x3f;

		if (end_head + 1 == 0 || end_sector == 0)
			goto out_free_buf;

#ifdef DEBUG
		printk("scsicam_bios_param : end at h = %d, c = %d, s = %d\n",
		       end_head, end_cyl, end_sector);
#endif

		physical_end = end_cyl * (end_head + 1) * end_sector +
		    end_head * end_sector + end_sector;

		/* This is the actual _sector_ number at the end */
		logical_end = get_unaligned_le32(&largest->start_sect)
		    + get_unaligned_le32(&largest->nr_sects);

		/* This is for >1023 cylinders */
		ext_cyl = (logical_end - (end_head * end_sector + end_sector))
		    / (end_head + 1) / end_sector;
		ext_physical_end = ext_cyl * (end_head + 1) * end_sector +
		    end_head * end_sector + end_sector;

#ifdef DEBUG
		printk("scsicam_bios_param : logical_end=%d physical_end=%d ext_physical_end=%d ext_cyl=%d\n"
		  ,logical_end, physical_end, ext_physical_end, ext_cyl);
#endif

		if (logical_end == physical_end ||
		    (end_cyl == 1023 && ext_physical_end == logical_end)) {
			geom[0] = end_head + 1;
			geom[1] = end_sector;
			geom[2] = (unsigned long)capacity /
				((end_head + 1) * end_sector);
			ret = true;
			goto out_free_buf;
		}
#ifdef DEBUG
		printk("scsicam_bios_param : logical (%u) != physical (%u)\n",
		       logical_end, physical_end);
#endif
	}

out_free_buf:
	kfree(buf);
	return ret;
}
EXPORT_SYMBOL(scsi_partsize);

/*
 * Function : static int setsize(unsigned long capacity,unsigned int *cyls,
 *      unsigned int *hds, unsigned int *secs);
 *
 * Purpose : to determine a near-optimal int 0x13 mapping for a
 *      SCSI disk in terms of lost space of size capacity, storing
 *      the results in *cyls, *hds, and *secs.
 *
 * Returns : -1 on failure, 0 on success.
 *
 * Extracted from
 *
 * WORKING                                                    X3T9.2
 * DRAFT                                                        792D
 * see http://www.t10.org/ftp/t10/drafts/cam/cam-r12b.pdf
 *
 *                                                        Revision 6
 *                                                         10-MAR-94
 * Information technology -
 * SCSI-2 Common access method
 * transport and SCSI interface module
 * 
 * ANNEX A :
 *
 * setsize() converts a read capacity value to int 13h
 * head-cylinder-sector requirements. It minimizes the value for
 * number of heads and maximizes the number of cylinders. This
 * will support rather large disks before the number of heads
 * will not fit in 4 bits (or 6 bits). This algorithm also
 * minimizes the number of sectors that will be unused at the end
 * of the disk while allowing for very large disks to be
 * accommodated. This algorithm does not use physical geometry. 
 */

static int setsize(unsigned long capacity, unsigned int *cyls, unsigned int *hds,
		   unsigned int *secs)
{
	unsigned int rv = 0;
	unsigned long heads, sectors, cylinders, temp;

	cylinders = 1024L;	/* Set number of cylinders to max */
	sectors = 62L;		/* Maximize sectors per track */

	temp = cylinders * sectors;	/* Compute divisor for heads */
	heads = capacity / temp;	/* Compute value for number of heads */
	if (capacity % temp) {	/* If no remainder, done! */
		heads++;	/* Else, increment number of heads */
		temp = cylinders * heads;	/* Compute divisor for sectors */
		sectors = capacity / temp;	/* Compute value for sectors per
						   track */
		if (capacity % temp) {	/* If no remainder, done! */
			sectors++;	/* Else, increment number of sectors */
			temp = heads * sectors;		/* Compute divisor for cylinders */
			cylinders = capacity / temp;	/* Compute number of cylinders */
		}
	}
	if (cylinders == 0)
		rv = (unsigned) -1;	/* Give error if 0 cylinders */

	*cyls = (unsigned int) cylinders;	/* Stuff return values */
	*secs = (unsigned int) sectors;
	*hds = (unsigned int) heads;
	return (rv);
}

/**
 * scsicam_bios_param - Determine geometry of a disk in cylinders/heads/sectors.
 * @bdev: which device
 * @capacity: size of the disk in sectors
 * @ip: return value: ip[0]=heads, ip[1]=sectors, ip[2]=cylinders
 *
 * Description : determine the BIOS mapping/geometry used for a drive in a
 *      SCSI-CAM system, storing the results in ip as required
 *      by the HDIO_GETGEO ioctl().
 *
 * Returns : -1 on failure, 0 on success.
 */
int scsicam_bios_param(struct block_device *bdev, sector_t capacity, int *ip)
{
	u64 capacity64 = capacity;	/* Suppress gcc warning */
	int ret = 0;

	/* try to infer mapping from partition table */
	if (scsi_partsize(bdev, capacity, ip))
		return 0;

	if (capacity64 < (1ULL << 32)) {
		/*
		 * Pick some standard mapping with at most 1024 cylinders, and
		 * at most 62 sectors per track - this works up to 7905 MB.
		 */
		ret = setsize((unsigned long)capacity, (unsigned int *)ip + 2,
		       (unsigned int *)ip + 0, (unsigned int *)ip + 1);
	}

	/*
	 * If something went wrong, then apparently we have to return a geometry
	 * with more than 1024 cylinders.
	 */
	if (ret || ip[0] > 255 || ip[1] > 63) {
		if ((capacity >> 11) > 65534) {
			ip[0] = 255;
			ip[1] = 63;
		} else {
			ip[0] = 64;
			ip[1] = 32;
		}

		if (capacity > 65535*63*255)
			ip[2] = 65535;
		else
			ip[2] = (unsigned long)capacity / (ip[0] * ip[1]);
	}

	return 0;
}
EXPORT_SYMBOL(scsicam_bios_param);
