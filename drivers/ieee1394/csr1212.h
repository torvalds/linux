/*
 * csr1212.h -- IEEE 1212 Control and Status Register support for Linux
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

#ifndef __CSR1212_H__
#define __CSR1212_H__

#include <linux/types.h>
#include <linux/slab.h>

#define CSR1212_MALLOC(size)	kmalloc((size), GFP_KERNEL)
#define CSR1212_FREE(ptr)	kfree(ptr)

#define CSR1212_SUCCESS (0)


/* CSR 1212 key types */
#define CSR1212_KV_TYPE_IMMEDIATE		0
#define CSR1212_KV_TYPE_CSR_OFFSET		1
#define CSR1212_KV_TYPE_LEAF			2
#define CSR1212_KV_TYPE_DIRECTORY		3


/* CSR 1212 key ids */
#define CSR1212_KV_ID_DESCRIPTOR		0x01
#define CSR1212_KV_ID_BUS_DEPENDENT_INFO	0x02
#define CSR1212_KV_ID_VENDOR			0x03
#define CSR1212_KV_ID_HARDWARE_VERSION		0x04
#define CSR1212_KV_ID_MODULE			0x07
#define CSR1212_KV_ID_NODE_CAPABILITIES		0x0C
#define CSR1212_KV_ID_EUI_64			0x0D
#define CSR1212_KV_ID_UNIT			0x11
#define CSR1212_KV_ID_SPECIFIER_ID		0x12
#define CSR1212_KV_ID_VERSION			0x13
#define CSR1212_KV_ID_DEPENDENT_INFO		0x14
#define CSR1212_KV_ID_UNIT_LOCATION		0x15
#define CSR1212_KV_ID_MODEL			0x17
#define CSR1212_KV_ID_INSTANCE			0x18
#define CSR1212_KV_ID_KEYWORD			0x19
#define CSR1212_KV_ID_FEATURE			0x1A
#define CSR1212_KV_ID_EXTENDED_ROM		0x1B
#define CSR1212_KV_ID_EXTENDED_KEY_SPECIFIER_ID	0x1C
#define CSR1212_KV_ID_EXTENDED_KEY		0x1D
#define CSR1212_KV_ID_EXTENDED_DATA		0x1E
#define CSR1212_KV_ID_MODIFIABLE_DESCRIPTOR	0x1F
#define CSR1212_KV_ID_DIRECTORY_ID		0x20
#define CSR1212_KV_ID_REVISION			0x21


/* IEEE 1212 Address space map */
#define CSR1212_ALL_SPACE_BASE			(0x000000000000ULL)
#define CSR1212_ALL_SPACE_SIZE			(1ULL << 48)
#define CSR1212_ALL_SPACE_END			(CSR1212_ALL_SPACE_BASE + CSR1212_ALL_SPACE_SIZE)

#define  CSR1212_MEMORY_SPACE_BASE		(0x000000000000ULL)
#define  CSR1212_MEMORY_SPACE_SIZE		((256ULL * (1ULL << 40)) - (512ULL * (1ULL << 20)))
#define  CSR1212_MEMORY_SPACE_END		(CSR1212_MEMORY_SPACE_BASE + CSR1212_MEMORY_SPACE_SIZE)

#define  CSR1212_PRIVATE_SPACE_BASE		(0xffffe0000000ULL)
#define  CSR1212_PRIVATE_SPACE_SIZE		(256ULL * (1ULL << 20))
#define  CSR1212_PRIVATE_SPACE_END		(CSR1212_PRIVATE_SPACE_BASE + CSR1212_PRIVATE_SPACE_SIZE)

#define  CSR1212_REGISTER_SPACE_BASE		(0xfffff0000000ULL)
#define  CSR1212_REGISTER_SPACE_SIZE		(256ULL * (1ULL << 20))
#define  CSR1212_REGISTER_SPACE_END		(CSR1212_REGISTER_SPACE_BASE + CSR1212_REGISTER_SPACE_SIZE)

#define  CSR1212_CSR_ARCH_REG_SPACE_BASE	(0xfffff0000000ULL)
#define  CSR1212_CSR_ARCH_REG_SPACE_SIZE	(512)
#define  CSR1212_CSR_ARCH_REG_SPACE_END		(CSR1212_CSR_ARCH_REG_SPACE_BASE + CSR1212_CSR_ARCH_REG_SPACE_SIZE)
#define  CSR1212_CSR_ARCH_REG_SPACE_OFFSET	(CSR1212_CSR_ARCH_REG_SPACE_BASE - CSR1212_REGISTER_SPACE_BASE)

#define  CSR1212_CSR_BUS_DEP_REG_SPACE_BASE	(0xfffff0000200ULL)
#define  CSR1212_CSR_BUS_DEP_REG_SPACE_SIZE	(512)
#define  CSR1212_CSR_BUS_DEP_REG_SPACE_END	(CSR1212_CSR_BUS_DEP_REG_SPACE_BASE + CSR1212_CSR_BUS_DEP_REG_SPACE_SIZE)
#define  CSR1212_CSR_BUS_DEP_REG_SPACE_OFFSET	(CSR1212_CSR_BUS_DEP_REG_SPACE_BASE - CSR1212_REGISTER_SPACE_BASE)

#define  CSR1212_CONFIG_ROM_SPACE_BASE		(0xfffff0000400ULL)
#define  CSR1212_CONFIG_ROM_SPACE_SIZE		(1024)
#define  CSR1212_CONFIG_ROM_SPACE_END		(CSR1212_CONFIG_ROM_SPACE_BASE + CSR1212_CONFIG_ROM_SPACE_SIZE)
#define  CSR1212_CONFIG_ROM_SPACE_OFFSET	(CSR1212_CONFIG_ROM_SPACE_BASE - CSR1212_REGISTER_SPACE_BASE)

#define  CSR1212_UNITS_SPACE_BASE		(0xfffff0000800ULL)
#define  CSR1212_UNITS_SPACE_SIZE		((256ULL * (1ULL << 20)) - 2048)
#define  CSR1212_UNITS_SPACE_END		(CSR1212_UNITS_SPACE_BASE + CSR1212_UNITS_SPACE_SIZE)
#define  CSR1212_UNITS_SPACE_OFFSET		(CSR1212_UNITS_SPACE_BASE - CSR1212_REGISTER_SPACE_BASE)

#define  CSR1212_INVALID_ADDR_SPACE		-1


/* Config ROM image structures */
struct csr1212_bus_info_block_img {
	u8 length;
	u8 crc_length;
	u16 crc;

	/* Must be last */
	u32 data[0];	/* older gcc can't handle [] which is standard */
};

struct csr1212_leaf {
	int len;
	u32 *data;
};

struct csr1212_dentry {
	struct csr1212_dentry *next, *prev;
	struct csr1212_keyval *kv;
};

struct csr1212_directory {
	int len;
	struct csr1212_dentry *dentries_head, *dentries_tail;
};

struct csr1212_keyval {
	struct {
		u8 type;
		u8 id;
	} key;
	union {
		u32 immediate;
		u32 csr_offset;
		struct csr1212_leaf leaf;
		struct csr1212_directory directory;
	} value;
	struct csr1212_keyval *associate;
	int refcnt;

	/* used in generating and/or parsing CSR image */
	struct csr1212_keyval *next, *prev;	/* flat list of CSR elements */
	u32 offset;	/* position in CSR from 0xffff f000 0000 */
	u8 valid;	/* flag indicating keyval has valid data*/
};


struct csr1212_cache_region {
	struct csr1212_cache_region *next, *prev;
	u32 offset_start;	/* inclusive */
	u32 offset_end;		/* exclusive */
};

struct csr1212_csr_rom_cache {
	struct csr1212_csr_rom_cache *next, *prev;
	struct csr1212_cache_region *filled_head, *filled_tail;
	struct csr1212_keyval *layout_head, *layout_tail;
	size_t size;
	u32 offset;
	struct csr1212_keyval *ext_rom;
	size_t len;

	/* Must be last */
	u32 data[0];	/* older gcc can't handle [] which is standard */
};

struct csr1212_csr {
	size_t bus_info_len;	/* bus info block length in bytes */
	size_t crc_len;		/* crc length in bytes */
	u32 *bus_info_data;	/* bus info data incl bus name and EUI */

	void *private;		/* private, bus specific data */
	struct csr1212_bus_ops *ops;

	struct csr1212_keyval *root_kv;

	int max_rom;		/* max bytes readable in Config ROM region */

	/* Items below used for image parsing and generation */
	struct csr1212_csr_rom_cache *cache_head, *cache_tail;
};

struct csr1212_bus_ops {
	/* This function is used by csr1212 to read additional information
	 * from remote nodes when parsing a Config ROM (i.e., read Config ROM
	 * entries located in the Units Space.  Must return 0 on success
	 * anything else indicates an error. */
	int (*bus_read) (struct csr1212_csr *csr, u64 addr,
			 u16 length, void *buffer, void *private);

	/* This function is used by csr1212 to allocate a region in units space
	 * in the event that Config ROM entries don't all fit in the predefined
	 * 1K region.  The void *private parameter is private member of struct
	 * csr1212_csr. */
	u64 (*allocate_addr_range) (u64 size, u32 alignment, void *private);

	/* This function is used by csr1212 to release a region in units space
	 * that is no longer needed. */
	void (*release_addr) (u64 addr, void *private);

	/* This function is used by csr1212 to determine the max read request
	 * supported by a remote node when reading the ConfigROM space.  Must
	 * return 0, 1, or 2 per IEEE 1212.  */
	int (*get_max_rom) (u32 *bus_info, void *private);
};


/* Descriptor Leaf manipulation macros */
#define CSR1212_DESCRIPTOR_LEAF_TYPE_SHIFT 24
#define CSR1212_DESCRIPTOR_LEAF_SPECIFIER_ID_MASK 0xffffff
#define CSR1212_DESCRIPTOR_LEAF_OVERHEAD (1 * sizeof(u32))

#define CSR1212_DESCRIPTOR_LEAF_TYPE(kv) \
	(be32_to_cpu((kv)->value.leaf.data[0]) >> \
	 CSR1212_DESCRIPTOR_LEAF_TYPE_SHIFT)
#define CSR1212_DESCRIPTOR_LEAF_SPECIFIER_ID(kv) \
	(be32_to_cpu((kv)->value.leaf.data[0]) & \
	 CSR1212_DESCRIPTOR_LEAF_SPECIFIER_ID_MASK)


/* Text Descriptor Leaf manipulation macros */
#define CSR1212_TEXTUAL_DESCRIPTOR_LEAF_WIDTH_SHIFT 28
#define CSR1212_TEXTUAL_DESCRIPTOR_LEAF_WIDTH_MASK 0xf /* after shift */
#define CSR1212_TEXTUAL_DESCRIPTOR_LEAF_CHAR_SET_SHIFT 16
#define CSR1212_TEXTUAL_DESCRIPTOR_LEAF_CHAR_SET_MASK 0xfff  /* after shift */
#define CSR1212_TEXTUAL_DESCRIPTOR_LEAF_LANGUAGE_MASK 0xffff
#define CSR1212_TEXTUAL_DESCRIPTOR_LEAF_OVERHEAD (1 * sizeof(u32))

#define CSR1212_TEXTUAL_DESCRIPTOR_LEAF_WIDTH(kv) \
	(be32_to_cpu((kv)->value.leaf.data[1]) >> \
	 CSR1212_TEXTUAL_DESCRIPTOR_LEAF_WIDTH_SHIFT)
#define CSR1212_TEXTUAL_DESCRIPTOR_LEAF_CHAR_SET(kv) \
	((be32_to_cpu((kv)->value.leaf.data[1]) >> \
	  CSR1212_TEXTUAL_DESCRIPTOR_LEAF_CHAR_SET_SHIFT) & \
	 CSR1212_TEXTUAL_DESCRIPTOR_LEAF_CHAR_SET_MASK)
#define CSR1212_TEXTUAL_DESCRIPTOR_LEAF_LANGUAGE(kv) \
	(be32_to_cpu((kv)->value.leaf.data[1]) & \
	 CSR1212_TEXTUAL_DESCRIPTOR_LEAF_LANGUAGE_MASK)
#define CSR1212_TEXTUAL_DESCRIPTOR_LEAF_DATA(kv) \
	(&((kv)->value.leaf.data[2]))


/* The following 2 function are for creating new Configuration ROM trees.  The
 * first function is used for both creating local trees and parsing remote
 * trees.  The second function adds pertinent information to local Configuration
 * ROM trees - namely data for the bus information block. */
extern struct csr1212_csr *csr1212_create_csr(struct csr1212_bus_ops *ops,
					      size_t bus_info_size,
					      void *private);
extern void csr1212_init_local_csr(struct csr1212_csr *csr,
				   const u32 *bus_info_data, int max_rom);


/* Destroy a Configuration ROM tree and release all memory taken by the tree. */
extern void csr1212_destroy_csr(struct csr1212_csr *csr);


/* The following set of functions are fore creating new keyvals for placement in
 * a Configuration ROM tree.  Code that creates new keyvals with these functions
 * must release those keyvals with csr1212_release_keyval() when they are no
 * longer needed. */
extern struct csr1212_keyval *csr1212_new_immediate(u8 key, u32 value);
extern struct csr1212_keyval *csr1212_new_directory(u8 key);
extern struct csr1212_keyval *csr1212_new_string_descriptor_leaf(const char *s);


/* The following function manages association between keyvals.  Typically,
 * Descriptor Leaves and Directories will be associated with another keyval and
 * it is desirable for the Descriptor keyval to be place immediately after the
 * keyval that it is associated with.
 * Take care with subsequent ROM modifications:  There is no function to remove
 * previously specified associations.
 */
extern void csr1212_associate_keyval(struct csr1212_keyval *kv,
				     struct csr1212_keyval *associate);


/* The following functions manage the association of a keyval and directories.
 * A keyval may be attached to more than one directory. */
extern int csr1212_attach_keyval_to_directory(struct csr1212_keyval *dir,
					      struct csr1212_keyval *kv);
extern void csr1212_detach_keyval_from_directory(struct csr1212_keyval *dir,
						 struct csr1212_keyval *kv);


/* Creates a complete Configuration ROM image in the list of caches available
 * via csr->cache_head. */
extern int csr1212_generate_csr_image(struct csr1212_csr *csr);


/* This is a convience function for reading a block of data out of one of the
 * caches in the csr->cache_head list. */
extern int csr1212_read(struct csr1212_csr *csr, u32 offset, void *buffer,
			u32 len);


/* The following functions are in place for parsing Configuration ROM images.
 * csr1212_parse_keyval() is used should there be a need to directly parse a
 * Configuration ROM directly. */
extern int csr1212_parse_keyval(struct csr1212_keyval *kv,
				struct csr1212_csr_rom_cache *cache);
extern int csr1212_parse_csr(struct csr1212_csr *csr);


/* This function allocates a new cache which may be used for either parsing or
 * generating sub-sets of Configuration ROM images. */
static inline struct csr1212_csr_rom_cache *
csr1212_rom_cache_malloc(u32 offset, size_t size)
{
	struct csr1212_csr_rom_cache *cache;

	cache = CSR1212_MALLOC(sizeof(*cache) + size);
	if (!cache)
		return NULL;

	cache->next = NULL;
	cache->prev = NULL;
	cache->filled_head = NULL;
	cache->filled_tail = NULL;
	cache->layout_head = NULL;
	cache->layout_tail = NULL;
	cache->offset = offset;
	cache->size = size;
	cache->ext_rom = NULL;

	return cache;
}


/* This function ensures that a keyval contains data when referencing a keyval
 * created by parsing a Configuration ROM. */
extern struct csr1212_keyval *
csr1212_get_keyval(struct csr1212_csr *csr, struct csr1212_keyval *kv);


/* This function increments the reference count for a keyval should there be a
 * need for code to retain a keyval that has been parsed. */
static inline void csr1212_keep_keyval(struct csr1212_keyval *kv)
{
	kv->refcnt++;
}


/* This function decrements a keyval's reference count and will destroy the
 * keyval when there are no more users of the keyval.  This should be called by
 * any code that calls csr1212_keep_keyval() or any of the keyval creation
 * routines csr1212_new_*(). */
extern void csr1212_release_keyval(struct csr1212_keyval *kv);


/*
 * This macro allows for looping over the keyval entries in a directory and it
 * ensures that keyvals from remote ConfigROMs are parsed properly.
 *
 * struct csr1212_csr *_csr points to the CSR associated with dir.
 * struct csr1212_keyval *_kv points to the current keyval (loop index).
 * struct csr1212_keyval *_dir points to the directory to be looped.
 * struct csr1212_dentry *_pos is used internally for indexing.
 *
 * kv will be NULL upon exit of the loop.
 */
#define csr1212_for_each_dir_entry(_csr, _kv, _dir, _pos)		\
	for (csr1212_get_keyval((_csr), (_dir)),			\
	     _pos = (_dir)->value.directory.dentries_head,		\
	     _kv = (_pos) ? csr1212_get_keyval((_csr), _pos->kv) : NULL;\
	     (_kv) && (_pos);						\
	     (_kv->associate == NULL) ?					\
		     ((_pos = _pos->next), (_kv = (_pos) ?		\
				csr1212_get_keyval((_csr), _pos->kv) :	\
				NULL)) :				\
		     (_kv = csr1212_get_keyval((_csr), _kv->associate)))

#endif /* __CSR1212_H__ */
