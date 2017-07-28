/*
 * Copyright(c) 2015, 2016 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <linux/delay.h>
#include "hfi.h"
#include "common.h"
#include "eprom.h"

/*
 * The EPROM is logically divided into three partitions:
 *	partition 0: the first 128K, visible from PCI ROM BAR
 *	partition 1: 4K config file (sector size)
 *	partition 2: the rest
 */
#define P0_SIZE (128 * 1024)
#define P1_SIZE   (4 * 1024)
#define P1_START P0_SIZE
#define P2_START (P0_SIZE + P1_SIZE)

/* controller page size, in bytes */
#define EP_PAGE_SIZE 256
#define EP_PAGE_MASK (EP_PAGE_SIZE - 1)
#define EP_PAGE_DWORDS (EP_PAGE_SIZE / sizeof(u32))

/* controller commands */
#define CMD_SHIFT 24
#define CMD_NOP			    (0)
#define CMD_READ_DATA(addr)	    ((0x03 << CMD_SHIFT) | addr)
#define CMD_RELEASE_POWERDOWN_NOID  ((0xab << CMD_SHIFT))

/* controller interface speeds */
#define EP_SPEED_FULL 0x2	/* full speed */

/*
 * How long to wait for the EPROM to become available, in ms.
 * The spec 32 Mb EPROM takes around 40s to erase then write.
 * Double it for safety.
 */
#define EPROM_TIMEOUT 80000 /* ms */

/*
 * Read a 256 byte (64 dword) EPROM page.
 * All callers have verified the offset is at a page boundary.
 */
static void read_page(struct hfi1_devdata *dd, u32 offset, u32 *result)
{
	int i;

	write_csr(dd, ASIC_EEP_ADDR_CMD, CMD_READ_DATA(offset));
	for (i = 0; i < EP_PAGE_DWORDS; i++)
		result[i] = (u32)read_csr(dd, ASIC_EEP_DATA);
	write_csr(dd, ASIC_EEP_ADDR_CMD, CMD_NOP); /* close open page */
}

/*
 * Read length bytes starting at offset from the start of the EPROM.
 */
static int read_length(struct hfi1_devdata *dd, u32 start, u32 len, void *dest)
{
	u32 buffer[EP_PAGE_DWORDS];
	u32 end;
	u32 start_offset;
	u32 read_start;
	u32 bytes;

	if (len == 0)
		return 0;

	end = start + len;

	/*
	 * Make sure the read range is not outside of the controller read
	 * command address range.  Note that '>' is correct below - the end
	 * of the range is OK if it stops at the limit, but no higher.
	 */
	if (end > (1 << CMD_SHIFT))
		return -EINVAL;

	/* read the first partial page */
	start_offset = start & EP_PAGE_MASK;
	if (start_offset) {
		/* partial starting page */

		/* align and read the page that contains the start */
		read_start = start & ~EP_PAGE_MASK;
		read_page(dd, read_start, buffer);

		/* the rest of the page is available data */
		bytes = EP_PAGE_SIZE - start_offset;

		if (len <= bytes) {
			/* end is within this page */
			memcpy(dest, (u8 *)buffer + start_offset, len);
			return 0;
		}

		memcpy(dest, (u8 *)buffer + start_offset, bytes);

		start += bytes;
		len -= bytes;
		dest += bytes;
	}
	/* start is now page aligned */

	/* read whole pages */
	while (len >= EP_PAGE_SIZE) {
		read_page(dd, start, buffer);
		memcpy(dest, buffer, EP_PAGE_SIZE);

		start += EP_PAGE_SIZE;
		len -= EP_PAGE_SIZE;
		dest += EP_PAGE_SIZE;
	}

	/* read the last partial page */
	if (len) {
		read_page(dd, start, buffer);
		memcpy(dest, buffer, len);
	}

	return 0;
}

/*
 * Initialize the EPROM handler.
 */
int eprom_init(struct hfi1_devdata *dd)
{
	int ret = 0;

	/* only the discrete chip has an EPROM */
	if (dd->pcidev->device != PCI_DEVICE_ID_INTEL0)
		return 0;

	/*
	 * It is OK if both HFIs reset the EPROM as long as they don't
	 * do it at the same time.
	 */
	ret = acquire_chip_resource(dd, CR_EPROM, EPROM_TIMEOUT);
	if (ret) {
		dd_dev_err(dd,
			   "%s: unable to acquire EPROM resource, no EPROM support\n",
			   __func__);
		goto done_asic;
	}

	/* reset EPROM to be sure it is in a good state */

	/* set reset */
	write_csr(dd, ASIC_EEP_CTL_STAT, ASIC_EEP_CTL_STAT_EP_RESET_SMASK);
	/* clear reset, set speed */
	write_csr(dd, ASIC_EEP_CTL_STAT,
		  EP_SPEED_FULL << ASIC_EEP_CTL_STAT_RATE_SPI_SHIFT);

	/* wake the device with command "release powerdown NoID" */
	write_csr(dd, ASIC_EEP_ADDR_CMD, CMD_RELEASE_POWERDOWN_NOID);

	dd->eprom_available = true;
	release_chip_resource(dd, CR_EPROM);
done_asic:
	return ret;
}

/* magic character sequence that trails an image */
#define IMAGE_TRAIL_MAGIC "egamiAPO"

/* EPROM file types */
#define HFI1_EFT_PLATFORM_CONFIG 2

/* segment size - 128 KiB */
#define SEG_SIZE (128 * 1024)

struct hfi1_eprom_footer {
	u32 oprom_size;		/* size of the oprom, in bytes */
	u16 num_table_entries;
	u16 version;		/* version of this footer */
	u32 magic;		/* must be last */
};

struct hfi1_eprom_table_entry {
	u32 type;		/* file type */
	u32 offset;		/* file offset from start of EPROM */
	u32 size;		/* file size, in bytes */
};

/*
 * Calculate the max number of table entries that will fit within a directory
 * buffer of size 'dir_size'.
 */
#define MAX_TABLE_ENTRIES(dir_size) \
	(((dir_size) - sizeof(struct hfi1_eprom_footer)) / \
		sizeof(struct hfi1_eprom_table_entry))

#define DIRECTORY_SIZE(n) (sizeof(struct hfi1_eprom_footer) + \
	(sizeof(struct hfi1_eprom_table_entry) * (n)))

#define MAGIC4(a, b, c, d) ((d) << 24 | (c) << 16 | (b) << 8 | (a))
#define FOOTER_MAGIC MAGIC4('e', 'p', 'r', 'm')
#define FOOTER_VERSION 1

/*
 * Read all of partition 1.  The actual file is at the front.  Adjust
 * the returned size if a trailing image magic is found.
 */
static int read_partition_platform_config(struct hfi1_devdata *dd, void **data,
					  u32 *size)
{
	void *buffer;
	void *p;
	u32 length;
	int ret;

	buffer = kmalloc(P1_SIZE, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	ret = read_length(dd, P1_START, P1_SIZE, buffer);
	if (ret) {
		kfree(buffer);
		return ret;
	}

	/* scan for image magic that may trail the actual data */
	p = strnstr(buffer, IMAGE_TRAIL_MAGIC, P1_SIZE);
	if (p)
		length = p - buffer;
	else
		length = P1_SIZE;

	*data = buffer;
	*size = length;
	return 0;
}

/*
 * The segment magic has been checked.  There is a footer and table of
 * contents present.
 *
 * directory is a u32 aligned buffer of size EP_PAGE_SIZE.
 */
static int read_segment_platform_config(struct hfi1_devdata *dd,
					void *directory, void **data, u32 *size)
{
	struct hfi1_eprom_footer *footer;
	struct hfi1_eprom_table_entry *table;
	struct hfi1_eprom_table_entry *entry;
	void *buffer = NULL;
	void *table_buffer = NULL;
	int ret, i;
	u32 directory_size;
	u32 seg_base, seg_offset;
	u32 bytes_available, ncopied, to_copy;

	/* the footer is at the end of the directory */
	footer = (struct hfi1_eprom_footer *)
			(directory + EP_PAGE_SIZE - sizeof(*footer));

	/* make sure the structure version is supported */
	if (footer->version != FOOTER_VERSION)
		return -EINVAL;

	/* oprom size cannot be larger than a segment */
	if (footer->oprom_size >= SEG_SIZE)
		return -EINVAL;

	/* the file table must fit in a segment with the oprom */
	if (footer->num_table_entries >
			MAX_TABLE_ENTRIES(SEG_SIZE - footer->oprom_size))
		return -EINVAL;

	/* find the file table start, which precedes the footer */
	directory_size = DIRECTORY_SIZE(footer->num_table_entries);
	if (directory_size <= EP_PAGE_SIZE) {
		/* the file table fits into the directory buffer handed in */
		table = (struct hfi1_eprom_table_entry *)
				(directory + EP_PAGE_SIZE - directory_size);
	} else {
		/* need to allocate and read more */
		table_buffer = kmalloc(directory_size, GFP_KERNEL);
		if (!table_buffer)
			return -ENOMEM;
		ret = read_length(dd, SEG_SIZE - directory_size,
				  directory_size, table_buffer);
		if (ret)
			goto done;
		table = table_buffer;
	}

	/* look for the platform configuration file in the table */
	for (entry = NULL, i = 0; i < footer->num_table_entries; i++) {
		if (table[i].type == HFI1_EFT_PLATFORM_CONFIG) {
			entry = &table[i];
			break;
		}
	}
	if (!entry) {
		ret = -ENOENT;
		goto done;
	}

	/*
	 * Sanity check on the configuration file size - it should never
	 * be larger than 4 KiB.
	 */
	if (entry->size > (4 * 1024)) {
		dd_dev_err(dd, "Bad configuration file size 0x%x\n",
			   entry->size);
		ret = -EINVAL;
		goto done;
	}

	/* check for bogus offset and size that wrap when added together */
	if (entry->offset + entry->size < entry->offset) {
		dd_dev_err(dd,
			   "Bad configuration file start + size 0x%x+0x%x\n",
			   entry->offset, entry->size);
		ret = -EINVAL;
		goto done;
	}

	/* allocate the buffer to return */
	buffer = kmalloc(entry->size, GFP_KERNEL);
	if (!buffer) {
		ret = -ENOMEM;
		goto done;
	}

	/*
	 * Extract the file by looping over segments until it is fully read.
	 */
	seg_offset = entry->offset % SEG_SIZE;
	seg_base = entry->offset - seg_offset;
	ncopied = 0;
	while (ncopied < entry->size) {
		/* calculate data bytes available in this segment */

		/* start with the bytes from the current offset to the end */
		bytes_available = SEG_SIZE - seg_offset;
		/* subtract off footer and table from segment 0 */
		if (seg_base == 0) {
			/*
			 * Sanity check: should not have a starting point
			 * at or within the directory.
			 */
			if (bytes_available <= directory_size) {
				dd_dev_err(dd,
					   "Bad configuration file - offset 0x%x within footer+table\n",
					   entry->offset);
				ret = -EINVAL;
				goto done;
			}
			bytes_available -= directory_size;
		}

		/* calculate bytes wanted */
		to_copy = entry->size - ncopied;

		/* max out at the available bytes in this segment */
		if (to_copy > bytes_available)
			to_copy = bytes_available;

		/*
		 * Read from the EPROM.
		 *
		 * The sanity check for entry->offset is done in read_length().
		 * The EPROM offset is validated against what the hardware
		 * addressing supports.  In addition, if the offset is larger
		 * than the actual EPROM, it silently wraps.  It will work
		 * fine, though the reader may not get what they expected
		 * from the EPROM.
		 */
		ret = read_length(dd, seg_base + seg_offset, to_copy,
				  buffer + ncopied);
		if (ret)
			goto done;

		ncopied += to_copy;

		/* set up for next segment */
		seg_offset = footer->oprom_size;
		seg_base += SEG_SIZE;
	}

	/* success */
	ret = 0;
	*data = buffer;
	*size = entry->size;

done:
	kfree(table_buffer);
	if (ret)
		kfree(buffer);
	return ret;
}

/*
 * Read the platform configuration file from the EPROM.
 *
 * On success, an allocated buffer containing the data and its size are
 * returned.  It is up to the caller to free this buffer.
 *
 * Return value:
 *   0	      - success
 *   -ENXIO   - no EPROM is available
 *   -EBUSY   - not able to acquire access to the EPROM
 *   -ENOENT  - no recognizable file written
 *   -ENOMEM  - buffer could not be allocated
 *   -EINVAL  - invalid EPROM contentents found
 */
int eprom_read_platform_config(struct hfi1_devdata *dd, void **data, u32 *size)
{
	u32 directory[EP_PAGE_DWORDS]; /* aligned buffer */
	int ret;

	if (!dd->eprom_available)
		return -ENXIO;

	ret = acquire_chip_resource(dd, CR_EPROM, EPROM_TIMEOUT);
	if (ret)
		return -EBUSY;

	/* read the last page of the segment for the EPROM format magic */
	ret = read_length(dd, SEG_SIZE - EP_PAGE_SIZE, EP_PAGE_SIZE, directory);
	if (ret)
		goto done;

	/* last dword of the segment contains a magic value */
	if (directory[EP_PAGE_DWORDS - 1] == FOOTER_MAGIC) {
		/* segment format */
		ret = read_segment_platform_config(dd, directory, data, size);
	} else {
		/* partition format */
		ret = read_partition_platform_config(dd, data, size);
	}

done:
	release_chip_resource(dd, CR_EPROM);
	return ret;
}
