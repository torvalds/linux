/*
 * csr1212.c -- IEEE 1212 Control and Status Register support for Linux
 *
 * Copyright (C) 2003 Francois Retief <fgretief@sun.ac.za>
 *                    Steve Kinneberg <kinnebergsteve@acmsystems.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *    1. Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *    3. The name of the author may not be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/* TODO List:
 * - Verify interface consistency: i.e., public functions that take a size
 *   parameter expect size to be in bytes.
 * - Convenience functions for reading a block of data from a given offset.
 */

#ifndef __KERNEL__
#include <string.h>
#endif

#include "csr1212.h"


/* Permitted key type for each key id */
#define __I (1 << CSR1212_KV_TYPE_IMMEDIATE)
#define __C (1 << CSR1212_KV_TYPE_CSR_OFFSET)
#define __D (1 << CSR1212_KV_TYPE_DIRECTORY)
#define __L (1 << CSR1212_KV_TYPE_LEAF)
static const u_int8_t csr1212_key_id_type_map[0x30] = {
	__C,			/* used by Apple iSight */
	__D | __L,		/* Descriptor */
	__I | __D | __L,	/* Bus_Dependent_Info */
	__I | __D | __L,	/* Vendor */
	__I,			/* Hardware_Version */
	0, 0,			/* Reserved */
	__D | __L | __I,	/* Module */
	__I, 0, 0, 0,		/* used by Apple iSight, Reserved */
	__I,			/* Node_Capabilities */
	__L,			/* EUI_64 */
	0, 0, 0,		/* Reserved */
	__D,			/* Unit */
	__I,			/* Specifier_ID */
	__I,			/* Version */
	__I | __C | __D | __L,	/* Dependent_Info */
	__L,			/* Unit_Location */
	0,			/* Reserved */
	__I,			/* Model */
	__D,			/* Instance */
	__L,			/* Keyword */
	__D,			/* Feature */
	__L,			/* Extended_ROM */
	__I,			/* Extended_Key_Specifier_ID */
	__I,			/* Extended_Key */
	__I | __C | __D | __L,	/* Extended_Data */
	__L,			/* Modifiable_Descriptor */
	__I,			/* Directory_ID */
	__I,			/* Revision */
};
#undef __I
#undef __C
#undef __D
#undef __L


#define quads_to_bytes(_q) ((_q) * sizeof(u_int32_t))
#define bytes_to_quads(_b) (((_b) + sizeof(u_int32_t) - 1) / sizeof(u_int32_t))

static inline void free_keyval(struct csr1212_keyval *kv)
{
	if ((kv->key.type == CSR1212_KV_TYPE_LEAF) &&
	    (kv->key.id != CSR1212_KV_ID_EXTENDED_ROM))
		CSR1212_FREE(kv->value.leaf.data);

	CSR1212_FREE(kv);
}

static u_int16_t csr1212_crc16(const u_int32_t *buffer, size_t length)
{
	int shift;
	u_int32_t data;
	u_int16_t sum, crc = 0;

	for (; length; length--) {
		data = CSR1212_BE32_TO_CPU(*buffer);
		buffer++;
		for (shift = 28; shift >= 0; shift -= 4 ) {
			sum = ((crc >> 12) ^ (data >> shift)) & 0xf;
			crc = (crc << 4) ^ (sum << 12) ^ (sum << 5) ^ (sum);
		}
		crc &= 0xffff;
	}

	return CSR1212_CPU_TO_BE16(crc);
}

#if 0
/* Microsoft computes the CRC with the bytes in reverse order.  Therefore we
 * have a special version of the CRC algorithm to account for their buggy
 * software. */
static u_int16_t csr1212_msft_crc16(const u_int32_t *buffer, size_t length)
{
	int shift;
	u_int32_t data;
	u_int16_t sum, crc = 0;

	for (; length; length--) {
		data = CSR1212_LE32_TO_CPU(*buffer);
		buffer++;
		for (shift = 28; shift >= 0; shift -= 4 ) {
			sum = ((crc >> 12) ^ (data >> shift)) & 0xf;
			crc = (crc << 4) ^ (sum << 12) ^ (sum << 5) ^ (sum);
		}
		crc &= 0xffff;
	}

	return CSR1212_CPU_TO_BE16(crc);
}
#endif

static inline struct csr1212_dentry *csr1212_find_keyval(struct csr1212_keyval *dir,
							 struct csr1212_keyval *kv)
{
	struct csr1212_dentry *pos;

	for (pos = dir->value.directory.dentries_head;
	     pos != NULL; pos = pos->next) {
		if (pos->kv == kv)
			return pos;
	}
	return NULL;
}


static inline struct csr1212_keyval *csr1212_find_keyval_offset(struct csr1212_keyval *kv_list,
								u_int32_t offset)
{
	struct csr1212_keyval *kv;

	for (kv = kv_list->next; kv && (kv != kv_list); kv = kv->next) {
		if (kv->offset == offset)
			return kv;
	}
	return NULL;
}


/* Creation Routines */
struct csr1212_csr *csr1212_create_csr(struct csr1212_bus_ops *ops,
				       size_t bus_info_size, void *private)
{
	struct csr1212_csr *csr;

	csr = CSR1212_MALLOC(sizeof(*csr));
	if (!csr)
		return NULL;

	csr->cache_head =
		csr1212_rom_cache_malloc(CSR1212_CONFIG_ROM_SPACE_OFFSET,
					 CSR1212_CONFIG_ROM_SPACE_SIZE);
	if (!csr->cache_head) {
		CSR1212_FREE(csr);
		return NULL;
	}

	/* The keyval key id is not used for the root node, but a valid key id
	 * that can be used for a directory needs to be passed to
	 * csr1212_new_directory(). */
	csr->root_kv = csr1212_new_directory(CSR1212_KV_ID_VENDOR);
	if (!csr->root_kv) {
		CSR1212_FREE(csr->cache_head);
		CSR1212_FREE(csr);
		return NULL;
	}

	csr->bus_info_data = csr->cache_head->data;
	csr->bus_info_len = bus_info_size;
	csr->crc_len = bus_info_size;
	csr->ops = ops;
	csr->private = private;
	csr->cache_tail = csr->cache_head;

	return csr;
}



void csr1212_init_local_csr(struct csr1212_csr *csr,
			    const u_int32_t *bus_info_data, int max_rom)
{
	static const int mr_map[] = { 4, 64, 1024, 0 };

#ifdef __KERNEL__
	BUG_ON(max_rom & ~0x3);
	csr->max_rom = mr_map[max_rom];
#else
	if (max_rom & ~0x3) /* caller supplied invalid argument */
		csr->max_rom = 0;
	else
		csr->max_rom = mr_map[max_rom];
#endif
	memcpy(csr->bus_info_data, bus_info_data, csr->bus_info_len);
}


static struct csr1212_keyval *csr1212_new_keyval(u_int8_t type, u_int8_t key)
{
	struct csr1212_keyval *kv;

	if (key < 0x30 && ((csr1212_key_id_type_map[key] & (1 << type)) == 0))
		return NULL;

	kv = CSR1212_MALLOC(sizeof(*kv));
	if (!kv)
		return NULL;

	kv->key.type = type;
	kv->key.id = key;

	kv->associate = NULL;
	kv->refcnt = 1;

	kv->next = NULL;
	kv->prev = NULL;
	kv->offset = 0;
	kv->valid = 0;
	return kv;
}

struct csr1212_keyval *csr1212_new_immediate(u_int8_t key, u_int32_t value)
{
	struct csr1212_keyval *kv = csr1212_new_keyval(CSR1212_KV_TYPE_IMMEDIATE, key);

	if (!kv)
		return NULL;

	kv->value.immediate = value;
	kv->valid = 1;
	return kv;
}

struct csr1212_keyval *csr1212_new_leaf(u_int8_t key, const void *data, size_t data_len)
{
	struct csr1212_keyval *kv = csr1212_new_keyval(CSR1212_KV_TYPE_LEAF, key);

	if (!kv)
		return NULL;

	if (data_len > 0) {
		kv->value.leaf.data = CSR1212_MALLOC(data_len);
		if (!kv->value.leaf.data) {
			CSR1212_FREE(kv);
			return NULL;
		}

		if (data)
			memcpy(kv->value.leaf.data, data, data_len);
	} else {
		kv->value.leaf.data = NULL;
	}

	kv->value.leaf.len = bytes_to_quads(data_len);
	kv->offset = 0;
	kv->valid = 1;

	return kv;
}

struct csr1212_keyval *csr1212_new_csr_offset(u_int8_t key, u_int32_t csr_offset)
{
	struct csr1212_keyval *kv = csr1212_new_keyval(CSR1212_KV_TYPE_CSR_OFFSET, key);

	if (!kv)
		return NULL;

	kv->value.csr_offset = csr_offset;

	kv->offset = 0;
	kv->valid = 1;
	return kv;
}

struct csr1212_keyval *csr1212_new_directory(u_int8_t key)
{
	struct csr1212_keyval *kv = csr1212_new_keyval(CSR1212_KV_TYPE_DIRECTORY, key);

	if (!kv)
		return NULL;

	kv->value.directory.len = 0;
	kv->offset = 0;
	kv->value.directory.dentries_head = NULL;
	kv->value.directory.dentries_tail = NULL;
	kv->valid = 1;
	return kv;
}

int csr1212_associate_keyval(struct csr1212_keyval *kv,
			     struct csr1212_keyval *associate)
{
	if (!kv || !associate)
		return CSR1212_EINVAL;

	if (kv->key.id == CSR1212_KV_ID_DESCRIPTOR ||
	   (associate->key.id != CSR1212_KV_ID_DESCRIPTOR &&
	    associate->key.id != CSR1212_KV_ID_DEPENDENT_INFO &&
	    associate->key.id != CSR1212_KV_ID_EXTENDED_KEY &&
	    associate->key.id != CSR1212_KV_ID_EXTENDED_DATA &&
	    associate->key.id < 0x30))
		return CSR1212_EINVAL;

	if (kv->key.id == CSR1212_KV_ID_EXTENDED_KEY_SPECIFIER_ID &&
	   associate->key.id != CSR1212_KV_ID_EXTENDED_KEY)
		return CSR1212_EINVAL;

	if (kv->key.id == CSR1212_KV_ID_EXTENDED_KEY &&
	   associate->key.id != CSR1212_KV_ID_EXTENDED_DATA)
		return CSR1212_EINVAL;

	if (associate->key.id == CSR1212_KV_ID_EXTENDED_KEY &&
	   kv->key.id != CSR1212_KV_ID_EXTENDED_KEY_SPECIFIER_ID)
		return CSR1212_EINVAL;

	if (associate->key.id == CSR1212_KV_ID_EXTENDED_DATA &&
	   kv->key.id != CSR1212_KV_ID_EXTENDED_KEY)
		return CSR1212_EINVAL;

	if (kv->associate)
		csr1212_release_keyval(kv->associate);

	associate->refcnt++;
	kv->associate = associate;

	return CSR1212_SUCCESS;
}

int csr1212_attach_keyval_to_directory(struct csr1212_keyval *dir,
				       struct csr1212_keyval *kv)
{
	struct csr1212_dentry *dentry;

	if (!kv || !dir || dir->key.type != CSR1212_KV_TYPE_DIRECTORY)
		return CSR1212_EINVAL;

	dentry = CSR1212_MALLOC(sizeof(*dentry));
	if (!dentry)
		return CSR1212_ENOMEM;

	dentry->kv = kv;

	kv->refcnt++;

	dentry->next = NULL;
	dentry->prev = dir->value.directory.dentries_tail;

	if (!dir->value.directory.dentries_head)
		dir->value.directory.dentries_head = dentry;

	if (dir->value.directory.dentries_tail)
		dir->value.directory.dentries_tail->next = dentry;
	dir->value.directory.dentries_tail = dentry;

	return CSR1212_SUCCESS;
}

struct csr1212_keyval *csr1212_new_extended_immediate(u_int32_t spec, u_int32_t key,
						      u_int32_t value)
{
	struct csr1212_keyval *kvs, *kvk, *kvv;

	kvs = csr1212_new_immediate(CSR1212_KV_ID_EXTENDED_KEY_SPECIFIER_ID, spec);
	kvk = csr1212_new_immediate(CSR1212_KV_ID_EXTENDED_KEY, key);
	kvv = csr1212_new_immediate(CSR1212_KV_ID_EXTENDED_DATA, value);

	if (!kvs || !kvk || !kvv) {
		if (kvs)
			free_keyval(kvs);
		if (kvk)
			free_keyval(kvk);
		if (kvv)
			free_keyval(kvv);
		return NULL;
	}

	/* Don't keep a local reference to the extended key or value. */
	kvk->refcnt = 0;
	kvv->refcnt = 0;

	csr1212_associate_keyval(kvk, kvv);
	csr1212_associate_keyval(kvs, kvk);

	return kvs;
}

struct csr1212_keyval *csr1212_new_extended_leaf(u_int32_t spec, u_int32_t key,
						 const void *data, size_t data_len)
{
	struct csr1212_keyval *kvs, *kvk, *kvv;

	kvs = csr1212_new_immediate(CSR1212_KV_ID_EXTENDED_KEY_SPECIFIER_ID, spec);
	kvk = csr1212_new_immediate(CSR1212_KV_ID_EXTENDED_KEY, key);
	kvv = csr1212_new_leaf(CSR1212_KV_ID_EXTENDED_DATA, data, data_len);

	if (!kvs || !kvk || !kvv) {
		if (kvs)
			free_keyval(kvs);
		if (kvk)
			free_keyval(kvk);
		if (kvv)
			free_keyval(kvv);
		return NULL;
	}

	/* Don't keep a local reference to the extended key or value. */
	kvk->refcnt = 0;
	kvv->refcnt = 0;

	csr1212_associate_keyval(kvk, kvv);
	csr1212_associate_keyval(kvs, kvk);

	return kvs;
}

struct csr1212_keyval *csr1212_new_descriptor_leaf(u_int8_t dtype, u_int32_t specifier_id,
						   const void *data, size_t data_len)
{
	struct csr1212_keyval *kv;

	kv = csr1212_new_leaf(CSR1212_KV_ID_DESCRIPTOR, NULL,
			      data_len + CSR1212_DESCRIPTOR_LEAF_OVERHEAD);
	if (!kv)
		return NULL;

	CSR1212_DESCRIPTOR_LEAF_SET_TYPE(kv, dtype);
	CSR1212_DESCRIPTOR_LEAF_SET_SPECIFIER_ID(kv, specifier_id);

	if (data) {
		memcpy(CSR1212_DESCRIPTOR_LEAF_DATA(kv), data, data_len);
	}

	return kv;
}


struct csr1212_keyval *csr1212_new_textual_descriptor_leaf(u_int8_t cwidth,
							   u_int16_t cset,
							   u_int16_t language,
							   const void *data,
							   size_t data_len)
{
	struct csr1212_keyval *kv;
	char *lstr;

	kv = csr1212_new_descriptor_leaf(0, 0, NULL, data_len +
					 CSR1212_TEXTUAL_DESCRIPTOR_LEAF_OVERHEAD);
	if (!kv)
		return NULL;

	CSR1212_TEXTUAL_DESCRIPTOR_LEAF_SET_WIDTH(kv, cwidth);
	CSR1212_TEXTUAL_DESCRIPTOR_LEAF_SET_CHAR_SET(kv, cset);
	CSR1212_TEXTUAL_DESCRIPTOR_LEAF_SET_LANGUAGE(kv, language);

	lstr = (char*)CSR1212_TEXTUAL_DESCRIPTOR_LEAF_DATA(kv);

	/* make sure last quadlet is zeroed out */
	*((u_int32_t*)&(lstr[(data_len - 1) & ~0x3])) = 0;

	/* don't copy the NUL terminator */
	memcpy(lstr, data, data_len);

	return kv;
}

static int csr1212_check_minimal_ascii(const char *s)
{
	static const char minimal_ascii_table[] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07,
		0x00, 0x00, 0x0a, 0x00, 0x0C, 0x0D, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x20, 0x21, 0x22, 0x00, 0x00, 0x25, 0x26, 0x27,
		0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
		0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
		0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
		0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
		0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
		0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
		0x58, 0x59, 0x5a, 0x00, 0x00, 0x00, 0x00, 0x5f,
		0x00, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
		0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
		0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
		0x78, 0x79, 0x7a, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	for (; *s; s++) {
		if (minimal_ascii_table[*s & 0x7F] != *s)
			return -1; /* failed */
	}
	/* String conforms to minimal-ascii, as specified by IEEE 1212,
	 * par. 7.4 */
	return 0;
}

struct csr1212_keyval *csr1212_new_string_descriptor_leaf(const char *s)
{
	/* Check if string conform to minimal_ascii format */
	if (csr1212_check_minimal_ascii(s))
		return NULL;

	/* IEEE 1212, par. 7.5.4.1  Textual descriptors (minimal ASCII) */
	return csr1212_new_textual_descriptor_leaf(0, 0, 0, s, strlen(s));
}

struct csr1212_keyval *csr1212_new_icon_descriptor_leaf(u_int32_t version,
							u_int8_t palette_depth,
							u_int8_t color_space,
							u_int16_t language,
							u_int16_t hscan,
							u_int16_t vscan,
							u_int32_t *palette,
							u_int32_t *pixels)
{
	static const int pd[4] = { 0, 4, 16, 256 };
	static const int cs[16] = { 4, 2 };
	struct csr1212_keyval *kv;
	int palette_size;
	int pixel_size = (hscan * vscan + 3) & ~0x3;

	if (!pixels || (!palette && palette_depth) ||
	    (palette_depth & ~0x3) || (color_space & ~0xf))
		return NULL;

	palette_size = pd[palette_depth] * cs[color_space];

	kv = csr1212_new_descriptor_leaf(1, 0, NULL,
					 palette_size + pixel_size +
					 CSR1212_ICON_DESCRIPTOR_LEAF_OVERHEAD);
	if (!kv)
		return NULL;

	CSR1212_ICON_DESCRIPTOR_LEAF_SET_VERSION(kv, version);
	CSR1212_ICON_DESCRIPTOR_LEAF_SET_PALETTE_DEPTH(kv, palette_depth);
	CSR1212_ICON_DESCRIPTOR_LEAF_SET_COLOR_SPACE(kv, color_space);
	CSR1212_ICON_DESCRIPTOR_LEAF_SET_LANGUAGE(kv, language);
	CSR1212_ICON_DESCRIPTOR_LEAF_SET_HSCAN(kv, hscan);
	CSR1212_ICON_DESCRIPTOR_LEAF_SET_VSCAN(kv, vscan);

	if (palette_size)
		memcpy(CSR1212_ICON_DESCRIPTOR_LEAF_PALETTE(kv), palette,
		       palette_size);

	memcpy(CSR1212_ICON_DESCRIPTOR_LEAF_PIXELS(kv), pixels, pixel_size);

	return kv;
}

struct csr1212_keyval *csr1212_new_modifiable_descriptor_leaf(u_int16_t max_size,
							      u_int64_t address)
{
	struct csr1212_keyval *kv;

	/* IEEE 1212, par. 7.5.4.3  Modifiable descriptors */
	kv = csr1212_new_leaf(CSR1212_KV_ID_MODIFIABLE_DESCRIPTOR, NULL, sizeof(u_int64_t));
	if(!kv)
		return NULL;

	CSR1212_MODIFIABLE_DESCRIPTOR_SET_MAX_SIZE(kv, max_size);
	CSR1212_MODIFIABLE_DESCRIPTOR_SET_ADDRESS_HI(kv, address);
	CSR1212_MODIFIABLE_DESCRIPTOR_SET_ADDRESS_LO(kv, address);

	return kv;
}

static int csr1212_check_keyword(const char *s)
{
	for (; *s; s++) {

		if (('A' <= *s) && (*s <= 'Z'))
			continue;
		if (('0' <= *s) && (*s <= '9'))
			continue;
		if (*s == '-')
			continue;

		return -1; /* failed */
	}
	/* String conforms to keyword, as specified by IEEE 1212,
	 * par. 7.6.5 */
	return CSR1212_SUCCESS;
}

struct csr1212_keyval *csr1212_new_keyword_leaf(int strc, const char *strv[])
{
	struct csr1212_keyval *kv;
	char *buffer;
	int i, data_len = 0;

	/* Check all keywords to see if they conform to restrictions:
	 * Only the following characters is allowed ['A'..'Z','0'..'9','-']
	 * Each word is zero-terminated.
	 * Also calculate the total length of the keywords.
	 */
	for (i = 0; i < strc; i++) {
		if (!strv[i] || csr1212_check_keyword(strv[i])) {
			return NULL;
		}
		data_len += strlen(strv[i]) + 1; /* Add zero-termination char. */
	}

	/* IEEE 1212, par. 7.6.5 Keyword leaves */
	kv = csr1212_new_leaf(CSR1212_KV_ID_KEYWORD, NULL, data_len);
	if (!kv)
		return NULL;

	buffer = (char *)kv->value.leaf.data;

	/* make sure last quadlet is zeroed out */
	*((u_int32_t*)&(buffer[(data_len - 1) & ~0x3])) = 0;

	/* Copy keyword(s) into leaf data buffer */
	for (i = 0; i < strc; i++) {
		int len = strlen(strv[i]) + 1;
		memcpy(buffer, strv[i], len);
		buffer += len;
	}
	return kv;
}


/* Destruction Routines */

void csr1212_detach_keyval_from_directory(struct csr1212_keyval *dir,
					  struct csr1212_keyval *kv)
{
	struct csr1212_dentry *dentry;

	if (!kv || !dir || dir->key.type != CSR1212_KV_TYPE_DIRECTORY)
		return;

	dentry = csr1212_find_keyval(dir, kv);

	if (!dentry)
		return;

	if (dentry->prev)
		dentry->prev->next = dentry->next;
	if (dentry->next)
		dentry->next->prev = dentry->prev;
	if (dir->value.directory.dentries_head == dentry)
		dir->value.directory.dentries_head = dentry->next;
	if (dir->value.directory.dentries_tail == dentry)
		dir->value.directory.dentries_tail = dentry->prev;

	CSR1212_FREE(dentry);

	csr1212_release_keyval(kv);
}


void csr1212_disassociate_keyval(struct csr1212_keyval *kv)
{
	if (kv->associate) {
		csr1212_release_keyval(kv->associate);
	}

	kv->associate = NULL;
}


/* This function is used to free the memory taken by a keyval.  If the given
 * keyval is a directory type, then any keyvals contained in that directory
 * will be destroyed as well if their respective refcnts are 0.  By means of
 * list manipulation, this routine will descend a directory structure in a
 * non-recursive manner. */
void _csr1212_destroy_keyval(struct csr1212_keyval *kv)
{
	struct csr1212_keyval *k, *a;
	struct csr1212_dentry dentry;
	struct csr1212_dentry *head, *tail;

	dentry.kv = kv;
	dentry.next = NULL;
	dentry.prev = NULL;

	head = &dentry;
	tail = head;

	while (head) {
		k = head->kv;

		while (k) {
			k->refcnt--;

			if (k->refcnt > 0)
				break;

			a = k->associate;

			if (k->key.type == CSR1212_KV_TYPE_DIRECTORY) {
				/* If the current entry is a directory, then move all
				 * the entries to the destruction list. */
				if (k->value.directory.dentries_head) {
					tail->next = k->value.directory.dentries_head;
					k->value.directory.dentries_head->prev = tail;
					tail = k->value.directory.dentries_tail;
				}
			}
			free_keyval(k);
			k = a;
		}

		head = head->next;
		if (head) {
			if (head->prev && head->prev != &dentry) {
				CSR1212_FREE(head->prev);
			}
			head->prev = NULL;
		} else if (tail != &dentry)
			CSR1212_FREE(tail);
	}
}


void csr1212_destroy_csr(struct csr1212_csr *csr)
{
	struct csr1212_csr_rom_cache *c, *oc;
	struct csr1212_cache_region *cr, *ocr;

	csr1212_release_keyval(csr->root_kv);

	c = csr->cache_head;
	while (c) {
		oc = c;
		cr = c->filled_head;
		while (cr) {
			ocr = cr;
			cr = cr->next;
			CSR1212_FREE(ocr);
		}
		c = c->next;
		CSR1212_FREE(oc);
	}

	CSR1212_FREE(csr);
}



/* CSR Image Creation */

static int csr1212_append_new_cache(struct csr1212_csr *csr, size_t romsize)
{
	struct csr1212_csr_rom_cache *cache;
	u_int64_t csr_addr;

	if (!csr || !csr->ops || !csr->ops->allocate_addr_range ||
	    !csr->ops->release_addr || csr->max_rom < 1)
		return CSR1212_EINVAL;

	/* ROM size must be a multiple of csr->max_rom */
	romsize = (romsize + (csr->max_rom - 1)) & ~(csr->max_rom - 1);

	csr_addr = csr->ops->allocate_addr_range(romsize, csr->max_rom, csr->private);
	if (csr_addr == CSR1212_INVALID_ADDR_SPACE) {
		return CSR1212_ENOMEM;
	}
	if (csr_addr < CSR1212_REGISTER_SPACE_BASE) {
		/* Invalid address returned from allocate_addr_range(). */
		csr->ops->release_addr(csr_addr, csr->private);
		return CSR1212_ENOMEM;
	}

	cache = csr1212_rom_cache_malloc(csr_addr - CSR1212_REGISTER_SPACE_BASE, romsize);
	if (!cache) {
		csr->ops->release_addr(csr_addr, csr->private);
		return CSR1212_ENOMEM;
	}

	cache->ext_rom = csr1212_new_keyval(CSR1212_KV_TYPE_LEAF, CSR1212_KV_ID_EXTENDED_ROM);
	if (!cache->ext_rom) {
		csr->ops->release_addr(csr_addr, csr->private);
		CSR1212_FREE(cache);
		return CSR1212_ENOMEM;
	}

	if (csr1212_attach_keyval_to_directory(csr->root_kv, cache->ext_rom) != CSR1212_SUCCESS) {
		csr1212_release_keyval(cache->ext_rom);
		csr->ops->release_addr(csr_addr, csr->private);
		CSR1212_FREE(cache);
		return CSR1212_ENOMEM;
	}
	cache->ext_rom->offset = csr_addr - CSR1212_REGISTER_SPACE_BASE;
	cache->ext_rom->value.leaf.len = -1;
	cache->ext_rom->value.leaf.data = cache->data;

	/* Add cache to tail of cache list */
	cache->prev = csr->cache_tail;
	csr->cache_tail->next = cache;
	csr->cache_tail = cache;
	return CSR1212_SUCCESS;
}

static inline void csr1212_remove_cache(struct csr1212_csr *csr,
					struct csr1212_csr_rom_cache *cache)
{
	if (csr->cache_head == cache)
		csr->cache_head = cache->next;
	if (csr->cache_tail == cache)
		csr->cache_tail = cache->prev;

	if (cache->prev)
		cache->prev->next = cache->next;
	if (cache->next)
		cache->next->prev = cache->prev;

	if (cache->ext_rom) {
		csr1212_detach_keyval_from_directory(csr->root_kv, cache->ext_rom);
		csr1212_release_keyval(cache->ext_rom);
	}

	CSR1212_FREE(cache);
}

static int csr1212_generate_layout_subdir(struct csr1212_keyval *dir,
					  struct csr1212_keyval **layout_tail)
{
	struct csr1212_dentry *dentry;
	struct csr1212_keyval *dkv;
	struct csr1212_keyval *last_extkey_spec = NULL;
	struct csr1212_keyval *last_extkey = NULL;
	int num_entries = 0;

	for (dentry = dir->value.directory.dentries_head; dentry;
	     dentry = dentry->next) {
		for (dkv = dentry->kv; dkv; dkv = dkv->associate) {
			/* Special Case: Extended Key Specifier_ID */
			if (dkv->key.id == CSR1212_KV_ID_EXTENDED_KEY_SPECIFIER_ID) {
				if (last_extkey_spec == NULL) {
					last_extkey_spec = dkv;
				} else if (dkv->value.immediate != last_extkey_spec->value.immediate) {
					last_extkey_spec = dkv;
				} else {
					continue;
				}
			/* Special Case: Extended Key */
			} else if (dkv->key.id == CSR1212_KV_ID_EXTENDED_KEY) {
				if (last_extkey == NULL) {
					last_extkey = dkv;
				} else if (dkv->value.immediate != last_extkey->value.immediate) {
					last_extkey = dkv;
				} else {
					continue;
				}
			}

			num_entries += 1;

			switch(dkv->key.type) {
			default:
			case CSR1212_KV_TYPE_IMMEDIATE:
			case CSR1212_KV_TYPE_CSR_OFFSET:
				break;
			case CSR1212_KV_TYPE_LEAF:
			case CSR1212_KV_TYPE_DIRECTORY:
				/* Remove from list */
				if (dkv->prev && (dkv->prev->next == dkv))
					dkv->prev->next = dkv->next;
				if (dkv->next && (dkv->next->prev == dkv))
					dkv->next->prev = dkv->prev;
				//if (dkv == *layout_tail)
				//	*layout_tail = dkv->prev;

				/* Special case: Extended ROM leafs */
				if (dkv->key.id == CSR1212_KV_ID_EXTENDED_ROM) {
					dkv->value.leaf.len = -1;
					/* Don't add Extended ROM leafs in the layout list,
					 * they are handled differently. */
					break;
				}

				/* Add to tail of list */
				dkv->next = NULL;
				dkv->prev = *layout_tail;
				(*layout_tail)->next = dkv;
				*layout_tail = dkv;
				break;
			}
		}
	}
	return num_entries;
}

size_t csr1212_generate_layout_order(struct csr1212_keyval *kv)
{
	struct csr1212_keyval *ltail = kv;
	size_t agg_size = 0;

	while(kv) {
		switch(kv->key.type) {
		case CSR1212_KV_TYPE_LEAF:
			/* Add 1 quadlet for crc/len field */
			agg_size += kv->value.leaf.len + 1;
			break;

		case CSR1212_KV_TYPE_DIRECTORY:
			kv->value.directory.len = csr1212_generate_layout_subdir(kv, &ltail);
			/* Add 1 quadlet for crc/len field */
			agg_size += kv->value.directory.len + 1;
			break;
		}
		kv = kv->next;
	}
	return quads_to_bytes(agg_size);
}

struct csr1212_keyval *csr1212_generate_positions(struct csr1212_csr_rom_cache *cache,
						  struct csr1212_keyval *start_kv,
						  int start_pos)
{
	struct csr1212_keyval *kv = start_kv;
	struct csr1212_keyval *okv = start_kv;
	int pos = start_pos;
	int kv_len = 0, okv_len = 0;

	cache->layout_head = kv;

	while(kv && pos < cache->size) {
		/* Special case: Extended ROM leafs */
		if (kv->key.id != CSR1212_KV_ID_EXTENDED_ROM) {
			kv->offset = cache->offset + pos;
		}

		switch(kv->key.type) {
		case CSR1212_KV_TYPE_LEAF:
			kv_len = kv->value.leaf.len;
			break;

		case CSR1212_KV_TYPE_DIRECTORY:
			kv_len = kv->value.directory.len;
			break;

		default:
			/* Should never get here */
			break;
		}

		pos += quads_to_bytes(kv_len + 1);

		if (pos <= cache->size) {
			okv = kv;
			okv_len = kv_len;
			kv = kv->next;
		}
	}

	cache->layout_tail = okv;
	cache->len = (okv->offset - cache->offset) + quads_to_bytes(okv_len + 1);

	return kv;
}

static void csr1212_generate_tree_subdir(struct csr1212_keyval *dir,
					 u_int32_t *data_buffer)
{
	struct csr1212_dentry *dentry;
	struct csr1212_keyval *last_extkey_spec = NULL;
	struct csr1212_keyval *last_extkey = NULL;
	int index = 0;

	for (dentry = dir->value.directory.dentries_head; dentry; dentry = dentry->next) {
		struct csr1212_keyval *a;

		for (a = dentry->kv; a; a = a->associate) {
			u_int32_t value = 0;

			/* Special Case: Extended Key Specifier_ID */
			if (a->key.id == CSR1212_KV_ID_EXTENDED_KEY_SPECIFIER_ID) {
				if (last_extkey_spec == NULL) {
					last_extkey_spec = a;
				} else if (a->value.immediate != last_extkey_spec->value.immediate) {
					last_extkey_spec = a;
				} else {
					continue;
				}
			/* Special Case: Extended Key */
			} else if (a->key.id == CSR1212_KV_ID_EXTENDED_KEY) {
				if (last_extkey == NULL) {
					last_extkey = a;
				} else if (a->value.immediate != last_extkey->value.immediate) {
					last_extkey = a;
				} else {
					continue;
				}
			}

			switch(a->key.type) {
			case CSR1212_KV_TYPE_IMMEDIATE:
				value = a->value.immediate;
				break;
			case CSR1212_KV_TYPE_CSR_OFFSET:
				value = a->value.csr_offset;
				break;
			case CSR1212_KV_TYPE_LEAF:
				value = a->offset;
				value -= dir->offset + quads_to_bytes(1+index);
				value = bytes_to_quads(value);
				break;
			case CSR1212_KV_TYPE_DIRECTORY:
				value = a->offset;
				value -= dir->offset + quads_to_bytes(1+index);
				value = bytes_to_quads(value);
				break;
			default:
				/* Should never get here */
				break; /* GDB breakpoint */
			}

			value |= (a->key.id & CSR1212_KV_KEY_ID_MASK) << CSR1212_KV_KEY_SHIFT;
			value |= (a->key.type & CSR1212_KV_KEY_TYPE_MASK) <<
				(CSR1212_KV_KEY_SHIFT + CSR1212_KV_KEY_TYPE_SHIFT);
			data_buffer[index] = CSR1212_CPU_TO_BE32(value);
			index++;
		}
	}
}

void csr1212_fill_cache(struct csr1212_csr_rom_cache *cache)
{
	struct csr1212_keyval *kv, *nkv;
	struct csr1212_keyval_img *kvi;

	for (kv = cache->layout_head; kv != cache->layout_tail->next; kv = nkv) {
		kvi = (struct csr1212_keyval_img *)
			(cache->data + bytes_to_quads(kv->offset - cache->offset));
		switch(kv->key.type) {
		default:
		case CSR1212_KV_TYPE_IMMEDIATE:
		case CSR1212_KV_TYPE_CSR_OFFSET:
			/* Should never get here */
			break; /* GDB breakpoint */

		case CSR1212_KV_TYPE_LEAF:
			/* Don't copy over Extended ROM areas, they are
			 * already filled out! */
			if (kv->key.id != CSR1212_KV_ID_EXTENDED_ROM)
				memcpy(kvi->data, kv->value.leaf.data,
				       quads_to_bytes(kv->value.leaf.len));

			kvi->length = CSR1212_CPU_TO_BE16(kv->value.leaf.len);
			kvi->crc = csr1212_crc16(kvi->data, kv->value.leaf.len);
			break;

		case CSR1212_KV_TYPE_DIRECTORY:
			csr1212_generate_tree_subdir(kv, kvi->data);

			kvi->length = CSR1212_CPU_TO_BE16(kv->value.directory.len);
			kvi->crc = csr1212_crc16(kvi->data, kv->value.directory.len);
			break;
		}

		nkv = kv->next;
		if (kv->prev)
			kv->prev->next = NULL;
		if (kv->next)
			kv->next->prev = NULL;
		kv->prev = NULL;
		kv->next = NULL;
	}
}

int csr1212_generate_csr_image(struct csr1212_csr *csr)
{
	struct csr1212_bus_info_block_img *bi;
	struct csr1212_csr_rom_cache *cache;
	struct csr1212_keyval *kv;
	size_t agg_size;
	int ret;
	int init_offset;

	if (!csr)
		return CSR1212_EINVAL;

	cache = csr->cache_head;

	bi = (struct csr1212_bus_info_block_img*)cache->data;

	bi->length = bytes_to_quads(csr->bus_info_len) - 1;
	bi->crc_length = bi->length;
	bi->crc = csr1212_crc16(bi->data, bi->crc_length);

	csr->root_kv->next = NULL;
	csr->root_kv->prev = NULL;

	agg_size = csr1212_generate_layout_order(csr->root_kv);

	init_offset = csr->bus_info_len;

	for (kv = csr->root_kv, cache = csr->cache_head; kv; cache = cache->next) {
		if (!cache) {
			/* Estimate approximate number of additional cache
			 * regions needed (it assumes that the cache holding
			 * the first 1K Config ROM space always exists). */
			int est_c = agg_size / (CSR1212_EXTENDED_ROM_SIZE -
						(2 * sizeof(u_int32_t))) + 1;

			/* Add additional cache regions, extras will be
			 * removed later */
			for (; est_c; est_c--) {
				ret = csr1212_append_new_cache(csr, CSR1212_EXTENDED_ROM_SIZE);
				if (ret != CSR1212_SUCCESS)
					return ret;
			}
			/* Need to re-layout for additional cache regions */
			agg_size = csr1212_generate_layout_order(csr->root_kv);
			kv = csr->root_kv;
			cache = csr->cache_head;
			init_offset = csr->bus_info_len;
		}
		kv = csr1212_generate_positions(cache, kv, init_offset);
		agg_size -= cache->len;
		init_offset = sizeof(u_int32_t);
	}

	/* Remove unused, excess cache regions */
	while (cache) {
		struct csr1212_csr_rom_cache *oc = cache;

		cache = cache->next;
		csr1212_remove_cache(csr, oc);
	}

	/* Go through the list backward so that when done, the correct CRC
	 * will be calculated for the Extended ROM areas. */
	for(cache = csr->cache_tail; cache; cache = cache->prev) {
		/* Only Extended ROM caches should have this set. */
		if (cache->ext_rom) {
			int leaf_size;

			/* Make sure the Extended ROM leaf is a multiple of
			 * max_rom in size. */
			if (csr->max_rom < 1)
				return CSR1212_EINVAL;
			leaf_size = (cache->len + (csr->max_rom - 1)) &
				~(csr->max_rom - 1);

			/* Zero out the unused ROM region */
			memset(cache->data + bytes_to_quads(cache->len), 0x00,
			       leaf_size - cache->len);

			/* Subtract leaf header */
			leaf_size -= sizeof(u_int32_t);

			/* Update the Extended ROM leaf length */
			cache->ext_rom->value.leaf.len =
				bytes_to_quads(leaf_size);
		} else {
			/* Zero out the unused ROM region */
			memset(cache->data + bytes_to_quads(cache->len), 0x00,
			       cache->size - cache->len);
		}

		/* Copy the data into the cache buffer */
		csr1212_fill_cache(cache);

		if (cache != csr->cache_head) {
			/* Set the length and CRC of the extended ROM. */
			struct csr1212_keyval_img *kvi =
				(struct csr1212_keyval_img*)cache->data;

			kvi->length = CSR1212_CPU_TO_BE16(bytes_to_quads(cache->len) - 1);
			kvi->crc = csr1212_crc16(kvi->data,
						 bytes_to_quads(cache->len) - 1);

		}
	}

	return CSR1212_SUCCESS;
}

int csr1212_read(struct csr1212_csr *csr, u_int32_t offset, void *buffer, u_int32_t len)
{
	struct csr1212_csr_rom_cache *cache;

	for (cache = csr->cache_head; cache; cache = cache->next) {
		if (offset >= cache->offset &&
		    (offset + len) <= (cache->offset + cache->size)) {
			memcpy(buffer,
			       &cache->data[bytes_to_quads(offset - cache->offset)],
			       len);
			return CSR1212_SUCCESS;
		}
	}
	return CSR1212_ENOENT;
}



/* Parse a chunk of data as a Config ROM */

static int csr1212_parse_bus_info_block(struct csr1212_csr *csr)
{
	struct csr1212_bus_info_block_img *bi;
	struct csr1212_cache_region *cr;
	int i;
	int ret;

	/* IEEE 1212 says that the entire bus info block should be readable in
	 * a single transaction regardless of the max_rom value.
	 * Unfortunately, many IEEE 1394 devices do not abide by that, so the
	 * bus info block will be read 1 quadlet at a time.  The rest of the
	 * ConfigROM will be read according to the max_rom field. */
	for (i = 0; i < csr->bus_info_len; i += sizeof(csr1212_quad_t)) {
		ret = csr->ops->bus_read(csr, CSR1212_CONFIG_ROM_SPACE_BASE + i,
					 sizeof(csr1212_quad_t),
					 &csr->cache_head->data[bytes_to_quads(i)],
					 csr->private);
		if (ret != CSR1212_SUCCESS)
			return ret;

		/* check ROM header's info_length */
		if (i == 0 &&
		    CSR1212_BE32_TO_CPU(csr->cache_head->data[0]) >> 24 !=
		    bytes_to_quads(csr->bus_info_len) - 1)
			return CSR1212_EINVAL;
	}

	bi = (struct csr1212_bus_info_block_img*)csr->cache_head->data;
	csr->crc_len = quads_to_bytes(bi->crc_length);

	/* IEEE 1212 recommends that crc_len be equal to bus_info_len, but that is not
	 * always the case, so read the rest of the crc area 1 quadlet at a time. */
	for (i = csr->bus_info_len; i <= csr->crc_len; i += sizeof(csr1212_quad_t)) {
		ret = csr->ops->bus_read(csr, CSR1212_CONFIG_ROM_SPACE_BASE + i,
					 sizeof(csr1212_quad_t),
					 &csr->cache_head->data[bytes_to_quads(i)],
					 csr->private);
		if (ret != CSR1212_SUCCESS)
			return ret;
	}

#if 0
	/* Apparently there are too many differnt wrong implementations of the
	 * CRC algorithm that verifying them is moot. */
	if ((csr1212_crc16(bi->data, bi->crc_length) != bi->crc) &&
	    (csr1212_msft_crc16(bi->data, bi->crc_length) != bi->crc))
		return CSR1212_EINVAL;
#endif

	cr = CSR1212_MALLOC(sizeof(*cr));
	if (!cr)
		return CSR1212_ENOMEM;

	cr->next = NULL;
	cr->prev = NULL;
	cr->offset_start = 0;
	cr->offset_end = csr->crc_len + 4;

	csr->cache_head->filled_head = cr;
	csr->cache_head->filled_tail = cr;

	return CSR1212_SUCCESS;
}

static int csr1212_parse_dir_entry(struct csr1212_keyval *dir,
				   csr1212_quad_t ki,
				   u_int32_t kv_pos)
{
	int ret = CSR1212_SUCCESS;
	struct csr1212_keyval *k = NULL;
	u_int32_t offset;

	switch(CSR1212_KV_KEY_TYPE(ki)) {
	case CSR1212_KV_TYPE_IMMEDIATE:
		k = csr1212_new_immediate(CSR1212_KV_KEY_ID(ki),
					  CSR1212_KV_VAL(ki));
		if (!k) {
			ret = CSR1212_ENOMEM;
			goto fail;
		}

		k->refcnt = 0;	/* Don't keep local reference when parsing. */
		break;

	case CSR1212_KV_TYPE_CSR_OFFSET:
		k = csr1212_new_csr_offset(CSR1212_KV_KEY_ID(ki),
					   CSR1212_KV_VAL(ki));
		if (!k) {
			ret = CSR1212_ENOMEM;
			goto fail;
		}
		k->refcnt = 0;	/* Don't keep local reference when parsing. */
		break;

	default:
		/* Compute the offset from 0xffff f000 0000. */
		offset = quads_to_bytes(CSR1212_KV_VAL(ki)) + kv_pos;
		if (offset == kv_pos) {
			/* Uh-oh.  Can't have a relative offset of 0 for Leaves
			 * or Directories.  The Config ROM image is most likely
			 * messed up, so we'll just abort here. */
			ret = CSR1212_EIO;
			goto fail;
		}

		k = csr1212_find_keyval_offset(dir, offset);

		if (k)
			break;		/* Found it. */

		if (CSR1212_KV_KEY_TYPE(ki) == CSR1212_KV_TYPE_DIRECTORY) {
			k = csr1212_new_directory(CSR1212_KV_KEY_ID(ki));
		} else {
			k = csr1212_new_leaf(CSR1212_KV_KEY_ID(ki), NULL, 0);
		}
		if (!k) {
			ret = CSR1212_ENOMEM;
			goto fail;
		}
		k->refcnt = 0;	/* Don't keep local reference when parsing. */
		k->valid = 0;	/* Contents not read yet so it's not valid. */
		k->offset = offset;

		k->prev = dir;
		k->next = dir->next;
		dir->next->prev = k;
		dir->next = k;
	}
	ret = csr1212_attach_keyval_to_directory(dir, k);

fail:
	if (ret != CSR1212_SUCCESS) {
		if (k)
			free_keyval(k);
	}
	return ret;
}


int csr1212_parse_keyval(struct csr1212_keyval *kv,
			 struct csr1212_csr_rom_cache *cache)
{
	struct csr1212_keyval_img *kvi;
	int i;
	int ret = CSR1212_SUCCESS;
	int kvi_len;

	kvi = (struct csr1212_keyval_img*)&cache->data[bytes_to_quads(kv->offset -
								      cache->offset)];
	kvi_len = CSR1212_BE16_TO_CPU(kvi->length);

#if 0
	/* Apparently there are too many differnt wrong implementations of the
	 * CRC algorithm that verifying them is moot. */
	if ((csr1212_crc16(kvi->data, kvi_len) != kvi->crc) &&
	    (csr1212_msft_crc16(kvi->data, kvi_len) != kvi->crc)) {
		ret = CSR1212_EINVAL;
		goto fail;
	}
#endif

	switch(kv->key.type) {
	case CSR1212_KV_TYPE_DIRECTORY:
		for (i = 0; i < kvi_len; i++) {
			csr1212_quad_t ki = kvi->data[i];

			/* Some devices put null entries in their unit
			 * directories.  If we come across such an entry,
			 * then skip it. */
			if (ki == 0x0)
				continue;
			ret = csr1212_parse_dir_entry(kv, ki,
						      (kv->offset +
						       quads_to_bytes(i + 1)));
		}
		kv->value.directory.len = kvi_len;
		break;

	case CSR1212_KV_TYPE_LEAF:
		if (kv->key.id != CSR1212_KV_ID_EXTENDED_ROM) {
			kv->value.leaf.data = CSR1212_MALLOC(quads_to_bytes(kvi_len));
			if (!kv->value.leaf.data) {
				ret = CSR1212_ENOMEM;
				goto fail;
			}

			kv->value.leaf.len = kvi_len;
			memcpy(kv->value.leaf.data, kvi->data, quads_to_bytes(kvi_len));
		}
		break;
	}

	kv->valid = 1;

fail:
	return ret;
}


int _csr1212_read_keyval(struct csr1212_csr *csr, struct csr1212_keyval *kv)
{
	struct csr1212_cache_region *cr, *ncr, *newcr = NULL;
	struct csr1212_keyval_img *kvi = NULL;
	struct csr1212_csr_rom_cache *cache;
	int cache_index;
	u_int64_t addr;
	u_int32_t *cache_ptr;
	u_int16_t kv_len = 0;

	if (!csr || !kv || csr->max_rom < 1)
		return CSR1212_EINVAL;

	/* First find which cache the data should be in (or go in if not read
	 * yet). */
	for (cache = csr->cache_head; cache; cache = cache->next) {
		if (kv->offset >= cache->offset &&
		    kv->offset < (cache->offset + cache->size))
			break;
	}

	if (!cache) {
		csr1212_quad_t q;
		u_int32_t cache_size;

		/* Only create a new cache for Extended ROM leaves. */
		if (kv->key.id != CSR1212_KV_ID_EXTENDED_ROM)
			return CSR1212_EINVAL;

		if (csr->ops->bus_read(csr,
				       CSR1212_REGISTER_SPACE_BASE + kv->offset,
				       sizeof(csr1212_quad_t), &q, csr->private)) {
			return CSR1212_EIO;
		}

		kv->value.leaf.len = CSR1212_BE32_TO_CPU(q) >> 16;

		cache_size = (quads_to_bytes(kv->value.leaf.len + 1) +
			      (csr->max_rom - 1)) & ~(csr->max_rom - 1);

		cache = csr1212_rom_cache_malloc(kv->offset, cache_size);
		if (!cache)
			return CSR1212_ENOMEM;

		kv->value.leaf.data = &cache->data[1];
		csr->cache_tail->next = cache;
		cache->prev = csr->cache_tail;
		cache->next = NULL;
		csr->cache_tail = cache;
		cache->filled_head =
			CSR1212_MALLOC(sizeof(*cache->filled_head));
		if (!cache->filled_head) {
			return CSR1212_ENOMEM;
		}

		cache->filled_head->offset_start = 0;
		cache->filled_head->offset_end = sizeof(csr1212_quad_t);
		cache->filled_tail = cache->filled_head;
		cache->filled_head->next = NULL;
		cache->filled_head->prev = NULL;
		cache->data[0] = q;

		/* Don't read the entire extended ROM now.  Pieces of it will
		 * be read when entries inside it are read. */
		return csr1212_parse_keyval(kv, cache);
	}

	cache_index = kv->offset - cache->offset;

	/* Now seach read portions of the cache to see if it is there. */
	for (cr = cache->filled_head; cr; cr = cr->next) {
		if (cache_index < cr->offset_start) {
			newcr = CSR1212_MALLOC(sizeof(*newcr));
			if (!newcr)
				return CSR1212_ENOMEM;

			newcr->offset_start = cache_index & ~(csr->max_rom - 1);
			newcr->offset_end = newcr->offset_start;
			newcr->next = cr;
			newcr->prev = cr->prev;
			cr->prev = newcr;
			cr = newcr;
			break;
		} else if ((cache_index >= cr->offset_start) &&
			   (cache_index < cr->offset_end)) {
			kvi = (struct csr1212_keyval_img*)
				(&cache->data[bytes_to_quads(cache_index)]);
			kv_len = quads_to_bytes(CSR1212_BE16_TO_CPU(kvi->length) +
						1);
			break;
		} else if (cache_index == cr->offset_end)
			break;
	}

	if (!cr) {
		cr = cache->filled_tail;
		newcr = CSR1212_MALLOC(sizeof(*newcr));
		if (!newcr)
			return CSR1212_ENOMEM;

		newcr->offset_start = cache_index & ~(csr->max_rom - 1);
		newcr->offset_end = newcr->offset_start;
		newcr->prev = cr;
		newcr->next = cr->next;
		cr->next = newcr;
		cr = newcr;
		cache->filled_tail = newcr;
	}

	while(!kvi || cr->offset_end < cache_index + kv_len) {
		cache_ptr = &cache->data[bytes_to_quads(cr->offset_end &
							~(csr->max_rom - 1))];

		addr = (CSR1212_CSR_ARCH_REG_SPACE_BASE + cache->offset +
			cr->offset_end) & ~(csr->max_rom - 1);

		if (csr->ops->bus_read(csr, addr, csr->max_rom, cache_ptr,
				       csr->private)) {
			if (csr->max_rom == 4)
				/* We've got problems! */
				return CSR1212_EIO;

			/* Apperently the max_rom value was a lie, set it to
			 * do quadlet reads and try again. */
			csr->max_rom = 4;
			continue;
		}

		cr->offset_end += csr->max_rom - (cr->offset_end &
						  (csr->max_rom - 1));

		if (!kvi && (cr->offset_end > cache_index)) {
			kvi = (struct csr1212_keyval_img*)
				(&cache->data[bytes_to_quads(cache_index)]);
			kv_len = quads_to_bytes(CSR1212_BE16_TO_CPU(kvi->length) +
						1);
		}

		if ((kv_len + (kv->offset - cache->offset)) > cache->size) {
			/* The Leaf or Directory claims its length extends
			 * beyond the ConfigROM image region and thus beyond the
			 * end of our cache region.  Therefore, we abort now
			 * rather than seg faulting later. */
			return CSR1212_EIO;
		}

		ncr = cr->next;

		if (ncr && (cr->offset_end >= ncr->offset_start)) {
			/* consolidate region entries */
			ncr->offset_start = cr->offset_start;

			if (cr->prev)
				cr->prev->next = cr->next;
			ncr->prev = cr->prev;
			if (cache->filled_head == cr)
				cache->filled_head = ncr;
			CSR1212_FREE(cr);
			cr = ncr;
		}
	}

	return csr1212_parse_keyval(kv, cache);
}



int csr1212_parse_csr(struct csr1212_csr *csr)
{
	static const int mr_map[] = { 4, 64, 1024, 0 };
	struct csr1212_dentry *dentry;
	int ret;

	if (!csr || !csr->ops || !csr->ops->bus_read)
		return CSR1212_EINVAL;

	ret = csr1212_parse_bus_info_block(csr);
	if (ret != CSR1212_SUCCESS)
		return ret;

	if (!csr->ops->get_max_rom)
		csr->max_rom = mr_map[0];	/* default value */
	else {
		int i = csr->ops->get_max_rom(csr->bus_info_data,
					      csr->private);
		if (i & ~0x3)
			return CSR1212_EINVAL;
		csr->max_rom = mr_map[i];
	}

	csr->cache_head->layout_head = csr->root_kv;
	csr->cache_head->layout_tail = csr->root_kv;

	csr->root_kv->offset = (CSR1212_CONFIG_ROM_SPACE_BASE & 0xffff) +
		csr->bus_info_len;

	csr->root_kv->valid = 0;
	csr->root_kv->next = csr->root_kv;
	csr->root_kv->prev = csr->root_kv;
	ret = _csr1212_read_keyval(csr, csr->root_kv);
	if (ret != CSR1212_SUCCESS)
		return ret;

	/* Scan through the Root directory finding all extended ROM regions
	 * and make cache regions for them */
	for (dentry = csr->root_kv->value.directory.dentries_head;
	     dentry; dentry = dentry->next) {
		if (dentry->kv->key.id == CSR1212_KV_ID_EXTENDED_ROM &&
			!dentry->kv->valid) {
			ret = _csr1212_read_keyval(csr, dentry->kv);
			if (ret != CSR1212_SUCCESS)
				return ret;
		}
	}

	return CSR1212_SUCCESS;
}
