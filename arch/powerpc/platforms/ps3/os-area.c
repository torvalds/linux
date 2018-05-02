/*
 *  PS3 flash memory os area.
 *
 *  Copyright (C) 2006 Sony Computer Entertainment Inc.
 *  Copyright 2006 Sony Corp.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/workqueue.h>
#include <linux/fs.h>
#include <linux/syscalls.h>
#include <linux/export.h>
#include <linux/ctype.h>
#include <linux/memblock.h>
#include <linux/of.h>
#include <linux/slab.h>

#include <asm/prom.h>

#include "platform.h"

enum {
	OS_AREA_SEGMENT_SIZE = 0X200,
};

enum os_area_ldr_format {
	HEADER_LDR_FORMAT_RAW = 0,
	HEADER_LDR_FORMAT_GZIP = 1,
};

#define OS_AREA_HEADER_MAGIC_NUM "cell_ext_os_area"

/**
 * struct os_area_header - os area header segment.
 * @magic_num: Always 'cell_ext_os_area'.
 * @hdr_version: Header format version number.
 * @db_area_offset: Starting segment number of other os database area.
 * @ldr_area_offset: Starting segment number of bootloader image area.
 * @ldr_format: HEADER_LDR_FORMAT flag.
 * @ldr_size: Size of bootloader image in bytes.
 *
 * Note that the docs refer to area offsets.  These are offsets in units of
 * segments from the start of the os area (top of the header).  These are
 * better thought of as segment numbers.  The os area of the os area is
 * reserved for the os image.
 */

struct os_area_header {
	u8 magic_num[16];
	u32 hdr_version;
	u32 db_area_offset;
	u32 ldr_area_offset;
	u32 _reserved_1;
	u32 ldr_format;
	u32 ldr_size;
	u32 _reserved_2[6];
};

enum os_area_boot_flag {
	PARAM_BOOT_FLAG_GAME_OS = 0,
	PARAM_BOOT_FLAG_OTHER_OS = 1,
};

enum os_area_ctrl_button {
	PARAM_CTRL_BUTTON_O_IS_YES = 0,
	PARAM_CTRL_BUTTON_X_IS_YES = 1,
};

/**
 * struct os_area_params - os area params segment.
 * @boot_flag: User preference of operating system, PARAM_BOOT_FLAG flag.
 * @num_params: Number of params in this (params) segment.
 * @rtc_diff: Difference in seconds between 1970 and the ps3 rtc value.
 * @av_multi_out: User preference of AV output, PARAM_AV_MULTI_OUT flag.
 * @ctrl_button: User preference of controller button config, PARAM_CTRL_BUTTON
 *	flag.
 * @static_ip_addr: User preference of static IP address.
 * @network_mask: User preference of static network mask.
 * @default_gateway: User preference of static default gateway.
 * @dns_primary: User preference of static primary dns server.
 * @dns_secondary: User preference of static secondary dns server.
 *
 * The ps3 rtc maintains a read-only value that approximates seconds since
 * 2000-01-01 00:00:00 UTC.
 *
 * User preference of zero for static_ip_addr means use dhcp.
 */

struct os_area_params {
	u32 boot_flag;
	u32 _reserved_1[3];
	u32 num_params;
	u32 _reserved_2[3];
	/* param 0 */
	s64 rtc_diff;
	u8 av_multi_out;
	u8 ctrl_button;
	u8 _reserved_3[6];
	/* param 1 */
	u8 static_ip_addr[4];
	u8 network_mask[4];
	u8 default_gateway[4];
	u8 _reserved_4[4];
	/* param 2 */
	u8 dns_primary[4];
	u8 dns_secondary[4];
	u8 _reserved_5[8];
};

#define OS_AREA_DB_MAGIC_NUM "-db-"

/**
 * struct os_area_db - Shared flash memory database.
 * @magic_num: Always '-db-'.
 * @version: os_area_db format version number.
 * @index_64: byte offset of the database id index for 64 bit variables.
 * @count_64: number of usable 64 bit index entries
 * @index_32: byte offset of the database id index for 32 bit variables.
 * @count_32: number of usable 32 bit index entries
 * @index_16: byte offset of the database id index for 16 bit variables.
 * @count_16: number of usable 16 bit index entries
 *
 * Flash rom storage for exclusive use by guests running in the other os lpar.
 * The current system configuration allocates 1K (two segments) for other os
 * use.
 */

struct os_area_db {
	u8 magic_num[4];
	u16 version;
	u16 _reserved_1;
	u16 index_64;
	u16 count_64;
	u16 index_32;
	u16 count_32;
	u16 index_16;
	u16 count_16;
	u32 _reserved_2;
	u8 _db_data[1000];
};

/**
 * enum os_area_db_owner - Data owners.
 */

enum os_area_db_owner {
	OS_AREA_DB_OWNER_ANY = -1,
	OS_AREA_DB_OWNER_NONE = 0,
	OS_AREA_DB_OWNER_PROTOTYPE = 1,
	OS_AREA_DB_OWNER_LINUX = 2,
	OS_AREA_DB_OWNER_PETITBOOT = 3,
	OS_AREA_DB_OWNER_MAX = 32,
};

enum os_area_db_key {
	OS_AREA_DB_KEY_ANY = -1,
	OS_AREA_DB_KEY_NONE = 0,
	OS_AREA_DB_KEY_RTC_DIFF = 1,
	OS_AREA_DB_KEY_VIDEO_MODE = 2,
	OS_AREA_DB_KEY_MAX = 8,
};

struct os_area_db_id {
	int owner;
	int key;
};

static const struct os_area_db_id os_area_db_id_empty = {
	.owner = OS_AREA_DB_OWNER_NONE,
	.key = OS_AREA_DB_KEY_NONE
};

static const struct os_area_db_id os_area_db_id_any = {
	.owner = OS_AREA_DB_OWNER_ANY,
	.key = OS_AREA_DB_KEY_ANY
};

static const struct os_area_db_id os_area_db_id_rtc_diff = {
	.owner = OS_AREA_DB_OWNER_LINUX,
	.key = OS_AREA_DB_KEY_RTC_DIFF
};

#define SECONDS_FROM_1970_TO_2000 946684800LL

/**
 * struct saved_params - Static working copies of data from the PS3 'os area'.
 *
 * The order of preference we use for the rtc_diff source:
 *  1) The database value.
 *  2) The game os value.
 *  3) The number of seconds from 1970 to 2000.
 */

struct saved_params {
	unsigned int valid;
	s64 rtc_diff;
	unsigned int av_multi_out;
} static saved_params;

static struct property property_rtc_diff = {
	.name = "linux,rtc_diff",
	.length = sizeof(saved_params.rtc_diff),
	.value = &saved_params.rtc_diff,
};

static struct property property_av_multi_out = {
	.name = "linux,av_multi_out",
	.length = sizeof(saved_params.av_multi_out),
	.value = &saved_params.av_multi_out,
};


static DEFINE_MUTEX(os_area_flash_mutex);

static const struct ps3_os_area_flash_ops *os_area_flash_ops;

void ps3_os_area_flash_register(const struct ps3_os_area_flash_ops *ops)
{
	mutex_lock(&os_area_flash_mutex);
	os_area_flash_ops = ops;
	mutex_unlock(&os_area_flash_mutex);
}
EXPORT_SYMBOL_GPL(ps3_os_area_flash_register);

static ssize_t os_area_flash_read(void *buf, size_t count, loff_t pos)
{
	ssize_t res = -ENODEV;

	mutex_lock(&os_area_flash_mutex);
	if (os_area_flash_ops)
		res = os_area_flash_ops->read(buf, count, pos);
	mutex_unlock(&os_area_flash_mutex);

	return res;
}

static ssize_t os_area_flash_write(const void *buf, size_t count, loff_t pos)
{
	ssize_t res = -ENODEV;

	mutex_lock(&os_area_flash_mutex);
	if (os_area_flash_ops)
		res = os_area_flash_ops->write(buf, count, pos);
	mutex_unlock(&os_area_flash_mutex);

	return res;
}


/**
 * os_area_set_property - Add or overwrite a saved_params value to the device tree.
 *
 * Overwrites an existing property.
 */

static void os_area_set_property(struct device_node *node,
	struct property *prop)
{
	int result;
	struct property *tmp = of_find_property(node, prop->name, NULL);

	if (tmp) {
		pr_debug("%s:%d found %s\n", __func__, __LINE__, prop->name);
		of_remove_property(node, tmp);
	}

	result = of_add_property(node, prop);

	if (result)
		pr_debug("%s:%d of_set_property failed\n", __func__,
			__LINE__);
}

/**
 * os_area_get_property - Get a saved_params value from the device tree.
 *
 */

static void __init os_area_get_property(struct device_node *node,
	struct property *prop)
{
	const struct property *tmp = of_find_property(node, prop->name, NULL);

	if (tmp) {
		BUG_ON(prop->length != tmp->length);
		memcpy(prop->value, tmp->value, prop->length);
	} else
		pr_debug("%s:%d not found %s\n", __func__, __LINE__,
			prop->name);
}

static void dump_field(char *s, const u8 *field, int size_of_field)
{
#if defined(DEBUG)
	int i;

	for (i = 0; i < size_of_field; i++)
		s[i] = isprint(field[i]) ? field[i] : '.';
	s[i] = 0;
#endif
}

#define dump_header(_a) _dump_header(_a, __func__, __LINE__)
static void _dump_header(const struct os_area_header *h, const char *func,
	int line)
{
	char str[sizeof(h->magic_num) + 1];

	dump_field(str, h->magic_num, sizeof(h->magic_num));
	pr_debug("%s:%d: h.magic_num:       '%s'\n", func, line,
		str);
	pr_debug("%s:%d: h.hdr_version:     %u\n", func, line,
		h->hdr_version);
	pr_debug("%s:%d: h.db_area_offset:  %u\n", func, line,
		h->db_area_offset);
	pr_debug("%s:%d: h.ldr_area_offset: %u\n", func, line,
		h->ldr_area_offset);
	pr_debug("%s:%d: h.ldr_format:      %u\n", func, line,
		h->ldr_format);
	pr_debug("%s:%d: h.ldr_size:        %xh\n", func, line,
		h->ldr_size);
}

#define dump_params(_a) _dump_params(_a, __func__, __LINE__)
static void _dump_params(const struct os_area_params *p, const char *func,
	int line)
{
	pr_debug("%s:%d: p.boot_flag:       %u\n", func, line, p->boot_flag);
	pr_debug("%s:%d: p.num_params:      %u\n", func, line, p->num_params);
	pr_debug("%s:%d: p.rtc_diff         %lld\n", func, line, p->rtc_diff);
	pr_debug("%s:%d: p.av_multi_out     %u\n", func, line, p->av_multi_out);
	pr_debug("%s:%d: p.ctrl_button:     %u\n", func, line, p->ctrl_button);
	pr_debug("%s:%d: p.static_ip_addr:  %u.%u.%u.%u\n", func, line,
		p->static_ip_addr[0], p->static_ip_addr[1],
		p->static_ip_addr[2], p->static_ip_addr[3]);
	pr_debug("%s:%d: p.network_mask:    %u.%u.%u.%u\n", func, line,
		p->network_mask[0], p->network_mask[1],
		p->network_mask[2], p->network_mask[3]);
	pr_debug("%s:%d: p.default_gateway: %u.%u.%u.%u\n", func, line,
		p->default_gateway[0], p->default_gateway[1],
		p->default_gateway[2], p->default_gateway[3]);
	pr_debug("%s:%d: p.dns_primary:     %u.%u.%u.%u\n", func, line,
		p->dns_primary[0], p->dns_primary[1],
		p->dns_primary[2], p->dns_primary[3]);
	pr_debug("%s:%d: p.dns_secondary:   %u.%u.%u.%u\n", func, line,
		p->dns_secondary[0], p->dns_secondary[1],
		p->dns_secondary[2], p->dns_secondary[3]);
}

static int verify_header(const struct os_area_header *header)
{
	if (memcmp(header->magic_num, OS_AREA_HEADER_MAGIC_NUM,
		sizeof(header->magic_num))) {
		pr_debug("%s:%d magic_num failed\n", __func__, __LINE__);
		return -1;
	}

	if (header->hdr_version < 1) {
		pr_debug("%s:%d hdr_version failed\n", __func__, __LINE__);
		return -1;
	}

	if (header->db_area_offset > header->ldr_area_offset) {
		pr_debug("%s:%d offsets failed\n", __func__, __LINE__);
		return -1;
	}

	return 0;
}

static int db_verify(const struct os_area_db *db)
{
	if (memcmp(db->magic_num, OS_AREA_DB_MAGIC_NUM,
		sizeof(db->magic_num))) {
		pr_debug("%s:%d magic_num failed\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (db->version != 1) {
		pr_debug("%s:%d version failed\n", __func__, __LINE__);
		return -EINVAL;
	}

	return 0;
}

struct db_index {
       uint8_t owner:5;
       uint8_t key:3;
};

struct db_iterator {
	const struct os_area_db *db;
	struct os_area_db_id match_id;
	struct db_index *idx;
	struct db_index *last_idx;
	union {
		uint64_t *value_64;
		uint32_t *value_32;
		uint16_t *value_16;
	};
};

static unsigned int db_align_up(unsigned int val, unsigned int size)
{
	return (val + (size - 1)) & (~(size - 1));
}

/**
 * db_for_each_64 - Iterator for 64 bit entries.
 *
 * A NULL value for id can be used to match all entries.
 * OS_AREA_DB_OWNER_ANY and OS_AREA_DB_KEY_ANY can be used to match all.
 */

static int db_for_each_64(const struct os_area_db *db,
	const struct os_area_db_id *match_id, struct db_iterator *i)
{
next:
	if (!i->db) {
		i->db = db;
		i->match_id = match_id ? *match_id : os_area_db_id_any;
		i->idx = (void *)db + db->index_64;
		i->last_idx = i->idx + db->count_64;
		i->value_64 = (void *)db + db->index_64
			+ db_align_up(db->count_64, 8);
	} else {
		i->idx++;
		i->value_64++;
	}

	if (i->idx >= i->last_idx) {
		pr_debug("%s:%d: reached end\n", __func__, __LINE__);
		return 0;
	}

	if (i->match_id.owner != OS_AREA_DB_OWNER_ANY
		&& i->match_id.owner != (int)i->idx->owner)
		goto next;
	if (i->match_id.key != OS_AREA_DB_KEY_ANY
		&& i->match_id.key != (int)i->idx->key)
		goto next;

	return 1;
}

static int db_delete_64(struct os_area_db *db, const struct os_area_db_id *id)
{
	struct db_iterator i;

	for (i.db = NULL; db_for_each_64(db, id, &i); ) {

		pr_debug("%s:%d: got (%d:%d) %llxh\n", __func__, __LINE__,
			i.idx->owner, i.idx->key,
			(unsigned long long)*i.value_64);

		i.idx->owner = 0;
		i.idx->key = 0;
		*i.value_64 = 0;
	}
	return 0;
}

static int db_set_64(struct os_area_db *db, const struct os_area_db_id *id,
	uint64_t value)
{
	struct db_iterator i;

	pr_debug("%s:%d: (%d:%d) <= %llxh\n", __func__, __LINE__,
		id->owner, id->key, (unsigned long long)value);

	if (!id->owner || id->owner == OS_AREA_DB_OWNER_ANY
		|| id->key == OS_AREA_DB_KEY_ANY) {
		pr_debug("%s:%d: bad id: (%d:%d)\n", __func__,
			__LINE__, id->owner, id->key);
		return -1;
	}

	db_delete_64(db, id);

	i.db = NULL;
	if (db_for_each_64(db, &os_area_db_id_empty, &i)) {

		pr_debug("%s:%d: got (%d:%d) %llxh\n", __func__, __LINE__,
			i.idx->owner, i.idx->key,
			(unsigned long long)*i.value_64);

		i.idx->owner = id->owner;
		i.idx->key = id->key;
		*i.value_64 = value;

		pr_debug("%s:%d: set (%d:%d) <= %llxh\n", __func__, __LINE__,
			i.idx->owner, i.idx->key,
			(unsigned long long)*i.value_64);
		return 0;
	}
	pr_debug("%s:%d: database full.\n",
		__func__, __LINE__);
	return -1;
}

static int db_get_64(const struct os_area_db *db,
	const struct os_area_db_id *id, uint64_t *value)
{
	struct db_iterator i;

	i.db = NULL;
	if (db_for_each_64(db, id, &i)) {
		*value = *i.value_64;
		pr_debug("%s:%d: found %lld\n", __func__, __LINE__,
				(long long int)*i.value_64);
		return 0;
	}
	pr_debug("%s:%d: not found\n", __func__, __LINE__);
	return -1;
}

static int db_get_rtc_diff(const struct os_area_db *db, int64_t *rtc_diff)
{
	return db_get_64(db, &os_area_db_id_rtc_diff, (uint64_t*)rtc_diff);
}

#define dump_db(a) _dump_db(a, __func__, __LINE__)
static void _dump_db(const struct os_area_db *db, const char *func,
	int line)
{
	char str[sizeof(db->magic_num) + 1];

	dump_field(str, db->magic_num, sizeof(db->magic_num));
	pr_debug("%s:%d: db.magic_num:      '%s'\n", func, line,
		str);
	pr_debug("%s:%d: db.version:         %u\n", func, line,
		db->version);
	pr_debug("%s:%d: db.index_64:        %u\n", func, line,
		db->index_64);
	pr_debug("%s:%d: db.count_64:        %u\n", func, line,
		db->count_64);
	pr_debug("%s:%d: db.index_32:        %u\n", func, line,
		db->index_32);
	pr_debug("%s:%d: db.count_32:        %u\n", func, line,
		db->count_32);
	pr_debug("%s:%d: db.index_16:        %u\n", func, line,
		db->index_16);
	pr_debug("%s:%d: db.count_16:        %u\n", func, line,
		db->count_16);
}

static void os_area_db_init(struct os_area_db *db)
{
	enum {
		HEADER_SIZE = offsetof(struct os_area_db, _db_data),
		INDEX_64_COUNT = 64,
		VALUES_64_COUNT = 57,
		INDEX_32_COUNT = 64,
		VALUES_32_COUNT = 57,
		INDEX_16_COUNT = 64,
		VALUES_16_COUNT = 57,
	};

	memset(db, 0, sizeof(struct os_area_db));

	memcpy(db->magic_num, OS_AREA_DB_MAGIC_NUM, sizeof(db->magic_num));
	db->version = 1;
	db->index_64 = HEADER_SIZE;
	db->count_64 = VALUES_64_COUNT;
	db->index_32 = HEADER_SIZE
			+ INDEX_64_COUNT * sizeof(struct db_index)
			+ VALUES_64_COUNT * sizeof(u64);
	db->count_32 = VALUES_32_COUNT;
	db->index_16 = HEADER_SIZE
			+ INDEX_64_COUNT * sizeof(struct db_index)
			+ VALUES_64_COUNT * sizeof(u64)
			+ INDEX_32_COUNT * sizeof(struct db_index)
			+ VALUES_32_COUNT * sizeof(u32);
	db->count_16 = VALUES_16_COUNT;

	/* Rules to check db layout. */

	BUILD_BUG_ON(sizeof(struct db_index) != 1);
	BUILD_BUG_ON(sizeof(struct os_area_db) != 2 * OS_AREA_SEGMENT_SIZE);
	BUILD_BUG_ON(INDEX_64_COUNT & 0x7);
	BUILD_BUG_ON(VALUES_64_COUNT > INDEX_64_COUNT);
	BUILD_BUG_ON(INDEX_32_COUNT & 0x7);
	BUILD_BUG_ON(VALUES_32_COUNT > INDEX_32_COUNT);
	BUILD_BUG_ON(INDEX_16_COUNT & 0x7);
	BUILD_BUG_ON(VALUES_16_COUNT > INDEX_16_COUNT);
	BUILD_BUG_ON(HEADER_SIZE
			+ INDEX_64_COUNT * sizeof(struct db_index)
			+ VALUES_64_COUNT * sizeof(u64)
			+ INDEX_32_COUNT * sizeof(struct db_index)
			+ VALUES_32_COUNT * sizeof(u32)
			+ INDEX_16_COUNT * sizeof(struct db_index)
			+ VALUES_16_COUNT * sizeof(u16)
			> sizeof(struct os_area_db));
}

/**
 * update_flash_db - Helper for os_area_queue_work_handler.
 *
 */

static int update_flash_db(void)
{
	const unsigned int buf_len = 8 * OS_AREA_SEGMENT_SIZE;
	struct os_area_header *header;
	ssize_t count;
	int error;
	loff_t pos;
	struct os_area_db* db;

	/* Read in header and db from flash. */

	header = kmalloc(buf_len, GFP_KERNEL);
	if (!header) {
		pr_debug("%s: kmalloc failed\n", __func__);
		return -ENOMEM;
	}

	count = os_area_flash_read(header, buf_len, 0);
	if (count < 0) {
		pr_debug("%s: os_area_flash_read failed %zd\n", __func__,
			 count);
		error = count;
		goto fail;
	}

	pos = header->db_area_offset * OS_AREA_SEGMENT_SIZE;
	if (count < OS_AREA_SEGMENT_SIZE || verify_header(header) ||
	    count < pos) {
		pr_debug("%s: verify_header failed\n", __func__);
		dump_header(header);
		error = -EINVAL;
		goto fail;
	}

	/* Now got a good db offset and some maybe good db data. */

	db = (void *)header + pos;

	error = db_verify(db);
	if (error) {
		pr_notice("%s: Verify of flash database failed, formatting.\n",
			  __func__);
		dump_db(db);
		os_area_db_init(db);
	}

	/* Now got good db data. */

	db_set_64(db, &os_area_db_id_rtc_diff, saved_params.rtc_diff);

	count = os_area_flash_write(db, sizeof(struct os_area_db), pos);
	if (count < sizeof(struct os_area_db)) {
		pr_debug("%s: os_area_flash_write failed %zd\n", __func__,
			 count);
		error = count < 0 ? count : -EIO;
	}

fail:
	kfree(header);
	return error;
}

/**
 * os_area_queue_work_handler - Asynchronous write handler.
 *
 * An asynchronous write for flash memory and the device tree.  Do not
 * call directly, use os_area_queue_work().
 */

static void os_area_queue_work_handler(struct work_struct *work)
{
	struct device_node *node;
	int error;

	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	node = of_find_node_by_path("/");
	if (node) {
		os_area_set_property(node, &property_rtc_diff);
		of_node_put(node);
	} else
		pr_debug("%s:%d of_find_node_by_path failed\n",
			__func__, __LINE__);

	error = update_flash_db();
	if (error)
		pr_warn("%s: Could not update FLASH ROM\n", __func__);

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
}

static void os_area_queue_work(void)
{
	static DECLARE_WORK(q, os_area_queue_work_handler);

	wmb();
	schedule_work(&q);
}

/**
 * ps3_os_area_save_params - Copy data from os area mirror to @saved_params.
 *
 * For the convenience of the guest the HV makes a copy of the os area in
 * flash to a high address in the boot memory region and then puts that RAM
 * address and the byte count into the repository for retrieval by the guest.
 * We copy the data we want into a static variable and allow the memory setup
 * by the HV to be claimed by the memblock manager.
 *
 * The os area mirror will not be available to a second stage kernel, and
 * the header verify will fail.  In this case, the saved_params values will
 * be set from flash memory or the passed in device tree in ps3_os_area_init().
 */

void __init ps3_os_area_save_params(void)
{
	int result;
	u64 lpar_addr;
	unsigned int size;
	struct os_area_header *header;
	struct os_area_params *params;
	struct os_area_db *db;

	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	result = ps3_repository_read_boot_dat_info(&lpar_addr, &size);

	if (result) {
		pr_debug("%s:%d ps3_repository_read_boot_dat_info failed\n",
			__func__, __LINE__);
		return;
	}

	header = (struct os_area_header *)__va(lpar_addr);
	params = (struct os_area_params *)__va(lpar_addr
		+ OS_AREA_SEGMENT_SIZE);

	result = verify_header(header);

	if (result) {
		/* Second stage kernels exit here. */
		pr_debug("%s:%d verify_header failed\n", __func__, __LINE__);
		dump_header(header);
		return;
	}

	db = (struct os_area_db *)__va(lpar_addr
		+ header->db_area_offset * OS_AREA_SEGMENT_SIZE);

	dump_header(header);
	dump_params(params);
	dump_db(db);

	result = db_verify(db) || db_get_rtc_diff(db, &saved_params.rtc_diff);
	if (result)
		saved_params.rtc_diff = params->rtc_diff ? params->rtc_diff
			: SECONDS_FROM_1970_TO_2000;
	saved_params.av_multi_out = params->av_multi_out;
	saved_params.valid = 1;

	memset(header, 0, sizeof(*header));

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
}

/**
 * ps3_os_area_init - Setup os area device tree properties as needed.
 */

void __init ps3_os_area_init(void)
{
	struct device_node *node;

	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	node = of_find_node_by_path("/");

	if (!saved_params.valid && node) {
		/* Second stage kernels should have a dt entry. */
		os_area_get_property(node, &property_rtc_diff);
		os_area_get_property(node, &property_av_multi_out);
	}

	if(!saved_params.rtc_diff)
		saved_params.rtc_diff = SECONDS_FROM_1970_TO_2000;

	if (node) {
		os_area_set_property(node, &property_rtc_diff);
		os_area_set_property(node, &property_av_multi_out);
		of_node_put(node);
	} else
		pr_debug("%s:%d of_find_node_by_path failed\n",
			__func__, __LINE__);

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
}

/**
 * ps3_os_area_get_rtc_diff - Returns the rtc diff value.
 */

u64 ps3_os_area_get_rtc_diff(void)
{
	return saved_params.rtc_diff;
}
EXPORT_SYMBOL_GPL(ps3_os_area_get_rtc_diff);

/**
 * ps3_os_area_set_rtc_diff - Set the rtc diff value.
 *
 * An asynchronous write is needed to support writing updates from
 * the timer interrupt context.
 */

void ps3_os_area_set_rtc_diff(u64 rtc_diff)
{
	if (saved_params.rtc_diff != rtc_diff) {
		saved_params.rtc_diff = rtc_diff;
		os_area_queue_work();
	}
}
EXPORT_SYMBOL_GPL(ps3_os_area_set_rtc_diff);

/**
 * ps3_os_area_get_av_multi_out - Returns the default video mode.
 */

enum ps3_param_av_multi_out ps3_os_area_get_av_multi_out(void)
{
    return saved_params.av_multi_out;
}
EXPORT_SYMBOL_GPL(ps3_os_area_get_av_multi_out);
