// SPDX-License-Identifier: GPL-2.0
/*
 * Thunderbolt XDomain property support
 *
 * Copyright (C) 2017, Intel Corporation
 * Authors: Michael Jamet <michael.jamet@intel.com>
 *          Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#include <linux/err.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uuid.h>
#include <linux/thunderbolt.h>

struct tb_property_entry {
	u32 key_hi;
	u32 key_lo;
	u16 length;
	u8 reserved;
	u8 type;
	u32 value;
};

struct tb_property_rootdir_entry {
	u32 magic;
	u32 length;
	struct tb_property_entry entries[];
};

struct tb_property_dir_entry {
	u32 uuid[4];
	struct tb_property_entry entries[];
};

#define TB_PROPERTY_ROOTDIR_MAGIC	0x55584401

static struct tb_property_dir *__tb_property_parse_dir(const u32 *block,
	size_t block_len, unsigned int dir_offset, size_t dir_len,
	bool is_root);

static inline void parse_dwdata(void *dst, const void *src, size_t dwords)
{
	be32_to_cpu_array(dst, src, dwords);
}

static inline void format_dwdata(void *dst, const void *src, size_t dwords)
{
	cpu_to_be32_array(dst, src, dwords);
}

static bool tb_property_entry_valid(const struct tb_property_entry *entry,
				  size_t block_len)
{
	switch (entry->type) {
	case TB_PROPERTY_TYPE_DIRECTORY:
	case TB_PROPERTY_TYPE_DATA:
	case TB_PROPERTY_TYPE_TEXT:
		if (entry->length > block_len)
			return false;
		if (entry->value + entry->length > block_len)
			return false;
		break;

	case TB_PROPERTY_TYPE_VALUE:
		if (entry->length != 1)
			return false;
		break;
	}

	return true;
}

static bool tb_property_key_valid(const char *key)
{
	return key && strlen(key) <= TB_PROPERTY_KEY_SIZE;
}

static struct tb_property *
tb_property_alloc(const char *key, enum tb_property_type type)
{
	struct tb_property *property;

	property = kzalloc(sizeof(*property), GFP_KERNEL);
	if (!property)
		return NULL;

	strcpy(property->key, key);
	property->type = type;
	INIT_LIST_HEAD(&property->list);

	return property;
}

static struct tb_property *tb_property_parse(const u32 *block, size_t block_len,
					const struct tb_property_entry *entry)
{
	char key[TB_PROPERTY_KEY_SIZE + 1];
	struct tb_property *property;
	struct tb_property_dir *dir;

	if (!tb_property_entry_valid(entry, block_len))
		return NULL;

	parse_dwdata(key, entry, 2);
	key[TB_PROPERTY_KEY_SIZE] = '\0';

	property = tb_property_alloc(key, entry->type);
	if (!property)
		return NULL;

	property->length = entry->length;

	switch (property->type) {
	case TB_PROPERTY_TYPE_DIRECTORY:
		dir = __tb_property_parse_dir(block, block_len, entry->value,
					      entry->length, false);
		if (!dir) {
			kfree(property);
			return NULL;
		}
		property->value.dir = dir;
		break;

	case TB_PROPERTY_TYPE_DATA:
		property->value.data = kcalloc(property->length, sizeof(u32),
					       GFP_KERNEL);
		if (!property->value.data) {
			kfree(property);
			return NULL;
		}
		parse_dwdata(property->value.data, block + entry->value,
			     entry->length);
		break;

	case TB_PROPERTY_TYPE_TEXT:
		property->value.text = kcalloc(property->length, sizeof(u32),
					       GFP_KERNEL);
		if (!property->value.text) {
			kfree(property);
			return NULL;
		}
		parse_dwdata(property->value.text, block + entry->value,
			     entry->length);
		/* Force null termination */
		property->value.text[property->length * 4 - 1] = '\0';
		break;

	case TB_PROPERTY_TYPE_VALUE:
		property->value.immediate = entry->value;
		break;

	default:
		property->type = TB_PROPERTY_TYPE_UNKNOWN;
		break;
	}

	return property;
}

static struct tb_property_dir *__tb_property_parse_dir(const u32 *block,
	size_t block_len, unsigned int dir_offset, size_t dir_len, bool is_root)
{
	const struct tb_property_entry *entries;
	size_t i, content_len, nentries;
	unsigned int content_offset;
	struct tb_property_dir *dir;

	dir = kzalloc(sizeof(*dir), GFP_KERNEL);
	if (!dir)
		return NULL;

	if (is_root) {
		content_offset = dir_offset + 2;
		content_len = dir_len;
	} else {
		dir->uuid = kmemdup(&block[dir_offset], sizeof(*dir->uuid),
				    GFP_KERNEL);
		content_offset = dir_offset + 4;
		content_len = dir_len - 4; /* Length includes UUID */
	}

	entries = (const struct tb_property_entry *)&block[content_offset];
	nentries = content_len / (sizeof(*entries) / 4);

	INIT_LIST_HEAD(&dir->properties);

	for (i = 0; i < nentries; i++) {
		struct tb_property *property;

		property = tb_property_parse(block, block_len, &entries[i]);
		if (!property) {
			tb_property_free_dir(dir);
			return NULL;
		}

		list_add_tail(&property->list, &dir->properties);
	}

	return dir;
}

/**
 * tb_property_parse_dir() - Parses properties from given property block
 * @block: Property block to parse
 * @block_len: Number of dword elements in the property block
 *
 * This function parses the XDomain properties data block into format that
 * can be traversed using the helper functions provided by this module.
 * Upon success returns the parsed directory. In case of error returns
 * %NULL. The resulting &struct tb_property_dir needs to be released by
 * calling tb_property_free_dir() when not needed anymore.
 *
 * The @block is expected to be root directory.
 */
struct tb_property_dir *tb_property_parse_dir(const u32 *block,
					      size_t block_len)
{
	const struct tb_property_rootdir_entry *rootdir =
		(const struct tb_property_rootdir_entry *)block;

	if (rootdir->magic != TB_PROPERTY_ROOTDIR_MAGIC)
		return NULL;
	if (rootdir->length > block_len)
		return NULL;

	return __tb_property_parse_dir(block, block_len, 0, rootdir->length,
				       true);
}

/**
 * tb_property_create_dir() - Creates new property directory
 * @uuid: UUID used to identify the particular directory
 *
 * Creates new, empty property directory. If @uuid is %NULL then the
 * directory is assumed to be root directory.
 */
struct tb_property_dir *tb_property_create_dir(const uuid_t *uuid)
{
	struct tb_property_dir *dir;

	dir = kzalloc(sizeof(*dir), GFP_KERNEL);
	if (!dir)
		return NULL;

	INIT_LIST_HEAD(&dir->properties);
	if (uuid) {
		dir->uuid = kmemdup(uuid, sizeof(*dir->uuid), GFP_KERNEL);
		if (!dir->uuid) {
			kfree(dir);
			return NULL;
		}
	}

	return dir;
}
EXPORT_SYMBOL_GPL(tb_property_create_dir);

static void tb_property_free(struct tb_property *property)
{
	switch (property->type) {
	case TB_PROPERTY_TYPE_DIRECTORY:
		tb_property_free_dir(property->value.dir);
		break;

	case TB_PROPERTY_TYPE_DATA:
		kfree(property->value.data);
		break;

	case TB_PROPERTY_TYPE_TEXT:
		kfree(property->value.text);
		break;

	default:
		break;
	}

	kfree(property);
}

/**
 * tb_property_free_dir() - Release memory allocated for property directory
 * @dir: Directory to release
 *
 * This will release all the memory the directory occupies including all
 * descendants. It is OK to pass %NULL @dir, then the function does
 * nothing.
 */
void tb_property_free_dir(struct tb_property_dir *dir)
{
	struct tb_property *property, *tmp;

	if (!dir)
		return;

	list_for_each_entry_safe(property, tmp, &dir->properties, list) {
		list_del(&property->list);
		tb_property_free(property);
	}
	kfree(dir->uuid);
	kfree(dir);
}
EXPORT_SYMBOL_GPL(tb_property_free_dir);

static size_t tb_property_dir_length(const struct tb_property_dir *dir,
				     bool recurse, size_t *data_len)
{
	const struct tb_property *property;
	size_t len = 0;

	if (dir->uuid)
		len += sizeof(*dir->uuid) / 4;
	else
		len += sizeof(struct tb_property_rootdir_entry) / 4;

	list_for_each_entry(property, &dir->properties, list) {
		len += sizeof(struct tb_property_entry) / 4;

		switch (property->type) {
		case TB_PROPERTY_TYPE_DIRECTORY:
			if (recurse) {
				len += tb_property_dir_length(
					property->value.dir, recurse, data_len);
			}
			/* Reserve dword padding after each directory */
			if (data_len)
				*data_len += 1;
			break;

		case TB_PROPERTY_TYPE_DATA:
		case TB_PROPERTY_TYPE_TEXT:
			if (data_len)
				*data_len += property->length;
			break;

		default:
			break;
		}
	}

	return len;
}

static ssize_t __tb_property_format_dir(const struct tb_property_dir *dir,
	u32 *block, unsigned int start_offset, size_t block_len)
{
	unsigned int data_offset, dir_end;
	const struct tb_property *property;
	struct tb_property_entry *entry;
	size_t dir_len, data_len = 0;
	int ret;

	/*
	 * The structure of property block looks like following. Leaf
	 * data/text is included right after the directory and each
	 * directory follows each other (even nested ones).
	 *
	 * +----------+ <-- start_offset
	 * |  header  | <-- root directory header
	 * +----------+ ---
	 * |  entry 0 | -^--------------------.
	 * +----------+  |                    |
	 * |  entry 1 | -|--------------------|--.
	 * +----------+  |                    |  |
	 * |  entry 2 | -|-----------------.  |  |
	 * +----------+  |                 |  |  |
	 * :          :  |  dir_len        |  |  |
	 * .          .  |                 |  |  |
	 * :          :  |                 |  |  |
	 * +----------+  |                 |  |  |
	 * |  entry n |  v                 |  |  |
	 * +----------+ <-- data_offset    |  |  |
	 * |  data 0  | <------------------|--'  |
	 * +----------+                    |     |
	 * |  data 1  | <------------------|-----'
	 * +----------+                    |
	 * | 00000000 | padding            |
	 * +----------+ <-- dir_end <------'
	 * |   UUID   | <-- directory UUID (child directory)
	 * +----------+
	 * |  entry 0 |
	 * +----------+
	 * |  entry 1 |
	 * +----------+
	 * :          :
	 * .          .
	 * :          :
	 * +----------+
	 * |  entry n |
	 * +----------+
	 * |  data 0  |
	 * +----------+
	 *
	 * We use dir_end to hold pointer to the end of the directory. It
	 * will increase as we add directories and each directory should be
	 * added starting from previous dir_end.
	 */
	dir_len = tb_property_dir_length(dir, false, &data_len);
	data_offset = start_offset + dir_len;
	dir_end = start_offset + data_len + dir_len;

	if (data_offset > dir_end)
		return -EINVAL;
	if (dir_end > block_len)
		return -EINVAL;

	/* Write headers first */
	if (dir->uuid) {
		struct tb_property_dir_entry *pe;

		pe = (struct tb_property_dir_entry *)&block[start_offset];
		memcpy(pe->uuid, dir->uuid, sizeof(pe->uuid));
		entry = pe->entries;
	} else {
		struct tb_property_rootdir_entry *re;

		re = (struct tb_property_rootdir_entry *)&block[start_offset];
		re->magic = TB_PROPERTY_ROOTDIR_MAGIC;
		re->length = dir_len - sizeof(*re) / 4;
		entry = re->entries;
	}

	list_for_each_entry(property, &dir->properties, list) {
		const struct tb_property_dir *child;

		format_dwdata(entry, property->key, 2);
		entry->type = property->type;

		switch (property->type) {
		case TB_PROPERTY_TYPE_DIRECTORY:
			child = property->value.dir;
			ret = __tb_property_format_dir(child, block, dir_end,
						       block_len);
			if (ret < 0)
				return ret;
			entry->length = tb_property_dir_length(child, false,
							       NULL);
			entry->value = dir_end;
			dir_end = ret;
			break;

		case TB_PROPERTY_TYPE_DATA:
			format_dwdata(&block[data_offset], property->value.data,
				      property->length);
			entry->length = property->length;
			entry->value = data_offset;
			data_offset += entry->length;
			break;

		case TB_PROPERTY_TYPE_TEXT:
			format_dwdata(&block[data_offset], property->value.text,
				      property->length);
			entry->length = property->length;
			entry->value = data_offset;
			data_offset += entry->length;
			break;

		case TB_PROPERTY_TYPE_VALUE:
			entry->length = property->length;
			entry->value = property->value.immediate;
			break;

		default:
			break;
		}

		entry++;
	}

	return dir_end;
}

/**
 * tb_property_format_dir() - Formats directory to the packed XDomain format
 * @dir: Directory to format
 * @block: Property block where the packed data is placed
 * @block_len: Length of the property block
 *
 * This function formats the directory to the packed format that can be
 * then send over the thunderbolt fabric to receiving host. Returns %0 in
 * case of success and negative errno on faulure. Passing %NULL in @block
 * returns number of entries the block takes.
 */
ssize_t tb_property_format_dir(const struct tb_property_dir *dir, u32 *block,
			       size_t block_len)
{
	ssize_t ret;

	if (!block) {
		size_t dir_len, data_len = 0;

		dir_len = tb_property_dir_length(dir, true, &data_len);
		return dir_len + data_len;
	}

	ret = __tb_property_format_dir(dir, block, 0, block_len);
	return ret < 0 ? ret : 0;
}

/**
 * tb_property_add_immediate() - Add immediate property to directory
 * @parent: Directory to add the property
 * @key: Key for the property
 * @value: Immediate value to store with the property
 */
int tb_property_add_immediate(struct tb_property_dir *parent, const char *key,
			      u32 value)
{
	struct tb_property *property;

	if (!tb_property_key_valid(key))
		return -EINVAL;

	property = tb_property_alloc(key, TB_PROPERTY_TYPE_VALUE);
	if (!property)
		return -ENOMEM;

	property->length = 1;
	property->value.immediate = value;

	list_add_tail(&property->list, &parent->properties);
	return 0;
}
EXPORT_SYMBOL_GPL(tb_property_add_immediate);

/**
 * tb_property_add_data() - Adds arbitrary data property to directory
 * @parent: Directory to add the property
 * @key: Key for the property
 * @buf: Data buffer to add
 * @buflen: Number of bytes in the data buffer
 *
 * Function takes a copy of @buf and adds it to the directory.
 */
int tb_property_add_data(struct tb_property_dir *parent, const char *key,
			 const void *buf, size_t buflen)
{
	/* Need to pad to dword boundary */
	size_t size = round_up(buflen, 4);
	struct tb_property *property;

	if (!tb_property_key_valid(key))
		return -EINVAL;

	property = tb_property_alloc(key, TB_PROPERTY_TYPE_DATA);
	if (!property)
		return -ENOMEM;

	property->length = size / 4;
	property->value.data = kzalloc(size, GFP_KERNEL);
	memcpy(property->value.data, buf, buflen);

	list_add_tail(&property->list, &parent->properties);
	return 0;
}
EXPORT_SYMBOL_GPL(tb_property_add_data);

/**
 * tb_property_add_text() - Adds string property to directory
 * @parent: Directory to add the property
 * @key: Key for the property
 * @text: String to add
 *
 * Function takes a copy of @text and adds it to the directory.
 */
int tb_property_add_text(struct tb_property_dir *parent, const char *key,
			 const char *text)
{
	/* Need to pad to dword boundary */
	size_t size = round_up(strlen(text) + 1, 4);
	struct tb_property *property;

	if (!tb_property_key_valid(key))
		return -EINVAL;

	property = tb_property_alloc(key, TB_PROPERTY_TYPE_TEXT);
	if (!property)
		return -ENOMEM;

	property->length = size / 4;
	property->value.data = kzalloc(size, GFP_KERNEL);
	strcpy(property->value.text, text);

	list_add_tail(&property->list, &parent->properties);
	return 0;
}
EXPORT_SYMBOL_GPL(tb_property_add_text);

/**
 * tb_property_add_dir() - Adds a directory to the parent directory
 * @parent: Directory to add the property
 * @key: Key for the property
 * @dir: Directory to add
 */
int tb_property_add_dir(struct tb_property_dir *parent, const char *key,
			struct tb_property_dir *dir)
{
	struct tb_property *property;

	if (!tb_property_key_valid(key))
		return -EINVAL;

	property = tb_property_alloc(key, TB_PROPERTY_TYPE_DIRECTORY);
	if (!property)
		return -ENOMEM;

	property->value.dir = dir;

	list_add_tail(&property->list, &parent->properties);
	return 0;
}
EXPORT_SYMBOL_GPL(tb_property_add_dir);

/**
 * tb_property_remove() - Removes property from a parent directory
 * @property: Property to remove
 *
 * Note memory for @property is released as well so it is not allowed to
 * touch the object after call to this function.
 */
void tb_property_remove(struct tb_property *property)
{
	list_del(&property->list);
	kfree(property);
}
EXPORT_SYMBOL_GPL(tb_property_remove);

/**
 * tb_property_find() - Find a property from a directory
 * @dir: Directory where the property is searched
 * @key: Key to look for
 * @type: Type of the property
 *
 * Finds and returns property from the given directory. Does not recurse
 * into sub-directories. Returns %NULL if the property was not found.
 */
struct tb_property *tb_property_find(struct tb_property_dir *dir,
	const char *key, enum tb_property_type type)
{
	struct tb_property *property;

	list_for_each_entry(property, &dir->properties, list) {
		if (property->type == type && !strcmp(property->key, key))
			return property;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(tb_property_find);

/**
 * tb_property_get_next() - Get next property from directory
 * @dir: Directory holding properties
 * @prev: Previous property in the directory (%NULL returns the first)
 */
struct tb_property *tb_property_get_next(struct tb_property_dir *dir,
					 struct tb_property *prev)
{
	if (prev) {
		if (list_is_last(&prev->list, &dir->properties))
			return NULL;
		return list_next_entry(prev, list);
	}
	return list_first_entry_or_null(&dir->properties, struct tb_property,
					list);
}
EXPORT_SYMBOL_GPL(tb_property_get_next);
