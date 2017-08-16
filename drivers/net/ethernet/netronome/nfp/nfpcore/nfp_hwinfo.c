/*
 * Copyright (C) 2015-2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* Parse the hwinfo table that the ARM firmware builds in the ARM scratch SRAM
 * after chip reset.
 *
 * Examples of the fields:
 *   me.count = 40
 *   me.mask = 0x7f_ffff_ffff
 *
 *   me.count is the total number of MEs on the system.
 *   me.mask is the bitmask of MEs that are available for application usage.
 *
 *   (ie, in this example, ME 39 has been reserved by boardconfig.)
 */

#include <asm/byteorder.h>
#include <asm/unaligned.h>
#include <linux/delay.h>
#include <linux/log2.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#define NFP_SUBSYS "nfp_hwinfo"

#include "crc32.h"
#include "nfp.h"
#include "nfp_cpp.h"
#include "nfp6000/nfp6000.h"

#define HWINFO_SIZE_MIN	0x100
#define HWINFO_WAIT	20	/* seconds */

/* The Hardware Info Table defines the properties of the system.
 *
 * HWInfo v1 Table (fixed size)
 *
 * 0x0000: u32 version	        Hardware Info Table version (1.0)
 * 0x0004: u32 size	        Total size of the table, including
 *			        the CRC32 (IEEE 802.3)
 * 0x0008: u32 jumptab	        Offset of key/value table
 * 0x000c: u32 keys	        Total number of keys in the key/value table
 * NNNNNN:		        Key/value jump table and string data
 * (size - 4): u32 crc32	CRC32 (same as IEEE 802.3, POSIX csum, etc)
 *				CRC32("",0) = ~0, CRC32("a",1) = 0x48C279FE
 *
 * HWInfo v2 Table (variable size)
 *
 * 0x0000: u32 version	        Hardware Info Table version (2.0)
 * 0x0004: u32 size	        Current size of the data area, excluding CRC32
 * 0x0008: u32 limit	        Maximum size of the table
 * 0x000c: u32 reserved	        Unused, set to zero
 * NNNNNN:			Key/value data
 * (size - 4): u32 crc32	CRC32 (same as IEEE 802.3, POSIX csum, etc)
 *				CRC32("",0) = ~0, CRC32("a",1) = 0x48C279FE
 *
 * If the HWInfo table is in the process of being updated, the low bit
 * of version will be set.
 *
 * HWInfo v1 Key/Value Table
 * -------------------------
 *
 *  The key/value table is a set of offsets to ASCIIZ strings which have
 *  been strcmp(3) sorted (yes, please use bsearch(3) on the table).
 *
 *  All keys are guaranteed to be unique.
 *
 * N+0:	u32 key_1		Offset to the first key
 * N+4:	u32 val_1		Offset to the first value
 * N+8: u32 key_2		Offset to the second key
 * N+c: u32 val_2		Offset to the second value
 * ...
 *
 * HWInfo v2 Key/Value Table
 * -------------------------
 *
 * Packed UTF8Z strings, ie 'key1\000value1\000key2\000value2\000'
 *
 * Unsorted.
 */

#define NFP_HWINFO_VERSION_1 ('H' << 24 | 'I' << 16 | 1 << 8 | 0 << 1 | 0)
#define NFP_HWINFO_VERSION_2 ('H' << 24 | 'I' << 16 | 2 << 8 | 0 << 1 | 0)
#define NFP_HWINFO_VERSION_UPDATING	BIT(0)

struct nfp_hwinfo {
	u8 start[0];

	__le32 version;
	__le32 size;

	/* v2 specific fields */
	__le32 limit;
	__le32 resv;

	char data[];
};

static bool nfp_hwinfo_is_updating(struct nfp_hwinfo *hwinfo)
{
	return le32_to_cpu(hwinfo->version) & NFP_HWINFO_VERSION_UPDATING;
}

static int
hwinfo_db_walk(struct nfp_cpp *cpp, struct nfp_hwinfo *hwinfo, u32 size)
{
	const char *key, *val, *end = hwinfo->data + size;

	for (key = hwinfo->data; *key && key < end;
	     key = val + strlen(val) + 1) {

		val = key + strlen(key) + 1;
		if (val >= end) {
			nfp_warn(cpp, "Bad HWINFO - overflowing key\n");
			return -EINVAL;
		}

		if (val + strlen(val) + 1 > end) {
			nfp_warn(cpp, "Bad HWINFO - overflowing value\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int
hwinfo_db_validate(struct nfp_cpp *cpp, struct nfp_hwinfo *db, u32 len)
{
	u32 size, crc;

	size = le32_to_cpu(db->size);
	if (size > len) {
		nfp_err(cpp, "Unsupported hwinfo size %u > %u\n", size, len);
		return -EINVAL;
	}

	size -= sizeof(u32);
	crc = crc32_posix(db, size);
	if (crc != get_unaligned_le32(db->start + size)) {
		nfp_err(cpp, "Corrupt hwinfo table (CRC mismatch), calculated 0x%x, expected 0x%x\n",
			crc, get_unaligned_le32(db->start + size));

		return -EINVAL;
	}

	return hwinfo_db_walk(cpp, db, size);
}

static struct nfp_hwinfo *
hwinfo_try_fetch(struct nfp_cpp *cpp, size_t *cpp_size)
{
	struct nfp_hwinfo *header;
	struct nfp_resource *res;
	u64 cpp_addr;
	u32 cpp_id;
	int err;
	u8 *db;

	res = nfp_resource_acquire(cpp, NFP_RESOURCE_NFP_HWINFO);
	if (!IS_ERR(res)) {
		cpp_id = nfp_resource_cpp_id(res);
		cpp_addr = nfp_resource_address(res);
		*cpp_size = nfp_resource_size(res);

		nfp_resource_release(res);

		if (*cpp_size < HWINFO_SIZE_MIN)
			return NULL;
	} else if (PTR_ERR(res) == -ENOENT) {
		/* Try getting the HWInfo table from the 'classic' location */
		cpp_id = NFP_CPP_ISLAND_ID(NFP_CPP_TARGET_MU,
					   NFP_CPP_ACTION_RW, 0, 1);
		cpp_addr = 0x30000;
		*cpp_size = 0x0e000;
	} else {
		return NULL;
	}

	db = kmalloc(*cpp_size + 1, GFP_KERNEL);
	if (!db)
		return NULL;

	err = nfp_cpp_read(cpp, cpp_id, cpp_addr, db, *cpp_size);
	if (err != *cpp_size)
		goto exit_free;

	header = (void *)db;
	if (nfp_hwinfo_is_updating(header))
		goto exit_free;

	if (le32_to_cpu(header->version) != NFP_HWINFO_VERSION_2) {
		nfp_err(cpp, "Unknown HWInfo version: 0x%08x\n",
			le32_to_cpu(header->version));
		goto exit_free;
	}

	/* NULL-terminate for safety */
	db[*cpp_size] = '\0';

	return (void *)db;
exit_free:
	kfree(db);
	return NULL;
}

static struct nfp_hwinfo *hwinfo_fetch(struct nfp_cpp *cpp, size_t *hwdb_size)
{
	const unsigned long wait_until = jiffies + HWINFO_WAIT * HZ;
	struct nfp_hwinfo *db;
	int err;

	for (;;) {
		const unsigned long start_time = jiffies;

		db = hwinfo_try_fetch(cpp, hwdb_size);
		if (db)
			return db;

		err = msleep_interruptible(100);
		if (err || time_after(start_time, wait_until)) {
			nfp_err(cpp, "NFP access error\n");
			return NULL;
		}
	}
}

struct nfp_hwinfo *nfp_hwinfo_read(struct nfp_cpp *cpp)
{
	struct nfp_hwinfo *db;
	size_t hwdb_size = 0;
	int err;

	db = hwinfo_fetch(cpp, &hwdb_size);
	if (!db)
		return NULL;

	err = hwinfo_db_validate(cpp, db, hwdb_size);
	if (err) {
		kfree(db);
		return NULL;
	}

	return db;
}

/**
 * nfp_hwinfo_lookup() - Find a value in the HWInfo table by name
 * @hwinfo:	NFP HWinfo table
 * @lookup:	HWInfo name to search for
 *
 * Return: Value of the HWInfo name, or NULL
 */
const char *nfp_hwinfo_lookup(struct nfp_hwinfo *hwinfo, const char *lookup)
{
	const char *key, *val, *end;

	if (!hwinfo || !lookup)
		return NULL;

	end = hwinfo->data + le32_to_cpu(hwinfo->size) - sizeof(u32);

	for (key = hwinfo->data; *key && key < end;
	     key = val + strlen(val) + 1) {

		val = key + strlen(key) + 1;

		if (strcmp(key, lookup) == 0)
			return val;
	}

	return NULL;
}
